/*
 * Copyright (C) 2009 Ole André Vadla Ravnås <oravnas@cisco.com>
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

#include "miovideosrc.h"

#include "coremediabuffer.h"

#include <gst/interfaces/propertyprobe.h>
#include <gst/video/video.h>

#include <CoreVideo/CVHostTime.h>

#define DEFAULT_DEVICE_INDEX -1

#define FRAME_QUEUE_SIZE      2

#define FRAME_QUEUE_LOCK(instance) g_mutex_lock (instance->qlock)
#define FRAME_QUEUE_UNLOCK(instance) g_mutex_unlock (instance->qlock)
#define FRAME_QUEUE_WAIT(instance) \
    g_cond_wait (instance->qcond, instance->qlock)
#define FRAME_QUEUE_NOTIFY(instance) g_cond_signal (instance->qcond)

#define GST_MIO_REQUIRED_APIS \
    (GST_API_CORE_VIDEO | GST_API_CORE_MEDIA | GST_API_MIO)

GST_DEBUG_CATEGORY (gst_mio_video_src_debug);
#define GST_CAT_DEFAULT gst_mio_video_src_debug

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("UYVY") ";"
        GST_VIDEO_CAPS_YUV ("YUY2") ";"
        "image/jpeg, "
        "width = " GST_VIDEO_SIZE_RANGE ", "
        "height = " GST_VIDEO_SIZE_RANGE ", "
        "framerate = " GST_VIDEO_FPS_RANGE ";")
    );

enum
{
  PROP_0,
  PROP_DEVICE_UID,
  PROP_DEVICE_NAME,
  PROP_DEVICE_INDEX
};

typedef gboolean (*GstMIOCallback) (GstMIOVideoSrc * self, gpointer data);
#define GST_MIO_CALLBACK(cb) ((GstMIOCallback) (cb))

static gboolean gst_mio_video_src_open_device (GstMIOVideoSrc * self);
static void gst_mio_video_src_close_device (GstMIOVideoSrc * self);
static gboolean gst_mio_video_src_build_capture_graph_for
    (GstMIOVideoSrc * self, GstMIOVideoDevice * device);
static TundraStatus gst_mio_video_src_configure_output_node
    (GstMIOVideoSrc * self, TundraGraph * graph, guint node_id);

static void gst_mio_video_src_start_dispatcher (GstMIOVideoSrc * self);
static void gst_mio_video_src_stop_dispatcher (GstMIOVideoSrc * self);
static gpointer gst_mio_video_src_dispatcher_thread (gpointer data);
static gboolean gst_mio_video_src_perform (GstMIOVideoSrc * self,
    GstMIOCallback cb, gpointer data);
static gboolean gst_mio_video_src_perform_proxy (gpointer data);

static void gst_mio_video_src_probe_interface_init (gpointer g_iface,
    gpointer iface_data);

static void gst_mio_video_src_init_interfaces (GType type);

GST_BOILERPLATE_FULL (GstMIOVideoSrc, gst_mio_video_src, GstPushSrc,
    GST_TYPE_PUSH_SRC, gst_mio_video_src_init_interfaces);

static void
gst_mio_video_src_init (GstMIOVideoSrc * self, GstMIOVideoSrcClass * gclass)
{
  GstBaseSrc *base_src = GST_BASE_SRC_CAST (self);
  guint64 host_freq;

  gst_base_src_set_live (base_src, TRUE);
  gst_base_src_set_format (base_src, GST_FORMAT_TIME);

  host_freq = gst_gdouble_to_guint64 (CVGetHostClockFrequency ());
  if (host_freq <= GST_SECOND) {
    self->cv_ratio_n = GST_SECOND / host_freq;
    self->cv_ratio_d = 1;
  } else {
    self->cv_ratio_n = 1;
    self->cv_ratio_d = host_freq / GST_SECOND;
  }

  self->queue = g_queue_new ();
  self->qlock = g_mutex_new ();
  self->qcond = g_cond_new ();
}

static void
gst_mio_video_src_finalize (GObject * object)
{
  GstMIOVideoSrc *self = GST_MIO_VIDEO_SRC_CAST (object);

  g_cond_free (self->qcond);
  g_mutex_free (self->qlock);
  g_queue_free (self->queue);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_mio_video_src_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMIOVideoSrc *self = GST_MIO_VIDEO_SRC_CAST (object);

  switch (prop_id) {
    case PROP_DEVICE_UID:
      g_value_set_string (value, self->device_uid);
      break;
    case PROP_DEVICE_NAME:
      g_value_set_string (value, self->device_name);
      break;
    case PROP_DEVICE_INDEX:
      g_value_set_int (value, self->device_index);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mio_video_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMIOVideoSrc *self = GST_MIO_VIDEO_SRC_CAST (object);

  switch (prop_id) {
    case PROP_DEVICE_UID:
      g_free (self->device_uid);
      self->device_uid = g_value_dup_string (value);
      break;
    case PROP_DEVICE_NAME:
      g_free (self->device_name);
      self->device_name = g_value_dup_string (value);
      break;
    case PROP_DEVICE_INDEX:
      self->device_index = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_mio_video_src_change_state (GstElement * element, GstStateChange transition)
{
  GstMIOVideoSrc *self = GST_MIO_VIDEO_SRC_CAST (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      gst_mio_video_src_start_dispatcher (self);
      if (!gst_mio_video_src_perform (self,
              GST_MIO_CALLBACK (gst_mio_video_src_open_device), NULL)) {
        goto open_failed;
      }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_mio_video_src_perform (self,
          GST_MIO_CALLBACK (gst_mio_video_src_close_device), NULL);

      gst_mio_video_src_stop_dispatcher (self);
      break;
    default:
      break;
  }

  return ret;

  /* ERRORS */
