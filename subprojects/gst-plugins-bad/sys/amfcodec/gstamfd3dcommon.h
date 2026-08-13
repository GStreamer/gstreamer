/* GStreamer
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

#pragma once

#include <gst/gst.h>
#include <gst/video/video.h>
#include <core/Context.h>
#include "gstamfutils.h"

#ifdef G_OS_WIN32
#include <gst/d3d11/gstd3d11.h>
#ifdef HAVE_GST_D3D12
#include <gst/d3d12/gstd3d12.h>
#endif

/* Shared D3D11/D3D12 interop helpers used by GstAmfEncoder and
 * GstAmfBaseFilter (VPP-style filters) to run either backend selected by
 * GstAmfApiState (see gstamfutils.h). See gstamfd3dcommon.cpp for the
 * documentation of each function below. */

typedef struct _GstAmfD3DPrivate
{
  gint64 adapter_luid;
  GstD3D11Device *d3d11_device;
  GstD3D11Fence *d3d11_fence;
#ifdef HAVE_GST_D3D12
  GstD3D12Device *d3d12_device;
  /* Cached and reused across frames by gst_amf_d3d12_submit_copy_regions().
   * Released in gst_amf_d3d_private_clear(). */
  GstD3D12CmdAllocPool *d3d12_cmd_alloc_pool;
  /* Reused across calls; only the allocator, not the list, needs the GPU
   * to be done before Reset(). Released in gst_amf_d3d_private_clear(). */
  ID3D12GraphicsCommandList *d3d12_cmd_list;
  /* Keeps each call's allocator (and optional src object) alive until the
   * GPU fence signals. Released in gst_amf_d3d_private_clear(). */
  GstD3D12FenceDataPool *d3d12_fence_data_pool;
#endif
} GstAmfD3DPrivate;

void gst_amf_d3d_private_init (GstAmfD3DPrivate * d3d);

void gst_amf_d3d_private_clear (GstAmfD3DPrivate * d3d);

void gst_amf_d3d_set_context (GstElement * element, GstContext * context,
    GstAmfApi active, GstAmfD3DPrivate * d3d);

gboolean gst_amf_d3d_handle_context_query (GstElement * element,
    GstQuery * query, GstAmfD3DPrivate * d3d);

gboolean gst_amf_d3d_open_context (GstElement * element,
    GstAmfApi configured, GstAmfD3DPrivate * d3d, amf::AMFContext * context);

GstBufferPool * gst_amf_d3d11_pool_new (GstD3D11Device * device,
    GstCaps * caps, const GstVideoInfo * info, UINT bind_flags,
    UINT misc_flags, guint * out_size, gboolean activate);

GstBuffer * gst_amf_d3d11_copy (GstElement * element, GstAmfD3DPrivate * d3d,
    GstBufferPool * dst_pool, GstBuffer * src_buf, gboolean shared);

gboolean gst_amf_d3d11_get_input_buffer (GstElement * element,
    GstAmfD3DPrivate * d3d, GstBufferPool * dst_pool, GstBuffer * inbuf,
    GstBuffer ** out_buf);

#ifdef HAVE_GST_D3D12
GstBufferPool * gst_amf_d3d12_pool_new (GstD3D12Device * device,
    GstCaps * caps, const GstVideoInfo * info,
    D3D12_RESOURCE_FLAGS resource_flags, D3D12_HEAP_FLAGS heap_flags,
    guint * out_size, gboolean activate);

gboolean gst_amf_d3d12_get_resource_amf_fence (ID3D12Resource * resource,
    ID3D12Fence ** fence, guint64 * fence_value);

/* One CopyTextureRegion() call's worth of src/dst subresource + region,
 * for gst_amf_d3d12_submit_copy_regions() below. */
typedef struct _GstAmfD3D12CopyRegion
{
  ID3D12Resource *dst_resource;
  guint dst_subresource;
  ID3D12Resource *src_resource;
  guint src_subresource;
  D3D12_BOX src_box;
} GstAmfD3D12CopyRegion;

gboolean gst_amf_d3d12_submit_copy_regions (GstElement * element,
    GstAmfD3DPrivate * d3d, const GstAmfD3D12CopyRegion * regions,
    guint num_regions, ID3D12Fence ** fences_to_wait,
    const guint64 * fence_values_to_wait, guint num_fences_to_wait,
    gpointer src_object, GDestroyNotify src_object_notify,
    ID3D12Fence ** out_fence, guint64 * out_fence_value);

GstBuffer * gst_amf_d3d12_copy (GstElement * element, GstAmfD3DPrivate * d3d,
    amf::AMFContext * context, const GstVideoInfo * info,
    GstBufferPool * dst_pool, GstBuffer * src_buf);

gboolean gst_amf_d3d12_get_input_buffer (GstElement * element,
    GstAmfD3DPrivate * d3d, amf::AMFContext * context,
    const GstVideoInfo * info, GstBufferPool * dst_pool, GstBuffer * inbuf,
    GstBuffer ** out_buf);
#endif /* HAVE_GST_D3D12 */

#endif /* G_OS_WIN32 */
