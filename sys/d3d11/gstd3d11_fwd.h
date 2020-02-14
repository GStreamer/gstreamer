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
#include "gstd3d11config.h"

#ifndef INITGUID
#include <initguid.h>
#endif

#if (D3D11_HEADER_VERSION >= 4)
#include <d3d11_4.h>
#elif (D3D11_HEADER_VERSION >= 3)
#include <d3d11_3.h>
#elif (D3D11_HEADER_VERSION >= 2)
#include <d3d11_2.h>
#elif (D3D11_HEADER_VERSION >= 1)
#include <d3d11_1.h>
#else
#include <d3d11.h>
#endif

#if (DXGI_HEADER_VERSION >= 6)
#include <dxgi1_6.h>
#elif (DXGI_HEADER_VERSION >= 5)
#include <dxgi1_5.h>
#elif (DXGI_HEADER_VERSION >= 4)
#include <dxgi1_4.h>
#elif (DXGI_HEADER_VERSION >= 3)
#include <dxgi1_3.h>
#elif (DXGI_HEADER_VERSION >= 2)
#include <dxgi1_2.h>
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

typedef struct _GstD3D11Format GstD3D11Format;

typedef struct _GstD3D11BaseFilter GstD3D11BaseFilter;
typedef struct _GstD3D11BaseFilterClass GstD3D11BaseFilterClass;

typedef struct _GstD3D11Upload GstD3D11Upload;
typedef struct _GstD3D11UploadClass GstD3D11UploadClass;

typedef struct _GstD3D11Download GstD3D11Download;
typedef struct _GstD3D11DownloadClass GstD3D11DownloadClass;

typedef struct _GstD3D11ColorConvert GstD3D11ColorConvert;
typedef struct _GstD3D11ColorConvertClass GstD3D11ColorConvertClass;

typedef struct _GstD3D11Decoder GstD3D11Decoder;
typedef struct _GstD3D11DecoderClass GstD3D11DecoderClass;
typedef struct _GstD3D11DecoderPrivate GstD3D11DecoderPrivate;

G_END_DECLS

#endif /* __GST_D3D11_FWD_H__ */
