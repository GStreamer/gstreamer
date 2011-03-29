/* GStreamer
 *
 * unit test for gstrtpbin
 *
 * Copyright (C) <2009> Wim Taymans <wim.taymans@gmail.com>
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

GST_START_TEST (test_cleanup_send)
{
  GstElement *rtpbin;
  GstPad *rtp_sink, *rtp_src, *rtcp_src;
  GObject *session;
  gint count = 2;

  rtpbin = gst_element_factory_make ("gstrtpbin", "rtpbin");

  while (count--) {
    /* request session 0 */
    rtp_sink = gst_element_get_request_pad (rtpbin, "send_rtp_sink_0");
    fail_unless (rtp_sink != NULL);
    ASSERT_OBJECT_REFCOUNT (rtp_sink, "rtp_sink", 2);

    /* this static pad should be created automatically now */
    rtp_src = gst_element_get_static_pad (rtpbin, "send_rtp_src_0");
    fail_unless (rtp_src != NULL);
    ASSERT_OBJECT_REFCOUNT (rtp_src, "rtp_src", 2);

    /* we should be able to get an internal session 0 now */
    g_signal_emit_by_name (rtpbin, "get-internal-session", 0, &session);
    fail_unless (session != NULL);
    g_object_unref (session);

    /* get the send RTCP pad too */
    rtcp_src = gst_element_get_request_pad (rtpbin, "send_rtcp_src_0");
    fail_unless (rtcp_src != NULL);
    ASSERT_OBJECT_REFCOUNT (rtcp_src, "rtcp_src", 2);

    gst_element_release_request_pad (rtpbin, rtp_sink);
    /* we should only have our refs to the pads now */
    ASSERT_OBJECT_REFCOUNT (rtp_sink, "rtp_sink", 1);
    ASSERT_OBJECT_REFCOUNT (rtp_src, "rtp_src", 1);
    ASSERT_OBJECT_REFCOUNT (rtcp_src, "rtp_src", 2);

    /* the other pad should be gone now */
    fail_unless (gst_element_get_static_pad (rtpbin, "send_rtp_src_0") == NULL);

    /* internal session should still be there */
    g_signal_emit_by_name (rtpbin, "get-internal-session", 0, &session);
    fail_unless (session != NULL);
    g_object_unref (session);

    /* release the RTCP pad */
    gst_element_release_request_pad (rtpbin, rtcp_src);
    /* we should only have our refs to the pads now */
    ASSERT_OBJECT_REFCOUNT (rtp_sink, "rtp_sink", 1);
    ASSERT_OBJECT_REFCOUNT (rtp_src, "rtp_src", 1);
    ASSERT_OBJECT_REFCOUNT (rtcp_src, "rtp_src", 1);

    /* the session should be gone now */
    g_signal_emit_by_name (rtpbin, "get-internal-session", 0, &session);
    fail_unless (session == NULL);

    /* unref the request pad and the static pad */
    gst_object_unref (rtp_sink);
    gst_object_unref (rtp_src);
    gst_object_unref (rtcp_src);
  }

  gst_object_unref (rtpbin);
}

GST_END_TEST;

typedef struct
{
  guint16 seqnum;
  gboolean pad_added;
  GstPad *pad;
  GMutex *lock;
  GCond *cond;
  GstPad *sinkpad;
  GList *pads;
} CleanupData;

static void
init_data (CleanupData * data)
{
  data->seqnum = 10;
  data->pad_added = FALSE;
  data->lock = g_mutex_new ();
  data->cond = g_cond_new ();
  data->pads = NULL;
}

static void
clean_data (CleanupData * data)
{
  g_list_foreach (data->pads, (GFunc) gst_object_unref, NULL);
  g_list_free (data->pads);
  g_mutex_free (data->lock);
  g_cond_free (data->cond);
}

static guint8 rtp_packet[] = { 0x80, 0x60, 0x94, 0xbc, 0x8f, 0x37, 0x4e, 0xb8,
  0x44, 0xa8, 0xf3, 0x7c, 0x06, 0x6a, 0x0c, 0xce,
  0x13, 0x25, 0x19, 0x69, 0x1f, 0x93, 0x25, 0x9d,
  0x2b, 0x82, 0x31, 0x3b, 0x36, 0xc1, 0x3c, 0x13
};

static GstBuffer *
make_rtp_packet (CleanupData * data)
{
  static GstCaps *caps = NULL;
  GstBuffer *result;
  guint8 *datap;

  if (caps == NULL) {
    caps = gst_caps_from_string ("application/x-rtp,"
        "media=(string)audio, clock-rate=(int)44100, "
        "encoding-name=(string)L16, encoding-params=(string)1, channels=(int)1");
    data->seqnum = 0;
  }

  result = gst_buffer_new_and_alloc (sizeof (rtp_packet));
  datap = GST_BUFFER_DATA (result);
  memcpy (datap, rtp_packet, sizeof (rtp_packet));

  datap[2] = (data->seqnum >> 8) & 0xff;
  datap[3] = data->seqnum & 0xff;

  data->seqnum++;

  gst_buffer_set_caps (result, caps);

  return result;
}

