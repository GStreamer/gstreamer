/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstpipeline.c: Overall pipeline management element
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

#include "gst_private.h"

#include "gstpipeline.h"
#include "gstscheduler.h"

GstElementDetails gst_pipeline_details = {
  "Pipeline object",
  "Generic/Bin",
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


static void			gst_pipeline_class_init		(GstPipelineClass *klass);
static void			gst_pipeline_init		(GstPipeline *pipeline);

static void                     gst_pipeline_dispose         	(GObject *object);

static GstElementStateReturn	gst_pipeline_change_state	(GstElement *element);

static GstBinClass *parent_class = NULL;
/* static guint gst_pipeline_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_pipeline_get_type (void) {
  static GType pipeline_type = 0;

  if (!pipeline_type) {
    static const GTypeInfo pipeline_info = {
      sizeof(GstPipelineClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_pipeline_class_init,
      NULL,
      NULL,
      sizeof(GstPipeline),
      0,
      (GInstanceInitFunc)gst_pipeline_init,
      NULL
    };
    pipeline_type = g_type_register_static (GST_TYPE_BIN, "GstPipeline", &pipeline_info, 0);
  }
  return pipeline_type;
}

static void
gst_pipeline_class_init (GstPipelineClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (gst_bin_get_type ());

  gobject_class->dispose 		= GST_DEBUG_FUNCPTR (gst_pipeline_dispose);

  gstelement_class->change_state 	= GST_DEBUG_FUNCPTR (gst_pipeline_change_state);
}

static void
gst_pipeline_init (GstPipeline *pipeline)
{
  GstScheduler *scheduler;

  /* pipelines are managing bins */
  GST_FLAG_SET (pipeline, GST_BIN_FLAG_MANAGER);

  /* get an instance of the default scheduler */
  scheduler = gst_scheduler_factory_make (NULL, GST_ELEMENT (pipeline));
	  
  gst_scheduler_setup (scheduler);
}

static void
gst_pipeline_dispose (GObject *object)
{
  GstPipeline *pipeline = GST_PIPELINE (object);

  G_OBJECT_CLASS (parent_class)->dispose (object);

  if (GST_ELEMENT_SCHED (pipeline)) {
    gst_scheduler_reset (GST_ELEMENT_SCHED (pipeline));
    gst_object_unref (GST_OBJECT (GST_ELEMENT_SCHED (pipeline)));
    GST_ELEMENT_SCHED (pipeline) = NULL;
  }
}

/**
 * gst_pipeline_new:
 * @name: name of new pipeline
 *
 * Create a new pipeline with the given name.
 *
 * Returns: newly created GstPipeline
 */
GstElement*
gst_pipeline_new (const gchar *name) 
{
  return gst_element_factory_make ("pipeline", name);
}

static GstElementStateReturn
gst_pipeline_change_state (GstElement *element)
{
  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

