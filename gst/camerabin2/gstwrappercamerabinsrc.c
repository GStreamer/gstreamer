/*
 * GStreamer
 * Copyright (C) 2010 Texas Instruments, Inc
 * Copyright (C) 2011 Thiago Santos <thiago.sousa.santos@collabora.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */


/**
 * SECTION:element-wrappercamerabinsrc
 *
 * A camera bin src element that wraps a default video source with a single
 * pad into the 3pad model that camerabin2 expects.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* FIXME 0.11: suppress warnings for deprecated API such as GStaticRecMutex
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include <gst/interfaces/photography.h>

#include "gstwrappercamerabinsrc.h"
#include "camerabingeneral.h"

enum
{
  PROP_0,
  PROP_VIDEO_SRC,
  PROP_VIDEO_SRC_FILTER
};

GST_DEBUG_CATEGORY (wrapper_camera_bin_src_debug);
#define GST_CAT_DEFAULT wrapper_camera_bin_src_debug

#define gst_wrapper_camera_bin_src_parent_class parent_class
G_DEFINE_TYPE (GstWrapperCameraBinSrc, gst_wrapper_camera_bin_src,
    GST_TYPE_BASE_CAMERA_SRC);

static GstStaticPadTemplate vfsrc_template =
GST_STATIC_PAD_TEMPLATE (GST_BASE_CAMERA_SRC_VIEWFINDER_PAD_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate imgsrc_template =
GST_STATIC_PAD_TEMPLATE (GST_BASE_CAMERA_SRC_IMAGE_PAD_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate vidsrc_template =
GST_STATIC_PAD_TEMPLATE (GST_BASE_CAMERA_SRC_VIDEO_PAD_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void set_capsfilter_caps (GstWrapperCameraBinSrc * self,
    GstCaps * new_caps);

static void
gst_wrapper_camera_bin_src_dispose (GObject * object)
{
  GstWrapperCameraBinSrc *self = GST_WRAPPER_CAMERA_BIN_SRC (object);

  if (self->app_vid_src) {
    gst_object_unref (self->app_vid_src);
    self->app_vid_src = NULL;
  }
  if (self->app_vid_filter) {
    gst_object_unref (self->app_vid_filter);
    self->app_vid_filter = NULL;
  }
  gst_caps_replace (&self->image_capture_caps, NULL);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_wrapper_camera_bin_src_finalize (GstWrapperCameraBinSrc * self)
{
  G_OBJECT_CLASS (parent_class)->finalize ((GObject *) (self));
}

static void
gst_wrapper_camera_bin_src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstWrapperCameraBinSrc *self = GST_WRAPPER_CAMERA_BIN_SRC (object);

  switch (prop_id) {
    case PROP_VIDEO_SRC:
      if (GST_STATE (self) != GST_STATE_NULL) {
        GST_ELEMENT_ERROR (self, CORE, FAILED,
            ("camerasrc must be in NULL state when setting the video source element"),
            (NULL));
      } else {
        if (self->app_vid_src)
          gst_object_unref (self->app_vid_src);
        self->app_vid_src = g_value_get_object (value);
        if (self->app_vid_src)
          gst_object_ref (self->app_vid_src);
      }
      break;
    case PROP_VIDEO_SRC_FILTER:
      if (GST_STATE (self) != GST_STATE_NULL) {
        GST_ELEMENT_ERROR (self, CORE, FAILED,
            ("camerasrc must be in NULL state when setting the video source filter element"),
            (NULL));
      } else {
        if (self->app_vid_filter)
          gst_object_unref (self->app_vid_filter);
        self->app_vid_filter = g_value_get_object (value);
        if (self->app_vid_filter)
          gst_object_ref (self->app_vid_filter);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (self, prop_id, pspec);
      break;
  }
}

static void
gst_wrapper_camera_bin_src_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstWrapperCameraBinSrc *self = GST_WRAPPER_CAMERA_BIN_SRC (object);

  switch (prop_id) {
    case PROP_VIDEO_SRC:
      if (self->src_vid_src)
        g_value_set_object (value, self->src_vid_src);
      else
        g_value_set_object (value, self->app_vid_src);
      break;
    case PROP_VIDEO_SRC_FILTER:
      if (self->video_filter)
        g_value_set_object (value, self->video_filter);
      else
        g_value_set_object (value, self->app_vid_filter);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (self, prop_id, pspec);
      break;
  }
}

static void
gst_wrapper_camera_bin_reset_video_src_caps (GstWrapperCameraBinSrc * self,
    GstCaps * caps)
{
  GST_DEBUG_OBJECT (self, "Resetting src caps to %" GST_PTR_FORMAT, caps);
  if (self->src_vid_src) {
    GstCaps *old_caps;

    g_object_get (G_OBJECT (self->src_filter), "caps", &old_caps, NULL);
    if (gst_caps_is_equal (caps, old_caps)) {
      GST_DEBUG_OBJECT (self, "old and new caps are same, do not reset it");
      if (old_caps)
        gst_caps_unref (old_caps);
      return;
    }
    if (old_caps)
      gst_caps_unref (old_caps);

    set_capsfilter_caps (self, caps);
  }
}

/**
 * gst_wrapper_camera_bin_src_imgsrc_probe:
 *
 * Buffer probe called before sending each buffer to image queue.
 */
