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
 * SECTION:ges-keyfile-formatter
 * @short_description: GKeyFile formatter
 **/

#include <gst/gst.h>
#include <stdlib.h>
#include "ges.h"
#include "ges-internal.h"

G_DEFINE_TYPE (GESKeyfileFormatter, ges_keyfile_formatter, GES_TYPE_FORMATTER);

/* for ini format */
static gboolean save_keyfile (GESFormatter * keyfile_formatter,
    GESTimeline * timeline);
static gboolean load_keyfile (GESFormatter * keyfile_formatter,
    GESTimeline * timeline);

static void
ges_keyfile_formatter_class_init (GESKeyfileFormatterClass * klass)
{
  GESFormatterClass *formatter_klass;

  formatter_klass = GES_FORMATTER_CLASS (klass);

  formatter_klass->save = save_keyfile;
  formatter_klass->load = load_keyfile;
}

static void
ges_keyfile_formatter_init (GESKeyfileFormatter * object)
{
}

/**
 * ges_keyfile_formatter_new:
 *
 * Creates a new #GESKeyfileFormatter.
 *
 * Returns: The newly created #GESKeyfileFormatter.
 */
GESKeyfileFormatter *
ges_keyfile_formatter_new (void)
{
  return g_object_new (GES_TYPE_KEYFILE_FORMATTER, NULL);
}

static gboolean
save_keyfile (GESFormatter * keyfile_formatter, GESTimeline * timeline)
{
  GKeyFile *kf;
  GList *tmp, *tracks, *layers;
  int i = 0;
  int n_objects = 0;
  gchar buffer[255];
  gchar *data;
  gsize length;

  GST_DEBUG ("saving keyfile_formatter");

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
    caps = gst_caps_to_string (ges_track_get_caps (track));

    g_key_file_set_value (kf, buffer, "type", type);
    g_key_file_set_string (kf, buffer, "caps", caps);

    g_free (caps);
    g_free (type);
    gst_object_unref (track);
    tmp->data = NULL;
  }

  g_list_free (tracks);

  layers = ges_timeline_get_layers (timeline);

  for (i = 0, tmp = layers; tmp; i++, tmp = tmp->next) {
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

    g_key_file_set_integer (kf, buffer, "priority",
        ges_timeline_layer_get_priority (layer));
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

  g_list_foreach (layers, (GFunc) g_object_unref, NULL);
  g_list_free (layers);

  data = g_key_file_to_data (kf, &length, NULL);
  ges_formatter_set_data (keyfile_formatter, data, length);
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
load_keyfile (GESFormatter * keyfile_formatter, GESTimeline * timeline)
{
  GKeyFile *kf;
  GError *error = NULL;
  gboolean ret = TRUE;
  gchar **groups;
  gsize n_groups, i;
  GESTimelineLayer *cur_layer = NULL;
  gchar *data;
  gsize length;

  kf = g_key_file_new ();
  data = ges_formatter_get_data (keyfile_formatter, &length);
  if (!g_key_file_load_from_data (kf, data, length, G_KEY_FILE_NONE, &error)) {
    ret = FALSE;
    GST_ERROR ("%s", error->message);
    GST_INFO ("%s", data);
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
