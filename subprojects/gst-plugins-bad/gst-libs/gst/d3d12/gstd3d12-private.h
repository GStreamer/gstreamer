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

#pragma once

#include <gst/gst.h>
#include <gst/d3d12/gstd3d12.h>
#include <gst/d3d12/gstd3d12device-private.h>
#include <gst/d3d12/gstd3d12format-private.h>
#include <gst/d3d12/gstd3d12converter-private.h>
#include <gst/d3d12/gstd3d12commandqueue-private.h>
#include <gst/d3d12/gstd3d12compat.h>

/*
 * Preferred sorting order in a tier
 *   - number of components
 *   - depth
 *   - subsampling
 *   - supports both SRV and RTV
 *   - prefer smaller number of planes
 *   - prefer non-complex formats
 *   - prefer YUV formats over RGB ones
 *   - prefer I420 over YV12
 *   - format name
 */

/* DXGI (semi) native formats */
#define GST_D3D12_TIER_0_FORMATS \
    "RGBA64_LE, BGRA64_LE, Y416_LE, Y412_LE, RGB10A2_LE, Y410, BGR10A2_LE, Y216_LE, Y212_LE, " \
    "Y210, VUYA, RGBA, BGRA, RBGA, P016_LE, P012_LE, P010_10LE, RGBx, BGRx, " \
    "YUY2, NV12"

/* both SRV and RTV are supported */
#define GST_D3D12_TIER_1_FORMATS \
    "ARGB64_LE, AYUV64, GBRA_12LE, GBRA_10LE, AYUV, ABGR, ARGB, GBRA, Y444_16LE, " \
    "A444_16LE, A444_12LE, A444_10LE, A444, " \
    "A422_16LE, A422_12LE, A422_10LE, A422, A420_16LE, A420_12LE, A420_10LE, A420, AV12, " \
    "GBR_16LE, Y444_12LE, GBR_12LE, I422_12LE, I420_12LE, Y444_10LE, GBR_10LE, " \
    "I422_10LE, I420_10LE, Y444, BGRP, GBR, RGBP, xBGR, xRGB, Y42B, NV24, NV16, NV61, NV21, " \
    "I420, YV12, Y41B, YUV9, YVU9, GRAY16_LE, GRAY8"

/* pre/post processing required formats */
#define GST_D3D12_TIER_LAST_FORMATS \
    "v216, v210, r210, v308, IYU2, RGB, BGR, UYVY, VYUY, YVYU, RGB16, BGR16, " \
    "RGB15, BGR15"

#define GST_D3D12_COMMON_FORMATS \
    GST_D3D12_TIER_0_FORMATS ", " \
    GST_D3D12_TIER_1_FORMATS ", " \
    GST_D3D12_TIER_LAST_FORMATS

#define GST_D3D12_ALL_FORMATS \
    "{ " GST_D3D12_COMMON_FORMATS " }"

#ifdef __cplusplus
#include <mutex>

#define GST_D3D12_CALL_ONCE_BEGIN \
    static std::once_flag __once_flag; \
    std::call_once (__once_flag, [&]()

#define GST_D3D12_CALL_ONCE_END )

class GstD3D12Device11on12LockGuard
{
public:
  explicit GstD3D12Device11on12LockGuard(GstD3D12Device * device) : device_ (device)
  {
    if (device_)
      gst_d3d12_device_11on12_lock (device_);
  }

  ~GstD3D12Device11on12LockGuard()
  {
    if (device_)
      gst_d3d12_device_11on12_unlock (device_);
  }

  GstD3D12Device11on12LockGuard(const GstD3D12Device11on12LockGuard&) = delete;
  GstD3D12Device11on12LockGuard& operator=(const GstD3D12Device11on12LockGuard&) = delete;

private:
  GstD3D12Device *device_;
};

class GstD3D12DeviceDecoderLockGuard
{
public:
  explicit GstD3D12DeviceDecoderLockGuard(GstD3D12Device * device) : device_ (device)
  {
    if (device_)
      gst_d3d12_device_decoder_lock (device_);
  }

  ~GstD3D12DeviceDecoderLockGuard()
  {
    if (device_)
      gst_d3d12_device_decoder_unlock (device_);
  }

  GstD3D12DeviceDecoderLockGuard(const GstD3D12DeviceDecoderLockGuard&) = delete;
  GstD3D12DeviceDecoderLockGuard& operator=(const GstD3D12DeviceDecoderLockGuard&) = delete;

private:
  GstD3D12Device *device_;
};

static inline void
gst_d3d12_com_release (IUnknown * unknown)
{
  if (unknown)
    unknown->Release ();
}

#define FENCE_NOTIFY_COM(obj) \
    ((gpointer) (obj)), ((GDestroyNotify) gst_d3d12_com_release)

#define FENCE_NOTIFY_MINI_OBJECT(obj) \
    ((gpointer) (obj)), ((GDestroyNotify) gst_mini_object_unref)

#endif /* __cplusplus */
