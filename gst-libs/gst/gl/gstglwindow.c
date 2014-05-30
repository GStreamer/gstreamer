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

/**
 * SECTION:gstglwindow
 * @short_description: window/surface abstraction
 * @title: GstGLWindow
 * @see_also: #GstGLContext, #GstGLDisplay
 *
 * GstGLWindow represents a window that elements can render into.  A window can
 * either be a user visible window (onscreen) or hidden (offscreen).
 */

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <gmodule.h>
#include <stdio.h>

#include "gl.h"
#include "gstglwindow.h"

/* FIXME make this work with windowless contexts */

#if GST_GL_HAVE_WINDOW_X11
#include "x11/gstglwindow_x11.h"
#endif
#if GST_GL_HAVE_WINDOW_WIN32
#include "win32/gstglwindow_win32.h"
#endif
#if GST_GL_HAVE_WINDOW_COCOA
#include "cocoa/gstglwindow_cocoa.h"
#endif
#if GST_GL_HAVE_WINDOW_WAYLAND
#include "wayland/gstglwindow_wayland_egl.h"
#endif
#if GST_GL_HAVE_WINDOW_ANDROID
#include "android/gstglwindow_android_egl.h"
#endif
#if GST_GL_HAVE_WINDOW_EAGL
#include "eagl/gstglwindow_eagl.h"
#endif
#if GST_GL_HAVE_WINDOW_DISPMANX
#include "dispmanx/gstglwindow_dispmanx_egl.h"
#endif

#define USING_OPENGL(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL, 1, 0))
#define USING_OPENGL3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL3, 3, 1))
#define USING_GLES(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES, 1, 0))
#define USING_GLES2(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 2, 0))
#define USING_GLES3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 3, 0))

#define GST_CAT_DEFAULT gst_gl_window_debug
GST_DEBUG_CATEGORY (GST_CAT_DEFAULT);

#define gst_gl_window_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE (GstGLWindow, gst_gl_window, GST_TYPE_OBJECT);

#define GST_GL_WINDOW_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_GL_TYPE_WINDOW, GstGLWindowPrivate))

static void gst_gl_window_default_send_message (GstGLWindow * window,
    GstGLWindowCB callback, gpointer data);

struct _GstGLWindowPrivate
{
  GThread *gl_thread;

  gboolean alive;
};

static void gst_gl_window_finalize (GObject * object);

typedef struct _GstGLDummyWindow
{
  GstGLWindow parent;

  guintptr handle;

  GMainContext *main_context;
  GMainLoop *loop;
} GstGLDummyWindow;

typedef struct _GstGLDummyWindowCass
{
  GstGLWindowClass parent;
} GstGLDummyWindowClass;

GstGLDummyWindow *gst_gl_dummy_window_new (void);

GQuark
gst_gl_window_error_quark (void)
{
  return g_quark_from_static_string ("gst-gl-window-error-quark");
}

static void
gst_gl_window_init (GstGLWindow * window)
{
  window->priv = GST_GL_WINDOW_GET_PRIVATE (window);

  g_mutex_init (&window->lock);
  window->is_drawing = FALSE;

  g_weak_ref_init (&window->context_ref, NULL);
}

static void
gst_gl_window_class_init (GstGLWindowClass * klass)
{
  g_type_class_add_private (klass, sizeof (GstGLWindowPrivate));

  klass->send_message = GST_DEBUG_FUNCPTR (gst_gl_window_default_send_message);

  G_OBJECT_CLASS (klass)->finalize = gst_gl_window_finalize;
}

/**
 * gst_gl_window_new:
 * @display: a #GstGLDisplay
 *
 * Returns: (transfer full): a new #GstGLWindow using @display's connection
 */
GstGLWindow *
gst_gl_window_new (GstGLDisplay * display)
{
  GstGLWindow *window = NULL;
  const gchar *user_choice;
  static volatile gsize _init = 0;

  g_return_val_if_fail (display != NULL, NULL);

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_INIT (gst_gl_window_debug, "glwindow", 0,
        "glwindow element");
    g_once_init_leave (&_init, 1);
  }

  user_choice = g_getenv ("GST_GL_WINDOW");
  GST_INFO ("creating a window, user choice:%s", user_choice);

