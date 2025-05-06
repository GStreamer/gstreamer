/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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

#include "gsthip.h"
#include <mutex>
#include <condition_variable>
#include <queue>

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static GstDebugCategory *cat = nullptr;
  static std::once_flag once;

  std::call_once (once,[&] {
        cat = _gst_debug_category_new ("hipallocator", 0, "hipallocator");
      });

  return cat;
}
#endif

static GstHipAllocator *_hip_memory_allocator = nullptr;
#define N_TEX_ADDR_MODES 4
#define N_TEX_FILTER_MODES 2
struct _GstHipMemoryPrivate
{
  GstHipVendor vendor;
  void *data = nullptr;
  void *staging = nullptr;
  gsize pitch = 0;
  guint width_in_bytes = 0;
  guint height = 0;
  gboolean texture_support = FALSE;
  hipTextureObject_t texture[4][N_TEX_ADDR_MODES][N_TEX_FILTER_MODES] = { };

  std::mutex lock;
};

struct _GstHipAllocatorPrivate
{
  GstMemoryCopyFunction fallback_copy;
};

#define gst_hip_allocator_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstHipAllocator,
    gst_hip_allocator, GST_TYPE_ALLOCATOR);

static void gst_hip_allocator_free (GstAllocator * allocator,
    GstMemory * memory);

static gpointer hip_mem_map (GstMemory * mem, gsize maxsize, GstMapFlags flags);
static void hip_mem_unmap (GstMemory * mem);
static GstMemory *hip_mem_copy (GstMemory * mem, gssize offset, gssize size);

static GstMemory *
gst_hip_allocator_dummy_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  g_return_val_if_reached (nullptr);
}

static void
gst_hip_allocator_class_init (GstHipAllocatorClass * klass)
{
  auto alloc_class = GST_ALLOCATOR_CLASS (klass);

  alloc_class->alloc = GST_DEBUG_FUNCPTR (gst_hip_allocator_dummy_alloc);
  alloc_class->free = GST_DEBUG_FUNCPTR (gst_hip_allocator_free);
}

static void
gst_hip_allocator_init (GstHipAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);
  GstHipAllocatorPrivate *priv;

  priv = allocator->priv = (GstHipAllocatorPrivate *)
      gst_hip_allocator_get_instance_private (allocator);

  alloc->mem_type = GST_HIP_MEMORY_NAME;

  alloc->mem_map = hip_mem_map;
  alloc->mem_unmap = hip_mem_unmap;

  /* Store pointer to default mem_copy method for fallback copy */
  priv->fallback_copy = alloc->mem_copy;
  alloc->mem_copy = hip_mem_copy;

  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

static gboolean
gst_hip_allocator_update_info (const GstVideoInfo * reference,
    gsize pitch, gsize alloc_height, GstVideoInfo * aligned)
{
  GstVideoInfo ret = *reference;
  guint height = reference->height;

  ret.size = pitch * alloc_height;

  switch (GST_VIDEO_INFO_FORMAT (reference)) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_I420_10LE:
    case GST_VIDEO_FORMAT_I420_12LE:
    {
      guint chroma_height = GST_ROUND_UP_2 (height) / 2;
      /* we are wasting space yes, but required so that this memory
       * can be used in kernel function */
      ret.stride[0] = pitch;
      ret.stride[1] = pitch;
      ret.stride[2] = pitch;
      ret.offset[0] = 0;
      ret.offset[1] = ret.stride[0] * height;
      ret.offset[2] = ret.offset[1] + (ret.stride[1] * chroma_height);
      break;
    }
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_I422_10LE:
    case GST_VIDEO_FORMAT_I422_12LE:
      ret.stride[0] = pitch;
      ret.stride[1] = pitch;
      ret.stride[2] = pitch;
      ret.offset[0] = 0;
      ret.offset[1] = ret.stride[0] * height;
      ret.offset[2] = ret.offset[1] + (ret.stride[1] * height);
      break;
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
    case GST_VIDEO_FORMAT_P010_10LE:
    case GST_VIDEO_FORMAT_P012_LE:
    case GST_VIDEO_FORMAT_P016_LE:
      ret.stride[0] = pitch;
      ret.stride[1] = pitch;
      ret.offset[0] = 0;
      ret.offset[1] = ret.stride[0] * height;
      break;
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y444_10LE:
    case GST_VIDEO_FORMAT_Y444_12LE:
    case GST_VIDEO_FORMAT_Y444_16LE:
    case GST_VIDEO_FORMAT_RGBP:
    case GST_VIDEO_FORMAT_BGRP:
    case GST_VIDEO_FORMAT_GBR:
    case GST_VIDEO_FORMAT_GBR_10LE:
    case GST_VIDEO_FORMAT_GBR_12LE:
    case GST_VIDEO_FORMAT_GBR_16LE:
      ret.stride[0] = pitch;
      ret.stride[1] = pitch;
      ret.stride[2] = pitch;
      ret.offset[0] = 0;
      ret.offset[1] = ret.stride[0] * height;
      ret.offset[2] = ret.offset[1] * 2;
      break;
    case GST_VIDEO_FORMAT_GBRA:
      ret.stride[0] = pitch;
      ret.stride[1] = pitch;
      ret.stride[2] = pitch;
      ret.stride[3] = pitch;
      ret.offset[0] = 0;
      ret.offset[1] = ret.stride[0] * height;
      ret.offset[2] = ret.offset[1] * 2;
      ret.offset[3] = ret.offset[1] * 3;
      break;
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
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_VUYA:
      ret.stride[0] = pitch;
      ret.offset[0] = 0;
      break;
    default:
      return FALSE;
  }

  *aligned = ret;

  return TRUE;
}

