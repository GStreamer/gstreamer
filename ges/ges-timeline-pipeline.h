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

#ifndef _GES_TIMELINE_PIPELINE
#define _GES_TIMELINE_PIPELINE

#include <glib-object.h>
#include <ges/ges.h>

G_BEGIN_DECLS

#define GES_TYPE_TIMELINE_PIPELINE ges_timeline_pipeline_get_type()

#define GES_TIMELINE_PIPELINE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_TIMELINE_PIPELINE, GESTimelinePipeline))

#define GES_TIMELINE_PIPELINE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_TIMELINE_PIPELINE, GESTimelinePipelineClass))

#define GES_IS_TIMELINE_PIPELINE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_TIMELINE_PIPELINE))

#define GES_IS_TIMELINE_PIPELINE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_TIMELINE_PIPELINE))

#define GES_TIMELINE_PIPELINE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_TIMELINE_PIPELINE, GESTimelinePipelineClass))

struct _GESTimelinePipeline {
  GstPipeline parent;
};

struct _GESTimelinePipelineClass {
  GstPipelineClass parent_class;
};

GType ges_timeline_pipeline_get_type (void);

GESTimelinePipeline* ges_timeline_pipeline_new (void);

G_END_DECLS

#endif /* _GES_TIMELINE_PIPELINE */

