/* GStreamer
 * Copyright (C) 2022 Seungha Yang <seungha@centricular.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstnvencoder.h"
#include "gstcudautils.h"
#include "gstcudamemory.h"
#include "gstcudabufferpool.h"
#include <string.h>

#ifdef HAVE_NVCODEC_GST_D3D11
#include <gst/d3d11/gstd3d11.h>
#endif

#ifdef G_OS_WIN32
#include <wrl.h>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */
#endif

GST_DEBUG_CATEGORY_STATIC (gst_nv_encoder_debug);
#define GST_CAT_DEFAULT gst_nv_encoder_debug

#define GET_LOCK(e) (&(GST_NV_ENCODER_CAST(e)->priv->lock))
#define GST_NV_ENCODER_LOCK(e) G_STMT_START { \
  GST_TRACE_OBJECT (e, "Locking from thread %p", g_thread_self ()); \
  g_mutex_lock(GET_LOCK(e)); \
  GST_TRACE_OBJECT (e, "Locked from thread %p", g_thread_self ()); \
} G_STMT_END

#define GST_NV_ENCODER_UNLOCK(e) G_STMT_START { \
  GST_TRACE_OBJECT (e, "Unlocking from thread %p", g_thread_self ()); \
  g_mutex_unlock(GET_LOCK(e)); \
} G_STMT_END

struct _GstNvEncoderPrivate
{
  GstCudaContext *context;
#ifdef HAVE_NVCODEC_GST_D3D11
  GstD3D11Device *device;
#endif

  gint64 dxgi_adapter_luid;
  guint cuda_device_id;
  gboolean d3d11_mode;

  NV_ENC_INITIALIZE_PARAMS init_params;
  NV_ENC_CONFIG config;
  gpointer session;

  GstVideoCodecState *input_state;

  GstBufferPool *internal_pool;

  GstClockTime dts_offset;

  /* Array of GstNvEncoderTask, holding ownership */
  GArray *task_pool;

  GQueue free_tasks;
  GQueue output_tasks;

  GMutex lock;
  GCond cond;

  GThread *encoding_thread;

  GstFlowReturn last_flow;
};

#define gst_nv_encoder_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GstNvEncoder, gst_nv_encoder,
    GST_TYPE_VIDEO_ENCODER);

static void gst_nv_encoder_finalize (GObject * object);
static void gst_nv_encoder_set_context (GstElement * element,
    GstContext * context);
static gboolean gst_nv_encoder_open (GstVideoEncoder * encoder);
static gboolean gst_nv_encoder_close (GstVideoEncoder * encoder);
static gboolean gst_nv_encoder_stop (GstVideoEncoder * encoder);
static gboolean gst_nv_encoder_sink_query (GstVideoEncoder * encoder,
    GstQuery * query);
static gboolean gst_nv_encoder_src_query (GstVideoEncoder * encoder,
    GstQuery * query);
static gboolean gst_nv_encoder_propose_allocation (GstVideoEncoder *
    encoder, GstQuery * query);
static gboolean gst_nv_encoder_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state);
static GstFlowReturn gst_nv_encoder_handle_frame (GstVideoEncoder *
    encoder, GstVideoCodecFrame * frame);
static GstFlowReturn gst_nv_encoder_finish (GstVideoEncoder * encoder);
static gboolean gst_nv_encoder_flush (GstVideoEncoder * encoder);
static void gst_nv_encoder_task_clear (GstNvEncoderTask * task);

static void
gst_nv_encoder_class_init (GstNvEncoderClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoEncoderClass *videoenc_class = GST_VIDEO_ENCODER_CLASS (klass);

  object_class->finalize = gst_nv_encoder_finalize;

  element_class->set_context = GST_DEBUG_FUNCPTR (gst_nv_encoder_set_context);

  videoenc_class->open = GST_DEBUG_FUNCPTR (gst_nv_encoder_open);
  videoenc_class->close = GST_DEBUG_FUNCPTR (gst_nv_encoder_close);
  videoenc_class->stop = GST_DEBUG_FUNCPTR (gst_nv_encoder_stop);
  videoenc_class->sink_query = GST_DEBUG_FUNCPTR (gst_nv_encoder_sink_query);
  videoenc_class->src_query = GST_DEBUG_FUNCPTR (gst_nv_encoder_src_query);
  videoenc_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_nv_encoder_propose_allocation);
  videoenc_class->set_format = GST_DEBUG_FUNCPTR (gst_nv_encoder_set_format);
  videoenc_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_nv_encoder_handle_frame);
  videoenc_class->finish = GST_DEBUG_FUNCPTR (gst_nv_encoder_finish);
  videoenc_class->flush = GST_DEBUG_FUNCPTR (gst_nv_encoder_flush);

  GST_DEBUG_CATEGORY_INIT (gst_nv_encoder_debug, "nvencoder", 0, "nvencoder");
}

static void
gst_nv_encoder_init (GstNvEncoder * self)
{
  GstNvEncoderPrivate *priv;

  self->priv = priv = (GstNvEncoderPrivate *)
      gst_nv_encoder_get_instance_private (self);

  priv->task_pool = g_array_new (FALSE, TRUE, sizeof (GstNvEncoderTask));
  g_array_set_clear_func (priv->task_pool,
      (GDestroyNotify) gst_nv_encoder_task_clear);

  g_queue_init (&priv->free_tasks);
  g_queue_init (&priv->output_tasks);

  g_mutex_init (&priv->lock);
  g_cond_init (&priv->cond);

  gst_video_encoder_set_min_pts (GST_VIDEO_ENCODER (self),
      GST_SECOND * 60 * 60 * 1000);
}

