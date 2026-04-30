/* GStreamer
 *
 * unit test for vulkandownload element
 * Copyright (C) 2026 Piotr Brzeziński <piotr@centricular.com>
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

#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>
#include <gst/video/video.h>
#include <gst/vulkan/vulkan.h>

static const struct
{
  guint width;
  guint height;
} stride_test_resolutions[] = {
  {123, 345},
  {678, 789},
  {511, 257},
  {855, 481},
};

static gboolean
cmp_buffers (GstBuffer * buf1, GstBuffer * buf2, const GstVideoInfo * info)
{
  GstVideoFrame frame1, frame2;
  gint comp[GST_VIDEO_MAX_COMPONENTS], stride1, stride2;
  guint32 width, height;
  gboolean ret = FALSE;
  GstVideoMeta *meta1, *meta2;

  fail_unless_equals_int (gst_buffer_n_memory (buf1),
      GST_VIDEO_INFO_N_PLANES (info));
  fail_unless_equals_int (gst_buffer_n_memory (buf2),
      GST_VIDEO_INFO_N_PLANES (info));

  meta1 = gst_buffer_get_video_meta (buf1);
  meta2 = gst_buffer_get_video_meta (buf2);
  fail_unless (meta1 != NULL);
  fail_unless (meta2 != NULL);
  fail_unless_equals_int (meta1->format, meta2->format);

  fail_unless (!GST_VIDEO_FORMAT_INFO_IS_COMPLEX (info->finfo));
  fail_unless (gst_video_frame_map (&frame1, info, buf1, GST_MAP_READ));
  fail_unless (gst_video_frame_map (&frame2, info, buf2, GST_MAP_READ));

  fail_unless_equals_int (frame1.info.width, frame2.info.width);
  fail_unless_equals_int (frame1.info.height, frame2.info.height);

  for (int plane = 0; plane < GST_VIDEO_INFO_N_PLANES (info); plane++) {
    guint8 *row1, *row2;

    gst_video_format_info_component (info->finfo, plane, comp);

    width = GST_VIDEO_INFO_COMP_WIDTH (info, comp[0])
        * GST_VIDEO_INFO_COMP_PSTRIDE (info, comp[0]);
    height = GST_VIDEO_INFO_COMP_HEIGHT (info, comp[0]);

    stride1 = GST_VIDEO_INFO_PLANE_STRIDE (&frame1.info, plane);
    stride2 = GST_VIDEO_INFO_PLANE_STRIDE (&frame2.info, plane);

    row1 = frame1.data[plane];
    row2 = frame2.data[plane];

    for (int i = 0; i < height; i++) {
      if (memcmp (row1, row2, width) != 0) {
        guint x;

        for (x = 0; x < width; x++) {
          if (row1[x] != row2[x])
            break;
        }

        GST_ERROR ("Mismatch plane %d row %d/%u byte %u/%u: expected 0x%02x, "
            "got 0x%02x", plane, i, height, x, width, row1[x], row2[x]);
        goto bail;
      }

      row1 += stride1;
      row2 += stride2;
    }
  }

  ret = TRUE;

bail:
  gst_video_frame_unmap (&frame1);
  gst_video_frame_unmap (&frame2);

  return ret;
}

static gboolean
fill_stride_test_image_buffer (GstBuffer * buf, const GstVideoInfo * info)
{
  GstVideoFrame frame;
  gint width, height, stride;

  if (!gst_video_frame_map (&frame, info, buf, GST_MAP_WRITE))
    return FALSE;

  width = GST_VIDEO_INFO_WIDTH (info);
  height = GST_VIDEO_INFO_HEIGHT (info);
  stride = GST_VIDEO_FRAME_PLANE_STRIDE (&frame, 0);

  for (gint y = 0; y < height; y++) {
    guint8 *row =
        (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (&frame, 0) + y * stride;
    const guint8 r = (y * 3) & 0xff;
    const guint8 g = (y * 7) & 0xff;
    const guint8 b = (y * 11) & 0xff;

    for (gint x = 0; x < width; x++) {
      row[x * 4 + 0] = r;
      row[x * 4 + 1] = g;
      row[x * 4 + 2] = b;
      row[x * 4 + 3] = 0xff;
    }
  }

  gst_video_frame_unmap (&frame);

  return TRUE;
}

static gboolean
assert_one_memory_per_plane (GstBuffer * buf, const GstVideoInfo * info,
    GstMemory ** expected_mems)
{
  GstVideoMeta *meta;

  fail_unless_equals_int (gst_buffer_n_memory (buf),
      GST_VIDEO_INFO_N_PLANES (info));

  meta = gst_buffer_get_video_meta (buf);
  fail_unless (meta != NULL);

  for (guint plane = 0; plane < GST_VIDEO_INFO_N_PLANES (info); plane++) {
    guint idx = 0, len = 0;
    gsize skip = 0;

    fail_unless (gst_buffer_find_memory (buf, meta->offset[plane], 1, &idx,
            &len, &skip));
    fail_unless_equals_int (idx, plane);

    if (expected_mems)
      fail_unless (gst_buffer_peek_memory (buf, plane) == expected_mems[plane]);
  }

  return TRUE;
}

static gboolean
fill_multiplane_test_buffer (GstBuffer * buf, const GstVideoInfo * info)
{
  GstVideoFrame frame;
  GstMemory *mems[GST_VIDEO_MAX_PLANES] = { NULL, };

  for (guint plane = 0; plane < GST_VIDEO_INFO_N_PLANES (info); plane++)
    mems[plane] = gst_buffer_peek_memory (buf, plane);

  assert_one_memory_per_plane (buf, info, mems);

  if (!gst_video_frame_map (&frame, info, buf, GST_MAP_WRITE))
    return FALSE;

  for (guint plane = 0; plane < GST_VIDEO_INFO_N_PLANES (info); plane++) {
    gint comp[GST_VIDEO_MAX_COMPONENTS];
    guint32 width, height;
    gint stride;

    gst_video_format_info_component (info->finfo, plane, comp);

    width = GST_VIDEO_INFO_COMP_WIDTH (info, comp[0])
        * GST_VIDEO_INFO_COMP_PSTRIDE (info, comp[0]);
    height = GST_VIDEO_INFO_COMP_HEIGHT (info, comp[0]);
    stride = GST_VIDEO_FRAME_PLANE_STRIDE (&frame, plane);

    for (guint y = 0; y < height; y++) {
      guint8 *row =
          (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (&frame, plane) + y * stride;

      for (guint x = 0; x < width; x++)
        row[x] = (plane * 67 + x * 5 + y * 13) & 0xff;
    }
  }

  gst_video_frame_unmap (&frame);
  assert_one_memory_per_plane (buf, info, mems);

  return TRUE;
}

static GstBuffer *
create_stride_test_vulkan_image_buffer (GstVulkanDevice * device,
    GstVideoInfo * info)
{
  /* Finds an odd-sized linear image layout where the source image memory
   * layout differs from the tightly-packed raw output layout.
   * I originally found this bug on an M4 Pro Mac at 854x480, but odd resolutions
   * should be more reliable at triggering the problem on different hardware. */
  for (guint i = 0; i < G_N_ELEMENTS (stride_test_resolutions); i++) {
    const guint width = stride_test_resolutions[i].width;
    const guint height = stride_test_resolutions[i].height;
    GstBuffer *buf;
    GstMemory *mem;
    VkImageCreateInfo image_info;
    GstVulkanImageMemory *img_mem;
    VkImageSubresource subresource = {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .mipLevel = 0,
      .arrayLayer = 0,
    };
    VkSubresourceLayout layout;
    VkFormat vk_format;
    gsize offsets[GST_VIDEO_MAX_PLANES] = { 0, };
    gint strides[GST_VIDEO_MAX_PLANES] = { 0, };
    guint tight_stride;

    fail_unless (gst_video_info_set_format (info,
            GST_VIDEO_FORMAT_RGBA, width, height));
    tight_stride = GST_VIDEO_INFO_PLANE_STRIDE (info, 0);
    vk_format = gst_vulkan_format_from_video_info (info, 0);

    /* *INDENT-OFF* */
    image_info = (VkImageCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = vk_format,
      .extent = { width, height, 1 },
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_LINEAR,
      .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
    };
    /* *INDENT-ON* */

    mem = gst_vulkan_image_memory_alloc_with_image_info (device, &image_info,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
        | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!mem)
      continue;

    img_mem = (GstVulkanImageMemory *) mem;
    vkGetImageSubresourceLayout (device->device, img_mem->image, &subresource,
        &layout);

    /* We want a padded stride value, that's when vulkandownload corrupted the output previously */
    if (layout.rowPitch <= tight_stride) {
      gst_memory_unref (mem);
      continue;
    }

    buf = gst_buffer_new ();
    fail_unless (buf != NULL);
    gst_buffer_append_memory (buf, mem);

    offsets[0] = layout.offset;
    strides[0] = (gint) layout.rowPitch;

    fail_unless (gst_buffer_add_video_meta_full (buf, GST_VIDEO_FRAME_FLAG_NONE,
            GST_VIDEO_FORMAT_RGBA, width, height, 1, offsets, strides) != NULL);
    fail_unless (fill_stride_test_image_buffer (buf, info));

    GST_INFO ("Using vkdownload stride test resolution %ux%u", width, height);

    return buf;
  }

  return NULL;
}

