/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <edward.hervey@collabora.co.uk>
 *               2009 Nokia Corporation
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
 * SECTION:gespipeline
 * @title: GESPipeline
 * @short_description: Convenience GstPipeline for editing.
 * @symbols:
 * - ges_play_sink_convert_frame
 *
 * A #GESPipeline can take an audio-video #GESTimeline and conveniently
 * link its #GESTrack-s to an internal #playsink element, for
 * preview/playback, and an internal #encodebin element, for rendering.
 * You can switch between these modes using ges_pipeline_set_mode().
 *
 * You can choose the specific audio and video sinks used for previewing
 * the timeline by setting the #GESPipeline:audio-sink and
 * #GESPipeline:video-sink properties.
 *
 * You can set the encoding and save location used in rendering by calling
 * ges_pipeline_set_render_settings().
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/videooverlay.h>
#include <stdio.h>

#include "ges-internal.h"
#include "ges-pipeline.h"
#include "ges-screenshot.h"
#include "ges-audio-track.h"
#include "ges-video-track.h"

GST_DEBUG_CATEGORY_STATIC (ges_pipeline_debug);
#undef GST_CAT_DEFAULT
#define GST_CAT_DEFAULT ges_pipeline_debug

#define DEFAULT_TIMELINE_MODE  GES_PIPELINE_MODE_PREVIEW
#define IN_RENDERING_MODE(timeline) ((timeline->priv->mode) & (GES_PIPELINE_MODE_RENDER | GES_PIPELINE_MODE_SMART_RENDER))
#define CHECK_THREAD(pipeline) g_assert(pipeline->priv->valid_thread == g_thread_self())

/* Structure corresponding to a timeline - sink link */

typedef struct
{
  GESTrack *track;
  GstElement *tee;
  GstPad *srcpad;               /* Timeline source pad */
  GstPad *playsinkpad;
  GstPad *encodebinpad;
} OutputChain;


struct _GESPipelinePrivate
{
  GESTimeline *timeline;
  GstElement *playsink;
  GstElement *encodebin;
  /* Note : urisink is only created when a URI has been provided */
  GstElement *urisink;

  GESPipelineFlags mode;

  GMutex dyn_mutex;
  GList *chains;
  GList *not_rendered_tracks;

  GstEncodingProfile *profile;

  GThread *valid_thread;
};

enum
{
  PROP_0,
  PROP_AUDIO_SINK,
  PROP_VIDEO_SINK,
  PROP_TIMELINE,
  PROP_MODE,
  PROP_AUDIO_FILTER,
  PROP_VIDEO_FILTER,
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

static GstStateChangeReturn ges_pipeline_change_state (GstElement *
    element, GstStateChange transition);

static OutputChain *get_output_chain_for_track (GESPipeline * self,
    GESTrack * track);
static OutputChain *new_output_chain_for_track (GESPipeline * self,
    GESTrack * track);
static void _link_track (GESPipeline * self, GESTrack * track);
static void _unlink_track (GESPipeline * self, GESTrack * track);

/****************************************************
 *    Video Overlay vmethods implementation         *
 ****************************************************/
static void
_overlay_expose (GstVideoOverlay * overlay)
{
  GESPipeline *pipeline = GES_PIPELINE (overlay);

  gst_video_overlay_expose (GST_VIDEO_OVERLAY (pipeline->priv->playsink));
}

static void
_overlay_handle_events (GstVideoOverlay * overlay, gboolean handle_events)
{
  GESPipeline *pipeline = GES_PIPELINE (overlay);

  gst_video_overlay_handle_events (GST_VIDEO_OVERLAY (pipeline->priv->playsink),
      handle_events);
}

static void
_overlay_set_render_rectangle (GstVideoOverlay * overlay, gint x,
    gint y, gint width, gint height)
{
  GESPipeline *pipeline = GES_PIPELINE (overlay);

  gst_video_overlay_set_render_rectangle (GST_VIDEO_OVERLAY (pipeline->priv->
          playsink), x, y, width, height);
}

static void
_overlay_set_window_handle (GstVideoOverlay * overlay, guintptr handle)
{
  GESPipeline *pipeline = GES_PIPELINE (overlay);

  gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (pipeline->
          priv->playsink), handle);
}

static void
video_overlay_init (gpointer g_iface, gpointer g_iface_data)
{
  GstVideoOverlayInterface *iface = (GstVideoOverlayInterface *) g_iface;

  iface->expose = _overlay_expose;
  iface->handle_events = _overlay_handle_events;
  iface->set_render_rectangle = _overlay_set_render_rectangle;
  iface->set_window_handle = _overlay_set_window_handle;
}

G_DEFINE_TYPE_WITH_CODE (GESPipeline, ges_pipeline,
    GST_TYPE_PIPELINE, G_ADD_PRIVATE (GESPipeline)
    G_IMPLEMENT_INTERFACE (GST_TYPE_VIDEO_OVERLAY, video_overlay_init));

