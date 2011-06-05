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

#define CINTERFACE

#include <windows.h>
//#include <d3d11.h>

#include "dx11_d3d.h"

void
dx11_d3d_init (DirectXAPIComponent * component, gpointer data)
{
  DIRECTX_DEBUG ("Initializing Direct3D");
  DIRECTX_OPEN_COMPONENT_MODULE (component, "d3d11");
  DIRECTX_DEBUG ("Completed Initializing Direct3D");

  DIRECTX_DEBUG ("Setting Direct3D dispatch table");
  //DIRECTX_OPEN_COMPONENT_SYMBOL(component, D3D11DispatchTable, D3D11CreateDevice);

  //{
  //  ID3D11Device* pDevice = NULL;
  //  DIRECTX_DEBUG("Calling D3D11CreateDevice");
  //  DX11_D3D_COMPONENT_CALL_FUNC(component, D3D11CreateDevice, NULL, D3D11_DRIVER_TYPE_HARDWARE, NULL, 0, D3D11_SDK_VERSION, &pDevice);
  //  DIRECTX_DEBUG("Releasing D3D11 device");
  //  ID3D11Device_Release(pDevice);
  //  DIRECTX_DEBUG("Released D3D11 device");
  //}
}

DirectXD3D *
dx11_d3d_create (const DirectXAPI * api)
{
  return NULL;
}

gboolean
dx11_d3d_resize (const DirectXD3D * d3d)
{
  return TRUE;
}

gboolean
dx11_d3d_device_lost (const DirectXD3D * d3d)
{
  return TRUE;
}

gboolean
dx11_d3d_notify_device_reset (const DirectXD3D * d3d)
{
  return TRUE;
}

gboolean
dx11_d3d_release (const DirectXD3D * d3d)
{
  return TRUE;
}