static void
gst_nv_encoder_finalize (GObject * object)
{
  GstNvEncoder *self = GST_NV_ENCODER (object);
  GstNvEncoderPrivate *priv = self->priv;

  g_array_unref (priv->task_pool);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_nv_encoder_set_context (GstElement * element, GstContext * context)
{
  GstNvEncoder *self = GST_NV_ENCODER (element);
  GstNvEncoderPrivate *priv = self->priv;

#ifdef HAVE_NVCODEC_GST_D3D11
  if (priv->d3d11_mode) {
    gst_d3d11_handle_set_context_for_adapter_luid (element,
        context, priv->dxgi_adapter_luid, &priv->device);
  }
#endif

  gst_cuda_handle_set_context (element, context, priv->cuda_device_id,
      &priv->context);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_nv_encoder_reset (GstNvEncoder * self)
{
  GstNvEncoderPrivate *priv = self->priv;

  GST_LOG_OBJECT (self, "Reset");

  g_array_set_size (priv->task_pool, 0);

  if (priv->internal_pool) {
    gst_buffer_pool_set_active (priv->internal_pool, FALSE);
    gst_clear_object (&priv->internal_pool);
  }

  if (priv->session) {
    NvEncDestroyEncoder (priv->session);
    priv->session = NULL;
  }

  g_queue_clear (&priv->free_tasks);
  g_queue_clear (&priv->output_tasks);

  priv->last_flow = GST_FLOW_OK;

  return TRUE;
}

static gboolean
gst_nv_encoder_device_lock (GstNvEncoder * self)
{
  GstNvEncoderPrivate *priv = self->priv;

#ifdef HAVE_NVCODEC_GST_D3D11
  if (priv->d3d11_mode) {
    gst_d3d11_device_lock (priv->device);
    return TRUE;
  }
#endif

  return gst_cuda_context_push (priv->context);
}

static gboolean
gst_nv_encoder_device_unlock (GstNvEncoder * self)
{
#ifdef HAVE_NVCODEC_GST_D3D11
  GstNvEncoderPrivate *priv = self->priv;

  if (priv->d3d11_mode) {
    gst_d3d11_device_unlock (priv->device);
    return TRUE;
  }
#endif

  return gst_cuda_context_pop (NULL);
}

static GstFlowReturn
gst_nv_encoder_get_free_task (GstNvEncoder * self, GstNvEncoderTask ** task,
    gboolean check_last_flow)
{
  GstNvEncoderPrivate *priv = self->priv;
  GstFlowReturn ret = GST_FLOW_OK;
  GstNvEncoderTask *free_task = NULL;

  GST_NV_ENCODER_LOCK (self);
  if (check_last_flow) {
    if (priv->last_flow != GST_FLOW_OK) {
      ret = priv->last_flow;
      GST_NV_ENCODER_UNLOCK (self);
      return ret;
    }

    while (priv->last_flow == GST_FLOW_OK && (free_task = (GstNvEncoderTask *)
            g_queue_pop_head (&priv->free_tasks)) == NULL) {
      g_cond_wait (&priv->cond, &priv->lock);
    }

    ret = priv->last_flow;
    if (ret != GST_FLOW_OK && free_task) {
      g_queue_push_tail (&priv->free_tasks, free_task);
      free_task = NULL;
    }
  } else {
    while ((free_task = (GstNvEncoderTask *)
            g_queue_pop_head (&priv->free_tasks)) == NULL)
      g_cond_wait (&priv->cond, &priv->lock);
  }
  GST_NV_ENCODER_UNLOCK (self);

  *task = free_task;

  return ret;
}

static gboolean
gst_nv_encoder_drain (GstNvEncoder * self, gboolean locked)
{
  GstNvEncoderPrivate *priv = self->priv;
  NV_ENC_PIC_PARAMS pic_params = { 0, };
  NVENCSTATUS status;
  GstNvEncoderTask *task;

  if (!priv->session || !priv->encoding_thread)
    return TRUE;

  GST_DEBUG_OBJECT (self, "Drain");

  if (locked)
    GST_VIDEO_ENCODER_STREAM_UNLOCK (self);

  gst_nv_encoder_get_free_task (self, &task, FALSE);

  task->is_eos = TRUE;

  pic_params.version = gst_nvenc_get_pic_params_version ();
  pic_params.encodePicFlags = NV_ENC_PIC_FLAG_EOS;
  pic_params.completionEvent = task->event_handle;

  gst_nv_encoder_device_lock (self);
  status = NvEncEncodePicture (priv->session, &pic_params);
  if (status != NV_ENC_SUCCESS) {
    GST_DEBUG_OBJECT (self, "Drain returned status %" GST_NVENC_STATUS_FORMAT,
        GST_NVENC_STATUS_ARGS (status));
#ifdef G_OS_WIN32
    if (task->event_handle) {
      SetEvent (task->event_handle);
    }
#endif
  }
  gst_nv_encoder_device_unlock (self);

  GST_NV_ENCODER_LOCK (self);
  g_queue_push_tail (&priv->output_tasks, task);
  g_cond_broadcast (&priv->cond);
  GST_NV_ENCODER_UNLOCK (self);

  g_clear_pointer (&priv->encoding_thread, g_thread_join);
  gst_nv_encoder_reset (self);

  if (locked)
    GST_VIDEO_ENCODER_STREAM_LOCK (self);

  return TRUE;
}

#ifdef HAVE_NVCODEC_GST_D3D11
static gboolean
gst_nv_encoder_open_d3d11_device (GstNvEncoder * self)
{
  GstNvEncoderPrivate *priv = self->priv;
  ComPtr < ID3D10Multithread > multi_thread;
  ID3D11Device *device_handle;
  HRESULT hr;

  if (!gst_d3d11_ensure_element_data_for_adapter_luid (GST_ELEMENT (self),
          priv->dxgi_adapter_luid, &priv->device)) {
    GST_ERROR_OBJECT (self, "Cannot create d3d11device");
    return FALSE;
  }

  device_handle = gst_d3d11_device_get_device_handle (priv->device);
  hr = device_handle->QueryInterface (IID_PPV_ARGS (&multi_thread));
  if (!gst_d3d11_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "ID3D10Multithread interface is unavailable");
    gst_clear_object (&priv->device);

    return FALSE;
  }

  multi_thread->SetMultithreadProtected (TRUE);

  return TRUE;
}
#endif

static gboolean
gst_nv_encoder_open (GstVideoEncoder * encoder)
{
  GstNvEncoder *self = GST_NV_ENCODER (encoder);
  GstNvEncoderPrivate *priv = self->priv;

#ifdef HAVE_NVCODEC_GST_D3D11
  if (priv->d3d11_mode) {
    return gst_nv_encoder_open_d3d11_device (self);
  }
#endif

  if (!gst_cuda_ensure_element_context (GST_ELEMENT_CAST (encoder),
          priv->cuda_device_id, &priv->context)) {
    GST_ERROR_OBJECT (self, "failed to create CUDA context");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_nv_encoder_close (GstVideoEncoder * encoder)
{
  GstNvEncoder *self = GST_NV_ENCODER (encoder);
  GstNvEncoderPrivate *priv = self->priv;

  gst_clear_object (&priv->context);
#ifdef HAVE_NVCODEC_GST_D3D11
  gst_clear_object (&priv->device);
#endif

  return TRUE;
}

static gboolean
gst_nv_encoder_stop (GstVideoEncoder * encoder)
{
  GstNvEncoder *self = GST_NV_ENCODER (encoder);
  GstNvEncoderPrivate *priv = self->priv;

  GST_DEBUG_OBJECT (self, "Stop");

  gst_nv_encoder_drain (self, FALSE);

  g_clear_pointer (&priv->input_state, gst_video_codec_state_unref);

  return TRUE;
}

static gboolean
gst_nv_encoder_handle_context_query (GstNvEncoder * self, GstQuery * query)
{
  GstNvEncoderPrivate *priv = self->priv;

#ifdef HAVE_NVCODEC_GST_D3D11
  if (priv->d3d11_mode) {
    return gst_d3d11_handle_context_query (GST_ELEMENT (self),
        query, priv->device);
  }
#endif

  return gst_cuda_handle_context_query (GST_ELEMENT (self),
      query, priv->context);
}

static gboolean
gst_nv_encoder_sink_query (GstVideoEncoder * encoder, GstQuery * query)
{
  GstNvEncoder *self = GST_NV_ENCODER (encoder);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      if (gst_nv_encoder_handle_context_query (self, query))
        return TRUE;
      break;
    default:
      break;
  }

  return GST_VIDEO_ENCODER_CLASS (parent_class)->sink_query (encoder, query);
}

static gboolean
gst_nv_encoder_src_query (GstVideoEncoder * encoder, GstQuery * query)
{
  GstNvEncoder *self = GST_NV_ENCODER (encoder);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      if (gst_nv_encoder_handle_context_query (self, query))
        return TRUE;
      break;
    default:
      break;
  }

  return GST_VIDEO_ENCODER_CLASS (parent_class)->src_query (encoder, query);
}

static gboolean
gst_nv_encoder_propose_allocation (GstVideoEncoder * encoder, GstQuery * query)
{
  GstNvEncoder *self = GST_NV_ENCODER (encoder);
  GstNvEncoderPrivate *priv = self->priv;
  GstVideoInfo info;
  GstBufferPool *pool = NULL;
  GstCaps *caps;
  guint size;
  GstStructure *config;
  GstCapsFeatures *features;
  guint min_buffers;

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps) {
    GST_WARNING_OBJECT (self, "null caps in query");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_WARNING_OBJECT (self, "Failed to convert caps into info");
    return FALSE;
  }

  features = gst_caps_get_features (caps, 0);
#ifdef HAVE_NVCODEC_GST_D3D11
  if (priv->d3d11_mode && features && gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY)) {
    GST_DEBUG_OBJECT (self, "upstream support d3d11 memory");
    pool = gst_d3d11_buffer_pool_new (priv->device);
  }
#endif

  if (!priv->d3d11_mode && features && gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY)) {
    GST_DEBUG_OBJECT (self, "upstream support CUDA memory");
    pool = gst_cuda_buffer_pool_new (priv->context);
  }

  if (!pool)
    pool = gst_video_buffer_pool_new ();

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  min_buffers = gst_nv_encoder_get_task_size (GST_NV_ENCODER (self));

  size = GST_VIDEO_INFO_SIZE (&info);
  gst_buffer_pool_config_set_params (config, caps, size, min_buffers, 0);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_WARNING_OBJECT (self, "Failed to set pool config");
    gst_object_unref (pool);
    return FALSE;
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_get_params (config, NULL, &size, NULL, NULL);
  gst_structure_free (config);

  gst_query_add_allocation_pool (query, pool, size, min_buffers, 0);
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
  gst_object_unref (pool);

  return TRUE;
}

/* called with lock */
static void
gst_nv_encoder_task_reset (GstNvEncoder * self, GstNvEncoderTask * task)
{
  GstNvEncoderPrivate *priv = self->priv;

  if (!task)
    return;

  if (task->buffer) {
    gst_nv_encoder_device_lock (self);
    if (priv->session) {
      NvEncUnmapInputResource (priv->session,
          task->mapped_resource.mappedResource);
      NvEncUnregisterResource (priv->session,
          task->register_resource.registeredResource);
    }
    gst_nv_encoder_device_unlock (self);

    gst_buffer_unmap (task->buffer, &task->map_info);
    gst_clear_buffer (&task->buffer);
  }
#ifdef G_OS_WIN32
  if (task->event_handle)
    ResetEvent (task->event_handle);
#endif

  task->is_eos = FALSE;

  g_queue_push_head (&priv->free_tasks, task);
}

static gboolean
gst_nv_encoder_create_event_handle (GstNvEncoder * self, gpointer session,
    gpointer * event_handle)
{
#ifdef G_OS_WIN32
  NV_ENC_EVENT_PARAMS event_params = { 0, };
  NVENCSTATUS status;

  event_params.version = gst_nvenc_get_event_params_version ();
  event_params.completionEvent = CreateEvent (NULL, FALSE, FALSE, NULL);
  status = NvEncRegisterAsyncEvent (session, &event_params);

  if (status != NV_ENC_SUCCESS) {
    GST_ERROR_OBJECT (self,
        "Failed to register async event handle, status %"
        GST_NVENC_STATUS_FORMAT, GST_NVENC_STATUS_ARGS (status));
    CloseHandle (event_params.completionEvent);
    return FALSE;
  }

  *event_handle = event_params.completionEvent;
#endif

  return TRUE;
}

