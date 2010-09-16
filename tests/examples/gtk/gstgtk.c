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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
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
gst_x_overlay_set_gtk_window (GstXOverlay * xoverlay, GtkWidget * window)
{

#if defined(GDK_WINDOWING_WIN32)
  gst_x_overlay_set_window_handle (xoverlay,
      (gulong) GDK_WINDOW_HWND (window->window));
#elif defined(GDK_WINDOWING_QUARTZ)
  gst_x_overlay_set_window_handle (xoverlay,
      (gulong) gdk_quartz_window_get_nswindow (window->window));
#elif defined(GDK_WINDOWING_X11)
  gst_x_overlay_set_window_handle (xoverlay,
      GDK_WINDOW_XWINDOW (window->window));
#else
#error unimplemented GTK backend
#endif

}
