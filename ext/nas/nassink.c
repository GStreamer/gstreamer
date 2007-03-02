/* GStreamer
 * Copyright (C) <2003> Laurent Vivier <Laurent.Vivier@bull.net>
 * Copyright (C) <2004> Arwed v. Merkatz <v.merkatz@gmx.net>
 *
 * Based on esdsink.c:
 * Copyright (C) <2001> Richard Boulton <richard-gst@tartarus.org>
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
#include <string.h>
#include <audio/audiolib.h>
#include <audio/soundlib.h>
#include "nassink.h"

#define NAS_SOUND_PORT_DURATION (2)

GST_DEBUG_CATEGORY (NAS);
/* Signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_MUTE,
  ARG_HOST
};

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) TRUE, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) [ 1000, 96000 ], "
        "channels = (int) [ 1, 2 ]; "
        "audio/x-raw-int, "
        "signed = (boolean) FALSE, "
        "width = (int) 8, "
        "depth = (int) 8, "
        "rate = (int) [ 1000, 96000 ], " "channels = (int) [ 1, 2 ]")
    );

static void gst_nassink_base_init (gpointer g_class);
static void gst_nassink_class_init (GstNassinkClass * klass);
static void gst_nassink_init (GstNassink * nassink);
static void gst_nassink_finalize (GObject * object);

static gboolean gst_nassink_open (GstAudioSink * sink);
static gboolean gst_nassink_close (GstAudioSink * sink);
static gboolean gst_nassink_prepare (GstAudioSink * sink,
    GstRingBufferSpec * spec);
static gboolean gst_nassink_unprepare (GstAudioSink * sink);
static guint gst_nassink_write (GstAudioSink * asink, gpointer data,
    guint length);
static guint gst_nassink_delay (GstAudioSink * asink);
static void gst_nassink_reset (GstAudioSink * asink);
static GstCaps *gst_nassink_getcaps (GstBaseSink * pad);

static void gst_nassink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_nassink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void NAS_flush (GstNassink * sink);
static void NAS_sendData (GstNassink * sink, AuUint32 numBytes);
static AuBool NAS_EventHandler (AuServer * aud, AuEvent * ev,
    AuEventHandlerRec * handler);
static AuDeviceID NAS_getDevice (AuServer * aud, int numTracks);
static int NAS_createFlow (GstNassink * sink, GstRingBufferSpec * spec);

static GstElementClass *parent_class = NULL;

GType
gst_nassink_get_type (void)
{
  static GType nassink_type = 0;

  if (!nassink_type) {
    static const GTypeInfo nassink_info = {
      sizeof (GstNassinkClass),
      gst_nassink_base_init,
      NULL,
      (GClassInitFunc) gst_nassink_class_init,
      NULL,
      NULL,
      sizeof (GstNassink),
      0,
      (GInstanceInitFunc) gst_nassink_init,
    };

    nassink_type =
        g_type_register_static (GST_TYPE_AUDIO_SINK, "GstNassink",
        &nassink_info, 0);
  }

  return nassink_type;
}

static void
gst_nassink_base_init (gpointer g_class)
{
  static const GstElementDetails nassink_details =
      GST_ELEMENT_DETAILS ("NAS audio sink",
      "Sink/Audio",
      "Plays audio to a Network Audio Server",
      "Laurent Vivier <Laurent.Vivier@bull.net>, "
      "Arwed v. Merkatz <v.merkatz@gmx.net>");
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_details (element_class, &nassink_details);
  GST_DEBUG_CATEGORY_INIT (NAS, "NAS", 0, NULL);
}

static void
gst_nassink_class_init (GstNassinkClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSinkClass *gstbasesink_class;
  GstAudioSinkClass *gstaudiosink_class;

  gobject_class = (GObjectClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;
  gstaudiosink_class = (GstAudioSinkClass *) klass;

  if (parent_class == NULL)
    parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_nassink_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_nassink_get_property);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_nassink_finalize);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MUTE, g_param_spec_boolean ("mute", "mute", "mute", TRUE, G_PARAM_READWRITE));   /* CHECKME */
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_HOST, g_param_spec_string ("host", "host", "host", NULL, G_PARAM_READWRITE));    /* CHECKME */

  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_nassink_getcaps);

  gstaudiosink_class->open = GST_DEBUG_FUNCPTR (gst_nassink_open);
  gstaudiosink_class->close = GST_DEBUG_FUNCPTR (gst_nassink_close);
  gstaudiosink_class->prepare = GST_DEBUG_FUNCPTR (gst_nassink_prepare);
  gstaudiosink_class->unprepare = GST_DEBUG_FUNCPTR (gst_nassink_unprepare);
  gstaudiosink_class->write = GST_DEBUG_FUNCPTR (gst_nassink_write);
  gstaudiosink_class->delay = GST_DEBUG_FUNCPTR (gst_nassink_delay);
  gstaudiosink_class->reset = GST_DEBUG_FUNCPTR (gst_nassink_reset);
}

