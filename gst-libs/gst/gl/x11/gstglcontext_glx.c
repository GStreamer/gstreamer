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

/* FIXME: Sharing contexts requires the Display to be the same.
 * May need to box it
 */

#include <gst/gst.h>

#include "../gstgl_fwd.h"
#include <gst/gl/gstglcontext.h>

#include <gst/gl/gl.h>
#include "gstglcontext_glx.h"
#include "../utils/opengl_versions.h"

#define GST_CAT_DEFAULT gst_gl_context_debug

#define gst_gl_context_glx_parent_class parent_class
G_DEFINE_TYPE (GstGLContextGLX, gst_gl_context_glx, GST_TYPE_GL_CONTEXT);

#define GST_GL_CONTEXT_GLX_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_TYPE_GL_CONTEXT_GLX, GstGLContextGLXPrivate))

static guintptr gst_gl_context_glx_get_gl_context (GstGLContext * context);
static void gst_gl_context_glx_swap_buffers (GstGLContext * context);
static gboolean gst_gl_context_glx_activate (GstGLContext * context,
    gboolean activate);
static gboolean gst_gl_context_glx_create_context (GstGLContext *
    context, GstGLAPI gl_api, GstGLContext * other_context, GError ** error);
static void gst_gl_context_glx_destroy_context (GstGLContext * context);
static gboolean gst_gl_context_glx_choose_format (GstGLContext *
    context, GError ** error);
GstGLAPI gst_gl_context_glx_get_gl_api (GstGLContext * context);
static GstGLPlatform gst_gl_context_glx_get_gl_platform (GstGLContext *
    context);
static void gst_gl_context_glx_get_gl_platform_version (GstGLContext * context,
    gint * major, gint * minor);

struct _GstGLContextGLXPrivate
{
  int glx_major;
  int glx_minor;

  GstGLAPI context_api;

  GLXFBConfig *fbconfigs;
    GLXContext (*glXCreateContextAttribsARB) (Display *, GLXFBConfig,
      GLXContext, Bool, const int *);
};

static void
gst_gl_context_glx_class_init (GstGLContextGLXClass * klass)
{
  GstGLContextClass *context_class = (GstGLContextClass *) klass;

  g_type_class_add_private (klass, sizeof (GstGLContextGLXPrivate));

  context_class->get_gl_context =
      GST_DEBUG_FUNCPTR (gst_gl_context_glx_get_gl_context);
  context_class->activate = GST_DEBUG_FUNCPTR (gst_gl_context_glx_activate);
  context_class->create_context =
      GST_DEBUG_FUNCPTR (gst_gl_context_glx_create_context);
  context_class->destroy_context =
      GST_DEBUG_FUNCPTR (gst_gl_context_glx_destroy_context);
  context_class->choose_format =
      GST_DEBUG_FUNCPTR (gst_gl_context_glx_choose_format);
  context_class->swap_buffers =
      GST_DEBUG_FUNCPTR (gst_gl_context_glx_swap_buffers);

  context_class->get_gl_api = GST_DEBUG_FUNCPTR (gst_gl_context_glx_get_gl_api);
  context_class->get_gl_platform =
      GST_DEBUG_FUNCPTR (gst_gl_context_glx_get_gl_platform);
  context_class->get_proc_address =
      GST_DEBUG_FUNCPTR (gst_gl_context_glx_get_proc_address);
  context_class->get_current_context =
      GST_DEBUG_FUNCPTR (gst_gl_context_glx_get_current_context);
  context_class->get_gl_platform_version =
      GST_DEBUG_FUNCPTR (gst_gl_context_glx_get_gl_platform_version);
}

static void
gst_gl_context_glx_init (GstGLContextGLX * context)
{
  context->priv = GST_GL_CONTEXT_GLX_GET_PRIVATE (context);
}

GstGLContextGLX *
gst_gl_context_glx_new (GstGLDisplay * display)
{
  if ((gst_gl_display_get_handle_type (display) & GST_GL_DISPLAY_TYPE_X11) == 0)
    /* we require an x11 display handle to create GLX contexts */
    return NULL;

  return g_object_new (GST_TYPE_GL_CONTEXT_GLX, NULL);
}

