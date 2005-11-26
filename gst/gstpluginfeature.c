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

/**
 * SECTION:gstpluginfeature
 * @short_description: Base class for contents of a GstPlugin
 * @see_also: #GstPlugin
 *
 * This is a base class for anything that can be added to a #GstPlugin.
 */

#include "gst_private.h"

#include "gstpluginfeature.h"
#include "gstplugin.h"
#include "gstregistry.h"
#include "gstinfo.h"

#include <string.h>

#define GST_CAT_DEFAULT GST_CAT_PLUGIN_LOADING

static void gst_plugin_feature_class_init (GstPluginFeatureClass * klass);
static void gst_plugin_feature_init (GstPluginFeature * feature);
static void gst_plugin_feature_finalize (GObject * object);

/* static guint gst_plugin_feature_signals[LAST_SIGNAL] = { 0 }; */

G_DEFINE_ABSTRACT_TYPE (GstPluginFeature, gst_plugin_feature, GST_TYPE_OBJECT);
static GstObjectClass *parent_class = NULL;

static void
gst_plugin_feature_class_init (GstPluginFeatureClass * klass)
{
  parent_class = g_type_class_ref (GST_TYPE_OBJECT);

  G_OBJECT_CLASS (klass)->finalize =
      GST_DEBUG_FUNCPTR (gst_plugin_feature_finalize);
}

static void
gst_plugin_feature_init (GstPluginFeature * feature)
{

}

static void
gst_plugin_feature_finalize (GObject * object)
{
  GstPluginFeature *feature = GST_PLUGIN_FEATURE (object);

  GST_DEBUG ("finalizing feature %p", feature);
  g_free (feature->name);
  g_free (feature->plugin_name);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * gst_plugin_feature_load:
 * @feature: the plugin feature to check
 *
 * Loads the plugin containing @feature if it's not already loaded. @feature is
 * unaffected; use the return value instead.
 *
 * Normally this function is used like this:
 *
 * <programlisting>
 * GstPluginFeature *loaded_feature;
 * loaded_feature = gst_plugin_feature_load (feature);
 * // presumably, we're no longer interested in the potentially-unloaded feature
 * gst_object_unref (feature);
 * feature = loaded_feature;
 * </programlisting>
 *
 * Returns: A reference to the loaded feature, or NULL on error.
 */
GstPluginFeature *
gst_plugin_feature_load (GstPluginFeature * feature)
{
  GstPlugin *plugin;
  GstPluginFeature *real_feature;

  g_return_val_if_fail (feature != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLUGIN_FEATURE (feature), FALSE);

  GST_DEBUG ("loading plugin for feature %p", feature);
  if (feature->loaded)
    return feature;

  GST_DEBUG ("loading plugin %s", feature->plugin_name);
  plugin = gst_plugin_load_by_name (feature->plugin_name);
  if (!plugin) {
    GST_WARNING ("Failed to load plugin containing feature '%s'.",
        GST_PLUGIN_FEATURE_NAME (feature));
    return NULL;
  }
  GST_DEBUG ("loaded plugin %s", feature->plugin_name);
  gst_object_unref (plugin);

  real_feature =
      gst_registry_lookup_feature (gst_registry_get_default (), feature->name);

  if (real_feature == NULL) {
    GST_INFO
        ("Loaded plugin containing feature '%s', but feature disappeared.",
        feature->name);
  } else if (!real_feature->loaded) {
    GST_INFO ("Tried to load plugin containing feature '%s', but feature was "
        "not found.", real_feature->name);
    return NULL;
  }

  return real_feature;
}

/**
 * gst_plugin_feature_type_name_filter:
 * @feature: the #GstPluginFeature
 * @data: the type and name to check against
 *
 * Compares type and name of plugin feature. Can be used with gst_filter_run().
 *
 * Returns: TRUE if equal.
 */
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
  gst_object_set_name (GST_OBJECT (feature), feature->name);
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
 * gst_plugin_feature_get_rank:
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
 * gst_plugin_feature_list_free:
 * @list: list of #GstPluginFeature
 *
 * Unrefs each member of @list, then frees the list.
 */
void
gst_plugin_feature_list_free (GList * list)
{
  GList *g;

  for (g = list; g; g = g->next) {
    GstPluginFeature *feature = GST_PLUGIN_FEATURE (g->data);

    gst_object_unref (feature);
  }
  g_list_free (list);
}

/**
 * gst_plugin_feature_check_version:
 * @feature: a feature
 * @min_major: minimum required major version
 * @min_minor: minimum required minor version
 * @min_micro: minimum required micro version
 *
 * Checks whether the given plugin feature is at least
 *  the required version
 *
 * Returns: #TRUE if the plugin feature has at least
 *  the required version, otherwise #FALSE.
 */
gboolean
gst_plugin_feature_check_version (GstPluginFeature * feature,
    guint min_major, guint min_minor, guint min_micro)
{
  GstRegistry *registry;
  GstPlugin *plugin;
  gboolean ret = FALSE;

  g_return_val_if_fail (feature != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PLUGIN_FEATURE (feature), FALSE);

  GST_DEBUG ("Looking up plugin '%s' containing plugin feature '%s'",
      feature->plugin_name, feature->name);

  registry = gst_registry_get_default ();
  plugin = gst_registry_find_plugin (registry, feature->plugin_name);

  if (plugin) {
    const gchar *ver_str;
    guint major, minor, micro;

    ver_str = gst_plugin_get_version (plugin);
    g_return_val_if_fail (ver_str != NULL, FALSE);

    if (sscanf (ver_str, "%u.%u.%u", &major, &minor, &micro) == 3) {
      if (major > min_major)
        ret = TRUE;
      else if (major < min_major)
        ret = FALSE;
      else if (minor > min_minor)
        ret = TRUE;
      else if (minor < min_minor)
        ret = FALSE;
      else if (micro > min_micro)
        ret = TRUE;
      else
        ret = (micro == min_micro);

      GST_DEBUG ("Checking whether %u.%u.%u >= %u.%u.%u? %s", major, minor,
          micro, min_major, min_minor, min_micro, (ret) ? "yes" : "no");
    } else {
      GST_WARNING ("Could not parse version string '%s' of plugin '%s'",
          ver_str, feature->plugin_name);
    }

    gst_object_unref (plugin);
  } else {
    GST_DEBUG ("Could not find plugin '%s'", feature->plugin_name);
  }

  return ret;
}
