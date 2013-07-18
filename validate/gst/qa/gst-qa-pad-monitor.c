/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-qa-pad-monitor.c - QA PadMonitor class
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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

#include "gst-qa-pad-monitor.h"
#include "gst-qa-element-monitor.h"
#include <gst/gst_private.h>

/**
 * SECTION:gst-qa-pad-monitor
 * @short_description: Class that wraps a #GstPad for QA checks
 *
 * TODO
 */

GST_DEBUG_CATEGORY_STATIC (gst_qa_pad_monitor_debug);
#define GST_CAT_DEFAULT gst_qa_pad_monitor_debug

#define _do_init \
  GST_DEBUG_CATEGORY_INIT (gst_qa_pad_monitor_debug, "qa_pad_monitor", 0, "QA PadMonitor");
#define gst_qa_pad_monitor_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstQaPadMonitor, gst_qa_pad_monitor,
    GST_TYPE_QA_MONITOR, _do_init);

static gboolean gst_qa_pad_monitor_do_setup (GstQaMonitor * monitor);

/* This was copied from gstpad.c and might need
 * updating whenever it changes in core */
static GstCaps *
_gst_pad_get_caps_default (GstPad * pad)
{
  GstCaps *result = NULL;
  GstPadTemplate *templ;

  if ((templ = GST_PAD_PAD_TEMPLATE (pad))) {
    result = GST_PAD_TEMPLATE_CAPS (templ);
    GST_CAT_DEBUG_OBJECT (GST_CAT_CAPS, pad,
        "using pad template %p with caps %p %" GST_PTR_FORMAT, templ, result,
        result);

    result = gst_caps_ref (result);
    goto done;
  }
  if ((result = GST_PAD_CAPS (pad))) {
    GST_CAT_DEBUG_OBJECT (GST_CAT_CAPS, pad,
        "using pad caps %p %" GST_PTR_FORMAT, result, result);

    result = gst_caps_ref (result);
    goto done;
  }

  /* this almost never happens */
  GST_CAT_DEBUG_OBJECT (GST_CAT_CAPS, pad, "pad has no caps");
  result = gst_caps_new_empty ();

done:
  return result;
}

static gboolean
_structure_is_raw_video (GstStructure * structure)
{
  return gst_structure_has_name (structure, "video/x-raw-yuv")
      || gst_structure_has_name (structure, "video/x-raw-rgb")
      || gst_structure_has_name (structure, "video/x-raw-gray");
}

static gboolean
_structure_is_raw_audio (GstStructure * structure)
{
  return gst_structure_has_name (structure, "audio/x-raw-int")
      || gst_structure_has_name (structure, "audio/x-raw-float");
}

