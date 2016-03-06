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
 * String used for %GST_GL_TEXTURE_TARGET_ESTERNAL_OES as a #GstBufferPool pool option
 */
#define GST_BUFFER_POOL_OPTION_GL_TEXTURE_TARGET_EXTERNAL_OES "GstBufferPoolOptionGLTextureTargetExternalOES"

G_BEGIN_DECLS

guint                   gst_gl_format_type_n_bytes                  (guint format,
                                                                     guint type);
guint                   gst_gl_texture_type_n_bytes                 (GstVideoGLTextureType tex_format);
guint                   gst_gl_format_from_gl_texture_type          (GstVideoGLTextureType tex_format);
GstVideoGLTextureType   gst_gl_texture_type_from_format             (GstGLContext * context,
                                                                     GstVideoFormat v_format,
                                                                     guint plane);
guint                   gst_gl_sized_gl_format_from_gl_format_type  (GstGLContext * context,
                                                                     guint format,
                                                                     guint type);

GstGLTextureTarget      gst_gl_texture_target_from_string           (const gchar * str);
const gchar *           gst_gl_texture_target_to_string             (GstGLTextureTarget target);
guint                   gst_gl_texture_target_to_gl                 (GstGLTextureTarget target);
GstGLTextureTarget      gst_gl_texture_target_from_gl               (guint target);
const gchar *           gst_gl_texture_target_to_buffer_pool_option (GstGLTextureTarget target);

G_END_DECLS

#endif /* _GST_GL_FORMAT_H_ */
