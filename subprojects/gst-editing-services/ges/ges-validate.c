/* GStreamer Editing Services
 *
 * Copyright (C) <2014> Thibault Saunier <thibault.saunier@collabora.com>
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

#include <ges/ges.h>

#ifdef HAVE_GST_VALIDATE
#include <gst/validate/validate.h>
#include <gst/validate/gst-validate-scenario.h>
#include <gst/validate/gst-validate-utils.h>
#include "ges-internal.h"
#include "ges-structured-interface.h"

#define MONITOR_ON_PIPELINE "validate-monitor"
#define RUNNER_ON_PIPELINE "runner-monitor"

typedef struct
{
  GMainLoop *ml;
  GError *error;
} LoadTimelineData;

static gboolean
_get_clocktime (GstStructure * structure, const gchar * name,
    GstClockTime * val, GESFrameNumber * frames)
{
  const GValue *gvalue = gst_structure_get_value (structure, name);

  if (!gvalue)
    return FALSE;

  if (frames && G_VALUE_TYPE (gvalue) == G_TYPE_STRING) {
    const gchar *str = g_value_get_string (gvalue);

    if (str && str[0] == 'f') {
      GValue v = G_VALUE_INIT;

      g_value_init (&v, G_TYPE_UINT64);
      if (gst_value_deserialize (&v, &str[1])) {
        *frames = g_value_get_uint64 (&v);
        if (val)
          *val = GST_CLOCK_TIME_NONE;
        g_value_reset (&v);

        return TRUE;
      }
      g_value_reset (&v);
    }
  }

  if (!val)
    return FALSE;

  return gst_validate_utils_get_clocktime (structure, name, val);
}

static void
project_loaded_cb (GESProject * project, GESTimeline * timeline,
    LoadTimelineData * data)
{
  g_main_loop_quit (data->ml);
}

static void
error_loading_asset_cb (GESProject * project, GError * err,
    const gchar * unused_id, GType extractable_type, LoadTimelineData * data)
{
  data->error = g_error_copy (err);
  g_main_loop_quit (data->ml);
}

static GESTimeline *
_ges_load_timeline (GstValidateScenario * scenario, GstValidateAction * action,
    const gchar * project_uri)
{
  GESProject *project = ges_project_new (project_uri);
  GESTimeline *timeline;
  LoadTimelineData data = { 0 };

  data.ml = g_main_loop_new (NULL, TRUE);
  timeline =
      GES_TIMELINE (ges_asset_extract (GES_ASSET (project), &data.error));
  if (!timeline)
    goto done;

  g_signal_connect (project, "loaded", (GCallback) project_loaded_cb, &data);
  g_signal_connect (project, "error-loading-asset",
      (GCallback) error_loading_asset_cb, &data);
  g_main_loop_run (data.ml);
  g_signal_handlers_disconnect_by_func (project, project_loaded_cb, &data);
  g_signal_handlers_disconnect_by_func (project, error_loading_asset_cb, &data);
  GST_INFO_OBJECT (scenario, "Loaded timeline from %s", project_uri);

done:
  if (data.error) {
    GST_VALIDATE_REPORT_ACTION (scenario, action,
        SCENARIO_ACTION_EXECUTION_ERROR, "Can not load timeline from: %s (%s)",
        project_uri, data.error->message);
    g_clear_error (&data.error);
    gst_clear_object (&timeline);
  }

  g_main_loop_unref (data.ml);
  gst_object_unref (project);
  return timeline;
}

#ifdef G_HAVE_ISO_VARARGS
#define REPORT_UNLESS(condition, errpoint, ...)                                \
  G_STMT_START {                                                               \
    if (!(condition)) {                                                        \
      res = GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;                        \
      gst_validate_report_action(GST_VALIDATE_REPORTER(scenario), action,      \
                                 SCENARIO_ACTION_EXECUTION_ERROR,              \
                                 __VA_ARGS__);                                 \
      goto errpoint;                                                           \
    }                                                                          \
  }                                                                            \
  G_STMT_END
#else /* G_HAVE_GNUC_VARARGS */
#ifdef G_HAVE_GNUC_VARARGS
#define REPORT_UNLESS(condition, errpoint, args...)                            \
  G_STMT_START {                                                               \
    if (!(condition)) {                                                        \
      res = GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;                        \
      gst_validate_report_action(GST_VALIDATE_REPORTER(scenario), action,      \
                                 SCENARIO_ACTION_EXECUTION_ERROR, ##args);     \
      goto errpoint;                                                           \
    }                                                                          \
  }                                                                            \
  G_STMT_END
#endif /* G_HAVE_ISO_VARARGS */
#endif /* G_HAVE_GNUC_VARARGS */

#define DECLARE_AND_GET_TIMELINE_AND_PIPELINE(scenario, action)                \
  GESTimeline *timeline = NULL;                                                \
  GstElement *pipeline = NULL;                                                 \
  const gchar *project_uri =                                                   \
      gst_structure_get_string(action->structure, "project-uri");              \
  if (!project_uri) {                                                          \
    pipeline = gst_validate_scenario_get_pipeline(scenario);                   \
    REPORT_UNLESS(GES_IS_PIPELINE(pipeline), done,                     \
                  "Can't execute a '%s' action after the pipeline "            \
                  "has been destroyed.",                                       \
                  action->type);                                               \
    g_object_get(pipeline, "timeline", &timeline, NULL);                       \
  } else {                                                                     \
    timeline = _ges_load_timeline(scenario, action, project_uri);              \
    if (!timeline)                                                             \
      return GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;                       \
  }

#define DECLARE_AND_GET_TIMELINE(scenario, action)         \
  DECLARE_AND_GET_TIMELINE_AND_PIPELINE(scenario, action); \
  if (pipeline)                                            \
    gst_object_unref(pipeline);

#define GES_START_VALIDATE_ACTION(funcname)                                    \
static gint                                                                    \
funcname(GstValidateScenario *scenario, GstValidateAction *action) {           \
  GstValidateActionReturn res = GST_VALIDATE_EXECUTE_ACTION_OK;                \
  DECLARE_AND_GET_TIMELINE_AND_PIPELINE(scenario, action);

#define GST_END_VALIDATE_ACTION                                                \
done:                                                                          \
  if (res == GST_VALIDATE_EXECUTE_ACTION_OK) {                                        \
    REPORT_UNLESS(                                                             \
        _ges_save_timeline_if_needed(timeline, action->structure, NULL),       \
        done_no_save, "Could not save timeline to %s",                         \
        gst_structure_get_string(action->structure, "project-id"));            \
  }                                                                            \
                                                                               \
done_no_save:                                                                  \
  gst_clear_object(&pipeline);                                                 \
  gst_clear_object(&timeline);                                                 \
  return res;                                                                  \
}

#define TRY_GET(name,type,var,def) G_STMT_START {\
  if  (!gst_structure_get (action->structure, name, type, var, NULL)) {\
    *var = def; \
  } \
} G_STMT_END

GES_START_VALIDATE_ACTION (_serialize_project)
{
  const gchar *uri = gst_structure_get_string (action->structure, "uri");
  gchar *location = gst_uri_get_location (uri),
      *dir = g_path_get_dirname (location);

  gst_validate_printf (action, "Saving project to %s", uri);

  g_mkdir_with_parents (dir, 0755);
  g_free (location);
  g_free (dir);

  res = ges_timeline_save_to_uri (timeline, uri, NULL, TRUE, NULL);
}

GST_END_VALIDATE_ACTION;

