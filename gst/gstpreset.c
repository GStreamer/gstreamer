/* GStreamer
 * Copyright (C) 2006 Stefan Kost <ensonic@users.sf.net>
 *
 * gstpreset.c: helper interface for element presets
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
 * SECTION:gstpreset
 * @short_description: helper interface for element presets
 *
 * This interface offers methods to query and manipulate parameter preset sets.
 * A preset is a bunch of property settings, together with meta data and a name.
 * The name of a preset serves as key for subsequent method calls to manipulate
 * single presets.
 * All instances of one type will share the list of presets. The list is created
 * on demand, if presets are not used, the list is not created.
 *
 * The interface comes with a default implementation that servers most plugins.
 * Wrapper plugins will override most methods to implement support for the
 * native preset format of those wrapped plugins.
 * One method that is useful to be overridde is gst_preset_get_property_names().
 * With that one can control which properties are saved and in which order.
 */
/* @todo:
 * - we need locks to avoid two instances manipulating the preset list -> flock
 *   better save the new file to a tempfile and then rename
 * - need to add support for GstChildProxy
 * - how can we support both Preferences and Presets,
 *   - preferences = static settings, configurations (non controlable)
 *     e.g. alsasink:device
 *   - preset = a snapshot of dynamic params
 *     e.g. volume:volume
 *   - we could use a flag for _get_preset_names()
 *   - we could even use tags as part of preset metadata
 *     e.g. quality, performance, preset, config
 *     if there are some agreed tags, one could say, please optimize the pipline
 *     for 'performance'
 * - should there be a 'preset-list' property to get the preset list
 *   (and to connect a notify:: to to listen for changes)
 * - should there be a 'preset-name' property so that we can set a preset via
 *   gst-launch
 *
 * - do we want to ship presets for some elements?
 */

#include "gst_private.h"

#include "gstpreset.h"

#include "stdlib.h"
#include <unistd.h>
#include <glib/gstdio.h>

#define GST_CAT_DEFAULT preset_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static GQuark preset_list_quark = 0;
static GQuark preset_path_quark = 0;
static GQuark preset_data_quark = 0;
static GQuark preset_meta_quark = 0;
static GQuark instance_list_quark = 0;

/*static GQuark property_list_quark = 0;*/

/* default iface implementation */

/* max character per line */
#define LINE_LEN 200

static gboolean
preset_get_storage (GstPreset * self, GList ** presets,
    GHashTable ** preset_meta, GHashTable ** preset_data)
{
  gboolean res = FALSE;
  GType type = G_TYPE_FROM_INSTANCE (self);

  g_assert (presets);

  if ((*presets = g_type_get_qdata (type, preset_list_quark))) {
    GST_DEBUG ("have presets");
    res = TRUE;
  }
  if (preset_meta) {
    if (!(*preset_meta = g_type_get_qdata (type, preset_meta_quark))) {
      *preset_meta = g_hash_table_new (g_str_hash, g_str_equal);
      g_type_set_qdata (type, preset_meta_quark, (gpointer) * preset_meta);
      GST_DEBUG ("new meta hash");
    }
  }
  if (preset_data) {
    if (!(*preset_data = g_type_get_qdata (type, preset_data_quark))) {
      *preset_data = g_hash_table_new (g_str_hash, g_str_equal);
      g_type_set_qdata (type, preset_data_quark, (gpointer) * preset_data);
      GST_DEBUG ("new data hash");
    }
  }
  GST_INFO ("%s: presets: %p, %p, %p", G_OBJECT_TYPE_NAME (self),
      *presets, (preset_meta ? *preset_meta : 0),
      (preset_data ? *preset_data : 0));
  return (res);
}

