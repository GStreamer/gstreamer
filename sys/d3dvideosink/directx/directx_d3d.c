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

#include "directx.h"

const DirectXD3D *
directx_d3d_create (const DirectXAPI * api)
{
  if (!api)
    return NULL;

  return DIRECTX_D3D_CALL_API_FUNCTION (api, create, api);
}

gboolean
directx_d3d_resize (const DirectXD3D * d3d)
{
  if (!d3d)
    return FALSE;

  return DIRECTX_D3D_CALL_FUNCTION (d3d, resize, d3d);
}

gboolean
directx_d3d_device_lost (const DirectXD3D * d3d)
{
  if (!d3d)
    return FALSE;

  return DIRECTX_D3D_CALL_FUNCTION (d3d, device_lost, d3d);
}

gboolean
directx_d3d_notify_device_reset (const DirectXD3D * d3d)
{
  if (!d3d)
    return FALSE;

  return DIRECTX_D3D_CALL_FUNCTION (d3d, notify_device_reset, d3d);
}

gboolean
directx_d3d_release (const DirectXD3D * d3d)
{
  if (!d3d)
    return FALSE;

  return DIRECTX_D3D_CALL_FUNCTION (d3d, release, d3d);
}
