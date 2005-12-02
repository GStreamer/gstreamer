/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2005 David A. Schleef <ds@schleef.org>
 *
 * gstregistryxml.c: GstRegistry object, support routines
 *
 * This library is free software; you can redistribute it and/or
 * modify it ulnder the terms of the GNU Library General Public
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
#  include "config.h"
#endif

#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <gst/gst_private.h>
#include <gst/gstelement.h>
#include <gst/gsttypefind.h>
#include <gst/gsttypefindfactory.h>
#include <gst/gsturi.h>
#include <gst/gstinfo.h>
#include <gst/gstenumtypes.h>
#include <gst/gstregistry.h>

#include <libxml/xmlreader.h>

#include "glib-compat-private.h"
#include <glib/gstdio.h>

#define BLOCK_SIZE 1024*10

#define GST_CAT_DEFAULT GST_CAT_REGISTRY

#define CLASS(registry)  GST_XML_REGISTRY_CLASS (G_OBJECT_GET_CLASS (registry))

static gboolean
gst_registry_xml_save (GstRegistry * registry, gchar * format, ...)
{
  va_list var_args;

  va_start (var_args, format);

  vfprintf (registry->cache_file, format, var_args);

  va_end (var_args);

  return TRUE;
}

static void
add_to_char_array (gchar *** array, gchar * value)
{
  gchar **new;
  gchar **old = *array;
  gint i = 0;

  /* expensive, but cycles are cheap... */
  if (old)
    while (old[i])
      i++;
  new = g_new0 (gchar *, i + 2);
  new[i] = value;
  while (i > 0) {
    i--;
    new[i] = old[i];
  }
  g_free (old);
  *array = new;
}

/* read a string and copy it into the given location */
static gboolean
read_string (xmlTextReaderPtr reader, gchar ** write_to)
{
  int depth = xmlTextReaderDepth (reader);
  gboolean found = FALSE;

  while (xmlTextReaderRead (reader) == 1) {
    if (xmlTextReaderDepth (reader) == depth)
      return found;
    if (xmlTextReaderNodeType (reader) == XML_READER_TYPE_TEXT) {
      if (found)
        return FALSE;
      *write_to = g_strdup ((gchar *) xmlTextReaderConstValue (reader));
      found = TRUE;
    }
  }
  return FALSE;
}

static gboolean
read_uint (xmlTextReaderPtr reader, guint * write_to)
{
  int depth = xmlTextReaderDepth (reader);
  gboolean found = FALSE;

  while (xmlTextReaderRead (reader) == 1) {
    if (xmlTextReaderDepth (reader) == depth)
      return found;
    if (xmlTextReaderNodeType (reader) == XML_READER_TYPE_TEXT) {
      gchar *ret;
      const gchar *s;

      if (found) {
        GST_DEBUG ("failed to read uint, multiple text nodes");
        return FALSE;
      }
      s = (const gchar *) xmlTextReaderConstValue (reader);
      *write_to = strtol (s, &ret, 0);
      if (s == ret) {
        GST_DEBUG ("failed to read uint, text didn't convert to int");
        return FALSE;
      }
      found = TRUE;
    }
  }
  GST_DEBUG ("failed to read uint, no text node");
  return FALSE;
}

static gboolean
read_enum (xmlTextReaderPtr reader, GType enum_type, guint * write_to)
{
  int depth = xmlTextReaderDepth (reader);
  gboolean found = FALSE;

  if (*write_to)
    return FALSE;
  while (xmlTextReaderRead (reader) == 1) {
    if (xmlTextReaderDepth (reader) == depth)
      return found;
    if (xmlTextReaderNodeType (reader) == XML_READER_TYPE_TEXT) {
      GEnumClass *enum_class;
      GEnumValue *value;

      if (found)
        return FALSE;
      enum_class = g_type_class_ref (enum_type);
      if (!enum_class)
        return FALSE;
      value =
          g_enum_get_value_by_nick (enum_class,
          (gchar *) xmlTextReaderConstValue (reader));
      if (value) {
        *write_to = value->value;
        found = TRUE;
      }
      g_type_class_unref (enum_class);
    }
  }
  return FALSE;
}

