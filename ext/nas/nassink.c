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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
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

GST_DEBUG_CATEGORY_STATIC (nas_debug);
#define GST_CAT_DEFAULT nas_debug

enum
{
  ARG_0,
  ARG_MUTE,
  ARG_HOST
};

#define DEFAULT_MUTE  FALSE
#define DEFAULT_HOST  NULL

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

static void gst_nas_sink_finalize (GObject * object);

static gboolean gst_nas_sink_open (GstAudioSink * sink);
static gboolean gst_nas_sink_close (GstAudioSink * sink);
static gboolean gst_nas_sink_prepare (GstAudioSink * sink,
    GstRingBufferSpec * spec);
static gboolean gst_nas_sink_unprepare (GstAudioSink * sink);
static guint gst_nas_sink_write (GstAudioSink * asink, gpointer data,
    guint length);
static guint gst_nas_sink_delay (GstAudioSink * asink);
static void gst_nas_sink_reset (GstAudioSink * asink);
static GstCaps *gst_nas_sink_getcaps (GstBaseSink * pad);

static void gst_nas_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_nas_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void NAS_flush (GstNasSink * sink);
static void NAS_sendData (GstNasSink * sink, AuUint32 numBytes);
static AuBool NAS_EventHandler (AuServer * aud, AuEvent * ev,
    AuEventHandlerRec * handler);
static AuDeviceID NAS_getDevice (AuServer * aud, int numTracks);

GST_BOILERPLATE (GstNasSink, gst_nas_sink, GstAudioSink, GST_TYPE_AUDIO_SINK);

static void
gst_nas_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_static_metadata (element_class, "NAS audio sink",
      "Sink/Audio",
      "Plays audio to a Network Audio Server",
      "Laurent Vivier <Laurent.Vivier@bull.net>, "
      "Arwed v. Merkatz <v.merkatz@gmx.net>");
}

