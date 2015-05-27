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
  GstValidateReportingDetails default_level;
  GHashTable *reports_by_type;

  /* A list of PatternLevel */
  GList *report_pattern_levels;
};

/* Describes the reporting level to apply to a name pattern */
typedef struct _PatternLevel
{
  GPatternSpec *pattern;
  GstValidateReportingDetails level;
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
  STOPPING_SIGNAL,
  /* add more above */
  LAST_SIGNAL
};

static guint _signals[LAST_SIGNAL] = { 0 };

static gboolean
_parse_reporting_level (gchar * str, GstValidateReportingDetails * level)
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
      *level = (GstValidateReportingDetails) l;
    } else {
      return FALSE;
    }
  } else if (g_ascii_strcasecmp (str, "none") == 0) {
    *level = GST_VALIDATE_SHOW_NONE;
  } else if (g_ascii_strcasecmp (str, "synthetic") == 0) {
    *level = GST_VALIDATE_SHOW_SYNTHETIC;
  } else if (g_ascii_strcasecmp (str, "subchain") == 0) {
    *level = GST_VALIDATE_SHOW_SUBCHAIN;
  } else if (g_ascii_strcasecmp (str, "monitor") == 0) {
    *level = GST_VALIDATE_SHOW_MONITOR;
  } else if (g_ascii_strcasecmp (str, "all") == 0) {
    *level = GST_VALIDATE_SHOW_ALL;
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
    const gchar * pattern, GstValidateReportingDetails level)
{
  PatternLevel *pattern_level = g_malloc (sizeof (PatternLevel));
  GPatternSpec *pattern_spec = g_pattern_spec_new (pattern);

  pattern_level->pattern = pattern_spec;
  pattern_level->level = level;

  /* Allow the user to single out a pad with the "element-name__pad-name" syntax
   */
  if (g_strrstr (pattern, "__"))
    runner->priv->report_pattern_levels =
        g_list_prepend (runner->priv->report_pattern_levels, pattern_level);
  else
    runner->priv->report_pattern_levels =
        g_list_append (runner->priv->report_pattern_levels, pattern_level);
}

