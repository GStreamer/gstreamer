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

static gboolean
_edit_clip (GstValidateScenario * scenario, GstValidateAction * action)
{
  GList *layers = NULL;
  GstClockTime position;
  GESTimeline *timeline;
  GESTimelineElement *clip;

  gint new_layer_priority = -1;
  GESEditMode edge = GES_EDGE_NONE;
  GESEditMode mode = GES_EDIT_MODE_NORMAL;

  const gchar *edit_mode_str = NULL, *edge_str = NULL;
  const gchar *clip_name;

  clip_name = gst_structure_get_string (action->structure, "clip-name");

  g_object_get (scenario->pipeline, "timeline", &timeline, NULL);
  g_return_val_if_fail (timeline, FALSE);

  clip = ges_timeline_get_element (timeline, clip_name);
  g_return_val_if_fail (GES_IS_CLIP (clip), FALSE);

  if (!gst_validate_action_get_clocktime (scenario, action,
          "position", &position)) {
    GST_WARNING ("Could not get position");
    return FALSE;
  }

  if ((edit_mode_str =
          gst_structure_get_string (action->structure, "edit-mode")))
    gst_validate_utils_enum_from_str (GES_TYPE_EDIT_MODE, edit_mode_str, &mode);

  if ((edge_str = gst_structure_get_string (action->structure, "edge")))
    gst_validate_utils_enum_from_str (GES_TYPE_EDGE, edge_str, &edge);

  gst_structure_get_int (action->structure, "new-layer-priority",
      &new_layer_priority);

  g_print ("\n\n(position %" GST_TIME_FORMAT
      "), %s (num %u, missing repeat: %i), "
      "Editing %s to %" GST_TIME_FORMAT " in %s mode, edge: %s "
      "with new layer prio: %d \n\n",
      GST_TIME_ARGS (action->playback_time), action->name,
      action->action_number, action->repeat,
      clip_name, GST_TIME_ARGS (position),
      edit_mode_str ? edit_mode_str : "normal",
      edge_str ? edge_str : "None", new_layer_priority);

  if (!ges_container_edit (GES_CONTAINER (clip), layers, new_layer_priority,
          mode, edge, position)) {
    gst_object_unref (clip);
    return FALSE;
  }
  gst_object_unref (clip);

  return ges_timeline_commit (timeline);
}

gboolean
ges_validate_activate (GstPipeline * pipeline, const gchar * scenario)
{
  GstValidateRunner *runner = NULL;
  GstValidateMonitor *monitor = NULL;

  if (scenario) {
    const gchar *move_clip_mandatory_fields[] = { "clip-name", "position",
      NULL
    };

    gst_validate_init ();

    if (g_strcmp0 (scenario, "none")) {
      gchar *scenario_name = g_strconcat (scenario, "->gespipeline*", NULL);
      g_setenv ("GST_VALIDATE_SCENARIO", scenario_name, TRUE);
      g_free (scenario_name);
    }

    gst_validate_add_action_type ("edit-clip", _edit_clip,
        move_clip_mandatory_fields, "Allows to seek into the files", FALSE);

    runner = gst_validate_runner_new ();
    g_signal_connect (runner, "report-added",
        G_CALLBACK (_validate_report_added_cb), pipeline);
    monitor =
        gst_validate_monitor_factory_create (GST_OBJECT_CAST (pipeline), runner,
        NULL);
  }

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
    res = gst_validate_runner_printf (runner);

  gst_object_unref (pipeline);
  if (runner) {
    gst_object_unref (runner);
    if (monitor)
      gst_object_unref (monitor);
  }

  return res;
}

#else
static gboolean
_print_position (GstElement * pipeline)
{
  gint64 position, duration;

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
ges_validate_activate (GstPipeline * pipeline, const gchar * scenario)
{
  if (scenario) {
    GST_WARNING ("Trying to run scenario %s, but gst-validate not supported",
        scenario);

    return FALSE;
  }

  g_object_set_data (G_OBJECT (pipeline), "pposition-id",
      GUINT_TO_POINTER (g_timeout_add (200,
              (GSourceFunc) _print_position, pipeline)));

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

#endif
