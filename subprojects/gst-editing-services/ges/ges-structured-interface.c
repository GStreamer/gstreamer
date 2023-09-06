/* GStreamer Editing Services
 *
 * Copyright (C) <2015> Thibault Saunier <tsaunier@gnome.org>
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

#include "ges-structured-interface.h"
#include "ges-internal.h"

#include <string.h>


#define LAST_CONTAINER_QDATA g_quark_from_string("ges-structured-last-container")
#define LAST_CHILD_QDATA g_quark_from_string("ges-structured-last-child")

#ifdef G_HAVE_ISO_VARARGS
#define REPORT_UNLESS(condition, errpoint, ...)                                \
  G_STMT_START {                                                               \
    if (!(condition)) {                                                        \
      gchar *tmp = gst_info_strdup_printf(__VA_ARGS__);                            \
      *error = g_error_new_literal (GES_ERROR, 0, tmp);                        \
      g_free (tmp);                                                            \
      goto errpoint;                                                           \
    }                                                                          \
  }                                                                            \
  G_STMT_END
#else /* G_HAVE_GNUC_VARARGS */
#ifdef G_HAVE_GNUC_VARARGS
#define REPORT_UNLESS(condition, errpoint, args...)                            \
  G_STMT_START {                                                               \
    if (!(condition)) {                                                        \
      gchar *tmp = gst_info_strdup_printf(##args);                            \
      *error = g_error_new_literal (GES_ERROR, 0, tmp);                        \
      g_free (tmp);                                                            \
      goto errpoint;                                                           \
    }                                                                          \
  }                                                                            \
  G_STMT_END
#endif /* G_HAVE_ISO_VARARGS */
#endif /* G_HAVE_GNUC_VARARGS */

#define GET_AND_CHECK(name,type,var,label) G_STMT_START {\
  gboolean found = FALSE; \
\
  if (type == GST_TYPE_CLOCK_TIME) {\
    found = ges_util_structure_get_clocktime (structure,name, (GstClockTime*)var,NULL);\
  }\
  else { \
    found = gst_structure_get (structure, name, type, var, NULL); \
  }\
  if (!found) {\
    gchar *struct_str = gst_structure_to_string (structure); \
    *error = g_error_new (GES_ERROR, 0, \
        "Could not get the mandatory field '%s'" \
        " of type %s - fields in %s", name, g_type_name (type), struct_str); \
    g_free (struct_str); \
    goto label;\
  } \
} G_STMT_END

#define TRY_GET_STRING(name,var,def) G_STMT_START {\
  *var = gst_structure_get_string (structure, name); \
  if (*var == NULL) \
    *var = def; \
} G_STMT_END

#define TRY_GET_TIME(name, var, var_frames, def) G_STMT_START  {       \
  if (!ges_util_structure_get_clocktime (structure, name, var, var_frames)) { \
      *var = def;                                          \
      *var_frames = GES_FRAME_NUMBER_NONE;                            \
  }                                                        \
} G_STMT_END

static gboolean
_get_structure_value (GstStructure * structure, const gchar * field, GType type,
    gpointer v)
{
  gboolean res = TRUE;
  const gchar *value_str;
  const GValue *value;
  GValue nvalue = G_VALUE_INIT;

  if (gst_structure_get (structure, field, type, v, NULL))
    return res;

  g_value_init (&nvalue, type);
  value = gst_structure_get_value (structure, field);
  if (!value)
    goto fail;

  if (g_value_transform (value, &nvalue))
    goto set_and_get_value;

  if (!G_VALUE_HOLDS_STRING (value))
    goto fail;

  value_str = g_value_get_string (value);
  if (!gst_value_deserialize (&nvalue, value_str))
    goto done;

set_and_get_value:
  gst_structure_set_value (structure, field, &nvalue);
  gst_structure_get (structure, field, type, v, NULL);

done:
  g_value_reset (&nvalue);
  return res;

fail:
  res = FALSE;
  goto done;
}

#define TRY_GET(name, type, var, def) G_STMT_START {\
  g_assert (type != GST_TYPE_CLOCK_TIME);                      \
  if (!_get_structure_value (structure, name, type, var))\
    *var = def;                                             \
} G_STMT_END

typedef struct
{
  const gchar **fields;
  GList *invalid_fields;
} FieldsError;

static gboolean
enum_from_str (GType type, const gchar * str_enum, guint * enum_value)
{
  GValue value = G_VALUE_INIT;
  g_value_init (&value, type);

  if (!gst_value_deserialize (&value, str_enum))
    return FALSE;

  *enum_value = g_value_get_enum (&value);
  g_value_unset (&value);

  return TRUE;
}

