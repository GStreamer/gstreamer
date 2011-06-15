/* GStreamer
 * Copyright (C) 2011 David Hoyt <dhoyt@hoytsoft.org>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __DIRECTX_DIRECTX11_DX11_D3D_H__
#define __DIRECTX_DIRECTX11_DX11_D3D_H__

#include <windows.h>

#include "../d3d.h"

#define DX11_D3D_API_CALL_FUNC(api, func_name, ...)              (DIRECTX_CALL_COMPONENT_SYMBOL(DIRECTX_D3D(api), D3D11DispatchTable, func_name, __VA_ARGS__))
#define DX11_D3D_COMPONENT_CALL_FUNC(component, func_name, ...)  (DIRECTX_CALL_COMPONENT_SYMBOL(component, D3D11DispatchTable, func_name, __VA_ARGS__))

/* Structs */
typedef struct _D3D11 D3D11;
typedef struct _D3D11DispatchTable D3D11DispatchTable;

/* Functions */
/* Courtesy http://code.google.com/p/theaimworldeditor/source/browse/trunk/DXUT/Core/DXUTmisc.cpp */
typedef HRESULT (WINAPI *LPD3D11CREATEDEVICE)(gpointer /* IDXGIAdapter* */, UINT /* D3D11_DRIVER_TYPE */, HMODULE, UINT, UINT32, gpointer* /* ID3D11Device** */ );

struct _D3D11DispatchTable 
{
  LPD3D11CREATEDEVICE D3D11CreateDevice;
};

/* Global data */
struct _D3D11 
{
  D3D11DispatchTable vtable;
};

/* Global vars */
static D3D11 dx11_d3d;

/* Function declarations */

void dx11_d3d_init(DirectXAPIComponent* component, gpointer data);
DirectXD3D* dx11_d3d_create(const DirectXAPI* api);
gboolean dx11_d3d_resize(const DirectXD3D* d3d);
gboolean dx11_d3d_device_lost(const DirectXD3D* d3d);
gboolean dx11_d3d_notify_device_reset(const DirectXD3D* d3d);
gboolean dx11_d3d_release(const DirectXD3D* d3d);

DIRECTX_D3D_API(
  DIRECTX_11, 
  dx11_d3d.vtable, 
  dx11_d3d_init, 
  dx11_d3d_create, 
  dx11_d3d_resize, 
  dx11_d3d_device_lost, 
  dx11_d3d_notify_device_reset, 
  dx11_d3d_release
)

#endif /* __DIRECTX_DIRECTX11_DX11_D3D_H__ */
