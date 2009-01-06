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
gst_registry_save (GstRegistry * registry, gchar * format, ...)
{
  va_list var_args;
  gsize written, len;
  gboolean ret;
  char *str;

  va_start (var_args, format);
  str = g_strdup_vprintf (format, var_args);
  va_end (var_args);

  len = strlen (str);

  written = write (registry->cache_file, str, len);

  if (len == written)
    ret = TRUE;
  else {
    ret = FALSE;
    GST_ERROR ("Failed to write registry to temporary file: %s",
        g_strerror (errno));
  }

  g_free (str);

  return ret;
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
read_string (xmlTextReaderPtr reader, gchar ** write_to, gboolean allow_blank)
{
  int depth = xmlTextReaderDepth (reader);
  gboolean found = FALSE;

  while (xmlTextReaderRead (reader) == 1) {
    if (xmlTextReaderDepth (reader) == depth) {
      if (allow_blank && !found &&
          xmlTextReaderNodeType (reader) == XML_READER_TYPE_END_ELEMENT) {
        /* Allow blank strings */
        *write_to = g_strdup ("");
        found = TRUE;
      }
      return found;
    }
    if (xmlTextReaderNodeType (reader) == XML_READER_TYPE_TEXT) {
      xmlChar *value;

      if (found)
        return FALSE;

      value = xmlTextReaderValue (reader);
      *write_to = g_strdup ((gchar *) value);
      xmlFree (value);

      found = TRUE;
    }
  }
  return FALSE;
}

static gboolean
read_const_interned_string (xmlTextReaderPtr reader, const gchar ** write_to,
    gboolean allow_blank)
{
  gchar *s = NULL;

  if (!read_string (reader, &s, allow_blank))
    return FALSE;

  *write_to = g_intern_string (s);
  g_free (s);
  return TRUE;
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
  const gchar *name = NULL;
  gchar *caps_str = NULL;
  guint direction = 0, presence = 0;

  while ((ret = xmlTextReaderRead (reader)) == 1) {
    if (xmlTextReaderDepth (reader) == depth) {
      GstStaticPadTemplate *template;

      template = g_new0 (GstStaticPadTemplate, 1);
      template->name_template = name;   /* must be an interned string! */
      template->presence = presence;
      template->direction = direction;
      template->static_caps.string = caps_str;

      return template;
    }
    if (xmlTextReaderNodeType (reader) == XML_READER_TYPE_ELEMENT &&
        xmlTextReaderDepth (reader) == depth + 1) {
      const gchar *tag = (gchar *) xmlTextReaderConstName (reader);

      if (g_str_equal (tag, "nametemplate")) {
        read_const_interned_string (reader, &name, FALSE);
      } else if (g_str_equal (tag, "direction")) {
        read_enum (reader, GST_TYPE_PAD_DIRECTION, &direction);
      } else if (g_str_equal (tag, "presence")) {
        read_enum (reader, GST_TYPE_PAD_PRESENCE, &presence);
      } else if (!strncmp (tag, "caps", 4)) {
        read_string (reader, &caps_str, FALSE);
      }
    }
  }
  g_free (caps_str);

  return NULL;
}

static GstPluginFeature *
load_feature (xmlTextReaderPtr reader)
{
  int ret;
  int depth;
  xmlChar *feature_name;
  GstPluginFeature *feature;
  GType type;

  depth = xmlTextReaderDepth (reader);
  feature_name = xmlTextReaderGetAttribute (reader, BAD_CAST "typename");

  GST_LOG ("loading feature '%s'", GST_STR_NULL ((const char *) feature_name));

  if (!feature_name)
    return NULL;

  type = g_type_from_name ((const char *) feature_name);
  xmlFree (feature_name);
  feature_name = NULL;

  if (!type) {
    return NULL;
  }
  feature = g_object_new (type, NULL);
  if (!feature) {
    return NULL;
  }
  if (!GST_IS_PLUGIN_FEATURE (feature)) {
    /* don't really know what it is */
    if (GST_IS_OBJECT (feature))
      gst_object_unref (feature);
    else
      g_object_unref (feature);
    return NULL;
  }
  while ((ret = xmlTextReaderRead (reader)) == 1) {
    if (xmlTextReaderDepth (reader) == depth) {
      GST_LOG ("loaded feature %p with name %s", feature, feature->name);
      return feature;
    }
    if (xmlTextReaderNodeType (reader) == XML_READER_TYPE_ELEMENT &&
        xmlTextReaderDepth (reader) == depth + 1) {
      const gchar *tag = (gchar *) xmlTextReaderConstName (reader);

      if (g_str_equal (tag, "name"))
        read_string (reader, &feature->name, FALSE);
      else if (g_str_equal (tag, "rank"))
        read_uint (reader, &feature->rank);

      if (GST_IS_ELEMENT_FACTORY (feature)) {
        GstElementFactory *factory = GST_ELEMENT_FACTORY_CAST (feature);

        if (g_str_equal (tag, "longname")) {
          int ret;

          ret = read_string (reader, &factory->details.longname, TRUE);
          GST_LOG ("longname ret=%d, name=%s", ret, factory->details.longname);
        } else if (g_str_equal (tag, "class")) {
          read_string (reader, &factory->details.klass, TRUE);
        } else if (g_str_equal (tag, "description")) {
          read_string (reader, &factory->details.description, TRUE);
        } else if (g_str_equal (tag, "author")) {
          read_string (reader, &factory->details.author, TRUE);
        } else if (g_str_equal (tag, "uri_type")) {
          gchar *s = NULL;

          if (read_string (reader, &s, FALSE)) {
            if (g_ascii_strncasecmp (s, "sink", 4) == 0) {
              factory->uri_type = GST_URI_SINK;
            } else if (g_ascii_strncasecmp (s, "source", 5) == 0) {
              factory->uri_type = GST_URI_SRC;
            }
            g_free (s);
          }
        } else if (g_str_equal (tag, "uri_protocol")) {
          gchar *s = NULL;

          if (read_string (reader, &s, FALSE))
            add_to_char_array (&factory->uri_protocols, s);
        } else if (g_str_equal (tag, "interface")) {
          gchar *s = NULL;

          if (read_string (reader, &s, FALSE)) {
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

          if (read_string (reader, &s, TRUE))
            add_to_char_array (&factory->extensions, s);
        } else if (g_str_equal (tag, "caps")) {
          gchar *s = NULL;

          if (read_string (reader, &s, FALSE)) {
            factory->caps = gst_caps_from_string (s);
            g_free (s);
          }
        }
      } else if (GST_IS_INDEX_FACTORY (feature)) {
        GstIndexFactory *factory = GST_INDEX_FACTORY (feature);

        if (g_str_equal (tag, "longdesc"))
          read_string (reader, &factory->longdesc, TRUE);
      }
    }
  }

  GST_WARNING ("Error reading feature from registry: registry corrupt?");
  return NULL;
}

static GstPlugin *
load_plugin (xmlTextReaderPtr reader, GList ** feature_list)
{
  int ret;
  GstPlugin *plugin;

  *feature_list = NULL;

  GST_LOG ("creating new plugin and parsing");

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

        ret = read_const_interned_string (reader, &plugin->desc.name, FALSE);
        GST_LOG ("name ret=%d, name=%s", ret, plugin->desc.name);
        if (!ret)
          break;
      } else if (g_str_equal (tag, "description")) {
        if (!read_string (reader, &plugin->desc.description, TRUE)) {
          GST_WARNING ("description field was invalid in registry");
          break;
        }
        GST_LOG ("description %s", plugin->desc.description);
      } else if (g_str_equal (tag, "filename")) {
        if (!read_string (reader, &plugin->filename, FALSE)) {
          GST_WARNING ("filename field was invalid in registry");
          break;
        }
        GST_LOG ("filename %s", plugin->filename);
        plugin->basename = g_path_get_basename (plugin->filename);
      } else if (g_str_equal (tag, "version")) {
        if (!read_const_interned_string (reader, &plugin->desc.version, TRUE)) {
          GST_WARNING ("version field was invalid in registry");
          break;
        }
        GST_LOG ("version %s", plugin->desc.version);
      } else if (g_str_equal (tag, "license")) {
        if (!read_const_interned_string (reader, &plugin->desc.license, TRUE)) {
          GST_WARNING ("license field was invalid in registry");
          break;
        }
        GST_LOG ("license %s", plugin->desc.license);
      } else if (g_str_equal (tag, "source")) {
        if (!read_const_interned_string (reader, &plugin->desc.source, TRUE)) {
          GST_WARNING ("source field was invalid in registry");
          break;
        }
        GST_LOG ("source %s", plugin->desc.source);
      } else if (g_str_equal (tag, "package")) {
        if (!read_const_interned_string (reader, &plugin->desc.package, TRUE)) {
          GST_WARNING ("package field was invalid in registry");
          break;
        }
        GST_LOG ("package %s", plugin->desc.package);
      } else if (g_str_equal (tag, "origin")) {
        if (!read_const_interned_string (reader, &plugin->desc.origin, TRUE)) {
          GST_WARNING ("failed to read origin");
          break;
        }
      } else if (g_str_equal (tag, "m32p")) {
        char *s;

        if (!read_string (reader, &s, FALSE)) {
          GST_WARNING ("failed to read mtime");
          break;
        }
        plugin->file_mtime = strtol (s, NULL, 0);
        GST_LOG ("mtime %d", (int) plugin->file_mtime);
        g_free (s);
      } else if (g_str_equal (tag, "size")) {
        unsigned int x;

        if (read_uint (reader, &x)) {
          plugin->file_size = x;
          GST_LOG ("file_size %" G_GINT64_FORMAT, (gint64) plugin->file_size);
        } else {
          GST_WARNING ("failed to read size");
        }
      } else if (g_str_equal (tag, "feature")) {
        GstPluginFeature *feature = load_feature (reader);

        if (feature) {
          feature->plugin_name = plugin->desc.name;     /* interned string */
          *feature_list = g_list_prepend (*feature_list, feature);
        }
      } else {
        GST_WARNING ("unknown tag %s", tag);
      }
    }
  }
  gst_object_unref (plugin);

  GST_WARNING ("problem reading plugin");

  return NULL;
}

