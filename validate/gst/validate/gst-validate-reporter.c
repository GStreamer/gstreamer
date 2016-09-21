/* GStreamer
 *
 * Copyright (C) 2013 Thibault Saunier <thibault.saunier@collabora.com>
 *
 * gst-validate-reporter.c
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
/**
 * SECTION:gst-validate-reporter
 * @short_description: A #GInterface that allows #GObject to be used as originator of
 * issues in the GstValidate reporting system
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gst-validate-internal.h"
#include "gst-validate-reporter.h"
#include "gst-validate-report.h"

#define REPORTER_PRIVATE "gst-validate-reporter-private"

typedef struct _GstValidateReporterPrivate
{
  GstValidateRunner *runner;
  GHashTable *reports;
  char *name;
  guint log_handler_id;
  GMutex reports_lock;
} GstValidateReporterPrivate;

static GstValidateReporterPrivate *g_log_handler = NULL;

G_DEFINE_INTERFACE (GstValidateReporter, gst_validate_reporter, G_TYPE_OBJECT);

static void
gst_validate_reporter_default_init (GstValidateReporterInterface * iface)
{
  g_object_interface_install_property (iface,
      g_param_spec_object ("validate-runner", "Validate Runner",
          "The Validate runner to " "report errors to",
          GST_TYPE_VALIDATE_RUNNER,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));
}

static void
_free_priv (GstValidateReporterPrivate * priv)
{
  if (priv->runner)
    g_object_remove_weak_pointer (G_OBJECT (priv->runner),
        (gpointer) & priv->runner);

  if (g_log_handler == priv) {
    g_log_set_default_handler (g_log_default_handler, NULL);
    g_log_handler = NULL;
  }

  g_hash_table_unref (priv->reports);
  g_free (priv->name);
  g_mutex_clear (&priv->reports_lock);
  g_slice_free (GstValidateReporterPrivate, priv);
}

static GstValidateReporterPrivate *
gst_validate_reporter_get_priv (GstValidateReporter * reporter)
{
  GstValidateReporterPrivate *priv;

  priv = g_object_get_data (G_OBJECT (reporter), REPORTER_PRIVATE);

  if (priv == NULL) {
    priv = g_slice_new0 (GstValidateReporterPrivate);
    priv->reports = g_hash_table_new_full (g_direct_hash,
        g_direct_equal, NULL, (GDestroyNotify) gst_validate_report_unref);

    g_mutex_init (&priv->reports_lock);
    g_object_set_data_full (G_OBJECT (reporter), REPORTER_PRIVATE, priv,
        (GDestroyNotify) _free_priv);
  }

  return priv;
}

#define GST_VALIDATE_REPORTER_REPORTS_LOCK(r)			\
  G_STMT_START {					\
  (g_mutex_lock (&gst_validate_reporter_get_priv(GST_VALIDATE_REPORTER_CAST(r))->reports_lock));		\
  } G_STMT_END

#define GST_VALIDATE_REPORTER_REPORTS_UNLOCK(r)			\
  G_STMT_START {					\
  (g_mutex_unlock (&gst_validate_reporter_get_priv(GST_VALIDATE_REPORTER_CAST(r))->reports_lock));		\
  } G_STMT_END

static GstValidateInterceptionReturn
gst_validate_reporter_intercept_report (GstValidateReporter * reporter,
    GstValidateReport * report)
{
  GstValidateInterceptionReturn ret = GST_VALIDATE_REPORTER_REPORT;
  GstValidateReporterInterface *iface =
      GST_VALIDATE_REPORTER_GET_INTERFACE (reporter);

  if (iface->intercept_report) {
    ret = iface->intercept_report (reporter, report);
  }

  return ret;
}

GstValidateReportingDetails
gst_validate_reporter_get_reporting_level (GstValidateReporter * reporter)
{
  GstValidateReportingDetails ret = GST_VALIDATE_SHOW_UNKNOWN;
  GstValidateReporterInterface *iface =
      GST_VALIDATE_REPORTER_GET_INTERFACE (reporter);

  if (iface->get_reporting_level) {
    ret = iface->get_reporting_level (reporter);
  }

  return ret;
}

GstPipeline *
gst_validate_reporter_get_pipeline (GstValidateReporter * reporter)
{
  GstValidateReporterInterface *iface =
      GST_VALIDATE_REPORTER_GET_INTERFACE (reporter);

  if (iface->get_pipeline)
    return iface->get_pipeline (reporter);

  return NULL;
}

GstValidateReport *
gst_validate_reporter_get_report (GstValidateReporter * reporter,
    GstValidateIssueId issue_id)
{
  GstValidateReport *report;
  GstValidateReporterPrivate *priv = gst_validate_reporter_get_priv (reporter);

  GST_VALIDATE_REPORTER_REPORTS_LOCK (reporter);
  report = g_hash_table_lookup (priv->reports, (gconstpointer) issue_id);
  GST_VALIDATE_REPORTER_REPORTS_UNLOCK (reporter);

  return report;
}

void
gst_validate_report_valist (GstValidateReporter * reporter,
    GstValidateIssueId issue_id, const gchar * format, va_list var_args)
{
  GstValidateReport *report, *prev_report;
  gchar *message, *combo;
  va_list vacopy;
  GstValidateIssue *issue;
  GstValidateReporterPrivate *priv = gst_validate_reporter_get_priv (reporter);
  GstValidateInterceptionReturn int_ret;

  issue = gst_validate_issue_from_id (issue_id);

  g_return_if_fail (issue != NULL);
  g_return_if_fail (GST_IS_VALIDATE_REPORTER (reporter));

  G_VA_COPY (vacopy, var_args);
  message = g_strdup_vprintf (format, vacopy);
  report = gst_validate_report_new (issue, reporter, message);

#ifndef GST_DISABLE_GST_DEBUG
  combo =
      g_strdup_printf ("<%s> %" GST_VALIDATE_ISSUE_FORMAT " : %s", priv->name,
      GST_VALIDATE_ISSUE_ARGS (issue), format);
  G_VA_COPY (vacopy, var_args);
  if (report->level == GST_VALIDATE_REPORT_LEVEL_CRITICAL) {
    gst_debug_log_valist (GST_CAT_DEFAULT, GST_LEVEL_ERROR, __FILE__,
        GST_FUNCTION, __LINE__, NULL, combo, vacopy);
  } else if (report->level == GST_VALIDATE_REPORT_LEVEL_WARNING)
    gst_debug_log_valist (GST_CAT_DEFAULT, GST_LEVEL_WARNING, __FILE__,
        GST_FUNCTION, __LINE__, NULL, combo, vacopy);
  else if (report->level == GST_VALIDATE_REPORT_LEVEL_ISSUE)
    gst_debug_log_valist (GST_CAT_DEFAULT, GST_LEVEL_LOG, __FILE__,
        GST_FUNCTION, __LINE__, (GObject *) NULL, combo, vacopy);
  else
    gst_debug_log_valist (GST_CAT_DEFAULT, GST_LEVEL_DEBUG, __FILE__,
        GST_FUNCTION, __LINE__, NULL, combo, vacopy);
  g_free (combo);
#endif

  int_ret = gst_validate_reporter_intercept_report (reporter, report);

  if (int_ret == GST_VALIDATE_REPORTER_DROP) {
    gst_validate_report_unref (report);
    goto done;
  }

  prev_report = g_hash_table_lookup (priv->reports, (gconstpointer) issue_id);

  if (prev_report) {
    GstValidateReportingDetails reporter_level =
        gst_validate_reporter_get_reporting_level (reporter);
    GstValidateReportingDetails runner_level = GST_VALIDATE_SHOW_UNKNOWN;

    if (priv->runner)
      runner_level =
          gst_validate_runner_get_default_reporting_level (priv->runner);

    if (reporter_level == GST_VALIDATE_SHOW_ALL ||
        (runner_level == GST_VALIDATE_SHOW_ALL &&
            reporter_level == GST_VALIDATE_SHOW_UNKNOWN))
      gst_validate_report_add_repeated_report (prev_report, report);

    gst_validate_report_unref (report);
    goto done;
  }

  GST_VALIDATE_REPORTER_REPORTS_LOCK (reporter);
  g_hash_table_insert (priv->reports, (gpointer) issue_id, report);
  GST_VALIDATE_REPORTER_REPORTS_UNLOCK (reporter);

  if (priv->runner && int_ret == GST_VALIDATE_REPORTER_REPORT) {
    gst_validate_runner_add_report (priv->runner, report);
  }

  if (gst_validate_report_check_abort (report)) {
    if (priv->runner)
      gst_validate_runner_printf (priv->runner);

    g_error ("Fatal report received: %" GST_VALIDATE_ERROR_REPORT_PRINT_FORMAT,
        GST_VALIDATE_REPORT_PRINT_ARGS (report));
  }

done:
  g_free (message);
}

static void
gst_validate_reporter_destroyed (gpointer udata, GObject * freed_reporter)
{
  g_log_set_handler ("GStreamer",
      G_LOG_LEVEL_MASK, (GLogFunc) g_log_default_handler, NULL);
  g_log_set_handler ("GLib",
      G_LOG_LEVEL_MASK, (GLogFunc) g_log_default_handler, NULL);
  g_log_set_handler ("GLib-GObject",
      G_LOG_LEVEL_MASK, (GLogFunc) g_log_default_handler, NULL);
}

static void
gst_validate_reporter_g_log_func (const gchar * log_domain,
    GLogLevelFlags log_level, const gchar * message,
    GstValidateReporter * reporter)
{
  if (log_level & G_LOG_LEVEL_CRITICAL)
    GST_VALIDATE_REPORT (reporter, G_LOG_CRITICAL, "%s", message);
  else if (log_level & G_LOG_LEVEL_WARNING)
    GST_VALIDATE_REPORT (reporter, G_LOG_WARNING, "%s", message);
  else
    GST_VALIDATE_REPORT (reporter, G_LOG_ISSUE, "%s", message);
}

/**
 * gst_validate_report:
 * @reporter: The source of the new report
 * @issue_id: The #GstValidateIssueId of the issue
 * @format: The format of the message describing the issue in a printf
 *       format followed by the parametters.
 *
 * Reports a new issue in the GstValidate reporting system with @m
 * as the source of that issue.
 *
 * You can also use #GST_VALIDATE_REPORT instead.
 */
