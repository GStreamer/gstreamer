/*
 * GStreamer
 * Copyright (C) 2012 Matthew Waters <ystreet00@gmail.com>
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

#include "x11_event_source.h"
#include "gstgldisplay_x11.h"

extern gboolean gst_gl_window_x11_handle_event (GstGLWindowX11 * window_x11);

typedef struct _X11EventSource
{
  GSource source;
  GPollFD pfd;
  uint32_t mask;
  GstGLWindowX11 *window;
} X11EventSource;

static gboolean
x11_event_source_prepare (GSource * base, gint * timeout)
{
  X11EventSource *source = (X11EventSource *) base;
  gboolean retval;

  *timeout = -1;

  retval = XPending (source->window->device);

  return retval;
}

static gboolean
x11_event_source_check (GSource * base)
{
  X11EventSource *source = (X11EventSource *) base;
  gboolean retval;

  retval = source->pfd.revents;

  return retval;
}

static gboolean
x11_event_source_dispatch (GSource * base, GSourceFunc callback, gpointer data)
{
  X11EventSource *source = (X11EventSource *) base;

  gboolean ret = gst_gl_window_x11_handle_event (source->window);

  if (callback)
    callback (data);

  return ret;
}

static GSourceFuncs x11_event_source_funcs = {
  x11_event_source_prepare,
  x11_event_source_check,
  x11_event_source_dispatch,
  NULL
};

GSource *
x11_event_source_new (GstGLWindowX11 * window_x11)
{
  X11EventSource *source;

  source = (X11EventSource *)
      g_source_new (&x11_event_source_funcs, sizeof (X11EventSource));
  source->window = window_x11;
  source->pfd.fd = ConnectionNumber (source->window->device);
  source->pfd.events = G_IO_IN | G_IO_ERR;
  g_source_add_poll (&source->source, &source->pfd);

  return &source->source;
}
