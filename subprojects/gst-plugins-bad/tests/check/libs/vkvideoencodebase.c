/* GStreamer
 *
 * Copyright (C) 2025 Igalia, S.L.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/vulkan/vulkan.h>
#include <gst/vulkan/gstvkencoder-private.h>

static GstVulkanInstance *instance;
static GstVulkanQueue *video_queue = NULL;
static GstVulkanQueue *graphics_queue = NULL;
static GstVulkanDevice *device;
static GstBufferPool *img_pool;
static GstBufferPool *buffer_pool;
static GstVulkanOperation *exec = NULL;
static GstVideoInfo in_info;
static GstVideoInfo out_info;

static void
setup (void)
{
  instance = gst_vulkan_instance_new ();
  fail_unless (gst_vulkan_instance_open (instance, NULL));
}

static void
teardown (void)
{
  gst_clear_object (&video_queue);
  gst_clear_object (&graphics_queue);
  gst_clear_object (&device);
  gst_object_unref (instance);
}

struct QueueProps
{
  guint expected_flags;
  guint codec;
};

static gboolean
_choose_queue (GstVulkanDevice * device, GstVulkanQueue * _queue, gpointer data)
{
  guint flags =
      device->physical_device->queue_family_props[_queue->family].queueFlags;
  guint32 codec =
      device->physical_device->queue_family_ops[_queue->family].video;
  struct QueueProps *qprops = data;

  if ((flags & VK_QUEUE_TRANSFER_BIT) == VK_QUEUE_TRANSFER_BIT) {
    gst_object_replace ((GstObject **) & graphics_queue,
        GST_OBJECT_CAST (_queue));
  }

  if (((flags & qprops->expected_flags) == qprops->expected_flags)
      && ((codec & qprops->codec) == qprops->codec))
    gst_object_replace ((GstObject **) & video_queue, GST_OBJECT_CAST (_queue));


  return !(graphics_queue && video_queue);
}

static void
setup_queue (guint expected_flags, guint codec)
{
  int i;
  struct QueueProps qprops = { expected_flags, codec };

  for (i = 0; i < instance->n_physical_devices; i++) {
    device = gst_vulkan_device_new_with_index (instance, i);
    fail_unless (gst_vulkan_device_open (device, NULL));
    gst_vulkan_device_foreach_queue (device, _choose_queue, &qprops);
    if (video_queue && GST_IS_VULKAN_QUEUE (video_queue)
        && graphics_queue && GST_IS_VULKAN_QUEUE (graphics_queue))
      break;
    gst_clear_object (&device);
    gst_clear_object (&video_queue);
    gst_clear_object (&graphics_queue);
  }
}

/* initialize the vulkan image buffer pool */
static GstBufferPool *
allocate_image_buffer_pool (GstVulkanEncoder * enc, uint32_t width,
    uint32_t height)
{
  GstVideoFormat format = GST_VIDEO_FORMAT_NV12;
  GstCaps *profile_caps, *caps = gst_caps_new_simple ("video/x-raw", "format",
      G_TYPE_STRING, gst_video_format_to_string (format), "width", G_TYPE_INT,
      width, "height", G_TYPE_INT, height, NULL);
  GstBufferPool *pool = gst_vulkan_image_buffer_pool_new (video_queue->device);
  GstStructure *config = gst_buffer_pool_get_config (pool);
  gsize frame_size = width * height * 2;

  gst_caps_set_features_simple (caps,
      gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE, NULL));
  fail_unless (gst_vulkan_encoder_create_dpb_pool (enc, caps));

  gst_video_info_from_caps (&out_info, caps);

  gst_buffer_pool_config_set_params (config, caps, frame_size, 1, 0);
  gst_vulkan_image_buffer_pool_config_set_allocation_params (config,
      VK_IMAGE_USAGE_TRANSFER_DST_BIT |
      VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
      VK_IMAGE_LAYOUT_VIDEO_ENCODE_SRC_KHR,
      VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);

  profile_caps = gst_vulkan_encoder_profile_caps (enc);
  gst_vulkan_image_buffer_pool_config_set_encode_caps (config, profile_caps);

  gst_caps_unref (caps);
  gst_caps_unref (profile_caps);

  fail_unless (gst_buffer_pool_set_config (pool, config));
  fail_unless (gst_buffer_pool_set_active (pool, TRUE));
  return pool;
}

