/* GStreamer
 *
 * unit test for vulkancolorconvert element
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>
#include <gst/vulkan/vulkan.h>

typedef struct _TestFrame
{
  gint width;
  gint height;
  GstVideoFormat v_format;
  guint8 *data[GST_VIDEO_MAX_PLANES];
} TestFrame;

#define IGNORE_MAGIC 0x05

static const guint8 rgba_reorder_data[] = { 0x49, 0x24, 0x72, 0xff };
static const guint8 rgbx_reorder_data[] = { 0x49, 0x24, 0x72, IGNORE_MAGIC };
static const guint8 argb_reorder_data[] = { 0xff, 0x49, 0x24, 0x72 };
static const guint8 xrgb_reorder_data[] = { IGNORE_MAGIC, 0x49, 0x24, 0x72 };
static const guint8 bgra_reorder_data[] = { 0x72, 0x24, 0x49, 0xff };
static const guint8 bgrx_reorder_data[] = { 0x72, 0x24, 0x49, IGNORE_MAGIC };
static const guint8 abgr_reorder_data[] = { 0xff, 0x72, 0x24, 0x49 };
static const guint8 xbgr_reorder_data[] = { IGNORE_MAGIC, 0x72, 0x24, 0x49 };

static TestFrame test_rgba_reorder[] = {
  {1, 1, GST_VIDEO_FORMAT_RGBA, {(guint8 *) & rgba_reorder_data}},
  {1, 1, GST_VIDEO_FORMAT_RGBx, {(guint8 *) & rgbx_reorder_data}},
  {1, 1, GST_VIDEO_FORMAT_ARGB, {(guint8 *) & argb_reorder_data}},
  {1, 1, GST_VIDEO_FORMAT_xRGB, {(guint8 *) & xrgb_reorder_data}},
  {1, 1, GST_VIDEO_FORMAT_BGRA, {(guint8 *) & bgra_reorder_data}},
  {1, 1, GST_VIDEO_FORMAT_BGRx, {(guint8 *) & bgrx_reorder_data}},
  {1, 1, GST_VIDEO_FORMAT_ABGR, {(guint8 *) & abgr_reorder_data}},
  {1, 1, GST_VIDEO_FORMAT_xBGR, {(guint8 *) & xbgr_reorder_data}},
};

static const guint8 alpha_data[] = { 0x33, 0x77, 0xcc, 0xff };

static GstBuffer *
create_black_av12_buffer (const GstVideoInfo * info)
{
  GstBuffer *buf;
  GstMapInfo map_info;

  fail_unless_equals_int (GST_VIDEO_INFO_FORMAT (info), GST_VIDEO_FORMAT_AV12);
  fail_unless_equals_int (GST_VIDEO_INFO_WIDTH (info), 2);
  fail_unless_equals_int (GST_VIDEO_INFO_HEIGHT (info), 2);

  buf = gst_buffer_new_allocate (NULL, GST_VIDEO_INFO_SIZE (info), NULL);
  fail_unless (buf != NULL);
  fail_unless (gst_buffer_map (buf, &map_info, GST_MAP_WRITE));
  memset (map_info.data, 0, map_info.size);

  for (guint y = 0; y < GST_VIDEO_INFO_HEIGHT (info); y++) {
    guint8 *row = map_info.data + GST_VIDEO_INFO_PLANE_OFFSET (info, 0)
        + y * GST_VIDEO_INFO_PLANE_STRIDE (info, 0);

    memset (row, 0, GST_VIDEO_INFO_WIDTH (info));
  }

  for (guint y = 0; y < GST_VIDEO_INFO_COMP_HEIGHT (info, 1); y++) {
    guint8 *row = map_info.data + GST_VIDEO_INFO_PLANE_OFFSET (info, 1)
        + y * GST_VIDEO_INFO_PLANE_STRIDE (info, 1);

    for (guint x = 0; x < GST_VIDEO_INFO_COMP_WIDTH (info, 1); x++) {
      row[x * 2] = 0x80;
      row[x * 2 + 1] = 0x80;
    }
  }

  for (guint y = 0; y < GST_VIDEO_INFO_HEIGHT (info); y++) {
    guint8 *row = map_info.data + GST_VIDEO_INFO_PLANE_OFFSET (info, 2)
        + y * GST_VIDEO_INFO_PLANE_STRIDE (info, 2);

    for (guint x = 0; x < GST_VIDEO_INFO_WIDTH (info); x++)
      row[x] = alpha_data[y * GST_VIDEO_INFO_WIDTH (info) + x];
  }

  gst_buffer_unmap (buf, &map_info);

  return buf;
}

static GstBuffer *
create_black_rgb_family_buffer (const GstVideoInfo * info)
{
  GstBuffer *buf;
  GstMapInfo map_info;
  gboolean has_alpha = GST_VIDEO_INFO_FORMAT (info) == GST_VIDEO_FORMAT_RGBA;
  if (!has_alpha) {
    fail_unless_equals_int (GST_VIDEO_INFO_FORMAT (info),
        GST_VIDEO_FORMAT_RGBx);
  }

  fail_unless_equals_int (GST_VIDEO_INFO_WIDTH (info), 2);
  fail_unless_equals_int (GST_VIDEO_INFO_HEIGHT (info), 2);

  buf = gst_buffer_new_allocate (NULL, GST_VIDEO_INFO_SIZE (info), NULL);
  fail_unless (buf != NULL);
  fail_unless (gst_buffer_map (buf, &map_info, GST_MAP_WRITE));
  memset (map_info.data, 0, map_info.size);

  for (guint y = 0; y < GST_VIDEO_INFO_HEIGHT (info); y++) {
    guint8 *row = map_info.data + GST_VIDEO_INFO_PLANE_OFFSET (info, 0)
        + y * GST_VIDEO_INFO_PLANE_STRIDE (info, 0);

    for (guint x = 0; x < GST_VIDEO_INFO_WIDTH (info); x++) {
      row[x * 4 + 0] = 0;
      row[x * 4 + 1] = 0;
      row[x * 4 + 2] = 0;
      /* Non-alpha formats get 0, to validate that conversion gives 0xff */
      row[x * 4 + 3] = has_alpha ?
          alpha_data[y * GST_VIDEO_INFO_WIDTH (info) + x] : 0;
    }
  }

  gst_buffer_unmap (buf, &map_info);

  return buf;
}

