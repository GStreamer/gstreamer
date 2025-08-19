/*
 * Copyright (C) 2008 Ole André Vadla Ravnås <ole.andre.ravnas@tandberg.com>
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
 * SECTION:element-wasapi2src
 * @title: wasapi2src
 *
 * Provides audio capture from the Windows Audio Session API available with
 * Windows 10.
 *
 * ## Example pipelines
 * |[
 * gst-launch-1.0 -v wasapi2src ! fakesink
 * ]| Capture from the default audio device and render to fakesink.
 *
 * |[
 * gst-launch-1.0 -v wasapi2src low-latency=true ! fakesink
 * ]| Capture from the default audio device with the minimum possible latency and render to fakesink.
 *
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstwasapi2src.h"
#include "gstwasapi2util.h"
#include "gstwasapi2rbuf.h"
#include <mutex>
#include <atomic>

GST_DEBUG_CATEGORY_STATIC (gst_wasapi2_src_debug);
#define GST_CAT_DEFAULT gst_wasapi2_src_debug

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_WASAPI2_STATIC_CAPS));

/**
 * GstWasapi2SrcLoopbackMode:
 *
 * Loopback capture mode
 *
 * Since: 1.22
 */
typedef enum
{
  /**
   * GstWasapi2SrcLoopbackMode::default:
   *
   * Default loopback mode
   *
   * Since: 1.22
   */
  GST_WASAPI2_SRC_LOOPBACK_DEFAULT,

  /**
   * GstWasapi2SrcLoopbackMode::include-process-tree:
   *
   * Captures only specified process and its child process
   *
   * Since: 1.22
   */
  GST_WASAPI2_SRC_LOOPBACK_INCLUDE_PROCESS_TREE,

  /**
   * GstWasapi2SrcLoopbackMode::exclude-process-tree:
   *
   * Excludes specified process and its child process
   *
   * Since: 1.22
   */
  GST_WASAPI2_SRC_LOOPBACK_EXCLUDE_PROCESS_TREE,
} GstWasapi2SrcLoopbackMode;

#define GST_TYPE_WASAPI2_SRC_LOOPBACK_MODE (gst_wasapi2_src_loopback_mode_get_type ())
static GType
gst_wasapi2_src_loopback_mode_get_type (void)
{
  static GType loopback_type = 0;
  static const GEnumValue types[] = {
    {GST_WASAPI2_SRC_LOOPBACK_DEFAULT, "Default", "default"},
    {GST_WASAPI2_SRC_LOOPBACK_INCLUDE_PROCESS_TREE,
          "Include process and its child processes",
        "include-process-tree"},
    {GST_WASAPI2_SRC_LOOPBACK_EXCLUDE_PROCESS_TREE,
          "Exclude process and its child processes",
        "exclude-process-tree"},
    {0, nullptr, nullptr}
  };

  GST_WASAPI2_CALL_ONCE_BEGIN {
    loopback_type = g_enum_register_static ("GstWasapi2SrcLoopbackMode", types);
  } GST_WASAPI2_CALL_ONCE_END;

  return loopback_type;
}

#define DEFAULT_LOW_LATENCY   FALSE
#define DEFAULT_MUTE          FALSE
#define DEFAULT_VOLUME        1.0
#define DEFAULT_LOOPBACK      FALSE
#define DEFAULT_LOOPBACK_MODE GST_WASAPI2_SRC_LOOPBACK_DEFAULT
#define DEFAULT_LOOPBACK_SILENCE_ON_DEVICE_MUTE FALSE
#define DEFAULT_CONTINUE_ON_ERROR FALSE
#define DEFAULT_EXCLUSIVE FALSE

enum
{
  PROP_0,
  PROP_DEVICE,
  PROP_LOW_LATENCY,
  PROP_MUTE,
  PROP_VOLUME,
  PROP_DISPATCHER,
  PROP_LOOPBACK,
  PROP_LOOPBACK_MODE,
  PROP_LOOPBACK_TARGET_PID,
  PROP_LOOPBACK_SILENCE_ON_DEVICE_MUTE,
  PROP_CONTINUE_ON_ERROR,
  PROP_EXCLUSIVE,
};

