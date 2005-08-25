/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstxml_registry.c: GstXMLRegistry object, support routines
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
#ifdef _MSC_VER
#include <sys/utime.h>
#include <io.h>
#ifndef F_OK
#define F_OK 0
#define W_OK 2
#define R_OK 4
#endif
#ifndef S_ISREG
#define S_ISREG(mode) ((mode)&_S_IFREG)
#endif
#ifndef S_ISDIR
#define S_ISDIR(mode) ((mode)&_S_IFDIR)
#endif
#else /* _MSC_VER */
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <utime.h>
#endif

#include <gst/gst_private.h>
#include <gst/gstelement.h>
#include <gst/gsttypefind.h>
#include <gst/gsturi.h>
#include <gst/gstinfo.h>
#include <gst/gstenumtypes.h>

#include "gstlibxmlregistry.h"
#include <libxml/xmlreader.h>

#define BLOCK_SIZE 1024*10

#define CLASS(registry)  GST_XML_REGISTRY_CLASS (G_OBJECT_GET_CLASS (registry))


enum
{
  PROP_0,
  PROP_LOCATION
};


static void gst_xml_registry_class_init (GstXMLRegistryClass * klass);
static void gst_xml_registry_init (GstXMLRegistry * registry);

static void gst_xml_registry_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_xml_registry_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_xml_registry_load (GstRegistry * registry);
static gboolean gst_xml_registry_save (GstRegistry * registry);
static gboolean gst_xml_registry_rebuild (GstRegistry * registry);

static void gst_xml_registry_get_perms_func (GstXMLRegistry * registry);
static gboolean gst_xml_registry_open_func (GstXMLRegistry * registry,
    GstXMLRegistryMode mode);
static gboolean gst_xml_registry_load_func (GstXMLRegistry * registry,
    gchar * data, gssize * size);
static gboolean gst_xml_registry_save_func (GstXMLRegistry * registry,
    gchar * format, ...);
static gboolean gst_xml_registry_close_func (GstXMLRegistry * registry);

static GstRegistryReturn gst_xml_registry_load_plugin (GstRegistry * registry,
    GstPlugin * plugin);

static GstRegistryClass *parent_class = NULL;

/* static guint gst_xml_registry_signals[LAST_SIGNAL] = { 0 }; */


GType
gst_xml_registry_get_type (void)
{
  static GType xml_registry_type = 0;

  if (!xml_registry_type) {
    static const GTypeInfo xml_registry_info = {
      sizeof (GstXMLRegistryClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_xml_registry_class_init,
      NULL,
      NULL,
      sizeof (GstXMLRegistry),
      0,
      (GInstanceInitFunc) gst_xml_registry_init,
      NULL
    };

    xml_registry_type = g_type_register_static (GST_TYPE_REGISTRY,
        "GstXMLRegistry", &xml_registry_info, 0);
  }
  return xml_registry_type;
}

static void
gst_xml_registry_class_init (GstXMLRegistryClass * klass)
{
  GObjectClass *gobject_class;
  GstRegistryClass *gstregistry_class;
  GstXMLRegistryClass *gstxmlregistry_class;

  gobject_class = (GObjectClass *) klass;
  gstregistry_class = (GstRegistryClass *) klass;
  gstxmlregistry_class = (GstXMLRegistryClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_REGISTRY);

  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_xml_registry_get_property);
  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_xml_registry_set_property);

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_LOCATION,
      g_param_spec_string ("location", "Location",
          "Location of the registry file", NULL, G_PARAM_READWRITE));

  gstregistry_class->load = GST_DEBUG_FUNCPTR (gst_xml_registry_load);
  gstregistry_class->save = GST_DEBUG_FUNCPTR (gst_xml_registry_save);
  gstregistry_class->rebuild = GST_DEBUG_FUNCPTR (gst_xml_registry_rebuild);

  gstregistry_class->load_plugin =
      GST_DEBUG_FUNCPTR (gst_xml_registry_load_plugin);

  gstxmlregistry_class->get_perms_func =
      GST_DEBUG_FUNCPTR (gst_xml_registry_get_perms_func);
  gstxmlregistry_class->open_func =
      GST_DEBUG_FUNCPTR (gst_xml_registry_open_func);
  gstxmlregistry_class->load_func =
      GST_DEBUG_FUNCPTR (gst_xml_registry_load_func);
  gstxmlregistry_class->save_func =
      GST_DEBUG_FUNCPTR (gst_xml_registry_save_func);
  gstxmlregistry_class->close_func =
      GST_DEBUG_FUNCPTR (gst_xml_registry_close_func);
}

