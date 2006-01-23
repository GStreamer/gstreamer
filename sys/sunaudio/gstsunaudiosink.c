/*
 * GStreamer
 * Copyright (C) 2004 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2005 Brian Cameron <brian.cameron@sun.com>
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
 * SECTION:element-sunaudiosink
 *
 * <refsect2>
 * <para>
 * sunaudiosink is an audio sink designed to work with the Sun Audio
 * interface available in Solaris.
 * </para>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch -v sinesrc ! sunaudiosink
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "gstsunaudiosink.h"

/* elementfactory information */
static GstElementDetails plugin_details = GST_ELEMENT_DETAILS ("Sun Audio Sink",
    "Sink/Audio",
    "Audio sink for Sun Audio devices",
    "David A. Schleef <ds@schleef.org>, "
    "Brian Cameron <brian.cameron@sun.com>");

static void gst_sunaudiosink_base_init (gpointer g_class);
static void gst_sunaudiosink_class_init (GstSunAudioSinkClass * klass);
static void gst_sunaudiosink_init (GstSunAudioSink * filter);
static void gst_sunaudiosink_dispose (GObject * object);

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
gst_sunaudiosink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_sunaudiosink_factory));
  gst_element_class_set_details (element_class, &plugin_details);
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

  parent_class = g_type_class_ref (GST_TYPE_BASE_AUDIO_SINK);

  gobject_class->dispose = gst_sunaudiosink_dispose;
  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_sunaudiosink_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_sunaudiosink_get_property);

  g_object_class_install_property (gobject_class, PROP_DEVICE,
      g_param_spec_string ("device", "Device", "Audio Device (/dev/audio)",
          DEFAULT_DEVICE, G_PARAM_READWRITE));

  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_sunaudiosink_getcaps);

  gstaudiosink_class->open = GST_DEBUG_FUNCPTR (gst_sunaudiosink_open);
  gstaudiosink_class->close = GST_DEBUG_FUNCPTR (gst_sunaudiosink_close);
  gstaudiosink_class->prepare = GST_DEBUG_FUNCPTR (gst_sunaudiosink_prepare);
  gstaudiosink_class->unprepare =
      GST_DEBUG_FUNCPTR (gst_sunaudiosink_unprepare);
  gstaudiosink_class->write = GST_DEBUG_FUNCPTR (gst_sunaudiosink_write);
  gstaudiosink_class->delay = GST_DEBUG_FUNCPTR (gst_sunaudiosink_delay);
  gstaudiosink_class->reset = GST_DEBUG_FUNCPTR (gst_sunaudiosink_reset);
}

static void
gst_sunaudiosink_init (GstSunAudioSink * sunaudiosink)
{
  const char *audiodev;
  GstClockTime buffer_time;
  GValue gvalue = { 0, };

  GST_DEBUG_OBJECT (sunaudiosink, "initializing sunaudiosink");

  /*
   * According to the Sun audio man page, this value can't be set and
   * will be ignored for playback, but setting it the same way that
   * esound does.  Probably not necessary, but doesn't hurt.
   */
  sunaudiosink->buffer_size = 8180;

  /*
   * Reset the buffer-time to 5ms instead of the normal default of 500us
   * (10 times larger, in other words).  
   *
   * Setting a larger buffer causes the sinesrc to not stutter with this
   * sink.  The fact that SunAudio requires a larger buffer should be
   * investigated further to see if this is needed due to limitations of
   * SunAudio itself or because of a more serious problem with the
   * GStreamer engine on Solaris.
   */
  g_value_init (&gvalue, G_TYPE_INT64);
  g_object_get_property (G_OBJECT (sunaudiosink), "buffer-time", &gvalue);
  buffer_time = g_value_get_int64 (&gvalue);
  if (buffer_time < 5000000) {
    g_value_set_int64 (&gvalue, 5000000);
    g_object_set_property (G_OBJECT (sunaudiosink), "buffer-time", &gvalue);
  }

  audiodev = g_getenv ("AUDIODEV");
  if (audiodev == NULL)
    audiodev = DEFAULT_DEVICE;
  sunaudiosink->device = g_strdup (audiodev);
}

