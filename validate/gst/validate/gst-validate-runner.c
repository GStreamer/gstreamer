/* GStreamer
 *
 * Copyright (C) 2013 Collabora Ltd.
 *  Author: Thiago Sousa Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-validate-runner.c - Validate Runner class
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gst-validate-internal.h"
#include "gst-validate-report.h"
#include "gst-validate-monitor-factory.h"
#include "gst-validate-override-registry.h"
#include "gst-validate-runner.h"

/**
 * SECTION:gst-validate-runner
 * @short_description: Class that runs Gst Validate tests for a pipeline
 *
 * TODO
 */

#define gst_validate_runner_parent_class parent_class
G_DEFINE_TYPE (GstValidateRunner, gst_validate_runner, G_TYPE_OBJECT);

/* signals */
enum
{
  REPORT_ADDED_SIGNAL,
  /* add more above */
  LAST_SIGNAL
};

static guint _signals[LAST_SIGNAL] = { 0 };

static void
gst_validate_runner_dispose (GObject * object)
{
  GstValidateRunner *runner = GST_VALIDATE_RUNNER_CAST (object);

  g_slist_free_full (runner->reports,
      (GDestroyNotify) gst_validate_report_unref);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_validate_runner_class_init (GstValidateRunnerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = gst_validate_runner_dispose;

  _signals[REPORT_ADDED_SIGNAL] =
      g_signal_new ("report-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1,
      GST_TYPE_VALIDATE_REPORT);
}

static void
gst_validate_runner_init (GstValidateRunner * runner)
{
  runner->setup = FALSE;
}

/**
 * gst_validate_runner_new:
 */
GstValidateRunner *
gst_validate_runner_new (void)
{
  return g_object_new (GST_TYPE_VALIDATE_RUNNER, NULL);
}

void
gst_validate_runner_add_report (GstValidateRunner * runner,
    GstValidateReport * report)
{
  runner->reports = g_slist_prepend (runner->reports, report);

  g_signal_emit (runner, _signals[REPORT_ADDED_SIGNAL], 0, report);
}

guint
gst_validate_runner_get_reports_count (GstValidateRunner * runner)
{
  g_return_val_if_fail (runner != NULL, 0);
  return g_slist_length (runner->reports);
}

GSList *
gst_validate_runner_get_reports (GstValidateRunner * runner)
{
  /* TODO should we need locking or put in htte docs to always call this
   * after pipeline ends? */
  return runner->reports;
}

int
gst_validate_runner_printf (GstValidateRunner * runner)
{
  GSList *tmp;
  guint count = 0;
  int ret = 0;
  GList *criticals = NULL;

  for (tmp = gst_validate_runner_get_reports (runner); tmp; tmp = tmp->next) {
    GstValidateReport *report = tmp->data;

    gst_validate_report_printf (report);
    if (ret == 0 && report->level == GST_VALIDATE_REPORT_LEVEL_CRITICAL) {
      criticals = g_list_append (criticals, tmp->data);
      ret = 18;
    }
    count++;
  }

  if (criticals) {
    GList *iter;

    g_printerr ("\n\n==== Got criticals, Return value set to 18 ====\n");

    for (iter = criticals; iter; iter = iter->next) {
      g_printerr ("     Critical error %s\n",
          ((GstValidateReport *) (iter->data))->message);
    }
    g_printerr ("\n");
  }

  gst_validate_printf (NULL, "Pipeline finished, issues found: %u\n", count);
  return ret;
}
