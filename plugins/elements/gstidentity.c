/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *
 * gstidentity.c: 
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


#include <stdlib.h>

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "../gst-i18n-lib.h"
#include "gstidentity.h"
#include <gst/gstmarshal.h>

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_identity_debug);
#define GST_CAT_DEFAULT gst_identity_debug

GstElementDetails gst_identity_details = GST_ELEMENT_DETAILS ("Identity",
    "Generic",
    "Pass data without modification",
    "Erik Walthinsen <omega@cse.ogi.edu>");


/* Identity signals and args */
enum
{
  SIGNAL_HANDOFF,
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_LOOP_BASED		FALSE
#define DEFAULT_SLEEP_TIME		0
#define DEFAULT_DUPLICATE		1
#define DEFAULT_ERROR_AFTER		-1
#define DEFAULT_DROP_PROBABILITY	0.0
#define DEFAULT_DATARATE		0
#define DEFAULT_SILENT			FALSE
#define DEFAULT_DUMP			FALSE
#define DEFAULT_SYNC			FALSE
#define DEFAULT_CHECK_PERFECT		FALSE

enum
{
  PROP_0,
  PROP_HAS_GETRANGE,
  PROP_HAS_CHAIN,
  PROP_HAS_SINK_LOOP,
  PROP_HAS_SRC_LOOP,
  PROP_LOOP_BASED,
  PROP_SLEEP_TIME,
  PROP_DUPLICATE,
  PROP_ERROR_AFTER,
  PROP_DROP_PROBABILITY,
  PROP_DATARATE,
  PROP_SILENT,
  PROP_LAST_MESSAGE,
  PROP_DUMP,
  PROP_SYNC,
  PROP_CHECK_PERFECT
};


typedef GstFlowReturn (*IdentityPushFunc) (GstIdentity *, GstBuffer *);


#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_identity_debug, "identity", 0, "identity element");

GST_BOILERPLATE_FULL (GstIdentity, gst_identity, GstElement, GST_TYPE_ELEMENT,
    _do_init);

static void gst_identity_finalize (GObject * object);
static void gst_identity_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_identity_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstElementStateReturn gst_identity_change_state (GstElement * element);

static gboolean gst_identity_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_identity_getrange (GstPad * pad, guint64 offset,
    guint length, GstBuffer ** buffer);
static GstFlowReturn gst_identity_chain (GstPad * pad, GstBuffer * buffer);
static void gst_identity_src_loop (GstPad * pad);
static void gst_identity_sink_loop (GstPad * pad);
static GstFlowReturn gst_identity_handle_buffer (GstIdentity * identity,
    GstBuffer * buf);
static void gst_identity_set_clock (GstElement * element, GstClock * clock);
static GstCaps *gst_identity_proxy_getcaps (GstPad * pad);


static guint gst_identity_signals[LAST_SIGNAL] = { 0 };

static void
gst_identity_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));
  gst_element_class_set_details (gstelement_class, &gst_identity_details);
}

