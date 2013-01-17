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
 * SECTION:ges-timeline-pipeline
 * @short_description: Convenience GstPipeline for editing.
 *
 * #GESTimelinePipeline allows developers to view and render #GESTimeline
 * in a simple fashion.
 * Its usage is inspired by the 'playbin' element from gst-plugins-base.
 */

#include <gst/gst.h>
#include <stdio.h>
#include "ges-internal.h"
#include "ges-timeline-pipeline.h"
#include "ges-screenshot.h"

#define DEFAULT_TIMELINE_MODE  TIMELINE_MODE_PREVIEW

/* Structure corresponding to a timeline - sink link */

typedef struct
{
  GESTrack *track;
  GstElement *tee;
  GstPad *srcpad;               /* Timeline source pad */
  GstPad *playsinkpad;
  GstPad *encodebinpad;
  GstPad *blocked_pad;
  gulong probe_id;
} OutputChain;

G_DEFINE_TYPE (GESTimelinePipeline, ges_timeline_pipeline, GST_TYPE_PIPELINE);

struct _GESTimelinePipelinePrivate
{
  GESTimeline *timeline;
  GstElement *playsink;
  GstElement *encodebin;
  /* Note : urisink is only created when a URI has been provided */
  GstElement *urisink;

  GESPipelineFlags mode;

  GList *chains;

  GstEncodingProfile *profile;
};

enum
{
  PROP_0,
  PROP_AUDIO_SINK,
  PROP_VIDEO_SINK,
  PROP_TIMELINE,
  PROP_MODE,
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

static GstStateChangeReturn ges_timeline_pipeline_change_state (GstElement *
    element, GstStateChange transition);

static OutputChain *get_output_chain_for_track (GESTimelinePipeline * self,
    GESTrack * track);
static OutputChain *new_output_chain_for_track (GESTimelinePipeline * self,
    GESTrack * track);

static void
ges_timeline_pipeline_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESTimelinePipeline *self = GES_TIMELINE_PIPELINE (object);

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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_pipeline_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESTimelinePipeline *self = GES_TIMELINE_PIPELINE (object);

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
      ges_timeline_pipeline_add_timeline (GES_TIMELINE_PIPELINE (object),
          g_value_get_object (value));
      break;
    case PROP_MODE:
      ges_timeline_pipeline_set_mode (GES_TIMELINE_PIPELINE (object),
          g_value_get_flags (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_pipeline_dispose (GObject * object)
{
  GESTimelinePipeline *self = GES_TIMELINE_PIPELINE (object);

  if (self->priv->playsink) {
    if (self->priv->mode & (TIMELINE_MODE_PREVIEW))
      gst_bin_remove (GST_BIN (object), self->priv->playsink);
    else
      gst_object_unref (self->priv->playsink);
    self->priv->playsink = NULL;
  }

  if (self->priv->encodebin) {
    if (self->priv->mode & (TIMELINE_MODE_RENDER | TIMELINE_MODE_SMART_RENDER))
      gst_bin_remove (GST_BIN (object), self->priv->encodebin);
    else
      gst_object_unref (self->priv->encodebin);
    self->priv->encodebin = NULL;
  }

  if (self->priv->profile) {
    gst_encoding_profile_unref (self->priv->profile);
    self->priv->profile = NULL;
  }

  G_OBJECT_CLASS (ges_timeline_pipeline_parent_class)->dispose (object);
}

static void
ges_timeline_pipeline_class_init (GESTimelinePipelineClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESTimelinePipelinePrivate));

  object_class->dispose = ges_timeline_pipeline_dispose;
  object_class->get_property = ges_timeline_pipeline_get_property;
  object_class->set_property = ges_timeline_pipeline_set_property;

  /**
   * GESTimelinePipeline:audio-sink:
   *
   * Audio sink for the preview.
   */
  properties[PROP_AUDIO_SINK] = g_param_spec_object ("audio-sink", "Audio Sink",
      "Audio sink for the preview.",
      GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_AUDIO_SINK,
      properties[PROP_AUDIO_SINK]);

  /**
   * GESTimelinePipeline:video-sink:
   *
   * Video sink for the preview.
   */
  properties[PROP_VIDEO_SINK] = g_param_spec_object ("video-sink", "Video Sink",
      "Video sink for the preview.",
      GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_VIDEO_SINK,
      properties[PROP_VIDEO_SINK]);

  /**
   * GESTimelinePipeline:timeline:
   *
   * Timeline to use in this pipeline. See also
   * ges_timeline_pipeline_add_timeline() for more info.
   */
  properties[PROP_TIMELINE] = g_param_spec_object ("timeline", "Timeline",
      "Timeline to use in this pipeline. See also "
      "ges_timeline_pipeline_add_timeline() for more info.",
      GES_TYPE_TIMELINE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_TIMELINE,
      properties[PROP_TIMELINE]);

  /**
   * GESTimelinePipeline:mode:
   *
   * Pipeline mode. See ges_timeline_pipeline_set_mode() for more
   * info.
   */
  properties[PROP_MODE] = g_param_spec_flags ("mode", "Mode",
      "Pipeline mode. See ges_timeline_pipeline_set_mode() for more info.",
      GES_TYPE_PIPELINE_FLAGS, DEFAULT_TIMELINE_MODE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_MODE,
      properties[PROP_MODE]);

  element_class->change_state =
      GST_DEBUG_FUNCPTR (ges_timeline_pipeline_change_state);

  /* TODO : Add state_change handlers
   * Don't change state if we don't have a timeline */
}

static void
ges_timeline_pipeline_init (GESTimelinePipeline * self)
{
  GST_INFO_OBJECT (self, "Creating new 'playsink'");
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TIMELINE_PIPELINE, GESTimelinePipelinePrivate);

