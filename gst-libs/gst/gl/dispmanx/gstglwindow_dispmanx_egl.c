/*
 * GStreamer
 * Copyright (C) 2013 Julien Isorce <julien.isorce@collabora.co.uk>
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

#define GLIB_DISABLE_DEPRECATION_WARNINGS

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "../gstgl_fwd.h"
#include <gst/gl/gstglcontext.h>

#include "gstglwindow_dispmanx_egl.h"

#define GST_CAT_DEFAULT gst_gl_window_debug

#define gst_gl_window_dispmanx_egl_parent_class parent_class
G_DEFINE_TYPE (GstGLWindowDispmanxEGL, gst_gl_window_dispmanx_egl,
    GST_GL_TYPE_WINDOW);

static guintptr gst_gl_window_dispmanx_egl_get_window_handle (GstGLWindow *
    window);
static void gst_gl_window_dispmanx_egl_set_window_handle (GstGLWindow * window,
    guintptr handle);
static void gst_gl_window_dispmanx_egl_draw (GstGLWindow * window, guint width,
    guint height);
static void gst_gl_window_dispmanx_egl_run (GstGLWindow * window);
static void gst_gl_window_dispmanx_egl_quit (GstGLWindow * window);
static void gst_gl_window_dispmanx_egl_send_message_async (GstGLWindow * window,
    GstGLWindowCB callback, gpointer data, GDestroyNotify destroy);
static void gst_gl_window_dispmanx_egl_close (GstGLWindow * window);
static gboolean gst_gl_window_dispmanx_egl_open (GstGLWindow * window,
    GError ** error);
static guintptr gst_gl_window_dispmanx_egl_get_display (GstGLWindow * window);


static void window_resize (GstGLWindowDispmanxEGL * window_egl, guint width,
    guint height);

static void
gst_gl_window_dispmanx_egl_class_init (GstGLWindowDispmanxEGLClass * klass)
{
  GstGLWindowClass *window_class = (GstGLWindowClass *) klass;

  window_class->get_window_handle =
      GST_DEBUG_FUNCPTR (gst_gl_window_dispmanx_egl_get_window_handle);
  window_class->set_window_handle =
      GST_DEBUG_FUNCPTR (gst_gl_window_dispmanx_egl_set_window_handle);
  window_class->draw_unlocked =
      GST_DEBUG_FUNCPTR (gst_gl_window_dispmanx_egl_draw);
  window_class->draw = GST_DEBUG_FUNCPTR (gst_gl_window_dispmanx_egl_draw);
  window_class->run = GST_DEBUG_FUNCPTR (gst_gl_window_dispmanx_egl_run);
  window_class->quit = GST_DEBUG_FUNCPTR (gst_gl_window_dispmanx_egl_quit);
  window_class->send_message_async =
      GST_DEBUG_FUNCPTR (gst_gl_window_dispmanx_egl_send_message_async);
  window_class->close = GST_DEBUG_FUNCPTR (gst_gl_window_dispmanx_egl_close);
  window_class->open = GST_DEBUG_FUNCPTR (gst_gl_window_dispmanx_egl_open);
  window_class->get_display =
      GST_DEBUG_FUNCPTR (gst_gl_window_dispmanx_egl_get_display);
}

static void
gst_gl_window_dispmanx_egl_init (GstGLWindowDispmanxEGL * window)
{
}

/* Must be called in the gl thread */
GstGLWindowDispmanxEGL *
gst_gl_window_dispmanx_egl_new (void)
{
  GstGLWindowDispmanxEGL *window;

  GST_DEBUG ("creating Dispmanx EGL window");

  window = g_object_new (GST_GL_TYPE_WINDOW_DISPMANX_EGL, NULL);

  window->egldisplay = EGL_DEFAULT_DISPLAY;

  window->display = 0;
  window->dp_width = 0;
  window->dp_height = 0;
  window->native.element = 0;
  window->native.width = 0;
  window->native.height = 0;

  return window;
}

static void
gst_gl_window_dispmanx_egl_close (GstGLWindow * window)
{
  GstGLWindowDispmanxEGL *window_egl;
  DISPMANX_UPDATE_HANDLE_T dispman_update;

  window_egl = GST_GL_WINDOW_DISPMANX_EGL (window);

  if (window_egl->native.element) {
    dispman_update = vc_dispmanx_update_start (0);
    vc_dispmanx_element_remove (dispman_update, window_egl->native.element);
    vc_dispmanx_update_submit_sync (dispman_update);
  }
  vc_dispmanx_display_close (window_egl->display);

  g_main_loop_unref (window_egl->loop);
  window_egl->loop = NULL;
  g_main_context_unref (window_egl->main_context);
  window_egl->main_context = NULL;
}

static gboolean
gst_gl_window_dispmanx_egl_open (GstGLWindow * window, GError ** error)
{
  GstGLWindowDispmanxEGL *window_egl = GST_GL_WINDOW_DISPMANX_EGL (window);
  gint ret = graphics_get_display_size (0, &window_egl->dp_width,
      &window_egl->dp_height);
  if (ret < 0) {
    GST_ERROR ("Can't open display");
    return FALSE;
  }
  GST_DEBUG ("Got display size: %dx%d\n", window_egl->dp_width,
      window_egl->dp_height);

  window_egl->native.width = 0;
  window_egl->native.height = 0;
  window_egl->display = vc_dispmanx_display_open (0);
  window_egl->native.element = 0;

  window_egl->main_context = g_main_context_new ();
  window_egl->loop = g_main_loop_new (window_egl->main_context, FALSE);

  window_resize (window_egl, 16, 16);

  return TRUE;
}

