/* GStreamer
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
#include <gst/video/video.h>

#include <windows.h>
#include <string.h>
#include "../key-handler.h"

static GMainLoop *loop = nullptr;
static HWND hwnd_0 = nullptr;
static HWND hwnd_1 = nullptr;

struct AppData
{
  GstElement *pipeline = nullptr;
  GstVideoOverlay *overlay_0 = nullptr;
  GstVideoOverlay *overlay_1 = nullptr;
  guint mode = 0;
};

#define APP_DATA_PROP_NAME L"EXAMPLE-APP-DATA"

static LRESULT CALLBACK
window_proc (HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
  switch (message) {
    case WM_NCCREATE:
    {
      LPCREATESTRUCTW lpcs = (LPCREATESTRUCTW) lparam;
      auto data = (AppData *) lpcs->lpCreateParams;
      SetPropW (hwnd, APP_DATA_PROP_NAME, data);
      break;
    }
    case WM_DESTROY:
      gst_println ("Destroy window %d", hwnd == hwnd_0 ? 0 : 1);
      if (loop)
        g_main_loop_quit (loop);
      break;
    case WM_SIZE:
    {
      auto data = (AppData *) GetPropW (hwnd, APP_DATA_PROP_NAME);
      if (!data || !data->overlay_0 || !data->overlay_1)
        break;

      if ((data->mode == 1 && hwnd == hwnd_0) ||
          (data->mode == 2 && hwnd == hwnd_1)) {
        RECT rect;
        GetClientRect (hwnd, &rect);
        gint width = (rect.right - rect.left) / 2;
        gint height = (rect.bottom - rect.top);

        auto overlay_0 = data->overlay_0;
        auto overlay_1 = data->overlay_1;

        gst_video_overlay_set_render_rectangle (overlay_0,
            0, 0, width, height);
        gst_video_overlay_set_render_rectangle (overlay_1,
            width, 0, width, height);
      }
      break;
    }
    default:
      break;
  }

  return DefWindowProcW (hwnd, message, wparam, lparam);
}

static void
keyboard_cb (gchar input, gboolean is_ascii, AppData * data)
{
  if (!is_ascii)
    return;

  switch (input) {
    case 'q':
    case 'Q':
      gst_element_send_event (data->pipeline, gst_event_new_eos ());
      break;
    case ' ':
      data->mode++;
      data->mode %= 4;
      switch (data->mode) {
        case 0:
        {
          auto overlay_0 = data->overlay_0;
          auto overlay_1 = data->overlay_1;
          gst_video_overlay_set_window_handle (overlay_0,
              (guintptr) hwnd_0);
          gst_video_overlay_set_render_rectangle (overlay_0, 0, 0, -1, -1);

          gst_video_overlay_set_window_handle (overlay_1,
              (guintptr) hwnd_1);
          gst_video_overlay_set_render_rectangle (overlay_1, 0, 0, -1, -1);
          break;
        }
        case 1:
        {
          RECT rect;
          GetClientRect (hwnd_0, &rect);
          gint width = (rect.right - rect.left) / 2;
          gint height = (rect.bottom - rect.top);

          auto overlay_0 = data->overlay_0;
          auto overlay_1 = data->overlay_1;

          gst_video_overlay_set_window_handle (overlay_0,
              (guintptr) hwnd_0);
          gst_video_overlay_set_render_rectangle (overlay_0,
              0, 0, width, height);

          gst_video_overlay_set_window_handle (overlay_1,
              (guintptr) hwnd_0);
          gst_video_overlay_set_render_rectangle (overlay_1,
              width, 0, width, height);
          break;
        }
        case 2:
        {
          RECT rect;
          GetClientRect (hwnd_1, &rect);
          gint width = (rect.right - rect.left) / 2;
          gint height = (rect.bottom - rect.top);

          auto overlay_0 = data->overlay_0;
          auto overlay_1 = data->overlay_1;

          gst_video_overlay_set_window_handle (overlay_0,
              (guintptr) hwnd_1);
          gst_video_overlay_set_render_rectangle (overlay_0,
              0, 0, width, height);

          gst_video_overlay_set_window_handle (overlay_1,
              (guintptr) hwnd_1);
          gst_video_overlay_set_render_rectangle (overlay_1,
              width, 0, width, height);
          break;
        }
        case 3:
        {
          auto overlay_0 = data->overlay_0;
          auto overlay_1 = data->overlay_1;
          gst_video_overlay_set_window_handle (overlay_0,
              (guintptr) hwnd_1);
          gst_video_overlay_set_render_rectangle (overlay_0, 0, 0, -1, -1);

          gst_video_overlay_set_window_handle (overlay_1,
              (guintptr) hwnd_0);
          gst_video_overlay_set_render_rectangle (overlay_1, 0, 0, -1, -1);
          break;
        }
        default:
          break;
      }
      break;
    default:
      break;
  }
}

static gboolean
bus_msg (GstBus * bus, GstMessage * msg, AppData * data)
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

static gboolean
msg_cb (GIOChannel * source, GIOCondition condition, gpointer data)
{
  MSG msg;

  if (!PeekMessageW (&msg, nullptr, 0, 0, PM_REMOVE))
    return G_SOURCE_CONTINUE;

  TranslateMessage (&msg);
  DispatchMessage (&msg);

  return G_SOURCE_CONTINUE;
}

static void
print_keyboard_help (void)
{
  static struct
  {
    const gchar *key_desc;
    const gchar *key_help;
  } key_controls[] = {
    {
        "q", "Quit"}, {
        "space", "Toggle render window"}
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

gint
main (gint argc, gchar ** argv)
{
  WNDCLASSEXW wc = { };
  HINSTANCE hinstance = GetModuleHandle (nullptr);
  GIOChannel *msg_io_channel = nullptr;
  RECT wr = { 0, 0, 320, 240 };
  AppData app_data = { };

  gst_init (nullptr, nullptr);

  print_keyboard_help ();

  loop = g_main_loop_new (nullptr, FALSE);

  /* prepare window */
  wc.cbSize = sizeof (WNDCLASSEXW);
  wc.lpfnWndProc = (WNDPROC) window_proc;
  wc.hInstance = hinstance;
  wc.hIcon = LoadIcon (nullptr, IDI_WINLOGO);
  wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
  wc.hCursor = LoadCursor (nullptr, IDC_ARROW);
  wc.hbrBackground = (HBRUSH) GetStockObject (BLACK_BRUSH);
  wc.lpszClassName = L"GstD3D12VideoSinkExample";

  RegisterClassExW (&wc);
  AdjustWindowRect (&wr, WS_OVERLAPPEDWINDOW, FALSE);

  hwnd_0 = CreateWindowExW (0, wc.lpszClassName, L"Window-0",
      WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_OVERLAPPEDWINDOW | WS_VISIBLE,
      CW_USEDEFAULT, CW_USEDEFAULT,
      wr.right - wr.left, wr.bottom - wr.top, (HWND) nullptr, (HMENU) nullptr,
      hinstance, &app_data);

  hwnd_1 = CreateWindowExW (0, wc.lpszClassName, L"Window-1",
      WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_OVERLAPPEDWINDOW | WS_VISIBLE,
      CW_USEDEFAULT, CW_USEDEFAULT,
      wr.right - wr.left, wr.bottom - wr.top, (HWND) nullptr, (HMENU) nullptr,
      hinstance, &app_data);

  msg_io_channel = g_io_channel_win32_new_messages (0);
  g_io_add_watch (msg_io_channel, G_IO_IN, msg_cb, nullptr);

  /* prepare the pipeline */
  app_data.pipeline = gst_parse_launch ("d3d12testsrc pattern=ball ! queue ! "
      "d3d12videosink name=sink0 d3d12testsrc ! queue ! "
      "d3d12videosink name=sink1", nullptr);
  if (!app_data.pipeline) {
    gst_printerrln ("Couldn't create pipeline");
    return 0;
  }

  auto sink_0 = gst_bin_get_by_name (GST_BIN (app_data.pipeline), "sink0");
  auto sink_1 = gst_bin_get_by_name (GST_BIN (app_data.pipeline), "sink1");

  app_data.overlay_0 = GST_VIDEO_OVERLAY (sink_0);
  app_data.overlay_1 = GST_VIDEO_OVERLAY (sink_1);

  gst_video_overlay_set_window_handle (app_data.overlay_0, (guintptr) hwnd_0);
  gst_video_overlay_set_window_handle (app_data.overlay_1, (guintptr) hwnd_1);

  gst_object_unref (sink_0);
  gst_object_unref (sink_1);

  gst_bus_add_watch (GST_ELEMENT_BUS (app_data.pipeline), (GstBusFunc) bus_msg,
      &app_data);

  set_key_handler ((KeyInputCallback) keyboard_cb, &app_data);

  gst_element_set_state (app_data.pipeline, GST_STATE_PLAYING);
  g_main_loop_run (loop);

  gst_element_set_state (app_data.pipeline, GST_STATE_NULL);
  gst_bus_remove_watch (GST_ELEMENT_BUS (app_data.pipeline));

  gst_object_unref (app_data.pipeline);
  unset_key_handler ();

  if (hwnd_0)
    DestroyWindow (hwnd_0);

  if (hwnd_1)
    DestroyWindow (hwnd_1);

  if (msg_io_channel)
    g_io_channel_unref (msg_io_channel);
  g_main_loop_unref (loop);

  gst_deinit ();

  return 0;
}
