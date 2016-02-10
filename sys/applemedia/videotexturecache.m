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
#include <gst/gl/gstglbufferpool.h>
#include "iosurfacememory.h"
#endif
#include "videotexturecache.h"
#include "coremediabuffer.h"
#include "corevideobuffer.h"
#include "vtutil.h"

typedef struct _ContextThreadData
{
  GstVideoTextureCache *cache;
  GstBuffer *input_buffer;
  GstBuffer *output_buffer;
} ContextThreadData;

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
      (CVEAGLContext) gst_gl_context_get_gl_context (ctx), NULL, &cache->cache);
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
  gst_caps_features_add (features, GST_CAPS_FEATURE_MEMORY_GL_MEMORY);
  gst_video_info_from_caps (&cache->output_info, out_caps);

  in_caps = gst_caps_copy (out_caps);
  gst_caps_set_simple (in_caps, "format",
          G_TYPE_STRING, gst_video_format_to_string (in_format), NULL);
  features = gst_caps_get_features (in_caps, 0);
  gst_caps_features_add (features, GST_CAPS_FEATURE_MEMORY_GL_MEMORY);
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

#if HAVE_IOS
static void
_do_get_gl_buffer (GstGLContext * context, ContextThreadData * data)
{
  CVOpenGLESTextureRef texture = NULL;
  GstVideoTextureCache *cache = data->cache;
  CVPixelBufferRef pixel_buf = cv_pixel_buffer_from_gst_buffer (data->input_buffer);
  GstGLTextureTarget gl_target;
  GstGLBaseMemoryAllocator *base_mem_alloc;
  GstGLVideoAllocationParams *params;
  GstBuffer *output_buffer;

  base_mem_alloc = GST_GL_BASE_MEMORY_ALLOCATOR (gst_gl_memory_allocator_get_default (cache->ctx));
  output_buffer = gst_buffer_new ();
  gst_buffer_copy_into (output_buffer, data->input_buffer, GST_BUFFER_COPY_ALL, 0, -1);

  switch (GST_VIDEO_INFO_FORMAT (&cache->input_info)) {
      case GST_VIDEO_FORMAT_BGRA:
        /* avfvideosrc does BGRA on iOS when doing GLMemory */
        if (CVOpenGLESTextureCacheCreateTextureFromImage (kCFAllocatorDefault,
              cache->cache, pixel_buf, NULL, GL_TEXTURE_2D, GL_RGBA,
              GST_VIDEO_INFO_WIDTH (&cache->input_info),
              GST_VIDEO_INFO_HEIGHT (&cache->input_info),
              GL_RGBA, GL_UNSIGNED_BYTE, 0, &texture) != kCVReturnSuccess)
          goto error;

        gl_target = gst_gl_texture_target_from_gl (CVOpenGLESTextureGetTarget (texture));
        params = gst_gl_video_allocation_params_new_wrapped_texture (cache->ctx,
            NULL, &cache->input_info, 0, NULL, gl_target,
            CVOpenGLESTextureGetName (texture), texture,
            (GDestroyNotify) CFRelease);

        gst_buffer_replace_memory (output_buffer, 0,
                (GstMemory *) gst_gl_base_memory_alloc (base_mem_alloc,
                    (GstGLAllocationParams *) params));
        gst_gl_allocation_params_free ((GstGLAllocationParams *) params);
        break;
      case GST_VIDEO_FORMAT_NV12: {
        GstVideoGLTextureType textype;
        GLenum texifmt, texfmt;

        textype = gst_gl_texture_type_from_format (cache->ctx, GST_VIDEO_FORMAT_NV12, 0);
        texifmt = gst_gl_format_from_gl_texture_type (textype);
        texfmt = gst_gl_sized_gl_format_from_gl_format_type (cache->ctx, texifmt, GL_UNSIGNED_BYTE);

        /* vtdec does NV12 on iOS when doing GLMemory */
        if (CVOpenGLESTextureCacheCreateTextureFromImage (kCFAllocatorDefault,
              cache->cache, pixel_buf, NULL, GL_TEXTURE_2D, texifmt,
              GST_VIDEO_INFO_WIDTH (&cache->input_info),
              GST_VIDEO_INFO_HEIGHT (&cache->input_info),
              texfmt, GL_UNSIGNED_BYTE, 0, &texture) != kCVReturnSuccess)
          goto error;

        gl_target = gst_gl_texture_target_from_gl (CVOpenGLESTextureGetTarget (texture));
        params = gst_gl_video_allocation_params_new_wrapped_texture (cache->ctx,
            NULL, &cache->input_info, 0, NULL, gl_target,
            CVOpenGLESTextureGetName (texture), texture,
            (GDestroyNotify) CFRelease);

        gst_buffer_replace_memory (output_buffer, 0,
                (GstMemory *) gst_gl_base_memory_alloc (base_mem_alloc,
                    (GstGLAllocationParams *) params));
        gst_gl_allocation_params_free ((GstGLAllocationParams *) params);

        textype = gst_gl_texture_type_from_format (cache->ctx, GST_VIDEO_FORMAT_NV12, 1);
        texifmt = gst_gl_format_from_gl_texture_type (textype);
        texfmt = gst_gl_sized_gl_format_from_gl_format_type (cache->ctx, texifmt, GL_UNSIGNED_BYTE);

        if (CVOpenGLESTextureCacheCreateTextureFromImage (kCFAllocatorDefault,
              cache->cache, pixel_buf, NULL, GL_TEXTURE_2D, texifmt,
              GST_VIDEO_INFO_WIDTH (&cache->input_info) / 2,
              GST_VIDEO_INFO_HEIGHT (&cache->input_info) / 2,
              texfmt, GL_UNSIGNED_BYTE, 1, &texture) != kCVReturnSuccess)
          goto error;

        gl_target = gst_gl_texture_target_from_gl (CVOpenGLESTextureGetTarget (texture));
        params = gst_gl_video_allocation_params_new_wrapped_texture (cache->ctx,
            NULL, &cache->input_info, 1, NULL, gl_target,
            CVOpenGLESTextureGetName (texture), texture,
            (GDestroyNotify) CFRelease);

        gst_buffer_replace_memory (output_buffer, 1,
                (GstMemory *) gst_gl_base_memory_alloc (base_mem_alloc,
                    (GstGLAllocationParams *) params));
        gst_gl_allocation_params_free ((GstGLAllocationParams *) params);
        break;
      }
    default:
      g_warn_if_reached ();
      goto error;
  }

  gst_object_unref (base_mem_alloc);

  data->output_buffer = output_buffer;

  return;

error:
  data->output_buffer = NULL;
}
#else /* !HAVE_IOS */
static void
_do_get_gl_buffer (GstGLContext * context, ContextThreadData * data)
{
  GstVideoTextureCache *cache = data->cache;
  CVPixelBufferRef pixel_buf = cv_pixel_buffer_from_gst_buffer (data->input_buffer);
  IOSurfaceRef surface = CVPixelBufferGetIOSurface (pixel_buf);

  data->output_buffer = gst_buffer_new ();
  gst_buffer_copy_into (data->output_buffer, data->input_buffer, GST_BUFFER_COPY_ALL, 0, -1);
  for (int i = 0; i < GST_VIDEO_INFO_N_PLANES (&cache->input_info); i++) {
    GstIOSurfaceMemory *mem;

    CFRetain (pixel_buf);
    mem = gst_io_surface_memory_wrapped (cache->ctx,
            surface, GST_GL_TEXTURE_TARGET_RECTANGLE, &cache->input_info,
            i, NULL, pixel_buf, (GDestroyNotify) CFRelease);

    gst_buffer_replace_memory (data->output_buffer, i, (GstMemory *) mem);
  }
}
#endif

GstBuffer *
gst_video_texture_cache_get_gl_buffer (GstVideoTextureCache * cache,
        GstBuffer * cv_buffer)
{
  ContextThreadData data = {cache, cv_buffer, NULL};

  gst_gl_context_thread_add (cache->ctx,
      (GstGLContextThreadFunc) _do_get_gl_buffer, &data);

  gst_buffer_unref (cv_buffer);

  return data.output_buffer;
}
