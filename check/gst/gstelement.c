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

#include "../gstcheck.h"

START_TEST (test_add_remove_pad)
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
}

END_TEST;

START_TEST (test_add_pad_unref_element)
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

END_TEST;


Suite *
gst_element_suite (void)
{
  Suite *s = suite_create ("GstElement");
  TCase *tc_chain = tcase_create ("element tests");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_add_remove_pad);
  tcase_add_test (tc_chain, test_add_pad_unref_element);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = gst_element_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
