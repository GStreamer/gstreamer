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
  feature->plugin = NULL;
}

/**
 * gst_plugin_feature_load:
 * @feature: the plugin feature to check
 *
 * Check if the plugin containing the feature is loaded,
 * if not, the plugin will be loaded.
 *
 * Returns: The new feature
 */
GstPluginFeature *
gst_plugin_feature_load (GstPluginFeature * feature)
{
  GstPlugin *plugin;
  GstPluginFeature *real_feature;

  g_return_val_if_fail (feature != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLUGIN_FEATURE (feature), FALSE);

  plugin = gst_plugin_load (feature->plugin);
  if (!plugin) {
    g_critical ("Failed to load plugin containing feature '%s'.",
        GST_PLUGIN_FEATURE_NAME (feature));
    return NULL;
  }
  if (plugin == feature->plugin) {
    return feature;
  }

  real_feature = gst_plugin_find_feature_by_name (plugin, feature->name);

  if (real_feature == NULL) {
    g_critical
        ("Loaded plugin containing feature '%s', but feature disappeared.",
        feature->name);
    return NULL;
  }

  return real_feature;
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
 * gst_plugin_feature_set_name:
 * @feature: a feature
 * @name: the name to set
 *
 * Sets the name of a plugin feature. The name uniquely identifies a feature
 * within all features of the same type. Renaming a plugin feature is not 
 * allowed. A copy is made of the name so you should free the supplied @name
 * after calling this function.
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
