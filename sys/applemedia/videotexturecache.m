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
#include "iosurfacememory.h"
#endif
#include "iosglmemory.h"
#include "videotexturecache.h"
#include "coremediabuffer.h"
#include "corevideobuffer.h"
#include "vtutil.h"

typedef struct _ContextThreadData
{
  GstVideoTextureCache *cache;
  GstAppleCoreVideoPixelBuffer *gpixbuf;
  guint plane;
  gsize size;
  GstMemory *memory;
} ContextThreadData;

typedef struct _TextureWrapper
{
#if HAVE_IOS
    CVOpenGLESTextureCacheRef cache;
    CVOpenGLESTextureRef texture;
#else
    CVOpenGLTextureCacheRef cache;
    CVOpenGLTextureRef texture;
#endif

} TextureWrapper;

GstVideoTextureCache *
gst_video_texture_cache_new (GstGLContext * ctx)
{
  g_return_val_if_fail (ctx != NULL, NULL);

  GstVideoTextureCache *cache = g_new0 (GstVideoTextureCache, 1);

  cache->ctx = gst_object_ref (ctx);
  gst_video_info_init (&cache->input_info);

#if HAVE_IOS
  CFMutableDictionaryRef cache_attrs =
      CFDictionaryCreateMutable (NULL, 0, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks);
  CVOpenGLESTextureCacheCreate (kCFAllocatorDefault, (CFDictionaryRef) cache_attrs,
      (__bridge CVEAGLContext) (gpointer)gst_gl_context_get_gl_context (ctx), NULL, &cache->cache);
#else
  gst_ios_surface_memory_init ();
#if 0
  cache->pool = GST_BUFFER_POOL (gst_gl_buffer_pool_new (ctx));
#endif
#endif

  return cache;
}

void
gst_video_texture_cache_free (GstVideoTextureCache * cache)
{
  g_return_if_fail (cache != NULL);

#if HAVE_IOS
  CFRelease (cache->cache); /* iOS has no "CVOpenGLESTextureCacheRelease" */
#else
#if 0
  gst_buffer_pool_set_active (cache->pool, FALSE);
  gst_object_unref (cache->pool);
#endif
#endif
  gst_object_unref (cache->ctx);
  if (cache->in_caps)
    gst_caps_unref (cache->in_caps);
  if (cache->out_caps)
    gst_caps_unref (cache->out_caps);
  g_free (cache);
}

void
gst_video_texture_cache_set_format (GstVideoTextureCache * cache,
    GstVideoFormat in_format, GstCaps * out_caps)
{
  GstCaps *in_caps;
  GstCapsFeatures *features;

  g_return_if_fail (gst_caps_is_fixed (out_caps));

  out_caps = gst_caps_copy (out_caps);
  features = gst_caps_get_features (out_caps, 0);
  gst_video_info_from_caps (&cache->output_info, out_caps);

  in_caps = gst_caps_copy (out_caps);
  gst_caps_set_simple (in_caps, "format",
          G_TYPE_STRING, gst_video_format_to_string (in_format), NULL);
  features = gst_caps_get_features (in_caps, 0);
  gst_video_info_from_caps (&cache->input_info, in_caps);

  if (cache->in_caps)
    gst_caps_unref (cache->in_caps);
  if (cache->out_caps)
    gst_caps_unref (cache->out_caps);
  cache->in_caps = in_caps;
  cache->out_caps = out_caps;

#if 0
  GstStructure *config = gst_buffer_pool_get_config (cache->pool);
  gst_buffer_pool_config_set_params (config, cache->in_caps,
          GST_VIDEO_INFO_SIZE (&cache->input_info), 0, 0);
  gst_buffer_pool_config_set_allocator (config,
          gst_allocator_find (GST_IO_SURFACE_MEMORY_ALLOCATOR_NAME), NULL);
  gst_buffer_pool_config_add_option (config,
          GST_BUFFER_POOL_OPTION_GL_TEXTURE_TARGET_RECTANGLE);
  gst_buffer_pool_set_config (cache->pool, config);
  gst_buffer_pool_set_active (cache->pool, TRUE);
#endif
}

