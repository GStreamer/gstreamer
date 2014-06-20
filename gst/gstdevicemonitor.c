/* GStreamer
 * Copyright (C) 2012 Olivier Crete <olivier.crete@collabora.com>
 *
 * gstdevicemonitor.c: Device probing and monitoring
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
 * SECTION:gstdevicemonitor
 * @short_description: A device monitor and prober
 * @see_also: #GstDevice, #GstGlobalDeviceMonitor
 *
 * A #GstDeviceMonitor subclass is provided by a plugin that handles devices
 * if there is a way to programatically list connected devices. It can also
 * optionally provide updates to the list of connected devices.
 *
 * Each #GstDeviceMonitor subclass is a singleton, a plugin should
 * normally provide a single subclass for all devices.
 *
 * Applications would normally use a #GstGlobalDeviceMonitor to monitor devices
 * from all revelant monitors.
 *
 * Since: 1.4
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gst_private.h"

#include "gstdevicemonitor.h"

#include "gstelementmetadata.h"
#include "gstquark.h"

struct _GstDeviceMonitorPrivate
{
  GstBus *bus;

  GMutex start_lock;

  gboolean started_count;
};

/* this is used in gstelementfactory.c:gst_element_register() */
GQuark __gst_devicemonitorclass_factory = 0;

static void gst_device_monitor_class_init (GstDeviceMonitorClass * klass);
static void gst_device_monitor_init (GstDeviceMonitor * element);
static void gst_device_monitor_base_class_init (gpointer g_class);
static void gst_device_monitor_base_class_finalize (gpointer g_class);
static void gst_device_monitor_dispose (GObject * object);
static void gst_device_monitor_finalize (GObject * object);

static gpointer gst_device_monitor_parent_class = NULL;

GType
gst_device_monitor_get_type (void)
{
  static volatile gsize gst_device_monitor_type = 0;

  if (g_once_init_enter (&gst_device_monitor_type)) {
    GType _type;
    static const GTypeInfo element_info = {
      sizeof (GstDeviceMonitorClass),
      gst_device_monitor_base_class_init,
      gst_device_monitor_base_class_finalize,
      (GClassInitFunc) gst_device_monitor_class_init,
      NULL,
      NULL,
      sizeof (GstDeviceMonitor),
      0,
      (GInstanceInitFunc) gst_device_monitor_init,
      NULL
    };

    _type = g_type_register_static (GST_TYPE_OBJECT, "GstDeviceMonitor",
        &element_info, G_TYPE_FLAG_ABSTRACT);

    __gst_devicemonitorclass_factory =
        g_quark_from_static_string ("GST_DEVICEMONITORCLASS_FACTORY");
    g_once_init_leave (&gst_device_monitor_type, _type);
  }
  return gst_device_monitor_type;
}

static void
gst_device_monitor_base_class_init (gpointer g_class)
{
  GstDeviceMonitorClass *klass = GST_DEVICE_MONITOR_CLASS (g_class);

  /* Copy the element details here so elements can inherit the
   * details from their base class and classes only need to set
   * the details in class_init instead of base_init */
  klass->metadata =
      klass->metadata ? gst_structure_copy (klass->metadata) :
      gst_structure_new_empty ("metadata");

  klass->factory = g_type_get_qdata (G_TYPE_FROM_CLASS (klass),
      __gst_devicemonitorclass_factory);
}

static void
gst_device_monitor_base_class_finalize (gpointer g_class)
{
  GstDeviceMonitorClass *klass = GST_DEVICE_MONITOR_CLASS (g_class);

  gst_structure_free (klass->metadata);
}

static void
gst_device_monitor_class_init (GstDeviceMonitorClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gst_device_monitor_parent_class = g_type_class_peek_parent (klass);

  g_type_class_add_private (klass, sizeof (GstDeviceMonitorPrivate));

  gobject_class->dispose = gst_device_monitor_dispose;
  gobject_class->finalize = gst_device_monitor_finalize;
}

