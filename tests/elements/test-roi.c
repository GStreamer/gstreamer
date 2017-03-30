/*
 *  test-roi.c - Testsuite for Region of Interest
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
#include <string.h>
#include <gst/gst.h>

typedef struct _CustomData
{
  GstElement *pipeline;
  GstPad *src_pad;
  GMainLoop *loop;
} AppData;

static void
send_roi_event (AppData * data)
{
  gboolean res = FALSE;
  GstEvent *event;
  gint value[2] = { 4, 0 };
  static gint counter = 0;

  event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM_OOB,
      gst_structure_new ("GstVaapiEncoderRegionOfInterest",
          "roi-x", G_TYPE_UINT, 0,
          "roi-y", G_TYPE_UINT, 0,
          "roi-width", G_TYPE_UINT, 320,
          "roi-height", G_TYPE_UINT, 240,
          "roi-value", G_TYPE_INT, value[counter++ % 2], NULL));

  /* Send the event */
  res = gst_pad_push_event (data->src_pad, event);
  g_print ("Sending event %p done: %d\n", event, res);
}

static void
send_eos_event (AppData * data)
{
  GstBus *bus;
  GstMessage *msg;

  bus = gst_element_get_bus (data->pipeline);
  gst_element_send_event (data->pipeline, gst_event_new_eos ());
  msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_EOS);

  gst_message_unref (msg);
  gst_object_unref (bus);
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
    case 'r':
      send_roi_event (data);
      break;
    case 'q':
      send_eos_event (data);
      g_main_loop_quit (data->loop);
      break;
    default:
      break;
  }

  g_free (str);

  return TRUE;
}

/*
 * This is an example pipeline to recognize difference between ROI and non-ROI.
 * 1. Produce snow pattern with 320p
 * 2. Encode and decode the raw data with 2 pipelines at same time.
 *    2.1. Inject a GstCustomEvent to the 2nd pipeline to enable ROI.
 * 3. Mix both streams in videomixer.
 * 5. Output the result in one window.
 *
 * Note that the higher definition of original raw data, the easier we
 * recognize.  So you can replace videotestsrc with your
 * high-definition camera or other src elements.
 */

/*
.----------.  .---.     .--------.  .---.  .---.  .---.  .--------.  .----------.  .-----.
| videosrc |->|tee|->Q->|txtovrly|->|enc|->|dec|->|vpp|->|videobox|->|videomixer|->|vsink|
'----------'  '---'     '--------'  '---'  '---'  '---'  '--------'  '----------'  '-----'
                ^                                                    ^
                |                                                    |
                |       .--------.  .---.  .---.  .---.  .--------.  |
                '--->Q->|txtovrly|->|enc|->|dec|->|vpp|->|videobox|->'
                     ^  '--------'  '---'  '---'  '---'  '--------'
                     |
                     '-- Injection of GstCustomEvent "GstVaapiEncoderRegionOfInterest"
*/

int
main (int argc, char *argv[])
{
  AppData data;
  GstStateChangeReturn ret;
  GstElement *q;
  GIOChannel *io_stdin;
  GError *err = NULL;

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  /* Print usage map */
  g_print ("USAGE: Choose one of the following options, then press enter:\n"
      " 'r' to send ROI event \n" " 'q' to quit\n");

#define ENCDEC "vaapih264enc rate-control=cbr bitrate=2000 ! vaapih264dec ! vaapipostproc width=640 "
#define TEXT "textoverlay font-desc=\"Arial Bold 48\" text="

  data.pipeline =
      gst_parse_launch
      ("videomixer name=mix ! vaapipostproc ! vaapisink sync=false "
      "videotestsrc pattern=snow ! video/x-raw, width=320, framerate=5/1 "
      "! tee name=t "
      "t. ! queue ! " TEXT "\"non-ROI\" ! " ENCDEC
      "! videobox left=-640 ! mix. "
      "t. ! queue name=roi ! " TEXT "\"ROI\" ! " ENCDEC
      "! videobox ! mix.", &err);

  if (err) {
    g_printerr ("failed to parse pipeline: %s\n", err->message);
    g_error_free (err);
    return -1;
  }

  q = gst_bin_get_by_name (GST_BIN (data.pipeline), "roi");
  data.src_pad = gst_element_get_static_pad (q, "src");
  gst_object_unref (q);

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

  /* Create a GLib Main Loop and set it to run */
  data.loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (data.loop);

  /* Free resources */
  g_main_loop_unref (data.loop);
  g_io_channel_unref (io_stdin);
  gst_element_set_state (data.pipeline, GST_STATE_NULL);

  if (data.src_pad)
    gst_object_unref (data.src_pad);
  gst_object_unref (data.pipeline);

  return 0;
}
