/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
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

#ifndef __GST_D3D11_FWD_H__
#define __GST_D3D11_FWD_H__

#include <gst/gst.h>

/* define COBJMACROS to use d3d11 C APIs */
#ifndef COBJMACROS
#define COBJMACROS
#endif

#ifndef INITGUID
#include <initguid.h>
#endif

#include <d3d11.h>
#ifdef HAVE_DXGI_1_5_H
#include <dxgi1_5.h>
#else
#include <dxgi.h>
#endif

G_BEGIN_DECLS

typedef struct _GstD3D11Device GstD3D11Device;
typedef struct _GstD3D11DeviceClass GstD3D11DeviceClass;
typedef struct _GstD3D11DevicePrivate GstD3D11DevicePrivate;

typedef struct _GstD3D11AllocationParams GstD3D11AllocationParams;
typedef struct _GstD3D11Memory GstD3D11Memory;
typedef struct _GstD3D11Allocator GstD3D11Allocator;
typedef struct _GstD3D11AllocatorClass GstD3D11AllocatorClass;
typedef struct _GstD3D11AllocatorPrivate GstD3D11AllocatorPrivate;

typedef struct _GstD3D11BufferPool GstD3D11BufferPool;
typedef struct _GstD3D11BufferPoolClass GstD3D11BufferPoolClass;
typedef struct _GstD3D11BufferPoolPrivate GstD3D11BufferPoolPrivate;

G_END_DECLS

#endif /* __GST_D3D11_FWD_H__ */
