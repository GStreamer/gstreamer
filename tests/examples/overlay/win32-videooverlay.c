/*
 * GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
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
#include <gst/video/videooverlay.h>
#include <gst/video/gstvideosink.h>
#include <windows.h>
#include <string.h>

static GMainLoop *loop = NULL;
static gboolean visible = FALSE;
static GIOChannel *msg_io_channel;
static HWND hwnd = NULL;

#define DEFAULT_VIDEO_SINK "glimagesink"

static LRESULT CALLBACK
window_proc (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  switch (message) {
    case WM_DESTROY:
      hwnd = NULL;

      if (loop) {
        g_main_loop_quit (loop);
      }
      return 0;
    default:
      break;
  }

  return DefWindowProc (hWnd, message, wParam, lParam);
}

static gboolean
bus_msg (GstBus * bus, GstMessage * msg, gpointer user_data)
{
  GstElement *pipeline = GST_ELEMENT (user_data);
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ASYNC_DONE:
      /* make window visible when we have something to show */
      if (!visible && hwnd) {
        ShowWindow (hwnd, SW_SHOW);
        visible = TRUE;
      }

      gst_element_set_state (pipeline, GST_STATE_PLAYING);
      break;
    case GST_MESSAGE_ERROR:{
      GError *err;
      gchar *dbg;

      gst_message_parse_error (msg, &err, &dbg);
      g_printerr ("ERROR %s \n", err->message);
      if (dbg != NULL)
        g_printerr ("ERROR debug information: %s\n", dbg);
      g_clear_error (&err);
      g_free (dbg);

      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

static gboolean
msg_cb (GIOChannel * source, GIOCondition condition, gpointer data)
{
  MSG msg;

  if (!PeekMessage (&msg, NULL, 0, 0, PM_REMOVE))
    return G_SOURCE_CONTINUE;

  TranslateMessage (&msg);
  DispatchMessage (&msg);

  return G_SOURCE_CONTINUE;
}

gint
main (gint argc, gchar ** argv)
{
  GstElement *pipeline, *src, *sink;
  GstStateChangeReturn sret;
  WNDCLASSEX wc = { 0, };
  HINSTANCE hinstance = GetModuleHandle (NULL);
  GstBus *bus;
  GIOChannel *msg_io_channel;
  GOptionContext *option_ctx;
  GError *error = NULL;
  gchar *video_sink = NULL;
  gchar *title = NULL;
  RECT wr = { 0, 0, 320, 240 };
  gint exitcode = 0;
  GOptionEntry options[] = {
    {"videosink", 0, 0, G_OPTION_ARG_STRING, &video_sink,
        "Video sink to use (default is glimagesink)", NULL}
    ,
    {NULL}
  };

  option_ctx = g_option_context_new ("WIN32 video overlay example");
  g_option_context_add_main_entries (option_ctx, options, NULL);
  g_option_context_set_help_enabled (option_ctx, TRUE);
  if (!g_option_context_parse (option_ctx, &argc, &argv, &error)) {
    g_printerr ("option parsing failed: %s\n", error->message);
    g_clear_error (&error);
    exit (1);
  }

  g_option_context_free (option_ctx);
  gst_init (NULL, NULL);

  /* prepare window */
  wc.cbSize = sizeof (WNDCLASSEX);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = (WNDPROC) window_proc;
  wc.hInstance = hinstance;
  wc.hCursor = LoadCursor (NULL, IDC_ARROW);
  wc.lpszClassName = "GstWIN32VideoOverlay";
  RegisterClassEx (&wc);

  title = g_strdup_printf ("%s - Win32-VideoOverlay",
      video_sink ? video_sink : DEFAULT_VIDEO_SINK);

  AdjustWindowRect (&wr, WS_OVERLAPPEDWINDOW, FALSE);
  hwnd = CreateWindowEx (0, wc.lpszClassName,
      title,
      WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT,
      wr.right - wr.left, wr.bottom - wr.top, (HWND) NULL, (HMENU) NULL,
      hinstance, NULL);

  loop = g_main_loop_new (NULL, FALSE);
  msg_io_channel = g_io_channel_win32_new_messages (0);
  g_io_add_watch (msg_io_channel, G_IO_IN, msg_cb, NULL);

  /* prepare the pipeline */
  pipeline = gst_pipeline_new ("win32-overlay");
  src = gst_element_factory_make ("videotestsrc", NULL);
  if (video_sink) {
    sink = gst_element_factory_make (video_sink, NULL);
  } else {
    sink = gst_element_factory_make (DEFAULT_VIDEO_SINK, NULL);
  }

  if (!sink) {
    g_printerr ("%s element is not available\n",
        video_sink ? video_sink : DEFAULT_VIDEO_SINK);
    goto terminate;
  }

  gst_bin_add_many (GST_BIN (pipeline), src, sink, NULL);
  gst_element_link (src, sink);

  gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (sink),
      (guintptr) hwnd);

  gst_bus_add_watch (GST_ELEMENT_BUS (pipeline), bus_msg, pipeline);

  /* run the pipeline */
  sret = gst_element_set_state (pipeline, GST_STATE_PAUSED);
  if (sret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Pipeline doesn't want to pause\n");
  } else {
    g_main_loop_run (loop);
  }

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_bus_remove_watch (GST_ELEMENT_BUS (pipeline));

terminate:
  if (hwnd)
    DestroyWindow (hwnd);

  gst_object_unref (pipeline);
  g_io_channel_unref (msg_io_channel);
  g_main_loop_unref (loop);
  g_free (title);

  return exitcode;
}
