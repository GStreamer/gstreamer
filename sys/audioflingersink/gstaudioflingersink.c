/* GStreamer
 * Copyright (C) <2009> Prajnashi S <prajnashi@gmail.com>
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

/**
 * SECTION:element-audioflindersink
 *
 * This element lets you output sound using the Audio Flinger system in Android
 *
 * Note that you should almost always use generic audio conversion elements
 * like audioconvert and audioresample in front of an audiosink to make sure
 * your pipeline works under all circumstances (those conversion elements will
 * act in passthrough-mode if no conversion is necessary).
 */

#ifdef HAVE_CONFIG_H
//#include "config.h"
#endif
#include "gstaudioflingersink.h"
#include <utils/Log.h>



#define LOG_NDEBUG 0

#undef LOG_TAG
#define LOG_TAG "GstAudioFlingerSink"


#define DEFAULT_BUFFERTIME (500*GST_MSECOND) / (GST_USECOND)
#define DEFAULT_LATENCYTIME (50*GST_MSECOND) / (GST_USECOND)
#define DEFAULT_VOLUME 10.0
#define DEFAULT_MUTE FALSE
#define DEFAULT_EXPORT_SYSTEM_AUDIO_CLOCK TRUE

/*
 * PROPERTY_ID
 */
enum
{
  PROP_NULL,
  PROP_VOLUME,
  PROP_MUTE,
  PROP_AUDIO_SINK,
};

GST_DEBUG_CATEGORY_STATIC (audioflinger_debug);
#define GST_CAT_DEFAULT audioflinger_debug

/* elementfactory information */
static const GstElementDetails gst_audioflinger_sink_details =
GST_ELEMENT_DETAILS ("Audio Sink (AudioFlinger)",
    "Sink/Audio",
    "Output to android's AudioFlinger",
    "Prajnashi S <prajnashi@gmail.com>, "
    "Alessandro Decina <alessandro.decina@collabora.co.uk>");

#define GST_TYPE_ANDROID_AUDIORING_BUFFER        \
        (gst_android_audioringbuffer_get_type())
#define GST_ANDROID_AUDIORING_BUFFER(obj)        \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ANDROID_AUDIORING_BUFFER,GstAndroidAudioRingBuffer))
#define GST_ANDROID_AUDIORING_BUFFER_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ANDROID_AUDIORING_BUFFER,GstAndroidAudioRingBufferClass))
#define GST_ANDROID_AUDIORING_BUFFER_GET_CLASS(obj) \
        (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_ANDROID_AUDIORING_BUFFER, GstAndroidAudioRingBufferClass))
#define GST_ANDROID_AUDIORING_BUFFER_CAST(obj)        \
        ((GstAndroidAudioRingBuffer *)obj)
#define GST_IS_ANDROID_AUDIORING_BUFFER(obj)     \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ANDROID_AUDIORING_BUFFER))
#define GST_IS_ANDROID_AUDIORING_BUFFER_CLASS(klass)\
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ANDROID_AUDIORING_BUFFER))

typedef struct _GstAndroidAudioRingBuffer GstAndroidAudioRingBuffer;
typedef struct _GstAndroidAudioRingBufferClass GstAndroidAudioRingBufferClass;

#define GST_ANDROID_AUDIORING_BUFFER_GET_COND(buf) (((GstAndroidAudioRingBuffer *)buf)->cond)
#define GST_ANDROID_AUDIORING_BUFFER_WAIT(buf)     (g_cond_wait (GST_ANDROID_ANDROID_AUDIORING_BUFFER_GET_COND (buf), GST_OBJECT_GET_LOCK (buf)))
#define GST_ANDROID_AUDIORING_BUFFER_SIGNAL(buf)   (g_cond_signal (GST_ANDROID_ANDROID_AUDIORING_BUFFER_GET_COND (buf)))
#define GST_ANDROID_AUDIORING_BUFFER_BROADCAST(buf)(g_cond_broadcast (GST_ANDROID_ANDROID_AUDIORING_BUFFER_GET_COND (buf)))

struct _GstAndroidAudioRingBuffer
{
  GstRingBuffer object;

  gboolean running;
  gint queuedseg;

  GCond *cond;
};

struct _GstAndroidAudioRingBufferClass
{
  GstRingBufferClass parent_class;
};

static void
gst_android_audioringbuffer_class_init (GstAndroidAudioRingBufferClass * klass);
static void gst_android_audioringbuffer_init (GstAndroidAudioRingBuffer *
    ringbuffer, GstAndroidAudioRingBufferClass * klass);
static void gst_android_audioringbuffer_dispose (GObject * object);
static void gst_android_audioringbuffer_finalize (GObject * object);

static GstRingBufferClass *ring_parent_class = NULL;

static gboolean gst_android_audioringbuffer_open_device (GstRingBuffer * buf);
static gboolean gst_android_audioringbuffer_close_device (GstRingBuffer * buf);
static gboolean gst_android_audioringbuffer_acquire (GstRingBuffer * buf,
    GstRingBufferSpec * spec);
static gboolean gst_android_audioringbuffer_release (GstRingBuffer * buf);
static gboolean gst_android_audioringbuffer_start (GstRingBuffer * buf);
static gboolean gst_android_audioringbuffer_pause (GstRingBuffer * buf);
static gboolean gst_android_audioringbuffer_stop (GstRingBuffer * buf);
static gboolean gst_android_audioringbuffer_activate (GstRingBuffer * buf,
    gboolean active);
static void gst_android_audioringbuffer_clear (GstRingBuffer * buf);
static guint gst_android_audioringbuffer_commit (GstRingBuffer * buf,
    guint64 * sample, guchar * data, gint in_samples, gint out_samples,
    gint * accum);

static void gst_audioflinger_sink_base_init (gpointer g_class);
static void gst_audioflinger_sink_class_init (GstAudioFlingerSinkClass * klass);
static void gst_audioflinger_sink_init (GstAudioFlingerSink *
    audioflinger_sink);

static void gst_audioflinger_sink_dispose (GObject * object);
static void gst_audioflinger_sink_finalise (GObject * object);

static void gst_audioflinger_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_audioflinger_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static GstCaps *gst_audioflinger_sink_getcaps (GstBaseSink * bsink);

static gboolean gst_audioflinger_sink_open (GstAudioFlingerSink * asink);
static gboolean gst_audioflinger_sink_close (GstAudioFlingerSink * asink);
static gboolean gst_audioflinger_sink_prepare (GstAudioFlingerSink * asink,
    GstRingBufferSpec * spec);
static gboolean gst_audioflinger_sink_unprepare (GstAudioFlingerSink * asink);
static void gst_audioflinger_sink_reset (GstAudioFlingerSink * asink,
    gboolean create_clock);
static void gst_audioflinger_sink_set_mute (GstAudioFlingerSink *
    audioflinger_sink, gboolean mute);
static void gst_audioflinger_sink_set_volume (GstAudioFlingerSink *
    audioflinger_sink, float volume);
static gboolean gst_audioflinger_sink_event (GstBaseSink * bsink,
    GstEvent * event);
static GstRingBuffer *gst_audioflinger_sink_create_ringbuffer (GstBaseAudioSink
    * sink);
static GstClockTime gst_audioflinger_sink_get_time (GstClock * clock,
    gpointer user_data);
static GstFlowReturn gst_audioflinger_sink_preroll (GstBaseSink * bsink,
    GstBuffer * buffer);
static GstClockTime gst_audioflinger_sink_system_audio_clock_get_time (GstClock
    * clock, gpointer user_data);