void
gst_validate_report (GstValidateReporter * reporter,
    GstValidateIssueId issue_id, const gchar * format, ...)
{
  va_list var_args;

  va_start (var_args, format);
  gst_validate_report_valist (reporter, issue_id, format, var_args);
  va_end (var_args);
}

void
gst_validate_reporter_report_simple (GstValidateReporter * reporter,
    GstValidateIssueId issue_id, const gchar * message)
{
  gst_validate_report (reporter, issue_id, "%s", message);
}

/**
 * gst_validate_reporter_set_name:
 * @reporter: The reporter to set the name on
 * @name: (transfer full): The name of the reporter
 *
 * Sets @ name on @reporter
 */
void
gst_validate_reporter_set_name (GstValidateReporter * reporter, gchar * name)
{
  GstValidateReporterPrivate *priv = gst_validate_reporter_get_priv (reporter);

  if (priv->name)
    g_free (priv->name);

  priv->name = name;
}

const gchar *
gst_validate_reporter_get_name (GstValidateReporter * reporter)
{
  GstValidateReporterPrivate *priv = gst_validate_reporter_get_priv (reporter);

  return priv->name;
}

/**
 * gst_validate_reporter_get_runner:
 * @reporter: The reporter to get the runner from
 *
 * Returns: (transfer none): The runner
 */
