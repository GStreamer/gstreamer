/* GStreamer
 * Copyright (C) 2016 Igalia S.L
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
#include <gst/gst.h>

/*
 * An bundling RTP server
 *  creates two sessions and streams audio on one, video on the other, with RTCP
 *  on both sessions. The destination is 127.0.0.1.
 *
 *  The RTP streams are bundled to a single outgoing connection. Same for the RTCP streams.
 *
 *  .-------.    .-------.    .-------.      .------------.         .------.
 *  |audiots|    |alawenc|    |pcmapay|      | rtpbin     |         |funnel|
 *  |      src->sink    src->sink    src->send_rtp_0 send_rtp_0--->sink_0  |    .-------.
 *  '-------'    '-------'    '-------'      |            |         |      |    |udpsink|
 *                                           |            |         |     src->sink     |
 *  .-------.               .---------.      |            |         |      |    '-------'
 *  |videots|               | vrawpay |      |            |         |      |
 *  |      src------------>sink      src->send_rtp_1 send_rtp_1--->sink_1  |
 *  '-------'               '---------'      |            |         '------'
 *                                           |            |
 *                               .------.    |            |
 *                               |udpsrc|    |            |         .------.
 *                               |     src->recv_rtcp_0   |         |funnel|
 *                               '------'    |       send_rtcp_0-->sink_0  |   .-------.
 *                                           |            |         |      |   |udpsink|
 *                               .------.    |            |         |    src->sink     |
 *                               |udpsrc|    |            |         |      |   '-------'
 *                               |     src->recv_rtcp_1   |         |      |
 *                               '------'    |       send_rtcp_1-->sink_1  |
 *                                           '------------'         '------'
 *
 */

static GstElement *
create_pipeline (void)
{
  GstElement *pipeline, *rtpbin, *audiosrc, *audio_encoder,
      *audio_rtppayloader, *sendrtp_udpsink,
      *send_rtcp_udpsink, *sendrtcp_funnel, *sendrtp_funnel;
  GstElement *videosrc, *video_rtppayloader, *time_overlay;
  gint rtp_udp_port = 5001;
  gint rtcp_udp_port = 5002;
  gint recv_audio_rtcp_port = 5003;
  gint recv_video_rtcp_port = 5004;
  GstElement *audio_rtcp_udpsrc, *video_rtcp_udpsrc;

  pipeline = gst_pipeline_new (NULL);

  rtpbin = gst_element_factory_make ("rtpbin", NULL);

  audiosrc = gst_element_factory_make ("audiotestsrc", NULL);
  g_object_set (audiosrc, "is-live", TRUE, NULL);
  audio_encoder = gst_element_factory_make ("alawenc", NULL);
  audio_rtppayloader = gst_element_factory_make ("rtppcmapay", NULL);
  g_object_set (audio_rtppayloader, "pt", 96, NULL);

  videosrc = gst_element_factory_make ("videotestsrc", NULL);
  g_object_set (videosrc, "is-live", TRUE, NULL);
  time_overlay = gst_element_factory_make ("timeoverlay", NULL);
  video_rtppayloader = gst_element_factory_make ("rtpvrawpay", NULL);
  g_object_set (video_rtppayloader, "pt", 100, NULL);

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
  g_object_set (sendrtp_udpsink, "sync", FALSE, NULL);
  g_object_set (sendrtp_udpsink, "async", FALSE, NULL);

  gst_bin_add_many (GST_BIN (pipeline), rtpbin, audiosrc, audio_encoder,
      audio_rtppayloader, sendrtp_udpsink, send_rtcp_udpsink,
      sendrtp_funnel, sendrtcp_funnel, videosrc, video_rtppayloader, NULL);

  if (time_overlay)
    gst_bin_add (GST_BIN (pipeline), time_overlay);

  gst_element_link_many (audiosrc, audio_encoder, audio_rtppayloader, NULL);
  gst_element_link_pads (audio_rtppayloader, "src", rtpbin, "send_rtp_sink_0");

  if (time_overlay) {
    gst_element_link_many (videosrc, time_overlay, video_rtppayloader, NULL);
  } else {
    gst_element_link (videosrc, video_rtppayloader);
  }

  gst_element_link_pads (video_rtppayloader, "src", rtpbin, "send_rtp_sink_1");

  gst_element_link_pads (sendrtp_funnel, "src", sendrtp_udpsink, "sink");
  gst_element_link_pads (rtpbin, "send_rtp_src_0", sendrtp_funnel, "sink_%u");
  gst_element_link_pads (rtpbin, "send_rtp_src_1", sendrtp_funnel, "sink_%u");
  gst_element_link_pads (sendrtcp_funnel, "src", send_rtcp_udpsink, "sink");
  gst_element_link_pads (rtpbin, "send_rtcp_src_0", sendrtcp_funnel, "sink_%u");
  gst_element_link_pads (rtpbin, "send_rtcp_src_1", sendrtcp_funnel, "sink_%u");

  audio_rtcp_udpsrc = gst_element_factory_make ("udpsrc", NULL);
  g_object_set (audio_rtcp_udpsrc, "port", recv_audio_rtcp_port, NULL);
  video_rtcp_udpsrc = gst_element_factory_make ("udpsrc", NULL);
  g_object_set (video_rtcp_udpsrc, "port", recv_video_rtcp_port, NULL);
  gst_bin_add_many (GST_BIN (pipeline), audio_rtcp_udpsrc, video_rtcp_udpsrc,
      NULL);
  gst_element_link_pads (audio_rtcp_udpsrc, "src", rtpbin, "recv_rtcp_sink_0");
  gst_element_link_pads (video_rtcp_udpsrc, "src", rtpbin, "recv_rtcp_sink_1");

  return pipeline;
}

/*
 * Used to generate informative messages during pipeline startup
 */
static void
cb_state (GstBus * bus, GstMessage * message, gpointer data)
{
  GstObject *pipe = GST_OBJECT (data);
  GstState old, new, pending;
  gst_message_parse_state_changed (message, &old, &new, &pending);
  if (message->src == pipe) {
    g_print ("Pipeline %s changed state from %s to %s\n",
        GST_OBJECT_NAME (message->src),
        gst_element_state_get_name (old), gst_element_state_get_name (new));
  }
}

int
main (int argc, char **argv)
{
  GstElement *pipe;
  GstBus *bus;
  GMainLoop *loop;

  gst_init (&argc, &argv);

  loop = g_main_loop_new (NULL, FALSE);

  pipe = create_pipeline ();
  bus = gst_element_get_bus (pipe);
  g_signal_connect (bus, "message::state-changed", G_CALLBACK (cb_state), pipe);
  gst_bus_add_signal_watch (bus);
  gst_object_unref (bus);

  g_print ("starting server pipeline\n");
  gst_element_set_state (pipe, GST_STATE_PLAYING);

  g_main_loop_run (loop);

  g_print ("stopping server pipeline\n");
  gst_element_set_state (pipe, GST_STATE_NULL);

  gst_object_unref (pipe);
  g_main_loop_unref (loop);

  return 0;
}
