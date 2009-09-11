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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <gst/check/gstcheck.h>

#ifdef GST_DISABLE_DEPRECATED
void _gst_plugin_register_static (GstPluginDesc * desc);
#endif

/* keep in sync with GST_GNUC_CONSTRUCTOR in gstmacros.h (ideally we'd just
 * do it there, but I don't want to touch that now, and also we really want
 * to deprecate this macro in the long run, I think) */
#if defined (__GNUC__) && (__GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ > 4))
#define GST_GNUC_CONSTRUCTOR_DEFINED
#else
#undef GST_GNUC_CONSTRUCTOR_DEFINED
#endif

#ifdef GST_GNUC_CONSTRUCTOR_DEFINED
/* ------------------------------------------------------------------------- */
/* To make sure the old and deprecated GST_PLUGIN_DEFINE_STATIC still works  */

static guint plugin_init_counter;       /* 0 */

static gboolean
plugin1_init (GstPlugin * plugin)
{
  ++plugin_init_counter;
  return TRUE;
}

static gboolean
plugin2_init (GstPlugin * plugin)
{
  ++plugin_init_counter;
  return TRUE;
}

static gboolean
plugin3_init (GstPlugin * plugin)
{
  ++plugin_init_counter;
  return TRUE;
}

GST_PLUGIN_DEFINE_STATIC (GST_VERSION_MAJOR, GST_VERSION_MINOR, "plugin-1",
    "some static elements 1", plugin1_init, VERSION, GST_LICENSE, PACKAGE,
    GST_PACKAGE_ORIGIN);

GST_PLUGIN_DEFINE_STATIC (GST_VERSION_MAJOR, GST_VERSION_MINOR, "plugin-2",
    "some static elements 2", plugin2_init, VERSION, GST_LICENSE, PACKAGE,
    GST_PACKAGE_ORIGIN);

GST_PLUGIN_DEFINE_STATIC (GST_VERSION_MAJOR, GST_VERSION_MINOR, "plugin-3",
    "some static elements 3", plugin3_init, VERSION, GST_LICENSE, PACKAGE,
    GST_PACKAGE_ORIGIN);

GST_START_TEST (test_old_register_static)
{
  fail_unless (plugin_init_counter == 3);
}

GST_END_TEST;

#endif /* GST_GNUC_CONSTRUCTOR_DEFINED */


static gboolean
register_check_elements (GstPlugin * plugin)
{
  return TRUE;
}

static GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "check elements",
  "check elements",
  register_check_elements,
  VERSION,
  GST_LICENSE,
  PACKAGE,
  GST_PACKAGE_NAME,
  GST_PACKAGE_ORIGIN,

  GST_PADDING_INIT
};

GST_START_TEST (test_register_static)
{
  GstPlugin *plugin;

  _gst_plugin_register_static (&plugin_desc);
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
  GList *g;
  GstRegistry *registry;

  registry = gst_registry_get_default ();

  for (g = registry->plugins; g; g = g->next) {
    GstPlugin *plugin = GST_PLUGIN (g->data);

    ASSERT_OBJECT_REFCOUNT (plugin, "plugin in registry", 1);
    GST_DEBUG ("refcount %d %s", GST_OBJECT_REFCOUNT_VALUE (plugin),
        plugin->desc.name);
  }
  for (g = registry->features; g; g = g->next) {
    GstPluginFeature *feature = GST_PLUGIN_FEATURE (g->data);

    fail_if (GST_OBJECT_REFCOUNT_VALUE (feature) != 1,
        "Feature in registry should have refcount of 1");
    GST_DEBUG ("refcount %d %s", GST_OBJECT_REFCOUNT_VALUE (feature),
        feature->name);
  }
}

GST_END_TEST;

GST_START_TEST (test_load_coreelements)
{
  GstPlugin *unloaded_plugin;
  GstPlugin *loaded_plugin;

  unloaded_plugin = gst_default_registry_find_plugin ("coreelements");
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

  plugin = gst_default_registry_find_plugin ("coreelements");
  fail_if (GST_OBJECT_REFCOUNT_VALUE (plugin) != 2,
      "Refcount of plugin in registry should be 2");

  list = gst_registry_get_plugin_list (gst_registry_get_default ());

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

  plugin = gst_registry_find_plugin (gst_registry_get_default (),
      "coreelements");
  fail_if (plugin == NULL, "Failed to find coreelements plugin");
  ASSERT_OBJECT_REFCOUNT (plugin, "plugin", 2);

  fail_unless_equals_string (plugin->desc.version, VERSION);
  fail_unless_equals_string (plugin->desc.license, "LGPL");
  fail_unless_equals_string (plugin->desc.source, "gstreamer");
  fail_unless_equals_string (plugin->desc.package, GST_PACKAGE_NAME);
  fail_unless_equals_string (plugin->desc.origin, GST_PACKAGE_ORIGIN);

  gst_object_unref (plugin);
}

GST_END_TEST;


GST_START_TEST (test_find_feature)
{
  GstPluginFeature *feature;

  feature = gst_registry_find_feature (gst_registry_get_default (),
      "identity", GST_TYPE_ELEMENT_FACTORY);
  fail_if (feature == NULL, "Failed to find identity element factory");
  fail_if (strcmp (feature->plugin_name, "coreelements"),
      "Expected identity to be from coreelements plugin");

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

  feature = gst_registry_find_feature (gst_registry_get_default (),
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

  /* If the nano is set, then we expect that X.Y.Z-1.x >= X.Y.Z, so that a
   * devel plugin is valid against an upcoming release */
  if (GST_VERSION_NANO > 0) {
    fail_unless (gst_default_registry_check_feature_version ("identity",
            GST_VERSION_MAJOR, GST_VERSION_MINOR, GST_VERSION_MICRO + 1) ==
        TRUE, "Unexpected version check result");
  } else {
    fail_if (gst_default_registry_check_feature_version ("identity",
            GST_VERSION_MAJOR, GST_VERSION_MINOR, GST_VERSION_MICRO + 1) ==
        TRUE, "Unexpected version check result");
  }

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
