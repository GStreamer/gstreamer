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

#ifndef __GST_GL_API_H__
#define __GST_GL_API_H__

#include "gstglconfig.h"

/* OpenGL 2.0 for Embedded Systems */
#if GST_GL_HAVE_GLES2
# include <GLES2/gl2.h>
# include <GLES2/gl2ext.h>
# if !GST_GL_HAVE_OPENGL
#  include "gstgles2.h"
# endif
#endif

/* OpenGL for desktop systems */
#if GST_GL_HAVE_OPENGL
# if __APPLE__
#  include <OpenGL/OpenGL.h>
#  include <OpenGL/gl.h>
# else
#  include <GL/glu.h>
#  include <GL/gl.h>
#  if __WIN32__
#   include <GL/glext.h>
#  endif
# endif
#endif

#include "glprototypes/gstgl_compat.h"

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
  GST_GL_PLATFORM_EGL = (1 << 0),
  GST_GL_PLATFORM_GLX = (1 << 1),
  GST_GL_PLATFORM_WGL = (1 << 2),
  GST_GL_PLATFORM_CGL = (1 << 3),

  GST_GL_PLATFORM_ANY = G_MAXUINT32
} GstGLPlatform;

#define GST_GL_EXT_BEGIN(name, min_gl, maj_gl, in_gles, ext_suf, ext_name)
#define GST_GL_EXT_FUNCTION(ret, name, args) \
  ret (*name) args;
#define GST_GL_EXT_END()

typedef struct _GstGLFuncs
{
#include "glprototypes/opengl.h"
  gpointer padding1[GST_PADDING_LARGE];
#include "glprototypes/gles1.h"
  gpointer padding2[GST_PADDING_LARGE];
#include "glprototypes/gles1gles2.h"
  gpointer padding3[GST_PADDING_LARGE];
#include "glprototypes/gles1opengl.h"
  gpointer padding4[GST_PADDING_LARGE*2];
#include "glprototypes/gles2.h"
  gpointer padding5[GST_PADDING_LARGE*2];
#include "glprototypes/gles2opengl.h"
  gpointer padding6[GST_PADDING_LARGE*2];
#include "glprototypes/gles1gles2opengl.h"
  gpointer padding7[GST_PADDING_LARGE*2];
#include "glprototypes/gles3opengl.h"
  gpointer padding8[GST_PADDING_LARGE*4];
} GstGLFuncs;

#undef GST_GL_EXT_BEGIN
#undef GST_GL_EXT_FUNCTION
#undef GST_GL_EXT_END

gchar * gst_gl_api_string (GstGLAPI api);

G_END_DECLS

#endif /* __GST_GL_API_H__ */
