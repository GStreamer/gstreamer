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
#include <gmodule.h>
#include <gst/gstpluginfeature.h>
#include <gst/gstmacros.h>
#include <gst/gstobject.h>

G_BEGIN_DECLS

typedef struct _GstPlugin GstPlugin;
typedef struct _GstPluginClass GstPluginClass;
typedef struct _GstPluginDesc GstPluginDesc;

GQuark gst_plugin_error_quark (void);
#define GST_PLUGIN_ERROR gst_plugin_error_quark ()

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

/* Initialiser function: returns TRUE if plugin initialised successfully */
typedef gboolean (*GstPluginInitFunc) (GstPlugin *plugin);

struct _GstPluginDesc {
  gint major_version;			/* major version of core that plugin was compiled for */
  gint minor_version;			/* minor version of core that plugin was compiled for */
  gchar *name;				/* unique name of plugin */
  gchar *description;			/* description of plugin */
  GstPluginInitFunc plugin_init;	/* pointer to plugin_init function */
  gchar *version;			/* version of the plugin */
  gchar *license;			/* effective license of plugin */
  gchar *source;			/* source module plugin belongs to */
  gchar *package;			/* shipped package plugin belongs to */
  gchar *origin;			/* URL to provider of plugin */

  gpointer _gst_reserved[GST_PADDING];
};


#define GST_TYPE_PLUGIN   (gst_plugin_get_type())
#define GST_IS_PLUGIN(obj)             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_PLUGIN))
#define GST_IS_PLUGIN_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_PLUGIN))
#define GST_PLUGIN_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_PLUGIN, GstPluginClass))
#define GST_PLUGIN(obj)                (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_PLUGIN, GstPlugin))
#define GST_PLUGIN_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_PLUGIN, GstPluginClass))


struct _GstPlugin {
  GstObject       object;

  GstPluginDesc	desc;

  GstPluginDesc *orig_desc;

  unsigned int  flags;

  gchar *	filename;
  GList *	features;	/* list of features provided */
  gint		numfeatures;

  GModule *	module;		/* contains the module if plugin is loaded */

  size_t        file_size;
  time_t        file_mtime;

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstPluginClass {
  GstObjectClass  object_class;

};


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

#define GST_LICENSE_UNKNOWN "unknown"


/* function for filters */
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

GList*			gst_plugin_feature_filter	(GstPlugin *plugin,
							 GstPluginFeatureFilter filter,
							 gboolean first,
							 gpointer user_data);
GList*			gst_plugin_list_feature_filter	(GList *list,
							 GstPluginFeatureFilter filter,
							 gboolean first,
							 gpointer user_data);
void gst_plugin_list_free (GList *list);
gboolean		gst_plugin_name_filter		(GstPlugin *plugin, const gchar *name);

GList*			gst_plugin_get_feature_list	(GstPlugin *plugin);
GstPluginFeature*	gst_plugin_find_feature		(GstPlugin *plugin, const gchar *name, GType type);
GstPluginFeature*	gst_plugin_find_feature_by_name	(GstPlugin *plugin, const gchar *name);

gboolean		gst_plugin_check_file		(const gchar *filename, GError** error);
GstPlugin *		gst_plugin_load_file		(const gchar *filename, GError** error);

void			gst_plugin_add_feature		(GstPlugin *plugin, GstPluginFeature *feature);

GstPlugin *             gst_plugin_load                 (GstPlugin *plugin);

/* shortcuts to load from the registry pool */
gboolean		gst_plugin_load_1		(const gchar *name);

G_END_DECLS

#endif /* __GST_PLUGIN_H__ */