  self->priv->playsink =
      gst_element_factory_make ("playsink", "internal-sinks");
  self->priv->encodebin =
      gst_element_factory_make ("encodebin", "internal-encodebin");
  /* Limit encodebin buffering to 1 buffer since we know the various
   * stream fed to it are decoupled already */
  g_object_set (self->priv->encodebin, "queue-buffers-max", (guint) 1,
      "queue-bytes-max", (guint) 0, "queue-time-max", (guint64) 0,
      "avoid-reencoding", TRUE, NULL);

  if (G_UNLIKELY (self->priv->playsink == NULL))
    goto no_playsink;
  if (G_UNLIKELY (self->priv->encodebin == NULL))
    goto no_encodebin;

  ges_timeline_pipeline_set_mode (self, DEFAULT_TIMELINE_MODE);

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
 * ges_timeline_pipeline_new:
 *
 * Creates a new conveninence #GESTimelinePipeline.
 *
 * Returns: the new #GESTimelinePipeline.
 */
GESTimelinePipeline *
ges_timeline_pipeline_new (void)
{
  return g_object_new (GES_TYPE_TIMELINE_PIPELINE, NULL);
}

#define TRACK_COMPATIBLE_PROFILE(tracktype, profile)			\
  ( (GST_IS_ENCODING_AUDIO_PROFILE (profile) && (tracktype) == GES_TRACK_TYPE_AUDIO) || \
    (GST_IS_ENCODING_VIDEO_PROFILE (profile) && (tracktype) == GES_TRACK_TYPE_VIDEO))

static gboolean
ges_timeline_pipeline_update_caps (GESTimelinePipeline * self)
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

    allstreams = (GList *)
        gst_encoding_container_profile_get_profiles (
        (GstEncodingContainerProfile *) self->priv->profile);

    /* Find a matching stream setting */
    for (lstream = allstreams; lstream; lstream = lstream->next) {
      GstEncodingProfile *prof = (GstEncodingProfile *) lstream->data;

      if (TRACK_COMPATIBLE_PROFILE (track->type, prof)) {
        if (self->priv->mode == TIMELINE_MODE_SMART_RENDER) {
          GstCaps *ocaps, *rcaps;

          GST_DEBUG ("Smart Render mode, setting input caps");
          ocaps = gst_encoding_profile_get_input_caps (prof);
          if (track->type == GES_TRACK_TYPE_AUDIO)
            rcaps = gst_caps_new_empty_simple ("audio/x-raw");
          else
            rcaps = gst_caps_new_empty_simple ("video/x-raw");
          gst_caps_append (ocaps, rcaps);
          ges_track_set_caps (track, ocaps);
        } else {
          GstCaps *caps = NULL;

          /* Raw preview or rendering mode */
          if (track->type == GES_TRACK_TYPE_VIDEO)
            caps = gst_caps_new_empty_simple ("video/x-raw");
          else if (track->type == GES_TRACK_TYPE_AUDIO)
            caps = gst_caps_new_empty_simple ("audio/x-raw");

          if (caps) {
            ges_track_set_caps (track, caps);
            gst_caps_unref (caps);
          }
        }
        break;
      }
    }

