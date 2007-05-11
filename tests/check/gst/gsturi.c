/* GStreamer unit tests for GstURI
 *
 * Copyright (C) 2007 Tim-Philipp Müller <tim centricular net>
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

GST_START_TEST (test_protocol_case)
{
  GstElement *element;

  element = gst_element_make_from_uri (GST_URI_SRC, "file:///foo/bar", NULL);

  /* no element? probably no registry, bail out */
  if (element == NULL)
    return;

  gst_object_unref (element);
  element = gst_element_make_from_uri (GST_URI_SRC, "FILE:///foo/bar", NULL);
  fail_unless (element != NULL,
      "Got source for 'file://' URI but not for 'FILE://' URI");
  gst_object_unref (element);
}

GST_END_TEST;

static Suite *
gst_uri_suite (void)
{
  Suite *s = suite_create ("GstURI");
  TCase *tc_chain = tcase_create ("uri");

  tcase_set_timeout (tc_chain, 20);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_protocol_case);
  return s;
}

GST_CHECK_MAIN (gst_uri);
