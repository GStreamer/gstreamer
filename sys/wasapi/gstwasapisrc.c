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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-wasapisrc
 *
 * Provides audio capture from the Windows Audio Session API available with
 * Vista and newer.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch-0.10 -v wasapisrc ! fakesink
 * ]| Capture from the default audio device and render to fakesink.
 * </refsect2>
 */

#include "gstwasapisrc.h"
#include <gst/audio/gstaudioclock.h>

GST_DEBUG_CATEGORY_STATIC (gst_wasapi_src_debug);
#define GST_CAT_DEFAULT gst_wasapi_src_debug

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) 8000, "
        "channels = (int) 1, "
        "signed = (boolean) TRUE, "
        "endianness = (int) " G_STRINGIFY (G_BYTE_ORDER)));

static void gst_wasapi_src_dispose (GObject * object);
static void gst_wasapi_src_finalize (GObject * object);

static GstClock *gst_wasapi_src_provide_clock (GstElement * element);

static gboolean gst_wasapi_src_start (GstBaseSrc * src);
static gboolean gst_wasapi_src_stop (GstBaseSrc * src);
static gboolean gst_wasapi_src_query (GstBaseSrc * src, GstQuery * query);

static GstFlowReturn gst_wasapi_src_create (GstPushSrc * src, GstBuffer ** buf);

static GstClockTime gst_wasapi_src_get_time (GstClock * clock,
    gpointer user_data);

GST_BOILERPLATE (GstWasapiSrc, gst_wasapi_src, GstPushSrc, GST_TYPE_PUSH_SRC);

static void
gst_wasapi_src_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_add_static_pad_template (element_class, &src_template);
  gst_element_class_set_details_simple (element_class, "WasapiSrc",
      "Source/Audio",
      "Stream audio from an audio capture device through WASAPI",
      "Ole André Vadla Ravnås <ole.andre.ravnas@tandberg.com>");
}

static void
gst_wasapi_src_class_init (GstWasapiSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstBaseSrcClass *gstbasesrc_class = GST_BASE_SRC_CLASS (klass);
  GstPushSrcClass *gstpushsrc_class = GST_PUSH_SRC_CLASS (klass);

  gobject_class->dispose = gst_wasapi_src_dispose;
  gobject_class->finalize = gst_wasapi_src_finalize;

  gstelement_class->provide_clock = gst_wasapi_src_provide_clock;

  gstbasesrc_class->start = gst_wasapi_src_start;
  gstbasesrc_class->stop = gst_wasapi_src_stop;
  gstbasesrc_class->query = gst_wasapi_src_query;

  gstpushsrc_class->create = gst_wasapi_src_create;

  GST_DEBUG_CATEGORY_INIT (gst_wasapi_src_debug, "wasapisrc",
      0, "Windows audio session API source");
}

static void
gst_wasapi_src_init (GstWasapiSrc * self, GstWasapiSrcClass * gclass)
{
  GstBaseSrc *basesrc = GST_BASE_SRC (self);

  gst_base_src_set_format (basesrc, GST_FORMAT_TIME);
  gst_base_src_set_live (basesrc, TRUE);

  self->rate = 8000;
  self->buffer_time = 20 * GST_MSECOND;
  self->period_time = 20 * GST_MSECOND;
  self->latency = GST_CLOCK_TIME_NONE;
  self->samples_per_buffer = self->rate / (GST_SECOND / self->period_time);

  self->start_time = GST_CLOCK_TIME_NONE;
  self->next_time = GST_CLOCK_TIME_NONE;

#if GST_CHECK_VERSION(0, 10, 31) || (GST_CHECK_VERSION(0, 10, 30) && GST_VERSION_NANO > 0)
  self->clock = gst_audio_clock_new_full ("GstWasapiSrcClock",
      gst_wasapi_src_get_time, gst_object_ref (self),
      (GDestroyNotify) gst_object_unref);
#else
  self->clock = gst_audio_clock_new ("GstWasapiSrcClock",
      gst_wasapi_src_get_time, self);
#endif

  CoInitialize (NULL);
}

