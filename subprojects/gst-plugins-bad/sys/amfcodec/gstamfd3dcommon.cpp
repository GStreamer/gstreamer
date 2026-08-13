/* GStreamer
 * Copyright (C) 2022 Seungha Yang <seungha@centricular.com>
 * Copyright (C) 2026 Azat Nurgaliev <azat.nurg@gmail.com>
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

#include "gstamfd3dcommon.h"

#ifdef G_OS_WIN32

#include <core/Factory.h>
#include <core/D3D12AMF.h>
#include <wrl.h>
#include <mutex>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
using namespace amf;
/* *INDENT-ON* */

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category ()
static GstDebugCategory *
ensure_debug_category (void)
{
  static GstDebugCategory *cat = nullptr;
  static std::once_flag once_flag;

  /* *INDENT-OFF* */
  std::call_once (once_flag, [&]() {
    cat = _gst_debug_category_new ("amfd3dcommon",
        0, "AMF D3D11/D3D12 interop utils");
  });
  /* *INDENT-ON* */

  return cat;
}
#endif /* GST_DISABLE_GST_DEBUG */

void
gst_amf_d3d_private_init (GstAmfD3DPrivate * d3d)
{
  d3d->adapter_luid = 0;
  d3d->d3d11_device = nullptr;
  d3d->d3d11_fence = nullptr;
#ifdef HAVE_GST_D3D12
  d3d->d3d12_device = nullptr;
  d3d->d3d12_cmd_alloc_pool = nullptr;
  d3d->d3d12_fence_data_pool = nullptr;
  d3d->d3d12_cmd_list = nullptr;
#endif
}

void
gst_amf_d3d_private_clear (GstAmfD3DPrivate * d3d)
{
  gst_clear_d3d11_fence (&d3d->d3d11_fence);
  gst_clear_object (&d3d->d3d11_device);
#ifdef HAVE_GST_D3D12
  gst_clear_object (&d3d->d3d12_device);
  gst_clear_object (&d3d->d3d12_cmd_alloc_pool);
  gst_clear_object (&d3d->d3d12_fence_data_pool);
  if (d3d->d3d12_cmd_list) {
    d3d->d3d12_cmd_list->Release ();
    d3d->d3d12_cmd_list = nullptr;
  }
#endif
}

void
gst_amf_d3d_set_context (GstElement * element, GstContext * context,
    GstAmfApi active, GstAmfD3DPrivate * d3d)
{
#ifdef HAVE_GST_D3D12
  if (active == GST_AMF_API_D3D12) {
    gst_d3d12_handle_set_context_for_adapter_luid (element, context,
        d3d->adapter_luid, &d3d->d3d12_device);
    return;
  }
#endif
  gst_d3d11_handle_set_context_for_adapter_luid (element, context,
      d3d->adapter_luid, &d3d->d3d11_device);
}

/* GST_QUERY_CONTEXT handler for whichever of d3d12_device/d3d11_device
 * is set. */
gboolean
gst_amf_d3d_handle_context_query (GstElement * element, GstQuery * query,
    GstAmfD3DPrivate * d3d)
{
#ifdef HAVE_GST_D3D12
  if (gst_d3d12_handle_context_query (element, query, d3d->d3d12_device)) {
    return TRUE;
  }
#endif
  if (gst_d3d11_handle_context_query (element, query, d3d->d3d11_device)) {
    return TRUE;
  }

  return FALSE;
}

