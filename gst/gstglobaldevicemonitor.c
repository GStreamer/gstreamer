/* GStreamer
 * Copyright (C) 2013 Olivier Crete <olivier.crete@collabora.com>
 *
 * gstglobaldevicemonitor.c: Global device monitor
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

/**
 * SECTION:gstglobaldevicemonitor
 * @short_description: A global device monitor and prober
 * @see_also: #GstDevice, #GstDeviceProvider
 *
 * Applications should create a #GstGlobalDeviceMonitor when they want
 * to probe, list and monitor devices of a specific type. The
 * #GstGlobalDeviceMonitor will create the appropriate
 * #GstDeviceProvider objects and manage them. It will then post
 * messages on its #GstBus for devices that have been added and
 * removed.
 *
 * Since: 1.4
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gst_private.h"
#include "gstglobaldevicemonitor.h"

struct _GstGlobalDeviceMonitorPrivate
{
  gboolean started;

  GstBus *bus;

  GPtrArray *providers;
  guint cookie;

  GstCaps *caps;
  gchar *classes;
};


G_DEFINE_TYPE (GstGlobalDeviceMonitor, gst_global_device_monitor,
    GST_TYPE_OBJECT);

static void gst_global_device_monitor_dispose (GObject * object);

static void
gst_global_device_monitor_class_init (GstGlobalDeviceMonitorClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GstGlobalDeviceMonitorPrivate));

  object_class->dispose = gst_global_device_monitor_dispose;
}

static void
bus_sync_message (GstBus * bus, GstMessage * message,
    GstGlobalDeviceMonitor * monitor)
{
  GstMessageType type = GST_MESSAGE_TYPE (message);

  if (type == GST_MESSAGE_DEVICE_ADDED || type == GST_MESSAGE_DEVICE_REMOVED) {
    gboolean matches;
    GstCaps *caps;
    GstDevice *device;

    if (type == GST_MESSAGE_DEVICE_ADDED)
      gst_message_parse_device_added (message, &device);
    else
      gst_message_parse_device_removed (message, &device);

    GST_OBJECT_LOCK (monitor);
    caps = gst_device_get_caps (device);
    matches = gst_caps_can_intersect (monitor->priv->caps, caps) &&
        gst_device_has_classes (device, monitor->priv->classes);
    gst_caps_unref (caps);
    GST_OBJECT_UNLOCK (monitor);

    if (matches)
      gst_bus_post (monitor->priv->bus, gst_message_ref (message));
  }
}


static void
gst_global_device_monitor_init (GstGlobalDeviceMonitor * self)
{
  GList *factories;

  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GST_TYPE_GLOBAL_DEVICE_MONITOR, GstGlobalDeviceMonitorPrivate);

  self->priv->bus = gst_bus_new ();
  gst_bus_set_flushing (self->priv->bus, TRUE);

  self->priv->providers = g_ptr_array_new ();
  self->priv->caps = gst_caps_new_any ();
  self->priv->classes = g_strdup ("");

  factories =
      gst_device_provider_factory_list_get_device_providers (self->priv->
      classes, 1);

  while (factories) {
    GstDeviceProviderFactory *factory = factories->data;
    GstDeviceProvider *provider;

    factories = g_list_remove (factories, factory);

    provider = gst_device_provider_factory_get (factory);
    if (provider) {
      GstBus *bus = gst_device_provider_get_bus (provider);

      gst_bus_enable_sync_message_emission (bus);
      g_signal_connect (bus, "sync-message",
          G_CALLBACK (bus_sync_message), self);
      g_ptr_array_add (self->priv->providers, provider);
    }

    gst_object_unref (factory);
  }
}


static void
gst_global_device_monitor_remove (GstGlobalDeviceMonitor * self, guint i)
{
  GstDeviceProvider *provider = g_ptr_array_index (self->priv->providers, i);
  GstBus *bus;

  g_ptr_array_remove_index_fast (self->priv->providers, i);

  bus = gst_device_provider_get_bus (provider);
  g_signal_handlers_disconnect_by_func (bus, bus_sync_message, self);
  gst_object_unref (bus);

  gst_object_unref (provider);
}

static void
gst_global_device_monitor_dispose (GObject * object)
{
  GstGlobalDeviceMonitor *self = GST_GLOBAL_DEVICE_MONITOR (object);

  g_return_if_fail (self->priv->started == FALSE);

  if (self->priv->providers) {
    while (self->priv->providers->len)
      gst_global_device_monitor_remove (self, self->priv->providers->len - 1);
    g_ptr_array_unref (self->priv->providers);
    self->priv->providers = NULL;
  }

  gst_caps_replace (&self->priv->caps, NULL);
  g_free (self->priv->classes);
  gst_object_replace ((GstObject **) & self->priv->bus, NULL);

  G_OBJECT_CLASS (gst_global_device_monitor_parent_class)->dispose (object);
}

/**
 * gst_global_device_monitor_get_devices:
 * @monitor: A #GstDeviceProvider
 *
 * Gets a list of devices from all of the relevant monitors. This may actually
 * probe the hardware if the global monitor is not currently started.
 *
 * Returns: (transfer full) (element-type GstDevice): a #GList of
 *   #GstDevice
 *
 * Since: 1.4
 */

