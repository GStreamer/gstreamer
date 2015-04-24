/* GStreamer
 * Copyright (C) 2008 Wim Taymans <wim.taymans@gmail.com>
 *
 * gstaudioringbuffer.c:
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
 * SECTION:element-audioringbuffer
 * @short_description: Asynchronous audio ringbuffer.
 *
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <glib/gstdio.h>

#include <gst/gst.h>
#include <gst/gst-i18n-plugin.h>

#include <gst/audio/gstringbuffer.h>

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (audioringbuffer_debug);
#define GST_CAT_DEFAULT (audioringbuffer_debug)

enum
{
  LAST_SIGNAL
};

#define DEFAULT_BUFFER_TIME     ((200 * GST_MSECOND) / GST_USECOND)
#define DEFAULT_SEGMENT_TIME    ((10 * GST_MSECOND) / GST_USECOND)


enum
{
  PROP_0,
  PROP_BUFFER_TIME,
  PROP_SEGMENT_TIME
};

#define GST_TYPE_AUDIO_RINGBUFFER \
  (gst_audio_ringbuffer_get_type())
#define GST_AUDIO_RINGBUFFER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUDIO_RINGBUFFER,GstAudioRingbuffer))
#define GST_AUDIO_RINGBUFFER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUDIO_RINGBUFFER,GstAudioRingbufferClass))
#define GST_IS_AUDIO_RINGBUFFER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUDIO_RINGBUFFER))
#define GST_IS_AUDIO_RINGBUFFER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AUDIO_RINGBUFFER))
#define GST_AUDIO_RINGBUFFER_CAST(obj) \
  ((GstAudioRingbuffer *)(obj))

static GType gst_audio_ringbuffer_get_type (void);

typedef struct _GstAudioRingbuffer GstAudioRingbuffer;
typedef struct _GstAudioRingbufferClass GstAudioRingbufferClass;

typedef struct _GstIntRingBuffer GstIntRingBuffer;
typedef struct _GstIntRingBufferClass GstIntRingBufferClass;

struct _GstAudioRingbuffer
{
  GstElement element;

  /*< private > */
  GstPad *sinkpad;
  GstPad *srcpad;

  gboolean pushing;
  gboolean pulling;

  /* segments to keep track of timestamps */
  GstSegment sink_segment;
  GstSegment src_segment;

  /* flowreturn when srcpad is paused */
  gboolean is_eos;
  gboolean flushing;
  gboolean waiting;

  GCond *cond;

  GstRingBuffer *buffer;

  GstClockTime buffer_time;
  GstClockTime segment_time;

  guint64 next_sample;
  guint64 last_align;
};

struct _GstAudioRingbufferClass
{
  GstElementClass parent_class;
};


#define GST_TYPE_INT_RING_BUFFER             (gst_int_ring_buffer_get_type())
#define GST_INT_RING_BUFFER(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_INT_RING_BUFFER,GstIntRingBuffer))
#define GST_INT_RING_BUFFER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_INT_RING_BUFFER,GstIntRingBufferClass))
#define GST_INT_RING_BUFFER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_INT_RING_BUFFER, GstIntRingBufferClass))
#define GST_INT_RING_BUFFER_CAST(obj)        ((GstIntRingBuffer *)obj)
#define GST_IS_INT_RING_BUFFER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_INT_RING_BUFFER))
#define GST_IS_INT_RING_BUFFER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_INT_RING_BUFFER))


struct _GstIntRingBuffer
{
  GstRingBuffer object;
};

struct _GstIntRingBufferClass
{
  GstRingBufferClass parent_class;
};

GST_BOILERPLATE (GstIntRingBuffer, gst_int_ring_buffer, GstRingBuffer,
    GST_TYPE_RING_BUFFER);

static gboolean
gst_int_ring_buffer_acquire (GstRingBuffer * buf, GstRingBufferSpec * spec)
{
  spec->seglatency = spec->segtotal;

  buf->data = gst_buffer_new_and_alloc (spec->segtotal * spec->segsize);
  memset (GST_BUFFER_DATA (buf->data), 0, GST_BUFFER_SIZE (buf->data));

  return TRUE;
}

static gboolean
gst_int_ring_buffer_release (GstRingBuffer * buf)
{
  gst_buffer_unref (buf->data);
  buf->data = NULL;

  return TRUE;
}

static gboolean
gst_int_ring_buffer_start (GstRingBuffer * buf)
{
  GstAudioRingbuffer *ringbuffer;

  ringbuffer = GST_AUDIO_RINGBUFFER (GST_OBJECT_PARENT (buf));

  GST_OBJECT_LOCK (ringbuffer);
  if (G_UNLIKELY (ringbuffer->waiting)) {
    ringbuffer->waiting = FALSE;
    GST_DEBUG_OBJECT (ringbuffer, "start, sending signal");
    g_cond_broadcast (ringbuffer->cond);
  }
  GST_OBJECT_UNLOCK (ringbuffer);

  return TRUE;
}


