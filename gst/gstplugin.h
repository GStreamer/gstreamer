/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstplugin.h: Header for plugin subsystem
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


#ifndef __GST_PLUGIN_H__
#define __GST_PLUGIN_H__

#include <gst/gstconfig.h>

#include <time.h> /* time_t */
#include <sys/types.h> /* off_t */
#include <sys/stat.h> /* off_t */
#include <gmodule.h>
#include <gst/gstobject.h>
#include <gst/gstmacros.h>

G_BEGIN_DECLS

typedef struct _GstPlugin GstPlugin;
typedef struct _GstPluginClass GstPluginClass;
typedef struct _GstPluginDesc GstPluginDesc;

/**
 * gst_plugin_error_quark:
 *
 * Get the error quark.
 *
 * Returns: The error quark used in GError messages
 */
GQuark gst_plugin_error_quark (void);
/**
 * GST_PLUGIN_ERROR:
 *
 * The error message category quark
 */
#define GST_PLUGIN_ERROR gst_plugin_error_quark ()

/**
 * GstPluginError:
 * @GST_PLUGIN_ERROR_MODULE: The plugin could not be loaded
 * @GST_PLUGIN_ERROR_DEPENDENCIES: The plugin has unresolved dependencies
 * @GST_PLUGIN_ERROR_NAME_MISMATCH: The plugin has already be loaded from a different file
 *
 * The plugin loading errors
 */
typedef enum
{
  GST_PLUGIN_ERROR_MODULE,
  GST_PLUGIN_ERROR_DEPENDENCIES,
  GST_PLUGIN_ERROR_NAME_MISMATCH
} GstPluginError;


typedef enum
{
  GST_PLUGIN_FLAG_CACHED = (1<<0),
} GstPluginFlags;

/**
 * GstPluginInitFunc:
 * @plugin: The plugin object that can be used to register #GstPluginFeatures for this plugin.
 *
 * A plugin should provide a pointer to a function of this type in the
 * plugin_desc struct.
 * This function will be called by the loader at startup.
 *
 * Returns: %TRUE if plugin initialised successfully
 */
typedef gboolean (*GstPluginInitFunc) (GstPlugin *plugin);

/**
 * GstPluginDesc:
 * @major_version: the major version number of core that plugin was compiled for
 * @minor_version: the minor version number of core that plugin was compiled for
 * @name: a unique name of the plugin
 * @description: description of plugin
 * @plugin_init: pointer to the init function of this plugin.
 * @version: version of the plugin
 * @license: effective license of plugin
 * @source: source module plugin belongs to
 * @package: shipped package plugin belongs to
 * @origin: URL to provider of plugin
 * @_gst_reserved: private, for later expansion
 *
 *
 * A plugins should export a variable of this type called plugin_desc. This plugin
 * loaded will use this variable to initialize the plugin.
 */
struct _GstPluginDesc {
  gint major_version;
  gint minor_version;
  gchar *name;
  gchar *description;
  GstPluginInitFunc plugin_init;
  gchar *version;
  gchar *license;
  gchar *source;
  gchar *package;
  gchar *origin;

  gpointer _gst_reserved[GST_PADDING];
};


#define GST_TYPE_PLUGIN   (gst_plugin_get_type())
#define GST_IS_PLUGIN(obj)             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_PLUGIN))
#define GST_IS_PLUGIN_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_PLUGIN))
#define GST_PLUGIN_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_PLUGIN, GstPluginClass))
#define GST_PLUGIN(obj)                (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_PLUGIN, GstPlugin))
#define GST_PLUGIN_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_PLUGIN, GstPluginClass))

/**
 * GstPlugin:
 *
 * The plugin object
 */
struct _GstPlugin {
  GstObject       object;

  /*< private >*/
  GstPluginDesc	desc;

  GstPluginDesc *orig_desc;

  unsigned int  flags;

  gchar *	filename;
  gchar *	basename;       /* base name (non-dir part) of plugin path */

  GModule *	module;		/* contains the module if plugin is loaded */