static gboolean
gst_d3d11_encoder_wait_for_event_handle (GstNvEncoder * self,
    gpointer event_handle)
{
#ifdef G_OS_WIN32
  /* NVCODEC SDK uses 20s */
  if (WaitForSingleObject (event_handle, 20000) == WAIT_FAILED) {
    GST_ERROR_OBJECT (self, "Failed to wait for completion event");
    return FALSE;
  }
#endif

  return TRUE;
}

static void
gst_nv_encoder_destroy_event_handle (GstNvEncoder * self, gpointer session,
    gpointer event_handle)
{
#ifdef G_OS_WIN32
  NV_ENC_EVENT_PARAMS event_params = { 0, };
  NVENCSTATUS status;

  event_params.version = gst_nvenc_get_event_params_version ();
  event_params.completionEvent = event_handle;
  status = NvEncUnregisterAsyncEvent (session, &event_params);
  CloseHandle (event_handle);

  if (status != NV_ENC_SUCCESS) {
    GST_ERROR_OBJECT (self,
        "Failed to unregister async event handle, status %"
        GST_NVENC_STATUS_FORMAT, GST_NVENC_STATUS_ARGS (status));
  }
#endif
}

static void
gst_nv_encoder_task_clear (GstNvEncoderTask * task)
{
  GstNvEncoder *self;
  GstNvEncoderPrivate *priv;

  if (!task)
    return;

  self = task->encoder;
  priv = self->priv;

  if (priv->session) {
    gst_nv_encoder_device_lock (self);
    if (task->buffer) {
      NvEncUnmapInputResource (priv->session,
          task->mapped_resource.mappedResource);
      NvEncUnregisterResource (priv->session,
          task->register_resource.registeredResource);
    }
    if (task->output_ptr)
      NvEncDestroyBitstreamBuffer (priv->session, task->output_ptr);
    if (task->input_buffer.inputBuffer)
      NvEncDestroyInputBuffer (priv->session, task->input_buffer.inputBuffer);
    if (task->event_handle) {
      gst_nv_encoder_destroy_event_handle (self, priv->session,
          task->event_handle);
    }

    gst_nv_encoder_device_unlock (self);
  }

  if (task->buffer) {
    gst_buffer_unmap (task->buffer, &task->map_info);
    gst_clear_buffer (&task->buffer);
  }

  memset (task, 0, sizeof (GstNvEncoderTask));
}

static NV_ENC_PIC_STRUCT
gst_nv_encoder_get_pic_struct (GstNvEncoder * self, GstBuffer * buffer)
{
  GstNvEncoderPrivate *priv = self->priv;
  GstVideoInfo *info = &priv->input_state->info;

  if (!GST_VIDEO_INFO_IS_INTERLACED (info))
    return NV_ENC_PIC_STRUCT_FRAME;

  if (GST_VIDEO_INFO_INTERLACE_MODE (info) == GST_VIDEO_INTERLACE_MODE_MIXED) {
    if (!GST_BUFFER_FLAG_IS_SET (buffer, GST_VIDEO_BUFFER_FLAG_INTERLACED)) {
      return NV_ENC_PIC_STRUCT_FRAME;
    }

    if (GST_BUFFER_FLAG_IS_SET (buffer, GST_VIDEO_BUFFER_FLAG_TFF))
      return NV_ENC_PIC_STRUCT_FIELD_TOP_BOTTOM;

    return NV_ENC_PIC_STRUCT_FIELD_BOTTOM_TOP;
  }

  switch (GST_VIDEO_INFO_FIELD_ORDER (info)) {
    case GST_VIDEO_FIELD_ORDER_TOP_FIELD_FIRST:
      return NV_ENC_PIC_STRUCT_FIELD_TOP_BOTTOM;
      break;
    case GST_VIDEO_FIELD_ORDER_BOTTOM_FIELD_FIRST:
      return NV_ENC_PIC_STRUCT_FIELD_BOTTOM_TOP;
      break;
    default:
      break;
  }

  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_VIDEO_BUFFER_FLAG_TFF))
    return NV_ENC_PIC_STRUCT_FIELD_TOP_BOTTOM;

  return NV_ENC_PIC_STRUCT_FIELD_BOTTOM_TOP;
}

static GstFlowReturn
gst_nv_encoder_encode_frame (GstNvEncoder * self,
    GstVideoCodecFrame * frame, GstNvEncoderTask * task)
{
  GstNvEncoderPrivate *priv = self->priv;
  NV_ENC_PIC_PARAMS pic_params = { 0, };
  NVENCSTATUS status;
  guint retry_count = 0;
  const guint retry_threshold = 100;

  pic_params.version = gst_nvenc_get_pic_params_version ();
  if (task->buffer) {
    pic_params.inputWidth = task->register_resource.width;
    pic_params.inputHeight = task->register_resource.height;
    pic_params.inputPitch = task->register_resource.pitch;
    pic_params.inputBuffer = task->mapped_resource.mappedResource;
    pic_params.bufferFmt = task->mapped_resource.mappedBufferFmt;
  } else {
    pic_params.inputWidth = task->input_buffer.width;
    pic_params.inputHeight = task->input_buffer.height;
    pic_params.inputPitch = task->lk_input_buffer.pitch;
    pic_params.inputBuffer = task->input_buffer.inputBuffer;
    pic_params.bufferFmt = task->input_buffer.bufferFmt;
  }

  pic_params.frameIdx = frame->system_frame_number;
  pic_params.inputTimeStamp = frame->pts;
  pic_params.inputDuration = frame->duration;
  pic_params.outputBitstream = task->output_ptr;
  pic_params.completionEvent = task->event_handle;
  pic_params.pictureStruct = gst_nv_encoder_get_pic_struct (self, task->buffer);
  if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (frame))
    pic_params.encodePicFlags = NV_ENC_PIC_FLAG_FORCEIDR;

  do {
    gst_nv_encoder_device_lock (self);
    status = NvEncEncodePicture (priv->session, &pic_params);
    gst_nv_encoder_device_unlock (self);

    if (status == NV_ENC_ERR_ENCODER_BUSY) {
      if (retry_count < 100) {
        GST_DEBUG_OBJECT (self, "GPU is busy, retry count (%d/%d)", retry_count,
            retry_threshold);
        retry_count++;

        /* Magic number 1ms */
        g_usleep (1000);
        continue;
      } else {
        GST_ERROR_OBJECT (self, "GPU is keep busy, give up");
        break;
      }
    }

    break;
  } while (TRUE);

  GST_NV_ENCODER_LOCK (self);
  if (status != NV_ENC_SUCCESS && status != NV_ENC_ERR_NEED_MORE_INPUT) {
    GST_ERROR_OBJECT (self, "Encode return %" GST_NVENC_STATUS_FORMAT,
        GST_NVENC_STATUS_ARGS (status));
    gst_nv_encoder_task_reset (self, task);
    GST_NV_ENCODER_UNLOCK (self);

    return GST_FLOW_ERROR;
  }

  gst_video_codec_frame_set_user_data (frame, task, NULL);
  g_queue_push_tail (&priv->output_tasks, task);
  g_cond_broadcast (&priv->cond);
  GST_NV_ENCODER_UNLOCK (self);

  return GST_FLOW_OK;
}

static GstVideoCodecFrame *
gst_nv_encoder_find_output_frame (GstVideoEncoder * self,
    GstNvEncoderTask * task)
{
  GList *frames, *iter;
  GstVideoCodecFrame *ret = NULL;

  frames = gst_video_encoder_get_frames (self);

  for (iter = frames; iter; iter = g_list_next (iter)) {
    GstVideoCodecFrame *frame = (GstVideoCodecFrame *) iter->data;
    GstNvEncoderTask *other = (GstNvEncoderTask *)
        gst_video_codec_frame_get_user_data (frame);

    if (!other)
      continue;

    if (other == task) {
      ret = frame;
      break;
    }
  }

  if (ret)
    gst_video_codec_frame_ref (ret);

  if (frames)
    g_list_free_full (frames, (GDestroyNotify) gst_video_codec_frame_unref);

  return ret;
}