static void
_replace_double_colons (gchar * word)
{
  while (word) {
    word = strstr (word, "::");
    if (word) {
      word[0] = '_';
      word[1] = '_';
    }
  }
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
    _replace_double_colons (*walk);
    if (strchr (*walk, ':')) {
      gchar **values = g_strsplit (*walk, ":", 2);

      if (values[0] && values[1]) {
        GstValidateReportingDetails level;

        if (_parse_reporting_level (values[1], &level))
          _set_reporting_level_for_name (self, values[0], level);
      }

      g_strfreev (values);
    } else {
      GstValidateReportingDetails level;

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

  env = g_getenv ("GST_VALIDATE_REPORTING_DETAILS");
  if (env)
    _set_report_levels_from_string (self, env);
}

static void
_unref_report_list (gpointer unused, GList * reports, gpointer unused_too)
{
  g_list_free_full (reports, (GDestroyNotify) gst_validate_report_unref);
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

  g_hash_table_foreach (runner->priv->reports_by_type, (GHFunc)
      _unref_report_list, NULL);
  g_hash_table_destroy (runner->priv->reports_by_type);
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

  _signals[STOPPING_SIGNAL] =
      g_signal_new ("stopping", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0,
      NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
gst_validate_runner_init (GstValidateRunner * runner)
{
  runner->priv = G_TYPE_INSTANCE_GET_PRIVATE (runner, GST_TYPE_VALIDATE_RUNNER,
      GstValidateRunnerPrivate);
  g_mutex_init (&runner->priv->mutex);

  runner->priv->reports_by_type = g_hash_table_new (g_direct_hash,
      g_direct_equal);

  runner->priv->default_level = GST_VALIDATE_SHOW_DEFAULT;
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
 * Returns: the default #GstValidateReportingDetails used to output a report.
 */
GstValidateReportingDetails
gst_validate_runner_get_default_reporting_level (GstValidateRunner * runner)
{
  return runner->priv->default_level;
}

/*
 * gst_validate_runner_get_reporting_level_for_name:
 *
 * Returns: the #GstValidateReportingDetails that will be applied for a given name.
 * If no pattern was set for such a name, this function will return
 * #GST_VALIDATE_SHOW_UNKNOWN, and reporting for that name will
 * default to the global reporting level.
 */
GstValidateReportingDetails
gst_validate_runner_get_reporting_level_for_name (GstValidateRunner * runner,
    const gchar * name)
{
  GList *tmp;

  for (tmp = runner->priv->report_pattern_levels; tmp; tmp = tmp->next) {
    PatternLevel *pattern_level = (PatternLevel *) tmp->data;
    if (g_pattern_match_string (pattern_level->pattern, name))
      return pattern_level->level;
  }

  return GST_VALIDATE_SHOW_UNKNOWN;
}

static void
synthesize_reports (GstValidateRunner * runner, GstValidateReport * report)
{
  GstValidateIssueId issue_id;
  GList *reports;

  issue_id = report->issue->issue_id;

  GST_VALIDATE_RUNNER_LOCK (runner);
  reports =
      g_hash_table_lookup (runner->priv->reports_by_type,
      (gconstpointer) issue_id);
  reports = g_list_append (reports, gst_validate_report_ref (report));
  g_hash_table_insert (runner->priv->reports_by_type, (gpointer) issue_id,
      reports);
  GST_VALIDATE_RUNNER_UNLOCK (runner);
}

void
gst_validate_runner_add_report (GstValidateRunner * runner,
    GstValidateReport * report)
{
  GstValidateReportingDetails reporter_level =
      gst_validate_reporter_get_reporting_level (report->reporter);

  /* Let's use our own reporting strategy */
  if (reporter_level == GST_VALIDATE_SHOW_UNKNOWN) {
    gst_validate_report_set_reporting_level (report,
        runner->priv->default_level);
    switch (runner->priv->default_level) {
      case GST_VALIDATE_SHOW_NONE:
        return;
      case GST_VALIDATE_SHOW_SYNTHETIC:
        synthesize_reports (runner, report);
        return;
      default:
        break;
    }
  }

  GST_VALIDATE_RUNNER_LOCK (runner);
  runner->priv->reports =
      g_list_append (runner->priv->reports, gst_validate_report_ref (report));
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
  GList *tmp;
  guint l;

  g_return_val_if_fail (runner != NULL, 0);

  GST_VALIDATE_RUNNER_LOCK (runner);
  l = g_list_length (runner->priv->reports);
  for (tmp = runner->priv->reports; tmp; tmp = tmp->next)
    l += g_list_length (((GstValidateReport *) tmp->data)->repeated_reports);
  l += g_hash_table_size (runner->priv->reports_by_type);
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

static GList *
_do_report_synthesis (GstValidateRunner * runner)
{
  GHashTableIter iter;
  GList *reports, *tmp;
  gpointer key, value;
  GList *criticals = NULL;

  g_hash_table_iter_init (&iter, runner->priv->reports_by_type);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    GstValidateReport *report;
    reports = (GList *) value;

    if (!reports)
      continue;

    report = (GstValidateReport *) (reports->data);
    gst_validate_report_print_level (report);
    gst_validate_report_print_detected_on (report);

    if (report->level == GST_VALIDATE_REPORT_LEVEL_CRITICAL)
      criticals = g_list_append (criticals, report);

    for (tmp = g_list_next (reports); tmp; tmp = tmp->next) {
      report = (GstValidateReport *) (tmp->data);
      gst_validate_report_print_detected_on (report);

      if (report->level == GST_VALIDATE_REPORT_LEVEL_CRITICAL)
        criticals = g_list_append (criticals, report);
    }
    report = (GstValidateReport *) (reports->data);
    gst_validate_report_print_description (report);
    gst_validate_printf (NULL, "\n");
  }

  return criticals;
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
 */
int
gst_validate_runner_printf (GstValidateRunner * runner)
{
  GList *reports, *tmp;
  int ret = 0;
  GList *criticals = NULL;

  criticals = _do_report_synthesis (runner);
  reports = gst_validate_runner_get_reports (runner);
  for (tmp = reports; tmp; tmp = tmp->next) {
    GstValidateReport *report = tmp->data;

    if (gst_validate_report_should_print (report))
      gst_validate_report_printf (report);

    if (report->level == GST_VALIDATE_REPORT_LEVEL_CRITICAL) {
      criticals = g_list_append (criticals, tmp->data);
    }
  }

  if (criticals) {
    GList *iter;

    g_printerr ("\n\n==== Got criticals, Return value set to 18 ====\n");
    ret = 18;

    for (iter = criticals; iter; iter = iter->next) {
      g_printerr ("     Critical error %s\n",
          ((GstValidateReport *) (iter->data))->message);
    }
    g_printerr ("\n");
  }

  g_list_free_full (reports, (GDestroyNotify) gst_validate_report_unref);
  g_list_free (criticals);
  gst_validate_printf (NULL, "Issues found: %u\n",
      gst_validate_runner_get_reports_count (runner));
  return ret;
}

int
gst_validate_runner_exit (GstValidateRunner * runner, gboolean print_result)
{
  gint ret = 0;

  g_signal_emit (runner, _signals[STOPPING_SIGNAL], 0);

  if (print_result) {
    ret = gst_validate_runner_printf (runner);
  } else {
    GList *tmp;

    for (tmp = runner->priv->reports; tmp; tmp = tmp->next) {
      GstValidateReport *report = tmp->data;

      if (report->level == GST_VALIDATE_REPORT_LEVEL_CRITICAL)
        ret = 18;
    }
  }

  return ret;
}
