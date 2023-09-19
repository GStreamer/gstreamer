/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstd3d11pluginutils.h"

#include <windows.h>
#include <versionhelpers.h>

GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_plugin_utils_debug);
#define GST_CAT_DEFAULT gst_d3d11_plugin_utils_debug

/**
 * GstD3D11AlphaMode:
 *
 * Since: 1.24
 */
GType
gst_d3d11_alpha_mode_get_type (void)
{
  static GType type = 0;
  static const GEnumValue alpha_mode[] = {
    /**
     * GstD3D11AlphaMode::unspecified:
     *
     * Since: 1.24
     */
    {GST_D3D11_ALPHA_MODE_UNSPECIFIED, "Unspecified", "unspecified"},

    /**
     * GstD3D11AlphaMode::premultiplied:
     *
     * Since: 1.24
     */
    {GST_D3D11_ALPHA_MODE_PREMULTIPLIED, "Premultiplied", "premultiplied"},

    /**
     * GstD3D11AlphaMode::straight:
     *
     * Since: 1.24
     */
    {GST_D3D11_ALPHA_MODE_STRAIGHT, "Straight", "straight"},
    {0, nullptr, nullptr},
  };

  GST_D3D11_CALL_ONCE_BEGIN {
    type = g_enum_register_static ("GstD3D11AlphaMode", alpha_mode);
  } GST_D3D11_CALL_ONCE_END;

  return type;
}

/* Max Texture Dimension for feature level 11_0 ~ 12_1 */
static guint _gst_d3d11_texture_max_dimension = 16384;

void
gst_d3d11_plugin_utils_init (D3D_FEATURE_LEVEL feature_level)
{
  GST_D3D11_CALL_ONCE_BEGIN {
    /* https://docs.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-devices-downlevel-intro */
    if (feature_level >= D3D_FEATURE_LEVEL_11_0)
      _gst_d3d11_texture_max_dimension = 16384;
    else if (feature_level >= D3D_FEATURE_LEVEL_10_0)
      _gst_d3d11_texture_max_dimension = 8192;
    else
      _gst_d3d11_texture_max_dimension = 4096;
  }
  GST_D3D11_CALL_ONCE_END;
}

GstCaps *
gst_d3d11_get_updated_template_caps (GstStaticCaps * template_caps)
{
  GstCaps *caps;

  g_return_val_if_fail (template_caps != NULL, NULL);

  caps = gst_static_caps_get (template_caps);
  if (!caps) {
    GST_ERROR ("Couldn't get caps from static caps");
    return NULL;
  }

  caps = gst_caps_make_writable (caps);
  gst_caps_set_simple (caps,
      "width", GST_TYPE_INT_RANGE, 1, _gst_d3d11_texture_max_dimension,
      "height", GST_TYPE_INT_RANGE, 1, _gst_d3d11_texture_max_dimension, NULL);

  return caps;
}

gboolean
gst_d3d11_is_windows_8_or_greater (void)
{
  static gboolean ret = FALSE;

  GST_D3D11_CALL_ONCE_BEGIN {
#if (!GST_D3D11_WINAPI_ONLY_APP)
    if (IsWindows8OrGreater ())
      ret = TRUE;
#else
    ret = TRUE;
#endif
  } GST_D3D11_CALL_ONCE_END;

  return ret;
}

GstD3D11DeviceVendor
gst_d3d11_get_device_vendor_from_id (guint vendor_id)
{
  GstD3D11DeviceVendor vendor = GST_D3D11_DEVICE_VENDOR_UNKNOWN;

  switch (vendor_id) {
    case 0x1002:
    case 0x1022:
      vendor = GST_D3D11_DEVICE_VENDOR_AMD;
      break;
    case 0x8086:
      vendor = GST_D3D11_DEVICE_VENDOR_INTEL;
      break;
    case 0x10de:
      vendor = GST_D3D11_DEVICE_VENDOR_NVIDIA;
      break;
    case 0x4d4f4351:
      vendor = GST_D3D11_DEVICE_VENDOR_QUALCOMM;
      break;
    default:
      break;
  }

  return vendor;
}

GstD3D11DeviceVendor
gst_d3d11_get_device_vendor (GstD3D11Device * device)
{
  guint device_id = 0;
  guint vendor_id = 0;
  gchar *desc = nullptr;
  GstD3D11DeviceVendor vendor = GST_D3D11_DEVICE_VENDOR_UNKNOWN;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device),
      GST_D3D11_DEVICE_VENDOR_UNKNOWN);

  g_object_get (device, "device-id", &device_id, "vendor-id", &vendor_id,
      "description", &desc, nullptr);

  if (device_id == 0 && desc && g_strrstr (desc, "SraKmd"))
    vendor = GST_D3D11_DEVICE_VENDOR_XBOX;

  g_free (desc);

  if (vendor != GST_D3D11_DEVICE_VENDOR_UNKNOWN)
    return vendor;

  return gst_d3d11_get_device_vendor_from_id (vendor_id);
}

