/*
 * Copyright (C) 2008 Ole André Vadla Ravnås <ole.andre.ravnas@tandberg.com>
 * Copyright (C) 2013 Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (C) 2018 Centricular Ltd.
 *   Author: Nirbheek Chauhan <nirbheek@centricular.com>
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
 * @title: wasapisink
 *
 * Provides audio playback using the Windows Audio Session API available with
 * Vista and newer.
 *
 * ## Example pipelines
 * |[
 * gst-launch-1.0 -v audiotestsrc samplesperbuffer=160 ! wasapisink
 * ]| Generate 20 ms buffers and render to the default audio device.
 *
 */
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "gstwasapisink.h"

#include <mmdeviceapi.h>

GST_DEBUG_CATEGORY_STATIC (gst_wasapi_sink_debug);
#define GST_CAT_DEFAULT gst_wasapi_sink_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_WASAPI_STATIC_CAPS));

#define DEFAULT_ROLE    GST_WASAPI_DEVICE_ROLE_CONSOLE
#define DEFAULT_MUTE    FALSE

enum
{
  PROP_0,
  PROP_ROLE,
  PROP_MUTE,
  PROP_DEVICE
};

static void gst_wasapi_sink_dispose (GObject * object);
static void gst_wasapi_sink_finalize (GObject * object);
static void gst_wasapi_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_wasapi_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

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

