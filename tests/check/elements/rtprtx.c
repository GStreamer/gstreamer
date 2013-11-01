/* GStreamer
 *
 * Copyright (C) 2013 Collabora Ltd.
 *   @author Julien Isorce <julien.isorce@collabora.co.uk>
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
#include <gst/check/gstconsistencychecker.h>
#include <gst/check/gsttestclock.h>

#include <gst/rtp/gstrtpbuffer.h>

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
static GstPad *srcpad, *sinkpad;
/* we also have a list of src buffers */
static GList *inbuffers = NULL;

static GMainLoop *main_loop;

#define RTP_CAPS_STRING    \
    "application/x-rtp, "               \
    "media = (string)audio, "           \
    "payload = (int) 0, "               \
    "clock-rate = (int) 8000, "         \
    "encoding-name = (string)PCMU"

#define RTP_FRAME_SIZE 20

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );
static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static void
setup_rtprtx (GstElement * rtprtxsend, GstElement * rtprtxreceive,
    gint num_buffers)
{
  GstClock *clock;
  GstBuffer *buffer;
  GstPad *sendsrcpad;
  GstPad *receivesinkpad;
  gboolean ret = FALSE;

  /* a 20 sample audio block (2,5 ms) generated with
   * gst-launch audiotestsrc wave=silence blocksize=40 num-buffers=3 !
   *    "audio/x-raw,channels=1,rate=8000" ! mulawenc ! rtppcmupay !
   *     fakesink dump=1
   */
  guint8 in[] = {               /* first 4 bytes are rtp-header, next 4 bytes are timestamp */
    0x80, 0x80, 0x1c, 0x24, 0x46, 0xcd, 0xb7, 0x11, 0x3c, 0x3a, 0x7c, 0x5b,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
  };
  GstClockTime ts = G_GUINT64_CONSTANT (0);
  GstClockTime tso = gst_util_uint64_scale (RTP_FRAME_SIZE, GST_SECOND, 8000);
  gint i;

  /* we need a clock here */
  clock = gst_system_clock_obtain ();
  gst_element_set_clock (rtprtxsend, clock);
  gst_object_unref (clock);

  srcpad = gst_check_setup_src_pad (rtprtxsend, &srctemplate);
  sendsrcpad = gst_element_get_static_pad (rtprtxsend, "src");
  ret = gst_pad_set_active (srcpad, TRUE);
  fail_if (ret == FALSE);

  sinkpad = gst_check_setup_sink_pad (rtprtxreceive, &sinktemplate);
  receivesinkpad = gst_element_get_static_pad (rtprtxreceive, "sink");
  ret = gst_pad_set_active (sinkpad, TRUE);
  fail_if (ret == FALSE);

  fail_if (gst_pad_link (sendsrcpad, receivesinkpad) != GST_PAD_LINK_OK);

  ret = gst_pad_set_active (sendsrcpad, TRUE);
  fail_if (ret == FALSE);
  ret = gst_pad_set_active (receivesinkpad, TRUE);
  fail_if (ret == FALSE);

  gst_object_unref (sendsrcpad);
  gst_object_unref (receivesinkpad);

  for (i = 0; i < num_buffers; i++) {
    buffer = gst_buffer_new_and_alloc (sizeof (in));
    gst_buffer_fill (buffer, 0, in, sizeof (in));
    GST_BUFFER_DTS (buffer) = ts;
    GST_BUFFER_PTS (buffer) = ts;
    GST_BUFFER_DURATION (buffer) = tso;
    GST_DEBUG ("created buffer: %p", buffer);

    /*if (!i)
       GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT); */

    inbuffers = g_list_append (inbuffers, buffer);

    /* hackish way to update the rtp header */
    in[1] = 0x00;
    in[3]++;                    /* seqnumber */
    in[7] += RTP_FRAME_SIZE;    /* inc. timestamp with framesize */
    ts += tso;
  }
}

