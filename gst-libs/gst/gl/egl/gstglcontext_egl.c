/*
 * GStreamer
 * Copyright (C) 2013 Sebastian Dröge <slomo@circular-chaos.org>
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

#include <gmodule.h>

/* FIXME: Sharing contexts requires the EGLDisplay to be the same
 * may need to box it.
 */

#include "gstglcontext_egl.h"

#if GST_GL_HAVE_WINDOW_X11
#include "../x11/gstglwindow_x11.h"
#include <gst/gl/x11/gstgldisplay_x11.h>
#endif
#if GST_GL_HAVE_WINDOW_WIN32
#include "../win32/gstglwindow_win32.h"
#endif

static gboolean gst_gl_context_egl_create_context (GstGLContext * context,
    GstGLAPI gl_api, GstGLContext * other_context, GError ** error);
static void gst_gl_context_egl_destroy_context (GstGLContext * context);
static gboolean gst_gl_context_egl_choose_format (GstGLContext * context,
    GError ** error);

static gboolean gst_gl_context_egl_activate (GstGLContext * context,
    gboolean activate);
static void gst_gl_context_egl_swap_buffers (GstGLContext * context);
static guintptr gst_gl_context_egl_get_gl_context (GstGLContext * context);
static GstGLAPI gst_gl_context_egl_get_gl_api (GstGLContext * context);
static GstGLPlatform gst_gl_context_egl_get_gl_platform (GstGLContext *
    context);
static gpointer gst_gl_context_egl_get_proc_address (GstGLContext * context,
    const gchar * name);
static gboolean gst_gl_context_egl_check_feature (GstGLContext * context,
    const gchar * feature);

G_DEFINE_TYPE (GstGLContextEGL, gst_gl_context_egl, GST_GL_TYPE_CONTEXT);

static void
gst_gl_context_egl_class_init (GstGLContextEGLClass * klass)
{
  GstGLContextClass *context_class = (GstGLContextClass *) klass;

  context_class->get_gl_context =
      GST_DEBUG_FUNCPTR (gst_gl_context_egl_get_gl_context);
  context_class->activate = GST_DEBUG_FUNCPTR (gst_gl_context_egl_activate);
  context_class->create_context =
      GST_DEBUG_FUNCPTR (gst_gl_context_egl_create_context);
  context_class->destroy_context =
      GST_DEBUG_FUNCPTR (gst_gl_context_egl_destroy_context);
  context_class->choose_format =
      GST_DEBUG_FUNCPTR (gst_gl_context_egl_choose_format);
  context_class->swap_buffers =
      GST_DEBUG_FUNCPTR (gst_gl_context_egl_swap_buffers);

  context_class->get_gl_api = GST_DEBUG_FUNCPTR (gst_gl_context_egl_get_gl_api);
  context_class->get_gl_platform =
      GST_DEBUG_FUNCPTR (gst_gl_context_egl_get_gl_platform);
  context_class->get_proc_address =
      GST_DEBUG_FUNCPTR (gst_gl_context_egl_get_proc_address);
  context_class->check_feature =
      GST_DEBUG_FUNCPTR (gst_gl_context_egl_check_feature);
}

static void
gst_gl_context_egl_init (GstGLContextEGL * context)
{
}

/* Must be called in the gl thread */
GstGLContextEGL *
gst_gl_context_egl_new (void)
{
  GstGLContextEGL *window = g_object_new (GST_GL_TYPE_CONTEXT_EGL, NULL);

  return window;
}

static const gchar *
gst_gl_context_egl_get_error_string (void)
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

static gboolean
gst_gl_context_egl_choose_format (GstGLContext * context, GError ** error)
{
#if GST_GL_HAVE_WINDOW_X11
  if (GST_GL_IS_WINDOW_X11 (context->window)) {
    GstGLWindow *window = gst_gl_context_get_window (context);
    GstGLWindowX11 *window_x11 = GST_GL_WINDOW_X11 (window);
    gint ret;

    window_x11->visual_info = g_new0 (XVisualInfo, 1);
    ret = XMatchVisualInfo (window_x11->device, window_x11->screen_num,
        window_x11->depth, TrueColor, window_x11->visual_info);

    gst_object_unref (window);

    if (ret == 0) {
      g_set_error (error, GST_GL_CONTEXT_ERROR,
          GST_GL_CONTEXT_ERROR_WRONG_CONFIG, "Failed to match XVisualInfo");
      return FALSE;
    }
  }
#endif

  return TRUE;
}