static gboolean
_check_field (GQuark field_id, const GValue * value, FieldsError * fields_error)
{
  guint i;
  const gchar *field = g_quark_to_string (field_id);

  for (i = 0; fields_error->fields[i]; i++) {
    if (g_strcmp0 (fields_error->fields[i], field) == 0) {

      return TRUE;
    }
  }

  fields_error->invalid_fields =
      g_list_append (fields_error->invalid_fields, (gpointer) field);

  return TRUE;
}

static gboolean
_check_fields (GstStructure * structure, FieldsError fields_error,
    GError ** error)
{
  gst_structure_foreach (structure,
      (GstStructureForeachFunc) _check_field, &fields_error);

  if (fields_error.invalid_fields) {
    GList *tmp;
    const gchar *struct_name = gst_structure_get_name (structure);
    GString *msg = g_string_new (NULL);

    g_string_append_printf (msg, "Unknown propert%s in %s%s:",
        g_list_length (fields_error.invalid_fields) > 1 ? "ies" : "y",
        strlen (struct_name) > 1 ? "--" : "-",
        gst_structure_get_name (structure));

    for (tmp = fields_error.invalid_fields; tmp; tmp = tmp->next)
      g_string_append_printf (msg, " %s", (gchar *) tmp->data);

    if (error)
      *error = g_error_new_literal (GES_ERROR, 0, msg->str);

    g_string_free (msg, TRUE);

    return FALSE;
  }

  return TRUE;
}

static GESTimelineElement *
find_element_for_property (GESTimeline * timeline, GstStructure * structure,
    gchar ** property_name, gboolean require_track_element, GError ** error)
{
  GList *tmp;
  const gchar *element_name;
  GESTimelineElement *element;

  element_name = gst_structure_get_string (structure, "element-name");
  if (element_name == NULL) {
    element = g_object_get_qdata (G_OBJECT (timeline), LAST_CHILD_QDATA);
    if (element)
      gst_object_ref (element);
  } else {
    element = ges_timeline_get_element (timeline, element_name);
  }

  if (*property_name == NULL) {
    gchar *tmpstr = *property_name;
    const gchar *name = gst_structure_get_name (structure);

    REPORT_UNLESS (g_str_has_prefix (name, "set-"), err,
        "Could not find any property name in %" GST_PTR_FORMAT, structure);

    *property_name = g_strdup (&tmpstr[4]);

    g_free (tmpstr);
  }

  if (element) {
    if (!ges_timeline_element_lookup_child (element,
            *property_name, NULL, NULL)) {
      gst_clear_object (&element);
    }
  }

  if (!element) {
    element = g_object_get_qdata (G_OBJECT (timeline), LAST_CONTAINER_QDATA);
    if (element)
      gst_object_ref (element);
  }

  REPORT_UNLESS (GES_IS_TIMELINE_ELEMENT (element), err,
      "Could not find child %s from %" GST_PTR_FORMAT, element_name, structure);

  if (!require_track_element || GES_IS_TRACK_ELEMENT (element))
    return element;


  REPORT_UNLESS (GES_IS_CONTAINER (element), err,
      "Could not find child %s from %" GST_PTR_FORMAT, element_name, structure);

  for (tmp = GES_CONTAINER_CHILDREN (element); tmp; tmp = tmp->next) {
    if (ges_timeline_element_lookup_child (tmp->data, *property_name, NULL,
            NULL)) {
      gst_object_replace ((GstObject **) & element, tmp->data);

      break;
    }
  }

  REPORT_UNLESS (GES_IS_TRACK_ELEMENT (element), err,
      "Could not find TrackElement from %" GST_PTR_FORMAT, structure);

  return element;

err:
  g_clear_object (&element);
  return element;
}

gboolean
_ges_save_timeline_if_needed (GESTimeline * timeline, GstStructure * structure,
    GError ** error)
{
  gboolean res = TRUE;
  const gchar *nested_timeline_id =
      gst_structure_get_string (structure, "project-uri");

  if (nested_timeline_id) {
    res = ges_timeline_save_to_uri (timeline, nested_timeline_id, NULL, TRUE,
        error);
  }

  return res;
}

typedef struct
{
  GstTimedValueControlSource *source;
  GstStructure *structure;
  GError *error;
  const gchar *property_name;
  gboolean res;
} SetKeyframesData;


typedef struct
{
  gboolean ok;
  union
  {
    struct
    {
      gdouble v;
    } Ok;

    struct
    {
      const gchar *err;
    } Err;
  };
} ValueToDoubleRes;

