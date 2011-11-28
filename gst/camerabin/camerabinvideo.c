/*
 * GStreamer
 * Copyright (C) 2008 Nokia Corporation <multimedia@maemo.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:camerabinvideo
 * @short_description: video recording module of #GstCameraBin
 *
 * <refsect2>
 * <para>
 *
 * The pipeline for this module is:
 *
 * <informalexample>
 * <programlisting>
 *-----------------------------------------------------------------------------
 * audiosrc -> audio_queue -> audioconvert -> volume -> audioenc
 *                                                       > videomux -> filesink
 *                       video_queue -> [timeoverlay] -> [csp] -> videoenc -> queue
 * -> [post proc] -> tee <
 *                       queue ->
 *-----------------------------------------------------------------------------
 * </programlisting>
 * </informalexample>
 *
 * The properties of elements are:
 *
 * queue - "leaky", 2 (Leaky on downstream (old buffers))
 *
 * </para>
 * </refsect2>
 */

/*
 * includes
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "camerabindebug.h"
#include "camerabingeneral.h"

#include "camerabinvideo.h"

/*
 * defines and static global vars
 */

/* internal element names */

#define DEFAULT_AUD_ENC "vorbisenc"
#define DEFAULT_VID_ENC "theoraenc"
#define DEFAULT_MUX "oggmux"
#define DEFAULT_SINK "filesink"

#define DEFAULT_FLAGS 0

enum
{
  PROP_0,
  PROP_FILENAME
};

static void gst_camerabin_video_dispose (GstCameraBinVideo * sink);
static void gst_camerabin_video_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_camerabin_video_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn
gst_camerabin_video_change_state (GstElement * element,
    GstStateChange transition);

static
    gboolean camerabin_video_pad_tee_src0_have_buffer (GstPad * pad,
    GstBuffer * buffer, gpointer u_data);
static gboolean camerabin_video_sink_have_event (GstPad * pad, GstEvent * event,
    gpointer u_data);
static gboolean gst_camerabin_video_create_elements (GstCameraBinVideo * vid);
static void gst_camerabin_video_destroy_elements (GstCameraBinVideo * vid);

GST_BOILERPLATE (GstCameraBinVideo, gst_camerabin_video, GstBin, GST_TYPE_BIN);

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);


/* GObject methods implementation */

static void
gst_camerabin_video_base_init (gpointer klass)
{
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (eklass, &sink_template);
  gst_element_class_add_static_pad_template (eklass, &src_template);
  gst_element_class_set_details_simple (eklass,
      "Video capture bin for camerabin", "Bin/Video",
      "Process and store video data",
      "Edgard Lima <edgard.lima@indt.org.br>, "
      "Nokia Corporation <multimedia@maemo.org>");
}

static void
gst_camerabin_video_class_init (GstCameraBinVideoClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->dispose =
      (GObjectFinalizeFunc) GST_DEBUG_FUNCPTR (gst_camerabin_video_dispose);
  eklass->change_state = GST_DEBUG_FUNCPTR (gst_camerabin_video_change_state);

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_camerabin_video_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_camerabin_video_get_property);

  /**
   * GstCameraBinVideo:filename:
   *
   * This property can be used to specify the filename of the video.
   *
   **/
  g_object_class_install_property (gobject_class, PROP_FILENAME,
      g_param_spec_string ("filename", "Filename",
          "Filename of the video to save", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_camerabin_video_init (GstCameraBinVideo * vid,
    GstCameraBinVideoClass * g_class)
{
  vid->filename = g_string_new ("");

  vid->app_post = NULL;
  vid->app_vid_enc = NULL;
  vid->app_aud_enc = NULL;
  vid->app_aud_src = NULL;
  vid->app_mux = NULL;

  vid->aud_src = NULL;
  vid->sink = NULL;
  vid->tee = NULL;
  vid->volume = NULL;
  vid->video_queue = NULL;

  vid->tee_video_srcpad = NULL;
  vid->tee_vf_srcpad = NULL;

  vid->pending_eos = NULL;

  vid->mute = ARG_DEFAULT_MUTE;
  vid->flags = DEFAULT_FLAGS;

  vid->vid_src_probe_id = 0;
  vid->vid_tee_probe_id = 0;
  vid->vid_sink_probe_id = 0;

  /* Create src and sink ghost pads */
  vid->sinkpad = gst_ghost_pad_new_no_target ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (vid), vid->sinkpad);

  vid->srcpad = gst_ghost_pad_new_no_target ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (vid), vid->srcpad);

  /* Add probe for handling eos when stopping recording */
  vid->vid_sink_probe_id = gst_pad_add_event_probe (vid->sinkpad,
      G_CALLBACK (camerabin_video_sink_have_event), vid);
}

