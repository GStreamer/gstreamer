/* GStreamer
 *
 * Copyright (C) 2018-2019 Igalia S.L.
 * Copyright (C) 2018 Metrological Group B.V.
 *  Author: Alicia Boya García <aboya@igalia.com>
 *
 * gstvalidateflow.c: A plugin to record streams and match them to
 * expectation files.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "../../gst/validate/validate.h"
#include "../../gst/validate/gst-validate-utils.h"
#include "../../gst/validate/gst-validate-report.h"
#include "formatting.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>

#define VALIDATE_FLOW_MISMATCH g_quark_from_static_string ("validateflow::mismatch")

typedef enum _ValidateFlowMode
{
  VALIDATE_FLOW_MODE_WRITING_EXPECTATIONS,
  VALIDATE_FLOW_MODE_WRITING_ACTUAL_RESULTS
} ValidateFlowMode;

typedef struct _ValidateFlowOverride
{
  GstValidateOverride parent;

  const gchar *pad_name;
  gboolean record_buffers;
  gchar *expectations_dir;
  gchar *actual_results_dir;
  gboolean error_writing_file;
  gchar **caps_properties;
  gboolean record_stream_id;

  gchar *expectations_file_path;
  gchar *actual_results_file_path;
  ValidateFlowMode mode;

  /* output_file will refer to the expectations file if it did not exist,
   * or to the actual results file otherwise. */
  gchar *output_file_path;
  FILE *output_file;
  GMutex output_file_mutex;

} ValidateFlowOverride;

GList *all_overrides = NULL;

static void validate_flow_override_finalize (GObject * object);
static void _runner_set (GObject * object, GParamSpec * pspec,
    gpointer user_data);
static void runner_stopping (GstValidateRunner * runner,
    ValidateFlowOverride * flow);

#define VALIDATE_TYPE_FLOW_OVERRIDE validate_flow_override_get_type ()
G_DECLARE_FINAL_TYPE (ValidateFlowOverride, validate_flow_override,
    VALIDATE, FLOW_OVERRIDE, GstValidateOverride);
G_DEFINE_TYPE (ValidateFlowOverride, validate_flow_override,
    GST_TYPE_VALIDATE_OVERRIDE);

void
validate_flow_override_init (ValidateFlowOverride * self)
{
}

void
validate_flow_override_class_init (ValidateFlowOverrideClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = validate_flow_override_finalize;

  g_assert (gst_validate_is_initialized ());

  gst_validate_issue_register (gst_validate_issue_new
      (VALIDATE_FLOW_MISMATCH,
          "The recorded log does not match the expectation file.",
          "The recorded log does not match the expectation file.",
          GST_VALIDATE_REPORT_LEVEL_CRITICAL));
}

static void
validate_flow_override_vprintf (ValidateFlowOverride * flow, const char *format,
    va_list ap)
{
  g_mutex_lock (&flow->output_file_mutex);
  if (!flow->error_writing_file && vfprintf (flow->output_file, format, ap) < 0) {
    GST_ERROR_OBJECT (flow, "Writing to file %s failed",
        flow->output_file_path);
    flow->error_writing_file = TRUE;
  }
  g_mutex_unlock (&flow->output_file_mutex);
}

static void
validate_flow_override_printf (ValidateFlowOverride * flow, const char *format,
    ...)
{
  va_list ap;
  va_start (ap, format);
  validate_flow_override_vprintf (flow, format, ap);
  va_end (ap);
}

static void
validate_flow_override_event_handler (GstValidateOverride * override,
    GstValidateMonitor * pad_monitor, GstEvent * event)
{
  ValidateFlowOverride *flow = VALIDATE_FLOW_OVERRIDE (override);
  gchar *event_string;

  if (flow->error_writing_file)
    return;

  event_string = validate_flow_format_event (event, flow->record_stream_id,
      (const gchar * const *) flow->caps_properties);
  validate_flow_override_printf (flow, "event %s\n", event_string);
  g_free (event_string);
}