static gboolean
gst_amf_d3d11_open_context (GstElement * element, GstAmfD3DPrivate * d3d,
    AMFContext * context)
{
  AMF_RESULT result;
  ComPtr < ID3D10Multithread > multi_thread;
  ID3D11Device *device_handle;
  HRESULT hr;
  D3D_FEATURE_LEVEL feature_level;
  AMF_DX_VERSION dx_ver;

  if (!gst_d3d11_ensure_element_data_for_adapter_luid (element,
          d3d->adapter_luid, &d3d->d3d11_device)) {
    GST_ERROR_OBJECT (element, "d3d11 device is unavailable");
    return FALSE;
  }

  device_handle = gst_d3d11_device_get_device_handle (d3d->d3d11_device);
  feature_level = device_handle->GetFeatureLevel ();
  dx_ver = (feature_level >= D3D_FEATURE_LEVEL_11_1) ? AMF_DX11_1 : AMF_DX11_0;

  hr = device_handle->QueryInterface (IID_PPV_ARGS (&multi_thread));
  if (!gst_d3d11_result (hr, d3d->d3d11_device)) {
    GST_ERROR_OBJECT (element, "ID3D10Multithread interface is unavailable");
    goto error;
  }

  multi_thread->SetMultithreadProtected (TRUE);

  result = context->InitDX11 (device_handle, dx_ver);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (element, "Failed to init AMF DX11 context, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    goto error;
  }

  return TRUE;

error:
  gst_clear_object (&d3d->d3d11_device);
  return FALSE;
}

#ifdef HAVE_GST_D3D12
/* Same @context ownership contract as gst_amf_d3d11_open_context(). */
static gboolean
gst_amf_d3d12_open_context (GstElement * element, GstAmfD3DPrivate * d3d,
    AMFContext * context)
{
  AMF_RESULT result;
  ID3D12Device *device_handle;

  if (!gst_d3d12_ensure_element_data_for_adapter_luid (element,
          d3d->adapter_luid, &d3d->d3d12_device)) {
    GST_ERROR_OBJECT (element, "d3d12 device is unavailable");
    return FALSE;
  }

  device_handle = gst_d3d12_device_get_device_handle (d3d->d3d12_device);

  /* D3D12 command lists/queues are free-threaded by design; there is no
   * D3D12 equivalent of the ID3D10Multithread step DX11 needs above. */

  result = AMFContext2Ptr (context)->InitDX12 (device_handle, AMF_DX12);
  if (result != AMF_OK) {
    GST_ERROR_OBJECT (element, "Failed to init AMF DX12 context, result %"
        GST_AMF_RESULT_FORMAT ". This GPU/driver may not support AMF over "
        "DX12; try api=d3d11", GST_AMF_RESULT_ARGS (result));
    goto error;
  }

  return TRUE;

error:
  gst_clear_object (&d3d->d3d12_device);
  return FALSE;
}
#endif /* HAVE_GST_D3D12 */

/* Inits @context (already created by the caller via gst_amf_context_new())
 * for @configured against @d3d->adapter_luid. On failure, @context itself is
 * untouched */
gboolean
gst_amf_d3d_open_context (GstElement * element, GstAmfApi configured,
    GstAmfD3DPrivate * d3d, AMFContext * context)
{
  switch (configured) {
    case GST_AMF_API_AUTO:
#ifdef HAVE_GST_D3D12
      if (!gst_amf_d3d12_open_context (element, d3d, context)) {
        GST_WARNING_OBJECT (element, "This GPU adapter doesn't support D3D12 "
            "over AMF, falling back to D3D11");
      }
#endif
      /* fall through */
    case GST_AMF_API_D3D11:
      return gst_amf_d3d11_open_context (element, d3d, context);
    case GST_AMF_API_D3D12:
#ifdef HAVE_GST_D3D12
      return gst_amf_d3d12_open_context (element, d3d, context);
#else
      GST_ERROR_OBJECT (element, "This build was compiled without D3D12 "
          "support (gstd3d12-1.0 was not found)");
      return FALSE;
#endif
    default:
      g_assert_not_reached ();
      return FALSE;
  }
}

/* A configured + activated (if activate == TRUE) GstD3D11 buffer pool for
 * @info/@caps, or NULL on failure. */