GES_START_VALIDATE_ACTION (_remove_asset)
{
  const gchar *id = NULL;
  const gchar *type_string = NULL;
  GType type;
  GESAsset *asset;
  GESProject *project;

  project = ges_timeline_get_project (timeline);

  id = gst_structure_get_string (action->structure, "id");
  type_string = gst_structure_get_string (action->structure, "type");

  REPORT_UNLESS (type_string && id, done,
      "Missing parameters, we got type %s and id %s", type_string, id);
  REPORT_UNLESS ((type = g_type_from_name (type_string)), done,
      "This type doesn't exist : %s", type_string);

  asset = ges_project_get_asset (project, id, type);
  REPORT_UNLESS (asset, done, "No asset with id %s and type %s", id,
      type_string);
  res = ges_project_remove_asset (project, asset);
  gst_object_unref (asset);
}

GST_END_VALIDATE_ACTION;

GES_START_VALIDATE_ACTION (_add_asset)
{
  const gchar *id = NULL;
  const gchar *type_string = NULL;
  GType type;
  GESAsset *asset = NULL;
  GESProject *project;

  project = ges_timeline_get_project (timeline);

  id = gst_structure_get_string (action->structure, "id");
  type_string = gst_structure_get_string (action->structure, "type");

  gst_validate_printf (action, "Adding asset of type %s with ID %s\n",
      id, type_string);

  REPORT_UNLESS (type_string
      && id, beach, "Missing parameters, we got type %s and id %s", type_string,
      id);
  REPORT_UNLESS ((type =
          g_type_from_name (type_string)), beach,
      "This type doesn't exist : %s", type_string);

  asset = _ges_get_asset_from_timeline (timeline, type, id, NULL);

  REPORT_UNLESS (asset, beach, "Could not get asset for %s id: %s", type_string,
      id);
  res = ges_project_add_asset (project, asset);

beach:
  gst_clear_object (&asset);
}

GST_END_VALIDATE_ACTION;

GES_START_VALIDATE_ACTION (_add_layer)
{
  GESLayer *layer;
  gint priority;
  gboolean auto_transition = FALSE;

  REPORT_UNLESS (gst_structure_get_int (action->structure, "priority",
          &priority), done, "priority is needed when adding a layer");
  REPORT_UNLESS ((layer =
          _ges_get_layer_by_priority (timeline, priority)), done,
      "No layer with priority: %d", priority);

  gst_structure_get_boolean (action->structure, "auto-transition",
      &auto_transition);
  g_object_set (layer, "priority", priority, "auto-transition", auto_transition,
      NULL);
  gst_object_unref (layer);
}

GST_END_VALIDATE_ACTION;

GES_START_VALIDATE_ACTION (_remove_layer)
{
  GESLayer *layer = NULL;
  gint priority;

  REPORT_UNLESS (gst_structure_get_int (action->structure, "priority",
          &priority), done, "'priority' is required when removing a layer");
  layer = _ges_get_layer_by_priority (timeline, priority);
  REPORT_UNLESS (layer, beach, "No layer with priority %d", priority);

  res = ges_timeline_remove_layer (timeline, layer);

beach:
  gst_clear_object (&layer);
}

GST_END_VALIDATE_ACTION;

GES_START_VALIDATE_ACTION (_remove_clip)
{
  GESTimelineElement *clip;
  GESLayer *layer = NULL;
  const gchar *name;

  name = gst_structure_get_string (action->structure, "name");
  clip = ges_timeline_get_element (timeline, name);
  REPORT_UNLESS (GES_IS_CLIP (clip), beach, "Couldn't find clip: %s", name);

  layer = ges_clip_get_layer (GES_CLIP (clip));
  REPORT_UNLESS (layer, beach, "Clip %s not in a layer", name);

  res = ges_layer_remove_clip (layer, GES_CLIP (clip));

beach:
  gst_clear_object (&layer);
  gst_clear_object (&clip);
}

GST_END_VALIDATE_ACTION;

GES_START_VALIDATE_ACTION (_edit)
{
  GList *layers = NULL;
  GESTimelineElement *element;
  GESFrameNumber fposition = GES_FRAME_NUMBER_NONE;
  GstClockTime position;
  GError *err = NULL;
  gboolean source_position = FALSE;

  gint new_layer_priority = -1;
  guint edge = GES_EDGE_NONE;
  guint mode = GES_EDIT_MODE_NORMAL;

  const gchar *edit_mode_str = NULL, *edge_str = NULL;
  const gchar *element_name;

  res = GST_VALIDATE_EXECUTE_ACTION_ERROR;
  element_name = gst_structure_get_string (action->structure,
      gst_structure_has_name (action->structure, "edit-container") ?
      "container-name" : "element-name");

  element = ges_timeline_get_element (timeline, element_name);
  REPORT_UNLESS (element, beach, "Could not find element %s", element_name);

  if (!_get_clocktime (action->structure, "position", &position, &fposition)) {
    gint pos;
    gint64 pos64;

    if (gst_structure_get_int (action->structure, "source-frame", &pos)) {
      fposition = pos;
    } else if (gst_structure_get_int64 (action->structure, "source-frame",
            &pos64)) {
      fposition = pos64;
    } else {
      gchar *structstr = gst_structure_to_string (action->structure);

      GST_VALIDATE_REPORT_ACTION (scenario, action,
          SCENARIO_ACTION_EXECUTION_ERROR,
          "could not find `position` or `source-frame` in %s", structstr);
      g_free (structstr);
      goto beach;
    }

    source_position = TRUE;
    position = GST_CLOCK_TIME_NONE;
  }

  if ((edit_mode_str =
          gst_structure_get_string (action->structure, "edit-mode"))) {
    REPORT_UNLESS (gst_validate_utils_enum_from_str (GES_TYPE_EDIT_MODE,
            edit_mode_str, &mode), beach,
        "Could not get enum from %s", edit_mode_str);
  }

  if ((edge_str = gst_structure_get_string (action->structure, "edge"))) {
    REPORT_UNLESS (gst_validate_utils_enum_from_str (GES_TYPE_EDGE, edge_str,
            &edge), beach, "Could not get enum from %s", edge_str);
  }

  if (GES_FRAME_NUMBER_IS_VALID (fposition)) {
    if (source_position) {
      GESClip *clip = NULL;

      if (GES_IS_CLIP (element))
        clip = GES_CLIP (element);
      else if (GES_IS_TRACK_ELEMENT (element))
        clip = GES_CLIP (element->parent);

      REPORT_UNLESS (clip, beach,
          "Could not get find element to edit using source frame for %"
          GST_PTR_FORMAT, action->structure);
      position =
          ges_clip_get_timeline_time_from_source_frame (clip, fposition, &err);
    } else {
      position = ges_timeline_get_frame_time (timeline, fposition);
    }

    REPORT_UNLESS (GST_CLOCK_TIME_IS_VALID (position), beach,
        "Invalid frame number '%" G_GINT64_FORMAT "': %s", fposition,
        err ? err->message : "Unknown");
  }

  gst_structure_get_int (action->structure, "new-layer-priority",
      &new_layer_priority);

  if (!(res = ges_timeline_element_edit (element, layers,
              new_layer_priority, mode, edge, position))) {

    gchar *fpositionstr = GES_FRAME_NUMBER_IS_VALID (fposition)
        ? g_strdup_printf ("(%" G_GINT64_FORMAT ")", fposition)
        : NULL;

    GST_VALIDATE_REPORT_ACTION (scenario, action,
        SCENARIO_ACTION_EXECUTION_ERROR,
        "Could not edit '%s' to %" GST_TIME_FORMAT
        "%s in %s mode, edge: %s "
        "with new layer prio: %d",
        element_name, GST_TIME_ARGS (position),
        fpositionstr ? fpositionstr : "",
        edit_mode_str ? edit_mode_str : "normal",
        edge_str ? edge_str : "None", new_layer_priority);
    g_free (fpositionstr);
    goto beach;
  }

beach:
  gst_clear_object (&element);
  g_clear_error (&err);
}

