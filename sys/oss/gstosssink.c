/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *               2000,2005 Wim Taymans <wim@fluendo.com>
 *
 * gstosssink.c: 
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
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/soundcard.h>


#include "gstosssink.h"

/* elementfactory information */
static GstElementDetails gst_osssink_details =
GST_ELEMENT_DETAILS ("Audio Sink (OSS)",
    "Sink/Audio",
    "Output to a sound card via OSS",
    "Erik Walthinsen <omega@cse.ogi.edu>, "
    "Wim Taymans <wim.taymans@chello.be>");

static void gst_osssink_base_init (gpointer g_class);
static void gst_osssink_class_init (GstOssSinkClass * klass);
static void gst_osssink_init (GstOssSink * osssink);
static void gst_osssink_dispose (GObject * object);

static GstCaps *gst_osssink_getcaps (GstBaseSink * bsink);

static gboolean gst_osssink_open (GstAudioSink * asink,
    GstRingBufferSpec * spec);
static gboolean gst_osssink_close (GstAudioSink * asink);
static guint gst_osssink_write (GstAudioSink * asink, gpointer data,
    guint length);
static guint gst_osssink_delay (GstAudioSink * asink);
static void gst_osssink_reset (GstAudioSink * asink);

/* OssSink signals and args */
enum
{
  LAST_SIGNAL
};

static GstStaticPadTemplate osssink_sink_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        //"endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, "
        //"signed = (boolean) { TRUE, FALSE }, "
        "endianness = (int) LITTLE_ENDIAN, "
        "signed = (boolean) TRUE, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, 2 ]; "
        "audio/x-raw-int, "
        "signed = (boolean) { TRUE, FALSE }, "
        "width = (int) 8, "
        "depth = (int) 8, "
        "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, 2 ]")
    );

static GstElementClass *parent_class = NULL;

/* static guint gst_osssink_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_osssink_get_type (void)
{
  static GType osssink_type = 0;

  if (!osssink_type) {
    static const GTypeInfo osssink_info = {
      sizeof (GstOssSinkClass),
      gst_osssink_base_init,
      NULL,
      (GClassInitFunc) gst_osssink_class_init,
      NULL,
      NULL,
      sizeof (GstOssSink),
      0,
      (GInstanceInitFunc) gst_osssink_init,
    };

    osssink_type =
        g_type_register_static (GST_TYPE_AUDIOSINK, "GstOssSink",
        &osssink_info, 0);
  }

  return osssink_type;
}

static void
gst_osssink_dispose (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_osssink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_osssink_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&osssink_sink_factory));
}
static void
gst_osssink_class_init (GstOssSinkClass * klass)
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

  parent_class = g_type_class_ref (GST_TYPE_BASEAUDIOSINK);

  gobject_class->dispose = gst_osssink_dispose;

  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_osssink_getcaps);

  gstaudiosink_class->open = GST_DEBUG_FUNCPTR (gst_osssink_open);
  gstaudiosink_class->close = GST_DEBUG_FUNCPTR (gst_osssink_close);
  gstaudiosink_class->write = GST_DEBUG_FUNCPTR (gst_osssink_write);
  gstaudiosink_class->delay = GST_DEBUG_FUNCPTR (gst_osssink_delay);
  gstaudiosink_class->reset = GST_DEBUG_FUNCPTR (gst_osssink_reset);
}

static void
gst_osssink_init (GstOssSink * osssink)
{
  GST_DEBUG ("initializing osssink");

  osssink->element = g_object_new (GST_TYPE_OSSELEMENT, NULL);
}

static GstCaps *
gst_osssink_getcaps (GstBaseSink * bsink)
{
  GstOssSink *osssink;
  GstOssElement *element;
  GstCaps *caps;

  osssink = GST_OSSSINK (bsink);
  element = osssink->element;

  gst_osselement_probe_caps (element);

  if (element->probed_caps == NULL) {
    caps =
        gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASESINK_PAD
            (bsink)));
  } else {
    caps = gst_caps_ref (element->probed_caps);
  }

  return caps;
}

static gint
ilog2 (gint x)
{
  /* well... hacker's delight explains... */
  x = x | (x >> 1);
  x = x | (x >> 2);
  x = x | (x >> 4);
  x = x | (x >> 8);
  x = x | (x >> 16);
  x = x - ((x >> 1) & 0x55555555);
  x = (x & 0x33333333) + ((x >> 2) & 0x33333333);
  x = (x + (x >> 4)) & 0x0f0f0f0f;
  x = x + (x >> 8);
  x = x + (x >> 16);
  return (x & 0x0000003f) - 1;
}

