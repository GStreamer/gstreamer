/* GStreamer
 * Copyright (C) 2024 Seungha Yang <seungha@centricular.com>
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

/* A CUDA based inplace transform example implementation.
 *
 * Apart from general requirements for a GStreamer element and CUDA programming,
 * plugin developers should implement GstContext handling for a single
 * GstCudaContext to be shared in the pipeline. That requires
 * GstElementClass::set_context() vfunc and GstQuery handler
 *
 * In addition to the GstContext handling, in case of multi-GPU system,
 * GstCudaContext update might need to be handled since upstream element
 * can produce CUDA memory which belongs to different GPU.
 *
 * This example CUDA element demonstrates:
 * - GstContext handling (device selection and GstCudaContext allocation)
 * - GstCudaContext update if needed via GstBaseTransform::before_transform() vfunc
 * - Simple CUDA operation in a GstBaseTransform subclass
 *
 * Note that CUDA API error handling is omitted to simplify the code
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "cuda-transform-ip-template.h"

#ifdef G_OS_WIN32
#include <windows.h>
#endif

#include <gst/video/video.h>
#include <gst/cuda/gstcuda.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_cuda_transform_ip_debug);
#define GST_CAT_DEFAULT gst_cuda_transform_ip_debug

#define STATIC_CAPS \
  GST_STATIC_CAPS ( \
      GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY, \
      "Y444"))

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK, GST_PAD_ALWAYS, STATIC_CAPS);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC, GST_PAD_ALWAYS, STATIC_CAPS);

enum
{
  PROP_0,
  PROP_DEVICE_ID,
  PROP_UPDATE_IMAGE,
};

// -1 = uses any GPU. Element will accept any already existing CUDA context
//      in the pipeline or user provided one
// others: Explicit GPU selection
#define DEFAULT_DEVICE_ID -1
#define DEFAULT_UPDATE_IMAGE TRUE

struct _GstCudaTransformIp
{
  GstBaseTransform parent;

  GstCudaContext *context;
  GstVideoInfo info;
  guint8 *read_host_mem;
  guint8 *write_host_mem;
  guint stride;
  guint size;

  /* Protects context since context update can happen in streaming thread
   * as well */
  GRecMutex lock;
  gboolean update_image;

  gint device_id;
};

static void gst_cuda_transform_ip_dispose (GObject * object);
static void gst_cuda_transform_ip_finalize (GObject * object);
static void gst_cuda_transform_ip_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_cuda_transform_ip_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static void gst_cuda_transform_ip_set_context (GstElement * element,
    GstContext * context);

static gboolean gst_cuda_transform_ip_start (GstBaseTransform * trans);
static gboolean gst_cuda_transform_ip_stop (GstBaseTransform * trans);
static gboolean gst_cuda_transform_ip_set_caps (GstBaseTransform * trans,
    GstCaps * incaps, GstCaps * outcaps);
static gboolean gst_cuda_transform_ip_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query);
static void gst_cuda_transform_ip_before_transform (GstBaseTransform * trans,
    GstBuffer * buffer);
static GstFlowReturn gst_cuda_transform_ip_execute (GstBaseTransform * trans,
    GstBuffer * buffer);

#define gst_cuda_transform_ip_parent_class parent_class
G_DEFINE_TYPE (GstCudaTransformIp, gst_cuda_transform_ip,
    GST_TYPE_BASE_TRANSFORM);

static void
gst_cuda_transform_ip_class_init (GstCudaTransformIpClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);

  object_class->dispose = gst_cuda_transform_ip_dispose;
  object_class->finalize = gst_cuda_transform_ip_finalize;
  object_class->set_property = gst_cuda_transform_ip_set_property;
  object_class->get_property = gst_cuda_transform_ip_get_property;

  g_object_class_install_property (object_class, PROP_DEVICE_ID,
      g_param_spec_int ("cuda-device-id",
          "CUDA Device ID", "CUDA GPU device id (-1 = auto)",
          -1, G_MAXINT, DEFAULT_DEVICE_ID,
          G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
          G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_UPDATE_IMAGE,
      g_param_spec_boolean ("update-image", "Image Update",
          "Update image to gray", DEFAULT_UPDATE_IMAGE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_cuda_transform_ip_set_context);

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_static_metadata (element_class,
      "CUDA transform-ip", "Filter/Video",
      "CUDA in-place transform template element",
      "Seungha Yang <seungha@centricular.com>");

  trans_class->start = GST_DEBUG_FUNCPTR (gst_cuda_transform_ip_start);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_cuda_transform_ip_stop);
  trans_class->query = GST_DEBUG_FUNCPTR (gst_cuda_transform_ip_query);
  trans_class->before_transform =
      GST_DEBUG_FUNCPTR (gst_cuda_transform_ip_before_transform);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_cuda_transform_ip_set_caps);
  trans_class->transform_ip = GST_DEBUG_FUNCPTR (gst_cuda_transform_ip_execute);
}

