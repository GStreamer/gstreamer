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
#include "vtutil.h"

typedef struct _ContextThreadData
{
  GstCoreVideoTextureCache *cache;
  GstBuffer *input_buffer;
  GstBuffer *output_buffer;
} ContextThreadData;

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
  CFMutableDictionaryRef cache_attrs =
      CFDictionaryCreateMutable (NULL, 0, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks);
  gst_vtutil_dict_set_i32 (cache_attrs,
      kCVOpenGLESTextureCacheMaximumTextureAgeKey, 0);
  CVOpenGLESTextureCacheCreate (kCFAllocatorDefault, (CFDictionaryRef) cache_attrs,
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
  CFRelease (cache->cache); /* iOS has no "CVOpenGLESTextureCacheRelease" */
#endif
  gst_object_unref (cache->convert);
  gst_object_unref (cache->ctx);
  g_free (cache);
}

void
gst_core_video_texture_cache_set_format (GstCoreVideoTextureCache * cache,
    GstVideoFormat in_format, GstCaps * out_caps)
{
  GstCaps *in_caps;
  GstCapsFeatures *features;

  g_return_if_fail (gst_caps_is_fixed (out_caps));

  out_caps = gst_caps_copy (out_caps);
  features = gst_caps_get_features (out_caps, 0);
  gst_caps_features_add (features, GST_CAPS_FEATURE_MEMORY_GL_MEMORY);
  gst_video_info_from_caps (&cache->output_info, out_caps); 
  
  in_caps = gst_caps_copy (out_caps);
  gst_caps_set_simple (in_caps, "format",
          G_TYPE_STRING, gst_video_format_to_string (in_format), NULL);
  features = gst_caps_get_features (in_caps, 0);
  gst_caps_features_add (features, GST_CAPS_FEATURE_MEMORY_GL_MEMORY);
  gst_video_info_from_caps (&cache->input_info, in_caps);

  gst_gl_color_convert_set_caps (cache->convert, in_caps, out_caps);

  gst_caps_unref (out_caps);
  gst_caps_unref (in_caps);
}

static CVPixelBufferRef
cv_pixel_buffer_from_gst_buffer (GstBuffer * buffer)
{
  GstCoreMediaMeta *cm_meta =
      (GstCoreMediaMeta *) gst_buffer_get_meta (buffer,
      gst_core_media_meta_api_get_type ());
  GstCoreVideoMeta *cv_meta =
      (GstCoreVideoMeta *) gst_buffer_get_meta (buffer,
      gst_core_video_meta_api_get_type ());

  g_return_val_if_fail (cm_meta || cv_meta, NULL);

  return cm_meta ? cm_meta->pixel_buf : cv_meta->pixbuf;
}