#if HAVE_IOS
void gst_video_texture_cache_release_texture(TextureWrapper *data) {
    CFRelease(data->texture);
    CFRelease(data->cache);
    g_free(data);
}

static void
_do_create_memory (GstGLContext * context, ContextThreadData * data)
{
  CVOpenGLESTextureRef texture = NULL;
  GstVideoTextureCache *cache = data->cache;
  GstAppleCoreVideoPixelBuffer *gpixbuf = data->gpixbuf;
  CVPixelBufferRef pixel_buf = gpixbuf->buf;
  guint plane = data->plane;
  gssize size = data->size;
  GstGLTextureTarget gl_target;
  GstAppleCoreVideoMemory *memory;
  GstIOSGLMemory *gl_memory;
  GstGLFormat texformat;

  switch (GST_VIDEO_INFO_FORMAT (&cache->input_info)) {
      case GST_VIDEO_FORMAT_BGRA:
        if (CVOpenGLESTextureCacheCreateTextureFromImage (kCFAllocatorDefault,
              cache->cache, pixel_buf, NULL, GL_TEXTURE_2D, GL_RGBA,
              GST_VIDEO_INFO_WIDTH (&cache->input_info),
              GST_VIDEO_INFO_HEIGHT (&cache->input_info),
              GL_RGBA, GL_UNSIGNED_BYTE, 0, &texture) != kCVReturnSuccess)
          goto error;

        texformat = GST_GL_RGBA;
        plane = 0;
        goto success;
      case GST_VIDEO_FORMAT_NV12: {
        GstGLFormat texifmt, texfmt;

        if (plane == 0)
          texformat = GST_GL_LUMINANCE;
        else
          texformat = GST_GL_LUMINANCE_ALPHA;
        texfmt = gst_gl_sized_gl_format_from_gl_format_type (cache->ctx, texformat, GL_UNSIGNED_BYTE);

        if (CVOpenGLESTextureCacheCreateTextureFromImage (kCFAllocatorDefault,
              cache->cache, pixel_buf, NULL, GL_TEXTURE_2D, texformat,
              GST_VIDEO_INFO_COMP_WIDTH (&cache->input_info, plane),
              GST_VIDEO_INFO_COMP_HEIGHT (&cache->input_info, plane),
              texfmt, GL_UNSIGNED_BYTE, plane, &texture) != kCVReturnSuccess)
          goto error;

        goto success;
      }
    default:
      g_warn_if_reached ();
      goto error;
  }

success: {
  TextureWrapper *texture_data = g_new(TextureWrapper, 1);
  CFRetain(cache->cache);
  texture_data->cache = cache->cache;
  texture_data->texture = texture;
  gl_target = gst_gl_texture_target_from_gl (CVOpenGLESTextureGetTarget (texture));
  memory = gst_apple_core_video_memory_new_wrapped (gpixbuf, plane, size);
  gl_memory = gst_ios_gl_memory_new_wrapped (context, memory,
      gl_target, texformat, CVOpenGLESTextureGetName (texture), &cache->input_info,
     plane, NULL, texture_data, (GDestroyNotify) gst_video_texture_cache_release_texture);

  data->memory = GST_MEMORY_CAST (gl_memory);

  return;
}

error:
  data->memory = NULL;
}
#endif

GstMemory *
gst_video_texture_cache_create_memory (GstVideoTextureCache * cache,
      GstAppleCoreVideoPixelBuffer *gpixbuf,
      guint plane,
      gsize size)
{
  ContextThreadData data = {cache, gpixbuf, plane, size, NULL};

#if HAVE_IOS
  gst_gl_context_thread_add (cache->ctx,
      (GstGLContextThreadFunc) _do_create_memory, &data);
#endif

  return data.memory;
}
