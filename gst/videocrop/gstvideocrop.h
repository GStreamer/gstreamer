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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_VIDEO_CROP_H__
#define __GST_VIDEO_CROP_H__

#include <gst/base/gstbasetransform.h>

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

typedef enum {
  VIDEO_CROP_PIXEL_FORMAT_PACKED_SIMPLE = 0,  /* RGBx, AYUV */
  VIDEO_CROP_PIXEL_FORMAT_PACKED_COMPLEX,     /* UYVY, YVYU */
  VIDEO_CROP_PIXEL_FORMAT_PLANAR              /* I420, YV12 */
} VideoCropPixelFormat;

typedef struct _GstVideoCropImageDetails GstVideoCropImageDetails;
struct _GstVideoCropImageDetails
{
  /*< private >*/
  VideoCropPixelFormat  packing;

  guint width;
  guint height;
  guint size;

  /* for packed RGB and YUV */
  guint   stride;
  guint   bytes_per_pixel;
  guint8  macro_y_off;            /* for YUY2, YVYU, UYVY, Y offset within macropixel in bytes */

  /* for planar YUV */
  guint y_stride, y_off;
  guint u_stride, u_off;
  guint v_stride, v_off;
};

typedef struct _GstVideoCrop GstVideoCrop;
typedef struct _GstVideoCropClass GstVideoCropClass;

struct _GstVideoCrop
{
  GstBaseTransform basetransform;

  /*< private >*/
  gint crop_left;
  gint crop_right;
  gint crop_top;
  gint crop_bottom;

  GstVideoCropImageDetails in;  /* details of input image */
  GstVideoCropImageDetails out; /* details of output image */
};

struct _GstVideoCropClass
{
  GstBaseTransformClass basetransform_class;
};

GType gst_video_crop_get_type (void);

G_END_DECLS

#endif /* __GST_VIDEO_CROP_H__ */