#define CHECK_FIELD_TYPE(m,structure,field,type,multtype) \
G_STMT_START { \
  if (!gst_structure_has_field (structure, field)) { \
    GST_QA_MONITOR_REPORT_WARNING (monitor, CAPS_NEGOTIATION, MISSING_FIELD, \
        #field " is missing"); \
  } else if (!gst_structure_has_field_typed (structure, field, type) && \
      !gst_structure_has_field_typed (structure, field, multtype)) { \
    GST_QA_MONITOR_REPORT_CRITICAL (monitor, CAPS_NEGOTIATION, BAD_FIELD_TYPE, \
        #field " has wrong type"); \
  } \
} G_STMT_END

static void
gst_qa_pad_monitor_check_raw_video_caps_complete (GstQaPadMonitor * monitor,
    GstStructure * structure)
{
  CHECK_FIELD_TYPE (monitor, structure, "width", G_TYPE_INT,
      GST_TYPE_INT_RANGE);
  CHECK_FIELD_TYPE (monitor, structure, "height", G_TYPE_INT,
      GST_TYPE_INT_RANGE);
  CHECK_FIELD_TYPE (monitor, structure, "framerate", GST_TYPE_FRACTION,
      GST_TYPE_FRACTION_RANGE);
  CHECK_FIELD_TYPE (monitor, structure, "pixel-aspect-ratio", GST_TYPE_FRACTION,
      GST_TYPE_FRACTION_RANGE);

  if (gst_structure_has_name (structure, "video/x-raw-yuv")) {
    CHECK_FIELD_TYPE (monitor, structure, "format", GST_TYPE_FOURCC,
        G_TYPE_ARRAY);

  } else if (gst_structure_has_name (structure, "video/x-raw-rgb")) {
    CHECK_FIELD_TYPE (monitor, structure, "bpp", G_TYPE_INT, G_TYPE_ARRAY);
    CHECK_FIELD_TYPE (monitor, structure, "depth", G_TYPE_INT, G_TYPE_ARRAY);
    CHECK_FIELD_TYPE (monitor, structure, "endianness", G_TYPE_INT,
        G_TYPE_ARRAY);
  }

}

static void
gst_qa_pad_monitor_check_raw_audio_caps_complete (GstQaPadMonitor * monitor,
    GstStructure * structure)
{
  CHECK_FIELD_TYPE (monitor, structure, "rate", G_TYPE_INT, GST_TYPE_INT_RANGE);
  CHECK_FIELD_TYPE (monitor, structure, "channels", G_TYPE_INT,
      GST_TYPE_INT_RANGE);
  CHECK_FIELD_TYPE (monitor, structure, "endianness", G_TYPE_INT, G_TYPE_ARRAY);
  CHECK_FIELD_TYPE (monitor, structure, "channel-layout", G_TYPE_STRING,
      G_TYPE_ARRAY);
}

static void
gst_qa_pad_monitor_check_caps_complete (GstQaPadMonitor * monitor,
    GstCaps * caps)
{
  GstStructure *structure;
  gint i;

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    structure = gst_caps_get_structure (caps, i);

    if (_structure_is_raw_video (structure)) {
      gst_qa_pad_monitor_check_raw_video_caps_complete (monitor, structure);

    } else if (_structure_is_raw_audio (structure)) {
      gst_qa_pad_monitor_check_raw_audio_caps_complete (monitor, structure);
    }
  }
}

void
_parent_set_cb (GstObject * object, GstObject * parent, GstQaMonitor * monitor)
{
  gst_qa_monitor_set_target_name (monitor, g_strdup_printf ("%s:%s",
          GST_DEBUG_PAD_NAME (object)));
}


static void
gst_qa_pad_monitor_dispose (GObject * object)
{
  GstQaPadMonitor *monitor = GST_QA_PAD_MONITOR_CAST (object);
  GstPad *pad = GST_QA_PAD_MONITOR_GET_PAD (monitor);

  if (monitor->buffer_probe_id)
    gst_pad_remove_data_probe (pad, monitor->buffer_probe_id);
  if (monitor->event_probe_id)
    gst_pad_remove_data_probe (pad, monitor->event_probe_id);

  if (monitor->expected_segment)
    gst_event_unref (monitor->expected_segment);


  g_signal_handlers_disconnect_by_func (pad, (GCallback) _parent_set_cb,
      monitor);
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_qa_pad_monitor_class_init (GstQaPadMonitorClass * klass)
{
  GObjectClass *gobject_class;
  GstQaMonitorClass *monitor_klass;

  gobject_class = G_OBJECT_CLASS (klass);
  monitor_klass = GST_QA_MONITOR_CLASS (klass);

  gobject_class->dispose = gst_qa_pad_monitor_dispose;

  monitor_klass->setup = gst_qa_pad_monitor_do_setup;
}

static void
gst_qa_pad_monitor_init (GstQaPadMonitor * pad_monitor)
{
  gst_segment_init (&pad_monitor->segment, GST_FORMAT_BYTES);
  pad_monitor->first_buffer = TRUE;

  pad_monitor->timestamp_range_start = GST_CLOCK_TIME_NONE;
  pad_monitor->timestamp_range_end = GST_CLOCK_TIME_NONE;
}

/**
 * gst_qa_pad_monitor_new:
 * @pad: (transfer-none): a #GstPad to run QA on
 */
GstQaPadMonitor *
gst_qa_pad_monitor_new (GstPad * pad, GstQaRunner * runner,
    GstQaElementMonitor * parent)
{
  GstQaPadMonitor *monitor = g_object_new (GST_TYPE_QA_PAD_MONITOR,
      "object", pad, "qa-runner", runner, "qa-parent",
      parent, NULL);

  if (GST_QA_PAD_MONITOR_GET_PAD (monitor) == NULL) {
    g_object_unref (monitor);
    return NULL;
  }
  return monitor;
}

static gboolean
gst_qa_pad_monitor_timestamp_is_in_received_range (GstQaPadMonitor * monitor,
    GstClockTime ts)
{
  return !GST_CLOCK_TIME_IS_VALID (monitor->timestamp_range_start) ||
      !GST_CLOCK_TIME_IS_VALID (monitor->timestamp_range_end) ||
      (monitor->timestamp_range_start <= ts
      && ts <= monitor->timestamp_range_end);
}

/* Iterates over internal links (sinkpads) to check that this buffer has
 * a timestamp that is in the range of the lastly received buffers */
static void
gst_qa_pad_monitor_check_buffer_timestamp_in_received_range (GstQaPadMonitor *
    monitor, GstBuffer * buffer)
{
  GstClockTime ts;
  GstClockTime ts_end;
  GstIterator *iter;
  gboolean has_one = FALSE;
  gboolean found = FALSE;
  gboolean done;
  GstPad *otherpad;
  GstQaPadMonitor *othermonitor;

  if (!GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (buffer))
      || !GST_CLOCK_TIME_IS_VALID (GST_BUFFER_DURATION (buffer))) {
    GST_DEBUG_OBJECT (monitor,
        "Can't check buffer timestamps range as "
        "buffer has no valid timestamp/duration");
    return;
  }
  ts = GST_BUFFER_TIMESTAMP (buffer);
  ts_end = ts + GST_BUFFER_DURATION (buffer);

  iter = gst_pad_iterate_internal_links (GST_QA_PAD_MONITOR_GET_PAD (monitor));
  done = FALSE;
  while (!done) {
    switch (gst_iterator_next (iter, (gpointer *) & otherpad)) {
      case GST_ITERATOR_OK:
        GST_DEBUG_OBJECT (monitor, "Checking pad %s:%s input timestamps",
            GST_DEBUG_PAD_NAME (otherpad));
        othermonitor = g_object_get_data ((GObject *) otherpad, "qa-monitor");
        if (gst_qa_pad_monitor_timestamp_is_in_received_range (othermonitor, ts)
            && gst_qa_pad_monitor_timestamp_is_in_received_range (othermonitor,
                ts_end)) {
          done = TRUE;
          found = TRUE;
        }
        gst_object_unref (otherpad);
        has_one = TRUE;
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        has_one = FALSE;
        found = FALSE;
        break;
      case GST_ITERATOR_ERROR:
        GST_WARNING_OBJECT (monitor, "Internal links pad iteration error");
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (iter);

  if (!has_one) {
    GST_DEBUG_OBJECT (monitor, "Skipping timestamp in range check as no "
        "internal linked pad was found");
  }
  if (!found) {
    GST_QA_MONITOR_REPORT_WARNING (monitor, BUFFER, TIMESTAMP,
        "Timestamp is out of range of received input");
  }
}

static void
gst_qa_pad_monitor_notify_buffer_pushed (GstQaPadMonitor * monitor)
{
  GstIterator *iter;
  gboolean done;
  GstPad *otherpad;
  GstQaPadMonitor *othermonitor;

  iter = gst_pad_iterate_internal_links (GST_QA_PAD_MONITOR_GET_PAD (monitor));
  done = FALSE;
  while (!done) {
    switch (gst_iterator_next (iter, (gpointer *) & otherpad)) {
      case GST_ITERATOR_OK:
        othermonitor = g_object_get_data ((GObject *) otherpad, "qa-monitor");
        othermonitor->buffer_pushed = TRUE;
        gst_object_unref (otherpad);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        break;
      case GST_ITERATOR_ERROR:
        GST_WARNING_OBJECT (monitor, "Internal links pad iteration error");
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (iter);
}

static void
gst_qa_pad_monitor_check_first_buffer (GstQaPadMonitor * pad_monitor,
    GstBuffer * buffer)
{
  if (G_UNLIKELY (pad_monitor->first_buffer)) {
    pad_monitor->first_buffer = FALSE;

    if (!pad_monitor->has_segment) {
      GST_QA_MONITOR_REPORT_WARNING (pad_monitor, EVENT, EXPECTED,
          "Received buffer before Segment event");
    }
    if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (buffer))) {
      gint64 running_time = gst_segment_to_running_time (&pad_monitor->segment,
          pad_monitor->segment.format, GST_BUFFER_TIMESTAMP (buffer));
      if (running_time != 0) {
        GST_QA_MONITOR_REPORT_WARNING (pad_monitor, BUFFER, TIMESTAMP,
            "First buffer running time is not 0");
      }
    }
  }
}

static void
gst_qa_pad_monitor_update_buffer_data (GstQaPadMonitor * pad_monitor,
    GstBuffer * buffer)
{
  pad_monitor->current_timestamp = GST_BUFFER_TIMESTAMP (buffer);
  pad_monitor->current_duration = GST_BUFFER_DURATION (buffer);
  if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (buffer)) &&
      GST_CLOCK_TIME_IS_VALID (GST_BUFFER_DURATION (buffer))) {
    if (pad_monitor->buffer_pushed) {
      pad_monitor->timestamp_range_start = GST_BUFFER_TIMESTAMP (buffer);
    }
    pad_monitor->timestamp_range_end = GST_BUFFER_TIMESTAMP (buffer) +
        GST_BUFFER_DURATION (buffer);
  } else {
    pad_monitor->timestamp_range_start = GST_CLOCK_TIME_NONE;
    pad_monitor->timestamp_range_end = GST_CLOCK_TIME_NONE;
  }
  pad_monitor->buffer_pushed = FALSE;
}

static GstFlowReturn
_combine_flows (GstFlowReturn ret1, GstFlowReturn ret2)
{
  /* TODO review the combination
   * what about not-negotiated and unexpected ? */
  if (ret1 == ret2)
    return ret1;
  if (ret1 <= GST_FLOW_NOT_NEGOTIATED)
    return ret1;
  if (ret2 <= GST_FLOW_NOT_NEGOTIATED)
    return ret2;
  if (ret1 == GST_FLOW_OK || ret2 == GST_FLOW_OK)
    return GST_FLOW_OK;
  return ret2;
}

static void
gst_qa_pad_monitor_check_aggregated_return (GstQaPadMonitor * monitor,
    GstFlowReturn ret)
{
  GstIterator *iter;
  gboolean done;
  GstPad *otherpad;
  GstPad *peerpad;
  GstQaPadMonitor *othermonitor;
  GstFlowReturn aggregated = GST_FLOW_NOT_LINKED;
  gboolean found_a_pad = FALSE;

  iter = gst_pad_iterate_internal_links (GST_QA_PAD_MONITOR_GET_PAD (monitor));
  done = FALSE;
  while (!done) {
    switch (gst_iterator_next (iter, (gpointer *) & otherpad)) {
      case GST_ITERATOR_OK:
        found_a_pad = TRUE;
        peerpad = gst_pad_get_peer (otherpad);
        if (peerpad) {
          othermonitor = g_object_get_data ((GObject *) peerpad, "qa-monitor");
          if (othermonitor) {
            aggregated =
                _combine_flows (aggregated, othermonitor->last_flow_return);
          }
          gst_object_unref (peerpad);
        }
        gst_object_unref (otherpad);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        break;
      case GST_ITERATOR_ERROR:
        GST_WARNING_OBJECT (monitor, "Internal links pad iteration error");
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (iter);
  if (!found_a_pad) {
    /* no peer pad found, nothing to do */
    return;
  }
  if (aggregated != ret) {
    /* TODO review this error code */
    GST_QA_MONITOR_REPORT_CRITICAL (monitor, BUFFER, UNEXPECTED,
        "Wrong combined flow return");
  }
}

static void
gst_qa_pad_monitor_add_expected_newsegment (GstQaPadMonitor * monitor,
    GstEvent * event)
{
  GstIterator *iter;
  gboolean done;
  GstPad *otherpad;
  GstQaPadMonitor *othermonitor;

  iter = gst_pad_iterate_internal_links (GST_QA_PAD_MONITOR_GET_PAD (monitor));
  done = FALSE;
  while (!done) {
    switch (gst_iterator_next (iter, (gpointer *) & otherpad)) {
      case GST_ITERATOR_OK:
        othermonitor = g_object_get_data ((GObject *) otherpad, "qa-monitor");
        if (othermonitor->expected_segment) {
          GST_QA_MONITOR_REPORT_WARNING (othermonitor, EVENT, EXPECTED,
              "expected newsegment event never pushed");
          gst_event_unref (othermonitor->expected_segment);
        }
        othermonitor->expected_segment = gst_event_ref (event);
        gst_object_unref (otherpad);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        break;
      case GST_ITERATOR_ERROR:
        GST_WARNING_OBJECT (monitor, "Internal links pad iteration error");
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (iter);
}

static gboolean
gst_qa_pad_monitor_sink_event_check (GstQaPadMonitor * pad_monitor,
    GstEvent * event, GstPadEventFunction handler)
{
  gboolean ret = TRUE;
  gboolean update;
  gdouble rate, applied_rate;
  GstFormat format;
  gint64 start, stop, position;
  guint32 seqnum = gst_event_get_seqnum (event);
  GstPad *pad = GST_QA_PAD_MONITOR_GET_PAD (pad_monitor);

  /* pre checks */
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
      /* parse newsegment data to be used if event is handled */
      gst_event_parse_new_segment_full (event, &update, &rate, &applied_rate,
          &format, &start, &stop, &position);

      if (pad_monitor->pending_newsegment_seqnum) {
        if (pad_monitor->pending_newsegment_seqnum == seqnum) {
          pad_monitor->pending_newsegment_seqnum = 0;
        } else {
          /* TODO is this an error? could be a segment from the start
           * received just before the seek segment */
        }
      }

      if (GST_PAD_DIRECTION (pad) == GST_PAD_SINK) {
        gst_qa_pad_monitor_add_expected_newsegment (pad_monitor, event);
      } else {
        /* check if this segment is the expected one */
        if (pad_monitor->expected_segment) {
          gint64 exp_start, exp_stop, exp_position;
          gdouble exp_rate, exp_applied_rate;
          gboolean exp_update;
          GstFormat exp_format;

          if (pad_monitor->expected_segment != event) {
            gst_event_parse_new_segment_full (event, &exp_update, &exp_rate,
                &exp_applied_rate, &exp_format, &exp_start, &exp_stop,
                &exp_position);
            if (format == exp_format) {
              if (update != exp_update
                  || (exp_rate * exp_applied_rate != rate * applied_rate)
                  || exp_start != start || exp_stop != stop
                  || exp_position != position) {
                GST_QA_MONITOR_REPORT_WARNING (pad_monitor, EVENT, EXPECTED,
                    "Expected segment didn't match received segment event");
              }
            }
          }
          gst_event_replace (&pad_monitor->expected_segment, NULL);
        }
      }
      break;
    case GST_EVENT_FLUSH_START:
    {
      if (pad_monitor->pending_flush_start_seqnum) {
        if (seqnum == pad_monitor->pending_flush_start_seqnum) {
          pad_monitor->pending_flush_start_seqnum = 0;
        } else {
          GST_QA_MONITOR_REPORT_ISSUE (pad_monitor, EVENT, SEQNUM,
              "The expected flush-start seqnum should be the same as the "
              "one from the event that caused it (probably a seek)");
        }
      }

      if (pad_monitor->pending_flush_stop) {
        GST_QA_MONITOR_REPORT_ISSUE (pad_monitor, EVENT, UNEXPECTED,
            "Received flush-start when flush-stop was expected");
      }
    }
      break;
    case GST_EVENT_FLUSH_STOP:
    {
      if (pad_monitor->pending_flush_stop_seqnum) {
        if (seqnum == pad_monitor->pending_flush_stop_seqnum) {
          pad_monitor->pending_flush_stop_seqnum = 0;
        } else {
          GST_QA_MONITOR_REPORT_ISSUE (pad_monitor, EVENT, SEQNUM,
              "The expected flush-stop seqnum should be the same as the "
              "one from the event that caused it (probably a seek)");
        }
      }

      if (!pad_monitor->pending_flush_stop) {
        GST_QA_MONITOR_REPORT_ISSUE (pad_monitor, EVENT, UNEXPECTED,
            "Unexpected flush-stop");
      }
    }
      break;
    case GST_EVENT_EOS:
    case GST_EVENT_TAG:
    case GST_EVENT_SINK_MESSAGE:
    default:
      break;
  }

  if (handler) {
    gst_event_ref (event);
    ret = pad_monitor->event_func (pad, event);
  }

  /* post checks */
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
      if (ret) {
        if (!pad_monitor->has_segment && pad_monitor->segment.format != format) {
          gst_segment_init (&pad_monitor->segment, format);
        }
        gst_segment_set_newsegment_full (&pad_monitor->segment, update, rate,
            applied_rate, format, start, stop, position);
        pad_monitor->has_segment = TRUE;
      }
      break;
    case GST_EVENT_FLUSH_START:
      if (ret) {
        pad_monitor->pending_flush_stop = TRUE;
      }
      break;
    case GST_EVENT_FLUSH_STOP:
      if (ret) {
        pad_monitor->pending_flush_stop = FALSE;
      }
      break;
    case GST_EVENT_EOS:
    case GST_EVENT_TAG:
    case GST_EVENT_SINK_MESSAGE:
    default:
      break;
  }

  if (handler)
    gst_event_unref (event);
  return ret;
}

static gboolean
gst_qa_pad_monitor_src_event_check (GstQaPadMonitor * pad_monitor,
    GstEvent * event, GstPadEventFunction handler)
{
  gboolean ret = TRUE;
  gdouble rate;
  GstFormat format;
  gint64 start, stop;
  GstSeekFlags seek_flags;
  GstSeekType start_type, stop_type;
  guint32 seqnum = gst_event_get_seqnum (event);
  GstPad *pad = GST_QA_PAD_MONITOR_GET_PAD (pad_monitor);

  /* pre checks */
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      gst_event_parse_seek (event, &rate, &format, &seek_flags, &start_type,
          &start, &stop_type, &stop);
      /* upstream seek - store the seek event seqnum to check
       * flushes and newsegments share the same */

      /* TODO we might need to use a list as multiple seeks can be sent
       * before the flushes arrive here */
      if (seek_flags & GST_SEEK_FLAG_FLUSH) {
        pad_monitor->pending_flush_start_seqnum = seqnum;
        pad_monitor->pending_flush_stop_seqnum = seqnum;
      }
      pad_monitor->pending_newsegment_seqnum = seqnum;
    }
      break;
    case GST_EVENT_FLUSH_START:
    {
      if (pad_monitor->pending_flush_start_seqnum) {
        if (seqnum == pad_monitor->pending_flush_start_seqnum) {
          pad_monitor->pending_flush_start_seqnum = 0;
        } else {
          GST_QA_MONITOR_REPORT_ISSUE (pad_monitor, EVENT, SEQNUM,
              "The expected flush-start seqnum should be the same as the "
              "one from the event that caused it (probably a seek)");
        }
      } else {
        GST_QA_MONITOR_REPORT_CRITICAL (pad_monitor, EVENT, UNEXPECTED,
            "Received unexpected flush-start");
      }

      if (pad_monitor->pending_flush_stop) {
        GST_QA_MONITOR_REPORT_ISSUE (pad_monitor, EVENT, UNEXPECTED,
            "Received flush-start when flush-stop was expected");
      }
    }
      break;
    case GST_EVENT_FLUSH_STOP:
    {
      if (pad_monitor->pending_flush_stop_seqnum) {
        if (seqnum == pad_monitor->pending_flush_stop_seqnum) {
          pad_monitor->pending_flush_stop_seqnum = 0;
        } else {
          GST_QA_MONITOR_REPORT_ISSUE (pad_monitor, EVENT, SEQNUM,
              "The expected flush-stop seqnum should be the same as the "
              "one from the event that caused it (probably a seek)");
        }
      }

      if (!pad_monitor->pending_flush_stop) {
        GST_QA_MONITOR_REPORT_ISSUE (pad_monitor, EVENT, UNEXPECTED,
            "Unexpected flush-stop");
      }
    }
      break;
    case GST_EVENT_NAVIGATION:
    case GST_EVENT_LATENCY:
    case GST_EVENT_STEP:
    case GST_EVENT_QOS:
    default:
      break;
  }

  if (handler) {
    gst_event_ref (event);
    ret = pad_monitor->event_func (pad, event);
  }

  /* post checks */
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      if (ret) {
        pad_monitor->pending_flush_stop = TRUE;
      }
      break;
    case GST_EVENT_FLUSH_STOP:
      if (ret) {
        pad_monitor->pending_flush_stop = FALSE;
      }
      break;
    case GST_EVENT_QOS:
    case GST_EVENT_SEEK:
    case GST_EVENT_NAVIGATION:
    case GST_EVENT_LATENCY:
    case GST_EVENT_STEP:
    default:
      break;
  }

  if (handler)
    gst_event_unref (event);
  return ret;
}