static GstPadProbeReturn
gst_wrapper_camera_bin_src_imgsrc_probe (GstPad * pad, GstPadProbeInfo * info,
    gpointer data)
{
  GstWrapperCameraBinSrc *self = GST_WRAPPER_CAMERA_BIN_SRC (data);
  GstBaseCameraSrc *camerasrc = GST_BASE_CAMERA_SRC (data);
  GstBuffer *buffer = GST_BUFFER (info->data);
  GstPadProbeReturn ret = GST_PAD_PROBE_DROP;

  GST_LOG_OBJECT (self, "Image probe, mode %d, capture count %d bufsize: %"
      G_GSIZE_FORMAT, camerasrc->mode, self->image_capture_count,
      gst_buffer_get_size (buffer));

  g_mutex_lock (&camerasrc->capturing_mutex);
  if (self->image_capture_count > 0) {
    GstSample *sample;
    GstCaps *caps;
    ret = GST_PAD_PROBE_OK;
    self->image_capture_count--;

    /* post preview */
    /* TODO This can likely be optimized if the viewfinder caps is the same as
     * the preview caps, avoiding another scaling of the same buffer. */
    GST_DEBUG_OBJECT (self, "Posting preview for image");
    caps = gst_pad_get_current_caps (pad);
    sample = gst_sample_new (buffer, caps, NULL, NULL);
    gst_base_camera_src_post_preview (camerasrc, sample);
    gst_caps_unref (caps);
    gst_sample_unref (sample);

    if (self->image_capture_count == 0) {
      gst_base_camera_src_finish_capture (camerasrc);
    }
  }
  g_mutex_unlock (&camerasrc->capturing_mutex);
  return ret;
}

/**
 * gst_wrapper_camera_bin_src_vidsrc_probe:
 *
 * Buffer probe called before sending each buffer to image queue.
 */
static GstPadProbeReturn
gst_wrapper_camera_bin_src_vidsrc_probe (GstPad * pad, GstPadProbeInfo * info,
    gpointer data)
{
  GstWrapperCameraBinSrc *self = GST_WRAPPER_CAMERA_BIN_SRC (data);
  GstBaseCameraSrc *camerasrc = GST_BASE_CAMERA_SRC_CAST (self);
  GstPadProbeReturn ret = GST_PAD_PROBE_DROP;
  GstBuffer *buffer = GST_BUFFER (info->data);

  GST_LOG_OBJECT (self, "Video probe, mode %d, capture status %d",
      camerasrc->mode, self->video_rec_status);

  /* TODO do we want to lock for every buffer? */
  /*
   * Note that we can use gst_pad_push_event here because we are a buffer
   * probe.
   */
  /* TODO shouldn't access this directly */
  g_mutex_lock (&camerasrc->capturing_mutex);
  if (self->video_rec_status == GST_VIDEO_RECORDING_STATUS_DONE) {
    /* NOP */
  } else if (self->video_rec_status == GST_VIDEO_RECORDING_STATUS_STARTING) {
    GstClockTime ts;
    GstSegment segment;
    GstCaps *caps;
    GstSample *sample;

    GST_DEBUG_OBJECT (self, "Starting video recording");
    self->video_rec_status = GST_VIDEO_RECORDING_STATUS_RUNNING;

    ts = GST_BUFFER_TIMESTAMP (buffer);
    if (!GST_CLOCK_TIME_IS_VALID (ts))
      ts = 0;
    gst_segment_init (&segment, GST_FORMAT_TIME);
    segment.start = ts;
    gst_pad_push_event (self->vidsrc, gst_event_new_segment (&segment));

    /* post preview */
    GST_DEBUG_OBJECT (self, "Posting preview for video");
    caps = gst_pad_get_current_caps (pad);
    sample = gst_sample_new (buffer, caps, NULL, NULL);
    gst_base_camera_src_post_preview (camerasrc, sample);
    gst_caps_unref (caps);
    gst_sample_unref (sample);

    ret = GST_PAD_PROBE_OK;
  } else if (self->video_rec_status == GST_VIDEO_RECORDING_STATUS_FINISHING) {
    GstPad *peer;

    /* send eos */
    GST_DEBUG_OBJECT (self, "Finishing video recording, pushing eos");

    peer = gst_pad_get_peer (self->vidsrc);

    if (peer) {
      /* send to the peer as we don't want our pads with eos flag */
      gst_pad_send_event (peer, gst_event_new_eos ());
      gst_object_unref (peer);
    } else {
      GST_WARNING_OBJECT (camerasrc, "No peer pad for vidsrc");
    }
    self->video_rec_status = GST_VIDEO_RECORDING_STATUS_DONE;
    gst_base_camera_src_finish_capture (camerasrc);
  } else {
    ret = GST_PAD_PROBE_OK;
  }
  g_mutex_unlock (&camerasrc->capturing_mutex);
  return ret;
}

static GstPadProbeReturn
gst_wrapper_camera_src_src_event_probe (GstPad * pad, GstPadProbeInfo * info,
    gpointer udata)
{
  GstPadProbeReturn ret = GST_PAD_PROBE_OK;
  GstWrapperCameraBinSrc *self = udata;
  GstEvent *evt = GST_EVENT (info->data);

  switch (GST_EVENT_TYPE (evt)) {
    case GST_EVENT_EOS:
      /* drop */
      ret = GST_PAD_PROBE_DROP;
      break;
    case GST_EVENT_SEGMENT:
      if (self->drop_newseg) {
        ret = GST_PAD_PROBE_DROP;
        self->drop_newseg = FALSE;
      }
      break;
    default:
      break;
  }
  return ret;
}

static void
gst_wrapper_camera_bin_src_caps_cb (GstPad * pad, GParamSpec * pspec,
    gpointer user_data)
{
  GstBaseCameraSrc *bcamsrc = GST_BASE_CAMERA_SRC (user_data);
  GstWrapperCameraBinSrc *self = GST_WRAPPER_CAMERA_BIN_SRC (user_data);
  GstCaps *caps;
  GstStructure *in_st = NULL;

  caps = gst_pad_get_current_caps (pad);

  GST_DEBUG_OBJECT (self, "src-filter caps changed to %" GST_PTR_FORMAT, caps);

  if (caps && gst_caps_get_size (caps)) {
    in_st = gst_caps_get_structure (caps, 0);
    if (in_st) {
      gst_structure_get_int (in_st, "width", &bcamsrc->width);
      gst_structure_get_int (in_st, "height", &bcamsrc->height);

      GST_DEBUG_OBJECT (self, "Source dimensions now: %dx%d", bcamsrc->width,
          bcamsrc->height);
    }
  }

  /* Update zoom */
  gst_base_camera_src_setup_zoom (bcamsrc);

  /* Update post-zoom capsfilter */
  if (self->src_zoom_filter) {
    GstCaps *filtercaps;

    g_object_get (G_OBJECT (self->src_zoom_filter), "caps", &filtercaps, NULL);

    if (caps != filtercaps && (caps == NULL || filtercaps == NULL ||
            !gst_caps_is_equal (filtercaps, caps)))
      g_object_set (G_OBJECT (self->src_zoom_filter), "caps", caps, NULL);

    if (filtercaps)
      gst_caps_unref (filtercaps);
  }

  if (caps)
    gst_caps_unref (caps);
};

