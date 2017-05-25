/*
 * GStreamer
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
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

/**
 * SECTION:gstglformat
 * @title: GstGlFormat
 * @short_description: utilities for dealing with OpenGL formats
 * @see_also: #GstGLBaseMemory, #GstGLMemory, #GstGLFramebuffer, #GstGLBuffer
 *
 * Some useful utilities for converting between various formats and OpenGL
 * formats.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gl/gstglformat.h>
#include <gst/gl/gstglcontext.h>

#define USING_OPENGL(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL, 1, 0))
#define USING_OPENGL3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL3, 3, 1))
#define USING_GLES(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES, 1, 0))
#define USING_GLES2(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 2, 0))
#define USING_GLES3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 3, 0))

#ifndef GL_TEXTURE_RECTANGLE
#define GL_TEXTURE_RECTANGLE 0x84F5
#endif
#ifndef GL_TEXTURE_EXTERNAL_OES
#define GL_TEXTURE_EXTERNAL_OES 0x8D65
#endif

static inline guint
_gl_format_n_components (guint format)
{
  switch (format) {
    case GST_VIDEO_GL_TEXTURE_TYPE_RGBA:
    case GST_GL_RGBA:
      return 4;
    case GST_VIDEO_GL_TEXTURE_TYPE_RGB:
    case GST_VIDEO_GL_TEXTURE_TYPE_RGB16:
    case GST_GL_RGB:
    case GST_GL_RGB565:
      return 3;
    case GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE_ALPHA:
    case GST_VIDEO_GL_TEXTURE_TYPE_RG:
    case GST_GL_LUMINANCE_ALPHA:
    case GST_GL_RG:
      return 2;
    case GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE:
    case GST_VIDEO_GL_TEXTURE_TYPE_R:
    case GST_GL_LUMINANCE:
    case GST_GL_RED:
      return 1;
    default:
      return 0;
  }
}

static inline guint
_gl_type_n_components (guint type)
{
  switch (type) {
    case GL_UNSIGNED_BYTE:
      return 1;
    case GL_UNSIGNED_SHORT_5_6_5:
      return 3;
    default:
      g_assert_not_reached ();
      return 0;
  }
}

static inline guint
_gl_type_n_bytes (guint type)
{
  switch (type) {
    case GL_UNSIGNED_BYTE:
      return 1;
    case GL_UNSIGNED_SHORT_5_6_5:
      return 2;
    default:
      g_assert_not_reached ();
      return 0;
  }
}

/**
 * gst_gl_format_type_n_bytes:
 * @format: the OpenGL format, %GL_RGBA, %GL_LUMINANCE, etc
 * @type: the OpenGL type, %GL_UNSIGNED_BYTE, %GL_FLOAT, etc
 *
 * Returns: the number of bytes the specified @format, @type combination takes
 * per pixel
 */
guint
gst_gl_format_type_n_bytes (guint format, guint type)
{
  return _gl_format_n_components (format) / _gl_type_n_components (type) *
      _gl_type_n_bytes (type);
}

/**
 * gst_gl_format_from_video_info:
 * @context: a #GstGLContext
 * @vinfo: a #GstVideoInfo
 * @plane: the plane number in @vinfo
 *
 * Returns: the #GstGLFormat necessary for holding the data in @plane of @vinfo
 */
