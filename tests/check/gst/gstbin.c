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

START_TEST (test_interface)
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

END_TEST;

START_TEST (test_message_state_changed)
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

END_TEST;

Suite *
gst_bin_suite (void)
{
  Suite *s = suite_create ("GstBin");
  TCase *tc_chain = tcase_create ("bin tests");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_interface);
  tcase_add_test (tc_chain, test_message_state_changed);

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
