/* GStreamer
 *
 * Copyright (C) 2013-2016 Collabora Ltd.
 *  Author: Thiago Sousa Santos <thiago.sousa.santos@collabora.com>
 *  Author: Thibault Saunier <thibault.saunier@collabora.com>
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

#include <string.h>
#include <stdlib.h>

#include "validate.h"
#include "gst-validate-internal.h"
#include "gst-validate-report.h"
#include "gst-validate-monitor-factory.h"
#include "gst-validate-override-registry.h"
#include "gst-validate-runner.h"
#include "gst-validate-reporter.h"
#include "gst-validate-mockdecryptor.h"

GST_DEBUG_CATEGORY_STATIC (gst_validate_runner_debug);
#undef GST_CAT_DEFAULT
#define GST_CAT_DEFAULT gst_validate_runner_debug

static gboolean element_created = FALSE;

/* We create a GstValidateRunner on _init ()
 * so that we keep backward compatibility when
 * the user create a Runner after creating the pipeline
 * but the runner was actually already ready to be used.
 */
static GstValidateRunner *first_runner = NULL;

/**
 * SECTION:gst-validate-runner
 * @title: GstValidateRunner
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
 *
 */

struct _GstValidateRunnerPrivate
{
  GMutex mutex;
  GList *reports;
  GstValidateReportingDetails default_level;
  GHashTable *reports_by_type;

  /* A list of PatternLevel */
  GList *report_pattern_levels;

  /* Whether the runner was create with GST_TRACERS=validate or not) */
  gboolean user_created;

  gchar *pipeline_names;
  gchar **pipeline_names_strv;

