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

#include <gmodule.h>
#include <parser.h> // NOTE: this is xml-config's fault

// Include compatability defines: if libxml hasn't already defined these,
// we have an old version 1.x
#ifndef xmlChildrenNode
#define xmlChildrenNode childs
#define xmlRootNode root
#endif

#include <gst/gsttype.h>
#include <gst/gstelement.h>
#include <gst/gstautoplug.h>


typedef struct _GstPlugin		GstPlugin;
typedef struct _GstPluginElement	GstPluginElement;

struct _GstPlugin {
  gchar *name;			/* name of the plugin */
  gchar *longname;		/* long name of plugin */
  gchar *filename;		/* filename it came from */

  GList *types;			/* list of types provided */
  gint numtypes;
  GList *elements;		/* list of elements provided */
  gint numelements;
  GList *autopluggers;		/* list of autopluggers provided */
  gint numautopluggers;

  gboolean loaded;              /* if the plugin is in memory */
};


typedef GstPlugin* (*GstPluginInitFunc) (GModule *module);

void			_gst_plugin_initialize		(void);

GstPlugin*		gst_plugin_new			(const gchar *name);

const gchar*		gst_plugin_get_name		(GstPlugin *plugin);
void			gst_plugin_set_name		(GstPlugin *plugin, const gchar *name);
const gchar*		gst_plugin_get_longname		(GstPlugin *plugin);
void			gst_plugin_set_longname		(GstPlugin *plugin, const gchar *longname);

const gchar*		gst_plugin_get_filename		(GstPlugin *plugin);
gboolean		gst_plugin_is_loaded		(GstPlugin *plugin);

GList*			gst_plugin_get_type_list	(GstPlugin *plugin);
GList*			gst_plugin_get_factory_list	(GstPlugin *plugin);
GList*			gst_plugin_get_autoplug_list	(GstPlugin *plugin);

void			gst_plugin_load_all		(void);
gboolean		gst_plugin_load			(const gchar *name);
gboolean		gst_plugin_load_absolute	(const gchar *name);
gboolean		gst_library_load		(const gchar *name);

void			gst_plugin_add_factory		(GstPlugin *plugin, GstElementFactory *factory);
void			gst_plugin_add_type		(GstPlugin *plugin, GstTypeFactory *factory);
void			gst_plugin_add_autoplugger	(GstPlugin *plugin, GstAutoplugFactory *factory);

GstPlugin*		gst_plugin_find			(const gchar *name);
GList*			gst_plugin_get_list		(void);

GstElementFactory*	gst_plugin_load_elementfactory	(const gchar *name);
void			gst_plugin_load_typefactory	(const gchar *mime);
GstAutoplugFactory*	gst_plugin_load_autoplugfactory	(const gchar *name);

xmlNodePtr		gst_plugin_save_thyself		(xmlNodePtr parent);
void			gst_plugin_load_thyself		(xmlNodePtr parent);

#endif /* __GST_PLUGIN_H__ */
