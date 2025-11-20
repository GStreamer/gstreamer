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

/*
 * Test application demonstrating the "resync" signal of GstClockSync.
 *
 * This example builds a simple pipeline using:
 * videotestsrc ! timeoverlay ! clocksync ! autovideosink
 *
 * It simulates a temporary upstream stall by inserting blocking pad probe.
 * After some delay, the probe is removed and the "resync" signal is emitted to
 * request recalculation of the ts-offset on the next incoming buffer.
 *
 * This demonstrates how "resync" signal can be used to recover a correct
 * ts-offset when buffer running-time progression becomes non-linear
 * while keeping the element state unchanged.
 */

#include <gst/gst.h>

static GMainLoop *loop = NULL;

typedef struct
{
  GstElement *clocksync;
  GstPad *sinkpad;
  gulong block_id;
} TestData;

static gboolean
bus_call (GstBus * bus, GstMessage * msg, gpointer data)
{
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_EOS:{
      g_print ("End-of-stream\n");
      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_ERROR:{
      gchar *debug;
      GError *err;

      gst_message_parse_error (msg, &err, &debug);
      gst_printerrln ("Debugging info: %s", (debug) ? debug : "none");
      g_free (debug);

      g_print ("Error: %s\n", err->message);
      g_clear_error (&err);

      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }
  return TRUE;
}

static gboolean
shutdown_cb (gpointer user_data)
{
  g_main_loop_quit (loop);

  return G_SOURCE_REMOVE;
}

static gboolean
after_blocking_timeout_cb (TestData * test_data)
{
  gst_println ("Unblock and schedule resync");

  /* Clocksync will calculate new ts-offset on the next buffer */
  g_signal_emit_by_name (test_data->clocksync, "resync");

  /* Unblock pad now */
  gst_pad_remove_probe (test_data->sinkpad, test_data->block_id);

  /* Shutdown pipeline after 10 seconds */
  g_timeout_add_seconds (10, shutdown_cb, NULL);

  return G_SOURCE_REMOVE;
}

static GstPadProbeReturn
clocksync_pad_blocked_cb (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  return GST_PAD_PROBE_OK;
}

static gboolean
timeout_cb (TestData * test_data)
{
  test_data->block_id = gst_pad_add_probe (test_data->sinkpad,
      GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM,
      clocksync_pad_blocked_cb, NULL, NULL);

  gst_println
      ("Blocking pad probe added, waiting for 5 seconds for unblocking");
  g_timeout_add_seconds (5, (GSourceFunc) after_blocking_timeout_cb, test_data);

  return G_SOURCE_REMOVE;
}

gint
main (gint argc, gchar * argv[])
{
  GstElement *pipeline;
  GstBus *bus;
  guint bus_watch_id;
  GError *err = NULL;
  TestData test_data;

  gst_init (&argc, &argv);

  loop = g_main_loop_new (NULL, FALSE);

  pipeline =
      gst_parse_launch
      ("videotestsrc ! timeoverlay ! clocksync name=c ! autovideosink sync=false",
      &err);
  if (!pipeline) {
    gst_println ("Couldn't construct pipeline, %s", err->message);
    g_clear_error (&err);
    return 1;
  }

  test_data.clocksync = gst_bin_get_by_name (GST_BIN (pipeline), "c");
  test_data.sinkpad = gst_element_get_static_pad (test_data.clocksync, "sink");

  /* Enable automatic ts-offset calculation. "resync" signal will have an effect
   * only if sync-to-first is enabled */
  g_object_set (test_data.clocksync, "sync-to-first", TRUE, NULL);

  /* Add 5 seconds timer to add blocking pad probe */
  g_timeout_add_seconds (5, (GSourceFunc) timeout_cb, &test_data);

  bus = gst_element_get_bus (pipeline);
  bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);
  gst_object_unref (bus);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_main_loop_run (loop);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (test_data.sinkpad);
  gst_object_unref (test_data.clocksync);
  gst_object_unref (pipeline);
  g_source_remove (bus_watch_id);
  g_main_loop_unref (loop);

  return 0;
}