static const gchar *
preset_get_path (GstPreset * self)
{
  GType type = G_TYPE_FROM_INSTANCE (self);
  gchar *preset_path;

  preset_path = (gchar *) g_type_get_qdata (type, preset_path_quark);
  if (!preset_path) {
    const gchar *element_name, *plugin_name, *file_name;
    gchar *preset_dir;
    GstElementFactory *factory;
    GstPlugin *plugin;

    element_name = G_OBJECT_TYPE_NAME (self);
    GST_INFO ("element_name: '%s'", element_name);

    factory = GST_ELEMENT_GET_CLASS (self)->elementfactory;
    GST_INFO ("factory: %p", factory);
    if (factory) {
      plugin_name = GST_PLUGIN_FEATURE (factory)->plugin_name;
      GST_INFO ("plugin_name: '%s'", plugin_name);
      plugin = gst_default_registry_find_plugin (plugin_name);
      GST_INFO ("plugin: %p", plugin);
      file_name = gst_plugin_get_filename (plugin);
      GST_INFO ("file_name: '%s'", file_name);
      /*
         '/home/ensonic/buzztard/lib/gstreamer-0.10/libgstsimsyn.so'
         -> '/home/ensonic/buzztard/share/gstreamer-0.10/presets/GstSimSyn.prs'
         -> '$HOME/.gstreamer-0.10/presets/GstSimSyn.prs'

         '/usr/lib/gstreamer-0.10/libgstaudiofx.so'
         -> '/usr/share/gstreamer-0.10/presets/GstAudioPanorama.prs'
         -> '$HOME/.gstreamer-0.10/presets/GstAudioPanorama.prs'
       */
    }

    preset_dir =
        g_build_filename (g_get_home_dir (), ".gstreamer-0.10", "presets",
        NULL);
    GST_INFO ("preset_dir: '%s'", preset_dir);
    preset_path =
        g_strdup_printf ("%s" G_DIR_SEPARATOR_S "%s.prs", preset_dir,
        element_name);
    GST_INFO ("preset_path: '%s'", preset_path);
    g_mkdir_with_parents (preset_dir, 0755);
    g_free (preset_dir);

    /* attach the preset path to the type */
    g_type_set_qdata (type, preset_path_quark, (gpointer) preset_path);
  }
  return (preset_path);
}

static gboolean
preset_skip_property (GParamSpec * property)
{
  if (!(property->flags & (G_PARAM_READABLE | G_PARAM_WRITABLE)) ||
      (property->flags & G_PARAM_CONSTRUCT_ONLY))
    return TRUE;
  return FALSE;
}

static void
preset_cleanup (gpointer user_data, GObject * self)
{
  GType type = (GType) user_data;
  GList *instances;

  /* remove instance from instance list (if not yet there) */
  instances = (GList *) g_type_get_qdata (type, instance_list_quark);
  if (instances != NULL) {
    instances = g_list_remove (instances, self);
    GST_INFO ("old instanc removed");
    g_type_set_qdata (type, instance_list_quark, (gpointer) instances);
  }
}

