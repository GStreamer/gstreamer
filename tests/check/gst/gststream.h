/* GStreamer
 * Copyright (C) <2015> Edward Hervey <edward@centricular.com>
 *
 * gststructure.c: Unit tests for GstStream and GstStreamCollection
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
#include <gst/check/gstcheck.h>

GST_START_TEST (test_stream_creation)
{
  GstStream *stream;
  GstCaps *caps;

  caps = gst_caps_from_string("some/caps");
  stream = gst_stream_new ("upstream-id", caps, GST_STREAM_TYPE_AUDIO, 0);
  fail_unless (stream != NULL);

  fail_unless_equals_string (gst_stream_get_stream_id (stream), "upstream-id");

  gst_object_unref (stream);
}

GST_END_TEST;

static Suite *
gst_streams_suite (void)
{
  Suite *s = suite_create ("GstStream");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_stream_creation);
  return s;
}

GST_CHECK_MAIN (gst_streams);
