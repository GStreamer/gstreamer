/* GStreamer
 *
 * Unit tests for lc3enc and lc3dec
 *
 * Copyright (C) <2023> Asymptotic Inc. <taruntej@asymptotic.io>
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

#include <string.h>
#include <unistd.h>

#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>
#include <gst/audio/audio.h>

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function
 * `srcpad` is the peer pad of lc3enc sinkpad upstream
 * `sinkpad` is the peer pad of lc3enc srcpad downstream
 */
static GstPad *srcpad, *sinkpad;

#define SAMPLE_RATES "8000, 16000, 24000, 32000, 48000"
#define FORMATS "S16LE, S24LE, S32LE, F32LE"

#define FRAME_DURATION_10000US 10000
#define FRAME_DURATION_7500US 7500
#define FRAME_BYTES_MIN 20
#define FRAME_BYTES_MAX 400

#define FRAME_DURATIONS G_STRINGIFY(FRAME_DURATION_10000US) ", " G_STRINGIFY(FRAME_DURATION_7500US)
#define FRAME_BYTES_RANGE  G_STRINGIFY(FRAME_BYTES_MIN) ", " G_STRINGIFY(FRAME_BYTES_MAX)

#define RAW_AUDIO_CAPS_STRING "audio/x-raw, " \
        "format = { " FORMATS "  }, " \
        "rate = (int) { " SAMPLE_RATES  " }, " "channels = (int) [1, MAX]" \
        ", layout=(string)interleaved"

#define LC3_AUDIO_CAPS_STRING "audio/x-lc3, " \
        "rate = (int) { " SAMPLE_RATES " }, " \
        "channels = (int) [1, MAX], " \
        "frame-bytes = (int) [" FRAME_BYTES_RANGE "], " \
        "frame-duration-us = (int) { " FRAME_DURATIONS "}, " \
        "framed=(boolean) true"

#define LC3_AUDIO_CAPS_STRING_10000US "audio/x-lc3, " \
        "rate = (int) { " SAMPLE_RATES " }, " \
        "channels = (int) [1, MAX], " \
        "frame-bytes = (int) [" FRAME_BYTES_RANGE "], " \
        "frame-duration-us = (int) 10000, " \
        "framed=(boolean) true"

#define LC3_AUDIO_CAPS_STRING_7500US "audio/x-lc3, " \
        "rate = (int) { " SAMPLE_RATES " }, " \
        "channels = (int) [1, MAX], " \
        "frame-bytes = (int) [" FRAME_BYTES_RANGE "], " \
        "frame-duration-us = (int)  7500, " \
        "framed=(boolean) true"

static GstStaticPadTemplate sinktemplate_10000us =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (LC3_AUDIO_CAPS_STRING_10000US));

static GstStaticPadTemplate sinktemplate_7500us =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (LC3_AUDIO_CAPS_STRING_7500US));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (RAW_AUDIO_CAPS_STRING));

static GstStaticPadTemplate dec_srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (LC3_AUDIO_CAPS_STRING));

static GstStaticPadTemplate dec_sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (RAW_AUDIO_CAPS_STRING));

/* a fixed array of the size `frame-bytes` (i.e. 200)
 * contains the first few bytes of the encoded output
 * for an input buffer containing all 1's e.g. {'1','1','1','1' ....}
 */