  GList *expected_issues;
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
G_DEFINE_TYPE_WITH_PRIVATE (GstValidateRunner, gst_validate_runner,
    GST_TYPE_TRACER);

/* signals */
enum
{
  REPORT_ADDED_SIGNAL,
  STOPPING_SIGNAL,
  /* add more above */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_PARAMS,
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

static guint _signals[LAST_SIGNAL] = { 0, };

static gboolean
gst_validate_runner_should_monitor (GstValidateRunner * self,
    GstElement * element)
{
  gint i;
  GstValidateMonitor *monitor;

  if (!GST_IS_PIPELINE (element)) {
    return FALSE;
  }

  if (self->priv->user_created)
    return FALSE;

  if (!self->priv->pipeline_names_strv)
    return TRUE;

  monitor = gst_validate_get_monitor (G_OBJECT (element));

  if (monitor) {
    GST_ERROR_OBJECT (self, "Pipeline %" GST_PTR_FORMAT " is already"
        " monitored by %" GST_PTR_FORMAT " using runner: %" GST_PTR_FORMAT
        " NOT monitoring again.",
        element, monitor,
        gst_validate_reporter_get_runner (GST_VALIDATE_REPORTER (monitor)));
  }

  for (i = 0; self->priv->pipeline_names_strv[i]; i++) {
    if (g_pattern_match_simple (self->priv->pipeline_names_strv[i],
            GST_OBJECT_NAME (element)))
      return TRUE;
  }

  return FALSE;
}

static void
do_element_new (GstValidateRunner * self, guint64 ts, GstElement * element)
{
  element_created = TRUE;
  if (gst_validate_runner_should_monitor (self, element)) {
    /* the reference to the monitor is lost */
    gst_validate_monitor_factory_create (GST_OBJECT_CAST (element), self, NULL);
  }
}

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
  } else if (g_ascii_strcasecmp (str, "smart") == 0) {
    *level = GST_VALIDATE_SHOW_SMART;
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
  PatternLevel *pattern_level = g_new (PatternLevel, 1);
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
gst_validate_runner_finalize (GObject * object)
{
  GstValidateRunner *runner = GST_VALIDATE_RUNNER_CAST (object);

  if (!runner->priv->user_created)
    gst_validate_runner_exit (runner, TRUE);

  g_list_free_full (runner->priv->reports,
      (GDestroyNotify) gst_validate_report_unref);

  g_list_free_full (runner->priv->report_pattern_levels,
      (GDestroyNotify) _free_report_pattern_level);

  g_mutex_clear (&runner->priv->mutex);

  g_free (runner->priv->pipeline_names);
  g_strfreev (runner->priv->pipeline_names_strv);

  g_hash_table_foreach (runner->priv->reports_by_type, (GHFunc)
      _unref_report_list, NULL);
  g_hash_table_destroy (runner->priv->reports_by_type);

  G_OBJECT_CLASS (parent_class)->finalize (object);

  if (!runner->priv->user_created)
    gst_validate_deinit ();
}

static GObject *
gst_validate_runner_constructor (GType type, guint n_construct_params,
    GObjectConstructParam * construct_params)
{
  GObject *runner = G_OBJECT_CLASS (parent_class)->constructor (type,
      n_construct_params, construct_params);

  if (!gst_validate_is_initialized ()) {
    first_runner = GST_VALIDATE_RUNNER (runner);
    gst_validate_init ();
    first_runner = NULL;

    return runner;
  }

  return runner;
}


static void
gst_validate_runner_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstValidateRunner *runner;

  runner = GST_VALIDATE_RUNNER (object);
  switch (prop_id) {
    case PROP_PARAMS:
    {
      g_value_set_string (value, runner->priv->pipeline_names);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_validate_runner_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstValidateRunner *runner;

  runner = GST_VALIDATE_RUNNER (object);
  switch (prop_id) {
    case PROP_PARAMS:
    {
      g_free (runner->priv->pipeline_names);
      g_strfreev (runner->priv->pipeline_names_strv);

      runner->priv->pipeline_names = g_value_dup_string (value);
      if (runner->priv->pipeline_names)
        runner->priv->pipeline_names_strv =
            g_strsplit (runner->priv->pipeline_names, ",", -1);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_validate_runner_class_init (GstValidateRunnerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_validate_runner_finalize;

  gobject_class->set_property = gst_validate_runner_set_property;
  gobject_class->get_property = gst_validate_runner_get_property;
  gobject_class->constructor = gst_validate_runner_constructor;

  properties[PROP_PARAMS] =
      g_param_spec_string ("params", "Params", "Extra configuration parameters",
      NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, PROP_LAST, properties);

  _signals[REPORT_ADDED_SIGNAL] =
      g_signal_new ("report-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1,
      GST_TYPE_VALIDATE_REPORT);

  _signals[STOPPING_SIGNAL] =
      g_signal_new ("stopping", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0,
      NULL, NULL, NULL, G_TYPE_NONE, 0);

  GST_DEBUG_CATEGORY_INIT (gst_validate_runner_debug, "gstvalidaterunner",
      GST_DEBUG_FG_YELLOW, "Gst validate runner");
}

static void
gst_validate_runner_init (GstValidateRunner * runner)
{
  runner->priv = gst_validate_runner_get_instance_private (runner);

  g_mutex_init (&runner->priv->mutex);
  runner->priv->reports_by_type = g_hash_table_new (g_direct_hash,
      g_direct_equal);

  runner->priv->default_level = GST_VALIDATE_SHOW_DEFAULT;
  _init_report_levels (runner);

  runner->priv->expected_issues = gst_validate_get_test_file_expected_issues ();

  gst_tracing_register_hook (GST_TRACER (runner), "element-new",
      G_CALLBACK (do_element_new));

  gst_element_register (NULL, GST_MOCKDECRYPTOR_NAME, GST_RANK_MARGINAL,
      GST_TYPE_MOCKDECRYPTOR);
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
  GstValidateRunner *runner;

  if (first_runner) {
    runner = first_runner;
    first_runner = NULL;
  } else if (element_created) {
    gst_validate_abort
        ("Should never create a GstValidateRunner after a GstElement "
        "has been created in the same process.");

    return NULL;
  } else {
    runner = g_object_new (GST_TYPE_VALIDATE_RUNNER, NULL);
    runner->priv->user_created = TRUE;
  }

  {
    GstValidateOverrideRegistry *registry =
        gst_validate_override_registry_get ();
    GList *all_overrides =
        gst_validate_override_registry_get_override_list (registry);
    GList *i;
    for (i = all_overrides; i; i = i->next) {
      GstValidateOverride *override = (GstValidateOverride *) i->data;
      gst_validate_reporter_set_runner (GST_VALIDATE_REPORTER (override),
          runner);
    }
    g_list_free (all_overrides);
  }

  return runner;
}

/*
 * gst_validate_runner_get_default_reporting_level:
 *
 * Returns: the default #GstValidateReportingDetails used to output a report.
 */
GstValidateReportingDetails
gst_validate_runner_get_default_reporting_level (GstValidateRunner * runner)
{
  g_return_val_if_fail (GST_IS_VALIDATE_RUNNER (runner),
      GST_VALIDATE_SHOW_UNKNOWN);

  return runner->priv->default_level;
}

#if !GLIB_CHECK_VERSION(2,70,0)
#define g_pattern_spec_match_string g_pattern_match_string
#endif

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
  gchar *fixed_name;

  g_return_val_if_fail (GST_IS_VALIDATE_RUNNER (runner),
      GST_VALIDATE_SHOW_UNKNOWN);

  fixed_name = g_strdup (name);
  _replace_double_colons (fixed_name);
  for (tmp = runner->priv->report_pattern_levels; tmp; tmp = tmp->next) {
    PatternLevel *pattern_level = (PatternLevel *) tmp->data;
    if (g_pattern_spec_match_string (pattern_level->pattern, fixed_name)) {
      g_free (fixed_name);

      return pattern_level->level;
    }
  }

  g_free (fixed_name);
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
      GINT_TO_POINTER (issue_id));
  reports = g_list_append (reports, gst_validate_report_ref (report));
  g_hash_table_insert (runner->priv->reports_by_type,
      GINT_TO_POINTER (issue_id), reports);
  GST_VALIDATE_RUNNER_UNLOCK (runner);
}

static void
_dot_pipeline (GstValidateReport * report, GstStructure * config)
{
  GstPipeline *pipeline = gst_validate_reporter_get_pipeline (report->reporter);

  if (pipeline) {
    gint details = GST_DEBUG_GRAPH_SHOW_ALL;
    gchar *reporter_basename =
        g_path_get_basename (gst_validate_reporter_get_name (report->reporter));
    report->dotfile_name =
        g_strdup_printf ("%" GST_TIME_FORMAT "-validate-report-%s-on-%s-%s",
        GST_TIME_ARGS (GST_CLOCK_DIFF (_priv_start_time,
                gst_util_get_timestamp ())),
        gst_validate_report_level_get_name (report->level), reporter_basename,
        g_quark_to_string (report->issue->issue_id));

    g_free (reporter_basename);

    if (config)
      gst_structure_get_int (config, "details", &details);

    GST_DEBUG_BIN_TO_DOT_FILE (GST_BIN (pipeline),
        GST_DEBUG_GRAPH_SHOW_ALL, report->dotfile_name);

    gst_object_unref (pipeline);
  }

}

static void
gst_validate_runner_maybe_dot_pipeline (GstValidateRunner * runner,
    GstValidateReport * report)
{
  GList *config;

  if (report->level == GST_VALIDATE_REPORT_LEVEL_CRITICAL ||
      gst_validate_report_check_abort (report)) {

    _dot_pipeline (report, NULL);
    return;
  }

  for (config = gst_validate_plugin_get_config (NULL);
      config; config = config->next) {

    if (gst_structure_has_name (config->data, "core")) {
      GstValidateReportLevel level;
      const gchar *level_str,
          *action = gst_structure_get_string (config->data, "action");

      if (g_strcmp0 (action, "dot-pipeline"))
        continue;

      level_str = gst_structure_get_string (config->data, "report-level");
      level = level_str ? gst_validate_report_level_from_name (level_str) :
          GST_VALIDATE_REPORT_LEVEL_CRITICAL;

      if (level >= report->level) {
        _dot_pipeline (report, config->data);

        return;
      }
    }
  }
}

static gboolean
check_report_expected (GstValidateRunner * runner, GstValidateReport * report)
{
  GList *tmp;

#define GET_STR(name) gst_structure_get_string (known_issue, name)

  for (tmp = runner->priv->expected_issues; tmp; tmp = tmp->next) {
    GstStructure *known_issue = tmp->data;
    const gchar *id = GET_STR ("issue-id");

    if (!id || g_quark_from_string (id) == report->issue->issue_id) {
      const gchar *summary = GET_STR ("summary");

      if (!summary || !g_strcmp0 (summary, report->issue->summary)) {
        const gchar *details = GET_STR ("details");

        if (!details || g_regex_match_simple (details, report->message, 0, 0)) {
          const gchar *detected_on = GET_STR ("detected-on");

          if (!detected_on || !g_strcmp0 (detected_on, report->reporter_name)) {
            const gchar *level = GET_STR ("level");
            const gchar *report_level =
                gst_validate_report_level_get_name (report->level);

            if (!detected_on || !g_strcmp0 (level, report_level)) {
              gboolean is_sometimes;

              if (!gst_structure_get_boolean (known_issue, "sometimes",
                      &is_sometimes) || !is_sometimes) {
                runner->priv->expected_issues =
                    g_list_remove (runner->priv->expected_issues, known_issue);
                gst_structure_free (known_issue);
              }
              return TRUE;
            }
          }
        }
      }
    }
#undef GET_STR
  }

  return FALSE;
}

void
gst_validate_runner_add_report (GstValidateRunner * runner,
    GstValidateReport * report)
{
  GstValidateReportingDetails details, reporter_details, issue_type_details;

  g_return_if_fail (GST_IS_VALIDATE_RUNNER (runner));

  if (report->level == GST_VALIDATE_REPORT_LEVEL_IGNORE)
    return;

  if (check_report_expected (runner, report)) {
    GST_INFO_OBJECT (runner, "Found expected issue: %p", report);
    report->level = GST_VALIDATE_REPORT_LEVEL_EXPECTED;
  }

  gst_validate_send (json_boxed_serialize (GST_MINI_OBJECT_TYPE (report),
          report));
  gst_validate_runner_maybe_dot_pipeline (runner, report);

  details = reporter_details =
      gst_validate_reporter_get_reporting_level (report->reporter);
  issue_type_details =
      gst_validate_runner_get_reporting_level_for_name (runner,
      g_quark_to_string (report->issue->issue_id));

  if (reporter_details == GST_VALIDATE_SHOW_UNKNOWN)
    details = issue_type_details;

  /* Let's use our own reporting strategy */
  if (details == GST_VALIDATE_SHOW_UNKNOWN) {
    gst_validate_report_set_reporting_level (report,
        runner->priv->default_level);
    switch (runner->priv->default_level) {
      case GST_VALIDATE_SHOW_NONE:
        return;
      case GST_VALIDATE_SHOW_SMART:
        if (!gst_validate_report_check_abort (report) &&
            report->level != GST_VALIDATE_REPORT_LEVEL_CRITICAL &&
            !report->trace) {
          synthesize_reports (runner, report);
          return;
        }
        break;
      case GST_VALIDATE_SHOW_SYNTHETIC:
        if (!report->trace) {
          synthesize_reports (runner, report);
          return;
        }
      default:
        break;
    }
  } else if (details == GST_VALIDATE_SHOW_NONE) {
    GST_DEBUG ("Not reporting.");
    return;
  }

  GST_VALIDATE_RUNNER_LOCK (runner);
  runner->priv->reports =
      g_list_append (runner->priv->reports, gst_validate_report_ref (report));
  GST_VALIDATE_RUNNER_UNLOCK (runner);

  g_signal_emit (runner, _signals[REPORT_ADDED_SIGNAL], 0, report);
}

/**
 * gst_validate_runner_get_reports_count:
 * @runner: The #GstValidateRunner to get the number of reports from
 *
 * Get the number of reports present in the runner:
 *
 * Returns: The number of reports present in the runner.
 */
guint
gst_validate_runner_get_reports_count (GstValidateRunner * runner)
{
  GList *tmp;
  guint l;

  g_return_val_if_fail (GST_IS_VALIDATE_RUNNER (runner), 0);

  GST_VALIDATE_RUNNER_LOCK (runner);
  l = g_list_length (runner->priv->reports);
  for (tmp = runner->priv->reports; tmp; tmp = tmp->next) {
    GstValidateReport *report = (GstValidateReport *) tmp->data;
    l += g_list_length (report->repeated_reports);
  }
  l += g_hash_table_size (runner->priv->reports_by_type);
  GST_VALIDATE_RUNNER_UNLOCK (runner);

  return l;
}

/**
 * gst_validate_runner_get_reports:
 * @runner: The #GstValidateRunner
 *
 * Returns: (element-type GstValidateReport) (transfer full): The list of reports
 */
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

  /* Take the lock so the hash table won't be modified while we are iterating
   * over it */
  GST_VALIDATE_RUNNER_LOCK (runner);
  g_hash_table_iter_init (&iter, runner->priv->reports_by_type);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    GstValidateReport *report;
    reports = (GList *) value;

    if (!reports)
      continue;

    report = (GstValidateReport *) (reports->data);

    gst_validate_report_print_level (report);
    gst_validate_report_print_detected_on (report);

    if (report->level == GST_VALIDATE_REPORT_LEVEL_CRITICAL) {
      criticals = g_list_append (criticals, report);
      gst_validate_report_print_details (report);
    } else if (report->issue->flags & GST_VALIDATE_ISSUE_FLAGS_FULL_DETAILS)
      gst_validate_report_print_details (report);

    for (tmp = g_list_next (reports); tmp; tmp = tmp->next) {
      report = (GstValidateReport *) tmp->data;
      gst_validate_report_print_detected_on (report);

      if ((report->level == GST_VALIDATE_REPORT_LEVEL_CRITICAL) ||
          (report->issue->flags & GST_VALIDATE_ISSUE_FLAGS_FULL_DETAILS)) {
        if (report->level == GST_VALIDATE_REPORT_LEVEL_CRITICAL)
          criticals = g_list_append (criticals, report);
        gst_validate_report_print_details (report);
      }
    }
    report = (GstValidateReport *) (reports->data);
    gst_validate_report_print_description (report);
    gst_validate_printf (NULL, "\n");
  }
  GST_VALIDATE_RUNNER_UNLOCK (runner);

  return criticals;
}

/**
 * gst_validate_runner_printf:
 * @runner: The #GstValidateRunner to print all the reports for
 *
 * Prints all the reports on the terminal or on wherever is set
 * in the `GST_VALIDATE_FILE` env variable.
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

  g_return_val_if_fail (GST_IS_VALIDATE_RUNNER (runner), 1);

  criticals = _do_report_synthesis (runner);
  reports = gst_validate_runner_get_reports (runner);
  for (tmp = reports; tmp; tmp = tmp->next) {
    GstValidateReport *report = (GstValidateReport *) tmp->data;

    if (gst_validate_report_should_print (report))
      gst_validate_report_printf (report);

    if (report->level == GST_VALIDATE_REPORT_LEVEL_CRITICAL) {
      criticals = g_list_append (criticals, report);
    }
  }

  if (criticals) {
    GList *iter;
    g_printerr ("\n\n**Got criticals. Return value set to 18**:\n");
    ret = 18;
    for (iter = criticals; iter; iter = iter->next) {
      g_printerr ("  * critical error %s\n",
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
  GList *tmp, *configs;

  g_return_val_if_fail (GST_IS_VALIDATE_RUNNER (runner), 1);

  g_signal_emit (runner, _signals[STOPPING_SIGNAL], 0);
  if (print_result) {
    ret = gst_validate_runner_printf (runner);
  } else {
    GList *tmp;
    for (tmp = runner->priv->reports; tmp; tmp = tmp->next) {
      GstValidateReport *report = (GstValidateReport *) tmp->data;
      if (report->level == GST_VALIDATE_REPORT_LEVEL_CRITICAL)
        ret = 18;
    }
  }

  configs = gst_validate_get_config (NULL);
  for (tmp = configs; tmp; tmp = tmp->next) {
    if (!gst_structure_has_field (tmp->data, "__n_usages__")) {
      gst_validate_error_structure (tmp->data,
          "Unused config: '%" GST_PTR_FORMAT "'", tmp->data);
    }
  }
  g_list_free (configs);

  for (tmp = runner->priv->expected_issues; tmp; tmp = tmp->next) {
    GstStructure *known_issue = tmp->data;
    gboolean is_sometimes;

    if (!gst_structure_get_boolean (known_issue, "sometimes", &is_sometimes)
        || !is_sometimes) {
      GstStructure *tmpstruct = gst_structure_copy (known_issue);
      gst_structure_remove_fields (tmpstruct, "__debug__", "__lineno__",
          "__filename__", NULL);
      /* Ideally we should report an issue here.. but we do not have a reporter */
      gst_validate_error_structure (known_issue,
          "Expected issue didn't happen: '%" GST_PTR_FORMAT "'", tmpstruct);
      gst_structure_free (tmpstruct);
    }
  }

  g_list_free_full (runner->priv->expected_issues,
      (GDestroyNotify) gst_structure_free);
  runner->priv->expected_issues = NULL;

  return ret;
}

void
gst_validate_init_runner (void)
{
  if (!first_runner) {
    first_runner = g_object_new (GST_TYPE_VALIDATE_RUNNER, NULL);
    first_runner->priv->user_created = TRUE;
  }                             /* else the first runner has been created through the GST_TRACERS system */
}

void
gst_validate_deinit_runner (void)
{
  g_clear_object (&first_runner);
}

GstValidateReportingDetails
gst_validate_runner_get_default_reporting_details (GstValidateRunner * runner)
{
  g_return_val_if_fail (GST_IS_VALIDATE_RUNNER (runner),
      GST_VALIDATE_SHOW_UNKNOWN);

  return runner->priv->default_level;
}

#ifdef __GST_VALIDATE_PLUGIN
static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_tracer_register (plugin, "validate", GST_TYPE_VALIDATE_RUNNER))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, validatetracer,
    "GStreamer Validate tracers", plugin_init, VERSION, GST_LICENSE,
    GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
#endif /* __GST_VALIDATE_PLUGIN */
