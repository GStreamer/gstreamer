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

#include "../../gst/gst-i18n-lib.h"
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

static GstElementDetails gst_identity_details = GST_ELEMENT_DETAILS ("Identity",
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

#define DEFAULT_SLEEP_TIME		0
#define DEFAULT_DUPLICATE		1
#define DEFAULT_ERROR_AFTER		-1
#define DEFAULT_DROP_PROBABILITY	0.0
#define DEFAULT_DATARATE		0
#define DEFAULT_SILENT			FALSE
#define DEFAULT_SINGLE_SEGMENT		FALSE
#define DEFAULT_DUMP			FALSE
#define DEFAULT_SYNC			FALSE
#define DEFAULT_CHECK_PERFECT		FALSE

enum
{
  PROP_0,
  PROP_SLEEP_TIME,
  PROP_ERROR_AFTER,
  PROP_DROP_PROBABILITY,
  PROP_DATARATE,
  PROP_SILENT,
  PROP_SINGLE_SEGMENT,
  PROP_LAST_MESSAGE,
  PROP_DUMP,
  PROP_SYNC,
  PROP_CHECK_PERFECT
};


#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_identity_debug, "identity", 0, "identity element");

GST_BOILERPLATE_FULL (GstIdentity, gst_identity, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, _do_init);

static void gst_identity_finalize (GObject * object);
static void gst_identity_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_identity_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_identity_event (GstBaseTransform * trans, GstEvent * event);
static GstFlowReturn gst_identity_transform_ip (GstBaseTransform * trans,
    GstBuffer * buf);
static gboolean gst_identity_start (GstBaseTransform * trans);
static gboolean gst_identity_stop (GstBaseTransform * trans);

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

/* fixme: do something about this */
static void
marshal_VOID__MINIOBJECT (GClosure * closure, GValue * return_value,
    guint n_param_values, const GValue * param_values, gpointer invocation_hint,
    gpointer marshal_data)
{
  typedef void (*marshalfunc_VOID__MINIOBJECT) (gpointer obj, gpointer arg1,
      gpointer data2);
  register marshalfunc_VOID__MINIOBJECT callback;
  register GCClosure *cc = (GCClosure *) closure;
  register gpointer data1, data2;

  g_return_if_fail (n_param_values == 2);

  if (G_CCLOSURE_SWAP_DATA (closure)) {
    data1 = closure->data;
    data2 = g_value_peek_pointer (param_values + 0);
  } else {
    data1 = g_value_peek_pointer (param_values + 0);
    data2 = closure->data;
  }
  callback =
      (marshalfunc_VOID__MINIOBJECT) (marshal_data ? marshal_data : cc->
      callback);

  callback (data1, gst_value_get_mini_object (param_values + 1), data2);
}

static void
gst_identity_class_init (GstIdentityClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseTransformClass *gstbasetrans_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  gstbasetrans_class = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_identity_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_identity_get_property);

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SLEEP_TIME,
      g_param_spec_uint ("sleep-time", "Sleep time",
          "Microseconds to sleep between processing", 0, G_MAXUINT,
          DEFAULT_SLEEP_TIME, G_PARAM_READWRITE));
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
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SINGLE_SEGMENT,
      g_param_spec_boolean ("single-segment", "Single Segment",
          "Timestamp buffers and eat newsegments so as to appear as one segment",
          DEFAULT_SINGLE_SEGMENT, G_PARAM_READWRITE));
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
      marshal_VOID__MINIOBJECT, G_TYPE_NONE, 1, GST_TYPE_BUFFER);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_identity_finalize);

  gstbasetrans_class->event = GST_DEBUG_FUNCPTR (gst_identity_event);
  gstbasetrans_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_identity_transform_ip);
  gstbasetrans_class->start = GST_DEBUG_FUNCPTR (gst_identity_start);
  gstbasetrans_class->stop = GST_DEBUG_FUNCPTR (gst_identity_stop);
}

static void
gst_identity_init (GstIdentity * identity, GstIdentityClass * g_class)
{
  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (identity), TRUE);

  identity->sleep_time = DEFAULT_SLEEP_TIME;
  identity->error_after = DEFAULT_ERROR_AFTER;
  identity->drop_probability = DEFAULT_DROP_PROBABILITY;
  identity->datarate = DEFAULT_DATARATE;
  identity->silent = DEFAULT_SILENT;
  identity->single_segment = DEFAULT_SINGLE_SEGMENT;
  identity->sync = DEFAULT_SYNC;
  identity->check_perfect = DEFAULT_CHECK_PERFECT;
  identity->dump = DEFAULT_DUMP;
  identity->last_message = NULL;
}