static guint8 lc3_encoded_buff_48K[200] = {
  0x16, 0x34, 0x7b, 0x8f, 0x5f, 0xd4, 0xf0, 0xa8, 0x34, 0x7f, 0xd1, 0xc7,
  0x55, 0xdc, 0x1a, 0x85, 0x77, 0x8d, 0xb1, 0xb7, 0x78, 0x2c, 0x20, 0x88,
  0x87, 0xd3, 0x4d, 0xb7, 0xf5, 0x1a, 0x15, 0x7d, 0xc1, 0xde, 0x25, 0xca,
  0x94, 0x80, 0x1d, 0x95, 0xbd, 0xf3, 0x50, 0x01, 0x64, 0xe2, 0x60, 0x28,
  0xec, 0xd3, 0xf6, 0x72, 0x2b, 0xf2, 0x6d, 0xf0, 0x83, 0xb4, 0x68, 0x97,
  0x7e, 0x6f, 0x49, 0xc6, 0x38, 0x79, 0x9e, 0xa8, 0x49, 0xab, 0xfc, 0xca,
  0xb8, 0x5c, 0xc6, 0xa5, 0xd9, 0x6e, 0xb4, 0xd2, 0x6a, 0x79, 0x17, 0x29,
  0xac, 0x70, 0x32, 0x6b, 0x13, 0x1b, 0x65, 0xdf, 0xc8, 0x6e, 0x81, 0xa4,
  0xe2, 0x8e, 0xd6, 0x4d, 0xe7, 0x30, 0xdc, 0x02, 0x12, 0xbb, 0x8c, 0x4d,
  0x11, 0x82, 0x66, 0xfa, 0x23, 0xa7, 0xcc, 0xd0, 0x35, 0x2b, 0x1d, 0x30,
  0x09, 0x52, 0x35, 0xf1, 0x3f, 0xc9, 0xb4, 0x52, 0xb5, 0x2b, 0x52, 0xb5,
  0x2b, 0x52, 0xb5, 0x2b, 0x52, 0xb5, 0x2b, 0x52, 0xb5, 0x2b, 0x52, 0xb5,
  0x2b, 0x52, 0xb5, 0x2b, 0x52, 0xb5, 0x55, 0xa9, 0x5a, 0x94, 0x6a, 0x29,
  0x8d, 0x59, 0x4d, 0xd6, 0x75, 0x53, 0x59, 0xd4, 0x4b, 0x64, 0x72, 0xa6,
  0x9d, 0x19, 0x4c, 0xae, 0xea, 0xd1, 0xc5, 0x91, 0x37, 0x50, 0x0e, 0xea,
  0xfb, 0xbb, 0x6b, 0x49, 0xee, 0xe3, 0x91, 0x96, 0xe2, 0x7a, 0x39, 0x84,
  0x1d, 0x17, 0xb8, 0x92, 0x34, 0x3c, 0x86, 0x3c
};

static guint8 lc3_encoded_buff_48K_7500us[200] = {
  0x08, 0x90, 0x6b, 0x8b, 0x5d, 0x8e, 0x39, 0x55, 0x6c, 0x78, 0xb9, 0xed,
  0x00, 0x10, 0xeb, 0x7a, 0x67, 0x97, 0x4c, 0x59, 0xf4, 0xde, 0x4e, 0xc6,
  0x21, 0x9f, 0xf0, 0x83, 0x63, 0xd1, 0xa3, 0xe2, 0x28, 0x11, 0x30, 0xa0,
  0xf9, 0xa7, 0x6c, 0x38, 0x4c, 0xfb, 0xf0, 0xc3, 0x68, 0x70, 0xa3, 0x6e,
  0x3c, 0x4f, 0x8b, 0xd1, 0xd9, 0x67, 0x9b, 0x2c, 0x2b, 0x03, 0xca, 0xc8,
  0xa6, 0x62, 0xb1, 0xb1, 0xe2, 0x8b, 0x29, 0x5e, 0xd2, 0x6e, 0xaf, 0x3e,
  0xc3, 0x04, 0x0f, 0x16, 0xc8, 0xb4, 0xaf, 0x37, 0x3c, 0x64, 0x99, 0x52,
  0xb8, 0x55, 0x0e, 0x23, 0x6f, 0xf2, 0x1c, 0xc1, 0x10, 0xad, 0xd2, 0x41,
  0x55, 0x2a, 0xad, 0x2a, 0x96, 0xab, 0x4a, 0xa5, 0x55, 0xaa, 0xa5, 0x55,
  0xaa, 0xaa, 0xaa, 0xad, 0x56, 0x95, 0x56, 0xab, 0x4a, 0xd5, 0xab, 0x4a,
  0xd5, 0x68, 0x8b, 0x99, 0x5e, 0xaa, 0x5c, 0x60, 0x83, 0xcd, 0x99, 0x1b,
  0x69, 0xf7, 0xd0, 0xfa, 0x04, 0xeb, 0xb8, 0x24, 0xf1, 0x59, 0x00, 0xca,
  0x20, 0xe8, 0x38, 0x3c, 0x54, 0xac, 0x08, 0x90, 0x6b, 0x8b, 0x5d, 0x8e,
  0x39, 0x55, 0x6c, 0x78, 0xb9, 0xed, 0x00, 0x10, 0xeb, 0x7a, 0x67, 0x97,
  0x4c, 0x59, 0xf4, 0xde, 0x4e, 0xc6, 0x21, 0x9f, 0xf0, 0x83, 0x63, 0xd1,
  0xa3, 0xe2, 0x28, 0x11, 0x30, 0xa0, 0xf9, 0xa7, 0x6c, 0x38, 0x4c, 0xfb,
  0xf0, 0xc3, 0x68, 0x70, 0xa3, 0x6e, 0x3c, 0x4f
};


