/* GStreamer
 *
 * Copyright (c) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
#include <gst/check/gstharness.h>
#include <gst/check/gstcheck.h>
#include <gst/video/video.h>

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-vp8, "
        "width = (int) [1, MAX], "
        "height = (int) [1, MAX], " "framerate = (fraction) [0, MAX]"));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, "
        "format = (string) I420, "
        "width = (int) [1, MAX], "
        "height = (int) [1, MAX], " "framerate = (fraction) [0, MAX]"));

static GstPad *sinkpad, *srcpad;

static GstElement *
setup_vp8enc (const gchar * src_caps_str)
{
  GstElement *vp8enc;
  GstCaps *srccaps = NULL;
  GstBus *bus;

  if (src_caps_str) {
    srccaps = gst_caps_from_string (src_caps_str);
    fail_unless (srccaps != NULL);
  }

  vp8enc = gst_check_setup_element ("vp8enc");
  fail_unless (vp8enc != NULL);
  srcpad = gst_check_setup_src_pad (vp8enc, &srctemplate);
  sinkpad = gst_check_setup_sink_pad (vp8enc, &sinktemplate);
  gst_pad_set_active (srcpad, TRUE);
  gst_pad_set_active (sinkpad, TRUE);
  gst_check_setup_events (srcpad, vp8enc, srccaps, GST_FORMAT_TIME);

  bus = gst_bus_new ();
  gst_element_set_bus (vp8enc, bus);

  fail_unless (gst_element_set_state (vp8enc,
          GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE,
      "could not set to playing");

  if (srccaps)
    gst_caps_unref (srccaps);

  buffers = NULL;
  return vp8enc;
}

static void
cleanup_vp8enc (GstElement * vp8enc)
{
  GstBus *bus;

  /* Free parsed buffers */
  gst_check_drop_buffers ();

  bus = GST_ELEMENT_BUS (vp8enc);
  gst_bus_set_flushing (bus, TRUE);
  gst_object_unref (bus);

  gst_pad_set_active (srcpad, FALSE);
  gst_pad_set_active (sinkpad, FALSE);
  gst_check_teardown_src_pad (vp8enc);
  gst_check_teardown_sink_pad (vp8enc);
  gst_check_teardown_element (vp8enc);
}


GST_START_TEST (test_encode_simple)
{
  GstElement *vp8enc;
  GstBuffer *buffer;
  gint i;
  GList *l;
  GstCaps *outcaps;
  GstSegment seg;

  vp8enc =
      setup_vp8enc
      ("video/x-raw,format=(string)I420,width=(int)320,height=(int)240,framerate=(fraction)25/1");

  g_object_set (vp8enc, "lag-in-frames", 5, NULL);

  gst_segment_init (&seg, GST_FORMAT_TIME);
  seg.stop = gst_util_uint64_scale (20, GST_SECOND, 25);
  fail_unless (gst_pad_push_event (srcpad, gst_event_new_segment (&seg)));

  buffer = gst_buffer_new_and_alloc (320 * 240 + 2 * 160 * 120);
  gst_buffer_memset (buffer, 0, 0, -1);

  for (i = 0; i < 20; i++) {
    GST_BUFFER_TIMESTAMP (buffer) = gst_util_uint64_scale (i, GST_SECOND, 25);
    GST_BUFFER_DURATION (buffer) = gst_util_uint64_scale (1, GST_SECOND, 25);
    fail_unless (gst_pad_push (srcpad, gst_buffer_ref (buffer)) == GST_FLOW_OK);
  }

  gst_buffer_unref (buffer);

  /* Only 5 buffers are allowed to be queued now */
  fail_unless (g_list_length (buffers) > 15);

  fail_unless (gst_pad_push_event (srcpad, gst_event_new_eos ()));


  /* All buffers must be there now */
  fail_unless_equals_int (g_list_length (buffers), 20);

  outcaps =
      gst_caps_from_string
      ("video/x-vp8,width=(int)320,height=(int)240,framerate=(fraction)25/1");

  for (l = buffers, i = 0; l; l = l->next, i++) {
    buffer = l->data;

    if (i == 0)
      fail_if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT));

    fail_unless_equals_uint64 (GST_BUFFER_TIMESTAMP (buffer),
        gst_util_uint64_scale (i, GST_SECOND, 25));
    fail_unless_equals_uint64 (GST_BUFFER_DURATION (buffer),
        gst_util_uint64_scale (1, GST_SECOND, 25));
  }

  gst_caps_unref (outcaps);

  cleanup_vp8enc (vp8enc);
}