GST_END_VALIDATE_ACTION;


static void
_state_changed_cb (GstBus * bus, GstMessage * message,
    GstValidateAction * action)
{
  GstState next_state;

  if (!GST_IS_PIPELINE (GST_MESSAGE_SRC (message)))
    return;

  gst_message_parse_state_changed (message, NULL, NULL, &next_state);

  if (next_state == GST_STATE_VOID_PENDING) {
    gst_validate_action_set_done (action);

    g_signal_handlers_disconnect_by_func (bus, _state_changed_cb, action);
  }
}

GES_START_VALIDATE_ACTION (_commit)
{
  GstBus *bus;
  GstState state;

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));

  gst_validate_printf (action, "Committing timeline %s\n",
      GST_OBJECT_NAME (timeline));

  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (_state_changed_cb), action);

  gst_element_get_state (pipeline, &state, NULL, 0);
  if (!ges_timeline_commit (timeline) || state < GST_STATE_PAUSED) {
    g_signal_handlers_disconnect_by_func (bus, G_CALLBACK (_state_changed_cb),
        action);
    gst_object_unref (bus);
    goto done;
  }

  gst_object_unref (bus);

  res = GST_VALIDATE_EXECUTE_ACTION_ASYNC;
}

GST_END_VALIDATE_ACTION;

GES_START_VALIDATE_ACTION (_split_clip)
{
  const gchar *clip_name;
  GESTimelineElement *element;
  GstClockTime position;

  clip_name = gst_structure_get_string (action->structure, "clip-name");

  element = ges_timeline_get_element (timeline, clip_name);
  REPORT_UNLESS (GES_IS_CLIP (element), beach, "Could not find clip: %s",
      clip_name);
  REPORT_UNLESS (gst_validate_action_get_clocktime (scenario, action,
          "position", &position), beach,
      "Could not find position in %" GST_PTR_FORMAT, action->structure);
  res = (ges_clip_split (GES_CLIP (element), position) != NULL);

beach:
  gst_clear_object (&element);
}

GST_END_VALIDATE_ACTION;

typedef struct
{
  GstValidateScenario *scenario;
  GESTimelineElement *element;
  GstValidateActionReturn res;
  GstClockTime time;
  gboolean on_children;
  GstValidateAction *action;
} PropertyData;

static gboolean
check_property (GQuark field_id, GValue * expected_value, PropertyData * data)
{
  GValue cvalue = G_VALUE_INIT, *tvalue = NULL, comparable_value = G_VALUE_INIT,
      *observed_value;
  const gchar *property = g_quark_to_string (field_id);
  GstControlBinding *binding = NULL;

  if (!data->on_children) {
    GObject *tmpobject, *object = g_object_ref (G_OBJECT (data->element));
    gchar **object_prop_name = g_strsplit (property, "::", 2);
    gint i = 0;
    GParamSpec *pspec = NULL;

    while (TRUE) {
      pspec =
          g_object_class_find_property (G_OBJECT_GET_CLASS (object),
          object_prop_name[i]);

      if (!pspec) {
        GST_VALIDATE_REPORT_ACTION (data->scenario, data->action,
            SCENARIO_ACTION_EXECUTION_ERROR,
            "Could not get property %s on %" GES_FORMAT,
            object_prop_name[i], GES_ARGS (data->element));
        data->res = GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
        g_strfreev (object_prop_name);

        return FALSE;
      }

      if (!object_prop_name[++i])
        break;

      tmpobject = object;
      g_object_get (tmpobject, pspec->name, &object, NULL);
      g_object_unref (tmpobject);
    }

    g_strfreev (object_prop_name);
    g_value_init (&cvalue, pspec->value_type);
    g_object_get_property (object, pspec->name, &cvalue);
    g_object_unref (object);
    goto compare;
  }

  if (GST_CLOCK_TIME_IS_VALID (data->time)) {
    if (!GES_IS_TRACK_ELEMENT (data->element)) {
      GST_VALIDATE_REPORT_ACTION (data->scenario, data->action,
          SCENARIO_ACTION_EXECUTION_ERROR,
          "Could not get property at time for type %s - only GESTrackElement supported",
          G_OBJECT_TYPE_NAME (data->element));
      data->res = GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;

      return FALSE;
    }

    binding =
        ges_track_element_get_control_binding (GES_TRACK_ELEMENT
        (data->element), property);
    if (binding) {
      tvalue = gst_control_binding_get_value (binding, data->time);

      if (!tvalue) {
        GST_VALIDATE_REPORT_ACTION (data->scenario, data->action,
            SCENARIO_ACTION_EXECUTION_ERROR,
            "Could not get property: %s at %" GST_TIME_FORMAT, property,
            GST_TIME_ARGS (data->time));
        data->res = GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;

        return FALSE;
      }
    }
  }

  if (!tvalue
      && !ges_timeline_element_get_child_property (data->element, property,
          &cvalue)) {
    GST_VALIDATE_REPORT_ACTION (data->scenario, data->action,
        SCENARIO_ACTION_EXECUTION_ERROR, "Could not get child property: %s:",
        property);
    data->res = GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;

    return FALSE;
  }

compare:
  observed_value = tvalue ? tvalue : &cvalue;

  if (G_VALUE_TYPE (observed_value) != G_VALUE_TYPE (expected_value)) {
    g_value_init (&comparable_value, G_VALUE_TYPE (observed_value));

    if (G_VALUE_TYPE (observed_value) == GST_TYPE_CLOCK_TIME) {
      GstClockTime t;

      if (gst_validate_utils_get_clocktime (data->action->structure, property,
              &t)) {
        g_value_set_uint64 (&comparable_value, t);
        expected_value = &comparable_value;
      }
    } else if (g_value_transform (expected_value, &comparable_value)) {
      expected_value = &comparable_value;
    }
  }

  if (gst_value_compare (observed_value, expected_value) != GST_VALUE_EQUAL) {
    gchar *expected = gst_value_serialize (expected_value), *observed =
        gst_value_serialize (observed_value);

    GST_VALIDATE_REPORT_ACTION (data->scenario, data->action,
        SCENARIO_ACTION_CHECK_ERROR,
        "%s::%s expected value: '(%s)%s' different than observed: '(%s)%s'",
        GES_TIMELINE_ELEMENT_NAME (data->element), property,
        G_VALUE_TYPE_NAME (observed_value), expected,
        G_VALUE_TYPE_NAME (expected_value), observed);

    g_free (expected);
    g_free (observed);
    data->res = GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
  }

  if (G_VALUE_TYPE (&comparable_value) != G_TYPE_NONE)
    g_value_unset (&comparable_value);

  if (tvalue) {
    g_value_unset (tvalue);
    g_free (tvalue);
  } else
    g_value_reset (&cvalue);
  return TRUE;
}

