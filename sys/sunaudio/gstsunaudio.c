/*
 * GStreamer
 * Copyright (C) 1999-2001 Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2004 David A. Schleef <ds@schleef.org>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gst.h>

#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/audioio.h>


#define GST_TYPE_SUNAUDIOSINK \
  (gst_gst_sunaudiosink_get_type())
#define GST_SUNAUDIOSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SUNAUDIOSINK,GstSunAudioSink))
#define GST_SUNAUDIOSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SUNAUDIOSINK,GstSunAudioSink))
#define GST_IS_SUNAUDIOSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SUNAUDIOSINK))
#define GST_IS_SUNAUDIOSINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SUNAUDIOSINK))

typedef struct _GstSunAudioSink GstSunAudioSink;
typedef struct _GstSunAudioSinkClass GstSunAudioSinkClass;

struct _GstSunAudioSink
{
  GstElement element;

  GstPad *sinkpad;

  char *device;
  int fd;
  audio_device_t dev;
  audio_info_t info;

  int channels;
  int width;
  int rate;
  int buffer_size;
};

struct _GstSunAudioSinkClass
{
  GstElementClass parent_class;
};

GType gst_gst_sunaudiosink_get_type (void);


static GstElementDetails plugin_details = {
  "SunAudioSink",
  "Sink/Audio",
  "Audio sink for Sun Audio devices",
  "David A. Schleef <ds@schleef.org>",
};

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_DEVICE,
  ARG_BUFFER_SIZE
};

static GstStaticPadTemplate gst_sunaudiosink_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) { TRUE, FALSE }, "
        "width = (int) { 8, 16 }, "
        "depth = (int) { 8, 16 }, "
        "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, 2 ]")
    );

static void gst_sunaudiosink_base_init (gpointer g_class);
static void gst_sunaudiosink_class_init (GstSunAudioSinkClass * klass);
static void gst_sunaudiosink_init (GstSunAudioSink * filter);

static GstCaps *gst_sunaudiosink_getcaps (GstPad * pad);
static GstPadLinkReturn gst_sunaudiosink_pad_link (GstPad * pad,
    const GstCaps * caps);

static void gst_sunaudiosink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_sunaudiosink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_sunaudiosink_setparams (GstSunAudioSink * sunaudiosink);
static void gst_sunaudiosink_chain (GstPad * pad, GstData * _data);
static GstElementStateReturn gst_sunaudiosink_change_state (GstElement *
    element);

static GstElementClass *parent_class = NULL;

typedef struct _GstFencedBuffer GstFencedBuffer;
struct _GstFencedBuffer
{
  GstBuffer buffer;
  void *region;
  unsigned int length;
};

GType
gst_gst_sunaudiosink_get_type (void)
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

    plugin_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstSunAudioSink", &plugin_info, 0);
  }
  return plugin_type;
}

static void
gst_sunaudiosink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_sunaudiosink_sink_factory));
  gst_element_class_set_details (element_class, &plugin_details);
}

static void
gst_sunaudiosink_class_init (GstSunAudioSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_sunaudiosink_set_property;
  gobject_class->get_property = gst_sunaudiosink_get_property;

  gstelement_class->change_state = gst_sunaudiosink_change_state;

  g_object_class_install_property (gobject_class, ARG_DEVICE,
      g_param_spec_string ("device", "Device", "Audio Device (/dev/audio)",
          "/dev/audio", G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_BUFFER_SIZE,
      g_param_spec_int ("buffer_size", "Buffer Size", "Buffer Size",
          1, G_MAXINT, 64, G_PARAM_READWRITE));
}

static void
gst_sunaudiosink_init (GstSunAudioSink * sunaudiosink)
{
  sunaudiosink->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_sunaudiosink_sink_factory), "sink");
  gst_pad_set_getcaps_function (sunaudiosink->sinkpad,
      gst_sunaudiosink_getcaps);
  gst_pad_set_link_function (sunaudiosink->sinkpad, gst_sunaudiosink_pad_link);

  gst_element_add_pad (GST_ELEMENT (sunaudiosink), sunaudiosink->sinkpad);
  gst_pad_set_chain_function (sunaudiosink->sinkpad, gst_sunaudiosink_chain);

  sunaudiosink->buffer_size = 64;
  sunaudiosink->device = g_strdup ("/dev/audio");
}

static GstCaps *
gst_sunaudiosink_getcaps (GstPad * pad)
{
  GstSunAudioSink *sunaudiosink = GST_SUNAUDIOSINK (gst_pad_get_parent (pad));
  GstCaps *caps;

  caps = gst_caps_from_string ("audio/x-raw-int, "
      "endianness = (int) BYTE_ORDER, "
      "signed = (boolean) TRUE, "
      "width = (int) 16, "
      "depth = (int) 16, " "rate = (int) 44100, " "channels = (int) 1");
  GST_ERROR ("getcaps called on %" GST_PTR_FORMAT ", returning %"
      GST_PTR_FORMAT, pad, caps);

  return caps;
}

static GstPadLinkReturn
gst_sunaudiosink_pad_link (GstPad * pad, const GstCaps * caps)
{
  GstSunAudioSink *sunaudiosink = GST_SUNAUDIOSINK (gst_pad_get_parent (pad));
  GstPadLinkReturn ret;
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "rate", &sunaudiosink->rate);
  gst_structure_get_int (structure, "width", &sunaudiosink->width);
  gst_structure_get_int (structure, "channels", &sunaudiosink->channels);

  if (gst_sunaudiosink_setparams (sunaudiosink)) {
    ret = GST_PAD_LINK_OK;
  } else {
    ret = GST_PAD_LINK_REFUSED;
  }
  GST_ERROR ("pad_link called on %" GST_PTR_FORMAT " with caps %"
      GST_PTR_FORMAT ", returning %d", pad, caps, ret);

  return ret;
}

static void
gst_sunaudiosink_chain (GstPad * pad, GstData * data)
{
  GstSunAudioSink *sunaudiosink = GST_SUNAUDIOSINK (gst_pad_get_parent (pad));
  GstBuffer *buffer = GST_BUFFER (data);
  int ret;

  if (GST_IS_EVENT (data)) {
    g_assert (0);
  } else {
    ret = write (sunaudiosink->fd, GST_BUFFER_DATA (buffer),
        GST_BUFFER_SIZE (buffer));
    if (ret != GST_BUFFER_SIZE (buffer)) {
      GST_ELEMENT_ERROR (sunaudiosink, RESOURCE, WRITE, (NULL),
          ("%s", strerror (errno)));
    }
  }

  gst_data_unref (data);
}

static gboolean
gst_sunaudiosink_setparams (GstSunAudioSink * sunaudiosink)
{
  audio_info_t ainfo;
  int ret;

  AUDIO_INITINFO (&ainfo);

  ainfo.play.sample_rate = sunaudiosink->rate;
  ainfo.play.channels = sunaudiosink->channels;
  ainfo.play.precision = sunaudiosink->width;
  ainfo.play.encoding = AUDIO_ENCODING_LINEAR;
  ainfo.play.port = AUDIO_SPEAKER;
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
gst_sunaudiosink_open (GstSunAudioSink * sunaudiosink)
{
  const char *file;
  int fd, ret;

  file = "/dev/audio";

  fd = open (file, O_WRONLY);
  if (fd == -1) {
    /* FIXME error */
    return FALSE;
  }

  sunaudiosink->fd = fd;

  ret = ioctl (fd, AUDIO_GETDEV, &sunaudiosink->dev);
  if (ret == -1) {
    GST_ELEMENT_ERROR (sunaudiosink, RESOURCE, SETTINGS, (NULL), ("%s",
            strerror (errno)));
    return FALSE;
  }

  GST_INFO ("name %s", sunaudiosink->dev.name);
  GST_INFO ("version %s", sunaudiosink->dev.version);
  GST_INFO ("config %s", sunaudiosink->dev.config);

  ret = ioctl (fd, AUDIO_GETINFO, &sunaudiosink->info);
  if (ret == -1) {
    GST_ELEMENT_ERROR (sunaudiosink, RESOURCE, SETTINGS, (NULL), ("%s",
            strerror (errno)));
    return FALSE;
  }

  GST_INFO ("monitor_gain %d", sunaudiosink->info.monitor_gain);
  GST_INFO ("output_muted %d", sunaudiosink->info.output_muted);
  GST_INFO ("hw_features %08x", sunaudiosink->info.hw_features);
  GST_INFO ("sw_features %08x", sunaudiosink->info.sw_features);
  GST_INFO ("sw_features_enabled %08x", sunaudiosink->info.sw_features_enabled);

  return TRUE;
}

