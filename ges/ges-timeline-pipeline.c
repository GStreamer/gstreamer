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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:ges-timeline-pipeline
 * @short_description: Convenience #GstPipeline for editing.
 *
 * #GESTimelinePipeline allows developers to view and render #GESTimeline
 * in a simple fashion.
 * Its usage is inspired by the 'playbin' element from gst-plugins-base.
 */

#include "ges-internal.h"
#include "ges-timeline-pipeline.h"

#define DEFAULT_TIMELINE_MODE  TIMELINE_MODE_PREVIEW

/* Structure corresponding to a timeline - sink link */

typedef struct
{
  GESTrack *track;
  GstElement *tee;
  GstPad *srcpad;               /* Timeline source pad */
  GstPad *playsinkpad;
  GstPad *encodebinpad;
} OutputChain;

G_DEFINE_TYPE (GESTimelinePipeline, ges_timeline_pipeline, GST_TYPE_PIPELINE);

static GstStateChangeReturn ges_timeline_pipeline_change_state (GstElement *
    element, GstStateChange transition);

static OutputChain *get_output_chain_for_track (GESTimelinePipeline * self,
    GESTrack * track);
static OutputChain *new_output_chain_for_track (GESTimelinePipeline * self,
    GESTrack * track);

static void
ges_timeline_pipeline_finalize (GObject * object)
{
  GESTimelinePipeline *self = GES_TIMELINE_PIPELINE (object);

  if (self->playsink) {
    gst_bin_remove (GST_BIN (object), self->playsink);
    gst_object_unref (self->playsink);
    self->playsink = NULL;
  }
  G_OBJECT_CLASS (ges_timeline_pipeline_parent_class)->finalize (object);
}

static void
ges_timeline_pipeline_class_init (GESTimelinePipelineClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  object_class->finalize = ges_timeline_pipeline_finalize;

  element_class->change_state =
      GST_DEBUG_FUNCPTR (ges_timeline_pipeline_change_state);

  /* TODO : Add state_change handlers
   * Don't change state if we don't have a timeline */
}

