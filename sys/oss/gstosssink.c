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
static GstElementDetails gst_oss_sink_details =
GST_ELEMENT_DETAILS ("Audio Sink (OSS)",
    "Sink/Audio",
    "Output to a sound card via OSS",
    "Erik Walthinsen <omega@cse.ogi.edu>, "
    "Wim Taymans <wim.taymans@chello.be>");

static void gst_oss_sink_base_init (gpointer g_class);
static void gst_oss_sink_class_init (GstOssSinkClass * klass);
static void gst_oss_sink_init (GstOssSink * osssink);
static void gst_oss_sink_dispose (GObject * object);

static GstCaps *gst_oss_sink_getcaps (GstBaseSink * bsink);

static gboolean gst_oss_sink_open (GstAudioSink * asink);
static gboolean gst_oss_sink_close (GstAudioSink * asink);
static gboolean gst_oss_sink_prepare (GstAudioSink * asink,
    GstRingBufferSpec * spec);
static gboolean gst_oss_sink_unprepare (GstAudioSink * asink);
static guint gst_oss_sink_write (GstAudioSink * asink, gpointer data,
    guint length);
static guint gst_oss_sink_delay (GstAudioSink * asink);
static void gst_oss_sink_reset (GstAudioSink * asink);

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
        "endianness = (int) { " G_STRINGIFY (G_BYTE_ORDER) " }, "
        "signed = (boolean) { TRUE, FALSE }, "
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

/* static guint gst_oss_sink_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_oss_sink_get_type (void)
{
  static GType osssink_type = 0;

  if (!osssink_type) {
    static const GTypeInfo osssink_info = {
      sizeof (GstOssSinkClass),
      gst_oss_sink_base_init,
      NULL,
      (GClassInitFunc) gst_oss_sink_class_init,
      NULL,
      NULL,
      sizeof (GstOssSink),
      0,
      (GInstanceInitFunc) gst_oss_sink_init,
    };

    osssink_type =
        g_type_register_static (GST_TYPE_AUDIO_SINK, "GstOssSink",
        &osssink_info, 0);
  }

  return osssink_type;
}

static void
gst_oss_sink_dispose (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_oss_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_oss_sink_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&osssink_sink_factory));
}
static void
gst_oss_sink_class_init (GstOssSinkClass * klass)
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

  gobject_class->dispose = gst_oss_sink_dispose;

  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_oss_sink_getcaps);

  gstaudiosink_class->open = GST_DEBUG_FUNCPTR (gst_oss_sink_open);
  gstaudiosink_class->close = GST_DEBUG_FUNCPTR (gst_oss_sink_close);
  gstaudiosink_class->prepare = GST_DEBUG_FUNCPTR (gst_oss_sink_prepare);
  gstaudiosink_class->unprepare = GST_DEBUG_FUNCPTR (gst_oss_sink_unprepare);
  gstaudiosink_class->write = GST_DEBUG_FUNCPTR (gst_oss_sink_write);
  gstaudiosink_class->delay = GST_DEBUG_FUNCPTR (gst_oss_sink_delay);
  gstaudiosink_class->reset = GST_DEBUG_FUNCPTR (gst_oss_sink_reset);
}

static void
gst_oss_sink_init (GstOssSink * osssink)
{
  GST_DEBUG ("initializing osssink");

  osssink->fd = -1;
}

static GstCaps *
gst_oss_sink_getcaps (GstBaseSink * bsink)
{
  GstOssSink *osssink;
  GstCaps *caps;

  osssink = GST_OSSSINK (bsink);

  if (osssink->fd == -1) {
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASE_SINK_PAD
            (bsink)));
  } else {
    caps = gst_oss_helper_probe_caps (osssink->fd);
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

static gint
gst_oss_sink_get_format (GstBufferFormat fmt)
{
  gint result;

  switch (fmt) {
    case GST_MU_LAW:
      result = AFMT_MU_LAW;
      break;
    case GST_A_LAW:
      result = AFMT_A_LAW;
      break;
    case GST_IMA_ADPCM:
      result = AFMT_IMA_ADPCM;
      break;
    case GST_U8:
      result = AFMT_U8;
      break;
    case GST_S16_LE:
      result = AFMT_S16_LE;
      break;
    case GST_S16_BE:
      result = AFMT_S16_BE;
      break;
    case GST_S8:
      result = AFMT_S8;
      break;
    case GST_U16_LE:
      result = AFMT_U16_LE;
      break;
    case GST_U16_BE:
      result = AFMT_U16_BE;
      break;
    case GST_MPEG:
      result = AFMT_MPEG;
      break;
    default:
      result = 0;
      break;
  }
  return result;
}

static gboolean
gst_oss_sink_open (GstAudioSink * asink)
{
  GstOssSink *oss;
  int mode;

  oss = GST_OSSSINK (asink);

  mode = O_WRONLY;
  mode |= O_NONBLOCK;

  oss->fd = open ("/dev/dsp", mode, 0);
  if (oss->fd == -1) {
    perror ("/dev/dsp");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_oss_sink_close (GstAudioSink * asink)
{
  close (GST_OSSSINK (asink)->fd);
  return TRUE;
}

static gboolean
gst_oss_sink_prepare (GstAudioSink * asink, GstRingBufferSpec * spec)
{
  GstOssSink *oss;
  struct audio_buf_info info;
  int mode;
  int tmp;

  oss = GST_OSSSINK (asink);

  mode = fcntl (oss->fd, F_GETFL);
  mode &= ~O_NONBLOCK;
  if (fcntl (oss->fd, F_SETFL, mode) == -1) {
    perror ("/dev/dsp");
    return FALSE;
  }

  tmp = gst_oss_sink_get_format (spec->format);
  if (tmp == 0)
    goto wrong_format;

  SET_PARAM (oss, SNDCTL_DSP_SETFMT, tmp);
  if (spec->channels == 2)
    SET_PARAM (oss, SNDCTL_DSP_STEREO, 1);
  SET_PARAM (oss, SNDCTL_DSP_CHANNELS, spec->channels);
  SET_PARAM (oss, SNDCTL_DSP_SPEED, spec->rate);

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

wrong_format:
  {
    GST_DEBUG ("wrong format %d\n", spec->format);
    return FALSE;
  }
}

static gboolean
gst_oss_sink_unprepare (GstAudioSink * asink)
{
  /* could do a SNDCTL_DSP_RESET, but the OSS manual recommends a close/open */

  if (!gst_oss_sink_close (asink))
    goto couldnt_close;

  if (!gst_oss_sink_open (asink))
    goto couldnt_reopen;

  return TRUE;

couldnt_close:
  {
    GST_DEBUG ("Could not close the audio device");
    return FALSE;
  }
couldnt_reopen:
  {
    GST_DEBUG ("Could not reopen the audio device");
    return FALSE;
  }
}

static guint
gst_oss_sink_write (GstAudioSink * asink, gpointer data, guint length)
{
  return write (GST_OSSSINK (asink)->fd, data, length);
}

static guint
gst_oss_sink_delay (GstAudioSink * asink)
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
gst_oss_sink_reset (GstAudioSink * asink)
{
  GstOssSink *oss;

  //gint ret;

  oss = GST_OSSSINK (asink);

  /* deadlocks on my machine... */
  //ret = ioctl (oss->fd, SNDCTL_DSP_RESET, 0);
}