static void
gst_identity_finalize (GObject * object)
{
  GstIdentity *identity;

  identity = GST_IDENTITY (object);

  g_mutex_free (identity->pen_lock);
  g_cond_free (identity->pen_cond);

  g_free (identity->last_message);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_identity_class_init (GstIdentityClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_identity_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_identity_get_property);

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_HAS_GETRANGE,
      g_param_spec_boolean ("has-getrange", "Has getrange",
          "If the src pad will implement a getrange function",
          TRUE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_HAS_CHAIN,
      g_param_spec_boolean ("has-chain", "Has chain",
          "If the sink pad will implement a chain function",
          TRUE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_HAS_SRC_LOOP,
      g_param_spec_boolean ("has-src-loop", "Has src loop",
          "If the src pad will implement a loop function",
          FALSE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_HAS_SINK_LOOP,
      g_param_spec_boolean ("has-sink-loop", "Has sink loop",
          "If the sink pad will implement a loop function",
          FALSE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SLEEP_TIME,
      g_param_spec_uint ("sleep-time", "Sleep time",
          "Microseconds to sleep between processing", 0, G_MAXUINT,
          DEFAULT_SLEEP_TIME, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DUPLICATE,
      g_param_spec_uint ("duplicate", "Duplicate Buffers",
          "Push the buffers N times", 0, G_MAXUINT, DEFAULT_DUPLICATE,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_ERROR_AFTER,
      g_param_spec_int ("error_after", "Error After", "Error after N buffers",
          G_MININT, G_MAXINT, DEFAULT_ERROR_AFTER, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_DROP_PROBABILITY, g_param_spec_float ("drop_probability",
          "Drop Probability", "The Probability a buffer is dropped", 0.0, 1.0,
          DEFAULT_DROP_PROBABILITY, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DATARATE,
      g_param_spec_int ("datarate", "Datarate",
          "(Re)timestamps buffers with number of bytes per second (0 = inactive)",
          0, G_MAXINT, DEFAULT_DATARATE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SILENT,
      g_param_spec_boolean ("silent", "silent", "silent", DEFAULT_SILENT,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_LAST_MESSAGE,
      g_param_spec_string ("last-message", "last-message", "last-message", NULL,
          G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DUMP,
      g_param_spec_boolean ("dump", "Dump", "Dump buffer contents",
          DEFAULT_DUMP, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SYNC,
      g_param_spec_boolean ("sync", "Synchronize",
          "Synchronize to pipeline clock", DEFAULT_SYNC, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_CHECK_PERFECT,
      g_param_spec_boolean ("check-perfect", "Check For Perfect Stream",
          "Verify that the stream is time- and data-contiguous",
          DEFAULT_CHECK_PERFECT, G_PARAM_READWRITE));

  gst_identity_signals[SIGNAL_HANDOFF] =
      g_signal_new ("handoff", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstIdentityClass, handoff), NULL, NULL,
      gst_marshal_VOID__BOXED, G_TYPE_NONE, 1,
      GST_TYPE_BUFFER | G_SIGNAL_TYPE_STATIC_SCOPE);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_identity_finalize);

  gstelement_class->set_clock = GST_DEBUG_FUNCPTR (gst_identity_set_clock);
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_identity_change_state);

}

static void
gst_identity_init (GstIdentity * identity)
{
  identity->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sinktemplate),
      "sink");
  gst_element_add_pad (GST_ELEMENT (identity), identity->sinkpad);
  gst_pad_set_getcaps_function (identity->sinkpad,
      GST_DEBUG_FUNCPTR (gst_identity_proxy_getcaps));
  gst_pad_set_event_function (identity->sinkpad,
      GST_DEBUG_FUNCPTR (gst_identity_event));

  identity->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&srctemplate),
      "src");
  gst_pad_set_getcaps_function (identity->srcpad,
      GST_DEBUG_FUNCPTR (gst_identity_proxy_getcaps));
  gst_pad_set_getrange_function (identity->srcpad,
      GST_DEBUG_FUNCPTR (gst_identity_getrange));
  gst_element_add_pad (GST_ELEMENT (identity), identity->srcpad);

  identity->sleep_time = DEFAULT_SLEEP_TIME;
  identity->duplicate = DEFAULT_DUPLICATE;
  identity->error_after = DEFAULT_ERROR_AFTER;
  identity->drop_probability = DEFAULT_DROP_PROBABILITY;
  identity->datarate = DEFAULT_DATARATE;
  identity->silent = DEFAULT_SILENT;
  identity->sync = DEFAULT_SYNC;
  identity->check_perfect = DEFAULT_CHECK_PERFECT;
  identity->dump = DEFAULT_DUMP;
  identity->last_message = NULL;
  identity->srccaps = NULL;

  identity->pen_data = NULL;
  identity->pen_lock = g_mutex_new ();
  identity->pen_cond = g_cond_new ();
  identity->pen_flushing = FALSE;
}

static void
gst_identity_set_clock (GstElement * element, GstClock * clock)
{
  GstIdentity *identity = GST_IDENTITY (element);

  gst_object_replace ((GstObject **) & identity->clock, (GstObject *) clock);
}

static GstCaps *
gst_identity_proxy_getcaps (GstPad * pad)
{
  GstPad *otherpad;
  GstIdentity *identity = GST_IDENTITY (GST_OBJECT_PARENT (pad));

  otherpad = pad == identity->srcpad ? identity->sinkpad : identity->srcpad;

  return gst_pad_peer_get_caps (otherpad);
}

static gboolean
identity_queue_push (GstIdentity * identity, GstData * data)
{
  gboolean ret;

  g_mutex_lock (identity->pen_lock);
  while (identity->pen_data && !identity->pen_flushing)
    g_cond_wait (identity->pen_cond, identity->pen_lock);
  if (identity->pen_flushing) {
    gst_data_unref (identity->pen_data);
    identity->pen_data = NULL;
    gst_data_unref (data);
    ret = FALSE;
  } else {
    identity->pen_data = data;
    ret = TRUE;
  }
  g_cond_signal (identity->pen_cond);
  g_mutex_unlock (identity->pen_lock);

  return ret;
}

static GstData *
identity_queue_pop (GstIdentity * identity)
{
  GstData *ret;

  g_mutex_lock (identity->pen_lock);
  while (!(ret = identity->pen_data) && !identity->pen_flushing)
    g_cond_wait (identity->pen_cond, identity->pen_lock);
  g_cond_signal (identity->pen_cond);
  g_mutex_unlock (identity->pen_lock);

  return ret;
}

static void
identity_queue_flush (GstIdentity * identity)
{
  g_mutex_lock (identity->pen_lock);
  identity->pen_flushing = TRUE;
  g_cond_signal (identity->pen_cond);
  g_mutex_unlock (identity->pen_lock);
}

static gboolean
gst_identity_event (GstPad * pad, GstEvent * event)
{
  GstIdentity *identity;
  gboolean ret;

  identity = GST_IDENTITY (GST_PAD_PARENT (pad));

  GST_STREAM_LOCK (pad);

  if (!identity->silent) {
    g_free (identity->last_message);

    identity->last_message =
        g_strdup_printf ("chain   ******* (%s:%s)E (type: %d) %p",
        GST_DEBUG_PAD_NAME (pad), GST_EVENT_TYPE (event), event);

    g_object_notify (G_OBJECT (identity), "last_message");
  }

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH:
      /* forward event */
      gst_pad_event_default (pad, event);
      if (GST_EVENT_FLUSH_DONE (event)) {
        if (identity->sink_mode == GST_ACTIVATE_PULL) {
          /* already have the sink stream lock */
          gst_task_start (GST_RPAD_TASK (identity->sinkpad));
        }
        if (identity->src_mode == GST_ACTIVATE_PUSH) {
          GST_STREAM_LOCK (identity->srcpad);
          gst_task_start (GST_RPAD_TASK (identity->srcpad));
          GST_STREAM_UNLOCK (identity->srcpad);
        }
      } else {
        /* unblock both functions */
        identity_queue_flush (identity);

      }
      ret = TRUE;
      goto done;
    case GST_EVENT_EOS:
      if (identity->sink_mode == GST_ACTIVATE_PULL) {
        /* already have the sink stream lock */
        gst_task_pause (GST_RPAD_TASK (identity->sinkpad));
      }
      break;
    default:
      break;
  }

  if (identity->decoupled) {
    ret = identity_queue_push (identity, (GstData *) event);
  } else {
    ret = gst_pad_push_event (identity->srcpad, event);
  }

done:
  GST_STREAM_UNLOCK (pad);
  return ret;
}

static GstFlowReturn
gst_identity_getrange (GstPad * pad, guint64 offset,
    guint length, GstBuffer ** buffer)
{
  GstIdentity *identity;
  GstFlowReturn ret;

  identity = GST_IDENTITY (GST_PAD_PARENT (pad));

  GST_STREAM_LOCK (pad);

  ret = gst_pad_pull_range (identity->sinkpad, offset, length, buffer);

  GST_STREAM_UNLOCK (pad);

  return ret;
}

static GstFlowReturn
gst_identity_chain (GstPad * pad, GstBuffer * buffer)
{
  GstIdentity *identity;
  GstFlowReturn ret = GST_FLOW_OK;

  identity = GST_IDENTITY (GST_PAD_PARENT (pad));

  GST_STREAM_LOCK (pad);

  ret = gst_identity_handle_buffer (identity, buffer);

  GST_STREAM_UNLOCK (pad);

  return ret;
}

#define DEFAULT_PULL_SIZE 1024

static void
gst_identity_sink_loop (GstPad * pad)
{
  GstIdentity *identity;
  GstBuffer *buffer;
  GstFlowReturn ret;

  identity = GST_IDENTITY (GST_PAD_PARENT (pad));

  GST_STREAM_LOCK (pad);

  ret = gst_pad_pull_range (pad, identity->offset, DEFAULT_PULL_SIZE, &buffer);
  if (ret != GST_FLOW_OK)
    goto sink_loop_pause;

  ret = gst_identity_handle_buffer (identity, buffer);
  if (ret != GST_FLOW_OK)
    goto sink_loop_pause;

  GST_STREAM_UNLOCK (pad);
  return;

sink_loop_pause:
  gst_task_pause (GST_RPAD_TASK (identity->sinkpad));
  GST_STREAM_UNLOCK (pad);
  return;
}

static void
gst_identity_src_loop (GstPad * pad)
{
  GstIdentity *identity;
  GstData *data;
  GstFlowReturn ret;

  identity = GST_IDENTITY (GST_PAD_PARENT (pad));

  GST_STREAM_LOCK (pad);

  data = identity_queue_pop (identity);
  if (!data)                    /* we're getting flushed */
    goto src_loop_pause;

  if (GST_IS_EVENT (data)) {
    if (GST_EVENT_TYPE (data) == GST_EVENT_EOS)
      gst_task_pause (GST_RPAD_TASK (identity->srcpad));
    gst_pad_push_event (identity->srcpad, GST_EVENT (data));
  } else {
    ret = gst_pad_push (identity->srcpad, (GstBuffer *) data);
    if (ret != GST_FLOW_OK)
      goto src_loop_pause;
  }

  GST_STREAM_UNLOCK (pad);
  return;

src_loop_pause:
  gst_task_pause (GST_RPAD_TASK (identity->srcpad));
  GST_STREAM_UNLOCK (pad);
  return;
}

static GstFlowReturn
gst_identity_handle_buffer (GstIdentity * identity, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  guint i;

  /* see if we need to do perfect stream checking */
  /* invalid timestamp drops us out of check.  FIXME: maybe warn ? */
  if (identity->check_perfect &&
      GST_BUFFER_TIMESTAMP (buf) != GST_CLOCK_TIME_NONE) {
    /* check if we had a previous buffer to compare to */
    if (identity->prev_timestamp != GST_CLOCK_TIME_NONE) {
      if (identity->prev_timestamp + identity->prev_duration !=
          GST_BUFFER_TIMESTAMP (buf)) {
        GST_WARNING_OBJECT (identity,
            "Buffer not time-contiguous with previous one: " "prev ts %"
            GST_TIME_FORMAT ", prev dur %" GST_TIME_FORMAT ", new ts %"
            GST_TIME_FORMAT, GST_TIME_ARGS (identity->prev_timestamp),
            GST_TIME_ARGS (identity->prev_duration),
            GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));
      }
      if (identity->prev_offset_end != GST_BUFFER_OFFSET (buf)) {
        GST_WARNING_OBJECT (identity,
            "Buffer not data-contiguous with previous one: "
            "prev offset_end %" G_GINT64_FORMAT ", new offset %"
            G_GINT64_FORMAT, identity->prev_offset_end,
            GST_BUFFER_OFFSET (buf));
      }
    }
    /* update prev values */
    identity->prev_timestamp = GST_BUFFER_TIMESTAMP (buf);
    identity->prev_duration = GST_BUFFER_DURATION (buf);
    identity->prev_offset_end = GST_BUFFER_OFFSET_END (buf);
  }

  if (identity->error_after >= 0) {
    identity->error_after--;
    if (identity->error_after == 0) {
      gst_buffer_unref (buf);
      GST_ELEMENT_ERROR (identity, CORE, FAILED,
          (_("Failed after iterations as requested.")), (NULL));
      return GST_FLOW_ERROR;
    }
  }

  if (identity->drop_probability > 0.0) {
    if ((gfloat) (1.0 * rand () / (RAND_MAX)) < identity->drop_probability) {
      g_free (identity->last_message);
      identity->last_message =
          g_strdup_printf ("dropping   ******* (%s:%s)i (%d bytes, timestamp: %"
          GST_TIME_FORMAT ", duration: %" GST_TIME_FORMAT ", offset: %"
          G_GINT64_FORMAT ", offset_end: % " G_GINT64_FORMAT ", flags: %d) %p",
          GST_DEBUG_PAD_NAME (identity->sinkpad), GST_BUFFER_SIZE (buf),
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
          GST_TIME_ARGS (GST_BUFFER_DURATION (buf)), GST_BUFFER_OFFSET (buf),
          GST_BUFFER_OFFSET_END (buf), GST_BUFFER_FLAGS (buf), buf);
      g_object_notify (G_OBJECT (identity), "last-message");
      gst_buffer_unref (buf);
      return GST_FLOW_OK;
    }
  }

  if (identity->dump) {
    gst_util_dump_mem (GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
  }

  for (i = identity->duplicate; i; i--) {
    GstClockTime time;

    if (!identity->silent) {
      g_free (identity->last_message);
      identity->last_message =
          g_strdup_printf ("chain   ******* (%s:%s)i (%d bytes, timestamp: %"
          GST_TIME_FORMAT ", duration: %" GST_TIME_FORMAT ", offset: %"
          G_GINT64_FORMAT ", offset_end: % " G_GINT64_FORMAT ", flags: %d) %p",
          GST_DEBUG_PAD_NAME (identity->sinkpad), GST_BUFFER_SIZE (buf),
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
          GST_TIME_ARGS (GST_BUFFER_DURATION (buf)), GST_BUFFER_OFFSET (buf),
          GST_BUFFER_OFFSET_END (buf), GST_BUFFER_FLAGS (buf), buf);
      g_object_notify (G_OBJECT (identity), "last-message");
    }

    time = GST_BUFFER_TIMESTAMP (buf);

    if (identity->datarate > 0) {
      time = identity->offset * GST_SECOND / identity->datarate;

      GST_BUFFER_TIMESTAMP (buf) = time;
      GST_BUFFER_DURATION (buf) =
          GST_BUFFER_SIZE (buf) * GST_SECOND / identity->datarate;
    }

    g_signal_emit (G_OBJECT (identity), gst_identity_signals[SIGNAL_HANDOFF], 0,
        buf);

    if (i > 1)
      gst_buffer_ref (buf);

    if (identity->sync) {
      if (identity->clock) {
        /* gst_element_wait (GST_ELEMENT (identity), time); */
      }
    }

    identity->offset += GST_BUFFER_SIZE (buf);
    if (identity->decoupled) {
      if (!identity_queue_push (identity, (GstData *) buf))
        return GST_FLOW_UNEXPECTED;
    } else {
      ret = gst_pad_push (identity->srcpad, buf);
      if (ret != GST_FLOW_OK)
        return ret;
    }

    if (identity->sleep_time)
      g_usleep (identity->sleep_time);
  }

  return ret;
}

static void
gst_identity_set_dataflow_funcs (GstIdentity * identity)
{
  if (identity->has_getrange)
    gst_pad_set_getrange_function (identity->srcpad, gst_identity_getrange);
  else
    gst_pad_set_getrange_function (identity->srcpad, NULL);

  if (identity->has_chain)
    gst_pad_set_chain_function (identity->sinkpad, gst_identity_chain);
  else
    gst_pad_set_chain_function (identity->sinkpad, NULL);

  if (identity->has_src_loop)
    gst_pad_set_loop_function (identity->srcpad, gst_identity_src_loop);
  else
    gst_pad_set_loop_function (identity->srcpad, NULL);

  if (identity->has_sink_loop)
    gst_pad_set_loop_function (identity->sinkpad, gst_identity_sink_loop);
  else
    gst_pad_set_loop_function (identity->sinkpad, NULL);
}

static void
gst_identity_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstIdentity *identity;

  identity = GST_IDENTITY (object);

  switch (prop_id) {
    case PROP_HAS_GETRANGE:
      identity->has_getrange = g_value_get_boolean (value);
      gst_identity_set_dataflow_funcs (identity);
      break;
    case PROP_HAS_CHAIN:
      identity->has_chain = g_value_get_boolean (value);
      gst_identity_set_dataflow_funcs (identity);
      break;
    case PROP_HAS_SRC_LOOP:
      identity->has_src_loop = g_value_get_boolean (value);
      gst_identity_set_dataflow_funcs (identity);
      break;
    case PROP_HAS_SINK_LOOP:
      identity->has_sink_loop = g_value_get_boolean (value);
      gst_identity_set_dataflow_funcs (identity);
      break;
    case PROP_SLEEP_TIME:
      identity->sleep_time = g_value_get_uint (value);
      break;
    case PROP_SILENT:
      identity->silent = g_value_get_boolean (value);
      break;
    case PROP_DUPLICATE:
      identity->duplicate = g_value_get_uint (value);
      break;
    case PROP_DUMP:
      identity->dump = g_value_get_boolean (value);
      break;
    case PROP_ERROR_AFTER:
      identity->error_after = g_value_get_int (value);
      break;
    case PROP_DROP_PROBABILITY:
      identity->drop_probability = g_value_get_float (value);
      break;
    case PROP_DATARATE:
      identity->datarate = g_value_get_int (value);
      break;
    case PROP_SYNC:
      identity->sync = g_value_get_boolean (value);
      break;
    case PROP_CHECK_PERFECT:
      identity->check_perfect = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_identity_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstIdentity *identity;

  identity = GST_IDENTITY (object);

  switch (prop_id) {
    case PROP_HAS_GETRANGE:
      g_value_set_boolean (value, identity->has_getrange);
      break;
    case PROP_HAS_CHAIN:
      g_value_set_boolean (value, identity->has_chain);
      break;
    case PROP_HAS_SRC_LOOP:
      g_value_set_boolean (value, identity->has_src_loop);
      break;
    case PROP_HAS_SINK_LOOP:
      g_value_set_boolean (value, identity->has_sink_loop);
      break;
    case PROP_SLEEP_TIME:
      g_value_set_uint (value, identity->sleep_time);
      break;
    case PROP_DUPLICATE:
      g_value_set_uint (value, identity->duplicate);
      break;
    case PROP_ERROR_AFTER:
      g_value_set_int (value, identity->error_after);
      break;
    case PROP_DROP_PROBABILITY:
      g_value_set_float (value, identity->drop_probability);
      break;
    case PROP_DATARATE:
      g_value_set_int (value, identity->datarate);
      break;
    case PROP_SILENT:
      g_value_set_boolean (value, identity->silent);
      break;
    case PROP_DUMP:
      g_value_set_boolean (value, identity->dump);
      break;
    case PROP_LAST_MESSAGE:
      g_value_set_string (value, identity->last_message);
      break;
    case PROP_SYNC:
      g_value_set_boolean (value, identity->sync);
      break;
    case PROP_CHECK_PERFECT:
      g_value_set_boolean (value, identity->check_perfect);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn
gst_identity_change_state (GstElement * element)
{
  GstIdentity *identity;

  g_return_val_if_fail (GST_IS_IDENTITY (element), GST_STATE_FAILURE);

  identity = GST_IDENTITY (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      identity->offset = 0;
      identity->prev_timestamp = GST_CLOCK_TIME_NONE;
      identity->prev_duration = GST_CLOCK_TIME_NONE;
      identity->prev_offset_end = -1;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      g_free (identity->last_message);
      identity->last_message = NULL;
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}