open_failed:
  {
    gst_mio_video_src_stop_dispatcher (self);
    return GST_STATE_CHANGE_FAILURE;
  }
}

static GstCaps *
gst_mio_video_src_get_caps (GstBaseSrc * basesrc)
{
  GstMIOVideoSrc *self = GST_MIO_VIDEO_SRC_CAST (basesrc);
  GstCaps *result;

  if (self->device != NULL) {
    result =
        gst_caps_ref (gst_mio_video_device_get_available_caps (self->device));
  } else {
    result = NULL;              /* BaseSrc will return template caps */
  }

  return result;
}

static gboolean
gst_mio_video_src_do_set_caps (GstMIOVideoSrc * self, GstCaps * caps)
{
  TundraStatus status;

  if (self->device == NULL)
    goto no_device;

  if (!gst_mio_video_device_set_caps (self->device, caps))
    goto invalid_format;

  if (!gst_mio_video_src_build_capture_graph_for (self, self->device))
    goto graph_build_error;

  status = self->ctx->mio->TundraGraphInitialize (self->graph);
  if (status != kTundraSuccess)
    goto graph_init_error;

  status = self->ctx->mio->TundraGraphStart (self->graph);
  if (status != kTundraSuccess)
    goto graph_start_error;

  return TRUE;

  /* ERRORS */
no_device:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED, ("no device"), (NULL));
    return FALSE;
  }
invalid_format:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED, ("invalid format"), (NULL));
    return FALSE;
  }
graph_build_error:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
        ("failed to build capture graph"), (NULL));
    return FALSE;
  }
graph_init_error:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
        ("failed to initialize capture graph: %08x", status), (NULL));
    return FALSE;
  }
graph_start_error:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
        ("failed to start capture graph: %08x", status), (NULL));
    return FALSE;
  }
}

static gboolean
gst_mio_video_src_set_caps (GstBaseSrc * basesrc, GstCaps * caps)
{
  GstMIOVideoSrc *self = GST_MIO_VIDEO_SRC_CAST (basesrc);

  {
    gchar *str;

    str = gst_caps_to_string (caps);
    GST_DEBUG_OBJECT (self, "caps: %s", str);
    g_free (str);
  }

  return gst_mio_video_src_perform (self,
      GST_MIO_CALLBACK (gst_mio_video_src_do_set_caps), caps);
}

static gboolean
gst_mio_video_src_start (GstBaseSrc * basesrc)
{
  GstMIOVideoSrc *self = GST_MIO_VIDEO_SRC_CAST (basesrc);

  self->running = TRUE;
  self->prev_offset = GST_BUFFER_OFFSET_NONE;
  self->prev_format = NULL;

  return TRUE;
}

static gboolean
gst_mio_video_src_do_stop (GstMIOVideoSrc * self)
{
  TundraStatus status;

  if (self->graph == NULL)
    goto nothing_to_stop;

  status = self->ctx->mio->TundraGraphStop (self->graph);
  if (status != kTundraSuccess)
    goto graph_failed_to_stop;

  while (!g_queue_is_empty (self->queue))
    gst_buffer_unref (g_queue_pop_head (self->queue));

  self->ctx->cm->FigFormatDescriptionRelease (self->prev_format);
  self->prev_format = NULL;

  return TRUE;

nothing_to_stop:
  return TRUE;

graph_failed_to_stop:
  GST_WARNING_OBJECT (self, "failed to stop capture graph: %d", status);
  return FALSE;
}