static ValueToDoubleRes
value_to_double (const GValue * v)
{
  GValue v2 = G_VALUE_INIT;
  ValueToDoubleRes res = { 0, };

  if (G_VALUE_HOLDS_STRING (v)) {
    errno = 0;
    res.Ok.v = g_ascii_strtod (g_value_get_string (v), NULL);

    if (errno)
      res.Err.err = g_strerror (errno);
    else
      res.ok = TRUE;

    return res;
  }

  g_value_init (&v2, G_TYPE_DOUBLE);
  res.ok = g_value_transform (v, &v2);
  if (res.ok) {
    res.Ok.v = g_value_get_double (&v2);
  } else {
    res.Err.err = "unsupported conversion";
  }
  g_value_reset (&v2);

  return res;
}

static gboolean
un_set_keyframes_foreach (GQuark field_id, const GValue * value,
    SetKeyframesData * d)
{
  GError **error = &d->error;
  gchar *tmp;
  gint i;
  const gchar *valid_fields[] = {
    "element-name", "property-name", "value", "timestamp", "project-uri",
    "binding-type", "source-type", "interpolation-mode", "interpolation-mode",
    NULL
  };
  const gchar *field = g_quark_to_string (field_id);
  gdouble ts;

  for (i = 0; valid_fields[i]; i++) {
    if (g_quark_from_string (valid_fields[i]) == field_id)
      return TRUE;
  }

  errno = 0;
  ts = g_strtod (field, &tmp);

  REPORT_UNLESS (errno == 0 && field != tmp, err,
      "Could not convert `%s` to GstClockTime (%s)", field, g_strerror (errno));

  if (gst_structure_has_name (d->structure, "remove-keyframe")) {
    REPORT_UNLESS (gst_timed_value_control_source_unset (d->source,
            ts * GST_SECOND), err, "Could not unset keyframe at %f", ts);

    return TRUE;
  }

  ValueToDoubleRes res = value_to_double (value);
  REPORT_UNLESS (res.ok, err,
      "Could not convert keyframe %f value (%s)%s to double (%s)", ts,
      G_VALUE_TYPE_NAME (value), gst_value_serialize (value), res.Err.err);

  REPORT_UNLESS (gst_timed_value_control_source_set (d->source, ts * GST_SECOND,
          res.Ok.v), err, "Could not set keyframe %f=%f", ts, res.Ok.v);

  return TRUE;

err:
  d->res = FALSE;
  return FALSE;
}


