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
#include "gst-qa-reporter.h"
#include "gst-qa-report.h"

#define REPORTER_PRIVATE "gst-qa-reporter-private"

GST_DEBUG_CATEGORY_STATIC (gst_qa_reporter);
#define GST_CAT_DEFAULT gst_qa_reporter

typedef struct _GstQaReporterPrivate
{
  GstQaRunner *runner;
  GHashTable *reports;
  char *name;
} GstQaReporterPrivate;

static void
gst_qa_reporter_default_init (GstQaReporterInterface * iface)
{
  GST_DEBUG_CATEGORY_INIT (gst_qa_reporter, "gstqareporter",
      GST_DEBUG_FG_MAGENTA, "gst qa reporter");

  g_object_interface_install_property (iface,
      g_param_spec_object ("qa-runner", "QA Runner", "The QA runner to "
          "report errors to", GST_TYPE_QA_RUNNER,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));
}

G_DEFINE_INTERFACE (GstQaReporter, gst_qa_reporter, G_TYPE_OBJECT);

static void
_free_priv (GstQaReporterPrivate * priv)
{
  g_hash_table_unref (priv->reports);
  g_free (priv->name);
}

static GstQaReporterPrivate *
gst_qa_reporter_get_priv (GstQaReporter * reporter)
{
  GstQaReporterPrivate *priv =
      g_object_get_data (G_OBJECT (reporter), REPORTER_PRIVATE);

  if (priv == NULL) {
    priv = g_slice_new0 (GstQaReporterPrivate);
    priv->reports = g_hash_table_new_full (g_direct_hash,
        g_direct_equal, g_free, (GDestroyNotify) gst_qa_report_unref);

    g_object_set_data_full (G_OBJECT (reporter), REPORTER_PRIVATE, priv,
        (GDestroyNotify) _free_priv);
  }

  return priv;
}

static void
gst_qa_reporter_intercept_report (GstQaReporter * reporter,
    GstQaReport * report)
{
  GstQaReporterInterface *iface = GST_QA_REPORTER_GET_INTERFACE (reporter);

  if (iface->intercept_report) {
    iface->intercept_report (reporter, report);
  }
}

void
gst_qa_report_valist (GstQaReporter * reporter,
    GstQaIssueId issue_id, const gchar * format, va_list var_args)
{
  GstQaReport *report;
  gchar *message;
  GstQaIssue *issue;
  GstQaReporterPrivate *priv = gst_qa_reporter_get_priv (reporter);

  issue = gst_qa_issue_from_id (issue_id);

  g_return_if_fail (issue != NULL);

  message = g_strdup_vprintf (format, var_args);
  report = gst_qa_report_new (issue, reporter, message);

  gst_qa_reporter_intercept_report (reporter, report);

  if (issue->repeat == FALSE) {
    GstQaIssueId issue_id = gst_qa_issue_get_id (issue);

    if (g_hash_table_lookup (priv->reports, (gconstpointer) issue_id)) {
      GST_DEBUG ("Report %d:%s already present", issue_id, issue->summary);
      return;
    }

    g_hash_table_insert (priv->reports, (gpointer) issue_id, report);
  }

  if (report->level == GST_QA_REPORT_LEVEL_CRITICAL)
    GST_ERROR ("<%s>: %s", priv->name, message);
  else if (report->level == GST_QA_REPORT_LEVEL_WARNING)
    GST_WARNING ("<%s>: %s", priv->name, message);
  else if (report->level == GST_QA_REPORT_LEVEL_ISSUE)
    GST_LOG ("<%s>: %s", priv->name, message);
  else
    GST_DEBUG ("<%s>: %s", priv->name, message);

  GST_INFO_OBJECT (reporter, "Received error report %" GST_QA_ISSUE_FORMAT
      " : %s", GST_QA_ISSUE_ARGS (issue), message);
  gst_qa_report_printf (report);
  gst_qa_report_check_abort (report);

  if (priv->runner) {
    gst_qa_runner_add_report (priv->runner, report);
  } else {
    gst_qa_report_unref (report);
  }

  g_free (message);
}

void
gst_qa_report (GstQaReporter * reporter, GstQaIssueId issue_id,
    const gchar * format, ...)
{
  va_list var_args;

  va_start (var_args, format);
  gst_qa_report_valist (reporter, issue_id, format, var_args);
  va_end (var_args);
}

void
gst_qa_reporter_set_name (GstQaReporter * reporter, gchar * name)
{
  GstQaReporterPrivate *priv = gst_qa_reporter_get_priv (reporter);

  if (priv->name)
    g_free (priv->name);

  priv->name = name;
}

const gchar *
gst_qa_reporter_get_name (GstQaReporter * reporter)
{
  GstQaReporterPrivate *priv = gst_qa_reporter_get_priv (reporter);

  return priv->name;
}

GstQaRunner *
gst_qa_reporter_get_runner (GstQaReporter * reporter)
{
  GstQaReporterPrivate *priv = gst_qa_reporter_get_priv (reporter);

  return priv->runner;
}

void
gst_qa_reporter_set_runner (GstQaReporter * reporter, GstQaRunner * runner)
{
  GstQaReporterPrivate *priv = gst_qa_reporter_get_priv (reporter);

  priv->runner = runner;
}
