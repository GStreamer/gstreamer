/* GStreamer unit tests for postproc
 * Copyright (C) 2011 Collabora Ltd.
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
#include <gst/gst.h>

GST_START_TEST (test_postproc_default)
{
  GstElement *pp;

  pp = gst_element_factory_make ("postproc_default", NULL);
  fail_unless (pp != NULL, "Failed to create postproc_default!");
  gst_object_unref (pp);
}

GST_END_TEST;

static Suite *
postproc_suite (void)
{
  Suite *s = suite_create ("postproc");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_postproc_default);

  return s;
}

GST_CHECK_MAIN (postproc)
