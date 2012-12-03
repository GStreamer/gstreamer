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

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include "gstglwindow.h"

#ifdef HAVE_WINDOW_X11
#include "x11/gstglwindow_x11.h"
#endif
#ifdef HAVE_WINDOW_WIN32
#include "win32/gstglwindow_win32.h"
#endif
#ifdef HAVE_WINDOW_COCOA
#include "cocoa/gstglwindow_cocoa.h"
#endif
#ifdef HAVE_WINDOW_WAYLAND
#include "wayland/gstglwindow_wayland_egl.h"
#endif

#define GST_CAT_DEFAULT gst_gl_window_debug
GST_DEBUG_CATEGORY (GST_CAT_DEFAULT);

#define gst_gl_window_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE (GstGLWindow, gst_gl_window, G_TYPE_OBJECT);

static void
gst_gl_window_init (GstGLWindow * window)
{
  g_mutex_init (&window->lock);
  window->need_lock = TRUE;
}

static void
gst_gl_window_class_init (GstGLWindowClass * klass)
{
}

GstGLWindow *
gst_gl_window_new (GstGLAPI api, guintptr external_gl_context)
{
  GstGLWindow *window = NULL;
  const gchar *user_choice;
  static volatile gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_INIT (gst_gl_window_debug, "glwindow", 0,
        "glwindow element");
    g_once_init_leave (&_init, 1);
  }

  user_choice = g_getenv ("GST_GL_WINDOW");
  GST_INFO ("creating a window, user choice:%s", user_choice);

#ifdef HAVE_WINDOW_X11
  if (!window && (!user_choice || g_strstr_len (user_choice, 3, "x11")))
    window = GST_GL_WINDOW (gst_gl_window_x11_new (api, external_gl_context));
#endif
#ifdef HAVE_WINDOW_WIN32
  if (!window && (!user_choice || g_strstr_len (user_choice, 5, "win32")))
    window = GST_GL_WINDOW (gst_gl_window_win32_new (api, external_gl_context));
#endif
#ifdef HAVE_WINDOW_COCOA
  if (!window && (!user_choice || g_strstr_len (user_choice, 5, "cocoa")))
    window = GST_GL_WINDOW (gst_gl_window_cocoa_new (api, external_gl_context));
#endif
#ifdef HAVE_WINDOW_WAYLAND
  if (!window && (!user_choice || g_strstr_len (user_choice, 7, "wayland")))
    window =
        GST_GL_WINDOW (gst_gl_window_wayland_egl_new (api,
            external_gl_context));
#endif
  if (!window) {
    GST_WARNING ("could not create a window, user choice:%s", user_choice);
    /* FIXME: set and return a GError */
    return NULL;
  }

  window->external_gl_context = external_gl_context;

  return window;
}

guintptr
gst_gl_window_get_gl_context (GstGLWindow * window)
{
  GstGLWindowClass *window_class;
  guintptr result;

  g_return_val_if_fail (GST_GL_IS_WINDOW (window), 0);
  window_class = GST_GL_WINDOW_GET_CLASS (window);
  g_return_val_if_fail (window_class->get_gl_context != NULL, 0);

  GST_GL_WINDOW_LOCK (window);
  result = window_class->get_gl_context (window);
  GST_GL_WINDOW_UNLOCK (window);

  return result;
}

gboolean
gst_gl_window_activate (GstGLWindow * window, gboolean activate)
{
  GstGLWindowClass *window_class;
  gboolean result;

  g_return_val_if_fail (GST_GL_IS_WINDOW (window), FALSE);
  window_class = GST_GL_WINDOW_GET_CLASS (window);
  g_return_val_if_fail (window_class->activate != NULL, FALSE);

  GST_GL_WINDOW_LOCK (window);
  result = window_class->activate (window, activate);
  GST_GL_WINDOW_UNLOCK (window);

  return result;
}

void
gst_gl_window_set_window_handle (GstGLWindow * window, guintptr handle)
{
  GstGLWindowClass *window_class;

  g_return_if_fail (GST_GL_IS_WINDOW (window));
  g_return_if_fail (handle != 0);
  window_class = GST_GL_WINDOW_GET_CLASS (window);
  g_return_if_fail (window_class->set_window_handle != NULL);

  GST_GL_WINDOW_LOCK (window);
  window_class->set_window_handle (window, handle);
  GST_GL_WINDOW_UNLOCK (window);
}

