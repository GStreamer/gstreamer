/*
 * Copyright (c) 2015, Igalia S.L
 *     Author: Philippe Normand <philn@igalia.com>
 * Licence: LGPL. (See COPYING.LGPL)
 */

#include <glib.h>
#include <gst/gst.h>
#include <gst/video/videoorientation.h>

#define PIPELINE "rpicamsrc name=src preview=0 fullscreen=0 ! h264parse ! omxh264dec ! glimagesink sync=0"

static void
configure_orientation (GstVideoOrientation * orientation)
{
  gboolean flip;

  if (gst_video_orientation_get_hflip (orientation, &flip)) {
    g_print ("current hflip: %s\n", flip ? "enabled" : "disabled");

    if (g_getenv ("HFLIP"))
      gst_video_orientation_set_hflip (orientation, TRUE);

    gst_video_orientation_get_hflip (orientation, &flip);
    g_print ("new hflip: %s\n", flip ? "enabled" : "disabled");
  }

  if (gst_video_orientation_get_vflip (orientation, &flip)) {
    g_print ("current vflip: %s\n", flip ? "enabled" : "disabled");

    if (g_getenv ("VFLIP"))
      gst_video_orientation_set_vflip (orientation, TRUE);

    gst_video_orientation_get_vflip (orientation, &flip);
    g_print ("new vflip: %s\n", flip ? "enabled" : "disabled");
  }
}

int
main (int argc, char **argv)
{
  GMainLoop *loop;
  GstElement *pipeline;
  GError *error = NULL;
  GstElement *src;
  GstVideoOrientation *orientation;

  gst_init (&argc, &argv);
  loop = g_main_loop_new (NULL, FALSE);

  pipeline = gst_parse_launch (PIPELINE, &error);
  if (error != NULL) {
    g_printerr ("Error parsing '%s': %s", PIPELINE, error->message);
    g_error_free (error);
    return -1;
  }

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  src = gst_bin_get_by_name (GST_BIN (pipeline), "src");
  if (!src) {
    g_printerr ("Source element not found\n");
    return -2;
  }

  orientation = GST_VIDEO_ORIENTATION (src);
  configure_orientation (orientation);
  g_main_loop_run (loop);

  gst_object_unref (src);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  return 0;
}