static void
gst_device_monitor_init (GstDeviceMonitor * monitor)
{
  monitor->priv = G_TYPE_INSTANCE_GET_PRIVATE (monitor,
      GST_TYPE_DEVICE_MONITOR, GstDeviceMonitorPrivate);

  g_mutex_init (&monitor->priv->start_lock);

  monitor->priv->bus = gst_bus_new ();
  gst_bus_set_flushing (monitor->priv->bus, TRUE);
}


static void
gst_device_monitor_dispose (GObject * object)
{
  GstDeviceMonitor *monitor = GST_DEVICE_MONITOR (object);

  gst_object_replace ((GstObject **) & monitor->priv->bus, NULL);

  GST_OBJECT_LOCK (monitor);
  g_list_free_full (monitor->devices, (GDestroyNotify) gst_object_unparent);
  monitor->devices = NULL;
  GST_OBJECT_UNLOCK (monitor);

  G_OBJECT_CLASS (gst_device_monitor_parent_class)->dispose (object);
}

static void
gst_device_monitor_finalize (GObject * object)
{
  GstDeviceMonitor *monitor = GST_DEVICE_MONITOR (object);

  g_mutex_clear (&monitor->priv->start_lock);

  G_OBJECT_CLASS (gst_device_monitor_parent_class)->finalize (object);
}

/**
 * gst_device_monitor_class_add_metadata:
 * @klass: class to set metadata for
 * @key: the key to set
 * @value: the value to set
 *
 * Set @key with @value as metadata in @klass.
 */
void
gst_device_monitor_class_add_metadata (GstDeviceMonitorClass * klass,
    const gchar * key, const gchar * value)
{
  g_return_if_fail (GST_IS_DEVICE_MONITOR_CLASS (klass));
  g_return_if_fail (key != NULL);
  g_return_if_fail (value != NULL);

  gst_structure_set ((GstStructure *) klass->metadata,
      key, G_TYPE_STRING, value, NULL);
}

/**
 * gst_device_monitor_class_add_static_metadata:
 * @klass: class to set metadata for
 * @key: the key to set
 * @value: the value to set
 *
 * Set @key with @value as metadata in @klass.
 *
 * Same as gst_device_monitor_class_add_metadata(), but @value must be a static string
 * or an inlined string, as it will not be copied. (GStreamer plugins will
 * be made resident once loaded, so this function can be used even from
 * dynamically loaded plugins.)
 *
 * Since: 1.4
 */
void
gst_device_monitor_class_add_static_metadata (GstDeviceMonitorClass * klass,
    const gchar * key, const gchar * value)
{
  GValue val = G_VALUE_INIT;

  g_return_if_fail (GST_IS_DEVICE_MONITOR_CLASS (klass));
  g_return_if_fail (key != NULL);
  g_return_if_fail (value != NULL);

  g_value_init (&val, G_TYPE_STRING);
  g_value_set_static_string (&val, value);
  gst_structure_take_value ((GstStructure *) klass->metadata, key, &val);
}

/**
 * gst_device_monitor_class_set_metadata:
 * @klass: class to set metadata for
 * @longname: The long English name of the device monitor. E.g. "File Sink"
 * @classification: String describing the type of device monitor, as an unordered list
 * separated with slashes ('/'). See draft-klass.txt of the design docs
 * for more details and common types. E.g: "Sink/File"
 * @description: Sentence describing the purpose of the device monitor.
 * E.g: "Write stream to a file"
 * @author: Name and contact details of the author(s). Use \n to separate
 * multiple author metadata. E.g: "Joe Bloggs &lt;joe.blogs at foo.com&gt;"
 *
 * Sets the detailed information for a #GstDeviceMonitorClass.
 * <note>This function is for use in _class_init functions only.</note>
 *
 * Since: 1.4
 */
