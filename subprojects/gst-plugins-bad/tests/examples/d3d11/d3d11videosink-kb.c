/* GStreamer command line playback testing utility - keyboard handling helpers
 *
 * Copyright (C) 2013 Tim-Philipp Müller <tim centricular net>
 * Copyright (C) 2013 Centricular Ltd
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

#include "d3d11videosink-kb.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <io.h>

#include <gst/gst.h>

/* This is all not thread-safe, but doesn't have to be really */
static GstD3D11VideoSinkKbFunc kb_callback;
static gpointer kb_callback_data;

typedef struct
{
  GThread *thread;
  HANDLE event_handle;
  HANDLE console_handle;
  gboolean closing;
  GMutex lock;
} Win32KeyHandler;

static Win32KeyHandler *win32_handler = NULL;

static gboolean
gst_d3d11_video_sink_source_cb (Win32KeyHandler * handler)
{
  HANDLE h_input = handler->console_handle;
  INPUT_RECORD buffer;
  DWORD n;

  if (PeekConsoleInput (h_input, &buffer, 1, &n) && n == 1) {
    ReadConsoleInput (h_input, &buffer, 1, &n);

    if (buffer.EventType == KEY_EVENT && buffer.Event.KeyEvent.bKeyDown) {
      if (buffer.Event.KeyEvent.wVirtualKeyCode == VK_SPACE) {
        kb_callback (' ', kb_callback_data);
      } else {
        kb_callback (buffer.Event.KeyEvent.uChar.AsciiChar, kb_callback_data);
      }
    }
  }

  return G_SOURCE_REMOVE;
}

static gpointer
gst_d3d11_video_sink_kb_thread (gpointer user_data)
{
  Win32KeyHandler *handler = (Win32KeyHandler *) user_data;
  HANDLE handles[2];

  handles[0] = handler->event_handle;
  handles[1] = handler->console_handle;

  if (!kb_callback)
    return NULL;

  while (TRUE) {
    DWORD ret = WaitForMultipleObjects (2, handles, FALSE, INFINITE);

    if (ret == WAIT_FAILED) {
      GST_WARNING ("WaitForMultipleObject Failed");
      return NULL;
    }

    g_mutex_lock (&handler->lock);
    if (handler->closing) {
      g_mutex_unlock (&handler->lock);

      return NULL;
    }
    g_mutex_unlock (&handler->lock);

    g_idle_add ((GSourceFunc) gst_d3d11_video_sink_source_cb, handler);
  }

  return NULL;
}

gboolean
gst_d3d11_video_sink_kb_set_key_handler (GstD3D11VideoSinkKbFunc kb_func,
    gpointer user_data)
{
  gint fd = _fileno (stdin);

  if (!_isatty (fd)) {
    GST_INFO ("stdin is not connected to a terminal");
    return FALSE;
  }

  if (win32_handler) {
    g_mutex_lock (&win32_handler->lock);
    win32_handler->closing = TRUE;
    g_mutex_unlock (&win32_handler->lock);

    SetEvent (win32_handler->event_handle);
    g_thread_join (win32_handler->thread);
    CloseHandle (win32_handler->event_handle);

    g_mutex_clear (&win32_handler->lock);
    g_free (win32_handler);
    win32_handler = NULL;
  }

  if (kb_func) {
    SECURITY_ATTRIBUTES sec_attrs;

    sec_attrs.nLength = sizeof (SECURITY_ATTRIBUTES);
    sec_attrs.lpSecurityDescriptor = NULL;
    sec_attrs.bInheritHandle = FALSE;

    win32_handler = g_new0 (Win32KeyHandler, 1);

    /* create cancellable event handle */
    win32_handler->event_handle = CreateEvent (&sec_attrs, TRUE, FALSE, NULL);

    if (!win32_handler->event_handle) {
      g_warning ("Couldn't create event handle\n");
      g_free (win32_handler);
      win32_handler = NULL;

      return FALSE;
    }

    win32_handler->console_handle = GetStdHandle (STD_INPUT_HANDLE);
    if (!win32_handler->console_handle) {
      g_warning ("Couldn't get console handle\n");
      CloseHandle (win32_handler->event_handle);
      g_free (win32_handler);
      win32_handler = NULL;

      return FALSE;
    }

    g_mutex_init (&win32_handler->lock);
    win32_handler->thread =
        g_thread_new ("gst-play-kb", gst_d3d11_video_sink_kb_thread,
        win32_handler);
  }

  kb_callback = kb_func;
  kb_callback_data = user_data;

  return TRUE;
}