static guint8 lc3_encoded_buff_24K[200] = {
  0x08, 0xb8, 0xd1, 0xf2, 0xa8, 0x25, 0x52, 0x16, 0x75, 0x74, 0xab, 0x3d,
  0xae, 0x0f, 0xed, 0x0a, 0xfe, 0x7a, 0xf4, 0x16, 0x85, 0x14, 0x6f, 0x12,
  0x42, 0x6f, 0xdc, 0xea, 0x7e, 0x55, 0x01, 0x0c, 0x7d, 0x70, 0x91, 0x9d,
  0x42, 0xd9, 0xc2, 0x1e, 0x37, 0xdd, 0x27, 0xb6, 0x6e, 0x21, 0x48, 0xc0,
  0x6d, 0xe8, 0x56, 0xe2, 0x62, 0x56, 0x5b, 0x89, 0x0b, 0x5d, 0x4c, 0xc9,
  0x1e, 0x37, 0xe8, 0x7f, 0xb3, 0xa4, 0x32, 0xee, 0xce, 0x41, 0x26, 0x46,
  0x75, 0x49, 0xec, 0xdd, 0x7e, 0xed, 0x10, 0x84, 0xc8, 0x74, 0xac, 0xbc,
  0xff, 0x7b, 0x3b, 0x9e, 0xf8, 0xb7, 0xee, 0x26, 0xe6, 0xa5, 0xc0, 0xfb,
  0x4b, 0x2f, 0x90, 0x4c, 0x68, 0x7d, 0x57, 0x2e, 0x5a, 0xba, 0xaa, 0x45,
  0xf3, 0xba, 0xae, 0x5c, 0x91, 0xa8, 0xa2, 0x13, 0x74, 0x6c, 0xa2, 0x15,
  0x25, 0x6e, 0xb8, 0x26, 0x79, 0x8c, 0x3a, 0xe5, 0x55, 0x55, 0xcd, 0xb4,
  0xe5, 0x24, 0xd3, 0xfa, 0x89, 0xb0, 0x33, 0x59, 0x55, 0x45, 0x19, 0x92,
  0xad, 0xb2, 0xdb, 0x63, 0xc8, 0x19, 0xae, 0xb2, 0x0e, 0x23, 0xb9, 0x15,
  0x37, 0x16, 0xa4, 0xbc, 0xcc, 0xf4, 0x48, 0x4a, 0x50, 0x8e, 0x20, 0xad,
  0x8c, 0xb5, 0x9c, 0x45, 0xcd, 0xcc, 0xea, 0xd4, 0xc9, 0xa0, 0xc9, 0xac,
  0x57, 0x18, 0x10, 0x9c, 0xc3, 0x1d, 0x2d, 0xb2, 0x87, 0x0c, 0x3b, 0xe9,
  0xe3, 0xbf, 0x24, 0x08, 0x47, 0x07, 0x15, 0xde
};

static GstElement *
setup_lc3enc (GstStaticPadTemplate * srctmpl, GstStaticPadTemplate * sinktmpl)
{
  GstElement *lc3enc;

  GST_DEBUG ("setup_lc3enc");
  lc3enc = gst_check_setup_element ("lc3enc");

  srcpad = gst_check_setup_src_pad (lc3enc, srctmpl);
  sinkpad = gst_check_setup_sink_pad (lc3enc, sinktmpl);

  gst_pad_set_active (srcpad, TRUE);
  gst_pad_set_active (sinkpad, TRUE);

  return lc3enc;
}