gboolean
gst_d3d11_hdr_meta_data_to_dxgi (GstVideoMasteringDisplayInfo * minfo,
    GstVideoContentLightLevel * cll, DXGI_HDR_METADATA_HDR10 * dxgi_hdr10)
{
  g_return_val_if_fail (dxgi_hdr10 != NULL, FALSE);

  memset (dxgi_hdr10, 0, sizeof (DXGI_HDR_METADATA_HDR10));

  if (minfo) {
    dxgi_hdr10->RedPrimary[0] = minfo->display_primaries[0].x;
    dxgi_hdr10->RedPrimary[1] = minfo->display_primaries[0].y;
    dxgi_hdr10->GreenPrimary[0] = minfo->display_primaries[1].x;
    dxgi_hdr10->GreenPrimary[1] = minfo->display_primaries[1].y;
    dxgi_hdr10->BluePrimary[0] = minfo->display_primaries[2].x;
    dxgi_hdr10->BluePrimary[1] = minfo->display_primaries[2].y;

    dxgi_hdr10->WhitePoint[0] = minfo->white_point.x;
    dxgi_hdr10->WhitePoint[1] = minfo->white_point.y;
    dxgi_hdr10->MaxMasteringLuminance = minfo->max_display_mastering_luminance;
    dxgi_hdr10->MinMasteringLuminance = minfo->min_display_mastering_luminance;
  }

  if (cll) {
    dxgi_hdr10->MaxContentLightLevel = cll->max_content_light_level;
    dxgi_hdr10->MaxFrameAverageLightLevel = cll->max_frame_average_light_level;
  }

  return TRUE;
}

gboolean
gst_d3d11_find_swap_chain_color_space (const GstVideoInfo * info,
    IDXGISwapChain3 * swapchain, DXGI_COLOR_SPACE_TYPE * color_space)
{
  UINT can_support = 0;
  HRESULT hr;
  DXGI_COLOR_SPACE_TYPE color;

  g_return_val_if_fail (info != NULL, FALSE);
  g_return_val_if_fail (swapchain != NULL, FALSE);
  g_return_val_if_fail (color_space != NULL, FALSE);

  if (!GST_VIDEO_INFO_IS_RGB (info)) {
    GST_WARNING ("Swapchain colorspace should be RGB format");
    return FALSE;
  }

  /* Select PQ color space only if input is also PQ */
  if (info->colorimetry.primaries == GST_VIDEO_COLOR_PRIMARIES_BT2020 &&
      info->colorimetry.transfer == GST_VIDEO_TRANSFER_SMPTE2084) {
    color = (DXGI_COLOR_SPACE_TYPE) 12;
    hr = swapchain->CheckColorSpaceSupport (color, &can_support);
    if (SUCCEEDED (hr) && can_support) {
      *color_space = color;
      return TRUE;
    }
  }

  /* otherwise use standard sRGB color space */
  color = (DXGI_COLOR_SPACE_TYPE) 0;
  hr = swapchain->CheckColorSpaceSupport (color, &can_support);
  if (SUCCEEDED (hr) && can_support) {
    *color_space = color;
    return TRUE;
  }

  return FALSE;
}

static void
fill_staging_desc (const D3D11_TEXTURE2D_DESC * ref,
    D3D11_TEXTURE2D_DESC * staging)
{
  memset (staging, 0, sizeof (D3D11_TEXTURE2D_DESC));

  staging->Width = ref->Width;
  staging->Height = ref->Height;
  staging->MipLevels = 1;
  staging->Format = ref->Format;
  staging->SampleDesc.Count = 1;
  staging->ArraySize = 1;
  staging->Usage = D3D11_USAGE_STAGING;
  staging->CPUAccessFlags = (D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE);
}