static inline void
_describe_fbconfig (Display * display, GLXFBConfig config)
{
  int val;

  glXGetFBConfigAttrib (display, config, GLX_FBCONFIG_ID, &val);
  GST_DEBUG ("ID: %d", val);
  glXGetFBConfigAttrib (display, config, GLX_DOUBLEBUFFER, &val);
  GST_DEBUG ("double buffering: %d", val);
  glXGetFBConfigAttrib (display, config, GLX_RED_SIZE, &val);
  GST_DEBUG ("red: %d", val);
  glXGetFBConfigAttrib (display, config, GLX_GREEN_SIZE, &val);
  GST_DEBUG ("green: %d", val);
  glXGetFBConfigAttrib (display, config, GLX_BLUE_SIZE, &val);
  GST_DEBUG ("blue: %d", val);
  glXGetFBConfigAttrib (display, config, GLX_ALPHA_SIZE, &val);
  GST_DEBUG ("alpha: %d", val);
  glXGetFBConfigAttrib (display, config, GLX_DEPTH_SIZE, &val);
  GST_DEBUG ("depth: %d", val);
  glXGetFBConfigAttrib (display, config, GLX_STENCIL_SIZE, &val);
  GST_DEBUG ("stencil: %d", val);
}

static GLXContext
_create_context_with_flags (GstGLContextGLX * context_glx, Display * dpy,
    GLXFBConfig fbconfig, GLXContext share_context, gint major, gint minor,
    gint contextFlags, gint profileMask)
{
  GLXContext ret;
#define N_ATTRIBS 20
  gint attribs[N_ATTRIBS];
  int x_error = 0;
  gint n = 0;

  if (major) {
    attribs[n++] = GLX_CONTEXT_MAJOR_VERSION_ARB;
    attribs[n++] = major;
  }
  if (minor) {
    attribs[n++] = GLX_CONTEXT_MINOR_VERSION_ARB;
    attribs[n++] = minor;
  }
  if (contextFlags) {
    attribs[n++] = GLX_CONTEXT_FLAGS_ARB;
    attribs[n++] = contextFlags;
  }
#ifdef GLX_ARB_create_context_profile
  if (profileMask) {
    attribs[n++] = GLX_CONTEXT_PROFILE_MASK_ARB;
    attribs[n++] = profileMask;
  }
#endif
  attribs[n++] = None;

  g_assert (n < N_ATTRIBS);
#undef N_ATTRIBS

  gst_gl_window_x11_trap_x_errors ();
  ret = context_glx->priv->glXCreateContextAttribsARB (dpy, fbconfig,
      share_context, True, attribs);
  x_error = gst_gl_window_x11_untrap_x_errors ();

  if (x_error)
    ret = 0;

  return ret;
}

static gboolean
gst_gl_context_glx_create_context (GstGLContext * context,
    GstGLAPI gl_api, GstGLContext * other_context, GError ** error)
{
  GstGLContextGLX *context_glx;
  GstGLWindow *window;
  GstGLWindowX11 *window_x11;
  GstGLDisplay *display = NULL;
  gboolean create_context;
  const char *glx_exts;
  Display *device;
  guintptr external_gl_context = 0;

  context_glx = GST_GL_CONTEXT_GLX (context);
  window = gst_gl_context_get_window (context);

  if (!GST_IS_GL_WINDOW_X11 (window)) {
    g_set_error (error, GST_GL_CONTEXT_ERROR,
        GST_GL_CONTEXT_ERROR_WRONG_CONFIG,
        "Cannot create an GLX context from a non-X11 window");
    goto failure;
  }

  window_x11 = GST_GL_WINDOW_X11 (window);
  display = gst_gl_context_get_display (context);

  if (other_context) {
    if (gst_gl_context_get_gl_platform (other_context) != GST_GL_PLATFORM_GLX) {
      g_set_error (error, GST_GL_CONTEXT_ERROR,
          GST_GL_CONTEXT_ERROR_WRONG_CONFIG,
          "Cannot share context with non-GLX context");
      goto failure;
    }

    external_gl_context = gst_gl_context_get_gl_context (other_context);
  }

  device = (Display *) gst_gl_display_get_handle (display);
  if (!device) {
    g_set_error (error, GST_GL_CONTEXT_ERROR,
        GST_GL_CONTEXT_ERROR_RESOURCE_UNAVAILABLE, "Invalid Display handle");
    goto failure;
  }

  glx_exts = glXQueryExtensionsString (device, DefaultScreen (device));

  create_context = gst_gl_check_extension ("GLX_ARB_create_context", glx_exts);
  context_glx->priv->glXCreateContextAttribsARB =
      (gpointer) glXGetProcAddressARB ((const GLubyte *)
      "glXCreateContextAttribsARB");

  if (!context_glx->glx_context && gl_api & GST_GL_API_OPENGL3 && create_context
      && context_glx->priv->glXCreateContextAttribsARB) {
    gint i;

    for (i = 0; i < G_N_ELEMENTS (opengl_versions); i++) {
      gint profileMask = 0;
      gint contextFlags = 0;

      if ((opengl_versions[i].major > 3
              || (opengl_versions[i].major == 3
                  && opengl_versions[i].minor >= 2))) {
        profileMask |= GLX_CONTEXT_CORE_PROFILE_BIT_ARB;
        contextFlags |= GLX_CONTEXT_DEBUG_BIT_ARB;
      } else {
        break;
      }

      GST_DEBUG_OBJECT (context, "trying to create a GL %d.%d context",
          opengl_versions[i].major, opengl_versions[i].minor);

      context_glx->glx_context = _create_context_with_flags (context_glx,
          device, context_glx->priv->fbconfigs[0],
          (GLXContext) external_gl_context, opengl_versions[i].major,
          opengl_versions[i].minor, contextFlags, profileMask);

      if (context_glx->glx_context) {
        context_glx->priv->context_api = GST_GL_API_OPENGL3;
        break;
      }
    }
  }
  if (!context_glx->glx_context && gl_api & GST_GL_API_OPENGL) {
    context_glx->glx_context =
        glXCreateContext (device, window_x11->visual_info,
        (GLXContext) external_gl_context, TRUE);
    context_glx->priv->context_api = GST_GL_API_OPENGL;
  }

  if (context_glx->priv->fbconfigs)
    XFree (context_glx->priv->fbconfigs);

  if (!context_glx->glx_context) {
    g_set_error (error, GST_GL_CONTEXT_ERROR,
        GST_GL_CONTEXT_ERROR_CREATE_CONTEXT, "Failed to create opengl context");
    goto failure;
  }

  GST_LOG ("gl context id: %ld", (gulong) context_glx->glx_context);

  gst_object_unref (window);
  gst_object_unref (display);

  return TRUE;

failure:
  if (window)
    gst_object_unref (window);
  if (display)
    gst_object_unref (display);

  return FALSE;
}