static GstBuffer *
convert_buffer (GstVideoInfo * in_info, GstVideoInfo * out_info,
    GstBuffer * inbuf)
{
  GstHarness *h =
      gst_harness_new_parse
      ("vulkanupload ! vulkancolorconvert ! vulkandownload");
  GstCaps *in_caps = gst_video_info_to_caps (in_info);
  GstCaps *out_caps = gst_video_info_to_caps (out_info);
  GstBuffer *outbuf;

  gst_harness_set_caps (h, in_caps, out_caps);

  outbuf = gst_harness_push_and_pull (h, inbuf);
  gst_harness_teardown (h);

  return outbuf;
}

static void
check_av12_buffers_equal (GstBuffer * outbuf, GstBuffer * expected,
    const GstVideoInfo * info)
{
  GstMapInfo out_map, expected_map;

  fail_unless (gst_buffer_map (outbuf, &out_map, GST_MAP_READ));
  fail_unless (gst_buffer_map (expected, &expected_map, GST_MAP_READ));

  for (guint plane = 0; plane < GST_VIDEO_INFO_N_PLANES (info); plane++) {
    gint comp[GST_VIDEO_MAX_COMPONENTS];
    guint width, height;

    gst_video_format_info_component (info->finfo, plane, comp);
    width = GST_VIDEO_INFO_COMP_WIDTH (info, comp[0]) *
        GST_VIDEO_INFO_COMP_PSTRIDE (info, comp[0]);
    height = GST_VIDEO_INFO_COMP_HEIGHT (info, comp[0]);

    for (guint y = 0; y < height; y++) {
      const guint8 *out_row = out_map.data +
          GST_VIDEO_INFO_PLANE_OFFSET (info, plane) +
          y * GST_VIDEO_INFO_PLANE_STRIDE (info, plane);
      const guint8 *expected_row = expected_map.data +
          GST_VIDEO_INFO_PLANE_OFFSET (info, plane) +
          y * GST_VIDEO_INFO_PLANE_STRIDE (info, plane);

      fail_unless (memcmp (out_row, expected_row, width) == 0);
    }
  }

  gst_buffer_unmap (expected, &expected_map);
  gst_buffer_unmap (outbuf, &out_map);
}

