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

#define TIMEOUT 2

static gboolean timeout_1 (GMainLoop * loop);

static guint id;
static gint rounds = 3;
static GstRTSPServer *server;

static gboolean
timeout_2 (GMainLoop * loop)
{
  rounds--;
  if (rounds > 0) {
    id = gst_rtsp_server_attach (server, NULL);
    g_print ("have attached\n");
    g_timeout_add_seconds (TIMEOUT, (GSourceFunc) timeout_1, loop);
  } else {
    g_main_loop_quit (loop);
  }
  return FALSE;
}

static gboolean
timeout_1 (GMainLoop * loop)
{
  g_source_remove (id);
  g_print ("have removed\n");
  g_timeout_add_seconds (TIMEOUT, (GSourceFunc) timeout_2, loop);
  return FALSE;
}

int
main (int argc, char *argv[])
{
  GMainLoop *loop;

  gst_init (&argc, &argv);

  loop = g_main_loop_new (NULL, FALSE);

  /* create a server instance */
  server = gst_rtsp_server_new ();

  /* attach the server to the default maincontext */
  if ((id = gst_rtsp_server_attach (server, NULL)) == 0)
    goto failed;
  g_print ("have attached\n");

  g_timeout_add_seconds (TIMEOUT, (GSourceFunc) timeout_1, loop);

  /* start serving */
  g_main_loop_run (loop);

  /* cleanup */
  g_object_unref (server);
  g_main_loop_unref (loop);

  g_print ("quit\n");
  return 0;

  /* ERRORS */
failed:
  {
    g_print ("failed to attach the server\n");
    return -1;
  }
}
