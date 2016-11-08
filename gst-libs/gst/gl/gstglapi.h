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

#include <gst/gl/gstglconfig.h>

/* This mimic GCC behaviour with system headers files even if GL headers may
 * not be in the system header path. */
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wredundant-decls"
#endif

/* OpenGL 2.0 for Embedded Systems */
#if GST_GL_HAVE_GLES2
# if GST_GL_HAVE_PLATFORM_EAGL
#  include <OpenGLES/ES2/gl.h>
#  include <OpenGLES/ES2/glext.h>
# else
#  if GST_GL_HAVE_GLES3
#   include <GLES3/gl3.h>
#   include <GLES3/gl3ext.h>
#  else
#   include <GLES2/gl2.h>
#   include <GLES2/gl2ext.h>
#  endif
# endif
# if !GST_GL_HAVE_OPENGL
#  include <gst/gl/glprototypes/gstgl_gles2compat.h>
# endif
#endif

/* OpenGL for desktop systems */
#if GST_GL_HAVE_OPENGL
# ifdef __APPLE__
#  include <OpenGL/OpenGL.h>
#  include <OpenGL/gl.h>
#  if MAC_OS_X_VERSION_MAX_ALLOWED >= 1070
#   define GL_DO_NOT_WARN_IF_MULTI_GL_VERSION_HEADERS_INCLUDED
#   include <OpenGL/gl3.h>
#  endif
# else
#  if defined(_MSC_VER)
#   include <windows.h>
#  endif
#  include <GL/gl.h>
#  if defined(__WIN32__) || defined(_WIN32)
#   include <GL/glext.h>
#  endif
# endif
#endif

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#if defined(WINAPI)
#define GSTGLAPI WINAPI
#else
#define GSTGLAPI
#endif
#include <gst/gl/glprototypes/gstgl_compat.h>

#include <gst/gst.h>

G_BEGIN_DECLS

/**
 * GstGLAPI:
 * @GST_GL_API_NONE: no API
 * @GST_GL_API_OPENGL: Desktop OpenGL up to and including 3.1.  The
 *                    compatibility profile when the OpenGL version is >= 3.2
 * @GST_GL_API_OPENGL3: Desktop OpenGL >= 3.2 core profile
 * @GST_GL_API_GLES1: OpenGL ES 1.x
 * @GST_GL_API_GLES2: OpenGL ES 2.x and 3.x
 * @GST_GL_API_ANY: Any OpenGL API
 */
typedef enum {
  GST_GL_API_NONE = 0,
  GST_GL_API_OPENGL = (1 << 0),
  GST_GL_API_OPENGL3 = (1 << 1),
  GST_GL_API_GLES1 = (1 << 15),
  GST_GL_API_GLES2 = (1 << 16),

  GST_GL_API_ANY = G_MAXUINT32
} GstGLAPI;

/**
 * GST_GL_API_OPENGL_NAME:
 *
 * The name for %GST_GL_API_OPENGL used in various places
 */
#define GST_GL_API_OPENGL_NAME "opengl"

/**
 * GST_GL_API_OPENGL3_NAME:
 *
 * The name for %GST_GL_API_OPENGL3 used in various places
 */
#define GST_GL_API_OPENGL3_NAME "opengl3"

/**
 * GST_GL_API_GLES1_NAME:
 *
 * The name for %GST_GL_API_GLES1 used in various places
 */
#define GST_GL_API_GLES1_NAME "gles1"

/**
 * GST_GL_API_GLES2_NAME:
 *
 * The name for %GST_GL_API_GLES2 used in various places
 */
#define GST_GL_API_GLES2_NAME "gles2"

/**
 * GstGLPlatform:
 * @GST_GL_PLATFORM_NONE: no platform
 * @GST_GL_PLATFORM_EGL: the EGL platform used primarily with the X11, wayland
 *                      and android window systems as well as on embedded Linux
 * @GST_GL_PLATFORM_GLX: the GLX platform used primarily with the X11 window system
 * @GST_GL_PLATFORM_WGL: the WGL platform used primarily on Windows
 * @GST_GL_PLATFORM_CGL: the CGL platform used primarily on OS X
 * @GST_GL_PLATFORM_EAGL: the EAGL platform used primarily on iOS
 * @GST_GL_PLATFORM_ANY: any OpenGL platform
 */
typedef enum
{
  GST_GL_PLATFORM_NONE = 0,
  GST_GL_PLATFORM_EGL = (1 << 0),
  GST_GL_PLATFORM_GLX = (1 << 1),
  GST_GL_PLATFORM_WGL = (1 << 2),
  GST_GL_PLATFORM_CGL = (1 << 3),
  GST_GL_PLATFORM_EAGL = (1 << 4),

  GST_GL_PLATFORM_ANY = G_MAXUINT32
} GstGLPlatform;

#define GST_GL_EXT_BEGIN(name, gl_availability, min_gl, maj_gl, gles_maj, \
    gles_min, ext_suf, ext_name)
#define GST_GL_EXT_FUNCTION(ret, name, args) \
  ret (GSTGLAPI *name) args;
#define GST_GL_EXT_END()

typedef struct _GstGLFuncs
{
#include <gst/gl/glprototypes/all_functions.h>
  gpointer padding[GST_PADDING_LARGE*6];
} GstGLFuncs;

#undef GST_GL_EXT_BEGIN
#undef GST_GL_EXT_FUNCTION
#undef GST_GL_EXT_END

GST_EXPORT
gchar * gst_gl_api_to_string (GstGLAPI api);
GST_EXPORT
GstGLAPI gst_gl_api_from_string (const gchar * api_s);

GST_EXPORT
gchar * gst_gl_platform_to_string (GstGLPlatform platform);
GST_EXPORT
GstGLPlatform gst_gl_platform_from_string (const gchar * platform_s);

G_END_DECLS

#endif /* __GST_GL_API_H__ */