#if GST_GL_HAVE_WINDOW_COCOA
  if (!window && (!user_choice || g_strstr_len (user_choice, 5, "cocoa")))
    window = GST_GL_WINDOW (gst_gl_window_cocoa_new ());
#endif
#if GST_GL_HAVE_WINDOW_X11
  if (!window && (!user_choice || g_strstr_len (user_choice, 3, "x11")))
    window = GST_GL_WINDOW (gst_gl_window_x11_new (display));
#endif
#if GST_GL_HAVE_WINDOW_WIN32
  if (!window && (!user_choice || g_strstr_len (user_choice, 5, "win32")))
    window = GST_GL_WINDOW (gst_gl_window_win32_new ());
#endif
#if GST_GL_HAVE_WINDOW_WAYLAND
  if (!window && (!user_choice || g_strstr_len (user_choice, 7, "wayland")))
    window = GST_GL_WINDOW (gst_gl_window_wayland_egl_new ());
#endif
#if GST_GL_HAVE_WINDOW_DISPMANX
  if (!window && (!user_choice || g_strstr_len (user_choice, 8, "dispmanx")))
    window = GST_GL_WINDOW (gst_gl_window_dispmanx_egl_new ());
#endif
#if GST_GL_HAVE_WINDOW_ANDROID
  if (!window && (!user_choice || g_strstr_len (user_choice, 7, "android")))
    window = GST_GL_WINDOW (gst_gl_window_android_egl_new ());
#endif
#if GST_GL_HAVE_WINDOW_EAGL
  if (!window && (!user_choice || g_strstr_len (user_choice, 4, "eagl")))
    window = GST_GL_WINDOW (gst_gl_window_eagl_new ());
#endif
  if (!window) {
    /* subclass returned a NULL window */
    GST_WARNING ("Could not create window. user specified %s, creating dummy"
        " window", user_choice ? user_choice : "(null)");

    window = GST_GL_WINDOW (gst_gl_dummy_window_new ());
  }

  window->display = gst_object_ref (display);

  return window;
}

static void
gst_gl_window_finalize (GObject * object)
{
  GstGLWindow *window = GST_GL_WINDOW (object);

  g_weak_ref_clear (&window->context_ref);

  g_mutex_clear (&window->lock);
  gst_object_unref (window->display);

  G_OBJECT_CLASS (gst_gl_window_parent_class)->finalize (object);
}

/**
 * gst_gl_window_set_window_handle:
 * @window: a #GstGLWindow
 * @handle: handle to the window
 *
 * Sets the window that this @window should render into.  Some implementations
 * require this to be called with a valid handle before drawing can commence.
 */
void
gst_gl_window_set_window_handle (GstGLWindow * window, guintptr handle)
{
  GstGLWindowClass *window_class;

  g_return_if_fail (GST_GL_IS_WINDOW (window));
  g_return_if_fail (handle != 0);
  window_class = GST_GL_WINDOW_GET_CLASS (window);
  g_return_if_fail (window_class->set_window_handle != NULL);

  window_class->set_window_handle (window, handle);
}

/**
 * gst_gl_window_draw_unlocked:
 * @window: a #GstGLWindow
 * @width: requested width of the window
 * @height: requested height of the window
 *
 * Redraw the window contents.  Implementations should invoke the draw callback.
 */
void
gst_gl_window_draw_unlocked (GstGLWindow * window, guint width, guint height)
{
  GstGLWindowClass *window_class;

  g_return_if_fail (GST_GL_IS_WINDOW (window));
  window_class = GST_GL_WINDOW_GET_CLASS (window);
  g_return_if_fail (window_class->draw_unlocked != NULL);

  window_class->draw_unlocked (window, width, height);
}

/**
 * gst_gl_window_draw:
 * @window: a #GstGLWindow
 * @width: requested width of the window
 * @height: requested height of the window
 *
 * Redraw the window contents.  Implementations should invoke the draw callback.
 */
void
gst_gl_window_draw (GstGLWindow * window, guint width, guint height)
{
  GstGLWindowClass *window_class;

  g_return_if_fail (GST_GL_IS_WINDOW (window));
  window_class = GST_GL_WINDOW_GET_CLASS (window);
  g_return_if_fail (window_class->draw != NULL);

  /* avoid to overload the drawer */
  if (window->is_drawing) {
    return;
  }

  window_class->draw (window, width, height);
}

