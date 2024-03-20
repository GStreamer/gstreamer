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
 * @title: GstValidateReporter
 * @short_description: A #GInterface that allows #GObject to be used as originator of
 * issues in the GstValidate reporting system
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <math.h>
#include "gst-validate-internal.h"
#include "gst-validate-reporter.h"
#include "gst-validate-report.h"

#define REPORTER_PRIVATE "gst-validate-reporter-private"

typedef struct _GstValidateReporterPrivate
{
  GWeakRef runner;
  GHashTable *reports;
  char *name;
  guint log_handler_id;
  GMutex reports_lock;
} GstValidateReporterPrivate;

static GstValidateReporterPrivate *g_log_handler = NULL;
static GWeakRef log_reporter;

G_DEFINE_INTERFACE (GstValidateReporter, gst_validate_reporter, G_TYPE_OBJECT);

static void
gst_validate_reporter_default_init (GstValidateReporterInterface * iface)
{
  g_object_interface_install_property (iface,
      g_param_spec_object ("validate-runner", "Validate Runner",
          "The Validate runner to report errors to",
          GST_TYPE_VALIDATE_RUNNER,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));
}

static void
_free_priv (GstValidateReporterPrivate * priv)
{
  if (g_log_handler == priv) {
    g_log_set_default_handler (g_log_default_handler, NULL);
    g_log_handler = NULL;
  }

  g_hash_table_unref (priv->reports);
  g_free (priv->name);
  g_mutex_clear (&priv->reports_lock);
  g_weak_ref_clear (&priv->runner);
  g_free (priv);
}

