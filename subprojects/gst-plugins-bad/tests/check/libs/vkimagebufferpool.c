/* GStreamer
 *
 * Copyright (C) 2023 Igalia, S.L.
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

#if GST_VULKAN_HAVE_VIDEO_EXTENSIONS
#include "gst/vulkan/gstvkvideoutils-private.h"
#endif

static GstVulkanInstance *instance;
static GstVulkanDevice *device;
static GstVulkanQueue *queue = NULL;

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
  gst_clear_object (&queue);
  gst_clear_object (&device);
  gst_object_unref (instance);
}

static void
setup_queue (guint expected_flags)
{
  queue = gst_vulkan_device_select_queue (device, VK_QUEUE_COMPUTE_BIT);

  fail_unless (GST_IS_VULKAN_QUEUE (queue));
}

static void
assert_multi_memory_plane_offsets (const gchar * format)
{
  GstBufferPool *pool;
  GstBuffer *buffer = NULL;
  GstFlowReturn ret;
  GstCaps *caps;
  GstStructure *config;
  GstVideoInfo info;
  GstVideoMeta *meta;
  gsize expected_offset = 0;

  caps = gst_caps_new_simple ("video/x-raw", "format", G_TYPE_STRING, format,
      "width", G_TYPE_INT, 320, "height", G_TYPE_INT, 240, NULL);
  fail_unless (caps != NULL);
  fail_unless (gst_video_info_from_caps (&info, caps));
  gst_caps_set_features_simple (caps,
      gst_caps_features_new_static_str (GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE,
          NULL));

  pool = gst_vulkan_image_buffer_pool_new (device);
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, 0, 1, 0);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  fail_unless (gst_buffer_pool_set_config (pool, config));
  gst_buffer_pool_set_active (pool, TRUE);

  ret = gst_buffer_pool_acquire_buffer (pool, &buffer, NULL);
  fail_unless (ret == GST_FLOW_OK);
  fail_unless (buffer != NULL);

  fail_unless_equals_int (gst_buffer_n_memory (buffer),
      GST_VIDEO_INFO_N_PLANES (&info));

  meta = gst_buffer_get_video_meta (buffer);
  fail_unless (meta != NULL);

  for (guint plane = 0; plane < GST_VIDEO_INFO_N_PLANES (&info); plane++) {
    guint idx = 0, len = 0;
    gsize skip = 0;

    fail_unless_equals_int64 (meta->offset[plane], expected_offset);
    fail_unless (gst_buffer_find_memory (buffer, meta->offset[plane], 1, &idx,
            &len, &skip));
    fail_unless_equals_int (idx, plane);

    expected_offset += gst_buffer_peek_memory (buffer, plane)->size;
  }

  gst_buffer_unref (buffer);
  gst_buffer_pool_set_active (pool, FALSE);
  gst_object_unref (pool);
  gst_caps_unref (caps);
}

static GstBufferPool *
create_buffer_pool (const char *format, VkImageUsageFlags usage,
    VkImageLayout initial_layout, guint64 initial_access, GstCaps * dec_caps)
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

  if (dec_caps)
    gst_vulkan_image_buffer_pool_config_set_decode_caps (config, dec_caps);

  fail_unless (gst_buffer_pool_set_config (pool, config));
  gst_buffer_pool_set_active (pool, TRUE);

  return pool;
}

GST_START_TEST (test_image)
{
  GstBufferPool *pool;
  GstFlowReturn ret;
  GstBuffer *buffer = NULL;

  setup_queue (VK_QUEUE_COMPUTE_BIT);
  pool = create_buffer_pool ("NV12", 0, VK_IMAGE_LAYOUT_UNDEFINED, 0, NULL);

  ret = gst_buffer_pool_acquire_buffer (pool, &buffer, NULL);
  fail_unless (ret == GST_FLOW_OK);
  gst_buffer_unref (buffer);

  gst_buffer_pool_set_active (pool, FALSE);
  gst_object_unref (pool);
}

GST_END_TEST;

GST_START_TEST (test_multi_memory_plane_offsets)
{
  static const gchar *formats[] = { "I420", "Y42B", "Y444" };

  for (guint i = 0; i < G_N_ELEMENTS (formats); i++)
    assert_multi_memory_plane_offsets (formats[i]);
}

GST_END_TEST;

#if GST_VULKAN_HAVE_VIDEO_EXTENSIONS
GST_START_TEST (test_vulkan_profiles)
{
  GstCaps *caps;
  /* *INDENT-OFF* */
  GstVulkanVideoProfile profile2 = { { 0, }, }, profile = {
    .profile = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR,
      .pNext = &profile.codec,
      .videoCodecOperation = VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR,
      .chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR,
      .chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_10_BIT_KHR,
      .lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_10_BIT_KHR,
    },
    .codec.h265dec = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_PROFILE_INFO_KHR,
      .stdProfileIdc = STD_VIDEO_H265_PROFILE_IDC_MAIN_10,
    }
  };
  /* *INDENT-ON* */

  caps = gst_vulkan_video_profile_to_caps (&profile);
  fail_unless (caps);

  fail_unless (gst_vulkan_video_profile_from_caps (&profile2, caps,
          GST_VULKAN_VIDEO_OPERATION_DECODE));
  gst_caps_unref (caps);
  fail_unless (profile2.profile.sType
      == VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR);
  fail_unless (profile2.profile.videoCodecOperation
      == profile.profile.videoCodecOperation);
  fail_unless (profile2.codec.h265dec.stdProfileIdc
      == profile.codec.h265dec.stdProfileIdc);
}