static size_t
do_align (size_t value, size_t align)
{
  if (align == 0)
    return value;

  return ((value + align - 1) / align) * align;
}

static GstMemory *
gst_hip_allocator_alloc_internal (GstHipAllocator * self,
    GstHipDevice * device, const GstVideoInfo * info,
    guint width_in_bytes, guint alloc_height)
{
  hipError_t hip_ret = hipSuccess;

  if (!gst_hip_device_set_current (device))
    return nullptr;

  auto vendor = gst_hip_device_get_vendor (device);
  gint texture_align = 0;
  gst_hip_device_get_attribute (device,
      hipDeviceAttributeTextureAlignment, &texture_align);
  if (texture_align <= 0)
    texture_align = 0;
  auto pitch = do_align (width_in_bytes, texture_align);

  void *data;
  hip_ret = HipMalloc (vendor, &data, pitch * alloc_height);
  if (!gst_hip_result (hip_ret, vendor)) {
    GST_ERROR_OBJECT (self, "Failed to allocate memory");
    return nullptr;
  }

  GstVideoInfo alloc_info;
  if (!gst_hip_allocator_update_info (info, pitch, alloc_height, &alloc_info)) {
    GST_ERROR_OBJECT (self, "Couldn't calculate aligned info");
    HipFree (vendor, data);
    return nullptr;
  }

  auto mem = g_new0 (GstHipMemory, 1);
  mem->device = (GstHipDevice *) gst_object_ref (device);
  mem->info = alloc_info;

  auto priv = new GstHipMemoryPrivate ();
  mem->priv = priv;

  priv->data = data;
  priv->pitch = pitch;
  priv->width_in_bytes = width_in_bytes;
  priv->height = alloc_height;
  priv->vendor = vendor;

  g_object_get (device, "texture2d-support", &priv->texture_support, nullptr);

  gst_memory_init (GST_MEMORY_CAST (mem), (GstMemoryFlags) 0,
      GST_ALLOCATOR_CAST (self), nullptr, alloc_info.size, 0, 0,
      alloc_info.size);

  return GST_MEMORY_CAST (mem);
}

static void
gst_hip_allocator_free (GstAllocator * allocator, GstMemory * mem)
{
  auto hmem = GST_HIP_MEMORY_CAST (mem);
  auto priv = hmem->priv;

  gst_hip_device_set_current (hmem->device);

  for (guint i = 0; i < 4; i++) {
    for (guint j = 0; j < N_TEX_ADDR_MODES; j++) {
      for (guint k = 0; k < N_TEX_FILTER_MODES; k++) {
        if (priv->texture[i][j][k]) {
          HipTexObjectDestroy (priv->vendor, priv->texture[i][j][k]);
        }
      }
    }
  }

  HipFree (priv->vendor, priv->data);

  if (priv->staging)
    HipHostFree (priv->vendor, priv->staging);

  gst_object_unref (hmem->device);

  delete hmem->priv;

  g_free (mem);
}

static gboolean
gst_hip_memory_upload (GstHipAllocator * self, GstHipMemory * mem)
{
  auto priv = mem->priv;
  hip_Memcpy2D param = { };

  if (!priv->staging ||
      !GST_MEMORY_FLAG_IS_SET (mem, GST_HIP_MEMORY_TRANSFER_NEED_UPLOAD)) {
    return TRUE;
  }

  if (!gst_hip_device_set_current (mem->device)) {
    GST_ERROR_OBJECT (self, "Failed to set device");
    return FALSE;
  }

  param.srcMemoryType = hipMemoryTypeHost;
  param.srcHost = priv->staging;
  param.srcPitch = priv->pitch;

  param.dstMemoryType = hipMemoryTypeDevice;
  param.dstDevice = priv->data;
  param.dstPitch = priv->pitch;
  param.WidthInBytes = priv->width_in_bytes;
  param.Height = priv->height;

  /* TODO use stream */
  auto hip_ret = HipMemcpyParam2DAsync (priv->vendor, &param, nullptr);
  if (gst_hip_result (hip_ret, priv->vendor))
    hip_ret = HipStreamSynchronize (priv->vendor, nullptr);

  GST_MEMORY_FLAG_UNSET (mem, GST_HIP_MEMORY_TRANSFER_NEED_UPLOAD);

  return gst_hip_result (hip_ret, priv->vendor);
}

