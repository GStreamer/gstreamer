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

#include <string.h>
#include <stdlib.h>

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
 * Allows you to test a pipeline within GstValidate. It is the object where
 * all issue reporting is done.
 *
 * In the tools using GstValidate the only minimal code to be able to monitor
 * your pipelines is:
 *
 * |[
 *  GstPipeline *pipeline = gst_pipeline_new ("monitored-pipeline");
 *  GstValidateRunner *runner = gst_validate_runner_new ();
 *  GstValidateMonitor *monitor = gst_validate_monitor_factory_create (
 *          GST_OBJECT (pipeline), runner, NULL);
 *
 *  // Run the pipeline and do whatever you want with it
 *
 *  // In that same order
 *  gst_object_unref (pipeline);
 *  gst_object_unref (runner);
 *  gst_object_unref (monitor);
 * ]|
 */

struct _GstValidateRunnerPrivate
{
  GMutex mutex;
  GList *reports;
  GstValidateReportingLevel default_level;

  /* A list of PatternLevel */
  GList *report_pattern_levels;
};

/* Describes the reporting level to apply to a name pattern */
typedef struct _PatternLevel
{
  GPatternSpec *pattern;
  GstValidateReportingLevel level;
} PatternLevel;

#define GST_VALIDATE_RUNNER_LOCK(r)			\
  G_STMT_START {					\
  GST_LOG_OBJECT (r, "About to lock %p", &GST_VALIDATE_RUNNER_CAST(r)->priv->mutex); \
  (g_mutex_lock (&GST_VALIDATE_RUNNER_CAST(r)->priv->mutex));		\
  GST_LOG_OBJECT (r, "Acquired lock %p", &GST_VALIDATE_RUNNER_CAST(r)->priv->mutex); \
  } G_STMT_END

#define GST_VALIDATE_RUNNER_UNLOCK(r)			\
  G_STMT_START {					\
  GST_LOG_OBJECT (r, "About to unlock %p", &GST_VALIDATE_RUNNER_CAST(r)->priv->mutex); \
  (g_mutex_unlock (&GST_VALIDATE_RUNNER_CAST(r)->priv->mutex));		\
  GST_LOG_OBJECT (r, "Released lock %p", &GST_VALIDATE_RUNNER_CAST(r)->priv->mutex); \
  } G_STMT_END

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

static gboolean
_parse_reporting_level (gchar * str, GstValidateReportingLevel * level)
{
  if (!str)
    return FALSE;

  /* works in place */
  g_strstrip (str);

  if (g_ascii_isdigit (str[0])) {
    unsigned long l;
    char *endptr;
    l = strtoul (str, &endptr, 10);
    if (endptr > str && endptr[0] == 0) {
      *level = (GstValidateReportingLevel) l;
    } else {
      return FALSE;
    }
  } else if (g_ascii_strcasecmp (str, "none") == 0) {
    *level = GST_VALIDATE_REPORTING_LEVEL_NONE;
  } else if (g_ascii_strcasecmp (str, "synthetic") == 0) {
    *level = GST_VALIDATE_REPORTING_LEVEL_SYNTHETIC;
  } else if (g_ascii_strcasecmp (str, "subchain") == 0) {
    *level = GST_VALIDATE_REPORTING_LEVEL_SUBCHAIN;
  } else if (g_ascii_strcasecmp (str, "monitor") == 0) {
    *level = GST_VALIDATE_REPORTING_LEVEL_MONITOR;
  } else if (g_ascii_strcasecmp (str, "all") == 0) {
    *level = GST_VALIDATE_REPORTING_LEVEL_ALL;
  } else
    return FALSE;

  return TRUE;
}

static void
_free_report_pattern_level (PatternLevel * pattern_level)
{
  g_pattern_spec_free (pattern_level->pattern);
  g_free (pattern_level);
}

static void
_set_reporting_level_for_name (GstValidateRunner * runner,
    const gchar * pattern, GstValidateReportingLevel level)
{
  PatternLevel *pattern_level = g_malloc (sizeof (PatternLevel));
  GPatternSpec *pattern_spec = g_pattern_spec_new (pattern);

  pattern_level->pattern = pattern_spec;
  pattern_level->level = level;

  runner->priv->report_pattern_levels =
      g_list_append (runner->priv->report_pattern_levels, pattern_level);
}

static void
_set_report_levels_from_string (GstValidateRunner * self, const gchar * list)
{
  gchar **split;
  gchar **walk;

  g_assert (list);

  GST_DEBUG_OBJECT (self, "setting report levels from string [%s]", list);

  split = g_strsplit (list, ",", 0);

  for (walk = split; *walk; walk++) {
    if (strchr (*walk, ':')) {
      gchar **values = g_strsplit (*walk, ":", 2);

      if (values[0] && values[1]) {
        GstValidateReportingLevel level;

        if (_parse_reporting_level (values[1], &level))
          _set_reporting_level_for_name (self, values[0], level);
      }

      g_strfreev (values);
    } else {
      GstValidateReportingLevel level;

      if (_parse_reporting_level (*walk, &level))
        self->priv->default_level = level;
    }
  }

  g_strfreev (split);
}

