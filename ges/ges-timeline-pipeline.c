/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <bilboed@bilboed.com>
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

#include "ges-internal.h"
#include "ges-timeline-pipeline.h"

/* TimelinePipeline
 *
 */

G_DEFINE_TYPE (GESTimelinePipeline, ges_timeline_pipeline, GST_TYPE_PIPELINE);

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
  G_OBJECT_CLASS (ges_timeline_pipeline_parent_class)->finalize (object);
}

static void
ges_timeline_pipeline_class_init (GESTimelinePipelineClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = ges_timeline_pipeline_get_property;
  object_class->set_property = ges_timeline_pipeline_set_property;
  object_class->dispose = ges_timeline_pipeline_dispose;
  object_class->finalize = ges_timeline_pipeline_finalize;
}

static void
ges_timeline_pipeline_init (GESTimelinePipeline * self)
{
}

GESTimelinePipeline *
ges_timeline_pipeline_new (void)
{
  return g_object_new (GEST_TYPE_TIMELINE_PIPELINE, NULL);
}
