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
  gst_object_unref (cache->ctx);
  g_free (cache);
}

gboolean
gst_core_video_texture_cache_upload (GstVideoGLTextureUploadMeta * meta,
    guint texture_id[4])
{
  g_return_val_if_fail (meta != NULL, FALSE);

  GstCoreVideoTextureCache *cache =
      (GstCoreVideoTextureCache *) meta->user_data;
  const GstGLFuncs *gl = cache->ctx->gl_vtable;
#if !HAVE_IOS
  CVOpenGLTextureRef texture = NULL;
#else
  CVOpenGLESTextureRef texture = NULL;
#endif
  GstVideoMeta *video_meta = gst_buffer_get_video_meta (meta->buffer);
  GstCoreMediaMeta *cm_meta =
      (GstCoreMediaMeta *) gst_buffer_get_meta (meta->buffer,
      gst_core_media_meta_api_get_type ());
  GstCoreVideoMeta *cv_meta =
      (GstCoreVideoMeta *) gst_buffer_get_meta (meta->buffer,
      gst_core_video_meta_api_get_type ());
  CVPixelBufferRef pixel_buf;
  if (cm_meta)
    pixel_buf = cm_meta->pixel_buf;
  else
    pixel_buf = cv_meta->pixbuf;
#if !HAVE_IOS
  CVOpenGLTextureCacheCreateTextureFromImage (kCFAllocatorDefault,
      cache->cache, pixel_buf, NULL, &texture);
#else
  CVOpenGLESTextureCacheCreateTextureFromImage (kCFAllocatorDefault,
      cache->cache, cm_meta->image_buf, NULL, GL_TEXTURE_2D,
      GL_RGBA, video_meta->width, video_meta->height, GL_RGBA, GL_UNSIGNED_BYTE,
      0, &texture);
#endif
  GLuint fboId;
  gl->GenFramebuffers (1, &fboId);
  gl->BindFramebuffer (GL_FRAMEBUFFER, fboId);

  gl->FramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
#if !HAVE_IOS
      CVOpenGLTextureGetTarget (texture), CVOpenGLTextureGetName (texture),
#else
      CVOpenGLESTextureGetTarget (texture), CVOpenGLESTextureGetName (texture),
#endif
      0);
  gl->BindTexture (GL_TEXTURE_2D, texture_id[0]);
  gl->CopyTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, 0, 0, video_meta->width,
      video_meta->height, 0);
  gl->BindTexture (GL_TEXTURE_2D, 0);
  gl->BindFramebuffer (GL_FRAMEBUFFER, 0);
  gl->DeleteFramebuffers (1, &fboId);

  return TRUE;
}
