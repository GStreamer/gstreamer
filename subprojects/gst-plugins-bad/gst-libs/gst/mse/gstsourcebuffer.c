/* GStreamer
 *
 * SPDX-License-Identifier: LGPL-2.1
 *
 * Copyright (C) 2013 Google Inc. All rights reserved.
 * Copyright (C) 2013 Orange
 * Copyright (C) 2013-2020 Apple Inc. All rights reserved.
 * Copyright (C) 2014 Sebastian Dröge <sebastian@centricular.com>
 * Copyright (C) 2015, 2016, 2017 Igalia, S.L
 * Copyright (C) 2015, 2016, 2017 Metrological Group B.V.
 * Copyright (C) 2022, 2023 Collabora Ltd.
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

/**
 * SECTION:gstsourcebuffer
 * @title: GstSourceBuffer
 * @short_description: Source Buffer
 * @include: mse/mse.h
 * @symbols:
 * - GstSourceBuffer
 *
 * The Source Buffer is the primary means of data flow between an application
 * and the Media Source API. It represents a single timeline of media,
 * containing some combination of audio, video, and text tracks.
 * An application is responsible for feeding raw data into the Source Buffer
 * using gst_source_buffer_append_buffer() and the Source Buffer will
 * asynchronously process the data into tracks of time-coded multimedia samples.
 *
 * The application as well as the associated playback component can then select
 * to play media from any subset of tracks across all Source Buffers of a Media
 * Source.
 *
 * A few control points are also provided to customize the behavior.
 *
 *  - #GstSourceBuffer:append-mode controls how timestamps of processed samples are
 *  interpreted. They are either inserted in the timeline directly where the
 *  decoded media states they should, or inserted directly after the previously
 *  encountered sample.
 *
 *  - #GstSourceBuffer:append-window-start / #GstSourceBuffer:append-window-end
 *  control the planned time window where media from appended data can be added
 *  to the current timeline. Any samples outside that range may be ignored.
 *
 *  - #GstSourceBuffer:timestamp-offset is added to the start time of any sample
 *  processed.
 *
 * Since: 1.24
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/mse/mse-enumtypes.h>
#include "gstsourcebuffer.h"
#include "gstsourcebuffer-private.h"

#include "gstmselogging-private.h"
#include "gstmsemediatype-private.h"
#include "gstmseeventqueue-private.h"

#include "gstappendpipeline-private.h"
#include "gstmediasource.h"
#include "gstmediasource-private.h"
#include "gstmediasourcesamplemap-private.h"
#include "gstmediasourcetrack-private.h"
#include "gstmediasourcetrackbuffer-private.h"
#include "gstsourcebufferlist-private.h"
#include "gstmsesrc.h"
#include "gstmsesrc-private.h"

#define g_array_new_ranges() \
  (g_array_new (TRUE, FALSE, sizeof (GstMediaSourceRange)))

typedef struct
{
  GstSourceBufferCallbacks callbacks;
  gpointer user_data;
} Callbacks;

typedef struct
{
  GstSourceBuffer *parent;
  GstTask *task;
  GRecMutex lock;
  gboolean cancelled;
} AppendToBufferTask;

typedef struct
{
  GWeakRef parent;

  GstMediaSourceTrack *track;
  GstMediaSourceTrackBuffer *buffer;

  GstTask *task;
  GRecMutex lock;

  gboolean cancelled;
} TrackFeedTask;

typedef struct
{
  GstSourceBuffer *parent;
  GHashTable *processed_samples;
  gboolean push_failed;
} TrackFeedAccumulator;

typedef struct
{
  const GstClockTime time;
  gboolean buffered;
} IsBufferedAccumulator;

typedef struct
{
  const GstClockTime start;
  const GstClockTime end;
  gboolean start_buffered;
  gboolean end_buffered;
} IsRangeBufferedAccumulator;

/**
 * GstSourceBuffer:
 * Since: 1.24
 */
struct _GstSourceBuffer
{
  GstObject parent_instance;

  GstSourceBufferAppendMode append_mode;
  GstClockTime append_window_start;
  GstClockTime append_window_end;
  gchar *content_type;
  gboolean generate_timestamps;
  GstClockTime timestamp_offset;
  gboolean updating;
  gboolean errored;
  gsize size_limit;
  gsize size;
  GstBuffer *pending_data;
  GCond pending_data_cond;
  AppendToBufferTask *append_to_buffer_task;
  GstAppendPipeline *append_pipeline;
  GstMseEventQueue *event_queue;

  GMutex tracks_lock;
  GstClockTime seek_time;
  gboolean processed_init_segment;
  GHashTable *track_buffers;
  GHashTable *track_feeds;

  Callbacks callbacks;
};

G_DEFINE_TYPE (GstSourceBuffer, gst_source_buffer, GST_TYPE_OBJECT);

enum
{
  PROP_0,

  PROP_APPEND_MODE,
  PROP_APPEND_WINDOW_START,
  PROP_APPEND_WINDOW_END,
  PROP_BUFFERED,
  PROP_CONTENT_TYPE,
  PROP_TIMESTAMP_OFFSET,
  PROP_UDPATING,

  N_PROPS,
};

typedef enum
{
  ON_UPDATE_START,
  ON_UPDATE,
  ON_UPDATE_END,
  ON_ERROR,
  ON_ABORT,

  N_SIGNALS,
} SourceBufferEvent;

typedef struct
{
  GstDataQueueItem item;
  SourceBufferEvent event;
} SourceBufferEventItem;

#define DEFAULT_BUFFER_SIZE 1 << 24
#define DEFAULT_APPEND_MODE GST_SOURCE_BUFFER_APPEND_MODE_SEGMENTS

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

static void call_received_init_segment (GstSourceBuffer * self);
static void call_duration_changed (GstSourceBuffer * self);
static void call_active_state_changed (GstSourceBuffer * self);

static GArray *get_buffered_unlocked (GstSourceBuffer * self);
static inline gboolean is_removed_unlocked (GstSourceBuffer * self);
static void reset_parser_state_unlocked (GstSourceBuffer * self);
static void append_error_unlocked (GstSourceBuffer * self);

static void seek_track_buffer_unlocked (GstMediaSourceTrack * track,
    GstMediaSourceTrackBuffer * buffer, GstSourceBuffer * self);
static void dispatch_event (SourceBufferEventItem * item, GstSourceBuffer *
    self);
static void schedule_event_unlocked (GstSourceBuffer * self,
    SourceBufferEvent event);
static void append_to_buffer_task_func (AppendToBufferTask * task);
static void append_to_buffer_task_start (AppendToBufferTask * task);
static void append_to_buffer_task_stop (AppendToBufferTask * task);
static AppendToBufferTask *append_to_buffer_task_new (GstSourceBuffer * parent);
static void append_to_buffer_task_free (AppendToBufferTask * task);
static void track_feed_task (TrackFeedTask * feed);
static void clear_track_feed (TrackFeedTask * feed);
static void stop_track_feed (TrackFeedTask * feed);
static void start_track_feed (TrackFeedTask * feed);
static void reset_track_feed (TrackFeedTask * feed);
static TrackFeedTask *get_track_feed_unlocked (GstSourceBuffer * self,
    GstMediaSourceTrack * track);
static GstMediaSourceTrackBuffer *get_track_buffer_unlocked (GstSourceBuffer *
    self, GstMediaSourceTrack * track);
static void add_track_feed_unlocked (GstMediaSourceTrack * track,
    GstMediaSourceTrackBuffer * track_buffer, GstSourceBuffer * self);
static void add_track_buffer_unlocked (GstMediaSourceTrack * track,
    GstSourceBuffer * self);

static void on_duration_changed (GstAppendPipeline * pipeline,
    gpointer user_data);
static void on_eos (GstAppendPipeline * pipeline, GstMediaSourceTrack * track,
    gpointer user_data);
static void on_error (GstAppendPipeline * pipeline, gpointer user_data);
static void on_new_sample (GstAppendPipeline * pipeline,
    GstMediaSourceTrack * track, GstSample * sample, gpointer user_data);
static void on_received_init_segment (GstAppendPipeline * pipeline,
    gpointer user_data);

#define TRACKS_LOCK(b)   g_mutex_lock(&(b)->tracks_lock)
#define TRACKS_UNLOCK(b) g_mutex_unlock(&(b)->tracks_lock)

static inline GstMediaSource *
get_media_source_unlocked (GstSourceBuffer * self)
{
  GstObject *parent = GST_OBJECT_PARENT (self);
  if (parent == NULL) {
    return NULL;
  }
  return GST_MEDIA_SOURCE (gst_object_ref (parent));
}

static inline GstMediaSource *
get_media_source (GstSourceBuffer * self)
{
  return GST_MEDIA_SOURCE (gst_object_get_parent (GST_OBJECT (self)));
}

static void
clear_pending_data_unlocked (GstSourceBuffer * self)
{
  gst_clear_buffer (&self->pending_data);
}

static GstBuffer *
take_pending_data_unlocked (GstSourceBuffer * self)
{
  return g_steal_pointer (&self->pending_data);
}

static void
set_pending_data_unlocked (GstSourceBuffer * self, GstBuffer * buffer)
{
  clear_pending_data_unlocked (self);
  self->pending_data = buffer;
}

