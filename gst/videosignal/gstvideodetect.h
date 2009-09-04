/* GStreamer
 * Copyright (C) <2006> Wim Taymans <wim@fluendo.com>
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

#ifndef __GST_VIDEO_DETECT_H__
#define __GST_VIDEO_DETECT_H__

#include <gst/video/gstvideofilter.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_VIDEO_DETECT \
  (gst_video_detect_get_type())
#define GST_VIDEO_DETECT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_DETECT,GstVideoDetect))
#define GST_VIDEO_DETECT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEO_DETECT,GstVideoDetectClass))
#define GST_IS_VIDEO_DETECT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_DETECT))
#define GST_IS_VIDEO_DETECT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEO_DETECT))

typedef struct _GstVideoDetect GstVideoDetect;
typedef struct _GstVideoDetectClass GstVideoDetectClass;

/**
 * GstVideoDetect:
 *
 * Opaque datastructure.
 */
struct _GstVideoDetect {
  GstVideoFilter videofilter;
  
  gint width, height;
  GstVideoFormat format;

  gboolean message;
  gint pattern_width;
  gint pattern_height;
  gint pattern_count;
  gint pattern_data_count;
  gdouble pattern_center;
  gdouble pattern_sensitivity;
  gint left_offset;
  gint bottom_offset;

  gboolean in_pattern;
};

struct _GstVideoDetectClass {
  GstVideoFilterClass parent_class;
};

GType gst_video_detect_get_type (void);

G_END_DECLS

#endif /* __GST_VIDEO_DETECT_H__ */