static void
gst_wrapper_camera_bin_src_max_zoom_cb (GObject * self, GParamSpec * pspec,
    gpointer user_data)
{
  GstBaseCameraSrc *bcamsrc = (GstBaseCameraSrc *) user_data;

  g_object_get (self, "max-zoom", &bcamsrc->max_zoom, NULL);
  g_object_notify (G_OBJECT (bcamsrc), "max-zoom");
}


static gboolean
gst_wrapper_camera_bin_src_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  gboolean ret = TRUE;
  GstWrapperCameraBinSrc *self = GST_WRAPPER_CAMERA_BIN_SRC (parent);
  GstPad *upstream_pad = NULL;

  GST_DEBUG_OBJECT (self, "Handling event %p %" GST_PTR_FORMAT, event, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_RECONFIGURE:
      if (pad == self->imgsrc) {
        GST_DEBUG_OBJECT (self, "Image mode reconfigure event received");
        self->image_renegotiate = TRUE;
      } else if (pad == self->vidsrc) {
        GST_DEBUG_OBJECT (self, "Video mode reconfigure event received");
        self->video_renegotiate = TRUE;
      }
      if (pad == self->imgsrc || pad == self->vidsrc) {
        gst_event_unref (event);
        return ret;
      }
      break;
    default:
      break;
  }

  if (pad == self->imgsrc) {
    upstream_pad = self->outsel_imgpad;
  } else if (pad == self->vidsrc) {
    upstream_pad = self->outsel_vidpad;
  }

  if (upstream_pad) {
    ret = gst_pad_send_event (upstream_pad, event);
  } else {
    GST_WARNING_OBJECT (self, "Event caught that doesn't have an upstream pad -"
        "this shouldn't be possible!");
    gst_event_unref (event);
    ret = FALSE;
  }

  return ret;
}

/**
 * check_and_replace_src
 * @self: #GstWrapperCamerabinSrcCameraSrc object
 *
 * Checks if the current videosrc needs to be replaced
 */
static gboolean
check_and_replace_src (GstWrapperCameraBinSrc * self)
{
  GstBin *cbin = GST_BIN_CAST (self);
  GstBaseCameraSrc *bcamsrc = GST_BASE_CAMERA_SRC_CAST (self);

  if (self->src_vid_src && self->src_vid_src == self->app_vid_src) {
    GST_DEBUG_OBJECT (self, "No need to change current videosrc");
    return TRUE;
  }

  if (self->src_vid_src) {
    GST_DEBUG_OBJECT (self, "Removing old video source");
    if (self->src_max_zoom_signal_id) {
      g_signal_handler_disconnect (self->src_vid_src,
          self->src_max_zoom_signal_id);
      self->src_max_zoom_signal_id = 0;
    }
    if (self->src_event_probe_id) {
      GstPad *pad;
      pad = gst_element_get_static_pad (self->src_vid_src, "src");
      gst_pad_remove_probe (pad, self->src_event_probe_id);
      gst_object_unref (pad);
      self->src_event_probe_id = 0;
    }
    gst_bin_remove (GST_BIN_CAST (self), self->src_vid_src);
    self->src_vid_src = NULL;
  }

  GST_DEBUG_OBJECT (self, "Adding new video source");

  /* Add application set or default video src element */
  if (!(self->src_vid_src = gst_camerabin_setup_default_element (cbin,
              self->app_vid_src, "autovideosrc", DEFAULT_VIDEOSRC,
              "camerasrc-real-src"))) {
    self->src_vid_src = NULL;
    return FALSE;
  } else {
    GstElement *videoconvert;
    if (!gst_bin_add (cbin, self->src_vid_src)) {
      return FALSE;
    }

    /* check if we already have the next element to link to */
    videoconvert = gst_bin_get_by_name (cbin, "src-videoconvert");
    if (videoconvert) {
      if (!gst_element_link_pads (self->src_vid_src, "src", videoconvert,
              "sink")) {
        gst_object_unref (videoconvert);
        return FALSE;
      }
      gst_object_unref (videoconvert);
    }
  }

  /* we listen for changes to max-zoom in the video src so that
   * we can proxy them to the basecamerasrc property */
  if (g_object_class_find_property (G_OBJECT_GET_CLASS (bcamsrc), "max-zoom")) {
    self->src_max_zoom_signal_id =
        g_signal_connect (G_OBJECT (self->src_vid_src), "notify::max-zoom",
        (GCallback) gst_wrapper_camera_bin_src_max_zoom_cb, bcamsrc);
  }

  /* add a buffer probe to the src elemento to drop EOS from READY->NULL */
  {
    GstPad *pad;
    pad = gst_element_get_static_pad (self->src_vid_src, "src");

    self->src_event_probe_id =
        gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
        gst_wrapper_camera_src_src_event_probe, gst_object_ref (self),
        gst_object_unref);
    gst_object_unref (pad);
  }
  return TRUE;
}

/**
 * gst_wrapper_camera_bin_src_construct_pipeline:
 * @bcamsrc: camerasrc object
 *
 * This function creates and links the elements of the camerasrc bin
 * videosrc ! cspconv ! srcfilter ! cspconv ! capsfilter ! crop ! scale ! \
 * capsfilter ! tee name=t
 *    t. ! ... (viewfinder pad)
 *    t. ! output-selector name=outsel
 *        outsel. ! (image pad)
 *        outsel. ! (video pad)
 *
 * Returns: TRUE, if elements were successfully created, FALSE otherwise
 */
