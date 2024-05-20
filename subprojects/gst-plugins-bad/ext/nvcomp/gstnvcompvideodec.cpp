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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstnvcompvideodec.h"

#ifdef HAVE_GST_GL
#include <gst/gl/gl.h>
#include <gst/gl/gstglfuncs.h>
#endif

#include <nvcomp/nvcompManagerFactory.hpp>
#include <nvcomp/ans.h>
#include <nvcomp/bitcomp.h>
#include <nvcomp/cascaded.h>
#include <nvcomp/deflate.h>
#include <nvcomp/gdeflate.h>
#include <nvcomp/lz4.h>
#include <nvcomp/snappy.h>
#include <nvcomp/zstd.h>
#include <memory>
#include <string>
#include <string.h>
#include <vector>

GST_DEBUG_CATEGORY_STATIC (gst_nv_comp_video_dec_debug);
#define GST_CAT_DEFAULT gst_nv_comp_video_dec_debug

#ifdef HAVE_GST_GL
#define SRC_CAPS \
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY, \
        GST_VIDEO_FORMATS_ALL) ";" \
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_GL_MEMORY, \
        GST_VIDEO_FORMATS_ALL) ";" \
    GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS_ALL)
#else
#define SRC_CAPS \
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY, \
        GST_VIDEO_FORMATS_ALL) ";" \
    GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS_ALL)
#endif

static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-nvcomp; video/x-nvcomp-lz4; "
        "video/x-nvcomp-snappy; video/x-nvcomp-gdeflate; "
        "video/x-nvcomp-deflate; video/x-nvcomp-zstd; video/x-nvcomp-cascaded; "
        "video/x-nvcomp-bitcomp; video/x-nvcomp-ans"));

static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SRC_CAPS));

/* *INDENT-OFF* */
using namespace nvcomp;

struct DecoderTask
{
  ~DecoderTask ()
  {
    if (ctx) {
      gst_cuda_context_push (ctx);
      clear_resource ();
      gst_cuda_context_pop (nullptr);
      gst_object_unref (ctx);
    }
  }

  void clear_resource ()
  {
    if (!ctx)
      return;

    if (device_compressed)
      CuMemFree ((CUdeviceptr) device_compressed);
    device_compressed = nullptr;

    if (host_compressed)
      CuMemFreeHost (host_compressed);
    host_compressed = nullptr;

    if (device_compressed_bytes)
      CuMemFree ((CUdeviceptr) device_compressed_bytes);
    device_compressed_bytes = nullptr;

    if (device_compressed_ptrs)
      CuMemFree ((CUdeviceptr) device_compressed_ptrs);
    device_compressed_ptrs = nullptr;

    if (host_compressed_bytes)
      CuMemFreeHost (host_compressed_bytes);
    host_compressed_bytes = nullptr;

    if (host_compressed_ptrs)
      CuMemFreeHost (host_compressed_ptrs);
    host_compressed_ptrs = nullptr;

    if (device_uncompressed)
      CuMemFree ((CUdeviceptr) device_uncompressed);
    device_uncompressed = nullptr;

    if (device_uncompressed_temp)
      CuMemFree ((CUdeviceptr) device_uncompressed_temp);
    device_uncompressed_temp = nullptr;

    if (host_uncompressed)
      CuMemFreeHost (host_uncompressed);
    host_uncompressed = nullptr;

    if (device_uncompressed_bytes)
      CuMemFree ((CUdeviceptr) device_uncompressed_bytes);
    device_uncompressed_bytes = nullptr;

    if (device_uncompressed_ptrs)
      CuMemFree ((CUdeviceptr) device_uncompressed_ptrs);
    device_uncompressed_ptrs = nullptr;

    if (host_uncompressed_bytes)
      CuMemFreeHost (host_uncompressed_bytes);
    host_uncompressed_bytes = nullptr;

    if (host_uncompressed_ptrs)
      CuMemFreeHost (host_uncompressed_ptrs);
    host_uncompressed_ptrs = nullptr;

    if (device_actual_uncompressed_bytes)
      CuMemFree ((CUdeviceptr) device_actual_uncompressed_bytes);
    device_actual_uncompressed_bytes = nullptr;

    if (temp_ptr)
      CuMemFree ((CUdeviceptr) temp_ptr);
    temp_ptr = nullptr;

    if (device_statuses)
      CuMemFree ((CUdeviceptr) device_statuses);
    device_statuses = nullptr;

    batch_size = 0;
    max_compressed_chunk_size = 0;
    max_uncompressed_chunk_size = 0;
  }

  bool allocate_batched (size_t num_chunks,
      size_t compressed_chunk_size,
      size_t uncompressed_chunk_size, size_t temp_bytes)
  {
    size_t compressed_alloc;
    size_t uncompressed_alloc;
    size_t alloc_size = num_chunks * sizeof (size_t);
    uint8_t *src;

    compressed_chunk_size = GST_ROUND_UP_8 (compressed_chunk_size);
    uncompressed_chunk_size = GST_ROUND_UP_8 (uncompressed_chunk_size);

    compressed_alloc = num_chunks * compressed_chunk_size;
    uncompressed_alloc = num_chunks * uncompressed_chunk_size;

    auto ret = CuMemAlloc ((CUdeviceptr *) &device_compressed,
        compressed_alloc);
    if (!gst_cuda_result (ret))
      return false;

    ret = CuMemAllocHost ((void **) &host_compressed, compressed_alloc);
    if (!gst_cuda_result (ret))
      return false;

    ret = CuMemAlloc ((CUdeviceptr *) &device_compressed_bytes,
        alloc_size);
    if (!gst_cuda_result (ret))
      return false;

    ret = CuMemAlloc ((CUdeviceptr *) &device_compressed_ptrs,
        alloc_size);
    if (!gst_cuda_result (ret))
      return false;

    ret = CuMemAllocHost ((void **) &host_compressed_bytes,
        alloc_size);
    if (!gst_cuda_result (ret))
      return false;

    ret = CuMemAllocHost ((void **) &host_compressed_ptrs,
        alloc_size);
    if (!gst_cuda_result (ret))
      return false;

    src = device_compressed;
    for (size_t i = 0; i < num_chunks; i++) {
      host_compressed_ptrs[i] = src;
      src += compressed_chunk_size;
    }

    ret = CuMemcpyHtoD ((CUdeviceptr) device_compressed_ptrs,
        host_compressed_ptrs, alloc_size);
    if (!gst_cuda_result (ret))
      return false;

    ret = CuMemAlloc ((CUdeviceptr *) &device_uncompressed_temp,
        uncompressed_alloc);
    if (!gst_cuda_result (ret))
      return false;

    ret = CuMemAlloc ((CUdeviceptr *) &device_uncompressed,
        uncompressed_alloc);
    if (!gst_cuda_result (ret))
      return false;

    ret = CuMemAllocHost ((void **) &host_uncompressed, uncompressed_alloc);
    if (!gst_cuda_result (ret))
      return false;

    ret = CuMemAlloc ((CUdeviceptr *) &device_uncompressed_bytes,
        alloc_size);
    if (!gst_cuda_result (ret))
      return false;

    ret = CuMemAlloc ((CUdeviceptr *) &device_uncompressed_ptrs,
        alloc_size);
    if (!gst_cuda_result (ret))
      return false;

    ret = CuMemAllocHost ((void **) &host_uncompressed_bytes,
        alloc_size);
    if (!gst_cuda_result (ret))
      return false;

    ret = CuMemAllocHost ((void **) &host_uncompressed_ptrs,
        alloc_size);
    if (!gst_cuda_result (ret))
      return false;

    src = device_uncompressed_temp;
    for (size_t i = 0; i < num_chunks; i++) {
      host_uncompressed_bytes[i] = uncompressed_chunk_size;
      host_uncompressed_ptrs[i] = src;
      src += uncompressed_chunk_size;
    }

    ret = CuMemcpyHtoD ((CUdeviceptr) device_uncompressed_bytes,
        host_uncompressed_bytes, alloc_size);
    if (!gst_cuda_result (ret))
      return false;

    ret = CuMemcpyHtoD ((CUdeviceptr) device_uncompressed_ptrs,
        host_uncompressed_ptrs, alloc_size);
    if (!gst_cuda_result (ret))
      return false;

    ret = CuMemAlloc ((CUdeviceptr *) &device_actual_uncompressed_bytes,
        alloc_size);
    if (!gst_cuda_result (ret))
      return false;

    if (temp_bytes > 0) {
      ret = CuMemAlloc ((CUdeviceptr *) &temp_ptr, temp_bytes);
      if (!gst_cuda_result (ret))
        return false;
    }

    ret = CuMemAlloc ((CUdeviceptr *) &device_statuses,
        sizeof (nvcompStatus_t) * num_chunks);
    if (!gst_cuda_result (ret))
      return false;

    batched = TRUE;
    batch_size = num_chunks;
    temp_size = temp_bytes;
    max_compressed_chunk_size = compressed_chunk_size;
    max_uncompressed_chunk_size = uncompressed_chunk_size;
    compressed_alloc_size = compressed_alloc;
    uncompressed_alloc_size = uncompressed_alloc;

    return true;
  }