static void
cleanup_lc3enc (GstElement * lc3enc)
{
  GST_DEBUG ("cleanup_lc3enc");
  gst_element_set_state (lc3enc, GST_STATE_NULL);

  gst_pad_set_active (srcpad, FALSE);
  gst_pad_set_active (sinkpad, FALSE);

  gst_check_teardown_src_pad (lc3enc);
  gst_check_teardown_sink_pad (lc3enc);
  gst_check_teardown_element (lc3enc);
}

static int
get_delay_samples (gint rate, gint frame_duration_us)
{
  /* delay is
     - 2.5ms for 10ms
     - 4ms for 7.5ms
   */
  int frame_samples = (rate * frame_duration_us) / GST_MSECOND;

  if (frame_duration_us == 10000) {
    return (2.5 * frame_samples) / 10;
  } else if (frame_duration_us == 7500) {
    return (4 * frame_samples) / 7.5;
  }
  return 0;
}

static void
enc_buffer_test (gint rate, gint channels, gint nbuffers, int frame_dur_us,
    guint8 * expected_output)
{
  GstElement *lc3enc;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  guint i, num_buffers;
  GstStructure *s;
  gint frame_duration_us;
  gint frame_bytes;
  guint outbuffers;
  guint64 chmask;
  int num;
  GstStaticPadTemplate *sinktmpl =
      frame_dur_us == 7500 ? &sinktemplate_7500us : &sinktemplate_10000us;

  /* cleanup any stale buffers from previous run */
  g_list_free (buffers);
  buffers = NULL;

  lc3enc = setup_lc3enc (&srctemplate, sinktmpl);
  fail_unless (gst_element_set_state (lc3enc,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  /* corresponds to audio buffer mentioned in the caps */
  inbuffer = gst_buffer_new_and_alloc (1024 * nbuffers * 2 * channels);
  /* makes valgrind's memcheck happier */
  gst_buffer_memset (inbuffer, 0, '1', 1024 * nbuffers * 2 * channels);

  chmask = gst_audio_channel_get_fallback_mask (channels);

  caps =
      gst_caps_new_simple ("audio/x-raw", "rate", G_TYPE_INT, rate, "channels",
      G_TYPE_INT, channels, "format", G_TYPE_STRING, "S16LE", "layout",
      G_TYPE_STRING, "interleaved", "channel-mask", GST_TYPE_BITMASK, chmask,
      NULL);

  gst_check_setup_events (srcpad, lc3enc, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  GST_BUFFER_TIMESTAMP (inbuffer) = 0;
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless (gst_pad_push (srcpad, inbuffer) == GST_FLOW_OK);

  /* send eos to have all flushed if needed */
  fail_unless (gst_pad_push_event (srcpad, gst_event_new_eos ()) == TRUE);

  /* get the caps on the srcpad of lc3enc */
  fail_unless (gst_pad_has_current_caps (sinkpad));
  caps = gst_pad_get_current_caps (sinkpad);
  fail_unless (gst_caps_is_fixed (caps));
  s = gst_caps_get_structure (caps, 0);
  fail_if (s == NULL);

  gst_structure_get_int (s, "frame-duration-us", &frame_duration_us);
  fail_unless (frame_duration_us == frame_dur_us);
  gst_structure_get_int (s, "frame-bytes", &frame_bytes);
  fail_if (frame_bytes == 0);

  gst_caps_unref (caps);
  num = get_delay_samples (rate, frame_duration_us) + (1024 * nbuffers);
  outbuffers =
      gst_util_uint64_scale_int_ceil (1, num,
      ((rate * frame_duration_us) / GST_MSECOND));
  num_buffers = g_list_length (buffers);
  /* check the num of outbuffers is as expected */
  fail_unless_equals_int (num_buffers, outbuffers);

  /* clean up buffers */
  for (i = 0; i < num_buffers; ++i) {
    GstMapInfo map;
    gsize size;
    guint8 *data;

    outbuffer = GST_BUFFER (buffers->data);
    fail_if (outbuffer == NULL);

    gst_buffer_map (outbuffer, &map, GST_MAP_READ);
    data = map.data;
    size = map.size;

    /* check the size of each out buffer per channel
     * is same as frame_bytes
     */
    fail_unless_equals_int (size / channels, frame_bytes);
    if (i == 0) {
      /* verify the first channel data */
      fail_unless (0 == memcmp (data, expected_output, frame_bytes));

      /* verify the first channel data */
      fail_unless (0 == memcmp (data + (frame_bytes * (channels - 1)),
              expected_output, frame_bytes));
    }

    gst_buffer_unmap (outbuffer, &map);

    buffers = g_list_remove (buffers, outbuffer);

    ASSERT_BUFFER_REFCOUNT (outbuffer, "outbuffer", 1);
    gst_buffer_unref (outbuffer);
    outbuffer = NULL;
  }

  cleanup_lc3enc (lc3enc);
  g_list_free (buffers);
  buffers = NULL;
}

static GstBuffer *
create_test_buffer (guint64 num, guint64 size)
{
  GstBuffer *buffer;
  guint64 *data = g_malloc (sizeof (guint64));

  *data = num;

  buffer = gst_buffer_new_wrapped (data, size);

  GST_BUFFER_PTS (buffer) = num * 10 * GST_MSECOND;
  GST_BUFFER_DURATION (buffer) = 10 * GST_MSECOND;

  return buffer;
}

static void
dec_plc_test (void)
{
  GstElement *dec;
  GstClockTime pts;
  GstClockTime dur;
  GstBuffer *buf;
  GstHarness *h;
  GstCaps *caps;

  dec = gst_check_setup_element ("lc3dec");
  g_object_set (dec, "plc", TRUE, NULL);

  dur = GST_MSECOND * 10;
  h = gst_harness_new_full (dec, &dec_srctemplate, "sink", &dec_sinktemplate,
      "src");
  caps =
      gst_caps_from_string
      ("audio/x-lc3,channels=2,frame-bytes=100,frame-duration-us=10000,framed=true,rate=48000");

  gst_check_setup_events (h->srcpad, dec, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  pts = 0;
  gst_harness_push (h, create_test_buffer (0, 2 * 100));
  buf = gst_harness_pull (h);
  fail_unless_equals_int (pts, GST_BUFFER_PTS (buf));
  fail_unless_equals_int (dur, GST_BUFFER_DURATION (buf));
  /* first buffer is marked with discont in the gstaudiodecoder */
  fail_unless (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT));
  gst_buffer_unref (buf);

  pts = 10 * GST_MSECOND;
  fail_unless (gst_harness_push_event (h, gst_event_new_gap (pts, dur)));
  buf = gst_harness_try_pull (h);
  fail_unless (buf);
  fail_unless_equals_int (pts, GST_BUFFER_PTS (buf));
  fail_unless_equals_int (dur, GST_BUFFER_DURATION (buf));
  fail_unless (!GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT));
  gst_buffer_unref (buf);

  pts = 2 * 10 * GST_MSECOND;
  buf = create_test_buffer (2, 2 * 100);
  GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
  gst_harness_push (h, buf);
  buf = gst_harness_pull (h);
  fail_unless_equals_int (pts, GST_BUFFER_PTS (buf));
  fail_unless_equals_int (dur, GST_BUFFER_DURATION (buf));
  fail_unless (!GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT));

  gst_buffer_unref (buf);
  gst_harness_teardown (h);
}

GST_START_TEST (test_48k_8ch_10000us)
{
  enc_buffer_test (48000, 8, 100, 10000, lc3_encoded_buff_48K);
}

GST_END_TEST;

GST_START_TEST (test_48k_8ch_7500us)
{
  enc_buffer_test (48000, 8, 100, 7500, lc3_encoded_buff_48K_7500us);
}

GST_END_TEST;

GST_START_TEST (test_24k_4ch_10000us)
{
  enc_buffer_test (24000, 4, 150, 10000, lc3_encoded_buff_24K);
}

GST_END_TEST;

GST_START_TEST (test_dec_plc)
{
  dec_plc_test ();
}

GST_END_TEST;

static Suite *
lc3_suite (void)
{
  Suite *s = suite_create ("lc3");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_48k_8ch_10000us);
  tcase_add_test (tc_chain, test_48k_8ch_7500us);
  tcase_add_test (tc_chain, test_24k_4ch_10000us);
  tcase_add_test (tc_chain, test_dec_plc);

  return s;
}

GST_CHECK_MAIN (lc3);