static void
ges_pipeline_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESPipeline *self = GES_PIPELINE (object);

  switch (property_id) {
    case PROP_AUDIO_SINK:
      g_object_get_property (G_OBJECT (self->priv->playsink), "audio-sink",
          value);
      break;
    case PROP_VIDEO_SINK:
      g_object_get_property (G_OBJECT (self->priv->playsink), "video-sink",
          value);
      break;
    case PROP_TIMELINE:
      g_value_set_object (value, self->priv->timeline);
      break;
    case PROP_MODE:
      g_value_set_flags (value, self->priv->mode);
      break;
    case PROP_AUDIO_FILTER:
      g_object_get_property (G_OBJECT (self->priv->playsink), "audio-filter",
          value);
      break;
    case PROP_VIDEO_FILTER:
      g_object_get_property (G_OBJECT (self->priv->playsink), "video-filter",
          value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_pipeline_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESPipeline *self = GES_PIPELINE (object);

  switch (property_id) {
    case PROP_AUDIO_SINK:
      g_object_set_property (G_OBJECT (self->priv->playsink), "audio-sink",
          value);
      break;
    case PROP_VIDEO_SINK:
      g_object_set_property (G_OBJECT (self->priv->playsink), "video-sink",
          value);
      break;
    case PROP_TIMELINE:
      ges_pipeline_set_timeline (GES_PIPELINE (object),
          g_value_get_object (value));
      break;
    case PROP_MODE:
      ges_pipeline_set_mode (GES_PIPELINE (object), g_value_get_flags (value));
      break;
    case PROP_AUDIO_FILTER:
      g_object_set (self->priv->playsink, "audio-filter",
          GST_ELEMENT (g_value_get_object (value)), NULL);
      break;
    case PROP_VIDEO_FILTER:
      g_object_set (self->priv->playsink, "video-filter",
          GST_ELEMENT (g_value_get_object (value)), NULL);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
_timeline_track_added_cb (GESTimeline * timeline, GESTrack * track,
    GESPipeline * pipeline)
{
  track_disable_last_gap (track,
      !!(pipeline->priv->mode & (GES_PIPELINE_MODE_RENDER |
              GES_PIPELINE_MODE_SMART_RENDER)));
  _link_track (pipeline, track);
}

static void
_timeline_track_removed_cb (GESTimeline * timeline, GESTrack * track,
    GESPipeline * pipeline)
{
  _unlink_track (pipeline, track);
}

static void
ges_pipeline_constructed (GObject * object)
{
  GESPipeline *self = GES_PIPELINE (object);

  ges_pipeline_set_mode (self, DEFAULT_TIMELINE_MODE);

  ((GObjectClass *) ges_pipeline_parent_class)->constructed (object);
}

static void
ges_pipeline_dispose (GObject * object)
{
  GESPipeline *self = GES_PIPELINE (object);

  if (self->priv->playsink) {
    if (self->priv->mode & (GES_PIPELINE_MODE_PREVIEW))
      gst_bin_remove (GST_BIN (object), self->priv->playsink);
    else
      gst_object_unref (self->priv->playsink);
    self->priv->playsink = NULL;
  }

  if (self->priv->encodebin) {
    if (self->priv->mode & (GES_PIPELINE_MODE_RENDER |
            GES_PIPELINE_MODE_SMART_RENDER))
      gst_bin_remove (GST_BIN (object), self->priv->encodebin);
    else
      gst_object_unref (self->priv->encodebin);
    self->priv->encodebin = NULL;
  }

  if (self->priv->profile) {
    gst_encoding_profile_unref (self->priv->profile);
    self->priv->profile = NULL;
  }

  if (self->priv->timeline) {
    g_signal_handlers_disconnect_by_func (self->priv->timeline,
        _timeline_track_added_cb, self);
    g_signal_handlers_disconnect_by_func (self->priv->timeline,
        _timeline_track_removed_cb, self);
    gst_element_set_state (GST_ELEMENT (self->priv->timeline), GST_STATE_NULL);
  }

  G_OBJECT_CLASS (ges_pipeline_parent_class)->dispose (object);
}

static void
ges_pipeline_class_init (GESPipelineClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (ges_pipeline_debug, "gespipeline",
      GST_DEBUG_FG_YELLOW, "ges pipeline");


  object_class->constructed = ges_pipeline_constructed;
  object_class->dispose = ges_pipeline_dispose;
  object_class->get_property = ges_pipeline_get_property;
  object_class->set_property = ges_pipeline_set_property;

  /**
   * GESPipeline:audio-sink:
   *
   * The audio sink used for preview. This exposes the
   * #playsink:audio-sink property of the internal #playsink.
   */
  properties[PROP_AUDIO_SINK] = g_param_spec_object ("audio-sink", "Audio Sink",
      "Audio sink for the preview.",
      GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GESPipeline:video-sink:
   *
   * The video sink used for preview. This exposes the
   * #playsink:video-sink property of the internal #playsink.
   */
  properties[PROP_VIDEO_SINK] = g_param_spec_object ("video-sink", "Video Sink",
      "Video sink for the preview.",
      GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GESPipeline:timeline:
   *
   * The timeline used by this pipeline, whose content it will play and
   * render, or %NULL if the pipeline does not yet have a timeline.
   *
   * Note that after you set the timeline for the first time, subsequent
   * calls to change the timeline will fail.
   */
  properties[PROP_TIMELINE] = g_param_spec_object ("timeline", "Timeline",
      "The timeline to use in this pipeline.",
      GES_TYPE_TIMELINE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GESPipeline:mode:
   *
   * The pipeline's mode. In preview mode (for audio or video, or both)
   * the pipeline can display the timeline's content to an end user. In
   * rendering mode the pipeline can encode the timeline's content and
   * save it to a file.
   */
  properties[PROP_MODE] = g_param_spec_flags ("mode", "Mode",
      "The pipeline's mode.",
      GES_TYPE_PIPELINE_FLAGS, DEFAULT_TIMELINE_MODE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GESPipeline:audio-filter:
   *
   * The audio filter(s) to apply during playback in preview mode,
   * immediately before the #GESPipeline:audio-sink. This exposes the
   * #playsink:audio-filter property of the internal #playsink.
   *
   * Since: 1.6.0
   */
  properties[PROP_AUDIO_FILTER] =
      g_param_spec_object ("audio-filter", "Audio filter",
      "the audio filter(s) to apply, if possible", GST_TYPE_ELEMENT,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GESPipeline:video-filter:
   *
   * The video filter(s) to apply during playback in preview mode,
   * immediately before the #GESPipeline:video-sink. This exposes the
   * #playsink:video-filter property of the internal #playsink.
   *
   * Since: 1.6.0
   */
  properties[PROP_VIDEO_FILTER] =
      g_param_spec_object ("video-filter", "Video filter",
      "the Video filter(s) to apply, if possible", GST_TYPE_ELEMENT,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST, properties);

  element_class->change_state = GST_DEBUG_FUNCPTR (ges_pipeline_change_state);

  /* TODO : Add state_change handlers
   * Don't change state if we don't have a timeline */
}

static void
ges_pipeline_init (GESPipeline * self)
{
  GST_INFO_OBJECT (self, "Creating new 'playsink'");
  self->priv = ges_pipeline_get_instance_private (self);
  self->priv->valid_thread = g_thread_self ();

  self->priv->playsink =
      gst_element_factory_make ("playsink", "internal-sinks");
  self->priv->encodebin =
      gst_element_factory_make ("encodebin", "internal-encodebin");
  g_object_set (self->priv->encodebin, "avoid-reencoding", TRUE, NULL);

  if (G_UNLIKELY (self->priv->playsink == NULL))
    goto no_playsink;
  if (G_UNLIKELY (self->priv->encodebin == NULL))
    goto no_encodebin;

  return;

no_playsink:
  {
    GST_ERROR_OBJECT (self, "Can't create playsink instance !");
    return;
  }
no_encodebin:
  {
    GST_ERROR_OBJECT (self, "Can't create encodebin instance !");
    return;
  }
}

/**
 * ges_pipeline_new:
 *
 * Creates a new pipeline.
 *
 * Returns: (transfer floating): The newly created pipeline.
 */
GESPipeline *
ges_pipeline_new (void)
{
  return GES_PIPELINE (gst_element_factory_make ("gespipeline", NULL));
}

#define TRACK_COMPATIBLE_PROFILE(tracktype, profile)			\
  ( (GST_IS_ENCODING_AUDIO_PROFILE (profile) && (tracktype) == GES_TRACK_TYPE_AUDIO) || \
    (GST_IS_ENCODING_VIDEO_PROFILE (profile) && (tracktype) == GES_TRACK_TYPE_VIDEO))

static gboolean
_track_is_compatible_with_profile (GESPipeline * self, GESTrack * track,
    GstEncodingProfile * prof)
{
  if (TRACK_COMPATIBLE_PROFILE (track->type, prof))
    return TRUE;

  return FALSE;
}

static gboolean
ges_pipeline_update_caps (GESPipeline * self)
{
  GList *ltrack, *tracks, *lstream;

  if (!self->priv->profile)
    return TRUE;

  GST_DEBUG ("Updating track caps");

  tracks = ges_timeline_get_tracks (self->priv->timeline);

  /* Take each stream of the encoding profile and find a matching
   * track to set the caps on */
  for (ltrack = tracks; ltrack; ltrack = ltrack->next) {
    GESTrack *track = (GESTrack *) ltrack->data;
    GList *allstreams;

    if (!GST_IS_ENCODING_CONTAINER_PROFILE (self->priv->profile)) {
      if (_track_is_compatible_with_profile (self, track, self->priv->profile)) {
        gst_object_unref (track);

        goto done;
      } else {
        gst_object_unref (track);
        continue;
      }
    }

    allstreams = (GList *)
        gst_encoding_container_profile_get_profiles (
        (GstEncodingContainerProfile *) self->priv->profile);

    /* Find a matching stream setting */
    for (lstream = allstreams; lstream; lstream = lstream->next) {
      GstEncodingProfile *prof = (GstEncodingProfile *) lstream->data;
      if (_track_is_compatible_with_profile (self, track, prof))
        break;
    }

    gst_object_unref (track);
  }

done:
  if (tracks)
    g_list_free (tracks);

  GST_DEBUG ("Done updating caps");

  return TRUE;
}

static void
_link_tracks (GESPipeline * pipeline)
{
  GList *tmp;

  GST_DEBUG_OBJECT (pipeline, "Linking tracks");

  if (!pipeline->priv->timeline) {
    GST_INFO_OBJECT (pipeline, "Not timeline set yet, doing nothing");

    return;
  }

  for (tmp = pipeline->priv->timeline->tracks; tmp; tmp = tmp->next)
    _link_track (pipeline, tmp->data);

  if (IN_RENDERING_MODE (pipeline)) {
    GString *unlinked_issues = NULL;
    GstIterator *pads;
    gboolean done = FALSE;
    GValue paditem = { 0, };

    pads = gst_element_iterate_sink_pads (pipeline->priv->encodebin);
    while (!done) {
      switch (gst_iterator_next (pads, &paditem)) {
        case GST_ITERATOR_OK:
        {
          GstPad *testpad = g_value_get_object (&paditem);
          if (!gst_pad_is_linked (testpad)) {
            GstCaps *sinkcaps = gst_pad_query_caps (testpad, NULL);
            gchar *caps_string = gst_caps_to_string (sinkcaps);
            gchar *path_string =
                gst_object_get_path_string (GST_OBJECT (testpad));
            gst_caps_unref (sinkcaps);

            if (!unlinked_issues)
              unlinked_issues =
                  g_string_new ("Following encodebin pads are not linked:\n");

            g_string_append_printf (unlinked_issues, " - %s: %s", path_string,
                caps_string);
            g_free (caps_string);
            g_free (path_string);
          }
          g_value_reset (&paditem);
        }
          break;
        case GST_ITERATOR_DONE:
        case GST_ITERATOR_ERROR:
          done = TRUE;
          break;
        case GST_ITERATOR_RESYNC:
          gst_iterator_resync (pads);
          break;
      }
    }
    g_value_reset (&paditem);
    gst_iterator_free (pads);

    if (unlinked_issues) {
      GST_ELEMENT_ERROR (pipeline, STREAM, FAILED, (NULL), ("%s",
              unlinked_issues->str));
      g_string_free (unlinked_issues, TRUE);
    }
  }
}

static void
_unlink_tracks (GESPipeline * pipeline)
{
  GList *tmp;

  GST_DEBUG_OBJECT (pipeline, "Disconnecting all tracks");
  if (!pipeline->priv->timeline) {
    GST_INFO_OBJECT (pipeline, "Not timeline set yet, doing nothing");

    return;
  }

  for (tmp = pipeline->priv->timeline->tracks; tmp; tmp = tmp->next)
    _unlink_track (pipeline, tmp->data);
}

static GstStateChangeReturn
ges_pipeline_change_state (GstElement * element, GstStateChange transition)
{
  GESPipeline *self;
  GstStateChangeReturn ret;

  self = GES_PIPELINE (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (G_UNLIKELY (self->priv->timeline == NULL)) {
        GST_ERROR_OBJECT (element,
            "No GESTimeline set on the pipeline, cannot play !");
        ret = GST_STATE_CHANGE_FAILURE;
        goto done;
      }
      if (IN_RENDERING_MODE (self)) {
        GST_DEBUG ("rendering => Updating pipeline caps");
        /* Set caps on all tracks according to profile if present */
        if (!ges_pipeline_update_caps (self)) {
          GST_ERROR_OBJECT (element, "Error setting the caps for rendering");
          ret = GST_STATE_CHANGE_FAILURE;
          goto done;
        }
      }
      _link_tracks (self);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    {
      GList *tmp;

      for (tmp = self->priv->not_rendered_tracks; tmp; tmp = tmp->next)
        gst_element_set_locked_state (tmp->data, FALSE);
    }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    {
      GstElement *queue = gst_bin_get_by_name (GST_BIN (self->priv->playsink),
          "vqueue");

      if (queue) {
        GST_INFO_OBJECT (self, "Setting playsink video queue max-size-time to"
            " 2 seconds.");
        g_object_set (G_OBJECT (queue), "max-size-buffers", 0,
            "max-size-bytes", 0, "max-size-time", (gint64) 2 * GST_SECOND,
            NULL);
        gst_object_unref (queue);
      }
      break;
    }
    default:
      break;
  }

  ret =
      GST_ELEMENT_CLASS (ges_pipeline_parent_class)->change_state
      (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    case GST_STATE_CHANGE_READY_TO_NULL:
    case GST_STATE_CHANGE_NULL_TO_NULL:
      _unlink_tracks (self);
      break;
    default:
      break;
  }

done:
  return ret;
}

static OutputChain *
new_output_chain_for_track (GESPipeline * self, GESTrack * track)
{
  OutputChain *chain;

  chain = g_new0 (OutputChain, 1);
  chain->track = track;

  return chain;
}

static OutputChain *
get_output_chain_for_track (GESPipeline * self, GESTrack * track)
{
  GList *tmp;

  for (tmp = self->priv->chains; tmp; tmp = tmp->next) {
    OutputChain *chain = (OutputChain *) tmp->data;
    if (chain->track == track)
      return chain;
  }

  return NULL;
}

/* Fetches a compatible pad on the target element which isn't already
 * linked */
static GstPad *
get_compatible_unlinked_pad (GstElement * element, GESTrack * track)
{
  GstPad *res = NULL;
  GstIterator *pads;
  gboolean done = FALSE;
  const GstCaps *srccaps;
  GValue paditem = { 0, };

  if (G_UNLIKELY (track == NULL))
    goto no_track;

  GST_DEBUG_OBJECT (element, " track %" GST_PTR_FORMAT, track);

  pads = gst_element_iterate_sink_pads (element);
  srccaps = ges_track_get_caps (track);

  GST_DEBUG_OBJECT (track, "srccaps %" GST_PTR_FORMAT, srccaps);

  while (!done) {
    switch (gst_iterator_next (pads, &paditem)) {
      case GST_ITERATOR_OK:
      {
        GstPad *testpad = g_value_get_object (&paditem);

        if (!gst_pad_is_linked (testpad)) {
          GstCaps *sinkcaps = gst_pad_query_caps (testpad, NULL);

          GST_DEBUG_OBJECT (track, "sinkccaps %" GST_PTR_FORMAT, sinkcaps);

          if (gst_caps_can_intersect (srccaps, sinkcaps)) {
            res = gst_object_ref (testpad);
            done = TRUE;
          }
          gst_caps_unref (sinkcaps);
        }
        g_value_reset (&paditem);
      }
        break;
      case GST_ITERATOR_DONE:
      case GST_ITERATOR_ERROR:
        done = TRUE;
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (pads);
        break;
    }
  }
  g_value_reset (&paditem);
  gst_iterator_free (pads);

  return res;

no_track:
  {
    GST_ERROR ("No track to check against");
    return NULL;
  }
}

static void
_link_track (GESPipeline * self, GESTrack * track)
{
  GstPad *pad;
  OutputChain *chain;
  GstPad *sinkpad;
  GstCaps *caps;
  GstPadLinkReturn lret;
  gboolean reconfigured = FALSE;
  gboolean ignore;

  pad = ges_timeline_get_pad_for_track (self->priv->timeline, track);
  if (G_UNLIKELY (!pad)) {
    GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
        ("Trying to link %" GST_PTR_FORMAT
            " but no pad is exposed for it.", track));
    return;
  }

  caps = gst_pad_query_caps (pad, NULL);
  GST_DEBUG_OBJECT (self, "new pad %s:%s , caps:%" GST_PTR_FORMAT,
      GST_DEBUG_PAD_NAME (pad), caps);
  gst_caps_unref (caps);

  /* FIXME: provide a way for the user to select which audio, video or
   * text track to use in preview mode when a timeline has multiple audio,
   * video or text tracks. Also provide a way to switch between these. */

  /* Don't connect track if it's not going to be used */
  ignore = TRUE;
  /* only support audio and video. Technically, preview mode could support
   * text quite easily, but this isn't yet the case for rendering using
   * encodebin */
  if (track->type == GES_TRACK_TYPE_AUDIO ||
      track->type == GES_TRACK_TYPE_VIDEO) {
    if (IN_RENDERING_MODE (self))
      ignore = FALSE;
    else if (track->type == GES_TRACK_TYPE_VIDEO &&
        self->priv->mode & GES_PIPELINE_MODE_PREVIEW_VIDEO)
      ignore = FALSE;
    else if (track->type == GES_TRACK_TYPE_AUDIO &&
        self->priv->mode & GES_PIPELINE_MODE_PREVIEW_AUDIO)
      ignore = FALSE;
  }

  if (ignore) {
    gst_object_unref (pad);
    GST_DEBUG_OBJECT (self, "Ignoring track (type %u). Not linking",
        track->type);
    return;
  }

  /* Get an existing chain or create it */
  if (!(chain = get_output_chain_for_track (self, track)))
    chain = new_output_chain_for_track (self, track);

  if (chain->tee) {
    gst_object_unref (pad);
    GST_INFO_OBJECT (self, "Chain is already built (%" GST_PTR_FORMAT ")",
        chain->encodebinpad ? chain->encodebinpad : chain->playsinkpad);

    return;
  }

  chain->srcpad = pad;

  /* Adding tee */
  chain->tee = gst_element_factory_make ("tee", NULL);
  gst_bin_add (GST_BIN_CAST (self), chain->tee);
  gst_element_sync_state_with_parent (chain->tee);

  /* Linking pad to tee */
  sinkpad = gst_element_get_static_pad (chain->tee, "sink");
  lret = gst_pad_link (pad, sinkpad);
  if (lret != GST_PAD_LINK_OK) {
    GST_ELEMENT_ERROR (self, CORE, NEGOTIATION,
        (NULL), ("Could not link the tee (%s)", gst_pad_link_get_name (lret)));
    goto error;
  }

  gst_object_unref (sinkpad);

  /* Connect playsink */
  if (self->priv->mode & GES_PIPELINE_MODE_PREVIEW) {
    const gchar *sinkpad_name;
    GstPad *tmppad;

    GST_DEBUG_OBJECT (self, "Connecting to playsink");

    switch (track->type) {
      case GES_TRACK_TYPE_VIDEO:
        sinkpad_name = "video_sink";
        break;
      case GES_TRACK_TYPE_AUDIO:
        sinkpad_name = "audio_sink";
        break;
      case GES_TRACK_TYPE_TEXT:
        sinkpad_name = "text_sink";
        break;
      default:
        GST_WARNING_OBJECT (self, "Can't handle tracks of type %d yet",
            track->type);
        goto error;
    }

    /* Request a sinkpad from playsink */
    if (G_UNLIKELY (!(sinkpad =
                gst_element_request_pad_simple (self->priv->playsink,
                    sinkpad_name)))) {
      GST_ELEMENT_ERROR (self, CORE, NEGOTIATION,
          (NULL), ("Could not get a pad from playsink for %s", sinkpad_name));
      goto error;
    }

    tmppad = gst_element_request_pad_simple (chain->tee, "src_%u");
    lret = gst_pad_link_full (tmppad, sinkpad, GST_PAD_LINK_CHECK_NOTHING);
    if (G_UNLIKELY (lret != GST_PAD_LINK_OK)) {
      gst_object_unref (tmppad);
      GST_ELEMENT_ERROR (self, CORE, NEGOTIATION,
          (NULL),
          ("Could not link %" GST_PTR_FORMAT " and %" GST_PTR_FORMAT " (%s)",
              tmppad, sinkpad, gst_pad_link_get_name (lret)));
      goto error;
    }
    gst_object_unref (tmppad);

    GST_DEBUG ("Reconfiguring playsink");

    /* reconfigure playsink */
    g_signal_emit_by_name (self->priv->playsink, "reconfigure", &reconfigured);
    GST_DEBUG ("'reconfigure' returned %d", reconfigured);

    /* We still hold a reference on the sinkpad */
    chain->playsinkpad = sinkpad;
  }

  /* Connect to encodebin */
  if (IN_RENDERING_MODE (self)) {
    GstPad *tmppad;
    GST_DEBUG_OBJECT (self, "Connecting to encodebin");

    if (!chain->encodebinpad) {
      /* Check for unused static pads */
      sinkpad = get_compatible_unlinked_pad (self->priv->encodebin, track);

      if (sinkpad == NULL) {
        GstCaps *caps = gst_pad_query_caps (pad, NULL);

        /* If no compatible static pad is available, request a pad */
        g_signal_emit_by_name (self->priv->encodebin, "request-pad", caps,
            &sinkpad);

        if (G_UNLIKELY (sinkpad == NULL)) {
          gst_element_set_locked_state (GST_ELEMENT (track), TRUE);

          self->priv->not_rendered_tracks =
              g_list_append (self->priv->not_rendered_tracks, track);

          GST_INFO_OBJECT (self,
              "Couldn't get a pad from encodebin for: %" GST_PTR_FORMAT, caps);
          gst_caps_unref (caps);
          goto error;
        }

        gst_caps_unref (caps);
      }
      chain->encodebinpad = sinkpad;
      GST_INFO_OBJECT (track, "Linked to %" GST_PTR_FORMAT, sinkpad);
    }

    tmppad = gst_element_request_pad_simple (chain->tee, "src_%u");
    if (G_UNLIKELY (gst_pad_link_full (tmppad, chain->encodebinpad,
                GST_PAD_LINK_CHECK_NOTHING) != GST_PAD_LINK_OK)) {
      GST_ERROR_OBJECT (self, "Couldn't link track pad to encodebin");
      goto error;
    }
    gst_object_unref (tmppad);

  }

  /* If chain wasn't already present, insert it in list */
  if (!get_output_chain_for_track (self, track))
    self->priv->chains = g_list_append (self->priv->chains, chain);

  GST_DEBUG ("done");
  gst_object_unref (pad);
  return;

error:
  {
    gst_object_unref (pad);
    if (chain->tee) {
      gst_element_set_state (chain->tee, GST_STATE_NULL);
      gst_bin_remove (GST_BIN_CAST (self), chain->tee);
    }
    if (sinkpad)
      gst_object_unref (sinkpad);

    g_free (chain);
  }
}

static void
_unlink_track (GESPipeline * self, GESTrack * track)
{
  OutputChain *chain;

  GST_DEBUG_OBJECT (self, "Unlinking removed %" GST_PTR_FORMAT, track);

  if (G_UNLIKELY (!(chain = get_output_chain_for_track (self, track)))) {
    GST_DEBUG_OBJECT (self, "Track wasn't used");
    return;
  }

  /* Unlink encodebin */
  if (chain->encodebinpad) {
    GstPad *peer = gst_pad_get_peer (chain->encodebinpad);
    gst_pad_unlink (peer, chain->encodebinpad);
    gst_object_unref (peer);
    gst_element_release_request_pad (self->priv->encodebin,
        chain->encodebinpad);
    gst_object_unref (chain->encodebinpad);
  }

  /* Unlink playsink */
  if (chain->playsinkpad) {
    GstPad *peer = gst_pad_get_peer (chain->playsinkpad);
    gst_pad_unlink (peer, chain->playsinkpad);
    gst_object_unref (peer);
    gst_element_release_request_pad (self->priv->playsink, chain->playsinkpad);
    gst_object_unref (chain->playsinkpad);
  }

  gst_element_set_state (chain->tee, GST_STATE_NULL);
  gst_bin_remove (GST_BIN (self), chain->tee);

  self->priv->chains = g_list_remove (self->priv->chains, chain);
  g_free (chain);

  GST_DEBUG ("done");
}

/**
 * ges_pipeline_set_timeline:
 * @pipeline: A #GESPipeline
 * @timeline: (transfer floating): The timeline to set for @pipeline
 *
 * Takes the given timeline and sets it as the #GESPipeline:timeline for
 * the pipeline.
 *
 * Note that you should only call this method once on a given pipeline
 * because a pipeline can not have its #GESPipeline:timeline changed after
 * it has been set.
 *
 * Returns: %TRUE if @timeline was successfully given to @pipeline.
 */
/* FIXME: allow us to set a new timeline and a NULL timeline */
/* FIXME 2.0: since the method can fail, (transfer none) would be more
 * appropriate for the timeline argument (currently, it is only
 * transferred if the method is successful, which makes the memory
 * transfer mixed!) */
gboolean
ges_pipeline_set_timeline (GESPipeline * pipeline, GESTimeline * timeline)
{

  g_return_val_if_fail (GES_IS_PIPELINE (pipeline), FALSE);
  g_return_val_if_fail (GES_IS_TIMELINE (timeline), FALSE);
  g_return_val_if_fail (pipeline->priv->timeline == NULL, FALSE);
  CHECK_THREAD (pipeline);

  GST_DEBUG ("pipeline:%p, timeline:%p", timeline, pipeline);

  if (G_UNLIKELY (!gst_bin_add (GST_BIN_CAST (pipeline),
              GST_ELEMENT (timeline)))) {
    return FALSE;
  }
  pipeline->priv->timeline = timeline;

  g_signal_connect (timeline, "track-added",
      G_CALLBACK (_timeline_track_added_cb), pipeline);
  g_signal_connect (timeline, "track-removed",
      G_CALLBACK (_timeline_track_removed_cb), pipeline);
  /* FIXME Check if we should rollback if we can't sync state */
  gst_element_sync_state_with_parent (GST_ELEMENT (timeline));

  return TRUE;
}

/**
 * ges_pipeline_set_render_settings:
 * @pipeline: A #GESPipeline
 * @output_uri: The URI to save the #GESPipeline:timeline rendering
 * result to
 * @profile: The encoding to use for rendering the #GESPipeline:timeline
 *
 * Specifies encoding setting to be used by the pipeline to render its
 * #GESPipeline:timeline, and where the result should be written to.
 *
 * This method **must** be called before setting the pipeline mode to
 * #GES_PIPELINE_MODE_RENDER.
 *
 * Returns: %TRUE if the settings were successfully set on @pipeline.
 */
gboolean
ges_pipeline_set_render_settings (GESPipeline * pipeline,
    const gchar * output_uri, GstEncodingProfile * profile)
{
  GError *err = NULL;
  GstEncodingProfile *set_profile;
  guint n_videotracks = 0, n_audiotracks = 0;

  g_return_val_if_fail (GES_IS_PIPELINE (pipeline), FALSE);
  CHECK_THREAD (pipeline);

  /*  FIXME Properly handle multi track, for now GESPipeline
   *  only handles single track per type, so we should just set the
   *  presence to 1.
   */
  if (GST_IS_ENCODING_CONTAINER_PROFILE (profile)) {
    const GList *tmpprofiles =
        gst_encoding_container_profile_get_profiles
        (GST_ENCODING_CONTAINER_PROFILE (profile));
    GList *tmptrack, *tracks =
        ges_timeline_get_tracks (pipeline->priv->timeline);

    for (tmptrack = tracks; tmptrack; tmptrack = tmptrack->next) {
      if (GES_IS_AUDIO_TRACK (tmptrack->data))
        n_audiotracks++;
      else if (GES_IS_VIDEO_TRACK (tmptrack->data))
        n_videotracks++;
    }
    g_list_free_full (tracks, gst_object_unref);

    for (; tmpprofiles; tmpprofiles = tmpprofiles->next) {
      if (!gst_encoding_profile_is_enabled (tmpprofiles->data))
        continue;

      if (GST_IS_ENCODING_AUDIO_PROFILE (tmpprofiles->data)) {
        if (n_audiotracks) {
          n_audiotracks--;
        } else {
          GST_INFO_OBJECT (pipeline, "No audio track but got an audio profile, "
              " make it optional: %" GST_PTR_FORMAT, tmpprofiles);
          gst_encoding_profile_set_presence (tmpprofiles->data, 0);

          continue;
        }
      } else if (GST_IS_ENCODING_VIDEO_PROFILE (tmpprofiles->data)) {
        if (n_videotracks) {
          n_videotracks--;
        } else {
          GST_INFO_OBJECT (pipeline, "No video track but got a video profile, "
              " make it optional: %" GST_PTR_FORMAT, tmpprofiles);
          gst_encoding_profile_set_presence (tmpprofiles->data, 0);

          continue;
        }
      } else {
        continue;
      }

      GST_DEBUG_OBJECT (pipeline, "Setting presence to 1!");
      gst_encoding_profile_set_single_segment (tmpprofiles->data, TRUE);
      if (gst_encoding_profile_get_presence (tmpprofiles->data) == 0)
        gst_encoding_profile_set_presence (tmpprofiles->data, 1);
      gst_encoding_profile_set_allow_dynamic_output (tmpprofiles->data, FALSE);
    }
  }

  /* Clear previous URI sink if it existed */
  if (pipeline->priv->urisink) {
    GstObject *sink_parent =
        gst_object_get_parent (GST_OBJECT (pipeline->priv->urisink));
    if (sink_parent == GST_OBJECT (pipeline))
      gst_bin_remove (GST_BIN (pipeline), pipeline->priv->urisink);
    pipeline->priv->urisink = NULL;
    gst_clear_object (&sink_parent);
  }

  pipeline->priv->urisink =
      gst_element_make_from_uri (GST_URI_SINK, output_uri, NULL, &err);
  if (G_UNLIKELY (pipeline->priv->urisink == NULL)) {
    GST_ERROR_OBJECT (pipeline, "Couldn't not create sink for URI %s: '%s'",
        output_uri, ((err
                && err->message) ? err->message : "failed to create element"));
    g_clear_error (&err);
    return FALSE;
  }

  if (pipeline->priv->profile)
    gst_encoding_profile_unref (pipeline->priv->profile);
  g_object_set (pipeline->priv->encodebin, "avoid-reencoding",
      !(!(pipeline->priv->mode & GES_PIPELINE_MODE_SMART_RENDER)), NULL);
  g_object_set (pipeline->priv->encodebin, "profile", profile, NULL);
  g_object_get (pipeline->priv->encodebin, "profile", &set_profile, NULL);

  if (set_profile == NULL) {
    GST_ERROR_OBJECT (pipeline, "Profile %" GST_PTR_FORMAT " could no be set",
        profile);

    return FALSE;
  }

  /* We got a reference when getting back the profile */
  pipeline->priv->profile = profile;

  return TRUE;
}

/**
 * ges_pipeline_get_mode:
 * @pipeline: A #GESPipeline
 *
 * Gets the #GESPipeline:mode of the pipeline.
 *
 * Returns: The current mode of @pipeline.
 **/
GESPipelineFlags
ges_pipeline_get_mode (GESPipeline * pipeline)
{
  return pipeline->priv->mode;
}

/**
 * ges_pipeline_set_mode:
 * @pipeline: A #GESPipeline
 * @mode: The mode to set for @pipeline
 *
 * Sets the #GESPipeline:mode of the pipeline.
 *
 * Note that the pipeline will be set to #GST_STATE_NULL during this call to
 * perform the necessary changes. You will need to set the state again yourself
 * after calling this.
 *
 * > **NOTE**: [Rendering settings](ges_pipeline_set_render_settings) need to be
 * > set before setting @mode to #GES_PIPELINE_MODE_RENDER or
 * > #GES_PIPELINE_MODE_SMART_RENDER, the call to this method will fail
 * > otherwise.
 *
 * Returns: %TRUE if the mode of @pipeline was successfully set to @mode.
 **/
gboolean
ges_pipeline_set_mode (GESPipeline * pipeline, GESPipelineFlags mode)
{

  GList *tmp;
  g_return_val_if_fail (GES_IS_PIPELINE (pipeline), FALSE);
  CHECK_THREAD (pipeline);

  GST_DEBUG_OBJECT (pipeline, "current mode : %d, mode : %d",
      pipeline->priv->mode, mode);

  /* fast-path, nothing to change */
  if (mode == pipeline->priv->mode)
    return TRUE;

  /* FIXME: It would be nice if we are only (de)activating preview
   * modes to not set the whole pipeline to NULL, but instead just
   * do the proper (un)linking to playsink. */

  /* Switch pipeline to NULL since we're changing the configuration */
  gst_element_set_state (GST_ELEMENT_CAST (pipeline), GST_STATE_NULL);


  if (pipeline->priv->timeline) {
    gboolean disabled =
        !!(mode & (GES_PIPELINE_MODE_RENDER | GES_PIPELINE_MODE_SMART_RENDER));

    for (tmp = pipeline->priv->timeline->tracks; tmp; tmp = tmp->next)
      track_disable_last_gap (GES_TRACK (tmp->data), disabled);
  }

  /* remove no-longer needed components */
  if (pipeline->priv->mode & GES_PIPELINE_MODE_PREVIEW &&
      !(mode & GES_PIPELINE_MODE_PREVIEW)) {
    /* Disable playsink */
    GST_DEBUG ("Disabling playsink");
    gst_object_ref (pipeline->priv->playsink);
    gst_bin_remove (GST_BIN_CAST (pipeline), pipeline->priv->playsink);
  }
  if ((pipeline->priv->mode &
          (GES_PIPELINE_MODE_RENDER | GES_PIPELINE_MODE_SMART_RENDER)) &&
      !(mode & (GES_PIPELINE_MODE_RENDER | GES_PIPELINE_MODE_SMART_RENDER))) {

    /* Disable render bin */
    GST_DEBUG ("Disabling rendering bin");
    ges_timeline_thaw_commit (pipeline->priv->timeline);
    gst_object_ref (pipeline->priv->encodebin);
    gst_object_ref (pipeline->priv->urisink);
    gst_bin_remove_many (GST_BIN_CAST (pipeline),
        pipeline->priv->encodebin, pipeline->priv->urisink, NULL);
  }

  /* Add new elements */
  if (!(pipeline->priv->mode & GES_PIPELINE_MODE_PREVIEW) &&
      (mode & GES_PIPELINE_MODE_PREVIEW)) {
    /* Add playsink */
    GST_DEBUG ("Adding playsink");
    if (!gst_bin_add (GST_BIN_CAST (pipeline), pipeline->priv->playsink)) {
      GST_ERROR_OBJECT (pipeline, "Couldn't add playsink");
      return FALSE;
    }
  }
  if (!(pipeline->priv->mode &
          (GES_PIPELINE_MODE_RENDER | GES_PIPELINE_MODE_SMART_RENDER)) &&
      (mode & (GES_PIPELINE_MODE_RENDER | GES_PIPELINE_MODE_SMART_RENDER))) {
    /* Adding render bin */
    GST_DEBUG ("Adding render bin");
    /* in render mode the commit needs to be locked, see #136 */
    ges_timeline_freeze_commit (pipeline->priv->timeline);
    if (G_UNLIKELY (pipeline->priv->urisink == NULL)) {
      GST_ERROR_OBJECT (pipeline, "Output URI not set !");
      return FALSE;
    }
    if (!gst_bin_add (GST_BIN_CAST (pipeline), pipeline->priv->encodebin)) {
      GST_ERROR_OBJECT (pipeline, "Couldn't add encodebin");
      return FALSE;
    }
    if (!gst_bin_add (GST_BIN_CAST (pipeline), pipeline->priv->urisink)) {
      GST_ERROR_OBJECT (pipeline, "Couldn't add URI sink");
      return FALSE;
    }
    g_object_set (pipeline->priv->encodebin, "avoid-reencoding",
        !(!(mode & GES_PIPELINE_MODE_SMART_RENDER)), NULL);

    gst_element_link_pads_full (pipeline->priv->encodebin, "src",
        pipeline->priv->urisink, "sink", GST_PAD_LINK_CHECK_NOTHING);
  }

  if (pipeline->priv->timeline) {
    ges_timeline_set_smart_rendering (pipeline->priv->timeline,
        (mode & GES_PIPELINE_MODE_SMART_RENDER) != 0);
  }

  /* FIXUPS */
  /* FIXME
   * If we are rendering, set playsink to sync=False,
   * If we are NOT rendering, set playsink to sync=TRUE */

  pipeline->priv->mode = mode;

  return TRUE;
}

/**
 * ges_pipeline_get_thumbnail:
 * @self: A #GESPipeline in #GST_STATE_PLAYING or #GST_STATE_PAUSED
 * @caps: (transfer none): Some caps to specifying the desired format, or
 * #GST_CAPS_ANY to use the native format
 *
 * Gets a sample from the pipeline of the currently displayed image in
 * preview, in the specified format.
 *
 * Note that if you use "ANY" caps for @caps, then the current format of
 * the image is used. You can retrieve these caps from the returned sample
 * with gst_sample_get_caps().
 *
 * Returns: (transfer full) (nullable): A sample of @self's current image preview in
 * the format given by @caps, or %NULL if an error prevented fetching the
 * sample.
 */

GstSample *
ges_pipeline_get_thumbnail (GESPipeline * self, GstCaps * caps)
{
  GstElement *sink;

  g_return_val_if_fail (GES_IS_PIPELINE (self), FALSE);
  CHECK_THREAD (self);

  sink = self->priv->playsink;

  if (!sink) {
    GST_WARNING ("thumbnailing can only be done if we have a playsink");
    return NULL;
  }

  return ges_play_sink_convert_frame (sink, caps);
}

/**
 * ges_pipeline_save_thumbnail:
 * @self: A #GESPipeline in %GST_STATE_PLAYING or %GST_STATE_PAUSED
 * @width: The requested pixel width of the image, or -1 to use the native
 * size
 * @height: The requested pixel height of the image, or -1 to use the
 * native size
 * @format: The desired mime type (for example, "image/jpeg")
 * @location: The path to save the thumbnail to
 * @error: An error to be set in case something goes wrong, or %NULL to ignore
 *
 * Saves the currently displayed image of the pipeline in preview to the
 * given location, in the specified dimensions and format.
 *
 * Returns: %TRUE if @self's current image preview was successfully saved
 * to @location using the given @format, @height and @width.
 */
gboolean
ges_pipeline_save_thumbnail (GESPipeline * self, int width, int
    height, const gchar * format, const gchar * location, GError ** error)
{
  GstMapInfo map_info;
  GstBuffer *b;
  GstSample *sample;
  GstCaps *caps;
  gboolean res = TRUE;

  g_return_val_if_fail (GES_IS_PIPELINE (self), FALSE);
  CHECK_THREAD (self);

  caps = gst_caps_from_string (format);

  if (width > 1)
    gst_caps_set_simple (caps, "width", G_TYPE_INT, width, NULL);

  if (height > 1)
    gst_caps_set_simple (caps, "height", G_TYPE_INT, height, NULL);

  if (!(sample = ges_pipeline_get_thumbnail (self, caps))) {
    gst_caps_unref (caps);
    return FALSE;
  }

  b = gst_sample_get_buffer (sample);
  if (gst_buffer_map (b, &map_info, GST_MAP_READ)) {
    if (!g_file_set_contents (location, (const char *) map_info.data,
            map_info.size, error)) {
      GST_WARNING ("Could not save thumbnail: %s",
          error ? (*error)->message : "");
      res = FALSE;
    }
  }

  gst_caps_unref (caps);
  gst_buffer_unmap (b, &map_info);
  gst_sample_unref (sample);

  return res;
}

/**
 * ges_pipeline_get_thumbnail_rgb24:
 * @self: A #GESPipeline in %GST_STATE_PLAYING or %GST_STATE_PAUSED
 * @width: The requested pixel width of the image, or -1 to use the native
 * size
 * @height: The requested pixel height of the image, or -1 to use the
 * native size
 *
 * Gets a sample from the pipeline of the currently displayed image in
 * preview, in the 24-bit "RGB" format and of the desired width and
 * height.
 *
 * See ges_pipeline_get_thumbnail().
 *
 * Returns: (transfer full) (nullable): A sample of @self's current image preview in
 * the "RGB" format, scaled to @width and @height, or %NULL if an error
 * prevented fetching the sample.
 */

GstSample *
ges_pipeline_get_thumbnail_rgb24 (GESPipeline * self, gint width, gint height)
{
  GstSample *ret;
  GstCaps *caps;

  g_return_val_if_fail (GES_IS_PIPELINE (self), FALSE);
  CHECK_THREAD (self);

  caps = gst_caps_new_simple ("video/x-raw", "format", G_TYPE_STRING,
      "RGB", NULL);

  if (width != -1)
    gst_caps_set_simple (caps, "width", G_TYPE_INT, (gint) width, NULL);

  if (height != -1)
    gst_caps_set_simple (caps, "height", G_TYPE_INT, (gint) height, NULL);

  ret = ges_pipeline_get_thumbnail (self, caps);
  gst_caps_unref (caps);
  return ret;
}

/**
 * ges_pipeline_preview_get_video_sink:
 * @self: A #GESPipeline
 *
 * Gets the #GESPipeline:video-sink of the pipeline.
 *
 * Returns: (transfer full) (nullable): The video sink used by @self for preview.
 */
GstElement *
ges_pipeline_preview_get_video_sink (GESPipeline * self)
{
  GstElement *sink = NULL;

  g_return_val_if_fail (GES_IS_PIPELINE (self), FALSE);
  CHECK_THREAD (self);

  g_object_get (self->priv->playsink, "video-sink", &sink, NULL);

  return sink;
};

/**
 * ges_pipeline_preview_set_video_sink:
 * @self: A #GESPipeline in #GST_STATE_NULL
 * @sink: (transfer none) (nullable): A video sink for @self to use for preview
 *
 * Sets the #GESPipeline:video-sink of the pipeline.
 */
void
ges_pipeline_preview_set_video_sink (GESPipeline * self, GstElement * sink)
{
  g_return_if_fail (GES_IS_PIPELINE (self));
  CHECK_THREAD (self);

  g_object_set (self->priv->playsink, "video-sink", sink, NULL);
};

/**
 * ges_pipeline_preview_get_audio_sink:
 * @self: A #GESPipeline
 *
 * Gets the #GESPipeline:audio-sink of the pipeline.
 *
 * Returns: (transfer full) (nullable): The audio sink used by @self for preview.
 */
GstElement *
ges_pipeline_preview_get_audio_sink (GESPipeline * self)
{
  GstElement *sink = NULL;

  g_return_val_if_fail (GES_IS_PIPELINE (self), FALSE);
  CHECK_THREAD (self);

  g_object_get (self->priv->playsink, "audio-sink", &sink, NULL);

  return sink;
};

/**
 * ges_pipeline_preview_set_audio_sink:
 * @self: A #GESPipeline in #GST_STATE_NULL
 * @sink: (transfer none) (nullable): A audio sink for @self to use for preview
 *
 * Sets the #GESPipeline:audio-sink of the pipeline.
 */
void
ges_pipeline_preview_set_audio_sink (GESPipeline * self, GstElement * sink)
{
  g_return_if_fail (GES_IS_PIPELINE (self));
  CHECK_THREAD (self);

  g_object_set (self->priv->playsink, "audio-sink", sink, NULL);
};
