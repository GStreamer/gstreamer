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

GST_DEBUG_CATEGORY_STATIC (gst_baseaudiosink_debug);
#define GST_CAT_DEFAULT gst_baseaudiosink_debug

/* BaseAudioSink signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_BUFFER_TIME	500 * GST_USECOND
#define DEFAULT_LATENCY_TIME	10 * GST_USECOND
enum
{
  PROP_0,
  PROP_BUFFER_TIME,
  PROP_LATENCY_TIME,
};

#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_baseaudiosink_debug, "baseaudiosink", 0, "baseaudiosink element");

GST_BOILERPLATE_FULL (GstBaseAudioSink, gst_baseaudiosink, GstBaseSink,
    GST_TYPE_BASESINK, _do_init);

static void gst_baseaudiosink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_baseaudiosink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstElementStateReturn gst_baseaudiosink_change_state (GstElement *
    element);

static GstClock *gst_baseaudiosink_get_clock (GstElement * elem);
static GstClockTime gst_baseaudiosink_get_time (GstClock * clock,
    GstBaseAudioSink * sink);

static GstFlowReturn gst_baseaudiosink_preroll (GstBaseSink * bsink,
    GstBuffer * buffer);
static GstFlowReturn gst_baseaudiosink_render (GstBaseSink * bsink,
    GstBuffer * buffer);
static gboolean gst_baseaudiosink_event (GstBaseSink * bsink, GstEvent * event);
static void gst_baseaudiosink_get_times (GstBaseSink * bsink,
    GstBuffer * buffer, GstClockTime * start, GstClockTime * end);
static gboolean gst_baseaudiosink_setcaps (GstBaseSink * bsink, GstCaps * caps);

//static guint gst_baseaudiosink_signals[LAST_SIGNAL] = { 0 };

static void
gst_baseaudiosink_base_init (gpointer g_class)
{
}

static void
gst_baseaudiosink_class_init (GstBaseAudioSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_baseaudiosink_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_baseaudiosink_get_property);

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BUFFER_TIME,
      g_param_spec_int64 ("buffer-time", "Buffer Time",
          "Size of audio buffer in milliseconds (-1 = default)",
          -1, G_MAXINT64, DEFAULT_BUFFER_TIME, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_LATENCY_TIME,
      g_param_spec_int64 ("latency-time", "Latency Time",
          "Audio latency in milliseconds (-1 = default)",
          -1, G_MAXINT64, DEFAULT_LATENCY_TIME, G_PARAM_READWRITE));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_baseaudiosink_change_state);
  gstelement_class->get_clock = GST_DEBUG_FUNCPTR (gst_baseaudiosink_get_clock);

  gstbasesink_class->event = GST_DEBUG_FUNCPTR (gst_baseaudiosink_event);
  gstbasesink_class->preroll = GST_DEBUG_FUNCPTR (gst_baseaudiosink_preroll);
  gstbasesink_class->render = GST_DEBUG_FUNCPTR (gst_baseaudiosink_render);
  gstbasesink_class->get_times =
      GST_DEBUG_FUNCPTR (gst_baseaudiosink_get_times);
  gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_baseaudiosink_setcaps);
}

static void
gst_baseaudiosink_init (GstBaseAudioSink * baseaudiosink)
{
  baseaudiosink->buffer_time = DEFAULT_BUFFER_TIME;
  baseaudiosink->latency_time = DEFAULT_LATENCY_TIME;

  baseaudiosink->clock = gst_audio_clock_new ("clock",
      (GstAudioClockGetTimeFunc) gst_baseaudiosink_get_time, baseaudiosink);
}

static GstClock *
gst_baseaudiosink_get_clock (GstElement * elem)
{
  GstBaseAudioSink *sink;

  sink = GST_BASEAUDIOSINK (elem);

  return GST_CLOCK (gst_object_ref (sink->clock));
}

static GstClockTime
gst_baseaudiosink_get_time (GstClock * clock, GstBaseAudioSink * sink)
{
  guint64 samples;
  GstClockTime result;

  if (sink->ringbuffer == NULL || sink->ringbuffer->spec.rate == 0)
    return 0;

  samples = gst_ringbuffer_played_samples (sink->ringbuffer);

  result = samples * GST_SECOND / sink->ringbuffer->spec.rate;
  result += GST_ELEMENT (sink)->base_time;

  return result;
}

static void
gst_baseaudiosink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBaseAudioSink *sink;

  sink = GST_BASEAUDIOSINK (object);

  switch (prop_id) {
    case PROP_BUFFER_TIME:
      sink->buffer_time = g_value_get_int64 (value);
      break;
    case PROP_LATENCY_TIME:
      sink->latency_time = g_value_get_int64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_baseaudiosink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstBaseAudioSink *sink;

  sink = GST_BASEAUDIOSINK (object);

  switch (prop_id) {
    case PROP_BUFFER_TIME:
      g_value_set_int64 (value, sink->buffer_time);
      break;
    case PROP_LATENCY_TIME:
      g_value_set_int64 (value, sink->latency_time);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static int linear_formats[4 * 2 * 2] = {
  GST_S8,
  GST_S8,
  GST_U8,
  GST_U8,
  GST_S16_LE,
  GST_S16_BE,
  GST_U16_LE,
  GST_U16_BE,
  GST_S24_LE,
  GST_S24_BE,
  GST_U24_LE,
  GST_U24_BE,
  GST_S32_LE,
  GST_S32_BE,
  GST_U32_LE,
  GST_U32_BE
};

static int linear24_formats[3 * 2 * 2] = {
  GST_S24_3LE,
  GST_S24_3BE,
  GST_U24_3LE,
  GST_U24_3BE,
  GST_S20_3LE,
  GST_S20_3BE,
  GST_U20_3LE,
  GST_U20_3BE,
  GST_S18_3LE,
  GST_S18_3BE,
  GST_U18_3LE,
  GST_U18_3BE,
};

static GstBufferFormat
build_linear_format (int depth, int width, int unsignd, int big_endian)
{
  if (width == 24) {
    switch (depth) {
      case 24:
        depth = 0;
        break;
      case 20:
        depth = 1;
        break;
      case 18:
        depth = 2;
        break;
      default:
        return GST_UNKNOWN;
    }
    return ((int (*)[2][2]) linear24_formats)[depth][!!unsignd][!!big_endian];
  } else {
    switch (depth) {
      case 8:
        depth = 0;
        break;
      case 16:
        depth = 1;
        break;
      case 24:
        depth = 2;
        break;
      case 32:
        depth = 3;
        break;
      default:
        return GST_UNKNOWN;
    }
  }
  return ((int (*)[2][2]) linear_formats)[depth][!!unsignd][!!big_endian];
}

static void
debug_spec_caps (GstBaseAudioSink * sink, GstRingBufferSpec * spec)
{
  GST_DEBUG ("spec caps: %p %" GST_PTR_FORMAT, spec->caps, spec->caps);
  GST_DEBUG ("parsed caps: type:         %d", spec->type);
  GST_DEBUG ("parsed caps: format:       %d", spec->format);
  GST_DEBUG ("parsed caps: width:        %d", spec->width);
  GST_DEBUG ("parsed caps: depth:        %d", spec->depth);
  GST_DEBUG ("parsed caps: sign:         %d", spec->sign);
  GST_DEBUG ("parsed caps: bigend:       %d", spec->bigend);
  GST_DEBUG ("parsed caps: rate:         %d", spec->rate);
  GST_DEBUG ("parsed caps: channels:     %d", spec->channels);
  GST_DEBUG ("parsed caps: sample bytes: %d", spec->bytes_per_sample);
}

static void
debug_spec_buffer (GstBaseAudioSink * sink, GstRingBufferSpec * spec)
{
  GST_DEBUG ("acquire ringbuffer: buffer time: %" G_GINT64_FORMAT " usec",
      spec->buffer_time);
  GST_DEBUG ("acquire ringbuffer: latency time: %" G_GINT64_FORMAT " usec",
      spec->latency_time);
  GST_DEBUG ("acquire ringbuffer: total segments: %d", spec->segtotal);
  GST_DEBUG ("acquire ringbuffer: segment size: %d bytes = %d samples",
      spec->segsize, spec->segsize / spec->bytes_per_sample);
  GST_DEBUG ("acquire ringbuffer: buffer size: %d bytes = %d samples",
      spec->segsize * spec->segtotal,
      spec->segsize * spec->segtotal / spec->bytes_per_sample);
}

static gboolean
gst_baseaudiosink_setcaps (GstBaseSink * bsink, GstCaps * caps)
{
  GstBaseAudioSink *sink = GST_BASEAUDIOSINK (bsink);
  GstRingBufferSpec *spec;
  const gchar *mimetype;
  GstStructure *structure;

  spec = &sink->ringbuffer->spec;

  structure = gst_caps_get_structure (caps, 0);

  /* we have to differentiate between int and float formats */
  mimetype = gst_structure_get_name (structure);

  if (!strncmp (mimetype, "audio/x-raw-int", 15)) {
    gint endianness;

    spec->type = GST_BUFTYPE_LINEAR;

    /* extract the needed information from the cap */
    if (!(gst_structure_get_int (structure, "width", &spec->width) &&
            gst_structure_get_int (structure, "depth", &spec->depth) &&
            gst_structure_get_boolean (structure, "signed", &spec->sign)))
      goto parse_error;

    /* extract endianness if needed */
    if (spec->width > 8) {
      if (!gst_structure_get_int (structure, "endianness", &endianness))
        goto parse_error;
    } else {
      endianness = G_BYTE_ORDER;
    }

    spec->bigend = endianness == G_LITTLE_ENDIAN ? FALSE : TRUE;

    spec->format =
        build_linear_format (spec->depth, spec->width, spec->sign ? 0 : 1,
        spec->bigend ? 1 : 0);

  } else if (!strncmp (mimetype, "audio/x-raw-float", 17)) {

    spec->type = GST_BUFTYPE_FLOAT;

    /* get layout */
    if (!gst_structure_get_int (structure, "width", &spec->width))
      goto parse_error;

    /* match layout to format wrt to endianness */
    switch (spec->width) {
      case 32:
        spec->format =
            G_BYTE_ORDER == G_LITTLE_ENDIAN ? GST_FLOAT32_LE : GST_FLOAT32_BE;
        break;
      case 64:
        spec->format =
            G_BYTE_ORDER == G_LITTLE_ENDIAN ? GST_FLOAT64_LE : GST_FLOAT64_BE;
        break;
      default:
        goto parse_error;
    }
  } else if (!strncmp (mimetype, "audio/x-alaw", 12)) {
    spec->type = GST_BUFTYPE_A_LAW;
    spec->format = GST_A_LAW;
  } else if (!strncmp (mimetype, "audio/x-mulaw", 13)) {
    spec->type = GST_BUFTYPE_MU_LAW;
    spec->format = GST_MU_LAW;
  } else {
    goto parse_error;
  }

  /* get rate and channels */
  if (!(gst_structure_get_int (structure, "rate", &spec->rate) &&
          gst_structure_get_int (structure, "channels", &spec->channels)))
    goto parse_error;

  spec->bytes_per_sample = (spec->width >> 3) * spec->channels;

  gst_caps_replace (&spec->caps, caps);

  debug_spec_caps (sink, spec);

  spec->buffer_time = sink->buffer_time;
  spec->latency_time = sink->latency_time;

  /* calculate suggested segsize and segtotal */
  spec->segsize =
      spec->rate * spec->bytes_per_sample * spec->latency_time / GST_MSECOND;
  spec->segtotal = spec->buffer_time / spec->latency_time;

  GST_DEBUG ("release old ringbuffer");

  gst_ringbuffer_release (sink->ringbuffer);

  debug_spec_buffer (sink, spec);

  if (!gst_ringbuffer_acquire (sink->ringbuffer, spec))
    goto acquire_error;

  /* calculate actual latency and buffer times */
  spec->latency_time =
      spec->segsize * GST_MSECOND / (spec->rate * spec->bytes_per_sample);
  spec->buffer_time =
      spec->segtotal * spec->segsize * GST_MSECOND / (spec->rate *
      spec->bytes_per_sample);

  debug_spec_buffer (sink, spec);

  return TRUE;

  /* ERRORS */
