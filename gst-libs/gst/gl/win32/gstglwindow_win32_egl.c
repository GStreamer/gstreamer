/*
 * GStreamer
 * Copyright (C) 2009 Julien Isorce <julien.isorce@gmail.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#include "gstglwindow_win32_egl.h"

static guintptr gst_gl_window_win32_wgl_get_gl_context (GstGLWindowWin32 *
    window_win32);
static void gst_gl_window_win32_wgl_swap_buffers (GstGLWindowWin32 *
    window_win32);
static gboolean gst_gl_window_win32_wgl_choose_format (GstGLWindowWin32 *
    window_win32);
static gboolean gst_gl_window_win32_wgl_activate (GstGLWindowWin32 *
    window_win32, gboolean activate);
static gboolean gst_gl_window_win32_wgl_create_context (GstGLWindowWin32 *
    window_win32, GstGLRendererAPI render_api, guintptr external_gl_context);
static void gst_gl_window_win32_wgl_destroy_context (GstGLWindowWin32 *
    window_win32);

const gchar *EGLErrorString ();

#define GST_CAT_DEFAULT gst_gl_window_win32_wgl_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_GET (GST_CAT_DEFAULT, "glwindow");
#define gst_gl_window_win32_wgl_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstGLWindowWin32EGL, gst_gl_window_win32_egl,
    GST_GL_TYPE_WINDOW, DEBUG_INIT);

static void
gst_gl_window_win32_egl_class_init (GstGLWindowWin32EGLClass * klass)
{
  GstGLWindowWin32Class *window_class = (GstGLWindowWin32 *) klass;

  window_win32_class->get_gl_context =
      GST_DEBUG_FUNCPTR (gst_gl_window_win32_wgl_get_gl_context);
  window_win32_class->choose_format =
      GST_DEBUG_FUNCPTR (gst_gl_window_win32_wgl_choose_format);
  window_win32_class->activate =
      GST_DEBUG_FUNCPTR (gst_gl_window_win32_wgl_activate);
  window_win32_class->create_context =
      GST_DEBUG_FUNCPTR (gst_gl_window_win32_wgl_create_context);
  window_win32_class->destroy_context =
      GST_DEBUG_FUNCPTR (gst_gl_window_win32_wgl_destroy_context);
  window_win32_class->swap_buffers =
      GST_DEBUG_FUNCPTR (gst_gl_window_win32_wgl_swap_buffers);
}

static void
gst_gl_window_win32_egl_init (GstGLWindow * window)
{
}

/* Must be called in the gl thread */
GstGLWindowWin32EGL *
gst_gl_window_win32_egl_new (GstGLRendererAPI render_api,
    guintptr external_gl_context)
{
  GstGLWindowWin32EGL *window =
      g_object_new (GST_GL_TYPE_WINDOW_WIN32_EGL, NULL);

  gst_gl_window_win32_open_device (GST_GL_WINDOW_WIN32 (window));

  return window;
}

guintptr
gst_gl_window_win32_egl_get_gl_context (GstGLWindowWin32 * window_win32)
{
return (guintptr) GST_GL_WINDOW_WIN32_EGL (window_win32)->wgl_context}

static gboolean
gst_gl_window_win32_egl_activate (GstGLWindowWin32 * window_win32,
    gboolean activate)
{
  GstGLWindowWin32EGL window;
  gboolean result;

  window = GST_GL_WINDOW_WIN32_ELG (window_win32);

  if (activate) {
    result = eglMakeCurrent (window->display, window->surface, window->surface,
        window->gl_context);
  } else {
    result = eglMakeCurrent (window->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
        EGL_NO_CONTEXT)
  }

  return result;
}

