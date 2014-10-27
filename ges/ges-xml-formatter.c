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


/* TODO Determine error codes numbers */

#include <string.h>
#include <errno.h>
#include <locale.h>

#include "ges.h"
#include "ges-internal.h"

#define parent_class ges_xml_formatter_parent_class
G_DEFINE_TYPE (GESXmlFormatter, ges_xml_formatter, GES_TYPE_BASE_XML_FORMATTER);

#define API_VERSION 0
#define MINOR_VERSION 1
#define VERSION 0.1

#define COLLECT_STR_OPT (G_MARKUP_COLLECT_STRING | G_MARKUP_COLLECT_OPTIONAL)

#define _GET_PRIV(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GES_TYPE_XML_FORMATTER, GESXmlFormatterPrivate))

struct _GESXmlFormatterPrivate
{
  gboolean ges_opened;
  gboolean project_opened;

  GString *str;

  GHashTable *element_id;

  guint nbelements;
};

static inline void
_parse_ges_element (GMarkupParseContext * context, const gchar * element_name,
    const gchar ** attribute_names, const gchar ** attribute_values,
    GESXmlFormatter * self, GError ** error)
{
  const gchar *version, *properties;
  guint api_version, min_version;

  gchar **split_version = NULL;

  if (g_strcmp0 (element_name, "ges")) {
    g_set_error (error, G_MARKUP_ERROR,
        G_MARKUP_ERROR_INVALID_CONTENT,
        "element '%s', Missing <ges> element'", element_name);
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

  min_version = g_ascii_strtoull (split_version[1], NULL, 10);
  if (min_version > MINOR_VERSION)
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
        "element '%s', Missing project element'", element_name);
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
  const gchar *name, *description, *type, *preset = NULL, *preset_name =
      NULL, *format;

  if (!g_markup_collect_attributes (element_name, attribute_names,
          attribute_values, error,
          G_MARKUP_COLLECT_STRING, "name", &name,
          G_MARKUP_COLLECT_STRING, "description", &description,
          G_MARKUP_COLLECT_STRING, "type", &type,
          COLLECT_STR_OPT, "preset", &preset,
          COLLECT_STR_OPT, "preset-name", &preset_name,
          COLLECT_STR_OPT, "format", &format, G_MARKUP_COLLECT_INVALID))
    return;

  if (format)
    capsformat = gst_caps_from_string (format);

  ges_base_xml_formatter_add_encoding_profile (GES_BASE_XML_FORMATTER (self),
      type, NULL, name, description, capsformat, preset, preset_name, 0, 0,
      NULL, 0, FALSE, NULL, error);
}

