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

  g_object_unref (timeline);

  return TRUE;
}

static gboolean
_serialize_project (GstValidateScenario * scenario, GstValidateAction * action)
{
  const gchar *uri = gst_structure_get_string (action->structure, "uri");
  GESTimeline *timeline = get_timeline (scenario);
  gboolean res;

  gst_validate_printf (action, "Saving project to %s", uri);

  res = ges_timeline_save_to_uri (timeline, uri, NULL, TRUE, NULL);

  g_object_unref (timeline);
  return res;
}

static gboolean
_remove_asset (GstValidateScenario * scenario, GstValidateAction * action)
{
  const gchar *id = NULL;
  const gchar *type_string = NULL;
  GType type;
  GESTimeline *timeline = get_timeline (scenario);
  GESProject *project = ges_timeline_get_project (timeline);
  GESAsset *asset;
  gboolean res = FALSE;

  id = gst_structure_get_string (action->structure, "id");
  type_string = gst_structure_get_string (action->structure, "type");

  if (!type_string || !id) {
    GST_ERROR ("Missing parameters, we got type %s and id %s", type_string, id);
    goto beach;
  }

  if (!(type = g_type_from_name (type_string))) {
    GST_ERROR ("This type doesn't exist : %s", type_string);
    goto beach;
  }

  asset = ges_project_get_asset (project, id, type);

  if (!asset) {
    GST_ERROR ("No asset with id %s and type %s", id, type_string);
    goto beach;
  }

  res = ges_project_remove_asset (project, asset);

beach:
  g_object_unref (timeline);
  return res;
}

static gboolean
_add_asset (GstValidateScenario * scenario, GstValidateAction * action)
{
  const gchar *id = NULL;
  const gchar *type_string = NULL;
  GType type;
  GESTimeline *timeline = get_timeline (scenario);
  GESProject *project = ges_timeline_get_project (timeline);
  GESAsset *asset;
  GError *error = NULL;
  gboolean res = FALSE;

  id = gst_structure_get_string (action->structure, "id");
  type_string = gst_structure_get_string (action->structure, "type");

  if (!type_string || !id) {
    GST_ERROR ("Missing parameters, we got type %s and id %s", type_string, id);
    goto beach;
  }

  if (!(type = g_type_from_name (type_string))) {
    GST_ERROR ("This type doesn't exist : %s", type_string);
    goto beach;
  }

  if (type == GES_TYPE_URI_CLIP)
    asset = (GESAsset *) ges_uri_clip_asset_request_sync (id, &error);
  else
    asset = ges_asset_request (type, id, &error);

  if (!asset || error) {
    GST_ERROR ("There was an error requesting the asset with id %s and type %s",
        id, type_string);
    goto beach;
  }

  res = ges_project_add_asset (project, asset);

beach:
  g_object_unref (timeline);
  return res;
}

/* Unref after usage */
static GESLayer *
_get_layer_by_priority (GESTimeline * timeline, gint priority)
{
  GList *layers, *tmp;
  GESLayer *layer = NULL;

  layers = ges_timeline_get_layers (timeline);
  for (tmp = layers; tmp; tmp = tmp->next) {
    GESLayer *tmp_layer = GES_LAYER (tmp->data);
    guint tmp_priority;
    g_object_get (tmp_layer, "priority", &tmp_priority, NULL);
    if ((gint) tmp_priority == priority) {
      layer = gst_object_ref (tmp_layer);
      break;
    }
  }

  g_list_free_full (layers, gst_object_unref);
  return layer;
}

static gboolean
_add_layer (GstValidateScenario * scenario, GstValidateAction * action)
{
  GESTimeline *timeline = get_timeline (scenario);
  GESLayer *layer;
  gint priority;
  gboolean res = FALSE;

  if (!gst_structure_get_int (action->structure, "priority", &priority)) {
    GST_ERROR ("priority is needed when adding a layer");
    goto beach;
  }

  layer = _get_layer_by_priority (timeline, priority);

  if (layer != NULL) {
    GST_ERROR
        ("A layer with priority %d already exists, not creating a new one",
        priority);
    gst_object_unref (layer);
    goto beach;
  }

  layer = ges_layer_new ();
  g_object_set (layer, "priority", priority, NULL);
  res = ges_timeline_add_layer (timeline, layer);

beach:
  g_object_unref (timeline);
  return res;
}

static gboolean
_remove_layer (GstValidateScenario * scenario, GstValidateAction * action)
{
  GESTimeline *timeline = get_timeline (scenario);
  GESLayer *layer;
  gint priority;
  gboolean res = FALSE;

  if (!gst_structure_get_int (action->structure, "priority", &priority)) {
    GST_ERROR ("priority is needed when removing a layer");
    goto beach;
  }

  layer = _get_layer_by_priority (timeline, priority);

  if (layer) {
    res = ges_timeline_remove_layer (timeline, layer);
    gst_object_unref (layer);
  } else {
    GST_ERROR ("No layer with priority %d", priority);
  }

beach:
  g_object_unref (timeline);
  return res;
}

static gboolean
_remove_clip (GstValidateScenario * scenario, GstValidateAction * action)
{
  GESTimeline *timeline = get_timeline (scenario);
  GESTimelineElement *clip;
  GESLayer *layer;
  const gchar *name;
  gboolean res = FALSE;

  name = gst_structure_get_string (action->structure, "name");
  clip = ges_timeline_get_element (timeline, name);
  g_return_val_if_fail (GES_IS_CLIP (clip), FALSE);

  layer = ges_clip_get_layer (GES_CLIP (clip));

  if (layer) {
    res = ges_layer_remove_clip (layer, GES_CLIP (clip));
    gst_object_unref (layer);
  } else {
    GST_ERROR ("No layer for clip %s", ges_timeline_element_get_name (clip));
  }

  g_object_unref (timeline);
  return res;
}