static GstStateChangeReturn
start_rtprtx (GstElement * element)
{
  GstStateChangeReturn ret;
  GstClockTime now;
  GstClock *clock;

  clock = gst_element_get_clock (element);
  if (clock) {
    now = gst_clock_get_time (clock);
    gst_object_unref (clock);
    gst_element_set_base_time (element, now);
  }

  ret = gst_element_set_state (element, GST_STATE_PLAYING);
  ck_assert_int_ne (ret, GST_STATE_CHANGE_FAILURE);

  ret = gst_element_get_state (element, NULL, NULL, GST_CLOCK_TIME_NONE);
  ck_assert_int_ne (ret, GST_STATE_CHANGE_FAILURE);

  return ret;
}

static void
cleanup_rtprtx (GstElement * rtprtxsend, GstElement * rtprtxreceive)
{
  GST_DEBUG ("cleanup_rtprtx");

  g_list_free (inbuffers);
  inbuffers = NULL;

  gst_pad_set_active (srcpad, FALSE);
  gst_check_teardown_src_pad (rtprtxsend);
  gst_check_teardown_element (rtprtxsend);

  gst_pad_set_active (sinkpad, FALSE);
  gst_check_teardown_sink_pad (rtprtxreceive);
  gst_check_teardown_element (rtprtxreceive);
}

static void
check_rtprtx_results (GstElement * rtprtxsend, GstElement * rtprtxreceive,
    gint num_buffers)
{
  guint nbrtxrequests = 0;
  guint nbrtxpackets = 0;

  g_object_get (G_OBJECT (rtprtxsend), "num-rtx-requests", &nbrtxrequests,
      NULL);
  fail_unless_equals_int (nbrtxrequests, 3);

  g_object_get (G_OBJECT (rtprtxsend), "num-rtx-packets", &nbrtxpackets, NULL);
  fail_unless_equals_int (nbrtxpackets, 3);

  g_object_get (G_OBJECT (rtprtxreceive), "num-rtx-requests", &nbrtxrequests,
      NULL);
  fail_unless_equals_int (nbrtxrequests, 3);

  g_object_get (G_OBJECT (rtprtxreceive), "num-rtx-packets", &nbrtxpackets,
      NULL);
  fail_unless_equals_int (nbrtxpackets, 3);

  g_object_get (G_OBJECT (rtprtxreceive), "num-rtx-assoc-packets",
      &nbrtxpackets, NULL);
  fail_unless_equals_int (nbrtxpackets, 3);
}


GST_START_TEST (test_push_forward_seq)
{
  GstElement *rtprtxsend;
  GstElement *rtprtxreceive;
  const guint num_buffers = 4;
  GList *node;
  gint i = 0;
  GstCaps *caps = NULL;

  rtprtxsend = gst_check_setup_element ("rtprtxsend");
  rtprtxreceive = gst_check_setup_element ("rtprtxreceive");
  setup_rtprtx (rtprtxsend, rtprtxreceive, num_buffers);
  GST_DEBUG ("setup_rtprtx");

  fail_unless (start_rtprtx (rtprtxsend)
      == GST_STATE_CHANGE_SUCCESS, "could not set to playing");

  fail_unless (start_rtprtx (rtprtxreceive)
      == GST_STATE_CHANGE_SUCCESS, "could not set to playing");

  caps = gst_caps_from_string (RTP_CAPS_STRING);
  gst_check_setup_events (srcpad, rtprtxsend, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  g_object_set (rtprtxsend, "rtx-payload-type", 97, NULL);
  g_object_set (rtprtxreceive, "rtx-payload-types", "97", NULL);

  /* push buffers: 0,1,2, */
  for (node = inbuffers; node; node = g_list_next (node)) {
    GstEvent *event = NULL;
    GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
    GstBuffer *buffer = (GstBuffer *) node->data;
    fail_unless (gst_pad_push (srcpad, buffer) == GST_FLOW_OK);

    if (i < 3) {
      gst_rtp_buffer_map (buffer, GST_MAP_READ, &rtp);

      event = gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM,
          gst_structure_new ("GstRTPRetransmissionRequest",
              "seqnum", G_TYPE_UINT, (guint) gst_rtp_buffer_get_seq (&rtp),
              "ssrc", G_TYPE_UINT, (guint) gst_rtp_buffer_get_ssrc (&rtp),
              "payload-type", G_TYPE_UINT,
              (guint) gst_rtp_buffer_get_payload_type (&rtp), NULL));

      fail_unless (gst_pad_push_event (sinkpad, event));
      gst_rtp_buffer_unmap (&rtp);
    }
    gst_buffer_unref (buffer);
    ++i;
  }

  /* check the buffer list */
  check_rtprtx_results (rtprtxsend, rtprtxreceive, num_buffers);

  /* cleanup */
  cleanup_rtprtx (rtprtxsend, rtprtxreceive);
}

