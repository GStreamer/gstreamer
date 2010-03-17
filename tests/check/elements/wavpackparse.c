/* GStreamer
 *
 * unit test for wavpackparse
 *
 * Copyright (c) 2006 Sebastian Dröge <slomo@circular-chaos.org>
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

#include <unistd.h>

#include <gst/check/gstcheck.h>

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
static GstPad *mysrcpad, *mysinkpad;
static GstBus *bus;
static GstElement *wavpackparse;

/* Wavpack file with 2 frames of silence */
guint8 test_file[] = {
  0x77, 0x76, 0x70, 0x6B, 0x62, 0x00, 0x00, 0x00,       /* first frame */
  0x04, 0x04, 0x00, 0x00, 0x00, 0xC8, 0x00, 0x00,       /* include RIFF header */
  0x00, 0x00, 0x00, 0x00, 0x00, 0x64, 0x00, 0x00,
  0x05, 0x18, 0x80, 0x04, 0xFF, 0xAF, 0x80, 0x60,
  0x21, 0x16, 0x52, 0x49, 0x46, 0x46, 0x24, 0x90,
  0x01, 0x00, 0x57, 0x41, 0x56, 0x45, 0x66, 0x6D,
  0x74, 0x20, 0x10, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x01, 0x00, 0x44, 0xAC, 0x00, 0x00, 0x88, 0x58,
  0x01, 0x00, 0x02, 0x00, 0x10, 0x00, 0x64, 0x61,
  0x74, 0x61, 0x00, 0x90, 0x01, 0x00, 0x02, 0x00,
  0x03, 0x00, 0x04, 0x00, 0x05, 0x03, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x65, 0x02, 0x00, 0x00,
  0x00, 0x00, 0x8A, 0x02, 0x00, 0x00, 0xFF, 0x7F,
  0x00, 0xE4,
  0x77, 0x76, 0x70, 0x6B, 0x2E, 0x00, 0x00, 0x00,       /* second frame */
  0x04, 0x04, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
  0x00, 0x64, 0x00, 0x00, 0x00, 0x64, 0x00, 0x00,
  0x05, 0x18, 0x80, 0x04, 0xFF, 0xAF, 0x80, 0x60,
  0x02, 0x00, 0x03, 0x00, 0x04, 0x00, 0x05, 0x03,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8A, 0x02,
  0x00, 0x00, 0xFF, 0x7F, 0x00, 0xE4,
};

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-wavpack, "
        "width = (int) 16, "
        "channels = (int) 1, "
        "rate = (int) 44100, " "framed = (boolean) TRUE"));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-wavpack"));

static void
wavpackparse_found_pad (GstElement * src, GstPad * pad, gpointer data)
{
  GstPad *srcpad;

  mysinkpad = gst_pad_new_from_static_template (&sinktemplate, "sink");
  fail_if (mysinkpad == NULL, "Couldn't create sinkpad");
  srcpad = gst_element_get_static_pad (wavpackparse, "src");
  fail_if (srcpad == NULL, "Failed to get srcpad from wavpackparse");
  gst_pad_set_chain_function (mysinkpad, gst_check_chain_func);
  fail_unless (gst_pad_link (srcpad, mysinkpad) == GST_PAD_LINK_OK,
      "Failed to link pads");
  gst_pad_set_active (mysinkpad, TRUE);
  gst_object_unref (srcpad);
}

