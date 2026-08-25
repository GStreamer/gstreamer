/* GStreamer
 * Copyright (C) 2026 Seungha Yang <seungha@centricular.com>
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

#include "gstonnximporter-hip.h"
#include <string.h>

GST_DEBUG_CATEGORY_EXTERN (onnx_inference_debug);
#define GST_CAT_DEFAULT onnx_inference_debug

/* *INDENT-OFF* */
static const gchar kernel_source[] =
"extern \"C\" {\n"
"__global__ void\n"
"scale_packed_f32 (const float *src, size_t src_stride,\n"
"    float *dst, int width, int height,\n"
"    float scale0, float scale1, float scale2, \n"
"    float offset0, float offset1, float offset2)\n"
"{\n"
"  const int x = blockIdx.x * blockDim.x + threadIdx.x;\n"
"  const int y = blockIdx.y * blockDim.y + threadIdx.y;\n"
"\n"
"  if (x >= width || y >= height)\n"
"    return;\n"
"\n"
"  const float *src_row =\n"
"      (const float *) ((const unsigned char *) src + y * src_stride);\n"
"  float *dst_row = dst + (size_t) y * width * 3;\n"
"\n"
"  const int pos = x * 3;\n"
"  dst_row[pos] = src_row[pos] * scale0 + offset0;\n"
"  dst_row[pos + 1] = src_row[pos + 1] * scale1 + offset1;\n"
"  dst_row[pos + 2] = src_row[pos + 2] * scale2 + offset2;\n"
"}\n"
"\n"
"__global__ void\n"
"scale_plane_f32 (const float *src, size_t src_stride,\n"
"    float *dst, int width, int height,\n"
"    float scale, float offset)\n"
"{\n"
"  const int x = blockIdx.x * blockDim.x + threadIdx.x;\n"
"  const int y = blockIdx.y * blockDim.y + threadIdx.y;\n"
"\n"
"  if (x >= width || y >= height)\n"
"    return;\n"
"\n"
"  const float *src_row =\n"
"      (const float *) ((const unsigned char *) src + y * src_stride);\n"
"\n"
"  float *dst_row = dst + (size_t) y * width;\n"
"  dst_row[x] = src_row[x] * scale + offset;\n"
"}\n"
"\n"
"__device__ inline unsigned char\n"
"float_to_u8 (float v)\n"
"{\n"
"  return (unsigned char) __float2int_rz (fminf (fmaxf (v, 0.0f), 255.0f));\n"
"}\n"
"\n"
"__global__ void\n"
"scale_packed_u8 (const unsigned char *src, size_t src_stride,\n"
"    unsigned char *dst, int width, int height,\n"
"    float scale0, float scale1, float scale2, \n"
"    float offset0, float offset1, float offset2)\n"
"{\n"
"  const int x = blockIdx.x * blockDim.x + threadIdx.x;\n"
"  const int y = blockIdx.y * blockDim.y + threadIdx.y;\n"
"\n"
"  if (x >= width || y >= height)\n"
"    return;\n"
"\n"
"  const unsigned char *src_row = src + y * src_stride;\n"
"  unsigned char *dst_row = dst + (size_t) y * width * 3;\n"
"\n"
"  const int pos = x * 3;\n"
"\n"
"  float r = src_row[pos] * scale0 + offset0;\n"
"  float g = src_row[pos + 1] * scale1 + offset1;\n"
"  float b = src_row[pos + 2] * scale2 + offset2;\n"
"\n"
"  dst_row[pos] = float_to_u8 (r);\n"
"  dst_row[pos + 1] = float_to_u8 (g);\n"
"  dst_row[pos + 2] = float_to_u8 (b);\n"
"}\n"
"\n"
"__global__ void\n"
"scale_plane_u8 (const unsigned char *src, size_t src_stride,\n"
"    unsigned char *dst, int width, int height,\n"
"    float scale, float offset)\n"
"{\n"
"  const int x = blockIdx.x * blockDim.x + threadIdx.x;\n"
"  const int y = blockIdx.y * blockDim.y + threadIdx.y;\n"
"\n"
"  if (x >= width || y >= height)\n"
"    return;\n"
"\n"
"  const unsigned char *src_row = src + y * src_stride;\n"
"  unsigned char *dst_row = dst + (size_t) y * width;\n"
"\n"
"  float v = src_row[x] * scale + offset;\n"
"  dst_row[x] = float_to_u8 (v);\n"
"}\n"
"}\n"
"\n";
/* *INDENT-ON* */

