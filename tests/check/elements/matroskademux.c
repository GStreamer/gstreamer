/* GStreamer unit test for matroskademux
 * Copyright (C) 2015 Tim-Philipp Müller <tim@centricular.com>
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

#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>

const gchar mkv_sub_base64[] =
    "GkXfowEAAAAAAAAUQoKJbWF0cm9za2EAQoeBAkKFgQIYU4BnAQAAAAAAAg0RTZt0AQAAAAAAAIxN"
    "uwEAAAAAAAASU6uEFUmpZlOsiAAAAAAAAACYTbsBAAAAAAAAElOrhBZUrmtTrIgAAAAAAAABEuya"
    "AQAAAAAAABJTq4QQQ6dwU6yI///////////smgEAAAAAAAASU6uEHFO7a1OsiP//////////TbsB"
    "AAAAAAAAElOrhBJUw2dTrIgAAAAAAAAB9xVJqWYBAAAAAAAAbnOkkDylQZJlrLziQo8+gsrZVtUq"
    "17GDD0JARImIQNGUAAAAAABNgJ9HU3RyZWFtZXIgcGx1Z2luIHZlcnNpb24gMS40LjUAV0GZR1N0"
    "cmVhbWVyIE1hdHJvc2thIG11eGVyAERhiAZfU0rcEwgAFlSuawEAAAAAAAA0rgEAAAAAAAAr14EB"
    "g4ERc8WIoWF8pYlELidTbolTdWJ0aXRsZQCGjFNfVEVYVC9VVEY4AB9DtnUBAAAAAAAAmeeCA+ig"
    "AQAAAAAAAA2bggfQoYeBAAAAZm9voAEAAAAAAAAUm4IH0KGOgQu4ADxpPmJhcjwvaT6gAQAAAAAA"
    "AA2bggfQoYeBF3AAYmF6oAEAAAAAAAAOm4IH0KGIgScQAGbDtgCgAQAAAAAAABWbggfQoY+BMsgA"
    "PGk+YmFyPC9pPgCgAQAAAAAAAA6bggfQoYiBPoAAYuR6ABJUw2cBAAAAAAAACnNzAQAAAAAAAAA=";

static void
pad_added_cb (GstElement * matroskademux, GstPad * pad, gpointer user_data)
{
  GstHarness *h = user_data;

  GST_LOG_OBJECT (pad, "got new source pad");
  gst_harness_add_element_src_pad (h, pad);
}

static void
pull_and_check_buffer (GstHarness * h, GstClockTime pts, GstClockTime duration,
    const gchar * output)
{
  GstMapInfo map;
  GstBuffer *buf;

  /* wait for buffer */
  buf = gst_harness_pull (h);

  /* Make sure there's no 0-terminator in there */
  fail_unless (gst_buffer_map (buf, &map, GST_MAP_READ));
  GST_MEMDUMP ("subtitle buffer", map.data, map.size);
  fail_unless (map.size > 0);
  fail_unless (map.data[map.size - 1] != '\0');
  if (output != NULL && memcmp (map.data, output, map.size) != 0) {
    g_printerr ("Got:\n");
    gst_util_dump_mem (map.data, map.size);;
    g_printerr ("Wanted:\n");
    gst_util_dump_mem ((guint8 *) output, strlen (output));
    g_error ("Did not get output expected.");
  }

  gst_buffer_unmap (buf, &map);

  fail_unless_equals_int64 (pts, GST_BUFFER_PTS (buf));
  fail_unless_equals_int64 (duration, GST_BUFFER_DURATION (buf));

  gst_buffer_unref (buf);
}

GST_START_TEST (test_sub_terminator)
{
  GstHarness *h;
  GstBuffer *buf;
  guchar *mkv_data;
  gsize mkv_size;

  h = gst_harness_new_with_padnames ("matroskademux", "sink", NULL);

  g_signal_connect (h->element, "pad-added", G_CALLBACK (pad_added_cb), h);

  mkv_data = g_base64_decode (mkv_sub_base64, &mkv_size);
  fail_unless (mkv_data != NULL);

  gst_harness_set_src_caps_str (h, "video/x-matroska");

  buf = gst_buffer_new_wrapped (mkv_data, mkv_size);
  GST_BUFFER_OFFSET (buf) = 0;

  fail_unless_equals_int (gst_harness_push (h, buf), GST_FLOW_OK);
  gst_harness_push_event (h, gst_event_new_eos ());

  pull_and_check_buffer (h, 1 * GST_SECOND, 2 * GST_SECOND, "foo");
  pull_and_check_buffer (h, 4 * GST_SECOND, 2 * GST_SECOND, "<i>bar</i>");
  pull_and_check_buffer (h, 7 * GST_SECOND, 2 * GST_SECOND, "baz");
  pull_and_check_buffer (h, 11 * GST_SECOND, 2 * GST_SECOND, "f\303\266");
  pull_and_check_buffer (h, 14 * GST_SECOND, 2 * GST_SECOND, "<i>bar</i>");
  /* The input is invalid UTF-8 here, what happens might depend on locale */
  pull_and_check_buffer (h, 17 * GST_SECOND, 2 * GST_SECOND, NULL);

  fail_unless (gst_harness_try_pull (h) == NULL);

  gst_harness_teardown (h);
}

GST_END_TEST;

static Suite *
matroskademux_suite (void)
{
  Suite *s = suite_create ("matroskademux");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_sub_terminator);

  return s;
}

GST_CHECK_MAIN (matroskademux);