static inline void
_parse_stream_profile (GMarkupParseContext * context,
    const gchar * element_name, const gchar ** attribute_names,
    const gchar ** attribute_values, GESXmlFormatter * self, GError ** error)
{
  gboolean variableframerate = FALSE;
  guint id = 0, presence = 0, pass = 0;
  GstCaps *format_caps = NULL, *restriction_caps = NULL;
  const gchar *parent, *strid, *type, *strpresence, *format = NULL,
      *name = NULL, *description = NULL, *preset, *preset_name =
      NULL, *restriction = NULL, *strpass = NULL, *strvariableframerate = NULL;

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
          COLLECT_STR_OPT, "preset-name", &preset_name,
          COLLECT_STR_OPT, "restriction", &restriction,
          COLLECT_STR_OPT, "pass", &strpass,
          COLLECT_STR_OPT, "variableframerate", &strvariableframerate,
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

  if (format)
    format_caps = gst_caps_from_string (format);

  if (restriction)
    restriction_caps = gst_caps_from_string (restriction);

  ges_base_xml_formatter_add_encoding_profile (GES_BASE_XML_FORMATTER (self),
      type, parent, name, description, format_caps, preset, preset_name, id,
      presence, restriction_caps, pass, variableframerate, NULL, error);

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
      NULL;

  if (!g_markup_collect_attributes (element_name, attribute_names,
          attribute_values, error, G_MARKUP_COLLECT_STRING, "id", &id,
          G_MARKUP_COLLECT_STRING, "extractable-type-name",
          &extractable_type_name,
          COLLECT_STR_OPT, "properties", &properties,
          COLLECT_STR_OPT, "metadatas", &metadatas, G_MARKUP_COLLECT_INVALID))
    return;

  extractable_type = g_type_from_name (extractable_type_name);
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

    ges_base_xml_formatter_add_asset (GES_BASE_XML_FORMATTER (self), id,
        extractable_type, props, metadatas, error);
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
  }

  ges_base_xml_formatter_add_track (GES_BASE_XML_FORMATTER (self), track_type,
      caps, strtrack_id, props, metadatas, error);

  if (props)
    gst_structure_free (props);

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
      *extractable_type_name;

  if (!g_markup_collect_attributes (element_name, attribute_names,
          attribute_values, error,
          G_MARKUP_COLLECT_STRING, "priority", &strprio,
          COLLECT_STR_OPT, "extractable-type-name", &extractable_type_name,
          COLLECT_STR_OPT, "properties", &properties,
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

  ges_base_xml_formatter_add_layer (GES_BASE_XML_FORMATTER (self),
      extractable_type, priority, props, metadatas, error);

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
  GstStructure *props = NULL;
  GESTrackType track_types;
  GstClockTime start, inpoint = 0, duration, layer_prio;

  const gchar *strid, *asset_id, *strstart, *strin, *strduration, *strrate,
      *strtrack_types, *strtype, *metadatas = NULL, *properties =
      NULL, *strlayer_prio;

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

  ges_base_xml_formatter_add_clip (GES_BASE_XML_FORMATTER (self),
      strid, asset_id, type, start, inpoint, duration, layer_prio,
      track_types, props, metadatas, error);
  if (props)
    gst_structure_free (props);

  return;

wrong_properties:
  g_set_error (error, G_MARKUP_ERROR,
      G_MARKUP_ERROR_INVALID_CONTENT,
      "element '%s', Clip %s properties '%s', could no be deserialized",
      element_name, asset_id, properties);
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

      value = g_slice_new (GstTimedValue);
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
  GstStructure *children_props = NULL;
  const gchar *track_id = NULL, *children_properties = NULL;

  if (!g_markup_collect_attributes (element_name, attribute_names,
          attribute_values, error,
          G_MARKUP_COLLECT_STRING, "track-id", &track_id,
          COLLECT_STR_OPT, "children-properties", &children_properties,
          G_MARKUP_COLLECT_INVALID)) {
    return;
  }

  if (children_properties) {
    children_props = gst_structure_from_string (children_properties, NULL);
    if (children_props == NULL)
      goto wrong_children_properties;
  }

  ges_base_xml_formatter_add_source (GES_BASE_XML_FORMATTER (self), track_id,
      children_props);

  if (children_props)
    gst_structure_free (children_props);

  return;

wrong_children_properties:
  g_set_error (error, G_MARKUP_ERROR,
      G_MARKUP_ERROR_INVALID_CONTENT,
      "element '%s', children properties '%s', could no be deserialized",
      element_name, children_properties);
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
  const gchar *id, *properties;

  if (!g_markup_collect_attributes (element_name, attribute_names,
          attribute_values, error,
          G_MARKUP_COLLECT_STRING, "id", &id,
          G_MARKUP_COLLECT_STRING, "properties", &properties,
          G_MARKUP_COLLECT_INVALID)) {
    return;
  }

  ges_base_xml_formatter_add_group (GES_BASE_XML_FORMATTER (self), id,
      properties);
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

  if (!G_UNLIKELY (priv->ges_opened))
    _parse_ges_element (context, element_name, attribute_names,
        attribute_values, self, error);
  else if (!G_UNLIKELY (priv->project_opened))
    _parse_project (context, element_name, attribute_names, attribute_values,
        self, error);
  else if (g_strcmp0 (element_name, "encoding-profile") == 0)
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

static void
_parse_element_end (GMarkupParseContext * context,
    const gchar * element_name, gpointer self, GError ** error)
{
  /*GESXmlFormatterPrivate *priv = _GET_PRIV (self); */
}

static void
_error_parsing (GMarkupParseContext * context, GError * error,
    gpointer user_data)
{
  GST_WARNING ("Error accured when parsing %s", error->message);
}

/***********************************************
 *                                             *
 *            Saving implementation            *
 *                                             *
 ***********************************************/

/* XML writting utils */
static inline void
append_escaped (GString * str, gchar * tmpstr)
{
  g_string_append (str, tmpstr);
  g_free (tmpstr);
}

static inline gboolean
_can_serialize_spec (GParamSpec * spec)
{
  if (!(spec->flags & G_PARAM_WRITABLE)) {
    GST_DEBUG ("%s from %s is not writable",
        spec->name, g_type_name (spec->owner_type));

    return FALSE;
  } else if (spec->flags & G_PARAM_CONSTRUCT_ONLY) {
    GST_DEBUG ("%s from %s is construct only",
        spec->name, g_type_name (spec->owner_type));

    return FALSE;
  } else if (spec->flags & GES_PARAM_NO_SERIALIZATION &&
      g_type_is_a (spec->owner_type, GES_TYPE_TIMELINE_ELEMENT)) {
    GST_DEBUG ("%s from %s is set as GES_PARAM_NO_SERIALIZATION",
        spec->name, g_type_name (spec->owner_type));

    return FALSE;
  } else if (g_type_is_a (G_PARAM_SPEC_VALUE_TYPE (spec), G_TYPE_OBJECT)) {
    GST_DEBUG ("%s from %s contains GObject, can't serialize that.",
        spec->name, g_type_name (spec->owner_type));

    return FALSE;
  } else if ((g_type_is_a (spec->owner_type, GST_TYPE_OBJECT) &&
          !g_strcmp0 (spec->name, "name"))) {

    GST_DEBUG ("We do not want to serialize the name of GstObjects.");
    return FALSE;
  } else if (G_PARAM_SPEC_VALUE_TYPE (spec) == G_TYPE_GTYPE) {
    GST_DEBUG ("%s from %s contains a GType, can't serialize.",
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
_serialize_properties (GObject * object, const gchar * fieldname, ...)
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
    if (spec->value_type == GST_TYPE_CAPS) {
      GstCaps *caps;

      g_object_get (object, spec->name, &caps, NULL);
      gst_structure_set (structure, spec->name, G_TYPE_STRING,
          gst_caps_to_string (caps), NULL);
    } else if (_can_serialize_spec (spec)) {
      _init_value_from_spec_for_serialization (&val, spec);
      g_object_get_property (object, spec->name, &val);
      gst_structure_set_value (structure, spec->name, &val);
      g_value_unset (&val);
    }
  }
  g_free (pspecs);

  if (fieldname) {
    va_list varargs;
    va_start (varargs, fieldname);
    gst_structure_remove_fields_valist (structure, fieldname, varargs);
    va_end (varargs);
  }

  ret = gst_structure_to_string (structure);
  gst_structure_free (structure);

  return ret;
}

static inline void
_save_assets (GString * str, GESProject * project)
{
  char *properties, *metas;
  GESAsset *asset;
  GList *assets, *tmp;

  assets = ges_project_list_assets (project, GES_TYPE_EXTRACTABLE);
  for (tmp = assets; tmp; tmp = tmp->next) {
    asset = GES_ASSET (tmp->data);
    properties = _serialize_properties (G_OBJECT (asset), NULL);
    metas = ges_meta_container_metas_to_string (GES_META_CONTAINER (asset));
    append_escaped (str,
        g_markup_printf_escaped
        ("      <asset id='%s' extractable-type-name='%s' properties='%s' metadatas='%s' />\n",
            ges_asset_get_id (asset),
            g_type_name (ges_asset_get_extractable_type (asset)), properties,
            metas));
    g_free (properties);
    g_free (metas);
  }
  g_list_free_full (assets, gst_object_unref);
}

static inline void
_save_tracks (GESXmlFormatter * self, GString * str, GESTimeline * timeline)
{
  gchar *strtmp, *metas;
  GESTrack *track;
  GList *tmp, *tracks;
  char *properties;

  guint nb_tracks = 0;

  tracks = ges_timeline_get_tracks (timeline);
  for (tmp = tracks; tmp; tmp = tmp->next) {
    track = GES_TRACK (tmp->data);
    properties = _serialize_properties (G_OBJECT (track), NULL);
    strtmp = gst_caps_to_string (ges_track_get_caps (track));
    metas = ges_meta_container_metas_to_string (GES_META_CONTAINER (track));
    append_escaped (str,
        g_markup_printf_escaped
        ("      <track caps='%s' track-type='%i' track-id='%i' properties='%s' metadatas='%s'/>\n",
            strtmp, track->type, nb_tracks++, properties, metas));
    g_free (strtmp);
    g_free (metas);
    g_free (properties);
  }
  g_list_free_full (tracks, gst_object_unref);
}

static inline void
_save_children_properties (GString * str, GESTrackElement * trackelement)
{
  GstStructure *structure;
  GParamSpec **pspecs, *spec;
  guint i, n_props;

  pspecs = ges_track_element_list_children_properties (trackelement, &n_props);

  structure = gst_structure_new_empty ("properties");
  for (i = 0; i < n_props; i++) {
    GValue val = { 0 };
    spec = pspecs[i];

    if (_can_serialize_spec (spec)) {
      _init_value_from_spec_for_serialization (&val, spec);
      ges_track_element_get_child_property_by_pspec (trackelement, spec, &val);
      gst_structure_set_value (structure, spec->name, &val);
      g_value_unset (&val);
    }
    g_param_spec_unref (spec);
  }
  g_free (pspecs);

  append_escaped (str,
      g_markup_printf_escaped (" children-properties='%s'",
          gst_structure_to_string (structure)));

  gst_structure_free (structure);
}

/* TODO : Use this function for every track element with controllable properties */
static inline void
_save_keyframes (GString * str, GESTrackElement * trackelement, gint index)
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
      GstDirectControlBinding *binding;

      binding = (GstDirectControlBinding *) value;

      g_object_get (binding, "control-source", &source, NULL);

      if (GST_IS_INTERPOLATION_CONTROL_SOURCE (source)) {
        GList *timed_values, *tmp;
        GstInterpolationMode mode;

        append_escaped (str,
            g_markup_printf_escaped
            ("            <binding type='direct' source_type='interpolation' property='%s'",
                (gchar *) key));
        g_object_get (source, "mode", &mode, NULL);
        append_escaped (str, g_markup_printf_escaped (" mode='%d'", mode));
        append_escaped (str, g_markup_printf_escaped (" track_id='%d'", index));
        append_escaped (str, g_markup_printf_escaped (" values ='"));
        timed_values =
            gst_timed_value_control_source_get_all
            (GST_TIMED_VALUE_CONTROL_SOURCE (source));
        for (tmp = timed_values; tmp; tmp = tmp->next) {
          gchar strbuf[G_ASCII_DTOSTR_BUF_SIZE];
          GstTimedValue *value;

          value = (GstTimedValue *) tmp->data;
          append_escaped (str, g_markup_printf_escaped (" %" G_GUINT64_FORMAT
                  ":%s ", value->timestamp, g_ascii_dtostr (strbuf,
                      G_ASCII_DTOSTR_BUF_SIZE, value->value)));
        }
        append_escaped (str, g_markup_printf_escaped ("'/>\n"));
      } else
        GST_DEBUG ("control source not in [interpolation]");
    } else
      GST_DEBUG ("Binding type not in [direct]");
  }
}

