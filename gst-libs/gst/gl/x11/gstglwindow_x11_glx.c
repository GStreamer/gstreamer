/*
 * GStreamer
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
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

#define GLIB_DISABLE_DEPRECATION_WARNINGS

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#include "gstglwindow_x11_glx.h"

#define GST_CAT_DEFAULT gst_gl_window_debug

#define gst_gl_window_x11_glx_parent_class parent_class
G_DEFINE_TYPE (GstGLWindowX11GLX, gst_gl_window_x11_glx,
    GST_GL_TYPE_WINDOW_X11);

static guintptr gst_gl_window_x11_glx_get_gl_context (GstGLWindowX11 *
    window_x11);
static void gst_gl_window_x11_glx_swap_buffers (GstGLWindowX11 * window_x11);
static gboolean gst_gl_window_x11_glx_activate (GstGLWindowX11 * window_x11,
    gboolean activate);
static gboolean gst_gl_window_x11_glx_create_context (GstGLWindowX11 *
    window_x11, GstGLAPI gl_api, guintptr external_gl_context, GError ** error);
static void gst_gl_window_x11_glx_destroy_context (GstGLWindowX11 * window_x11);
static gboolean gst_gl_window_x11_glx_choose_format (GstGLWindowX11 *
    window_x11, GError ** error);
GstGLAPI gst_gl_window_x11_glx_get_gl_api (GstGLWindow * window);
static gpointer gst_gl_window_x11_glx_get_proc_address (GstGLWindow * window,
    const gchar * name);

static void
gst_gl_window_x11_glx_class_init (GstGLWindowX11GLXClass * klass)
{
  GstGLWindowClass *window_class = (GstGLWindowClass *) klass;
  GstGLWindowX11Class *window_x11_class = (GstGLWindowX11Class *) klass;

  window_x11_class->get_gl_context =
      GST_DEBUG_FUNCPTR (gst_gl_window_x11_glx_get_gl_context);
  window_x11_class->activate =
      GST_DEBUG_FUNCPTR (gst_gl_window_x11_glx_activate);
  window_x11_class->create_context =
      GST_DEBUG_FUNCPTR (gst_gl_window_x11_glx_create_context);
  window_x11_class->destroy_context =
      GST_DEBUG_FUNCPTR (gst_gl_window_x11_glx_destroy_context);
  window_x11_class->choose_format =
      GST_DEBUG_FUNCPTR (gst_gl_window_x11_glx_choose_format);
  window_x11_class->swap_buffers =
      GST_DEBUG_FUNCPTR (gst_gl_window_x11_glx_swap_buffers);

  window_class->get_gl_api =
      GST_DEBUG_FUNCPTR (gst_gl_window_x11_glx_get_gl_api);
  window_class->get_proc_address =
      GST_DEBUG_FUNCPTR (gst_gl_window_x11_glx_get_proc_address);
}

static void
gst_gl_window_x11_glx_init (GstGLWindowX11GLX * window)
{
}

/* Must be called in the gl thread */
GstGLWindowX11GLX *
gst_gl_window_x11_glx_new (GstGLAPI gl_api, guintptr external_gl_context,
    GError ** error)
{
  GstGLWindowX11GLX *window = g_object_new (GST_GL_TYPE_WINDOW_X11_GLX, NULL);

  gst_gl_window_x11_open_device (GST_GL_WINDOW_X11 (window), gl_api,
      external_gl_context, error);

  return window;
}

static gboolean
gst_gl_window_x11_glx_create_context (GstGLWindowX11 * window_x11,
    GstGLAPI gl_api, guintptr external_gl_context, GError ** error)
{
  GstGLWindowX11GLX *window_glx;

  window_glx = GST_GL_WINDOW_X11_GLX (window_x11);

  window_glx->glx_context =
      glXCreateContext (window_x11->device, window_x11->visual_info,
      (GLXContext) external_gl_context, TRUE);

  if (!window_glx->glx_context) {
    g_set_error (error, GST_GL_WINDOW_ERROR, GST_GL_WINDOW_ERROR_CREATE_CONTEXT,
        "Failed to create opengl context (glXCreateContext failed)");
    goto failure;
  }

  GST_LOG ("gl context id: %ld", (gulong) window_glx->glx_context);

  return TRUE;

failure:
  return FALSE;
}

static void
gst_gl_window_x11_glx_destroy_context (GstGLWindowX11 * window_x11)
{
  GstGLWindowX11GLX *window_glx;

  window_glx = GST_GL_WINDOW_X11_GLX (window_x11);

  glXDestroyContext (window_x11->device, window_glx->glx_context);

  window_glx->glx_context = 0;
}

static gboolean
gst_gl_window_x11_glx_choose_format (GstGLWindowX11 * window_x11,
    GError ** error)
{
  gint error_base;
  gint event_base;

  gint attrib[] = {
    GLX_RGBA,
    GLX_RED_SIZE, 1, GLX_GREEN_SIZE, 1, GLX_BLUE_SIZE, 1,
    GLX_DEPTH_SIZE, 16,
    GLX_DOUBLEBUFFER,
    None
  };

  if (!glXQueryExtension (window_x11->device, &error_base, &event_base)) {
    g_set_error (error, GST_GL_WINDOW_ERROR,
        GST_GL_WINDOW_ERROR_RESOURCE_UNAVAILABLE, "No GLX extension");
    goto failure;
  }

  window_x11->visual_info = glXChooseVisual (window_x11->device,
      window_x11->screen_num, attrib);

  if (!window_x11->visual_info) {
    g_set_error (error, GST_GL_WINDOW_ERROR, GST_GL_WINDOW_ERROR_WRONG_CONFIG,
        "Bad attributes in glXChooseVisual");
    goto failure;
  }

  return TRUE;

failure:
  return FALSE;
}

static void
gst_gl_window_x11_glx_swap_buffers (GstGLWindowX11 * window_x11)
{
  glXSwapBuffers (window_x11->device, window_x11->internal_win_id);
}

static guintptr
gst_gl_window_x11_glx_get_gl_context (GstGLWindowX11 * window_x11)
{
  return (guintptr) GST_GL_WINDOW_X11_GLX (window_x11)->glx_context;
}

static gboolean
gst_gl_window_x11_glx_activate (GstGLWindowX11 * window_x11, gboolean activate)
{
  gboolean result;

  if (activate) {
    result = glXMakeCurrent (window_x11->device, window_x11->internal_win_id,
        GST_GL_WINDOW_X11_GLX (window_x11)->glx_context);
  } else {
    result = glXMakeCurrent (window_x11->device, None, NULL);
  }

  return result;
}

GstGLAPI
gst_gl_window_x11_glx_get_gl_api (GstGLWindow * window)
{
  return GST_GL_API_OPENGL;
}

static gpointer
gst_gl_window_x11_glx_get_proc_address (GstGLWindow * window,
    const gchar * name)
{
  gpointer result;

  if (!(result = glXGetProcAddressARB ((const GLubyte *) name))) {
    result = gst_gl_window_default_get_proc_address (window, name);
  }

  return result;
}