static void
gst_gl_window_dispmanx_egl_run (GstGLWindow * window)
{
  GstGLWindowDispmanxEGL *window_egl;

  window_egl = GST_GL_WINDOW_DISPMANX_EGL (window);

  GST_LOG ("starting main loop");
  g_main_loop_run (window_egl->loop);
  GST_LOG ("exiting main loop");
}

static void
gst_gl_window_dispmanx_egl_quit (GstGLWindow * window)
{
  GstGLWindowDispmanxEGL *window_egl;

  window_egl = GST_GL_WINDOW_DISPMANX_EGL (window);

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
gst_gl_window_dispmanx_egl_send_message_async (GstGLWindow * window,
    GstGLWindowCB callback, gpointer data, GDestroyNotify destroy)
{
  GstGLWindowDispmanxEGL *window_egl;
  GstGLMessage *message;

  window_egl = GST_GL_WINDOW_DISPMANX_EGL (window);
  message = g_slice_new (GstGLMessage);

  message->callback = callback;
  message->data = data;
  message->destroy = destroy;

  g_main_context_invoke (window_egl->main_context, (GSourceFunc) _run_message,
      message);
}

static guintptr
gst_gl_window_dispmanx_egl_get_window_handle (GstGLWindow * window)
{
  return (guintptr) & GST_GL_WINDOW_DISPMANX_EGL (window)->native;
}

static void
gst_gl_window_dispmanx_egl_set_window_handle (GstGLWindow * window,
    guintptr handle)
{
}

static void
window_resize (GstGLWindowDispmanxEGL * window_egl, guint width, guint height)
{
  GST_DEBUG ("resizing window from %ux%u to %ux%u",
      window_egl->native.width, window_egl->native.height, width, height);

  if (window_egl->display) {
    VC_RECT_T dst_rect;
    VC_RECT_T src_rect;
    GstVideoRectangle src, dst, res;
    DISPMANX_UPDATE_HANDLE_T dispman_update;
    VC_DISPMANX_ALPHA_T alpha =
        { DISPMANX_FLAGS_ALPHA_FIXED_ALL_PIXELS, 255, 0 };

    /* Center width*height frame inside dp_width*dp_height */
    src.w = width;
    src.h = height;
    src.x = src.y = 0;
    dst.w = window_egl->dp_width;
    dst.h = window_egl->dp_height;
    dst.x = dst.y = 0;
    gst_video_sink_center_rect (src, dst, &res, FALSE);

    dst_rect.x = res.x;
    dst_rect.y = res.y;
    dst_rect.width = res.w;
    dst_rect.height = res.h;

    src_rect.x = 0;
    src_rect.y = 0;
    src_rect.width = width << 16;
    src_rect.height = height << 16;

    dispman_update = vc_dispmanx_update_start (0);

    if (window_egl->native.element) {
      vc_dispmanx_element_change_attributes (dispman_update,
          window_egl->native.element, 0x00000110, 0, 0, &dst_rect, &src_rect, 0,
          0);
    } else {
      window_egl->native.element = vc_dispmanx_element_add (dispman_update,
          window_egl->display, 0, &dst_rect, 0, &src_rect,
          DISPMANX_PROTECTION_NONE, &alpha, 0, 0);
    }

    vc_dispmanx_update_submit_sync (dispman_update);

    if (GST_GL_WINDOW (window_egl)->resize)
      GST_GL_WINDOW (window_egl)->resize (GST_GL_WINDOW (window_egl)->
          resize_data, width, height);
  }

  window_egl->native.width = width;
  window_egl->native.height = height;
}

struct draw
{
  GstGLWindowDispmanxEGL *window;
  guint width, height;
};

static void
draw_cb (gpointer data)
{
  struct draw *draw_data = data;
  GstGLWindowDispmanxEGL *window_egl = draw_data->window;
  GstGLWindow *window = GST_GL_WINDOW (window_egl);
  GstGLContext *context = gst_gl_window_get_context (window);
  GstGLContextClass *context_class = GST_GL_CONTEXT_GET_CLASS (context);

  if (window_egl->native.width != draw_data->width
      || window_egl->native.height != draw_data->height) {
    GST_DEBUG ("dimensions don't match, attempting resize");
    window_resize (window_egl, draw_data->width, draw_data->height);
  }

  if (window->draw)
    window->draw (window->draw_data);

  context_class->swap_buffers (context);

  gst_object_unref (context);
}

static void
gst_gl_window_dispmanx_egl_draw (GstGLWindow * window, guint width,
    guint height)
{
  struct draw draw_data;

  draw_data.window = GST_GL_WINDOW_DISPMANX_EGL (window);
  draw_data.width = width;
  draw_data.height = height;

  gst_gl_window_send_message (window, (GstGLWindowCB) draw_cb, &draw_data);
}

static guintptr
gst_gl_window_dispmanx_egl_get_display (GstGLWindow * window)
{
  GstGLWindowDispmanxEGL *window_egl;

  window_egl = GST_GL_WINDOW_DISPMANX_EGL (window);

  return (guintptr) window_egl->egldisplay;
}