static void
gst_int_ring_buffer_base_init (gpointer klass)
{
}

static void
gst_int_ring_buffer_class_init (GstIntRingBufferClass * klass)
{
  GstRingBufferClass *gstringbuffer_class;

  gstringbuffer_class = (GstRingBufferClass *) klass;

  gstringbuffer_class->acquire =
      GST_DEBUG_FUNCPTR (gst_int_ring_buffer_acquire);
  gstringbuffer_class->release =
      GST_DEBUG_FUNCPTR (gst_int_ring_buffer_release);
  gstringbuffer_class->start = GST_DEBUG_FUNCPTR (gst_int_ring_buffer_start);
}

static void
gst_int_ring_buffer_init (GstIntRingBuffer * buff,
    GstIntRingBufferClass * g_class)
{
}

static GstRingBuffer *
gst_int_ring_buffer_new (void)
{
  GstRingBuffer *res;

  res = g_object_new (GST_TYPE_INT_RING_BUFFER, NULL);

  return res;
}

/* can't use boilerplate as we need to register with Queue2 to avoid conflicts
 * with ringbuffer in core elements */
static void gst_audio_ringbuffer_class_init (GstAudioRingbufferClass * klass);
static void gst_audio_ringbuffer_init (GstAudioRingbuffer * ringbuffer,
    GstAudioRingbufferClass * g_class);
static GstElementClass *elem_parent_class;

static GType
gst_audio_ringbuffer_get_type (void)
{
  static GType gst_audio_ringbuffer_type = 0;

  if (!gst_audio_ringbuffer_type) {
    static const GTypeInfo gst_audio_ringbuffer_info = {
      sizeof (GstAudioRingbufferClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_audio_ringbuffer_class_init,
      NULL,
      NULL,
      sizeof (GstAudioRingbuffer),
      0,
      (GInstanceInitFunc) gst_audio_ringbuffer_init,
      NULL
    };

    gst_audio_ringbuffer_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstAudioRingbuffer",
        &gst_audio_ringbuffer_info, 0);
  }
  return gst_audio_ringbuffer_type;
}

static void gst_audio_ringbuffer_finalize (GObject * object);

static void gst_audio_ringbuffer_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_audio_ringbuffer_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_audio_ringbuffer_chain (GstPad * pad,
    GstBuffer * buffer);
static GstFlowReturn gst_audio_ringbuffer_bufferalloc (GstPad * pad,
    guint64 offset, guint size, GstCaps * caps, GstBuffer ** buf);

static gboolean gst_audio_ringbuffer_handle_sink_event (GstPad * pad,
    GstEvent * event);

static gboolean gst_audio_ringbuffer_handle_src_event (GstPad * pad,
    GstEvent * event);
static gboolean gst_audio_ringbuffer_handle_src_query (GstPad * pad,
    GstQuery * query);

static GstCaps *gst_audio_ringbuffer_getcaps (GstPad * pad);
static gboolean gst_audio_ringbuffer_setcaps (GstPad * pad, GstCaps * caps);

static GstFlowReturn gst_audio_ringbuffer_get_range (GstPad * pad,
    guint64 offset, guint length, GstBuffer ** buffer);
static gboolean gst_audio_ringbuffer_src_checkgetrange_function (GstPad * pad);

static gboolean gst_audio_ringbuffer_src_activate_pull (GstPad * pad,
    gboolean active);
static gboolean gst_audio_ringbuffer_src_activate_push (GstPad * pad,
    gboolean active);
static gboolean gst_audio_ringbuffer_sink_activate_push (GstPad * pad,
    gboolean active);

static GstStateChangeReturn gst_audio_ringbuffer_change_state (GstElement *
    element, GstStateChange transition);

/* static guint gst_audio_ringbuffer_signals[LAST_SIGNAL] = { 0 }; */

static void
gst_audio_ringbuffer_class_init (GstAudioRingbufferClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  elem_parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_audio_ringbuffer_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_audio_ringbuffer_get_property);

  g_object_class_install_property (gobject_class, PROP_BUFFER_TIME,
      g_param_spec_int64 ("buffer-time", "Buffer Time",
          "Size of audio buffer in nanoseconds", 1,
          G_MAXINT64, DEFAULT_BUFFER_TIME,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SEGMENT_TIME,
      g_param_spec_int64 ("segment-time", "Segment Time",
          "Audio segment duration in nanoseconds", 1,
          G_MAXINT64, DEFAULT_SEGMENT_TIME,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));

  gst_element_class_set_static_metadata (gstelement_class, "AudioRingbuffer",
      "Generic",
      "Asynchronous Audio ringbuffer", "Wim Taymans <wim.taymans@gmail.com>");

  /* set several parent class virtual functions */
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_audio_ringbuffer_finalize);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_audio_ringbuffer_change_state);
}

