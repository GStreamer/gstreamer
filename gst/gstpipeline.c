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
#include "gstinfo.h"
#include "gstscheduler.h"

static GstElementDetails gst_pipeline_details =
GST_ELEMENT_DETAILS ("Pipeline object",
    "Generic/Bin",
    "Complete pipeline object",
    "Erik Walthinsen <omega@cse.ogi.edu>");

/* Pipeline signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
      /* FILL ME */
};


static void gst_pipeline_base_init (gpointer g_class);
static void gst_pipeline_class_init (gpointer g_class, gpointer class_data);
static void gst_pipeline_init (GTypeInstance * instance, gpointer g_class);

static void gst_pipeline_dispose (GObject * object);

static GstElementStateReturn gst_pipeline_change_state (GstElement * element);

static GstBinClass *parent_class = NULL;

/* static guint gst_pipeline_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_pipeline_get_type (void)
{
  static GType pipeline_type = 0;

  if (!pipeline_type) {
    static const GTypeInfo pipeline_info = {
      sizeof (GstPipelineClass),
      gst_pipeline_base_init,
      NULL,
      (GClassInitFunc) gst_pipeline_class_init,
      NULL,
      NULL,
      sizeof (GstPipeline),
      0,
      gst_pipeline_init,
      NULL
    };

    pipeline_type =
        g_type_register_static (GST_TYPE_BIN, "GstPipeline", &pipeline_info, 0);
  }
  return pipeline_type;
}

static void
gst_pipeline_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (gstelement_class, &gst_pipeline_details);
}

static void
gst_pipeline_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (g_class);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);
  GstPipelineClass *klass = GST_PIPELINE_CLASS (g_class);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_pipeline_dispose);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_pipeline_change_state);
}

static void
gst_pipeline_init (GTypeInstance * instance, gpointer g_class)
{
  GstScheduler *scheduler;
  GstPipeline *pipeline = GST_PIPELINE (instance);

  /* pipelines are managing bins */
  GST_FLAG_SET (pipeline, GST_BIN_FLAG_MANAGER);

  /* get an instance of the default scheduler */
  scheduler = gst_scheduler_factory_make (NULL, GST_ELEMENT (pipeline));

  /* FIXME need better error handling */
  if (scheduler == NULL) {
    const gchar *name = gst_scheduler_factory_get_default_name ();

    g_error ("Critical error: could not get scheduler \"%s\"\n"
        "Are you sure you have a registry ?\n"
        "Run gst-register as root if you haven't done so yet.", name);
  }
}

static void
gst_pipeline_dispose (GObject * object)
{
  GstPipeline *pipeline = GST_PIPELINE (object);

  G_OBJECT_CLASS (parent_class)->dispose (object);

  gst_object_replace ((GstObject **) & GST_ELEMENT_SCHED (pipeline), NULL);
}

/**
 * gst_pipeline_new:
 * @name: name of new pipeline
 *
 * Create a new pipeline with the given name.
 *
 * Returns: newly created GstPipeline
 */
GstElement *
gst_pipeline_new (const gchar * name)
{
  return gst_element_factory_make ("pipeline", name);
}

static GstElementStateReturn
gst_pipeline_change_state (GstElement * element)
{
  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      gst_scheduler_setup (GST_ELEMENT_SCHED (element));
      break;
    case GST_STATE_READY_TO_PAUSED:
    case GST_STATE_PAUSED_TO_PLAYING:
    case GST_STATE_PLAYING_TO_PAUSED:
    case GST_STATE_PAUSED_TO_READY:
      break;
    case GST_STATE_READY_TO_NULL:
      /* FIXME: calling gst_scheduler_reset() here is bad, since we
       * might not be in cothread 0 */
#if 0
      if (GST_ELEMENT_SCHED (element)) {
        gst_scheduler_reset (GST_ELEMENT_SCHED (element));
      }
#endif
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}