static gboolean
gst_hip_memory_download (GstHipAllocator * self, GstHipMemory * mem)
{
  auto priv = mem->priv;
  hip_Memcpy2D param = { };

  if (!GST_MEMORY_FLAG_IS_SET (mem, GST_HIP_MEMORY_TRANSFER_NEED_DOWNLOAD))
    return TRUE;

  if (!gst_hip_device_set_current (mem->device)) {
    GST_ERROR_OBJECT (self, "Failed to push cuda context");
    return FALSE;
  }

  if (!priv->staging) {
    auto hip_ret = HipHostMalloc (priv->vendor,
        &priv->staging, GST_MEMORY_CAST (mem)->size, 0);

    if (!gst_hip_result (hip_ret, priv->vendor)) {
      GST_ERROR_OBJECT (self, "Failed to allocate staging memory");
      return FALSE;
    }
  }

  param.srcMemoryType = hipMemoryTypeDevice;
  param.srcDevice = priv->data;
  param.srcPitch = priv->pitch;

  param.dstMemoryType = hipMemoryTypeHost;
  param.dstHost = priv->staging;
  param.dstPitch = priv->pitch;
  param.WidthInBytes = priv->width_in_bytes;
  param.Height = priv->height;

  /* TODO use stream */
  auto hip_ret = HipMemcpyParam2DAsync (priv->vendor, &param, nullptr);
  if (gst_hip_result (hip_ret, priv->vendor))
    hip_ret = HipStreamSynchronize (priv->vendor, nullptr);

  GST_MEMORY_FLAG_UNSET (mem, GST_HIP_MEMORY_TRANSFER_NEED_DOWNLOAD);

  return gst_hip_result (hip_ret, priv->vendor);
}

static gpointer
hip_mem_map (GstMemory * mem, gsize maxsize, GstMapFlags flags)
{
  auto self = GST_HIP_ALLOCATOR (mem->allocator);
  auto hmem = GST_HIP_MEMORY_CAST (mem);
  auto priv = hmem->priv;

  std::lock_guard < std::mutex > lk (priv->lock);

  if ((flags & GST_MAP_HIP) == GST_MAP_HIP) {
    if (!gst_hip_memory_upload (self, hmem))
      return nullptr;

    if ((flags & GST_MAP_WRITE) != 0)
      GST_MINI_OBJECT_FLAG_SET (mem, GST_HIP_MEMORY_TRANSFER_NEED_DOWNLOAD);

    return priv->data;
  }

  /* First CPU access, must be downloaded */
  if (!priv->staging)
    GST_MINI_OBJECT_FLAG_SET (mem, GST_HIP_MEMORY_TRANSFER_NEED_DOWNLOAD);

  if (!gst_hip_memory_download (self, hmem))
    return nullptr;

  if ((flags & GST_MAP_WRITE) != 0)
    GST_MINI_OBJECT_FLAG_SET (mem, GST_HIP_MEMORY_TRANSFER_NEED_UPLOAD);

  return priv->staging;
}

static void
hip_mem_unmap (GstMemory * mem)
{
  /* Nothing to do */
}

