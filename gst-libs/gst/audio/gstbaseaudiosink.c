/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *
 * gstbaseaudiosink.c: 
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

#include <string.h>

#include "gstbaseaudiosink.h"

GST_DEBUG_CATEGORY_STATIC (gst_base_audio_sink_debug);
#define GST_CAT_DEFAULT gst_base_audio_sink_debug

/* BaseAudioSink signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

/* we tollerate a 10th of a second diff before we start resyncing. This
 * should be enough to compensate for various rounding errors in the timestamp
 * and sample offset position. */
#define DIFF_TOLERANCE  10

#define DEFAULT_BUFFER_TIME     200 * GST_USECOND
#define DEFAULT_LATENCY_TIME    10 * GST_USECOND
#define DEFAULT_PROVIDE_CLOCK   TRUE

enum
{
  PROP_0,
  PROP_BUFFER_TIME,
  PROP_LATENCY_TIME,
  PROP_PROVIDE_CLOCK,
};

#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_base_audio_sink_debug, "baseaudiosink", 0, "baseaudiosink element");

GST_BOILERPLATE_FULL (GstBaseAudioSink, gst_base_audio_sink, GstBaseSink,
    GST_TYPE_BASE_SINK, _do_init);

static void gst_base_audio_sink_dispose (GObject * object);

static void gst_base_audio_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_base_audio_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_base_audio_sink_change_state (GstElement *
    element, GstStateChange transition);

static GstClock *gst_base_audio_sink_provide_clock (GstElement * elem);
static GstClockTime gst_base_audio_sink_get_time (GstClock * clock,
    GstBaseAudioSink * sink);
static void gst_base_audio_sink_callback (GstRingBuffer * rbuf, guint8 * data,
    guint len, gpointer user_data);

static GstFlowReturn gst_base_audio_sink_preroll (GstBaseSink * bsink,
    GstBuffer * buffer);
static GstFlowReturn gst_base_audio_sink_render (GstBaseSink * bsink,
    GstBuffer * buffer);
static gboolean gst_base_audio_sink_event (GstBaseSink * bsink,
    GstEvent * event);
static void gst_base_audio_sink_get_times (GstBaseSink * bsink,
    GstBuffer * buffer, GstClockTime * start, GstClockTime * end);
static gboolean gst_base_audio_sink_setcaps (GstBaseSink * bsink,
    GstCaps * caps);

//static guint gst_base_audio_sink_signals[LAST_SIGNAL] = { 0 };

static void
gst_base_audio_sink_base_init (gpointer g_class)
{
}

static void
gst_base_audio_sink_class_init (GstBaseAudioSinkClass * klass)
{
  gchar *longdesc;

  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_base_audio_sink_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_base_audio_sink_get_property);
  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_base_audio_sink_dispose);

  longdesc =
      g_strdup_printf
      ("Size of audio buffer in microseconds (use -1 for default of %"
      G_GUINT64_FORMAT " us)", DEFAULT_BUFFER_TIME / GST_USECOND);
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BUFFER_TIME,
      g_param_spec_int64 ("buffer-time", "Buffer Time", longdesc, -1,
          G_MAXINT64, DEFAULT_BUFFER_TIME, G_PARAM_READWRITE));
  g_free (longdesc);
  longdesc =
      g_strdup_printf ("Audio latency in microseconds (use -1 for default of %"
      G_GUINT64_FORMAT " us)", DEFAULT_LATENCY_TIME / GST_USECOND);
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_LATENCY_TIME,
      g_param_spec_int64 ("latency-time", "Latency Time", longdesc, -1,
          G_MAXINT64, DEFAULT_LATENCY_TIME, G_PARAM_READWRITE));
  g_free (longdesc);
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PROVIDE_CLOCK,
      g_param_spec_boolean ("provide-clock", "Provide Clock",
          "Provide a clock to be used as the global pipeline clock",
          DEFAULT_PROVIDE_CLOCK, G_PARAM_READWRITE));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_base_audio_sink_change_state);
  gstelement_class->provide_clock =
      GST_DEBUG_FUNCPTR (gst_base_audio_sink_provide_clock);

  gstbasesink_class->event = GST_DEBUG_FUNCPTR (gst_base_audio_sink_event);
  gstbasesink_class->preroll = GST_DEBUG_FUNCPTR (gst_base_audio_sink_preroll);
  gstbasesink_class->render = GST_DEBUG_FUNCPTR (gst_base_audio_sink_render);
  gstbasesink_class->get_times =
      GST_DEBUG_FUNCPTR (gst_base_audio_sink_get_times);
  gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_base_audio_sink_setcaps);
}

