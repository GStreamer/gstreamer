/* GStreamer video frame cropping
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
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

#ifndef __GST_VIDEO_CROP_H__
#define __GST_VIDEO_CROP_H__

#include <gst/video/gstvideofilter.h>

G_BEGIN_DECLS
#define GST_TYPE_VIDEO_CROP \
  (gst_video_crop_get_type())
#define GST_VIDEO_CROP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_CROP,GstVideoCrop))
#define GST_VIDEO_CROP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEO_CROP,GstVideoCropClass))
#define GST_IS_VIDEO_CROP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_CROP))
#define GST_IS_VIDEO_CROP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEO_CROP))

GST_ELEMENT_REGISTER_DECLARE (videocrop);

typedef enum
{
  /* RGB (+ variants), ARGB (+ variants), AYUV, GRAY */
  VIDEO_CROP_PIXEL_FORMAT_PACKED_SIMPLE = 0,
  /* YVYU, YUY2, UYVY */
  VIDEO_CROP_PIXEL_FORMAT_PACKED_YVYU,
  /* v210 */
  VIDEO_CROP_PIXEL_FORMAT_PACKED_v210,
  /* I420, A420, YV12, Y444, Y42B, Y41B,
   * I420_10BE, A420_10BE, Y444_10BE, A444_10BE, I422_10BE, A422_10BE,
   * I420_10LE, A420_10LE, Y444_10LE, A444_10LE, I422_10LE, A422_10LE,
   * I420_12BE, Y444_12BE, I422_12BE,
   * I420_12LE, Y444_12LE, I422_12LE,
   * GBR, GBR_10BE, GBR_10LE, GBR_12BE, GBR_12LE,
   * GBRA, GBRA_10BE, GBRA_10LE, GBRA_12BE, GBRA_12LE */
  VIDEO_CROP_PIXEL_FORMAT_PLANAR,
  /* NV12, NV21 */
  VIDEO_CROP_PIXEL_FORMAT_SEMI_PLANAR
} VideoCropPixelFormat;

typedef struct _GstVideoCropImageDetails GstVideoCropImageDetails;

typedef struct _GstVideoCrop GstVideoCrop;
typedef struct _GstVideoCropClass GstVideoCropClass;

struct _GstVideoCrop
{
  GstVideoFilter parent;

  /*< private > */
  gint prop_left;
  gint prop_right;
  gint prop_top;
  gint prop_bottom;
  gboolean need_update;

  GstVideoInfo in_info;
  GstVideoInfo out_info;

  gint crop_left;
  gint crop_right;
  gint crop_top;
  gint crop_bottom;

  VideoCropPixelFormat packing;
  gint macro_y_off;

  gboolean raw_caps;
};

struct _GstVideoCropClass
{
  GstVideoFilterClass parent_class;
};

GType gst_video_crop_get_type (void);

G_END_DECLS
#endif /* __GST_VIDEO_CROP_H__ */