typedef struct
{
  gchar *arch;
  const gchar *blob;
} KernelCacheEntry;

G_LOCK_DEFINE_STATIC (cache_lock);
static GArray *kernel_cache = NULL;

static const gchar *
get_blob_for_device (GstHipDevice * device)
{
  hipDeviceProp_t prop;
  hipError_t hip_ret;
  gint device_id;
  const gchar *compiled_blob;
  gchar *arch_opt;
  const gchar *opts[1];
  guint i;

  device_id = gst_hip_device_get_device_id (device);

  hip_ret = HipGetDeviceProperties (GST_HIP_VENDOR_AMD, &prop, device_id);
  if (!gst_hip_result (hip_ret, GST_HIP_VENDOR_AMD)) {
    GST_ERROR_OBJECT (device, "Couldn't get device property");
    return NULL;
  }

  G_LOCK (cache_lock);
  if (!kernel_cache)
    kernel_cache = g_array_new (FALSE, FALSE, sizeof (KernelCacheEntry));

  for (i = 0; i < kernel_cache->len; i++) {
    KernelCacheEntry *entry =
        &g_array_index (kernel_cache, KernelCacheEntry, i);

    if (g_str_equal (entry->arch, prop.gcnArchName)) {
      compiled_blob = entry->blob;
      G_UNLOCK (cache_lock);
      return compiled_blob;
    }
  }

  arch_opt = g_strdup_printf ("--gpu-architecture=%s", prop.gcnArchName);
  opts[0] = arch_opt;

  compiled_blob = gst_hip_rtc_compile (device, kernel_source, opts, 1);
  g_free (arch_opt);

  if (!compiled_blob) {
    GST_WARNING_OBJECT (device, "Couldn't compile kernel");
    G_UNLOCK (cache_lock);
    return NULL;
  }

  {
    KernelCacheEntry entry;
    entry.arch = g_strdup (prop.gcnArchName);
    entry.blob = compiled_blob;

    g_array_append_val (kernel_cache, entry);
  }

  G_UNLOCK (cache_lock);

  return compiled_blob;
}

struct _GstOnnxImporterHip
{
  GstOnnxImporter parent;

  const OrtApi *api;
  OrtMemoryInfo *memory_info;

  GstVideoInfo info;
  GstOnnxImporterConfig config;
  ONNXTensorElementDataType data_type_onnx;
  gboolean needs_normalization;

  gboolean is_packed;
  gsize elem_size;
  GstVideoFrame frame;

  GstHipDevice *device;
  GstHipStream *stream;

  gpointer device_ptr;

  hipModule_t kernel_module;
  hipFunction_t scale_packed_func_f32;
  hipFunction_t scale_plane_func_f32;
  hipFunction_t scale_packed_func_u8;
  hipFunction_t scale_plane_func_u8;
  hipFunction_t active_func;
};

static void gst_onnx_importer_hip_finalize (GObject * object);
static gboolean gst_onnx_importer_hip_setup (GstOnnxImporter * importer,
    const GstVideoInfo * info, const GstOnnxImporterConfig * config);
static OrtValue *gst_onnx_importer_hip_prepare (GstOnnxImporter * importer,
    GstBuffer * buffer);
static void gst_onnx_importer_hip_unprepare (GstOnnxImporter * importer);

#define gst_onnx_importer_hip_parent_class parent_class
G_DEFINE_TYPE (GstOnnxImporterHip, gst_onnx_importer_hip,
    GST_TYPE_ONNX_IMPORTER);

static void
gst_onnx_importer_hip_class_init (GstOnnxImporterHipClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstOnnxImporterClass *importer_class = GST_ONNX_IMPORTER_CLASS (klass);

  object_class->finalize = gst_onnx_importer_hip_finalize;

  importer_class->setup = gst_onnx_importer_hip_setup;
  importer_class->prepare = gst_onnx_importer_hip_prepare;
  importer_class->unprepare = gst_onnx_importer_hip_unprepare;
}

static void
gst_onnx_importer_hip_init (GstOnnxImporterHip * self)
{
}