GstBufferPool *
gst_amf_d3d11_pool_new (GstD3D11Device * device, GstCaps * caps,
    const GstVideoInfo * info, UINT bind_flags, UINT misc_flags,
    guint * out_size, gboolean activate)
{
  GstBufferPool *pool;
  GstStructure *config;
  GstD3D11AllocationParams *params;

  pool = gst_d3d11_buffer_pool_new (device);
  config = gst_buffer_pool_get_config (pool);
  params = gst_d3d11_allocation_params_new (device, info,
      GST_D3D11_ALLOCATION_FLAG_DEFAULT, bind_flags, misc_flags);
  gst_buffer_pool_config_set_d3d11_allocation_params (config, params);
  gst_d3d11_allocation_params_free (params);
  gst_buffer_pool_config_set_params (config, caps,
      GST_VIDEO_INFO_SIZE (info), 0, 0);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR ("Failed to set D3D11 pool config");
    gst_object_unref (pool);
    return nullptr;
  }

  if (activate && !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR ("Failed to activate D3D11 pool");
    gst_object_unref (pool);
    return nullptr;
  }

  if (out_size) {
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config, nullptr, out_size, nullptr,
        nullptr);
    gst_structure_free (config);
  }

  return pool;
}

/* GPU-to-GPU copy of a D3D11 texture from @src_buf into a fresh buffer
 * acquired from @dst_pool. @shared selects a DXGI-shared-handle copy
 * (source texture lives on a different ID3D11Device, same adapter) versus
 * a plain same-device copy (source texture has the wrong D3D11_USAGE /
 * bind flags for AMF to import directly). Returns NULL on failure. */
GstBuffer *
gst_amf_d3d11_copy (GstElement * element, GstAmfD3DPrivate * d3d,
    GstBufferPool * dst_pool, GstBuffer * src_buf, gboolean shared)
{
  GstBuffer *dst_buf;
  GstMapInfo src_info, dst_info;
  D3D11_TEXTURE2D_DESC src_desc, dst_desc;
  D3D11_BOX src_box;
  guint subresource_idx;
  HRESULT hr;
  ID3D11Texture2D *src_tex, *dst_tex;
  ID3D11Device *device_handle;
  ID3D11DeviceContext *device_context;
  ComPtr < IDXGIResource > dxgi_resource;
  ComPtr < ID3D11Texture2D > shared_texture;
  HANDLE shared_handle;
  GstMemory *src_mem, *dst_mem;
  GstD3D11Device *src_device;

  if (gst_buffer_pool_acquire_buffer (dst_pool, &dst_buf,
          nullptr) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (element, "Failed to acquire buffer from pool");
    return nullptr;
  }

  src_mem = gst_buffer_peek_memory (src_buf, 0);
  dst_mem = gst_buffer_peek_memory (dst_buf, 0);
  src_device = GST_D3D11_MEMORY_CAST (src_mem)->device;

  device_handle = gst_d3d11_device_get_device_handle (src_device);
  device_context = gst_d3d11_device_get_device_context_handle (src_device);

  if (!gst_memory_map (src_mem, &src_info,
          (GstMapFlags) (GST_MAP_READ | GST_MAP_D3D11))) {
    GST_ERROR_OBJECT (element, "Failed to map src D3D11 memory");
    gst_buffer_unref (dst_buf);
    return nullptr;
  }
  if (!gst_memory_map (dst_mem, &dst_info,
          (GstMapFlags) (GST_MAP_WRITE | GST_MAP_D3D11))) {
    GST_ERROR_OBJECT (element, "Failed to map dst D3D11 memory");
    gst_memory_unmap (src_mem, &src_info);
    gst_buffer_unref (dst_buf);
    return nullptr;
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
    if (!gst_d3d11_result (hr, d3d->d3d11_device))
      goto error;
    hr = dxgi_resource->GetSharedHandle (&shared_handle);
    if (!gst_d3d11_result (hr, d3d->d3d11_device))
      goto error;
    hr = device_handle->OpenSharedResource (shared_handle,
        IID_PPV_ARGS (&shared_texture));
    if (!gst_d3d11_result (hr, src_device))
      goto error;
    dst_tex = shared_texture.Get ();
  }

  src_box.left = 0;
  src_box.top = 0;
  src_box.front = 0;
  src_box.back = 1;
  src_box.right = MIN (src_desc.Width, dst_desc.Width);
  src_box.bottom = MIN (src_desc.Height, dst_desc.Height);

  gst_d3d11_device_lock (src_device);
  device_context->CopySubresourceRegion (dst_tex, 0, 0, 0, 0,
      src_tex, subresource_idx, &src_box);

  if (shared) {
    if (d3d->d3d11_fence && d3d->d3d11_fence->device != src_device)
      gst_clear_d3d11_fence (&d3d->d3d11_fence);
    if (!d3d->d3d11_fence)
      d3d->d3d11_fence = gst_d3d11_device_create_fence (src_device);
    if (!d3d->d3d11_fence) {
      GST_ERROR_OBJECT (element, "Couldn't create fence");
      gst_d3d11_device_unlock (src_device);
      goto error;
    }
    if (!gst_d3d11_fence_signal (d3d->d3d11_fence) ||
        !gst_d3d11_fence_wait (d3d->d3d11_fence)) {
      gst_d3d11_device_unlock (src_device);
      gst_clear_d3d11_fence (&d3d->d3d11_fence);
      goto error;
    }
  }
  gst_d3d11_device_unlock (src_device);

  gst_memory_unmap (src_mem, &src_info);
  gst_memory_unmap (dst_mem, &dst_info);
  return dst_buf;

error:
  gst_memory_unmap (src_mem, &src_info);
  gst_memory_unmap (dst_mem, &dst_info);
  gst_buffer_unref (dst_buf);
  return nullptr;
}

