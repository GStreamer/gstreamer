/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2004,2005 Wim Taymans <wim@fluendo.com>
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
#include "gstsystemclock.h"

static GstElementDetails gst_pipeline_details =
GST_ELEMENT_DETAILS ("Pipeline object",
    "Generic/Bin",
    "Complete pipeline object",
    "Erik Walthinsen <omega@cse.ogi.edu>, Wim Taymans <wim@fluendo.com>");

/* Pipeline signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_DELAY 0
#define DEFAULT_PLAY_TIMEOUT  (2*GST_SECOND)
enum
{
  ARG_0,
  ARG_DELAY,
  ARG_PLAY_TIMEOUT,
  /* FILL ME */
};


static void gst_pipeline_base_init (gpointer g_class);
static void gst_pipeline_class_init (gpointer g_class, gpointer class_data);
static void gst_pipeline_init (GTypeInstance * instance, gpointer g_class);

static void gst_pipeline_dispose (GObject * object);
static void gst_pipeline_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_pipeline_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstClock *gst_pipeline_get_clock_func (GstElement * element);
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

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_pipeline_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_pipeline_get_property);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DELAY,
      g_param_spec_uint64 ("delay", "Delay",
          "Expected delay needed for elements "
          "to spin up to PLAYING in nanoseconds", 0, G_MAXUINT64, DEFAULT_DELAY,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PLAY_TIMEOUT,
      g_param_spec_uint64 ("play-timeout", "Play Timeout",
          "Max timeout for going " "to PLAYING in nanoseconds", 0, G_MAXUINT64,
          DEFAULT_PLAY_TIMEOUT, G_PARAM_READWRITE));

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_pipeline_dispose);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_pipeline_change_state);
  gstelement_class->get_clock = GST_DEBUG_FUNCPTR (gst_pipeline_get_clock_func);
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

  gst_scheduler_reset (GST_ELEMENT_SCHEDULER (object));
  gst_object_replace ((GstObject **) & pipeline->fixed_clock, NULL);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_pipeline_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPipeline *pipeline = GST_PIPELINE (object);

  GST_LOCK (pipeline);
  switch (prop_id) {
    case ARG_DELAY:
      pipeline->delay = g_value_get_uint64 (value);
      break;
    case ARG_PLAY_TIMEOUT:
      pipeline->play_timeout = g_value_get_uint64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_UNLOCK (pipeline);
}

static void
gst_pipeline_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstPipeline *pipeline = GST_PIPELINE (object);

  GST_LOCK (pipeline);
  switch (prop_id) {
    case ARG_DELAY:
      g_value_set_uint64 (value, pipeline->delay);
      break;
    case ARG_PLAY_TIMEOUT:
      g_value_set_uint64 (value, pipeline->play_timeout);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_UNLOCK (pipeline);
}

