/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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
#include <gst/video/video.h>
#include <windows.h>
#include <string.h>
#include "../key-handler.h"

static GMainLoop *loop = nullptr;

static gboolean
bus_msg (GstBus * bus, GstMessage * msg, gpointer user_data)
{
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:
    {
      GError *err;
      gchar *dbg;

      gst_message_parse_error (msg, &err, &dbg);
      gst_printerrln ("ERROR %s", err->message);
      if (dbg != nullptr)
        gst_printerrln ("ERROR debug information: %s", dbg);
      g_clear_error (&err);
      g_free (dbg);

      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_EOS:
    {
      gst_println ("Got EOS");
      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

static void
print_keyboard_help (void)
{
  static struct
  {
    const gchar *key_desc;
    const gchar *key_help;
  } key_controls[] = {
    {"left arrow", "Decrease Y angle"},
    {"right arrow", "Increase X angle"},
    {"down arrow", "Decrease Y angle"},
    {"up arrow", "Increase Y angle"},
    {"-", "Decrease Z angle"},
    {"+", "Increase Z angle"},
    {"0 - 3", "Select projection type"},
    {"t", "Toggle rotation space"},
    {"space", "Reset angle"},
    {"q", "Quit"},
  };

  guint i, chars_to_pad, desc_len, max_desc_len = 0;

  gst_print ("\n%s\n", "Keyboard controls:");

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
keyboard_cb (gchar input, gboolean is_ascii, GstElement * dewarp)
{
  static double x_angle = 0;
  static double y_angle = 0;
  static double z_angle = 0;
  static int rotation_space = 0;

  if (!is_ascii) {
    switch (input) {
      case KB_ARROW_UP:
        x_angle += 1.0;
        gst_println ("Increase X angle to %lf", x_angle);
        g_object_set (dewarp, "rotation-x", x_angle, nullptr);
        break;
      case KB_ARROW_DOWN:
        x_angle -= 1.0;
        gst_println ("Decrease X angle to %lf", x_angle);
        g_object_set (dewarp, "rotation-x", x_angle, nullptr);
        break;
      case KB_ARROW_LEFT:
        y_angle -= 1.0;
        gst_println ("Decrease Y angle to %lf", y_angle);
        g_object_set (dewarp, "rotation-y", y_angle, nullptr);
        break;
      case KB_ARROW_RIGHT:
        y_angle += 1.0;
        gst_println ("Increase Y angle to %lf", y_angle);
        g_object_set (dewarp, "rotation-y", y_angle, nullptr);
        break;
      default:
        break;
    }
  } else {
    switch (input) {
      case '-':
        z_angle -= 1.0;
        gst_println ("Decrease Z angle to %lf", z_angle);
        g_object_set (dewarp, "rotation-z", z_angle, nullptr);
        break;
      case '+':
        z_angle += 1.0;
        gst_println ("Increase Z angle to %lf", z_angle);
        g_object_set (dewarp, "rotation-z", z_angle, nullptr);
        break;
      case '0':
        gst_println ("Updated mode: passthrough");
        g_object_set (dewarp, "projection-type", 0, nullptr);
        break;
      case '1':
        gst_println ("Updated mode: equirect");
        g_object_set (dewarp, "projection-type", 1, nullptr);
        break;
      case '2':
        gst_println ("Updated mode: panorama");
        g_object_set (dewarp, "projection-type", 2, nullptr);
        break;
      case '3':
        gst_println ("Updated mode: perspective");
        g_object_set (dewarp, "projection-type", 3, nullptr);
        break;
      case 't':
      case 'T':
        rotation_space++;
        rotation_space %= 2;
        gst_println ("Updated rotation space: %s",
            rotation_space == 0 ? "local" : "world");
        g_object_set (dewarp, "rotation-space", rotation_space, nullptr);
        break;
      case ' ':
        x_angle = 0;
        y_angle = 0;
        z_angle = 0;
        gst_println ("Reset angle");
        g_object_set (dewarp, "rotation-x", x_angle,
          "rotation-y", y_angle, "rotation-z", z_angle, nullptr);
        break;
      case 'q':
        g_main_loop_quit (loop);
        break;
      default:
        break;
    }
  }
}

gint
main (gint argc, gchar ** argv)
{
  gchar *location = nullptr;
  gdouble radius_x = 0.5;
  gdouble radius_y = 0.5;
  GOptionEntry options[] = {
    {"location", 0, 0, G_OPTION_ARG_STRING, &location,
        "Fisheye image file location"},
    {"radius-x", 0, 0, G_OPTION_ARG_DOUBLE, &radius_x,
        "Normalized horizontal radius of fisheye circle"},
    {"radius-y", 0, 0, G_OPTION_ARG_DOUBLE, &radius_y,
        "Normalized horizontal radius of fisheye circle"},
    {nullptr}
  };

  auto option_ctx =
      g_option_context_new ("Fisheye dewarp example using d3d12fisheyedewarp");
  g_option_context_add_main_entries (option_ctx, options, nullptr);
  g_option_context_set_help_enabled (option_ctx, TRUE);
  GError *err = nullptr;
  if (!g_option_context_parse (option_ctx, &argc, &argv, &err)) {
    gst_printerrln ("option parsing failed: %s\n", err->message);
    g_clear_error (&err);
    return 0;
  }
  g_option_context_free (option_ctx);

  if (!location) {
    gst_println ("Location must be specified");
    return 0;
  }

  gst_init (nullptr, nullptr);
  loop = g_main_loop_new (nullptr, FALSE);

  auto pipeline_str = g_strdup_printf ("filesrc location=%s "
    "! decodebin ! d3d12upload ! imagefreeze ! tee name=t ! queue "
    "! d3d12fisheyedewarp name=dewarp ! d3d12videosink t. ! queue ! d3d12videosink",
      location);

  auto pipeline = gst_parse_launch (pipeline_str, nullptr);
  g_free (location);
  g_free (pipeline_str);
  if (!pipeline) {
    gst_println ("Couldn't create pipeline");
    return 0;
  }

  auto remap = gst_bin_get_by_name (GST_BIN (pipeline), "dewarp");

  g_object_set (remap, "radius-x", radius_x, "radius-y", radius_y, nullptr);

  gst_bus_add_watch (GST_ELEMENT_BUS (pipeline), bus_msg, nullptr);

  print_keyboard_help ();
  set_key_handler ((KeyInputCallback) keyboard_cb, remap);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  g_main_loop_run (loop);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_bus_remove_watch (GST_ELEMENT_BUS (pipeline));

  gst_object_unref (remap);
  gst_object_unref (pipeline);

  return 0;
}
