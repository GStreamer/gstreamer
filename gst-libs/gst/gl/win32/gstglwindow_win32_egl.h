/*
 * GStreamer
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

#ifndef __GST_GL_WINDOW_WIN32_EGL_H__
#define __GST_GL_WINDOW_WIN32_EGL_H__

#include <gst/gst.h>

#include "gstglwindow_win32.h"

G_BEGIN_DECLS

#define GST_GL_TYPE_WINDOW_WIN32_EGL         (gst_gl_window_win32_egl_get_type())
#define GST_GL_WINDOW_WIN32_EGL(o)           (G_TYPE_CHECK_INSTANCE_CAST((o), GST_GL_TYPE_WINDOW_WIN32_EGL, GstGLWindowWin32EGL))
#define GST_GL_WINDOW_WIN32_EGL_CLASS(k)     (G_TYPE_CHECK_CLASS((k), GST_GL_TYPE_WINDOW_WIN32_EGL, GstGLWindowWin32EGLClass))
#define GST_GL_IS_WINDOW_WIN32_EGL(o)        (G_TYPE_CHECK_INSTANCE_TYPE((o), GST_GL_TYPE_WINDOW_WIN32_EGL))
#define GST_GL_IS_WINDOW_WIN32_EGL_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE((k), GST_GL_TYPE_WINDOW_WIN32_EGL))
#define GST_GL_WINDOW_WIN32_EGL_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS((o), GST_GL_TYPE_WINDOW_WIN32_EGL, GstGLWindowWin32EGLClass))

typedef struct _GstGLWindowWin32EGL        GstGLWindowWin32EGL;
typedef struct _GstGLWindowWin32EGLClass   GstGLWindowWin32EGLClass;

struct _GstGLWindowWin32EGL {
  /*< private >*/
  GstGLWindowWin32 parent;

  EGLNativeWindowType internal_win_id;
  EGLDisplay display;
  EGLSurface surface;
  EGLContext egl_context;
  EGLContext external_gl_context;

  gpointer _reserved[GST_PADDING];
};

struct _GstGLWindowWin32EGLClass {
  /*< private >*/
  GstGLWindowWin32Class parent_class;

  /*< private >*/
  gpointer _reserved[GST_PADDING];
};

GType gst_gl_window_win32_egl_get_type     (void);

GstGLWindowWin32EGL * gst_gl_window_win32_egl_new  (void);

G_END_DECLS

#endif /* __GST_GL_WINDOW_X11_H__ */
