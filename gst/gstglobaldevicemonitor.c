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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gstglobaldevicemonitor.h>

#include "gst/gst_private.h"
#include <gst/gst.h>

struct _GstGlobalDeviceMonitorPrivate
{
  gboolean started;

  GstBus *bus;

  GPtrArray *monitors;
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

  self->priv->monitors = g_ptr_array_new ();
  self->priv->caps = gst_caps_new_any ();
  self->priv->classes = g_strdup ("");

  factories =
      gst_device_monitor_factory_list_get_device_monitors (self->priv->classes,
      1);

  while (factories) {
    GstDeviceMonitorFactory *factory = factories->data;
    GstDeviceMonitor *monitor;

    factories = g_list_remove (factories, factory);

    monitor = gst_device_monitor_factory_get (factory);
    if (monitor) {
      GstBus *bus = gst_device_monitor_get_bus (monitor);

      gst_bus_enable_sync_message_emission (bus);
      g_signal_connect (monitor, "sync-message",
          G_CALLBACK (bus_sync_message), self);
      g_ptr_array_add (self->priv->monitors, monitor);
    }

    gst_object_unref (factory);
  }
}


static void
gst_global_device_monitor_remove (GstGlobalDeviceMonitor * self, guint i)
{
  GstDeviceMonitor *monitor = g_ptr_array_index (self->priv->monitors, i);
  GstBus *bus;

  g_ptr_array_remove_index_fast (self->priv->monitors, i);

  bus = gst_device_monitor_get_bus (monitor);
  g_signal_handlers_disconnect_by_func (bus, bus_sync_message, self);
  gst_object_unref (bus);

  gst_object_unref (monitor);
}

static void
gst_global_device_monitor_dispose (GObject * object)
{
  GstGlobalDeviceMonitor *self = GST_GLOBAL_DEVICE_MONITOR (object);

  g_return_if_fail (self->priv->started == FALSE);

  if (self->priv->monitors) {
    while (self->priv->monitors->len)
      gst_global_device_monitor_remove (self, self->priv->monitors->len - 1);
    g_ptr_array_unref (self->priv->monitors);
    self->priv->monitors = NULL;
  }

  gst_caps_replace (&self->priv->caps, NULL);
  g_free (self->priv->classes);
  gst_object_replace ((GstObject **) & self->priv->bus, NULL);

  G_OBJECT_CLASS (gst_global_device_monitor_parent_class)->dispose (object);
}

/**
 * gst_global_device_monitor_get_devices:
 * @monitor: A #GstDeviceMonitor
 *
 * Gets a list of devices from all of the relevant monitors. This may actually
 * probe the hardware if the global monitor is not currently started.
 *
 * Returns: (transfer full) (element-type GstDevice): a #GList of
 *   #GstDevice
 */

GList *
gst_global_device_monitor_get_devices (GstGlobalDeviceMonitor * self)
{
  GList *devices = NULL;
  guint i;
  guint cookie;

  g_return_val_if_fail (GST_IS_GLOBAL_DEVICE_MONITOR (self), NULL);

  GST_OBJECT_LOCK (self);

again:

  g_list_free_full (devices, gst_object_unref);
  devices = NULL;

  cookie = self->priv->cookie;

  for (i = 0; i < self->priv->monitors->len; i++) {
    GList *tmpdev;
    GstDeviceMonitor *monitor =
        gst_object_ref (g_ptr_array_index (self->priv->monitors, i));
    GList *item;

    GST_OBJECT_UNLOCK (self);

    tmpdev = gst_device_monitor_get_devices (monitor);

    for (item = tmpdev; item; item = item->next) {
      GstDevice *dev = GST_DEVICE (item->data);
      GstCaps *caps = gst_device_get_caps (dev);

      if (gst_caps_can_intersect (self->priv->caps, caps) &&
          gst_device_has_classes (dev, self->priv->classes))
        devices = g_list_prepend (devices, gst_object_ref (dev));
      gst_caps_unref (caps);
    }

    g_list_free_full (tmpdev, gst_object_unref);
    gst_object_unref (monitor);

    GST_OBJECT_LOCK (self);

    if (self->priv->cookie != cookie)
      goto again;
  }

  GST_OBJECT_UNLOCK (self);

  return devices;
}

/**
 * gst_global_device_monitor_start:
 * @monitor: A #GstGlobalDeviceMonitor
 *
 * Starts monitoring the devices, one this has succeeded, the
 * #GstGlobalDeviceMonitor:added and #GstGlobalDeviceMonitor:removed
 * signals will be emitted when the list of devices changes.
 *
 * Returns: %TRUE if the device monitoring could be started
 */

gboolean
gst_global_device_monitor_start (GstGlobalDeviceMonitor * self)
{
  guint i;

  g_return_val_if_fail (GST_IS_GLOBAL_DEVICE_MONITOR (self), FALSE);

  GST_OBJECT_LOCK (self);

  if (self->priv->monitors->len == 0) {
    GST_OBJECT_UNLOCK (self);
    return FALSE;
  }

  gst_bus_set_flushing (self->priv->bus, FALSE);

  for (i = 0; i < self->priv->monitors->len; i++) {
    if (!gst_device_monitor_start (g_ptr_array_index (self->priv->monitors, i))) {
      gst_bus_set_flushing (self->priv->bus, TRUE);

      for (; i != 0; i--)
        gst_device_monitor_stop (g_ptr_array_index (self->priv->monitors,
                i - 1));

      GST_OBJECT_UNLOCK (self);
      return FALSE;
    }
  }

  self->priv->started = TRUE;
  GST_OBJECT_UNLOCK (self);

  return TRUE;
}