/**
 * gst_pipeline_new:
 * @name: name of new pipeline
 *
 * Create a new pipeline with the given name.
 *
 * Returns: newly created GstPipeline
 *
 * MT safe.
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
      gst_scheduler_setup (GST_ELEMENT_SCHEDULER (element));
      break;
    case GST_STATE_READY_TO_PAUSED:
    case GST_STATE_PAUSED_TO_PLAYING:
    case GST_STATE_PLAYING_TO_PAUSED:
    case GST_STATE_PAUSED_TO_READY:
    case GST_STATE_READY_TO_NULL:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static GstClock *
gst_pipeline_get_clock_func (GstElement * element)
{
  GstClock *clock = NULL;
  GstPipeline *pipeline = GST_PIPELINE (element);

  /* if we have a fixed clock, use that one */
  GST_LOCK (pipeline);
  if (GST_FLAG_IS_SET (pipeline, GST_PIPELINE_FLAG_FIXED_CLOCK)) {
    clock = pipeline->fixed_clock;
    gst_object_ref (GST_OBJECT (clock));
    GST_UNLOCK (pipeline);

    GST_CAT_DEBUG (GST_CAT_CLOCK, "pipeline using fixed clock %p (%s)",
        clock, clock ? GST_STR_NULL (GST_OBJECT_NAME (clock)) : "-");
  } else {
    GST_UNLOCK (pipeline);
    clock =
        GST_ELEMENT_CLASS (parent_class)->get_clock (GST_ELEMENT (pipeline));
    /* no clock, use a system clock */
    if (!clock) {
      clock = gst_system_clock_obtain ();
      /* we unref since this function is not supposed to increase refcount
       * of clock object returned; this is ok since the systemclock always
       * has a refcount of at least one in the current code. */
      gst_object_unref (GST_OBJECT (clock));
      GST_CAT_DEBUG (GST_CAT_CLOCK, "pipeline obtained system clock: %p (%s)",
          clock, clock ? GST_STR_NULL (GST_OBJECT_NAME (clock)) : "-");
    } else {
      GST_CAT_DEBUG (GST_CAT_CLOCK, "pipeline obtained clock: %p (%s)",
          clock, clock ? GST_STR_NULL (GST_OBJECT_NAME (clock)) : "-");
    }
  }
  return clock;
}

/**
 * gst_pipeline_get_clock:
 * @pipeline: the pipeline
 *
 * Gets the current clock used by the pipeline.
 *
 * Returns: a GstClock
 */
GstClock *
gst_pipeline_get_clock (GstPipeline * pipeline)
{
  g_return_val_if_fail (GST_IS_PIPELINE (pipeline), NULL);

  return gst_pipeline_get_clock_func (GST_ELEMENT (pipeline));
}


/**
 * gst_pipeline_use_clock:
 * @pipeline: the pipeline
 * @clock: the clock to use
 *
 * Force the pipeline to use the given clock. The pipeline will
 * always use the given clock even if new clock providers are added
 * to this pipeline.
 *
 * MT safe.
 */
void
gst_pipeline_use_clock (GstPipeline * pipeline, GstClock * clock)
{
  g_return_if_fail (GST_IS_PIPELINE (pipeline));

  GST_LOCK (pipeline);
  GST_FLAG_SET (pipeline, GST_PIPELINE_FLAG_FIXED_CLOCK);

  gst_object_replace ((GstObject **) & pipeline->fixed_clock,
      (GstObject *) clock);
  GST_LOCK (pipeline);

  GST_CAT_DEBUG (GST_CAT_CLOCK, "pipeline using fixed clock %p (%s)", clock,
      (clock ? GST_OBJECT_NAME (clock) : "nil"));
}

/**
 * gst_pipeline_set_clock:
 * @pipeline: the pipeline
 * @clock: the clock to set
 *
 * Set the clock for the pipeline. The clock will be distributed
 * to all the elements managed by the pipeline.
 *
 * MT safe.
 */
void
gst_pipeline_set_clock (GstPipeline * pipeline, GstClock * clock)
{
  g_return_if_fail (pipeline != NULL);
  g_return_if_fail (GST_IS_PIPELINE (pipeline));

  GST_ELEMENT_CLASS (parent_class)->set_clock (GST_ELEMENT (pipeline), clock);
}

/**
 * gst_pipeline_auto_clock:
 * @pipeline: the pipeline
 *
 * Let the pipeline select a clock automatically.
 *
 * MT safe.
 */
void
gst_pipeline_auto_clock (GstPipeline * pipeline)
{
  g_return_if_fail (pipeline != NULL);
  g_return_if_fail (GST_IS_PIPELINE (pipeline));

  GST_LOCK (pipeline);
  GST_FLAG_UNSET (pipeline, GST_PIPELINE_FLAG_FIXED_CLOCK);

  gst_object_replace ((GstObject **) & pipeline->fixed_clock, NULL);
  GST_UNLOCK (pipeline);

  GST_CAT_DEBUG (GST_CAT_CLOCK, "pipeline using automatic clock");
}