static void
gst_camerabin_video_dispose (GstCameraBinVideo * vid)
{
  GST_DEBUG_OBJECT (vid, "disposing");

  g_string_free (vid->filename, TRUE);
  vid->filename = NULL;

  if (vid->vid_sink_probe_id) {
    gst_pad_remove_event_probe (vid->sinkpad, vid->vid_sink_probe_id);
    vid->vid_sink_probe_id = 0;
  }

  /* Note: if videobin was never set to READY state the
     ownership of elements created by application were never
     taken by bin and therefore gst_object_sink is called for
     these elements (they may still be in floating state
     and not unreffed properly without sinking first)
   */
  if (vid->app_post) {
    gst_object_sink (vid->app_post);
    gst_object_unref (vid->app_post);
    vid->app_post = NULL;
  }

  if (vid->app_vid_enc) {
    gst_object_sink (vid->app_vid_enc);
    gst_object_unref (vid->app_vid_enc);
    vid->app_vid_enc = NULL;
  }

  if (vid->app_aud_enc) {
    gst_object_sink (vid->app_aud_enc);
    gst_object_unref (vid->app_aud_enc);
    vid->app_aud_enc = NULL;
  }

  if (vid->app_aud_src) {
    gst_object_sink (vid->app_aud_src);
    gst_object_unref (vid->app_aud_src);
    vid->app_aud_src = NULL;
  }

  if (vid->app_mux) {
    gst_object_sink (vid->app_mux);
    gst_object_unref (vid->app_mux);
    vid->app_mux = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose ((GObject *) vid);
}


static void
gst_camerabin_video_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCameraBinVideo *bin = GST_CAMERABIN_VIDEO (object);

  switch (prop_id) {
    case PROP_FILENAME:
      g_string_assign (bin->filename, g_value_get_string (value));
      GST_INFO_OBJECT (bin, "received filename: '%s'", bin->filename->str);
      if (bin->sink) {
        g_object_set (G_OBJECT (bin->sink), "location", bin->filename->str,
            NULL);
      } else {
        GST_INFO_OBJECT (bin, "no sink, not setting name yet");
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_camerabin_video_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCameraBinVideo *bin = GST_CAMERABIN_VIDEO (object);

  switch (prop_id) {
    case PROP_FILENAME:
      g_value_set_string (value, bin->filename->str);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_camerabin_video_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstCameraBinVideo *vid = GST_CAMERABIN_VIDEO (element);

  GST_DEBUG_OBJECT (element, "changing state: %s -> %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_camerabin_video_create_elements (vid)) {
        return GST_STATE_CHANGE_FAILURE;
      }
      /* Don't change sink to READY yet to allow changing the
         filename in READY state. */
      gst_element_set_locked_state (vid->sink, TRUE);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      vid->calculate_adjust_ts_video = TRUE;
      g_object_set (G_OBJECT (vid->sink), "async", FALSE, NULL);
      gst_element_set_locked_state (vid->sink, FALSE);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      vid->calculate_adjust_ts_video = TRUE;
      break;

    case GST_STATE_CHANGE_PAUSED_TO_READY:
      /* Set sink to NULL in order to write the file _now_ */
      GST_INFO ("write video file: %s", vid->filename->str);
      gst_element_set_locked_state (vid->sink, TRUE);
      gst_element_set_state (vid->sink, GST_STATE_NULL);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      /* Write debug graph to file */
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (GST_ELEMENT_PARENT (vid)),
          GST_DEBUG_GRAPH_SHOW_MEDIA_TYPE |
          GST_DEBUG_GRAPH_SHOW_NON_DEFAULT_PARAMS, "videobin.playing");

      if (vid->pending_eos) {
        /* Video bin is still paused, so push eos directly to video queue */
        GST_DEBUG_OBJECT (vid, "pushing pending eos");
        gst_pad_push_event (vid->tee_video_srcpad, vid->pending_eos);
        vid->pending_eos = NULL;
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      /* Reset counters related to timestamp rewriting */
      vid->adjust_ts_video = 0;
      vid->last_ts_video = 0;

      if (vid->pending_eos) {
        gst_event_unref (vid->pending_eos);
        vid->pending_eos = NULL;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_camerabin_video_destroy_elements (vid);
      break;
    default:
      break;
  }

  GST_DEBUG_OBJECT (element, "changed state: %s -> %s = %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)),
      gst_element_state_change_return_get_name (ret));

  return ret;
}

/*
 * static helper functions implementation
 */

/*
 * camerabin_video_pad_tee_src0_have_buffer:
 * @pad: tee src pad leading to video encoding
 * @event: received buffer
 * @u_data: video bin object
 *
 * Buffer probe for rewriting video buffer timestamps.
 *
 * Returns: TRUE always
 */
static gboolean
camerabin_video_pad_tee_src0_have_buffer (GstPad * pad, GstBuffer * buffer,
    gpointer u_data)
{
  GstCameraBinVideo *vid = (GstCameraBinVideo *) u_data;

  GST_LOG ("buffer in with size %d ts %" GST_TIME_FORMAT,
      GST_BUFFER_SIZE (buffer), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)));

  if (G_UNLIKELY (vid->calculate_adjust_ts_video)) {
    GstEvent *event;
    GstObject *tee;
    GstPad *sinkpad;

    vid->adjust_ts_video = GST_BUFFER_TIMESTAMP (buffer) - vid->last_ts_video;
    vid->calculate_adjust_ts_video = FALSE;
    event = gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME,
        0, GST_CLOCK_TIME_NONE, vid->last_ts_video);
    /* Send the newsegment to both view finder and video bin */
    tee = gst_pad_get_parent (pad);
    sinkpad = gst_element_get_static_pad (GST_ELEMENT (tee), "sink");
    gst_pad_send_event (sinkpad, event);
    gst_object_unref (tee);
    gst_object_unref (sinkpad);
    GST_LOG_OBJECT (vid, "vid ts adjustment: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (vid->adjust_ts_video));
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
  }
  GST_BUFFER_TIMESTAMP (buffer) -= vid->adjust_ts_video;
  vid->last_ts_video = GST_BUFFER_TIMESTAMP (buffer);
  if (GST_BUFFER_DURATION_IS_VALID (buffer))
    vid->last_ts_video += GST_BUFFER_DURATION (buffer);

  GST_LOG ("buffer out with size %d ts %" GST_TIME_FORMAT,
      GST_BUFFER_SIZE (buffer), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)));
  return TRUE;
}