/* *INDENT-OFF* */
struct GstWasapi2SrcPrivate
{
  ~GstWasapi2SrcPrivate ()
  {
    gst_object_unref (rbuf);
    g_free (device_id);
  }

  GstWasapi2Rbuf *rbuf = nullptr;

  std::mutex lock;
  std::atomic<bool> device_invalidated = { false };

  /* properties */
  gchar *device_id = nullptr;
  gboolean low_latency = DEFAULT_LOW_LATENCY;
  gboolean loopback = DEFAULT_LOOPBACK;
  GstWasapi2SrcLoopbackMode loopback_mode = DEFAULT_LOOPBACK_MODE;
  guint loopback_pid = 0;
  gboolean loopback_silence_on_device_mute =
      DEFAULT_LOOPBACK_SILENCE_ON_DEVICE_MUTE;
  gboolean continue_on_error = DEFAULT_CONTINUE_ON_ERROR;
  gboolean exclusive = DEFAULT_EXCLUSIVE;
};
/* *INDENT-ON* */

struct _GstWasapi2Src
{
  GstAudioBaseSrc parent;

  GstWasapi2SrcPrivate *priv;
};

static void gst_wasapi2_src_finalize (GObject * object);
static void gst_wasapi2_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_wasapi2_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstCaps *gst_wasapi2_src_get_caps (GstBaseSrc * bsrc, GstCaps * filter);
static GstAudioRingBuffer *gst_wasapi2_src_create_ringbuffer (GstAudioBaseSrc *
    src);

#define gst_wasapi2_src_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstWasapi2Src, gst_wasapi2_src,
    GST_TYPE_AUDIO_BASE_SRC,
    G_IMPLEMENT_INTERFACE (GST_TYPE_STREAM_VOLUME, nullptr));

