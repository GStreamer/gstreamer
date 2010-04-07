/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>

#ifndef __GST_VIDEO_BOX_H__
#define __GST_VIDEO_BOX_H__

#define GST_TYPE_VIDEO_BOX \
  (gst_video_box_get_type())
#define GST_VIDEO_BOX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_BOX,GstVideoBox))
#define GST_VIDEO_BOX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEO_BOX,GstVideoBoxClass))
#define GST_IS_VIDEO_BOX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_BOX))
#define GST_IS_VIDEO_BOX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEO_BOX))

typedef struct _GstVideoBox GstVideoBox;
typedef struct _GstVideoBoxClass GstVideoBoxClass;

typedef enum
{
  VIDEO_BOX_FILL_BLACK,
  VIDEO_BOX_FILL_GREEN,
  VIDEO_BOX_FILL_BLUE,
  VIDEO_BOX_FILL_RED,
  VIDEO_BOX_FILL_YELLOW,
  VIDEO_BOX_FILL_WHITE,
  VIDEO_BOX_FILL_LAST
}
GstVideoBoxFill;

struct _GstVideoBox
{
  GstBaseTransform element;

  /* <private> */

  /* Guarding everything below */
  GMutex *mutex;
  /* caps */
  GstVideoFormat in_format;
  gint in_width, in_height;
  gboolean in_sdtv;
  GstVideoFormat out_format;
  gint out_width, out_height;
  gboolean out_sdtv;

  gint box_left, box_right, box_top, box_bottom;

  gint border_left, border_right, border_top, border_bottom;
  gint crop_left, crop_right, crop_top, crop_bottom;

  gdouble alpha;
  gdouble border_alpha;

  GstVideoBoxFill fill_type;

  gboolean autocrop;

  void (*fill) (GstVideoBoxFill fill_type, guint b_alpha, GstVideoFormat format, guint8 *dest, gboolean sdtv, gint width, gint height);
  void (*copy) (guint i_alpha, GstVideoFormat dest_format, guint8 *dest, gboolean dest_sdtv, gint dest_width, gint dest_height, gint dest_x, gint dest_y, GstVideoFormat src_format, const guint8 *src, gboolean src_sdtv, gint src_width, gint src_height, gint src_x, gint src_y, gint w, gint h);
};

struct _GstVideoBoxClass
{
  GstBaseTransformClass parent_class;
};

GType gst_video_box_get_type (void);

#endif /* __GST_VIDEO_BOX_H__ */
