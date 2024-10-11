/* GStreamer
 * Copyright (C) 2024 Seungha Yang <seungha@centricular.com>
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

#ifdef __cplusplus
template <typename T>
D3D12_CPU_DESCRIPTOR_HANDLE
GetCPUDescriptorHandleForHeapStart (T heap)
{
#if defined(_MSC_VER) || !defined(_WIN32)
  return heap->GetCPUDescriptorHandleForHeapStart ();
#else
  D3D12_CPU_DESCRIPTOR_HANDLE handle;
  heap->GetCPUDescriptorHandleForHeapStart (&handle);
  return handle;
#endif
}

template <typename T>
D3D12_GPU_DESCRIPTOR_HANDLE
GetGPUDescriptorHandleForHeapStart (T heap)
{
#if defined(_MSC_VER) || !defined(_WIN32)
  return heap->GetGPUDescriptorHandleForHeapStart ();
#else
  D3D12_GPU_DESCRIPTOR_HANDLE handle;
  heap->GetGPUDescriptorHandleForHeapStart (&handle);
  return handle;
#endif
}

template <typename T>
D3D12_RESOURCE_DESC
GetDesc (T resource)
{
#if defined(_MSC_VER) || !defined(_WIN32)
  return resource->GetDesc ();
#else
  D3D12_RESOURCE_DESC desc;
  resource->GetDesc (&desc);
  return desc;
#endif
}
#endif /* __cplusplus */