static void
setup_wavpackparse (void)
{
  GstPad *sinkpad;

  GST_DEBUG ("setup_wavpackparse");

  wavpackparse = gst_element_factory_make ("wavpackparse", "wavpackparse");
  fail_if (wavpackparse == NULL, "Could not create wavpackparse");

  mysrcpad = gst_pad_new_from_static_template (&srctemplate, "src");
  fail_if (mysrcpad == NULL, "Could not create srcpad");

  sinkpad = gst_element_get_static_pad (wavpackparse, "sink");
  fail_if (sinkpad == NULL, "Failed to get sinkpad from wavpackparse");
  fail_unless (gst_pad_link (mysrcpad, sinkpad) == GST_PAD_LINK_OK,
      "Failed to link pads");
  gst_object_unref (sinkpad);

  g_signal_connect (wavpackparse, "pad-added",
      G_CALLBACK (wavpackparse_found_pad), NULL);

  bus = gst_bus_new ();
  gst_element_set_bus (wavpackparse, bus);

  fail_unless (gst_element_set_state (wavpackparse,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
}

static void
cleanup_wavpackparse (void)
{
  GstPad *sinkpad, *srcpad;

  GST_DEBUG ("cleanup_wavpackparse");

  gst_bus_set_flushing (bus, TRUE);
  gst_element_set_bus (wavpackparse, NULL);
  gst_object_unref (GST_OBJECT (bus));

  sinkpad = gst_element_get_static_pad (wavpackparse, "sink");
  fail_if (sinkpad == NULL, "Failed to get sinkpad from wavpackparse");
  fail_unless (gst_pad_unlink (mysrcpad, sinkpad), "Failed to unlink pads");
  gst_pad_set_caps (mysrcpad, NULL);
  gst_object_unref (sinkpad);
  gst_object_unref (mysrcpad);

  srcpad = gst_element_get_static_pad (wavpackparse, "src");
  fail_if (srcpad == NULL, "Failed to get srcpad from wavpackparse");
  fail_unless (gst_pad_unlink (srcpad, mysinkpad), "Failed to unlink pads");
  gst_pad_set_caps (mysinkpad, NULL);
  gst_object_unref (srcpad);
  gst_object_unref (mysinkpad);

  fail_unless (gst_element_set_state (wavpackparse, GST_STATE_NULL) ==
      GST_STATE_CHANGE_SUCCESS, "could not set to null");

  gst_object_unref (wavpackparse);
}

GST_START_TEST (test_parsing_valid_frames)
{
  GstBuffer *inbuffer, *outbuffer;
  int i, num_buffers;
  GstFormat format = GST_FORMAT_DEFAULT;
  gint64 pos;

  setup_wavpackparse ();

  inbuffer = gst_buffer_new_and_alloc (sizeof (test_file));
  memcpy (GST_BUFFER_DATA (inbuffer), test_file, sizeof (test_file));
  gst_buffer_ref (inbuffer);

  /* should decode the buffer without problems */
  fail_unless_equals_int (gst_pad_push (mysrcpad, inbuffer), GST_FLOW_OK);
  gst_buffer_unref (inbuffer);

  num_buffers = g_list_length (buffers);
  /* should get 2 buffers, each one complete wavpack frame */
  fail_unless_equals_int (num_buffers, 2);

  for (i = 0; i < num_buffers; ++i) {
    outbuffer = GST_BUFFER (buffers->data);
    fail_if (outbuffer == NULL);

    fail_unless (memcmp (GST_BUFFER_DATA (outbuffer), "wvpk", 4) == 0,
        "Buffer contains no Wavpack frame");
    fail_unless_equals_int (GST_BUFFER_DURATION (outbuffer), 580498866);

    switch (i) {
      case 0:{
        fail_unless_equals_int (GST_BUFFER_TIMESTAMP (outbuffer), 0);
        fail_unless_equals_int (GST_BUFFER_OFFSET (outbuffer), 0);
        fail_unless_equals_int (GST_BUFFER_OFFSET_END (outbuffer), 25600);
        break;
      }
      case 1:{
        fail_unless_equals_int (GST_BUFFER_TIMESTAMP (outbuffer), 580498866);
        fail_unless_equals_int (GST_BUFFER_OFFSET (outbuffer), 25600);
        fail_unless_equals_int (GST_BUFFER_OFFSET_END (outbuffer), 51200);
        break;
      }
    }

    buffers = g_list_remove (buffers, outbuffer);

    gst_buffer_unref (outbuffer);
    outbuffer = NULL;
  }

  fail_unless (gst_element_query_position (wavpackparse, &format, &pos),
      "Position query failed");
  fail_unless_equals_int (pos, 51200);
  fail_unless (gst_element_query_duration (wavpackparse, &format, NULL),
      "Duration query failed");

  g_list_free (buffers);
  buffers = NULL;

  cleanup_wavpackparse ();
}

GST_END_TEST;

GST_START_TEST (test_parsing_invalid_first_header)
{
  GstBuffer *inbuffer, *outbuffer;
  int i, num_buffers;

  setup_wavpackparse ();

  inbuffer = gst_buffer_new_and_alloc (sizeof (test_file));
  memcpy (GST_BUFFER_DATA (inbuffer), test_file, sizeof (test_file));
  GST_BUFFER_DATA (inbuffer)[0] = 'k';
  gst_buffer_ref (inbuffer);

  /* should decode the buffer without problems */
  fail_unless_equals_int (gst_pad_push (mysrcpad, inbuffer), GST_FLOW_OK);
  gst_buffer_unref (inbuffer);

  num_buffers = g_list_length (buffers);

  /* should get 1 buffers, the second non-broken one */
  fail_unless_equals_int (num_buffers, 1);

  for (i = 0; i < num_buffers; ++i) {
    outbuffer = GST_BUFFER (buffers->data);
    fail_if (outbuffer == NULL);

    fail_unless (memcmp (GST_BUFFER_DATA (outbuffer), "wvpk", 4) == 0,
        "Buffer contains no Wavpack frame");
    fail_unless_equals_int (GST_BUFFER_DURATION (outbuffer), 580498866);

    switch (i) {
      case 0:{
        fail_unless_equals_int (GST_BUFFER_TIMESTAMP (outbuffer), 580498866);
        fail_unless_equals_int (GST_BUFFER_OFFSET (outbuffer), 25600);
        break;
      }
    }

    buffers = g_list_remove (buffers, outbuffer);

    gst_buffer_unref (outbuffer);
    outbuffer = NULL;
  }

  g_list_free (buffers);
  buffers = NULL;

  cleanup_wavpackparse ();
}

GST_END_TEST;


static Suite *
wavpackparse_suite (void)
{
  Suite *s = suite_create ("wavpackparse");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_parsing_valid_frames);
  tcase_add_test (tc_chain, test_parsing_invalid_first_header);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = wavpackparse_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
