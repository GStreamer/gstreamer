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

static gboolean send_pipeline_eos = FALSE;
static gboolean receive_pipeline_eos = FALSE;

static void
message_received (GstBus * bus, GstMessage * message, GstPipeline * bin)
{
  GST_INFO ("bus message from \"%" GST_PTR_FORMAT "\": %" GST_PTR_FORMAT,
      GST_MESSAGE_SRC (message), message);

  switch (message->type) {
    case GST_MESSAGE_EOS:
      if (!strcmp ("pipeline_send",
              GST_OBJECT_NAME (GST_MESSAGE_SRC (message))))
        send_pipeline_eos = TRUE;
      else if (!strcmp ("pipeline_receive",
              GST_OBJECT_NAME (GST_MESSAGE_SRC (message))))
        receive_pipeline_eos = TRUE;
      else
        fail ("Unknown pipeline: %s",
            GST_OBJECT_NAME (GST_MESSAGE_SRC (message)));
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
      fail ("Error!");
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

static void
on_rtpbinreceive_pad_added (GstElement * element, GstPad * newPad,
    gpointer data)
{
  GstElement *rtpdepayloader = GST_ELEMENT (data);

  gchar *padName = gst_pad_get_name (newPad);
  if (g_str_has_prefix (padName, "recv_rtp_src_")) {
    GstPad *sinkpad = gst_element_get_static_pad (rtpdepayloader, "sink");
    gst_pad_link (newPad, sinkpad);
    gst_object_unref (sinkpad);
  }
  g_free (padName);
}

static gboolean
on_timeout (gpointer data)
{
  GstEvent *eos = gst_event_new_eos ();
  if (!gst_element_send_event (GST_ELEMENT (data), eos)) {
    GST_ERROR ("failed to send end of stream event");
    gst_event_unref (eos);
  }

  return FALSE;
}

static GstElement *
request_aux_receive (GstElement * rtpbin, guint sessid, GstElement * receive)
{
  GstElement *bin;
  GstPad *pad;

  GST_INFO ("creating AUX receiver");
  bin = gst_bin_new (NULL);
  gst_bin_add (GST_BIN (bin), receive);

  pad = gst_element_get_static_pad (receive, "src");
  gst_element_add_pad (bin, gst_ghost_pad_new ("src_0", pad));
  gst_object_unref (pad);
  pad = gst_element_get_static_pad (receive, "sink");
  gst_element_add_pad (bin, gst_ghost_pad_new ("sink_0", pad));
  gst_object_unref (pad);

  return bin;
}

static GstElement *
request_aux_send (GstElement * rtpbin, guint sessid, GstElement * send)
{
  GstElement *bin;
  GstPad *pad;

  GST_INFO ("creating AUX sender");
  bin = gst_bin_new (NULL);
  gst_bin_add (GST_BIN (bin), send);

  pad = gst_element_get_static_pad (send, "src");
  gst_element_add_pad (bin, gst_ghost_pad_new ("src_0", pad));
  gst_object_unref (pad);
  pad = gst_element_get_static_pad (send, "sink");
  gst_element_add_pad (bin, gst_ghost_pad_new ("sink_0", pad));
  gst_object_unref (pad);

  return bin;
}


GST_START_TEST (test_simple_rtpbin_aux)
{
  GstElement *binsend, *rtpbinsend, *src, *encoder, *rtppayloader,
      *rtprtxsend, *sendrtp_udpsink, *sendrtcp_udpsink, *sendrtcp_udpsrc;
  GstElement *binreceive, *rtpbinreceive, *recvrtp_udpsrc, *recvrtcp_udpsrc,
      *recvrtcp_udpsink, *rtprtxreceive, *rtpdepayloader, *decoder, *converter,
      *sink;
  GstBus *bussend;
  GstBus *busreceive;
  gboolean res;
  GstCaps *rtpcaps = NULL;
  GstStructure *pt_map;
  GstStateChangeReturn state_res = GST_STATE_CHANGE_FAILURE;
  GstPad *srcpad = NULL;
  guint nb_rtx_send_packets = 0;
  guint nb_rtx_recv_packets = 0;
  RTXSendData send_rtxdata;
  send_rtxdata.count = 1;
  send_rtxdata.nb_packets = 0;
  send_rtxdata.drop_every_n_packets = 25;

  GST_INFO ("preparing test");

  /* build pipeline */
  binsend = gst_pipeline_new ("pipeline_send");
  bussend = gst_element_get_bus (binsend);
  gst_bus_add_signal_watch_full (bussend, G_PRIORITY_HIGH);

  binreceive = gst_pipeline_new ("pipeline_receive");
  busreceive = gst_element_get_bus (binreceive);
  gst_bus_add_signal_watch_full (busreceive, G_PRIORITY_HIGH);

  rtpbinsend = gst_element_factory_make ("rtpbin", "rtpbinsend");
  g_object_set (rtpbinsend, "latency", 200, "do-retransmission", TRUE, NULL);
  src = gst_element_factory_make ("audiotestsrc", "src");
  encoder = gst_element_factory_make ("alawenc", "encoder");
  rtppayloader = gst_element_factory_make ("rtppcmapay", "rtppayloader");
  rtprtxsend = gst_element_factory_make ("rtprtxsend", "rtprtxsend");
  sendrtp_udpsink = gst_element_factory_make ("udpsink", "sendrtp_udpsink");
  g_object_set (sendrtp_udpsink, "host", "127.0.0.1", NULL);
  g_object_set (sendrtp_udpsink, "port", 5006, NULL);
  sendrtcp_udpsink = gst_element_factory_make ("udpsink", "sendrtcp_udpsink");
  g_object_set (sendrtcp_udpsink, "host", "127.0.0.1", NULL);
  g_object_set (sendrtcp_udpsink, "port", 5007, NULL);
  g_object_set (sendrtcp_udpsink, "sync", FALSE, NULL);
  g_object_set (sendrtcp_udpsink, "async", FALSE, NULL);
  sendrtcp_udpsrc = gst_element_factory_make ("udpsrc", "sendrtcp_udpsrc");
  g_object_set (sendrtcp_udpsrc, "port", 5009, NULL);

  rtpbinreceive = gst_element_factory_make ("rtpbin", "rtpbinreceive");
  g_object_set (rtpbinreceive, "latency", 200, "do-retransmission", TRUE, NULL);
  recvrtp_udpsrc = gst_element_factory_make ("udpsrc", "recvrtp_udpsrc");
  g_object_set (recvrtp_udpsrc, "port", 5006, NULL);
  rtpcaps =
      gst_caps_from_string
      ("application/x-rtp,media=(string)audio,clock-rate=(int)8000,encoding-name=(string)PCMA,payload=(int)96");
  g_object_set (recvrtp_udpsrc, "caps", rtpcaps, NULL);
  gst_caps_unref (rtpcaps);
  recvrtcp_udpsrc = gst_element_factory_make ("udpsrc", "recvrtcp_udpsrc");
  g_object_set (recvrtcp_udpsrc, "port", 5007, NULL);
  recvrtcp_udpsink = gst_element_factory_make ("udpsink", "recvrtcp_udpsink");
  g_object_set (recvrtcp_udpsink, "host", "127.0.0.1", NULL);
  g_object_set (recvrtcp_udpsink, "port", 5009, NULL);
  g_object_set (recvrtcp_udpsink, "sync", FALSE, NULL);
  g_object_set (recvrtcp_udpsink, "async", FALSE, NULL);
  rtprtxreceive = gst_element_factory_make ("rtprtxreceive", "rtprtxreceive");
  rtpdepayloader = gst_element_factory_make ("rtppcmadepay", "rtpdepayloader");
  decoder = gst_element_factory_make ("alawdec", "decoder");
  converter = gst_element_factory_make ("identity", "converter");
  sink = gst_element_factory_make ("fakesink", "sink");
  g_object_set (sink, "sync", TRUE, NULL);

  gst_bin_add_many (GST_BIN (binsend), rtpbinsend, src, encoder, rtppayloader,
      sendrtp_udpsink, sendrtcp_udpsink, sendrtcp_udpsrc, NULL);

  gst_bin_add_many (GST_BIN (binreceive), rtpbinreceive,
      recvrtp_udpsrc, recvrtcp_udpsrc, recvrtcp_udpsink,
      rtpdepayloader, decoder, converter, sink, NULL);

  g_signal_connect (rtpbinreceive, "pad-added",
      G_CALLBACK (on_rtpbinreceive_pad_added), rtpdepayloader);

  pt_map = gst_structure_new ("application/x-rtp-pt-map",
      "96", G_TYPE_UINT, 99, NULL);
  g_object_set (rtppayloader, "pt", 96, NULL);
  g_object_set (rtppayloader, "seqnum-offset", 1, NULL);
  g_object_set (rtprtxsend, "payload-type-map", pt_map, NULL);
  g_object_set (rtprtxreceive, "payload-type-map", pt_map, NULL);
  gst_structure_free (pt_map);

  /* set rtp aux receive */
  g_signal_connect (rtpbinreceive, "request-aux-receiver", (GCallback)
      request_aux_receive, rtprtxreceive);
  /* set rtp aux send */
  g_signal_connect (rtpbinsend, "request-aux-sender", (GCallback)
      request_aux_send, rtprtxsend);

  /* gst-launch-1.0 rtpbin name=rtpbin audiotestsrc ! amrnbenc ! rtpamrpay ! \
   * rtpbin.send_rtp_sink_1 rtpbin.send_rtp_src_1 ! udpsink host="127.0.0.1" \
   * port=5002 rtpbin.send_rtcp_src_1 ! udpsink host="127.0.0.1" port=5003 \
   * sync=false async=false  udpsrc port=5007 ! rtpbin.recv_rtcp_sink_1
   */

  res = gst_element_link (src, encoder);
  fail_unless (res == TRUE, NULL);
  res = gst_element_link (encoder, rtppayloader);
  fail_unless (res == TRUE, NULL);
  res =
      gst_element_link_pads_full (rtppayloader, "src", rtpbinsend,
      "send_rtp_sink_0", GST_PAD_LINK_CHECK_NOTHING);
  fail_unless (res == TRUE, NULL);
  res =
      gst_element_link_pads_full (rtpbinsend, "send_rtp_src_0", sendrtp_udpsink,
      "sink", GST_PAD_LINK_CHECK_NOTHING);
  fail_unless (res == TRUE, NULL);
  res =
      gst_element_link_pads_full (rtpbinsend, "send_rtcp_src_0",
      sendrtcp_udpsink, "sink", GST_PAD_LINK_CHECK_NOTHING);
  fail_unless (res == TRUE, NULL);
  res =
      gst_element_link_pads_full (sendrtcp_udpsrc, "src", rtpbinsend,
      "recv_rtcp_sink_0", GST_PAD_LINK_CHECK_NOTHING);
  fail_unless (res == TRUE, NULL);

  srcpad = gst_element_get_static_pad (rtpbinsend, "send_rtp_src_0");
  gst_pad_add_probe (srcpad,
      (GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_PUSH),
      (GstPadProbeCallback) rtprtxsend_srcpad_probe, &send_rtxdata, NULL);
  gst_object_unref (srcpad);

  /* gst-launch-1.0 rtpbin name=rtpbin udpsrc caps="application/x-rtp,media=(string)audio, \
   * clock-rate=(int)8000,encoding-name=(string)AMR,encoding-params=(string)1,o
   * ctet-align=(string)1" port=5002 ! rtpbin.recv_rtp_sink_1 rtpbin. ! rtpamrdepay ! \
   * amrnbdec ! fakesink sync=True  udpsrc port=5003 ! rtpbin.recv_rtcp_sink_1 \
   * rtpbin.send_rtcp_src_1 ! udpsink host="127.0.0.1" port=5007 sync=false async=false
   */

  res =
      gst_element_link_pads_full (recvrtp_udpsrc, "src", rtpbinreceive,
      "recv_rtp_sink_0", GST_PAD_LINK_CHECK_NOTHING);
  fail_unless (res == TRUE, NULL);
  res =
      gst_element_link_pads_full (rtpdepayloader, "src", decoder, "sink",
      GST_PAD_LINK_CHECK_NOTHING);
  fail_unless (res == TRUE, NULL);
  res = gst_element_link (decoder, converter);
  fail_unless (res == TRUE, NULL);
  res =
      gst_element_link_pads_full (converter, "src", sink, "sink",
      GST_PAD_LINK_CHECK_NOTHING);
  fail_unless (res == TRUE, NULL);
  res =
      gst_element_link_pads_full (recvrtcp_udpsrc, "src", rtpbinreceive,
      "recv_rtcp_sink_0", GST_PAD_LINK_CHECK_NOTHING);
  fail_unless (res == TRUE, NULL);
  res =
      gst_element_link_pads_full (rtpbinreceive, "send_rtcp_src_0",
      recvrtcp_udpsink, "sink", GST_PAD_LINK_CHECK_NOTHING);
  fail_unless (res == TRUE, NULL);

  g_signal_connect (bussend, "message::error", (GCallback) message_received,
      binsend);
  g_signal_connect (bussend, "message::warning", (GCallback) message_received,
      binsend);
  g_signal_connect (bussend, "message::eos", (GCallback) message_received,
      binsend);

  g_signal_connect (busreceive, "message::error", (GCallback) message_received,
      binreceive);
  g_signal_connect (busreceive, "message::warning",
      (GCallback) message_received, binreceive);
  g_signal_connect (busreceive, "message::eos", (GCallback) message_received,
      binreceive);

  state_res = gst_element_set_state (binreceive, GST_STATE_PLAYING);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  state_res = gst_element_set_state (binsend, GST_STATE_PLAYING);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  g_timeout_add (5000, on_timeout, binsend);
  g_timeout_add (5000, on_timeout, binreceive);

  GST_INFO ("enter mainloop");
  while (!send_pipeline_eos && !receive_pipeline_eos)
    g_main_context_iteration (NULL, TRUE);
  GST_INFO ("exit mainloop");

  /* check that FB NACK is working */
  g_object_get (G_OBJECT (rtprtxsend), "num-rtx-requests", &nb_rtx_send_packets,
      NULL);
  g_object_get (G_OBJECT (rtprtxreceive), "num-rtx-requests",
      &nb_rtx_recv_packets, NULL);

  state_res = gst_element_set_state (binsend, GST_STATE_NULL);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  state_res = gst_element_set_state (binreceive, GST_STATE_NULL);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  GST_INFO ("nb_rtx_send_packets %d", nb_rtx_send_packets);
  GST_INFO ("nb_rtx_recv_packets %d", nb_rtx_recv_packets);
  fail_if (nb_rtx_send_packets < 1);
  fail_if (nb_rtx_recv_packets < 1);

  /* cleanup */
  gst_bus_remove_signal_watch (bussend);
  gst_object_unref (bussend);
  gst_object_unref (binsend);

  gst_bus_remove_signal_watch (busreceive);
  gst_object_unref (busreceive);
  gst_object_unref (binreceive);
}

GST_END_TEST;

static Suite *
rtpaux_suite (void)
{
  Suite *s = suite_create ("rtpaux");
  TCase *tc_chain = tcase_create ("general");

  tcase_set_timeout (tc_chain, 10000);

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_simple_rtpbin_aux);

  return s;
}

GST_CHECK_MAIN (rtpaux);
