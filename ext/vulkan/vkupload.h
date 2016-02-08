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

#ifndef _VK_UPLOAD_H_
#define _VK_UPLOAD_H_

#include <gst/gst.h>
#include <gst/video/video.h>
#include <vk.h>

G_BEGIN_DECLS

#define GST_TYPE_VULKAN_UPLOAD            (gst_vulkan_upload_get_type())
#define GST_VULKAN_UPLOAD(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VULKAN_UPLOAD,GstVulkanUpload))
#define GST_VULKAN_UPLOAD_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VULKAN_UPLOAD,GstVulkanUploadClass))
#define GST_IS_VULKAN_UPLOAD(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VULKAN_UPLOAD))
#define GST_IS_VULKAN_UPLOAD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VULKAN_UPLOAD))

typedef struct _GstVulkanUpload GstVulkanUpload;
typedef struct _GstVulkanUploadClass GstVulkanUploadClass;

struct _GstVulkanUpload
{
  GstBaseTransform      parent;

  GstVulkanInstance     *instance;
  GstVulkanDevice       *device;

  GstVulkanDisplay      *display;

  GstVideoInfo          in_info;
  GstVideoInfo          out_info;

  gsize                 alloc_sizes[GST_VIDEO_MAX_PLANES];
};

struct _GstVulkanUploadClass
{
  GstBaseTransformClass video_sink_class;
};

GType gst_vulkan_upload_get_type(void);

G_END_DECLS

#endif
