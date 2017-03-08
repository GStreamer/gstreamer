/*
 * Copyright (C) 2008 Ole André Vadla Ravnås <ole.andre.ravnas@tandberg.com>
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
 * SECTION:element-wasapisrc
 * @title: wasapisrc
 *
 * Provides audio capture from the Windows Audio Session API available with
 * Vista and newer.
 *
 * ## Example pipelines
 * |[
 * gst-launch-1.0 -v wasapisrc ! fakesink
 * ]| Capture from the default audio device and render to fakesink.
 *
 */
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "gstwasapisrc.h"

GST_DEBUG_CATEGORY_STATIC (gst_wasapi_src_debug);
#define GST_CAT_DEFAULT gst_wasapi_src_debug

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, "
        "format = (string) S16LE, "
        "layout = (string) interleaved, "
        "rate = (int) 44100, " "channels = (int) 1"));

static void gst_wasapi_src_dispose (GObject * object);
static void gst_wasapi_src_finalize (GObject * object);

static GstCaps *gst_wasapi_src_get_caps (GstBaseSrc * bsrc, GstCaps * filter);

static gboolean gst_wasapi_src_open (GstAudioSrc * asrc);
static gboolean gst_wasapi_src_close (GstAudioSrc * asrc);
static gboolean gst_wasapi_src_prepare (GstAudioSrc * asrc,
    GstAudioRingBufferSpec * spec);
static gboolean gst_wasapi_src_unprepare (GstAudioSrc * asrc);
static guint gst_wasapi_src_read (GstAudioSrc * asrc, gpointer data,
    guint length, GstClockTime * timestamp);
static guint gst_wasapi_src_delay (GstAudioSrc * asrc);
static void gst_wasapi_src_reset (GstAudioSrc * asrc);

static GstClockTime gst_wasapi_src_get_time (GstClock * clock,
    gpointer user_data);

G_DEFINE_TYPE (GstWasapiSrc, gst_wasapi_src, GST_TYPE_AUDIO_SRC);

static void
gst_wasapi_src_class_init (GstWasapiSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstBaseSrcClass *gstbasesrc_class = GST_BASE_SRC_CLASS (klass);
  GstAudioSrcClass *gstaudiosrc_class = GST_AUDIO_SRC_CLASS (klass);

  gobject_class->dispose = gst_wasapi_src_dispose;
  gobject_class->finalize = gst_wasapi_src_finalize;

  gst_element_class_add_static_pad_template (gstelement_class, &src_template);
  gst_element_class_set_static_metadata (gstelement_class, "WasapiSrc",
      "Source/Audio",
      "Stream audio from an audio capture device through WASAPI",
      "Ole André Vadla Ravnås <ole.andre.ravnas@tandberg.com>");

  gstbasesrc_class->get_caps = GST_DEBUG_FUNCPTR (gst_wasapi_src_get_caps);

  gstaudiosrc_class->open = GST_DEBUG_FUNCPTR (gst_wasapi_src_open);
  gstaudiosrc_class->close = GST_DEBUG_FUNCPTR (gst_wasapi_src_close);
  gstaudiosrc_class->read = GST_DEBUG_FUNCPTR (gst_wasapi_src_read);
  gstaudiosrc_class->prepare = GST_DEBUG_FUNCPTR (gst_wasapi_src_prepare);
  gstaudiosrc_class->unprepare = GST_DEBUG_FUNCPTR (gst_wasapi_src_unprepare);
  gstaudiosrc_class->delay = GST_DEBUG_FUNCPTR (gst_wasapi_src_delay);
  gstaudiosrc_class->reset = GST_DEBUG_FUNCPTR (gst_wasapi_src_reset);

  GST_DEBUG_CATEGORY_INIT (gst_wasapi_src_debug, "wasapisrc",
      0, "Windows audio session API source");
}

static void
gst_wasapi_src_init (GstWasapiSrc * self)
{
  /* override with a custom clock */
  if (GST_AUDIO_BASE_SRC (self)->clock)
    gst_object_unref (GST_AUDIO_BASE_SRC (self)->clock);

  GST_AUDIO_BASE_SRC (self)->clock = gst_audio_clock_new ("GstWasapiSrcClock",
      gst_wasapi_src_get_time, gst_object_ref (self),
      (GDestroyNotify) gst_object_unref);

  self->event_handle = CreateEvent (NULL, FALSE, FALSE, NULL);

  CoInitialize (NULL);
}

static void
gst_wasapi_src_dispose (GObject * object)
{
  GstWasapiSrc *self = GST_WASAPI_SRC (object);

  if (self->event_handle != NULL) {
    CloseHandle (self->event_handle);
    self->event_handle = NULL;
  }

  G_OBJECT_CLASS (gst_wasapi_src_parent_class)->dispose (object);
}

static void
gst_wasapi_src_finalize (GObject * object)
{
  CoUninitialize ();

  G_OBJECT_CLASS (gst_wasapi_src_parent_class)->finalize (object);
}

static GstCaps *
gst_wasapi_src_get_caps (GstBaseSrc * bsrc, GstCaps * filter)
{
  /* TODO: Implement */
  return NULL;
}