static GstClock *gst_audioflinger_sink_provide_clock (GstElement * elem);
static GstStateChangeReturn gst_audioflinger_sink_change_state (GstElement *
    element, GstStateChange transition);

static GstStaticPadTemplate audioflingersink_sink_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) { " G_STRINGIFY (G_BYTE_ORDER) " }, "
        "signed = (boolean) { TRUE }, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, 2 ]; ")
    );

static GType
gst_android_audioringbuffer_get_type (void)
{
  static GType ringbuffer_type = 0;

  if (!ringbuffer_type) {
    static const GTypeInfo ringbuffer_info = {
      sizeof (GstAndroidAudioRingBufferClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_android_audioringbuffer_class_init,
      NULL,
      NULL,
      sizeof (GstAndroidAudioRingBuffer),
      0,
      (GInstanceInitFunc) gst_android_audioringbuffer_init,
      NULL
    };

    ringbuffer_type =
        g_type_register_static (GST_TYPE_RING_BUFFER,
        "GstAndroidAudioSinkRingBuffer", &ringbuffer_info, 0);
  }
  return ringbuffer_type;
}

static void
gst_android_audioringbuffer_class_init (GstAndroidAudioRingBufferClass * klass)
{
  GObjectClass *gobject_class;
  GstRingBufferClass *gstringbuffer_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstringbuffer_class = GST_RING_BUFFER_CLASS (klass);

  ring_parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = gst_android_audioringbuffer_dispose;
  gobject_class->finalize = gst_android_audioringbuffer_finalize;

  gstringbuffer_class->open_device =
      GST_DEBUG_FUNCPTR (gst_android_audioringbuffer_open_device);
  gstringbuffer_class->close_device =
      GST_DEBUG_FUNCPTR (gst_android_audioringbuffer_close_device);
  gstringbuffer_class->acquire =
      GST_DEBUG_FUNCPTR (gst_android_audioringbuffer_acquire);
  gstringbuffer_class->release =
      GST_DEBUG_FUNCPTR (gst_android_audioringbuffer_release);
  gstringbuffer_class->start =
      GST_DEBUG_FUNCPTR (gst_android_audioringbuffer_start);
  gstringbuffer_class->pause =
      GST_DEBUG_FUNCPTR (gst_android_audioringbuffer_pause);
  gstringbuffer_class->resume =
      GST_DEBUG_FUNCPTR (gst_android_audioringbuffer_start);
  gstringbuffer_class->stop =
      GST_DEBUG_FUNCPTR (gst_android_audioringbuffer_stop);
  gstringbuffer_class->clear_all =
      GST_DEBUG_FUNCPTR (gst_android_audioringbuffer_clear);
  gstringbuffer_class->commit =
      GST_DEBUG_FUNCPTR (gst_android_audioringbuffer_commit);

#if 0
  gstringbuffer_class->delay =
      GST_DEBUG_FUNCPTR (gst_android_audioringbuffer_delay);
#endif
  gstringbuffer_class->activate =
      GST_DEBUG_FUNCPTR (gst_android_audioringbuffer_activate);
}

static void
gst_android_audioringbuffer_init (G_GNUC_UNUSED GstAndroidAudioRingBuffer *
    ringbuffer, G_GNUC_UNUSED GstAndroidAudioRingBufferClass * g_class)
{
}

static void
gst_android_audioringbuffer_dispose (GObject * object)
{
  G_OBJECT_CLASS (ring_parent_class)->dispose (object);
}

static void
gst_android_audioringbuffer_finalize (GObject * object)
{
  G_OBJECT_CLASS (ring_parent_class)->finalize (object);
}

static gboolean
gst_android_audioringbuffer_open_device (GstRingBuffer * buf)
{
  GstAudioFlingerSink *sink;
  gboolean result = TRUE;
  LOGD (">gst_android_audioringbuffer_open_device");
  sink = GST_AUDIOFLINGERSINK (GST_OBJECT_PARENT (buf));
  result = gst_audioflinger_sink_open (sink);

  if (!result)
    goto could_not_open;

  return result;

could_not_open:
  {
    GST_DEBUG_OBJECT (sink, "could not open device");
    LOGE ("could not open device");
    return FALSE;
  }
}

static gboolean
gst_android_audioringbuffer_close_device (GstRingBuffer * buf)
{
  GstAudioFlingerSink *sink;
  gboolean result = TRUE;

  LOGD (">gst_android_audioringbuffer_close_device");

  sink = GST_AUDIOFLINGERSINK (GST_OBJECT_PARENT (buf));

  result = gst_audioflinger_sink_close (sink);

  if (!result)
    goto could_not_close;

  return result;

could_not_close:
  {
    GST_DEBUG_OBJECT (sink, "could not close device");
    LOGE ("could not close device");
    return FALSE;
  }
}

static gboolean
gst_android_audioringbuffer_acquire (GstRingBuffer * buf,
    GstRingBufferSpec * spec)
{
  GstAudioFlingerSink *sink;
  gboolean result = FALSE;

  LOGD (">gst_android_audioringbuffer_acquire");

  sink = GST_AUDIOFLINGERSINK (GST_OBJECT_PARENT (buf));

  result = gst_audioflinger_sink_prepare (sink, spec);

  if (!result)
    goto could_not_prepare;

  return TRUE;

  /* ERRORS */
could_not_prepare:
  {
    GST_DEBUG_OBJECT (sink, "could not prepare device");
    LOGE ("could not close device");
    return FALSE;
  }
}

static gboolean
gst_android_audioringbuffer_activate (G_GNUC_UNUSED GstRingBuffer * buf,
    G_GNUC_UNUSED gboolean active)
{
  return TRUE;
}

/* function is called with LOCK */
static gboolean
gst_android_audioringbuffer_release (GstRingBuffer * buf)
{
  GstAudioFlingerSink *sink;
  gboolean result = FALSE;
  LOGD (">gst_android_audioringbuffer_release");

  sink = GST_AUDIOFLINGERSINK (GST_OBJECT_PARENT (buf));

  result = gst_audioflinger_sink_unprepare (sink);

  if (!result)
    goto could_not_unprepare;

  GST_DEBUG_OBJECT (sink, "unprepared");
  LOGD ("unprepared");

  return result;

could_not_unprepare:
  {
    GST_DEBUG_OBJECT (sink, "could not unprepare device");
    LOGE ("could not unprepare device");
    return FALSE;
  }
}

static gboolean
gst_android_audioringbuffer_start (GstRingBuffer * buf)
{
  GstAudioFlingerSink *asink;
  GstAndroidAudioRingBuffer *abuf;

  abuf = GST_ANDROID_AUDIORING_BUFFER_CAST (buf);
  asink = GST_AUDIOFLINGERSINK (GST_OBJECT_PARENT (abuf));

  GST_INFO_OBJECT (buf, "starting ringbuffer");
  LOGD ("starting ringbuffer");

  audioflinger_device_start (asink->audioflinger_device);

  return TRUE;
}

static gboolean
gst_android_audioringbuffer_pause (GstRingBuffer * buf)
{
  GstAudioFlingerSink *asink;
  GstAndroidAudioRingBuffer *abuf;

  abuf = GST_ANDROID_AUDIORING_BUFFER_CAST (buf);
  asink = GST_AUDIOFLINGERSINK (GST_OBJECT_PARENT (abuf));

  GST_INFO_OBJECT (buf, "pausing ringbuffer");
  LOGD ("pausing ringbuffer");

  audioflinger_device_pause (asink->audioflinger_device);

  return TRUE;
}

static gboolean
gst_android_audioringbuffer_stop (GstRingBuffer * buf)
{
  GstAudioFlingerSink *asink;
  GstAndroidAudioRingBuffer *abuf;

  abuf = GST_ANDROID_AUDIORING_BUFFER_CAST (buf);
  asink = GST_AUDIOFLINGERSINK (GST_OBJECT_PARENT (abuf));

  GST_INFO_OBJECT (buf, "stopping ringbuffer");
  LOGD ("stopping ringbuffer");

  audioflinger_device_stop (asink->audioflinger_device);

  return TRUE;
}

#if 0
static guint
gst_android_audioringbuffer_delay (GstRingBuffer * buf)
{
  return 0;
}
#endif

static void
gst_android_audioringbuffer_clear (GstRingBuffer * buf)
{
  GstAudioFlingerSink *asink;
  GstAndroidAudioRingBuffer *abuf;

  abuf = GST_ANDROID_AUDIORING_BUFFER_CAST (buf);
  asink = GST_AUDIOFLINGERSINK (GST_OBJECT_PARENT (abuf));

  GST_INFO_OBJECT (buf, "clearing ringbuffer");
  LOGD ("clearing ringbuffer");

  if (asink->audioflinger_device == NULL)
    return;

  GST_INFO_OBJECT (asink, "resetting clock");
  gst_audio_clock_reset (GST_AUDIO_CLOCK (asink->audio_clock), 0);

  audioflinger_device_flush (asink->audioflinger_device);
}

#define FWD_SAMPLES(s,se,d,de)		 	\
G_STMT_START {					\
  /* no rate conversion */			\
  guint towrite = MIN (se + bps - s, de - d);	\
  /* simple copy */				\
  if (!skip)					\
    memcpy (d, s, towrite);			\
  in_samples -= towrite / bps;			\
  out_samples -= towrite / bps;			\
  s += towrite;					\
  GST_LOG ("copy %u bytes", towrite);		\
} G_STMT_END

/* in_samples >= out_samples, rate > 1.0 */
#define FWD_UP_SAMPLES(s,se,d,de) 	 	\
G_STMT_START {					\
  guint8 *sb = s, *db = d;			\
  while (s <= se && d < de) {			\
    if (!skip)					\
      memcpy (d, s, bps);			\
    s += bps;					\
    *accum += outr;				\
    if ((*accum << 1) >= inr) {			\
      *accum -= inr;				\
      d += bps;					\
    }						\
  }						\
  in_samples -= (s - sb)/bps;			\
  out_samples -= (d - db)/bps;			\
  GST_DEBUG ("fwd_up end %d/%d",*accum,*toprocess);	\
} G_STMT_END

/* out_samples > in_samples, for rates smaller than 1.0 */
#define FWD_DOWN_SAMPLES(s,se,d,de) 	 	\
G_STMT_START {					\
  guint8 *sb = s, *db = d;			\
  while (s <= se && d < de) {			\
    if (!skip)					\
      memcpy (d, s, bps);			\
    d += bps;					\
    *accum += inr;				\
    if ((*accum << 1) >= outr) {		\
      *accum -= outr;				\
      s += bps;					\
    }						\
  }						\
  in_samples -= (s - sb)/bps;			\
  out_samples -= (d - db)/bps;			\
  GST_DEBUG ("fwd_down end %d/%d",*accum,*toprocess);	\
} G_STMT_END

#define REV_UP_SAMPLES(s,se,d,de) 	 	\
G_STMT_START {					\
  guint8 *sb = se, *db = d;			\
  while (s <= se && d < de) {			\
    if (!skip)					\
      memcpy (d, se, bps);			\
    se -= bps;					\
    *accum += outr;				\
    while (d < de && (*accum << 1) >= inr) {	\
      *accum -= inr;				\
      d += bps;					\
    }						\
  }						\
  in_samples -= (sb - se)/bps;			\
  out_samples -= (d - db)/bps;			\
  GST_DEBUG ("rev_up end %d/%d",*accum,*toprocess);	\
} G_STMT_END

#define REV_DOWN_SAMPLES(s,se,d,de) 	 	\
G_STMT_START {					\
  guint8 *sb = se, *db = d;			\
  while (s <= se && d < de) {			\
    if (!skip)					\
      memcpy (d, se, bps);			\
    d += bps;					\
    *accum += inr;				\
    while (s <= se && (*accum << 1) >= outr) {	\
      *accum -= outr;				\
      se -= bps;				\
    }						\
  }						\
  in_samples -= (sb - se)/bps;			\
  out_samples -= (d - db)/bps;			\
  GST_DEBUG ("rev_down end %d/%d",*accum,*toprocess);	\
} G_STMT_END

static guint
gst_android_audioringbuffer_commit (GstRingBuffer * buf, guint64 * sample,
    guchar * data, gint in_samples, gint out_samples, gint * accum)
{
  GstBaseAudioSink *baseaudiosink;
  GstAudioFlingerSink *asink;
  GstAndroidAudioRingBuffer *abuf;
  guint result;
  guint8 *data_end;
  gboolean reverse;
  gint *toprocess;
  gint inr, outr, bps;
  guint bufsize;
  gboolean skip = FALSE;
  guint32 position;
  gboolean slaved;
  guint64 corrected_sample;
  gboolean sync;

  abuf = GST_ANDROID_AUDIORING_BUFFER_CAST (buf);
  asink = GST_AUDIOFLINGERSINK (GST_OBJECT_PARENT (abuf));
  baseaudiosink = GST_BASE_AUDIO_SINK (asink);
  sync = gst_base_sink_get_sync (GST_BASE_SINK_CAST (asink));

  GST_LOG_OBJECT (asink, "entering commit");

  /* make sure the ringbuffer is started */
  if (G_UNLIKELY (g_atomic_int_get (&buf->state) !=
          GST_RING_BUFFER_STATE_STARTED)) {
    /* see if we are allowed to start it */
    if (G_UNLIKELY (g_atomic_int_get (&buf->abidata.ABI.may_start) == FALSE))
      goto no_start;

    GST_LOG_OBJECT (buf, "start!");
    LOGD ("start!");
    if (!gst_ring_buffer_start (buf))
      goto start_failed;
  }

  slaved = GST_ELEMENT_CLOCK (baseaudiosink) != asink->exported_clock;
  if (asink->last_resync_sample == -1 ||
      (gint64) baseaudiosink->next_sample == -1) {
    if (slaved) {
      /* we're writing a discont buffer. Disable slaving for a while in order to
       * fill the initial buffer needed by the audio mixer thread. This avoids
       * some cases where audioflinger removes us from the list of active tracks
       * because we aren't writing enough data.
       */
      GST_INFO_OBJECT (asink, "no previous sample, now %" G_GINT64_FORMAT
          " disabling slaving", *sample);
      LOGD ("no previous sample, now %ld disabling slaving", *sample);

      asink->last_resync_sample = *sample;
      g_object_set (asink, "slave-method", GST_BASE_AUDIO_SINK_SLAVE_NONE,
          NULL);
      asink->slaving_disabled = TRUE;
    } else {
/* Trace displayed too much time : remove it
      GST_INFO_OBJECT (asink, "no previous sample but not slaved");
      LOGD("no previous sample but not slaved");
*/
    }
  }

  if (slaved && asink->slaving_disabled) {
    guint64 threshold;

    threshold = gst_util_uint64_scale_int (buf->spec.rate, 5, 1);
    threshold += asink->last_resync_sample;

    if (*sample >= threshold) {
      GST_INFO_OBJECT (asink, "last sync %" G_GINT64_FORMAT
          " reached sample %" G_GINT64_FORMAT ", enabling slaving",
          asink->last_resync_sample, *sample);
      g_object_set (asink, "slave-method", GST_BASE_AUDIO_SINK_SLAVE_SKEW,
          NULL);
      asink->slaving_disabled = FALSE;
    }
  }

  bps = buf->spec.bytes_per_sample;
  bufsize = buf->spec.segsize * buf->spec.segtotal;

  /* our toy resampler for trick modes */
  reverse = out_samples < 0;
  out_samples = ABS (out_samples);

  if (in_samples >= out_samples)
    toprocess = &in_samples;
  else
    toprocess = &out_samples;

  inr = in_samples - 1;
  outr = out_samples - 1;

  GST_LOG_OBJECT (asink, "in %d, out %d reverse %d sync %d", inr, outr,
      reverse, sync);

  /* data_end points to the last sample we have to write, not past it. This is
   * needed to properly handle reverse playback: it points to the last sample. */
  data_end = data + (bps * inr);

  while (*toprocess > 0) {
    if (sync) {
      size_t avail;
      guint towrite;
      gint err;
      guint8 *d, *d_end;
      gpointer buffer_handle;

      position = audioflinger_device_get_position (asink->audioflinger_device);
      avail = out_samples;
      buffer_handle = NULL;
      GST_LOG_OBJECT (asink, "calling obtain buffer, position %d"
          " offset %" G_GINT64_FORMAT " samples %" G_GSSIZE_FORMAT,
          position, *sample, avail);
      err = audioflinger_device_obtain_buffer (asink->audioflinger_device,
          &buffer_handle, (int8_t **) & d, &avail, *sample);
      GST_LOG_OBJECT (asink, "obtain buffer returned");
      if (err < 0) {
        GST_LOG_OBJECT (asink, "obtain buffer error %d, state %d",
            err, buf->state);
        LOGD ("obtain buffer error 0x%x, state %d", err, buf->state);

        if (err == LATE)
          skip = TRUE;
        else if (buf->state != GST_RING_BUFFER_STATE_STARTED)
          goto done;
        else
          goto obtain_buffer_failed;
      }

      towrite = avail * bps;
      d_end = d + towrite;

      GST_LOG_OBJECT (asink, "writing %u samples at offset %" G_GUINT64_FORMAT,
          (guint) avail, *sample);

      if (G_LIKELY (inr == outr && !reverse)) {
        FWD_SAMPLES (data, data_end, d, d_end);
      } else if (!reverse) {
        if (inr >= outr) {
          /* forward speed up */
          FWD_UP_SAMPLES (data, data_end, d, d_end);
        } else {
          /* forward slow down */
          FWD_DOWN_SAMPLES (data, data_end, d, d_end);
        }
      } else {
        if (inr >= outr)
          /* reverse speed up */
          REV_UP_SAMPLES (data, data_end, d, d_end);
        else
          /* reverse slow down */
          REV_DOWN_SAMPLES (data, data_end, d, d_end);
      }

      *sample += avail;

      if (buffer_handle)
        audioflinger_device_release_buffer (asink->audioflinger_device,
            buffer_handle);
    } else {
      gint written;

      written = audioflinger_device_write (asink->audioflinger_device, data,
          *toprocess * bps);
      if (written > 0) {
        *toprocess -= written / bps;
        data += written;
      } else {
        LOGE ("Error to write buffer(error=%d)", written);
        GST_LOG_OBJECT (asink, "Error to write buffer(error=%d)", written);
        goto start_failed;
      }
    }
  }
skip:
  /* we consumed all samples here */
  data = data_end + bps;

done:
  result = inr - ((data_end - data) / bps);
  GST_LOG_OBJECT (asink, "wrote %d samples", result);

  return result;

  /* ERRORS */
no_start:
  {
    GST_LOG_OBJECT (asink, "we can not start");
    LOGE ("we can not start");
    return 0;
  }
start_failed:
  {
    GST_LOG_OBJECT (asink, "failed to start the ringbuffer");
    LOGE ("failed to start the ringbuffer");
    return 0;
  }
obtain_buffer_failed:
  {
    GST_ELEMENT_ERROR (asink, RESOURCE, FAILED,
        ("obtain_buffer failed"), (NULL));
    LOGE ("obtain_buffer failed");
    return -1;
  }
}

static GstElementClass *parent_class = NULL;

GType
gst_audioflinger_sink_get_type (void)
{
  static GType audioflingersink_type = 0;

  if (!audioflingersink_type) {
    static const GTypeInfo audioflingersink_info = {
      sizeof (GstAudioFlingerSinkClass),
      gst_audioflinger_sink_base_init,
      NULL,
      (GClassInitFunc) gst_audioflinger_sink_class_init,
      NULL,
      NULL,
      sizeof (GstAudioFlingerSink),
      0,
      (GInstanceInitFunc) gst_audioflinger_sink_init,
    };

    audioflingersink_type =
        g_type_register_static (GST_TYPE_AUDIO_SINK, "GstAudioFlingerSink",
        &audioflingersink_info, 0);
  }

  return audioflingersink_type;
}

static void
gst_audioflinger_sink_dispose (GObject * object)
{
  GstAudioFlingerSink *audioflinger_sink = GST_AUDIOFLINGERSINK (object);

  if (audioflinger_sink->probed_caps) {
    gst_caps_unref (audioflinger_sink->probed_caps);
    audioflinger_sink->probed_caps = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_audioflinger_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_audioflinger_sink_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&audioflingersink_sink_factory));
  GST_DEBUG_CATEGORY_INIT (audioflinger_debug, "audioflingersink", 0,
      "audioflinger sink trace");
}