static GstBufferPool *
allocate_buffer_pool (GstVulkanEncoder * enc, uint32_t width, uint32_t height)
{
  GstVideoFormat format = GST_VIDEO_FORMAT_NV12;
  GstCaps *profile_caps, *caps = gst_caps_new_simple ("video/x-raw", "format",
      G_TYPE_STRING, gst_video_format_to_string (format), "width", G_TYPE_INT,
      width, "height", G_TYPE_INT, height, NULL);
  gsize frame_size = width * height * 2;
  GstBufferPool *pool = gst_vulkan_buffer_pool_new (video_queue->device);
  GstStructure *config = gst_buffer_pool_get_config (pool);

  gst_caps_set_features_simple (caps,
      gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_VULKAN_BUFFER, NULL));

  gst_video_info_from_caps (&in_info, caps);

  gst_buffer_pool_config_set_params (config, caps, frame_size, 1, 0);


  profile_caps = gst_vulkan_encoder_profile_caps (enc);
  gst_vulkan_image_buffer_pool_config_set_encode_caps (config, profile_caps);

  gst_caps_unref (caps);
  gst_caps_unref (profile_caps);

  gst_vulkan_image_buffer_pool_config_set_allocation_params (config,
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
      VK_IMAGE_LAYOUT_VIDEO_ENCODE_SRC_KHR, VK_ACCESS_TRANSFER_WRITE_BIT);

  fail_unless (gst_buffer_pool_set_config (pool, config));
  fail_unless (gst_buffer_pool_set_active (pool, TRUE));

  return pool;
}

static GstBuffer *
generate_input_buffer (GstBufferPool * pool, int width, int height)
{
  int i;
  GstBuffer *buffer;
  GstMapInfo info;
  GstMemory *mem;

  if ((gst_buffer_pool_acquire_buffer (pool, &buffer, NULL))
      != GST_FLOW_OK)
    goto out;

  // PLANE Y COLOR BLUE
  mem = gst_buffer_peek_memory (buffer, 0);
  gst_memory_map (mem, &info, GST_MAP_WRITE);
  for (i = 0; i < width * height; i++)
    info.data[i] = 0x29;
  gst_memory_unmap (mem, &info);

  // PLANE UV
  mem = gst_buffer_peek_memory (buffer, 1);
  gst_memory_map (mem, &info, GST_MAP_WRITE);
  for (i = 0; i < width * height / 2; i++) {
    info.data[i] = 0xf0;
    info.data[i++] = 0x6e;
  }

  gst_memory_unmap (mem, &info);

out:
  return buffer;
}