static void
gst_gl_context_glx_destroy_context (GstGLContext * context)
{
  GstGLWindow *window;
  GstGLContextGLX *context_glx;
  Display *device;

  context_glx = GST_GL_CONTEXT_GLX (context);
  window = gst_gl_context_get_window (context);
  device = (Display *) gst_gl_display_get_handle (window->display);

  glXDestroyContext (device, context_glx->glx_context);

  context_glx->glx_context = 0;

  gst_object_unref (window);
}

static gboolean
gst_gl_context_glx_choose_format (GstGLContext * context, GError ** error)
{
  GstGLContextGLX *context_glx;
  GstGLWindow *window;
  GstGLWindowX11 *window_x11;
  gint error_base;
  gint event_base;
  Display *device;

  context_glx = GST_GL_CONTEXT_GLX (context);
  window = gst_gl_context_get_window (context);

  if (!GST_IS_GL_WINDOW_X11 (window)) {
    g_set_error (error, GST_GL_CONTEXT_ERROR,
        GST_GL_CONTEXT_ERROR_WRONG_CONFIG,
        "Cannot create an GLX context from a non-X11 window");
    goto failure;
  }
  window_x11 = GST_GL_WINDOW_X11 (window);

  device = (Display *) gst_gl_display_get_handle (window->display);
  if (!device) {
    g_set_error (error, GST_GL_CONTEXT_ERROR,
        GST_GL_CONTEXT_ERROR_RESOURCE_UNAVAILABLE, "Invalid Display handle");
    goto failure;
  }

  if (!glXQueryExtension (device, &error_base, &event_base)) {
    g_set_error (error, GST_GL_CONTEXT_ERROR,
        GST_GL_CONTEXT_ERROR_RESOURCE_UNAVAILABLE, "No GLX extension");
    goto failure;
  }

  if (!glXQueryVersion (device, &context_glx->priv->glx_major,
          &context_glx->priv->glx_minor)) {
    g_set_error (error, GST_GL_CONTEXT_ERROR,
        GST_GL_CONTEXT_ERROR_CREATE_CONTEXT,
        "Failed to query GLX version (glXQueryVersion failed)");
    goto failure;
  }

  GST_INFO ("GLX Version: %d.%d", context_glx->priv->glx_major,
      context_glx->priv->glx_minor);

  /* legacy case */
  if (context_glx->priv->glx_major < 1 || (context_glx->priv->glx_major == 1
          && context_glx->priv->glx_minor < 3)) {
    gint attribs[] = {
      GLX_RGBA,
      GLX_RED_SIZE, 1,
      GLX_GREEN_SIZE, 1,
      GLX_BLUE_SIZE, 1,
      GLX_DEPTH_SIZE, 16,
      GLX_DOUBLEBUFFER,
      None
    };

    window_x11->visual_info = glXChooseVisual (device,
        window_x11->screen_num, attribs);

    if (!window_x11->visual_info) {
      g_set_error (error, GST_GL_CONTEXT_ERROR,
          GST_GL_CONTEXT_ERROR_WRONG_CONFIG,
          "Bad attributes in glXChooseVisual");
      goto failure;
    }
  } else {
    gint attribs[] = {
      GLX_RENDER_TYPE, GLX_RGBA_BIT,
      GLX_RED_SIZE, 1,
      GLX_GREEN_SIZE, 1,
      GLX_BLUE_SIZE, 1,
      GLX_DEPTH_SIZE, 16,
      GLX_DOUBLEBUFFER, True,
      None
    };
    int fbcount;

    context_glx->priv->fbconfigs = glXChooseFBConfig (device,
        DefaultScreen (device), attribs, &fbcount);

    if (!context_glx->priv->fbconfigs) {
      g_set_error (error, GST_GL_CONTEXT_ERROR,
          GST_GL_CONTEXT_ERROR_WRONG_CONFIG,
          "Could not find any FBConfig's to use (check attributes?)");
      goto failure;
    }

    _describe_fbconfig (device, context_glx->priv->fbconfigs[0]);

    window_x11->visual_info = glXGetVisualFromFBConfig (device,
        context_glx->priv->fbconfigs[0]);

    if (!window_x11->visual_info) {
      g_set_error (error, GST_GL_CONTEXT_ERROR,
          GST_GL_CONTEXT_ERROR_WRONG_CONFIG, "Bad attributes in FBConfig");
      goto failure;
    }
  }

  gst_gl_window_x11_create_window ((GstGLWindowX11 *) window);

  gst_object_unref (window);

  return TRUE;

failure:
  if (window)
    gst_object_unref (window);

  return FALSE;
}

