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

#include <gmodule.h>
#include <gst/gstpluginfeature.h>
#include <gst/gstmacros.h>

G_BEGIN_DECLS

GQuark gst_plugin_error_quark (void);
#define GST_PLUGIN_ERROR gst_plugin_error_quark ()

typedef enum
{
  GST_PLUGIN_ERROR_MODULE,
  GST_PLUGIN_ERROR_DEPENDENCIES,
  GST_PLUGIN_ERROR_NAME_MISMATCH
} GstPluginError;

#define GST_PLUGIN(plugin)		((GstPlugin *) (plugin))

typedef struct _GstPlugin		GstPlugin;
typedef struct _GstPluginDesc		GstPluginDesc;

/* Initialiser function: returns TRUE if plugin initialised successfully */
typedef gboolean (*GstPluginInitFunc) (GstPlugin *plugin);
/* exiting function when plugin is unloaded */
typedef void (*GstPluginExitFunc) (GstPlugin *plugin);

struct _GstPluginDesc {
  gint major_version;			/* major version of core that plugin was compiled for */
  gint minor_version;			/* minor version of core that plugin was compiled for */
  gchar *name;				/* unique name of plugin */
  gchar *description;			/* description of plugin */
  GstPluginInitFunc plugin_init;	/* pointer to plugin_init function */
  GstPluginExitFunc plugin_exit;	/* pointer to exiting function */
  gchar *version;			/* version of the plugin */
  gchar *license;			/* effective license of plugin */
  gchar *package;			/* package plugin belongs to */
  gchar *origin;			/* URL to provider of plugin */
  
  GST_STRUCT_PADDING
};

struct _GstPlugin {
  GstPluginDesc	desc;

  gchar *	filename;
  GList *	features;		/* list of features provided */
  gint 		numfeatures;

  gpointer 	manager;		/* managing registry */
  GModule *	module;			/* contains the module if the plugin is loaded */

  GST_STRUCT_PADDING
};

#ifndef GST_PLUGIN_STATIC				
#define GST_PLUGIN_DEFINE_DYNAMIC(major,minor,name,description,init,version,license,package,origin)	\
GstPluginDesc gst_plugin_desc = {		      	\
  major,						\
  minor,						\
  name,							\
  description,	      					\
  init,							\
  NULL,							\
  version,						\
  license,					      	\
  package,						\
  origin,						\
  GST_STRUCT_PADDING_INIT				\
};							
#define GST_PLUGIN_DEFINE_STATIC(major,minor,name,description,init,version,license,package,origin)
#else
#define GST_PLUGIN_DEFINE_DYNAMIC(major,minor,name,description,init,version,license,package,origin)
#define GST_PLUGIN_DEFINE_STATIC(major,minor,name,description,init,version,license,package,origin)  \
static void GST_GNUC_CONSTRUCTOR			\
_gst_plugin_static_init__ ##init (void)			\
{							\
  static GstPluginDesc plugin_desc_ = {			\
    major,						\
    minor,						\
    name,		      				\
    description,	      	      			\
    init,				      		\
    NULL,					      	\
    version,						\
    license,					      	\
    package,						\
    origin,						\
    GST_STRUCT_PADDING_INIT				\
  };							\
  _gst_plugin_register_static (&plugin_desc_);		\
}			
#endif

#define GST_PLUGIN_DEFINE(major,minor,name,description,init,version,license,package,origin)\
  GST_PLUGIN_DEFINE_STATIC(major,minor,name,description,init,version,license,package,origin)\
  GST_PLUGIN_DEFINE_DYNAMIC(major,minor,name,description,init,version,license,package,origin)
  
#define GST_LICENSE_UNKNOWN "unknown"


/* function for filters */
typedef gboolean        (*GstPluginFilter)              (GstPlugin *plugin,
                                                         gpointer user_data);

#define GST_TYPE_PLUGIN   (gst_plugin_get_type())
GType                   gst_plugin_get_type             (void);
void			_gst_plugin_initialize		(void);
void 			_gst_plugin_register_static 	(GstPluginDesc *desc);

G_CONST_RETURN gchar*	gst_plugin_get_name		(GstPlugin *plugin);
void			gst_plugin_set_name		(GstPlugin *plugin, const gchar *name);
G_CONST_RETURN gchar*	gst_plugin_get_longname		(GstPlugin *plugin);
G_CONST_RETURN gchar*	gst_plugin_get_filename		(GstPlugin *plugin);
G_CONST_RETURN gchar*	gst_plugin_get_license		(GstPlugin *plugin);
G_CONST_RETURN gchar*	gst_plugin_get_package		(GstPlugin *plugin);
G_CONST_RETURN gchar*	gst_plugin_get_origin      	(GstPlugin *plugin);
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
gboolean		gst_plugin_name_filter		(GstPlugin *plugin, const gchar *name);

GList*			gst_plugin_get_feature_list	(GstPlugin *plugin);
GstPluginFeature*	gst_plugin_find_feature		(GstPlugin *plugin, const gchar *name, GType type);

GstPlugin * 		gst_plugin_load_file		(const gchar *filename, GError** error);
gboolean 		gst_plugin_unload_plugin	(GstPlugin *plugin);

void			gst_plugin_add_feature		(GstPlugin *plugin, GstPluginFeature *feature);

/* shortcuts to load from the registry pool */
gboolean 		gst_plugin_load			(const gchar *name);
gboolean 		gst_library_load		(const gchar *name);

G_END_DECLS

#endif /* __GST_PLUGIN_H__ */
