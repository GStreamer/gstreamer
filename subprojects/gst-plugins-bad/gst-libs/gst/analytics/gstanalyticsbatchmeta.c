/*
 * GStreamer
 *
 * Copyright (C) 2025 Sebastian Dröge <sebastian@centricular.com>
 *
 * gstanalyticsbatchmeta.c
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

#include "gstanalyticsbatchmeta.h"

static gboolean
gst_analytics_batch_meta_transform (GstBuffer * dest, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstAnalyticsBatchMeta *dmeta, *smeta;

  smeta = (GstAnalyticsBatchMeta *) meta;

  if (GST_META_TRANSFORM_IS_COPY (type)) {
    smeta = (GstAnalyticsBatchMeta *) meta;
    dmeta = gst_buffer_add_analytics_batch_meta (dest);
    if (!dmeta)
      return FALSE;
    GST_TRACE ("copy analytics batch metadata");

    dmeta->streams = g_new (GstAnalyticsBatchStream, smeta->n_streams);
    for (gsize i = 0; i < smeta->n_streams; i++) {
      GstAnalyticsBatchStream *sstream = &smeta->streams[i];
      GstAnalyticsBatchStream *dstream = &dmeta->streams[i];

      dstream->index = sstream->index;
      dstream->buffers = g_new (GstAnalyticsBatchBuffer, sstream->n_buffers);
      for (gsize j = 0; j < sstream->n_buffers; j++) {
        GstAnalyticsBatchBuffer *sbuffer = &sstream->buffers[j];
        GstAnalyticsBatchBuffer *dbuffer = &dstream->buffers[j];

        dbuffer->sticky_events = g_new (GstEvent *, sbuffer->n_sticky_events);
        for (gsize k = 0; k < sbuffer->n_sticky_events; k++) {
          dbuffer->sticky_events[k] = gst_event_ref (sbuffer->sticky_events[k]);
        }
        dbuffer->n_sticky_events = sbuffer->n_sticky_events;

        dbuffer->serialized_events =
            g_new (GstEvent *, sbuffer->n_serialized_events);
        for (gsize k = 0; k < sbuffer->n_serialized_events; k++) {
          dbuffer->serialized_events[k] =
              gst_event_ref (sbuffer->serialized_events[k]);
        }
        dbuffer->n_serialized_events = sbuffer->n_serialized_events;

        dbuffer->buffer =
            sbuffer->buffer ? gst_buffer_ref (sbuffer->buffer) : NULL;
        dbuffer->buffer_list =
            sbuffer->
            buffer_list ? gst_buffer_list_ref (sbuffer->buffer_list) : NULL;
      }
      dstream->n_buffers = sstream->n_buffers;
    }
    dmeta->n_streams = smeta->n_streams;

  } else {
    GST_WARNING
        ("gst_analytics_batch_meta_transform: transform type %u not supported",
        type);
    return FALSE;
  }
  return TRUE;
}

static gboolean
gst_analytics_batch_meta_init (GstMeta * meta, gpointer params,
    GstBuffer * buffer)
{
  GstAnalyticsBatchMeta *bmeta = (GstAnalyticsBatchMeta *) meta;

  bmeta->streams = NULL;
  bmeta->n_streams = 0;

  return TRUE;
}

static void
gst_analytics_batch_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  GstAnalyticsBatchMeta *bmeta = (GstAnalyticsBatchMeta *) meta;

  for (gsize i = 0; i < bmeta->n_streams; i++) {
    GstAnalyticsBatchStream *stream = &bmeta->streams[i];

    for (gsize j = 0; j < stream->n_buffers; j++) {
      GstAnalyticsBatchBuffer *buffer = &stream->buffers[j];

      for (gsize k = 0; k < buffer->n_sticky_events; k++) {
        gst_clear_event (&buffer->sticky_events[k]);
      }
      g_clear_pointer (&buffer->sticky_events, g_free);
      for (gsize k = 0; k < buffer->n_serialized_events; k++) {
        gst_clear_event (&buffer->serialized_events[k]);
      }
      g_clear_pointer (&buffer->serialized_events, g_free);
      gst_clear_buffer (&buffer->buffer);
      gst_clear_buffer_list (&buffer->buffer_list);
    }

    g_clear_pointer (&stream->buffers, g_free);
  }

  g_free (bmeta->streams);
}

/**
 * gst_analytics_batch_meta_api_get_type: (skip)
 *
 * Since: 1.28
 */
