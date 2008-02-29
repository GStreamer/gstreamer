/* GStreamer GstImplementsInterface check
 * Copyright (C) 2008 Tim-Philipp Müller <tim centricular net>
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

GST_START_TEST (test_without_implements_interface)
{
  GstElement *element;

  /* we shouldn't crash if someone tries to use
   * gst_element_implements_interface() on an element which doesn't implement
   * the GstImplementsInterface (neither if the element does implement the
   * requested interface, nor if it doesn't) */
  element = gst_element_factory_make ("filesrc", "filesrc");
  fail_unless (element != NULL, "Could not create filesrc element");

  /* does not implement GstImplementsInterface, but does implement the
   * GstUriHandler interface, so should just return TRUE */
  fail_if (!gst_element_implements_interface (element, GST_TYPE_URI_HANDLER));
  fail_if (gst_element_implements_interface (element,
          GST_TYPE_IMPLEMENTS_INTERFACE));
  gst_object_unref (element);

  element = gst_element_factory_make ("identity", "identity");
  fail_unless (element != NULL, "Could not create identity element");
  fail_if (gst_element_implements_interface (element, GST_TYPE_URI_HANDLER));
  fail_if (gst_element_implements_interface (element,
          GST_TYPE_IMPLEMENTS_INTERFACE));
  gst_object_unref (element);
}

GST_END_TEST;

static Suite *
gst_interface_suite (void)
{
  Suite *s = suite_create ("GstImplementsInterface");
  TCase *tc_chain = tcase_create ("correctness");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_without_implements_interface);
  return s;
}

GST_CHECK_MAIN (gst_interface);
