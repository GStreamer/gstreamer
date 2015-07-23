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
#include "ges-structured-interface.h"

#include <string.h>


#define LAST_CONTAINER_QDATA g_quark_from_string("ges-structured-last-container")
#define LAST_CHILD_QDATA g_quark_from_string("ges-structured-last-child")

static gboolean
_get_clocktime (GstStructure * structure, const gchar * name, gpointer var)
{
  gboolean found = FALSE;
  GstClockTime *val = (GstClockTime *) var;

  const GValue *gvalue = gst_structure_get_value (structure, name);

  if (gvalue) {
    if (G_VALUE_TYPE (gvalue) == GST_TYPE_CLOCK_TIME) {
      *val = (GstClockTime) g_value_get_uint64 (gvalue);
      found = TRUE;
    } else if (G_VALUE_TYPE (gvalue) == G_TYPE_UINT64) {
      *val = (GstClockTime) g_value_get_uint64 (gvalue);
      found = TRUE;
    } else if (G_VALUE_TYPE (gvalue) == G_TYPE_UINT) {
      *val = (GstClockTime) g_value_get_uint (gvalue);
      found = TRUE;
    } else if (G_VALUE_TYPE (gvalue) == G_TYPE_INT) {
      *val = (GstClockTime) g_value_get_int (gvalue);
      found = TRUE;
    } else if (G_VALUE_TYPE (gvalue) == G_TYPE_INT64) {
      *val = (GstClockTime) g_value_get_int64 (gvalue);
      found = TRUE;
    } else if (G_VALUE_TYPE (gvalue) == G_TYPE_DOUBLE) {
      gdouble d = g_value_get_double (gvalue);

      found = TRUE;
      if (d == -1.0)
        *val = GST_CLOCK_TIME_NONE;
      else {
        *val = d * GST_SECOND;
        *val = GST_ROUND_UP_4 (*val);
      }
    }
  }

  return found;
}

#define GET_AND_CHECK(name,type,var,label) G_STMT_START {\
  gboolean found = FALSE; \
\
  if (type == GST_TYPE_CLOCK_TIME) {\
    found = _get_clocktime(structure,name,var);\
  }\
  else { \
    found = gst_structure_get (structure, name, type, var, NULL); \
  }\
  if (!found) {\
    gchar *struct_str = gst_structure_to_string (structure); \
    *error = g_error_new (GES_ERROR, 0, \
        "Could not get the mandatory field '%s'" \
        " fields in %s", name, struct_str); \
    g_free (struct_str); \
    goto label;\
  } \
} G_STMT_END

#define TRY_GET(name,type,var,def) G_STMT_START {\
  if (type == GST_TYPE_CLOCK_TIME) {\
    if (!_get_clocktime(structure,name,var))\
      *var = def; \
  } else if  (!gst_structure_get (structure, name, type, var, NULL)) {\
    *var = def; \
  } \
} G_STMT_END

typedef struct
{
  const gchar **fields;
  GList *invalid_fields;
} FieldsError;

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
    GST_ERROR ("%s", msg->str);

    g_string_free (msg, TRUE);

    return FALSE;
  }

  return TRUE;
}


