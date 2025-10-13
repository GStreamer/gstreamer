/* Gstreamer Editing Services
 *
 * Copyright (C) <2012> Thibault Saunier <thibault.saunier@collabora.com>
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
#undef VERSION
#endif


/* TODO Determine error codes numbers */

#include <string.h>
#include <errno.h>
#include <locale.h>

#include "ges.h"
#include <glib/gstdio.h>
#include "ges-internal.h"

#define parent_class ges_xml_formatter_parent_class
#define API_VERSION 0
#define MINOR_VERSION 8
#define VERSION 0.8

#define COLLECT_STR_OPT (G_MARKUP_COLLECT_STRING | G_MARKUP_COLLECT_OPTIONAL)

#define _GET_PRIV(o) (((GESXmlFormatter*)o)->priv)

typedef struct
{
  const gchar *id;
  gint start_line;
  gint start_char;
  gint fd;
  gchar *filename;
  GError *error;
  GMainLoop *ml;
} SubprojectData;

struct _GESXmlFormatterPrivate
{
  gboolean ges_opened;
  gboolean project_opened;

  GString *str;

  GHashTable *element_id;
  GHashTable *subprojects_map;
  SubprojectData *subproject;
  gint subproject_depth;

  guint nbelements;

  guint min_version;
};

G_LOCK_DEFINE_STATIC (uri_subprojects_map_lock);
/* { project_uri: { subproject_uri: new_suproject_uri}} */
static GHashTable *uri_subprojects_map = NULL;

G_DEFINE_TYPE_WITH_PRIVATE (GESXmlFormatter, ges_xml_formatter,
    GES_TYPE_BASE_XML_FORMATTER);

static GString *_save_project (GESFormatter * formatter, GString * str,
    GESProject * project, GESTimeline * timeline, GError ** error, guint depth);

static inline void
_parse_ges_element (GMarkupParseContext * context, const gchar * element_name,
    const gchar ** attribute_names, const gchar ** attribute_values,
    GESXmlFormatter * self, GError ** error)
{
  guint api_version;
  const gchar *version, *properties;

  gchar **split_version = NULL;

  if (g_strcmp0 (element_name, "ges")) {
    g_set_error (error, G_MARKUP_ERROR,
        G_MARKUP_ERROR_INVALID_CONTENT,
        "Found element '%s', Missing '<ges>' element'", element_name);
    return;
  }

  if (!g_markup_collect_attributes (element_name, attribute_names,
          attribute_values, error, G_MARKUP_COLLECT_STRING, "version", &version,
          COLLECT_STR_OPT, "properties", &properties,
          G_MARKUP_COLLECT_INVALID)) {
    return;
  }

  split_version = g_strsplit (version, ".", 2);
  if (split_version[1] == NULL)
    goto failed;

  errno = 0;
  api_version = g_ascii_strtoull (split_version[0], NULL, 10);
  if (errno || api_version != API_VERSION)
    goto stroull_failed;

  self->priv->min_version = g_ascii_strtoull (split_version[1], NULL, 10);
  if (self->priv->min_version > MINOR_VERSION)
    goto failed;

  _GET_PRIV (self)->ges_opened = TRUE;
  g_strfreev (split_version);
  return;

failed:
  g_set_error (error, G_MARKUP_ERROR,
      G_MARKUP_ERROR_INVALID_CONTENT,
      "element '%s', %s wrong version'", element_name, version);
  if (split_version)
    g_strfreev (split_version);

  return;

stroull_failed:
  GST_WARNING_OBJECT (self, "Error while strtoull: %s", g_strerror (errno));
  goto failed;
}

static inline void
_parse_project (GMarkupParseContext * context, const gchar * element_name,
    const gchar ** attribute_names, const gchar ** attribute_values,
    GESXmlFormatter * self, GError ** error)
{
  const gchar *metadatas = NULL, *properties;
  GESXmlFormatterPrivate *priv = _GET_PRIV (self);

  if (g_strcmp0 (element_name, "project")) {
    g_set_error (error, G_MARKUP_ERROR,
        G_MARKUP_ERROR_INVALID_CONTENT,
        "Found element '%s', Missing '<project>' element'", element_name);
  } else {
    priv->project_opened = TRUE;
    if (!g_markup_collect_attributes (element_name, attribute_names,
            attribute_values, error,
            COLLECT_STR_OPT, "properties", &properties,
            COLLECT_STR_OPT, "metadatas", &metadatas, G_MARKUP_COLLECT_INVALID))
      return;

    if (GES_FORMATTER (self)->project && metadatas)
      ges_meta_container_add_metas_from_string (GES_META_CONTAINER
          (GES_FORMATTER (self)->project), metadatas);

  }
}

static inline void
_parse_encoding_profile (GMarkupParseContext * context,
    const gchar * element_name, const gchar ** attribute_names,
    const gchar ** attribute_values, GESXmlFormatter * self, GError ** error)
{
  GstCaps *capsformat = NULL;
  GstStructure *preset_properties = NULL;
  const gchar *name, *description, *type, *preset = NULL,
      *str_preset_properties = NULL, *preset_name = NULL, *format;

  if (!g_markup_collect_attributes (element_name, attribute_names,
          attribute_values, error,
          G_MARKUP_COLLECT_STRING, "name", &name,
          G_MARKUP_COLLECT_STRING, "description", &description,
          G_MARKUP_COLLECT_STRING, "type", &type,
          COLLECT_STR_OPT, "preset", &preset,
          COLLECT_STR_OPT, "preset-properties", &str_preset_properties,
          COLLECT_STR_OPT, "preset-name", &preset_name,
          COLLECT_STR_OPT, "format", &format, G_MARKUP_COLLECT_INVALID))
    return;

  if (format)
    capsformat = gst_caps_from_string (format);

  if (str_preset_properties) {
    preset_properties = gst_structure_from_string (str_preset_properties, NULL);
    if (preset_properties == NULL) {
      gst_caps_unref (capsformat);
      g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
          "element '%s', Wrong preset-properties format.", element_name);
      return;
    }
  }

  ges_base_xml_formatter_add_encoding_profile (GES_BASE_XML_FORMATTER (self),
      type, NULL, name, description, capsformat, preset, preset_properties,
      preset_name, 0, 0, NULL, 0, FALSE, NULL, TRUE, error);

  if (preset_properties)
    gst_structure_free (preset_properties);
}

static inline void
_parse_stream_profile (GMarkupParseContext * context,
    const gchar * element_name, const gchar ** attribute_names,
    const gchar ** attribute_values, GESXmlFormatter * self, GError ** error)
{
  gboolean variableframerate = FALSE, enabled = TRUE;
  guint id = 0, presence = 0, pass = 0;
  GstCaps *format_caps = NULL, *restriction_caps = NULL;
  GstStructure *preset_properties = NULL;
  const gchar *parent, *strid, *type, *strpresence, *format = NULL,
      *name = NULL, *description = NULL, *preset,
      *str_preset_properties = NULL, *preset_name = NULL, *restriction = NULL,
      *strpass = NULL, *strvariableframerate = NULL, *strenabled = NULL;

  /* FIXME Looks like there is a bug in that function, if we put the parent
   * at the beginning it set %NULL and not the real value... :/ */
  if (!g_markup_collect_attributes (element_name, attribute_names,
          attribute_values, error,
          G_MARKUP_COLLECT_STRING, "id", &strid,
          G_MARKUP_COLLECT_STRING, "type", &type,
          G_MARKUP_COLLECT_STRING, "presence", &strpresence,
          COLLECT_STR_OPT, "format", &format,
          COLLECT_STR_OPT, "name", &name,
          COLLECT_STR_OPT, "description", &description,
          COLLECT_STR_OPT, "preset", &preset,
          COLLECT_STR_OPT, "preset-properties", &str_preset_properties,
          COLLECT_STR_OPT, "preset-name", &preset_name,
          COLLECT_STR_OPT, "restriction", &restriction,
          COLLECT_STR_OPT, "pass", &strpass,
          COLLECT_STR_OPT, "variableframerate", &strvariableframerate,
          COLLECT_STR_OPT, "enabled", &strenabled,
          G_MARKUP_COLLECT_STRING, "parent", &parent, G_MARKUP_COLLECT_INVALID))
    return;

  errno = 0;
  id = g_ascii_strtoll (strid, NULL, 10);
  if (errno)
    goto convertion_failed;

  if (strpresence) {
    presence = g_ascii_strtoll (strpresence, NULL, 10);
    if (errno)
      goto convertion_failed;
  }

  if (str_preset_properties) {
    preset_properties = gst_structure_from_string (str_preset_properties, NULL);
    if (preset_properties == NULL)
      goto convertion_failed;
  }

  if (strpass) {
    pass = g_ascii_strtoll (strpass, NULL, 10);
    if (errno)
      goto convertion_failed;
  }

  if (strvariableframerate) {
    variableframerate = g_ascii_strtoll (strvariableframerate, NULL, 10);
    if (errno)
      goto convertion_failed;
  }

  if (strenabled) {
    enabled = g_ascii_strtoll (strenabled, NULL, 10);
    if (errno)
      goto convertion_failed;
  }

  if (format)
    format_caps = gst_caps_from_string (format);

  if (restriction)
    restriction_caps = gst_caps_from_string (restriction);

  ges_base_xml_formatter_add_encoding_profile (GES_BASE_XML_FORMATTER (self),
      type, parent, name, description, format_caps, preset, preset_properties,
      preset_name, id, presence, restriction_caps, pass, variableframerate,
      NULL, enabled, error);

  if (preset_properties)
    gst_structure_free (preset_properties);

  return;

convertion_failed:
  g_set_error (error, G_MARKUP_ERROR,
      G_MARKUP_ERROR_INVALID_CONTENT,
      "element '%s', Wrong property type, error: %s'", element_name,
      g_strerror (errno));
  return;
}

