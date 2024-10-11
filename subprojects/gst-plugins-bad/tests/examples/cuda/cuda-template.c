/*
 * GStreamer
 * Copyright (C) 2024 Seungha Yang <seungha@centricular.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "../key-handler.h"

GST_PLUGIN_STATIC_DECLARE (cuda_template);

static GMainLoop *loop = NULL;
static GstElement *filter = NULL;
static gboolean update_image = TRUE;

static void
print_keyboard_help (void)
{
  /* *INDENT-OFF* */
  static struct
  {
    const gchar *key_desc;
    const gchar *key_help;
  } key_controls[] = {
    {
    "q", "Quit"}, {
    "t", "toggle processing mode (read-only or image-update)"}
  };
  /* *INDENT-ON* */

  guint i, chars_to_pad, desc_len, max_desc_len = 0;

  gst_print ("\n\n%s\n\n", "Keyboard controls:");

  for (i = 0; i < G_N_ELEMENTS (key_controls); ++i) {
    desc_len = g_utf8_strlen (key_controls[i].key_desc, -1);
    max_desc_len = MAX (max_desc_len, desc_len);
  }
  ++max_desc_len;

  for (i = 0; i < G_N_ELEMENTS (key_controls); ++i) {
    chars_to_pad = max_desc_len - g_utf8_strlen (key_controls[i].key_desc, -1);
    gst_print ("\t%s", key_controls[i].key_desc);
    gst_print ("%-*s: ", chars_to_pad, "");
    gst_print ("%s\n", key_controls[i].key_help);
  }
  gst_print ("\n");
}

static void
keyboard_cb (gchar input, gboolean is_ascii, gpointer user_data)
{
  if (!is_ascii)
    return;

  switch (input) {
    case 'q':
    case 'Q':
      g_main_loop_quit (loop);
      break;
    case 't':
    case 'T':
      update_image = !update_image;
      gst_println ("Toggle image update mode: %d", update_image);
      g_object_set (filter, "update-image", update_image, NULL);
      break;
    default:
      break;
  }
}

static gboolean
bus_handler (GstBus * bus, GstMessage * msg, GMainLoop * loop)
{
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_EOS:
      gst_println ("Got EOS");
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_ERROR:
    {
      GError *err = NULL;
      gchar *name, *debug = NULL;

      name = gst_object_get_path_string (msg->src);
      gst_message_parse_error (msg, &err, &debug);

      gst_printerrln ("ERROR: from element %s: %s", name, err->message);
      if (debug != NULL)
        gst_printerrln ("Additional debug info:\n%s", debug);

      g_clear_error (&err);
      g_free (debug);
      g_free (name);

      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }

  return G_SOURCE_CONTINUE;
}

gint
main (gint argc, gchar ** argv)
{
  GstElement *pipeline;
  GstBus *bus;

  gst_init (NULL, NULL);

  GST_PLUGIN_STATIC_REGISTER (cuda_template);

  loop = g_main_loop_new (NULL, FALSE);
  pipeline = gst_parse_launch ("videotestsrc ! cudaupload ! "
      "cuda-transform-ip name=filter ! cudadownload ! videoconvert ! "
      "queue max-size-buffers=3 max-size-time=0 max-size-bytes=0 ! "
      "autovideosink", NULL);

  if (!pipeline) {
    gst_printerrln ("Couldn't create pipeline");
    return 0;
  }

  filter = gst_bin_get_by_name (GST_BIN (pipeline), "filter");

  bus = gst_element_get_bus (pipeline);
  gst_bus_add_watch (bus, (GstBusFunc) bus_handler, loop);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  print_keyboard_help ();

  set_key_handler ((KeyInputCallback) keyboard_cb, NULL);
  g_main_loop_run (loop);
  unset_key_handler ();

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_bus_remove_watch (bus);
  gst_object_unref (bus);
  gst_object_unref (pipeline);

  gst_deinit ();

  return 0;
}
