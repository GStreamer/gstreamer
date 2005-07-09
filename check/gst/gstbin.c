/* GStreamer
 * Copyright (C) 2005 Wim Taymans <wim@fluendo.com>
 * Copyright (C) 2005 Thomas Vander Stichele <thomas at apestaart dot org>
 *
 * gstbin.c: Unit test for GstBin
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

#include "../gstcheck.h"

static void
pop_messages (GstBus * bus, int count)
{
  GstMessage *message;

  int i;

  GST_DEBUG ("popping %d messages", count);
  for (i = 0; i < count; ++i) {
    fail_unless (gst_bus_poll (bus, GST_MESSAGE_STATE_CHANGED, -1)
        == GST_MESSAGE_STATE_CHANGED, "did not get GST_MESSAGE_STATE_CHANGED");

    message = gst_bus_pop (bus);
    gst_message_unref (message);
  }
  GST_DEBUG ("popped %d messages", count);
}

GST_START_TEST (test_interface)
{
  GstBin *bin, *bin2;
  GstElement *filesrc;
  GstIterator *it;
  gpointer item;

  bin = GST_BIN (gst_bin_new (NULL));
  fail_unless (bin != NULL, "Could not create bin");

  filesrc = gst_element_factory_make ("filesrc", NULL);
  fail_unless (filesrc != NULL, "Could not create filesrc");
  fail_unless (GST_IS_URI_HANDLER (filesrc), "Filesrc not a URI handler");
  gst_bin_add (bin, filesrc);

  fail_unless (gst_bin_get_by_interface (bin, GST_TYPE_URI_HANDLER) == filesrc);
  it = gst_bin_iterate_all_by_interface (bin, GST_TYPE_URI_HANDLER);
  fail_unless (it != NULL);
  fail_unless (gst_iterator_next (it, &item) == GST_ITERATOR_OK);
  fail_unless (item == (gpointer) filesrc);
  fail_unless (gst_iterator_next (it, &item) == GST_ITERATOR_DONE);
  gst_iterator_free (it);

  gst_bin_add_many (bin,
      gst_element_factory_make ("identity", NULL),
      gst_element_factory_make ("identity", NULL),
      gst_element_factory_make ("identity", NULL), NULL);
  fail_unless (gst_bin_get_by_interface (bin, GST_TYPE_URI_HANDLER) == filesrc);
  it = gst_bin_iterate_all_by_interface (bin, GST_TYPE_URI_HANDLER);
  fail_unless (it != NULL);
  fail_unless (gst_iterator_next (it, &item) == GST_ITERATOR_OK);
  fail_unless (item == (gpointer) filesrc);
  fail_unless (gst_iterator_next (it, &item) == GST_ITERATOR_DONE);
  gst_iterator_free (it);

  bin2 = bin;
  bin = GST_BIN (gst_bin_new (NULL));
  fail_unless (bin != NULL);
  gst_bin_add_many (bin,
      gst_element_factory_make ("identity", NULL),
      gst_element_factory_make ("identity", NULL),
      GST_ELEMENT (bin2), gst_element_factory_make ("identity", NULL), NULL);
  fail_unless (gst_bin_get_by_interface (bin, GST_TYPE_URI_HANDLER) == filesrc);
  it = gst_bin_iterate_all_by_interface (bin, GST_TYPE_URI_HANDLER);
  fail_unless (gst_iterator_next (it, &item) == GST_ITERATOR_OK);
  fail_unless (item == (gpointer) filesrc);
  fail_unless (gst_iterator_next (it, &item) == GST_ITERATOR_DONE);
  gst_iterator_free (it);

  gst_bin_add (bin, gst_element_factory_make ("filesrc", NULL));
  gst_bin_add (bin2, gst_element_factory_make ("filesrc", NULL));
  it = gst_bin_iterate_all_by_interface (bin, GST_TYPE_URI_HANDLER);
  fail_unless (gst_iterator_next (it, &item) == GST_ITERATOR_OK);
  fail_unless (gst_iterator_next (it, &item) == GST_ITERATOR_OK);
  fail_unless (gst_iterator_next (it, &item) == GST_ITERATOR_OK);
  fail_unless (gst_iterator_next (it, &item) == GST_ITERATOR_DONE);
  gst_iterator_free (it);

  gst_object_unref (bin);
}

GST_END_TEST;

GST_START_TEST (test_message_state_changed)
{
  GstBin *bin;
  GstBus *bus;
  GstMessage *message;

  bin = GST_BIN (gst_bin_new (NULL));
  fail_unless (bin != NULL, "Could not create bin");
  ASSERT_OBJECT_REFCOUNT (bin, "bin", 1);

  bus = GST_ELEMENT_BUS (bin);

  /* change state, spawning a message, causing an incref on the bin */
  gst_element_set_state (GST_ELEMENT (bin), GST_STATE_READY);

  ASSERT_OBJECT_REFCOUNT (bin, "bin", 2);

  /* get and unref the message, causing a decref on the bin */
  fail_unless (gst_bus_poll (bus, GST_MESSAGE_STATE_CHANGED,
          -1) == GST_MESSAGE_STATE_CHANGED,
      "did not get GST_MESSAGE_STATE_CHANGED");

  message = gst_bus_pop (bus);
  gst_message_unref (message);

  ASSERT_OBJECT_REFCOUNT (bin, "bin", 1);

  /* clean up */
  gst_object_unref (bin);
}