static inline void
_parse_timeline (GMarkupParseContext * context, const gchar * element_name,
    const gchar ** attribute_names, const gchar ** attribute_values,
    GESXmlFormatter * self, GError ** error)
{
  const gchar *metadatas = NULL, *properties = NULL;
  GESTimeline *timeline = GES_FORMATTER (self)->timeline;

  if (!g_markup_collect_attributes (element_name, attribute_names,
          attribute_values, error,
          COLLECT_STR_OPT, "properties", &properties,
          COLLECT_STR_OPT, "metadatas", &metadatas, G_MARKUP_COLLECT_INVALID))
    return;

  if (timeline == NULL)
    return;

  ges_base_xml_formatter_set_timeline_properties (GES_BASE_XML_FORMATTER (self),
      timeline, properties, metadatas);
}

static inline void
_parse_asset (GMarkupParseContext * context, const gchar * element_name,
    const gchar ** attribute_names, const gchar ** attribute_values,
    GESXmlFormatter * self, GError ** error)
{
  GType extractable_type;
  const gchar *id, *extractable_type_name, *metadatas = NULL, *properties =
      NULL, *proxy_id = NULL;
  GESXmlFormatterPrivate *priv = _GET_PRIV (self);

  if (!g_markup_collect_attributes (element_name, attribute_names,
          attribute_values, error, G_MARKUP_COLLECT_STRING, "id", &id,
          G_MARKUP_COLLECT_STRING, "extractable-type-name",
          &extractable_type_name,
          COLLECT_STR_OPT, "properties", &properties,
          COLLECT_STR_OPT, "metadatas", &metadatas,
          COLLECT_STR_OPT, "proxy-id", &proxy_id, G_MARKUP_COLLECT_INVALID))
    return;

  extractable_type = g_type_from_name (extractable_type_name);
  if (extractable_type == GES_TYPE_TIMELINE) {
    SubprojectData *subproj_data = g_new0 (SubprojectData, 1);
    const gchar *nid;

    priv->subproject = subproj_data;
    G_LOCK (uri_subprojects_map_lock);
    nid = g_hash_table_lookup (priv->subprojects_map, id);
    G_UNLOCK (uri_subprojects_map_lock);

    if (!nid) {
      subproj_data->id = id;
      subproj_data->fd =
          g_file_open_tmp ("XXXXXX.xges", &subproj_data->filename, error);
      if (subproj_data->fd == -1) {
        GST_ERROR_OBJECT (self, "Could not create subproject file for %s", id);
        return;
      }
      g_markup_parse_context_get_position (context, &subproj_data->start_line,
          &subproj_data->start_char);
      id = g_filename_to_uri (subproj_data->filename, NULL, NULL);
      G_LOCK (uri_subprojects_map_lock);
      g_hash_table_insert (priv->subprojects_map, g_strdup (subproj_data->id),
          (gchar *) id);
      G_UNLOCK (uri_subprojects_map_lock);
      GST_INFO_OBJECT (self, "Serialized subproject %sis now at: %s",
          subproj_data->id, id);
    } else {
      GST_DEBUG_OBJECT (self, "Subproject already exists: %s -> %s", id, nid);
      id = nid;
      subproj_data->start_line = -1;
    }
  }

  if (extractable_type == G_TYPE_NONE)
    g_set_error (error, G_MARKUP_ERROR,
        G_MARKUP_ERROR_INVALID_CONTENT,
        "element '%s' invalid extractable_type %s'",
        element_name, extractable_type_name);
  else if (!g_type_is_a (extractable_type, GES_TYPE_EXTRACTABLE))
    g_set_error (error, G_MARKUP_ERROR,
        G_MARKUP_ERROR_INVALID_CONTENT,
        "element '%s', %s not an extractable_type'",
        element_name, extractable_type_name);
  else {
    GstStructure *props = NULL;
    if (properties)
      props = gst_structure_from_string (properties, NULL);

    if (extractable_type == GES_TYPE_URI_CLIP) {
      G_LOCK (uri_subprojects_map_lock);
      if (g_hash_table_contains (priv->subprojects_map, id)) {
        id = g_hash_table_lookup (priv->subprojects_map, id);

        GST_DEBUG_OBJECT (self, "Using subproject %s", id);
      }
      G_UNLOCK (uri_subprojects_map_lock);
    }

    ges_base_xml_formatter_add_asset (GES_BASE_XML_FORMATTER (self), id,
        extractable_type, props, metadatas, proxy_id, error);
    if (props)
      gst_structure_free (props);
  }
}


static inline void
_parse_track (GMarkupParseContext * context, const gchar * element_name,
    const gchar ** attribute_names, const gchar ** attribute_values,
    GESXmlFormatter * self, GError ** error)
{
  GstCaps *caps;
  GESTrackType track_type;
  GstStructure *props = NULL;
  const gchar *strtrack_type, *strcaps, *strtrack_id, *metadatas =
      NULL, *properties = NULL;

  if (!g_markup_collect_attributes (element_name, attribute_names,
          attribute_values, error,
          G_MARKUP_COLLECT_STRING, "track-type", &strtrack_type,
          G_MARKUP_COLLECT_STRING, "track-id", &strtrack_id,
          COLLECT_STR_OPT, "properties", &properties,
          COLLECT_STR_OPT, "metadatas", &metadatas,
          G_MARKUP_COLLECT_STRING, "caps", &strcaps, G_MARKUP_COLLECT_INVALID))
    return;

  if ((caps = gst_caps_from_string (strcaps)) == NULL)
    goto wrong_caps;

  errno = 0;
  track_type = g_ascii_strtoll (strtrack_type, NULL, 10);
  if (errno)
    goto convertion_failed;

  if (properties) {
    props = gst_structure_from_string (properties, NULL);
    if (!props)
      goto wrong_properties;
  }

  ges_base_xml_formatter_add_track (GES_BASE_XML_FORMATTER (self), track_type,
      caps, strtrack_id, props, metadatas, error);

  if (props)
    gst_structure_free (props);

  gst_caps_unref (caps);

  return;

wrong_caps:
  g_set_error (error, G_MARKUP_ERROR,
      G_MARKUP_ERROR_INVALID_CONTENT,
      "element '%s', Can not create caps: %s'", element_name, strcaps);
  return;

convertion_failed:
  gst_caps_unref (caps);
  g_set_error (error, G_MARKUP_ERROR,
      G_MARKUP_ERROR_INVALID_CONTENT,
      "element '%s', Wrong property type, error: %s'", element_name,
      g_strerror (errno));
  return;

wrong_properties:
  gst_clear_caps (&caps);
  g_set_error (error, G_MARKUP_ERROR,
      G_MARKUP_ERROR_INVALID_CONTENT,
      "element '%s', Can not create properties: %s'", element_name, properties);
  return;

}

static inline void
_parse_layer (GMarkupParseContext * context, const gchar * element_name,
    const gchar ** attribute_names, const gchar ** attribute_values,
    GESXmlFormatter * self, GError ** error)
{
  GstStructure *props = NULL;
  guint priority;
  GType extractable_type = G_TYPE_NONE;
  const gchar *metadatas = NULL, *properties = NULL, *strprio = NULL,
      *extractable_type_name, *deactivated_tracks_str;

  gchar **deactivated_tracks = NULL;

  if (!g_markup_collect_attributes (element_name, attribute_names,
          attribute_values, error,
          G_MARKUP_COLLECT_STRING, "priority", &strprio,
          COLLECT_STR_OPT, "extractable-type-name", &extractable_type_name,
          COLLECT_STR_OPT, "properties", &properties,
          COLLECT_STR_OPT, "deactivated-tracks", &deactivated_tracks_str,
          COLLECT_STR_OPT, "metadatas", &metadatas, G_MARKUP_COLLECT_INVALID))
    return;

  if (extractable_type_name) {
    extractable_type = g_type_from_name (extractable_type_name);
    if (extractable_type == G_TYPE_NONE) {
      g_set_error (error, G_MARKUP_ERROR,
          G_MARKUP_ERROR_INVALID_CONTENT,
          "element '%s' invalid extractable_type %s'",
          element_name, extractable_type_name);

      return;
    } else if (!g_type_is_a (extractable_type, GES_TYPE_EXTRACTABLE)) {
      g_set_error (error, G_MARKUP_ERROR,
          G_MARKUP_ERROR_INVALID_CONTENT,
          "element '%s', %s not an extractable_type'",
          element_name, extractable_type_name);

      return;
    }
  }

  if (properties) {
    props = gst_structure_from_string (properties, NULL);
    if (props == NULL)
      goto wrong_properties;
  }

  errno = 0;
  priority = g_ascii_strtoll (strprio, NULL, 10);
  if (errno)
    goto convertion_failed;

  if (deactivated_tracks_str)
    deactivated_tracks = g_strsplit (deactivated_tracks_str, " ", -1);

  ges_base_xml_formatter_add_layer (GES_BASE_XML_FORMATTER (self),
      extractable_type, priority, props, metadatas, deactivated_tracks, error);

  g_strfreev (deactivated_tracks);

done:
  if (props)
    gst_structure_free (props);

  return;

convertion_failed:
  g_set_error (error, G_MARKUP_ERROR,
      G_MARKUP_ERROR_INVALID_CONTENT,
      "element '%s', Wrong property type, error: %s'", element_name,
      g_strerror (errno));
  goto done;

wrong_properties:
  g_set_error (error, G_MARKUP_ERROR,
      G_MARKUP_ERROR_INVALID_CONTENT,
      "element '%s', wrong layer properties '%s', could no be deserialized",
      element_name, properties);
}