/**
 * gst_gl_window_run:
 * @window: a #GstGLWindow
 *
 * Start the execution of the runloop.
 */
void
gst_gl_window_run (GstGLWindow * window)
{
  GstGLWindowClass *window_class;

  g_return_if_fail (GST_GL_IS_WINDOW (window));
  window_class = GST_GL_WINDOW_GET_CLASS (window);
  g_return_if_fail (window_class->run != NULL);

  window->priv->alive = TRUE;
  window_class->run (window);
}

/**
 * gst_gl_window_quit:
 * @window: a #GstGLWindow
 *
 * Quit the runloop's execution.
 */
void
gst_gl_window_quit (GstGLWindow * window)
{
  GstGLWindowClass *window_class;

  g_return_if_fail (GST_GL_IS_WINDOW (window));
  window_class = GST_GL_WINDOW_GET_CLASS (window);
  g_return_if_fail (window_class->quit != NULL);

  GST_GL_WINDOW_LOCK (window);

  window->priv->alive = FALSE;

  window_class->quit (window);

  GST_INFO ("quit sent to gl window loop");

  GST_GL_WINDOW_UNLOCK (window);
}

typedef struct _GstGLSyncMessage
{
  GMutex lock;
  GCond cond;
  gboolean fired;

  GstGLWindowCB callback;
  gpointer data;
} GstGLSyncMessage;

static void
_run_message_sync (GstGLSyncMessage * message)
{
  g_mutex_lock (&message->lock);

  if (message->callback)
    message->callback (message->data);

  message->fired = TRUE;
  g_cond_signal (&message->cond);
  g_mutex_unlock (&message->lock);
}

void
gst_gl_window_default_send_message (GstGLWindow * window,
    GstGLWindowCB callback, gpointer data)
{
  GstGLSyncMessage message;

  message.callback = callback;
  message.data = data;
  message.fired = FALSE;
  g_mutex_init (&message.lock);
  g_cond_init (&message.cond);

  gst_gl_window_send_message_async (window, (GstGLWindowCB) _run_message_sync,
      &message, NULL);

  g_mutex_lock (&message.lock);

  /* block until opengl calls have been executed in the gl thread */
  while (!message.fired)
    g_cond_wait (&message.cond, &message.lock);
  g_mutex_unlock (&message.lock);

  g_mutex_clear (&message.lock);
  g_cond_clear (&message.cond);
}

/**
 * gst_gl_window_send_message:
 * @window: a #GstGLWindow
 * @callback: (scope async): function to invoke
 * @data: (closure): data to invoke @callback with
 *
 * Invoke @callback with data on the window thread.  @callback is guarenteed to
 * have executed when this function returns.
 */
void
gst_gl_window_send_message (GstGLWindow * window, GstGLWindowCB callback,
    gpointer data)
{
  GstGLWindowClass *window_class;

  g_return_if_fail (GST_GL_IS_WINDOW (window));
  g_return_if_fail (callback != NULL);
  window_class = GST_GL_WINDOW_GET_CLASS (window);
  g_return_if_fail (window_class->send_message != NULL);

  window_class->send_message (window, callback, data);
}

/**
 * gst_gl_window_send_message_async:
 * @window: a #GstGLWindow
 * @callback: (scope async): function to invoke
 * @data: (closure): data to invoke @callback with
 * @destroy: (destroy): called when @data is not needed anymore
 *
 * Invoke @callback with @data on the window thread.  The callback may not
 * have been executed when this function returns.
 */
void
gst_gl_window_send_message_async (GstGLWindow * window, GstGLWindowCB callback,
    gpointer data, GDestroyNotify destroy)
{
  GstGLWindowClass *window_class;

  g_return_if_fail (GST_GL_IS_WINDOW (window));
  g_return_if_fail (callback != NULL);
  window_class = GST_GL_WINDOW_GET_CLASS (window);
  g_return_if_fail (window_class->send_message_async != NULL);

  window_class->send_message_async (window, callback, data, destroy);
}

/**
 * gst_gl_window_set_draw_callback:
 * @window: a #GstGLWindow
 * @callback: (scope notified): function to invoke
 * @data: (closure): data to invoke @callback with
 * @destroy_notify: (destroy): called when @data is not needed any more
 *
 * Sets the draw callback called everytime gst_gl_window_draw() is called
 */
