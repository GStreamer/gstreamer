/* GStreamer
 * Copyright (C) 2014 Collabora
 *   Author: Olivier Crete <olivier.crete@collabora.com>
 *
 * gstdevice.c: Unit test for GstDevice
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

typedef struct _GstTestDevice
{
  GstDevice parent;
} GstTestDevice;

typedef struct _GstTestDeviceClass
{
  GstDeviceClass parent_class;
} GstTestDeviceClass;

GType gst_test_device_get_type (void);

G_DEFINE_TYPE (GstTestDevice, gst_test_device, GST_TYPE_DEVICE);

static GstElement *
gst_test_device_create_element (GstDevice * device, const gchar * name)
{
  return gst_bin_new (name);
}

static gboolean
gst_test_device_reconfigure_element (GstDevice * device, GstElement * element)
{
  if (!strcmp (GST_ELEMENT_NAME (element), "reconfigurable"))
    return TRUE;
  else
    return FALSE;
}

static void
gst_test_device_class_init (GstTestDeviceClass * klass)
{
  GstDeviceClass *dclass = GST_DEVICE_CLASS (klass);

  dclass->create_element = gst_test_device_create_element;
  dclass->reconfigure_element = gst_test_device_reconfigure_element;
}

static void
gst_test_device_init (GstTestDevice * self)
{
}

#define DEVICE_CLASS "Test0/Test1/Test2/Test3/Test4/TestDev"
#define DISPLAY_NAME "Test device"

static GstDevice *
test_device_new (void)
{
  GstCaps *caps = gst_caps_new_empty_simple ("video/test");
  GstDevice *device = g_object_new (gst_test_device_get_type (), "caps", caps,
      "display-name", DISPLAY_NAME, "device-class", DEVICE_CLASS, NULL);

  gst_caps_unref (caps);

  return device;
}

GST_START_TEST (test_device)
{
  GstDevice *device = test_device_new ();
  GstCaps *caps;
  gchar *display_name;
  gchar *device_class;
  GstCaps *compare_caps = gst_caps_new_empty_simple ("video/test");
  GstElement *element;

  caps = gst_device_get_caps (device);
  display_name = gst_device_get_display_name (device);
  device_class = gst_device_get_device_class (device);

  ck_assert_str_eq (DISPLAY_NAME, display_name);
  ck_assert_str_eq (DEVICE_CLASS, device_class);
  gst_check_caps_equal (caps, compare_caps);

  g_free (display_name);
  g_free (device_class);
  gst_caps_unref (caps);

  ck_assert (gst_device_has_classes (device, "Test1"));
  ck_assert (gst_device_has_classes (device, "Test2/Test1"));

  element = gst_device_create_element (device, "reconfigurable");
  ck_assert (GST_IS_BIN (element));

  ck_assert (gst_device_reconfigure_element (device, element));

  gst_element_set_name (element, "no-no");

  ck_assert (!gst_device_reconfigure_element (device, element));

  gst_object_unref (element);

  gst_caps_unref (compare_caps);
  gst_object_unref (device);
}

GST_END_TEST;


typedef struct _GstTestDeviceProvider
{
  GstDeviceProvider parent;

} GstTestDeviceProvider;

typedef struct _GstTestDeviceProviderClass
{
  GstDeviceProviderClass parent_class;
} GstTestDeviceProviderClass;

GType gst_test_device_provider_get_type (void);

G_DEFINE_TYPE (GstTestDeviceProvider, gst_test_device_provider,
    GST_TYPE_DEVICE_PROVIDER);

int num_devices = 1;

static GList *
gst_test_device_provider_probe (GstDeviceProvider * provider)
{
  int i;
  GList *devs = NULL;

  for (i = 0; i < num_devices; i++)
    devs = g_list_prepend (devs, test_device_new ());

  return devs;
}

static void
gst_test_device_provider_class_init (GstTestDeviceProviderClass * klass)
{
  GstDeviceProviderClass *dpclass = GST_DEVICE_PROVIDER_CLASS (klass);

  dpclass->probe = gst_test_device_provider_probe;

  gst_device_provider_class_set_static_metadata (dpclass,
      "Test Device Provider", "Test0/Test1/Test2/Test3/TestProvider",
      "List but does NOT monitor test devices",
      "Olivier Crete <olivier.crete@collabora.com>");
}

static void
gst_test_device_provider_init (GstTestDeviceProvider * self)
{
}

GST_DEVICE_PROVIDER_REGISTER_DECLARE (testdeviceprovider);

GST_DEVICE_PROVIDER_REGISTER_DEFINE (testdeviceprovider, "testdeviceprovider",
    1, gst_test_device_provider_get_type ())
     static void register_test_device_provider (void)
{
  gst_device_provider_register (NULL, "testdeviceprovider", 1,
      gst_test_device_provider_get_type ());
}

GST_START_TEST (test_device_provider_factory)
{
  GstDeviceProvider *dp, *dp2;
  GList *factories;
  GstDeviceProviderFactory *f;

  GST_DEVICE_PROVIDER_REGISTER (testdeviceprovider, NULL);

  factories = gst_device_provider_factory_list_get_device_providers (1);

  ck_assert_ptr_ne (factories, NULL);

  f = gst_device_provider_factory_find ("testdeviceprovider");
  ck_assert_ptr_ne (f, NULL);

  gst_plugin_feature_list_free (factories);

  ck_assert (gst_device_provider_factory_has_classes (f, "Test2"));
  ck_assert (gst_device_provider_factory_has_classes (f, "Test2/Test0"));
  ck_assert (!gst_device_provider_factory_has_classes (f, "Test2/TestN/Test0"));
  ck_assert (!gst_device_provider_factory_has_classes (f, "TestN"));
  ck_assert (!gst_device_provider_factory_has_classes (f, "Test"));

  dp = gst_device_provider_factory_get (f);

  gst_object_unref (f);

  dp2 = gst_device_provider_factory_get_by_name ("testdeviceprovider");

  ck_assert_ptr_eq (dp, dp2);

  gst_object_unref (dp);
  gst_object_unref (dp2);

  dp2 = gst_device_provider_factory_get_by_name ("testdeviceprovider");
  ck_assert_ptr_eq (dp, dp2);
  gst_object_unref (dp2);
}

GST_END_TEST;

GST_START_TEST (test_device_provider)
{
  GstDeviceProvider *dp;
  GList *devs;
  GstBus *bus;

  register_test_device_provider ();

  dp = gst_device_provider_factory_get_by_name ("testdeviceprovider");
  num_devices = 0;
  ck_assert_ptr_ne (dp, NULL);
  ck_assert_ptr_eq (gst_device_provider_get_devices (dp), NULL);

  num_devices = 1;

  devs = gst_device_provider_get_devices (dp);
  ck_assert (g_list_length (devs) == 1);
  ck_assert (GST_IS_DEVICE (devs->data));
  g_list_free_full (devs, (GDestroyNotify) gst_object_unref);

  fail_if (gst_device_provider_can_monitor (dp));
  fail_if (gst_device_provider_is_started (dp));
  ck_assert (gst_device_provider_start (dp));

  bus = gst_device_provider_get_bus (dp);
  ck_assert (GST_IS_BUS (bus));
  gst_object_unref (bus);

  ck_assert (gst_device_provider_is_started (dp));
  gst_device_provider_stop (dp);

  gst_object_unref (dp);
}

GST_END_TEST;

typedef struct _GstTestDeviceProviderMonitor
{
  GstDeviceProvider parent;

} GstTestDeviceProviderMonitor;

typedef struct _GstTestDeviceProviderMonitorClass
{
  GstDeviceProviderClass parent_class;
} GstTestDeviceProviderMonitorClass;

GType gst_test_device_provider_monitor_get_type (void);

G_DEFINE_TYPE (GstTestDeviceProviderMonitor, gst_test_device_provider_monitor,
    GST_TYPE_DEVICE_PROVIDER);

static gboolean
gst_test_device_provider_monitor_start (GstDeviceProvider * monitor)
{
  GList *devices = gst_test_device_provider_probe (monitor);

  for (GList * iter = devices; iter; iter = iter->next) {
    gst_device_provider_device_add (monitor, GST_DEVICE (iter->data));
  }

  /* Device references were floating, so were transferred in
   * gst_device_provider_device_add() */
  g_list_free (devices);
  return TRUE;
}