  GstCudaContext *ctx = nullptr;

  uint8_t *device_compressed = nullptr;
  uint8_t *host_compressed = nullptr;

  size_t *device_compressed_bytes = nullptr;
  void **device_compressed_ptrs = nullptr;

  size_t *host_compressed_bytes = nullptr;
  void **host_compressed_ptrs = nullptr;

  uint8_t *device_uncompressed = nullptr;
  uint8_t *device_uncompressed_temp = nullptr;
  uint8_t *host_uncompressed = nullptr;

  size_t *device_uncompressed_bytes = nullptr;
  void **device_uncompressed_ptrs = nullptr;

  size_t *host_uncompressed_bytes = nullptr;
  void **host_uncompressed_ptrs = nullptr;

  size_t *device_actual_uncompressed_bytes = nullptr;

  void *temp_ptr = nullptr;
  size_t temp_size = 0;

  nvcompStatus_t *device_statuses = nullptr;

  gboolean batched = FALSE;
  size_t batch_size = 0;
  size_t max_uncompressed_chunk_size = 0;
  size_t max_compressed_chunk_size = 0;
  size_t uncompressed_alloc_size = 0;
  size_t compressed_alloc_size = 0;
};

struct BatchedDecompBase
{
  virtual nvcompStatus_t get_temp_size(
      size_t num_chunks,
      size_t max_uncompressed_chunk_bytes,
      size_t * temp_bytes) = 0;

  virtual nvcompStatus_t decompress(
    void **device_compressed_ptrs,
    size_t *device_compressed_bytes,
    size_t *device_uncompressed_bytes,
    size_t *device_actual_uncompressed_bytes,
    size_t batch_size,
    void *device_temp_ptr,
    size_t temp_bytes,
    void **device_uncompressed_ptrs,
    nvcompStatus_t *device_statuses,
    cudaStream_t stream) = 0;
};

template <auto T, auto D>
class BatchedDecomp : public BatchedDecompBase
{
public:
  BatchedDecomp () {}

  nvcompStatus_t get_temp_size(
      size_t num_chunks,
      size_t max_uncompressed_chunk_bytes,
      size_t * temp_bytes)
  {
    return T (num_chunks, max_uncompressed_chunk_bytes, temp_bytes);
  }

  nvcompStatus_t decompress(
    void **device_compressed_ptrs,
    size_t *device_compressed_bytes,
    size_t *device_uncompressed_bytes,
    size_t *device_actual_uncompressed_bytes,
    size_t batch_size,
    void *device_temp_ptr,
    size_t temp_bytes,
    void **device_uncompressed_ptrs,
    nvcompStatus_t *device_statuses,
    cudaStream_t stream)
  {
    return D (device_compressed_ptrs, device_compressed_bytes,
        device_uncompressed_bytes, device_actual_uncompressed_bytes,
        batch_size, device_temp_ptr, temp_bytes, device_uncompressed_ptrs,
        device_statuses, stream);
  }
};

struct GstNvCompVideoDecPrivate
{
  GstNvCompVideoDecPrivate ()
  {
    gst_video_info_init (&info);
  }

  GstCudaContext *ctx = nullptr;
  GstCudaStream *stream = nullptr;

#ifdef HAVE_GST_GL
  GstGLDisplay *gl_display = nullptr;
  GstGLContext *gl_context = nullptr;
  GstGLContext *other_gl_context = nullptr;
#endif

  GstVideoCodecState *state = nullptr;
  std::shared_ptr<nvcompManagerBase> manager;
  std::shared_ptr<BatchedDecompBase> batched_decomp;
  std::shared_ptr<DecoderTask> task;
  gboolean gl_interop = FALSE;

  GstVideoInfo info;
  gboolean batched = FALSE;
  GstNvCompMethod method;
};
/* *INDENT-ON* */

struct _GstNvCompVideoDec
{
  GstVideoDecoder parent;
  GstNvCompVideoDecPrivate *priv;
};

static void gst_nv_comp_video_dec_finalize (GObject * object);

static void gst_nv_comp_video_dec_set_context (GstElement * element,
    GstContext * context);

static gboolean gst_nv_comp_video_dec_open (GstVideoDecoder * decoder);
static gboolean gst_nv_comp_video_dec_close (GstVideoDecoder * decoder);
static gboolean gst_nv_comp_video_dec_sink_query (GstVideoDecoder * decoder,
    GstQuery * query);
static gboolean gst_nv_comp_video_dec_src_query (GstVideoDecoder * decoder,
    GstQuery * query);
static gboolean
gst_nv_comp_video_dec_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query);
static gboolean gst_nv_comp_video_dec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state);
static gboolean gst_nv_comp_video_dec_negotiate (GstVideoDecoder * decoder);
static GstFlowReturn
gst_nv_comp_video_dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);

#define gst_nv_comp_video_dec_parent_class parent_class
G_DEFINE_TYPE (GstNvCompVideoDec,
    gst_nv_comp_video_dec, GST_TYPE_VIDEO_DECODER);

static void
gst_nv_comp_video_dec_class_init (GstNvCompVideoDecClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto element_class = GST_ELEMENT_CLASS (klass);
  auto decoder_class = GST_VIDEO_DECODER_CLASS (klass);

  object_class->finalize = gst_nv_comp_video_dec_finalize;

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_static_metadata (element_class,
      "nvCOMP Video Decoder", "Decoder/Video/Hardware",
      "Decompress a video stream using nvCOMP library",
      "Seungha Yang <seungha@centricular.com>");

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_nv_comp_video_dec_set_context);

  decoder_class->open = GST_DEBUG_FUNCPTR (gst_nv_comp_video_dec_open);
  decoder_class->close = GST_DEBUG_FUNCPTR (gst_nv_comp_video_dec_close);
  decoder_class->sink_query =
      GST_DEBUG_FUNCPTR (gst_nv_comp_video_dec_sink_query);
  decoder_class->src_query =
      GST_DEBUG_FUNCPTR (gst_nv_comp_video_dec_src_query);
  decoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_nv_comp_video_dec_decide_allocation);
  decoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_nv_comp_video_dec_set_format);
  decoder_class->negotiate =
      GST_DEBUG_FUNCPTR (gst_nv_comp_video_dec_negotiate);
  decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_nv_comp_video_dec_handle_frame);

  GST_DEBUG_CATEGORY_INIT (gst_nv_comp_video_dec_debug,
      "nvcompvideodec", 0, "nvcompvideodec");
}

