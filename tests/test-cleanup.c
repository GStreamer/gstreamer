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
timeout (GMainLoop * loop)
{
  g_main_loop_quit (loop);
  return FALSE;
}


int
main (int argc, char *argv[])
{
  GMainLoop *loop;
  GstRTSPServer *server;
  guint id;

  gst_init (&argc, &argv);

  loop = g_main_loop_new (NULL, FALSE);

  /* create a server instance */
  server = gst_rtsp_server_new ();

  /* We just want to bind any port here, so that tests can run in parallel */
  gst_rtsp_server_set_service (server, "0");

  /* attach the server to the default maincontext */
  if ((id = gst_rtsp_server_attach (server, NULL)) == 0)
    goto failed;

  g_timeout_add_seconds (2, (GSourceFunc) timeout, loop);

  /* start serving */
  g_main_loop_run (loop);

  /* cleanup */
  g_source_remove (id);
  g_object_unref (server);
  g_main_loop_unref (loop);

  return 0;

  /* ERRORS */
failed:
  {
    g_print ("failed to attach the server\n");
    return -1;
  }
}
