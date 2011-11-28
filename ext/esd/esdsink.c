/* GStreamer
 * Copyright (C) <2005> Arwed v. Merkatz <v.merkatz@gmx.net>
 *
 * Roughly based on the gstreamer 0.8 esdsink plugin:
 * Copyright (C) <2001> Richard Boulton <richard-gst@tartarus.org>
 *
 * esdsink.c: an EsounD audio sink
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
 * SECTION:element-esdsink
 * @see_also: #GstAlsaSink, #GstAutoAudioSink
 *
 * This element outputs sound to an already-running Enlightened Sound Daemon
 * (ESound Daemon, esd). Note that a sound daemon will never be auto-spawned
 * through this element (regardless of the system configuration), since this
 * is actively prevented by the element. If you must use esd, you need to
 * make sure it is started automatically with your session or otherwise.
 *
 * TODO: insert some comments about how sucky esd is and that all the cool
 * kids use pulseaudio or whatever these days.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v filesrc location=foo.ogg ! decodebin ! audioconvert ! audioresample ! esdsink
 * ]| play an Ogg/Vorbis audio file via esd
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "esdsink.h"
#include <esd.h>
#include <unistd.h>
#include <errno.h>

#include <gst/gst-i18n-plugin.h>

/* wtay: from my esd.h (debian unstable libesd0-dev 0.2.36-3) */
#ifndef ESD_MAX_WRITE_SIZE
#define ESD_MAX_WRITE_SIZE (21 * 4096)
#endif

GST_DEBUG_CATEGORY_EXTERN (esd_debug);
#define GST_CAT_DEFAULT esd_debug

enum
{
  PROP_0,
  PROP_HOST
};

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) TRUE, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, 2 ]; "
        "audio/x-raw-int, "
        "signed = (boolean) { true, false }, "
        "width = (int) 8, "
        "depth = (int) 8, "
        "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, 2 ]")
    );

static void gst_esdsink_finalize (GObject * object);

static GstCaps *gst_esdsink_getcaps (GstBaseSink * bsink);

static gboolean gst_esdsink_open (GstAudioSink * asink);
static gboolean gst_esdsink_close (GstAudioSink * asink);
static gboolean gst_esdsink_prepare (GstAudioSink * asink,
    GstRingBufferSpec * spec);
static gboolean gst_esdsink_unprepare (GstAudioSink * asink);
static guint gst_esdsink_write (GstAudioSink * asink, gpointer data,
    guint length);
static guint gst_esdsink_delay (GstAudioSink * asink);
static void gst_esdsink_reset (GstAudioSink * asink);

static void gst_esdsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_esdsink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

GST_BOILERPLATE (GstEsdSink, gst_esdsink, GstAudioSink, GST_TYPE_AUDIO_SINK);

static void
gst_esdsink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class, &sink_factory);
  gst_element_class_set_details_simple (element_class, "Esound audio sink",
      "Sink/Audio",
      "Plays audio to an esound server",
      "Arwed von Merkatz <v.merkatz@gmx.net>");
}