static inline void
_save_effect (GString * str, guint clip_id, GESTrackElement * trackelement,
    GESTimeline * timeline)
{
  GESTrack *tck;
  GList *tmp, *tracks;
  gchar *properties, *metas;
  guint track_id = 0;

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

  properties = _serialize_properties (G_OBJECT (trackelement), "start",
      "in-point", "duration", "locked", "max-duration", "name", NULL);
  metas =
      ges_meta_container_metas_to_string (GES_META_CONTAINER (trackelement));
  append_escaped (str,
      g_markup_printf_escaped ("          <effect asset-id='%s' clip-id='%u'"
          " type-name='%s' track-type='%i' track-id='%i' properties='%s' metadatas='%s'",
          ges_extractable_get_id (GES_EXTRACTABLE (trackelement)), clip_id,
          g_type_name (G_OBJECT_TYPE (trackelement)), tck->type, track_id,
          properties, metas));
  g_free (properties);
  g_free (metas);

  _save_children_properties (str, trackelement);
  append_escaped (str, g_markup_printf_escaped (">\n"));

  _save_keyframes (str, trackelement, -1);

  append_escaped (str, g_markup_printf_escaped ("          </effect>\n"));
}

static inline void
_save_layers (GESXmlFormatter * self, GString * str, GESTimeline * timeline)
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
    properties = _serialize_properties (G_OBJECT (layer), "priority", NULL);
    metas = ges_meta_container_metas_to_string (GES_META_CONTAINER (layer));
    append_escaped (str,
        g_markup_printf_escaped
        ("      <layer priority='%i' properties='%s' metadatas='%s'>\n",
            priority, properties, metas));
    g_free (properties);
    g_free (metas);

    clips = ges_layer_get_clips (layer);
    for (tmpclip = clips; tmpclip; tmpclip = tmpclip->next) {
      GList *effects, *tmpeffect;
      GList *tmptrackelement;
      GList *tracks;

      clip = GES_CLIP (tmpclip->data);
      effects = ges_clip_get_top_effects (clip);

      /* We escape all mandatrorry properties that are handled sparetely
       * and vtype for StandarTransition as it is the asset ID */
      properties = _serialize_properties (G_OBJECT (clip),
          "supported-formats", "rate", "in-point", "start", "duration",
          "max-duration", "priority", "vtype", "uri", NULL);
      append_escaped (str,
          g_markup_printf_escaped ("        <clip id='%i' asset-id='%s'"
              " type-name='%s' layer-priority='%i' track-types='%i' start='%"
              G_GUINT64_FORMAT "' duration='%" G_GUINT64_FORMAT "' inpoint='%"
              G_GUINT64_FORMAT "' rate='%d' properties='%s' >\n",
              priv->nbelements, ges_extractable_get_id (GES_EXTRACTABLE (clip)),
              g_type_name (G_OBJECT_TYPE (clip)), priority,
              ges_clip_get_supported_formats (clip), _START (clip),
              _DURATION (clip), _INPOINT (clip), 0, properties));
      g_free (properties);

      g_hash_table_insert (self->priv->element_id, clip,
          GINT_TO_POINTER (priv->nbelements));

      for (tmpeffect = effects; tmpeffect; tmpeffect = tmpeffect->next)
        _save_effect (str, priv->nbelements,
            GES_TRACK_ELEMENT (tmpeffect->data), timeline);

      tracks = ges_timeline_get_tracks (timeline);

      for (tmptrackelement = GES_CONTAINER_CHILDREN (clip); tmptrackelement;
          tmptrackelement = tmptrackelement->next) {
        gint index;

        if (!GES_IS_SOURCE (tmptrackelement->data))
          continue;

        index =
            g_list_index (tracks,
            ges_track_element_get_track (tmptrackelement->data));
        append_escaped (str,
            g_markup_printf_escaped ("          <source track-id='%i'", index));
        _save_children_properties (str, tmptrackelement->data);
        append_escaped (str, g_markup_printf_escaped (">\n"));
        _save_keyframes (str, tmptrackelement->data, index);
        append_escaped (str, g_markup_printf_escaped ("          </source>\n"));
      }

      g_list_free_full (tracks, gst_object_unref);

      g_string_append (str, "        </clip>\n");

      priv->nbelements++;
    }
    g_string_append (str, "      </layer>\n");
  }
}

