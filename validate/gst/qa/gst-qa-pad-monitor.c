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
#include <gst/gst.h>
#include <string.h>

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
    GST_DEBUG_OBJECT (pad, "using pad template %p with caps %p %"
        GST_PTR_FORMAT, templ, result, result);

    result = gst_caps_ref (result);
    goto done;
  }
  if ((result = GST_PAD_CAPS (pad))) {
    GST_DEBUG_OBJECT (pad, "using pad caps %p %" GST_PTR_FORMAT, result,
        result);

    result = gst_caps_ref (result);
    goto done;
  }

  /* this almost never happens */
  GST_DEBUG_OBJECT (pad, "pad has no caps");
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
    GST_QA_MONITOR_REPORT_WARNING (monitor, FALSE, CAPS_NEGOTIATION, MISSING_FIELD, \
        #field " is missing from structure: %" GST_PTR_FORMAT, structure); \
  } else if (!gst_structure_has_field_typed (structure, field, type) && \
      !gst_structure_has_field_typed (structure, field, multtype)) { \
    GST_QA_MONITOR_REPORT_CRITICAL (monitor, FALSE, CAPS_NEGOTIATION, BAD_FIELD_TYPE, \
        #field " has wrong type %s in structure '%" GST_PTR_FORMAT \
        "'. Expected: %s or %s", \
        g_type_name (gst_structure_get_field_type (structure, field)), \
        structure, g_type_name (type), g_type_name (multtype)); \
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
        GST_TYPE_LIST);

  } else if (gst_structure_has_name (structure, "video/x-raw-rgb")) {
    CHECK_FIELD_TYPE (monitor, structure, "bpp", G_TYPE_INT, GST_TYPE_LIST);
    CHECK_FIELD_TYPE (monitor, structure, "depth", G_TYPE_INT, GST_TYPE_LIST);
    CHECK_FIELD_TYPE (monitor, structure, "endianness", G_TYPE_INT,
        GST_TYPE_LIST);
  }

}

