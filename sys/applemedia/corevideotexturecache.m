/*
 * Copyright (C) 2010 Ole André Vadla Ravnås <oleavr@soundrop.com>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#if !HAVE_IOS
#import <AppKit/AppKit.h>
#include <gst/gl/cocoa/gstglcontext_cocoa.h>
#endif
#include "corevideotexturecache.h"
#include "coremediabuffer.h"
#include "corevideobuffer.h"

GstCoreVideoTextureCache *
gst_core_video_texture_cache_new (GstGLContext * ctx)
{
  g_return_val_if_fail (ctx != NULL, NULL);

  GstCoreVideoTextureCache *cache = g_new0 (GstCoreVideoTextureCache, 1);
  cache->ctx = gst_object_ref (ctx);
  gst_video_info_init (&cache->input_info);
  cache->convert = gst_gl_color_convert_new (cache->ctx);

#if !HAVE_IOS
  CGLPixelFormatObj pixelFormat =
      gst_gl_context_cocoa_get_pixel_format (GST_GL_CONTEXT_COCOA (ctx));
  CGLContextObj platform_ctx =
      (CGLContextObj) gst_gl_context_get_gl_context (ctx);
  CVOpenGLTextureCacheCreate (kCFAllocatorDefault, NULL, platform_ctx,
      pixelFormat, NULL, &cache->cache);
#else
  CVOpenGLESTextureCacheCreate (kCFAllocatorDefault, NULL,
      (CVEAGLContext) gst_gl_context_get_gl_context (ctx), NULL, &cache->cache);
#endif

  return cache;
}

void
gst_core_video_texture_cache_free (GstCoreVideoTextureCache * cache)
{
  g_return_if_fail (cache != NULL);

#if !HAVE_IOS
  CVOpenGLTextureCacheRelease (cache->cache);
#else
  /* FIXME: how do we release ->cache ? */
#endif
  gst_object_unref (cache->convert);
  gst_object_unref (cache->ctx);
  g_free (cache);
}

void
gst_core_video_texture_cache_set_format (GstCoreVideoTextureCache * cache,
    const gchar * input_format, GstCaps * out_caps)
{
  GstCaps *in_caps;
  GstCapsFeatures *features;

  g_return_if_fail (gst_caps_is_fixed (out_caps));

  out_caps = gst_caps_copy (out_caps);
  features = gst_caps_get_features (out_caps, 0);
  gst_caps_features_add (features, GST_CAPS_FEATURE_MEMORY_GL_MEMORY);
  gst_video_info_from_caps (&cache->output_info, out_caps); 
  
  in_caps = gst_caps_copy (out_caps);
  gst_caps_set_simple (in_caps, "format", G_TYPE_STRING, input_format, NULL);
  features = gst_caps_get_features (in_caps, 0);
  gst_caps_features_add (features, GST_CAPS_FEATURE_MEMORY_GL_MEMORY);
  gst_video_info_from_caps (&cache->input_info, in_caps);

  gst_gl_color_convert_set_caps (cache->convert, in_caps, out_caps);

  gst_caps_unref (out_caps);
  gst_caps_unref (in_caps);
}

CFTypeRef
texture_from_buffer (GstCoreVideoTextureCache * cache,
    GstBuffer * buffer, GLuint * texture_id, GLuint * texture_target)
{
#if !HAVE_IOS
  CVOpenGLTextureRef texture = NULL;
#else
  CVOpenGLESTextureRef texture = NULL;
#endif
  GstCoreMediaMeta *cm_meta =
      (GstCoreMediaMeta *) gst_buffer_get_meta (buffer,
      gst_core_media_meta_api_get_type ());
  GstCoreVideoMeta *cv_meta =
      (GstCoreVideoMeta *) gst_buffer_get_meta (buffer,
      gst_core_video_meta_api_get_type ());
  CVPixelBufferRef pixel_buf;
  if (cm_meta)
    pixel_buf = cm_meta->pixel_buf;
  else
    pixel_buf = cv_meta->pixbuf;
#if !HAVE_IOS
  CVOpenGLTextureCacheCreateTextureFromImage (kCFAllocatorDefault,
      cache->cache, pixel_buf, NULL, &texture);
  *texture_id = CVOpenGLTextureGetName (texture);
  *texture_target = CVOpenGLTextureGetTarget (texture);
  CVOpenGLTextureCacheFlush (cache->cache, 0);
#else
  CVOpenGLESTextureCacheCreateTextureFromImage (kCFAllocatorDefault,
      cache->cache, pixel_buf, NULL, GL_TEXTURE_2D, GL_RGBA,
      GST_VIDEO_INFO_WIDTH (&cache->input_info),
      GST_VIDEO_INFO_HEIGHT (&cache->input_info),
      GL_RGBA, GL_UNSIGNED_BYTE, 0, &texture);
  *texture_id = CVOpenGLESTextureGetName (texture);
  *texture_target = CVOpenGLESTextureGetTarget (texture);
  CVOpenGLESTextureCacheFlush (cache->cache, 0);
#endif

  return (CFTypeRef) texture;
}

GstBuffer *
gst_core_video_texture_cache_get_gl_buffer (GstCoreVideoTextureCache * cache,
        GstBuffer * cv_buffer)
{
  const GstGLFuncs *gl;
  GstBuffer *rgb_buffer;
  CFTypeRef texture;
  GLuint texture_id, texture_target;
  GstMemory *memory;

  gl = cache->ctx->gl_vtable;
  texture = texture_from_buffer (cache, cv_buffer, &texture_id, &texture_target);
  memory = (GstMemory *) gst_gl_memory_wrapped_texture (cache->ctx, texture_id, texture_target,
      &cache->input_info, 0, NULL, NULL, NULL);
  gst_buffer_append_memory (cv_buffer, memory);
  rgb_buffer = gst_gl_color_convert_perform (cache->convert, cv_buffer);
  gst_buffer_unref (cv_buffer);
  CFRelease (texture);

  return rgb_buffer;
}
