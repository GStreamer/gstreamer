/* GStreamer
 *
 * unit test for y4menc
 *
 * Copyright (C) <2006> Mark Nauwelaerts <manauw@skynet.be>
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
#include <gst/video/video.h>

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
static GstPad *mysrcpad, *mysinkpad;

#define VIDEO_CAPS_STRING "video/x-raw, " \
                           "format = (string) I420, "\
                           "width = (int) 384, " \
                           "height = (int) 288, " \
                           "framerate = (fraction) 25/1, " \
                           "pixel-aspect-ratio = (fraction) 1/1"

#define Y4M_CAPS_STRING "application/x-yuv4mpeg, " \
                        "y4mversion = (int) 2"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (Y4M_CAPS_STRING));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VIDEO_CAPS_STRING));


static GstElement *
setup_y4menc (void)
{
  GstElement *y4menc;

  GST_DEBUG ("setup_y4menc");
  y4menc = gst_check_setup_element ("y4menc");
  mysrcpad = gst_check_setup_src_pad (y4menc, &srctemplate);
  mysinkpad = gst_check_setup_sink_pad (y4menc, &sinktemplate);
  gst_pad_set_active (mysrcpad, TRUE);
  gst_pad_set_active (mysinkpad, TRUE);

  return y4menc;
}

static void
cleanup_y4menc (GstElement * y4menc)
{
  GST_DEBUG ("cleanup_y4menc");
  gst_element_set_state (y4menc, GST_STATE_NULL);

  gst_pad_set_active (mysrcpad, FALSE);
  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_src_pad (y4menc);
  gst_check_teardown_sink_pad (y4menc);
  gst_check_teardown_element (y4menc);
}

GST_START_TEST (test_y4m)
{
  GstElement *y4menc;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  int i, num_buffers, size;
  const gchar *data0 = "YUV4MPEG2 W384 H288 Ip F25:1 A1:1\n";
  const gchar *data1 = "YUV4MPEG2 C420jpeg W384 H288 Ip F25:1 A1:1\n";
  const gchar *data2 = "FRAME\n";

  y4menc = setup_y4menc ();
  fail_unless (gst_element_set_state (y4menc,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  /* corresponds to I420 buffer for the size mentioned in the caps */
  size = 384 * 288 * 3 / 2;
  inbuffer = gst_buffer_new_and_alloc (size);
  /* makes valgrind's memcheck happier */
  gst_buffer_memset (inbuffer, 0, 0, size);
  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_check_setup_events (mysrcpad, y4menc, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);
  GST_BUFFER_TIMESTAMP (inbuffer) = 0;
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);

  num_buffers = g_list_length (buffers);
  fail_unless (num_buffers == 1);

  /* clean up buffers */
  for (i = 0; i < num_buffers; ++i) {
    GstMapInfo map;
    gchar *data;
    gsize outsize;

    outbuffer = GST_BUFFER (buffers->data);
    fail_if (outbuffer == NULL);

    switch (i) {
      case 0:
        gst_buffer_map (outbuffer, &map, GST_MAP_READ);
        outsize = map.size;
        data = (gchar *) map.data;

        fail_unless (outsize > size);
        fail_unless (memcmp (data, data0, strlen (data0)) == 0 ||
            memcmp (data, data1, strlen (data1)) == 0);
        /* so we know there is a newline */
        data = strchr (data, '\n');
        fail_unless (data != NULL);
        data++;
        fail_unless (memcmp (data2, data, strlen (data2)) == 0);
        data += strlen (data2);
        /* remainder must be frame data */
        fail_unless (data - (gchar *) map.data + size == outsize);
        gst_buffer_unmap (outbuffer, &map);
        break;
      default:
        break;
    }
    buffers = g_list_remove (buffers, outbuffer);

    ASSERT_BUFFER_REFCOUNT (outbuffer, "outbuffer", 1);
    gst_buffer_unref (outbuffer);
    outbuffer = NULL;
  }

  cleanup_y4menc (y4menc);
  g_list_free (buffers);
  buffers = NULL;
}

GST_END_TEST;

#include "y4mdata.c"

GST_START_TEST (test_y4m_i420_padded_square)
{
  GstHarness *h;
  GstBuffer *inbuf, *outbuf;
  GstFlowReturn ret;
  GstVideoInfo info;
  gsize len;
  guint cmp;

  h = gst_harness_new ("y4menc");

  gst_harness_set_src_caps_str (h,
      "video/x-raw,format=I420,width=15,height=15,framerate=(fraction)30/1");
  gst_harness_set_sink_caps_str (h, "application/x-yuv4mpeg,y4mversion=2");

  gst_video_info_set_format (&info, GST_VIDEO_FORMAT_I420, 15, 15);

  inbuf = gst_buffer_new_and_alloc (GST_VIDEO_INFO_SIZE (&info));
  fail_unless (inbuf);
  len = gst_buffer_fill (inbuf, 0, red_box_i420_15x15_yuv,
      red_box_i420_15x15_yuv_len);
  fail_unless (len == red_box_i420_15x15_yuv_len);

  ret = gst_harness_push (h, inbuf);
  fail_unless (ret == GST_FLOW_OK, "GstFlowReturn was %s",
      gst_flow_get_name (ret));

  outbuf = gst_harness_pull (h);
  fail_unless (outbuf);

  cmp = gst_buffer_memcmp (outbuf, 0, red_box_y4m, red_box_y4m_len);
  fail_unless (cmp == 0);

  gst_buffer_unref (outbuf);

  gst_harness_teardown (h);
}

GST_END_TEST;

static Suite *
y4menc_suite (void)
{
  Suite *s = suite_create ("y4menc");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_y4m);
  tcase_add_test (tc_chain, test_y4m_i420_padded_square);

  return s;
}

GST_CHECK_MAIN (y4menc);
