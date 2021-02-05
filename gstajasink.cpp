/* GStreamer
 * Copyright (C) 2021 Sebastian Dröge <sebastian@centricular.com>
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
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ajaanc/includes/ancillarydata_cea708.h>
#include <ajaanc/includes/ancillarylist.h>
#include <ajantv2/includes/ntv2rp188.h>

#include "gstajacommon.h"
#include "gstajasink.h"

GST_DEBUG_CATEGORY_STATIC(gst_aja_sink_debug);
#define GST_CAT_DEFAULT gst_aja_sink_debug

#define DEFAULT_DEVICE_IDENTIFIER ("0")
#define DEFAULT_CHANNEL (::NTV2_CHANNEL1)
#define DEFAULT_AUDIO_SYSTEM (GST_AJA_AUDIO_SYSTEM_AUTO)
#define DEFAULT_OUTPUT_DESTINATION (GST_AJA_OUTPUT_DESTINATION_AUTO)
#define DEFAULT_REFERENCE_SOURCE (GST_AJA_REFERENCE_SOURCE_AUTO)
#define DEFAULT_QUEUE_SIZE (16)
#define DEFAULT_OUTPUT_CPU_CORE (G_MAXUINT)

enum {
  PROP_0,
  PROP_DEVICE_IDENTIFIER,
  PROP_CHANNEL,
  PROP_AUDIO_SYSTEM,
  PROP_OUTPUT_DESTINATION,
  PROP_REFERENCE_SOURCE,
  PROP_QUEUE_SIZE,
  PROP_OUTPUT_CPU_CORE,
};

typedef enum {
  QUEUE_ITEM_TYPE_FRAME,
} QueueItemType;

typedef struct {
  QueueItemType type;

  // For FRAME
  GstVideoFrame frame;
  GstBuffer *audio_buffer;
  GstMapInfo audio_map;
  NTV2_RP188 tc;
  AJAAncillaryList *anc_packet_list;
} QueueItem;

static void gst_aja_sink_set_property(GObject *object, guint property_id,
                                      const GValue *value, GParamSpec *pspec);
static void gst_aja_sink_get_property(GObject *object, guint property_id,
                                      GValue *value, GParamSpec *pspec);
static void gst_aja_sink_finalize(GObject *object);

static gboolean gst_aja_sink_set_caps(GstBaseSink *bsink, GstCaps *caps);
static GstCaps *gst_aja_sink_get_caps(GstBaseSink *bsink, GstCaps *filter);
static gboolean gst_aja_sink_event(GstBaseSink *bsink, GstEvent *event);
static gboolean gst_aja_sink_propose_allocation(GstBaseSink *bsink,
                                                GstQuery *query);
static GstFlowReturn gst_aja_sink_render(GstBaseSink *bsink, GstBuffer *buffer);

static gboolean gst_aja_sink_open(GstAjaSink *sink);
static gboolean gst_aja_sink_close(GstAjaSink *sink);
static gboolean gst_aja_sink_start(GstAjaSink *sink);
static gboolean gst_aja_sink_stop(GstAjaSink *sink);

static GstStateChangeReturn gst_aja_sink_change_state(
    GstElement *element, GstStateChange transition);

static void output_thread_func(AJAThread *thread, void *data);

#define parent_class gst_aja_sink_parent_class
G_DEFINE_TYPE(GstAjaSink, gst_aja_sink, GST_TYPE_BASE_SINK);

static void gst_aja_sink_class_init(GstAjaSinkClass *klass) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
  GstBaseSinkClass *basesink_class = GST_BASE_SINK_CLASS(klass);
  GstCaps *templ_caps;

  gobject_class->set_property = gst_aja_sink_set_property;
  gobject_class->get_property = gst_aja_sink_get_property;
  gobject_class->finalize = gst_aja_sink_finalize;

  g_object_class_install_property(
      gobject_class, PROP_DEVICE_IDENTIFIER,
      g_param_spec_string(
          "device-identifier", "Device identifier",
          "Input device instance to use", DEFAULT_DEVICE_IDENTIFIER,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                        G_PARAM_CONSTRUCT)));

  g_object_class_install_property(
      gobject_class, PROP_CHANNEL,
      g_param_spec_uint(
          "channel", "Channel", "Channel to use", 0, NTV2_MAX_NUM_CHANNELS - 1,
          DEFAULT_CHANNEL,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                        G_PARAM_CONSTRUCT)));

  g_object_class_install_property(
      gobject_class, PROP_QUEUE_SIZE,
      g_param_spec_uint(
          "queue-size", "Queue Size",
          "Size of internal queue in number of video frames. "
          "Half of this is allocated as device buffers and equal to the "
          "latency.",
          1, G_MAXINT, DEFAULT_QUEUE_SIZE,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
      gobject_class, PROP_AUDIO_SYSTEM,
      g_param_spec_enum(
          "audio-system", "Audio System", "Audio system to use",
          GST_TYPE_AJA_AUDIO_SYSTEM, DEFAULT_AUDIO_SYSTEM,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                        G_PARAM_CONSTRUCT)));

  g_object_class_install_property(
      gobject_class, PROP_OUTPUT_DESTINATION,
      g_param_spec_enum(
          "output-destination", "Output Destination",
          "Output destination to use", GST_TYPE_AJA_OUTPUT_DESTINATION,
          DEFAULT_OUTPUT_DESTINATION,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                        G_PARAM_CONSTRUCT)));

  g_object_class_install_property(
      gobject_class, PROP_REFERENCE_SOURCE,
      g_param_spec_enum(
          "reference-source", "Reference Source", "Reference source to use",
          GST_TYPE_AJA_REFERENCE_SOURCE, DEFAULT_REFERENCE_SOURCE,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                        G_PARAM_CONSTRUCT)));

  g_object_class_install_property(
      gobject_class, PROP_OUTPUT_CPU_CORE,
      g_param_spec_uint(
          "output-cpu-core", "Output CPU Core",
          "Sets the affinity of the output thread to this CPU core "
          "(-1=disabled)",
          0, G_MAXUINT, DEFAULT_OUTPUT_CPU_CORE,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                        G_PARAM_CONSTRUCT)));

  element_class->change_state = GST_DEBUG_FUNCPTR(gst_aja_sink_change_state);

  basesink_class->set_caps = GST_DEBUG_FUNCPTR(gst_aja_sink_set_caps);
  basesink_class->get_caps = GST_DEBUG_FUNCPTR(gst_aja_sink_get_caps);
  basesink_class->event = GST_DEBUG_FUNCPTR(gst_aja_sink_event);
  basesink_class->propose_allocation =
      GST_DEBUG_FUNCPTR(gst_aja_sink_propose_allocation);
  basesink_class->render = GST_DEBUG_FUNCPTR(gst_aja_sink_render);

  templ_caps = gst_ntv2_supported_caps(DEVICE_ID_INVALID);
  gst_element_class_add_pad_template(
      element_class,
      gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, templ_caps));
  gst_caps_unref(templ_caps);

  gst_element_class_set_static_metadata(
      element_class, "AJA audio/video sink", "Audio/Video/Sink",
      "Outputs audio/video frames with AJA devices",
      "Sebastian Dröge <sebastian@centricular.com>");

  GST_DEBUG_CATEGORY_INIT(gst_aja_sink_debug, "ajasink", 0, "AJA sink");
}

static void gst_aja_sink_init(GstAjaSink *self) {
  g_mutex_init(&self->queue_lock);
  g_cond_init(&self->queue_cond);
  g_cond_init(&self->drain_cond);

  self->device_identifier = g_strdup(DEFAULT_DEVICE_IDENTIFIER);
  self->channel = DEFAULT_CHANNEL;
  self->queue_size = DEFAULT_QUEUE_SIZE;
  self->audio_system_setting = DEFAULT_AUDIO_SYSTEM;
  self->output_destination = DEFAULT_OUTPUT_DESTINATION;
  self->reference_source = DEFAULT_REFERENCE_SOURCE;
  self->output_cpu_core = DEFAULT_OUTPUT_CPU_CORE;

  gst_base_sink_set_render_delay(GST_BASE_SINK(self),
                                 (self->queue_size / 2) * GST_SECOND / 30);
  self->queue =
      gst_queue_array_new_for_struct(sizeof(QueueItem), self->queue_size);
}

void gst_aja_sink_set_property(GObject *object, guint property_id,
                               const GValue *value, GParamSpec *pspec) {
  GstAjaSink *self = GST_AJA_SINK(object);

  switch (property_id) {
    case PROP_DEVICE_IDENTIFIER:
      g_free(self->device_identifier);
      self->device_identifier = g_value_dup_string(value);
      break;
    case PROP_CHANNEL:
      self->channel = (NTV2Channel)g_value_get_uint(value);
      break;
    case PROP_QUEUE_SIZE:
      self->queue_size = g_value_get_uint(value);
      break;
    case PROP_AUDIO_SYSTEM:
      self->audio_system_setting = (GstAjaAudioSystem)g_value_get_enum(value);
      break;
    case PROP_OUTPUT_DESTINATION:
      self->output_destination =
          (GstAjaOutputDestination)g_value_get_enum(value);
      break;
    case PROP_REFERENCE_SOURCE:
      self->reference_source = (GstAjaReferenceSource)g_value_get_enum(value);
      break;
    case PROP_OUTPUT_CPU_CORE:
      self->output_cpu_core = g_value_get_uint(value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
      break;
  }
}

void gst_aja_sink_get_property(GObject *object, guint property_id,
                               GValue *value, GParamSpec *pspec) {
  GstAjaSink *self = GST_AJA_SINK(object);

  switch (property_id) {
    case PROP_DEVICE_IDENTIFIER:
      g_value_set_string(value, self->device_identifier);
      break;
    case PROP_CHANNEL:
      g_value_set_uint(value, self->channel);
      break;
    case PROP_QUEUE_SIZE:
      g_value_set_uint(value, self->queue_size);
      break;
    case PROP_AUDIO_SYSTEM:
      g_value_set_enum(value, self->audio_system_setting);
      break;
    case PROP_OUTPUT_DESTINATION:
      g_value_set_enum(value, self->output_destination);
      break;
    case PROP_REFERENCE_SOURCE:
      g_value_set_enum(value, self->reference_source);
      break;
    case PROP_OUTPUT_CPU_CORE:
      g_value_set_uint(value, self->output_cpu_core);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
      break;
  }
}

void gst_aja_sink_finalize(GObject *object) {
  GstAjaSink *self = GST_AJA_SINK(object);

  g_assert(self->device == NULL);
  g_assert(gst_queue_array_get_length(self->queue) == 0);
  g_clear_pointer(&self->queue, gst_queue_array_free);

  g_mutex_clear(&self->queue_lock);
  g_cond_clear(&self->queue_cond);
  g_cond_clear(&self->drain_cond);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static gboolean gst_aja_sink_open(GstAjaSink *self) {
  GST_DEBUG_OBJECT(self, "Opening device");

  g_assert(self->device == NULL);

  self->device = gst_aja_device_obtain(self->device_identifier);
  if (!self->device) {
    GST_ERROR_OBJECT(self, "Failed to open device");
    return FALSE;
  }

  if (!self->device->device->IsDeviceReady(false)) {
    g_clear_pointer(&self->device, gst_aja_device_unref);
    return FALSE;
  }

  self->device->device->SetEveryFrameServices(::NTV2_OEM_TASKS);
  self->device_id = self->device->device->GetDeviceID();

  std::string serial_number;
  if (!self->device->device->GetSerialNumberString(serial_number))
    serial_number = "none";

  GST_DEBUG_OBJECT(self,
                   "Opened device with ID %d at index %d (%s, version %s, "
                   "serial number %s, can do VANC %d)",
                   self->device_id, self->device->device->GetIndexNumber(),
                   self->device->device->GetDisplayName().c_str(),
                   self->device->device->GetDeviceVersionString().c_str(),
                   serial_number.c_str(),
                   ::NTV2DeviceCanDoCustomAnc(self->device_id));

  GST_DEBUG_OBJECT(self,
                   "Using SDK version %d.%d.%d.%d (%s) and driver version %s",
                   AJA_NTV2_SDK_VERSION_MAJOR, AJA_NTV2_SDK_VERSION_MINOR,
                   AJA_NTV2_SDK_VERSION_POINT, AJA_NTV2_SDK_BUILD_NUMBER,
                   AJA_NTV2_SDK_BUILD_DATETIME,
                   self->device->device->GetDriverVersionString().c_str());

  self->device->device->SetMultiFormatMode(true);

  self->allocator = gst_aja_allocator_new(self->device);

  GST_DEBUG_OBJECT(self, "Opened device");

  return TRUE;
}

static gboolean gst_aja_sink_close(GstAjaSink *self) {
  gst_clear_object(&self->allocator);
  g_clear_pointer(&self->device, gst_aja_device_unref);
  self->device_id = DEVICE_ID_INVALID;

  GST_DEBUG_OBJECT(self, "Closed device");

  return TRUE;
}

static gboolean gst_aja_sink_start(GstAjaSink *self) {
  GST_DEBUG_OBJECT(self, "Starting");
  self->output_thread = new AJAThread();
  self->output_thread->Attach(output_thread_func, self);
  self->output_thread->SetPriority(AJA_ThreadPriority_High);
  self->output_thread->Start();
  g_mutex_lock(&self->queue_lock);
  self->shutdown = FALSE;
  self->playing = FALSE;
  self->eos = FALSE;
  g_cond_signal(&self->queue_cond);
  g_mutex_unlock(&self->queue_lock);

  return TRUE;
}

static gboolean gst_aja_sink_stop(GstAjaSink *self) {
  QueueItem *item;

  GST_DEBUG_OBJECT(self, "Stopping");

  g_mutex_lock(&self->queue_lock);
  self->shutdown = TRUE;
  self->playing = FALSE;
  g_cond_signal(&self->queue_cond);
  g_mutex_unlock(&self->queue_lock);

  if (self->output_thread) {
    self->output_thread->Stop();
    delete self->output_thread;
    self->output_thread = NULL;
  }

  GST_OBJECT_LOCK(self);
  gst_clear_caps(&self->configured_caps);
  self->configured_audio_channels = 0;
  GST_OBJECT_UNLOCK(self);

  while ((item = (QueueItem *)gst_queue_array_pop_head_struct(self->queue))) {
    if (item->type == QUEUE_ITEM_TYPE_FRAME) {
      gst_video_frame_unmap(&item->frame);
      if (item->audio_buffer) {
        gst_buffer_unmap(item->audio_buffer, &item->audio_map);
        gst_buffer_unref(item->audio_buffer);
      }
      if (item->anc_packet_list) {
        delete item->anc_packet_list;
      }
    }
  }

  if (self->buffer_pool) {
    gst_buffer_pool_set_active(self->buffer_pool, FALSE);
    gst_clear_object(&self->buffer_pool);
  }

  if (self->audio_buffer_pool) {
    gst_buffer_pool_set_active(self->audio_buffer_pool, FALSE);
    gst_clear_object(&self->audio_buffer_pool);
  }

  GST_DEBUG_OBJECT(self, "Stopped");

  return TRUE;
}

static GstStateChangeReturn gst_aja_sink_change_state(
    GstElement *element, GstStateChange transition) {
  GstAjaSink *self = GST_AJA_SINK(element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_aja_sink_open(self)) return GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!gst_aja_sink_start(self)) return GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE) return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      g_mutex_lock(&self->queue_lock);
      self->playing = FALSE;
      g_cond_signal(&self->queue_cond);
      g_mutex_unlock(&self->queue_lock);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      g_mutex_lock(&self->queue_lock);
      self->playing = TRUE;
      g_cond_signal(&self->queue_cond);
      g_mutex_unlock(&self->queue_lock);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (!gst_aja_sink_stop(self)) return GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (!gst_aja_sink_close(self)) return GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  return ret;
}

static gboolean gst_aja_sink_set_caps(GstBaseSink *bsink, GstCaps *caps) {
  GstAjaSink *self = GST_AJA_SINK(bsink);
  const GstStructure *s;
  NTV2VideoFormat video_format = ::NTV2_FORMAT_UNKNOWN;

  GST_DEBUG_OBJECT(self, "Configuring caps %" GST_PTR_FORMAT, caps);

  GST_OBJECT_LOCK(self);
  if (self->configured_caps) {
    if (!gst_caps_can_intersect(self->configured_caps, caps)) {
      GST_DEBUG_OBJECT(self, "Need to reconfigure, waiting for draining");
      GST_OBJECT_UNLOCK(self);
      g_mutex_lock(&self->queue_lock);
      self->draining = TRUE;
      g_cond_signal(&self->queue_cond);
      while (self->draining && !self->flushing && !self->shutdown) {
        g_cond_wait(&self->drain_cond, &self->queue_lock);
      }

      if (self->flushing || self->shutdown) {
        g_mutex_unlock(&self->queue_lock);
        GST_DEBUG_OBJECT(self, "Flushing");
        return FALSE;
      }
      g_mutex_unlock(&self->queue_lock);
      GST_OBJECT_LOCK(self);
    } else {
      GST_OBJECT_UNLOCK(self);
      GST_DEBUG_OBJECT(self,
                       "Compatible caps with previous caps, not reconfiguring");
      return TRUE;
    }
  }

  if (!gst_video_info_from_caps(&self->configured_info, caps)) {
    GST_OBJECT_UNLOCK(self);
    GST_FIXME_OBJECT(self, "Failed to parse caps");
    return FALSE;
  }

  self->configured_audio_channels = 0;
  s = gst_caps_get_structure(caps, 0);
  gst_structure_get_int(s, "audio-channels", &self->configured_audio_channels);

  gst_caps_replace(&self->configured_caps, caps);
  GST_OBJECT_UNLOCK(self);

  video_format = gst_ntv2_video_format_from_caps(caps);
  if (video_format == NTV2_FORMAT_UNKNOWN) {
    GST_ERROR_OBJECT(self, "Unsupported caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  self->video_format = video_format;

  // Configure render delay based on the framerate and queue size
  gst_base_sink_set_render_delay(
      GST_BASE_SINK(self),
      gst_util_uint64_scale(self->queue_size / 2,
                            self->configured_info.fps_d * GST_SECOND,
                            self->configured_info.fps_n));

  g_assert(self->device != NULL);

  // Make sure to globally lock here as the routing settings and others are
  // global shared state
  ShmMutexLocker locker;

  if (!::NTV2DeviceCanDoVideoFormat(self->device_id, video_format)) {
    GST_ERROR_OBJECT(self, "Device does not support mode %d",
                     (int)video_format);
    return FALSE;
  }

  self->device->device->SetMode(self->channel, NTV2_MODE_DISPLAY, false);

  GST_DEBUG_OBJECT(self, "Configuring video format %d on channel %d",
                   (int)video_format, (int)self->channel);
  self->device->device->SetVideoFormat(video_format, false, false,
                                       self->channel);

  if (!::NTV2DeviceCanDoFrameBufferFormat(self->device_id,
                                          ::NTV2_FBF_10BIT_YCBCR)) {
    GST_ERROR_OBJECT(self, "Device does not support frame buffer format %d",
                     (int)::NTV2_FBF_10BIT_YCBCR);
    return FALSE;
  }
  self->device->device->SetFrameBufferFormat(self->channel,
                                             ::NTV2_FBF_10BIT_YCBCR);

  NTV2ReferenceSource reference_source;
  switch (self->reference_source) {
    case GST_AJA_REFERENCE_SOURCE_EXTERNAL:
      reference_source = ::NTV2_REFERENCE_EXTERNAL;
      break;
    case GST_AJA_REFERENCE_SOURCE_FREERUN:
    case GST_AJA_REFERENCE_SOURCE_AUTO:
      reference_source = ::NTV2_REFERENCE_FREERUN;
      break;
    case GST_AJA_REFERENCE_SOURCE_INPUT_1:
      reference_source = ::NTV2_REFERENCE_INPUT1;
      break;
    case GST_AJA_REFERENCE_SOURCE_INPUT_2:
      reference_source = ::NTV2_REFERENCE_INPUT2;
      break;
    case GST_AJA_REFERENCE_SOURCE_INPUT_3:
      reference_source = ::NTV2_REFERENCE_INPUT3;
      break;
    case GST_AJA_REFERENCE_SOURCE_INPUT_4:
      reference_source = ::NTV2_REFERENCE_INPUT4;
      break;
    case GST_AJA_REFERENCE_SOURCE_INPUT_5:
      reference_source = ::NTV2_REFERENCE_INPUT5;
      break;
    case GST_AJA_REFERENCE_SOURCE_INPUT_6:
      reference_source = ::NTV2_REFERENCE_INPUT6;
      break;
    case GST_AJA_REFERENCE_SOURCE_INPUT_7:
      reference_source = ::NTV2_REFERENCE_INPUT7;
      break;
    case GST_AJA_REFERENCE_SOURCE_INPUT_8:
      reference_source = ::NTV2_REFERENCE_INPUT8;
      break;
    default:
      g_assert_not_reached();
      break;
  }
  GST_DEBUG_OBJECT(self, "Configuring reference source %d",
                   (int)reference_source);
  self->device->device->SetFramePulseReference(reference_source);

  if (!self->device->device->EnableChannel(self->channel)) {
    GST_ERROR_OBJECT(self, "Failed to enable channel");
    return FALSE;
  }

  self->device->device->DMABufferAutoLock(false, true, 0);

  if (::NTV2DeviceHasBiDirectionalSDI(self->device_id))
    self->device->device->SetSDITransmitEnable(self->channel, true);

  const NTV2Standard standard(::GetNTV2StandardFromVideoFormat(video_format));
  self->device->device->SetSDIOutputStandard(self->channel, standard);
  const NTV2FrameGeometry geometry =
      ::GetNTV2FrameGeometryFromVideoFormat(video_format);
  self->device->device->SetVANCMode(::NTV2_VANCMODE_OFF, standard, geometry,
                                    self->channel);

  NTV2SmpteLineNumber smpte_line_num_info = ::GetSmpteLineNumber(standard);
  self->f2_start_line =
      (smpte_line_num_info.GetLastLine(
           smpte_line_num_info.firstFieldTop ? NTV2_FIELD0 : NTV2_FIELD1) +
       1);

  if (self->configured_audio_channels) {
    switch (self->audio_system_setting) {
      case GST_AJA_AUDIO_SYSTEM_1:
        self->audio_system = ::NTV2_AUDIOSYSTEM_1;
        break;
      case GST_AJA_AUDIO_SYSTEM_2:
        self->audio_system = ::NTV2_AUDIOSYSTEM_2;
        break;
      case GST_AJA_AUDIO_SYSTEM_3:
        self->audio_system = ::NTV2_AUDIOSYSTEM_3;
        break;
      case GST_AJA_AUDIO_SYSTEM_4:
        self->audio_system = ::NTV2_AUDIOSYSTEM_4;
        break;
      case GST_AJA_AUDIO_SYSTEM_5:
        self->audio_system = ::NTV2_AUDIOSYSTEM_5;
        break;
      case GST_AJA_AUDIO_SYSTEM_6:
        self->audio_system = ::NTV2_AUDIOSYSTEM_6;
        break;
      case GST_AJA_AUDIO_SYSTEM_7:
        self->audio_system = ::NTV2_AUDIOSYSTEM_7;
        break;
      case GST_AJA_AUDIO_SYSTEM_8:
        self->audio_system = ::NTV2_AUDIOSYSTEM_8;
        break;
      case GST_AJA_AUDIO_SYSTEM_AUTO:
        self->audio_system = ::NTV2_AUDIOSYSTEM_1;
        if (::NTV2DeviceGetNumAudioSystems(self->device_id) > 1)
          self->audio_system = ::NTV2ChannelToAudioSystem(self->channel);
        if (!::NTV2DeviceCanDoFrameStore1Display(self->device_id))
          self->audio_system = ::NTV2_AUDIOSYSTEM_1;
        break;
      default:
        g_assert_not_reached();
        break;
    }

    GST_DEBUG_OBJECT(self, "Using audio system %d", self->audio_system);

    self->device->device->SetNumberAudioChannels(
        self->configured_audio_channels, self->audio_system);
    self->device->device->SetAudioRate(::NTV2_AUDIO_48K, self->audio_system);
    self->device->device->SetAudioBufferSize(::NTV2_AUDIO_BUFFER_BIG,
                                             self->audio_system);
    self->device->device->SetSDIOutputAudioSystem(self->channel,
                                                  self->audio_system);
    self->device->device->SetSDIOutputDS2AudioSystem(self->channel,
                                                     self->audio_system);
    self->device->device->SetAudioLoopBack(::NTV2_AUDIO_LOOPBACK_OFF,
                                           self->audio_system);
  } else {
    self->audio_system = ::NTV2_AUDIOSYSTEM_INVALID;
  }

  CNTV2SignalRouter router;

  self->device->device->GetRouting(router);

  // Always use the framebuffer associated with the channel
  NTV2OutputCrosspointID framebuffer_id =
      ::GetFrameBufferOutputXptFromChannel(self->channel, false, false);

  NTV2InputCrosspointID output_destination_id;
  switch (self->output_destination) {
    case GST_AJA_OUTPUT_DESTINATION_AUTO:
      output_destination_id = ::GetSDIOutputInputXpt(self->channel, false);
      break;
    case GST_AJA_OUTPUT_DESTINATION_SDI1:
      output_destination_id = ::NTV2_XptSDIOut1Input;
      break;
    case GST_AJA_OUTPUT_DESTINATION_SDI2:
      output_destination_id = ::NTV2_XptSDIOut2Input;
      break;
    case GST_AJA_OUTPUT_DESTINATION_SDI3:
      output_destination_id = ::NTV2_XptSDIOut3Input;
      break;
    case GST_AJA_OUTPUT_DESTINATION_SDI4:
      output_destination_id = ::NTV2_XptSDIOut4Input;
      break;
    case GST_AJA_OUTPUT_DESTINATION_SDI5:
      output_destination_id = ::NTV2_XptSDIOut5Input;
      break;
    case GST_AJA_OUTPUT_DESTINATION_SDI6:
      output_destination_id = ::NTV2_XptSDIOut6Input;
      break;
    case GST_AJA_OUTPUT_DESTINATION_SDI7:
      output_destination_id = ::NTV2_XptSDIOut7Input;
      break;
    case GST_AJA_OUTPUT_DESTINATION_SDI8:
      output_destination_id = ::NTV2_XptSDIOut8Input;
      break;
    case GST_AJA_OUTPUT_DESTINATION_ANALOG:
      output_destination_id = ::NTV2_XptAnalogOutInput;
      break;
    case GST_AJA_OUTPUT_DESTINATION_HDMI:
      output_destination_id = ::NTV2_XptHDMIOutInput;
      break;
    default:
      g_assert_not_reached();
      break;
  }

  // Need to remove old routes for the output and framebuffer we're going to use
  NTV2ActualConnections connections = router.GetConnections();

  for (NTV2ActualConnectionsConstIter iter = connections.begin();
       iter != connections.end(); iter++) {
    if (iter->first == output_destination_id || iter->second == framebuffer_id)
      router.RemoveConnection(iter->first, iter->second);
  }

  GST_DEBUG_OBJECT(self, "Creating connection %d - %d", output_destination_id,
                   framebuffer_id);
  router.AddConnection(output_destination_id, framebuffer_id);

  {
    std::stringstream os;
    CNTV2SignalRouter oldRouter;
    self->device->device->GetRouting(oldRouter);
    oldRouter.Print(os);
    GST_DEBUG_OBJECT(self, "Previous routing:\n%s", os.str().c_str());
  }
  self->device->device->ApplySignalRoute(router, true);
  {
    std::stringstream os;
    CNTV2SignalRouter currentRouter;
    self->device->device->GetRouting(currentRouter);
    currentRouter.Print(os);
    GST_DEBUG_OBJECT(self, "New routing:\n%s", os.str().c_str());
  }

  return TRUE;
}

static GstCaps *gst_aja_sink_get_caps(GstBaseSink *bsink, GstCaps *filter) {
  GstAjaSink *self = GST_AJA_SINK(bsink);
  GstCaps *caps;

  if (self->device) {
    caps = gst_ntv2_supported_caps(self->device_id);
  } else {
    caps = gst_pad_get_pad_template_caps(GST_BASE_SINK_PAD(self));
  }

  if (filter) {
    GstCaps *tmp =
        gst_caps_intersect_full(filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref(caps);
    caps = tmp;
  }

  return caps;
}

static gboolean gst_aja_sink_event(GstBaseSink *bsink, GstEvent *event) {
  GstAjaSink *self = GST_AJA_SINK(bsink);

  switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_EOS: {
      GST_DEBUG_OBJECT(self, "Signalling EOS");

      g_mutex_lock(&self->queue_lock);
      self->eos = TRUE;
      g_cond_signal(&self->queue_cond);
      g_mutex_unlock(&self->queue_lock);

      break;
    }
    case GST_EVENT_FLUSH_START: {
      g_mutex_lock(&self->queue_lock);
      self->flushing = TRUE;
      self->draining = FALSE;
      g_cond_signal(&self->drain_cond);
      g_mutex_unlock(&self->queue_lock);
      break;
    }
    case GST_EVENT_FLUSH_STOP: {
      QueueItem *item;

      g_mutex_lock(&self->queue_lock);
      while (
          (item = (QueueItem *)gst_queue_array_pop_head_struct(self->queue))) {
        if (item->type == QUEUE_ITEM_TYPE_FRAME) {
          gst_video_frame_unmap(&item->frame);
          if (item->audio_buffer) {
            gst_buffer_unmap(item->audio_buffer, &item->audio_map);
            gst_buffer_unref(item->audio_buffer);
          }
          if (item->anc_packet_list) {
            delete item->anc_packet_list;
          }
        }
      }
      g_cond_signal(&self->queue_cond);

      self->flushing = FALSE;
      g_cond_signal(&self->drain_cond);
      g_mutex_unlock(&self->queue_lock);
      break;
    }
    default:
      break;
  }

  return GST_BASE_SINK_CLASS(parent_class)->event(bsink, event);
}

static gboolean gst_aja_sink_propose_allocation(GstBaseSink *bsink,
                                                GstQuery *query) {
  GstAjaSink *self = GST_AJA_SINK(bsink);

  if (self->allocator) {
    GstAllocationParams params;

    gst_allocation_params_init(&params);
    params.prefix = 0;
    params.padding = 0;
    params.align = 4095;

    gst_query_add_allocation_param(query, self->allocator, &params);
  }

  return TRUE;
}

static GstFlowReturn gst_aja_sink_render(GstBaseSink *bsink,
                                         GstBuffer *buffer) {
  GstAjaSink *self = GST_AJA_SINK(bsink);
  GstFlowReturn flow_ret = GST_FLOW_OK;
  GstAjaAudioMeta *meta;
  GstBuffer *item_buffer = NULL, *item_audio_buffer = NULL;
  GstVideoTimeCodeMeta *tc_meta;
  QueueItem item = {
      .type = QUEUE_ITEM_TYPE_FRAME,
      .frame =
          {
              {0},
          },
      .audio_buffer = NULL,
      .audio_map = GST_MAP_INFO_INIT,
      .tc = NTV2_RP188(),
      .anc_packet_list = NULL,
  };

  guint video_buffer_size = ::GetVideoActiveSize(
      self->video_format, ::NTV2_FBF_10BIT_YCBCR, ::NTV2_VANCMODE_OFF);

  meta = gst_buffer_get_aja_audio_meta(buffer);
  tc_meta = gst_buffer_get_video_time_code_meta(buffer);

  if (gst_buffer_n_memory(buffer) == 1) {
    GstMemory *mem = gst_buffer_peek_memory(buffer, 0);

    if (gst_memory_get_sizes(mem, NULL, NULL) == video_buffer_size &&
        strcmp(mem->allocator->mem_type, GST_AJA_ALLOCATOR_MEMTYPE) == 0 &&
        GST_AJA_ALLOCATOR(mem->allocator)->device->device->GetIndexNumber() ==
            self->device->device->GetIndexNumber()) {
      item_buffer = gst_buffer_ref(buffer);
    }
  }

  if (!item_buffer) {
    GstVideoFrame in_frame;

    GST_DEBUG_OBJECT(self, "Allocating new video buffer");

    if (!self->buffer_pool) {
      self->buffer_pool = gst_buffer_pool_new();
      GstStructure *config = gst_buffer_pool_get_config(self->buffer_pool);
      gst_buffer_pool_config_set_params(config, NULL, video_buffer_size,
                                        self->queue_size, 0);
      gst_buffer_pool_config_set_allocator(config, self->allocator, NULL);
      gst_buffer_pool_set_config(self->buffer_pool, config);
      gst_buffer_pool_set_active(self->buffer_pool, TRUE);
    }

    if (!gst_video_frame_map(&in_frame, &self->configured_info, buffer,
                             GST_MAP_READ)) {
      GST_ERROR_OBJECT(self, "Failed to map buffer");
      return GST_FLOW_ERROR;
    }

    flow_ret =
        gst_buffer_pool_acquire_buffer(self->buffer_pool, &item_buffer, NULL);
    if (flow_ret != GST_FLOW_OK) {
      gst_video_frame_unmap(&in_frame);
      return flow_ret;
    }

    item.type = QUEUE_ITEM_TYPE_FRAME;

    gst_video_frame_map(&item.frame, &self->configured_info, item_buffer,
                        GST_MAP_READWRITE);
    gst_video_frame_copy(&item.frame, &in_frame);
    gst_video_frame_unmap(&in_frame);
    gst_buffer_unref(item_buffer);
  } else {
    item.type = QUEUE_ITEM_TYPE_FRAME;

    gst_video_frame_map(&item.frame, &self->configured_info, item_buffer,
                        GST_MAP_READ);
    gst_buffer_unref(item_buffer);
  }

  if (meta) {
    if (gst_buffer_n_memory(meta->buffer) == 1) {
      GstMemory *mem = gst_buffer_peek_memory(meta->buffer, 0);

      if (strcmp(mem->allocator->mem_type, GST_AJA_ALLOCATOR_MEMTYPE) == 0 &&
          GST_AJA_ALLOCATOR(mem->allocator)->device->device->GetIndexNumber() ==
              self->device->device->GetIndexNumber()) {
        item_audio_buffer = gst_buffer_ref(meta->buffer);
      }
    }

    if (!item_audio_buffer) {
      GstMapInfo audio_map;

      GST_DEBUG_OBJECT(self, "Allocating new audio buffer");

      if (!self->audio_buffer_pool) {
        guint audio_buffer_size = 1UL * 1024UL * 1024UL;

        self->audio_buffer_pool = gst_buffer_pool_new();
        GstStructure *config =
            gst_buffer_pool_get_config(self->audio_buffer_pool);
        gst_buffer_pool_config_set_params(config, NULL, audio_buffer_size,
                                          self->queue_size, 0);
        gst_buffer_pool_config_set_allocator(config, self->allocator, NULL);
        gst_buffer_pool_set_config(self->audio_buffer_pool, config);
        gst_buffer_pool_set_active(self->audio_buffer_pool, TRUE);
      }

      flow_ret = gst_buffer_pool_acquire_buffer(self->audio_buffer_pool,
                                                &item_audio_buffer, NULL);
      if (flow_ret != GST_FLOW_OK) {
        gst_video_frame_unmap(&item.frame);
        return flow_ret;
      }

      gst_buffer_set_size(item_audio_buffer, gst_buffer_get_size(meta->buffer));

      gst_buffer_map(meta->buffer, &audio_map, GST_MAP_READ);
      gst_buffer_map(item_audio_buffer, &item.audio_map, GST_MAP_READWRITE);
      memcpy(item.audio_map.data, audio_map.data, audio_map.size);
      gst_buffer_unmap(meta->buffer, &audio_map);
      item.audio_buffer = item_audio_buffer;
    } else {
      gst_buffer_map(item_audio_buffer, &item.audio_map, GST_MAP_READ);
      item.audio_buffer = item_audio_buffer;
    }
  } else {
    item.audio_buffer = NULL;
  }

  if (tc_meta) {
    TimecodeFormat tc_format = ::kTCFormatUnknown;

    if (tc_meta->tc.config.fps_n == 24 && tc_meta->tc.config.fps_d == 1) {
      tc_format = kTCFormat24fps;
    } else if (tc_meta->tc.config.fps_n == 25 &&
               tc_meta->tc.config.fps_d == 1) {
      tc_format = kTCFormat25fps;
    } else if (tc_meta->tc.config.fps_n == 30 &&
               tc_meta->tc.config.fps_d == 1) {
      tc_format = kTCFormat30fps;
    } else if (tc_meta->tc.config.fps_n == 30000 &&
               tc_meta->tc.config.fps_d == 1001) {
      tc_format = kTCFormat30fpsDF;
    } else if (tc_meta->tc.config.fps_n == 48 &&
               tc_meta->tc.config.fps_d == 1) {
      tc_format = kTCFormat48fps;
    } else if (tc_meta->tc.config.fps_n == 50 &&
               tc_meta->tc.config.fps_d == 1) {
      tc_format = kTCFormat50fps;
    } else if (tc_meta->tc.config.fps_n == 60 &&
               tc_meta->tc.config.fps_d == 1) {
      tc_format = kTCFormat60fps;
    } else if (tc_meta->tc.config.fps_n == 60000 &&
               tc_meta->tc.config.fps_d == 1001) {
      tc_format = kTCFormat60fpsDF;
    }

    const CRP188 rp188(tc_meta->tc.frames, tc_meta->tc.seconds,
                       tc_meta->tc.minutes, tc_meta->tc.hours, tc_format);
    rp188.GetRP188Reg(item.tc);
  } else {
    item.tc.fDBB = 0xffffffff;
  }

  // TODO: Handle AFD/Bar meta
#if 0
    if (bar_meta || afd_meta) {
      const uint16_t kF1PktLineNumAFDBAR(11);
      const AJAAncillaryDataLocation kAFDBARLocF1(
          AJAAncillaryDataLink_A, AJAAncillaryDataVideoStream_Y,
          AJAAncillaryDataSpace_VANC, kF1PktLineNumAFDBAR,
          AJAAncDataHorizOffset_AnyVanc);
      const uint16_t kF2PktLineNumAFDBAR(self->f2_start_line + 11);
      const AJAAncillaryDataLocation kAFDBARLocF2(
          AJAAncillaryDataLink_A, AJAAncillaryDataVideoStream_Y,
          AJAAncillaryDataSpace_VANC, kF2PktLineNumAFDBAR,
          AJAAncDataHorizOffset_AnyVanc);

      AJAAncillaryData pkt;
      pkt.SetFromSMPTE334(NULL, 0, kAFDBARLocF1);
      item.anc_packet_list->AddAncillaryData(pkt);

      if (self->configured_info.interlace_mode != GST_VIDEO_INTERLACE_MODE_PROGRESSIVE) {
        AJAAncillaryData pkt2;
        pkt.SetFromSMPTE334(NULL, 0, kAFDBARLocF2);
        item.anc_packet_list->AddAncillaryData(pkt);
      }
    }
#endif

  GstVideoCaptionMeta *caption_meta;
  gpointer iter = NULL;
  while (
      (caption_meta = (GstVideoCaptionMeta *)gst_buffer_iterate_meta_filtered(
           buffer, &iter, GST_VIDEO_CAPTION_META_API_TYPE))) {
    if (!item.anc_packet_list) item.anc_packet_list = new AJAAncillaryList;

    if (caption_meta->caption_type == GST_VIDEO_CAPTION_TYPE_CEA708_CDP) {
      const uint16_t kF1PktLineNumCEA708(9);
      const AJAAncillaryDataLocation kCEA708LocF1(
          AJAAncillaryDataLink_A, AJAAncillaryDataVideoStream_Y,
          AJAAncillaryDataSpace_VANC, kF1PktLineNumCEA708,
          AJAAncDataHorizOffset_AnyVanc);

      AJAAncillaryData_Cea708 pkt;

      pkt.SetDID(GST_VIDEO_ANCILLARY_DID16_S334_EIA_708 >> 8);
      pkt.SetSID(GST_VIDEO_ANCILLARY_DID16_S334_EIA_708 & 0xff);
      pkt.SetDataLocation(kCEA708LocF1);
      pkt.SetDataCoding(AJAAncillaryDataCoding_Digital);
      pkt.SetPayloadData(caption_meta->data, caption_meta->size);

      item.anc_packet_list->AddAncillaryData(pkt);
    } else {
      GST_WARNING_OBJECT(self, "Unhandled caption type %d",
                         caption_meta->caption_type);
    }
  }

  g_mutex_lock(&self->queue_lock);
  while (gst_queue_array_get_length(self->queue) >= self->queue_size) {
    QueueItem *tmp = (QueueItem *)gst_queue_array_pop_head_struct(self->queue);

    if (tmp->type == QUEUE_ITEM_TYPE_FRAME) {
      GST_WARNING_OBJECT(self, "Element queue overrun, dropping old frame");

      GstMessage *msg = gst_message_new_qos(
          GST_OBJECT_CAST(self), TRUE, GST_CLOCK_TIME_NONE, GST_CLOCK_TIME_NONE,
          GST_BUFFER_PTS(tmp->frame.buffer),
          gst_util_uint64_scale(GST_SECOND, self->configured_info.fps_d,
                                self->configured_info.fps_n));
      gst_element_post_message(GST_ELEMENT_CAST(self), msg);

      gst_video_frame_unmap(&tmp->frame);
      if (tmp->audio_buffer) {
        gst_buffer_unmap(tmp->audio_buffer, &tmp->audio_map);
        gst_buffer_unref(tmp->audio_buffer);
      }
      if (tmp->anc_packet_list) {
        delete tmp->anc_packet_list;
      }
    }
  }

  GST_TRACE_OBJECT(self, "Queuing frame video %p audio %p",
                   GST_VIDEO_FRAME_PLANE_DATA(&item.frame, 0),
                   item.audio_buffer ? item.audio_map.data : NULL);
  gst_queue_array_push_tail_struct(self->queue, &item);
  GST_TRACE_OBJECT(self, "%u frames queued",
                   gst_queue_array_get_length(self->queue));
  g_cond_signal(&self->queue_cond);
  g_mutex_unlock(&self->queue_lock);

  return flow_ret;
}

static void output_thread_func(AJAThread *thread, void *data) {
  GstAjaSink *self = GST_AJA_SINK(data);
  GstClock *clock = NULL;
  guint64 frames_renderded_start = G_MAXUINT64;
  GstClockTime frames_renderded_start_time = GST_CLOCK_TIME_NONE;
  guint64 frames_dropped_last = G_MAXUINT64;
  AUTOCIRCULATE_TRANSFER transfer;

  if (self->output_cpu_core != G_MAXUINT) {
    cpu_set_t mask;
    pthread_t current_thread = pthread_self();

    CPU_ZERO(&mask);
    CPU_SET(self->output_cpu_core, &mask);

    if (pthread_setaffinity_np(current_thread, sizeof(mask), &mask) != 0) {
      GST_ERROR_OBJECT(self,
                       "Failed to set affinity for current thread to core %u",
                       self->output_cpu_core);
    }
  }

  g_mutex_lock(&self->queue_lock);
restart:
  if (self->draining && gst_queue_array_get_length(self->queue) == 0) {
    GST_DEBUG_OBJECT(self, "Drained");
    self->draining = FALSE;
    g_cond_signal(&self->drain_cond);
  }

  GST_DEBUG_OBJECT(self, "Waiting for playing or shutdown");
  while ((!self->playing && !self->shutdown) ||
         (self->playing &&
          gst_queue_array_get_length(self->queue) < self->queue_size / 2 &&
          !self->eos))
    g_cond_wait(&self->queue_cond, &self->queue_lock);
  if (self->shutdown) {
    GST_DEBUG_OBJECT(self, "Shutting down");
    g_mutex_unlock(&self->queue_lock);
    return;
  }

  GST_DEBUG_OBJECT(self, "Starting playing");
  g_mutex_unlock(&self->queue_lock);

  {
    // Make sure to globally lock here as the routing settings and others are
    // global shared state
    ShmMutexLocker locker;

    self->device->device->AutoCirculateStop(self->channel);

    self->device->device->EnableOutputInterrupt(self->channel);
    self->device->device->SubscribeOutputVerticalEvent(self->channel);
    if (!self->device->device->AutoCirculateInitForOutput(
            self->channel, self->queue_size / 2, self->audio_system,
            AUTOCIRCULATE_WITH_RP188 | AUTOCIRCULATE_WITH_ANC, 1)) {
      GST_ELEMENT_ERROR(self, STREAM, FAILED, (NULL),
                        ("Failed to initialize autocirculate"));
      goto out;
    }
    self->device->device->AutoCirculateStart(self->channel);
  }

  gst_clear_object(&clock);
  clock = gst_element_get_clock(GST_ELEMENT_CAST(self));
  frames_renderded_start = G_MAXUINT64;
  frames_renderded_start_time = GST_CLOCK_TIME_NONE;
  frames_dropped_last = G_MAXUINT64;

  transfer.acANCBuffer.Allocate(2048);
  if (self->configured_info.interlace_mode !=
      GST_VIDEO_INTERLACE_MODE_INTERLEAVED)
    transfer.acANCField2Buffer.Allocate(2048);

  g_mutex_lock(&self->queue_lock);
  while (self->playing && !self->shutdown &&
         !(self->draining && gst_queue_array_get_length(self->queue) == 0)) {
    AUTOCIRCULATE_STATUS status;

    self->device->device->AutoCirculateGetStatus(self->channel, status);

    GST_TRACE_OBJECT(self,
                     "Start frame %d "
                     "end frame %d "
                     "active frame %d "
                     "start time %" G_GUINT64_FORMAT
                     " "
                     "current time %" G_GUINT64_FORMAT
                     " "
                     "frames processed %u "
                     "frames dropped %u "
                     "buffer level %u",
                     status.acStartFrame, status.acEndFrame,
                     status.acActiveFrame, status.acRDTSCStartTime,
                     status.acRDTSCCurrentTime, status.acFramesProcessed,
                     status.acFramesDropped, status.acBufferLevel);

    // Trivial drift calculation
    //
    // TODO: Should probably take averages over a timespan (say 1 minute) into a
    // ringbuffer and calculate a linear regression over them
    // FIXME: Add some compensation by dropping/duplicating frames as needed
    // but make this configurable
    // FIXME: Should use transfer.acTransferStatus.acFrameStamp after
    // AutoCirculateTransfer()
    if (frames_renderded_start_time == GST_CLOCK_TIME_NONE &&
        status.acRDTSCStartTime != 0 &&
        status.acFramesProcessed + status.acFramesDropped > self->queue_size &&
        clock) {
      frames_renderded_start =
          status.acFramesProcessed + status.acFramesDropped;
      frames_renderded_start_time = gst_clock_get_time(clock);
    }

    if (clock && frames_renderded_start_time != GST_CLOCK_TIME_NONE) {
      GstClockTime now = gst_clock_get_time(clock);
      GstClockTime diff = now - frames_renderded_start_time;
      guint64 frames_rendered =
          (status.acFramesProcessed + status.acFramesDropped) -
          frames_renderded_start;
      guint64 frames_produced =
          gst_util_uint64_scale(diff, self->configured_info.fps_n,
                                self->configured_info.fps_d * GST_SECOND);
      gdouble fps_rendered = ((gdouble)frames_rendered * GST_SECOND) / diff;

      GST_TRACE_OBJECT(self,
                       "Frames rendered %" G_GUINT64_FORMAT
                       ", frames produced %" G_GUINT64_FORMAT
                       ", FPS rendered %lf",
                       frames_rendered, frames_produced, fps_rendered);
    }

    // Detect if we were too slow with providing frames and report if that was
    // the case together with the amount of frames dropped
    if (frames_dropped_last == G_MAXUINT64) {
      frames_dropped_last = status.acFramesDropped;
    } else if (frames_dropped_last < status.acFramesDropped) {
      GST_WARNING_OBJECT(self, "Dropped %" G_GUINT64_FORMAT " frames",
                         status.acFramesDropped - frames_dropped_last);

      GstClockTime timestamp =
          gst_util_uint64_scale(status.acFramesProcessed + frames_dropped_last,
                                self->configured_info.fps_n,
                                self->configured_info.fps_d * GST_SECOND);
      GstClockTime timestamp_end = gst_util_uint64_scale(
          status.acFramesProcessed + status.acFramesDropped,
          self->configured_info.fps_n,
          self->configured_info.fps_d * GST_SECOND);
      GstMessage *msg = gst_message_new_qos(
          GST_OBJECT_CAST(self), TRUE, GST_CLOCK_TIME_NONE, GST_CLOCK_TIME_NONE,
          timestamp, timestamp_end - timestamp);
      gst_element_post_message(GST_ELEMENT_CAST(self), msg);

      frames_dropped_last = status.acFramesDropped;
    }

    if (status.GetNumAvailableOutputFrames() > 1) {
      QueueItem item, *item_p;

      while ((item_p = (QueueItem *)gst_queue_array_pop_head_struct(
                  self->queue)) == NULL &&
             self->playing && !self->shutdown && !self->draining) {
        GST_DEBUG_OBJECT(
            self,
            "Element queue underrun, waiting for more frames or shutdown");
        g_cond_wait(&self->queue_cond, &self->queue_lock);
      }

      if (!self->playing || self->shutdown || (!item_p && self->draining)) {
        if (item_p && item_p->type == QUEUE_ITEM_TYPE_FRAME) {
          gst_video_frame_unmap(&item_p->frame);
          if (item_p->audio_buffer) {
            gst_buffer_unmap(item_p->audio_buffer, &item_p->audio_map);
            gst_buffer_unref(item_p->audio_buffer);
          }
          if (item_p->anc_packet_list) {
            delete item_p->anc_packet_list;
          }
        }
        break;
      }

      if (item_p && item_p->type != QUEUE_ITEM_TYPE_FRAME) {
        continue;
      }

      GST_TRACE_OBJECT(self, "%u frames queued",
                       gst_queue_array_get_length(self->queue));

      item = *item_p;
      g_mutex_unlock(&self->queue_lock);

      GST_TRACE_OBJECT(self,
                       "Transferring frame: "
                       "Video %p %" G_GSIZE_FORMAT
                       " "
                       "Audio %p %" G_GSIZE_FORMAT,
                       GST_VIDEO_FRAME_PLANE_DATA(&item.frame, 0),
                       GST_VIDEO_FRAME_SIZE(&item.frame),
                       item.audio_buffer ? item.audio_map.data : NULL,
                       item.audio_buffer ? item.audio_map.size : 0);

      // Set timecodes if provided by upstream
      if (item.tc.IsValid() && item.tc.fDBB != 0xffffffff) {
        NTV2TimeCodes timecodes;

        timecodes[::NTV2ChannelToTimecodeIndex(self->channel, false)] = item.tc;
        timecodes[::NTV2ChannelToTimecodeIndex(self->channel, true)] = item.tc;
        if (self->configured_info.interlace_mode !=
            GST_VIDEO_INTERLACE_MODE_PROGRESSIVE)
          timecodes[::NTV2ChannelToTimecodeIndex(self->channel, false, true)] =
              item.tc;
        transfer.SetOutputTimeCodes(timecodes);
      }

      transfer.SetVideoBuffer(
          (guint *)GST_VIDEO_FRAME_PLANE_DATA(&item.frame, 0),
          GST_VIDEO_FRAME_SIZE(&item.frame));
      if (item.audio_buffer) {
        transfer.SetAudioBuffer((guint *)item.audio_map.data,
                                item.audio_map.size);
      }

      // Clear VANC and fill in captions as needed
      transfer.acANCBuffer.Fill(ULWord(0));
      transfer.acANCField2Buffer.Fill(ULWord(0));

      if (item.anc_packet_list) {
        item.anc_packet_list->GetTransmitData(
            transfer.acANCBuffer, transfer.acANCField2Buffer,
            self->configured_info.interlace_mode !=
                GST_VIDEO_INTERLACE_MODE_PROGRESSIVE,
            self->f2_start_line);
      }

      if (!self->device->device->AutoCirculateTransfer(self->channel,
                                                       transfer)) {
        GST_WARNING_OBJECT(self, "Failed to transfer frame");
      }

      gst_video_frame_unmap(&item.frame);

      if (item.audio_buffer) {
        gst_buffer_unmap(item.audio_buffer, &item.audio_map);
        gst_buffer_unref(item.audio_buffer);
      }

      if (item.anc_packet_list) {
        delete item.anc_packet_list;
      }

      g_mutex_lock(&self->queue_lock);
    } else {
      g_mutex_unlock(&self->queue_lock);

      self->device->device->WaitForOutputVerticalInterrupt(self->channel);

      g_mutex_lock(&self->queue_lock);
    }
  }

out : {
  // Make sure to globally lock here as the routing settings and others are
  // global shared state
  ShmMutexLocker locker;

  self->device->device->AutoCirculateStop(self->channel);
  self->device->device->UnsubscribeOutputVerticalEvent(self->channel);
  self->device->device->DisableOutputInterrupt(self->channel);
}

  if ((!self->playing || self->draining) && !self->shutdown) goto restart;
  g_mutex_unlock(&self->queue_lock);

  gst_clear_object(&clock);

  GST_DEBUG_OBJECT(self, "Stopped");
}