GstGLFormat
gst_gl_format_from_video_info (GstGLContext * context, GstVideoInfo * vinfo,
    guint plane)
{
  gboolean texture_rg =
      gst_gl_context_check_feature (context, "GL_EXT_texture_rg")
      || gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 3, 0)
      || gst_gl_context_check_feature (context, "GL_ARB_texture_rg")
      || gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL3, 3, 0);
  GstVideoFormat v_format = GST_VIDEO_INFO_FORMAT (vinfo);
  guint n_plane_components;

  switch (v_format) {
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_AYUV:
      n_plane_components = 4;
      break;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
      n_plane_components = 3;
      break;
    case GST_VIDEO_FORMAT_RGB16:
    case GST_VIDEO_FORMAT_BGR16:
      return GST_GL_RGB565;
    case GST_VIDEO_FORMAT_GRAY16_BE:
    case GST_VIDEO_FORMAT_GRAY16_LE:
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
      n_plane_components = 2;
      break;
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
      n_plane_components = plane == 0 ? 1 : 2;
      break;
    case GST_VIDEO_FORMAT_GRAY8:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      n_plane_components = 1;
      break;
    default:
      n_plane_components = 4;
      g_assert_not_reached ();
      break;
  }

  switch (n_plane_components) {
    case 4:
      return GST_GL_RGBA;
      break;
    case 3:
      return GST_GL_RGB;
      break;
    case 2:
      return texture_rg ? GST_GL_RG : GST_GL_LUMINANCE_ALPHA;
      break;
    case 1:
      return texture_rg ? GST_GL_RED : GST_GL_LUMINANCE;
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return GST_GL_RGBA;
}

/**
 * gst_gl_sized_gl_format_from_gl_format_type:
 * @context: a #GstGLContext
 * @format: an OpenGL format, %GL_RGBA, %GL_LUMINANCE, etc
 * @type: an OpenGL type, %GL_UNSIGNED_BYTE, %GL_FLOAT, etc
 *
 * Returns: the sized internal format specified by @format and @type that can
 *          be used in @context
 */
guint
gst_gl_sized_gl_format_from_gl_format_type (GstGLContext * context,
    guint format, guint type)
{
  gboolean ext_texture_rg =
      gst_gl_context_check_feature (context, "GL_EXT_texture_rg");

  switch (format) {
    case GST_GL_RGBA:
      switch (type) {
        case GL_UNSIGNED_BYTE:
          return USING_GLES2 (context)
              && !USING_GLES3 (context) ? GST_GL_RGBA : GST_GL_RGBA8;
          break;
      }
      break;
    case GST_GL_RGB:
      switch (type) {
        case GL_UNSIGNED_BYTE:
          return USING_GLES2 (context)
              && !USING_GLES3 (context) ? GST_GL_RGB : GST_GL_RGB8;
          break;
        case GL_UNSIGNED_SHORT_5_6_5:
          return GST_GL_RGB565;
          break;
      }
      break;
    case GST_GL_RG:
      switch (type) {
        case GL_UNSIGNED_BYTE:
          if (!USING_GLES3 (context) && USING_GLES2 (context) && ext_texture_rg)
            return GST_GL_RG;
          return GST_GL_RG8;
          break;
      }
      break;
    case GST_GL_RED:
      switch (type) {
        case GL_UNSIGNED_BYTE:
          if (!USING_GLES3 (context) && USING_GLES2 (context) && ext_texture_rg)
            return GST_GL_RED;
          return GST_GL_R8;
          break;
      }
      break;
    case GST_GL_RGBA8:
    case GST_GL_RGB8:
    case GST_GL_RGB565:
    case GST_GL_RG8:
    case GST_GL_R8:
    case GST_GL_LUMINANCE:
    case GST_GL_LUMINANCE_ALPHA:
    case GST_GL_ALPHA:
    case GST_GL_DEPTH_COMPONENT16:
    case GST_GL_DEPTH24_STENCIL8:
      return format;
    default:
      break;
  }

  g_assert_not_reached ();
  return 0;
}

/**
 * gst_gl_texture_target_to_string:
 * @target: a #GstGLTextureTarget
 *
 * Returns: the stringified version of @target or %NULL
 */