static inline void
_parse_clip (GMarkupParseContext * context,
    const gchar * element_name, const gchar ** attribute_names,
    const gchar ** attribute_values, GESXmlFormatter * self, GError ** error)
{
  GType type;
  GstStructure *props = NULL, *children_props = NULL;
  GESTrackType track_types;
  GstClockTime start, inpoint = 0, duration, layer_prio;
  GESXmlFormatterPrivate *priv = _GET_PRIV (self);

  const gchar *strid, *asset_id, *strstart, *strin, *strduration, *strrate,
      *strtrack_types, *strtype, *metadatas = NULL, *properties =
      NULL, *children_properties = NULL, *strlayer_prio;

  if (!g_markup_collect_attributes (element_name, attribute_names,
          attribute_values, error,
          G_MARKUP_COLLECT_STRING, "id", &strid,
          G_MARKUP_COLLECT_STRING, "type-name", &strtype,
          G_MARKUP_COLLECT_STRING, "start", &strstart,
          G_MARKUP_COLLECT_STRING, "duration", &strduration,
          G_MARKUP_COLLECT_STRING, "asset-id", &asset_id,
          G_MARKUP_COLLECT_STRING, "track-types", &strtrack_types,
          G_MARKUP_COLLECT_STRING, "layer-priority", &strlayer_prio,
          COLLECT_STR_OPT, "properties", &properties,
          COLLECT_STR_OPT, "children-properties", &children_properties,
          COLLECT_STR_OPT, "metadatas", &metadatas,
          COLLECT_STR_OPT, "rate", &strrate,
          COLLECT_STR_OPT, "inpoint", &strin, G_MARKUP_COLLECT_INVALID)) {
    return;
  }
  type = g_type_from_name (strtype);
  if (!g_type_is_a (type, GES_TYPE_CLIP))
    goto wrong_type;

  errno = 0;
  track_types = g_ascii_strtoll (strtrack_types, NULL, 10);
  if (errno)
    goto convertion_failed;

  layer_prio = g_ascii_strtoll (strlayer_prio, NULL, 10);
  if (errno)
    goto convertion_failed;

  if (strin) {
    inpoint = g_ascii_strtoull (strin, NULL, 10);
    if (errno)
      goto convertion_failed;
  }

  start = g_ascii_strtoull (strstart, NULL, 10);
  if (errno)
    goto convertion_failed;

  duration = g_ascii_strtoull (strduration, NULL, 10);
  if (errno)
    goto convertion_failed;

  if (properties) {
    props = gst_structure_from_string (properties, NULL);
    if (props == NULL)
      goto wrong_properties;
  }

  if (children_properties) {
    children_props = gst_structure_from_string (children_properties, NULL);
    if (children_props == NULL)
      goto wrong_children_properties;
  }

  G_LOCK (uri_subprojects_map_lock);
  if (g_hash_table_contains (priv->subprojects_map, asset_id)) {
    asset_id = g_hash_table_lookup (priv->subprojects_map, asset_id);
    GST_DEBUG_OBJECT (self, "Using subproject %s", asset_id);
  }
  G_UNLOCK (uri_subprojects_map_lock);
  ges_base_xml_formatter_add_clip (GES_BASE_XML_FORMATTER (self),
      strid, asset_id, type, start, inpoint, duration, layer_prio,
      track_types, props, children_props, metadatas, error);
  if (props)
    gst_structure_free (props);
  if (children_props)
    gst_structure_free (children_props);

  return;

wrong_properties:
  g_set_error (error, G_MARKUP_ERROR,
      G_MARKUP_ERROR_INVALID_CONTENT,
      "element '%s', Clip %s properties '%s', could no be deserialized",
      element_name, asset_id, properties);
  return;

wrong_children_properties:
  g_set_error (error, G_MARKUP_ERROR,
      G_MARKUP_ERROR_INVALID_CONTENT,
      "element '%s', Clip %s children properties '%s', could no be deserialized",
      element_name, asset_id, children_properties);
  if (props)
    gst_structure_free (props);
  return;

convertion_failed:
  g_set_error (error, G_MARKUP_ERROR,
      G_MARKUP_ERROR_INVALID_CONTENT,
      "element '%s', Wrong property type, error: %s'", element_name,
      g_strerror (errno));
  return;

wrong_type:
  g_set_error (error, G_MARKUP_ERROR,
      G_MARKUP_ERROR_INVALID_CONTENT,
      "element '%s', %s not a GESClip'", element_name, strtype);
}

static inline void
_parse_binding (GMarkupParseContext * context, const gchar * element_name,
    const gchar ** attribute_names, const gchar ** attribute_values,
    GESXmlFormatter * self, GError ** error)
{
  const gchar *type = NULL, *source_type = NULL, *timed_values =
      NULL, *property_name = NULL, *mode = NULL, *track_id = NULL;
  gchar **pairs, **tmp;
  gchar *pair;
  GSList *list = NULL;

  if (!g_markup_collect_attributes (element_name, attribute_names,
          attribute_values, error,
          G_MARKUP_COLLECT_STRING, "type", &type,
          G_MARKUP_COLLECT_STRING, "source_type", &source_type,
          G_MARKUP_COLLECT_STRING, "property", &property_name,
          G_MARKUP_COLLECT_STRING, "mode", &mode,
          G_MARKUP_COLLECT_STRING, "track_id", &track_id,
          G_MARKUP_COLLECT_STRING, "values", &timed_values,
          G_MARKUP_COLLECT_INVALID)) {
    return;
  }

  pairs = g_strsplit (timed_values, " ", 0);
  for (tmp = pairs; tmp != NULL; tmp += 1) {
    gchar **value_pair;

    pair = *tmp;
    if (pair == NULL)
      break;
    if (strlen (pair)) {
      GstTimedValue *value;

      value = g_new0 (GstTimedValue, 1);
      value_pair = g_strsplit (pair, ":", 0);
      value->timestamp = g_ascii_strtoull (value_pair[0], NULL, 10);
      value->value = g_ascii_strtod (value_pair[1], NULL);
      list = g_slist_append (list, value);
      g_strfreev (value_pair);
    }
  }

  g_strfreev (pairs);

  ges_base_xml_formatter_add_control_binding (GES_BASE_XML_FORMATTER (self),
      type,
      source_type,
      property_name, (gint) g_ascii_strtoll (mode, NULL, 10), track_id, list);
}

static inline void
_parse_source (GMarkupParseContext * context, const gchar * element_name,
    const gchar ** attribute_names, const gchar ** attribute_values,
    GESXmlFormatter * self, GError ** error)
{
  GstStructure *children_props = NULL, *props = NULL;
  const gchar *track_id = NULL, *children_properties = NULL, *properties =
      NULL, *metadatas = NULL;

  if (!g_markup_collect_attributes (element_name, attribute_names,
          attribute_values, error,
          G_MARKUP_COLLECT_STRING, "track-id", &track_id,
          COLLECT_STR_OPT, "children-properties", &children_properties,
          COLLECT_STR_OPT, "properties", &properties,
          COLLECT_STR_OPT, "metadatas", &metadatas, G_MARKUP_COLLECT_INVALID)) {
    return;
  }

  if (children_properties) {
    children_props = gst_structure_from_string (children_properties, NULL);
    if (children_props == NULL)
      goto wrong_children_properties;
  }

  if (properties) {
    props = gst_structure_from_string (properties, NULL);
    if (props == NULL)
      goto wrong_properties;
  }

  ges_base_xml_formatter_add_source (GES_BASE_XML_FORMATTER (self), track_id,
      children_props, props, metadatas);

done:
  if (children_props)
    gst_structure_free (children_props);

  if (props)
    gst_structure_free (props);

  return;

wrong_children_properties:
  g_set_error (error, G_MARKUP_ERROR,
      G_MARKUP_ERROR_INVALID_CONTENT,
      "element '%s', children properties '%s', could no be deserialized",
      element_name, children_properties);
  goto done;

wrong_properties:
  g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
      "element '%s', properties '%s', could no be deserialized",
      element_name, properties);
  goto done;
}

static inline void
_parse_effect (GMarkupParseContext * context, const gchar * element_name,
    const gchar ** attribute_names, const gchar ** attribute_values,
    GESXmlFormatter * self, GError ** error)
{
  GType type;

  GstStructure *children_props = NULL, *props = NULL;
  const gchar *asset_id = NULL, *strtype = NULL, *track_id =
      NULL, *metadatas = NULL, *properties = NULL, *track_type = NULL,
      *children_properties = NULL, *clip_id;

  if (!g_markup_collect_attributes (element_name, attribute_names,
          attribute_values, error,
          COLLECT_STR_OPT, "metadatas", &metadatas,
          G_MARKUP_COLLECT_STRING, "asset-id", &asset_id,
          G_MARKUP_COLLECT_STRING, "clip-id", &clip_id,
          G_MARKUP_COLLECT_STRING, "type-name", &strtype,
          G_MARKUP_COLLECT_STRING, "track-id", &track_id,
          COLLECT_STR_OPT, "children-properties", &children_properties,
          COLLECT_STR_OPT, "track-type", &track_type,
          COLLECT_STR_OPT, "properties", &properties,
          G_MARKUP_COLLECT_INVALID)) {
    return;
  }

  type = g_type_from_name (strtype);
  if (!g_type_is_a (type, GES_TYPE_BASE_EFFECT))
    goto wrong_type;

  if (children_properties) {
    children_props = gst_structure_from_string (children_properties, NULL);
    if (children_props == NULL)
      goto wrong_children_properties;
  }

  if (properties) {
    props = gst_structure_from_string (properties, NULL);
    if (props == NULL)
      goto wrong_properties;
  }

  ges_base_xml_formatter_add_track_element (GES_BASE_XML_FORMATTER (self),
      type, asset_id, track_id, clip_id, children_props, props, metadatas,
      error);

out:

  if (props)
    gst_structure_free (props);
  if (children_props)
    gst_structure_free (children_props);

  return;

wrong_properties:
  g_set_error (error, G_MARKUP_ERROR,
      G_MARKUP_ERROR_INVALID_CONTENT,
      "element '%s', Effect %s properties '%s', could no be deserialized",
      element_name, asset_id, properties);
  goto out;

wrong_children_properties:
  g_set_error (error, G_MARKUP_ERROR,
      G_MARKUP_ERROR_INVALID_CONTENT,
      "element '%s', Effect %s children properties '%s', could no be deserialized",
      element_name, asset_id, children_properties);
  goto out;

wrong_type:
  g_set_error (error, G_MARKUP_ERROR,
      G_MARKUP_ERROR_INVALID_CONTENT,
      "element '%s', %s not a GESBaseEffect'", element_name, strtype);
}