static void
gst_audioflinger_sink_class_init (GstAudioFlingerSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;
  GstBaseAudioSinkClass *gstbaseaudiosink_class;
  GstAudioSinkClass *gstaudiosink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;
  gstbaseaudiosink_class = (GstBaseAudioSinkClass *) klass;
  gstaudiosink_class = (GstAudioSinkClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_audioflinger_sink_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_audioflinger_sink_finalise);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_audioflinger_sink_get_property);
  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_audioflinger_sink_set_property);

  gstelement_class->provide_clock =
      GST_DEBUG_FUNCPTR (gst_audioflinger_sink_provide_clock);
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_audioflinger_sink_change_state);

  gstbasesink_class->get_caps =
      GST_DEBUG_FUNCPTR (gst_audioflinger_sink_getcaps);

  gstbaseaudiosink_class->create_ringbuffer =
      GST_DEBUG_FUNCPTR (gst_audioflinger_sink_create_ringbuffer);

  gstbasesink_class->event = GST_DEBUG_FUNCPTR (gst_audioflinger_sink_event);
  gstbasesink_class->preroll =
      GST_DEBUG_FUNCPTR (gst_audioflinger_sink_preroll);

  /* Install properties */
  g_object_class_install_property (gobject_class, PROP_MUTE,
      g_param_spec_boolean ("mute", "Mute",
          "Mute output", DEFAULT_MUTE, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_VOLUME,
      g_param_spec_double ("volume", "Volume",
          "control volume size", 0.0, 10.0, DEFAULT_VOLUME, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_AUDIO_SINK,
      g_param_spec_pointer ("audiosink", "AudioSink",
          "The pointer of MediaPlayerBase::AudioSink", G_PARAM_WRITABLE));
}

static void
gst_audioflinger_sink_init (GstAudioFlingerSink * audioflinger_sink)
{
  GST_DEBUG_OBJECT (audioflinger_sink, "initializing audioflinger_sink");
  LOGD ("initializing audioflinger_sink");

  audioflinger_sink->audio_clock = NULL;
  audioflinger_sink->system_clock = NULL;
  audioflinger_sink->system_audio_clock = NULL;
  audioflinger_sink->exported_clock = NULL;
  audioflinger_sink->export_system_audio_clock =
      DEFAULT_EXPORT_SYSTEM_AUDIO_CLOCK;
  gst_audioflinger_sink_reset (audioflinger_sink, TRUE);
}

static void
gst_audioflinger_sink_reset (GstAudioFlingerSink * sink, gboolean create_clocks)
{

  if (sink->audioflinger_device != NULL) {
    audioflinger_device_release (sink->audioflinger_device);
    sink->audioflinger_device = NULL;
  }

  sink->audioflinger_device = NULL;
  sink->m_volume = DEFAULT_VOLUME;
  sink->m_mute = DEFAULT_MUTE;
  sink->m_init = FALSE;
  sink->m_audiosink = NULL;
  sink->eos = FALSE;
  sink->may_provide_clock = TRUE;
  sink->last_resync_sample = -1;

  if (sink->system_clock) {
    GstClock *clock = sink->system_clock;

    GST_INFO_OBJECT (sink, "destroying system_clock %d",
        GST_OBJECT_REFCOUNT (sink->system_clock));
    gst_clock_set_master (sink->system_clock, NULL);
    gst_object_replace ((GstObject **) & sink->system_clock, NULL);
    GST_INFO_OBJECT (sink, "destroyed system_clock");
    GST_INFO_OBJECT (sink, "destroying system_audio_clock %d",
        GST_OBJECT_REFCOUNT (sink->system_audio_clock));
    gst_object_replace ((GstObject **) & sink->system_audio_clock, NULL);
    GST_INFO_OBJECT (sink, "destroyed system_audio_clock");
  }

  if (sink->audio_clock) {
    GST_INFO_OBJECT (sink, "destroying audio clock %d",
        GST_OBJECT_REFCOUNT (sink->audio_clock));

    gst_object_replace ((GstObject **) & sink->audio_clock, NULL);
  }

  if (sink->exported_clock) {
    GST_INFO_OBJECT (sink, "destroying exported clock %d",
        GST_OBJECT_REFCOUNT (sink->exported_clock));
    gst_object_replace ((GstObject **) & sink->exported_clock, NULL);
    GST_INFO_OBJECT (sink, "destroyed exported clock");
  }

  if (create_clocks) {
    GstClockTime external, internal;

    /* create the audio clock that uses the ringbuffer as its audio source */
    sink->audio_clock = gst_audio_clock_new ("GstAudioFlingerSinkClock",
        gst_audioflinger_sink_get_time, sink);

    /* always set audio_clock as baseaudiosink's provided_clock */
    gst_object_replace ((GstObject **) &
        GST_BASE_AUDIO_SINK (sink)->provided_clock,
        GST_OBJECT (sink->audio_clock));

    /* create the system_audio_clock, which is an *audio clock* that uses an
     * instance of the system clock as its time source */
    sink->system_audio_clock =
        gst_audio_clock_new ("GstAudioFlingerSystemAudioClock",
        gst_audioflinger_sink_system_audio_clock_get_time, sink);

    /* create an instance of the system clock, that we slave to
     * sink->audio_clock to have an audio clock with an higher resolution than
     * the segment size (50ms) */
    sink->system_clock = g_object_new (GST_TYPE_SYSTEM_CLOCK,
        "name", "GstAudioFlingerSystemClock", NULL);

    /* calibrate the clocks */
    external = gst_clock_get_time (sink->audio_clock);
    internal = gst_clock_get_internal_time (sink->system_clock);
    gst_clock_set_calibration (sink->system_clock, internal, external, 1, 1);

    /* slave the system clock to the audio clock */
    GST_OBJECT_FLAG_SET (sink->system_clock, GST_CLOCK_FLAG_CAN_SET_MASTER);
    g_object_set (sink->system_clock, "timeout", 50 * GST_MSECOND, NULL);
    gst_clock_set_master (sink->system_clock, sink->audio_clock);
  }

}

static void
gst_audioflinger_sink_finalise (GObject * object)
{
  GstAudioFlingerSink *audioflinger_sink = GST_AUDIOFLINGERSINK (object);

  GST_INFO_OBJECT (object, "finalize");

  gst_audioflinger_sink_reset (audioflinger_sink, FALSE);

  G_OBJECT_CLASS (parent_class)->finalize ((GObject *) (object));
}

static GstRingBuffer *
gst_audioflinger_sink_create_ringbuffer (GstBaseAudioSink * sink)
{
  GstRingBuffer *buffer;

  GST_DEBUG_OBJECT (sink, "creating ringbuffer");
  LOGD ("creating ringbuffer");
  buffer = g_object_new (GST_TYPE_ANDROID_AUDIORING_BUFFER, NULL);
  GST_DEBUG_OBJECT (sink, "created ringbuffer @%p", buffer);
  LOGD ("created ringbuffer @%p", buffer);

  return buffer;
}

static void
gst_audioflinger_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAudioFlingerSink *audioflinger_sink;

  audioflinger_sink = GST_AUDIOFLINGERSINK (object);
  g_return_if_fail (audioflinger_sink != NULL);

  switch (prop_id) {
    case PROP_MUTE:
      g_value_set_boolean (value, audioflinger_sink->m_mute);
      GST_DEBUG_OBJECT (audioflinger_sink, "get mute: %d",
          audioflinger_sink->m_mute);
      LOGD ("get mute: %d", audioflinger_sink->m_mute);
      break;
    case PROP_VOLUME:
      g_value_set_double (value, audioflinger_sink->m_volume);
      GST_DEBUG_OBJECT (audioflinger_sink, "get volume: %f",
          audioflinger_sink->m_volume);
      LOGD ("get volume: %f", audioflinger_sink->m_volume);
      break;
    case PROP_AUDIO_SINK:
      GST_ERROR_OBJECT (audioflinger_sink, "Shall not go here!");
      LOGD ("Shall not go here!");
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_audioflinger_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAudioFlingerSink *audioflinger_sink;
  audioflinger_sink = GST_AUDIOFLINGERSINK (object);

  g_return_if_fail (audioflinger_sink != NULL);
  GST_OBJECT_LOCK (audioflinger_sink);
  switch (prop_id) {
    case PROP_MUTE:
      audioflinger_sink->m_mute = g_value_get_boolean (value);
      GST_DEBUG_OBJECT (audioflinger_sink, "set mute: %d",
          audioflinger_sink->m_mute);
      LOGD ("set mute: %d", audioflinger_sink->m_mute);
      /* set device if it's initialized */
      if (audioflinger_sink->audioflinger_device && audioflinger_sink->m_init)
        gst_audioflinger_sink_set_mute (audioflinger_sink,
            (int) (audioflinger_sink->m_mute));
      break;
    case PROP_VOLUME:
      audioflinger_sink->m_volume = g_value_get_double (value);
      GST_DEBUG_OBJECT (audioflinger_sink, "set volume: %f",
          audioflinger_sink->m_volume);
      LOGD ("set volume: %f", audioflinger_sink->m_volume);
      /* set device if it's initialized */
      if (audioflinger_sink->audioflinger_device && audioflinger_sink->m_init)
        gst_audioflinger_sink_set_volume (audioflinger_sink,
            (float) audioflinger_sink->m_volume);
      break;
    case PROP_AUDIO_SINK:
      audioflinger_sink->m_audiosink = g_value_get_pointer (value);
      GST_DEBUG_OBJECT (audioflinger_sink, "set audiosink: %p",
          audioflinger_sink->m_audiosink);
      LOGD ("set audiosink: %p", audioflinger_sink->m_audiosink);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (audioflinger_sink);
}

static GstCaps *
gst_audioflinger_sink_getcaps (GstBaseSink * bsink)
{
  GstAudioFlingerSink *audioflinger_sink;
  GstCaps *caps;

  audioflinger_sink = GST_AUDIOFLINGERSINK (bsink);
  GST_DEBUG_OBJECT (audioflinger_sink, "enter,%p",
      audioflinger_sink->audioflinger_device);
  LOGD ("gst_audioflinger_sink_getcaps,%p",
      audioflinger_sink->audioflinger_device);
  if (audioflinger_sink->audioflinger_device == NULL
      || audioflinger_sink->m_init == FALSE) {
    caps =
        gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASE_SINK_PAD
            (bsink)));
  } else if (audioflinger_sink->probed_caps) {
    caps = gst_caps_copy (audioflinger_sink->probed_caps);
  } else {
    caps = gst_caps_new_any ();
    if (caps && !gst_caps_is_empty (caps)) {
      audioflinger_sink->probed_caps = gst_caps_copy (caps);
    }
  }

  return caps;
}