void
gst_gl_window_draw_unlocked (GstGLWindow * window, guint width, guint height)
{
  GstGLWindowClass *window_class;

  g_return_if_fail (GST_GL_IS_WINDOW (window));
  window_class = GST_GL_WINDOW_GET_CLASS (window);
  g_return_if_fail (window_class->draw_unlocked != NULL);

  window_class->draw_unlocked (window, width, height);
}

void
gst_gl_window_draw (GstGLWindow * window, guint width, guint height)
{
  GstGLWindowClass *window_class;

  g_return_if_fail (GST_GL_IS_WINDOW (window));
  window_class = GST_GL_WINDOW_GET_CLASS (window);
  g_return_if_fail (window_class->draw != NULL);

  GST_GL_WINDOW_LOCK (window);
  window_class->draw (window, width, height);
  GST_GL_WINDOW_UNLOCK (window);
}

void
gst_gl_window_run (GstGLWindow * window)
{
  GstGLWindowClass *window_class;

  g_return_if_fail (GST_GL_IS_WINDOW (window));
  window_class = GST_GL_WINDOW_GET_CLASS (window);
  g_return_if_fail (window_class->run != NULL);

  GST_GL_WINDOW_LOCK (window);
  window_class->run (window);
  GST_GL_WINDOW_UNLOCK (window);
}

void
gst_gl_window_quit (GstGLWindow * window, GstGLWindowCB callback, gpointer data)
{
  GstGLWindowClass *window_class;

  g_return_if_fail (GST_GL_IS_WINDOW (window));
  window_class = GST_GL_WINDOW_GET_CLASS (window);
  g_return_if_fail (window_class->quit != NULL);

  GST_GL_WINDOW_LOCK (window);

  window->close = callback;
  window->close_data = data;
  window_class->quit (window, callback, data);

  GST_GL_WINDOW_UNLOCK (window);
}

void
gst_gl_window_send_message (GstGLWindow * window, GstGLWindowCB callback,
    gpointer data)
{
  GstGLWindowClass *window_class;

  g_return_if_fail (GST_GL_IS_WINDOW (window));
  g_return_if_fail (callback != NULL);
  window_class = GST_GL_WINDOW_GET_CLASS (window);
  g_return_if_fail (window_class->quit != NULL);

  GST_GL_WINDOW_LOCK (window);
  window_class->send_message (window, callback, data);
  GST_GL_WINDOW_UNLOCK (window);
}

/**
 * gst_gl_window_set_need_lock:
 *
 * window: a #GstGLWindow
 * need_lock: whether the @window needs to lock for concurrent access
 *
 * This API is intended only for subclasses of #GstGLWindow in order to ensure
 * correct interaction with the underlying window system.
 */
void
gst_gl_window_set_need_lock (GstGLWindow * window, gboolean need_lock)
{
  g_return_if_fail (GST_GL_IS_WINDOW (window));

  window->need_lock = need_lock;
}

void
gst_gl_window_set_draw_callback (GstGLWindow * window, GstGLWindowCB callback,
    gpointer data)
{
  g_return_if_fail (GST_GL_IS_WINDOW (window));

  GST_GL_WINDOW_LOCK (window);

  window->draw = callback;
  window->draw_data = data;

  GST_GL_WINDOW_UNLOCK (window);
}

void
gst_gl_window_set_resize_callback (GstGLWindow * window,
    GstGLWindowResizeCB callback, gpointer data)
{
  g_return_if_fail (GST_GL_IS_WINDOW (window));

  GST_GL_WINDOW_LOCK (window);

  window->resize = callback;
  window->resize_data = data;

  GST_GL_WINDOW_UNLOCK (window);
}

void
gst_gl_window_set_close_callback (GstGLWindow * window, GstGLWindowCB callback,
    gpointer data)
{
  g_return_if_fail (GST_GL_IS_WINDOW (window));

  GST_GL_WINDOW_LOCK (window);

  window->close = callback;
  window->close_data = data;

  GST_GL_WINDOW_UNLOCK (window);
}

GstGLAPI
gst_gl_window_get_gl_api (GstGLWindow * window)
{
  GstGLAPI ret;
  GstGLWindowClass *window_class;

  g_return_val_if_fail (GST_GL_IS_WINDOW (window), GST_GL_API_NONE);
  window_class = GST_GL_WINDOW_GET_CLASS (window);
  g_return_val_if_fail (window_class->get_gl_api != NULL, GST_GL_API_NONE);

  GST_GL_WINDOW_LOCK (window);

  ret = window_class->get_gl_api (window);

  GST_GL_WINDOW_UNLOCK (window);

  return ret;
}