GstBuffer *
gst_d3d11_allocate_staging_buffer_for (GstBuffer * buffer,
    const GstVideoInfo * info, gboolean add_videometa)
{
  GstD3D11Memory *dmem;
  GstD3D11Device *device;
  GstBuffer *staging_buffer = NULL;
  gint stride[GST_VIDEO_MAX_PLANES] = { 0, };
  gsize offset[GST_VIDEO_MAX_PLANES] = { 0, };
  guint i;
  gsize size = 0;
  GstD3D11Format format;
  D3D11_TEXTURE2D_DESC desc;

  for (i = 0; i < gst_buffer_n_memory (buffer); i++) {
    GstMemory *mem = gst_buffer_peek_memory (buffer, i);

    if (!gst_is_d3d11_memory (mem)) {
      GST_DEBUG ("Not a d3d11 memory");

      return NULL;
    }
  }

  dmem = (GstD3D11Memory *) gst_buffer_peek_memory (buffer, 0);
  device = dmem->device;
  if (!gst_d3d11_device_get_format (device, GST_VIDEO_INFO_FORMAT (info),
          &format)) {
    GST_ERROR ("Unknown d3d11 format");
    return NULL;
  }

  staging_buffer = gst_buffer_new ();
  for (i = 0; i < gst_buffer_n_memory (buffer); i++) {
    D3D11_TEXTURE2D_DESC staging_desc;
    GstD3D11Memory *mem = (GstD3D11Memory *) gst_buffer_peek_memory (buffer, i);
    GstD3D11Memory *new_mem;

    guint cur_stride = 0;

    gst_d3d11_memory_get_texture_desc (mem, &desc);
    fill_staging_desc (&desc, &staging_desc);

    new_mem = (GstD3D11Memory *)
        gst_d3d11_allocator_alloc (nullptr, mem->device, &staging_desc);
    if (!new_mem) {
      GST_ERROR ("Failed to allocate memory");
      goto error;
    }

    if (!gst_d3d11_memory_get_resource_stride (new_mem, &cur_stride) ||
        cur_stride < staging_desc.Width) {
      GST_ERROR ("Failed to calculate memory size");
      gst_memory_unref (GST_MEMORY_CAST (mem));
      goto error;
    }

    offset[i] = size;
    stride[i] = cur_stride;
    size += GST_MEMORY_CAST (new_mem)->size;

    gst_buffer_append_memory (staging_buffer, GST_MEMORY_CAST (new_mem));
  }

  /* single texture semi-planar formats */
  if (format.dxgi_format != DXGI_FORMAT_UNKNOWN &&
      GST_VIDEO_INFO_N_PLANES (info) == 2) {
    stride[1] = stride[0];
    offset[1] = stride[0] * desc.Height;
  }

  gst_buffer_add_video_meta_full (staging_buffer, GST_VIDEO_FRAME_FLAG_NONE,
      GST_VIDEO_INFO_FORMAT (info), GST_VIDEO_INFO_WIDTH (info),
      GST_VIDEO_INFO_HEIGHT (info), GST_VIDEO_INFO_N_PLANES (info),
      offset, stride);

  return staging_buffer;

error:
  gst_clear_buffer (&staging_buffer);

  return NULL;
}

static gboolean
gst_d3d11_buffer_copy_into_fallback (GstBuffer * dst, GstBuffer * src,
    const GstVideoInfo * info)
{
  GstVideoFrame in_frame, out_frame;
  gboolean ret;

  if (!gst_video_frame_map (&in_frame, (GstVideoInfo *) info, src,
          (GstMapFlags) (GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF)))
    goto invalid_buffer;

  if (!gst_video_frame_map (&out_frame, (GstVideoInfo *) info, dst,
          (GstMapFlags) (GST_MAP_WRITE | GST_VIDEO_FRAME_MAP_FLAG_NO_REF))) {
    gst_video_frame_unmap (&in_frame);
    goto invalid_buffer;
  }

  ret = gst_video_frame_copy (&out_frame, &in_frame);

  gst_video_frame_unmap (&in_frame);
  gst_video_frame_unmap (&out_frame);

  return ret;

  /* ERRORS */
invalid_buffer:
  {
    GST_ERROR ("Invalid video buffer");
    return FALSE;
  }
}