/* Resolves @inbuf into an AMF-importable D3D11 buffer (ref or copy via
 * @dst_pool). Returns FALSE with *out_buf untouched if @inbuf isn't
 * single-memory D3D11, is on a different GPU, or a needed copy failed
 * (caller falls back to host copy). @dst_pool == NULL forces FALSE in
 * every copy-needed case. */
gboolean
gst_amf_d3d11_get_input_buffer (GstElement * element, GstAmfD3DPrivate * d3d,
    GstBufferPool * dst_pool, GstBuffer * inbuf, GstBuffer ** out_buf)
{
  GstMemory *mem;
  GstD3D11Memory *dmem;
  D3D11_TEXTURE2D_DESC desc;
  GstBuffer *ret;

  *out_buf = nullptr;

  mem = gst_buffer_peek_memory (inbuf, 0);
  if (!gst_is_d3d11_memory (mem) || gst_buffer_n_memory (inbuf) > 1)
    return FALSE;

  dmem = GST_D3D11_MEMORY_CAST (mem);

  if (dmem->device != d3d->d3d11_device) {
    gint64 mem_luid;
    g_object_get (dmem->device, "adapter-luid", &mem_luid, nullptr);
    if (mem_luid != d3d->adapter_luid)
      return FALSE;             /* Different GPU */

    if (!dst_pool)
      return FALSE;

    GST_LOG_OBJECT (element,
        "Different D3D11 device, same GPU: cross-device copy");
    gst_d3d11_device_lock (d3d->d3d11_device);
    ret = gst_amf_d3d11_copy (element, d3d, dst_pool, inbuf, TRUE);
    gst_d3d11_device_unlock (d3d->d3d11_device);
    if (!ret)
      return FALSE;
    *out_buf = ret;
    return TRUE;
  }

  gst_d3d11_memory_get_texture_desc (dmem, &desc);
  if (desc.Usage != D3D11_USAGE_DEFAULT
      || (desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) == 0) {
    if (!dst_pool)
      return FALSE;
    GST_TRACE_OBJECT (element,
        "D3D11 texture wrong usage/bind flags, same-device copy");
    gst_d3d11_device_lock (d3d->d3d11_device);
    ret = gst_amf_d3d11_copy (element, d3d, dst_pool, inbuf, FALSE);
    gst_d3d11_device_unlock (d3d->d3d11_device);
    if (!ret)
      return FALSE;
    *out_buf = ret;
    return TRUE;
  }

  /* Direct zero-copy: just take a ref. */
  *out_buf = gst_buffer_ref (inbuf);
  return TRUE;
}

