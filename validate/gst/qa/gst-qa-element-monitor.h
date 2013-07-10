/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-qa-element-monitor.h - QA ElementMonitor class
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

#ifndef __GST_QA_ELEMENT_MONITOR_H__
#define __GST_QA_ELEMENT_MONITOR_H__

#include <glib-object.h>
#include <gst/gst.h>

#include "gst-qa-monitor.h"

G_BEGIN_DECLS

#define GST_TYPE_QA_ELEMENT_MONITOR			(gst_qa_element_monitor_get_type ())
#define GST_IS_QA_ELEMENT_MONITOR(obj)		        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_QA_ELEMENT_MONITOR))
#define GST_IS_QA_ELEMENT_MONITOR_CLASS(klass)	        (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_QA_ELEMENT_MONITOR))
#define GST_QA_ELEMENT_MONITOR_GET_CLASS(obj)	        (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_QA_ELEMENT_MONITOR, GstQaElementMonitorClass))
#define GST_QA_ELEMENT_MONITOR(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_QA_ELEMENT_MONITOR, GstQaElementMonitor))
#define GST_QA_ELEMENT_MONITOR_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_QA_ELEMENT_MONITOR, GstQaElementMonitorClass))
#define GST_QA_ELEMENT_MONITOR_CAST(obj)                ((GstQaElementMonitor*)(obj))
#define GST_QA_ELEMENT_MONITOR_CLASS_CAST(klass)        ((GstQaElementMonitorClass*)(klass))

#define GST_QA_ELEMENT_MONITOR_GET_ELEMENT(m) (GST_ELEMENT_CAST (GST_QA_MONITOR_GET_OBJECT (m)))

typedef struct _GstQaElementMonitor GstQaElementMonitor;
typedef struct _GstQaElementMonitorClass GstQaElementMonitorClass;

/**
 * GstQaElementMonitor:
 *
 * GStreamer QA ElementMonitor class.
 *
 * Class that wraps a #GstElement for QA checks
 */
struct _GstQaElementMonitor {
  GstQaMonitor 	 parent;

  /*< private >*/
  gulong         pad_added_id;
  GList         *pad_monitors;
};

/**
 * GstQaElementMonitorClass:
 * @parent_class: parent
 *
 * GStreamer QA ElementMonitor object class.
 */
struct _GstQaElementMonitorClass {
  GstQaMonitorClass	parent_class;
};

/* normal GObject stuff */
GType		gst_qa_element_monitor_get_type		(void);

GstQaElementMonitor *   gst_qa_element_monitor_new      (GstElement * element);

G_END_DECLS

#endif /* __GST_QA_ELEMENT_MONITOR_H__ */

