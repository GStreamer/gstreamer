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
#ifndef _GST_VALIDATE_REPORTER_
#define _GST_VALIDATE_REPORTER_

typedef struct _GstValidateReporter GstValidateReporter;
typedef struct _GstValidateReporterInterface GstValidateReporterInterface;

#include <glib-object.h>
#include <gst/validate/validate-prelude.h>
#include <gst/validate/gst-validate-report.h>
#include <gst/validate/gst-validate-runner.h>
#include <gst/validate/gst-validate-enums.h>
#include <gst/validate/gst-validate-scenario.h>

G_BEGIN_DECLS

/* GstValidateReporter interface declarations */
#ifndef __GI_SCANNER__
#define GST_TYPE_VALIDATE_REPORTER                (gst_validate_reporter_get_type ())
#define GST_VALIDATE_REPORTER(obj)                (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VALIDATE_REPORTER, GstValidateReporter))
#define GST_IS_VALIDATE_REPORTER(obj)             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VALIDATE_REPORTER))
#define GST_VALIDATE_REPORTER_GET_INTERFACE(inst) (G_TYPE_INSTANCE_GET_INTERFACE ((inst), GST_TYPE_VALIDATE_REPORTER, GstValidateReporterInterface))
#define GST_VALIDATE_REPORTER_CAST(obj)           ((GstValidateReporter *) obj)
#endif

/**
 * GST_VALIDATE_REPORT:
 * @m: The #GstValidateReporter where the issue happened
 * @issue_id: The #GstValidateIssueId of the issue
 * @...: The format of the message describing the issue in a printf
 *       format, followed by the parameters.
 *
 * Reports a new issue in the GstValidate reporting system with @m
 * as the source of that issue.
 */
#define GST_VALIDATE_REPORT(m, issue_id, ...)				\
  G_STMT_START {							\
    gst_validate_report (GST_VALIDATE_REPORTER (m),			\
			 issue_id,		\
			 __VA_ARGS__ );					\
  } G_STMT_END

#define GST_VALIDATE_REPORT_ACTION(m, a, issue_id, ...)				\
  G_STMT_START {							\
    gst_validate_report_action (GST_VALIDATE_REPORTER (m), a,	\
			 issue_id,		\
			 __VA_ARGS__ );					\
  } G_STMT_END

GST_VALIDATE_API
GType gst_validate_reporter_get_type (void);

/**
 * GstValidateInterceptionReturn:
 * @GST_VALIDATE_REPORTER_DROP: The report will be completely ignored.
 * @GST_VALIDATE_REPORTER_KEEP: The report will be kept by the reporter,
 *                              but not reported to the runner.
 * @GST_VALIDATE_REPORTER_REPORT: The report will be kept by the reporter
 *                                and reported to the runner.
 */
typedef enum
{
  GST_VALIDATE_REPORTER_DROP,
  GST_VALIDATE_REPORTER_KEEP,
  GST_VALIDATE_REPORTER_REPORT
} GstValidateInterceptionReturn;

/**
 * GstValidateReporterInterface:
 * @parent: parent interface type.
 *
 */
struct _GstValidateReporterInterface
{
  GTypeInterface parent;

  GstValidateInterceptionReturn (*intercept_report)    (GstValidateReporter * reporter,
                                                        GstValidateReport   * report);
  GstValidateReportingDetails   (*get_reporting_level) (GstValidateReporter * reporter);
  GstPipeline *                 (*get_pipeline)        (GstValidateReporter *reporter);

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GST_VALIDATE_API
void gst_validate_reporter_set_name            (GstValidateReporter * reporter,
                                          gchar * name);
GST_VALIDATE_API
const gchar * gst_validate_reporter_get_name            (GstValidateReporter * reporter);
GST_VALIDATE_API
GstValidateRunner * gst_validate_reporter_get_runner (GstValidateReporter *reporter) G_GNUC_WARN_UNUSED_RESULT;
GST_VALIDATE_API
void gst_validate_reporter_init                (GstValidateReporter * reporter, const gchar *name);
GST_VALIDATE_API
void gst_validate_report                       (GstValidateReporter * reporter, GstValidateIssueId issue_id,
                                                const gchar * format, ...) G_GNUC_PRINTF (3, 4) G_GNUC_NO_INSTRUMENT;
GST_VALIDATE_API
void gst_validate_report_action                (GstValidateReporter * reporter,
                                                GstValidateAction *action,
                                                GstValidateIssueId issue_id,
                                                const gchar * format, ...) G_GNUC_PRINTF (4, 5) G_GNUC_NO_INSTRUMENT;
GST_VALIDATE_API
void gst_validate_report_valist                (GstValidateReporter * reporter, GstValidateIssueId issue_id,
                                          const gchar * format, va_list var_args) G_GNUC_PRINTF (3, 0);
GST_VALIDATE_API void
gst_validate_reporter_report_simple (GstValidateReporter * reporter, GstValidateIssueId issue_id,
                                          const gchar * message);

GST_VALIDATE_API
void gst_validate_reporter_set_runner          (GstValidateReporter * reporter, GstValidateRunner *runner);
GST_VALIDATE_API
void gst_validate_reporter_set_handle_g_logs   (GstValidateReporter * reporter);
GST_VALIDATE_API
GstValidateReport * gst_validate_reporter_get_report (GstValidateReporter *reporter,
                                                      GstValidateIssueId issue_id);
GST_VALIDATE_API
GList * gst_validate_reporter_get_reports (GstValidateReporter * reporter) G_GNUC_WARN_UNUSED_RESULT;
GST_VALIDATE_API
gint gst_validate_reporter_get_reports_count (GstValidateReporter *reporter);
GST_VALIDATE_API
GstValidateReportingDetails gst_validate_reporter_get_reporting_level (GstValidateReporter *reporter);

GST_VALIDATE_API
void gst_validate_reporter_purge_reports (GstValidateReporter * reporter);
GST_VALIDATE_API
GstPipeline * gst_validate_reporter_get_pipeline (GstValidateReporter * reporter) G_GNUC_WARN_UNUSED_RESULT;

G_END_DECLS
#endif /* _GST_VALIDATE_REPORTER_ */
