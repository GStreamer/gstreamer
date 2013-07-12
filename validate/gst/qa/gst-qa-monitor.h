/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-qa-monitor.h - QA Monitor abstract base class
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

#ifndef __GST_QA_MONITOR_H__
#define __GST_QA_MONITOR_H__

#include <glib-object.h>
#include <gst/gst.h>
#include "gst-qa-report.h"
#include "gst-qa-runner.h"

G_BEGIN_DECLS

#define GST_TYPE_QA_MONITOR			(gst_qa_monitor_get_type ())
#define GST_IS_QA_MONITOR(obj)		        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_QA_MONITOR))
#define GST_IS_QA_MONITOR_CLASS(klass)	        (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_QA_MONITOR))
#define GST_QA_MONITOR_GET_CLASS(obj)	        (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_QA_MONITOR, GstQaMonitorClass))
#define GST_QA_MONITOR(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_QA_MONITOR, GstQaMonitor))
#define GST_QA_MONITOR_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_QA_MONITOR, GstQaMonitorClass))
#define GST_QA_MONITOR_CAST(obj)                ((GstQaMonitor*)(obj))
#define GST_QA_MONITOR_CLASS_CAST(klass)        ((GstQaMonitorClass*)(klass))

#define GST_QA_MONITOR_GET_OBJECT(m) (GST_QA_MONITOR_CAST (m)->target)
#define GST_QA_MONITOR_GET_RUNNER(m) (GST_QA_MONITOR_CAST (m)->runner)
#define GST_QA_MONITOR_GET_PARENT(m) (GST_QA_MONITOR_CAST (m)->parent)
#define GST_QA_MONITOR_LOCK(m) (g_mutex_lock (&GST_QA_MONITOR_CAST(m)->mutex))
#define GST_QA_MONITOR_UNLOCK(m) (g_mutex_unlock (&GST_QA_MONITOR_CAST(m)->mutex))

typedef struct _GstQaMonitor GstQaMonitor;
typedef struct _GstQaMonitorClass GstQaMonitorClass;

/**
 * GstQaMonitor:
 *
 * GStreamer QA Monitor class.
 *
 * Class that wraps a #GObject for QA checks
 */
struct _GstQaMonitor {
  GObject 	 object;

  GstObject     *target;
  GMutex         mutex;

  GstQaMonitor  *parent;

  GstQaRunner   *runner;

  /*< private >*/
};

/**
 * GstQaMonitorClass:
 * @parent_class: parent
 *
 * GStreamer QA Monitor object class.
 */
struct _GstQaMonitorClass {
  GObjectClass	parent_class;

  gboolean (* setup) (GstQaMonitor * monitor);
};

/* normal GObject stuff */
GType		gst_qa_monitor_get_type		(void);

void            gst_qa_monitor_post_error       (GstQaMonitor * monitor, GstQaErrorArea area, const gchar * message, const gchar * detail);

G_END_DECLS

#endif /* __GST_QA_MONITOR_H__ */

