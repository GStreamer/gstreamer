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
source_created (GstElement * pipe, GParamSpec * pspec)
{
  GstElement *source;

  g_object_get (pipe, "source", &source, NULL);
  g_assert (source != NULL);

  g_object_set (source, "latency", PLAYBACK_DELAY_MS,
      "use-pipeline-clock", TRUE, "buffer-mode", 1, NULL);

  gst_object_unref (source);
}

int
main (int argc, char *argv[])
{
  GstClock *net_clock;
  gchar *server;
  gint clock_port;
  GstElement *pipe;
  GstMessage *msg;

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

  /* Wait 0.5 seconds for the clock to stabilise */
  g_usleep (G_USEC_PER_SEC / 2);

  pipe = gst_element_factory_make ("playbin", NULL);
  g_object_set (pipe, "uri", argv[1], NULL);
  g_signal_connect (pipe, "notify::source", (GCallback) source_created, NULL);

  gst_element_set_start_time (pipe, GST_CLOCK_TIME_NONE);
  gst_element_set_base_time (pipe, 0);
  gst_pipeline_use_clock (GST_PIPELINE (pipe), net_clock);

  if (gst_element_set_state (pipe,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    g_print ("Failed to set state to PLAYING\n");
    goto exit;
  };

  msg = gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (pipe),
      GST_CLOCK_TIME_NONE, GST_MESSAGE_EOS | GST_MESSAGE_ERROR);

  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR) {
    GError *err = NULL;
    gchar *debug = NULL;
    gst_message_parse_error (msg, &err, &debug);
    g_print ("\nERROR: %s\n%s\n\n", err->message, debug);
    g_error_free (err);
    g_free (debug);
  }
  gst_message_unref (msg);

exit:
  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (pipe);

  return 0;
}
