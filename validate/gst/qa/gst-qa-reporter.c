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

typedef struct _GstQaReporterPrivate
{
  GstQaRunner *runner;
  GHashTable *reports;
  char *name;
} GstQaReporterPrivate;

static void
gst_qa_reporter_default_init (GstQaReporterInterface * iface)
{
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

static inline gchar *
_qa_report_id (GstQaReport * report)
{
  return g_strdup_printf ("%i-%i-%i-%s",
      report->level, report->area, report->subarea, report->id);
}

static GstQaReporterPrivate *
gst_qa_reporter_get_priv (GstQaReporter * reporter)
{
  GstQaReporterPrivate *priv =
      g_object_get_data (G_OBJECT (reporter), REPORTER_PRIVATE);

  if (priv == NULL) {
    priv = g_slice_new0 (GstQaReporterPrivate);
    priv->reports = g_hash_table_new_full (g_str_hash, g_str_equal,
        g_free, (GDestroyNotify) gst_qa_report_unref);

    g_object_set_data_full (G_OBJECT (reporter), REPORTER_PRIVATE, priv,
        (GDestroyNotify) _free_priv);
  }

  return priv;
}

void
gst_qa_report_valist (GstQaReporter * reporter, gboolean repeat,
    GstQaReportLevel level, GstQaReportArea area,
    gint subarea, const gchar * format, va_list var_args)
{
  GstQaReport *report;
  gchar *message, *report_id = NULL;
  GstQaReporterPrivate *priv = gst_qa_reporter_get_priv (reporter);

  message = g_strdup_vprintf (format, var_args);
  report = gst_qa_report_new (priv->name, level, area, subarea,
      format, message);

  if (repeat == FALSE) {
    report_id = _qa_report_id (report);

    if (g_hash_table_lookup (priv->reports, report_id)) {
      GST_DEBUG ("Report %s already present", report_id);
      g_free (report_id);
      return;
    }

    g_hash_table_insert (priv->reports, report_id, report);
  }

  GST_INFO_OBJECT (reporter, "Received error report %d : %d : %d : %s",
      level, area, subarea, message);
  gst_qa_report_printf (report);
  if (priv->runner) {
    gst_qa_runner_add_report (priv->runner, report);
  } else {
    gst_qa_report_unref (report);
  }

  g_free (message);
}

void
gst_qa_report (GstQaReporter * reporter, gboolean repeat,
    GstQaReportLevel level, GstQaReportArea area,
    gint subarea, const gchar * format, ...)
{
  va_list var_args;

  va_start (var_args, format);
  gst_qa_report_valist (reporter, repeat, level, area, subarea,
      format, var_args);
  va_end (var_args);
}

void
gst_qa_reporter_set_name (GstQaReporter * reporter, const gchar * name)
{
  GstQaReporterPrivate *priv = gst_qa_reporter_get_priv (reporter);

  if (priv->name)
    g_free (priv->name);

  priv->name = g_strdup (name);
}

GstQaRunner *
gst_qa_reporter_get_runner (GstQaReporter * reporter)
{
  GstQaReporterPrivate *priv = gst_qa_reporter_get_priv (reporter);

  return priv->runner;
}