static void
gst_audio_ringbuffer_init (GstAudioRingbuffer * ringbuffer,
    GstAudioRingbufferClass * g_class)
{
  ringbuffer->sinkpad =
      gst_pad_new_from_static_template (&sinktemplate, "sink");

  gst_pad_set_chain_function (ringbuffer->sinkpad,
      GST_DEBUG_FUNCPTR (gst_audio_ringbuffer_chain));
  gst_pad_set_activatepush_function (ringbuffer->sinkpad,
      GST_DEBUG_FUNCPTR (gst_audio_ringbuffer_sink_activate_push));
  gst_pad_set_event_function (ringbuffer->sinkpad,
      GST_DEBUG_FUNCPTR (gst_audio_ringbuffer_handle_sink_event));
  gst_pad_set_getcaps_function (ringbuffer->sinkpad,
      GST_DEBUG_FUNCPTR (gst_audio_ringbuffer_getcaps));
  gst_pad_set_setcaps_function (ringbuffer->sinkpad,
      GST_DEBUG_FUNCPTR (gst_audio_ringbuffer_setcaps));
  gst_pad_set_bufferalloc_function (ringbuffer->sinkpad,
      GST_DEBUG_FUNCPTR (gst_audio_ringbuffer_bufferalloc));
  gst_element_add_pad (GST_ELEMENT (ringbuffer), ringbuffer->sinkpad);

  ringbuffer->srcpad = gst_pad_new_from_static_template (&srctemplate, "src");

  gst_pad_set_activatepull_function (ringbuffer->srcpad,
      GST_DEBUG_FUNCPTR (gst_audio_ringbuffer_src_activate_pull));
  gst_pad_set_activatepush_function (ringbuffer->srcpad,
      GST_DEBUG_FUNCPTR (gst_audio_ringbuffer_src_activate_push));
  gst_pad_set_getrange_function (ringbuffer->srcpad,
      GST_DEBUG_FUNCPTR (gst_audio_ringbuffer_get_range));
  gst_pad_set_checkgetrange_function (ringbuffer->srcpad,
      GST_DEBUG_FUNCPTR (gst_audio_ringbuffer_src_checkgetrange_function));
  gst_pad_set_getcaps_function (ringbuffer->srcpad,
      GST_DEBUG_FUNCPTR (gst_audio_ringbuffer_getcaps));
  gst_pad_set_event_function (ringbuffer->srcpad,
      GST_DEBUG_FUNCPTR (gst_audio_ringbuffer_handle_src_event));
  gst_pad_set_query_function (ringbuffer->srcpad,
      GST_DEBUG_FUNCPTR (gst_audio_ringbuffer_handle_src_query));
  gst_element_add_pad (GST_ELEMENT (ringbuffer), ringbuffer->srcpad);

  gst_segment_init (&ringbuffer->sink_segment, GST_FORMAT_TIME);

  ringbuffer->cond = g_cond_new ();

  ringbuffer->is_eos = FALSE;

  ringbuffer->buffer_time = DEFAULT_BUFFER_TIME;
  ringbuffer->segment_time = DEFAULT_SEGMENT_TIME;

  GST_DEBUG_OBJECT (ringbuffer,
      "initialized ringbuffer's not_empty & not_full conditions");
}

/* called only once, as opposed to dispose */
static void
gst_audio_ringbuffer_finalize (GObject * object)
{
  GstAudioRingbuffer *ringbuffer = GST_AUDIO_RINGBUFFER (object);

  GST_DEBUG_OBJECT (ringbuffer, "finalizing ringbuffer");

  g_cond_free (ringbuffer->cond);

  G_OBJECT_CLASS (elem_parent_class)->finalize (object);
}

static GstCaps *
gst_audio_ringbuffer_getcaps (GstPad * pad)
{
  GstAudioRingbuffer *ringbuffer;
  GstPad *otherpad;
  GstCaps *result;

  ringbuffer = GST_AUDIO_RINGBUFFER (GST_PAD_PARENT (pad));

  otherpad =
      (pad == ringbuffer->srcpad ? ringbuffer->sinkpad : ringbuffer->srcpad);
  result = gst_pad_peer_get_caps (otherpad);
  if (result == NULL)
    result = gst_caps_new_any ();

  return result;
}

