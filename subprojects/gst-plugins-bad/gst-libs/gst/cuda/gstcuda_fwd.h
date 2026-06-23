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

#pragma once

#include <gst/gst.h>
#include <gst/cuda/cuda-prelude.h>
#include <cuda.h>

typedef struct _GstCudaContext GstCudaContext;
typedef struct _GstCudaContextClass GstCudaContextClass;
typedef struct _GstCudaContextPrivate GstCudaContextPrivate;

typedef struct _GstCudaStream GstCudaStream;
typedef struct _GstCudaStreamPrivate GstCudaStreamPrivate;

typedef struct _GstCudaMemory GstCudaMemory;
typedef struct _GstCudaMemoryPrivate GstCudaMemoryPrivate;

typedef struct _GstCudaAllocator GstCudaAllocator;
typedef struct _GstCudaAllocatorClass GstCudaAllocatorClass;
typedef struct _GstCudaAllocatorPrivate GstCudaAllocatorPrivate;

typedef struct _GstCudaPoolAllocator GstCudaPoolAllocator;
typedef struct _GstCudaPoolAllocatorClass GstCudaPoolAllocatorClass;
typedef struct _GstCudaPoolAllocatorPrivate GstCudaPoolAllocatorPrivate;

typedef struct _GstCudaMemoryPool GstCudaMemoryPool;
typedef struct _GstCudaMemoryPoolPrivate GstCudaMemoryPoolPrivate;

typedef struct _GstCudaBufferPool GstCudaBufferPool;
typedef struct _GstCudaBufferPoolClass GstCudaBufferPoolClass;
typedef struct _GstCudaBufferPoolPrivate GstCudaBufferPoolPrivate;

typedef struct _GstCudaConverter GstCudaConverter;
typedef struct _GstCudaConverterClass GstCudaConverterClass;
typedef struct _GstCudaConverterPrivate GstCudaConverterPrivate;

typedef struct _GstCudaAggregatorPad GstCudaAggregatorPad;
typedef struct _GstCudaAggregatorPadClass GstCudaAggregatorPadClass;
typedef struct _GstCudaAggregatorPadPrivate GstCudaAggregatorPadPrivate;

typedef struct _GstCudaAggregatorConvertPad GstCudaAggregatorConvertPad;
typedef struct _GstCudaAggregatorConvertPadClass GstCudaAggregatorConvertPadClass;
typedef struct _GstCudaAggregatorConvertPadPrivate GstCudaAggregatorConvertPadPrivate;

typedef struct _GstCudaAggregator GstCudaAggregator;
typedef struct _GstCudaAggregatorClass GstCudaAggregatorClass;
typedef struct _GstCudaAggregatorPrivate GstCudaAggregatorPrivate;