GST_START_TEST (test_vulkan_color_convert_rgba_reorder)
{
  GstHarness *h =
      gst_harness_new_parse
      ("vulkanupload ! vulkancolorconvert ! vulkandownload");
  int i, j, k;

  for (i = 0; i < G_N_ELEMENTS (test_rgba_reorder); i++) {
    for (j = 0; j < G_N_ELEMENTS (test_rgba_reorder); j++) {
      GstCaps *in_caps, *out_caps;
      GstVideoInfo in_info, out_info;
      GstBuffer *inbuf, *outbuf;
      GstMapInfo map_info;

      fail_unless (gst_video_info_set_format (&in_info,
              test_rgba_reorder[i].v_format, test_rgba_reorder[i].width,
              test_rgba_reorder[i].height));
      fail_unless (gst_video_info_set_format (&out_info,
              test_rgba_reorder[j].v_format, test_rgba_reorder[j].width,
              test_rgba_reorder[j].height));

      in_caps = gst_video_info_to_caps (&in_info);
      out_caps = gst_video_info_to_caps (&out_info);

      gst_harness_set_caps (h, in_caps, out_caps);

      GST_INFO ("converting from %s to %s",
          gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&in_info)),
          gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&out_info)));

      inbuf =
          gst_buffer_new_wrapped_full (0, test_rgba_reorder[i].data[0], 4, 0, 4,
          NULL, NULL);
      outbuf = gst_harness_push_and_pull (h, inbuf);

      fail_unless (gst_buffer_map (outbuf, &map_info, GST_MAP_READ));
      fail_unless (map_info.size == out_info.size);

      for (k = 0; k < out_info.size; k++) {
        if (test_rgba_reorder[j].data[0][k] != IGNORE_MAGIC
            && map_info.data[k] != IGNORE_MAGIC) {
          guint8 *expected = test_rgba_reorder[j].data[0];
          GST_DEBUG ("%i 0x%x =? 0x%x", k, expected[k],
              (guint) map_info.data[k]);
          fail_unless (expected[k] == map_info.data[k]);
        }
      }
      gst_buffer_unmap (outbuf, &map_info);
      gst_buffer_unref (outbuf);
    }
  }

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_vulkan_color_convert_av12_to_rgba)
{
  GstVideoInfo in_info, out_info;
  GstBuffer *inbuf, *outbuf;
  GstMapInfo map_info;

  fail_unless (gst_video_info_set_format (&in_info, GST_VIDEO_FORMAT_AV12, 2,
          2));
  fail_unless (gst_video_info_set_format (&out_info, GST_VIDEO_FORMAT_RGBA, 2,
          2));

  inbuf = create_black_av12_buffer (&in_info);
  outbuf = convert_buffer (&in_info, &out_info, inbuf);
  fail_unless (outbuf != NULL);

  fail_unless (gst_buffer_map (outbuf, &map_info, GST_MAP_READ));
  fail_unless_equals_int (map_info.size, GST_VIDEO_INFO_SIZE (&out_info));

  for (guint i = 0; i < G_N_ELEMENTS (alpha_data); i++) {
    guint8 *pixel = map_info.data + i * 4;

    /* YUV->RGB math and 8-bit quantization may produce near-black values. */
    fail_unless (pixel[0] <= 2);
    fail_unless (pixel[1] <= 2);
    fail_unless (pixel[2] <= 2);
    fail_unless_equals_int (pixel[3], alpha_data[i]);
  }

  gst_buffer_unmap (outbuf, &map_info);
  gst_buffer_unref (outbuf);
}

GST_END_TEST;

