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

#include "gstglwindow_x11_egl.h"

const gchar *X11EGLErrorString ();

#define GST_CAT_DEFAULT gst_gl_window_x11_egl_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_GET (GST_CAT_DEFAULT, "glwindow");
#define gst_gl_window_x11_egl_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstGLWindowX11EGL, gst_gl_window_x11_egl,
    GST_GL_TYPE_WINDOW_X11, DEBUG_INIT);

static guintptr gst_gl_window_x11_egl_get_gl_context (GstGLWindowX11 *
    window_x11);
static void gst_gl_window_x11_egl_swap_buffers (GstGLWindowX11 * window_x11);
static gboolean gst_gl_window_x11_egl_activate (GstGLWindowX11 * window_x11,
    gboolean activate);
static gboolean gst_gl_window_x11_egl_create_context (GstGLWindowX11 *
    window_x11, GstGLAPI gl_api, guintptr external_gl_context);
static void gst_gl_window_x11_egl_destroy_context (GstGLWindowX11 * window_x11);
static gboolean gst_gl_window_x11_egl_choose_format (GstGLWindowX11 *
    window_x11);
GstGLAPI gst_gl_window_x11_egl_get_gl_api (GstGLWindow * window);

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
}

static void
gst_gl_window_x11_egl_init (GstGLWindowX11EGL * window)
{
}

/* Must be called in the gl thread */
GstGLWindowX11EGL *
gst_gl_window_x11_egl_new (GstGLAPI gl_api, guintptr external_gl_context)
{
  GstGLWindowX11EGL *window = g_object_new (GST_GL_TYPE_WINDOW_X11_EGL, NULL);

  gst_gl_window_x11_open_device (GST_GL_WINDOW_X11 (window), gl_api,
      external_gl_context);

  return window;
}

static gboolean
gst_gl_window_x11_egl_choose_format (GstGLWindowX11 * window_x11)
{
  gint ret;

  window_x11->visual_info = g_new0 (XVisualInfo, 1);
  ret = XMatchVisualInfo (window_x11->device, window_x11->screen_num,
      window_x11->depth, TrueColor, window_x11->visual_info);

  return ret != 0;
}

static gboolean
gst_gl_window_x11_egl_create_context (GstGLWindowX11 * window_x11,
    GstGLAPI gl_api, guintptr external_gl_context)
{
  GstGLWindowX11EGL *window_egl;

  EGLint config_attrib[] = {
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_DEPTH_SIZE, 16,
    EGL_NONE
  };

  EGLint context_attrib[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE
  };

  EGLint majorVersion;
  EGLint minorVersion;
  EGLint numConfigs;
  EGLConfig config;

  window_egl = GST_GL_WINDOW_X11_EGL (window_x11);

  window_egl->egl_display =
      eglGetDisplay ((EGLNativeDisplayType) window_x11->device);

  if (eglInitialize (window_egl->egl_display, &majorVersion, &minorVersion))
    g_debug ("egl initialized: %d.%d\n", majorVersion, minorVersion);
  else {
    g_debug ("failed to initialize egl %ld, %s\n",
        (gulong) window_egl->egl_display, X11EGLErrorString ());
    goto failure;
  }

  if (eglChooseConfig (window_egl->egl_display, config_attrib, &config, 1,
          &numConfigs))
    g_debug ("config set: %ld, %ld\n", (gulong) config, (gulong) numConfigs);
  else {
    g_debug ("failed to set config %ld, %s\n", (gulong) window_egl->egl_display,
        X11EGLErrorString ());
    goto failure;
  }

  window_egl->egl_surface =
      eglCreateWindowSurface (window_egl->egl_display, config,
      (EGLNativeWindowType) window_x11->internal_win_id, NULL);
  if (window_egl->egl_surface != EGL_NO_SURFACE)
    g_debug ("surface created: %ld\n", (gulong) window_egl->egl_surface);
  else {
    g_debug ("failed to create surface %ld, %ld, %ld, %s\n",
        (gulong) window_egl->egl_display, (gulong) window_egl->egl_surface,
        (gulong) window_egl->egl_display, X11EGLErrorString ());
    goto failure;
  }

  g_debug ("about to create gl context\n");

  window_egl->egl_context =
      eglCreateContext (window_egl->egl_display, config,
      (EGLContext) external_gl_context, context_attrib);

  if (window_egl->egl_context != EGL_NO_CONTEXT)
    g_debug ("gl context created: %ld\n", (gulong) window_egl->egl_context);
  else {
    g_debug ("failed to create glcontext %ld, %ld, %s\n",
        (gulong) window_egl->egl_context, (gulong) window_egl->egl_display,
        X11EGLErrorString ());
    goto failure;
  }

  return TRUE;

failure:
  return FALSE;
}

