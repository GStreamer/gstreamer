/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2005 Wim Taymans <wim@fluendo.com>
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
/**
 * SECTION:element-fakesink
 * @short_description: black hole for data
 * @see_also: #GstFakeSrc
 *
 * Dummy sink that swallows everything.
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

GST_DEBUG_CATEGORY_STATIC (gst_fake_sink_debug);
#define GST_CAT_DEFAULT gst_fake_sink_debug

static GstElementDetails gst_fake_sink_details =
GST_ELEMENT_DETAILS ("Fake Sink",
    "Sink",
    "Black hole for data",
    "Erik Walthinsen <omega@cse.ogi.edu>, "
    "Wim Taymans <wim@fluendo.com>, "
    "Mr. 'frag-me-more' Vanderwingo <wingo@fluendo.com>");


/* FakeSink signals and args */
enum
{
  /* FILL ME */
  SIGNAL_HANDOFF,
  LAST_SIGNAL
};

#define DEFAULT_SYNC FALSE

#define DEFAULT_STATE_ERROR FAKE_SINK_STATE_ERROR_NONE
#define DEFAULT_SILENT FALSE
#define DEFAULT_DUMP FALSE
#define DEFAULT_SIGNAL_HANDOFFS FALSE
#define DEFAULT_LAST_MESSAGE NULL
#define DEFAULT_CAN_ACTIVATE_PUSH TRUE
#define DEFAULT_CAN_ACTIVATE_PULL FALSE

enum
{
  PROP_0,
  PROP_STATE_ERROR,
  PROP_SILENT,
  PROP_DUMP,
  PROP_SIGNAL_HANDOFFS,
  PROP_LAST_MESSAGE,
  PROP_CAN_ACTIVATE_PUSH,
  PROP_CAN_ACTIVATE_PULL
};

#define GST_TYPE_FAKE_SINK_STATE_ERROR (gst_fake_sink_state_error_get_type())
static GType
gst_fake_sink_state_error_get_type (void)
{
  static GType fakesink_state_error_type = 0;
  static GEnumValue fakesink_state_error[] = {
    {FAKE_SINK_STATE_ERROR_NONE, "No state change errors", "none"},
    {FAKE_SINK_STATE_ERROR_NULL_READY,
        "Fail state change from NULL to READY", "null-to-ready"},
    {FAKE_SINK_STATE_ERROR_READY_PAUSED,
        "Fail state change from READY to PAUSED", "ready-to-paused"},
    {FAKE_SINK_STATE_ERROR_PAUSED_PLAYING,
        "Fail state change from PAUSED to PLAYING", "paused-to-playing"},
    {FAKE_SINK_STATE_ERROR_PLAYING_PAUSED,
        "Fail state change from PLAYING to PAUSED", "playing-to-paused"},
    {FAKE_SINK_STATE_ERROR_PAUSED_READY,
        "Fail state change from PAUSED to READY", "paused-to-ready"},
    {FAKE_SINK_STATE_ERROR_READY_NULL,
        "Fail state change from READY to NULL", "ready-to-null"},
    {0, NULL, NULL},
  };

  if (!fakesink_state_error_type) {
    fakesink_state_error_type =
        g_enum_register_static ("GstFakeSinkStateError", fakesink_state_error);
  }
  return fakesink_state_error_type;
}

#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_fake_sink_debug, "fakesink", 0, "fakesink element");

GST_BOILERPLATE_FULL (GstFakeSink, gst_fake_sink, GstBaseSink,
    GST_TYPE_BASE_SINK, _do_init);

static void gst_fake_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_fake_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_fake_sink_change_state (GstElement * element,
    GstStateChange transition);

static GstFlowReturn gst_fake_sink_preroll (GstBaseSink * bsink,
    GstBuffer * buffer);
static GstFlowReturn gst_fake_sink_render (GstBaseSink * bsink,
    GstBuffer * buffer);
static gboolean gst_fake_sink_event (GstBaseSink * bsink, GstEvent * event);

static guint gst_fake_sink_signals[LAST_SIGNAL] = { 0 };

static void
gst_fake_sink_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));
  gst_element_class_set_details (gstelement_class, &gst_fake_sink_details);
}

