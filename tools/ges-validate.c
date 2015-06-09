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

#include "ges-validate.h"
#include <ges/ges.h>

#ifdef HAVE_GST_VALIDATE
#include <gst/validate/gst-validate-scenario.h>
#include <gst/validate/validate.h>
#include <gst/validate/gst-validate-utils.h>

#define MONITOR_ON_PIPELINE "validate-monitor"
#define RUNNER_ON_PIPELINE "runner-monitor"

static void
_validate_report_added_cb (GstValidateRunner * runner,
    GstValidateReport * report, GstPipeline * pipeline)
{
  if (report->level == GST_VALIDATE_REPORT_LEVEL_CRITICAL) {
    GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
        GST_DEBUG_GRAPH_SHOW_ALL, "ges-launch--validate-error");
  }
}

gboolean
ges_validate_activate (GstPipeline * pipeline, const gchar * scenario,
    gboolean * needs_setting_state)
{
  GstValidateRunner *runner = NULL;
  GstValidateMonitor *monitor = NULL;

  ges_validate_register_action_types ();

  if (scenario) {
    if (g_strcmp0 (scenario, "none")) {
      gchar *scenario_name = g_strconcat (scenario, "->gespipeline*", NULL);
      g_setenv ("GST_VALIDATE_SCENARIO", scenario_name, TRUE);
      g_free (scenario_name);
    }
  }

  runner = gst_validate_runner_new ();
  g_signal_connect (runner, "report-added",
      G_CALLBACK (_validate_report_added_cb), pipeline);
  monitor =
      gst_validate_monitor_factory_create (GST_OBJECT_CAST (pipeline), runner,
      NULL);

  gst_validate_reporter_set_handle_g_logs (GST_VALIDATE_REPORTER (monitor));

  g_object_get (monitor, "handles-states", needs_setting_state, NULL);
  *needs_setting_state = !*needs_setting_state;
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

  gst_object_unref (pipeline);
  if (runner) {
    gst_object_unref (runner);
    if (monitor)
      gst_object_unref (monitor);
  }

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
        "State change request NULL, " "quiting application\n");
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
static gboolean
_print_position (GstElement * pipeline)
{
  gint64 position = 0, duration = -1;

  if (pipeline) {
    gst_element_query_position (GST_ELEMENT (pipeline), GST_FORMAT_TIME,
        &position);
    gst_element_query_duration (GST_ELEMENT (pipeline), GST_FORMAT_TIME,
        &duration);

    g_print ("<position: %" GST_TIME_FORMAT " duration: %" GST_TIME_FORMAT
        "/>\r", GST_TIME_ARGS (position), GST_TIME_ARGS (duration));
  }

  return TRUE;
}

gboolean
ges_validate_activate (GstPipeline * pipeline, const gchar * scenario,
    gboolean * needs_setting_state)
{
  if (scenario) {
    GST_WARNING ("Trying to run scenario %s, but gst-validate not supported",
        scenario);

    return FALSE;
  }

  g_object_set_data (G_OBJECT (pipeline), "pposition-id",
      GUINT_TO_POINTER (g_timeout_add (200,
              (GSourceFunc) _print_position, pipeline)));

  *needs_setting_state = TRUE;

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