gboolean
_ges_add_remove_keyframe_from_struct (GESTimeline * timeline,
    GstStructure * structure, GError ** error)
{
  GESTrackElement *element;

  gdouble value;
  GstClockTime timestamp;
  GstControlBinding *binding = NULL;
  GstTimedValueControlSource *source = NULL;
  gchar *element_name = NULL, *property_name = NULL;

  gboolean ret = FALSE;

  const gchar *valid_fields[] =
      { "element-name", "property-name", "value", "timestamp",
    NULL
  };

  FieldsError fields_error = { valid_fields, NULL };

  if (!_check_fields (structure, fields_error, error))
    return FALSE;

  GET_AND_CHECK ("element-name", G_TYPE_STRING, &element_name, done);
  GET_AND_CHECK ("property-name", G_TYPE_STRING, &property_name, done);
  GET_AND_CHECK ("value", G_TYPE_DOUBLE, &value, done);
  GET_AND_CHECK ("timestamp", GST_TYPE_CLOCK_TIME, &timestamp, done);

  element =
      GES_TRACK_ELEMENT (ges_timeline_get_element (timeline, element_name));

  if (!GES_IS_TRACK_ELEMENT (element)) {
    *error =
        g_error_new (GES_ERROR, 0, "Could not find TrackElement %s",
        element_name);

    goto done;
  }

  binding = ges_track_element_get_control_binding (element, property_name);
  if (binding == NULL) {
    *error = g_error_new (GES_ERROR, 0, "No control binding found for %s:%s"
        " you should first set-control-binding on it",
        element_name, property_name);

    goto done;
  }

  g_object_get (binding, "control-source", &source, NULL);

  if (source == NULL) {
    *error = g_error_new (GES_ERROR, 0, "No control source found for %s:%s"
        " you should first set-control-binding on it",
        element_name, property_name);

    goto done;
  }

  if (!GST_IS_TIMED_VALUE_CONTROL_SOURCE (source)) {
    *error = g_error_new (GES_ERROR, 0, "You can use add-keyframe"
        " only on GstTimedValueControlSource not %s",
        G_OBJECT_TYPE_NAME (source));

    goto done;
  }

  if (!g_strcmp0 (gst_structure_get_name (structure), "add-keyframe"))
    ret = gst_timed_value_control_source_set (source, timestamp, value);
  else {
    ret = gst_timed_value_control_source_unset (source, timestamp);

    if (!ret) {
      *error =
          g_error_new (GES_ERROR, 0,
          "Could not unset value for timestamp: %" GST_TIME_FORMAT,
          GST_TIME_ARGS (timestamp));
    }
  }

done:
  if (source)
    gst_object_unref (source);
  g_free (element_name);
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

gboolean
_ges_add_clip_from_struct (GESTimeline * timeline, GstStructure * structure,
    GError ** error)
{
  GESAsset *asset;
  GESLayer *layer;
  GESClip *clip;
  gint layer_priority;
  const gchar *name;
  const gchar *pattern;
  gchar *asset_id = NULL;
  const gchar *type_string;
  GType type;
  gboolean res = FALSE;

  GstClockTime duration = 1 * GST_SECOND, inpoint = 0, start =
      GST_CLOCK_TIME_NONE;

  const gchar *valid_fields[] =
      { "asset-id", "pattern", "name", "layer-priority", "layer", "type",
    "start", "inpoint", "duration", NULL
  };

  FieldsError fields_error = { valid_fields, NULL };

  if (!_check_fields (structure, fields_error, error))
    return FALSE;

  GET_AND_CHECK ("asset-id", G_TYPE_STRING, &asset_id, beach);

  TRY_GET ("pattern", G_TYPE_STRING, &pattern, NULL);
  TRY_GET ("name", G_TYPE_STRING, &name, NULL);
  TRY_GET ("layer-priority", G_TYPE_INT, &layer_priority, -1);
  if (layer_priority == -1)
    TRY_GET ("layer", G_TYPE_INT, &layer_priority, -1);
  TRY_GET ("type", G_TYPE_STRING, &type_string, "GESUriClip");
  TRY_GET ("start", GST_TYPE_CLOCK_TIME, &start, GST_CLOCK_TIME_NONE);
  TRY_GET ("inpoint", GST_TYPE_CLOCK_TIME, &inpoint, 0);
  TRY_GET ("duration", GST_TYPE_CLOCK_TIME, &duration, GST_CLOCK_TIME_NONE);

  if (!(type = g_type_from_name (type_string))) {
    *error = g_error_new (GES_ERROR, 0, "This type doesn't exist : %s",
        type_string);

    goto beach;
  }

  if (type == GES_TYPE_URI_CLIP) {
    asset_id = ensure_uri (asset_id);
  } else {
    asset_id = g_strdup (asset_id);
  }

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

  clip = ges_layer_add_asset (layer, asset, start, inpoint, duration,
      GES_TRACK_TYPE_UNKNOWN);

  if (clip) {
    res = TRUE;

    if (GES_IS_TEST_CLIP (clip)) {
      if (pattern) {
        GEnumClass *enum_class =
            G_ENUM_CLASS (g_type_class_ref (GES_VIDEO_TEST_PATTERN_TYPE));
        GEnumValue *value = g_enum_get_value_by_nick (enum_class, pattern);

        if (!value) {
          res = FALSE;
          goto beach;
        }

        ges_test_clip_set_vpattern (GES_TEST_CLIP (clip), value->value);
        g_type_class_unref (enum_class);
      }
    }

    if (name
        && !ges_timeline_element_set_name (GES_TIMELINE_ELEMENT (clip), name)) {
      res = FALSE;
      *error =
          g_error_new (GES_ERROR, 0, "couldn't set name %s on clip with id %s",
          name, asset_id);
    }
  } else {
    *error = g_error_new (GES_ERROR, 0,
        "Couldn't add clip with id %s to layer with priority %d", asset_id,
        layer_priority);
  }

  if (res) {
    g_object_set_qdata (G_OBJECT (timeline), LAST_CONTAINER_QDATA, clip);
    g_object_set_qdata (G_OBJECT (timeline), LAST_CHILD_QDATA, NULL);
  }

  gst_object_unref (layer);

beach:
  g_free (asset_id);
  return res;
}

gboolean
_ges_container_add_child_from_struct (GESTimeline * timeline,
    GstStructure * structure, GError ** error)
{
  GESAsset *asset;
  GESContainer *container;
  GESTimelineElement *child = NULL;
  const gchar *container_name, *child_name, *child_type, *id;

  gboolean res = TRUE;
  const gchar *valid_fields[] = { "container-name", "asset-id",
    "child-type", "child-name", NULL
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

  g_return_val_if_fail (GES_IS_CONTAINER (container), FALSE);

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
      g_error_new (GES_ERROR, 0, "Could not extract child element");

      goto beach;
    }
  }

  child_name = gst_structure_get_string (structure, "child-name");
  if (!child && child_name) {
    child = ges_timeline_get_element (timeline, child_name);
    if (!GES_IS_TIMELINE_ELEMENT (child)) {
      g_error_new (GES_ERROR, 0, "Could not find child element");

      goto beach;
    }
  }

  if (!child) {
    g_error_new (GES_ERROR, 0, "Wong parametters, could not get a child");

    return FALSE;
  }

  if (child_name)
    ges_timeline_element_set_name (child, child_name);
  else
    child_name = GES_TIMELINE_ELEMENT_NAME (child);

  res = ges_container_add (container, child);
  if (res == FALSE) {
    g_error_new (GES_ERROR, 0, "Could not add child to container");
  } else {
    g_object_set_qdata (G_OBJECT (timeline), LAST_CHILD_QDATA, child);
  }

beach:
  return res;
}

