/* GStreamer
 * Copyright (C) 2008 Wim Taymans <wim.taymans at gmail.com>
 * Copyright (C) 2014 Jan Schmidt <jan@centricular.com>
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

#include <stdlib.h>

#include <gst/gst.h>
#include <gst/net/gstnet.h>

#define PLAYBACK_DELAY_MS 40

static void
source_created (GstElement * pipe, GstElement * source)
{
  g_object_set (source, "latency", PLAYBACK_DELAY_MS,
      "ntp-time-source", 3, "buffer-mode", 4, "ntp-sync", TRUE, NULL);
}

static gboolean
message (GstBus * bus, GstMessage * message, gpointer user_data)
{
  GMainLoop *loop = user_data;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:{
      GError *err = NULL;
      gchar *name, *debug = NULL;

      name = gst_object_get_path_string (message->src);
      gst_message_parse_error (message, &err, &debug);

      g_printerr ("ERROR: from element %s: %s\n", name, err->message);
      if (debug != NULL)
        g_printerr ("Additional debug info:\n%s\n", debug);

      g_error_free (err);
      g_free (debug);
      g_free (name);

      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_WARNING:{
      GError *err = NULL;
      gchar *name, *debug = NULL;

      name = gst_object_get_path_string (message->src);
      gst_message_parse_warning (message, &err, &debug);

      g_printerr ("ERROR: from element %s: %s\n", name, err->message);
      if (debug != NULL)
        g_printerr ("Additional debug info:\n%s\n", debug);

      g_error_free (err);
      g_free (debug);
      g_free (name);
      break;
    }
    case GST_MESSAGE_EOS:
      g_print ("Got EOS\n");
      g_main_loop_quit (loop);
      break;
    default:
      break;
  }

  return TRUE;
}

int
main (int argc, char *argv[])
{
  GstClock *net_clock;
  gchar *server;
  gint clock_port;
  GstElement *pipe;
  GMainLoop *loop;

  gst_init (&argc, &argv);

  if (argc < 2) {
    g_print ("usage: %s rtsp://URI clock-IP clock-PORT\n"
        "example: %s rtsp://localhost:8554/test 127.0.0.1 8554\n",
        argv[0], argv[0]);
    return -1;
  }

  server = argv[2];
  clock_port = atoi (argv[3]);

  net_clock = gst_net_client_clock_new ("net_clock", server, clock_port, 0);
  if (net_clock == NULL) {
    g_print ("Failed to create net clock client for %s:%d\n",
        server, clock_port);
    return 1;
  }

  /* Wait for the clock to stabilise */
  gst_clock_wait_for_sync (net_clock, GST_CLOCK_TIME_NONE);

  loop = g_main_loop_new (NULL, FALSE);

  pipe = gst_element_factory_make ("playbin", NULL);
  g_object_set (pipe, "uri", argv[1], NULL);
  g_signal_connect (pipe, "source-setup", G_CALLBACK (source_created), NULL);

  gst_pipeline_use_clock (GST_PIPELINE (pipe), net_clock);

  /* Set this high enough so that it's higher than the minimum latency
   * on all receivers */
  gst_pipeline_set_latency (GST_PIPELINE (pipe), 500 * GST_MSECOND);

  if (gst_element_set_state (pipe,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    g_print ("Failed to set state to PLAYING\n");
    goto exit;
  };

  gst_bus_add_signal_watch (GST_ELEMENT_BUS (pipe));
  g_signal_connect (GST_ELEMENT_BUS (pipe), "message", G_CALLBACK (message),
      loop);

  g_main_loop_run (loop);

exit:
  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (pipe);
  g_main_loop_unref (loop);

  return 0;
}