/* upload the raw input buffer pool into a vulkan image buffer */
static GstFlowReturn
upload_buffer_to_image (GstBufferPool * pool, GstBuffer * inbuf,
    GstBuffer ** outbuf)
{
  GstFlowReturn ret = GST_FLOW_ERROR;
  GError *error = NULL;
  GstVulkanCommandBuffer *cmd_buf;
  guint i, n_mems, n_planes;
  GArray *barriers = NULL;
  VkImageLayout dst_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  VkDependencyInfoKHR dependency_info;

  if ((ret = gst_buffer_pool_acquire_buffer (pool, outbuf, NULL))
      != GST_FLOW_OK)
    goto out;

  if (!exec) {
    GstVulkanCommandPool *cmd_pool =
        gst_vulkan_queue_create_command_pool (graphics_queue, &error);
    if (!cmd_pool)
      goto error;

    exec = gst_vulkan_operation_new (cmd_pool);
    gst_object_unref (cmd_pool);
  }

  if (!gst_vulkan_operation_add_dependency_frame (exec, *outbuf,
          VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
          VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT))
    goto error;

  if (!gst_vulkan_operation_begin (exec, &error))
    goto error;

  cmd_buf = exec->cmd_buf;

  if (!gst_vulkan_operation_add_frame_barrier (exec, *outbuf,
          VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          NULL))
    goto unlock_error;

  barriers = gst_vulkan_operation_retrieve_image_barriers (exec);
  if (barriers->len == 0) {
    ret = GST_FLOW_ERROR;
    goto unlock_error;
  }

  dependency_info = (VkDependencyInfoKHR) {
  .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR,.imageMemoryBarrierCount =
        barriers->len,.pImageMemoryBarriers =
        (VkImageMemoryBarrier2 *) barriers->data,};

  gst_vulkan_operation_pipeline_barrier2 (exec, &dependency_info);
  dst_layout = g_array_index (barriers, VkImageMemoryBarrier2KHR, 0).newLayout;

  g_clear_pointer (&barriers, g_array_unref);

  n_mems = gst_buffer_n_memory (*outbuf);
  n_planes = GST_VIDEO_INFO_N_PLANES (&out_info);

  for (i = 0; i < n_planes; i++) {
    VkBufferImageCopy region;
    GstMemory *in_mem, *out_mem;
    GstVulkanBufferMemory *buf_mem;
    GstVulkanImageMemory *img_mem;
    const VkImageAspectFlags aspects[] = { VK_IMAGE_ASPECT_PLANE_0_BIT,
      VK_IMAGE_ASPECT_PLANE_1_BIT, VK_IMAGE_ASPECT_PLANE_2_BIT,
    };
    VkImageAspectFlags plane_aspect;
    guint idx;

    in_mem = gst_buffer_peek_memory (inbuf, i);

    buf_mem = (GstVulkanBufferMemory *) in_mem;

    if (n_planes == n_mems)
      plane_aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    else
      plane_aspect = aspects[i];

    /* *INDENT-OFF* */
    region = (VkBufferImageCopy) {
        .bufferOffset = 0,
        .bufferRowLength = GST_VIDEO_INFO_COMP_WIDTH (&in_info, i),
        .bufferImageHeight = GST_VIDEO_INFO_COMP_HEIGHT (&in_info, i),
        .imageSubresource = {
            .aspectMask = plane_aspect,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .imageOffset = { .x = 0, .y = 0, .z = 0, },
        .imageExtent = {
            .width = GST_VIDEO_INFO_COMP_WIDTH (&out_info, i),
            .height = GST_VIDEO_INFO_COMP_HEIGHT (&out_info, i),
            .depth = 1,
        }
    };
    /* *INDENT-ON* */

    idx = MIN (i, n_mems - 1);
    out_mem = gst_buffer_peek_memory (*outbuf, idx);
    if (!gst_is_vulkan_image_memory (out_mem)) {
      GST_WARNING ("Output is not a GstVulkanImageMemory");
      goto unlock_error;
    }
    img_mem = (GstVulkanImageMemory *) out_mem;

    gst_vulkan_command_buffer_lock (cmd_buf);
    vkCmdCopyBufferToImage (cmd_buf->cmd, buf_mem->buffer, img_mem->image,
        dst_layout, 1, &region);
    gst_vulkan_command_buffer_unlock (cmd_buf);
  }

  if (!gst_vulkan_operation_end (exec, &error))
    goto error;

  /*Hazard WRITE_AFTER_WRITE */
  gst_vulkan_operation_wait (exec);

  ret = GST_FLOW_OK;

out:
  return ret;

unlock_error:
  gst_vulkan_operation_reset (exec);

error:
  if (error) {
    GST_WARNING ("Error: %s", error->message);
    g_clear_error (&error);
  }
  gst_clear_buffer (outbuf);
  ret = GST_FLOW_ERROR;
  goto out;
}