GST_END_TEST;

GST_START_TEST (test_decoding_image)
{
  GstBufferPool *pool;
  GstCaps *dec_caps;
  GstBuffer *buffer = NULL;
  GstFlowReturn ret;
  /* *INDENT-OFF* */
  GstVulkanVideoProfile profile = {
    .profile = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR,
      .pNext = &profile.codec,
      .videoCodecOperation = VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR,
      .chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR,
      .chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
      .lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
    },
    .codec.h264dec = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PROFILE_INFO_KHR,
      .stdProfileIdc = STD_VIDEO_H264_PROFILE_IDC_MAIN,
      .pictureLayout = VK_VIDEO_DECODE_H264_PICTURE_LAYOUT_PROGRESSIVE_KHR,
    }
  };
  /* *INDENT-ON* */

  /* force to use a queue with decoding support */
  if (queue && (device->physical_device->queue_family_ops[queue->family].video
          & VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR) == 0)
    gst_clear_object (&queue);

  if (!queue) {
    queue =
        gst_vulkan_device_select_queue (device, VK_QUEUE_VIDEO_DECODE_BIT_KHR);
  }

  if (!queue)
    return;

  if ((device->physical_device->queue_family_ops[queue->family].video
          & VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR) == 0)
    return;

  dec_caps = gst_vulkan_video_profile_to_caps (&profile);
  fail_unless (dec_caps);

  pool = create_buffer_pool ("NV12", VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR,
      VK_IMAGE_LAYOUT_VIDEO_DECODE_DST_KHR, VK_ACCESS_TRANSFER_WRITE_BIT,
      dec_caps);

  gst_caps_unref (dec_caps);

  ret = gst_buffer_pool_acquire_buffer (pool, &buffer, NULL);
  fail_unless (ret == GST_FLOW_OK);
  gst_buffer_unref (buffer);

  gst_buffer_pool_set_active (pool, FALSE);
  gst_object_unref (pool);
}

GST_END_TEST;
#endif /* GST_VULKAN_HAVE_VIDEO_EXTENSIONS */

static Suite *
vkimagebufferpool_suite (void)
{
  Suite *s = suite_create ("vkimagebufferpool");
  TCase *tc_basic = tcase_create ("general");
  gboolean have_instance;

  suite_add_tcase (s, tc_basic);
  tcase_add_checked_fixture (tc_basic, setup, teardown);

  instance = gst_vulkan_instance_new ();
  have_instance = gst_vulkan_instance_open (instance, NULL);
  gst_object_unref (instance);
  if (have_instance) {
    tcase_add_test (tc_basic, test_image);
    tcase_add_test (tc_basic, test_multi_memory_plane_offsets);
#if GST_VULKAN_HAVE_VIDEO_EXTENSIONS
    tcase_add_test (tc_basic, test_decoding_image);
    tcase_add_test (tc_basic, test_vulkan_profiles);
#endif
  }

  return s;
}

#ifdef __APPLE__
GST_CHECK_MAIN_NOFORK (vkimagebufferpool);
#else
GST_CHECK_MAIN (vkimagebufferpool);
#endif
