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

#ifndef _GST_GL_FORMAT_H_
#define _GST_GL_FORMAT_H_

#include <gst/gst.h>

#include <gst/gl/gstgl_fwd.h>
#include <gst/video/video.h>

/**
 * GST_GL_TEXTURE_TARGET_2D_STR:
 *
 * String used for %GST_GL_TEXTURE_TARGET_2D in things like caps values
 */
#define GST_GL_TEXTURE_TARGET_2D_STR "2D"

/**
 * GST_GL_TEXTURE_TARGET_RECTANGLE_STR:
 *
 * String used for %GST_GL_TEXTURE_TARGET_RECTANGLE in things like caps values
 */
#define GST_GL_TEXTURE_TARGET_RECTANGLE_STR "rectangle"

/**
 * GST_GL_TEXTURE_TARGET_EXTERNAL_OES_STR:
 *
 * String used for %GST_GL_TEXTURE_TARGET_EXTERNAL_OES in things like caps values
 */
#define GST_GL_TEXTURE_TARGET_EXTERNAL_OES_STR "external-oes"

/**
 * GST_BUFFER_POOL_OPTION_GL_TEXTURE_TARGET_2D:
 *
 * String used for %GST_GL_TEXTURE_TARGET_2D as a #GstBufferPool pool option
 */
#define GST_BUFFER_POOL_OPTION_GL_TEXTURE_TARGET_2D "GstBufferPoolOptionGLTextureTarget2D"

/**
 * GST_BUFFER_POOL_OPTION_GL_TEXTURE_TARGET_RECTANGLE:
 *
 * String used for %GST_GL_TEXTURE_TARGET_RECTANGLE as a #GstBufferPool pool option
 */
#define GST_BUFFER_POOL_OPTION_GL_TEXTURE_TARGET_RECTANGLE "GstBufferPoolOptionGLTextureTargetRectangle"

/**
 * GST_BUFFER_POOL_OPTION_GL_TEXTURE_TARGET_EXTERNAL_OES:
 *
 * String used for %GST_GL_TEXTURE_TARGET_EXTERNAL_OES as a #GstBufferPool pool option
 */
#define GST_BUFFER_POOL_OPTION_GL_TEXTURE_TARGET_EXTERNAL_OES "GstBufferPoolOptionGLTextureTargetExternalOES"

G_BEGIN_DECLS

typedef enum
{
  /* values taken from the GL headers */
  GST_GL_LUMINANCE                      = 0x1909,

  GST_GL_ALPHA                          = 0x1906,

  GST_GL_LUMINANCE_ALPHA                = 0x190A,

  GST_GL_RED                            = 0x1903,
  GST_GL_R8                             = 0x8229,

  GST_GL_RG                             = 0x8227,
  GST_GL_RG8                            = 0x822B,

  GST_GL_RGB                            = 0x1907,
  GST_GL_RGB8                           = 0x8051,
  GST_GL_RGB565                         = 0x8D62,

  GST_GL_RGBA                           = 0x1908,
  GST_GL_RGBA8                          = 0x8058,

  GST_GL_DEPTH_COMPONENT16              = 0x81A5,

  GST_GL_DEPTH24_STENCIL8               = 0x88F0,
} GstGLFormat;

GST_EXPORT
guint                   gst_gl_format_type_n_bytes                  (guint format,
                                                                     guint type);
GST_EXPORT
GstGLFormat             gst_gl_format_from_video_info               (GstGLContext * context,
                                                                     GstVideoInfo * vinfo,
                                                                     guint plane);
GST_EXPORT
guint                   gst_gl_sized_gl_format_from_gl_format_type  (GstGLContext * context,
                                                                     guint format,
                                                                     guint type);

GST_EXPORT
GstGLTextureTarget      gst_gl_texture_target_from_string           (const gchar * str);
GST_EXPORT
const gchar *           gst_gl_texture_target_to_string             (GstGLTextureTarget target);
GST_EXPORT
guint                   gst_gl_texture_target_to_gl                 (GstGLTextureTarget target);
GST_EXPORT
GstGLTextureTarget      gst_gl_texture_target_from_gl               (guint target);
GST_EXPORT
const gchar *           gst_gl_texture_target_to_buffer_pool_option (GstGLTextureTarget target);

G_END_DECLS

#endif /* _GST_GL_FORMAT_H_ */