void
gst_gl_window_set_draw_callback (GstGLWindow * window, GstGLWindowCB callback,
    gpointer data, GDestroyNotify destroy_notify)
{
  g_return_if_fail (GST_GL_IS_WINDOW (window));

  GST_GL_WINDOW_LOCK (window);

  if (window->draw_notify)
    window->draw_notify (window->draw_data);

  window->draw = callback;
  window->draw_data = data;
  window->draw_notify = destroy_notify;

  GST_GL_WINDOW_UNLOCK (window);
}

/**
 * gst_gl_window_set_resize_callback:
 * @window: a #GstGLWindow
 * @callback: (scope notified): function to invoke
 * @data: (closure): data to invoke @callback with
 * @destroy_notify: (destroy): called when @data is not needed any more
 *
 * Sets the resize callback called everytime a resize of the window occurs.
 */
void
gst_gl_window_set_resize_callback (GstGLWindow * window,
    GstGLWindowResizeCB callback, gpointer data, GDestroyNotify destroy_notify)
{
  g_return_if_fail (GST_GL_IS_WINDOW (window));

  GST_GL_WINDOW_LOCK (window);

  if (window->resize_notify)
    window->resize_notify (window->resize_data);

  window->resize = callback;
  window->resize_data = data;
  window->resize_notify = destroy_notify;

  GST_GL_WINDOW_UNLOCK (window);
}

/**
 * gst_gl_window_set_close_callback:
 * @window: a #GstGLWindow
 * @callback: (scope notified): function to invoke
 * @data: (closure): data to invoke @callback with
 * @destroy_notify: (destroy): called when @data is not needed any more
 *
 * Sets the callback called when the window is about to close.
 */
void
gst_gl_window_set_close_callback (GstGLWindow * window, GstGLWindowCB callback,
    gpointer data, GDestroyNotify destroy_notify)
{
  g_return_if_fail (GST_GL_IS_WINDOW (window));

  GST_GL_WINDOW_LOCK (window);

  if (window->close_notify)
    window->close_notify (window->close_data);

  window->close = callback;
  window->close_data = data;
  window->close_notify = destroy_notify;

  GST_GL_WINDOW_UNLOCK (window);
}

/**
 * gst_gl_window_is_running:
 * @window: a #GstGLWindow
 *
 * Whether the runloop is running
 */
gboolean
gst_gl_window_is_running (GstGLWindow * window)
{
  return window->priv->alive;
}

/**
 * gst_gl_window_get_display:
 * @window: a #GstGLWindow
 *
 * Returns: the windowing system display handle for this @window
 */
guintptr
gst_gl_window_get_display (GstGLWindow * window)
{
  GstGLWindowClass *window_class;

  g_return_val_if_fail (GST_GL_IS_WINDOW (window), 0);
  window_class = GST_GL_WINDOW_GET_CLASS (window);
  g_return_val_if_fail (window_class->get_display != NULL, 0);

  return window_class->get_display (window);
}

/**
 * gst_gl_window_get_window_handle:
 * @window: a #GstGLWindow
 *
 * Returns: the window handle we are currently rendering into
 */
guintptr
gst_gl_window_get_window_handle (GstGLWindow * window)
{
  GstGLWindowClass *window_class;

  g_return_val_if_fail (GST_GL_IS_WINDOW (window), 0);
  window_class = GST_GL_WINDOW_GET_CLASS (window);
  g_return_val_if_fail (window_class->get_window_handle != NULL, 0);

  return window_class->get_window_handle (window);
}

/**
 * gst_gl_window_get_context:
 * @window: a #GstGLWindow
 *
 * Returns: (transfer full): the #GstGLContext associated with this @window
 */
GstGLContext *
gst_gl_window_get_context (GstGLWindow * window)
{
  g_return_val_if_fail (GST_GL_IS_WINDOW (window), NULL);

  return (GstGLContext *) g_weak_ref_get (&window->context_ref);
}

GType gst_gl_dummy_window_get_type (void);

G_DEFINE_TYPE (GstGLDummyWindow, gst_gl_dummy_window, GST_GL_TYPE_WINDOW);

static gboolean
gst_gl_dummy_window_open (GstGLWindow * window, GError ** error)
{
  GstGLDummyWindow *dummy = (GstGLDummyWindow *) window;

  dummy->main_context = g_main_context_new ();
  dummy->loop = g_main_loop_new (dummy->main_context, FALSE);

  return TRUE;
}

