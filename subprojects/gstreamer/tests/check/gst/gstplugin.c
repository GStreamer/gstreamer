/* GStreamer
 *
 * unit test for GstPlugin
 *
 * Copyright 2004 Thomas Vander Stichele <thomas at apestaart dot org>
 * Copyright 2005 David Schleef <ds@schleef.org>
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
#include "config.h"
#endif

#include <gst/check/gstcheck.h>


static gboolean
register_check_elements (GstPlugin * plugin)
{
  return TRUE;
}

GST_START_TEST (test_register_static)
{
  GstPlugin *plugin;

  fail_unless (gst_plugin_register_static (GST_VERSION_MAJOR,
          GST_VERSION_MINOR, "more-elements", "more-elements",
          register_check_elements, VERSION, GST_LICENSE, PACKAGE,
          GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN));

  plugin = g_object_new (GST_TYPE_PLUGIN, NULL);

  gst_object_unref (plugin);
}

GST_END_TEST;

GST_START_TEST (test_registry)
{
  GList *list, *g;
  GstRegistry *registry;

  registry = gst_registry_get ();

  list = gst_registry_get_plugin_list (registry);
  for (g = list; g; g = g->next) {
    GstPlugin *plugin = GST_PLUGIN (g->data);

    /* one for the registry, one for the list */
    GST_DEBUG ("Plugin refcount %d %s", GST_OBJECT_REFCOUNT_VALUE (plugin),
        gst_plugin_get_name (plugin));
    ASSERT_OBJECT_REFCOUNT (plugin, "plugin in registry", 2);

    gst_object_unref (plugin);
  }
  g_list_free (list);

  list = gst_registry_feature_filter (registry, NULL, FALSE, NULL);
  for (g = list; g; g = g->next) {
    GstPluginFeature *feature = GST_PLUGIN_FEATURE (g->data);

    /* one for the registry, one for the list */
    GST_DEBUG ("Feature refcount %d %s", GST_OBJECT_REFCOUNT_VALUE (feature),
        GST_OBJECT_NAME (feature));
    gst_object_unref (feature);
  }
  g_list_free (list);
}

GST_END_TEST;

GST_START_TEST (test_load_coreelements)
{
  GstPlugin *unloaded_plugin;
  GstPlugin *loaded_plugin;

  unloaded_plugin = gst_registry_find_plugin (gst_registry_get (),
      "coreelements");
  fail_if (unloaded_plugin == NULL, "Failed to find coreelements plugin");
  fail_if (GST_OBJECT_REFCOUNT_VALUE (unloaded_plugin) != 2,
      "Refcount of unloaded plugin in registry initially should be 2");
  GST_DEBUG ("refcount %d", GST_OBJECT_REFCOUNT_VALUE (unloaded_plugin));

  loaded_plugin = gst_plugin_load (unloaded_plugin);
  fail_if (loaded_plugin == NULL, "Failed to load plugin");

  if (loaded_plugin != unloaded_plugin) {
    fail_if (GST_OBJECT_REFCOUNT_VALUE (loaded_plugin) != 2,
        "Refcount of loaded plugin in registry should be 2");
    GST_DEBUG ("refcount %d", GST_OBJECT_REFCOUNT_VALUE (loaded_plugin));
    fail_if (GST_OBJECT_REFCOUNT_VALUE (unloaded_plugin) != 1,
        "Refcount of replaced plugin should be 1");
    GST_DEBUG ("refcount %d", GST_OBJECT_REFCOUNT_VALUE (unloaded_plugin));
  }

  gst_object_unref (unloaded_plugin);
  gst_object_unref (loaded_plugin);
}

GST_END_TEST;

GST_START_TEST (test_registry_get_plugin_list)
{
  GList *list;
  GstPlugin *plugin;

  plugin = gst_registry_find_plugin (gst_registry_get (), "coreelements");
  fail_if (GST_OBJECT_REFCOUNT_VALUE (plugin) != 2,
      "Refcount of plugin in registry should be 2");

  list = gst_registry_get_plugin_list (gst_registry_get ());

  fail_if (GST_OBJECT_REFCOUNT_VALUE (plugin) != 3,
      "Refcount of plugin in registry+list should be 3");

  gst_plugin_list_free (list);

  fail_if (GST_OBJECT_REFCOUNT_VALUE (plugin) != 2,
      "Refcount of plugin in after list free should be 2");

  gst_object_unref (plugin);
}

GST_END_TEST;

GST_START_TEST (test_find_plugin)
{
  GstPlugin *plugin;

  plugin = gst_registry_find_plugin (gst_registry_get (), "coreelements");
  fail_if (plugin == NULL, "Failed to find coreelements plugin");
  ASSERT_OBJECT_REFCOUNT (plugin, "plugin", 2);

  fail_unless_equals_string (gst_plugin_get_version (plugin), VERSION);
  fail_unless_equals_string (gst_plugin_get_license (plugin), "LGPL");
  fail_unless_equals_string (gst_plugin_get_source (plugin), "gstreamer");
  fail_unless_equals_string (gst_plugin_get_package (plugin), GST_PACKAGE_NAME);
  fail_unless_equals_string (gst_plugin_get_origin (plugin),
      GST_PACKAGE_ORIGIN);

  gst_object_unref (plugin);
}

GST_END_TEST;


GST_START_TEST (test_find_feature)
{
  GstPluginFeature *feature;
  GstPlugin *plugin;

  feature = gst_registry_find_feature (gst_registry_get (),
      "identity", GST_TYPE_ELEMENT_FACTORY);
  fail_if (feature == NULL, "Failed to find identity element factory");

  plugin = gst_plugin_feature_get_plugin (feature);
  fail_unless (plugin != NULL);
  fail_unless_equals_string (gst_plugin_get_name (plugin), "coreelements");
  gst_object_unref (plugin);

  fail_if (GST_OBJECT_REFCOUNT_VALUE (feature) != 2,
      "Refcount of feature should be 2");
  GST_DEBUG ("refcount %d", GST_OBJECT_REFCOUNT_VALUE (feature));

  gst_object_unref (feature);
}