static void
gst_nassink_init (GstNassink * nassink)
{
  GST_CAT_DEBUG (NAS, "nassink: init");

  nassink->mute = FALSE;
  nassink->host = g_strdup (getenv ("AUDIOSERVER"));
  if (nassink->host == NULL)
    nassink->host = g_strdup (getenv ("DISPLAY"));

  nassink->audio = NULL;
  nassink->flow = AuNone;
  nassink->need_data = 0;
}

static void
gst_nassink_finalize (GObject * object)
{
  GstNassink *nassink = GST_NASSINK (object);

  g_free (nassink->host);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
gst_nassink_getcaps (GstBaseSink * bsink)
{
  GstNassink *nassink = GST_NASSINK (bsink);
  GstCaps *templatecaps =
      gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASE_SINK_PAD (bsink)));
  GstCaps *caps;
  int i;
  AuServer *server;

  server = AuOpenServer (nassink->host, 0, NULL, 0, NULL, NULL);
  if (!server)
    return templatecaps;

  for (i = 0; i < gst_caps_get_size (templatecaps); i++) {
    GstStructure *structure = gst_caps_get_structure (templatecaps, i);

    gst_structure_set (structure, "rate", GST_TYPE_INT_RANGE,
        AuServerMinSampleRate (server), AuServerMaxSampleRate (server), NULL);
  }
  caps =
      gst_caps_intersect (templatecaps,
      gst_pad_get_pad_template_caps (GST_BASE_SINK_PAD (bsink)));
  gst_caps_unref (templatecaps);

  return caps;

}

static gboolean
gst_nassink_prepare (GstAudioSink * asink, GstRingBufferSpec * spec)
{
  GstNassink *sink = GST_NASSINK (asink);

  /*spec->bytes_per_sample = sink->rate * NAS_SOUND_PORT_DURATION; */
  /*spec->bytes_per_sample = (spec->width / 8) * spec->channels; */
  memset (spec->silence_sample, 0, spec->bytes_per_sample);
  GST_CAT_DEBUG (NAS, "Sample %d", spec->bytes_per_sample);

  if (sink->audio == NULL)
    return TRUE;

  if (sink->flow != AuNone) {
    GST_CAT_DEBUG (NAS, "flushing buffer");
    NAS_flush (sink);
    AuStopFlow (sink->audio, sink->flow, NULL);
    AuReleaseScratchFlow (sink->audio, sink->flow, NULL);
    sink->flow = AuNone;
  }

  return NAS_createFlow (sink, spec);
}

static gboolean
gst_nassink_unprepare (GstAudioSink * asink)
{
  //GstNassink *sink = GST_NASSINK (asink);
  return TRUE;
}

static guint
gst_nassink_delay (GstAudioSink * asink)
{
  GST_CAT_DEBUG (NAS, "nassink_delay");
  return 0;
}

static void
gst_nassink_reset (GstAudioSink * asink)
{
  GstNassink *sink = GST_NASSINK (asink);

  GST_CAT_DEBUG (NAS, "nassink_reset");
  NAS_flush (sink);
}

static guint
gst_nassink_write (GstAudioSink * asink, gpointer data, guint length)
{
  GstNassink *nassink = GST_NASSINK (asink);
  int used = 0;

  NAS_flush (nassink);
  if (!nassink->mute && nassink->audio != NULL && nassink->flow != AuNone) {

    if (nassink->need_data == 0)
      return 0;

    used = nassink->need_data > length ? length : nassink->need_data;
    AuWriteElement (nassink->audio, nassink->flow, 0, used, data, AuFalse,
        NULL);
    nassink->need_data -= used;
    if (used == length)
      AuSync (nassink->audio, AuFalse);
  } else
    used = length;
  return used;
}

