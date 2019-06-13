/*
 * GStreamer
 * Copyright (C) 2016 Matthew Waters <matthew@centricular.com>
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

#ifndef __GST_VULKAN_FORMAT_H__
#define __GST_VULKAN_FORMAT_H__

#include <gst/vulkan/vulkan.h>

G_BEGIN_DECLS

typedef struct _GstVulkanFormatInfo GstVulkanFormatInfo;

#define GST_VULKAN_MAX_COMPONENTS 4

typedef enum
{
  GST_VULKAN_FORMAT_SCALING_UNORM = 1,
  GST_VULKAN_FORMAT_SCALING_SNORM,
  GST_VULKAN_FORMAT_SCALING_USCALED,
  GST_VULKAN_FORMAT_SCALING_SSCALED,
  GST_VULKAN_FORMAT_SCALING_UINT,
  GST_VULKAN_FORMAT_SCALING_SINT,
  GST_VULKAN_FORMAT_SCALING_SRGB,
} GstVulkanFormatScaling;

typedef enum
{
  GST_VULKAN_FORMAT_FLAG_YUV = (1 << 0),
  GST_VULKAN_FORMAT_FLAG_RGB = (1 << 1),
  GST_VULKAN_FORMAT_FLAG_ALPHA = (1 << 2),
  GST_VULKAN_FORMAT_FLAG_LE = (1 << 3),
  GST_VULKAN_FORMAT_FLAG_COMPLEX = (1 << 4),
} GstVulkanFormatFlags;

/**
 * GstVulkanFormatInfo:
 * @format: the Vulkan format being described
 * @name: name of this format
 * @scaling: how raw data is interpreted and scaled
 * @n_components; number of components in this format
 * @comp_order: the order of the components.  The 'R' component can be
 *                   found at index 0, the G component at index 1, etc
 * @comp_offset: number of bits from the start of a pixel where the component
 *               is located
 * @comp_depth: number of bits the component uses
 */
struct _GstVulkanFormatInfo
{
  VkFormat format;
  const gchar *name;
  GstVulkanFormatScaling scaling;
  GstVulkanFormatFlags flags;
  guint bits;
  guint n_components;
  guint8 shift[GST_VULKAN_MAX_COMPONENTS];
  guint8 depth[GST_VULKAN_MAX_COMPONENTS];
  gint8 pixel_stride[GST_VULKAN_MAX_COMPONENTS];
  guint n_planes;
  guint8 plane[GST_VULKAN_MAX_COMPONENTS];
  guint8 poffset[GST_VULKAN_MAX_COMPONENTS];
  guint8 w_sub[GST_VULKAN_MAX_COMPONENTS];
  guint8 h_sub[GST_VULKAN_MAX_COMPONENTS];
};

GST_VULKAN_API
const GstVulkanFormatInfo *     gst_vulkan_format_get_info                      (VkFormat format);

G_END_DECLS

#endif /* __GST_VULKAN_FORMAT_H__ */