static void
gst_gl_context_glx_swap_buffers (GstGLContext * context)
{
  GstGLWindow *window = gst_gl_context_get_window (context);
  Display *device = (Display *) gst_gl_display_get_handle (window->display);
  Window window_handle = (Window) gst_gl_window_get_window_handle (window);

  glXSwapBuffers (device, window_handle);

  gst_object_unref (window);
}

static guintptr
gst_gl_context_glx_get_gl_context (GstGLContext * context)
{
  return (guintptr) GST_GL_CONTEXT_GLX (context)->glx_context;
}

static gboolean
gst_gl_context_glx_activate (GstGLContext * context, gboolean activate)
{
  GstGLWindow *window = gst_gl_context_get_window (context);
  Display *device = (Display *) gst_gl_display_get_handle (window->display);
  Window window_handle = (Window) gst_gl_window_get_window_handle (window);
  gboolean result;

  if (activate) {
    result = glXMakeCurrent (device, window_handle,
        GST_GL_CONTEXT_GLX (context)->glx_context);
  } else {
    result = glXMakeCurrent (device, None, NULL);
  }

  gst_object_unref (window);

  return result;
}

GstGLAPI
gst_gl_context_glx_get_gl_api (GstGLContext * context)
{
  GstGLContextGLX *context_glx;

  context_glx = GST_GL_CONTEXT_GLX (context);

  return context_glx->priv->context_api;
}

static GstGLPlatform
gst_gl_context_glx_get_gl_platform (GstGLContext * context)
{
  return GST_GL_PLATFORM_GLX;
}

gpointer
gst_gl_context_glx_get_proc_address (GstGLAPI gl_api, const gchar * name)
{
  gpointer result;

  if (!(result = gst_gl_context_default_get_proc_address (gl_api, name))) {
    result = glXGetProcAddressARB ((const GLubyte *) name);
  }

  return result;
}

guintptr
gst_gl_context_glx_get_current_context (void)
{
  return (guintptr) glXGetCurrentContext ();
}

static void
gst_gl_context_glx_get_gl_platform_version (GstGLContext * context,
    gint * major, gint * minor)
{
  GstGLContextGLX *context_glx = GST_GL_CONTEXT_GLX (context);

  *major = context_glx->priv->glx_major;
  *minor = context_glx->priv->glx_minor;
}