GList *
gst_global_device_monitor_get_devices (GstGlobalDeviceMonitor * monitor)
{
  GList *devices = NULL;
  guint i;
  guint cookie;

  g_return_val_if_fail (GST_IS_GLOBAL_DEVICE_MONITOR (monitor), NULL);

  GST_OBJECT_LOCK (monitor);

again:

  g_list_free_full (devices, gst_object_unref);
  devices = NULL;

  cookie = monitor->priv->cookie;

  for (i = 0; i < monitor->priv->providers->len; i++) {
    GList *tmpdev;
    GstDeviceProvider *provider =
        gst_object_ref (g_ptr_array_index (monitor->priv->providers, i));
    GList *item;

    GST_OBJECT_UNLOCK (monitor);

    tmpdev = gst_device_provider_get_devices (provider);

    for (item = tmpdev; item; item = item->next) {
      GstDevice *dev = GST_DEVICE (item->data);
      GstCaps *caps = gst_device_get_caps (dev);

      if (gst_caps_can_intersect (monitor->priv->caps, caps) &&
          gst_device_has_classes (dev, monitor->priv->classes))
        devices = g_list_prepend (devices, gst_object_ref (dev));
      gst_caps_unref (caps);
    }

    g_list_free_full (tmpdev, gst_object_unref);
    gst_object_unref (provider);

    GST_OBJECT_LOCK (monitor);

    if (monitor->priv->cookie != cookie)
      goto again;
  }

  GST_OBJECT_UNLOCK (monitor);

  return devices;
}

/**
 * gst_global_device_monitor_start:
 * @monitor: A #GstGlobalDeviceMonitor
 *
 * Starts monitoring the devices, one this has succeeded, the
 * %GST_MESSAGE_DEVICE_ADDED and %GST_MESSAGE_DEVICE_REMOVED messages
 * will be emitted on the bus when the list of devices changes.
 *
 * Returns: %TRUE if the device monitoring could be started
 *
 * Since: 1.4
 */

gboolean
gst_global_device_monitor_start (GstGlobalDeviceMonitor * monitor)
{
  guint i;

  g_return_val_if_fail (GST_IS_GLOBAL_DEVICE_MONITOR (monitor), FALSE);

  GST_OBJECT_LOCK (monitor);

  if (monitor->priv->providers->len == 0) {
    GST_OBJECT_UNLOCK (monitor);
    return FALSE;
  }

  gst_bus_set_flushing (monitor->priv->bus, FALSE);

  for (i = 0; i < monitor->priv->providers->len; i++) {
    if (!gst_device_provider_start (g_ptr_array_index (monitor->priv->providers,
                i))) {
      gst_bus_set_flushing (monitor->priv->bus, TRUE);

      for (; i != 0; i--)
        gst_device_provider_stop (g_ptr_array_index (monitor->priv->providers,
                i - 1));

      GST_OBJECT_UNLOCK (monitor);
      return FALSE;
    }
  }

  monitor->priv->started = TRUE;
  GST_OBJECT_UNLOCK (monitor);

  return TRUE;
}

/**
 * gst_global_device_monitor_stop:
 * @monitor: A #GstDeviceProvider
 *
 * Stops monitoring the devices.
 *
 * Since: 1.4
 */
void
gst_global_device_monitor_stop (GstGlobalDeviceMonitor * monitor)
{
  guint i;

  g_return_if_fail (GST_IS_GLOBAL_DEVICE_MONITOR (monitor));

  gst_bus_set_flushing (monitor->priv->bus, TRUE);

  GST_OBJECT_LOCK (monitor);
  for (i = 0; i < monitor->priv->providers->len; i++)
    gst_device_provider_stop (g_ptr_array_index (monitor->priv->providers, i));
  monitor->priv->started = FALSE;
  GST_OBJECT_UNLOCK (monitor);

}

/**
 * gst_global_device_monitor_set_classes_filter:
 * @monitor: the global device monitor
 * @classes: device classes to use as filter
 *
 * Filter devices monitored by device class, e.g. in case you are only
 * interested in a certain type of device like audio devices or
 * video sources.
 *
 * Since: 1.4
 */
