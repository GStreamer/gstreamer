/* GStreamer
 * Copyright (C) <2011> Wim Taymans <wim.taymans@gmail.com>
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

#ifndef __GST_VIDEO_META_H__
#define __GST_VIDEO_META_H__

#include <gst/gst.h>

#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_VIDEO_META_API   "GstVideoMeta"
#define GST_VIDEO_META_INFO  (gst_video_meta_get_info())
typedef struct _GstVideoMeta GstVideoMeta;

#define GST_VIDEO_CROP_META_API   "GstVideoCropMeta"
#define GST_VIDEO_CROP_META_INFO  (gst_video_crop_meta_get_info())
typedef struct _GstVideoCropMeta GstVideoCropMeta;

/**
 * GstVideoMeta:
 * @meta: parent #GstMeta
 * @buffer: the buffer this metadata belongs to
 * @flags: additional video flags
 * @format: the video format
 * @id: identifier of the frame
 * @width: the video width
 * @height: the video height
 * @n_planes: the number of planes in the image
 * @offset: array of offsets for the planes
 * @stride: array of strides for the planes
 * @map: map the memory of a plane
 * @unmap: unmap the memory of a plane
 *
 * Extra buffer metadata describing image properties
 */
struct _GstVideoMeta {
  GstMeta            meta;

  GstBuffer         *buffer;

  GstVideoFlags      flags;
  GstVideoFormat     format;
  gint               id;
  guint              width;
  guint              height;

  guint              n_planes;
  gsize              offset[GST_VIDEO_MAX_PLANES];
  gint               stride[GST_VIDEO_MAX_PLANES];

  gboolean (*map)    (GstVideoMeta *meta, guint plane, GstMapInfo *info, gint *stride,
                      GstMapFlags flags);
  gboolean (*unmap)  (GstVideoMeta *meta, guint plane, GstMapInfo *info);
};

const GstMetaInfo * gst_video_meta_get_info (void);

#define gst_buffer_get_video_meta(b) ((GstVideoMeta*)gst_buffer_get_meta((b),GST_VIDEO_META_INFO))
GstVideoMeta * gst_buffer_get_video_meta_id    (GstBuffer *buffer, gint id);

GstVideoMeta * gst_buffer_add_video_meta       (GstBuffer *buffer, GstVideoFlags flags,
                                                GstVideoFormat format, guint width, guint height);
GstVideoMeta * gst_buffer_add_video_meta_full  (GstBuffer *buffer, GstVideoFlags flags,
                                                GstVideoFormat format, guint width, guint height,
                                                guint n_planes, gsize offset[GST_VIDEO_MAX_PLANES],
                                                gint stride[GST_VIDEO_MAX_PLANES]);

gboolean       gst_video_meta_map        (GstVideoMeta *meta, guint plane, GstMapInfo *info,
                                          gint *stride, GstMapFlags flags);
gboolean       gst_video_meta_unmap      (GstVideoMeta *meta, guint plane, GstMapInfo *info);

/**
 * GstVideoCropMeta:
 * @meta: parent #GstMeta
 * @x: the horizontal offset
 * @y: the vertical offset
 * @width: the cropped width
 * @height: the cropped height
 *
 * Extra buffer metadata describing image cropping.
 */
struct _GstVideoCropMeta {
  GstMeta       meta;

  guint         x;
  guint         y;
  guint         width;
  guint         height;
};

const GstMetaInfo * gst_video_crop_meta_get_info (void);

#define gst_buffer_get_video_crop_meta(b) ((GstVideoCropMeta*)gst_buffer_get_meta((b),GST_VIDEO_CROP_META_INFO))
#define gst_buffer_add_video_crop_meta(b) ((GstVideoCropMeta*)gst_buffer_add_meta((b),GST_VIDEO_CROP_META_INFO, NULL))

G_END_DECLS

#endif /* __GST_VIDEO_META_H__ */
