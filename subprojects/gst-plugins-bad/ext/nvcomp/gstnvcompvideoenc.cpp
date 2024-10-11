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

#include "gstnvcompvideoenc.h"
#ifdef HAVE_GST_GL
#include <gst/gl/gl.h>
#include <gst/gl/gstglfuncs.h>
#endif

#include <nvcomp/ans.hpp>
#include <nvcomp/bitcomp.hpp>
#include <nvcomp/cascaded.hpp>
#include <nvcomp/deflate.hpp>
#include <nvcomp/gdeflate.hpp>
#include <nvcomp/lz4.hpp>
#include <nvcomp/snappy.hpp>
#include <nvcomp/zstd.hpp>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <string>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_nv_comp_video_enc_debug);
#define GST_CAT_DEFAULT gst_nv_comp_video_enc_debug

#ifdef HAVE_GST_GL
#define SINK_CAPS \
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY, \
        GST_VIDEO_FORMATS_ALL) ";" \
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_GL_MEMORY, \
        GST_VIDEO_FORMATS_ALL) ";" \
    GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS_ALL)
#else
#define SINK_CAPS \
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY, \
        GST_VIDEO_FORMATS_ALL) ";" \
    GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS_ALL)
#endif

static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SINK_CAPS));

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-nvcomp; video/x-nvcomp-lz4; "
        "video/x-nvcomp-snappy; video/x-nvcomp-gdeflate; "
        "video/x-nvcomp-deflate; video/x-nvcomp-zstd; video/x-nvcomp-cascaded; "
        "video/x-nvcomp-bitcomp; video/x-nvcomp-ans"));

enum GstNvCompDataType
{
  GST_NV_COMP_DATA_TYPE_DEFAULT = -1,
  GST_NV_COMP_DATA_TYPE_CHAR = NVCOMP_TYPE_CHAR,
  GST_NV_COMP_DATA_TYPE_UCHAR = NVCOMP_TYPE_UCHAR,
  GST_NV_COMP_DATA_TYPE_SHORT = NVCOMP_TYPE_SHORT,
  GST_NV_COMP_DATA_TYPE_USHORT = NVCOMP_TYPE_USHORT,
  GST_NV_COMP_DATA_TYPE_INT = NVCOMP_TYPE_INT,
  GST_NV_COMP_DATA_TYPE_UINT = NVCOMP_TYPE_UINT,
  GST_NV_COMP_DATA_TYPE_LONGLONG = NVCOMP_TYPE_LONGLONG,
  GST_NV_COMP_DATA_TYPE_ULONGLONG = NVCOMP_TYPE_ULONGLONG,
  GST_NV_COMP_DATA_TYPE_BITS = NVCOMP_TYPE_BITS,
};

#define GST_TYPE_NV_COMP_DATA_TYPE (gst_nv_comp_data_type_type())
static GType
gst_nv_comp_data_type_type (void)
{
  static GType data_type = 0;
  static std::once_flag once;
  static const GEnumValue types[] = {
    {GST_NV_COMP_DATA_TYPE_DEFAULT, "Default", "default"},
    {GST_NV_COMP_DATA_TYPE_CHAR, "CHAR", "char"},
    {GST_NV_COMP_DATA_TYPE_UCHAR, "UCHAR", "uchar"},
    {GST_NV_COMP_DATA_TYPE_SHORT, "SHORT", "short"},
    {GST_NV_COMP_DATA_TYPE_USHORT, "USHORT", "ushort"},
    {GST_NV_COMP_DATA_TYPE_INT, "INT", "int"},
    {GST_NV_COMP_DATA_TYPE_UINT, "UINT", "uint"},
    {GST_NV_COMP_DATA_TYPE_LONGLONG, "LONGLONG", "longlong"},
    {GST_NV_COMP_DATA_TYPE_ULONGLONG, "ULONGLONG", "ulonglong"},
    {GST_NV_COMP_DATA_TYPE_BITS, "BITS", "bits"},
    {0, nullptr, nullptr},
  };

  std::call_once (once,[&] {
        data_type = g_enum_register_static ("GstNvCompDataType", types);
      });

  return data_type;
}

enum GstNvCompDeflateAlgo
{
  GST_NV_COMP_DEFLATE_HIGH_THROUGHPUT,
  GST_NV_COMP_DEFLATE_LOW_THROUGHPUT,
  GST_NV_COMP_DEFLATE_HIGHEST_THROUGHPUT,
};

#define GST_TYPE_NV_COMP_DEFLATE_ALGO (gst_nv_comp_deflate_algo_get_type())
static GType
gst_nv_comp_deflate_algo_get_type (void)
{
  static GType algo_type = 0;
  static std::once_flag once;
  static const GEnumValue algo[] = {
    {GST_NV_COMP_DEFLATE_HIGH_THROUGHPUT,
        "High throughput, low compression ratio", "high-throughput"},
    {GST_NV_COMP_DEFLATE_LOW_THROUGHPUT,
        "Low throughput, high compression ratio", "low-throughput"},
    {GST_NV_COMP_DEFLATE_HIGHEST_THROUGHPUT,
        "Highest throughput, entropy-only compression", "highest-throughput"},
    {0, nullptr, nullptr},
  };

  std::call_once (once,[&] {
        algo_type = g_enum_register_static ("GstNvCompDeflateAlgo", algo);
      });

  return algo_type;
}

enum GstNvCompBitcompAlgo
{
  GST_NV_COMP_BITCOMP_DEFAULT,
  GST_NV_COMP_BITCOMP_SPARSE,
};

#define GST_TYPE_NV_COMP_BITCOMP_ALGO (gst_nv_comp_bitcomp_algo_get_type())
static GType
gst_nv_comp_bitcomp_algo_get_type (void)
{
  static GType algo_type = 0;
  static std::once_flag once;
  static const GEnumValue algo[] = {
    {GST_NV_COMP_BITCOMP_DEFAULT, "Default", "default"},
    {GST_NV_COMP_BITCOMP_SPARSE, "Sparse", "sparse"},
    {0, nullptr, nullptr},
  };

  std::call_once (once,[&] {
        algo_type = g_enum_register_static ("GstNvCompBitcompAlgo", algo);
      });

  return algo_type;
}

enum
{
  PROP_0,
  PROP_METHOD,
  PROP_DEFLATE_ALGO,
  PROP_BITCOMP_ALGO,
  PROP_DATA_TYPE,
  PROP_CHUNK_SIZE,
  PROP_ASYNC_DEPTH,
  PROP_BATCHED,
};

#define DEFAULT_METHOD GST_NV_COMP_BITCOMP
#define DEFAULT_DEFLATE_ALGO GST_NV_COMP_DEFLATE_HIGH_THROUGHPUT
#define DEFAULT_BITCOMP_ALGO GST_NV_COMP_BITCOMP_SPARSE
#define DEFAULT_DATA_TYPE GST_NV_COMP_DATA_TYPE_DEFAULT
#define DEFAULT_CHUNK_SIZE 0
#define DEFAULT_BATCHED TRUE
#define DEFAULT_ASYNC_DEPTH 2

/* *INDENT-OFF* */
using namespace nvcomp;

