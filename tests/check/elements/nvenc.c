/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
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

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, "
        "width = (int) [1, MAX], "
        "height = (int) [1, MAX], " "framerate = (fraction) [0, MAX]"));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, "
        "format = (string) NV12, "
        "width = (int) [1, MAX], "
        "height = (int) [1, MAX], " "framerate = (fraction) [0, MAX]"));

static GstPad *sinkpad, *srcpad;

static GstElement *
setup_nvenc (const gchar * src_caps_str, GstCaps ** srccaps)
{
  GstElement *nvenc;
  GstCaps *caps = NULL;
  GstBus *bus;

  caps = gst_caps_from_string (src_caps_str);
  fail_unless (caps != NULL);

  nvenc = gst_check_setup_element ("nvh264enc");
  fail_unless (nvenc != NULL);
  srcpad = gst_check_setup_src_pad (nvenc, &srctemplate);
  sinkpad = gst_check_setup_sink_pad (nvenc, &sinktemplate);
  gst_pad_set_active (srcpad, TRUE);
  gst_pad_set_active (sinkpad, TRUE);

  bus = gst_bus_new ();
  gst_element_set_bus (nvenc, bus);

  *srccaps = caps;

  buffers = NULL;
  return nvenc;
}

static void
cleanup_nvenc (GstElement * nvenc)
{
  GstBus *bus;

  /* Free parsed buffers */
  gst_check_drop_buffers ();

  bus = GST_ELEMENT_BUS (nvenc);
  gst_bus_set_flushing (bus, TRUE);
  gst_object_unref (bus);

  gst_pad_set_active (srcpad, FALSE);
  gst_pad_set_active (sinkpad, FALSE);
  gst_check_teardown_src_pad (nvenc);
  gst_check_teardown_sink_pad (nvenc);
  gst_check_teardown_element (nvenc);
}

GST_START_TEST (test_encode_simple)
{
  GstElement *nvenc;
  GstBuffer *buffer;
  gint i;
  GList *iter;
  GstCaps *outcaps, *sinkcaps, *srccaps;
  GstSegment seg;

  nvenc =
      setup_nvenc
      ("video/x-raw,format=(string)NV12,width=(int)320,height=(int)240,"
      "framerate=(fraction)25/1,interlace-mode=(string)progressive", &srccaps);

  ASSERT_SET_STATE (nvenc, GST_STATE_PLAYING, GST_STATE_CHANGE_SUCCESS);

  gst_segment_init (&seg, GST_FORMAT_TIME);
  seg.stop = gst_util_uint64_scale (10, GST_SECOND, 25);

  gst_check_setup_events (srcpad, nvenc, srccaps, GST_FORMAT_TIME);
  fail_unless (gst_pad_push_event (srcpad, gst_event_new_segment (&seg)));

  buffer = gst_buffer_new_allocate (NULL, 320 * 240 + 2 * 160 * 120, NULL);
  gst_buffer_memset (buffer, 0, 0, -1);

  for (i = 0; i < 10; i++) {
    GST_BUFFER_TIMESTAMP (buffer) = gst_util_uint64_scale (i, GST_SECOND, 25);
    GST_BUFFER_DURATION (buffer) = gst_util_uint64_scale (1, GST_SECOND, 25);
    fail_unless (gst_pad_push (srcpad, gst_buffer_ref (buffer)) == GST_FLOW_OK);
  }

  gst_buffer_unref (buffer);

  fail_unless (gst_pad_push_event (srcpad, gst_event_new_eos ()));

  /* All buffers must be there now */
  fail_unless_equals_int (g_list_length (buffers), 10);

  outcaps =
      gst_caps_from_string
      ("video/x-h264,width=(int)320,height=(int)240,framerate=(fraction)25/1");

  for (iter = buffers; iter; iter = g_list_next (iter)) {
    buffer = iter->data;

    fail_unless_equals_uint64 (GST_BUFFER_DURATION (buffer),
        gst_util_uint64_scale (1, GST_SECOND, 25));

    sinkcaps = gst_pad_get_current_caps (sinkpad);
    fail_unless (gst_caps_can_intersect (sinkcaps, outcaps));
    gst_caps_unref (sinkcaps);
  }

  gst_caps_unref (outcaps);
  gst_caps_unref (srccaps);

  cleanup_nvenc (nvenc);
}

GST_END_TEST;