void
gst_device_monitor_class_set_metadata (GstDeviceMonitorClass * klass,
    const gchar * longname, const gchar * classification,
    const gchar * description, const gchar * author)
{
  g_return_if_fail (GST_IS_DEVICE_MONITOR_CLASS (klass));
  g_return_if_fail (longname != NULL && *longname != '\0');
  g_return_if_fail (classification != NULL && *classification != '\0');
  g_return_if_fail (description != NULL && *description != '\0');
  g_return_if_fail (author != NULL && *author != '\0');

  gst_structure_id_set ((GstStructure *) klass->metadata,
      GST_QUARK (ELEMENT_METADATA_LONGNAME), G_TYPE_STRING, longname,
      GST_QUARK (ELEMENT_METADATA_KLASS), G_TYPE_STRING, classification,
      GST_QUARK (ELEMENT_METADATA_DESCRIPTION), G_TYPE_STRING, description,
      GST_QUARK (ELEMENT_METADATA_AUTHOR), G_TYPE_STRING, author, NULL);
}

/**
 * gst_device_monitor_class_set_static_metadata:
 * @klass: class to set metadata for
 * @longname: The long English name of the element. E.g. "File Sink"
 * @classification: String describing the type of element, as an unordered list
 * separated with slashes ('/'). See draft-klass.txt of the design docs
 * for more details and common types. E.g: "Sink/File"
 * @description: Sentence describing the purpose of the element.
 * E.g: "Write stream to a file"
 * @author: Name and contact details of the author(s). Use \n to separate
 * multiple author metadata. E.g: "Joe Bloggs &lt;joe.blogs at foo.com&gt;"
 *
 * Sets the detailed information for a #GstDeviceMonitorClass.
 * <note>This function is for use in _class_init functions only.</note>
 *
 * Same as gst_device_monitor_class_set_metadata(), but @longname, @classification,
 * @description, and @author must be static strings or inlined strings, as
 * they will not be copied. (GStreamer plugins will be made resident once
 * loaded, so this function can be used even from dynamically loaded plugins.)
 *
 * Since: 1.4
 */
void
gst_device_monitor_class_set_static_metadata (GstDeviceMonitorClass * klass,
    const gchar * longname, const gchar * classification,
    const gchar * description, const gchar * author)
{
  GstStructure *s = (GstStructure *) klass->metadata;
  GValue val = G_VALUE_INIT;

  g_return_if_fail (GST_IS_DEVICE_MONITOR_CLASS (klass));
  g_return_if_fail (longname != NULL && *longname != '\0');
  g_return_if_fail (classification != NULL && *classification != '\0');
  g_return_if_fail (description != NULL && *description != '\0');
  g_return_if_fail (author != NULL && *author != '\0');

  g_value_init (&val, G_TYPE_STRING);

  g_value_set_static_string (&val, longname);
  gst_structure_id_set_value (s, GST_QUARK (ELEMENT_METADATA_LONGNAME), &val);

  g_value_set_static_string (&val, classification);
  gst_structure_id_set_value (s, GST_QUARK (ELEMENT_METADATA_KLASS), &val);

  g_value_set_static_string (&val, description);
  gst_structure_id_set_value (s, GST_QUARK (ELEMENT_METADATA_DESCRIPTION),
      &val);

  g_value_set_static_string (&val, author);
  gst_structure_id_take_value (s, GST_QUARK (ELEMENT_METADATA_AUTHOR), &val);
}

/**
 * gst_device_monitor_class_get_metadata:
 * @klass: class to get metadata for
 * @key: the key to get
 *
 * Get metadata with @key in @klass.
 *
 * Returns: the metadata for @key.
 *
 * Since: 1.4
 */
const gchar *
gst_device_monitor_class_get_metadata (GstDeviceMonitorClass * klass,
    const gchar * key)
{
  g_return_val_if_fail (GST_IS_DEVICE_MONITOR_CLASS (klass), NULL);
  g_return_val_if_fail (key != NULL, NULL);

  return gst_structure_get_string ((GstStructure *) klass->metadata, key);
}

