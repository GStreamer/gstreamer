/* GStreamer
 * Copyright (C) 2008 Wim Taymans <wim.taymans at gmail.com>
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
#include <gst/app/app.h>

#include <gst/rtsp-server/rtsp-server.h>

typedef struct
{
  GstElement *generator_pipe;
  GstElement *vid_appsink;
  GstElement *vid_appsrc;
  GstElement *aud_appsink;
  GstElement *aud_appsrc;
} MyContext;

/* called when we need to give data to an appsrc */
static void
need_data (GstElement * appsrc, guint unused, MyContext * ctx)
{
  GstSample *sample;
  GstFlowReturn ret;

  if (appsrc == ctx->vid_appsrc)
    sample = gst_app_sink_pull_sample (GST_APP_SINK (ctx->vid_appsink));
  else
    sample = gst_app_sink_pull_sample (GST_APP_SINK (ctx->aud_appsink));

  if (sample) {
    GstBuffer *buffer = gst_sample_get_buffer (sample);
    GstSegment *seg = gst_sample_get_segment (sample);
    GstClockTime pts, dts;

    /* Convert the PTS/DTS to running time so they start from 0 */
    pts = GST_BUFFER_PTS (buffer);
    if (GST_CLOCK_TIME_IS_VALID (pts))
      pts = gst_segment_to_running_time (seg, GST_FORMAT_TIME, pts);

    dts = GST_BUFFER_DTS (buffer);
    if (GST_CLOCK_TIME_IS_VALID (dts))
      dts = gst_segment_to_running_time (seg, GST_FORMAT_TIME, dts);

    if (buffer) {
      /* Make writable so we can adjust the timestamps */
      buffer = gst_buffer_copy (buffer);
      GST_BUFFER_PTS (buffer) = pts;
      GST_BUFFER_DTS (buffer) = dts;
      g_signal_emit_by_name (appsrc, "push-buffer", buffer, &ret);
      gst_buffer_unref (buffer);
    }

    /* we don't need the appsink sample anymore */
    gst_sample_unref (sample);
  }
}

static void
ctx_free (MyContext * ctx)
{
  gst_element_set_state (ctx->generator_pipe, GST_STATE_NULL);

  gst_object_unref (ctx->generator_pipe);
  gst_object_unref (ctx->vid_appsrc);
  gst_object_unref (ctx->vid_appsink);
  gst_object_unref (ctx->aud_appsrc);
  gst_object_unref (ctx->aud_appsink);

  g_free (ctx);
}

/* called when a new media pipeline is constructed. We can query the
 * pipeline and configure our appsrc */
static void
media_configure (GstRTSPMediaFactory * factory, GstRTSPMedia * media,
    gpointer user_data)
{
  GstElement *element, *appsrc, *appsink;
  GstCaps *caps;
  MyContext *ctx;

  ctx = g_new0 (MyContext, 1);
  /* This pipeline generates H264 video and PCM audio. The appsinks are kept small so that if delivery is slow,
   * encoded buffers are dropped as needed. There's slightly more buffers (32) allowed for audio */
  ctx->generator_pipe =
      gst_parse_launch
      ("videotestsrc is-live=true ! x264enc speed-preset=superfast tune=zerolatency ! h264parse ! appsink name=vid max-buffers=1 drop=true "
      "audiotestsrc is-live=true ! appsink name=aud max-buffers=32 drop=true",
      NULL);

  /* make sure the data is freed when the media is gone */
  g_object_set_data_full (G_OBJECT (media), "rtsp-extra-data", ctx,
      (GDestroyNotify) ctx_free);

  /* get the element (bin) used for providing the streams of the media */
  element = gst_rtsp_media_get_element (media);

  /* Find the 2 app sources (video / audio), and configure them, connect to the
   * signals to request data */
  /* configure the caps of the video */
  caps = gst_caps_new_simple ("video/x-h264",
      "stream-format", G_TYPE_STRING, "byte-stream",
      "alignment", G_TYPE_STRING, "au",
      "width", G_TYPE_INT, 384, "height", G_TYPE_INT, 288,
      "framerate", GST_TYPE_FRACTION, 15, 1, NULL);
  ctx->vid_appsrc = appsrc =
      gst_bin_get_by_name_recurse_up (GST_BIN (element), "videosrc");
  ctx->vid_appsink = appsink =
      gst_bin_get_by_name (GST_BIN (ctx->generator_pipe), "vid");
  gst_util_set_object_arg (G_OBJECT (appsrc), "format", "time");
  g_object_set (G_OBJECT (appsrc), "caps", caps, NULL);
  g_object_set (G_OBJECT (appsink), "caps", caps, NULL);
  /* install the callback that will be called when a buffer is needed */
  g_signal_connect (appsrc, "need-data", (GCallback) need_data, ctx);
  gst_caps_unref (caps);

  caps = gst_caps_new_simple ("audio/x-raw", "format", G_TYPE_STRING, "S24BE",
      "layout", G_TYPE_STRING, "interleaved", "rate", G_TYPE_INT, 48000,
      "channels", G_TYPE_INT, 2, NULL);
  ctx->aud_appsrc = appsrc =
      gst_bin_get_by_name_recurse_up (GST_BIN (element), "audiosrc");
  ctx->aud_appsink = appsink =
      gst_bin_get_by_name (GST_BIN (ctx->generator_pipe), "aud");
  gst_util_set_object_arg (G_OBJECT (appsrc), "format", "time");
  g_object_set (G_OBJECT (appsrc), "caps", caps, NULL);
  g_object_set (G_OBJECT (appsink), "caps", caps, NULL);
  g_signal_connect (appsrc, "need-data", (GCallback) need_data, ctx);
  gst_caps_unref (caps);

  gst_element_set_state (ctx->generator_pipe, GST_STATE_PLAYING);
  gst_object_unref (element);
}

int
main (int argc, char *argv[])
{
  GMainLoop *loop;
  GstRTSPServer *server;
  GstRTSPMountPoints *mounts;
  GstRTSPMediaFactory *factory;

  gst_init (&argc, &argv);

  loop = g_main_loop_new (NULL, FALSE);

  /* create a server instance */
  server = gst_rtsp_server_new ();

  /* get the mount points for this server, every server has a default object
   * that be used to map uri mount points to media factories */
  mounts = gst_rtsp_server_get_mount_points (server);

  /* make a media factory for a test stream. The default media factory can use
   * gst-launch syntax to create pipelines.
   * any launch line works as long as it contains elements named pay%d. Each
   * element with pay%d names will be a stream */
  factory = gst_rtsp_media_factory_new ();
  gst_rtsp_media_factory_set_launch (factory,
      "( appsrc name=videosrc ! h264parse ! rtph264pay name=pay0 pt=96 "
      "  appsrc name=audiosrc ! audioconvert ! rtpL24pay name=pay1 pt=97 )");

  /* notify when our media is ready, This is called whenever someone asks for
   * the media and a new pipeline with our appsrc is created */
  g_signal_connect (factory, "media-configure", (GCallback) media_configure,
      NULL);

  /* attach the test factory to the /test url */
  gst_rtsp_mount_points_add_factory (mounts, "/test", factory);

  /* don't need the ref to the mounts anymore */
  g_object_unref (mounts);

  /* attach the server to the default maincontext */
  gst_rtsp_server_attach (server, NULL);

  /* start serving */
  g_print ("stream ready at rtsp://127.0.0.1:8554/test\n");
  g_main_loop_run (loop);

  return 0;
}