/**
 * gst_registry_xml_read_cache:
 * @registry: a #GstRegistry
 * @location: a filename
 *
 * Read the contents of the XML cache file at @location into @registry.
 *
 * Returns: %TRUE on success.
 */
gboolean
gst_registry_xml_read_cache (GstRegistry * registry, const char *location)
{
  GMappedFile *mapped = NULL;
  GTimer *timer;
  gdouble seconds;
  xmlTextReaderPtr reader = NULL;
  int ret;
  gboolean in_registry = FALSE;
  FILE *file = NULL;

  /* make sure these types exist */
  GST_TYPE_ELEMENT_FACTORY;
  GST_TYPE_TYPE_FIND_FACTORY;
  GST_TYPE_INDEX_FACTORY;

  timer = g_timer_new ();

  mapped = g_mapped_file_new (location, FALSE, NULL);
  if (mapped) {
    reader = xmlReaderForMemory (g_mapped_file_get_contents (mapped),
        g_mapped_file_get_length (mapped), NULL, NULL, 0);
    if (reader == NULL) {
      g_mapped_file_free (mapped);
      mapped = NULL;
    }
  }

  if (reader == NULL) {
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

            gst_registry_add_plugin (registry, plugin);
            for (g = feature_list; g; g = g_list_next (g)) {
              gst_registry_add_feature (registry,
                  GST_PLUGIN_FEATURE_CAST (g->data));
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
    if (mapped)
      g_mapped_file_free (mapped);
    if (file)
      fclose (file);
    g_timer_destroy (timer);
    return FALSE;
  }

  g_timer_stop (timer);
  seconds = g_timer_elapsed (timer, NULL);
  g_timer_destroy (timer);

  GST_INFO ("loaded %s in %lf seconds", location, seconds);

  if (mapped)
    g_mapped_file_free (mapped);

  if (file)
    fclose (file);

  return TRUE;
}

/*
 * Save
 */
static gboolean
gst_registry_save_escaped (GstRegistry * registry, const char *prefix,
    const char *tag, const char *value)
{
  gboolean ret = TRUE;

  if (value) {
    gchar *v;

    if (g_utf8_validate (value, -1, NULL)) {
      v = g_markup_escape_text (value, -1);
    } else {
      g_warning ("Invalid UTF-8 while saving registry tag '%s'", tag);
      v = g_strdup ("[ERROR: invalid UTF-8]");
    }

    ret = gst_registry_save (registry, "%s<%s>%s</%s>\n", prefix, tag, v, tag);
    g_free (v);
  }

  return ret;
}


static gboolean
gst_registry_xml_save_caps (GstRegistry * registry, const GstCaps * caps)
{
  /* we copy the caps here so we can simplify them before saving. This is a lot
   * faster when loading them later on */
  char *s;
  GstCaps *copy = gst_caps_copy (caps);
  gboolean ret;

  gst_caps_do_simplify (copy);
  s = gst_caps_to_string (copy);
  gst_caps_unref (copy);

  ret = gst_registry_save_escaped (registry, "  ", "caps", s);
  g_free (s);
  return ret;
}

static gboolean
gst_registry_xml_save_pad_template (GstRegistry * registry,
    GstStaticPadTemplate * template)
{
  gchar *presence;

  if (!gst_registry_save_escaped (registry, "   ", "nametemplate",
          template->name_template))
    return FALSE;

  if (!gst_registry_save (registry,
          "   <direction>%s</direction>\n",
          (template->direction == GST_PAD_SINK ? "sink" : "src")))
    return FALSE;

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
  if (!gst_registry_save (registry, "   <presence>%s</presence>\n", presence))
    return FALSE;

  if (template->static_caps.string) {
    if (!gst_registry_save (registry, "   <caps>%s</caps>\n",
            template->static_caps.string))
      return FALSE;
  }
  return TRUE;
}

static gboolean
gst_registry_xml_save_feature (GstRegistry * registry,
    GstPluginFeature * feature)
{
  if (!gst_registry_save_escaped (registry, "  ", "name", feature->name))
    return FALSE;

  if (feature->rank > 0) {
    gint rank = feature->rank;

    if (!gst_registry_save (registry, "  <rank>%d</rank>\n", rank))
      return FALSE;
  }

  if (GST_IS_ELEMENT_FACTORY (feature)) {
    GstElementFactory *factory = GST_ELEMENT_FACTORY (feature);
    GList *walk;

    if (!gst_registry_save_escaped (registry, "  ", "longname",
            factory->details.longname))
      return FALSE;
    if (!gst_registry_save_escaped (registry, "  ", "class",
            factory->details.klass))
      return FALSE;
    if (!gst_registry_save_escaped (registry, "  ", "description",
            factory->details.description))
      return FALSE;
    if (!gst_registry_save_escaped (registry, "  ", "author",
            factory->details.author))
      return FALSE;

    walk = factory->staticpadtemplates;

    while (walk) {
      GstStaticPadTemplate *template = walk->data;

      if (!gst_registry_save (registry, "  <padtemplate>\n"))
        return FALSE;
      if (!gst_registry_xml_save_pad_template (registry, template))
        return FALSE;
      if (!gst_registry_save (registry, "  </padtemplate>\n"))
        return FALSE;

      walk = g_list_next (walk);
    }

    walk = factory->interfaces;
    while (walk) {
      if (!gst_registry_save_escaped (registry, "  ", "interface",
              (gchar *) walk->data))
        return FALSE;
      walk = g_list_next (walk);
    }

    if (GST_URI_TYPE_IS_VALID (factory->uri_type)) {
      if (!gst_registry_save_escaped (registry, "  ", "uri_type",
              factory->uri_type == GST_URI_SINK ? "sink" : "source"))
        return FALSE;
      if (factory->uri_protocols) {
        gchar **protocol;

        protocol = factory->uri_protocols;
        while (*protocol) {
          if (!gst_registry_save_escaped (registry, "  ", "uri_protocol",
                  *protocol))
            return FALSE;
          protocol++;
        }
      } else {
        g_warning ("GStreamer feature '%s' is URI handler but does not provide"
            " any protocols it can handle", feature->name);
      }
    }
  } else if (GST_IS_TYPE_FIND_FACTORY (feature)) {
    GstTypeFindFactory *factory = GST_TYPE_FIND_FACTORY (feature);
    gint i = 0;

    if (factory->caps) {
      if (!gst_registry_xml_save_caps (registry, factory->caps))
        return FALSE;
    }
    if (factory->extensions) {
      while (factory->extensions[i]) {
        if (!gst_registry_save_escaped (registry, "  ", "extension",
                factory->extensions[i]))
          return FALSE;
        i++;
      }
    }
  } else if (GST_IS_INDEX_FACTORY (feature)) {
    if (!gst_registry_save_escaped (registry, "  ", "longdesc",
            GST_INDEX_FACTORY (feature)->longdesc))
      return FALSE;
  }
  return TRUE;
}

static gboolean
gst_registry_xml_save_plugin (GstRegistry * registry, GstPlugin * plugin)
{
  GList *list;
  GList *walk;
  char s[100];

  if (plugin->priv->deps != NULL) {
    GST_WARNING ("XML registry does not support external plugin dependencies");
  }

  if (!gst_registry_save_escaped (registry, " ", "name", plugin->desc.name))
    return FALSE;
  if (!gst_registry_save_escaped (registry, " ", "description",
          plugin->desc.description))
    return FALSE;
  if (!gst_registry_save_escaped (registry, " ", "filename", plugin->filename))
    return FALSE;

  sprintf (s, "%d", (int) plugin->file_size);
  if (!gst_registry_save_escaped (registry, " ", "size", s))
    return FALSE;

  sprintf (s, "%d", (int) plugin->file_mtime);
  if (!gst_registry_save_escaped (registry, " ", "m32p", s))
    return FALSE;

  if (!gst_registry_save_escaped (registry, " ", "version",
          plugin->desc.version))
    return FALSE;
  if (!gst_registry_save_escaped (registry, " ", "license",
          plugin->desc.license))
    return FALSE;
  if (!gst_registry_save_escaped (registry, " ", "source", plugin->desc.source))
    return FALSE;
  if (!gst_registry_save_escaped (registry, " ", "package",
          plugin->desc.package))
    return FALSE;
  if (!gst_registry_save_escaped (registry, " ", "origin", plugin->desc.origin))
    return FALSE;

  list = gst_registry_get_feature_list_by_plugin (registry, plugin->desc.name);

  for (walk = list; walk; walk = g_list_next (walk)) {
    GstPluginFeature *feature = GST_PLUGIN_FEATURE (walk->data);

    if (!gst_registry_save (registry,
            " <feature typename=\"%s\">\n",
            g_type_name (G_OBJECT_TYPE (feature))))
      goto fail;
    if (!gst_registry_xml_save_feature (registry, feature))
      goto fail;
    if (!gst_registry_save (registry, " </feature>\n"))
      goto fail;
  }

  gst_plugin_feature_list_free (list);
  return TRUE;

fail:
  gst_plugin_feature_list_free (list);
  return FALSE;

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

  tmp_location = g_strconcat (location, ".tmpXXXXXX", NULL);
  registry->cache_file = g_mkstemp (tmp_location);
  if (registry->cache_file == -1) {
    char *dir;

    /* oops, I bet the directory doesn't exist */
    dir = g_path_get_dirname (location);
    g_mkdir_with_parents (dir, 0777);
    g_free (dir);

    /* the previous g_mkstemp call overwrote the XXXXXX placeholder ... */
    g_free (tmp_location);
    tmp_location = g_strconcat (location, ".tmpXXXXXX", NULL);
    registry->cache_file = g_mkstemp (tmp_location);

    if (registry->cache_file == -1) {
      GST_DEBUG ("g_mkstemp() failed: %s", g_strerror (errno));
      g_free (tmp_location);
      return FALSE;
    }
  }

  if (!gst_registry_save (registry, "<?xml version=\"1.0\"?>\n"))
    goto fail;
  if (!gst_registry_save (registry, "<GST-PluginRegistry>\n"))
    goto fail;


  for (walk = g_list_last (registry->plugins); walk;
      walk = g_list_previous (walk)) {
    GstPlugin *plugin = GST_PLUGIN_CAST (walk->data);

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

    if (!gst_registry_save (registry, "<plugin>\n"))
      goto fail;
    if (!gst_registry_xml_save_plugin (registry, plugin))
      goto fail;
    if (!gst_registry_save (registry, "</plugin>\n"))
      goto fail;
  }
  if (!gst_registry_save (registry, "</GST-PluginRegistry>\n"))
    goto fail;

  /* check return value of close(), write errors may only get reported here */
  if (close (registry->cache_file) < 0)
    goto close_failed;

  if (g_file_test (tmp_location, G_FILE_TEST_EXISTS)) {
#ifdef WIN32
    g_unlink (location);
#endif
    if (g_rename (tmp_location, location) < 0)
      goto rename_failed;
  } else {
    /* FIXME: shouldn't we return FALSE here? */
  }

  g_free (tmp_location);
  GST_INFO ("Wrote XML registry cache");
  return TRUE;

/* ERRORS */
fail:
  {
    (void) close (registry->cache_file);
    /* fall through */
  }
fail_after_close:
  {
    g_unlink (tmp_location);
    g_free (tmp_location);
    return FALSE;
  }
close_failed:
  {
    GST_ERROR ("close() failed: %s", g_strerror (errno));
    goto fail_after_close;
  }
rename_failed:
  {
    GST_ERROR ("g_rename() failed: %s", g_strerror (errno));
    goto fail_after_close;
  }
}