static void
gst_sunaudiosink_close (GstSunAudioSink * sunaudiosink)
{
  close (sunaudiosink->fd);
}

static GstElementStateReturn
gst_sunaudiosink_change_state (GstElement * element)
{
  GstSunAudioSink *sunaudiosink = GST_SUNAUDIOSINK (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      if (!gst_sunaudiosink_open (sunaudiosink)) {
        return GST_STATE_FAILURE;
      }
      break;
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      break;
    case GST_STATE_READY_TO_NULL:
      gst_sunaudiosink_close (sunaudiosink);
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static void
gst_sunaudiosink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSunAudioSink *sunaudiosink;

  g_return_if_fail (GST_IS_SUNAUDIOSINK (object));
  sunaudiosink = GST_SUNAUDIOSINK (object);

  switch (prop_id) {
    case ARG_DEVICE:
      if (gst_element_get_state (GST_ELEMENT (sunaudiosink)) == GST_STATE_NULL) {
        g_free (sunaudiosink->device);
        sunaudiosink->device = g_strdup (g_value_get_string (value));
      }
      break;
    case ARG_BUFFER_SIZE:
      if (gst_element_get_state (GST_ELEMENT (sunaudiosink)) == GST_STATE_NULL) {
        sunaudiosink->buffer_size = g_value_get_int (value);
      }
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

  g_return_if_fail (GST_IS_SUNAUDIOSINK (object));
  sunaudiosink = GST_SUNAUDIOSINK (object);

  switch (prop_id) {
    case ARG_DEVICE:
      g_value_set_string (value, sunaudiosink->device);
      break;
    case ARG_BUFFER_SIZE:
      g_value_set_int (value, sunaudiosink->buffer_size);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "sunaudiosink", GST_RANK_NONE,
          GST_TYPE_SUNAUDIOSINK))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "sunaudio",
    "elements for SunAudio",
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)