static GstStaticPadTemplate *
load_pad_template (xmlTextReaderPtr reader)
{
  int ret;
  int depth = xmlTextReaderDepth (reader);
  gchar *name = NULL, *caps_str = NULL;
  guint direction = 0, presence = 0;

  while ((ret = xmlTextReaderRead (reader)) == 1) {
    if (xmlTextReaderDepth (reader) == depth) {
      GstStaticPadTemplate *template;

      template = g_new0 (GstStaticPadTemplate, 1);
      template->name_template = name;
      template->presence = presence;
      template->direction = direction;
      template->static_caps.string = caps_str;

      return template;
    }
    if (xmlTextReaderNodeType (reader) == XML_READER_TYPE_ELEMENT &&
        xmlTextReaderDepth (reader) == depth + 1) {
      const gchar *tag = (gchar *) xmlTextReaderConstName (reader);

      if (g_str_equal (tag, "nametemplate")) {
        read_string (reader, &name);
      } else if (g_str_equal (tag, "direction")) {
        read_enum (reader, GST_TYPE_PAD_DIRECTION, &direction);
      } else if (g_str_equal (tag, "presence")) {
        read_enum (reader, GST_TYPE_PAD_PRESENCE, &presence);
      } else if (!strncmp (tag, "caps", 4)) {
        read_string (reader, &caps_str);
      }
    }
  }
  g_free (name);
  g_free (caps_str);

  return NULL;
}

static GstPluginFeature *
load_feature (xmlTextReaderPtr reader)
{
  int ret;
  int depth = xmlTextReaderDepth (reader);
  gchar *feature_name =
      (gchar *) xmlTextReaderGetAttribute (reader, BAD_CAST "typename");
  GstPluginFeature *feature;
  GType type;

  GST_DEBUG ("loading feature");

  if (!feature_name)
    return NULL;
  type = g_type_from_name (feature_name);
  g_free (feature_name);
  feature_name = NULL;

  if (!type) {
    return NULL;
  }
  feature = g_object_new (type, NULL);
  if (!feature) {
    return NULL;
  }
  if (!GST_IS_PLUGIN_FEATURE (feature)) {
    g_object_unref (feature);
    return NULL;
  }
  while ((ret = xmlTextReaderRead (reader)) == 1) {
    if (xmlTextReaderDepth (reader) == depth) {
      GST_DEBUG ("loaded feature %p with name %s", feature, feature->name);
      return feature;
    }
    if (xmlTextReaderNodeType (reader) == XML_READER_TYPE_ELEMENT &&
        xmlTextReaderDepth (reader) == depth + 1) {
      const gchar *tag = (gchar *) xmlTextReaderConstName (reader);

      if (g_str_equal (tag, "name"))
        read_string (reader, &feature->name);
      if (g_str_equal (tag, "rank"))
        read_uint (reader, &feature->rank);
      if (GST_IS_ELEMENT_FACTORY (feature)) {
        GstElementFactory *factory = GST_ELEMENT_FACTORY (feature);

        if (g_str_equal (tag, "longname")) {
          int ret;

          ret = read_string (reader, &factory->details.longname);
          GST_DEBUG ("longname ret=%d, name=%s",
              ret, factory->details.longname);
        } else if (g_str_equal (tag, "class")) {
          read_string (reader, &factory->details.klass);
        } else if (g_str_equal (tag, "description")) {
          read_string (reader, &factory->details.description);
        } else if (g_str_equal (tag, "author")) {
          read_string (reader, &factory->details.author);
        } else if (g_str_equal (tag, "uri_type")) {
          gchar *s = NULL;

          if (read_string (reader, &s)) {
            if (g_ascii_strncasecmp (s, "sink", 4) == 0) {
              factory->uri_type = GST_URI_SINK;
            } else if (g_ascii_strncasecmp (s, "source", 5) == 0) {
              factory->uri_type = GST_URI_SRC;
            }
            g_free (s);
          }
        } else if (g_str_equal (tag, "uri_protocol")) {
          gchar *s = NULL;

          if (read_string (reader, &s))
            add_to_char_array (&factory->uri_protocols, s);
        } else if (g_str_equal (tag, "interface")) {
          gchar *s = NULL;

          if (read_string (reader, &s)) {
            __gst_element_factory_add_interface (factory, s);
            /* add_interface strdup's s */
            g_free (s);
          }
        } else if (g_str_equal (tag, "padtemplate")) {
          GstStaticPadTemplate *template = load_pad_template (reader);

          if (template) {
            GST_LOG ("adding template %s to factory %s",
                GST_STR_NULL (GST_PAD_TEMPLATE_NAME_TEMPLATE (template)),
                GST_PLUGIN_FEATURE_NAME (feature));
            __gst_element_factory_add_static_pad_template (factory, template);
          }
        }
      } else if (GST_IS_TYPE_FIND_FACTORY (feature)) {
        GstTypeFindFactory *factory = GST_TYPE_FIND_FACTORY (feature);

        if (g_str_equal (tag, "extension")) {
          gchar *s = NULL;

          if (read_string (reader, &s))
            add_to_char_array (&factory->extensions, s);
        } else if (g_str_equal (tag, "caps")) {
          gchar *s = NULL;

          if (read_string (reader, &s)) {
            factory->caps = gst_caps_from_string (s);
            g_free (s);
          }
        }
      } else if (GST_IS_INDEX_FACTORY (feature)) {
        GstIndexFactory *factory = GST_INDEX_FACTORY (feature);

        if (g_str_equal (tag, "longdesc"))
          read_string (reader, &factory->longdesc);
      }
    }
  }

  g_assert_not_reached ();
  return NULL;
}