static void
validate_flow_override_buffer_handler (GstValidateOverride * override,
    GstValidateMonitor * pad_monitor, GstBuffer * buffer)
{
  ValidateFlowOverride *flow = VALIDATE_FLOW_OVERRIDE (override);
  gchar *buffer_str;

  if (flow->error_writing_file || !flow->record_buffers)
    return;

  buffer_str = validate_flow_format_buffer (buffer);
  validate_flow_override_printf (flow, "buffer: %s\n", buffer_str);
  g_free (buffer_str);
}

static gchar **
parse_caps_properties_setting (const ValidateFlowOverride * flow,
    GstStructure * config)
{
  const GValue *list;
  gchar **parsed_list;
  guint i, size;

  list = gst_structure_get_value (config, "caps-properties");
  if (!list)
    return NULL;

  if (!GST_VALUE_HOLDS_LIST (list)) {
    GST_ERROR_OBJECT (flow,
        "caps-properties must have type list of string, e.g. caps-properties={ width, height };");
    return NULL;
  }

  size = gst_value_list_get_size (list);
  parsed_list = g_malloc_n (size + 1, sizeof (gchar *));
  for (i = 0; i < size; i++)
    parsed_list[i] = g_value_dup_string (gst_value_list_get_value (list, i));
  parsed_list[i] = NULL;
  return parsed_list;
}

static ValidateFlowOverride *
validate_flow_override_new (GstStructure * config)
{
  ValidateFlowOverride *flow;
  GstValidateOverride *override;

  flow = g_object_new (VALIDATE_TYPE_FLOW_OVERRIDE, NULL);
  override = GST_VALIDATE_OVERRIDE (flow);

  /* pad: Name of the pad where flowing buffers and events will be monitorized. */
  flow->pad_name = gst_structure_get_string (config, "pad");
  if (!flow->pad_name) {
    g_error ("pad property is mandatory, not found in %s",
        gst_structure_to_string (config));
  }

  /* record-buffers: Whether buffers will be written to the expectation log. */
  flow->record_buffers = FALSE;
  gst_structure_get_boolean (config, "record-buffers", &flow->record_buffers);

  /* caps-properties: Caps events can include many dfferent properties, but
   * many of these may be irrelevant for some tests. If this option is set,
   * only the listed properties will be written to the expectation log. */
  flow->caps_properties = parse_caps_properties_setting (flow, config);

  /* record-stream-id: stream-id's are often non reproducible (this is the case
   * for basesrc, for instance). For this reason, they are omitted by default
   * when recording a stream-start event. This setting allows to override that
   * behavior. */
  flow->record_stream_id = FALSE;
  gst_structure_get_boolean (config, "record-stream-id",
      &flow->record_stream_id);

  /* expectations-dir: Path to the directory where the expectations will be
   * written if they don't exist, relative to the current working directory.
   * By default the current working directory is used. */
  flow->expectations_dir =
      g_strdup (gst_structure_get_string (config, "expectations-dir"));
  if (!flow->expectations_dir)
    flow->expectations_dir = g_strdup (".");

  /* actual-results-dir: Path to the directory where the events will be
   * recorded. The expectation file will be compared to this. */
  flow->actual_results_dir =
      g_strdup (gst_structure_get_string (config, "actual-results-dir"));
  if (!flow->actual_results_dir)
    flow->actual_results_dir = g_strdup (".");

  {
    gchar *expectations_file_name =
        g_strdup_printf ("log-%s-expected", flow->pad_name);
    gchar *actual_results_file_name =
        g_strdup_printf ("log-%s-actual", flow->pad_name);
    flow->expectations_file_path =
        g_build_path (G_DIR_SEPARATOR_S, flow->expectations_dir,
        expectations_file_name, NULL);
    flow->actual_results_file_path =
        g_build_path (G_DIR_SEPARATOR_S, flow->actual_results_dir,
        actual_results_file_name, NULL);
    g_free (expectations_file_name);
    g_free (actual_results_file_name);
  }

  if (g_file_test (flow->expectations_file_path, G_FILE_TEST_EXISTS)) {
    flow->mode = VALIDATE_FLOW_MODE_WRITING_ACTUAL_RESULTS;
    flow->output_file_path = g_strdup (flow->actual_results_file_path);
  } else {
    flow->mode = VALIDATE_FLOW_MODE_WRITING_EXPECTATIONS;
    flow->output_file_path = g_strdup (flow->expectations_file_path);
    gst_validate_printf (NULL, "Writing expectations file: %s\n",
        flow->expectations_file_path);
  }

  {
    gchar *directory_path = g_path_get_dirname (flow->output_file_path);
    if (g_mkdir_with_parents (directory_path, 0755) < 0) {
      g_error ("Could not create directory tree: %s Reason: %s",
          directory_path, g_strerror (errno));
    }
    g_free (directory_path);
  }

  flow->output_file = fopen (flow->output_file_path, "w");
  if (!flow->output_file)
    g_error ("Could not open for writing: %s", flow->output_file_path);

  gst_validate_override_register_by_name (flow->pad_name, override);

  override->buffer_handler = validate_flow_override_buffer_handler;
  override->event_handler = validate_flow_override_event_handler;

  g_signal_connect (flow, "notify::validate-runner",
      G_CALLBACK (_runner_set), NULL);

  return flow;
}

