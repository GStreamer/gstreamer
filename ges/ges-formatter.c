/* GStreamer Editing Services
 * Copyright (C) 2010 Brandon Lewis <brandon.lewis@collabora.co.uk>
 *               2010 Nokia Corporation
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

/**
 * SECTION:ges-formatter
 * @short_description: Base Class for loading and saving #GESTimeline data.
 *
 * Responsible for loading and/or saving the contents of a #GESTimeline to/from
 * various formats.
 **/

#include <gst/gst.h>
#include <stdlib.h>
#include "ges-formatter.h"
#include "ges.h"
#include "ges-internal.h"

G_DEFINE_TYPE (GESFormatter, ges_formatter, G_TYPE_OBJECT);

static void ges_formatter_dispose (GObject * object);
static void ges_formatter_finalize (GObject * object);

/* for ini format */
static gboolean save_ini (GESFormatter * formatter, GESTimeline * timeline);
static gboolean load_ini (GESFormatter * formatter, GESTimeline * timeline);

static void
ges_formatter_class_init (GESFormatterClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = ges_formatter_dispose;
  object_class->finalize = ges_formatter_finalize;

  klass->save = save_ini;
  klass->load = load_ini;
}

static void
ges_formatter_init (GESFormatter * object)
{
}

static void
ges_formatter_dispose (GObject * object)
{
  GESFormatter *formatter;
  formatter = GES_FORMATTER (object);

  if (formatter->data) {
    g_free (formatter->data);
  }
}

static void
ges_formatter_finalize (GObject * formatter)
{
}

GESFormatter *
ges_formatter_new (void)
{
  return g_object_new (GES_TYPE_FORMATTER, NULL);
}

/**
 * ges_formatter_new_for_uri:
 * @uri: a #gchar * pointing to the uri
 *
 * Creates a #GESFormatter that can handle the given URI.
 *
 * Returns: A GESFormatter or subclass that can load the given uri, or NULL if
 * the uri is not supported.
 */

GESFormatter *
ges_formatter_new_for_uri (gchar * uri)
{
  if (ges_formatter_can_load_uri (uri))
    return ges_formatter_new ();
  return NULL;
}

/**
 * ges_formatter_can_load_uri:
 * @uri: a #gchar * pointing to the URI
 * 
 * Returns true if there is a #GESFormatterClass derivative registered with
 * the system which can load data from the given URI.
 *
 * Returns: TRUE if the given uri is supported or FALSE if the uri is 
 * not supported.
 */

gboolean
ges_formatter_can_load_uri (gchar * uri)
{
  if (!(gst_uri_is_valid (uri))) {
    GST_ERROR ("Invalid uri!");
    return FALSE;
  }

  if (!(gst_uri_has_protocol (uri, "file"))) {
    gchar *proto = gst_uri_get_protocol (uri);
    GST_ERROR ("Unspported protocol '%s'", proto);
    g_free (proto);
    return FALSE;
  }

  /* TODO: implement file format registry */
  /* TODO: search through the registry and chose a GESFormatter class that can
   * handle the URI.*/

  return TRUE;
}

/**
 * ges_formatter_can_load_uri:
 * @uri: a #gchar * pointing to a URI
 * 
 * Returns TRUE if thereis a #GESFormatterClass derivative registered with the
 * system which can save data to the given URI.
 *
 * Returns: TRUE if the given uri is supported or FALSE if the given URI is
 * not suported.
 */

gboolean
ges_formatter_can_save_uri (gchar * uri)
{
  if (!(gst_uri_is_valid (uri))) {
    GST_ERROR ("Invalid uri!");
    return FALSE;
  }

  if (!(gst_uri_has_protocol (uri, "file"))) {
    gchar *proto = gst_uri_get_protocol (uri);
    GST_ERROR ("Unspported protocol '%s'", proto);
    g_free (proto);
    return FALSE;
  }

  /* TODO: implement file format registry */
  /* TODO: search through the registry and chose a GESFormatter class that can
   * handle the URI.*/

  return TRUE;
  return FALSE;
}

