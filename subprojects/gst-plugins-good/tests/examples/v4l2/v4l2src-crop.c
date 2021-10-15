/* GStreamer
 * Copyright (C) 2021 Damian Hobson-Garcia <dhobsong@igel.co.jp>
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

/* demo application showing v4l2src cropping */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#define PLAY_TIME_SEC   5

struct rect
{
  guint left;
  guint top;
  guint width;
  guint height;
};

static gboolean
bus_call (GstBus * bus, GstMessage * msg, gpointer data)
{
  GMainLoop *loop = (GMainLoop *) data;

  switch (GST_MESSAGE_TYPE (msg)) {

    case GST_MESSAGE_EOS:
      g_print ("End of stream\n");
      g_main_loop_quit (loop);
      break;

    case GST_MESSAGE_ERROR:{
      gchar *debug;
      GError *error;

      gst_message_parse_error (msg, &error, &debug);
      g_free (debug);

      g_printerr ("Error: %s\n", error->message);
      g_error_free (error);

      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

static void
on_crop_bounds (GstElement * element, GParamSpec * pspec, void *data)
{
  GValue bounds = { 0 };
  int i;
  union
  {
    struct rect rect;
    guint vals[4];
  } crop_bounds;

  struct rect crop;

  g_value_init (&bounds, G_PARAM_SPEC_VALUE_TYPE (pspec));
  g_object_get_property (G_OBJECT (element), "crop-bounds", &bounds);

  if (gst_value_array_get_size (&bounds) != 4) {
    g_error ("Invalid crop bounds received");
    return;
  }

  for (i = 0; i < 4; i++) {
    const GValue *val = gst_value_array_get_value (&bounds, i);
    if (!G_VALUE_HOLDS_INT (val)) {
      g_error ("Invalid crop bounds value received");
      return;
    }
    crop_bounds.vals[i] = g_value_get_int (val);
  }

  g_print ("Crop bounds: @(%d, %d), %dx%d\n", crop_bounds.rect.left,
      crop_bounds.rect.top, crop_bounds.rect.width, crop_bounds.rect.height);

  /* Crop out a region from the bottom right quandrant of the
   * crop bounding region */
  crop.left = (crop_bounds.rect.width - crop_bounds.rect.left) / 2;
  crop.top = (crop_bounds.rect.height - crop_bounds.rect.top) / 2;
  crop.width = crop.left / 2;
  crop.height = crop.left / 2;

  g_print ("Setting crop region to @(%d, %d), %dx%d\n", crop.left, crop.top,
      crop.width, crop.height);

  g_object_set (G_OBJECT (element),
      "crop-left", crop.left,
      "crop-top", crop.top,
      "crop-right", crop_bounds.rect.width - crop.left - crop.width,
      "crop-bottom", crop_bounds.rect.height - crop.top - crop.height, NULL);
}

static const gchar *device = "/dev/video0";
static const gchar *videosink = "autovideosink";

static GOptionEntry entries[] = {
  {"device", 'd', 0, G_OPTION_ARG_STRING, &device, "V4L2 Camera Device",
      NULL},
  {"videosink", 's', 0, G_OPTION_ARG_STRING, &videosink, "Video Sink to use",
      NULL},
  {NULL}
};

static gboolean
send_eos (gpointer user_data)
{
  GstElement *pipeline = user_data;

  gst_element_send_event (pipeline, gst_event_new_eos ());
  return FALSE;
}

int
main (int argc, char *argv[])
{
  GMainLoop *loop;

  GstElement *pipeline, *source, *conv, *sink;
  GstBus *bus;
  guint bus_watch_id;
  GError *error = NULL;
  GOptionContext *context;
  gboolean ret;

  context = g_option_context_new ("- test v4l2src crop");
  g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);
  g_option_context_add_group (context, gst_init_get_option_group ());
  ret = g_option_context_parse (context, &argc, &argv, &error);
  g_option_context_free (context);

  if (!ret) {
    g_print ("option parsing failed: %s\n", error->message);
    g_error_free (error);
    return 1;
  }

  gst_init (&argc, &argv);

  loop = g_main_loop_new (NULL, FALSE);

  /* Create gstreamer elements */
  pipeline = gst_pipeline_new ("v4l2src crop sample");
  source = gst_element_factory_make ("v4l2src", "source");
  conv = gst_element_factory_make ("videoconvert", "converter");
  sink = gst_element_factory_make (videosink, "video-output");

  if (!pipeline || !source || !conv || !sink) {
    g_printerr ("One or more elements could not be created. Exiting.\n");
    return -1;
  }

  /* Set up the v4l2src element */
  g_object_set (G_OBJECT (source), "device", device, NULL);

  /* Add a message handler */
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);
  gst_object_unref (bus);

  gst_bin_add_many (GST_BIN (pipeline), source, conv, sink, NULL);
  gst_element_link_many (source, conv, sink, NULL);

  /* Connect a callback to be notified when the crop bounding region is
   * retreived from the V4L2 device */
  g_signal_connect (source, "notify::crop-bounds", (GCallback) on_crop_bounds,
      NULL);

  /* Set the pipeline to "playing" state */
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  /* Stop the playback after specified time */
  g_timeout_add_seconds (PLAY_TIME_SEC, send_eos, pipeline);

  g_main_loop_run (loop);

  /* Out of the main loop, clean up nicely */
  g_print ("Stopping playback\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_print ("Deleting pipeline\n");
  gst_object_unref (GST_OBJECT (pipeline));
  g_source_remove (bus_watch_id);
  g_main_loop_unref (loop);

  return 0;
}