static gboolean
gst_audioflinger_sink_open (GstAudioFlingerSink * audioflinger)
{
  GstBaseAudioSink *baseaudiosink = (GstBaseAudioSink *) audioflinger;

  GST_DEBUG_OBJECT (audioflinger, "enter");
  LOGD ("gst_audioflinger_sink_open");
  g_return_val_if_fail (audioflinger != NULL, FALSE);

  baseaudiosink->buffer_time = DEFAULT_BUFFERTIME;
  baseaudiosink->latency_time = DEFAULT_LATENCYTIME;

  if (audioflinger->audioflinger_device == NULL) {
    if (audioflinger->m_audiosink) {
      if (!(audioflinger->audioflinger_device =
              audioflinger_device_open (audioflinger->m_audiosink)))
        goto failed_creation;
      GST_DEBUG_OBJECT (audioflinger, "open an existed flinger, %p",
          audioflinger->audioflinger_device);
      LOGD ("open an existed flinger, %p", audioflinger->audioflinger_device);
    } else {
      if (!(audioflinger->audioflinger_device = audioflinger_device_create ()))
        goto failed_creation;
      GST_DEBUG_OBJECT (audioflinger, "create a new flinger, %p",
          audioflinger->audioflinger_device);
      LOGD ("create a new flinger, %p", audioflinger->audioflinger_device);
    }
  }
  return TRUE;

  /* ERRORS */
failed_creation:
  {
    GST_ELEMENT_ERROR (audioflinger, RESOURCE, SETTINGS, (NULL),
        ("Failed to create AudioFlinger"));
    LOGE ("Failed to create AudioFlinger");
    return FALSE;
  }
}

