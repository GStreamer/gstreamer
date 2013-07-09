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
    G_TYPE_OBJECT, _do_init);


static void
gst_qa_pad_monitor_dispose (GObject * object)
{
  GstQaPadMonitor *monitor = GST_QA_PAD_MONITOR_CAST (object);

  if (monitor->pad)
    gst_object_unref (monitor->pad);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}


static void
gst_qa_pad_monitor_class_init (GstQaPadMonitorClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = gst_qa_pad_monitor_dispose;
}

static void
gst_qa_pad_monitor_init (GstQaPadMonitor * pad_monitor)
{
  pad_monitor->setup = FALSE;
}

/**
 * gst_qa_pad_monitor_new:
 * @pad: (transfer-none): a #GstPad to run QA on
 */
GstQaPadMonitor *
gst_qa_pad_monitor_new (GstPad * pad)
{
  GstQaPadMonitor *monitor = g_object_new (GST_TYPE_QA_PAD_MONITOR, NULL);

  g_return_val_if_fail (pad != NULL, NULL);

  monitor->pad = gst_object_ref (pad);
  return monitor;
}

gboolean
gst_qa_pad_monitor_setup (GstQaPadMonitor * monitor)
{
  if (monitor->setup)
    return TRUE;

  GST_DEBUG_OBJECT (monitor, "Setting up monitor for pad %" GST_PTR_FORMAT,
      monitor->pad);

  monitor->setup = TRUE;
  return TRUE;
}
