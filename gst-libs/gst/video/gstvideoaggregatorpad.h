/* Generic video aggregator plugin
 * Copyright (C) 2008 Wim Taymans <wim@fluendo.com>
 * Copyright (C) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 
#ifndef __GST_VIDEO_AGGREGATOR_PAD_H__
#define __GST_VIDEO_AGGREGATOR_PAD_H__

#include <gst/gst.h>
#include <gst/video/video.h>

#include <gst/base/gstaggregator.h>

G_BEGIN_DECLS

#define GST_TYPE_VIDEO_AGGREGATOR_PAD (gst_videoaggregator_pad_get_type())
#define GST_VIDEO_AGGREGATOR_PAD(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_AGGREGATOR_PAD, GstVideoAggregatorPad))
#define GST_VIDEO_AGGREGATOR_PAD_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_COMPOSITOR_PAD, GstVideoAggregatorPadClass))
#define GST_IS_VIDEO_AGGREGATOR_PAD(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_AGGREGATOR_PAD))
#define GST_IS_VIDEO_AGGREGATOR_PAD_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEO_AGGREGATOR_PAD))

typedef struct _GstVideoAggregatorPad GstVideoAggregatorPad;
typedef struct _GstVideoAggregatorPadClass GstVideoAggregatorPadClass;
typedef struct _GstVideoAggregatorPadPrivate GstVideoAggregatorPadPrivate;

/**
 * GstVideoAggregatorPad:
 *
 * The opaque #GstVideoAggregatorPad structure.
 */
struct _GstVideoAggregatorPad
{
  GstAggregatorPad parent;

  /* < private > */

  /* caps */
  GstVideoInfo info;

  /* properties */
  guint zorder;

  /* caps used for conversion if needed */
  GstVideoInfo conversion_info;

  gboolean need_conversion_update;
  GstBuffer *converted_buffer;

  GstBuffer *buffer;
  GstVideoInfo queued_vinfo;
  GstBuffer *queued;
  GstVideoInfo buffer_vinfo;

  GstClockTime start_time;
  GstClockTime end_time;
  GstVideoFrame *aggregated_frame;

  /* < private > */
  GstVideoAggregatorPadPrivate *priv;
  gpointer          _gst_reserved[GST_PADDING];
};

struct _GstVideoAggregatorPadClass
{
  GstAggregatorPadClass parent_class;

  gpointer          _gst_reserved[GST_PADDING];
};

GType gst_videoaggregator_pad_get_type (void);

G_END_DECLS
#endif /* __GST_VIDEO_AGGREGATOR_PAD_H__ */