GST_END_TEST;

static void
message_received (GstBus * bus, GstMessage * message, GstPipeline * bin)
{
  GST_INFO ("bus message from \"%" GST_PTR_FORMAT "\": %" GST_PTR_FORMAT,
      GST_MESSAGE_SRC (message), message);

  switch (message->type) {
    case GST_MESSAGE_EOS:
      g_main_loop_quit (main_loop);
      break;
    case GST_MESSAGE_WARNING:{
      GError *gerror;
      gchar *debug;

      gst_message_parse_warning (message, &gerror, &debug);
      gst_object_default_error (GST_MESSAGE_SRC (message), gerror, debug);
      g_error_free (gerror);
      g_free (debug);
      break;
    }
    case GST_MESSAGE_ERROR:{
      GError *gerror;
      gchar *debug;

      gst_message_parse_error (message, &gerror, &debug);
      gst_object_default_error (GST_MESSAGE_SRC (message), gerror, debug);
      g_error_free (gerror);
      g_free (debug);
      g_main_loop_quit (main_loop);
      break;
    }
    default:
      break;
  }
}

typedef struct
{
  guint count;
  guint nb_packets;
  guint drop_every_n_packets;
} RTXSendData;

typedef struct
{
  guint nb_packets;
  guint seqnum_offset;
  guint seqnum_prev;
} RTXReceiveData;

static GstPadProbeReturn
rtprtxsend_srcpad_probe (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  GstPadProbeReturn ret = GST_PAD_PROBE_OK;

  if (info->type == (GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_PUSH)) {
    GstBuffer *buffer = GST_BUFFER (info->data);
    RTXSendData *rtxdata = (RTXSendData *) user_data;
    GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
    guint payload_type = 0;

    gst_rtp_buffer_map (buffer, GST_MAP_READ, &rtp);
    payload_type = gst_rtp_buffer_get_payload_type (&rtp);

    /* main stream packets */
    if (payload_type == 96) {
      /* count packets of the main stream */
      ++rtxdata->nb_packets;
      /* drop some packets */
      if (rtxdata->count < rtxdata->drop_every_n_packets) {
        ++rtxdata->count;
      } else {
        /* drop a packet every 'rtxdata->count' packets */
        rtxdata->count = 1;
        ret = GST_PAD_PROBE_DROP;
      }
    } else {
      /* retransmission packets */
    }

    gst_rtp_buffer_unmap (&rtp);
  }

  return ret;
}

static GstPadProbeReturn
rtprtxreceive_srcpad_probe (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  if (info->type == (GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_PUSH)) {
    GstBuffer *buffer = GST_BUFFER (info->data);
    RTXReceiveData *rtxdata = (RTXReceiveData *) user_data;
    GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
    guint seqnum = 0;
    guint i = 0;

    gst_rtp_buffer_map (buffer, GST_MAP_READ, &rtp);
    seqnum = gst_rtp_buffer_get_seq (&rtp);

    /* check if there is a dropped packet */
    if (seqnum > rtxdata->seqnum_prev + rtxdata->seqnum_offset) {
      GstPad *peerpad = gst_pad_get_peer (pad);

      /* ask retransmission of missing packet */
      for (i = rtxdata->seqnum_prev + rtxdata->seqnum_offset; i < seqnum;
          i += rtxdata->seqnum_offset) {
        GstEvent *event = gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM,
            gst_structure_new ("GstRTPRetransmissionRequest",
                "seqnum", G_TYPE_UINT, i,
                "ssrc", G_TYPE_UINT, gst_rtp_buffer_get_ssrc (&rtp),
                "payload-type", G_TYPE_UINT,
                gst_rtp_buffer_get_payload_type (&rtp),
                NULL));
        gst_pad_push_event (peerpad, event);
      }
      gst_object_unref (peerpad);

      rtxdata->seqnum_prev = seqnum;
    } else if (seqnum == rtxdata->seqnum_prev + rtxdata->seqnum_offset) {
      /* also update previous seqnum in this case */
      rtxdata->seqnum_prev = seqnum;
    }

    gst_rtp_buffer_unmap (&rtp);

    ++rtxdata->nb_packets;
  }

  return GST_PAD_PROBE_OK;
}

