/* GStreamer
 *
 * Copyright (C) 2016 Igalia S.L.
 *   @author Philippe Normand <philn@igalia.com>
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

static GMainLoop *main_loop;

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
      fail ("Error!");
      break;
    }
    default:
      break;
  }
}

static void
on_rtpbinreceive_pad_added (GstElement * element, GstPad * new_pad,
    gpointer data)
{
  GstElement *pipeline = GST_ELEMENT (data);
  gchar *pad_name = gst_pad_get_name (new_pad);

  if (g_str_has_prefix (pad_name, "recv_rtp_src_")) {
    GstCaps *caps = gst_pad_get_current_caps (new_pad);
    GstStructure *s = gst_caps_get_structure (caps, 0);
    const gchar *media_type = gst_structure_get_string (s, "media");
    gchar *depayloader_name = g_strdup_printf ("%s_rtpdepayloader", media_type);
    GstElement *rtpdepayloader =
        gst_bin_get_by_name (GST_BIN (pipeline), depayloader_name);
    GstPad *sinkpad;

    g_free (depayloader_name);
    fail_unless (rtpdepayloader != NULL, NULL);

    sinkpad = gst_element_get_static_pad (rtpdepayloader, "sink");
    gst_pad_link (new_pad, sinkpad);
    gst_object_unref (sinkpad);
    gst_object_unref (rtpdepayloader);

    gst_caps_unref (caps);
  }
  g_free (pad_name);
}

static guint
on_bundled_ssrc (GstElement * rtpbin, guint ssrc, gpointer user_data)
{
  static gboolean create_session = FALSE;
  guint session_id = 0;

  if (create_session) {
    session_id = 1;
  } else {
    create_session = TRUE;
    /* use existing session 0, a new session will be created for the next discovered bundled SSRC */
  }
  return session_id;
}

static GstCaps *
on_request_pt_map (GstElement * rtpbin, guint session_id, guint pt,
    gpointer user_data)
{
  GstCaps *caps = NULL;
  if (pt == 96) {
    caps =
        gst_caps_from_string
        ("application/x-rtp,media=(string)audio,encoding-name=(string)PCMA,clock-rate=(int)8000");
  } else if (pt == 100) {
    caps =
        gst_caps_from_string
        ("application/x-rtp,media=(string)video,encoding-name=(string)RAW,clock-rate=(int)90000,sampling=(string)\"YCbCr-4:2:0\",depth=(string)8,width=(string)320,height=(string)240");
  }
  return caps;
}