static gboolean
gst_mio_video_src_stop (GstBaseSrc * basesrc)
{
  GstMIOVideoSrc *self = GST_MIO_VIDEO_SRC_CAST (basesrc);

  return gst_mio_video_src_perform (self,
      GST_MIO_CALLBACK (gst_mio_video_src_do_stop), NULL);
}

static gboolean
gst_mio_video_src_query (GstBaseSrc * basesrc, GstQuery * query)
{
  GstMIOVideoSrc *self = GST_MIO_VIDEO_SRC_CAST (basesrc);
  gboolean result = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:{
      GstClockTime min_latency, max_latency;

      if (self->device == NULL)
        goto beach;

      if (gst_mio_video_device_get_selected_format (self->device) == NULL)
        goto beach;

      min_latency = max_latency =
          gst_mio_video_device_get_duration (self->device);

      GST_DEBUG_OBJECT (self, "reporting latency of min %" GST_TIME_FORMAT
          " max %" GST_TIME_FORMAT,
          GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));

      gst_query_set_latency (query, TRUE, min_latency, max_latency);
      result = TRUE;
      break;
    }
    default:
      result = GST_BASE_SRC_CLASS (parent_class)->query (basesrc, query);
      break;
  }

beach:
  return result;
}

static gboolean
gst_mio_video_src_unlock (GstBaseSrc * basesrc)
{
  GstMIOVideoSrc *self = GST_MIO_VIDEO_SRC_CAST (basesrc);

  FRAME_QUEUE_LOCK (self);
  self->running = FALSE;
  FRAME_QUEUE_NOTIFY (self);
  FRAME_QUEUE_UNLOCK (self);

  return TRUE;
}

static gboolean
gst_mio_video_src_unlock_stop (GstBaseSrc * basesrc)
{
  return TRUE;
}

static GstFlowReturn
gst_mio_video_src_create (GstPushSrc * pushsrc, GstBuffer ** buf)
{
  GstMIOVideoSrc *self = GST_MIO_VIDEO_SRC_CAST (pushsrc);
  GstCMApi *cm = self->ctx->cm;
  CMFormatDescriptionRef format;

  FRAME_QUEUE_LOCK (self);
  while (self->running && g_queue_is_empty (self->queue))
    FRAME_QUEUE_WAIT (self);
  *buf = g_queue_pop_tail (self->queue);
  FRAME_QUEUE_UNLOCK (self);

  if (G_UNLIKELY (!self->running))
    goto shutting_down;

  format = cm->CMSampleBufferGetFormatDescription
      (GST_CORE_MEDIA_BUFFER (*buf)->sample_buf);
  if (self->prev_format != NULL &&
      !cm->CMFormatDescriptionEqual (format, self->prev_format)) {
    goto unexpected_format;
  }
  cm->FigFormatDescriptionRelease (self->prev_format);
  self->prev_format = cm->FigFormatDescriptionRetain (format);

  if (self->prev_offset == GST_BUFFER_OFFSET_NONE ||
      GST_BUFFER_OFFSET (*buf) - self->prev_offset != 1) {
    GST_BUFFER_FLAG_SET (*buf, GST_BUFFER_FLAG_DISCONT);
  }
  self->prev_offset = GST_BUFFER_OFFSET (*buf);

  return GST_FLOW_OK;

  /* ERRORS */
shutting_down:
  {
    if (*buf != NULL) {
      gst_buffer_unref (*buf);
      *buf = NULL;
    }

    return GST_FLOW_WRONG_STATE;
  }
unexpected_format:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, READ,
        ("capture format changed unexpectedly"),
        ("another application likely reconfigured the device"));

    if (*buf != NULL) {
      gst_buffer_unref (*buf);
      *buf = NULL;
    }

    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_mio_video_src_open_device (GstMIOVideoSrc * self)
{
  GError *error = NULL;
  GList *devices = NULL, *walk;
  guint device_idx;

  self->ctx = gst_core_media_ctx_new (GST_API_CORE_VIDEO | GST_API_CORE_MEDIA
      | GST_API_MIO, &error);
  if (error != NULL)
    goto api_error;

  devices = gst_mio_video_device_list_create (self->ctx);
  if (devices == NULL)
    goto no_devices;

  for (walk = devices, device_idx = 0; walk != NULL; walk = walk->next) {
    GstMIOVideoDevice *device = walk->data;
    gboolean match;

    if (self->device_uid != NULL) {
      match = g_ascii_strcasecmp (gst_mio_video_device_get_uid (device),
          self->device_uid) == 0;
    } else if (self->device_name != NULL) {
      match = g_ascii_strcasecmp (gst_mio_video_device_get_name (device),
          self->device_name) == 0;
    } else if (self->device_index >= 0) {
      match = device_idx == self->device_index;
    } else {
      match = TRUE;             /* pick the first entry */
    }

    if (self->device != NULL)
      match = FALSE;

    GST_DEBUG_OBJECT (self, "%c device[%u] = handle: %d name: '%s' uid: '%s'",
        (match) ? '*' : '-', device_idx,
        gst_mio_video_device_get_handle (device),
        gst_mio_video_device_get_name (device),
        gst_mio_video_device_get_uid (device));

    /*gst_mio_video_device_print_debug_info (device); */

    if (match)
      self->device = g_object_ref (device);

    device_idx++;
  }

  if (self->device == NULL)
    goto no_such_device;

  if (!gst_mio_video_device_open (self->device))
    goto device_busy_or_gone;

  gst_mio_video_device_list_destroy (devices);
  return TRUE;

  /* ERRORS */
api_error:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED, ("API error"),
        ("%s", error->message));
    g_clear_error (&error);
    goto any_error;
  }
