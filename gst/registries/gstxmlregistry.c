/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstxml_registry.c: GstXMLRegistry object, support routines
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
#  include "config.h"
#endif

#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <utime.h>

#include <gst/gst_private.h>
#include <gst/gstelement.h>
#include <gst/gsttypefind.h>
#include <gst/gstscheduler.h>
#include <gst/gsturi.h>
#include <gst/gstinfo.h>

#include "gstxmlregistry.h"

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
static void gst_xml_registry_add_path_list_func (GstXMLRegistry * registry);
static gboolean gst_xml_registry_open_func (GstXMLRegistry * registry,
    GstXMLRegistryMode mode);
static gboolean gst_xml_registry_load_func (GstXMLRegistry * registry,
    gchar * data, gssize * size);
static gboolean gst_xml_registry_save_func (GstXMLRegistry * registry,
    gchar * format, ...);
static gboolean gst_xml_registry_close_func (GstXMLRegistry * registry);

static GstRegistryReturn gst_xml_registry_load_plugin (GstRegistry * registry,
    GstPlugin * plugin);

static void gst_xml_registry_start_element (GMarkupParseContext * context,
    const gchar * element_name,
    const gchar ** attribute_names,
    const gchar ** attribute_values, gpointer user_data, GError ** error);
static void gst_xml_registry_end_element (GMarkupParseContext * context,
    const gchar * element_name, gpointer user_data, GError ** error);
static void gst_xml_registry_text (GMarkupParseContext * context,
    const gchar * text, gsize text_len, gpointer user_data, GError ** error);
static void gst_xml_registry_passthrough (GMarkupParseContext * context,
    const gchar * passthrough_text,
    gsize text_len, gpointer user_data, GError ** error);
static void gst_xml_registry_error (GMarkupParseContext * context,
    GError * error, gpointer user_data);


static void gst_xml_registry_paths_start_element (GMarkupParseContext * context,
    const gchar * element_name,
    const gchar ** attribute_names,
    const gchar ** attribute_values, gpointer user_data, GError ** error);
static void gst_xml_registry_paths_end_element (GMarkupParseContext * context,
    const gchar * element_name, gpointer user_data, GError ** error);
static void gst_xml_registry_paths_text (GMarkupParseContext * context,
    const gchar * text, gsize text_len, gpointer user_data, GError ** error);

static GstRegistryClass *parent_class = NULL;

/* static guint gst_xml_registry_signals[LAST_SIGNAL] = { 0 }; */

static const GMarkupParser gst_xml_registry_parser = {
  gst_xml_registry_start_element,
  gst_xml_registry_end_element,
  gst_xml_registry_text,
  gst_xml_registry_passthrough,
  gst_xml_registry_error,
};

static const GMarkupParser gst_xml_registry_paths_parser = {
  gst_xml_registry_paths_start_element,
  gst_xml_registry_paths_end_element,
  gst_xml_registry_paths_text,
  NULL,
  NULL
};


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
  gstxmlregistry_class->add_path_list_func =
      GST_DEBUG_FUNCPTR (gst_xml_registry_add_path_list_func);
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
  registry->context = NULL;
  registry->state = GST_XML_REGISTRY_NONE;
  registry->current_plugin = NULL;
  registry->current_feature = NULL;
  registry->open_tags = NULL;
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

      if (CLASS (object)->add_path_list_func)
	CLASS (object)->add_path_list_func (registry);
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
 */
static time_t
get_time (const char *path)
{
  struct stat statbuf;

  if (stat (path, &statbuf))
    return 0;
  if (statbuf.st_mtime > statbuf.st_ctime)
    return statbuf.st_mtime;
  return statbuf.st_ctime;
}

/* same as 0755 */
#define dirmode \
  (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)

static gboolean
make_dir (gchar * filename)
{
  struct stat dirstat;
  gchar *dirname;

  if (strrchr (filename, '/') == NULL)
    return FALSE;

  dirname = g_strndup (filename, strrchr (filename, '/') - filename);

  if (stat (dirname, &dirstat) == -1 && errno == ENOENT) {
    if (mkdir (dirname, dirmode) != 0) {
      if (make_dir (dirname) != TRUE) {
	g_free (dirname);
	return FALSE;
      } else {
	if (mkdir (dirname, dirmode) != 0)
	  return FALSE;
      }
    }
  }

  g_free (dirname);
  return TRUE;
}

