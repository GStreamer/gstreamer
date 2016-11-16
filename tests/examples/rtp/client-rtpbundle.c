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
 * RTP bundle receiver
 *
 * In this example we initially create one RTP session but the incoming RTP
 * and RTCP streams actually bundle 2 different media type, one audio stream
 * and one video stream. We are notified of the discovery of the streams by
 * the on-bundled-ssrc rtpbin signal. In the handler we decide to assign the
 * first SSRC to the (existing) audio session and the second SSRC to a new
 * session (id: 1).
 *
 *             .-------.      .----------.        .-----------.    .-------.    .-------------.
 *  RTP        |udpsrc |      | rtpbin   |        | pcmadepay |    |alawdec|    |autoaudiosink|
 *  port=5001  |      src->recv_rtp_0 recv_rtp_0->sink       src->sink    src->sink           |
 *             '-------'      |          |        '-----------'    '-------'    '-------------'
 *                            |          |
 *                            |          |     .-------.
 *                            |          |     |udpsink|  RTCP
 *                            |  send_rtcp_0->sink     | port=5003
 *             .-------.      |          |     '-------' sync=false
 *  RTCP       |udpsrc |      |          |               async=false
 *  port=5002  |     src->recv_rtcp_0    |
 *             '-------'      |          |
 *                            |          |
 *                            |          |        .---------.    .-------------.
 *                            |          |        |vrawdepay|    |autovideosink|
 *                            |       recv_rtp_1->sink     src->sink           |
 *                            |          |        '---------'    '-------------'
 *                            |          |
 *                            |          |     .-------.
 *                            |          |     |udpsink|  RTCP
 *                            |  send_rtcp_1->sink     | port=5004
 *                            |          |     '-------' sync=false
 *                            |          |               async=false
 *                            |          |
 *                            '----------'
 *
 */

static gboolean
plug_video_rtcp_sender (gpointer user_data)
{
  gint send_video_rtcp_port = 5004;
  GstElement *rtpbin = GST_ELEMENT_CAST (user_data);
  GstElement *send_video_rtcp_udpsink;
  GstElement *pipeline =
      GST_ELEMENT_CAST (gst_object_get_parent (GST_OBJECT (rtpbin)));

  send_video_rtcp_udpsink = gst_element_factory_make ("udpsink", NULL);
  g_object_set (send_video_rtcp_udpsink, "host", "127.0.0.1", NULL);
  g_object_set (send_video_rtcp_udpsink, "port", send_video_rtcp_port, NULL);
  g_object_set (send_video_rtcp_udpsink, "sync", FALSE, NULL);
  g_object_set (send_video_rtcp_udpsink, "async", FALSE, NULL);
  gst_bin_add (GST_BIN (pipeline), send_video_rtcp_udpsink);
  gst_element_link_pads (rtpbin, "send_rtcp_src_1", send_video_rtcp_udpsink,
      "sink");
  gst_element_sync_state_with_parent (send_video_rtcp_udpsink);

  gst_object_unref (pipeline);
  gst_object_unref (rtpbin);
  return G_SOURCE_REMOVE;
}

static void
on_rtpbinreceive_pad_added (GstElement * rtpbin, GstPad * new_pad,
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

    sinkpad = gst_element_get_static_pad (rtpdepayloader, "sink");
    gst_pad_link (new_pad, sinkpad);
    gst_object_unref (sinkpad);
    gst_object_unref (rtpdepayloader);

    gst_caps_unref (caps);

    if (g_str_has_prefix (pad_name, "recv_rtp_src_1")) {
      g_timeout_add (0, plug_video_rtcp_sender, gst_object_ref (rtpbin));
    }
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
create_pipeline (void)
{
  GstElement *pipeline, *rtpbin, *recv_rtp_udpsrc, *recv_rtcp_udpsrc,
      *audio_rtpdepayloader, *audio_decoder, *audio_sink, *video_rtpdepayloader,
      *video_sink, *send_audio_rtcp_udpsink;
  GstCaps *rtpcaps;
  gint rtp_udp_port = 5001;
  gint rtcp_udp_port = 5002;
  gint send_audio_rtcp_port = 5003;

  pipeline = gst_pipeline_new (NULL);

  rtpbin = gst_element_factory_make ("rtpbin", NULL);
  g_object_set (rtpbin, "latency", 200, NULL);

  g_signal_connect (rtpbin, "on-bundled-ssrc",
      G_CALLBACK (on_bundled_ssrc), NULL);
  g_signal_connect (rtpbin, "request-pt-map",
      G_CALLBACK (on_request_pt_map), NULL);

  g_signal_connect (rtpbin, "pad-added",
      G_CALLBACK (on_rtpbinreceive_pad_added), pipeline);

  gst_bin_add (GST_BIN (pipeline), rtpbin);

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
  audio_sink = gst_element_factory_make ("autoaudiosink", NULL);

  video_rtpdepayloader =
      gst_element_factory_make ("rtpvrawdepay", "video_rtpdepayloader");
  video_sink = gst_element_factory_make ("autovideosink", NULL);

  gst_bin_add_many (GST_BIN (pipeline), recv_rtp_udpsrc, recv_rtcp_udpsrc,
      audio_rtpdepayloader, audio_decoder, audio_sink, video_rtpdepayloader,
      video_sink, NULL);

  gst_element_link_pads (audio_rtpdepayloader, "src", audio_decoder, "sink");
  gst_element_link (audio_decoder, audio_sink);

  gst_element_link_pads (video_rtpdepayloader, "src", video_sink, "sink");

  /* request a single receiving RTP session. */
  gst_element_link_pads (recv_rtcp_udpsrc, "src", rtpbin, "recv_rtcp_sink_0");
  gst_element_link_pads (recv_rtp_udpsrc, "src", rtpbin, "recv_rtp_sink_0");

  send_audio_rtcp_udpsink = gst_element_factory_make ("udpsink", NULL);
  g_object_set (send_audio_rtcp_udpsink, "host", "127.0.0.1", NULL);
  g_object_set (send_audio_rtcp_udpsink, "port", send_audio_rtcp_port, NULL);
  g_object_set (send_audio_rtcp_udpsink, "sync", FALSE, NULL);
  g_object_set (send_audio_rtcp_udpsink, "async", FALSE, NULL);
  gst_bin_add (GST_BIN (pipeline), send_audio_rtcp_udpsink);
  gst_element_link_pads (rtpbin, "send_rtcp_src_0", send_audio_rtcp_udpsink,
      "sink");

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
    if (old == GST_STATE_PAUSED && new == GST_STATE_PLAYING)
      GST_DEBUG_BIN_TO_DOT_FILE (GST_BIN (pipe), GST_DEBUG_GRAPH_SHOW_ALL,
          GST_OBJECT_NAME (message->src));
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