#define SET_PARAM(_oss, _name, _val) 		\
G_STMT_START {					\
  int _tmp = _val;				\
  if (ioctl(_oss->fd, _name, &_tmp) == -1) {	\
    perror(G_STRINGIFY (_name));		\
    return FALSE;				\
  }						\
  GST_DEBUG(G_STRINGIFY (name) " %d", _tmp);	\
} G_STMT_END

#define GET_PARAM(oss, name, val) 		\
G_STMT_START {					\
  if (ioctl(oss->fd, name, val) == -1) {	\
    perror(G_STRINGIFY (name));			\
    return FALSE;				\
  }						\
} G_STMT_END

static gboolean
gst_osssink_open (GstAudioSink * asink, GstRingBufferSpec * spec)
{
  struct audio_buf_info info;
  int mode;
  GstOssSink *oss;
  int tmp;

  oss = GST_OSSSINK (asink);

  mode = O_WRONLY;
  mode |= O_NONBLOCK;

  oss->fd = open ("/dev/dsp", mode, 0);
  if (oss->fd == -1) {
    perror ("/dev/dsp");
    return FALSE;
  }
  mode = fcntl (oss->fd, F_GETFL);
  mode &= ~O_NONBLOCK;
  fcntl (oss->fd, F_SETFL, mode);

  SET_PARAM (oss, SNDCTL_DSP_SETFMT, AFMT_S16_LE);
  SET_PARAM (oss, SNDCTL_DSP_STEREO, 1);
  SET_PARAM (oss, SNDCTL_DSP_CHANNELS, 2);
  SET_PARAM (oss, SNDCTL_DSP_SPEED, 44100);

  tmp = ilog2 (spec->segsize);
  tmp = ((spec->segtotal & 0x7fff) << 16) | tmp;
  GST_DEBUG ("set segsize: %d, segtotal: %d, value: %08x", spec->segsize,
      spec->segtotal, tmp);

  SET_PARAM (oss, SNDCTL_DSP_SETFRAGMENT, tmp);
  GET_PARAM (oss, SNDCTL_DSP_GETOSPACE, &info);

  spec->segsize = info.fragsize;
  spec->segtotal = info.fragstotal;
  spec->bytes_per_sample = 4;
  oss->bytes_per_sample = 4;
  memset (spec->silence_sample, 0, spec->bytes_per_sample);

  GST_DEBUG ("got segsize: %d, segtotal: %d, value: %08x", spec->segsize,
      spec->segtotal, tmp);

  return TRUE;
}

static gboolean
gst_osssink_close (GstAudioSink * asink)
{
  close (GST_OSSSINK (asink)->fd);
  return TRUE;
}

static guint
gst_osssink_write (GstAudioSink * asink, gpointer data, guint length)
{
  return write (GST_OSSSINK (asink)->fd, data, length);
}

static guint
gst_osssink_delay (GstAudioSink * asink)
{
  GstOssSink *oss;
  gint delay = 0;
  gint ret;

  oss = GST_OSSSINK (asink);

#ifdef SNDCTL_DSP_GETODELAY
  ret = ioctl (oss->fd, SNDCTL_DSP_GETODELAY, &delay);
#else
  ret = -1;
#endif
  if (ret < 0) {
    audio_buf_info info;

    ret = ioctl (oss->fd, SNDCTL_DSP_GETOSPACE, &info);

    delay = (ret < 0 ? 0 : (info.fragstotal * info.fragsize) - info.bytes);
  }
  return delay / oss->bytes_per_sample;
}

static void
gst_osssink_reset (GstAudioSink * asink)
{
  GstOssSink *oss;

  //gint ret;

  oss = GST_OSSSINK (asink);

  /* deadlocks on my machine... */
  //ret = ioctl (oss->fd, SNDCTL_DSP_RESET, 0);
}
