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

static void
check_binary_semaphores (GstVulkanDevice * test_device,
    GstVulkanCommandPool * test_pool, guint64 signal_stage)
{
  GstVulkanOperation *signal = gst_vulkan_operation_new (test_pool);
  GstVulkanOperation *wait = gst_vulkan_operation_new (test_pool);
  GstVulkanHandle *semaphores[2];
  const guint64 stages[] = { VK_PIPELINE_STAGE_TRANSFER_BIT,
    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
  };
  VkSemaphoreCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
  };
  GError *error = NULL;

  for (guint i = 0; i < G_N_ELEMENTS (semaphores); i++) {
    VkSemaphore semaphore;

    fail_unless_equals_int (vkCreateSemaphore (test_device->device, &info,
            NULL, &semaphore), VK_SUCCESS);
    semaphores[i] = gst_vulkan_handle_new_wrapped (test_device,
        GST_VULKAN_HANDLE_TYPE_SEMAPHORE, (GstVulkanHandleTypedef) semaphore,
        gst_vulkan_handle_free_semaphore, NULL);
  }

  /* Exercise multiple waits with different stages and reuse the operations
   * across submissions to exercise clearing the recorded dependencies. */
  for (guint round = 0; round < 2; round++) {
    fail_unless (gst_vulkan_operation_begin (signal, &error));
    fail_if (error);
    for (guint i = 0; i < G_N_ELEMENTS (semaphores); i++)
      gst_vulkan_operation_add_signal_semaphore (signal,
          gst_vulkan_handle_ref (semaphores[i]), signal_stage);
    fail_unless (gst_vulkan_operation_end (signal, &error));
    fail_if (error);

    fail_unless (gst_vulkan_operation_begin (wait, &error));
    fail_if (error);
    for (guint i = 0; i < G_N_ELEMENTS (semaphores); i++)
      gst_vulkan_operation_add_wait_semaphore (wait,
          gst_vulkan_handle_ref (semaphores[i]), stages[i]);
    fail_unless (gst_vulkan_operation_end (wait, &error));
    fail_if (error);
    fail_unless (gst_vulkan_operation_wait (wait));
  }

  gst_object_unref (wait);
  gst_object_unref (signal);
  for (guint i = 0; i < G_N_ELEMENTS (semaphores); i++)
    gst_vulkan_handle_unref (semaphores[i]);
}

