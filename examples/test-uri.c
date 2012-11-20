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

static gboolean
remove_map (GstRTSPServer * server)
{
  GstRTSPMediaMapping *mapping;

  g_print ("removing /test mapping\n");
  mapping = gst_rtsp_server_get_media_mapping (server);
  gst_rtsp_media_mapping_remove_factory (mapping, "/test");
  g_object_unref (mapping);

  return FALSE;
}

int
main (int argc, char *argv[])
{
  GMainLoop *loop;
  GstRTSPServer *server;
  GstRTSPMediaMapping *mapping;
  GstRTSPMediaFactoryURI *factory;

  gst_init (&argc, &argv);

  if (argc < 2) {
    g_message ("usage: %s <uri>", argv[0]);
    return -1;
  }

  loop = g_main_loop_new (NULL, FALSE);

  /* create a server instance */
  server = gst_rtsp_server_new ();

  /* get the mapping for this server, every server has a default mapper object
   * that be used to map uri mount points to media factories */
  mapping = gst_rtsp_server_get_media_mapping (server);

  /* make a URI media factory for a test stream. */
  factory = gst_rtsp_media_factory_uri_new ();
  /* when using GStreamer as a client, one can use the gst payloader, which is
   * more efficient when there is no payloader for the compressed format */
  /* g_object_set (factory, "use-gstpay", TRUE, NULL); */
  gst_rtsp_media_factory_uri_set_uri (factory, argv[1]);
  /* if you want multiple clients to see the same video, set the shared property
   * to TRUE */
  /* gst_rtsp_media_factory_set_shared ( GST_RTSP_MEDIA_FACTORY (factory), TRUE); */

  /* attach the test factory to the /test url */
  gst_rtsp_media_mapping_add_factory (mapping, "/test",
      GST_RTSP_MEDIA_FACTORY (factory));

  /* don't need the ref to the mapper anymore */
  g_object_unref (mapping);

  /* attach the server to the default maincontext */
  if (gst_rtsp_server_attach (server, NULL) == 0)
    goto failed;

  /* do session cleanup every 2 seconds */
  g_timeout_add_seconds (2, (GSourceFunc) timeout, server);
  /* remove the mapping after 10 seconds, new clients won't be able to use the
   * /test url anymore */
  g_timeout_add_seconds (10, (GSourceFunc) remove_map, server);

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
