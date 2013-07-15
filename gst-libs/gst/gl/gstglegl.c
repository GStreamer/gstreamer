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

#include "gl.h"
#include "gstglegl.h"

static const gchar *
gst_gl_egl_get_error_string (void)
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
gst_gl_egl_choose_config (GstGLEGL * egl, GError ** error)
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
  config_attrib[i++] = EGL_DEPTH_SIZE;
  config_attrib[i++] = 16;
  config_attrib[i++] = EGL_NONE;

  if (eglChooseConfig (egl->egl_display, config_attrib,
          &egl->egl_config, 1, &numConfigs)) {
    GST_INFO ("config set: %ld, %ld", (gulong) egl->egl_config,
        (gulong) numConfigs);
  } else {
    g_set_error (error, GST_GL_WINDOW_ERROR, GST_GL_WINDOW_ERROR_WRONG_CONFIG,
        "Failed to set window configuration: %s",
        gst_gl_egl_get_error_string ());
    goto failure;
  }

  return TRUE;

failure:
  return FALSE;
}

GstGLEGL *
gst_gl_egl_create_context (EGLDisplay display, EGLNativeWindowType window,
    GstGLAPI gl_api, guintptr external_gl_context, GError ** error)
{
  GstGLEGL *egl;
  gint i = 0;
  EGLint context_attrib[3];
  EGLint majorVersion;
  EGLint minorVersion;

  egl = g_slice_new0 (GstGLEGL);

  if ((gl_api & GST_GL_API_OPENGL) == GST_GL_API_NONE &&
      (gl_api & GST_GL_API_GLES2) == GST_GL_API_NONE) {
    g_set_error (error, GST_GL_WINDOW_ERROR, GST_GL_WINDOW_ERROR_WRONG_API,
        "xEGL supports opengl or gles2");
    goto failure;
  }

  egl->egl_display = display;

  if (eglInitialize (egl->egl_display, &majorVersion, &minorVersion)) {
    GST_INFO ("egl initialized, version: %d.%d", majorVersion, minorVersion);
  } else {
    g_set_error (error, GST_GL_WINDOW_ERROR,
        GST_GL_WINDOW_ERROR_RESOURCE_UNAVAILABLE,
        "Failed to initialize egl: %s", gst_gl_egl_get_error_string ());
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
        if (gl_api & GST_GL_API_GLES2) {
          goto try_gles2;
        } else {
          g_set_error (error, GST_GL_WINDOW_ERROR,
              GST_GL_WINDOW_ERROR_WRONG_CONFIG,
              "Failed to choose a suitable OpenGL API");
          goto failure;
        }
      }
    }

    if (!eglBindAPI (EGL_OPENGL_API)) {
      g_set_error (error, GST_GL_WINDOW_ERROR, GST_GL_WINDOW_ERROR_FAILED,
          "Failed to bind OpenGL|ES API: %s", gst_gl_egl_get_error_string ());
      goto failure;
    }

    egl->gl_api = GST_GL_API_OPENGL;
  } else if (gl_api & GST_GL_API_GLES2) {
  try_gles2:
    if (!eglBindAPI (EGL_OPENGL_ES_API)) {
      g_set_error (error, GST_GL_WINDOW_ERROR, GST_GL_WINDOW_ERROR_FAILED,
          "Failed to bind OpenGL|ES API: %s", gst_gl_egl_get_error_string ());
      goto failure;
    }

    egl->gl_api = GST_GL_API_GLES2;
  }

  if (!gst_gl_egl_choose_config (egl, error)) {
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
    GST_INFO ("gl context created: %ld", (gulong) egl->egl_context);
  } else {
    g_set_error (error, GST_GL_WINDOW_ERROR, GST_GL_WINDOW_ERROR_CREATE_CONTEXT,
        "Failed to create a OpenGL context: %s",
        gst_gl_egl_get_error_string ());
    goto failure;
  }


  if (window) {
    egl->egl_surface =
        eglCreateWindowSurface (egl->egl_display, egl->egl_config, window,
        NULL);
  } else {
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
  }

  if (egl->egl_surface != EGL_NO_SURFACE) {
    GST_INFO ("surface created");
  } else {
    g_set_error (error, GST_GL_WINDOW_ERROR, GST_GL_WINDOW_ERROR_FAILED,
        "Failed to create window surface: %s", gst_gl_egl_get_error_string ());
    goto failure;
  }

  gst_gl_egl_activate (egl, TRUE);

  return egl;

failure:
  gst_gl_egl_destroy_context (egl);

  return NULL;
}

void
gst_gl_egl_destroy_context (GstGLEGL * egl)
{
  gst_gl_egl_activate (egl, FALSE);

  if (egl->egl_surface)
    eglDestroySurface (egl->egl_surface, egl->egl_display);

  if (egl->egl_context)
    eglDestroyContext (egl->egl_display, egl->egl_context);

  if (egl->egl_display) {
    eglTerminate (egl->egl_display);
    eglReleaseThread ();
  }

  g_slice_free (GstGLEGL, egl);
}

gboolean
gst_gl_egl_activate (GstGLEGL * egl, gboolean activate)
{
  gboolean result;

  if (activate)
    result = eglMakeCurrent (egl->egl_display, egl->egl_surface,
        egl->egl_surface, egl->egl_context);
  else
    result = eglMakeCurrent (egl->egl_display, EGL_NO_SURFACE,
        EGL_NO_SURFACE, EGL_NO_CONTEXT);

  return result;
}

guintptr
gst_gl_egl_get_gl_context (GstGLEGL * egl)
{
  return (guintptr) egl->egl_context;
}

void
gst_gl_egl_swap_buffers (GstGLEGL * egl)
{
  eglSwapBuffers (egl->egl_display, egl->egl_surface);
}

GstGLAPI
gst_gl_egl_get_gl_api (GstGLEGL * egl)
{
  return egl->gl_api;
}

gpointer
gst_gl_egl_get_proc_address (GstGLEGL * egl, const gchar * name)
{
  gpointer result;

  /* FIXME: On Android this returns wrong addresses for non-EGL functions */
#ifdef GST_GL_HAVE_WINDOW_ANDROID
  return NULL;
#endif

  result = eglGetProcAddress (name);

  return result;
}