static gboolean
gst_wasapi_src_open (GstAudioSrc * asrc)
{
  GstWasapiSrc *self = GST_WASAPI_SRC (asrc);
  gboolean res = FALSE;
  IAudioClient *client = NULL;

  if (!gst_wasapi_util_get_default_device_client (GST_ELEMENT (self), TRUE,
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
gst_wasapi_src_close (GstAudioSrc * asrc)
{
  GstWasapiSrc *self = GST_WASAPI_SRC (asrc);

  if (self->client != NULL) {
    IUnknown_Release (self->client);
    self->client = NULL;
  }

  return TRUE;
}

static gboolean
gst_wasapi_src_prepare (GstAudioSrc * asrc, GstAudioRingBufferSpec * spec)
{
  GstWasapiSrc *self = GST_WASAPI_SRC (asrc);
  gboolean res = FALSE;
  IAudioClock *client_clock = NULL;
  guint64 client_clock_freq = 0;
  IAudioCaptureClient *capture_client = NULL;
  REFERENCE_TIME latency_rt, def_period, min_period;
  WAVEFORMATEXTENSIBLE format;
  HRESULT hr;

  hr = IAudioClient_GetDevicePeriod (self->client, &def_period, &min_period);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (self, "IAudioClient::GetDevicePeriod () failed");
    goto beach;
  }

  gst_wasapi_util_audio_info_to_waveformatex (&spec->info, &format);
  self->info = spec->info;

  hr = IAudioClient_Initialize (self->client, AUDCLNT_SHAREMODE_SHARED,
      AUDCLNT_STREAMFLAGS_EVENTCALLBACK, spec->buffer_time / 100, 0,
      (WAVEFORMATEX *) & format, NULL);
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

  if (!gst_wasapi_util_get_clock (GST_ELEMENT (self), self->client,
          &client_clock)) {
    goto beach;
  }

  hr = IAudioClock_GetFrequency (client_clock, &client_clock_freq);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (self, "IAudioClock::GetFrequency () failed");
    goto beach;
  }

  if (!gst_wasapi_util_get_capture_client (GST_ELEMENT (self), self->client,
          &capture_client)) {
    goto beach;
  }

  hr = IAudioClient_Start (self->client);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (self, "IAudioClient::Start failed");
    goto beach;
  }

  self->client_clock = client_clock;
  self->client_clock_freq = client_clock_freq;
  self->capture_client = capture_client;

  res = TRUE;

beach:
  if (!res) {
    if (capture_client != NULL)
      IUnknown_Release (capture_client);

    if (client_clock != NULL)
      IUnknown_Release (client_clock);
  }

  return res;
}

static gboolean
gst_wasapi_src_unprepare (GstAudioSrc * asrc)
{
  GstWasapiSrc *self = GST_WASAPI_SRC (asrc);

  if (self->client != NULL) {
    IAudioClient_Stop (self->client);
  }

  if (self->capture_client != NULL) {
    IUnknown_Release (self->capture_client);
    self->capture_client = NULL;
  }

  if (self->client_clock != NULL) {
    IUnknown_Release (self->client_clock);
    self->client_clock = NULL;
  }

  return TRUE;
}

static guint
gst_wasapi_src_read (GstAudioSrc * asrc, gpointer data, guint length,
    GstClockTime * timestamp)
{
  GstWasapiSrc *self = GST_WASAPI_SRC (asrc);
  HRESULT hr;
  gint16 *samples = NULL;
  guint32 nsamples = 0, length_samples;
  DWORD flags = 0;
  guint64 devpos;
  guint i;
  gint16 *dst;

  WaitForSingleObject (self->event_handle, INFINITE);

  do {
    hr = IAudioCaptureClient_GetBuffer (self->capture_client,
        (BYTE **) & samples, &nsamples, &flags, &devpos, NULL);
  }
  while (hr == AUDCLNT_S_BUFFER_EMPTY);

  if (hr != S_OK) {
    GST_ERROR_OBJECT (self, "IAudioCaptureClient::GetBuffer () failed: %s",
        gst_wasapi_util_hresult_to_string (hr));
    length = 0;
    goto beach;
  }

  if (flags != 0) {
    GST_WARNING_OBJECT (self, "devpos %" G_GUINT64_FORMAT ": flags=0x%08x",
        devpos, (guint) flags);
  }

  length_samples = length / self->info.bpf;
  nsamples = MIN (length_samples, nsamples);
  length = nsamples * self->info.bpf;

  dst = (gint16 *) data;
  for (i = 0; i < nsamples; i++) {
    *dst = *samples;

    samples += 2;
    dst++;
  }

  hr = IAudioCaptureClient_ReleaseBuffer (self->capture_client, nsamples);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (self, "IAudioCaptureClient::ReleaseBuffer () failed: %s",
        gst_wasapi_util_hresult_to_string (hr));
    goto beach;
  }

beach:

  return length;
}

static guint
gst_wasapi_src_delay (GstAudioSrc * asrc)
{
  /* FIXME: Implement */
  return 0;
}

static void
gst_wasapi_src_reset (GstAudioSrc * asrc)
{
  GstWasapiSrc *self = GST_WASAPI_SRC (asrc);
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

static GstClockTime
gst_wasapi_src_get_time (GstClock * clock, gpointer user_data)
{
  GstWasapiSrc *self = GST_WASAPI_SRC (user_data);
  HRESULT hr;
  guint64 devpos;
  GstClockTime result;

  if (G_UNLIKELY (self->client_clock == NULL))
    return GST_CLOCK_TIME_NONE;

  hr = IAudioClock_GetPosition (self->client_clock, &devpos, NULL);
  if (G_UNLIKELY (hr != S_OK))
    return GST_CLOCK_TIME_NONE;

  result = gst_util_uint64_scale_int (devpos, GST_SECOND,
      self->client_clock_freq);

  /*
     GST_DEBUG_OBJECT (self, "devpos = %" G_GUINT64_FORMAT
     " frequency = %" G_GUINT64_FORMAT
     " result = %" G_GUINT64_FORMAT " ms",
     devpos, self->client_clock_freq, GST_TIME_AS_MSECONDS (result));
   */

  return result;
}