static GstMemory *
hip_mem_copy (GstMemory * mem, gssize offset, gssize size)
{
  auto self = GST_HIP_ALLOCATOR (mem->allocator);
  auto src_mem = GST_HIP_MEMORY_CAST (mem);
  auto vendor = src_mem->priv->vendor;
  auto device = src_mem->device;
  GstMapInfo src_info, dst_info;
  hip_Memcpy2D param = { };
  GstMemory *copy = nullptr;

  /* non-zero offset or different size is not supported */
  if (offset != 0 || (size != -1 && (gsize) size != mem->size)) {
    GST_DEBUG_OBJECT (self, "Different size/offset, try fallback copy");
    return self->priv->fallback_copy (mem, offset, size);
  }

  if (GST_IS_HIP_POOL_ALLOCATOR (self)) {
    gst_hip_pool_allocator_acquire_memory (GST_HIP_POOL_ALLOCATOR (self),
        &copy);
  }

  if (!copy) {
    copy = gst_hip_allocator_alloc_internal (self, device,
        &src_mem->info, src_mem->priv->width_in_bytes, src_mem->priv->height);
  }

  if (!copy) {
    GST_ERROR_OBJECT (self, "Failed to allocate memory for copying");
    return nullptr;
  }

  if (!gst_memory_map (mem, &src_info, GST_MAP_READ_HIP)) {
    GST_ERROR_OBJECT (self, "Failed to map src memory");
    gst_memory_unref (copy);
    return nullptr;
  }

  if (!gst_memory_map (copy, &dst_info, GST_MAP_WRITE_HIP)) {
    GST_ERROR_OBJECT (self, "Failed to map dst memory");
    gst_memory_unmap (mem, &src_info);
    gst_memory_unref (copy);
    return nullptr;
  }

  if (!gst_hip_device_set_current (device)) {
    GST_ERROR_OBJECT (self, "Failed to set device");
    gst_memory_unmap (mem, &src_info);
    gst_memory_unmap (copy, &dst_info);

    return nullptr;
  }

  param.srcMemoryType = hipMemoryTypeDevice;
  param.srcDevice = src_info.data;
  param.srcPitch = src_mem->priv->pitch;

  param.dstMemoryType = hipMemoryTypeDevice;
  param.dstDevice = dst_info.data;
  param.dstPitch = src_mem->priv->pitch;
  param.WidthInBytes = src_mem->priv->width_in_bytes;
  param.Height = src_mem->priv->height;

  /* TODO: use stream */
  auto ret = HipMemcpyParam2DAsync (vendor, &param, nullptr);
  if (gst_hip_result (ret, vendor))
    ret = HipStreamSynchronize (vendor, nullptr);

  gst_memory_unmap (mem, &src_info);
  gst_memory_unmap (copy, &dst_info);

  if (!gst_hip_result (ret, vendor)) {
    GST_ERROR_OBJECT (self, "Failed to copy memory");
    gst_memory_unref (copy);
    return nullptr;
  }

  return copy;
}

void
gst_hip_memory_init_once (void)
{
  static std::once_flag once;

  std::call_once (once,[&] {
        _hip_memory_allocator =
        (GstHipAllocator *) g_object_new (GST_TYPE_HIP_ALLOCATOR, nullptr);
        gst_object_ref_sink (_hip_memory_allocator);
        gst_object_ref (_hip_memory_allocator);
        gst_allocator_register (GST_HIP_MEMORY_NAME,
            GST_ALLOCATOR_CAST (_hip_memory_allocator));
      });
}

gboolean
gst_is_hip_memory (GstMemory * mem)
{
  return mem != nullptr && mem->allocator != nullptr &&
      GST_IS_HIP_ALLOCATOR (mem->allocator);
}

typedef struct _TextureFormat
{
  GstVideoFormat format;
  hipArray_Format array_format[GST_VIDEO_MAX_COMPONENTS];
  guint channels[GST_VIDEO_MAX_COMPONENTS];
} TextureFormat;

#define HIP_AD_FORMAT_NONE ((hipArray_Format) 0)
#define MAKE_FORMAT_YUV_PLANAR(f,cf) \
  { GST_VIDEO_FORMAT_ ##f,  { HIP_AD_FORMAT_ ##cf, HIP_AD_FORMAT_ ##cf, \
      HIP_AD_FORMAT_ ##cf, HIP_AD_FORMAT_NONE },  {1, 1, 1, 0} }
#define MAKE_FORMAT_YUV_SEMI_PLANAR(f,cf) \
  { GST_VIDEO_FORMAT_ ##f,  { HIP_AD_FORMAT_ ##cf, HIP_AD_FORMAT_ ##cf, \
      HIP_AD_FORMAT_NONE, HIP_AD_FORMAT_NONE }, {1, 2, 0, 0} }
#define MAKE_FORMAT_RGB(f,cf) \
  { GST_VIDEO_FORMAT_ ##f,  { HIP_AD_FORMAT_ ##cf, HIP_AD_FORMAT_NONE, \
      HIP_AD_FORMAT_NONE, HIP_AD_FORMAT_NONE }, {4, 0, 0, 0} }
