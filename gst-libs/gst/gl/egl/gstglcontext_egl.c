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

#include <gst/gl/gstglcontext_private.h>
#include <gst/gl/gstglfeature.h>
#include <gst/gl/gstglwindow.h>

#include "gstegl.h"
#include "../utils/opengl_versions.h"
#include "../utils/gles_versions.h"

#if GST_GL_HAVE_WINDOW_X11
#include "../x11/gstglwindow_x11.h"
#include <gst/gl/x11/gstgldisplay_x11.h>
#endif
#if GST_GL_HAVE_WINDOW_WAYLAND
#include "../wayland/gstglwindow_wayland_egl.h"
#endif
#if GST_GL_HAVE_WINDOW_WIN32
#include "../win32/gstglwindow_win32.h"
#endif
#if GST_GL_HAVE_WINDOW_DISPMANX
#include "../dispmanx/gstglwindow_dispmanx_egl.h"
#endif
#if GST_GL_HAVE_WINDOW_GBM
#include "../gbm/gstglwindow_gbm_egl.h"
#endif

#define GST_CAT_DEFAULT gst_gl_context_debug

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
static gboolean gst_gl_context_egl_check_feature (GstGLContext * context,
    const gchar * feature);
static void gst_gl_context_egl_get_gl_platform_version (GstGLContext * context,
    gint * major, gint * minor);

G_DEFINE_TYPE (GstGLContextEGL, gst_gl_context_egl, GST_TYPE_GL_CONTEXT);

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
  context_class->get_current_context =
      GST_DEBUG_FUNCPTR (gst_gl_context_egl_get_current_context);
  context_class->get_gl_platform_version =
      GST_DEBUG_FUNCPTR (gst_gl_context_egl_get_gl_platform_version);
}

static void
gst_gl_context_egl_init (GstGLContextEGL * context)
{
}

/* Must be called in the gl thread */
GstGLContextEGL *
gst_gl_context_egl_new (GstGLDisplay * display)
{
  GstGLContextEGL *context;

  /* XXX: display type could theoretically be anything, as long as
   * eglGetDisplay supports it. */
  context = g_object_new (GST_TYPE_GL_CONTEXT_EGL, NULL);
  gst_object_ref_sink (context);

  return context;
}

