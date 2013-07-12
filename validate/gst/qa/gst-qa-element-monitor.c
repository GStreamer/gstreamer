/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-qa-element-monitor.c - QA ElementMonitor class
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

#include "gst-qa-element-monitor.h"
#include "gst-qa-pad-monitor.h"
#include <string.h>

/**
 * SECTION:gst-qa-element-monitor
 * @short_description: Class that wraps a #GstElement for QA checks
 *
 * TODO
 */

GST_DEBUG_CATEGORY_STATIC (gst_qa_element_monitor_debug);
#define GST_CAT_DEFAULT gst_qa_element_monitor_debug

#define _do_init \
  GST_DEBUG_CATEGORY_INIT (gst_qa_element_monitor_debug, "qa_element_monitor", 0, "QA ElementMonitor");
#define gst_qa_element_monitor_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstQaElementMonitor, gst_qa_element_monitor,
    GST_TYPE_QA_MONITOR, _do_init);

static void
gst_qa_element_monitor_wrap_pad (GstQaElementMonitor * monitor, GstPad * pad);
static gboolean gst_qa_element_monitor_do_setup (GstQaMonitor * monitor);

static void
_qa_element_pad_added (GstElement * element, GstPad * pad,
    GstQaElementMonitor * monitor);

static void
gst_qa_element_monitor_dispose (GObject * object)
{
  GstQaElementMonitor *monitor = GST_QA_ELEMENT_MONITOR_CAST (object);

  if (monitor->pad_added_id)
    g_signal_handler_disconnect (GST_QA_MONITOR_GET_OBJECT (monitor),
        monitor->pad_added_id);

  g_list_free_full (monitor->pad_monitors, g_object_unref);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}


static void
gst_qa_element_monitor_class_init (GstQaElementMonitorClass * klass)
{
  GObjectClass *gobject_class;
  GstQaMonitorClass *monitor_klass;

  gobject_class = G_OBJECT_CLASS (klass);
  monitor_klass = GST_QA_MONITOR_CLASS (klass);

  gobject_class->dispose = gst_qa_element_monitor_dispose;

  monitor_klass->setup = gst_qa_element_monitor_do_setup;
}

static void
gst_qa_element_monitor_init (GstQaElementMonitor * element_monitor)
{
}

/**
 * gst_qa_element_monitor_new:
 * @element: (transfer-none): a #GstElement to run QA on
 */
GstQaElementMonitor *
gst_qa_element_monitor_new (GstElement * element, GstQaRunner * runner)
{
  GstQaElementMonitor *monitor;

  g_return_val_if_fail (element != NULL, NULL);

  monitor = g_object_new (GST_TYPE_QA_ELEMENT_MONITOR, "object", element,
      "qa-runner", runner, NULL);

  if (GST_QA_ELEMENT_MONITOR_GET_ELEMENT (monitor) == NULL) {
    g_object_unref (monitor);
    return NULL;
  }

  return monitor;
}

static void
gst_qa_element_monitor_inspect (GstQaElementMonitor * monitor)
{
  GstElement *element;
  GstElementClass *klass;

  element = GST_QA_ELEMENT_MONITOR_GET_ELEMENT (monitor);
  klass = GST_ELEMENT_CLASS (G_OBJECT_GET_CLASS (element));

  monitor->is_decoder = strstr (klass->details.klass, "Decoder") != NULL;
}

static gboolean
gst_qa_element_monitor_do_setup (GstQaMonitor * monitor)
{
  GstIterator *iterator;
  gboolean done;
  GstPad *pad;
  GstQaElementMonitor *elem_monitor;
  GstElement *element;

  if (!GST_IS_ELEMENT (GST_QA_MONITOR_GET_OBJECT (monitor))) {
    GST_WARNING_OBJECT (monitor, "Trying to create element monitor with other "
        "type of object");
    return FALSE;
  }

  elem_monitor = GST_QA_ELEMENT_MONITOR_CAST (monitor);

  GST_DEBUG_OBJECT (monitor, "Setting up monitor for element %" GST_PTR_FORMAT,
      GST_QA_MONITOR_GET_OBJECT (monitor));
  element = GST_QA_ELEMENT_MONITOR_GET_ELEMENT (monitor);

  gst_qa_element_monitor_inspect (elem_monitor);

  elem_monitor->pad_added_id = g_signal_connect (element, "pad-added",
      G_CALLBACK (_qa_element_pad_added), monitor);

  iterator = gst_element_iterate_pads (element);
  done = FALSE;
  while (!done) {
    switch (gst_iterator_next (iterator, (gpointer *) & pad)) {
      case GST_ITERATOR_OK:
        gst_qa_element_monitor_wrap_pad (elem_monitor, pad);
        gst_object_unref (pad);
        break;
      case GST_ITERATOR_RESYNC:
        /* TODO how to handle this? */
        gst_iterator_resync (iterator);
        break;
      case GST_ITERATOR_ERROR:
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (iterator);
  return TRUE;
}

static void
gst_qa_element_monitor_wrap_pad (GstQaElementMonitor * monitor, GstPad * pad)
{
  GstQaPadMonitor *pad_monitor;
  GST_DEBUG_OBJECT (monitor, "Wrapping pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  pad_monitor =
      gst_qa_pad_monitor_new (pad, GST_QA_MONITOR_GET_RUNNER (monitor));
  g_return_if_fail (pad_monitor != NULL);

  GST_QA_MONITOR_LOCK (monitor);
  monitor->pad_monitors = g_list_prepend (monitor->pad_monitors, pad_monitor);
  GST_QA_MONITOR_UNLOCK (monitor);
}

static void
_qa_element_pad_added (GstElement * element, GstPad * pad,
    GstQaElementMonitor * monitor)
{
  g_return_if_fail (GST_QA_ELEMENT_MONITOR_GET_ELEMENT (monitor) == element);
  gst_qa_element_monitor_wrap_pad (monitor, pad);
}