static gboolean
gst_audioflinger_sink_close (GstAudioFlingerSink * audioflinger)
{
  GST_DEBUG_OBJECT (audioflinger, "enter");
  LOGD ("gst_audioflinger_sink_close");

  if (audioflinger->audioflinger_device != NULL) {
    GST_DEBUG_OBJECT (audioflinger, "release flinger device");
    LOGD ("release flinger device");
    audioflinger_device_stop (audioflinger->audioflinger_device);
    audioflinger_device_release (audioflinger->audioflinger_device);
    audioflinger->audioflinger_device = NULL;
  }
  return TRUE;
}

static gboolean
gst_audioflinger_sink_prepare (GstAudioFlingerSink * audioflinger,
    GstRingBufferSpec * spec)
{
  GST_DEBUG_OBJECT (audioflinger, "enter");
  LOGD ("gst_audioflinger_sink_prepare");

  /* FIXME: 
   * 
   * Pipeline crashes in audioflinger_device_set(), after releasing audio
   * flinger device and creating it again. In most cases, it will happen when
   * playing the same audio again.
   *
   * It seems the root cause is we create and release audio flinger sink in
   * different thread in playbin2. Till now, I haven't found way to
   * create/release device in the same thread. Fortunately, it will not effect
   * the gst-launch usage 
   */
  if (audioflinger_device_set (audioflinger->audioflinger_device,
          3, spec->channels, spec->rate, spec->segsize) == -1)
    goto failed_creation;

  audioflinger->m_init = TRUE;
//  gst_audioflinger_sink_set_volume (audioflinger, audioflinger->m_volume);
//  gst_audioflinger_sink_set_mute (audioflinger, audioflinger->m_mute);
  spec->bytes_per_sample = (spec->width / 8) * spec->channels;
  audioflinger->bytes_per_sample = spec->bytes_per_sample;

  spec->segsize =
      audioflinger_device_frameCount (audioflinger->audioflinger_device);

  GST_DEBUG_OBJECT (audioflinger,
      "channels: %d, rate: %d, width: %d, got segsize: %d, segtotal: %d, "
      "frame count: %d, frame size: %d",
      spec->channels, spec->rate, spec->width, spec->segsize, spec->segtotal,
      audioflinger_device_frameCount (audioflinger->audioflinger_device),
      audioflinger_device_frameSize (audioflinger->audioflinger_device)
      );
  LOGD ("channels: %d, rate: %d, width: %d, got segsize: %d, segtotal: %d, "
      "frame count: %d, frame size: %d",
      spec->channels, spec->rate, spec->width, spec->segsize, spec->segtotal,
      audioflinger_device_frameCount (audioflinger->audioflinger_device),
      audioflinger_device_frameSize (audioflinger->audioflinger_device)
      );

#if 0
  GST_DEBUG_OBJECT (audioflinger, "pause device");
  LOGD ("pause device");
  audioflinger_device_pause (audioflinger->audioflinger_device);
#endif

  return TRUE;

  /* ERRORS */
failed_creation:
  {
    GST_ELEMENT_ERROR (audioflinger, RESOURCE, SETTINGS, (NULL),
        ("Failed to create AudioFlinger for format %d", spec->format));
    LOGE ("Failed to create AudioFlinger for format %d", spec->format);
    return FALSE;
  }
dodgy_width:
  {
    GST_ELEMENT_ERROR (audioflinger, RESOURCE, SETTINGS, (NULL),
        ("Unhandled width %d", spec->width));
    LOGE ("Unhandled width %d", spec->width);
    return FALSE;
  }
}

