/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-qa-runner.c - QA Runner class
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

#include "gst-qa-runner.h"
#include "gst-qa-report.h"
#include "gst-qa-monitor-factory.h"
#include "gst-qa-override-registry.h"

/**
 * SECTION:gst-qa-runner
 * @short_description: Class that runs Gst QA tests for a pipeline
 *
 * TODO
 */

GST_DEBUG_CATEGORY_STATIC (gst_qa_runner_debug);
#define GST_CAT_DEFAULT gst_qa_runner_debug

#define _do_init \
  GST_DEBUG_CATEGORY_INIT (gst_qa_runner_debug, "qa_runner", 0, "QA Runner");
#define gst_qa_runner_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstQaRunner, gst_qa_runner, G_TYPE_OBJECT, _do_init);

/* signals */
enum
{
  REPORT_ADDED_SIGNAL,
  /* add more above */
  LAST_SIGNAL
};

static guint _signals[LAST_SIGNAL] = { 0 };

static void
gst_qa_runner_dispose (GObject * object)
{
  GstQaRunner *runner = GST_QA_RUNNER_CAST (object);

  g_slist_free_full (runner->reports, (GDestroyNotify) gst_qa_report_unref);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_qa_runner_class_init (GstQaRunnerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = gst_qa_runner_dispose;

  /* init the report system (can be called multiple times) */
  gst_qa_report_init ();

  /* Ensure we load overrides before any use of a monitor */
  gst_qa_override_registry_preload ();

  _signals[REPORT_ADDED_SIGNAL] =
      g_signal_new ("report-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1,
      GST_TYPE_QA_REPORT);
}

static void
gst_qa_runner_init (GstQaRunner * runner)
{
  runner->setup = FALSE;
}

/**
 * gst_qa_runner_new:
 */
GstQaRunner *
gst_qa_runner_new (void)
{
  return g_object_new (GST_TYPE_QA_RUNNER, NULL);
}

void
gst_qa_runner_add_report (GstQaRunner * runner, GstQaReport * report)
{
  runner->reports = g_slist_prepend (runner->reports, report);

  g_signal_emit (runner, _signals[REPORT_ADDED_SIGNAL], 0, report);
}

guint
gst_qa_runner_get_reports_count (GstQaRunner * runner)
{
  g_return_val_if_fail (runner != NULL, 0);
  return g_slist_length (runner->reports);
}

GSList *
gst_qa_runner_get_reports (GstQaRunner * runner)
{
  /* TODO should we need locking or put in htte docs to always call this
   * after pipeline ends? */
  return runner->reports;
}