static GstPlugin *
load_plugin (xmlTextReaderPtr reader, GList ** feature_list)
{
  int ret;
  GstPlugin *plugin;

  *feature_list = NULL;

  GST_DEBUG ("creating new plugin and parsing");

  plugin = g_object_new (GST_TYPE_PLUGIN, NULL);

  plugin->flags |= GST_PLUGIN_FLAG_CACHED;
  while ((ret = xmlTextReaderRead (reader)) == 1) {
    if (xmlTextReaderDepth (reader) == 1) {
      return plugin;
    }
    if (xmlTextReaderNodeType (reader) == XML_READER_TYPE_ELEMENT &&
        xmlTextReaderDepth (reader) == 2) {
      const gchar *tag = (gchar *) xmlTextReaderConstName (reader);

      if (g_str_equal (tag, "name")) {
        int ret;

        ret = read_string (reader, &plugin->desc.name);
        GST_DEBUG ("name ret=%d, name=%s", ret, plugin->desc.name);
        if (!ret)
          break;
      } else if (g_str_equal (tag, "description")) {
        if (!read_string (reader, &plugin->desc.description))
          break;
        GST_DEBUG ("description %s", plugin->desc.description);
      } else if (g_str_equal (tag, "filename")) {
        if (!read_string (reader, &plugin->filename))
          break;
        GST_DEBUG ("filename %s", plugin->filename);
        plugin->basename = g_path_get_basename (plugin->filename);
      } else if (g_str_equal (tag, "version")) {
        if (!read_string (reader, &plugin->desc.version))
          break;
        GST_DEBUG ("version %s", plugin->desc.version);
      } else if (g_str_equal (tag, "license")) {
        if (!read_string (reader, &plugin->desc.license))
          break;
        GST_DEBUG ("license %s", plugin->desc.license);
      } else if (g_str_equal (tag, "source")) {
        if (!read_string (reader, &plugin->desc.source))
          break;
        GST_DEBUG ("source %s", plugin->desc.source);
      } else if (g_str_equal (tag, "package")) {
        if (!read_string (reader, &plugin->desc.package))
          break;
        GST_DEBUG ("package %s", plugin->desc.package);
      } else if (g_str_equal (tag, "origin")) {
        if (!read_string (reader, &plugin->desc.origin)) {
          GST_DEBUG ("failed to read origin");
          break;
        }
      } else if (g_str_equal (tag, "m32p")) {
        char *s;

        if (!read_string (reader, &s)) {
          GST_DEBUG ("failed to read mtime");
          break;
        }
        plugin->file_mtime = strtol (s, NULL, 0);
        GST_DEBUG ("mtime %d", (int) plugin->file_mtime);
        g_free (s);
      } else if (g_str_equal (tag, "size")) {
        unsigned int x;

        if (read_uint (reader, &x)) {
          plugin->file_size = x;
          GST_DEBUG ("file_size %d", plugin->file_size);
        } else {
          GST_DEBUG ("failed to read size");
        }
      } else if (g_str_equal (tag, "feature")) {
        GstPluginFeature *feature = load_feature (reader);

        if (feature) {
          feature->plugin_name = g_strdup (plugin->desc.name);
          *feature_list = g_list_prepend (*feature_list, feature);
        }
      } else {
        GST_DEBUG ("unknown tag %s", tag);
      }
    }
  }
  gst_object_unref (plugin);

  GST_DEBUG ("problem reading plugin");

  return NULL;
}

