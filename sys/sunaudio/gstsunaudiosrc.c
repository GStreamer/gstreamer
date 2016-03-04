/*
 * GStreamer - SunAudio source
 * Copyright (C) 2005,2006 Sun Microsystems, Inc.,
 *               Brian Cameron <brian.cameron@sun.com>
 *
 * gstsunaudiosrc.c: 
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
 * SECTION:element-sunaudiosrc
 *
 * sunaudiosrc is an audio source designed to work with the Sun Audio
 * interface available in Solaris.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 sunaudiosrc ! wavenc ! filesink location=audio.wav
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stropts.h>
#include <sys/mixer.h>

#include "gstsunaudiosrc.h"

GST_DEBUG_CATEGORY_EXTERN (sunaudio_debug);
#define GST_CAT_DEFAULT sunaudio_debug

static void gst_sunaudiosrc_base_init (gpointer g_class);
static void gst_sunaudiosrc_class_init (GstSunAudioSrcClass * klass);
static void gst_sunaudiosrc_init (GstSunAudioSrc * sunaudiosrc,
    GstSunAudioSrcClass * g_class);
static void gst_sunaudiosrc_dispose (GObject * object);

static void gst_sunaudiosrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_sunaudiosrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstCaps *gst_sunaudiosrc_getcaps (GstBaseSrc * bsrc);

static gboolean gst_sunaudiosrc_open (GstAudioSrc * asrc);
static gboolean gst_sunaudiosrc_close (GstAudioSrc * asrc);
static gboolean gst_sunaudiosrc_prepare (GstAudioSrc * asrc,
    GstRingBufferSpec * spec);
static gboolean gst_sunaudiosrc_unprepare (GstAudioSrc * asrc);
static guint gst_sunaudiosrc_read (GstAudioSrc * asrc, gpointer data,
    guint length);
static guint gst_sunaudiosrc_delay (GstAudioSrc * asrc);
static void gst_sunaudiosrc_reset (GstAudioSrc * asrc);

#define DEFAULT_DEVICE          "/dev/audio"

enum
{
  PROP_0,
  PROP_DEVICE
};

GST_BOILERPLATE_WITH_INTERFACE (GstSunAudioSrc, gst_sunaudiosrc,
    GstAudioSrc, GST_TYPE_AUDIO_SRC, GstMixer, GST_TYPE_MIXER, gst_sunaudiosrc);

GST_IMPLEMENT_SUNAUDIO_MIXER_CTRL_METHODS (GstSunAudioSrc, gst_sunaudiosrc);

static GstStaticPadTemplate gst_sunaudiosrc_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) TRUE, " "width = (int) 16, " "depth = (int) 16, "
        /* [5510,48000] seems to be a Solaris limit */
        "rate = (int) [ 5510, 48000 ], " "channels = (int) [ 1, 2 ]")
    );

static void
gst_sunaudiosrc_dispose (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_sunaudiosrc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &gst_sunaudiosrc_factory);
  gst_element_class_set_static_metadata (element_class, "Sun Audio Source",
      "Source/Audio", "Audio source for Sun Audio devices",
      "Brian Cameron <brian.cameron@sun.com>");
}

