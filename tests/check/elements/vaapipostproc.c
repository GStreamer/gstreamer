/*
 *  vaapipostproc.c - GStreamer unit test for the vaapipostproc element
 *
 *  Copyright (C) 2019 Intel Corporation
 *    Author: U. Artie Eoff <ullysses.a.eoff@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <gst/check/gstcheck.h>

GST_START_TEST (test_make)
{
  GstElement *vaapipostproc;

  vaapipostproc = gst_element_factory_make ("vaapipostproc", "vaapipostproc");
  fail_unless (vaapipostproc != NULL, "Failed to create vaapipostproc element");

  gst_object_unref (vaapipostproc);
}

GST_END_TEST;

static Suite *
vaapipostproc_suite (void)
{
  Suite *s = suite_create ("vaapipostproc");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_make);

  return s;
}

GST_CHECK_MAIN (vaapipostproc);
