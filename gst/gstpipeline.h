/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#ifndef __GST_PIPELINE_H__
#define __GST_PIPELINE_H__


#include <gst/gstbin.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

GstElementDetails gst_pipeline_details;


#define GST_TYPE_PIPELINE \
  (gst_pipeline_get_type())
#define GST_PIPELINE(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_PIPELINE,GstPipeline))
#define GST_PIPELINE_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_PIPELINE,GstPipelineClass))
#define GST_IS_PIPELINE(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_PIPELINE))
#define GST_IS_PIPELINE_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_PIPELINE)))

typedef struct _GstPipeline GstPipeline;
typedef struct _GstPipelineClass GstPipelineClass;

struct _GstPipeline {
  GstBin bin;
};

struct _GstPipelineClass {
  GstBinClass parent_class;
};

GtkType gst_pipeline_get_type(void);
GstPipeline *gst_pipeline_new(guchar *name);
#define gst_pipeline_destroy(pipeline) gst_object_destroy(GST_OBJECT(pipeline))

void gst_pipeline_iterate(GstPipeline *pipeline);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_PIPELINE_H__ */     