static void
gst_wasapi_src_dispose (GObject * object)
{
  GstWasapiSrc *self = GST_WASAPI_SRC (object);

  if (self->clock != NULL) {
    gst_object_unref (self->clock);
    self->clock = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_wasapi_src_finalize (GObject * object)
{
  GstWasapiSrc *self = GST_WASAPI_SRC (object);

  CoUninitialize ();

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstClock *
gst_wasapi_src_provide_clock (GstElement * element)
{
  GstWasapiSrc *self = GST_WASAPI_SRC (element);
  GstClock *clock;

  GST_OBJECT_LOCK (self);

  if (self->client_clock == NULL)
    goto wrong_state;

  clock = GST_CLOCK (gst_object_ref (self->clock));

  GST_OBJECT_UNLOCK (self);
  return clock;

  /* ERRORS */
wrong_state:
  {
    GST_OBJECT_UNLOCK (self);
    GST_DEBUG_OBJECT (self, "IAudioClock not acquired");
    return NULL;
  }
}

static gboolean
gst_wasapi_src_start (GstBaseSrc * src)
{
  GstWasapiSrc *self = GST_WASAPI_SRC (src);
  gboolean res = FALSE;
  IAudioClient *client = NULL;
  IAudioClock *client_clock = NULL;
  guint64 client_clock_freq = 0;
  IAudioCaptureClient *capture_client = NULL;
  HRESULT hr;

  if (!gst_wasapi_util_get_default_device_client (GST_ELEMENT (self),
          TRUE, self->rate, self->buffer_time, self->period_time, 0, &client,
          &self->latency))
    goto beach;

  hr = IAudioClient_GetService (client, &IID_IAudioClock, &client_clock);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (self, "IAudioClient::GetService (IID_IAudioClock) "
        "failed");
    goto beach;
  }

  hr = IAudioClock_GetFrequency (client_clock, &client_clock_freq);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (self, "IAudioClock::GetFrequency () failed");
    goto beach;
  }

  hr = IAudioClient_GetService (client, &IID_IAudioCaptureClient,
      &capture_client);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (self, "IAudioClient::GetService "
        "(IID_IAudioCaptureClient) failed");
    goto beach;
  }

  hr = IAudioClient_Start (client);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (self, "IAudioClient::Start failed");
    goto beach;
  }

  self->client = client;
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

    if (client != NULL)
      IUnknown_Release (client);
  }

  return res;
}

static gboolean
gst_wasapi_src_stop (GstBaseSrc * src)
{
  GstWasapiSrc *self = GST_WASAPI_SRC (src);

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

  if (self->client != NULL) {
    IUnknown_Release (self->client);
    self->client = NULL;
  }

  return TRUE;
}

static gboolean
gst_wasapi_src_query (GstBaseSrc * src, GstQuery * query)
{
  GstWasapiSrc *self = GST_WASAPI_SRC (src);
  gboolean ret = FALSE;

  GST_DEBUG_OBJECT (self, "query for %s",
      gst_query_type_get_name (GST_QUERY_TYPE (query)));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:{
      GstClockTime min_latency, max_latency;

      min_latency = self->latency + self->period_time;
      max_latency = min_latency;

      GST_DEBUG_OBJECT (self, "reporting latency of min %" GST_TIME_FORMAT
          " max %" GST_TIME_FORMAT,
          GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));

      gst_query_set_latency (query, TRUE, min_latency, max_latency);
      ret = TRUE;
      break;
    }

    default:
      ret = GST_BASE_SRC_CLASS (parent_class)->query (src, query);
      break;
  }

  return ret;
}