gboolean
gst_d3d11_buffer_copy_into (GstBuffer * dst, GstBuffer * src,
    const GstVideoInfo * info)
{
  guint i;

  g_return_val_if_fail (GST_IS_BUFFER (dst), FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (src), FALSE);
  g_return_val_if_fail (info != NULL, FALSE);

  if (gst_buffer_n_memory (dst) != gst_buffer_n_memory (src)) {
    GST_LOG ("different memory layout, perform fallback copy");
    return gst_d3d11_buffer_copy_into_fallback (dst, src, info);
  }

  if (!gst_is_d3d11_buffer (dst) || !gst_is_d3d11_buffer (src)) {
    GST_LOG ("non-d3d11 memory, perform fallback copy");
    return gst_d3d11_buffer_copy_into_fallback (dst, src, info);
  }

  for (i = 0; i < gst_buffer_n_memory (dst); i++) {
    GstMemory *dst_mem, *src_mem;
    GstD3D11Memory *dst_dmem, *src_dmem;
    GstMapInfo dst_info;
    GstMapInfo src_info;
    ID3D11Resource *dst_texture, *src_texture;
    ID3D11DeviceContext *device_context;
    GstD3D11Device *device;
    D3D11_BOX src_box = { 0, };
    D3D11_TEXTURE2D_DESC dst_desc, src_desc;
    guint dst_subidx, src_subidx;

    dst_mem = gst_buffer_peek_memory (dst, i);
    src_mem = gst_buffer_peek_memory (src, i);

    dst_dmem = (GstD3D11Memory *) dst_mem;
    src_dmem = (GstD3D11Memory *) src_mem;

    device = dst_dmem->device;
    if (device != src_dmem->device) {
      GST_LOG ("different device, perform fallback copy");
      return gst_d3d11_buffer_copy_into_fallback (dst, src, info);
    }

    gst_d3d11_memory_get_texture_desc (dst_dmem, &dst_desc);
    gst_d3d11_memory_get_texture_desc (src_dmem, &src_desc);

    if (dst_desc.Format != src_desc.Format) {
      GST_WARNING ("different dxgi format");
      return FALSE;
    }

    device_context = gst_d3d11_device_get_device_context_handle (device);

    if (!gst_memory_map (dst_mem, &dst_info,
            (GstMapFlags) (GST_MAP_WRITE | GST_MAP_D3D11))) {
      GST_ERROR ("Cannot map dst d3d11 memory");
      return FALSE;
    }

    if (!gst_memory_map (src_mem, &src_info,
            (GstMapFlags) (GST_MAP_READ | GST_MAP_D3D11))) {
      GST_ERROR ("Cannot map src d3d11 memory");
      gst_memory_unmap (dst_mem, &dst_info);
      return FALSE;
    }

    dst_texture = (ID3D11Resource *) dst_info.data;
    src_texture = (ID3D11Resource *) src_info.data;

    /* src/dst texture size might be different if padding was used.
     * select smaller size */
    src_box.left = 0;
    src_box.top = 0;
    src_box.front = 0;
    src_box.back = 1;
    src_box.right = MIN (src_desc.Width, dst_desc.Width);
    src_box.bottom = MIN (src_desc.Height, dst_desc.Height);

    dst_subidx = gst_d3d11_memory_get_subresource_index (dst_dmem);
    src_subidx = gst_d3d11_memory_get_subresource_index (src_dmem);

    GstD3D11DeviceLockGuard lk (device);
    device_context->CopySubresourceRegion (dst_texture, dst_subidx, 0, 0, 0,
        src_texture, src_subidx, &src_box);

    gst_memory_unmap (src_mem, &src_info);
    gst_memory_unmap (dst_mem, &dst_info);
  }

  return TRUE;
}

gboolean
gst_is_d3d11_buffer (GstBuffer * buffer)
{
  guint i;
  guint size;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);

  size = gst_buffer_n_memory (buffer);
  if (size == 0)
    return FALSE;

  for (i = 0; i < size; i++) {
    GstMemory *mem = gst_buffer_peek_memory (buffer, i);

    if (!gst_is_d3d11_memory (mem))
      return FALSE;
  }

  return TRUE;
}

gboolean
gst_d3d11_buffer_can_access_device (GstBuffer * buffer, ID3D11Device * device)
{
  guint i;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);
  g_return_val_if_fail (device != NULL, FALSE);

  if (!gst_is_d3d11_buffer (buffer)) {
    GST_LOG ("Not a d3d11 buffer");
    return FALSE;
  }

  for (i = 0; i < gst_buffer_n_memory (buffer); i++) {
    GstD3D11Memory *mem = (GstD3D11Memory *) gst_buffer_peek_memory (buffer, i);
    ID3D11Device *handle;

    handle = gst_d3d11_device_get_device_handle (mem->device);
    if (handle != device) {
      GST_LOG ("D3D11 device is incompatible");
      return FALSE;
    }
  }

  return TRUE;
}