    g_object_unref (track);
  }

  if (tracks)
    g_list_free (tracks);

  GST_DEBUG ("Done updating caps");

  return TRUE;
}

static GstStateChangeReturn
ges_timeline_pipeline_change_state (GstElement * element,
    GstStateChange transition)
{
  GESTimelinePipeline *self;
  GstStateChangeReturn ret;

  self = GES_TIMELINE_PIPELINE (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (G_UNLIKELY (self->priv->timeline == NULL)) {
        GST_ERROR_OBJECT (element,
            "No GESTimeline set on the pipeline, cannot play !");
        ret = GST_STATE_CHANGE_FAILURE;
        goto done;
      }
      if (self->
          priv->mode & (TIMELINE_MODE_RENDER | TIMELINE_MODE_SMART_RENDER))
        GST_DEBUG ("rendering => Updating pipeline caps");
      if (!ges_timeline_pipeline_update_caps (self)) {
        GST_ERROR_OBJECT (element, "Error setting the caps for rendering");
        ret = GST_STATE_CHANGE_FAILURE;
        goto done;
      }
      /* Set caps on all tracks according to profile if present */
      break;
    default:
      break;
  }

  ret =
      GST_ELEMENT_CLASS (ges_timeline_pipeline_parent_class)->change_state
      (element, transition);

done:
  return ret;
}

static OutputChain *
new_output_chain_for_track (GESTimelinePipeline * self, GESTrack * track)
{
  OutputChain *chain;

  chain = g_new0 (OutputChain, 1);
  chain->track = track;

  return chain;
}

