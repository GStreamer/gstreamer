/*
 * GStreamer
 * Copyright (C) 2009 David A. Schleef <ds@schleef.org>
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

#include "gstgtk.h"

#if defined(GDK_WINDOWING_WIN32)
#include <gdk/gdkwin32.h>
#elif defined(GDK_WINDOWING_X11)
#include <gdk/gdkx.h>
#elif defined(GDK_WINDOWING_QUARTZ)
#include <gdk/gdkquartz.h>
#else
#error unimplemented GTK backend
#endif


void
gst_video_overlay_set_gtk_window (GstVideoOverlay * videooverlay,
    GtkWidget * window)
{

#if defined(GDK_WINDOWING_WIN32)
  gst_video_overlay_set_window_handle (videooverlay,
      (gulong) GDK_WINDOW_HWND (window->window));
#elif defined(GDK_WINDOWING_QUARTZ)
  gst_video_overlay_set_window_handle (videooverlay,
      (gulong) gdk_quartz_window_get_nswindow (window->window));
#elif defined(GDK_WINDOWING_X11)
  gst_video_overlay_set_window_handle (videooverlay,
      GDK_WINDOW_XWINDOW (window->window));
#else
#error unimplemented GTK backend
#endif

}
