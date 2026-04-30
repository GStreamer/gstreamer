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
    tcase_add_test (tc_basic, test_vulkan_color_convert_rgba_reorder);
    tcase_add_test (tc_basic, test_vulkan_color_convert_av12_to_rgba);
    tcase_add_test (tc_basic, test_vulkan_color_convert_rgba_to_av12);
    tcase_add_test (tc_basic, test_vulkan_color_convert_rgbx_to_av12_alpha);
    tcase_add_test (tc_basic, test_vulkan_color_convert_av12_passthrough);
  }

  return s;
}

#ifdef __APPLE__
GST_CHECK_MAIN_NOFORK (vkcolorconvert);
#else
GST_CHECK_MAIN (vkcolorconvert);
#endif