const gchar *
gst_gl_texture_target_to_string (GstGLTextureTarget target)
{
  switch (target) {
    case GST_GL_TEXTURE_TARGET_2D:
      return GST_GL_TEXTURE_TARGET_2D_STR;
    case GST_GL_TEXTURE_TARGET_RECTANGLE:
      return GST_GL_TEXTURE_TARGET_RECTANGLE_STR;
    case GST_GL_TEXTURE_TARGET_EXTERNAL_OES:
      return GST_GL_TEXTURE_TARGET_EXTERNAL_OES_STR;
    default:
      return NULL;
  }
}

/**
 * gst_gl_texture_target_from_string:
 * @str: a string equivalant to one of the GST_GL_TEXTURE_TARGET_*_STR values
 *
 * Returns: the #GstGLTextureTarget represented by @str or
 *          %GST_GL_TEXTURE_TARGET_NONE
 */
GstGLTextureTarget
gst_gl_texture_target_from_string (const gchar * str)
{
  if (!str)
    return GST_GL_TEXTURE_TARGET_NONE;

  if (g_strcmp0 (str, GST_GL_TEXTURE_TARGET_2D_STR) == 0)
    return GST_GL_TEXTURE_TARGET_2D;
  if (g_strcmp0 (str, GST_GL_TEXTURE_TARGET_RECTANGLE_STR) == 0)
    return GST_GL_TEXTURE_TARGET_RECTANGLE;
  if (g_strcmp0 (str, GST_GL_TEXTURE_TARGET_EXTERNAL_OES_STR) == 0)
    return GST_GL_TEXTURE_TARGET_EXTERNAL_OES;

  return GST_GL_TEXTURE_TARGET_NONE;
}

/**
 * gst_gl_texture_target_to_gl:
 * @target: a #GstGLTextureTarget
 *
 * Returns: the OpenGL value for binding the @target with glBindTexture() and
 *          similar functions or 0
 */
guint
gst_gl_texture_target_to_gl (GstGLTextureTarget target)
{
  switch (target) {
    case GST_GL_TEXTURE_TARGET_2D:
      return GL_TEXTURE_2D;
    case GST_GL_TEXTURE_TARGET_RECTANGLE:
      return GL_TEXTURE_RECTANGLE;
    case GST_GL_TEXTURE_TARGET_EXTERNAL_OES:
      return GL_TEXTURE_EXTERNAL_OES;
    default:
      return 0;
  }
}

/**
 * gst_gl_texture_target_from_gl:
 * @target: an OpenGL texture binding target
 *
 * Returns: the #GstGLTextureTarget that's equiavalant to @target or
 *          %GST_GL_TEXTURE_TARGET_NONE
 */
GstGLTextureTarget
gst_gl_texture_target_from_gl (guint target)
{
  switch (target) {
    case GL_TEXTURE_2D:
      return GST_GL_TEXTURE_TARGET_2D;
    case GL_TEXTURE_RECTANGLE:
      return GST_GL_TEXTURE_TARGET_RECTANGLE;
    case GL_TEXTURE_EXTERNAL_OES:
      return GST_GL_TEXTURE_TARGET_EXTERNAL_OES;
    default:
      return GST_GL_TEXTURE_TARGET_NONE;
  }
}

/**
 * gst_gl_texture_target_to_buffer_pool_option:
 * @target: a #GstGLTextureTarget
 *
 * Returns: a string representing the @GstBufferPoolOption specified by @target
 */
const gchar *
gst_gl_texture_target_to_buffer_pool_option (GstGLTextureTarget target)
{
  switch (target) {
    case GST_GL_TEXTURE_TARGET_2D:
      return GST_BUFFER_POOL_OPTION_GL_TEXTURE_TARGET_2D;
    case GST_GL_TEXTURE_TARGET_RECTANGLE:
      return GST_BUFFER_POOL_OPTION_GL_TEXTURE_TARGET_RECTANGLE;
    case GST_GL_TEXTURE_TARGET_EXTERNAL_OES:
      return GST_BUFFER_POOL_OPTION_GL_TEXTURE_TARGET_EXTERNAL_OES;
    default:
      return NULL;
  }
}