static gboolean
gst_wrapper_camera_bin_src_construct_pipeline (GstBaseCameraSrc * bcamsrc)
{
  GstWrapperCameraBinSrc *self = GST_WRAPPER_CAMERA_BIN_SRC (bcamsrc);
  GstBin *cbin = GST_BIN (bcamsrc);
  GstElement *tee;
  GstElement *filter_csp;
  GstElement *src_csp;
  GstElement *capsfilter;
  gboolean ret = FALSE;
  GstPad *vf_pad;
  GstPad *tee_capture_pad;
  GstPad *src_caps_src_pad;

  /* checks and adds a new video src if needed */
  if (!check_and_replace_src (self))
    goto done;

  if (!self->elements_created) {

    GST_DEBUG_OBJECT (self, "constructing pipeline");

    if (!gst_camerabin_create_and_add_element (cbin, "videoconvert",
            "src-videoconvert"))
      goto done;

    if (self->app_vid_filter) {
      self->video_filter = gst_object_ref (self->app_vid_filter);

      if (!gst_camerabin_add_element (cbin, self->video_filter))
        goto done;
      if (!gst_camerabin_create_and_add_element (cbin, "videoconvert",
              "filter-videoconvert"))
        goto done;
    }

    if (!(self->src_filter =
            gst_camerabin_create_and_add_element (cbin, "capsfilter",
                "src-capsfilter")))
      goto done;

    /* attach to notify::caps on the first capsfilter and use a callback
     * to recalculate the zoom properties when these caps change and to
     * propagate the caps to the second capsfilter */
    src_caps_src_pad = gst_element_get_static_pad (self->src_filter, "src");
    g_signal_connect (src_caps_src_pad, "notify::caps",
        G_CALLBACK (gst_wrapper_camera_bin_src_caps_cb), self);
    gst_object_unref (src_caps_src_pad);

    if (!(self->src_zoom_crop =
            gst_camerabin_create_and_add_element (cbin, "videocrop",
                "zoom-crop")))
      goto done;
    if (!(self->src_zoom_scale =
            gst_camerabin_create_and_add_element (cbin, "videoscale",
                "zoom-scale")))
      goto done;
    if (!(self->src_zoom_filter =
            gst_camerabin_create_and_add_element (cbin, "capsfilter",
                "zoom-capsfilter")))
      goto done;

    if (!(tee =
            gst_camerabin_create_and_add_element (cbin, "tee",
                "camerasrc-tee")))
      goto done;

    /* viewfinder pad */
    vf_pad = gst_element_get_request_pad (tee, "src_%u");
    g_object_set (tee, "alloc-pad", vf_pad, NULL);
    gst_ghost_pad_set_target (GST_GHOST_PAD (self->vfsrc), vf_pad);
    gst_object_unref (vf_pad);

    /* image/video pad from tee */
    tee_capture_pad = gst_element_get_request_pad (tee, "src_%u");

    self->output_selector =
        gst_element_factory_make ("output-selector", "outsel");
    g_object_set (self->output_selector, "pad-negotiation-mode", 2, NULL);
    gst_bin_add (GST_BIN (self), self->output_selector);
    {
      GstPad *pad = gst_element_get_static_pad (self->output_selector, "sink");

      /* check return TODO */
      gst_pad_link (tee_capture_pad, pad);
      gst_object_unref (pad);
    }
    gst_object_unref (tee_capture_pad);

    /* Create the 2 output pads for video and image */
    self->outsel_vidpad =
        gst_element_get_request_pad (self->output_selector, "src_%u");
    self->outsel_imgpad =
        gst_element_get_request_pad (self->output_selector, "src_%u");

    g_assert (self->outsel_vidpad != NULL);
    g_assert (self->outsel_imgpad != NULL);

    gst_pad_add_probe (self->outsel_imgpad, GST_PAD_PROBE_TYPE_BUFFER,
        gst_wrapper_camera_bin_src_imgsrc_probe, gst_object_ref (self),
        gst_object_unref);
    gst_pad_add_probe (self->outsel_vidpad, GST_PAD_PROBE_TYPE_BUFFER,
        gst_wrapper_camera_bin_src_vidsrc_probe, gst_object_ref (self),
        gst_object_unref);
    gst_ghost_pad_set_target (GST_GHOST_PAD (self->imgsrc),
        self->outsel_imgpad);
    gst_ghost_pad_set_target (GST_GHOST_PAD (self->vidsrc),
        self->outsel_vidpad);

    if (bcamsrc->mode == MODE_IMAGE) {
      g_object_set (self->output_selector, "active-pad", self->outsel_imgpad,
          NULL);
    } else {
      g_object_set (self->output_selector, "active-pad", self->outsel_vidpad,
          NULL);
    }



    gst_pad_set_active (self->vfsrc, TRUE);
    gst_pad_set_active (self->imgsrc, TRUE);    /* XXX ??? */
    gst_pad_set_active (self->vidsrc, TRUE);    /* XXX ??? */
  }

  /* Do this even if pipeline is constructed */

  if (self->video_filter) {
    /* check if we need to replace the current one */
    if (self->video_filter != self->app_vid_filter) {
      gst_bin_remove (cbin, self->video_filter);
      gst_object_unref (self->video_filter);
      self->video_filter = NULL;
      filter_csp = gst_bin_get_by_name (cbin, "filter-videoconvert");
      gst_bin_remove (cbin, filter_csp);
      gst_object_unref (filter_csp);
      filter_csp = NULL;
    }
  }

  if (!self->video_filter) {
    if (self->app_vid_filter) {
      self->video_filter = gst_object_ref (self->app_vid_filter);
      filter_csp = gst_element_factory_make ("videoconvert",
          "filter-videoconvert");
      gst_bin_add_many (cbin, self->video_filter, filter_csp, NULL);
      src_csp = gst_bin_get_by_name (cbin, "src-videoconvert");
      capsfilter = gst_bin_get_by_name (cbin, "src-capsfilter");
      if (gst_pad_is_linked (gst_element_get_static_pad (src_csp, "src")))
        gst_element_unlink (src_csp, capsfilter);
      if (!gst_element_link_many (src_csp, self->video_filter, filter_csp,
              capsfilter, NULL)) {
        gst_object_unref (src_csp);
        gst_object_unref (capsfilter);
        goto done;
      }
      gst_object_unref (src_csp);
      gst_object_unref (capsfilter);
    }
  }
  ret = TRUE;
  self->elements_created = TRUE;
done:
  return ret;
}

