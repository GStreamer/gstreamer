/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-qa-bin-monitor.h - QA BinMonitor class
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

#ifndef __GST_QA_BIN_MONITOR_H__
#define __GST_QA_BIN_MONITOR_H__

#include <glib-object.h>
#include <gst/gst.h>
#include "gst-qa-element-monitor.h"
#include "gst-qa-runner.h"
#include "gst-qa-scenario.h"

G_BEGIN_DECLS

#define GST_TYPE_QA_BIN_MONITOR			(gst_qa_bin_monitor_get_type ())
#define GST_IS_QA_BIN_MONITOR(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_QA_BIN_MONITOR))
#define GST_IS_QA_BIN_MONITOR_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_QA_BIN_MONITOR))
#define GST_QA_BIN_MONITOR_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_QA_BIN_MONITOR, GstQaBinMonitorClass))
#define GST_QA_BIN_MONITOR(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_QA_BIN_MONITOR, GstQaBinMonitor))
#define GST_QA_BIN_MONITOR_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_QA_BIN_MONITOR, GstQaBinMonitorClass))
#define GST_QA_BIN_MONITOR_CAST(obj)            ((GstQaBinMonitor*)(obj))
#define GST_QA_BIN_MONITOR_CLASS_CAST(klass)    ((GstQaBinMonitorClass*)(klass))

#define GST_QA_BIN_MONITOR_GET_BIN(m) (GST_BIN_CAST (GST_QA_ELEMENT_MONITOR_GET_ELEMENT (m)))

typedef struct _GstQaBinMonitor GstQaBinMonitor;
typedef struct _GstQaBinMonitorClass GstQaBinMonitorClass;

/**
 * GstQaBinMonitor:
 *
 * GStreamer QA BinMonitor class.
 *
 * Class that wraps a #GstBin for QA checks
 */
struct _GstQaBinMonitor {
  GstQaElementMonitor parent;

  GList *element_monitors;

  GstQaScenario *scenario;

  /*< private >*/
  gulong element_added_id;
};

/**
 * GstQaBinMonitorClass:
 * @parent_class: parent
 *
 * GStreamer QA BinMonitor object class.
 */
struct _GstQaBinMonitorClass {
  GstQaElementMonitorClass parent_class;
};

/* normal GObject stuff */
GType		gst_qa_bin_monitor_get_type		(void);

GstQaBinMonitor *   gst_qa_bin_monitor_new      (GstBin * bin, GstQaRunner * runner, GstQaMonitor * parent);

G_END_DECLS

#endif /* __GST_QA_BIN_MONITOR_H__ */