static void
gst_wasapi2_src_class_init (GstWasapi2SrcClass * klass)
{
  auto gobject_class = G_OBJECT_CLASS (klass);
  auto element_class = GST_ELEMENT_CLASS (klass);
  auto basesrc_class = GST_BASE_SRC_CLASS (klass);
  auto audiobasesrc_class = GST_AUDIO_BASE_SRC_CLASS (klass);

  gobject_class->finalize = gst_wasapi2_src_finalize;
  gobject_class->set_property = gst_wasapi2_src_set_property;
  gobject_class->get_property = gst_wasapi2_src_get_property;

  g_object_class_install_property (gobject_class, PROP_DEVICE,
      g_param_spec_string ("device", "Device",
          "Audio device ID as provided by "
          "WASAPI device endpoint ID as provided by IMMDevice::GetId",
          nullptr, (GParamFlags) (GST_PARAM_MUTABLE_PLAYING |
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_LOW_LATENCY,
      g_param_spec_boolean ("low-latency", "Low latency",
          "Optimize all settings for lowest latency. Always safe to enable.",
          DEFAULT_LOW_LATENCY, (GParamFlags) (GST_PARAM_MUTABLE_READY |
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_MUTE,
      g_param_spec_boolean ("mute", "Mute", "Mute state of this stream",
          DEFAULT_MUTE, (GParamFlags) (GST_PARAM_MUTABLE_PLAYING |
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_VOLUME,
      g_param_spec_double ("volume", "Volume", "Volume of this stream",
          0.0, 1.0, DEFAULT_VOLUME, (GParamFlags) (GST_PARAM_MUTABLE_PLAYING |
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  /**
   * GstWasapi2Src:dispatcher:
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
          (GParamFlags) (GST_PARAM_MUTABLE_READY | G_PARAM_WRITABLE |
              G_PARAM_STATIC_STRINGS)));

  /**
   * GstWasapi2Src:loopback:
   *
   * Open render device for loopback recording
   *
   * Since: 1.20
   */
  g_object_class_install_property (gobject_class, PROP_LOOPBACK,
      g_param_spec_boolean ("loopback", "Loopback recording",
          "Open render device for loopback recording", DEFAULT_LOOPBACK,
          (GParamFlags) (GST_PARAM_MUTABLE_READY | G_PARAM_READWRITE |
              G_PARAM_STATIC_STRINGS)));

  if (gst_wasapi2_can_process_loopback ()) {
    /**
     * GstWasapi2Src:loopback-mode:
     *
     * Loopback mode. "target-process-id" must be specified in case of
     * process loopback modes.
     *
     * This feature requires "Windows 10 build 20348"
     *
     * Since: 1.22
     */
    g_object_class_install_property (gobject_class, PROP_LOOPBACK_MODE,
        g_param_spec_enum ("loopback-mode", "Loopback Mode",
            "Loopback mode to use", GST_TYPE_WASAPI2_SRC_LOOPBACK_MODE,
            DEFAULT_LOOPBACK_MODE,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
                GST_PARAM_MUTABLE_READY | G_PARAM_READWRITE |
                G_PARAM_STATIC_STRINGS)));

    /**
     * GstWasapi2Src:loopback-target-pid:
     *
     * Target process id to be recorded or excluded depending on loopback mode
     *
     * This feature requires "Windows 10 build 20348"
     *
     * Since: 1.22
     */
    g_object_class_install_property (gobject_class, PROP_LOOPBACK_TARGET_PID,
        g_param_spec_uint ("loopback-target-pid", "Loopback Target PID",
            "Process ID to be recorded or excluded for process loopback mode",
            0, G_MAXUINT32, 0,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
                GST_PARAM_MUTABLE_READY | G_PARAM_READWRITE |
                G_PARAM_STATIC_STRINGS)));
  }

  /**
   * GstWasapi2Src:loopback-silence-on-device-mute:
   *
   * When loopback recording, if the device is muted, inject silence in the pipeline
   *
   * Since: 1.24
   */
  g_object_class_install_property (gobject_class,
      PROP_LOOPBACK_SILENCE_ON_DEVICE_MUTE,
      g_param_spec_boolean ("loopback-silence-on-device-mute",
          "Loopback Silence On Device Mute",
          "When loopback recording, if the device is muted, inject silence in the pipeline",
          DEFAULT_LOOPBACK_SILENCE_ON_DEVICE_MUTE,
          (GParamFlags) (GST_PARAM_MUTABLE_PLAYING | G_PARAM_READWRITE |
              G_PARAM_STATIC_STRINGS)));

  /**
   * GstWasapi2Src:continue-on-error:
   *
   * If enabled, wasapi2src will post a warning message instead of an error,
   * when device failures occur, such as open failure, I/O error,
   * or device removal.
   * The element will continue to produce audio buffers and behave as if
   * a capture device were active, allowing pipeline to keep running even when
   * no audio endpoint is available
   *
   * Since: 1.28
   */
  g_object_class_install_property (gobject_class, PROP_CONTINUE_ON_ERROR,
      g_param_spec_boolean ("continue-on-error", "Continue On Error",
          "Continue running and produce buffers on device failure",
          DEFAULT_CONTINUE_ON_ERROR, (GParamFlags) (GST_PARAM_MUTABLE_READY |
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  /**
   * GstWasapi2Src:exclusive:
   *
   * Since: 1.28
   */
  g_object_class_install_property (gobject_class, PROP_EXCLUSIVE,
      g_param_spec_boolean ("exclusive", "Exclusive",
          "Open the device in exclusive mode",
          DEFAULT_EXCLUSIVE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_add_static_pad_template (element_class, &src_template);
  gst_element_class_set_static_metadata (element_class, "Wasapi2Src",
      "Source/Audio/Hardware",
      "Stream audio from an audio capture device through WASAPI",
      "Seungha Yang <seungha@centricular.com>");

  basesrc_class->get_caps = GST_DEBUG_FUNCPTR (gst_wasapi2_src_get_caps);

  audiobasesrc_class->create_ringbuffer =
      GST_DEBUG_FUNCPTR (gst_wasapi2_src_create_ringbuffer);

  GST_DEBUG_CATEGORY_INIT (gst_wasapi2_src_debug, "wasapi2src",
      0, "Windows audio session API source");

  if (gst_wasapi2_can_process_loopback ()) {
    gst_type_mark_as_plugin_api (GST_TYPE_WASAPI2_SRC_LOOPBACK_MODE,
        (GstPluginAPIFlags) 0);
  }
}

static void
gst_wasapi2_src_on_invalidated (gpointer elem)
{
  auto self = GST_WASAPI2_SRC (elem);
  auto priv = self->priv;

  GST_WARNING_OBJECT (self, "Device invalidated");

  priv->device_invalidated = true;
}

static void
gst_wasapi2_src_init (GstWasapi2Src * self)
{
  auto priv = new GstWasapi2SrcPrivate ();

  priv->rbuf = gst_wasapi2_rbuf_new (self, gst_wasapi2_src_on_invalidated);
  gst_wasapi2_rbuf_set_device (priv->rbuf, nullptr,
      GST_WASAPI2_ENDPOINT_CLASS_CAPTURE, 0, DEFAULT_LOW_LATENCY,
      DEFAULT_EXCLUSIVE);

  self->priv = priv;
}

static void
gst_wasapi2_src_finalize (GObject * object)
{
  auto self = GST_WASAPI2_SRC (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_wasapi2_src_set_device (GstWasapi2Src * self, bool updated)
{
  auto priv = self->priv;
  GstWasapi2EndpointClass device_class = GST_WASAPI2_ENDPOINT_CLASS_CAPTURE;
  bool expected = true;
  bool set_device = priv->device_invalidated.compare_exchange_strong (expected,
      false);

  if (!set_device && !updated)
    return;

  if (priv->loopback_pid) {
    if (priv->loopback_mode == GST_WASAPI2_SRC_LOOPBACK_INCLUDE_PROCESS_TREE) {
      device_class =
          GST_WASAPI2_ENDPOINT_CLASS_INCLUDE_PROCESS_LOOPBACK_CAPTURE;
    } else if (priv->loopback_mode ==
        GST_WASAPI2_SRC_LOOPBACK_EXCLUDE_PROCESS_TREE) {
      device_class =
          GST_WASAPI2_ENDPOINT_CLASS_EXCLUDE_PROCESS_LOOPBACK_CAPTURE;
    }
  } else if (priv->loopback) {
    device_class = GST_WASAPI2_ENDPOINT_CLASS_LOOPBACK_CAPTURE;
  }

  gst_wasapi2_rbuf_set_device (priv->rbuf, priv->device_id, device_class,
      priv->loopback_pid, priv->low_latency, priv->exclusive);
}

static void
gst_wasapi2_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  auto self = GST_WASAPI2_SRC (object);
  auto priv = self->priv;

  std::lock_guard < std::mutex > lk (priv->lock);

  switch (prop_id) {
    case PROP_DEVICE:
    {
      auto new_val = g_value_get_string (value);
      bool updated = false;
      if (g_strcmp0 (new_val, priv->device_id) != 0) {
        g_free (priv->device_id);
        priv->device_id = g_strdup (new_val);
        updated = true;
      }

      gst_wasapi2_src_set_device (self, updated);
      break;
    }
    case PROP_LOW_LATENCY:
    {
      auto new_val = g_value_get_boolean (value);
      bool updated = false;
      if (new_val != priv->low_latency) {
        priv->low_latency = new_val;
        updated = true;
      }

      gst_wasapi2_src_set_device (self, updated);
      break;
    }
    case PROP_MUTE:
      gst_wasapi2_rbuf_set_mute (priv->rbuf, g_value_get_boolean (value));
      break;
    case PROP_VOLUME:
      gst_wasapi2_rbuf_set_volume (priv->rbuf, g_value_get_double (value));
      break;
    case PROP_DISPATCHER:
      /* Unused */
      break;
    case PROP_LOOPBACK:
    {
      auto new_val = g_value_get_boolean (value);
      bool updated = false;
      if (new_val != priv->loopback) {
        priv->loopback = new_val;
        updated = true;
      }

      gst_wasapi2_src_set_device (self, updated);
      break;
    }
    case PROP_LOOPBACK_MODE:
    {
      auto new_val = (GstWasapi2SrcLoopbackMode) g_value_get_enum (value);
      bool updated = false;
      if (new_val != priv->loopback_mode) {
        priv->loopback_mode = new_val;
        updated = true;
      }

      gst_wasapi2_src_set_device (self, updated);
      break;
    }
    case PROP_LOOPBACK_TARGET_PID:
    {
      auto new_val = g_value_get_uint (value);
      bool updated = false;
      if (new_val != priv->loopback_pid) {
        priv->loopback_pid = new_val;
        updated = true;
      }

      gst_wasapi2_src_set_device (self, updated);
      break;
    }
    case PROP_LOOPBACK_SILENCE_ON_DEVICE_MUTE:
      priv->loopback_silence_on_device_mute = g_value_get_boolean (value);
      gst_wasapi2_rbuf_set_device_mute_monitoring (priv->rbuf,
          priv->loopback_silence_on_device_mute);
      break;
    case PROP_CONTINUE_ON_ERROR:
      priv->continue_on_error = g_value_get_boolean (value);
      gst_wasapi2_rbuf_set_continue_on_error (priv->rbuf,
          priv->continue_on_error);
      break;
    case PROP_EXCLUSIVE:
    {
      auto new_val = g_value_get_boolean (value);
      bool updated = false;
      if (new_val != priv->exclusive) {
        priv->exclusive = new_val;
        updated = true;
      }

      gst_wasapi2_src_set_device (self, updated);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_wasapi2_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  auto self = GST_WASAPI2_SRC (object);
  auto priv = self->priv;

  std::lock_guard < std::mutex > lk (priv->lock);

  switch (prop_id) {
    case PROP_DEVICE:
      g_value_set_string (value, priv->device_id);
      break;
    case PROP_LOW_LATENCY:
      g_value_set_boolean (value, priv->low_latency);
      break;
    case PROP_MUTE:
      g_value_set_boolean (value, gst_wasapi2_rbuf_get_mute (priv->rbuf));
      break;
    case PROP_VOLUME:
      g_value_set_double (value, gst_wasapi2_rbuf_get_volume (priv->rbuf));
      break;
    case PROP_LOOPBACK:
      g_value_set_boolean (value, priv->loopback);
      break;
    case PROP_LOOPBACK_MODE:
      g_value_set_enum (value, priv->loopback_mode);
      break;
    case PROP_LOOPBACK_TARGET_PID:
      g_value_set_uint (value, priv->loopback_pid);
      break;
    case PROP_LOOPBACK_SILENCE_ON_DEVICE_MUTE:
      g_value_set_boolean (value, priv->loopback_silence_on_device_mute);
      break;
    case PROP_CONTINUE_ON_ERROR:
      g_value_set_boolean (value, priv->continue_on_error);
      break;
    case PROP_EXCLUSIVE:
      g_value_set_boolean (value, priv->exclusive);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstCaps *
gst_wasapi2_src_get_caps (GstBaseSrc * bsrc, GstCaps * filter)
{
  auto self = GST_WASAPI2_SRC (bsrc);
  auto priv = self->priv;
  auto caps = gst_wasapi2_rbuf_get_caps (priv->rbuf);

  if (!caps)
    caps = gst_pad_get_pad_template_caps (bsrc->srcpad);

  if (filter) {
    GstCaps *filtered =
        gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    caps = filtered;
  }

  GST_DEBUG_OBJECT (self, "returning caps %" GST_PTR_FORMAT, caps);

  return caps;
}

static GstAudioRingBuffer *
gst_wasapi2_src_create_ringbuffer (GstAudioBaseSrc * src)
{
  auto self = GST_WASAPI2_SRC (src);
  auto priv = self->priv;

  return GST_AUDIO_RING_BUFFER (priv->rbuf);
}