#define gst_wasapi_sink_parent_class parent_class
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
  gobject_class->set_property = gst_wasapi_sink_set_property;
  gobject_class->get_property = gst_wasapi_sink_get_property;

  g_object_class_install_property (gobject_class,
      PROP_ROLE,
      g_param_spec_enum ("role", "Role",
          "Role of the device: communications, multimedia, etc",
          GST_WASAPI_DEVICE_TYPE_ROLE, DEFAULT_ROLE, G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class,
      PROP_MUTE,
      g_param_spec_boolean ("mute", "Mute", "Mute state of this stream",
          DEFAULT_MUTE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));

  g_object_class_install_property (gobject_class,
      PROP_DEVICE,
      g_param_spec_string ("device", "Device",
          "WASAPI playback device as a GUID string",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (gstelement_class, &sink_template);
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

  if (self->client != NULL) {
    IUnknown_Release (self->client);
    self->client = NULL;
  }

  if (self->render_client != NULL) {
    IUnknown_Release (self->render_client);
    self->render_client = NULL;
  }

  G_OBJECT_CLASS (gst_wasapi_sink_parent_class)->dispose (object);
}

static void
gst_wasapi_sink_finalize (GObject * object)
{
  GstWasapiSink *self = GST_WASAPI_SINK (object);

  g_clear_pointer (&self->mix_format, CoTaskMemFree);

  CoUninitialize ();

  if (self->cached_caps != NULL) {
    gst_caps_unref (self->cached_caps);
    self->cached_caps = NULL;
  }

  g_clear_pointer (&self->positions, g_free);
  g_clear_pointer (&self->device, g_free);
  self->mute = FALSE;

  G_OBJECT_CLASS (gst_wasapi_sink_parent_class)->finalize (object);
}

static void
gst_wasapi_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstWasapiSink *self = GST_WASAPI_SINK (object);

  switch (prop_id) {
    case PROP_ROLE:
      self->role = gst_wasapi_device_role_to_erole (g_value_get_enum (value));
      break;
    case PROP_MUTE:
      self->mute = g_value_get_boolean (value);
      break;
    case PROP_DEVICE:
    {
      const gchar *device = g_value_get_string (value);
      g_free (self->device);
      self->device =
          device ? g_utf8_to_utf16 (device, -1, NULL, NULL, NULL) : NULL;
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_wasapi_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstWasapiSink *self = GST_WASAPI_SINK (object);

  switch (prop_id) {
    case PROP_ROLE:
      g_value_set_enum (value, gst_wasapi_erole_to_device_role (self->role));
      break;
    case PROP_MUTE:
      g_value_set_boolean (value, self->mute);
      break;
    case PROP_DEVICE:
      g_value_take_string (value, self->device ?
          g_utf16_to_utf8 (self->device, -1, NULL, NULL, NULL) : NULL);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstCaps *
gst_wasapi_sink_get_caps (GstBaseSink * bsink, GstCaps * filter)
{
  GstWasapiSink *self = GST_WASAPI_SINK (bsink);
  WAVEFORMATEX *format = NULL;
  GstCaps *caps = NULL;
  HRESULT hr;

  GST_DEBUG_OBJECT (self, "entering get caps");

  if (self->cached_caps) {
    caps = gst_caps_ref (self->cached_caps);
  } else {
    GstCaps *template_caps;

    template_caps = gst_pad_get_pad_template_caps (bsink->sinkpad);

    if (!self->client)
      gst_wasapi_sink_open (GST_AUDIO_SINK (bsink));

    hr = IAudioClient_GetMixFormat (self->client, &format);
    if (hr != S_OK || format == NULL) {
      GST_ELEMENT_ERROR (self, STREAM, FORMAT, (NULL),
          ("GetMixFormat failed: %s", gst_wasapi_util_hresult_to_string (hr)));
      goto out;
    }

    gst_wasapi_util_parse_waveformatex ((WAVEFORMATEXTENSIBLE *) format,
        template_caps, &caps, &self->positions);
    if (caps == NULL) {
      GST_ELEMENT_ERROR (self, STREAM, FORMAT, (NULL), ("unknown format"));
      goto out;
    }

    {
      gchar *pos_str = gst_audio_channel_positions_to_string (self->positions,
          format->nChannels);
      GST_INFO_OBJECT (self, "positions are: %s", pos_str);
      g_free (pos_str);
    }

    self->mix_format = format;
    gst_caps_replace (&self->cached_caps, caps);
    gst_caps_unref (template_caps);
  }

  if (filter) {
    GstCaps *filtered =
        gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    caps = filtered;
  }

  GST_DEBUG_OBJECT (self, "returning caps %" GST_PTR_FORMAT, caps);

out:
  return caps;
}

static gboolean
gst_wasapi_sink_open (GstAudioSink * asink)
{
  GstWasapiSink *self = GST_WASAPI_SINK (asink);
  gboolean res = FALSE;
  IAudioClient *client = NULL;

  GST_DEBUG_OBJECT (self, "opening device");

  if (self->client)
    return TRUE;

  if (!gst_wasapi_util_get_device_client (GST_ELEMENT (self), FALSE,
          self->role, self->device, &client)) {
    if (!self->device)
      GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ, (NULL),
          ("Failed to get default device"));
    else
      GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ, (NULL),
          ("Failed to open device %S", self->device));
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
  REFERENCE_TIME latency_rt;
  IAudioRenderClient *render_client = NULL;

  hr = IAudioClient_Initialize (self->client, AUDCLNT_SHAREMODE_SHARED,
      AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
      spec->buffer_time * 10, 0, self->mix_format, NULL);
  if (hr != S_OK) {
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ, (NULL),
        ("IAudioClient::Initialize () failed: %s",
            gst_wasapi_util_hresult_to_string (hr)));
    goto beach;
  }

  /* Get latency for logging */
  hr = IAudioClient_GetStreamLatency (self->client, &latency_rt);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (self, "IAudioClient::GetStreamLatency failed");
    goto beach;
  }
  GST_INFO_OBJECT (self, "wasapi stream latency: %" G_GINT64_FORMAT " (%"
      G_GINT64_FORMAT "ms)", latency_rt, latency_rt / 10000);

  /* Set the event handler which will trigger writes */
  hr = IAudioClient_SetEventHandle (self->client, self->event_handle);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (self, "IAudioClient::SetEventHandle failed");
    goto beach;
  }

  /* Total size of the allocated buffer that we will write to
   * XXX: Will this ever change while playing? */
  hr = IAudioClient_GetBufferSize (self->client, &self->buffer_frame_count);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (self, "IAudioClient::GetBufferSize failed");
    goto beach;
  }
  GST_INFO_OBJECT (self, "frame count is %i, blockAlign is %i, "
      "buffer_time is %" G_GINT64_FORMAT, self->buffer_frame_count,
      self->mix_format->nBlockAlign, spec->buffer_time);

  /* Get render sink client and start it up */
  if (!gst_wasapi_util_get_render_client (GST_ELEMENT (self), self->client,
          &render_client)) {
    goto beach;
  }

  GST_INFO_OBJECT (self, "got render client");

  hr = IAudioClient_Start (self->client);
  if (hr != S_OK) {
    GST_ERROR_OBJECT (self, "IAudioClient::Start failed");
    goto beach;
  }

  self->render_client = render_client;
  render_client = NULL;

  gst_audio_ring_buffer_set_channel_positions (
      GST_AUDIO_BASE_SINK (self)->ringbuffer, self->positions);

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
  guint pending = length;

  while (pending > 0) {
    guint have_frames, can_frames, n_frames, n_frames_padding, write_len;

    /* We have N frames to be written out */
    have_frames = pending / (self->mix_format->nBlockAlign);

    WaitForSingleObject (self->event_handle, INFINITE);

    /* Frames the card hasn't rendered yet */
    hr = IAudioClient_GetCurrentPadding (self->client, &n_frames_padding);
    if (hr != S_OK) {
      GST_ERROR_OBJECT (self, "IAudioClient::GetCurrentPadding failed: %s",
          gst_wasapi_util_hresult_to_string (hr));
      length = 0;
      goto beach;
    }

    /* We can write out these many frames */
    can_frames = self->buffer_frame_count - n_frames_padding;

    /* We will write out these many frames, and this much length */
    n_frames = MIN (can_frames, have_frames);
    write_len = n_frames * self->mix_format->nBlockAlign;

    GST_TRACE_OBJECT (self, "total: %i, unread: %i, have: %i (%i bytes), "
        "will write: %i (%i bytes)", self->buffer_frame_count, n_frames_padding,
        have_frames, pending, n_frames, write_len);

    hr = IAudioRenderClient_GetBuffer (self->render_client, n_frames,
        (BYTE **) & dst);
    if (hr != S_OK) {
      GST_ELEMENT_ERROR (self, RESOURCE, WRITE, (NULL),
          ("IAudioRenderClient::GetBuffer failed: %s",
              gst_wasapi_util_hresult_to_string (hr)));
      length = 0;
      goto beach;
    }

    memcpy (dst, data, write_len);

    hr = IAudioRenderClient_ReleaseBuffer (self->render_client, n_frames,
        self->mute ? AUDCLNT_BUFFERFLAGS_SILENT : 0);
    if (hr != S_OK) {
      GST_ERROR_OBJECT (self, "IAudioRenderClient::ReleaseBuffer failed: %s",
          gst_wasapi_util_hresult_to_string (hr));
      length = 0;
      goto beach;
    }

    pending -= write_len;
  }

beach:

  return length;
}

static guint
gst_wasapi_sink_delay (GstAudioSink * asink)
{
  GstWasapiSink *self = GST_WASAPI_SINK (asink);
  guint delay = 0;
  HRESULT hr;

  hr = IAudioClient_GetCurrentPadding (self->client, &delay);
  if (hr != S_OK) {
    GST_ELEMENT_ERROR (self, RESOURCE, READ, (NULL),
        ("IAudioClient::GetCurrentPadding failed %s",
            gst_wasapi_util_hresult_to_string (hr)));
  }

  return delay;
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