static void
_runner_set (GObject * object, GParamSpec * pspec, gpointer user_data)
{
  ValidateFlowOverride *flow = VALIDATE_FLOW_OVERRIDE (object);
  GstValidateRunner *runner =
      gst_validate_reporter_get_runner (GST_VALIDATE_REPORTER (flow));

  g_signal_connect (runner, "stopping", G_CALLBACK (runner_stopping), flow);
  gst_object_unref (runner);
}

static void
run_diff (const gchar * expected_file, const gchar * actual_file)
{
  GError *error = NULL;
  GSubprocess *process =
      g_subprocess_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE, &error, "diff", "-u",
      "--", expected_file, actual_file, NULL);
  gchar *stdout_text = NULL;

  g_subprocess_communicate_utf8 (process, NULL, NULL, &stdout_text, NULL,
      &error);
  if (!error) {
    fprintf (stderr, "%s\n", stdout_text);
  } else {
    fprintf (stderr, "Cannot show more details, failed to run diff: %s",
        error->message);
    g_error_free (error);
  }

  g_object_unref (process);
  g_free (stdout_text);
}

static const gchar *
_line_to_show (gchar ** lines, gsize i)
{
  if (lines[i] == NULL) {
    return "<nothing>";
  } else if (*lines[i] == '\0') {
    if (lines[i + 1] != NULL)
      /* skip blank lines for reporting purposes (e.g. before CHECKPOINT) */
      return lines[i + 1];
    else
      /* last blank line in the file */
      return "<nothing>";
  } else {
    return lines[i];
  }
}

static void
show_mismatch_error (ValidateFlowOverride * flow, gchar ** lines_expected,
    gchar ** lines_actual, gsize line_index)
{
  const gchar *line_expected = _line_to_show (lines_expected, line_index);
  const gchar *line_actual = _line_to_show (lines_actual, line_index);

  GST_VALIDATE_REPORT (flow, VALIDATE_FLOW_MISMATCH,
      "Mismatch error in pad %s, line %" G_GSIZE_FORMAT
      ". Expected:\n%s\nActual:\n%s\n", flow->pad_name, line_index + 1,
      line_expected, line_actual);

  run_diff (flow->expectations_file_path, flow->actual_results_file_path);
}