/**
 * gst_registry_xml_read_cache:
 * @registry: a #GstRegistry
 * @location: a filename
 *
 * Read the contents of the XML cache file at location 
 * @location into @registry.
 *
 * Returns: TRUE on success.
 */
gboolean
gst_registry_xml_read_cache (GstRegistry * registry, const char *location)
{
  GTimer *timer;
  gdouble seconds;
  xmlTextReaderPtr reader;
  int ret;
  gboolean in_registry = FALSE;
  FILE *file;

  /* make sure these types exist */
  GST_TYPE_ELEMENT_FACTORY;
  GST_TYPE_TYPE_FIND_FACTORY;
  GST_TYPE_INDEX_FACTORY;

  timer = g_timer_new ();

  file = fopen (location, "r");
  if (file == NULL) {
    g_timer_destroy (timer);
    return FALSE;
  }

  reader = xmlReaderForFd (fileno (file), NULL, NULL, 0);
  if (!reader) {
    fclose (file);
    g_timer_destroy (timer);
    return FALSE;
  }

  while ((ret = xmlTextReaderRead (reader)) == 1) {
    if (xmlTextReaderDepth (reader) == 0) {
      in_registry = xmlTextReaderNodeType (reader) == XML_READER_TYPE_ELEMENT &&
          g_str_equal ("GST-PluginRegistry", xmlTextReaderConstName (reader));
    } else if (in_registry) {
      if (xmlTextReaderDepth (reader) == 1 &&
          xmlTextReaderNodeType (reader) == XML_READER_TYPE_ELEMENT) {
        const gchar *tag = (const gchar *) xmlTextReaderConstName (reader);

        if (g_str_equal (tag, "plugin")) {
          GList *feature_list;
          GstPlugin *plugin = load_plugin (reader, &feature_list);

          if (plugin) {
            GList *g;

            GST_DEBUG ("adding plugin %s", plugin->desc.name);
            gst_registry_add_plugin (registry, plugin);
            for (g = feature_list; g; g = g_list_next (g)) {
              gst_registry_add_feature (registry, GST_PLUGIN_FEATURE (g->data));
            }
            g_list_free (feature_list);
          }
        }
      }
    }
  }
  xmlFreeTextReader (reader);
  if (ret != 0) {
    GST_ERROR ("parsing registry cache: %s", location);
    fclose (file);
    g_timer_destroy (timer);
    return FALSE;
  }

  g_timer_stop (timer);
  seconds = g_timer_elapsed (timer, NULL);
  g_timer_destroy (timer);

  GST_INFO ("loaded %s in %f seconds", location, seconds);

  fclose (file);

  return TRUE;
}

/*
 * Save
 */