static void
_save_group (GESXmlFormatter * self, GString * str, GList ** seen_groups,
    GESGroup * group)
{
  GList *tmp;
  gchar *properties;

  if (g_list_find (*seen_groups, group)) {
    GST_DEBUG_OBJECT (group, "Already serialized");

    return;
  }

  *seen_groups = g_list_prepend (*seen_groups, group);
  for (tmp = GES_CONTAINER_CHILDREN (group); tmp; tmp = tmp->next) {
    if (GES_IS_GROUP (tmp->data)) {
      _save_group (self, str, seen_groups,
          GES_GROUP (GES_TIMELINE_ELEMENT (tmp->data)));
    }
  }

  properties = _serialize_properties (G_OBJECT (group), NULL);
  g_string_append_printf (str, "        <group id='%d' properties='%s'>\n",
      self->priv->nbelements, properties);
  g_free (properties);
  g_hash_table_insert (self->priv->element_id, group,
      GINT_TO_POINTER (self->priv->nbelements));
  self->priv->nbelements++;

  for (tmp = GES_CONTAINER_CHILDREN (group); tmp; tmp = tmp->next) {
    gint id = GPOINTER_TO_INT (g_hash_table_lookup (self->priv->element_id,
            tmp->data));

    g_string_append_printf (str, "          <child id='%d' name='%s'/>\n", id,
        GES_TIMELINE_ELEMENT_NAME (tmp->data));
  }
  g_string_append (str, "        </group>\n");
}