static void
gst_nv_comp_video_dec_init (GstNvCompVideoDec * self)
{
  self->priv = new GstNvCompVideoDecPrivate ();
}

static void
gst_nv_comp_video_dec_finalize (GObject * object)
{
  auto self = GST_NV_COMP_VIDEO_DEC (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_nv_comp_video_dec_set_context (GstElement * element, GstContext * context)
{
  auto self = GST_NV_COMP_VIDEO_DEC (element);
  auto priv = self->priv;

  gst_cuda_handle_set_context (element, context, -1, &priv->ctx);
#ifdef HAVE_GST_GL
  if (gst_gl_handle_set_context (element, context, &priv->gl_display,
          &priv->other_gl_context)) {
    if (priv->gl_display)
      gst_gl_display_filter_gl_api (priv->gl_display, GST_GL_API_OPENGL3);
  }
#endif

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_nv_comp_video_dec_open (GstVideoDecoder * decoder)
{
  auto self = GST_NV_COMP_VIDEO_DEC (decoder);
  auto priv = self->priv;

  if (!gst_cuda_ensure_element_context (GST_ELEMENT_CAST (decoder),
          -1, &priv->ctx)) {
    GST_ERROR_OBJECT (self, "Couldn't get cuda context");
    return FALSE;
  }

  priv->stream = gst_cuda_stream_new (priv->ctx);

  return TRUE;
}

static gboolean
gst_nv_comp_video_dec_close (GstVideoDecoder * decoder)
{
  auto self = GST_NV_COMP_VIDEO_DEC (decoder);
  auto priv = self->priv;

  if (priv->ctx) {
    gst_cuda_context_push (priv->ctx);
    priv->manager = nullptr;
    priv->task = nullptr;

    gst_cuda_context_pop (nullptr);
  }

  gst_clear_cuda_stream (&priv->stream);
  gst_clear_object (&priv->ctx);

#ifdef HAVE_GST_GL
  gst_clear_object (&priv->other_gl_context);
  gst_clear_object (&priv->gl_context);
  gst_clear_object (&priv->gl_context);
#endif

  return TRUE;
}

static gboolean
gst_nv_comp_video_dec_handle_context_query (GstNvCompVideoDec * self,
    GstQuery * query)
{
  auto priv = self->priv;

#ifdef HAVE_GST_GL
  {
    GstGLDisplay *display = nullptr;
    GstGLContext *other = nullptr;
    GstGLContext *local = nullptr;

    if (priv->gl_display)
      display = (GstGLDisplay *) gst_object_ref (priv->gl_display);
    if (priv->gl_context)
      local = (GstGLContext *) gst_object_ref (priv->gl_context);
    if (priv->other_gl_context)
      other = (GstGLContext *) gst_object_ref (priv->other_gl_context);

    auto ret = gst_gl_handle_context_query (GST_ELEMENT (self), query,
        display, local, other);
    gst_clear_object (&display);
    gst_clear_object (&other);
    gst_clear_object (&local);

    if (ret)
      return TRUE;
  }
#endif

  if (gst_cuda_handle_context_query (GST_ELEMENT (self), query, priv->ctx))
    return TRUE;

  return FALSE;
}

static gboolean
gst_nv_comp_video_dec_sink_query (GstVideoDecoder * decoder, GstQuery * query)
{
  auto self = GST_NV_COMP_VIDEO_DEC (decoder);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      if (gst_nv_comp_video_dec_handle_context_query (self, query))
        return TRUE;
      break;
    default:
      break;
  }

  return GST_VIDEO_DECODER_CLASS (parent_class)->sink_query (decoder, query);
}

static gboolean
gst_nv_comp_video_dec_src_query (GstVideoDecoder * decoder, GstQuery * query)
{
  auto self = GST_NV_COMP_VIDEO_DEC (decoder);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      if (gst_nv_comp_video_dec_handle_context_query (self, query))
        return TRUE;
      break;
    default:
      break;
  }

  return GST_VIDEO_DECODER_CLASS (parent_class)->src_query (decoder, query);
}

#ifdef HAVE_GST_GL
static void
check_cuda_device_from_gl_context (GstGLContext * context, gboolean * ret)
{
  guint device_count = 0;
  CUdevice device_list[1] = { 0, };
  CUresult cuda_ret;

  *ret = FALSE;
  cuda_ret = CuGLGetDevices (&device_count,
      device_list, 1, CU_GL_DEVICE_LIST_ALL);

  if (!gst_cuda_result (cuda_ret) || device_count == 0)
    return;

  *ret = TRUE;
}

static gboolean
gst_nv_comp_video_dec_ensure_gl_context (GstNvCompVideoDec * self)
{
  auto priv = self->priv;
  gboolean ret = FALSE;

  if (!gst_gl_ensure_element_data (GST_ELEMENT (self), &priv->gl_display,
          &priv->other_gl_context)) {
    GST_DEBUG_OBJECT (self, "Couldn't get GL display");
    return FALSE;
  }

  gst_gl_display_filter_gl_api (priv->gl_display, GST_GL_API_OPENGL3);

  if (!gst_gl_display_ensure_context (priv->gl_display, priv->other_gl_context,
          &priv->gl_context, nullptr)) {
    GST_DEBUG_OBJECT (self, "Couldn't get GL context");
    return FALSE;
  }

  gst_gl_context_thread_add (priv->gl_context,
      (GstGLContextThreadFunc) check_cuda_device_from_gl_context, &ret);

  return ret;
}
#endif

static gboolean
gst_nv_comp_video_dec_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query)
{
  auto self = GST_NV_COMP_VIDEO_DEC (decoder);
  auto priv = self->priv;
  GstBufferPool *pool = nullptr;
  guint size;
  guint min = 0;
  guint max = 0;
  GstCaps *caps;

  gst_query_parse_allocation (query, &caps, nullptr);
  if (!caps) {
    GST_WARNING_OBJECT (self, "null caps in query");
    return FALSE;
  }

  GstVideoInfo info;
  if (!gst_video_info_from_caps (&info, caps)) {
    GST_WARNING_OBJECT (self, "Failed to convert caps into info");
    return FALSE;
  }

  gboolean update_pool = FALSE;
  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    update_pool = TRUE;
  }

  auto features = gst_caps_get_features (caps, 0);
  gboolean use_cuda_pool = FALSE;
  if (gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY)) {
    GST_DEBUG_OBJECT (self, "Downstream support CUDA memory");
    if (pool) {
      if (!GST_IS_CUDA_BUFFER_POOL (pool)) {
        gst_clear_object (&pool);
      } else {
        auto cuda_pool = GST_CUDA_BUFFER_POOL (pool);
        if (cuda_pool->context != priv->ctx)
          gst_clear_object (&pool);
      }
    }

    if (!pool)
      pool = gst_cuda_buffer_pool_new (priv->ctx);
    use_cuda_pool = TRUE;
  }