static inline void
_parse_group (GMarkupParseContext * context, const gchar * element_name,
    const gchar ** attribute_names, const gchar ** attribute_values,
    GESXmlFormatter * self, GError ** error)
{
  const gchar *id, *properties, *metadatas = NULL;

  if (!g_markup_collect_attributes (element_name, attribute_names,
          attribute_values, error,
          G_MARKUP_COLLECT_STRING, "id", &id,
          G_MARKUP_COLLECT_STRING, "properties", &properties,
          COLLECT_STR_OPT, "metadatas", &metadatas, G_MARKUP_COLLECT_INVALID)) {
    return;
  }

  ges_base_xml_formatter_add_group (GES_BASE_XML_FORMATTER (self), id,
      properties, metadatas);
}

static inline void
_parse_group_child (GMarkupParseContext * context, const gchar * element_name,
    const gchar ** attribute_names, const gchar ** attribute_values,
    GESXmlFormatter * self, GError ** error)
{
  const gchar *child_id, *name;

  if (!g_markup_collect_attributes (element_name, attribute_names,
          attribute_values, error,
          G_MARKUP_COLLECT_STRING, "id", &child_id,
          G_MARKUP_COLLECT_STRING, "name", &name, G_MARKUP_COLLECT_INVALID)) {
    return;
  }

  ges_base_xml_formatter_last_group_add_child (GES_BASE_XML_FORMATTER (self),
      child_id, name);
}

static void
_parse_element_start (GMarkupParseContext * context, const gchar * element_name,
    const gchar ** attribute_names, const gchar ** attribute_values,
    gpointer self, GError ** error)
{
  GESXmlFormatterPrivate *priv = _GET_PRIV (self);

  if (priv->subproject) {
    if (g_strcmp0 (element_name, "ges") == 0) {
      priv->subproject_depth += 1;
    }
    return;
  }

  if (!G_UNLIKELY (priv->ges_opened)) {
    _parse_ges_element (context, element_name, attribute_names,
        attribute_values, self, error);
  } else if (!G_UNLIKELY (priv->project_opened))
    _parse_project (context, element_name, attribute_names, attribute_values,
        self, error);
  else if (g_strcmp0 (element_name, "ges") == 0) {
  } else if (g_strcmp0 (element_name, "encoding-profile") == 0)
    _parse_encoding_profile (context, element_name, attribute_names,
        attribute_values, self, error);
  else if (g_strcmp0 (element_name, "stream-profile") == 0)
    _parse_stream_profile (context, element_name, attribute_names,
        attribute_values, self, error);
  else if (g_strcmp0 (element_name, "timeline") == 0)
    _parse_timeline (context, element_name, attribute_names, attribute_values,
        self, error);
  else if (g_strcmp0 (element_name, "asset") == 0)
    _parse_asset (context, element_name, attribute_names, attribute_values,
        self, error);
  else if (g_strcmp0 (element_name, "track") == 0)
    _parse_track (context, element_name, attribute_names,
        attribute_values, self, error);
  else if (g_strcmp0 (element_name, "layer") == 0)
    _parse_layer (context, element_name, attribute_names,
        attribute_values, self, error);
  else if (g_strcmp0 (element_name, "clip") == 0)
    _parse_clip (context, element_name, attribute_names,
        attribute_values, self, error);
  else if (g_strcmp0 (element_name, "source") == 0)
    _parse_source (context, element_name, attribute_names,
        attribute_values, self, error);
  else if (g_strcmp0 (element_name, "effect") == 0)
    _parse_effect (context, element_name, attribute_names,
        attribute_values, self, error);
  else if (g_strcmp0 (element_name, "binding") == 0)
    _parse_binding (context, element_name, attribute_names,
        attribute_values, self, error);
  else if (g_strcmp0 (element_name, "group") == 0)
    _parse_group (context, element_name, attribute_names,
        attribute_values, self, error);
  else if (g_strcmp0 (element_name, "child") == 0)
    _parse_group_child (context, element_name, attribute_names,
        attribute_values, self, error);
  else
    GST_LOG_OBJECT (self, "Element %s not handled", element_name);
}

static gboolean
_save_subproject_data (GESXmlFormatter * self, SubprojectData * subproj_data,
    gint subproject_end_line, gint subproject_end_char, GError ** error)
{
  gsize size;
  gint line = 1, i;
  gboolean res = FALSE;
  gsize start = 0, end = 0;
  gchar *xml = GES_BASE_XML_FORMATTER (self)->xmlcontent;

  for (i = 0; xml[i] != '\0'; i++) {
    if (!start && line == subproj_data->start_line) {
      i += subproj_data->start_char - 1;
      start = i;
    }

    if (line == subproject_end_line) {
      end = i + subproject_end_char - 1;
      break;
    }

    if (xml[i] == '\n')
      line++;
  }
  g_assert (start && end);
  size = (end - start);

  GST_INFO_OBJECT (self, "Saving subproject %s from %d:%d(%" G_GSIZE_FORMAT
      ") to %d:%d(%" G_GSIZE_FORMAT ")",
      subproj_data->id, subproj_data->start_line, subproj_data->start_char,
      start, subproject_end_line, subproject_end_char, end);

  res = g_file_set_contents (subproj_data->filename, &xml[start], size, error);

  return res;
}

static void
_parse_element_end (GMarkupParseContext * context,
    const gchar * element_name, gpointer self, GError ** error)
{
  GESXmlFormatterPrivate *priv = _GET_PRIV (self);
  SubprojectData *subproj_data = priv->subproject;

  /*GESXmlFormatterPrivate *priv = _GET_PRIV (self); */

  if (!g_strcmp0 (element_name, "ges")) {
    gint subproject_end_line, subproject_end_char;

    if (priv->subproject_depth)
      priv->subproject_depth -= 1;

    if (!subproj_data) {
      if (GES_FORMATTER (self)->project) {
        gchar *version = g_strdup_printf ("%d.%d",
            API_VERSION, GES_XML_FORMATTER (self)->priv->min_version);

        ges_meta_container_set_string (GES_META_CONTAINER (GES_FORMATTER
                (self)->project), GES_META_FORMAT_VERSION, version);

        g_free (version);
        _GET_PRIV (self)->ges_opened = FALSE;
      }
    } else if (subproj_data->start_line != -1 && !priv->subproject_depth) {
      g_markup_parse_context_get_position (context, &subproject_end_line,
          &subproject_end_char);
      _save_subproject_data (GES_XML_FORMATTER (self), subproj_data,
          subproject_end_line, subproject_end_char, error);

      subproj_data->filename = NULL;
      g_close (subproj_data->fd, error);
      subproj_data->id = NULL;
      subproj_data->start_line = 0;
      subproj_data->start_char = 0;
    }

    if (!priv->subproject_depth) {
      g_clear_pointer (&priv->subproject, g_free);
    }
  } else if (!g_strcmp0 (element_name, "clip")) {
    if (!priv->subproject)
      ges_base_xml_formatter_end_current_clip (GES_BASE_XML_FORMATTER (self));
  }
}

static void
_error_parsing (GMarkupParseContext * context, GError * error,
    gpointer user_data)
{
  GST_WARNING ("Error occurred when parsing %s", error->message);
}

/***********************************************
 *                                             *
 *            Saving implementation            *
 *                                             *
 ***********************************************/

/* XML writting utils */
static inline void
string_add_indents (GString * str, guint depth, gboolean prepend)
{
  gint i;
  for (i = 0; i < depth; i++)
    prepend ? g_string_prepend (str, "  ") : g_string_append (str, "  ");
}

static inline void
string_append_with_depth (GString * str, const gchar * string, guint depth)
{
  string_add_indents (str, depth, FALSE);
  g_string_append (str, string);
}

static inline void
append_escaped (GString * str, gchar * tmpstr, guint depth)
{
  string_append_with_depth (str, tmpstr, depth);
  g_free (tmpstr);
}

gboolean
ges_util_can_serialize_spec (GParamSpec * spec)
{
  if (!(spec->flags & G_PARAM_WRITABLE)) {
    GST_LOG ("%s from %s is not writable",
        spec->name, g_type_name (spec->owner_type));

    return FALSE;
  } else if (spec->flags & G_PARAM_CONSTRUCT_ONLY) {
    GST_LOG ("%s from %s is construct only",
        spec->name, g_type_name (spec->owner_type));

    return FALSE;
  } else if (spec->flags & GES_PARAM_NO_SERIALIZATION &&
      g_type_is_a (spec->owner_type, GES_TYPE_TIMELINE_ELEMENT)) {
    GST_LOG ("%s from %s is set as GES_PARAM_NO_SERIALIZATION",
        spec->name, g_type_name (spec->owner_type));

    return FALSE;
  } else if (g_type_is_a (G_PARAM_SPEC_VALUE_TYPE (spec), G_TYPE_OBJECT)) {
    GST_LOG ("%s from %s contains GObject, can't serialize that.",
        spec->name, g_type_name (spec->owner_type));

    return FALSE;
  } else if ((g_type_is_a (spec->owner_type, GST_TYPE_OBJECT) &&
          !g_strcmp0 (spec->name, "name"))) {

    GST_LOG ("We do not want to serialize the name of GstObjects.");
    return FALSE;
  } else if (G_PARAM_SPEC_VALUE_TYPE (spec) == G_TYPE_GTYPE) {
    GST_LOG ("%s from %s contains a GType, can't serialize.",
        spec->name, g_type_name (spec->owner_type));
    return FALSE;
  }

  return TRUE;
}

