/*
 * Copyright (C) 2009 Ole André Vadla Ravnås <oleavr@soundrop.com>
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

#include "coremediabuffer.h"

static void
gst_core_media_meta_free (GstCoreMediaMeta * meta, GstBuffer * buf)
{
  if (meta->image_buf != NULL) {
    CVPixelBufferUnlockBaseAddress (meta->image_buf,
        kCVPixelBufferLock_ReadOnly);
    CVBufferRelease (meta->image_buf);
  }
  if (meta->block_buf != NULL) {
    CFRelease (meta->block_buf);
  }
  CVBufferRelease ((CVBufferRef)meta->sample_buf);
}

GType
gst_core_media_meta_api_get_type (void)
{
  static volatile GType type;
  static const gchar *tags[] = { "memory", NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstCoreMediaMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

static const GstMetaInfo *
gst_core_media_meta_get_info (void)
{
  static const GstMetaInfo *core_media_meta_info = NULL;

  if (g_once_init_enter (&core_media_meta_info)) {
    const GstMetaInfo *meta = gst_meta_register (GST_CORE_MEDIA_META_API_TYPE,
        "GstCoreMediaMeta", sizeof (GstCoreMediaMeta),
        (GstMetaInitFunction) NULL,
        (GstMetaFreeFunction) gst_core_media_meta_free,
        (GstMetaTransformFunction) NULL);
    g_once_init_leave (&core_media_meta_info, meta);
  }
  return core_media_meta_info;
}

GstBuffer *
gst_core_media_buffer_new (CMSampleBufferRef sample_buf)
{
  CVImageBufferRef image_buf;
  CVPixelBufferRef pixel_buf;
  CMBlockBufferRef block_buf;
  gchar *data = NULL;
  UInt32 size;
  OSStatus status;
  GstBuffer *buf;
  GstCoreMediaMeta *meta;

  image_buf = CMSampleBufferGetImageBuffer (sample_buf);
  pixel_buf = NULL;
  block_buf = CMSampleBufferGetDataBuffer (sample_buf);

  if (image_buf != NULL &&
      CFGetTypeID (image_buf) == CVPixelBufferGetTypeID ()) {
    pixel_buf = (CVPixelBufferRef) image_buf;

    if (CVPixelBufferLockBaseAddress (pixel_buf,
            kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) {
      goto error;
    }

    if (CVPixelBufferIsPlanar (pixel_buf)) {
      gint plane_count, plane_idx;

      data = CVPixelBufferGetBaseAddressOfPlane (pixel_buf, 0);

      size = 0;
      plane_count = CVPixelBufferGetPlaneCount (pixel_buf);
      for (plane_idx = 0; plane_idx != plane_count; plane_idx++) {
        size += CVPixelBufferGetBytesPerRowOfPlane (pixel_buf, plane_idx) *
            CVPixelBufferGetHeightOfPlane (pixel_buf, plane_idx);
      }
    } else {
      data = CVPixelBufferGetBaseAddress (pixel_buf);
      size = CVPixelBufferGetBytesPerRow (pixel_buf) *
          CVPixelBufferGetHeight (pixel_buf);
    }
  } else if (block_buf != NULL) {
    status = CMBlockBufferGetDataPointer (block_buf, 0, 0, 0, &data);
    if (status != noErr)
      goto error;
    size = CMBlockBufferGetDataLength (block_buf);
  } else {
    goto error;
  }

  buf = gst_buffer_new ();

  meta = (GstCoreMediaMeta *) gst_buffer_add_meta (buf,
      gst_core_media_meta_get_info (), NULL);
  CVBufferRetain ((CVBufferRef)sample_buf);
  meta->sample_buf = sample_buf;
  meta->image_buf = image_buf;
  meta->pixel_buf = pixel_buf;
  meta->block_buf = block_buf;

  gst_buffer_append_memory (buf,
      gst_memory_new_wrapped (GST_MEMORY_FLAG_NO_SHARE, data,
          size, 0, size, NULL, NULL));

  return buf;

error:
  return NULL;
}

CVPixelBufferRef
gst_core_media_buffer_get_pixel_buffer (GstBuffer * buf)
{
  GstCoreMediaMeta *meta = (GstCoreMediaMeta *) gst_buffer_get_meta (buf,
      GST_CORE_MEDIA_META_API_TYPE);
  g_return_val_if_fail (meta != NULL, NULL);

  return CVPixelBufferRetain (meta->pixel_buf);
}
