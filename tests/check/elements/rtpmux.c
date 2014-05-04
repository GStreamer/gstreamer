/* GStreamer
 *
 * unit test for rtpmux elements
 *
 * Copyright 2009 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2009 Nokia Corp.
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
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/gst.h>

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp"));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp"));

typedef void (*check_cb) (GstPad * pad, int i);

static gboolean
query_func (GstPad * pad, GstObject * noparent, GstQuery * query)
{
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps **caps = g_object_get_data (G_OBJECT (pad), "caps");

      fail_unless (caps != NULL && *caps != NULL);
      gst_query_set_caps_result (query, *caps);
      break;
    }
    case GST_QUERY_ACCEPT_CAPS:
      gst_query_set_accept_caps_result (query, TRUE);
      break;
    default:
      break;
  }

  return TRUE;
}

static gboolean
event_func (GstPad * pad, GstObject * noparent, GstEvent * event)
{
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;
      GstCaps **caps2 = g_object_get_data (G_OBJECT (pad), "caps");

      gst_event_parse_caps (event, &caps);
      fail_unless (caps2 != NULL && *caps2 != NULL);
      fail_unless (gst_caps_is_fixed (caps));
      fail_unless (gst_caps_is_fixed (*caps2));
      fail_unless (gst_caps_is_equal_fixed (caps, *caps2));
      break;
    }
    default:
      break;
  }

  gst_event_unref (event);

  return TRUE;
}

static void
test_basic (const gchar * elem_name, const gchar * sink2, int count,
    check_cb cb)
{
  GstElement *rtpmux = NULL;
  GstPad *reqpad1 = NULL;
  GstPad *reqpad2 = NULL;
  GstPad *src1 = NULL;
  GstPad *src2 = NULL;
  GstPad *sink = NULL;
  GstBuffer *inbuf = NULL;
  GstCaps *src1caps = NULL;
  GstCaps *src2caps = NULL;
  GstCaps *sinkcaps = NULL;
  GstCaps *caps;
  GstSegment segment;
  int i;

  rtpmux = gst_check_setup_element (elem_name);

  reqpad1 = gst_element_get_request_pad (rtpmux, "sink_1");
  fail_unless (reqpad1 != NULL);
  reqpad2 = gst_element_get_request_pad (rtpmux, sink2);
  fail_unless (reqpad2 != NULL);
  sink = gst_check_setup_sink_pad_by_name (rtpmux, &sinktemplate, "src");

  src1 = gst_pad_new_from_static_template (&srctemplate, "src");
  src2 = gst_pad_new_from_static_template (&srctemplate, "src");
  fail_unless (gst_pad_link (src1, reqpad1) == GST_PAD_LINK_OK);
  fail_unless (gst_pad_link (src2, reqpad2) == GST_PAD_LINK_OK);
  gst_pad_set_query_function (src1, query_func);
  gst_pad_set_query_function (src2, query_func);
  gst_pad_set_query_function (sink, query_func);
  gst_pad_set_event_function (sink, event_func);
  g_object_set_data (G_OBJECT (src1), "caps", &src1caps);
  g_object_set_data (G_OBJECT (src2), "caps", &src2caps);
  g_object_set_data (G_OBJECT (sink), "caps", &sinkcaps);

  src1caps = gst_caps_new_simple ("application/x-rtp",
      "clock-rate", G_TYPE_INT, 1, "ssrc", G_TYPE_UINT, 11, NULL);
  src2caps = gst_caps_new_simple ("application/x-rtp",
      "clock-rate", G_TYPE_INT, 2, "ssrc", G_TYPE_UINT, 12, NULL);
  sinkcaps = gst_caps_new_simple ("application/x-rtp",
      "clock-rate", G_TYPE_INT, 3, "ssrc", G_TYPE_UINT, 13, NULL);

  caps = gst_pad_peer_query_caps (src1, NULL);
  fail_unless (gst_caps_is_empty (caps));
  gst_caps_unref (caps);

  gst_caps_set_simple (src2caps, "clock-rate", G_TYPE_INT, 3, NULL);
  caps = gst_pad_peer_query_caps (src1, NULL);
  fail_unless (gst_caps_is_equal (caps, sinkcaps));
  gst_caps_unref (caps);

  g_object_set (rtpmux, "seqnum-offset", 100, "timestamp-offset", 1000,
      "ssrc", 55, NULL);

  fail_unless (gst_element_set_state (rtpmux,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS);
  gst_pad_set_active (sink, TRUE);
  gst_pad_set_active (src1, TRUE);
  gst_pad_set_active (src2, TRUE);

  fail_unless (gst_pad_push_event (src1,
          gst_event_new_stream_start ("stream1")));
  fail_unless (gst_pad_push_event (src2,
          gst_event_new_stream_start ("stream2")));

  gst_caps_set_simple (sinkcaps,
      "payload", G_TYPE_INT, 98, "seqnum-base", G_TYPE_UINT, 100,
      "clock-base", G_TYPE_UINT, 1000, "ssrc", G_TYPE_UINT, 66, NULL);
  caps = gst_caps_new_simple ("application/x-rtp",
      "payload", G_TYPE_INT, 98, "clock-rate", G_TYPE_INT, 3,
      "seqnum-base", G_TYPE_UINT, 56, "clock-base", G_TYPE_UINT, 57,
      "ssrc", G_TYPE_UINT, 66, NULL);
  fail_unless (gst_pad_set_caps (src1, caps));

  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.start = 100000;
  fail_unless (gst_pad_push_event (src1, gst_event_new_segment (&segment)));
  segment.start = 0;
  fail_unless (gst_pad_push_event (src2, gst_event_new_segment (&segment)));


  for (i = 0; i < count; i++) {
    GstRTPBuffer rtpbuffer = GST_RTP_BUFFER_INIT;

    inbuf = gst_rtp_buffer_new_allocate (10, 0, 0);
    GST_BUFFER_PTS (inbuf) = i * 1000 + 100000;
    GST_BUFFER_DURATION (inbuf) = 1000;

    gst_rtp_buffer_map (inbuf, GST_MAP_WRITE, &rtpbuffer);

    gst_rtp_buffer_set_version (&rtpbuffer, 2);
    gst_rtp_buffer_set_payload_type (&rtpbuffer, 98);
    gst_rtp_buffer_set_ssrc (&rtpbuffer, 44);
    gst_rtp_buffer_set_timestamp (&rtpbuffer, 200 + i);
    gst_rtp_buffer_set_seq (&rtpbuffer, 2000 + i);
    gst_rtp_buffer_unmap (&rtpbuffer);
    fail_unless (gst_pad_push (src1, inbuf) == GST_FLOW_OK);

    if (buffers)
      fail_unless (GST_BUFFER_PTS (buffers->data) == i * 1000, "%lld",
          GST_BUFFER_PTS (buffers->data));

    cb (src2, i);

    g_list_foreach (buffers, (GFunc) gst_buffer_unref, NULL);
    g_list_free (buffers);
    buffers = NULL;
  }


  gst_pad_set_active (sink, FALSE);
  gst_pad_set_active (src1, FALSE);
  gst_pad_set_active (src2, FALSE);
  fail_unless (gst_element_set_state (rtpmux,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);
  gst_check_teardown_pad_by_name (rtpmux, "src");
  gst_object_unref (reqpad1);
  gst_object_unref (reqpad2);
  gst_check_teardown_pad_by_name (rtpmux, "sink_1");
  gst_check_teardown_pad_by_name (rtpmux, sink2);
  gst_element_release_request_pad (rtpmux, reqpad1);
  gst_element_release_request_pad (rtpmux, reqpad2);

  gst_caps_unref (caps);
  gst_caps_replace (&src1caps, NULL);
  gst_caps_replace (&src2caps, NULL);
  gst_caps_replace (&sinkcaps, NULL);

  gst_check_teardown_element (rtpmux);
}

static void
basic_check_cb (GstPad * pad, int i)
{
  GstRTPBuffer rtpbuffer = GST_RTP_BUFFER_INIT;
  fail_unless (buffers && g_list_length (buffers) == 1);

  gst_rtp_buffer_map (buffers->data, GST_MAP_READ, &rtpbuffer);
  fail_unless (gst_rtp_buffer_get_ssrc (&rtpbuffer) == 66);
  fail_unless (gst_rtp_buffer_get_timestamp (&rtpbuffer) ==
      200 - 57 + 1000 + i);
  fail_unless (gst_rtp_buffer_get_seq (&rtpbuffer) == 100 + 1 + i);
  gst_rtp_buffer_unmap (&rtpbuffer);
}


GST_START_TEST (test_rtpmux_basic)
{
  test_basic ("rtpmux", "sink_2", 10, basic_check_cb);
}

GST_END_TEST;

GST_START_TEST (test_rtpdtmfmux_basic)
{
  test_basic ("rtpdtmfmux", "sink_2", 10, basic_check_cb);
}

GST_END_TEST;

static void
lock_check_cb (GstPad * pad, int i)
{
  GstBuffer *inbuf;

  if (i % 2) {
    fail_unless (buffers == NULL);
  } else {
    GstRTPBuffer rtpbuffer = GST_RTP_BUFFER_INIT;

    fail_unless (buffers && g_list_length (buffers) == 1);
    gst_rtp_buffer_map (buffers->data, GST_MAP_READ, &rtpbuffer);
    fail_unless (gst_rtp_buffer_get_ssrc (&rtpbuffer) == 66);
    fail_unless (gst_rtp_buffer_get_timestamp (&rtpbuffer) ==
        200 - 57 + 1000 + i);
    fail_unless (gst_rtp_buffer_get_seq (&rtpbuffer) == 100 + 1 + i);
    gst_rtp_buffer_unmap (&rtpbuffer);

    inbuf = gst_rtp_buffer_new_allocate (10, 0, 0);
    GST_BUFFER_PTS (inbuf) = i * 1000 + 500;
    GST_BUFFER_DURATION (inbuf) = 1000;
    gst_rtp_buffer_map (inbuf, GST_MAP_WRITE, &rtpbuffer);
    gst_rtp_buffer_set_version (&rtpbuffer, 2);
    gst_rtp_buffer_set_payload_type (&rtpbuffer, 98);
    gst_rtp_buffer_set_ssrc (&rtpbuffer, 44);
    gst_rtp_buffer_set_timestamp (&rtpbuffer, 200 + i);
    gst_rtp_buffer_set_seq (&rtpbuffer, 2000 + i);
    gst_rtp_buffer_unmap (&rtpbuffer);
    fail_unless (gst_pad_push (pad, inbuf) == GST_FLOW_OK);


    g_list_foreach (buffers, (GFunc) gst_buffer_unref, NULL);
    g_list_free (buffers);
    buffers = NULL;
  }
}

GST_START_TEST (test_rtpdtmfmux_lock)
{
  test_basic ("rtpdtmfmux", "priority_sink_2", 10, lock_check_cb);
}

GST_END_TEST;

static Suite *
rtpmux_suite (void)
{
  Suite *s = suite_create ("rtpmux");
  TCase *tc_chain;

  tc_chain = tcase_create ("rtpmux_basic");
  tcase_add_test (tc_chain, test_rtpmux_basic);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("rtpdtmfmux_basic");
  tcase_add_test (tc_chain, test_rtpdtmfmux_basic);
  suite_add_tcase (s, tc_chain);

  tc_chain = tcase_create ("rtpdtmfmux_lock");
  tcase_add_test (tc_chain, test_rtpdtmfmux_lock);
  suite_add_tcase (s, tc_chain);

  return s;
}

GST_CHECK_MAIN (rtpmux)