static void
check_rgb_family_to_av12 (GstVideoFormat in_format)
{
  GstVideoInfo in_info, out_info;
  GstBuffer *inbuf, *outbuf;
  GstMapInfo map_info;
  gboolean has_alpha;

  fail_unless (gst_video_info_set_format (&in_info, in_format, 2, 2));
  fail_unless (gst_video_info_set_format (&out_info, GST_VIDEO_FORMAT_AV12, 2,
          2));

  has_alpha = GST_VIDEO_FORMAT_INFO_HAS_ALPHA (in_info.finfo);
  inbuf = create_black_rgb_family_buffer (&in_info);
  outbuf = convert_buffer (&in_info, &out_info, inbuf);
  fail_unless (outbuf != NULL);

  fail_unless (gst_buffer_map (outbuf, &map_info, GST_MAP_READ));
  fail_unless_equals_int (map_info.size, GST_VIDEO_INFO_SIZE (&out_info));

  for (guint y = 0; y < GST_VIDEO_INFO_HEIGHT (&out_info); y++) {
    guint8 *row = map_info.data + GST_VIDEO_INFO_PLANE_OFFSET (&out_info, 2)
        + y * GST_VIDEO_INFO_PLANE_STRIDE (&out_info, 2);

    for (guint x = 0; x < GST_VIDEO_INFO_WIDTH (&out_info); x++)
      fail_unless_equals_int (row[x], has_alpha ?
          alpha_data[y * GST_VIDEO_INFO_WIDTH (&out_info) + x] : 0xff);
  }

  gst_buffer_unmap (outbuf, &map_info);
  gst_buffer_unref (outbuf);
}

GST_START_TEST (test_vulkan_color_convert_rgba_to_av12)
{
  check_rgb_family_to_av12 (GST_VIDEO_FORMAT_RGBA);
}

GST_END_TEST;

GST_START_TEST (test_vulkan_color_convert_rgbx_to_av12_alpha)
{
  check_rgb_family_to_av12 (GST_VIDEO_FORMAT_RGBx);
}

GST_END_TEST;

GST_START_TEST (test_vulkan_color_convert_av12_passthrough)
{
  GstVideoInfo info;
  GstBuffer *inbuf, *outbuf;

  fail_unless (gst_video_info_set_format (&info, GST_VIDEO_FORMAT_AV12, 2, 2));

  inbuf = create_black_av12_buffer (&info);
  gst_buffer_ref (inbuf);
  outbuf = convert_buffer (&info, &info, inbuf);
  fail_unless (outbuf != NULL);

  check_av12_buffers_equal (outbuf, inbuf, &info);

  gst_buffer_unref (inbuf);
  gst_buffer_unref (outbuf);
}

GST_END_TEST;

/* These conversions only sample and transfer the composite image. They need
 * MUTABLE_FORMAT for plane views, which the image buffer pool sets on
 * composite multi-planar images when the driver allows it. Returns an
 * inactive pool, or NULL if the device can't allocate such images. */
static GstBufferPool *
create_composite_pool (GstVulkanDevice * device, const GstVideoInfo * info,
    VkFormat * format)
{
  const VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT |
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  GstBufferPool *pool = gst_vulkan_image_buffer_pool_new (device);
  GstStructure *config = gst_buffer_pool_get_config (pool);
  GstCaps *caps = gst_video_info_to_caps (info);
  GstBuffer *buffer = NULL;
  GstVulkanImageMemory *image;
  gboolean usable;

  gst_caps_set_features_simple (caps,
      gst_caps_features_new_static_str (GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE,
          NULL));
  gst_buffer_pool_config_set_params (config, caps, info->size, 0, 0);
  gst_caps_unref (caps);
  gst_vulkan_image_buffer_pool_config_set_allocation_params (config, usage,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_LAYOUT_UNDEFINED, 0);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_add_option (config,
      GST_BUFFER_POOL_OPTION_VULKAN_IMAGE_MULTIPLANAR_YUV);
  fail_unless (gst_buffer_pool_set_config (pool, config));

  fail_unless (gst_buffer_pool_set_active (pool, TRUE));
  fail_unless_equals_int (gst_buffer_pool_acquire_buffer (pool, &buffer, NULL),
      GST_FLOW_OK);
  image = (GstVulkanImageMemory *) gst_buffer_peek_memory (buffer, 0);
  usable = gst_buffer_n_memory (buffer) == 1
      && (image->create_info.flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT);
  *format = image->create_info.format;
  gst_buffer_unref (buffer);
  fail_unless (gst_buffer_pool_set_active (pool, FALSE));

  if (!usable)
    gst_clear_object (&pool);

  return pool;
}