gboolean
_ges_set_child_property_from_struct (GESTimeline * timeline,
    GstStructure * structure, GError ** error)
{
  const GValue *value;
  GESTimelineElement *element;
  const gchar *property_name, *element_name;

  const gchar *valid_fields[] = { "element-name", "property", "value", NULL };

  FieldsError fields_error = { valid_fields, NULL };

  if (!_check_fields (structure, fields_error, error))
    return FALSE;

  element_name = gst_structure_get_string (structure, "element-name");
  if (element_name == NULL)
    element = g_object_get_qdata (G_OBJECT (timeline), LAST_CHILD_QDATA);
  else
    element = ges_timeline_get_element (timeline, element_name);

  property_name = gst_structure_get_string (structure, "property");
  if (property_name == NULL) {
    const gchar *name = gst_structure_get_name (structure);

    if (g_str_has_prefix (name, "set-"))
      property_name = &name[4];
    else {
      gchar *struct_str = gst_structure_to_string (structure);

      *error =
          g_error_new (GES_ERROR, 0, "Could not find any property name in %s",
          struct_str);
      g_free (struct_str);

      return FALSE;
    }
  }

  if (element) {
    if (!ges_track_element_lookup_child (GES_TRACK_ELEMENT (element),
            property_name, NULL, NULL))
      element = NULL;
  }

  if (!element) {
    element = g_object_get_qdata (G_OBJECT (timeline), LAST_CONTAINER_QDATA);

    if (element == NULL) {
      *error =
          g_error_new (GES_ERROR, 0,
          "Could not find anywhere to set property: %s", property_name);

      return FALSE;
    }
  }

  if (!GES_IS_TIMELINE_ELEMENT (element)) {
    *error =
        g_error_new (GES_ERROR, 0, "Could not find child %s", element_name);

    return FALSE;
  }

  value = gst_structure_get_value (structure, "value");

  GST_DEBUG ("%s Setting %s property to %p",
      element->name, property_name, value);

  ges_timeline_element_set_child_property (element, property_name,
      (GValue *) value);

  return TRUE;
}

#undef GET_AND_CHECK
#undef TRY_GET