static void
gst_test_device_provider_monitor_class_init (GstTestDeviceProviderMonitorClass *
    klass)
{
  GstDeviceProviderClass *dpclass = GST_DEVICE_PROVIDER_CLASS (klass);

  dpclass->probe = gst_test_device_provider_probe;
  dpclass->start = gst_test_device_provider_monitor_start;

  gst_device_provider_class_set_static_metadata (dpclass,
      "Test Device Provider Monitor",
      "Test0/Test1/Test2/Test4/TestProviderMonitor",
      "List and monitors Test devices",
      "Olivier Crete <olivier.crete@collabora.com>");
}

static void
gst_test_device_provider_monitor_init (GstTestDeviceProviderMonitor * self)
{
}

static void
register_test_device_provider_monitor (void)
{
  gst_device_provider_register (NULL, "testdeviceprovidermonitor", 2,
      gst_test_device_provider_monitor_get_type ());
}


GST_START_TEST (test_device_provider_monitor)
{
  GstDeviceProvider *dp;
  GList *devs;
  GstBus *bus;
  GstDevice *mydev;
  GstDevice *dev;
  GstMessage *msg;

  register_test_device_provider_monitor ();

  dp = gst_device_provider_factory_get_by_name ("testdeviceprovidermonitor");

  bus = gst_device_provider_get_bus (dp);

  msg = gst_bus_pop (bus);
  ck_assert_ptr_eq (msg, NULL);

  ck_assert (gst_device_provider_can_monitor (dp));
  ck_assert (gst_device_provider_start (dp));

  devs = gst_device_provider_get_devices (dp);
  ck_assert_int_eq (g_list_length (devs), 1);
  g_list_free_full (devs, (GDestroyNotify) gst_object_unref);

  mydev = test_device_new ();
  ck_assert (g_object_is_floating (mydev));
  ASSERT_OBJECT_REFCOUNT (mydev, "dev", 1);

  gst_device_provider_device_add (dp, mydev);
  ck_assert (!g_object_is_floating (mydev));
  ASSERT_OBJECT_REFCOUNT (mydev, "dev", 2);

  devs = gst_device_provider_get_devices (dp);
  ASSERT_OBJECT_REFCOUNT (mydev, "dev", 3);

  fail_unless_equals_int (g_list_length (devs), 2);
  ck_assert_ptr_eq (devs->next->data, mydev);
  g_list_free_full (devs, (GDestroyNotify) gst_object_unref);

  ASSERT_OBJECT_REFCOUNT (mydev, "dev", 2);

  msg = gst_bus_pop (bus);
  ck_assert_ptr_ne (msg, NULL);
  ck_assert (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_DEVICE_ADDED);
  gst_message_unref (msg);

  msg = gst_bus_pop (bus);
  ck_assert_ptr_ne (msg, NULL);
  ck_assert (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_DEVICE_ADDED);

  gst_message_parse_device_added (msg, &dev);
  ck_assert_ptr_eq (dev, mydev);
  gst_object_unref (dev);
  gst_message_unref (msg);

  ASSERT_OBJECT_REFCOUNT (mydev, "dev", 1);

  msg = gst_bus_pop (bus);
  ck_assert_ptr_eq (msg, NULL);

  gst_device_provider_device_remove (dp, mydev);
  devs = gst_device_provider_get_devices (dp);
  ck_assert_int_eq (g_list_length (devs), 1);
  g_list_free_full (devs, (GDestroyNotify) gst_object_unref);

  msg = gst_bus_pop (bus);
  ck_assert_ptr_ne (msg, NULL);

  ck_assert (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_DEVICE_REMOVED);

  gst_message_parse_device_removed (msg, &dev);
  ck_assert_ptr_eq (dev, mydev);
  ASSERT_OBJECT_REFCOUNT (mydev, "dev", 2);
  gst_object_unref (dev);
  gst_message_unref (msg);

  msg = gst_bus_pop (bus);
  ck_assert_ptr_eq (msg, NULL);

  gst_device_provider_stop (dp);
  gst_object_unref (bus);
  ASSERT_OBJECT_REFCOUNT (dp, "monitor", 2);
  gst_object_unref (dp);

  /* Is singleton, so system keeps a ref */
  ASSERT_OBJECT_REFCOUNT (dp, "monitor", 1);
}

