/*
 * GStreamer
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
 * Copyright (C) 2012 Matthew Waters <ystreet00@gmail.com>
 * Copyright (C) 2013 Sebastian Dröge <slomo@circular-chaos.org>
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

/* TODO: - Window resize handling
 *       - Event handling input event handling
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#include <gst/gl/egl/gstglcontext_egl.h>
#include "gstglwindow_android_egl.h"

#define GST_CAT_DEFAULT gst_gl_window_debug

#define gst_gl_window_android_egl_parent_class parent_class
G_DEFINE_TYPE (GstGLWindowAndroidEGL, gst_gl_window_android_egl,
    GST_GL_TYPE_WINDOW);

static guintptr gst_gl_window_android_egl_get_display (GstGLWindow * window);
static guintptr gst_gl_window_android_egl_get_window_handle (GstGLWindow *
    window);
static void gst_gl_window_android_egl_set_window_handle (GstGLWindow * window,
    guintptr handle);
static void gst_gl_window_android_egl_draw (GstGLWindow * window, guint width,
    guint height);
static void gst_gl_window_android_egl_run (GstGLWindow * window);
static void gst_gl_window_android_egl_quit (GstGLWindow * window);
static void gst_gl_window_android_egl_send_message_async (GstGLWindow * window,
    GstGLWindowCB callback, gpointer data, GDestroyNotify destroy);
static gboolean gst_gl_window_android_egl_open (GstGLWindow * window,
    GError ** error);
static void gst_gl_window_android_egl_close (GstGLWindow * window);

static void
gst_gl_window_android_egl_class_init (GstGLWindowAndroidEGLClass * klass)
{
  GstGLWindowClass *window_class = (GstGLWindowClass *) klass;

  window_class->get_display =
      GST_DEBUG_FUNCPTR (gst_gl_window_android_egl_get_display);
  window_class->get_window_handle =
      GST_DEBUG_FUNCPTR (gst_gl_window_android_egl_get_window_handle);
  window_class->set_window_handle =
      GST_DEBUG_FUNCPTR (gst_gl_window_android_egl_set_window_handle);
  window_class->draw_unlocked =
      GST_DEBUG_FUNCPTR (gst_gl_window_android_egl_draw);
  window_class->draw = GST_DEBUG_FUNCPTR (gst_gl_window_android_egl_draw);
  window_class->run = GST_DEBUG_FUNCPTR (gst_gl_window_android_egl_run);
  window_class->quit = GST_DEBUG_FUNCPTR (gst_gl_window_android_egl_quit);
  window_class->send_message_async =
      GST_DEBUG_FUNCPTR (gst_gl_window_android_egl_send_message_async);
  window_class->open = GST_DEBUG_FUNCPTR (gst_gl_window_android_egl_open);
  window_class->close = GST_DEBUG_FUNCPTR (gst_gl_window_android_egl_close);
}

static void
gst_gl_window_android_egl_init (GstGLWindowAndroidEGL * window)
{
}

/* Must be called in the gl thread */
GstGLWindowAndroidEGL *
gst_gl_window_android_egl_new (void)
{
  GstGLWindowAndroidEGL *window;

  GST_DEBUG ("creating Android EGL window");

  window = g_object_new (GST_GL_TYPE_WINDOW_ANDROID_EGL, NULL);

  return window;
}

static gboolean
gst_gl_window_android_egl_open (GstGLWindow * window, GError ** error)
{
  GstGLWindowAndroidEGL *window_egl;

  window_egl = GST_GL_WINDOW_ANDROID_EGL (window);

  window_egl->main_context = g_main_context_new ();
  window_egl->loop = g_main_loop_new (window_egl->main_context, FALSE);

  return TRUE;
}

static void
gst_gl_window_android_egl_close (GstGLWindow * window)
{
  GstGLWindowAndroidEGL *window_egl;

  window_egl = GST_GL_WINDOW_ANDROID_EGL (window);

  g_main_loop_unref (window_egl->loop);
  g_main_context_unref (window_egl->main_context);
}

