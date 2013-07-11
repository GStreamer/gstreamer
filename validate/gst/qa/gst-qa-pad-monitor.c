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

static void
gst_qa_pad_monitor_dispose (GObject * object)
{
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
}

/**
 * gst_qa_pad_monitor_new:
 * @pad: (transfer-none): a #GstPad to run QA on
 */
GstQaPadMonitor *
gst_qa_pad_monitor_new (GstPad * pad)
{
  GstQaPadMonitor *monitor = g_object_new (GST_TYPE_QA_PAD_MONITOR,
      "object", pad, NULL);

  if (GST_QA_PAD_MONITOR_GET_PAD (monitor) == NULL) {
    g_object_unref (monitor);
    return NULL;
  }
  return monitor;
}

static GstFlowReturn
gst_qa_pad_monitor_chain_func (GstPad * pad, GstBuffer * buffer)
{
  GstQaPadMonitor *pad_monitor =
      g_object_get_data ((GObject *) pad, "qa-monitor");
  GstFlowReturn ret;
  ret = pad_monitor->chain_func (pad, buffer);
  return ret;
}

static gboolean
gst_qa_pad_monitor_event_func (GstPad * pad, GstEvent * event)
{
  GstQaPadMonitor *pad_monitor =
      g_object_get_data ((GObject *) pad, "qa-monitor");
  GstFlowReturn ret;
  gboolean update;
  gdouble rate, applied_rate;
  GstFormat format;
  gint64 start, stop, position;
  GstSeekFlags seek_flags;
  GstSeekType start_type, stop_type;
  guint32 seqnum = gst_event_get_seqnum (event);

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
      break;
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
          /* TODO error */
        }
      }

      if (pad_monitor->pending_flush_stop) {
        /* TODO ERROR, do report */
      }
    }
      break;
    case GST_EVENT_FLUSH_STOP:
    {
      if (pad_monitor->pending_flush_stop_seqnum) {
        if (seqnum == pad_monitor->pending_flush_stop_seqnum) {
          pad_monitor->pending_flush_stop_seqnum = 0;
        } else {
          /* TODO error */
        }
      }

      if (!pad_monitor->pending_flush_stop) {
        /* TODO ERROR, do report */
      }
    }
      break;
    case GST_EVENT_NAVIGATION:
    case GST_EVENT_LATENCY:
    case GST_EVENT_STEP:
    case GST_EVENT_EOS:
    case GST_EVENT_TAG:
    case GST_EVENT_SINK_MESSAGE:
    case GST_EVENT_QOS:
    default:
      break;
  }

  gst_event_ref (event);
  ret = pad_monitor->event_func (pad, event);

  /* post checks */
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
      if (ret) {
        gst_segment_set_newsegment_full (&pad_monitor->segment, update, rate,
            applied_rate, format, start, stop, position);
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
    case GST_EVENT_QOS:
    case GST_EVENT_SEEK:
    case GST_EVENT_NAVIGATION:
    case GST_EVENT_LATENCY:
    case GST_EVENT_STEP:
    default:
      break;
  }

  gst_event_unref (event);
  return ret;
}

static gboolean
gst_qa_pad_monitor_query_func (GstPad * pad, GstQuery * query)
{
  GstQaPadMonitor *pad_monitor =
      g_object_get_data ((GObject *) pad, "qa-monitor");
  GstFlowReturn ret;
  ret = pad_monitor->query_func (pad, query);
  return ret;
}

static gboolean
gst_qa_pad_buffer_alloc_func (GstPad * pad, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buffer)
{
  GstQaPadMonitor *pad_monitor =
      g_object_get_data ((GObject *) pad, "qa-monitor");
  GstFlowReturn ret;
  ret = pad_monitor->bufferalloc_func (pad, offset, size, caps, buffer);
  return ret;
}

static gboolean
gst_qa_pad_get_range_func (GstPad * pad, guint64 offset, guint size,
    GstBuffer ** buffer)
{
  GstQaPadMonitor *pad_monitor =
      g_object_get_data ((GObject *) pad, "qa-monitor");
  GstFlowReturn ret;
  ret = pad_monitor->getrange_func (pad, offset, size, buffer);
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

  if (GST_PAD_DIRECTION (pad) == GST_PAD_SINK) {
    pad_monitor->bufferalloc_func = GST_PAD_BUFFERALLOCFUNC (pad);
    if (pad_monitor->bufferalloc_func)
      gst_pad_set_bufferalloc_function (pad, gst_qa_pad_buffer_alloc_func);

    pad_monitor->chain_func = GST_PAD_CHAINFUNC (pad);
    if (pad_monitor->chain_func)
      gst_pad_set_chain_function (pad, gst_qa_pad_monitor_chain_func);

  } else {
    pad_monitor->getrange_func = GST_PAD_GETRANGEFUNC (pad);
    if (pad_monitor->getrange_func)
      gst_pad_set_getrange_function (pad, gst_qa_pad_get_range_func);
  }
  pad_monitor->event_func = GST_PAD_EVENTFUNC (pad);
  pad_monitor->query_func = GST_PAD_QUERYFUNC (pad);
  gst_pad_set_event_function (pad, gst_qa_pad_monitor_event_func);
  gst_pad_set_query_function (pad, gst_qa_pad_monitor_query_func);

  return TRUE;
}
