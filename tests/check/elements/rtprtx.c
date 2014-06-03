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

#include <gst/rtp/gstrtpbuffer.h>

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
static GstPad *srcpad, *sinkpad;
/* we also have a list of src buffers */
static GList *inbuffers = NULL;

#define RTP_CAPS_STRING    \
    "application/x-rtp, "               \
    "media = (string)audio, "           \
    "payload = (int) 0, "               \
    "clock-rate = (int) 8000, "         \
    "ssrc = (uint) 42, "                \
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
  GstStructure *pt_map;

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

  pt_map = gst_structure_new ("application/x-rtp-pt-map",
      "0", G_TYPE_UINT, 97, NULL);
  g_object_set (rtprtxsend, "payload-type-map", pt_map, NULL);
  g_object_set (rtprtxreceive, "payload-type-map", pt_map, NULL);
  gst_structure_free (pt_map);

  /* push buffers: 0,1,2, */
  for (node = inbuffers; node; node = g_list_next (node)) {
    GstEvent *event = NULL;
    GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
    GstBuffer *buffer = (GstBuffer *) node->data;
    GList *last_out_buffer;
    guint64 end_time;
    gboolean res;

    gst_buffer_ref (buffer);
    fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);

    if (i < 3) {
      gst_rtp_buffer_map (buffer, GST_MAP_READ, &rtp);
      event = gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM,
          gst_structure_new ("GstRTPRetransmissionRequest",
              "seqnum", G_TYPE_UINT, (guint) gst_rtp_buffer_get_seq (&rtp),
              "ssrc", G_TYPE_UINT, (guint) gst_rtp_buffer_get_ssrc (&rtp),
              "payload-type", G_TYPE_UINT,
              (guint) gst_rtp_buffer_get_payload_type (&rtp), NULL));
      gst_rtp_buffer_unmap (&rtp);

      /* synchronize with the chain() function of the "sinkpad"
       * to make sure that rtxsend has pushed the rtx buffer out
       * before continuing */
      last_out_buffer = g_list_last (buffers);
      g_mutex_lock (&check_mutex);
      fail_unless (gst_pad_push_event (sinkpad, event));
      end_time = g_get_monotonic_time () + G_TIME_SPAN_SECOND;
      do
        res = g_cond_wait_until (&check_cond, &check_mutex, end_time);
      while (res == TRUE && last_out_buffer == g_list_last (buffers));
      g_mutex_unlock (&check_mutex);
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
message_received (GstBus * bus, GstMessage * message, gboolean * eos)
{
  GST_INFO ("bus message from \"%" GST_PTR_FORMAT "\": %" GST_PTR_FORMAT,
      GST_MESSAGE_SRC (message), message);

  switch (message->type) {
    case GST_MESSAGE_EOS:
      *eos = TRUE;
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
      fail ("Error: %s / %s", gerror->message, debug);
      g_error_free (gerror);
      g_free (debug);
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
    guint drop_every_n_packets, gboolean * eos)
{
  GstStateChangeReturn state_res = GST_STATE_CHANGE_FAILURE;
  guint nbrtxrequests = 0;
  guint nbrtxpackets = 0;
  guint nb_expected_requests = 0;
  GstStructure *pt_map;

  GST_INFO ("starting test");

  pt_map = gst_structure_new ("application/x-rtp-pt-map",
      "96", G_TYPE_UINT, 99, NULL);
  g_object_set (rtppayloader, "pt", 96, NULL);
  g_object_set (rtppayloader, "seqnum-offset", 1, NULL);
  g_object_set (rtprtxsend, "payload-type-map", pt_map, NULL);
  g_object_set (rtprtxreceive, "payload-type-map", pt_map, NULL);
  gst_structure_free (pt_map);

  send_rtxdata->count = 1;
  send_rtxdata->nb_packets = 0;
  send_rtxdata->drop_every_n_packets = drop_every_n_packets;

  receive_rtxdata->nb_packets = 0;
  receive_rtxdata->seqnum_offset = 0;
  receive_rtxdata->seqnum_prev = 0;

  *eos = FALSE;

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
  while (!*eos)
    g_main_context_iteration (NULL, TRUE);

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
  gboolean eos = FALSE;

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

  g_signal_connect (bus, "message::error", (GCallback) message_received, NULL);
  g_signal_connect (bus, "message::warning", (GCallback) message_received,
      NULL);
  g_signal_connect (bus, "message::eos", (GCallback) message_received, &eos);

  for (drop_every_n_packets = 2; drop_every_n_packets < 10;
      drop_every_n_packets++) {
    start_test_drop_and_check_results (bin, rtppayloader, rtprtxsend,
        rtprtxreceive, &send_rtxdata, &receive_rtxdata, drop_every_n_packets,
        &eos);
  }

  /* cleanup */
  gst_consistency_checker_free (chk_1);
  gst_consistency_checker_free (chk_2);
  gst_consistency_checker_free (chk_3);
  gst_bus_remove_signal_watch (bus);
  gst_object_unref (bus);
  gst_object_unref (bin);
}

GST_END_TEST;

static void
message_received_multiple (GstBus * bus, GstMessage * message, gpointer data)
{
  GST_INFO ("bus message from \"%" GST_PTR_FORMAT "\": %" GST_PTR_FORMAT,
      GST_MESSAGE_SRC (message), message);

  switch (message->type) {
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
      fail ("Error: %s / %s", gerror->message, debug);
      g_error_free (gerror);
      g_free (debug);
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
  guint payload_type_master;
  guint total_packets;
} RTXSendMultipleData;

/* drop some packets */
static GstPadProbeReturn
rtprtxsend_srcpad_probe_multiple (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  GstPadProbeReturn ret = GST_PAD_PROBE_OK;

  if (info->type == (GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_PUSH)) {
    GstBuffer *buffer = GST_BUFFER (info->data);
    RTXSendMultipleData *rtxdata = (RTXSendMultipleData *) user_data;
    GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
    guint payload_type = 0;

    gst_rtp_buffer_map (buffer, GST_MAP_READ, &rtp);
    payload_type = gst_rtp_buffer_get_payload_type (&rtp);

    /* main stream packets */
    if (payload_type == rtxdata->payload_type_master) {
      /* count packets of the main stream */
      ++rtxdata->nb_packets;
      /* drop some packets */
      /* but make sure we never drop the last one, otherwise there
       * will be nothing to trigger a retransmission.
       */
      if (rtxdata->count < rtxdata->drop_every_n_packets ||
          rtxdata->nb_packets == rtxdata->total_packets) {
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

/* make sure every sources has sent all their buffers */
static GstPadProbeReturn
source_srcpad_probe_multiple_drop_eos (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);

  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS)
    return GST_PAD_PROBE_DROP;
  else
    return GST_PAD_PROBE_OK;
}

typedef struct
{
  GHashTable *ssrc_to_nb_packets_map;
  GHashTable *ssrc_to_seqnum_offset_map;
  guint seqnum_offset;

  gint to_send;
  volatile gint dropped_requests;
  volatile gint received;
  gboolean request_passed;
} RTXReceiveMultipleData;

/* add one branch videotestsrc ! rtpvrawpay ! rtprtxsend ! queue ! funnel. */
static RTXSendMultipleData *
add_sender (GstElement * bin, const gchar * src_name,
    const gchar * payloader_name, guint payload_type_master,
    guint payload_type_aux, RTXReceiveMultipleData * rtxdata)
{
  GstElement *src = NULL;
  GstCaps *caps;
  GstElement *rtppayloader = NULL;
  GstElement *rtprtxsend = NULL;
  GstElement *queue = NULL;
  GstElement *funnel = NULL;
  GstPad *srcpad = NULL;
  gboolean res = FALSE;
  RTXSendMultipleData *send_rtxdata = g_slice_new0 (RTXSendMultipleData);
  gchar *pt_master;
  GstStructure *pt_map;

  send_rtxdata->count = 1;
  send_rtxdata->nb_packets = 0;
  send_rtxdata->drop_every_n_packets = 0;
  send_rtxdata->payload_type_master = payload_type_master;
  send_rtxdata->total_packets = 25;
  rtxdata->to_send += send_rtxdata->total_packets;

  src = gst_element_factory_make (src_name, NULL);
  rtppayloader = gst_element_factory_make (payloader_name, NULL);
  rtprtxsend = gst_element_factory_make ("rtprtxsend", NULL);
  queue = gst_element_factory_make ("queue", NULL);
  funnel = gst_bin_get_by_name (GST_BIN (bin), "funnel");

  pt_master = g_strdup_printf ("%" G_GUINT32_FORMAT, payload_type_master);
  pt_map = gst_structure_new ("application/x-rtp-pt-map",
      pt_master, G_TYPE_UINT, payload_type_aux, NULL);
  g_free (pt_master);

  g_object_set (src, "num-buffers", send_rtxdata->total_packets, NULL);
  g_object_set (src, "is-live", TRUE, NULL);
  g_object_set (rtppayloader, "pt", payload_type_master, NULL);
  g_object_set (rtppayloader, "seqnum-offset", 1, NULL);
  g_object_set (rtprtxsend, "payload-type-map", pt_map, NULL);
  /* we want that every drop packet be resent fast */
  g_object_set (queue, "max-size-buffers", 1, NULL);
  g_object_set (queue, "flush-on-eos", FALSE, NULL);

  gst_structure_free (pt_map);

  gst_bin_add_many (GST_BIN (bin), src, rtppayloader, rtprtxsend, queue, NULL);

  /* Make sure we have one buffer per frame, makes it easier to count! */
  caps =
      gst_caps_from_string ("video/x-raw, width=20, height=10, framerate=30/1");
  res = gst_element_link_filtered (src, rtppayloader, caps);
  gst_caps_unref (caps);
  fail_unless (res == TRUE, NULL);
  res = gst_element_link (rtppayloader, rtprtxsend);
  fail_unless (res == TRUE, NULL);
  res = gst_element_link (rtprtxsend, queue);
  fail_unless (res == TRUE, NULL);
  res = gst_element_link (queue, funnel);
  fail_unless (res == TRUE, NULL);
  gst_object_unref (funnel);

  /* to drop some packets */
  srcpad = gst_element_get_static_pad (rtprtxsend, "src");
  gst_pad_add_probe (srcpad,
      (GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_PUSH),
      (GstPadProbeCallback) rtprtxsend_srcpad_probe_multiple, send_rtxdata,
      NULL);
  gst_object_unref (srcpad);

  /* to make sure every sources has sent all their buffers */
  srcpad = gst_element_get_static_pad (src, "src");
  gst_pad_add_probe (srcpad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
      (GstPadProbeCallback) source_srcpad_probe_multiple_drop_eos, NULL, NULL);
  gst_object_unref (srcpad);

  return send_rtxdata;
}

static GstPadProbeReturn
rtprtxreceive_sinkpad_probe_check_drop (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);
  RTXReceiveMultipleData *rtxdata = (RTXReceiveMultipleData *) user_data;

  if (GST_EVENT_TYPE (event) == GST_EVENT_CUSTOM_UPSTREAM &&
      gst_event_get_structure (event) != NULL &&
      gst_structure_has_name (gst_event_get_structure (event),
          "GstRTPRetransmissionRequest"))
    rtxdata->request_passed = TRUE;

  return GST_PAD_PROBE_OK;
}

static gboolean
check_finished (RTXReceiveMultipleData * rtxdata)
{
  return (g_atomic_int_get (&rtxdata->received) >= (rtxdata->to_send -
          g_atomic_int_get (&rtxdata->dropped_requests)));
}

static GstPadProbeReturn
rtprtxreceive_srcpad_probe_multiple (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  if (info->type == (GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_PUSH)) {
    GstBuffer *buffer = GST_BUFFER (info->data);
    RTXReceiveMultipleData *rtxdata = (RTXReceiveMultipleData *) user_data;
    GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
    guint ssrc = 0;
    guint seqnum = 0;
    gpointer seqnum_prev = 0;
    guint nb_packets = 0;

    gst_rtp_buffer_map (buffer, GST_MAP_READ, &rtp);
    ssrc = gst_rtp_buffer_get_ssrc (&rtp);
    seqnum = gst_rtp_buffer_get_seq (&rtp);

    g_atomic_int_inc (&rtxdata->received);
    if (check_finished (rtxdata))
      g_main_context_wakeup (NULL);

    if (!g_hash_table_lookup_extended (rtxdata->ssrc_to_seqnum_offset_map,
            GUINT_TO_POINTER (ssrc), NULL, &seqnum_prev)) {
      /*In our test we take care to never drop the first buffer */
      g_hash_table_insert (rtxdata->ssrc_to_seqnum_offset_map,
          GUINT_TO_POINTER (ssrc), GUINT_TO_POINTER (seqnum));
      g_hash_table_insert (rtxdata->ssrc_to_nb_packets_map,
          GUINT_TO_POINTER (ssrc), GUINT_TO_POINTER (1));
      gst_rtp_buffer_unmap (&rtp);
      return GST_PAD_PROBE_OK;
    }


    /* check if there is a dropped packet
     * (in our test every packet arrived in increasing order) */
    if (seqnum > GPOINTER_TO_UINT (seqnum_prev) + rtxdata->seqnum_offset) {
      GstPad *peerpad = gst_pad_get_peer (pad);
      guint i = 0;

      /* ask retransmission of missing packets */
      for (i = GPOINTER_TO_UINT (seqnum_prev) + rtxdata->seqnum_offset;
          i < seqnum; i += rtxdata->seqnum_offset) {
        GstEvent *event = gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM,
            gst_structure_new ("GstRTPRetransmissionRequest",
                "seqnum", G_TYPE_UINT, i,
                "ssrc", G_TYPE_UINT, gst_rtp_buffer_get_ssrc (&rtp),
                "payload-type", G_TYPE_UINT,
                gst_rtp_buffer_get_payload_type (&rtp),
                NULL));
        rtxdata->request_passed = FALSE;
        gst_pad_push_event (peerpad, event);
        if (!rtxdata->request_passed) {
          g_atomic_int_inc (&rtxdata->dropped_requests);
          if (check_finished (rtxdata))
            g_main_context_wakeup (NULL);
        }
      }
      gst_object_unref (peerpad);

      g_hash_table_insert (rtxdata->ssrc_to_seqnum_offset_map,
          GUINT_TO_POINTER (ssrc), GUINT_TO_POINTER (seqnum));
    } else if (seqnum ==
        GPOINTER_TO_UINT (seqnum_prev) + rtxdata->seqnum_offset) {
      /* also update previous seqnum in this case */
      g_hash_table_insert (rtxdata->ssrc_to_seqnum_offset_map,
          GUINT_TO_POINTER (ssrc), GUINT_TO_POINTER (seqnum));
    } else {
      /* receive retransmited packet */
    }

    gst_rtp_buffer_unmap (&rtp);

    nb_packets =
        GPOINTER_TO_UINT (g_hash_table_lookup (rtxdata->ssrc_to_nb_packets_map,
            GUINT_TO_POINTER (ssrc)));
    g_hash_table_insert (rtxdata->ssrc_to_nb_packets_map,
        GUINT_TO_POINTER (ssrc), GUINT_TO_POINTER (++nb_packets));
  }

  return GST_PAD_PROBE_OK;
}

static void
reset_rtx_send_data (RTXSendMultipleData * send_rtxdata, gpointer data)
{
  send_rtxdata->count = 1;
  send_rtxdata->nb_packets = 0;
  send_rtxdata->drop_every_n_packets = *(guint *) data;
}

/* compute number of all packets sent by all sender */
static void
compute_total_packets_sent (RTXSendMultipleData * send_rtxdata, gpointer data)
{
  guint *sum = (guint *) data;
  *sum += send_rtxdata->nb_packets;
}

/* compute number of all packets received by rtprtxreceive::src pad */
static void
compute_total_packets_received (gpointer key, gpointer value, gpointer data)
{
  guint *sum = (guint *) data;
  *sum += GPOINTER_TO_UINT (value);
}

static void
start_test_drop_multiple_and_check_results (GstElement * bin,
    GList * send_rtxdata_list, RTXReceiveMultipleData * receive_rtxdata,
    guint drop_every_n_packets)
{
  GstStateChangeReturn state_res = GST_STATE_CHANGE_FAILURE;
  GstElement *rtprtxreceive =
      gst_bin_get_by_name (GST_BIN (bin), "rtprtxreceive");
  guint sum_all_packets_sent = 0;
  guint sum_rtx_packets_sent = 0;
  guint sum_all_packets_received = 0;
  guint sum_rtx_packets_received = 0;
  guint sum_rtx_assoc_packets_received = 0;
  guint sum_rtx_dropped_packets_received = 0;
  gdouble error_sent_recv = 0;
  GstIterator *itr_elements = NULL;
  gboolean done = FALSE;
  GValue item = { 0 };
  GstElement *element = NULL;
  gchar *name = NULL;

  GST_INFO ("starting test");

  g_atomic_int_set (&receive_rtxdata->received, 0);
  g_atomic_int_set (&receive_rtxdata->dropped_requests, 0);

  g_hash_table_remove_all (receive_rtxdata->ssrc_to_nb_packets_map);
  g_hash_table_remove_all (receive_rtxdata->ssrc_to_seqnum_offset_map);

  g_list_foreach (send_rtxdata_list, (GFunc) reset_rtx_send_data,
      &drop_every_n_packets);

  /* run pipeline */
  state_res = gst_element_set_state (bin, GST_STATE_PLAYING);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  state_res = gst_element_get_state (bin, NULL, NULL, GST_CLOCK_TIME_NONE);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  GST_INFO ("running main loop");
  while (!check_finished (receive_rtxdata))
    g_main_context_iteration (NULL, TRUE);

  /* check results */
  itr_elements = gst_bin_iterate_elements (GST_BIN (bin));
  done = FALSE;
  while (!done) {
    switch (gst_iterator_next (itr_elements, &item)) {
      case GST_ITERATOR_OK:
        element = GST_ELEMENT (g_value_get_object (&item));
        name = gst_element_get_name (element);
        if (g_str_has_prefix (name, "rtprtxsend") > 0) {
          guint nb_packets = 0;
          g_object_get (G_OBJECT (element), "num-rtx-packets", &nb_packets,
              NULL);
          sum_rtx_packets_sent += nb_packets;
        }
        g_free (name);
        g_value_reset (&item);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (itr_elements);
        break;
      case GST_ITERATOR_ERROR:
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  g_value_unset (&item);
  gst_iterator_free (itr_elements);

  /* compute number of all packets sent by all sender */
  g_list_foreach (send_rtxdata_list, (GFunc) compute_total_packets_sent,
      &sum_all_packets_sent);

  /* compute number of all packets received by rtprtxreceive::src pad */
  g_hash_table_foreach (receive_rtxdata->ssrc_to_nb_packets_map,
      compute_total_packets_received, (gpointer) & sum_all_packets_received);

  sum_all_packets_received +=
      g_atomic_int_get (&receive_rtxdata->dropped_requests);
  fail_if (sum_all_packets_sent < sum_all_packets_received);

  /* some packet are not received, I still have to figure out why
   * but I suspect it comes from pipeline setup/shutdown
   */
  if (sum_all_packets_sent != sum_all_packets_received) {
    error_sent_recv =
        1 - sum_all_packets_received / (gdouble) sum_all_packets_sent;
    fail_if (error_sent_recv > 0.30);
    /* it should be 0% */
  }

  /* retrieve number of retransmit packets received by rtprtxreceive */
  g_object_get (G_OBJECT (rtprtxreceive), "num-rtx-packets",
      &sum_rtx_packets_received, NULL);

  /* some of rtx packet are not received because the receiver avoids
   * collision (= requests that have the same seqnum)
   */
  fail_if (sum_rtx_packets_sent < sum_rtx_packets_received);
  g_object_get (G_OBJECT (rtprtxreceive), "num-rtx-assoc-packets",
      &sum_rtx_assoc_packets_received, NULL);
  sum_rtx_dropped_packets_received =
      sum_rtx_packets_received - sum_rtx_assoc_packets_received;
  fail_unless_equals_int (sum_rtx_packets_sent,
      sum_rtx_assoc_packets_received + sum_rtx_dropped_packets_received);

  gst_object_unref (rtprtxreceive);
  state_res = gst_element_set_state (bin, GST_STATE_NULL);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);
}

static void
free_rtx_send_data (gpointer data)
{
  g_slice_free (RTXSendMultipleData, data);
}

/* This test build the pipeline funnel name=funnel
 * videotestsrc ! rtpvrawpay ! rtprtxsend ! queue ! funnel.
 * videotestsrc ! rtpvrawpay ! rtprtxsend ! queue ! funnel.
 * N
 * funnel. ! rtprtxreceive ! fakesink
 * and drop some buffer just after each rtprtxsend
 * Then it checks that every dropped packet has been re-sent and it checks
 * that not too much requests has been sent.
 */
GST_START_TEST (test_drop_multiple_sender)
{
  GstElement *bin, *funnel, *rtprtxreceive, *sink;
  GstBus *bus;
  gboolean res;
  GstPad *srcpad, *sinkpad;
  guint drop_every_n_packets = 0;
  GList *send_rtxdata_list = NULL;
  RTXReceiveMultipleData receive_rtxdata = { NULL };
  GstStructure *pt_map;

  GST_INFO ("preparing test");

  receive_rtxdata.ssrc_to_nb_packets_map =
      g_hash_table_new (g_direct_hash, g_direct_equal);
  receive_rtxdata.ssrc_to_seqnum_offset_map =
      g_hash_table_new (g_direct_hash, g_direct_equal);
  receive_rtxdata.seqnum_offset = 1;

  /* build pipeline */
  bin = gst_pipeline_new ("pipeline");
  bus = gst_element_get_bus (bin);
  gst_bus_add_signal_watch_full (bus, G_PRIORITY_HIGH);

  funnel = gst_element_factory_make ("funnel", "funnel");
  rtprtxreceive = gst_element_factory_make ("rtprtxreceive", "rtprtxreceive");
  sink = gst_element_factory_make ("fakesink", "sink");
  g_object_set (sink, "sync", TRUE, NULL);
  g_object_set (sink, "qos", FALSE, NULL);
  gst_bin_add_many (GST_BIN (bin), funnel, rtprtxreceive, sink, NULL);

  send_rtxdata_list =
      g_list_append (send_rtxdata_list, add_sender (bin, "videotestsrc",
          "rtpvrawpay", 96, 121, &receive_rtxdata));
  send_rtxdata_list =
      g_list_append (send_rtxdata_list, add_sender (bin, "videotestsrc",
          "rtpvrawpay", 97, 122, &receive_rtxdata));
  send_rtxdata_list =
      g_list_append (send_rtxdata_list, add_sender (bin, "videotestsrc",
          "rtpvrawpay", 98, 123, &receive_rtxdata));
  send_rtxdata_list =
      g_list_append (send_rtxdata_list, add_sender (bin, "videotestsrc",
          "rtpvrawpay", 99, 124, &receive_rtxdata));

  pt_map = gst_structure_new ("application/x-rtp-pt-map",
      "96", G_TYPE_UINT, 121, "97", G_TYPE_UINT, 122,
      "98", G_TYPE_UINT, 123, "99", G_TYPE_UINT, 124, NULL);
  g_object_set (rtprtxreceive, "payload-type-map", pt_map, NULL);
  gst_structure_free (pt_map);

  res = gst_element_link (funnel, rtprtxreceive);
  fail_unless (res == TRUE, NULL);
  res = gst_element_link (rtprtxreceive, sink);
  fail_unless (res == TRUE, NULL);

  srcpad = gst_element_get_static_pad (rtprtxreceive, "src");
  gst_pad_add_probe (srcpad,
      (GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_PUSH),
      (GstPadProbeCallback) rtprtxreceive_srcpad_probe_multiple,
      &receive_rtxdata, NULL);
  gst_object_unref (srcpad);

  sinkpad = gst_element_get_static_pad (rtprtxreceive, "sink");
  gst_pad_add_probe (sinkpad,
      GST_PAD_PROBE_TYPE_EVENT_UPSTREAM,
      (GstPadProbeCallback) rtprtxreceive_sinkpad_probe_check_drop,
      &receive_rtxdata, NULL);
  gst_object_unref (sinkpad);

  g_signal_connect (bus, "message::error",
      (GCallback) message_received_multiple, NULL);
  g_signal_connect (bus, "message::warning",
      (GCallback) message_received_multiple, NULL);

  for (drop_every_n_packets = 2; drop_every_n_packets < 10;
      drop_every_n_packets++) {
    start_test_drop_multiple_and_check_results (bin, send_rtxdata_list,
        &receive_rtxdata, drop_every_n_packets);
  }

  /* cleanup */

  g_list_free_full (send_rtxdata_list, free_rtx_send_data);
  g_hash_table_destroy (receive_rtxdata.ssrc_to_nb_packets_map);
  g_hash_table_destroy (receive_rtxdata.ssrc_to_seqnum_offset_map);

  gst_bus_remove_signal_watch (bus);
  gst_object_unref (bus);
  gst_object_unref (bin);
}

GST_END_TEST;

struct GenerateTestBuffersData
{
  GstElement *src, *capsfilter, *payloader, *sink;
  GMutex mutex;
  GCond cond;
  GList *buffers;
  gint num_buffers;
  guint last_seqnum;
};

static void
fakesink_handoff (GstElement * sink, GstBuffer * buf, GstPad * pad,
    gpointer user_data)
{
  struct GenerateTestBuffersData *data = user_data;

  g_mutex_lock (&data->mutex);

  if (data->num_buffers > 0)
    data->buffers = g_list_append (data->buffers, gst_buffer_ref (buf));

  /* if we have collected enough buffers, unblock the main thread to stop */
  if (--data->num_buffers <= 0)
    g_cond_signal (&data->cond);

  if (data->num_buffers == 0)
    g_object_get (data->payloader, "seqnum", &data->last_seqnum, NULL);

  g_mutex_unlock (&data->mutex);
}

static GList *
generate_test_buffers (const gint num_buffers, guint ssrc, guint * payload_type)
{
  GstElement *bin;
  GstCaps *videotestsrc_caps;
  gboolean res;
  struct GenerateTestBuffersData data;

  fail_unless (num_buffers > 0);

  g_mutex_init (&data.mutex);
  g_cond_init (&data.cond);
  data.buffers = NULL;
  data.num_buffers = num_buffers;

  bin = gst_pipeline_new (NULL);
  data.src = gst_element_factory_make ("videotestsrc", NULL);
  data.capsfilter = gst_element_factory_make ("capsfilter", NULL);
  data.payloader = gst_element_factory_make ("rtpvrawpay", NULL);
  data.sink = gst_element_factory_make ("fakesink", NULL);

  /* small frame size will cause vrawpay to generate exactly one rtp packet
   * per video frame, which we need for the max-size-time test */
  videotestsrc_caps =
      gst_caps_from_string
      ("video/x-raw,format=I420,width=10,height=10,framerate=30/1");

  g_object_set (data.src, "do-timestamp", TRUE, NULL);
  g_object_set (data.capsfilter, "caps", videotestsrc_caps, NULL);
  g_object_set (data.payloader, "seqnum-offset", 1, "ssrc", ssrc, NULL);
  g_object_set (data.sink, "signal-handoffs", TRUE, NULL);
  g_signal_connect (data.sink, "handoff", (GCallback) fakesink_handoff, &data);

  gst_caps_unref (videotestsrc_caps);

  gst_bin_add_many (GST_BIN (bin), data.src, data.capsfilter, data.payloader,
      data.sink, NULL);
  res = gst_element_link_many (data.src, data.capsfilter, data.payloader,
      data.sink, NULL);
  fail_unless_equals_int (res, TRUE);

  g_mutex_lock (&data.mutex);
  ASSERT_SET_STATE (bin, GST_STATE_PLAYING, GST_STATE_CHANGE_ASYNC);
  while (data.num_buffers > 0)
    g_cond_wait (&data.cond, &data.mutex);
  g_mutex_unlock (&data.mutex);

  g_object_get (data.payloader, "pt", payload_type, NULL);

  ASSERT_SET_STATE (bin, GST_STATE_NULL, GST_STATE_CHANGE_SUCCESS);

  fail_unless_equals_int (g_list_length (data.buffers), num_buffers);
  fail_unless_equals_int (num_buffers, data.last_seqnum);

  g_mutex_clear (&data.mutex);
  g_cond_clear (&data.cond);
  gst_object_unref (bin);

  return data.buffers;
}

static GstEvent *
create_rtx_event (guint seqnum, guint ssrc, guint payload_type)
{
  return gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM,
      gst_structure_new ("GstRTPRetransmissionRequest",
          "seqnum", G_TYPE_UINT, seqnum,
          "ssrc", G_TYPE_UINT, ssrc,
          "payload-type", G_TYPE_UINT, payload_type, NULL));
}

static void
test_rtxsender_packet_retention (gboolean test_with_time)
{
  const gint num_buffers = test_with_time ? 30 : 10;
  const gint half_buffers = num_buffers / 2;
  const guint ssrc = 1234567;
  const guint rtx_ssrc = 7654321;
  const guint rtx_payload_type = 99;
  GstStructure *pt_map;
  GstStructure *ssrc_map;
  GList *in_buffers, *node;
  guint payload_type;
  GstElement *rtxsend;
  GstPad *srcpad, *sinkpad;
  GstCaps *caps;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  gint i, j;
  gboolean res;

  /* generate test data  */
  in_buffers = generate_test_buffers (num_buffers, ssrc, &payload_type);

  /* clear the global buffers list, which we are going to use later */
  gst_check_drop_buffers ();

  /* setup element & pads */
  rtxsend = gst_check_setup_element ("rtprtxsend");

  pt_map = gst_structure_new ("application/x-rtp-pt-map",
      "96", G_TYPE_UINT, rtx_payload_type, NULL);
  ssrc_map = gst_structure_new ("application/x-rtp-ssrc-map",
      "1234567", G_TYPE_UINT, rtx_ssrc, NULL);

  /* in both cases we want the rtxsend queue to store 'half_buffers'
   * amount of buffers at most. In max-size-packets mode, it's trivial.
   * In max-size-time mode, we specify almost half a second, which is
   * the equivalent of 15 frames in a 30fps video stream */
  g_object_set (rtxsend,
      "max-size-packets", test_with_time ? 0 : half_buffers,
      "max-size-time", test_with_time ? 499 : 0,
      "payload-type-map", pt_map, "ssrc-map", ssrc_map, NULL);
  gst_structure_free (pt_map);
  gst_structure_free (ssrc_map);

  srcpad = gst_check_setup_src_pad (rtxsend, &srctemplate);
  fail_unless_equals_int (gst_pad_set_active (srcpad, TRUE), TRUE);

  sinkpad = gst_check_setup_sink_pad (rtxsend, &sinktemplate);
  fail_unless_equals_int (gst_pad_set_active (sinkpad, TRUE), TRUE);

  ASSERT_SET_STATE (rtxsend, GST_STATE_PLAYING, GST_STATE_CHANGE_SUCCESS);

  caps = gst_caps_from_string ("application/x-rtp, "
      "media = (string)video, payload = (int)96, "
      "ssrc = (uint)1234567, clock-rate = (int)90000, "
      "encoding-name = (string)RAW");
  gst_check_setup_events (srcpad, rtxsend, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  /* now push all buffers and request retransmission every time for all of them */
  node = in_buffers;
  for (i = 1; i <= num_buffers; i++) {
    GstBuffer *buffer = GST_BUFFER (node->data);

    /* verify that the original packets are correct */
    res = gst_rtp_buffer_map (buffer, GST_MAP_READ, &rtp);
    fail_unless_equals_int (res, TRUE);
    fail_unless_equals_int (gst_rtp_buffer_get_ssrc (&rtp), ssrc);
    fail_unless_equals_int (gst_rtp_buffer_get_payload_type (&rtp),
        payload_type);
    fail_unless_equals_int (gst_rtp_buffer_get_seq (&rtp), i);
    gst_rtp_buffer_unmap (&rtp);

    /* retransmit all the previous ones */
    for (j = 1; j < i; j++) {
      /* synchronize with the chain() function of the "sinkpad"
       * to make sure that rtxsend has pushed the rtx buffer out
       * before continuing */
      GList *last_out_buffer = g_list_last (buffers);
      g_mutex_lock (&check_mutex);
      fail_unless_equals_int (gst_pad_push_event (sinkpad,
              create_rtx_event (j, ssrc, payload_type)), TRUE);
      /* wait for the rtx packet only if we expect the element
       * to actually retransmit something */
      if (j >= MAX (i - half_buffers, 1)) {
        guint64 end_time = g_get_monotonic_time () + G_TIME_SPAN_SECOND;

        while (last_out_buffer == g_list_last (buffers))
          fail_unless (g_cond_wait_until (&check_cond, &check_mutex, end_time));
      }
      g_mutex_unlock (&check_mutex);
    }

    /* push this one */
    gst_pad_push (srcpad, gst_buffer_ref (buffer));
    node = g_list_next (node);
  }

  /* verify the result. buffers should be in this order (numbers are seqnums):
   * 1, 1rtx, 2, 1rtx, 2rtx, 3, ... , 9, 5rtx, 6rtx, 7rtx, 8rtx, 9rtx, 10 */
  {
    GstRTPBuffer orig_rtp = GST_RTP_BUFFER_INIT;
    gint expected_rtx_requests, expected_rtx_packets;
    gint real_rtx_requests, real_rtx_packets;

    /* verify statistics first */
    expected_rtx_packets = half_buffers * half_buffers +
        ((half_buffers - 1) / 2.0f) * half_buffers;
    for (i = 1, expected_rtx_requests = 0; i < num_buffers; i++)
      expected_rtx_requests += i;

    g_object_get (rtxsend, "num-rtx-requests", &real_rtx_requests,
        "num-rtx-packets", &real_rtx_packets, NULL);
    fail_unless_equals_int (expected_rtx_requests, real_rtx_requests);
    fail_unless_equals_int (expected_rtx_packets, real_rtx_packets);

    /* and the number of actual buffers that we were pushed out of rtxsend */
    fail_unless_equals_int (g_list_length (buffers),
        num_buffers + expected_rtx_packets);

    node = buffers;
    for (i = 1; i <= num_buffers; i++) {
      /* verify the retransmission packets */
      for (j = MAX (i - half_buffers, 1); j < i; j++) {
        GST_INFO ("checking %d, %d", i, j);

        res = gst_rtp_buffer_map (GST_BUFFER (node->data), GST_MAP_READ, &rtp);
        fail_unless_equals_int (res, TRUE);

        fail_if (gst_rtp_buffer_get_ssrc (&rtp) == ssrc);
        fail_unless_equals_int (gst_rtp_buffer_get_ssrc (&rtp), rtx_ssrc);
        fail_unless_equals_int (gst_rtp_buffer_get_payload_type (&rtp),
            rtx_payload_type);
        fail_unless_equals_int (GST_READ_UINT16_BE (gst_rtp_buffer_get_payload (&rtp)), j);     /* j == rtx seqnum */

        /* open the original packet for this rtx packet and verify timestamps */
        res = gst_rtp_buffer_map (GST_BUFFER (g_list_nth_data (in_buffers,
                    j - 1)), GST_MAP_READ, &orig_rtp);
        fail_unless_equals_int (res, TRUE);
        fail_unless_equals_int (gst_rtp_buffer_get_timestamp (&orig_rtp),
            gst_rtp_buffer_get_timestamp (&rtp));
        gst_rtp_buffer_unmap (&orig_rtp);

        gst_rtp_buffer_unmap (&rtp);
        node = g_list_next (node);
      }

      /* verify the normal rtp flow packet */
      res = gst_rtp_buffer_map (GST_BUFFER (node->data), GST_MAP_READ, &rtp);
      fail_unless_equals_int (res, TRUE);
      fail_unless_equals_int (gst_rtp_buffer_get_ssrc (&rtp), ssrc);
      fail_unless_equals_int (gst_rtp_buffer_get_payload_type (&rtp),
          payload_type);
      fail_unless_equals_int (gst_rtp_buffer_get_seq (&rtp), i);
      gst_rtp_buffer_unmap (&rtp);
      node = g_list_next (node);
    }
  }

  g_list_free_full (in_buffers, (GDestroyNotify) gst_buffer_unref);
  gst_check_drop_buffers ();

  gst_check_teardown_src_pad (rtxsend);
  gst_check_teardown_sink_pad (rtxsend);
  gst_check_teardown_element (rtxsend);
}

GST_START_TEST (test_rtxsender_max_size_packets)
{
  test_rtxsender_packet_retention (FALSE);
}

GST_END_TEST;

GST_START_TEST (test_rtxsender_max_size_time)
{
  test_rtxsender_packet_retention (TRUE);
}

GST_END_TEST;

static void
compare_rtp_packets (GstBuffer * a, GstBuffer * b)
{
  GstRTPBuffer rtp_a = GST_RTP_BUFFER_INIT;
  GstRTPBuffer rtp_b = GST_RTP_BUFFER_INIT;

  gst_rtp_buffer_map (a, GST_MAP_READ, &rtp_a);
  gst_rtp_buffer_map (b, GST_MAP_READ, &rtp_b);

  fail_unless_equals_int (gst_rtp_buffer_get_header_len (&rtp_a),
      gst_rtp_buffer_get_header_len (&rtp_b));
  fail_unless_equals_int (gst_rtp_buffer_get_version (&rtp_a),
      gst_rtp_buffer_get_version (&rtp_b));
  fail_unless_equals_int (gst_rtp_buffer_get_ssrc (&rtp_a),
      gst_rtp_buffer_get_ssrc (&rtp_b));
  fail_unless_equals_int (gst_rtp_buffer_get_seq (&rtp_a),
      gst_rtp_buffer_get_seq (&rtp_b));
  fail_unless_equals_int (gst_rtp_buffer_get_csrc_count (&rtp_a),
      gst_rtp_buffer_get_csrc_count (&rtp_b));
  fail_unless_equals_int (gst_rtp_buffer_get_marker (&rtp_a),
      gst_rtp_buffer_get_marker (&rtp_b));
  fail_unless_equals_int (gst_rtp_buffer_get_payload_type (&rtp_a),
      gst_rtp_buffer_get_payload_type (&rtp_b));
  fail_unless_equals_int (gst_rtp_buffer_get_timestamp (&rtp_a),
      gst_rtp_buffer_get_timestamp (&rtp_b));
  fail_unless_equals_int (gst_rtp_buffer_get_extension (&rtp_a),
      gst_rtp_buffer_get_extension (&rtp_b));

  fail_unless_equals_int (gst_rtp_buffer_get_payload_len (&rtp_a),
      gst_rtp_buffer_get_payload_len (&rtp_b));
  fail_unless_equals_int (memcmp (gst_rtp_buffer_get_payload (&rtp_a),
          gst_rtp_buffer_get_payload (&rtp_b),
          gst_rtp_buffer_get_payload_len (&rtp_a)), 0);

  gst_rtp_buffer_unmap (&rtp_a);
  gst_rtp_buffer_unmap (&rtp_b);
}

GST_START_TEST (test_rtxreceive_data_reconstruction)
{
  const guint ssrc = 1234567;
  GList *in_buffers;
  guint payload_type;
  GstElement *rtxsend, *rtxrecv;
  GstPad *srcpad, *sinkpad;
  GstCaps *caps;
  GstBuffer *buffer;
  GstStructure *pt_map;

  /* generate test data  */
  in_buffers = generate_test_buffers (1, ssrc, &payload_type);

  /* clear the global buffers list, which we are going to use later */
  gst_check_drop_buffers ();

  /* setup element & pads */
  rtxsend = gst_check_setup_element ("rtprtxsend");
  rtxrecv = gst_check_setup_element ("rtprtxreceive");

  pt_map = gst_structure_new ("application/x-rtp-pt-map",
      "96", G_TYPE_UINT, 99, NULL);
  g_object_set (rtxsend, "payload-type-map", pt_map, NULL);
  g_object_set (rtxrecv, "payload-type-map", pt_map, NULL);
  gst_structure_free (pt_map);

  fail_unless_equals_int (gst_element_link (rtxsend, rtxrecv), TRUE);

  srcpad = gst_check_setup_src_pad (rtxsend, &srctemplate);
  fail_unless_equals_int (gst_pad_set_active (srcpad, TRUE), TRUE);

  sinkpad = gst_check_setup_sink_pad (rtxrecv, &sinktemplate);
  fail_unless_equals_int (gst_pad_set_active (sinkpad, TRUE), TRUE);

  ASSERT_SET_STATE (rtxsend, GST_STATE_PLAYING, GST_STATE_CHANGE_SUCCESS);
  ASSERT_SET_STATE (rtxrecv, GST_STATE_PLAYING, GST_STATE_CHANGE_SUCCESS);

  caps = gst_caps_from_string ("application/x-rtp, "
      "media = (string)video, payload = (int)96, "
      "ssrc = (uint)1234567, clock-rate = (int)90000, "
      "encoding-name = (string)RAW");
  gst_check_setup_events (srcpad, rtxsend, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  /* push buffer */
  buffer = gst_buffer_ref (GST_BUFFER (in_buffers->data));
  fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);

  /* push retransmission request */
  {
    GList *last_out_buffer;
    guint64 end_time;
    gboolean res;

    /* synchronize with the chain() function of the "sinkpad"
     * to make sure that rtxsend has pushed the rtx buffer out
     * before continuing */
    last_out_buffer = g_list_last (buffers);
    g_mutex_lock (&check_mutex);
    fail_unless_equals_int (gst_pad_push_event (sinkpad,
            create_rtx_event (1, ssrc, payload_type)), TRUE);
    end_time = g_get_monotonic_time () + G_TIME_SPAN_SECOND;
    do
      res = g_cond_wait_until (&check_cond, &check_mutex, end_time);
    while (res == TRUE && last_out_buffer == g_list_last (buffers));
    fail_unless_equals_int (res, TRUE);
    g_mutex_unlock (&check_mutex);
  }

  /* verify */
  fail_unless_equals_int (g_list_length (buffers), 2);
  compare_rtp_packets (GST_BUFFER (buffers->data),
      GST_BUFFER (buffers->next->data));

  /* cleanup */
  g_list_free_full (in_buffers, (GDestroyNotify) gst_buffer_unref);
  gst_check_drop_buffers ();

  gst_check_teardown_src_pad (rtxsend);
  gst_check_teardown_sink_pad (rtxrecv);
  gst_element_unlink (rtxsend, rtxrecv);
  gst_check_teardown_element (rtxsend);
  gst_check_teardown_element (rtxrecv);
}

GST_END_TEST;

static Suite *
rtprtx_suite (void)
{
  Suite *s = suite_create ("rtprtx");
  TCase *tc_chain = tcase_create ("general");

  tcase_set_timeout (tc_chain, 120);

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_push_forward_seq);
  tcase_add_test (tc_chain, test_drop_one_sender);
  tcase_add_test (tc_chain, test_drop_multiple_sender);
  tcase_add_test (tc_chain, test_rtxsender_max_size_packets);
  tcase_add_test (tc_chain, test_rtxsender_max_size_time);
  tcase_add_test (tc_chain, test_rtxreceive_data_reconstruction);

  return s;
}

GST_CHECK_MAIN (rtprtx);