static void
gst_nas_sink_class_init (GstNasSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSinkClass *gstbasesink_class;
  GstAudioSinkClass *gstaudiosink_class;

  gobject_class = (GObjectClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;
  gstaudiosink_class = (GstAudioSinkClass *) klass;

  gobject_class->set_property = gst_nas_sink_set_property;
  gobject_class->get_property = gst_nas_sink_get_property;
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_nas_sink_finalize);

  g_object_class_install_property (gobject_class, ARG_MUTE,
      g_param_spec_boolean ("mute", "mute", "Whether to mute playback",
          DEFAULT_MUTE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_HOST,
      g_param_spec_string ("host", "host",
          "host running the NAS daemon (name of X/Terminal, default is "
          "$AUDIOSERVER or $DISPLAY)", DEFAULT_HOST,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_nas_sink_getcaps);

  gstaudiosink_class->open = GST_DEBUG_FUNCPTR (gst_nas_sink_open);
  gstaudiosink_class->close = GST_DEBUG_FUNCPTR (gst_nas_sink_close);
  gstaudiosink_class->prepare = GST_DEBUG_FUNCPTR (gst_nas_sink_prepare);
  gstaudiosink_class->unprepare = GST_DEBUG_FUNCPTR (gst_nas_sink_unprepare);
  gstaudiosink_class->write = GST_DEBUG_FUNCPTR (gst_nas_sink_write);
  gstaudiosink_class->delay = GST_DEBUG_FUNCPTR (gst_nas_sink_delay);
  gstaudiosink_class->reset = GST_DEBUG_FUNCPTR (gst_nas_sink_reset);
}

static void
gst_nas_sink_init (GstNasSink * nassink, GstNasSinkClass * klass)
{
  /* properties will automatically be set to their default values */
  nassink->audio = NULL;
  nassink->flow = AuNone;
  nassink->need_data = 0;
}

static void
gst_nas_sink_finalize (GObject * object)
{
  GstNasSink *nassink = GST_NAS_SINK (object);

  g_free (nassink->host);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
gst_nas_sink_getcaps (GstBaseSink * bsink)
{
  GstNasSink *nassink = GST_NAS_SINK (bsink);
  const GstCaps *templatecaps;
  AuServer *server;
  GstCaps *fixated, *caps;
  int i;

  server = nassink->audio;

  templatecaps = gst_static_pad_template_get_caps (&sink_factory);

  if (server == NULL)
    return gst_caps_copy (templatecaps);

  fixated = gst_caps_copy (templatecaps);
  for (i = 0; i < gst_caps_get_size (fixated); i++) {
    GstStructure *structure;
    gint min, max;

    min = AuServerMinSampleRate (server);
    max = AuServerMaxSampleRate (server);

    structure = gst_caps_get_structure (fixated, i);

    if (min == max)
      gst_structure_set (structure, "rate", G_TYPE_INT, max, NULL);
    else
      gst_structure_set (structure, "rate", GST_TYPE_INT_RANGE, min, max, NULL);
  }

  caps = gst_caps_intersect (fixated, templatecaps);
  gst_caps_unref (fixated);

  if (nassink->audio == NULL)
    AuCloseServer (server);

  return caps;
}

static gint
gst_nas_sink_sink_get_format (const GstRingBufferSpec * spec)
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
gst_nas_sink_prepare (GstAudioSink * asink, GstRingBufferSpec * spec)
{
  GstNasSink *sink = GST_NAS_SINK (asink);
  AuElement elements[2];
  AuUint32 buf_samples;
  unsigned char format;

  format = gst_nas_sink_sink_get_format (spec);
  if (format == 0) {
    GST_ELEMENT_ERROR (sink, RESOURCE, SETTINGS, (NULL),
        ("Unable to get format %d", spec->format));
    return FALSE;
  }
  GST_DEBUG_OBJECT (sink, "Format: %d %d\n", spec->format, format);

  sink->flow = AuGetScratchFlow (sink->audio, NULL);
  if (sink->flow == 0) {
    GST_DEBUG_OBJECT (sink, "couldn't get flow");
    return FALSE;
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

  memset (spec->silence_sample, 0, spec->bytes_per_sample);
  GST_DEBUG_OBJECT (sink, "Bytes per sample %d", spec->bytes_per_sample);

  GST_DEBUG_OBJECT (sink, "Rate %d Format %d tracks %d bufs %d %d/%d w %d",
      spec->rate, format, spec->channels, (gint) buf_samples, spec->segsize,
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
    GST_DEBUG_OBJECT (sink, "no device with %i tracks found", spec->channels);
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

static gboolean
gst_nas_sink_unprepare (GstAudioSink * asink)
{
  GstNasSink *sink = GST_NAS_SINK (asink);

  if (sink->flow != AuNone) {
    AuBool clocked;
    int num_elements;
    AuStatus status;
    AuElement *oldelems;

    GST_DEBUG_OBJECT (sink, "flushing buffer");
    NAS_flush (sink);

    oldelems =
        AuGetElements (sink->audio, sink->flow, &clocked, &num_elements,
        &status);
    if (num_elements > 0) {
      GST_DEBUG_OBJECT (sink, "GetElements status: %i", status);
      if (oldelems)
        AuFreeElements (sink->audio, num_elements, oldelems);
    }

    AuStopFlow (sink->audio, sink->flow, NULL);
    AuReleaseScratchFlow (sink->audio, sink->flow, NULL);
    sink->flow = AuNone;
  }
  sink->need_data = 0;

  return TRUE;
}

static guint
gst_nas_sink_delay (GstAudioSink * asink)
{
  GST_DEBUG_OBJECT (asink, "nas_sink_delay");
  return 0;
}

static void
gst_nas_sink_reset (GstAudioSink * asink)
{
  GstNasSink *sink = GST_NAS_SINK (asink);

  GST_DEBUG_OBJECT (sink, "reset");

  if (sink->flow != AuNone)
    AuStopFlow (sink->audio, sink->flow, NULL);
}

static guint
gst_nas_sink_write (GstAudioSink * asink, gpointer data, guint length)
{
  GstNasSink *nassink = GST_NAS_SINK (asink);
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
gst_nas_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstNasSink *nassink;

  nassink = GST_NAS_SINK (object);

  switch (prop_id) {
    case ARG_MUTE:
      nassink->mute = g_value_get_boolean (value);
      break;
    case ARG_HOST:
      g_free (nassink->host);
      nassink->host = g_value_dup_string (value);
      if (nassink->host == NULL)
        nassink->host = g_strdup (g_getenv ("AUDIOSERVER"));
      if (nassink->host == NULL)
        nassink->host = g_strdup (g_getenv ("DISPLAY"));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_nas_sink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstNasSink *nassink;

  nassink = GST_NAS_SINK (object);

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
gst_nas_sink_open (GstAudioSink * asink)
{
  GstNasSink *sink = GST_NAS_SINK (asink);

  GST_DEBUG_OBJECT (sink, "opening, host = '%s'", GST_STR_NULL (sink->host));

  /* Open Server */
  sink->audio = AuOpenServer (sink->host, 0, NULL, 0, NULL, NULL);
  if (sink->audio == NULL) {
    GST_DEBUG_OBJECT (sink, "opening failed");
    return FALSE;
  }
  sink->flow = AuNone;
  sink->need_data = 0;

  /* Start a flow */
  GST_DEBUG_OBJECT (asink, "opened audio device");
  return TRUE;
}

static gboolean
gst_nas_sink_close (GstAudioSink * asink)
{
  GstNasSink *sink = GST_NAS_SINK (asink);

  if (sink->audio) {
    AuCloseServer (sink->audio);
    sink->audio = NULL;
  }

  GST_DEBUG_OBJECT (sink, "closed audio device");
  return TRUE;
}

static void
NAS_flush (GstNasSink * sink)
{
  AuEvent ev;

  AuNextEvent (sink->audio, AuTrue, &ev);
  AuDispatchEvent (sink->audio, &ev);
}

static void
NAS_sendData (GstNasSink * sink, AuUint32 numBytes)
{
  sink->need_data += numBytes;
  return;
}

static AuBool
NAS_EventHandler (AuServer * aud, AuEvent * ev, AuEventHandlerRec * handler)
{
  GstNasSink *sink = (GstNasSink *) handler->data;
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

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (nas_debug, "NAS", 0, NULL);

  if (!gst_element_register (plugin, "nassink", GST_RANK_NONE,
          GST_TYPE_NAS_SINK)) {
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    nas,
    "NAS (Network Audio System) support for GStreamer",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
