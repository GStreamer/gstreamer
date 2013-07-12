/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-qa-monitor-factory.c - QA Element monitors factory utility functions
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

#include "gst-qa-monitor-factory.h"
#include "gst-qa-bin-monitor.h"

GstQaElementMonitor *
gst_qa_monitor_factory_create (GstElement * element, GstQaRunner * runner,
    GstQaMonitor * parent)
{
  g_return_val_if_fail (element != NULL, NULL);

  if (GST_IS_BIN (element)) {
    return
        GST_QA_ELEMENT_MONITOR_CAST (gst_qa_bin_monitor_new (GST_BIN_CAST
            (element), runner, parent));
  }

  return gst_qa_element_monitor_new (element, runner, parent);
}
