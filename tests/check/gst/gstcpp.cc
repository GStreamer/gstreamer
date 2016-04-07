/* GStreamer C++ compiler include compatibility test
 * Copyright (C) 2016 Tim-Philipp Müller <tim centricular com>
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

#include <gst/gst.h>
#include <gst/check/check.h>

GST_START_TEST (test_init_macros)
{
  GstMapInfo map = GST_MAP_INFO_INIT;
  GstPollFD poll_fd = GST_POLL_FD_INIT;

  fail_unless_equals_int (map.size, 0);
  fail_unless_equals_int (poll_fd.fd, -1);
}

GST_END_TEST;

static Suite *
gst_cpp_suite (void)
{
  Suite *s = suite_create ("GstC++");
  TCase *tc_chain = tcase_create ("gst C++ tests");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_init_macros);

  return s;
}

GST_CHECK_MAIN (gst_cpp);
