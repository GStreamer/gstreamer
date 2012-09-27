/* GStreamer
 * Copyright (C) 2012 Fluendo S.A. <support@fluendo.com>
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
#  include <config.h>
#endif

#include "openslessink.h"

GST_DEBUG_CATEGORY_STATIC (opensles_sink_debug);
#define GST_CAT_DEFAULT opensles_sink_debug

enum
{
  PROP_0,
  PROP_VOLUME,
  PROP_MUTE,
  PROP_LAST
};

#define DEFAULT_VOLUME 1.0
#define DEFAULT_MUTE   FALSE


/* According to Android's NDK doc the following are the supported rates */
#define RATES "8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000"

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) {" G_STRINGIFY (G_BYTE_ORDER) " }, "
        "signed = (boolean) { TRUE }, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) { " RATES "}, "
        "channels = (int) [1, 2];"
        "audio/x-raw-int, "
        "endianness = (int) {" G_STRINGIFY (G_BYTE_ORDER) " }, "
        "signed = (boolean) { FALSE }, "
        "width = (int) 8, "
        "depth = (int) 8, "
        "rate = (int) { " RATES "}, " "channels = (int) [1, 2]")
    );

static void
_do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT (opensles_sink_debug, "opensles_sink", 0,
      "OpenSL ES Sink");
}

GST_BOILERPLATE_FULL (GstOpenSLESSink, gst_opensles_sink, GstBaseAudioSink,
    GST_TYPE_BASE_AUDIO_SINK, _do_init);

static void
gst_opensles_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class, &sink_factory);

  gst_element_class_set_details_simple (element_class, "OpenSL ES Sink",
      "Sink/Audio",
      "Output sound using the OpenSL ES APIs",
      "Josep Torra <support@fluendo.com>");
}

static GstRingBuffer *
gst_opensles_sink_create_ringbuffer (GstBaseAudioSink * base)
{
  GstOpenSLESSink *sink = GST_OPENSLES_SINK (base);
  GstRingBuffer *rb;

  rb = gst_opensles_ringbuffer_new (RB_MODE_SINK_PCM);
  gst_opensles_ringbuffer_set_volume (rb, sink->volume);
  gst_opensles_ringbuffer_set_mute (rb, sink->mute);
  return rb;
}

#define AUDIO_OUTPUT_DESC_FORMAT                                              \
    "deviceName: %s deviceConnection: %d deviceScope: %d deviceLocation: %d " \
    "isForTelephony: %d minSampleRate: %d maxSampleRate: %d "                 \
    "isFreqRangeContinuous: %d maxChannels: %d"

#define AUDIO_OUTPUT_DESC_ARGS(aod)                                           \
    (gchar*) (aod)->pDeviceName, (gint) (aod)->deviceConnection,              \
    (gint) (aod)->deviceScope, (gint) (aod)->deviceLocation,                  \
    (gint) (aod)->isForTelephony, (gint) (aod)->minSampleRate,                \
    (gint) (aod)->maxSampleRate, (gint) (aod)->isFreqRangeContinuous,         \
    (gint) (aod)->maxChannels

/* Next it's not defined in Android */
#ifndef MAX_NUMBER_OUTPUT_DEVICES
#define MAX_NUMBER_OUTPUT_DEVICES 16
#endif

