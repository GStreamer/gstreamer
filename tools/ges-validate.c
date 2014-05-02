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

static GESTimeline *
get_timeline (GstValidateScenario * scenario)
{
  GESTimeline *timeline;

  g_object_get (scenario->pipeline, "timeline", &timeline, NULL);

  return timeline;
}

static gboolean
_set_child_property (GstValidateScenario * scenario, GstValidateAction * action)
{
  const GValue *value;
  GESTimeline *timeline;
  GESTimelineElement *element;
  const gchar *property_name, *element_name;

  element_name = gst_structure_get_string (action->structure, "element-name");

  timeline = get_timeline (scenario);
  g_return_val_if_fail (timeline, FALSE);

  element = ges_timeline_get_element (timeline, element_name);
  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (element), FALSE);

  property_name = gst_structure_get_string (action->structure, "property");
  value = gst_structure_get_value (action->structure, "value");

  GST_DEBUG ("%s Setting %s property to %p",
      element->name, property_name, value);
  ges_track_element_set_child_property (GES_TRACK_ELEMENT (element),
      property_name, (GValue *) value);

  return TRUE;
}

static gboolean
_serialize_project (GstValidateScenario * scenario, GstValidateAction * action)
{
  const gchar *uri = gst_structure_get_string (action->structure, "uri");
  GESTimeline *timeline = get_timeline (scenario);

  gst_validate_printf (action, "Saving project to %s", uri);

  return ges_timeline_save_to_uri (timeline, uri, NULL, TRUE, NULL);
}

static gboolean
_edit_clip (GstValidateScenario * scenario, GstValidateAction * action)
{
  gint64 cpos;
  gdouble rate;
  GList *layers = NULL;
  GESTimeline *timeline;
  GstQuery *query_segment;
  GESTimelineElement *clip;
  GstClockTime position;
  gint64 stop_value;

  gint new_layer_priority = -1;
  GESEditMode edge = GES_EDGE_NONE;
  GESEditMode mode = GES_EDIT_MODE_NORMAL;

  const gchar *edit_mode_str = NULL, *edge_str = NULL;
  const gchar *clip_name;

  clip_name = gst_structure_get_string (action->structure, "clip-name");

  timeline = get_timeline (scenario);
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
    g_return_val_if_fail (gst_validate_utils_enum_from_str (GES_TYPE_EDIT_MODE,
            edit_mode_str, &mode), FALSE);

  if ((edge_str = gst_structure_get_string (action->structure, "edge")))
    g_return_val_if_fail (gst_validate_utils_enum_from_str (GES_TYPE_EDGE,
            edge_str, &edge), FALSE);

  gst_structure_get_int (action->structure, "new-layer-priority",
      &new_layer_priority);

  gst_validate_printf (action, "Editing %s to %" GST_TIME_FORMAT
      " in %s mode, edge: %s "
      "with new layer prio: %d \n\n",
      clip_name, GST_TIME_ARGS (position),
      edit_mode_str ? edit_mode_str : "normal",
      edge_str ? edge_str : "None", new_layer_priority);

  if (!ges_container_edit (GES_CONTAINER (clip), layers, new_layer_priority,
          mode, edge, position)) {
    gst_object_unref (clip);
    return FALSE;
  }
  gst_object_unref (clip);

  query_segment = gst_query_new_segment (GST_FORMAT_TIME);
  if (!gst_element_query (scenario->pipeline, query_segment)) {
    GST_ERROR_OBJECT (scenario, "Could not query segment");
    return FALSE;
  }

  if (!gst_element_query_position (scenario->pipeline, GST_FORMAT_TIME, &cpos)) {
    GST_ERROR_OBJECT (scenario, "Could not query position");
    return FALSE;
  }

  if (!ges_timeline_commit (timeline)) {
    GST_DEBUG_OBJECT (scenario, "nothing changed, no need to seek");
    return TRUE;
  }


  gst_query_parse_segment (query_segment, &rate, NULL, NULL, &stop_value);

  return gst_validate_scenario_execute_seek (scenario, action,
      rate, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
      GST_SEEK_TYPE_SET, cpos, GST_SEEK_TYPE_SET, stop_value);
}

gboolean
ges_validate_activate (GstPipeline * pipeline, const gchar * scenario)
{
  GstValidateRunner *runner = NULL;
  GstValidateMonitor *monitor = NULL;

  const gchar *move_clip_mandatory_fields[] = { "clip-name", "position",
    NULL
  };

  const gchar *set_child_property_mandatory_fields[] =
      { "element-name", "property", "value", NULL };

  const gchar *serialize_project_mandatory_fields[] = { "uri",
    NULL
  };

  gst_validate_init ();

  if (scenario) {
    if (g_strcmp0 (scenario, "none")) {
      gchar *scenario_name = g_strconcat (scenario, "->gespipeline*", NULL);
      g_setenv ("GST_VALIDATE_SCENARIO", scenario_name, TRUE);
      g_free (scenario_name);
    }
  }

  gst_validate_add_action_type ("edit-clip", _edit_clip,
      move_clip_mandatory_fields, "Allows to seek into the files", FALSE);

  gst_validate_add_action_type ("serialize-project", _serialize_project,
      serialize_project_mandatory_fields, "serializes a project", FALSE);

  gst_validate_add_action_type ("set-child-property", _set_child_property,
      set_child_property_mandatory_fields,
      "Allows to change child property of an object", FALSE);

  runner = gst_validate_runner_new ();
  g_signal_connect (runner, "report-added",
      G_CALLBACK (_validate_report_added_cb), pipeline);
  monitor =
      gst_validate_monitor_factory_create (GST_OBJECT_CAST (pipeline), runner,
      NULL);

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