static OutputChain *
get_output_chain_for_track (GESTimelinePipeline * self, GESTrack * track)
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
get_compatible_unlinked_pad (GstElement * element, GstPad * pad)
{
  GstPad *res = NULL;
  GstIterator *pads;
  gboolean done = FALSE;
  GstCaps *srccaps;
  GValue paditem = { 0, };

  if (G_UNLIKELY (pad == NULL))
    goto no_pad;

  GST_DEBUG ("element : %s, pad %s:%s",
      GST_ELEMENT_NAME (element), GST_DEBUG_PAD_NAME (pad));

  if (GST_PAD_DIRECTION (pad) == GST_PAD_SRC)
    pads = gst_element_iterate_sink_pads (element);
  else
    pads = gst_element_iterate_src_pads (element);
  srccaps = gst_pad_query_caps (pad, NULL);

  GST_DEBUG ("srccaps %" GST_PTR_FORMAT, srccaps);

  while (!done) {
    switch (gst_iterator_next (pads, &paditem)) {
      case GST_ITERATOR_OK:
      {
        GstPad *testpad = g_value_get_object (&paditem);

        if (!gst_pad_is_linked (testpad)) {
          GstCaps *sinkcaps = gst_pad_query_caps (testpad, NULL);

          GST_DEBUG ("sinkccaps %" GST_PTR_FORMAT, sinkcaps);

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
  gst_caps_unref (srccaps);

  return res;

no_pad:
  {
    GST_ERROR ("No pad to check against");
    return NULL;
  }
}

static GstPadProbeReturn
pad_blocked (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  /* no nothing */
  GST_DEBUG_OBJECT (pad, "blocked callback, blocked");
  return GST_PAD_PROBE_OK;
}

static void
pad_added_cb (GstElement * timeline, GstPad * pad, GESTimelinePipeline * self)
{
  OutputChain *chain;
  GESTrack *track;
  GstPad *sinkpad;
  GstCaps *caps;
  gboolean reconfigured = FALSE;

  caps = gst_pad_query_caps (pad, NULL);

  GST_DEBUG_OBJECT (self, "new pad %s:%s , caps:%" GST_PTR_FORMAT,
      GST_DEBUG_PAD_NAME (pad), caps);

  gst_caps_unref (caps);

  track = ges_timeline_get_track_for_pad (self->priv->timeline, pad);

  if (G_UNLIKELY (!track)) {
    GST_WARNING_OBJECT (self, "Couldn't find coresponding track !");
    return;
  }

  /* Don't connect track if it's not going to be used */
  if (track->type == GES_TRACK_TYPE_VIDEO &&
      !(self->priv->mode & TIMELINE_MODE_PREVIEW_VIDEO) &&
      !(self->priv->mode & TIMELINE_MODE_RENDER) &&
      !(self->priv->mode & TIMELINE_MODE_SMART_RENDER)) {
    GST_DEBUG_OBJECT (self, "Video track... but we don't need it. Not linking");
  }
  if (track->type == GES_TRACK_TYPE_AUDIO &&
      !(self->priv->mode & TIMELINE_MODE_PREVIEW_AUDIO) &&
      !(self->priv->mode & TIMELINE_MODE_RENDER) &&
      !(self->priv->mode & TIMELINE_MODE_SMART_RENDER)) {
    GST_DEBUG_OBJECT (self, "Audio track... but we don't need it. Not linking");
  }

  /* Get an existing chain or create it */
  if (!(chain = get_output_chain_for_track (self, track)))
    chain = new_output_chain_for_track (self, track);
  chain->srcpad = pad;

  /* Adding tee */
  chain->tee = gst_element_factory_make ("tee", NULL);
  gst_bin_add (GST_BIN_CAST (self), chain->tee);
  gst_element_sync_state_with_parent (chain->tee);

  /* Linking pad to tee */
  sinkpad = gst_element_get_static_pad (chain->tee, "sink");
  gst_pad_link_full (pad, sinkpad, GST_PAD_LINK_CHECK_NOTHING);
  gst_object_unref (sinkpad);

  /* Connect playsink */
  if (self->priv->mode & TIMELINE_MODE_PREVIEW) {
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
                gst_element_get_request_pad (self->priv->playsink,
                    sinkpad_name)))) {
      GST_ERROR_OBJECT (self, "Couldn't get a pad from the playsink !");
      goto error;
    }

    tmppad = gst_element_get_request_pad (chain->tee, "src_%u");
    if (G_UNLIKELY (gst_pad_link_full (tmppad, sinkpad,
                GST_PAD_LINK_CHECK_NOTHING) != GST_PAD_LINK_OK)) {
      GST_ERROR_OBJECT (self, "Couldn't link track pad to playsink");
      gst_object_unref (tmppad);
      goto error;
    }
    chain->blocked_pad = tmppad;
    GST_DEBUG_OBJECT (tmppad, "blocking pad");
    chain->probe_id = gst_pad_add_probe (tmppad,
        GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM, pad_blocked, NULL, NULL);

    GST_DEBUG ("Reconfiguring playsink");

    /* reconfigure playsink */
    g_signal_emit_by_name (self->priv->playsink, "reconfigure", &reconfigured);
    GST_DEBUG ("'reconfigure' returned %d", reconfigured);

    /* We still hold a reference on the sinkpad */
    chain->playsinkpad = sinkpad;
  }

  /* Connect to encodebin */
  if (self->priv->mode & (TIMELINE_MODE_RENDER | TIMELINE_MODE_SMART_RENDER)) {
    GstPad *tmppad;
    GST_DEBUG_OBJECT (self, "Connecting to encodebin");

    if (!chain->encodebinpad) {
      /* Check for unused static pads */
      sinkpad = get_compatible_unlinked_pad (self->priv->encodebin, pad);

      if (sinkpad == NULL) {
        GstCaps *caps = gst_pad_query_caps (pad, NULL);

        /* If no compatible static pad is available, request a pad */
        g_signal_emit_by_name (self->priv->encodebin, "request-pad", caps,
            &sinkpad);
        gst_caps_unref (caps);

        if (G_UNLIKELY (sinkpad == NULL)) {
          GST_ERROR_OBJECT (self, "Couldn't get a pad from encodebin !");
          goto error;
        }
      }
      chain->encodebinpad = sinkpad;
    }

    tmppad = gst_element_get_request_pad (chain->tee, "src_%u");
    if (G_UNLIKELY (gst_pad_link_full (tmppad,
                chain->encodebinpad,
                GST_PAD_LINK_CHECK_NOTHING) != GST_PAD_LINK_OK)) {
      GST_WARNING_OBJECT (self, "Couldn't link track pad to playsink");
      goto error;
    }
    gst_object_unref (tmppad);

  }

  /* If chain wasn't already present, insert it in list */
  if (!get_output_chain_for_track (self, track))
    self->priv->chains = g_list_append (self->priv->chains, chain);

  GST_DEBUG ("done");
  return;

error:
  {
    if (chain->tee) {
      gst_bin_remove (GST_BIN_CAST (self), chain->tee);
    }
    if (sinkpad)
      gst_object_unref (sinkpad);
    g_free (chain);
  }
}

static void
pad_removed_cb (GstElement * timeline, GstPad * pad, GESTimelinePipeline * self)
{
  OutputChain *chain;
  GESTrack *track;
  GstPad *peer;

  GST_DEBUG_OBJECT (self, "pad removed %s:%s", GST_DEBUG_PAD_NAME (pad));

  if (G_UNLIKELY (!(track =
              ges_timeline_get_track_for_pad (self->priv->timeline, pad)))) {
    GST_WARNING_OBJECT (self, "Couldn't find coresponding track !");
    return;
  }

  if (G_UNLIKELY (!(chain = get_output_chain_for_track (self, track)))) {
    GST_DEBUG_OBJECT (self, "Pad wasn't used");
    return;
  }

  /* Unlink encodebin */
  if (chain->encodebinpad) {
    peer = gst_pad_get_peer (chain->encodebinpad);
    gst_pad_unlink (peer, chain->encodebinpad);
    gst_object_unref (peer);
    gst_element_release_request_pad (self->priv->encodebin,
        chain->encodebinpad);
  }

  /* Unlink playsink */
  if (chain->playsinkpad) {
    peer = gst_pad_get_peer (chain->playsinkpad);
    gst_pad_unlink (peer, chain->playsinkpad);
    gst_object_unref (peer);
    gst_element_release_request_pad (self->priv->playsink, chain->playsinkpad);
    gst_object_unref (chain->playsinkpad);
  }

  if (chain->blocked_pad) {
    GST_DEBUG_OBJECT (chain->blocked_pad, "unblocking pad");
    gst_pad_remove_probe (chain->blocked_pad, chain->probe_id);
    gst_object_unref (chain->blocked_pad);
    chain->blocked_pad = NULL;
    chain->probe_id = 0;
  }

  /* Unlike/remove tee */
  peer = gst_element_get_static_pad (chain->tee, "sink");
  gst_pad_unlink (pad, peer);
  gst_object_unref (peer);
  gst_element_set_state (chain->tee, GST_STATE_NULL);
  gst_bin_remove (GST_BIN (self), chain->tee);

  self->priv->chains = g_list_remove (self->priv->chains, chain);
  g_free (chain);

  GST_DEBUG ("done");
}

static void
no_more_pads_cb (GstElement * timeline, GESTimelinePipeline * self)
{
  GList *tmp;

  GST_DEBUG ("received no-more-pads");
  for (tmp = self->priv->chains; tmp; tmp = g_list_next (tmp)) {
    OutputChain *chain = (OutputChain *) tmp->data;

    if (chain->blocked_pad) {
      GST_DEBUG_OBJECT (chain->blocked_pad, "unblocking pad");
      gst_pad_remove_probe (chain->blocked_pad, chain->probe_id);
      gst_object_unref (chain->blocked_pad);
      chain->blocked_pad = NULL;
      chain->probe_id = 0;
    }
  }
}

/**
 * ges_timeline_pipeline_add_timeline:
 * @pipeline: a #GESTimelinePipeline
 * @timeline: the #GESTimeline to set on the @pipeline.
 *
 * Sets the timeline to use in this pipeline.
 *
 * The reference to the @timeline will be stolen by the @pipeline.
 *
 * Returns: TRUE if the @timeline could be successfully set on the @pipeline,
 * else FALSE.
 */
gboolean
ges_timeline_pipeline_add_timeline (GESTimelinePipeline * pipeline,
    GESTimeline * timeline)
{
  g_return_val_if_fail (pipeline->priv->timeline == NULL, FALSE);
  g_return_val_if_fail (timeline != NULL, FALSE);

  GST_DEBUG ("pipeline:%p, timeline:%p", timeline, pipeline);

  if (G_UNLIKELY (!gst_bin_add (GST_BIN_CAST (pipeline),
              GST_ELEMENT (timeline)))) {
    return FALSE;
  }
  pipeline->priv->timeline = timeline;

  /* Connect to pipeline */
  g_signal_connect (timeline, "pad-added", (GCallback) pad_added_cb, pipeline);
  g_signal_connect (timeline, "pad-removed", (GCallback) pad_removed_cb,
      pipeline);
  g_signal_connect (timeline, "no-more-pads", (GCallback) no_more_pads_cb,
      pipeline);

  return TRUE;
}

/**
 * ges_timeline_pipeline_set_render_settings:
 * @pipeline: a #GESTimelinePipeline
 * @output_uri: the URI to which the timeline will be rendered
 * @profile: the #GstEncodingProfile to use to render the timeline.
 *
 * Specify where the pipeline shall be rendered and with what settings.
 *
 * A copy of @profile and @output_uri will be done internally, the caller can
 * safely free those values afterwards.
 *
 * This method must be called before setting the pipeline mode to
 * #TIMELINE_MODE_RENDER
 *
 * Returns: %TRUE if the settings were aknowledged properly, else %FALSE
 */
gboolean
ges_timeline_pipeline_set_render_settings (GESTimelinePipeline * pipeline,
    const gchar * output_uri, GstEncodingProfile * profile)
{
  GError *err = NULL;

  /* Clear previous URI sink if it existed */
  /* FIXME : We should figure out if it was added to the pipeline,
   * and if so, remove it. */
  if (pipeline->priv->urisink) {
    g_object_unref (pipeline->priv->urisink);
    pipeline->priv->urisink = NULL;
  }

  pipeline->priv->urisink =
      gst_element_make_from_uri (GST_URI_SINK, output_uri, "urisink", &err);
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
      !(!(pipeline->priv->mode & TIMELINE_MODE_SMART_RENDER)), NULL);
  g_object_set (pipeline->priv->encodebin, "profile", profile, NULL);
  pipeline->priv->profile =
      (GstEncodingProfile *) gst_encoding_profile_ref (profile);

  return TRUE;
}

