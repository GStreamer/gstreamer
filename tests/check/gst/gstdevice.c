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


G_DEFINE_TYPE (GstTestDevice, gst_test_device, GST_TYPE_DEVICE)

     static GstElement *gst_test_device_create_element (GstDevice * device,
    const gchar * name)
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

  fail_unless_equals_string (DISPLAY_NAME, display_name);
  fail_unless_equals_string (DEVICE_CLASS, device_class);
  gst_check_caps_equal (caps, compare_caps);

  g_free (display_name);
  g_free (device_class);
  gst_caps_unref (caps);

  fail_unless (gst_device_has_classes (device, "Test1"));
  fail_unless (gst_device_has_classes (device, "Test2/Test1"));

  element = gst_device_create_element (device, "reconfigurable");
  fail_unless (GST_IS_BIN (element));

  fail_unless (gst_device_reconfigure_element (device, element));

  gst_element_set_name (element, "no-no");

  fail_unless (!gst_device_reconfigure_element (device, element));

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
    GST_TYPE_DEVICE_PROVIDER)


     static GList *devices = NULL;

     static GList *gst_test_device_provider_probe (GstDeviceProvider * provider)
{
  GList *devs;

  devs = g_list_copy (devices);
  g_list_foreach (devs, (GFunc) gst_object_ref, NULL);

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

static void
register_test_device_provider (void)
{
  gst_device_provider_register (NULL, "testdeviceprovider", 1,
      gst_test_device_provider_get_type ());
}

GST_START_TEST (test_device_provider_factory)
{
  GstDeviceProvider *dp, *dp2;
  GList *factories;
  GstDeviceProviderFactory *f;

  register_test_device_provider ();

  factories = gst_device_provider_factory_list_get_device_providers (1);

  fail_unless (factories != NULL);

  f = gst_device_provider_factory_find ("testdeviceprovider");
  fail_unless (f != NULL);

  gst_plugin_feature_list_free (factories);

  fail_unless (gst_device_provider_factory_has_classes (f, "Test2"));
  fail_unless (gst_device_provider_factory_has_classes (f, "Test2/Test0"));
  fail_unless (!gst_device_provider_factory_has_classes (f,
          "Test2/TestN/Test0"));
  fail_unless (!gst_device_provider_factory_has_classes (f, "TestN"));
  fail_unless (!gst_device_provider_factory_has_classes (f, "Test"));

  dp = gst_device_provider_factory_get (f);

  gst_object_unref (f);

  dp2 = gst_device_provider_factory_get_by_name ("testdeviceprovider");

  fail_unless_equals_pointer (dp, dp2);

  gst_object_unref (dp);
  gst_object_unref (dp2);

  dp2 = gst_device_provider_factory_get_by_name ("testdeviceprovider");
  fail_unless_equals_pointer (dp, dp2);
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

  fail_unless (gst_device_provider_get_devices (dp) == NULL);

  devices = g_list_append (NULL, test_device_new ());

  devs = gst_device_provider_get_devices (dp);
  fail_unless (g_list_length (devs) == 1);
  fail_unless_equals_pointer (devs->data, devices->data);
  g_list_free_full (devs, (GDestroyNotify) gst_object_unref);

  fail_if (gst_device_provider_can_monitor (dp));
  fail_if (gst_device_provider_start (dp));

  bus = gst_device_provider_get_bus (dp);
  fail_unless (GST_IS_BUS (bus));
  gst_object_unref (bus);

  g_list_free_full (devices, (GDestroyNotify) gst_object_unref);
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
    GST_TYPE_DEVICE_PROVIDER)


     static gboolean
         gst_test_device_provider_monitor_start (GstDeviceProvider * monitor)
{
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

  devices = g_list_append (NULL, test_device_new ());

  dp = gst_device_provider_factory_get_by_name ("testdeviceprovidermonitor");

  bus = gst_device_provider_get_bus (dp);

  msg = gst_bus_pop (bus);
  fail_unless (msg == NULL);

  fail_unless (gst_device_provider_can_monitor (dp));
  fail_unless (gst_device_provider_start (dp));

  fail_unless (gst_device_provider_get_devices (dp) == NULL);

  devs = gst_device_provider_get_devices (dp);
  fail_unless (devs == NULL);

  mydev = test_device_new ();
  fail_unless (g_object_is_floating (mydev));
  ASSERT_OBJECT_REFCOUNT (mydev, "dev", 1);

  gst_device_provider_device_add (dp, mydev);
  fail_unless (!g_object_is_floating (mydev));
  ASSERT_OBJECT_REFCOUNT (mydev, "dev", 2);

  devs = gst_device_provider_get_devices (dp);
  ASSERT_OBJECT_REFCOUNT (mydev, "dev", 3);

  fail_unless_equals_int (g_list_length (devs), 1);
  fail_unless_equals_pointer (devs->data, mydev);
  g_list_free_full (devs, (GDestroyNotify) gst_object_unref);

  ASSERT_OBJECT_REFCOUNT (mydev, "dev", 2);

  msg = gst_bus_pop (bus);
  fail_unless (msg != NULL);

  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_DEVICE_ADDED);

  gst_message_parse_device_added (msg, &dev);
  fail_unless_equals_pointer (dev, mydev);
  gst_object_unref (dev);
  gst_message_unref (msg);

  ASSERT_OBJECT_REFCOUNT (mydev, "dev", 1);

  msg = gst_bus_pop (bus);
  fail_unless (msg == NULL);

  gst_device_provider_device_remove (dp, mydev);
  devs = gst_device_provider_get_devices (dp);
  fail_unless (devs == NULL);

  msg = gst_bus_pop (bus);
  fail_unless (msg != NULL);

  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_DEVICE_REMOVED);

  gst_message_parse_device_removed (msg, &dev);
  fail_unless_equals_pointer (dev, mydev);
  ASSERT_OBJECT_REFCOUNT (mydev, "dev", 2);
  gst_object_unref (dev);
  gst_message_unref (msg);

  msg = gst_bus_pop (bus);
  fail_unless (msg == NULL);

  gst_device_provider_stop (dp);
  gst_object_unref (bus);
  ASSERT_OBJECT_REFCOUNT (dp, "monitor", 2);
  gst_object_unref (dp);

  /* Is singleton, so system keeps a ref */
  ASSERT_OBJECT_REFCOUNT (dp, "monitor", 1);

  g_list_free_full (devices, (GDestroyNotify) gst_object_unref);
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

  devices = g_list_append (NULL, test_device_new ());

  devs = gst_device_monitor_get_devices (mon);
  fail_unless (devs == NULL);

  id = gst_device_monitor_add_filter (mon, "TestProvider", NULL);
  fail_unless (id > 0);

  devs = gst_device_monitor_get_devices (mon);
  fail_unless (devs == NULL);

  fail_unless (gst_device_monitor_add_filter (mon, "TestDevice", NULL) == 0);
  ASSERT_CRITICAL (gst_device_monitor_remove_filter (mon, 0));

  fail_unless (gst_device_monitor_remove_filter (mon, id));

  id = gst_device_monitor_add_filter (mon, "Test3", NULL);
  fail_unless (id > 0);
  devs = gst_device_monitor_get_devices (mon);
  fail_unless (g_list_length (devs) == 1);
  fail_unless_equals_pointer (devs->data, devices->data);
  g_list_free_full (devs, (GDestroyNotify) gst_object_unref);

  id2 = gst_device_monitor_add_filter (mon, "Test1", NULL);
  fail_unless (id2 > 0);
  devs = gst_device_monitor_get_devices (mon);
  fail_unless (g_list_length (devs) == 2);
  fail_unless_equals_pointer (devs->data, devices->data);
  fail_unless_equals_pointer (devs->next->data, devices->data);
  g_list_free_full (devs, (GDestroyNotify) gst_object_unref);

  fail_unless (gst_device_monitor_remove_filter (mon, id));

  devs = gst_device_monitor_get_devices (mon);
  fail_unless (g_list_length (devs) == 2);
  fail_unless_equals_pointer (devs->data, devices->data);
  fail_unless_equals_pointer (devs->next->data, devices->data);
  g_list_free_full (devs, (GDestroyNotify) gst_object_unref);


  fail_unless (gst_device_monitor_start (mon));

  devs = gst_device_monitor_get_devices (mon);
  fail_unless (g_list_length (devs) == 1);
  fail_unless_equals_pointer (devs->data, devices->data);
  g_list_free_full (devs, (GDestroyNotify) gst_object_unref);

  gst_device_monitor_stop (mon);

  fail_unless (gst_device_monitor_remove_filter (mon, id2));

  id = gst_device_monitor_add_filter (mon, "Test4", NULL);
  fail_unless (id > 0);

  devs = gst_device_monitor_get_devices (mon);
  fail_unless (g_list_length (devs) == 1);
  fail_unless_equals_pointer (devs->data, devices->data);
  g_list_free_full (devs, (GDestroyNotify) gst_object_unref);

  fail_unless (gst_device_monitor_start (mon));

  devs = gst_device_monitor_get_devices (mon);
  fail_unless (devs == NULL);

  bus = gst_device_monitor_get_bus (mon);

  msg = gst_bus_pop (bus);
  fail_unless (msg == NULL);

  mydev = test_device_new ();
  gst_device_provider_device_add (dp2, mydev);

  msg = gst_bus_pop (bus);
  fail_unless (msg != NULL);

  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_DEVICE_ADDED);

  gst_message_parse_device_added (msg, &dev);
  fail_unless_equals_pointer (dev, mydev);
  gst_object_unref (dev);
  gst_message_unref (msg);

  msg = gst_bus_pop (bus);
  fail_unless (msg == NULL);

  gst_device_provider_device_remove (dp2, mydev);
  devs = gst_device_monitor_get_devices (mon);
  fail_unless (devs == NULL);

  msg = gst_bus_pop (bus);
  fail_unless (msg != NULL);

  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_DEVICE_REMOVED);

  gst_message_parse_device_removed (msg, &dev);
  fail_unless_equals_pointer (dev, mydev);
  gst_object_unref (dev);
  gst_message_unref (msg);

  msg = gst_bus_pop (bus);
  fail_unless (msg == NULL);


  gst_device_monitor_stop (mon);

  gst_object_unref (bus);
  gst_object_unref (mon);

  gst_object_unref (dp);
  gst_object_unref (dp2);
  g_list_free_full (devices, (GDestroyNotify) gst_object_unref);

  /* should work fine without any filters */
  mon = gst_device_monitor_new ();
  fail_unless (gst_device_monitor_start (mon));
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