static gboolean
copy_missing_fields (GQuark field_id, const GValue * value, gpointer user_data)
{
  GstStructure *st = (GstStructure *) user_data;
  const GValue *val = gst_structure_id_get_value (st, field_id);

  if (G_UNLIKELY (val == NULL)) {
    gst_structure_id_set_value (st, field_id, value);
  }

  return TRUE;
}

/**
 * adapt_image_capture:
 * @self: camerasrc object
 * @in_caps: caps object that describes incoming image format
 *
 * Adjust capsfilters and crop according image capture caps if necessary.
 * The captured image format from video source might be different from
 * what application requested, so we can try to fix that in camerabin.
 *
 */
static void
adapt_image_capture (GstWrapperCameraBinSrc * self, GstCaps * in_caps)
{
  GstBaseCameraSrc *bcamsrc = GST_BASE_CAMERA_SRC (self);
  GstStructure *in_st, *new_st, *req_st;
  gint in_width = 0, in_height = 0, req_width = 0, req_height = 0, crop = 0;
  gdouble ratio_w, ratio_h;
  GstCaps *filter_caps = NULL;

  GST_LOG_OBJECT (self, "in caps: %" GST_PTR_FORMAT, in_caps);
  GST_LOG_OBJECT (self, "requested caps: %" GST_PTR_FORMAT,
      self->image_capture_caps);

  in_st = gst_caps_get_structure (in_caps, 0);
  gst_structure_get_int (in_st, "width", &in_width);
  gst_structure_get_int (in_st, "height", &in_height);

  req_st = gst_caps_get_structure (self->image_capture_caps, 0);
  gst_structure_get_int (req_st, "width", &req_width);
  gst_structure_get_int (req_st, "height", &req_height);

  GST_INFO_OBJECT (self, "we requested %dx%d, and got %dx%d", req_width,
      req_height, in_width, in_height);

  new_st = gst_structure_copy (req_st);
  /* If new fields have been added, we need to copy them */
  gst_structure_foreach (in_st, copy_missing_fields, new_st);

  gst_structure_set (new_st, "width", G_TYPE_INT, in_width, "height",
      G_TYPE_INT, in_height, NULL);

  GST_LOG_OBJECT (self, "new image capture caps: %" GST_PTR_FORMAT, new_st);

  /* Crop if requested aspect ratio differs from incoming frame aspect ratio */
  if (self->src_zoom_crop) {

    ratio_w = (gdouble) in_width / req_width;
    ratio_h = (gdouble) in_height / req_height;

    if (ratio_w < ratio_h) {
      crop = in_height - (req_height * ratio_w);
      self->base_crop_top = crop / 2;
      self->base_crop_bottom = crop / 2;
    } else {
      crop = in_width - (req_width * ratio_h);
      self->base_crop_left = crop / 2;
      self->base_crop_right += crop / 2;
    }

    GST_INFO_OBJECT (self,
        "setting base crop: left:%d, right:%d, top:%d, bottom:%d",
        self->base_crop_left, self->base_crop_right, self->base_crop_top,
        self->base_crop_bottom);
    g_object_set (G_OBJECT (self->src_zoom_crop),
        "top", self->base_crop_top,
        "bottom", self->base_crop_bottom,
        "left", self->base_crop_left, "right", self->base_crop_right, NULL);
  }

  /* Update capsfilters */
  if (self->image_capture_caps) {
    gst_caps_unref (self->image_capture_caps);
  }
  self->image_capture_caps = gst_caps_new_full (new_st, NULL);
  set_capsfilter_caps (self, self->image_capture_caps);

  /* Adjust the capsfilter before crop and videoscale elements if necessary */
  if (in_width == bcamsrc->width && in_height == bcamsrc->height) {
    GST_DEBUG_OBJECT (self, "no adaptation with resolution needed");
  } else {
    GST_DEBUG_OBJECT (self,
        "changing %" GST_PTR_FORMAT " from %dx%d to %dx%d", self->src_filter,
        bcamsrc->width, bcamsrc->height, in_width, in_height);
    /* Apply the width and height to filter caps */
    g_object_get (G_OBJECT (self->src_filter), "caps", &filter_caps, NULL);
    filter_caps = gst_caps_make_writable (filter_caps);
    gst_caps_set_simple (filter_caps, "width", G_TYPE_INT, in_width, "height",
        G_TYPE_INT, in_height, NULL);
    g_object_set (G_OBJECT (self->src_filter), "caps", filter_caps, NULL);
    gst_caps_unref (filter_caps);
  }
}

/**
 * img_capture_prepared:
 * @data: camerasrc object
 * @caps: caps describing the prepared image format
 *
 * Callback which is called after image capture has been prepared.
 */
static void
img_capture_prepared (gpointer data, GstCaps * caps)
{
  GstWrapperCameraBinSrc *self = GST_WRAPPER_CAMERA_BIN_SRC (data);

  GST_INFO_OBJECT (self, "image capture prepared");

  /* It is possible we are about to get something else that we requested */
  if (!gst_caps_is_equal (self->image_capture_caps, caps)) {
    adapt_image_capture (self, caps);
  } else {
    set_capsfilter_caps (self, self->image_capture_caps);
  }
}

/**
 *
 */