static gboolean
gst_gl_context_egl_choose_config (GstGLContextEGL * egl,
    GstGLContext * other_context, GError ** error)
{
  EGLint numConfigs;
  gint i = 0;
  EGLint config_attrib[20];

  config_attrib[i++] = EGL_SURFACE_TYPE;
  config_attrib[i++] = EGL_WINDOW_BIT;
  config_attrib[i++] = EGL_RENDERABLE_TYPE;
  if (egl->gl_api & GST_GL_API_GLES2)
    config_attrib[i++] = EGL_OPENGL_ES2_BIT;
  else
    config_attrib[i++] = EGL_OPENGL_BIT;
#if defined(USE_EGL_RPI) && GST_GL_HAVE_WINDOW_WAYLAND
  /* The configurations r=5 g=6 b=5 seems to be buggy whereas
   * it works when using dispmanx directly */
  config_attrib[i++] = EGL_BUFFER_SIZE;
  config_attrib[i++] = 24;
  /* same with a=0 */
  config_attrib[i++] = EGL_ALPHA_SIZE;
  config_attrib[i++] = 1;
#endif
  config_attrib[i++] = EGL_DEPTH_SIZE;
  config_attrib[i++] = 16;
  config_attrib[i++] = EGL_NONE;

  if (eglChooseConfig (egl->egl_display, config_attrib,
          &egl->egl_config, 1, &numConfigs)) {
    GST_INFO ("config set: %" G_GUINTPTR_FORMAT ", %u",
        (guintptr) egl->egl_config, (unsigned int) numConfigs);
  } else {
    g_set_error (error, GST_GL_CONTEXT_ERROR, GST_GL_CONTEXT_ERROR_WRONG_CONFIG,
        "Failed to set window configuration: %s",
        gst_gl_context_egl_get_error_string ());
    goto failure;
  }

  return TRUE;

failure:
  return FALSE;
}