static inline void
_init_value_from_spec_for_serialization (GValue * value, GParamSpec * spec)
{

  if (g_type_is_a (spec->value_type, G_TYPE_ENUM) ||
      g_type_is_a (spec->value_type, G_TYPE_FLAGS))
    g_value_init (value, G_TYPE_INT);
  else
    g_value_init (value, spec->value_type);
}

static gchar *
_serialize_properties (GObject * object, gint * ret_n_props,
    const gchar * fieldname, ...)
{
  gchar *ret;
  guint n_props, j;
  GParamSpec *spec, **pspecs;
  GObjectClass *class = G_OBJECT_GET_CLASS (object);
  GstStructure *structure = gst_structure_new_empty ("properties");

  pspecs = g_object_class_list_properties (class, &n_props);
  for (j = 0; j < n_props; j++) {
    GValue val = { 0 };

    spec = pspecs[j];
    if (!ges_util_can_serialize_spec (spec))
      continue;

    _init_value_from_spec_for_serialization (&val, spec);
    g_object_get_property (object, spec->name, &val);
    if (gst_value_compare (g_param_spec_get_default_value (spec),
            &val) == GST_VALUE_EQUAL) {
      GST_INFO ("Ignoring %s as it is using the default value", spec->name);
      goto next;
    }

    if (spec->value_type == GST_TYPE_CAPS) {
      gchar *caps_str;
      const GstCaps *caps = gst_value_get_caps (&val);

      caps_str = gst_caps_to_string (caps);
      gst_structure_set (structure, spec->name, G_TYPE_STRING, caps_str, NULL);
      g_free (caps_str);
      goto next;
    }

    gst_structure_set_value (structure, spec->name, &val);

  next:
    g_value_unset (&val);
  }
  g_free (pspecs);

  if (fieldname) {
    va_list varargs;
    va_start (varargs, fieldname);
    gst_structure_remove_fields_valist (structure, fieldname, varargs);
    va_end (varargs);
  }

  ret = gst_structure_to_string (structure);
  if (ret_n_props)
    *ret_n_props = gst_structure_n_fields (structure);
  gst_structure_free (structure);

  return ret;
}

static void
project_loaded_cb (GESProject * project, GESTimeline * timeline,
    SubprojectData * data)
{
  g_main_loop_quit (data->ml);
}

static void
error_loading_asset_cb (GESProject * project, GError * err,
    const gchar * unused_id, GType extractable_type, SubprojectData * data)
{
  data->error = g_error_copy (err);
  g_main_loop_quit (data->ml);
}

static gboolean
_save_subproject (GESXmlFormatter * self, GString * str, GESProject * project,
    GESAsset * subproject, GError ** error, guint depth)
{
  GString *substr;
  GESTimeline *timeline;
  gchar *properties, *metas;
  GESXmlFormatterPrivate *priv = self->priv;
  GMainContext *context = g_main_context_get_thread_default ();
  const gchar *id = ges_asset_get_id (subproject);
  SubprojectData data = { 0, };

  if (!g_strcmp0 (ges_asset_get_id (GES_ASSET (project)), id)) {
    g_set_error (error, G_MARKUP_ERROR,
        G_MARKUP_ERROR_INVALID_CONTENT,
        "Project %s trying to recurse into itself", id);
    return FALSE;
  }

  G_LOCK (uri_subprojects_map_lock);
  g_hash_table_insert (priv->subprojects_map, g_strdup (id), g_strdup (id));
  G_UNLOCK (uri_subprojects_map_lock);
  timeline = GES_TIMELINE (ges_asset_extract (subproject, error));
  if (!timeline) {
    return FALSE;
  }

  if (!context)
    context = g_main_context_default ();

  data.ml = g_main_loop_new (context, TRUE);
  g_signal_connect (subproject, "loaded", (GCallback) project_loaded_cb, &data);
  g_signal_connect (subproject, "error-loading-asset",
      (GCallback) error_loading_asset_cb, &data);
  g_main_loop_run (data.ml);

  g_signal_handlers_disconnect_by_func (subproject, project_loaded_cb, &data);
  g_signal_handlers_disconnect_by_func (subproject, error_loading_asset_cb,
      &data);
  g_main_loop_unref (data.ml);
  if (data.error) {
    g_propagate_error (error, data.error);
    return FALSE;
  }

  subproject = ges_extractable_get_asset (GES_EXTRACTABLE (timeline));
  substr = g_string_new (NULL);
  properties = _serialize_properties (G_OBJECT (subproject), NULL, NULL);
  metas = ges_meta_container_metas_to_string (GES_META_CONTAINER (subproject));
  append_escaped (str,
      g_markup_printf_escaped
      ("      <asset id='%s' extractable-type-name='%s' properties='%s' metadatas='%s'>\n",
          ges_asset_get_id (subproject),
          g_type_name (ges_asset_get_extractable_type (subproject)), properties,
          metas), depth);
  self->priv->min_version = MAX (self->priv->min_version, 6);
  g_free (properties);
  g_free (metas);

  depth += 4;
  GST_DEBUG_OBJECT (self, "Saving subproject %s (depth: %d)",
      ges_asset_get_id (subproject), depth / 4);
  if (!_save_project (GES_FORMATTER (self), substr, GES_PROJECT (subproject),
          timeline, error, depth)) {
    g_string_free (substr, TRUE);
    g_object_unref (subproject);
    goto err;
  }
  GST_DEBUG_OBJECT (self, "DONE Saving subproject %s",
      ges_asset_get_id (subproject));
  depth -= 4;

  g_string_append (str, substr->str);
  g_string_free (substr, TRUE);
  string_append_with_depth (str, "      </asset>\n", depth);

err:
  g_object_unref (subproject);

  return TRUE;
}

static gint
sort_assets (GESAsset * a, GESAsset * b)
{
  if (GES_IS_PROJECT (a))
    return -1;

  if (GES_IS_PROJECT (b))
    return 1;

  return 0;
}

static void
_serialize_streams (GESXmlFormatter * self, GString * str,
    GESUriClipAsset * asset, GError ** error, guint depth)
{
  const GList *tmp, *streams = ges_uri_clip_asset_get_stream_assets (asset);

  for (tmp = streams; tmp; tmp = tmp->next) {
    gchar *properties, *metas, *capsstr;
    const gchar *id = ges_asset_get_id (tmp->data);
    GstDiscovererStreamInfo *sinfo =
        ges_uri_source_asset_get_stream_info (tmp->data);
    GstCaps *caps = gst_discoverer_stream_info_get_caps (sinfo);

    properties = _serialize_properties (tmp->data, NULL, NULL);
    metas = ges_meta_container_metas_to_string (tmp->data);
    capsstr = gst_caps_to_string (caps);

    append_escaped (str,
        g_markup_printf_escaped
        ("        <stream-info id='%s' extractable-type-name='%s' properties='%s' metadatas='%s' caps='%s'/>\n",
            id, g_type_name (ges_asset_get_extractable_type (tmp->data)),
            properties, metas, capsstr), depth);
    self->priv->min_version = MAX (self->priv->min_version, 6);
    g_free (metas);
    g_free (properties);
    g_free (capsstr);
    gst_caps_unref (caps);
  }

}

static inline gboolean
_save_assets (GESXmlFormatter * self, GString * str, GESProject * project,
    GError ** error, guint depth)
{
  gchar *properties, *metas;
  GESAsset *asset, *proxy;
  GList *assets, *tmp;
  const gchar *id;
  GESXmlFormatterPrivate *priv = self->priv;

  assets = ges_project_list_assets (project, GES_TYPE_EXTRACTABLE);
  for (tmp = g_list_sort (assets, (GCompareFunc) sort_assets); tmp;
      tmp = tmp->next) {
    asset = GES_ASSET (tmp->data);
    id = ges_asset_get_id (asset);

    if (GES_IS_PROJECT (asset)) {
      if (!_save_subproject (self, str, project, asset, error, depth))
        return FALSE;

      continue;
    }

    if (ges_asset_get_extractable_type (asset) == GES_TYPE_URI_CLIP) {
      G_LOCK (uri_subprojects_map_lock);
      if (g_hash_table_contains (priv->subprojects_map, id)) {
        id = g_hash_table_lookup (priv->subprojects_map, id);

        GST_DEBUG_OBJECT (self, "Using subproject %s", id);
      }
      G_UNLOCK (uri_subprojects_map_lock);
    }

    properties = _serialize_properties (G_OBJECT (asset), NULL, NULL);
    metas = ges_meta_container_metas_to_string (GES_META_CONTAINER (asset));
    append_escaped (str,
        g_markup_printf_escaped
        ("      <asset id='%s' extractable-type-name='%s' properties='%s' metadatas='%s' ",
            id, g_type_name (ges_asset_get_extractable_type (asset)),
            properties, metas), depth);

    /*TODO Save the whole list of proxies */
    proxy = ges_asset_get_proxy (asset);
    if (proxy) {
      const gchar *proxy_id = ges_asset_get_id (proxy);

      if (ges_asset_get_extractable_type (asset) == GES_TYPE_URI_CLIP) {
        G_LOCK (uri_subprojects_map_lock);
        if (g_hash_table_contains (priv->subprojects_map, proxy_id)) {
          proxy_id = g_hash_table_lookup (priv->subprojects_map, proxy_id);

          GST_DEBUG_OBJECT (self, "Using subproject %s", id);
        }
        G_UNLOCK (uri_subprojects_map_lock);
      }
      append_escaped (str, g_markup_printf_escaped (" proxy-id='%s' ",
              proxy_id), depth);

      if (!g_list_find (assets, proxy)) {
        assets = g_list_append (assets, gst_object_ref (proxy));

        if (!tmp->next)
          tmp->next = g_list_last (assets);
      }

      self->priv->min_version = MAX (self->priv->min_version, 3);
    }
    g_string_append (str, ">\n");

    if (GES_IS_URI_CLIP_ASSET (asset)) {
      _serialize_streams (self, str, GES_URI_CLIP_ASSET (asset), error, depth);
    }

    string_append_with_depth (str, "      </asset>\n", depth);
    g_free (properties);
    g_free (metas);
  }

  g_list_free_full (assets, gst_object_unref);

  return TRUE;
}

