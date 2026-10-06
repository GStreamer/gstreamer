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

static void
setup (void)
{
  instance = gst_vulkan_instance_new ();
  fail_unless (gst_vulkan_instance_open (instance, NULL));
}

static void
teardown (void)
{
  gst_object_unref (instance);
}

GST_START_TEST (test_device_new)
{
  GstVulkanDevice *device;
  GstVulkanInstance *dev_instance;

  device = gst_vulkan_device_new_with_index (instance, 0);
  g_object_get (device, "instance", &dev_instance, NULL);
  fail_unless (dev_instance == instance);
  gst_object_unref (dev_instance);
  gst_object_unref (device);
}

GST_END_TEST;

GST_START_TEST (test_physical_device_version)
{
  GstVulkanPhysicalDevice *phys = gst_vulkan_physical_device_new (instance, 0);
  guint major, minor;

  gst_vulkan_physical_device_get_api_version (phys, &major, &minor, NULL);

  if (major > 0)
    fail_unless (gst_vulkan_physical_device_check_api_version (phys, major,
            minor, 0));

  if (minor > 0)
    fail_unless (gst_vulkan_physical_device_check_api_version (phys, major,
            minor - 1, 0));

  fail_unless (!gst_vulkan_physical_device_check_api_version (phys, major,
          minor + 1, 0));
  gst_object_unref (phys);
}

GST_END_TEST;

struct queue_count
{
  guint32 family;
  guint32 n_queues;
};

static gboolean
_count_family_queues (GstVulkanDevice * device, GstVulkanQueue * queue,
    gpointer user_data)
{
  struct queue_count *count = user_data;

  if (queue->family == count->family)
    count->n_queues = MAX (count->n_queues, queue->index + 1);

  return TRUE;
}

GST_START_TEST (test_get_queue_out_of_range)
{
  GstVulkanDevice *device;
  GstVulkanQueue *queue;
  GArray *families;
  struct queue_count count = { 0, 0 };

  device = gst_vulkan_device_new_with_index (instance, 0);
  fail_unless (gst_vulkan_device_open (device, NULL));

  families = gst_vulkan_device_queue_family_indices (device);
  fail_unless (families->len > 0);
  count.family = g_array_index (families, uint32_t, 0);
  g_array_unref (families);

  gst_vulkan_device_foreach_queue (device, _count_family_queues, &count);
  fail_unless (count.n_queues > 0);

  queue = gst_vulkan_device_get_queue (device, count.family,
      count.n_queues - 1);
  fail_unless (queue != NULL);
  gst_object_unref (queue);

  ASSERT_CRITICAL (queue = gst_vulkan_device_get_queue (device, count.family,
          count.n_queues));
  fail_unless (queue == NULL);

  gst_object_unref (device);
}

GST_END_TEST;

static Suite *
vkdevice_suite (void)
{
  Suite *s = suite_create ("vkdevice");
  TCase *tc_basic = tcase_create ("general");
  gboolean have_instance;

  suite_add_tcase (s, tc_basic);
  tcase_add_checked_fixture (tc_basic, setup, teardown);

  instance = gst_vulkan_instance_new ();
  have_instance = gst_vulkan_instance_open (instance, NULL);
  gst_object_unref (instance);
  if (have_instance) {
    tcase_add_test (tc_basic, test_device_new);
    tcase_add_test (tc_basic, test_physical_device_version);
    tcase_add_test (tc_basic, test_get_queue_out_of_range);
  }

  return s;
}


#ifdef __APPLE__
GST_CHECK_MAIN_NOFORK (vkdevice);
#else
GST_CHECK_MAIN (vkdevice);
#endif