static gchar **
gst_preset_default_get_preset_names (GstPreset * self)
{
  GType type = G_TYPE_FROM_INSTANCE (self);
  GList *presets;
  GList *instances;
  GHashTable *preset_meta, *preset_data;
  gboolean found = FALSE;

  /* get the presets from the type */
  if (!preset_get_storage (self, &presets, &preset_meta, &preset_data)) {
    const gchar *preset_path = preset_get_path (self);
    FILE *in;

    GST_DEBUG ("probing preset file: '%s'", preset_path);

    /* read presets */
    if ((in = fopen (preset_path, "rb"))) {
      const gchar *element_name = G_OBJECT_TYPE_NAME (self);
      gchar line[LINE_LEN + 1], *str, *val;
      gboolean parse_preset;
      gchar *preset_name;
      GHashTable *meta;
      GHashTable *data;
      GObjectClass *klass;
      GParamSpec *property;

      GST_DEBUG ("loading preset file: '%s'", preset_path);

      /* read header */
      if (!fgets (line, LINE_LEN, in))
        goto eof_error;
      if (strcmp (line, "GStreamer Preset\n")) {
        GST_WARNING ("%s:1: file id expected", preset_path);
        goto eof_error;
      }
      if (!fgets (line, LINE_LEN, in))
        goto eof_error;
      /* @todo: what version (core?) */
      if (!fgets (line, LINE_LEN, in))
        goto eof_error;
      if (strcmp (g_strchomp (line), element_name)) {
        GST_WARNING ("%s:3: wrong element name", preset_path);
        goto eof_error;
      }
      if (!fgets (line, LINE_LEN, in))
        goto eof_error;
      if (*line != '\n') {
        GST_WARNING ("%s:4: blank line expected", preset_path);
        goto eof_error;
      }

      klass = G_OBJECT_CLASS (GST_ELEMENT_GET_CLASS (self));

      /* read preset entries */
      while (!feof (in)) {
        /* read preset entry */
        if (!fgets (line, LINE_LEN, in))
          break;
        g_strchomp (line);
        if (*line) {
          preset_name = g_strdup (line);
          GST_INFO ("%s: preset '%s'", preset_path, preset_name);

          data = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
          meta =
              g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

          /* read preset lines */
          parse_preset = TRUE;
          while (parse_preset) {
            if (!fgets (line, LINE_LEN, in) || (*line == '\n')) {
              GST_DEBUG ("preset done");
              parse_preset = FALSE;
              break;
            }
            str = g_strchomp (line);
            while (*str) {
              if (*str == ':') {
                *str = '\0';
                GST_DEBUG ("meta[%s]='%s'", line, &str[1]);
                if ((val = g_hash_table_lookup (meta, line))) {
                  g_free (val);
                  g_hash_table_insert (meta, (gpointer) line,
                      (gpointer) g_strdup (&str[1]));
                } else {
                  g_hash_table_insert (meta, (gpointer) g_strdup (line),
                      (gpointer) g_strdup (&str[1]));
                }
                break;
              } else if (*str == '=') {
                *str = '\0';
                GST_DEBUG ("data[%s]='%s'", line, &str[1]);
                if ((property = g_object_class_find_property (klass, line))) {
                  g_hash_table_insert (data, (gpointer) property->name,
                      (gpointer) g_strdup (&str[1]));
                } else {
                  GST_WARNING ("%s: Invalid property '%s'", preset_path, line);
                }
                break;
              }
              str++;
            }
            /* @todo: handle childproxy properties
             * <property>[child]=<value>
             */
          }

          GST_INFO ("preset: %p, %p", meta, data);
          g_hash_table_insert (preset_data, (gpointer) preset_name,
              (gpointer) data);
          g_hash_table_insert (preset_meta, (gpointer) preset_name,
              (gpointer) meta);
          presets =
              g_list_insert_sorted (presets, (gpointer) preset_name,
              (GCompareFunc) strcmp);
        }
      }

    eof_error:
      fclose (in);
    } else {
      GST_INFO ("can't open preset file: '%s'", preset_path);
    }

    /* attach the preset to the type */
    g_type_set_qdata (type, preset_list_quark, (gpointer) presets);
  }

  /* insert instance in instance list (if not yet there) */
  instances = (GList *) g_type_get_qdata (type, instance_list_quark);
  if (instances != NULL) {
    if (g_list_find (instances, self))
      found = TRUE;
  }
  if (!found) {
    GST_INFO ("new instance added");
    /* register a weak ref, to clean up when the object gets destroyed */
    g_object_weak_ref (G_OBJECT (self), preset_cleanup, (gpointer) type);
    instances = g_list_prepend (instances, self);
    g_type_set_qdata (type, instance_list_quark, (gpointer) instances);
  }
  /* copy strings to avoid races */
  if (presets) {
    gchar **preset_names = g_new (gchar *, g_list_length (presets) + 1);
    GList *node;
    guint i = 0;

    for (node = presets; node; node = g_list_next (node)) {
      preset_names[i++] = g_strdup (node->data);
    }
    preset_names[i] = NULL;
    return (preset_names);
  }
  return (NULL);
}

static GList *
gst_preset_default_get_property_names (GstPreset * self)
{
  GParamSpec **properties, *property;
  GList *names = NULL;
  guint i, number_of_properties;

  if ((properties = g_object_class_list_properties (G_OBJECT_CLASS
              (GST_ELEMENT_GET_CLASS (self)), &number_of_properties))) {
    GST_INFO ("  filtering properties: %u", number_of_properties);
    for (i = 0; i < number_of_properties; i++) {
      property = properties[i];
      if (preset_skip_property (property) ||
          (property->flags & GST_PARAM_CONTROLLABLE))
        continue;

      names = g_list_prepend (names, property->name);
    }
    for (i = 0; i < number_of_properties; i++) {
      property = properties[i];
      if (preset_skip_property (property) ||
          !(property->flags & GST_PARAM_CONTROLLABLE))
        continue;

      names = g_list_prepend (names, property->name);
    }
    g_free (properties);
  } else {
    GST_INFO ("no properties");
  }
  return names;
}