static void
gst_qa_pad_monitor_check_raw_audio_caps_complete (GstQaPadMonitor * monitor,
    GstStructure * structure)
{
  CHECK_FIELD_TYPE (monitor, structure, "rate", G_TYPE_INT, GST_TYPE_LIST);
  CHECK_FIELD_TYPE (monitor, structure, "channels", G_TYPE_INT,
      GST_TYPE_INT_RANGE);
  CHECK_FIELD_TYPE (monitor, structure, "endianness", G_TYPE_INT,
      GST_TYPE_LIST);
  CHECK_FIELD_TYPE (monitor, structure, "channel-layout", G_TYPE_STRING,
      GST_TYPE_LIST);
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

static GstCaps *
gst_qa_pad_monitor_get_othercaps (GstQaPadMonitor * monitor)
{
  GstCaps *caps = gst_caps_new_empty ();
  GstIterator *iter;
  gboolean done;
  GstPad *otherpad;

  iter = gst_pad_iterate_internal_links (GST_QA_PAD_MONITOR_GET_PAD (monitor));
  done = FALSE;
  while (!done) {
    switch (gst_iterator_next (iter, (gpointer *) & otherpad)) {
      case GST_ITERATOR_OK:

        /* TODO What would be the correct caps operation to merge the caps in
         * case one sink is internally linked to multiple srcs? */
        gst_caps_merge (caps, gst_pad_peer_get_caps_reffed (otherpad));
        gst_object_unref (otherpad);

        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        gst_caps_replace (&caps, gst_caps_new_empty ());
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

  GST_DEBUG_OBJECT (monitor, "Otherpad caps: %" GST_PTR_FORMAT, caps);

  return caps;
}

static gboolean
_structure_is_video (GstStructure * structure)
{
  const gchar *name = gst_structure_get_name (structure);

  return g_strstr_len (name, 6, "video/")
      && strcmp (name, "video/quicktime") != 0;
}

static gboolean
_structure_is_audio (GstStructure * structure)
{
  const gchar *name = gst_structure_get_name (structure);

  return g_strstr_len (name, 6, "audio/") != NULL;
}

static gboolean
gst_qa_pad_monitor_pad_should_proxy_othercaps (GstQaPadMonitor * monitor)
{
  GstQaMonitor *parent = GST_QA_MONITOR_GET_PARENT (monitor);
  /* We only know how to handle othercaps checks for decoders so far */
  return GST_QA_ELEMENT_MONITOR_ELEMENT_IS_DECODER (parent) ||
      GST_QA_ELEMENT_MONITOR_ELEMENT_IS_ENCODER (parent);
}

static gboolean
_structures_field_match (GstStructure * s1, GstStructure * s2, const gchar * f)
{
  const GValue *v1;
  const GValue *v2;

  v2 = gst_structure_get_value (s2, f);
  if (!v2)
    return TRUE;                /* nothing to compare to */

  v1 = gst_structure_get_value (s1, f);
  if (!v1)
    return FALSE;

  return gst_value_compare (v1, v2) == GST_VALUE_EQUAL;
}

static void
gst_qa_pad_monitor_check_caps_fields_proxied (GstQaPadMonitor * monitor,
    GstCaps * caps)
{
  GstStructure *structure;
  GstStructure *otherstructure;
  GstCaps *othercaps;
  gint i, j;

  if (!gst_qa_pad_monitor_pad_should_proxy_othercaps (monitor))
    return;

  othercaps = gst_qa_pad_monitor_get_othercaps (monitor);

  for (i = 0; i < gst_caps_get_size (othercaps); i++) {
    gboolean found = FALSE;
    gboolean type_match = FALSE;

    otherstructure = gst_caps_get_structure (othercaps, i);

    if (_structure_is_video (otherstructure)) {
      for (j = 0; j < gst_caps_get_size (caps); j++) {
        structure = gst_caps_get_structure (caps, j);
        if (_structure_is_video (structure)) {
          type_match = TRUE;
          if (_structures_field_match (structure, otherstructure, "width") &&
              _structures_field_match (structure, otherstructure, "height") &&
              _structures_field_match (structure, otherstructure, "framerate")
              && _structures_field_match (structure, otherstructure,
                  "pixel-aspect-ratio")) {
            found = TRUE;
            break;
          }
        }
      }
    } else if (_structure_is_audio (otherstructure)) {
      for (j = 0; j < gst_caps_get_size (caps); j++) {
        structure = gst_caps_get_structure (caps, j);
        if (_structure_is_audio (structure)) {
          type_match = TRUE;
          if (_structures_field_match (structure, otherstructure, "rate") &&
              _structures_field_match (structure, otherstructure, "channels")) {
            found = TRUE;
            break;
          }
        }
      }
    }

    if (type_match && !found) {
      GST_QA_MONITOR_REPORT_WARNING (monitor, FALSE, CAPS_NEGOTIATION,
          GET_CAPS,
          "Peer pad structure %" GST_PTR_FORMAT " has no similar version "
          "on pad's caps %" GST_PTR_FORMAT, otherstructure, caps);
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

  if (pad) {
    if (monitor->buffer_probe_id)
      gst_pad_remove_data_probe (pad, monitor->buffer_probe_id);
    if (monitor->event_probe_id)
      gst_pad_remove_data_probe (pad, monitor->event_probe_id);

    g_signal_handlers_disconnect_by_func (pad, (GCallback) _parent_set_cb,
        monitor);
  }

  if (monitor->expected_segment)
    gst_event_unref (monitor->expected_segment);


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
  GST_DEBUG_OBJECT (monitor, "Checking if timestamp %" GST_TIME_FORMAT
      " is in range: %" GST_TIME_FORMAT " - %" GST_TIME_FORMAT " for pad "
      "%s:%s", GST_TIME_ARGS (ts),
      GST_TIME_ARGS (monitor->timestamp_range_start),
      GST_TIME_ARGS (monitor->timestamp_range_end),
      GST_DEBUG_PAD_NAME (GST_QA_PAD_MONITOR_GET_PAD (monitor)));
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
    GST_QA_MONITOR_REPORT_WARNING (monitor, FALSE, BUFFER, TIMESTAMP,
        "Timestamp is out of range of received input");
  }
}

static void
gst_qa_pad_monitor_check_first_buffer (GstQaPadMonitor * pad_monitor,
    GstBuffer * buffer)
{
  if (G_UNLIKELY (pad_monitor->first_buffer)) {
    pad_monitor->first_buffer = FALSE;

    if (!pad_monitor->has_segment) {
      GST_QA_MONITOR_REPORT_WARNING (pad_monitor, FALSE, EVENT, EXPECTED,
          "Received buffer before Segment event");
    }
    if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (buffer))) {
      gint64 running_time = gst_segment_to_running_time (&pad_monitor->segment,
          pad_monitor->segment.format, GST_BUFFER_TIMESTAMP (buffer));
      if (running_time != 0) {
        GST_QA_MONITOR_REPORT_WARNING (pad_monitor, FALSE, BUFFER, TIMESTAMP,
            "First buffer running time is not 0, it is: %" GST_TIME_FORMAT,
            GST_TIME_ARGS (running_time));
      }
    }
  }
}

static void
gst_qa_pad_monitor_update_buffer_data (GstQaPadMonitor * pad_monitor,
    GstBuffer * buffer)
{
  /* TODO handle reverse playback too */
  pad_monitor->current_timestamp = GST_BUFFER_TIMESTAMP (buffer);
  pad_monitor->current_duration = GST_BUFFER_DURATION (buffer);
  if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (buffer))) {
    if (GST_CLOCK_TIME_IS_VALID (pad_monitor->timestamp_range_start)) {
      pad_monitor->timestamp_range_start =
          MIN (pad_monitor->timestamp_range_start,
          GST_BUFFER_TIMESTAMP (buffer));
    } else {
      pad_monitor->timestamp_range_start = GST_BUFFER_TIMESTAMP (buffer);
    }

    if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_DURATION (buffer))) {
      GstClockTime endts =
          GST_BUFFER_TIMESTAMP (buffer) + GST_BUFFER_DURATION (buffer);
      if (GST_CLOCK_TIME_IS_VALID (pad_monitor->timestamp_range_end)) {
        pad_monitor->timestamp_range_end =
            MAX (pad_monitor->timestamp_range_end, endts);
      } else {
        pad_monitor->timestamp_range_end = endts;
      }
    }
  }
  GST_DEBUG_OBJECT (pad_monitor, "Current stored range: %" GST_TIME_FORMAT
      " - %" GST_TIME_FORMAT,
      GST_TIME_ARGS (pad_monitor->timestamp_range_start),
      GST_TIME_ARGS (pad_monitor->timestamp_range_end));
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
        peerpad = gst_pad_get_peer (otherpad);
        if (peerpad) {
          othermonitor = g_object_get_data ((GObject *) peerpad, "qa-monitor");
          if (othermonitor) {
            found_a_pad = TRUE;
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
    GST_QA_MONITOR_REPORT_CRITICAL (monitor, TRUE, BUFFER, UNEXPECTED,
        "Wrong combined flow return %s(%d). Expected: %s(%d)",
        gst_flow_get_name (ret), ret,
        gst_flow_get_name (aggregated), aggregated);
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
          GST_QA_MONITOR_REPORT_WARNING (othermonitor, FALSE, EVENT, EXPECTED,
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
                GST_QA_MONITOR_REPORT_WARNING (pad_monitor, TRUE, EVENT,
                    EXPECTED,
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
          GST_QA_MONITOR_REPORT_ISSUE (pad_monitor, TRUE, EVENT, SEQNUM,
              "The expected flush-start seqnum should be the same as the "
              "one from the event that caused it (probably a seek). Got: %u."
              " Expected: %u", seqnum, pad_monitor->pending_flush_start_seqnum);
        }
      }

      if (pad_monitor->pending_flush_stop) {
        GST_QA_MONITOR_REPORT_ISSUE (pad_monitor, TRUE, EVENT, UNEXPECTED,
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
          GST_QA_MONITOR_REPORT_ISSUE (pad_monitor, TRUE, EVENT, SEQNUM,
              "The expected flush-stop seqnum should be the same as the "
              "one from the event that caused it (probably a seek). Got: %u."
              " Expected: %u", seqnum, pad_monitor->pending_flush_stop_seqnum);
        }
      }

      if (!pad_monitor->pending_flush_stop) {
        GST_QA_MONITOR_REPORT_ISSUE (pad_monitor, TRUE, EVENT, UNEXPECTED,
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
          GST_QA_MONITOR_REPORT_ISSUE (pad_monitor, TRUE, EVENT, SEQNUM,
              "The expected flush-start seqnum should be the same as the "
              "one from the event that caused it (probably a seek). Got: %u."
              " Expected: %u", seqnum, pad_monitor->pending_flush_start_seqnum);
        }
      } else {
        GST_QA_MONITOR_REPORT_CRITICAL (pad_monitor, TRUE, EVENT, UNEXPECTED,
            "Received unexpected flush-start");
      }

      if (pad_monitor->pending_flush_stop) {
        GST_QA_MONITOR_REPORT_ISSUE (pad_monitor, TRUE, EVENT, UNEXPECTED,
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
          GST_QA_MONITOR_REPORT_ISSUE (pad_monitor, TRUE, EVENT, SEQNUM,
              "The expected flush-stop seqnum should be the same as the "
              "one from the event that caused it (probably a seek). Got: %u."
              " Expected: %u", seqnum, pad_monitor->pending_flush_stop_seqnum);
        }
      }

      if (!pad_monitor->pending_flush_stop) {
        GST_QA_MONITOR_REPORT_ISSUE (pad_monitor, TRUE, EVENT, UNEXPECTED,
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
        GST_QA_MONITOR_REPORT_ISSUE (monitor, FALSE, BUFFER, TIMESTAMP,
            "buffer is out of segment and shouldn't be pushed. Timestamp: %"
            GST_TIME_FORMAT " - duration: %" GST_TIME_FORMAT
            ". Range: %" GST_TIME_FORMAT " - %" GST_TIME_FORMAT,
            GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
            GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)),
            GST_TIME_ARGS (monitor->segment.start),
            GST_TIME_ARGS (monitor->segment.stop));
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
    gst_qa_pad_monitor_check_caps_fields_proxied (pad_monitor, ret);
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