static void
gst_xml_registry_get_perms_func (GstXMLRegistry * registry)
{
  time_t mod_time = 0;
  FILE *temp;

  /* if the dir does not exist, make it. if that can't be done, flags = 0x0.
     if the file can be appended to, it's writable. if it can then be read,
     it's readable. 
     After that check if it exists. */

  if (make_dir (registry->location) != TRUE) {
    /* we can't do anything with it, leave flags as 0x0 */
    return;
  }

  mod_time = get_time (registry->location);

  if ((temp = fopen (registry->location, "a"))) {
    GST_REGISTRY (registry)->flags |= GST_REGISTRY_WRITABLE;
    fclose (temp);
  }

  if ((temp = fopen (registry->location, "r"))) {
    GST_REGISTRY (registry)->flags |= GST_REGISTRY_READABLE;
    fclose (temp);
  }

  if (g_file_test (registry->location, G_FILE_TEST_EXISTS)) {
    GST_REGISTRY (registry)->flags |= GST_REGISTRY_EXISTS;
  }

  if (mod_time) {
    struct utimbuf utime_buf;

    /* set the modification time back to its previous value */
    utime_buf.actime = mod_time;
    utime_buf.modtime = mod_time;
    utime (registry->location, &utime_buf);
  } else if (GST_REGISTRY (registry)->flags & GST_REGISTRY_WRITABLE) {
    /* it did not exist before, so delete it */
    unlink (registry->location);
  }
}

static void
gst_xml_registry_add_path_list_func (GstXMLRegistry * registry)
{
  FILE *reg = NULL;
  GMarkupParseContext *context;
  gchar *text = NULL;
  gssize size;
  GError *error = NULL;

  context = g_markup_parse_context_new (&gst_xml_registry_paths_parser, 0,
      registry, NULL);

  if (!(reg = fopen (registry->location, "r"))) {
    goto finished;
  }

  /* slightly allocate more as gmarkup reads too much */
  text = g_malloc0 (BLOCK_SIZE + 32);

  size = fread (text, 1, BLOCK_SIZE, reg);

  while (size) {
    g_markup_parse_context_parse (context, text, size, &error);

    if (error) {
      GST_ERROR ("parsing registry %s: %s\n",
	  registry->location, error->message);
      goto finished;
    }

    if (registry->state == GST_XML_REGISTRY_PATHS_DONE)
      break;

    size = fread (text, 1, BLOCK_SIZE, reg);
  }

finished:

  g_markup_parse_context_free (context);

  if (reg)
    fclose (reg);

  g_free (text);
}

static gboolean
plugin_times_older_than_recurse (gchar * path, time_t regtime)
{
  DIR *dir;
  struct dirent *dirent;
  gchar *pluginname;

  time_t pathtime = get_time (path);

  if (pathtime > regtime) {
    GST_CAT_INFO (GST_CAT_PLUGIN_LOADING,
	"time for %s was %ld; more recent than registry time of %ld\n",
	path, (long) pathtime, (long) regtime);
    return FALSE;
  }

  dir = opendir (path);
  if (dir) {
    while ((dirent = readdir (dir))) {
      /* don't want to recurse in place or backwards */
      if (strcmp (dirent->d_name, ".") && strcmp (dirent->d_name, "..")) {
	pluginname = g_strjoin ("/", path, dirent->d_name, NULL);
	if (!plugin_times_older_than_recurse (pluginname, regtime)) {
	  g_free (pluginname);
	  closedir (dir);
	  return FALSE;
	}
	g_free (pluginname);
      }
    }
    closedir (dir);
  }
  return TRUE;
}