/**
 * ges_formatter_load:
 * @formatter: a pointer to a #GESFormatter instance or subclass.
 * @timeline: a pointer to a #GESTimeline
 *
 * Loads data from formatter to into timeline. The data field of formatter
 * must point to a block of data, and length must be correctly set to the
 * length of the data. This method is only implemented in subclasses.
 * 
 * Returns: TRUE if the data was successfully loaded into timeline
 * or FALSE if an error occured during loading.
 */

gboolean
ges_formatter_load (GESFormatter * formatter, GESTimeline * timeline)
{
  GESFormatterClass *klass;

  klass = GES_FORMATTER_GET_CLASS (formatter);

  if (klass->load)
    return klass->load (formatter, timeline);
  return FALSE;
}

/**
 * ges_formatter_save:
 * @formatter: a pointer to a #GESFormatter instance or subclass.
 * @timeline: a pointer to a #GESTimeline
 *
 * Save data from timeline into formatter. Upon success, The data field of the
 * formatter will point to a newly-allocated block of data, and the length
 * field of the formatter will contain the length of the block. This method is
 * only implemented in subclasses.
 *
 * Returns: TRUE if the timeline data was successfully saved for FALSE if
 * an error occured during saving.
 */

gboolean
ges_formatter_save (GESFormatter * formatter, GESTimeline * timeline)
{
  GESFormatterClass *klass;

  klass = GES_FORMATTER_GET_CLASS (formatter);

  if (klass->save)
    return klass->save (formatter, timeline);
  return FALSE;
}

/**
 * ges_formatter_load_from_uri:
 * @formatter: a pointer to a #GESFormatter instance or subclass
 * @timeline: a pointer to a #GESTimeline
 * @uri: a #gchar * pointing to a URI
 * 
 * Load data from the given URI into timeline. The default implementation
 * loads the entire contents of the uri with g_file_get_contents, then calls
 * ges_formatter_load(). It works only on valid URIs pointing to local files.
 *
 * Subclasses should override the class method load_from_uri if they want to
 * handle other types of URIs. They should also override the class method
 * can_load_uri() to indicate that they can handle other types of URI.
 *
 * Returns: TRUE if the timeline data was successfully loaded from the URI or
 * FALSE if an error occured during loading.
 */

gboolean
ges_formatter_load_from_uri (GESFormatter * formatter, GESTimeline * timeline,
    gchar * uri)
{
  gchar *location;
  GError *e = NULL;
  gboolean ret = TRUE;


  if (formatter->data) {
    GST_ERROR ("formatter already has data! please set data to NULL");
  }

  if (!(location = gst_uri_get_location (uri))) {
    return FALSE;
  }

  if (g_file_get_contents (location, &formatter->data, &formatter->length, &e)) {
    if (!ges_formatter_load (formatter, timeline)) {
      GST_ERROR ("couldn't deserialize formatter");
      ret = FALSE;
    }
  } else {
    GST_ERROR ("couldn't read file '%s': %s", location, e->message);
    ret = FALSE;
  }

  if (e)
    g_error_free (e);
  g_free (location);

  return ret;
}

/**
 * ges_formatter_save_to_uri:
 * @formatter: a pointer to a #GESFormatter instance or subclass
 * @timeline: a pointer to a #GESTimeline
 * @uri: a #gchar * pointing to a URI
 *
 * Save data from timeline to the given URI. The default implementation first
 * calls ges_formatter_save () and then writes the entire contents of the data
 * field to a local file using g_file_set_contents. It works only for local
 * files.
 *
 * Subclasses should override the class method save_to_uri if they want to
 * handle other types of URIs. They should also override the class method
 * can_save_uri to return true for custom URIs.
 *
 * Returns: TRUE if the timeline data was successfully saved to the URI or
 * FALSE if an error occured during saving.
 */

