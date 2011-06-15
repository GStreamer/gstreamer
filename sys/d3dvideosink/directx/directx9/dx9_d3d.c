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

#include <d3d9.h>
#include <d3dx9tex.h>

#include "dx9_d3d.h"

void
dx9_d3d_init (DirectXAPIComponent * component, gpointer data)
{
  DIRECTX_DEBUG ("Initializing Direct3D");
  DIRECTX_OPEN_COMPONENT_MODULE (component, "d3d9");

  DIRECTX_DEBUG ("Setting Direct3D dispatch table");
  DIRECTX_OPEN_COMPONENT_SYMBOL (component, D3D9DispatchTable, Direct3DCreate9);

  //{
  //  IDirect3D9* blah;
  //  DIRECTX_DEBUG("CALLING CREATE9!");
  //  //blah = DX9_CALL_FUNC(data, Direct3DCreate9, D3D_SDK_VERSION);
  //  blah = DX9_D3D_COMPONENT_CALL_FUNC(component, Direct3DCreate9, D3D_SDK_VERSION);
  //  DIRECTX_DEBUG("RELEASING CREATE9!");
  //  IDirect3D9_Release(blah);
  //  DIRECTX_DEBUG("RELEASED CREATE9!");
  //}
}

DirectXD3D *
dx9_d3d_create (const DirectXAPI * api)
{
  return NULL;
}

gboolean
dx9_d3d_resize (const DirectXD3D * d3d)
{
  return TRUE;
}

gboolean
dx9_d3d_device_lost (const DirectXD3D * d3d)
{
  return TRUE;
}

gboolean
dx9_d3d_notify_device_reset (const DirectXD3D * d3d)
{
  return TRUE;
}

gboolean
dx9_d3d_release (const DirectXD3D * d3d)
{
  return TRUE;
}