#ifdef HAVE_GST_GL
  else if (gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_GL_MEMORY) && priv->gl_interop) {
    GST_DEBUG_OBJECT (self, "Downstream support GL memory");
    if (!gst_nv_comp_video_dec_ensure_gl_context (self)) {
      priv->gl_interop = FALSE;
    } else {
      if (pool && !GST_IS_GL_BUFFER_POOL (pool))
        gst_clear_object (&pool);

      if (!pool)
        pool = gst_gl_buffer_pool_new (priv->gl_context);
    }
  }
#endif

  if (!pool)
    pool = gst_video_buffer_pool_new ();

  auto config = gst_buffer_pool_get_config (pool);

  size = GST_VIDEO_INFO_SIZE (&info);
  gst_buffer_pool_config_set_params (config, caps, size, 0, 0);
  if (use_cuda_pool && priv->stream) {
    /* Set our stream on buffer pool config so that CUstream can be shared */
    gst_buffer_pool_config_set_cuda_stream (config, priv->stream);
  }

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_WARNING_OBJECT (self, "Failed to set pool config");
    gst_object_unref (pool);
    return FALSE;
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_get_params (config, nullptr, &size, nullptr, nullptr);
  gst_structure_free (config);

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);
  gst_object_unref (pool);

  return TRUE;
}

static gboolean
gst_nv_comp_video_dec_alloc_task (GstNvCompVideoDec * self,
    DecoderTask * task, gboolean batched, gsize size)
{
  if (batched)
    return TRUE;

  task->uncompressed_alloc_size = size;
  auto cuda_ret =
      CuMemAlloc ((CUdeviceptr *) & task->device_uncompressed, size);
  if (!gst_cuda_result (cuda_ret))
    return FALSE;

  cuda_ret = CuMemAllocHost ((void **) &task->host_uncompressed, size);
  if (!gst_cuda_result (cuda_ret))
    return FALSE;

  task->compressed_alloc_size = size;
  cuda_ret = CuMemAlloc ((CUdeviceptr *) & task->device_compressed, size);
  if (!gst_cuda_result (cuda_ret))
    return FALSE;

  cuda_ret = CuMemAllocHost ((void **) &task->host_compressed, size);
  if (!gst_cuda_result (cuda_ret))
    return FALSE;

  return TRUE;
}

static gboolean
gst_nv_comp_video_dec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state)
{
  auto self = GST_NV_COMP_VIDEO_DEC (decoder);
  auto priv = self->priv;

  if (!priv->ctx) {
    GST_ERROR_OBJECT (self, "CUDA context was not configured");
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "Set format with caps %" GST_PTR_FORMAT, state->caps);

  g_clear_pointer (&priv->state, gst_video_codec_state_unref);
  priv->state = gst_video_codec_state_ref (state);

  auto s = gst_caps_get_structure (state->caps, 0);
  std::string mime_type = gst_structure_get_name (s);

  auto format_str = gst_structure_get_string (s, "format");
  if (!format_str) {
    GST_ERROR_OBJECT (self, "Unknown video format");
    return FALSE;
  }

  GstVideoFormat format = gst_video_format_from_string (format_str);
  if (format == GST_VIDEO_FORMAT_UNKNOWN || format == GST_VIDEO_FORMAT_ENCODED) {
    GST_ERROR_OBJECT (self, "Invalid format string %s", format_str);
    return FALSE;
  }

  s = gst_structure_copy (s);
  gst_structure_set_name (s, "video/x-raw");

  auto video_caps = gst_caps_new_empty ();
  gst_caps_append_structure (video_caps, s);

  auto ret = gst_video_info_from_caps (&priv->info, video_caps);
  gst_caps_unref (video_caps);
  if (!ret) {
    GST_ERROR_OBJECT (self, "Couldn't build output caps");
    return FALSE;
  }

  if (!gst_cuda_context_push (priv->ctx)) {
    GST_ERROR_OBJECT (self, "Couldn't push context");
    return FALSE;
  }

  priv->manager = nullptr;
  priv->batched_decomp = nullptr;
  priv->task = nullptr;

  priv->batched = TRUE;
  if (mime_type == "video/x-nvcomp") {
    priv->batched = FALSE;
  } else if (mime_type == "video/x-nvcomp-lz4") {
    priv->batched_decomp = std::make_shared < BatchedDecomp <
        nvcompBatchedLZ4DecompressGetTempSize,
        nvcompBatchedLZ4DecompressAsync >> ();
  } else if (mime_type == "video/x-nvcomp-snappy") {
    priv->batched_decomp = std::make_shared < BatchedDecomp <
        nvcompBatchedSnappyDecompressGetTempSize,
        nvcompBatchedSnappyDecompressAsync >> ();
  } else if (mime_type == "video/x-nvcomp-gdeflate") {
    priv->batched_decomp = std::make_shared < BatchedDecomp <
        nvcompBatchedGdeflateDecompressGetTempSize,
        nvcompBatchedGdeflateDecompressAsync >> ();
  } else if (mime_type == "video/x-nvcomp-deflate") {
    priv->batched_decomp = std::make_shared < BatchedDecomp <
        nvcompBatchedDeflateDecompressGetTempSize,
        nvcompBatchedDeflateDecompressAsync >> ();
  } else if (mime_type == "video/x-nvcomp-zstd") {
    priv->batched_decomp = std::make_shared < BatchedDecomp <
        nvcompBatchedZstdDecompressGetTempSize,
        nvcompBatchedZstdDecompressAsync >> ();
  } else if (mime_type == "video/x-nvcomp-cascaded") {
    priv->batched_decomp = std::make_shared < BatchedDecomp <
        nvcompBatchedCascadedDecompressGetTempSize,
        nvcompBatchedCascadedDecompressAsync >> ();
  } else if (mime_type == "video/x-nvcomp-bitcomp") {
    priv->batched_decomp = std::make_shared < BatchedDecomp <
        nvcompBatchedBitcompDecompressGetTempSize,
        nvcompBatchedBitcompDecompressAsync >> ();
  } else if (mime_type == "video/x-nvcomp-ans") {
    priv->batched_decomp = std::make_shared < BatchedDecomp <
        nvcompBatchedANSDecompressGetTempSize,
        nvcompBatchedANSDecompressAsync >> ();
  } else {
    gst_cuda_context_pop (nullptr);
    g_assert_not_reached ();
    return FALSE;
  }

  auto task = std::make_shared < DecoderTask > ();
  task->ctx = (GstCudaContext *) gst_object_ref (priv->ctx);

  if (!gst_nv_comp_video_dec_alloc_task (self, task.get (), priv->batched,
          priv->info.size)) {
    task = nullptr;
    gst_cuda_context_pop (nullptr);
    return FALSE;
  }

  priv->task = task;
  gst_cuda_context_pop (nullptr);

  return gst_video_decoder_negotiate (decoder);
}