GST_START_TEST (test_operation_binary_semaphores)
{
  check_binary_semaphores (device, cmd_pool,
      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
}

GST_END_TEST;

GST_START_TEST (test_operation_binary_semaphores_legacy)
{
  GstVulkanInstance *legacy_instance = gst_vulkan_instance_new ();
  GstVulkanDevice *legacy_device;
  GstVulkanQueue *legacy_queue;
  GstVulkanCommandPool *legacy_pool;

  g_object_set (legacy_instance, "requested-api-major", 1,
      "requested-api-minor", 0, NULL);
  fail_unless (gst_vulkan_instance_open (legacy_instance, NULL));
  legacy_device = gst_vulkan_device_new_with_index (legacy_instance, 0);
  gst_vulkan_device_disable_extension (legacy_device,
      VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
  fail_unless (gst_vulkan_device_open (legacy_device, NULL));
  fail_if (gst_vulkan_physical_device_check_api_version
      (legacy_device->physical_device, 1, 2, 0));
  fail_if (gst_vulkan_device_is_extension_enabled (legacy_device,
          VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME));
  legacy_queue =
      gst_vulkan_device_select_queue (legacy_device, VK_QUEUE_GRAPHICS_BIT);
  fail_unless (legacy_queue != NULL);
  legacy_pool = gst_vulkan_queue_create_command_pool (legacy_queue, NULL);
  fail_unless (legacy_pool != NULL);

  check_binary_semaphores (legacy_device, legacy_pool,
      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

  gst_object_unref (legacy_pool);
  gst_object_unref (legacy_queue);
  gst_object_unref (legacy_device);
  gst_object_unref (legacy_instance);
}

GST_END_TEST;

/* Keep timeline values in the legacy submit, even for signal stages that
 * cannot be represented by VkPipelineStageFlags. */
GST_START_TEST (test_operation_binary_semaphores_legacy_timeline)
{
  GstVulkanInstance *legacy_instance = gst_vulkan_instance_new ();
  GstVulkanDevice *legacy_device;
  GstVulkanQueue *legacy_queue;
  GstVulkanCommandPool *legacy_pool;
  GstVulkanOperation *op;
  const VkBaseInStructure *feature;
  const VkPhysicalDeviceVulkan12Features *features12 = NULL;

  g_object_set (legacy_instance, "requested-api-major", 1,
      "requested-api-minor", 2, NULL);
  if (!gst_vulkan_instance_open (legacy_instance, NULL)) {
    g_print
        ("SKIP legacy timeline binary semaphores: Vulkan 1.2 unavailable\n");
    gst_object_unref (legacy_instance);
    return;
  }
  legacy_device = gst_vulkan_device_new_with_index (legacy_instance, 0);
  feature = (const VkBaseInStructure *)
      gst_vulkan_physical_device_get_features (legacy_device->physical_device);
  for (; feature; feature = feature->pNext) {
    if (feature->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES)
      features12 = (const VkPhysicalDeviceVulkan12Features *) feature;
  }
  if (!features12 || !features12->timelineSemaphore) {
    g_print ("SKIP legacy timeline binary semaphores: timeline support "
        "unavailable\n");
    gst_object_unref (legacy_device);
    gst_object_unref (legacy_instance);
    return;
  }

  gst_vulkan_device_disable_extension (legacy_device,
      VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
  fail_unless (gst_vulkan_device_open (legacy_device, NULL));
  fail_if (gst_vulkan_physical_device_check_api_version
      (legacy_device->physical_device, 1, 3, 0));
  fail_if (gst_vulkan_device_is_extension_enabled (legacy_device,
          VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME));
  legacy_queue =
      gst_vulkan_device_select_queue (legacy_device, VK_QUEUE_GRAPHICS_BIT);
  fail_unless (legacy_queue != NULL);
  legacy_pool = gst_vulkan_queue_create_command_pool (legacy_queue, NULL);
  fail_unless (legacy_pool != NULL);
  op = gst_vulkan_operation_new (legacy_pool);
  fail_if (gst_vulkan_operation_use_sync2 (op));
  gst_object_unref (op);

  check_binary_semaphores (legacy_device, legacy_pool,
      VK_PIPELINE_STAGE_2_COPY_BIT);
  g_print ("PASS legacy timeline binary semaphores: high-bit signal stage\n");

  gst_object_unref (legacy_pool);
  gst_object_unref (legacy_queue);
  gst_object_unref (legacy_device);
  gst_object_unref (legacy_instance);
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

#if defined(VK_KHR_video_maintenance1)
GST_START_TEST (test_begin_query_twice)
{
  GstVulkanQueue *video_queue;
  GstVulkanCommandPool *pool;
  GstVulkanOperation *op;
  GError *error = NULL;
  VkVideoDecodeH264ProfileInfoKHR h264_profile = {
    .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PROFILE_INFO_KHR,
    .stdProfileIdc = STD_VIDEO_H264_PROFILE_IDC_MAIN,
    .pictureLayout = VK_VIDEO_DECODE_H264_PICTURE_LAYOUT_PROGRESSIVE_KHR,
  };
  VkVideoProfileInfoKHR profile = {
    .sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR,
    .pNext = &h264_profile,
    .videoCodecOperation = VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR,
    .chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR,
    .lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
    .chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
  };
  VkBaseInStructure tail = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO };
  VkBaseInStructure base = {.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_INFO_KHR,
    .pNext = &tail
  };
  const VkBaseInStructure *node;
  guint i;

  video_queue = gst_vulkan_device_select_queue (device,
      VK_QUEUE_VIDEO_DECODE_BIT_KHR);
  if (!video_queue)
    return;

  pool = gst_vulkan_queue_create_command_pool (video_queue, NULL);
  fail_unless (GST_IS_VULKAN_COMMAND_POOL (pool));
  op = gst_vulkan_operation_new (pool);

  if (!gst_vulkan_operation_enable_query (op,
          VK_QUERY_TYPE_RESULT_STATUS_ONLY_KHR, 1, &profile, &error)) {
    g_clear_error (&error);
    goto done;
  }

  fail_unless (gst_vulkan_operation_begin (op, &error));
  fail_if (error);

  /* a decoder retry begins the query again on the same structure */
  fail_unless (gst_vulkan_operation_begin_query (op, &base, 0));
  fail_unless (gst_vulkan_operation_begin_query (op, &base, 0));

  node = &base;
  for (i = 0; i < 4 && node; i++)
    node = node->pNext;
  fail_unless_equals_int (i, 3);
  fail_unless (node == NULL);
  fail_unless_equals_int (base.pNext->sType,
      VK_STRUCTURE_TYPE_VIDEO_INLINE_QUERY_INFO_KHR);
  fail_unless (base.pNext->pNext == &tail);

  fail_unless (gst_vulkan_operation_end (op, &error));
  fail_if (error);

done:
  gst_object_unref (op);
  gst_object_unref (pool);
  gst_object_unref (video_queue);
}

GST_END_TEST;
#endif

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
    tcase_add_test (tc_basic, test_operation_binary_semaphores);
    tcase_add_test (tc_basic, test_operation_binary_semaphores_legacy);
    tcase_add_test (tc_basic, test_operation_binary_semaphores_legacy_timeline);
    tcase_add_test (tc_basic, test_barrier_update);
    tcase_add_test (tc_basic, test_barrier_update_concurrent);
#if defined(VK_KHR_video_maintenance1)
    tcase_add_test (tc_basic, test_begin_query_twice);
#endif
  }

  return s;
}

#ifdef __APPLE__
GST_CHECK_MAIN_NOFORK (vkoperation);
#else
GST_CHECK_MAIN (vkoperation);
#endif
