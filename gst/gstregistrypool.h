/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstregistrypool.h: maintain list of registries and plugins
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


#ifndef __GST_REGISTRY_POOL_H__
#define __GST_REGISTRY_POOL_H__

G_BEGIN_DECLS
#include <gst/gstplugin.h>
#include <gst/gstregistry.h>
/* the pool of registries */
    GList * gst_registry_pool_list (void);

void gst_registry_pool_add (GstRegistry * registry, guint priority);
void gst_registry_pool_remove (GstRegistry * registry);

void gst_registry_pool_add_plugin (GstPlugin * plugin);

void gst_registry_pool_load_all (void);

/* query the plugins/features */
GList *gst_registry_pool_plugin_filter (GstPluginFilter filter,
    gboolean first, gpointer user_data);
GList *gst_registry_pool_feature_filter (GstPluginFeatureFilter filter,
    gboolean first, gpointer user_data);

/* some predefined filters */
GList *gst_registry_pool_plugin_list (void);
GList *gst_registry_pool_feature_list (GType type);

GstPlugin *gst_registry_pool_find_plugin (const gchar * name);
GstPluginFeature *gst_registry_pool_find_feature (const gchar * name,
    GType type);

GstRegistry *gst_registry_pool_get_prefered (GstRegistryFlags flags);

G_END_DECLS
#endif /* __GST_REGISTRY_POOL_H__ */