static gboolean
_add_clip (GstValidateScenario * scenario, GstValidateAction * action)
{
  GESTimeline *timeline = get_timeline (scenario);
  GESAsset *asset;
  GESLayer *layer;
  GESClip *clip;
  GError *error = NULL;
  gint layer_priority;
  const gchar *name;
  const gchar *asset_id;
  const gchar *type_string;
  GType type;
  gboolean res = FALSE;
  GstClockTime duration = 1 * GST_SECOND;

  gst_structure_get_int (action->structure, "layer-priority", &layer_priority);
  name = gst_structure_get_string (action->structure, "name");
  asset_id = gst_structure_get_string (action->structure, "asset-id");
  type_string = gst_structure_get_string (action->structure, "type");

  if (!(type = g_type_from_name (type_string))) {
    GST_ERROR ("This type doesn't exist : %s", type_string);
    goto beach;
  }

  asset = ges_asset_request (type, asset_id, &error);

  if (!asset || error) {
    GST_ERROR
        ("There was an error requesting the asset with id %s and type %s (%s)",
        asset_id, type_string, error->message);
    goto beach;
  }

  layer = _get_layer_by_priority (timeline, layer_priority);

  if (!layer) {
    GST_ERROR ("No layer with priority %d", layer_priority);
    goto beach;
  }

  if (type == GES_TYPE_URI_CLIP) {
    duration = GST_CLOCK_TIME_NONE;
  }

  clip = ges_layer_add_asset (layer, asset, GST_CLOCK_TIME_NONE, 0, duration,
      GES_TRACK_TYPE_UNKNOWN);

  if (clip) {
    res = TRUE;
    if (!ges_timeline_element_set_name (GES_TIMELINE_ELEMENT (clip), name)) {
      res = FALSE;
      GST_ERROR ("couldn't set name %s on clip with id %s", name, asset_id);
    }
  } else {
    GST_ERROR ("Couldn't add clip with id %s to layer with priority %d",
        asset_id, layer_priority);
  }

  gst_object_unref (layer);

  ges_timeline_commit (timeline);

beach:
  g_object_unref (timeline);
  return res;
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
  gboolean res = FALSE;

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
    goto beach;
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
    goto beach;
  }
  gst_object_unref (clip);

  query_segment = gst_query_new_segment (GST_FORMAT_TIME);
  if (!gst_element_query (scenario->pipeline, query_segment)) {
    GST_ERROR_OBJECT (scenario, "Could not query segment");
    goto beach;
  }

  if (!gst_element_query_position (scenario->pipeline, GST_FORMAT_TIME, &cpos)) {
    GST_ERROR_OBJECT (scenario, "Could not query position");
    goto beach;
  }

  if (!ges_timeline_commit (timeline)) {
    GST_DEBUG_OBJECT (scenario, "nothing changed, no need to seek");
    res = TRUE;
    goto beach;
  }


  gst_query_parse_segment (query_segment, &rate, NULL, NULL, &stop_value);

  res = gst_validate_scenario_execute_seek (scenario, action,
      rate, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
      GST_SEEK_TYPE_SET, cpos, GST_SEEK_TYPE_SET, stop_value);

beach:
  g_object_unref (timeline);
  return res;
}

gboolean
ges_validate_activate (GstPipeline * pipeline, const gchar * scenario,
    gboolean * needs_setting_state)
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

  const gchar *add_asset_mandatory_fields[] = { "id", "type",
    NULL
  };

  const gchar *remove_asset_mandatory_fields[] = { "id", "type",
    NULL
  };

  const gchar *add_layer_mandatory_fields[] = { "priority",
    NULL
  };

  const gchar *remove_layer_mandatory_fields[] = { "priority",
    NULL
  };

  const gchar *add_clip_mandatory_fields[] = { "name", "layer-priority",
    "asset-id", "type",
    NULL
  };

  const gchar *remove_clip_mandatory_fields[] = { "name",
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
  gst_validate_add_action_type ("add-asset", _add_asset,
      add_asset_mandatory_fields,
      "Allows to add an asset to the current project", FALSE);
  gst_validate_add_action_type ("remove-asset", _remove_asset,
      remove_asset_mandatory_fields,
      "Allows to remove an asset from the current project", FALSE);
  gst_validate_add_action_type ("add-layer", _add_layer,
      add_layer_mandatory_fields,
      "Allows to add a layer to the current timeline", FALSE);
  gst_validate_add_action_type ("remove-layer", _remove_layer,
      remove_layer_mandatory_fields,
      "Allows to remove a layer from the current timeline", FALSE);
  gst_validate_add_action_type ("add-clip", _add_clip,
      add_clip_mandatory_fields, "Allows to add a clip to a given layer",
      FALSE);
  gst_validate_add_action_type ("remove-clip", _remove_clip,
      remove_clip_mandatory_fields,
      "Allows to remove a clip from a given layer", FALSE);
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
    res = gst_validate_runner_printf (runner);

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
    GMainLoop * mainloop)
{
  GstState state;

  gst_message_parse_request_state (message, &state);

  if (GST_IS_VALIDATE_SCENARIO (GST_MESSAGE_SRC (message))
      && state == GST_STATE_NULL) {
    gst_validate_printf (GST_MESSAGE_SRC (message),
        "State change request NULL, " "quiting mainloop\n");
    g_main_loop_quit (mainloop);
  }
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
    GMainLoop * mainloop)
{
  return;
}

#endif
