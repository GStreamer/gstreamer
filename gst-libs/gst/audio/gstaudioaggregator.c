/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2001 Thomas <thomas@apestaart.org>
 *               2005,2006 Wim Taymans <wim@fluendo.com>
 *                    2013 Sebastian Dröge <sebastian@centricular.com>
 *                    2014 Collabora
 *                             Olivier Crete <olivier.crete@collabora.com>
 *
 * gstaudioaggregator.c:
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
 * SECTION: gstaudioaggregator
 * @short_description: manages a set of pads with the purpose of
 * aggregating their buffers for raw audio
 * @see_also: #GstAggregator
 *
 */


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstaudioaggregator.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (audio_aggregator_debug);
#define GST_CAT_DEFAULT audio_aggregator_debug

struct _GstAudioAggregatorPadPrivate
{
  /* All members are protected by the pad object lock */

  GstBuffer *buffer;            /* current buffer we're mixing,
                                   for comparison with collect.buffer
                                   to see if we need to update our
                                   cached values. */
  guint position, size;

  guint64 output_offset;        /* Sample offset in output segment relative to
                                   segment.start that collect.pos refers to in the
                                   current buffer. */

  guint64 next_offset;          /* Next expected sample offset in the input segment
                                   relative to segment.start */

  /* Last time we noticed a discont */
  GstClockTime discont_time;

  /* A new unhandled segment event has been received */
  gboolean new_segment;
};


/*****************************************
 * GstAudioAggregatorPad implementation  *
 *****************************************/
G_DEFINE_TYPE (GstAudioAggregatorPad, gst_audio_aggregator_pad,
    GST_TYPE_AGGREGATOR_PAD);

static GstFlowReturn
gst_audio_aggregator_pad_flush_pad (GstAggregatorPad * aggpad,
    GstAggregator * aggregator);

static void
gst_audio_aggregator_pad_finalize (GObject * object)
{
  GstAudioAggregatorPad *pad = (GstAudioAggregatorPad *) object;

  gst_buffer_replace (&pad->priv->buffer, NULL);

  G_OBJECT_CLASS (gst_audio_aggregator_pad_parent_class)->finalize (object);
}

static void
gst_audio_aggregator_pad_class_init (GstAudioAggregatorPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstAggregatorPadClass *aggpadclass = (GstAggregatorPadClass *) klass;

  g_type_class_add_private (klass, sizeof (GstAudioAggregatorPadPrivate));

  gobject_class->finalize = gst_audio_aggregator_pad_finalize;
  aggpadclass->flush = GST_DEBUG_FUNCPTR (gst_audio_aggregator_pad_flush_pad);
}

static void
gst_audio_aggregator_pad_init (GstAudioAggregatorPad * pad)
{
  pad->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (pad, GST_TYPE_AUDIO_AGGREGATOR_PAD,
      GstAudioAggregatorPadPrivate);

  gst_audio_info_init (&pad->info);

  pad->priv->buffer = NULL;
  pad->priv->position = 0;
  pad->priv->size = 0;
  pad->priv->output_offset = -1;
  pad->priv->next_offset = -1;
  pad->priv->discont_time = GST_CLOCK_TIME_NONE;
}


static GstFlowReturn
gst_audio_aggregator_pad_flush_pad (GstAggregatorPad * aggpad,
    GstAggregator * aggregator)
{
  GstAudioAggregatorPad *pad = GST_AUDIO_AGGREGATOR_PAD (aggpad);

  GST_OBJECT_LOCK (aggpad);
  pad->priv->position = pad->priv->size = 0;
  pad->priv->output_offset = pad->priv->next_offset = -1;
  pad->priv->discont_time = GST_CLOCK_TIME_NONE;
  gst_buffer_replace (&pad->priv->buffer, NULL);
  GST_OBJECT_UNLOCK (aggpad);

  return GST_FLOW_OK;
}



/**************************************
 * GstAudioAggregator implementation  *
 **************************************/

struct _GstAudioAggregatorPrivate
{
  GMutex mutex;

  gboolean send_caps;           /* aagg lock */

  /* All three properties are unprotected, can't be modified while streaming */
  /* Size in frames that is output per buffer */
  GstClockTime output_buffer_duration;
  GstClockTime alignment_threshold;
  GstClockTime discont_wait;

  /* Protected by srcpad stream clock */
  /* Buffer starting at offset containing block_size frames */
  GstBuffer *current_buffer;

  /* counters to keep track of timestamps */
  /* Readable with object lock, writable with both aag lock and object lock */

  gint64 offset;                /* Sample offset starting from 0 at segment.start */
};

#define GST_AUDIO_AGGREGATOR_LOCK(self)   g_mutex_lock (&(self)->priv->mutex);
#define GST_AUDIO_AGGREGATOR_UNLOCK(self) g_mutex_unlock (&(self)->priv->mutex);

static void gst_audio_aggregator_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_audio_aggregator_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_audio_aggregator_dispose (GObject * object);

static gboolean gst_audio_aggregator_src_event (GstAggregator * agg,
    GstEvent * event);
static gboolean gst_audio_aggregator_sink_event (GstAggregator * agg,
    GstAggregatorPad * aggpad, GstEvent * event);
static gboolean gst_audio_aggregator_src_query (GstAggregator * agg,
    GstQuery * query);
static gboolean gst_audio_aggregator_start (GstAggregator * agg);
static gboolean gst_audio_aggregator_stop (GstAggregator * agg);
static GstFlowReturn gst_audio_aggregator_flush (GstAggregator * agg);

static GstBuffer *gst_audio_aggregator_create_output_buffer (GstAudioAggregator
    * aagg, guint num_frames);
static GstFlowReturn gst_audio_aggregator_do_clip (GstAggregator * agg,
    GstAggregatorPad * bpad, GstBuffer * buffer, GstBuffer ** outbuf);
static GstFlowReturn gst_audio_aggregator_aggregate (GstAggregator * agg,
    gboolean timeout);
static gboolean sync_pad_values (GstAudioAggregator * aagg,
    GstAudioAggregatorPad * pad);

