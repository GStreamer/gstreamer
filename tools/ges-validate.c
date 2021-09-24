/* GStreamer Editing Services
 *
 * Copyright (C) <2013> Thibault Saunier <thibault.saunier@collabora.com>
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
#include "config.h"
#endif

#include "utils.h"
#include "ges-validate.h"

#include <string.h>

static gboolean
_print_position (GstElement * pipeline)
{
  gint64 position = 0, duration = -1;

  if (pipeline) {
    gst_element_query_position (GST_ELEMENT (pipeline), GST_FORMAT_TIME,
        &position);
    gst_element_query_duration (GST_ELEMENT (pipeline), GST_FORMAT_TIME,
        &duration);

    gst_print ("<position: %" GST_TIME_FORMAT " duration: %" GST_TIME_FORMAT
        "/>\r", GST_TIME_ARGS (position), GST_TIME_ARGS (duration));
  }

  return TRUE;
}

#ifdef HAVE_GST_VALIDATE
#include <gst/validate/gst-validate-scenario.h>
#include <gst/validate/validate.h>
#include <gst/validate/gst-validate-utils.h>
#include <gst/validate/gst-validate-element-monitor.h>
#include <gst/validate/gst-validate-bin-monitor.h>

#define MONITOR_ON_PIPELINE "validate-monitor"
#define RUNNER_ON_PIPELINE "runner-monitor"
#define WRONG_DECODER_ADDED g_quark_from_static_string ("ges::wrong-decoder-added")

static void
_validate_report_added_cb (GstValidateRunner * runner,
    GstValidateReport * report, GstPipeline * pipeline)
{
  if (report->level == GST_VALIDATE_REPORT_LEVEL_CRITICAL) {
    GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
        GST_DEBUG_GRAPH_SHOW_ALL, "ges-launch--validate-error");
  }
}

static void
bin_element_added (GstTracer * runner, GstClockTime ts,
    GstBin * bin, GstElement * element, gboolean result)
{
  GstObject *parent;
  GstValidateElementMonitor *monitor =
      g_object_get_data (G_OBJECT (element), "validate-monitor");

  if (!monitor)
    return;

  if (!monitor->is_decoder)
    return;

  parent = gst_object_get_parent (GST_OBJECT (element));
  do {
    if (GES_IS_TRACK (parent)) {
      GstElementClass *klass = GST_ELEMENT_CLASS (G_OBJECT_GET_CLASS (element));
      const gchar *klassname =
          gst_element_class_get_metadata (klass, GST_ELEMENT_METADATA_KLASS);

      if (GES_IS_AUDIO_TRACK (parent) && strstr (klassname, "Audio") == NULL) {
        GST_VALIDATE_REPORT (monitor, WRONG_DECODER_ADDED,
            "Adding non audio decoder %s in audio track %s.",
            GST_OBJECT_NAME (element), GST_OBJECT_NAME (parent));
      } else if (GES_IS_VIDEO_TRACK (parent)
          && strstr (klassname, "Video") == NULL
          && strstr (klassname, "Image") == NULL) {
        GST_VALIDATE_REPORT (monitor, WRONG_DECODER_ADDED,
            "Adding non video decoder %s in video track %s.",
            GST_OBJECT_NAME (element), GST_OBJECT_NAME (parent));

      }
      gst_object_unref (parent);
      break;
    }

    gst_object_unref (parent);
    parent = gst_object_get_parent (parent);
  } while (parent);
}

static void
ges_validate_register_issues (void)
{
  gst_validate_issue_register (gst_validate_issue_new (WRONG_DECODER_ADDED,
          "Wrong decoder type added to track.",
          "In a specific track type we should never create decoders"
          " for some other types (No audio decoder should be added"
          " in a Video track).", GST_VALIDATE_REPORT_LEVEL_CRITICAL));
}

gboolean
ges_validate_activate (GstPipeline * pipeline, GESLauncher * launcher,
    GESLauncherParsedOptions * opts)
{
  GstValidateRunner *runner = NULL;
  GstValidateMonitor *monitor = NULL;

  if (!opts->enable_validate) {
    opts->needs_set_state = TRUE;
    g_object_set_data (G_OBJECT (pipeline), "pposition-id",
        GUINT_TO_POINTER (g_timeout_add (200,
                (GSourceFunc) _print_position, pipeline)));
    return TRUE;
  }

  gst_validate_init_debug ();

  if (opts->testfile) {
    if (opts->scenario)
      g_error ("Can not specify scenario and testfile at the same time");
    gst_validate_setup_test_file (opts->testfile, opts->mute);
  } else if (opts->scenario) {
    if (g_strcmp0 (opts->scenario, "none")) {
      gchar *scenario_name =
          g_strconcat (opts->scenario, "->gespipeline*", NULL);
      g_setenv ("GST_VALIDATE_SCENARIO", scenario_name, TRUE);
      g_free (scenario_name);
    }
  }

  ges_validate_register_action_types ();
  ges_validate_register_issues ();

  runner = gst_validate_runner_new ();
  gst_tracing_register_hook (GST_TRACER (runner), "bin-add-post",
      G_CALLBACK (bin_element_added));
  g_signal_connect (runner, "report-added",
      G_CALLBACK (_validate_report_added_cb), pipeline);
  monitor =
      gst_validate_monitor_factory_create (GST_OBJECT_CAST (pipeline), runner,
      NULL);
  if (GST_VALIDATE_BIN_MONITOR (monitor)->scenario) {
    GstStructure *metas =
        GST_VALIDATE_BIN_MONITOR (monitor)->scenario->description;

    if (metas) {
      gchar **ges_options = gst_validate_utils_get_strv (metas, "ges-options");
      if (!ges_options)
        ges_options = gst_validate_utils_get_strv (metas, "args");

      gst_structure_get_boolean (metas, "ignore-eos", &opts->ignore_eos);
      if (ges_options) {
        gint i;
        gchar **ges_options_full =
            g_new0 (gchar *, g_strv_length (ges_options) + 2);

        ges_options_full[0] = g_strdup ("something");
        for (i = 0; ges_options[i]; i++)
          ges_options_full[i + 1] = g_strdup (ges_options[i]);

        ges_launcher_parse_options (launcher, &ges_options_full, NULL, NULL,
            NULL);
        opts->sanitized_timeline =
            sanitize_timeline_description (ges_options_full, opts);
        g_strfreev (ges_options_full);
        g_strfreev (ges_options);
      }
    }
  }

  gst_validate_reporter_set_handle_g_logs (GST_VALIDATE_REPORTER (monitor));

  g_object_get (monitor, "handles-states", &opts->needs_set_state, NULL);
  opts->needs_set_state = !opts->needs_set_state;
  g_object_set_data (G_OBJECT (pipeline), MONITOR_ON_PIPELINE, monitor);
  g_object_set_data (G_OBJECT (pipeline), RUNNER_ON_PIPELINE, runner);

  return TRUE;
}

gint
ges_validate_clean (GstPipeline * pipeline)
{
  gint res = 0;
  GstValidateMonitor *monitor =
      g_object_get_data (G_OBJECT (pipeline), MONITOR_ON_PIPELINE);
  GstValidateRunner *runner =
      g_object_get_data (G_OBJECT (pipeline), RUNNER_ON_PIPELINE);

  if (runner)
    res = gst_validate_runner_exit (runner, TRUE);
  else
    g_source_remove (GPOINTER_TO_INT (g_object_get_data (G_OBJECT (pipeline),
                "pposition-id")));

  gst_object_unref (pipeline);
  if (runner)
    gst_object_unref (runner);
  if (monitor)
    gst_object_unref (monitor);

  return res;
}

void
ges_validate_handle_request_state_change (GstMessage * message,
    GApplication * application)
{
  GstState state;

  gst_message_parse_request_state (message, &state);

  if (GST_IS_VALIDATE_SCENARIO (GST_MESSAGE_SRC (message))
      && state == GST_STATE_NULL) {
    gst_validate_printf (GST_MESSAGE_SRC (message),
        "State change request NULL, " "quitting application\n");
    g_application_quit (application);
  }
}

gint
ges_validate_print_action_types (const gchar ** types, gint num_types)
{
  ges_validate_register_action_types ();

  if (!gst_validate_print_action_types (types, num_types)) {
    GST_ERROR ("Could not print all wanted types");
    return 1;
  }

  return 0;
}

#else
gboolean
ges_validate_activate (GstPipeline * pipeline, GESLauncher * launcher,
    GESLauncherParsedOptions * opts)
{
  if (opts->testfile) {
    GST_WARNING ("Trying to run testfile %s, but gst-validate not supported",
        opts->testfile);

    return FALSE;
  }

  if (opts->scenario) {
    GST_WARNING ("Trying to run scenario %s, but gst-validate not supported",
        opts->scenario);

    return FALSE;
  }

  g_object_set_data (G_OBJECT (pipeline), "pposition-id",
      GUINT_TO_POINTER (g_timeout_add (200,
              (GSourceFunc) _print_position, pipeline)));

  opts->needs_set_state = TRUE;

  return TRUE;
}

gint
ges_validate_clean (GstPipeline * pipeline)
{
  g_source_remove (GPOINTER_TO_INT (g_object_get_data (G_OBJECT (pipeline),
              "pposition-id")));

  gst_object_unref (pipeline);

  return 0;
}

void
ges_validate_handle_request_state_change (GstMessage * message,
    GApplication * application)
{
  return;
}

gint
ges_validate_print_action_types (const gchar ** types, gint num_types)
{
  return 0;
}

#endif