static inline void
_save_tracks (GESXmlFormatter * self, GString * str, GESTimeline * timeline,
    guint depth)
{
  gchar *strtmp, *metas;
  GESTrack *track;
  GList *tmp, *tracks;
  char *properties;

  guint nb_tracks = 0;

  tracks = ges_timeline_get_tracks (timeline);
  for (tmp = tracks; tmp; tmp = tmp->next) {
    track = GES_TRACK (tmp->data);
    properties = _serialize_properties (G_OBJECT (track), NULL, "caps", NULL);
    strtmp = gst_caps_to_string (ges_track_get_caps (track));
    metas = ges_meta_container_metas_to_string (GES_META_CONTAINER (track));
    append_escaped (str,
        g_markup_printf_escaped
        ("      <track caps='%s' track-type='%i' track-id='%i' properties='%s' metadatas='%s'/>\n",
            strtmp, track->type, nb_tracks++, properties, metas), depth);
    g_free (strtmp);
    g_free (metas);
    g_free (properties);
  }
  g_list_free_full (tracks, gst_object_unref);
}

static inline void
_save_children_properties (GString * str, GESTimelineElement * element,
    guint depth)
{
  GstStructure *structure;
  GParamSpec **pspecs, *spec;
  guint i, n_props;
  gchar *struct_str;

  pspecs = ges_timeline_element_list_children_properties (element, &n_props);

  structure = gst_structure_new_empty ("properties");
  for (i = 0; i < n_props; i++) {
    GValue val = { 0 };
    spec = pspecs[i];

    if (ges_util_can_serialize_spec (spec)) {
      gchar *spec_name =
          g_strdup_printf ("%s::%s", g_type_name (spec->owner_type),
          spec->name);

      _init_value_from_spec_for_serialization (&val, spec);
      ges_timeline_element_get_child_property_by_pspec (element, spec, &val);
      gst_structure_set_value (structure, spec_name, &val);

      g_free (spec_name);
      g_value_unset (&val);
    }
    g_param_spec_unref (spec);
  }
  g_free (pspecs);

  struct_str = gst_structure_to_string (structure);
  append_escaped (str,
      g_markup_printf_escaped (" children-properties='%s'", struct_str), 0);
  gst_structure_free (structure);
  g_free (struct_str);
}

/* TODO : Use this function for every track element with controllable properties */
static inline void
_save_keyframes (GString * str, GESTrackElement * trackelement, gint index,
    guint depth)
{
  GHashTable *bindings_hashtable;
  GHashTableIter iter;
  gpointer key, value;

  bindings_hashtable =
      ges_track_element_get_all_control_bindings (trackelement);

  g_hash_table_iter_init (&iter, bindings_hashtable);

  /* We iterate over the bindings, and save the timed values */
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    if (GST_IS_DIRECT_CONTROL_BINDING ((GstControlBinding *) value)) {
      GstControlSource *source;
      gboolean absolute = FALSE;
      GstDirectControlBinding *binding;

      binding = (GstDirectControlBinding *) value;

      g_object_get (binding, "control-source", &source,
          "absolute", &absolute, NULL);

      if (GST_IS_INTERPOLATION_CONTROL_SOURCE (source)) {
        GstTimedValue *timed_values;
        gsize n_values;
        GstInterpolationMode mode;

        append_escaped (str,
            g_markup_printf_escaped
            ("            <binding type='%s' source_type='interpolation' property='%s'",
                absolute ? "direct-absolute" : "direct", (gchar *) key), depth);

        g_object_get (source, "mode", &mode, NULL);
        append_escaped (str, g_markup_printf_escaped (" mode='%d'", mode),
            depth);
        append_escaped (str, g_markup_printf_escaped (" track_id='%d'", index),
            depth);
        append_escaped (str, g_markup_printf_escaped (" values ='"), depth);
        timed_values =
            gst_timed_value_control_source_list_control_points
            (GST_TIMED_VALUE_CONTROL_SOURCE (source), &n_values);

        if (timed_values) {
          gsize i;

          for (i = 0; i < n_values; i++) {
            gchar strbuf[G_ASCII_DTOSTR_BUF_SIZE];
            GstTimedValue *value;

            value = &timed_values[i];
            append_escaped (str, g_markup_printf_escaped (" %" G_GUINT64_FORMAT
                    ":%s ", value->timestamp, g_ascii_dtostr (strbuf,
                        G_ASCII_DTOSTR_BUF_SIZE, value->value)), depth);
          }

          g_free (timed_values);
        }

        append_escaped (str, g_markup_printf_escaped ("'/>\n"), depth);
      } else
        GST_DEBUG ("control source not in [interpolation]");

      gst_object_unref (source);
    } else
      GST_DEBUG ("Binding type not in [direct, direct-absolute]");
  }
}

static inline void
_save_effect (GString * str, guint clip_id, GESTrackElement * trackelement,
    GESTimeline * timeline, guint depth)
{
  GESTrack *tck;
  GList *tmp, *tracks;
  gchar *properties, *metas;
  guint track_id = 0;
  gboolean serialize;
  gchar *extractable_id;

  g_object_get (trackelement, "serialize", &serialize, NULL);
  if (!serialize) {

    GST_DEBUG_OBJECT (trackelement, "Should not be serialized");

    return;
  }

  tck = ges_track_element_get_track (trackelement);
  if (tck == NULL) {
    GST_WARNING_OBJECT (trackelement, " Not in any track, can not save it");

    return;
  }

  tracks = ges_timeline_get_tracks (timeline);
  for (tmp = tracks; tmp; tmp = tmp->next) {
    if (tmp->data == tck)
      break;
    track_id++;
  }
  g_list_free_full (tracks, gst_object_unref);

  properties = _serialize_properties (G_OBJECT (trackelement), NULL, "start",
      "duration", "locked", "name", "priority", NULL);
  metas =
      ges_meta_container_metas_to_string (GES_META_CONTAINER (trackelement));
  extractable_id = ges_extractable_get_id (GES_EXTRACTABLE (trackelement));
  append_escaped (str,
      g_markup_printf_escaped ("          <effect asset-id='%s' clip-id='%u'"
          " type-name='%s' track-type='%i' track-id='%i' properties='%s' metadatas='%s'",
          extractable_id, clip_id,
          g_type_name (G_OBJECT_TYPE (trackelement)), tck->type, track_id,
          properties, metas), depth);
  g_free (extractable_id);
  g_free (properties);
  g_free (metas);

  _save_children_properties (str, GES_TIMELINE_ELEMENT (trackelement), depth);
  append_escaped (str, g_markup_printf_escaped (">\n"), depth);

  _save_keyframes (str, trackelement, -1, depth);

  append_escaped (str, g_markup_printf_escaped ("          </effect>\n"),
      depth);
}

static inline void
_save_layer_track_activness (GESXmlFormatter * self, GESLayer * layer,
    GString * str, GESTimeline * timeline, guint depth)
{
  guint nb_tracks = 0, i;
  GList *tmp, *tracks = ges_timeline_get_tracks (timeline);
  GArray *deactivated_tracks = g_array_new (TRUE, FALSE, sizeof (gint32));

  for (tmp = tracks; tmp; tmp = tmp->next, nb_tracks++) {
    if (!ges_layer_get_active_for_track (layer, tmp->data))
      g_array_append_val (deactivated_tracks, nb_tracks);
  }

  if (!deactivated_tracks->len) {
    g_string_append (str, ">\n");
    goto done;
  }

  self->priv->min_version = MAX (self->priv->min_version, 7);
  g_string_append (str, " deactivated-tracks='");
  for (i = 0; i < deactivated_tracks->len; i++)
    g_string_append_printf (str, "%d ", g_array_index (deactivated_tracks, gint,
            i));
  g_string_append (str, "'>\n");

done:
  g_array_free (deactivated_tracks, TRUE);
  g_list_free_full (tracks, gst_object_unref);
}

static void
_save_source (GESXmlFormatter * self, GString * str,
    GESTimelineElement * element, GESTimeline * timeline, GList * tracks,
    guint depth)
{
  gint index, n_props;
  gboolean serialize;
  gchar *properties, *metas;

  if (!GES_IS_SOURCE (element))
    return;

  g_object_get (element, "serialize", &serialize, NULL);
  if (!serialize) {
    GST_DEBUG_OBJECT (element, "Should not be serialized");
    return;
  }

  index =
      g_list_index (tracks,
      ges_track_element_get_track (GES_TRACK_ELEMENT (element)));
  append_escaped (str,
      g_markup_printf_escaped
      ("          <source track-id='%i' ", index), depth);

  properties = _serialize_properties (G_OBJECT (element), &n_props,
      "in-point", "priority", "start", "duration", "track", "track-type"
      "uri", "name", "max-duration", NULL);

  /* Try as possible to allow older versions of GES to load the files */
  if (n_props) {
    self->priv->min_version = MAX (self->priv->min_version, 7);
    g_string_append_printf (str, "properties='%s' ", properties);
  }
  g_free (properties);

  metas = ges_meta_container_metas_to_string (GES_META_CONTAINER (element));
  g_string_append_printf (str, "metadatas='%s' ", metas);
  g_free (metas);

  _save_children_properties (str, element, depth);
  append_escaped (str, g_markup_printf_escaped (">\n"), depth);
  _save_keyframes (str, GES_TRACK_ELEMENT (element), index, depth);
  append_escaped (str, g_markup_printf_escaped ("          </source>\n"),
      depth);
}