#define MAKE_FORMAT_RGBP(f,cf) \
  { GST_VIDEO_FORMAT_ ##f,  { HIP_AD_FORMAT_ ##cf, HIP_AD_FORMAT_ ##cf, \
      HIP_AD_FORMAT_ ##cf, HIP_AD_FORMAT_NONE }, {1, 1, 1, 0} }
#define MAKE_FORMAT_RGBAP(f,cf) \
  { GST_VIDEO_FORMAT_ ##f,  { HIP_AD_FORMAT_ ##cf, HIP_AD_FORMAT_ ##cf, \
      HIP_AD_FORMAT_ ##cf, HIP_AD_FORMAT_ ##cf }, {1, 1, 1, 1} }

static const TextureFormat format_map[] = {
  MAKE_FORMAT_YUV_PLANAR (I420, UNSIGNED_INT8),
  MAKE_FORMAT_YUV_PLANAR (YV12, UNSIGNED_INT8),
  MAKE_FORMAT_YUV_SEMI_PLANAR (NV12, UNSIGNED_INT8),
  MAKE_FORMAT_YUV_SEMI_PLANAR (NV21, UNSIGNED_INT8),
  MAKE_FORMAT_YUV_SEMI_PLANAR (P010_10LE, UNSIGNED_INT16),
  MAKE_FORMAT_YUV_SEMI_PLANAR (P012_LE, UNSIGNED_INT16),
  MAKE_FORMAT_YUV_SEMI_PLANAR (P016_LE, UNSIGNED_INT16),
  MAKE_FORMAT_YUV_PLANAR (I420_10LE, UNSIGNED_INT16),
  MAKE_FORMAT_YUV_PLANAR (I420_12LE, UNSIGNED_INT16),
  MAKE_FORMAT_YUV_PLANAR (Y444, UNSIGNED_INT8),
  MAKE_FORMAT_YUV_PLANAR (Y444_10LE, UNSIGNED_INT16),
  MAKE_FORMAT_YUV_PLANAR (Y444_12LE, UNSIGNED_INT16),
  MAKE_FORMAT_YUV_PLANAR (Y444_16LE, UNSIGNED_INT16),
  MAKE_FORMAT_RGB (RGBA, UNSIGNED_INT8),
  MAKE_FORMAT_RGB (BGRA, UNSIGNED_INT8),
  MAKE_FORMAT_RGB (RGBx, UNSIGNED_INT8),
  MAKE_FORMAT_RGB (BGRx, UNSIGNED_INT8),
  MAKE_FORMAT_RGB (ARGB, UNSIGNED_INT8),
  MAKE_FORMAT_RGB (ARGB64, UNSIGNED_INT16),
  MAKE_FORMAT_RGB (ABGR, UNSIGNED_INT8),
  MAKE_FORMAT_YUV_PLANAR (Y42B, UNSIGNED_INT8),
  MAKE_FORMAT_YUV_PLANAR (I422_10LE, UNSIGNED_INT16),
  MAKE_FORMAT_YUV_PLANAR (I422_12LE, UNSIGNED_INT16),
  MAKE_FORMAT_RGBP (RGBP, UNSIGNED_INT8),
  MAKE_FORMAT_RGBP (BGRP, UNSIGNED_INT8),
  MAKE_FORMAT_RGBP (GBR, UNSIGNED_INT8),
  MAKE_FORMAT_RGBP (GBR_10LE, UNSIGNED_INT16),
  MAKE_FORMAT_RGBP (GBR_12LE, UNSIGNED_INT16),
  MAKE_FORMAT_RGBP (GBR_16LE, UNSIGNED_INT16),
  MAKE_FORMAT_RGBAP (GBRA, UNSIGNED_INT8),
  MAKE_FORMAT_RGB (VUYA, UNSIGNED_INT8),
};

gboolean
gst_hip_memory_get_texture (GstHipMemory * mem, guint plane,
    guint filter_mode, guint address_mode, hipTextureObject_t * texture)
{
  g_return_val_if_fail (gst_is_hip_memory (GST_MEMORY_CAST (mem)), FALSE);
  g_return_val_if_fail (GST_VIDEO_INFO_N_PLANES (&mem->info) > plane, FALSE);
  g_return_val_if_fail (N_TEX_FILTER_MODES > filter_mode, FALSE);
  g_return_val_if_fail (N_TEX_ADDR_MODES > address_mode, FALSE);
  g_return_val_if_fail (texture, FALSE);

  auto priv = mem->priv;

  if (!priv->texture_support) {
    GST_WARNING_OBJECT (mem->device, "Texture not supported");
    return FALSE;
  }

  std::lock_guard < std::mutex > lk (priv->lock);
  if (priv->texture[plane][address_mode][filter_mode]) {
    *texture = priv->texture[plane][address_mode][filter_mode];
    return TRUE;
  }

  const TextureFormat *format = nullptr;
  for (guint i = 0; i < G_N_ELEMENTS (format_map); i++) {
    if (format_map[i].format == GST_VIDEO_INFO_FORMAT (&mem->info)) {
      format = &format_map[i];
      break;
    }
  }

  if (!format) {
    GST_WARNING_OBJECT (mem->device, "Not supported format %s",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&mem->info)));
    return FALSE;
  }

  if (!gst_hip_device_set_current (mem->device)) {
    GST_ERROR_OBJECT (mem->device, "Couldn't set current");
    return FALSE;
  }

  auto src_ptr = ((guint8 *) priv->data) + mem->info.offset[plane];

  HIP_RESOURCE_DESC res_desc = { };
  HIP_TEXTURE_DESC tex_desc = { };

  res_desc.resType = HIP_RESOURCE_TYPE_PITCH2D;
  res_desc.res.pitch2D.format = format->array_format[plane];
  res_desc.res.pitch2D.numChannels = format->channels[plane];
  res_desc.res.pitch2D.width = GST_VIDEO_INFO_COMP_WIDTH (&mem->info, plane);
  res_desc.res.pitch2D.height = GST_VIDEO_INFO_COMP_HEIGHT (&mem->info, plane);
  res_desc.res.pitch2D.pitchInBytes =
      GST_VIDEO_INFO_PLANE_STRIDE (&mem->info, plane);
  res_desc.res.pitch2D.devPtr = src_ptr;

  tex_desc.filterMode = (HIPfilter_mode) filter_mode;
  /* Will read texture value as a normalized [0, 1] float value
   * with [0, 1) coordinates */
  tex_desc.flags = HIP_TRSF_NORMALIZED_COORDINATES;
  tex_desc.addressMode[0] = (HIPaddress_mode) address_mode;
  tex_desc.addressMode[1] = (HIPaddress_mode) address_mode;
  tex_desc.addressMode[2] = (HIPaddress_mode) address_mode;

  hipTextureObject_t tex_obj;
  auto hip_ret =
      HipTexObjectCreate (priv->vendor, &tex_obj, &res_desc, &tex_desc,
      nullptr);
  if (!gst_hip_result (hip_ret, priv->vendor)) {
    GST_ERROR_OBJECT (mem->device, "Couldn't create texture object");
    return FALSE;
  }

  priv->texture[plane][address_mode][filter_mode] = tex_obj;

  *texture = tex_obj;

  return TRUE;
}

