/* GStreamer
 * Copyright (C) 2003 Thomas Vander Stichele <thomas@apestaart.org>
 *               2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *               2005 Andy Wingo <wingo@pobox.com>
 *               2005 Jan Schmidt <thaytan@mad.scientist.com>
 *
 * gst-metadata.c: Use GStreamer to display metadata within files.
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


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <locale.h>
#include <gst/gst.h>

char *filename = NULL;
GstElement *pipeline = NULL;
GstElement *source = NULL;

#define NEW_PIPE_PER_FILE

static void
message_loop (GstElement * element, GstTagList ** tags)
{
  GstBus *bus;
  gboolean done = FALSE;

  bus = gst_element_get_bus (element);
  g_return_if_fail (bus != NULL);
  g_return_if_fail (tags != NULL);

  while (!done) {
    GstMessage *message;
    GstMessageType revent;

    revent = gst_bus_poll (bus, GST_MESSAGE_ANY, -1);
    if (revent == GST_MESSAGE_UNKNOWN) {
      /* Messages ended */
      gst_object_unref (bus);
      return;
    }

    message = gst_bus_pop (bus);
    if (message == NULL)
      continue;

    switch (GST_MESSAGE_TYPE (message)) {
      case GST_MESSAGE_EOS:
      case GST_MESSAGE_ERROR:
        done = TRUE;
        break;
      case GST_MESSAGE_TAG:
      {
        GstTagList *new_tags;

        gst_message_parse_tag (message, &new_tags);
        if (tags) {
          if (*tags)
            *tags =
                gst_tag_list_merge (*tags, new_tags, GST_TAG_MERGE_KEEP_ALL);
          else
            *tags = new_tags;
        } else {
          GST_WARNING ("Failed to extract tags list from message");
        }
        break;
      }
      case GST_MESSAGE_APPLICATION:
      {
        const GstStructure *s = gst_message_get_structure (message);

        /* Application specific message used to mark end point */
        if (strcmp (gst_structure_get_name (s), "gst-metadata-mark") == 0)
          done = TRUE;
        break;
      }
      default:
        break;
    }
    gst_message_unref (message);
  }
  gst_object_unref (bus);
}

void
have_pad_handler (GstElement * decodebin, GstPad * pad, gboolean last,
    GstElement * sink)
{
  GST_DEBUG ("New pad %" GST_PTR_FORMAT " - attempting link", pad);

  gst_pad_link (pad, gst_element_get_pad (sink, "sink"));
}

static void
make_pipeline ()
{
  GstElement *decodebin, *fakesink;

  if (pipeline != NULL)
    gst_object_unref (pipeline);

  pipeline = gst_pipeline_new (NULL);

  g_object_set (G_OBJECT (pipeline), "play-timeout",
      (GstClockTime) 5 * GST_SECOND, NULL);

  source = gst_element_factory_make ("filesrc", "source");
  g_assert (GST_IS_ELEMENT (source));
  decodebin = gst_element_factory_make ("decodebin", "decodebin");
  g_assert (GST_IS_ELEMENT (decodebin));
  fakesink = gst_element_factory_make ("fakesink", "fakesink");
  g_assert (GST_IS_ELEMENT (fakesink));

  gst_bin_add_many (GST_BIN (pipeline), source, decodebin, fakesink, NULL);
  gst_element_link (source, decodebin);

  /* Listen for pads from decodebin */
  g_signal_connect (G_OBJECT (decodebin), "new-decoded-pad",
      G_CALLBACK (have_pad_handler), fakesink);
}

static void
print_tag (const GstTagList * list, const gchar * tag, gpointer unused)
{
  gint i, count;

  count = gst_tag_list_get_tag_size (list, tag);

  for (i = 0; i < count; i++) {
    gchar *str;

    if (gst_tag_get_type (tag) == G_TYPE_STRING) {
      if (!gst_tag_list_get_string_index (list, tag, i, &str))
        g_assert_not_reached ();
    } else {
      str =
          g_strdup_value_contents (gst_tag_list_get_value_index (list, tag, i));
    }

    if (i == 0) {
      g_print ("  %15s: %s\n", gst_tag_get_nick (tag), str);
    } else {
      g_print ("                 : %s\n", str);
    }

    g_free (str);
  }
}

int
main (int argc, char *argv[])
{
  guint i = 1;

  setlocale (LC_ALL, "");

  gst_init (&argc, &argv);

  if (argc < 2) {
    g_print ("Please give filenames to read metadata from\n\n");
    return 1;
  }

  make_pipeline ();
  while (i < argc) {
    GstStateChangeReturn sret;
    GstState state;

    filename = argv[i];
    g_object_set (source, "location", filename, NULL);

    GST_DEBUG ("Starting reading for %s", filename);

    /* Decodebin will only commit to PAUSED if it actually finds a type;
     * otherwise the state change fails */
    sret = gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PAUSED);

    if (GST_STATE_CHANGE_ASYNC == sret) {
      if (GST_STATE_CHANGE_FAILURE ==
          gst_element_get_state (GST_ELEMENT (pipeline), &state, NULL, NULL)) {
        g_print ("State change failed. Aborting");
        break;
      }
    } else if (sret != GST_STATE_CHANGE_SUCCESS) {
      g_print ("%s - Could not read file\n", argv[i]);
    } else {
      GstTagList *tags = NULL;
      GstBus *bus;

      /* Send message on the bus to mark end point of preroll. */
      bus = gst_element_get_bus (GST_ELEMENT (pipeline));
      if (bus) {
        gst_bus_post (bus,
            gst_message_new_application (NULL,
                gst_structure_new ("gst-metadata-mark", NULL)));
        gst_object_unref (bus);
      }

      message_loop (GST_ELEMENT (pipeline), &tags);

      if (tags) {
        g_print ("Metadata for %s:\n", argv[i]);
        gst_tag_list_foreach (tags, print_tag, NULL);
        gst_tag_list_free (tags);
        tags = NULL;
      } else
        g_print ("No metadata found for %s\n", argv[i]);

      sret = gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
      if (GST_STATE_CHANGE_ASYNC == sret) {
        if (GST_STATE_CHANGE_FAILURE ==
            gst_element_get_state (GST_ELEMENT (pipeline), &state, NULL, NULL))
        {
          g_print ("State change failed. Aborting");
          break;
        }
      }
    }

    i++;

#ifdef NEW_PIPE_PER_FILE
    make_pipeline ();
#endif
  }

  if (pipeline)
    gst_object_unref (pipeline);
  return 0;
}
