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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "esdsink.h"
#include <esd.h>
#include <unistd.h>
#include <errno.h>

GST_DEBUG_CATEGORY_EXTERN (esd_debug);
#define GST_CAT_DEFAULT esd_debug

/* elementfactory information */
static GstElementDetails esdsink_details = {
  "Esound audio sink",
  "Sink/Audio",
  "Plays audio to an esound server",
  "Arwed von Merkatz <v.merkatz@gmx.net>",
};

enum
{
  ARG_0,
  ARG_HOST
};

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) " G_STRINGIFY (G_BYTE_ORDER) ", "
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

static void gst_esdsink_base_init (gpointer g_class);
static void gst_esdsink_class_init (GstEsdSinkClass * klass);
static void gst_esdsink_init (GstEsdSink * esdsink);
static void gst_esdsink_dispose (GObject * object);

static GstElementStateReturn gst_esdsink_change_state (GstElement * element);
static GstCaps *gst_esdsink_getcaps (GstBaseSink * bsink);

static gboolean gst_esdsink_open (GstAudioSink * asink,
    GstRingBufferSpec * spec);
static gboolean gst_esdsink_close (GstAudioSink * asink);
static guint gst_esdsink_write (GstAudioSink * asink, gpointer data,
    guint length);
static guint gst_esdsink_delay (GstAudioSink * asink);
static void gst_esdsink_reset (GstAudioSink * asink);

static void gst_esdsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_esdsink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstElementClass *parent_class = NULL;

GType
gst_esdsink_get_type (void)
{
  static GType esdsink_type = 0;

  if (!esdsink_type) {
    static const GTypeInfo esdsink_info = {
      sizeof (GstEsdSinkClass),
      gst_esdsink_base_init,
      NULL,
      (GClassInitFunc) gst_esdsink_class_init,
      NULL,
      NULL,
      sizeof (GstEsdSink),
      0,
      (GInstanceInitFunc) gst_esdsink_init,
    };

    esdsink_type =
        g_type_register_static (GST_TYPE_AUDIO_SINK, "GstEsdSink",
        &esdsink_info, 0);
  }
  return esdsink_type;
}

static void
gst_esdsink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_details (element_class, &esdsink_details);
}

static void
gst_esdsink_class_init (GstEsdSinkClass * klass)
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

  parent_class = g_type_class_ref (GST_TYPE_AUDIO_SINK);

  gobject_class->dispose = gst_esdsink_dispose;

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_esdsink_change_state);

  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_esdsink_getcaps);

  gstaudiosink_class->open = GST_DEBUG_FUNCPTR (gst_esdsink_open);
  gstaudiosink_class->close = GST_DEBUG_FUNCPTR (gst_esdsink_close);
  gstaudiosink_class->write = GST_DEBUG_FUNCPTR (gst_esdsink_write);
  gstaudiosink_class->delay = GST_DEBUG_FUNCPTR (gst_esdsink_delay);
  gstaudiosink_class->reset = GST_DEBUG_FUNCPTR (gst_esdsink_reset);

  gobject_class->set_property = gst_esdsink_set_property;
  gobject_class->get_property = gst_esdsink_get_property;

  g_object_class_install_property (gobject_class, ARG_HOST,
      g_param_spec_string ("host", "host", "host", NULL, G_PARAM_READWRITE));

}

static void
gst_esdsink_init (GstEsdSink * esdsink)
{
  esdsink->fd = -1;
  esdsink->ctrl_fd = -1;
  esdsink->host = g_strdup (getenv ("ESPEAKER"));
}

static void
gst_esdsink_dispose (GObject * object)
{
  GstEsdSink *esdsink = GST_ESDSINK (object);

  g_free (esdsink->host);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static GstElementStateReturn
gst_esdsink_change_state (GstElement * element)
{
  GstEsdSink *esdsink = GST_ESDSINK (element);
  GstElementState transition = GST_STATE_TRANSITION (element);
  GstElementStateReturn ret = GST_STATE_SUCCESS;

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      GST_INFO ("attempting to open control connection to esound server");
      esdsink->ctrl_fd = esd_open_sound (esdsink->host);
      if (esdsink->ctrl_fd < 0) {
        GST_ELEMENT_ERROR (esdsink, RESOURCE, OPEN_WRITE, (NULL),
            ("can't open connection to esound server"));
        ret = GST_STATE_FAILURE;
      }
      break;
    default:
      break;
  }
  if (ret == GST_STATE_SUCCESS)
    ret = GST_ELEMENT_CLASS (parent_class)->change_state (element);
  return ret;
}

