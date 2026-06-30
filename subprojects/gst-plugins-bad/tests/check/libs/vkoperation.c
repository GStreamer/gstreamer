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
static GstVulkanQueue *queue;
static GstVulkanCommandPool *cmd_pool;

static void
setup (void)
{
  instance = gst_vulkan_instance_new ();
  fail_unless (gst_vulkan_instance_open (instance, NULL));
  device = gst_vulkan_device_new_with_index (instance, 0);
  fail_unless (gst_vulkan_device_open (device, NULL));
  /* family and id may be wrong! */
  queue = gst_vulkan_device_get_queue (device, 0, 0);
  fail_unless (GST_IS_VULKAN_QUEUE (queue));
  cmd_pool = gst_vulkan_queue_create_command_pool (queue, NULL);
  fail_unless (GST_IS_VULKAN_COMMAND_POOL (cmd_pool));
}

static void
teardown (void)
{
  gst_object_unref (instance);
  gst_object_unref (device);
  gst_object_unref (queue);
  gst_object_unref (cmd_pool);
}

GST_START_TEST (test_new)
{
  GstVulkanOperation *op = gst_vulkan_operation_new (cmd_pool);
  gst_object_unref (op);
}

GST_END_TEST;

GST_START_TEST (test_operation_empty)
{
  GstVulkanOperation *op = gst_vulkan_operation_new (cmd_pool);
  GError *error = NULL;

  fail_unless (gst_vulkan_operation_begin (op, &error));
  fail_if (error);
  fail_unless (gst_vulkan_operation_end (op, &error));
  fail_if (error);
  gst_object_unref (op);
}

GST_END_TEST;

static GstBufferPool *
create_buffer_pool (const char *format, VkImageUsageFlags usage,
    VkImageLayout initial_layout, guint64 initial_access)
{
  GstCaps *caps;
  GstBufferPool *pool;
  GstStructure *config;

  caps = gst_caps_new_simple ("video/x-raw", "format", G_TYPE_STRING, format,
      "width", G_TYPE_INT, 1024, "height", G_TYPE_INT, 780, NULL);
  gst_caps_set_features_simple (caps,
      gst_caps_features_new_static_str (GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE,
          NULL));

  pool = gst_vulkan_image_buffer_pool_new (device);

  config = gst_buffer_pool_get_config (pool);

  gst_buffer_pool_config_set_params (config, caps, 1024, 1, 0);
  gst_caps_unref (caps);

  if (usage != 0) {
    gst_vulkan_image_buffer_pool_config_set_allocation_params (config,
        usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, initial_layout,
        initial_access);
  }

  fail_unless (gst_buffer_pool_set_config (pool, config));
  gst_buffer_pool_set_active (pool, TRUE);

  return pool;
}

