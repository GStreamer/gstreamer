/*
 * GStreamer
 * Copyright (C) 2019 Nirbheek Chauhan <nirbheek@centricular.com>
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

#include <gst/gl/gstglcontext.h>
#include <gst/gl/egl/gstglcontext_egl.h>

#include "gstglwindow_winrt_egl.h"
#include "../gstglwindow_private.h"

#define GST_CAT_DEFAULT gst_gl_window_debug

#define GST_GL_WINDOW_WINRT_EGL_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
    GST_TYPE_GL_WINDOW_WINRT_EGL, GstGLWindowWinRTEGLPrivate))


G_DEFINE_TYPE (GstGLWindowWinRTEGL, gst_gl_window_winrt_egl,
    GST_TYPE_GL_WINDOW);

static guintptr gst_gl_window_winrt_egl_get_display (GstGLWindow * window);
static guintptr gst_gl_window_winrt_egl_get_window_handle (GstGLWindow *
    window);
static void gst_gl_window_winrt_egl_set_window_handle (GstGLWindow * window,
    guintptr handle);

static void
gst_gl_window_winrt_egl_class_init (GstGLWindowWinRTEGLClass * klass)
{
  GstGLWindowClass *window_class = (GstGLWindowClass *) klass;

  window_class->get_display =
      GST_DEBUG_FUNCPTR (gst_gl_window_winrt_egl_get_display);
  window_class->get_window_handle =
      GST_DEBUG_FUNCPTR (gst_gl_window_winrt_egl_get_window_handle);
  window_class->set_window_handle =
      GST_DEBUG_FUNCPTR (gst_gl_window_winrt_egl_set_window_handle);
}

static void
gst_gl_window_winrt_egl_init (GstGLWindowWinRTEGL * window_winrt)
{
}

static void
gst_gl_window_winrt_egl_set_window_handle (GstGLWindow * window,
    guintptr handle)
{
  GstGLWindowWinRTEGL *window_egl = GST_GL_WINDOW_WINRT_EGL (window);

  GST_INFO_OBJECT (window, "Setting WinRT EGL window handle: %p", handle);

  window_egl->window = (EGLNativeWindowType) handle;
}

static guintptr
gst_gl_window_winrt_egl_get_window_handle (GstGLWindow * window)
{
  GstGLWindowWinRTEGL *window_egl = GST_GL_WINDOW_WINRT_EGL (window);

  GST_INFO_OBJECT (window, "Getting WinRT EGL window handle");

  return (guintptr) window_egl->window;
}

/* Must be called in the gl thread */
GstGLWindowWinRTEGL *
gst_gl_window_winrt_egl_new (GstGLDisplay * display)
{
  GstGLWindowWinRTEGL *window_egl;

  GST_INFO_OBJECT (display, "Trying to create WinRT EGL window");

  if ((gst_gl_display_get_handle_type (display) & GST_GL_DISPLAY_TYPE_EGL) == 0)
    /* we require an EGL display to create windows */
    return NULL;

  GST_INFO_OBJECT (display, "Creating WinRT EGL window");

  window_egl = g_object_new (GST_TYPE_GL_WINDOW_WINRT_EGL, NULL);

  return window_egl;
}

static guintptr
gst_gl_window_winrt_egl_get_display (GstGLWindow * window)
{
  /* EGL_DEFAULT_DISPLAY */
  return 0;
}