#ifdef HAVE_GST_D3D12
/* D3D12 sibling of gst_amf_d3d11_pool_new; see its @activate doc. */
GstBufferPool *
gst_amf_d3d12_pool_new (GstD3D12Device * device, GstCaps * caps,
    const GstVideoInfo * info, D3D12_RESOURCE_FLAGS resource_flags,
    D3D12_HEAP_FLAGS heap_flags, guint * out_size, gboolean activate)
{
  GstBufferPool *pool;
  GstStructure *config;
  GstD3D12AllocationParams *params;

  pool = gst_d3d12_buffer_pool_new (device);
  config = gst_buffer_pool_get_config (pool);
  params = gst_d3d12_allocation_params_new (device, info,
      GST_D3D12_ALLOCATION_FLAG_DEFAULT, resource_flags, heap_flags);
  gst_buffer_pool_config_set_d3d12_allocation_params (config, params);
  gst_d3d12_allocation_params_free (params);
  gst_buffer_pool_config_set_params (config, caps,
      GST_VIDEO_INFO_SIZE (info), 0, 0);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR ("Failed to set D3D12 pool config");
    gst_object_unref (pool);
    return nullptr;
  }

  if (activate && !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR ("Failed to activate D3D12 pool");
    gst_object_unref (pool);
    return nullptr;
  }

  if (out_size) {
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config, nullptr, out_size, nullptr,
        nullptr);
    gst_structure_free (config);
  }

  return pool;
}

/* Extracts AMF's own AMFFenceGUID/AMFFenceValueGUID pair off @resource,
 * FALSE if untagged. */
gboolean
gst_amf_d3d12_get_resource_amf_fence (ID3D12Resource * resource,
    ID3D12Fence ** fence, guint64 * fence_value)
{
  IUnknown *fence_unk = nullptr;
  UINT size = sizeof (fence_unk);
  ComPtr < ID3D12Fence > local_fence;
  guint64 value = 0;
  UINT value_size = sizeof (value);

  if (FAILED (resource->GetPrivateData (AMFFenceGUID, &size, &fence_unk)) ||
      !fence_unk)
    return FALSE;

  HRESULT hr = fence_unk->QueryInterface (IID_PPV_ARGS (&local_fence));
  fence_unk->Release ();
  if (FAILED (hr))
    return FALSE;

  if (FAILED (local_fence->GetPrivateData (AMFFenceValueGUID, &value_size,
              &value)))
    return FALSE;

  *fence = local_fence.Detach ();
  *fence_value = value;

  return TRUE;
}

/* Records @regions on the reused d3d->d3d12_cmd_list and submits it,
 * GPU-waiting on @fences_to_wait first. Fully async: no CPU wait here.
 * @out_fence/@out_fence_value (optional) report the completion fence for
 * the caller to attach elsewhere. The command allocator and (if non-NULL)
 * @src_object -- e.g. an AMF surface the caller wants pinned while the GPU
 * reads it -- are released via @src_object_notify once the fence signals. */
