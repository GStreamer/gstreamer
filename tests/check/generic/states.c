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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/check/gstcheck.h>

static GList *elements = NULL;

static void
setup (void)
{
  GList *features, *f;
  GList *plugins, *p;
  gchar **ignorelist = NULL;
  const gchar *STATE_IGNORE_ELEMENTS = NULL;

  GST_DEBUG ("getting elements for package %s", PACKAGE);
  STATE_IGNORE_ELEMENTS = g_getenv ("GST_STATE_IGNORE_ELEMENTS");
  fail_unless (STATE_IGNORE_ELEMENTS != NULL, "Test environment not set up!");
  if (!g_getenv ("GST_NO_STATE_IGNORE_ELEMENTS")) {
    GST_DEBUG ("Will ignore element factories: '%s'", STATE_IGNORE_ELEMENTS);
    ignorelist = g_strsplit (STATE_IGNORE_ELEMENTS, " ", 0);
  }

  plugins = gst_registry_get_plugin_list (gst_registry_get ());

  for (p = plugins; p; p = p->next) {
    GstPlugin *plugin = p->data;

    if (strcmp (gst_plugin_get_source (plugin), PACKAGE) != 0)
      continue;

    features =
        gst_registry_get_feature_list_by_plugin (gst_registry_get (),
        gst_plugin_get_name (plugin));

    for (f = features; f; f = f->next) {
      GstPluginFeature *feature = f->data;
      const gchar *name;
      gboolean ignore = FALSE;

      if (!GST_IS_ELEMENT_FACTORY (feature))
        continue;

      name = GST_OBJECT_NAME (feature);

      if (ignorelist) {
        gchar **s;

        for (s = ignorelist; s && *s; ++s) {
          if (g_str_has_prefix (name, *s)) {
            GST_DEBUG ("ignoring element %s", name);
            ignore = TRUE;
          }
        }
        if (ignore)
          continue;
      }

      GST_DEBUG ("adding element %s", name);
      elements = g_list_prepend (elements, g_strdup (name));
    }
    gst_plugin_feature_list_free (features);
  }
  gst_plugin_list_free (plugins);
  g_strfreev (ignorelist);
}

static void
teardown (void)
{
  GList *e;

  for (e = elements; e; e = e->next) {
    g_free (e->data);
  }
  g_list_free (elements);
  elements = NULL;
}