GST_START_TEST (test_reuse)
{
  GstElement *nvenc;
  GstBuffer *buffer;
  gint i, loop;
  GList *iter;
  GstCaps *outcaps, *sinkcaps;
  GstSegment seg;
  GstCaps *srccaps;

  nvenc =
      setup_nvenc
      ("video/x-raw,format=(string)NV12,width=(int)320,height=(int)240,"
      "framerate=(fraction)25/1,interlace-mode=(string)progressive", &srccaps);

  for (loop = 0; loop < 2; loop++) {
    ASSERT_SET_STATE (nvenc, GST_STATE_PLAYING, GST_STATE_CHANGE_SUCCESS);

    gst_segment_init (&seg, GST_FORMAT_TIME);
    seg.stop = gst_util_uint64_scale (10, GST_SECOND, 25);

    gst_check_setup_events (srcpad, nvenc, srccaps, GST_FORMAT_TIME);
    fail_unless (gst_pad_push_event (srcpad, gst_event_new_segment (&seg)));

    gst_segment_init (&seg, GST_FORMAT_TIME);
    seg.stop = gst_util_uint64_scale (10, GST_SECOND, 25);

    fail_unless (gst_pad_push_event (srcpad, gst_event_new_segment (&seg)));

    buffer = gst_buffer_new_allocate (NULL, 320 * 240 + 2 * 160 * 120, NULL);
    gst_buffer_memset (buffer, 0, 0, -1);

    for (i = 0; i < 10; i++) {
      GST_BUFFER_TIMESTAMP (buffer) = gst_util_uint64_scale (i, GST_SECOND, 25);
      GST_BUFFER_DURATION (buffer) = gst_util_uint64_scale (1, GST_SECOND, 25);
      fail_unless (gst_pad_push (srcpad,
              gst_buffer_ref (buffer)) == GST_FLOW_OK);
    }

    gst_buffer_unref (buffer);

    fail_unless (gst_pad_push_event (srcpad, gst_event_new_eos ()));

    /* All buffers must be there now */
    fail_unless_equals_int (g_list_length (buffers), 10);

    outcaps =
        gst_caps_from_string
        ("video/x-h264,width=(int)320,height=(int)240,framerate=(fraction)25/1");

    for (iter = buffers; iter; iter = g_list_next (iter)) {
      buffer = iter->data;

      fail_unless_equals_uint64 (GST_BUFFER_DURATION (buffer),
          gst_util_uint64_scale (1, GST_SECOND, 25));

      sinkcaps = gst_pad_get_current_caps (sinkpad);
      fail_unless (gst_caps_can_intersect (sinkcaps, outcaps));
      gst_caps_unref (sinkcaps);
    }
    gst_check_drop_buffers ();
    gst_caps_unref (outcaps);

    ASSERT_SET_STATE (nvenc, GST_STATE_READY, GST_STATE_CHANGE_SUCCESS);
  }

  gst_caps_unref (srccaps);

  cleanup_nvenc (nvenc);
}

GST_END_TEST;


static gboolean
check_nvenc_available (void)
{
  gboolean ret = TRUE;
  GstElement *nvenc;

  nvenc = gst_element_factory_make ("nvh264enc", NULL);
  if (!nvenc) {
    GST_WARNING ("nvh264enc is not available, possibly driver load failure");
    return FALSE;
  }

  /* GST_STATE_READY is meaning that driver could be loaded */
  if (gst_element_set_state (nvenc,
          GST_STATE_PAUSED) != GST_STATE_CHANGE_SUCCESS) {
    GST_WARNING ("cannot open device");
    ret = FALSE;
  }

  gst_element_set_state (nvenc, GST_STATE_NULL);
  gst_object_unref (nvenc);

  return ret;
}

static Suite *
nvenc_suite (void)
{
  Suite *s;
  TCase *tc_chain;

  /* HACK: cuda device init/deinit with fork seems to problematic */
  g_setenv ("CK_FORK", "no", TRUE);

  s = suite_create ("nvenc");
  tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  if (!check_nvenc_available ()) {
    GST_DEBUG ("Skip nvenc test since cannot open device");
    goto end;
  }

  tcase_add_test (tc_chain, test_encode_simple);
  tcase_add_test (tc_chain, test_reuse);

end:
  return s;
}

GST_CHECK_MAIN (nvenc);