gboolean
_ges_add_remove_keyframe_from_struct (GESTimeline * timeline,
    GstStructure * structure, GError ** error)
{
  GESTimelineElement *element = NULL;

  gboolean absolute;
  gdouble value;
  GstClockTime timestamp;
  GstControlBinding *binding = NULL;
  GstTimedValueControlSource *source = NULL;
  gchar *property_name = NULL;

  gboolean ret = FALSE;
  gboolean setting_value;

  const gchar *valid_fields[] =
      { "element-name", "property-name", "value", "timestamp", "project-uri",
    NULL
  };

  FieldsError fields_error = { valid_fields, NULL };

  if (gst_structure_has_field (structure, "value")) {
    if (!_check_fields (structure, fields_error, error))
      return FALSE;
    GET_AND_CHECK ("timestamp", GST_TYPE_CLOCK_TIME, &timestamp, done);
  } else {
    REPORT_UNLESS (!gst_structure_has_field (structure, "timestamp"), done,
        "Doesn't have a `value` field in %" GST_PTR_FORMAT
        " but has a `timestamp`" " that can't work!", structure);
  }

  GET_AND_CHECK ("property-name", G_TYPE_STRING, &property_name, done);
  if (!(element =
          find_element_for_property (timeline, structure, &property_name, TRUE,
              error)))
    goto done;

  REPORT_UNLESS (binding =
      ges_track_element_get_control_binding (GES_TRACK_ELEMENT (element),
          property_name),
      done, "No control binding found for %" GST_PTR_FORMAT, structure);

  g_object_get (binding, "control-source", &source, NULL);
  REPORT_UNLESS (source, done,
      "No control source found for '%" GST_PTR_FORMAT
      "' you should first set-control-binding on it", structure);
  REPORT_UNLESS (GST_IS_TIMED_VALUE_CONTROL_SOURCE (source), done,
      "You can use add-keyframe"
      " only on GstTimedValueControlSource not %s",
      G_OBJECT_TYPE_NAME (source));

  if (!gst_structure_has_field (structure, "value")) {
    SetKeyframesData d = {
      source, structure, NULL, property_name, TRUE,
    };
    gst_structure_foreach (structure,
        (GstStructureForeachFunc) un_set_keyframes_foreach, &d);
    if (!d.res)
      g_propagate_error (error, d.error);

    return d.res;
  }

  g_object_get (binding, "absolute", &absolute, NULL);
  if (absolute) {
    GParamSpec *pspec;
    const GValue *v;

    if (!ges_timeline_element_lookup_child (element, property_name, NULL,
            &pspec)) {
      *error =
          g_error_new (GES_ERROR, 0, "Could not get property %s for %s",
          property_name, GES_TIMELINE_ELEMENT_NAME (element));
      goto done;
    }

    v = gst_structure_get_value (structure, "value");
    if (!v) {
      gchar *struct_str = gst_structure_to_string (structure);

      *error = g_error_new (GES_ERROR, 0,
          "Could not get the mandatory field 'value'"
          " of type %s - fields in %s", g_type_name (pspec->value_type),
          struct_str);
      g_free (struct_str);
      goto done;
    }

    ValueToDoubleRes res = value_to_double (v);
    if (!res.ok) {
      gchar *struct_str = gst_structure_to_string (structure);

      *error = g_error_new (GES_ERROR, 0,
          "Could not get the mandatory field 'value'"
          " of type %s - fields in %s", g_type_name (pspec->value_type),
          struct_str);
      g_free (struct_str);
      goto done;
    }

    value = res.Ok.v;
  } else {
    GET_AND_CHECK ("value", G_TYPE_DOUBLE, &value, done);
  }

  setting_value =
      !g_strcmp0 (gst_structure_get_name (structure), "add-keyframe");
  if (setting_value)
    ret = gst_timed_value_control_source_set (source, timestamp, value);
  else
    ret = gst_timed_value_control_source_unset (source, timestamp);

  if (!ret) {
    *error =
        g_error_new (GES_ERROR, 0,
        "Could not %sset value for timestamp: %" GST_TIME_FORMAT,
        setting_value ? "" : "un", GST_TIME_ARGS (timestamp));
  }
  ret = _ges_save_timeline_if_needed (timeline, structure, error);

done:
  gst_clear_object (&source);
  gst_clear_object (&element);
  g_free (property_name);

  return ret;

}

GESAsset *
_ges_get_asset_from_timeline (GESTimeline * timeline, GType type,
    const gchar * id, GError ** error)
{
  GESAsset *asset;
  GESProject *project = ges_timeline_get_project (timeline);
  GError *err = NULL;

  asset = ges_project_create_asset_sync (project, id, type, &err);

  if (err)
    g_propagate_error (error, err);
  if (!asset || (error && *error)) {

    if (error && !*error) {
      *error = g_error_new (GES_ERROR, 0,
          "There was an error requesting the asset with id %s and type %s (%s)",
          id, g_type_name (type), "unknown");
    }

    GST_ERROR
        ("There was an error requesting the asset with id %s and type %s (%s)",
        id, g_type_name (type), error ? (*error)->message : "unknown");

    return NULL;
  }

  return asset;
}

/* Unref after usage */
GESLayer *
_ges_get_layer_by_priority (GESTimeline * timeline, gint priority)
{
  GList *layers;
  gint nlayers;
  GESLayer *layer = NULL;

  priority = MAX (priority, 0);
  layers = ges_timeline_get_layers (timeline);
  nlayers = (gint) g_list_length (layers);
  if (priority >= nlayers) {
    gint i = nlayers;

    while (i <= priority) {
      layer = ges_timeline_append_layer (timeline);

      i++;
    }

    layer = gst_object_ref (layer);

    goto done;
  }

  layer = ges_timeline_get_layer (timeline, priority);

done:
  g_list_free_full (layers, gst_object_unref);

  return layer;
}

static gchar *
ensure_uri (gchar * location)
{
  if (gst_uri_is_valid (location))
    return g_strdup (location);
  else
    return gst_filename_to_uri (location, NULL);
}

static gboolean
get_flags_from_string (GType type, const gchar * str_flags, guint * flags)
{
  GValue value = G_VALUE_INIT;
  g_value_init (&value, type);

  if (!gst_value_deserialize (&value, str_flags)) {
    g_value_unset (&value);

    return FALSE;
  }

  *flags = g_value_get_flags (&value);
  g_value_unset (&value);

  return TRUE;
}