static guint
gst_hip_allocator_calculate_alloc_height (const GstVideoInfo * info)
{
  guint alloc_height;

  alloc_height = GST_VIDEO_INFO_HEIGHT (info);

  /* make sure valid height for subsampled formats */
  switch (GST_VIDEO_INFO_FORMAT (info)) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_P010_10LE:
    case GST_VIDEO_FORMAT_P012_LE:
    case GST_VIDEO_FORMAT_P016_LE:
    case GST_VIDEO_FORMAT_I420_10LE:
    case GST_VIDEO_FORMAT_I420_12LE:
      alloc_height = GST_ROUND_UP_2 (alloc_height);
      break;
    default:
      break;
  }

  switch (GST_VIDEO_INFO_FORMAT (info)) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_I420_10LE:
    case GST_VIDEO_FORMAT_I420_12LE:
      alloc_height *= 2;
      break;
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
    case GST_VIDEO_FORMAT_P010_10LE:
    case GST_VIDEO_FORMAT_P012_LE:
    case GST_VIDEO_FORMAT_P016_LE:
      alloc_height += alloc_height / 2;
      break;
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_I422_10LE:
    case GST_VIDEO_FORMAT_I422_12LE:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y444_10LE:
    case GST_VIDEO_FORMAT_Y444_12LE:
    case GST_VIDEO_FORMAT_Y444_16LE:
    case GST_VIDEO_FORMAT_RGBP:
    case GST_VIDEO_FORMAT_BGRP:
    case GST_VIDEO_FORMAT_GBR:
    case GST_VIDEO_FORMAT_GBR_10LE:
    case GST_VIDEO_FORMAT_GBR_12LE:
    case GST_VIDEO_FORMAT_GBR_16LE:
      alloc_height *= 3;
      break;
    case GST_VIDEO_FORMAT_GBRA:
      alloc_height *= 4;
      break;
    default:
      break;
  }

  return alloc_height;
}

GstMemory *
gst_hip_allocator_alloc (GstHipAllocator * allocator,
    GstHipDevice * device, const GstVideoInfo * info)
{
  guint alloc_height;

  g_return_val_if_fail (GST_IS_HIP_DEVICE (device), nullptr);
  g_return_val_if_fail (info, nullptr);

  if (!allocator)
    allocator = (GstHipAllocator *) _hip_memory_allocator;

  alloc_height = gst_hip_allocator_calculate_alloc_height (info);

  return gst_hip_allocator_alloc_internal (allocator, device,
      info, info->stride[0], alloc_height);
}

gboolean
gst_hip_allocator_set_active (GstHipAllocator * allocator, gboolean active)
{
  g_return_val_if_fail (GST_IS_HIP_ALLOCATOR (allocator), FALSE);

  auto klass = GST_HIP_ALLOCATOR_GET_CLASS (allocator);
  if (klass->set_active)
    return klass->set_active (allocator, active);

  return TRUE;
}

