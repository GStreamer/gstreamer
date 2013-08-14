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

#include "../gstgl_fwd.h"
#include <gst/gl/gstglcontext.h>

#include "gstglwindow_x11_egl.h"

const gchar *X11EGLErrorString ();

#define GST_CAT_DEFAULT gst_gl_window_debug

#define gst_gl_window_x11_egl_parent_class parent_class
G_DEFINE_TYPE (GstGLWindowX11EGL, gst_gl_window_x11_egl,
    GST_GL_TYPE_WINDOW_X11);

static guintptr gst_gl_window_x11_egl_get_gl_context (GstGLWindowX11 *
    window_x11);
static void gst_gl_window_x11_egl_swap_buffers (GstGLWindowX11 * window_x11);
static gboolean gst_gl_window_x11_egl_activate (GstGLWindowX11 * window_x11,
    gboolean activate);
static gboolean gst_gl_window_x11_egl_create_context (GstGLWindowX11 *
    window_x11, GstGLAPI gl_api, guintptr external_gl_context, GError ** error);
static void gst_gl_window_x11_egl_destroy_context (GstGLWindowX11 * window_x11);
static gboolean gst_gl_window_x11_egl_choose_format (GstGLWindowX11 *
    window_x11, GError ** error);
GstGLAPI gst_gl_window_x11_egl_get_gl_api (GstGLWindow * window);
static gpointer gst_gl_window_x11_egl_get_proc_address (GstGLWindow * window,
    const gchar * name);

static void
gst_gl_window_x11_egl_class_init (GstGLWindowX11EGLClass * klass)
{
  GstGLWindowClass *window_class = (GstGLWindowClass *) klass;
  GstGLWindowX11Class *window_x11_class = (GstGLWindowX11Class *) klass;

  window_x11_class->get_gl_context =
      GST_DEBUG_FUNCPTR (gst_gl_window_x11_egl_get_gl_context);
  window_x11_class->activate =
      GST_DEBUG_FUNCPTR (gst_gl_window_x11_egl_activate);
  window_x11_class->create_context =
      GST_DEBUG_FUNCPTR (gst_gl_window_x11_egl_create_context);
  window_x11_class->destroy_context =
      GST_DEBUG_FUNCPTR (gst_gl_window_x11_egl_destroy_context);
  window_x11_class->choose_format =
      GST_DEBUG_FUNCPTR (gst_gl_window_x11_egl_choose_format);
  window_x11_class->swap_buffers =
      GST_DEBUG_FUNCPTR (gst_gl_window_x11_egl_swap_buffers);

  window_class->get_gl_api =
      GST_DEBUG_FUNCPTR (gst_gl_window_x11_egl_get_gl_api);
  window_class->get_proc_address =
      GST_DEBUG_FUNCPTR (gst_gl_window_x11_egl_get_proc_address);
}

static void
gst_gl_window_x11_egl_init (GstGLWindowX11EGL * window)
{
}

/* Must be called in the gl thread */
GstGLWindowX11EGL *
gst_gl_window_x11_egl_new (void)
{
  GstGLWindowX11EGL *window = g_object_new (GST_GL_TYPE_WINDOW_X11_EGL, NULL);

  return window;
}

static gboolean
gst_gl_window_x11_egl_choose_format (GstGLWindowX11 * window_x11,
    GError ** error)
{
  gint ret;

  window_x11->visual_info = g_new0 (XVisualInfo, 1);
  ret = XMatchVisualInfo (window_x11->device, window_x11->screen_num,
      window_x11->depth, TrueColor, window_x11->visual_info);

  if (ret == 0) {
    g_set_error (error, GST_GL_WINDOW_ERROR, GST_GL_WINDOW_ERROR_WRONG_CONFIG,
        "Failed to match XVisualInfo");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_gl_window_x11_egl_create_context (GstGLWindowX11 * window_x11,
    GstGLAPI gl_api, guintptr external_gl_context, GError ** error)
{
  GstGLWindowX11EGL *window_egl;

  if ((gl_api & GST_GL_API_OPENGL) == GST_GL_API_NONE &&
      (gl_api & GST_GL_API_GLES2) == GST_GL_API_NONE) {
    g_set_error (error, GST_GL_WINDOW_ERROR, GST_GL_WINDOW_ERROR_WRONG_API,
        "xEGL supports opengl or gles2");
    goto failure;
  }

  window_egl = GST_GL_WINDOW_X11_EGL (window_x11);

  window_egl->egl =
      gst_gl_egl_create_context (eglGetDisplay ((EGLNativeDisplayType)
          window_x11->device),
      (EGLNativeWindowType) window_x11->internal_win_id, gl_api,
      external_gl_context, error);
  if (!window_egl->egl)
    goto failure;

  return TRUE;

failure:
  return FALSE;
}

static void
gst_gl_window_x11_egl_destroy_context (GstGLWindowX11 * window_x11)
{
  GstGLWindowX11EGL *window_egl;

  window_egl = GST_GL_WINDOW_X11_EGL (window_x11);

  gst_gl_egl_destroy_context (window_egl->egl);
  window_egl->egl = NULL;
}

static gboolean
gst_gl_window_x11_egl_activate (GstGLWindowX11 * window_x11, gboolean activate)
{
  GstGLWindowX11EGL *window_egl;

  window_egl = GST_GL_WINDOW_X11_EGL (window_x11);

  return gst_gl_egl_activate (window_egl->egl, activate);
}

static guintptr
gst_gl_window_x11_egl_get_gl_context (GstGLWindowX11 * window_x11)
{
  GstGLWindowX11EGL *window_egl;

  window_egl = GST_GL_WINDOW_X11_EGL (window_x11);

  return gst_gl_egl_get_gl_context (window_egl->egl);
}

static void
gst_gl_window_x11_egl_swap_buffers (GstGLWindowX11 * window_x11)
{
  GstGLWindowX11EGL *window_egl = GST_GL_WINDOW_X11_EGL (window_x11);

  gst_gl_egl_swap_buffers (window_egl->egl);
}

GstGLAPI
gst_gl_window_x11_egl_get_gl_api (GstGLWindow * window)
{
  GstGLWindowX11EGL *window_egl = GST_GL_WINDOW_X11_EGL (window);

  return window_egl->egl ? gst_gl_egl_get_gl_api (window_egl->
      egl) : GST_GL_API_GLES2 | GST_GL_API_OPENGL;
}

static gpointer
gst_gl_window_x11_egl_get_proc_address (GstGLWindow * window,
    const gchar * name)
{
  GstGLContext *context = NULL;
  GstGLWindowX11EGL *window_egl = GST_GL_WINDOW_X11_EGL (window);
  gpointer result;

  if (!(result = gst_gl_egl_get_proc_address (window_egl->egl, name))) {
    result = gst_gl_context_default_get_proc_address (context, name);
  }

  return result;
}
