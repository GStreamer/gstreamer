/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstpluginfeature.c: Abstract base class for all plugin features
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

#include "gst_private.h"
#include "gstpluginfeature.h"
#include "gstplugin.h"
#include "gstregistry.h"

static void		gst_plugin_feature_class_init		(GstPluginFeatureClass *klass);
static void		gst_plugin_feature_init			(GstPluginFeature *feature);

static GstObjectClass *parent_class = NULL;
/* static guint gst_plugin_feature_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_plugin_feature_get_type (void)
{
  static GType plugin_feature_type = 0;

  if (!plugin_feature_type) {
    static const GTypeInfo plugin_feature_info = {
      sizeof (GObjectClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_plugin_feature_class_init,
      NULL,
      NULL,
      sizeof (GObject),
      32,
      (GInstanceInitFunc) gst_plugin_feature_init,
      NULL
    };
    plugin_feature_type = g_type_register_static (G_TYPE_OBJECT, "GstPluginFeature", 
		    				  &plugin_feature_info, G_TYPE_FLAG_ABSTRACT);
  }
  return plugin_feature_type;
}

static void
gst_plugin_feature_class_init (GstPluginFeatureClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass*) klass;

  parent_class = g_type_class_ref (G_TYPE_OBJECT);
}

static void
gst_plugin_feature_init (GstPluginFeature *feature)
{
  feature->manager = NULL;
}

/**
 * gst_plugin_feature_ensure_loaded:
 * @feature: the plugin feature to check
 *
 * Check if the plugin containing the feature is loaded,
 * if not, the plugin will be loaded.
 *
 * Returns: a boolean indicating the feature is loaded.
 */
gboolean
gst_plugin_feature_ensure_loaded (GstPluginFeature *feature)
{
  GstPlugin *plugin = (GstPlugin *) (feature->manager);

  if (plugin && !gst_plugin_is_loaded (plugin)) {
    if (GST_IS_REGISTRY (plugin->manager)) {
      GST_DEBUG (GST_CAT_PLUGIN_LOADING, "loading plugin %s for feature", plugin->name);

      if (gst_registry_load_plugin (GST_REGISTRY (plugin->manager), plugin) != GST_REGISTRY_OK)
	return FALSE;
    }
    else
      return FALSE;
  }
  return TRUE;
}

/**
 * gst_plugin_feature_unload_thyself:
 * @feature: the plugin feature to check
 *
 * Unload the given feature. This will decrease the refcount
 * in the plugin and will eventually unload the plugin
 */
void
gst_plugin_feature_unload_thyself (GstPluginFeature *feature)
{
  GstPluginFeatureClass *oclass; 

  g_return_if_fail (feature != NULL);
  g_return_if_fail (GST_IS_PLUGIN_FEATURE (feature));
  
  oclass = (GstPluginFeatureClass *)G_OBJECT_GET_CLASS (feature);

  if (oclass->unload_thyself)
    oclass->unload_thyself (feature);
}