/**
 * gst_global_device_monitor_stop:
 * @monitor: A #GstDeviceMonitor
 *
 * Stops monitoring the devices.
 */

void
gst_global_device_monitor_stop (GstGlobalDeviceMonitor * self)
{
  guint i;

  g_return_if_fail (GST_IS_GLOBAL_DEVICE_MONITOR (self));

  gst_bus_set_flushing (self->priv->bus, TRUE);

  GST_OBJECT_LOCK (self);
  for (i = 0; i < self->priv->monitors->len; i++)
    gst_device_monitor_stop (g_ptr_array_index (self->priv->monitors, i));
  self->priv->started = FALSE;
  GST_OBJECT_UNLOCK (self);

}

void
gst_global_device_monitor_set_classes_filter (GstGlobalDeviceMonitor * self,
    const gchar * classes)
{
  GList *factories = NULL;
  guint i;

  g_return_if_fail (GST_IS_GLOBAL_DEVICE_MONITOR (self));
  g_return_if_fail (!self->priv->started);

  GST_OBJECT_LOCK (self);
  if (!strcmp (self->priv->classes, classes)) {
    GST_OBJECT_UNLOCK (self);
    return;
  }

  g_free (self->priv->classes);
  self->priv->classes = g_strdup (classes);

  factories =
      gst_device_monitor_factory_list_get_device_monitors (self->priv->classes,
      1);

  for (i = 0; i < self->priv->monitors->len; i++) {
    GstDeviceMonitor *monitor = g_ptr_array_index (self->priv->monitors, i);
    GstDeviceMonitorFactory *f = gst_device_monitor_get_factory (monitor);
    GList *item;

    item = g_list_find (factories, f);

    if (item) {
      /* If the item is in our list, then remove it from the list of factories,
       * we don't have it to re-create it later
       */
      factories = g_list_remove_link (factories, item);
      gst_object_unref (f);
    } else {
      /* If it's not in our list, them remove it from the list of monitors.
       */

      self->priv->cookie++;
      gst_global_device_monitor_remove (self, i);
      i--;
    }
  }

  while (factories) {
    GstDeviceMonitorFactory *factory = factories->data;
    GstDeviceMonitor *monitor;

    factories = g_list_remove (factories, factory);

    monitor = gst_device_monitor_factory_get (factory);
    if (monitor) {
      GstBus *bus = gst_device_monitor_get_bus (monitor);

      gst_bus_enable_sync_message_emission (bus);
      g_signal_connect (bus, "sync-message",
          G_CALLBACK (bus_sync_message), self);
      gst_object_unref (bus);
      g_ptr_array_add (self->priv->monitors, monitor);
      self->priv->cookie++;
    }

    gst_object_unref (factory);
  }

  GST_OBJECT_UNLOCK (self);
}

gchar *
gst_global_device_monitor_get_classes_filter (GstGlobalDeviceMonitor * self)
{
  gchar *res;

  g_return_val_if_fail (GST_IS_GLOBAL_DEVICE_MONITOR (self), 0);

  GST_OBJECT_LOCK (self);
  res = g_strdup (self->priv->classes);
  GST_OBJECT_UNLOCK (self);

  return res;
}

void
gst_global_device_monitor_set_caps_filter (GstGlobalDeviceMonitor * self,
    GstCaps * caps)
{
  g_return_if_fail (GST_IS_GLOBAL_DEVICE_MONITOR (self));
  g_return_if_fail (GST_IS_CAPS (caps));

  GST_OBJECT_LOCK (self);
  gst_caps_replace (&self->priv->caps, caps);
  GST_OBJECT_UNLOCK (self);
}

GstCaps *
gst_global_device_monitor_get_caps_filter (GstGlobalDeviceMonitor * self)
{
  GstCaps *res;

  g_return_val_if_fail (GST_IS_GLOBAL_DEVICE_MONITOR (self), NULL);

  GST_OBJECT_LOCK (self);
  res = gst_caps_ref (self->priv->caps);
  GST_OBJECT_UNLOCK (self);

  return res;
}

GstGlobalDeviceMonitor *
gst_global_device_monitor_new (void)
{
  return g_object_new (GST_TYPE_GLOBAL_DEVICE_MONITOR, NULL);
}

/**
 * gst_global_device_monitor_get_bus:
 * @monitor: a #GstDeviceMonitor
 *
 * Gets the #GstBus of this #GstGlobalDeviceMonitor
 *
 * Returns: (transfer full): a #GstBus
 */
GstBus *
gst_global_device_monitor_get_bus (GstGlobalDeviceMonitor * monitor)
{
  g_return_val_if_fail (GST_IS_GLOBAL_DEVICE_MONITOR (monitor), NULL);

  return gst_object_ref (monitor->priv->bus);
}