static GstFlowReturn
gst_qa_pad_monitor_chain_func (GstPad * pad, GstBuffer * buffer)
{
  GstQaPadMonitor *pad_monitor =
      g_object_get_data ((GObject *) pad, "qa-monitor");
  GstFlowReturn ret;

  gst_qa_pad_monitor_check_first_buffer (pad_monitor, buffer);

  gst_qa_pad_monitor_update_buffer_data (pad_monitor, buffer);

  ret = pad_monitor->chain_func (pad, buffer);

  pad_monitor->last_flow_return = ret;
  gst_qa_pad_monitor_check_aggregated_return (pad_monitor, ret);

  return ret;
}

static gboolean
gst_qa_pad_monitor_sink_event_func (GstPad * pad, GstEvent * event)
{
  GstQaPadMonitor *pad_monitor =
      g_object_get_data ((GObject *) pad, "qa-monitor");

  return gst_qa_pad_monitor_sink_event_check (pad_monitor, event,
      pad_monitor->event_func);
}

static gboolean
gst_qa_pad_monitor_src_event_func (GstPad * pad, GstEvent * event)
{
  GstQaPadMonitor *pad_monitor =
      g_object_get_data ((GObject *) pad, "qa-monitor");

  return gst_qa_pad_monitor_src_event_check (pad_monitor, event,
      pad_monitor->event_func);
}