no_devices:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("no video capture devices found"), (NULL));
    goto any_error;
  }
no_such_device:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("specified video capture device not found"), (NULL));
    goto any_error;
  }
device_busy_or_gone:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, BUSY,
        ("failed to start capture (device already in use or gone)"), (NULL));
    goto any_error;
  }
any_error:
  {
    if (devices != NULL) {
      gst_mio_video_device_list_destroy (devices);
    }
    if (self->ctx != NULL) {
      g_object_unref (self->ctx);
      self->ctx = NULL;
    }
    return FALSE;
  }
}

static void
gst_mio_video_src_close_device (GstMIOVideoSrc * self)
{
  self->ctx->mio->TundraGraphUninitialize (self->graph);
  self->ctx->mio->TundraGraphRelease (self->graph);
  self->graph = NULL;

  gst_mio_video_device_close (self->device);
  g_object_unref (self->device);
  self->device = NULL;

  g_object_unref (self->ctx);
  self->ctx = NULL;
}

#define CHECK_TUNDRA_ERROR(fn)      \
  if (status != kTundraSuccess) {   \
    last_function_name = fn;        \
    goto tundra_error;              \
  }

static gboolean
gst_mio_video_src_build_capture_graph_for (GstMIOVideoSrc * self,
    GstMIOVideoDevice * device)
{
  GstMIOApi *mio = self->ctx->mio;
  const gchar *last_function_name;
  TundraGraph *graph = NULL;
  TundraTargetSpec spec = { 0, };
  TundraUnitID input_node = -1;
  gpointer input_info;
  TundraObjectID device_handle;
  TundraUnitID sync_node = -1;
  guint8 is_master;
  guint sync_direction;
  TundraUnitID output_node = -1;
  TundraStatus status;

  const gint node_id_input = 1;
  const gint node_id_sync = 22;
  const gint node_id_output = 16;

  /*
   * Graph
   */
  status = mio->TundraGraphCreate (kCFAllocatorDefault, &graph);
  CHECK_TUNDRA_ERROR ("TundraGraphCreate");

  /*
   * Node: input
   */
  spec.name = kTundraUnitInput;
  spec.scope = kTundraScopeDAL;
  spec.vendor = kTundraVendorApple;
  status = mio->TundraGraphCreateNode (graph, node_id_input, 0, 0, &spec, 0,
      &input_node);
  CHECK_TUNDRA_ERROR ("TundraGraphCreateNode(input)");

  /* store node info for setting clock provider */
  input_info = NULL;
  status = mio->TundraGraphGetNodeInfo (graph, input_node, 0, 0, 0, 0,
      &input_info);
  CHECK_TUNDRA_ERROR ("TundraGraphGetNodeInfo(input)");

  /* set device handle */
  device_handle = gst_mio_video_device_get_handle (device);
  status = mio->TundraGraphSetProperty (graph, node_id_input, 0,
      kTundraInputPropertyDeviceID, 0, 0, &device_handle,
      sizeof (device_handle));
  CHECK_TUNDRA_ERROR ("TundraGraphSetProperty(input, DeviceID)");

  /*
   * Node: sync
   */
  spec.name = kTundraUnitSync;
  spec.scope = kTundraScopeVSyn;
  status = mio->TundraGraphCreateNode (graph, node_id_sync, 0, 0, &spec, 0,
      &sync_node);
  CHECK_TUNDRA_ERROR ("TundraGraphCreateNode(sync)");
  status = mio->TundraGraphSetProperty (graph, node_id_sync, 0,
      kTundraSyncPropertyClockProvider, 0, 0, &input_info, sizeof (input_info));
  CHECK_TUNDRA_ERROR ("TundraGraphSetProperty(sync, ClockProvider)");
  is_master = TRUE;
  status = mio->TundraGraphSetProperty (graph, node_id_sync, 0,
      kTundraSyncPropertyMasterSynchronizer, 0, 0,
      &is_master, sizeof (is_master));
  CHECK_TUNDRA_ERROR ("TundraGraphSetProperty(sync, MasterSynchronizer)");
  sync_direction = 0;
  status = mio->TundraGraphSetProperty (graph, node_id_sync, 0,
      kTundraSyncPropertySynchronizationDirection, 0, 0,
      &sync_direction, sizeof (sync_direction));
  CHECK_TUNDRA_ERROR ("TundraGraphSetProperty(sync, SynchronizationDirection)");

  /*
   * Node: output
   */
  spec.name = kTundraUnitOutput;
  spec.scope = kTundraScope2PRC;
  status = mio->TundraGraphCreateNode (graph, node_id_output, 0, 0, &spec, 0,
      &output_node);
  CHECK_TUNDRA_ERROR ("TundraGraphCreateNode(output)");
  status = gst_mio_video_src_configure_output_node (self, graph,
      node_id_output);
  CHECK_TUNDRA_ERROR ("TundraGraphSetProperty(output, Delegate)");

  /*
   * Connect the nodes
   */
  status = mio->TundraGraphConnectNodeInput (graph, input_node, 0,
      sync_node, 0);
  CHECK_TUNDRA_ERROR ("TundraGraphConnectNodeInput(input, sync)");
  status = mio->TundraGraphConnectNodeInput (graph, sync_node, 0,
      output_node, 0);
  CHECK_TUNDRA_ERROR ("TundraGraphConnectNodeInput(sync, output)");

  self->graph = graph;

  return TRUE;

tundra_error:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
        ("%s failed (status=%d)", last_function_name, (gint) status), (NULL));
    goto any_error;
  }