static gboolean
set_property (GQuark field_id, const GValue * value, PropertyData * data)
{
  const gchar *property = g_quark_to_string (field_id);

  if (data->on_children) {
    if (!ges_timeline_element_set_child_property (data->element, property,
            value)) {
      gchar *v = gst_value_serialize (value);

      GST_VALIDATE_REPORT_ACTION (data->scenario, data->action,
          SCENARIO_ACTION_EXECUTION_ERROR,
          "Could not set %s child property %s to %s",
          GES_TIMELINE_ELEMENT_NAME (data->element), property, v);

      data->res = GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
      g_free (v);

      return FALSE;
    }
  } else {
    data->res =
        gst_validate_object_set_property (GST_VALIDATE_REPORTER
        (data->scenario), G_OBJECT (data->element), property, value, FALSE);
  }

  return TRUE;
}

GES_START_VALIDATE_ACTION (set_or_check_properties)
{
  GESTimelineElement *element;
  GstStructure *structure;
  const gchar *element_name;
  gboolean is_setting = FALSE;
  PropertyData data = {
    .scenario = scenario,
    .element = NULL,
    .res = GST_VALIDATE_EXECUTE_ACTION_OK,
    .time = GST_CLOCK_TIME_NONE,
    .on_children =
        !gst_structure_has_name (action->structure, "check-ges-properties")
        && !gst_structure_has_name (action->structure, "set-ges-properties"),
    .action = action,
  };

  is_setting = gst_structure_has_name (action->structure, "set-ges-properties")
      || gst_structure_has_name (action->structure, "set-child-properties");
  gst_validate_action_get_clocktime (scenario, action, "at-time", &data.time);

  structure = gst_structure_copy (action->structure);
  element_name = gst_structure_get_string (structure, "element-name");
  element = ges_timeline_get_element (timeline, element_name);
  if (!element) {
    GST_VALIDATE_REPORT_ACTION (scenario, action,
        SCENARIO_ACTION_EXECUTION_ERROR,
        "Can not find element: %s", element_name);

    data.res = GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
    goto local_done;
  }

  data.element = element;
  gst_structure_remove_fields (structure, "element-name", "at-time",
      "project-uri", NULL);
  gst_structure_foreach (structure,
      is_setting ? (GstStructureForeachFunc) set_property
      : (GstStructureForeachFunc) check_property, &data);
  gst_object_unref (element);

local_done:
  gst_structure_free (structure);
  res = data.res;
}

GST_END_VALIDATE_ACTION;

GES_START_VALIDATE_ACTION (_set_track_restriction_caps)
{
  GList *tmp;
  GstCaps *caps;
  GESTrackType track_types;

  const gchar *track_type_str =
      gst_structure_get_string (action->structure, "track-type");
  const gchar *caps_str = gst_structure_get_string (action->structure, "caps");

  REPORT_UNLESS (track_types =
      gst_validate_utils_flags_from_str (GES_TYPE_TRACK_TYPE, track_type_str),
      done, "Invalid track types: %s", track_type_str);

  REPORT_UNLESS (caps = gst_caps_from_string (caps_str),
      done, "Invalid track restriction caps: %s", caps_str);

  res = GST_VALIDATE_EXECUTE_ACTION_ERROR;
  for (tmp = timeline->tracks; tmp; tmp = tmp->next) {
    GESTrack *track = tmp->data;

    if (track->type & track_types) {
      gchar *str;

      str = gst_caps_to_string (caps);
      g_free (str);

      ges_track_set_restriction_caps (track, caps);

      res = GST_VALIDATE_EXECUTE_ACTION_OK;
    }
  }
  gst_caps_unref (caps);
}

GST_END_VALIDATE_ACTION;

GES_START_VALIDATE_ACTION (_set_asset_on_element)
{
  GESAsset *asset;
  GESTimelineElement *element;
  const gchar *element_name, *id;

  element_name = gst_structure_get_string (action->structure, "element-name");
  element = ges_timeline_get_element (timeline, element_name);
  REPORT_UNLESS (element, done, "Can't find %s", element_name);

  id = gst_structure_get_string (action->structure, "asset-id");

  gst_validate_printf (action, "Setting asset %s on element %s\n",
      id, element_name);

  asset = _ges_get_asset_from_timeline (timeline, G_OBJECT_TYPE (element), id,
      NULL);
  REPORT_UNLESS (asset, beach, "Could not find asset: %s", id);

  res = ges_extractable_set_asset (GES_EXTRACTABLE (element), asset);

beach:
  gst_clear_object (&asset);
  gst_clear_object (&element);
}

GST_END_VALIDATE_ACTION;

GES_START_VALIDATE_ACTION (_container_remove_child)
{
  GESContainer *container = NULL;
  GESTimelineElement *child = NULL;
  const gchar *container_name, *child_name;

  container_name =
      gst_structure_get_string (action->structure, "container-name");
  container =
      (GESContainer *) ges_timeline_get_element (timeline, container_name);
  REPORT_UNLESS (GES_IS_CONTAINER (container), beach,
      "Could not find container: %s", container_name);

  child_name = gst_structure_get_string (action->structure, "child-name");
  child = ges_timeline_get_element (timeline, child_name);
  REPORT_UNLESS (GES_IS_TIMELINE_ELEMENT (child), beach, "Could not find %s",
      child_name);

  res = ges_container_remove (container, child);

beach:
  gst_clear_object (&container);
  gst_clear_object (&child);
}

GST_END_VALIDATE_ACTION;

GES_START_VALIDATE_ACTION (_ungroup)
{
  GESContainer *container;
  gboolean recursive = FALSE;
  const gchar *container_name;

  container_name =
      gst_structure_get_string (action->structure, "container-name");
  container =
      (GESContainer *) ges_timeline_get_element (timeline, container_name);
  REPORT_UNLESS (GES_IS_CONTAINER (container), beach, "Could not find %s",
      container_name);

  gst_structure_get_boolean (action->structure, "recursive", &recursive);

  g_list_free (ges_container_ungroup (container, recursive));

beach:
  gst_clear_object (&container);
}

GST_END_VALIDATE_ACTION;

GES_START_VALIDATE_ACTION (_copy_element)
{
  GESTimelineElement *element = NULL, *copied = NULL, *pasted = NULL;
  gboolean recursive = FALSE;
  const gchar *element_name, *paste_name;
  GstClockTime position;

  element_name = gst_structure_get_string (action->structure, "element-name");
  element = ges_timeline_get_element (timeline, element_name);

  REPORT_UNLESS (GES_IS_CONTAINER (element), beach, "Could not find %s",
      element_name);

  if (!gst_structure_get_boolean (action->structure, "recursive", &recursive))
    recursive = TRUE;

  REPORT_UNLESS (gst_validate_action_get_clocktime (scenario, action,
          "position", &position), beach, "Could not find position");

  copied = ges_timeline_element_copy (element, recursive);
  pasted = ges_timeline_element_paste (copied, position);

  REPORT_UNLESS (pasted, beach, "Could not paste clip %s", element_name);

  paste_name = gst_structure_get_string (action->structure, "paste-name");
  if (paste_name)
    REPORT_UNLESS (ges_timeline_element_set_name (pasted, paste_name),
        beach, "Could not set element name %s", paste_name);

beach:
  gst_clear_object (&pasted);
  gst_clear_object (&element);

  /* `copied` is only used for the single paste operation, and is not
   * actually in any timeline. We own it (it is actually still floating).
   * `pasted` is the actual new object in the timeline. We own a
   * reference to it. */
  gst_clear_object (&copied);
}

GST_END_VALIDATE_ACTION;

