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
#include "gstinfo.h"

#include <string.h>

static void gst_plugin_feature_class_init (GstPluginFeatureClass * klass);
static void gst_plugin_feature_init (GstPluginFeature * feature);

static GObjectClass *parent_class = NULL;

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
      0,
      (GInstanceInitFunc) gst_plugin_feature_init,
      NULL
    };

    plugin_feature_type =
        g_type_register_static (G_TYPE_OBJECT, "GstPluginFeature",
        &plugin_feature_info, G_TYPE_FLAG_ABSTRACT);
  }
  return plugin_feature_type;
}

static void
gst_plugin_feature_class_init (GstPluginFeatureClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  parent_class = g_type_class_ref (G_TYPE_OBJECT);
}

static void
gst_plugin_feature_init (GstPluginFeature * feature)
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
gst_plugin_feature_ensure_loaded (GstPluginFeature * feature)
{
  GstPlugin *plugin;

  g_return_val_if_fail (feature != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLUGIN_FEATURE (feature), FALSE);

  plugin = (GstPlugin *) (feature->manager);

  if (plugin && !gst_plugin_is_loaded (plugin)) {
#ifndef GST_DISABLE_REGISTRY
    if (GST_IS_REGISTRY (plugin->manager)) {
      GST_CAT_DEBUG (GST_CAT_PLUGIN_LOADING,
          "loading plugin %s for feature", plugin->desc.name);

      if (gst_registry_load_plugin (GST_REGISTRY (plugin->manager),
              plugin) != GST_REGISTRY_OK)
        return FALSE;
    } else
#endif /* GST_DISABLE_REGISTRY */
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
gst_plugin_feature_unload_thyself (GstPluginFeature * feature)
{
  GstPluginFeatureClass *oclass;

  g_return_if_fail (feature != NULL);
  g_return_if_fail (GST_IS_PLUGIN_FEATURE (feature));

  oclass = GST_PLUGIN_FEATURE_GET_CLASS (feature);

  if (oclass->unload_thyself)
    oclass->unload_thyself (feature);
}

gboolean
gst_plugin_feature_type_name_filter (GstPluginFeature * feature,
    GstTypeNameData * data)
{
  return ((data->type == 0 || data->type == G_OBJECT_TYPE (feature)) &&
      (data->name == NULL
          || !strcmp (data->name, GST_PLUGIN_FEATURE_NAME (feature))));
}

/**
 * gst_plugin_feature_set_rank:
 * @feature: feature to rank
 * @rank: rank value - higher number means more priority rank
 *
 * Specifies a rank for a plugin feature, so that autoplugging uses
 * the most appropriate feature.
 */
void
gst_plugin_feature_set_rank (GstPluginFeature * feature, guint rank)
{
  g_return_if_fail (feature != NULL);
  g_return_if_fail (GST_IS_PLUGIN_FEATURE (feature));

  feature->rank = rank;
}

/**
 * gst_plugin_feature_set_name:
 * @feature: a feature
 * @name: the name to set
 *
 * Sets the name of a plugin feature. The name uniquely identifies a feature
 * within all features of the same type. Renaming a plugin feature is not 
 * allowed.
 */
void
gst_plugin_feature_set_name (GstPluginFeature * feature, const gchar * name)
{
  g_return_if_fail (GST_IS_PLUGIN_FEATURE (feature));
  g_return_if_fail (name != NULL);

  if (feature->name) {
    g_return_if_fail (strcmp (feature->name, name) == 0);
  } else {
    feature->name = g_strdup (name);
  }
}

/**
 * gst_plugin_feature_get rank:
 * @feature: a feature
 *
 * Gets the rank of a plugin feature.
 *
 * Returns: The rank of the feature
 */
guint
gst_plugin_feature_get_rank (GstPluginFeature * feature)
{
  g_return_val_if_fail (GST_IS_PLUGIN_FEATURE (feature), GST_RANK_NONE);

  return feature->rank;
}

/**
 * gst_plugin_feature_get_name:
 * @feature: a feature
 *
 * Gets the name of a plugin feature.
 *
 * Returns: the name
 */
G_CONST_RETURN gchar *
gst_plugin_feature_get_name (GstPluginFeature * feature)
{
  g_return_val_if_fail (GST_IS_PLUGIN_FEATURE (feature), NULL);

  return feature->name;
}
