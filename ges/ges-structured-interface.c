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


#define LAST_CONTAINER_QDATA g_quark_from_string("ges-structured-last-container")
#define LAST_CHILD_QDATA g_quark_from_string("ges-structured-last-child")

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


  if (!gst_structure_get (structure,
          "element-name", G_TYPE_STRING, &element_name,
          "property-name", G_TYPE_STRING, &property_name,
          "value", G_TYPE_DOUBLE, &value,
          "timestamp", GST_TYPE_CLOCK_TIME, &timestamp, NULL)) {
    *error = g_error_new (GES_ERROR, 0, "Could not get one of the mandatory"
        " fields in %s", gst_structure_get_name (structure));

    goto done;
  }

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
    const gchar * id)
{
  GESAsset *asset;
  GError *error = NULL;
  GESProject *project = ges_timeline_get_project (timeline);

  asset = ges_project_create_asset_sync (project, id, type, &error);
  if (!asset || error) {
    GST_ERROR
        ("There was an error requesting the asset with id %s and type %s (%s)",
        id, g_type_name (type), error ? error->message : "unknown");

    return NULL;
  }

  return asset;
}

/* Unref after usage */
GESLayer *
_ges_get_layer_by_priority (GESTimeline * timeline, gint priority)
{
  GList *layers, *tmp;
  gint nlayers;
  GESLayer *layer = NULL;

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

  for (tmp = layers; tmp; tmp = tmp->next) {
    GESLayer *tmp_layer = GES_LAYER (tmp->data);
    guint tmp_priority;

    g_object_get (tmp_layer, "priority", &tmp_priority, NULL);
    if ((gint) tmp_priority == priority) {
      layer = gst_object_ref (tmp_layer);
      break;
    }
  }

done:
  g_list_free_full (layers, gst_object_unref);

  return layer;
}

#define GET_AND_CHECK(name,type,var) G_STMT_START {\
  if (!gst_structure_get (structure, name, type, var, NULL)) {\
    *error = g_error_new (GES_ERROR, 0, "Could not get mandatory field: %s in %s",\
        name,  gst_structure_get_name (structure)); \
    goto beach;\
  } \
} G_STMT_END

#define TRY_GET(name,type,var,def) G_STMT_START {\
  if (!gst_structure_get (structure, name, type, var, NULL)) {\
    *var = def; \
  } \
} G_STMT_END

gboolean
_ges_add_clip_from_struct (GESTimeline * timeline, GstStructure * structure,
    GError ** error)
{
  GESAsset *asset;
  GESLayer *layer;
  GESClip *clip;
  gint layer_priority;
  const gchar *name;
  const gchar *asset_id;
  const gchar *type_string;
  GType type;
  gboolean res = FALSE;
  GstClockTime duration = 1 * GST_SECOND, inpoint = 0, start =
      GST_CLOCK_TIME_NONE;

  GET_AND_CHECK ("asset-id", G_TYPE_STRING, &asset_id);

  TRY_GET ("name", G_TYPE_STRING, &name, NULL);
  TRY_GET ("layer-priority", G_TYPE_INT, &layer_priority,
      g_list_length (timeline->layers));
  TRY_GET ("layer", G_TYPE_INT, &layer_priority,
      g_list_length (timeline->layers));
  TRY_GET ("type", G_TYPE_STRING, &type_string, "GESUriClip");
  TRY_GET ("start", GST_TYPE_CLOCK_TIME, &start, 0);
  TRY_GET ("inpoint", GST_TYPE_CLOCK_TIME, &inpoint, 0);
  TRY_GET ("duration", GST_TYPE_CLOCK_TIME, &duration, GST_CLOCK_TIME_NONE);

  if (!(type = g_type_from_name (type_string))) {
    *error = g_error_new (GES_ERROR, 0, "This type doesn't exist : %s",
        type_string);

    goto beach;
  }

  asset = _ges_get_asset_from_timeline (timeline, type, asset_id);
  if (!asset) {
    res = FALSE;

    goto beach;
  }

  layer = _ges_get_layer_by_priority (timeline, layer_priority);

  if (!layer) {
    g_error_new (GES_ERROR, 0, "No layer with priority %d", layer_priority);
    goto beach;
  }

  clip = ges_layer_add_asset (layer, asset, start, inpoint, duration,
      GES_TRACK_TYPE_UNKNOWN);

  if (clip) {
    res = TRUE;
    if (name
        && !ges_timeline_element_set_name (GES_TIMELINE_ELEMENT (clip), name)) {
      res = FALSE;
      g_error_new (GES_ERROR, 0, "couldn't set name %s on clip with id %s",
          name, asset_id);
    }
  } else {
    g_error_new (GES_ERROR, 0,
        "Couldn't add clip with id %s to layer with priority %d", asset_id,
        layer_priority);
  }

  if (res) {
    g_object_set_qdata (G_OBJECT (timeline), LAST_CONTAINER_QDATA, clip);
    g_object_set_qdata (G_OBJECT (timeline), LAST_CHILD_QDATA, NULL);
  }

  gst_object_unref (layer);

beach:
  return res;
}

#undef GET_AND_CHECK
#undef TRY_GET

gboolean
_ges_container_add_child_from_struct (GESTimeline * timeline,
    GstStructure * structure, GError ** error)
{
  GESAsset *asset;
  GESContainer *container;
  GESTimelineElement *child = NULL;
  const gchar *container_name, *child_name, *child_type, *id;

  gboolean res = TRUE;

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
        id);

    if (asset == NULL) {
      res = FALSE;
      g_error_new (GES_ERROR, 0, "Could not find asset: %s", id);
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