static gboolean
gst_preset_default_load_preset (GstPreset * self, const gchar * name)
{
  GList *presets;
  GHashTable *preset_data;

  /* get the presets from the type */
  if (preset_get_storage (self, &presets, NULL, &preset_data)) {
    GList *node;

    if ((node = g_list_find_custom (presets, name, (GCompareFunc) strcmp))) {
      GHashTable *data = g_hash_table_lookup (preset_data, node->data);
      GList *properties;
      GType base, parent;
      gchar *val = NULL;

      GST_DEBUG ("loading preset : '%s', data : %p (size=%d)", name, data,
          g_hash_table_size (data));

      /* preset found, now set values */
      if ((properties = gst_preset_get_property_names (self))) {
        GParamSpec *property;
        GList *node;

        for (node = properties; node; node = g_list_next (node)) {
          property = g_object_class_find_property (G_OBJECT_CLASS
              (GST_ELEMENT_GET_CLASS (self)), node->data);

          /* check if we have a settings for this property */
          if ((val = (gchar *) g_hash_table_lookup (data, property->name))) {
            GST_DEBUG ("setting value '%s' for property '%s'", val,
                property->name);
            /* get base type */
            base = property->value_type;
            while ((parent = g_type_parent (base)))
              base = parent;

            switch (base) {
              case G_TYPE_INT:
              case G_TYPE_UINT:
              case G_TYPE_BOOLEAN:
              case G_TYPE_ENUM:
                g_object_set (G_OBJECT (self), property->name, atoi (val),
                    NULL);
                break;
              case G_TYPE_LONG:
              case G_TYPE_ULONG:
                g_object_set (G_OBJECT (self), property->name, atol (val),
                    NULL);
                break;
              case G_TYPE_FLOAT:
                g_object_set (G_OBJECT (self), property->name,
                    (float) g_ascii_strtod (val, NULL), NULL);
                break;
              case G_TYPE_DOUBLE:
                g_object_set (G_OBJECT (self), property->name,
                    g_ascii_strtod (val, NULL), NULL);
                break;
              case G_TYPE_STRING:
                g_object_set (G_OBJECT (self), property->name, val, NULL);
                break;
              default:
                GST_WARNING
                    ("incomplete implementation for GParamSpec type '%s'",
                    G_PARAM_SPEC_TYPE_NAME (property));
            }
          } else {
            GST_INFO ("parameter '%s' not in preset", property->name);
          }
        }
        g_list_free (properties);
        return (TRUE);
      } else {
        GST_INFO ("no properties");
      }
    }
  } else {
    GST_INFO ("no presets");
  }
  return (FALSE);
}

static void
preset_store_meta (gpointer key, gpointer value, gpointer user_data)
{
  if (key && value) {
    fprintf ((FILE *) user_data, "%s:%s\n", (gchar *) key, (gchar *) value);
  }
}

static void
preset_store_data (gpointer key, gpointer value, gpointer user_data)
{
  if (key && value) {
    fprintf ((FILE *) user_data, "%s=%s\n", (gchar *) key, (gchar *) value);
  }
}

static gboolean
gst_preset_default_save_presets_file (GstPreset * self)
{
  gboolean res = FALSE;
  GList *presets;
  GHashTable *preset_meta, *preset_data;
  const gchar *preset_path = preset_get_path (self);

  /* get the presets from the type */
  if (preset_get_storage (self, &presets, &preset_meta, &preset_data)) {
    FILE *out;
    gchar *bak_file_name;
    gboolean backup = TRUE;

    GST_DEBUG ("saving preset file: '%s'", preset_path);

    /* create backup if possible */
    bak_file_name = g_strdup_printf ("%s.bak", preset_path);
    if (g_file_test (bak_file_name, G_FILE_TEST_EXISTS)) {
      if (g_unlink (bak_file_name)) {
        backup = FALSE;
        GST_INFO ("cannot remove old backup file : %s", bak_file_name);
      }
    }
    if (backup) {
      if (g_rename (preset_path, bak_file_name)) {
        GST_INFO ("cannot backup file : %s -> %s", preset_path, bak_file_name);
      }
    }
    g_free (bak_file_name);

    /* write presets */
    if ((out = fopen (preset_path, "wb"))) {
      const gchar *element_name = G_OBJECT_TYPE_NAME (self);
      gchar *preset_name;
      GList *node;
      GHashTable *meta, *data;

      /* write header */
      if (!(fputs ("GStreamer Preset\n", out)))
        goto eof_error;
      /* @todo: what version (core?) */
      if (!(fputs ("1.0\n", out)))
        goto eof_error;
      if (!(fputs (element_name, out)))
        goto eof_error;
      if (!(fputs ("\n\n", out)))
        goto eof_error;

      /* write preset entries */
      for (node = presets; node; node = g_list_next (node)) {
        preset_name = node->data;
        /* write preset entry */
        if (!(fputs (preset_name, out)))
          goto eof_error;
        if (!(fputs ("\n", out)))
          goto eof_error;

        /* write data */
        meta = g_hash_table_lookup (preset_meta, (gpointer) preset_name);
        g_hash_table_foreach (meta, preset_store_meta, out);
        data = g_hash_table_lookup (preset_data, (gpointer) preset_name);
        g_hash_table_foreach (data, preset_store_data, out);
        if (!(fputs ("\n", out)))
          goto eof_error;
      }

      res = TRUE;
    eof_error:
      fclose (out);
    }
  } else {
    GST_DEBUG
        ("no presets, trying to unlink possibly existing preset file: '%s'",
        preset_path);
    unlink (preset_path);
  }
  return (res);
}