static gpointer
gst_nv_encoder_thread_func (GstNvEncoder * self)
{
  GstVideoEncoder *encoder = GST_VIDEO_ENCODER (self);
  GstNvEncoderClass *klass = GST_NV_ENCODER_GET_CLASS (self);
  GstNvEncoderPrivate *priv = self->priv;
  GstNvEncoderTask *task = NULL;

  do {
    NV_ENC_LOCK_BITSTREAM bitstream = { 0, };
    NVENCSTATUS status;
    GstVideoCodecFrame *frame;
    GstFlowReturn ret;

    GST_NV_ENCODER_LOCK (self);
    while ((task = (GstNvEncoderTask *)
            g_queue_pop_head (&priv->output_tasks)) == NULL) {
      g_cond_wait (&priv->cond, &priv->lock);
    }
    GST_NV_ENCODER_UNLOCK (self);

    if (task->event_handle) {
      if (!gst_d3d11_encoder_wait_for_event_handle (self, task->event_handle)) {
        GST_ELEMENT_ERROR (self, STREAM, ENCODE, (NULL),
            ("Failed to wait for event signal"));
        goto error;
      }
    }

    if (task->is_eos) {
      GST_INFO_OBJECT (self, "Got EOS packet");

      GST_NV_ENCODER_LOCK (self);
      gst_nv_encoder_task_reset (self, task);
      g_cond_broadcast (&priv->cond);
      GST_NV_ENCODER_UNLOCK (self);

      goto exit_thread;
    }

    frame = gst_nv_encoder_find_output_frame (encoder, task);
    if (!frame) {
      GST_ELEMENT_ERROR (self, STREAM, ENCODE, (NULL),
          ("Failed to find associated codec frame"));
      goto error;
    }

    if (!gst_nv_encoder_device_lock (self)) {
      GST_ELEMENT_ERROR (self, STREAM, ENCODE, (NULL),
          ("Failed to lock device"));
      goto error;
    }

    bitstream.version = gst_nvenc_get_lock_bitstream_version ();
    bitstream.outputBitstream = task->output_ptr;

    status = NvEncLockBitstream (priv->session, &bitstream);
    if (status != NV_ENC_SUCCESS) {
      gst_nv_encoder_device_unlock (self);
      GST_ELEMENT_ERROR (self, STREAM, ENCODE, (NULL),
          ("Failed to lock bitstream, status: %" GST_NVENC_STATUS_FORMAT,
              GST_NVENC_STATUS_ARGS (status)));
      goto error;
    }

    if (klass->create_output_buffer) {
      frame->output_buffer = klass->create_output_buffer (self, &bitstream);
    } else {
      frame->output_buffer =
          gst_buffer_new_memdup (bitstream.bitstreamBufferPtr,
          bitstream.bitstreamSizeInBytes);
    }

    GST_BUFFER_FLAG_SET (frame->output_buffer, GST_BUFFER_FLAG_MARKER);

    if (bitstream.pictureType == NV_ENC_PIC_TYPE_IDR)
      GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);

    NvEncUnlockBitstream (priv->session, task->output_ptr);
    gst_nv_encoder_device_unlock (self);

    frame->dts = frame->pts - priv->dts_offset;
    frame->pts = bitstream.outputTimeStamp;
    frame->duration = bitstream.outputDuration;

    ret = gst_video_encoder_finish_frame (encoder, frame);
    if (ret != GST_FLOW_OK) {
      GST_INFO_OBJECT (self,
          "Finish frame returned %s", gst_flow_get_name (ret));
    }

    GST_NV_ENCODER_LOCK (self);
    gst_nv_encoder_task_reset (self, task);
    priv->last_flow = ret;
    g_cond_broadcast (&priv->cond);
    GST_NV_ENCODER_UNLOCK (self);

    if (ret != GST_FLOW_OK) {
      GST_INFO_OBJECT (self, "Push returned %s", gst_flow_get_name (ret));
      goto exit_thread;
    }
  } while (TRUE);

exit_thread:
  {
    GST_INFO_OBJECT (self, "Exiting thread");

    return NULL;

  }
error:
  {
    GST_NV_ENCODER_LOCK (self);
    gst_nv_encoder_task_reset (self, task);
    priv->last_flow = GST_FLOW_ERROR;
    g_cond_broadcast (&priv->cond);
    GST_NV_ENCODER_UNLOCK (self);

    goto exit_thread;
  }
}

static guint
gst_nv_encoder_calculate_task_pool_size (GstNvEncoder * self,
    NV_ENC_CONFIG * config)
{
  guint num_tasks;

  /* At least 4 surfaces are required as documented by Nvidia Encoder guide */
  num_tasks = 4;

  /* lookahead depth */
  num_tasks += config->rcParams.lookaheadDepth;

  /* B frames + 1 */
  num_tasks += MAX (0, config->frameIntervalP - 1) + 1;

  GST_DEBUG_OBJECT (self, "Calculated task pool size: %d "
      "(lookahead %d, frameIntervalP %d)",
      num_tasks, config->rcParams.lookaheadDepth, config->frameIntervalP);

  return num_tasks;
}

static gboolean
gst_nv_encoder_open_encode_session (GstNvEncoder * self, gpointer * session)
{
  GstNvEncoderPrivate *priv = self->priv;
  NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS session_params = { 0, };
  session_params.version =
      gst_nvenc_get_open_encode_session_ex_params_version ();
  session_params.apiVersion = gst_nvenc_get_api_version ();
  NVENCSTATUS status;

#ifdef HAVE_NVCODEC_GST_D3D11
  if (priv->d3d11_mode) {
    session_params.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX;
    session_params.device = gst_d3d11_device_get_device_handle (priv->device);
  } else
#endif
  {
    session_params.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
    session_params.device = gst_cuda_context_get_handle (priv->context);
  }

  status = NvEncOpenEncodeSessionEx (&session_params, session);
  if (status != NV_ENC_SUCCESS) {
    GST_ERROR_OBJECT (self, "Failed to open session, status: %"
        GST_NVENC_STATUS_FORMAT, GST_NVENC_STATUS_ARGS (status));
    return FALSE;
  }

  return TRUE;
}

#ifdef HAVE_NVCODEC_GST_D3D11
static GstBufferPool *
gst_nv_encoder_create_d3d11_pool (GstNvEncoder * self,
    GstVideoCodecState * state)
{
  GstNvEncoderPrivate *priv = self->priv;
  GstStructure *config;
  GstBufferPool *pool = NULL;
  GstD3D11AllocationParams *params;

  params = gst_d3d11_allocation_params_new (priv->device, &state->info,
      (GstD3D11AllocationFlags) 0, 0);
  params->desc[0].MiscFlags = D3D11_RESOURCE_MISC_SHARED;

  pool = gst_d3d11_buffer_pool_new (priv->device);

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_d3d11_allocation_params (config, params);
  gst_d3d11_allocation_params_free (params);

  gst_buffer_pool_config_set_params (config, state->caps,
      GST_VIDEO_INFO_SIZE (&state->info), 0, 0);
  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (self, "Failed to set pool config");
    gst_object_unref (pool);

    return NULL;
  }

  if (!gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (self, "Failed to set active");
    gst_object_unref (pool);
    return NULL;
  }

  return pool;
}
#endif

static GstBufferPool *
gst_nv_encoder_create_pool (GstNvEncoder * self, GstVideoCodecState * state)
{
  GstNvEncoderPrivate *priv = self->priv;
  GstStructure *config;
  GstBufferPool *pool = NULL;
#ifdef HAVE_NVCODEC_GST_D3D11
  if (priv->d3d11_mode)
    return gst_nv_encoder_create_d3d11_pool (self, state);
#endif

  pool = gst_cuda_buffer_pool_new (priv->context);

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, state->caps,
      GST_VIDEO_INFO_SIZE (&state->info), 0, 0);
  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (self, "Failed to set pool config");
    gst_object_unref (pool);

    return NULL;
  }

  if (!gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (self, "Failed to set active");
    gst_object_unref (pool);
    return NULL;
  }

  return pool;
}

