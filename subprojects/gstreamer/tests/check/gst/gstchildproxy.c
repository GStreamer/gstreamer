/* GStreamer
 * Copyright (C) 2009 Stefan Kost <ensonic@users.sf.net>
 *
 * gstchildproxy.c: Unit test for GstChildProxy interface
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

GST_START_TEST (test_get)
{
  GstElement *pipeline;
  gchar *name;

  pipeline = gst_pipeline_new ("foo");
  fail_unless (pipeline != NULL, "Could not create pipeline");

  gst_child_proxy_get (GST_CHILD_PROXY (pipeline), "name", &name, NULL);
  fail_if (g_strcmp0 ("foo", name));
  g_free (name);

  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_child_get)
{
  GstElement *pipeline, *elem;
  gchar *name;

  pipeline = gst_pipeline_new (NULL);
  fail_unless (pipeline != NULL, "Could not create pipeline");

  elem = gst_element_factory_make ("fakesrc", "src");
  fail_if (elem == NULL, "Could not create fakesrc");

  gst_bin_add (GST_BIN (pipeline), elem);

  gst_child_proxy_get (GST_CHILD_PROXY (pipeline), "src::name", &name, NULL);
  fail_if (g_strcmp0 ("src", name));
  g_free (name);

  gst_object_unref (pipeline);
}

GST_END_TEST;


static Suite *
gst_child_proxy_suite (void)
{
  Suite *s = suite_create ("GstChildProxy");
  TCase *tc_chain = tcase_create ("child proxy tests");

  tcase_set_timeout (tc_chain, 0);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_get);
  tcase_add_test (tc_chain, test_child_get);

  return s;
}

GST_CHECK_MAIN (gst_child_proxy);