static void
ges_timeline_pipeline_init (GESTimelinePipeline * self)
{
  GST_INFO_OBJECT (self, "Creating new 'playsink'");

  self->playsink = gst_element_factory_make ("playsink", "internal-sinks");
  self->encodebin =
      gst_element_factory_make ("encodebin", "internal-encodebin");

  if (G_UNLIKELY (self->playsink == NULL))
    GST_ERROR_OBJECT (self, "Can't create playsink instance !");
  if (G_UNLIKELY (self->encodebin == NULL))
    GST_ERROR_OBJECT (self, "Can't create encodebin instance !");

  ges_timeline_pipeline_set_mode (self, DEFAULT_TIMELINE_MODE);
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

#define TRACK_COMPATIBLE_PROFILE(tracktype, proftype) \
  (((proftype) == GST_ENCODING_PROFILE_AUDIO && (tracktype) == GES_TRACK_TYPE_AUDIO) || \
  ((proftype) == GST_ENCODING_PROFILE_VIDEO && (tracktype) == GES_TRACK_TYPE_VIDEO) || \
   ((proftype) == GST_ENCODING_PROFILE_TEXT && (tracktype) == GES_TRACK_TYPE_TEXT))

static gboolean
ges_timeline_pipeline_update_caps (GESTimelinePipeline * self)
{
  GList *ltrack, *tracks, *lstream;

  if (!self->profile)
    return TRUE;

  GST_DEBUG ("Updating track caps");

  tracks = ges_timeline_get_tracks (self->timeline);

  /* Take each stream of the encoding profile and find a matching
   * track to set the caps on */
  for (ltrack = tracks; ltrack; ltrack = ltrack->next) {
    GESTrack *track = (GESTrack *) ltrack->data;

    /* Find a matching stream setting */
    for (lstream = self->profile->encodingprofiles; lstream;
        lstream = lstream->next) {
      GstStreamEncodingProfile *prof =
          (GstStreamEncodingProfile *) lstream->data;

      if (TRACK_COMPATIBLE_PROFILE (track->type, prof->type)) {
        if (self->mode == TIMELINE_MODE_SMART_RENDER) {
          GstCaps *ocaps, *rcaps;

          GST_DEBUG ("Smart Render mode, setting output caps");
          ocaps = gst_stream_encoding_profile_get_output_caps (prof);
          if (track->type == GES_TRACK_TYPE_AUDIO)
            rcaps = gst_caps_from_string ("audio/x-raw-int;audio/x-raw-float");
          else
            rcaps = gst_caps_from_string ("video/x-raw-yuv;video/x-raw-rgb");
          gst_caps_append (ocaps, rcaps);
          ges_track_set_caps (track, ocaps);
        } else {
          /* Raw preview or rendering mode */
          if (track->type == GES_TRACK_TYPE_VIDEO)
            ges_track_set_caps (track,
                gst_caps_from_string ("video/x-raw-yuv;video/x-raw-rgb"));
          else if (track->type == GES_TRACK_TYPE_AUDIO)
            ges_track_set_caps (track,
                gst_caps_from_string ("audio/x-raw-int;audio/x-raw-float"));
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
      if (G_UNLIKELY (self->timeline == NULL)) {
        GST_ERROR_OBJECT (element,
            "No GESTimeline set on the pipeline, cannot play !");
        ret = GST_STATE_CHANGE_FAILURE;
        goto done;
      }
      if (self->mode & (TIMELINE_MODE_RENDER | TIMELINE_MODE_SMART_RENDER))
        GST_DEBUG ("rendering => Updating pipeline caps");
      if (!ges_timeline_pipeline_update_caps (self)) {
        GST_ERROR_OBJECT (element, "Error setting the caps for rendering");
        ret = GST_STATE_CHANGE_FAILURE;
        goto done;
      }
      /* Set caps on all tracks according to profile if present */
      /* FIXME : Add a new SMART_RENDER mode to avoid decoding */
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

  for (tmp = self->chains; tmp; tmp = tmp->next) {
    OutputChain *chain = (OutputChain *) tmp->data;
    if (chain->track == track)
      return chain;
  }

  return NULL;
}

/* Fetches a ocmpatible pad on the target element which isn't already
 * linked */
static GstPad *
get_compatible_unlinked_pad (GstElement * element, GstPad * pad)
{
  GstPad *res = NULL;
  GstIterator *pads;
  gboolean done = FALSE;
  GstCaps *srccaps;

  GST_DEBUG ("element : %s, pad %s:%s",
      GST_ELEMENT_NAME (element), GST_DEBUG_PAD_NAME (pad));

  if (GST_PAD_DIRECTION (pad) == GST_PAD_SRC)
    pads = gst_element_iterate_sink_pads (element);
  else
    pads = gst_element_iterate_src_pads (element);
  srccaps = gst_pad_get_caps (pad);

  GST_DEBUG ("srccaps %" GST_PTR_FORMAT, srccaps);

  while (!done) {
    gpointer padptr;

    switch (gst_iterator_next (pads, &padptr)) {
      case GST_ITERATOR_OK:
      {
        GstPad *testpad = (GstPad *) padptr;

        if (gst_pad_is_linked (testpad)) {
          gst_object_unref (testpad);
        } else {
          GstCaps *sinkcaps = gst_pad_get_caps (testpad);

          GST_DEBUG ("sinkccaps %" GST_PTR_FORMAT, sinkcaps);

          if (gst_caps_can_intersect (srccaps, sinkcaps)) {
            res = testpad;
            done = TRUE;
          } else
            gst_object_unref (testpad);
          gst_caps_unref (sinkcaps);
        }
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
  gst_iterator_free (pads);
  gst_caps_unref (srccaps);

  return res;
}

static void
pad_added_cb (GstElement * timeline, GstPad * pad, GESTimelinePipeline * self)
{
  OutputChain *chain;
  GESTrack *track;
  GstPad *sinkpad;
  gboolean reconfigured = FALSE;

  GST_DEBUG_OBJECT (self, "new pad %s:%s , caps:%" GST_PTR_FORMAT,
      GST_DEBUG_PAD_NAME (pad), GST_PAD_CAPS (pad));

  if (G_UNLIKELY (!(track =
              ges_timeline_get_track_for_pad (self->timeline, pad)))) {
    GST_WARNING_OBJECT (self, "Couldn't find coresponding track !");
    return;
  }

  /* Don't connect track if it's not going to be used */
  if (track->type == GES_TRACK_TYPE_VIDEO &&
      !(self->mode & TIMELINE_MODE_PREVIEW_VIDEO) &&
      !(self->mode & TIMELINE_MODE_RENDER) &&
      !(self->mode & TIMELINE_MODE_SMART_RENDER)) {
    GST_DEBUG_OBJECT (self, "Video track... but we don't need it. Not linking");
  }
  if (track->type == GES_TRACK_TYPE_AUDIO &&
      !(self->mode & TIMELINE_MODE_PREVIEW_AUDIO) &&
      !(self->mode & TIMELINE_MODE_RENDER) &&
      !(self->mode & TIMELINE_MODE_SMART_RENDER)) {
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
  gst_pad_link (pad, sinkpad);

  /* Connect playsink */
  if (self->mode & TIMELINE_MODE_PREVIEW) {
    const gchar *sinkpad_name;

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
                gst_element_get_request_pad (self->playsink, sinkpad_name)))) {
      GST_ERROR_OBJECT (self, "Couldn't get a pad from the playsink !");
      goto error;
    }

    if (G_UNLIKELY (gst_pad_link (gst_element_get_request_pad (chain->tee,
                    "src%d"), sinkpad) != GST_PAD_LINK_OK)) {
      GST_ERROR_OBJECT (self, "Couldn't link track pad to playsink");
      goto error;
    }

    GST_DEBUG ("Reconfiguring playsink");

    /* reconfigure playsink */
    g_signal_emit_by_name (self->playsink, "reconfigure", &reconfigured);
    GST_DEBUG ("'reconfigure' returned %d", reconfigured);

    chain->playsinkpad = sinkpad;
  }

  /* Connect to encodebin */
  if (self->mode & (TIMELINE_MODE_RENDER | TIMELINE_MODE_SMART_RENDER)) {
    GST_DEBUG_OBJECT (self, "Connecting to encodebin");

    if (!chain->encodebinpad) {
      /* Check for unused static pads */
      sinkpad = get_compatible_unlinked_pad (self->encodebin, pad);

      if (sinkpad == NULL) {
        /* If no compatible static pad is available, request a pad */
        g_signal_emit_by_name (self->encodebin, "request-pad",
            gst_pad_get_caps (pad), &sinkpad);
        if (G_UNLIKELY (sinkpad == NULL)) {
          GST_ERROR_OBJECT (self, "Couldn't get a pad from encodebin !");
          goto error;
        }
      }
      chain->encodebinpad = sinkpad;
    }

    if (G_UNLIKELY (gst_pad_link (gst_element_get_request_pad (chain->tee,
                    "src%d"), chain->encodebinpad) != GST_PAD_LINK_OK)) {
      GST_WARNING_OBJECT (self, "Couldn't link track pad to playsink");
      goto error;
    }

  }

  /* If chain wasn't already present, insert it in list */
  if (!get_output_chain_for_track (self, track))
    self->chains = g_list_append (self->chains, chain);

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
              ges_timeline_get_track_for_pad (self->timeline, pad)))) {
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
    gst_element_release_request_pad (self->encodebin, chain->encodebinpad);
  }

  /* Unlink playsink */
  if (chain->playsinkpad) {
    peer = gst_pad_get_peer (chain->playsinkpad);
    gst_pad_unlink (peer, chain->playsinkpad);
    gst_element_release_request_pad (self->playsink, chain->playsinkpad);
  }

  /* Unlike/remove tee */
  peer = gst_element_get_static_pad (chain->tee, "sink");
  gst_pad_unlink (pad, peer);
  gst_element_set_state (chain->tee, GST_STATE_NULL);
  gst_bin_remove (GST_BIN (self), chain->tee);

  self->chains = g_list_remove (self->chains, chain);
  g_free (chain);

  GST_DEBUG ("done");
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
  g_return_val_if_fail (pipeline->timeline == NULL, FALSE);
  g_return_val_if_fail (timeline != NULL, FALSE);

  GST_DEBUG ("pipeline:%p, timeline:%p", timeline, pipeline);

  if (G_UNLIKELY (!gst_bin_add (GST_BIN_CAST (pipeline),
              GST_ELEMENT (timeline)))) {
    return FALSE;
  }
  pipeline->timeline = timeline;

  /* Connect to pipeline */
  g_signal_connect (timeline, "pad-added", (GCallback) pad_added_cb, pipeline);
  g_signal_connect (timeline, "pad-removed", (GCallback) pad_removed_cb,
      pipeline);

  return TRUE;
}

/**
 * ges_timeline_pipeline_set_render_settings:
 * @pipeline: a #GESTimelinePipeline
 * @output_uri: the %URI to which the timeline will be rendered
 * @profile: the #GstEncodingProfile to use to render the timeline
 *
 * Specify where the pipeline shall be rendered and with what settings.
 *
 * This method must be called before setting the pipeline mode to
 * #TIMELINE_MODE_RENDER
 *
 * Returns: %TRUE if the settings were aknowledged properly, else %FALSE
 */
gboolean
ges_timeline_pipeline_set_render_settings (GESTimelinePipeline * pipeline,
    gchar * output_uri, GstEncodingProfile * profile)
{
  /* Clear previous URI sink if it existed */
  /* FIXME : We should figure out if it was added to the pipeline,
   * and if so, remove it. */
  if (pipeline->urisink) {
    g_object_unref (pipeline->urisink);
    pipeline->urisink = NULL;
  }

  pipeline->urisink =
      gst_element_make_from_uri (GST_URI_SINK, output_uri, "urisink");
  if (G_UNLIKELY (pipeline->urisink == NULL)) {
    GST_ERROR_OBJECT (pipeline, "Couldn't not create sink for URI %s",
        output_uri);
    return FALSE;
  }

  g_object_set (pipeline->encodebin, "profile", profile, NULL);
  pipeline->profile = profile;

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
  GST_DEBUG_OBJECT (pipeline, "current mode : %d, mode : %d", pipeline->mode,
      mode);

  /* fast-path, nothing to change */
  if (mode == pipeline->mode)
    return TRUE;

  /* FIXME: It would be nice if we are only (de)activating preview
   * modes to not set the whole pipeline to NULL, but instead just
   * do the proper (un)linking to playsink. */

  /* Switch pipeline to NULL since we're changing the configuration */
  gst_element_set_state (GST_ELEMENT_CAST (pipeline), GST_STATE_NULL);

  /* remove no-longer needed components */
  if (pipeline->mode & TIMELINE_MODE_PREVIEW && !(mode & TIMELINE_MODE_PREVIEW)) {
    /* Disable playsink */
    GST_DEBUG ("Disabling playsink");
    g_object_ref (pipeline->playsink);
    gst_bin_remove (GST_BIN_CAST (pipeline), pipeline->playsink);
  }
  if ((pipeline->mode & (TIMELINE_MODE_RENDER | TIMELINE_MODE_SMART_RENDER)) &&
      !(mode & (TIMELINE_MODE_RENDER | TIMELINE_MODE_SMART_RENDER))) {
    /* Disable render bin */
    GST_DEBUG ("Disabling rendering bin");
    g_object_ref (pipeline->encodebin);
    g_object_ref (pipeline->urisink);
    gst_bin_remove_many (GST_BIN_CAST (pipeline),
        pipeline->encodebin, pipeline->urisink, NULL);
  }

  /* Add new elements */
  if (!(pipeline->mode & TIMELINE_MODE_PREVIEW) &&
      (mode & TIMELINE_MODE_PREVIEW)) {
    /* Add playsink */
    GST_DEBUG ("Adding playsink");

    if (!gst_bin_add (GST_BIN_CAST (pipeline), pipeline->playsink)) {
      GST_ERROR_OBJECT (pipeline, "Couldn't add playsink");
      return FALSE;
    }
  }
  if (!(pipeline->mode & (TIMELINE_MODE_RENDER | TIMELINE_MODE_SMART_RENDER)) &&
      (mode & (TIMELINE_MODE_RENDER | TIMELINE_MODE_SMART_RENDER))) {
    /* Adding render bin */
    GST_DEBUG ("Adding render bin");

    if (G_UNLIKELY (pipeline->urisink == NULL)) {
      GST_ERROR_OBJECT (pipeline, "Output URI not set !");
      return FALSE;
    }
    if (!gst_bin_add (GST_BIN_CAST (pipeline), pipeline->encodebin)) {
      GST_ERROR_OBJECT (pipeline, "Couldn't add encodebin");
      return FALSE;
    }
    if (!gst_bin_add (GST_BIN_CAST (pipeline), pipeline->urisink)) {
      GST_ERROR_OBJECT (pipeline, "Couldn't add URI sink");
      return FALSE;
    }
    gst_element_link (pipeline->encodebin, pipeline->urisink);
  }

  /* FIXUPS */
  /* FIXME
   * If we are rendering, set playsink to sync=False,
   * If we are NOT rendering, set playsink to sync=TRUE */

  pipeline->mode = mode;

  return TRUE;
}