static gboolean
gst_audio_ringbuffer_setcaps (GstPad * pad, GstCaps * caps)
{
  GstAudioRingbuffer *ringbuffer;
  GstRingBufferSpec *spec;

  ringbuffer = GST_AUDIO_RINGBUFFER (GST_PAD_PARENT (pad));

  if (!ringbuffer->buffer)
    return FALSE;

  spec = &ringbuffer->buffer->spec;

  GST_DEBUG_OBJECT (ringbuffer, "release old ringbuffer");

  /* release old ringbuffer */
  gst_ring_buffer_activate (ringbuffer->buffer, FALSE);
  gst_ring_buffer_release (ringbuffer->buffer);

  GST_DEBUG_OBJECT (ringbuffer, "parse caps");

  spec->buffer_time = ringbuffer->buffer_time;
  spec->latency_time = ringbuffer->segment_time;

  /* parse new caps */
  if (!gst_ring_buffer_parse_caps (spec, caps))
    goto parse_error;

  gst_ring_buffer_debug_spec_buff (spec);

  GST_DEBUG_OBJECT (ringbuffer, "acquire ringbuffer");
  if (!gst_ring_buffer_acquire (ringbuffer->buffer, spec))
    goto acquire_error;

  GST_DEBUG_OBJECT (ringbuffer, "activate ringbuffer");
  gst_ring_buffer_activate (ringbuffer->buffer, TRUE);

  /* calculate actual latency and buffer times. 
   * FIXME: In 0.11, store the latency_time internally in ns */
  spec->latency_time = gst_util_uint64_scale (spec->segsize,
      (GST_SECOND / GST_USECOND), spec->rate * spec->bytes_per_sample);

  spec->buffer_time = spec->segtotal * spec->latency_time;

  gst_ring_buffer_debug_spec_buff (spec);

  return TRUE;

  /* ERRORS */
parse_error:
  {
    GST_DEBUG_OBJECT (ringbuffer, "could not parse caps");
    GST_ELEMENT_ERROR (ringbuffer, STREAM, FORMAT,
        (NULL), ("cannot parse audio format."));
    return FALSE;
  }
acquire_error:
  {
    GST_DEBUG_OBJECT (ringbuffer, "could not acquire ringbuffer");
    return FALSE;
  }
}

static GstFlowReturn
gst_audio_ringbuffer_bufferalloc (GstPad * pad, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf)
{
  GstAudioRingbuffer *ringbuffer;
  GstFlowReturn result;

  ringbuffer = GST_AUDIO_RINGBUFFER (GST_PAD_PARENT (pad));

  /* Forward to src pad, without setting caps on the src pad */
  result = gst_pad_alloc_buffer (ringbuffer->srcpad, offset, size, caps, buf);

  return result;
}

static gboolean
gst_audio_ringbuffer_handle_sink_event (GstPad * pad, GstEvent * event)
{
  GstAudioRingbuffer *ringbuffer;
  gboolean forward;

  ringbuffer = GST_AUDIO_RINGBUFFER (GST_OBJECT_PARENT (pad));

  forward = ringbuffer->pushing || ringbuffer->pulling;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
    {
      GST_LOG_OBJECT (ringbuffer, "received flush start event");
      break;
    }
    case GST_EVENT_FLUSH_STOP:
    {
      ringbuffer->is_eos = FALSE;
      GST_LOG_OBJECT (ringbuffer, "received flush stop event");
      break;
    }
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      gdouble rate, arate;
      GstFormat format;
      gint64 start, stop, time;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);

      gst_segment_set_newsegment_full (&ringbuffer->sink_segment, update, rate,
          arate, format, start, stop, time);
      break;
    }
    case GST_EVENT_EOS:
      ringbuffer->is_eos = TRUE;
      break;
    default:
      break;
  }
  if (forward) {
    gst_pad_push_event (ringbuffer->srcpad, event);
  } else {
    if (event)
      gst_event_unref (event);
  }
  return TRUE;
}

#define DIFF_TOLERANCE  2