gboolean
_ges_add_clip_from_struct (GESTimeline * timeline, GstStructure * structure,
    GError ** error)
{
  GESAsset *asset = NULL;
  GESLayer *layer = NULL;
  GESClip *clip;
  gint layer_priority;
  const gchar *name;
  const gchar *text;
  const gchar *pattern;
  const gchar *track_types_str;
  const gchar *nested_timeline_id;
  gchar *asset_id = NULL;
  gchar *check_asset_id = NULL;
  const gchar *type_string;
  GType type;
  gboolean res = FALSE;
  GESTrackType track_types = GES_TRACK_TYPE_UNKNOWN;

  GESFrameNumber start_frame = GES_FRAME_NUMBER_NONE, inpoint_frame =
      GES_FRAME_NUMBER_NONE, duration_frame = GES_FRAME_NUMBER_NONE;
  GstClockTime duration = 1 * GST_SECOND, inpoint = 0, start =
      GST_CLOCK_TIME_NONE;

  const gchar *valid_fields[] =
      { "asset-id", "pattern", "name", "layer-priority", "layer", "type",
    "start", "inpoint", "duration", "text", "track-types", "project-uri",
    NULL
  };

  FieldsError fields_error = { valid_fields, NULL };

  if (!_check_fields (structure, fields_error, error))
    return FALSE;

  GET_AND_CHECK ("asset-id", G_TYPE_STRING, &check_asset_id, beach);

  TRY_GET_STRING ("pattern", &pattern, NULL);
  TRY_GET_STRING ("text", &text, NULL);
  TRY_GET_STRING ("name", &name, NULL);
  TRY_GET ("layer-priority", G_TYPE_INT, &layer_priority, -1);
  if (layer_priority == -1)
    TRY_GET ("layer", G_TYPE_INT, &layer_priority, -1);
  TRY_GET_STRING ("type", &type_string, "GESUriClip");
  TRY_GET_TIME ("start", &start, &start_frame, GST_CLOCK_TIME_NONE);
  TRY_GET_TIME ("inpoint", &inpoint, &inpoint_frame, 0);
  TRY_GET_TIME ("duration", &duration, &duration_frame, GST_CLOCK_TIME_NONE);
  TRY_GET_STRING ("track-types", &track_types_str, NULL);
  TRY_GET_STRING ("project-uri", &nested_timeline_id, NULL);

  if (track_types_str) {
    if (!get_flags_from_string (GES_TYPE_TRACK_TYPE, track_types_str,
            &track_types)) {
      *error =
          g_error_new (GES_ERROR, 0, "Invalid track types: %s",
          track_types_str);
    }

  }

  if (!(type = g_type_from_name (type_string))) {
    *error = g_error_new (GES_ERROR, 0, "This type doesn't exist : %s",
        type_string);

    goto beach;
  }

  if (type == GES_TYPE_URI_CLIP) {
    asset_id = ensure_uri (check_asset_id);
  } else {
    asset_id = g_strdup (check_asset_id);
  }

  gst_structure_set (structure, "asset-id", G_TYPE_STRING, asset_id, NULL);
  asset = _ges_get_asset_from_timeline (timeline, type, asset_id, error);
  if (!asset) {
    res = FALSE;

    goto beach;
  }

  if (layer_priority == -1) {
    GESContainer *container;

    container = g_object_get_qdata (G_OBJECT (timeline), LAST_CONTAINER_QDATA);
    if (!container || !GES_IS_CLIP (container))
      layer = _ges_get_layer_by_priority (timeline, 0);
    else
      layer = ges_clip_get_layer (GES_CLIP (container));

    if (!layer)
      layer = _ges_get_layer_by_priority (timeline, 0);
  } else {
    layer = _ges_get_layer_by_priority (timeline, layer_priority);
  }

  if (!layer) {
    *error =
        g_error_new (GES_ERROR, 0, "No layer with priority %d", layer_priority);
    goto beach;
  }

  if (GES_FRAME_NUMBER_IS_VALID (start_frame))
    start = ges_timeline_get_frame_time (timeline, start_frame);

  if (GES_FRAME_NUMBER_IS_VALID (inpoint_frame)) {
    inpoint =
        ges_clip_asset_get_frame_time (GES_CLIP_ASSET (asset), inpoint_frame);
    if (!GST_CLOCK_TIME_IS_VALID (inpoint)) {
      *error =
          g_error_new (GES_ERROR, 0, "Could not get inpoint from frame %"
          G_GINT64_FORMAT, inpoint_frame);
      goto beach;
    }
  }

  if (GES_FRAME_NUMBER_IS_VALID (duration_frame)) {
    duration = ges_timeline_get_frame_time (timeline, duration_frame);
  }

  if (GES_IS_URI_CLIP_ASSET (asset) && !GST_CLOCK_TIME_IS_VALID (duration)) {
    duration = GST_CLOCK_DIFF (inpoint,
        ges_uri_clip_asset_get_duration (GES_URI_CLIP_ASSET (asset)));
  }

  clip = ges_layer_add_asset (layer, asset, start, inpoint, duration,
      track_types);

  if (clip) {
    res = TRUE;

    if (GES_TIMELINE_ELEMENT_DURATION (clip) == 0) {
      *error = g_error_new (GES_ERROR, 0,
          "Clip %s has 0 as duration, please provide a proper duration",
          asset_id);
      res = FALSE;
      goto beach;
    }


    if (GES_IS_TEST_CLIP (clip)) {
      if (pattern) {
        guint v;

        REPORT_UNLESS (enum_from_str (GES_VIDEO_TEST_PATTERN_TYPE, pattern, &v),
            beach, "Invalid pattern: %s", pattern);
        ges_test_clip_set_vpattern (GES_TEST_CLIP (clip), v);
      }
    }

    if (GES_IS_TITLE_CLIP (clip) && text)
      ges_timeline_element_set_child_properties (GES_TIMELINE_ELEMENT (clip),
          "text", text, NULL);

    if (name
        && !ges_timeline_element_set_name (GES_TIMELINE_ELEMENT (clip), name)) {
      res = FALSE;
      *error =
          g_error_new (GES_ERROR, 0, "couldn't set name %s on clip with id %s",
          name, asset_id);
    }
  } else {
    *error =
        g_error_new (GES_ERROR, 0,
        "Couldn't add clip with id %s to layer with priority %d", asset_id,
        layer_priority);
    res = FALSE;
    goto beach;
  }

  if (res) {
    g_object_set_qdata (G_OBJECT (timeline), LAST_CONTAINER_QDATA, clip);
    g_object_set_qdata (G_OBJECT (timeline), LAST_CHILD_QDATA, NULL);
  }

  res = _ges_save_timeline_if_needed (timeline, structure, error);

beach:
  gst_clear_object (&layer);
  gst_clear_object (&asset);
  g_free (asset_id);
  g_free (check_asset_id);
  return res;
}

