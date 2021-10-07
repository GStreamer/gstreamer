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
#include "../validate.h"
#include "../gst-validate-utils.h"
#include "../gst-validate-report.h"
#include "../gst-validate-internal.h"
#include "formatting.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>

#include "gstvalidateflow.h"

#define VALIDATE_FLOW_MISMATCH g_quark_from_static_string ("validateflow::mismatch")
#define VALIDATE_FLOW_NOT_ATTACHED g_quark_from_static_string ("validateflow::not-attached")

typedef enum _ValidateFlowMode
{
  VALIDATE_FLOW_MODE_WRITING_EXPECTATIONS,
  VALIDATE_FLOW_MODE_WRITING_ACTUAL_RESULTS
} ValidateFlowMode;

#define GST_TYPE_VALIDATE_FLOW_CHECKSUM_TYPE (validate_flow_checksum_type_get_type ())
static GType
validate_flow_checksum_type_get_type (void)
{
  static GType gtype = 0;

  if (gtype == 0) {
    static const GEnumValue values[] = {
      {CHECKSUM_TYPE_NONE, "NONE", "none"},
      {CHECKSUM_TYPE_AS_ID, "AS-ID", "as-id"},
      {CHECKSUM_TYPE_CONTENT_HEX, "raw-hex", "raw-hex"},
      {G_CHECKSUM_MD5, "MD5", "md5"},
      {G_CHECKSUM_SHA1, "SHA-1", "sha1"},
      {G_CHECKSUM_SHA256, "SHA-256", "sha256"},
      {G_CHECKSUM_SHA512, "SHA-512", "sha512"},
      {0, NULL, NULL},
    };

    gtype = g_enum_register_static ("ValidateFlowChecksumType", values);
  }
  return gtype;
}

struct _ValidateFlowOverride
{
  GstValidateOverride parent;

  const gchar *pad_name;
  gboolean record_buffers;
  gint checksum_type;
  gchar *expectations_dir;
  gchar *actual_results_dir;
  gboolean error_writing_file;
  gchar **caps_properties;
  GstStructure *ignored_fields;
  GstStructure *logged_fields;

  gchar **logged_event_types;
  gchar **ignored_event_types;

  gchar *expectations_file_path;
  gchar *actual_results_file_path;
  ValidateFlowMode mode;
  gboolean was_attached;
  GstStructure *config;

  /* output_file will refer to the expectations file if it did not exist,
   * or to the actual results file otherwise. */
  gchar *output_file_path;
  FILE *output_file;
  GMutex output_file_mutex;

};

GList *all_overrides = NULL;

static void validate_flow_override_finalize (GObject * object);
static void validate_flow_override_attached (GstValidateOverride * override);
static void _runner_set (GObject * object, GParamSpec * pspec,
    gpointer user_data);
static void runner_stopping (GstValidateRunner * runner,
    ValidateFlowOverride * flow);

#define VALIDATE_TYPE_FLOW_OVERRIDE validate_flow_override_get_type ()
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
  GstValidateOverrideClass *override_class =
      GST_VALIDATE_OVERRIDE_CLASS (klass);

  object_class->finalize = validate_flow_override_finalize;
  override_class->attached = validate_flow_override_attached;

  g_assert (gst_validate_is_initialized ());

  gst_validate_issue_register (gst_validate_issue_new
      (VALIDATE_FLOW_MISMATCH,
          "The recorded log does not match the expectation file.",
          "The recorded log does not match the expectation file.",
          GST_VALIDATE_REPORT_LEVEL_CRITICAL));

  gst_validate_issue_register (gst_validate_issue_new
      (VALIDATE_FLOW_NOT_ATTACHED,
          "The pad to monitor was never attached.",
          "The pad to monitor was never attached.",
          GST_VALIDATE_REPORT_LEVEL_CRITICAL));
}

/* *INDENT-OFF* */
G_GNUC_PRINTF (2, 0)
/* *INDENT-ON* */

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

/* *INDENT-OFF* */
G_GNUC_PRINTF (2, 3)
/* *INDENT-ON* */

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

  event_string = validate_flow_format_event (event,
      (const gchar * const *) flow->caps_properties,
      flow->logged_fields,
      flow->ignored_fields,
      (const gchar * const *) flow->ignored_event_types,
      (const gchar * const *) flow->logged_event_types);

  if (event_string) {
    validate_flow_override_printf (flow, "event %s\n", event_string);
    g_free (event_string);
  }
}

