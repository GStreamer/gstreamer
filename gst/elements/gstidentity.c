/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
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
  ARG_0,
  ARG_LOOP_BASED,
  ARG_SLEEP_TIME,
  ARG_DUPLICATE,
  ARG_ERROR_AFTER,
  ARG_DROP_PROBABILITY,
  ARG_DATARATE,
  ARG_SILENT,
  ARG_LAST_MESSAGE,
  ARG_DUMP,
  ARG_SYNC,
  ARG_CHECK_PERFECT
};


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

static void gst_identity_chain (GstPad * pad, GstData * _data);
static void gst_identity_set_clock (GstElement * element, GstClock * clock);


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

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LOOP_BASED,
      g_param_spec_boolean ("loop-based", "Loop-based",
          "Set to TRUE to use loop-based rather than chain-based scheduling",
          DEFAULT_LOOP_BASED, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SLEEP_TIME,
      g_param_spec_uint ("sleep-time", "Sleep time",
          "Microseconds to sleep between processing", 0, G_MAXUINT,
          DEFAULT_SLEEP_TIME, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DUPLICATE,
      g_param_spec_uint ("duplicate", "Duplicate Buffers",
          "Push the buffers N times", 0, G_MAXUINT, DEFAULT_DUPLICATE,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ERROR_AFTER,
      g_param_spec_int ("error_after", "Error After", "Error after N buffers",
          G_MININT, G_MAXINT, DEFAULT_ERROR_AFTER, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DROP_PROBABILITY,
      g_param_spec_float ("drop_probability", "Drop Probability",
          "The Probability a buffer is dropped", 0.0, 1.0,
          DEFAULT_DROP_PROBABILITY, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DATARATE,
      g_param_spec_int ("datarate", "Datarate",
          "(Re)timestamps buffers with number of bytes per second (0 = inactive)",
          0, G_MAXINT, DEFAULT_DATARATE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SILENT,
      g_param_spec_boolean ("silent", "silent", "silent", DEFAULT_SILENT,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LAST_MESSAGE,
      g_param_spec_string ("last-message", "last-message", "last-message", NULL,
          G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DUMP,
      g_param_spec_boolean ("dump", "Dump", "Dump buffer contents",
          DEFAULT_DUMP, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SYNC,
      g_param_spec_boolean ("sync", "Synchronize",
          "Synchronize to pipeline clock", DEFAULT_SYNC, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_CHECK_PERFECT,
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
  gst_pad_set_chain_function (identity->sinkpad,
      GST_DEBUG_FUNCPTR (gst_identity_chain));
  //gst_pad_set_link_function (identity->sinkpad, gst_pad_proxy_pad_link);
  gst_pad_set_getcaps_function (identity->sinkpad, gst_pad_proxy_getcaps);

  identity->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&srctemplate),
      "src");
  gst_element_add_pad (GST_ELEMENT (identity), identity->srcpad);
  //gst_pad_set_link_function (identity->srcpad, gst_pad_proxy_pad_link);
  gst_pad_set_getcaps_function (identity->srcpad, gst_pad_proxy_getcaps);

  identity->loop_based = DEFAULT_LOOP_BASED;
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

  GST_FLAG_SET (identity, GST_ELEMENT_EVENT_AWARE);
}

static void
gst_identity_set_clock (GstElement * element, GstClock * clock)
{
  GstIdentity *identity = GST_IDENTITY (element);

  gst_object_replace ((GstObject **) & identity->clock, (GstObject *) clock);
}


static void
gst_identity_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstIdentity *identity;
  guint i;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  identity = GST_IDENTITY (gst_pad_get_parent (pad));

  if (GST_IS_EVENT (buf)) {
    GstEvent *event = GST_EVENT (buf);

    if (!identity->silent) {
      g_free (identity->last_message);

      identity->last_message =
          g_strdup_printf ("chain   ******* (%s:%s)E (type: %d) %p",
          GST_DEBUG_PAD_NAME (pad), GST_EVENT_TYPE (event), event);

      g_object_notify (G_OBJECT (identity), "last_message");
    }
    gst_pad_event_default (pad, event);
    return;
  }

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
      return;
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
      return;
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
      time = identity->bytes_handled * GST_SECOND / identity->datarate;

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
        gst_element_wait (GST_ELEMENT (identity), time);
      }
    }

    identity->bytes_handled += GST_BUFFER_SIZE (buf);
    gst_pad_push (identity->srcpad, GST_DATA (buf));

    if (identity->sleep_time)
      g_usleep (identity->sleep_time);
  }
}

static void
gst_identity_loop (GstElement * element)
{
  GstIdentity *identity;
  GstBuffer *buf;

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_IDENTITY (element));

  identity = GST_IDENTITY (element);

  buf = GST_BUFFER (gst_pad_pull (identity->sinkpad));
  if (GST_IS_EVENT (buf)) {
    GstEvent *event = GST_EVENT (buf);

    if (GST_EVENT_IS_INTERRUPT (event)) {
      gst_event_unref (event);
    } else {
      gst_pad_event_default (identity->sinkpad, event);
    }
  } else {
    gst_identity_chain (identity->sinkpad, GST_DATA (buf));
  }
}

static void
gst_identity_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstIdentity *identity;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_IDENTITY (object));

  identity = GST_IDENTITY (object);

  switch (prop_id) {
    case ARG_LOOP_BASED:
      identity->loop_based = g_value_get_boolean (value);
      if (identity->loop_based) {
        gst_element_set_loop_function (GST_ELEMENT (identity),
            gst_identity_loop);
        gst_pad_set_chain_function (identity->sinkpad, NULL);
      } else {
        gst_pad_set_chain_function (identity->sinkpad, gst_identity_chain);
        gst_element_set_loop_function (GST_ELEMENT (identity), NULL);
      }
      break;
    case ARG_SLEEP_TIME:
      identity->sleep_time = g_value_get_uint (value);
      break;
    case ARG_SILENT:
      identity->silent = g_value_get_boolean (value);
      break;
    case ARG_DUPLICATE:
      identity->duplicate = g_value_get_uint (value);
      break;
    case ARG_DUMP:
      identity->dump = g_value_get_boolean (value);
      break;
    case ARG_ERROR_AFTER:
      identity->error_after = g_value_get_int (value);
      break;
    case ARG_DROP_PROBABILITY:
      identity->drop_probability = g_value_get_float (value);
      break;
    case ARG_DATARATE:
      identity->datarate = g_value_get_int (value);
      break;
    case ARG_SYNC:
      identity->sync = g_value_get_boolean (value);
      break;
    case ARG_CHECK_PERFECT:
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

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_IDENTITY (object));

  identity = GST_IDENTITY (object);

  switch (prop_id) {
    case ARG_LOOP_BASED:
      g_value_set_boolean (value, identity->loop_based);
      break;
    case ARG_SLEEP_TIME:
      g_value_set_uint (value, identity->sleep_time);
      break;
    case ARG_DUPLICATE:
      g_value_set_uint (value, identity->duplicate);
      break;
    case ARG_ERROR_AFTER:
      g_value_set_int (value, identity->error_after);
      break;
    case ARG_DROP_PROBABILITY:
      g_value_set_float (value, identity->drop_probability);
      break;
    case ARG_DATARATE:
      g_value_set_int (value, identity->datarate);
      break;
    case ARG_SILENT:
      g_value_set_boolean (value, identity->silent);
      break;
    case ARG_DUMP:
      g_value_set_boolean (value, identity->dump);
      break;
    case ARG_LAST_MESSAGE:
      g_value_set_string (value, identity->last_message);
      break;
    case ARG_SYNC:
      g_value_set_boolean (value, identity->sync);
      break;
    case ARG_CHECK_PERFECT:
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
      identity->bytes_handled = 0;
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