gboolean
_ges_add_track_from_struct (GESTimeline * timeline,
    GstStructure * structure, GError ** error)
{
  const gchar *ttype;
  GESTrack *track;
  GstCaps *caps;

  const gchar *valid_fields[] = { "type", "restrictions", NULL };

  FieldsError fields_error = { valid_fields, NULL };

  if (!_check_fields (structure, fields_error, error))
    return FALSE;

  ttype = gst_structure_get_string (structure, "type");
  if (!g_strcmp0 (ttype, "video")) {
    track = GES_TRACK (ges_video_track_new ());
  } else if (!g_strcmp0 (ttype, "audio")) {
    track = GES_TRACK (ges_audio_track_new ());
  } else {
    g_set_error (error, GES_ERROR, 0, "Unhandled track type: `%s`", ttype);

    return FALSE;
  }

  if (gst_structure_has_field (structure, "restrictions")) {
    GstStructure *restriction_struct;
    gchar *restriction_str;

    if (gst_structure_get (structure, "restrictions", GST_TYPE_STRUCTURE,
            &restriction_struct, NULL)) {
      caps = gst_caps_new_full (restriction_struct, NULL);
    } else if (gst_structure_get (structure, "restrictions", G_TYPE_STRING,
            &restriction_str, NULL)) {
      caps = gst_caps_from_string (restriction_str);

      if (!caps) {
        g_set_error (error, GES_ERROR, 0, "Invalid restrictions caps: %s",
            restriction_str);

        g_free (restriction_str);
        return FALSE;
      }
      g_free (restriction_str);
    } else if (!gst_structure_get (structure, "restrictions", GST_TYPE_CAPS,
            &caps, NULL)) {
      gchar *tmp = gst_structure_to_string (structure);

      g_set_error (error, GES_ERROR, 0, "Can't use restrictions caps from %s",
          tmp);

      g_object_unref (track);
      return FALSE;
    }

    ges_track_set_restriction_caps (track, caps);
    gst_caps_unref (caps);
  }

  return ges_timeline_add_track (timeline, track);
}

