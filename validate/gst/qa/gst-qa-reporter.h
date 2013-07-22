/* GStreamer
 *
 * Copyright (C) 2013 Thibault Saunier <thibault.saunier@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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
#ifndef _GST_QA_REPORTER_
#define _GST_QA_REPORTER_

#include <glib-object.h>
#include "gst-qa-runner.h"

G_BEGIN_DECLS

typedef struct _GstQaReporter GstQaReporter;
typedef struct _GstQaReporterInterface GstQaReporterInterface;

/* GstQaReporter interface declarations */
#define GST_TYPE_QA_REPORTER                (gst_qa_reporter_get_type ())
#define GST_QA_REPORTER(obj)                (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_QA_REPORTER, GstQaReporter))
#define GST_IS_QA_REPORTER(obj)             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_QA_REPORTER))
#define GST_QA_REPORTER_GET_INTERFACE(inst) (G_TYPE_INSTANCE_GET_INTERFACE ((inst), GST_TYPE_QA_REPORTER, GESExtractableInterface))

#ifdef G_HAVE_ISO_VARARGS
#define GST_QA_REPORT(m, repeat, status, area, subarea, ...)           \
G_STMT_START {                                                                 \
  gst_qa_report (GST_QA_REPORTER (m), repeat,                        \
    GST_QA_REPORT_LEVEL_ ## status, GST_QA_AREA_ ## area,                      \
    GST_QA_AREA_ ## area ## _ ## subarea, __VA_ARGS__ );                       \
} G_STMT_END

#define GST_QA_REPORT_CRITICAL(m, repeat, area, subarea, ...)          \
G_STMT_START {                                                                 \
  GST_ERROR_OBJECT (m, "Critical report: %s: %s: %s",                          \
      #area, #subarea, __VA_ARGS__);                                           \
  GST_QA_REPORT(m, repeat, CRITICAL, area, subarea, __VA_ARGS__);      \
} G_STMT_END

#define GST_QA_REPORT_WARNING(m, repeat, area, subarea, ...)           \
G_STMT_START {                                                                 \
  GST_WARNING_OBJECT (m, "Warning report: %s: %s: %s",                         \
      #area, #subarea, __VA_ARGS__);                                           \
  GST_QA_REPORT(m, repeat, WARNING, area, subarea, __VA_ARGS__);       \
} G_STMT_END

#define GST_QA_REPORT_ISSUE(m, repeat, area, subarea, ...)             \
G_STMT_START {                                                                 \
  GST_WARNING_OBJECT (m, "Issue report: %s: %s: %s",                           \
      #area, #subarea, __VA_ARGS__);                                           \
  GST_QA_REPORT(m, repeat, ISSUE, area, subarea, __VA_ARGS__);         \
} G_STMT_END
#else /* G_HAVE_GNUC_VARARGS */
#ifdef G_HAVE_GNUC_VARARGS
#define GST_QA_REPORT(m, repeat, status, area, subarea, args...)       \
G_STMT_START {                                                                 \
  gst_qa_reporter_do_report (GST_QA_REPORTER (m),                                \
    GST_QA_REPORT_LEVEL_ ## status, GST_QA_AREA_ ## area,                      \
    GST_QA_AREA_ ## area ## _ ## subarea, ##args );                            \
} G_STMT_END

#define GST_QA_REPORT_CRITICAL(m, repeat, area, subarea, args...)      \
G_STMT_START {                                                                 \
  GST_ERROR_OBJECT (m, "Critical report: %s: %s: %s",                          \
      #area, #subarea, ##args);                                                \
  GST_QA_REPORT(m, repeat, CRITICAL, area, subarea, ##args);           \
} G_STMT_END

#define GST_QA_REPORT_WARNING(m, repeat, area, subarea, args...)       \
G_STMT_START {                                                                 \
  GST_WARNING_OBJECT (m, "Warning report: %s: %s: %s",                         \
      #area, #subarea, ##args);                                                \
  GST_QA_REPORT(m, repeat, WARNING, area, subarea, ##args);            \
} G_STMT_END

#define GST_QA_REPORT_ISSUE(m, repeat, area, subarea, args...)         \
G_STMT_START {                                                                 \
  GST_WARNING_OBJECT (m, "Issue report: %s: %s: %s",                           \
      #area, #subarea, ##args);                                                \
  GST_QA_REPORT(m, repeat, ISSUE, area, subarea, ##args);              \
} G_STMT_END
#endif /* G_HAVE_ISO_VARARGS */
#endif /* G_HAVE_GNUC_VARARGS */

GType gst_qa_reporter_get_type (void);

/**
 * GstQaReporter:
 */
struct _GstQaReporterInterface
{
  GTypeInterface parent;
};

void gst_qa_reporter_set_name            (GstQaReporter * reporter,
                                          const gchar * name);
GstQaRunner * gst_qa_reporter_get_runner (GstQaReporter *reporter);
void gst_qa_reporter_init                (GstQaReporter * reporter, const gchar *name);
void gst_qa_report                       (GstQaReporter * reporter, gboolean repeat,
                                          GstQaReportLevel level, GstQaReportArea area,
                                          gint subarea, const gchar * format, ...);
void gst_qa_report_valist                (GstQaReporter * reporter, gboolean repeat,
                                          GstQaReportLevel level, GstQaReportArea area,
                                          gint subarea, const gchar * format, va_list var_args);

G_END_DECLS
#endif /* _GST_QA_REPORTER_ */