static gboolean
gst_gl_context_egl_create_context (GstGLContext * context,
    GstGLAPI gl_api, GstGLContext * other_context, GError ** error)
{
  GstGLContextEGL *egl;
  GstGLWindow *window = NULL;
  EGLNativeWindowType window_handle = (EGLNativeWindowType) 0;
  gint i = 0;
  EGLint context_attrib[3];
  EGLint majorVersion;
  EGLint minorVersion;
  const gchar *egl_exts;
  gboolean need_surface = TRUE;
  guintptr external_gl_context = 0;
  GstGLDisplay *display;

  egl = GST_GL_CONTEXT_EGL (context);
  window = gst_gl_context_get_window (context);

  if (other_context) {
    if (gst_gl_context_get_gl_platform (other_context) != GST_GL_PLATFORM_EGL) {
      g_set_error (error, GST_GL_CONTEXT_ERROR,
          GST_GL_CONTEXT_ERROR_WRONG_CONFIG,
          "Cannot share context with non-EGL context");
      goto failure;
    }
    external_gl_context = gst_gl_context_get_gl_context (other_context);
  }

  if ((gl_api & (GST_GL_API_OPENGL | GST_GL_API_GLES2)) == GST_GL_API_NONE) {
    g_set_error (error, GST_GL_CONTEXT_ERROR, GST_GL_CONTEXT_ERROR_WRONG_API,
        "EGL supports opengl or gles2");
    goto failure;
  }

  display = gst_gl_context_get_display (context);

  if (display->type == GST_GL_DISPLAY_TYPE_EGL) {
    egl->egl_display = (EGLDisplay) gst_gl_display_get_handle (display);
  } else {
    guintptr native_display = gst_gl_display_get_handle (display);

    if (!native_display) {
      GstGLWindow *window = NULL;
      GST_WARNING ("Failed to get a global display handle, falling back to "
          "per-window display handles.  Context sharing may not work");

      if (other_context)
        window = gst_gl_context_get_window (other_context);
      if (!window)
        window = gst_gl_context_get_window (context);
      if (window) {
        native_display = gst_gl_window_get_display (window);
        gst_object_unref (window);
      }
    }

    egl->egl_display = eglGetDisplay ((EGLNativeDisplayType) native_display);
  }
  gst_object_unref (display);

  if (eglInitialize (egl->egl_display, &majorVersion, &minorVersion)) {
    GST_INFO ("egl initialized, version: %d.%d", majorVersion, minorVersion);
  } else {
    g_set_error (error, GST_GL_CONTEXT_ERROR,
        GST_GL_CONTEXT_ERROR_RESOURCE_UNAVAILABLE,
        "Failed to initialize egl: %s", gst_gl_context_egl_get_error_string ());
    goto failure;
  }

  if (gl_api & GST_GL_API_OPENGL) {
    /* egl + opengl only available with EGL 1.4+ */
    if (majorVersion == 1 && minorVersion <= 3) {
      if ((gl_api & ~GST_GL_API_OPENGL) == GST_GL_API_NONE) {
        g_set_error (error, GST_GL_CONTEXT_ERROR, GST_GL_CONTEXT_ERROR_OLD_LIBS,
            "EGL version (%i.%i) too old for OpenGL support, (needed at least 1.4)",
            majorVersion, minorVersion);
        goto failure;
      } else {
        GST_WARNING
            ("EGL version (%i.%i) too old for OpenGL support, (needed at least 1.4)",
            majorVersion, minorVersion);
        if (gl_api & GST_GL_API_GLES2) {
          goto try_gles2;
        } else {
          g_set_error (error, GST_GL_CONTEXT_ERROR,
              GST_GL_CONTEXT_ERROR_WRONG_CONFIG,
              "Failed to choose a suitable OpenGL API");
          goto failure;
        }
      }
    }

    if (!eglBindAPI (EGL_OPENGL_API)) {
      g_set_error (error, GST_GL_CONTEXT_ERROR, GST_GL_CONTEXT_ERROR_FAILED,
          "Failed to bind OpenGL API: %s",
          gst_gl_context_egl_get_error_string ());
      goto failure;
    }

    GST_INFO ("Using OpenGL");
    egl->gl_api = GST_GL_API_OPENGL;
  } else if (gl_api & GST_GL_API_GLES2) {
  try_gles2:
    if (!eglBindAPI (EGL_OPENGL_ES_API)) {
      g_set_error (error, GST_GL_CONTEXT_ERROR, GST_GL_CONTEXT_ERROR_FAILED,
          "Failed to bind OpenGL|ES API: %s",
          gst_gl_context_egl_get_error_string ());
      goto failure;
    }

    GST_INFO ("Using OpenGL|ES 2.0");
    egl->gl_api = GST_GL_API_GLES2;
  }

  if (!gst_gl_context_egl_choose_config (egl, other_context, error)) {
    g_assert (error == NULL || *error != NULL);
    goto failure;
  }

  GST_DEBUG ("about to create gl context\n");

  if (egl->gl_api & GST_GL_API_GLES2) {
    context_attrib[i++] = EGL_CONTEXT_CLIENT_VERSION;
    context_attrib[i++] = 2;
  }
  context_attrib[i++] = EGL_NONE;

  egl->egl_context =
      eglCreateContext (egl->egl_display, egl->egl_config,
      (EGLContext) external_gl_context, context_attrib);

  if (egl->egl_context != EGL_NO_CONTEXT) {
    GST_INFO ("gl context created: %" G_GUINTPTR_FORMAT,
        (guintptr) egl->egl_context);
  } else {
    g_set_error (error, GST_GL_CONTEXT_ERROR,
        GST_GL_CONTEXT_ERROR_CREATE_CONTEXT,
        "Failed to create a OpenGL context: %s",
        gst_gl_context_egl_get_error_string ());
    goto failure;
  }

  egl_exts = eglQueryString (egl->egl_display, EGL_EXTENSIONS);

  if (other_context == NULL) {
    /* FIXME do we want a window vfunc ? */
#if GST_GL_HAVE_WINDOW_X11
    if (GST_GL_IS_WINDOW_X11 (context->window)) {
      gst_gl_window_x11_create_window ((GstGLWindowX11 *) context->window);
    }
#endif
#if GST_GL_HAVE_WINDOW_WIN32
    if (GST_GL_IS_WINDOW_WIN32 (context->window)) {
      gst_gl_window_win32_create_window ((GstGLWindowWin32 *) context->window);
    }
#endif
  }

  if (window)
    window_handle =
        (EGLNativeWindowType) gst_gl_window_get_window_handle (window);

  if (window_handle) {
    egl->egl_surface =
        eglCreateWindowSurface (egl->egl_display, egl->egl_config,
        window_handle, NULL);
  } else if (!gst_gl_check_extension ("EGL_KHR_surfaceless_context", egl_exts)) {
    EGLint surface_attrib[7];
    gint j = 0;

    /* FIXME: Width/height doesn't seem to matter but we can't leave them
     * at 0, otherwise X11 complains about BadValue */
    surface_attrib[j++] = EGL_WIDTH;
    surface_attrib[j++] = 1;
    surface_attrib[j++] = EGL_HEIGHT;
    surface_attrib[j++] = 1;
    surface_attrib[j++] = EGL_LARGEST_PBUFFER;
    surface_attrib[j++] = EGL_TRUE;
    surface_attrib[j++] = EGL_NONE;

    egl->egl_surface =
        eglCreatePbufferSurface (egl->egl_display, egl->egl_config,
        surface_attrib);
  } else {
    egl->egl_surface = EGL_NO_SURFACE;
    need_surface = FALSE;
  }

  if (need_surface) {
    if (egl->egl_surface != EGL_NO_SURFACE) {
      GST_INFO ("surface created");
    } else {
      g_set_error (error, GST_GL_CONTEXT_ERROR, GST_GL_CONTEXT_ERROR_FAILED,
          "Failed to create window surface: %s",
          gst_gl_context_egl_get_error_string ());
      goto failure;
    }
  }

  /* EGLImage functions */
  if (GST_GL_CHECK_GL_VERSION (majorVersion, minorVersion, 1, 5)) {
    egl->eglCreateImage = gst_gl_context_get_proc_address (context,
        "eglCreateImage");
    egl->eglDestroyImage = gst_gl_context_get_proc_address (context,
        "eglDestroyImage");
  } else if (gst_gl_check_extension ("EGL_KHR_image_base", egl_exts)) {
    egl->eglCreateImage = gst_gl_context_get_proc_address (context,
        "eglCreateImageKHR");
    egl->eglDestroyImage = gst_gl_context_get_proc_address (context,
        "eglDestroyImageKHR");
  }
  if (egl->eglCreateImage == NULL || egl->eglDestroyImage == NULL) {
    egl->eglCreateImage = NULL;
    egl->eglDestroyImage = NULL;
  }

  if (window)
    gst_object_unref (window);

  return TRUE;

failure:
  if (window)
    gst_object_unref (window);

  return FALSE;
}