/*
 * camerabin_video_sink_have_event:
 * @pad: video bin sink pad
 * @event: received event
 * @u_data: video bin object
 *
 * Event probe for video bin eos handling.
 * Copies the eos event to audio branch of video bin.
 *
 * Returns: FALSE to drop the event, TRUE otherwise
 */
static gboolean
camerabin_video_sink_have_event (GstPad * pad, GstEvent * event,
    gpointer u_data)
{
  GstCameraBinVideo *vid = (GstCameraBinVideo *) u_data;
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (vid, "got videobin sink event: %s",
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      if (vid->aud_src) {
        GST_DEBUG_OBJECT (vid, "copying %s to audio branch",
            GST_EVENT_TYPE_NAME (event));
        gst_element_send_event (vid->aud_src, gst_event_copy (event));
      }

      /* If we're paused, we can't pass eos to video now to avoid blocking.
         Instead send eos when changing to playing next time. */
      if (GST_STATE (GST_ELEMENT (vid)) == GST_STATE_PAUSED) {
        GST_DEBUG_OBJECT (vid, "paused, delay eos sending");
        vid->pending_eos = gst_event_ref (event);
        ret = FALSE;            /* Drop the event */
      }
      break;
    default:
      break;
  }
  return ret;
}

/*
 * gst_camerabin_video_create_elements:
 * @vid: a pointer to #GstCameraBinVideo
 *
 * This function creates the needed #GstElements and resources to record videos.
 * Use gst_camerabin_video_destroy_elements() to free these resources.
 *
 * Returns: %TRUE if succeeded or FALSE if failed
 */
