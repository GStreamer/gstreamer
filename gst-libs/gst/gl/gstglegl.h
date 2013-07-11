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

#ifndef __GST_GL_EGL_H__
#define __GST_GL_EGL_H__

#include <gst/gst.h>
#include <EGL/egl.h>

#include <gst/gl/gstgl_fwd.h>

G_BEGIN_DECLS

typedef struct _GstGLEGL GstGLEGL;

struct _GstGLEGL {
  EGLContext egl_context;
  EGLDisplay egl_display;
  EGLSurface egl_surface;
  EGLConfig  egl_config;

  GstGLAPI gl_api;
};

GstGLEGL * gst_gl_egl_create_context (EGLDisplay display, EGLNativeWindowType window, GstGLAPI gl_api, guintptr external_gl_context, GError ** error);
void gst_gl_egl_destroy_context (GstGLEGL *egl);

gboolean gst_gl_egl_activate (GstGLEGL *egl, gboolean activate);
void gst_gl_egl_swap_buffers (GstGLEGL *egl);

guintptr gst_gl_egl_get_gl_context (GstGLEGL *egl);
GstGLAPI gst_gl_egl_get_gl_api (GstGLEGL *egl);

gpointer gst_gl_egl_get_proc_address (GstGLEGL *egl, const gchar * name);

G_END_DECLS

#endif /* __GST_GL_EGL_H__ */
