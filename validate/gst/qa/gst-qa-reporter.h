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
#include "gst-qa-report.h"

G_BEGIN_DECLS

typedef struct _GstQaReporter GstQaReporter;
typedef struct _GstQaReporterInterface GstQaReporterInterface;

/* GstQaReporter interface declarations */
#define GST_TYPE_QA_REPORTER                (gst_qa_reporter_get_type ())
#define GST_QA_REPORTER(obj)                (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_QA_REPORTER, GstQaReporter))
#define GST_IS_QA_REPORTER(obj)             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_QA_REPORTER))
#define GST_QA_REPORTER_GET_INTERFACE(inst) (G_TYPE_INSTANCE_GET_INTERFACE ((inst), GST_TYPE_QA_REPORTER, GstQaReporterInterface))

#ifdef G_HAVE_ISO_VARARGS
#define GST_QA_REPORT(m, issue_id, ...)                              \
G_STMT_START {                                                       \
  gst_qa_report (GST_QA_REPORTER (m), issue_id,                      \
      __VA_ARGS__ );                                                 \
} G_STMT_END

#else /* G_HAVE_GNUC_VARARGS */
#ifdef G_HAVE_GNUC_VARARGS
#define GST_QA_REPORT(m, issue_id, args...)                          \
G_STMT_START {                                                       \
  gst_qa_report (GST_QA_REPORTER (m),                                \
    issue_id, ##args );                                              \
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

  void (*intercept_report)(GstQaReporter * reporter, GstQaReport * report);
};

void gst_qa_reporter_set_name            (GstQaReporter * reporter,
                                          gchar * name);
const gchar * gst_qa_reporter_get_name            (GstQaReporter * reporter);
GstQaRunner * gst_qa_reporter_get_runner (GstQaReporter *reporter);
void gst_qa_reporter_init                (GstQaReporter * reporter, const gchar *name);
void gst_qa_report                       (GstQaReporter * reporter, GstQaIssueId issue_id,
                                          const gchar * format, ...);
void gst_qa_report_valist                (GstQaReporter * reporter, GstQaIssueId issue_id,
                                          const gchar * format, va_list var_args);

void gst_qa_reporter_set_runner          (GstQaReporter * reporter,
                                          GstQaRunner *runner);

G_END_DECLS
#endif /* _GST_QA_REPORTER_ */
