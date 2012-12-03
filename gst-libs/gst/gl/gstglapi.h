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
#ifdef HAVE_GLES2
# include <GLES2/gl2.h>
# if !HAVE_OPENGL
#  include "gstgles2.h"
# endif
#endif

/* OpenGL for usual systems */
#if HAVE_OPENGL
# if __APPLE__
#  include <GL/glew.h>
#  include <OpenGL/OpenGL.h>
#  include <OpenGL/gl.h>
# else
#  include <GL/glew.h>
#  include <GL/gl.h>
# endif
#endif

#if HAVE_GLX
# include <GL/glx.h>
#endif

#if HAVE_EGL
# undef UNICODE
# include <EGL/egl.h>
# define UNICODE
#endif

#include <gst/gst.h>

G_BEGIN_DECLS

typedef enum {
  GST_GL_API_NONE = 0,
  GST_GL_API_OPENGL = (1 << 0),
  GST_GL_API_OPENGL3 = (1 << 1),
  GST_GL_API_GLES = (1 << 15),
  GST_GL_API_GLES2 = (1 << 16),
  GST_GL_API_GLES3 = (1 << 17),

  GST_GL_API_ANY = G_MAXUINT32
} GstGLAPI;

typedef enum
{
  GST_GL_PLATFORM_UNKNOWN = 0,
  GST_GL_PLATFORM_EGL,
  GST_GL_PLATFORM_GLX,
  GST_GL_PLATFORM_WGL,
  GST_GL_PLATFORM_CGL,

  GST_GL_PLATFORM_ANY = 254,
  GST_GL_PLATFORM_LAST = 255
} GstGLPlatform;

G_END_DECLS

#endif /* __GST_GL_WINDOW_H__ */
