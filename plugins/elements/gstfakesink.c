/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstfakesink.c: 
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


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstfakesink.h"
#include <gst/gstmarshal.h>

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_fakesink_debug);
#define GST_CAT_DEFAULT gst_fakesink_debug

#define DEFAULT_SIZE 1024

GstElementDetails gst_fakesink_details = GST_ELEMENT_DETAILS ("Fake Sink",
    "Sink",
    "Black hole for data",
    "Erik Walthinsen <omega@cse.ogi.edu>, Tim Waymans <tway@fluendo.com>");


/* FakeSink signals and args */
enum
{
  /* FILL ME */
  SIGNAL_HANDOFF,
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_STATE_ERROR,
  ARG_NUM_SINKS,
  ARG_SILENT,
  ARG_DUMP,
  ARG_SYNC,
  ARG_SIGNAL_HANDOFFS,
  ARG_LAST_MESSAGE,
  ARG_HAS_LOOP,
  ARG_HAS_CHAIN
};

GstStaticPadTemplate fakesink_sink_template = GST_STATIC_PAD_TEMPLATE ("sink%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

#define GST_TYPE_FAKESINK_STATE_ERROR (gst_fakesink_state_error_get_type())
static GType
gst_fakesink_state_error_get_type (void)
{
  static GType fakesink_state_error_type = 0;
  static GEnumValue fakesink_state_error[] = {
    {FAKESINK_STATE_ERROR_NONE, "0", "No state change errors"},
    {FAKESINK_STATE_ERROR_NULL_READY, "1",
        "Fail state change from NULL to READY"},
    {FAKESINK_STATE_ERROR_READY_PAUSED, "2",
        "Fail state change from READY to PAUSED"},
    {FAKESINK_STATE_ERROR_PAUSED_PLAYING, "3",
        "Fail state change from PAUSED to PLAYING"},
    {FAKESINK_STATE_ERROR_PLAYING_PAUSED, "4",
        "Fail state change from PLAYING to PAUSED"},
    {FAKESINK_STATE_ERROR_PAUSED_READY, "5",
        "Fail state change from PAUSED to READY"},
    {FAKESINK_STATE_ERROR_READY_NULL, "6",
        "Fail state change from READY to NULL"},
    {0, NULL, NULL},
  };

  if (!fakesink_state_error_type) {
    fakesink_state_error_type =
        g_enum_register_static ("GstFakeSinkStateError", fakesink_state_error);
  }
  return fakesink_state_error_type;
}

#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_fakesink_debug, "fakesink", 0, "fakesink element");

GST_BOILERPLATE_FULL (GstFakeSink, gst_fakesink, GstElement, GST_TYPE_ELEMENT,
    _do_init);

static void gst_fakesink_set_clock (GstElement * element, GstClock * clock);
static GstPad *gst_fakesink_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * unused);

static void gst_fakesink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_fakesink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstElementStateReturn gst_fakesink_change_state (GstElement * element);

static GstFlowReturn gst_fakesink_chain_unlocked (GstPad * pad,
    GstBuffer * buffer);
static void gst_fakesink_loop (GstPad * pad);
static GstFlowReturn gst_fakesink_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_fakesink_activate (GstPad * pad, GstActivateMode mode);
static gboolean gst_fakesink_event (GstPad * pad, GstEvent * event);

static guint gst_fakesink_signals[LAST_SIGNAL] = { 0 };

static void
gst_fakesink_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));
  gst_element_class_set_details (gstelement_class, &gst_fakesink_details);
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&fakesink_sink_template));
}

