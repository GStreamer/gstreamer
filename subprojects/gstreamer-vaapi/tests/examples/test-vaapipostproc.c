/*
 *  test-vaapipostproc.c - Testsuite for VAAPI Postprocessor
 *
 *  Copyright (C) 2017 Intel Corporation
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#include <stdio.h>
#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

typedef struct _CustomData
{
  GstElement *pipeline;
  GstElement *postproc;
  GMainLoop *loop;
} AppData;

static gboolean
_check_passthrough_mode (gpointer user_data)
{
  gboolean ret;
  AppData *data = (AppData *) user_data;

  ret = gst_base_transform_is_passthrough (GST_BASE_TRANSFORM (data->postproc));

  if (ret)
    gst_println ("Now this pipeline is on passthrough mode");
  else
    gst_println ("Now this pipeline is NOT on passthrough mode");

  return FALSE;
}

static void
set_contrast (AppData * data)
{
  static gfloat value = 1.0;

  value = value == 1.0 ? 0.5 : 1.0;
  g_object_set (data->postproc, "contrast", value, NULL);
  gst_println ("contrast value is changed to %f", value);

  g_timeout_add (300, _check_passthrough_mode, data);
}

static void
change_size (AppData * data)
{
  static gint i = 0;
  if (i == 0) {
    g_object_set (data->postproc, "width", 1280, "height", 720, NULL);
    gst_println ("frame size is changed to 1280x720");
    i++;
  } else {
    g_object_set (data->postproc, "width", 0, "height", 0, NULL);
    gst_println ("frame size is changed to default");
    i = 0;
  }

  g_timeout_add (300, _check_passthrough_mode, data);
}

/* Process keyboard input */
static gboolean
handle_keyboard (GIOChannel * source, GIOCondition cond, AppData * data)
{
  gchar *str = NULL;

  if (g_io_channel_read_line (source, &str, NULL, NULL,
          NULL) != G_IO_STATUS_NORMAL) {
    return TRUE;
  }

  switch (g_ascii_tolower (str[0])) {
    case 's':{
      set_contrast (data);
      break;
    }
    case 'c':{
      change_size (data);
      break;
    }
    case 'q':
      g_main_loop_quit (data->loop);
      break;
    default:
      break;
  }

  g_free (str);

  return TRUE;
}

int
main (int argc, char *argv[])
{
  AppData data = { 0, };
  GstStateChangeReturn ret;
  GIOChannel *io_stdin;

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  /* Print usage map */
  gst_println ("USAGE: Choose one of the following options, then press enter:\n"
      " 's' to set contrast\n" " 'c' to change size\n" " 'q' to quit\n");

  data.pipeline =
      gst_parse_launch
      ("videotestsrc name=src ! vaapih264enc ! vaapih264dec ! vaapipostproc name=postproc ! vaapisink",
      NULL);
  data.postproc = gst_bin_get_by_name (GST_BIN (data.pipeline), "postproc");

  /* Add a keyboard watch so we get notified of keystrokes */
  io_stdin = g_io_channel_unix_new (fileno (stdin));
  g_io_add_watch (io_stdin, G_IO_IN, (GIOFunc) handle_keyboard, &data);

  /* Start playing */
  ret = gst_element_set_state (data.pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    gst_object_unref (data.pipeline);
    return -1;
  }

  g_timeout_add (300, _check_passthrough_mode, &data);

  /* Create a GLib Main Loop and set it to run */
  data.loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (data.loop);

  /* Free resources */
  g_main_loop_unref (data.loop);
  g_io_channel_unref (io_stdin);
  gst_element_set_state (data.pipeline, GST_STATE_NULL);

  gst_object_unref (data.postproc);
  gst_object_unref (data.pipeline);

  return 0;
}
