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

#include "../gstgl_fwd.h"
#include <gst/gl/gstglcontext.h>

#include "gstglwindow_android_egl.h"

#define GST_CAT_DEFAULT gst_gl_window_debug

#define gst_gl_window_android_egl_parent_class parent_class
G_DEFINE_TYPE (GstGLWindowAndroidEGL, gst_gl_window_android_egl,
    GST_GL_TYPE_WINDOW);

static guintptr gst_gl_window_android_egl_get_gl_context (GstGLWindow * window);
static gboolean gst_gl_window_android_egl_activate (GstGLWindow * window,
    gboolean activate);
static void gst_gl_window_android_egl_set_window_handle (GstGLWindow * window,
    guintptr handle);
static void gst_gl_window_android_egl_draw (GstGLWindow * window, guint width,
    guint height);
static void gst_gl_window_android_egl_run (GstGLWindow * window);
static void gst_gl_window_android_egl_quit (GstGLWindow * window,
    GstGLWindowCB callback, gpointer data);
static void gst_gl_window_android_egl_send_message (GstGLWindow * window,
    GstGLWindowCB callback, gpointer data);
static void gst_gl_window_android_egl_destroy_context (GstGLWindowAndroidEGL *
    window_egl);
static gboolean gst_gl_window_android_egl_create_context (GstGLWindow
    * window, GstGLAPI gl_api, guintptr external_gl_context, GError ** error);
static GstGLAPI gst_gl_window_android_egl_get_gl_api (GstGLWindow * window);
static gpointer gst_gl_window_android_egl_get_proc_address (GstGLWindow *
    window, const gchar * name);
static void gst_gl_window_android_egl_close (GstGLWindow * window);

static void
gst_gl_window_android_egl_class_init (GstGLWindowAndroidEGLClass * klass)
{
  GstGLWindowClass *window_class = (GstGLWindowClass *) klass;

  window_class->create_context =
      GST_DEBUG_FUNCPTR (gst_gl_window_android_egl_create_context);
  window_class->get_gl_context =
      GST_DEBUG_FUNCPTR (gst_gl_window_android_egl_get_gl_context);
  window_class->activate =
      GST_DEBUG_FUNCPTR (gst_gl_window_android_egl_activate);
  window_class->set_window_handle =
      GST_DEBUG_FUNCPTR (gst_gl_window_android_egl_set_window_handle);
  window_class->draw_unlocked =
      GST_DEBUG_FUNCPTR (gst_gl_window_android_egl_draw);
  window_class->draw = GST_DEBUG_FUNCPTR (gst_gl_window_android_egl_draw);
  window_class->run = GST_DEBUG_FUNCPTR (gst_gl_window_android_egl_run);
  window_class->quit = GST_DEBUG_FUNCPTR (gst_gl_window_android_egl_quit);
  window_class->send_message =
      GST_DEBUG_FUNCPTR (gst_gl_window_android_egl_send_message);
  window_class->get_gl_api =
      GST_DEBUG_FUNCPTR (gst_gl_window_android_egl_get_gl_api);
  window_class->get_proc_address =
      GST_DEBUG_FUNCPTR (gst_gl_window_android_egl_get_proc_address);
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

  gst_gl_window_set_need_lock (GST_GL_WINDOW (window), FALSE);

  return window;
}

static void
gst_gl_window_android_egl_close (GstGLWindow * window)
{
  GstGLWindowAndroidEGL *window_egl;

  window_egl = GST_GL_WINDOW_ANDROID_EGL (window);

  gst_gl_window_android_egl_destroy_context (window_egl);
}

gboolean
gst_gl_window_android_egl_create_context (GstGLWindow * window,
    GstGLAPI gl_api, guintptr external_gl_context, GError ** error)
{
  GstGLWindowAndroidEGL *window_egl = GST_GL_WINDOW_ANDROID_EGL (window);

  window_egl->main_context = g_main_context_new ();
  window_egl->loop = g_main_loop_new (window_egl->main_context, FALSE);

  window_egl->egl =
      gst_gl_egl_create_context (eglGetDisplay (EGL_DEFAULT_DISPLAY),
      window_egl->native_window, gl_api, external_gl_context, error);
  if (!window_egl->egl)
    goto failure;

  return TRUE;

failure:
  return FALSE;
}

