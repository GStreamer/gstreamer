/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2004 Wim Taymans <wim@fluendo.com>
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
    "Erik Walthinsen <omega@cse.ogi.edu>" "Wim Taymans <wim@fluendo.com>");

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

static GstBusSyncReply pipeline_bus_handler (GstBus * bus, GstMessage * message,
    GstPipeline * pipeline);

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

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_pipeline_dispose);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_pipeline_change_state);
  gstelement_class->get_clock = GST_DEBUG_FUNCPTR (gst_pipeline_get_clock_func);
}

static void
gst_pipeline_init (GTypeInstance * instance, gpointer g_class)
{
  GstPipeline *pipeline = GST_PIPELINE (instance);

  /* get an instance of the default scheduler */
  pipeline->scheduler =
      gst_scheduler_factory_make (NULL, GST_ELEMENT (pipeline));

  /* FIXME need better error handling */
  if (pipeline->scheduler == NULL) {
    const gchar *name = gst_scheduler_factory_get_default_name ();

    g_error ("Critical error: could not get scheduler \"%s\"\n"
        "Are you sure you have a registry ?\n"
        "Run gst-register as root if you haven't done so yet.", name);
  }
  pipeline->bus = g_object_new (gst_bus_get_type (), NULL);
  gst_bus_set_sync_handler (pipeline->bus,
      (GstBusSyncHandler) pipeline_bus_handler, pipeline);
  pipeline->eosed = NULL;
  GST_ELEMENT_MANAGER (pipeline) = pipeline;
}

static void
gst_pipeline_dispose (GObject * object)
{
  GstPipeline *pipeline = GST_PIPELINE (object);

  g_assert (GST_IS_SCHEDULER (pipeline->scheduler));

  gst_scheduler_reset (pipeline->scheduler);
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static gboolean
is_eos (GstPipeline * pipeline)
{
  GstIterator *sinks;
  gboolean result = TRUE;
  gboolean done = FALSE;

  sinks = gst_bin_iterate_sinks (GST_BIN (pipeline));
  while (!done) {
    gpointer data;

    switch (gst_iterator_next (sinks, &data)) {
      case GST_ITERATOR_OK:
      {
        GstElement *element = GST_ELEMENT (data);
        GList *eosed;
        GstElementState state, pending;
        gboolean complete;

        complete = gst_element_get_state (element, &state, &pending, NULL);

        if (!complete) {
          GST_DEBUG ("element %s still performing state change",
              gst_element_get_name (element));
          result = FALSE;
          done = TRUE;
          break;
        } else if (state != GST_STATE_PLAYING) {
          GST_DEBUG ("element %s not playing %d %d",
              gst_element_get_name (element), GST_STATE (element),
              GST_STATE_PENDING (element));
          break;
        }
        eosed = g_list_find (pipeline->eosed, element);
        if (!eosed) {
          result = FALSE;
          done = TRUE;
        }
        gst_object_unref (GST_OBJECT (element));
        break;
      }
      case GST_ITERATOR_RESYNC:
        result = TRUE;
        gst_iterator_resync (sinks);
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
      default:
        g_assert_not_reached ();
        break;
    }
  }
  return result;
}

static GstBusSyncReply
pipeline_bus_handler (GstBus * bus, GstMessage * message,
    GstPipeline * pipeline)
{
  GstBusSyncReply result = GST_BUS_PASS;
  gboolean posteos = FALSE;
  gboolean locked;

  /* we don't want messages from the streaming thread while we're doing the 
   * state change. We do want them from the state change functions. */
  locked = GST_STATE_TRYLOCK (pipeline);

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_EOS:
      if (GST_MESSAGE_SRC (message) != GST_OBJECT (pipeline)) {
        pipeline->eosed =
            g_list_prepend (pipeline->eosed, GST_MESSAGE_SRC (message));
        if (is_eos (pipeline)) {
          posteos = TRUE;
        }
        /* we drop all EOS messages */
        result = GST_BUS_DROP;
      }
    case GST_MESSAGE_ERROR:
      break;
    default:
      break;
  }
  if (locked)
    GST_STATE_UNLOCK (pipeline);

  if (posteos) {
    gst_bus_post (bus, gst_message_new_eos (GST_OBJECT (pipeline)));
  }

  return result;
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
  GstElementStateReturn result = GST_STATE_SUCCESS;
  GstPipeline *pipeline = GST_PIPELINE (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      gst_scheduler_setup (pipeline->scheduler);
      break;
    case GST_STATE_READY_TO_PAUSED:
      gst_element_set_clock (element, gst_element_get_clock (element));
      pipeline->eosed = NULL;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      if (element->clock) {
        element->base_time = gst_clock_get_time (element->clock);
      }
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
    case GST_STATE_PAUSED_TO_READY:
    case GST_STATE_READY_TO_NULL:
      break;
  }

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element);

  /* we wait for async state changes ourselves */
  if (result == GST_STATE_ASYNC) {
    GST_STATE_UNLOCK (pipeline);
    gst_element_get_state (element, NULL, NULL, NULL);
    GST_STATE_LOCK (pipeline);
    result = GST_STATE_SUCCESS;
  }

  return result;
}

