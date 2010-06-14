/*
 * Copyright (C) 2008  Barracuda Networks, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301  USA
 *
 */

#ifndef __GST_VIDEOMAXRATE_H__
#define __GST_VIDEOMAXRATE_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

#define GST_TYPE_VIDEO_MAX_RATE \
  (gst_video_max_rate_get_type())
#define GST_VIDEO_MAX_RATE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_MAX_RATE,GstVideoMaxRate))
#define GST_VIDEO_MAX_RATE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEO_MAX_RATE, \
      GstVideoMaxRateClass))
#define GST_IS_VIDEO_MAX_RATE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_MAX_RATE))
#define GST_IS_VIDEO_MAX_RATE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEO_MAX_RATE))

typedef struct _GstVideoMaxRate      GstVideoMaxRate;
typedef struct _GstVideoMaxRateClass GstVideoMaxRateClass;

struct _GstVideoMaxRate
{
  GstBaseTransform parent;

  /*< private >*/

  GstClockTimeDiff wanted_diff;
  GstClockTime average_period;

  GstClockTime last_ts;
  GstClockTimeDiff average;
};

struct _GstVideoMaxRateClass
{
  GstBaseTransformClass parent_class;
};

GType gst_video_max_rate_get_type(void);

G_END_DECLS

#endif /* __GST_VIDEOMAXRATE_H__ */
