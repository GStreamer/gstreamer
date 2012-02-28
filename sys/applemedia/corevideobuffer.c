/*
 * Copyright (C) 2010 Ole André Vadla Ravnås <oravnas@cisco.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "corevideobuffer.h"

static void
gst_core_video_meta_free (GstCoreVideoMeta * meta, GstBuffer * buf)
{
  GstCVApi *cv = meta->ctx->cv;

  if (meta->pixbuf != NULL) {
    cv->CVPixelBufferUnlockBaseAddress (meta->pixbuf,
        kCVPixelBufferLock_ReadOnly);
  }

  cv->CVBufferRelease (meta->cvbuf);
  g_object_unref (meta->ctx);
}

static const GstMetaInfo *
gst_core_video_meta_get_info (void)
{
  static const GstMetaInfo *core_video_meta_info = NULL;

  if (core_video_meta_info == NULL) {
    core_video_meta_info = gst_meta_register ("GstCoreVideoeMeta",
        "GstCoreVideoMeta", sizeof (GstCoreVideoMeta),
        (GstMetaInitFunction) NULL,
        (GstMetaFreeFunction) gst_core_video_meta_free,
        (GstMetaTransformFunction) NULL);
  }
  return core_video_meta_info;
}

GstBuffer *
gst_core_video_buffer_new (GstCoreMediaCtx * ctx, CVBufferRef cvbuf)
{
  GstCVApi *cv = ctx->cv;
  void *data;
  size_t size;
  CVPixelBufferRef pixbuf = NULL;
  GstBuffer *buf;
  GstCoreVideoMeta *meta;

  if (CFGetTypeID (cvbuf) == cv->CVPixelBufferGetTypeID ()) {
    pixbuf = (CVPixelBufferRef) cvbuf;

    if (cv->CVPixelBufferLockBaseAddress (pixbuf,
            kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) {
      goto error;
    }
    data = cv->CVPixelBufferGetBaseAddress (pixbuf);
    size = cv->CVPixelBufferGetBytesPerRow (pixbuf) *
        cv->CVPixelBufferGetHeight (pixbuf);
  } else {
    /* TODO: Do we need to handle other buffer types? */
    goto error;
  }

  buf = gst_buffer_new ();
  meta = (GstCoreVideoMeta *) gst_buffer_add_meta (buf,
      gst_core_video_meta_get_info (), NULL);
  meta->ctx = g_object_ref (ctx);
  meta->cvbuf = cv->CVBufferRetain (cvbuf);
  meta->pixbuf = pixbuf;
  gst_buffer_take_memory (buf, -1,
      gst_memory_new_wrapped (GST_MEMORY_FLAG_NO_SHARE, data,
          size, 0, size, NULL, NULL));

  return buf;

error:
  return NULL;
}