static GstFlowReturn
dummy_chain (GstPad * pad, GstBuffer * buffer)
{
  gst_buffer_unref (buffer);

  return GST_FLOW_OK;
}

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp"));


static GstPad *
make_sinkpad (CleanupData * data)
{
  GstPad *pad;

  pad = gst_pad_new_from_static_template (&sink_factory, "sink");

  gst_pad_set_chain_function (pad, dummy_chain);
  gst_pad_set_active (pad, TRUE);

  data->pads = g_list_prepend (data->pads, pad);

  return pad;
}

static void
pad_added_cb (GstElement * rtpbin, GstPad * pad, CleanupData * data)
{
  GstPad *sinkpad;

  GST_DEBUG ("pad added %s:%s\n", GST_DEBUG_PAD_NAME (pad));

  if (GST_PAD_IS_SINK (pad))
    return;

  fail_unless (data->pad_added == FALSE);

  sinkpad = make_sinkpad (data);
  fail_unless (gst_pad_link (pad, sinkpad) == GST_PAD_LINK_OK);

  g_mutex_lock (data->lock);
  data->pad_added = TRUE;
  data->pad = pad;
  g_cond_signal (data->cond);
  g_mutex_unlock (data->lock);
}

static void
pad_removed_cb (GstElement * rtpbin, GstPad * pad, CleanupData * data)
{
  GST_DEBUG ("pad removed %s:%s\n", GST_DEBUG_PAD_NAME (pad));

  if (data->pad != pad)
    return;

  fail_unless (data->pad_added == TRUE);

  g_mutex_lock (data->lock);
  data->pad_added = FALSE;
  g_cond_signal (data->cond);
  g_mutex_unlock (data->lock);
}

GST_START_TEST (test_cleanup_recv)
{
  GstElement *rtpbin;
  GstPad *rtp_sink;
  CleanupData data;
  GstStateChangeReturn ret;
  GstFlowReturn res;
  GstBuffer *buffer;
  gint count = 2;

  init_data (&data);

  rtpbin = gst_element_factory_make ("gstrtpbin", "rtpbin");

  g_signal_connect (rtpbin, "pad-added", (GCallback) pad_added_cb, &data);
  g_signal_connect (rtpbin, "pad-removed", (GCallback) pad_removed_cb, &data);

  ret = gst_element_set_state (rtpbin, GST_STATE_PLAYING);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS);

  while (count--) {
    /* request session 0 */
    rtp_sink = gst_element_get_request_pad (rtpbin, "recv_rtp_sink_0");
    fail_unless (rtp_sink != NULL);
    ASSERT_OBJECT_REFCOUNT (rtp_sink, "rtp_sink", 2);

    /* no sourcepads are created yet */
    fail_unless (rtpbin->numsinkpads == 1);
    fail_unless (rtpbin->numsrcpads == 0);

    buffer = make_rtp_packet (&data);
    res = gst_pad_chain (rtp_sink, buffer);
    GST_DEBUG ("res %d, %s\n", res, gst_flow_get_name (res));
    fail_unless (res == GST_FLOW_OK);

    buffer = make_rtp_packet (&data);
    res = gst_pad_chain (rtp_sink, buffer);
    GST_DEBUG ("res %d, %s\n", res, gst_flow_get_name (res));
    fail_unless (res == GST_FLOW_OK);

    /* we wait for the new pad to appear now */
    g_mutex_lock (data.lock);
    while (!data.pad_added)
      g_cond_wait (data.cond, data.lock);
    g_mutex_unlock (data.lock);

    /* sourcepad created now */
    fail_unless (rtpbin->numsinkpads == 1);
    fail_unless (rtpbin->numsrcpads == 1);

    /* remove the session */
    gst_element_release_request_pad (rtpbin, rtp_sink);
    gst_object_unref (rtp_sink);

    /* pad should be gone now */
    g_mutex_lock (data.lock);
    while (data.pad_added)
      g_cond_wait (data.cond, data.lock);
    g_mutex_unlock (data.lock);

    /* nothing left anymore now */
    fail_unless (rtpbin->numsinkpads == 0);
    fail_unless (rtpbin->numsrcpads == 0);
  }

  ret = gst_element_set_state (rtpbin, GST_STATE_NULL);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (rtpbin);

  clean_data (&data);
}

GST_END_TEST;

