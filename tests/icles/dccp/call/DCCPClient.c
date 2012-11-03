/* GStreamer
 * Copyright (C) <2007> Leandro Melo de Sales <leandroal@gmail.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <gst/gst.h>

static gboolean
bus_call (GstBus * bus, GstMessage * msg, gpointer data)
{

  GMainLoop *loop = (GMainLoop *) data;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_EOS:
      g_print ("End-of-stream\n");
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_ERROR:{
      gchar *debug;
      GError *err;

      gst_message_parse_error (msg, &err, &debug);
      g_free (debug);

      g_print ("Error: %s\n", err->message);
      g_error_free (err);

      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

void
start_dccpserversink_pipe (GstElement * object, gint socket, gpointer data)
{
  GstElement *dccpserversink = (GstElement *) data;
  g_object_set (G_OBJECT (dccpserversink), "sockfd", socket, NULL);

  g_print ("Setting pipelinesink to PLAYING\n");
  GstElement *pipelinesink =
      (GstElement *) gst_element_get_parent (dccpserversink);
  gst_element_set_state (pipelinesink, GST_STATE_PLAYING);
}


int
main (int argc, char *argv[])
{

  GstElement *pipelinesink, *alsasrc, *dccpserversink;
  GstElement *pipelinesrc, *alsasink, *dccpclientsrc;
  GMainLoop *loop;
  GstBus *bus;
  GstCaps *caps;


  /* initialize GStreamer */
  gst_init (&argc, &argv);
  loop = g_main_loop_new (NULL, FALSE);

  /* check input arguments */
  if (argc != 2) {
    g_print ("%s\n", "see usage: serverHost");
    return -1;
  }

  /* create elements */
  pipelinesink = gst_pipeline_new ("audio-sender");
  alsasrc = gst_element_factory_make ("alsasrc", "alsa-source");
  dccpserversink = gst_element_factory_make ("dccpserversink", "server-sink");
  pipelinesrc = gst_pipeline_new ("audio-receiver");
  alsasink = gst_element_factory_make ("alsasink", "alsa-sink");
  dccpclientsrc = gst_element_factory_make ("dccpclientsrc", "client-source");


  if (!pipelinesink || !alsasrc || !dccpserversink || !pipelinesrc || !alsasink
      || !dccpclientsrc) {
    g_print ("One element could not be created\n");
    return -1;
  }

  caps =
      gst_caps_from_string
      ("audio/x-raw-int, endianness=(int)1234, signed=(boolean)true, width=(int)32, depth=(int)32, rate=(int)44100, channels=(int)2");
  g_object_set (G_OBJECT (dccpclientsrc), "caps", caps, NULL);

  g_object_set (G_OBJECT (dccpclientsrc), "host", argv[1], NULL);
  /*g_object_set (G_OBJECT (dccpclientsrc), "ccid", 3, NULL);
     g_object_set (G_OBJECT (dccpserversink), "ccid", 3, NULL); */

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipelinesink));
  gst_bus_add_watch (bus, bus_call, loop);
  gst_object_unref (bus);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipelinesrc));
  gst_bus_add_watch (bus, bus_call, loop);
  gst_object_unref (bus);


  /* put all elements in a bin */
  gst_bin_add_many (GST_BIN (pipelinesink), alsasrc, dccpserversink, NULL);
  gst_element_link_many (alsasrc, dccpserversink, NULL);

  gst_bin_add_many (GST_BIN (pipelinesrc), dccpclientsrc, alsasink, NULL);
  gst_element_link (dccpclientsrc, alsasink);

  g_signal_connect (dccpclientsrc, "connected",
      (GCallback) start_dccpserversink_pipe, dccpserversink);

  /* Now set to playing and iterate. */
  g_print ("Setting pipelinesrc to PLAYING\n");
  gst_element_set_state (pipelinesrc, GST_STATE_PLAYING);
  g_print ("Running\n");
  g_main_loop_run (loop);

  /* clean up nicely */
  g_print ("Returned, stopping playback\n");
  gst_element_set_state (pipelinesink, GST_STATE_NULL);
  g_print ("Deleting pipelinesink\n");
  gst_object_unref (GST_OBJECT (pipelinesink));

  g_print ("Returned, stopping playback\n");
  gst_element_set_state (pipelinesrc, GST_STATE_NULL);
  g_print ("Deleting pipelinesrc\n");
  gst_object_unref (GST_OBJECT (pipelinesrc));


  return 0;
}