GstSourceBuffer *
gst_source_buffer_new (const gchar * content_type, GstObject * parent,
    GError ** error)
{
  g_return_val_if_fail (GST_IS_MEDIA_SOURCE (parent), NULL);
  g_return_val_if_fail (content_type != NULL, NULL);

  gst_mse_init_logging ();

  GstMediaSourceMediaType media_type = GST_MEDIA_SOURCE_MEDIA_TYPE_INIT;
  gst_media_source_media_type_parse (&media_type, content_type);

  gboolean generate_timestamps = gst_media_source_media_type_generates_timestamp
      (&media_type);
  gst_media_source_media_type_reset (&media_type);

  GstSourceBufferAppendMode append_mode = generate_timestamps
      ? GST_SOURCE_BUFFER_APPEND_MODE_SEQUENCE
      : GST_SOURCE_BUFFER_APPEND_MODE_SEGMENTS;

  GstSourceBuffer *self = g_object_new (GST_TYPE_SOURCE_BUFFER, NULL);

  gst_object_set_parent (GST_OBJECT_CAST (self), parent);
  self->generate_timestamps = generate_timestamps;
  self->append_mode = append_mode;
  self->content_type = g_strdup (content_type);

  GstAppendPipelineCallbacks callbacks = {
    .duration_changed = on_duration_changed,
    .eos = on_eos,
    .error = on_error,
    .new_sample = on_new_sample,
    .received_init_segment = on_received_init_segment,
  };
  GError *append_pipeline_error = NULL;
  self->append_pipeline =
      gst_append_pipeline_new (&callbacks, self, &append_pipeline_error);
  if (append_pipeline_error) {
    g_propagate_prefixed_error (error, append_pipeline_error,
        "failed to create source buffer");
    goto error;
  }

  return gst_object_ref_sink (self);
error:
  gst_clear_object (&self);
  return NULL;
}

GstSourceBuffer *
gst_source_buffer_new_with_callbacks (const gchar * content_type,
    GstObject * parent, GstSourceBufferCallbacks * callbacks,
    gpointer user_data, GError ** error)
{
  g_return_val_if_fail (callbacks, NULL);

  GError *source_buffer_error = NULL;
  GstSourceBuffer *self =
      gst_source_buffer_new (content_type, parent, &source_buffer_error);
  if (source_buffer_error) {
    g_propagate_error (error, source_buffer_error);
    gst_clear_object (&self);
    return NULL;
  }
  self->callbacks.callbacks = *callbacks;
  self->callbacks.user_data = user_data;

  return self;
}

static void
gst_source_buffer_constructed (GObject * object)
{
  GstSourceBuffer *self = GST_SOURCE_BUFFER (object);

  append_to_buffer_task_start (self->append_to_buffer_task);

  G_OBJECT_CLASS (gst_source_buffer_parent_class)->constructed (object);
}

static void
gst_source_buffer_dispose (GObject * object)
{
  GstSourceBuffer *self = (GstSourceBuffer *) object;

  g_clear_pointer (&self->append_to_buffer_task, append_to_buffer_task_free);

  gst_clear_object (&self->append_pipeline);

  g_hash_table_remove_all (self->track_feeds);

  if (!is_removed_unlocked (self)) {
    GstMediaSource *parent = get_media_source (self);
    gst_media_source_remove_source_buffer (parent, self, NULL);
    gst_object_unref (parent);
  }

  gst_clear_object (&self->event_queue);

  G_OBJECT_CLASS (gst_source_buffer_parent_class)->dispose (object);
}

static void
gst_source_buffer_finalize (GObject * object)
{
  GstSourceBuffer *self = (GstSourceBuffer *) object;

  g_clear_pointer (&self->content_type, g_free);

  g_hash_table_unref (self->track_buffers);
  g_hash_table_unref (self->track_feeds);
  g_mutex_clear (&self->tracks_lock);

  G_OBJECT_CLASS (gst_source_buffer_parent_class)->finalize (object);
}