any_error:
  {
    mio->TundraGraphRelease (graph);
    return FALSE;
  }
}

static GstClockTime
gst_mio_video_src_get_timestamp (GstMIOVideoSrc * self, CMSampleBufferRef sbuf)
{
  GstClock *clock;
  GstClockTime base_time;
  GstClockTime timestamp;

  GST_OBJECT_LOCK (self);
  if ((clock = GST_ELEMENT_CLOCK (self)) != NULL) {
    gst_object_ref (clock);
  }
  base_time = GST_ELEMENT_CAST (self)->base_time;
  GST_OBJECT_UNLOCK (self);

  if (G_UNLIKELY (clock == NULL))
    goto no_clock;

  timestamp = GST_CLOCK_TIME_NONE;

  /*
   * If the current clock is GstSystemClock, we know that it's using the
   * CoreAudio/CoreVideo clock. As such we may use the timestamp attached
   * to the CMSampleBuffer.
   */
  if (G_TYPE_FROM_INSTANCE (clock) == GST_TYPE_SYSTEM_CLOCK) {
    CFNumberRef number;
    UInt64 ht;

    number = self->ctx->cm->CMGetAttachment (sbuf,
        *self->ctx->mio->kTundraSampleBufferAttachmentKey_HostTime, NULL);
    if (number != NULL && CFNumberGetValue (number, kCFNumberSInt64Type, &ht)) {
      timestamp = gst_util_uint64_scale_int (ht,
          self->cv_ratio_n, self->cv_ratio_d);
    }
  }

  if (!GST_CLOCK_TIME_IS_VALID (timestamp)) {
    timestamp = gst_clock_get_time (clock);
  }

  if (timestamp > base_time)
    timestamp -= base_time;
  else
    timestamp = 0;

  gst_object_unref (clock);

  return timestamp;

no_clock:
  return GST_CLOCK_TIME_NONE;
}