/**
 * gst_device_monitor_get_devices:
 * @monitor: A #GstDeviceMonitor
 *
 * Gets a list of devices that this monitor understands. This may actually
 * probe the hardware if the monitor is not currently started.
 *
 * Returns: (transfer full) (element-type GstDevice): a #GList of
 *   #GstDevice
 *
 * Since: 1.4
 */

GList *
gst_device_monitor_get_devices (GstDeviceMonitor * monitor)
{
  GstDeviceMonitorClass *klass;
  GList *devices = NULL;
  gboolean started;
  GList *item;

  g_return_val_if_fail (GST_IS_DEVICE_MONITOR (monitor), NULL);
  klass = GST_DEVICE_MONITOR_GET_CLASS (monitor);

  g_mutex_lock (&monitor->priv->start_lock);
  started = (monitor->priv->started_count > 0);

  if (started) {
    GST_OBJECT_LOCK (monitor);
    for (item = monitor->devices; item; item = item->next)
      devices = g_list_prepend (devices, gst_object_ref (item->data));
    GST_OBJECT_UNLOCK (monitor);
  } else if (klass->probe)
    devices = klass->probe (monitor);

  g_mutex_unlock (&monitor->priv->start_lock);

  return devices;
}

/**
 * gst_device_monitor_start:
 * @monitor: A #GstDeviceMonitor
 *
 * Starts monitoring the devices. This will cause #GST_MESSAGE_DEVICE messages
 * to be posted on the monitor's bus when devices are added or removed from
 * the system.
 *
 * Since the #GstDeviceMonitor is a singleton,
 * gst_device_monitor_start() may already have been called by another
 * user of the object, gst_device_monitor_stop() needs to be called the same
 * number of times.
 *
 * Returns: %TRUE if the device monitoring could be started
 *
 * Since: 1.4
 */

gboolean
gst_device_monitor_start (GstDeviceMonitor * monitor)
{
  GstDeviceMonitorClass *klass;
  gboolean ret = FALSE;

  g_return_val_if_fail (GST_IS_DEVICE_MONITOR (monitor), FALSE);
  klass = GST_DEVICE_MONITOR_GET_CLASS (monitor);

  g_mutex_lock (&monitor->priv->start_lock);

  if (monitor->priv->started_count > 0) {
    ret = TRUE;
    goto started;
  }

  if (klass->start)
    ret = klass->start (monitor);

  if (ret) {
    monitor->priv->started_count++;
    gst_bus_set_flushing (monitor->priv->bus, FALSE);
  }

started:

  g_mutex_unlock (&monitor->priv->start_lock);

  return ret;
}

/**
 * gst_device_monitor_stop:
 * @monitor: A #GstDeviceMonitor
 *
 * Decreases the use-count by one. If the use count reaches zero, this
 * #GstDeviceMonitor will stop monitoring the devices. This needs to be
 * called the same number of times that gst_device_monitor_start() was called.
 *
 * Since: 1.4
 */

void
gst_device_monitor_stop (GstDeviceMonitor * monitor)
{
  GstDeviceMonitorClass *klass;

  g_return_if_fail (GST_IS_DEVICE_MONITOR (monitor));
  klass = GST_DEVICE_MONITOR_GET_CLASS (monitor);

  g_mutex_lock (&monitor->priv->start_lock);

  if (monitor->priv->started_count == 1) {
    gst_bus_set_flushing (monitor->priv->bus, TRUE);
    if (klass->stop)
      klass->stop (monitor);
    GST_OBJECT_LOCK (monitor);
    g_list_free_full (monitor->devices, (GDestroyNotify) gst_object_unparent);
    monitor->devices = NULL;
    GST_OBJECT_UNLOCK (monitor);
  } else if (monitor->priv->started_count < 1) {
    g_critical ("Trying to stop a GstDeviceMonitor %s which is already stopped",
        GST_OBJECT_NAME (monitor));
  }

  monitor->priv->started_count--;
  g_mutex_unlock (&monitor->priv->start_lock);
}