static GstElement *
create_pipeline (gboolean send)
{
  GstElement *pipeline, *rtpbin, *audiosrc, *audio_encoder,
      *audio_rtppayloader, *sendrtp_udpsink, *recv_rtp_udpsrc,
      *send_rtcp_udpsink, *recv_rtcp_udpsrc, *sendrtcp_funnel, *sendrtp_funnel;
  GstElement *audio_rtpdepayloader, *audio_decoder, *audio_sink;
  GstElement *videosrc, *video_rtppayloader, *video_rtpdepayloader, *video_sink;
  gboolean res;
  GstPad *funnel_pad, *rtp_src_pad;
  GstCaps *rtpcaps;
  gint rtp_udp_port = 5001;
  gint rtcp_udp_port = 5002;

  pipeline = gst_pipeline_new (send ? "pipeline_send" : "pipeline_receive");

  rtpbin =
      gst_element_factory_make ("rtpbin",
      send ? "rtpbin_send" : "rtpbin_receive");
  g_object_set (rtpbin, "latency", 200, NULL);

  if (!send) {
    g_signal_connect (rtpbin, "on-bundled-ssrc",
        G_CALLBACK (on_bundled_ssrc), NULL);
    g_signal_connect (rtpbin, "request-pt-map",
        G_CALLBACK (on_request_pt_map), NULL);
  }

  g_signal_connect (rtpbin, "pad-added",
      G_CALLBACK (on_rtpbinreceive_pad_added), pipeline);

  gst_bin_add (GST_BIN (pipeline), rtpbin);

  if (send) {
    audiosrc = gst_element_factory_make ("audiotestsrc", NULL);
    audio_encoder = gst_element_factory_make ("alawenc", NULL);
    audio_rtppayloader = gst_element_factory_make ("rtppcmapay", NULL);
    g_object_set (audio_rtppayloader, "pt", 96, NULL);
    g_object_set (audio_rtppayloader, "seqnum-offset", 1, NULL);

    videosrc = gst_element_factory_make ("videotestsrc", NULL);
    video_rtppayloader = gst_element_factory_make ("rtpvrawpay", NULL);
    g_object_set (video_rtppayloader, "pt", 100, "seqnum-offset", 1, NULL);

    g_object_set (audiosrc, "num-buffers", 5, NULL);
    g_object_set (videosrc, "num-buffers", 5, NULL);

    /* muxed rtcp */
    sendrtcp_funnel = gst_element_factory_make ("funnel", "send_rtcp_funnel");
    send_rtcp_udpsink = gst_element_factory_make ("udpsink", NULL);
    g_object_set (send_rtcp_udpsink, "host", "127.0.0.1", NULL);
    g_object_set (send_rtcp_udpsink, "port", rtcp_udp_port, NULL);
    g_object_set (send_rtcp_udpsink, "sync", FALSE, NULL);
    g_object_set (send_rtcp_udpsink, "async", FALSE, NULL);

    /* outgoing bundled stream */
    sendrtp_funnel = gst_element_factory_make ("funnel", "send_rtp_funnel");
    sendrtp_udpsink = gst_element_factory_make ("udpsink", NULL);
    g_object_set (sendrtp_udpsink, "host", "127.0.0.1", NULL);
    g_object_set (sendrtp_udpsink, "port", rtp_udp_port, NULL);

    gst_bin_add_many (GST_BIN (pipeline), audiosrc, audio_encoder,
        audio_rtppayloader, sendrtp_udpsink, send_rtcp_udpsink,
        sendrtp_funnel, sendrtcp_funnel, videosrc, video_rtppayloader, NULL);

    res = gst_element_link (audiosrc, audio_encoder);
    fail_unless (res == TRUE, NULL);
    res = gst_element_link (audio_encoder, audio_rtppayloader);
    fail_unless (res == TRUE, NULL);
    res =
        gst_element_link_pads_full (audio_rtppayloader, "src", rtpbin,
        "send_rtp_sink_0", GST_PAD_LINK_CHECK_NOTHING);
    fail_unless (res == TRUE, NULL);

    res = gst_element_link (videosrc, video_rtppayloader);
    fail_unless (res == TRUE, NULL);
    res =
        gst_element_link_pads_full (video_rtppayloader, "src", rtpbin,
        "send_rtp_sink_1", GST_PAD_LINK_CHECK_NOTHING);
    fail_unless (res == TRUE, NULL);

    res =
        gst_element_link_pads_full (sendrtp_funnel, "src", sendrtp_udpsink,
        "sink", GST_PAD_LINK_CHECK_NOTHING);
    fail_unless (res == TRUE, NULL);

    funnel_pad = gst_element_get_request_pad (sendrtp_funnel, "sink_%u");
    rtp_src_pad = gst_element_get_static_pad (rtpbin, "send_rtp_src_0");
    res = gst_pad_link (rtp_src_pad, funnel_pad);
    gst_object_unref (funnel_pad);
    gst_object_unref (rtp_src_pad);

    funnel_pad = gst_element_get_request_pad (sendrtp_funnel, "sink_%u");
    rtp_src_pad = gst_element_get_static_pad (rtpbin, "send_rtp_src_1");
    res = gst_pad_link (rtp_src_pad, funnel_pad);
    gst_object_unref (funnel_pad);
    gst_object_unref (rtp_src_pad);

    res =
        gst_element_link_pads_full (sendrtcp_funnel, "src", send_rtcp_udpsink,
        "sink", GST_PAD_LINK_CHECK_NOTHING);
    fail_unless (res == TRUE, NULL);

    funnel_pad = gst_element_get_request_pad (sendrtcp_funnel, "sink_%u");
    rtp_src_pad = gst_element_get_request_pad (rtpbin, "send_rtcp_src_0");
    res =
        gst_pad_link_full (rtp_src_pad, funnel_pad, GST_PAD_LINK_CHECK_NOTHING);
    gst_object_unref (funnel_pad);
    gst_object_unref (rtp_src_pad);

    funnel_pad = gst_element_get_request_pad (sendrtcp_funnel, "sink_%u");
    rtp_src_pad = gst_element_get_request_pad (rtpbin, "send_rtcp_src_1");
    res =
        gst_pad_link_full (rtp_src_pad, funnel_pad, GST_PAD_LINK_CHECK_NOTHING);
    gst_object_unref (funnel_pad);
    gst_object_unref (rtp_src_pad);

  } else {
    recv_rtp_udpsrc = gst_element_factory_make ("udpsrc", NULL);
    g_object_set (recv_rtp_udpsrc, "port", rtp_udp_port, NULL);
    rtpcaps = gst_caps_from_string ("application/x-rtp");
    g_object_set (recv_rtp_udpsrc, "caps", rtpcaps, NULL);
    gst_caps_unref (rtpcaps);

    recv_rtcp_udpsrc = gst_element_factory_make ("udpsrc", NULL);
    g_object_set (recv_rtcp_udpsrc, "port", rtcp_udp_port, NULL);

    audio_rtpdepayloader =
        gst_element_factory_make ("rtppcmadepay", "audio_rtpdepayloader");
    audio_decoder = gst_element_factory_make ("alawdec", NULL);
    audio_sink = gst_element_factory_make ("fakesink", NULL);
    g_object_set (audio_sink, "sync", TRUE, NULL);

    video_rtpdepayloader =
        gst_element_factory_make ("rtpvrawdepay", "video_rtpdepayloader");
    video_sink = gst_element_factory_make ("fakesink", NULL);
    g_object_set (video_sink, "sync", TRUE, NULL);

    gst_bin_add_many (GST_BIN (pipeline), recv_rtp_udpsrc, recv_rtcp_udpsrc,
        audio_rtpdepayloader, audio_decoder, audio_sink, video_rtpdepayloader,
        video_sink, NULL);

    res =
        gst_element_link_pads_full (audio_rtpdepayloader, "src", audio_decoder,
        "sink", GST_PAD_LINK_CHECK_NOTHING);
    fail_unless (res == TRUE, NULL);
    res = gst_element_link (audio_decoder, audio_sink);
    fail_unless (res == TRUE, NULL);

    res =
        gst_element_link_pads_full (video_rtpdepayloader, "src", video_sink,
        "sink", GST_PAD_LINK_CHECK_NOTHING);
    fail_unless (res == TRUE, NULL);

    /* request a single receiving RTP session. */
    res =
        gst_element_link_pads_full (recv_rtcp_udpsrc, "src", rtpbin,
        "recv_rtcp_sink_0", GST_PAD_LINK_CHECK_NOTHING);
    fail_unless (res == TRUE, NULL);
    res =
        gst_element_link_pads_full (recv_rtp_udpsrc, "src", rtpbin,
        "recv_rtp_sink_0", GST_PAD_LINK_CHECK_NOTHING);
    fail_unless (res == TRUE, NULL);
  }

  return pipeline;
}

