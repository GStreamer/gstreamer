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

typedef struct _GstPlugin		GstPlugin;
typedef struct _GstPluginDesc		GstPluginDesc;

struct _GstPlugin {
  gchar *name;			/* name of the plugin */
  gchar *longname;		/* long name of plugin */
  gchar *filename;		/* filename it came from */

  GList *features;		/* list of features provided */
  gint numfeatures;

  GModule *module;		/* contains the module if the plugin is loaded */
};

/* Initialiser function: returns TRUE if plugin initialised successfully */
typedef gboolean (*GstPluginInitFunc) (GModule *module, GstPlugin *plugin);

struct _GstPluginDesc {
  gint major_version; /* major version of core that plugin was compiled for */
  gint minor_version; /* minor version of core that plugin was compiled for */
  gchar *name;        /* name of plugin */
  GstPluginInitFunc plugin_init; /* pointer to plugin_init function */
};

#ifndef GST_PLUGIN_STATIC				
#define GST_PLUGIN_DESC_DYNAMIC(major,minor,name,init)	\
GstPluginDesc plugin_desc = {				\
  major,						\
  minor,						\
  name,							\
  init							\
};							
#else
#define GST_PLUGIN_DESC_DYNAMIC(major,minor,name,init)
#endif
#define GST_PLUGIN_DESC_STATIC(major,minor,name,init)	\
static void __attribute__ ((constructor))		\
_gst_plugin_static_init__ ##init (void)				\
{							\
  static GstPluginDesc plugin_desc_ = {			\
    major,						\
    minor,						\
    name,						\
    init						\
  };							\
  _gst_plugin_register_static (&plugin_desc_);		\
}			

#define GST_PLUGIN_DESC(major,minor,name,init)		\
  GST_PLUGIN_DESC_DYNAMIC (major,minor,name,init)	\
  GST_PLUGIN_DESC_STATIC (major,minor,name,init)	

void			_gst_plugin_initialize		(void);
void 			_gst_plugin_register_static 	(GstPluginDesc *desc);

void			gst_plugin_add_path		(const gchar *path);

const gchar*		gst_plugin_get_name		(GstPlugin *plugin);
void			gst_plugin_set_name		(GstPlugin *plugin, const gchar *name);
const gchar*		gst_plugin_get_longname		(GstPlugin *plugin);
void			gst_plugin_set_longname		(GstPlugin *plugin, const gchar *longname);

const gchar*		gst_plugin_get_filename		(GstPlugin *plugin);
gboolean		gst_plugin_is_loaded		(GstPlugin *plugin);

GList*			gst_plugin_get_feature_list	(GstPlugin *plugin);

void 			gst_plugin_load_all		(void);
void 			gst_plugin_unload_all 		(void);

gboolean 		gst_plugin_load			(const gchar *name);
gboolean 		gst_plugin_load_absolute	(const gchar *name);
gboolean 		gst_library_load		(const gchar *name);
gboolean 		gst_plugin_load_plugin		(GstPlugin *plugin);

void			gst_plugin_add_feature		(GstPlugin *plugin, GstPluginFeature *feature);

GstPlugin*		gst_plugin_find			(const gchar *name);
const GList*		gst_plugin_get_list		(void);


#ifndef GST_DISABLE_REGISTRY
xmlNodePtr		gst_plugin_save_thyself		(xmlNodePtr parent);
void			gst_plugin_load_thyself		(xmlNodePtr parent);
#else
#pragma GCC poison gst_plugin_save_thyself
#pragma GCC poison gst_plugin_load_thyself
#endif

#endif /* __GST_PLUGIN_H__ */