static void
gst_cuda_transform_ip_init (GstCudaTransformIp * self)
{
  self->device_id = DEFAULT_DEVICE_ID;
  self->update_image = DEFAULT_UPDATE_IMAGE;

  g_rec_mutex_init (&self->lock);
}

static void
gst_cuda_transform_ip_dispose (GObject * object)
{
  GstCudaTransformIp *self = GST_CUDA_TRANSFORM_IP (object);

  gst_clear_object (&self->context);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_cuda_transform_ip_finalize (GObject * object)
{
  GstCudaTransformIp *self = GST_CUDA_TRANSFORM_IP (object);

  g_rec_mutex_clear (&self->lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_cuda_transform_ip_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCudaTransformIp *self = GST_CUDA_TRANSFORM_IP (object);

  g_rec_mutex_lock (&self->lock);
  switch (prop_id) {
    case PROP_DEVICE_ID:
      self->device_id = g_value_get_int (value);
      break;
    case PROP_UPDATE_IMAGE:
      self->update_image = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  g_rec_mutex_unlock (&self->lock);
}

static void
gst_cuda_transform_ip_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCudaTransformIp *self = GST_CUDA_TRANSFORM_IP (object);

  g_rec_mutex_lock (&self->lock);
  switch (prop_id) {
    case PROP_DEVICE_ID:
      g_value_set_int (value, self->device_id);
      break;
    case PROP_UPDATE_IMAGE:
      g_value_set_boolean (value, self->update_image);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  g_rec_mutex_unlock (&self->lock);
}

static void
gst_cuda_transform_ip_set_context (GstElement * element, GstContext * context)
{
  GstCudaTransformIp *self = GST_CUDA_TRANSFORM_IP (element);

  g_rec_mutex_lock (&self->lock);

  /* Util function which parses GstContex type and sets cuda context if
   * given GstContext holds GstCudaContext with matching device-id */
  gst_cuda_handle_set_context (element,
      context, self->device_id, &self->context);

  g_rec_mutex_unlock (&self->lock);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_cuda_transform_ip_start (GstBaseTransform * trans)
{
  GstCudaTransformIp *self = GST_CUDA_TRANSFORM_IP (trans);
  gboolean ret;

  g_rec_mutex_lock (&self->lock);
  /* Util function which queries GstCudaContext and creates if needed */
  ret = gst_cuda_ensure_element_context (GST_ELEMENT (self), self->device_id,
      &self->context);
  g_rec_mutex_unlock (&self->lock);

  if (!ret) {
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND, (NULL),
        ("CUDA device unavailable"));
    return FALSE;
  }

  return TRUE;
}

static void
gst_cuda_transform_ip_prepare_resource (GstCudaTransformIp * self)
{
  gst_cuda_context_push (self->context);
  CuMemAllocHost ((void **) &self->read_host_mem, self->size);
  CuMemAllocHost ((void **) &self->write_host_mem, self->size);
  gst_cuda_context_pop (NULL);

  memset (self->write_host_mem, 128, self->size);
}

static void
gst_cuda_transform_ip_release_resource (GstCudaTransformIp * self)
{
  if (self->read_host_mem) {
    gst_cuda_context_push (self->context);
    CuMemFreeHost (self->read_host_mem);
    self->read_host_mem = NULL;

    CuMemFreeHost (self->write_host_mem);
    self->write_host_mem = NULL;
    gst_cuda_context_pop (NULL);
  }
}

static gboolean
gst_cuda_transform_ip_stop (GstBaseTransform * trans)
{
  GstCudaTransformIp *self = GST_CUDA_TRANSFORM_IP (trans);

  gst_cuda_transform_ip_release_resource (self);
  gst_clear_object (&self->context);

  return TRUE;
}

static gboolean
gst_cuda_transform_ip_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query)
{
  GstCudaTransformIp *self = GST_CUDA_TRANSFORM_IP (trans);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
    {
      gboolean ret;
      g_rec_mutex_lock (&self->lock);
      ret = gst_cuda_handle_context_query (GST_ELEMENT (self), query,
          self->context);
      g_rec_mutex_unlock (&self->lock);

      /* Returns immediately if context query is handled here */
      if (ret)
        return TRUE;
      break;
    }
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->query (trans,
      direction, query);
}

static gboolean
gst_cuda_transform_ip_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstCudaTransformIp *self = GST_CUDA_TRANSFORM_IP (trans);

  gst_cuda_transform_ip_release_resource (self);

  if (!gst_video_info_from_caps (&self->info, incaps)) {
    GST_ERROR_OBJECT (self, "Invalid caps %" GST_PTR_FORMAT, incaps);
    return FALSE;
  }

  /* Prepare resolution dependent resources */
  self->stride = GST_ROUND_UP_64 (self->info.stride[0]);
  self->size = self->stride * self->info.height;

  gst_cuda_transform_ip_prepare_resource (self);

  return TRUE;
}

/* Checks GstCudaMemory's context and updates ours if needed */
static void
gst_cuda_transform_ip_before_transform (GstBaseTransform * trans,
    GstBuffer * buffer)
{
  GstCudaTransformIp *self = GST_CUDA_TRANSFORM_IP (trans);
  GstMemory *mem;
  GstCudaMemory *cmem;

  mem = gst_buffer_peek_memory (buffer, 0);
  g_assert (gst_is_cuda_memory (mem));

  cmem = GST_CUDA_MEMORY_CAST (mem);
  if (cmem->context != self->context) {
    GST_INFO_OBJECT (self, "updating context");
    g_rec_mutex_lock (&self->lock);
    gst_cuda_transform_ip_release_resource (self);
    gst_clear_object (&self->context);
    self->context = gst_object_ref (cmem->context);
    gst_cuda_transform_ip_prepare_resource (self);
    g_rec_mutex_unlock (&self->lock);
  }
}

static GstFlowReturn
gst_cuda_transform_ip_execute (GstBaseTransform * trans, GstBuffer * buffer)
{
  GstCudaTransformIp *self = GST_CUDA_TRANSFORM_IP (trans);
  GstMemory *mem;
  GstCudaMemory *cmem;
  CUstream stream;
  GstVideoFrame frame;
  GstMapFlags flags = GST_MAP_CUDA;
  CUDA_MEMCPY2D params;
  gboolean update_image;

  g_rec_mutex_lock (&self->lock);
  update_image = self->update_image;
  g_rec_mutex_unlock (&self->lock);

  /* Gets memory to access cuda stream object */
  mem = gst_buffer_peek_memory (buffer, 0);
  g_assert (gst_is_cuda_memory (mem));

  cmem = GST_CUDA_MEMORY_CAST (mem);
  /* NOTE: gst_cuda_stream_get_handle() is null-safe and will return
   * default stream if GstCudaStream is nullptr  */
  stream = gst_cuda_stream_get_handle (gst_cuda_memory_get_stream (cmem));

  /* BEGIN-ELEMENT-SPECIFIC-PROCESSING */
  if (update_image) {
    /* Emulating image update process (e.g., image enhancement) */
    flags |= GST_MAP_WRITE;
  } else {
    /* Emulating image analysis process (e.g., edge detection) */
    flags |= GST_MAP_READ;
  }

  if (!gst_video_frame_map (&frame, &self->info, buffer, flags)) {
    GST_ERROR_OBJECT (self, "Couldn't map buffer");
    return GST_FLOW_ERROR;
  }

  memset (&params, 0, sizeof (params));
  gst_cuda_context_push (self->context);
  if (update_image) {
    params.srcMemoryType = CU_MEMORYTYPE_HOST;
    params.srcHost = self->write_host_mem;
    params.srcPitch = self->stride;

    params.dstMemoryType = CU_MEMORYTYPE_DEVICE;
    params.dstDevice = (CUdeviceptr) GST_VIDEO_FRAME_PLANE_DATA (&frame, 1);
    params.dstPitch = GST_VIDEO_FRAME_PLANE_STRIDE (&frame, 1);

    params.WidthInBytes = GST_VIDEO_FRAME_WIDTH (&frame);
    params.Height = GST_VIDEO_FRAME_HEIGHT (&frame);

    /* Upload to U plane */
    CuMemcpy2DAsync (&params, stream);

    params.dstDevice = (CUdeviceptr) GST_VIDEO_FRAME_PLANE_DATA (&frame, 2);

    /* Upload to V plane */
    CuMemcpy2DAsync (&params, stream);
  } else {
    /* Download Y plane data */
    params.srcMemoryType = CU_MEMORYTYPE_DEVICE;
    params.srcDevice = (CUdeviceptr) GST_VIDEO_FRAME_PLANE_DATA (&frame, 0);
    params.srcPitch = GST_VIDEO_FRAME_PLANE_STRIDE (&frame, 0);

    params.dstMemoryType = CU_MEMORYTYPE_HOST;
    params.dstHost = self->read_host_mem;
    params.dstPitch = self->stride;

    params.WidthInBytes = GST_VIDEO_FRAME_WIDTH (&frame);
    params.Height = GST_VIDEO_FRAME_HEIGHT (&frame);

    CuMemcpy2DAsync (&params, stream);
    CuStreamSynchronize (stream);

    /* Do something */
  }
  gst_cuda_context_pop (NULL);

  gst_video_frame_unmap (&frame);

  if (update_image) {
    /* Writable map can replace memory of the given buffer if memory was not
     * writable when map() was called. Gets memory pointer again */
    mem = gst_buffer_peek_memory (buffer, 0);

    /* We skipped CuStreamSynchronize() above. Mark this memory is not
     * synchronized yet */
    GST_MINI_OBJECT_FLAG_SET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_SYNC);
  }

  /* END-ELEMENT-SPECIFIC-PROCESSING */

  return GST_FLOW_OK;
}
