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

#pragma once

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstHipDevice GstHipDevice;
typedef struct _GstHipDeviceClass GstHipDeviceClass;
typedef struct _GstHipDevicePrivate GstHipDevicePrivate;

typedef struct _GstHipMemory GstHipMemory;
typedef struct _GstHipMemoryPrivate GstHipMemoryPrivate;

typedef struct _GstHipAllocator GstHipAllocator;
typedef struct _GstHipAllocatorClass GstHipAllocatorClass;
typedef struct _GstHipAllocatorPrivate GstHipAllocatorPrivate;

typedef struct _GstHipPoolAllocator GstHipPoolAllocator;
typedef struct _GstHipPoolAllocatorClass GstHipPoolAllocatorClass;
typedef struct _GstHipPoolAllocatorPrivate GstHipPoolAllocatorPrivate;

typedef struct _GstHipBufferPool GstHipBufferPool;
typedef struct _GstHipBufferPoolClass GstHipBufferPoolClass;
typedef struct _GstHipBufferPoolPrivate GstHipBufferPoolPrivate;

typedef struct _GstHipGraphicsResource GstHipGraphicsResource;

G_END_DECLS