static void
gst_nassink_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstNassink *nassink;

  g_return_if_fail (GST_IS_NASSINK (object));
  nassink = GST_NASSINK (object);

  switch (prop_id) {
    case ARG_MUTE:
      nassink->mute = g_value_get_boolean (value);
      break;
    case ARG_HOST:
      if (nassink->host != NULL)
        g_free (nassink->host);
      if (g_value_get_string (value) == NULL)
        nassink->host = NULL;
      else
        nassink->host = g_strdup (g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_nassink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstNassink *nassink;

  g_return_if_fail (GST_IS_NASSINK (object));

  nassink = GST_NASSINK (object);

  switch (prop_id) {
    case ARG_MUTE:
      g_value_set_boolean (value, nassink->mute);
      break;
    case ARG_HOST:
      g_value_set_string (value, nassink->host);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "nassink", GST_RANK_NONE,
          GST_TYPE_NASSINK)) {
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "nassink",
    "uses NAS for audio output",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);

static gboolean
gst_nassink_open (GstAudioSink * asink)
{
  GstNassink *sink = GST_NASSINK (asink);

  /* Open Server */

  sink->audio = AuOpenServer (sink->host, 0, NULL, 0, NULL, NULL);
  if (sink->audio == NULL)
    return FALSE;
  sink->flow = AuNone;
  sink->need_data = 0;

  /* Start a flow */

  GST_OBJECT_FLAG_SET (sink, GST_NASSINK_OPEN);

  GST_CAT_DEBUG (NAS, "opened audio device");
  return TRUE;
}

static gboolean
gst_nassink_close (GstAudioSink * asink)
{
  GstNassink *sink = GST_NASSINK (asink);

  if (sink->audio == NULL)
    return TRUE;

  if (sink->flow != AuNone) {
    NAS_flush (sink);

    AuStopFlow (sink->audio, sink->flow, NULL);
    AuReleaseScratchFlow (sink->audio, sink->flow, NULL);
    sink->flow = AuNone;
  }

  sink->need_data = 0;

  AuCloseServer (sink->audio);
  sink->audio = NULL;

  GST_OBJECT_FLAG_UNSET (sink, GST_NASSINK_OPEN);

  GST_CAT_DEBUG (NAS, "closed audio device");
  return TRUE;
}

static void
NAS_flush (GstNassink * sink)
{
  AuEvent ev;

  AuNextEvent (sink->audio, AuTrue, &ev);
  AuDispatchEvent (sink->audio, &ev);
}

static void
NAS_sendData (GstNassink * sink, AuUint32 numBytes)
{
  sink->need_data += numBytes;
  return;
}

static AuBool
NAS_EventHandler (AuServer * aud, AuEvent * ev, AuEventHandlerRec * handler)
{
  GstNassink *sink = (GstNassink *) handler->data;
  AuElementNotifyEvent *notify;

  switch (ev->type) {

    case AuEventTypeElementNotify:

      notify = (AuElementNotifyEvent *) ev;

      switch (notify->kind) {

        case AuElementNotifyKindLowWater:
          NAS_sendData (sink, notify->num_bytes);
          break;

        case AuElementNotifyKindState:

          switch (notify->cur_state) {

            case AuStateStop:

              if (sink->flow != AuNone) {
                if (notify->reason == AuReasonEOF)
                  AuStopFlow (handler->aud, sink->flow, NULL);
                AuReleaseScratchFlow (handler->aud, sink->flow, NULL);
                sink->flow = AuNone;
              }
              AuUnregisterEventHandler (handler->aud, handler);
              break;

            case AuStatePause:

              switch (notify->reason) {
                case AuReasonUnderrun:
                case AuReasonOverrun:
                case AuReasonEOF:
                case AuReasonWatermark:

                  NAS_sendData (sink, notify->num_bytes);

                  break;

                case AuReasonHardware:

                  if (AuSoundRestartHardwarePauses)
                    AuStartFlow (handler->aud, sink->flow, NULL);
                  else
                    AuStopFlow (handler->aud, sink->flow, NULL);

                  break;
              }
              break;
          }
          break;
      }
      break;
  }

  return AuTrue;
}

static AuDeviceID
NAS_getDevice (AuServer * aud, int numTracks)
{
  int i;

  for (i = 0; i < AuServerNumDevices (aud); i++) {
    if ((AuDeviceKind (AuServerDevice (aud, i))
            == AuComponentKindPhysicalOutput) &&
        (AuDeviceNumTracks (AuServerDevice (aud, i)) == numTracks)) {

      return AuDeviceIdentifier (AuServerDevice (aud, i));

    }
  }

  return AuNone;
}

static gint
gst_nassink_sink_get_format (const GstRingBufferSpec * spec)
{
  gint result;

  switch (spec->format) {
    case GST_U8:
      result = AuFormatLinearUnsigned8;
      break;
    case GST_S8:
      result = AuFormatLinearSigned8;
      break;
    case GST_S16_LE:
      result = AuFormatLinearSigned16LSB;
      break;
    case GST_S16_BE:
      result = AuFormatLinearSigned16MSB;
      break;
    case GST_U16_LE:
      result = AuFormatLinearUnsigned16LSB;
      break;
    case GST_U16_BE:
      result = AuFormatLinearUnsigned16MSB;
      break;
    default:
      result = 0;
      break;
  }
  return result;
}

static gboolean
NAS_createFlow (GstNassink * sink, GstRingBufferSpec * spec)
{
  AuElement elements[2];
  AuUint32 buf_samples;
  unsigned char format;

  format = gst_nassink_sink_get_format (spec);
  if (format == 0) {
    GST_ELEMENT_ERROR (sink, RESOURCE, SETTINGS, (NULL),
        ("Unable to get format %d", spec->format));
    return FALSE;
  }
  GST_CAT_DEBUG (NAS, "Format: %d %d\n", spec->format, format);

  sink->flow = AuGetScratchFlow (sink->audio, NULL);
  if (sink->flow == 0) {
    GST_CAT_DEBUG (NAS, "couldn't get flow");
    return FALSE;
  }

  /* free old Elements and reconnet to server, needed to change samplerate */
  {
    AuBool clocked;
    int num_elements;
    AuStatus status;
    AuElement *oldelems;

    oldelems =
        AuGetElements (sink->audio, sink->flow, &clocked, &num_elements,
        &status);
    if (num_elements > 0) {
      GST_CAT_DEBUG (NAS, "GetElements status: %i", status);
      if (oldelems)
        AuFreeElements (sink->audio, num_elements, oldelems);
      gst_nassink_close (GST_AUDIO_SINK (sink));
      gst_nassink_open (GST_AUDIO_SINK (sink));
      sink->flow = AuGetScratchFlow (sink->audio, NULL);
      if (sink->flow == 0) {
        GST_CAT_DEBUG (NAS, "couldn't get flow");
        return FALSE;
      }
    }
  }

  /* free old Elements and reconnet to server, needed to change samplerate */
  {
    AuBool clocked;
    int num_elements;
    AuStatus status;
    AuElement *oldelems;

    oldelems =
        AuGetElements (sink->audio, sink->flow, &clocked, &num_elements,
        &status);
    if (num_elements > 0) {
      GST_CAT_DEBUG (NAS, "GetElements status: %i", status);
      if (oldelems)
        AuFreeElements (sink->audio, num_elements, oldelems);
      gst_nassink_close (GST_AUDIO_SINK (sink));
      gst_nassink_open (GST_AUDIO_SINK (sink));
      sink->flow = AuGetScratchFlow (sink->audio, NULL);
      if (sink->flow == 0) {
        GST_CAT_DEBUG (NAS, "couldn't get flow");
        return FALSE;
      }
    }
  }

  buf_samples = spec->rate * NAS_SOUND_PORT_DURATION;
  /*
     spec->segsize = gst_util_uint64_scale (buf_samples * spec->bytes_per_sample,
     spec->latency_time, GST_SECOND / GST_USECOND);
     spec->segsize -= spec->segsize % spec->bytes_per_sample;
     spec->segtotal = spec->buffer_time / spec->latency_time;
   */
  spec->segsize = buf_samples * spec->bytes_per_sample;
  spec->segtotal = 1;

  GST_CAT_DEBUG (NAS, "Rate %d Format %d tracks %d bufs %d %d/%d w %d",
      spec->rate, format, spec->channels, buf_samples, spec->segsize,
      spec->segtotal, spec->width);
  AuMakeElementImportClient (&elements[0],      /* element */
      spec->rate,               /* rate */
      format,                   /* format */
      spec->channels,           /* number of tracks */
      AuTrue,                   /* discart */
      buf_samples,              /* max samples */
      (AuUint32) (buf_samples / 100 * AuSoundPortLowWaterMark),
      /* low water mark */
      0,                        /* num actions */
      NULL);

  sink->device = NAS_getDevice (sink->audio, spec->channels);
  if (sink->device == AuNone) {
    GST_CAT_DEBUG (NAS, "no device with %i tracks found", spec->channels);
    return FALSE;
  }

  AuMakeElementExportDevice (&elements[1],      /* element */
      0,                        /* input */
      sink->device,             /* device */
      spec->rate,               /* rate */
      AuUnlimitedSamples,       /* num samples */
      0,                        /* num actions */
      NULL);                    /* actions */

  AuSetElements (sink->audio,   /* server */
      sink->flow,               /* flow ID */
      AuTrue,                   /* clocked */
      2,                        /* num elements */
      elements,                 /* elements */
      NULL);

  AuRegisterEventHandler (sink->audio,  /* server */
      AuEventHandlerIDMask,     /* value mask */
      0,                        /* type */
      sink->flow,               /* flow ID */
      NAS_EventHandler,         /* callback */
      (AuPointer) sink);        /* data */

  AuStartFlow (sink->audio, sink->flow, NULL);

  return TRUE;
}