static void
_save_groups (GESXmlFormatter * self, GString * str, GESTimeline * timeline)
{
  GList *tmp;
  GList *seen_groups = NULL;

  g_string_append (str, "      <groups>\n");
  for (tmp = timeline_get_groups (timeline); tmp; tmp = tmp->next) {
    _save_group (self, str, &seen_groups, tmp->data);
  }
  g_string_append (str, "      </groups>\n");
}

static inline void
_save_timeline (GESXmlFormatter * self, GString * str, GESTimeline * timeline)
{
  gchar *properties = NULL, *metas = NULL;

  properties = _serialize_properties (G_OBJECT (timeline), "update", "name",
      "async-handling", "message-forward", NULL);

  ges_meta_container_set_uint64 (GES_META_CONTAINER (timeline), "duration",
      ges_timeline_get_duration (timeline));
  metas = ges_meta_container_metas_to_string (GES_META_CONTAINER (timeline));
  append_escaped (str,
      g_markup_printf_escaped
      ("    <timeline properties='%s' metadatas='%s'>\n", properties, metas));

  _save_tracks (self, str, timeline);
  _save_layers (self, str, timeline);
  _save_groups (self, str, timeline);

  g_string_append (str, "    </timeline>\n");

  g_free (properties);
  g_free (metas);
}