gboolean
gst_d3d11_buffer_map (GstBuffer * buffer, ID3D11Device * device,
    GstMapInfo info[GST_VIDEO_MAX_PLANES], GstMapFlags flags)
{
  GstMapFlags map_flags;
  guint num_mapped = 0;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);
  g_return_val_if_fail (info != NULL, FALSE);

  if (!gst_d3d11_buffer_can_access_device (buffer, device))
    return FALSE;

  map_flags = (GstMapFlags) (flags | GST_MAP_D3D11);

  for (num_mapped = 0; num_mapped < gst_buffer_n_memory (buffer); num_mapped++) {
    GstMemory *mem = gst_buffer_peek_memory (buffer, num_mapped);

    if (!gst_memory_map (mem, &info[num_mapped], map_flags)) {
      GST_ERROR ("Couldn't map memory");
      goto error;
    }
  }

  return TRUE;

error:
  {
    guint i;
    for (i = 0; i < num_mapped; i++) {
      GstMemory *mem = gst_buffer_peek_memory (buffer, i);
      gst_memory_unmap (mem, &info[i]);
    }

    return FALSE;
  }
}

gboolean
gst_d3d11_buffer_unmap (GstBuffer * buffer,
    GstMapInfo info[GST_VIDEO_MAX_PLANES])
{
  guint i;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);
  g_return_val_if_fail (info != NULL, FALSE);

  for (i = 0; i < gst_buffer_n_memory (buffer); i++) {
    GstMemory *mem = gst_buffer_peek_memory (buffer, i);

    gst_memory_unmap (mem, &info[i]);
  }

  return TRUE;
}

guint
gst_d3d11_buffer_get_shader_resource_view (GstBuffer * buffer,
    ID3D11ShaderResourceView * view[GST_VIDEO_MAX_PLANES])
{
  guint i;
  guint num_views = 0;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), 0);
  g_return_val_if_fail (view != NULL, 0);

  if (!gst_is_d3d11_buffer (buffer)) {
    GST_ERROR ("Buffer contains non-d3d11 memory");
    return 0;
  }

  for (i = 0; i < gst_buffer_n_memory (buffer); i++) {
    GstD3D11Memory *mem = (GstD3D11Memory *) gst_buffer_peek_memory (buffer, i);
    guint view_size;
    guint j;

    view_size = gst_d3d11_memory_get_shader_resource_view_size (mem);
    if (!view_size) {
      GST_LOG ("SRV is unavailable for memory index %d", i);
      return 0;
    }

    for (j = 0; j < view_size; j++) {
      if (num_views >= GST_VIDEO_MAX_PLANES) {
        GST_ERROR ("Too many SRVs");
        return 0;
      }

      view[num_views++] = gst_d3d11_memory_get_shader_resource_view (mem, j);
    }
  }

  return num_views;
}

guint
gst_d3d11_buffer_get_render_target_view (GstBuffer * buffer,
    ID3D11RenderTargetView * view[GST_VIDEO_MAX_PLANES])
{
  guint i;
  guint num_views = 0;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), 0);
  g_return_val_if_fail (view != NULL, 0);

  if (!gst_is_d3d11_buffer (buffer)) {
    GST_ERROR ("Buffer contains non-d3d11 memory");
    return 0;
  }

  for (i = 0; i < gst_buffer_n_memory (buffer); i++) {
    GstD3D11Memory *mem = (GstD3D11Memory *) gst_buffer_peek_memory (buffer, i);
    guint view_size;
    guint j;

    view_size = gst_d3d11_memory_get_render_target_view_size (mem);
    if (!view_size) {
      GST_LOG ("RTV is unavailable for memory index %d", i);
      return 0;
    }

    for (j = 0; j < view_size; j++) {
      if (num_views >= GST_VIDEO_MAX_PLANES) {
        GST_ERROR ("Too many RTVs");
        return 0;
      }

      view[num_views++] = gst_d3d11_memory_get_render_target_view (mem, j);
    }
  }

  return num_views;
}

GstBufferPool *
gst_d3d11_buffer_pool_new_with_options (GstD3D11Device * device,
    GstCaps * caps, GstD3D11AllocationParams * alloc_params,
    guint min_buffers, guint max_buffers)
{
  GstBufferPool *pool;
  GstStructure *config;
  GstVideoInfo info;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);
  g_return_val_if_fail (GST_IS_CAPS (caps), NULL);
  g_return_val_if_fail (alloc_params != NULL, NULL);

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (device, "invalid caps");
    return NULL;
  }

  pool = gst_d3d11_buffer_pool_new (device);
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config,
      caps, GST_VIDEO_INFO_SIZE (&info), min_buffers, max_buffers);

  gst_buffer_pool_config_set_d3d11_allocation_params (config, alloc_params);

  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (pool, "Couldn't set config");
    gst_object_unref (pool);
    return NULL;
  }

  return pool;
}