struct EncoderTask
{
  ~EncoderTask ()
  {
    if (ctx) {
      gst_cuda_context_push (ctx);
      if (event)
        CuEventDestroy (event);
      if (device_uncompressed)
        CuMemFree ((CUdeviceptr) device_uncompressed);
      if (host_uncompressed)
        CuMemFreeHost (host_uncompressed);
      if (device_compressed)
        CuMemFree ((CUdeviceptr) device_compressed);
      if (host_compressed)
        CuMemFreeHost (host_compressed);
      if (device_uncompressed_bytes)
        CuMemFree ((CUdeviceptr) device_uncompressed_bytes);
      if (device_uncompressed_ptrs)
        CuMemFree ((CUdeviceptr) device_uncompressed_ptrs);
      if (device_compressed_bytes)
        CuMemFree ((CUdeviceptr) device_compressed_bytes);
      if (host_uncompressed_bytes)
        CuMemFreeHost (host_uncompressed_bytes);
      if (host_uncompressed_ptrs)
        CuMemFreeHost (host_uncompressed_ptrs);
      if (device_compressed_ptrs)
        CuMemFree ((CUdeviceptr) device_compressed_ptrs);
      if (host_compressed_bytes)
        CuMemFreeHost (host_compressed_bytes);
      if (host_compressed_ptrs)
        CuMemFreeHost (host_compressed_ptrs);
      if (temp_ptr)
        CuMemFree ((CUdeviceptr) temp_ptr);

      gst_cuda_context_pop (nullptr);
      gst_object_unref (ctx);
    }
  }

  GstCudaContext *ctx = nullptr;
  CUevent event = nullptr;
  uint8_t *device_uncompressed = nullptr;
  uint8_t *host_uncompressed = nullptr;

  uint8_t *device_compressed = nullptr;
  uint8_t *host_compressed = nullptr;

  size_t *device_uncompressed_bytes = nullptr;
  void **device_uncompressed_ptrs = nullptr;

  size_t *host_uncompressed_bytes = nullptr;
  void **host_uncompressed_ptrs = nullptr;

  size_t *device_compressed_bytes = nullptr;
  void **device_compressed_ptrs = nullptr;

  size_t *host_compressed_bytes = nullptr;
  void **host_compressed_ptrs = nullptr;

  void *temp_ptr = nullptr;
  size_t temp_size = 0;

  size_t compressed_size = 0;

  gboolean batched;
  size_t batch_size;
  size_t chunk_size;
  size_t max_output_chunk_size;
  size_t compressed_alloc_size;
};

struct BatchedCompBase
{
  virtual nvcompStatus_t get_temp_size(
      size_t batch_size,
      size_t max_uncompressed_chunk_bytes,
      size_t * temp_bytes) = 0;

  virtual nvcompStatus_t get_max_compressed_chunk_size(
      size_t max_uncompressed_chunk_bytes,
      size_t * max_compressed_bytes) = 0;

  virtual nvcompStatus_t compress(
      void **device_uncompressed_ptrs,
      size_t *device_uncompressed_bytes,
      size_t max_uncompressed_chunk_bytes,
      size_t batch_size,
      void *device_temp_ptr,
      size_t temp_bytes,
      void **device_compressed_ptrs,
      size_t *device_compressed_bytes,
      cudaStream_t stream) = 0;
};

template <typename FormatOptT, auto T, auto O, auto C>
class BatchedComp : public BatchedCompBase
{
public:
  BatchedComp (const FormatOptT & opt) : opts_(opt) {}

  nvcompStatus_t get_temp_size(
      size_t batch_size,
      size_t max_uncompressed_chunk_bytes,
      size_t * temp_bytes)
  {
    return T (batch_size, max_uncompressed_chunk_bytes, opts_, temp_bytes);
  }

  nvcompStatus_t get_max_compressed_chunk_size(
      size_t max_uncompressed_chunk_bytes,
      size_t * max_compressed_bytes)
  {
    return O (max_uncompressed_chunk_bytes, opts_, max_compressed_bytes);
  }

  nvcompStatus_t compress(
      void **device_uncompressed_ptrs,
      size_t *device_uncompressed_bytes,
      size_t max_uncompressed_chunk_bytes,
      size_t batch_size,
      void *device_temp_ptr,
      size_t temp_bytes,
      void **device_compressed_ptrs,
      size_t *device_compressed_bytes,
      cudaStream_t stream)
  {
    return C (device_uncompressed_ptrs, device_uncompressed_bytes,
        max_uncompressed_chunk_bytes, batch_size, device_temp_ptr, temp_bytes,
        device_compressed_ptrs, device_compressed_bytes, opts_, stream);
  }

private:
  FormatOptT opts_;
};

struct GstNvCompVideoEncPrivate
{
  GstCudaContext *ctx = nullptr;
  GstCudaStream *stream = nullptr;

#ifdef HAVE_GST_GL
  GstGLDisplay *gl_display = nullptr;
  GstGLContext *gl_context = nullptr;
  GstGLContext *other_gl_context = nullptr;
#endif

  GstBufferPool *pool = nullptr;

  GstVideoCodecState *state = nullptr;
  std::shared_ptr<nvcompManagerBase> manager;
  std::shared_ptr<CompressionConfig> config;
  std::shared_ptr<BatchedCompBase> batched_comp;

  gboolean gl_interop = FALSE;

  std::mutex lock;
  std::mutex input_lock;
  std::condition_variable input_cond;
  std::mutex output_lock;
  std::condition_variable output_cond;

  std::queue<std::shared_ptr<EncoderTask>> input_task_queue;
  std::queue<std::shared_ptr<EncoderTask>> output_task_queue;
  std::shared_ptr<EncoderTask> cur_task;
  GThread *encode_thread = nullptr;
  std::atomic<GstFlowReturn> last_flow = { GST_FLOW_OK };

  GstNvCompMethod method = DEFAULT_METHOD;
  GstNvCompDeflateAlgo deflate_algo = DEFAULT_DEFLATE_ALGO;
  GstNvCompBitcompAlgo bitcomp_algo = DEFAULT_BITCOMP_ALGO;
  GstNvCompDataType data_type = DEFAULT_DATA_TYPE;
  guint chunk_size = DEFAULT_CHUNK_SIZE;
  gboolean batched = DEFAULT_BATCHED;
  guint async_depth = DEFAULT_ASYNC_DEPTH;
};
/* *INDENT-ON* */

struct _GstNvCompVideoEnc
{
  GstVideoEncoder parent;
  GstNvCompVideoEncPrivate *priv;
};

static void gst_nv_comp_video_enc_finalize (GObject * object);
static void gst_nv_comp_video_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_nv_comp_video_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_nv_comp_video_enc_set_context (GstElement * element,
    GstContext * context);

static gboolean gst_nv_comp_video_enc_open (GstVideoEncoder * encoder);
static gboolean gst_nv_comp_video_enc_close (GstVideoEncoder * encoder);
static gboolean gst_nv_comp_video_enc_stop (GstVideoEncoder * encoder);
static gboolean gst_nv_comp_video_enc_flush (GstVideoEncoder * encoder);
static GstFlowReturn gst_nv_comp_video_enc_finish (GstVideoEncoder * encoder);
static gboolean gst_nv_comp_video_enc_sink_query (GstVideoEncoder * encoder,
    GstQuery * query);
static gboolean gst_nv_comp_video_enc_src_query (GstVideoEncoder * encoder,
    GstQuery * query);
