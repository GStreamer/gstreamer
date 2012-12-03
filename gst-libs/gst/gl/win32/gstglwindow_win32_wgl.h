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

#ifndef __GST_GL_WINDOW_WIN32_WGL_H__
#define __GST_GL_WINDOW_WIN32_WGL_H__

#include "gstglwindow_win32.h"

#undef UNICODE
#include <windows.h>
#define UNICODE

G_BEGIN_DECLS

#define GST_GL_TYPE_WINDOW_WIN32_WGL         (gst_gl_window_win32_wgl_get_type())
#define GST_GL_WINDOW_WIN32_WGL(o)           (G_TYPE_CHECK_INSTANCE_CAST((o), GST_GL_TYPE_WINDOW_WIN32_WGL, GstGLWindowWin32WGL))
#define GST_GL_WINDOW_WIN32_WGL_CLASS(k)     (G_TYPE_CHECK_CLASS((k), GST_GL_TYPE_WINDOW_WIN32_WGL, GstGLWindowWin32WGLClass))
#define GST_GL_IS_WINDOW_WIN32_WGL(o)        (G_TYPE_CHECK_INSTANCE_TYPE((o), GST_GL_TYPE_WINDOW_WIN32_WGL))
#define GST_GL_IS_WINDOW_WIN32_WGL_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE((k), GST_GL_TYPE_WINDOW_WIN32_WGL))
#define GST_GL_WINDOW_WIN32_WGL_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS((o), GST_GL_TYPE_WINDOW_WIN32_WGL, GstGLWindowWin32WGLClass))

typedef struct _GstGLWindowWin32WGL        GstGLWindowWin32WGL;
typedef struct _GstGLWindowWin32WGLClass   GstGLWindowWin32WGLClass;

struct _GstGLWindowWin32WGL {
  /*< private >*/
  GstGLWindowWin32 parent;

  HGLRC wgl_context;
  HGLRC external_gl_context;

  gpointer _reserved[GST_PADDING];
};

struct _GstGLWindowWin32WGLClass {
  /*< private >*/
  GstGLWindowWin32Class parent_class;

  /*< private >*/
  gpointer _reserved[GST_PADDING];
};

GType gst_gl_window_win32_wgl_get_type     (void);

GstGLWindowWin32WGL * gst_gl_window_win32_wgl_new  (GstGLAPI gl_api,
                                                    guintptr external_gl_context);

G_END_DECLS

#endif /* __GST_GL_WINDOW_X11_H__ */
