/*
 * skeldec.c - GStreamer annodex skeleton decoder test suite
 * Copyright (C) 2005 Alessandro Decina
 * 
 * Authors:
 *   Alessandro Decina <alessandro@nnva.org>
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

GList *buffers;

GstPad *srcpad, *sinkpad;

#define SKELETON_CAPS "application/x-ogg-skeleton"

#define SKELETON_FISHEAD \
  "fishead\0"\
  "\x03\0\0\0"\
  "\x39\x30\0\0\0\0\0\0"\
  "\x39\x30\0\0\0\0\0\0"\
  "\x39\x30\0\0\0\0\0\0"\
  "\x39\x30\0\0\0\0\0\0"\
  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"

#define SKELETON_FISHEAD_SIZE 64

#define SKELETON_FISBONE \
  "fisbone\0"\
  "\x2c\0\0\0"\
  "\x39\x30\0\0"\
  "\x39\x30\0\0"\
  "\x39\x30\0\0\0\0\0\0"\
  "\x39\x30\0\0\0\0\0\0"\
  "\x39\x30\0\0\0\0\0\0"\
  "\x39\x30\0\0"\
  "\x20"\
  "\0\0\0"\
  "Content-Type: application/ogg; UTF-8\r\n"

#define SKELETON_FISBONE_SIZE 90

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SKELETON_CAPS)
    );

static GstElement *
setup_skeldec ()
{
  GstElement *skeldec;

  GST_DEBUG ("setup_skeldec");
  skeldec = gst_check_setup_element ("skeldec");
  srcpad = gst_check_setup_src_pad (skeldec, &srctemplate, NULL);
  sinkpad = gst_check_setup_sink_pad (skeldec, &sinktemplate, NULL);

  return skeldec;
}

static void
cleanup_skeldec (GstElement * skeldec)
{
  GST_DEBUG ("cleanup_skeldec");

  gst_check_teardown_src_pad (skeldec);
  gst_check_teardown_sink_pad (skeldec);
  gst_check_teardown_element (skeldec);
}

static void
skel_buffer_unref (void *buf, void *user_data)
{
  GstBuffer *buffer = GST_BUFFER (buf);

  ASSERT_OBJECT_REFCOUNT (buffer, "skel-buffer", 1);
  gst_buffer_unref (buffer);
}

static GstBuffer *
skel_buffer_new (gchar * buffer_data, guint size)
{
  GstBuffer *buffer;
  GstCaps *caps;

  buffer = gst_buffer_new_and_alloc (size);
  memcpy (GST_BUFFER_DATA (buffer), buffer_data, size);
  caps = gst_caps_from_string (SKELETON_CAPS);
  gst_buffer_set_caps (buffer, caps);
  gst_caps_unref (caps);

  return buffer;
}

GST_START_TEST (test_dec)
{
  GstElement *skeldec;
  GstBus *bus;
  GstBuffer *inbuffer;
  GstMessage *message;
  GstTagList *tags;
  const GValue *tag_val, *val;
  GObject *tag;
  gint major, minor;
  gint64 prestime_n, prestime_d;
  gint64 basetime_n, basetime_d;
  guint serial_number;
  gint64 granule_rate_n, granule_rate_d;
  gint64 granule_start;
  guint64 preroll;
  guint granule_shift;
  GValueArray *headers;
  gchar *content_type;
  gchar *encoding;

  skeldec = setup_skeldec ();

  bus = gst_bus_new ();
  gst_element_set_bus (skeldec, bus);

  fail_unless (gst_element_set_state (skeldec,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  /* test the fishead */
  inbuffer = skel_buffer_new (SKELETON_FISHEAD, SKELETON_FISHEAD_SIZE);
  fail_unless_equals_int (gst_pad_push (srcpad, inbuffer), GST_FLOW_OK);

  message = gst_bus_poll (bus, GST_MESSAGE_TAG, -1);
  fail_unless (GST_MESSAGE_SRC (message) == GST_OBJECT (skeldec));

  gst_message_parse_tag (message, &tags);
  fail_unless (tags != NULL);
  fail_unless_equals_int (gst_tag_list_get_tag_size (tags,
          GST_TAG_SKELETON_FISHEAD), 1);

  tag_val = gst_tag_list_get_value_index (tags, GST_TAG_SKELETON_FISHEAD, 0);
  fail_unless (tag_val != NULL);

  tag = g_value_get_object (tag_val);
  fail_unless (tag != NULL);

  g_object_get (tag,
      "version-major", &major, "version-minor", &minor,
      "presentation-time-numerator", &prestime_n,
      "presentation-time-denominator", &prestime_d,
      "base-time-numerator", &basetime_n,
      "base-time-denominator", &basetime_d, NULL);

  fail_unless_equals_int (major, 3);
  fail_unless_equals_int (minor, 0);
  fail_unless_equals_int (prestime_n, 12345);
  fail_unless_equals_int (prestime_d, 12345);
  fail_unless_equals_int (basetime_n, 12345);
  fail_unless_equals_int (basetime_d, 12345);

  gst_tag_list_free (tags);
  gst_message_unref (message);

  /* test the fisbone */
  inbuffer = skel_buffer_new (SKELETON_FISBONE, SKELETON_FISBONE_SIZE);
  fail_unless_equals_int (gst_pad_push (srcpad, inbuffer), GST_FLOW_OK);

  message = gst_bus_poll (bus, GST_MESSAGE_TAG, -1);
  fail_unless (GST_MESSAGE_SRC (message) == GST_OBJECT (skeldec));

  gst_message_parse_tag (message, &tags);
  fail_unless (tags != NULL);
  fail_unless_equals_int (gst_tag_list_get_tag_size (tags,
          GST_TAG_SKELETON_FISBONE), 1);

  tag_val = gst_tag_list_get_value_index (tags, GST_TAG_SKELETON_FISBONE, 0);
  fail_unless (tag_val != NULL);

  tag = g_value_get_object (tag_val);
  fail_unless (tag != NULL);

  g_object_get (tag, "serial-number", &serial_number,
      "granule-rate-numerator", &granule_rate_n,
      "granule-rate-denominator", &granule_rate_d,
      "granule-start", &granule_start,
      "granule-shift", &granule_shift,
      "preroll", &preroll,
      "headers", &headers,
      "content-type", &content_type, "encoding", &encoding, NULL);

  fail_unless_equals_int (serial_number, 12345);
  fail_unless_equals_int (granule_rate_n, 12345);
  fail_unless_equals_int (granule_rate_d, 12345);
  fail_unless_equals_int (granule_start, 12345);
  fail_unless_equals_int (preroll, 12345);
  fail_unless_equals_int (granule_shift, 32);
  fail_unless_equals_int (headers->n_values, 2);
  fail_unless_equals_string (content_type, "application/ogg");
  fail_unless_equals_string (encoding, "UTF-8");

  g_value_array_free (headers);
  g_free (content_type);
  g_free (encoding);
  gst_tag_list_free (tags);
  gst_message_unref (message);
  gst_bus_set_flushing (bus, TRUE);
  gst_object_unref (bus);
  g_list_foreach (buffers, skel_buffer_unref, NULL);
  cleanup_skeldec (skeldec);
}

GST_END_TEST;

static Suite *
skeldec_suite ()
{
  Suite *s = suite_create ("skeldec");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_dec);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = skeldec_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