/**
 * ges_timeline_pipeline_set_mode:
 * @pipeline: a #GESTimelinePipeline
 * @mode: the #GESPipelineFlags to use
 *
 * switches the @pipeline to the specified @mode. The default mode when
 * creating a #GESTimelinePipeline is #TIMELINE_MODE_PREVIEW.
 *
 * Note: The @pipeline will be set to #GST_STATE_NULL during this call due to
 * the internal changes that happen. The caller will therefore have to 
 * set the @pipeline to the requested state after calling this method.
 *
 * Returns: %TRUE if the mode was properly set, else %FALSE.
 **/
gboolean
ges_timeline_pipeline_set_mode (GESTimelinePipeline * pipeline,
    GESPipelineFlags mode)
{
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

  /* remove no-longer needed components */
  if (pipeline->priv->mode & TIMELINE_MODE_PREVIEW &&
      !(mode & TIMELINE_MODE_PREVIEW)) {
    /* Disable playsink */
    GST_DEBUG ("Disabling playsink");
    g_object_ref (pipeline->priv->playsink);
    gst_bin_remove (GST_BIN_CAST (pipeline), pipeline->priv->playsink);
  }
  if ((pipeline->priv->mode &
          (TIMELINE_MODE_RENDER | TIMELINE_MODE_SMART_RENDER)) &&
      !(mode & (TIMELINE_MODE_RENDER | TIMELINE_MODE_SMART_RENDER))) {
    GList *tmp;
    GstCaps *caps;

    for (tmp = pipeline->priv->timeline->tracks; tmp; tmp = tmp->next) {
      GESTrackType type = GES_TRACK (tmp->data)->type;

      if (type == GES_TRACK_TYPE_AUDIO)
        caps = gst_caps_new_empty_simple ("audio/x-raw");
      else if (type == GES_TRACK_TYPE_VIDEO)
        caps = gst_caps_new_empty_simple ("video/x-raw");
      else
        continue;

      ges_track_set_caps (GES_TRACK (tmp->data), caps);
      gst_caps_unref (caps);
    }

    /* Disable render bin */
    GST_DEBUG ("Disabling rendering bin");
    g_object_ref (pipeline->priv->encodebin);
    g_object_ref (pipeline->priv->urisink);
    gst_bin_remove_many (GST_BIN_CAST (pipeline),
        pipeline->priv->encodebin, pipeline->priv->urisink, NULL);
  }

  /* Add new elements */
  if (!(pipeline->priv->mode & TIMELINE_MODE_PREVIEW) &&
      (mode & TIMELINE_MODE_PREVIEW)) {
    /* Add playsink */
    GST_DEBUG ("Adding playsink");

    if (!gst_bin_add (GST_BIN_CAST (pipeline), pipeline->priv->playsink)) {
      GST_ERROR_OBJECT (pipeline, "Couldn't add playsink");
      return FALSE;
    }
  }
  if (!(pipeline->priv->mode &
          (TIMELINE_MODE_RENDER | TIMELINE_MODE_SMART_RENDER)) &&
      (mode & (TIMELINE_MODE_RENDER | TIMELINE_MODE_SMART_RENDER))) {
    /* Adding render bin */
    GST_DEBUG ("Adding render bin");

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
        !(!(mode & TIMELINE_MODE_SMART_RENDER)), NULL);

    gst_element_link_pads_full (pipeline->priv->encodebin, "src",
        pipeline->priv->urisink, "sink", GST_PAD_LINK_CHECK_NOTHING);
  }

  /* FIXUPS */
  /* FIXME
   * If we are rendering, set playsink to sync=False,
   * If we are NOT rendering, set playsink to sync=TRUE */

  pipeline->priv->mode = mode;

  return TRUE;
}

