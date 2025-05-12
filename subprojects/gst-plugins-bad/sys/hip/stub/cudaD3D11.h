/* CUDA stub header
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

#include <glib.h>

G_BEGIN_DECLS

typedef enum
{
  CU_D3D11_DEVICE_LIST_ALL = 0x01,
  CU_D3D11_DEVICE_LIST_CURRENT_FRAME = 0x02,
  CU_D3D11_DEVICE_LIST_NEXT_FRAME = 0x03,
} CUd3d11DeviceList;

G_END_DECLS
