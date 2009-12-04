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
  GESTimelinePipeline *pipeline;
  GstElement *queue;
  GstPad *srcpad;
  GstPad *playsinkpad;
  GstPad *encodebinpad;
} OutputChain;

G_DEFINE_TYPE (GESTimelinePipeline, ges_timeline_pipeline, GST_TYPE_PIPELINE);

static GstStateChangeReturn ges_timeline_pipeline_change_state (GstElement *
    element, GstStateChange transition);
static void
ges_timeline_pipeline_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_pipeline_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_pipeline_dispose (GObject * object)
{
  G_OBJECT_CLASS (ges_timeline_pipeline_parent_class)->dispose (object);
}

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

  object_class->get_property = ges_timeline_pipeline_get_property;
  object_class->set_property = ges_timeline_pipeline_set_property;
  object_class->dispose = ges_timeline_pipeline_dispose;
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

static GstStateChangeReturn
ges_timeline_pipeline_change_state (GstElement * element,
    GstStateChange transition)
{
  GESTimelinePipeline *self;
  GstStateChangeReturn ret;

  self = GES_TIMELINE_PIPELINE (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (G_UNLIKELY (self->timeline == NULL)) {
        GST_ERROR_OBJECT (element,
            "No GESTimeline set on the pipeline, cannot play !");
        ret = GST_STATE_CHANGE_FAILURE;
        goto done;
      }
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

static void
pad_added_cb (GstElement * timeline, GstPad * pad, GESTimelinePipeline * self)
{
  OutputChain *chain;
  GESTrack *track;
  const gchar *sinkpad_name;
  GstPad *sinkpad;
  gboolean reconfigured = FALSE;

  GST_DEBUG_OBJECT (self, "new pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  /* FIXME : Adapt for usage of render sink */

  if (G_UNLIKELY (!(track =
              ges_timeline_get_track_for_pad (self->timeline, pad)))) {
    GST_WARNING_OBJECT (self, "Couldn't find coresponding track !");
    return;
  }

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
      return;
  }

  /* Request a sinkpad from playsink */
  if (G_UNLIKELY (!(sinkpad =
              gst_element_get_request_pad (self->playsink, sinkpad_name)))) {
    GST_WARNING_OBJECT (self, "Couldn't get a pad from the playsink !");
    return;
  }

  if (G_UNLIKELY (gst_pad_link (pad, sinkpad) != GST_PAD_LINK_OK)) {
    GST_WARNING_OBJECT (self, "Couldn't link track pad to playsink");
    gst_object_unref (sinkpad);
    return;
  }

  GST_DEBUG ("Reconfiguring playsink");

  /* reconfigure playsink */
  g_signal_emit_by_name (self->playsink, "reconfigure", &reconfigured);
  GST_DEBUG ("'reconfigure' returned %d", reconfigured);

  /* Create a new chain */
  chain = g_new0 (OutputChain, 1);
  chain->pipeline = self;
  chain->srcpad = pad;
  chain->playsinkpad = sinkpad;

  self->chains = g_list_append (self->chains, chain);

  GST_DEBUG ("done");
}

static void
pad_removed_cb (GstElement * timeline, GstPad * pad, GESTimelinePipeline * self)
{
  GST_DEBUG_OBJECT (self, "pad removed %s:%s", GST_DEBUG_PAD_NAME (pad));

  /* FIXME : IMPLEMENT ! */

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

  if (G_UNLIKELY (!gst_bin_add (GST_BIN (pipeline), GST_ELEMENT (timeline)))) {
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
  if ((pipeline->mode & TIMELINE_MODE_RENDER) && !(mode & TIMELINE_MODE_RENDER)) {
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
  if (!(pipeline->mode & TIMELINE_MODE_RENDER) && (mode & TIMELINE_MODE_RENDER)) {
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

  return TRUE;
}
