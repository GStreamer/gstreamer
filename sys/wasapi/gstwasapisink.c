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
 * SECTION:element-wasapisink
 *
 * Provides audio playback using the Windows Audio Session API available with
 * Vista and newer.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch-0.10 -v audiotestsrc samplesperbuffer=160 ! wasapisink
 * ]| Generate 20 ms buffers and render to the default audio device.
 * </refsect2>
 */

#include "gstwasapisink.h"

GST_DEBUG_CATEGORY_STATIC (gst_wasapi_sink_debug);
#define GST_CAT_DEFAULT gst_wasapi_sink_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) 8000, "
        "channels = (int) 1, "
        "signed = (boolean) TRUE, "
        "endianness = (int) " G_STRINGIFY (G_BYTE_ORDER)));

static void gst_wasapi_sink_dispose (GObject * object);
static void gst_wasapi_sink_finalize (GObject * object);

static void gst_wasapi_sink_get_times (GstBaseSink * sink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end);
static gboolean gst_wasapi_sink_start (GstBaseSink * sink);
static gboolean gst_wasapi_sink_stop (GstBaseSink * sink);
static GstFlowReturn gst_wasapi_sink_render (GstBaseSink * sink,
    GstBuffer * buffer);

GST_BOILERPLATE (GstWasapiSink, gst_wasapi_sink, GstBaseSink,
    GST_TYPE_BASE_SINK);

static void
gst_wasapi_sink_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_add_static_pad_template (element_class,
      &sink_template);
  gst_element_class_set_details_simple (element_class, "WasapiSrc",
      "Sink/Audio",
      "Stream audio to an audio capture device through WASAPI",
      "Ole André Vadla Ravnås <ole.andre.ravnas@tandberg.com>");
}

static void
gst_wasapi_sink_class_init (GstWasapiSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseSinkClass *gstbasesink_class = GST_BASE_SINK_CLASS (klass);

  gobject_class->dispose = gst_wasapi_sink_dispose;
  gobject_class->finalize = gst_wasapi_sink_finalize;

  gstbasesink_class->get_times = gst_wasapi_sink_get_times;
  gstbasesink_class->start = gst_wasapi_sink_start;
  gstbasesink_class->stop = gst_wasapi_sink_stop;
  gstbasesink_class->render = gst_wasapi_sink_render;

  GST_DEBUG_CATEGORY_INIT (gst_wasapi_sink_debug, "wasapisink",
      0, "Windows audio session API sink");
}

static void
gst_wasapi_sink_init (GstWasapiSink * self, GstWasapiSinkClass * gclass)
{
  self->rate = 8000;
  self->buffer_time = 20 * GST_MSECOND;
  self->period_time = 20 * GST_MSECOND;
  self->latency = GST_CLOCK_TIME_NONE;

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

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_wasapi_sink_finalize (GObject * object)
{
  GstWasapiSink *self = GST_WASAPI_SINK (object);

  CoUninitialize ();

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_wasapi_sink_get_times (GstBaseSink * sink,
    GstBuffer * buffer, GstClockTime * start, GstClockTime * end)
{
  GstWasapiSink *self = GST_WASAPI_SINK (sink);

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer)) {
    *start = GST_BUFFER_TIMESTAMP (buffer);

    if (GST_BUFFER_DURATION_IS_VALID (buffer)) {
      *end = *start + GST_BUFFER_DURATION (buffer);
    } else {
      *end = *start + self->buffer_time;
    }

    *start += self->latency;
    *end += self->latency;
  }
}

static gboolean
gst_wasapi_sink_start (GstBaseSink * sink)
{
  GstWasapiSink *self = GST_WASAPI_SINK (sink);
  gboolean res = FALSE;
  IAudioClient *client = NULL;
  HRESULT hr;
  IAudioRenderClient *render_client = NULL;

  if (!gst_wasapi_util_get_default_device_client (GST_ELEMENT (self),
          FALSE, self->rate, self->buffer_time, self->period_time,
          AUDCLNT_STREAMFLAGS_EVENTCALLBACK, &client, &self->latency))
    goto beach;

  hr = IAudioClient_SetEventHandle (client, self->event_handle);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (self, "IAudioClient::SetEventHandle () failed");
    goto beach;
  }

  hr = IAudioClient_GetService (client, &IID_IAudioRenderClient,
      &render_client);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (self, "IAudioClient::GetService "
        "(IID_IAudioRenderClient) failed");
    goto beach;
  }

  hr = IAudioClient_Start (client);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (self, "IAudioClient::Start failed");
    goto beach;
  }

  self->client = client;
  self->render_client = render_client;

  res = TRUE;

beach:
  if (!res) {
    if (render_client != NULL)
      IUnknown_Release (render_client);

    if (client != NULL)
      IUnknown_Release (client);
  }

  return res;
}

static gboolean
gst_wasapi_sink_stop (GstBaseSink * sink)
{
  GstWasapiSink *self = GST_WASAPI_SINK (sink);

  if (self->client != NULL) {
    IAudioClient_Stop (self->client);
  }

  if (self->render_client != NULL) {
    IUnknown_Release (self->render_client);
    self->render_client = NULL;
  }

  if (self->client != NULL) {
    IUnknown_Release (self->client);
    self->client = NULL;
  }

  return TRUE;
}

static GstFlowReturn
gst_wasapi_sink_render (GstBaseSink * sink, GstBuffer * buffer)
{
  GstWasapiSink *self = GST_WASAPI_SINK (sink);
  GstFlowReturn ret = GST_FLOW_OK;
  HRESULT hr;
  gint16 *src = (gint16 *) GST_BUFFER_DATA (buffer);
  gint16 *dst = NULL;
  guint nsamples = GST_BUFFER_SIZE (buffer) / sizeof (gint16);
  guint i;

  WaitForSingleObject (self->event_handle, INFINITE);

  hr = IAudioRenderClient_GetBuffer (self->render_client, nsamples,
      (BYTE **) & dst);
  if (hr != S_OK) {
    GST_ELEMENT_ERROR (self, RESOURCE, WRITE, (NULL),
        ("IAudioRenderClient::GetBuffer () failed: %s",
            gst_wasapi_util_hresult_to_string (hr)));
    ret = GST_FLOW_ERROR;
    goto beach;
  }

  for (i = 0; i < nsamples; i++) {
    dst[0] = *src;
    dst[1] = *src;

    src++;
    dst += 2;
  }

  hr = IAudioRenderClient_ReleaseBuffer (self->render_client, nsamples, 0);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (self, "IAudioRenderClient::ReleaseBuffer () failed: %s",
        gst_wasapi_util_hresult_to_string (hr));
    ret = GST_FLOW_ERROR;
    goto beach;
  }

beach:
  return ret;
}