gboolean
_ges_container_add_child_from_struct (GESTimeline * timeline,
    GstStructure * structure, GError ** error)
{
  GESAsset *asset = NULL;
  GESContainer *container;
  GESTimelineElement *child = NULL;
  const gchar *container_name, *child_name, *child_type, *id;

  gboolean res = TRUE;
  const gchar *valid_fields[] = { "container-name", "asset-id", "inpoint",
    "child-type", "child-name", "project-uri", NULL
  };

  FieldsError fields_error = { valid_fields, NULL };

  if (!_check_fields (structure, fields_error, error))
    return FALSE;

  container_name = gst_structure_get_string (structure, "container-name");

  if (container_name == NULL) {
    container = g_object_get_qdata (G_OBJECT (timeline), LAST_CONTAINER_QDATA);
  } else {
    container =
        GES_CONTAINER (ges_timeline_get_element (timeline, container_name));
  }

  if (!GES_IS_CONTAINER (container)) {
    *error =
        g_error_new (GES_ERROR, 0, "Could not find container: %s (%p)",
        container_name, container);

    res = FALSE;
    goto beach;
  }

  id = gst_structure_get_string (structure, "asset-id");
  child_type = gst_structure_get_string (structure, "child-type");

  if (id && child_type) {
    asset =
        _ges_get_asset_from_timeline (timeline, g_type_from_name (child_type),
        id, error);

    if (asset == NULL) {
      res = FALSE;
      goto beach;
    }

    child = GES_TIMELINE_ELEMENT (ges_asset_extract (asset, NULL));
    if (!GES_IS_TIMELINE_ELEMENT (child)) {
      *error = g_error_new (GES_ERROR, 0, "Could not extract child element");

      goto beach;
    }
  }

  child_name = gst_structure_get_string (structure, "child-name");
  if (!child && child_name) {
    child = ges_timeline_get_element (timeline, child_name);
    if (!GES_IS_TIMELINE_ELEMENT (child)) {
      *error = g_error_new (GES_ERROR, 0, "Could not find child element");

      goto beach;
    }
  }

  if (!child) {
    *error =
        g_error_new (GES_ERROR, 0, "Wrong parameters, could not get a child");

    return FALSE;
  }

  if (child_name)
    ges_timeline_element_set_name (child, child_name);
  else
    child_name = GES_TIMELINE_ELEMENT_NAME (child);

  if (gst_structure_has_field (structure, "inpoint")) {
    GstClockTime inpoint;
    GESFrameNumber finpoint;

    if (!GES_IS_TRACK_ELEMENT (child)) {
      *error = g_error_new (GES_ERROR, 0, "Child %s is not a trackelement"
          ", can't set inpoint.", child_name);

      gst_object_unref (child);

      goto beach;
    }

    if (!ges_util_structure_get_clocktime (structure, "inpoint", &inpoint,
            &finpoint)) {
      *error = g_error_new (GES_ERROR, 0, "Could not use inpoint.");
      gst_object_unref (child);

      goto beach;
    }

    if (!ges_track_element_set_has_internal_source (GES_TRACK_ELEMENT (child),
            TRUE)) {
      *error =
          g_error_new (GES_ERROR, 0,
          "Could not set inpoint as %s can't have an internal source",
          child_name);
      gst_object_unref (child);

      goto beach;
    }

    if (GES_FRAME_NUMBER_IS_VALID (finpoint))
      inpoint = ges_timeline_get_frame_time (timeline, finpoint);

    ges_timeline_element_set_inpoint (child, inpoint);

  }

  res = ges_container_add (container, child);
  if (res == FALSE) {
    g_error_new (GES_ERROR, 0, "Could not add child to container");
  } else {
    g_object_set_qdata (G_OBJECT (timeline), LAST_CHILD_QDATA, child);
  }
  res = _ges_save_timeline_if_needed (timeline, structure, error);

beach:
  gst_clear_object (&asset);
  return res;
}