static TundraStatus
gst_mio_video_src_output_render (gpointer instance, gpointer unk1,
    gpointer unk2, gpointer unk3, CMSampleBufferRef sample_buf)
{
  GstMIOVideoSrc *self = GST_MIO_VIDEO_SRC_CAST (instance);
  GstBuffer *buf;
  CFNumberRef number;
  UInt32 seq;

  buf = gst_core_media_buffer_new (self->ctx, sample_buf);
  if (G_UNLIKELY (buf == NULL))
    goto buffer_creation_failed;

  number = self->ctx->cm->CMGetAttachment (sample_buf,
      *self->ctx->mio->kTundraSampleBufferAttachmentKey_SequenceNumber, NULL);
  if (number != NULL && CFNumberGetValue (number, kCFNumberSInt32Type, &seq)) {
    GST_BUFFER_OFFSET (buf) = seq;
    GST_BUFFER_OFFSET_END (buf) = seq + 1;
  }

  GST_BUFFER_TIMESTAMP (buf) = gst_mio_video_src_get_timestamp (self,
      sample_buf);

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    GST_BUFFER_DURATION (buf) =
        gst_mio_video_device_get_duration (self->device);
  }

  FRAME_QUEUE_LOCK (self);
  if (g_queue_get_length (self->queue) == FRAME_QUEUE_SIZE)
    gst_buffer_unref (g_queue_pop_tail (self->queue));
  g_queue_push_head (self->queue, buf);
  FRAME_QUEUE_NOTIFY (self);
  FRAME_QUEUE_UNLOCK (self);

  return kTundraSuccess;

buffer_creation_failed:
  GST_WARNING_OBJECT (instance, "failed to create buffer");
  return kTundraSuccess;
}

static TundraStatus
gst_mio_video_src_output_initialize (gpointer instance)
{
  GST_DEBUG_OBJECT (instance, "%s", G_STRFUNC);

  return kTundraSuccess;
}

static TundraStatus
gst_mio_video_src_output_uninitialize (gpointer instance)
{
  GST_DEBUG_OBJECT (instance, "%s", G_STRFUNC);

  return kTundraSuccess;
}

static TundraStatus
gst_mio_video_src_output_start (gpointer instance)
{
  GST_DEBUG_OBJECT (instance, "%s", G_STRFUNC);

  return kTundraSuccess;
}

static TundraStatus
gst_mio_video_src_output_stop (gpointer instance)
{
  return kTundraSuccess;
}

static TundraStatus
gst_mio_video_src_output_reset (gpointer instance)
{
  GST_DEBUG_OBJECT (instance, "%s", G_STRFUNC);

  return kTundraSuccess;
}

static TundraStatus
gst_mio_video_src_output_deallocate (gpointer instance)
{
  GST_DEBUG_OBJECT (instance, "%s", G_STRFUNC);

  return kTundraSuccess;
}

static gboolean
gst_mio_video_src_output_can_render_now (gpointer instance, guint * unk)
{
  if (unk != NULL)
    *unk = 0;

  return TRUE;
}

static CFArrayRef
gst_mio_video_src_output_available_formats (gpointer instance,
    gboolean ensure_only)
{
  GstMIOVideoSrc *self = GST_MIO_VIDEO_SRC (instance);
  CMFormatDescriptionRef format_desc;

  GST_DEBUG_OBJECT (self, "%s: ensure_only=%d", G_STRFUNC, ensure_only);

  if (ensure_only)
    return NULL;

  g_assert (self->device != NULL);
  format_desc = gst_mio_video_device_get_selected_format (self->device);
  g_assert (format_desc != NULL);

  return CFArrayCreate (kCFAllocatorDefault, (const void **) &format_desc, 1,
      &kCFTypeArrayCallBacks);
}

static TundraStatus
gst_mio_video_src_output_copy_clock (gpointer instance)
{
  GST_DEBUG_OBJECT (instance, "%s", G_STRFUNC);

  return kTundraSuccess;
}

static TundraStatus
gst_mio_video_src_output_get_property_info (gpointer instance, guint prop_id)
{
  GST_DEBUG_OBJECT (instance, "%s: prop_id=%u", G_STRFUNC, prop_id);

  if (prop_id == kTundraInputUnitProperty_SourcePath)
    return kTundraSuccess;

  return kTundraNotSupported;
}

static TundraStatus
gst_mio_video_src_output_get_property (gpointer instance, guint prop_id)
{
  GST_DEBUG_OBJECT (instance, "%s", G_STRFUNC);

  if (prop_id == kTundraInputUnitProperty_SourcePath)
    return kTundraSuccess;

  return kTundraNotSupported;
}

static TundraStatus
gst_mio_video_src_output_set_property (gpointer instance, guint prop_id)
{
  GST_DEBUG_OBJECT (instance, "%s: prop_id=%u", G_STRFUNC, prop_id);

  if (prop_id == kTundraInputUnitProperty_SourcePath)
    return kTundraSuccess;

  return kTundraNotSupported;
}