static gboolean
gst_nv_encoder_init_session (GstNvEncoder * self)
{
  GstNvEncoderPrivate *priv = self->priv;
  GstNvEncoderClass *klass = GST_NV_ENCODER_GET_CLASS (self);
  GstVideoCodecState *state = priv->input_state;
  GstVideoInfo *info = &state->info;
  NVENCSTATUS status;
  guint task_pool_size;
  gint fps_n, fps_d;
  GstClockTime frame_duration, min_latency, max_latency;
  guint i;

  gst_nv_encoder_reset (self);

  memset (&priv->init_params, 0, sizeof (NV_ENC_INITIALIZE_PARAMS));
  memset (&priv->config, 0, sizeof (NV_ENC_CONFIG));

  priv->internal_pool = gst_nv_encoder_create_pool (self, state);
  if (!priv->internal_pool) {
    GST_ELEMENT_ERROR (self, STREAM, ENCODE, (NULL),
        ("Failed to create internal pool"));
    return FALSE;
  }

  if (!gst_nv_encoder_device_lock (self)) {
    GST_ELEMENT_ERROR (self, STREAM, ENCODE, (NULL), ("Failed to lock device"));
    gst_nv_encoder_reset (self);
    return FALSE;
  }

  if (!gst_nv_encoder_open_encode_session (self, &priv->session)) {
    GST_ELEMENT_ERROR (self, STREAM, ENCODE, (NULL),
        ("Failed to open session"));
    goto error;
  }

  if (!klass->set_format (self, state, priv->session, &priv->init_params,
          &priv->config)) {
    GST_ELEMENT_ERROR (self, STREAM, ENCODE, (NULL), ("Failed to set format"));
    goto error;
  }

  priv->init_params.encodeConfig = &priv->config;
  status = NvEncInitializeEncoder (priv->session, &priv->init_params);
  if (status != NV_ENC_SUCCESS) {
    GST_ELEMENT_ERROR (self, STREAM, ENCODE, (NULL),
        ("Failed to init encoder, status: %"
            GST_NVENC_STATUS_FORMAT, GST_NVENC_STATUS_ARGS (status)));
    goto error;
  }

  task_pool_size = gst_nv_encoder_calculate_task_pool_size (self,
      &priv->config);
  g_array_set_size (priv->task_pool, task_pool_size);

  for (i = 0; i < task_pool_size; i++) {
    NV_ENC_CREATE_BITSTREAM_BUFFER buffer_params = { 0, };
    GstNvEncoderTask *task = (GstNvEncoderTask *)
        & g_array_index (priv->task_pool, GstNvEncoderTask, i);

    task->encoder = self;

    buffer_params.version = gst_nvenc_get_create_bitstream_buffer_version ();
    status = NvEncCreateBitstreamBuffer (priv->session, &buffer_params);

    if (status != NV_ENC_SUCCESS) {
      GST_ELEMENT_ERROR (self, STREAM, ENCODE, (NULL),
          ("Failed to create bitstream buffer, status: %"
              GST_NVENC_STATUS_FORMAT, GST_NVENC_STATUS_ARGS (status)));
      goto error;
    }

    task->output_ptr = buffer_params.bitstreamBuffer;

    if (priv->init_params.enableEncodeAsync) {
      if (!gst_nv_encoder_create_event_handle (self,
              priv->session, &task->event_handle)) {
        GST_ELEMENT_ERROR (self, STREAM, ENCODE, (NULL),
            ("Failed to create async event handle"));
        goto error;
      }
    }

    g_queue_push_tail (&priv->free_tasks, task);
  }
  gst_nv_encoder_device_unlock (self);

  if (!klass->set_output_state (self, priv->input_state, priv->session)) {
    GST_ELEMENT_ERROR (self, STREAM, ENCODE, (NULL),
        ("Failed to set output state"));
    gst_nv_encoder_reset (self);
    return FALSE;
  }

  priv->encoding_thread = g_thread_new ("GstNvEncoderThread",
      (GThreadFunc) gst_nv_encoder_thread_func, self);

  if (info->fps_n > 0 && info->fps_d > 0) {
    fps_n = info->fps_n;
    fps_d = info->fps_d;
  } else {
    fps_n = 25;
    fps_d = 1;
  }

  frame_duration = gst_util_uint64_scale (GST_SECOND, fps_d, fps_n);

  priv->dts_offset = 0;
  /* Calculate DTS offset for B frame. NVENC does not provide DTS */
  if (priv->config.frameIntervalP > 1)
    priv->dts_offset = frame_duration * (priv->config.frameIntervalP - 1);

  min_latency = priv->dts_offset +
      priv->config.rcParams.lookaheadDepth * frame_duration;
  max_latency = frame_duration * priv->task_pool->len;
  gst_video_encoder_set_latency (GST_VIDEO_ENCODER (self),
      min_latency, max_latency);

  return TRUE;

error:
  gst_nv_encoder_device_unlock (self);

  gst_nv_encoder_reset (self);

  return FALSE;
}

static gboolean
gst_nv_encoder_reconfigure_session (GstNvEncoder * self)
{
  GstNvEncoderPrivate *priv = self->priv;
  NV_ENC_RECONFIGURE_PARAMS params = { 0, };
  NVENCSTATUS status;

  if (!priv->session) {
    GST_WARNING_OBJECT (self,
        "Encoding session was not configured, open session");
    gst_nv_encoder_drain (self, TRUE);

    return gst_nv_encoder_init_session (self);
  }

  params.version = gst_nvenc_get_reconfigure_params_version ();
  params.reInitEncodeParams = priv->init_params;
  params.reInitEncodeParams.encodeConfig = &priv->config;

  status = NvEncReconfigureEncoder (priv->session, &params);
  if (status != NV_ENC_SUCCESS) {
    GST_WARNING_OBJECT (self, "Failed to reconfigure encoder, status %"
        GST_NVENC_STATUS_FORMAT, GST_NVENC_STATUS_ARGS (status));
    gst_nv_encoder_drain (self, TRUE);

    return gst_nv_encoder_init_session (self);
  }

  return TRUE;
}

static gboolean
gst_nv_encoder_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state)
{
  GstNvEncoder *self = GST_NV_ENCODER (encoder);
  GstNvEncoderPrivate *priv = self->priv;

  gst_nv_encoder_drain (self, TRUE);

  g_clear_pointer (&priv->input_state, gst_video_codec_state_unref);
  priv->input_state = gst_video_codec_state_ref (state);

  priv->last_flow = GST_FLOW_OK;

  return gst_nv_encoder_init_session (self);
}

static NV_ENC_BUFFER_FORMAT
gst_nv_encoder_get_buffer_format (GstNvEncoder * self, GstVideoFormat format)
{
  switch (format) {
    case GST_VIDEO_FORMAT_NV12:
      return NV_ENC_BUFFER_FORMAT_NV12;
    case GST_VIDEO_FORMAT_Y444:
      return NV_ENC_BUFFER_FORMAT_YUV444;
    case GST_VIDEO_FORMAT_P010_10LE:
      return NV_ENC_BUFFER_FORMAT_YUV420_10BIT;
    case GST_VIDEO_FORMAT_Y444_16LE:
      return NV_ENC_BUFFER_FORMAT_YUV444_10BIT;
    default:
      GST_ERROR_OBJECT (self, "Unexpected format %s",
          gst_video_format_to_string (format));
      g_assert_not_reached ();
      break;
  }

  return NV_ENC_BUFFER_FORMAT_UNDEFINED;
}