static void
runner_stopping (GstValidateRunner * runner, ValidateFlowOverride * flow)
{
  gchar **lines_expected, **lines_actual;
  gsize i = 0;

  fclose (flow->output_file);
  flow->output_file = NULL;
  if (flow->mode == VALIDATE_FLOW_MODE_WRITING_EXPECTATIONS)
    return;

  {
    gchar *contents;
    GError *error = NULL;
    g_file_get_contents (flow->expectations_file_path, &contents, NULL, &error);
    if (error) {
      g_error ("Failed to open expectations file: %s Reason: %s",
          flow->expectations_file_path, error->message);
    }
    lines_expected = g_strsplit (contents, "\n", 0);
  }

  {
    gchar *contents;
    GError *error = NULL;
    g_file_get_contents (flow->actual_results_file_path, &contents, NULL,
        &error);
    if (error) {
      g_error ("Failed to open actual results file: %s Reason: %s",
          flow->actual_results_file_path, error->message);
    }
    lines_actual = g_strsplit (contents, "\n", 0);
  }

  gst_validate_printf (flow, "Checking that flow %s matches expected flow %s\n",
      flow->expectations_file_path, flow->actual_results_file_path);

  for (i = 0; lines_expected[i] && lines_actual[i]; i++) {
    if (strcmp (lines_expected[i], lines_actual[i])) {
      show_mismatch_error (flow, lines_expected, lines_actual, i);
      goto stop;
    }
  }

  if (!lines_expected[i] && lines_actual[i]) {
    show_mismatch_error (flow, lines_expected, lines_actual, i);
  } else if (lines_expected[i] && !lines_actual[i]) {
    show_mismatch_error (flow, lines_expected, lines_actual, i);
  }

stop:
  g_strfreev (lines_expected);
  g_strfreev (lines_actual);
}

static void
validate_flow_override_finalize (GObject * object)
{
  ValidateFlowOverride *flow = VALIDATE_FLOW_OVERRIDE (object);

  all_overrides = g_list_remove (all_overrides, flow);
  g_free (flow->actual_results_dir);
  g_free (flow->actual_results_file_path);
  g_free (flow->expectations_dir);
  g_free (flow->expectations_file_path);
  g_free (flow->output_file_path);
  if (flow->output_file)
    fclose (flow->output_file);
  if (flow->caps_properties) {
    gchar **str_pointer;
    for (str_pointer = flow->caps_properties; *str_pointer != NULL;
        str_pointer++)
      g_free (*str_pointer);
    g_free (flow->caps_properties);
  }

  G_OBJECT_CLASS (validate_flow_override_parent_class)->finalize (object);
}

static gboolean
_execute_checkpoint (GstValidateScenario * scenario, GstValidateAction * action)
{
  GList *i;
  gchar *checkpoint_name =
      g_strdup (gst_structure_get_string (action->structure, "text"));

  for (i = all_overrides; i; i = i->next) {
    ValidateFlowOverride *flow = (ValidateFlowOverride *) i->data;

    if (checkpoint_name)
      validate_flow_override_printf (flow, "\nCHECKPOINT: %s\n\n",
          checkpoint_name);
    else
      validate_flow_override_printf (flow, "\nCHECKPOINT\n\n");
  }

  g_free (checkpoint_name);
  return TRUE;
}

static gboolean
gst_validate_flow_init (GstPlugin * plugin)
{
  GList *tmp;
  GList *config_list = gst_validate_plugin_get_config (plugin);

  if (!config_list)
    return TRUE;

  for (tmp = config_list; tmp; tmp = tmp->next) {
    GstStructure *config = tmp->data;
    ValidateFlowOverride *flow = validate_flow_override_new (config);
    all_overrides = g_list_append (all_overrides, flow);
  }

/*  *INDENT-OFF* */
  gst_validate_register_action_type_dynamic (plugin, "checkpoint",
      GST_RANK_PRIMARY, _execute_checkpoint, ((GstValidateActionParameter [])
      {
        {
          .name = "text",
          .description = "Text that will be logged in validateflow",
          .mandatory = FALSE,
          .types = "string"
        },
        {NULL}
      }),
      "Prints a line of text in validateflow logs so that it's easy to distinguish buffers and events ocurring before or after a given action.",
      GST_VALIDATE_ACTION_TYPE_NONE);
/*  *INDENT-ON* */

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    validateflow,
    "GstValidate plugin that records buffers and events on specified pads and matches the log with expectation files.",
    gst_validate_flow_init, VERSION, "LGPL", GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)
