/*
 * Copyright (C) 2008 Ole André Vadla Ravnås <ole.andre.ravnas@tandberg.com>
 * Copyright (C) 2013 Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (C) 2018 Centricular Ltd.
 *   Author: Nirbheek Chauhan <nirbheek@centricular.com>
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
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
 * SECTION:element-wasapi2sink
 * @title: wasapi2sink
 *
 * Provides audio playback using the Windows Audio Session API available with
 * Windows 10.
 *
 * ## Example pipelines
 * |[
 * gst-launch-1.0 -v audiotestsrc samplesperbuffer=160 ! wasapi2sink
 * ]| Generate 20 ms buffers and render to the default audio device.
 *
 * |[
 * gst-launch-1.0 -v audiotestsrc samplesperbuffer=160 ! wasapi2sink low-latency=true
 * ]| Same as above, but with the minimum possible latency
 *
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstwasapi2sink.h"
#include "gstwasapi2util.h"
#include "gstwasapi2client.h"

GST_DEBUG_CATEGORY_STATIC (gst_wasapi2_sink_debug);
#define GST_CAT_DEFAULT gst_wasapi2_sink_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_WASAPI2_STATIC_CAPS));

#define DEFAULT_LOW_LATENCY   FALSE
#define DEFAULT_MUTE          FALSE
#define DEFAULT_VOLUME        1.0

#define GST_WASAPI2_SINK_LOCK(s) g_mutex_lock(&(s)->lock)
#define GST_WASAPI2_SINK_UNLOCK(s) g_mutex_unlock(&(s)->lock)

enum
{
  PROP_0,
  PROP_DEVICE,
  PROP_LOW_LATENCY,
  PROP_MUTE,
  PROP_VOLUME,
  PROP_DISPATCHER,
};

struct _GstWasapi2Sink
{
  GstAudioSink parent;

  GstWasapi2Client *client;
  GstCaps *cached_caps;
  gboolean started;

  /* properties */
  gchar *device_id;
  gboolean low_latency;
  gboolean mute;
  gdouble volume;
  gpointer dispatcher;

  gboolean mute_changed;
  gboolean volume_changed;

  /* to protect audioclient from set/get property */
  GMutex lock;
};

static void gst_wasapi2_sink_dispose (GObject * object);
static void gst_wasapi2_sink_finalize (GObject * object);
static void gst_wasapi2_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_wasapi2_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstCaps *gst_wasapi2_sink_get_caps (GstBaseSink * bsink,
    GstCaps * filter);

static gboolean gst_wasapi2_sink_prepare (GstAudioSink * asink,
    GstAudioRingBufferSpec * spec);
static gboolean gst_wasapi2_sink_unprepare (GstAudioSink * asink);
static gboolean gst_wasapi2_sink_open (GstAudioSink * asink);
static gboolean gst_wasapi2_sink_close (GstAudioSink * asink);
static gint gst_wasapi2_sink_write (GstAudioSink * asink,
    gpointer data, guint length);
static guint gst_wasapi2_sink_delay (GstAudioSink * asink);
static void gst_wasapi2_sink_reset (GstAudioSink * asink);

static void gst_wasapi2_sink_set_mute (GstWasapi2Sink * self, gboolean mute);
static gboolean gst_wasapi2_sink_get_mute (GstWasapi2Sink * self);
static void gst_wasapi2_sink_set_volume (GstWasapi2Sink * self, gdouble volume);
static gdouble gst_wasapi2_sink_get_volume (GstWasapi2Sink * self);

#define gst_wasapi2_sink_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstWasapi2Sink, gst_wasapi2_sink, GST_TYPE_AUDIO_SINK,
    G_IMPLEMENT_INTERFACE (GST_TYPE_STREAM_VOLUME, NULL));