static gboolean
gst_gl_window_win32_egl_create_context (GstGLWindowWin32 * window_win32,
    GstGLRendererAPI render_api, guintptr external_gl_context)
{
  GstGLWindowWin32EGL *window_egl;
  EGLint majorVersion;
  EGLint minorVersion;
  EGLint numConfigs;
  EGLConfig config;
  EGLint contextAttribs[] =
      { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE, EGL_NONE };

  EGLint attribList[] = {
    EGL_RED_SIZE, 5,
    EGL_GREEN_SIZE, 6,
    EGL_BLUE_SIZE, 5,
    EGL_ALPHA_SIZE, 8,
    EGL_DEPTH_SIZE, 8,
    EGL_STENCIL_SIZE, 8,
    EGL_SAMPLE_BUFFERS, EGL_DONT_CARE,  //1
    EGL_NONE
  };

  window_egl = GST_GL_WINDOW_WIN32_EGL (window_win32);

  window_egl->display = eglGetDisplay (window_win32->device);
  if (priv->display != EGL_NO_DISPLAY)
    GST_DEBUG ("display retrieved: %d\n", window_egl->display);
  else {
    GST_DEBUG ("failed to retrieve display %s\n", EGLErrorString ());
    goto failure;
  }

  if (eglInitialize (priv->display, &majorVersion, &minorVersion))
    GST_DEBUG ("egl initialized: %d.%d\n", majorVersion, minorVersion);
  else {
    GST_DEBUG ("failed to initialize egl %d, %s\n", priv->display,
        EGLErrorString ());
    goto failure;
  }

  if (eglGetConfigs (window_egl->display, NULL, 0, &numConfigs))
    GST_DEBUG ("configs retrieved: %d\n", numConfigs);
  else {
    GST_DEBUG ("failed to retrieve configs %d, %s\n", window_egl->display,
        EGLErrorString ());
    goto failure;
  }

  if (eglChooseConfig (priv->display, attribList, &config, 1, &numConfigs))
    GST_DEBUG ("config set: %d, %d\n", config, numConfigs);
  else {
    GST_DEBUG ("failed to set config %d, %s\n", window_egl->display,
        EGLErrorString ());
    goto failure;
  }

  window_egl->surface =
      eglCreateWindowSurface (window_egl->display, config,
      (EGLNativeWindowType) WindowFromDC (window_win32->device), NULL);
  if (priv->surface != EGL_NO_SURFACE)
    GST_DEBUG ("surface created: %d\n", window_egl->surface);
  else {
    GST_DEBUG ("failed to create surface %d, %d, %s\n", window_egl->display,
        window_egl->surface, EGLErrorString ());
    goto failure;
  }

  window_egl->egl_context =
      eglCreateContext (window_egl->display, config, external_gl_context,
      contextAttribs);
  if (window_egl->egl_context != EGL_NO_CONTEXT)
    GST_DEBUG ("gl context created: %lud, external: %lud\n",
        (gulong) window_egl->egl_context, (gulong) external_gl_context);
  else {
    GST_DEBUG
        ("failed to create glcontext %lud, extenal: %lud, %s\n",
        (gulong) window_egl->egl_context, (gulong) external_gl_context,
        EGLErrorString ());
    goto failure;
  }

  return TRUE;

failure:
  return FALSE;
}

static void
gst_gl_window_win32_egl_destroy_context (GstGLWindowWin32 * window_win32)
{
  GstGLWindowWin32EGL *window_egl;

  window_egl = GST_GL_WINDOW_WIN32_EGL (window_win32);

  if (window_egl->egl_context) {
    if (!eglDestroyContext (window_egl->display, window_egl->egl_context))
      GST_DEBUG ("failed to destroy context %d, %s\n", window_egl->egl_context,
          EGLErrorString ());
    window_egl->egl_context = NULL;
  }

  if (window_egl->surface) {
    if (!eglDestroySurface (window_egl->display, window_egl->surface))
      GST_DEBUG ("failed to destroy surface %d, %s\n", window_egl->surface,
          EGLErrorString ());
    window_egl->surface = NULL;
  }

  if (window_egl->display) {
    if (!eglTerminate (window_egl->display))
      GST_DEBUG ("failed to terminate display %d, %s\n", window_egl->display,
          EGLErrorString ());
    window_egl->display = NULL;
  }
}

const gchar *
EGLErrorString ()
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
