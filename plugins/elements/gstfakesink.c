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

GST_DEBUG_CATEGORY_STATIC (gst_fakesink_debug);
#define GST_CAT_DEFAULT gst_fakesink_debug

GstElementDetails gst_fakesink_details = GST_ELEMENT_DETAILS ("Fake Sink",
    "Sink",
    "Black hole for data",
    "Erik Walthinsen <omega@cse.ogi.edu>");


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
  ARG_LAST_MESSAGE
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

static void gst_fakesink_chain (GstPad * pad, GstData * _data);

static guint gst_fakesink_signals[LAST_SIGNAL] = { 0 };

static void
gst_fakesink_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

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

  gst_fakesink_signals[SIGNAL_HANDOFF] =
      g_signal_new ("handoff", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstFakeSinkClass, handoff), NULL, NULL,
      gst_marshal_VOID__POINTER_OBJECT, G_TYPE_NONE, 2,
      GST_TYPE_BUFFER, GST_TYPE_PAD);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_fakesink_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_fakesink_get_property);

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

  pad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (fakesink), pad);
  gst_pad_set_chain_function (pad, GST_DEBUG_FUNCPTR (gst_fakesink_chain));

  fakesink->silent = FALSE;
  fakesink->dump = FALSE;
  fakesink->sync = FALSE;
  fakesink->last_message = NULL;
  fakesink->state_error = FAKESINK_STATE_ERROR_NONE;
  fakesink->signal_handoffs = FALSE;

  GST_FLAG_SET (fakesink, GST_ELEMENT_EVENT_AWARE);
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
  gst_pad_set_chain_function (sinkpad, GST_DEBUG_FUNCPTR (gst_fakesink_chain));

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

static void
gst_fakesink_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstFakeSink *fakesink;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  fakesink = GST_FAKESINK (GST_OBJECT_PARENT (pad));

  if (GST_IS_EVENT (buf)) {
    GstEvent *event = GST_EVENT (buf);

    if (!fakesink->silent) {
      g_free (fakesink->last_message);

      fakesink->last_message =
          g_strdup_printf ("chain   ******* (%s:%s)E (type: %d) %p",
          GST_DEBUG_PAD_NAME (pad), GST_EVENT_TYPE (event), event);

      g_object_notify (G_OBJECT (fakesink), "last_message");
    }

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_DISCONTINUOUS:
        if (fakesink->sync && fakesink->clock) {
          gint64 value = GST_EVENT_DISCONT_OFFSET (event, 0).value;

          gst_element_set_time (GST_ELEMENT (fakesink), value);
        }
      default:
        gst_pad_event_default (pad, event);
        break;
    }
    return;
  }

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

  gst_buffer_unref (buf);
}

static GstElementStateReturn
gst_fakesink_change_state (GstElement * element)
{
  GstFakeSink *fakesink = GST_FAKESINK (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      if (fakesink->state_error == FAKESINK_STATE_ERROR_NULL_READY)
        goto error;
      break;
    case GST_STATE_READY_TO_PAUSED:
      if (fakesink->state_error == FAKESINK_STATE_ERROR_READY_PAUSED)
        goto error;
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
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;

error:
  GST_ELEMENT_ERROR (element, CORE, STATE_CHANGE, (NULL), (NULL));
  return GST_STATE_FAILURE;
}