static void
gst_gl_window_android_egl_destroy_context (GstGLWindowAndroidEGL * window_egl)
{
  gst_gl_egl_activate (window_egl->egl, FALSE);
  gst_gl_egl_destroy_context (window_egl->egl);
  window_egl->egl = NULL;

  g_main_loop_unref (window_egl->loop);
  g_main_context_unref (window_egl->main_context);
}

static gboolean
gst_gl_window_android_egl_activate (GstGLWindow * window, gboolean activate)
{
  GstGLWindowAndroidEGL *window_egl;

  window_egl = GST_GL_WINDOW_ANDROID_EGL (window);

  return gst_gl_egl_activate (window_egl->egl, activate);
}

static guintptr
gst_gl_window_android_egl_get_gl_context (GstGLWindow * window)
{
  GstGLWindowAndroidEGL *window_egl;

  window_egl = GST_GL_WINDOW_ANDROID_EGL (window);

  return gst_gl_egl_get_gl_context (window_egl->egl);
}

static GstGLAPI
gst_gl_window_android_egl_get_gl_api (GstGLWindow * window)
{
  GstGLWindowAndroidEGL *window_egl;

  window_egl = GST_GL_WINDOW_ANDROID_EGL (window);

  return gst_gl_egl_get_gl_api (window_egl->egl);
}

static void
gst_gl_window_android_egl_swap_buffers (GstGLWindow * window)
{
  GstGLWindowAndroidEGL *window_egl = GST_GL_WINDOW_ANDROID_EGL (window);

  gst_gl_egl_swap_buffers (window_egl->egl);
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
gst_gl_window_android_egl_quit (GstGLWindow * window, GstGLWindowCB callback,
    gpointer data)
{
  GstGLWindowAndroidEGL *window_egl;

  window_egl = GST_GL_WINDOW_ANDROID_EGL (window);

  if (callback)
    gst_gl_window_android_egl_send_message (window, callback, data);

  GST_LOG ("sending quit");

  g_main_loop_quit (window_egl->loop);

  GST_LOG ("quit sent");
}

typedef struct _GstGLMessage
{
  GMutex lock;
  GCond cond;
  gboolean fired;

  GstGLWindowCB callback;
  gpointer data;
} GstGLMessage;

static gboolean
_run_message (GstGLMessage * message)
{
  g_mutex_lock (&message->lock);

  if (message->callback)
    message->callback (message->data);

  message->fired = TRUE;
  g_cond_signal (&message->cond);
  g_mutex_unlock (&message->lock);

  return FALSE;
}

static void
gst_gl_window_android_egl_send_message (GstGLWindow * window,
    GstGLWindowCB callback, gpointer data)
{
  GstGLWindowAndroidEGL *window_egl;
  GstGLMessage message;

  window_egl = GST_GL_WINDOW_ANDROID_EGL (window);
  message.callback = callback;
  message.data = data;
  message.fired = FALSE;
  g_mutex_init (&message.lock);
  g_cond_init (&message.cond);

  g_main_context_invoke (window_egl->main_context, (GSourceFunc) _run_message,
      &message);

  g_mutex_lock (&message.lock);

  while (!message.fired)
    g_cond_wait (&message.cond, &message.lock);
  g_mutex_unlock (&message.lock);
}

static void
gst_gl_window_android_egl_set_window_handle (GstGLWindow * window,
    guintptr handle)
{
  GstGLWindowAndroidEGL *window_egl = GST_GL_WINDOW_ANDROID_EGL (window);

  window_egl->native_window = (EGLNativeWindowType) handle;
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

  if (window->draw)
    window->draw (window->draw_data);

  gst_gl_window_android_egl_swap_buffers (window);
}

static void
gst_gl_window_android_egl_draw (GstGLWindow * window, guint width, guint height)
{
  struct draw draw_data;

  draw_data.window = GST_GL_WINDOW_ANDROID_EGL (window);
  draw_data.width = width;
  draw_data.height = height;

  gst_gl_window_send_message (window, (GstGLWindowCB) draw_cb, &draw_data);
}

static gpointer
gst_gl_window_android_egl_get_proc_address (GstGLWindow * window,
    const gchar * name)
{
  GstGLContext *context = NULL;
  GstGLWindowAndroidEGL *window_egl;
  gpointer result;

  window_egl = GST_GL_WINDOW_ANDROID_EGL (window);

  if (!(result = gst_gl_egl_get_proc_address (window_egl->egl, name))) {
    result = gst_gl_context_default_get_proc_address (context, name);
  }

  return result;
}