parse_error:
  {
    GST_DEBUG ("could not parse caps");
    return FALSE;
  }
acquire_error:
  {
    GST_DEBUG ("could not acquire ringbuffer");
    return FALSE;
  }
}

static void
gst_baseaudiosink_get_times (GstBaseSink * bsink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  /* ne need to sync to a clock here, we schedule the samples based
   * on our own clock for the moment. FIXME, implement this when
   * we are not using our own clock */
  *start = GST_CLOCK_TIME_NONE;
  *end = GST_CLOCK_TIME_NONE;
}

static gboolean
gst_baseaudiosink_event (GstBaseSink * bsink, GstEvent * event)
{
  GstBaseAudioSink *sink = GST_BASEAUDIOSINK (bsink);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH:
      if (GST_EVENT_FLUSH_DONE (event)) {
      } else {
        gst_ringbuffer_pause (sink->ringbuffer);
      }
      break;
    case GST_EVENT_DISCONTINUOUS:
    {
      gint64 time, sample;

      if (gst_event_discont_get_value (event, GST_FORMAT_DEFAULT, &sample,
              NULL))
        goto have_value;
      if (gst_event_discont_get_value (event, GST_FORMAT_TIME, &time, NULL)) {
        sample = time * sink->ringbuffer->spec.rate / GST_SECOND;
        goto have_value;
      }
      g_warning ("discont without valid timestamp");
      sample = 0;

    have_value:
      GST_DEBUG ("discont now at %lld", sample);
      gst_ringbuffer_set_sample (sink->ringbuffer, sample);
      break;
    }
    default:
      break;
  }
  return TRUE;
}