  off_t         file_size;
  time_t        file_mtime;
  gboolean      registered;     /* TRUE when the registry has seen a filename
                                 * that matches the plugin's basename */

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstPluginClass {
  GstObjectClass  object_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GST_PLUGIN_DEFINE:
 * @major: major version number of the gstreamer-core that plugin was compiled for
 * @minor: minor version number of the gstreamer-core that plugin was compiled for
 * @name: short, but unique name of the plugin
 * @description: information about the purpose of the plugin
 * @init: function pointer to the plugin_init method with the signature of <code>static gboolean plugin_init (GstPlugin * plugin)</code>.
 * @version: full version string (e.g. VERSION from config.h)
 * @license: under which licence the package has been released, e.g. GPL, LGPL.
 * @package: the package-name (e.g. PACKAGE_NAME from config.h)
 * @origin: a description from where the package comes from (e.g. the homepage URL)
 *
 * This macro needs to be used to define the entry point and meta data of a
 * plugin. One would use this macro to export a plugin, so that it can be used
 * by other applications
 */
#define GST_PLUGIN_DEFINE(major,minor,name,description,init,version,license,package,origin)	\
GST_PLUGIN_EXPORT GstPluginDesc gst_plugin_desc = {	\
  major,						\
  minor,						\
  name,							\
  description,						\
  init,							\
  version,						\
  license,						\
  PACKAGE,						\
  package,						\
  origin,						\
  GST_PADDING_INIT				        \
};

/**
 * GST_PLUGIN_DEFINE_STATIC:
 * @major: major version number of the gstreamer-core that plugin was compiled for
 * @minor: minor version number of the gstreamer-core that plugin was compiled for
 * @name: short, but unique name of the plugin
 * @description: information about the purpose of the plugin
 * @init: function pointer to the plugin_init method with the signature of <code>static gboolean plugin_init (GstPlugin * plugin)</code>.
 * @version: full version string (e.g. VERSION from config.h)
 * @license: under which licence the package has been released, e.g. GPL, LGPL.
 * @package: the package-name (e.g. PACKAGE_NAME from config.h)
 * @origin: a description from where the package comes from (e.g. the homepage URL)
 *
 * This macro needs to be used to define the entry point and meta data of a
 * local plugin. One would use this macro to define a local plugin that can only
 * be used by the own application.
 */
#define GST_PLUGIN_DEFINE_STATIC(major,minor,name,description,init,version,license,package,origin)  \
static void GST_GNUC_CONSTRUCTOR			\
_gst_plugin_static_init__ ##init (void)			\
{							\
  static GstPluginDesc plugin_desc_ = {			\
    major,						\
    minor,						\
    name,						\
    description,					\
    init,						\
    version,						\
    license,						\
    PACKAGE,						\
    package,						\
    origin,						\
    GST_PADDING_INIT				        \
  };							\
  _gst_plugin_register_static (&plugin_desc_);		\
}

/**
 * GST_LICENSE_UNKNOWN:
 *
 * To be used in GST_PLUGIN_DEFINE or GST_PLUGIN_DEFINE_STATIC if usure about
 * the licence.
 */
#define GST_LICENSE_UNKNOWN "unknown"


/* function for filters */
/**
 * GstPluginFilter:
 * @plugin: the plugin to check
 * @user_data: the user_data that has been passed on e.g. gst_registry_plugin_filter()
 *
 * A function that can be used with e.g. gst_registry_plugin_filter()
 * to get a list of plugins that match certain criteria.
 *
 * Returns: TRUE for a positive match, FALSE otherwise
 */
typedef gboolean        (*GstPluginFilter)              (GstPlugin *plugin,
                                                         gpointer user_data);

GType                   gst_plugin_get_type             (void);

void			_gst_plugin_initialize		(void);
void			_gst_plugin_register_static	(GstPluginDesc *desc);

G_CONST_RETURN gchar*	gst_plugin_get_name		(GstPlugin *plugin);
G_CONST_RETURN gchar*	gst_plugin_get_description	(GstPlugin *plugin);
G_CONST_RETURN gchar*	gst_plugin_get_filename		(GstPlugin *plugin);
G_CONST_RETURN gchar*	gst_plugin_get_version		(GstPlugin *plugin);
G_CONST_RETURN gchar*	gst_plugin_get_license		(GstPlugin *plugin);
G_CONST_RETURN gchar*	gst_plugin_get_source		(GstPlugin *plugin);
G_CONST_RETURN gchar*	gst_plugin_get_package		(GstPlugin *plugin);
G_CONST_RETURN gchar*	gst_plugin_get_origin		(GstPlugin *plugin);
GModule *		gst_plugin_get_module		(GstPlugin *plugin);
gboolean		gst_plugin_is_loaded		(GstPlugin *plugin);

gboolean		gst_plugin_name_filter		(GstPlugin *plugin, const gchar *name);

GstPlugin *		gst_plugin_load_file		(const gchar *filename, GError** error);

GstPlugin *             gst_plugin_load                 (GstPlugin *plugin);
GstPlugin *             gst_plugin_load_by_name         (const gchar *name);

void gst_plugin_list_free (GList *list);

G_END_DECLS

#endif /* __GST_PLUGIN_H__ */