static void
gst_sunaudiosink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSunAudioSink *sunaudiosink;

  sunaudiosink = GST_SUNAUDIO_SINK (object);

  switch (prop_id) {
    case PROP_DEVICE:
      g_free (sunaudiosink->device);
      sunaudiosink->device = g_strdup (g_value_get_string (value));
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
      g_value_set_string (value, sunaudiosink->device);
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
  fd = open (sunaudiosink->device, O_WRONLY | O_NONBLOCK);

  if (fd >= 0) {
    close (fd);
    fd = open (sunaudiosink->device, O_WRONLY);
  }

  if (fd == -1) {
    GST_ELEMENT_ERROR (sunaudiosink, RESOURCE, OPEN_WRITE, (NULL),
        ("can't open connection to Sun Audio device %s", sunaudiosink->device));

    return FALSE;
  }

  sunaudiosink->fd = fd;

  ret = ioctl (fd, AUDIO_GETDEV, &sunaudiosink->dev);
  if (ret == -1) {
    GST_ELEMENT_ERROR (sunaudiosink, RESOURCE, SETTINGS, (NULL), ("%s",
            strerror (errno)));
    return FALSE;
  }

  GST_DEBUG_OBJECT (sunaudiosink, "name %s", sunaudiosink->dev.name);
  GST_DEBUG_OBJECT (sunaudiosink, "version %s", sunaudiosink->dev.version);
  GST_DEBUG_OBJECT (sunaudiosink, "config %s", sunaudiosink->dev.config);

  ret = ioctl (fd, AUDIO_GETINFO, &sunaudiosink->info);
  if (ret == -1) {
    GST_ELEMENT_ERROR (sunaudiosink, RESOURCE, SETTINGS, (NULL), ("%s",
            strerror (errno)));
    return FALSE;
  }

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
}

static gboolean
gst_sunaudiosink_close (GstAudioSink * asink)
{
  GstSunAudioSink *sunaudiosink = GST_SUNAUDIO_SINK (asink);

  close (sunaudiosink->fd);
  sunaudiosink->fd = -1;
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
  if (!(ports & AUDIO_SPEAKER) && (ainfo.play.avail_ports & AUDIO_SPEAKER)) {
    ports = ports | AUDIO_SPEAKER;
  }

  AUDIO_INITINFO (&ainfo);

  ainfo.play.sample_rate = spec->rate;
  ainfo.play.channels = spec->channels;
  ainfo.play.precision = spec->width;
  ainfo.play.encoding = AUDIO_ENCODING_LINEAR;
  ainfo.play.port = ports;
  ainfo.play.buffer_size = sunaudiosink->buffer_size;
  ainfo.output_muted = 0;

  ret = ioctl (sunaudiosink->fd, AUDIO_SETINFO, &ainfo);
  if (ret == -1) {
    GST_ELEMENT_ERROR (sunaudiosink, RESOURCE, SETTINGS, (NULL), ("%s",
            strerror (errno)));
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_sunaudiosink_unprepare (GstAudioSink * asink)
{
  return TRUE;
}

static guint
gst_sunaudiosink_write (GstAudioSink * asink, gpointer data, guint length)
{
  GstSunAudioSink *sunaudiosink = GST_SUNAUDIO_SINK (asink);

  if (length > sunaudiosink->buffer_size)
    return write (sunaudiosink->fd, data, sunaudiosink->buffer_size);
  else
    return write (sunaudiosink->fd, data, length);
}

/*
 * Should provide the current delay between writing a sample to the
 * audio device and that sample being actually played.  Returning 0 for
 * now, but this isn't good for synchronization
 */
static guint
gst_sunaudiosink_delay (GstAudioSink * asink)
{
  return 0;
}

static void
gst_sunaudiosink_reset (GstAudioSink * asink)
{
}