/**
 * ges_timeline_pipeline_get_thumbnail:
 * @self: a #GESTimelinePipeline in %GST_STATE_PLAYING or %GST_STATE_PAUSED
 * @caps: (transfer none): caps specifying current format. Use %GST_CAPS_ANY
 * for native size.
 *
 * Returns a #GstSample with the currently playing image in the format specified by
 * caps. The caller should free the sample with #gst_sample_unref when finished. If ANY
 * caps are specified, the information will be returned in the whatever format
 * is currently used by the sink. This information can be retrieve from caps
 * associated with the buffer.
 *
 * Returns: (transfer full): a #GstSample or %NULL
 */

GstSample *
ges_timeline_pipeline_get_thumbnail (GESTimelinePipeline * self, GstCaps * caps)
{
  GstElement *sink;

  sink = self->priv->playsink;

  if (!sink) {
    GST_WARNING ("thumbnailing can only be done if we have a playsink");
    return NULL;
  }

  return ges_play_sink_convert_frame (sink, caps);
}

/**
 * ges_timeline_pipeline_save_thumbnail:
 * @self: a #GESTimelinePipeline in %GST_STATE_PLAYING or %GST_STATE_PAUSED
 * @width: the requested width or -1 for native size
 * @height: the requested height or -1 for native size
 * @format: a string specifying the desired mime type (for example,
 * image/jpeg)
 * @location: the path to save the thumbnail
 *
 * Saves the current frame to the specified @location.
 * 
 * Returns: %TRUE if the thumbnail was properly save, else %FALSE.
 */