GST_END_TEST;

GST_START_TEST (test_message_state_changed_child)
{
  GstBin *bin;
  GstElement *src;
  GstBus *bus;
  GstMessage *message;

  bin = GST_BIN (gst_bin_new (NULL));
  fail_unless (bin != NULL, "Could not create bin");
  ASSERT_OBJECT_REFCOUNT (bin, "bin", 1);

  src = gst_element_factory_make ("fakesrc", NULL);
  fail_if (src == NULL, "Could not create fakesrc");
  gst_bin_add (bin, src);
  ASSERT_OBJECT_REFCOUNT (bin, "bin", 1);
  ASSERT_OBJECT_REFCOUNT (src, "src", 1);

  bus = GST_ELEMENT_BUS (bin);

  /* change state, spawning two messages:
   * - first for fakesrc, forwarded to bin's bus, causing incref on fakesrc
   * - second for bin, causing an incref on the bin */
  GST_DEBUG ("setting bin to READY");
  fail_unless (gst_element_set_state (GST_ELEMENT (bin), GST_STATE_READY)
      == GST_STATE_SUCCESS);

  ASSERT_OBJECT_REFCOUNT (src, "src", 2);
  ASSERT_OBJECT_REFCOUNT (bin, "bin", 2);

  /* get and unref the message, causing a decref on the src */
  fail_unless (gst_bus_poll (bus, GST_MESSAGE_STATE_CHANGED, -1)
      == GST_MESSAGE_STATE_CHANGED, "did not get GST_MESSAGE_STATE_CHANGED");

  message = gst_bus_pop (bus);
  fail_unless (message->src == GST_OBJECT (src));
  gst_message_unref (message);

  ASSERT_OBJECT_REFCOUNT (src, "src", 1);
  ASSERT_OBJECT_REFCOUNT (bin, "bin", 2);

  /* get and unref message 2, causing a decref on the bin */
  fail_unless (gst_bus_poll (bus, GST_MESSAGE_STATE_CHANGED, -1)
      == GST_MESSAGE_STATE_CHANGED, "did not get GST_MESSAGE_STATE_CHANGED");

  message = gst_bus_pop (bus);
  fail_unless (message->src == GST_OBJECT (bin));
  gst_message_unref (message);

  ASSERT_OBJECT_REFCOUNT (src, "src", 1);
  ASSERT_OBJECT_REFCOUNT (bin, "bin", 1);

  /* clean up */
  gst_object_unref (bin);
}

GST_END_TEST;

GST_START_TEST (test_message_state_changed_children)
{
  GstPipeline *pipeline;
  GstElement *src, *sink;
  GstBus *bus;

  pipeline = GST_PIPELINE (gst_pipeline_new (NULL));
  fail_unless (pipeline != NULL, "Could not create pipeline");
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 1);

  src = gst_element_factory_make ("fakesrc", NULL);
  fail_if (src == NULL, "Could not create fakesrc");
  gst_bin_add (GST_BIN (pipeline), src);

  sink = gst_element_factory_make ("fakesink", NULL);
  fail_if (sink == NULL, "Could not create fakesink");
  gst_bin_add (GST_BIN (pipeline), sink);

  fail_unless (gst_element_link (src, sink), "could not link src and sink");

  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 1);
  ASSERT_OBJECT_REFCOUNT (src, "src", 1);
  ASSERT_OBJECT_REFCOUNT (src, "sink", 1);

  bus = GST_ELEMENT_BUS (pipeline);

  /* change state, spawning three times three messages */
  GST_DEBUG ("setting pipeline to PLAYING");
  fail_unless (gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING)
      == GST_STATE_SUCCESS);

  pop_messages (bus, 9);

  ASSERT_OBJECT_REFCOUNT (src, "src", 1);
  ASSERT_OBJECT_REFCOUNT (sink, "sink", 1);
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 1);

  /* go back to READY, spawning six messages */
  GST_DEBUG ("setting pipeline to READY");
  fail_unless (gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_READY)
      == GST_STATE_SUCCESS);

  /* each object is referenced by two messages */
  ASSERT_OBJECT_REFCOUNT (src, "src", 3);
  ASSERT_OBJECT_REFCOUNT (sink, "sink", 3);
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 3);

  pop_messages (bus, 6);

  ASSERT_OBJECT_REFCOUNT (src, "src", 1);
  ASSERT_OBJECT_REFCOUNT (sink, "sink", 1);
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 1);

  /* setting pipeline to NULL flushes the bus automatically */
  fail_unless (gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL)
      == GST_STATE_SUCCESS);

  ASSERT_OBJECT_REFCOUNT (src, "src", 1);
  ASSERT_OBJECT_REFCOUNT (sink, "sink", 1);
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 1);

  /* clean up */
  gst_object_unref (pipeline);
}

GST_END_TEST;

Suite *
gst_bin_suite (void)
{
  Suite *s = suite_create ("GstBin");
  TCase *tc_chain = tcase_create ("bin tests");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_interface);
  tcase_add_test (tc_chain, test_message_state_changed);
  tcase_add_test (tc_chain, test_message_state_changed_child);
  tcase_add_test (tc_chain, test_message_state_changed_children);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = gst_bin_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