GstValidateRunner *
gst_validate_reporter_get_runner (GstValidateReporter * reporter)
{
  GstValidateReporterPrivate *priv = gst_validate_reporter_get_priv (reporter);

  return priv->runner;
}

void
gst_validate_reporter_set_runner (GstValidateReporter * reporter,
    GstValidateRunner * runner)
{
  GstValidateReporterPrivate *priv = gst_validate_reporter_get_priv (reporter);

  priv->runner = runner;

  /* The runner is supposed to stay alive during the whole scenario but if
   * we are using another tracer we may have messages catched after it has been
   * destroyed. This may happen if the 'leaks' tracer detected leaks for
   * example. */
  if (runner)
    g_object_add_weak_pointer (G_OBJECT (runner), (gpointer) & priv->runner);

  g_object_notify (G_OBJECT (reporter), "validate-runner");
}

/**
 * gst_validate_reporter_set_handle_g_logs:
 * @reporter: The #GstValidateReporter to set has the handler for g_log
 *
 * Set @reporter has the 'source' of any g_log happening during the
 * execution. Usually the monitor of the first #GstPipeline is used
 * to handle g_logs.
 *
 * Basically this function is used in order to start tracking any
 * issue reported with g_log in the process into GstValidate report
 * in the GstValidate reporting system.
 */