GST_END_TEST;

GST_START_TEST (test_find_element)
{
  GstElementFactory *element_factory;

  element_factory = gst_element_factory_find ("identity");
  fail_if (element_factory == NULL, "Failed to find identity element factory");

  fail_if (GST_OBJECT_REFCOUNT_VALUE (element_factory) != 2,
      "Refcount of plugin in registry+feature should be 2");

  gst_object_unref (element_factory);
}

GST_END_TEST;

#if 0
guint8 *
peek (gpointer data, gint64 offset, guint size)
{
  return NULL;
}

void
suggest (gpointer data, guint probability, const GstCaps * caps)
{

}

GST_START_TEST (test_typefind)
{
  GstPlugin *plugin;
  GstPluginFeature *feature;
  GstTypeFind typefind = {
    peek,
    suggest,
    NULL,
    NULL,
    GST_PADDING_INIT
  };

  plugin = gst_default_registry_find_plugin ("typefindfunctions");
  fail_if (plugin == NULL, "Failed to find typefind functions");
  fail_if (GST_OBJECT_REFCOUNT_VALUE (plugin) != 2,
      "Refcount of plugin in registry should be 2");
  fail_if (gst_plugin_is_loaded (plugin), "Expected plugin to be unloaded");

  feature = gst_registry_find_feature (gst_registry_get (),
      "audio/x-au", GST_TYPE_TYPE_FIND_FACTORY);
  fail_if (feature == NULL, "Failed to find audio/x-aw typefind factory");
  fail_if (feature->plugin != plugin,
      "Expected identity to be from coreelements plugin");

  fail_if (GST_OBJECT_REFCOUNT_VALUE (plugin) != 3,
      "Refcount of plugin in registry+feature should be 3");

  gst_type_find_factory_call_function (GST_TYPE_FIND_FACTORY (feature),
      &typefind);

  gst_object_unref (feature->plugin);

  fail_if (GST_OBJECT_REFCOUNT_VALUE (plugin) != 1,
      "Refcount of plugin in after list free should be 1");

  gst_object_unref (plugin);
}

GST_END_TEST;
#endif

#define gst_default_registry_check_feature_version(name,a,b,c) \
    gst_registry_check_feature_version(gst_registry_get(),(name),(a),(b),(c))

GST_START_TEST (test_version_checks)
{
  fail_if (gst_default_registry_check_feature_version ("identity",
          GST_VERSION_MAJOR, GST_VERSION_MINOR, GST_VERSION_MICRO) == FALSE,
      "Unexpected version check result");

  fail_if (gst_default_registry_check_feature_version ("identity",
          GST_VERSION_MAJOR + 1, GST_VERSION_MINOR, GST_VERSION_MICRO) == TRUE,
      "Unexpected version check result");

  fail_if (gst_default_registry_check_feature_version ("identity",
          GST_VERSION_MAJOR, GST_VERSION_MINOR + 1, GST_VERSION_MICRO) == TRUE,
      "Unexpected version check result");

  fail_if (gst_default_registry_check_feature_version ("identity",
          GST_VERSION_MAJOR, GST_VERSION_MINOR, GST_VERSION_MICRO + 1) ==
      TRUE, "Unexpected version check result");

  if (GST_VERSION_MAJOR > 0) {
    fail_if (gst_default_registry_check_feature_version ("identity",
            GST_VERSION_MAJOR - 1, GST_VERSION_MINOR,
            GST_VERSION_MICRO) == FALSE, "Unexpected version check result");
  }

  if (GST_VERSION_MINOR > 0) {
    fail_if (gst_default_registry_check_feature_version ("identity",
            GST_VERSION_MAJOR, GST_VERSION_MINOR - 1,
            GST_VERSION_MICRO) == FALSE, "Unexpected version check result");
  }

  if (GST_VERSION_MICRO > 0) {
    fail_if (gst_default_registry_check_feature_version ("identity",
            GST_VERSION_MAJOR, GST_VERSION_MINOR,
            GST_VERSION_MICRO - 1) == FALSE, "Unexpected version check result");
  }

  fail_if (gst_default_registry_check_feature_version ("entityid",
          GST_VERSION_MAJOR, GST_VERSION_MINOR, GST_VERSION_MICRO) == TRUE,
      "Unexpected version check result");
}

GST_END_TEST;

static Suite *
gst_plugin_suite (void)
{
  Suite *s = suite_create ("GstPlugin");
  TCase *tc_chain = tcase_create ("general");

  /* turn off timeout */
  tcase_set_timeout (tc_chain, 60);

  suite_add_tcase (s, tc_chain);
#ifdef GST_GNUC_CONSTRUCTOR_DEFINED
  tcase_add_test (tc_chain, test_old_register_static);
#endif
  tcase_add_test (tc_chain, test_register_static);
  tcase_add_test (tc_chain, test_registry);
  tcase_add_test (tc_chain, test_load_coreelements);
  tcase_add_test (tc_chain, test_registry_get_plugin_list);
  tcase_add_test (tc_chain, test_find_plugin);
  tcase_add_test (tc_chain, test_find_feature);
  tcase_add_test (tc_chain, test_find_element);
  tcase_add_test (tc_chain, test_version_checks);
  //tcase_add_test (tc_chain, test_typefind);

  return s;
}

GST_CHECK_MAIN (gst_plugin);