static gboolean
gst_qa_pad_monitor_query_func (GstPad * pad, GstQuery * query)
{
  GstQaPadMonitor *pad_monitor =
      g_object_get_data ((GObject *) pad, "qa-monitor");
  gboolean ret;
  ret = pad_monitor->query_func (pad, query);
  return ret;
}

static gboolean
gst_qa_pad_buffer_alloc_func (GstPad * pad, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buffer)
{
  GstQaPadMonitor *pad_monitor =
      g_object_get_data ((GObject *) pad, "qa-monitor");
  gboolean ret;
  ret = pad_monitor->bufferalloc_func (pad, offset, size, caps, buffer);
  return ret;
}

static gboolean
gst_qa_pad_get_range_func (GstPad * pad, guint64 offset, guint size,
    GstBuffer ** buffer)
{
  GstQaPadMonitor *pad_monitor =
      g_object_get_data ((GObject *) pad, "qa-monitor");
  gboolean ret;
  ret = pad_monitor->getrange_func (pad, offset, size, buffer);
  return ret;
}

static gboolean
gst_qa_pad_monitor_buffer_probe (GstPad * pad, GstBuffer * buffer,
    gpointer udata)
{
  GstQaPadMonitor *monitor = udata;

  gst_qa_pad_monitor_check_first_buffer (monitor, buffer);
  gst_qa_pad_monitor_update_buffer_data (monitor, buffer);

  gst_qa_pad_monitor_check_buffer_timestamp_in_received_range (monitor, buffer);
  gst_qa_pad_monitor_notify_buffer_pushed (monitor);

  /* TODO should we assume that a pad-monitor should always have an
   * element-monitor as a parent? */
  if (G_LIKELY (GST_QA_MONITOR_GET_PARENT (monitor))) {
    /* a GstQaPadMonitor parent must be a GstQaElementMonitor */
    if (GST_QA_ELEMENT_MONITOR_ELEMENT_IS_DECODER (monitor)) {
      /* should not push out of segment data */
      if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (buffer)) &&
          GST_CLOCK_TIME_IS_VALID (GST_BUFFER_DURATION (buffer)) &&
          !gst_segment_clip (&monitor->segment, monitor->segment.format,
              GST_BUFFER_TIMESTAMP (buffer), GST_BUFFER_TIMESTAMP (buffer) +
              GST_BUFFER_DURATION (buffer), NULL, NULL)) {
        /* TODO is this a timestamp issue? */
        GST_QA_MONITOR_REPORT_ISSUE (monitor, BUFFER, TIMESTAMP,
            "buffer is out of segment and shouldn't be pushed");
      }
    }
  }
  return TRUE;
}