GST_START_TEST (test_state_changes_up_and_down_seq)
{
  GstElement *element;
  GList *e;

  for (e = elements; e; e = e->next) {
    const gchar *name = e->data;

    GST_DEBUG ("testing element %s", name);
    element = gst_element_factory_make (name, name);
    fail_if (element == NULL, "Could not make element from factory %s", name);

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

GST_END_TEST;

GST_START_TEST (test_state_changes_up_seq)
{
  GstElement *element;
  GList *e;

  for (e = elements; e; e = e->next) {
    const gchar *name = e->data;

    GST_DEBUG ("testing element %s", name);
    element = gst_element_factory_make (name, name);
    fail_if (element == NULL, "Could not make element from factory %s", name);

    if (GST_IS_PIPELINE (element)) {
      GST_DEBUG ("element %s is a pipeline", name);
    }

    gst_element_set_state (element, GST_STATE_READY);

    gst_element_set_state (element, GST_STATE_PAUSED);
    gst_element_set_state (element, GST_STATE_READY);

    gst_element_set_state (element, GST_STATE_PAUSED);
    gst_element_set_state (element, GST_STATE_PLAYING);
    gst_element_set_state (element, GST_STATE_PAUSED);
    gst_element_set_state (element, GST_STATE_READY);

    gst_element_set_state (element, GST_STATE_NULL);
    gst_object_unref (GST_OBJECT (element));
  }
}

GST_END_TEST;

GST_START_TEST (test_state_changes_down_seq)
{
  GstElement *element;
  GList *e;

  for (e = elements; e; e = e->next) {
    const gchar *name = e->data;

    GST_DEBUG ("testing element %s", name);
    element = gst_element_factory_make (name, name);
    fail_if (element == NULL, "Could not make element from factory %s", name);

    if (GST_IS_PIPELINE (element)) {
      GST_DEBUG ("element %s is a pipeline", name);
    }

    gst_element_set_state (element, GST_STATE_READY);
    gst_element_set_state (element, GST_STATE_PAUSED);
    gst_element_set_state (element, GST_STATE_PLAYING);

    gst_element_set_state (element, GST_STATE_PAUSED);
    gst_element_set_state (element, GST_STATE_PLAYING);

    gst_element_set_state (element, GST_STATE_PAUSED);
    gst_element_set_state (element, GST_STATE_READY);
    gst_element_set_state (element, GST_STATE_PAUSED);
    gst_element_set_state (element, GST_STATE_PLAYING);

    gst_element_set_state (element, GST_STATE_PAUSED);
    gst_element_set_state (element, GST_STATE_READY);
    gst_element_set_state (element, GST_STATE_NULL);
    gst_object_unref (GST_OBJECT (element));
  }
}

GST_END_TEST;

static gboolean
element_state_is (GstElement * e, GstState s)
{
  GstStateChangeReturn ret;
  GstState state;

  ret = gst_element_get_state (e, &state, NULL, GST_CLOCK_TIME_NONE);
  return (ret == GST_STATE_CHANGE_SUCCESS && state == s);
}

GST_START_TEST (test_state_changes_up_failure)
{
  GstElement *bin;
  GstElement *mid[3];
  int n;

  /* we want at least one before and one after */
  g_assert (G_N_ELEMENTS (mid) >= 3);

  /* make a bin */
  bin = gst_element_factory_make ("bin", NULL);

  /* add children */
  for (n = 0; n < G_N_ELEMENTS (mid); ++n) {
    const char *element = n != 1 ? "identity" : "fakesink";
    mid[n] = gst_element_factory_make (element, NULL);
    gst_bin_add (GST_BIN (bin), mid[n]);
    if (n == 1)
      g_object_set (mid[n], "async", FALSE, NULL);
  }

  /* This one should work */
  for (n = 0; n < G_N_ELEMENTS (mid); ++n)
    fail_unless (element_state_is (mid[n], GST_STATE_NULL));
  gst_element_set_state (bin, GST_STATE_READY);
  for (n = 0; n < G_N_ELEMENTS (mid); ++n)
    fail_unless (element_state_is (mid[n], GST_STATE_READY));
  gst_element_set_state (bin, GST_STATE_NULL);
  for (n = 0; n < G_N_ELEMENTS (mid); ++n)
    fail_unless (element_state_is (mid[n], GST_STATE_NULL));

  /* make the middle element fail to switch up */
  g_object_set (mid[1], "state-error", 1 /* null-to-ready */ , NULL);

  /* This one should not */
  for (n = 0; n < G_N_ELEMENTS (mid); ++n)
    fail_unless (element_state_is (mid[n], GST_STATE_NULL));
  gst_element_set_state (bin, GST_STATE_READY);
  for (n = 0; n < G_N_ELEMENTS (mid); ++n)
    fail_unless (element_state_is (mid[n], GST_STATE_NULL));
  gst_element_set_state (bin, GST_STATE_NULL);
  for (n = 0; n < G_N_ELEMENTS (mid); ++n)
    fail_unless (element_state_is (mid[n], GST_STATE_NULL));

  /* cleanup */
  gst_object_unref (bin);
}

GST_END_TEST;


static Suite *
states_suite (void)
{
  Suite *s = suite_create ("states_core");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_checked_fixture (tc_chain, setup, teardown);
  tcase_add_test (tc_chain, test_state_changes_up_and_down_seq);
  tcase_add_test (tc_chain, test_state_changes_up_seq);
  tcase_add_test (tc_chain, test_state_changes_down_seq);
  tcase_add_test (tc_chain, test_state_changes_up_failure);

  return s;
}

GST_CHECK_MAIN (states);
