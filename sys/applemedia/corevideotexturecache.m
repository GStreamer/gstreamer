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
#endif
#include "corevideotexturecache.h"
#include "coremediabuffer.h"

GstCoreVideoTextureCache *
gst_core_video_texture_cache_new (GstGLContext * ctx)
{
  CVReturn err;

  g_return_val_if_fail (ctx != NULL, NULL);

  GstCoreVideoTextureCache *cache = g_new0 (GstCoreVideoTextureCache, 1);
  cache->ctx = gst_object_ref (ctx);

#if !HAVE_IOS
  CGLPixelFormatAttribute attribs[1] = { 0 };
  int numPixelFormats;
  CGLPixelFormatObj pixelFormat;
  CGLChoosePixelFormat (attribs, &pixelFormat, &numPixelFormats); // 5
  NSOpenGLContext *platform_ctx = (NSOpenGLContext *) gst_gl_context_get_gl_context (ctx);
  err = CVOpenGLTextureCacheCreate (kCFAllocatorDefault, NULL,
      [platform_ctx CGLContextObj], pixelFormat,
      NULL, &cache->cache);
#else
  err = CVOpenGLESTextureCacheCreate (kCFAllocatorDefault, NULL,
      (CVEAGLContext) gst_gl_context_get_gl_context (ctx),
      NULL, &cache->cache);
#endif

  return cache;
}

void gst_core_video_texture_cache_free (GstCoreVideoTextureCache * cache)
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
gst_core_video_texture_cache_upload (GstVideoGLTextureUploadMeta * meta, guint texture_id[4])
{
  g_return_val_if_fail (meta != NULL, FALSE);

  GstCoreVideoTextureCache *cache = (GstCoreVideoTextureCache *) meta->user_data;
  const GstGLFuncs *gl = cache->ctx->gl_vtable;
#if !HAVE_IOS
  CVOpenGLTextureRef texture = NULL;
#else
  CVOpenGLESTextureRef texture = NULL;
#endif
  GstVideoMeta *video_meta = gst_buffer_get_video_meta (meta->buffer);
  GstCoreMediaMeta *cv_meta = (GstCoreMediaMeta *) gst_buffer_get_meta (meta->buffer,
          gst_core_media_meta_api_get_type ());
#if !HAVE_IOS
  CVOpenGLTextureCacheCreateTextureFromImage (kCFAllocatorDefault,
          cache->cache, cv_meta->pixel_buf, NULL, &texture);
#else
  CVOpenGLESTextureCacheCreateTextureFromImage (kCFAllocatorDefault,
          cache->cache, cv_meta->image_buf, NULL, GL_TEXTURE_2D,
          GL_RGBA, video_meta->width, video_meta->height, GL_RGBA, GL_UNSIGNED_BYTE,
          0, &texture);
#endif
  GLuint fboId;
  gl->GenFramebuffers (1, &fboId);
  gl->BindFramebuffer (GL_FRAMEBUFFER, fboId);

  gl->FramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
#if !HAVE_IOS
      CVOpenGLTextureGetTarget(texture), CVOpenGLTextureGetName(texture),
#else
      CVOpenGLESTextureGetTarget(texture), CVOpenGLESTextureGetName(texture),
#endif
      0);
  gl->BindTexture (GL_TEXTURE_2D, texture_id[0]);
  gl->CopyTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, 0, 0, video_meta->width, video_meta->height, 0);
  gl->BindTexture (GL_TEXTURE_2D, 0);
  gl->BindFramebuffer (GL_FRAMEBUFFER, 0);
  gl->DeleteFramebuffers (1, &fboId);

  return TRUE;
}
