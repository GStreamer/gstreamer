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

/* Structure corresponding to a timeline - sink link */
/* TODO : Don't forget we want to render also :) */

typedef struct
{
  GESTimelinePipeline *pipeline;
  GstPad *srcpad;
  GstPad *sinkpad;
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

  if (self->sink) {
    gst_bin_remove (GST_BIN (object), self->sink);
    gst_object_unref (self->sink);
    self->sink = NULL;
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

  self->sink = gst_element_factory_make ("playsink", "internal-sinks");

  if (G_UNLIKELY (self->sink == NULL))
    GST_ERROR_OBJECT (self, "Can't create playsink instance !");
  else {
    GST_INFO_OBJECT (self, "Adding playsink to self");

    if (G_UNLIKELY (!gst_bin_add (GST_BIN (self), self->sink))) {
      GST_ERROR_OBJECT (self, "Can't add playsink to ourselves !");
    }
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
              gst_element_get_request_pad (self->sink, sinkpad_name)))) {
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
  g_signal_emit_by_name (self->sink, "reconfigure", &reconfigured);
  GST_DEBUG ("'reconfigure' returned %d", reconfigured);

  /* Create a new chain */
  chain = g_new0 (OutputChain, 1);
  chain->pipeline = self;
  chain->srcpad = pad;
  chain->sinkpad = sinkpad;

  self->chains = g_list_append (self->chains, chain);

  GST_DEBUG ("done");
}

static void
pad_removed_cb (GstElement * timeline, GstPad * pad, GESTimelinePipeline * self)
{
  GST_DEBUG ("pad %s:%s", GST_DEBUG_PAD_NAME (pad));

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