gboolean
gst_amf_d3d12_submit_copy_regions (GstElement * element,
    GstAmfD3DPrivate * d3d, const GstAmfD3D12CopyRegion * regions,
    guint num_regions, ID3D12Fence ** fences_to_wait,
    const guint64 * fence_values_to_wait, guint num_fences_to_wait,
    gpointer src_object, GDestroyNotify src_object_notify,
    ID3D12Fence ** out_fence, guint64 * out_fence_value)
{
  ID3D12Device *device_handle =
      gst_d3d12_device_get_device_handle (d3d->d3d12_device);
  GstD3D12CmdQueue *queue = gst_d3d12_device_get_cmd_queue (d3d->d3d12_device,
      D3D12_COMMAND_LIST_TYPE_DIRECT);
  GstD3D12CmdAlloc *cmd_alloc = nullptr;
  GstD3D12FenceData *fence_data = nullptr;
  ID3D12CommandAllocator *alloc_handle;
  ID3D12CommandList *cmd_lists[1];
  guint64 fence_value = 0;
  HRESULT hr;
  gboolean ret = FALSE;

  if (!queue) {
    GST_ERROR_OBJECT (element, "Couldn't get D3D12 command queue");
    goto out;
  }

  if (!d3d->d3d12_cmd_alloc_pool) {
    d3d->d3d12_cmd_alloc_pool = gst_d3d12_cmd_alloc_pool_new (device_handle,
        D3D12_COMMAND_LIST_TYPE_DIRECT);
  }

  if (!d3d->d3d12_cmd_alloc_pool ||
      !gst_d3d12_cmd_alloc_pool_acquire (d3d->d3d12_cmd_alloc_pool,
          &cmd_alloc)) {
    GST_ERROR_OBJECT (element, "Failed to acquire command allocator");
    goto out;
  }

  /* gst_d3d12_cmd_alloc_pool_acquire() hands back a recycled allocator
   * as-is -- reclaim its memory (no-op cost on a brand new one) before
   * reusing it, which callers are expected to do themselves. */
  alloc_handle = gst_d3d12_cmd_alloc_get_handle (cmd_alloc);
  hr = alloc_handle->Reset ();
  if (!gst_d3d12_result (hr, d3d->d3d12_device)) {
    GST_ERROR_OBJECT (element, "Failed to reset command allocator, hr: 0x%x",
        (guint) hr);
    goto out;
  }

  /* Reused across calls -- Reset() with this call's own allocator is safe
   * even if the GPU is still executing a previous submission recorded on
   * this same list (see the field comment on d3d12_cmd_list). */
  if (!d3d->d3d12_cmd_list) {
    hr = device_handle->CreateCommandList (0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        alloc_handle, nullptr, IID_PPV_ARGS (&d3d->d3d12_cmd_list));
    if (!gst_d3d12_result (hr, d3d->d3d12_device)) {
      GST_ERROR_OBJECT (element, "Failed to create command list, hr: 0x%x",
          (guint) hr);
      goto out;
    }
  } else {
    hr = d3d->d3d12_cmd_list->Reset (alloc_handle, nullptr);
    if (!gst_d3d12_result (hr, d3d->d3d12_device)) {
      GST_ERROR_OBJECT (element, "Failed to reset command list, hr: 0x%x",
          (guint) hr);
      goto out;
    }
  }

  if (!d3d->d3d12_fence_data_pool)
    d3d->d3d12_fence_data_pool = gst_d3d12_fence_data_pool_new ();

  if (!d3d->d3d12_fence_data_pool ||
      !gst_d3d12_fence_data_pool_acquire (d3d->d3d12_fence_data_pool,
          &fence_data)) {
    GST_ERROR_OBJECT (element, "Failed to acquire fence data");
    goto out;
  }

  for (guint i = 0; i < num_regions; i++) {
    D3D12_TEXTURE_COPY_LOCATION dst_loc = { };
    D3D12_TEXTURE_COPY_LOCATION src_loc = { };

    dst_loc.pResource = regions[i].dst_resource;
    dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst_loc.SubresourceIndex = regions[i].dst_subresource;

    src_loc.pResource = regions[i].src_resource;
    src_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src_loc.SubresourceIndex = regions[i].src_subresource;

    d3d->d3d12_cmd_list->CopyTextureRegion (&dst_loc, 0, 0, 0, &src_loc,
        &regions[i].src_box);
  }

  hr = d3d->d3d12_cmd_list->Close ();
  if (!gst_d3d12_result (hr, d3d->d3d12_device)) {
    GST_ERROR_OBJECT (element, "Failed to close command list, hr: 0x%x",
        (guint) hr);
    goto out;
  }

  cmd_lists[0] = d3d->d3d12_cmd_list;
  hr = gst_d3d12_cmd_queue_execute_command_lists_full (queue,
      num_fences_to_wait, fences_to_wait, fence_values_to_wait, 1, cmd_lists,
      &fence_value);
  if (!gst_d3d12_result (hr, d3d->d3d12_device)) {
    GST_ERROR_OBJECT (element, "Failed to execute command list, hr: 0x%x",
        (guint) hr);
    goto out;
  }

  /* Defer releasing cmd_alloc until the GPU fence signals, instead of
   * blocking here. d3d12_cmd_list itself is reused, not owned per-call. */
  gst_d3d12_fence_data_push (fence_data, cmd_alloc,
      (GDestroyNotify) gst_d3d12_cmd_alloc_unref);
  cmd_alloc = nullptr;
  if (src_object) {
    gst_d3d12_fence_data_push (fence_data, src_object, src_object_notify);
    src_object = nullptr;
  }
  gst_d3d12_cmd_queue_set_notify (queue, fence_value, fence_data,
      (GDestroyNotify) gst_d3d12_fence_data_unref);
  fence_data = nullptr;

  if (out_fence) {
    *out_fence = gst_d3d12_cmd_queue_get_fence_handle (queue);
    (*out_fence)->AddRef ();
  }
  if (out_fence_value)
    *out_fence_value = fence_value;

  ret = TRUE;

out:
  gst_clear_d3d12_cmd_alloc (&cmd_alloc);
  gst_clear_d3d12_fence_data (&fence_data);
  if (src_object && src_object_notify)
    src_object_notify (src_object);

  return ret;
}