static gboolean
plugin_times_older_than (GList * paths, time_t regtime)
{
  /* return true iff regtime is more recent than the times of all the files
   * in the plugin dirs.
   */

  while (paths) {
    GST_CAT_LOG (GST_CAT_PLUGIN_LOADING,
	"comparing plugin times from %s with %ld",
	(gchar *) paths->data, (long) regtime);
    if (!plugin_times_older_than_recurse (paths->data, regtime))
      return FALSE;
    paths = g_list_next (paths);
  }
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

    if (!plugin_times_older_than (paths, get_time (registry->location))) {
      if (gst_registry->flags & GST_REGISTRY_WRITABLE) {
	GST_CAT_INFO (GST_CAT_GST_INIT, "Registry out of date, rebuilding...");

	gst_registry_rebuild (gst_registry);

	gst_registry_save (gst_registry);

	if (!plugin_times_older_than (paths, get_time (registry->location))) {
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
    g_return_val_if_fail (gst_registry->flags & GST_REGISTRY_WRITABLE, FALSE);

    GST_CAT_DEBUG (GST_CAT_GST_INIT, "opening registry %s for writing",
	registry->location);
    registry->regfile = fopen (registry->location, "w");
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
  GST_CAT_DEBUG (GST_CAT_GST_INIT, "closing registry %s", registry->location);
  fclose (registry->regfile);

  registry->open = FALSE;

  return TRUE;
}

static gboolean
gst_xml_registry_load (GstRegistry * registry)
{
  GstXMLRegistry *xmlregistry;
  gchar *text;
  gssize size;
  GError *error = NULL;
  GTimer *timer;
  gdouble seconds;

  xmlregistry = GST_XML_REGISTRY (registry);

  timer = g_timer_new ();

  xmlregistry->context =
      g_markup_parse_context_new (&gst_xml_registry_parser, 0, registry, NULL);

  if (!CLASS (xmlregistry)->open_func (xmlregistry, GST_XML_REGISTRY_READ)) {
    g_timer_destroy (timer);
    return FALSE;
  }

  text = g_malloc0 (BLOCK_SIZE + 32);

  size = BLOCK_SIZE;
  CLASS (xmlregistry)->load_func (xmlregistry, text, &size);

  while (size) {
    g_markup_parse_context_parse (xmlregistry->context, text, size, &error);

    if (error) {
      GST_ERROR ("parsing registry: %s\n", error->message);
      g_free (text);
      CLASS (xmlregistry)->close_func (xmlregistry);
      g_timer_destroy (timer);
      return FALSE;
    }

    size = BLOCK_SIZE;
    CLASS (xmlregistry)->load_func (xmlregistry, text, &size);
  }

  g_free (text);

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

static gboolean
gst_xml_registry_parse_plugin (GMarkupParseContext * context, const gchar * tag,
    const gchar * text, gsize text_len, GstXMLRegistry * registry,
    GError ** error)
{
  GstPlugin *plugin = registry->current_plugin;

  if (!strcmp (tag, "name")) {
    plugin->desc.name = g_strndup (text, text_len);
  } else if (!strcmp (tag, "description")) {
    plugin->desc.description = g_strndup (text, text_len);
  } else if (!strcmp (tag, "filename")) {
    plugin->filename = g_strndup (text, text_len);
  } else if (!strcmp (tag, "version")) {
    plugin->desc.version = g_strndup (text, text_len);
  } else if (!strcmp (tag, "license")) {
    plugin->desc.license = g_strndup (text, text_len);
  } else if (!strcmp (tag, "package")) {
    plugin->desc.package = g_strndup (text, text_len);
  } else if (!strcmp (tag, "origin")) {
    plugin->desc.origin = g_strndup (text, text_len);
  }

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

static gboolean
gst_xml_registry_parse_element_factory (GMarkupParseContext * context,
    const gchar * tag, const gchar * text, gsize text_len,
    GstXMLRegistry * registry, GError ** error)
{
  GstElementFactory *factory = GST_ELEMENT_FACTORY (registry->current_feature);

  if (!strcmp (tag, "name")) {
    gchar *name = g_strndup (text, text_len);

    gst_plugin_feature_set_name (registry->current_feature, name);
    g_free (name);
  } else if (!strcmp (tag, "longname")) {
    g_free (factory->details.longname);
    factory->details.longname = g_strndup (text, text_len);
  } else if (!strcmp (tag, "class")) {
    g_free (factory->details.klass);
    factory->details.klass = g_strndup (text, text_len);
  } else if (!strcmp (tag, "description")) {
    g_free (factory->details.description);
    factory->details.description = g_strndup (text, text_len);
  } else if (!strcmp (tag, "author")) {
    g_free (factory->details.author);
    factory->details.author = g_strndup (text, text_len);
  } else if (!strcmp (tag, "rank")) {
    gint rank;
    gchar *ret;

    rank = strtol (text, &ret, 0);
    if (ret == text + text_len) {
      gst_plugin_feature_set_rank (GST_PLUGIN_FEATURE (factory), rank);
    }
  } else if (!strcmp (tag, "uri_type")) {
    if (strncasecmp (text, "sink", 4) == 0) {
      factory->uri_type = GST_URI_SINK;
    } else if (strncasecmp (text, "source", 5) == 0) {
      factory->uri_type = GST_URI_SRC;
    }
  } else if (!strcmp (tag, "uri_protocol")) {
    add_to_char_array (&factory->uri_protocols, g_strndup (text, text_len));
  } else if (!strcmp (tag, "interface")) {
    gchar *tmp = g_strndup (text, text_len);

    __gst_element_factory_add_interface (factory, tmp);
    g_free (tmp);
  }

  return TRUE;
}

static gboolean
gst_xml_registry_parse_type_find_factory (GMarkupParseContext * context,
    const gchar * tag, const gchar * text, gsize text_len,
    GstXMLRegistry * registry, GError ** error)
{
  GstTypeFindFactory *factory =
      GST_TYPE_FIND_FACTORY (registry->current_feature);

  if (!strcmp (tag, "name")) {
    registry->current_feature->name = g_strndup (text, text_len);
  } else if (!strcmp (tag, "rank")) {
    glong rank;
    gchar *ret;

    rank = strtol (text, &ret, 0);
    if (ret == text + text_len) {
      gst_plugin_feature_set_rank (GST_PLUGIN_FEATURE (factory), rank);
    }
  }
  /* FIXME!!
     else if (!strcmp (tag, "caps")) {
     factory->caps = g_strndup (text, text_len);
     } */
  else if (!strcmp (tag, "extension")) {
    add_to_char_array (&factory->extensions, g_strndup (text, text_len));
  }

  return TRUE;
}

static gboolean
gst_xml_registry_parse_scheduler_factory (GMarkupParseContext * context,
    const gchar * tag, const gchar * text, gsize text_len,
    GstXMLRegistry * registry, GError ** error)
{
  GstSchedulerFactory *factory =
      GST_SCHEDULER_FACTORY (registry->current_feature);

  if (!strcmp (tag, "name")) {
    registry->current_feature->name = g_strndup (text, text_len);
  } else if (!strcmp (tag, "longdesc")) {
    factory->longdesc = g_strndup (text, text_len);
  }
  return TRUE;
}

static gboolean
gst_xml_registry_parse_index_factory (GMarkupParseContext * context,
    const gchar * tag, const gchar * text, gsize text_len,
    GstXMLRegistry * registry, GError ** error)
{
  GstIndexFactory *factory = GST_INDEX_FACTORY (registry->current_feature);

  if (!strcmp (tag, "name")) {
    registry->current_feature->name = g_strndup (text, text_len);
  } else if (!strcmp (tag, "longdesc")) {
    factory->longdesc = g_strndup (text, text_len);
  }
  return TRUE;
}

static gboolean
gst_xml_registry_parse_padtemplate (GMarkupParseContext * context,
    const gchar * tag, const gchar * text, gsize text_len,
    GstXMLRegistry * registry, GError ** error)
{
  if (!strcmp (tag, "nametemplate")) {
    registry->name_template = g_strndup (text, text_len);
  } else if (!strcmp (tag, "direction")) {
    if (!strncmp (text, "sink", text_len)) {
      registry->direction = GST_PAD_SINK;
    } else if (!strncmp (text, "src", text_len)) {
      registry->direction = GST_PAD_SRC;
    }
  } else if (!strcmp (tag, "presence")) {
    if (!strncmp (text, "always", text_len)) {
      registry->presence = GST_PAD_ALWAYS;
    } else if (!strncmp (text, "sometimes", text_len)) {
      registry->presence = GST_PAD_SOMETIMES;
    } else if (!strncmp (text, "request", text_len)) {
      registry->presence = GST_PAD_REQUEST;
    }
  } else if (!strncmp (tag, "caps", 4)) {
    char *s;

    s = g_strndup (text, text_len);
    g_assert (registry->caps == NULL);
    registry->caps = gst_caps_from_string (s);
    if (registry->caps == NULL) {
      g_critical ("Could not parse caps: length %d, content: %*s\n", text_len,
	  text_len, text);
    }
    g_free (s);
    return TRUE;
  }
  return TRUE;
}

static void
gst_xml_registry_start_element (GMarkupParseContext * context,
    const gchar * element_name,
    const gchar ** attribute_names,
    const gchar ** attribute_values, gpointer user_data, GError ** error)
{
  GstXMLRegistry *xmlregistry = GST_XML_REGISTRY (user_data);

  xmlregistry->open_tags = g_list_prepend (xmlregistry->open_tags,
      g_strdup (element_name));

  switch (xmlregistry->state) {
    case GST_XML_REGISTRY_NONE:
      if (!strcmp (element_name, "GST-PluginRegistry")) {
	xmlregistry->state = GST_XML_REGISTRY_TOP;
      }
      break;
    case GST_XML_REGISTRY_TOP:
      if (!strncmp (element_name, "plugin", 6)) {
	xmlregistry->state = GST_XML_REGISTRY_PLUGIN;
	xmlregistry->parser = gst_xml_registry_parse_plugin;
	xmlregistry->current_plugin = (GstPlugin *) g_new0 (GstPlugin, 1);
      }
      break;
    case GST_XML_REGISTRY_PLUGIN:
      if (!strncmp (element_name, "feature", 7)) {
	gint i = 0;
	GstPluginFeature *feature = NULL;

	xmlregistry->state = GST_XML_REGISTRY_FEATURE;

	while (attribute_names[i]) {
	  if (!strncmp (attribute_names[i], "typename", 8)) {
	    feature =
		GST_PLUGIN_FEATURE (g_object_new (g_type_from_name
		    (attribute_values[i]), NULL));
	    break;
	  }
	  i++;
	}
	if (feature) {
	  xmlregistry->current_feature = feature;

	  if (GST_IS_ELEMENT_FACTORY (feature)) {
	    GstElementFactory *factory = GST_ELEMENT_FACTORY (feature);

	    factory->padtemplates = NULL;
	    xmlregistry->parser = gst_xml_registry_parse_element_factory;
	    break;
	  } else if (GST_IS_TYPE_FIND_FACTORY (feature)) {
	    xmlregistry->parser = gst_xml_registry_parse_type_find_factory;
	  } else if (GST_IS_SCHEDULER_FACTORY (feature)) {
	    xmlregistry->parser = gst_xml_registry_parse_scheduler_factory;
	    GST_SCHEDULER_FACTORY (feature)->type = 0;
	  } else if (GST_IS_INDEX_FACTORY (feature)) {
	    xmlregistry->parser = gst_xml_registry_parse_index_factory;
	  } else {
	    g_warning ("unknown feature type");
	  }
	}
      }
      break;
    case GST_XML_REGISTRY_FEATURE:
      if (!strncmp (element_name, "padtemplate", 11)) {
	xmlregistry->state = GST_XML_REGISTRY_PADTEMPLATE;
	xmlregistry->parser = gst_xml_registry_parse_padtemplate;
	xmlregistry->name_template = NULL;
	xmlregistry->direction = 0;
	xmlregistry->presence = 0;
	xmlregistry->caps = NULL;
      }
      break;
    default:
      break;
  }
}

static void
gst_xml_registry_end_element (GMarkupParseContext * context,
    const gchar * element_name, gpointer user_data, GError ** error)
{
  GstXMLRegistry *xmlregistry = GST_XML_REGISTRY (user_data);
  gchar *open_tag = (gchar *) xmlregistry->open_tags->data;

  xmlregistry->open_tags = g_list_remove (xmlregistry->open_tags, open_tag);
  g_free (open_tag);

  switch (xmlregistry->state) {
    case GST_XML_REGISTRY_TOP:
      if (!strcmp (element_name, "GST-PluginRegistry")) {
	xmlregistry->state = GST_XML_REGISTRY_NONE;
      }
      break;
    case GST_XML_REGISTRY_PLUGIN:
      if (!strcmp (element_name, "plugin")) {
	xmlregistry->state = GST_XML_REGISTRY_TOP;
	xmlregistry->parser = NULL;
	gst_registry_add_plugin (GST_REGISTRY (xmlregistry),
	    xmlregistry->current_plugin);
      }
      break;
    case GST_XML_REGISTRY_FEATURE:
      if (!strcmp (element_name, "feature")) {
	xmlregistry->state = GST_XML_REGISTRY_PLUGIN;
	xmlregistry->parser = gst_xml_registry_parse_plugin;
	gst_plugin_add_feature (xmlregistry->current_plugin,
	    xmlregistry->current_feature);
	xmlregistry->current_feature = NULL;
      }
      break;
    case GST_XML_REGISTRY_PADTEMPLATE:
      if (!strcmp (element_name, "padtemplate")) {
	GstPadTemplate *template;

	template = gst_pad_template_new (xmlregistry->name_template,
	    xmlregistry->direction, xmlregistry->presence, xmlregistry->caps);

	g_free (xmlregistry->name_template);
	xmlregistry->name_template = NULL;
	xmlregistry->caps = NULL;

	__gst_element_factory_add_pad_template (GST_ELEMENT_FACTORY
	    (xmlregistry->current_feature), template);
	xmlregistry->state = GST_XML_REGISTRY_FEATURE;
	xmlregistry->parser = gst_xml_registry_parse_element_factory;
      }
      break;
    default:
      break;
  }
}

static void
gst_xml_registry_text (GMarkupParseContext * context, const gchar * text,
    gsize text_len, gpointer user_data, GError ** error)
{
  GstXMLRegistry *xmlregistry = GST_XML_REGISTRY (user_data);
  gchar *open_tag;

  if (xmlregistry->open_tags) {
    open_tag = (gchar *) xmlregistry->open_tags->data;

    if (!strcmp (open_tag, "plugin-path")) {
      //gst_plugin_add_path (g_strndup (text, text_len));
    } else if (xmlregistry->parser) {
      xmlregistry->parser (context, open_tag, text, text_len, xmlregistry,
	  error);
    }
  }
}

static void
gst_xml_registry_passthrough (GMarkupParseContext * context,
    const gchar * passthrough_text, gsize text_len, gpointer user_data,
    GError ** error)
{
}

static void
gst_xml_registry_error (GMarkupParseContext * context, GError * error,
    gpointer user_data)
{
  GST_ERROR ("%s\n", error->message);
}

static void
gst_xml_registry_paths_start_element (GMarkupParseContext * context,
    const gchar * element_name,
    const gchar ** attribute_names,
    const gchar ** attribute_values, gpointer user_data, GError ** error)
{
  GstXMLRegistry *xmlregistry = GST_XML_REGISTRY (user_data);

  switch (xmlregistry->state) {
    case GST_XML_REGISTRY_NONE:
      if (!strcmp (element_name, "GST-PluginRegistry")) {
	xmlregistry->state = GST_XML_REGISTRY_TOP;
      }
      break;
    case GST_XML_REGISTRY_TOP:
      if (!strcmp (element_name, "gst-registry-paths")) {
	xmlregistry->state = GST_XML_REGISTRY_PATHS;
      }
      break;
    case GST_XML_REGISTRY_PATHS:
      if (!strcmp (element_name, "path")) {
	xmlregistry->state = GST_XML_REGISTRY_PATH;
      }
      break;
    default:
      break;
  }
}

static void
gst_xml_registry_paths_end_element (GMarkupParseContext * context,
    const gchar * element_name, gpointer user_data, GError ** error)
{
  GstXMLRegistry *xmlregistry = GST_XML_REGISTRY (user_data);

  switch (xmlregistry->state) {
    case GST_XML_REGISTRY_PATH:
      if (!strcmp (element_name, "path")) {
	xmlregistry->state = GST_XML_REGISTRY_PATHS;
      }
      break;
    case GST_XML_REGISTRY_PATHS:
      if (!strcmp (element_name, "gst-plugin-paths")) {
	xmlregistry->state = GST_XML_REGISTRY_PATHS_DONE;
      }
      break;
    default:
      break;
  }
}

static void
gst_xml_registry_paths_text (GMarkupParseContext * context, const gchar * text,
    gsize text_len, gpointer user_data, GError ** error)
{
  GstXMLRegistry *xmlregistry = GST_XML_REGISTRY (user_data);

  if (xmlregistry->state == GST_XML_REGISTRY_PATH)
    gst_registry_add_path (GST_REGISTRY (xmlregistry), g_strndup (text,
	    text_len));
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
  char *s = gst_caps_to_string (caps);

  PUT_ESCAPED ("caps", s);
  g_free (s);
  return TRUE;
}

static gboolean
gst_xml_registry_save_pad_template (GstXMLRegistry * xmlregistry,
    GstPadTemplate * template)
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

  if (GST_PAD_TEMPLATE_CAPS (template)) {
    gst_xml_registry_save_caps (xmlregistry, GST_PAD_TEMPLATE_CAPS (template));
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

    walk = factory->padtemplates;

    while (walk) {
      GstPadTemplate *template = GST_PAD_TEMPLATE (walk->data);

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
  } else if (GST_IS_SCHEDULER_FACTORY (feature)) {
    PUT_ESCAPED ("longdesc", GST_SCHEDULER_FACTORY (feature)->longdesc);
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

  walk = g_list_last (gst_registry_get_path_list (GST_REGISTRY (registry)));

  CLASS (xmlregistry)->save_func (xmlregistry, "<gst-plugin-paths>\n");
  while (walk) {
    CLASS (xmlregistry)->save_func (xmlregistry, "<path>");
    CLASS (xmlregistry)->save_func (xmlregistry, (gchar *) walk->data);
    CLASS (xmlregistry)->save_func (xmlregistry, "</path>\n");
    walk = g_list_previous (walk);
  }
  CLASS (xmlregistry)->save_func (xmlregistry, "</gst-plugin-paths>\n");

  walk = g_list_last (registry->plugins);

  while (walk) {
    GstPlugin *plugin = GST_PLUGIN (walk->data);

    CLASS (xmlregistry)->save_func (xmlregistry, "<plugin>\n");
    gst_xml_registry_save_plugin (xmlregistry, plugin);
    CLASS (xmlregistry)->save_func (xmlregistry, "</plugin>\n");

    walk = g_list_previous (walk);
  }
  CLASS (xmlregistry)->save_func (xmlregistry, "</GST-PluginRegistry>\n");

  CLASS (xmlregistry)->close_func (xmlregistry);

  return TRUE;
}

static GList *
gst_xml_registry_rebuild_recurse (GstXMLRegistry * registry,
    const gchar * directory)
{
  GDir *dir;
  gchar *temp;
  GList *ret = NULL;

  dir = g_dir_open (directory, 0, NULL);

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
  } else {
    if ((temp = strstr (directory, G_MODULE_SUFFIX)) &&
	(!strcmp (temp, G_MODULE_SUFFIX))) {
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

  plugins = g_list_reverse (plugins);

  do {
    length = g_list_length (plugins);

    walk = plugins;
    while (walk) {
      g_assert (walk->data);
      plugin = gst_plugin_load_file ((gchar *) walk->data, NULL);
      if (plugin) {
	prune = g_list_prepend (prune, walk->data);
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