static gboolean
gst_camerabin_video_create_elements (GstCameraBinVideo * vid)
{
  GstPad *vid_sinkpad = NULL, *vid_srcpad = NULL;
  GstBin *vidbin = GST_BIN (vid);
  GstElement *queue = NULL;

  vid->adjust_ts_video = 0;
  vid->last_ts_video = 0;
  vid->calculate_adjust_ts_video = FALSE;

  /* Add video post processing element if any */
  if (vid->app_post) {
    if (!gst_camerabin_add_element (vidbin, vid->app_post)) {
      goto error;
    }
    vid_sinkpad = gst_element_get_static_pad (vid->app_post, "sink");
  }

  /* Add tee element */
  if (!(vid->tee =
          gst_camerabin_create_and_add_element (vidbin, "tee", "video-tee"))) {
    goto error;
  }

  /* Set up sink ghost pad for video bin */
  if (!vid_sinkpad) {
    vid_sinkpad = gst_element_get_static_pad (vid->tee, "sink");
  }
  gst_ghost_pad_set_target (GST_GHOST_PAD (vid->sinkpad), vid_sinkpad);
  gst_object_unref (vid_sinkpad);

  /* Add queue element for video */
  vid->tee_video_srcpad = gst_element_get_request_pad (vid->tee, "src%d");

  vid->video_queue = gst_element_factory_make ("queue", "video-queue");
  if (!gst_camerabin_add_element (vidbin, vid->video_queue)) {
    goto error;
  }
  g_object_set (vid->video_queue, "silent", TRUE, NULL);

  /* Add probe for rewriting video timestamps */
  vid->vid_tee_probe_id = gst_pad_add_buffer_probe (vid->tee_video_srcpad,
      G_CALLBACK (camerabin_video_pad_tee_src0_have_buffer), vid);

  if (vid->flags & GST_CAMERABIN_FLAG_VIDEO_COLOR_CONVERSION) {
    /* Add colorspace converter */
    if (gst_camerabin_create_and_add_element (vidbin,
            "ffmpegcolorspace", "video-ffmpegcolorspace") == NULL) {
      goto error;
    }
  }

  /* Add user set or default video encoder element */
  if (vid->app_vid_enc) {
    vid->vid_enc = vid->app_vid_enc;
    if (!gst_camerabin_add_element (vidbin, vid->vid_enc)) {
      goto error;
    }
  } else if (!(vid->vid_enc =
          gst_camerabin_create_and_add_element (vidbin, DEFAULT_VID_ENC,
              "video-encoder"))) {
    goto error;
  }

  /* Add application set or default muxer element */
  if (vid->app_mux) {
    vid->muxer = vid->app_mux;
    if (!gst_camerabin_add_element (vidbin, vid->muxer)) {
      goto error;
    }
  } else if (!(vid->muxer =
          gst_camerabin_create_and_add_element (vidbin, DEFAULT_MUX,
              "video-muxer"))) {
    goto error;
  }

  /* Add sink element for storing the video */
  if (!(vid->sink =
          gst_camerabin_create_and_add_element (vidbin, DEFAULT_SINK,
              "video-sink"))) {
    goto error;
  }
  g_object_set (G_OBJECT (vid->sink), "location", vid->filename->str, "buffer-mode", 2, /* non buffered io */
      NULL);

  if (!(vid->flags & GST_CAMERABIN_FLAG_DISABLE_AUDIO)) {
    /* Add application set or default audio source element */
    if (!(vid->aud_src = gst_camerabin_setup_default_element (vidbin,
                vid->app_aud_src, "autoaudiosrc", DEFAULT_AUDIOSRC))) {
      vid->aud_src = NULL;
      goto error;
    } else {
      if (!gst_camerabin_add_element (vidbin, vid->aud_src))
        goto error;
    }

    /* Add queue element for audio */
    queue = gst_element_factory_make ("queue", "audio-queue");
    if (!gst_camerabin_add_element (vidbin, queue)) {
      goto error;
    }
    g_object_set (queue, "silent", TRUE, NULL);

    /* Add optional audio conversion and volume elements and
       raise no errors if adding them fails */
    if (vid->flags & GST_CAMERABIN_FLAG_AUDIO_CONVERSION) {
      if (!gst_camerabin_try_add_element (vidbin,
              gst_element_factory_make ("audioconvert", NULL))) {
        GST_WARNING_OBJECT (vid, "unable to add audio conversion element");
        /* gst_camerabin_try_add_element() destroyed the element */
      }
    }

    vid->volume = gst_element_factory_make ("volume", NULL);
    if (!gst_camerabin_try_add_element (vidbin, vid->volume)) {
      GST_WARNING_OBJECT (vid, "unable to add volume element");
      /* gst_camerabin_try_add_element() destroyed the element */
      vid->volume = NULL;
    } else {
      g_object_set (vid->volume, "mute", vid->mute, NULL);
    }

    /* Add application set or default audio encoder element */
    if (vid->app_aud_enc) {
      vid->aud_enc = vid->app_aud_enc;
      if (!gst_camerabin_add_element (vidbin, vid->aud_enc)) {
        goto error;
      }
    } else if (!(vid->aud_enc =
            gst_camerabin_create_and_add_element (vidbin, DEFAULT_AUD_ENC,
                "audio-encoder"))) {
      goto error;
    }

    /* Link audio part to the muxer */
    if (!gst_element_link_pads_full (vid->aud_enc, NULL, vid->muxer, NULL,
            GST_PAD_LINK_CHECK_CAPS)) {
      GST_ELEMENT_ERROR (vid, CORE, NEGOTIATION, (NULL),
          ("linking audio encoder and muxer failed"));
      goto error;
    }
  }
  /* Add queue leading out of the video bin and to view finder */
  vid->tee_vf_srcpad = gst_element_get_request_pad (vid->tee, "src%d");
  queue = gst_element_factory_make ("queue", "viewfinder-queue");
  if (!gst_camerabin_add_element (vidbin, queue)) {
    goto error;
  }
  /* Set queue leaky, we don't want to block video encoder feed, but
     prefer leaking view finder buffers instead. */
  g_object_set (G_OBJECT (queue), "leaky", 2, "max-size-buffers", 1, "silent",
      TRUE, NULL);

  /* Set up src ghost pad for video bin */
  vid_srcpad = gst_element_get_static_pad (queue, "src");
  gst_ghost_pad_set_target (GST_GHOST_PAD (vid->srcpad), vid_srcpad);
  /* Never let video bin eos events reach view finder */
  vid->vid_src_probe_id = gst_pad_add_event_probe (vid_srcpad,
      G_CALLBACK (gst_camerabin_drop_eos_probe), vid);
  gst_object_unref (vid_srcpad);

  /* audio source is not always present and might be set to NULL during operation */
  if (vid->aud_src
      && g_object_class_find_property (G_OBJECT_GET_CLASS (vid->aud_src),
          "provide-clock")) {
    g_object_set (vid->aud_src, "provide-clock", FALSE, NULL);
  }

  GST_DEBUG ("created video elements");

  return TRUE;

error:

  gst_camerabin_video_destroy_elements (vid);

  return FALSE;

}

