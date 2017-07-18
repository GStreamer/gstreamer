/* GStreamer
 * Copyright (C) 2011 David Schleef <ds@entropywave.com>
 * Copyright (C) 2014 Sebastian Dröge <sebastian@centricular.com>
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
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdecklinkaudiosink.h"

GST_DEBUG_CATEGORY_STATIC (gst_decklink_audio_sink_debug);
#define GST_CAT_DEFAULT gst_decklink_audio_sink_debug

// Ringbuffer implementation

#define GST_TYPE_DECKLINK_AUDIO_SINK_RING_BUFFER \
  (gst_decklink_audio_sink_ringbuffer_get_type())
#define GST_DECKLINK_AUDIO_SINK_RING_BUFFER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DECKLINK_AUDIO_SINK_RING_BUFFER,GstDecklinkAudioSinkRingBuffer))
#define GST_DECKLINK_AUDIO_SINK_RING_BUFFER_CAST(obj) \
  ((GstDecklinkAudioSinkRingBuffer*) obj)
#define GST_DECKLINK_AUDIO_SINK_RING_BUFFER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DECKLINK_AUDIO_SINK_RING_BUFFER,GstDecklinkAudioSinkRingBufferClass))
#define GST_DECKLINK_AUDIO_SINK_RING_BUFFER_CAST_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_DECKLINK_AUDIO_SINK_RING_BUFFER,GstDecklinkAudioSinkRingBufferClass))
#define GST_IS_DECKLINK_AUDIO_SINK_RING_BUFFER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DECKLINK_AUDIO_SINK_RING_BUFFER))
#define GST_IS_DECKLINK_AUDIO_SINK_RING_BUFFER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DECKLINK_AUDIO_SINK_RING_BUFFER))

typedef struct _GstDecklinkAudioSinkRingBuffer GstDecklinkAudioSinkRingBuffer;
typedef struct _GstDecklinkAudioSinkRingBufferClass
    GstDecklinkAudioSinkRingBufferClass;

struct _GstDecklinkAudioSinkRingBuffer
{
  GstAudioRingBuffer object;

  GstDecklinkOutput *output;
  GstDecklinkAudioSink *sink;

  GMutex clock_id_lock;
  GstClockID clock_id;
};

struct _GstDecklinkAudioSinkRingBufferClass
{
  GstAudioRingBufferClass parent_class;
};

GType gst_decklink_audio_sink_ringbuffer_get_type (void);

static void gst_decklink_audio_sink_ringbuffer_finalize (GObject * object);

static void gst_decklink_audio_sink_ringbuffer_clear_all (GstAudioRingBuffer *
    rb);
static guint gst_decklink_audio_sink_ringbuffer_delay (GstAudioRingBuffer * rb);
static gboolean gst_decklink_audio_sink_ringbuffer_start (GstAudioRingBuffer *
    rb);
static gboolean gst_decklink_audio_sink_ringbuffer_pause (GstAudioRingBuffer *
    rb);
static gboolean gst_decklink_audio_sink_ringbuffer_stop (GstAudioRingBuffer *
    rb);
static gboolean gst_decklink_audio_sink_ringbuffer_acquire (GstAudioRingBuffer *
    rb, GstAudioRingBufferSpec * spec);
static gboolean gst_decklink_audio_sink_ringbuffer_release (GstAudioRingBuffer *
    rb);
static gboolean
gst_decklink_audio_sink_ringbuffer_open_device (GstAudioRingBuffer * rb);
static gboolean
gst_decklink_audio_sink_ringbuffer_close_device (GstAudioRingBuffer * rb);

#define ringbuffer_parent_class gst_decklink_audio_sink_ringbuffer_parent_class
G_DEFINE_TYPE (GstDecklinkAudioSinkRingBuffer,
    gst_decklink_audio_sink_ringbuffer, GST_TYPE_AUDIO_RING_BUFFER);

static void
    gst_decklink_audio_sink_ringbuffer_class_init
    (GstDecklinkAudioSinkRingBufferClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstAudioRingBufferClass *gstringbuffer_class =
      GST_AUDIO_RING_BUFFER_CLASS (klass);

  gobject_class->finalize = gst_decklink_audio_sink_ringbuffer_finalize;

  gstringbuffer_class->open_device =
      GST_DEBUG_FUNCPTR (gst_decklink_audio_sink_ringbuffer_open_device);
  gstringbuffer_class->close_device =
      GST_DEBUG_FUNCPTR (gst_decklink_audio_sink_ringbuffer_close_device);
  gstringbuffer_class->acquire =
      GST_DEBUG_FUNCPTR (gst_decklink_audio_sink_ringbuffer_acquire);
  gstringbuffer_class->release =
      GST_DEBUG_FUNCPTR (gst_decklink_audio_sink_ringbuffer_release);
  gstringbuffer_class->start =
      GST_DEBUG_FUNCPTR (gst_decklink_audio_sink_ringbuffer_start);
  gstringbuffer_class->pause =
      GST_DEBUG_FUNCPTR (gst_decklink_audio_sink_ringbuffer_pause);
  gstringbuffer_class->resume =
      GST_DEBUG_FUNCPTR (gst_decklink_audio_sink_ringbuffer_start);
  gstringbuffer_class->stop =
      GST_DEBUG_FUNCPTR (gst_decklink_audio_sink_ringbuffer_stop);
  gstringbuffer_class->delay =
      GST_DEBUG_FUNCPTR (gst_decklink_audio_sink_ringbuffer_delay);
  gstringbuffer_class->clear_all =
      GST_DEBUG_FUNCPTR (gst_decklink_audio_sink_ringbuffer_clear_all);
}

static void
gst_decklink_audio_sink_ringbuffer_init (GstDecklinkAudioSinkRingBuffer * self)
{
  g_mutex_init (&self->clock_id_lock);
}

static void
gst_decklink_audio_sink_ringbuffer_finalize (GObject * object)
{
  GstDecklinkAudioSinkRingBuffer *self =
      GST_DECKLINK_AUDIO_SINK_RING_BUFFER_CAST (object);

  gst_object_unref (self->sink);
  self->sink = NULL;
  g_mutex_clear (&self->clock_id_lock);

  G_OBJECT_CLASS (ringbuffer_parent_class)->finalize (object);
}

class GStreamerAudioOutputCallback:public IDeckLinkAudioOutputCallback
{
public:
  GStreamerAudioOutputCallback (GstDecklinkAudioSinkRingBuffer * ringbuffer)
  :IDeckLinkAudioOutputCallback (), m_refcount (1)
  {
    m_ringbuffer =
        GST_DECKLINK_AUDIO_SINK_RING_BUFFER_CAST (gst_object_ref (ringbuffer));
    g_mutex_init (&m_mutex);
  }

  virtual HRESULT WINAPI QueryInterface (REFIID, LPVOID *)
  {
    return E_NOINTERFACE;
  }

  virtual ULONG WINAPI AddRef (void)
  {
    ULONG ret;

    g_mutex_lock (&m_mutex);
    m_refcount++;
    ret = m_refcount;
    g_mutex_unlock (&m_mutex);

    return ret;
  }

  virtual ULONG WINAPI Release (void)
  {
    ULONG ret;

    g_mutex_lock (&m_mutex);
    m_refcount--;
    ret = m_refcount;
    g_mutex_unlock (&m_mutex);

    if (ret == 0) {
      delete this;
    }

    return ret;
  }

  virtual ~ GStreamerAudioOutputCallback () {
    gst_object_unref (m_ringbuffer);
    g_mutex_clear (&m_mutex);
  }

  virtual HRESULT WINAPI RenderAudioSamples (bool preroll)
  {
    guint8 *ptr;
    gint seg;
    gint len;
    gint bpf;
    guint written, written_sum;
    HRESULT res;
    const GstAudioRingBufferSpec *spec =
        &GST_AUDIO_RING_BUFFER_CAST (m_ringbuffer)->spec;
    guint delay, max_delay;

    GST_LOG_OBJECT (m_ringbuffer->sink, "Writing audio samples (preroll: %d)",
        preroll);

    delay =
        gst_audio_ring_buffer_delay (GST_AUDIO_RING_BUFFER_CAST (m_ringbuffer));
    max_delay = MAX ((spec->segtotal * spec->segsize) / 2, spec->segsize);
    max_delay /= GST_AUDIO_INFO_BPF (&spec->info);
    if (delay > max_delay) {
      GstClock *clock =
          gst_element_get_clock (GST_ELEMENT_CAST (m_ringbuffer->sink));
      GstClockTime wait_time;
      GstClockID clock_id;
      GstClockReturn clock_ret;

      GST_DEBUG_OBJECT (m_ringbuffer->sink, "Delay %u > max delay %u", delay,
          max_delay);

      wait_time =
          gst_util_uint64_scale (delay - max_delay, GST_SECOND,
          GST_AUDIO_INFO_RATE (&spec->info));
      GST_DEBUG_OBJECT (m_ringbuffer->sink, "Waiting for %" GST_TIME_FORMAT,
          GST_TIME_ARGS (wait_time));
      wait_time += gst_clock_get_time (clock);

      g_mutex_lock (&m_ringbuffer->clock_id_lock);
      if (!GST_AUDIO_RING_BUFFER_CAST (m_ringbuffer)->acquired) {
        GST_DEBUG_OBJECT (m_ringbuffer->sink,
            "Ringbuffer not acquired anymore");
        g_mutex_unlock (&m_ringbuffer->clock_id_lock);
        gst_object_unref (clock);
        return S_OK;
      }
      clock_id = gst_clock_new_single_shot_id (clock, wait_time);
      m_ringbuffer->clock_id = clock_id;
      g_mutex_unlock (&m_ringbuffer->clock_id_lock);
      gst_object_unref (clock);

      clock_ret = gst_clock_id_wait (clock_id, NULL);

      g_mutex_lock (&m_ringbuffer->clock_id_lock);
      gst_clock_id_unref (clock_id);
      m_ringbuffer->clock_id = NULL;
      g_mutex_unlock (&m_ringbuffer->clock_id_lock);

      if (clock_ret == GST_CLOCK_UNSCHEDULED) {
        GST_DEBUG_OBJECT (m_ringbuffer->sink, "Flushing");
        return S_OK;
      }
    }

    if (!gst_audio_ring_buffer_prepare_read (GST_AUDIO_RING_BUFFER_CAST
            (m_ringbuffer), &seg, &ptr, &len)) {
      GST_WARNING_OBJECT (m_ringbuffer->sink, "No segment available");
      return E_FAIL;
    }

    bpf =
        GST_AUDIO_INFO_BPF (&GST_AUDIO_RING_BUFFER_CAST (m_ringbuffer)->
        spec.info);
    len /= bpf;
    GST_LOG_OBJECT (m_ringbuffer->sink,
        "Write audio samples: %p size %d segment: %d", ptr, len, seg);

    written_sum = 0;
    do {
      res =
          m_ringbuffer->output->output->ScheduleAudioSamples (ptr, len,
          0, 0, &written);
      len -= written;
      ptr += written * bpf;
      written_sum += written;
    } while (len > 0 && res == S_OK);

    GST_LOG_OBJECT (m_ringbuffer->sink, "Wrote %u samples: 0x%08x", written_sum,
        res);

    gst_audio_ring_buffer_clear (GST_AUDIO_RING_BUFFER_CAST (m_ringbuffer),
        seg);
    gst_audio_ring_buffer_advance (GST_AUDIO_RING_BUFFER_CAST (m_ringbuffer),
        1);

    return res;
  }

private:
  GstDecklinkAudioSinkRingBuffer * m_ringbuffer;
  GMutex m_mutex;
  gint m_refcount;
};

static void
gst_decklink_audio_sink_ringbuffer_clear_all (GstAudioRingBuffer * rb)
{
  GstDecklinkAudioSinkRingBuffer *self =
      GST_DECKLINK_AUDIO_SINK_RING_BUFFER_CAST (rb);

  GST_DEBUG_OBJECT (self->sink, "Flushing");

  if (self->output)
    self->output->output->FlushBufferedAudioSamples ();
}

static guint
gst_decklink_audio_sink_ringbuffer_delay (GstAudioRingBuffer * rb)
{
  GstDecklinkAudioSinkRingBuffer *self =
      GST_DECKLINK_AUDIO_SINK_RING_BUFFER_CAST (rb);
  guint ret = 0;
  HRESULT res = S_OK;

  if (self->output) {
    if ((res =
            self->output->output->GetBufferedAudioSampleFrameCount (&ret)) !=
        S_OK)
      ret = 0;
  }

  GST_DEBUG_OBJECT (self->sink, "Delay: %u (0x%08x)", ret, res);

  return ret;
}

#if 0
static gboolean
in_same_pipeline (GstElement * a, GstElement * b)
{
  GstObject *root = NULL, *tmp;
  gboolean ret = FALSE;

  tmp = gst_object_get_parent (GST_OBJECT_CAST (a));
  while (tmp != NULL) {
    if (root)
      gst_object_unref (root);
    root = tmp;
    tmp = gst_object_get_parent (root);
  }

  ret = root && gst_object_has_ancestor (GST_OBJECT_CAST (b), root);

  if (root)
    gst_object_unref (root);

  return ret;
}
#endif

static gboolean
gst_decklink_audio_sink_ringbuffer_start (GstAudioRingBuffer * rb)
{
  GstDecklinkAudioSinkRingBuffer *self =
      GST_DECKLINK_AUDIO_SINK_RING_BUFFER_CAST (rb);
  GstElement *videosink = NULL;
  gboolean ret = TRUE;

  // Check if there is a video sink for this output too and if it
  // is actually in the same pipeline
  g_mutex_lock (&self->output->lock);
  if (self->output->videosink)
    videosink = GST_ELEMENT_CAST (gst_object_ref (self->output->videosink));
  g_mutex_unlock (&self->output->lock);

  if (!videosink) {
    GST_ELEMENT_ERROR (self->sink, STREAM, FAILED,
        (NULL), ("Audio sink needs a video sink for its operation"));
    ret = FALSE;
  }
  // FIXME: This causes deadlocks sometimes  
#if 0
  else if (!in_same_pipeline (GST_ELEMENT_CAST (self->sink), videosink)) {
    GST_ELEMENT_ERROR (self->sink, STREAM, FAILED,
        (NULL), ("Audio sink and video sink need to be in the same pipeline"));
    ret = FALSE;
  }
#endif

  if (videosink)
    gst_object_unref (videosink);
  return ret;
}

static gboolean
gst_decklink_audio_sink_ringbuffer_pause (GstAudioRingBuffer * rb)
{
  return TRUE;
}

static gboolean
gst_decklink_audio_sink_ringbuffer_stop (GstAudioRingBuffer * rb)
{
  return TRUE;
}

static gboolean
gst_decklink_audio_sink_ringbuffer_acquire (GstAudioRingBuffer * rb,
    GstAudioRingBufferSpec * spec)
{
  GstDecklinkAudioSinkRingBuffer *self =
      GST_DECKLINK_AUDIO_SINK_RING_BUFFER_CAST (rb);
  HRESULT ret;
  BMDAudioSampleType sample_depth;

  GST_DEBUG_OBJECT (self->sink, "Acquire");

  if (spec->info.finfo->format == GST_AUDIO_FORMAT_S16LE) {
    sample_depth = bmdAudioSampleType16bitInteger;
  } else {
    sample_depth = bmdAudioSampleType32bitInteger;
  }

  ret = self->output->output->EnableAudioOutput (bmdAudioSampleRate48kHz,
      sample_depth, spec->info.channels, bmdAudioOutputStreamContinuous);
  if (ret != S_OK) {
    GST_WARNING_OBJECT (self->sink, "Failed to enable audio output 0x%08x",
        ret);
    return FALSE;
  }

  ret =
      self->output->
      output->SetAudioCallback (new GStreamerAudioOutputCallback (self));
  if (ret != S_OK) {
    GST_WARNING_OBJECT (self->sink,
        "Failed to set audio output callback 0x%08x", ret);
    return FALSE;
  }

  spec->segsize =
      (spec->latency_time * GST_AUDIO_INFO_RATE (&spec->info) /
      G_USEC_PER_SEC) * GST_AUDIO_INFO_BPF (&spec->info);
  spec->segtotal = spec->buffer_time / spec->latency_time;
  // set latency to one more segment as we need some headroom
  spec->seglatency = spec->segtotal + 1;

  rb->size = spec->segtotal * spec->segsize;
  rb->memory = (guint8 *) g_malloc0 (rb->size);

  return TRUE;
}

static gboolean
gst_decklink_audio_sink_ringbuffer_release (GstAudioRingBuffer * rb)
{
  GstDecklinkAudioSinkRingBuffer *self =
      GST_DECKLINK_AUDIO_SINK_RING_BUFFER_CAST (rb);

  GST_DEBUG_OBJECT (self->sink, "Release");

  if (self->output) {
    g_mutex_lock (&self->clock_id_lock);
    if (self->clock_id)
      gst_clock_id_unschedule (self->clock_id);
    g_mutex_unlock (&self->clock_id_lock);

    g_mutex_lock (&self->output->lock);
    self->output->audio_enabled = FALSE;
    if (self->output->start_scheduled_playback && self->output->videosink)
      self->output->start_scheduled_playback (self->output->videosink);
    g_mutex_unlock (&self->output->lock);

    self->output->output->DisableAudioOutput ();
  }
  // free the buffer
  g_free (rb->memory);
  rb->memory = NULL;

  return TRUE;
}

static gboolean
gst_decklink_audio_sink_ringbuffer_open_device (GstAudioRingBuffer * rb)
{
  GstDecklinkAudioSinkRingBuffer *self =
      GST_DECKLINK_AUDIO_SINK_RING_BUFFER_CAST (rb);

  GST_DEBUG_OBJECT (self->sink, "Open device");

  self->output =
      gst_decklink_acquire_nth_output (self->sink->device_number,
      GST_ELEMENT_CAST (self), TRUE);
  if (!self->output) {
    GST_ERROR_OBJECT (self, "Failed to acquire output");
    return FALSE;
  }

  gst_decklink_output_set_audio_clock (self->output,
      GST_AUDIO_BASE_SINK_CAST (self->sink)->provided_clock);

  return TRUE;
}

static gboolean
gst_decklink_audio_sink_ringbuffer_close_device (GstAudioRingBuffer * rb)
{
  GstDecklinkAudioSinkRingBuffer *self =
      GST_DECKLINK_AUDIO_SINK_RING_BUFFER_CAST (rb);

  GST_DEBUG_OBJECT (self->sink, "Close device");

  if (self->output) {
    gst_decklink_output_set_audio_clock (self->output, NULL);
    gst_decklink_release_nth_output (self->sink->device_number,
        GST_ELEMENT_CAST (self), TRUE);
    self->output = NULL;
  }

  return TRUE;
}

enum
{
  PROP_0,
  PROP_DEVICE_NUMBER
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS
    ("audio/x-raw, format={S16LE,S32LE}, channels={2, 8, 16}, rate=48000, "
        "layout=interleaved")
    );

static void gst_decklink_audio_sink_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_decklink_audio_sink_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_decklink_audio_sink_finalize (GObject * object);

static GstStateChangeReturn gst_decklink_audio_sink_change_state (GstElement *
    element, GstStateChange transition);
static GstCaps *gst_decklink_audio_sink_get_caps (GstBaseSink * bsink,
    GstCaps * filter);
static GstAudioRingBuffer
    * gst_decklink_audio_sink_create_ringbuffer (GstAudioBaseSink * absink);

#define parent_class gst_decklink_audio_sink_parent_class
G_DEFINE_TYPE (GstDecklinkAudioSink, gst_decklink_audio_sink,
    GST_TYPE_AUDIO_BASE_SINK);

static void
gst_decklink_audio_sink_class_init (GstDecklinkAudioSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSinkClass *basesink_class = GST_BASE_SINK_CLASS (klass);
  GstAudioBaseSinkClass *audiobasesink_class =
      GST_AUDIO_BASE_SINK_CLASS (klass);

  gobject_class->set_property = gst_decklink_audio_sink_set_property;
  gobject_class->get_property = gst_decklink_audio_sink_get_property;
  gobject_class->finalize = gst_decklink_audio_sink_finalize;

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_decklink_audio_sink_change_state);

  basesink_class->get_caps =
      GST_DEBUG_FUNCPTR (gst_decklink_audio_sink_get_caps);

  audiobasesink_class->create_ringbuffer =
      GST_DEBUG_FUNCPTR (gst_decklink_audio_sink_create_ringbuffer);

  g_object_class_install_property (gobject_class, PROP_DEVICE_NUMBER,
      g_param_spec_int ("device-number", "Device number",
          "Output device instance to use", 0, G_MAXINT, 0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              G_PARAM_CONSTRUCT)));

  gst_element_class_add_static_pad_template (element_class, &sink_template);

  gst_element_class_set_static_metadata (element_class, "Decklink Audio Sink",
      "Audio/Sink", "Decklink Sink", "David Schleef <ds@entropywave.com>, "
      "Sebastian Dröge <sebastian@centricular.com>");

  GST_DEBUG_CATEGORY_INIT (gst_decklink_audio_sink_debug, "decklinkaudiosink",
      0, "debug category for decklinkaudiosink element");
}

static void
gst_decklink_audio_sink_init (GstDecklinkAudioSink * self)
{
  self->device_number = 0;

  // 25.000ms latency time seems to be needed at least,
  // everything below can cause drop-outs
  // TODO: This is probably related to the video mode that
  // is selected, but not directly it seems. Choosing the
  // duration of a frame does not work.
  GST_AUDIO_BASE_SINK_CAST (self)->latency_time = 25000;
}

void
gst_decklink_audio_sink_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDecklinkAudioSink *self = GST_DECKLINK_AUDIO_SINK_CAST (object);

  switch (property_id) {
    case PROP_DEVICE_NUMBER:
      self->device_number = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_decklink_audio_sink_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstDecklinkAudioSink *self = GST_DECKLINK_AUDIO_SINK_CAST (object);

  switch (property_id) {
    case PROP_DEVICE_NUMBER:
      g_value_set_int (value, self->device_number);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_decklink_audio_sink_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstStateChangeReturn
gst_decklink_audio_sink_change_state (GstElement * element,
    GstStateChange transition)
{
  GstDecklinkAudioSink *self = GST_DECKLINK_AUDIO_SINK_CAST (element);
  GstDecklinkAudioSinkRingBuffer *buf =
      GST_DECKLINK_AUDIO_SINK_RING_BUFFER_CAST (GST_AUDIO_BASE_SINK_CAST
      (self)->ringbuffer);
  GstStateChangeReturn ret;

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      g_mutex_lock (&buf->output->lock);
      buf->output->audio_enabled = TRUE;
      if (buf->output->start_scheduled_playback && buf->output->videosink)
        buf->output->start_scheduled_playback (buf->output->videosink);
      g_mutex_unlock (&buf->output->lock);
      break;
    default:
      break;
  }

  return ret;
}

static GstCaps *
gst_decklink_audio_sink_get_caps (GstBaseSink * bsink, GstCaps * filter)
{
  GstDecklinkAudioSink *self = GST_DECKLINK_AUDIO_SINK_CAST (bsink);
  GstDecklinkAudioSinkRingBuffer *buf =
      GST_DECKLINK_AUDIO_SINK_RING_BUFFER_CAST (GST_AUDIO_BASE_SINK_CAST
      (self)->ringbuffer);
  GstCaps *caps = gst_pad_get_pad_template_caps (GST_BASE_SINK_PAD (bsink));

  if (buf) {
    GST_OBJECT_LOCK (buf);
    if (buf->output && buf->output->attributes) {
      int64_t max_channels = 0;
      HRESULT ret;
      GstStructure *s;
      GValue arr = G_VALUE_INIT;
      GValue v = G_VALUE_INIT;

      ret =
          buf->output->attributes->GetInt (BMDDeckLinkMaximumAudioChannels,
          &max_channels);
      /* 2 should always be supported */
      if (ret != S_OK) {
        max_channels = 2;
      }

      caps = gst_caps_make_writable (caps);
      s = gst_caps_get_structure (caps, 0);

      g_value_init (&arr, GST_TYPE_LIST);
      g_value_init (&v, G_TYPE_INT);
      if (max_channels >= 16) {
        g_value_set_int (&v, 16);
        gst_value_list_append_value (&arr, &v);
      }
      if (max_channels >= 8) {
        g_value_set_int (&v, 8);
        gst_value_list_append_value (&arr, &v);
      }
      g_value_set_int (&v, 2);
      gst_value_list_append_value (&arr, &v);

      gst_structure_set_value (s, "channels", &arr);
      g_value_unset (&v);
      g_value_unset (&arr);
    }
    GST_OBJECT_UNLOCK (buf);
  }

  if (filter) {
    GstCaps *intersection =
        gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    caps = intersection;
  }

  return caps;
}

static GstAudioRingBuffer *
gst_decklink_audio_sink_create_ringbuffer (GstAudioBaseSink * absink)
{
  GstAudioRingBuffer *ret;

  GST_DEBUG_OBJECT (absink, "Creating ringbuffer");

  ret =
      GST_AUDIO_RING_BUFFER_CAST (g_object_new
      (GST_TYPE_DECKLINK_AUDIO_SINK_RING_BUFFER, NULL));

  GST_DECKLINK_AUDIO_SINK_RING_BUFFER_CAST (ret)->sink =
      (GstDecklinkAudioSink *) gst_object_ref (absink);

  return ret;
}
