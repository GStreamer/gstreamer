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
#define DEFAULT_MOUNT "/test"
#define DEFAULT_DISABLE_RTCP FALSE

static char *port = (char *) DEFAULT_RTSP_PORT;
static char *mount = (char *) DEFAULT_MOUNT;
static gboolean disable_rtcp = DEFAULT_DISABLE_RTCP;

static GOptionEntry entries[] = {
  {"port", 'p', 0, G_OPTION_ARG_STRING, &port,
      "Port to listen on (default: " DEFAULT_RTSP_PORT ")", "PORT"},
  {"mount", 'm', 0, G_OPTION_ARG_STRING, &mount,
      "Mountpoint (default: " DEFAULT_MOUNT ")", "MOUNT"},
  {"disable-rtcp", '\0', 0, G_OPTION_ARG_NONE, &disable_rtcp,
      "Whether RTCP should be disabled (default false)", NULL},
  {NULL}
};

static gboolean
dump_debug (gpointer user_data)
{
  GstObject *pipe;
  GWeakRef *ref = user_data;
  GstElement *e = g_weak_ref_get (ref);
  if (!e)
    return G_SOURCE_REMOVE;
  pipe = gst_element_get_parent (GST_ELEMENT (e));
  GST_DEBUG_BIN_TO_DOT_FILE (GST_BIN (pipe), GST_DEBUG_GRAPH_SHOW_ALL,
      "rtsp-server-test-launch");
  return G_SOURCE_CONTINUE;
}

static void
constructed (GstRTSPMediaFactory * self G_GNUC_UNUSED, GstRTSPMedia * m,
    gpointer user_data G_GNUC_UNUSED)
{
  GstElement *e = gst_rtsp_media_get_element (m);
  GWeakRef *ref = g_new0 (GWeakRef, 1);
  g_weak_ref_set (ref, e);
  dump_debug (ref);
  g_timeout_add_seconds (5, dump_debug, ref);
}

int
main (int argc, char *argv[])
{
  GMainLoop *loop;
  GstRTSPServer *server;
  GstRTSPMountPoints *mounts;
  GstRTSPMediaFactory *factory;
  GOptionContext *optctx;
  GError *error = NULL;

  optctx = g_option_context_new ("<launch line> - Test RTSP Server, Launch\n\n"
      "Example: \"( videotestsrc ! x264enc ! rtph264pay name=pay0 pt=96 )\"");
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

  /* make a media factory for a test stream. The default media factory can use
   * gst-launch syntax to create pipelines.
   * any launch line works as long as it contains elements named pay%d. Each
   * element with pay%d names will be a stream */
  factory = gst_rtsp_media_factory_new ();
  gst_rtsp_media_factory_set_launch (factory, argv[1]);
  gst_rtsp_media_factory_set_shared (factory, TRUE);
  gst_rtsp_media_factory_set_enable_rtcp (factory, !disable_rtcp);

  g_signal_connect (factory, "media-constructed", G_CALLBACK (constructed),
      NULL);

  /* attach the test factory to the mount url */
  gst_rtsp_mount_points_add_factory (mounts, mount, factory);

  /* don't need the ref to the mapper anymore */
  g_object_unref (mounts);

  /* attach the server to the default maincontext */
  gst_rtsp_server_attach (server, NULL);

  /* start serving */
  g_print ("stream ready at rtsp://127.0.0.1:%s%s\n", port, mount);
  g_main_loop_run (loop);

  return 0;
}