/* Request the composite fixture at the allocation boundary and verify that
 * the producer actually used it before accepting any pixel results. */
typedef struct
{
  GstBufferPool *pool;
  VkFormat format;
  gboolean saw_buffer;
} CompositeAllocation;

static GstPadProbeReturn
composite_allocation_probe (GstPad * pad, GstPadProbeInfo * probe,
    gpointer user_data)
{
  CompositeAllocation *allocation = user_data;

  if (GST_PAD_PROBE_INFO_TYPE (probe) & GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM) {
    GstQuery *query = GST_PAD_PROBE_INFO_QUERY (probe);

    if (GST_QUERY_TYPE (query) == GST_QUERY_ALLOCATION) {
      GstStructure *config = gst_buffer_pool_get_config (allocation->pool);
      guint size;

      gst_buffer_pool_config_get_params (config, NULL, &size, NULL, NULL);
      gst_structure_free (config);
      while (gst_query_get_n_allocation_pools (query))
        gst_query_remove_nth_allocation_pool (query, 0);
      gst_query_add_allocation_pool (query, allocation->pool, size, 0, 0);
      gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
      return GST_PAD_PROBE_HANDLED;
    }
  } else if (GST_PAD_PROBE_INFO_TYPE (probe) & GST_PAD_PROBE_TYPE_BUFFER) {
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER (probe);
    GstMemory *mem = gst_buffer_peek_memory (buffer, 0);
    GstVulkanImageMemory *image;

    fail_unless_equals_int (gst_buffer_n_memory (buffer), 1);
    fail_unless (gst_is_vulkan_image_memory (mem));
    image = (GstVulkanImageMemory *) mem;
    fail_unless_equals_int (image->create_info.format, allocation->format);
    fail_unless (image->create_info.flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT);
    allocation->saw_buffer = TRUE;
  }

  return GST_PAD_PROBE_OK;
}

/* Keep each RGB colour constant over a 2x2 block so 4:2:0 downsampling has
 * an unambiguous reference. Chroma varies in both directions. */
static void
composite_rgb_pixel (guint x, guint y, guint frame, gdouble rgb[3])
{
  rgb[0] = 48 + 24 * (x / 2) + frame * 8;
  rgb[1] = 64 + 32 * (y / 2);
  rgb[2] = 176 - 16 * ((x / 2 + y / 2 + frame) % 4);
}

static guint8
composite_yuv_value (guint component, guint x, guint y, guint frame)
{
  if (component == 0)
    return 80 + (x * 7 + y * 11 + frame * 13) % 80;
  /* Unequal U/V, changed between frames, also catch swapped/stale views. */
  return component == 1 ? 96 + frame * 32 : 160 - frame * 32;
}