static GstFlowReturn
gst_nv_encoder_copy_system (GstNvEncoder * self, const GstVideoInfo * info,
    GstBuffer * buffer, gpointer session, GstNvEncoderTask * task)
{
  NVENCSTATUS status;
  GstVideoFrame frame;
  guint8 *dst_data;
  NV_ENC_BUFFER_FORMAT format;

  format =
      gst_nv_encoder_get_buffer_format (self, GST_VIDEO_INFO_FORMAT (info));
  if (format == NV_ENC_BUFFER_FORMAT_UNDEFINED)
    return GST_FLOW_ERROR;

  if (!gst_video_frame_map (&frame, info, buffer, GST_MAP_READ)) {
    GST_ERROR_OBJECT (self, "Failed to map buffer");
    return GST_FLOW_ERROR;
  }

  if (!task->input_buffer.inputBuffer) {
    NV_ENC_CREATE_INPUT_BUFFER input_buffer = { 0, };
    input_buffer.version = gst_nvenc_get_create_input_buffer_version ();
    input_buffer.width = info->width;
    input_buffer.height = info->height;
    input_buffer.bufferFmt = format;

    status = NvEncCreateInputBuffer (session, &input_buffer);
    if (status != NV_ENC_SUCCESS) {
      GST_ERROR_OBJECT (self, "Failed to create input buffer, status %"
          GST_NVENC_STATUS_FORMAT, GST_NVENC_STATUS_ARGS (status));
      gst_video_frame_unmap (&frame);
      return GST_FLOW_ERROR;
    }

    task->input_buffer = input_buffer;
  }

  task->lk_input_buffer.version = gst_nvenc_get_lock_input_buffer_version ();
  task->lk_input_buffer.inputBuffer = task->input_buffer.inputBuffer;
  status = NvEncLockInputBuffer (session, &task->lk_input_buffer);
  if (status != NV_ENC_SUCCESS) {
    GST_ERROR_OBJECT (self, "Failed to lock input buffer, status %"
        GST_NVENC_STATUS_FORMAT, GST_NVENC_STATUS_ARGS (status));
    gst_video_frame_unmap (&frame);
    return GST_FLOW_ERROR;
  }

  dst_data = (guint8 *) task->lk_input_buffer.bufferDataPtr;

  for (guint i = 0; i < GST_VIDEO_FRAME_N_PLANES (&frame); i++) {
    guint8 *src_data = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (&frame, i);
    guint width_in_bytes = GST_VIDEO_FRAME_COMP_WIDTH (&frame, i) *
        GST_VIDEO_FRAME_COMP_PSTRIDE (&frame, i);
    guint stride = GST_VIDEO_FRAME_PLANE_STRIDE (&frame, i);
    guint height = GST_VIDEO_FRAME_COMP_HEIGHT (&frame, i);

    for (guint j = 0; j < height; j++) {
      memcpy (dst_data, src_data, width_in_bytes);
      dst_data += task->lk_input_buffer.pitch;
      src_data += stride;
    }
  }

  NvEncUnlockInputBuffer (session, task->input_buffer.inputBuffer);
  gst_video_frame_unmap (&frame);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_nv_encoder_prepare_task_input_cuda (GstNvEncoder * self,
    const GstVideoInfo * info, GstBuffer * buffer, gpointer session,
    GstNvEncoderTask * task)
{
  GstNvEncoderPrivate *priv = self->priv;
  GstMemory *mem;
  GstCudaMemory *cmem;
  NVENCSTATUS status;

  mem = gst_buffer_peek_memory (buffer, 0);
  if (!gst_is_cuda_memory (mem)) {
    GST_LOG_OBJECT (self, "Not a CUDA buffer, system copy");
    return gst_nv_encoder_copy_system (self, info, buffer, session, task);
  }

  cmem = GST_CUDA_MEMORY_CAST (mem);
  if (cmem->context != priv->context) {
    GST_LOG_OBJECT (self, "Different context, system copy");
    return gst_nv_encoder_copy_system (self, info, buffer, session, task);
  }

  task->buffer = gst_buffer_ref (buffer);
  if (!gst_buffer_map (task->buffer, &task->map_info,
          (GstMapFlags) (GST_MAP_READ | GST_MAP_CUDA))) {
    GST_ERROR_OBJECT (self, "Failed to map buffer");
    gst_clear_buffer (&task->buffer);

    return GST_FLOW_ERROR;
  }

  cmem = (GstCudaMemory *) gst_buffer_peek_memory (task->buffer, 0);

  task->register_resource.version = gst_nvenc_get_register_resource_version ();
  task->register_resource.resourceType =
      NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR;
  task->register_resource.width = cmem->info.width;
  task->register_resource.height = cmem->info.height;
  task->register_resource.pitch = cmem->info.stride[0];
  task->register_resource.resourceToRegister = task->map_info.data;
  task->register_resource.bufferFormat =
      gst_nv_encoder_get_buffer_format (self, GST_VIDEO_INFO_FORMAT (info));
  if (task->register_resource.bufferFormat == NV_ENC_BUFFER_FORMAT_UNDEFINED)
    return GST_FLOW_ERROR;

  status = NvEncRegisterResource (session, &task->register_resource);
  if (status != NV_ENC_SUCCESS) {
    GST_ERROR_OBJECT (self, "Failed to register resource, status %"
        GST_NVENC_STATUS_FORMAT, GST_NVENC_STATUS_ARGS (status));

    gst_buffer_unmap (task->buffer, &task->map_info);
    gst_clear_buffer (&task->buffer);

    return GST_FLOW_ERROR;
  }

  task->mapped_resource.version = gst_nvenc_get_map_input_resource_version ();
  task->mapped_resource.registeredResource =
      task->register_resource.registeredResource;
  status = NvEncMapInputResource (session, &task->mapped_resource);
  if (status != NV_ENC_SUCCESS) {
    GST_ERROR_OBJECT (self, "Failed to map input resource, status %"
        GST_NVENC_STATUS_FORMAT, GST_NVENC_STATUS_ARGS (status));
    NvEncUnregisterResource (session,
        task->register_resource.registeredResource);

    gst_buffer_unmap (task->buffer, &task->map_info);
    gst_clear_buffer (&task->buffer);

    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

#ifdef HAVE_NVCODEC_GST_D3D11
static GstBuffer *
gst_nv_encoder_copy_d3d11 (GstNvEncoder * self,
    GstBuffer * src_buffer, GstBufferPool * pool, gboolean shared)
{
  GstNvEncoderPrivate *priv = self->priv;
  D3D11_TEXTURE2D_DESC src_desc, dst_desc;
  D3D11_BOX src_box;
  guint subresource_idx;
  GstMemory *src_mem, *dst_mem;
  GstMapInfo src_info, dst_info;
  ID3D11Texture2D *src_tex, *dst_tex;
  ID3D11Device *device_handle;
  ID3D11DeviceContext *device_context;
  GstBuffer *dst_buffer;
  GstFlowReturn ret;
  ComPtr < IDXGIResource > dxgi_resource;
  ComPtr < ID3D11Texture2D > shared_texture;
  ComPtr < ID3D11Query > query;
  D3D11_QUERY_DESC query_desc;
  BOOL sync_done = FALSE;
  HANDLE shared_handle;
  GstD3D11Device *device;
  HRESULT hr;

  ret = gst_buffer_pool_acquire_buffer (pool, &dst_buffer, NULL);
  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (self, "Failed to acquire buffer");
    return NULL;
  }

  src_mem = gst_buffer_peek_memory (src_buffer, 0);
  dst_mem = gst_buffer_peek_memory (dst_buffer, 0);

  device = GST_D3D11_MEMORY_CAST (src_mem)->device;

  device_handle = gst_d3d11_device_get_device_handle (device);
  device_context = gst_d3d11_device_get_device_context_handle (device);

  if (!gst_memory_map (src_mem, &src_info,
          (GstMapFlags) (GST_MAP_READ | GST_MAP_D3D11))) {
    GST_WARNING ("Failed to map src memory");
    gst_buffer_unref (dst_buffer);
    return NULL;
  }

  if (!gst_memory_map (dst_mem, &dst_info,
          (GstMapFlags) (GST_MAP_WRITE | GST_MAP_D3D11))) {
    GST_WARNING ("Failed to map dst memory");
    gst_memory_unmap (src_mem, &src_info);
    gst_buffer_unref (dst_buffer);
    return NULL;
  }

  src_tex = (ID3D11Texture2D *) src_info.data;
  dst_tex = (ID3D11Texture2D *) dst_info.data;

  gst_d3d11_memory_get_texture_desc (GST_D3D11_MEMORY_CAST (src_mem),
      &src_desc);
  gst_d3d11_memory_get_texture_desc (GST_D3D11_MEMORY_CAST (dst_mem),
      &dst_desc);
  subresource_idx =
      gst_d3d11_memory_get_subresource_index (GST_D3D11_MEMORY_CAST (src_mem));

  if (shared) {
    hr = dst_tex->QueryInterface (IID_PPV_ARGS (&dxgi_resource));
    if (!gst_d3d11_result (hr, priv->device)) {
      GST_ERROR_OBJECT (self,
          "IDXGIResource interface is not available, hr: 0x%x", (guint) hr);
      goto error;
    }

    hr = dxgi_resource->GetSharedHandle (&shared_handle);
    if (!gst_d3d11_result (hr, priv->device)) {
      GST_ERROR_OBJECT (self, "Failed to get shared handle, hr: 0x%x",
          (guint) hr);
      goto error;
    }

    hr = device_handle->OpenSharedResource (shared_handle,
        IID_PPV_ARGS (&shared_texture));

    if (!gst_d3d11_result (hr, device)) {
      GST_ERROR_OBJECT (self, "Failed to get shared texture, hr: 0x%x",
          (guint) hr);
      goto error;
    }

    dst_tex = shared_texture.Get ();
  }

  src_box.left = 0;
  src_box.top = 0;
  src_box.front = 0;
  src_box.back = 1;
  src_box.right = MIN (src_desc.Width, dst_desc.Width);
  src_box.bottom = MIN (src_desc.Height, dst_desc.Height);

  if (shared) {
    query_desc.Query = D3D11_QUERY_EVENT;
    query_desc.MiscFlags = 0;

    hr = device_handle->CreateQuery (&query_desc, &query);
    if (!gst_d3d11_result (hr, device)) {
      GST_ERROR_OBJECT (self, "Couldn't Create event query, hr: 0x%x",
          (guint) hr);
      goto error;
    }

    gst_d3d11_device_lock (device);
  }

  device_context->CopySubresourceRegion (dst_tex, 0,
      0, 0, 0, src_tex, subresource_idx, &src_box);

  if (shared) {
    device_context->End (query.Get ());
    do {
      hr = device_context->GetData (query.Get (), &sync_done, sizeof (BOOL), 0);
    } while (!sync_done && (hr == S_OK || hr == S_FALSE));

    if (!gst_d3d11_result (hr, device)) {
      GST_ERROR_OBJECT (self, "Couldn't sync GPU operation, hr: 0x%x",
          (guint) hr);
      gst_d3d11_device_unlock (device);
      goto error;
    }

    gst_d3d11_device_unlock (device);
  }

  gst_memory_unmap (dst_mem, &dst_info);
  gst_memory_unmap (src_mem, &src_info);

  return dst_buffer;

error:
  gst_memory_unmap (dst_mem, &dst_info);
  gst_memory_unmap (src_mem, &src_info);
  gst_buffer_unref (dst_buffer);

  return NULL;
}

static GstBuffer *
gst_nv_encoder_upload_d3d11_frame (GstNvEncoder * self,
    const GstVideoInfo * info, GstBuffer * buffer, GstBufferPool * pool)
{
  GstD3D11Memory *dmem;
  D3D11_TEXTURE2D_DESC desc;

  dmem = (GstD3D11Memory *) gst_buffer_peek_memory (buffer, 0);

  gst_d3d11_memory_get_texture_desc (dmem, &desc);
  if (desc.Usage != D3D11_USAGE_DEFAULT) {
    GST_TRACE_OBJECT (self, "Not a default usage texture, d3d11 copy");
    return gst_nv_encoder_copy_d3d11 (self, buffer, pool, FALSE);
  }

  GST_TRACE_OBJECT (self, "Use input buffer without copy");

  return gst_buffer_ref (buffer);
}

static GstFlowReturn
gst_nv_encoder_prepare_task_input_d3d11 (GstNvEncoder * self,
    const GstVideoInfo * info, GstBuffer * buffer, gpointer session,
    GstBufferPool * pool, GstNvEncoderTask * task)
{
  GstNvEncoderPrivate *priv = self->priv;
  GstMemory *mem;
  GstD3D11Memory *dmem;
  D3D11_TEXTURE2D_DESC desc;
  NVENCSTATUS status;

  if (gst_buffer_n_memory (buffer) > 1) {
    GST_LOG_OBJECT (self, "Not a native DXGI format, system copy");
    return gst_nv_encoder_copy_system (self, info, buffer, session, task);
  }

  mem = gst_buffer_peek_memory (buffer, 0);
  if (!gst_is_d3d11_memory (mem)) {
    GST_LOG_OBJECT (self, "Not a D3D11 buffer, system copy");
    return gst_nv_encoder_copy_system (self, info, buffer, session, task);
  }

  dmem = GST_D3D11_MEMORY_CAST (mem);
  if (dmem->device != priv->device) {
    gint64 adapter_luid;

    g_object_get (dmem->device, "adapter-luid", &adapter_luid, NULL);
    if (adapter_luid == priv->dxgi_adapter_luid) {
      GST_LOG_OBJECT (self, "Different device but same GPU, copy d3d11");
      task->buffer = gst_nv_encoder_copy_d3d11 (self, buffer, pool, TRUE);
    } else {
      GST_LOG_OBJECT (self, "Different device, system copy");
      return gst_nv_encoder_copy_system (self, info, buffer, session, task);
    }
  }

  if (!task->buffer)
    task->buffer = gst_nv_encoder_upload_d3d11_frame (self, info, buffer, pool);

  if (!task->buffer) {
    GST_ERROR_OBJECT (self, "Failed to upload buffer");
    return GST_FLOW_ERROR;
  }

  if (!gst_buffer_map (task->buffer, &task->map_info,
          (GstMapFlags) (GST_MAP_READ | GST_MAP_D3D11))) {
    GST_ERROR_OBJECT (self, "Failed to map buffer");
    gst_clear_buffer (&task->buffer);

    return GST_FLOW_ERROR;
  }

  dmem = (GstD3D11Memory *) gst_buffer_peek_memory (task->buffer, 0);
  gst_d3d11_memory_get_texture_desc (dmem, &desc);

  task->register_resource.version = gst_nvenc_get_register_resource_version ();
  task->register_resource.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
  task->register_resource.width = desc.Width;
  task->register_resource.height = desc.Height;
  switch (desc.Format) {
    case DXGI_FORMAT_NV12:
      task->register_resource.bufferFormat = NV_ENC_BUFFER_FORMAT_NV12;
      break;
    case DXGI_FORMAT_P010:
      task->register_resource.bufferFormat = NV_ENC_BUFFER_FORMAT_YUV420_10BIT;
      break;
    default:
      GST_ERROR_OBJECT (self, "Unexpected DXGI format %d", desc.Format);
      g_assert_not_reached ();
      return GST_FLOW_ERROR;
  }

  task->register_resource.subResourceIndex =
      gst_d3d11_memory_get_subresource_index (dmem);
  task->register_resource.resourceToRegister =
      gst_d3d11_memory_get_resource_handle (dmem);

  status = NvEncRegisterResource (session, &task->register_resource);
  if (status != NV_ENC_SUCCESS) {
    GST_ERROR_OBJECT (self, "Failed to register resource, status %"
        GST_NVENC_STATUS_FORMAT, GST_NVENC_STATUS_ARGS (status));

    gst_buffer_unmap (task->buffer, &task->map_info);
    gst_clear_buffer (&task->buffer);

    return GST_FLOW_ERROR;
  }

  task->mapped_resource.version = gst_nvenc_get_map_input_resource_version ();
  task->mapped_resource.registeredResource =
      task->register_resource.registeredResource;
  status = NvEncMapInputResource (session, &task->mapped_resource);
  if (status != NV_ENC_SUCCESS) {
    GST_ERROR_OBJECT (self, "Failed to map input resource, status %"
        GST_NVENC_STATUS_FORMAT, GST_NVENC_STATUS_ARGS (status));
    NvEncUnregisterResource (session,
        task->register_resource.registeredResource);

    gst_buffer_unmap (task->buffer, &task->map_info);
    gst_clear_buffer (&task->buffer);

    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}
#endif

static GstFlowReturn
gst_nv_encoder_prepare_task_input (GstNvEncoder * self,
    const GstVideoInfo * info, GstBuffer * buffer, gpointer session,
    GstBufferPool * pool, GstNvEncoderTask * task)
{
#ifdef HAVE_NVCODEC_GST_D3D11
  GstNvEncoderPrivate *priv = self->priv;
  if (priv->d3d11_mode) {
    return gst_nv_encoder_prepare_task_input_d3d11 (self, info, buffer,
        session, pool, task);
  }
#endif

  return gst_nv_encoder_prepare_task_input_cuda (self, info, buffer,
      session, task);
}

static GstFlowReturn
gst_nv_encoder_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstNvEncoder *self = GST_NV_ENCODER (encoder);
  GstNvEncoderPrivate *priv = self->priv;
  GstNvEncoderClass *klass = GST_NV_ENCODER_GET_CLASS (self);
  GstFlowReturn ret = GST_FLOW_ERROR;
  GstNvEncoderTask *task = NULL;
  GstNvEncoderReconfigure reconfig;

  GST_TRACE_OBJECT (self, "Handle frame");

  GST_NV_ENCODER_LOCK (self);
  ret = priv->last_flow;
  GST_NV_ENCODER_UNLOCK (self);

  if (ret != GST_FLOW_OK) {
    GST_INFO_OBJECT (self, "Last flow was %s", gst_flow_get_name (ret));
    gst_video_encoder_finish_frame (encoder, frame);

    return ret;
  }

  if (!priv->session && !gst_nv_encoder_init_session (self)) {
    GST_ERROR_OBJECT (self, "Encoder object was not configured");
    gst_video_encoder_finish_frame (encoder, frame);

    return GST_FLOW_NOT_NEGOTIATED;
  }

  reconfig = klass->check_reconfigure (self, &priv->config);
  switch (reconfig) {
    case GST_NV_ENCODER_RECONFIGURE_BITRATE:
      if (!gst_nv_encoder_reconfigure_session (self)) {
        gst_video_encoder_finish_frame (encoder, frame);
        return GST_FLOW_NOT_NEGOTIATED;
      }
      break;
    case GST_NV_ENCODER_RECONFIGURE_FULL:
    {
      gst_nv_encoder_drain (self, TRUE);
      if (!gst_nv_encoder_init_session (self)) {
        gst_video_encoder_finish_frame (encoder, frame);
        return GST_FLOW_NOT_NEGOTIATED;
      }
      break;
    }
    default:
      break;
  }

  /* Release stream lock temporarily for encoding thread to be able to
   * push encoded data */
  GST_VIDEO_ENCODER_STREAM_UNLOCK (self);
  ret = gst_nv_encoder_get_free_task (self, &task, TRUE);
  GST_VIDEO_ENCODER_STREAM_LOCK (self);
  if (ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (self, "Last flow was %s", gst_flow_get_name (ret));
    gst_video_encoder_finish_frame (encoder, frame);
    return ret;
  }

  if (!gst_nv_encoder_device_lock (self)) {
    GST_ERROR_OBJECT (self, "Failed to lock device");
    gst_video_encoder_finish_frame (encoder, frame);

    return GST_FLOW_ERROR;
  }

  g_assert (task->buffer == NULL);
  ret = gst_nv_encoder_prepare_task_input (self, &priv->input_state->info,
      frame->input_buffer, priv->session, priv->internal_pool, task);
  gst_nv_encoder_device_unlock (self);

  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (self, "Failed to upload frame");
    GST_NV_ENCODER_LOCK (self);
    gst_nv_encoder_task_reset (self, task);
    GST_NV_ENCODER_UNLOCK (self);

    gst_video_encoder_finish_frame (encoder, frame);

    return ret;
  }

  ret = gst_nv_encoder_encode_frame (self, frame, task);
  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (self, "Failed to encode frame");
    gst_video_encoder_finish_frame (encoder, frame);

    return ret;
  }

  gst_video_codec_frame_unref (frame);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_nv_encoder_finish (GstVideoEncoder * encoder)
{
  GstNvEncoder *self = GST_NV_ENCODER (encoder);

  GST_DEBUG_OBJECT (self, "Finish");

  gst_nv_encoder_drain (self, TRUE);

  return GST_FLOW_OK;
}

static gboolean
gst_nv_encoder_flush (GstVideoEncoder * encoder)
{
  GstNvEncoder *self = GST_NV_ENCODER (encoder);
  GstNvEncoderPrivate *priv = self->priv;

  GST_DEBUG_OBJECT (self, "Flush");

  gst_nv_encoder_drain (self, TRUE);

  priv->last_flow = GST_FLOW_OK;

  return TRUE;
}

guint
gst_nv_encoder_get_task_size (GstNvEncoder * encoder)
{
  g_return_val_if_fail (GST_IS_NV_ENCODER (encoder), 0);

  return encoder->priv->task_pool->len;
}

void
gst_nv_encoder_set_cuda_device_id (GstNvEncoder * encoder, guint device_id)
{
  g_return_if_fail (GST_IS_NV_ENCODER (encoder));

  encoder->priv->cuda_device_id = device_id;
  encoder->priv->d3d11_mode = FALSE;
}

void
gst_nv_encoder_set_dxgi_adapter_luid (GstNvEncoder * encoder,
    gint64 adapter_luid)
{
  g_return_if_fail (GST_IS_NV_ENCODER (encoder));

  encoder->priv->dxgi_adapter_luid = adapter_luid;
  encoder->priv->d3d11_mode = TRUE;
}

GType
gst_nv_encoder_preset_get_type (void)
{
  static GType preset_type = 0;
  static const GEnumValue presets[] = {
    {GST_NV_ENCODER_PRESET_DEFAULT, "Default", "default"},
    {GST_NV_ENCODER_PRESET_HP, "High Performance", "hp"},
    {GST_NV_ENCODER_PRESET_HQ, "High Quality", "hq"},
    {GST_NV_ENCODER_PRESET_LOW_LATENCY_DEFAULT, "Low Latency", "low-latency"},
    {GST_NV_ENCODER_PRESET_LOW_LATENCY_HQ, "Low Latency, High Quality",
        "low-latency-hq"},
    {GST_NV_ENCODER_PRESET_LOW_LATENCY_HP, "Low Latency, High Performance",
        "low-latency-hp"},
    {GST_NV_ENCODER_PRESET_LOSSLESS_DEFAULT, "Lossless", "lossless"},
    {GST_NV_ENCODER_PRESET_LOSSLESS_HP, "Lossless, High Performance",
        "lossless-hp"},
    {0, NULL, NULL},
  };

  if (g_once_init_enter (&preset_type)) {
    GType type = g_enum_register_static ("GstNvEncoderPreset", presets);

    g_once_init_leave (&preset_type, type);
  }

  return preset_type;
}

void
gst_nv_encoder_preset_to_guid (GstNvEncoderPreset preset, GUID * guid)
{
  switch (preset) {
    case GST_NV_ENCODER_PRESET_DEFAULT:
      *guid = NV_ENC_PRESET_DEFAULT_GUID;
      break;
    case GST_NV_ENCODER_PRESET_HP:
      *guid = NV_ENC_PRESET_HP_GUID;
      break;
    case GST_NV_ENCODER_PRESET_HQ:
      *guid = NV_ENC_PRESET_HQ_GUID;
      break;
    case GST_NV_ENCODER_PRESET_LOW_LATENCY_DEFAULT:
      *guid = NV_ENC_PRESET_LOW_LATENCY_DEFAULT_GUID;
      break;
    case GST_NV_ENCODER_PRESET_LOW_LATENCY_HQ:
      *guid = NV_ENC_PRESET_LOW_LATENCY_HQ_GUID;
      break;
    case GST_NV_ENCODER_PRESET_LOW_LATENCY_HP:
      *guid = NV_ENC_PRESET_LOW_LATENCY_HP_GUID;
      break;
    case GST_NV_ENCODER_PRESET_LOSSLESS_DEFAULT:
      *guid = NV_ENC_PRESET_LOSSLESS_DEFAULT_GUID;
      break;
    case GST_NV_ENCODER_PRESET_LOSSLESS_HP:
      *guid = NV_ENC_PRESET_LOSSLESS_HP_GUID;
      break;
    default:
      break;
  }

  *guid = NV_ENC_PRESET_DEFAULT_GUID;
}

GType
gst_nv_encoder_rc_mode_get_type (void)
{
  static GType rc_mode_type = 0;
  static const GEnumValue rc_modes[] = {
    {GST_NV_ENCODER_RC_MODE_DEFAULT, "Default", "default"},
    {GST_NV_ENCODER_RC_MODE_CONSTQP, "Constant Quantization", "constqp"},
    {GST_NV_ENCODER_RC_MODE_VBR, "Variable Bit Rate", "vbr"},
    {GST_NV_ENCODER_RC_MODE_CBR, "Constant Bit Rate", "cbr"},
    {GST_NV_ENCODER_RC_MODE_CBR_LOWDELAY_HQ,
        "Low-Delay CBR, High Quality", "cbr-ld-hq"},
    {GST_NV_ENCODER_RC_MODE_CBR_HQ, "CBR, High Quality (slower)", "cbr-hq"},
    {GST_NV_ENCODER_RC_MODE_VBR_HQ, "VBR, High Quality (slower)", "vbr-hq"},
    {0, NULL, NULL},
  };

  if (g_once_init_enter (&rc_mode_type)) {
    GType type = g_enum_register_static ("GstNvEncoderRCMode", rc_modes);

    g_once_init_leave (&rc_mode_type, type);
  }

  return rc_mode_type;
}

NV_ENC_PARAMS_RC_MODE
gst_nv_encoder_rc_mode_to_native (GstNvEncoderRCMode rc_mode)
{
  switch (rc_mode) {
    case GST_NV_ENCODER_RC_MODE_DEFAULT:
      return NV_ENC_PARAMS_RC_VBR;
    case GST_NV_ENCODER_RC_MODE_CONSTQP:
      return NV_ENC_PARAMS_RC_CONSTQP;
    case GST_NV_ENCODER_RC_MODE_VBR:
      return NV_ENC_PARAMS_RC_VBR;
    case GST_NV_ENCODER_RC_MODE_CBR:
      return NV_ENC_PARAMS_RC_CBR;
    case GST_NV_ENCODER_RC_MODE_CBR_LOWDELAY_HQ:
      return NV_ENC_PARAMS_RC_CBR_LOWDELAY_HQ;
    case GST_NV_ENCODER_RC_MODE_CBR_HQ:
      return NV_ENC_PARAMS_RC_CBR_HQ;
    case GST_NV_ENCODER_RC_MODE_VBR_HQ:
      return NV_ENC_PARAMS_RC_VBR_HQ;
    default:
      break;
  }

  return NV_ENC_PARAMS_RC_VBR;
}

const gchar *
gst_nv_encoder_status_to_string (NVENCSTATUS status)
{
#define CASE(err) \
    case err: \
    return G_STRINGIFY (err);

  switch (status) {
      CASE (NV_ENC_SUCCESS);
      CASE (NV_ENC_ERR_NO_ENCODE_DEVICE);
      CASE (NV_ENC_ERR_UNSUPPORTED_DEVICE);
      CASE (NV_ENC_ERR_INVALID_ENCODERDEVICE);
      CASE (NV_ENC_ERR_INVALID_DEVICE);
      CASE (NV_ENC_ERR_DEVICE_NOT_EXIST);
      CASE (NV_ENC_ERR_INVALID_PTR);
      CASE (NV_ENC_ERR_INVALID_EVENT);
      CASE (NV_ENC_ERR_INVALID_PARAM);
      CASE (NV_ENC_ERR_INVALID_CALL);
      CASE (NV_ENC_ERR_OUT_OF_MEMORY);
      CASE (NV_ENC_ERR_ENCODER_NOT_INITIALIZED);
      CASE (NV_ENC_ERR_UNSUPPORTED_PARAM);
      CASE (NV_ENC_ERR_LOCK_BUSY);
      CASE (NV_ENC_ERR_NOT_ENOUGH_BUFFER);
      CASE (NV_ENC_ERR_INVALID_VERSION);
      CASE (NV_ENC_ERR_MAP_FAILED);
      CASE (NV_ENC_ERR_NEED_MORE_INPUT);
      CASE (NV_ENC_ERR_ENCODER_BUSY);
      CASE (NV_ENC_ERR_EVENT_NOT_REGISTERD);
      CASE (NV_ENC_ERR_GENERIC);
      CASE (NV_ENC_ERR_INCOMPATIBLE_CLIENT_KEY);
      CASE (NV_ENC_ERR_UNIMPLEMENTED);
      CASE (NV_ENC_ERR_RESOURCE_REGISTER_FAILED);
      CASE (NV_ENC_ERR_RESOURCE_NOT_REGISTERED);
      CASE (NV_ENC_ERR_RESOURCE_NOT_MAPPED);
    default:
      break;
  }
#undef CASE

  return "Unknown";
}
