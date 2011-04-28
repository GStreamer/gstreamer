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

#ifndef __GST_META_VIDEO_H__
#define __GST_META_VIDEO_H__

#include <gst/gst.h>

#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_META_INFO_VIDEO  (gst_meta_video_get_info())

#define GST_VIDEO_MAX_PLANES 4

typedef struct _GstMetaVideo GstMetaVideo;
typedef struct _GstMetaVideoPlane GstMetaVideoPlane;

/**
 * GstMetaVideoFlags:
 * @GST_META_VIDEO_FLAG_NONE: no flags
 *
 * Extra video flags
 */
typedef enum {
  GST_META_VIDEO_FLAG_NONE = 0
} GstMetaVideoFlags;

/**
 * GstMetaVideoPlane:
 * @offset: offset of the first pixel in the buffer memory region
 * @stride: stride of the image lines. Can be negative when the image is
 *    upside-down
 *
 * Information for one video plane.
 */
struct _GstMetaVideoPlane {
  gsize           offset;
  gint            stride;
};

/**
 * GstMetaVideo:
 * @meta: parent #GstMeta
 * @flags: additional video flags
 * @n_planes: the number of planes in the image
 * @plane: array of #GstMetaVideoPlane
 * @map: map the memory of a plane
 * @unmap: unmap the memory of a plane
 *
 * Extra buffer metadata describing image properties
 */
struct _GstMetaVideo {
  GstMeta       meta;

  GstMetaVideoFlags  flags;

  GstBuffer         *buffer;

  GstVideoFormat     format;
  guint              width;
  guint              height;

  guint              n_planes;
  GstMetaVideoPlane  plane[GST_VIDEO_MAX_PLANES];

  gpointer (*map)    (GstMetaVideo *meta, guint plane, gint *stride,
                      GstMapFlags flags);
  gboolean (*unmap)  (GstMetaVideo *meta, guint plane, gpointer data);
};

const GstMetaInfo * gst_meta_video_get_info (void);

#define gst_buffer_get_meta_video(b) ((GstMetaVideo*)gst_buffer_get_meta((b),GST_META_INFO_VIDEO))
GstMetaVideo * gst_buffer_add_meta_video       (GstBuffer *buffer, GstMetaVideoFlags flags,
                                                GstVideoFormat format, guint width, guint height);
GstMetaVideo * gst_buffer_add_meta_video_full  (GstBuffer *buffer, GstMetaVideoFlags flags,
                                                GstVideoFormat format, guint width, guint height,
                                                guint n_planes, GstMetaVideoPlane plane[GST_VIDEO_MAX_PLANES]);

gpointer       gst_meta_video_map        (GstMetaVideo *meta, guint plane, gint *stride,
                                          GstMapFlags flags);
gboolean       gst_meta_video_unmap      (GstMetaVideo *meta, guint plane, gpointer data);


G_END_DECLS

#endif /* __GST_META_VIDEO_H__ */
