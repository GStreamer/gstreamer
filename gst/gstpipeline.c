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

#include <gst/gstpipeline.h>

GstElementDetails gst_pipeline_details = {
  "Pipeline object",
  "Bin",
  "Complete pipeline object",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};


/* Pipeline signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};


static void gst_pipeline_class_init(GstPipelineClass *klass);
static void gst_pipeline_init(GstPipeline *pipeline);

static GstElementStateReturn gst_pipeline_change_state(GstElement *element);

static void gst_pipeline_prepare(GstPipeline *pipeline);


static GstBin *parent_class = NULL;
//static guint gst_pipeline_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_pipeline_get_type(void) {
  static GtkType pipeline_type = 0;

  if (!pipeline_type) {
    static const GtkTypeInfo pipeline_info = {
      "GstPipeline",
      sizeof(GstPipeline),
      sizeof(GstPipelineClass),
      (GtkClassInitFunc)gst_pipeline_class_init,
      (GtkObjectInitFunc)gst_pipeline_init,
      (GtkArgSetFunc)NULL,
      (GtkArgGetFunc)NULL,
      (GtkClassInitFunc)NULL,
    };
    pipeline_type = gtk_type_unique(gst_bin_get_type(),&pipeline_info);
  }
  return pipeline_type;
}

static void
gst_pipeline_class_init(GstPipelineClass *klass) {
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass*)klass;

  parent_class = gtk_type_class(gst_bin_get_type());

  gstelement_class->change_state = gst_pipeline_change_state;
}

static void gst_pipeline_init(GstPipeline *pipeline) {
}


/**
 * gst_pipeline_new:
 * @name: name of new pipeline
 *
 * Create a new pipeline with the given name.
 *
 * Returns: newly created GstPipeline
 */
GstPipeline *gst_pipeline_new(guchar *name) {
  GstPipeline *pipeline;

  pipeline = gtk_type_new(gst_pipeline_get_type());
  gst_element_set_name(GST_ELEMENT(pipeline),name);
  return pipeline;
}

static void gst_pipeline_prepare(GstPipeline *pipeline) {
  g_print("GstPipeline: preparing pipeline \"%s\" for playing\n", gst_element_get_name(GST_ELEMENT(pipeline)));
}


static GstElementStateReturn gst_pipeline_change_state(GstElement *element) {
  GstPipeline *pipeline;

  g_return_val_if_fail(GST_IS_PIPELINE(element), FALSE);
  pipeline = GST_PIPELINE(element);


  switch (GST_STATE_PENDING(pipeline)) {
    case GST_STATE_READY:
      // we need to set up internal state
      g_print("preparing pipeline \"%s\" for iterations:\n",
              gst_element_get_name(GST_ELEMENT(element)));
      gst_pipeline_prepare(pipeline);
      break;
    default:
      break;
  }
    
  if (GST_ELEMENT_CLASS(parent_class)->change_state)
    return GST_ELEMENT_CLASS(parent_class)->change_state(element);
  return GST_STATE_SUCCESS;
}


/**
 * gst_pipeline_iterate:
 * @pipeline: GstPipeline to iterate
 *
 * Cause the pipeline's contents to be run through one full 'iteration'.
 */
void gst_pipeline_iterate(GstPipeline *pipeline) {
  g_return_if_fail(pipeline != NULL);
  g_return_if_fail(GST_IS_PIPELINE(pipeline));
}