static void
gst_wasapi2_sink_class_init (GstWasapi2SinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSinkClass *basesink_class = GST_BASE_SINK_CLASS (klass);
  GstAudioSinkClass *audiosink_class = GST_AUDIO_SINK_CLASS (klass);

  gobject_class->dispose = gst_wasapi2_sink_dispose;
  gobject_class->finalize = gst_wasapi2_sink_finalize;
  gobject_class->set_property = gst_wasapi2_sink_set_property;
  gobject_class->get_property = gst_wasapi2_sink_get_property;

  g_object_class_install_property (gobject_class, PROP_DEVICE,
      g_param_spec_string ("device", "Device",
          "WASAPI playback device as a GUID string",
          NULL, GST_PARAM_MUTABLE_READY | G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_LOW_LATENCY,
      g_param_spec_boolean ("low-latency", "Low latency",
          "Optimize all settings for lowest latency. Always safe to enable.",
          DEFAULT_LOW_LATENCY, GST_PARAM_MUTABLE_READY | G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MUTE,
      g_param_spec_boolean ("mute", "Mute", "Mute state of this stream",
          DEFAULT_MUTE, GST_PARAM_MUTABLE_PLAYING | G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VOLUME,
      g_param_spec_double ("volume", "Volume", "Volume of this stream",
          0.0, 1.0, DEFAULT_VOLUME,
          GST_PARAM_MUTABLE_PLAYING | G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS));

  /**
   * GstWasapi2Sink:dispatcher:
   *
   * ICoreDispatcher COM object used for activating device from UI thread.
   *
   * Since: 1.18
   */
  g_object_class_install_property (gobject_class, PROP_DISPATCHER,
      g_param_spec_pointer ("dispatcher", "Dispatcher",
          "ICoreDispatcher COM object to use. In order for application to ask "
          "permission of audio device, device activation should be running "
          "on UI thread via ICoreDispatcher. This element will increase "
          "the reference count of given ICoreDispatcher and release it after "
          "use. Therefore, caller does not need to consider additional "
          "reference count management",
          GST_PARAM_MUTABLE_READY | G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_set_static_metadata (element_class, "Wasapi2Sink",
      "Sink/Audio/Hardware",
      "Stream audio to an audio capture device through WASAPI",
      "Nirbheek Chauhan <nirbheek@centricular.com>, "
      "Ole André Vadla Ravnås <ole.andre.ravnas@tandberg.com>, "
      "Seungha Yang <seungha@centricular.com>");

  basesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_wasapi2_sink_get_caps);

  audiosink_class->prepare = GST_DEBUG_FUNCPTR (gst_wasapi2_sink_prepare);
  audiosink_class->unprepare = GST_DEBUG_FUNCPTR (gst_wasapi2_sink_unprepare);
  audiosink_class->open = GST_DEBUG_FUNCPTR (gst_wasapi2_sink_open);
  audiosink_class->close = GST_DEBUG_FUNCPTR (gst_wasapi2_sink_close);
  audiosink_class->write = GST_DEBUG_FUNCPTR (gst_wasapi2_sink_write);
  audiosink_class->delay = GST_DEBUG_FUNCPTR (gst_wasapi2_sink_delay);
  audiosink_class->reset = GST_DEBUG_FUNCPTR (gst_wasapi2_sink_reset);

  GST_DEBUG_CATEGORY_INIT (gst_wasapi2_sink_debug, "wasapi2sink",
      0, "Windows audio session API sink");
}

static void
gst_wasapi2_sink_init (GstWasapi2Sink * self)
{
  self->low_latency = DEFAULT_LOW_LATENCY;
  self->mute = DEFAULT_MUTE;
  self->volume = DEFAULT_VOLUME;

  g_mutex_init (&self->lock);
}

static void
gst_wasapi2_sink_dispose (GObject * object)
{
  GstWasapi2Sink *self = GST_WASAPI2_SINK (object);

  GST_WASAPI2_SINK_LOCK (self);
  gst_clear_object (&self->client);
  gst_clear_caps (&self->cached_caps);
  GST_WASAPI2_SINK_UNLOCK (self);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_wasapi2_sink_finalize (GObject * object)
{
  GstWasapi2Sink *self = GST_WASAPI2_SINK (object);

  g_free (self->device_id);
  g_mutex_clear (&self->lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_wasapi2_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstWasapi2Sink *self = GST_WASAPI2_SINK (object);

  switch (prop_id) {
    case PROP_DEVICE:
      g_free (self->device_id);
      self->device_id = g_value_dup_string (value);
      break;
    case PROP_LOW_LATENCY:
      self->low_latency = g_value_get_boolean (value);
      break;
    case PROP_MUTE:
      gst_wasapi2_sink_set_mute (self, g_value_get_boolean (value));
      break;
    case PROP_VOLUME:
      gst_wasapi2_sink_set_volume (self, g_value_get_double (value));
      break;
    case PROP_DISPATCHER:
      self->dispatcher = g_value_get_pointer (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_wasapi2_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstWasapi2Sink *self = GST_WASAPI2_SINK (object);

  switch (prop_id) {
    case PROP_DEVICE:
      g_value_set_string (value, self->device_id);
      break;
    case PROP_LOW_LATENCY:
      g_value_set_boolean (value, self->low_latency);
      break;
    case PROP_MUTE:
      g_value_set_boolean (value, gst_wasapi2_sink_get_mute (self));
      break;
    case PROP_VOLUME:
      g_value_set_double (value, gst_wasapi2_sink_get_volume (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstCaps *
gst_wasapi2_sink_get_caps (GstBaseSink * bsink, GstCaps * filter)
{
  GstWasapi2Sink *self = GST_WASAPI2_SINK (bsink);
  GstCaps *caps = NULL;

  /* In case of UWP, device activation might not be finished yet */
  if (self->client && !gst_wasapi2_client_ensure_activation (self->client)) {
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_WRITE, (NULL),
        ("Failed to activate device"));
    return NULL;
  }

  if (self->client)
    caps = gst_wasapi2_client_get_caps (self->client);

  /* store one caps here so that we can return device caps even if
   * audioclient was closed due to unprepare() */
  if (!self->cached_caps && caps)
    self->cached_caps = gst_caps_ref (caps);

  if (!caps && self->cached_caps)
    caps = gst_caps_ref (self->cached_caps);

  if (!caps)
    caps = gst_pad_get_pad_template_caps (bsink->sinkpad);

  if (filter) {
    GstCaps *filtered =
        gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    caps = filtered;
  }

  GST_DEBUG_OBJECT (self, "returning caps %" GST_PTR_FORMAT, caps);

  return caps;
}

static gboolean
gst_wasapi2_sink_open_unlocked (GstAudioSink * asink)
{
  GstWasapi2Sink *self = GST_WASAPI2_SINK (asink);

  self->client =
      gst_wasapi2_client_new (GST_WASAPI2_CLIENT_DEVICE_CLASS_RENDER,
      self->low_latency, -1, self->device_id, self->dispatcher);

  return ! !self->client;
}

static gboolean
gst_wasapi2_sink_open (GstAudioSink * asink)
{
  GstWasapi2Sink *self = GST_WASAPI2_SINK (asink);
  gboolean ret;

  GST_DEBUG_OBJECT (self, "Opening device");

  GST_WASAPI2_SINK_LOCK (self);
  ret = gst_wasapi2_sink_open_unlocked (asink);
  GST_WASAPI2_SINK_UNLOCK (self);

  if (!ret) {
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_WRITE, (NULL),
        ("Failed to open device"));
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_wasapi2_sink_close (GstAudioSink * asink)
{
  GstWasapi2Sink *self = GST_WASAPI2_SINK (asink);

  GST_WASAPI2_SINK_LOCK (self);

  gst_clear_object (&self->client);
  gst_clear_caps (&self->cached_caps);
  self->started = FALSE;

  GST_WASAPI2_SINK_UNLOCK (self);

  return TRUE;
}

static gboolean
gst_wasapi2_sink_prepare (GstAudioSink * asink, GstAudioRingBufferSpec * spec)
{
  GstWasapi2Sink *self = GST_WASAPI2_SINK (asink);
  GstAudioBaseSink *bsink = GST_AUDIO_BASE_SINK (asink);
  gboolean ret = FALSE;

  GST_WASAPI2_SINK_LOCK (self);
  if (!self->client && !gst_wasapi2_sink_open_unlocked (asink)) {
    GST_ERROR_OBJECT (self, "No audio client was configured");
    goto done;
  }

  if (!gst_wasapi2_client_ensure_activation (self->client)) {
    GST_ERROR_OBJECT (self, "Couldn't activate audio device");
    goto done;
  }

  if (!gst_wasapi2_client_open (self->client, spec, bsink->ringbuffer)) {
    GST_ERROR_OBJECT (self, "Couldn't open audio client");
    goto done;
  }

  /* Set mute and volume here again, maybe when "mute" property was set, audioclient
   * might not be configured at that moment */
  if (self->mute_changed) {
    gst_wasapi2_client_set_mute (self->client, self->mute);
    self->mute_changed = FALSE;
  }

  if (self->volume_changed) {
    gst_wasapi2_client_set_volume (self->client, self->volume);
    self->volume_changed = FALSE;
  }

  /* Will start IAudioClient on the first write request */
  self->started = FALSE;
  ret = TRUE;

done:
  GST_WASAPI2_SINK_UNLOCK (self);

  return ret;
}

static gboolean
gst_wasapi2_sink_unprepare (GstAudioSink * asink)
{
  GstWasapi2Sink *self = GST_WASAPI2_SINK (asink);

  self->started = FALSE;

  /* Will reopen device later prepare() */
  GST_WASAPI2_SINK_LOCK (self);
  if (self->client) {
    gst_wasapi2_client_stop (self->client);
    gst_clear_object (&self->client);
  }
  GST_WASAPI2_SINK_UNLOCK (self);

  return TRUE;
}

static gint
gst_wasapi2_sink_write (GstAudioSink * asink, gpointer data, guint length)
{
  GstWasapi2Sink *self = GST_WASAPI2_SINK (asink);

  if (!self->client) {
    GST_ERROR_OBJECT (self, "No audio client was configured");
    return -1;
  }

  if (!self->started) {
    if (!gst_wasapi2_client_start (self->client)) {
      GST_ERROR_OBJECT (self, "Failed to re-start client");
      return -1;
    }

    self->started = TRUE;
  }

  return gst_wasapi2_client_write (self->client, data, length);
}

static guint
gst_wasapi2_sink_delay (GstAudioSink * asink)
{
  GstWasapi2Sink *self = GST_WASAPI2_SINK (asink);

  if (!self->client)
    return 0;

  return gst_wasapi2_client_delay (self->client);
}

static void
gst_wasapi2_sink_reset (GstAudioSink * asink)
{
  GstWasapi2Sink *self = GST_WASAPI2_SINK (asink);

  GST_INFO_OBJECT (self, "reset called");

  self->started = FALSE;

  if (!self->client)
    return;

  gst_wasapi2_client_stop (self->client);
}

static void
gst_wasapi2_sink_set_mute (GstWasapi2Sink * self, gboolean mute)
{
  GST_WASAPI2_SINK_LOCK (self);

  self->mute = mute;
  self->mute_changed = TRUE;

  if (self->client) {
    if (!gst_wasapi2_client_set_mute (self->client, mute)) {
      GST_INFO_OBJECT (self, "Couldn't set mute");
    } else {
      self->mute_changed = FALSE;
    }
  } else {
    GST_DEBUG_OBJECT (self, "audio client is not configured yet");
  }

  GST_WASAPI2_SINK_UNLOCK (self);
}

static gboolean
gst_wasapi2_sink_get_mute (GstWasapi2Sink * self)
{
  gboolean mute;

  GST_WASAPI2_SINK_LOCK (self);

  mute = self->mute;

  if (self->client) {
    if (!gst_wasapi2_client_get_mute (self->client, &mute)) {
      GST_INFO_OBJECT (self, "Couldn't get mute state");
    } else {
      self->mute = mute;
    }
  } else {
    GST_DEBUG_OBJECT (self, "audio client is not configured yet");
  }

  GST_WASAPI2_SINK_UNLOCK (self);

  return mute;
}

static void
gst_wasapi2_sink_set_volume (GstWasapi2Sink * self, gdouble volume)
{
  GST_WASAPI2_SINK_LOCK (self);

  self->volume = volume;
  /* clip volume value */
  self->volume = MAX (0.0, self->volume);
  self->volume = MIN (1.0, self->volume);
  self->volume_changed = TRUE;

  if (self->client) {
    if (!gst_wasapi2_client_set_volume (self->client, (gfloat) self->volume)) {
      GST_INFO_OBJECT (self, "Couldn't set volume");
    } else {
      self->volume_changed = FALSE;
    }
  } else {
    GST_DEBUG_OBJECT (self, "audio client is not configured yet");
  }

  GST_WASAPI2_SINK_UNLOCK (self);
}

static gdouble
gst_wasapi2_sink_get_volume (GstWasapi2Sink * self)
{
  gfloat volume;

  GST_WASAPI2_SINK_LOCK (self);

  volume = (gfloat) self->volume;

  if (self->client) {
    if (!gst_wasapi2_client_get_volume (self->client, &volume)) {
      GST_INFO_OBJECT (self, "Couldn't get volume");
    } else {
      self->volume = volume;
    }
  } else {
    GST_DEBUG_OBJECT (self, "audio client is not configured yet");
  }

  GST_WASAPI2_SINK_UNLOCK (self);

  volume = MAX (0.0, volume);
  volume = MIN (1.0, volume);

  return volume;
}