static gboolean
gst_qa_pad_monitor_event_probe (GstPad * pad, GstEvent * event, gpointer udata)
{
  GstQaPadMonitor *monitor = GST_QA_PAD_MONITOR_CAST (udata);
  /* This so far is just like an event that is flowing downstream,
   * so we do the same checks as a sinkpad event handler */
  return gst_qa_pad_monitor_sink_event_check (monitor, event, NULL);
}

static GstCaps *
gst_qa_pad_monitor_getcaps_func (GstPad * pad)
{
  GstQaPadMonitor *pad_monitor =
      g_object_get_data ((GObject *) pad, "qa-monitor");
  GstCaps *ret = NULL;

  if (pad_monitor->getcaps_func) {
    ret = pad_monitor->getcaps_func (pad);
  } else {
    ret = _gst_pad_get_caps_default (pad);
  }

  if (ret) {
    gst_qa_pad_monitor_check_caps_complete (pad_monitor, ret);
  }

  return ret;
}

static gboolean
gst_qa_pad_monitor_setcaps_func (GstPad * pad, GstCaps * caps)
{
  GstQaPadMonitor *pad_monitor =
      g_object_get_data ((GObject *) pad, "qa-monitor");
  gboolean ret = TRUE;

  if (pad_monitor->setcaps_func) {
    ret = pad_monitor->setcaps_func (pad, caps);
  }

  return ret;
}