static GstBuffer *
create_multiplane_vulkan_image_buffer (GstVulkanDevice * device,
    GstVideoFormat format, guint width, guint height, GstVideoInfo * info)
{
  GstBuffer *buf;
  gsize offsets[GST_VIDEO_MAX_PLANES] = { 0, };
  gint strides[GST_VIDEO_MAX_PLANES] = { 0, };
  gsize cumulative_offset = 0;

  fail_unless (gst_video_info_set_format (info, format, width, height));

  buf = gst_buffer_new ();
  fail_unless (buf != NULL);

  for (guint plane = 0; plane < GST_VIDEO_INFO_N_PLANES (info); plane++) {
    GstMemory *mem;
    GstVulkanImageMemory *img_mem;
    VkImageCreateInfo image_info;
    VkImageSubresource subresource = {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .mipLevel = 0,
      .arrayLayer = 0,
    };
    VkSubresourceLayout layout;
    gint comp[GST_VIDEO_MAX_COMPONENTS];
    guint plane_width, plane_height;

    gst_video_format_info_component (info->finfo, plane, comp);
    plane_width = GST_VIDEO_INFO_COMP_WIDTH (info, comp[0]);
    plane_height = GST_VIDEO_INFO_COMP_HEIGHT (info, comp[0]);

    /* *INDENT-OFF* */
    image_info = (VkImageCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = gst_vulkan_format_from_video_info (info, plane),
      .extent = { plane_width, plane_height, 1 },
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_LINEAR,
      .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
    };
    /* *INDENT-ON* */

    mem = gst_vulkan_image_memory_alloc_with_image_info (device, &image_info,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
        | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    fail_unless (mem != NULL);

    img_mem = (GstVulkanImageMemory *) mem;
    vkGetImageSubresourceLayout (device->device, img_mem->image, &subresource,
        &layout);

    offsets[plane] = cumulative_offset + layout.offset;
    strides[plane] = (gint) layout.rowPitch;
    cumulative_offset += mem->size;

    gst_buffer_append_memory (buf, mem);
  }

  fail_unless (gst_buffer_add_video_meta_full (buf, GST_VIDEO_FRAME_FLAG_NONE,
          format, width, height, GST_VIDEO_INFO_N_PLANES (info), offsets,
          strides) != NULL);
  fail_unless (fill_multiplane_test_buffer (buf, info));

  return buf;
}

static void
run_multiplane_download_roundtrip_test (GstVideoFormat format)
{
  GstVulkanInstance *instance;
  GstVulkanDevice *device;
  GstHarness *h;
  GstContext *context;
  GstCaps *in_caps, *out_caps;
  GstVideoInfo info;
  GstBuffer *inbuf = NULL, *outbuf = NULL;

  h = gst_harness_new ("vulkandownload");
  context = gst_element_get_context (h->element,
      GST_VULKAN_INSTANCE_CONTEXT_TYPE_STR);
  fail_unless (context != NULL);
  fail_unless (gst_context_get_vulkan_instance (context, &instance));
  gst_context_unref (context);

  device = gst_vulkan_device_new_with_index (instance, 0);
  fail_unless (gst_vulkan_device_open (device, NULL));

  inbuf = create_multiplane_vulkan_image_buffer (device, format, 320, 240,
      &info);
  fail_unless (inbuf != NULL);

  in_caps = gst_video_info_to_caps (&info);
  fail_unless (in_caps != NULL);
  gst_caps_set_features_simple (in_caps,
      gst_caps_features_new_static_str (GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE,
          NULL));

  out_caps = gst_video_info_to_caps (&info);
  fail_unless (out_caps != NULL);

  gst_harness_set_caps (h, in_caps, out_caps);

  outbuf = gst_harness_push_and_pull (h, gst_buffer_ref (inbuf));
  fail_unless (outbuf != NULL);
  fail_unless (cmp_buffers (inbuf, outbuf, &info));

  gst_buffer_unref (inbuf);
  gst_buffer_unref (outbuf);
  gst_harness_teardown (h);
  gst_clear_object (&device);
  gst_clear_object (&instance);
}

GST_START_TEST (test_vulkan_download_uses_output_stride)
{
  GstVulkanInstance *instance;
  GstVulkanDevice *device;
  GstHarness *h;
  GstContext *context;
  GstCaps *in_caps, *out_caps;
  GstVideoInfo info;
  GstBuffer *inbuf = NULL, *outbuf = NULL;

  /* We need a Vulkan image whose plane layout differs from the tightly packed
   * raw output buffer so the test catches vkdownload using the source layout
   * instead of the destination one while setting up VkBufferImageCopy.
   * This problem was originally spotted on macOS with just vtdec ! vulkandownload. */

  h = gst_harness_new ("vulkandownload");
  context = gst_element_get_context (h->element,
      GST_VULKAN_INSTANCE_CONTEXT_TYPE_STR);
  fail_unless (context != NULL);
  fail_unless (gst_context_get_vulkan_instance (context, &instance));
  gst_context_unref (context);

  device = gst_vulkan_device_new_with_index (instance, 0);
  fail_unless (gst_vulkan_device_open (device, NULL));

  inbuf = create_stride_test_vulkan_image_buffer (device, &info);
  if (!inbuf) {
    GST_INFO ("Couldn't create a buffer required for vkdownload stride test");
    goto cleanup;
  }

  in_caps = gst_video_info_to_caps (&info);
  fail_unless (in_caps != NULL);
  gst_caps_set_features_simple (in_caps,
      gst_caps_features_new_static_str (GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE,
          NULL));

  out_caps = gst_video_info_to_caps (&info);
  fail_unless (out_caps != NULL);

  gst_harness_set_caps (h, in_caps, out_caps);

  outbuf = gst_harness_push_and_pull (h, gst_buffer_ref (inbuf));
  fail_unless (outbuf != NULL);

  fail_unless (cmp_buffers (inbuf, outbuf, &info));

  gst_buffer_unref (inbuf);
  gst_buffer_unref (outbuf);

cleanup:
  gst_harness_teardown (h);
  gst_clear_object (&device);
  gst_clear_object (&instance);
}

GST_END_TEST;

GST_START_TEST (test_vulkan_download_i420_roundtrip)
{
  run_multiplane_download_roundtrip_test (GST_VIDEO_FORMAT_I420);
}

GST_END_TEST;

GST_START_TEST (test_vulkan_download_a420_roundtrip)
{
  run_multiplane_download_roundtrip_test (GST_VIDEO_FORMAT_A420);
}

GST_END_TEST;

GST_START_TEST (test_vulkan_download_nv12_roundtrip)
{
  run_multiplane_download_roundtrip_test (GST_VIDEO_FORMAT_NV12);
}

GST_END_TEST;

GST_START_TEST (test_vulkan_download_av12_roundtrip)
{
  run_multiplane_download_roundtrip_test (GST_VIDEO_FORMAT_AV12);
}

GST_END_TEST;

static Suite *
vkdownload_suite (void)
{
  Suite *s = suite_create ("vkdownload");
  TCase *tc_basic = tcase_create ("general");
  GstVulkanInstance *instance;
  gboolean have_instance;

  suite_add_tcase (s, tc_basic);

  instance = gst_vulkan_instance_new ();
  have_instance = gst_vulkan_instance_open (instance, NULL);
  gst_object_unref (instance);
  if (have_instance) {
    tcase_add_test (tc_basic, test_vulkan_download_uses_output_stride);
    tcase_add_test (tc_basic, test_vulkan_download_i420_roundtrip);
    tcase_add_test (tc_basic, test_vulkan_download_a420_roundtrip);
    tcase_add_test (tc_basic, test_vulkan_download_nv12_roundtrip);
    tcase_add_test (tc_basic, test_vulkan_download_av12_roundtrip);
  }

  return s;
}

#ifdef __APPLE__
GST_CHECK_MAIN_NOFORK (vkdownload);
#else
GST_CHECK_MAIN (vkdownload);
#endif