GES_START_VALIDATE_ACTION (_validate_action_execute)
{
  GError *err = NULL;
  ActionFromStructureFunc func;

  gst_structure_remove_field (action->structure, "playback-time");
  if (gst_structure_has_name (action->structure, "add-keyframe") ||
      gst_structure_has_name (action->structure, "remove-keyframe")) {
    func = _ges_add_remove_keyframe_from_struct;
  } else if (gst_structure_has_name (action->structure, "set-control-source")) {
    func = _ges_set_control_source_from_struct;
  } else if (gst_structure_has_name (action->structure, "add-clip")) {
    func = _ges_add_clip_from_struct;
  } else if (gst_structure_has_name (action->structure, "container-add-child")) {
    func = _ges_container_add_child_from_struct;
  } else if (gst_structure_has_name (action->structure, "set-child-property")) {
    func = _ges_set_child_property_from_struct;
  } else {
    g_assert_not_reached ();
  }

  if (!func (timeline, action->structure, &err)) {
    GST_VALIDATE_REPORT_ACTION (scenario, action,
        g_quark_from_string ("scenario::execution-error"),
        "Could not execute %s (error: %s)",
        gst_structure_get_name (action->structure),
        err ? err->message : "None");

    g_clear_error (&err);
    res = GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
  }
}

GST_END_VALIDATE_ACTION;

static void
_project_loaded_cb (GESProject * project, GESTimeline * timeline,
    GstValidateAction * action)
{
  gst_validate_action_set_done (action);
}

GES_START_VALIDATE_ACTION (_load_project)
{
  GstState state;
  GESProject *project = NULL;
  GList *tmp, *tmp_full;

  gchar *uri = NULL;
  GError *error = NULL;
  const gchar *content = NULL;

  gchar *tmpfile = g_strdup_printf ("%s%s%s", g_get_tmp_dir (),
      G_DIR_SEPARATOR_S, "tmpxgesload.xges");

  res = GST_VALIDATE_EXECUTE_ACTION_ASYNC;
  REPORT_UNLESS (GES_IS_PIPELINE (pipeline), local_done,
      "Not a GES pipeline, can't work with it");

  gst_element_get_state (pipeline, &state, NULL, 0);
  gst_element_set_state (pipeline, GST_STATE_NULL);

  content = gst_structure_get_string (action->structure, "serialized-content");
  if (content) {

    g_file_set_contents (tmpfile, content, -1, &error);
    REPORT_UNLESS (!error, local_done,
        "Could not set XML content: %s", error->message);

    uri = gst_filename_to_uri (tmpfile, &error);
    REPORT_UNLESS (!error, local_done,
        "Could not set filename to URI: %s", error->message);
  } else {
    uri = g_strdup (gst_structure_get_string (action->structure, "uri"));
    REPORT_UNLESS (uri, local_done,
        "None of 'uri' or 'content' passed as parameter"
        " can't load any timeline!");
  }

  tmp_full = ges_timeline_get_layers (timeline);
  for (tmp = tmp_full; tmp; tmp = tmp->next)
    ges_timeline_remove_layer (timeline, tmp->data);
  g_list_free_full (tmp_full, gst_object_unref);

  tmp_full = ges_timeline_get_tracks (timeline);
  for (tmp = tmp_full; tmp; tmp = tmp->next)
    ges_timeline_remove_track (timeline, tmp->data);
  g_list_free_full (tmp_full, gst_object_unref);

  project = ges_project_new (uri);
  g_signal_connect (project, "loaded", G_CALLBACK (_project_loaded_cb), action);
  ges_project_load (project, gst_object_ref (timeline), &error);
  REPORT_UNLESS (!error, local_done,
      "Could not load timeline: %s", error->message);

  gst_element_set_state (pipeline, state);

local_done:
  gst_clear_object (&project);
  g_clear_error (&error);
  g_free (uri);
  g_free (tmpfile);
}

GST_END_VALIDATE_ACTION;

static gint
prepare_seek_action (GstValidateAction * action)
{
  gint res = GST_VALIDATE_EXECUTE_ACTION_ERROR;
  GESFrameNumber fstart, fstop;
  GstValidateScenario *scenario = gst_validate_action_get_scenario (action);
  GstValidateActionType *type = gst_validate_get_action_type (action->type);
  GError *err = NULL;

  DECLARE_AND_GET_TIMELINE (scenario, action);

  if (timeline
      && ges_util_structure_get_clocktime (action->structure, "start", NULL,
          &fstart)) {
    GstClockTime start = ges_timeline_get_frame_time (timeline, fstart);

    if (err) {
      GST_VALIDATE_REPORT_ACTION (scenario, action,
          SCENARIO_ACTION_EXECUTION_ERROR,
          "Invalid seeking frame number '%" G_GINT64_FORMAT "': %s", fstart,
          err->message);
      goto done;
    }
    gst_structure_set (action->structure, "start", G_TYPE_UINT64, start, NULL);
  }

  if (timeline
      && ges_util_structure_get_clocktime (action->structure, "stop", NULL,
          &fstop)) {
    GstClockTime stop = ges_timeline_get_frame_time (timeline, fstop);

    if (err) {
      GST_VALIDATE_REPORT_ACTION (scenario, action,
          SCENARIO_ACTION_EXECUTION_ERROR,
          "Invalid seeking frame number '%" G_GINT64_FORMAT "': %s", fstop,
          err->message);
      goto done;
    }
    gst_structure_set (action->structure, "stop", G_TYPE_UINT64, stop, NULL);
  }

  gst_object_unref (scenario);
  gst_object_unref (timeline);
  return type->overriden_type->prepare (action);

done:
  gst_object_unref (scenario);
  gst_object_unref (timeline);
  return res;
}

static gint
set_layer_active (GstValidateScenario * scenario, GstValidateAction * action)
{
  gboolean active;
  gint i, layer_prio;
  GESLayer *layer;
  GList *tracks = NULL;
  GstValidateExecuteActionReturn res = GST_VALIDATE_EXECUTE_ACTION_OK;
  gchar **track_names =
      gst_validate_utils_get_strv (action->structure, "tracks");

  DECLARE_AND_GET_TIMELINE (scenario, action);

  for (i = 0; track_names[i]; i++) {
    GESTrack *track =
        (GESTrack *) gst_bin_get_by_name (GST_BIN (timeline), track_names[i]);

    if (!track) {
      GST_VALIDATE_REPORT_ACTION (scenario, action,
          SCENARIO_ACTION_EXECUTION_ERROR,
          "Could not find track %s", track_names[i]);
      res = GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
      goto done;
    }

    tracks = g_list_prepend (tracks, track);
  }

  if (!gst_structure_get_int (action->structure, "layer-priority", &layer_prio)) {
    GST_VALIDATE_REPORT_ACTION (scenario, action,
        SCENARIO_ACTION_EXECUTION_ERROR,
        "Could not find layer from %" GST_PTR_FORMAT, action->structure);
    res = GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
    goto done;
  }
  if (!(layer = g_list_nth_data (timeline->layers, layer_prio))) {
    GST_VALIDATE_REPORT_ACTION (scenario, action,
        SCENARIO_ACTION_EXECUTION_ERROR, "Could not find layer %d", layer_prio);
    res = GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
    goto done;
  }

  if (!gst_structure_get_boolean (action->structure, "active", &active)) {
    GST_VALIDATE_REPORT_ACTION (scenario, action,
        SCENARIO_ACTION_EXECUTION_ERROR,
        "Could not find 'active' boolean in %" GST_PTR_FORMAT,
        action->structure);
    res = GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
    goto done;
  }

  if (!ges_layer_set_active_for_tracks (layer, active, tracks)) {
    GST_VALIDATE_REPORT_ACTION (scenario, action,
        SCENARIO_ACTION_EXECUTION_ERROR,
        "Could not set active for track defined in %" GST_PTR_FORMAT,
        action->structure);
    res = GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
    goto done;
  }

done:
  g_strfreev (track_names);
  gst_object_unref (timeline);
  g_list_free_full (tracks, gst_object_unref);

  return res;
}