static GstFlowReturn
gst_audio_ringbuffer_render (GstAudioRingbuffer * ringbuffer, GstBuffer * buf)
{
  GstRingBuffer *rbuf;
  gint bps, accum;
  guint size;
  guint samples, written, out_samples;
  gint64 diff, align, ctime, cstop;
  guint8 *data;
  guint64 in_offset;
  GstClockTime time, stop, render_start, render_stop, sample_offset;
  gboolean align_next;

  rbuf = ringbuffer->buffer;

  /* can't do anything when we don't have the device */
  if (G_UNLIKELY (!gst_ring_buffer_is_acquired (rbuf)))
    goto wrong_state;

  bps = rbuf->spec.bytes_per_sample;

  size = GST_BUFFER_SIZE (buf);
  if (G_UNLIKELY (size % bps) != 0)
    goto wrong_size;

  samples = size / bps;
  out_samples = samples;

  in_offset = GST_BUFFER_OFFSET (buf);
  time = GST_BUFFER_TIMESTAMP (buf);

  GST_DEBUG_OBJECT (ringbuffer,
      "time %" GST_TIME_FORMAT ", offset %llu, start %" GST_TIME_FORMAT
      ", samples %u", GST_TIME_ARGS (time), in_offset,
      GST_TIME_ARGS (ringbuffer->sink_segment.start), samples);

  data = GST_BUFFER_DATA (buf);

  stop = time + gst_util_uint64_scale_int (samples, GST_SECOND,
      rbuf->spec.rate);

  if (!gst_segment_clip (&ringbuffer->sink_segment, GST_FORMAT_TIME, time, stop,
          &ctime, &cstop))
    goto out_of_segment;

  /* see if some clipping happened */
  diff = ctime - time;
  if (diff > 0) {
    /* bring clipped time to samples */
    diff = gst_util_uint64_scale_int (diff, rbuf->spec.rate, GST_SECOND);
    GST_DEBUG_OBJECT (ringbuffer, "clipping start to %" GST_TIME_FORMAT " %"
        G_GUINT64_FORMAT " samples", GST_TIME_ARGS (ctime), diff);
    samples -= diff;
    data += diff * bps;
    time = ctime;
  }
  diff = stop - cstop;
  if (diff > 0) {
    /* bring clipped time to samples */
    diff = gst_util_uint64_scale_int (diff, rbuf->spec.rate, GST_SECOND);
    GST_DEBUG_OBJECT (ringbuffer, "clipping stop to %" GST_TIME_FORMAT " %"
        G_GUINT64_FORMAT " samples", GST_TIME_ARGS (cstop), diff);
    samples -= diff;
    stop = cstop;
  }

  /* bring buffer start and stop times to running time */
  render_start =
      gst_segment_to_running_time (&ringbuffer->sink_segment, GST_FORMAT_TIME,
      time);
  render_stop =
      gst_segment_to_running_time (&ringbuffer->sink_segment, GST_FORMAT_TIME,
      stop);

  GST_DEBUG_OBJECT (ringbuffer,
      "running: start %" GST_TIME_FORMAT " - stop %" GST_TIME_FORMAT,
      GST_TIME_ARGS (render_start), GST_TIME_ARGS (render_stop));

  /* and bring the time to the rate corrected offset in the buffer */
  render_start = gst_util_uint64_scale_int (render_start,
      rbuf->spec.rate, GST_SECOND);
  render_stop = gst_util_uint64_scale_int (render_stop,
      rbuf->spec.rate, GST_SECOND);

  /* positive playback rate, first sample is render_start, negative rate, first
   * sample is render_stop. When no rate conversion is active, render exactly
   * the amount of input samples to avoid aligning to rounding errors. */
  if (ringbuffer->sink_segment.rate >= 0.0) {
    sample_offset = render_start;
    if (ringbuffer->sink_segment.rate == 1.0)
      render_stop = sample_offset + samples;
  } else {
    sample_offset = render_stop;
    if (ringbuffer->sink_segment.rate == -1.0)
      render_start = sample_offset + samples;
  }

  /* always resync after a discont */
  if (G_UNLIKELY (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT))) {
    GST_DEBUG_OBJECT (ringbuffer, "resync after discont");
    goto no_align;
  }

  /* resync when we don't know what to align the sample with */
  if (G_UNLIKELY (ringbuffer->next_sample == -1)) {
    GST_DEBUG_OBJECT (ringbuffer,
        "no align possible: no previous sample position known");
    goto no_align;
  }

  /* now try to align the sample to the previous one, first see how big the
   * difference is. */
  if (sample_offset >= ringbuffer->next_sample)
    diff = sample_offset - ringbuffer->next_sample;
  else
    diff = ringbuffer->next_sample - sample_offset;

  /* we tollerate half a second diff before we start resyncing. This
   * should be enough to compensate for various rounding errors in the timestamp
   * and sample offset position. We always resync if we got a discont anyway and
   * non-discont should be aligned by definition. */
  if (G_LIKELY (diff < rbuf->spec.rate / DIFF_TOLERANCE)) {
    /* calc align with previous sample */
    align = ringbuffer->next_sample - sample_offset;
    GST_DEBUG_OBJECT (ringbuffer,
        "align with prev sample, ABS (%" G_GINT64_FORMAT ") < %d", align,
        rbuf->spec.rate / DIFF_TOLERANCE);
  } else {
    /* bring sample diff to seconds for error message */
    diff = gst_util_uint64_scale_int (diff, GST_SECOND, rbuf->spec.rate);
    /* timestamps drifted apart from previous samples too much, we need to
     * resync. We log this as an element warning. */
    GST_ELEMENT_WARNING (ringbuffer, CORE, CLOCK,
        ("Compensating for audio synchronisation problems"),
        ("Unexpected discontinuity in audio timestamps of more "
            "than half a second (%" GST_TIME_FORMAT "), resyncing",
            GST_TIME_ARGS (diff)));
    align = 0;
  }
  ringbuffer->last_align = align;

  /* apply alignment */
  render_start += align;
  render_stop += align;