gboolean
ges_formatter_save_to_uri (GESFormatter * formatter, GESTimeline * timeline,
    gchar * uri)
{
  gchar *location;
  GError *e = NULL;
  gboolean ret = TRUE;


  if (!(location = g_filename_from_uri (uri, NULL, NULL))) {
    return FALSE;
  }

  if (!ges_formatter_save (formatter, timeline)) {
    GST_ERROR ("couldn't serialize formatter");
  } else {
    if (!g_file_set_contents (location, formatter->data, formatter->length, &e)) {
      GST_ERROR ("couldn't write file '%s': %s", location, e->message);
      ret = FALSE;
    }
  }

  if (e)
    g_error_free (e);
  g_free (location);

  return ret;
}

static gboolean
save_ini (GESFormatter * formatter, GESTimeline * timeline)
{
  GKeyFile *kf;
  GList *tmp, *tracks;
  int i = 0;
  int n_objects = 0;
  gchar buffer[255];

  GST_DEBUG ("saving formatter");

  kf = g_key_file_new ();

  g_key_file_set_value (kf, "General", "version", "1");

  tracks = ges_timeline_get_tracks (timeline);

  for (i = 0, tmp = tracks; tmp; i++, tmp = tmp->next) {
    GESTrack *track;
    gchar *type;
    gchar *caps;
    GValue v = { 0 };

    track = GES_TRACK (tmp->data);

    g_snprintf (buffer, 255, "Track%d", i);
    g_value_init (&v, GES_TYPE_TRACK_TYPE);
    g_object_get_property (G_OBJECT (track), "track-type", &v);

    type = gst_value_serialize (&v);
    caps = gst_caps_to_string (track->caps);

    g_key_file_set_value (kf, buffer, "type", type);
    g_key_file_set_string (kf, buffer, "caps", caps);

    g_free (caps);
    g_free (type);
    gst_object_unref (track);
    tmp->data = NULL;
  }

  g_list_free (tracks);

  for (i = 0, tmp = timeline->layers; tmp; i++, tmp = tmp->next) {
    const gchar *type;
    GESTimelineLayer *layer;
    GList *objs, *cur;
    layer = tmp->data;

    g_snprintf (buffer, 255, "Layer%d", i);

    if (GES_IS_SIMPLE_TIMELINE_LAYER (tmp->data)) {
      type = "simple";
    } else {
      type = "default";
    }

    g_key_file_set_integer (kf, buffer, "priority", layer->priority);
    g_key_file_set_value (kf, buffer, "type", type);

    objs = ges_timeline_layer_get_objects (layer);

    for (cur = objs; cur; cur = cur->next) {
      GESTimelineObject *obj;
      GParamSpec **properties;
      guint i, n;

      obj = GES_TIMELINE_OBJECT (cur->data);
      properties =
          g_object_class_list_properties (G_OBJECT_GET_CLASS (obj), &n);

      g_snprintf (buffer, 255, "Object%d", n_objects);
      n_objects++;

      g_key_file_set_value (kf, buffer, "type",
          G_OBJECT_TYPE_NAME (G_OBJECT (obj)));

      for (i = 0; i < n; i++) {
        GValue v = { 0 };
        gchar *serialized;
        GParamSpec *p = properties[i];

        g_value_init (&v, p->value_type);
        g_object_get_property (G_OBJECT (obj), p->name, &v);

        /* FIXME: does this work for properties marked G_PARAM_CONSTRUCT_ONLY?
         * */

        if ((p->flags & G_PARAM_READABLE) && (p->flags & G_PARAM_WRITABLE)) {
          if (!(serialized = gst_value_serialize (&v)))
            continue;

          g_key_file_set_string (kf, buffer, p->name, serialized);
          g_free (serialized);
        }

        g_value_unset (&v);
      }

      g_free (properties);
      g_object_unref (obj);
      cur->data = NULL;
    }

    g_list_free (objs);
  }

  if (formatter->data) {
    g_free (formatter->data);
  }

  formatter->data = g_key_file_to_data (kf, &formatter->length, NULL);
  g_key_file_free (kf);

  return TRUE;
}