static gboolean
gst_preset_default_save_preset (GstPreset * self, const gchar * name)
{
  GType type = G_TYPE_FROM_INSTANCE (self);
  GList *presets;
  GHashTable *preset_meta, *preset_data;
  GHashTable *meta, *data;
  GList *properties;
  GType base, parent;
  gchar *str = NULL, buffer[30 + 1];

  /*guint flags; */

  GST_INFO ("saving new preset: %s", name);

  /* get the presets from the type */
  preset_get_storage (self, &presets, &preset_meta, &preset_data);

  data = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
  meta = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

  /* take copies of current gobject properties from self */
  if ((properties = gst_preset_get_property_names (self))) {
    GParamSpec *property;
    GList *node;

    for (node = properties; node; node = g_list_next (node)) {
      property = g_object_class_find_property (G_OBJECT_CLASS
          (GST_ELEMENT_GET_CLASS (self)), node->data);

      /* get base type */
      base = property->value_type;
      while ((parent = g_type_parent (base)))
        base = parent;
      /* get value and serialize */
      GST_INFO ("  storing property: %s (type is %s)", property->name,
          g_type_name (base));

      switch (base) {
        case G_TYPE_BOOLEAN:
        case G_TYPE_ENUM:
        case G_TYPE_INT:{
          gint val;

          g_object_get (G_OBJECT (self), property->name, &val, NULL);
          str = g_strdup_printf ("%d", val);
        }
          break;
        case G_TYPE_UINT:{
          guint val;

          g_object_get (G_OBJECT (self), property->name, &val, NULL);
          str = g_strdup_printf ("%u", val);
        }
          break;
        case G_TYPE_LONG:{
          glong val;

          g_object_get (G_OBJECT (self), property->name, &val, NULL);
          str = g_strdup_printf ("%ld", val);
        }
          break;
        case G_TYPE_ULONG:{
          gulong val;

          g_object_get (G_OBJECT (self), property->name, &val, NULL);
          str = g_strdup_printf ("%lu", val);
        }
          break;
        case G_TYPE_FLOAT:{
          gfloat val;

          g_object_get (G_OBJECT (self), property->name, &val, NULL);
          g_ascii_dtostr (buffer, 30, (gdouble) val);
          str = g_strdup (buffer);
        }
          break;
        case G_TYPE_DOUBLE:{
          gdouble val;

          g_object_get (G_OBJECT (self), property->name, &val, NULL);
          g_ascii_dtostr (buffer, 30, val);
          str = g_strdup (buffer);
        }
          break;
        case G_TYPE_STRING:
          g_object_get (G_OBJECT (self), property->name, &str, NULL);
          if (str && !*str)
            str = NULL;
          break;
        default:
          GST_WARNING ("incomplete implementation for GParamSpec type '%s'",
              G_PARAM_SPEC_TYPE_NAME (property));
      }
      if (str) {
        g_hash_table_insert (data, (gpointer) property->name, (gpointer) str);
        str = NULL;
      }
    }
    /* @todo: handle childproxy properties as well */
    GST_INFO ("  saved");
    g_list_free (properties);
  } else {
    GST_INFO ("no properties");
  }

  /*
   * @todo: flock(fileno())
   * http://www.ecst.csuchico.edu/~beej/guide/ipc/flock.html
   */
  g_hash_table_insert (preset_data, (gpointer) name, (gpointer) data);
  g_hash_table_insert (preset_meta, (gpointer) name, (gpointer) meta);
  presets =
      g_list_insert_sorted (presets, (gpointer) name, (GCompareFunc) strcmp);
  /* attach the preset list to the type */
  g_type_set_qdata (type, preset_list_quark, (gpointer) presets);
  GST_INFO ("done");

  return (gst_preset_default_save_presets_file (self));
}

static gboolean
gst_preset_default_rename_preset (GstPreset * self, const gchar * old_name,
    const gchar * new_name)
{
  GType type = G_TYPE_FROM_INSTANCE (self);
  GList *presets;
  GHashTable *preset_meta, *preset_data;

  /* get the presets from the type */
  if (preset_get_storage (self, &presets, &preset_meta, &preset_data)) {
    GList *node;

    if ((node = g_list_find_custom (presets, old_name, (GCompareFunc) strcmp))) {
      GHashTable *meta, *data;

      /* readd under new name */
      presets =
          g_list_insert_sorted (presets, (gpointer) new_name,
          (GCompareFunc) strcmp);

      /* readd the hash entries */
      if ((meta = g_hash_table_lookup (preset_meta, node->data))) {
        g_hash_table_remove (preset_meta, node->data);
        g_hash_table_insert (preset_meta, (gpointer) new_name, (gpointer) meta);
      }
      if ((data = g_hash_table_lookup (preset_data, node->data))) {
        g_hash_table_remove (preset_data, node->data);
        g_hash_table_insert (preset_data, (gpointer) new_name, (gpointer) data);
      }

      /* remove the old one */
      presets = g_list_delete_link (presets, node);

      GST_INFO ("preset moved '%s' -> '%s'", old_name, new_name);
      g_type_set_qdata (type, preset_list_quark, (gpointer) presets);

      return (gst_preset_default_save_presets_file (self));
    }
  } else {
    GST_WARNING ("no presets");
  }
  return (FALSE);
}

