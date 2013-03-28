/*
 * Copyright (C) 2008 Ole André Vadla Ravnås <ole.andre.ravnas@tandberg.com>
 * Copyright (C) 2013 Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 * SECTION:element-wasapisink
 *
 * Provides audio playback using the Windows Audio Session API available with
 * Vista and newer.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch-1.0 -v audiotestsrc samplesperbuffer=160 ! wasapisink
 * ]| Generate 20 ms buffers and render to the default audio device.
 * </refsect2>
 */
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "gstwasapisink.h"

GST_DEBUG_CATEGORY_STATIC (gst_wasapi_sink_debug);
#define GST_CAT_DEFAULT gst_wasapi_sink_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, "
        "format = (string) S16LE, "
        "layout = (string) interleaved, "
        "rate = (int) 44100, " "channels = (int) 2"));

static void gst_wasapi_sink_dispose (GObject * object);
static void gst_wasapi_sink_finalize (GObject * object);

static GstCaps *gst_wasapi_sink_get_caps (GstBaseSink * bsink,
    GstCaps * filter);
static gboolean gst_wasapi_sink_prepare (GstAudioSink * asink,
    GstAudioRingBufferSpec * spec);
static gboolean gst_wasapi_sink_unprepare (GstAudioSink * asink);
static gboolean gst_wasapi_sink_open (GstAudioSink * asink);
static gboolean gst_wasapi_sink_close (GstAudioSink * asink);
static gint gst_wasapi_sink_write (GstAudioSink * asink,
    gpointer data, guint length);
static guint gst_wasapi_sink_delay (GstAudioSink * asink);
static void gst_wasapi_sink_reset (GstAudioSink * asink);

G_DEFINE_TYPE (GstWasapiSink, gst_wasapi_sink, GST_TYPE_AUDIO_SINK);

static void
gst_wasapi_sink_class_init (GstWasapiSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstBaseSinkClass *gstbasesink_class = GST_BASE_SINK_CLASS (klass);
  GstAudioSinkClass *gstaudiosink_class = GST_AUDIO_SINK_CLASS (klass);

  gobject_class->dispose = gst_wasapi_sink_dispose;
  gobject_class->finalize = gst_wasapi_sink_finalize;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_set_static_metadata (gstelement_class, "WasapiSrc",
      "Sink/Audio",
      "Stream audio to an audio capture device through WASAPI",
      "Ole André Vadla Ravnås <ole.andre.ravnas@tandberg.com>");

  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_wasapi_sink_get_caps);

  gstaudiosink_class->prepare = GST_DEBUG_FUNCPTR (gst_wasapi_sink_prepare);
  gstaudiosink_class->unprepare = GST_DEBUG_FUNCPTR (gst_wasapi_sink_unprepare);
  gstaudiosink_class->open = GST_DEBUG_FUNCPTR (gst_wasapi_sink_open);
  gstaudiosink_class->close = GST_DEBUG_FUNCPTR (gst_wasapi_sink_close);
  gstaudiosink_class->write = GST_DEBUG_FUNCPTR (gst_wasapi_sink_write);
  gstaudiosink_class->delay = GST_DEBUG_FUNCPTR (gst_wasapi_sink_delay);
  gstaudiosink_class->reset = GST_DEBUG_FUNCPTR (gst_wasapi_sink_reset);

  GST_DEBUG_CATEGORY_INIT (gst_wasapi_sink_debug, "wasapisink",
      0, "Windows audio session API sink");
}

static void
gst_wasapi_sink_init (GstWasapiSink * self)
{
  self->event_handle = CreateEvent (NULL, FALSE, FALSE, NULL);

  CoInitialize (NULL);
}

static void
gst_wasapi_sink_dispose (GObject * object)
{
  GstWasapiSink *self = GST_WASAPI_SINK (object);

  if (self->event_handle != NULL) {
    CloseHandle (self->event_handle);
    self->event_handle = NULL;
  }

  G_OBJECT_CLASS (gst_wasapi_sink_parent_class)->dispose (object);
}

static void
gst_wasapi_sink_finalize (GObject * object)
{
  CoUninitialize ();

  G_OBJECT_CLASS (gst_wasapi_sink_parent_class)->finalize (object);
}

static GstCaps *
gst_wasapi_sink_get_caps (GstBaseSink * bsink, GstCaps * filter)
{
  /* FIXME: Implement */
  return NULL;
}

static gboolean
gst_wasapi_sink_open (GstAudioSink * asink)
{
  GstWasapiSink *self = GST_WASAPI_SINK (asink);
  gboolean res = FALSE;
  IAudioClient *client = NULL;

  if (!gst_wasapi_util_get_default_device_client (GST_ELEMENT (self), FALSE,
          &client)) {
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ, (NULL),
        ("Failed to get default device"));
    goto beach;
  }

  self->client = client;
  res = TRUE;

beach:

  return res;
}