void
gst_validate_reporter_set_handle_g_logs (GstValidateReporter * reporter)
{
  g_log_set_default_handler ((GLogFunc) gst_validate_reporter_g_log_func,
      reporter);

  g_log_set_handler ("GStreamer",
      G_LOG_LEVEL_MASK, (GLogFunc) gst_validate_reporter_g_log_func, reporter);

  g_log_set_handler ("GLib",
      G_LOG_LEVEL_MASK, (GLogFunc) gst_validate_reporter_g_log_func, reporter);


  g_log_set_handler ("GLib-GObject",
      G_LOG_LEVEL_MASK, (GLogFunc) gst_validate_reporter_g_log_func, reporter);

  g_log_handler = gst_validate_reporter_get_priv (reporter);
  g_object_weak_ref (G_OBJECT (reporter), gst_validate_reporter_destroyed,
      NULL);

}

/**
 * gst_validate_reporter_get_reports:
 * @reporter: a #GstValidateReporter
 *
 * Get the list of reports present in the reporter.
 *
 * Returns: (transfer full) (element-type GstValidateReport): the list of
 * #GstValidateReport present in the reporter.
 * The caller should unref each report once it is done with them.
 */
GList *
gst_validate_reporter_get_reports (GstValidateReporter * reporter)
{
  GstValidateReporterPrivate *priv;
  GList *reports, *tmp;
  GList *ret = NULL;

  priv = g_object_get_data (G_OBJECT (reporter), REPORTER_PRIVATE);

  GST_VALIDATE_REPORTER_REPORTS_LOCK (reporter);
  reports = g_hash_table_get_values (priv->reports);
  for (tmp = reports; tmp; tmp = tmp->next) {
    ret =
        g_list_append (ret,
        gst_validate_report_ref ((GstValidateReport *) (tmp->data)));
  }
  g_list_free (reports);
  GST_VALIDATE_REPORTER_REPORTS_UNLOCK (reporter);

  return ret;
}

/**
 * gst_validate_reporter_get_reports_count:
 * @reporter: a #GstValidateReporter
 *
 * Get the number of reports present in the reporter.
 *
 * Returns: the number of reports currently present in @reporter.
 */
gint
gst_validate_reporter_get_reports_count (GstValidateReporter * reporter)
{
  GstValidateReporterPrivate *priv;
  gint ret;

  priv = g_object_get_data (G_OBJECT (reporter), REPORTER_PRIVATE);

  GST_VALIDATE_REPORTER_REPORTS_LOCK (reporter);
  ret = g_hash_table_size (priv->reports);
  GST_VALIDATE_REPORTER_REPORTS_UNLOCK (reporter);

  return ret;
}

/**
 * gst_validate_reporter_purge_reports:
 * @reporter: a #GstValidateReporter
 *
 * Remove all the #GstValidateReport from @reporter. This should be called
 * before unreffing the reporter to break cyclic references.
 */
void
gst_validate_reporter_purge_reports (GstValidateReporter * reporter)
{
  GstValidateReporterPrivate *priv;

  priv = g_object_get_data (G_OBJECT (reporter), REPORTER_PRIVATE);

  GST_VALIDATE_REPORTER_REPORTS_LOCK (reporter);
  g_hash_table_remove_all (priv->reports);
  GST_VALIDATE_REPORTER_REPORTS_UNLOCK (reporter);
}
