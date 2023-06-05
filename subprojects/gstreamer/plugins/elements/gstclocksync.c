/*
 * GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2005 Wim Taymans <wim@fluendo.com>
 * Copyright (C) 2020 Jan Schmidt <jan@centricular.com>
 *
 * gstclocksync.c:
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-clocksync
 * @title: clocksync
 *
 * Simple element that passes all buffers and buffer-lists intact, but
 * synchronising them to the clock before passing.
 *
 * Synchronisation to the clock is on by default, but can be turned
 * off by setting the 'sync' property to FALSE
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m videotestsrc ! clocksync ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "gstclocksync.h"
#include "gstcoreelementselements.h"

GST_DEBUG_CATEGORY_STATIC (gst_clock_sync_debug);
#define GST_CAT_DEFAULT gst_clock_sync_debug

/* ClockSync args */
#define DEFAULT_SYNC                    TRUE
#define DEFAULT_TS_OFFSET               0
#define DEFAULT_SYNC_TO_FIRST           FALSE
#define DEFAULT_QOS                     FALSE

enum
{
  PROP_0,
  PROP_SYNC,
  PROP_TS_OFFSET,
  PROP_SYNC_TO_FIRST,
  PROP_QOS,
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

#define _do_init \
    GST_DEBUG_CATEGORY_INIT (gst_clock_sync_debug, "clocksync", 0, "clocksync element");
#define gst_clock_sync_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstClockSync, gst_clock_sync, GST_TYPE_ELEMENT,
    _do_init);
GST_ELEMENT_REGISTER_DEFINE (clocksync, "clocksync", GST_RANK_NONE,
    GST_TYPE_CLOCKSYNC);

#define DO_RUNNING_AVG(avg,val,size) (((val) + ((size)-1) * (avg)) / (size))

/* generic running average, this has a neutral window size */
#define UPDATE_RUNNING_AVG(avg,val)   DO_RUNNING_AVG(avg,val,8)

/* the windows for these running averages are experimentally obtained.
 * positive values get averaged more while negative values use a small
 * window so we can react faster to badness. */
#define UPDATE_RUNNING_AVG_P(avg,val) DO_RUNNING_AVG(avg,val,16)
#define UPDATE_RUNNING_AVG_N(avg,val) DO_RUNNING_AVG(avg,val,4)

static void gst_clock_sync_finalize (GObject * object);

static void gst_clock_sync_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_clock_sync_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_clock_sync_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static GstFlowReturn gst_clock_sync_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf);
static GstFlowReturn gst_clock_sync_chain_list (GstPad * pad,
    GstObject * parent, GstBufferList * buflist);
static gboolean gst_clock_sync_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static gboolean gst_clock_sync_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static GstStateChangeReturn gst_clocksync_change_state (GstElement * element,
    GstStateChange transition);
static GstClock *gst_clocksync_provide_clock (GstElement * element);