static void
start_test_drop_and_check_results (GstElement * bin, GstElement * rtppayloader,
    GstElement * rtprtxsend, GstElement * rtprtxreceive,
    RTXSendData * send_rtxdata, RTXReceiveData * receive_rtxdata,
    guint drop_every_n_packets)
{
  GstStateChangeReturn state_res = GST_STATE_CHANGE_FAILURE;
  guint nbrtxrequests = 0;
  guint nbrtxpackets = 0;
  guint nb_expected_requests = 0;

  GST_INFO ("starting test");

  g_object_set (rtppayloader, "pt", 96, NULL);
  g_object_set (rtppayloader, "seqnum-offset", 1, NULL);
  g_object_set (rtprtxsend, "rtx-payload-type", 99, NULL);
  g_object_set (rtprtxreceive, "rtx-payload-types", "99:111:125", NULL);

  send_rtxdata->count = 1;
  send_rtxdata->nb_packets = 0;
  send_rtxdata->drop_every_n_packets = drop_every_n_packets;

  receive_rtxdata->nb_packets = 0;
  receive_rtxdata->seqnum_offset = 0;
  receive_rtxdata->seqnum_prev = 0;

  /* retrieve offset before going to paused */
  g_object_get (G_OBJECT (rtppayloader), "seqnum-offset",
      &receive_rtxdata->seqnum_offset, NULL);

  /* prepare playing */
  state_res = gst_element_set_state (bin, GST_STATE_PAUSED);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  /* wait for completion */
  state_res = gst_element_get_state (bin, NULL, NULL, GST_CLOCK_TIME_NONE);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  /* retrieve seqnum_prev here to make sure it has been reseted */
  g_object_get (G_OBJECT (rtppayloader), "seqnum",
      &receive_rtxdata->seqnum_prev, NULL);

  /* run pipeline */
  state_res = gst_element_set_state (bin, GST_STATE_PLAYING);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  GST_INFO ("running main loop");
  g_main_loop_run (main_loop);

  /* check results */

  if (send_rtxdata->nb_packets % drop_every_n_packets == 0) {
    /* special case because the last buffer will be dropped
     * so the receiver cannot know if it has been dropped (no next packet)
     */
    nb_expected_requests = send_rtxdata->nb_packets / drop_every_n_packets - 1;
    fail_unless_equals_int (send_rtxdata->nb_packets,
        receive_rtxdata->nb_packets + 1);
  } else {
    nb_expected_requests = send_rtxdata->nb_packets / drop_every_n_packets;
    fail_unless_equals_int (send_rtxdata->nb_packets,
        receive_rtxdata->nb_packets);
  }

  g_object_get (G_OBJECT (rtprtxsend), "num-rtx-requests", &nbrtxrequests,
      NULL);
  fail_unless_equals_int (nbrtxrequests, nb_expected_requests);

  g_object_get (G_OBJECT (rtprtxsend), "num-rtx-packets", &nbrtxpackets, NULL);
  fail_unless_equals_int (nbrtxpackets, nb_expected_requests);

  g_object_get (G_OBJECT (rtprtxreceive), "num-rtx-requests", &nbrtxrequests,
      NULL);
  fail_unless_equals_int (nbrtxrequests, nb_expected_requests);

  g_object_get (G_OBJECT (rtprtxreceive), "num-rtx-packets", &nbrtxpackets,
      NULL);
  fail_unless_equals_int (nbrtxpackets, nb_expected_requests);

  g_object_get (G_OBJECT (rtprtxreceive), "num-rtx-assoc-packets",
      &nbrtxpackets, NULL);
  fail_unless_equals_int (nbrtxpackets, nb_expected_requests);

  state_res = gst_element_set_state (bin, GST_STATE_NULL);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);
}

/* This test build the pipeline videotestsrc ! rtpvrawpay ! rtprtxsend ! rtprtxreceive ! fakesink
 * and drop some buffer between rtprtxsend and rtprtxreceive
 * Then it checks that every dropped packet has been re-sent and it checks that
 * not too much requests has been sent.
 */