static gboolean
gst_audioflinger_sink_unprepare (GstAudioFlingerSink * audioflinger)
{
  GST_DEBUG_OBJECT (audioflinger, "enter");
  LOGD ("gst_audioflinger_sink_unprepare");

  if (audioflinger->audioflinger_device != NULL) {
    GST_DEBUG_OBJECT (audioflinger, "release flinger device");
    LOGD ("release flinger device");
    audioflinger_device_stop (audioflinger->audioflinger_device);
    audioflinger->m_init = FALSE;
  }

  return TRUE;
}

static void
gst_audioflinger_sink_set_mute (GstAudioFlingerSink * audioflinger_sink,
    gboolean mute)
{
  GST_DEBUG_OBJECT (audioflinger_sink, "set PROP_MUTE = %d\n", mute);
  LOGD ("set PROP_MUTE = %d\n", mute);

  if (audioflinger_sink->audioflinger_device)
    audioflinger_device_mute (audioflinger_sink->audioflinger_device, mute);
  audioflinger_sink->m_mute = mute;
}

static void
gst_audioflinger_sink_set_volume (GstAudioFlingerSink * audioflinger_sink,
    float volume)
{
  GST_DEBUG_OBJECT (audioflinger_sink, "set PROP_VOLUME = %f\n", volume);
  LOGD ("set PROP_VOLUME = %f\n", volume);

  if (audioflinger_sink->audioflinger_device != NULL) {
    audioflinger_device_set_volume (audioflinger_sink->audioflinger_device,
        volume, volume);
  }
}