static gboolean
gst_nv_comp_video_enc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query);
static gboolean gst_nv_comp_video_enc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state);
static GstFlowReturn
gst_nv_comp_video_enc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame);

#define gst_nv_comp_video_enc_parent_class parent_class
G_DEFINE_TYPE (GstNvCompVideoEnc,
    gst_nv_comp_video_enc, GST_TYPE_VIDEO_ENCODER);

static void
gst_nv_comp_video_enc_class_init (GstNvCompVideoEncClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto element_class = GST_ELEMENT_CLASS (klass);
  auto encoder_class = GST_VIDEO_ENCODER_CLASS (klass);

  object_class->finalize = gst_nv_comp_video_enc_finalize;
  object_class->set_property = gst_nv_comp_video_enc_set_property;
  object_class->get_property = gst_nv_comp_video_enc_get_property;

  g_object_class_install_property (object_class, PROP_METHOD,
      g_param_spec_enum ("method", "Method",
          "Compression method",
          GST_TYPE_NV_COMP_METHOD, DEFAULT_METHOD,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_DEFLATE_ALGO,
      g_param_spec_enum ("deflate-algo", "Deflate Algo",
          "Algorithm to use for deflate and gdeflate methods",
          GST_TYPE_NV_COMP_DEFLATE_ALGO, DEFAULT_DEFLATE_ALGO,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_BITCOMP_ALGO,
      g_param_spec_enum ("bitcomp-algo", "Bitcomp Algo",
          "Algorithm to use for bitcomp method",
          GST_TYPE_NV_COMP_BITCOMP_ALGO, DEFAULT_BITCOMP_ALGO,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_DATA_TYPE,
      g_param_spec_enum ("data-type", "Data Type",
          "Compression data type",
          GST_TYPE_NV_COMP_DATA_TYPE, DEFAULT_DATA_TYPE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_CHUNK_SIZE,
      g_param_spec_uint ("chunk-size", "Chunk Size",
          "Uncompressed chunk size for batched compression (0 = default)",
          0, G_MAXINT32, DEFAULT_CHUNK_SIZE,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_BATCHED,
      g_param_spec_boolean ("batched", "Batched",
          "Use low-level C API for batched operation", DEFAULT_BATCHED,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_ASYNC_DEPTH,
      g_param_spec_uint ("async-depth", "Async Depth",
          "Internal resource pool size for threaded encoding",
          1, 4, DEFAULT_ASYNC_DEPTH,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_static_metadata (element_class,
      "nvCOMP Video Encoder", "Encoder/Video/Hardware",
      "Lossless video compression element based on nvCOMP library",
      "Seungha Yang <seungha@centricular.com>");

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_nv_comp_video_enc_set_context);

  encoder_class->open = GST_DEBUG_FUNCPTR (gst_nv_comp_video_enc_open);
  encoder_class->close = GST_DEBUG_FUNCPTR (gst_nv_comp_video_enc_close);
  encoder_class->stop = GST_DEBUG_FUNCPTR (gst_nv_comp_video_enc_stop);
  encoder_class->flush = GST_DEBUG_FUNCPTR (gst_nv_comp_video_enc_flush);
  encoder_class->finish = GST_DEBUG_FUNCPTR (gst_nv_comp_video_enc_finish);
  encoder_class->sink_query =
      GST_DEBUG_FUNCPTR (gst_nv_comp_video_enc_sink_query);
  encoder_class->src_query =
      GST_DEBUG_FUNCPTR (gst_nv_comp_video_enc_src_query);
  encoder_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_nv_comp_video_enc_propose_allocation);
  encoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_nv_comp_video_enc_set_format);
  encoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_nv_comp_video_enc_handle_frame);

  GST_DEBUG_CATEGORY_INIT (gst_nv_comp_video_enc_debug,
      "nvcompvideoenc", 0, "nvcompvideoenc");
}

static void
gst_nv_comp_video_enc_init (GstNvCompVideoEnc * self)
{
  self->priv = new GstNvCompVideoEncPrivate ();
}

static void
gst_nv_comp_video_enc_finalize (GObject * object)
{
  auto self = GST_NV_COMP_VIDEO_ENC (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_nv_comp_video_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  auto self = GST_NV_COMP_VIDEO_ENC (object);
  auto priv = self->priv;

  std::lock_guard < std::mutex > lk (priv->lock);

  switch (prop_id) {
    case PROP_METHOD:
      priv->method = (GstNvCompMethod) g_value_get_enum (value);
      break;
    case PROP_DEFLATE_ALGO:
      priv->deflate_algo = (GstNvCompDeflateAlgo) g_value_get_enum (value);
      break;
    case PROP_BITCOMP_ALGO:
      priv->bitcomp_algo = (GstNvCompBitcompAlgo) g_value_get_enum (value);
      break;
    case PROP_DATA_TYPE:
      priv->data_type = (GstNvCompDataType) g_value_get_enum (value);
      break;
    case PROP_CHUNK_SIZE:
      priv->chunk_size = g_value_get_uint (value);
      break;
    case PROP_BATCHED:
      priv->batched = g_value_get_boolean (value);
      break;
    case PROP_ASYNC_DEPTH:
      priv->async_depth = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_nv_comp_video_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  auto self = GST_NV_COMP_VIDEO_ENC (object);
  auto priv = self->priv;

  std::lock_guard < std::mutex > lk (priv->lock);

  switch (prop_id) {
    case PROP_METHOD:
      g_value_set_enum (value, priv->method);
      break;
    case PROP_DEFLATE_ALGO:
      g_value_set_enum (value, priv->deflate_algo);
      break;
    case PROP_BITCOMP_ALGO:
      g_value_set_enum (value, priv->bitcomp_algo);
      break;
    case PROP_DATA_TYPE:
      g_value_set_enum (value, priv->data_type);
      break;
    case PROP_CHUNK_SIZE:
      g_value_set_uint (value, priv->chunk_size);
      break;
    case PROP_BATCHED:
      g_value_set_boolean (value, priv->batched);
      break;
    case PROP_ASYNC_DEPTH:
      g_value_set_uint (value, priv->async_depth);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_nv_comp_video_enc_set_context (GstElement * element, GstContext * context)
{
  auto self = GST_NV_COMP_VIDEO_ENC (element);
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
gst_nv_comp_video_enc_open (GstVideoEncoder * encoder)
{
  auto self = GST_NV_COMP_VIDEO_ENC (encoder);
  auto priv = self->priv;

  if (!gst_cuda_ensure_element_context (GST_ELEMENT_CAST (encoder),
          -1, &priv->ctx)) {
    GST_ERROR_OBJECT (self, "Couldn't get cuda context");
    return FALSE;
  }

  priv->stream = gst_cuda_stream_new (priv->ctx);

  return TRUE;
}

static gboolean
gst_nv_comp_video_enc_close (GstVideoEncoder * encoder)
{
  auto self = GST_NV_COMP_VIDEO_ENC (encoder);
  auto priv = self->priv;

  gst_clear_cuda_stream (&priv->stream);
  gst_clear_object (&priv->ctx);

#ifdef HAVE_GST_GL
  gst_clear_object (&priv->other_gl_context);
  gst_clear_object (&priv->gl_context);
  gst_clear_object (&priv->gl_context);
#endif

  return TRUE;
}

static void
gst_nv_comp_video_enc_drain (GstNvCompVideoEnc * self, gboolean locked)
{
  auto priv = self->priv;
  if (!priv->encode_thread)
    return;

  if (locked)
    GST_VIDEO_ENCODER_STREAM_UNLOCK (self);

  {
    std::lock_guard < std::mutex > lk (priv->output_lock);
    priv->output_task_queue.push (nullptr);
    priv->output_cond.notify_all ();
  }

  g_clear_pointer (&priv->encode_thread, g_thread_join);

  if (locked)
    GST_VIDEO_ENCODER_STREAM_LOCK (self);

  priv->last_flow = GST_FLOW_OK;
}

static gboolean
gst_nv_comp_video_enc_stop (GstVideoEncoder * encoder)
{
  auto self = GST_NV_COMP_VIDEO_ENC (encoder);
  auto priv = self->priv;

  gst_nv_comp_video_enc_drain (self, FALSE);

  if (priv->ctx) {
    gst_cuda_context_push (priv->ctx);
    priv->manager = nullptr;
    priv->cur_task = nullptr;
    priv->input_task_queue = { };
    priv->output_task_queue = { };

    gst_cuda_context_pop (nullptr);
  }

  g_clear_pointer (&priv->state, gst_video_codec_state_unref);

  if (priv->pool) {
    gst_buffer_pool_set_active (priv->pool, FALSE);
    gst_clear_object (&priv->pool);
  }

  return TRUE;
}

static gboolean
gst_nv_comp_video_enc_flush (GstVideoEncoder * encoder)
{
  auto self = GST_NV_COMP_VIDEO_ENC (encoder);

  gst_nv_comp_video_enc_drain (self, TRUE);

  return TRUE;
}

static GstFlowReturn
gst_nv_comp_video_enc_finish (GstVideoEncoder * encoder)
{
  auto self = GST_NV_COMP_VIDEO_ENC (encoder);

  gst_nv_comp_video_enc_drain (self, TRUE);

  return GST_FLOW_OK;
}

static gboolean
gst_nv_comp_video_enc_handle_context_query (GstNvCompVideoEnc * self,
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
gst_nv_comp_video_enc_sink_query (GstVideoEncoder * encoder, GstQuery * query)
{
  auto self = GST_NV_COMP_VIDEO_ENC (encoder);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      if (gst_nv_comp_video_enc_handle_context_query (self, query))
        return TRUE;
      break;
    default:
      break;
  }

  return GST_VIDEO_ENCODER_CLASS (parent_class)->sink_query (encoder, query);
}

static gboolean
gst_nv_comp_video_enc_src_query (GstVideoEncoder * encoder, GstQuery * query)
{
  auto self = GST_NV_COMP_VIDEO_ENC (encoder);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      if (gst_nv_comp_video_enc_handle_context_query (self, query))
        return TRUE;
      break;
    default:
      break;
  }

  return GST_VIDEO_ENCODER_CLASS (parent_class)->src_query (encoder, query);
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
gst_nv_comp_video_enc_ensure_gl_context (GstNvCompVideoEnc * self)
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
gst_nv_comp_video_enc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query)
{
  auto self = GST_NV_COMP_VIDEO_ENC (encoder);
  auto priv = self->priv;
  GstBufferPool *pool = nullptr;
  guint size;

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

  auto features = gst_caps_get_features (caps, 0);
  gboolean use_cuda_pool = FALSE;
  if (gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY)) {
    GST_DEBUG_OBJECT (self, "upstream support CUDA memory");
    pool = gst_cuda_buffer_pool_new (priv->ctx);
    use_cuda_pool = TRUE;
  }
#ifdef HAVE_GST_GL
  else if (gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_GL_MEMORY)) {
    if (!gst_nv_comp_video_enc_ensure_gl_context (self)) {
      priv->gl_interop = FALSE;
    } else {
      pool = gst_gl_buffer_pool_new (priv->gl_context);
    }
  }
#endif

  if (!pool)
    pool = gst_video_buffer_pool_new ();

  auto config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

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

  gst_query_add_allocation_pool (query, pool, size, 0, 0);
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, nullptr);
  gst_object_unref (pool);

  return TRUE;
}

static gboolean
gst_nv_comp_video_enc_alloc_task (GstNvCompVideoEnc * self, EncoderTask * task,
    gboolean batched, size_t uncompressed_size, size_t compressed_size,
    size_t batch_size, size_t chunk_size, size_t output_chunk_size,
    size_t temp_size)
{
  size_t alloc_size = sizeof (size_t) * batch_size;
  uint8_t *uncomp_data;
  uint8_t *comp_data;

  auto ret = CuEventCreate (&task->event,
      CU_EVENT_BLOCKING_SYNC | CU_EVENT_DISABLE_TIMING);
  if (!gst_cuda_result (ret))
    return FALSE;

  auto aligned_uncompressed_size = uncompressed_size;
  ret = CuMemAlloc ((CUdeviceptr *) & task->device_uncompressed,
      aligned_uncompressed_size);
  if (!gst_cuda_result (ret))
    return FALSE;

  ret = CuMemAllocHost ((void **) &task->host_uncompressed,
      aligned_uncompressed_size);
  if (!gst_cuda_result (ret))
    return FALSE;

  auto aligned_compressed_size = GST_ROUND_UP_8 (compressed_size);
  ret = CuMemAlloc ((CUdeviceptr *) & task->device_compressed,
      aligned_compressed_size);
  if (!gst_cuda_result (ret))
    return FALSE;

  ret = CuMemAllocHost ((void **) &task->host_compressed,
      aligned_compressed_size);
  if (!gst_cuda_result (ret))
    return FALSE;

  if (!batched)
    return TRUE;

  ret = CuMemAllocHost ((void **) &task->host_uncompressed_bytes, alloc_size);
  if (!gst_cuda_result (ret))
    return FALSE;

  ret = CuMemAllocHost ((void **) &task->host_uncompressed_ptrs, alloc_size);
  if (!gst_cuda_result (ret))
    return FALSE;

  for (size_t i = 0; i < batch_size; i++) {
    if (i + 1 < batch_size)
      task->host_uncompressed_bytes[i] = chunk_size;
    else
      task->host_uncompressed_bytes[i] = (uncompressed_size - (chunk_size * i));
  }

  ret = CuMemAlloc ((CUdeviceptr *) & task->device_uncompressed_bytes,
      alloc_size);
  if (!gst_cuda_result (ret))
    return FALSE;

  ret = CuMemAlloc ((CUdeviceptr *) & task->device_uncompressed_ptrs,
      alloc_size);
  if (!gst_cuda_result (ret))
    return FALSE;

  ret = CuMemAlloc ((CUdeviceptr *) & task->device_compressed_bytes,
      alloc_size);
  if (!gst_cuda_result (ret))
    return FALSE;

  ret = CuMemAlloc ((CUdeviceptr *) & task->device_compressed_ptrs, alloc_size);
  if (!gst_cuda_result (ret))
    return FALSE;

  ret = CuMemAllocHost ((void **) &task->host_compressed_bytes, alloc_size);
  if (!gst_cuda_result (ret))
    return FALSE;

  ret = CuMemAllocHost ((void **) &task->host_compressed_ptrs, alloc_size);
  if (!gst_cuda_result (ret))
    return FALSE;

  if (temp_size > 0) {
    ret = CuMemAlloc ((CUdeviceptr *) & task->temp_ptr, temp_size);
    if (!gst_cuda_result (ret))
      return FALSE;
  }

  task->temp_size = temp_size;

  uncomp_data = task->device_uncompressed;
  comp_data = task->device_compressed;
  for (size_t i = 0; i < batch_size; i++) {
    task->host_uncompressed_ptrs[i] = uncomp_data;
    uncomp_data += chunk_size;

    task->host_compressed_ptrs[i] = comp_data;
    comp_data += output_chunk_size;
  }

  ret = CuMemcpyHtoD ((CUdeviceptr) task->device_uncompressed_bytes,
      task->host_uncompressed_bytes, alloc_size);
  if (!gst_cuda_result (ret))
    return FALSE;

  ret = CuMemcpyHtoD ((CUdeviceptr) task->device_uncompressed_ptrs,
      task->host_uncompressed_ptrs, alloc_size);
  if (!gst_cuda_result (ret))
    return FALSE;

  ret = CuMemcpyHtoD ((CUdeviceptr) task->device_compressed_ptrs,
      task->host_compressed_ptrs, alloc_size);
  if (!gst_cuda_result (ret))
    return FALSE;

  task->batched = batched;
  task->batch_size = batch_size;
  task->chunk_size = chunk_size;
  task->max_output_chunk_size = output_chunk_size;
  task->compressed_alloc_size = aligned_compressed_size;

  return TRUE;
}

static gboolean
gst_nv_comp_video_enc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state)
{
  auto self = GST_NV_COMP_VIDEO_ENC (encoder);
  auto priv = self->priv;

  gst_nv_comp_video_enc_drain (self, TRUE);

  std::lock_guard < std::mutex > lk (priv->lock);

  if (!priv->ctx) {
    GST_ERROR_OBJECT (self, "CUDA context was not configured");
    return FALSE;
  }

  if (priv->pool) {
    gst_buffer_pool_set_active (priv->pool, FALSE);
    gst_clear_object (&priv->pool);
  }

  g_clear_pointer (&priv->state, gst_video_codec_state_unref);
  priv->state = gst_video_codec_state_ref (state);

  std::string mime_type = "video/x-nvcomp";

  if (!gst_cuda_context_push (priv->ctx)) {
    GST_ERROR_OBJECT (self, "Couldn't push context");
    return FALSE;
  }

  priv->gl_interop = FALSE;
#if HAVE_GST_GL
  auto features = gst_caps_get_features (state->caps, 0);
  if (gst_caps_features_contains (features, GST_CAPS_FEATURE_MEMORY_GL_MEMORY))
    priv->gl_interop = TRUE;
#endif

  priv->manager = nullptr;
  priv->config = nullptr;
  priv->batched_comp = nullptr;
  priv->input_task_queue = { };
  priv->output_task_queue = { };
  auto stream = (cudaStream_t) gst_cuda_stream_get_handle (priv->stream);
  guint device_id = 0;
  g_object_get (priv->ctx, "cuda-device-id", &device_id, nullptr);
  size_t chunk_size = priv->chunk_size;
  size_t batch_size = 0;

  switch (priv->method) {
    case GST_NV_COMP_LZ4:
    {
      nvcompBatchedLZ4Opts_t opts = nvcompBatchedLZ4DefaultOpts;
      if (priv->data_type != GST_NV_COMP_DATA_TYPE_DEFAULT)
        opts.data_type = (nvcompType_t) priv->data_type;

      if (chunk_size == 0)
        chunk_size = 65536;

      chunk_size = MAX (32768, chunk_size);
      chunk_size = GST_ROUND_UP_8 (chunk_size);
      chunk_size = MIN (chunk_size, nvcompLZ4CompressionMaxAllowedChunkSize);

      if (priv->batched) {
        batch_size = (state->info.size + chunk_size - 1) / chunk_size;

        priv->batched_comp =
            std::make_shared < BatchedComp < nvcompBatchedLZ4Opts_t,
            nvcompBatchedLZ4CompressGetTempSize,
            nvcompBatchedLZ4CompressGetMaxOutputChunkSize,
            nvcompBatchedLZ4CompressAsync >> (opts);
        mime_type = "video/x-nvcomp-lz4";
      } else {
        priv->manager = std::make_shared < LZ4Manager > (chunk_size,
            opts, stream, device_id);
      }
      GST_DEBUG_OBJECT (self, "Using LZ4");
      break;
    }
    case GST_NV_COMP_SNAPPY:
    {
      if (chunk_size == 0)
        chunk_size = 65536;

      chunk_size = MAX (32768, chunk_size);
      chunk_size = GST_ROUND_UP_8 (chunk_size);
      chunk_size = MIN (chunk_size, nvcompSnappyCompressionMaxAllowedChunkSize);

      if (priv->batched) {
        batch_size = (state->info.size + chunk_size - 1) / chunk_size;

        priv->batched_comp =
            std::make_shared < BatchedComp < nvcompBatchedSnappyOpts_t,
            nvcompBatchedSnappyCompressGetTempSize,
            nvcompBatchedSnappyCompressGetMaxOutputChunkSize,
            nvcompBatchedSnappyCompressAsync >>
            (nvcompBatchedSnappyDefaultOpts);
        mime_type = "video/x-nvcomp-snappy";
      } else {
        priv->manager = std::make_shared < SnappyManager > (chunk_size,
            nvcompBatchedSnappyDefaultOpts, stream, device_id);
      }
      GST_DEBUG_OBJECT (self, "Using SNAPPY");
      break;
    }
    case GST_NV_COMP_GDEFLATE:
    {
      nvcompBatchedGdeflateOpts_t opts;
      opts.algo = (int) priv->deflate_algo;

      if (chunk_size == 0)
        chunk_size = 65536;

      chunk_size = MAX (32768, chunk_size);
      chunk_size = GST_ROUND_UP_8 (chunk_size);
      chunk_size = MIN (chunk_size,
          nvcompGdeflateCompressionMaxAllowedChunkSize);


      if (priv->batched) {
        batch_size = (state->info.size + chunk_size - 1) / chunk_size;

        priv->batched_comp =
            std::make_shared < BatchedComp < nvcompBatchedGdeflateOpts_t,
            nvcompBatchedGdeflateCompressGetTempSize,
            nvcompBatchedGdeflateCompressGetMaxOutputChunkSize,
            nvcompBatchedGdeflateCompressAsync >> (opts);
        mime_type = "video/x-nvcomp-gdeflate";
      } else {
        priv->manager = std::make_shared < GdeflateManager > (chunk_size,
            opts, stream, device_id);
      }
      GST_DEBUG_OBJECT (self, "Using GDEFLATE");
      break;
    }
    case GST_NV_COMP_DEFLATE:
    {
      if (chunk_size == 0)
        chunk_size = 65536;

      chunk_size = MAX (32768, chunk_size);
      chunk_size = GST_ROUND_UP_8 (chunk_size);
      chunk_size = MIN (chunk_size,
          nvcompDeflateCompressionMaxAllowedChunkSize);

      nvcompBatchedDeflateOpts_t opts;
      opts.algo = (int) priv->deflate_algo;
      if (priv->batched) {
        batch_size = (state->info.size + chunk_size - 1) / chunk_size;

        priv->batched_comp =
            std::make_shared < BatchedComp < nvcompBatchedDeflateOpts_t,
            nvcompBatchedDeflateCompressGetTempSize,
            nvcompBatchedDeflateCompressGetMaxOutputChunkSize,
            nvcompBatchedDeflateCompressAsync >> (opts);
        mime_type = "video/x-nvcomp-deflate";
      } else {
        priv->manager = std::make_shared < DeflateManager > (chunk_size,
            opts, stream, device_id);
      }
      GST_DEBUG_OBJECT (self, "Using DEFLATE");
      break;
    }
    case GST_NV_COMP_ZSTD:
    {
      if (chunk_size == 0)
        chunk_size = 65536;

      chunk_size = MAX (32768, chunk_size);
      chunk_size = GST_ROUND_UP_8 (chunk_size);
      chunk_size = MIN (chunk_size, nvcompZstdCompressionMaxAllowedChunkSize);

      if (priv->batched) {
        batch_size = (state->info.size + chunk_size - 1) / chunk_size;

        priv->batched_comp =
            std::make_shared < BatchedComp < nvcompBatchedZstdOpts_t,
            nvcompBatchedZstdCompressGetTempSize,
            nvcompBatchedZstdCompressGetMaxOutputChunkSize,
            nvcompBatchedZstdCompressAsync >> (nvcompBatchedZstdDefaultOpts);
        mime_type = "video/x-nvcomp-zstd";
      } else {
        priv->manager = std::make_shared < ZstdManager > (chunk_size,
            nvcompBatchedZstdDefaultOpts, stream, device_id);
      }
      GST_DEBUG_OBJECT (self, "Using ZSTD");
      break;
    }
    case GST_NV_COMP_CASCADED:
    {
      if (chunk_size == 0)
        chunk_size = 4096;

      chunk_size = MAX (512, chunk_size);
      chunk_size = GST_ROUND_UP_8 (chunk_size);
      chunk_size = MIN (chunk_size, 16384);

      nvcompBatchedCascadedOpts_t opts = nvcompBatchedCascadedDefaultOpts;
      opts.chunk_size = chunk_size;
      if (priv->data_type != GST_NV_COMP_DATA_TYPE_DEFAULT)
        opts.type = (nvcompType_t) priv->data_type;

      if (priv->batched) {
        batch_size = (state->info.size + chunk_size - 1) / chunk_size;

        priv->batched_comp =
            std::make_shared < BatchedComp < nvcompBatchedCascadedOpts_t,
            nvcompBatchedCascadedCompressGetTempSize,
            nvcompBatchedCascadedCompressGetMaxOutputChunkSize,
            nvcompBatchedCascadedCompressAsync >> (opts);
        mime_type = "video/x-nvcomp-cascaded";
      } else {
        priv->manager = std::make_shared < CascadedManager > (chunk_size,
            opts, stream, device_id);
      }
      GST_DEBUG_OBJECT (self, "Using CASCADED");
      break;
    }
    case GST_NV_COMP_BITCOMP:
    {
      if (chunk_size == 0)
        chunk_size = 65536;

      chunk_size = MAX (32768, chunk_size);
      chunk_size = GST_ROUND_UP_8 (chunk_size);
      chunk_size = MIN (chunk_size,
          nvcompBitcompCompressionMaxAllowedChunkSize);

      nvcompBatchedBitcompFormatOpts opts = nvcompBatchedBitcompDefaultOpts;
      opts.algorithm_type = (int) priv->bitcomp_algo;
      if (priv->data_type != GST_NV_COMP_DATA_TYPE_DEFAULT)
        opts.data_type = (nvcompType_t) priv->data_type;

      if (priv->batched) {
        batch_size = (state->info.size + chunk_size - 1) / chunk_size;

        priv->batched_comp =
            std::make_shared < BatchedComp < nvcompBatchedBitcompFormatOpts,
            nvcompBatchedBitcompCompressGetTempSize,
            nvcompBatchedBitcompCompressGetMaxOutputChunkSize,
            nvcompBatchedBitcompCompressAsync >> (opts);
        mime_type = "video/x-nvcomp-bitcomp";
      } else {
        priv->manager = std::make_shared < BitcompManager > (chunk_size,
            opts, stream, device_id);
      }
      GST_DEBUG_OBJECT (self, "Using BITCOMP");
      break;
    }
    case GST_NV_COMP_ANS:
    {
      if (chunk_size == 0)
        chunk_size = 65536;

      chunk_size = MAX (32768, chunk_size);
      chunk_size = GST_ROUND_UP_8 (chunk_size);
      chunk_size = MIN (chunk_size, nvcompANSCompressionMaxAllowedChunkSize);

      if (priv->batched) {
        batch_size = (state->info.size + chunk_size - 1) / chunk_size;
        priv->batched_comp =
            std::make_shared < BatchedComp < nvcompBatchedANSOpts_t,
            nvcompBatchedANSCompressGetTempSize,
            nvcompBatchedANSCompressGetMaxOutputChunkSize,
            nvcompBatchedANSCompressAsync >> (nvcompBatchedANSDefaultOpts);
        mime_type = "video/x-nvcomp-ans";
      } else {
        priv->manager = std::make_shared < ANSManager > (chunk_size,
            nvcompBatchedANSDefaultOpts, stream, device_id);
      }

      GST_DEBUG_OBJECT (self, "Using ANS");
      break;
    }
    default:
      g_assert_not_reached ();
      return FALSE;
  }

  size_t max_output_size = 0;
  size_t max_output_chunk_size = 0;
  size_t temp_size = 0;
  if (priv->batched) {
    auto status = priv->batched_comp->get_temp_size (batch_size,
        chunk_size, &temp_size);
    if (status != nvcompSuccess) {
      GST_ERROR_OBJECT (self, "Couldn't get temp size");
      gst_cuda_context_pop (nullptr);
      return FALSE;
    }

    status = priv->batched_comp->get_max_compressed_chunk_size (chunk_size,
        &max_output_chunk_size);
    if (status != nvcompSuccess) {
      GST_ERROR_OBJECT (self, "Couldn't get max output chunk size");
      gst_cuda_context_pop (nullptr);
      return FALSE;
    }

    max_output_chunk_size = GST_ROUND_UP_8 (max_output_chunk_size);
    max_output_size = max_output_chunk_size * batch_size;
  } else {
    priv->config = std::make_shared < CompressionConfig >
        (priv->manager->configure_compression (state->info.size));
    max_output_size = priv->config->max_compressed_buffer_size;
  }

  GST_DEBUG_OBJECT (self, "Allocating resource, batched: %d"
      ", uncompressed size: %" G_GSIZE_FORMAT
      ", max-output-size: %" G_GSIZE_FORMAT
      ", batch-size: %" G_GSIZE_FORMAT
      ", chunk-size: %" G_GSIZE_FORMAT
      ", max-output-chunk-size: %" G_GSIZE_FORMAT
      ", temp-size: %" G_GSIZE_FORMAT, priv->batched, state->info.size,
      max_output_size, batch_size, chunk_size, max_output_chunk_size,
      temp_size);

  for (guint i = 0; i < priv->async_depth; i++) {
    auto task = std::make_shared < EncoderTask > ();
    task->ctx = (GstCudaContext *) gst_object_ref (priv->ctx);

    if (!gst_nv_comp_video_enc_alloc_task (self, task.get (), priv->batched,
            state->info.size, max_output_size, batch_size, chunk_size,
            max_output_chunk_size, temp_size)) {
      priv->manager = nullptr;
      priv->input_task_queue = { };
      task = nullptr;
      gst_cuda_context_pop (nullptr);
      return FALSE;
    }

    priv->input_task_queue.push (task);
  }

  /* In case of batched, custom header is added to signal chunk and batch size */
  if (priv->batched) {
    /* version */
    max_output_size += sizeof (guint32);

    /* max uncompressed chunk size */
    max_output_size += sizeof (guint32);

    /* max compressed chunk size */
    max_output_size += sizeof (guint32);

    /* batch size */
    max_output_size += sizeof (guint32);

    /* each uncompressed/compressed chunk size */
    max_output_size += (sizeof (guint32) * batch_size * 2);
  }

  priv->pool = gst_buffer_pool_new ();
  auto config = gst_buffer_pool_get_config (priv->pool);
  gst_buffer_pool_config_set_params (config, nullptr, max_output_size, 0, 0);
  gst_buffer_pool_set_config (priv->pool, config);
  gst_buffer_pool_set_active (priv->pool, TRUE);

  gst_cuda_context_pop (nullptr);

  auto caps = gst_caps_new_simple (mime_type.c_str (), "format", G_TYPE_STRING,
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&state->info)),
      nullptr);
  auto out_state =
      gst_video_encoder_set_output_state (GST_VIDEO_ENCODER (encoder),
      caps, state);
  gst_video_codec_state_unref (out_state);

  return TRUE;
}

static gboolean
gst_nv_comp_video_enc_upload (GstNvCompVideoEnc * self, GstVideoFrame * frame,
    CUstream stream, gboolean is_device_copy)
{
  auto priv = self->priv;
  auto info = &priv->state->info;
  auto finfo = info->finfo;
  gint comp[GST_VIDEO_MAX_COMPONENTS];
  CUresult ret = CUDA_SUCCESS;
  auto cur_task = priv->cur_task;

  for (guint i = 0; i < GST_VIDEO_FRAME_N_PLANES (frame); i++) {
    guint8 *sp = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (frame, i);
    guint8 *dp;
    if (is_device_copy)
      dp = cur_task->device_uncompressed + info->offset[i];
    else
      dp = cur_task->host_uncompressed + info->offset[i];

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

    ss = GST_VIDEO_FRAME_PLANE_STRIDE (frame, i);
    ds = GST_VIDEO_INFO_PLANE_STRIDE (info, i);

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
  GstNvCompVideoEnc *self = nullptr;
  GstBuffer *buffer = nullptr;
  gboolean ret = FALSE;
};

static GstCudaGraphicsResource *
ensure_gl_cuda_resource (GstNvCompVideoEnc * self, GstMemory * mem)
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
gst_nv_comp_video_enc_upload_gl (GstGLContext * context, GLInteropData * data)
{
  auto self = data->self;
  auto priv = self->priv;
  auto info = &priv->state->info;
  auto finfo = info->finfo;
  GstCudaGraphicsResource *gst_res[GST_VIDEO_MAX_PLANES] = { nullptr, };
  CUgraphicsResource cuda_res[GST_VIDEO_MAX_PLANES] = { nullptr, };
  CUdeviceptr src_devptr[GST_VIDEO_MAX_PLANES] = { 0, };
  CUstream stream = gst_cuda_stream_get_handle (priv->stream);
  CUresult ret;
  gint comp[GST_VIDEO_MAX_COMPONENTS];
  auto cur_task = priv->cur_task;

  if (!gst_cuda_context_push (priv->ctx)) {
    GST_ERROR_OBJECT (self, "Couldn't push context");
    return;
  }

  for (guint i = 0; i < GST_VIDEO_INFO_N_PLANES (info); i++) {
    GstMemory *mem = gst_buffer_peek_memory (data->buffer, i);
    GstGLMemoryPBO *pbo = (GstGLMemoryPBO *) mem;
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

    gst_gl_memory_pbo_upload_transfer (pbo);
    gst_gl_memory_pbo_download_transfer (pbo);

    cuda_res[i] = gst_cuda_graphics_resource_map (gst_res[i], stream,
        CU_GRAPHICS_MAP_RESOURCE_FLAGS_READ_ONLY);
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
  }


  for (guint i = 0; i < GST_VIDEO_INFO_N_PLANES (info); i++) {
    guint8 *sp = (guint8 *) src_devptr[i];
    guint8 *dp = cur_task->device_uncompressed + info->offset[i];
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
      ss = meta->stride[i];
    else
      ss = GST_VIDEO_INFO_PLANE_STRIDE (info, i);

    ds = GST_VIDEO_INFO_PLANE_STRIDE (info, i);

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

static gpointer
gst_nv_comp_video_enc_thread_func (GstNvCompVideoEnc * self)
{
  auto encoder = GST_VIDEO_ENCODER (self);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Entering loop");

  while (1) {
    std::shared_ptr < EncoderTask > task;

    {
      std::unique_lock < std::mutex > lk (priv->output_lock);
      while (priv->output_task_queue.empty ())
        priv->output_cond.wait (lk);

      task = priv->output_task_queue.front ();
      priv->output_task_queue.pop ();
    }

    if (!task) {
      GST_DEBUG_OBJECT (self, "Got empty task, terminate");
      break;
    }

    auto frame = gst_video_encoder_get_oldest_frame (encoder);

    gst_cuda_context_push (priv->ctx);
    CuEventSynchronize (task->event);
    gst_cuda_context_pop (nullptr);

    gst_buffer_pool_acquire_buffer (priv->pool, &frame->output_buffer, nullptr);
    GstMapInfo map_info;
    gst_buffer_map (frame->output_buffer, &map_info, GST_MAP_WRITE);
    if (task->batched) {
      task->compressed_size = 0;
      auto dst = (uint8_t *) map_info.data;

      /* Write custom header */
      GST_WRITE_UINT32_LE (dst, GST_NV_COMP_HEADER_VERSION);
      dst += sizeof (guint32);
      task->compressed_size += sizeof (guint32);

      GST_WRITE_UINT32_LE (dst, task->chunk_size);
      dst += sizeof (guint32);
      task->compressed_size += sizeof (guint32);

      GST_WRITE_UINT32_LE (dst, task->max_output_chunk_size);
      dst += sizeof (guint32);
      task->compressed_size += sizeof (guint32);

      GST_WRITE_UINT32_LE (dst, task->batch_size);
      dst += sizeof (guint32);
      task->compressed_size += sizeof (guint32);

      for (size_t i = 0; i < task->batch_size; i++) {
        GST_WRITE_UINT32_LE (dst, task->host_uncompressed_bytes[i]);
        dst += sizeof (guint32);
        task->compressed_size += sizeof (guint32);

        GST_WRITE_UINT32_LE (dst, task->host_compressed_bytes[i]);
        dst += sizeof (guint32);
        task->compressed_size += sizeof (guint32);
      }

      /* Write compressed data */
      for (size_t i = 0; i < task->batch_size; i++) {
        auto size = task->host_compressed_bytes[i];
        auto src = task->host_compressed + (i * task->max_output_chunk_size);
        memcpy (dst, src, size);
        dst += size;
        task->compressed_size += size;
      }
    } else {
      memcpy (map_info.data, task->host_compressed, task->compressed_size);
    }
    gst_buffer_unmap (frame->output_buffer, &map_info);

    if (task->compressed_size > 0) {
      gst_buffer_set_size (frame->output_buffer, task->compressed_size);
      frame->dts = frame->pts;

      auto ratio = (double) priv->state->info.size / task->compressed_size;
      GST_LOG_OBJECT (self, "compressed buffer size %" G_GSIZE_FORMAT
          ", ratio %.2f", task->compressed_size, ratio);
    } else {
      GST_ERROR_OBJECT (self, "Zero compressed size");
      gst_clear_buffer (&frame->output_buffer);
    }

    {
      std::lock_guard < std::mutex > lk (priv->input_lock);
      priv->input_task_queue.push (task);
      priv->input_cond.notify_all ();
    }

    priv->last_flow = gst_video_encoder_finish_frame (encoder, frame);
  };

  GST_DEBUG_OBJECT (self, "Leaving loop");

  return nullptr;
}

static GstFlowReturn
gst_nv_comp_video_enc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  auto self = GST_NV_COMP_VIDEO_ENC (encoder);
  auto priv = self->priv;
  GstMemory *mem;
  CUstream stream = nullptr;
  GstVideoFrame vframe;
  auto info = &priv->state->info;
  size_t compressed_size = 0;
  gboolean need_copy = TRUE;
  std::shared_ptr < EncoderTask > task;

  if (!priv->ctx || (!priv->manager && !priv->batched_comp)) {
    GST_ERROR_OBJECT (self, "Context was not configured");
    goto error;
  }

  if (priv->last_flow != GST_FLOW_OK) {
    GST_INFO_OBJECT (self, "Last flow was %s",
        gst_flow_get_name (priv->last_flow));
    gst_video_encoder_finish_frame (encoder, frame);
    return priv->last_flow;
  }

  if (!priv->encode_thread) {
    priv->encode_thread = g_thread_new ("nvcompvideoenc",
        (GThreadFunc) gst_nv_comp_video_enc_thread_func, self);
  }

  GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);
  {
    std::unique_lock < std::mutex > lk (priv->input_lock);
    while (priv->input_task_queue.empty ())
      priv->input_cond.wait (lk);

    priv->cur_task = priv->input_task_queue.front ();
    priv->input_task_queue.pop ();
  }
  GST_VIDEO_ENCODER_STREAM_LOCK (encoder);

  mem = gst_buffer_peek_memory (frame->input_buffer, 0);
#ifdef HAVE_GST_GL
  if (priv->gl_interop && gst_is_gl_memory (mem) &&
      gst_buffer_n_memory (frame->input_buffer) ==
      GST_VIDEO_INFO_N_PLANES (info)) {
    GLInteropData interop_data;
    interop_data.self = self;
    interop_data.buffer = frame->input_buffer;
    interop_data.ret = FALSE;

    auto gl_mem = (GstGLMemory *) mem;
    gst_gl_context_thread_add (gl_mem->mem.context,
        (GstGLContextThreadFunc) gst_nv_comp_video_enc_upload_gl,
        &interop_data);
    if (interop_data.ret) {
      need_copy = FALSE;
      GST_TRACE_OBJECT (self, "GL -> CUDA copy done");
    } else {
      priv->gl_interop = FALSE;
    }
  }
#endif

  if (!gst_cuda_context_push (priv->ctx)) {
    GST_ERROR_OBJECT (self, "Couldn't push context");
    std::lock_guard < std::mutex > lk (priv->input_lock);
    priv->input_task_queue.push (std::move (priv->cur_task));
    goto error;
  }

  stream = gst_cuda_stream_get_handle (priv->stream);

  if (need_copy) {
    gboolean device_copy = FALSE;
    if (gst_is_cuda_memory (mem)) {
      GstCudaMemory *cmem = GST_CUDA_MEMORY_CAST (mem);
      if (cmem->context == priv->ctx) {
        device_copy = TRUE;
        if (!gst_video_frame_map (&vframe, info, frame->input_buffer,
                (GstMapFlags) (GST_MAP_READ | GST_MAP_CUDA))) {
          GST_ERROR_OBJECT (self, "Couldn't map cuda memory");
          gst_cuda_context_pop (nullptr);
          std::lock_guard < std::mutex > lk (priv->input_lock);
          priv->input_task_queue.push (std::move (priv->cur_task));
          goto error;
        }

        if (gst_cuda_memory_get_stream (cmem) != priv->stream) {
          GST_DEBUG_OBJECT (self, "Different stream, need sync");
          gst_cuda_memory_sync (cmem);
        }
      }
    }

    if (!device_copy && !gst_video_frame_map (&vframe,
            info, frame->input_buffer, GST_MAP_READ)) {
      GST_ERROR_OBJECT (self, "Couldn't map input frame");
      gst_cuda_context_pop (nullptr);
      std::lock_guard < std::mutex > lk (priv->input_lock);
      priv->input_task_queue.push (std::move (priv->cur_task));
      goto error;
    }

    if (!gst_nv_comp_video_enc_upload (self, &vframe, stream, device_copy)) {
      gst_video_frame_unmap (&vframe);
      gst_cuda_context_pop (nullptr);
      std::lock_guard < std::mutex > lk (priv->input_lock);
      priv->input_task_queue.push (std::move (priv->cur_task));
      goto error;
    }

    gst_video_frame_unmap (&vframe);

    if (!device_copy) {
      CuMemcpyHtoDAsync ((CUdeviceptr) priv->cur_task->device_uncompressed,
          priv->cur_task->host_uncompressed, info->size, stream);
    }
  }

  task = std::move (priv->cur_task);
  if (task->batched) {
    g_assert (priv->batched_comp);

    auto status = priv->batched_comp->compress (task->device_uncompressed_ptrs,
        task->device_uncompressed_bytes, task->chunk_size, task->batch_size,
        task->temp_ptr, task->temp_size, task->device_compressed_ptrs,
        task->device_compressed_bytes, (cudaStream_t) stream);
    if (status != nvcompSuccess) {
      GST_ERROR_OBJECT (self, "Compression failed, ret %d", status);
      gst_cuda_context_pop (nullptr);
      std::lock_guard < std::mutex > lk (priv->input_lock);
      priv->input_task_queue.push (std::move (task));
      goto error;
    }

    CuMemcpyDtoHAsync (task->host_compressed_bytes,
        (CUdeviceptr) task->device_compressed_bytes,
        sizeof (size_t) * task->batch_size, stream);
    CuMemcpyDtoHAsync (task->host_compressed,
        (CUdeviceptr) task->device_compressed,
        task->compressed_alloc_size, stream);
  } else {
    g_assert (priv->manager);

    priv->manager->compress (task->device_uncompressed,
        task->device_compressed, *priv->config);

    compressed_size =
        priv->manager->get_compressed_output_size (task->device_compressed);

    task->compressed_size = compressed_size;
    CuMemcpyDtoHAsync (task->host_compressed,
        (CUdeviceptr) task->device_compressed, compressed_size, stream);
  }

  CuEventRecord (task->event, stream);
  gst_cuda_context_pop (nullptr);

  {
    std::lock_guard < std::mutex > lk (priv->output_lock);
    priv->output_task_queue.push (std::move (task));
    priv->output_cond.notify_one ();
  }

  gst_video_codec_frame_unref (frame);

  return priv->last_flow;

error:
  gst_video_encoder_finish_frame (encoder, frame);

  return GST_FLOW_ERROR;
}