static gboolean
gst_wasapi_sink_close (GstAudioSink * asink)
{
  GstWasapiSink *self = GST_WASAPI_SINK (asink);

  if (self->client != NULL) {
    IUnknown_Release (self->client);
    self->client = NULL;
  }

  return TRUE;
}

static gboolean
gst_wasapi_sink_prepare (GstAudioSink * asink, GstAudioRingBufferSpec * spec)
{
  GstWasapiSink *self = GST_WASAPI_SINK (asink);
  gboolean res = FALSE;
  HRESULT hr;
  REFERENCE_TIME latency_rt, def_period, min_period;
  WAVEFORMATEXTENSIBLE format;
  IAudioRenderClient *render_client = NULL;

  hr = IAudioClient_GetDevicePeriod (self->client, &def_period, &min_period);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (self, "IAudioClient::GetDevicePeriod () failed");
    goto beach;
  }

  gst_wasapi_util_audio_info_to_waveformatex (&spec->info, &format);
  self->info = spec->info;

  hr = IAudioClient_Initialize (self->client, AUDCLNT_SHAREMODE_SHARED,
      AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
      spec->buffer_time / 100, 0, (WAVEFORMATEX *) & format, NULL);
  if (hr != S_OK) {
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ, (NULL),
        ("IAudioClient::Initialize () failed: %s",
            gst_wasapi_util_hresult_to_string (hr)));
    goto beach;
  }

  hr = IAudioClient_GetStreamLatency (self->client, &latency_rt);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (self, "IAudioClient::GetStreamLatency () failed");
    goto beach;
  }

  GST_INFO_OBJECT (self, "default period: %d (%d ms), "
      "minimum period: %d (%d ms), "
      "latency: %d (%d ms)",
      (guint32) def_period, (guint32) def_period / 10000,
      (guint32) min_period, (guint32) min_period / 10000,
      (guint32) latency_rt, (guint32) latency_rt / 10000);

  /* FIXME: What to do with the latency? */

  hr = IAudioClient_SetEventHandle (self->client, self->event_handle);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (self, "IAudioClient::SetEventHandle () failed");
    goto beach;
  }

  if (!gst_wasapi_util_get_render_client (GST_ELEMENT (self), self->client,
          &render_client)) {
    goto beach;
  }

  hr = IAudioClient_Start (self->client);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (self, "IAudioClient::Start failed");
    goto beach;
  }

  self->render_client = render_client;
  render_client = NULL;

  res = TRUE;

beach:
  if (render_client != NULL)
    IUnknown_Release (render_client);

  return res;
}

static gboolean
gst_wasapi_sink_unprepare (GstAudioSink * asink)
{
  GstWasapiSink *self = GST_WASAPI_SINK (asink);

  if (self->client != NULL) {
    IAudioClient_Stop (self->client);
  }

  if (self->render_client != NULL) {
    IUnknown_Release (self->render_client);
    self->render_client = NULL;
  }

  return TRUE;
}

static gint
gst_wasapi_sink_write (GstAudioSink * asink, gpointer data, guint length)
{
  GstWasapiSink *self = GST_WASAPI_SINK (asink);
  HRESULT hr;
  gint16 *dst = NULL;
  guint nsamples;

  nsamples = length / self->info.bpf;

  WaitForSingleObject (self->event_handle, INFINITE);

  hr = IAudioRenderClient_GetBuffer (self->render_client, nsamples,
      (BYTE **) & dst);
  if (hr != S_OK) {
    GST_ELEMENT_ERROR (self, RESOURCE, WRITE, (NULL),
        ("IAudioRenderClient::GetBuffer () failed: %s",
            gst_wasapi_util_hresult_to_string (hr)));
    length = 0;
    goto beach;
  }

  memcpy (dst, data, length);

  hr = IAudioRenderClient_ReleaseBuffer (self->render_client, nsamples, 0);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (self, "IAudioRenderClient::ReleaseBuffer () failed: %s",
        gst_wasapi_util_hresult_to_string (hr));
    length = 0;
    goto beach;
  }

beach:

  return length;
}

static guint
gst_wasapi_sink_delay (GstAudioSink * asink)
{
  /* FIXME: Implement */
  return 0;
}

static void
gst_wasapi_sink_reset (GstAudioSink * asink)
{
  GstWasapiSink *self = GST_WASAPI_SINK (asink);
  HRESULT hr;

  if (self->client) {
    hr = IAudioClient_Stop (self->client);
    if (hr != S_OK) {
      GST_ERROR_OBJECT (self, "IAudioClient::Stop () failed: %s",
          gst_wasapi_util_hresult_to_string (hr));
      return;
    }

    hr = IAudioClient_Reset (self->client);
    if (hr != S_OK) {
      GST_ERROR_OBJECT (self, "IAudioClient::Reset () failed: %s",
          gst_wasapi_util_hresult_to_string (hr));
      return;
    }
  }
}