static gboolean
gl_mem_from_buffer (GstCoreVideoTextureCache * cache,
        GstBuffer * buffer, GstMemory **mem1, GstMemory **mem2)
{
  gboolean ret = TRUE;
#if !HAVE_IOS
  CVOpenGLTextureRef texture = NULL;
#else
  CVOpenGLESTextureRef texture = NULL;
#endif
  CVPixelBufferRef pixel_buf = cv_pixel_buffer_from_gst_buffer (buffer);

  *mem1 = NULL;
  *mem2 = NULL;

#if !HAVE_IOS
  CVOpenGLTextureCacheFlush (cache->cache, 0);
#else
  CVOpenGLESTextureCacheFlush (cache->cache, 0);
#endif

  switch (GST_VIDEO_INFO_FORMAT (&cache->input_info)) {
#if !HAVE_IOS
      case GST_VIDEO_FORMAT_UYVY:
        /* both avfvideosrc and vtdec on OSX when doing GLMemory negotiate UYVY
         * under the hood, which means a single output texture. */
        if (CVOpenGLTextureCacheCreateTextureFromImage (kCFAllocatorDefault,
              cache->cache, pixel_buf, NULL, &texture) != kCVReturnSuccess)
          goto error;

        *mem1 = (GstMemory *) gst_gl_memory_wrapped_texture (cache->ctx,
            CVOpenGLTextureGetName (texture), CVOpenGLTextureGetTarget (texture),
            &cache->input_info, 0, NULL, texture, (GDestroyNotify) CFRelease);
        break;
#else
      case GST_VIDEO_FORMAT_BGRA:
        /* avfvideosrc does BGRA on iOS when doing GLMemory */
        if (CVOpenGLESTextureCacheCreateTextureFromImage (kCFAllocatorDefault,
              cache->cache, pixel_buf, NULL, GL_TEXTURE_2D, GL_RGBA,
              GST_VIDEO_INFO_WIDTH (&cache->input_info),
              GST_VIDEO_INFO_HEIGHT (&cache->input_info),
              GL_RGBA, GL_UNSIGNED_BYTE, 0, &texture) != kCVReturnSuccess)
          goto error;

        *mem1 = (GstMemory *) gst_gl_memory_wrapped_texture (cache->ctx,
            CVOpenGLESTextureGetName (texture), CVOpenGLESTextureGetTarget (texture),
            &cache->input_info, 0, NULL, texture, (GDestroyNotify) CFRelease);
        break;
      case GST_VIDEO_FORMAT_NV12:
        /* vtdec does NV12 on iOS when doing GLMemory */
        if (CVOpenGLESTextureCacheCreateTextureFromImage (kCFAllocatorDefault,
              cache->cache, pixel_buf, NULL, GL_TEXTURE_2D, GL_LUMINANCE,
              GST_VIDEO_INFO_WIDTH (&cache->input_info),
              GST_VIDEO_INFO_HEIGHT (&cache->input_info),
              GL_LUMINANCE, GL_UNSIGNED_BYTE, 0, &texture) != kCVReturnSuccess)
          goto error;

        *mem1 = (GstMemory *) gst_gl_memory_wrapped_texture (cache->ctx,
            CVOpenGLESTextureGetName (texture), CVOpenGLESTextureGetTarget (texture),
            &cache->input_info, 0, NULL, texture, (GDestroyNotify) CFRelease);

        if (CVOpenGLESTextureCacheCreateTextureFromImage (kCFAllocatorDefault,
              cache->cache, pixel_buf, NULL, GL_TEXTURE_2D, GL_LUMINANCE_ALPHA,
              GST_VIDEO_INFO_WIDTH (&cache->input_info) / 2,
              GST_VIDEO_INFO_HEIGHT (&cache->input_info) / 2,
              GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, 1, &texture) != kCVReturnSuccess)
          goto error;

        *mem2 = (GstMemory *) gst_gl_memory_wrapped_texture (cache->ctx,
            CVOpenGLESTextureGetName (texture), CVOpenGLESTextureGetTarget (texture),
            &cache->input_info, 0, NULL, texture, (GDestroyNotify) CFRelease);
        break;
#endif
      default:
        g_warn_if_reached ();
        ret = FALSE;
    }

  return ret;

error:
  ret = FALSE;

  if (*mem1)
      gst_memory_unref (*mem1);
  if (*mem2)
      gst_memory_unref (*mem2);

  return ret;
}

static void
_do_get_gl_buffer (GstGLContext * context, ContextThreadData * data)
{
  GstMemory *mem1 = NULL, *mem2 = NULL;
  GstCoreVideoTextureCache *cache = data->cache;
  GstBuffer *buffer = data->input_buffer;

  if (!gl_mem_from_buffer (cache, buffer, &mem1, &mem2)) {
    gst_buffer_unref (buffer);
    data->output_buffer = NULL;
    return;
  }

  gst_buffer_append_memory (buffer, mem1);
  if (mem2)
    gst_buffer_append_memory (buffer, mem2);

  data->output_buffer = gst_gl_color_convert_perform (cache->convert, buffer);
  gst_buffer_unref (buffer);
}

GstBuffer *
gst_core_video_texture_cache_get_gl_buffer (GstCoreVideoTextureCache * cache,
        GstBuffer * cv_buffer)
{
  ContextThreadData data = {cache, cv_buffer, NULL};
  gst_gl_context_thread_add (cache->ctx,
      (GstGLContextThreadFunc) _do_get_gl_buffer, &data);
  return data.output_buffer;
}
