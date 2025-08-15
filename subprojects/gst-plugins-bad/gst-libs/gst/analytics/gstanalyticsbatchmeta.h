/*
 * GStreamer
 *
 * Copyright (C) 2025 Sebastian Dröge <sebastian@centricular.com>
 *
 * gstanalyticsbatchmeta.h
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef __GST_ANALYTICS_BATCH_META_H__
#define __GST_ANALYTICS_BATCH_META_H__

#include <gst/gst.h>
#include <gst/analytics/analytics-meta-prelude.h>

G_BEGIN_DECLS

/**
 * GstAnalyticsBatchStream:
 * @index: Index of the stream in the meta's stream array
 * @sticky_events: (array length=n_sticky_events): The sticky events store before any of the mini objects in the @objects fields are processed
 * @n_sticky_events: Number of sticky events
 * @objects: (nullable) (array length=n_objects): #GstMiniObject in this batch for this stream. Those are serialized mini objects: buffers, bufferlists and serialized events
 * @n_objects: Number of objects
 *
 * Since: 1.28
 */
typedef struct _GstAnalyticsBatchStream {
  guint index;

  GstEvent **sticky_events;
  gsize n_sticky_events;

  GstMiniObject **objects;
  gsize n_objects;

  /* <private> */
  gpointer padding[GST_PADDING];
} GstAnalyticsBatchStream;

/**
 * GstAnalyticsBatchMeta:
 * @meta: parent
 * @streams: (nullable) (array length=n_streams): #GstAnalyticsBatchStream for this batch
 * @n_streams: Number of streams
 *
 * This meta represents a batch of buffers from one or more streams together
 * with the relevant events to be able to interpret the buffers and to be able
 * to reconstruct the original streams.
 *
 * When used for multiple streams and batching them temporarily, caps of type
 * `multistream/x-analytics-batch(meta:GstAnalyticsBatchMeta)` should be used,
 * with the original caps of each stream in an array-typed `streams` field. The
 * original caps of each stream might be extended by additional fields and the
 * order of the streams in the array corresponds to the order of the @streams
 * array of the meta. In this case, empty buffers would be used without any
 * #GstMemory and
 *
 * When used for a single stream, the original caps might be used together with
 * the `meta:GstAnalyticsBatchMeta` caps feature and potentially extended by
 * additional fields to describe the kind of batching and its configuration,
 * e.g. that each batch is made of 25% overlapping 320x320 slices of the
 * original video frame.
 *
 * The timestamp, duration and other metadata of each batch can be retrieved
 * from the parent buffer of this meta.
 *
 * Since: 1.28
 */
typedef struct _GstAnalyticsBatchMeta
{
  GstMeta meta;

  GstAnalyticsBatchStream *streams;
  gsize n_streams;
} GstAnalyticsBatchMeta;

/**
 * GST_ANALYTICS_BATCH_META_API_TYPE:
 *
 * The Analytics Batch Meta API type
 *
 * Since: 1.28
 */
#define GST_ANALYTICS_BATCH_META_API_TYPE \
  (gst_analytics_batch_meta_api_get_type())

/**
 * GST_ANALYTICS_BATCH_META_INFO: (skip)
 *
 * The Analytics Batch Meta API Info
 *
 * Since: 1.28
 */
#define GST_ANALYTICS_BATCH_META_INFO \
  (gst_analytics_batch_meta_get_info())

/**
 * GST_CAPS_FEATURE_META_GST_ANALYTICS_BATCH_META:
 *
 * The caps feature to be used on streams that make use of this meta.
 *
 * Since: 1.28
 */
#define GST_CAPS_FEATURE_META_GST_ANALYTICS_BATCH_META "meta:GstAnalyticsBatchMeta"

GST_ANALYTICS_META_API
GType gst_analytics_batch_meta_api_get_type (void);

GST_ANALYTICS_META_API
const GstMetaInfo *gst_analytics_batch_meta_get_info (void);

GST_ANALYTICS_META_API
GstAnalyticsBatchMeta *
gst_buffer_add_analytics_batch_meta (GstBuffer * buffer);

GST_ANALYTICS_META_API
GstAnalyticsBatchMeta *
gst_buffer_get_analytics_batch_meta (GstBuffer * buffer);

GST_ANALYTICS_META_API
const gchar *
gst_analytics_batch_stream_get_stream_id (GstAnalyticsBatchStream * stream);

GST_ANALYTICS_META_API
GstCaps *
gst_analytics_batch_stream_get_caps (GstAnalyticsBatchStream * stream);

GST_ANALYTICS_META_API
const GstSegment *
gst_analytics_batch_stream_get_segment (GstAnalyticsBatchStream * stream);

G_END_DECLS

#endif