static void
check_composite_color_convert (GstVideoFormat format, gboolean composite_input)
{
  GstHarness *h = gst_harness_new_parse ("vulkanupload name=upload ! "
      "vulkancolorconvert name=convert ! vulkandownload");
  GstElement *producer = gst_bin_get_by_name (GST_BIN (h->element),
      composite_input ? "upload" : "convert");
  GstPad *sinkpad = gst_element_get_static_pad (producer, "sink");
  GstPad *srcpad = gst_element_get_static_pad (producer, "src");
  GstQuery *query = gst_query_new_context (GST_VULKAN_DEVICE_CONTEXT_TYPE_STR);
  GstContext *context;
  GstVulkanDevice *device = NULL;
  GstVideoInfo yuv_info, rgb_info, *in_info, *out_info;
  CompositeAllocation allocation = { 0, };
  gulong probe_id;
  const gchar *name = gst_video_format_to_string (format);

  fail_unless (gst_pad_query (sinkpad, query));
  gst_query_parse_context (query, &context);
  fail_unless (gst_context_get_vulkan_device (context, &device));
  gst_query_unref (query);
  fail_unless (gst_video_info_set_format (&yuv_info, format, 12, 8));
  fail_unless (gst_video_info_set_format (&rgb_info, GST_VIDEO_FORMAT_RGBA, 12,
          8));
  fail_unless (gst_video_colorimetry_from_string (&yuv_info.colorimetry,
          "bt601"));
  yuv_info.colorimetry.range = GST_VIDEO_COLOR_RANGE_0_255;
  rgb_info.colorimetry = yuv_info.colorimetry;
  rgb_info.colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_RGB;
  in_info = composite_input ? &yuv_info : &rgb_info;
  out_info = composite_input ? &rgb_info : &yuv_info;

  allocation.pool = create_composite_pool (device, &yuv_info,
      &allocation.format);
  if (!allocation.pool) {
    g_print ("SKIP composite %s %s: device cannot allocate a composite "
        "mutable-format image\n", name, composite_input ? "input" : "output");
    goto cleanup;
  }

  probe_id = gst_pad_add_probe (srcpad,
      GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM | GST_PAD_PROBE_TYPE_BUFFER,
      composite_allocation_probe, &allocation, NULL);
  gst_harness_set_caps (h, gst_video_info_to_caps (in_info),
      gst_video_info_to_caps (out_info));

  for (guint frame = 0; frame < 2; frame++) {
    GstBuffer *input = gst_buffer_new_allocate (NULL, in_info->size, NULL);
    GstBuffer *output;
    GstVideoFrame mapped;

    fail_unless (gst_video_frame_map (&mapped, in_info, input, GST_MAP_WRITE));
    for (guint c = 0; c < GST_VIDEO_INFO_N_COMPONENTS (in_info); c++) {
      guint8 *data = GST_VIDEO_FRAME_COMP_DATA (&mapped, c);
      for (guint y = 0; y < GST_VIDEO_FRAME_COMP_HEIGHT (&mapped, c); y++) {
        for (guint x = 0; x < GST_VIDEO_FRAME_COMP_WIDTH (&mapped, c); x++) {
          gdouble rgb[3];
          guint8 value;

          composite_rgb_pixel (x, y, frame, rgb);
          value = composite_input ? composite_yuv_value (c, x, y, frame) :
              (c == 3 ? 255 : rgb[c]);
          data[y * GST_VIDEO_FRAME_COMP_STRIDE (&mapped, c) +
              x * GST_VIDEO_FRAME_COMP_PSTRIDE (&mapped, c)] = value;
        }
      }
    }
    gst_video_frame_unmap (&mapped);
    allocation.saw_buffer = FALSE;
    fail_unless_equals_int (gst_harness_push (h, input), GST_FLOW_OK);
    fail_unless (allocation.saw_buffer);
    output = gst_harness_try_pull (h);
    fail_unless (output != NULL);
    fail_unless (gst_video_frame_map (&mapped, out_info, output, GST_MAP_READ));

    for (guint c = 0; c < GST_VIDEO_INFO_N_COMPONENTS (out_info); c++) {
      const guint8 *data = GST_VIDEO_FRAME_COMP_DATA (&mapped, c);
      for (guint y = 0; y < GST_VIDEO_FRAME_COMP_HEIGHT (&mapped, c); y++) {
        for (guint x = 0; x < GST_VIDEO_FRAME_COMP_WIDTH (&mapped, c); x++) {
          gdouble expected;
          guint8 actual = data[y * GST_VIDEO_FRAME_COMP_STRIDE (&mapped, c) +
              x * GST_VIDEO_FRAME_COMP_PSTRIDE (&mapped, c)];

          if (composite_input) {
            gdouble luma = composite_yuv_value (0, x, y, frame);
            gdouble u = composite_yuv_value (1, 0, 0, frame) - 128.;
            gdouble v = composite_yuv_value (2, 0, 0, frame) - 128.;
            gdouble rgba[] = { luma + 1.402 * v,
              luma - 0.344136 * u - 0.714136 * v, luma + 1.772 * u, 255.
            };
            expected = CLAMP (rgba[c], 0., 255.);
          } else {
            gdouble rgb[3], luma, yuv[3];

            composite_rgb_pixel (c == 0 ? x : x * 2,
                c == 0 ? y : y * 2, frame, rgb);
            luma = 0.299 * rgb[0] + 0.587 * rgb[1] + 0.114 * rgb[2];
            yuv[0] = luma;
            yuv[1] = (rgb[2] - luma) / 1.772 + 128.;
            yuv[2] = (rgb[0] - luma) / 1.402 + 128.;
            expected = yuv[c];
          }
          fail_unless (ABS (actual - expected) <= 2.,
              "composite %s %s frame %u component %u at %u,%u: "
              "expected %.2f, got %u", name,
              composite_input ? "input" : "output", frame, c, x, y,
              expected, actual);
        }
      }
    }
    gst_video_frame_unmap (&mapped);
    gst_buffer_unref (output);
  }
  g_print ("PASS composite %s %s: verified image layout and two frames\n",
      name, composite_input ? "input" : "output");
  gst_pad_remove_probe (srcpad, probe_id);

cleanup:
  gst_harness_teardown (h);
  if (allocation.pool) {
    gst_buffer_pool_set_active (allocation.pool, FALSE);
    gst_object_unref (allocation.pool);
  }
  gst_object_unref (device);
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);
  gst_object_unref (producer);
}