static gboolean
gst_preset_default_delete_preset (GstPreset * self, const gchar * name)
{
  GType type = G_TYPE_FROM_INSTANCE (self);
  GList *presets;
  GHashTable *preset_meta, *preset_data;

  /* get the presets from the type */
  if (preset_get_storage (self, &presets, &preset_meta, &preset_data)) {
    GList *node;

    if ((node = g_list_find_custom (presets, name, (GCompareFunc) strcmp))) {
      GHashTable *meta, *data;

      /* free the hash entries */
      if ((meta = g_hash_table_lookup (preset_meta, node->data))) {
        g_hash_table_remove (preset_meta, node->data);
        g_hash_table_destroy (meta);
      }
      if ((data = g_hash_table_lookup (preset_data, node->data))) {
        g_hash_table_remove (preset_data, node->data);
        g_hash_table_destroy (data);
      }

      /* remove the found one */
      presets = g_list_delete_link (presets, node);

      GST_INFO ("preset removed '%s'", name);
      g_type_set_qdata (type, preset_list_quark, (gpointer) presets);
      g_free ((gpointer) name);

      return (gst_preset_default_save_presets_file (self));
    }
  } else {
    GST_WARNING ("no presets");
  }
  return (FALSE);
}

static gboolean
gst_preset_default_set_meta (GstPreset * self, const gchar * name,
    const gchar * tag, gchar * value)
{
  gboolean res = FALSE;
  GList *presets;
  GHashTable *preset_meta;

  /* get the presets from the type */
  if (preset_get_storage (self, &presets, &preset_meta, NULL)) {
    GList *node;

    if ((node = g_list_find_custom (presets, name, (GCompareFunc) strcmp))) {
      GHashTable *meta = g_hash_table_lookup (preset_meta, node->data);
      gchar *old_value;
      gboolean changed = FALSE;

      if ((old_value = g_hash_table_lookup (meta, tag))) {
        g_free (old_value);
        changed = TRUE;
      }
      if (value) {
        if (changed)
          tag = g_strdup (tag);
        g_hash_table_insert (meta, (gpointer) tag, g_strdup (value));
        changed = TRUE;
      }
      if (changed) {
        res = gst_preset_default_save_presets_file (self);
      }
    }
  } else {
    GST_WARNING ("no presets");
  }
  return (res);
}

static gboolean
gst_preset_default_get_meta (GstPreset * self, const gchar * name,
    const gchar * tag, gchar ** value)
{
  gboolean res = FALSE;
  GList *presets;
  GHashTable *preset_meta;

  /* get the presets from the type */
  if (preset_get_storage (self, &presets, &preset_meta, NULL)) {
    GList *node;

    if ((node = g_list_find_custom (presets, name, (GCompareFunc) strcmp))) {
      GHashTable *meta = g_hash_table_lookup (preset_meta, node->data);
      gchar *new_value;

      if ((new_value = g_hash_table_lookup (meta, tag))) {
        *value = g_strdup (new_value);
        res = TRUE;
      }
    }
  } else {
    GST_WARNING ("no presets");
  }
  if (!res)
    *value = NULL;
  return (res);
}