/* FIXME 0.11: save_thumbnail should have a GError parameter */
gboolean
ges_timeline_pipeline_save_thumbnail (GESTimelinePipeline * self, int width, int
    height, const gchar * format, const gchar * location)
{
  GstMapInfo map_info;
  GstBuffer *b;
  GstSample *sample;
  GstCaps *caps;
  gboolean res = TRUE;

  caps = gst_caps_from_string (format);

  if (width > 1)
    gst_caps_set_simple (caps, "width", G_TYPE_INT, width, NULL);

  if (height > 1)
    gst_caps_set_simple (caps, "height", G_TYPE_INT, height, NULL);

  if (!(sample = ges_timeline_pipeline_get_thumbnail (self, caps))) {
    gst_caps_unref (caps);
    return res;
  }

  b = gst_sample_get_buffer (sample);
  if (gst_buffer_map (b, &map_info, GST_MAP_READ)) {
    GError *err = NULL;

    if (!g_file_set_contents (location, (const char *) map_info.data,
            map_info.size, &err)) {
      GST_WARNING ("Could not save thumbnail: %s", err->message);
      g_error_free (err);
      res = FALSE;
    }
  }

  gst_caps_unref (caps);
  gst_buffer_unmap (b, &map_info);
  gst_buffer_unref (b);
  gst_sample_unref (sample);

  return res;
}

