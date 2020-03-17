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

#pragma once

#include <glib-object.h>
#include <ges/ges.h>
#include <gst/pbutils/encoding-profile.h>

G_BEGIN_DECLS

#define GES_TYPE_PIPELINE ges_pipeline_get_type()
GES_DECLARE_TYPE(Pipeline, pipeline, PIPELINE);

/**
 * GESPipeline:
 *
 */

struct _GESPipeline {
  /*< private >*/
  GstPipeline parent;

  GESPipelinePrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

/**
 * GESPipelineClass:
 * @parent_class: parent class
 *
 */

struct _GESPipelineClass {
  /*< private >*/
  GstPipelineClass parent_class;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GES_API
GESPipeline* ges_pipeline_new (void);

GES_API
gboolean ges_pipeline_set_timeline (GESPipeline * pipeline,
					     GESTimeline * timeline);

GES_API
gboolean ges_pipeline_set_render_settings (GESPipeline *pipeline,
						    const gchar * output_uri,
						    GstEncodingProfile *profile);
GES_API
gboolean ges_pipeline_set_mode (GESPipeline *pipeline,
					 GESPipelineFlags mode);

GES_API
GESPipelineFlags ges_pipeline_get_mode (GESPipeline *pipeline);

GES_API GstSample *
ges_pipeline_get_thumbnail(GESPipeline *self, GstCaps *caps);

GES_API GstSample *
ges_pipeline_get_thumbnail_rgb24(GESPipeline *self,
    gint width, gint height);

GES_API gboolean
ges_pipeline_save_thumbnail(GESPipeline *self,
    int width, int height, const gchar *format, const gchar *location,
    GError **error);

GES_API GstElement *
ges_pipeline_preview_get_video_sink (GESPipeline * self);

GES_API void
ges_pipeline_preview_set_video_sink (GESPipeline * self,
    GstElement * sink);

GES_API GstElement *
ges_pipeline_preview_get_audio_sink (GESPipeline * self);

GES_API void
ges_pipeline_preview_set_audio_sink (GESPipeline * self,
    GstElement * sink);

G_END_DECLS