gboolean
gst_audioflinger_sink_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "audioflingersink", GST_RANK_PRIMARY,
      GST_TYPE_AUDIOFLINGERSINK);
}

/*
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, "audioflingersink",
    "audioflinger sink audio", plugin_init, VERSION, "LGPL", "GStreamer",
    "http://gstreamer.net/")
    */

static GstClock *
gst_audioflinger_sink_provide_clock (GstElement * elem)
{
  GstBaseAudioSink *sink;
  GstAudioFlingerSink *asink;
  GstClock *clock;

  sink = GST_BASE_AUDIO_SINK (elem);
  asink = GST_AUDIOFLINGERSINK (elem);

  /* we have no ringbuffer (must be NULL state) */
  if (sink->ringbuffer == NULL)
    goto wrong_state;

  if (!gst_ring_buffer_is_acquired (sink->ringbuffer))
    goto wrong_state;

  GST_OBJECT_LOCK (sink);
  if (!asink->may_provide_clock)
    goto already_playing;

  if (!sink->provide_clock)
    goto clock_disabled;

  clock = GST_CLOCK_CAST (gst_object_ref (asink->exported_clock));
  GST_INFO_OBJECT (asink, "providing clock %p %s", clock,
      clock == NULL ? NULL : GST_OBJECT_NAME (clock));
  GST_OBJECT_UNLOCK (sink);

  return clock;

  /* ERRORS */
wrong_state:
  {
    GST_DEBUG_OBJECT (sink, "ringbuffer not acquired");
    LOGD ("ringbuffer not acquired");
    return NULL;
  }
already_playing:
  {
    GST_INFO_OBJECT (sink, "we went to playing already");
    GST_OBJECT_UNLOCK (sink);
    return NULL;
  }
clock_disabled:
  {
    GST_DEBUG_OBJECT (sink, "clock provide disabled");
    LOGD ("clock provide disabled");
    GST_OBJECT_UNLOCK (sink);
    return NULL;
  }
}

static GstStateChangeReturn
gst_audioflinger_sink_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstClockTime time;
  GstAudioFlingerSink *sink = GST_AUDIOFLINGERSINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      sink->may_provide_clock = FALSE;
      if (sink->exported_clock == sink->system_audio_clock) {
        GstClockTime cinternal, cexternal, crate_num, crate_denom;

        /* take the slave lock to make sure that the slave_callback doesn't run
         * while we're moving sink->audio_clock forward, causing
         * sink->system_clock to jump as well */
        GST_CLOCK_SLAVE_LOCK (sink->system_clock);
        gst_clock_get_calibration (sink->audio_clock, NULL, NULL,
            &crate_num, &crate_denom);
        cinternal = gst_clock_get_internal_time (sink->audio_clock);
        cexternal = gst_clock_get_time (GST_ELEMENT_CLOCK (sink));
        gst_clock_set_calibration (sink->audio_clock, cinternal, cexternal,
            crate_num, crate_denom);
        /* reset observations */
        sink->system_clock->filling = TRUE;
        sink->system_clock->time_index = 0;
        GST_CLOCK_SLAVE_UNLOCK (sink->system_clock);

        time = gst_clock_get_time (sink->audio_clock);
        GST_INFO_OBJECT (sink, "PAUSED_TO_PLAYING,"
            " base_time %" GST_TIME_FORMAT
            " after %" GST_TIME_FORMAT
            " internal %" GST_TIME_FORMAT " external %" GST_TIME_FORMAT,
            GST_TIME_ARGS (GST_ELEMENT (sink)->base_time),
            GST_TIME_ARGS (time),
            GST_TIME_ARGS (cinternal), GST_TIME_ARGS (cexternal));
      }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    default:
      break;
  }
  return ret;
}

static GstFlowReturn
gst_audioflinger_sink_preroll (GstBaseSink * bsink, GstBuffer * buffer)
{
  GstFlowReturn ret;
  gboolean us_live = FALSE;
  GstQuery *query;
  GstAudioFlingerSink *asink = GST_AUDIOFLINGERSINK (bsink);
  GstBaseAudioSink *baseaudiosink = GST_BASE_AUDIO_SINK (bsink);
  GstClock *clock;

  GST_INFO_OBJECT (bsink, "preroll");

  ret = GST_BASE_SINK_CLASS (parent_class)->preroll (bsink, buffer);
  if (ret != GST_FLOW_OK)
    goto done;

  if (asink->exported_clock != NULL) {
    GST_INFO_OBJECT (bsink, "clock already exported");
    goto done;
  }

  query = gst_query_new_latency ();

  /* ask the peer for the latency */
  if (gst_pad_peer_query (bsink->sinkpad, query)) {
    /* get upstream min and max latency */
    gst_query_parse_latency (query, &us_live, NULL, NULL);
    GST_INFO_OBJECT (bsink, "query result live: %d", us_live);
  } else {
    GST_WARNING_OBJECT (bsink, "latency query failed");
  }
  gst_query_unref (query);

  if (!us_live && asink->export_system_audio_clock) {
    clock = asink->system_audio_clock;
    /* set SLAVE_NONE so that baseaudiosink doesn't try to slave audio_clock to
     * system_audio_clock
     */
    g_object_set (asink, "slave-method", GST_BASE_AUDIO_SINK_SLAVE_NONE, NULL);
  } else {
    clock = asink->audio_clock;
  }

  GST_INFO_OBJECT (bsink, "using %s clock",
      clock == asink->audio_clock ? "audio" : "system_audio");
  gst_object_replace ((GstObject **) & asink->exported_clock,
      GST_OBJECT (clock));
  GST_OBJECT_UNLOCK (asink);

done:
  return ret;
}

