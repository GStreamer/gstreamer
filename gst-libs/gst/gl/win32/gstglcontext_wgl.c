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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#include "../gstgl_fwd.h"
#include <gst/gl/gstglcontext.h>

#include "gstglcontext_wgl.h"
#include <GL/wglext.h>

#define gst_gl_context_wgl_parent_class parent_class
G_DEFINE_TYPE (GstGLContextWGL, gst_gl_context_wgl, GST_GL_TYPE_CONTEXT);

static guintptr gst_gl_context_wgl_get_gl_context (GstGLContext * context);
static void gst_gl_context_wgl_swap_buffers (GstGLContext * context);
static gboolean gst_gl_context_wgl_choose_format (GstGLContext * context,
    GError ** error);
static gboolean gst_gl_context_wgl_activate (GstGLContext * context,
    gboolean activate);
static gboolean gst_gl_context_wgl_create_context (GstGLContext * context,
    GstGLAPI gl_api, GstGLContext * other_context, GError ** error);
static void gst_gl_context_wgl_destroy_context (GstGLContext * context);
GstGLAPI gst_gl_context_wgl_get_gl_api (GstGLContext * context);
static GstGLPlatform gst_gl_context_wgl_get_gl_platform (GstGLContext *
    context);
static gpointer gst_gl_context_wgl_get_proc_address (GstGLContext * context,
    const gchar * name);

static void
gst_gl_context_wgl_class_init (GstGLContextWGLClass * klass)
{
  GstGLContextClass *context_class = (GstGLContextClass *) klass;

  context_class->get_gl_context =
      GST_DEBUG_FUNCPTR (gst_gl_context_wgl_get_gl_context);
  context_class->choose_format =
      GST_DEBUG_FUNCPTR (gst_gl_context_wgl_choose_format);
  context_class->activate = GST_DEBUG_FUNCPTR (gst_gl_context_wgl_activate);
  context_class->create_context =
      GST_DEBUG_FUNCPTR (gst_gl_context_wgl_create_context);
  context_class->destroy_context =
      GST_DEBUG_FUNCPTR (gst_gl_context_wgl_destroy_context);
  context_class->swap_buffers =
      GST_DEBUG_FUNCPTR (gst_gl_context_wgl_swap_buffers);

  context_class->get_proc_address =
      GST_DEBUG_FUNCPTR (gst_gl_context_wgl_get_proc_address);
  context_class->get_gl_api = GST_DEBUG_FUNCPTR (gst_gl_context_wgl_get_gl_api);
  context_class->get_gl_platform =
      GST_DEBUG_FUNCPTR (gst_gl_context_wgl_get_gl_platform);
}

static void
gst_gl_context_wgl_init (GstGLContextWGL * context_wgl)
{
}

/* Must be called in the gl thread */
GstGLContextWGL *
gst_gl_context_wgl_new (void)
{
  GstGLContextWGL *context = g_object_new (GST_GL_TYPE_CONTEXT_WGL, NULL);

  return context;
}

static gboolean
gst_gl_context_wgl_create_context (GstGLContext * context,
    GstGLAPI gl_api, GstGLContext * other_context, GError ** error)
{
  GstGLWindow *window;
  GstGLContextWGL *context_wgl;
  HGLRC external_gl_context = NULL;
  PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = NULL;
  HDC device;

  context_wgl = GST_GL_CONTEXT_WGL (context);
  window = gst_gl_context_get_window (context);
  device = (HDC) gst_gl_window_get_display (window);

  if (other_context) {
    if (gst_gl_context_get_gl_platform (other_context) != GST_GL_PLATFORM_WGL) {
      g_set_error (error, GST_GL_CONTEXT_ERROR,
          GST_GL_CONTEXT_ERROR_WRONG_CONFIG,
          "Cannot share context with a non-WGL context");
      goto failure;
    }
    external_gl_context = (HGLRC) gst_gl_context_get_gl_context (other_context);
  }

  context_wgl->wgl_context = wglCreateContext (device);
  if (context_wgl->wgl_context)
    GST_DEBUG ("gl context created: %" G_GUINTPTR_FORMAT,
        (guintptr) context_wgl->wgl_context);
  else {
    g_set_error (error, GST_GL_CONTEXT_ERROR,
        GST_GL_CONTEXT_ERROR_CREATE_CONTEXT, "failed to create glcontext:0x%x",
        (unsigned int) GetLastError ());
    goto failure;
  }
  g_assert (context_wgl->wgl_context);


  if (external_gl_context) {

    wglMakeCurrent (device, context_wgl->wgl_context);

    wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)
        wglGetProcAddress ("wglCreateContextAttribsARB");

    if (wglCreateContextAttribsARB != NULL) {
      wglMakeCurrent (device, 0);
      wglDeleteContext (context_wgl->wgl_context);
      context_wgl->wgl_context =
          wglCreateContextAttribsARB (device, external_gl_context, 0);
      if (context_wgl->wgl_context == NULL) {
        g_set_error (error, GST_GL_CONTEXT_ERROR,
            GST_GL_CONTEXT_ERROR_CREATE_CONTEXT,
            "failed to share context through wglCreateContextAttribsARB 0x%x",
            (unsigned int) GetLastError ());
        goto failure;
      }
    } else if (!wglShareLists (external_gl_context, context_wgl->wgl_context)) {
      g_set_error (error, GST_GL_CONTEXT_ERROR,
          GST_GL_CONTEXT_ERROR_CREATE_CONTEXT,
          "failed to share contexts through wglShareLists 0x%x",
          (unsigned int) GetLastError ());
      goto failure;
    }
  }

  GST_LOG ("gl context id: %" G_GUINTPTR_FORMAT,
      (guintptr) context_wgl->wgl_context);

  gst_object_unref (window);

  return TRUE;