static void
gst_gl_window_x11_egl_destroy_context (GstGLWindowX11 * window_x11)
{
  GstGLWindowX11EGL *window_egl;

  window_egl = GST_GL_WINDOW_X11_EGL (window_x11);

  if (window_egl->egl_context)
    eglDestroyContext (window_x11->device, window_egl->egl_context);

  if (window_x11->device)
    eglTerminate (window_x11->device);
}

static gboolean
gst_gl_window_x11_egl_activate (GstGLWindowX11 * window_x11, gboolean activate)
{
  gboolean result;
  GstGLWindowX11EGL *window_egl;

  window_egl = GST_GL_WINDOW_X11_EGL (window_x11);

  if (activate)
    result = eglMakeCurrent (window_egl->egl_display, window_egl->egl_surface,
        window_egl->egl_surface, window_egl->egl_context);
  else
    result = eglMakeCurrent (window_egl->egl_display, EGL_NO_SURFACE,
        EGL_NO_SURFACE, EGL_NO_CONTEXT);

  return result;
}

static guintptr
gst_gl_window_x11_egl_get_gl_context (GstGLWindowX11 * window_x11)
{
  return (guintptr) GST_GL_WINDOW_X11_EGL (window_x11)->egl_context;
}

static void
gst_gl_window_x11_egl_swap_buffers (GstGLWindowX11 * window_x11)
{
  GstGLWindowX11EGL *window_egl = GST_GL_WINDOW_X11_EGL (window_x11);

  eglSwapBuffers (window_egl->egl_display, window_egl->egl_surface);
}

GstGLAPI
gst_gl_window_x11_egl_get_gl_api (GstGLWindow * window)
{
  return GST_GL_API_GLES2;
}

const gchar *
X11EGLErrorString ()
{
  EGLint nErr = eglGetError ();
  switch (nErr) {
    case EGL_SUCCESS:
      return "EGL_SUCCESS";
    case EGL_BAD_DISPLAY:
      return "EGL_BAD_DISPLAY";
    case EGL_NOT_INITIALIZED:
      return "EGL_NOT_INITIALIZED";
    case EGL_BAD_ACCESS:
      return "EGL_BAD_ACCESS";
    case EGL_BAD_ALLOC:
      return "EGL_BAD_ALLOC";
    case EGL_BAD_ATTRIBUTE:
      return "EGL_BAD_ATTRIBUTE";
    case EGL_BAD_CONFIG:
      return "EGL_BAD_CONFIG";
    case EGL_BAD_CONTEXT:
      return "EGL_BAD_CONTEXT";
    case EGL_BAD_CURRENT_SURFACE:
      return "EGL_BAD_CURRENT_SURFACE";
    case EGL_BAD_MATCH:
      return "EGL_BAD_MATCH";
    case EGL_BAD_NATIVE_PIXMAP:
      return "EGL_BAD_NATIVE_PIXMAP";
    case EGL_BAD_NATIVE_WINDOW:
      return "EGL_BAD_NATIVE_WINDOW";
    case EGL_BAD_PARAMETER:
      return "EGL_BAD_PARAMETER";
    case EGL_BAD_SURFACE:
      return "EGL_BAD_SURFACE";
    default:
      return "unknown";
  }
}
