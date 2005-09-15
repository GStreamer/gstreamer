/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstregistry.h: Header for registry handling
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


#ifndef __GST_REGISTRY_H__
#define __GST_REGISTRY_H__

#include <gst/gstplugin.h>

G_BEGIN_DECLS

#define GST_TYPE_REGISTRY 		(gst_registry_get_type ())
#define GST_REGISTRY(obj) 		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_REGISTRY, GstRegistry))
#define GST_IS_REGISTRY(obj) 		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_REGISTRY))
#define GST_REGISTRY_CLASS(klass) 	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_REGISTRY, GstRegistryClass))
#define GST_IS_REGISTRY_CLASS(klass) 	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_REGISTRY))
#define GST_REGISTRY_GET_CLASS(obj) 	(G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_REGISTRY, GstRegistryClass))

typedef struct _GstRegistry GstRegistry;
typedef struct _GstRegistryClass GstRegistryClass;

struct _GstRegistry {
  GstObject 	 object;

  GList		*plugins;

  GList 	*paths;

  /* FIXME move elsewhere */
  FILE          *cache_file;

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstRegistryClass {
  GstObjectClass	parent_class;

  /* signals */
  void 			(*plugin_added)		(GstRegistry *registry, GstPlugin *plugin);

  gpointer _gst_reserved[GST_PADDING];
};


/* normal GObject stuff */
GType			gst_registry_get_type		(void);

GstRegistry *           gst_registry_get_default        (void);

void			gst_registry_scan_path		(GstRegistry *registry, const gchar *path);
GList*			gst_registry_get_path_list	(GstRegistry *registry);

gboolean		gst_registry_add_plugin		(GstRegistry *registry, GstPlugin *plugin);
void			gst_registry_remove_plugin	(GstRegistry *registry, GstPlugin *plugin);

GList*                  gst_registry_get_plugin_list    (GstRegistry *registry);
GList*			gst_registry_plugin_filter	(GstRegistry *registry, 
							 GstPluginFilter filter, 
							 gboolean first, 
							 gpointer user_data);
GList*			gst_registry_feature_filter	(GstRegistry *registry, 
							 GstPluginFeatureFilter filter, 
							 gboolean first,
							 gpointer user_data);
GList *                 gst_registry_get_feature_list   (GstRegistry *registry,
                                                         GType type);

GstPlugin*		gst_registry_find_plugin	(GstRegistry *registry, const gchar *name);
GstPluginFeature*	gst_registry_find_feature	(GstRegistry *registry, const gchar *name, GType type);
GstPlugin * gst_registry_lookup (GstRegistry *registry, const char *filename);

gboolean gst_registry_xml_read_cache (GstRegistry * registry, const char *location);
gboolean gst_registry_xml_write_cache (GstRegistry * registry, const char *location);

void gst_registry_scan_paths (GstRegistry *registry);
void _gst_registry_remove_cache_plugins (GstRegistry *registry);

#define gst_default_registry_add_plugin(plugin) \
  gst_registry_add_plugin (gst_registry_get_default(), plugin)
#define gst_default_registry_add_path(path) \
  gst_registry_add_path (gst_registry_get_default(), path)
#define gst_default_registry_get_path_list() \
  gst_registry_get_path_list (gst_registry_get_default())
#define gst_default_registry_get_plugin_list() \
  gst_registry_get_plugin_list (gst_registry_get_default())
#define gst_default_registry_find_feature(name,type) \
  gst_registry_find_feature (gst_registry_get_default(),name,type)
#define gst_default_registry_find_plugin(name) \
  gst_registry_find_plugin (gst_registry_get_default(),name)
#define gst_default_registry_feature_filter(filter,first,user_data) \
  gst_registry_feature_filter (gst_registry_get_default(),filter,first,user_data)

G_END_DECLS

#endif /* __GST_REGISTRY_H__ */

