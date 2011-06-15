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

#ifndef __DIRECTX_DIRECTX9_DX9_D3D_H__
#define __DIRECTX_DIRECTX9_DX9_D3D_H__

#include <windows.h>

#include "../d3d.h"

#define DX9_D3D_API_CALL_FUNC(api, func_name, ...)              (DIRECTX_CALL_COMPONENT_SYMBOL(DIRECTX_D3D(api), D3D9DispatchTable, func_name, __VA_ARGS__))
#define DX9_D3D_COMPONENT_CALL_FUNC(component, func_name, ...)  (DIRECTX_CALL_COMPONENT_SYMBOL(component, D3D9DispatchTable, func_name, __VA_ARGS__))

/* Structs */
typedef struct _D3D9 D3D9;
typedef struct _D3D9DispatchTable D3D9DispatchTable;

/* Functions */
typedef gpointer /* IDirect3D9* */ (WINAPI *LPDIRECT3DCREATE9) (UINT);

struct _D3D9DispatchTable 
{
  LPDIRECT3DCREATE9 Direct3DCreate9;
};

/* Global data */
struct _D3D9 
{
  D3D9DispatchTable vtable;
};

/* Global vars */
static D3D9 dx9_d3d;

/* Function declarations */

void dx9_d3d_init(DirectXAPIComponent* component, gpointer data);
DirectXD3D* dx9_d3d_create(const DirectXAPI* api);
gboolean dx9_d3d_resize(const DirectXD3D* d3d);
gboolean dx9_d3d_device_lost(const DirectXD3D* d3d);
gboolean dx9_d3d_notify_device_reset(const DirectXD3D* d3d);
gboolean dx9_d3d_release(const DirectXD3D* d3d);

DIRECTX_D3D_API(
  DIRECTX_9, 
  dx9_d3d.vtable, 
  dx9_d3d_init, 
  dx9_d3d_create, 
  dx9_d3d_resize, 
  dx9_d3d_device_lost, 
  dx9_d3d_notify_device_reset, 
  dx9_d3d_release
)

#endif /* __DIRECTX_DIRECTX9_DX9_D3D_H__ */
