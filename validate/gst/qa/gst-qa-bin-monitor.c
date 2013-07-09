/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-qa-bin-monitor.c - QA BinMonitor class
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

#include "gst-qa-bin-monitor.h"

/**
 * SECTION:gst-qa-bin-monitor
 * @short_description: Class that wraps a #GstBin for QA checks
 *
 * TODO
 */

GST_DEBUG_CATEGORY_STATIC (gst_qa_bin_monitor_debug);
#define GST_CAT_DEFAULT gst_qa_bin_monitor_debug

#define _do_init \
  GST_DEBUG_CATEGORY_INIT (gst_qa_bin_monitor_debug, "qa_bin_monitor", 0, "QA BinMonitor");
#define gst_qa_bin_monitor_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstQaBinMonitor, gst_qa_bin_monitor,
    G_TYPE_OBJECT, _do_init);

static void
gst_qa_bin_monitor_wrap_element (GstQaBinMonitor * monitor,
    GstElement * element);
static gboolean gst_qa_bin_monitor_setup (GstQaElementMonitor * monitor);

static void
_qa_bin_element_added (GstBin * bin, GstElement * pad,
    GstQaBinMonitor * monitor);

static void
gst_qa_bin_monitor_dispose (GObject * object)
{
  GstQaBinMonitor *monitor = GST_QA_BIN_MONITOR_CAST (object);
  GstQaElementMonitor *element_monitor = GST_QA_ELEMENT_MONITOR_CAST (object);
  GstElement *bin = element_monitor->element;

  if (monitor->element_added_id)
    g_signal_handler_disconnect (bin, monitor->element_added_id);

  g_list_free_full (monitor->element_monitors, g_object_unref);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}


static void
gst_qa_bin_monitor_class_init (GstQaBinMonitorClass * klass)
{
  GObjectClass *gobject_class;
  GstQaElementMonitorClass *gstqaelementmonitor_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstqaelementmonitor_class = GST_QA_ELEMENT_MONITOR_CLASS_CAST (klass);

  gobject_class->dispose = gst_qa_bin_monitor_dispose;

  gstqaelementmonitor_class->setup = gst_qa_bin_monitor_setup;
}

static void
gst_qa_bin_monitor_init (GstQaBinMonitor * bin_monitor)
{
}

/**
 * gst_qa_bin_monitor_new:
 * @bin: (transfer-none): a #GstBin to run QA on
 */
GstQaBinMonitor *
gst_qa_bin_monitor_new (GstBin * bin)
{
  GstQaBinMonitor *monitor = g_object_new (GST_TYPE_QA_BIN_MONITOR, NULL);
  GstQaElementMonitor *element_monitor = GST_QA_ELEMENT_MONITOR_CAST (bin);

  g_return_val_if_fail (bin != NULL, NULL);

  element_monitor->element = gst_object_ref (bin);
  return monitor;
}

static gboolean
gst_qa_bin_monitor_setup (GstQaElementMonitor * element_monitor)
{
  GstIterator *iterator;
  gboolean done;
  GstElement *element;
  GstQaBinMonitor *monitor = GST_QA_BIN_MONITOR_CAST (element_monitor);

  GST_DEBUG_OBJECT (monitor, "Setting up monitor for bin %" GST_PTR_FORMAT,
      element_monitor->element);

  monitor->element_added_id =
      g_signal_connect (GST_BIN_CAST (element_monitor->element),
      "element-added", G_CALLBACK (_qa_bin_element_added), monitor);

  iterator = gst_bin_iterate_elements (GST_BIN_CAST (element_monitor->element));
  done = FALSE;
  while (!done) {
    switch (gst_iterator_next (iterator, (gpointer *) & element)) {
      case GST_ITERATOR_OK:
        gst_qa_bin_monitor_wrap_element (monitor, element);
        gst_object_unref (element);
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
gst_qa_bin_monitor_wrap_element (GstQaBinMonitor * monitor,
    GstElement * element)
{
  GST_DEBUG_OBJECT (monitor, "Wrapping element %s", GST_ELEMENT_NAME (element));
  /* TODO */
}

static void
_qa_bin_element_added (GstBin * bin, GstElement * element,
    GstQaBinMonitor * monitor)
{
  GstQaElementMonitor *element_monitor = GST_QA_ELEMENT_MONITOR_CAST (monitor);
  g_return_if_fail (element_monitor->element == GST_ELEMENT_CAST (bin));
  gst_qa_bin_monitor_wrap_element (monitor, element);
}