/**
 * gst_device_monitor_get_factory:
 * @monitor: a #GstDeviceMonitor to request the device monitor factory of.
 *
 * Retrieves the factory that was used to create this device monitor.
 *
 * Returns: (transfer none): the #GstDeviceMonitorFactory used for creating this
 *     device monitor. no refcounting is needed.
 *
 * Since: 1.4
 */
GstDeviceMonitorFactory *
gst_device_monitor_get_factory (GstDeviceMonitor * monitor)
{
  g_return_val_if_fail (GST_IS_DEVICE_MONITOR (monitor), NULL);

  return GST_DEVICE_MONITOR_GET_CLASS (monitor)->factory;
}

/**
 * gst_device_monitor_can_monitor:
 * @monitor: a #GstDeviceMonitor
 *
 * If this function returns %TRUE, then the device monitor can monitor if
 * devices are added or removed. Otherwise, it can only do static probing.
 *
 * Returns: %TRUE if the #GstDeviceMonitor support monitoring, %FALSE otherwise
 */
gboolean
gst_device_monitor_can_monitor (GstDeviceMonitor * monitor)
{
  GstDeviceMonitorClass *klass;

  g_return_val_if_fail (GST_IS_DEVICE_MONITOR (monitor), FALSE);
  klass = GST_DEVICE_MONITOR_GET_CLASS (monitor);

  if (klass->start)
    return TRUE;
  else
    return FALSE;
}

/**
 * gst_device_monitor_get_bus:
 * @monitor: a #GstDeviceMonitor
 *
 * Gets the #GstBus of this #GstDeviceMonitor
 *
 * Returns: (transfer full): a #GstBus
 *
 * Since: 1.4
 */
GstBus *
gst_device_monitor_get_bus (GstDeviceMonitor * monitor)
{
  g_return_val_if_fail (GST_IS_DEVICE_MONITOR (monitor), NULL);

  return gst_object_ref (monitor->priv->bus);
}

/**
 * gst_device_monitor_device_add:
 * @monitor: a #GstDeviceMonitor
 * @device: (transfer full): a #GstDevice that has been added
 *
 * Posts a message on the monitor's #GstBus to inform applications that
 * a new device has been added.
 *
 * This is for use by subclasses.
 *
 * Since: 1.4
 */
void
gst_device_monitor_device_add (GstDeviceMonitor * monitor, GstDevice * device)
{
  GstMessage *message;

  if (!gst_object_set_parent (GST_OBJECT (device), GST_OBJECT (monitor))) {
    GST_WARNING_OBJECT (monitor, "Could not parent device %p to monitor,"
        " it already has a parent", device);
    return;
  }

  GST_OBJECT_LOCK (monitor);
  monitor->devices = g_list_prepend (monitor->devices, gst_object_ref (device));
  GST_OBJECT_UNLOCK (monitor);

  message = gst_message_new_device_added (GST_OBJECT (monitor), device);
  gst_bus_post (monitor->priv->bus, message);
  gst_object_unref (device);
}


/**
 * gst_device_monitor_device_remove:
 * @monitor: a #GstDeviceMonitor
 * @device: a #GstDevice that has been removed
 *
 * Posts a message on the monitor's #GstBus to inform applications that
 * a device has been removed.
 *
 * This is for use by subclasses.
 *
 * Since: 1.4
 */
void
gst_device_monitor_device_remove (GstDeviceMonitor * monitor,
    GstDevice * device)
{
  GstMessage *message;
  GList *item;

  GST_OBJECT_LOCK (monitor);
  item = g_list_find (monitor->devices, device);
  if (item) {
    monitor->devices = g_list_delete_link (monitor->devices, item);
  }
  GST_OBJECT_UNLOCK (monitor);

  message = gst_message_new_device_removed (GST_OBJECT (monitor), device);
  g_signal_emit_by_name (device, "removed");
  gst_bus_post (monitor->priv->bus, message);
  if (item)
    gst_object_unparent (GST_OBJECT (device));
}