static void
_save_stream_profiles (GString * str, GstEncodingProfile * sprof,
    const gchar * profilename, guint id)
{
  gchar *tmpc;
  GstCaps *tmpcaps;
  const gchar *preset, *preset_name, *name, *description;

  append_escaped (str,
      g_markup_printf_escaped
      ("        <stream-profile parent='%s' id='%d' type='%s' "
          "presence='%d' ", profilename, id,
          gst_encoding_profile_get_type_nick (sprof),
          gst_encoding_profile_get_presence (sprof)));

  tmpcaps = gst_encoding_profile_get_format (sprof);
  if (tmpcaps) {
    tmpc = gst_caps_to_string (tmpcaps);
    append_escaped (str, g_markup_printf_escaped ("format='%s' ", tmpc));
    gst_caps_unref (tmpcaps);
    g_free (tmpc);
  }

  name = gst_encoding_profile_get_name (sprof);
  if (name)
    append_escaped (str, g_markup_printf_escaped ("name='%s' ", name));

  description = gst_encoding_profile_get_description (sprof);
  if (description)
    append_escaped (str, g_markup_printf_escaped ("description='%s' ",
            description));

  preset = gst_encoding_profile_get_preset (sprof);
  if (preset)
    append_escaped (str, g_markup_printf_escaped ("preset='%s' ", preset));

  preset_name = gst_encoding_profile_get_preset_name (sprof);
  if (preset_name)
    append_escaped (str, g_markup_printf_escaped ("preset-name='%s' ",
            preset_name));

  tmpcaps = gst_encoding_profile_get_restriction (sprof);
  if (tmpcaps) {
    tmpc = gst_caps_to_string (tmpcaps);
    append_escaped (str, g_markup_printf_escaped ("restriction='%s' ", tmpc));
    gst_caps_unref (tmpcaps);
    g_free (tmpc);
  }

  if (GST_IS_ENCODING_VIDEO_PROFILE (sprof)) {
    GstEncodingVideoProfile *vp = (GstEncodingVideoProfile *) sprof;

    append_escaped (str,
        g_markup_printf_escaped ("pass='%d' variableframerate='%i' ",
            gst_encoding_video_profile_get_pass (vp),
            gst_encoding_video_profile_get_variableframerate (vp)));
  }

  g_string_append (str, "/>\n");
}