static void
gst_esdsink_class_init (GstEsdSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSinkClass *gstbasesink_class;
  GstAudioSinkClass *gstaudiosink_class;

  gobject_class = (GObjectClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;
  gstaudiosink_class = (GstAudioSinkClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_esdsink_finalize;

  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_esdsink_getcaps);

  gstaudiosink_class->open = GST_DEBUG_FUNCPTR (gst_esdsink_open);
  gstaudiosink_class->close = GST_DEBUG_FUNCPTR (gst_esdsink_close);
  gstaudiosink_class->prepare = GST_DEBUG_FUNCPTR (gst_esdsink_prepare);
  gstaudiosink_class->unprepare = GST_DEBUG_FUNCPTR (gst_esdsink_unprepare);
  gstaudiosink_class->write = GST_DEBUG_FUNCPTR (gst_esdsink_write);
  gstaudiosink_class->delay = GST_DEBUG_FUNCPTR (gst_esdsink_delay);
  gstaudiosink_class->reset = GST_DEBUG_FUNCPTR (gst_esdsink_reset);

  gobject_class->set_property = gst_esdsink_set_property;
  gobject_class->get_property = gst_esdsink_get_property;

  /* default value is filled in the _init method */
  g_object_class_install_property (gobject_class, PROP_HOST,
      g_param_spec_string ("host", "Host",
          "The host running the esound daemon", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_esdsink_init (GstEsdSink * esdsink, GstEsdSinkClass * klass)
{
  esdsink->fd = -1;
  esdsink->ctrl_fd = -1;
  esdsink->host = g_strdup (g_getenv ("ESPEAKER"));
}

static void
gst_esdsink_finalize (GObject * object)
{
  GstEsdSink *esdsink = GST_ESDSINK (object);

  gst_caps_replace (&esdsink->cur_caps, NULL);
  g_free (esdsink->host);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
gst_esdsink_getcaps (GstBaseSink * bsink)
{
  GstEsdSink *esdsink;

  esdsink = GST_ESDSINK (bsink);

  /* no fd, we're done with the template caps */
  if (esdsink->ctrl_fd < 0 || esdsink->cur_caps == NULL) {
    GST_LOG_OBJECT (esdsink, "getcaps called, returning template caps");
    return NULL;
  }

  GST_LOG_OBJECT (esdsink, "returning %" GST_PTR_FORMAT, esdsink->cur_caps);

  return gst_caps_ref (esdsink->cur_caps);
}

static gboolean
gst_esdsink_open (GstAudioSink * asink)
{
  esd_server_info_t *server_info;
  GstPadTemplate *pad_template;
  GstEsdSink *esdsink;
  gchar *saved_env;
  gint i;

  esdsink = GST_ESDSINK (asink);

  GST_DEBUG_OBJECT (esdsink, "open");

  /* ensure libesd doesn't auto-spawn a sound daemon if none is running yet */
  saved_env = g_strdup (g_getenv ("ESD_NO_SPAWN"));
  g_setenv ("ESD_NO_SPAWN", "1", TRUE);

  /* now try to connect to any existing/running sound daemons */
  esdsink->ctrl_fd = esd_open_sound (esdsink->host);

  /* and restore the previous state */
  if (saved_env != NULL) {
    g_setenv ("ESD_NO_SPAWN", saved_env, TRUE);
  } else {
    g_unsetenv ("ESD_NO_SPAWN");
  }
  g_free (saved_env);

  if (esdsink->ctrl_fd < 0)
    goto couldnt_connect;

  /* get server info */
  server_info = esd_get_server_info (esdsink->ctrl_fd);
  if (!server_info)
    goto no_server_info;

  GST_INFO_OBJECT (esdsink, "got server info rate: %i", server_info->rate);

  pad_template = gst_static_pad_template_get (&sink_factory);
  esdsink->cur_caps = gst_caps_copy (gst_pad_template_get_caps (pad_template));
  gst_object_unref (pad_template);

  for (i = 0; i < esdsink->cur_caps->structs->len; i++) {
    GstStructure *s;

    s = gst_caps_get_structure (esdsink->cur_caps, i);
    gst_structure_set (s, "rate", G_TYPE_INT, server_info->rate, NULL);
  }

  esd_free_server_info (server_info);

  GST_INFO_OBJECT (esdsink, "server caps: %" GST_PTR_FORMAT, esdsink->cur_caps);

  return TRUE;

  /* ERRORS */
couldnt_connect:
  {
    GST_ELEMENT_ERROR (esdsink, RESOURCE, OPEN_WRITE,
        (_("Could not establish connection to sound server")),
        ("can't open connection to esound server"));
    return FALSE;
  }
no_server_info:
  {
    GST_ELEMENT_ERROR (esdsink, RESOURCE, OPEN_WRITE,
        (_("Failed to query sound server capabilities")),
        ("couldn't get server info!"));
    return FALSE;
  }
}

static gboolean
gst_esdsink_close (GstAudioSink * asink)
{
  GstEsdSink *esdsink = GST_ESDSINK (asink);

  GST_DEBUG_OBJECT (esdsink, "close");

  gst_caps_replace (&esdsink->cur_caps, NULL);
  esd_close (esdsink->ctrl_fd);
  esdsink->ctrl_fd = -1;

  return TRUE;
}

static gboolean
gst_esdsink_prepare (GstAudioSink * asink, GstRingBufferSpec * spec)
{
  GstEsdSink *esdsink = GST_ESDSINK (asink);
  esd_format_t esdformat;

  /* Name used by esound for this connection. */
  const char connname[] = "GStreamer";

  GST_DEBUG_OBJECT (esdsink, "prepare");

  /* Bitmap describing audio format. */
  esdformat = ESD_STREAM | ESD_PLAY;

  switch (spec->depth) {
    case 8:
      esdformat |= ESD_BITS8;
      break;
    case 16:
      esdformat |= ESD_BITS16;
      break;
    default:
      goto unsupported_depth;
  }

  switch (spec->channels) {
    case 1:
      esdformat |= ESD_MONO;
      break;
    case 2:
      esdformat |= ESD_STEREO;
      break;
    default:
      goto unsupported_channels;
  }

  GST_INFO_OBJECT (esdsink,
      "attempting to open data connection to esound server");

  esdsink->fd =
      esd_play_stream (esdformat, spec->rate, esdsink->host, connname);

  if ((esdsink->fd < 0) || (esdsink->ctrl_fd < 0))
    goto cannot_open;

  esdsink->rate = spec->rate;

  spec->segsize = ESD_BUF_SIZE;
  spec->segtotal = (ESD_MAX_WRITE_SIZE / spec->segsize);

  /* FIXME: this is wrong for signed ints (and the
   * audioringbuffers should do it for us anyway) */
  spec->silence_sample[0] = 0;
  spec->silence_sample[1] = 0;
  spec->silence_sample[2] = 0;
  spec->silence_sample[3] = 0;

  GST_INFO_OBJECT (esdsink, "successfully opened connection to esound server");

  return TRUE;

  /* ERRORS */
unsupported_depth:
  {
    GST_ELEMENT_ERROR (esdsink, STREAM, WRONG_TYPE, (NULL),
        ("can't handle sample depth of %d, only 8 or 16 supported",
            spec->depth));
    return FALSE;
  }
unsupported_channels:
  {
    GST_ELEMENT_ERROR (esdsink, STREAM, WRONG_TYPE, (NULL),
        ("can't handle %d channels, only 1 or 2 supported", spec->channels));
    return FALSE;
  }
cannot_open:
  {
    GST_ELEMENT_ERROR (esdsink, RESOURCE, OPEN_WRITE,
        (_("Could not establish connection to sound server")),
        ("can't open connection to esound server"));
    return FALSE;
  }
}

static gboolean
gst_esdsink_unprepare (GstAudioSink * asink)
{
  GstEsdSink *esdsink = GST_ESDSINK (asink);

  if ((esdsink->fd < 0) && (esdsink->ctrl_fd < 0))
    return TRUE;

  close (esdsink->fd);
  esdsink->fd = -1;

  GST_INFO_OBJECT (esdsink, "closed sound device");

  return TRUE;
}


static guint
gst_esdsink_write (GstAudioSink * asink, gpointer data, guint length)
{
  GstEsdSink *esdsink = GST_ESDSINK (asink);
  gint to_write = 0;

  to_write = length;

  while (to_write > 0) {
    int done;

    done = write (esdsink->fd, data, to_write);

    if (done < 0)
      goto write_error;

    to_write -= done;
    data = (char *) data + done;
  }
  return length;

  /* ERRORS */
write_error:
  {
    GST_ELEMENT_ERROR (esdsink, RESOURCE, WRITE,
        ("Failed to write data to the esound daemon"), GST_ERROR_SYSTEM);
    return -1;
  }
}

static guint
gst_esdsink_delay (GstAudioSink * asink)
{
  GstEsdSink *esdsink = GST_ESDSINK (asink);
  guint latency;

  latency = esd_get_latency (esdsink->ctrl_fd);

  if (latency == (guint) - 1) {
    GST_WARNING_OBJECT (asink, "couldn't get latency");
    return 0;
  }

  /* latency is measured in samples at a rate of 44100, this 
   * cannot overflow. */
  latency = latency * G_GINT64_CONSTANT (44100) / esdsink->rate;

  GST_DEBUG_OBJECT (asink, "got latency: %u", latency);

  return latency;
}

static void
gst_esdsink_reset (GstAudioSink * asink)
{
  GST_DEBUG_OBJECT (asink, "reset called");
}

static void
gst_esdsink_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstEsdSink *esdsink = GST_ESDSINK (object);

  switch (prop_id) {
    case PROP_HOST:
      g_free (esdsink->host);
      esdsink->host = g_value_dup_string (value);
      break;
    default:
      break;
  }
}

static void
gst_esdsink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstEsdSink *esdsink = GST_ESDSINK (object);

  switch (prop_id) {
    case PROP_HOST:
      g_value_set_string (value, esdsink->host);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