static GstFlowReturn
gst_wasapi_src_create (GstPushSrc * src, GstBuffer ** buf)
{
  GstWasapiSrc *self = GST_WASAPI_SRC (src);
  GstFlowReturn ret = GST_FLOW_OK;
  GstClock *clock;
  GstClockTime timestamp, duration = self->period_time;
  HRESULT hr;
  gint16 *samples = NULL;
  guint32 nsamples_read = 0, nsamples;
  DWORD flags = 0;
  guint64 devpos;

  GST_OBJECT_LOCK (self);
  clock = GST_ELEMENT_CLOCK (self);
  if (clock != NULL)
    gst_object_ref (clock);
  GST_OBJECT_UNLOCK (self);

  if (clock != NULL && GST_CLOCK_TIME_IS_VALID (self->next_time)) {
    GstClockID id;

    id = gst_clock_new_single_shot_id (clock, self->next_time);
    gst_clock_id_wait (id, NULL);
    gst_clock_id_unref (id);
  }

  do {
    hr = IAudioCaptureClient_GetBuffer (self->capture_client,
        (BYTE **) & samples, &nsamples_read, &flags, &devpos, NULL);
  }
  while (hr == AUDCLNT_S_BUFFER_EMPTY);

  if (hr != S_OK) {
    GST_ERROR_OBJECT (self, "IAudioCaptureClient::GetBuffer () failed: %s",
        gst_wasapi_util_hresult_to_string (hr));
    ret = GST_FLOW_ERROR;
    goto beach;
  }

  if (flags != 0) {
    GST_WARNING_OBJECT (self, "devpos %" G_GUINT64_FORMAT ": flags=0x%08x",
        devpos, flags);
  }

  /* FIXME: Why do we get 1024 sometimes and not a multiple of
   *        samples_per_buffer? Shouldn't WASAPI provide a DISCONT
   *        flag if we read too slow?
   */
  nsamples = nsamples_read;
  g_assert (nsamples >= self->samples_per_buffer);
  if (nsamples > self->samples_per_buffer) {
    GST_WARNING_OBJECT (self,
        "devpos %" G_GUINT64_FORMAT ": got %d samples, expected %d, clipping!",
        devpos, nsamples, self->samples_per_buffer);

    nsamples = self->samples_per_buffer;
  }

  if (clock == NULL || clock == self->clock) {
    timestamp =
        gst_util_uint64_scale (devpos, GST_SECOND, self->client_clock_freq);
  } else {
    GstClockTime base_time;

    timestamp = gst_clock_get_time (clock);

    base_time = GST_ELEMENT_CAST (self)->base_time;
    if (timestamp > base_time)
      timestamp -= base_time;
    else
      timestamp = 0;

    if (timestamp > duration)
      timestamp -= duration;
    else
      timestamp = 0;
  }

  ret = gst_pad_alloc_buffer_and_set_caps (GST_BASE_SRC_PAD (self),
      devpos,
      nsamples * sizeof (gint16), GST_PAD_CAPS (GST_BASE_SRC_PAD (self)), buf);

  if (ret == GST_FLOW_OK) {
    guint i;
    gint16 *dst;

    GST_BUFFER_OFFSET_END (*buf) = devpos + self->samples_per_buffer;
    GST_BUFFER_TIMESTAMP (*buf) = timestamp;
    GST_BUFFER_DURATION (*buf) = duration;

    dst = (gint16 *) GST_BUFFER_DATA (*buf);
    for (i = 0; i < nsamples; i++) {
      *dst = *samples;

      samples += 2;
      dst++;
    }
  }

  hr = IAudioCaptureClient_ReleaseBuffer (self->capture_client, nsamples_read);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (self, "IAudioCaptureClient::ReleaseBuffer () failed: %s",
        gst_wasapi_util_hresult_to_string (hr));
    ret = GST_FLOW_ERROR;
    goto beach;
  }

beach:
  if (clock != NULL)
    gst_object_unref (clock);

  return ret;
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