static void
gst_gl_context_egl_destroy_context (GstGLContext * context)
{
  GstGLContextEGL *egl;

  egl = GST_GL_CONTEXT_EGL (context);

  gst_gl_context_egl_activate (context, FALSE);

  if (egl->egl_surface)
    eglDestroySurface (egl->egl_display, egl->egl_surface);

  if (egl->egl_context)
    eglDestroyContext (egl->egl_display, egl->egl_context);

  eglReleaseThread ();
}

static gboolean
gst_gl_context_egl_activate (GstGLContext * context, gboolean activate)
{
  GstGLContextEGL *egl;
  gboolean result;

  egl = GST_GL_CONTEXT_EGL (context);

  if (activate)
    result = eglMakeCurrent (egl->egl_display, egl->egl_surface,
        egl->egl_surface, egl->egl_context);
  else
    result = eglMakeCurrent (egl->egl_display, EGL_NO_SURFACE,
        EGL_NO_SURFACE, EGL_NO_CONTEXT);

  return result;
}

static guintptr
gst_gl_context_egl_get_gl_context (GstGLContext * context)
{
  return (guintptr) GST_GL_CONTEXT_EGL (context)->egl_context;
}

static void
gst_gl_context_egl_swap_buffers (GstGLContext * context)
{
  GstGLContextEGL *egl;

  egl = GST_GL_CONTEXT_EGL (context);

  eglSwapBuffers (egl->egl_display, egl->egl_surface);
}

static GstGLAPI
gst_gl_context_egl_get_gl_api (GstGLContext * context)
{
  return GST_GL_CONTEXT_EGL (context)->gl_api;
}

static GstGLPlatform
gst_gl_context_egl_get_gl_platform (GstGLContext * context)
{
  return GST_GL_PLATFORM_EGL;
}

static GModule *module_egl;

static gpointer
load_egl_module (gpointer user_data)
{
#ifdef GST_GL_LIBEGL_MODULE_NAME
  module_egl = g_module_open (GST_GL_LIBEGL_MODULE_NAME, G_MODULE_BIND_LAZY);
#else
  /* On Linux the .so is only in -dev packages, try with a real soname
   * Proper compilers will optimize away the strcmp */
  if (strcmp (G_MODULE_SUFFIX, "so") == 0)
    module_egl = g_module_open ("libEGL.so.1", G_MODULE_BIND_LAZY);

  /* This automatically handles the suffix and even .la files */
  if (!module_egl)
    module_egl = g_module_open ("libEGL", G_MODULE_BIND_LAZY);
#endif

  return NULL;
}

static gpointer
gst_gl_context_egl_get_proc_address (GstGLContext * context, const gchar * name)
{
  gpointer result = NULL;
  static GOnce g_once = G_ONCE_INIT;

  result = gst_gl_context_default_get_proc_address (context, name);

  g_once (&g_once, load_egl_module, NULL);

  if (!result && module_egl) {
    g_module_symbol (module_egl, name, &result);
  }

  /* FIXME: On Android this returns wrong addresses for non-EGL functions */
#if GST_GL_HAVE_WINDOW_ANDROID
  if (!result && g_str_has_prefix (name, "egl")) {
#else
  if (!result) {
    result = eglGetProcAddress (name);
#endif
  }

  return result;
}

static gboolean
gst_gl_context_egl_check_feature (GstGLContext * context, const gchar * feature)
{
  GstGLContextEGL *context_egl = GST_GL_CONTEXT_EGL (context);

  if (g_strcmp0 (feature, "EGL_KHR_image_base") == 0) {
    return context_egl->eglCreateImage != NULL &&
        context_egl->eglDestroyImage != NULL;
  }

  return FALSE;
}