/**
 * ges_timeline_pipeline_get_thumbnail_rgb24:
 * @self: a #GESTimelinePipeline in %GST_STATE_PLAYING or %GST_STATE_PAUSED
 * @width: the requested width or -1 for native size
 * @height: the requested height or -1 for native size
 *
 * A convenience method for @ges_timeline_pipeline_get_thumbnail which
 * returns a buffer in 24-bit RGB, optionally scaled to the specified width
 * and height. If -1 is specified for either dimension, it will be left at
 * native size. You can retreive this information from the caps associated
 * with the buffer.
 * 
 * The caller is responsible for unreffing the returned sample with
 * #gst_sample_unref.
 *
 * Returns: (transfer full): a #GstSample or %NULL
 */

GstSample *
ges_timeline_pipeline_get_thumbnail_rgb24 (GESTimelinePipeline * self,
    gint width, gint height)
{
  GstSample *ret;
  GstCaps *caps;

  caps = gst_caps_new_simple ("video/x-raw", "format", G_TYPE_STRING,
      "RGB", NULL);

  if (width != -1)
    gst_caps_set_simple (caps, "width", G_TYPE_INT, (gint) width, NULL);

  if (height != -1)
    gst_caps_set_simple (caps, "height", G_TYPE_INT, (gint) height, NULL);

  ret = ges_timeline_pipeline_get_thumbnail (self, caps);
  gst_caps_unref (caps);
  return ret;
}

/**
 * ges_timeline_pipeline_preview_get_video_sink:
 * @self: a #GESTimelinePipeline
 *
 * Obtains a pointer to playsink's video sink element that is used for
 * displaying video when the #GESTimelinePipeline is in %TIMELINE_MODE_PREVIEW
 *
 * The caller is responsible for unreffing the returned element with
 * #gst_object_unref.
 *
 * Returns: (transfer full): a pointer to the playsink video sink #GstElement
 */
GstElement *
ges_timeline_pipeline_preview_get_video_sink (GESTimelinePipeline * self)
{
  GstElement *sink = NULL;

  g_object_get (self->priv->playsink, "video-sink", &sink, NULL);

  return sink;
};

/**
 * ges_timeline_pipeline_preview_set_video_sink:
 * @self: a #GESTimelinePipeline in %GST_STATE_NULL
 * @sink: (transfer none): a video sink #GstElement
 *
 * Sets playsink's video sink element that is used for displaying video when
 * the #GESTimelinePipeline is in %TIMELINE_MODE_PREVIEW
 */
void
ges_timeline_pipeline_preview_set_video_sink (GESTimelinePipeline * self,
    GstElement * sink)
{
  g_object_set (self->priv->playsink, "video-sink", sink, NULL);
};

/**
 * ges_timeline_pipeline_preview_get_audio_sink:
 * @self: a #GESTimelinePipeline
 *
 * Obtains a pointer to playsink's audio sink element that is used for
 * displaying audio when the #GESTimelinePipeline is in %TIMELINE_MODE_PREVIEW
 *
 * The caller is responsible for unreffing the returned element with
 * #gst_object_unref.
 *
 * Returns: (transfer full): a pointer to the playsink audio sink #GstElement
 */
GstElement *
ges_timeline_pipeline_preview_get_audio_sink (GESTimelinePipeline * self)
{
  GstElement *sink = NULL;

  g_object_get (self->priv->playsink, "audio-sink", &sink, NULL);

  return sink;
};

/**
 * ges_timeline_pipeline_preview_set_audio_sink:
 * @self: a #GESTimelinePipeline in %GST_STATE_NULL
 * @sink: (transfer none): a audio sink #GstElement
 *
 * Sets playsink's audio sink element that is used for displaying audio when
 * the #GESTimelinePipeline is in %TIMELINE_MODE_PREVIEW
 */
void
ges_timeline_pipeline_preview_set_audio_sink (GESTimelinePipeline * self,
    GstElement * sink)
{
  g_object_set (self->priv->playsink, "audio-sink", sink, NULL);
};