static gboolean
gst_audioflinger_sink_event (GstBaseSink * bsink, GstEvent * event)
{
  GstAudioFlingerSink *asink = GST_AUDIOFLINGERSINK (bsink);
  GstBaseAudioSink *baseaudiosink = GST_BASE_AUDIO_SINK (bsink);
  GstRingBuffer *ringbuf = baseaudiosink->ringbuffer;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      GST_INFO_OBJECT (asink, "got EOS");
      asink->eos = TRUE;

      if (baseaudiosink->next_sample) {
        guint64 next_sample, sample;
        gint sps;
        GstFlowReturn ret;
        GstBuffer *buf;

        sps = ringbuf->spec.segsize / ringbuf->spec.bytes_per_sample;
        sample = baseaudiosink->next_sample;
        next_sample = baseaudiosink->next_sample / sps;
        if (next_sample < ringbuf->spec.segsize) {
          gint samples, out_samples, accum, size;
          GstClockTime timestamp, before, after;
          guchar *data, *data_start;
          gint64 drift_tolerance;
          guint written;
          gint64 offset;

          samples = (ringbuf->spec.segsize - next_sample) * 4;

          size = samples * ringbuf->spec.bytes_per_sample;

          timestamp = gst_util_uint64_scale_int (baseaudiosink->next_sample,
              GST_SECOND, ringbuf->spec.rate);

          before = gst_clock_get_internal_time (asink->audio_clock);
          GST_INFO_OBJECT (asink, "%" G_GINT64_FORMAT " < %d, "
              "padding with silence, samples %d size %d ts %" GST_TIME_FORMAT,
              next_sample, ringbuf->spec.segsize, samples, size,
              GST_TIME_ARGS (timestamp));
          LOGD ("PADDING");

          data_start = data = g_malloc0 (size);
          offset = baseaudiosink->next_sample;
          out_samples = samples;

          GST_STATE_LOCK (bsink);
          do {
            written =
                gst_ring_buffer_commit_full (ringbuf, &offset, data, samples,
                out_samples, &accum);

            GST_DEBUG_OBJECT (bsink, "wrote %u of %u", written, samples);
            /* if we wrote all, we're done */
            if (written == samples)
              break;

            /* else something interrupted us and we wait for preroll. */
            if ((ret = gst_base_sink_wait_preroll (bsink)) != GST_FLOW_OK)
              break;

            /* update the output samples. FIXME, this will just skip them when pausing
             * during trick mode */
            if (out_samples > written) {
              out_samples -= written;
              accum = 0;
            } else
              break;

            samples -= written;
            data += written * ringbuf->spec.bytes_per_sample;
          } while (TRUE);


          GST_STATE_UNLOCK (bsink);

          g_free (data_start);
          after = gst_clock_get_internal_time (asink->audio_clock);

          GST_INFO_OBJECT (asink, "padded, left %d before %" GST_TIME_FORMAT
              " after %" GST_TIME_FORMAT, samples,
              GST_TIME_ARGS (before), GST_TIME_ARGS (after));


        } else {
          LOGD ("NOT PADDING 1");
        }
      } else {
        LOGD ("NOT PADDING 2");
      }

      break;
    case GST_EVENT_BUFFERING_START:
      GST_INFO_OBJECT (asink, "buffering start");
      break;
    case GST_EVENT_BUFFERING_STOP:
    {
      gboolean slaved;
      GstClockTime cinternal, cexternal, crate_num, crate_denom;
      GstClockTime before, after;

      gst_clock_get_calibration (asink->audio_clock, &cinternal, &cexternal,
          &crate_num, &crate_denom);

      before = gst_clock_get_time (asink->audio_clock);

      cinternal = gst_clock_get_internal_time (asink->audio_clock);
      cexternal = gst_clock_get_time (GST_ELEMENT_CLOCK (asink));
      gst_clock_set_calibration (asink->audio_clock, cinternal,
          cexternal, crate_num, crate_denom);

      after = gst_clock_get_time (asink->audio_clock);

      GST_INFO_OBJECT (asink, "buffering stopped, clock recalibrated"
          " before %" GST_TIME_FORMAT " after %" GST_TIME_FORMAT,
          GST_TIME_ARGS (before), GST_TIME_ARGS (after));

      /* force baseaudiosink to resync from the next buffer */
      GST_BASE_AUDIO_SINK (asink)->next_sample = -1;

      /* reset this so we allow some time before enabling slaving again */
      asink->last_resync_sample = -1;
      slaved = GST_ELEMENT_CLOCK (asink) != asink->exported_clock;
      if (slaved) {
        GST_INFO_OBJECT (asink, "disabling slaving");
        g_object_set (asink, "slave-method", GST_BASE_AUDIO_SINK_SLAVE_NONE,
            NULL);
        asink->slaving_disabled = TRUE;
      }

      g_object_set (asink, "drift-tolerance", 200 * GST_MSECOND, NULL);
      break;
    }
    default:
      break;
  }

  return GST_BASE_SINK_CLASS (parent_class)->event (bsink, event);
}

static GstClockTime
gst_audioflinger_sink_get_time (GstClock * clock, gpointer user_data)
{
  GstBaseAudioSink *sink = GST_BASE_AUDIO_SINK (user_data);
  uint32_t position = -1;
  GstAudioFlingerSink *asink = GST_AUDIOFLINGERSINK (sink);
  GstClockTime time = GST_CLOCK_TIME_NONE;
  GstClockTime ptime = GST_CLOCK_TIME_NONE;
  GstClockTime system_audio_clock_time = GST_CLOCK_TIME_NONE;
  GstClockTime offset = GST_CLOCK_TIME_NONE;
  GstClockTime adjusted_time = GST_CLOCK_TIME_NONE;
  GstClockTime cinternal, cexternal, crate_num, crate_denom;

  gst_clock_get_calibration (clock, &cinternal, &cexternal,
      &crate_num, &crate_denom);

  if (!asink->audioflinger_device || !asink->m_init) {
    GST_DEBUG_OBJECT (sink, "device not created yet");

    goto out;
  }

  if (!asink->audioflinger_device || !asink->m_init) {
    GST_DEBUG_OBJECT (sink, "device not created yet");

    goto out;
  }

  if (!sink->ringbuffer) {
    GST_DEBUG_OBJECT (sink, "NULL ringbuffer");

    goto out;
  }

  if (!sink->ringbuffer->acquired) {
    GST_DEBUG_OBJECT (sink, "ringbuffer not acquired");

    goto out;
  }

  position = audioflinger_device_get_position (asink->audioflinger_device);
  if (position == -1)
    goto out;

  time = gst_util_uint64_scale_int (position, GST_SECOND,
      sink->ringbuffer->spec.rate);

  offset = gst_audio_clock_adjust (GST_CLOCK (clock), 0);
  adjusted_time = gst_audio_clock_adjust (GST_CLOCK (clock), time);

  if (asink->system_audio_clock)
    system_audio_clock_time = gst_clock_get_time (asink->system_audio_clock);

  if (GST_ELEMENT_CLOCK (asink)
      && asink->audio_clock != GST_ELEMENT_CLOCK (asink))
    ptime = gst_clock_get_time (GST_ELEMENT_CLOCK (asink));

out:
  GST_DEBUG_OBJECT (sink,
      "clock %s processed samples %" G_GINT32_FORMAT " offset %" GST_TIME_FORMAT
      " time %" GST_TIME_FORMAT " pipeline time %" GST_TIME_FORMAT
      " system audio clock %" GST_TIME_FORMAT " adjusted_time %" GST_TIME_FORMAT
      " cinternal %" GST_TIME_FORMAT " cexternal %" GST_TIME_FORMAT,
      GST_OBJECT_NAME (clock), position, GST_TIME_ARGS (offset),
      GST_TIME_ARGS (time), GST_TIME_ARGS (ptime),
      GST_TIME_ARGS (system_audio_clock_time), GST_TIME_ARGS (adjusted_time),
      GST_TIME_ARGS (cinternal), GST_TIME_ARGS (cexternal));

  return time;
}

static GstClockTime
gst_audioflinger_sink_system_audio_clock_get_time (GstClock * clock,
    gpointer user_data)
{
  GstClockTime time, offset;
  GstAudioFlingerSink *sink = GST_AUDIOFLINGERSINK (user_data);

  time = gst_clock_get_time (sink->system_clock);
  offset = gst_audio_clock_adjust (clock, (GstClockTime) 0);
  time -= offset;

  return time;
}