static gboolean
gst_identity_event (GstBaseTransform * trans, GstEvent * event)
{
  GstIdentity *identity;
  gboolean ret = TRUE;

  identity = GST_IDENTITY (trans);

  if (!identity->silent) {
    const GstStructure *s;
    gchar *sstr;

    GST_OBJECT_LOCK (identity);
    g_free (identity->last_message);

    if ((s = gst_event_get_structure (event)))
      sstr = gst_structure_to_string (s);
    else
      sstr = g_strdup ("");

    identity->last_message =
        g_strdup_printf ("event   ******* (%s:%s) E (type: %d, %s) %p",
        GST_DEBUG_PAD_NAME (trans->sinkpad), GST_EVENT_TYPE (event), sstr,
        event);
    g_free (sstr);
    GST_OBJECT_UNLOCK (identity);

    g_object_notify (G_OBJECT (identity), "last_message");
  }

  if (identity->single_segment
      && (GST_EVENT_TYPE (event) == GST_EVENT_NEWSEGMENT)) {
    if (trans->have_newsegment == FALSE) {
      GstEvent *news;
      GstFormat format;

      gst_event_parse_new_segment (event, NULL, NULL, &format, NULL, NULL,
          NULL);

      /* This is the first newsegment, send out a (0, -1) newsegment */
      news = gst_event_new_new_segment (TRUE, 1.0, format, 0, -1, 0);

      if (!(gst_pad_event_default (trans->sinkpad, news)))
        return FALSE;
    }
  }

  GST_BASE_TRANSFORM_CLASS (parent_class)->event (trans, event);

  if (identity->single_segment
      && (GST_EVENT_TYPE (event) == GST_EVENT_NEWSEGMENT)) {
    /* eat up segments */
    ret = FALSE;
  }

  return ret;
}

static void
gst_identity_check_perfect (GstIdentity * identity, GstBuffer * buf)
{
  GstClockTime timestamp;

  timestamp = GST_BUFFER_TIMESTAMP (buf);

  /* see if we need to do perfect stream checking */
  /* invalid timestamp drops us out of check.  FIXME: maybe warn ? */
  if (timestamp != GST_CLOCK_TIME_NONE) {
    /* check if we had a previous buffer to compare to */
    if (identity->prev_timestamp != GST_CLOCK_TIME_NONE) {
      guint64 offset;

      if (identity->prev_timestamp + identity->prev_duration != timestamp) {
        GST_WARNING_OBJECT (identity,
            "Buffer not time-contiguous with previous one: " "prev ts %"
            GST_TIME_FORMAT ", prev dur %" GST_TIME_FORMAT ", new ts %"
            GST_TIME_FORMAT, GST_TIME_ARGS (identity->prev_timestamp),
            GST_TIME_ARGS (identity->prev_duration), GST_TIME_ARGS (timestamp));
      }

      offset = GST_BUFFER_OFFSET (buf);
      if (identity->prev_offset_end != offset) {
        GST_WARNING_OBJECT (identity,
            "Buffer not data-contiguous with previous one: "
            "prev offset_end %" G_GINT64_FORMAT ", new offset %"
            G_GINT64_FORMAT, identity->prev_offset_end, offset);
      }
    }
    /* update prev values */
    identity->prev_timestamp = timestamp;
    identity->prev_duration = GST_BUFFER_DURATION (buf);
    identity->prev_offset_end = GST_BUFFER_OFFSET_END (buf);
  }
}

