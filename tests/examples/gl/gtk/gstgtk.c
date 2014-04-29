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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gl/gl.h>
#include "gstgtk.h"

#if GST_GL_HAVE_WINDOW_WIN32 && defined(GDK_WINDOWING_WIN32)
#include <gdk/gdkwin32.h>
#endif
#if GST_GL_HAVE_WINDOW_X11 && defined(GDK_WINDOWING_X11)
#include <gdk/gdkx.h>
#endif
#if GST_GL_HAVE_WINDOW_COCOA && defined(GDK_WINDOWING_QUARTZ)
#include <gdk/gdkquartz.h>
#endif


void
gst_video_overlay_set_gtk_window (GstVideoOverlay * videooverlay,
    GtkWidget * widget)
{
  GdkWindow *window;
  GdkDisplay *display;
  const gchar *user_choice = g_getenv ("GST_GL_WINDOW");

  window = gtk_widget_get_window (widget);
  display = gdk_window_get_display (window);

#if GST_GL_HAVE_WINDOW_WIN32 && defined(GDK_WINDOWING_WIN32)
  if (GDK_IS_WIN32_DISPLAY (display) && (!user_choice
          || g_strcmp0 (user_choice, "win32") == 0)) {
    gst_video_overlay_set_window_handle (videooverlay,
        (guintptr) GDK_WINDOW_HWND (window));
  } else
#endif
#if GST_GL_HAVE_WINDOW_COCOA && defined(GDK_WINDOWING_QUARTZ)
  if (GDK_IS_QUARTZ_DISPLAY (display) && (!user_choice
          || g_strcmp0 (user_choice, "cocoa") == 0)) {
    gst_video_overlay_set_window_handle (videooverlay, (guintptr)
        gdk_quartz_window_get_nswindow (window));
  } else
#endif
#if GST_GL_HAVE_WINDOW_X11 && defined(GDK_WINDOWING_X11)
  if (GDK_IS_X11_DISPLAY (display) && (!user_choice
          || g_strcmp0 (user_choice, "x11") == 0)) {
    gst_video_overlay_set_window_handle (videooverlay, GDK_WINDOW_XID (window));
  } else
#endif
    g_error ("Unsupported Gtk+ backend");
}
