/*
 * GStreamer
 * Copyright (C) 2014 Sebastian Dröge <sebastian@centricular.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it un der the terms of the GNU Library General Public
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

#import <OpenGLES/EAGL.h>
#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>

#include "gstglwindow_eagl.h"
#include "gstglcontext_eagl.h"

#define GST_GL_WINDOW_EAGL_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_GL_TYPE_WINDOW_EAGL, GstGLWindowEaglPrivate))

#define GST_CAT_DEFAULT gst_gl_window_eagl_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_GET (GST_CAT_DEFAULT, "glwindow");
#define gst_gl_window_eagl_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstGLWindowEagl, gst_gl_window_eagl,
    GST_GL_TYPE_WINDOW, DEBUG_INIT);

static guintptr gst_gl_window_eagl_get_display (GstGLWindow * window);
static guintptr gst_gl_window_eagl_get_window_handle (GstGLWindow * window);
static void gst_gl_window_eagl_set_window_handle (GstGLWindow * window,
    guintptr handle);
static void gst_gl_window_eagl_draw (GstGLWindow * window, guint width,
    guint height);
static void gst_gl_window_eagl_run (GstGLWindow * window);
static void gst_gl_window_eagl_quit (GstGLWindow * window);
static void gst_gl_window_eagl_send_message_async (GstGLWindow * window,
    GstGLWindowCB callback, gpointer data, GDestroyNotify destroy);
static gboolean gst_gl_window_eagl_open (GstGLWindow * window, GError ** error);
static void gst_gl_window_eagl_close (GstGLWindow * window);

struct _GstGLWindowEaglPrivate
{
  UIView *view;
  gint window_width, window_height;

  GMainContext *main_context;
  GMainLoop *loop;
};

static void
gst_gl_window_eagl_class_init (GstGLWindowEaglClass * klass)
{
  GstGLWindowClass *window_class;

  window_class = (GstGLWindowClass *) klass;

  g_type_class_add_private (klass, sizeof (GstGLWindowEaglPrivate));

  window_class->get_display =
      GST_DEBUG_FUNCPTR (gst_gl_window_eagl_get_display);
  window_class->get_window_handle =
      GST_DEBUG_FUNCPTR (gst_gl_window_eagl_get_window_handle);
  window_class->set_window_handle =
      GST_DEBUG_FUNCPTR (gst_gl_window_eagl_set_window_handle);
  window_class->draw_unlocked = GST_DEBUG_FUNCPTR (gst_gl_window_eagl_draw);
  window_class->draw = GST_DEBUG_FUNCPTR (gst_gl_window_eagl_draw);
  window_class->run = GST_DEBUG_FUNCPTR (gst_gl_window_eagl_run);
  window_class->quit = GST_DEBUG_FUNCPTR (gst_gl_window_eagl_quit);
  window_class->send_message_async =
      GST_DEBUG_FUNCPTR (gst_gl_window_eagl_send_message_async);
  window_class->open = GST_DEBUG_FUNCPTR (gst_gl_window_eagl_open);
  window_class->close = GST_DEBUG_FUNCPTR (gst_gl_window_eagl_close);
}

static void
gst_gl_window_eagl_init (GstGLWindowEagl * window)
{
  window->priv = GST_GL_WINDOW_EAGL_GET_PRIVATE (window);
}

/* Must be called in the gl thread */
GstGLWindowEagl *
gst_gl_window_eagl_new (void)
{
  GstGLWindowEagl *window = g_object_new (GST_GL_TYPE_WINDOW_EAGL, NULL);

  return window;
}

static guintptr
gst_gl_window_eagl_get_display (GstGLWindow * window)
{
  return 0;
}

static guintptr
gst_gl_window_eagl_get_window_handle (GstGLWindow * window)
{
  return (guintptr) GST_GL_WINDOW_EAGL (window)->priv->view;
}

static void
gst_gl_window_eagl_set_window_handle (GstGLWindow * window, guintptr handle)
{
  GstGLWindowEagl *window_eagl;

  window_eagl = GST_GL_WINDOW_EAGL (window);

  window_eagl->priv->view = (UIView *) handle;
}

static gboolean
gst_gl_window_eagl_open (GstGLWindow * window, GError ** error)
{
  GstGLWindowEagl *window_eagl;

  window_eagl = GST_GL_WINDOW_EAGL (window);

  window_eagl->priv->main_context = g_main_context_new ();
  window_eagl->priv->loop =
      g_main_loop_new (window_eagl->priv->main_context, FALSE);

  return TRUE;
}

static void
gst_gl_window_eagl_close (GstGLWindow * window)
{
  GstGLWindowEagl *window_eagl;

  window_eagl = GST_GL_WINDOW_EAGL (window);

  g_main_loop_unref (window_eagl->priv->loop);
  g_main_context_unref (window_eagl->priv->main_context);
}

static void
gst_gl_window_eagl_run (GstGLWindow * window)
{
  GstGLWindowEagl *window_eagl;

  window_eagl = GST_GL_WINDOW_EAGL (window);

  GST_LOG ("starting main loop");
  g_main_loop_run (window_eagl->priv->loop);
  GST_LOG ("exiting main loop");
}

static void
gst_gl_window_eagl_quit (GstGLWindow * window)
{
  GstGLWindowEagl *window_eagl;

  window_eagl = GST_GL_WINDOW_EAGL (window);

  GST_LOG ("sending quit");

  g_main_loop_quit (window_eagl->priv->loop);

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
gst_gl_window_eagl_send_message_async (GstGLWindow * window,
    GstGLWindowCB callback, gpointer data, GDestroyNotify destroy)
{
  GstGLWindowEagl *window_eagl;
  GstGLMessage *message;

  window_eagl = GST_GL_WINDOW_EAGL (window);
  message = g_slice_new (GstGLMessage);

  message->callback = callback;
  message->data = data;
  message->destroy = destroy;

  g_main_context_invoke (window_eagl->priv->main_context,
      (GSourceFunc) _run_message, message);
}

struct draw
{
  GstGLWindowEagl *window;
  guint width, height;
};

static void
draw_cb (gpointer data)
{
  struct draw *draw_data = data;
  GstGLWindowEagl *window_eagl = draw_data->window;
  GstGLWindow *window = GST_GL_WINDOW (window_eagl);
  GstGLContext *context = gst_gl_window_get_context (window);
  GstGLContextEagl *eagl_context = GST_GL_CONTEXT_EAGL (context);
  GstGLContextClass *context_class = GST_GL_CONTEXT_GET_CLASS (context);

  if (window_eagl->priv->view) {
    CGSize size;
    CAEAGLLayer *eagl_layer;

    eagl_layer = (CAEAGLLayer *)[window_eagl->priv->view layer];
    size = eagl_layer.frame.size;

    if (window_eagl->priv->window_width != size.width || window_eagl->priv->window_height != size.height) {
      window_eagl->priv->window_width = size.width;
      window_eagl->priv->window_height = size.height;

      if (window->resize)
        window->resize (window->resize_data, size.width, size.height);
    }
  }

  gst_gl_context_eagl_prepare_draw (eagl_context);

  if (window->draw)
    window->draw (window->draw_data);

  context_class->swap_buffers (context);

  gst_gl_context_eagl_finish_draw (eagl_context);

  gst_object_unref (context);
}

static void
gst_gl_window_eagl_draw (GstGLWindow * window, guint width, guint height)
{
  struct draw draw_data;

  draw_data.window = GST_GL_WINDOW_EAGL (window);
  draw_data.width = width;
  draw_data.height = height;

  gst_gl_window_send_message (window, (GstGLWindowCB) draw_cb, &draw_data);
}