static void
gst_preset_default_create_preset (GstPreset * self)
{
  GList *properties;
  GType base, parent;

  if ((properties = gst_preset_get_property_names (self))) {
    GParamSpec *property;
    GList *node;
    gdouble rnd;

    for (node = properties; node; node = g_list_next (node)) {
      property = g_object_class_find_property (G_OBJECT_CLASS
          (GST_ELEMENT_GET_CLASS (self)), node->data);

      rnd = ((gdouble) rand ()) / (RAND_MAX + 1.0);

      /* get base type */
      base = property->value_type;
      while ((parent = g_type_parent (base)))
        base = parent;
      GST_INFO ("set random value for property: %s (type is %s)",
          property->name, g_type_name (base));

      switch (base) {
        case G_TYPE_BOOLEAN:{
          g_object_set (self, property->name, (gboolean) (2.0 * rnd), NULL);
        }
          break;
        case G_TYPE_INT:{
          const GParamSpecInt *int_property = G_PARAM_SPEC_INT (property);

          g_object_set (self, property->name,
              (gint) (int_property->minimum + ((int_property->maximum -
                          int_property->minimum) * rnd)), NULL);
        } break;
        case G_TYPE_UINT:{
          const GParamSpecUInt *uint_property = G_PARAM_SPEC_UINT (property);

          g_object_set (self, property->name,
              (guint) (uint_property->minimum + ((uint_property->maximum -
                          uint_property->minimum) * rnd)), NULL);
        } break;
        case G_TYPE_DOUBLE:{
          const GParamSpecDouble *double_property =
              G_PARAM_SPEC_DOUBLE (property);

          g_object_set (self, property->name,
              (gdouble) (double_property->minimum + ((double_property->maximum -
                          double_property->minimum) * rnd)), NULL);
        } break;
        case G_TYPE_ENUM:{
          const GParamSpecEnum *enum_property = G_PARAM_SPEC_ENUM (property);
          const GEnumClass *enum_class = enum_property->enum_class;

          g_object_set (self, property->name,
              (gulong) (enum_class->minimum + ((enum_class->maximum -
                          enum_class->minimum) * rnd)), NULL);
        } break;
        default:
          GST_WARNING ("incomplete implementation for GParamSpec type '%s'",
              G_PARAM_SPEC_TYPE_NAME (property));
      }
    }
    /* @todo: handle childproxy properties as well */
  }
}

/* wrapper */

/**
 * gst_preset_get_preset_names:
 * @self: a #GObject that implements #GstPreset
 *
 * Get a copy of preset names as a NULL terminated string array. Free with
 * g_strfreev() wen done.
 *
 * Returns: list with names
 */
gchar **
gst_preset_get_preset_names (GstPreset * self)
{
  g_return_val_if_fail (GST_IS_PRESET (self), NULL);

  return (GST_PRESET_GET_INTERFACE (self)->get_preset_names (self));
}

/**
 * gst_preset_get_property_names:
 * @self: a #GObject that implements #GstPreset
 *
 * Get a the gobject property names to use for presets.
 *
 * Returns: list with names
 */
GList *
gst_preset_get_property_names (GstPreset * self)
{
  g_return_val_if_fail (GST_IS_PRESET (self), NULL);

  return (GST_PRESET_GET_INTERFACE (self)->get_property_names (self));
}

/**
 * gst_preset_load_preset:
 * @self: a #GObject that implements #GstPreset
 * @name: preset name to load
 *
 * Load the given preset.
 *
 * Returns: %TRUE for success, %FALSE if e.g. there is no preset with that @name
 */
gboolean
gst_preset_load_preset (GstPreset * self, const gchar * name)
{
  g_return_val_if_fail (GST_IS_PRESET (self), FALSE);
  g_return_val_if_fail (name, FALSE);

  return (GST_PRESET_GET_INTERFACE (self)->load_preset (self, name));
}

/**
 * gst_preset_save_preset:
 * @self: a #GObject that implements #GstPreset
 * @name: preset name to save
 *
 * Save the current preset under the given name. If there is already a preset by
 * this @name it will be overwritten.
 *
 * Returns: %TRUE for success, %FALSE
 */
gboolean
gst_preset_save_preset (GstPreset * self, const gchar * name)
{
  g_return_val_if_fail (GST_IS_PRESET (self), FALSE);
  g_return_val_if_fail (name, FALSE);

  return (GST_PRESET_GET_INTERFACE (self)->save_preset (self, name));
}

/**
 * gst_preset_rename_preset:
 * @self: a #GObject that implements #GstPreset
 * @old_name: current preset name
 * @new_name: new preset name
 *
 * Renames a preset. If there is already a preset by thr @new_name it will be
 * overwritten.
 *
 * Returns: %TRUE for success, %FALSE if e.g. there is no preset with @old_name
 */
gboolean
gst_preset_rename_preset (GstPreset * self, const gchar * old_name,
    const gchar * new_name)
{
  g_return_val_if_fail (GST_IS_PRESET (self), FALSE);
  g_return_val_if_fail (old_name, FALSE);
  g_return_val_if_fail (new_name, FALSE);

  return (GST_PRESET_GET_INTERFACE (self)->rename_preset (self, old_name,
          new_name));
}

/**
 * gst_preset_delete_preset:
 * @self: a #GObject that implements #GstPreset
 * @name: preset name to remove
 *
 * Delete the given preset.
 *
 * Returns: %TRUE for success, %FALSE if e.g. there is no preset with that @name
 */