static GstFlowReturn
gst_identity_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstIdentity *identity = GST_IDENTITY (trans);
  GstClockTime runtimestamp = G_GINT64_CONSTANT (0);

  if (identity->check_perfect)
    gst_identity_check_perfect (identity, buf);

  if (identity->error_after >= 0) {
    identity->error_after--;
    if (identity->error_after == 0) {
      GST_ELEMENT_ERROR (identity, CORE, FAILED,
          (_("Failed after iterations as requested.")), (NULL));
      return GST_FLOW_ERROR;
    }
  }

  if (identity->drop_probability > 0.0) {
    if ((gfloat) (1.0 * rand () / (RAND_MAX)) < identity->drop_probability) {
      GST_OBJECT_LOCK (identity);
      g_free (identity->last_message);
      identity->last_message =
          g_strdup_printf ("dropping   ******* (%s:%s)i (%d bytes, timestamp: %"
          GST_TIME_FORMAT ", duration: %" GST_TIME_FORMAT ", offset: %"
          G_GINT64_FORMAT ", offset_end: % " G_GINT64_FORMAT ", flags: %d) %p",
          GST_DEBUG_PAD_NAME (trans->sinkpad), GST_BUFFER_SIZE (buf),
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
          GST_TIME_ARGS (GST_BUFFER_DURATION (buf)),
          GST_BUFFER_OFFSET (buf), GST_BUFFER_OFFSET_END (buf),
          GST_BUFFER_FLAGS (buf), buf);
      GST_OBJECT_UNLOCK (identity);
      g_object_notify (G_OBJECT (identity), "last-message");
      return GST_FLOW_OK;
    }
  }

  if (identity->dump) {
    gst_util_dump_mem (GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
  }

  if (!identity->silent) {
    GST_OBJECT_LOCK (identity);
    g_free (identity->last_message);
    identity->last_message =
        g_strdup_printf ("chain   ******* (%s:%s)i (%d bytes, timestamp: %"
        GST_TIME_FORMAT ", duration: %" GST_TIME_FORMAT ", offset: %"
        G_GINT64_FORMAT ", offset_end: % " G_GINT64_FORMAT ", flags: %d) %p",
        GST_DEBUG_PAD_NAME (trans->sinkpad), GST_BUFFER_SIZE (buf),
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (buf)),
        GST_BUFFER_OFFSET (buf), GST_BUFFER_OFFSET_END (buf),
        GST_BUFFER_FLAGS (buf), buf);
    GST_OBJECT_UNLOCK (identity);
    g_object_notify (G_OBJECT (identity), "last-message");
  }

  if (identity->datarate > 0) {
    GstClockTime time = identity->offset * GST_SECOND / identity->datarate;

    GST_BUFFER_TIMESTAMP (buf) = time;
    GST_BUFFER_DURATION (buf) =
        GST_BUFFER_SIZE (buf) * GST_SECOND / identity->datarate;
  }

  g_signal_emit (G_OBJECT (identity), gst_identity_signals[SIGNAL_HANDOFF], 0,
      buf);

  if (trans->segment.format == GST_FORMAT_TIME)
    runtimestamp = gst_segment_to_running_time (&trans->segment,
        GST_FORMAT_TIME, GST_BUFFER_TIMESTAMP (buf));

  if ((identity->sync) && (trans->segment.format == GST_FORMAT_TIME)) {
    GstClock *clock;

    GST_OBJECT_LOCK (identity);
    if ((clock = GST_ELEMENT (identity)->clock)) {
      GstClockReturn cret;
      GstClockTime timestamp;

      timestamp = runtimestamp + GST_ELEMENT (identity)->base_time;

      /* save id if we need to unlock */
      /* FIXME: actually unlock this somewhere in the state changes */
      identity->clock_id = gst_clock_new_single_shot_id (clock, timestamp);
      GST_OBJECT_UNLOCK (identity);

      cret = gst_clock_id_wait (identity->clock_id, NULL);

      GST_OBJECT_LOCK (identity);
      if (identity->clock_id) {
        gst_clock_id_unref (identity->clock_id);
        identity->clock_id = NULL;
      }
      if (cret == GST_CLOCK_UNSCHEDULED)
        ret = GST_FLOW_UNEXPECTED;
    }
    GST_OBJECT_UNLOCK (identity);
  }

  identity->offset += GST_BUFFER_SIZE (buf);

  if (identity->sleep_time && ret == GST_FLOW_OK)
    g_usleep (identity->sleep_time);

  if (identity->single_segment && (trans->segment.format == GST_FORMAT_TIME)
      && (ret == GST_FLOW_OK))
    GST_BUFFER_TIMESTAMP (buf) = runtimestamp;

  return ret;
}

static void
gst_identity_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstIdentity *identity;

  identity = GST_IDENTITY (object);

  switch (prop_id) {
    case PROP_SLEEP_TIME:
      identity->sleep_time = g_value_get_uint (value);
      break;
    case PROP_SILENT:
      identity->silent = g_value_get_boolean (value);
      break;
    case PROP_SINGLE_SEGMENT:
      identity->single_segment = g_value_get_boolean (value);
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
    case PROP_SLEEP_TIME:
      g_value_set_uint (value, identity->sleep_time);
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
    case PROP_SINGLE_SEGMENT:
      g_value_set_boolean (value, identity->single_segment);
      break;
    case PROP_DUMP:
      g_value_set_boolean (value, identity->dump);
      break;
    case PROP_LAST_MESSAGE:
      GST_OBJECT_LOCK (identity);
      g_value_set_string (value, identity->last_message);
      GST_OBJECT_UNLOCK (identity);
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

static gboolean
gst_identity_start (GstBaseTransform * trans)
{
  GstIdentity *identity;

  identity = GST_IDENTITY (trans);

  identity->offset = 0;
  identity->prev_timestamp = GST_CLOCK_TIME_NONE;
  identity->prev_duration = GST_CLOCK_TIME_NONE;
  identity->prev_offset_end = -1;

  return TRUE;
}

static gboolean
gst_identity_stop (GstBaseTransform * trans)
{
  GstIdentity *identity;

  identity = GST_IDENTITY (trans);

  GST_OBJECT_LOCK (identity);
  g_free (identity->last_message);
  identity->last_message = NULL;
  GST_OBJECT_UNLOCK (identity);

  return TRUE;
}