/* GPU-waits AMF's native D3D12 command queue on whatever GStreamer fence is
 * currently tracked on @dmem (no-op if none), so AMF doesn't touch the
 * resource before GStreamer's last recorded writer is done with it. */
static gboolean
gst_amf_d3d12_wait_gst_fence_on_amf_queue (GstElement * element,
    AMFContext * context, GstD3D12Memory * dmem)
{
  ComPtr < ID3D12Fence > fence;
  guint64 value = 0;

  if (!gst_d3d12_memory_get_fence (dmem, &fence, &value))
    return TRUE;

  AMFComputePtr compute;
  ID3D12CommandQueue *queue = nullptr;

  context->GetCompute (AMF_MEMORY_DX12, &compute);
  if (compute)
    queue = (ID3D12CommandQueue *) compute->GetNativeCommandQueue ();

  if (!queue) {
    GST_ERROR_OBJECT (element, "Couldn't get AMF's native D3D12 "
        "command queue");
    return FALSE;
  }

  /* GPU wait of GStreamer's fence on AMF's Command Queue */
  queue->Wait (fence.Get (), value);
  return TRUE;
}

/* D3D12 sibling of gst_amf_d3d11_copy. Only ever called for a same-device
 * reshape copy (see gst_amf_d3d12_get_input_buffer()) -- cross-device/
 * cross-GPU copy isn't supported here, callers must fail over to a CPU
 * copy instead. */
GstBuffer *
gst_amf_d3d12_copy (GstElement * element, GstAmfD3DPrivate * d3d,
    AMFContext * context, const GstVideoInfo * info,
    GstBufferPool * dst_pool, GstBuffer * src_buf)
{
  GstBuffer *dst_buf;
  GstD3D12Memory *src_dmem, *dst_dmem;

  src_dmem = GST_D3D12_MEMORY_CAST (gst_buffer_peek_memory (src_buf, 0));

  /* Defensive: guard the same-device invariant explicitly rather than
   * relying on the caller, so a future/other caller can't silently record
   * a copy command against the wrong device's command list. */
  if (!gst_d3d12_device_is_equal (src_dmem->device, d3d->d3d12_device)) {
    GST_ERROR_OBJECT (element, "Source memory is on a different device");
    return nullptr;
  }

  if (gst_buffer_pool_acquire_buffer (dst_pool, &dst_buf,
          nullptr) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (element, "Failed to acquire buffer from pool");
    return nullptr;
  }

  /* Async GPU copy -- gst_d3d12_buffer_copy_into() attaches the completion
   * fence to dst_buf's memories itself, no CPU wait here. */
  if (!gst_d3d12_buffer_copy_into (dst_buf, src_buf, info)) {
    GST_ERROR_OBJECT (element, "Failed to copy buffer");
    gst_buffer_unref (dst_buf);
    return nullptr;
  }

  dst_dmem = GST_D3D12_MEMORY_CAST (gst_buffer_peek_memory (dst_buf, 0));
  if (!gst_amf_d3d12_wait_gst_fence_on_amf_queue (element, context, dst_dmem)) {
    gst_buffer_unref (dst_buf);
    return nullptr;
  }

  return dst_buf;
}