/*
 * gst_camerabin_video_destroy_elements:
 * @vid: a pointer to #GstCameraBinVideo
 *
 * This function destroys all the elements created by
 * gst_camerabin_video_create_elements().
 *
 */
static void
gst_camerabin_video_destroy_elements (GstCameraBinVideo * vid)
{
  GST_DEBUG ("destroying video elements");

  /* Remove EOS event probe from videobin srcpad (queue's srcpad) */
  if (vid->vid_src_probe_id) {
    GstPad *pad = gst_ghost_pad_get_target (GST_GHOST_PAD (vid->srcpad));
    if (pad) {
      gst_pad_remove_event_probe (pad, vid->vid_src_probe_id);
      gst_object_unref (pad);
    }
    vid->vid_src_probe_id = 0;
  }

  /* Remove buffer probe from video tee srcpad */
  if (vid->vid_tee_probe_id) {
    gst_pad_remove_buffer_probe (vid->tee_video_srcpad, vid->vid_tee_probe_id);
    vid->vid_tee_probe_id = 0;
  }

  /* Release tee request pads */
  if (vid->tee_video_srcpad) {
    gst_element_release_request_pad (vid->tee, vid->tee_video_srcpad);
    gst_object_unref (vid->tee_video_srcpad);
    vid->tee_video_srcpad = NULL;
  }
  if (vid->tee_vf_srcpad) {
    gst_element_release_request_pad (vid->tee, vid->tee_vf_srcpad);
    gst_object_unref (vid->tee_vf_srcpad);
    vid->tee_vf_srcpad = NULL;
  }

  gst_ghost_pad_set_target (GST_GHOST_PAD (vid->sinkpad), NULL);
  gst_ghost_pad_set_target (GST_GHOST_PAD (vid->srcpad), NULL);

  gst_camerabin_remove_elements_from_bin (GST_BIN (vid));

  vid->aud_src = NULL;
  vid->sink = NULL;
  vid->tee = NULL;
  vid->volume = NULL;
  vid->video_queue = NULL;
  vid->vid_enc = NULL;
  vid->aud_enc = NULL;
  vid->muxer = NULL;

  if (vid->pending_eos) {
    gst_event_unref (vid->pending_eos);
    vid->pending_eos = NULL;
  }
}