failure:
  gst_object_unref (window);

  return FALSE;
}

static void
gst_gl_context_wgl_destroy_context (GstGLContext * context)
{
  GstGLContextWGL *context_wgl;

  context_wgl = GST_GL_CONTEXT_WGL (context);

  if (context_wgl->wgl_context)
    wglDeleteContext (context_wgl->wgl_context);
  context_wgl->wgl_context = NULL;
}

static gboolean
gst_gl_context_wgl_choose_format (GstGLContext * context, GError ** error)
{
  GstGLWindow *window;
  PIXELFORMATDESCRIPTOR pfd;
  gint pixelformat = 0;
  gboolean res = FALSE;
  HDC device;

  window = gst_gl_context_get_window (context);
  gst_gl_window_win32_create_window (GST_GL_WINDOW_WIN32 (window), error);
  device = (HDC) gst_gl_window_get_display (window);
  gst_object_unref (window);

  pfd.nSize = sizeof (PIXELFORMATDESCRIPTOR);
  pfd.nVersion = 1;
  pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
  pfd.iPixelType = PFD_TYPE_RGBA;
  pfd.cColorBits = 24;
  pfd.cRedBits = 8;
  pfd.cRedShift = 0;
  pfd.cGreenBits = 8;
  pfd.cGreenShift = 0;
  pfd.cBlueBits = 8;
  pfd.cBlueShift = 0;
  pfd.cAlphaBits = 0;
  pfd.cAlphaShift = 0;
  pfd.cAccumBits = 0;
  pfd.cAccumRedBits = 0;
  pfd.cAccumGreenBits = 0;
  pfd.cAccumBlueBits = 0;
  pfd.cAccumAlphaBits = 0;
  pfd.cDepthBits = 24;
  pfd.cStencilBits = 8;
  pfd.cAuxBuffers = 0;
  pfd.iLayerType = PFD_MAIN_PLANE;
  pfd.bReserved = 0;
  pfd.dwLayerMask = 0;
  pfd.dwVisibleMask = 0;
  pfd.dwDamageMask = 0;

  pfd.cColorBits = (BYTE) GetDeviceCaps (device, BITSPIXEL);

  pixelformat = ChoosePixelFormat (device, &pfd);

  if (!pixelformat) {
    g_set_error (error, GST_GL_CONTEXT_ERROR, GST_GL_CONTEXT_ERROR_FAILED,
        "Failed to choose a pixel format");
    return FALSE;
  }

  res = SetPixelFormat (device, pixelformat, &pfd);

  return res;
}

static void
gst_gl_context_wgl_swap_buffers (GstGLContext * context)
{
  GstGLWindow *window = gst_gl_context_get_window (context);
  HDC device = (HDC) gst_gl_window_get_display (window);

  SwapBuffers (device);

  gst_object_unref (window);
}

static guintptr
gst_gl_context_wgl_get_gl_context (GstGLContext * context)
{
  return (guintptr) GST_GL_CONTEXT_WGL (context)->wgl_context;
}

static gboolean
gst_gl_context_wgl_activate (GstGLContext * context, gboolean activate)
{
  GstGLWindow *window;
  GstGLContextWGL *context_wgl;
  HDC device;
  gboolean result;

  window = gst_gl_context_get_window (context);
  context_wgl = GST_GL_CONTEXT_WGL (context);
  device = (HDC) gst_gl_window_get_display (window);

  if (activate) {
    result = wglMakeCurrent (device, context_wgl->wgl_context);
  } else {
    result = wglMakeCurrent (NULL, NULL);
  }

  gst_object_unref (window);

  return result;
}

GstGLAPI
gst_gl_context_wgl_get_gl_api (GstGLContext * context)
{
  return GST_GL_API_OPENGL;
}

static GstGLPlatform
gst_gl_context_wgl_get_gl_platform (GstGLContext * context)
{
  return GST_GL_PLATFORM_WGL;
}

static gpointer
gst_gl_context_wgl_get_proc_address (GstGLContext * context, const gchar * name)
{
  gpointer result;

  if (!(result = gst_gl_context_default_get_proc_address (context, name))) {
    result = wglGetProcAddress ((LPCSTR) name);
  }

  return result;
}