static gboolean
start_image_capture (GstWrapperCameraBinSrc * self)
{
  GstBaseCameraSrc *bcamsrc = GST_BASE_CAMERA_SRC (self);
  GstPhotography *photography =
      (GstPhotography *) gst_bin_get_by_interface (GST_BIN_CAST (bcamsrc),
      GST_TYPE_PHOTOGRAPHY);
  gboolean ret = FALSE;
  GstCaps *caps;
  GstPad *pad, *peer;

  GST_DEBUG_OBJECT (self, "Starting image capture");

  /* FIXME - V4L2 source will not close the device until all buffers have came
   * back. Flushing the pipeline, will ensure it's properly closed, and that
   * setting it back to PLAYING will work. This is more a workaround then a
   * solution to buffer reclaiming. */
  pad = gst_element_get_static_pad (self->src_vid_src, "src");
  peer = gst_pad_get_peer (pad);
  gst_object_unref (pad);
  gst_pad_send_event (peer, gst_event_new_flush_start ());
  gst_element_set_state (self->src_vid_src, GST_STATE_READY);
  gst_pad_send_event (peer, gst_event_new_flush_stop (TRUE));
  gst_object_unref (peer);

  if (self->image_renegotiate) {
    /* clean capsfilter caps so they don't interfere here */
    g_object_set (self->src_filter, "caps", NULL, NULL);
    if (self->src_zoom_filter)
      g_object_set (self->src_zoom_filter, "caps", NULL, NULL);

    caps = gst_pad_get_allowed_caps (self->imgsrc);

    gst_caps_replace (&self->image_capture_caps, caps);
    gst_caps_unref (caps);

    /* FIXME - do we need to update basecamerasrc width/height somehow here?
     * if not, i think we need to do something about _when_ they get updated
     * to be sure that set_element_zoom doesn't use the wrong values */

    /* We caught this event in the src pad event handler and now we want to
     * actually push it upstream */
    gst_pad_send_event (self->outsel_imgpad, gst_event_new_reconfigure ());

    self->image_renegotiate = FALSE;
  }

  if (photography) {
    gst_element_set_state (self->src_vid_src, GST_STATE_PLAYING);
    GST_DEBUG_OBJECT (self, "prepare image capture caps %" GST_PTR_FORMAT,
        self->image_capture_caps);
    ret = gst_photography_prepare_for_capture (photography,
        (GstPhotographyCapturePrepared) img_capture_prepared,
        self->image_capture_caps, self);
    gst_object_unref (photography);
  } else {
    g_mutex_unlock (&bcamsrc->capturing_mutex);
    gst_wrapper_camera_bin_reset_video_src_caps (self,
        self->image_capture_caps);
    g_mutex_lock (&bcamsrc->capturing_mutex);
    ret = TRUE;
    gst_element_set_state (self->src_vid_src, GST_STATE_PLAYING);
  }

  return ret;
}

static gboolean
gst_wrapper_camera_bin_src_set_mode (GstBaseCameraSrc * bcamsrc,
    GstCameraBinMode mode)
{
  GstPhotography *photography =
      (GstPhotography *) gst_bin_get_by_interface (GST_BIN_CAST (bcamsrc),
      GST_TYPE_PHOTOGRAPHY);
  GstWrapperCameraBinSrc *self = GST_WRAPPER_CAMERA_BIN_SRC (bcamsrc);

  if (self->output_selector) {
    if (mode == MODE_IMAGE) {
      self->image_renegotiate = TRUE;
      g_object_set (self->output_selector, "active-pad", self->outsel_imgpad,
          NULL);
    } else {
      self->video_renegotiate = TRUE;
      g_object_set (self->output_selector, "active-pad", self->outsel_vidpad,
          NULL);
    }
  }
  self->mode = mode;

  if (photography) {
    if (g_object_class_find_property (G_OBJECT_GET_CLASS (photography),
            "capture-mode")) {
      g_object_set (G_OBJECT (photography), "capture-mode", mode, NULL);
    }
    gst_object_unref (photography);
  } else {
    GstCaps *anycaps = gst_caps_new_any ();
    gst_wrapper_camera_bin_reset_video_src_caps (self, anycaps);
    gst_caps_unref (anycaps);
  }

  return TRUE;
}

static gboolean
set_videosrc_zoom (GstWrapperCameraBinSrc * self, gfloat zoom)
{
  gboolean ret = FALSE;

  if (g_object_class_find_property (G_OBJECT_GET_CLASS (self->src_vid_src),
          "zoom")) {
    g_object_set (G_OBJECT (self->src_vid_src), "zoom", zoom, NULL);
    ret = TRUE;
  }
  return ret;
}

static gboolean
set_element_zoom (GstWrapperCameraBinSrc * self, gfloat zoom)
{
  gboolean ret = FALSE;
  GstBaseCameraSrc *bcamsrc = GST_BASE_CAMERA_SRC (self);
  gint w2_crop = 0, h2_crop = 0;
  GstPad *pad_zoom_sink = NULL;
  gint left = self->base_crop_left;
  gint right = self->base_crop_right;
  gint top = self->base_crop_top;
  gint bottom = self->base_crop_bottom;

  if (self->src_zoom_crop) {
    /* Update capsfilters to apply the zoom */
    GST_INFO_OBJECT (self, "zoom: %f, orig size: %dx%d", zoom,
        bcamsrc->width, bcamsrc->height);

    if (zoom != ZOOM_1X) {
      w2_crop = (bcamsrc->width - (gint) (bcamsrc->width * ZOOM_1X / zoom)) / 2;
      h2_crop =
          (bcamsrc->height - (gint) (bcamsrc->height * ZOOM_1X / zoom)) / 2;

      left += w2_crop;
      right += w2_crop;
      top += h2_crop;
      bottom += h2_crop;

      /* force number of pixels cropped from left to be even, to avoid slow code
       * path on videoscale */
      left &= 0xFFFE;
    }

    pad_zoom_sink = gst_element_get_static_pad (self->src_zoom_crop, "sink");

    GST_INFO_OBJECT (self,
        "sw cropping: left:%d, right:%d, top:%d, bottom:%d", left, right, top,
        bottom);

    GST_PAD_STREAM_LOCK (pad_zoom_sink);
    g_object_set (self->src_zoom_crop, "left", left, "right", right, "top",
        top, "bottom", bottom, NULL);
    GST_PAD_STREAM_UNLOCK (pad_zoom_sink);
    gst_object_unref (pad_zoom_sink);
    ret = TRUE;
  }
  return ret;
}

