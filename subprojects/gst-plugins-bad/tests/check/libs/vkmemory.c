/* GStreamer
 *
 * Copyright (C) 2019 Matthew Waters <matthew@centricular.com>
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

static GstVulkanInstance *instance;
static GstVulkanDevice *device;

static void
setup (void)
{
  instance = gst_vulkan_instance_new ();
  fail_unless (gst_vulkan_instance_open (instance, NULL));
  device = gst_vulkan_device_new_with_index (instance, 0);
  fail_unless (gst_vulkan_device_open (device, NULL));
}

static void
teardown (void)
{
  gst_object_unref (instance);
  gst_object_unref (device);
}

static void
check_size (GstMemory * mem, gsize at_least)
{
  gsize size, maxsize, offset;

  size = gst_memory_get_sizes (mem, &offset, &maxsize);
  fail_unless (size <= maxsize);
  fail_unless (size >= at_least);
}

GST_START_TEST (test_buffer_mem_allocate)
{
  VkBufferUsageFlags usage;
  GstVulkanBufferMemory *vk_mem;
  gsize orig_size = 1024, size, offset;
  GstMemory *mem;

  usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  mem =
      gst_vulkan_buffer_memory_alloc (device, orig_size,
      usage, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
  fail_unless (gst_is_vulkan_buffer_memory (mem));
  vk_mem = (GstVulkanBufferMemory *) mem;

  fail_unless (vk_mem->device == device);
  fail_unless (vk_mem->usage == usage);
  fail_unless (vk_mem->vk_mem != NULL);

  size = gst_memory_get_sizes (mem, &offset, NULL);
  fail_unless (offset == 0);
  check_size (mem, orig_size);
  fail_unless (vk_mem->requirements.size >= size);

  size = gst_memory_get_sizes ((GstMemory *) vk_mem->vk_mem, &offset, NULL);
  fail_unless (offset == 0);
  check_size ((GstMemory *) vk_mem->vk_mem, orig_size);

  gst_memory_unref (mem);
}

GST_END_TEST;

#define COPY_SIZE 4096

static void
fill_pattern (GstMemory * mem, guint8 seed)
{
  GstMapInfo info;
  gsize i;

  fail_unless (gst_memory_map (mem, &info, GST_MAP_WRITE));
  for (i = 0; i < info.size; i++)
    info.data[i] = (i * 7 + seed) & 0xff;
  gst_memory_unmap (mem, &info);
}

static void
check_pattern (GstMemory * mem, guint8 seed)
{
  GstMapInfo info;
  gsize i;

  fail_unless (gst_memory_map (mem, &info, GST_MAP_READ));
  for (i = 0; i < info.size; i++) {
    if (info.data[i] != ((i * 7 + seed) & 0xff))
      break;
  }
  fail_unless (i == info.size, "data differs at byte %" G_GSIZE_FORMAT, i);
  gst_memory_unmap (mem, &info);
}

static void
record_buffer_copy (GstVulkanOperation * exec, GstMemory * src, GstMemory * dst)
{
  GstVulkanBufferMemory *src_mem = (GstVulkanBufferMemory *) src;
  GstVulkanBufferMemory *dst_mem = (GstVulkanBufferMemory *) dst;
  GstVulkanBarrierState *barriers;
  VkBufferCopy region = {
    .srcOffset = 0,
    .dstOffset = 0,
    .size = src->size,
  };

  fail_unless (gst_vulkan_operation_begin (exec, NULL));
  barriers = gst_vulkan_operation_get_barriers (exec);
  fail_unless (gst_vulkan_barrier_state_add_buffer_barrier (barriers, src_mem,
          VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_ACCESS_TRANSFER_READ_BIT, NULL));
  fail_unless (gst_vulkan_barrier_state_add_buffer_barrier (barriers, dst_mem,
          VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_ACCESS_TRANSFER_WRITE_BIT, NULL));
  gst_vulkan_barrier_state_pipeline_barrier (barriers, exec->cmd_buf, 0);
  gst_vulkan_command_buffer_lock (exec->cmd_buf);
  vkCmdCopyBuffer (exec->cmd_buf->cmd, src_mem->buffer, dst_mem->buffer, 1,
      &region);
  gst_vulkan_command_buffer_unlock (exec->cmd_buf);
  fail_unless (gst_vulkan_operation_end (exec, NULL));
}

static GstVulkanOperation *
new_operation (void)
{
  GstVulkanCommandPool *cmd_pool;
  GstVulkanOperation *exec;
  GstVulkanQueue *queue;

  queue = gst_vulkan_device_select_queue (device, VK_QUEUE_GRAPHICS_BIT);
  fail_unless (queue);
  cmd_pool = gst_vulkan_queue_create_command_pool (queue, NULL);
  fail_unless (cmd_pool);
  exec = gst_vulkan_operation_new (cmd_pool);
  gst_object_unref (cmd_pool);
  gst_object_unref (queue);

  return exec;
}

GST_START_TEST (test_buffer_mem_copy_host_visible)
{
  GstVulkanBufferMemory *vk_mem, *vk_copy;
  VkBufferUsageFlags usage;
  GstMemory *mem, *copy;

  usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  mem = gst_vulkan_buffer_memory_alloc (device, COPY_SIZE, usage,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
      | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  fail_unless (mem);
  fill_pattern (mem, 3);

  copy = gst_memory_copy (mem, 0, -1);
  fail_unless (copy);
  fail_unless (gst_is_vulkan_buffer_memory (copy));
  vk_mem = (GstVulkanBufferMemory *) mem;
  vk_copy = (GstVulkanBufferMemory *) copy;
  fail_unless (vk_copy->buffer != vk_mem->buffer);
  fail_unless (vk_copy->device == device);
  fail_unless_equals_int (vk_copy->usage, usage);
  fail_unless_equals_int (vk_copy->vk_mem->properties,
      vk_mem->vk_mem->properties);
  fail_unless_equals_int (copy->size, mem->size);

  fill_pattern (mem, 5);
  check_pattern (copy, 3);
  gst_memory_unref (copy);

  copy = gst_memory_copy (mem, 0, mem->size);
  fail_unless (copy);
  check_pattern (copy, 5);
  gst_memory_unref (copy);

  fail_unless (gst_memory_copy (mem, 1, -1) == NULL);
  fail_unless (gst_memory_copy (mem, 0, 16) == NULL);

  gst_memory_unref (mem);
}

GST_END_TEST;

GST_START_TEST (test_buffer_mem_copy_device_local)
{
  GstMemory *staging, *mem, *copy, *readback;
  GstVulkanOperation *exec;
  VkBufferUsageFlags usage;

  usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  staging = gst_vulkan_buffer_memory_alloc (device, COPY_SIZE, usage,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
      | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  readback = gst_vulkan_buffer_memory_alloc (device, COPY_SIZE, usage,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
      | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  mem = gst_vulkan_buffer_memory_alloc (device, COPY_SIZE, usage,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  fail_unless (staging && readback && mem);
  fail_if (((GstVulkanBufferMemory *) mem)->vk_mem->properties &
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
  fill_pattern (staging, 11);

  exec = new_operation ();
  record_buffer_copy (exec, staging, mem);
  fail_unless (gst_vulkan_operation_wait (exec));

  copy = gst_memory_copy (mem, 0, -1);
  fail_unless (copy);
  fail_unless (gst_is_vulkan_buffer_memory (copy));
  fail_unless (((GstVulkanBufferMemory *) copy)->buffer !=
      ((GstVulkanBufferMemory *) mem)->buffer);
  fail_unless_equals_int (((GstVulkanBufferMemory *) copy)->vk_mem->properties,
      ((GstVulkanBufferMemory *) mem)->vk_mem->properties);

  record_buffer_copy (exec, copy, readback);
  fail_unless (gst_vulkan_operation_wait (exec));
  check_pattern (readback, 11);

  gst_object_unref (exec);
  gst_memory_unref (copy);
  gst_memory_unref (mem);
  gst_memory_unref (readback);
  gst_memory_unref (staging);
}

GST_END_TEST;

GST_START_TEST (test_buffer_mem_copy_no_transfer_src)
{
  GstMemory *mem;

  mem = gst_vulkan_buffer_memory_alloc (device, COPY_SIZE,
      VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  fail_unless (mem);
  if (!(((GstVulkanBufferMemory *) mem)->vk_mem->properties &
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
    fail_unless (gst_memory_copy (mem, 0, -1) == NULL);
  gst_memory_unref (mem);
}

GST_END_TEST;

static Suite *
vkmemory_suite (void)
{
  Suite *s = suite_create ("vkmemory");
  TCase *tc_basic = tcase_create ("general");
  gboolean have_instance;

  suite_add_tcase (s, tc_basic);
  tcase_add_checked_fixture (tc_basic, setup, teardown);

  instance = gst_vulkan_instance_new ();
  have_instance = gst_vulkan_instance_open (instance, NULL);
  gst_object_unref (instance);
  if (have_instance) {
    tcase_add_test (tc_basic, test_buffer_mem_allocate);
    tcase_add_test (tc_basic, test_buffer_mem_copy_host_visible);
    tcase_add_test (tc_basic, test_buffer_mem_copy_device_local);
    tcase_add_test (tc_basic, test_buffer_mem_copy_no_transfer_src);
  }

  return s;
}


#ifdef __APPLE__
GST_CHECK_MAIN_NOFORK (vkmemory);
#else
GST_CHECK_MAIN (vkmemory);
#endif