gboolean
_ges_set_child_property_from_struct (GESTimeline * timeline,
    GstStructure * structure, GError ** error)
{
  const GValue *value;
  GValue prop_value = G_VALUE_INIT;
  gboolean prop_value_set = FALSE;
  gchar *property_name;
  GESTimelineElement *element;
  gchar *serialized;
  gboolean res;

  const gchar *valid_fields[] =
      { "element-name", "property", "value", "project-uri", NULL };

  FieldsError fields_error = { valid_fields, NULL };

  if (!_check_fields (structure, fields_error, error))
    return FALSE;

  GET_AND_CHECK ("property", G_TYPE_STRING, &property_name, err);

  if (!(element =
          find_element_for_property (timeline, structure, &property_name, FALSE,
              error)))
    goto err;

  value = gst_structure_get_value (structure, "value");

  if (G_VALUE_TYPE (value) == G_TYPE_STRING) {
    GParamSpec *pspec;
    if (ges_timeline_element_lookup_child (element, property_name, NULL,
            &pspec)) {
      GType p_type = pspec->value_type;
      g_param_spec_unref (pspec);
      if (p_type != G_TYPE_STRING) {
        const gchar *val_string = g_value_get_string (value);
        g_value_init (&prop_value, p_type);
        if (!gst_value_deserialize (&prop_value, val_string)) {
          *error = g_error_new (GES_ERROR, 0, "Could not set the property %s "
              "because the value %s could not be deserialized to the %s type",
              property_name, val_string, g_type_name (p_type));
          return FALSE;
        }
        prop_value_set = TRUE;
      }
    }
    /* else, let the setter fail below */
  }

  if (!prop_value_set) {
    g_value_init (&prop_value, G_VALUE_TYPE (value));
    g_value_copy (value, &prop_value);
  }

  serialized = gst_value_serialize (&prop_value);
  GST_INFO_OBJECT (element, "Setting property %s to %s\n", property_name,
      serialized);
  g_free (serialized);

  res = ges_timeline_element_set_child_property (element, property_name,
      &prop_value);
  g_value_unset (&prop_value);
  if (!res) {
    guint n_specs, i;
    GParamSpec **specs =
        ges_timeline_element_list_children_properties (element, &n_specs);
    GString *errstr = g_string_new (NULL);

    g_string_append_printf (errstr,
        "\n  Could not set property `%s` on `%s`, valid properties:\n",
        property_name, GES_TIMELINE_ELEMENT_NAME (element));

    for (i = 0; i < n_specs; i++)
      g_string_append_printf (errstr, "    - %s\n", specs[i]->name);
    g_free (specs);

    *error = g_error_new_literal (GES_ERROR, 0, errstr->str);
    g_string_free (errstr, TRUE);

    return FALSE;
  }
  g_free (property_name);
  return _ges_save_timeline_if_needed (timeline, structure, error);

err:
  g_free (property_name);
  return FALSE;
}

gboolean
_ges_set_control_source_from_struct (GESTimeline * timeline,
    GstStructure * structure, GError ** error)
{
  guint mode;
  gboolean res = FALSE;
  GESTimelineElement *element = NULL;

  GstControlSource *source = NULL;
  gchar *property_name, *binding_type = NULL,
      *source_type = NULL, *interpolation_mode = NULL;

  GET_AND_CHECK ("property-name", G_TYPE_STRING, &property_name, beach);

  if (!(element =
          find_element_for_property (timeline, structure, &property_name, TRUE,
              error)))
    goto beach;

  if (GES_IS_CLIP (element)) {
    GList *tmp;
    for (tmp = GES_CONTAINER_CHILDREN (element); tmp; tmp = tmp->next) {
      if (ges_timeline_element_lookup_child (tmp->data, property_name, NULL,
              NULL)) {
        gst_object_replace ((GstObject **) & element, tmp->data);

        break;
      }
    }
  }

  REPORT_UNLESS (GES_IS_TRACK_ELEMENT (element), beach,
      "Could not find TrackElement from %" GST_PTR_FORMAT, structure);

  TRY_GET ("binding-type", G_TYPE_STRING, &binding_type, NULL);
  TRY_GET ("source-type", G_TYPE_STRING, &source_type, NULL);
  TRY_GET ("interpolation-mode", G_TYPE_STRING, &interpolation_mode, NULL);

  if (!binding_type)
    binding_type = g_strdup ("direct");

  REPORT_UNLESS (source_type == NULL
      || !g_strcmp0 (source_type, "interpolation"), beach,
      "Interpolation type %s not supported", source_type);
  source = gst_interpolation_control_source_new ();

  if (interpolation_mode)
    REPORT_UNLESS (enum_from_str (GST_TYPE_INTERPOLATION_MODE,
            interpolation_mode, &mode), beach,
        "Wrong interpolation mode: %s", interpolation_mode);
  else
    mode = GST_INTERPOLATION_MODE_LINEAR;

  g_object_set (source, "mode", mode, NULL);

  res = ges_track_element_set_control_source (GES_TRACK_ELEMENT (element),
      source, property_name, binding_type);

beach:
  gst_clear_object (&element);
  gst_clear_object (&source);
  g_free (property_name);
  g_free (binding_type);
  g_free (source_type);
  g_free (interpolation_mode);

  return res;
}

#undef GET_AND_CHECK
#undef TRY_GET