static void
gst_base_audio_sink_init (GstBaseAudioSink * baseaudiosink,
    GstBaseAudioSinkClass * g_class)
{
  baseaudiosink->buffer_time = DEFAULT_BUFFER_TIME;
  baseaudiosink->latency_time = DEFAULT_LATENCY_TIME;
  baseaudiosink->provide_clock = DEFAULT_PROVIDE_CLOCK;

  baseaudiosink->provided_clock = gst_audio_clock_new ("clock",
      (GstAudioClockGetTimeFunc) gst_base_audio_sink_get_time, baseaudiosink);
}

static void
gst_base_audio_sink_dispose (GObject * object)
{
  GstBaseAudioSink *sink;

  sink = GST_BASE_AUDIO_SINK (object);

  if (sink->provided_clock)
    gst_object_unref (sink->provided_clock);
  sink->provided_clock = NULL;

  if (sink->ringbuffer)
    gst_object_unref (sink->ringbuffer);
  sink->ringbuffer = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static GstClock *
gst_base_audio_sink_provide_clock (GstElement * elem)
{
  GstBaseAudioSink *sink;
  GstClock *clock;

  sink = GST_BASE_AUDIO_SINK (elem);

  /* we have no ringbuffer (must be NULL state */
  if (sink->ringbuffer == NULL)
    goto wrong_state;

  if (!gst_ring_buffer_is_acquired (sink->ringbuffer))
    goto wrong_state;

  GST_OBJECT_LOCK (sink);
  if (!sink->provide_clock)
    goto clock_disabled;

  clock = GST_CLOCK_CAST (gst_object_ref (sink->provided_clock));
  GST_OBJECT_UNLOCK (sink);

  return clock;

wrong_state:
  {
    GST_DEBUG_OBJECT (sink, "ringbuffer not acquired");
    return NULL;
  }
clock_disabled:
  {
    GST_DEBUG_OBJECT (sink, "clock provide disabled");
    GST_OBJECT_UNLOCK (sink);
    return NULL;
  }
}

static GstClockTime
gst_base_audio_sink_get_time (GstClock * clock, GstBaseAudioSink * sink)
{
  guint64 samples;
  GstClockTime result;

  if (sink->ringbuffer == NULL || sink->ringbuffer->spec.rate == 0)
    return GST_CLOCK_TIME_NONE;

  /* our processed samples are always increasing */
  samples = gst_ring_buffer_samples_done (sink->ringbuffer);

  result = gst_util_uint64_scale_int (samples, GST_SECOND,
      sink->ringbuffer->spec.rate);

  return result;
}

static void
gst_base_audio_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBaseAudioSink *sink;

  sink = GST_BASE_AUDIO_SINK (object);

  switch (prop_id) {
    case PROP_BUFFER_TIME:
      sink->buffer_time = g_value_get_int64 (value);
      break;
    case PROP_LATENCY_TIME:
      sink->latency_time = g_value_get_int64 (value);
      break;
    case PROP_PROVIDE_CLOCK:
      GST_OBJECT_LOCK (sink);
      sink->provide_clock = g_value_get_boolean (value);
      GST_OBJECT_UNLOCK (sink);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_base_audio_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstBaseAudioSink *sink;

  sink = GST_BASE_AUDIO_SINK (object);

  switch (prop_id) {
    case PROP_BUFFER_TIME:
      g_value_set_int64 (value, sink->buffer_time);
      break;
    case PROP_LATENCY_TIME:
      g_value_set_int64 (value, sink->latency_time);
      break;
    case PROP_PROVIDE_CLOCK:
      GST_OBJECT_LOCK (sink);
      g_value_set_boolean (value, sink->provide_clock);
      GST_OBJECT_UNLOCK (sink);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_base_audio_sink_setcaps (GstBaseSink * bsink, GstCaps * caps)
{
  GstBaseAudioSink *sink = GST_BASE_AUDIO_SINK (bsink);
  GstRingBufferSpec *spec;

  spec = &sink->ringbuffer->spec;

  GST_DEBUG_OBJECT (sink, "release old ringbuffer");

  /* release old ringbuffer */
  gst_ring_buffer_release (sink->ringbuffer);

  GST_DEBUG_OBJECT (sink, "parse caps");

  spec->buffer_time = sink->buffer_time;
  spec->latency_time = sink->latency_time;

  /* parse new caps */
  if (!gst_ring_buffer_parse_caps (spec, caps))
    goto parse_error;

  gst_ring_buffer_debug_spec_buff (spec);

  GST_DEBUG_OBJECT (sink, "acquire new ringbuffer");

  if (!gst_ring_buffer_acquire (sink->ringbuffer, spec))
    goto acquire_error;

  /* calculate actual latency and buffer times */
  spec->latency_time =
      spec->segsize * GST_MSECOND / (spec->rate * spec->bytes_per_sample);
  spec->buffer_time =
      spec->segtotal * spec->segsize * GST_MSECOND / (spec->rate *
      spec->bytes_per_sample);

  gst_ring_buffer_debug_spec_buff (spec);

  return TRUE;

  /* ERRORS */
parse_error:
  {
    GST_DEBUG_OBJECT (sink, "could not parse caps");
    GST_ELEMENT_ERROR (sink, STREAM, FORMAT,
        (NULL), ("cannot parse audio format."));
    return FALSE;
  }
acquire_error:
  {
    GST_DEBUG_OBJECT (sink, "could not acquire ringbuffer");
    return FALSE;
  }
}

static void
gst_base_audio_sink_get_times (GstBaseSink * bsink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  /* our clock sync is a bit too much for the base class to handle so
   * we implement it ourselves. */
  *start = GST_CLOCK_TIME_NONE;
  *end = GST_CLOCK_TIME_NONE;
}

/* FIXME, this waits for the drain to happen but it cannot be
 * canceled.
 */
static gboolean
gst_base_audio_sink_drain (GstBaseAudioSink * sink)
{
  if (!sink->ringbuffer)
    return TRUE;
  if (!sink->ringbuffer->spec.rate)
    return TRUE;

  if (sink->next_sample != -1) {
    GstClockTime time;
    GstClock *clock;

    time =
        gst_util_uint64_scale_int (sink->next_sample, GST_SECOND,
        sink->ringbuffer->spec.rate);

    GST_OBJECT_LOCK (sink);
    if ((clock = GST_ELEMENT_CLOCK (sink)) != NULL) {
      GstClockID id = gst_clock_new_single_shot_id (clock, time);

      GST_OBJECT_UNLOCK (sink);

      GST_DEBUG_OBJECT (sink, "waiting for last sample to play");
      gst_clock_id_wait (id, NULL);

      gst_clock_id_unref (id);
      sink->next_sample = -1;
    } else {
      GST_OBJECT_UNLOCK (sink);
    }
  }
  return TRUE;
}

static gboolean
gst_base_audio_sink_event (GstBaseSink * bsink, GstEvent * event)
{
  GstBaseAudioSink *sink = GST_BASE_AUDIO_SINK (bsink);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      gst_ring_buffer_set_flushing (sink->ringbuffer, TRUE);
      break;
    case GST_EVENT_FLUSH_STOP:
      /* always resync on sample after a flush */
      sink->next_sample = -1;
      gst_ring_buffer_set_flushing (sink->ringbuffer, FALSE);
      break;
    case GST_EVENT_EOS:
      /* need to start playback when we reach EOS */
      gst_ring_buffer_start (sink->ringbuffer);
      /* now wait till we played everything */
      gst_base_audio_sink_drain (sink);
      break;
    default:
      break;
  }
  return TRUE;
}

static GstFlowReturn
gst_base_audio_sink_preroll (GstBaseSink * bsink, GstBuffer * buffer)
{
  GstBaseAudioSink *sink = GST_BASE_AUDIO_SINK (bsink);

  if (!gst_ring_buffer_is_acquired (sink->ringbuffer))
    goto wrong_state;

  /* we don't really do anything when prerolling. We could make a
   * property to play this buffer to have some sort of scrubbing
   * support. */
  return GST_FLOW_OK;

wrong_state:
  {
    GST_DEBUG_OBJECT (sink, "ringbuffer in wrong state");
    GST_ELEMENT_ERROR (sink, STREAM, FORMAT, (NULL), ("sink not negotiated."));
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

static guint64
gst_base_audio_sink_get_offset (GstBaseAudioSink * sink)
{
  guint64 sample;
  gint writeseg, segdone, sps;
  gint diff;

  /* assume we can append to the previous sample */
  sample = sink->next_sample;
  /* no previous sample, try to insert at position 0 */
  if (sample == -1)
    sample = 0;

  sps = sink->ringbuffer->samples_per_seg;

  /* figure out the segment and the offset inside the segment where
   * the sample should be written. */
  writeseg = sample / sps;

  /* get the currently processed segment */
  segdone = g_atomic_int_get (&sink->ringbuffer->segdone)
      - sink->ringbuffer->segbase;

  /* see how far away it is from the write segment */
  diff = writeseg - segdone;
  if (diff < 0) {
    /* sample would be dropped, position to next playable position */
    sample = (segdone + 1) * sps;
  }

  return sample;
}

static GstFlowReturn
gst_base_audio_sink_render (GstBaseSink * bsink, GstBuffer * buf)
{
  guint64 render_offset, in_offset;
  GstClockTime time, stop, render_time, duration;
  GstBaseAudioSink *sink;
  GstRingBuffer *ringbuf;
  gint64 diff, ctime, cstop;
  guint8 *data;
  guint size;
  guint samples, written;
  gint bps;
  gdouble crate = 1.0;
  GstClockTime crate_num;
  GstClockTime crate_denom;
  GstClockTime cinternal, cexternal;

  sink = GST_BASE_AUDIO_SINK (bsink);

  if (G_UNLIKELY (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT))) {
    /* always resync after a discont */
    sink->next_sample = -1;
  }

  ringbuf = sink->ringbuffer;

  /* can't do anything when we don't have the device */
  if (G_UNLIKELY (!gst_ring_buffer_is_acquired (ringbuf)))
    goto wrong_state;

  bps = ringbuf->spec.bytes_per_sample;

  size = GST_BUFFER_SIZE (buf);
  if (G_UNLIKELY (size % bps) != 0)
    goto wrong_size;

  samples = size / bps;

  in_offset = GST_BUFFER_OFFSET (buf);
  time = GST_BUFFER_TIMESTAMP (buf);
  duration = GST_BUFFER_DURATION (buf);
  data = GST_BUFFER_DATA (buf);

  GST_DEBUG_OBJECT (sink,
      "time %" GST_TIME_FORMAT ", offset %llu, start %" GST_TIME_FORMAT,
      GST_TIME_ARGS (time), in_offset, GST_TIME_ARGS (bsink->segment.start));

  /* if not valid timestamp or we don't need to sync, try to play
   * sample ASAP */
  if (!GST_CLOCK_TIME_IS_VALID (time) || !bsink->sync) {
    render_offset = gst_base_audio_sink_get_offset (sink);
    stop = -1;
    GST_DEBUG_OBJECT (sink,
        "Buffer of size %u has no time. Using render_offset=%" G_GUINT64_FORMAT,
        GST_BUFFER_SIZE (buf), render_offset);
    goto no_sync;
  }

  /* samples should be rendered based on their timestamp. All samples
   * arriving before the segment.start or after segment.stop are to be 
   * thrown away. All samples should also be clipped to the segment 
   * boundaries */
  /* let's calc stop based on the number of samples in the buffer instead
   * of trusting the DURATION */
  stop =
      time + gst_util_uint64_scale_int (samples, GST_SECOND,
      ringbuf->spec.rate);
  if (!gst_segment_clip (&bsink->segment, GST_FORMAT_TIME, time, stop, &ctime,
          &cstop))
    goto out_of_segment;

  /* see if some clipping happened */
  diff = ctime - time;
  if (diff > 0) {
    /* bring clipped time to samples */
    diff = gst_util_uint64_scale_int (diff, ringbuf->spec.rate, GST_SECOND);
    GST_DEBUG_OBJECT (sink, "clipping start to %" GST_TIME_FORMAT " %"
        G_GUINT64_FORMAT " samples", GST_TIME_ARGS (ctime), diff);
    samples -= diff;
    data += diff * bps;
    time = ctime;
  }
  diff = stop - cstop;
  if (diff > 0) {
    /* bring clipped time to samples */
    diff = gst_util_uint64_scale_int (diff, ringbuf->spec.rate, GST_SECOND);
    GST_DEBUG_OBJECT (sink, "clipping stop to %" GST_TIME_FORMAT " %"
        G_GUINT64_FORMAT " samples", GST_TIME_ARGS (cstop), diff);
    samples -= diff;
    stop = cstop;
  }

  gst_clock_get_calibration (sink->provided_clock, &cinternal, &cexternal,
      &crate_num, &crate_denom);

  /* bring buffer timestamp to running time */
  render_time =
      gst_segment_to_running_time (&bsink->segment, GST_FORMAT_TIME, time);
  /* add base time to get absolute clock time */
  render_time +=
      (gst_element_get_base_time (GST_ELEMENT_CAST (bsink)) - cexternal) +
      cinternal;
  /* and bring the time to the offset in the buffer */
  render_offset =
      gst_util_uint64_scale_int (render_time, ringbuf->spec.rate, GST_SECOND);

  GST_DEBUG_OBJECT (sink, "render time %" GST_TIME_FORMAT
      ", render offset %llu, samples %lu",
      GST_TIME_ARGS (render_time), render_offset, samples);

  /* roundoff errors in timestamp conversion */
  if (G_LIKELY (sink->next_sample != -1)) {
    diff = ABS ((gint64) render_offset - (gint64) sink->next_sample);

    /* we tollerate a 10th of a second diff before we start resyncing. This
     * should be enough to compensate for various rounding errors in the timestamp
     * and sample offset position. We always resync if we got a discont anyway. */
    if (diff < ringbuf->spec.rate / DIFF_TOLERANCE) {
      GST_DEBUG_OBJECT (sink,
          "align with prev sample, %" G_GINT64_FORMAT " < %lu", diff,
          ringbuf->spec.rate / DIFF_TOLERANCE);
      /* just align with previous sample then */
      render_offset = sink->next_sample;
    } else {
      GST_DEBUG_OBJECT (sink,
          "resync after discont with previous sample of diff: %lu", diff);
    }
  } else {
    GST_DEBUG_OBJECT (sink, "resync after discont");
  }

  crate =
      gst_guint64_to_gdouble (crate_num) / gst_guint64_to_gdouble (crate_denom);
  GST_DEBUG_OBJECT (sink,
      "internal %" G_GUINT64_FORMAT ", %" G_GUINT64_FORMAT ", rate %g",
      cinternal, cexternal, crate);

no_sync:
  /* clip length based on rate */
  samples = MIN (samples, samples / (crate * bsink->segment.abs_rate));

  /* the next sample should be current sample and its length */
  sink->next_sample = render_offset + samples;

  do {
    written = gst_ring_buffer_commit (ringbuf, render_offset, data, samples);
    GST_DEBUG_OBJECT (sink, "wrote %u of %u", written, samples);
    /* if we wrote all, we're done */
    if (written == samples)
      break;

    /* else something interrupted us */
    GST_DEBUG_OBJECT (sink, "wait for preroll...");
    bsink->have_preroll = TRUE;
    GST_PAD_PREROLL_WAIT (bsink->sinkpad);
    bsink->have_preroll = FALSE;
    GST_DEBUG_OBJECT (sink, "preroll done");
    if (G_UNLIKELY (bsink->flushing))
      goto stopping;
    GST_DEBUG_OBJECT (sink, "continue after preroll");

    render_offset += written;
    samples -= written;
    data += written * bps;
  } while (TRUE);

  if (GST_CLOCK_TIME_IS_VALID (stop) && stop >= bsink->segment.stop) {
    GST_DEBUG_OBJECT (sink,
        "start playback because we are at the end of segment");
    gst_ring_buffer_start (ringbuf);
  }

  return GST_FLOW_OK;

  /* SPECIAL cases */
out_of_segment:
  {
    GST_DEBUG_OBJECT (sink,
        "dropping sample out of segment time %" GST_TIME_FORMAT ", start %"
        GST_TIME_FORMAT, GST_TIME_ARGS (time),
        GST_TIME_ARGS (bsink->segment.start));
    return GST_FLOW_OK;
  }
  /* ERRORS */
wrong_state:
  {
    GST_DEBUG_OBJECT (sink, "ringbuffer not negotiated");
    GST_ELEMENT_ERROR (sink, STREAM, FORMAT, (NULL), ("sink not negotiated."));
    return GST_FLOW_NOT_NEGOTIATED;
  }
wrong_size:
  {
    GST_DEBUG_OBJECT (sink, "wrong size");
    GST_ELEMENT_ERROR (sink, STREAM, WRONG_TYPE,
        (NULL), ("sink received buffer of wrong size."));
    return GST_FLOW_ERROR;
  }
stopping:
  {
    GST_DEBUG_OBJECT (sink, "ringbuffer is stopping");
    return GST_FLOW_WRONG_STATE;
  }
}

GstRingBuffer *
gst_base_audio_sink_create_ringbuffer (GstBaseAudioSink * sink)
{
  GstBaseAudioSinkClass *bclass;
  GstRingBuffer *buffer = NULL;

  bclass = GST_BASE_AUDIO_SINK_GET_CLASS (sink);
  if (bclass->create_ringbuffer)
    buffer = bclass->create_ringbuffer (sink);

  if (buffer)
    gst_object_set_parent (GST_OBJECT (buffer), GST_OBJECT (sink));

  return buffer;
}

static void
gst_base_audio_sink_callback (GstRingBuffer * rbuf, guint8 * data, guint len,
    gpointer user_data)
{
  //GstBaseAudioSink *sink = GST_BASE_AUDIO_SINK (data);
}

static GstStateChangeReturn
gst_base_audio_sink_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstBaseAudioSink *sink = GST_BASE_AUDIO_SINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (sink->ringbuffer == NULL) {
        sink->ringbuffer = gst_base_audio_sink_create_ringbuffer (sink);
        gst_ring_buffer_set_callback (sink->ringbuffer,
            gst_base_audio_sink_callback, sink);
      }
      if (!gst_ring_buffer_open_device (sink->ringbuffer))
        goto open_failed;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      sink->next_sample = -1;
      gst_ring_buffer_set_flushing (sink->ringbuffer, FALSE);
      gst_ring_buffer_may_start (sink->ringbuffer, FALSE);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    {
      GstClock *clock;
      GstClockTime time, base;

      gst_ring_buffer_may_start (sink->ringbuffer, TRUE);

      GST_OBJECT_LOCK (sink);
      clock = GST_ELEMENT_CLOCK (sink);
      if (clock == NULL)
        goto no_clock;

      /* FIXME, only start slaving when we really start the ringbuffer */
      /* if we are slaved to a clock, we need to set the initial
       * calibration */
      if (clock != sink->provided_clock) {
        GstClockTime rate_num, rate_denom;

        base = element->base_time;
        time = gst_clock_get_internal_time (sink->provided_clock);

        GST_DEBUG_OBJECT (sink,
            "time: %" GST_TIME_FORMAT " base: %" GST_TIME_FORMAT,
            GST_TIME_ARGS (time), GST_TIME_ARGS (base));

        gst_clock_set_master (sink->provided_clock, clock);
        /* FIXME, this is not yet accurate enough for smooth playback */
        gst_clock_get_calibration (sink->provided_clock, NULL, NULL, &rate_num,
            &rate_denom);
        /* Does not work yet. */
        gst_clock_set_calibration (sink->provided_clock,
            time, element->base_time, rate_num, rate_denom);
      }
    no_clock:
      GST_OBJECT_UNLOCK (sink);
      break;
    }
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      /* ringbuffer cannot start anymore */
      gst_ring_buffer_may_start (sink->ringbuffer, FALSE);
      gst_ring_buffer_pause (sink->ringbuffer);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_ring_buffer_set_flushing (sink->ringbuffer, TRUE);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      /* slop slaving ourselves to the master, if any */
      gst_clock_set_master (sink->provided_clock, NULL);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_ring_buffer_release (sink->ringbuffer);
      gst_pad_set_caps (GST_BASE_SINK_PAD (sink), NULL);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_ring_buffer_close_device (sink->ringbuffer);
      break;
    default:
      break;
  }

  return ret;

open_failed:
  {
    GST_DEBUG_OBJECT (sink, "open failed");
    return GST_STATE_CHANGE_FAILURE;
  }
}
