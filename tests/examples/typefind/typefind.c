/* GStreamer typefind element example
 * Copyright (C) <2005> Stefan Kost
 * Copyright (C) <2006> Tim-Philipp Müller
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <gst/gst.h>

#include <stdlib.h>

static void
type_found (GstElement * typefind, guint probability, const GstCaps * caps,
    gpointer user_data)
{
  gchar *xml, *caps_str;

  caps_str = gst_caps_to_string (caps);
  xml = g_markup_printf_escaped ("<?xml version=\"1.0\"?>\n<Capabilities>\n"
      " <Caps1>%s</Caps1>\n</Capabilities>", caps_str);
  g_free (caps_str);

  g_print ("%s\n", xml);
  g_free (xml);
}

static void
event_loop (GstElement * pipe)
{
  GstBus *bus;
  GstMessage *message = NULL;
  gboolean running = TRUE;

  bus = gst_element_get_bus (GST_ELEMENT (pipe));

  while (running) {
    message = gst_bus_poll (bus, GST_MESSAGE_ANY, -1);

    g_assert (message != NULL);

    switch (message->type) {
      case GST_MESSAGE_EOS:
        running = FALSE;
        break;
      case GST_MESSAGE_WARNING:{
        GError *gerror;
        gchar *debug;

        gst_message_parse_warning (message, &gerror, &debug);
        gst_object_default_error (GST_MESSAGE_SRC (message), gerror, debug);
        g_error_free (gerror);
        g_free (debug);
        break;
      }
      case GST_MESSAGE_ERROR:{
        GError *gerror;
        gchar *debug;

        gst_message_parse_error (message, &gerror, &debug);
        gst_object_default_error (GST_MESSAGE_SRC (message), gerror, debug);
        g_error_free (gerror);
        g_free (debug);
        running = FALSE;
        break;
      }
      default:
        break;
    }
    gst_message_unref (message);
  }
  gst_object_unref (bus);
}

int
main (int argc, char *argv[])
{
  GstElement *pipeline, *filesrc, *typefind, *sink;

  gst_init (&argc, &argv);

  if (argc != 2) {
    g_print ("usage: %s <filename>\n", argv[0]);
    exit (-1);
  }

  /* create a new pipeline to hold the elements */
  pipeline = gst_pipeline_new ("pipeline");
  g_assert (pipeline != NULL);

  /* create a file reader */
  filesrc = gst_element_factory_make ("filesrc", "file_source");
  g_assert (filesrc != NULL);
  g_object_set (G_OBJECT (filesrc), "location", argv[1], NULL);

  typefind = gst_element_factory_make ("typefind", "typefind");
  g_assert (typefind != NULL);

  sink = gst_element_factory_make ("fakesink", "sink");
  g_assert (sink != NULL);

  /* add objects to the main pipeline */
  gst_bin_add (GST_BIN (pipeline), filesrc);
  gst_bin_add (GST_BIN (pipeline), typefind);
  gst_bin_add (GST_BIN (pipeline), sink);

  g_signal_connect (G_OBJECT (typefind), "have-type",
      G_CALLBACK (type_found), NULL);

  gst_element_link_many (filesrc, typefind, sink, NULL);

  /* start playing */
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  /* Run event loop listening for bus messages until EOS or ERROR */
  event_loop (pipeline);

  /* stop the bin */
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);

  exit (0);
}