GST_START_TEST (test_barrier_update)
{
  GstVulkanOperation *op = gst_vulkan_operation_new (cmd_pool);
  GstBufferPool *pool = create_buffer_pool ("RGBA",
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      VK_IMAGE_LAYOUT_UNDEFINED, 0);
  GstVulkanImageMemory *image;
  GstBuffer *buffer;
  GError *error = NULL;
  GstFlowReturn ret;

  fail_unless (gst_vulkan_operation_begin (op, &error));
  fail_if (error);

  ret = gst_buffer_pool_acquire_buffer (pool, &buffer, NULL);
  fail_unless (ret == GST_FLOW_OK);

  image = (GstVulkanImageMemory *) gst_buffer_peek_memory (buffer, 0);
  fail_unless_equals_int (image->barrier.image_layout,
      VK_IMAGE_LAYOUT_UNDEFINED);
  fail_unless_equals_int (image->barrier.parent.pipeline_stages,
      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
  fail_unless_equals_int (image->barrier.parent.access_flags, 0);

  fail_unless (gst_vulkan_operation_add_dependency_frame (op, buffer,
          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT));

  fail_unless (gst_vulkan_operation_add_frame_barrier (op, buffer,
          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, NULL));

  fail_unless (gst_vulkan_operation_end (op, &error));
  fail_if (error);

  fail_unless_equals_int (image->barrier.image_layout, VK_IMAGE_LAYOUT_GENERAL);
  fail_unless_equals_int (image->barrier.parent.pipeline_stages,
      VK_PIPELINE_STAGE_TRANSFER_BIT);
  fail_unless_equals_int (image->barrier.parent.access_flags,
      VK_ACCESS_TRANSFER_READ_BIT);

  gst_buffer_unref (buffer);
  gst_buffer_pool_set_active (pool, FALSE);
  gst_object_unref (pool);
  gst_object_unref (op);
}

GST_END_TEST;

GST_START_TEST (test_barrier_update_concurrent)
{
  GstVulkanOperation *op = gst_vulkan_operation_new (cmd_pool);
  GstBufferPool *pool = create_buffer_pool ("RGBA",
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      VK_IMAGE_LAYOUT_UNDEFINED, 0);
  GstVulkanImageMemory *image;
  GstVulkanBarrierImageInfo old_info = { 0, }, new_info = { 0, };
  GstBuffer *buffer;
  GError *error = NULL;
  GstFlowReturn ret;

  fail_unless (gst_vulkan_operation_begin (op, &error));
  fail_if (error);

  ret = gst_buffer_pool_acquire_buffer (pool, &buffer, NULL);
  fail_unless (ret == GST_FLOW_OK);

  image = (GstVulkanImageMemory *) gst_buffer_peek_memory (buffer, 0);
  fail_unless_equals_int (image->barrier.image_layout,
      VK_IMAGE_LAYOUT_UNDEFINED);
  fail_unless_equals_int (image->barrier.parent.pipeline_stages,
      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
  fail_unless_equals_int (image->barrier.parent.access_flags, 0);
  gst_vulkan_image_memory_lock (image);
  gst_vulkan_image_memory_peek_barrier_unlocked (image, &old_info);
  gst_vulkan_image_memory_unlock (image);

  // Start with one barrier
  fail_unless (gst_vulkan_operation_add_dependency_frame (op, buffer,
          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT));

  fail_unless (gst_vulkan_operation_add_frame_barrier (op, buffer,
          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, NULL));

  // while operation is in progress, change the image memory barrier to
  // something else.
  gst_vulkan_barrier_image_info_copy_into (&old_info, &new_info);
  new_info.parent.pipeline_stages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
  gst_vulkan_image_memory_lock (image);
  fail_unless (gst_vulkan_image_memory_compare_exchange_barrier_unlocked (image,
          &old_info, &new_info));
  fail_unless_equals_int (image->barrier.image_layout,
      VK_IMAGE_LAYOUT_UNDEFINED);
  fail_unless_equals_int (image->barrier.parent.pipeline_stages,
      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
  fail_unless_equals_int (image->barrier.parent.access_flags, 0);
  gst_vulkan_image_memory_unlock (image);

  gst_vulkan_barrier_image_info_clear (&old_info);
  gst_vulkan_barrier_image_info_clear (&new_info);

  // The operation will fail as the original barrier is not the same as the
  // image barrier.
  fail_if (gst_vulkan_operation_end (op, &error));
  fail_unless (error);
  fail_unless_equals_int (error->code, VK_ERROR_OUT_OF_DATE_KHR);
  g_clear_error (&error);

  fail_unless_equals_int (image->barrier.image_layout,
      VK_IMAGE_LAYOUT_UNDEFINED);
  fail_unless_equals_int (image->barrier.parent.pipeline_stages,
      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
  fail_unless_equals_int (image->barrier.parent.access_flags, 0);

  // Redo the operation barrier without any conflicting change.
  fail_unless (gst_vulkan_operation_begin (op, &error));
  fail_if (error);
  fail_unless (gst_vulkan_operation_add_dependency_frame (op, buffer,
          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT));

  fail_unless (gst_vulkan_operation_add_frame_barrier (op, buffer,
          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, NULL));
  fail_unless (gst_vulkan_operation_end (op, &error));
  fail_if (error);

  fail_unless_equals_int (image->barrier.image_layout, VK_IMAGE_LAYOUT_GENERAL);
  fail_unless_equals_int (image->barrier.parent.pipeline_stages,
      VK_PIPELINE_STAGE_TRANSFER_BIT);
  fail_unless_equals_int (image->barrier.parent.access_flags,
      VK_ACCESS_TRANSFER_READ_BIT);

  gst_buffer_unref (buffer);
  gst_buffer_pool_set_active (pool, FALSE);
  gst_object_unref (pool);
  gst_object_unref (op);
}

GST_END_TEST;

static Suite *
vkoperation_suite (void)
{
  Suite *s = suite_create ("vkoperation");
  TCase *tc_basic = tcase_create ("general");
  gboolean have_instance;

  suite_add_tcase (s, tc_basic);
  tcase_add_checked_fixture (tc_basic, setup, teardown);

  instance = gst_vulkan_instance_new ();
  have_instance = gst_vulkan_instance_open (instance, NULL);
  gst_object_unref (instance);
  if (have_instance) {
    tcase_add_test (tc_basic, test_new);
    tcase_add_test (tc_basic, test_operation_empty);
    tcase_add_test (tc_basic, test_barrier_update);
    tcase_add_test (tc_basic, test_barrier_update_concurrent);
  }

  return s;
}

#ifdef __APPLE__
GST_CHECK_MAIN_NOFORK (vkoperation);
#else
GST_CHECK_MAIN (vkoperation);
#endif