static void
gst_wrapper_camera_bin_src_set_zoom (GstBaseCameraSrc * bcamsrc, gfloat zoom)
{
  GstWrapperCameraBinSrc *self = GST_WRAPPER_CAMERA_BIN_SRC (bcamsrc);

  GST_INFO_OBJECT (self, "setting zoom %f", zoom);

  if (set_videosrc_zoom (self, zoom)) {
    set_element_zoom (self, ZOOM_1X);
    GST_INFO_OBJECT (self, "zoom set using videosrc");
  } else if (set_element_zoom (self, zoom)) {
    GST_INFO_OBJECT (self, "zoom set using gst elements");
  } else {
    GST_INFO_OBJECT (self, "setting zoom failed");
  }
}

/**
 * update_aspect_filter:
 * @self: camerasrc object
 * @new_caps: new caps of next buffers arriving to view finder sink element
 *
 * Updates aspect ratio capsfilter to maintain aspect ratio, if we need to
 * scale frames for showing them in view finder.
 */
static void
update_aspect_filter (GstWrapperCameraBinSrc * self, GstCaps * new_caps)
{
  // XXX why not instead add a preserve-aspect-ratio property to videoscale?
#if 0
  if (camera->flags & GST_CAMERABIN_FLAG_VIEWFINDER_SCALE) {
    GstCaps *sink_caps, *ar_caps;
    GstStructure *st;
    gint in_w = 0, in_h = 0, sink_w = 0, sink_h = 0, target_w = 0, target_h = 0;
    gdouble ratio_w, ratio_h;
    GstPad *sink_pad;
    const GValue *range;

    sink_pad = gst_element_get_static_pad (camera->view_sink, "sink");

    if (sink_pad) {
      sink_caps = gst_pad_get_caps (sink_pad);
      gst_object_unref (sink_pad);
      if (sink_caps) {
        if (!gst_caps_is_any (sink_caps)) {
          GST_DEBUG_OBJECT (camera, "sink element caps %" GST_PTR_FORMAT,
              sink_caps);
          /* Get maximum resolution that view finder sink accepts */
          st = gst_caps_get_structure (sink_caps, 0);
          if (gst_structure_has_field_typed (st, "width", GST_TYPE_INT_RANGE)) {
            range = gst_structure_get_value (st, "width");
            sink_w = gst_value_get_int_range_max (range);
          }
          if (gst_structure_has_field_typed (st, "height", GST_TYPE_INT_RANGE)) {
            range = gst_structure_get_value (st, "height");
            sink_h = gst_value_get_int_range_max (range);
          }
          GST_DEBUG_OBJECT (camera, "sink element accepts max %dx%d", sink_w,
              sink_h);

          /* Get incoming frames' resolution */
          if (sink_h && sink_w) {
            st = gst_caps_get_structure (new_caps, 0);
            gst_structure_get_int (st, "width", &in_w);
            gst_structure_get_int (st, "height", &in_h);
            GST_DEBUG_OBJECT (camera, "new caps with %dx%d", in_w, in_h);
          }
        }
        gst_caps_unref (sink_caps);
      }
    }

    /* If we get bigger frames than view finder sink accepts, then we scale.
       If we scale we need to adjust aspect ratio capsfilter caps in order
       to maintain aspect ratio while scaling. */
    if (in_w && in_h && (in_w > sink_w || in_h > sink_h)) {
      ratio_w = (gdouble) sink_w / in_w;
      ratio_h = (gdouble) sink_h / in_h;

      if (ratio_w < ratio_h) {
        target_w = sink_w;
        target_h = (gint) (ratio_w * in_h);
      } else {
        target_w = (gint) (ratio_h * in_w);
        target_h = sink_h;
      }

      GST_DEBUG_OBJECT (camera, "setting %dx%d filter to maintain aspect ratio",
          target_w, target_h);
      ar_caps = gst_caps_copy (new_caps);
      gst_caps_set_simple (ar_caps, "width", G_TYPE_INT, target_w, "height",
          G_TYPE_INT, target_h, NULL);
    } else {
      GST_DEBUG_OBJECT (camera, "no scaling");
      ar_caps = new_caps;
    }

    GST_DEBUG_OBJECT (camera, "aspect ratio filter caps %" GST_PTR_FORMAT,
        ar_caps);
    g_object_set (G_OBJECT (camera->aspect_filter), "caps", ar_caps, NULL);
    if (ar_caps != new_caps)
      gst_caps_unref (ar_caps);
  }
#endif
}


/**
 * set_capsfilter_caps:
 * @self: camerasrc object
 * @new_caps: pointer to caps object to set
 *
 * Set given caps to camerabin capsfilters.
 */
static void
set_capsfilter_caps (GstWrapperCameraBinSrc * self, GstCaps * new_caps)
{
  GST_INFO_OBJECT (self, "new_caps:%" GST_PTR_FORMAT, new_caps);

  /* Update zoom */
  gst_base_camera_src_setup_zoom (GST_BASE_CAMERA_SRC (self));

  /* Update capsfilters */
  g_object_set (G_OBJECT (self->src_filter), "caps", new_caps, NULL);
  if (self->src_zoom_filter)
    g_object_set (G_OBJECT (self->src_zoom_filter), "caps", new_caps, NULL);
  update_aspect_filter (self, new_caps);
  GST_INFO_OBJECT (self, "updated");
}

