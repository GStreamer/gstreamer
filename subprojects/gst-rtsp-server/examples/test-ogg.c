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

#define DEFAULT_RTSP_PORT "8554"

static char *port = (char *) DEFAULT_RTSP_PORT;

static GOptionEntry entries[] = {
  {"port", 'p', 0, G_OPTION_ARG_STRING, &port,
      "Port to listen on (default: " DEFAULT_RTSP_PORT ")", "PORT"},
  {NULL}
};

int
main (int argc, char *argv[])
{
  GMainLoop *loop;
  GstRTSPServer *server;
  GstRTSPMountPoints *mounts;
  GstRTSPMediaFactory *factory;
  GOptionContext *optctx;
  GError *error = NULL;
  gchar *str;

  optctx = g_option_context_new ("<filename.ogg> - Test RTSP Server, OGG");
  g_option_context_add_main_entries (optctx, entries, NULL);
  g_option_context_add_group (optctx, gst_init_get_option_group ());
  if (!g_option_context_parse (optctx, &argc, &argv, &error)) {
    g_printerr ("Error parsing options: %s\n", error->message);
    g_option_context_free (optctx);
    g_clear_error (&error);
    return -1;
  }
  g_option_context_free (optctx);

  loop = g_main_loop_new (NULL, FALSE);

  /* create a server instance */
  server = gst_rtsp_server_new ();
  g_object_set (server, "service", port, NULL);

  /* get the mount points for this server, every server has a default object
   * that be used to map uri mount points to media factories */
  mounts = gst_rtsp_server_get_mount_points (server);

  str = g_strdup_printf ("( "
      "filesrc location=%s ! oggdemux name=d "
      "d. ! queue ! rtptheorapay name=pay0 pt=96 "
      "d. ! queue ! rtpvorbispay name=pay1 pt=97 " ")", argv[1]);

  /* make a media factory for a test stream. The default media factory can use
   * gst-launch syntax to create pipelines. 
   * any launch line works as long as it contains elements named pay%d. Each
   * element with pay%d names will be a stream */
  factory = gst_rtsp_media_factory_new ();
  gst_rtsp_media_factory_set_launch (factory, str);
  g_free (str);

  /* attach the test factory to the /test url */
  gst_rtsp_mount_points_add_factory (mounts, "/test", factory);

  /* don't need the ref to the mapper anymore */
  g_object_unref (mounts);

  /* attach the server to the default maincontext */
  gst_rtsp_server_attach (server, NULL);

  /* start serving */
  g_print ("stream ready at rtsp://127.0.0.1:%s/test\n", port);
  g_main_loop_run (loop);

  return 0;
}
