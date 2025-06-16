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
#include <gst/vulkan/vulkan.h>

#define SOURCE "videotestsrc num-buffers=1 pattern=blue ! "
#define CAPS "format=NV12, width=320, height=240"

static void
check_output_buffer (GstBuffer * buf)
{
  GstMapInfo mapinfo;
  guint i;

  fail_unless (gst_buffer_map (buf, &mapinfo, GST_MAP_READ));

  /* Check for a 320x240 blue square in NV12 format */
  /* Y */
  for (i = 0; i < 0x12c00; i++)
    fail_unless (mapinfo.data[i] == 0x29);
  /* UV */
  for (i = 0x12c00; i < 0x1c1f0; i++)
    fail_unless (mapinfo.data[i] == 0xf0 && mapinfo.data[++i] == 0x6e);
  gst_buffer_unmap (buf, &mapinfo);
}

GST_START_TEST (test_vulkan_upload_buffer)
{
  GstHarness *h;
  GstBuffer *buf;

  h = gst_harness_new_parse (SOURCE "vulkanupload");
  gst_harness_set_sink_caps_str (h, "video/x-raw(memory:VulkanBuffer), " CAPS);
  gst_harness_play (h);

  buf = gst_harness_pull (h);
  ck_assert (buf);
  check_output_buffer (buf);

  gst_buffer_unref (buf);
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_vulkan_upload_image)
{
  GstHarness *h;
  GstBuffer *buf;

  h = gst_harness_new_parse (SOURCE "vulkanupload ! vulkandownload");
  gst_harness_set_sink_caps_str (h, "video/x-raw, " CAPS);
  gst_harness_play (h);

  buf = gst_harness_pull (h);
  ck_assert (buf);
  check_output_buffer (buf);

  gst_buffer_unref (buf);
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

  /* FIXME: CI doesn't have a software vulkan renderer (and none exists currently) */
  instance = gst_vulkan_instance_new ();
  have_instance = gst_vulkan_instance_open (instance, NULL);
  gst_object_unref (instance);
  if (have_instance) {
    tcase_add_test (tc_basic, test_vulkan_upload_buffer);
    tcase_add_test (tc_basic, test_vulkan_upload_image);
  }

  return s;
}

GST_CHECK_MAIN (vkupload);