static gboolean
create_track (GKeyFile * kf, gchar * group, GESTimeline * timeline)
{
  GESTrack *track;
  GstCaps *caps;
  gchar *caps_field, *type_field;
  GValue v = { 0 };

  if (!(caps_field = g_key_file_get_string (kf, group, "caps", NULL)))
    return FALSE;

  caps = gst_caps_from_string (caps_field);
  g_free (caps_field);

  if (!(type_field = g_key_file_get_value (kf, group, "type", NULL)))
    return FALSE;

  g_value_init (&v, GES_TYPE_TRACK_TYPE);
  gst_value_deserialize (&v, type_field);
  g_free (type_field);

  if (!caps)
    return FALSE;

  track = ges_track_new (g_value_get_flags (&v), caps);

  if (!ges_timeline_add_track (timeline, track)) {
    g_object_unref (track);
    return FALSE;
  }

  return TRUE;
}

static GESTimelineLayer *
create_layer (GKeyFile * kf, gchar * group, GESTimeline * timeline)
{
  GESTimelineLayer *ret = NULL;
  gchar *type_field, *priority_field;
  gboolean is_simple;
  guint priority;

  if (!(type_field = g_key_file_get_value (kf, group, "type", NULL)))
    return FALSE;

  is_simple = g_str_equal (type_field, "simple");
  g_free (type_field);

  if (!(priority_field = g_key_file_get_value (kf, group, "priority", NULL)))
    return FALSE;

  priority = strtoul (priority_field, NULL, 10);
  g_free (priority_field);

  if (is_simple) {
    GESSimpleTimelineLayer *simple;
    simple = ges_simple_timeline_layer_new ();
    ret = (GESTimelineLayer *) simple;
  } else {
    ret = ges_timeline_layer_new ();
  }

  ges_timeline_layer_set_priority (ret, priority);
  if (!ges_timeline_add_layer (timeline, ret)) {
    g_object_unref (ret);
    ret = NULL;
  }

  return ret;
}