#define PUT_ESCAPED(prefix,tag,value)           		\
G_STMT_START{ 							\
  const gchar *toconv = value;					\
  if (toconv) {							\
    gchar *v = g_markup_escape_text (toconv, strlen (toconv));	\
    gst_registry_xml_save (registry, prefix "<%s>%s</%s>\n", tag, v, tag);			\
    g_free (v);							\
  }                                                             \
}G_STMT_END


static gboolean
gst_registry_xml_save_caps (GstRegistry * registry, const GstCaps * caps)
{
  /* we copy the caps here so we can simplify them before saving. This is a lot
   * faster when loading them later on */
  char *s;
  GstCaps *copy = gst_caps_copy (caps);

  gst_caps_do_simplify (copy);
  s = gst_caps_to_string (copy);
  gst_caps_unref (copy);

  PUT_ESCAPED ("  ", "caps", s);
  g_free (s);
  return TRUE;
}

static gboolean
gst_registry_xml_save_pad_template (GstRegistry * registry,
    GstStaticPadTemplate * template)
{
  gchar *presence;

  PUT_ESCAPED ("   ", "nametemplate", template->name_template);
  gst_registry_xml_save (registry, "   <direction>%s</direction>\n",
      (template->direction == GST_PAD_SINK ? "sink" : "src"));

  switch (template->presence) {
    case GST_PAD_ALWAYS:
      presence = "always";
      break;
    case GST_PAD_SOMETIMES:
      presence = "sometimes";
      break;
    case GST_PAD_REQUEST:
      presence = "request";
      break;
    default:
      presence = "unknown";
      break;
  }
  gst_registry_xml_save (registry, "   <presence>%s</presence>\n", presence);

  if (template->static_caps.string) {
    gst_registry_xml_save (registry, "   <caps>%s</caps>\n",
        template->static_caps.string);
  }
  return TRUE;
}

static gboolean
gst_registry_xml_save_feature (GstRegistry * registry,
    GstPluginFeature * feature)
{
  PUT_ESCAPED ("  ", "name", feature->name);

  if (feature->rank > 0) {
    gint rank = feature->rank;

    gst_registry_xml_save (registry, "  <rank>%d</rank>\n", rank);
  }

  if (GST_IS_ELEMENT_FACTORY (feature)) {
    GstElementFactory *factory = GST_ELEMENT_FACTORY (feature);
    GList *walk;

    PUT_ESCAPED ("  ", "longname", factory->details.longname);
    PUT_ESCAPED ("  ", "class", factory->details.klass);
    PUT_ESCAPED ("  ", "description", factory->details.description);
    PUT_ESCAPED ("  ", "author", factory->details.author);

    walk = factory->staticpadtemplates;

    while (walk) {
      GstStaticPadTemplate *template = walk->data;

      gst_registry_xml_save (registry, "  <padtemplate>\n");
      gst_registry_xml_save_pad_template (registry, template);
      gst_registry_xml_save (registry, "  </padtemplate>\n");

      walk = g_list_next (walk);
    }

    walk = factory->interfaces;
    while (walk) {
      PUT_ESCAPED ("  ", "interface", (gchar *) walk->data);
      walk = g_list_next (walk);
    }

    if (GST_URI_TYPE_IS_VALID (factory->uri_type)) {
      gchar **protocol;

      PUT_ESCAPED ("  ", "uri_type",
          factory->uri_type == GST_URI_SINK ? "sink" : "source");
      g_assert (factory->uri_protocols);
      protocol = factory->uri_protocols;
      while (*protocol) {
        PUT_ESCAPED ("  ", "uri_protocol", *protocol);
        protocol++;
      }
    }
  } else if (GST_IS_TYPE_FIND_FACTORY (feature)) {
    GstTypeFindFactory *factory = GST_TYPE_FIND_FACTORY (feature);
    gint i = 0;

    if (factory->caps) {
      gst_registry_xml_save_caps (registry, factory->caps);
    }
    if (factory->extensions) {
      while (factory->extensions[i]) {
        PUT_ESCAPED ("  ", "extension", factory->extensions[i]);
        i++;
      }
    }
  } else if (GST_IS_INDEX_FACTORY (feature)) {
    PUT_ESCAPED ("  ", "longdesc", GST_INDEX_FACTORY (feature)->longdesc);
  }
  return TRUE;
}

