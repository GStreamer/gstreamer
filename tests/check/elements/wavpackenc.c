/* GStreamer
 *
 * unit test for wavpackenc
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

#define RAW_CAPS_STRING "audio/x-raw-int, " \
                        "width = (int) 32, " \
                        "depth = (int) 16, " \
                        "channels = (int) 1, " \
                        "rate = (int) 44100, " \
                        "endianness = (int) BYTE_ORDER, " \
                        "signed = (boolean) true"

#define WAVPACK_CAPS_STRING "audio/x-wavpack, " \
                            "width = (int) 16, " \
                            "channels = (int) 1, " \
                            "rate = (int) 44100, " \
                            "framed = (boolean) true"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-wavpack, "
        "width = (int) 16, "
        "channels = (int) 1, "
        "rate = (int) 44100, " "framed = (boolean) true"));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) 32, "
        "depth = (int) 16, "
        "channels = (int) 1, "
        "rate = (int) 44100, "
        "endianness = (int) BYTE_ORDER, " "signed = (boolean) true"));

static GstElement *
setup_wavpackenc (void)
{
  GstElement *wavpackenc;

  GST_DEBUG ("setup_wavpackenc");
  wavpackenc = gst_check_setup_element ("wavpackenc");
  mysrcpad = gst_check_setup_src_pad (wavpackenc, &srctemplate, NULL);
  mysinkpad = gst_check_setup_sink_pad (wavpackenc, &sinktemplate, NULL);
  gst_pad_set_active (mysrcpad, TRUE);
  gst_pad_set_active (mysinkpad, TRUE);

  fail_unless (gst_element_set_state (wavpackenc,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  bus = gst_bus_new ();

  return wavpackenc;
}

static void
cleanup_wavpackenc (GstElement * wavpackenc)
{
  GST_DEBUG ("cleanup_wavpackenc");

  gst_bus_set_flushing (bus, TRUE);
  gst_element_set_bus (wavpackenc, NULL);
  gst_object_unref (GST_OBJECT (bus));

  gst_element_set_state (wavpackenc, GST_STATE_NULL);

  gst_pad_set_active (mysrcpad, FALSE);
  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_src_pad (wavpackenc);
  gst_check_teardown_sink_pad (wavpackenc);
  gst_check_teardown_element (wavpackenc);
}

GST_START_TEST (test_encode_silence)
{
  GstElement *wavpackenc;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  GstEvent *eos = gst_event_new_eos ();
  int i, num_buffers;

  wavpackenc = setup_wavpackenc ();

  inbuffer = gst_buffer_new_and_alloc (1000);
  for (i = 0; i < 1000; i++)
    GST_BUFFER_DATA (inbuffer)[i] = 0;
  caps = gst_caps_from_string (RAW_CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  GST_BUFFER_TIMESTAMP (inbuffer) = 0;
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  gst_buffer_ref (inbuffer);

  gst_element_set_bus (wavpackenc, bus);

  fail_unless_equals_int (gst_pad_push (mysrcpad, inbuffer), GST_FLOW_OK);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  gst_buffer_unref (inbuffer);

  fail_if (gst_pad_push_event (mysrcpad, eos) != TRUE);

  /* check first buffer */
  outbuffer = GST_BUFFER (buffers->data);

  fail_if (outbuffer == NULL);

  fail_unless_equals_int (GST_BUFFER_TIMESTAMP (outbuffer), 0);
  fail_unless_equals_int (GST_BUFFER_OFFSET (outbuffer), 0);
  fail_unless_equals_int (GST_BUFFER_DURATION (outbuffer), 5668934);
  fail_unless_equals_int (GST_BUFFER_OFFSET_END (outbuffer), 250);

  fail_unless (memcmp (GST_BUFFER_DATA (outbuffer), "wvpk", 4) == 0,
      "Failed to encode to valid Wavpack frames");

  /* free all buffers */
  num_buffers = g_list_length (buffers);

  for (i = 0; i < num_buffers; ++i) {
    outbuffer = GST_BUFFER (buffers->data);
    fail_if (outbuffer == NULL);

    buffers = g_list_remove (buffers, outbuffer);

    gst_buffer_unref (outbuffer);
    outbuffer = NULL;
  }

  g_list_free (buffers);
  buffers = NULL;

  cleanup_wavpackenc (wavpackenc);
}

GST_END_TEST;

static Suite *
wavpackenc_suite (void)
{
  Suite *s = suite_create ("wavpackenc");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_encode_silence);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = wavpackenc_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
