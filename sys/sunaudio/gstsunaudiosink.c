/*
 * GStreamer - SunAudio sink
 * Copyright (C) 2004 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2005,2006 Sun Microsystems, Inc.,
 *               Brian Cameron <brian.cameron@sun.com>
 * Copyright (C) 2006 Jan Schmidt <thaytan@mad.scientist.com>
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
 * SECTION:element-sunaudiosink
 *
 * sunaudiosink is an audio sink designed to work with the Sun Audio
 * interface available in Solaris.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 audiotestsrc volume=0.5 ! sunaudiosink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <fcntl.h>
#include <string.h>
#include <stropts.h>
#include <unistd.h>
#include <sys/mman.h>

#include "gstsunaudiosink.h"

GST_DEBUG_CATEGORY_EXTERN (sunaudio_debug);
#define GST_CAT_DEFAULT sunaudio_debug

static void gst_sunaudiosink_base_init (gpointer g_class);
static void gst_sunaudiosink_class_init (GstSunAudioSinkClass * klass);
static void gst_sunaudiosink_init (GstSunAudioSink * filter);
static void gst_sunaudiosink_dispose (GObject * object);
static void gst_sunaudiosink_finalize (GObject * object);

static void gst_sunaudiosink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_sunaudiosink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstCaps *gst_sunaudiosink_getcaps (GstBaseSink * bsink);

static gboolean gst_sunaudiosink_open (GstAudioSink * asink);
static gboolean gst_sunaudiosink_close (GstAudioSink * asink);
static gboolean gst_sunaudiosink_prepare (GstAudioSink * asink,
    GstRingBufferSpec * spec);
static gboolean gst_sunaudiosink_unprepare (GstAudioSink * asink);
static guint gst_sunaudiosink_write (GstAudioSink * asink, gpointer data,
    guint length);
static guint gst_sunaudiosink_delay (GstAudioSink * asink);
static void gst_sunaudiosink_reset (GstAudioSink * asink);

#define DEFAULT_DEVICE  "/dev/audio"
enum
{
  PROP_0,
  PROP_DEVICE,
};

static GstStaticPadTemplate gst_sunaudiosink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) TRUE, " "width = (int) 16, " "depth = (int) 16, "
        /* [5510,48000] seems to be a Solaris limit */
        "rate = (int) [ 5510, 48000 ], " "channels = (int) [ 1, 2 ]")
    );

static GstElementClass *parent_class = NULL;

GType
gst_sunaudiosink_get_type (void)
{
  static GType plugin_type = 0;

  if (!plugin_type) {
    static const GTypeInfo plugin_info = {
      sizeof (GstSunAudioSinkClass),
      gst_sunaudiosink_base_init,
      NULL,
      (GClassInitFunc) gst_sunaudiosink_class_init,
      NULL,
      NULL,
      sizeof (GstSunAudioSink),
      0,
      (GInstanceInitFunc) gst_sunaudiosink_init,
    };

    plugin_type = g_type_register_static (GST_TYPE_AUDIO_SINK,
        "GstSunAudioSink", &plugin_info, 0);
  }
  return plugin_type;
}

