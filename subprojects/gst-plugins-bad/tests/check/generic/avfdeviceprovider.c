/* GStreamer AVF device provider tests
 *
 * Copyright (C) 2026 Dominique Leroux
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

#include <gst/check/gstcheck.h>
#include <gst/gst.h>
#include <gst/gstdevice.h>
#include <gst/gstdevicemonitor.h>
#include <gst/gstdeviceproviderfactory.h>

#include <TargetConditionals.h>

#if TARGET_OS_OSX
#include <gst/gstmacos.h>
#endif

typedef struct
{
  gchar *unique_id;
  gchar *display_name;
  gchar *device_class;
} AvfDeviceInfo;

typedef struct
{
  guint added;
  guint removed;
  guint changed;
} AvfMonitorMessageCounts;

static void
free_avf_device_info (gpointer data)
{
  AvfDeviceInfo *info = data;

  if (info == NULL)
    return;

  g_free (info->unique_id);
  g_free (info->display_name);
  g_free (info->device_class);
  g_free (info);
}

static gboolean
is_avf_device (GstDevice * device)
{
  GstStructure *props;
  gboolean result = FALSE;

  if (device == NULL)
    return FALSE;

  props = gst_device_get_properties (device);
  if (props != NULL) {
    const gchar *device_api = gst_structure_get_string (props, "device.api");

    result = g_strcmp0 (device_api, "avf") == 0;
  }
  g_clear_pointer (&props, gst_structure_free);

  return result;
}

static void
count_initial_avf_monitor_message (AvfMonitorMessageCounts * counts,
    GstMessage * message)
{
  GstDevice *device = NULL;

  if (counts == NULL || message == NULL)
    return;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_DEVICE_ADDED:
      gst_message_parse_device_added (message, &device);
      if (is_avf_device (device))
        counts->added++;
      break;
    case GST_MESSAGE_DEVICE_REMOVED:
      gst_message_parse_device_removed (message, &device);
      if (is_avf_device (device))
        counts->removed++;
      break;
    case GST_MESSAGE_DEVICE_CHANGED:
      gst_message_parse_device_changed (message, &device, NULL);
      if (is_avf_device (device))
        counts->changed++;
      break;
    default:
      break;
  }

  g_clear_object (&device);
}

static void
start_monitor_and_wait_until_started (GstDeviceMonitor * monitor,
    AvfMonitorMessageCounts * counts)
{
  GstBus *bus;
  GstMessage *message;

  fail_unless (monitor != NULL, "GstDeviceMonitor is NULL");

  bus = gst_device_monitor_get_bus (monitor);
  fail_unless (bus != NULL, "Could not get GstDeviceMonitor bus");
  fail_unless (gst_device_monitor_start (monitor),
      "Could not start GstDeviceMonitor");

  while (TRUE) {
    message = gst_bus_timed_pop_filtered (bus, 10 * GST_SECOND,
        GST_MESSAGE_DEVICE_ADDED | GST_MESSAGE_DEVICE_REMOVED |
        GST_MESSAGE_DEVICE_CHANGED | GST_MESSAGE_DEVICE_MONITOR_STARTED |
        GST_MESSAGE_ERROR);
    fail_unless (message != NULL,
        "Timed out while waiting for GstDeviceMonitor startup");

    if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_DEVICE_MONITOR_STARTED) {
      gboolean success = FALSE;

      gst_message_parse_device_monitor_started (message, &success);
      gst_message_unref (message);
      fail_unless (success, "GstDeviceMonitor reported startup failure");
      break;
    }

    if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ERROR) {
      GError *error = NULL;
      gchar *debug = NULL;

      gst_message_parse_error (message, &error, &debug);
      gst_message_unref (message);
      ck_abort_msg ("GstDeviceMonitor failed to start: %s (%s)",
          error ? error->message : "unknown error", GST_STR_NULL (debug));
      g_clear_error (&error);
      g_clear_pointer (&debug, g_free);
    }

    count_initial_avf_monitor_message (counts, message);
    gst_message_unref (message);
  }

  gst_object_unref (bus);
}

static GPtrArray *
collect_avf_devices (GstDeviceMonitor * monitor,
    const gchar * device_class_prefix)
{
  GList *devices, *iter;
  GPtrArray *result;

  if (monitor == NULL)
    return NULL;

  result = g_ptr_array_new_with_free_func (free_avf_device_info);
  devices = gst_device_monitor_get_devices (monitor);
  for (iter = devices; iter != NULL; iter = g_list_next (iter)) {
    GstDevice *device = GST_DEVICE (iter->data);
    GstStructure *props;
    const gchar *unique_id;
    const gchar *device_class;
    AvfDeviceInfo *info;

    if (!is_avf_device (device))
      continue;

    device_class = gst_device_get_device_class (device);
    if (device_class == NULL
        || !g_str_has_prefix (device_class, device_class_prefix))
      continue;

    props = gst_device_get_properties (device);
    fail_unless (props != NULL, "AVF device is missing properties");

    unique_id = gst_structure_get_string (props, "avf.unique_id");
    if (unique_id == NULL || *unique_id == '\0') {
      g_clear_pointer (&props, gst_structure_free);
      continue;
    }

    info = g_new0 (AvfDeviceInfo, 1);
    info->unique_id = g_strdup (unique_id);
    info->display_name = gst_device_get_display_name (device);
    info->device_class = g_strdup (device_class);
    g_ptr_array_add (result, info);

    g_clear_pointer (&props, gst_structure_free);
  }

  g_list_free_full (devices, gst_object_unref);

  return result;
}

static GstDeviceMonitor *
create_started_avf_monitor_for_all_devices (AvfMonitorMessageCounts * counts)
{
  GstDeviceMonitor *monitor = gst_device_monitor_new ();

  fail_unless (monitor != NULL, "Could not create GstDeviceMonitor");
  gst_device_monitor_add_filter (monitor, "Video/Source", NULL);
  gst_device_monitor_add_filter (monitor, "Source/Monitor", NULL);
  start_monitor_and_wait_until_started (monitor, counts);

  return monitor;
}

static gboolean
avf_device_info_equals (const AvfDeviceInfo * a, const AvfDeviceInfo * b)
{
  return g_strcmp0 (a->unique_id, b->unique_id) == 0
      && g_strcmp0 (a->display_name, b->display_name) == 0
      && g_strcmp0 (a->device_class, b->device_class) == 0;
}

static void
fail_unless_same_avf_device_content (GPtrArray * expected, GPtrArray * current,
    const gchar * context)
{
  fail_unless (expected != NULL);
  fail_unless (current != NULL);
  fail_unless_equals_int (current->len, expected->len);

  for (guint i = 0; i < expected->len; i++) {
    const AvfDeviceInfo *expected_info = g_ptr_array_index (expected, i);
    gboolean found = FALSE;

    for (guint j = 0; j < current->len; j++) {
      const AvfDeviceInfo *current_info = g_ptr_array_index (current, j);

      if (avf_device_info_equals (expected_info, current_info)) {
        found = TRUE;
        break;
      }
    }

    fail_unless (found, "%s: missing AVF device %s [%s] class %s",
        GST_STR_NULL (context), GST_STR_NULL (expected_info->display_name),
        GST_STR_NULL (expected_info->unique_id),
        GST_STR_NULL (expected_info->device_class));
  }
}

GST_START_TEST (test_avf_device_provider_can_monitor)
{
  GstDeviceProvider *provider =
      gst_device_provider_factory_get_by_name ("avfdeviceprovider");

  fail_unless (provider != NULL, "Could not create avfdeviceprovider");
  fail_unless (gst_device_provider_can_monitor (provider),
      "avfdeviceprovider does not report live-monitoring capability");

  gst_object_unref (provider);
}

GST_END_TEST;

GST_START_TEST (test_avf_monitor_start_stop_smoke)
{
  GstDeviceMonitor *monitor = gst_device_monitor_new ();

  fail_unless (monitor != NULL, "Could not create GstDeviceMonitor");
  gst_device_monitor_add_filter (monitor, "Video/Source", NULL);
  gst_device_monitor_add_filter (monitor, "Source/Monitor", NULL);

  start_monitor_and_wait_until_started (monitor, NULL);
  gst_device_monitor_stop (monitor);
  start_monitor_and_wait_until_started (monitor, NULL);
  gst_device_monitor_stop (monitor);
  gst_object_unref (monitor);
}

GST_END_TEST;

GST_START_TEST (test_avf_initial_device_added_messages)
{
  AvfMonitorMessageCounts counts = { 0, };
  GstDeviceMonitor *monitor =
      create_started_avf_monitor_for_all_devices (&counts);
  GPtrArray *devices = collect_avf_devices (monitor, "");

  fail_unless_equals_int (counts.removed, 0);
  fail_unless_equals_int (counts.changed, 0);
  fail_unless_equals_int (counts.added, devices->len);

  g_ptr_array_unref (devices);
  gst_device_monitor_stop (monitor);
  gst_object_unref (monitor);
}

GST_END_TEST;

GST_START_TEST (test_avf_initial_snapshot_consistency)
{
  GstDeviceMonitor *first_monitor =
      create_started_avf_monitor_for_all_devices (NULL);
  GPtrArray *first_devices = collect_avf_devices (first_monitor, "");
  GstDeviceMonitor *second_monitor;
  GPtrArray *second_devices;

  gst_device_monitor_stop (first_monitor);
  gst_object_unref (first_monitor);

  second_monitor = create_started_avf_monitor_for_all_devices (NULL);
  second_devices = collect_avf_devices (second_monitor, "");

  fail_unless_same_avf_device_content (first_devices, second_devices,
      "Initial AVF provider snapshots do not match");

  g_ptr_array_unref (first_devices);
  g_ptr_array_unref (second_devices);
  gst_device_monitor_stop (second_monitor);
  gst_object_unref (second_monitor);
}

GST_END_TEST;

static Suite *
avfdeviceprovider_suite (void)
{
  Suite *s = suite_create ("avfdeviceprovider");
  TCase *tc_basic = tcase_create ("provider");

  suite_add_tcase (s, tc_basic);
  tcase_add_test (tc_basic, test_avf_device_provider_can_monitor);
  tcase_add_test (tc_basic, test_avf_monitor_start_stop_smoke);
  tcase_add_test (tc_basic, test_avf_initial_device_added_messages);
  tcase_add_test (tc_basic, test_avf_initial_snapshot_consistency);

  return s;
}

static int
run_tests ()
{
  Suite *s = avfdeviceprovider_suite ();
  return gst_check_run_suite_nofork (s, "avfdeviceprovider", __FILE__);
}

int
main (int argc, char **argv)
{
  gst_check_init (&argc, &argv);
#if TARGET_OS_OSX
  return gst_macos_main_simple ((GstMainFuncSimple) run_tests, NULL);
#else
  return run_tests (argc, argv, NULL);
#endif
}