static gboolean
gst_qa_pad_monitor_do_setup (GstQaMonitor * monitor)
{
  GstQaPadMonitor *pad_monitor = GST_QA_PAD_MONITOR_CAST (monitor);
  GstPad *pad;
  if (!GST_IS_PAD (GST_QA_MONITOR_GET_OBJECT (monitor))) {
    GST_WARNING_OBJECT (monitor, "Trying to create pad monitor with other "
        "type of object");
    return FALSE;
  }

  pad = GST_QA_PAD_MONITOR_GET_PAD (pad_monitor);

  if (g_object_get_data ((GObject *) pad, "qa-monitor")) {
    GST_WARNING_OBJECT (pad_monitor, "Pad already has a qa-monitor associated");
    return FALSE;
  }

  g_object_set_data ((GObject *) pad, "qa-monitor", pad_monitor);

  pad_monitor->event_func = GST_PAD_EVENTFUNC (pad);
  pad_monitor->query_func = GST_PAD_QUERYFUNC (pad);
  pad_monitor->setcaps_func = GST_PAD_SETCAPSFUNC (pad);
  pad_monitor->getcaps_func = GST_PAD_GETCAPSFUNC (pad);
  if (GST_PAD_DIRECTION (pad) == GST_PAD_SINK) {
    pad_monitor->bufferalloc_func = GST_PAD_BUFFERALLOCFUNC (pad);
    if (pad_monitor->bufferalloc_func)
      gst_pad_set_bufferalloc_function (pad, gst_qa_pad_buffer_alloc_func);

    pad_monitor->chain_func = GST_PAD_CHAINFUNC (pad);
    if (pad_monitor->chain_func)
      gst_pad_set_chain_function (pad, gst_qa_pad_monitor_chain_func);

    gst_pad_set_event_function (pad, gst_qa_pad_monitor_sink_event_func);
  } else {
    pad_monitor->getrange_func = GST_PAD_GETRANGEFUNC (pad);
    if (pad_monitor->getrange_func)
      gst_pad_set_getrange_function (pad, gst_qa_pad_get_range_func);

    gst_pad_set_event_function (pad, gst_qa_pad_monitor_src_event_func);

    /* add buffer/event probes */
    pad_monitor->buffer_probe_id = gst_pad_add_buffer_probe (pad,
        (GCallback) gst_qa_pad_monitor_buffer_probe, pad_monitor);
    pad_monitor->event_probe_id = gst_pad_add_event_probe (pad,
        (GCallback) gst_qa_pad_monitor_event_probe, pad_monitor);
  }
  gst_pad_set_query_function (pad, gst_qa_pad_monitor_query_func);
  gst_pad_set_getcaps_function (pad, gst_qa_pad_monitor_getcaps_func);
  gst_pad_set_setcaps_function (pad, gst_qa_pad_monitor_setcaps_func);

  gst_qa_monitor_set_target_name (monitor, g_strdup_printf ("%s:%s",
          GST_DEBUG_PAD_NAME (pad)));

  g_signal_connect (pad, "parent-set", (GCallback) _parent_set_cb, monitor);

  return TRUE;
}