no_align:
  /* number of target samples is difference between start and stop */
  out_samples = render_stop - render_start;

  /* we render the first or last sample first, depending on the rate */
  if (ringbuffer->sink_segment.rate >= 0.0)
    sample_offset = render_start;
  else
    sample_offset = render_stop;

  GST_DEBUG_OBJECT (ringbuffer, "rendering at %" G_GUINT64_FORMAT " %d/%d",
      sample_offset, samples, out_samples);

  /* we need to accumulate over different runs for when we get interrupted */
  accum = 0;
  align_next = TRUE;
  do {
    written =
        gst_ring_buffer_commit_full (rbuf, &sample_offset, data, samples,
        out_samples, &accum);

    GST_DEBUG_OBJECT (ringbuffer, "wrote %u of %u", written, samples);
    /* if we wrote all, we're done */
    if (written == samples)
      break;

    GST_OBJECT_LOCK (ringbuffer);
    if (ringbuffer->flushing)
      goto flushing;
    GST_OBJECT_UNLOCK (ringbuffer);

    /* if we got interrupted, we cannot assume that the next sample should
     * be aligned to this one */
    align_next = FALSE;

    samples -= written;
    data += written * bps;
  } while (TRUE);

  if (align_next)
    ringbuffer->next_sample = sample_offset;
  else
    ringbuffer->next_sample = -1;

  GST_DEBUG_OBJECT (ringbuffer, "next sample expected at %" G_GUINT64_FORMAT,
      ringbuffer->next_sample);

  if (GST_CLOCK_TIME_IS_VALID (stop) && stop >= ringbuffer->sink_segment.stop) {
    GST_DEBUG_OBJECT (ringbuffer,
        "start playback because we are at the end of segment");
    gst_ring_buffer_start (rbuf);
  }

  return GST_FLOW_OK;

  /* SPECIAL cases */
out_of_segment:
  {
    GST_DEBUG_OBJECT (ringbuffer,
        "dropping sample out of segment time %" GST_TIME_FORMAT ", start %"
        GST_TIME_FORMAT, GST_TIME_ARGS (time),
        GST_TIME_ARGS (ringbuffer->sink_segment.start));
    return GST_FLOW_OK;
  }
  /* ERRORS */
wrong_state:
  {
    GST_DEBUG_OBJECT (ringbuffer, "ringbuffer not negotiated");
    GST_ELEMENT_ERROR (ringbuffer, STREAM, FORMAT, (NULL),
        ("ringbuffer not negotiated."));
    return GST_FLOW_NOT_NEGOTIATED;
  }
wrong_size:
  {
    GST_DEBUG_OBJECT (ringbuffer, "wrong size");
    GST_ELEMENT_ERROR (ringbuffer, STREAM, WRONG_TYPE,
        (NULL), ("ringbuffer received buffer of wrong size."));
    return GST_FLOW_ERROR;
  }
flushing:
  {
    GST_DEBUG_OBJECT (ringbuffer, "ringbuffer is flushing");
    GST_OBJECT_UNLOCK (ringbuffer);
    return GST_FLOW_FLUSHING;
  }
}

static GstFlowReturn
gst_audio_ringbuffer_chain (GstPad * pad, GstBuffer * buffer)
{
  GstFlowReturn res;
  GstAudioRingbuffer *ringbuffer;

  ringbuffer = GST_AUDIO_RINGBUFFER (GST_OBJECT_PARENT (pad));

  if (ringbuffer->pushing) {
    GST_DEBUG_OBJECT (ringbuffer, "proxy pushing buffer");
    res = gst_pad_push (ringbuffer->srcpad, buffer);
  } else {
    GST_DEBUG_OBJECT (ringbuffer, "render buffer in ringbuffer");
    res = gst_audio_ringbuffer_render (ringbuffer, buffer);
  }

  return res;
}

static gboolean
gst_audio_ringbuffer_handle_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstAudioRingbuffer *ringbuffer = GST_AUDIO_RINGBUFFER (GST_PAD_PARENT (pad));

  /* just forward upstream */
  res = gst_pad_push_event (ringbuffer->sinkpad, event);

  return res;
}