struct _GstHipPoolAllocatorPrivate
{
  std::queue < GstMemory * >queue;

  std::mutex lock;
  std::condition_variable cond;
  gboolean started = FALSE;
  gboolean active = FALSE;

  guint outstanding = 0;
  guint cur_mems = 0;
  guint alloc_height;
  gboolean flushing = FALSE;
};

static void gst_hip_pool_allocator_finalize (GObject * object);

static gboolean
gst_hip_pool_allocator_set_active (GstHipAllocator * allocator,
    gboolean active);

static gboolean gst_hip_pool_allocator_start (GstHipPoolAllocator * self);
static gboolean gst_hip_pool_allocator_stop (GstHipPoolAllocator * self);
static gboolean gst_hip_memory_release (GstMiniObject * obj);

#define gst_hip_pool_allocator_parent_class pool_alloc_parent_class
G_DEFINE_TYPE (GstHipPoolAllocator, gst_hip_pool_allocator,
    GST_TYPE_HIP_ALLOCATOR);

static void
gst_hip_pool_allocator_class_init (GstHipPoolAllocatorClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto hipalloc_class = GST_HIP_ALLOCATOR_CLASS (klass);

  object_class->finalize = gst_hip_pool_allocator_finalize;
  hipalloc_class->set_active = gst_hip_pool_allocator_set_active;
}

static void
gst_hip_pool_allocator_init (GstHipPoolAllocator * self)
{
  self->priv = new GstHipPoolAllocatorPrivate ();
}

static void
gst_hip_pool_allocator_finalize (GObject * object)
{
  auto self = GST_HIP_POOL_ALLOCATOR (object);

  GST_DEBUG_OBJECT (self, "Finalize");

  gst_hip_pool_allocator_stop (self);
  delete self->priv;

  g_clear_object (&self->device);

  G_OBJECT_CLASS (pool_alloc_parent_class)->finalize (object);
}

static gboolean
gst_hip_pool_allocator_start (GstHipPoolAllocator * self)
{
  auto priv = self->priv;

  priv->started = TRUE;
  return TRUE;
}

static gboolean
gst_hip_pool_allocator_set_active (GstHipAllocator * allocator, gboolean active)
{
  auto self = GST_HIP_POOL_ALLOCATOR (allocator);
  auto priv = self->priv;

  GST_LOG_OBJECT (self, "active %d", active);

  std::unique_lock < std::mutex > lk (priv->lock);
  /* just return if we are already in the right state */
  if (priv->active == active) {
    GST_LOG_OBJECT (self, "allocator was in the right state");
    return TRUE;
  }

  if (active) {
    if (!gst_hip_pool_allocator_start (self)) {
      GST_ERROR_OBJECT (self, "start failed");
      return FALSE;
    }

    priv->active = TRUE;
    priv->flushing = FALSE;
  } else {
    priv->flushing = TRUE;
    priv->active = FALSE;

    priv->cond.notify_all ();

    /* when all memory objects are in the pool, free them. Else they will be
     * freed when they are released */
    GST_LOG_OBJECT (self, "outstanding memories %d, (in queue %u)",
        priv->outstanding, (guint) priv->queue.size ());
    if (priv->outstanding == 0) {
      if (!gst_hip_pool_allocator_stop (self)) {
        GST_ERROR_OBJECT (self, "stop failed");
        return FALSE;
      }
    }
  }

  return TRUE;
}

static void
gst_hip_pool_allocator_free_memory (GstHipPoolAllocator * self, GstMemory * mem)
{
  auto priv = self->priv;

  priv->cur_mems--;
  GST_LOG_OBJECT (self, "freeing memory %p (%u left)", mem, priv->cur_mems);

  GST_MINI_OBJECT_CAST (mem)->dispose = nullptr;
  gst_memory_unref (mem);
}

/* must be called with the lock */
static void
gst_hip_pool_allocator_clear_queue (GstHipPoolAllocator * self)
{
  auto priv = self->priv;

  GST_LOG_OBJECT (self, "Clearing queue");

  while (!priv->queue.empty ()) {
    GstMemory *mem = priv->queue.front ();
    priv->queue.pop ();
    gst_hip_pool_allocator_free_memory (self, mem);
  }

  GST_LOG_OBJECT (self, "Clear done");
}

/* must be called with the lock */
static gboolean
gst_hip_pool_allocator_stop (GstHipPoolAllocator * self)
{
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Stop");

  if (priv->started) {
    gst_hip_pool_allocator_clear_queue (self);
    priv->started = FALSE;
  }

  return TRUE;
}

