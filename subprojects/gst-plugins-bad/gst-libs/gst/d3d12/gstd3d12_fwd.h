/* GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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
#include <gst/d3d12/d3d12-prelude.h>

#ifndef INITGUID
#include <initguid.h>
#endif

#if defined(BUILDING_GST_D3D12) || defined(GST_D3D12_USE_DIRECTX_HEADERS)
  #include <directx/d3d12.h>
  #include <directx/d3d12video.h>
  #ifdef __cplusplus
    #include <dxguids/dxguids.h>
  #endif /* __cplusplus */
#else
  #include <d3d12.h>
  #include <d3d12video.h>
#endif

#include <dxgi1_6.h>
#include <d3d11.h>

G_BEGIN_DECLS

typedef struct _GstD3D12Device GstD3D12Device;
typedef struct _GstD3D12DeviceClass GstD3D12DeviceClass;
typedef struct _GstD3D12DevicePrivate GstD3D12DevicePrivate;

typedef struct _GstD3D12Memory GstD3D12Memory;
typedef struct _GstD3D12MemoryPrivate GstD3D12MemoryPrivate;

typedef struct _GstD3D12StagingMemory GstD3D12StagingMemory;
typedef struct _GstD3D12StagingMemoryPrivate GstD3D12StagingMemoryPrivate;

typedef struct _GstD3D12Allocator GstD3D12Allocator;
typedef struct _GstD3D12AllocatorClass GstD3D12AllocatorClass;
typedef struct _GstD3D12AllocatorPrivate GstD3D12AllocatorPrivate;

typedef struct _GstD3D12PoolAllocator GstD3D12PoolAllocator;
typedef struct _GstD3D12PoolAllocatorClass GstD3D12PoolAllocatorClass;
typedef struct _GstD3D12PoolAllocatorPrivate GstD3D12PoolAllocatorPrivate;

typedef struct _GstD3D12StagingAllocator GstD3D12StagingAllocator;
typedef struct _GstD3D12StagingAllocatorClass GstD3D12StagingAllocatorClass;
typedef struct _GstD3D12StagingAllocatorPrivate GstD3D12StagingAllocatorPrivate;

typedef struct _GstD3D12Format GstD3D12Format;

typedef struct _GstD3D12AllocationParams GstD3D12AllocationParams;

typedef struct _GstD3D12BufferPool GstD3D12BufferPool;
typedef struct _GstD3D12BufferPoolClass GstD3D12BufferPoolClass;
typedef struct _GstD3D12BufferPoolPrivate GstD3D12BufferPoolPrivate;

typedef struct _GstD3D12StagingBufferPool GstD3D12StagingBufferPool;
typedef struct _GstD3D12StagingBufferPoolClass GstD3D12StagingBufferPoolClass;
typedef struct _GstD3D12StagingBufferPoolPrivate GstD3D12StagingBufferPoolPrivate;

typedef struct _GstD3D12CmdAllocPool GstD3D12CmdAllocPool;
typedef struct _GstD3D12CmdAllocPoolClass GstD3D12CmdAllocPoolClass;
typedef struct _GstD3D12CmdAllocPoolPrivate GstD3D12CmdAllocPoolPrivate;
typedef struct _GstD3D12CmdAlloc GstD3D12CmdAlloc;

typedef struct _GstD3D12CmdQueue GstD3D12CmdQueue;
typedef struct _GstD3D12CmdQueueClass GstD3D12CmdQueueClass;
typedef struct _GstD3D12CmdQueuePrivate GstD3D12CmdQueuePrivate;

typedef struct _GstD3D12Converter GstD3D12Converter;
typedef struct _GstD3D12ConverterClass GstD3D12ConverterClass;
typedef struct _GstD3D12ConverterPrivate GstD3D12ConverterPrivate;

typedef struct _GstD3D12DescHeapPool GstD3D12DescHeapPool;
typedef struct _GstD3D12DescHeapPoolClass GstD3D12DescHeapPoolClass;
typedef struct _GstD3D12DescHeapPoolPrivate GstD3D12DescHeapPoolPrivate;
typedef struct _GstD3D12DescHeap GstD3D12DescHeap;

typedef struct _GstD3D12FenceDataPool GstD3D12FenceDataPool;
typedef struct _GstD3D12FenceDataPoolClass GstD3D12FenceDataPoolClass;
typedef struct _GstD3D12FenceDataPoolPrivate GstD3D12FenceDataPoolPrivate;
typedef struct _GstD3D12FenceData GstD3D12FenceData;

typedef struct _GstD3D12Frame GstD3D12Frame;

G_END_DECLS