GST_START_TEST (test_vulkan_color_convert_composite_nv12_to_rgba)
{
  check_composite_color_convert (GST_VIDEO_FORMAT_NV12, TRUE);
}

GST_END_TEST;

GST_START_TEST (test_vulkan_color_convert_rgba_to_composite_nv12)
{
  check_composite_color_convert (GST_VIDEO_FORMAT_NV12, FALSE);
}

GST_END_TEST;


GST_START_TEST (test_vulkan_color_convert_composite_i420_to_rgba)
{
  check_composite_color_convert (GST_VIDEO_FORMAT_I420, TRUE);
}

GST_END_TEST;

GST_START_TEST (test_vulkan_color_convert_rgba_to_composite_i420)
{
  check_composite_color_convert (GST_VIDEO_FORMAT_I420, FALSE);
}

GST_END_TEST;

static Suite *
vkcolorconvert_suite (void)
{
  Suite *s = suite_create ("vkcolorconvert");
  TCase *tc_basic = tcase_create ("general");
  GstVulkanInstance *instance;
  gboolean have_instance;

  suite_add_tcase (s, tc_basic);

  instance = gst_vulkan_instance_new ();
  have_instance = gst_vulkan_instance_open (instance, NULL);
  gst_object_unref (instance);
  if (have_instance) {
    tcase_add_test (tc_basic, test_vulkan_color_convert_composite_i420_to_rgba);
    tcase_add_test (tc_basic, test_vulkan_color_convert_rgba_to_composite_i420);
    tcase_add_test (tc_basic, test_vulkan_color_convert_composite_nv12_to_rgba);
    tcase_add_test (tc_basic, test_vulkan_color_convert_rgba_to_composite_nv12);
    tcase_add_test (tc_basic, test_vulkan_color_convert_rgba_reorder);
    tcase_add_test (tc_basic, test_vulkan_color_convert_av12_to_rgba);
    tcase_add_test (tc_basic, test_vulkan_color_convert_rgba_to_av12);
    tcase_add_test (tc_basic, test_vulkan_color_convert_rgbx_to_av12_alpha);
    tcase_add_test (tc_basic, test_vulkan_color_convert_av12_passthrough);
  } else {
    g_print ("SKIP vkcolorconvert: Vulkan instance unavailable\n");
  }

  return s;
}

#ifdef __APPLE__
GST_CHECK_MAIN_NOFORK (vkcolorconvert);
#else
GST_CHECK_MAIN (vkcolorconvert);
#endif