static inline void
_save_layers (GESXmlFormatter * self, GString * str, GESTimeline * timeline,
    guint depth)
{
  gchar *properties, *metas;
  GESLayer *layer;
  GESClip *clip;
  GList *tmplayer, *tmpclip, *clips;
  GESXmlFormatterPrivate *priv = self->priv;

  for (tmplayer = timeline->layers; tmplayer; tmplayer = tmplayer->next) {
    guint priority;
    layer = GES_LAYER (tmplayer->data);

    priority = ges_layer_get_priority (layer);
    properties =
        _serialize_properties (G_OBJECT (layer), NULL, "priority", NULL);
    metas = ges_meta_container_metas_to_string (GES_META_CONTAINER (layer));
    append_escaped (str,
        g_markup_printf_escaped
        ("      <layer priority='%i' properties='%s' metadatas='%s'",
            priority, properties, metas), depth);
    g_free (properties);
    g_free (metas);

    _save_layer_track_activness (self, layer, str, timeline, depth);

    clips = ges_layer_get_clips (layer);
    for (tmpclip = clips; tmpclip; tmpclip = tmpclip->next) {
      GList *effects, *tmpeffect;
      GList *tmptrackelement;
      GList *tracks;
      gboolean serialize;
      gchar *extractable_id;

      clip = GES_CLIP (tmpclip->data);

      g_object_get (clip, "serialize", &serialize, NULL);
      if (!serialize) {
        GST_DEBUG_OBJECT (clip, "Should not be serialized");
        continue;
      }

      /* We escape all mandatrorry properties that are handled sparetely
       * and vtype for StandarTransition as it is the asset ID */
      properties = _serialize_properties (G_OBJECT (clip), NULL,
          "supported-formats", "rate", "in-point", "start", "duration",
          "max-duration", "priority", "vtype", "uri", NULL);
      extractable_id = ges_extractable_get_id (GES_EXTRACTABLE (clip));
      if (GES_IS_URI_CLIP (clip)) {
        G_LOCK (uri_subprojects_map_lock);
        if (g_hash_table_contains (priv->subprojects_map, extractable_id)) {
          gchar *new_extractable_id =
              g_strdup (g_hash_table_lookup (priv->subprojects_map,
                  extractable_id));
          g_free (extractable_id);
          extractable_id = new_extractable_id;
        }
        G_UNLOCK (uri_subprojects_map_lock);
      }
      metas = ges_meta_container_metas_to_string (GES_META_CONTAINER (clip));
      append_escaped (str,
          g_markup_printf_escaped ("        <clip id='%i' asset-id='%s'"
              " type-name='%s' layer-priority='%i' track-types='%i' start='%"
              G_GUINT64_FORMAT "' duration='%" G_GUINT64_FORMAT "' inpoint='%"
              G_GUINT64_FORMAT "' rate='%d' properties='%s' metadatas='%s'",
              priv->nbelements, extractable_id,
              g_type_name (G_OBJECT_TYPE (clip)), priority,
              ges_clip_get_supported_formats (clip), _START (clip),
              _DURATION (clip), _INPOINT (clip), 0, properties, metas), depth);
      g_free (metas);

      if (GES_IS_TRANSITION_CLIP (clip)) {
        _save_children_properties (str, GES_TIMELINE_ELEMENT (clip), depth);
        self->priv->min_version = MAX (self->priv->min_version, 4);
      }
      g_string_append (str, ">\n");

      g_free (extractable_id);
      g_free (properties);

      g_hash_table_insert (self->priv->element_id, clip,
          GINT_TO_POINTER (priv->nbelements));


      /* Effects must always be serialized in the right priority order.
       * List order is guaranteed by the fact that ges_clip_get_top_effects
       * sorts the effects. */
      effects = ges_clip_get_top_effects (clip);
      for (tmpeffect = effects; tmpeffect; tmpeffect = tmpeffect->next) {
        _save_effect (str, priv->nbelements,
            GES_TRACK_ELEMENT (tmpeffect->data), timeline, depth);
      }
      g_list_free (effects);
      tracks = ges_timeline_get_tracks (timeline);

      for (tmptrackelement = GES_CONTAINER_CHILDREN (clip); tmptrackelement;
          tmptrackelement = tmptrackelement->next) {
        _save_source (self, str, tmptrackelement->data, timeline, tracks,
            depth);
      }
      g_list_free_full (tracks, gst_object_unref);

      string_append_with_depth (str, "        </clip>\n", depth);

      priv->nbelements++;
    }
    g_list_free_full (clips, (GDestroyNotify) gst_object_unref);
    string_append_with_depth (str, "      </layer>\n", depth);
  }
}

static void
_save_group (GESXmlFormatter * self, GString * str, GList ** seen_groups,
    GESGroup * group, guint depth)
{
  GList *tmp;
  gboolean serialize;
  gchar *properties, *metadatas;

  g_object_get (group, "serialize", &serialize, NULL);
  if (!serialize) {

    GST_DEBUG_OBJECT (group, "Should not be serialized");

    return;
  }

  if (g_list_find (*seen_groups, group)) {
    GST_DEBUG_OBJECT (group, "Already serialized");

    return;
  }

  *seen_groups = g_list_prepend (*seen_groups, group);
  for (tmp = GES_CONTAINER_CHILDREN (group); tmp; tmp = tmp->next) {
    if (GES_IS_GROUP (tmp->data)) {
      _save_group (self, str, seen_groups,
          GES_GROUP (GES_TIMELINE_ELEMENT (tmp->data)), depth);
    }
  }

  properties = _serialize_properties (G_OBJECT (group), NULL, NULL);

  metadatas = ges_meta_container_metas_to_string (GES_META_CONTAINER (group));
  self->priv->min_version = MAX (self->priv->min_version, 5);

  string_add_indents (str, depth, FALSE);
  g_string_append_printf (str,
      "        <group id='%d' properties='%s' metadatas='%s'>\n",
      self->priv->nbelements, properties, metadatas);
  g_free (properties);
  g_free (metadatas);
  g_hash_table_insert (self->priv->element_id, group,
      GINT_TO_POINTER (self->priv->nbelements));
  self->priv->nbelements++;

  for (tmp = GES_CONTAINER_CHILDREN (group); tmp; tmp = tmp->next) {
    gint id = GPOINTER_TO_INT (g_hash_table_lookup (self->priv->element_id,
            tmp->data));

    string_add_indents (str, depth, FALSE);
    g_string_append_printf (str, "          <child id='%d' name='%s'/>\n", id,
        GES_TIMELINE_ELEMENT_NAME (tmp->data));
  }
  string_append_with_depth (str, "        </group>\n", depth);
}

static void
_save_groups (GESXmlFormatter * self, GString * str, GESTimeline * timeline,
    guint depth)
{
  GList *tmp;
  GList *seen_groups = NULL;

  string_append_with_depth (str, "      <groups>\n", depth);
  for (tmp = ges_timeline_get_groups (timeline); tmp; tmp = tmp->next) {
    _save_group (self, str, &seen_groups, tmp->data, depth);
  }
  g_list_free (seen_groups);
  string_append_with_depth (str, "      </groups>\n", depth);
}

static inline void
_save_timeline (GESXmlFormatter * self, GString * str, GESTimeline * timeline,
    guint depth)
{
  gchar *properties = NULL, *metas = NULL;

  properties =
      _serialize_properties (G_OBJECT (timeline), NULL, "update", "name",
      "async-handling", "message-forward", NULL);

  ges_meta_container_set_uint64 (GES_META_CONTAINER (timeline), "duration",
      ges_timeline_get_duration (timeline));
  metas = ges_meta_container_metas_to_string (GES_META_CONTAINER (timeline));
  append_escaped (str,
      g_markup_printf_escaped
      ("    <timeline properties='%s' metadatas='%s'>\n", properties, metas),
      depth);

  _save_tracks (self, str, timeline, depth);
  _save_layers (self, str, timeline, depth);
  _save_groups (self, str, timeline, depth);

  string_append_with_depth (str, "    </timeline>\n", depth);

  g_free (properties);
  g_free (metas);
}

static void
_save_stream_profiles (GESXmlFormatter * self, GString * str,
    GstEncodingProfile * sprof, const gchar * profilename, guint id,
    guint depth)
{
  gchar *tmpc;
  GstCaps *tmpcaps;
  GstStructure *properties;
  const gchar *preset, *preset_name, *name, *description;

  append_escaped (str,
      g_markup_printf_escaped
      ("        <stream-profile parent='%s' id='%d' type='%s' "
          "presence='%d' ", profilename, id,
          gst_encoding_profile_get_type_nick (sprof),
          gst_encoding_profile_get_presence (sprof)), depth);

  if (!gst_encoding_profile_is_enabled (sprof)) {
    append_escaped (str, g_strdup ("enabled='0' "), depth);

    self->priv->min_version = MAX (self->priv->min_version, 2);
  }

  tmpcaps = gst_encoding_profile_get_format (sprof);
  if (tmpcaps) {
    tmpc = gst_caps_to_string (tmpcaps);
    append_escaped (str, g_markup_printf_escaped ("format='%s' ", tmpc), depth);
    gst_caps_unref (tmpcaps);
    g_free (tmpc);
  }

  name = gst_encoding_profile_get_name (sprof);
  if (name)
    append_escaped (str, g_markup_printf_escaped ("name='%s' ", name), depth);

  description = gst_encoding_profile_get_description (sprof);
  if (description)
    append_escaped (str, g_markup_printf_escaped ("description='%s' ",
            description), depth);

  preset = gst_encoding_profile_get_preset (sprof);
  if (preset) {
    append_escaped (str, g_markup_printf_escaped ("preset='%s' ", preset),
        depth);
  }

  properties = gst_encoding_profile_get_element_properties (sprof);
  if (properties) {
    gchar *props_str = gst_structure_to_string (properties);

    append_escaped (str,
        g_markup_printf_escaped ("preset-properties='%s' ", props_str), depth);
    g_free (props_str);
    gst_structure_free (properties);
  }

  preset_name = gst_encoding_profile_get_preset_name (sprof);
  if (preset_name)
    append_escaped (str, g_markup_printf_escaped ("preset-name='%s' ",
            preset_name), depth);

  tmpcaps = gst_encoding_profile_get_restriction (sprof);
  if (tmpcaps) {
    tmpc = gst_caps_to_string (tmpcaps);
    append_escaped (str, g_markup_printf_escaped ("restriction='%s' ", tmpc),
        depth);
    gst_caps_unref (tmpcaps);
    g_free (tmpc);
  }

  if (GST_IS_ENCODING_VIDEO_PROFILE (sprof)) {
    GstEncodingVideoProfile *vp = (GstEncodingVideoProfile *) sprof;

    append_escaped (str,
        g_markup_printf_escaped ("pass='%d' variableframerate='%i' ",
            gst_encoding_video_profile_get_pass (vp),
            gst_encoding_video_profile_get_variableframerate (vp)), depth);
  }

  g_string_append (str, "/>\n");
}

