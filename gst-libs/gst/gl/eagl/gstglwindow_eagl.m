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
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_TYPE_GL_WINDOW_EAGL, GstGLWindowEaglPrivate))

#define GST_CAT_DEFAULT gst_gl_window_eagl_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_GET (GST_CAT_DEFAULT, "glwindow");
#define gst_gl_window_eagl_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstGLWindowEagl, gst_gl_window_eagl,
    GST_TYPE_GL_WINDOW, DEBUG_INIT);
static void gst_gl_window_eagl_finalize (GObject * object);

static guintptr gst_gl_window_eagl_get_display (GstGLWindow * window);
static guintptr gst_gl_window_eagl_get_window_handle (GstGLWindow * window);
static void gst_gl_window_eagl_set_window_handle (GstGLWindow * window,
    guintptr handle);
static void gst_gl_window_eagl_set_preferred_size (GstGLWindow * window,
    gint width, gint height);
static void gst_gl_window_eagl_draw (GstGLWindow * window);
static void gst_gl_window_eagl_send_message_async (GstGLWindow * window,
    GstGLWindowCB callback, gpointer data, GDestroyNotify destroy);

struct _GstGLWindowEaglPrivate
{
  gpointer view;
  gint window_width, window_height;
  gint preferred_width, preferred_height;
  gpointer gl_queue;
};

static void
gst_gl_window_eagl_class_init (GstGLWindowEaglClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstGLWindowClass *window_class = (GstGLWindowClass *) klass;

  g_type_class_add_private (klass, sizeof (GstGLWindowEaglPrivate));

  gobject_class->finalize = gst_gl_window_eagl_finalize;

  window_class->get_display =
      GST_DEBUG_FUNCPTR (gst_gl_window_eagl_get_display);
  window_class->get_window_handle =
      GST_DEBUG_FUNCPTR (gst_gl_window_eagl_get_window_handle);
  window_class->set_window_handle =
      GST_DEBUG_FUNCPTR (gst_gl_window_eagl_set_window_handle);
  window_class->draw = GST_DEBUG_FUNCPTR (gst_gl_window_eagl_draw);
  window_class->set_preferred_size =
      GST_DEBUG_FUNCPTR (gst_gl_window_eagl_set_preferred_size);
  window_class->send_message_async =
      GST_DEBUG_FUNCPTR (gst_gl_window_eagl_send_message_async);
}

static void
gst_gl_window_eagl_init (GstGLWindowEagl * window)
{
  window->priv = GST_GL_WINDOW_EAGL_GET_PRIVATE (window);
  window->priv->gl_queue =
      (__bridge_retained gpointer)dispatch_queue_create ("org.freedesktop.gstreamer.glwindow", NULL);
}

static void
gst_gl_window_eagl_finalize (GObject * object)
{
  GstGLWindowEagl *window = GST_GL_WINDOW_EAGL (object);
  CFRelease(window->priv->gl_queue);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* Must be called in the gl thread */
GstGLWindowEagl *
gst_gl_window_eagl_new (GstGLDisplay * display)
{
  /* there isn't an eagl display type */
  return g_object_new (GST_TYPE_GL_WINDOW_EAGL, NULL);
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
  GstGLContext *context;

  window_eagl = GST_GL_WINDOW_EAGL (window);
  context = gst_gl_window_get_context (window);

  window_eagl->priv->view = (gpointer)handle;
  GST_INFO_OBJECT (context, "handle set, updating layer");
  gst_gl_context_eagl_update_layer (context);

  gst_object_unref (context);
}

static void
gst_gl_window_eagl_set_preferred_size (GstGLWindow * window, gint width, gint height)
{
  GstGLWindowEagl *window_eagl = GST_GL_WINDOW_EAGL (window);

  window_eagl->priv->preferred_width = width;
  window_eagl->priv->preferred_height = height;
}

static void
gst_gl_window_eagl_send_message_async (GstGLWindow * window,
    GstGLWindowCB callback, gpointer data, GDestroyNotify destroy)
{
  GstGLWindowEagl *window_eagl = (GstGLWindowEagl *) window;
  GstGLContext *context = gst_gl_window_get_context (window);
  GThread *thread = gst_gl_context_get_thread (context);

  if (thread == g_thread_self()) {
    /* this case happens for nested calls happening from inside the GCD queue */
    callback (data);
    if (destroy)
      destroy (data);
    gst_object_unref (context);
  } else {
    dispatch_async ((__bridge dispatch_queue_t)(window_eagl->priv->gl_queue), ^{
      gst_gl_context_activate (context, TRUE);
      callback (data);
      gst_object_unref (context);
      if (destroy)
        destroy (data);
    });
  }
  if (thread)
    g_thread_unref (thread);
}

static void
draw_cb (gpointer data)
{
  GstGLWindowEagl *window_eagl = data;
  GstGLWindow *window = GST_GL_WINDOW (window_eagl);
  GstGLContext *context = gst_gl_window_get_context (window);
  GstGLContextEagl *eagl_context = GST_GL_CONTEXT_EAGL (context);
  GstGLContextClass *context_class = GST_GL_CONTEXT_GET_CLASS (context);

  if (window_eagl->priv->view) {
    CGSize size;
    CAEAGLLayer *eagl_layer;

    eagl_layer = (CAEAGLLayer *)[GS_GL_WINDOW_EAGL_VIEW(window_eagl) layer];
    size = eagl_layer.frame.size;

    if (window->queue_resize || window_eagl->priv->window_width != size.width ||
        window_eagl->priv->window_height != size.height) {

      window_eagl->priv->window_width = size.width;
      window_eagl->priv->window_height = size.height;

      gst_gl_context_eagl_resize (eagl_context);

      gst_gl_window_resize (window, window_eagl->priv->window_width,
            window_eagl->priv->window_height);
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
gst_gl_window_eagl_draw (GstGLWindow * window)
{
  gst_gl_window_send_message (window, (GstGLWindowCB) draw_cb, window);
}