static GstValidateReporterPrivate *
gst_validate_reporter_get_priv (GstValidateReporter * reporter)
{
  GstValidateReporterPrivate *priv;

  priv = g_object_get_data (G_OBJECT (reporter), REPORTER_PRIVATE);

  if (priv == NULL) {
    priv = g_new0 (GstValidateReporterPrivate, 1);
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

/**
 * gst_validate_reporter_get_pipeline:
 * @reporter: The reporter to get the pipeline from
 *
 * Returns: (transfer full) (nullable): The #GstPipeline
 */
GstPipeline *
gst_validate_reporter_get_pipeline (GstValidateReporter * reporter)
{
  GstValidateReporterInterface *iface =
      GST_VALIDATE_REPORTER_GET_INTERFACE (reporter);

  if (iface->get_pipeline)
    return iface->get_pipeline (reporter);

  return NULL;
}

/**
 * gst_validate_reporter_get_report:
 * @reporter: The reporter to get the report from
 * @issue_id: The issue id to get the report from
 *
 * Returns: (transfer none) (nullable): The #GstValidateReport
 */
GstValidateReport *
gst_validate_reporter_get_report (GstValidateReporter * reporter,
    GstValidateIssueId issue_id)
{
  GstValidateReport *report;
  GstValidateReporterPrivate *priv = gst_validate_reporter_get_priv (reporter);

  GST_VALIDATE_REPORTER_REPORTS_LOCK (reporter);
  report = g_hash_table_lookup (priv->reports, GINT_TO_POINTER (issue_id));
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
  GstValidateRunner *runner = NULL;

  issue = gst_validate_issue_from_id (issue_id);

  g_return_if_fail (issue != NULL);
  g_return_if_fail (GST_IS_VALIDATE_REPORTER (reporter));

  G_VA_COPY (vacopy, var_args);
  message = gst_info_strdup_vprintf (format, vacopy);
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
  va_end (vacopy);

  int_ret = gst_validate_reporter_intercept_report (reporter, report);

  if (int_ret == GST_VALIDATE_REPORTER_DROP) {
    gst_validate_report_unref (report);
    goto done;
  }

  prev_report = g_hash_table_lookup (priv->reports, GINT_TO_POINTER (issue_id));

  runner = gst_validate_reporter_get_runner (reporter);
  if (prev_report && prev_report->level != GST_VALIDATE_REPORT_LEVEL_EXPECTED) {
    GstValidateReportingDetails reporter_level =
        gst_validate_reporter_get_reporting_level (reporter);
    GstValidateReportingDetails runner_level = GST_VALIDATE_SHOW_UNKNOWN;

    if (runner)
      runner_level = gst_validate_runner_get_default_reporting_level (runner);

    if ((reporter_level == GST_VALIDATE_SHOW_ALL ||
            (runner_level == GST_VALIDATE_SHOW_ALL &&
                reporter_level == GST_VALIDATE_SHOW_UNKNOWN)) ||
        (issue->flags & GST_VALIDATE_ISSUE_FLAGS_FULL_DETAILS)) {

      gst_validate_report_add_repeated_report (prev_report, report);
    }

    gst_validate_report_unref (report);
    goto done;
  }

  GST_VALIDATE_REPORTER_REPORTS_LOCK (reporter);
  g_hash_table_insert (priv->reports, GINT_TO_POINTER (issue_id), report);
  GST_VALIDATE_REPORTER_REPORTS_UNLOCK (reporter);

  if (runner && int_ret == GST_VALIDATE_REPORTER_REPORT) {
    gst_validate_runner_add_report (runner, report);
  }

  if (gst_validate_report_check_abort (report)) {
    if (runner)
      gst_validate_runner_printf (runner);

    gst_validate_abort ("Fatal report received: %"
        GST_VALIDATE_ERROR_REPORT_PRINT_FORMAT,
        GST_VALIDATE_REPORT_PRINT_ARGS (report));
  }

done:
  if (runner)
    gst_object_unref (runner);

  g_free (message);
}

static void
gst_validate_default_log_hanlder (const gchar * log_domain,
    GLogLevelFlags log_level, const gchar * message, gpointer user_data)
{
  gchar *trace = gst_debug_get_stack_trace (GST_STACK_TRACE_SHOW_FULL);

  if (trace) {
    gst_validate_printf (NULL, "\nStack trace:\n%s\n", trace);
    g_free (trace);
  }

  g_log_default_handler (log_domain, log_level, message, user_data);
}

static void
gst_validate_reporter_g_log_func (const gchar * log_domain,
    GLogLevelFlags log_level, const gchar * message, gpointer udata)
{
  GstValidateReporter *reporter = g_weak_ref_get (&log_reporter);

  g_printerr ("G_LOG: %s\n", message);
  if (!reporter) {
    gst_validate_default_log_hanlder (log_domain, log_level, message, NULL);
    return;
  }
  if (log_level & G_LOG_LEVEL_ERROR)
    gst_validate_default_log_hanlder (log_domain, log_level, message, reporter);
  else if (log_level & G_LOG_LEVEL_CRITICAL)
    GST_VALIDATE_REPORT (reporter, G_LOG_CRITICAL, "%s", message);
  else if (log_level & G_LOG_LEVEL_WARNING)
    GST_VALIDATE_REPORT (reporter, G_LOG_WARNING, "%s", message);

  gst_object_unref (reporter);
}

/**
 * gst_validate_report:
 * @reporter: The source of the new report
 * @issue_id: The #GstValidateIssueId of the issue
 * @format: The format of the message describing the issue in a printf
 *       format followed by the parameters.
 * @...: Substitution arguments for @format
 *
 * Reports a new issue in the GstValidate reporting system.
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

/**
 * gst_validate_report_action:
 * @reporter: The source of the new report
 * @action: The action reporting the issue
 * @issue_id: The #GstValidateIssueId of the issue
 * @format: The format of the message describing the issue in a printf
 *       format followed by the parameters.
 * @...: Substitution arguments for @format
 *
 * Reports a new issue in the GstValidate reporting system specifying @action
 * as failling action .
 *
 * You can also use #GST_VALIDATE_REPORT instead.
 */
void
gst_validate_report_action (GstValidateReporter * reporter,
    GstValidateAction * action, GstValidateIssueId issue_id,
    const gchar * format, ...)
{
  va_list var_args, var_copy;
  GString *f;

  if (!action) {
    f = g_string_new (format);
    goto done;
  }

  f = g_string_new (NULL);
  g_string_append_printf (f, "\n> %s:%d", GST_VALIDATE_ACTION_FILENAME (action),
      GST_VALIDATE_ACTION_LINENO (action));

  if (GST_VALIDATE_ACTION_N_REPEATS (action))
    g_string_append_printf (f, " (repeat: %d/%d)",
        action->repeat, GST_VALIDATE_ACTION_N_REPEATS (action));

  g_string_append_printf (f, "\n%s", GST_VALIDATE_ACTION_DEBUG (action));
  if (gst_validate_action_get_level (action)) {
    gchar *subaction_str = gst_structure_to_string (action->structure);

    g_string_append_printf (f, "\n       |-> %s", subaction_str);
    g_free (subaction_str);
  }

  g_string_append_printf (f, "\n       >\n       > %s", format);

done:
  va_start (var_args, format);
  G_VA_COPY (var_copy, var_args);
  gst_validate_report_valist (reporter, issue_id, f->str, var_args);
  if (action) {
    gint i, indent = gst_validate_action_get_level (action) * 2;
    gchar *message, **lines, *color = NULL;
    const gchar *endcolor = "";

    if (g_log_writer_supports_color (fileno (stderr))) {
      color = gst_debug_construct_term_color (GST_DEBUG_FG_RED);
      endcolor = "\033[0m";
    }
    gst_validate_printf (NULL, "%*s%s> Error%s:\n", indent, "",
        color ? color : "", endcolor);

    message = gst_info_strdup_vprintf (f->str, var_copy);
    lines = g_strsplit (message, "\n", -1);
    for (i = 1; lines[i]; i++)
      gst_validate_printf (NULL, "%*s%s>%s %s\n", indent, "", color, endcolor,
          lines[i]);
    g_strfreev (lines);
    g_free (message);
    g_free (color);
  }
  va_end (var_args);
  va_end (var_copy);

  g_string_free (f, TRUE);
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
 * @name: (transfer full) (nullable): The name of the reporter
 *
 * Sets @name on @reporter
 */
void
gst_validate_reporter_set_name (GstValidateReporter * reporter, gchar * name)
{
  GstValidateReporterPrivate *priv = gst_validate_reporter_get_priv (reporter);

  g_free (priv->name);

  priv->name = name;
}

/**
 * gst_validate_reporter_get_name:
 * @reporter: The reporter to get the name from
 *
 * Gets @name of @reporter
 *
 * Returns: (transfer none) (nullable): The name of the reporter
 */
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
 * Returns: (transfer full) (nullable): The runner
 */
GstValidateRunner *
gst_validate_reporter_get_runner (GstValidateReporter * reporter)
{
  GstValidateReporterPrivate *priv = gst_validate_reporter_get_priv (reporter);

  return g_weak_ref_get (&priv->runner);
}

void
gst_validate_reporter_set_runner (GstValidateReporter * reporter,
    GstValidateRunner * runner)
{
  GstValidateReporterPrivate *priv = gst_validate_reporter_get_priv (reporter);

  g_weak_ref_set (&priv->runner, runner);

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
  g_weak_ref_set (&log_reporter, reporter);

  g_log_set_default_handler ((GLogFunc) gst_validate_reporter_g_log_func, NULL);

  g_log_set_handler ("GStreamer",
      G_LOG_LEVEL_MASK, (GLogFunc) gst_validate_reporter_g_log_func, NULL);

  g_log_set_handler ("GLib",
      G_LOG_LEVEL_MASK, (GLogFunc) gst_validate_reporter_g_log_func, NULL);


  g_log_set_handler ("GLib-GObject",
      G_LOG_LEVEL_MASK, (GLogFunc) gst_validate_reporter_g_log_func, NULL);

  g_log_handler = gst_validate_reporter_get_priv (reporter);
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
