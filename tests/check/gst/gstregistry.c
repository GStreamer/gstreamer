/* GStreamer unit tests for the plugin registry
 *
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <gst/check/gstcheck.h>
#include <string.h>

static gint
plugin_name_cmp (GstPlugin * a, GstPlugin * b)
{
  const gchar *name_a = gst_plugin_get_name (a);
  const gchar *name_b = gst_plugin_get_name (b);

  return strcmp (name_a, name_b);
}

static gint
plugin_ptr_cmp (GstPlugin * a, GstPlugin * b)
{
  return (a == b) ? 0 : 1;
}

static void
print_plugin (const gchar * marker, GstRegistry * registry, GstPlugin * plugin)
{
  const gchar *name;
  GList *features, *f;

  name = gst_plugin_get_name (plugin);

  GST_DEBUG ("%s: plugin %p %d %s file: %s", marker, plugin,
      GST_OBJECT_REFCOUNT (plugin), name,
      GST_STR_NULL (gst_plugin_get_filename (plugin)));

  features = gst_registry_get_feature_list_by_plugin (registry, name);
  for (f = features; f != NULL; f = f->next) {
    GstPluginFeature *feature;

    feature = GST_PLUGIN_FEATURE (f->data);

    GST_LOG ("%s:    feature: %p %s", marker, feature,
        GST_OBJECT_NAME (feature));
  }
  gst_plugin_feature_list_free (features);
}

GST_START_TEST (test_registry_update)
{
  GstPluginFeature *old_identity, *new_identity;
  GstPluginFeature *old_pipeline, *new_pipeline;
  GstRegistry *registry;
  GList *plugins_before, *plugins_after, *l;

  registry = gst_registry_get ();
  fail_unless (registry != NULL);
  ASSERT_OBJECT_REFCOUNT (registry, "default registry", 1);

  /* refcount should still be 1 the second time */
  registry = gst_registry_get ();
  fail_unless (registry != NULL);
  ASSERT_OBJECT_REFCOUNT (registry, "default registry", 1);

  old_identity = gst_registry_lookup_feature (registry, "identity");
  fail_unless (old_identity != NULL, "Can't find plugin feature 'identity'");

  old_pipeline = gst_registry_lookup_feature (registry, "pipeline");
  fail_unless (old_pipeline != NULL, "Can't find plugin feature 'pipeline'");

  /* plugins should have a refcount of 2: the registry holds one reference,
   * and the other one is ours for the list */
  plugins_before = gst_registry_get_plugin_list (registry);
  for (l = plugins_before; l; l = l->next) {
    GstPlugin *plugin;

    plugin = GST_PLUGIN (l->data);

    print_plugin ("before1", registry, plugin);

    ASSERT_OBJECT_REFCOUNT (plugin, "plugin", 2);
  }

  GST_LOG (" ----- calling gst_update_registry -----");

  fail_unless (gst_update_registry () != FALSE, "registry update failed");

  GST_LOG (" ----- registry updated -----");

  /* static plugins should have the same refcount as before (ie. 2), whereas
   * file-based plugins *may* have been replaced by a newly-created object
   * if the on-disk file changed (and was not yet loaded). There should be
   * only one reference left for those, and that's ours */
  for (l = plugins_before; l; l = l->next) {
    GstPlugin *plugin;

    plugin = GST_PLUGIN (l->data);

    print_plugin ("before2", registry, plugin);

    if (gst_plugin_get_filename (plugin)) {
      /* file-based plugin. */
      ASSERT_OBJECT_REFCOUNT_BETWEEN (plugin, "plugin", 1, 2);
    } else {
      /* static plugin */
      ASSERT_OBJECT_REFCOUNT (plugin, "plugin", 2);
    }
  }

  GST_LOG (" -----------------------------------");

  plugins_after = gst_registry_get_plugin_list (registry);
  for (l = plugins_after; l; l = l->next) {
    GstPlugin *plugin = GST_PLUGIN (l->data);

    print_plugin ("after  ", registry, plugin);

    /* file-based plugins should have a refcount of 2 (one for the registry,
     * one for us for the list) or 3 (one for the registry, one for the before
     * list, one for the after list), static plugins should have one of 3
     * (one for the registry, one for the new list and one for the old list).
     * This implicitly also makes sure that all static plugins are the same
     * objects as they were before. Non-static ones may or may not have been
     * replaced by new objects */
    if (gst_plugin_get_filename (plugin)) {
      if (g_list_find_custom (plugins_before, plugin,
              (GCompareFunc) plugin_ptr_cmp) != NULL) {
        /* Same plugin existed in the before list. Refcount must be 3 */
        ASSERT_OBJECT_REFCOUNT (plugin, "plugin", 3);
      } else {
        /* This plugin is newly created, so should only exist in the after list
         * and the registry: Refcount must be 2 */
        ASSERT_OBJECT_REFCOUNT (plugin, "plugin", 2);
      }
    } else {
      ASSERT_OBJECT_REFCOUNT (plugin, "plugin", 3);
    }
  }

  /* check that we still have all plugins in the new list that we had before */
  for (l = plugins_after; l; l = l->next) {
    GstPlugin *plugin;

    plugin = GST_PLUGIN (l->data);

    fail_unless (g_list_find_custom (plugins_before, plugin,
            (GCompareFunc) plugin_name_cmp) != NULL,
        "Plugin %s is in new list but not in old one?!",
        gst_plugin_get_name (plugin));
  }
  for (l = plugins_before; l; l = l->next) {
    GstPlugin *plugin;

    plugin = GST_PLUGIN (l->data);
    fail_unless (g_list_find_custom (plugins_after, plugin,
            (GCompareFunc) plugin_name_cmp) != NULL,
        "Plugin %s is in old list but not in new one?!",
        gst_plugin_get_name (plugin));
  }

  new_identity = gst_registry_lookup_feature (registry, "identity");
  fail_unless (new_identity != NULL, "Can't find plugin feature 'identity'");
  fail_unless (old_identity == new_identity, "Old and new 'identity' feature "
      "objects should be the same, but are different objects");

  /* One ref each for: the registry, old_identity, new_identity */
  ASSERT_OBJECT_REFCOUNT (old_identity, "old identity feature after update", 3);

  new_pipeline = gst_registry_lookup_feature (registry, "pipeline");
  fail_unless (new_pipeline != NULL, "Can't find plugin feature 'pipeline'");
  fail_unless (old_pipeline == new_pipeline, "Old and new 'pipeline' feature "
      "objects should be the same, but are different objects");

  gst_plugin_list_free (plugins_before);
  plugins_before = NULL;
  gst_plugin_list_free (plugins_after);
  plugins_after = NULL;
  registry = NULL;

  gst_object_unref (old_identity);
  gst_object_unref (new_identity);
  gst_object_unref (old_pipeline);
  gst_object_unref (new_pipeline);
}

GST_END_TEST;

static Suite *
registry_suite (void)
{
  Suite *s = suite_create ("registry");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_registry_update);

  return s;
}

GST_CHECK_MAIN (registry);
