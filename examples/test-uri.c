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
#include <gst/rtsp-server/rtsp-media-factory-uri.h>

#define DEFAULT_RTSP_PORT "8554"

static char *port = (char *) DEFAULT_RTSP_PORT;

static GOptionEntry entries[] = {
  {"port", 'p', 0, G_OPTION_ARG_STRING, &port,
      "Port to listen on (default: " DEFAULT_RTSP_PORT ")", "PORT"},
  {NULL}
};


static gboolean
timeout (GstRTSPServer * server)
{
  GstRTSPSessionPool *pool;

  pool = gst_rtsp_server_get_session_pool (server);
  gst_rtsp_session_pool_cleanup (pool);
  g_object_unref (pool);

  return TRUE;
}

#if 0
static gboolean
remove_map (GstRTSPServer * server)
{
  GstRTSPMountPoints *mounts;

  g_print ("removing /test mount point\n");
  mounts = gst_rtsp_server_get_mount_points (server);
  gst_rtsp_mount_points_remove_factory (mounts, "/test");
  g_object_unref (mounts);

  return FALSE;
}
#endif

int
main (int argc, gchar * argv[])
{
  GMainLoop *loop;
  GstRTSPServer *server;
  GstRTSPMountPoints *mounts;
  GstRTSPMediaFactoryURI *factory;
  GOptionContext *optctx;
  GError *error = NULL;
  gchar *uri;

  optctx = g_option_context_new ("<uri> - Test RTSP Server, URI");
  g_option_context_add_main_entries (optctx, entries, NULL);
  g_option_context_add_group (optctx, gst_init_get_option_group ());
  if (!g_option_context_parse (optctx, &argc, &argv, &error)) {
    g_printerr ("Error parsing options: %s\n", error->message);
    g_option_context_free (optctx);
    g_clear_error (&error);
    return -1;
  }
  g_option_context_free (optctx);

  if (argc < 2) {
    g_printerr ("Please pass an URI or file as argument!\n");
    return -1;
  }

  loop = g_main_loop_new (NULL, FALSE);

  /* create a server instance */
  server = gst_rtsp_server_new ();
  g_object_set (server, "service", port, NULL);

  /* get the mount points for this server, every server has a default object
   * that be used to map uri mount points to media factories */
  mounts = gst_rtsp_server_get_mount_points (server);

  /* make a URI media factory for a test stream. */
  factory = gst_rtsp_media_factory_uri_new ();

  /* when using GStreamer as a client, one can use the gst payloader, which is
   * more efficient when there is no payloader for the compressed format */
  /* g_object_set (factory, "use-gstpay", TRUE, NULL); */

  /* check if URI is valid, otherwise convert filename to URI if it's a file */
  if (gst_uri_is_valid (argv[1])) {
    uri = g_strdup (argv[1]);
  } else if (g_file_test (argv[1], G_FILE_TEST_EXISTS)) {
    uri = gst_filename_to_uri (argv[1], NULL);
  } else {
    g_printerr ("Unrecognised command line argument '%s'.\n"
        "Please pass an URI or file as argument!\n", argv[1]);
    return -1;
  }

  gst_rtsp_media_factory_uri_set_uri (factory, uri);
  g_free (uri);

  /* if you want multiple clients to see the same video, set the shared property
   * to TRUE */
  /* gst_rtsp_media_factory_set_shared ( GST_RTSP_MEDIA_FACTORY (factory), TRUE); */

  /* attach the test factory to the /test url */
  gst_rtsp_mount_points_add_factory (mounts, "/test",
      GST_RTSP_MEDIA_FACTORY (factory));

  /* don't need the ref to the mapper anymore */
  g_object_unref (mounts);

  /* attach the server to the default maincontext */
  if (gst_rtsp_server_attach (server, NULL) == 0)
    goto failed;

  /* do session cleanup every 2 seconds */
  g_timeout_add_seconds (2, (GSourceFunc) timeout, server);

#if 0
  /* remove the mount point after 10 seconds, new clients won't be able to use
   * the /test url anymore */
  g_timeout_add_seconds (10, (GSourceFunc) remove_map, server);
#endif

  /* start serving */
  g_print ("stream ready at rtsp://127.0.0.1:%s/test\n", port);
  g_main_loop_run (loop);

  return 0;

  /* ERRORS */
failed:
  {
    g_print ("failed to attach the server\n");
    return -1;
  }
}