static gboolean
create_object (GKeyFile * kf, gchar * group, GESTimelineLayer * layer)
{
  GType type;
  gchar *type_name;
  GObject *obj;
  GESTimelineObject *timeline_obj;
  gchar **keys;
  gsize n_keys, i;
  GParamSpec *pspec;
  GObjectClass *klass;
  GParameter *params, *p;
  gboolean ret = FALSE;

  GST_INFO ("processing '%s'", group);

  /* get a reference to the object class */

  if (!(type_name = g_key_file_get_value (kf, group, "type", NULL))) {
    GST_ERROR ("no type name for object '%s'", group);
    return FALSE;
  }

  if (!(type = g_type_from_name (type_name))) {
    GST_ERROR ("invalid type name '%s'", type_name);
    goto fail_free_type_name;
  }

  if (!(klass = g_type_class_ref (type))) {
    GST_ERROR ("couldn't get class ref");
    goto fail_free_type_name;
  }

  if (!(keys = g_key_file_get_keys (kf, group, &n_keys, NULL)))
    goto fail_unref_class;

  /* create an array of parameters for the call to g_new0 */
  /* skip first field 'type' */

  if (!(params = g_new0 (GParameter, (n_keys - 1)))) {
    GST_ERROR ("couldn't allocate parameter list");
    goto fail_free_keys;
  }

  GST_DEBUG ("processing parameter list", group);

  for (p = params, i = 1; i < n_keys; i++, p++) {
    gchar *value;
    gchar *key;

    key = keys[i];

    GST_DEBUG ("processing key '%s'", key);

    /* find the param spec for this property */
    if (!(pspec = g_object_class_find_property (klass, key))) {
      GST_ERROR ("Object type %s has no property %s", type_name, key);
      goto fail_free_params;
    }

    p->name = key;
    g_value_init (&p->value, pspec->value_type);

    /* assume this is going to work */
    value = g_key_file_get_string (kf, group, key, NULL);

    if (!gst_value_deserialize (&p->value, value)) {
      GST_ERROR ("Couldn't read property value '%s' for property '%s'",
          key, value);
      goto fail_free_params;
    }

    g_free (value);
  }

  /* create the object from the supplied type name */

  if (!(obj = g_object_newv (type, (n_keys - 1), params))) {
    GST_ERROR ("couldn't create object");
    goto fail_free_type_name;
  }

  /* check that we have a subclass of GESTimelineObject */

  if (!GES_IS_TIMELINE_OBJECT (obj)) {
    GST_ERROR ("'%s' is not a subclass of GESTimelineObject!", type_name);
    goto fail_unref_obj;
  }
  timeline_obj = (GESTimelineObject *) obj;

  /* add the object to the layer */

  if (GES_IS_SIMPLE_TIMELINE_LAYER (layer)) {
    if (!ges_simple_timeline_layer_add_object ((GESSimpleTimelineLayer *)
            layer, timeline_obj, -1)) {
      goto fail_unref_obj;
    }
  } else {
    if (!ges_timeline_layer_add_object (layer, timeline_obj)) {
      goto fail_unref_obj;
    }
  }

  ret = TRUE;

fail_unref_obj:
  if (!ret)
    g_object_unref (obj);

fail_free_params:
  for (p = params, i = 1; i < n_keys; i++, p++) {
    g_value_unset (&p->value);
  }
  g_free (params);

fail_free_keys:
  g_strfreev (keys);

fail_unref_class:
  g_type_class_unref (klass);

fail_free_type_name:
  g_free (type_name);

  return ret;
}

static gboolean
load_ini (GESFormatter * formatter, GESTimeline * timeline)
{
  GKeyFile *kf;
  GError *error = NULL;
  gboolean ret = TRUE;
  gchar **groups;
  gsize n_groups, i;
  GESTimelineLayer *cur_layer = NULL;

  kf = g_key_file_new ();
  if (!g_key_file_load_from_data (kf, formatter->data, formatter->length,
          G_KEY_FILE_NONE, &error)) {
    ret = FALSE;
    GST_ERROR (error->message);
    GST_INFO (formatter->data);
    goto free_kf;
  }

  if (!(groups = g_key_file_get_groups (kf, &n_groups))) {
    goto free_kf;
  }

  for (i = 0; i < n_groups; i++) {
    gchar *group = groups[i];

    if (g_str_has_prefix (group, "Track")) {
      if (!create_track (kf, group, timeline)) {
        GST_ERROR ("couldn't create object for %s", group);
        ret = FALSE;
        break;
      }
    }

    else if (g_str_has_prefix (group, "Layer")) {
      if (!(cur_layer = create_layer (kf, group, timeline))) {
        GST_ERROR ("couldn't create object for %s", group);
        ret = FALSE;
        break;
      }
    }

    else if (g_str_has_prefix (group, "Object")) {
      if (!cur_layer) {
        GST_ERROR ("Group %s occurs outside of Layer", group);
        ret = FALSE;
        break;
      }

      if (!create_object (kf, group, cur_layer)) {
        GST_ERROR ("couldn't create object for %s", group);
        ret = FALSE;
        break;
      }
    }

    else if (g_str_equal (group, "General")) {
      continue;
    }

    else {
      GST_ERROR ("Unrecognized group name %s", group);
      ret = FALSE;
      break;
    }
  }

  g_strfreev (groups);

free_kf:
  g_key_file_free (kf);
  return ret;
}