static gboolean
is_supported_cuda_format (GstVideoFormat format)
{
  switch (format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
    case GST_VIDEO_FORMAT_P010_10LE:
    case GST_VIDEO_FORMAT_P012_LE:
    case GST_VIDEO_FORMAT_P016_LE:
    case GST_VIDEO_FORMAT_I420_10LE:
    case GST_VIDEO_FORMAT_I420_12LE:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y444_10LE:
    case GST_VIDEO_FORMAT_Y444_12LE:
    case GST_VIDEO_FORMAT_Y444_16LE:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_BGR10A2_LE:
    case GST_VIDEO_FORMAT_RGB10A2_LE:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_I422_10LE:
    case GST_VIDEO_FORMAT_I422_12LE:
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_RGBP:
    case GST_VIDEO_FORMAT_BGRP:
    case GST_VIDEO_FORMAT_GBR:
    case GST_VIDEO_FORMAT_GBR_10LE:
    case GST_VIDEO_FORMAT_GBR_12LE:
    case GST_VIDEO_FORMAT_GBR_16LE:
    case GST_VIDEO_FORMAT_GBRA:
    case GST_VIDEO_FORMAT_VUYA:
      return TRUE;
    default:
      break;
  }

  return FALSE;
}

#ifdef HAVE_GST_GL
static gboolean
is_supported_gl_format (GstVideoFormat format)
{
  auto gl_caps = gst_caps_from_string ("video/x-raw, format = (string) "
      GST_GL_COLOR_CONVERT_FORMATS);
  auto our_caps = gst_caps_new_empty_simple ("video/x-raw");
  gst_caps_set_simple (our_caps,
      "format", G_TYPE_STRING, gst_video_format_to_string (format), nullptr);
  auto ret = gst_caps_is_subset (our_caps, gl_caps);
  gst_caps_unref (gl_caps);
  gst_caps_unref (our_caps);

  return ret;
}
#endif

static gboolean
gst_nv_comp_video_dec_negotiate (GstVideoDecoder * decoder)
{
  auto self = GST_NV_COMP_VIDEO_DEC (decoder);
  auto priv = self->priv;
  gboolean is_cuda = FALSE;
#ifdef HAVE_GST_GL
  gboolean is_gl = FALSE;
#endif

  auto peer_caps = gst_pad_get_allowed_caps (decoder->srcpad);
  GST_DEBUG_OBJECT (self, "Allowed caps %" GST_PTR_FORMAT, peer_caps);

  if (!peer_caps || gst_caps_is_any (peer_caps)) {
    GST_DEBUG_OBJECT (self,
        "cannot determine output format, use system memory");
  } else {
    GstCapsFeatures *features;
    guint size = gst_caps_get_size (peer_caps);
    guint i;

    for (i = 0; i < size; i++) {
      features = gst_caps_get_features (peer_caps, i);

      if (!features)
        continue;

      if (gst_caps_features_contains (features,
              GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY)) {
        is_cuda = TRUE;
      }
#ifdef HAVE_GST_GL
      if (gst_caps_features_contains (features,
              GST_CAPS_FEATURE_MEMORY_GL_MEMORY)) {
        is_gl = TRUE;
      }
#endif
    }
  }
  gst_clear_caps (&peer_caps);

  auto state = gst_video_decoder_set_interlaced_output_state (decoder,
      GST_VIDEO_INFO_FORMAT (&priv->info),
      GST_VIDEO_INFO_INTERLACE_MODE (&priv->info), priv->info.width,
      priv->info.height, priv->state);

  if (!state) {
    GST_ERROR_OBJECT (self, "Couldn't set output state");
    return FALSE;
  }

  priv->gl_interop = FALSE;

  state->caps = gst_video_info_to_caps (&state->info);
  auto format = GST_VIDEO_INFO_FORMAT (&priv->info);
  if (is_cuda && is_supported_cuda_format (format)) {
    gst_caps_set_features_simple (state->caps,
        gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY, nullptr));
  }
#ifdef HAVE_GST_GL
  else if (is_gl && is_supported_gl_format (format)) {
    gst_caps_set_features_simple (state->caps,
        gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_GL_MEMORY, nullptr));
    priv->gl_interop = TRUE;
  }
#endif

  return GST_VIDEO_DECODER_CLASS (parent_class)->negotiate (decoder);
}

static gboolean
gst_nv_comp_video_dec_download (GstNvCompVideoDec * self, GstVideoFrame * frame,
    CUstream stream, gboolean is_device_copy)
{
  auto priv = self->priv;
  auto info = &priv->info;
  auto finfo = info->finfo;
  gint comp[GST_VIDEO_MAX_COMPONENTS];
  CUresult ret = CUDA_SUCCESS;
  auto task = priv->task;

  for (guint i = 0; i < GST_VIDEO_FRAME_N_PLANES (frame); i++) {
    guint8 *sp;
    if (is_device_copy)
      sp = task->device_uncompressed + info->offset[i];
    else
      sp = task->host_uncompressed + info->offset[i];

    guint8 *dp = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (frame, i);
    guint ss, ds;
    guint w, h;

    if (GST_VIDEO_FORMAT_INFO_HAS_PALETTE (finfo) && i == 1) {
      if (is_device_copy) {
        ret = CuMemcpyDtoDAsync ((CUdeviceptr) dp, (CUdeviceptr) sp,
            256 * 4, stream);
      } else {
        memcpy (dp, sp, 256 * 4);
      }

      if (!gst_cuda_result (ret)) {
        GST_ERROR_OBJECT (self, "CUDA memcpy failed");
        return FALSE;
      }

      return TRUE;
    }

    ds = GST_VIDEO_FRAME_PLANE_STRIDE (frame, i);
    ss = GST_VIDEO_INFO_PLANE_STRIDE (info, i);

    gst_video_format_info_component (finfo, i, comp);

    w = GST_VIDEO_INFO_COMP_WIDTH (info, comp[0]) *
        GST_VIDEO_INFO_COMP_PSTRIDE (info, comp[0]);
    if (w == 0)
      w = MIN (ss, ds);

    h = GST_VIDEO_INFO_COMP_HEIGHT (info, comp[0]);

    if (GST_VIDEO_FORMAT_INFO_IS_TILED (finfo)) {
      gint tile_size;
      gint sx_tiles, sy_tiles, dx_tiles, dy_tiles;
      GstVideoTileMode mode;

      tile_size = GST_VIDEO_FORMAT_INFO_TILE_SIZE (info->finfo, i);

      mode = GST_VIDEO_FORMAT_INFO_TILE_MODE (info->finfo);

      sx_tiles = GST_VIDEO_TILE_X_TILES (ss);
      sy_tiles = GST_VIDEO_TILE_Y_TILES (ss);

      dx_tiles = GST_VIDEO_TILE_X_TILES (ds);
      dy_tiles = GST_VIDEO_TILE_Y_TILES (ds);

      w = MIN (sx_tiles, dx_tiles);
      h = MIN (sy_tiles, dy_tiles);

      for (guint j = 0; j < h; j++) {
        for (guint k = 0; k < w; k++) {
          guint si, di;
          guint8 *cur_dp;
          guint8 *cur_sp;

          si = gst_video_tile_get_index (mode, k, j, sx_tiles, sy_tiles);
          di = gst_video_tile_get_index (mode, k, j, dx_tiles, dy_tiles);

          cur_dp = dp + (di * tile_size);
          cur_sp = sp + (si * tile_size);

          if (is_device_copy) {
            ret = CuMemcpyDtoDAsync ((CUdeviceptr) cur_dp, (CUdeviceptr) cur_sp,
                w, stream);
          } else {
            memcpy (cur_dp, cur_sp, w);
          }

          if (!gst_cuda_result (ret)) {
            GST_ERROR_OBJECT (self, "CUDA memcpy failed");
            return FALSE;
          }
        }
      }
    } else {
      if (is_device_copy) {
        CUDA_MEMCPY2D params = { };
        params.srcMemoryType = CU_MEMORYTYPE_DEVICE;
        params.srcDevice = (CUdeviceptr) sp;
        params.srcPitch = ss;

        params.dstMemoryType = CU_MEMORYTYPE_DEVICE;
        params.dstDevice = (CUdeviceptr) dp;
        params.dstPitch = ds;

        params.WidthInBytes = w;
        params.Height = h;

        ret = CuMemcpy2DAsync (&params, stream);

        if (!gst_cuda_result (ret)) {
          GST_ERROR_OBJECT (self, "CUDA memcpy failed");
          return FALSE;
        }
      } else {
        for (guint j = 0; j < h; j++) {
          memcpy (dp, sp, w);
          dp += ds;
          sp += ss;
        }
      }
    }
  }

  return TRUE;
}