static void
gst_fake_sink_class_init (GstFakeSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbase_sink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbase_sink_class = (GstBaseSinkClass *) klass;

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_fake_sink_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_fake_sink_get_property);

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_STATE_ERROR,
      g_param_spec_enum ("state_error", "State Error",
          "Generate a state change error", GST_TYPE_FAKE_SINK_STATE_ERROR,
          DEFAULT_STATE_ERROR, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_LAST_MESSAGE,
      g_param_spec_string ("last_message", "Last Message",
          "The message describing current status", DEFAULT_LAST_MESSAGE,
          G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SIGNAL_HANDOFFS,
      g_param_spec_boolean ("signal-handoffs", "Signal handoffs",
          "Send a signal before unreffing the buffer", DEFAULT_SIGNAL_HANDOFFS,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent",
          "Don't produce last_message events", DEFAULT_SILENT,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DUMP,
      g_param_spec_boolean ("dump", "Dump", "Dump received bytes to stdout",
          DEFAULT_DUMP, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_CAN_ACTIVATE_PUSH,
      g_param_spec_boolean ("can-activate-push", "Can activate push",
          "Can activate in push mode", DEFAULT_CAN_ACTIVATE_PUSH,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_CAN_ACTIVATE_PULL,
      g_param_spec_boolean ("can-activate-pull", "Can activate pull",
          "Can activate in pull mode", DEFAULT_CAN_ACTIVATE_PULL,
          G_PARAM_READWRITE));

  /**
   * GstFakeSink::handoff:
   * @fakesink: the fakesink instance
   * @buffer: the buffer that just has been received
   * @pad: the pad that received it
   *
   * This signal gets emitted before unreffing the buffer.
   */
  gst_fake_sink_signals[SIGNAL_HANDOFF] =
      g_signal_new ("handoff", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstFakeSinkClass, handoff), NULL, NULL,
      gst_marshal_VOID__OBJECT_OBJECT, G_TYPE_NONE, 2,
      GST_TYPE_BUFFER, GST_TYPE_PAD);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_fake_sink_change_state);

  gstbase_sink_class->event = GST_DEBUG_FUNCPTR (gst_fake_sink_event);
  gstbase_sink_class->preroll = GST_DEBUG_FUNCPTR (gst_fake_sink_preroll);
  gstbase_sink_class->render = GST_DEBUG_FUNCPTR (gst_fake_sink_render);
}

static void
gst_fake_sink_init (GstFakeSink * fakesink, GstFakeSinkClass * g_class)
{
  fakesink->silent = DEFAULT_SILENT;
  fakesink->dump = DEFAULT_DUMP;
  GST_BASE_SINK (fakesink)->sync = DEFAULT_SYNC;
  fakesink->last_message = g_strdup (DEFAULT_LAST_MESSAGE);
  fakesink->state_error = DEFAULT_STATE_ERROR;
  fakesink->signal_handoffs = DEFAULT_SIGNAL_HANDOFFS;
}

static void
gst_fake_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstFakeSink *sink;

  sink = GST_FAKE_SINK (object);

  switch (prop_id) {
    case PROP_SILENT:
      sink->silent = g_value_get_boolean (value);
      break;
    case PROP_STATE_ERROR:
      sink->state_error = g_value_get_enum (value);
      break;
    case PROP_DUMP:
      sink->dump = g_value_get_boolean (value);
      break;
    case PROP_SIGNAL_HANDOFFS:
      sink->signal_handoffs = g_value_get_boolean (value);
      break;
    case PROP_CAN_ACTIVATE_PUSH:
      GST_BASE_SINK (sink)->can_activate_push = g_value_get_boolean (value);
      break;
    case PROP_CAN_ACTIVATE_PULL:
      GST_BASE_SINK (sink)->can_activate_pull = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_fake_sink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstFakeSink *sink;

  sink = GST_FAKE_SINK (object);

  switch (prop_id) {
    case PROP_STATE_ERROR:
      g_value_set_enum (value, sink->state_error);
      break;
    case PROP_SILENT:
      g_value_set_boolean (value, sink->silent);
      break;
    case PROP_DUMP:
      g_value_set_boolean (value, sink->dump);
      break;
    case PROP_SIGNAL_HANDOFFS:
      g_value_set_boolean (value, sink->signal_handoffs);
      break;
    case PROP_LAST_MESSAGE:
      GST_OBJECT_LOCK (sink);
      g_value_set_string (value, sink->last_message);
      GST_OBJECT_UNLOCK (sink);
      break;
    case PROP_CAN_ACTIVATE_PUSH:
      g_value_set_boolean (value, GST_BASE_SINK (sink)->can_activate_push);
      break;
    case PROP_CAN_ACTIVATE_PULL:
      g_value_set_boolean (value, GST_BASE_SINK (sink)->can_activate_pull);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_fake_sink_event (GstBaseSink * bsink, GstEvent * event)
{
  GstFakeSink *sink = GST_FAKE_SINK (bsink);

  if (!sink->silent) {
    const GstStructure *s;
    gchar *sstr;

    GST_OBJECT_LOCK (sink);
    g_free (sink->last_message);

    if ((s = gst_event_get_structure (event)))
      sstr = gst_structure_to_string (s);
    else
      sstr = g_strdup ("");

    sink->last_message =
        g_strdup_printf ("event   ******* E (type: %d, %s) %p",
        GST_EVENT_TYPE (event), sstr, event);
    g_free (sstr);
    GST_OBJECT_UNLOCK (sink);

    g_object_notify (G_OBJECT (sink), "last_message");
  }

  return TRUE;
}

static GstFlowReturn
gst_fake_sink_preroll (GstBaseSink * bsink, GstBuffer * buffer)
{
  GstFakeSink *sink = GST_FAKE_SINK (bsink);

  if (!sink->silent) {
    GST_OBJECT_LOCK (sink);
    g_free (sink->last_message);

    sink->last_message = g_strdup_printf ("preroll   ******* ");
    GST_OBJECT_UNLOCK (sink);

    g_object_notify (G_OBJECT (sink), "last_message");
  }
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_fake_sink_render (GstBaseSink * bsink, GstBuffer * buf)
{
  GstFakeSink *sink = GST_FAKE_SINK (bsink);

  if (!sink->silent) {
    gchar ts_str[64], dur_str[64];

    GST_OBJECT_LOCK (sink);
    g_free (sink->last_message);

    if (GST_BUFFER_TIMESTAMP (buf) != GST_CLOCK_TIME_NONE) {
      g_snprintf (ts_str, sizeof (ts_str), "%" GST_TIME_FORMAT,
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));
    } else {
      g_strlcpy (ts_str, "none", sizeof (ts_str));
    }

    if (GST_BUFFER_DURATION (buf) != GST_CLOCK_TIME_NONE) {
      g_snprintf (dur_str, sizeof (dur_str), "%" GST_TIME_FORMAT,
          GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));
    } else {
      g_strlcpy (dur_str, "none", sizeof (dur_str));
    }

    sink->last_message =
        g_strdup_printf ("chain   ******* < (%5d bytes, timestamp: %s"
        ", duration: %s, offset: %" G_GINT64_FORMAT ", offset_end: %"
        G_GINT64_FORMAT ", flags: %d) %p", GST_BUFFER_SIZE (buf), ts_str,
        dur_str, GST_BUFFER_OFFSET (buf), GST_BUFFER_OFFSET_END (buf),
        GST_MINI_OBJECT (buf)->flags, buf);
    GST_OBJECT_UNLOCK (sink);

    g_object_notify (G_OBJECT (sink), "last_message");
  }
  if (sink->signal_handoffs)
    g_signal_emit (G_OBJECT (sink), gst_fake_sink_signals[SIGNAL_HANDOFF], 0,
        buf, bsink->sinkpad);

  if (sink->dump) {
    gst_util_dump_mem (GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
  }

  return GST_FLOW_OK;
}

static GstStateChangeReturn
gst_fake_sink_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstFakeSink *fakesink = GST_FAKE_SINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (fakesink->state_error == FAKE_SINK_STATE_ERROR_NULL_READY)
        goto error;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (fakesink->state_error == FAKE_SINK_STATE_ERROR_READY_PAUSED)
        goto error;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      if (fakesink->state_error == FAKE_SINK_STATE_ERROR_PAUSED_PLAYING)
        goto error;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      if (fakesink->state_error == FAKE_SINK_STATE_ERROR_PLAYING_PAUSED)
        goto error;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (fakesink->state_error == FAKE_SINK_STATE_ERROR_PAUSED_READY)
        goto error;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (fakesink->state_error == FAKE_SINK_STATE_ERROR_READY_NULL)
        goto error;
      GST_OBJECT_LOCK (fakesink);
      g_free (fakesink->last_message);
      fakesink->last_message = NULL;
      GST_OBJECT_UNLOCK (fakesink);
      break;
    default:
      break;
  }

  return ret;

  /* ERROR */
error:
  GST_ELEMENT_ERROR (element, CORE, STATE_CHANGE, (NULL), (NULL));
  return GST_STATE_CHANGE_FAILURE;
}
