/*
 * GStreamer
 * Copyright (C) 2026 Matthew Waters <matthew@centricular.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/vulkan/vulkan.h>

/**
 * gst_vulkan_barrier_memory_info_clear:
 * @info: a #GstVulkanBarrierMemoryInfo
 *
 * Clear any referenced objects from this @info.
 *
 * Since: 1.30
 */
void
gst_vulkan_barrier_memory_info_clear (GstVulkanBarrierMemoryInfo * info)
{
  gst_clear_object (&info->queue);
  gst_clear_vulkan_handle (&info->semaphore);
  memset (info, 0, sizeof (*info));
}

/**
 * gst_vulkan_barrier_memory_info_is_equal:
 * @info: a #GstVulkanBarrierMemoryInfo
 * @other: the other #GstVulkanBarrierMemoryInfo to compara against
 *
 * Returns: whether @info and @other_info are the same.
 *
 * Since: 1.30
 */
gboolean
gst_vulkan_barrier_memory_info_is_equal (GstVulkanBarrierMemoryInfo * info,
    GstVulkanBarrierMemoryInfo * other)
{
  if (info == other)
    return TRUE;
  if (!info || !other)
    return FALSE;
  return memcmp (info, other, sizeof (*info)) == 0;
}

/**
 * gst_vulkan_barrier_memory_info_copy_into:
 * @info: a #GstVulkanBarrierMemoryInfo
 * @other: (out caller-allocates): the other #GstVulkanBarrierMemoryInfo to copy into
 *
 * Since: 1.30
 */
void
gst_vulkan_barrier_memory_info_copy_into (GstVulkanBarrierMemoryInfo * info,
    GstVulkanBarrierMemoryInfo * other)
{
  g_return_if_fail (info);
  g_return_if_fail (other);

  if (info == other)
    return;

  gst_vulkan_barrier_memory_info_clear (other);
  memcpy (other, info, sizeof (*info));
  if (other->queue)
    gst_object_ref (other->queue);
  if (other->semaphore)
    gst_vulkan_handle_ref (other->semaphore);
}