#endif

gboolean
ges_validate_register_action_types (void)
{
#ifdef HAVE_GST_VALIDATE
  GstValidateActionType *validate_seek, *seek_override;


  gst_validate_init ();
  validate_seek = gst_validate_get_action_type ("seek");

  /*  *INDENT-OFF* */
  seek_override = gst_validate_register_action_type("seek", "ges", validate_seek->execute,
                                    validate_seek->parameters, validate_seek->description,
                                    validate_seek->flags);
  gst_mini_object_unref(GST_MINI_OBJECT(validate_seek));
  seek_override->prepare = prepare_seek_action;

  gst_validate_register_action_type ("edit-container", "ges", _edit,
      (GstValidateActionParameter [])  {
        {
         .name = "container-name",
         .description = "The name of the GESContainer to edit",
         .mandatory = TRUE,
         .types = "string",
        },
        {
          .name = "position",
          .description = "The new position of the GESContainer",
          .mandatory = FALSE,
          .types = "double or string",
          .possible_variables = "position: The current position in the stream\n"
            "duration: The duration of the stream",
           NULL
        },
        {
          .name = "edit-mode",
          .description = "The GESEditMode to use to edit @container-name",
          .mandatory = FALSE,
          .types = "string",
          .def = "normal",
        },
        {
          .name = "edge",
          .description = "The GESEdge to use to edit @container-name\n"
                         "should be in [ start, end, none ] ",
          .mandatory = FALSE,
          .types = "string",
          .def = "none",
        },
        {
          .name = "new-layer-priority",
          .description = "The priority of the layer @container should land in.\n"
                         "If the layer you're trying to move the container to doesn't exist, it will\n"
                         "be created automatically. -1 means no move.",
          .mandatory = FALSE,
          .types = "int",
          .def = "-1",
        },
        {
          .name = "project-uri",
          .description = "The project URI with the serialized timeline to execute the action on",
          .types = "string",
          .mandatory = FALSE,
        },
        {NULL}
       },
       "Allows to edit a container (like a GESClip), for more details, have a look at:\n"
       "ges_timeline_element_edit documentation, Note that the timeline will\n"
       "be committed, and flushed so that the edition is taken into account",
       GST_VALIDATE_ACTION_TYPE_NONE);

  gst_validate_register_action_type ("edit", "ges", _edit,
      (GstValidateActionParameter [])  {
        {
         .name = "element-name",
         .description = "The name of the element to edit",
         .mandatory = TRUE,
         .types = "string",
        },
        {
          .name = "position",
          .description = "The new position of the element",
          .mandatory = FALSE,
          .types = "double or string",
          .possible_variables = "position: The current position in the stream\n"
            "duration: The duration of the stream",
           NULL
        },
        {
          .name = "source-frame",
          .description = "The new frame of the element, computed from the @element-name"
            "clip's source frame.",
          .mandatory = FALSE,
          .types = "double or string",
           NULL
        },
        {
          .name = "edit-mode",
          .description = "The GESEditMode to use to edit @element-name",
          .mandatory = FALSE,
          .types = "string",
          .def = "normal",
        },
        {
          .name = "edge",
          .description = "The GESEdge to use to edit @element-name\n"
                         "should be in [ start, end, none ] ",
          .mandatory = FALSE,
          .types = "string",
          .def = "none",
        },
        {
          .name = "new-layer-priority",
          .description = "The priority of the layer @element should land in.\n"
                         "If the layer you're trying to move the element to doesn't exist, it will\n"
                         "be created automatically. -1 means no move.",
          .mandatory = FALSE,
          .types = "int",
          .def = "-1",
        },
        {
          .name = "project-uri",
          .description = "The project URI with the serialized timeline to execute the action on",
          .types = "string",
          .mandatory = FALSE,
        },
        {NULL}
       },
       "Allows to edit a element (like a GESClip), for more details, have a look at:\n"
       "ges_timeline_element_edit documentation, Note that the timeline will\n"
       "be committed, and flushed so that the edition is taken into account",
       GST_VALIDATE_ACTION_TYPE_NONE);

  gst_validate_register_action_type ("add-asset", "ges", _add_asset,
      (GstValidateActionParameter [])  {
        {
          .name = "id",
          .description = "Adds an asset to a project.",
          .mandatory = TRUE,
          NULL
        },
        {
          .name = "type",
          .description = "The type of asset to add",
          .mandatory = TRUE,
          NULL
        },
        {
          .name = "project-uri",
          .description = "The project URI with the serialized timeline to execute the action on",
          .types = "string",
          .mandatory = FALSE,
        },
        {NULL}
      },
      "Allows to add an asset to the current project", GST_VALIDATE_ACTION_TYPE_NONE);

  gst_validate_register_action_type ("remove-asset", "ges", _remove_asset,
      (GstValidateActionParameter [])  {
        {
          .name = "id",
          .description = "The ID of the clip to remove",
          .mandatory = TRUE,
          NULL
        },
        {
          .name = "type",
          .description = "The type of asset to remove",
          .mandatory = TRUE,
          NULL
        },
        {
          .name = "project-uri",
          .description = "The project URI with the serialized timeline to execute the action on",
          .types = "string",
          .mandatory = FALSE,
        },
        { NULL }
      },
      "Allows to remove an asset from the current project", GST_VALIDATE_ACTION_TYPE_NONE);

  gst_validate_register_action_type ("add-layer", "ges", _add_layer,
      (GstValidateActionParameter [])  {
        {
          .name = "priority",
          .description = "The priority of the new layer to add,"
                         "if not specified, the new layer will be"
                         " appended to the timeline",
          .mandatory = FALSE,
          NULL
        },
        {
          .name = "project-uri",
          .description = "The project URI with the serialized timeline to execute the action on",
          .types = "string",
          .mandatory = FALSE,
        },
        { NULL }
      },
      "Allows to add a layer to the current timeline", GST_VALIDATE_ACTION_TYPE_NONE);

  gst_validate_register_action_type ("remove-layer", "ges", _remove_layer,
      (GstValidateActionParameter [])  {
        {
          .name = "priority",
          .description = "The priority of the layer to remove",
          .mandatory = TRUE,
          NULL
        },
        {
          .name = "auto-transition",
          .description = "Whether auto-transition is activated on the new layer.",
          .mandatory = FALSE,
          .types="boolean",
          .def = "False"
        },
        {
          .name = "project-uri",
          .description = "The nested timeline to add clip to",
          .types = "string",
          .mandatory = FALSE,
        },
        { NULL }
      },
      "Allows to remove a layer from the current timeline", GST_VALIDATE_ACTION_TYPE_NONE);

  gst_validate_register_action_type ("add-clip", "ges", _validate_action_execute,
      (GstValidateActionParameter []) {
        {
          .name = "name",
          .description = "The name of the clip to add",
          .types = "string",
          .mandatory = TRUE,
        },
        {
          .name = "layer-priority",
          .description = "The priority of the clip to add",
          .types = "int",
          .mandatory = TRUE,
        },
        {
          .name = "asset-id",
          .description = "The id of the asset from which to extract the clip",
          .types = "string",
          .mandatory = TRUE,
        },
        {
          .name = "type",
          .description = "The type of the clip to create",
          .types = "string",
          .mandatory = TRUE,
        },
        {
          .name = "start",
          .description = "The start value to set on the new GESClip.",
          .types = "double or string",
          .mandatory = FALSE,
        },
        {
          .name = "inpoint",
          .description = "The  inpoint value to set on the new GESClip",
          .types = "double or string",
          .mandatory = FALSE,
        },
        {
          .name = "duration",
          .description = "The  duration value to set on the new GESClip",
          .types = "double or string",
          .mandatory = FALSE,
        },
        {
          .name = "project-uri",
          .description = "The project URI with the serialized timeline to execute the action on",
          .types = "string",
          .mandatory = FALSE,
        },
        {NULL}
      }, "Allows to add a clip to a given layer", GST_VALIDATE_ACTION_TYPE_NONE);

  gst_validate_register_action_type ("remove-clip", "ges", _remove_clip,
      (GstValidateActionParameter []) {
        {
          .name = "name",
          .description = "The name of the clip to remove",
          .types = "string",
          .mandatory = TRUE,
        },
        {
          .name = "project-uri",
          .description = "The project URI with the serialized timeline to execute the action on",
          .types = "string",
          .mandatory = FALSE,
        },
        {NULL}
      }, "Allows to remove a clip from a given layer", GST_VALIDATE_ACTION_TYPE_NONE);

  gst_validate_register_action_type ("serialize-project", "ges", _serialize_project,
      (GstValidateActionParameter []) {
        {
          .name = "uri",
          .description = "The uri where to store the serialized project",
          .types = "string",
          .mandatory = TRUE,
        },
        {NULL}
      }, "serializes a project", GST_VALIDATE_ACTION_TYPE_NONE);

  gst_validate_register_action_type ("set-child-property", "ges", _validate_action_execute,
      (GstValidateActionParameter []) {
        {
          .name = "element-name",
          .description = "The name of the element on which to modify the property",
          .types = "string",
          .mandatory = TRUE,
        },
        {
          .name = "property",
          .description = "The name of the property to modify",
          .types = "string",
          .mandatory = TRUE,
        },
        {
          .name = "value",
          .description = "The value of the property",
          .types = "gvalue",
          .mandatory = TRUE,
        },
        {
          .name = "project-uri",
          .description = "The project URI with the serialized timeline to execute the action on",
          .types = "string",
          .mandatory = FALSE,
        },
        {NULL}
      }, "Allows to change child property of an object", GST_VALIDATE_ACTION_TYPE_NONE);

  gst_validate_register_action_type ("set-layer-active", "ges", set_layer_active,
      (GstValidateActionParameter []) {
        {
          .name = "layer-priority",
          .description = "The priority of the layer to set activness on",
          .types = "gint",
          .mandatory = TRUE,
        },
        {
          .name = "active",
          .description = "The activness of the layer",
          .types = "gboolean",
          .mandatory = TRUE,
        },
        {
          .name = "tracks",
          .description = "tracks",
          .types = "{string, }",
          .mandatory = FALSE,
        },
        {NULL}
      }, "Set activness of a layer (on optional tracks).",
        GST_VALIDATE_ACTION_TYPE_NONE);

  gst_validate_register_action_type ("set-ges-properties", "ges", set_or_check_properties,
      (GstValidateActionParameter []) {
        {
          .name = "element-name",
          .description = "The name of the element on which to set properties",
          .types = "string",
          .mandatory = TRUE,
        },
        {NULL}
      }, "Set `element-name` properties values defined by the"
         " fields in the following format: `property_name=expected-value`",
        GST_VALIDATE_ACTION_TYPE_NONE);

  gst_validate_register_action_type ("check-ges-properties", "ges", set_or_check_properties,
      (GstValidateActionParameter []) {
        {
          .name = "element-name",
          .description = "The name of the element on which to check properties",
          .types = "string",
          .mandatory = TRUE,
        },
        {NULL}
      }, "Check `element-name` properties values defined by the"
         " fields in the following format: `property_name=expected-value`",
        GST_VALIDATE_ACTION_TYPE_NONE);

  gst_validate_register_action_type ("check-child-properties", "ges", set_or_check_properties,
      (GstValidateActionParameter []) {
        {
          .name = "element-name",
          .description = "The name of the element on which to check children properties",
          .types = "string",
          .mandatory = TRUE,
        },
        {
          .name = "at-time",
          .description = "The time at which to check the values, taking into"
            " account the ControlBinding if any set.",
          .types = "string",
          .mandatory = FALSE,
        },
        {NULL}
      }, "Check `element-name` children properties values defined by the"
         " fields in the following format: `property_name=expected-value`",
        GST_VALIDATE_ACTION_TYPE_NONE);

  gst_validate_register_action_type ("set-child-properties", "ges", set_or_check_properties,
      (GstValidateActionParameter []) {
        {
          .name = "element-name",
          .description = "The name of the element on which to modify child properties",
          .types = "string",
          .mandatory = TRUE,
        },
        {NULL}
      }, "Sets `element-name` children properties values defined by the"
         " fields in the following format: `property-name=new-value`",
        GST_VALIDATE_ACTION_TYPE_NONE);

  gst_validate_register_action_type ("split-clip", "ges", _split_clip,
      (GstValidateActionParameter []) {
        {
          .name = "clip-name",
          .description = "The name of the clip to split",
          .types = "string",
          .mandatory = TRUE,
        },
        {
          .name = "position",
          .description = "The position at which to split the clip",
          .types = "double or string",
          .mandatory = TRUE,
        },
        {
          .name = "project-uri",
          .description = "The project URI with the serialized timeline to execute the action on",
          .types = "string",
          .mandatory = FALSE,
        },
        {NULL}
      }, "Split a clip at a specified position.", GST_VALIDATE_ACTION_TYPE_NONE);

  gst_validate_register_action_type ("set-track-restriction-caps", "ges", _set_track_restriction_caps,
      (GstValidateActionParameter []) {
        {
          .name = "track-type",
          .description = "The type of track to set restriction caps on",
          .types = "string",
          .mandatory = TRUE,
        },
        {
          .name = "caps",
          .description = "The caps to set on the track",
          .types = "string",
          .mandatory = TRUE,
        },
        {
          .name = "project-uri",
          .description = "The project URI with the serialized timeline to execute the action on",
          .types = "string",
          .mandatory = FALSE,
        },
        {NULL}
      }, "Sets restriction caps on tracks of a specific type.", GST_VALIDATE_ACTION_TYPE_NONE);

  gst_validate_register_action_type ("element-set-asset", "ges", _set_asset_on_element,
      (GstValidateActionParameter []) {
        {
          .name = "element-name",
          .description = "The name of the TimelineElement to set an asset on",
          .types = "string",
          .mandatory = TRUE,
        },
        {
          .name = "asset-id",
          .description = "The id of the asset from which to extract the clip",
          .types = "string",
          .mandatory = TRUE,
        },
        {
          .name = "project-uri",
          .description = "The project URI with the serialized timeline to execute the action on",
          .types = "string",
          .mandatory = FALSE,
        },
        {NULL}
      }, "Sets restriction caps on tracks of a specific type.", GST_VALIDATE_ACTION_TYPE_NONE);


  gst_validate_register_action_type ("container-add-child", "ges", _validate_action_execute,
      (GstValidateActionParameter []) {
        {
          .name = "container-name",
          .description = "The name of the GESContainer to add a child to",
          .types = "string",
          .mandatory = TRUE,
        },
        {
          .name = "child-name",
          .description = "The name of the child to add to @container-name",
          .types = "string",
          .mandatory = FALSE,
          .def = "NULL"
        },
        {
          .name = "asset-id",
          .description = "The id of the asset from which to extract the child",
          .types = "string",
          .mandatory = TRUE,
          .def = "NULL"
        },
        {
          .name = "child-type",
          .description = "The type of the child to create",
          .types = "string",
          .mandatory = FALSE,
          .def = "NULL"
        },
        {
          .name = "project-uri",
          .description = "The project URI with the serialized timeline to execute the action on",
          .types = "string",
          .mandatory = FALSE,
        },
        {NULL}
      }, "Add a child to @container-name. If asset-id and child-type are specified,"
       " the child will be created and added. Otherwise @child-name has to be specified"
       " and will be added to the container.", GST_VALIDATE_ACTION_TYPE_NONE);

  gst_validate_register_action_type ("container-remove-child", "ges", _container_remove_child,
      (GstValidateActionParameter []) {
        {
          .name = "container-name",
          .description = "The name of the GESContainer to remove a child from",
          .types = "string",
          .mandatory = TRUE,
        },
        {
          .name = "child-name",
          .description = "The name of the child to reomve from @container-name",
          .types = "string",
          .mandatory = TRUE,
        },
        {
          .name = "project-uri",
          .description = "The project URI with the serialized timeline to execute the action on",
          .types = "string",
          .mandatory = FALSE,
        },
        {NULL}
      }, "Remove a child from @container-name.", FALSE);

  gst_validate_register_action_type ("ungroup-container", "ges", _ungroup,
      (GstValidateActionParameter []) {
        {
          .name = "container-name",
          .description = "The name of the GESContainer to ungroup children from",
          .types = "string",
          .mandatory = TRUE,
        },
        {
          .name = "recursive",
          .description = "Whether to recurse ungrouping or not.",
          .types = "boolean",
          .mandatory = FALSE,
        },
        {
          .name = "project-uri",
          .description = "The project URI with the serialized timeline to execute the action on",
          .types = "string",
          .mandatory = FALSE,
        },
        {NULL}
      }, "Ungroup children of @container-name.", FALSE);

  gst_validate_register_action_type ("set-control-source", "ges", _validate_action_execute,
      (GstValidateActionParameter []) {
        {
          .name = "element-name",
          .description = "The name of the GESTrackElement to set the control source on",
          .types = "string",
          .mandatory = TRUE,
        },
        {
          .name = "property-name",
          .description = "The name of the property for which to set a control source",
          .types = "string",
          .mandatory = TRUE,
        },
        {
          .name = "binding-type",
          .description = "The name of the type of binding to use",
          .types = "string",
          .mandatory = FALSE,
          .def = "direct",
        },
        {
          .name = "source-type",
          .description = "The name of the type of ControlSource to use",
          .types = "string",
          .mandatory = FALSE,
          .def = "interpolation",
        },
        {
          .name = "interpolation-mode",
          .description = "The name of the GstInterpolationMode to on the source",
          .types = "string",
          .mandatory = FALSE,
          .def = "linear",
        },
        {
          .name = "project-uri",
          .description = "The project URI with the serialized timeline to execute the action on",
          .types = "string",
          .mandatory = FALSE,
        },
        {NULL}
      }, "Adds a GstControlSource on @element-name::@property-name"
         " allowing you to then add keyframes on that property.", GST_VALIDATE_ACTION_TYPE_NONE);

  gst_validate_register_action_type ("add-keyframe", "ges", _validate_action_execute,
      (GstValidateActionParameter []) {
        {
          .name = "element-name",
          .description = "The name of the GESTrackElement to add a keyframe on",
          .types = "string",
          .mandatory = TRUE,
        },
        {
          .name = "property-name",
          .description = "The name of the property for which to add a keyframe on",
          .types = "string",
          .mandatory = TRUE,
        },
        {
          .name = "timestamp",
          .description = "The timestamp of the keyframe",
          .types = "string or float",
          .mandatory = TRUE,
        },
        {
          .name = "value",
          .description = "The value of the keyframe",
          .types = "float",
          .mandatory = TRUE,
        },
        {
          .name = "project-uri",
          .description = "The project URI with the serialized timeline to execute the action on",
          .types = "string",
          .mandatory = FALSE,
        },
        {NULL}
      }, "Set a keyframe on @element-name:property-name.", GST_VALIDATE_ACTION_TYPE_NONE);

  gst_validate_register_action_type ("copy-element", "ges", _copy_element,
      (GstValidateActionParameter []) {
        {
          .name = "element-name",
          .description = "The name of the GESTtimelineElement to copy",
          .types = "string",
          .mandatory = TRUE,
        },
        {
          .name = "recurse",
          .description = "Copy recursively or not",
          .types = "boolean",
          .def = "true",
          .mandatory = FALSE,
        },
        {
          .name = "position",
          .description = "The time where to paste the element",
          .types = "string or float",
          .mandatory = TRUE,
        },
        {
          .name = "paste-name",
          .description = "The name of the copied element",
          .types = "string",
          .mandatory = FALSE,
        },
        {
          .name = "project-uri",
          .description = "The project URI with the serialized timeline to execute the action on",
          .types = "string",
          .mandatory = FALSE,
        },
        {NULL}
      }, "Remove a child from @container-name.", GST_VALIDATE_ACTION_TYPE_NONE);

  gst_validate_register_action_type ("remove-keyframe", "ges", _validate_action_execute,
      (GstValidateActionParameter []) {
        {
          .name = "element-name",
          .description = "The name of the GESTrackElement to add a keyframe on",
          .types = "string",
          .mandatory = TRUE,
        },
        {
          .name = "property-name",
          .description = "The name of the property for which to add a keyframe on",
          .types = "string",
          .mandatory = TRUE,
        },
        {
          .name = "timestamp",
          .description = "The timestamp of the keyframe",
          .types = "string or float",
          .mandatory = TRUE,
        },
        {
          .name = "project-uri",
          .description = "The project URI with the serialized timeline to execute the action on",
          .types = "string",
          .mandatory = FALSE,
        },
        {NULL}
      }, "Remove a keyframe on @element-name:property-name.", GST_VALIDATE_ACTION_TYPE_NONE);

  gst_validate_register_action_type ("load-project", "ges", _load_project,
      (GstValidateActionParameter [])  {
        {
          .name = "serialized-content",
          .description = "The full content of the XML describing project in XGES format.",
          .mandatory = FALSE,
          .types = "string",
          NULL
        },
        {
          .name = "uri",
          .description = "The uri of the project to load (used only if serialized-content is not provided)",
          .mandatory = FALSE,
          .types = "string",
          NULL
        },
        {NULL}
      },
      "Loads a project either from its content passed in the 'serialized-content' field or using the provided 'uri'.\n"
      "Note that it will completely clean the previous timeline",
      GST_VALIDATE_ACTION_TYPE_NONE);


  gst_validate_register_action_type ("commit", "ges", _commit, NULL,
       "Commit the timeline.", GST_VALIDATE_ACTION_TYPE_ASYNC);
  /*  *INDENT-ON* */

  return TRUE;
#else
  return FALSE;
#endif
}
