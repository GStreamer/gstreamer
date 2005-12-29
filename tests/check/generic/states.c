/* GStreamer
 *
 * unit test for state changes on all elements
 *
 * Copyright (C) <2005> Thomas Vander Stichele <thomas at apestaart dot org>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <unistd.h>

#include <gst/check/gstcheck.h>

GST_START_TEST (test_state_changes)
{
  GstElement *element;
  GList *features, *f;
  GList *plugins, *p;

  GST_DEBUG ("testing elements from source %s", PACKAGE);

  plugins = gst_registry_get_plugin_list (gst_registry_get_default ());

  for (p = plugins; p; p = p->next) {
    GstPlugin *plugin = p->data;

    if (strcmp (gst_plugin_get_source (plugin), PACKAGE) != 0)
      continue;

    features =
        gst_registry_get_feature_list_by_plugin (gst_registry_get_default (),
        gst_plugin_get_name (plugin));

    for (f = features; f; f = f->next) {
      GstPluginFeature *feature = f->data;
      const gchar *name = gst_plugin_feature_get_name (feature);

      if (!GST_IS_ELEMENT_FACTORY (feature))
        continue;

      if (g_str_has_prefix (name, "cdparanoia") ||
          g_str_has_prefix (name, "cdio")) {
        continue;
      }

      GST_DEBUG ("testing element %s", name);
      element = gst_element_factory_make (name, name);
      if (GST_IS_PIPELINE (element)) {
        GST_DEBUG ("element %s is a pipeline", name);
      }

      gst_element_set_state (element, GST_STATE_READY);
      gst_element_set_state (element, GST_STATE_PAUSED);
      gst_element_set_state (element, GST_STATE_PLAYING);
      gst_element_set_state (element, GST_STATE_PAUSED);
      gst_element_set_state (element, GST_STATE_READY);
      gst_element_set_state (element, GST_STATE_NULL);
      gst_element_set_state (element, GST_STATE_PAUSED);
      gst_element_set_state (element, GST_STATE_READY);
      gst_element_set_state (element, GST_STATE_PLAYING);
      gst_element_set_state (element, GST_STATE_PAUSED);
      gst_element_set_state (element, GST_STATE_NULL);
      gst_object_unref (GST_OBJECT (element));
    }
  }
}

GST_END_TEST;

Suite *
states_suite (void)
{
  Suite *s = suite_create ("states");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_state_changes);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = states_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