#ifdef HAVE_GST_GL
struct GLInteropData
{
  GstNvCompVideoDec *self = nullptr;
  GstBuffer *buffer = nullptr;
  gboolean ret = FALSE;
};

static GstCudaGraphicsResource *
ensure_gl_cuda_resource (GstNvCompVideoDec * self, GstMemory * mem)
{
  auto priv = self->priv;
  GstCudaGraphicsResource *resource;
  GQuark quark;

  if (!gst_is_gl_memory_pbo (mem)) {
    GST_WARNING_OBJECT (self, "memory is not GL PBO memory, %s",
        mem->allocator->mem_type);
    return nullptr;
  }

  quark = gst_cuda_quark_from_id (GST_CUDA_QUARK_GRAPHICS_RESOURCE);
  resource = (GstCudaGraphicsResource *)
      gst_mini_object_get_qdata (GST_MINI_OBJECT (mem), quark);

  if (!resource) {
    GstMapInfo map_info;
    GstGLMemoryPBO *pbo = (GstGLMemoryPBO *) mem;
    GstGLBuffer *gl_buf = pbo->pbo;
    gboolean ret;

    if (!gst_memory_map (mem, &map_info,
            (GstMapFlags) (GST_MAP_READ | GST_MAP_GL))) {
      GST_ERROR_OBJECT (self, "Couldn't map gl memory");
      return nullptr;
    }

    resource = gst_cuda_graphics_resource_new (priv->ctx,
        GST_OBJECT (GST_GL_BASE_MEMORY_CAST (mem)->context),
        GST_CUDA_GRAPHICS_RESOURCE_GL_BUFFER);

    GST_LOG_OBJECT (self, "registering gl buffer %d to CUDA", gl_buf->id);
    ret = gst_cuda_graphics_resource_register_gl_buffer (resource, gl_buf->id,
        CU_GRAPHICS_REGISTER_FLAGS_NONE);
    gst_memory_unmap (mem, &map_info);

    if (!ret) {
      GST_ERROR_OBJECT (self, "Couldn't register gl buffer %d", gl_buf->id);
      gst_cuda_graphics_resource_free (resource);
      return nullptr;
    }

    gst_mini_object_set_qdata (GST_MINI_OBJECT (mem), quark, resource,
        (GDestroyNotify) gst_cuda_graphics_resource_free);
  }

  return resource;
}

static void
gst_nv_comp_video_dec_download_gl (GstGLContext * context, GLInteropData * data)
{
  auto self = data->self;
  auto priv = self->priv;
  auto info = &priv->info;
  auto finfo = info->finfo;
  GstCudaGraphicsResource *gst_res[GST_VIDEO_MAX_PLANES] = { nullptr, };
  CUgraphicsResource cuda_res[GST_VIDEO_MAX_PLANES] = { nullptr, };
  CUdeviceptr src_devptr[GST_VIDEO_MAX_PLANES] = { 0, };
  CUstream stream = gst_cuda_stream_get_handle (priv->stream);
  CUresult ret;
  gint comp[GST_VIDEO_MAX_COMPONENTS];
  auto task = priv->task;

  if (!gst_cuda_context_push (priv->ctx)) {
    GST_ERROR_OBJECT (self, "Couldn't push context");
    return;
  }

  for (guint i = 0; i < GST_VIDEO_INFO_N_PLANES (info); i++) {
    GstMemory *mem = gst_buffer_peek_memory (data->buffer, i);
    gsize src_size;

    if (!gst_is_gl_memory_pbo (mem)) {
      GST_ERROR_OBJECT (self, "Not a GL PBO memory");
      goto out;
    }

    gst_res[i] = ensure_gl_cuda_resource (self, mem);
    if (!gst_res[i]) {
      GST_ERROR_OBJECT (self, "Couldn't get resource %d", i);
      goto out;
    }

    cuda_res[i] = gst_cuda_graphics_resource_map (gst_res[i], stream,
        CU_GRAPHICS_MAP_RESOURCE_FLAGS_WRITE_DISCARD);
    if (!cuda_res[i]) {
      GST_ERROR_OBJECT (self, "Couldn't map resource");
      goto out;
    }

    ret = CuGraphicsResourceGetMappedPointer (&src_devptr[i],
        &src_size, cuda_res[i]);
    if (!gst_cuda_result (ret)) {
      GST_ERROR_OBJECT (self, "Couldn't get mapped device pointer");
      goto out;
    }

    /* Need PBO -> texture */
    GST_MINI_OBJECT_FLAG_SET (mem, GST_GL_BASE_MEMORY_TRANSFER_NEED_UPLOAD);
  }

  for (guint i = 0; i < GST_VIDEO_INFO_N_PLANES (info); i++) {
    guint8 *sp = task->device_uncompressed + info->offset[i];
    guint8 *dp = (guint8 *) src_devptr[i];
    guint ss, ds;
    guint w, h;

    if (GST_VIDEO_FORMAT_INFO_HAS_PALETTE (finfo) && i == 1) {
      ret = CuMemcpyDtoDAsync ((CUdeviceptr) dp, (CUdeviceptr) sp,
          256 * 4, stream);

      if (!gst_cuda_result (ret)) {
        GST_ERROR_OBJECT (self, "CUDA memcpy failed");
        goto out;
      }

      data->ret = TRUE;
      goto out;
    }

    auto meta = gst_buffer_get_video_meta (data->buffer);
    if (meta)
      ds = meta->stride[i];
    else
      ds = GST_VIDEO_INFO_PLANE_STRIDE (info, i);

    ss = GST_VIDEO_INFO_PLANE_STRIDE (info, i);

    gst_video_format_info_component (finfo, i, comp);

    w = GST_VIDEO_INFO_COMP_WIDTH (info, comp[0]) *
        GST_VIDEO_INFO_COMP_PSTRIDE (info, comp[0]);
    if (w == 0)
      w = MIN (ss, ds);

    h = GST_VIDEO_INFO_COMP_HEIGHT (info, comp[0]);

    if (GST_VIDEO_FORMAT_INFO_IS_TILED (finfo)) {
      gint tile_size;
      gint sx_tiles, sy_tiles, dx_tiles, dy_tiles;
      GstVideoTileMode mode;

      tile_size = GST_VIDEO_FORMAT_INFO_TILE_SIZE (info->finfo, i);

      mode = GST_VIDEO_FORMAT_INFO_TILE_MODE (info->finfo);

      sx_tiles = GST_VIDEO_TILE_X_TILES (ss);
      sy_tiles = GST_VIDEO_TILE_Y_TILES (ss);

      dx_tiles = GST_VIDEO_TILE_X_TILES (ds);
      dy_tiles = GST_VIDEO_TILE_Y_TILES (ds);

      w = MIN (sx_tiles, dx_tiles);
      h = MIN (sy_tiles, dy_tiles);

      for (guint j = 0; j < h; j++) {
        for (guint k = 0; k < w; k++) {
          guint si, di;
          guint8 *cur_dp;
          guint8 *cur_sp;

          si = gst_video_tile_get_index (mode, k, j, sx_tiles, sy_tiles);
          di = gst_video_tile_get_index (mode, k, j, dx_tiles, dy_tiles);

          cur_dp = dp + (di * tile_size);
          cur_sp = sp + (si * tile_size);

          ret = CuMemcpyDtoDAsync ((CUdeviceptr) cur_dp, (CUdeviceptr) cur_sp,
              w, stream);

          if (!gst_cuda_result (ret)) {
            GST_ERROR_OBJECT (self, "CUDA memcpy failed");
            goto out;
          }
        }
      }
    } else {
      CUDA_MEMCPY2D params = { };
      params.srcMemoryType = CU_MEMORYTYPE_DEVICE;
      params.srcDevice = (CUdeviceptr) sp;
      params.srcPitch = ss;

      params.dstMemoryType = CU_MEMORYTYPE_DEVICE;
      params.dstDevice = (CUdeviceptr) dp;
      params.dstPitch = ds;

      params.WidthInBytes = w;
      params.Height = h;

      ret = CuMemcpy2DAsync (&params, stream);
      if (!gst_cuda_result (ret)) {
        GST_ERROR_OBJECT (self, "CUDA memcpy failed");
        goto out;
      }
    }
  }

  data->ret = TRUE;

out:
  for (guint i = 0; i < gst_buffer_n_memory (data->buffer); i++) {
    if (!gst_res[i])
      break;

    gst_cuda_graphics_resource_unmap (gst_res[i], stream);
  }

  CuStreamSynchronize (stream);
  gst_cuda_context_pop (nullptr);
}
#endif

