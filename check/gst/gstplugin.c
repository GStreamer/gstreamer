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
  GST_PACKAGE,
  GST_ORIGIN,

  GST_PADDING_INIT
};

GST_START_TEST (test_register_static)
{
  GstPlugin *plugin;

  _gst_plugin_register_static (&plugin_desc);

  plugin = g_object_new (GST_TYPE_PLUGIN, NULL);

  gst_object_unref (plugin);
}

GST_END_TEST
GST_START_TEST (test_load_gstelements)
{
  GstPlugin *unloaded_plugin;
  GstPlugin *loaded_plugin;

  unloaded_plugin = gst_default_registry_find_plugin ("gstelements");
  fail_if (unloaded_plugin == NULL, "Failed to find gstelements plugin");
  fail_if (unloaded_plugin->object.refcount != 2,
      "Refcount of unloaded plugin in registry initially should be 2");

  gst_object_ref (unloaded_plugin);
  loaded_plugin = gst_plugin_load (unloaded_plugin);
  fail_if (loaded_plugin == NULL, "Failed to load plugin");

  fail_if (loaded_plugin->object.refcount != 2,
      "Refcount of loaded plugin in registry should be 2");
  fail_if (unloaded_plugin->object.refcount != 1,
      "Refcount of replaced plugin in registry should be 1");

  gst_object_unref (unloaded_plugin);
  gst_object_unref (loaded_plugin);
}

GST_END_TEST
GST_START_TEST (test_registry_get_plugin_list)
{
  GList *list;
  GstPlugin *plugin;

  plugin = gst_default_registry_find_plugin ("gstelements");
  fail_if (plugin->object.refcount != 2,
      "Refcount of plugin in registry should be 2");

  list = gst_registry_get_plugin_list (gst_registry_get_default ());

  fail_if (plugin->object.refcount != 3,
      "Refcount of plugin in registry+list should be 3");

  gst_plugin_list_free (list);

  fail_if (plugin->object.refcount != 2,
      "Refcount of plugin in after list free should be 2");

  gst_object_unref (plugin);
}

GST_END_TEST
GST_START_TEST (test_find_feature)
{
  GstPlugin *plugin;
  GstPluginFeature *feature;

  plugin = gst_default_registry_find_plugin ("gstelements");
  fail_if (plugin->object.refcount != 2,
      "Refcount of plugin in registry should be 2");

  feature = gst_registry_find_feature (gst_registry_get_default (),
      "identity", GST_TYPE_ELEMENT_FACTORY);
  fail_if (feature == NULL, "Failed to find identity element factory");
  fail_if (feature->plugin != plugin,
      "Expected indentity to be from gstelements plugin");

  fail_if (plugin->object.refcount != 3,
      "Refcount of plugin in registry+feature should be 3");

  gst_object_unref (feature->plugin);

  fail_if (plugin->object.refcount != 2,
      "Refcount of plugin in after list free should be 2");

  gst_object_unref (plugin);
}
GST_END_TEST Suite * gst_plugin_suite (void)
{
  Suite *s = suite_create ("GstPlugin");
  TCase *tc_chain = tcase_create ("general");

  /* turn off timeout */
  tcase_set_timeout (tc_chain, 60);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_register_static);
  tcase_add_test (tc_chain, test_load_gstelements);
  tcase_add_test (tc_chain, test_registry_get_plugin_list);
  tcase_add_test (tc_chain, test_find_feature);

  return s;
}


int
main (int argc, char **argv)
{
  int nf;

  Suite *s = gst_plugin_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_VERBOSE);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