GST_START_TEST (test_simple_rtpbin_bundle)
{
  GstElement *send_pipeline, *recv_pipeline;
  GstBus *send_bus, *recv_bus;
  GstStateChangeReturn state_res = GST_STATE_CHANGE_FAILURE;
  GstElement *rtpbin_receive;
  GObject *rtp_session;

  main_loop = g_main_loop_new (NULL, FALSE);

  send_pipeline = create_pipeline (TRUE);
  recv_pipeline = create_pipeline (FALSE);

  send_bus = gst_element_get_bus (send_pipeline);
  gst_bus_add_signal_watch_full (send_bus, G_PRIORITY_HIGH);

  g_signal_connect (send_bus, "message::error", (GCallback) message_received,
      send_pipeline);
  g_signal_connect (send_bus, "message::warning", (GCallback) message_received,
      send_pipeline);
  g_signal_connect (send_bus, "message::eos", (GCallback) message_received,
      send_pipeline);

  recv_bus = gst_element_get_bus (recv_pipeline);
  gst_bus_add_signal_watch_full (recv_bus, G_PRIORITY_HIGH);

  g_signal_connect (recv_bus, "message::error", (GCallback) message_received,
      recv_pipeline);
  g_signal_connect (recv_bus, "message::warning", (GCallback) message_received,
      recv_pipeline);
  g_signal_connect (recv_bus, "message::eos", (GCallback) message_received,
      recv_pipeline);

  state_res = gst_element_set_state (recv_pipeline, GST_STATE_PLAYING);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  state_res = gst_element_set_state (send_pipeline, GST_STATE_PLAYING);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  GST_INFO ("enter mainloop");
  g_main_loop_run (main_loop);
  GST_INFO ("exit mainloop");

  rtpbin_receive =
      gst_bin_get_by_name (GST_BIN (recv_pipeline), "rtpbin_receive");
  fail_if (rtpbin_receive == NULL, NULL);

  /* Check that 2 RTP sessions where created while only one was explicitely requested. */
  g_signal_emit_by_name (rtpbin_receive, "get-internal-session", 0,
      &rtp_session);
  fail_if (rtp_session == NULL, NULL);
  g_object_unref (rtp_session);
  g_signal_emit_by_name (rtpbin_receive, "get-internal-session", 1,
      &rtp_session);
  fail_if (rtp_session == NULL, NULL);
  g_object_unref (rtp_session);

  gst_object_unref (rtpbin_receive);

  state_res = gst_element_set_state (send_pipeline, GST_STATE_NULL);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  state_res = gst_element_set_state (recv_pipeline, GST_STATE_NULL);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  /* cleanup */
  g_main_loop_unref (main_loop);

  gst_bus_remove_signal_watch (send_bus);
  gst_object_unref (send_bus);
  gst_object_unref (send_pipeline);

  gst_bus_remove_signal_watch (recv_bus);
  gst_object_unref (recv_bus);
  gst_object_unref (recv_pipeline);

}

GST_END_TEST;

static Suite *
rtpbundle_suite (void)
{
  Suite *s = suite_create ("rtpbundle");
  TCase *tc_chain = tcase_create ("general");

  tcase_set_timeout (tc_chain, 10000);

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_simple_rtpbin_bundle);

  return s;
}

GST_CHECK_MAIN (rtpbundle);