static void
validate_flow_override_buffer_handler (GstValidateOverride * override,
    GstValidateMonitor * pad_monitor, GstBuffer * buffer)
{
  ValidateFlowOverride *flow = VALIDATE_FLOW_OVERRIDE (override);
  gchar *buffer_str;

  if (flow->error_writing_file || !flow->record_buffers)
    return;

  buffer_str = validate_flow_format_buffer (buffer, flow->checksum_type,
      flow->logged_fields, flow->ignored_fields);
  validate_flow_override_printf (flow, "buffer: %s\n", buffer_str);
  g_free (buffer_str);
}

static gchar *
make_safe_file_name (const gchar * name)
{
  gchar *ret = g_strdup (name);
  gchar *c;
  for (c = ret; *c; c++) {
    switch (*c) {
      case '<':
      case '>':
      case ':':
      case '"':
      case '/':
      case '\\':
      case '|':
      case '?':
      case '*':
        *c = '-';
        break;
    }
  }
  return ret;
}

static ValidateFlowOverride *
validate_flow_override_new (GstStructure * config)
{
  ValidateFlowOverride *flow;
  GstValidateOverride *override;
  gboolean use_checksum = FALSE;
  gchar *ignored_fields = NULL, *logged_fields;
  const GValue *tmpval;

  flow = g_object_new (VALIDATE_TYPE_FLOW_OVERRIDE, NULL);
  flow->config = config;

  GST_OBJECT_FLAG_SET (flow, GST_OBJECT_FLAG_MAY_BE_LEAKED);
  override = GST_VALIDATE_OVERRIDE (flow);

  /* pad: Name of the pad where flowing buffers and events will be monitorized. */
  flow->pad_name = gst_structure_get_string (config, "pad");
  if (!flow->pad_name) {
    gst_validate_error_structure (config,
        "pad property is mandatory, not found in %" GST_PTR_FORMAT, config);
  }

  /* record-buffers: Whether buffers will be written to the expectation log. */
  flow->record_buffers = FALSE;
  gst_structure_get_boolean (config, "record-buffers", &flow->record_buffers);

  flow->checksum_type = CHECKSUM_TYPE_NONE;
  gst_structure_get_boolean (config, "buffers-checksum", &use_checksum);

  if (use_checksum) {
    flow->checksum_type = G_CHECKSUM_SHA1;
  } else {
    const gchar *checksum_type =
        gst_structure_get_string (config, "buffers-checksum");

    if (checksum_type) {
      if (!gst_validate_utils_enum_from_str
          (GST_TYPE_VALIDATE_FLOW_CHECKSUM_TYPE, checksum_type,
              (guint *) & flow->checksum_type))
        gst_validate_error_structure (config,
            "Invalid value for buffers-checksum: %s", checksum_type);
    }
  }

  if (flow->checksum_type != CHECKSUM_TYPE_NONE)
    flow->record_buffers = TRUE;

  /* caps-properties: Caps events can include many dfferent properties, but
   * many of these may be irrelevant for some tests. If this option is set,
   * only the listed properties will be written to the expectation log. */
  flow->caps_properties =
      gst_validate_utils_get_strv (config, "caps-properties");

  flow->logged_event_types =
      gst_validate_utils_get_strv (config, "logged-event-types");
  flow->ignored_event_types =
      gst_validate_utils_get_strv (config, "ignored-event-types");

  tmpval = gst_structure_get_value (config, "ignored-fields");
  if (tmpval) {
    if (!G_VALUE_HOLDS_STRING (tmpval)) {
      gst_validate_error_structure (config,
          "Invalid value type for `ignored-fields`: '%s' instead of 'string'",
          G_VALUE_TYPE_NAME (tmpval));
    }
    ignored_fields = (gchar *) g_value_get_string (tmpval);
  }

  if (ignored_fields) {
    ignored_fields = g_strdup_printf ("ignored,%s", ignored_fields);
    flow->ignored_fields = gst_structure_new_from_string (ignored_fields);
    if (!flow->ignored_fields)
      gst_validate_error_structure (config,
          "Could not parse 'ignored-event-fields' structure: `%s`",
          ignored_fields);
    g_free (ignored_fields);
  } else {
    flow->ignored_fields =
        gst_structure_new_from_string ("ignored,stream-start={stream-id}");
  }

  if (!gst_structure_has_field (flow->ignored_fields, "stream-start"))
    gst_structure_set (flow->ignored_fields, "stream-start",
        G_TYPE_STRING, "stream-id", NULL);

  logged_fields = (gchar *) gst_structure_get_string (config, "logged-fields");
  if (logged_fields) {
    logged_fields = g_strdup_printf ("logged,%s", logged_fields);
    flow->logged_fields = gst_structure_new_from_string (logged_fields);
    if (!flow->logged_fields)
      gst_validate_error_structure (config,
          "Could not parse 'logged-fields' %s", logged_fields);
    g_free (logged_fields);
  } else {
    flow->logged_fields = NULL;
  }

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
    gchar *pad_name_safe = make_safe_file_name (flow->pad_name);
    gchar *expectations_file_name =
        g_strdup_printf ("log-%s-expected", pad_name_safe);
    gchar *actual_results_file_name =
        g_strdup_printf ("log-%s-actual", pad_name_safe);
    flow->expectations_file_path =
        g_build_path (G_DIR_SEPARATOR_S, flow->expectations_dir,
        expectations_file_name, NULL);
    flow->actual_results_file_path =
        g_build_path (G_DIR_SEPARATOR_S, flow->actual_results_dir,
        actual_results_file_name, NULL);
    g_free (expectations_file_name);
    g_free (actual_results_file_name);
    g_free (pad_name_safe);
  }

  flow->was_attached = FALSE;

  gst_validate_override_register_by_name (flow->pad_name, override);

  override->buffer_handler = validate_flow_override_buffer_handler;
  override->buffer_probe_handler = validate_flow_override_buffer_handler;
  override->event_handler = validate_flow_override_event_handler;

  g_signal_connect (flow, "notify::validate-runner",
      G_CALLBACK (_runner_set), NULL);

  return flow;
}

