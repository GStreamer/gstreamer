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

static GstVulkanDisplay *display;
static GstVulkanInstance *instance;

static void
setup (void)
{
  instance = gst_vulkan_instance_new ();
  fail_unless (gst_vulkan_instance_open (instance, NULL));
  display = gst_vulkan_display_new (instance);
}

static void
teardown (void)
{
  gst_object_unref (display);
  gst_object_unref (instance);
}

GST_START_TEST (test_window_new)
{
  GstVulkanWindow *window;
  GstVulkanDisplay *win_display;

  window = gst_vulkan_window_new (display);
  g_object_get (window, "display", &win_display, NULL);
  fail_unless (win_display == display);
  gst_object_unref (win_display);
  gst_object_unref (window);
}

GST_END_TEST;

static Suite *
vkwindow_suite (void)
{
  Suite *s = suite_create ("vkwindow");
  TCase *tc_basic = tcase_create ("general");
  gboolean have_instance;

  suite_add_tcase (s, tc_basic);
  tcase_add_checked_fixture (tc_basic, setup, teardown);

  instance = gst_vulkan_instance_new ();
  have_instance = gst_vulkan_instance_open (instance, NULL);
  gst_object_unref (instance);
  if (have_instance) {
    tcase_add_test (tc_basic, test_window_new);
  }

  return s;
}


GST_CHECK_MAIN (vkwindow);