static void
gst_gl_window_android_egl_run (GstGLWindow * window)
{
  GstGLWindowAndroidEGL *window_egl;

  window_egl = GST_GL_WINDOW_ANDROID_EGL (window);

  GST_LOG ("starting main loop");
  g_main_loop_run (window_egl->loop);
  GST_LOG ("exiting main loop");
}

static void
gst_gl_window_android_egl_quit (GstGLWindow * window)
{
  GstGLWindowAndroidEGL *window_egl;

  window_egl = GST_GL_WINDOW_ANDROID_EGL (window);

  GST_LOG ("sending quit");

  g_main_loop_quit (window_egl->loop);

  GST_LOG ("quit sent");
}

typedef struct _GstGLMessage
{
  GstGLWindowCB callback;
  gpointer data;
  GDestroyNotify destroy;
} GstGLMessage;

static gboolean
_run_message (GstGLMessage * message)
{
  if (message->callback)
    message->callback (message->data);

  if (message->destroy)
    message->destroy (message->data);

  g_slice_free (GstGLMessage, message);

  return FALSE;
}

static void
gst_gl_window_android_egl_send_message_async (GstGLWindow * window,
    GstGLWindowCB callback, gpointer data, GDestroyNotify destroy)
{
  GstGLWindowAndroidEGL *window_egl;
  GstGLMessage *message;

  window_egl = GST_GL_WINDOW_ANDROID_EGL (window);
  message = g_slice_new (GstGLMessage);

  message->callback = callback;
  message->data = data;
  message->destroy = destroy;

  g_main_context_invoke (window_egl->main_context, (GSourceFunc) _run_message,
      message);
}

static void
gst_gl_window_android_egl_set_window_handle (GstGLWindow * window,
    guintptr handle)
{
  GstGLWindowAndroidEGL *window_egl = GST_GL_WINDOW_ANDROID_EGL (window);

  window_egl->native_window = (EGLNativeWindowType) handle;
}

static guintptr
gst_gl_window_android_egl_get_window_handle (GstGLWindow * window)
{
  GstGLWindowAndroidEGL *window_egl = GST_GL_WINDOW_ANDROID_EGL (window);

  return (guintptr) window_egl->native_window;
}

struct draw
{
  GstGLWindowAndroidEGL *window;
  guint width, height;
};

static void
draw_cb (gpointer data)
{
  struct draw *draw_data = data;
  GstGLWindowAndroidEGL *window_egl = draw_data->window;
  GstGLWindow *window = GST_GL_WINDOW (window_egl);
  GstGLContext *context = gst_gl_window_get_context (window);
  GstGLContextEGL *context_egl = GST_GL_CONTEXT_EGL (context);
  GstGLContextClass *context_class = GST_GL_CONTEXT_GET_CLASS (context);

  if (context_egl->egl_surface) {
    gint width, height;

    if (eglQuerySurface (context_egl->egl_display,
            context_egl->egl_surface, EGL_WIDTH, &width) &&
        eglQuerySurface (context_egl->egl_display,
            context_egl->egl_surface, EGL_HEIGHT, &height)
        && (width != window_egl->window_width
            || height != window_egl->window_height)) {
      window_egl->window_width = width;
      window_egl->window_height = height;

      if (window->resize)
        window->resize (window->resize_data, width, height);
    }
  }

  if (window->draw)
    window->draw (window->draw_data);

  context_class->swap_buffers (context);

  gst_object_unref (context);
}

static void
gst_gl_window_android_egl_draw (GstGLWindow * window, guint width, guint height)
{
  struct draw draw_data;
  GstGLWindowAndroidEGL *window_egl = GST_GL_WINDOW_ANDROID_EGL (window);

  draw_data.window = window_egl;
  draw_data.width = width;
  draw_data.height = height;

  gst_gl_window_send_message (window, (GstGLWindowCB) draw_cb, &draw_data);
}

static guintptr
gst_gl_window_android_egl_get_display (GstGLWindow * window)
{
  return 0;
}