static gboolean
gst_wrapper_camera_bin_src_start_capture (GstBaseCameraSrc * camerasrc)
{
  GstWrapperCameraBinSrc *src = GST_WRAPPER_CAMERA_BIN_SRC (camerasrc);

  /* TODO should we access this directly? Maybe a macro is better? */
  if (src->mode == MODE_IMAGE) {
    start_image_capture (src);
    src->image_capture_count = 1;
  } else if (src->mode == MODE_VIDEO) {
    GstCaps *caps = NULL;

    if (src->video_renegotiate) {
      GstCaps *anycaps = gst_caps_new_any ();
      g_mutex_unlock (&camerasrc->capturing_mutex);
      gst_wrapper_camera_bin_reset_video_src_caps (src, anycaps);
      g_mutex_lock (&camerasrc->capturing_mutex);

      /* clean capsfilter caps so they don't interfere here */
      g_object_set (src->src_filter, "caps", NULL, NULL);
      if (src->src_zoom_filter)
        g_object_set (src->src_zoom_filter, "caps", NULL, NULL);

      GST_DEBUG_OBJECT (src, "Getting allowed videosrc caps");
      caps = gst_pad_get_allowed_caps (src->vidsrc);
      GST_DEBUG_OBJECT (src, "Video src caps %" GST_PTR_FORMAT, caps);

      src->video_renegotiate = FALSE;
      g_mutex_unlock (&camerasrc->capturing_mutex);
      gst_wrapper_camera_bin_reset_video_src_caps (src, caps);
      g_mutex_lock (&camerasrc->capturing_mutex);
      gst_caps_unref (caps);
      gst_caps_unref (anycaps);
    }
    if (src->video_rec_status == GST_VIDEO_RECORDING_STATUS_DONE) {
      src->video_rec_status = GST_VIDEO_RECORDING_STATUS_STARTING;
    }
  } else {
    g_assert_not_reached ();
    return FALSE;
  }
  return TRUE;
}

static void
gst_wrapper_camera_bin_src_stop_capture (GstBaseCameraSrc * camerasrc)
{
  GstWrapperCameraBinSrc *src = GST_WRAPPER_CAMERA_BIN_SRC (camerasrc);

  /* TODO shoud we access this directly? Maybe a macro is better? */
  if (src->mode == MODE_VIDEO) {
    if (src->video_rec_status == GST_VIDEO_RECORDING_STATUS_STARTING) {
      GST_DEBUG_OBJECT (src, "Aborting, had not started recording");
      src->video_rec_status = GST_VIDEO_RECORDING_STATUS_DONE;

    } else if (src->video_rec_status == GST_VIDEO_RECORDING_STATUS_RUNNING) {
      GST_DEBUG_OBJECT (src, "Marking video recording as finishing");
      src->video_rec_status = GST_VIDEO_RECORDING_STATUS_FINISHING;
    }
  } else {
    src->image_capture_count = 0;
  }
}

static GstStateChangeReturn
gst_wrapper_camera_bin_src_change_state (GstElement * element,
    GstStateChange trans)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstWrapperCameraBinSrc *self = GST_WRAPPER_CAMERA_BIN_SRC (element);

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, trans);

  if (ret == GST_STATE_CHANGE_FAILURE)
    goto end;

  switch (trans) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      self->video_renegotiate = TRUE;
      self->image_renegotiate = TRUE;
      self->drop_newseg = FALSE;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    default:
      break;
  }

end:
  return ret;
}

static void
gst_wrapper_camera_bin_src_class_init (GstWrapperCameraBinSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseCameraSrcClass *gstbasecamerasrc_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  gstbasecamerasrc_class = GST_BASE_CAMERA_SRC_CLASS (klass);

  gobject_class->dispose = gst_wrapper_camera_bin_src_dispose;
  gobject_class->finalize =
      (GObjectFinalizeFunc) gst_wrapper_camera_bin_src_finalize;
  gobject_class->set_property = gst_wrapper_camera_bin_src_set_property;
  gobject_class->get_property = gst_wrapper_camera_bin_src_get_property;

  /* g_object_class_install_property .... */
  g_object_class_install_property (gobject_class, PROP_VIDEO_SRC,
      g_param_spec_object ("video-source", "Video source",
          "The video source element to be used",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_VIDEO_SRC_FILTER,
      g_param_spec_object ("video-source-filter", "Video source filter",
          "Optional video source filter element",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state = gst_wrapper_camera_bin_src_change_state;

  gstbasecamerasrc_class->construct_pipeline =
      gst_wrapper_camera_bin_src_construct_pipeline;
  gstbasecamerasrc_class->set_zoom = gst_wrapper_camera_bin_src_set_zoom;
  gstbasecamerasrc_class->set_mode = gst_wrapper_camera_bin_src_set_mode;
  gstbasecamerasrc_class->start_capture =
      gst_wrapper_camera_bin_src_start_capture;
  gstbasecamerasrc_class->stop_capture =
      gst_wrapper_camera_bin_src_stop_capture;

  GST_DEBUG_CATEGORY_INIT (wrapper_camera_bin_src_debug, "wrappercamerabinsrc",
      0, "wrapper camera src");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&vfsrc_template));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&imgsrc_template));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&vidsrc_template));

  gst_element_class_set_static_metadata (gstelement_class,
      "Wrapper camera src element for camerabin2", "Source/Video",
      "Wrapper camera src element for camerabin2",
      "Thiago Santos <thiago.sousa.santos@collabora.com>");
}

static void
gst_wrapper_camera_bin_src_init (GstWrapperCameraBinSrc * self)
{
  self->vfsrc =
      gst_ghost_pad_new_no_target (GST_BASE_CAMERA_SRC_VIEWFINDER_PAD_NAME,
      GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (self), self->vfsrc);

  self->imgsrc =
      gst_ghost_pad_new_no_target (GST_BASE_CAMERA_SRC_IMAGE_PAD_NAME,
      GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (self), self->imgsrc);

  self->vidsrc =
      gst_ghost_pad_new_no_target (GST_BASE_CAMERA_SRC_VIDEO_PAD_NAME,
      GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (self), self->vidsrc);

  gst_pad_set_event_function (self->imgsrc,
      GST_DEBUG_FUNCPTR (gst_wrapper_camera_bin_src_src_event));
  gst_pad_set_event_function (self->vidsrc,
      GST_DEBUG_FUNCPTR (gst_wrapper_camera_bin_src_src_event));

  /* TODO where are variables reset? */
  self->image_capture_count = 0;
  self->video_rec_status = GST_VIDEO_RECORDING_STATUS_DONE;
  self->video_renegotiate = TRUE;
  self->image_renegotiate = TRUE;
  self->mode = GST_BASE_CAMERA_SRC_CAST (self)->mode;
  self->app_vid_filter = NULL;
}

gboolean
gst_wrapper_camera_bin_src_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "wrappercamerabinsrc", GST_RANK_NONE,
      gst_wrapper_camera_bin_src_get_type ());
}