/* D3D12 sibling of gst_amf_d3d11_get_input_buffer. */
gboolean
gst_amf_d3d12_get_input_buffer (GstElement * element, GstAmfD3DPrivate * d3d,
    AMFContext * context, const GstVideoInfo * info,
    GstBufferPool * dst_pool, GstBuffer * inbuf, GstBuffer ** out_buf)
{
  GstMemory *mem;
  GstD3D12Memory *dmem;
  GstBuffer *ret;

  *out_buf = nullptr;

  mem = gst_buffer_peek_memory (inbuf, 0);
  if (!gst_is_d3d12_memory (mem) || gst_buffer_n_memory (inbuf) > 1)
    return FALSE;

  dmem = GST_D3D12_MEMORY_CAST (mem);

  if (!gst_d3d12_device_is_equal (dmem->device, d3d->d3d12_device))
    return FALSE;               /* Different GPU */

  {
    /* CreateSurfaceFromDX12Native / CopyTextureRegion both need a
     * GPU-local (DEFAULT heap) resource; GetHeapProperties() fails
     * outright for resources that weren't created via
     * CreateCommittedResource (e.g. placed/reserved resources), so treat
     * that failure the same as "wrong heap type" and reshape
     * defensively. GetHeapProperties() is pure resource metadata -- no
     * GPU residency/sync needed -- so grab the resource handle directly
     * rather than gst_memory_map()'ing it, which would trigger residency/
     * upload bookkeeping for no reason. */
    ID3D12Resource *resource = gst_d3d12_memory_get_resource_handle (dmem);
    D3D12_HEAP_PROPERTIES heap_props = { };
    D3D12_HEAP_FLAGS heap_flags = D3D12_HEAP_FLAG_NONE;
    HRESULT hr = resource->GetHeapProperties (&heap_props, &heap_flags);
    gboolean needs_reshape =
        !(SUCCEEDED (hr) && heap_props.Type == D3D12_HEAP_TYPE_DEFAULT);

    if (needs_reshape) {
      if (!dst_pool)
        return FALSE;
      GST_TRACE_OBJECT (element, "Not a default heap resource, d3d12 copy");
      ret = gst_amf_d3d12_copy (element, d3d, context, info, dst_pool, inbuf);
      if (!ret)
        return FALSE;
      *out_buf = ret;
      return TRUE;
    }
  }

  /* CreateSurfaceFromDX12Native() (the
   * only consumer of this ref -- see gstamfencoder.cpp/gstamfbasefilter.cpp)
   * takes no subresource parameter, so it can only correctly address array
   * slice 0 of whatever resource it's given, unlike the copy paths above
   * (gst_amf_d3d12_copy() is array-slice-correct via each memory's own
   * subresource index). Not reachable via any GStreamer D3D12 decoder
   * today, but detect and hard-fail rather than
   * silently read the wrong slice if some future/external producer ever
   * hands us one. Plane 0's subresource index equals the array slice
   * (D3D12CalcSubresource with planeSlice 0 collapses to just the array
   * slice), so checking it alone is sufficient. */
  {
    guint subresource_index = 0;

    if (!gst_d3d12_memory_get_subresource_index (dmem, 0, &subresource_index)) {
      GST_ERROR_OBJECT (element, "Couldn't get subresource index");
      return FALSE;
    }

    if (subresource_index != 0) {
      GST_ELEMENT_ERROR (element, STREAM, FORMAT, (NULL),
          ("D3D12 buffer references a non-zero array slice, which is not "
              "supported for zero-copy AMF interop"));
      return FALSE;
    }
  }

  if (!gst_amf_d3d12_wait_gst_fence_on_amf_queue (element, context, dmem))
    return FALSE;

  /* Direct zero-copy: just take a ref. */
  *out_buf = gst_buffer_ref (inbuf);
  return TRUE;
}

#endif /* HAVE_GST_D3D12 */

#endif /* G_OS_WIN32 */