static GstFlowReturn
gst_baseaudiosink_preroll (GstBaseSink * bsink, GstBuffer * buffer)
{
  /* we don't really do anything when prerolling. We could make a
   * property to play this buffer to have some sort of scrubbing
   * support. */
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_baseaudiosink_render (GstBaseSink * bsink, GstBuffer * buf)
{
  guint64 offset;
  GstBaseAudioSink *sink = GST_BASEAUDIOSINK (bsink);

  offset = GST_BUFFER_OFFSET (buf);

  GST_DEBUG ("in offset %llu, time %" GST_TIME_FORMAT, offset,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));
  if (!gst_ringbuffer_is_acquired (sink->ringbuffer))
    goto wrong_state;

  gst_ringbuffer_commit (sink->ringbuffer, offset,
      GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));

  return GST_FLOW_OK;

wrong_state:
  {
    GST_DEBUG ("ringbuffer in wrong state");
    return GST_FLOW_ERROR;
  }
}

GstRingBuffer *
gst_baseaudiosink_create_ringbuffer (GstBaseAudioSink * sink)
{
  GstBaseAudioSinkClass *bclass;
  GstRingBuffer *buffer = NULL;

  bclass = GST_BASEAUDIOSINK_GET_CLASS (sink);
  if (bclass->create_ringbuffer)
    buffer = bclass->create_ringbuffer (sink);

  if (buffer) {
    gst_object_set_parent (GST_OBJECT (buffer), GST_OBJECT (sink));
  }

  return buffer;
}

void
gst_baseaudiosink_callback (GstRingBuffer * rbuf, guint8 * data, guint len,
    gpointer user_data)
{
  //GstBaseAudioSink *sink = GST_BASEAUDIOSINK (data);
}

static GstElementStateReturn
gst_baseaudiosink_change_state (GstElement * element)
{
  GstElementStateReturn ret = GST_STATE_SUCCESS;
  GstBaseAudioSink *sink = GST_BASEAUDIOSINK (element);
  GstElementState transition = GST_STATE_TRANSITION (element);

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      sink->ringbuffer = gst_baseaudiosink_create_ringbuffer (sink);
      gst_ringbuffer_set_callback (sink->ringbuffer, gst_baseaudiosink_callback,
          sink);
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element);

  switch (transition) {
    case GST_STATE_PLAYING_TO_PAUSED:
      gst_ringbuffer_pause (sink->ringbuffer);
      break;
    case GST_STATE_PAUSED_TO_READY:
      gst_ringbuffer_stop (sink->ringbuffer);
      gst_ringbuffer_release (sink->ringbuffer);
      gst_object_unref (sink->ringbuffer);
      sink->ringbuffer = NULL;
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}