static void
gst_gl_dummy_window_close (GstGLWindow * window)
{
  GstGLDummyWindow *dummy = (GstGLDummyWindow *) window;

  g_main_loop_unref (dummy->loop);
  g_main_context_unref (dummy->main_context);
}

static void
gst_gl_dummy_window_quit (GstGLWindow * window)
{
  GstGLDummyWindow *dummy = (GstGLDummyWindow *) window;

  g_main_loop_quit (dummy->loop);
}

static void
gst_gl_dummy_window_run (GstGLWindow * window)
{
  GstGLDummyWindow *dummy = (GstGLDummyWindow *) window;

  g_main_loop_run (dummy->loop);
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
gst_gl_dummy_window_send_message_async (GstGLWindow * window,
    GstGLWindowCB callback, gpointer data, GDestroyNotify destroy)
{
  GstGLDummyWindow *dummy;
  GstGLMessage *message;

  dummy = (GstGLDummyWindow *) window;
  message = g_slice_new (GstGLMessage);

  message->callback = callback;
  message->data = data;
  message->destroy = destroy;

  g_main_context_invoke (dummy->main_context, (GSourceFunc) _run_message,
      message);
}

static void
gst_gl_dummy_window_set_window_handle (GstGLWindow * window, guintptr handle)
{
  GstGLDummyWindow *dummy = (GstGLDummyWindow *) window;

  dummy->handle = handle;
}

static guintptr
gst_gl_dummy_window_get_window_handle (GstGLWindow * window)
{
  GstGLDummyWindow *dummy = (GstGLDummyWindow *) window;

  return (guintptr) dummy->handle;
}

struct draw
{
  GstGLDummyWindow *window;
  guint width, height;
};

static void
draw_cb (gpointer data)
{
  struct draw *draw_data = data;
  GstGLDummyWindow *dummy = draw_data->window;
  GstGLWindow *window = GST_GL_WINDOW (dummy);
  GstGLContext *context = gst_gl_window_get_context (window);
  GstGLContextClass *context_class = GST_GL_CONTEXT_GET_CLASS (context);

  if (window->draw)
    window->draw (window->draw_data);

  context_class->swap_buffers (context);

  gst_object_unref (context);
}

static void
gst_gl_dummy_window_draw (GstGLWindow * window, guint width, guint height)
{
  struct draw draw_data;

  draw_data.window = (GstGLDummyWindow *) window;
  draw_data.width = width;
  draw_data.height = height;

  gst_gl_window_send_message (window, (GstGLWindowCB) draw_cb, &draw_data);
}

static guintptr
gst_gl_dummy_window_get_display (GstGLWindow * window)
{
  return 0;
}

static void
gst_gl_dummy_window_class_init (GstGLDummyWindowClass * klass)
{
  GstGLWindowClass *window_class = (GstGLWindowClass *) klass;

  window_class->get_display =
      GST_DEBUG_FUNCPTR (gst_gl_dummy_window_get_display);
  window_class->get_window_handle =
      GST_DEBUG_FUNCPTR (gst_gl_dummy_window_get_window_handle);
  window_class->set_window_handle =
      GST_DEBUG_FUNCPTR (gst_gl_dummy_window_set_window_handle);
  window_class->draw_unlocked = GST_DEBUG_FUNCPTR (gst_gl_dummy_window_draw);
  window_class->draw = GST_DEBUG_FUNCPTR (gst_gl_dummy_window_draw);
  window_class->run = GST_DEBUG_FUNCPTR (gst_gl_dummy_window_run);
  window_class->quit = GST_DEBUG_FUNCPTR (gst_gl_dummy_window_quit);
  window_class->send_message_async =
      GST_DEBUG_FUNCPTR (gst_gl_dummy_window_send_message_async);
  window_class->open = GST_DEBUG_FUNCPTR (gst_gl_dummy_window_open);
  window_class->close = GST_DEBUG_FUNCPTR (gst_gl_dummy_window_close);
}

static void
gst_gl_dummy_window_init (GstGLDummyWindow * dummy)
{
  dummy->handle = 0;
}

GstGLDummyWindow *
gst_gl_dummy_window_new (void)
{
  return g_object_new (gst_gl_dummy_window_get_type (), NULL);
}