static inline void
_save_encoding_profiles (GString * str, GESProject * project)
{
  GstCaps *profformat;
  const gchar *profname, *profdesc, *profpreset, *proftype, *profpresetname;

  const GList *tmp;

  for (tmp = ges_project_list_encoding_profiles (project); tmp; tmp = tmp->next) {
    GstEncodingProfile *prof = GST_ENCODING_PROFILE (tmp->data);

    profname = gst_encoding_profile_get_name (prof);
    profdesc = gst_encoding_profile_get_description (prof);
    profpreset = gst_encoding_profile_get_preset (prof);
    profpresetname = gst_encoding_profile_get_preset_name (prof);
    proftype = gst_encoding_profile_get_type_nick (prof);

    append_escaped (str,
        g_markup_printf_escaped
        ("      <encoding-profile name='%s' description='%s' type='%s' ",
            profname, profdesc, proftype));

    if (profpreset)
      append_escaped (str, g_markup_printf_escaped ("preset='%s' ",
              profpreset));

    if (profpresetname)
      append_escaped (str, g_markup_printf_escaped ("preset-name='%s' ",
              profpresetname));

    profformat = gst_encoding_profile_get_format (prof);
    if (profformat) {
      gchar *format = gst_caps_to_string (profformat);
      append_escaped (str, g_markup_printf_escaped ("format='%s' ", format));
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
        _save_stream_profiles (str, sprof, profname, i);
      }
    }
    append_escaped (str,
        g_markup_printf_escaped ("      </encoding-profile>\n"));
  }
}

static GString *
_save (GESFormatter * formatter, GESTimeline * timeline, GError ** error)
{
  GString *str;
  GESProject *project;

  gchar *properties = NULL, *metas = NULL;
  GESXmlFormatterPrivate *priv;


  priv = _GET_PRIV (formatter);
  project = formatter->project;
  str = priv->str = g_string_new (NULL);

  g_string_append_printf (str, "<ges version='%i.%i'>\n", API_VERSION,
      MINOR_VERSION);
  properties = _serialize_properties (G_OBJECT (project), NULL);
  metas = ges_meta_container_metas_to_string (GES_META_CONTAINER (project));
  append_escaped (str,
      g_markup_printf_escaped ("  <project properties='%s' metadatas='%s'>\n",
          properties, metas));
  g_free (properties);
  g_free (metas);

  g_string_append (str, "    <encoding-profiles>\n");
  _save_encoding_profiles (str, project);
  g_string_append (str, "    </encoding-profiles>\n");

  g_string_append (str, "    <ressources>\n");
  _save_assets (str, project);
  g_string_append (str, "    </ressources>\n");

  _save_timeline (GES_XML_FORMATTER (formatter), str, timeline);
  g_string_append (str, "</project>\n</ges>");

  priv->str = NULL;

  return str;
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
  GESXmlFormatterPrivate *priv = _GET_PRIV (self);

  priv->project_opened = FALSE;
  priv->element_id = g_hash_table_new (g_direct_hash, g_direct_equal);

  self->priv = priv;
}

static void
_dispose (GObject * object)
{
  g_clear_pointer (&GES_XML_FORMATTER (object)->priv->element_id,
      (GDestroyNotify) g_hash_table_unref);
}

static void
ges_xml_formatter_class_init (GESXmlFormatterClass * self_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (self_class);
  GESBaseXmlFormatterClass *basexmlformatter_class;

  basexmlformatter_class = GES_BASE_XML_FORMATTER_CLASS (self_class);

  g_type_class_add_private (self_class, sizeof (GESXmlFormatterPrivate));
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
      "xges", "application/ges", VERSION, GST_RANK_PRIMARY);

  basexmlformatter_class->save = _save;
}

#undef COLLECT_STR_OPT
