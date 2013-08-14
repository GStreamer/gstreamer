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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gst-validate-reporter.h"
#include "gst-validate-report.h"

#define REPORTER_PRIVATE "gst-validate-reporter-private"

GST_DEBUG_CATEGORY_STATIC (gst_validate_reporter);
#define GST_CAT_DEFAULT gst_validate_reporter

typedef struct _GstValidateReporterPrivate
{
  GstValidateRunner *runner;
  GHashTable *reports;
  char *name;
} GstValidateReporterPrivate;

static void
gst_validate_reporter_default_init (GstValidateReporterInterface * iface)
{
  GST_DEBUG_CATEGORY_INIT (gst_validate_reporter, "gstvalidatereporter",
      GST_DEBUG_FG_MAGENTA, "gst qa reporter");

  g_object_interface_install_property (iface,
      g_param_spec_object ("qa-runner", "VALIDATE Runner",
          "The Validate runner to " "report errors to",
          GST_TYPE_VALIDATE_RUNNER,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));
}

G_DEFINE_INTERFACE (GstValidateReporter, gst_validate_reporter, G_TYPE_OBJECT);

static void
_free_priv (GstValidateReporterPrivate * priv)
{
  g_hash_table_unref (priv->reports);
  g_free (priv->name);
}

static GstValidateReporterPrivate *
gst_validate_reporter_get_priv (GstValidateReporter * reporter)
{
  GstValidateReporterPrivate *priv =
      g_object_get_data (G_OBJECT (reporter), REPORTER_PRIVATE);

  if (priv == NULL) {
    priv = g_slice_new0 (GstValidateReporterPrivate);
    priv->reports = g_hash_table_new_full (g_direct_hash,
        g_direct_equal, NULL, (GDestroyNotify) gst_validate_report_unref);

    g_object_set_data_full (G_OBJECT (reporter), REPORTER_PRIVATE, priv,
        (GDestroyNotify) _free_priv);
  }

  return priv;
}

static void
gst_validate_reporter_intercept_report (GstValidateReporter * reporter,
    GstValidateReport * report)
{
  GstValidateReporterInterface *iface =
      GST_VALIDATE_REPORTER_GET_INTERFACE (reporter);

  if (iface->intercept_report) {
    iface->intercept_report (reporter, report);
  }
}

void
gst_validate_report_valist (GstValidateReporter * reporter,
    GstValidateIssueId issue_id, const gchar * format, va_list var_args)
{
  GstValidateReport *report;
  gchar *message;
  GstValidateIssue *issue;
  GstValidateReporterPrivate *priv = gst_validate_reporter_get_priv (reporter);

  issue = gst_validate_issue_from_id (issue_id);

  g_return_if_fail (issue != NULL);

  message = g_strdup_vprintf (format, var_args);
  report = gst_validate_report_new (issue, reporter, message);

  gst_validate_reporter_intercept_report (reporter, report);

  if (issue->repeat == FALSE) {
    GstValidateIssueId issue_id = gst_validate_issue_get_id (issue);

    if (g_hash_table_lookup (priv->reports, (gconstpointer) issue_id)) {
      GST_DEBUG ("Report \"%" G_GUINTPTR_FORMAT ":%s\" already present",
          issue_id, issue->summary);
      gst_validate_report_unref (report);
      return;
    }

    g_hash_table_insert (priv->reports, (gpointer) issue_id, report);
  }

  if (report->level == GST_VALIDATE_REPORT_LEVEL_CRITICAL)
    GST_ERROR ("<%s>: %s", priv->name, message);
  else if (report->level == GST_VALIDATE_REPORT_LEVEL_WARNING)
    GST_WARNING ("<%s>: %s", priv->name, message);
  else if (report->level == GST_VALIDATE_REPORT_LEVEL_ISSUE)
    GST_LOG ("<%s>: %s", priv->name, message);
  else
    GST_DEBUG ("<%s>: %s", priv->name, message);

  GST_INFO_OBJECT (reporter, "Received error report %" GST_VALIDATE_ISSUE_FORMAT
      " : %s", GST_VALIDATE_ISSUE_ARGS (issue), message);
  gst_validate_report_printf (report);
  gst_validate_report_check_abort (report);

  if (priv->runner) {
    gst_validate_runner_add_report (priv->runner, report);
  } else {
    gst_validate_report_unref (report);
  }

  g_free (message);
}

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
}
