/*
 * GStreamer
 *
 * unit test for amrnbenc
 *
 * Copyright (C) 2006 Thomas Vander Stichele <thomas at apestaart dot org>
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
#include <gst/audio/audio.h>

#define SRC_CAPS "audio/x-raw, format = (string)" GST_AUDIO_NE (S16) ", " \
    "layout = (string) interleaved, channels = (int) 1, rate = (int) 8000"
#define SINK_CAPS "audio/AMR"

GList *buffers;
GList *current_buf = NULL;

GstPad *srcpad, *sinkpad;

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SINK_CAPS)
    );

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SRC_CAPS)
    );

static void
buffer_unref (void *buffer, void *user_data)
{
  gst_buffer_unref (GST_BUFFER (buffer));
}

static GstElement *
setup_amrnbenc (void)
{
  GstElement *amrnbenc;
  GstCaps *caps;
  GstBus *bus;
  guint64 granulerate_n, granulerate_d;

  GST_DEBUG ("setup_amrnbenc");

  amrnbenc = gst_check_setup_element ("amrnbenc");
  srcpad = gst_check_setup_src_pad (amrnbenc, &srctemplate);
  sinkpad = gst_check_setup_sink_pad (amrnbenc, &sinktemplate);
  gst_pad_set_active (srcpad, TRUE);
  gst_pad_set_active (sinkpad, TRUE);

  bus = gst_bus_new ();
  gst_element_set_bus (amrnbenc, bus);

  fail_unless (gst_element_set_state (amrnbenc,
          GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE,
      "could not set to playing");

  caps = gst_caps_from_string (SRC_CAPS);
  gst_check_setup_events (srcpad, amrnbenc, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  buffers = NULL;
  return amrnbenc;
}

static void
cleanup_amrnbenc (GstElement * amrnbenc)
{
  GstBus *bus;

  /* free encoded buffers */
  g_list_foreach (buffers, buffer_unref, NULL);
  g_list_free (buffers);
  buffers = NULL;

  bus = GST_ELEMENT_BUS (amrnbenc);
  gst_bus_set_flushing (bus, TRUE);
  gst_object_unref (bus);

  GST_DEBUG ("cleanup_amrnbenc");
  gst_pad_set_active (srcpad, FALSE);
  gst_pad_set_active (sinkpad, FALSE);
  gst_check_teardown_src_pad (amrnbenc);
  gst_check_teardown_sink_pad (amrnbenc);
  gst_check_teardown_element (amrnbenc);
}

/* push a random block of audio of the given size */
static void
push_data (gint size, GstFlowReturn expected_return)
{
  GstBuffer *buffer;
  GstFlowReturn res;

  buffer = gst_buffer_new_and_alloc (size);
  /* make valgrind happier */
  gst_buffer_memset (buffer, 0, 0, size);

  res = gst_pad_push (srcpad, buffer);
  fail_unless (res == expected_return,
      "pushing audio returned %d (%s) not %d (%s)", res,
      gst_flow_get_name (res), expected_return,
      gst_flow_get_name (expected_return));
}

GST_START_TEST (test_enc)
{
  GstElement *amrnbenc;

  amrnbenc = setup_amrnbenc ();
  push_data (1000, GST_FLOW_OK);

  cleanup_amrnbenc (amrnbenc);
}

GST_END_TEST;

static Suite *
amrnbenc_suite ()
{
  Suite *s = suite_create ("amrnbenc");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_enc);
  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = amrnbenc_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