GST_END_TEST;

#define gst_caps_new_i420(w, h) gst_caps_new_i420_full (w, h, 30, 1, 1, 1)
static GstCaps *
gst_caps_new_i420_full (gint width, gint height, gint fps_n, gint fps_d,
    gint par_n, gint par_d)
{
  GstVideoInfo info;
  gst_video_info_init (&info);
  gst_video_info_set_format (&info, GST_VIDEO_FORMAT_I420, width, height);
  GST_VIDEO_INFO_FPS_N (&info) = fps_n;
  GST_VIDEO_INFO_FPS_D (&info) = fps_d;
  GST_VIDEO_INFO_PAR_N (&info) = par_n;
  GST_VIDEO_INFO_PAR_D (&info) = par_d;
  return gst_video_info_to_caps (&info);
}

static GstBuffer *
gst_harness_create_video_buffer_from_info (GstHarness * h, gint value,
    GstVideoInfo * info, GstClockTime timestamp, GstClockTime duration)
{
  GstBuffer *buf;
  gsize size;

  size = GST_VIDEO_INFO_SIZE (info);

  buf = gst_harness_create_buffer (h, size);
  gst_buffer_memset (buf, 0, value, size);
  g_assert (buf != NULL);

  gst_buffer_add_video_meta_full (buf,
      GST_VIDEO_FRAME_FLAG_NONE,
      GST_VIDEO_INFO_FORMAT (info),
      GST_VIDEO_INFO_WIDTH (info),
      GST_VIDEO_INFO_HEIGHT (info),
      GST_VIDEO_INFO_N_PLANES (info), info->offset, info->stride);

  GST_BUFFER_PTS (buf) = timestamp;
  GST_BUFFER_DURATION (buf) = duration;

  return buf;
}

static GstBuffer *
gst_harness_create_video_buffer_full (GstHarness * h, gint value,
    guint width, guint height, GstClockTime timestamp, GstClockTime duration)
{
  GstVideoInfo info;

  gst_video_info_init (&info);
  gst_video_info_set_format (&info, GST_VIDEO_FORMAT_I420, width, height);

  return gst_harness_create_video_buffer_from_info (h, value, &info,
      timestamp, duration);
}

GST_START_TEST (test_encode_simple_when_bitrate_set_to_zero)
{
  GstHarness *h = gst_harness_new_parse ("vp8enc target-bitrate=0");
  GstBuffer *buf;

  gst_harness_set_src_caps (h, gst_caps_new_i420 (320, 240));

  buf = gst_harness_create_video_buffer_full (h, 0x42,
      320, 240, 0, gst_util_uint64_scale (GST_SECOND, 1, 30));
  gst_harness_push (h, buf);
  gst_buffer_unref (gst_harness_pull (h));
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_autobitrate_changes_with_caps)
{
  gint bitrate = 0;
  GstHarness *h = gst_harness_new ("vp8enc");
  gst_harness_set_src_caps (h, gst_caps_new_i420_full (1280, 720, 30, 1, 1, 1));

  /* Default settings for 720p @ 30fps ~1.2Mbps */
  g_object_get (h->element, "target-bitrate", &bitrate, NULL);
  fail_unless_equals_int (bitrate, 1199000);

  /* Change bits-per-pixel 0.036 to give us ~1Mbps */
  g_object_set (h->element, "bits-per-pixel", 0.037, NULL);
  g_object_get (h->element, "target-bitrate", &bitrate, NULL);
  fail_unless_equals_int (bitrate, 1022000);

  /* Halving the framerate should halve the auto bitrate */
  gst_harness_set_src_caps (h, gst_caps_new_i420_full (1280, 720, 15, 1, 1, 1));
  g_object_get (h->element, "target-bitrate", &bitrate, NULL);
  fail_unless_equals_int (bitrate, 511000);

  /* Halving the resolution should quarter the auto bitrate */
  gst_harness_set_src_caps (h, gst_caps_new_i420_full (640, 360, 15, 1, 1, 1));
  g_object_get (h->element, "target-bitrate", &bitrate, NULL);
  fail_unless_equals_int (bitrate, 127000);

  gst_harness_teardown (h);
}

GST_END_TEST;

static Suite *
vp8enc_suite (void)
{
  Suite *s = suite_create ("vp8enc");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_encode_simple);
  tcase_add_test (tc_chain, test_encode_simple_when_bitrate_set_to_zero);
  tcase_add_test (tc_chain, test_autobitrate_changes_with_caps);

  return s;
}

GST_CHECK_MAIN (vp8enc);