GST_START_TEST (test_drop_one_sender)
{
  GstElement *bin, *src, *rtppayloader, *rtprtxsend, *rtprtxreceive, *sink;
  GstBus *bus;
  gboolean res;
  GstPad *srcpad, *sinkpad;
  GstStreamConsistency *chk_1, *chk_2, *chk_3;
  gint num_buffers = 20;
  guint drop_every_n_packets = 0;
  RTXSendData send_rtxdata;
  RTXReceiveData receive_rtxdata;

  GST_INFO ("preparing test");

  /* build pipeline */
  bin = gst_pipeline_new ("pipeline");
  bus = gst_element_get_bus (bin);
  gst_bus_add_signal_watch_full (bus, G_PRIORITY_HIGH);

  src = gst_element_factory_make ("videotestsrc", "src");
  g_object_set (src, "num-buffers", num_buffers, NULL);
  rtppayloader = gst_element_factory_make ("rtpvrawpay", "rtppayloader");
  rtprtxsend = gst_element_factory_make ("rtprtxsend", "rtprtxsend");
  rtprtxreceive = gst_element_factory_make ("rtprtxreceive", "rtprtxreceive");
  sink = gst_element_factory_make ("fakesink", "sink");
  gst_bin_add_many (GST_BIN (bin), src, rtppayloader, rtprtxsend, rtprtxreceive,
      sink, NULL);

  res = gst_element_link (src, rtppayloader);
  fail_unless (res == TRUE, NULL);
  res = gst_element_link (rtppayloader, rtprtxsend);
  fail_unless (res == TRUE, NULL);
  res = gst_element_link (rtprtxsend, rtprtxreceive);
  fail_unless (res == TRUE, NULL);
  res = gst_element_link (rtprtxreceive, sink);
  fail_unless (res == TRUE, NULL);

  /* create consistency checkers for the pads */

  srcpad = gst_element_get_static_pad (rtppayloader, "src");
  chk_1 = gst_consistency_checker_new (srcpad);
  gst_object_unref (srcpad);

  srcpad = gst_element_get_static_pad (rtprtxsend, "src");
  gst_pad_add_probe (srcpad,
      (GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_PUSH),
      (GstPadProbeCallback) rtprtxsend_srcpad_probe, &send_rtxdata, NULL);
  sinkpad = gst_pad_get_peer (srcpad);
  fail_if (sinkpad == NULL);
  chk_2 = gst_consistency_checker_new (sinkpad);
  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);

  srcpad = gst_element_get_static_pad (rtprtxreceive, "src");
  gst_pad_add_probe (srcpad,
      (GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_PUSH),
      (GstPadProbeCallback) rtprtxreceive_srcpad_probe, &receive_rtxdata, NULL);
  sinkpad = gst_pad_get_peer (srcpad);
  fail_if (sinkpad == NULL);
  chk_3 = gst_consistency_checker_new (sinkpad);
  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);

  main_loop = g_main_loop_new (NULL, FALSE);
  g_signal_connect (bus, "message::error", (GCallback) message_received, bin);
  g_signal_connect (bus, "message::warning", (GCallback) message_received, bin);
  g_signal_connect (bus, "message::eos", (GCallback) message_received, bin);

  for (drop_every_n_packets = 2; drop_every_n_packets < 10;
      drop_every_n_packets++) {
    start_test_drop_and_check_results (bin, rtppayloader, rtprtxsend,
        rtprtxreceive, &send_rtxdata, &receive_rtxdata, drop_every_n_packets);
  }

  /* cleanup */
  g_main_loop_unref (main_loop);
  gst_consistency_checker_free (chk_1);
  gst_consistency_checker_free (chk_2);
  gst_consistency_checker_free (chk_3);
  gst_bus_remove_signal_watch (bus);
  gst_object_unref (bus);
  gst_object_unref (bin);
}

GST_END_TEST;

static Suite *
rtprtx_suite (void)
{
  Suite *s = suite_create ("rtprtx");
  TCase *tc_chain = tcase_create ("general");

  tcase_set_timeout (tc_chain, 10000);

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_push_forward_seq);
  tcase_add_test (tc_chain, test_drop_one_sender);

  return s;
}

GST_CHECK_MAIN (rtprtx);
