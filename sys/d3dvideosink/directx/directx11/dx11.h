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

#ifndef __DIRECTX_DIRECTX11_DX11_H__
#define __DIRECTX_DIRECTX11_DX11_H__

#include "../dx.h"

#include "dx11_d3d.h"

/* Function declarations */
void dx11_init(const DirectXAPI* api);

DIRECTX_API(
  DIRECTX_11, 
  dx11_init, 
  "d3d11", 
  "D3D11CreateDevice", 
  "DirectX11Description", 
  "DirectX 11.0"
)

#endif /* __DIRECTX_DIRECTX10_DX10_H__ */
