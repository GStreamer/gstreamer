/*
 * Copyright (C) 2009 Ole André Vadla Ravnås <oravnas@cisco.com>
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

#include "coremediabuffer.h"

G_DEFINE_TYPE (GstCoreMediaBuffer, gst_core_media_buffer, GST_TYPE_BUFFER);

static void
gst_core_media_buffer_init (GstCoreMediaBuffer * self)
{
  GST_BUFFER_FLAG_SET (self, GST_BUFFER_FLAG_READONLY);
}

static void
gst_core_media_buffer_finalize (GstMiniObject * mini_object)
{
  GstCoreMediaBuffer *self = GST_CORE_MEDIA_BUFFER_CAST (mini_object);

  if (self->image_buf != NULL) {
    GstCVApi *cv = self->ctx->cv;
    cv->CVPixelBufferUnlockBaseAddress (self->image_buf,
        kCVPixelBufferLock_ReadOnly);
  }
  self->ctx->cm->FigSampleBufferRelease (self->sample_buf);
  g_object_unref (self->ctx);

  GST_MINI_OBJECT_CLASS (gst_core_media_buffer_parent_class)->finalize
      (mini_object);
}

GstBuffer *
gst_core_media_buffer_new (GstCoreMediaCtx * ctx, CMSampleBufferRef sample_buf)
{
  GstCVApi *cv = ctx->cv;
  GstCMApi *cm = ctx->cm;
  CVImageBufferRef image_buf;
  CVPixelBufferRef pixel_buf;
  CMBlockBufferRef block_buf;
  Byte *data = NULL;
  UInt32 size;
  OSStatus status;
  GstCoreMediaBuffer *buf;

  image_buf = cm->CMSampleBufferGetImageBuffer (sample_buf);
  pixel_buf = NULL;
  block_buf = cm->CMSampleBufferGetDataBuffer (sample_buf);

  if (image_buf != NULL &&
      CFGetTypeID (image_buf) == cv->CVPixelBufferGetTypeID ()) {
    pixel_buf = (CVPixelBufferRef) image_buf;

    if (cv->CVPixelBufferLockBaseAddress (pixel_buf,
            kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) {
      goto error;
    }

    if (cv->CVPixelBufferIsPlanar (pixel_buf)) {
      gint plane_count, plane_idx;

      data = cv->CVPixelBufferGetBaseAddressOfPlane (pixel_buf, 0);

      size = 0;
      plane_count = cv->CVPixelBufferGetPlaneCount (pixel_buf);
      for (plane_idx = 0; plane_idx != plane_count; plane_idx++) {
        size += cv->CVPixelBufferGetBytesPerRowOfPlane (pixel_buf, plane_idx) *
            cv->CVPixelBufferGetHeightOfPlane (pixel_buf, plane_idx);
      }
    } else {
      data = cv->CVPixelBufferGetBaseAddress (pixel_buf);
      size = cv->CVPixelBufferGetBytesPerRow (pixel_buf) *
          cv->CVPixelBufferGetHeight (pixel_buf);
    }
  } else if (block_buf != NULL) {
    status = cm->CMBlockBufferGetDataPointer (block_buf, 0, 0, 0, &data);
    if (status != noErr)
      goto error;
    size = cm->CMBlockBufferGetDataLength (block_buf);
  } else {
    goto error;
  }

  buf =
      GST_CORE_MEDIA_BUFFER (gst_mini_object_new (GST_TYPE_CORE_MEDIA_BUFFER));
  buf->ctx = g_object_ref (ctx);
  buf->sample_buf = cm->FigSampleBufferRetain (sample_buf);
  buf->image_buf = image_buf;
  buf->pixel_buf = pixel_buf;
  buf->block_buf = block_buf;

  GST_BUFFER_DATA (buf) = data;
  GST_BUFFER_SIZE (buf) = size;

  return GST_BUFFER_CAST (buf);

error:
  return NULL;
}

CVPixelBufferRef
gst_core_media_buffer_get_pixel_buffer (GstCoreMediaBuffer * buf)
{
  return buf->ctx->cv->CVPixelBufferRetain (buf->pixel_buf);
}

static void
gst_core_media_buffer_class_init (GstCoreMediaBufferClass * klass)
{
  GstMiniObjectClass *miniobject_class = GST_MINI_OBJECT_CLASS (klass);

  miniobject_class->finalize = gst_core_media_buffer_finalize;
}