gboolean
gst_preset_delete_preset (GstPreset * self, const gchar * name)
{
  g_return_val_if_fail (GST_IS_PRESET (self), FALSE);
  g_return_val_if_fail (name, FALSE);

  return (GST_PRESET_GET_INTERFACE (self)->delete_preset (self, name));
}

/**
 * gst_preset_set_meta:
 * @self: a #GObject that implements #GstPreset
 * @name: preset name
 * @tag: meta data item name
 * @value: new value
 *
 * Sets a new @value for an existing meta data item or adds a new item. Meta
 * data @tag names can be something like e.g. "comment". Supplying %NULL for the
 * @value will unset an existing value.
 *
 * Returns: %TRUE for success, %FALSE if e.g. there is no preset with that @name
 */
gboolean
gst_preset_set_meta (GstPreset * self, const gchar * name, const gchar * tag,
    gchar * value)
{
  g_return_val_if_fail (GST_IS_PRESET (self), FALSE);
  g_return_val_if_fail (name, FALSE);
  g_return_val_if_fail (tag, FALSE);

  return GST_PRESET_GET_INTERFACE (self)->set_meta (self, name, tag, value);
}

/**
 * gst_preset_get_meta:
 * @self: a #GObject that implements #GstPreset
 * @name: preset name
 * @tag: meta data item name
 * @value: value
 *
 * Gets the @value for an existing meta data @tag. Meta data @tag names can be
 * something like e.g. "comment". Returned values need to be released when done.
 *
 * Returns: %TRUE for success, %FALSE if e.g. there is no preset with that @name
 * or no value for the given @tag
 */
gboolean
gst_preset_get_meta (GstPreset * self, const gchar * name, const gchar * tag,
    gchar ** value)
{
  g_return_val_if_fail (GST_IS_PRESET (self), FALSE);
  g_return_val_if_fail (name, FALSE);
  g_return_val_if_fail (tag, FALSE);
  g_return_val_if_fail (value, FALSE);

  return GST_PRESET_GET_INTERFACE (self)->get_meta (self, name, tag, value);
}

/**
 * gst_preset_create_preset:
 * @self: a #GObject that implements #GstPreset
 *
 * Create a new randomized preset. This method is optional. If not implemented
 * true randomization will be applied.
 */
void
gst_preset_create_preset (GstPreset * self)
{
  g_return_if_fail (GST_IS_PRESET (self));

  GST_PRESET_GET_INTERFACE (self)->create_preset (self);
}

/* class internals */

static void
gst_preset_class_init (GstPresetInterface * iface)
{
  iface->get_preset_names = gst_preset_default_get_preset_names;
  iface->get_property_names = gst_preset_default_get_property_names;

  iface->load_preset = gst_preset_default_load_preset;
  iface->save_preset = gst_preset_default_save_preset;
  iface->rename_preset = gst_preset_default_rename_preset;
  iface->delete_preset = gst_preset_default_delete_preset;

  iface->set_meta = gst_preset_default_set_meta;
  iface->get_meta = gst_preset_default_get_meta;

  iface->create_preset = gst_preset_default_create_preset;
}

static void
gst_preset_base_init (gpointer g_class)
{
  static gboolean initialized = FALSE;

  if (!initialized) {
    /* init default implementation */
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "preset",
        GST_DEBUG_FG_WHITE | GST_DEBUG_BG_BLACK, "preset interface");

    /* create quarks for use with g_type_{g,s}et_qdata() */
    preset_list_quark = g_quark_from_static_string ("GstPreset::presets");
    preset_path_quark = g_quark_from_static_string ("GstPreset::path");
    preset_data_quark = g_quark_from_static_string ("GstPreset::data");
    preset_meta_quark = g_quark_from_static_string ("GstPreset::meta");
    instance_list_quark = g_quark_from_static_string ("GstPreset::instances");
    /*property_list_quark = g_quark_from_static_string ("GstPreset::properties"); */

    initialized = TRUE;
  }
}

GType
gst_preset_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    const GTypeInfo info = {
      sizeof (GstPresetInterface),
      (GBaseInitFunc) gst_preset_base_init,     /* base_init */
      NULL,                     /* base_finalize */
      (GClassInitFunc) gst_preset_class_init,   /* class_init */
      NULL,                     /* class_finalize */
      NULL,                     /* class_data */
      0,
      0,                        /* n_preallocs */
      NULL                      /* instance_init */
    };
    type = g_type_register_static (G_TYPE_INTERFACE, "GstPreset", &info, 0);
  }
  return type;
}