static gboolean
gst_registry_xml_save_plugin (GstRegistry * registry, GstPlugin * plugin)
{
  GList *list;
  GList *walk;
  char s[100];

  PUT_ESCAPED (" ", "name", plugin->desc.name);
  PUT_ESCAPED (" ", "description", plugin->desc.description);
  PUT_ESCAPED (" ", "filename", plugin->filename);
  sprintf (s, "%d", (int) plugin->file_size);
  PUT_ESCAPED (" ", "size", s);
  sprintf (s, "%d", (int) plugin->file_mtime);
  PUT_ESCAPED (" ", "m32p", s);
  PUT_ESCAPED (" ", "version", plugin->desc.version);
  PUT_ESCAPED (" ", "license", plugin->desc.license);
  PUT_ESCAPED (" ", "source", plugin->desc.source);
  PUT_ESCAPED (" ", "package", plugin->desc.package);
  PUT_ESCAPED (" ", "origin", plugin->desc.origin);

  list = gst_registry_get_feature_list_by_plugin (registry, plugin->desc.name);

  for (walk = list; walk; walk = g_list_next (walk)) {
    GstPluginFeature *feature = GST_PLUGIN_FEATURE (walk->data);

    gst_registry_xml_save (registry, " <feature typename=\"%s\">\n",
        g_type_name (G_OBJECT_TYPE (feature)));
    gst_registry_xml_save_feature (registry, feature);
    gst_registry_xml_save (registry, " </feature>\n");
  }

  gst_plugin_feature_list_free (list);

  return TRUE;
}

/**
 * gst_registry_xml_write_cache:
 * @registry: a #GstRegistry
 * @location: a filename
 *
 * Write @registry in an XML format at the location given by
 * @location. Directories are automatically created.
 *
 * Returns: TRUE on success.
 */
gboolean
gst_registry_xml_write_cache (GstRegistry * registry, const char *location)
{
  GList *walk;
  char *tmp_location;

  g_return_val_if_fail (GST_IS_REGISTRY (registry), FALSE);

  tmp_location = g_strconcat (location, ".tmp", NULL);
  registry->cache_file = fopen (tmp_location, "w");
  if (registry->cache_file == NULL) {
    char *dir;

    /* oops, I bet the directory doesn't exist */
    dir = g_path_get_dirname (location);
    g_mkdir_with_parents (dir, 0777);
    g_free (dir);

    registry->cache_file = fopen (tmp_location, "w");
  }
  if (registry->cache_file == NULL) {
    return FALSE;
  }

  gst_registry_xml_save (registry, "<?xml version=\"1.0\"?>\n");
  gst_registry_xml_save (registry, "<GST-PluginRegistry>\n");


  for (walk = g_list_last (registry->plugins); walk;
      walk = g_list_previous (walk)) {
    GstPlugin *plugin = GST_PLUGIN (walk->data);

    if (!plugin->filename)
      continue;

    if (plugin->flags & GST_PLUGIN_FLAG_CACHED) {
      int ret;
      struct stat statbuf;

      ret = g_stat (plugin->filename, &statbuf);
      if (ret < 0)
        continue;
      if (plugin->file_mtime != statbuf.st_mtime ||
          plugin->file_size != statbuf.st_size) {
        continue;
      }
    }

    gst_registry_xml_save (registry, "<plugin>\n");
    gst_registry_xml_save_plugin (registry, plugin);
    gst_registry_xml_save (registry, "</plugin>\n");
  }
  gst_registry_xml_save (registry, "</GST-PluginRegistry>\n");

  fclose (registry->cache_file);

  if (g_file_test (tmp_location, G_FILE_TEST_EXISTS)) {
#ifdef WIN32
    remove (location);
#endif
    rename (tmp_location, location);
  }
  g_free (tmp_location);

  return TRUE;
}
