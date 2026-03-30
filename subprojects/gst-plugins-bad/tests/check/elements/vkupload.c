/* GStreamer
 *
 * unit test for vulkanupload element
 * Copyright (C) 2025 Igalia, S.L.
 *     Author: Víctor Jáquez <vjaquez@igalia.com>
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

static const gchar *formats[] = { "NV12", "RGBA" };

#define STRIDE_TEST_WIDTH 360
#define STRIDE_TEST_HEIGHT 480
#define STRIDE_TEST_INPUT_STRIDE 1472

static const struct
{
  guint width;
  guint height;
} resolutions[] = {
  {320, 240},
  {640, 480},
  {15, 10},
  {128, 96},
  {256, 144},
  {349, 287},
  {352, 289},
};

static gboolean
cmp_buffers (GstBuffer * buf1, GstBuffer * buf2, const GstVideoInfo * info)
{
  GstVideoFrame frame1, frame2;
  gint comp[GST_VIDEO_MAX_COMPONENTS], stride1, stride2;
  guint32 width, height;
  gboolean ret = FALSE;

  fail_unless (gst_video_frame_map (&frame1, info, buf1, GST_MAP_READ));
  fail_unless (gst_video_frame_map (&frame2, info, buf2, GST_MAP_READ));

  for (int plane = 0; plane < GST_VIDEO_INFO_N_PLANES (info); plane++) {
    guint8 *row1, *row2;

    gst_video_format_info_component (info->finfo, plane, comp);

    width = GST_VIDEO_INFO_COMP_WIDTH (info, comp[0])
        * GST_VIDEO_INFO_COMP_PSTRIDE (info, comp[0]);
    /* some tiled formats might have 0 pixel stride */
    if (width == 0) {
      width = MIN (GST_VIDEO_INFO_COMP_PSTRIDE (&frame1.info, plane),
          GST_VIDEO_INFO_COMP_PSTRIDE (&frame2.info, plane));
    }
    height = GST_VIDEO_INFO_COMP_HEIGHT (info, comp[0]);

    stride1 = GST_VIDEO_INFO_PLANE_STRIDE (&frame1.info, plane);
    stride2 = GST_VIDEO_INFO_PLANE_STRIDE (&frame2.info, plane);

    row1 = frame1.data[plane];
    row2 = frame2.data[plane];

    for (int i = 0; i < height; i++) {
      GST_MEMDUMP ("input row:", row1, width);
      GST_MEMDUMP ("output row:", row2, width);

      if (memcmp (row1, row2, width) != 0)
        goto bail;

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
fill_stride_test_rgba_buffer (GstBuffer * buf)
{
  GstMapInfo map;
  guint x, y;

  if (!gst_buffer_map (buf, &map, GST_MAP_WRITE))
    return FALSE;

  for (y = 0; y < STRIDE_TEST_HEIGHT; y++) {
    guint8 *row = map.data + y * STRIDE_TEST_INPUT_STRIDE;
    guint8 pixel[] = { (y * 3) & 0xff, (y * 7) & 0xff, (y * 11) & 0xff, 0xff };

    for (x = 0; x < STRIDE_TEST_WIDTH; x++)
      memcpy (row + x * 4, pixel, sizeof (pixel));

    memset (row + STRIDE_TEST_WIDTH * 4, 0x5a,
        STRIDE_TEST_INPUT_STRIDE - STRIDE_TEST_WIDTH * 4);
  }

  gst_buffer_unmap (buf, &map);

  return TRUE;
}

static GstBuffer *
create_stride_test_rgba_buffer (void)
{
  GstBuffer *buf;
  gsize offsets[GST_VIDEO_MAX_PLANES] = { 0, };
  gint strides[GST_VIDEO_MAX_PLANES] = { 0, };

  buf =
      gst_buffer_new_allocate (NULL,
      STRIDE_TEST_INPUT_STRIDE * STRIDE_TEST_HEIGHT, NULL);
  if (!buf)
    return NULL;

  if (!fill_stride_test_rgba_buffer (buf)) {
    gst_buffer_unref (buf);
    return NULL;
  }

  strides[0] = STRIDE_TEST_INPUT_STRIDE;
  if (!gst_buffer_add_video_meta_full (buf, GST_VIDEO_FRAME_FLAG_NONE,
          GST_VIDEO_FORMAT_RGBA, STRIDE_TEST_WIDTH, STRIDE_TEST_HEIGHT, 1,
          offsets, strides)) {
    gst_buffer_unref (buf);
    return NULL;
  }

  return buf;
}

static gboolean
run_test (const gchar * launchline, const gchar * format, guint width,
    guint height, const gchar * sink_caps_str)
{
  GstHarness *h_src, *h_el = NULL;
  GstBuffer *inbuf, *outbuf;
  GstCaps *src_caps, *caps = NULL;
  GstVideoInfo src_info;
  gboolean ret = FALSE;

  src_caps =
      gst_caps_new_simple ("video/x-raw", "format", G_TYPE_STRING, format,
      "width", G_TYPE_INT, width, "height", G_TYPE_INT, height, NULL);

  if (!gst_video_info_from_caps (&src_info, src_caps))
    return FALSE;

  h_src = gst_harness_new_parse ("videotestsrc num-buffers=1 pattern=blue");
  gst_harness_set_sink_caps (h_src, src_caps);

  gst_harness_play (h_src);
  while (TRUE) {
    GstEvent *event = gst_harness_pull_event (h_src);
    if (!event)
      break;
    if (GST_EVENT_TYPE (event) == GST_EVENT_CAPS)
      gst_event_parse_caps (event, &caps);
    if (caps)
      caps = gst_caps_ref (caps);
    gst_event_unref (event);
    if (caps)
      break;
  }

  if (!caps)
    goto bail;

  inbuf = gst_harness_pull (h_src);
  if (!inbuf)
    goto bail;

  h_el = gst_harness_new_parse (launchline);

  gst_harness_set_src_caps (h_el, caps);
  gst_harness_set_sink_caps_str (h_el, sink_caps_str);

  outbuf = gst_harness_push_and_pull (h_el, inbuf);
  if (!outbuf)
    goto bail;

  GST_INFO ("Testing format: %s [%dx%d]", format, width, height);

  ret = cmp_buffers (inbuf, outbuf, &src_info);

  gst_buffer_unref (outbuf);

bail:
  if (h_el)
    gst_harness_teardown (h_el);
  gst_harness_teardown (h_src);

  return ret;
}

GST_START_TEST (test_vulkan_upload_buffer)
{
  for (int i = 0; i < G_N_ELEMENTS (formats); i++) {
    for (int j = 0; j < G_N_ELEMENTS (resolutions); j++) {
      fail_unless (run_test ("vulkanupload", formats[i], resolutions[j].width,
              resolutions[j].height, "video/x-raw(memory:VulkanBuffer)"));
    }
  }
}

GST_END_TEST;

GST_START_TEST (test_vulkan_upload_image)
{

  for (int i = 0; i < G_N_ELEMENTS (formats); i++) {
    for (int j = 0; j < G_N_ELEMENTS (resolutions); j++) {
      fail_unless (run_test
          ("vulkanupload ! video/x-raw(memory:VulkanImage) ! vulkandownload",
              formats[i], resolutions[j].width, resolutions[j].height,
              "video/x-raw"));
    }
  }
}

GST_END_TEST;

GST_START_TEST (test_vulkan_upload_image_padded_stride)
{
  GstHarness *h;
  GstCaps *caps;
  GstVideoInfo info;
  GstBuffer *inbuf, *outbuf;

  h = gst_harness_new_parse ("vulkanupload ! vulkandownload");
  fail_unless (h != NULL);

  caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "RGBA",
      "width", G_TYPE_INT, STRIDE_TEST_WIDTH,
      "height", G_TYPE_INT, STRIDE_TEST_HEIGHT, NULL);
  fail_unless (caps != NULL);
  fail_unless (gst_video_info_from_caps (&info, caps));

  gst_harness_set_caps (h, caps, gst_caps_ref (caps));

  inbuf = create_stride_test_rgba_buffer ();
  fail_unless (inbuf != NULL);

  outbuf = gst_harness_push_and_pull (h, gst_buffer_ref (inbuf));
  fail_unless (outbuf != NULL);

  fail_unless (cmp_buffers (inbuf, outbuf, &info));

  gst_buffer_unref (inbuf);
  gst_buffer_unref (outbuf);
  gst_harness_teardown (h);
}

GST_END_TEST;

static Suite *
vkupload_suite (void)
{
  Suite *s = suite_create ("vkupload");
  TCase *tc_basic = tcase_create ("general");
  GstVulkanInstance *instance;
  gboolean have_instance;

  suite_add_tcase (s, tc_basic);

  instance = gst_vulkan_instance_new ();
  have_instance = gst_vulkan_instance_open (instance, NULL);
  gst_object_unref (instance);
  if (have_instance) {
    tcase_add_test (tc_basic, test_vulkan_upload_buffer);
    tcase_add_test (tc_basic, test_vulkan_upload_image);
    tcase_add_test (tc_basic, test_vulkan_upload_image_padded_stride);
  }

  return s;
}

#ifdef __APPLE__
GST_CHECK_MAIN_NOFORK (vkupload);
#else
GST_CHECK_MAIN (vkupload);
#endif
