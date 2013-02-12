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
gst_gl_window_x11_egl_choose_config (GstGLWindowX11EGL * window_egl,
    GError ** error)
{
  EGLint numConfigs;
  gint i = 0;
  EGLint config_attrib[20];

  config_attrib[i++] = EGL_SURFACE_TYPE;
  config_attrib[i++] = EGL_WINDOW_BIT;
  config_attrib[i++] = EGL_RENDERABLE_TYPE;
  if (window_egl->gl_api & GST_GL_API_GLES2)
    config_attrib[i++] = EGL_OPENGL_ES2_BIT;
  else
    config_attrib[i++] = EGL_OPENGL_BIT;
  config_attrib[i++] = EGL_DEPTH_SIZE;
  config_attrib[i++] = 16;
  config_attrib[i++] = EGL_NONE;

  if (eglChooseConfig (window_egl->egl_display, config_attrib,
          &window_egl->egl_config, 1, &numConfigs))
    GST_INFO ("config set: %ld, %ld", (gulong) window_egl->egl_config,
        (gulong) numConfigs);
  else {
    g_set_error (error, GST_GL_WINDOW_ERROR, GST_GL_WINDOW_ERROR_WRONG_CONFIG,
        "Failed to set window configuration: %s", X11EGLErrorString ());
    goto failure;
  }

  return TRUE;

failure:
  return FALSE;
}

static gboolean
gst_gl_window_x11_egl_create_context (GstGLWindowX11 * window_x11,
    GstGLAPI gl_api, guintptr external_gl_context, GError ** error)
{
  GstGLWindowX11EGL *window_egl;

  gint i = 0;
  EGLint context_attrib[3];
  EGLint majorVersion;
  EGLint minorVersion;

  if ((gl_api & GST_GL_API_OPENGL) == GST_GL_API_NONE &&
      (gl_api & GST_GL_API_GLES2) == GST_GL_API_NONE) {
    g_set_error (error, GST_GL_WINDOW_ERROR, GST_GL_WINDOW_ERROR_WRONG_API,
        "xEGL supports opengl or gles2");
    goto failure;
  }

  window_egl = GST_GL_WINDOW_X11_EGL (window_x11);

  window_egl->egl_display =
      eglGetDisplay ((EGLNativeDisplayType) window_x11->device);

  if (eglInitialize (window_egl->egl_display, &majorVersion, &minorVersion))
    GST_INFO ("egl initialized, version: %d.%d", majorVersion, minorVersion);
  else {
    g_set_error (error, GST_GL_WINDOW_ERROR,
        GST_GL_WINDOW_ERROR_RESOURCE_UNAVAILABLE,
        "Failed to initialize egl: %s", X11EGLErrorString ());
    goto failure;
  }

  if (gl_api & GST_GL_API_OPENGL) {
    /* egl + opengl only available with EGL 1.4+ */
    if (majorVersion == 1 && minorVersion <= 3) {
      if ((gl_api & ~GST_GL_API_OPENGL) == GST_GL_API_NONE) {
        g_set_error (error, GST_GL_WINDOW_ERROR, GST_GL_WINDOW_ERROR_OLD_LIBS,
            "EGL version (%i.%i) too old for OpenGL support, (needed at least 1.4)",
            majorVersion, minorVersion);
        goto failure;
      } else {
        GST_WARNING
            ("EGL version (%i.%i) too old for OpenGL support, (needed at least 1.4)",
            majorVersion, minorVersion);
        if (gl_api & GST_GL_API_GLES2)
          goto try_gles2;
        else
          goto failure;
      }
    }

    if (!eglBindAPI (EGL_OPENGL_API)) {
      g_set_error (error, GST_GL_WINDOW_ERROR, GST_GL_WINDOW_ERROR_FAILED,
          "Failed to bind OpenGL|ES API: %s", X11EGLErrorString ());
      goto failure;
    }

    window_egl->gl_api = GST_GL_API_OPENGL;
  } else if (gl_api & GST_GL_API_GLES2) {
  try_gles2:
    if (!eglBindAPI (EGL_OPENGL_ES_API)) {
      g_set_error (error, GST_GL_WINDOW_ERROR, GST_GL_WINDOW_ERROR_FAILED,
          "Failed to bind OpenGL|ES API: %s", X11EGLErrorString ());
      goto failure;
    }

    window_egl->gl_api = GST_GL_API_GLES2;
  }

  gst_gl_window_x11_egl_choose_config (window_egl, error);

  window_egl->egl_surface =
      eglCreateWindowSurface (window_egl->egl_display, window_egl->egl_config,
      (EGLNativeWindowType) window_x11->internal_win_id, NULL);
  if (window_egl->egl_surface != EGL_NO_SURFACE)
    GST_INFO ("surface created");
  else {
    g_set_error (error, GST_GL_WINDOW_ERROR, GST_GL_WINDOW_ERROR_FAILED,
        "Failed to create window surface: %s", X11EGLErrorString ());
    goto failure;
  }

  GST_DEBUG ("about to create gl context\n");

  if (window_egl->gl_api & GST_GL_API_GLES2) {
    context_attrib[i++] = EGL_CONTEXT_CLIENT_VERSION;
    context_attrib[i++] = 2;
  }
  context_attrib[i++] = EGL_NONE;

  window_egl->egl_context =
      eglCreateContext (window_egl->egl_display, window_egl->egl_config,
      (EGLContext) external_gl_context, context_attrib);

  if (window_egl->egl_context != EGL_NO_CONTEXT)
    GST_INFO ("gl context created: %ld", (gulong) window_egl->egl_context);
  else {
    g_set_error (error, GST_GL_WINDOW_ERROR, GST_GL_WINDOW_ERROR_CREATE_CONTEXT,
        "Failed to create a OpenGL context: %s", X11EGLErrorString ());
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
  GstGLWindowX11EGL *window_egl = GST_GL_WINDOW_X11_EGL (window);

  return window_egl->
      gl_api ? window_egl->gl_api : GST_GL_API_GLES2 | GST_GL_API_OPENGL;
}

static gpointer
gst_gl_window_x11_egl_get_proc_address (GstGLWindow * window,
    const gchar * name)
{
  gpointer result;

  if (!(result = eglGetProcAddress (name))) {
    result = gst_gl_window_default_get_proc_address (window, name);
  }

  return result;
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
