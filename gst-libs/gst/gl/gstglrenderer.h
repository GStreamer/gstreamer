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

#ifndef __GST_GL_RENDERER_H__
#define __GST_GL_RENDERER_H__

/* OpenGL 2.0 for Embedded Systems */
#ifdef GST_GL_RENDERER_GLES2
# undef UNICODE
# include <EGL/egl.h>
# define UNICODE
# include <GLES2/gl2.h>
# include "gstgles2.h"
/* OpenGL for usual systems */
#endif
#if GST_GL_RENDERER_GL || GST_GL_RENDERER_GL3
# if __APPLE__
#  include <GL/glew.h>
#  include <OpenGL/OpenGL.h>
#  include <OpenGL/gl.h>
# else
# if HAVE_GLEW
#  include <GL/glew.h>
# endif
# include <GL/gl.h>
# endif
#endif

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_GL_TYPE_RENDERER         (gst_gl_renderer_get_type())
#define GST_GL_RENDERER(o)           (G_TYPE_CHECK_INSTANCE_CAST((o), GST_GL_TYPE_RENDERER, GstGLRenderer))
#define GST_GL_RENDERER_CLASS(k)     (G_TYPE_CHECK_CLASS((k), GST_GL_TYPE_RENDERER, GstGLRendererClass))
#define GST_GL_IS_RENDERER(o)        (G_TYPE_CHECK_INSTANCE_TYPE((o), GST_GL_TYPE_RENDERER))
#define GST_GL_IS_RENDERER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE((k), GST_GL_TYPE_RENDERER))
#define GST_GL_RENDERER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS((o), GST_GL_TYPE_RENDERER, GstGLRendererClass))

#define GST_GL_RENDERER_ERROR (gst_gl_renderer_error_quark ())

typedef struct _GstGLRenderer        GstGLRenderer;
typedef struct _GstGLRendererPrivate GstGLRendererPrivate;
typedef struct _GstGLRendererClass   GstGLRendererClass;

typedef enum {
  GST_GL_RENDERER_API_OPENGL = 1,
  GST_GL_RENDERER_API_OPENGL3 = 2,
  GST_GL_RENDERER_API_GLES = 40,
  GST_GL_RENDERER_API_GLES2 = 41,
  GST_GL_RENDERER_API_GLES3 = 42,

  GST_GL_RENDERER_API_LAST = 255
} GstGLRendererAPI;

struct _GstGLRenderer {
  /*< private >*/
  GObject parent;

  /*< public >*/
  GstGLRendererAPI renderer_api;

  /*< private >*/
  gpointer _reserved[GST_PADDING_LARGE];
};

struct _GstGLRendererClass {
  /*< private >*/
  GObjectClass parent_class;

  /*< private >*/
  gpointer _reserved[GST_PADDING_LARGE];
};

/* methods */

GQuark gst_gl_renderer_error_quark (void);
GType gst_gl_renderer_get_type     (void);

GstGLRenderer * gst_gl_renderer_new  ();

GstGLRendererAPI gst_gl_renderer_get_renderer_api (GstGLRenderer *renderer);

G_END_DECLS

#endif /* __GST_GL_WINDOW_H__ */