static void
gst_sunaudiosink_dispose (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_sunaudiosink_finalize (GObject * object)
{
  GstSunAudioSink *sunaudiosink = GST_SUNAUDIO_SINK (object);

  g_mutex_free (sunaudiosink->write_mutex);
  g_cond_free (sunaudiosink->sleep_cond);

  g_free (sunaudiosink->device);

  if (sunaudiosink->fd != -1) {
    close (sunaudiosink->fd);
    sunaudiosink->fd = -1;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_sunaudiosink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &gst_sunaudiosink_factory);
  gst_element_class_set_static_metadata (element_class, "Sun Audio Sink",
      "Sink/Audio", "Audio sink for Sun Audio devices",
      "David A. Schleef <ds@schleef.org>, "
      "Brian Cameron <brian.cameron@sun.com>");
}

static void
gst_sunaudiosink_class_init (GstSunAudioSinkClass * klass)
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

  gobject_class->dispose = gst_sunaudiosink_dispose;
  gobject_class->finalize = gst_sunaudiosink_finalize;

  gobject_class->set_property = gst_sunaudiosink_set_property;
  gobject_class->get_property = gst_sunaudiosink_get_property;

  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_sunaudiosink_getcaps);

  gstaudiosink_class->open = GST_DEBUG_FUNCPTR (gst_sunaudiosink_open);
  gstaudiosink_class->close = GST_DEBUG_FUNCPTR (gst_sunaudiosink_close);
  gstaudiosink_class->prepare = GST_DEBUG_FUNCPTR (gst_sunaudiosink_prepare);
  gstaudiosink_class->unprepare =
      GST_DEBUG_FUNCPTR (gst_sunaudiosink_unprepare);
  gstaudiosink_class->write = GST_DEBUG_FUNCPTR (gst_sunaudiosink_write);
  gstaudiosink_class->delay = GST_DEBUG_FUNCPTR (gst_sunaudiosink_delay);
  gstaudiosink_class->reset = GST_DEBUG_FUNCPTR (gst_sunaudiosink_reset);

  g_object_class_install_property (gobject_class, PROP_DEVICE,
      g_param_spec_string ("device", "Device", "Audio Device (/dev/audio)",
          DEFAULT_DEVICE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_sunaudiosink_init (GstSunAudioSink * sunaudiosink)
{
  const char *audiodev;

  GST_DEBUG_OBJECT (sunaudiosink, "initializing sunaudiosink");

  sunaudiosink->fd = -1;

  audiodev = g_getenv ("AUDIODEV");
  if (audiodev == NULL)
    audiodev = DEFAULT_DEVICE;
  sunaudiosink->device = g_strdup (audiodev);

  /* mutex and gcond used to control the write method */
  sunaudiosink->write_mutex = g_mutex_new ();
  sunaudiosink->sleep_cond = g_cond_new ();
}

static void
gst_sunaudiosink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSunAudioSink *sunaudiosink;

  sunaudiosink = GST_SUNAUDIO_SINK (object);

  switch (prop_id) {
    case PROP_DEVICE:
      GST_OBJECT_LOCK (sunaudiosink);
      g_free (sunaudiosink->device);
      sunaudiosink->device = g_strdup (g_value_get_string (value));
      GST_OBJECT_UNLOCK (sunaudiosink);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_sunaudiosink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSunAudioSink *sunaudiosink;

  sunaudiosink = GST_SUNAUDIO_SINK (object);

  switch (prop_id) {
    case PROP_DEVICE:
      GST_OBJECT_LOCK (sunaudiosink);
      g_value_set_string (value, sunaudiosink->device);
      GST_OBJECT_UNLOCK (sunaudiosink);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstCaps *
gst_sunaudiosink_getcaps (GstBaseSink * bsink)
{
  GstPadTemplate *pad_template;
  GstCaps *caps = NULL;
  GstSunAudioSink *sunaudiosink = GST_SUNAUDIO_SINK (bsink);

  GST_DEBUG_OBJECT (sunaudiosink, "getcaps called");

  pad_template = gst_static_pad_template_get (&gst_sunaudiosink_factory);
  caps = gst_caps_copy (gst_pad_template_get_caps (pad_template));

  gst_object_unref (pad_template);

  return caps;
}

static gboolean
gst_sunaudiosink_open (GstAudioSink * asink)
{
  GstSunAudioSink *sunaudiosink = GST_SUNAUDIO_SINK (asink);
  int fd, ret;

  /* First try to open non-blocking */
  GST_OBJECT_LOCK (sunaudiosink);
  fd = open (sunaudiosink->device, O_WRONLY | O_NONBLOCK);

  if (fd >= 0) {
    close (fd);
    fd = open (sunaudiosink->device, O_WRONLY);
  }

  if (fd == -1) {
    GST_OBJECT_UNLOCK (sunaudiosink);
    goto open_failed;
  }

  sunaudiosink->fd = fd;
  GST_OBJECT_UNLOCK (sunaudiosink);

  ret = ioctl (fd, AUDIO_GETDEV, &sunaudiosink->dev);
  if (ret == -1)
    goto ioctl_error;

  GST_DEBUG_OBJECT (sunaudiosink, "name %s", sunaudiosink->dev.name);
  GST_DEBUG_OBJECT (sunaudiosink, "version %s", sunaudiosink->dev.version);
  GST_DEBUG_OBJECT (sunaudiosink, "config %s", sunaudiosink->dev.config);

  ret = ioctl (fd, AUDIO_GETINFO, &sunaudiosink->info);
  if (ret == -1)
    goto ioctl_error;

  GST_DEBUG_OBJECT (sunaudiosink, "monitor_gain %d",
      sunaudiosink->info.monitor_gain);
  GST_DEBUG_OBJECT (sunaudiosink, "output_muted %d",
      sunaudiosink->info.output_muted);
  GST_DEBUG_OBJECT (sunaudiosink, "hw_features %08x",
      sunaudiosink->info.hw_features);
  GST_DEBUG_OBJECT (sunaudiosink, "sw_features %08x",
      sunaudiosink->info.sw_features);
  GST_DEBUG_OBJECT (sunaudiosink, "sw_features_enabled %08x",
      sunaudiosink->info.sw_features_enabled);

  return TRUE;

open_failed:
  GST_ELEMENT_ERROR (sunaudiosink, RESOURCE, OPEN_WRITE, (NULL),
      ("can't open connection to Sun Audio device %s", sunaudiosink->device));
  return FALSE;
ioctl_error:
  close (sunaudiosink->fd);
  GST_ELEMENT_ERROR (sunaudiosink, RESOURCE, SETTINGS, (NULL), ("%s",
          strerror (errno)));
  return FALSE;
}

static gboolean
gst_sunaudiosink_close (GstAudioSink * asink)
{
  GstSunAudioSink *sunaudiosink = GST_SUNAUDIO_SINK (asink);

  if (sunaudiosink->fd != -1) {
    close (sunaudiosink->fd);
    sunaudiosink->fd = -1;
  }
  return TRUE;
}

static gboolean
gst_sunaudiosink_prepare (GstAudioSink * asink, GstRingBufferSpec * spec)
{
  GstSunAudioSink *sunaudiosink = GST_SUNAUDIO_SINK (asink);
  audio_info_t ainfo;
  int ret;
  int ports;

  ret = ioctl (sunaudiosink->fd, AUDIO_GETINFO, &ainfo);
  if (ret == -1) {
    GST_ELEMENT_ERROR (sunaudiosink, RESOURCE, SETTINGS, (NULL), ("%s",
            strerror (errno)));
    return FALSE;
  }

  if (spec->width != 16)
    return FALSE;

  ports = ainfo.play.port;

  AUDIO_INITINFO (&ainfo);

  ainfo.play.sample_rate = spec->rate;
  ainfo.play.channels = spec->channels;
  ainfo.play.precision = spec->width;
  ainfo.play.encoding = AUDIO_ENCODING_LINEAR;
  ainfo.play.port = ports;

  /* buffer_time for playback is not implemented in Solaris at the moment,
     but at some point in the future, it might be */
  ainfo.play.buffer_size =
      gst_util_uint64_scale (spec->rate * spec->bytes_per_sample,
      spec->buffer_time, GST_SECOND / GST_USECOND);

  spec->silence_sample[0] = 0;
  spec->silence_sample[1] = 0;
  spec->silence_sample[2] = 0;
  spec->silence_sample[3] = 0;

  ret = ioctl (sunaudiosink->fd, AUDIO_SETINFO, &ainfo);
  if (ret == -1) {
    GST_ELEMENT_ERROR (sunaudiosink, RESOURCE, SETTINGS, (NULL), ("%s",
            strerror (errno)));
    return FALSE;
  }

  /* Now read back the info to find out the actual buffer size and set 
     segtotal */
  AUDIO_INITINFO (&ainfo);

  ret = ioctl (sunaudiosink->fd, AUDIO_GETINFO, &ainfo);
  if (ret == -1) {
    GST_ELEMENT_ERROR (sunaudiosink, RESOURCE, SETTINGS, (NULL), ("%s",
            strerror (errno)));
    return FALSE;
  }
#if 0
  /* We don't actually use the buffer_size from the sound device, because
   * it seems it's just bogus sometimes */
  sunaudiosink->segtotal = spec->segtotal =
      ainfo.play.buffer_size / spec->segsize;
#else
  sunaudiosink->segtotal = spec->segtotal;
#endif
  sunaudiosink->segtotal_samples =
      spec->segtotal * spec->segsize / spec->bytes_per_sample;

  sunaudiosink->segs_written = (gint) ainfo.play.eof;
  sunaudiosink->samples_written = ainfo.play.samples;
  sunaudiosink->bytes_per_sample = spec->bytes_per_sample;

  GST_DEBUG_OBJECT (sunaudiosink, "Got device buffer_size of %u",
      ainfo.play.buffer_size);

  return TRUE;
}

static gboolean
gst_sunaudiosink_unprepare (GstAudioSink * asink)
{
  return TRUE;
}

#define LOOP_WHILE_EINTR(v,func) do { (v) = (func); } \
		while ((v) == -1 && errno == EINTR);

/* Called with the write_mutex held */
static void
gst_sunaudio_sink_do_delay (GstSunAudioSink * sink)
{
  GstBaseAudioSink *ba_sink = GST_BASE_AUDIO_SINK (sink);
  GstClockTime total_sleep;
  GstClockTime max_sleep;
  gint sleep_usecs;
  GTimeVal sleep_end;
  gint err;
  audio_info_t ainfo;
  guint diff;

  /* This code below ensures that we don't race any further than buffer_time 
   * ahead of the audio output, by sleeping if the next write call would cause
   * us to advance too far in the ring-buffer */
  LOOP_WHILE_EINTR (err, ioctl (sink->fd, AUDIO_GETINFO, &ainfo));
  if (err < 0)
    goto write_error;

  /* Compute our offset from the output (copes with overflow) */
  diff = (guint) (sink->segs_written) - ainfo.play.eof;
  if (diff > sink->segtotal) {
    /* This implies that reset did a flush just as the sound device aquired
     * some buffers internally, and it causes us to be out of sync with the
     * eof measure. This corrects it */
    sink->segs_written = ainfo.play.eof;
    diff = 0;
  }

  if (diff + 1 < sink->segtotal)
    return;                     /* no need to sleep at all */

  /* Never sleep longer than the initial number of undrained segments in the 
     device plus one */
  total_sleep = 0;
  max_sleep = (diff + 1) * (ba_sink->latency_time * GST_USECOND);
  /* sleep for a segment period between .eof polls */
  sleep_usecs = ba_sink->latency_time;

  /* Current time is our reference point */
  g_get_current_time (&sleep_end);

  /* If the next segment would take us too far along the ring buffer,
   * sleep for a bit to free up a slot. If there were a way to find out
   * when the eof field actually increments, we could use, but the only
   * notification mechanism seems to be SIGPOLL, which we can't use from
   * a support library */
  while (diff + 1 >= sink->segtotal && total_sleep < max_sleep) {
    GST_LOG_OBJECT (sink, "need to block to drain segment(s). "
        "Sleeping for %d us", sleep_usecs);

    g_time_val_add (&sleep_end, sleep_usecs);

    if (g_cond_timed_wait (sink->sleep_cond, sink->write_mutex, &sleep_end)) {
      GST_LOG_OBJECT (sink, "Waking up early due to reset");
      return;                   /* Got told to wake up */
    }
    total_sleep += (sleep_usecs * GST_USECOND);

    LOOP_WHILE_EINTR (err, ioctl (sink->fd, AUDIO_GETINFO, &ainfo));
    if (err < 0)
      goto write_error;

    /* Compute our (new) offset from the output (copes with overflow) */
    diff = (guint) g_atomic_int_get (&sink->segs_written) - ainfo.play.eof;
  }

  return;

write_error:
  GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE, (NULL),
      ("Playback error on device '%s': %s", sink->device, strerror (errno)));
  return;
}

static guint
gst_sunaudiosink_write (GstAudioSink * asink, gpointer data, guint length)
{
  GstSunAudioSink *sink = GST_SUNAUDIO_SINK (asink);

  gint bytes_written, err;

  g_mutex_lock (sink->write_mutex);
  if (sink->flushing) {
    /* Exit immediately if reset tells us to */
    g_mutex_unlock (sink->write_mutex);
    return length;
  }

  LOOP_WHILE_EINTR (bytes_written, write (sink->fd, data, length));
  if (bytes_written < 0) {
    err = bytes_written;
    goto write_error;
  }

  /* Increment our sample counter, for delay calcs */
  g_atomic_int_add (&sink->samples_written, length / sink->bytes_per_sample);

  /* Don't consider the segment written if we didn't output the whole lot yet */
  if (bytes_written < length) {
    g_mutex_unlock (sink->write_mutex);
    return (guint) bytes_written;
  }

  /* Write a zero length output to trigger increment of the eof field */
  LOOP_WHILE_EINTR (err, write (sink->fd, NULL, 0));
  if (err < 0)
    goto write_error;

  /* Count this extra segment we've written */
  sink->segs_written += 1;

  /* Now delay so we don't overrun the ring buffer */
  gst_sunaudio_sink_do_delay (sink);

  g_mutex_unlock (sink->write_mutex);
  return length;

write_error:
  g_mutex_unlock (sink->write_mutex);

  GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE, (NULL),
      ("Playback error on device '%s': %s", sink->device, strerror (errno)));
  return length;                /* Say we wrote the segment to let the ringbuffer exit */
}

/*
 * Provide the current number of unplayed samples that have been written
 * to the device */
static guint
gst_sunaudiosink_delay (GstAudioSink * asink)
{
  GstSunAudioSink *sink = GST_SUNAUDIO_SINK (asink);
  audio_info_t ainfo;
  gint ret;
  guint offset;

  ret = ioctl (sink->fd, AUDIO_GETINFO, &ainfo);
  if (G_UNLIKELY (ret == -1))
    return 0;

  offset = (g_atomic_int_get (&sink->samples_written) - ainfo.play.samples);

  /* If the offset is larger than the total ringbuffer size, then we asked
     between the write call and when samples_written is updated */
  if (G_UNLIKELY (offset > sink->segtotal_samples))
    return 0;

  return offset;
}

static void
gst_sunaudiosink_reset (GstAudioSink * asink)
{
  /* Get current values */
  GstSunAudioSink *sunaudiosink = GST_SUNAUDIO_SINK (asink);
  audio_info_t ainfo;
  int ret;

  ret = ioctl (sunaudiosink->fd, AUDIO_GETINFO, &ainfo);
  if (ret == -1) {
    /*
     * Should never happen, but if we couldn't getinfo, then no point
     * trying to setinfo
     */
    GST_ELEMENT_ERROR (sunaudiosink, RESOURCE, SETTINGS, (NULL), ("%s",
            strerror (errno)));
    return;
  }

  /*
   * Pause the audio - so audio stops playing immediately rather than
   * waiting for the ringbuffer to empty.
   */
  ainfo.play.pause = !NULL;
  ret = ioctl (sunaudiosink->fd, AUDIO_SETINFO, &ainfo);
  if (ret == -1) {
    GST_ELEMENT_ERROR (sunaudiosink, RESOURCE, SETTINGS, (NULL), ("%s",
            strerror (errno)));
  }

  /* Flush the audio */
  ret = ioctl (sunaudiosink->fd, I_FLUSH, FLUSHW);
  if (ret == -1) {
    GST_ELEMENT_ERROR (sunaudiosink, RESOURCE, SETTINGS, (NULL), ("%s",
            strerror (errno)));
  }

  /* Now, we take the write_mutex and signal to ensure the write thread
   * is not busy, and we signal the condition to wake up any sleeper,
   * then we flush again in case the write wrote something after we flushed,
   * and finally release the lock and unpause */
  g_mutex_lock (sunaudiosink->write_mutex);
  sunaudiosink->flushing = TRUE;

  g_cond_signal (sunaudiosink->sleep_cond);

  ret = ioctl (sunaudiosink->fd, I_FLUSH, FLUSHW);
  if (ret == -1) {
    GST_ELEMENT_ERROR (sunaudiosink, RESOURCE, SETTINGS, (NULL), ("%s",
            strerror (errno)));
  }

  /* unpause the audio */
  ainfo.play.pause = NULL;
  ret = ioctl (sunaudiosink->fd, AUDIO_SETINFO, &ainfo);
  if (ret == -1) {
    GST_ELEMENT_ERROR (sunaudiosink, RESOURCE, SETTINGS, (NULL), ("%s",
            strerror (errno)));
  }

  /* After flushing the audio device, we need to remeasure the sample count
   * and segments written count so we're in sync with the device */

  sunaudiosink->segs_written = ainfo.play.eof;
  g_atomic_int_set (&sunaudiosink->samples_written, ainfo.play.samples);

  sunaudiosink->flushing = FALSE;
  g_mutex_unlock (sunaudiosink->write_mutex);
}
