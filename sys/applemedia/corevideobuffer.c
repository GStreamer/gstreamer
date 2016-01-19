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

#include "corevideobuffer.h"
#include "corevideomemory.h"

static const GstMetaInfo *gst_core_video_meta_get_info (void);

static void
gst_core_video_meta_add (GstBuffer * buffer, CVBufferRef cvbuf)
{
  GstCoreVideoMeta *meta;

  meta = (GstCoreVideoMeta *) gst_buffer_add_meta (buffer,
      gst_core_video_meta_get_info (), NULL);
  meta->cvbuf = CVBufferRetain (cvbuf);
  meta->pixbuf = (CVPixelBufferRef) cvbuf;
}

static void
gst_core_video_meta_free (GstCoreVideoMeta * meta, GstBuffer * buf)
{
  CVBufferRelease (meta->cvbuf);
}

static gboolean
gst_core_video_meta_transform (GstBuffer * transbuf, GstCoreVideoMeta * meta,
    GstBuffer * buffer, GQuark type, GstMetaTransformCopy * data)
{
  if (!data->region) {
    /* only copy if the complete data is copied as well */
    gst_core_video_meta_add (transbuf, meta->cvbuf);
  } else {
    GST_WARNING_OBJECT (transbuf,
        "dropping Core Video metadata due to partial buffer");
  }

  return TRUE;                  /* retval unused */
}

GType
gst_core_video_meta_api_get_type (void)
{
  static volatile GType type;
  static const gchar *tags[] = { "memory", NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstCoreVideoMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

static const GstMetaInfo *
gst_core_video_meta_get_info (void)
{
  static const GstMetaInfo *core_video_meta_info = NULL;

  if (g_once_init_enter (&core_video_meta_info)) {
    const GstMetaInfo *meta = gst_meta_register (GST_CORE_VIDEO_META_API_TYPE,
        "GstCoreVideoMeta", sizeof (GstCoreVideoMeta),
        (GstMetaInitFunction) NULL,
        (GstMetaFreeFunction) gst_core_video_meta_free,
        (GstMetaTransformFunction) gst_core_video_meta_transform);
    g_once_init_leave (&core_video_meta_info, meta);
  }
  return core_video_meta_info;
}

void
gst_core_video_wrap_pixel_buffer (GstBuffer * buf, GstVideoInfo * info,
    CVPixelBufferRef pixel_buf, gboolean * has_padding)
{
  guint n_planes;
  gsize offset[GST_VIDEO_MAX_PLANES] = { 0 };
  gint stride[GST_VIDEO_MAX_PLANES] = { 0 };
  UInt32 size;
  GstAppleCoreVideoPixelBuffer *gpixbuf;

  gpixbuf = gst_apple_core_video_pixel_buffer_new (pixel_buf);
  *has_padding = FALSE;

  if (CVPixelBufferIsPlanar (pixel_buf)) {
    gint i, size = 0, plane_offset = 0;

    n_planes = CVPixelBufferGetPlaneCount (pixel_buf);
    for (i = 0; i < n_planes; i++) {
      stride[i] = CVPixelBufferGetBytesPerRowOfPlane (pixel_buf, i);

      if (stride[i] != GST_VIDEO_INFO_PLANE_STRIDE (info, i)) {
        *has_padding = TRUE;
      }

      size = stride[i] * CVPixelBufferGetHeightOfPlane (pixel_buf, i);
      offset[i] = plane_offset;
      plane_offset += size;

      gst_buffer_append_memory (buf,
          gst_apple_core_video_memory_new_wrapped (gpixbuf, i, size));
    }
  } else {
    n_planes = 1;
    stride[0] = CVPixelBufferGetBytesPerRow (pixel_buf);
    offset[0] = 0;
    size = stride[0] * CVPixelBufferGetHeight (pixel_buf);

    gst_buffer_append_memory (buf,
        gst_apple_core_video_memory_new_wrapped (gpixbuf,
            GST_APPLE_CORE_VIDEO_NO_PLANE, size));
  }

  gst_apple_core_video_pixel_buffer_unref (gpixbuf);

  if (info) {
    GstVideoMeta *video_meta;

    video_meta =
        gst_buffer_add_video_meta_full (buf, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_INFO_FORMAT (info), info->width, info->height, n_planes,
        offset, stride);
  }
}

GstBuffer *
gst_core_video_buffer_new (CVBufferRef cvbuf, GstVideoInfo * vinfo)
{
  CVPixelBufferRef pixbuf = NULL;
  GstBuffer *buf;
  GstCoreVideoMeta *meta;
  gboolean has_padding;         /* not used for now */

  if (CFGetTypeID (cvbuf) != CVPixelBufferGetTypeID ())
    /* TODO: Do we need to handle other buffer types? */
    return NULL;

  pixbuf = (CVPixelBufferRef) cvbuf;

  buf = gst_buffer_new ();

  /* add the corevideo meta to pass the underlying corevideo buffer */
  meta = (GstCoreVideoMeta *) gst_buffer_add_meta (buf,
      gst_core_video_meta_get_info (), NULL);
  meta->cvbuf = CVBufferRetain (cvbuf);
  meta->pixbuf = pixbuf;

  gst_core_video_wrap_pixel_buffer (buf, vinfo, pixbuf, &has_padding);

  return buf;
}