static gboolean
gst_gl_context_egl_choose_format (GstGLContext * context, GError ** error)
{
#if GST_GL_HAVE_WINDOW_X11
  if (GST_IS_GL_WINDOW_X11 (context->window)) {
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
gst_gl_context_egl_choose_config (GstGLContextEGL * egl, GstGLAPI gl_api,
    gint major, GError ** error)
{
  gboolean create_context;
  EGLint numConfigs;
  gint i = 0;
  EGLint config_attrib[20];

  create_context =
      gst_gl_check_extension ("EGL_KHR_create_context", egl->egl_exts);
  /* silence unused warnings */
  (void) create_context;

  config_attrib[i++] = EGL_SURFACE_TYPE;
  config_attrib[i++] = EGL_WINDOW_BIT;
  config_attrib[i++] = EGL_RENDERABLE_TYPE;
  if (gl_api & GST_GL_API_GLES2) {
    if (major == 3) {
#if defined(EGL_KHR_create_context)
      if (create_context) {
        config_attrib[i++] = EGL_OPENGL_ES3_BIT_KHR;
      } else
#endif
      {
        return FALSE;
      }
    } else {
      config_attrib[i++] = EGL_OPENGL_ES2_BIT;
    }
  } else
    config_attrib[i++] = EGL_OPENGL_BIT;
#if defined(USE_EGL_RPI) && GST_GL_HAVE_WINDOW_WAYLAND
  /* The configurations with a=0 seems to be buggy whereas
   * it works when using dispmanx directly */
  config_attrib[i++] = EGL_ALPHA_SIZE;
  config_attrib[i++] = 1;
#endif
  config_attrib[i++] = EGL_DEPTH_SIZE;
  config_attrib[i++] = 16;
  config_attrib[i++] = EGL_RED_SIZE;
  config_attrib[i++] = 1;
  config_attrib[i++] = EGL_GREEN_SIZE;
  config_attrib[i++] = 1;
  config_attrib[i++] = EGL_BLUE_SIZE;
  config_attrib[i++] = 1;
  config_attrib[i++] = EGL_NONE;

  if (eglChooseConfig (egl->egl_display, config_attrib,
          &egl->egl_config, 1, &numConfigs)) {
    GST_INFO ("config set: %" G_GUINTPTR_FORMAT ", %u",
        (guintptr) egl->egl_config, (unsigned int) numConfigs);
  } else {
    g_set_error (error, GST_GL_CONTEXT_ERROR, GST_GL_CONTEXT_ERROR_WRONG_CONFIG,
        "Failed to set window configuration: %s",
        gst_egl_get_error_string (eglGetError ()));
    goto failure;
  }

  return TRUE;

failure:
  return FALSE;
}

static EGLContext
_create_context_with_flags (GstGLContextEGL * egl, EGLContext share_context,
    GstGLAPI gl_api, gint major, gint minor, gint contextFlags,
    gint profileMask)
{
  gboolean create_context;
#define N_ATTRIBS 20
  gint attribs[N_ATTRIBS];
  gint n = 0;

  /* fail creation of apis/versions/flags that require EGL_KHR_create_context
   * if the extension doesn't exist, namely:0
   *
   * - profile mask
   * - context flags
   * - GL3 > 3.1
   * - GLES2 && minor > 0
   */
  create_context =
      gst_gl_check_extension ("EGL_KHR_create_context", egl->egl_exts);
  (void) create_context;
  if (!create_context && (profileMask || contextFlags
          || ((gl_api & GST_GL_API_OPENGL3)
              && GST_GL_CHECK_GL_VERSION (major, minor, 3, 2))
          || ((gl_api & GST_GL_API_GLES2) && minor > 0))) {
    return 0;
  }

  GST_DEBUG_OBJECT (egl, "attempting to create OpenGL%s context version %d.%d "
      "flags %x profile %x", gl_api & GST_GL_API_GLES2 ? " ES" : "", major,
      minor, contextFlags, profileMask);

#if defined(EGL_KHR_create_context)
  if (create_context) {
    if (major) {
      attribs[n++] = EGL_CONTEXT_MAJOR_VERSION_KHR;
      attribs[n++] = major;
    }
    if (minor) {
      attribs[n++] = EGL_CONTEXT_MINOR_VERSION_KHR;
      attribs[n++] = minor;
    }
    if (contextFlags) {
      attribs[n++] = EGL_CONTEXT_FLAGS_KHR;
      attribs[n++] = contextFlags;
    }
    if (profileMask) {
      attribs[n++] = EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR;
      attribs[n++] = profileMask;
    }
  } else
#endif
  {
    attribs[n++] = EGL_CONTEXT_CLIENT_VERSION;
    attribs[n++] = major;
  }
  attribs[n++] = EGL_NONE;

  g_assert (n < N_ATTRIBS);
#undef N_ATTRIBS

  return eglCreateContext (egl->egl_display, egl->egl_config, share_context,
      attribs);
}

static gboolean
gst_gl_context_egl_create_context (GstGLContext * context,
    GstGLAPI gl_api, GstGLContext * other_context, GError ** error)
{
  GstGLContextEGL *egl;
  GstGLWindow *window = NULL;
  guintptr window_handle = 0;
  EGLint egl_major;
  EGLint egl_minor;
  gboolean need_surface = TRUE;
  guintptr external_gl_context = 0;
  guintptr egl_display;

  egl = GST_GL_CONTEXT_EGL (context);
  window = gst_gl_context_get_window (context);

  GST_DEBUG_OBJECT (context, "Creating EGL context");

  if (other_context) {
    if (gst_gl_context_get_gl_platform (other_context) != GST_GL_PLATFORM_EGL) {
      g_set_error (error, GST_GL_CONTEXT_ERROR,
          GST_GL_CONTEXT_ERROR_WRONG_CONFIG,
          "Cannot share context with non-EGL context");
      goto failure;
    }
    external_gl_context = gst_gl_context_get_gl_context (other_context);
  }

  if ((gl_api & (GST_GL_API_OPENGL | GST_GL_API_OPENGL3 | GST_GL_API_GLES2)) ==
      GST_GL_API_NONE) {
    g_set_error (error, GST_GL_CONTEXT_ERROR, GST_GL_CONTEXT_ERROR_WRONG_API,
        "EGL supports opengl or gles2");
    goto failure;
  }

  if (!egl->display_egl) {
    GstGLDisplay *display = gst_gl_context_get_display (context);

    egl->display_egl = gst_gl_display_egl_from_gl_display (display);
    if (!egl->display_egl) {
      g_set_error (error, GST_GL_CONTEXT_ERROR,
          GST_GL_CONTEXT_ERROR_RESOURCE_UNAVAILABLE,
          "Failed to create EGLDisplay from native display");
      gst_object_unref (display);
      goto failure;
    }

    gst_object_unref (display);
  }

  egl_display = gst_gl_display_get_handle (GST_GL_DISPLAY (egl->display_egl));
  egl->egl_display = (EGLDisplay) egl_display;

  if (eglInitialize (egl->egl_display, &egl_major, &egl_minor)) {
    GST_INFO ("egl initialized, version: %d.%d", egl_major, egl_minor);
  } else {
    g_set_error (error, GST_GL_CONTEXT_ERROR,
        GST_GL_CONTEXT_ERROR_RESOURCE_UNAVAILABLE,
        "Failed to initialize egl: %s",
        gst_egl_get_error_string (eglGetError ()));
    goto failure;
  }

  egl->egl_exts = eglQueryString (egl->egl_display, EGL_EXTENSIONS);

  if (gl_api & (GST_GL_API_OPENGL | GST_GL_API_OPENGL3)) {
    GstGLAPI chosen_gl_api = 0;
    gint i;

    /* egl + opengl only available with EGL 1.4+ */
    if (egl_major == 1 && egl_minor <= 3) {
      if ((gl_api & ~GST_GL_API_OPENGL) == GST_GL_API_NONE) {
        g_set_error (error, GST_GL_CONTEXT_ERROR, GST_GL_CONTEXT_ERROR_OLD_LIBS,
            "EGL version (%i.%i) too old for OpenGL support, (needed at least 1.4)",
            egl_major, egl_minor);
        goto failure;
      } else {
        GST_WARNING
            ("EGL version (%i.%i) too old for OpenGL support, (needed at least 1.4)",
            egl_major, egl_minor);
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
          gst_egl_get_error_string (eglGetError ()));
      goto failure;
    }

    GST_INFO ("Bound OpenGL");

    /* api, version only matters for gles */
    if (!gst_gl_context_egl_choose_config (egl, GST_GL_API_OPENGL, 0, error)) {
      g_assert (error == NULL || *error != NULL);
      goto failure;
    }

    for (i = 0; i < G_N_ELEMENTS (opengl_versions); i++) {
      gint profileMask = 0;
      gint contextFlags = 0;

      if (GST_GL_CHECK_GL_VERSION (opengl_versions[i].major,
              opengl_versions[i].minor, 3, 2)) {
        /* skip gl3 contexts if requested */
        if ((gl_api & GST_GL_API_OPENGL3) == 0)
          continue;

        chosen_gl_api = GST_GL_API_OPENGL3;
#if defined(EGL_KHR_create_context)
        profileMask |= EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR;
        contextFlags |= EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR;
#endif
      } else if (opengl_versions[i].major == 3 && opengl_versions[i].minor == 1) {
        /* skip 3.1, the implementation is free to give us either a core or a
         * compatibility context (we have no say) */
        continue;
      } else {
        /* skip legacy contexts if requested */
        if ((gl_api & GST_GL_API_OPENGL) == 0)
          continue;

        chosen_gl_api = GST_GL_API_OPENGL;
      }

      egl->egl_context =
          _create_context_with_flags (egl, (EGLContext) external_gl_context,
          chosen_gl_api, opengl_versions[i].major,
          opengl_versions[i].minor, contextFlags, profileMask);

      if (egl->egl_context)
        break;

#if defined(EGL_KHR_create_context)
      profileMask &= ~EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR;

      egl->egl_context =
          _create_context_with_flags (egl, (EGLContext) external_gl_context,
          chosen_gl_api, opengl_versions[i].major,
          opengl_versions[i].minor, contextFlags, profileMask);

      if (egl->egl_context)
        break;
#endif
    }

    egl->gl_api = chosen_gl_api;
  } else if (gl_api & GST_GL_API_GLES2) {
    gint i;

  try_gles2:
    if (!eglBindAPI (EGL_OPENGL_ES_API)) {
      g_set_error (error, GST_GL_CONTEXT_ERROR, GST_GL_CONTEXT_ERROR_FAILED,
          "Failed to bind OpenGL|ES API: %s",
          gst_egl_get_error_string (eglGetError ()));
      goto failure;
    }

    GST_INFO ("Bound OpenGL|ES");

    for (i = 0; i < G_N_ELEMENTS (gles2_versions); i++) {
      gint profileMask = 0;
      gint contextFlags = 0;
      guint maj = gles2_versions[i].major;
      guint min = gles2_versions[i].minor;

      if (!gst_gl_context_egl_choose_config (egl, GST_GL_API_GLES2, maj, error)) {
        GST_DEBUG_OBJECT (context, "Failed to choose a GLES%d config: %s",
            maj, error && *error ? (*error)->message : "Unknown");
        g_clear_error (error);
        continue;
      }
#if defined(EGL_KHR_create_context)
      /* try a debug context */
      contextFlags |= EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR;

      egl->egl_context =
          _create_context_with_flags (egl, (EGLContext) external_gl_context,
          GST_GL_API_GLES2, maj, min, contextFlags, profileMask);

      if (egl->egl_context)
        break;

      /* try without a debug context */
      contextFlags &= ~EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR;
#endif

      egl->egl_context =
          _create_context_with_flags (egl, (EGLContext) external_gl_context,
          GST_GL_API_GLES2, maj, min, contextFlags, profileMask);

      if (egl->egl_context)
        break;
    }
    egl->gl_api = GST_GL_API_GLES2;
  }

  if (egl->egl_context != EGL_NO_CONTEXT) {
    GST_INFO ("gl context created: %" G_GUINTPTR_FORMAT,
        (guintptr) egl->egl_context);
  } else {
    g_set_error (error, GST_GL_CONTEXT_ERROR,
        GST_GL_CONTEXT_ERROR_CREATE_CONTEXT,
        "Failed to create a OpenGL context: %s",
        gst_egl_get_error_string (eglGetError ()));
    goto failure;
  }
  /* FIXME do we want a window vfunc ? */
#if GST_GL_HAVE_WINDOW_X11
  if (GST_IS_GL_WINDOW_X11 (context->window)) {
    gst_gl_window_x11_create_window ((GstGLWindowX11 *) context->window);
  }
#endif

  if (other_context == NULL) {
    /* FIXME: fails to show two outputs at all.  We need a property/option for
     * glimagesink to say its a visible context */
#if GST_GL_HAVE_WINDOW_WAYLAND
    if (GST_IS_GL_WINDOW_WAYLAND_EGL (context->window)) {
      gst_gl_window_wayland_egl_create_window ((GstGLWindowWaylandEGL *)
          context->window);
    }
#endif
#if GST_GL_HAVE_WINDOW_WIN32
    if (GST_IS_GL_WINDOW_WIN32 (context->window)) {
      gst_gl_window_win32_create_window ((GstGLWindowWin32 *) context->window);
    }
#endif
#if GST_GL_HAVE_WINDOW_DISPMANX
    if (GST_IS_GL_WINDOW_DISPMANX_EGL (context->window)) {
      gst_gl_window_dispmanx_egl_create_window ((GstGLWindowDispmanxEGL *)
          context->window);
    }
#endif
#if GST_GL_HAVE_WINDOW_GBM
    if (GST_IS_GL_WINDOW_GBM_EGL (context->window)) {
      gst_gl_window_gbm_egl_create_window ((GstGLWindowGBMEGL *)
          context->window);
    }
#endif
  }

  if (window)
    window_handle = gst_gl_window_get_window_handle (window);

  if (window_handle) {
    GST_DEBUG ("Creating EGLSurface from window_handle %p",
        (void *) window_handle);
    egl->egl_surface =
        eglCreateWindowSurface (egl->egl_display, egl->egl_config,
        (EGLNativeWindowType) window_handle, NULL);
    /* Store window handle for later comparision */
    egl->window_handle = window_handle;
  } else if (!gst_gl_check_extension ("EGL_KHR_surfaceless_context",
          egl->egl_exts)) {
    EGLint surface_attrib[7];
    gint j = 0;

    GST_DEBUG ("Surfaceless context, creating PBufferSurface");
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
    GST_DEBUG ("No surface/handle !");
    egl->egl_surface = EGL_NO_SURFACE;
    need_surface = FALSE;
  }

  if (need_surface) {
    if (egl->egl_surface != EGL_NO_SURFACE) {
      GST_INFO ("surface created");
    } else {
      g_set_error (error, GST_GL_CONTEXT_ERROR, GST_GL_CONTEXT_ERROR_FAILED,
          "Failed to create window surface: %s",
          gst_egl_get_error_string (eglGetError ()));
      goto failure;
    }
  }
  egl->egl_major = egl_major;
  egl->egl_minor = egl_minor;

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

  if (egl->egl_surface) {
    eglDestroySurface (egl->egl_display, egl->egl_surface);
    egl->egl_surface = EGL_NO_SURFACE;
  }

  if (egl->egl_context) {
    eglDestroyContext (egl->egl_display, egl->egl_context);
    egl->egl_context = NULL;
  }
  egl->window_handle = 0;

  eglReleaseThread ();

  if (egl->display_egl) {
    gst_object_unref (egl->display_egl);
    egl->display_egl = NULL;
  }
}

static gboolean
gst_gl_context_egl_activate (GstGLContext * context, gboolean activate)
{
  GstGLContextEGL *egl;
  gboolean result;

  egl = GST_GL_CONTEXT_EGL (context);

  if (activate) {
    GstGLWindow *window = gst_gl_context_get_window (context);
    guintptr handle = 0;
    /* Check if the backing handle changed */
    if (window) {
      handle = gst_gl_window_get_window_handle (window);
      gst_object_unref (window);
    }
    if (handle && handle != egl->window_handle) {
      GST_DEBUG_OBJECT (context,
          "Handle changed (have:%p, now:%p), switching surface",
          (void *) egl->window_handle, (void *) handle);
      if (egl->egl_surface) {
        result = eglDestroySurface (egl->egl_display, egl->egl_surface);
        egl->egl_surface = EGL_NO_SURFACE;
        if (!result) {
          GST_ERROR_OBJECT (context, "Failed to destroy old window surface: %s",
              gst_egl_get_error_string (eglGetError ()));
          goto done;
        }
      }
      egl->egl_surface =
          eglCreateWindowSurface (egl->egl_display, egl->egl_config,
          (EGLNativeWindowType) handle, NULL);
      egl->window_handle = handle;

      if (egl->egl_surface == EGL_NO_SURFACE) {
        GST_ERROR_OBJECT (context, "Failed to create window surface: %s",
            gst_egl_get_error_string (eglGetError ()));
        result = FALSE;
        goto done;
      }
    }
    result = eglMakeCurrent (egl->egl_display, egl->egl_surface,
        egl->egl_surface, egl->egl_context);
  } else {
    result = eglMakeCurrent (egl->egl_display, EGL_NO_SURFACE,
        EGL_NO_SURFACE, EGL_NO_CONTEXT);
  }

  if (!result) {
    GST_ERROR_OBJECT (context,
        "Failed to bind context to the current rendering thread: %s",
        gst_egl_get_error_string (eglGetError ()));
  }

done:
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
  if (g_strcmp0 (G_MODULE_SUFFIX, "so") == 0)
    module_egl = g_module_open ("libEGL.so.1", G_MODULE_BIND_LAZY);

  /* This automatically handles the suffix and even .la files */
  if (!module_egl)
    module_egl = g_module_open ("libEGL", G_MODULE_BIND_LAZY);
#endif

  return NULL;
}

gpointer
gst_gl_context_egl_get_proc_address (GstGLAPI gl_api, const gchar * name)
{
  gpointer result = NULL;
  static GOnce g_once = G_ONCE_INIT;

#ifdef __APPLE__
#if GST_GL_HAVE_OPENGL && !defined(GST_GL_LIBGL_MODULE_NAME)
  if (!result && (gl_api & (GST_GL_API_OPENGL | GST_GL_API_OPENGL3))) {
    static GModule *module_opengl = NULL;
    if (g_once_init_enter (&module_opengl)) {
      GModule *setup_module_opengl =
          g_module_open ("libGL.dylib", G_MODULE_BIND_LAZY);
      g_once_init_leave (&module_opengl, setup_module_opengl);
    }
    if (module_opengl)
      g_module_symbol (module_opengl, name, &result);
  }
#endif
#if GST_GL_HAVE_GLES2 && !defined(GST_GL_LIBGLESV2_MODULE_NAME)
  if (!result && (gl_api & (GST_GL_API_GLES2))) {
    static GModule *module_gles2 = NULL;
    if (g_once_init_enter (&module_gles2)) {
      GModule *setup_module_gles2 =
          g_module_open ("libGLESv2.dylib", G_MODULE_BIND_LAZY);
      g_once_init_leave (&module_gles2, setup_module_gles2);
    }
    if (module_gles2)
      g_module_symbol (module_gles2, name, &result);
  }
#endif
#endif // __APPLE__

  if (!result)
    result = gst_gl_context_default_get_proc_address (gl_api, name);

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

  return gst_gl_check_extension (feature, context_egl->egl_exts);
}

guintptr
gst_gl_context_egl_get_current_context (void)
{
  return (guintptr) eglGetCurrentContext ();
}

static void
gst_gl_context_egl_get_gl_platform_version (GstGLContext * context,
    gint * major, gint * minor)
{
  GstGLContextEGL *context_egl = GST_GL_CONTEXT_EGL (context);

  *major = context_egl->egl_major;
  *minor = context_egl->egl_minor;
}