struct ChunkData
{
  size_t uncomp_size = 0;
  size_t comp_size = 0;
  size_t offset = 0;
};

static gboolean
gst_nv_comp_video_dec_parse_header (GstNvCompVideoDec * self,
    const guint8 * data, gsize size,
    size_t &uncompressed_chunk_size, size_t &max_compressed_chunk_size,
    size_t &batch_size, std::vector < ChunkData > &compressed_chunks)
{
  guint32 val;
  const guint8 *ptr = data;
  gsize remaining = size;

  if (size <= GST_NV_COMP_HEADER_MIN_SIZE) {
    GST_ERROR_OBJECT (self, "Too small size");
    return FALSE;
  }

  val = GST_READ_UINT32_LE (ptr);
  if (val != GST_NV_COMP_HEADER_VERSION) {
    GST_ERROR_OBJECT (self, "Invalid version");
    return FALSE;
  }
  ptr += sizeof (guint32);
  remaining -= sizeof (guint32);

  uncompressed_chunk_size = GST_READ_UINT32_LE (ptr);
  ptr += sizeof (guint32);
  remaining -= sizeof (guint32);

  max_compressed_chunk_size = GST_READ_UINT32_LE (ptr);
  ptr += sizeof (guint32);
  remaining -= sizeof (guint32);

  batch_size = GST_READ_UINT32_LE (ptr);
  ptr += sizeof (guint32);
  remaining -= sizeof (guint32);

  compressed_chunks.resize (batch_size);
  size_t total_compressed_size = 0;
  for (size_t i = 0; i < batch_size; i++) {
    if (remaining < sizeof (guint32))
      return FALSE;

    compressed_chunks[i].uncomp_size = GST_READ_UINT32_LE (ptr);
    ptr += sizeof (guint32);
    remaining -= sizeof (guint32);

    if (remaining < sizeof (guint32))
      return FALSE;

    compressed_chunks[i].comp_size = GST_READ_UINT32_LE (ptr);
    total_compressed_size += compressed_chunks[i].comp_size;

    ptr += sizeof (guint32);
    remaining -= sizeof (guint32);
  }

  if (remaining != total_compressed_size) {
    GST_ERROR_OBJECT (self, "Size mismatch, remaining: %" G_GSIZE_FORMAT
        ", total compressed: %" G_GSIZE_FORMAT, remaining,
        total_compressed_size);
    return FALSE;
  }

  for (size_t i = 0; i < batch_size; i++) {
    compressed_chunks[i].offset = ptr - data;
    ptr += compressed_chunks[i].comp_size;
  }

  return TRUE;
}