static void
_init_report_levels (GstValidateRunner * self)
{
  const gchar *env;

  env = g_getenv ("GST_VALIDATE_REPORT_LEVEL");
  if (env)
    _set_report_levels_from_string (self, env);
}

static void
gst_validate_runner_dispose (GObject * object)
{
  GstValidateRunner *runner = GST_VALIDATE_RUNNER_CAST (object);

  g_list_free_full (runner->priv->reports,
      (GDestroyNotify) gst_validate_report_unref);
  g_list_free_full (runner->priv->report_pattern_levels,
      (GDestroyNotify) _free_report_pattern_level);

  g_mutex_clear (&runner->priv->mutex);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_validate_runner_class_init (GstValidateRunnerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = gst_validate_runner_dispose;

  g_type_class_add_private (klass, sizeof (GstValidateRunnerPrivate));

  _signals[REPORT_ADDED_SIGNAL] =
      g_signal_new ("report-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1,
      GST_TYPE_VALIDATE_REPORT);
}

static void
gst_validate_runner_init (GstValidateRunner * runner)
{
  runner->priv = G_TYPE_INSTANCE_GET_PRIVATE (runner, GST_TYPE_VALIDATE_RUNNER,
      GstValidateRunnerPrivate);
  g_mutex_init (&runner->priv->mutex);

  runner->priv->default_level = GST_VALIDATE_REPORTING_LEVEL_DEFAULT;
  _init_report_levels (runner);
}

/**
 * gst_validate_runner_new:
 *
 * Create a new #GstValidateRunner
 *
 * Returns: A newly created #GstValidateRunner
 */
GstValidateRunner *
gst_validate_runner_new (void)
{
  return g_object_new (GST_TYPE_VALIDATE_RUNNER, NULL);
}

/*
 * gst_validate_runner_get_default_reporting_level:
 *
 * Returns: the default #GstValidateReportingLevel used to output a report.
 */
GstValidateReportingLevel
gst_validate_runner_get_default_reporting_level (GstValidateRunner * runner)
{
  return runner->priv->default_level;
}

/*
 * gst_validate_runner_get_reporting_level_for_name:
 *
 * Returns: the #GstValidateReportingLevel that will be applied for a given name.
 * If no pattern was set for such a name, this function will return
 * #GST_VALIDATE_REPORTING_LEVEL_UNKNOWN, and reporting for that name will
 * default to the global reporting level.
 */
GstValidateReportingLevel
gst_validate_runner_get_reporting_level_for_name (GstValidateRunner * runner,
    const gchar * name)
{
  GList *tmp;

  for (tmp = runner->priv->report_pattern_levels; tmp; tmp = tmp->next) {
    PatternLevel *pattern_level = (PatternLevel *) tmp->data;
    if (g_pattern_match_string (pattern_level->pattern, name))
      return pattern_level->level;
  }

  return GST_VALIDATE_REPORTING_LEVEL_UNKNOWN;
}

void
gst_validate_runner_add_report (GstValidateRunner * runner,
    GstValidateReport * report)
{
  GST_VALIDATE_RUNNER_LOCK (runner);
  runner->priv->reports = g_list_append (runner->priv->reports, report);
  GST_VALIDATE_RUNNER_UNLOCK (runner);

  g_signal_emit (runner, _signals[REPORT_ADDED_SIGNAL], 0, report);
}

/**
 * gst_validate_runner_get_reports_count:
 * @runner: The $GstValidateRunner to get the number of report from
 *
 * Get the number of reports present in the runner:
 *
 * Returns: The number of report present in the runner.
 */
guint
gst_validate_runner_get_reports_count (GstValidateRunner * runner)
{
  guint l;

  g_return_val_if_fail (runner != NULL, 0);

  GST_VALIDATE_RUNNER_LOCK (runner);
  l = g_list_length (runner->priv->reports);
  GST_VALIDATE_RUNNER_UNLOCK (runner);

  return l;
}

GList *
gst_validate_runner_get_reports (GstValidateRunner * runner)
{
  GList *ret;

  GST_VALIDATE_RUNNER_LOCK (runner);
  ret =
      g_list_copy_deep (runner->priv->reports,
      (GCopyFunc) gst_validate_report_ref, NULL);
  GST_VALIDATE_RUNNER_UNLOCK (runner);

  return ret;
}

/**
 * gst_validate_runner_printf:
 * @runner: The #GstValidateRunner to print all the reports for
 *
 * Prints all the report on the terminal or on wherever set
 * in the #GST_VALIDATE_FILE env variable.
 *
 * Returns: 0 if no critical error has been found and 18 if a critical
 * error has been detected. That return value is usually to be used as
 * exit code of the application.
 * */
int
gst_validate_runner_printf (GstValidateRunner * runner)
{
  GList *reports, *tmp;
  guint count = 0;
  int ret = 0;
  GList *criticals = NULL;

  reports = gst_validate_runner_get_reports (runner);
  for (tmp = reports; tmp; tmp = tmp->next) {
    GstValidateReport *report = tmp->data;

    if (gst_validate_report_should_print (report))
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

  g_list_free_full (reports, (GDestroyNotify) gst_validate_report_unref);
  gst_validate_printf (NULL, "Issues found: %u\n", count);
  g_list_free (criticals);
  return ret;
}
