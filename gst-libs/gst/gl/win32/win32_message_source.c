/*
 * GStreamer
 * Copyright (C) 2012 Matthew Waters <ystreet00@gmail.com>
 * Copyright (C) 2015 Collabora ltd.
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

#include <stdint.h>
#include <stdlib.h>

#include "win32_message_source.h"

typedef struct _Win32MessageSource
{
  GSource source;
  GPollFD pfd;
  GstGLWindowWin32 *window;
} Win32MessageSource;

static gboolean
win32_message_source_check (GSource * base)
{
  MSG msg;

  return PeekMessage (&msg, NULL, 0, 0, PM_NOREMOVE);
}

static gboolean
win32_message_source_dispatch (GSource * base, GSourceFunc callback,
    gpointer user_data)
{
  Win32MessageSource *source = (Win32MessageSource *) base;
  Win32MessageSourceFunc func = (Win32MessageSourceFunc) callback;
  MSG msg;

  if (!PeekMessage (&msg, NULL, 0, 0, PM_REMOVE))
    return G_SOURCE_CONTINUE;

  if (func)
    func (source->window, &msg, user_data);

  return G_SOURCE_CONTINUE;
}

static GSourceFuncs win32_message_source_funcs = {
  NULL,
  win32_message_source_check,
  win32_message_source_dispatch,
  NULL
};

GSource *
win32_message_source_new (GstGLWindowWin32 * window_win32)
{
  Win32MessageSource *source;

  source = (Win32MessageSource *)
      g_source_new (&win32_message_source_funcs, sizeof (Win32MessageSource));
  source->window = window_win32;
  source->pfd.fd = G_WIN32_MSG_HANDLE;
  source->pfd.events = G_IO_IN;
  g_source_add_poll (&source->source, &source->pfd);

  return &source->source;
}
