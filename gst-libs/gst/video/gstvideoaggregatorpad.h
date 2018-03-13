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
#include "gstvideoaggregator.h"

G_BEGIN_DECLS

#define GST_TYPE_VIDEO_AGGREGATOR_PAD (gst_video_aggregator_pad_get_type())
#define GST_VIDEO_AGGREGATOR_PAD(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_AGGREGATOR_PAD, GstVideoAggregatorPad))
#define GST_VIDEO_AGGREGATOR_PAD_CAST(obj) ((GstVideoAggregatorPad *)(obj))
#define GST_VIDEO_AGGREGATOR_PAD_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_COMPOSITOR_PAD, GstVideoAggregatorPadClass))
#define GST_IS_VIDEO_AGGREGATOR_PAD(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_AGGREGATOR_PAD))
#define GST_IS_VIDEO_AGGREGATOR_PAD_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEO_AGGREGATOR_PAD))
#define GST_VIDEO_AGGREGATOR_PAD_GET_CLASS(obj) \
        (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_VIDEO_AGGREGATOR_PAD,GstVideoAggregatorPadClass))

typedef struct _GstVideoAggregatorPad GstVideoAggregatorPad;
typedef struct _GstVideoAggregatorPadClass GstVideoAggregatorPadClass;
typedef struct _GstVideoAggregatorPadPrivate GstVideoAggregatorPadPrivate;

/**
 * GstVideoAggregatorPad:
 * @info: The #GstVideoInfo currently set on the pad
 * @buffer_vinfo: The #GstVideoInfo representing the type contained
 *                in @buffer
 * @aggregated_frame: The #GstVideoFrame ready to be used for aggregation
 *                    inside the aggregate_frames vmethod.
 * @zorder: The zorder of this pad
 */
struct _GstVideoAggregatorPad
{
  GstAggregatorPad parent;

  GstVideoInfo info;

  GstBuffer *buffer;

  GstVideoFrame *aggregated_frame;

  /* properties */
  guint zorder;
  gboolean ignore_eos;

  /* Subclasses can force an alpha channel in the (input thus output)
   * colorspace format */
  gboolean needs_alpha;

  /* < private > */
  GstVideoAggregatorPadPrivate *priv;

  gpointer          _gst_reserved[GST_PADDING];
};

/**
 * GstVideoAggregatorPadClass:
 *
 * @set_info: Lets subclass set a converter on the pad,
 *                 right after a new format has been negotiated.
 * @prepare_frame: Prepare the frame from the pad buffer (if any)
 *                 and sets it to @aggregated_frame
 * @clean_frame:   clean the frame previously prepared in prepare_frame
 */
struct _GstVideoAggregatorPadClass
{
  GstAggregatorPadClass parent_class;
  gboolean           (*set_info)              (GstVideoAggregatorPad * pad,
                                               GstVideoAggregator    * videoaggregator,
                                               GstVideoInfo          * current_info,
                                               GstVideoInfo          * wanted_info);

  gboolean           (*prepare_frame)         (GstVideoAggregatorPad * pad,
                                               GstVideoAggregator    * videoaggregator);

  void               (*clean_frame)           (GstVideoAggregatorPad * pad,
                                               GstVideoAggregator    * videoaggregator);

  gpointer          _gst_reserved[GST_PADDING_LARGE];
};

GST_VIDEO_BAD_API
GType gst_video_aggregator_pad_get_type (void);

G_END_DECLS
#endif /* __GST_VIDEO_AGGREGATOR_PAD_H__ */
