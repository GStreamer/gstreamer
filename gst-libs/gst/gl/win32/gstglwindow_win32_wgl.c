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

#include "../gstgl_fwd.h"
#include <gst/gl/gstglcontext.h>

#include "gstglwindow_win32_wgl.h"

#define GST_CAT_DEFAULT gst_gl_window_debug

#define gst_gl_window_win32_wgl_parent_class parent_class
G_DEFINE_TYPE (GstGLWindowWin32WGL, gst_gl_window_win32_wgl,
    GST_GL_TYPE_WINDOW_WIN32);

static guintptr gst_gl_window_win32_wgl_get_gl_context (GstGLWindowWin32 *
    window_win32);
static void gst_gl_window_win32_wgl_swap_buffers (GstGLWindowWin32 *
    window_win32);
static gboolean gst_gl_window_win32_wgl_choose_format (GstGLWindowWin32 *
    window_win32);
static gboolean gst_gl_window_win32_wgl_activate (GstGLWindowWin32 *
    window_win32, gboolean activate);
static gboolean gst_gl_window_win32_wgl_create_context (GstGLWindowWin32 *
    window_win32, GstGLAPI gl_api, guintptr external_gl_context,
    GError ** error);
static void gst_gl_window_win32_wgl_destroy_context (GstGLWindowWin32 *
    window_win32);
GstGLAPI gst_gl_window_win32_wgl_get_gl_api (GstGLWindow * window);
static gpointer gst_gl_window_win32_wgl_get_proc_address (GstGLWindow * window,
    const gchar * name);

static void
gst_gl_window_win32_wgl_class_init (GstGLWindowWin32WGLClass * klass)
{
  GstGLWindowClass *window_class = (GstGLWindowClass *) klass;
  GstGLWindowWin32Class *window_win32_class = (GstGLWindowWin32Class *) klass;

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

  window_class->get_proc_address =
      GST_DEBUG_FUNCPTR (gst_gl_window_win32_wgl_get_proc_address);
  window_class->get_gl_api =
      GST_DEBUG_FUNCPTR (gst_gl_window_win32_wgl_get_gl_api);
}

static void
gst_gl_window_win32_wgl_init (GstGLWindowWin32WGL * window)
{
}

/* Must be called in the gl thread */
GstGLWindowWin32WGL *
gst_gl_window_win32_wgl_new (void)
{
  GstGLWindowWin32WGL *window =
      g_object_new (GST_GL_TYPE_WINDOW_WIN32_WGL, NULL);

  return window;
}

static gboolean
gst_gl_window_win32_wgl_create_context (GstGLWindowWin32 * window_win32,
    GstGLAPI gl_api, guintptr external_gl_context, GError ** error)
{
  GstGLWindowWin32WGL *window_wgl;

  window_wgl = GST_GL_WINDOW_WIN32_WGL (window_win32);

  window_wgl->wgl_context = wglCreateContext (window_win32->device);
  if (window_wgl->wgl_context)
    GST_DEBUG ("gl context created: %" G_GUINTPTR_FORMAT,
        (guintptr) window_wgl->wgl_context);
  else {
    g_set_error (error, GST_GL_WINDOW_ERROR, GST_GL_WINDOW_ERROR_CREATE_CONTEXT,
        "failed to create glcontext:%lu", GetLastError ());
    goto failure;
  }
  g_assert (window_wgl->wgl_context);

  GST_LOG ("gl context id: %" G_GUINTPTR_FORMAT,
      (guintptr) window_wgl->wgl_context);

  return TRUE;

failure:
  return FALSE;
}

static void
gst_gl_window_win32_wgl_destroy_context (GstGLWindowWin32 * window_win32)
{
  GstGLWindowWin32WGL *window_wgl;

  window_wgl = GST_GL_WINDOW_WIN32_WGL (window_win32);

  if (window_wgl->wgl_context)
    wglDeleteContext (window_wgl->wgl_context);
  window_wgl->wgl_context = NULL;
}

static gboolean
gst_gl_window_win32_wgl_choose_format (GstGLWindowWin32 * window_win32)
{
  PIXELFORMATDESCRIPTOR pfd;
  gint pixelformat = 0;
  gboolean res = FALSE;

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

  pfd.cColorBits = (BYTE) GetDeviceCaps (window_win32->device, BITSPIXEL);

  pixelformat = ChoosePixelFormat (window_win32->device, &pfd);

  g_return_val_if_fail (pixelformat, FALSE);

  res = SetPixelFormat (window_win32->device, pixelformat, &pfd);

  return res;
}

static void
gst_gl_window_win32_wgl_swap_buffers (GstGLWindowWin32 * window_win32)
{
  SwapBuffers (window_win32->device);
}

static guintptr
gst_gl_window_win32_wgl_get_gl_context (GstGLWindowWin32 * window_win32)
{
  return (guintptr) GST_GL_WINDOW_WIN32_WGL (window_win32)->wgl_context;
}

static gboolean
gst_gl_window_win32_wgl_activate (GstGLWindowWin32 * window_win32,
    gboolean activate)
{
  gboolean result;

  if (activate) {
    result = wglMakeCurrent (window_win32->device,
        GST_GL_WINDOW_WIN32_WGL (window_win32)->wgl_context);
  } else {
    result = wglMakeCurrent (NULL, NULL);
  }

  return result;
}

GstGLAPI
gst_gl_window_win32_wgl_get_gl_api (GstGLWindow * window)
{
  return GST_GL_API_OPENGL;
}

static gpointer
gst_gl_window_win32_wgl_get_proc_address (GstGLWindow * window,
    const gchar * name)
{
  GstGLContext *context;
  gpointer result;

  if (!(result = wglGetProcAddress ((LPCSTR) name))) {
    result = gst_gl_context_default_get_proc_address (context, name);
  }

  return result;
}