static GstFlowReturn
gst_nv_comp_video_dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  auto self = GST_NV_COMP_VIDEO_DEC (decoder);
  auto priv = self->priv;
  CUstream stream = nullptr;
  GstVideoFrame vframe;
  GstMapInfo map_info;
  CUresult cuda_ret;
  gboolean need_copy = TRUE;
  GstMemory *mem;
  nvcompStatus_t status;
  auto task = priv->task;
  GstFlowReturn ret;

  if (!priv->ctx || !priv->task) {
    GST_ERROR_OBJECT (self, "Context was not configured");
    goto error;
  }

  ret = gst_video_decoder_allocate_output_frame (decoder, frame);
  if (ret != GST_FLOW_OK) {
    gst_video_decoder_release_frame (decoder, frame);
    return ret;
  }

  if (!gst_cuda_context_push (priv->ctx)) {
    GST_ERROR_OBJECT (self, "Couldn't push context");
    goto error;
  }

  stream = gst_cuda_stream_get_handle (priv->stream);

  if (!gst_buffer_map (frame->input_buffer, &map_info, GST_MAP_READ)) {
    GST_ERROR_OBJECT (self, "Couldn't map input buffer");
    gst_cuda_context_pop (nullptr);
    goto error;
  }

  if (priv->batched) {
    g_assert (priv->batched_decomp);

    /* Parse custom header */
    size_t uncompressed_chunk_size;
    size_t max_compressed_chunk_size;
    size_t batch_size;
    std::vector < ChunkData > compressed_chunks;
    guint8 *mapped_data = map_info.data;
    uint8_t *uncompressed;
    if (!gst_nv_comp_video_dec_parse_header (self, mapped_data,
            map_info.size, uncompressed_chunk_size, max_compressed_chunk_size,
            batch_size, compressed_chunks)) {
      gst_buffer_unmap (frame->input_buffer, &map_info);
      gst_cuda_context_pop (nullptr);
      goto error;
    }

    GST_LOG_OBJECT (self, "batch size %" G_GSIZE_FORMAT
        ", uncompressed-chunk-size %" G_GSIZE_FORMAT
        ", compressed-chunk-size %" G_GSIZE_FORMAT,
        batch_size, uncompressed_chunk_size, max_compressed_chunk_size);

    if (task->batch_size < batch_size ||
        task->max_uncompressed_chunk_size < uncompressed_chunk_size ||
        task->max_compressed_chunk_size < max_compressed_chunk_size) {
      task->clear_resource ();
    }

    if (task->batch_size == 0) {
      size_t temp_size = 0;

      GST_DEBUG_OBJECT (self, "Allocating resource");

      status = priv->batched_decomp->get_temp_size (batch_size,
          uncompressed_chunk_size, &temp_size);
      if (status != nvcompSuccess) {
        GST_ERROR_OBJECT (self, "Couldn't get temp size");
        gst_buffer_unmap (frame->input_buffer, &map_info);
        gst_cuda_context_pop (nullptr);
        goto error;
      }

      if (!task->allocate_batched (batch_size,
              max_compressed_chunk_size, uncompressed_chunk_size, temp_size)) {
        GST_ERROR_OBJECT (self, "Couldn't allocate resource");
        gst_buffer_unmap (frame->input_buffer, &map_info);
        gst_cuda_context_pop (nullptr);
        goto error;
      }
    }

    for (size_t i = 0; i < batch_size; i++) {
      memcpy (task->host_compressed + (i * task->max_compressed_chunk_size),
          mapped_data + compressed_chunks[i].offset,
          compressed_chunks[i].comp_size);
      task->host_compressed_bytes[i] = compressed_chunks[i].comp_size;
    }
    gst_buffer_unmap (frame->input_buffer, &map_info);

    for (size_t i = 0; i < batch_size; i++) {
      GST_LOG_OBJECT (self, "Uploading chunk %" G_GSIZE_FORMAT
          ", size %" G_GSIZE_FORMAT, i, compressed_chunks[i].comp_size);
      auto offset = i * task->max_compressed_chunk_size;

      cuda_ret = CuMemcpyHtoDAsync ((CUdeviceptr)
          (task->device_compressed + offset),
          task->host_compressed + offset,
          compressed_chunks[i].comp_size, stream);
      if (!gst_cuda_result (cuda_ret)) {
        gst_cuda_context_pop (nullptr);
        goto error;
      }
    }

    cuda_ret = CuMemcpyHtoDAsync ((CUdeviceptr) task->device_compressed_bytes,
        task->host_compressed_bytes, sizeof (size_t) * batch_size, stream);
    if (!gst_cuda_result (cuda_ret)) {
      gst_cuda_context_pop (nullptr);
      goto error;
    }

    status = priv->batched_decomp->decompress (task->device_compressed_ptrs,
        task->device_compressed_bytes, task->device_uncompressed_bytes,
        task->device_actual_uncompressed_bytes, batch_size,
        task->temp_ptr, task->temp_size, task->device_uncompressed_ptrs,
        task->device_statuses, (cudaStream_t) stream);
    if (status != nvcompSuccess) {
      GST_ERROR_OBJECT (self, "Couldn't decompress stream, status: %d", status);
      gst_cuda_context_pop (nullptr);
      goto error;
    }

    uncompressed = task->device_uncompressed;
    for (size_t i = 0; i < batch_size; i++) {
      auto size = compressed_chunks[i].uncomp_size;
      cuda_ret = CuMemcpyDtoDAsync ((CUdeviceptr) uncompressed,
          (CUdeviceptr) task->host_uncompressed_ptrs[i], size, stream);

      if (!gst_cuda_result (cuda_ret)) {
        gst_cuda_context_pop (nullptr);
        goto error;
      }
      uncompressed += size;
    }
  } else {
    if (task->compressed_alloc_size < map_info.size) {
      if (task->device_compressed)
        CuMemFree ((CUdeviceptr) task->device_compressed);
      task->device_compressed = nullptr;

      if (task->host_compressed)
        CuMemFreeHost (task->host_compressed);
      task->host_compressed = nullptr;

      task->compressed_alloc_size = GST_ROUND_UP_128 (map_info.size);
      auto cuda_ret = CuMemAlloc ((CUdeviceptr *) & task->device_compressed,
          task->compressed_alloc_size);
      if (!gst_cuda_result (cuda_ret)) {
        gst_buffer_unmap (frame->input_buffer, &map_info);
        gst_cuda_context_pop (nullptr);
        goto error;
      }

      cuda_ret = CuMemAllocHost ((void **) &task->host_compressed,
          task->compressed_alloc_size);
      if (!gst_cuda_result (cuda_ret)) {
        gst_buffer_unmap (frame->input_buffer, &map_info);
        gst_cuda_context_pop (nullptr);
        goto error;
      }
    }

    memcpy (task->host_compressed, map_info.data, map_info.size);

    cuda_ret = CuMemcpyHtoDAsync ((CUdeviceptr) task->device_compressed,
        task->host_compressed, map_info.size, stream);
    gst_buffer_unmap (frame->input_buffer, &map_info);

    if (!gst_cuda_result (cuda_ret)) {
      GST_ERROR_OBJECT (self, "Couldn't copy compressed memory");
      gst_cuda_context_pop (nullptr);
      goto error;
    }

    if (!priv->manager) {
      priv->manager = create_manager (task->device_compressed,
          (cudaStream_t) stream);
    }

    {
      auto config =
          priv->manager->configure_decompression (task->device_compressed);
      if (config.decomp_data_size != priv->info.size) {
        GST_ERROR_OBJECT (self, "size mismatch, expected %" G_GSIZE_FORMAT
            ", required %" G_GSIZE_FORMAT, priv->info.size,
            config.decomp_data_size);
        gst_cuda_context_pop (nullptr);
        goto error;
      }

      priv->manager->decompress (task->device_uncompressed,
          task->device_compressed, config);
    }
  }

  mem = gst_buffer_peek_memory (frame->output_buffer, 0);
#ifdef HAVE_GST_GL
  if (priv->gl_interop && gst_buffer_n_memory (frame->output_buffer) ==
      GST_VIDEO_INFO_N_PLANES (&priv->info)) {
    GLInteropData interop_data;
    interop_data.self = self;
    interop_data.buffer = frame->output_buffer;
    interop_data.ret = FALSE;

    auto gl_mem = (GstGLMemory *) mem;
    gst_gl_context_thread_add (gl_mem->mem.context,
        (GstGLContextThreadFunc) gst_nv_comp_video_dec_download_gl,
        &interop_data);
    if (interop_data.ret) {
      need_copy = FALSE;
      GST_TRACE_OBJECT (self, "CUDA -> GL copy done");
    } else {
      priv->gl_interop = FALSE;
    }
  }
#endif

  if (need_copy) {
    GstMapFlags map_flags = GST_MAP_WRITE;
    gboolean device_copy = FALSE;
    gboolean do_sync = TRUE;
    if (gst_is_cuda_memory (mem)) {
      auto cmem = GST_CUDA_MEMORY_CAST (mem);
      if (cmem->context == priv->ctx) {
        map_flags = (GstMapFlags) (GST_MAP_WRITE | GST_MAP_CUDA);
        device_copy = TRUE;
        auto mem_stream = gst_cuda_memory_get_stream (cmem);
        if (mem_stream && mem_stream == priv->stream)
          do_sync = FALSE;
      }
    }

    if (!device_copy) {
      cuda_ret = CuMemcpyDtoHAsync (task->host_uncompressed,
          (CUdeviceptr) task->device_uncompressed, priv->info.size, stream);
      if (!gst_cuda_result (cuda_ret)) {
        GST_ERROR_OBJECT (self, "Couldn't download image");
        gst_cuda_context_pop (nullptr);
        goto error;
      }
      CuStreamSynchronize (stream);
      do_sync = FALSE;
    }

    gst_video_frame_map (&vframe, &priv->info, frame->output_buffer, map_flags);
    gst_nv_comp_video_dec_download (self, &vframe, stream, device_copy);
    if (do_sync)
      CuStreamSynchronize (stream);
    gst_video_frame_unmap (&vframe);
  }
  gst_cuda_context_pop (nullptr);

  return gst_video_decoder_finish_frame (decoder, frame);

error:
  gst_video_decoder_release_frame (decoder, frame);
  return GST_FLOW_ERROR;
}
