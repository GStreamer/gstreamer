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
    "RGBA64_LE, RGB10A2_LE, Y410, VUYA, RGBA, BGRA, RBGA, P016_LE, P012_LE, " \
    "P010_10LE, RGBx, BGRx, NV12"

/* both SRV and RTV are supported */
#define GST_D3D12_TIER_1_FORMATS \
    "AYUV64, GBRA_12LE, GBRA_10LE, AYUV, ABGR, ARGB, GBRA, Y444_16LE, " \
    "GBR_16LE, Y444_12LE, GBR_12LE, I422_12LE, I420_12LE, Y444_10LE, GBR_10LE, " \
    "I422_10LE, I420_10LE, Y444, BGRP, GBR, RGBP, xBGR, xRGB, Y42B, NV21, " \
    "I420, YV12, GRAY16_LE, GRAY8"

#define GST_D3D12_COMMON_FORMATS \
    GST_D3D12_TIER_0_FORMATS ", " \
    GST_D3D12_TIER_1_FORMATS

#define GST_D3D12_ALL_FORMATS \
    "{ " GST_D3D12_COMMON_FORMATS " }"

#ifdef __cplusplus
#include <mutex>

#define GST_D3D12_CALL_ONCE_BEGIN \
    static std::once_flag __once_flag; \
    std::call_once (__once_flag, [&]()

#define GST_D3D12_CALL_ONCE_END )

#endif /* __cplusplus */