GST_START_TEST (test_cleanup_recv2)
{
  GstElement *rtpbin;
  GstPad *rtp_sink;
  CleanupData data;
  GstStateChangeReturn ret;
  GstFlowReturn res;
  GstBuffer *buffer;
  gint count = 2;

  init_data (&data);

  rtpbin = gst_element_factory_make ("gstrtpbin", "rtpbin");

  g_signal_connect (rtpbin, "pad-added", (GCallback) pad_added_cb, &data);
  g_signal_connect (rtpbin, "pad-removed", (GCallback) pad_removed_cb, &data);

  ret = gst_element_set_state (rtpbin, GST_STATE_PLAYING);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS);

  /* request session 0 */
  rtp_sink = gst_element_get_request_pad (rtpbin, "recv_rtp_sink_0");
  fail_unless (rtp_sink != NULL);
  ASSERT_OBJECT_REFCOUNT (rtp_sink, "rtp_sink", 2);

  while (count--) {
    /* no sourcepads are created yet */
    fail_unless (rtpbin->numsinkpads == 1);
    fail_unless (rtpbin->numsrcpads == 0);

    buffer = make_rtp_packet (&data);
    res = gst_pad_chain (rtp_sink, buffer);
    GST_DEBUG ("res %d, %s\n", res, gst_flow_get_name (res));
    fail_unless (res == GST_FLOW_OK);

    buffer = make_rtp_packet (&data);
    res = gst_pad_chain (rtp_sink, buffer);
    GST_DEBUG ("res %d, %s\n", res, gst_flow_get_name (res));
    fail_unless (res == GST_FLOW_OK);

    /* we wait for the new pad to appear now */
    g_mutex_lock (data.lock);
    while (!data.pad_added)
      g_cond_wait (data.cond, data.lock);
    g_mutex_unlock (data.lock);

    /* sourcepad created now */
    fail_unless (rtpbin->numsinkpads == 1);
    fail_unless (rtpbin->numsrcpads == 1);

    /* change state */
    ret = gst_element_set_state (rtpbin, GST_STATE_NULL);
    fail_unless (ret == GST_STATE_CHANGE_SUCCESS);

    /* pad should be gone now */
    g_mutex_lock (data.lock);
    while (data.pad_added)
      g_cond_wait (data.cond, data.lock);
    g_mutex_unlock (data.lock);

    /* back to playing for the next round */
    ret = gst_element_set_state (rtpbin, GST_STATE_PLAYING);
    fail_unless (ret == GST_STATE_CHANGE_SUCCESS);
  }

  /* remove the session */
  gst_element_release_request_pad (rtpbin, rtp_sink);
  gst_object_unref (rtp_sink);

  /* nothing left anymore now */
  fail_unless (rtpbin->numsinkpads == 0);
  fail_unless (rtpbin->numsrcpads == 0);

  ret = gst_element_set_state (rtpbin, GST_STATE_NULL);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (rtpbin);

  clean_data (&data);
}

GST_END_TEST;

GST_START_TEST (test_request_pad_by_template_name)
{
  GstElement *rtpbin;
  GstPad *rtp_sink1, *rtp_sink2, *rtp_sink3;

  rtpbin = gst_element_factory_make ("gstrtpbin", "rtpbin");
  rtp_sink1 = gst_element_get_request_pad (rtpbin, "recv_rtp_sink_%d");
  fail_unless (rtp_sink1 != NULL);
  fail_unless_equals_string (GST_PAD_NAME (rtp_sink1), "recv_rtp_sink_0");
  ASSERT_OBJECT_REFCOUNT (rtp_sink1, "rtp_sink1", 2);

  rtp_sink2 = gst_element_get_request_pad (rtpbin, "recv_rtp_sink_%d");
  fail_unless (rtp_sink2 != NULL);
  fail_unless_equals_string (GST_PAD_NAME (rtp_sink2), "recv_rtp_sink_1");
  ASSERT_OBJECT_REFCOUNT (rtp_sink2, "rtp_sink2", 2);

  rtp_sink3 = gst_element_get_request_pad (rtpbin, "recv_rtp_sink_%d");
  fail_unless (rtp_sink3 != NULL);
  fail_unless_equals_string (GST_PAD_NAME (rtp_sink3), "recv_rtp_sink_2");
  ASSERT_OBJECT_REFCOUNT (rtp_sink3, "rtp_sink3", 2);


  gst_element_release_request_pad (rtpbin, rtp_sink2);
  gst_element_release_request_pad (rtpbin, rtp_sink1);
  gst_element_release_request_pad (rtpbin, rtp_sink3);
  ASSERT_OBJECT_REFCOUNT (rtp_sink3, "rtp_sink3", 1);
  ASSERT_OBJECT_REFCOUNT (rtp_sink2, "rtp_sink2", 1);
  ASSERT_OBJECT_REFCOUNT (rtp_sink1, "rtp_sink", 1);
  gst_object_unref (rtp_sink1);
  gst_object_unref (rtp_sink2);
  gst_object_unref (rtp_sink3);

  gst_object_unref (rtpbin);
}

GST_END_TEST;

static Suite *
gstrtpbin_suite (void)
{
  Suite *s = suite_create ("gstrtpbin");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_cleanup_send);
  tcase_add_test (tc_chain, test_cleanup_recv);
  tcase_add_test (tc_chain, test_cleanup_recv2);
  tcase_add_test (tc_chain, test_request_pad_by_template_name);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = gstrtpbin_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
