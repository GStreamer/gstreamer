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

#define GLOBAL_REGISTRY_DIR      GST_CONFIG_DIR
#define GLOBAL_REGISTRY_FILE     GLOBAL_REGISTRY_DIR"/registry.xml"
#define GLOBAL_REGISTRY_FILE_TMP GLOBAL_REGISTRY_DIR"/.registry.xml.tmp"

#define LOCAL_REGISTRY_DIR       ".gstreamer"
#define LOCAL_REGISTRY_FILE      LOCAL_REGISTRY_DIR"/registry.xml"
#define LOCAL_REGISTRY_FILE_TMP  LOCAL_REGISTRY_DIR"/.registry.xml.tmp"

#define REGISTRY_DIR_PERMS (S_ISGID | \
                            S_IRUSR | S_IWUSR | S_IXUSR | \
		            S_IRGRP | S_IXGRP | \
			    S_IROTH | S_IXOTH)
#define REGISTRY_TMPFILE_PERMS (S_IRUSR | S_IWUSR)
#define REGISTRY_FILE_PERMS (S_IRUSR | S_IWUSR | \
                             S_IRGRP | S_IWGRP | \
			     S_IROTH | S_IWOTH)

G_BEGIN_DECLS

typedef struct _GstRegistryWrite GstRegistryWrite;
struct _GstRegistryWrite {
  gchar *dir;
  gchar *file;
  gchar *tmp_file;
};

typedef struct _GstRegistryRead GstRegistryRead;
struct _GstRegistryRead {
  gchar *global_reg;
  gchar *local_reg;
};

GstRegistryWrite 	*gst_registry_write_get  	(void);
GstRegistryRead 	*gst_registry_read_get  	(void);
void 			gst_registry_option_set 	(const gchar *registry);


typedef enum {
  GST_REGISTRY_OK			= (0),
  GST_REGISTRY_LOAD_ERROR		= (1 << 1),
  GST_REGISTRY_SAVE_ERROR		= (1 << 2),
  GST_REGISTRY_PLUGIN_LOAD_ERROR	= (1 << 3),
  GST_REGISTRY_PLUGIN_SIGNATURE_ERROR	= (1 << 4),
} GstRegistryReturn;

typedef enum {
  GST_REGISTRY_READABLE			= (1 << 1),
  GST_REGISTRY_WRITABLE			= (1 << 2),
  GST_REGISTRY_REMOTE			= (1 << 3),
  GST_REGISTRY_DELAYED_LOADING		= (1 << 4),
} GstRegistryFlags;

  
#define GST_TYPE_REGISTRY \
  (gst_registry_get_type())
#define GST_REGISTRY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_REGISTRY,GstRegistry))
#define GST_REGISTRY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_REGISTRY,GstRegistryClass))
#define GST_IS_REGISTRY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_REGISTRY))
#define GST_IS_REGISTRY_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_REGISTRY))

typedef struct _GstRegistry GstRegistry;
typedef struct _GstRegistryClass GstRegistryClass;

struct _GstRegistry {
  GObject 	 object;

  gint 		 priority;
  GstRegistryFlags flags;

  gchar 	*name;
  gchar 	*details;

  gboolean	 loaded;
  GList		*plugins;

  GList 	*paths;
};

struct _GstRegistryClass {
  GObjectClass		parent_class;

  /* vtable */
  gboolean		(*load)      		(GstRegistry *registry);
  gboolean 		(*save)      		(GstRegistry *registry);
  gboolean		(*rebuild)     		(GstRegistry *registry);
  gboolean		(*unload)     		(GstRegistry *registry);

  GstRegistryReturn 	(*load_plugin) 		(GstRegistry *registry, GstPlugin *plugin);
  GstRegistryReturn 	(*unload_plugin) 	(GstRegistry *registry, GstPlugin *plugin);
  GstRegistryReturn    	(*update_plugin) 	(GstRegistry *registry, GstPlugin *plugin);

  /* signals */
  void 			(*plugin_added)		(GstRegistry *registry, GstPlugin *plugin);
};


/* normal GObject stuff */
GType			gst_registry_get_type		(void);

gboolean		gst_registry_load		(GstRegistry *registry);
gboolean		gst_registry_is_loaded		(GstRegistry *registry);
gboolean		gst_registry_save		(GstRegistry *registry);
gboolean		gst_registry_rebuild		(GstRegistry *registry);
gboolean		gst_registry_unload		(GstRegistry *registry);

void			gst_registry_add_path		(GstRegistry *registry, const gchar *path);
GList*			gst_registry_get_path_list	(GstRegistry *registry);
void 			gst_registry_clear_paths	(GstRegistry *registry);

gboolean		gst_registry_add_plugin		(GstRegistry *registry, GstPlugin *plugin);
void			gst_registry_remove_plugin	(GstRegistry *registry, GstPlugin *plugin);

GstPlugin*		gst_registry_find_plugin	(GstRegistry *registry, const gchar *name);
GstPluginFeature*	gst_registry_find_feature	(GstRegistry *registry, const gchar *name, GType type);

GstRegistryReturn	gst_registry_load_plugin	(GstRegistry *registry, GstPlugin *plugin);
GstRegistryReturn	gst_registry_unload_plugin	(GstRegistry *registry, GstPlugin *plugin);
GstRegistryReturn	gst_registry_update_plugin	(GstRegistry *registry, GstPlugin *plugin);

/* the pool of registries */
GList*			gst_registry_pool_list		(void);
void			gst_registry_pool_add		(GstRegistry *registry, guint priority);
void			gst_registry_pool_remove	(GstRegistry *registry);

void			gst_registry_pool_add_plugin	(GstPlugin *plugin);

void			gst_registry_pool_add		(GstRegistry *registry, guint priority);
void			gst_registry_pool_load_all	(void);

GList*			gst_registry_pool_plugin_list	(void);
GList*			gst_registry_pool_feature_list	(GType type);

GstPlugin*		gst_registry_pool_find_plugin	(const gchar *name);
GstPluginFeature*	gst_registry_pool_find_feature	(const gchar *name, GType type);

GstRegistry*		gst_registry_pool_get_prefered	(GstRegistryFlags flags);

G_END_DECLS

#endif /* __GST_REGISTRY_H__ */