/**
 * gst_pipeline_get_scheduler:
 * @pipeline: the pipeline
 *
 * Gets the #GstScheduler of this pipeline.
 *
 * Returns: a GstScheduler.
 */
GstScheduler *
gst_pipeline_get_scheduler (GstPipeline * pipeline)
{
  return pipeline->scheduler;
}

/**
 * gst_pipeline_get_bus:
 * @pipeline: the pipeline
 *
 * Gets the #GstBus of this pipeline.
 *
 * Returns: a GstBus
 */
GstBus *
gst_pipeline_get_bus (GstPipeline * pipeline)
{
  return pipeline->bus;
}

static GstClock *
gst_pipeline_get_clock_func (GstElement * element)
{
  GstClock *clock = NULL;
  GstPipeline *pipeline = GST_PIPELINE (element);

  /* if we have a fixed clock, use that one */
  if (GST_FLAG_IS_SET (pipeline, GST_PIPELINE_FLAG_FIXED_CLOCK)) {
    clock = pipeline->fixed_clock;

    GST_CAT_DEBUG (GST_CAT_CLOCK, "pipeline using fixed clock %p (%s)",
        clock, clock ? GST_STR_NULL (GST_OBJECT_NAME (clock)) : "-");
  } else {
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
 */
void
gst_pipeline_use_clock (GstPipeline * pipeline, GstClock * clock)
{
  g_return_if_fail (GST_IS_PIPELINE (pipeline));

  GST_FLAG_SET (pipeline, GST_PIPELINE_FLAG_FIXED_CLOCK);

  gst_object_replace ((GstObject **) & pipeline->fixed_clock,
      (GstObject *) clock);

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
 */
void
gst_pipeline_auto_clock (GstPipeline * pipeline)
{
  g_return_if_fail (pipeline != NULL);
  g_return_if_fail (GST_IS_PIPELINE (pipeline));

  GST_FLAG_UNSET (pipeline, GST_PIPELINE_FLAG_FIXED_CLOCK);

  gst_object_replace ((GstObject **) & pipeline->fixed_clock, NULL);

  GST_CAT_DEBUG (GST_CAT_CLOCK, "pipeline using automatic clock");
}

/**
 * gst_pipeline_post_message:
 * @pipeline: the pipeline
 * @message: the message
 *
 * Post a message on the message bus of this pipeline.
 *
 * Returns: TRUE if the message could be posted.
 */
gboolean
gst_pipeline_post_message (GstPipeline * pipeline, GstMessage * message)
{
  return gst_bus_post (pipeline->bus, message);
}