static void
gst_sunaudiosrc_class_init (GstSunAudioSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstBaseAudioSrcClass *gstbaseaudiosrc_class;
  GstAudioSrcClass *gstaudiosrc_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstbaseaudiosrc_class = (GstBaseAudioSrcClass *) klass;
  gstaudiosrc_class = (GstAudioSrcClass *) klass;

  gobject_class->dispose = gst_sunaudiosrc_dispose;
  gobject_class->get_property = gst_sunaudiosrc_get_property;
  gobject_class->set_property = gst_sunaudiosrc_set_property;

  gstbasesrc_class->get_caps = GST_DEBUG_FUNCPTR (gst_sunaudiosrc_getcaps);

  gstaudiosrc_class->open = GST_DEBUG_FUNCPTR (gst_sunaudiosrc_open);
  gstaudiosrc_class->prepare = GST_DEBUG_FUNCPTR (gst_sunaudiosrc_prepare);
  gstaudiosrc_class->unprepare = GST_DEBUG_FUNCPTR (gst_sunaudiosrc_unprepare);
  gstaudiosrc_class->close = GST_DEBUG_FUNCPTR (gst_sunaudiosrc_close);
  gstaudiosrc_class->read = GST_DEBUG_FUNCPTR (gst_sunaudiosrc_read);
  gstaudiosrc_class->delay = GST_DEBUG_FUNCPTR (gst_sunaudiosrc_delay);
  gstaudiosrc_class->reset = GST_DEBUG_FUNCPTR (gst_sunaudiosrc_reset);

  g_object_class_install_property (gobject_class, PROP_DEVICE,
      g_param_spec_string ("device", "Device",
          "SunAudio device (usually /dev/audio)", DEFAULT_DEVICE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_sunaudiosrc_init (GstSunAudioSrc * sunaudiosrc,
    GstSunAudioSrcClass * g_class)
{
  const char *audiodev;

  GST_DEBUG_OBJECT (sunaudiosrc, "initializing sunaudiosrc");

  sunaudiosrc->fd = -1;

  audiodev = g_getenv ("AUDIODEV");
  if (audiodev == NULL)
    audiodev = DEFAULT_DEVICE;
  sunaudiosrc->device = g_strdup (audiodev);
}

static void
gst_sunaudiosrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSunAudioSrc *sunaudiosrc;

  sunaudiosrc = GST_SUNAUDIO_SRC (object);

  switch (prop_id) {
    case PROP_DEVICE:
      g_free (sunaudiosrc->device);
      sunaudiosrc->device = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_sunaudiosrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSunAudioSrc *sunaudiosrc;

  sunaudiosrc = GST_SUNAUDIO_SRC (object);

  switch (prop_id) {
    case PROP_DEVICE:
      g_value_set_string (value, sunaudiosrc->device);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstCaps *
gst_sunaudiosrc_getcaps (GstBaseSrc * bsrc)
{
  GstPadTemplate *pad_template;
  GstCaps *caps = NULL;
  GstSunAudioSrc *sunaudiosrc = GST_SUNAUDIO_SRC (bsrc);

  GST_DEBUG_OBJECT (sunaudiosrc, "getcaps called");

  pad_template = gst_static_pad_template_get (&gst_sunaudiosrc_factory);
  caps = gst_caps_copy (gst_pad_template_get_caps (pad_template));

  gst_object_unref (pad_template);

  return caps;
}

static gboolean
gst_sunaudiosrc_open (GstAudioSrc * asrc)
{
  GstSunAudioSrc *sunaudiosrc = GST_SUNAUDIO_SRC (asrc);
  int fd, ret;

  fd = open (sunaudiosrc->device, O_RDONLY);

  if (fd == -1) {
    GST_ELEMENT_ERROR (sunaudiosrc, RESOURCE, OPEN_READ, (NULL),
        ("can't open connection to Sun Audio device %s", sunaudiosrc->device));

    return FALSE;
  }

  sunaudiosrc->fd = fd;

  ret = ioctl (fd, AUDIO_GETDEV, &sunaudiosrc->dev);
  if (ret == -1) {
    GST_ELEMENT_ERROR (sunaudiosrc, RESOURCE, SETTINGS, (NULL), ("%s",
            strerror (errno)));
    return FALSE;
  }

  GST_DEBUG_OBJECT (sunaudiosrc, "name %s", sunaudiosrc->dev.name);
  GST_DEBUG_OBJECT (sunaudiosrc, "version %s", sunaudiosrc->dev.version);
  GST_DEBUG_OBJECT (sunaudiosrc, "config %s", sunaudiosrc->dev.config);

  ret = ioctl (fd, AUDIO_GETINFO, &sunaudiosrc->info);
  if (ret == -1) {
    GST_ELEMENT_ERROR (sunaudiosrc, RESOURCE, SETTINGS, (NULL), ("%s",
            strerror (errno)));
    return FALSE;
  }

  GST_DEBUG_OBJECT (sunaudiosrc, "monitor_gain %d",
      sunaudiosrc->info.monitor_gain);
  GST_DEBUG_OBJECT (sunaudiosrc, "output_muted %d",
      sunaudiosrc->info.output_muted);
  GST_DEBUG_OBJECT (sunaudiosrc, "hw_features %08x",
      sunaudiosrc->info.hw_features);
  GST_DEBUG_OBJECT (sunaudiosrc, "sw_features %08x",
      sunaudiosrc->info.sw_features);
  GST_DEBUG_OBJECT (sunaudiosrc, "sw_features_enabled %08x",
      sunaudiosrc->info.sw_features_enabled);

  if (!sunaudiosrc->mixer) {
    const char *audiodev;

    audiodev = g_getenv ("AUDIODEV");
    if (audiodev == NULL) {
      sunaudiosrc->mixer = gst_sunaudiomixer_ctrl_new ("/dev/audioctl");
    } else {
      gchar *device = g_strdup_printf ("%sctl", audiodev);

      sunaudiosrc->mixer = gst_sunaudiomixer_ctrl_new (device);
      g_free (device);
    }
  }

  return TRUE;
}

static gboolean
gst_sunaudiosrc_close (GstAudioSrc * asrc)
{
  GstSunAudioSrc *sunaudiosrc = GST_SUNAUDIO_SRC (asrc);

  close (sunaudiosrc->fd);
  sunaudiosrc->fd = -1;

  if (sunaudiosrc->mixer) {
    gst_sunaudiomixer_ctrl_free (sunaudiosrc->mixer);
    sunaudiosrc->mixer = NULL;
  }

  return TRUE;
}

static gboolean
gst_sunaudiosrc_prepare (GstAudioSrc * asrc, GstRingBufferSpec * spec)
{
  GstSunAudioSrc *sunaudiosrc = GST_SUNAUDIO_SRC (asrc);
  audio_info_t ainfo;
  int ret;
  GstSunAudioMixerCtrl *mixer;
  struct audio_info audioinfo;

  ret = ioctl (sunaudiosrc->fd, AUDIO_GETINFO, &ainfo);
  if (ret == -1) {
    GST_ELEMENT_ERROR (sunaudiosrc, RESOURCE, SETTINGS, (NULL), ("%s",
            strerror (errno)));
    return FALSE;
  }

  if (spec->width != 16)
    return FALSE;

  AUDIO_INITINFO (&ainfo);

  ainfo.record.sample_rate = spec->rate;
  ainfo.record.precision = spec->width;
  ainfo.record.channels = spec->channels;
  ainfo.record.encoding = AUDIO_ENCODING_LINEAR;
  ainfo.record.buffer_size = spec->buffer_time;

  mixer = sunaudiosrc->mixer;

  if (ioctl (mixer->mixer_fd, AUDIO_GETINFO, &audioinfo) < 0) {
    g_warning ("Error getting audio device volume");
  }
  ainfo.record.port = audioinfo.record.port;
  ainfo.record.gain = audioinfo.record.gain;
  ainfo.record.balance = audioinfo.record.balance;

  spec->segsize = 128;
  spec->segtotal = spec->buffer_time / 128;

  spec->silence_sample[0] = 0;
  spec->silence_sample[1] = 0;
  spec->silence_sample[2] = 0;
  spec->silence_sample[3] = 0;

  ret = ioctl (sunaudiosrc->fd, AUDIO_SETINFO, &ainfo);
  if (ret == -1) {
    GST_ELEMENT_ERROR (sunaudiosrc, RESOURCE, SETTINGS, (NULL), ("%s",
            strerror (errno)));
    return FALSE;
  }


  ioctl (sunaudiosrc->fd, I_FLUSH, FLUSHR);

  return TRUE;
}

static gboolean
gst_sunaudiosrc_unprepare (GstAudioSrc * asrc)
{
  return TRUE;
}

static guint
gst_sunaudiosrc_read (GstAudioSrc * asrc, gpointer data, guint length)
{
  return read (GST_SUNAUDIO_SRC (asrc)->fd, data, length);
}

static guint
gst_sunaudiosrc_delay (GstAudioSrc * asrc)
{
  return 0;
}

static void
gst_sunaudiosrc_reset (GstAudioSrc * asrc)
{
  /* Get current values */
  GstSunAudioSrc *sunaudiosrc = GST_SUNAUDIO_SRC (asrc);
  audio_info_t ainfo;
  int ret;

  ret = ioctl (sunaudiosrc->fd, AUDIO_GETINFO, &ainfo);
  if (ret == -1) {
    /*
     * Should never happen, but if we couldn't getinfo, then no point
     * trying to setinfo
     */
    GST_ELEMENT_ERROR (sunaudiosrc, RESOURCE, SETTINGS, (NULL), ("%s",
            strerror (errno)));
    return;
  }

  /*
   * Pause the audio - so audio stops playing immediately rather than
   * waiting for the ringbuffer to empty.
   */
  ainfo.record.pause = !NULL;
  ret = ioctl (sunaudiosrc->fd, AUDIO_SETINFO, &ainfo);
  if (ret == -1) {
    GST_ELEMENT_ERROR (sunaudiosrc, RESOURCE, SETTINGS, (NULL), ("%s",
            strerror (errno)));
  }

  /* Flush the audio */
  ret = ioctl (sunaudiosrc->fd, I_FLUSH, FLUSHR);
  if (ret == -1) {
    GST_ELEMENT_ERROR (sunaudiosrc, RESOURCE, SETTINGS, (NULL), ("%s",
            strerror (errno)));
  }

  /* unpause the audio */
  ainfo.record.pause = NULL;
  ret = ioctl (sunaudiosrc->fd, AUDIO_SETINFO, &ainfo);
  if (ret == -1) {
    GST_ELEMENT_ERROR (sunaudiosrc, RESOURCE, SETTINGS, (NULL), ("%s",
            strerror (errno)));
  }
}