static void
gst_fakesink_class_init (GstFakeSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_fakesink_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_fakesink_get_property);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_NUM_SINKS,
      g_param_spec_int ("num_sinks", "Number of sinks",
          "The number of sinkpads", 1, G_MAXINT, 1, G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_STATE_ERROR,
      g_param_spec_enum ("state_error", "State Error",
          "Generate a state change error", GST_TYPE_FAKESINK_STATE_ERROR,
          FAKESINK_STATE_ERROR_NONE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LAST_MESSAGE,
      g_param_spec_string ("last_message", "Last Message",
          "The message describing current status", NULL, G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SYNC,
      g_param_spec_boolean ("sync", "Sync", "Sync on the clock", FALSE,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SIGNAL_HANDOFFS,
      g_param_spec_boolean ("signal-handoffs", "Signal handoffs",
          "Send a signal before unreffing the buffer", FALSE,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SILENT,
      g_param_spec_boolean ("silent", "Silent",
          "Don't produce last_message events", FALSE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DUMP,
      g_param_spec_boolean ("dump", "Dump", "Dump received bytes to stdout",
          FALSE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_HAS_LOOP,
      g_param_spec_boolean ("has-loop", "has-loop",
          "Enable loop-based operation", TRUE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_HAS_CHAIN,
      g_param_spec_boolean ("has-chain", "has-chain",
          "Enable chain-based operation", TRUE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  gst_fakesink_signals[SIGNAL_HANDOFF] =
      g_signal_new ("handoff", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstFakeSinkClass, handoff), NULL, NULL,
      gst_marshal_VOID__BOXED_OBJECT, G_TYPE_NONE, 2,
      GST_TYPE_BUFFER | G_SIGNAL_TYPE_STATIC_SCOPE, GST_TYPE_PAD);

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_fakesink_request_new_pad);
  gstelement_class->set_clock = GST_DEBUG_FUNCPTR (gst_fakesink_set_clock);
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_fakesink_change_state);
}

static void
gst_fakesink_init (GstFakeSink * fakesink)
{
  GstPad *pad;

  pad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sinktemplate),
      "sink");
  gst_element_add_pad (GST_ELEMENT (fakesink), pad);

  fakesink->silent = FALSE;
  fakesink->dump = FALSE;
  fakesink->sync = FALSE;
  fakesink->last_message = NULL;
  fakesink->state_error = FAKESINK_STATE_ERROR_NONE;
  fakesink->signal_handoffs = FALSE;
  fakesink->pad_mode = GST_ACTIVATE_NONE;
  GST_RPAD_TASK (pad) = NULL;
}

static void
gst_fakesink_set_pad_functions (GstFakeSink * this, GstPad * pad)
{
  gst_pad_set_activate_function (pad,
      GST_DEBUG_FUNCPTR (gst_fakesink_activate));
  gst_pad_set_event_function (pad, GST_DEBUG_FUNCPTR (gst_fakesink_event));

  if (this->has_chain)
    gst_pad_set_chain_function (pad, GST_DEBUG_FUNCPTR (gst_fakesink_chain));
  else
    gst_pad_set_chain_function (pad, NULL);

  if (this->has_loop)
    gst_pad_set_loop_function (pad, GST_DEBUG_FUNCPTR (gst_fakesink_loop));
  else
    gst_pad_set_loop_function (pad, NULL);
}

static void
gst_fakesink_set_all_pad_functions (GstFakeSink * this)
{
  GList *l;

  for (l = GST_ELEMENT_PADS (this); l; l = l->next)
    gst_fakesink_set_pad_functions (this, (GstPad *) l->data);
}

static void
gst_fakesink_set_clock (GstElement * element, GstClock * clock)
{
  GstFakeSink *sink;

  sink = GST_FAKESINK (element);

  sink->clock = clock;
}

static GstPad *
gst_fakesink_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * unused)
{
  gchar *name;
  GstPad *sinkpad;
  GstFakeSink *fakesink;

  g_return_val_if_fail (GST_IS_FAKESINK (element), NULL);

  if (templ->direction != GST_PAD_SINK) {
    g_warning ("gstfakesink: request new pad that is not a SINK pad\n");
    return NULL;
  }

  fakesink = GST_FAKESINK (element);

  name = g_strdup_printf ("sink%d", GST_ELEMENT (fakesink)->numsinkpads);

  sinkpad = gst_pad_new_from_template (templ, name);
  g_free (name);
  gst_fakesink_set_pad_functions (fakesink, sinkpad);
  gst_element_add_pad (GST_ELEMENT (fakesink), sinkpad);

  return sinkpad;
}

static void
gst_fakesink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstFakeSink *sink;

  /* it's not null if we got it, but it might not be ours */
  sink = GST_FAKESINK (object);

  switch (prop_id) {
    case ARG_HAS_LOOP:
      sink->has_loop = g_value_get_boolean (value);
      gst_fakesink_set_all_pad_functions (sink);
      break;
    case ARG_HAS_CHAIN:
      sink->has_chain = g_value_get_boolean (value);
      gst_fakesink_set_all_pad_functions (sink);
      break;
    case ARG_SILENT:
      sink->silent = g_value_get_boolean (value);
      break;
    case ARG_STATE_ERROR:
      sink->state_error = g_value_get_enum (value);
      break;
    case ARG_DUMP:
      sink->dump = g_value_get_boolean (value);
      break;
    case ARG_SYNC:
      sink->sync = g_value_get_boolean (value);
      break;
    case ARG_SIGNAL_HANDOFFS:
      sink->signal_handoffs = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_fakesink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstFakeSink *sink;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_FAKESINK (object));

  sink = GST_FAKESINK (object);

  switch (prop_id) {
    case ARG_NUM_SINKS:
      g_value_set_int (value, GST_ELEMENT (sink)->numsinkpads);
      break;
    case ARG_STATE_ERROR:
      g_value_set_enum (value, sink->state_error);
      break;
    case ARG_SILENT:
      g_value_set_boolean (value, sink->silent);
      break;
    case ARG_DUMP:
      g_value_set_boolean (value, sink->dump);
      break;
    case ARG_SYNC:
      g_value_set_boolean (value, sink->sync);
      break;
    case ARG_SIGNAL_HANDOFFS:
      g_value_set_boolean (value, sink->signal_handoffs);
      break;
    case ARG_LAST_MESSAGE:
      g_value_set_string (value, sink->last_message);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_fakesink_activate (GstPad * pad, GstActivateMode mode)
{
  gboolean result = FALSE;
  GstFakeSink *fakesink;

  fakesink = GST_FAKESINK (GST_OBJECT_PARENT (pad));

  switch (mode) {
    case GST_ACTIVATE_PUSH:
      g_return_val_if_fail (fakesink->has_chain, FALSE);
      result = TRUE;
      break;
    case GST_ACTIVATE_PULL:
      /* if we have a scheduler we can start the task */
      g_return_val_if_fail (fakesink->has_loop, FALSE);
      if (GST_ELEMENT_SCHEDULER (fakesink)) {
        GST_STREAM_LOCK (pad);
        GST_RPAD_TASK (pad) =
            gst_scheduler_create_task (GST_ELEMENT_SCHEDULER (fakesink),
            (GstTaskFunction) gst_fakesink_loop, pad);

        gst_task_start (GST_RPAD_TASK (pad));
        GST_STREAM_UNLOCK (pad);
        result = TRUE;
      }
      break;
    case GST_ACTIVATE_NONE:
      /* step 1, unblock clock sync (if any) */

      /* step 2, make sure streaming finishes */
      GST_STREAM_LOCK (pad);
      /* step 3, stop the task */
      if (GST_RPAD_TASK (pad)) {
        gst_task_stop (GST_RPAD_TASK (pad));
        gst_object_unref (GST_OBJECT (GST_RPAD_TASK (pad)));
        GST_RPAD_TASK (pad) = NULL;
      }
      GST_STREAM_UNLOCK (pad);

      result = TRUE;
      break;
  }
  return result;
}

static gboolean
gst_fakesink_event (GstPad * pad, GstEvent * event)
{
  GstFakeSink *fakesink;
  gboolean result = TRUE;

  fakesink = GST_FAKESINK (GST_OBJECT_PARENT (pad));

  GST_STREAM_LOCK (pad);

  if (!fakesink->silent) {
    g_free (fakesink->last_message);

    fakesink->last_message =
        g_strdup_printf ("chain   ******* (%s:%s)E (type: %d) %p",
        GST_DEBUG_PAD_NAME (pad), GST_EVENT_TYPE (event), event);

    g_object_notify (G_OBJECT (fakesink), "last_message");
  }

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
    {
      gst_element_finish_preroll (GST_ELEMENT (fakesink), pad);
      gst_element_post_message (GST_ELEMENT (fakesink),
          gst_message_new_eos (GST_OBJECT (fakesink)));
      break;
    }
    case GST_EVENT_DISCONTINUOUS:
      if (fakesink->sync && fakesink->clock) {
        //gint64 value = GST_EVENT_DISCONT_OFFSET (event, 0).value;
      }
    default:
      result = gst_pad_event_default (pad, event);
      break;
  }
  GST_STREAM_UNLOCK (pad);

  return result;
}

static GstFlowReturn
gst_fakesink_chain_unlocked (GstPad * pad, GstBuffer * buf)
{
  GstFakeSink *fakesink;
  GstFlowReturn result = GST_FLOW_OK;

  fakesink = GST_FAKESINK (GST_OBJECT_PARENT (pad));

  result = gst_element_finish_preroll (GST_ELEMENT (fakesink), pad);
  if (result != GST_FLOW_OK)
    goto exit;

  if (fakesink->sync && fakesink->clock) {
    gst_element_wait (GST_ELEMENT (fakesink), GST_BUFFER_TIMESTAMP (buf));
  }

  if (!fakesink->silent) {
    g_free (fakesink->last_message);

    fakesink->last_message =
        g_strdup_printf ("chain   ******* (%s:%s)< (%d bytes, timestamp: %"
        GST_TIME_FORMAT ", duration: %" GST_TIME_FORMAT ", offset: %"
        G_GINT64_FORMAT ", offset_end: %" G_GINT64_FORMAT ", flags: %d) %p",
        GST_DEBUG_PAD_NAME (pad), GST_BUFFER_SIZE (buf),
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (buf)), GST_BUFFER_OFFSET (buf),
        GST_BUFFER_OFFSET_END (buf), GST_BUFFER_FLAGS (buf), buf);

    g_object_notify (G_OBJECT (fakesink), "last_message");
  }

  if (fakesink->signal_handoffs)
    g_signal_emit (G_OBJECT (fakesink), gst_fakesink_signals[SIGNAL_HANDOFF], 0,
        buf, pad);

  if (fakesink->dump) {
    gst_util_dump_mem (GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
  }

exit:
  gst_buffer_unref (buf);

  return result;
}

static GstFlowReturn
gst_fakesink_chain (GstPad * pad, GstBuffer * buf)
{
  GstFlowReturn result;

  GST_STREAM_LOCK (pad);

  result = gst_fakesink_chain_unlocked (pad, buf);

  GST_STREAM_UNLOCK (pad);

  return result;
}

static void
gst_fakesink_loop (GstPad * pad)
{
  GstFakeSink *fakesink;
  GstBuffer *buf = NULL;
  GstFlowReturn result;

  fakesink = GST_FAKESINK (GST_OBJECT_PARENT (pad));

  GST_STREAM_LOCK (pad);

  result = gst_pad_pull_range (pad, fakesink->offset, DEFAULT_SIZE, &buf);
  if (result != GST_FLOW_OK)
    goto paused;

  result = gst_fakesink_chain_unlocked (pad, buf);
  if (result != GST_FLOW_OK)
    goto paused;

exit:
  GST_STREAM_UNLOCK (pad);
  return;

paused:
  gst_task_pause (GST_RPAD_TASK (pad));
  goto exit;
}

static GstElementStateReturn
gst_fakesink_change_state (GstElement * element)
{
  GstElementStateReturn ret = GST_STATE_SUCCESS;
  GstFakeSink *fakesink = GST_FAKESINK (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      if (fakesink->state_error == FAKESINK_STATE_ERROR_NULL_READY)
        goto error;
      break;
    case GST_STATE_READY_TO_PAUSED:
      if (fakesink->state_error == FAKESINK_STATE_ERROR_READY_PAUSED)
        goto error;
      /* need to complete preroll before this state change completes */
      fakesink->offset = 0;
      ret = GST_STATE_ASYNC;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      if (fakesink->state_error == FAKESINK_STATE_ERROR_PAUSED_PLAYING)
        goto error;
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      if (fakesink->state_error == FAKESINK_STATE_ERROR_PLAYING_PAUSED)
        goto error;
      break;
    case GST_STATE_PAUSED_TO_READY:
      if (fakesink->state_error == FAKESINK_STATE_ERROR_PAUSED_READY)
        goto error;
      break;
    case GST_STATE_READY_TO_NULL:
      if (fakesink->state_error == FAKESINK_STATE_ERROR_READY_NULL)
        goto error;
      g_free (fakesink->last_message);
      fakesink->last_message = NULL;
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return ret;

error:
  GST_ELEMENT_ERROR (element, CORE, STATE_CHANGE, (NULL), (NULL));
  return GST_STATE_FAILURE;
}