#define IS_WRITABLE(caps) \
  (g_atomic_int_get (&(caps)->refcount) == 1)

void
gst_caps_set_each (GstCaps * caps, char *field, ...)
{
  GstStructure *structure;
  va_list var_args;
  int i;

  g_return_if_fail (GST_IS_CAPS (caps));
  g_return_if_fail (IS_WRITABLE (caps));

  va_start (var_args, field);
  for (i = 0; i < caps->structs->len; i++) {
    structure = gst_caps_get_structure (caps, i);

    gst_structure_set_valist (structure, field, var_args);
  }
  va_end (var_args);
}

static GstCaps *
gst_esdsink_getcaps (GstBaseSink * bsink)
{
  GST_DEBUG ("getcaps called");
  esd_server_info_t *server_info;
  GstCaps *caps = NULL;
  GstEsdSink *esdsink = GST_ESDSINK (bsink);

  esdsink->ctrl_fd = esd_open_sound (esdsink->host);
  if (esdsink->ctrl_fd < 0)
    return NULL;
  server_info = esd_get_server_info (esdsink->ctrl_fd);
  if (server_info) {
    GST_DEBUG ("got server info rate: %i", server_info->rate);
    GstPadTemplate *pad_template;

    pad_template = gst_static_pad_template_get (&sink_factory);
    caps = gst_caps_copy (gst_pad_template_get_caps (pad_template));
    gst_caps_set_each (caps, "rate", G_TYPE_INT, server_info->rate, NULL);
    free (server_info);
    return caps;
  }
  return NULL;
}

static gboolean
gst_esdsink_open (GstAudioSink * asink, GstRingBufferSpec * spec)
{
  GstEsdSink *esdsink = GST_ESDSINK (asink);

  /* Name used by esound for this connection. */
  const char *connname = "GStreamer";

  /* Bitmap describing audio format. */
  esd_format_t esdformat = ESD_STREAM | ESD_PLAY;

  if (spec->depth == 16)
    esdformat |= ESD_BITS16;
  else if (spec->depth == 8) {
    esdformat |= ESD_BITS8;
  }

  if (spec->channels == 2)
    esdformat |= ESD_STEREO;
  else if (spec->channels == 1) {
    esdformat |= ESD_MONO;
  }

  GST_INFO ("attempting to open data connection to esound server");
  esdsink->fd =
      esd_play_stream (esdformat, spec->rate, esdsink->host, connname);

  if ((esdsink->fd < 0) || (esdsink->ctrl_fd < 0)) {
    GST_ELEMENT_ERROR (esdsink, RESOURCE, OPEN_WRITE, (NULL),
        ("can't open connection to esound server"));
    return FALSE;
  }
  GST_INFO ("successfully opened connection to esound server");

  return TRUE;
}

static gboolean
gst_esdsink_close (GstAudioSink * asink)
{
  GstEsdSink *esdsink = GST_ESDSINK (asink);

  if ((esdsink->fd < 0) && (esdsink->ctrl_fd < 0))
    return TRUE;

  close (esdsink->fd);
  esdsink->fd = -1;
  close (esdsink->ctrl_fd);
  esdsink->ctrl_fd = -1;

  GST_INFO ("esdsink: closed sound device");
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

    if (done < 0) {
      GST_ELEMENT_ERROR (esdsink, RESOURCE, WRITE,
          ("Failed to write data to the esound daemon"), GST_ERROR_SYSTEM);
      return 0;
    }

    to_write -= done;
    data += done;
  }
  return length;
}

static guint
gst_esdsink_delay (GstAudioSink * asink)
{
  GstEsdSink *esdsink = GST_ESDSINK (asink);
  guint latency = esd_get_latency (esdsink->ctrl_fd);

  GST_DEBUG ("got latency: %u", latency);
  return latency;
}

static void
gst_esdsink_reset (GstAudioSink * asink)
{
  GST_DEBUG ("reset called");
}

static void
gst_esdsink_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstEsdSink *esdsink = GST_ESDSINK (object);

  switch (prop_id) {
    case ARG_HOST:
      g_free (esdsink->host);
      if (g_value_get_string (value) == NULL)
        esdsink->host = NULL;
      else
        esdsink->host = g_strdup (g_value_get_string (value));
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
    case ARG_HOST:
      g_value_set_string (value, esdsink->host);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_esdsink_factory_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "esdsink", GST_RANK_NONE,
          GST_TYPE_ESDSINK))
    return FALSE;

  return TRUE;
}
