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

#ifdef G_HAVE_ISO_VARARGS
#define GST_QA_MONITOR_REPORT(m, status, area, subarea, ...)                   \
G_STMT_START {                                                                 \
  gst_qa_monitor_do_report (GST_QA_MONITOR (m),                                \
    GST_QA_REPORT_LEVEL_ ## status, GST_QA_AREA_ ## area,                      \
    GST_QA_AREA_ ## area ## _ ## subarea, __VA_ARGS__ );                       \
} G_STMT_END

#define GST_QA_MONITOR_REPORT_CRITICAL(m, area, subarea, ...)                  \
G_STMT_START {                                                                 \
  GST_ERROR_OBJECT (m, "Critical report: %s: %s: %s",                          \
      #area, #subarea, __VA_ARGS__);                                           \
  GST_QA_MONITOR_REPORT(m, CRITICAL, area, subarea, __VA_ARGS__);              \
} G_STMT_END

#define GST_QA_MONITOR_REPORT_WARNING(m, area, subarea, ...)                   \
G_STMT_START {                                                                 \
  GST_WARNING_OBJECT (m, "Warning report: %s: %s: %s",                         \
      #area, #subarea, __VA_ARGS__);                                           \
  GST_QA_MONITOR_REPORT(m, WARNING, area, subarea, __VA_ARGS__);               \
} G_STMT_END

#define GST_QA_MONITOR_REPORT_ISSUE(m, area, subarea, ...)                     \
G_STMT_START {                                                                 \
  GST_WARNING_OBJECT (m, "Issue report: %s: %s: %s",                           \
      #area, #subarea, __VA_ARGS__);                                           \
  GST_QA_MONITOR_REPORT(m, ISSUE, area, subarea, __VA_ARGS__);                 \
} G_STMT_END
#else /* G_HAVE_GNUC_VARARGS */
#ifdef G_HAVE_GNUC_VARARGS
#define GST_QA_MONITOR_REPORT(m, status, area, subarea, args...)                   \
G_STMT_START {                                                                 \
  gst_qa_monitor_do_report (GST_QA_MONITOR (m),                                \
    GST_QA_REPORT_LEVEL_ ## status, GST_QA_AREA_ ## area,                      \
    GST_QA_AREA_ ## area ## _ ## subarea, ##args );                       \
} G_STMT_END

#define GST_QA_MONITOR_REPORT_CRITICAL(m, area, subarea, args...)                  \
G_STMT_START {                                                                 \
  GST_ERROR_OBJECT (m, "Critical report: %s: %s: %s",                          \
      #area, #subarea, ##args);                                           \
  GST_QA_MONITOR_REPORT(m, CRITICAL, area, subarea, ##args);              \
} G_STMT_END

#define GST_QA_MONITOR_REPORT_WARNING(m, area, subarea, args...)                   \
G_STMT_START {                                                                 \
  GST_WARNING_OBJECT (m, "Warning report: %s: %s: %s",                         \
      #area, #subarea, ##args);                                           \
  GST_QA_MONITOR_REPORT(m, WARNING, area, subarea, ##args);               \
} G_STMT_END

#define GST_QA_MONITOR_REPORT_ISSUE(m, area, subarea, args...)                     \
G_STMT_START {                                                                 \
  GST_WARNING_OBJECT (m, "Issue report: %s: %s: %s",                           \
      #area, #subarea, ##args);                                           \
  GST_QA_MONITOR_REPORT(m, ISSUE, area, subarea, ##args);                 \
} G_STMT_END
#endif /* G_HAVE_ISO_VARARGS */
#endif /* G_HAVE_GNUC_VARARGS */

/* #else TODO Implemen no variadic macros, use inline,
 * Problem being:
 *     GST_QA_REPORT_LEVEL_ ## status
 *     GST_QA_AREA_ ## area ## _ ## subarea
 */

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
  gchar         *target_name;

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

void    gst_qa_monitor_do_report     (GstQaMonitor * monitor,
                                      GstQaReportLevel level, GstQaReportArea area,
                                      gint subarea, const gchar * format, ...);

void gst_qa_monitor_do_report_valist (GstQaMonitor * monitor,
                                      GstQaReportLevel level, GstQaReportArea area,
                                      gint subarea, const gchar *format,
                                      va_list var_args);

void gst_qa_monitor_set_target_name   (GstQaMonitor *monitor,
                                       gchar *target_name);

G_END_DECLS

#endif /* __GST_QA_MONITOR_H__ */

