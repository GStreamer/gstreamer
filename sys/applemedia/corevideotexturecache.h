/*
 * Copyright (C) 2015 Alessandro Decina <twi@centricular.com>
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

#ifndef __GST_CORE_VIDEO_TEXTURE_CACHE_H__
#define __GST_CORE_VIDEO_TEXTURE_CACHE_H__

#include <gst/video/gstvideometa.h>
#include <gst/gl/gstglcontext.h>
#include "CoreVideo/CoreVideo.h"

G_BEGIN_DECLS

typedef struct _GstCoreVideoTextureCache
{
  GstGLContext *ctx;
#if !HAVE_IOS
  CVOpenGLTextureCacheRef cache;
#else
  CVOpenGLESTextureCacheRef cache;
#endif
  GstVideoInfo input_info;
  GstVideoInfo output_info;
  GstGLColorConvert *convert;
} GstCoreVideoTextureCache;

GstCoreVideoTextureCache *gst_core_video_texture_cache_new (GstGLContext * ctx);
void gst_core_video_texture_cache_free (GstCoreVideoTextureCache * cache);
void gst_core_video_texture_cache_set_format (GstCoreVideoTextureCache * cache,
    GstVideoFormat in_format, GstCaps * out_caps);
gboolean gst_core_video_texture_cache_upload (GstVideoGLTextureUploadMeta * meta, guint texture_id[4]);
GstBuffer * gst_core_video_texture_cache_get_gl_buffer (GstCoreVideoTextureCache * cache,
        GstBuffer * cv_buffer);

G_END_DECLS

#endif /* __GST_CORE_VIDEO_TEXTURE_CACHE_H__ */