void
gst_global_device_monitor_set_classes_filter (GstGlobalDeviceMonitor * monitor,
    const gchar * classes)
{
  GList *factories = NULL;
  guint i;

  g_return_if_fail (GST_IS_GLOBAL_DEVICE_MONITOR (monitor));
  g_return_if_fail (!monitor->priv->started);

  GST_OBJECT_LOCK (monitor);
  if (!strcmp (monitor->priv->classes, classes)) {
    GST_OBJECT_UNLOCK (monitor);
    return;
  }

  g_free (monitor->priv->classes);
  monitor->priv->classes = g_strdup (classes);

  factories = gst_device_provider_factory_list_get_device_providers (classes,
      1);

  for (i = 0; i < monitor->priv->providers->len; i++) {
    GstDeviceProvider *provider;
    GstDeviceProviderFactory *f;
    GList *item;

    provider = g_ptr_array_index (monitor->priv->providers, i);
    f = gst_device_provider_get_factory (provider);

    item = g_list_find (factories, f);

    if (item) {
      /* If the item is in our list, then remove it from the list of factories,
       * we don't have it to re-create it later
       */
      factories = g_list_remove_link (factories, item);
      gst_object_unref (f);
    } else {
      /* If it's not in our list, them remove it from the list of providers.
       */

      monitor->priv->cookie++;
      gst_global_device_monitor_remove (monitor, i);
      i--;
    }
  }

  while (factories) {
    GstDeviceProviderFactory *factory = factories->data;
    GstDeviceProvider *provider;

    factories = g_list_remove (factories, factory);

    provider = gst_device_provider_factory_get (factory);
    if (provider) {
      GstBus *bus = gst_device_provider_get_bus (provider);

      gst_bus_enable_sync_message_emission (bus);
      g_signal_connect (bus, "sync-message",
          G_CALLBACK (bus_sync_message), monitor);
      gst_object_unref (bus);
      g_ptr_array_add (monitor->priv->providers, provider);
      monitor->priv->cookie++;
    }

    gst_object_unref (factory);
  }

  GST_OBJECT_UNLOCK (monitor);
}

/**
 * gst_global_device_monitor_get_classes_filter:
 * @monitor: the global device monitor
 *
 * Return the type (device classes) filter active for device filtering.
 *
 * Returns: string of device classes that are being filtered.
 *
 * Since: 1.4
 */
gchar *
gst_global_device_monitor_get_classes_filter (GstGlobalDeviceMonitor * monitor)
{
  gchar *res;

  g_return_val_if_fail (GST_IS_GLOBAL_DEVICE_MONITOR (monitor), 0);

  GST_OBJECT_LOCK (monitor);
  res = g_strdup (monitor->priv->classes);
  GST_OBJECT_UNLOCK (monitor);

  return res;
}

/**
 * gst_global_device_monitor_set_caps_filter:
 * @monitor: the global device monitor
 * @caps: caps to filter
 *
 * Set caps to use as filter for devices. By default ANY caps are used,
 * meaning no caps filter is active.
 *
 * Since: 1.4
 */
void
gst_global_device_monitor_set_caps_filter (GstGlobalDeviceMonitor * monitor,
    GstCaps * caps)
{
  g_return_if_fail (GST_IS_GLOBAL_DEVICE_MONITOR (monitor));
  g_return_if_fail (GST_IS_CAPS (caps));

  GST_OBJECT_LOCK (monitor);
  gst_caps_replace (&monitor->priv->caps, caps);
  GST_OBJECT_UNLOCK (monitor);
}

/**
 * gst_global_device_monitor_get_caps_filter:
 * @monitor: a global device monitor
 *
 * Get the #GstCaps filter set by gst_global_device_monitor_set_caps_filter().
 *
 * Returns: (transfer full): the filter caps that are active (or ANY caps)
 *
 * Since: 1.4
 */
GstCaps *
gst_global_device_monitor_get_caps_filter (GstGlobalDeviceMonitor * monitor)
{
  GstCaps *res;

  g_return_val_if_fail (GST_IS_GLOBAL_DEVICE_MONITOR (monitor), NULL);

  GST_OBJECT_LOCK (monitor);
  res = gst_caps_ref (monitor->priv->caps);
  GST_OBJECT_UNLOCK (monitor);

  return res;
}

/**
 * gst_global_device_monitor_new:
 *
 * Create a new #GstGlobalDeviceMonitor
 *
 * Returns: (transfer full): a new global device monitor.
 *
 * Since: 1.4
 */
GstGlobalDeviceMonitor *
gst_global_device_monitor_new (void)
{
  return g_object_new (GST_TYPE_GLOBAL_DEVICE_MONITOR, NULL);
}

/**
 * gst_global_device_monitor_get_bus:
 * @monitor: a #GstDeviceProvider
 *
 * Gets the #GstBus of this #GstGlobalDeviceMonitor
 *
 * Returns: (transfer full): a #GstBus
 *
 * Since: 1.4
 */
GstBus *
gst_global_device_monitor_get_bus (GstGlobalDeviceMonitor * monitor)
{
  g_return_val_if_fail (GST_IS_GLOBAL_DEVICE_MONITOR (monitor), NULL);

  return gst_object_ref (monitor->priv->bus);
}