static TundraStatus
gst_mio_video_src_configure_output_node (GstMIOVideoSrc * self,
    TundraGraph * graph, guint node_id)
{
  TundraStatus status;
  TundraOutputDelegate d = { 0, };

  d.unk1 = 2;
  d.instance = self;
  d.Render = gst_mio_video_src_output_render;
  d.Initialize = gst_mio_video_src_output_initialize;
  d.Uninitialize = gst_mio_video_src_output_uninitialize;
  d.Start = gst_mio_video_src_output_start;
  d.Stop = gst_mio_video_src_output_stop;
  d.Reset = gst_mio_video_src_output_reset;
  d.Deallocate = gst_mio_video_src_output_deallocate;
  d.CanRenderNow = gst_mio_video_src_output_can_render_now;
  d.AvailableFormats = gst_mio_video_src_output_available_formats;
  d.CopyClock = gst_mio_video_src_output_copy_clock;
  d.GetPropertyInfo = gst_mio_video_src_output_get_property_info;
  d.GetProperty = gst_mio_video_src_output_get_property;
  d.SetProperty = gst_mio_video_src_output_set_property;

  status = self->ctx->mio->TundraGraphSetProperty (graph, node_id, 0,
      kTundraOutputPropertyDelegate, 0, 0, &d, sizeof (d));

  return status;
}

static void
gst_mio_video_src_start_dispatcher (GstMIOVideoSrc * self)
{
  g_assert (self->dispatcher_ctx == NULL && self->dispatcher_loop == NULL);
  g_assert (self->dispatcher_thread == NULL);

  self->dispatcher_ctx = g_main_context_new ();
  self->dispatcher_loop = g_main_loop_new (self->dispatcher_ctx, TRUE);
  self->dispatcher_thread =
      g_thread_create (gst_mio_video_src_dispatcher_thread, self, TRUE, NULL);
}

static void
gst_mio_video_src_stop_dispatcher (GstMIOVideoSrc * self)
{
  g_assert (self->dispatcher_ctx != NULL && self->dispatcher_loop != NULL);
  g_assert (self->dispatcher_thread != NULL);

  g_main_loop_quit (self->dispatcher_loop);
  g_thread_join (self->dispatcher_thread);
  self->dispatcher_thread = NULL;

  g_main_loop_unref (self->dispatcher_loop);
  self->dispatcher_loop = NULL;

  g_main_context_unref (self->dispatcher_ctx);
  self->dispatcher_ctx = NULL;
}

static gpointer
gst_mio_video_src_dispatcher_thread (gpointer data)
{
  GstMIOVideoSrc *self = data;

  g_main_loop_run (self->dispatcher_loop);

  return NULL;
}

typedef struct
{
  GstMIOVideoSrc *self;
  GstMIOCallback callback;
  gpointer data;
  gboolean result;

  GMutex *mutex;
  GCond *cond;
  gboolean finished;
} GstMIOPerformCtx;

static gboolean
gst_mio_video_src_perform (GstMIOVideoSrc * self, GstMIOCallback cb,
    gpointer data)
{
  GstMIOPerformCtx ctx;
  GSource *source;

  ctx.self = self;
  ctx.callback = cb;
  ctx.data = data;
  ctx.result = FALSE;

  ctx.mutex = g_mutex_new ();
  ctx.cond = g_cond_new ();
  ctx.finished = FALSE;

  source = g_idle_source_new ();
  g_source_set_callback (source, gst_mio_video_src_perform_proxy, &ctx, NULL);
  g_source_attach (source, self->dispatcher_ctx);

  g_mutex_lock (ctx.mutex);
  while (!ctx.finished)
    g_cond_wait (ctx.cond, ctx.mutex);
  g_mutex_unlock (ctx.mutex);

  g_source_destroy (source);
  g_source_unref (source);

  g_cond_free (ctx.cond);
  g_mutex_free (ctx.mutex);

  return ctx.result;
}

static gboolean
gst_mio_video_src_perform_proxy (gpointer data)
{
  GstMIOPerformCtx *ctx = data;

  ctx->result = ctx->callback (ctx->self, ctx->data);

  g_mutex_lock (ctx->mutex);
  ctx->finished = TRUE;
  g_cond_signal (ctx->cond);
  g_mutex_unlock (ctx->mutex);

  return FALSE;
}

static const GList *
gst_mio_video_src_probe_get_properties (GstPropertyProbe * probe)
{
  static gsize init_value = 0;

  if (g_once_init_enter (&init_value)) {
    GObjectClass *klass;
    GList *props = NULL;

    klass = G_OBJECT_GET_CLASS (probe);

    props = g_list_append (props,
        g_object_class_find_property (klass, "device-uid"));
    props = g_list_append (props,
        g_object_class_find_property (klass, "device-name"));
    props = g_list_append (props,
        g_object_class_find_property (klass, "device-index"));

    g_once_init_leave (&init_value, GPOINTER_TO_SIZE (props));
  }

  return GSIZE_TO_POINTER (init_value);
}