static inline void
_save_encoding_profiles (GESXmlFormatter * self, GString * str,
    GESProject * project, guint depth)
{
  GstCaps *profformat;
  GstStructure *properties;
  const gchar *profname, *profdesc, *profpreset, *proftype, *profpresetname;

  const GList *tmp;
  GList *profiles = g_list_reverse (g_list_copy ((GList *)
          ges_project_list_encoding_profiles (project)));

  for (tmp = profiles; tmp; tmp = tmp->next) {
    GstEncodingProfile *prof = GST_ENCODING_PROFILE (tmp->data);

    profname = gst_encoding_profile_get_name (prof);
    profdesc = gst_encoding_profile_get_description (prof);
    profpreset = gst_encoding_profile_get_preset (prof);
    profpresetname = gst_encoding_profile_get_preset_name (prof);
    proftype = gst_encoding_profile_get_type_nick (prof);

    append_escaped (str,
        g_markup_printf_escaped
        ("      <encoding-profile name='%s' description='%s' type='%s' ",
            profname, profdesc, proftype), depth);

    if (profpreset) {
      append_escaped (str, g_markup_printf_escaped ("preset='%s' ",
              profpreset), depth);
    }

    properties = gst_encoding_profile_get_element_properties (prof);
    if (properties) {
      gchar *props_str = gst_structure_to_string (properties);

      append_escaped (str,
          g_markup_printf_escaped ("preset-properties='%s' ", props_str),
          depth);
      g_free (props_str);
      gst_structure_free (properties);
    }

    if (profpresetname)
      append_escaped (str, g_markup_printf_escaped ("preset-name='%s' ",
              profpresetname), depth);

    profformat = gst_encoding_profile_get_format (prof);
    if (profformat) {
      gchar *format = gst_caps_to_string (profformat);
      append_escaped (str, g_markup_printf_escaped ("format='%s' ", format),
          depth);
      g_free (format);
      gst_caps_unref (profformat);
    }

    g_string_append (str, ">\n");

    if (GST_IS_ENCODING_CONTAINER_PROFILE (prof)) {
      guint i = 0;
      const GList *tmp2;
      GstEncodingContainerProfile *container_prof;

      container_prof = GST_ENCODING_CONTAINER_PROFILE (prof);
      for (tmp2 = gst_encoding_container_profile_get_profiles (container_prof);
          tmp2; tmp2 = tmp2->next, i++) {
        GstEncodingProfile *sprof = (GstEncodingProfile *) tmp2->data;
        _save_stream_profiles (self, str, sprof, profname, i, depth);
      }
    }
    append_escaped (str,
        g_markup_printf_escaped ("      </encoding-profile>\n"), depth);
  }
  g_list_free (profiles);
}

static GString *
_save (GESFormatter * formatter, GESTimeline * timeline, GError ** error)
{
  GString *str;
  GESProject *project;
  GESXmlFormatterPrivate *priv = _GET_PRIV (formatter);

  priv->min_version = 1;
  project = formatter->project;
  str = priv->str = g_string_new (NULL);

  return _save_project (formatter, str, project, timeline, error, 0);
}

static GString *
_save_project (GESFormatter * formatter, GString * str, GESProject * project,
    GESTimeline * timeline, GError ** error, guint depth)
{
  gchar *projstr = NULL, *version;
  gchar *properties = NULL, *metas = NULL;
  GESXmlFormatter *self = GES_XML_FORMATTER (formatter);
  GESXmlFormatterPrivate *priv = _GET_PRIV (formatter);

  properties = _serialize_properties (G_OBJECT (project), NULL, NULL);
  metas = ges_meta_container_metas_to_string (GES_META_CONTAINER (project));
  append_escaped (str,
      g_markup_printf_escaped ("  <project properties='%s' metadatas='%s'>\n",
          properties, metas), depth);
  g_free (properties);
  g_free (metas);

  string_append_with_depth (str, "    <encoding-profiles>\n", depth);
  _save_encoding_profiles (GES_XML_FORMATTER (formatter), str, project, depth);
  string_append_with_depth (str, "    </encoding-profiles>\n", depth);

  string_append_with_depth (str, "    <ressources>\n", depth);
  if (!_save_assets (self, str, project, error, depth)) {
    g_string_free (str, TRUE);
    return NULL;
  }
  string_append_with_depth (str, "    </ressources>\n", depth);

  _save_timeline (self, str, timeline, depth);
  string_append_with_depth (str, "  </project>\n", depth);
  string_append_with_depth (str, "</ges>\n", depth);

  projstr = g_strdup_printf ("<ges version='%i.%i'>\n", API_VERSION,
      priv->min_version);
  g_string_prepend (str, projstr);
  string_add_indents (str, depth, TRUE);
  g_free (projstr);

  ges_meta_container_set_int (GES_META_CONTAINER (project),
      GES_META_FORMAT_VERSION, priv->min_version);

  version = g_strdup_printf ("%d.%d", API_VERSION,
      GES_XML_FORMATTER (formatter)->priv->min_version);

  ges_meta_container_set_string (GES_META_CONTAINER (project),
      GES_META_FORMAT_VERSION, version);

  g_free (version);

  priv->str = NULL;

  return str;
}

static void
_setup_subprojects_map (GESXmlFormatterPrivate * priv, const gchar * uri)
{
  GHashTable *subprojects_map;

  G_LOCK (uri_subprojects_map_lock);
  if (!uri_subprojects_map)
    uri_subprojects_map =
        g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
        (GDestroyNotify) g_hash_table_unref);

  subprojects_map = g_hash_table_lookup (uri_subprojects_map, uri);
  if (!subprojects_map) {
    subprojects_map =
        g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    g_hash_table_insert (uri_subprojects_map, g_strdup (uri), subprojects_map);
  }
  priv->subprojects_map = subprojects_map;
  G_UNLOCK (uri_subprojects_map_lock);

}

void
ges_xml_formatter_deinit (void)
{
  GST_DEBUG ("Deinit");
  G_LOCK (uri_subprojects_map_lock);
  if (uri_subprojects_map) {
    g_hash_table_unref (uri_subprojects_map);
    uri_subprojects_map = NULL;
  }
  G_UNLOCK (uri_subprojects_map_lock);
}

static gboolean
_save_to_uri (GESFormatter * formatter, GESTimeline * timeline,
    const gchar * uri, gboolean overwrite, GError ** error)
{
  _setup_subprojects_map (_GET_PRIV (formatter), uri);
  return GES_FORMATTER_CLASS (parent_class)->save_to_uri (formatter, timeline,
      uri, overwrite, error);
}

static gboolean
_can_load_uri (GESFormatter * formatter, const gchar * uri, GError ** error)
{
  _setup_subprojects_map (_GET_PRIV (formatter), uri);
  return GES_FORMATTER_CLASS (parent_class)->can_load_uri (formatter, uri,
      error);
}

static gboolean
_load_from_uri (GESFormatter * formatter, GESTimeline * timeline,
    const gchar * uri, GError ** error)
{
  _setup_subprojects_map (_GET_PRIV (formatter), uri);
  return GES_FORMATTER_CLASS (parent_class)->load_from_uri (formatter, timeline,
      uri, error);
}

/***********************************************
 *                                             *
 *   GObject virtual methods implementation    *
 *                                             *
 ***********************************************/

static void
_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
}

static void
_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
}

static void
ges_xml_formatter_init (GESXmlFormatter * self)
{
  GESXmlFormatterPrivate *priv = ges_xml_formatter_get_instance_private (self);

  priv->project_opened = FALSE;
  priv->element_id = g_hash_table_new (g_direct_hash, g_direct_equal);

  self->priv = priv;
  self->priv->min_version = 1;
}

static void
_dispose (GObject * object)
{
  g_clear_pointer (&GES_XML_FORMATTER (object)->priv->element_id,
      g_hash_table_unref);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
ges_xml_formatter_class_init (GESXmlFormatterClass * self_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (self_class);
  GESBaseXmlFormatterClass *basexmlformatter_class;
  GESFormatterClass *formatter_klass = GES_FORMATTER_CLASS (self_class);

  basexmlformatter_class = GES_BASE_XML_FORMATTER_CLASS (self_class);

  formatter_klass->save_to_uri = _save_to_uri;
  formatter_klass->can_load_uri = _can_load_uri;
  formatter_klass->load_from_uri = _load_from_uri;

  object_class->get_property = _get_property;
  object_class->set_property = _set_property;
  object_class->dispose = _dispose;

  basexmlformatter_class->content_parser.start_element = _parse_element_start;
  basexmlformatter_class->content_parser.end_element = _parse_element_end;
  basexmlformatter_class->content_parser.text = NULL;
  basexmlformatter_class->content_parser.passthrough = NULL;
  basexmlformatter_class->content_parser.error = _error_parsing;

  ges_formatter_class_register_metas (GES_FORMATTER_CLASS (self_class),
      "ges", "GStreamer Editing Services project files",
      "xges", "application/xges", VERSION, GST_RANK_PRIMARY);

  basexmlformatter_class->save = _save;
}

#undef COLLECT_STR_OPT