GType
gst_analytics_batch_meta_api_get_type (void)
{
  static GType type = 0;
  static const gchar *tags[] = { NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstAnalyticsBatchMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}


/**
 * gst_analytics_batch_meta_get_info: (skip)
 *
 * Since: 1.28
 */
const GstMetaInfo *
gst_analytics_batch_meta_get_info (void)
{
  static const GstMetaInfo *tmeta_info = NULL;

  if (g_once_init_enter (&tmeta_info)) {
    const GstMetaInfo *meta =
        gst_meta_register (gst_analytics_batch_meta_api_get_type (),
        "GstAnalyticsBatchMeta",
        sizeof (GstAnalyticsBatchMeta),
        gst_analytics_batch_meta_init,
        gst_analytics_batch_meta_free,
        gst_analytics_batch_meta_transform);
    g_once_init_leave (&tmeta_info, meta);
  }
  return tmeta_info;
}

/**
 * gst_buffer_add_analytics_batch_meta:
 * @buffer: A writable #GstBuffer
 *
 * Adds a #GstAnalyticsBatchMeta to a buffer or returns the existing one
 *
 * Returns: (transfer none): The new #GstAnalyticsBatchMeta
 *
 * Since: 1.28
 */

GstAnalyticsBatchMeta *
gst_buffer_add_analytics_batch_meta (GstBuffer * buffer)
{
  return (GstAnalyticsBatchMeta *) gst_buffer_add_meta (buffer,
      gst_analytics_batch_meta_get_info (), NULL);
}

/**
 * gst_buffer_get_analytics_batch_meta:
 * @buffer: A #GstBuffer
 *
 * Gets the #GstAnalyticsBatchMeta from a buffer
 *
 * Returns: (nullable)(transfer none): The #GstAnalyticsBatchMeta if there is one
 *
 * Since: 1.28
 */
GstAnalyticsBatchMeta *
gst_buffer_get_analytics_batch_meta (GstBuffer * buffer)
{
  return (GstAnalyticsBatchMeta *) gst_buffer_get_meta (buffer,
      GST_ANALYTICS_BATCH_META_API_TYPE);
}

/**
 * gst_analytics_batch_buffer_get_stream_id:
 * @buffer: A #GstAnalyticsBatchBuffer
 *
 * Gets the current stream id from a buffer
 *
 * Returns: (nullable) (transfer none): The stream id if there is any
 *
 * Since: 1.28
 */
const gchar *
gst_analytics_batch_buffer_get_stream_id (GstAnalyticsBatchBuffer * buffer)
{
  g_return_val_if_fail (buffer != NULL, NULL);

  if (!buffer->sticky_events)
    return NULL;

  for (gsize i = 0; i < buffer->n_sticky_events; i++) {
    GstEvent *event = buffer->sticky_events[i];

    if (GST_EVENT_TYPE (event) == GST_EVENT_STREAM_START) {
      const gchar *stream_id;

      gst_event_parse_stream_start (event, &stream_id);

      return stream_id;
    }
  }

  return NULL;
}

/**
 * gst_analytics_batch_buffer_get_caps:
 * @buffer: A #GstAnalyticsBatchBuffer
 *
 * Gets the #GstCaps from a buffer
 *
 * Returns: (nullable) (transfer none): The #GstCaps if there are any
 *
 * Since: 1.28
 */
GstCaps *
gst_analytics_batch_buffer_get_caps (GstAnalyticsBatchBuffer * buffer)
{
  g_return_val_if_fail (buffer != NULL, NULL);

  if (!buffer->sticky_events)
    return NULL;

  for (gsize i = 0; i < buffer->n_sticky_events; i++) {
    GstEvent *event = buffer->sticky_events[i];

    if (GST_EVENT_TYPE (event) == GST_EVENT_CAPS) {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);

      return caps;
    }
  }

  return NULL;
}

/**
 * gst_analytics_batch_buffer_get_segment:
 * @buffer: A #GstAnalyticsBatchBuffer
 *
 * Gets the #GstSegment from a buffer
 *
 * Returns: (nullable) (transfer none): The #GstSegment if there is one
 *
 * Since: 1.28
 */
const GstSegment *
gst_analytics_batch_buffer_get_segment (GstAnalyticsBatchBuffer * buffer)
{
  g_return_val_if_fail (buffer != NULL, NULL);

  if (!buffer->sticky_events)
    return NULL;

  for (gsize i = 0; i < buffer->n_sticky_events; i++) {
    GstEvent *event = buffer->sticky_events[i];

    if (GST_EVENT_TYPE (event) == GST_EVENT_SEGMENT) {
      const GstSegment *segment;

      gst_event_parse_segment (event, &segment);

      return segment;
    }
  }

  return NULL;
}