static GValueArray *
gst_mio_video_src_probe_get_values (GstPropertyProbe * probe, guint prop_id,
    const GParamSpec * pspec)
{
  GValueArray *values;
  GstCoreMediaCtx *ctx = NULL;
  GError *error = NULL;
  GList *devices = NULL, *walk;
  guint device_idx;

  values = g_value_array_new (3);

  ctx = gst_core_media_ctx_new (GST_MIO_REQUIRED_APIS, &error);
  if (error != NULL)
    goto beach;

  devices = gst_mio_video_device_list_create (ctx);
  if (devices == NULL)
    goto beach;

  for (walk = devices, device_idx = 0; walk != NULL; walk = walk->next) {
    GstMIOVideoDevice *device = walk->data;
    GValue value = { 0, };

    switch (prop_id) {
      case PROP_DEVICE_UID:
      case PROP_DEVICE_NAME:
      {
        const gchar *str;

        if (prop_id == PROP_DEVICE_UID)
          str = gst_mio_video_device_get_uid (device);
        else
          str = gst_mio_video_device_get_name (device);

        g_value_init (&value, G_TYPE_STRING);
        g_value_set_string (&value, str);

        break;
      }
      case PROP_DEVICE_INDEX:
      {
        g_value_init (&value, G_TYPE_INT);
        g_value_set_int (&value, device_idx);

        break;
      }
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
        goto beach;
    }

    g_value_array_append (values, &value);
    g_value_unset (&value);

    device_idx++;
  }

beach:
  if (devices != NULL)
    gst_mio_video_device_list_destroy (devices);
  if (ctx != NULL)
    g_object_unref (ctx);
  g_clear_error (&error);

  return values;
}

static void
gst_mio_video_src_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple (element_class,
      "Video Source (MIO)", "Source/Video",
      "Reads frames from a Mac OS X MIO device",
      "Ole André Vadla Ravnås <oravnas@cisco.com>");

  gst_element_class_add_static_pad_template (element_class, &src_template);
}

static void
gst_mio_video_src_class_init (GstMIOVideoSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstBaseSrcClass *gstbasesrc_class = GST_BASE_SRC_CLASS (klass);
  GstPushSrcClass *gstpushsrc_class = GST_PUSH_SRC_CLASS (klass);

  gobject_class->finalize = gst_mio_video_src_finalize;
  gobject_class->get_property = gst_mio_video_src_get_property;
  gobject_class->set_property = gst_mio_video_src_set_property;

  gstelement_class->change_state = gst_mio_video_src_change_state;

  gstbasesrc_class->get_caps = gst_mio_video_src_get_caps;
  gstbasesrc_class->set_caps = gst_mio_video_src_set_caps;
  gstbasesrc_class->start = gst_mio_video_src_start;
  gstbasesrc_class->stop = gst_mio_video_src_stop;
  gstbasesrc_class->query = gst_mio_video_src_query;
  gstbasesrc_class->unlock = gst_mio_video_src_unlock;
  gstbasesrc_class->unlock_stop = gst_mio_video_src_unlock_stop;

  gstpushsrc_class->create = gst_mio_video_src_create;

  g_object_class_install_property (gobject_class, PROP_DEVICE_UID,
      g_param_spec_string ("device-uid", "Device UID",
          "Unique ID of the desired device", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DEVICE_NAME,
      g_param_spec_string ("device-name", "Device Name",
          "Name of the desired device", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DEVICE_INDEX,
      g_param_spec_int ("device-index", "Device Index",
          "Zero-based device index of the desired device",
          -1, G_MAXINT, DEFAULT_DEVICE_INDEX,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (gst_mio_video_src_debug, "miovideosrc",
      0, "Mac OS X CoreMedia video source");
}

static void
gst_mio_video_src_init_interfaces (GType type)
{
  static const GInterfaceInfo probe_info = {
    gst_mio_video_src_probe_interface_init,
    NULL,
    NULL
  };

  g_type_add_interface_static (type, GST_TYPE_PROPERTY_PROBE, &probe_info);
}

static void
gst_mio_video_src_probe_interface_init (gpointer g_iface, gpointer iface_data)
{
  GstPropertyProbeInterface *iface = g_iface;

  iface->get_properties = gst_mio_video_src_probe_get_properties;
  iface->get_values = gst_mio_video_src_probe_get_values;
}