static void
gst_clock_sync_class_init (GstClockSyncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_clock_sync_set_property;
  gobject_class->get_property = gst_clock_sync_get_property;
  gobject_class->finalize = gst_clock_sync_finalize;

  properties[PROP_SYNC] =
      g_param_spec_boolean ("sync", "Synchronize",
      "Synchronize to pipeline clock", DEFAULT_SYNC,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  properties[PROP_TS_OFFSET] =
      g_param_spec_int64 ("ts-offset", "Timestamp offset for synchronisation",
      "Timestamp offset in nanoseconds for synchronisation, negative for earlier sync",
      G_MININT64, G_MAXINT64, DEFAULT_TS_OFFSET,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GstClockSync:sync-to-first:
   *
   * When enabled, clocksync elemenet will adjust "ts-offset" value
   * automatically by using given timestamp of the first buffer and running
   * time of pipeline, so that clocksync element can output the first buffer
   * immediately without clock waiting.
   *
   * Since: 1.20
   */
  properties[PROP_SYNC_TO_FIRST] =
      g_param_spec_boolean ("sync-to-first",
      "Sync to first",
      "Automatically set ts-offset based on running time of the first "
      "buffer and pipeline's running time "
      "(i.e., ts-offset = \"pipeline running time\" - \"buffer running time\"). "
      "When enabled, clocksync element will update ts-offset on the first "
      "buffer per flush event or READY to PAUSED state change. "
      "This property can be useful in case that buffer timestamp does not "
      "necessarily have to be synchronized with pipeline's running time, "
      "but duration of the buffer through clocksync element needs to be "
      "synchronized with the amount of clock time go. "
      "Note that mixed use of ts-offset and this property would be racy "
      "if clocksync element is running already.",
      DEFAULT_SYNC_TO_FIRST, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GstClockSync:qos:
   *
   * Generate Quality-of-Service events upstream.
   *
   * Since: 1.22
   */
  properties[PROP_QOS] =
      g_param_spec_boolean ("qos", "Qos",
      "Generate Quality-of-Service events upstream", DEFAULT_QOS,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, PROP_LAST, properties);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_clocksync_change_state);
  gstelement_class->provide_clock =
      GST_DEBUG_FUNCPTR (gst_clocksync_provide_clock);

  gst_element_class_set_static_metadata (gstelement_class,
      "ClockSync",
      "Generic",
      "Synchronise buffers to the clock", "Jan Schmidt <jan@centricular.com>");

  gst_element_class_add_static_pad_template (gstelement_class, &srctemplate);
  gst_element_class_add_static_pad_template (gstelement_class, &sinktemplate);
}

static void
gst_clock_sync_finalize (GObject * object)
{
  GstClockSync *clocksync = GST_CLOCKSYNC (object);

  g_cond_clear (&clocksync->blocked_cond);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_clock_sync_init (GstClockSync * clocksync)
{
  clocksync->sinkpad = gst_pad_new_from_static_template (&sinktemplate, "sink");
  gst_pad_set_event_function (clocksync->sinkpad,
      GST_DEBUG_FUNCPTR (gst_clock_sync_sink_event));
  gst_pad_set_chain_function (clocksync->sinkpad,
      GST_DEBUG_FUNCPTR (gst_clock_sync_chain));
  gst_pad_set_chain_list_function (clocksync->sinkpad,
      GST_DEBUG_FUNCPTR (gst_clock_sync_chain_list));
  GST_PAD_SET_PROXY_CAPS (clocksync->sinkpad);
  gst_element_add_pad (GST_ELEMENT (clocksync), clocksync->sinkpad);

  clocksync->srcpad = gst_pad_new_from_static_template (&srctemplate, "src");

  gst_pad_set_query_function (clocksync->srcpad, gst_clock_sync_src_query);

  GST_PAD_SET_PROXY_CAPS (clocksync->srcpad);
  gst_pad_set_event_function (clocksync->srcpad,
      GST_DEBUG_FUNCPTR (gst_clock_sync_src_event));
  gst_element_add_pad (GST_ELEMENT (clocksync), clocksync->srcpad);

  clocksync->ts_offset = DEFAULT_TS_OFFSET;
  clocksync->sync = DEFAULT_SYNC;
  clocksync->sync_to_first = DEFAULT_SYNC_TO_FIRST;

  g_atomic_int_set (&clocksync->qos_enabled, DEFAULT_QOS);
  g_cond_init (&clocksync->blocked_cond);

  GST_OBJECT_FLAG_SET (clocksync, GST_ELEMENT_FLAG_PROVIDE_CLOCK);
  GST_OBJECT_FLAG_SET (clocksync, GST_ELEMENT_FLAG_REQUIRE_CLOCK);
}

static void
gst_clock_sync_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstClockSync *clocksync = GST_CLOCKSYNC (object);
  GstMessage *clock_message = NULL;
  gboolean sync;

  switch (prop_id) {
    case PROP_SYNC:
      clocksync->sync = g_value_get_boolean (value);
      sync = g_value_get_boolean (value);
      GST_OBJECT_LOCK (clocksync);
      if (sync != clocksync->sync) {
        clocksync->sync = sync;
        if (sync) {
          GST_OBJECT_FLAG_SET (clocksync, GST_ELEMENT_FLAG_PROVIDE_CLOCK);
          clock_message =
              gst_message_new_clock_provide (GST_OBJECT_CAST (clocksync),
              gst_system_clock_obtain (), TRUE);
        } else {
          GST_OBJECT_FLAG_UNSET (clocksync, GST_ELEMENT_FLAG_PROVIDE_CLOCK);
          clock_message =
              gst_message_new_clock_lost (GST_OBJECT_CAST (clocksync),
              gst_system_clock_obtain ());
        }
      }
      GST_OBJECT_UNLOCK (clocksync);
      if (clock_message)
        gst_element_post_message (GST_ELEMENT_CAST (clocksync), clock_message);
      break;
    case PROP_TS_OFFSET:
      clocksync->ts_offset = g_value_get_int64 (value);
      break;
    case PROP_SYNC_TO_FIRST:
      clocksync->sync_to_first = g_value_get_boolean (value);
      break;
    case PROP_QOS:
      g_atomic_int_set (&clocksync->qos_enabled, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_clock_sync_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstClockSync *clocksync = GST_CLOCKSYNC (object);

  switch (prop_id) {
    case PROP_SYNC:
      g_value_set_boolean (value, clocksync->sync);
      break;
    case PROP_TS_OFFSET:
      g_value_set_int64 (value, clocksync->ts_offset);
      break;
    case PROP_SYNC_TO_FIRST:
      g_value_set_boolean (value, clocksync->sync_to_first);
      break;
    case PROP_QOS:
      g_value_set_boolean (value, g_atomic_int_get (&clocksync->qos_enabled));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* With STREAM_LOCK
 * reset all qos measuring */
static void
gst_clock_sync_reset_qos (GstClockSync * clocksync)
{
  clocksync->prev_rstart = GST_CLOCK_TIME_NONE;
  clocksync->earliest_in_time = GST_CLOCK_TIME_NONE;
  clocksync->last_left = GST_CLOCK_TIME_NONE;
  clocksync->avg_pt = GST_CLOCK_TIME_NONE;
  clocksync->avg_rate = -1.0;
  clocksync->avg_in_diff = GST_CLOCK_TIME_NONE;

}

static gboolean
gst_clock_sync_send_qos (GstClockSync * clocksync, GstQOSType type,
    gdouble proportion, GstClockTime time, GstClockTimeDiff diff)
{
  GstEvent *event;
  gboolean res;

  /* generate Quality-of-Service event */
  GST_DEBUG_OBJECT (clocksync,
      "qos: type %d, proportion: %lf, diff %" G_GINT64_FORMAT ", timestamp %"
      GST_TIME_FORMAT, type, proportion, diff, GST_TIME_ARGS (time));

  event = gst_event_new_qos (type, proportion, diff, time);

  /* send upstream */
  res = gst_pad_push_event (clocksync->sinkpad, event);

  return res;
}

static gboolean
gst_clock_sync_perform_qos (GstClockSync * clocksync)
{
  GstClockTime start;
  GstClockTimeDiff jitter;
  GstClockTime pt, entered;
  GstClockTime duration;
  gdouble rate;

  start = clocksync->current_rstart;

  /* if Quality-of-Service disabled, do nothing */
  if (!g_atomic_int_get (&clocksync->qos_enabled) ||
      !GST_CLOCK_TIME_IS_VALID (start) || !clocksync->sync) {

    return FALSE;
  }

  jitter = clocksync->current_jitter;

  if (jitter < 0) {
    /* this is the time the buffer entered the clocksync */
    if (start < -jitter)
      entered = 0;
    else
      entered = start + jitter;
  } else {
    /* this is the time the buffer entered the clocksync */
    entered = start + jitter;
  }

  /* Average duration between each buffer timestamps */
  duration = clocksync->avg_in_diff;

  /* if we have the time when the last buffer left us, calculate
   * processing time */
  if (GST_CLOCK_TIME_IS_VALID (clocksync->last_left)) {
    if (entered > clocksync->last_left) {
      pt = entered - clocksync->last_left;
    } else {
      pt = 0;
    }
  } else {
    pt = clocksync->avg_pt;
  }

  GST_DEBUG_OBJECT (clocksync, "start: %" GST_TIME_FORMAT
      ", entered %" GST_TIME_FORMAT ", pt: %" GST_TIME_FORMAT ", duration %"
      GST_TIME_FORMAT ",jitter %" G_GINT64_FORMAT, GST_TIME_ARGS (start),
      GST_TIME_ARGS (entered), GST_TIME_ARGS (pt), GST_TIME_ARGS (duration),
      jitter);

  GST_DEBUG_OBJECT (clocksync,
      "avg_pt: %" GST_TIME_FORMAT ", avg_rate: %g",
      GST_TIME_ARGS (clocksync->avg_pt), clocksync->avg_rate);

  /* collect running averages. for first observations, we copy the
   * values */
  if (!GST_CLOCK_TIME_IS_VALID (clocksync->avg_pt))
    clocksync->avg_pt = pt;
  else
    clocksync->avg_pt = UPDATE_RUNNING_AVG (clocksync->avg_pt, pt);

  if (duration != -1 && duration != 0) {
    rate =
        gst_guint64_to_gdouble (clocksync->avg_pt) /
        gst_guint64_to_gdouble (duration);
  } else {
    rate = 1.0;
  }

  if (GST_CLOCK_TIME_IS_VALID (clocksync->last_left)) {
    if (clocksync->avg_rate < 0.0) {
      clocksync->avg_rate = rate;
    } else {
      if (rate > 1.0)
        clocksync->avg_rate = UPDATE_RUNNING_AVG_N (clocksync->avg_rate, rate);
      else
        clocksync->avg_rate = UPDATE_RUNNING_AVG_P (clocksync->avg_rate, rate);
    }
  }

  GST_DEBUG_OBJECT (clocksync,
      "updated: avg_pt: %" GST_TIME_FORMAT
      ", avg_rate: %g", GST_TIME_ARGS (clocksync->avg_pt), clocksync->avg_rate);

  if (clocksync->avg_rate >= 0.0) {
    GstQOSType type;
    GstClockTimeDiff diff;

    /* if we have a valid rate, start sending QoS messages */
    if (clocksync->current_jitter < 0) {
      /* make sure we never go below 0 when adding the jitter to the
       * timestamp. */
      if (clocksync->current_rstart < -clocksync->current_jitter)
        clocksync->current_jitter = -clocksync->current_rstart;
    }

    diff = clocksync->current_jitter;
    if (diff <= 0)
      type = GST_QOS_TYPE_OVERFLOW;
    else
      type = GST_QOS_TYPE_UNDERFLOW;

    gst_clock_sync_send_qos (clocksync, type, clocksync->avg_rate,
        clocksync->current_rstart, diff);
  }

  return TRUE;
}

static GstFlowReturn
gst_clocksync_do_sync (GstClockSync * clocksync, GstClockTime running_time)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstClock *clock;

  clocksync->current_rstart = GST_CLOCK_TIME_NONE;

  if (!clocksync->sync)
    return GST_FLOW_OK;

  if (running_time == GST_CLOCK_TIME_NONE)
    return GST_FLOW_OK;         /* Can't sync on an invalid time either way */

  if (clocksync->segment.format != GST_FORMAT_TIME)
    return GST_FLOW_OK;

  GST_OBJECT_LOCK (clocksync);

  if (clocksync->flushing) {
    GST_OBJECT_UNLOCK (clocksync);
    return GST_FLOW_FLUSHING;
  }

  while (clocksync->blocked && !clocksync->flushing)
    g_cond_wait (&clocksync->blocked_cond, GST_OBJECT_GET_LOCK (clocksync));

  if (clocksync->flushing) {
    GST_OBJECT_UNLOCK (clocksync);
    return GST_FLOW_FLUSHING;
  }

  if ((clock = GST_ELEMENT (clocksync)->clock)) {
    GstClockReturn cret;
    GstClockTime timestamp;
    GstClockTimeDiff ts_offset = clocksync->ts_offset;
    GstClockTimeDiff jitter;

    timestamp = running_time + GST_ELEMENT (clocksync)->base_time +
        clocksync->upstream_latency;

    GST_DEBUG_OBJECT (clocksync,
        "running time: %" GST_TIME_FORMAT " base time: %" GST_TIME_FORMAT
        " upstream latency: %" GST_TIME_FORMAT, GST_TIME_ARGS (running_time),
        GST_TIME_ARGS (GST_ELEMENT (clocksync)->base_time),
        GST_TIME_ARGS (clocksync->upstream_latency));

    GST_DEBUG_OBJECT (clocksync,
        "Waiting for clock time %" GST_TIME_FORMAT " ts offset: %"
        GST_STIME_FORMAT, GST_TIME_ARGS (timestamp),
        GST_STIME_ARGS (ts_offset));

    if (ts_offset < 0) {
      ts_offset = -ts_offset;
      if (ts_offset < timestamp)
        timestamp -= ts_offset;
      else
        timestamp = 0;
    } else {
      timestamp += ts_offset;
    }

    GST_DEBUG_OBJECT (clocksync, "Offset clock time %" GST_TIME_FORMAT,
        GST_TIME_ARGS (timestamp));

    /* save id if we need to unlock */
    clocksync->clock_id = gst_clock_new_single_shot_id (clock, timestamp);
    GST_OBJECT_UNLOCK (clocksync);

    cret = gst_clock_id_wait (clocksync->clock_id, &jitter);

    GST_DEBUG_OBJECT (clocksync, "Clock returned %d, jitter %" GST_STIME_FORMAT,
        cret, GST_STIME_ARGS (jitter));

    GST_OBJECT_LOCK (clocksync);
    if (clocksync->clock_id) {
      gst_clock_id_unref (clocksync->clock_id);
      clocksync->clock_id = NULL;
    }
    if (cret == GST_CLOCK_UNSCHEDULED || clocksync->flushing)
      ret = GST_FLOW_FLUSHING;

    clocksync->current_jitter = jitter;
  }

  clocksync->current_rstart = running_time;
  /* calculate inter frame spacing */
  if (G_UNLIKELY (GST_CLOCK_TIME_IS_VALID (clocksync->prev_rstart) &&
          clocksync->prev_rstart < running_time)) {
    GstClockTime in_diff;

    in_diff = running_time - clocksync->prev_rstart;

    if (clocksync->avg_in_diff == -1)
      clocksync->avg_in_diff = in_diff;
    else
      clocksync->avg_in_diff =
          UPDATE_RUNNING_AVG (clocksync->avg_in_diff, in_diff);

    GST_LOG_OBJECT (clocksync, "avg frame diff %" GST_TIME_FORMAT,
        GST_TIME_ARGS (clocksync->avg_in_diff));
  }
  clocksync->prev_rstart = running_time;

  GST_OBJECT_UNLOCK (clocksync);

  return ret;
}

static gboolean
gst_clock_sync_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstClockSync *clocksync = GST_CLOCKSYNC (parent);
  gboolean ret;

  GST_LOG_OBJECT (clocksync, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
      /* store the event for synching */
      gst_event_copy_segment (event, &clocksync->segment);

      gst_clock_sync_reset_qos (clocksync);
      break;
    case GST_EVENT_GAP:
    {
      GstClockTime start, dur;

      if (clocksync->segment.format != GST_FORMAT_TIME)
        break;

      gst_event_parse_gap (event, &start, &dur);
      if (GST_CLOCK_TIME_IS_VALID (start)) {
        start = gst_segment_to_running_time (&clocksync->segment,
            GST_FORMAT_TIME, start);

        gst_clocksync_do_sync (clocksync, start);
      }
      break;
    }
    case GST_EVENT_FLUSH_START:
      GST_OBJECT_LOCK (clocksync);
      clocksync->flushing = TRUE;
      g_cond_signal (&clocksync->blocked_cond);
      if (clocksync->clock_id) {
        GST_DEBUG_OBJECT (clocksync, "unlock clock wait");
        gst_clock_id_unschedule (clocksync->clock_id);
      }
      GST_OBJECT_UNLOCK (clocksync);
      break;
    case GST_EVENT_FLUSH_STOP:
      GST_OBJECT_LOCK (clocksync);
      clocksync->flushing = FALSE;
      gst_segment_init (&clocksync->segment, GST_FORMAT_UNDEFINED);
      GST_OBJECT_UNLOCK (clocksync);
      gst_clock_sync_reset_qos (clocksync);
      clocksync->is_first = TRUE;
      break;
    default:
      break;
  }

  /* Always handle all events as normal: */
  ret = gst_pad_event_default (pad, parent, event);
  return ret;
}

static gboolean
gst_clock_sync_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstClockSync *clocksync = GST_CLOCKSYNC (parent);

  GST_LOG_OBJECT (clocksync, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:
      if (g_atomic_int_get (&clocksync->qos_enabled)) {
        GST_LOG_OBJECT (clocksync,
            "Dropping downstream QoS event as we are responsible for handling QoS");
        gst_event_unref (event);
        return TRUE;
      }
      break;
    default:
      break;
  }

  /* Always handle all events as normal: */
  return gst_pad_event_default (pad, parent, event);
}

static void
gst_clock_sync_update_ts_offset (GstClockSync * clocksync,
    GstClockTime runtimestamp)
{
  GstClock *clock;
  GstClockTimeDiff ts_offset = 0;
  GstClockTime running_time;

  if (!clocksync->sync_to_first || !clocksync->is_first || !clocksync->sync)
    return;

  GST_OBJECT_LOCK (clocksync);
  clock = GST_ELEMENT_CLOCK (clocksync);
  if (!clock) {
    GST_DEBUG_OBJECT (clocksync, "We have no clock");
    GST_OBJECT_UNLOCK (clocksync);
    return;
  }

  running_time = gst_clock_get_time (clock) -
      GST_ELEMENT_CAST (clocksync)->base_time;
  ts_offset = GST_CLOCK_DIFF (runtimestamp, running_time);
  GST_OBJECT_UNLOCK (clocksync);

  GST_DEBUG_OBJECT (clocksync, "Running time %" GST_TIME_FORMAT
      ", running time stamp %" GST_TIME_FORMAT ", calculated ts-offset %"
      GST_STIME_FORMAT, GST_TIME_ARGS (running_time),
      GST_TIME_ARGS (runtimestamp), GST_STIME_ARGS (ts_offset));

  clocksync->is_first = FALSE;
  if (ts_offset != clocksync->ts_offset) {
    clocksync->ts_offset = ts_offset;
    g_object_notify_by_pspec (G_OBJECT (clocksync), properties[PROP_TS_OFFSET]);
  }
}

static GstFlowReturn
gst_clock_sync_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  gboolean performed_qos = FALSE;
  GstClockSync *clocksync = GST_CLOCKSYNC (parent);
  GstFlowReturn ret = GST_FLOW_OK;

  GST_LOG_OBJECT (clocksync, "Handling buffer %" GST_PTR_FORMAT, buf);

  if (clocksync->segment.format == GST_FORMAT_TIME) {
    GstClockTime runtimestamp = 0;
    GstClockTime rundts, runpts;

    if (clocksync->segment.rate > 0.0) {
      rundts = gst_segment_to_running_time (&clocksync->segment,
          GST_FORMAT_TIME, GST_BUFFER_DTS (buf));
      runpts = gst_segment_to_running_time (&clocksync->segment,
          GST_FORMAT_TIME, GST_BUFFER_PTS (buf));
    } else {
      runpts = gst_segment_to_running_time (&clocksync->segment,
          GST_FORMAT_TIME, GST_CLOCK_TIME_IS_VALID (buf->duration)
          && GST_CLOCK_TIME_IS_VALID (buf->pts) ? buf->pts +
          buf->duration : buf->pts);
      rundts = gst_segment_to_running_time (&clocksync->segment,
          GST_FORMAT_TIME, GST_CLOCK_TIME_IS_VALID (buf->duration)
          && GST_CLOCK_TIME_IS_VALID (buf->dts) ? buf->dts +
          buf->duration : buf->dts);
    }

    if (GST_CLOCK_TIME_IS_VALID (rundts))
      runtimestamp = rundts;
    else if (GST_CLOCK_TIME_IS_VALID (runpts))
      runtimestamp = runpts;

    gst_clock_sync_update_ts_offset (clocksync, runtimestamp);

    ret = gst_clocksync_do_sync (clocksync, runtimestamp);
    if (ret != GST_FLOW_OK) {
      GST_LOG_OBJECT (clocksync,
          "Interrupted while waiting on the clock. Dropping buffer.");
      gst_buffer_unref (buf);
      return ret;
    }

    performed_qos = gst_clock_sync_perform_qos (clocksync);
  }

  /* Forward the buffer */
  ret = gst_pad_push (clocksync->srcpad, buf);

  if (performed_qos)
    clocksync->last_left =
        gst_element_get_current_running_time (GST_ELEMENT (parent));

  return ret;
}

static GstFlowReturn
gst_clock_sync_chain_list (GstPad * pad, GstObject * parent,
    GstBufferList * buffer_list)
{
  gboolean performed_qos = FALSE;
  GstClockSync *clocksync = GST_CLOCKSYNC (parent);
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *buf;

  GST_LOG_OBJECT (clocksync, "Handling buffer list %" GST_PTR_FORMAT,
      buffer_list);

  if (gst_buffer_list_length (buffer_list) == 0)
    goto done;

  buf = gst_buffer_list_get (buffer_list, 0);

  if (clocksync->segment.format == GST_FORMAT_TIME) {
    GstClockTime runtimestamp = 0;
    GstClockTime rundts, runpts;

    rundts = gst_segment_to_running_time (&clocksync->segment,
        GST_FORMAT_TIME, GST_BUFFER_DTS (buf));
    runpts = gst_segment_to_running_time (&clocksync->segment,
        GST_FORMAT_TIME, GST_BUFFER_PTS (buf));

    if (GST_CLOCK_TIME_IS_VALID (rundts))
      runtimestamp = rundts;
    else if (GST_CLOCK_TIME_IS_VALID (runpts))
      runtimestamp = runpts;

    gst_clock_sync_update_ts_offset (clocksync, runtimestamp);

    ret = gst_clocksync_do_sync (clocksync, runtimestamp);
    if (ret != GST_FLOW_OK) {
      gst_buffer_list_unref (buffer_list);
      return ret;
    }

    performed_qos = gst_clock_sync_perform_qos (clocksync);
  }

  /* Forward the buffer list */
done:
  ret = gst_pad_push_list (clocksync->srcpad, buffer_list);

  if (performed_qos)
    clocksync->last_left =
        gst_element_get_current_running_time (GST_ELEMENT (parent));

  return ret;
}

static gboolean
gst_clock_sync_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstClockSync *clocksync = GST_CLOCKSYNC (parent);
  gboolean res;

  res = gst_pad_query_default (pad, parent, query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:{
      gboolean live = FALSE;
      GstClockTime min = 0, max = 0;

      if (res) {
        gst_query_parse_latency (query, &live, &min, &max);

        if (clocksync->sync && max < min) {
          GST_ELEMENT_WARNING (parent, CORE, CLOCK, (NULL),
              ("Impossible to configure latency upstream of clocksync sync=true:"
                  " max %" GST_TIME_FORMAT " < min %"
                  GST_TIME_FORMAT ". Add queues or other buffering elements.",
                  GST_TIME_ARGS (max), GST_TIME_ARGS (min)));
        }
      }

      /* Ignore the upstream latency if it is not live */
      GST_OBJECT_LOCK (clocksync);
      if (live)
        clocksync->upstream_latency = min;
      else {
        clocksync->upstream_latency = 0;
        /* if upstream is non-live source, then there is no
         * limit on the maximum latency */
        max = -1;
      }

      GST_OBJECT_UNLOCK (clocksync);

      GST_DEBUG_OBJECT (clocksync,
          "Configured upstream latency = %" GST_TIME_FORMAT,
          GST_TIME_ARGS (clocksync->upstream_latency));

      gst_query_set_latency (query, live || clocksync->sync, min, max);
      break;
    }
    default:
      break;
  }

  return res;
}

static GstStateChangeReturn
gst_clocksync_change_state (GstElement * element, GstStateChange transition)
{
  GstClockSync *clocksync = GST_CLOCKSYNC (element);
  GstStateChangeReturn ret;
  gboolean no_preroll = FALSE;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_OBJECT_LOCK (clocksync);
      clocksync->flushing = FALSE;
      clocksync->blocked = TRUE;
      GST_OBJECT_UNLOCK (clocksync);
      if (clocksync->sync)
        no_preroll = TRUE;
      clocksync->is_first = TRUE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      GST_OBJECT_LOCK (clocksync);
      clocksync->blocked = FALSE;
      g_cond_signal (&clocksync->blocked_cond);
      GST_OBJECT_UNLOCK (clocksync);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_OBJECT_LOCK (clocksync);
      clocksync->flushing = TRUE;
      if (clocksync->clock_id) {
        GST_DEBUG_OBJECT (clocksync, "unlock clock wait");
        gst_clock_id_unschedule (clocksync->clock_id);
      }
      clocksync->blocked = FALSE;
      g_cond_signal (&clocksync->blocked_cond);
      GST_OBJECT_UNLOCK (clocksync);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      GST_OBJECT_LOCK (clocksync);
      clocksync->upstream_latency = 0;
      clocksync->blocked = TRUE;
      GST_OBJECT_UNLOCK (clocksync);
      if (clocksync->sync)
        no_preroll = TRUE;
      gst_clock_sync_reset_qos (clocksync);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  if (no_preroll && ret == GST_STATE_CHANGE_SUCCESS)
    ret = GST_STATE_CHANGE_NO_PREROLL;

  return ret;
}

/* FIXME: GStreamer 2.0 */
static GstClock *
gst_clocksync_provide_clock (GstElement * element)
{
  GstClockSync *clocksync = GST_CLOCKSYNC (element);

  if (!clocksync->sync)
    return NULL;

  return gst_system_clock_obtain ();
}