#define DEFAULT_OUTPUT_BUFFER_DURATION (10 * GST_MSECOND)
#define DEFAULT_ALIGNMENT_THRESHOLD   (40 * GST_MSECOND)
#define DEFAULT_DISCONT_WAIT (1 * GST_SECOND)

enum
{
  PROP_0,
  PROP_OUTPUT_BUFFER_DURATION,
  PROP_ALIGNMENT_THRESHOLD,
  PROP_DISCONT_WAIT,
};

G_DEFINE_ABSTRACT_TYPE (GstAudioAggregator, gst_audio_aggregator,
    GST_TYPE_AGGREGATOR);

static GstClockTime
gst_audio_aggregator_get_next_time (GstAggregator * agg)
{
  GstClockTime next_time;

  GST_OBJECT_LOCK (agg);
  if (agg->segment.position == -1 || agg->segment.position < agg->segment.start)
    next_time = agg->segment.start;
  else
    next_time = agg->segment.position;

  if (agg->segment.stop != -1 && next_time > agg->segment.stop)
    next_time = agg->segment.stop;

  next_time =
      gst_segment_to_running_time (&agg->segment, GST_FORMAT_TIME, next_time);
  GST_OBJECT_UNLOCK (agg);

  return next_time;
}

static void
gst_audio_aggregator_class_init (GstAudioAggregatorClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstAggregatorClass *gstaggregator_class = (GstAggregatorClass *) klass;

  g_type_class_add_private (klass, sizeof (GstAudioAggregatorPrivate));

  gobject_class->set_property = gst_audio_aggregator_set_property;
  gobject_class->get_property = gst_audio_aggregator_get_property;
  gobject_class->dispose = gst_audio_aggregator_dispose;

  gstaggregator_class->src_event =
      GST_DEBUG_FUNCPTR (gst_audio_aggregator_src_event);
  gstaggregator_class->sink_event =
      GST_DEBUG_FUNCPTR (gst_audio_aggregator_sink_event);
  gstaggregator_class->src_query =
      GST_DEBUG_FUNCPTR (gst_audio_aggregator_src_query);
  gstaggregator_class->start = gst_audio_aggregator_start;
  gstaggregator_class->stop = gst_audio_aggregator_stop;
  gstaggregator_class->flush = gst_audio_aggregator_flush;
  gstaggregator_class->aggregate =
      GST_DEBUG_FUNCPTR (gst_audio_aggregator_aggregate);
  gstaggregator_class->clip = GST_DEBUG_FUNCPTR (gst_audio_aggregator_do_clip);
  gstaggregator_class->get_next_time = gst_audio_aggregator_get_next_time;

  klass->create_output_buffer = gst_audio_aggregator_create_output_buffer;

  GST_DEBUG_REGISTER_FUNCPTR (sync_pad_values);

  GST_DEBUG_CATEGORY_INIT (audio_aggregator_debug, "audioaggregator",
      GST_DEBUG_FG_MAGENTA, "GstAudioAggregator");

  g_object_class_install_property (gobject_class, PROP_OUTPUT_BUFFER_DURATION,
      g_param_spec_uint64 ("output-buffer-duration", "Output Buffer Duration",
          "Output block size in nanoseconds", 1,
          G_MAXUINT64, DEFAULT_OUTPUT_BUFFER_DURATION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ALIGNMENT_THRESHOLD,
      g_param_spec_uint64 ("alignment-threshold", "Alignment Threshold",
          "Timestamp alignment threshold in nanoseconds", 0,
          G_MAXUINT64 - 1, DEFAULT_ALIGNMENT_THRESHOLD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DISCONT_WAIT,
      g_param_spec_uint64 ("discont-wait", "Discont Wait",
          "Window of time in nanoseconds to wait before "
          "creating a discontinuity", 0,
          G_MAXUINT64 - 1, DEFAULT_DISCONT_WAIT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_audio_aggregator_init (GstAudioAggregator * aagg)
{
  aagg->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (aagg, GST_TYPE_AUDIO_AGGREGATOR,
      GstAudioAggregatorPrivate);

  g_mutex_init (&aagg->priv->mutex);

  aagg->priv->output_buffer_duration = DEFAULT_OUTPUT_BUFFER_DURATION;
  aagg->priv->alignment_threshold = DEFAULT_ALIGNMENT_THRESHOLD;
  aagg->priv->discont_wait = DEFAULT_DISCONT_WAIT;

  aagg->current_caps = NULL;
  gst_audio_info_init (&aagg->info);

  gst_aggregator_set_latency (GST_AGGREGATOR (aagg),
      aagg->priv->output_buffer_duration, aagg->priv->output_buffer_duration);
}

static void
gst_audio_aggregator_dispose (GObject * object)
{
  GstAudioAggregator *aagg = GST_AUDIO_AGGREGATOR (object);

  gst_caps_replace (&aagg->current_caps, NULL);

  g_mutex_clear (&aagg->priv->mutex);

  G_OBJECT_CLASS (gst_audio_aggregator_parent_class)->dispose (object);
}

static void
gst_audio_aggregator_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAudioAggregator *aagg = GST_AUDIO_AGGREGATOR (object);

  switch (prop_id) {
    case PROP_OUTPUT_BUFFER_DURATION:
      aagg->priv->output_buffer_duration = g_value_get_uint64 (value);
      gst_aggregator_set_latency (GST_AGGREGATOR (aagg),
          aagg->priv->output_buffer_duration,
          aagg->priv->output_buffer_duration);
      break;
    case PROP_ALIGNMENT_THRESHOLD:
      aagg->priv->alignment_threshold = g_value_get_uint64 (value);
      break;
    case PROP_DISCONT_WAIT:
      aagg->priv->discont_wait = g_value_get_uint64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_audio_aggregator_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAudioAggregator *aagg = GST_AUDIO_AGGREGATOR (object);

  switch (prop_id) {
    case PROP_OUTPUT_BUFFER_DURATION:
      g_value_set_uint64 (value, aagg->priv->output_buffer_duration);
      break;
    case PROP_ALIGNMENT_THRESHOLD:
      g_value_set_uint64 (value, aagg->priv->alignment_threshold);
      break;
    case PROP_DISCONT_WAIT:
      g_value_set_uint64 (value, aagg->priv->discont_wait);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


/* event handling */

static gboolean
gst_audio_aggregator_src_event (GstAggregator * agg, GstEvent * event)
{
  gboolean result;

  GstAudioAggregator *aagg = GST_AUDIO_AGGREGATOR (agg);
  GST_DEBUG_OBJECT (agg->srcpad, "Got %s event on src pad",
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:
      /* QoS might be tricky */
      gst_event_unref (event);
      return FALSE;
    case GST_EVENT_NAVIGATION:
      /* navigation is rather pointless. */
      gst_event_unref (event);
      return FALSE;
      break;
    case GST_EVENT_SEEK:
    {
      GstSeekFlags flags;
      gdouble rate;
      GstSeekType start_type, stop_type;
      gint64 start, stop;
      GstFormat seek_format, dest_format;

      /* parse the seek parameters */
      gst_event_parse_seek (event, &rate, &seek_format, &flags, &start_type,
          &start, &stop_type, &stop);

      /* Check the seeking parametters before linking up */
      if ((start_type != GST_SEEK_TYPE_NONE)
          && (start_type != GST_SEEK_TYPE_SET)) {
        result = FALSE;
        GST_DEBUG_OBJECT (aagg,
            "seeking failed, unhandled seek type for start: %d", start_type);
        goto done;
      }
      if ((stop_type != GST_SEEK_TYPE_NONE) && (stop_type != GST_SEEK_TYPE_SET)) {
        result = FALSE;
        GST_DEBUG_OBJECT (aagg,
            "seeking failed, unhandled seek type for end: %d", stop_type);
        goto done;
      }

      GST_OBJECT_LOCK (agg);
      dest_format = agg->segment.format;
      GST_OBJECT_UNLOCK (agg);
      if (seek_format != dest_format) {
        result = FALSE;
        GST_DEBUG_OBJECT (aagg,
            "seeking failed, unhandled seek format: %s",
            gst_format_get_name (seek_format));
        goto done;
      }
    }
      break;
    default:
      break;
  }

  return
      GST_AGGREGATOR_CLASS (gst_audio_aggregator_parent_class)->src_event (agg,
      event);

done:
  return result;
}


static gboolean
gst_audio_aggregator_sink_event (GstAggregator * agg,
    GstAggregatorPad * aggpad, GstEvent * event)
{
  gboolean res = TRUE;

  GST_DEBUG_OBJECT (aggpad, "Got %s event on sink pad",
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
    {
      const GstSegment *segment;
      gst_event_parse_segment (event, &segment);

      if (segment->format != GST_FORMAT_TIME) {
        GST_ERROR_OBJECT (agg, "Segment of type %s are not supported,"
            " only TIME segments are supported",
            gst_format_get_name (segment->format));
        gst_event_unref (event);
        event = NULL;
        res = FALSE;
        break;
      }

      GST_OBJECT_LOCK (agg);
      if (segment->rate != agg->segment.rate) {
        GST_ERROR_OBJECT (aggpad,
            "Got segment event with wrong rate %lf, expected %lf",
            segment->rate, agg->segment.rate);
        res = FALSE;
        gst_event_unref (event);
        event = NULL;
      } else if (segment->rate < 0.0) {
        GST_ERROR_OBJECT (aggpad, "Negative rates not supported yet");
        res = FALSE;
        gst_event_unref (event);
        event = NULL;
      } else {
        GstAudioAggregatorPad *pad = GST_AUDIO_AGGREGATOR_PAD (aggpad);

        GST_OBJECT_LOCK (pad);
        pad->priv->new_segment = TRUE;
        GST_OBJECT_UNLOCK (pad);
      }
      GST_OBJECT_UNLOCK (agg);

      break;
    }
    default:
      break;
  }

  if (event != NULL)
    return
        GST_AGGREGATOR_CLASS (gst_audio_aggregator_parent_class)->sink_event
        (agg, aggpad, event);

  return res;
}

/* FIXME, the duration query should reflect how long you will produce
 * data, that is the amount of stream time until you will emit EOS.
 *
 * For synchronized mixing this is always the max of all the durations
 * of upstream since we emit EOS when all of them finished.
 *
 * We don't do synchronized mixing so this really depends on where the
 * streams where punched in and what their relative offsets are against
 * eachother which we can get from the first timestamps we see.
 *
 * When we add a new stream (or remove a stream) the duration might
 * also become invalid again and we need to post a new DURATION
 * message to notify this fact to the parent.
 * For now we take the max of all the upstream elements so the simple
 * cases work at least somewhat.
 */
static gboolean
gst_audio_aggregator_query_duration (GstAudioAggregator * aagg,
    GstQuery * query)
{
  gint64 max;
  gboolean res;
  GstFormat format;
  GstIterator *it;
  gboolean done;
  GValue item = { 0, };

  /* parse format */
  gst_query_parse_duration (query, &format, NULL);

  max = -1;
  res = TRUE;
  done = FALSE;

  it = gst_element_iterate_sink_pads (GST_ELEMENT_CAST (aagg));
  while (!done) {
    GstIteratorResult ires;

    ires = gst_iterator_next (it, &item);
    switch (ires) {
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
      case GST_ITERATOR_OK:
      {
        GstPad *pad = g_value_get_object (&item);
        gint64 duration;

        /* ask sink peer for duration */
        res &= gst_pad_peer_query_duration (pad, format, &duration);
        /* take max from all valid return values */
        if (res) {
          /* valid unknown length, stop searching */
          if (duration == -1) {
            max = duration;
            done = TRUE;
          }
          /* else see if bigger than current max */
          else if (duration > max)
            max = duration;
        }
        g_value_reset (&item);
        break;
      }
      case GST_ITERATOR_RESYNC:
        max = -1;
        res = TRUE;
        gst_iterator_resync (it);
        break;
      default:
        res = FALSE;
        done = TRUE;
        break;
    }
  }
  g_value_unset (&item);
  gst_iterator_free (it);

  if (res) {
    /* and store the max */
    GST_DEBUG_OBJECT (aagg, "Total duration in format %s: %"
        GST_TIME_FORMAT, gst_format_get_name (format), GST_TIME_ARGS (max));
    gst_query_set_duration (query, format, max);
  }

  return res;
}


static gboolean
gst_audio_aggregator_src_query (GstAggregator * agg, GstQuery * query)
{
  GstAudioAggregator *aagg = GST_AUDIO_AGGREGATOR (agg);
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
      res = gst_audio_aggregator_query_duration (aagg, query);
      break;
    case GST_QUERY_POSITION:
    {
      GstFormat format;

      gst_query_parse_position (query, &format, NULL);

      GST_OBJECT_LOCK (aagg);

      switch (format) {
        case GST_FORMAT_TIME:
          gst_query_set_position (query, format,
              gst_segment_to_stream_time (&agg->segment, GST_FORMAT_TIME,
                  agg->segment.position));
          res = TRUE;
          break;
        case GST_FORMAT_BYTES:
          if (GST_AUDIO_INFO_BPF (&aagg->info)) {
            gst_query_set_position (query, format, aagg->priv->offset *
                GST_AUDIO_INFO_BPF (&aagg->info));
            res = TRUE;
          }
          break;
        case GST_FORMAT_DEFAULT:
          gst_query_set_position (query, format, aagg->priv->offset);
          res = TRUE;
          break;
        default:
          break;
      }

      GST_OBJECT_UNLOCK (aagg);

      break;
    }
    default:
      res =
          GST_AGGREGATOR_CLASS (gst_audio_aggregator_parent_class)->src_query
          (agg, query);
      break;
  }

  return res;
}


void
gst_audio_aggregator_set_sink_caps (GstAudioAggregator * aagg,
    GstAudioAggregatorPad * pad, GstCaps * caps)
{
#ifndef G_DISABLE_ASSERT
  gboolean valid;

  GST_OBJECT_LOCK (pad);
  valid = gst_audio_info_from_caps (&pad->info, caps);
  g_assert (valid);
  GST_OBJECT_UNLOCK (pad);
#else
  GST_OBJECT_LOCK (pad);
  (void) gst_audio_info_from_caps (&pad->info, caps);
  GST_OBJECT_UNLOCK (pad);
#endif
}


gboolean
gst_audio_aggregator_set_src_caps (GstAudioAggregator * aagg, GstCaps * caps)
{
  GstAudioInfo info;

  if (!gst_audio_info_from_caps (&info, caps)) {
    GST_WARNING_OBJECT (aagg, "Rejecting invalid caps: %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  GST_AUDIO_AGGREGATOR_LOCK (aagg);
  GST_OBJECT_LOCK (aagg);

  if (!gst_audio_info_is_equal (&info, &aagg->info)) {
    GST_INFO_OBJECT (aagg, "setting caps to %" GST_PTR_FORMAT, caps);
    gst_caps_replace (&aagg->current_caps, caps);

    memcpy (&aagg->info, &info, sizeof (info));
    aagg->priv->send_caps = TRUE;

  }

  GST_OBJECT_UNLOCK (aagg);
  GST_AUDIO_AGGREGATOR_UNLOCK (aagg);

  /* send caps event later, after stream-start event */

  return TRUE;
}


/* Must hold object lock and aagg lock to call */

static void
gst_audio_aggregator_reset (GstAudioAggregator * aagg)
{
  GstAggregator *agg = GST_AGGREGATOR (aagg);

  GST_AUDIO_AGGREGATOR_LOCK (aagg);
  GST_OBJECT_LOCK (aagg);
  agg->segment.position = -1;
  aagg->priv->offset = -1;
  gst_audio_info_init (&aagg->info);
  gst_caps_replace (&aagg->current_caps, NULL);
  gst_buffer_replace (&aagg->priv->current_buffer, NULL);
  GST_OBJECT_UNLOCK (aagg);
  GST_AUDIO_AGGREGATOR_UNLOCK (aagg);
}

static gboolean
gst_audio_aggregator_start (GstAggregator * agg)
{
  GstAudioAggregator *aagg = GST_AUDIO_AGGREGATOR (agg);

  gst_audio_aggregator_reset (aagg);

  return TRUE;
}

static gboolean
gst_audio_aggregator_stop (GstAggregator * agg)
{
  GstAudioAggregator *aagg = GST_AUDIO_AGGREGATOR (agg);

  gst_audio_aggregator_reset (aagg);

  return TRUE;
}

static GstFlowReturn
gst_audio_aggregator_flush (GstAggregator * agg)
{
  GstAudioAggregator *aagg = GST_AUDIO_AGGREGATOR (agg);

  GST_AUDIO_AGGREGATOR_LOCK (aagg);
  GST_OBJECT_LOCK (aagg);
  agg->segment.position = -1;
  aagg->priv->offset = -1;
  gst_buffer_replace (&aagg->priv->current_buffer, NULL);
  GST_OBJECT_UNLOCK (aagg);
  GST_AUDIO_AGGREGATOR_UNLOCK (aagg);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_audio_aggregator_do_clip (GstAggregator * agg,
    GstAggregatorPad * bpad, GstBuffer * buffer, GstBuffer ** out)
{
  GstAudioAggregatorPad *pad = GST_AUDIO_AGGREGATOR_PAD (bpad);
  gint rate, bpf;


  rate = GST_AUDIO_INFO_RATE (&pad->info);
  bpf = GST_AUDIO_INFO_BPF (&pad->info);

  GST_OBJECT_LOCK (bpad);
  *out = gst_audio_buffer_clip (buffer, &bpad->clip_segment, rate, bpf);
  GST_OBJECT_UNLOCK (bpad);

  return GST_FLOW_OK;
}

/* Called with the object lock for both the element and pad held,
 * as well as the aagg lock
 */
static gboolean
gst_audio_aggregator_fill_buffer (GstAudioAggregator * aagg,
    GstAudioAggregatorPad * pad, GstBuffer * inbuf)
{
  GstClockTime start_time, end_time;
  gboolean discont = FALSE;
  guint64 start_offset, end_offset;
  gint rate, bpf;

  GstAggregator *agg = GST_AGGREGATOR (aagg);
  GstAggregatorPad *aggpad = GST_AGGREGATOR_PAD (pad);

  g_assert (pad->priv->buffer == NULL);

  rate = GST_AUDIO_INFO_RATE (&pad->info);
  bpf = GST_AUDIO_INFO_BPF (&pad->info);

  pad->priv->position = 0;
  pad->priv->size = gst_buffer_get_size (inbuf) / bpf;

  if (!GST_BUFFER_PTS_IS_VALID (inbuf)) {
    if (pad->priv->output_offset == -1)
      pad->priv->output_offset = aagg->priv->offset;
    if (pad->priv->next_offset == -1)
      pad->priv->next_offset = pad->priv->size;
    else
      pad->priv->next_offset += pad->priv->size;
    goto done;
  }

  start_time = GST_BUFFER_PTS (inbuf);
  end_time =
      start_time + gst_util_uint64_scale_ceil (pad->priv->size, GST_SECOND,
      rate);

  /* Clipping should've ensured this */
  g_assert (start_time >= aggpad->segment.start);

  start_offset =
      gst_util_uint64_scale (start_time - aggpad->segment.start, rate,
      GST_SECOND);
  end_offset = start_offset + pad->priv->size;

  if (GST_BUFFER_IS_DISCONT (inbuf)
      || GST_BUFFER_FLAG_IS_SET (inbuf, GST_BUFFER_FLAG_RESYNC)
      || pad->priv->new_segment || pad->priv->next_offset == -1) {
    discont = TRUE;
    pad->priv->new_segment = FALSE;
  } else {
    guint64 diff, max_sample_diff;

    /* Check discont, based on audiobasesink */
    if (start_offset <= pad->priv->next_offset)
      diff = pad->priv->next_offset - start_offset;
    else
      diff = start_offset - pad->priv->next_offset;

    max_sample_diff =
        gst_util_uint64_scale_int (aagg->priv->alignment_threshold, rate,
        GST_SECOND);

    /* Discont! */
    if (G_UNLIKELY (diff >= max_sample_diff)) {
      if (aagg->priv->discont_wait > 0) {
        if (pad->priv->discont_time == GST_CLOCK_TIME_NONE) {
          pad->priv->discont_time = start_time;
        } else if (start_time - pad->priv->discont_time >=
            aagg->priv->discont_wait) {
          discont = TRUE;
          pad->priv->discont_time = GST_CLOCK_TIME_NONE;
        }
      } else {
        discont = TRUE;
      }
    } else if (G_UNLIKELY (pad->priv->discont_time != GST_CLOCK_TIME_NONE)) {
      /* we have had a discont, but are now back on track! */
      pad->priv->discont_time = GST_CLOCK_TIME_NONE;
    }
  }

  if (discont) {
    /* Have discont, need resync */
    if (pad->priv->next_offset != -1)
      GST_DEBUG_OBJECT (pad, "Have discont. Expected %"
          G_GUINT64_FORMAT ", got %" G_GUINT64_FORMAT,
          pad->priv->next_offset, start_offset);
    pad->priv->output_offset = -1;
    pad->priv->next_offset = end_offset;
  } else {
    pad->priv->next_offset += pad->priv->size;
  }

  if (pad->priv->output_offset == -1) {
    GstClockTime start_running_time;
    GstClockTime end_running_time;
    guint64 start_output_offset;
    guint64 end_output_offset;

    start_running_time =
        gst_segment_to_running_time (&aggpad->segment,
        GST_FORMAT_TIME, start_time);
    end_running_time =
        gst_segment_to_running_time (&aggpad->segment,
        GST_FORMAT_TIME, end_time);

    /* Convert to position in the output segment */
    start_output_offset =
        gst_segment_position_from_running_time (&agg->segment, GST_FORMAT_TIME,
        start_running_time);
    if (start_output_offset != -1)
      start_output_offset =
          gst_util_uint64_scale (start_output_offset - agg->segment.start, rate,
          GST_SECOND);

    end_output_offset =
        gst_segment_position_from_running_time (&agg->segment, GST_FORMAT_TIME,
        end_running_time);
    if (end_output_offset != -1)
      end_output_offset =
          gst_util_uint64_scale (end_output_offset - agg->segment.start, rate,
          GST_SECOND);

    if (start_output_offset == -1 && end_output_offset == -1) {
      /* Outside output segment, drop */
      gst_buffer_unref (inbuf);
      pad->priv->buffer = NULL;
      pad->priv->position = 0;
      pad->priv->size = 0;
      pad->priv->output_offset = -1;
      GST_DEBUG_OBJECT (pad, "Buffer outside output segment");
      return FALSE;
    }

    /* Calculate end_output_offset if it was outside the output segment */
    if (end_output_offset == -1)
      end_output_offset = start_output_offset + pad->priv->size;

    if (end_output_offset < aagg->priv->offset) {
      /* Before output segment, drop */
      gst_buffer_unref (inbuf);
      pad->priv->buffer = NULL;
      pad->priv->position = 0;
      pad->priv->size = 0;
      pad->priv->output_offset = -1;
      GST_DEBUG_OBJECT (pad,
          "Buffer before segment or current position: %" G_GUINT64_FORMAT " < %"
          G_GINT64_FORMAT, end_output_offset, aagg->priv->offset);
      return FALSE;
    }

    if (start_output_offset == -1 || start_output_offset < aagg->priv->offset) {
      guint diff;

      if (start_output_offset == -1 && end_output_offset < pad->priv->size) {
        diff = pad->priv->size - end_output_offset + aagg->priv->offset;
      } else if (start_output_offset == -1) {
        start_output_offset = end_output_offset - pad->priv->size;

        if (start_output_offset < aagg->priv->offset)
          diff = aagg->priv->offset - start_output_offset;
        else
          diff = 0;
      } else {
        diff = aagg->priv->offset - start_output_offset;
      }

      pad->priv->position += diff;
      if (pad->priv->position >= pad->priv->size) {
        /* Empty buffer, drop */
        gst_buffer_unref (inbuf);
        pad->priv->buffer = NULL;
        pad->priv->position = 0;
        pad->priv->size = 0;
        pad->priv->output_offset = -1;
        GST_DEBUG_OBJECT (pad,
            "Buffer before segment or current position: %" G_GUINT64_FORMAT
            " < %" G_GINT64_FORMAT, end_output_offset, aagg->priv->offset);
        return FALSE;
      }
    }

    if (start_output_offset == -1 || start_output_offset < aagg->priv->offset)
      pad->priv->output_offset = aagg->priv->offset;
    else
      pad->priv->output_offset = start_output_offset;

    GST_DEBUG_OBJECT (pad,
        "Buffer resynced: Pad offset %" G_GUINT64_FORMAT
        ", current audio aggregator offset %" G_GINT64_FORMAT,
        pad->priv->output_offset, aagg->priv->offset);
  }

done:

  GST_LOG_OBJECT (pad,
      "Queued new buffer at offset %" G_GUINT64_FORMAT,
      pad->priv->output_offset);
  pad->priv->buffer = inbuf;

  return TRUE;
}

/* Called with pad object lock held */

static gboolean
gst_audio_aggregator_mix_buffer (GstAudioAggregator * aagg,
    GstAudioAggregatorPad * pad, GstBuffer * inbuf, GstBuffer * outbuf)
{
  guint overlap;
  guint out_start;
  gboolean filled;
  guint blocksize;

  blocksize = gst_util_uint64_scale (aagg->priv->output_buffer_duration,
      GST_AUDIO_INFO_RATE (&aagg->info), GST_SECOND);
  blocksize = MAX (1, blocksize);

  /* Overlap => mix */
  if (aagg->priv->offset < pad->priv->output_offset)
    out_start = pad->priv->output_offset - aagg->priv->offset;
  else
    out_start = 0;

  overlap = pad->priv->size - pad->priv->position;
  if (overlap > blocksize - out_start)
    overlap = blocksize - out_start;

  if (GST_BUFFER_FLAG_IS_SET (inbuf, GST_BUFFER_FLAG_GAP)) {
    /* skip gap buffer */
    GST_LOG_OBJECT (pad, "skipping GAP buffer");
    pad->priv->output_offset += pad->priv->size - pad->priv->position;
    pad->priv->position = pad->priv->size;

    gst_buffer_replace (&pad->priv->buffer, NULL);
    return FALSE;
  }

  filled = GST_AUDIO_AGGREGATOR_GET_CLASS (aagg)->aggregate_one_buffer (aagg,
      pad, inbuf, pad->priv->position, outbuf, out_start, overlap);

  if (filled)
    GST_BUFFER_FLAG_UNSET (outbuf, GST_BUFFER_FLAG_GAP);

  pad->priv->position += overlap;
  pad->priv->output_offset += overlap;

  if (pad->priv->position == pad->priv->size) {
    /* Buffer done, drop it */
    gst_buffer_replace (&pad->priv->buffer, NULL);
    GST_LOG_OBJECT (pad, "Finished mixing buffer, waiting for next");
    return FALSE;
  }

  return TRUE;
}

static GstBuffer *
gst_audio_aggregator_create_output_buffer (GstAudioAggregator * aagg,
    guint num_frames)
{
  GstBuffer *outbuf = gst_buffer_new_allocate (NULL, num_frames *
      GST_AUDIO_INFO_BPF (&aagg->info), NULL);
  GstMapInfo outmap;

  gst_buffer_map (outbuf, &outmap, GST_MAP_WRITE);
  gst_audio_format_fill_silence (aagg->info.finfo, outmap.data, outmap.size);
  gst_buffer_unmap (outbuf, &outmap);

  return outbuf;
}

static gboolean
sync_pad_values (GstAudioAggregator * aagg, GstAudioAggregatorPad * pad)
{
  GstAggregatorPad *bpad = GST_AGGREGATOR_PAD (pad);
  GstClockTime timestamp, stream_time;

  if (pad->priv->buffer == NULL)
    return TRUE;

  timestamp = GST_BUFFER_PTS (pad->priv->buffer);
  GST_OBJECT_LOCK (bpad);
  stream_time = gst_segment_to_stream_time (&bpad->segment, GST_FORMAT_TIME,
      timestamp);
  GST_OBJECT_UNLOCK (bpad);

  /* sync object properties on stream time */
  /* TODO: Ideally we would want to do that on every sample */
  if (GST_CLOCK_TIME_IS_VALID (stream_time))
    gst_object_sync_values (GST_OBJECT (pad), stream_time);

  return TRUE;
}

static GstFlowReturn
gst_audio_aggregator_aggregate (GstAggregator * agg, gboolean timeout)
{
  /* Get all pads that have data for us and store them in a
   * new list.
   *
   * Calculate the current output offset/timestamp and
   * offset_end/timestamp_end. Allocate a silence buffer
   * for this and store it.
   *
   * For all pads:
   * 1) Once per input buffer (cached)
   *   1) Check discont (flag and timestamp with tolerance)
   *   2) If discont or new, resync. That means:
   *     1) Drop all start data of the buffer that comes before
   *        the current position/offset.
   *     2) Calculate the offset (output segment!) that the first
   *        frame of the input buffer corresponds to. Base this on
   *        the running time.
   *
   * 2) If the current pad's offset/offset_end overlaps with the output
   *    offset/offset_end, mix it at the appropiate position in the output
   *    buffer and advance the pad's position. Remember if this pad needs
   *    a new buffer to advance behind the output offset_end.
   *
   * 3) If we had no pad with a buffer, go EOS.
   *
   * 4) If we had at least one pad that did not advance behind output
   *    offset_end, let collected be called again for the current
   *    output offset/offset_end.
   */
  GstElement *element;
  GstAudioAggregator *aagg;
  GList *iter;
  GstFlowReturn ret;
  GstBuffer *outbuf = NULL;
  gint64 next_offset;
  gint64 next_timestamp;
  gint rate, bpf;
  gboolean dropped = FALSE;
  gboolean is_eos = TRUE;
  gboolean is_done = TRUE;
  guint blocksize;

  element = GST_ELEMENT (agg);
  aagg = GST_AUDIO_AGGREGATOR (agg);

  /* Sync pad properties to the stream time */
  gst_aggregator_iterate_sinkpads (agg,
      (GstAggregatorPadForeachFunc) sync_pad_values, NULL);

  GST_AUDIO_AGGREGATOR_LOCK (aagg);
  GST_OBJECT_LOCK (agg);

  /* Update position from the segment start/stop if needed */
  if (agg->segment.position == -1) {
    if (agg->segment.rate > 0.0)
      agg->segment.position = agg->segment.start;
    else
      agg->segment.position = agg->segment.stop;
  }

  if (G_UNLIKELY (aagg->info.finfo->format == GST_AUDIO_FORMAT_UNKNOWN)) {
    if (timeout) {
      GST_DEBUG_OBJECT (aagg,
          "Got timeout before receiving any caps, don't output anything");

      /* Advance position */
      if (agg->segment.rate > 0.0)
        agg->segment.position += aagg->priv->output_buffer_duration;
      else if (agg->segment.position > aagg->priv->output_buffer_duration)
        agg->segment.position -= aagg->priv->output_buffer_duration;
      else
        agg->segment.position = 0;

      GST_OBJECT_UNLOCK (agg);
      GST_AUDIO_AGGREGATOR_UNLOCK (aagg);
      return GST_FLOW_OK;
    } else {
      GST_OBJECT_UNLOCK (agg);
      goto not_negotiated;
    }
  }

  if (aagg->priv->send_caps) {
    GST_OBJECT_UNLOCK (agg);
    gst_aggregator_set_src_caps (agg, aagg->current_caps);
    GST_OBJECT_LOCK (agg);

    aagg->priv->send_caps = FALSE;
  }

  rate = GST_AUDIO_INFO_RATE (&aagg->info);
  bpf = GST_AUDIO_INFO_BPF (&aagg->info);

  if (aagg->priv->offset == -1) {
    aagg->priv->offset =
        gst_util_uint64_scale (agg->segment.position - agg->segment.start, rate,
        GST_SECOND);
    GST_DEBUG_OBJECT (aagg, "Starting at offset %" G_GINT64_FORMAT,
        aagg->priv->offset);
  }

  blocksize = gst_util_uint64_scale (aagg->priv->output_buffer_duration,
      rate, GST_SECOND);
  blocksize = MAX (1, blocksize);

  /* for the next timestamp, use the sample counter, which will
   * never accumulate rounding errors */

  /* FIXME: Reverse mixing does not work at all yet */
  if (agg->segment.rate > 0.0) {
    next_offset = aagg->priv->offset + blocksize;
  } else {
    next_offset = aagg->priv->offset - blocksize;
  }

  next_timestamp =
      agg->segment.start + gst_util_uint64_scale (next_offset, GST_SECOND,
      rate);

  if (aagg->priv->current_buffer == NULL) {
    GST_OBJECT_UNLOCK (agg);
    aagg->priv->current_buffer =
        GST_AUDIO_AGGREGATOR_GET_CLASS (aagg)->create_output_buffer (aagg,
        blocksize);
    /* Be careful, some things could have changed ? */
    GST_OBJECT_LOCK (agg);
    GST_BUFFER_FLAG_SET (aagg->priv->current_buffer, GST_BUFFER_FLAG_GAP);
  }
  outbuf = aagg->priv->current_buffer;

  GST_LOG_OBJECT (agg,
      "Starting to mix %u samples for offset %" G_GINT64_FORMAT
      " with timestamp %" GST_TIME_FORMAT, blocksize,
      aagg->priv->offset, GST_TIME_ARGS (agg->segment.position));

  for (iter = element->sinkpads; iter; iter = iter->next) {
    GstBuffer *inbuf;
    GstAudioAggregatorPad *pad = (GstAudioAggregatorPad *) iter->data;
    GstAggregatorPad *aggpad = (GstAggregatorPad *) iter->data;
    gboolean drop_buf = FALSE;
    gboolean pad_eos = gst_aggregator_pad_is_eos (aggpad);

    if (!pad_eos)
      is_eos = FALSE;

    inbuf = gst_aggregator_pad_get_buffer (aggpad);

    GST_OBJECT_LOCK (pad);
    if (!inbuf) {
      if (timeout) {
        if (pad->priv->output_offset < next_offset) {
          gint64 diff = next_offset - pad->priv->output_offset;
          GST_DEBUG_OBJECT (pad, "Timeout, missing %" G_GINT64_FORMAT
              " frames (%" GST_TIME_FORMAT ")", diff,
              GST_TIME_ARGS (gst_util_uint64_scale (diff, GST_SECOND,
                      GST_AUDIO_INFO_RATE (&aagg->info))));
        }
      } else if (!pad_eos) {
        is_done = FALSE;
      }
      GST_OBJECT_UNLOCK (pad);
      continue;
    }

    g_assert (!pad->priv->buffer || pad->priv->buffer == inbuf);

    /* New buffer? */
    if (!pad->priv->buffer) {
      /* Takes ownership of buffer */
      if (!gst_audio_aggregator_fill_buffer (aagg, pad, inbuf)) {
        dropped = TRUE;
        GST_OBJECT_UNLOCK (pad);
        gst_aggregator_pad_drop_buffer (aggpad);
        continue;
      }
    } else {
      gst_buffer_unref (inbuf);
    }

    if (!pad->priv->buffer && !dropped && pad_eos) {
      GST_DEBUG_OBJECT (aggpad, "Pad is in EOS state");
      GST_OBJECT_UNLOCK (pad);
      continue;
    }

    g_assert (pad->priv->buffer);

    /* This pad is lacking behind, we need to update the offset
     * and maybe drop the current buffer */
    if (pad->priv->output_offset < aagg->priv->offset) {
      gint64 diff = aagg->priv->offset - pad->priv->output_offset;
      gint64 odiff = diff;

      if (pad->priv->position + diff > pad->priv->size)
        diff = pad->priv->size - pad->priv->position;
      pad->priv->position += diff;
      pad->priv->output_offset += diff;

      if (pad->priv->position == pad->priv->size) {
        GST_DEBUG_OBJECT (pad, "Buffer was late by %" GST_TIME_FORMAT
            ", dropping %" GST_PTR_FORMAT,
            GST_TIME_ARGS (gst_util_uint64_scale (odiff, GST_SECOND,
                    GST_AUDIO_INFO_RATE (&aagg->info))), pad->priv->buffer);
        /* Buffer done, drop it */
        gst_buffer_replace (&pad->priv->buffer, NULL);
        dropped = TRUE;
        GST_OBJECT_UNLOCK (pad);
        gst_aggregator_pad_drop_buffer (aggpad);
        continue;
      }
    }


    if (pad->priv->output_offset >= aagg->priv->offset
        && pad->priv->output_offset <
        aagg->priv->offset + blocksize && pad->priv->buffer) {
      GST_LOG_OBJECT (aggpad, "Mixing buffer for current offset");
      drop_buf = !gst_audio_aggregator_mix_buffer (aagg, pad, pad->priv->buffer,
          outbuf);
      if (pad->priv->output_offset >= next_offset) {
        GST_LOG_OBJECT (pad,
            "Pad is at or after current offset: %" G_GUINT64_FORMAT " >= %"
            G_GINT64_FORMAT, pad->priv->output_offset, next_offset);
      } else {
        is_done = FALSE;
      }
    }

    GST_OBJECT_UNLOCK (pad);
    if (drop_buf)
      gst_aggregator_pad_drop_buffer (aggpad);

  }
  GST_OBJECT_UNLOCK (agg);

  if (dropped) {
    /* We dropped a buffer, retry */
    GST_LOG_OBJECT (aagg, "A pad dropped a buffer, wait for the next one");
    GST_AUDIO_AGGREGATOR_UNLOCK (aagg);
    return GST_FLOW_OK;
  }

  if (!is_done && !is_eos) {
    /* Get more buffers */
    GST_LOG_OBJECT (aagg,
        "We're not done yet for the current offset, waiting for more data");
    GST_AUDIO_AGGREGATOR_UNLOCK (aagg);
    return GST_FLOW_OK;
  }

  if (is_eos) {
    gint64 max_offset = 0;

    GST_DEBUG_OBJECT (aagg, "We're EOS");

    GST_OBJECT_LOCK (agg);
    for (iter = GST_ELEMENT (agg)->sinkpads; iter; iter = iter->next) {
      GstAudioAggregatorPad *pad = GST_AUDIO_AGGREGATOR_PAD (iter->data);

      max_offset = MAX ((gint64) max_offset, (gint64) pad->priv->output_offset);
    }
    GST_OBJECT_UNLOCK (agg);

    /* This means EOS or nothing mixed in at all */
    if (aagg->priv->offset == max_offset) {
      gst_buffer_replace (&aagg->priv->current_buffer, NULL);
      GST_AUDIO_AGGREGATOR_UNLOCK (aagg);
      return GST_FLOW_EOS;
    }

    if (max_offset <= next_offset) {
      GST_DEBUG_OBJECT (aagg,
          "Last buffer is incomplete: %" G_GUINT64_FORMAT " <= %"
          G_GINT64_FORMAT, max_offset, next_offset);
      next_offset = max_offset;
      next_timestamp =
          agg->segment.start + gst_util_uint64_scale (next_offset, GST_SECOND,
          rate);

      if (next_offset > aagg->priv->offset)
        gst_buffer_resize (outbuf, 0, (next_offset - aagg->priv->offset) * bpf);
    }
  }

  /* set timestamps on the output buffer */
  GST_OBJECT_LOCK (agg);
  if (agg->segment.rate > 0.0) {
    GST_BUFFER_PTS (outbuf) = agg->segment.position;
    GST_BUFFER_OFFSET (outbuf) = aagg->priv->offset;
    GST_BUFFER_OFFSET_END (outbuf) = next_offset;
    GST_BUFFER_DURATION (outbuf) = next_timestamp - agg->segment.position;
  } else {
    GST_BUFFER_PTS (outbuf) = next_timestamp;
    GST_BUFFER_OFFSET (outbuf) = next_offset;
    GST_BUFFER_OFFSET_END (outbuf) = aagg->priv->offset;
    GST_BUFFER_DURATION (outbuf) = agg->segment.position - next_timestamp;
  }

  GST_OBJECT_UNLOCK (agg);

  /* send it out */
  GST_LOG_OBJECT (aagg,
      "pushing outbuf %p, timestamp %" GST_TIME_FORMAT " offset %"
      G_GINT64_FORMAT, outbuf, GST_TIME_ARGS (GST_BUFFER_PTS (outbuf)),
      GST_BUFFER_OFFSET (outbuf));

  GST_AUDIO_AGGREGATOR_UNLOCK (aagg);

  ret = gst_aggregator_finish_buffer (agg, aagg->priv->current_buffer);
  aagg->priv->current_buffer = NULL;

  GST_LOG_OBJECT (aagg, "pushed outbuf, result = %s", gst_flow_get_name (ret));

  GST_AUDIO_AGGREGATOR_LOCK (aagg);
  GST_OBJECT_LOCK (agg);
  aagg->priv->offset = next_offset;
  agg->segment.position = next_timestamp;

  /* If there was a timeout and there was a gap in data in out of the streams,
   * then it's a very good time to for a resync with the timestamps.
   */
  if (timeout) {
    for (iter = element->sinkpads; iter; iter = iter->next) {
      GstAudioAggregatorPad *pad = GST_AUDIO_AGGREGATOR_PAD (iter->data);

      GST_OBJECT_LOCK (pad);
      if (pad->priv->output_offset < aagg->priv->offset)
        pad->priv->output_offset = -1;
      GST_OBJECT_UNLOCK (pad);
    }
  }
  GST_OBJECT_UNLOCK (agg);
  GST_AUDIO_AGGREGATOR_UNLOCK (aagg);

  return ret;
  /* ERRORS */
not_negotiated:
  {
    GST_AUDIO_AGGREGATOR_UNLOCK (aagg);
    GST_ELEMENT_ERROR (aagg, STREAM, FORMAT, (NULL),
        ("Unknown data received, not negotiated"));
    return GST_FLOW_NOT_NEGOTIATED;
  }
}