static gboolean
_opensles_query_capabilities (GstOpenSLESSink * sink)
{
  gboolean res = FALSE;
  SLresult result;
  SLObjectItf engineObject = NULL;
  SLAudioIODeviceCapabilitiesItf audioIODeviceCapabilities;
  SLint32 i, j, numOutputs = MAX_NUMBER_OUTPUT_DEVICES;
  SLuint32 outputDeviceIDs[MAX_NUMBER_OUTPUT_DEVICES];
  SLAudioOutputDescriptor audioOutputDescriptor;

  /* Create engine */
  result = slCreateEngine (&engineObject, 0, NULL, 0, NULL, NULL);
  if (result != SL_RESULT_SUCCESS) {
    GST_ERROR_OBJECT (sink, "slCreateEngine failed(0x%08x)", (guint32) result);
    goto beach;
  }

  /* Realize the engine */
  result = (*engineObject)->Realize (engineObject, SL_BOOLEAN_FALSE);
  if (result != SL_RESULT_SUCCESS) {
    GST_ERROR_OBJECT (sink, "engine.Realize failed(0x%08x)", (guint32) result);
    goto beach;
  }

  /* Get the engine interface, which is needed in order to
   * create other objects */
  result = (*engineObject)->GetInterface (engineObject,
      SL_IID_AUDIOIODEVICECAPABILITIES, &audioIODeviceCapabilities);
  if (result != SL_RESULT_SUCCESS) {
    GST_ERROR_OBJECT (sink,
        "engine.GetInterface(IODeviceCapabilities) failed(0x%08x)",
        (guint32) result);
    goto beach;
  }

  result = (*audioIODeviceCapabilities)->GetAvailableAudioOutputs
      (audioIODeviceCapabilities, &numOutputs, outputDeviceIDs);
  if (result != SL_RESULT_SUCCESS) {
    GST_ERROR_OBJECT (sink,
        "IODeviceCapabilities.GetAvailableAudioOutputs failed(0x%08x)",
        (guint32) result);
    goto beach;
  }

  GST_DEBUG_OBJECT (sink, "Found %d output devices", (gint32) numOutputs);

  for (i = 0; i < numOutputs; i++) {
    result = (*audioIODeviceCapabilities)->QueryAudioOutputCapabilities
        (audioIODeviceCapabilities, outputDeviceIDs[i], &audioOutputDescriptor);
    if (result != SL_RESULT_SUCCESS) {
      GST_ERROR_OBJECT (sink,
          "IODeviceCapabilities.QueryAudioOutputCapabilities failed(0x%08x)",
          (guint32) result);
      continue;
    }

    GST_DEBUG_OBJECT (sink, "  ID: %08x " AUDIO_OUTPUT_DESC_FORMAT,
        (guint) outputDeviceIDs[i],
        AUDIO_OUTPUT_DESC_ARGS (&audioOutputDescriptor));
    GST_DEBUG_OBJECT (sink, "  Found %d supported sample rated",
        audioOutputDescriptor.numOfSamplingRatesSupported);

    for (j = 0; j < audioOutputDescriptor.numOfSamplingRatesSupported; j++) {
      GST_DEBUG_OBJECT (sink, "    %d Hz",
          (gint) audioOutputDescriptor.samplingRatesSupported[j]);
    }
  }

  res = TRUE;
beach:
  /* Destroy engine object */
  if (engineObject) {
    (*engineObject)->Destroy (engineObject);
  }

  return res;
}

static void
gst_opensles_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOpenSLESSink *sink = GST_OPENSLES_SINK (object);
  GstRingBuffer *rb = GST_BASE_AUDIO_SINK (sink)->ringbuffer;

  switch (prop_id) {
    case PROP_VOLUME:
      sink->volume = g_value_get_double (value);
      if (rb && GST_IS_OPENSLES_RING_BUFFER (rb)) {
        gst_opensles_ringbuffer_set_volume (rb, sink->volume);
      }
      break;
    case PROP_MUTE:
      sink->mute = g_value_get_boolean (value);
      if (rb && GST_IS_OPENSLES_RING_BUFFER (rb)) {
        gst_opensles_ringbuffer_set_mute (rb, sink->mute);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_opensles_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstOpenSLESSink *sink = GST_OPENSLES_SINK (object);
  switch (prop_id) {
    case PROP_VOLUME:
      g_value_set_double (value, sink->volume);
      break;
    case PROP_MUTE:
      g_value_set_boolean (value, sink->mute);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_opensles_sink_class_init (GstOpenSLESSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseAudioSinkClass *gstbaseaudiosink_class;

  gobject_class = (GObjectClass *) klass;
  gstbaseaudiosink_class = (GstBaseAudioSinkClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_opensles_sink_set_property;
  gobject_class->get_property = gst_opensles_sink_get_property;

  g_object_class_install_property (gobject_class, PROP_VOLUME,
      g_param_spec_double ("volume", "Volume", "Volume of this stream",
          0, 1.0, 1.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MUTE,
      g_param_spec_boolean ("mute", "Mute", "Mute state of this stream",
          DEFAULT_MUTE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstbaseaudiosink_class->create_ringbuffer =
      GST_DEBUG_FUNCPTR (gst_opensles_sink_create_ringbuffer);
}

static void
gst_opensles_sink_init (GstOpenSLESSink * sink, GstOpenSLESSinkClass * gclass)
{
  sink->volume = DEFAULT_VOLUME;
  sink->mute = DEFAULT_MUTE;

  _opensles_query_capabilities (sink);

  gst_base_audio_sink_set_provide_clock (GST_BASE_AUDIO_SINK (sink), TRUE);
}