static gboolean
gst_audio_ringbuffer_handle_src_query (GstPad * pad, GstQuery * query)
{
  GstAudioRingbuffer *ringbuffer;

  ringbuffer = GST_AUDIO_RINGBUFFER (GST_PAD_PARENT (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
      break;
    case GST_QUERY_DURATION:
      break;
    case GST_QUERY_BUFFERING:
      break;
    default:
      break;
  }

  return TRUE;
}

static GstFlowReturn
gst_audio_ringbuffer_get_range (GstPad * pad, guint64 offset, guint length,
    GstBuffer ** buffer)
{
  GstAudioRingbuffer *ringbuffer;
  GstRingBuffer *rbuf;
  GstFlowReturn ret;

  ringbuffer = GST_AUDIO_RINGBUFFER_CAST (gst_pad_get_parent (pad));

  rbuf = ringbuffer->buffer;

  if (ringbuffer->pulling) {
    GST_DEBUG_OBJECT (ringbuffer, "proxy pulling range");
    ret = gst_pad_pull_range (ringbuffer->sinkpad, offset, length, buffer);
  } else {
    guint8 *data;
    guint len;
    guint64 sample;
    gint bps, segsize, segtotal, sps;
    gint sampleslen, segdone;
    gint readseg, sampleoff;
    guint8 *dest;

    GST_DEBUG_OBJECT (ringbuffer,
        "pulling data at %" G_GUINT64_FORMAT ", length %u", offset, length);

    if (offset != ringbuffer->src_segment.last_stop) {
      GST_DEBUG_OBJECT (ringbuffer, "expected offset %" G_GINT64_FORMAT,
          ringbuffer->src_segment.last_stop);
    }

    /* first wait till we have something in the ringbuffer and it 
     * is running */
    GST_OBJECT_LOCK (ringbuffer);
    if (ringbuffer->flushing)
      goto flushing;

    while (ringbuffer->waiting) {
      GST_DEBUG_OBJECT (ringbuffer, "waiting for unlock");
      g_cond_wait (ringbuffer->cond, GST_OBJECT_GET_LOCK (ringbuffer));
      GST_DEBUG_OBJECT (ringbuffer, "unlocked");

      if (ringbuffer->flushing)
        goto flushing;
    }
    GST_OBJECT_UNLOCK (ringbuffer);

    bps = rbuf->spec.bytes_per_sample;

    if (G_UNLIKELY (length % bps) != 0)
      goto wrong_size;

    segsize = rbuf->spec.segsize;
    segtotal = rbuf->spec.segtotal;
    sps = rbuf->samples_per_seg;
    dest = GST_BUFFER_DATA (rbuf->data);

    sample = offset / bps;
    len = length / bps;

    *buffer = gst_buffer_new_and_alloc (length);
    data = GST_BUFFER_DATA (*buffer);

    while (len) {
      gint diff;

      /* figure out the segment and the offset inside the segment where
       * the sample should be read from. */
      readseg = sample / sps;
      sampleoff = (sample % sps);

      segdone = g_atomic_int_get (&rbuf->segdone) - rbuf->segbase;

      diff = readseg - segdone;

      /* we can read now */
      readseg = readseg % segtotal;
      sampleslen = MIN (sps - sampleoff, len);

      GST_DEBUG_OBJECT (ringbuffer,
          "read @%p seg %d, off %d, sampleslen %d, diff %d",
          dest + readseg * segsize, readseg, sampleoff, sampleslen, diff);

      memcpy (data, dest + (readseg * segsize) + (sampleoff * bps),
          (sampleslen * bps));

      if (diff > 0)
        gst_ring_buffer_advance (rbuf, diff);

      len -= sampleslen;
      sample += sampleslen;
      data += sampleslen * bps;
    }

    ringbuffer->src_segment.last_stop += length;

    ret = GST_FLOW_OK;
  }

  gst_object_unref (ringbuffer);

  return ret;

  /* ERRORS */
flushing:
  {
    GST_DEBUG_OBJECT (ringbuffer, "we are flushing");
    GST_OBJECT_UNLOCK (ringbuffer);
    gst_object_unref (ringbuffer);
    return GST_FLOW_FLUSHING;
  }
wrong_size:
  {
    GST_DEBUG_OBJECT (ringbuffer, "wrong size");
    GST_ELEMENT_ERROR (ringbuffer, STREAM, WRONG_TYPE,
        (NULL), ("asked to pull buffer of wrong size."));
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_audio_ringbuffer_src_checkgetrange_function (GstPad * pad)
{
  gboolean ret;

  /* we can always operate in pull mode */
  ret = TRUE;

  return ret;
}

/* sink currently only operates in push mode */
static gboolean
gst_audio_ringbuffer_sink_activate_push (GstPad * pad, gboolean active)
{
  gboolean result = TRUE;
  GstAudioRingbuffer *ringbuffer;

  ringbuffer = GST_AUDIO_RINGBUFFER (gst_pad_get_parent (pad));

  if (active) {
    GST_DEBUG_OBJECT (ringbuffer, "activating push mode");
    ringbuffer->is_eos = FALSE;
    ringbuffer->pulling = FALSE;
  } else {
    /* unblock chain function */
    GST_DEBUG_OBJECT (ringbuffer, "deactivating push mode");
    ringbuffer->pulling = FALSE;
  }

  gst_object_unref (ringbuffer);

  return result;
}

/* src operating in push mode, we will proxy the push from upstream, basically
 * acting as a passthrough element. */
static gboolean
gst_audio_ringbuffer_src_activate_push (GstPad * pad, gboolean active)
{
  gboolean result = FALSE;
  GstAudioRingbuffer *ringbuffer;

  ringbuffer = GST_AUDIO_RINGBUFFER (gst_pad_get_parent (pad));

  if (active) {
    GST_DEBUG_OBJECT (ringbuffer, "activating push mode");
    ringbuffer->is_eos = FALSE;
    ringbuffer->pushing = TRUE;
    ringbuffer->pulling = FALSE;
    result = TRUE;
  } else {
    GST_DEBUG_OBJECT (ringbuffer, "deactivating push mode");
    ringbuffer->pushing = FALSE;
    ringbuffer->pulling = FALSE;
    result = TRUE;
  }

  gst_object_unref (ringbuffer);

  return result;
}

/* pull mode, downstream will call our getrange function */
static gboolean
gst_audio_ringbuffer_src_activate_pull (GstPad * pad, gboolean active)
{
  gboolean result;
  GstAudioRingbuffer *ringbuffer;

  ringbuffer = GST_AUDIO_RINGBUFFER (gst_pad_get_parent (pad));

  if (active) {
    GST_DEBUG_OBJECT (ringbuffer, "activating pull mode");

    /* try to activate upstream in pull mode as well. If it fails, no problems,
     * we'll be activated in push mode. Remember that we are pulling-through */
    ringbuffer->pulling = gst_pad_activate_pull (ringbuffer->sinkpad, active);

    ringbuffer->is_eos = FALSE;
    ringbuffer->waiting = TRUE;
    ringbuffer->flushing = FALSE;
    gst_segment_init (&ringbuffer->src_segment, GST_FORMAT_BYTES);
    result = TRUE;
  } else {
    GST_DEBUG_OBJECT (ringbuffer, "deactivating pull mode");

    if (ringbuffer->pulling)
      gst_pad_activate_pull (ringbuffer->sinkpad, active);

    ringbuffer->pulling = FALSE;
    ringbuffer->waiting = FALSE;
    ringbuffer->flushing = TRUE;
    result = TRUE;
  }
  gst_object_unref (ringbuffer);

  return result;
}

static GstStateChangeReturn
gst_audio_ringbuffer_change_state (GstElement * element,
    GstStateChange transition)
{
  GstAudioRingbuffer *ringbuffer;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  ringbuffer = GST_AUDIO_RINGBUFFER (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (ringbuffer->buffer == NULL) {
        ringbuffer->buffer = gst_int_ring_buffer_new ();
        gst_object_set_parent (GST_OBJECT (ringbuffer->buffer),
            GST_OBJECT (ringbuffer));
        gst_ring_buffer_open_device (ringbuffer->buffer);
      }
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      ringbuffer->next_sample = -1;
      ringbuffer->last_align = -1;
      gst_ring_buffer_set_flushing (ringbuffer->buffer, FALSE);
      gst_ring_buffer_may_start (ringbuffer->buffer, TRUE);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_OBJECT_LOCK (ringbuffer);
      ringbuffer->flushing = TRUE;
      ringbuffer->waiting = FALSE;
      g_cond_broadcast (ringbuffer->cond);
      GST_OBJECT_UNLOCK (ringbuffer);

      gst_ring_buffer_set_flushing (ringbuffer->buffer, TRUE);
      gst_ring_buffer_may_start (ringbuffer->buffer, FALSE);
      break;
    default:
      break;
  }

  ret =
      GST_ELEMENT_CLASS (elem_parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_ring_buffer_activate (ringbuffer->buffer, FALSE);
      gst_ring_buffer_release (ringbuffer->buffer);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (ringbuffer->buffer != NULL) {
        gst_ring_buffer_close_device (ringbuffer->buffer);
        gst_object_unparent (GST_OBJECT (ringbuffer->buffer));
        ringbuffer->buffer = NULL;
      }
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_audio_ringbuffer_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstAudioRingbuffer *ringbuffer;

  ringbuffer = GST_AUDIO_RINGBUFFER (object);

  switch (prop_id) {
    case PROP_BUFFER_TIME:
      ringbuffer->buffer_time = g_value_get_int64 (value);
      break;
    case PROP_SEGMENT_TIME:
      ringbuffer->segment_time = g_value_get_int64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_audio_ringbuffer_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstAudioRingbuffer *ringbuffer;

  ringbuffer = GST_AUDIO_RINGBUFFER (object);

  switch (prop_id) {
    case PROP_BUFFER_TIME:
      g_value_set_int64 (value, ringbuffer->buffer_time);
      break;
    case PROP_SEGMENT_TIME:
      g_value_set_int64 (value, ringbuffer->segment_time);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (audioringbuffer_debug, "audioringbuffer", 0,
      "Audio ringbuffer element");

#ifdef ENABLE_NLS
  GST_DEBUG ("binding text domain %s to locale dir %s", GETTEXT_PACKAGE,
      LOCALEDIR);
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
#endif /* ENABLE_NLS */

  return gst_element_register (plugin, "audioringbuffer", GST_RANK_NONE,
      GST_TYPE_AUDIO_RINGBUFFER);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    audioringbuffer,
    "An audio ringbuffer", plugin_init, VERSION, GST_LICENSE,
    GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
