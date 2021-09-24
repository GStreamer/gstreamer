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

#include <gst/rtsp-server/rtsp-server.h>


static gboolean
timeout (GstRTSPServer * server)
{
  GstRTSPSessionPool *pool;

  pool = gst_rtsp_server_get_session_pool (server);
  gst_rtsp_session_pool_cleanup (pool);
  g_object_unref (pool);

  return TRUE;
}

static void
media_constructed (GstRTSPMediaFactory * factory, GstRTSPMedia * media)
{
  guint i, n_streams;

  n_streams = gst_rtsp_media_n_streams (media);

  for (i = 0; i < n_streams; i++) {
    GstRTSPAddressPool *pool;
    GstRTSPStream *stream;
    gchar *min, *max;

    stream = gst_rtsp_media_get_stream (media, i);

    /* make a new address pool */
    pool = gst_rtsp_address_pool_new ();

    min = g_strdup_printf ("224.3.0.%d", (2 * i) + 1);
    max = g_strdup_printf ("224.3.0.%d", (2 * i) + 2);
    gst_rtsp_address_pool_add_range (pool, min, max,
        5000 + (10 * i), 5010 + (10 * i), 1);
    g_free (min);
    g_free (max);

    gst_rtsp_stream_set_address_pool (stream, pool);
    g_object_unref (pool);
  }
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
  gst_rtsp_media_factory_set_launch (factory, "( "
      "videotestsrc ! video/x-raw,width=352,height=288,framerate=15/1 ! "
      "x264enc ! rtph264pay name=pay0 pt=96 "
      "audiotestsrc ! audio/x-raw,rate=8000 ! "
      "alawenc ! rtppcmapay name=pay1 pt=97 " ")");

  gst_rtsp_media_factory_set_shared (factory, TRUE);

  g_signal_connect (factory, "media-constructed", (GCallback)
      media_constructed, NULL);

  /* attach the test factory to the /test url */
  gst_rtsp_mount_points_add_factory (mounts, "/test", factory);

  /* don't need the ref to the mapper anymore */
  g_object_unref (mounts);

  /* attach the server to the default maincontext */
  if (gst_rtsp_server_attach (server, NULL) == 0)
    goto failed;

  g_timeout_add_seconds (2, (GSourceFunc) timeout, server);

  /* start serving */
  g_print ("stream ready at rtsp://127.0.0.1:8554/test\n");
  g_main_loop_run (loop);

  return 0;

  /* ERRORS */
failed:
  {
    g_print ("failed to attach the server\n");
    return -1;
  }
}
