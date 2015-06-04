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
static void gst_gl_window_eagl_set_preferred_size (GstGLWindow * window,
    gint width, gint height);
static void gst_gl_window_eagl_draw (GstGLWindow * window);

struct _GstGLWindowEaglPrivate
{
  UIView *view;
  gint window_width, window_height;
  gint preferred_width, preferred_height;
};

static void
gst_gl_window_eagl_class_init (GstGLWindowEaglClass * klass)
{
  GstGLWindowClass *window_class = (GstGLWindowClass *) klass;

  g_type_class_add_private (klass, sizeof (GstGLWindowEaglPrivate));

  window_class->get_display =
      GST_DEBUG_FUNCPTR (gst_gl_window_eagl_get_display);
  window_class->get_window_handle =
      GST_DEBUG_FUNCPTR (gst_gl_window_eagl_get_window_handle);
  window_class->set_window_handle =
      GST_DEBUG_FUNCPTR (gst_gl_window_eagl_set_window_handle);
  window_class->draw_unlocked = GST_DEBUG_FUNCPTR (gst_gl_window_eagl_draw);
  window_class->draw = GST_DEBUG_FUNCPTR (gst_gl_window_eagl_draw);
  window_class->set_preferred_size =
      GST_DEBUG_FUNCPTR (gst_gl_window_eagl_set_preferred_size);
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
  GstGLContext *context;

  window_eagl = GST_GL_WINDOW_EAGL (window);
  context = gst_gl_window_get_context (window);

  window_eagl->priv->view = (UIView *) handle;
  GST_INFO_OBJECT (context, "handle set, updating layer");
  gst_gl_context_eagl_update_layer (context);
}

static void
gst_gl_window_eagl_set_preferred_size (GstGLWindow * window, gint width, gint height)
{
  GstGLWindowEagl *window_eagl = GST_GL_WINDOW_EAGL (window);

  window_eagl->priv->preferred_width = width;
  window_eagl->priv->preferred_height = height;
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

    eagl_layer = (CAEAGLLayer *)[window_eagl->priv->view layer];
    size = eagl_layer.frame.size;

    if (window_eagl->priv->window_width != size.width ||
        window_eagl->priv->window_height != size.height) {

      window_eagl->priv->window_width = size.width;
      window_eagl->priv->window_height = size.height;

      gst_gl_context_eagl_resize (eagl_context);

      if (window->resize)
        window->resize (window->resize_data, window_eagl->priv->window_width,
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