static void
gst_xml_registry_init (GstXMLRegistry * registry)
{
  registry->location = NULL;
}

/**
 * gst_xml_registry_new:
 * @name: the name of the registry
 * @location: the location of the registry file
 *
 * Create a new xml registry with the given name and location.
 *
 * Returns: a new GstXMLRegistry with the given name an location.
 */
GstRegistry *
gst_xml_registry_new (const gchar * name, const gchar * location)
{
  GstXMLRegistry *xmlregistry;

  xmlregistry = GST_XML_REGISTRY (g_object_new (GST_TYPE_XML_REGISTRY, NULL));

  g_object_set (G_OBJECT (xmlregistry), "location", location, NULL);

  GST_REGISTRY (xmlregistry)->name = g_strdup (name);

  return GST_REGISTRY (xmlregistry);
}

static void
gst_xml_registry_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstXMLRegistry *registry;

  registry = GST_XML_REGISTRY (object);

  switch (prop_id) {
    case PROP_LOCATION:
      if (registry->open) {
        CLASS (object)->close_func (registry);
        g_return_if_fail (registry->open == FALSE);
      }

      if (registry->location)
        g_free (registry->location);

      registry->location = g_strdup (g_value_get_string (value));
      GST_REGISTRY (registry)->flags = 0x0;

      if (CLASS (object)->get_perms_func)
        CLASS (object)->get_perms_func (registry);

      /* FIXME? (can't enable, because it's called before initialization is done) */
      /* gst_xml_registry_load (GST_REGISTRY (registry)); */
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_xml_registry_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstXMLRegistry *registry;

  registry = GST_XML_REGISTRY (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, g_strdup (registry->location));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* this function returns the biggest of the path's mtime and ctime
 * mtime is updated through an actual write (data)
 * ctime is updated through changing inode information
 * so this function returns the last time *anything* changed to this path
 * it also sets the given boolean to TRUE if the given path is a directory
 */
static time_t
get_time (const char *path, gboolean * is_dir)
{
  struct stat statbuf;

  if (stat (path, &statbuf)) {
    *is_dir = FALSE;
    return 0;
  }

  if (is_dir)
    *is_dir = S_ISDIR (statbuf.st_mode);

  if (statbuf.st_mtime > statbuf.st_ctime)
    return statbuf.st_mtime;
  return statbuf.st_ctime;
}

#if defined(_MSC_VER) || defined(__MINGW32__)
#define xmkdir(dirname) _mkdir (dirname)
#else
#define xmkdir(dirname) mkdir (dirname, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)
#endif

static gboolean
make_dir (gchar * filename)
{
  struct stat dirstat;
  gchar *dirname;

  if (strrchr (filename, '/') == NULL)
    return FALSE;

  dirname = g_strndup (filename, strrchr (filename, '/') - filename);

  if (stat (dirname, &dirstat) == -1 && errno == ENOENT) {
    if (xmkdir (dirname) != 0) {
      if (make_dir (dirname) != TRUE) {
        g_free (dirname);
        return FALSE;
      } else {
        if (xmkdir (dirname) != 0) {
          return FALSE;
        }
      }
    }
  }

  g_free (dirname);
  return TRUE;
}

static void
gst_xml_registry_get_perms_func (GstXMLRegistry * registry)
{
  gchar *dirname;

  /* if the dir does not exist, make it. if that can't be done, flags = 0x0.
     if the file can be appended to, it's writable. if it can then be read,
     it's readable. 
     After that check if it exists. */

  if (make_dir (registry->location) != TRUE) {
    /* we can't do anything with it, leave flags as 0x0 */
    return;
  }

  dirname = g_path_get_dirname (registry->location);

  if (g_file_test (registry->location, G_FILE_TEST_EXISTS)) {
    GST_REGISTRY (registry)->flags |= GST_REGISTRY_EXISTS;
  }

  if (!access (dirname, W_OK)) {
    GST_REGISTRY (registry)->flags |= GST_REGISTRY_WRITABLE;
  }

  if (!access (dirname, R_OK)) {
    GST_REGISTRY (registry)->flags |= GST_REGISTRY_READABLE;
  }

  g_free (dirname);
}

/* return TRUE iff regtime is more recent than the times of all the .so files
 * in the plugin dirs; ie return TRUE if this path does not need to trigger
 * a rebuild of registry
 *
 * - if it's a directory, recurse on subdirs
 * - if it's a file
 *   - if entry is not newer, return TRUE.
 *   - if it's newer
 *     - and it's a plugin, return FALSE
 *     - otherwise return TRUE
 */
static gboolean
plugin_times_older_than_recurse (gchar * path, time_t regtime)
{
  DIR *dir;
  struct dirent *dirent;
  gboolean is_dir;
  gchar *new_path;

  time_t pathtime = get_time (path, &is_dir);

  if (is_dir) {
    dir = opendir (path);
    if (dir) {
      while ((dirent = readdir (dir))) {
        /* don't want to recurse in place or backwards */
        if (strcmp (dirent->d_name, ".") && strcmp (dirent->d_name, "..")) {
          new_path = g_build_filename (path, dirent->d_name, NULL);
          if (!plugin_times_older_than_recurse (new_path, regtime)) {
            GST_CAT_INFO (GST_CAT_PLUGIN_LOADING,
                "path %s is more recent than registry time of %ld",
                new_path, (long) regtime);
            g_free (new_path);
            closedir (dir);
            return FALSE;
          }
          g_free (new_path);
        }
      }
      closedir (dir);
    }
    return TRUE;
  }

  /* it's a file */
  if (pathtime <= regtime) {
    return TRUE;
  }

  /* it's a file, and it's more recent */
  if (g_str_has_suffix (path, ".so") || g_str_has_suffix (path, ".dll")) {
    if (!gst_plugin_check_file (path, NULL))
      return TRUE;

    /* it's a newer GStreamer plugin */
    GST_CAT_INFO (GST_CAT_PLUGIN_LOADING,
        "%s looks like a plugin and is more recent than registry time of %ld",
        path, (long) regtime);
    return FALSE;
  }
  return TRUE;
}

/* return TRUE iff regtime is more recent than the times of all the .so files
 * in the plugin dirs; ie return TRUE if registry is up to date.
 */
static gboolean
plugin_times_older_than (GList * paths, time_t regtime)
{
  while (paths) {
    GST_CAT_LOG (GST_CAT_PLUGIN_LOADING,
        "comparing plugin times from %s with %ld",
        (gchar *) paths->data, (long) regtime);
    if (!plugin_times_older_than_recurse (paths->data, regtime))
      return FALSE;
    paths = g_list_next (paths);
  }
  GST_CAT_LOG (GST_CAT_PLUGIN_LOADING,
      "everything's fine, no registry rebuild needed.");
  return TRUE;
}

static gboolean
gst_xml_registry_open_func (GstXMLRegistry * registry, GstXMLRegistryMode mode)
{
  GstRegistry *gst_registry;
  GList *paths;

  gst_registry = GST_REGISTRY (registry);
  paths = gst_registry->paths;
  GST_CAT_DEBUG (GST_CAT_GST_INIT, "opening registry %s", registry->location);

  g_return_val_if_fail (registry->open == FALSE, FALSE);

  /* if it doesn't exist, first try to build it, and check if it worked
   * if it's not readable, return false
   * if it's out of date, rebuild it */
  if (mode == GST_XML_REGISTRY_READ) {
    if (!(gst_registry->flags & GST_REGISTRY_EXISTS)) {
      /* if it's not writable, then don't bother */
      if (!(gst_registry->flags & GST_REGISTRY_WRITABLE)) {
        GST_CAT_INFO (GST_CAT_GST_INIT, "Registry isn't writable");
        return FALSE;
      }
      GST_CAT_INFO (GST_CAT_GST_INIT,
          "Registry doesn't exist, trying to build...");
      gst_registry_rebuild (gst_registry);
      gst_registry_save (gst_registry);
      /* FIXME: verify that the flags actually get updated ! */
      if (!(gst_registry->flags & GST_REGISTRY_EXISTS)) {
        return FALSE;
      }
    }
    /* at this point we know it exists */
    g_return_val_if_fail (gst_registry->flags & GST_REGISTRY_READABLE, FALSE);

    if (!plugin_times_older_than (paths, get_time (registry->location, NULL))) {
      if (gst_registry->flags & GST_REGISTRY_WRITABLE) {
        GST_CAT_INFO (GST_CAT_GST_INIT, "Registry out of date, rebuilding...");

        gst_registry_rebuild (gst_registry);

        gst_registry_save (gst_registry);

        if (!plugin_times_older_than (paths, get_time (registry->location,
                    NULL))) {
          GST_CAT_INFO (GST_CAT_GST_INIT,
              "Registry still out of date, something is wrong...");
          return FALSE;
        }
      } else {
        GST_CAT_INFO (GST_CAT_GST_INIT,
            "Can't write to this registry and it's out of date, ignoring it");
        return FALSE;
      }
    }

    GST_CAT_DEBUG (GST_CAT_GST_INIT, "opening registry %s for reading",
        registry->location);
    registry->regfile = fopen (registry->location, "r");
  } else if (mode == GST_XML_REGISTRY_WRITE) {
    char *tmploc;
    int fd;

    g_return_val_if_fail (gst_registry->flags & GST_REGISTRY_WRITABLE, FALSE);

    tmploc = g_strconcat (registry->location, ".tmp", NULL);

    GST_CAT_DEBUG (GST_CAT_GST_INIT, "opening registry %s for writing", tmploc);

    if ((fd = open (tmploc, O_WRONLY | O_CREAT, 0644)) < 0) {
      g_free (tmploc);
      return FALSE;
    }
    g_free (tmploc);

    registry->regfile = fdopen (fd, "w");
  }

  if (!registry->regfile)
    return FALSE;

  registry->open = TRUE;

  return TRUE;
}

static gboolean
gst_xml_registry_load_func (GstXMLRegistry * registry, gchar * data,
    gssize * size)
{
  *size = fread (data, 1, *size, registry->regfile);

  return TRUE;
}

static gboolean
gst_xml_registry_save_func (GstXMLRegistry * registry, gchar * format, ...)
{
  va_list var_args;

  va_start (var_args, format);

  vfprintf (registry->regfile, format, var_args);

  va_end (var_args);

  return TRUE;
}

static gboolean
gst_xml_registry_close_func (GstXMLRegistry * registry)
{
  char *tmploc;

  GST_CAT_DEBUG (GST_CAT_GST_INIT, "closing registry %s", registry->location);
  fclose (registry->regfile);

  /* If we opened for writing, rename our temporary file. */
  tmploc = g_strconcat (registry->location, ".tmp", NULL);
  if (g_file_test (tmploc, G_FILE_TEST_EXISTS)) {
#ifdef WIN32
    remove (registry->location);
#endif
    rename (tmploc, registry->location);
  }
  g_free (tmploc);

  registry->open = FALSE;

  return TRUE;
}

/* takes ownership of given value */
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

/* read a string and store the result in *write_to.
 * return whether or not *write_to was set to a newly allocated string
 * FIXME: return values aren't actually checked, and in those failure cases
 * (that currently aren't triggered) cleanup is not done correctly
 */
static gboolean
read_string (xmlTextReaderPtr reader, gchar ** write_to)
{
  int depth = xmlTextReaderDepth (reader);
  gboolean found = FALSE;

  if (*write_to)
    return FALSE;
  while (xmlTextReaderRead (reader) == 1) {
    if (xmlTextReaderDepth (reader) == depth)
      return found;
    if (xmlTextReaderNodeType (reader) == XML_READER_TYPE_TEXT) {
      if (found) {
        return FALSE;
      }
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

  if (*write_to)
    return FALSE;
  while (xmlTextReaderRead (reader) == 1) {
    if (xmlTextReaderDepth (reader) == depth)
      return found;
    if (xmlTextReaderNodeType (reader) == XML_READER_TYPE_TEXT) {
      gchar *ret;

      if (found)
        return FALSE;
      *write_to = strtol ((char *) xmlTextReaderConstValue (reader), &ret, 0);
      if (ret != NULL)
        return FALSE;
      found = TRUE;
    }
  }
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
    /* if we're back at our depth, we have all info, and can return
     * the completely parsed template */
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
  xmlChar *feature_name =
      xmlTextReaderGetAttribute (reader, BAD_CAST "typename");
  GstPluginFeature *feature;
  GType type;

  if (!feature_name)
    return NULL;
  type = g_type_from_name ((gchar *) feature_name);
  xmlFree (feature_name);
  if (!type)
    return NULL;
  feature = g_object_new (type, NULL);
  if (!feature)
    return NULL;
  if (!GST_IS_PLUGIN_FEATURE (feature)) {
    g_object_unref (feature);
    return NULL;
  }
  while ((ret = xmlTextReaderRead (reader)) == 1) {
    if (xmlTextReaderDepth (reader) == depth)
      return feature;
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
          read_string (reader, &factory->details.longname);
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

          if (read_string (reader, &s)) {
            add_to_char_array (&factory->uri_protocols, s);
          }
        } else if (g_str_equal (tag, "interface")) {
          gchar *s = NULL;

          if (read_string (reader, &s)) {
            __gst_element_factory_add_interface (factory, s);
            g_free (s);
          }
        } else if (g_str_equal (tag, "padtemplate")) {
          GstStaticPadTemplate *template = load_pad_template (reader);

          if (template) {
            GST_LOG ("adding template %s to factory %s",
                template->name_template, GST_PLUGIN_FEATURE_NAME (feature));
            __gst_element_factory_add_static_pad_template (factory, template);
          }
        }
      } else if (GST_IS_TYPE_FIND_FACTORY (feature)) {
        GstTypeFindFactory *factory = GST_TYPE_FIND_FACTORY (feature);

        if (g_str_equal (tag, "extension")) {
          gchar *s = NULL;

          if (read_string (reader, &s)) {
            add_to_char_array (&factory->extensions, s);
          }
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

  return NULL;
}

static GstPlugin *
load_plugin (xmlTextReaderPtr reader)
{
  int ret;
  GstPlugin *plugin = g_new0 (GstPlugin, 1);

  while ((ret = xmlTextReaderRead (reader)) == 1) {
    if (xmlTextReaderDepth (reader) == 1) {
      return plugin;
    }
    if (xmlTextReaderNodeType (reader) == XML_READER_TYPE_ELEMENT &&
        xmlTextReaderDepth (reader) == 2) {
      const gchar *tag = (gchar *) xmlTextReaderConstName (reader);

      if (g_str_equal (tag, "name")) {
        if (!read_string (reader, &plugin->desc.name))
          break;
      } else if (g_str_equal (tag, "description")) {
        if (!read_string (reader, &plugin->desc.description))
          break;
      } else if (g_str_equal (tag, "filename")) {
        if (!read_string (reader, &plugin->filename))
          break;
      } else if (g_str_equal (tag, "version")) {
        if (!read_string (reader, &plugin->desc.version))
          break;
      } else if (g_str_equal (tag, "license")) {
        if (!read_string (reader, &plugin->desc.license))
          break;
      } else if (g_str_equal (tag, "package")) {
        if (!read_string (reader, &plugin->desc.package))
          break;
      } else if (g_str_equal (tag, "origin")) {
        if (!read_string (reader, &plugin->desc.origin))
          break;
      } else if (g_str_equal (tag, "feature")) {
        GstPluginFeature *feature = load_feature (reader);

        if (feature)
          gst_plugin_add_feature (plugin, feature);
      }
    }
  }
  g_free (plugin);

  return NULL;
}

static void
load_paths (xmlTextReaderPtr reader, GstXMLRegistry * registry)
{
  int ret;

  while ((ret = xmlTextReaderRead (reader)) == 1) {
    if (xmlTextReaderDepth (reader) == 1) {
      return;
    }
    if (xmlTextReaderNodeType (reader) == XML_READER_TYPE_ELEMENT &&
        xmlTextReaderDepth (reader) == 2) {
      const gchar *tag = (gchar *) xmlTextReaderConstName (reader);

      if (g_str_equal (tag, "path")) {
        gchar *s = NULL;

        if (read_string (reader, &s) &&
            !g_list_find_custom (GST_REGISTRY (registry)->paths, s,
                (GCompareFunc) strcmp))
          gst_registry_add_path (GST_REGISTRY (registry), s);
        g_free (s);
      }
    }
  }

  return;
}

static gboolean
gst_xml_registry_load (GstRegistry * registry)
{
  GstXMLRegistry *xmlregistry;
  GTimer *timer;
  gdouble seconds;
  xmlTextReaderPtr reader;
  int ret;
  gboolean in_registry = FALSE;

  xmlregistry = GST_XML_REGISTRY (registry);

  /* make sure these types exist */
  GST_TYPE_ELEMENT_FACTORY;
  GST_TYPE_TYPE_FIND_FACTORY;
  GST_TYPE_INDEX_FACTORY;

  timer = g_timer_new ();

  if (!CLASS (xmlregistry)->open_func (xmlregistry, GST_XML_REGISTRY_READ)) {
    g_timer_destroy (timer);
    return FALSE;
  }

  reader = xmlReaderForFd (fileno (xmlregistry->regfile), NULL, NULL, 0);
  if (!reader) {
    CLASS (xmlregistry)->close_func (xmlregistry);
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
          GstPlugin *plugin = load_plugin (reader);

          if (plugin) {
            GST_CAT_DEBUG (GST_CAT_PLUGIN_LOADING,
                "adding plugin %s with %d features", plugin->desc.name,
                plugin->numfeatures);
            gst_registry_add_plugin (GST_REGISTRY (xmlregistry), plugin);
          }
        } else if (g_str_equal (tag, "gst-plugin-paths")) {
          load_paths (reader, xmlregistry);
        }
      }
    }
  }
  xmlFreeTextReader (reader);
  if (ret != 0) {
    GST_ERROR ("parsing registry: %s (at %s)", registry->name,
        xmlregistry->location);
    CLASS (xmlregistry)->close_func (xmlregistry);
    g_timer_destroy (timer);
    return FALSE;
  }

  g_timer_stop (timer);
  seconds = g_timer_elapsed (timer, NULL);
  g_timer_destroy (timer);

  GST_INFO ("loaded %s in %f seconds (%s)",
      registry->name, seconds, xmlregistry->location);

  CLASS (xmlregistry)->close_func (xmlregistry);

  return TRUE;
}

static GstRegistryReturn
gst_xml_registry_load_plugin (GstRegistry * registry, GstPlugin * plugin)
{
  GError *error = NULL;
  GstPlugin *loaded_plugin;

  /* FIXME: add gerror support */
  loaded_plugin = gst_plugin_load_file (plugin->filename, &error);
  if (!plugin) {
    if (error) {
      g_warning ("could not load plugin %s: %s", plugin->desc.name,
          error->message);
      g_error_free (error);
    }
    return GST_REGISTRY_PLUGIN_LOAD_ERROR;
  } else if (loaded_plugin != plugin) {
    g_critical ("how to remove plugins?");
  }

  return GST_REGISTRY_OK;
}

/*
 * Save
 */
#define PUT_ESCAPED(tag,value) 					\
G_STMT_START{ 							\
  const gchar *toconv = value;					\
  if (toconv) {							\
    gchar *v = g_markup_escape_text (toconv, strlen (toconv));	\
    CLASS (xmlregistry)->save_func (xmlregistry, "<%s>%s</%s>\n", tag, v, tag);			\
    g_free (v);							\
  }								\
}G_STMT_END
#define PUT_ESCAPED_INT(tag,value)				\
G_STMT_START{ 							\
  gchar *save = g_strdup_printf ("%ld", (glong) value);		\
  CLASS (xmlregistry)->save_func (xmlregistry, "<%s>%s</%s>\n", tag, save, tag);		\
  g_free (save);      						\
}G_STMT_END


static gboolean
gst_xml_registry_save_caps (GstXMLRegistry * xmlregistry, const GstCaps * caps)
{
  /* we copy the caps here so we can simplify them before saving. This is a lot 
   * faster when loading them later on */
  char *s;
  GstCaps *copy = gst_caps_copy (caps);

  gst_caps_do_simplify (copy);
  s = gst_caps_to_string (copy);
  gst_caps_unref (copy);

  PUT_ESCAPED ("caps", s);
  g_free (s);
  return TRUE;
}

static gboolean
gst_xml_registry_save_pad_template (GstXMLRegistry * xmlregistry,
    GstStaticPadTemplate * template)
{
  gchar *presence;

  PUT_ESCAPED ("nametemplate", template->name_template);
  CLASS (xmlregistry)->save_func (xmlregistry, "<direction>%s</direction>\n",
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
  CLASS (xmlregistry)->save_func (xmlregistry, "<presence>%s</presence>\n",
      presence);

  if (template->static_caps.string) {
    CLASS (xmlregistry)->save_func (xmlregistry, "<caps>%s</caps>\n",
        template->static_caps.string);
  }
  return TRUE;
}

static gboolean
gst_xml_registry_save_feature (GstXMLRegistry * xmlregistry,
    GstPluginFeature * feature)
{
  PUT_ESCAPED ("name", feature->name);

  if (feature->rank > 0) {
    gint rank = feature->rank;

    CLASS (xmlregistry)->save_func (xmlregistry, "<rank>%d</rank>\n", rank);
  }

  if (GST_IS_ELEMENT_FACTORY (feature)) {
    GstElementFactory *factory = GST_ELEMENT_FACTORY (feature);
    GList *walk;

    PUT_ESCAPED ("longname", factory->details.longname);
    PUT_ESCAPED ("class", factory->details.klass);
    PUT_ESCAPED ("description", factory->details.description);
    PUT_ESCAPED ("author", factory->details.author);

    walk = factory->staticpadtemplates;

    while (walk) {
      GstStaticPadTemplate *template = walk->data;

      CLASS (xmlregistry)->save_func (xmlregistry, "<padtemplate>\n");
      gst_xml_registry_save_pad_template (xmlregistry, template);
      CLASS (xmlregistry)->save_func (xmlregistry, "</padtemplate>\n");

      walk = g_list_next (walk);
    }

    walk = factory->interfaces;
    while (walk) {
      PUT_ESCAPED ("interface", (gchar *) walk->data);
      walk = g_list_next (walk);
    }

    if (GST_URI_TYPE_IS_VALID (factory->uri_type)) {
      gchar **protocol;

      PUT_ESCAPED ("uri_type",
          factory->uri_type == GST_URI_SINK ? "sink" : "source");
      g_assert (factory->uri_protocols);
      protocol = factory->uri_protocols;
      while (*protocol) {
        PUT_ESCAPED ("uri_protocol", *protocol);
        protocol++;
      }
    }
  } else if (GST_IS_TYPE_FIND_FACTORY (feature)) {
    GstTypeFindFactory *factory = GST_TYPE_FIND_FACTORY (feature);
    gint i = 0;

    if (factory->caps) {
      gst_xml_registry_save_caps (xmlregistry, factory->caps);
    }
    if (factory->extensions) {
      while (factory->extensions[i]) {
        PUT_ESCAPED ("extension", factory->extensions[i]);
        i++;
      }
    }
  } else if (GST_IS_INDEX_FACTORY (feature)) {
    PUT_ESCAPED ("longdesc", GST_INDEX_FACTORY (feature)->longdesc);
  }
  return TRUE;
}

static gboolean
gst_xml_registry_save_plugin (GstXMLRegistry * xmlregistry, GstPlugin * plugin)
{
  GList *walk;

  PUT_ESCAPED ("name", plugin->desc.name);
  PUT_ESCAPED ("description", plugin->desc.description);
  PUT_ESCAPED ("filename", plugin->filename);
  PUT_ESCAPED ("version", plugin->desc.version);
  PUT_ESCAPED ("license", plugin->desc.license);
  PUT_ESCAPED ("package", plugin->desc.package);
  PUT_ESCAPED ("origin", plugin->desc.origin);

  walk = plugin->features;

  while (walk) {
    GstPluginFeature *feature = GST_PLUGIN_FEATURE (walk->data);

    CLASS (xmlregistry)->save_func (xmlregistry, "<feature typename=\"%s\">\n",
        g_type_name (G_OBJECT_TYPE (feature)));
    gst_xml_registry_save_feature (xmlregistry, feature);
    CLASS (xmlregistry)->save_func (xmlregistry, "</feature>\n");

    walk = g_list_next (walk);
  }
  return TRUE;
}


static gboolean
gst_xml_registry_save (GstRegistry * registry)
{
  GList *walk;
  GstXMLRegistry *xmlregistry;

  g_return_val_if_fail (GST_IS_REGISTRY (registry), FALSE);
  g_return_val_if_fail (registry->flags & GST_REGISTRY_WRITABLE, FALSE);

  xmlregistry = GST_XML_REGISTRY (registry);

  if (!CLASS (xmlregistry)->open_func (xmlregistry, GST_XML_REGISTRY_WRITE)) {
    return FALSE;
  }

  CLASS (xmlregistry)->save_func (xmlregistry, "<?xml version=\"1.0\"?>\n");
  CLASS (xmlregistry)->save_func (xmlregistry, "<GST-PluginRegistry>\n");

  walk = gst_registry_get_path_list (GST_REGISTRY (registry));

  CLASS (xmlregistry)->save_func (xmlregistry, "<gst-plugin-paths>\n");
  while (walk) {
    CLASS (xmlregistry)->save_func (xmlregistry, "<path>");
    CLASS (xmlregistry)->save_func (xmlregistry, (gchar *) walk->data);
    CLASS (xmlregistry)->save_func (xmlregistry, "</path>\n");
    walk = g_list_next (walk);
  }
  CLASS (xmlregistry)->save_func (xmlregistry, "</gst-plugin-paths>\n");

  walk = registry->plugins;

  while (walk) {
    GstPlugin *plugin = GST_PLUGIN (walk->data);

    CLASS (xmlregistry)->save_func (xmlregistry, "<plugin>\n");
    gst_xml_registry_save_plugin (xmlregistry, plugin);
    CLASS (xmlregistry)->save_func (xmlregistry, "</plugin>\n");

    walk = g_list_next (walk);
  }
  CLASS (xmlregistry)->save_func (xmlregistry, "</GST-PluginRegistry>\n");

  CLASS (xmlregistry)->close_func (xmlregistry);

  return TRUE;
}

static GList *
gst_xml_registry_rebuild_recurse (GstXMLRegistry * registry,
    const gchar * directory)
{
  GList *ret = NULL;
  gint dr_len, sf_len;

  if (g_file_test (directory, G_FILE_TEST_IS_DIR)) {
    GDir *dir = g_dir_open (directory, 0, NULL);

    if (dir) {
      const gchar *dirent;

      while ((dirent = g_dir_read_name (dir))) {
        gchar *dirname;

        if (*dirent == '=') {
          /* =build, =inst, etc. -- automake distcheck directories */
          continue;
        }

        dirname = g_strjoin ("/", directory, dirent, NULL);
        ret =
            g_list_concat (ret, gst_xml_registry_rebuild_recurse (registry,
                dirname));
        g_free (dirname);
      }
      g_dir_close (dir);
    }
  } else {
    dr_len = strlen (directory);
    sf_len = strlen (G_MODULE_SUFFIX);
    if (dr_len >= sf_len &&
        strcmp (directory + dr_len - sf_len, G_MODULE_SUFFIX) == 0) {
      ret = g_list_prepend (ret, g_strdup (directory));
    }
  }

  return ret;
}

static gboolean
gst_xml_registry_rebuild (GstRegistry * registry)
{
  GList *walk = NULL, *plugins = NULL, *prune = NULL;
  GError *error = NULL;
  guint length;
  GstPlugin *plugin;
  GstXMLRegistry *xmlregistry = GST_XML_REGISTRY (registry);

  walk = registry->paths;

  while (walk) {
    gchar *path = (gchar *) walk->data;

    GST_CAT_INFO (GST_CAT_PLUGIN_LOADING,
        "Rebuilding registry %p in directory %s...", registry, path);

    plugins = g_list_concat (plugins,
        gst_xml_registry_rebuild_recurse (xmlregistry, path));

    walk = g_list_next (walk);
  }

  do {
    length = g_list_length (plugins);

    walk = plugins;
    while (walk) {
      g_assert (walk->data);
      plugin = gst_plugin_load_file ((gchar *) walk->data, NULL);
      if (plugin) {
        prune = g_list_append (prune, walk->data);
        gst_registry_add_plugin (registry, plugin);
      }

      walk = g_list_next (walk);
    }

    walk = prune;
    while (walk) {
      plugins = g_list_remove (plugins, walk->data);
      g_free (walk->data);
      walk = g_list_next (walk);
    }
    g_list_free (prune);
    prune = NULL;
  } while (g_list_length (plugins) != length);

  walk = plugins;
  while (walk) {
    if ((plugin = gst_plugin_load_file ((gchar *) walk->data, &error))) {
      g_warning ("Bizarre behavior: plugin %s actually loaded",
          (gchar *) walk->data);
      gst_registry_add_plugin (registry, plugin);
    } else {
      GST_CAT_INFO (GST_CAT_PLUGIN_LOADING, "Plugin %s failed to load: %s",
          (gchar *) walk->data, error->message);

      g_free (walk->data);
      g_error_free (error);
      error = NULL;
    }

    walk = g_list_next (walk);
  }
  return TRUE;
}