static void
gst_hip_pool_allocator_release_memory (GstHipPoolAllocator * self,
    GstMemory * mem)
{
  auto priv = self->priv;

  GST_LOG_OBJECT (self, "Released memory %p", mem);

  GST_MINI_OBJECT_CAST (mem)->dispose = nullptr;
  mem->allocator = (GstAllocator *) gst_object_ref (_hip_memory_allocator);

  /* keep it around in our queue */
  priv->queue.push (mem);
  priv->outstanding--;
  if (priv->outstanding == 0 && priv->flushing)
    gst_hip_pool_allocator_stop (self);
  priv->cond.notify_all ();
  priv->lock.unlock ();

  gst_object_unref (self);
}

static gboolean
gst_hip_memory_release (GstMiniObject * obj)
{
  GstMemory *mem = GST_MEMORY_CAST (obj);

  g_assert (mem->allocator);

  if (!GST_IS_HIP_POOL_ALLOCATOR (mem->allocator)) {
    GST_LOG_OBJECT (mem->allocator, "Not our memory, free");
    return TRUE;
  }

  auto self = GST_HIP_POOL_ALLOCATOR (mem->allocator);
  auto priv = self->priv;

  priv->lock.lock ();
  /* return the memory to the allocator */
  gst_memory_ref (mem);
  gst_hip_pool_allocator_release_memory (self, mem);

  return FALSE;
}

static GstFlowReturn
gst_hip_pool_allocator_alloc (GstHipPoolAllocator * self, GstMemory ** mem)
{
  auto priv = self->priv;

  auto new_mem = gst_hip_allocator_alloc_internal (_hip_memory_allocator,
      self->device, &self->info, self->info.stride[0], priv->alloc_height);

  if (!new_mem) {
    GST_ERROR_OBJECT (self, "Failed to allocate new memory");
    return GST_FLOW_ERROR;
  }

  priv->cur_mems++;
  *mem = new_mem;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_hip_pool_allocator_acquire_memory_internal (GstHipPoolAllocator * self,
    GstMemory ** memory, std::unique_lock < std::mutex > &lk)
{
  auto priv = self->priv;
  GstFlowReturn ret = GST_FLOW_ERROR;

  do {
    if (priv->flushing) {
      GST_DEBUG_OBJECT (self, "we are flushing");
      return GST_FLOW_FLUSHING;
    }

    if (!priv->queue.empty ()) {
      *memory = priv->queue.front ();
      priv->queue.pop ();
      GST_LOG_OBJECT (self, "acquired memory %p", *memory);
      return GST_FLOW_OK;
    }

    /* no memory, try to allocate some more */
    GST_LOG_OBJECT (self, "no memory, trying to allocate");
    ret = gst_hip_pool_allocator_alloc (self, memory);
    if (ret == GST_FLOW_OK)
      return ret;

    /* something went wrong, return error */
    if (ret != GST_FLOW_EOS)
      break;

    GST_LOG_OBJECT (self, "waiting for free memory or flushing");
    priv->cond.wait (lk);
  } while (TRUE);

  return ret;
}

GstHipPoolAllocator *
gst_hip_pool_allocator_new (GstHipDevice * device, const GstVideoInfo * info,
    GstStructure * config)
{
  g_return_val_if_fail (GST_IS_HIP_DEVICE (device), nullptr);
  g_return_val_if_fail (info, nullptr);

  auto self = (GstHipPoolAllocator *) g_object_new (GST_TYPE_HIP_POOL_ALLOCATOR,
      nullptr);
  gst_object_ref_sink (self);

  self->device = (GstHipDevice *) gst_object_ref (device);
  self->info = *info;

  self->priv->alloc_height = gst_hip_allocator_calculate_alloc_height (info);

  return self;
}

GstFlowReturn
gst_hip_pool_allocator_acquire_memory (GstHipPoolAllocator * allocator,
    GstMemory ** memory)
{
  g_return_val_if_fail (GST_IS_HIP_POOL_ALLOCATOR (allocator), GST_FLOW_ERROR);
  g_return_val_if_fail (memory, GST_FLOW_ERROR);
  GstFlowReturn ret;

  auto priv = allocator->priv;

  std::unique_lock < std::mutex > lk (priv->lock);
  ret = gst_hip_pool_allocator_acquire_memory_internal (allocator, memory, lk);

  if (ret == GST_FLOW_OK) {
    GstMemory *mem = *memory;
    /* Replace default allocator with ours */
    gst_object_unref (mem->allocator);
    mem->allocator = (GstAllocator *) gst_object_ref (allocator);
    GST_MINI_OBJECT_CAST (mem)->dispose = gst_hip_memory_release;
    priv->outstanding++;
  }

  return ret;
}
