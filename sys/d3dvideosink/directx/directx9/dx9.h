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

#ifndef __DIRECTX_DIRECTX9_DX10_H__
#define __DIRECTX_DIRECTX9_DX10_H__

#include "../dx.h"

#include "dx9_d3d.h"

void dx9_init(const DirectXAPI* api);

DIRECTX_API(
  DIRECTX_9, 
  dx9_init, 
  "d3d9", 
  "Direct3DCreate9", 
  "DirectX9Description", 
  "DirectX 9.0"
)

#endif /* __DIRECTX_DIRECTX9_DX10_H__ */