static void
validate_flow_setup_files (ValidateFlowOverride * flow, gint default_generate)
{
  gint local_generate_expectations = -1;
  gboolean generate_if_doesn_exit = default_generate == -1;
  gboolean exists =
      g_file_test (flow->expectations_file_path, G_FILE_TEST_EXISTS);

  if (generate_if_doesn_exit) {
    gst_structure_get_boolean (flow->config, "generate-expectations",
        &local_generate_expectations);
    generate_if_doesn_exit = local_generate_expectations == -1;
  }

  if ((!default_generate || !local_generate_expectations) && !exists) {
    gst_validate_error_structure (flow->config, "Not writing expectations and"
        " configured expectation file %s doesn't exist in config:\n       > %"
        GST_PTR_FORMAT, flow->expectations_file_path, flow->config);
  }

  if (exists && local_generate_expectations != 1 && default_generate != 1) {
    flow->mode = VALIDATE_FLOW_MODE_WRITING_ACTUAL_RESULTS;
    flow->output_file_path = g_strdup (flow->actual_results_file_path);
    gst_validate_printf (NULL, "**-> Checking expectations file: '%s'**\n",
        flow->expectations_file_path);
  } else {
    flow->mode = VALIDATE_FLOW_MODE_WRITING_EXPECTATIONS;
    flow->output_file_path = g_strdup (flow->expectations_file_path);
    gst_validate_printf (NULL, "**-> Writing expectations file: '%s'**\n",
        flow->expectations_file_path);
  }

  {
    gchar *directory_path = g_path_get_dirname (flow->output_file_path);
    if (g_mkdir_with_parents (directory_path, 0755) < 0) {
      gst_validate_abort ("Could not create directory tree: %s Reason: %s",
          directory_path, g_strerror (errno));
    }
    g_free (directory_path);
  }

  flow->output_file = fopen (flow->output_file_path, "wb");
  if (!flow->output_file)
    gst_validate_abort ("Could not open for writing: %s",
        flow->output_file_path);

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
validate_flow_override_attached (GstValidateOverride * override)
{
  ValidateFlowOverride *flow = VALIDATE_FLOW_OVERRIDE (override);
  flow->was_attached = TRUE;
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
    gboolean colored = gst_validate_has_colored_output ();
    GSubprocess *process2;
    gchar *fname = NULL;
    gint f = g_file_open_tmp ("XXXXXX.diff", &fname, NULL);

    if (f > 0) {
      gchar *tmpstdout;
      g_file_set_contents (fname, stdout_text, -1, NULL);
      close (f);

      process2 =
          g_subprocess_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE, &error, "bat", "-l",
          "diff", "--paging", "never", "--color", colored ? "always" : "never",
          fname, NULL);

      g_subprocess_communicate_utf8 (process2, NULL, NULL, &tmpstdout, NULL,
          &error);
      if (!error) {
        g_free (stdout_text);
        stdout_text = tmpstdout;
      } else {
        colored = FALSE;
        GST_DEBUG ("Could not use bat: %s", error->message);
        g_clear_error (&error);
      }
      g_clear_object (&process2);
      g_free (fname);
    }

    fprintf (stderr, "%s%s%s\n",
        !colored ? "``` diff\n" : "", stdout_text, !colored ? "\n```" : "");
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

  if (!flow->was_attached) {
    GST_VALIDATE_REPORT (flow, VALIDATE_FLOW_NOT_ATTACHED,
        "The test ended without the pad ever being attached: %s",
        flow->pad_name);
    return;
  }

  if (flow->mode == VALIDATE_FLOW_MODE_WRITING_EXPECTATIONS) {
    gst_validate_skip_test ("wrote expectation files for %s.\n",
        flow->pad_name);

    return;
  }

  {
    gchar *contents;
    GError *error = NULL;
    g_file_get_contents (flow->expectations_file_path, &contents, NULL, &error);
    if (error) {
      gst_validate_abort ("Failed to open expectations file: %s Reason: %s",
          flow->expectations_file_path, error->message);
    }
    lines_expected = g_strsplit (contents, "\n", 0);
    g_free (contents);
  }

  {
    gchar *contents;
    GError *error = NULL;
    g_file_get_contents (flow->actual_results_file_path, &contents, NULL,
        &error);
    if (error) {
      gst_validate_abort ("Failed to open actual results file: %s Reason: %s",
          flow->actual_results_file_path, error->message);
    }
    lines_actual = g_strsplit (contents, "\n", 0);
    g_free (contents);
  }

  gst_validate_printf (flow, "Checking that flow %s matches expected flow %s\n",
      flow->expectations_file_path, flow->actual_results_file_path);

  for (i = 0; lines_expected[i] && lines_actual[i]; i++) {
    if (g_strcmp0 (lines_expected[i], lines_actual[i])) {
      show_mismatch_error (flow, lines_expected, lines_actual, i);
      goto stop;
    }
  }
  gst_validate_printf (flow, "OK\n");
  if (!lines_expected[i] && lines_actual[i]) {
    show_mismatch_error (flow, lines_expected, lines_actual, i);
    goto stop;
  } else if (lines_expected[i] && !lines_actual[i]) {
    show_mismatch_error (flow, lines_expected, lines_actual, i);
    goto stop;
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
  g_strfreev (flow->caps_properties);
  g_strfreev (flow->logged_event_types);
  g_strfreev (flow->ignored_event_types);
  if (flow->ignored_fields)
    gst_structure_free (flow->ignored_fields);

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

gboolean
gst_validate_flow_init ()
{
  GList *tmp;
  gint default_generate = -1;
  GList *config_list = gst_validate_get_config ("validateflow");

  if (!config_list)
    return TRUE;

  for (tmp = config_list; tmp; tmp = tmp->next) {
    GstStructure *config = tmp->data;
    ValidateFlowOverride *flow;

    if (gst_structure_has_field (config, "generate-expectations") &&
        !gst_structure_has_field (config, "pad")) {
      if (!gst_structure_get_boolean (config, "generate-expectations",
              &default_generate)) {
        gst_validate_error_structure (config,
            "Field 'generate-expectations' should be a boolean");
      }

      continue;
    }

    flow = validate_flow_override_new (config);
    all_overrides = g_list_append (all_overrides, flow);
  }
  g_list_free (config_list);

  for (tmp = all_overrides; tmp; tmp = tmp->next)
    validate_flow_setup_files (tmp->data, default_generate);

/*  *INDENT-OFF* */
  gst_validate_register_action_type ("checkpoint", "validateflow",
      _execute_checkpoint, ((GstValidateActionParameter [])
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
