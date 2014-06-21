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
 
#ifndef __GST_VIDEO_AGGREGATOR_H__
#define __GST_VIDEO_AGGREGATOR_H__

#ifndef GST_USE_UNSTABLE_API
#warning "The Video library from gst-plugins-bad is unstable API and may change in future."
#warning "You can define GST_USE_UNSTABLE_API to avoid this warning."
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/base/gstaggregator.h>

#include "gstvideoaggregatorpad.h"

G_BEGIN_DECLS

#define GST_TYPE_VIDEO_AGGREGATOR (gst_videoaggregator_get_type())
#define GST_VIDEO_AGGREGATOR(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_AGGREGATOR, GstVideoAggregator))
#define GST_VIDEO_AGGREGATOR_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEO_AGGREGATOR, GstVideoAggregatorClass))
#define GST_IS_VIDEO_AGGREGATOR(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_AGGREGATOR))
#define GST_IS_VIDEO_AGGREGATOR_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEO_AGGREGATOR))
#define GST_VIDEO_AGGREGATOR_GET_CLASS(obj) \
        (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_VIDEO_AGGREGATOR,GstVideoAggregatorClass))

typedef struct _GstVideoAggregator GstVideoAggregator;
typedef struct _GstVideoAggregatorClass GstVideoAggregatorClass;
typedef struct _GstVideoAggregatorPrivate GstVideoAggregatorPrivate;

/**
 * GstVideoAggregator:
 * @info: The #GstVideoInfo representing the currently set
 * srcpad caps.
 */
struct _GstVideoAggregator
{
  GstAggregator aggregator;

  /*< public >*/
  /* Output caps */
  GstVideoInfo info;

  /* < private > */
  GstVideoAggregatorPrivate *priv;
  gpointer          _gst_reserved[GST_PADDING];
};

/**
 * GstVideoAggregatorClass:
 * @disable_frame_conversion: Optional.
 *                            Allows subclasses to disable the frame colorspace
 *                            conversion feature
 * @update_info:              Optional.
 *                            Lets subclasses update the src #GstVideoInfo representing
 *                            the src pad caps before usage.
 * @aggregate_frames:         Lets subclasses aggregate frames that are ready. Subclasses
 *                            should iterate the GstElement.sinkpads and use the already
 *                            mapped #GstVideoFrame from GstVideoAggregatorPad.aggregated_frame
 *                            or directly use the #GstBuffer from GstVideoAggregatorPad.buffer
 *                            if it needs to map the buffer in a special way. The result of the
 *                            aggregation should land in @outbuffer.
 * @get_output_buffer:        Optional.
 *                            Lets subclasses provide a #GstBuffer to be used as @outbuffer of
 *                            the #aggregate_frames vmethod.
 * @negotiated_caps:          Optional.
 *                            Notifies subclasses what caps format has been negotiated
 **/
struct _GstVideoAggregatorClass
{
  /*< private >*/
  GstAggregatorClass parent_class;

  /*< public >*/
  gboolean           disable_frame_conversion;

  gboolean           (*update_info)               (GstVideoAggregator *  videoaggregator,
                                                   GstVideoInfo       *  info);
  GstFlowReturn      (*aggregate_frames)          (GstVideoAggregator *  videoaggregator,
                                                   GstBuffer          *  outbuffer);
  GstFlowReturn      (*get_output_buffer)         (GstVideoAggregator *  videoaggregator,
                                                   GstBuffer          ** outbuffer);
  gboolean           (*negotiated_caps)           (GstVideoAggregator *  videoaggregator,
                                                   GstCaps            *  caps);
  /* < private > */
  gpointer            _gst_reserved[GST_PADDING];
};

GType gst_videoaggregator_get_type       (void);

G_END_DECLS
#endif /* __GST_VIDEO_AGGREGATOR_H__ */