static void
gst_onnx_importer_hip_reset (GstOnnxImporterHip * self, gboolean hard)
{
  gst_onnx_importer_hip_unprepare (GST_ONNX_IMPORTER (self));

  if (hard) {
    gst_hip_device_set_current (self->device);
    self->config.tensor_size = 0;

    if (self->device_ptr)
      HipFree (GST_HIP_VENDOR_AMD, self->device_ptr);
    self->device_ptr = NULL;
  }
}

static void
gst_onnx_importer_hip_finalize (GObject * object)
{
  GstOnnxImporterHip *self = GST_ONNX_IMPORTER_HIP (object);

  gst_onnx_importer_hip_reset (self, TRUE);
  if (self->memory_info)
    self->api->ReleaseMemoryInfo (self->memory_info);

  gst_hip_device_set_current (self->device);
  if (self->kernel_module)
    HipModuleUnload (GST_HIP_VENDOR_AMD, self->kernel_module);

  gst_clear_hip_stream (&self->stream);
  gst_clear_object (&self->device);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_onnx_importer_hip_setup (GstOnnxImporter * importer,
    const GstVideoInfo * info, const GstOnnxImporterConfig * config)
{
  GstOnnxImporterHip *self = GST_ONNX_IMPORTER_HIP (importer);
  hipError_t hip_ret;

  gst_onnx_importer_hip_reset (self, FALSE);

  if (!gst_hip_device_set_current (self->device)) {
    GST_ERROR_OBJECT (self, "Couldn't make device current");
    return FALSE;
  }

  self->is_packed = GST_VIDEO_INFO_N_COMPONENTS (info) == 3 &&
      GST_VIDEO_INFO_N_PLANES (info) == 1;

  switch (config->data_type) {
    case GST_TENSOR_DATA_TYPE_UINT8:
      self->data_type_onnx = ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8;
      self->elem_size = sizeof (guint8);
      self->active_func = self->is_packed ? self->scale_packed_func_u8 :
          self->scale_plane_func_u8;
      break;
    case GST_TENSOR_DATA_TYPE_FLOAT32:
      self->data_type_onnx = ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
      self->elem_size = sizeof (gfloat);
      self->active_func = self->is_packed ?
          self->scale_packed_func_f32 : self->scale_plane_func_f32;
      break;
    default:
      g_assert_not_reached ();
      return FALSE;
  }

  if (self->config.tensor_size != config->tensor_size) {
    if (self->device_ptr)
      HipFree (GST_HIP_VENDOR_AMD, self->device_ptr);
    self->device_ptr = NULL;
  }

  self->info = *info;
  self->config = *config;
  self->needs_normalization =
      gst_onnx_importer_config_needs_normalization (config, info);

  if (!self->device_ptr) {
    hip_ret = HipMalloc (GST_HIP_VENDOR_AMD, &self->device_ptr,
        config->tensor_size);
    if (!gst_hip_result (hip_ret, GST_HIP_VENDOR_AMD)) {
      GST_ERROR_OBJECT (self, "Couldn't allocate device memory");
      return FALSE;
    }
  }

  return TRUE;
}

static OrtValue *
gst_onnx_importer_hip_prepare (GstOnnxImporter * importer, GstBuffer * buffer)
{
  GstOnnxImporterHip *self = GST_ONNX_IMPORTER_HIP (importer);
  const GstVideoInfo *info = &self->info;
  GstVideoFrame *vframe = &self->frame;
  const OrtApi *api = self->api;
  OrtStatus *status = NULL;
  OrtValue *ret = NULL;
  gpointer tensor_data = NULL;
  hipStream_t stream;
  GstHipStream *mem_stream;
  hipError_t hip_ret = hipSuccess;
  const GstOnnxImporterConfig *config = &self->config;

  gst_onnx_importer_hip_unprepare (importer);

  guint n_mem = gst_buffer_n_memory (buffer);
  if (n_mem != 1) {
    /* GstHip expects a single memory per buffer */
    GST_DEBUG_OBJECT (self, "Unexpected memory layout");
    return NULL;
  }

  GstMemory *mem = gst_buffer_peek_memory (buffer, 0);
  if (!gst_is_hip_memory (mem)) {
    GST_DEBUG_OBJECT (self, "Not a HIP memory");
    return NULL;
  }

  GstHipMemory *hmem = GST_HIP_MEMORY_CAST (mem);
  if (gst_hip_device_get_vendor (hmem->device) != GST_HIP_VENDOR_AMD ||
      gst_hip_device_get_device_id (hmem->device) !=
      gst_hip_device_get_device_id (self->device)) {
    GST_DEBUG_OBJECT (self, "Different device, need CPU fallback");
    return NULL;
  }

  if (!gst_video_frame_map (vframe, info, buffer,
          (GstMapFlags) (GST_MAP_READ_HIP | GST_VIDEO_FRAME_MAP_FLAG_NO_REF))) {
    GST_ERROR_OBJECT (self, "Couldn't map buffer");
    return NULL;
  }

  if (!gst_hip_device_set_current (self->device)) {
    GST_ERROR_OBJECT (self, "Couldn't make device current");
    goto error;
  }

  mem_stream = gst_hip_memory_get_stream (hmem);
  if (mem_stream)
    stream = gst_hip_stream_get_handle (mem_stream);
  else
    stream = gst_hip_stream_get_handle (self->stream);

  {
    guint n_planes = GST_VIDEO_INFO_N_PLANES (info);
    gboolean direct = !self->needs_normalization;

    if (direct) {
      for (guint p = 0; p < n_planes; p++) {
        guint comp = self->is_packed ? 0 : p;
        gsize row_size = (gsize) GST_VIDEO_INFO_COMP_WIDTH (info, comp) *
            GST_VIDEO_INFO_COMP_PSTRIDE (info, comp);

        if ((gsize) GST_VIDEO_FRAME_PLANE_STRIDE (vframe, p) != row_size) {
          direct = FALSE;
          break;
        }
      }
    }

    if (direct && n_planes > 1) {
      guint8 *ptr = GST_VIDEO_FRAME_PLANE_DATA (vframe, 0);

      for (guint p = 0; p + 1 < n_planes; p++) {
        gsize plane_size = (gsize) GST_VIDEO_INFO_COMP_WIDTH (info, p) *
            GST_VIDEO_INFO_COMP_HEIGHT (info, p) *
            GST_VIDEO_INFO_COMP_PSTRIDE (info, p);

        ptr += plane_size;

        if (ptr != GST_VIDEO_FRAME_PLANE_DATA (vframe, p + 1)) {
          direct = FALSE;
          break;
        }
      }
    }

    if (direct) {
      GST_LOG_OBJECT (self, "Using input HIP memory directly");
      gst_hip_memory_sync (hmem);
      tensor_data = GST_VIDEO_FRAME_PLANE_DATA (vframe, 0);
    } else {
      gint width = GST_VIDEO_INFO_WIDTH (info);
      gint height = GST_VIDEO_INFO_HEIGHT (info);
      const guint block_x = 16;
      const guint block_y = 16;

      if (self->is_packed) {
        guint8 *src = GST_VIDEO_FRAME_PLANE_DATA (vframe, 0);
        size_t src_stride = GST_VIDEO_FRAME_PLANE_STRIDE (vframe, 0);
        gpointer dst = self->device_ptr;

        if (self->needs_normalization) {
          float scale0 = (float) config->scales[0];
          float scale1 = (float) config->scales[1];
          float scale2 = (float) config->scales[2];
          float offset0 = (float) config->offsets[0];
          float offset1 = (float) config->offsets[1];
          float offset2 = (float) config->offsets[2];

          gpointer args[] = {
            &src,
            &src_stride,
            &dst,
            &width,
            &height,
            &scale0,
            &scale1,
            &scale2,
            &offset0,
            &offset1,
            &offset2,
          };

          hip_ret = HipModuleLaunchKernel (GST_HIP_VENDOR_AMD,
              self->active_func, (width + block_x - 1) / block_x,
              (height + block_y - 1) / block_y, 1,
              block_x, block_y, 1, 0, stream, args, NULL);
        } else {
          guint dst_pitch = info->width * GST_VIDEO_INFO_COMP_PSTRIDE (info, 0);
          hip_Memcpy2D copy = { 0, };

          copy.srcMemoryType = hipMemoryTypeDevice;
          copy.srcDevice = (hipDeviceptr_t) src;
          copy.srcPitch = GST_VIDEO_FRAME_PLANE_STRIDE (vframe, 0);

          copy.dstMemoryType = hipMemoryTypeDevice;
          copy.dstDevice = (hipDeviceptr_t) self->device_ptr;
          copy.dstPitch = dst_pitch;
          copy.WidthInBytes = dst_pitch;
          copy.Height = info->height;

          hip_ret = HipMemcpyParam2DAsync (GST_HIP_VENDOR_AMD, &copy, stream);
        }

        if (!gst_hip_result (hip_ret, GST_HIP_VENDOR_AMD)) {
          GST_ERROR_OBJECT (self, "Couldn't launch packed conversion kernel");
          goto error;
        }
      } else {
        gsize dst_offset = 0;

        for (guint comp = 0; comp < n_planes; comp++) {
          gint plane_width = GST_VIDEO_INFO_COMP_WIDTH (info, comp);
          gint plane_height = GST_VIDEO_INFO_COMP_HEIGHT (info, comp);
          guint8 *src = GST_VIDEO_FRAME_PLANE_DATA (vframe, comp);
          size_t src_stride = GST_VIDEO_FRAME_PLANE_STRIDE (vframe, comp);
          guint8 *dst = (guint8 *) self->device_ptr + dst_offset;

          if (self->needs_normalization) {
            float scale = (float) config->scales[comp];
            float offset = (float) config->offsets[comp];

            gpointer args[] = {
              &src,
              &src_stride,
              &dst,
              &plane_width,
              &plane_height,
              &scale,
              &offset,
            };

            hip_ret = HipModuleLaunchKernel (GST_HIP_VENDOR_AMD,
                self->active_func,
                (plane_width + block_x - 1) / block_x,
                (plane_height + block_y - 1) / block_y,
                1, block_x, block_y, 1, 0, stream, args, NULL);
          } else {
            guint plane_width = GST_VIDEO_INFO_COMP_WIDTH (info, comp);
            guint plane_height = GST_VIDEO_INFO_COMP_HEIGHT (info, comp);
            guint dst_pitch =
                (gsize) plane_width * GST_VIDEO_INFO_COMP_PSTRIDE (info, comp);

            hip_Memcpy2D copy = { 0, };
            copy.srcMemoryType = hipMemoryTypeDevice;
            copy.srcDevice =
                (hipDeviceptr_t) GST_VIDEO_FRAME_PLANE_DATA (vframe, comp);
            copy.srcPitch = GST_VIDEO_FRAME_PLANE_STRIDE (vframe, comp);

            copy.dstMemoryType = hipMemoryTypeDevice;
            copy.dstDevice =
                (hipDeviceptr_t) ((guint8 *) self->device_ptr + dst_offset);
            copy.dstPitch = dst_pitch;
            copy.WidthInBytes = dst_pitch;
            copy.Height = plane_height;

            hip_ret = HipMemcpyParam2DAsync (GST_HIP_VENDOR_AMD, &copy, stream);
          }

          if (!gst_hip_result (hip_ret, GST_HIP_VENDOR_AMD)) {
            GST_ERROR_OBJECT (self, "Couldn't launch plane conversion kernel");
            goto error;
          }

          dst_offset += self->elem_size * plane_width * plane_height;
        }
      }

      /*
       * ORT/MIGraphX can use a different stream, so finish our work before
       * handing the tensor over.
       */
      hipError_t hip_ret = HipStreamSynchronize (GST_HIP_VENDOR_AMD, stream);
      if (!gst_hip_result (hip_ret, GST_HIP_VENDOR_AMD)) {
        GST_ERROR_OBJECT (self, "Couldn't synchronize HIP stream");
        goto error;
      }

      tensor_data = self->device_ptr;
    }
  }

  status = api->CreateTensorWithDataAsOrtValue (self->memory_info,
      tensor_data, config->tensor_size,
      config->dims, config->dims_count, self->data_type_onnx, &ret);
  if (status)
    goto error;

  GST_LOG_OBJECT (self, "Exporting done");

  return ret;

error:
  if (status) {
    GST_ERROR_OBJECT (self, "Couldn't create tensor: %s",
        api->GetErrorMessage (status));
    api->ReleaseStatus (status);
  }

  gst_onnx_importer_hip_unprepare (importer);
  return NULL;
}

static void
gst_onnx_importer_hip_unprepare (GstOnnxImporter * importer)
{
  GstOnnxImporterHip *self = GST_ONNX_IMPORTER_HIP (importer);

  if (self->frame.buffer)
    gst_video_frame_unmap (&self->frame);

  memset (&self->frame, 0, sizeof (GstVideoFrame));
}

GstOnnxImporter *
gst_onnx_importer_hip_new (const OrtApi * api, GstHipDevice * device)
{
  OrtMemoryInfo *memory_info;
  guint device_id = gst_hip_device_get_device_id (device);

  const gchar *blob = get_blob_for_device (device);
  if (!blob) {
    GST_WARNING_OBJECT (device, "Couldn't get compiled blob");
    return NULL;
  }

  if (!gst_hip_device_set_current (device)) {
    GST_ERROR_OBJECT (device, "Could't make device current");
    return NULL;
  }

  hipModule_t kernel_module;
  hipFunction_t scale_packed_func_f32;
  hipFunction_t scale_plane_func_f32;
  hipFunction_t scale_packed_func_u8;
  hipFunction_t scale_plane_func_u8;

  hipError_t hip_ret =
      HipModuleLoadData (GST_HIP_VENDOR_AMD, &kernel_module, blob);
  if (!gst_hip_result (hip_ret, GST_HIP_VENDOR_AMD)) {
    GST_ERROR_OBJECT (device, "Couldn't load kernel");
    return NULL;
  }

  hip_ret = HipModuleGetFunction (GST_HIP_VENDOR_AMD, &scale_packed_func_f32,
      kernel_module, "scale_packed_f32");
  if (!gst_hip_result (hip_ret, GST_HIP_VENDOR_AMD)) {
    GST_ERROR_OBJECT (device, "Couldn't get kernel function");
    HipModuleUnload (GST_HIP_VENDOR_AMD, kernel_module);
    return NULL;
  }

  hip_ret = HipModuleGetFunction (GST_HIP_VENDOR_AMD, &scale_plane_func_f32,
      kernel_module, "scale_plane_f32");
  if (!gst_hip_result (hip_ret, GST_HIP_VENDOR_AMD)) {
    GST_ERROR_OBJECT (device, "Couldn't get kernel function");
    HipModuleUnload (GST_HIP_VENDOR_AMD, kernel_module);
    return NULL;
  }

  hip_ret = HipModuleGetFunction (GST_HIP_VENDOR_AMD, &scale_packed_func_u8,
      kernel_module, "scale_packed_u8");
  if (!gst_hip_result (hip_ret, GST_HIP_VENDOR_AMD)) {
    GST_ERROR_OBJECT (device, "Couldn't get kernel function");
    HipModuleUnload (GST_HIP_VENDOR_AMD, kernel_module);
    return NULL;
  }

  hip_ret = HipModuleGetFunction (GST_HIP_VENDOR_AMD, &scale_plane_func_u8,
      kernel_module, "scale_plane_u8");
  if (!gst_hip_result (hip_ret, GST_HIP_VENDOR_AMD)) {
    GST_ERROR_OBJECT (device, "Couldn't get kernel function");
    HipModuleUnload (GST_HIP_VENDOR_AMD, kernel_module);
    return NULL;
  }

  OrtStatus *status = api->CreateMemoryInfo ("Hip",
      OrtDeviceAllocator, device_id, OrtMemTypeDefault, &memory_info);
  if (status) {
    GST_ERROR ("Couldn't create memory info: %s",
        api->GetErrorMessage (status));
    api->ReleaseStatus (status);
    HipModuleUnload (GST_HIP_VENDOR_AMD, kernel_module);
    return NULL;
  }

  GstOnnxImporterHip *self = g_object_new (GST_TYPE_ONNX_IMPORTER_HIP, NULL);

  gst_object_ref_sink (self);
  self->api = api;
  self->memory_info = memory_info;
  self->device = (GstHipDevice *) gst_object_ref (device);
  self->stream = gst_hip_stream_new (GST_HIP_VENDOR_AMD, device_id);
  if (!self->stream) {
    GST_WARNING_OBJECT (self,
        "Couldn't create HIP stream, will try default stream later");
  }

  self->kernel_module = kernel_module;
  self->scale_packed_func_f32 = scale_packed_func_f32;
  self->scale_plane_func_f32 = scale_plane_func_f32;
  self->scale_packed_func_u8 = scale_packed_func_u8;
  self->scale_plane_func_u8 = scale_plane_func_u8;

  return (GstOnnxImporter *) self;
}