/*
 * Set & get mute and video capture elements
 */

void
gst_camerabin_video_set_mute (GstCameraBinVideo * vid, gboolean mute)
{
  g_return_if_fail (vid != NULL);

  GST_DEBUG_OBJECT (vid, "setting mute %s", mute ? "on" : "off");
  vid->mute = mute;
  if (vid->volume) {
    g_object_set (vid->volume, "mute", mute, NULL);
  }
}

void
gst_camerabin_video_set_post (GstCameraBinVideo * vid, GstElement * post)
{
  GstElement **app_post;
  GST_DEBUG_OBJECT (vid, "setting video post processing: %" GST_PTR_FORMAT,
      post);
  GST_OBJECT_LOCK (vid);
  app_post = &vid->app_post;
  gst_object_replace ((GstObject **) app_post, GST_OBJECT (post));
  GST_OBJECT_UNLOCK (vid);
}

void
gst_camerabin_video_set_video_enc (GstCameraBinVideo * vid,
    GstElement * video_enc)
{
  GstElement **app_vid_enc;
  GST_DEBUG_OBJECT (vid, "setting video encoder: %" GST_PTR_FORMAT, video_enc);
  GST_OBJECT_LOCK (vid);
  app_vid_enc = &vid->app_vid_enc;
  gst_object_replace ((GstObject **) app_vid_enc, GST_OBJECT (video_enc));
  GST_OBJECT_UNLOCK (vid);
}

void
gst_camerabin_video_set_audio_enc (GstCameraBinVideo * vid,
    GstElement * audio_enc)
{
  GstElement **app_aud_enc;
  GST_DEBUG_OBJECT (vid, "setting audio encoder: %" GST_PTR_FORMAT, audio_enc);
  GST_OBJECT_LOCK (vid);
  app_aud_enc = &vid->app_aud_enc;
  gst_object_replace ((GstObject **) app_aud_enc, GST_OBJECT (audio_enc));
  GST_OBJECT_UNLOCK (vid);
}

void
gst_camerabin_video_set_muxer (GstCameraBinVideo * vid, GstElement * muxer)
{
  GstElement **app_mux;
  GST_DEBUG_OBJECT (vid, "setting muxer: %" GST_PTR_FORMAT, muxer);
  GST_OBJECT_LOCK (vid);
  app_mux = &vid->app_mux;
  gst_object_replace ((GstObject **) app_mux, GST_OBJECT (muxer));
  GST_OBJECT_UNLOCK (vid);
}

void
gst_camerabin_video_set_audio_src (GstCameraBinVideo * vid,
    GstElement * audio_src)
{
  GstElement **app_aud_src;
  GST_DEBUG_OBJECT (vid, "setting audio source: %" GST_PTR_FORMAT, audio_src);
  GST_OBJECT_LOCK (vid);
  app_aud_src = &vid->app_aud_src;
  gst_object_replace ((GstObject **) app_aud_src, GST_OBJECT (audio_src));
  GST_OBJECT_UNLOCK (vid);
}

void
gst_camerabin_video_set_flags (GstCameraBinVideo * vid, GstCameraBinFlags flags)
{
  GST_DEBUG_OBJECT (vid, "setting video flags: %d", flags);
  GST_OBJECT_LOCK (vid);
  vid->flags = flags;
  GST_OBJECT_UNLOCK (vid);
}


gboolean
gst_camerabin_video_get_mute (GstCameraBinVideo * vid)
{
  g_return_val_if_fail (vid != NULL, FALSE);

  if (vid->volume) {
    g_object_get (vid->volume, "mute", &vid->mute, NULL);
  }

  return vid->mute;
}

GstElement *
gst_camerabin_video_get_post (GstCameraBinVideo * vid)
{
  return vid->app_post;
}

GstElement *
gst_camerabin_video_get_video_enc (GstCameraBinVideo * vid)
{
  return vid->vid_enc ? vid->vid_enc : vid->app_vid_enc;
}

GstElement *
gst_camerabin_video_get_audio_enc (GstCameraBinVideo * vid)
{
  return vid->aud_enc ? vid->aud_enc : vid->app_aud_enc;
}

GstElement *
gst_camerabin_video_get_muxer (GstCameraBinVideo * vid)
{
  return vid->muxer ? vid->muxer : vid->app_mux;
}

GstElement *
gst_camerabin_video_get_audio_src (GstCameraBinVideo * vid)
{
  return vid->aud_src ? vid->aud_src : vid->app_aud_src;
}