GST_END_TEST;


GST_START_TEST (test_device_monitor)
{
  GstDeviceProvider *dp, *dp2;
  GstDeviceMonitor *mon;
  GList *devs;
  guint id, id2;
  GstDevice *mydev;
  GstMessage *msg;
  GstBus *bus;
  GstDevice *dev;

  register_test_device_provider ();
  register_test_device_provider_monitor ();

  dp = gst_device_provider_factory_get_by_name ("testdeviceprovider");
  dp2 = gst_device_provider_factory_get_by_name ("testdeviceprovidermonitor");

  mon = gst_device_monitor_new ();

  devs = gst_device_monitor_get_devices (mon);
  ck_assert_ptr_eq (devs, NULL);

  id = gst_device_monitor_add_filter (mon, "TestProvider", NULL);
  ck_assert_int_gt (id, 0);

  devs = gst_device_monitor_get_devices (mon);
  ck_assert_ptr_eq (devs, NULL);

  ck_assert (gst_device_monitor_add_filter (mon, "TestDevice", NULL) == 0);
  ASSERT_CRITICAL (gst_device_monitor_remove_filter (mon, 0));

  ck_assert (gst_device_monitor_remove_filter (mon, id));

  id = gst_device_monitor_add_filter (mon, "Test3", NULL);
  ck_assert_int_gt (id, 0);
  devs = gst_device_monitor_get_devices (mon);
  ck_assert_int_eq (g_list_length (devs), 1);
  ck_assert (GST_IS_DEVICE (devs->data));
  g_list_free_full (devs, (GDestroyNotify) gst_object_unref);

  id2 = gst_device_monitor_add_filter (mon, "Test1", NULL);
  ck_assert (id2 > 0);
  devs = gst_device_monitor_get_devices (mon);
  ck_assert_int_eq (g_list_length (devs), 2);
  ck_assert (GST_IS_DEVICE (devs->data));
  ck_assert (GST_IS_DEVICE (devs->next->data));
  g_list_free_full (devs, (GDestroyNotify) gst_object_unref);

  ck_assert (gst_device_monitor_remove_filter (mon, id));

  devs = gst_device_monitor_get_devices (mon);
  ck_assert_int_eq (g_list_length (devs), 2);
  ck_assert (GST_IS_DEVICE (devs->data));
  ck_assert (GST_IS_DEVICE (devs->next->data));
  g_list_free_full (devs, (GDestroyNotify) gst_object_unref);

  ck_assert (gst_device_monitor_start (mon));

  devs = gst_device_monitor_get_devices (mon);
  ck_assert_int_eq (g_list_length (devs), 2);
  ck_assert (GST_IS_DEVICE (devs->data));
  g_list_free_full (devs, (GDestroyNotify) gst_object_unref);

  gst_device_monitor_stop (mon);

  ck_assert (gst_device_monitor_remove_filter (mon, id2));

  id = gst_device_monitor_add_filter (mon, "Test4", NULL);
  ck_assert (id > 0);

  devs = gst_device_monitor_get_devices (mon);
  ck_assert_int_eq (g_list_length (devs), 1);
  ck_assert (GST_IS_DEVICE (devs->data));
  g_list_free_full (devs, (GDestroyNotify) gst_object_unref);

  bus = gst_device_monitor_get_bus (mon);

  ck_assert (gst_device_monitor_start (mon));

  msg = gst_bus_timed_pop (bus, GST_CLOCK_TIME_NONE);
  ck_assert_ptr_ne (msg, NULL);
  ck_assert_int_eq (GST_MESSAGE_TYPE (msg), GST_MESSAGE_DEVICE_ADDED);
  gst_message_unref (msg);

  msg = gst_bus_timed_pop (bus, GST_CLOCK_TIME_NONE);
  ck_assert_ptr_ne (msg, NULL);
  ck_assert_int_eq (GST_MESSAGE_TYPE (msg), GST_MESSAGE_DEVICE_MONITOR_STARTED);
  gst_message_unref (msg);

  mydev = test_device_new ();
  gst_device_provider_device_add (dp2, mydev);

  msg = gst_bus_pop (bus);
  ck_assert_ptr_ne (msg, NULL);

  ck_assert_int_eq (GST_MESSAGE_TYPE (msg), GST_MESSAGE_DEVICE_ADDED);

  gst_message_parse_device_added (msg, &dev);
  ck_assert_ptr_eq (dev, mydev);
  gst_object_unref (dev);
  gst_message_unref (msg);

  msg = gst_bus_pop (bus);
  ck_assert_ptr_eq (msg, NULL);

  gst_device_provider_device_remove (dp2, mydev);
  devs = gst_device_monitor_get_devices (mon);
  ck_assert_ptr_eq (g_list_find (devs, mydev), NULL);
  g_list_free_full (devs, (GDestroyNotify) gst_object_unref);

  msg = gst_bus_pop (bus);
  ck_assert_ptr_ne (msg, NULL);

  ck_assert (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_DEVICE_REMOVED);

  gst_message_parse_device_removed (msg, &dev);
  ck_assert_ptr_eq (dev, mydev);
  gst_object_unref (dev);
  gst_message_unref (msg);

  msg = gst_bus_pop (bus);
  ck_assert_ptr_eq (msg, NULL);


  gst_device_monitor_stop (mon);

  gst_object_unref (bus);
  gst_object_unref (mon);

  gst_object_unref (dp);
  gst_object_unref (dp2);

  /* should work fine without any filters */
  mon = gst_device_monitor_new ();
  ck_assert (gst_device_monitor_start (mon));
  gst_device_monitor_stop (mon);
  gst_object_unref (mon);
}

GST_END_TEST;


static Suite *
gst_device_suite (void)
{
  Suite *s = suite_create ("GstDevice");
  TCase *tc_chain = tcase_create ("device tests");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_device);
  tcase_add_test (tc_chain, test_device_provider_factory);
  tcase_add_test (tc_chain, test_device_provider);
  tcase_add_test (tc_chain, test_device_provider_monitor);
  tcase_add_test (tc_chain, test_device_monitor);

  return s;
}

GST_CHECK_MAIN (gst_device);
