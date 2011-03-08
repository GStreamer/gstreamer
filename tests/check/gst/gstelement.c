/* GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas at apestaart dot org>
 *
 * gstelement.c: Unit test for GstElement
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

#include <gst/check/gstcheck.h>

GST_START_TEST (test_add_remove_pad)
{
  GstElement *e;
  GstPad *p;

  /* getting an existing element class is cheating, but easier */
  e = gst_element_factory_make ("fakesrc", "source");

  /* create a new floating pad with refcount 1 */
  p = gst_pad_new ("source", GST_PAD_SRC);
  ASSERT_OBJECT_REFCOUNT (p, "pad", 1);
  /* ref it for ourselves */
  gst_object_ref (p);
  ASSERT_OBJECT_REFCOUNT (p, "pad", 2);
  /* adding it sinks the pad -> not floating, same refcount */
  gst_element_add_pad (e, p);
  ASSERT_OBJECT_REFCOUNT (p, "pad", 2);

  /* removing it reduces the refcount */
  gst_element_remove_pad (e, p);
  ASSERT_OBJECT_REFCOUNT (p, "pad", 1);

  /* clean up our own reference */
  gst_object_unref (p);
  gst_object_unref (e);
}

GST_END_TEST;

GST_START_TEST (test_add_pad_unref_element)
{
  GstElement *e;
  GstPad *p;

  /* getting an existing element class is cheating, but easier */
  e = gst_element_factory_make ("fakesrc", "source");

  /* create a new floating pad with refcount 1 */
  p = gst_pad_new ("source", GST_PAD_SRC);
  ASSERT_OBJECT_REFCOUNT (p, "pad", 1);
  /* ref it for ourselves */
  gst_object_ref (p);
  ASSERT_OBJECT_REFCOUNT (p, "pad", 2);
  /* adding it sinks the pad -> not floating, same refcount */
  gst_element_add_pad (e, p);
  ASSERT_OBJECT_REFCOUNT (p, "pad", 2);

  /* unreffing the element should clean it up */
  gst_object_unref (GST_OBJECT (e));

  ASSERT_OBJECT_REFCOUNT (p, "pad", 1);

  /* clean up our own reference */
  gst_object_unref (p);
}

GST_END_TEST;

GST_START_TEST (test_error_no_bus)
{
  GstElement *e;
  GstBus *bus;

  e = gst_element_factory_make ("fakesrc", "source");

  /* get the bus, should be NULL */
  bus = gst_element_get_bus (e);
  fail_if (bus != NULL);

  /* I don't want errors shown */
  gst_debug_set_default_threshold (GST_LEVEL_NONE);

  GST_ELEMENT_ERROR (e, RESOURCE, OPEN_READ, ("I could not read"), ("debug"));

  gst_object_unref (e);
}

GST_END_TEST;

/* link and run two elements without putting them in a pipeline */
GST_START_TEST (test_link)
{
  GstElement *src, *sink;

  src = gst_element_factory_make ("fakesrc", "source");
  sink = gst_element_factory_make ("fakesink", "sink");

  fail_unless (gst_element_link_pads (src, "src", sink, "sink"));

  /* do sink to source state change */
  gst_element_set_state (sink, GST_STATE_PAUSED);
  gst_element_set_state (src, GST_STATE_PAUSED);

  /* wait for preroll */
  gst_element_get_state (sink, NULL, NULL, GST_CLOCK_TIME_NONE);

  /* play some more */
  gst_element_set_state (sink, GST_STATE_PLAYING);
  gst_element_set_state (src, GST_STATE_PLAYING);

  g_usleep (G_USEC_PER_SEC);

  /* and stop */
  gst_element_set_state (sink, GST_STATE_PAUSED);
  gst_element_set_state (src, GST_STATE_PAUSED);

  /* wait for preroll */
  gst_element_get_state (sink, NULL, NULL, GST_CLOCK_TIME_NONE);

  gst_element_set_state (sink, GST_STATE_NULL);
  gst_element_set_state (src, GST_STATE_NULL);

  gst_element_get_state (sink, NULL, NULL, GST_CLOCK_TIME_NONE);
  g_usleep (G_USEC_PER_SEC / 2);

  ASSERT_OBJECT_REFCOUNT (sink, "sink", 1);
  ASSERT_OBJECT_REFCOUNT (src, "src", 1);
  gst_element_unlink_pads (src, "src", sink, "sink");
  ASSERT_OBJECT_REFCOUNT (sink, "sink", 1);
  ASSERT_OBJECT_REFCOUNT (src, "src", 1);

  gst_object_unref (src);
  gst_object_unref (sink);
}

GST_END_TEST;

/* linking two elements without pads should fail */
GST_START_TEST (test_link_no_pads)
{
  GstElement *src, *sink;

  src = gst_bin_new ("src");
  sink = gst_bin_new ("sink");

  fail_if (gst_element_link (src, sink));

  gst_object_unref (src);
  gst_object_unref (sink);
}

GST_END_TEST;


static Suite *
gst_element_suite (void)
{
  Suite *s = suite_create ("GstElement");
  TCase *tc_chain = tcase_create ("element tests");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_add_remove_pad);
  tcase_add_test (tc_chain, test_add_pad_unref_element);
  tcase_add_test (tc_chain, test_error_no_bus);
  tcase_add_test (tc_chain, test_link);
  tcase_add_test (tc_chain, test_link_no_pads);

  return s;
}

GST_CHECK_MAIN (gst_element);