static void
gst_source_buffer_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstSourceBuffer *self = GST_SOURCE_BUFFER (object);

  switch (prop_id) {
    case PROP_APPEND_MODE:
      g_value_set_enum (value, gst_source_buffer_get_append_mode (self));
      break;
    case PROP_APPEND_WINDOW_START:
      g_value_set_uint64 (value,
          gst_source_buffer_get_append_window_start (self));
      break;
    case PROP_APPEND_WINDOW_END:
      g_value_set_uint64 (value,
          gst_source_buffer_get_append_window_end (self));
      break;
    case PROP_BUFFERED:
      g_value_take_boxed (value, gst_source_buffer_get_buffered (self, NULL));
      break;
    case PROP_CONTENT_TYPE:
      g_value_take_string (value, gst_source_buffer_get_content_type (self));
      break;
    case PROP_TIMESTAMP_OFFSET:
      g_value_set_int64 (value, gst_source_buffer_get_timestamp_offset (self));
      break;
    case PROP_UDPATING:
      g_value_set_boolean (value, gst_source_buffer_get_updating (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_source_buffer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSourceBuffer *self = GST_SOURCE_BUFFER (object);

  switch (prop_id) {
    case PROP_APPEND_MODE:
      gst_source_buffer_set_append_mode (self, g_value_get_enum (value), NULL);
      break;
    case PROP_CONTENT_TYPE:
      gst_source_buffer_change_content_type (self,
          g_value_get_string (value), NULL);
      break;
    case PROP_TIMESTAMP_OFFSET:
      gst_source_buffer_set_timestamp_offset (self,
          g_value_get_int64 (value), NULL);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_source_buffer_class_init (GstSourceBufferClass * klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  oclass->constructed = GST_DEBUG_FUNCPTR (gst_source_buffer_constructed);
  oclass->dispose = GST_DEBUG_FUNCPTR (gst_source_buffer_dispose);
  oclass->finalize = GST_DEBUG_FUNCPTR (gst_source_buffer_finalize);
  oclass->get_property = GST_DEBUG_FUNCPTR (gst_source_buffer_get_property);
  oclass->set_property = GST_DEBUG_FUNCPTR (gst_source_buffer_set_property);

  /**
   * GstSourceBuffer:append-mode:
   *
   * Affects how timestamps of processed media segments are interpreted.
   * In %GST_SOURCE_BUFFER_APPEND_MODE_SEGMENTS, the start timestamp of a
   * processed media segment is used directly along with
   * #GstSourceBuffer:timestamp-offset .
   * In %GST_SOURCE_BUFFER_APPEND_MODE_SEQUENCE, the timestamp of a
   * processed media segment is ignored and replaced with the end time of the
   * most recently appended segment.
   *
   * [Specification](https://www.w3.org/TR/media-source-2/#dom-sourcebuffer-mode)
   *
   * Since: 1.24
   */
  properties[PROP_APPEND_MODE] = g_param_spec_enum ("append-mode",
      "Append Mode",
      "Either Segments or Sequence",
      GST_TYPE_SOURCE_BUFFER_APPEND_MODE, DEFAULT_APPEND_MODE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GstSourceBuffer:append-window-start:
   *
   * Any segments processed which end before this value will be ignored by this
   * Source Buffer.
   *
   * [Specification](https://www.w3.org/TR/media-source-2/#dom-sourcebuffer-appendwindowstart)
   *
   * Since: 1.24
   */
  properties[PROP_APPEND_WINDOW_START] =
      g_param_spec_uint64 ("append-window-start", "Append Window Start",
      "The timestamp representing the start of the append window", 0,
      GST_CLOCK_TIME_NONE, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  /**
   * GstSourceBuffer:append-window-end:
   *
   * Any segments processed which have a start time greater than this value will
   * be ignored by this Source Buffer.
   *
   * [Specification](https://www.w3.org/TR/media-source-2/#dom-sourcebuffer-appendwindowend)
   *
   * Since: 1.24
   */
  properties[PROP_APPEND_WINDOW_END] = g_param_spec_uint64 ("append-window-end",
      "Append Window End",
      "The timestamp representing the end of the append window",
      0, GST_CLOCK_TIME_NONE, GST_CLOCK_TIME_NONE, G_PARAM_READABLE |
      G_PARAM_STATIC_STRINGS);

  /**
   * GstSourceBuffer:buffered:
   *
   * The set of Time Intervals that have been loaded into the current Source
   * Buffer
   *
   * [Specification](https://www.w3.org/TR/media-source-2/#dom-sourcebuffer-buffered)
   *
   * Since: 1.24
   */
  properties[PROP_BUFFERED] = g_param_spec_boxed ("buffered",
      "Buffered Time Intervals",
      "The set of Time Intervals that have been loaded into"
      " the current Source Buffer", G_TYPE_ARRAY, G_PARAM_READABLE |
      G_PARAM_STATIC_STRINGS);

  /**
   * GstSourceBuffer:content-type:
   *
   * The MIME content-type of the data stream
   *
   * Since: 1.24
   */
  properties[PROP_CONTENT_TYPE] = g_param_spec_string ("content-type",
      "Content Type",
      "The MIME content-type of the data stream", NULL,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);

  /**
   * GstSourceBuffer:timestamp-offset:
   *
   * The next media segment appended to the current Source Buffer will have its
   * start timestamp increased by this amount.
   *
   * [Specification](https://www.w3.org/TR/media-source-2/#dom-sourcebuffer-timestampoffset)
   *
   * Since: 1.24
   */
  properties[PROP_TIMESTAMP_OFFSET] = g_param_spec_int64 ("timestamp-offset",
      "Timestamp Offset",
      "The next media segment appended to the current Source Buffer"
      " will have its start timestamp increased by this amount",
      0, G_MAXINT64, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GstSourceBuffer:updating:
   *
   * Whether the current source buffer is still asynchronously processing
   * previously issued commands.
   *
   * [Specification](https://www.w3.org/TR/media-source-2/#dom-sourcebuffer-updating)
   *
   * Since: 1.24
   */
  properties[PROP_UDPATING] = g_param_spec_boolean ("updating",
      "Updating",
      "Whether the current Source Buffer is still"
      " asynchronously processing previously issued commands",
      FALSE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (oclass, N_PROPS, properties);

  /**
   * GstSourceBuffer::on-update-start:
   * @self: The #GstSourceBuffer that has just started updating
   *
   * Emitted when @self has begun to process data after a call to
   * gst_source_buffer_append_buffer().
   *
   * [Specification](https://www.w3.org/TR/media-source-2/#dom-sourcebuffer-onupdatestart)
   *
   * Since: 1.24
   */
  signals[ON_UPDATE_START] = g_signal_new ("on-update-start",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);

  /**
   * GstSourceBuffer::on-update:
   * @self: The #GstSourceBuffer that has just updated
   *
   * Emitted when @self has successfully processed data after a call to
   * gst_source_buffer_append_buffer().
   *
   * [Specification](https://www.w3.org/TR/media-source-2/#dom-sourcebuffer-onupdate)
   *
   * Since: 1.24
   */
  signals[ON_UPDATE] = g_signal_new ("on-update",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);

  /**
   * GstSourceBuffer::on-update-end:
   * @self: The #GstSourceBuffer that is no longer updating
   *
   * Emitted when @self is no longer in the updating state after a call to
   * gst_source_buffer_append_buffer(). This can happen after a successful or
   * unsuccessful append.
   *
   * [Specification](https://www.w3.org/TR/media-source-2/#dom-sourcebuffer-onupdateend)
   *
   * Since: 1.24
   */
  signals[ON_UPDATE_END] = g_signal_new ("on-update-end",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);

  /**
   * GstSourceBuffer::on-error:
   * @self: The #GstSourceBuffer that has encountered an error
   *
   * Emitted when @self has encountered an error after a call to
   * gst_source_buffer_append_buffer().
   *
   * [Specification](https://www.w3.org/TR/media-source-2/#dom-sourcebuffer-onerror)
   *
   * Since: 1.24
   */
  signals[ON_ERROR] = g_signal_new ("on-error",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);

  /**
   * GstSourceBuffer::on-abort:
   * @self: The #GstSourceBuffer that has been aborted.
   *
   * Emitted when @self was aborted after a call to gst_source_buffer_abort().
   *
   * [Specification](https://www.w3.org/TR/media-source-2/#dom-sourcebuffer-onabort)
   *
   * Since: 1.24
   */
  signals[ON_ABORT] = g_signal_new ("on-abort",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
on_duration_changed (GstAppendPipeline * pipeline, gpointer user_data)
{
  GstSourceBuffer *self = GST_SOURCE_BUFFER (user_data);

  GST_OBJECT_LOCK (self);
  gboolean removed = is_removed_unlocked (self);
  GST_OBJECT_UNLOCK (self);

  if (removed) {
    return;
  }

  call_duration_changed (self);
}

static void
on_eos (GstAppendPipeline * pipeline, GstMediaSourceTrack * track,
    gpointer user_data)
{
  GstSourceBuffer *self = GST_SOURCE_BUFFER (user_data);

  if (GST_IS_MEDIA_SOURCE_TRACK (track)) {
    TRACKS_LOCK (self);
    GST_DEBUG_OBJECT (self, "got EOS event on %" GST_PTR_FORMAT, track);
    GstMediaSourceTrackBuffer *buffer = get_track_buffer_unlocked (self, track);
    gst_media_source_track_buffer_eos (buffer);
    TRACKS_UNLOCK (self);
  }
}

static void
on_error (GstAppendPipeline * pipeline, gpointer user_data)
{
  GstSourceBuffer *self = GST_SOURCE_BUFFER (user_data);

  GST_OBJECT_LOCK (self);
  append_error_unlocked (self);
  GST_OBJECT_UNLOCK (self);
}

static GstMediaSourceTrackBuffer *
get_track_buffer_unlocked (GstSourceBuffer * self, GstMediaSourceTrack * track)
{
  g_return_val_if_fail (g_hash_table_contains (self->track_buffers, track),
      NULL);
  return g_hash_table_lookup (self->track_buffers, track);
}

static inline TrackFeedTask *
get_track_feed_unlocked (GstSourceBuffer * self, GstMediaSourceTrack * track)
{
  g_return_val_if_fail (g_hash_table_contains (self->track_feeds, track), NULL);
  return g_hash_table_lookup (self->track_feeds, track);
}

static void
add_track_buffer_unlocked (GstMediaSourceTrack * track, GstSourceBuffer * self)
{
  const gchar *id = gst_media_source_track_get_id (track);
  if (g_hash_table_contains (self->track_buffers, track)) {
    GST_DEBUG_OBJECT (self, "already have a track buffer for track %s", id);
    return;
  }
  GstMediaSourceTrackBuffer *buf = gst_media_source_track_buffer_new ();
  g_hash_table_insert (self->track_buffers, track, buf);
  GST_DEBUG_OBJECT (self, "added track buffer for track %s", id);

  add_track_feed_unlocked (track, buf, self);
}

static void
add_track_feed_unlocked (GstMediaSourceTrack * track,
    GstMediaSourceTrackBuffer * track_buffer, GstSourceBuffer * self)
{
  TrackFeedTask *feed = g_new0 (TrackFeedTask, 1);
  GstTask *task = gst_task_new ((GstTaskFunction) track_feed_task, feed, NULL);
  g_rec_mutex_init (&feed->lock);
  gst_task_set_lock (task, &feed->lock);
  const gchar *track_id = gst_media_source_track_get_id (track);
  gchar *name = g_strdup_printf ("%s:%s", GST_OBJECT_NAME (self), track_id);
  g_object_set (task, "name", name, NULL);
  g_clear_pointer (&name, g_free);
  feed->task = task;
  feed->buffer = track_buffer;
  feed->track = gst_object_ref (track);
  g_weak_ref_init (&feed->parent, self);
  feed->cancelled = FALSE;
  g_hash_table_insert (self->track_feeds, track, feed);
}

static void
clear_track_feed (TrackFeedTask * feed)
{
  gst_object_unref (feed->task);
  g_rec_mutex_clear (&feed->lock);
  gst_object_unref (feed->track);
  g_weak_ref_clear (&feed->parent);
  g_free (feed);
}

static void
stop_track_feed (TrackFeedTask * feed)
{
  g_return_if_fail (feed != NULL);
  gst_media_source_track_flush (feed->track);
  g_atomic_int_set (&feed->cancelled, TRUE);
  gst_task_join (feed->task);
}

static void
start_track_feed (TrackFeedTask * feed)
{
  g_return_if_fail (feed != NULL);
  g_atomic_int_set (&feed->cancelled, FALSE);
  gst_media_source_track_resume (feed->track);
  gst_task_start (feed->task);
}

static void
reset_track_feed (TrackFeedTask * feed)
{
  stop_track_feed (feed);
  start_track_feed (feed);
}

static gboolean
is_within_append_window_unlocked (GstSourceBuffer * self, GstSample * sample)
{
  GstBuffer *buffer = gst_sample_get_buffer (sample);
  GstClockTime start = GST_BUFFER_PTS (buffer);
  GstClockTime end = start + GST_BUFFER_DURATION (buffer);

  if (start < self->append_window_start) {
    return FALSE;
  }

  if (!GST_CLOCK_TIME_IS_VALID (self->append_window_end)) {
    return TRUE;
  }

  return end <= self->append_window_end;
}

static void
on_new_sample (GstAppendPipeline * pipeline, GstMediaSourceTrack * track,
    GstSample * sample, gpointer user_data)
{
  GstSourceBuffer *self = GST_SOURCE_BUFFER (user_data);

  gboolean processed_init_segment =
      g_atomic_int_get (&self->processed_init_segment);
  g_return_if_fail (processed_init_segment);

  GST_OBJECT_LOCK (self);
  gboolean is_within_window = is_within_append_window_unlocked (self, sample);
  GST_OBJECT_UNLOCK (self);

  TRACKS_LOCK (self);
  if (is_within_window) {
    GstMediaSourceTrackBuffer *track_buffer =
        get_track_buffer_unlocked (self, track);
    GST_TRACE_OBJECT (self, "new sample on %s with %" GST_PTR_FORMAT,
        gst_media_source_track_get_id (track), gst_sample_get_buffer (sample));
    gst_media_source_track_buffer_add (track_buffer, sample);
    TrackFeedTask *feed = get_track_feed_unlocked (self, track);
    start_track_feed (feed);
  }
  TRACKS_UNLOCK (self);
}

static void
call_received_init_segment (GstSourceBuffer * self)
{
  GstSourceBufferCallbacks *callbacks = &self->callbacks.callbacks;
  if (callbacks->received_init_segment) {
    callbacks->received_init_segment (self, self->callbacks.user_data);
  }
}

static void
call_duration_changed (GstSourceBuffer * self)
{
  GstSourceBufferCallbacks *callbacks = &self->callbacks.callbacks;
  if (callbacks->duration_changed) {
    callbacks->duration_changed (self, self->callbacks.user_data);
  }
}

static void
call_active_state_changed (GstSourceBuffer * self)
{
  GstSourceBufferCallbacks *callbacks = &self->callbacks.callbacks;
  if (callbacks->active_state_changed) {
    callbacks->active_state_changed (self, self->callbacks.user_data);
  }
}

static void
update_track_buffer_modes (GstSourceBuffer * self)
{
  GHashTableIter iter;
  g_hash_table_iter_init (&iter, self->track_buffers);
  gboolean enabled =
      self->append_mode == GST_SOURCE_BUFFER_APPEND_MODE_SEQUENCE;
  for (gpointer value; g_hash_table_iter_next (&iter, NULL, &value);) {
    GstMediaSourceTrackBuffer *buffer = GST_MEDIA_SOURCE_TRACK_BUFFER (value);
    gst_media_source_track_buffer_process_init_segment (buffer, enabled);
    gst_media_source_track_buffer_set_group_start (buffer,
        self->timestamp_offset);
  }
}

static void
on_received_init_segment (GstAppendPipeline * pipeline, gpointer user_data)
{
  GstSourceBuffer *self = GST_SOURCE_BUFFER (user_data);
  GST_DEBUG_OBJECT (self, "got init segment, have duration %" GST_TIME_FORMAT,
      GST_TIME_ARGS (gst_append_pipeline_get_duration (pipeline)));

  TRACKS_LOCK (self);

  if (g_atomic_int_compare_and_exchange (&self->processed_init_segment, FALSE,
          TRUE)) {
    GST_DEBUG_OBJECT (self, "processing first init segment");

    GPtrArray *audio_tracks = gst_append_pipeline_get_audio_tracks (pipeline);
    GPtrArray *text_tracks = gst_append_pipeline_get_text_tracks (pipeline);
    GPtrArray *video_tracks = gst_append_pipeline_get_video_tracks (pipeline);

    g_ptr_array_foreach (audio_tracks, (GFunc) add_track_buffer_unlocked, self);
    g_ptr_array_foreach (text_tracks, (GFunc) add_track_buffer_unlocked, self);
    g_ptr_array_foreach (video_tracks, (GFunc) add_track_buffer_unlocked, self);
  }

  update_track_buffer_modes (self);

  TRACKS_UNLOCK (self);

  call_received_init_segment (self);
  call_active_state_changed (self);
}

static guint
track_buffer_hash (GstMediaSourceTrack * track)
{
  return g_str_hash (gst_media_source_track_get_id (track));
}

static gboolean
track_buffer_equal (GstMediaSourceTrack * a, GstMediaSourceTrack * b)
{
  return g_str_equal (gst_media_source_track_get_id (a),
      gst_media_source_track_get_id (b));
}

static void
gst_source_buffer_init (GstSourceBuffer * self)
{

  self->append_mode = DEFAULT_APPEND_MODE;
  self->append_window_start = 0;
  self->append_window_end = GST_CLOCK_TIME_NONE;
  self->content_type = NULL;
  self->timestamp_offset = 0;
  self->updating = FALSE;
  self->errored = FALSE;
  self->size_limit = DEFAULT_BUFFER_SIZE;
  self->size = 0;
  self->pending_data = NULL;
  g_cond_init (&self->pending_data_cond);
  self->processed_init_segment = FALSE;
  self->event_queue =
      gst_mse_event_queue_new ((GstMseEventQueueCallback) dispatch_event, self);

  self->append_to_buffer_task = append_to_buffer_task_new (self);
  self->track_buffers = g_hash_table_new_full ((GHashFunc) track_buffer_hash,
      (GEqualFunc) track_buffer_equal, NULL, gst_object_unref);
  g_mutex_init (&self->tracks_lock);

  self->track_feeds = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) clear_track_feed);
  self->seek_time = 0;
  self->callbacks.callbacks.duration_changed = NULL;
  self->callbacks.user_data = NULL;
}

static inline gboolean
is_removed_unlocked (GstSourceBuffer * self)
{
  GstObject *parent = GST_OBJECT_PARENT (self);
  if (parent == NULL) {
    return TRUE;
  }

  GstMediaSource *source = GST_MEDIA_SOURCE (parent);
  GstSourceBufferList *buffers = gst_media_source_get_source_buffers (source);
  gboolean removed = !gst_source_buffer_list_contains (buffers, self);

  gst_object_unref (buffers);

  return removed;
}

static inline gboolean
is_updating (GstSourceBuffer * self)
{
  return g_atomic_int_get (&self->updating);
}

static inline void
set_updating (GstSourceBuffer * self)
{
  g_atomic_int_set (&self->updating, TRUE);
}

static inline void
clear_updating (GstSourceBuffer * self)
{
  g_atomic_int_set (&self->updating, FALSE);
}

static inline gboolean
is_errored (GstSourceBuffer * self)
{
  return g_atomic_int_get (&self->errored);
}

static inline void
set_errored (GstSourceBuffer * self)
{
  g_atomic_int_set (&self->errored, TRUE);
}

static inline void
clear_errored (GstSourceBuffer * self)
{
  g_atomic_int_set (&self->errored, FALSE);
}

static inline gboolean
is_ended_unlocked (GstSourceBuffer * self)
{
  if (is_removed_unlocked (self)) {
    return TRUE;
  }

  GstMediaSource *source = get_media_source_unlocked (self);
  gboolean ended = gst_media_source_get_ready_state (source) ==
      GST_MEDIA_SOURCE_READY_STATE_ENDED;

  gst_object_unref (source);

  return ended;
}

static void
open_parent_unlocked (GstSourceBuffer * self)
{
  g_return_if_fail (!is_removed_unlocked (self));
  GstMediaSource *source = get_media_source_unlocked (self);
  gst_media_source_open (source);
  gst_object_unref (source);
}

/**
 * gst_source_buffer_get_append_mode:
 * @self: #GstSourceBuffer instance
 *
 * [Specification](https://www.w3.org/TR/media-source-2/#dom-sourcebuffer-mode)
 *
 * Returns: The current #GstSourceBufferAppendMode
 * Since: 1.24
 */
GstSourceBufferAppendMode
gst_source_buffer_get_append_mode (GstSourceBuffer * self)
{
  g_return_val_if_fail (GST_IS_SOURCE_BUFFER (self), DEFAULT_APPEND_MODE);
  GST_OBJECT_LOCK (self);
  GstSourceBufferAppendMode append_mode = self->append_mode;
  GST_OBJECT_UNLOCK (self);

  return append_mode;
}

/**
 * gst_source_buffer_set_append_mode:
 * @self: #GstSourceBuffer instance
 * @mode: #GstSourceBufferAppendMode the desired Append Mode
 * @error: (out) (optional) (nullable) (transfer full): the resulting error or `NULL`
 *
 * Changes the Append Mode of @self. This influences what timestamps will be
 * assigned to media processed by this Source Buffer. In Segment mode, the
 * timestamps in each segment determine the position of each sample after it
 * is processed. In Sequence mode, the timestamp of each processed sample is
 * generated based on the end of the most recently processed segment.
 *
 * [Specification](https://www.w3.org/TR/media-source-2/#dom-sourcebuffer-mode)
 *
 * Returns: `TRUE` on success, `FALSE` otherwise
 * Since: 1.24
 */
gboolean
gst_source_buffer_set_append_mode (GstSourceBuffer * self,
    GstSourceBufferAppendMode mode, GError ** error)
{
  g_return_val_if_fail (GST_IS_SOURCE_BUFFER (self), FALSE);

  GST_OBJECT_LOCK (self);

  if (is_removed_unlocked (self)) {
    g_set_error (error,
        GST_MEDIA_SOURCE_ERROR, GST_MEDIA_SOURCE_ERROR_INVALID_STATE,
        "buffer is removed");
    goto error;
  }

  if (is_updating (self)) {
    g_set_error (error,
        GST_MEDIA_SOURCE_ERROR, GST_MEDIA_SOURCE_ERROR_INVALID_STATE,
        "buffer is still updating");
    goto error;
  }

  if (self->generate_timestamps && mode ==
      GST_SOURCE_BUFFER_APPEND_MODE_SEGMENTS) {
    g_set_error (error,
        GST_MEDIA_SOURCE_ERROR, GST_MEDIA_SOURCE_ERROR_TYPE,
        "cannot change to segments mode while generate timestamps is active");
    goto error;
  }

  if (is_ended_unlocked (self)) {
    open_parent_unlocked (self);
  }

  self->append_mode = mode;

  GST_OBJECT_UNLOCK (self);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_APPEND_MODE]);

  return TRUE;

error:
  GST_OBJECT_UNLOCK (self);
  return FALSE;
}

/**
 * gst_source_buffer_get_append_window_start:
 * @self: #GstSourceBuffer instance
 *
 * Returns the current append window start time. Any segment processed that ends
 * earlier than this value will be ignored.
 *
 * [Specification](https://www.w3.org/TR/media-source-2/#dom-sourcebuffer-appendwindowstart)
 *
 * Returns: The current Append Window start time as a #GstClockTime
 * Since: 1.24
 */
GstClockTime
gst_source_buffer_get_append_window_start (GstSourceBuffer * self)
{
  g_return_val_if_fail (GST_IS_SOURCE_BUFFER (self), GST_CLOCK_TIME_NONE);

  GST_OBJECT_LOCK (self);
  GstClockTime append_window_start = self->append_window_start;
  GST_OBJECT_UNLOCK (self);

  return append_window_start;
}

/**
 * gst_source_buffer_set_append_window_start:
 * @self: #GstSourceBuffer instance
 * @start: the append window start
 * @error: (out) (optional) (nullable) (transfer full): the resulting error or `NULL`
 *
 * Modifies the current append window start of @self. If successful, samples
 * processed after setting this value that end before this point will be
 * ignored.
 *
 * [Specification](https://www.w3.org/TR/media-source-2/#dom-sourcebuffer-appendwindowstart)
 *
 * Returns: `TRUE` on success, `FALSE` otherwise
 * Since: 1.24
 */
gboolean
gst_source_buffer_set_append_window_start (GstSourceBuffer * self,
    GstClockTime start, GError ** error)
{
  g_return_val_if_fail (GST_IS_SOURCE_BUFFER (self), FALSE);

  GST_OBJECT_LOCK (self);

  if (is_removed_unlocked (self)) {
    g_set_error (error,
        GST_MEDIA_SOURCE_ERROR, GST_MEDIA_SOURCE_ERROR_INVALID_STATE,
        "append window start cannot be set on source buffer "
        "with no media source");
    goto error;
  }

  if (is_updating (self)) {
    g_set_error (error,
        GST_MEDIA_SOURCE_ERROR, GST_MEDIA_SOURCE_ERROR_INVALID_STATE,
        "append window start cannot be set on source buffer while updating");
    goto error;
  }

  if (!GST_CLOCK_TIME_IS_VALID (start) || start <= self->append_window_end) {
    g_set_error (error,
        GST_MEDIA_SOURCE_ERROR, GST_MEDIA_SOURCE_ERROR_TYPE,
        "append window start must be between zero and append window end");
    goto error;
  }

  self->append_window_start = start;

  GST_OBJECT_UNLOCK (self);

  g_object_notify_by_pspec (G_OBJECT (self),
      properties[PROP_APPEND_WINDOW_START]);

  return TRUE;

error:
  GST_OBJECT_UNLOCK (self);
  return FALSE;
}

/**
 * gst_source_buffer_get_append_window_end:
 * @self: #GstSourceBuffer instance
 *
 * Returns the current append window end time. Any segment processed that starts
 * after this value will be ignored.
 *
 * [Specification](https://www.w3.org/TR/media-source-2/#dom-sourcebuffer-appendwindowend)
 *
 * Returns: The current Append Window end time as a #GstClockTime
 * Since: 1.24
 */
GstClockTime
gst_source_buffer_get_append_window_end (GstSourceBuffer * self)
{
  g_return_val_if_fail (GST_IS_SOURCE_BUFFER (self), GST_CLOCK_TIME_NONE);

  GST_OBJECT_LOCK (self);
  GstClockTime append_window_end = self->append_window_end;
  GST_OBJECT_UNLOCK (self);

  return append_window_end;
}

/**
 * gst_source_buffer_set_append_window_end:
 * @self: #GstSourceBuffer instance
 * @end: the append window end
 * @error: (out) (optional) (nullable) (transfer full): the resulting error or `NULL`
 *
 * Modifies the current append window end of @self. If successful, samples
 * processed after setting this value that start after this point will be
 * ignored.
 *
 * [Specification](https://www.w3.org/TR/media-source-2/#dom-sourcebuffer-appendwindowend)
 *
 * Returns: `TRUE` on success, `FALSE` otherwise
 * Since: 1.24
 */
gboolean
gst_source_buffer_set_append_window_end (GstSourceBuffer * self,
    GstClockTime end, GError ** error)
{
  g_return_val_if_fail (GST_IS_SOURCE_BUFFER (self), FALSE);

  GST_OBJECT_LOCK (self);

  if (is_removed_unlocked (self)) {
    g_set_error (error,
        GST_MEDIA_SOURCE_ERROR, GST_MEDIA_SOURCE_ERROR_INVALID_STATE,
        "append window end cannot be set on source buffer "
        "with no media source");
    goto error;
  }

  if (is_updating (self)) {
    g_set_error (error,
        GST_MEDIA_SOURCE_ERROR, GST_MEDIA_SOURCE_ERROR_INVALID_STATE,
        "append window end cannot be set on source buffer while updating");
    goto error;
  }

  if (end <= self->append_window_start) {
    g_set_error (error,
        GST_MEDIA_SOURCE_ERROR, GST_MEDIA_SOURCE_ERROR_TYPE,
        "append window end must be after append window start");
    goto error;
  }

  self->append_window_end = end;

  GST_OBJECT_UNLOCK (self);

  g_object_notify_by_pspec (G_OBJECT (self),
      properties[PROP_APPEND_WINDOW_END]);

  return TRUE;

error:
  GST_OBJECT_UNLOCK (self);
  return FALSE;
}

static gboolean
get_intersection (GstMediaSourceRange * a, GstMediaSourceRange * b,
    GstMediaSourceRange * intersection)
{
  g_return_val_if_fail (a != NULL, FALSE);
  g_return_val_if_fail (b != NULL, FALSE);
  g_return_val_if_fail (intersection != NULL, FALSE);
  GstMediaSourceRange range = {
    .start = MAX (a->start, b->start),
    .end = MIN (a->end, b->end),
  };
  if (range.start >= range.end) {
    return FALSE;
  }
  *intersection = range;
  return TRUE;
}

static GArray *
intersect_ranges (GstMediaSourceRange * a, GstMediaSourceRange * a_end,
    GstMediaSourceRange * b, GstMediaSourceRange * b_end)
{
  GArray *intersection = g_array_new_ranges ();
  while (a < a_end && b < b_end) {
    GstMediaSourceRange range;
    if (!get_intersection (a, b, &range)) {
      if (a->end < b->end) {
        a++;
      } else {
        b++;
      }
      continue;
    }

    if (a->end < b->end) {
      a++;
    } else {
      b++;
    }

    g_array_append_val (intersection, range);
  }
  return intersection;
}

static inline gboolean
contributes_to_buffered (GstMediaSourceTrack * track)
{
  switch (gst_media_source_track_get_track_type (track)) {
    case GST_MEDIA_SOURCE_TRACK_TYPE_AUDIO:
    case GST_MEDIA_SOURCE_TRACK_TYPE_VIDEO:
      return TRUE;
    default:
      return FALSE;
  }
}

/**
 * gst_source_buffer_get_buffered:
 * @self: #GstSourceBuffer instance
 * @error: (out) (optional) (nullable) (transfer full): the resulting error or `NULL`
 *
 * Returns a sequence of #GstMediaSourceRange values representing which segments
 * of @self are buffered in memory.
 *
 * [Specification](https://www.w3.org/TR/media-source-2/#dom-sourcebuffer-buffered)
 *
 * Returns: (transfer full) (element-type GstMediaSourceRange): a #GArray of #GstMediaSourceRange values.
 * Since: 1.24
 */
GArray *
gst_source_buffer_get_buffered (GstSourceBuffer * self, GError ** error)
{
  g_return_val_if_fail (GST_IS_SOURCE_BUFFER (self), NULL);

  TRACKS_LOCK (self);
  GArray *buffered = get_buffered_unlocked (self);
  TRACKS_UNLOCK (self);
  return buffered;
}

static GArray *
get_buffered_unlocked (GstSourceBuffer * self)
{
  GHashTableIter iter;
  GArray *buffered = NULL;
  g_hash_table_iter_init (&iter, self->track_buffers);
  for (gpointer key, value; g_hash_table_iter_next (&iter, &key, &value);) {
    GstMediaSourceTrack *track = GST_MEDIA_SOURCE_TRACK (key);
    GstMediaSourceTrackBuffer *buffer = GST_MEDIA_SOURCE_TRACK_BUFFER (value);
    if (!contributes_to_buffered (track)) {
      continue;
    }
    GArray *current_ranges = gst_media_source_track_buffer_get_ranges (buffer);
    if (buffered == NULL) {
      buffered = current_ranges;
      continue;
    }
    GArray *intersection = intersect_ranges (
        (GstMediaSourceRange *) buffered->data,
        ((GstMediaSourceRange *) buffered->data) + buffered->len,
        (GstMediaSourceRange *) current_ranges->data,
        ((GstMediaSourceRange *) current_ranges->data) + current_ranges->len);
    g_array_unref (buffered);
    buffered = intersection;
  }

  if (buffered == NULL) {
    return g_array_new_ranges ();
  } else {
    return buffered;
  }
}

/**
 * gst_source_buffer_get_content_type:
 * @self: #GstSourceBuffer instance
 *
 * Returns the current content type of @self.
 *
 * Returns: (transfer full): a string representing the content type
 * Since: 1.24
 */
gchar *
gst_source_buffer_get_content_type (GstSourceBuffer * self)
{
  g_return_val_if_fail (GST_IS_SOURCE_BUFFER (self), NULL);

  GST_OBJECT_LOCK (self);
  gchar *content_type = g_strdup (self->content_type);
  GST_OBJECT_UNLOCK (self);

  return content_type;
}

/**
 * gst_source_buffer_change_content_type:
 * @self: #GstSourceBuffer instance
 * @type: (transfer none): the desired content type
 * @error: (out) (optional) (nullable) (transfer full): the resulting error or `NULL`
 *
 * Attempts to change the content type of @self to @type. Any new data appended
 * to the Source Buffer must be of the supplied @type afterward.
 *
 * Returns: `TRUE` on success, `FALSE` otherwise
 * Since: 1.24
 */
gboolean
gst_source_buffer_change_content_type (GstSourceBuffer * self,
    const gchar * type, GError ** error)
{
  g_return_val_if_fail (GST_IS_SOURCE_BUFFER (self), FALSE);

  if (type == NULL || g_strcmp0 (type, "") == 0) {
    g_set_error (error,
        GST_MEDIA_SOURCE_ERROR, GST_MEDIA_SOURCE_ERROR_TYPE,
        "content type must not be empty");
    return FALSE;
  }

  GST_OBJECT_LOCK (self);

  if (is_removed_unlocked (self)) {
    g_set_error (error,
        GST_MEDIA_SOURCE_ERROR, GST_MEDIA_SOURCE_ERROR_INVALID_STATE,
        "content type cannot be set on source buffer with no media source");
    goto error;
  }

  if (is_updating (self)) {
    g_set_error (error,
        GST_MEDIA_SOURCE_ERROR, GST_MEDIA_SOURCE_ERROR_INVALID_STATE,
        "content type cannot be set on source buffer that is updating");
    goto error;
  }

  g_set_error (error,
      GST_MEDIA_SOURCE_ERROR, GST_MEDIA_SOURCE_ERROR_NOT_SUPPORTED,
      "content type cannot be changed");

error:
  GST_OBJECT_UNLOCK (self);
  return FALSE;
}

/**
 * gst_source_buffer_remove:
 * @self: #GstSourceBuffer instance
 * @start: The beginning timestamp of data to remove
 * @end: The end timestamp of data to remove
 * @error: (out) (optional) (nullable) (transfer full): the resulting error or `NULL`
 *
 * Attempts to remove any parsed data between @start and @end from @self.
 *
 * [Specification](https://www.w3.org/TR/media-source-2/#dom-sourcebuffer-remove)
 *
 * Returns: `TRUE` on success, `FALSE` otherwise
 * Since: 1.24
 */
gboolean
gst_source_buffer_remove (GstSourceBuffer * self, GstClockTime start,
    GstClockTime end, GError ** error)
{
  g_return_val_if_fail (GST_IS_SOURCE_BUFFER (self), FALSE);
  return TRUE;
}

/**
 * gst_source_buffer_get_timestamp_offset:
 * @self: #GstSourceBuffer instance
 *
 * [Specification](https://www.w3.org/TR/media-source-2/#dom-sourcebuffer-timestampoffset)
 *
 * Returns: The current timestamp offset as a #GstClockTime
 * Since: 1.24
 */
GstClockTime
gst_source_buffer_get_timestamp_offset (GstSourceBuffer * self)
{
  g_return_val_if_fail (GST_IS_SOURCE_BUFFER (self), FALSE);

  GST_OBJECT_LOCK (self);
  GstClockTime timestamp_offset = self->timestamp_offset;
  GST_OBJECT_UNLOCK (self);

  return timestamp_offset;
}

/**
 * gst_source_buffer_set_timestamp_offset:
 * @self: #GstSourceBuffer instance
 * @offset: The new timestamp offset
 * @error: (out) (optional) (nullable) (transfer full): the resulting error or `NULL`
 *
 * Attempt to set the timestamp offset of @self. Any media processed after this
 * value is set will have this value added to its start time.
 *
 * [Specification](https://www.w3.org/TR/media-source-2/#dom-sourcebuffer-timestampoffset)
 *
 * Returns: `TRUE` on success, `FALSE` otherwise
 * Since: 1.24
 */
gboolean
gst_source_buffer_set_timestamp_offset (GstSourceBuffer * self, GstClockTime
    offset, GError ** error)
{
  g_return_val_if_fail (GST_IS_SOURCE_BUFFER (self), FALSE);

  GST_OBJECT_LOCK (self);

  if (is_removed_unlocked (self)) {
    g_set_error (error,
        GST_MEDIA_SOURCE_ERROR, GST_MEDIA_SOURCE_ERROR_INVALID_STATE,
        "source buffer is removed");
    goto error;
  }
  if (is_updating (self)) {
    g_set_error (error,
        GST_MEDIA_SOURCE_ERROR, GST_MEDIA_SOURCE_ERROR_INVALID_STATE,
        "source buffer is still updating");
    goto error;
  }
  if (is_ended_unlocked (self)) {
    GstMediaSource *parent = get_media_source_unlocked (self);
    gst_media_source_open (parent);
    gst_clear_object (&parent);
  }
  TRACKS_LOCK (self);
  GHashTableIter iter;
  g_hash_table_iter_init (&iter, self->track_buffers);
  for (gpointer value; g_hash_table_iter_next (&iter, NULL, &value);) {
    GstMediaSourceTrackBuffer *buffer = value;
    gst_media_source_track_buffer_set_group_start (buffer, offset);
  }
  TRACKS_UNLOCK (self);

  self->timestamp_offset = offset;

  GST_OBJECT_UNLOCK (self);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TIMESTAMP_OFFSET]);

  return TRUE;

error:
  GST_OBJECT_UNLOCK (self);
  return FALSE;
}

/**
 * gst_source_buffer_get_updating:
 * @self: #GstSourceBuffer instance
 *
 * [Specification](https://www.w3.org/TR/media-source-2/#dom-sourcebuffer-updating)
 *
 * Returns: Whether @self is currently adding or removing media content.
 * Since: 1.24
 */
gboolean
gst_source_buffer_get_updating (GstSourceBuffer * self)
{
  g_return_val_if_fail (GST_IS_SOURCE_BUFFER (self), FALSE);
  return is_updating (self);
}

static gsize
compute_total_size_unlocked (GstSourceBuffer * self)
{
  GHashTableIter iter;
  g_hash_table_iter_init (&iter, self->track_buffers);
  gsize total_size = 0;
  for (gpointer value; g_hash_table_iter_next (&iter, NULL, &value);) {
    GstMediaSourceTrackBuffer *buffer = value;
    total_size += gst_media_source_track_buffer_get_storage_size (buffer);
  }
  return total_size;
}

static gboolean
will_overflow_unlocked (GstSourceBuffer * self, gsize bytes)
{
  gsize total = compute_total_size_unlocked (self);
  return total + bytes > self->size_limit;
}

static void
evict_coded_frames_unlocked (GstSourceBuffer * self, gsize space_required,
    gsize size_limit, GstClockTime position, GstClockTime duration)
{
  if (!will_overflow_unlocked (self, space_required)) {
    return;
  }

  if (!GST_CLOCK_TIME_IS_VALID (position)) {
    GST_ERROR_OBJECT (self, "invalid position, cannot delete anything");
    return;
  }

  GstClockTime min_distance_from_position = GST_SECOND * 5;
  GstClockTime max_dts = position > min_distance_from_position ?
      position - min_distance_from_position : 0;

  GST_DEBUG_OBJECT (self, "position=%" GST_TIMEP_FORMAT
      ", attempting removal from 0 to %" GST_TIMEP_FORMAT, &position, &max_dts);

  GHashTableIter iter;
  g_hash_table_iter_init (&iter, self->track_buffers);
  for (gpointer value; g_hash_table_iter_next (&iter, NULL, &value);) {
    GstMediaSourceTrackBuffer *buffer = value;
    gst_media_source_track_buffer_remove_range (buffer, 0, max_dts);
  }
  self->size = compute_total_size_unlocked (self);

  GST_DEBUG_OBJECT (self, "capacity=%" G_GSIZE_FORMAT "/%" G_GSIZE_FORMAT
      "(%" G_GSIZE_FORMAT "%%)", self->size, self->size_limit,
      self->size * 100 / size_limit);
}

static void
reset_parser_state_unlocked (GstSourceBuffer * self)
{
  clear_pending_data_unlocked (self);
  if (gst_append_pipeline_reset (self->append_pipeline)) {
    clear_errored (self);
  } else {
    set_errored (self);
  }
}

static void
append_error_unlocked (GstSourceBuffer * self)
{
  reset_parser_state_unlocked (self);
  clear_updating (self);

  if (is_removed_unlocked (self)) {
    return;
  }

  schedule_event_unlocked (self, ON_ERROR);
  schedule_event_unlocked (self, ON_UPDATE_END);

  GstMediaSource *source = get_media_source_unlocked (self);
  gst_media_source_end_of_stream (source, GST_MEDIA_SOURCE_EOS_ERROR_DECODE,
      NULL);
  gst_object_unref (source);
}

static void
append_successful_unlocked (GstSourceBuffer * self, gboolean ended)
{
  clear_updating (self);
  schedule_event_unlocked (self, ON_UPDATE);
  schedule_event_unlocked (self, ON_UPDATE_END);
}

static gboolean
encountered_bad_bytes_unlocked (GstSourceBuffer * self)
{
  return gst_append_pipeline_get_failed (self->append_pipeline);
}

static GstBuffer *
await_pending_data_unlocked (GstSourceBuffer * self)
{
  if (self->pending_data == NULL) {
    guint64 deadline = g_get_monotonic_time () + G_TIME_SPAN_SECOND;
    g_cond_wait_until (&self->pending_data_cond, GST_OBJECT_GET_LOCK (self),
        deadline);
  }
  return take_pending_data_unlocked (self);
}

static AppendToBufferTask *
append_to_buffer_task_new (GstSourceBuffer * parent)
{
  AppendToBufferTask *task = g_new0 (AppendToBufferTask, 1);
  g_rec_mutex_init (&task->lock);
  task->parent = parent;
  task->task =
      gst_task_new ((GstTaskFunction) append_to_buffer_task_func, task, NULL);
  task->cancelled = FALSE;
  gst_task_set_lock (task->task, &task->lock);
  return task;
}

static void
append_to_buffer_task_free (AppendToBufferTask * task)
{
  append_to_buffer_task_stop (task);
  gst_task_join (task->task);
  gst_clear_object (&task->task);
  g_rec_mutex_clear (&task->lock);
  task->parent = NULL;
  g_free (task);
}

static void
append_to_buffer_task_start (AppendToBufferTask * task)
{
  gchar *name = g_strdup_printf ("%s:append", GST_OBJECT_NAME (task->parent));
  g_object_set (task->task, "name", name, NULL);
  gst_task_start (task->task);
  g_free (name);
}

static void
append_to_buffer_task_stop (AppendToBufferTask * task)
{
  gst_task_stop (task->task);
  g_atomic_int_set (&task->cancelled, TRUE);
  g_cond_signal (&task->parent->pending_data_cond);
}

static void
append_to_buffer_task_func (AppendToBufferTask * task)
{
  GstSourceBuffer *self = task->parent;
  if (g_atomic_int_get (&task->cancelled)) {
    GST_LOG_OBJECT (task->task, "task is done");
    append_to_buffer_task_stop (task);
    return;
  }

  GST_OBJECT_LOCK (self);

  GstBuffer *pending_data = await_pending_data_unlocked (self);

  if (is_removed_unlocked (self)) {
    append_successful_unlocked (self, TRUE);
    goto done;
  }

  if (encountered_bad_bytes_unlocked (self)) {
    append_error_unlocked (self);
    goto done;
  }

  if (!GST_IS_BUFFER (pending_data)) {
    GST_TRACE_OBJECT (self, "no pending data");
    append_successful_unlocked (self, is_ended_unlocked (self));
    goto done;
  }

  GstFlowReturn result = gst_append_pipeline_append (self->append_pipeline,
      pending_data);

  if (result != GST_FLOW_OK) {
    GST_ERROR_OBJECT (self, "failed to append: %s", gst_flow_get_name (result));
    append_error_unlocked (self);
    goto done;
  }

  append_successful_unlocked (self, is_ended_unlocked (self));

done:
  GST_OBJECT_UNLOCK (self);
}

static gboolean
track_feed_fold (const GValue * item, TrackFeedAccumulator * acc,
    TrackFeedTask * feed)
{
  if (g_atomic_int_get (&feed->cancelled)) {
    return FALSE;
  }
  GstSourceBuffer *self = acc->parent;

  GstMediaSourceCodedFrameGroup *group =
      gst_value_get_media_source_coded_frame_group (item);
  if (group == NULL) {
    return FALSE;
  }

  for (GList * it = group->samples; it != NULL; it = g_list_next (it)) {
    GstSample *sample = GST_SAMPLE (it->data);
    if (!g_hash_table_add (acc->processed_samples, gst_sample_ref (sample))) {
      continue;
    }
    if (!gst_media_source_track_push (feed->track, sample)) {
      g_hash_table_remove (acc->processed_samples, sample);
      GST_LOG_OBJECT (self, "%s: failed to push sample to track",
          gst_media_source_track_get_id (feed->track));
      acc->push_failed = TRUE;
      return FALSE;
    }
  }

  return TRUE;
}

static gint
clip_to_seek_time_dts (const GValue * a, const GValue * b)
{
  GstMediaSourceCodedFrameGroup *group =
      gst_value_get_media_source_coded_frame_group (a);
  GstClockTime start_dts = g_value_get_uint64 (b);
  return group != NULL && start_dts > group->end;
}

static void
track_feed_task (TrackFeedTask * feed)
{
  GstSourceBuffer *self = g_weak_ref_get (&feed->parent);
  if (self == NULL) {
    gst_task_stop (feed->task);
    return;
  }
  GstMediaSourceTrack *track = feed->track;
  GstMediaSourceTrackBuffer *buffer = feed->buffer;
  GstClockTime time = self->seek_time;
  const gchar *track_id = gst_media_source_track_get_id (track);

  GST_DEBUG_OBJECT (self, "%s: feed starting@%" GST_TIMEP_FORMAT, track_id,
      &time);

  TrackFeedAccumulator acc = {
    .parent = self,
    .processed_samples = g_hash_table_new_full (g_direct_hash, g_direct_equal,
        (GDestroyNotify) gst_sample_unref, NULL),
    .push_failed = FALSE,
  };
  GValue start_dts_value = G_VALUE_INIT;
  g_value_init (&start_dts_value, G_TYPE_UINT64);
  g_value_set_uint64 (&start_dts_value, time);
  while (TRUE) {
    gboolean eos = gst_media_source_track_buffer_is_eos (buffer);
    GstIterator *it = gst_media_source_track_buffer_iter_samples (buffer);
    it = gst_iterator_filter (it, (GCompareFunc) clip_to_seek_time_dts,
        &start_dts_value);
    gst_iterator_fold (it, (GstIteratorFoldFunction) track_feed_fold,
        (GValue *) & acc, feed);
    g_clear_pointer (&it, gst_iterator_free);

    if (acc.push_failed) {
      gst_task_stop (feed->task);
      break;
    }

    if (eos) {
      GST_DEBUG_OBJECT (self, "%s: enqueued all %u samples", track_id,
          g_hash_table_size (acc.processed_samples));
      gst_media_source_track_push_eos (track);
      GST_DEBUG_OBJECT (self, "%s: marked EOS", track_id);
      gst_task_stop (feed->task);
      break;
    }

    if (g_atomic_int_get (&feed->cancelled)) {
      GST_DEBUG_OBJECT (self, "feed is cancelled, stopping task");
      gst_task_stop (feed->task);
      break;
    }

    GST_TRACE_OBJECT (self, "%s: resume after %u samples",
        track_id, g_hash_table_size (acc.processed_samples));
    gint64 deadline = g_get_monotonic_time () + G_TIME_SPAN_SECOND;
    gst_media_source_track_buffer_await_new_data_until (buffer, deadline);
  }
  g_clear_pointer (&acc.processed_samples, g_hash_table_unref);
  g_value_unset (&start_dts_value);
  gst_clear_object (&self);
}

static void
dispatch_event (SourceBufferEventItem * item, GstSourceBuffer * self)
{
  g_signal_emit (self, signals[item->event], 0);
}

static void
schedule_event_unlocked (GstSourceBuffer * self, SourceBufferEvent event)
{
  g_return_if_fail (event < N_SIGNALS);
  if (is_removed_unlocked (self)) {
    return;
  }
  SourceBufferEventItem item = {
    .item = {.destroy = g_free,.visible = TRUE,.size = 1,.object = NULL},
    .event = event,
  };
  gst_mse_event_queue_push (self->event_queue, g_memdup2 (&item,
          sizeof (SourceBufferEventItem)));
}

static void
schedule_append_to_buffer_task (GstSourceBuffer * self)
{
  g_cond_signal (&self->pending_data_cond);
}

/**
 * gst_source_buffer_append_buffer:
 * @self: #GstSourceBuffer instance
 * @buf: (transfer full):The media data to append
 * @error: (out) (optional) (nullable) (transfer full): the resulting error or `NULL`
 *
 * Schedules the bytes inside @buf to be processed by @self. When it is possible
 * to accept the supplied data, it will be processed asynchronously and fill in
 * the track buffers for playback purposes.
 *
 * [Specification](https://www.w3.org/TR/media-source-2/#dom-sourcebuffer-appendbuffer)
 *
 * Returns: `TRUE` on success, `FALSE` otherwise
 * Since: 1.24
 */
gboolean
gst_source_buffer_append_buffer (GstSourceBuffer * self, GstBuffer * buf,
    GError ** error)
{
  g_return_val_if_fail (GST_IS_SOURCE_BUFFER (self), FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (buf), FALSE);

  GST_OBJECT_LOCK (self);
  TRACKS_LOCK (self);

  if (is_removed_unlocked (self) || is_updating (self)) {
    g_set_error (error,
        GST_MEDIA_SOURCE_ERROR, GST_MEDIA_SOURCE_ERROR_INVALID_STATE,
        "source buffer is removed or still updating");
    goto error;
  }

  if (is_errored (self)) {
    g_set_error (error,
        GST_MEDIA_SOURCE_ERROR, GST_MEDIA_SOURCE_ERROR_INVALID_STATE,
        "source buffer has encountered error");
    goto error;
  }

  if (is_ended_unlocked (self)) {
    open_parent_unlocked (self);
  }

  GstMediaSource *source = get_media_source_unlocked (self);
  gsize buffer_size = gst_buffer_get_size (buf);
  GstClockTime position = gst_media_source_get_position (source);
  GstClockTime duration = gst_media_source_get_duration (source);

  gst_object_unref (source);

  evict_coded_frames_unlocked (self, buffer_size, self->size_limit, position,
      duration);

  if (will_overflow_unlocked (self, buffer_size)) {
    g_set_error (error,
        GST_MEDIA_SOURCE_ERROR, GST_MEDIA_SOURCE_ERROR_QUOTA_EXCEEDED,
        "buffer is full");
    goto error;
  }

  g_return_val_if_fail (self->pending_data == NULL, FALSE);

  set_pending_data_unlocked (self, buf);
  set_updating (self);

  schedule_event_unlocked (self, ON_UPDATE_START);
  schedule_append_to_buffer_task (self);

  TRACKS_UNLOCK (self);
  GST_OBJECT_UNLOCK (self);
  return TRUE;

error:
  TRACKS_UNLOCK (self);
  GST_OBJECT_UNLOCK (self);
  gst_clear_buffer (&buf);
  return FALSE;
}

/**
 * gst_source_buffer_abort:
 * @self: #GstSourceBuffer instance
 * @error: (out) (optional) (nullable) (transfer full): the resulting error or `NULL`
 *
 * Attempts to end any processing of the currently pending data and reset the
 * media parser.
 *
 * [Specification](https://www.w3.org/TR/media-source-2/#dom-sourcebuffer-abort)
 *
 * Returns: `TRUE` on success, `FALSE` otherwise
 * Since: 1.24
 */
gboolean
gst_source_buffer_abort (GstSourceBuffer * self, GError ** error)
{
  g_return_val_if_fail (GST_IS_SOURCE_BUFFER (self), FALSE);
  if (gst_append_pipeline_eos (self->append_pipeline) == GST_FLOW_OK) {
    return TRUE;
  } else {
    g_set_error (error,
        GST_MEDIA_SOURCE_ERROR, GST_MEDIA_SOURCE_ERROR_INVALID_STATE,
        "failed to abort source buffer");
    return FALSE;
  }
}

gboolean
gst_source_buffer_has_init_segment (GstSourceBuffer * self)
{
  g_return_val_if_fail (GST_IS_SOURCE_BUFFER (self), FALSE);
  return gst_append_pipeline_has_init_segment (self->append_pipeline);
}

static gboolean
is_buffered_fold (const GValue * item, IsBufferedAccumulator * acc,
    GstSourceBuffer * self)
{
  GstMediaSourceCodedFrameGroup *group =
      gst_value_get_media_source_coded_frame_group (item);
  if (group == NULL) {
    acc->buffered = FALSE;
    return FALSE;
  }
  GstClockTime start = group->start;
  GstClockTime end = group->end;
  if (acc->time < start) {
    GST_TRACE_OBJECT (self, "position precedes group start, done");
    acc->buffered = FALSE;
    return FALSE;
  }
  if (acc->time >= start && acc->time < end) {
    GST_TRACE_OBJECT (self, "position is within group, done");
    acc->buffered = TRUE;
    return FALSE;
  }
  return TRUE;
}

static gboolean
is_buffered_unlocked (GstSourceBuffer * self, GstClockTime time)
{
  gboolean buffered = TRUE;
  GHashTableIter iter;
  g_hash_table_iter_init (&iter, self->track_buffers);
  for (gpointer key, value;
      buffered && g_hash_table_iter_next (&iter, &key, &value);) {
    GstMediaSourceTrack *track = key;
    if (!gst_media_source_track_get_active (track)) {
      continue;
    }
    GstMediaSourceTrackBuffer *track_buffer = value;
    IsBufferedAccumulator acc = {
      .time = time,
      .buffered = FALSE,
    };
    GstIterator *it = gst_media_source_track_buffer_iter_samples (track_buffer);
    gst_iterator_fold (it, (GstIteratorFoldFunction) is_buffered_fold,
        (GValue *) & acc, self);
    g_clear_pointer (&it, gst_iterator_free);
    buffered = acc.buffered;
  }
  return buffered;
}

gboolean
gst_source_buffer_is_buffered (GstSourceBuffer * self, GstClockTime time)
{
  TRACKS_LOCK (self);
  gboolean buffered = is_buffered_unlocked (self, time);
  TRACKS_UNLOCK (self);
  return buffered;
}

static gboolean
is_range_buffered_fold (const GValue * item, IsRangeBufferedAccumulator * acc,
    GstSourceBuffer * self)
{
  GstMediaSourceCodedFrameGroup *group =
      gst_value_get_media_source_coded_frame_group (item);
  if (group == NULL) {
    return FALSE;
  }
  GstClockTime buffer_start = group->start;
  GstClockTime buffer_end = group->end;

  GstClockTime start = acc->start;
  GstClockTime end = acc->end;

  if (!acc->start_buffered) {
    if (start < buffer_start) {
      GST_TRACE_OBJECT (self, "start position precedes buffer start, done");
      return FALSE;
    }
    if (start >= buffer_start && start < buffer_end) {
      GST_TRACE_OBJECT (self, "start position is within buffer, checking end");
      acc->start_buffered = TRUE;
      return TRUE;
    }
  } else {
    if (end < buffer_start) {
      GST_TRACE_OBJECT (self, "end position precedes buffer start, done");
      return FALSE;
    }
    if (end <= buffer_end) {
      GST_TRACE_OBJECT (self, "end position is within buffer, done");
      acc->end_buffered = TRUE;
      return FALSE;
    }
  }

  return TRUE;
}

gboolean
gst_source_buffer_is_range_buffered (GstSourceBuffer * self, GstClockTime start,
    GstClockTime end)
{
  GHashTableIter iter;
  gboolean buffered = TRUE;
  TRACKS_LOCK (self);
  g_hash_table_iter_init (&iter, self->track_buffers);
  for (gpointer key, value;
      buffered && g_hash_table_iter_next (&iter, &key, &value);) {
    GstMediaSourceTrack *track = key;
    if (!gst_media_source_track_get_active (track)) {
      continue;
    }
    GstMediaSourceTrackBuffer *track_buffer = value;
    IsRangeBufferedAccumulator acc = {
      .start = start,
      .end = end,
      .start_buffered = FALSE,
      .end_buffered = FALSE,
    };
    GstIterator *it = gst_media_source_track_buffer_iter_samples (track_buffer);
    gst_iterator_fold (it, (GstIteratorFoldFunction) is_range_buffered_fold,
        (GValue *) & acc, self);
    g_clear_pointer (&it, gst_iterator_free);
    buffered = acc.end_buffered;
  }
  TRACKS_UNLOCK (self);
  return buffered;
}

GstClockTime
gst_source_buffer_get_duration (GstSourceBuffer * self)
{
  g_return_val_if_fail (GST_IS_SOURCE_BUFFER (self), GST_CLOCK_TIME_NONE);
  return gst_append_pipeline_get_duration (self->append_pipeline);
}

void
gst_source_buffer_teardown (GstSourceBuffer * self)
{
  GST_OBJECT_LOCK (self);
  reset_parser_state_unlocked (self);
  clear_updating (self);
  GST_OBJECT_UNLOCK (self);
}

GPtrArray *
gst_source_buffer_get_all_tracks (GstSourceBuffer * self)
{
  g_return_val_if_fail (GST_IS_SOURCE_BUFFER (self), NULL);

  GPtrArray *tracks = g_ptr_array_new ();

  GPtrArray *audio_tracks = gst_append_pipeline_get_audio_tracks
      (self->append_pipeline);
  GPtrArray *text_tracks = gst_append_pipeline_get_text_tracks
      (self->append_pipeline);
  GPtrArray *video_tracks = gst_append_pipeline_get_video_tracks
      (self->append_pipeline);

  if (audio_tracks) {
    g_ptr_array_extend (tracks, audio_tracks, NULL, NULL);
  }
  if (text_tracks) {
    g_ptr_array_extend (tracks, text_tracks, NULL, NULL);
  }
  if (video_tracks) {
    g_ptr_array_extend (tracks, video_tracks, NULL, NULL);
  }

  return tracks;
}

static void
seek_track_buffer_unlocked (GstMediaSourceTrack * track,
    GstMediaSourceTrackBuffer * buffer, GstSourceBuffer * self)
{
  TrackFeedTask *feed = get_track_feed_unlocked (self, track);

  const gchar *track_id = gst_media_source_track_get_id (track);
  GST_DEBUG_OBJECT (self, "%s: seeking", track_id);
  reset_track_feed (feed);
  GST_DEBUG_OBJECT (self, "%s: restarted track feed", track_id);
}

void
gst_source_buffer_seek (GstSourceBuffer * self, GstClockTime time)
{
  g_return_if_fail (GST_IS_SOURCE_BUFFER (self));
  g_return_if_fail (GST_CLOCK_TIME_IS_VALID (time));

  TRACKS_LOCK (self);
  self->seek_time = time;
  g_hash_table_foreach (self->track_buffers,
      (GHFunc) seek_track_buffer_unlocked, self);
  TRACKS_UNLOCK (self);
}

gboolean
gst_source_buffer_get_active (GstSourceBuffer * self)
{
  gboolean active = FALSE;
  GHashTableIter iter;

  TRACKS_LOCK (self);
  g_hash_table_iter_init (&iter, self->track_buffers);
  for (gpointer key; !active && g_hash_table_iter_next (&iter, &key, NULL);) {
    GstMediaSourceTrack *track = GST_MEDIA_SOURCE_TRACK (key);
    active |= gst_media_source_track_get_active (track);
  }
  TRACKS_UNLOCK (self);

  return active;
}
