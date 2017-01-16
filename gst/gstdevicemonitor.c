/* GStreamer
 * Copyright (C) 2013 Olivier Crete <olivier.crete@collabora.com>
 *
 * gstdevicemonitor.c: device monitor
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
 * @title: GstDeviceMonitor
 * @short_description: A device monitor and prober
 * @see_also: #GstDevice, #GstDeviceProvider
 *
 * Applications should create a #GstDeviceMonitor when they want
 * to probe, list and monitor devices of a specific type. The
 * #GstDeviceMonitor will create the appropriate
 * #GstDeviceProvider objects and manage them. It will then post
 * messages on its #GstBus for devices that have been added and
 * removed.
 *
 * The device monitor will monitor all devices matching the filters that
 * the application has set.
 *
 * The basic use pattern of a device monitor is as follows:
 * |[
 *   static gboolean
 *   my_bus_func (GstBus * bus, GstMessage * message, gpointer user_data)
 *   {
 *      GstDevice *device;
 *      gchar *name;
 *
 *      switch (GST_MESSAGE_TYPE (message)) {
 *        case GST_MESSAGE_DEVICE_ADDED:
 *          gst_message_parse_device_added (message, &device);
 *          name = gst_device_get_display_name (device);
 *          g_print("Device added: %s\n", name);
 *          g_free (name);
 *          gst_object_unref (device);
 *          break;
 *        case GST_MESSAGE_DEVICE_REMOVED:
 *          gst_message_parse_device_removed (message, &device);
 *          name = gst_device_get_display_name (device);
 *          g_print("Device removed: %s\n", name);
 *          g_free (name);
 *          gst_object_unref (device);
 *          break;
 *        default:
 *          break;
 *      }
 *
 *      return G_SOURCE_CONTINUE;
 *   }
 *
 *   GstDeviceMonitor *
 *   setup_raw_video_source_device_monitor (void) {
 *      GstDeviceMonitor *monitor;
 *      GstBus *bus;
 *      GstCaps *caps;
 *
 *      monitor = gst_device_monitor_new ();
 *
 *      bus = gst_device_monitor_get_bus (monitor);
 *      gst_bus_add_watch (bus, my_bus_func, NULL);
 *      gst_object_unref (bus);
 *
 *      caps = gst_caps_new_empty_simple ("video/x-raw");
 *      gst_device_monitor_add_filter (monitor, "Video/Source", caps);
 *      gst_caps_unref (caps);
 *
 *      gst_device_monitor_start (monitor);
 *
 *      return monitor;
 *   }
 * ]|
 *
 * Since: 1.4
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gst_private.h"
#include "gstdevicemonitor.h"

struct _GstDeviceMonitorPrivate
{
  gboolean started;

  GstBus *bus;

  GPtrArray *providers;
  guint cookie;

  GPtrArray *filters;

  guint last_id;
  GList *hidden;
  gboolean show_all;
};

#define DEFAULT_SHOW_ALL        FALSE

enum
{
  PROP_SHOW_ALL = 1,
};

G_DEFINE_TYPE (GstDeviceMonitor, gst_device_monitor, GST_TYPE_OBJECT);

static void gst_device_monitor_dispose (GObject * object);

struct DeviceFilter
{
  guint id;

  gchar **classesv;
  GstCaps *caps;
};

static void
device_filter_free (struct DeviceFilter *filter)
{
  g_strfreev (filter->classesv);
  gst_caps_unref (filter->caps);

  g_slice_free (struct DeviceFilter, filter);
}

static void
gst_device_monitor_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDeviceMonitor *monitor = GST_DEVICE_MONITOR (object);

  switch (prop_id) {
    case PROP_SHOW_ALL:
      g_value_set_boolean (value,
          gst_device_monitor_get_show_all_devices (monitor));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_device_monitor_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDeviceMonitor *monitor = GST_DEVICE_MONITOR (object);

  switch (prop_id) {
    case PROP_SHOW_ALL:
      gst_device_monitor_set_show_all_devices (monitor,
          g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_device_monitor_class_init (GstDeviceMonitorClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GstDeviceMonitorPrivate));

  object_class->get_property = gst_device_monitor_get_property;
  object_class->set_property = gst_device_monitor_set_property;
  object_class->dispose = gst_device_monitor_dispose;

  g_object_class_install_property (object_class, PROP_SHOW_ALL,
      g_param_spec_boolean ("show-all", "Show All",
          "Show all devices, even those from hidden providers",
          DEFAULT_SHOW_ALL, G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE));
}

/* must be called with monitor lock */
static gboolean
is_provider_hidden (GstDeviceMonitor * monitor, GList * hidden,
    GstDeviceProvider * provider)
{
  GstDeviceProviderFactory *factory;

  if (monitor->priv->show_all)
    return FALSE;

  factory = gst_device_provider_get_factory (provider);
  if (g_list_find_custom (hidden, GST_OBJECT_NAME (factory),
          (GCompareFunc) g_strcmp0))
    return TRUE;

  return FALSE;
}

/* must be called with monitor lock */
static void
update_hidden_providers_list (GList ** hidden, GstDeviceProvider * provider)
{
  gchar **obs;

  obs = gst_device_provider_get_hidden_providers (provider);
  if (obs) {
    gint i;

    for (i = 0; obs[i]; i++)
      *hidden = g_list_prepend (*hidden, obs[i]);

    g_free (obs);
  }
}

static void
bus_sync_message (GstBus * bus, GstMessage * message,
    GstDeviceMonitor * monitor)
{
  GstMessageType type = GST_MESSAGE_TYPE (message);

  if (type == GST_MESSAGE_DEVICE_ADDED || type == GST_MESSAGE_DEVICE_REMOVED) {
    gboolean matches;
    GstDevice *device;
    GstDeviceProvider *provider;

    if (type == GST_MESSAGE_DEVICE_ADDED)
      gst_message_parse_device_added (message, &device);
    else
      gst_message_parse_device_removed (message, &device);

    GST_OBJECT_LOCK (monitor);
    provider =
        GST_DEVICE_PROVIDER (gst_object_get_parent (GST_OBJECT (device)));
    if (is_provider_hidden (monitor, monitor->priv->hidden, provider)) {
      matches = FALSE;
    } else if (monitor->priv->filters->len) {
      guint i;

      for (i = 0; i < monitor->priv->filters->len; i++) {
        struct DeviceFilter *filter =
            g_ptr_array_index (monitor->priv->filters, i);
        GstCaps *caps;

        caps = gst_device_get_caps (device);
        matches = gst_caps_can_intersect (filter->caps, caps) &&
            gst_device_has_classesv (device, filter->classesv);
        gst_caps_unref (caps);
        if (matches)
          break;
      }
    } else {
      matches = TRUE;
    }
    GST_OBJECT_UNLOCK (monitor);

    gst_object_unref (provider);
    gst_object_unref (device);

    if (matches)
      gst_bus_post (monitor->priv->bus, gst_message_ref (message));
  }
}


static void
gst_device_monitor_init (GstDeviceMonitor * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GST_TYPE_DEVICE_MONITOR, GstDeviceMonitorPrivate);

  self->priv->show_all = DEFAULT_SHOW_ALL;

  self->priv->bus = gst_bus_new ();
  gst_bus_set_flushing (self->priv->bus, TRUE);

  self->priv->providers = g_ptr_array_new ();
  self->priv->filters = g_ptr_array_new_with_free_func (
      (GDestroyNotify) device_filter_free);

  self->priv->last_id = 1;
}


static void
gst_device_monitor_remove (GstDeviceMonitor * self, guint i)
{
  GstDeviceProvider *provider = g_ptr_array_index (self->priv->providers, i);
  GstBus *bus;

  g_ptr_array_remove_index (self->priv->providers, i);

  bus = gst_device_provider_get_bus (provider);
  g_signal_handlers_disconnect_by_func (bus, bus_sync_message, self);
  gst_object_unref (bus);

  gst_object_unref (provider);
}

static void
gst_device_monitor_dispose (GObject * object)
{
  GstDeviceMonitor *self = GST_DEVICE_MONITOR (object);

  g_return_if_fail (!self->priv->started);

  if (self->priv->providers) {
    while (self->priv->providers->len)
      gst_device_monitor_remove (self, self->priv->providers->len - 1);
    g_ptr_array_unref (self->priv->providers);
    self->priv->providers = NULL;
  }

  if (self->priv->filters) {
    g_ptr_array_unref (self->priv->filters);
    self->priv->filters = NULL;
  }

  gst_object_replace ((GstObject **) & self->priv->bus, NULL);

  G_OBJECT_CLASS (gst_device_monitor_parent_class)->dispose (object);
}

/**
 * gst_device_monitor_get_devices:
 * @monitor: A #GstDeviceProvider
 *
 * Gets a list of devices from all of the relevant monitors. This may actually
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
  GList *devices = NULL, *hidden = NULL;
  guint i;
  guint cookie;

  g_return_val_if_fail (GST_IS_DEVICE_MONITOR (monitor), NULL);

  GST_OBJECT_LOCK (monitor);

  if (monitor->priv->filters->len == 0) {
    GST_OBJECT_UNLOCK (monitor);
    GST_WARNING_OBJECT (monitor, "No filters have been set");
    return FALSE;
  }

  if (monitor->priv->providers->len == 0) {
    GST_OBJECT_UNLOCK (monitor);
    GST_WARNING_OBJECT (monitor, "No providers match the current filters");
    return FALSE;
  }

again:

  g_list_free_full (devices, gst_object_unref);
  g_list_free_full (hidden, g_free);
  devices = NULL;
  hidden = NULL;

  cookie = monitor->priv->cookie;

  for (i = 0; i < monitor->priv->providers->len; i++) {
    GList *tmpdev;
    GstDeviceProvider *provider =
        gst_object_ref (g_ptr_array_index (monitor->priv->providers, i));
    GList *item;

    if (!is_provider_hidden (monitor, hidden, provider)) {
      GST_OBJECT_UNLOCK (monitor);

      tmpdev = gst_device_provider_get_devices (provider);

      GST_OBJECT_LOCK (monitor);
      update_hidden_providers_list (&hidden, provider);
    } else {
      tmpdev = NULL;
    }


    for (item = tmpdev; item; item = item->next) {
      GstDevice *dev = GST_DEVICE (item->data);
      GstCaps *caps = gst_device_get_caps (dev);
      guint j;

      for (j = 0; j < monitor->priv->filters->len; j++) {
        struct DeviceFilter *filter =
            g_ptr_array_index (monitor->priv->filters, j);

        if (gst_caps_can_intersect (filter->caps, caps) &&
            gst_device_has_classesv (dev, filter->classesv)) {
          devices = g_list_prepend (devices, gst_object_ref (dev));
          break;
        }
      }
      gst_caps_unref (caps);
    }

    g_list_free_full (tmpdev, gst_object_unref);
    gst_object_unref (provider);

    if (monitor->priv->cookie != cookie)
      goto again;
  }
  g_list_free_full (hidden, g_free);

  GST_OBJECT_UNLOCK (monitor);

  return g_list_reverse (devices);
}

/**
 * gst_device_monitor_start:
 * @monitor: A #GstDeviceMonitor
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
gst_device_monitor_start (GstDeviceMonitor * monitor)
{
  guint cookie, i;
  GList *pending = NULL, *started = NULL, *removed = NULL;

  g_return_val_if_fail (GST_IS_DEVICE_MONITOR (monitor), FALSE);

  GST_OBJECT_LOCK (monitor);

  if (monitor->priv->filters->len == 0) {
    GST_OBJECT_UNLOCK (monitor);
    GST_WARNING_OBJECT (monitor, "No filters have been set, will expose all "
        "devices found");
    gst_device_monitor_add_filter (monitor, NULL, NULL);
    GST_OBJECT_LOCK (monitor);
  }

  if (monitor->priv->providers->len == 0) {
    GST_OBJECT_UNLOCK (monitor);
    GST_WARNING_OBJECT (monitor, "No providers match the current filters");
    return FALSE;
  }

  gst_bus_set_flushing (monitor->priv->bus, FALSE);

again:
  cookie = monitor->priv->cookie;

  g_list_free_full (pending, gst_object_unref);
  pending = NULL;
  removed = started;
  started = NULL;

  for (i = 0; i < monitor->priv->providers->len; i++) {
    GstDeviceProvider *provider;
    GList *find;

    provider = g_ptr_array_index (monitor->priv->providers, i);

    find = g_list_find (removed, provider);
    if (find) {
      /* this was already started, move to started list */
      removed = g_list_remove_link (removed, find);
      started = g_list_concat (started, find);
    } else {
      /* not started, add to pending list */
      pending = g_list_append (pending, gst_object_ref (provider));
    }
  }
  g_list_free_full (removed, gst_object_unref);
  removed = NULL;

  while (pending) {
    GstDeviceProvider *provider = pending->data;

    if (gst_device_provider_can_monitor (provider)) {
      GST_OBJECT_UNLOCK (monitor);

      if (!gst_device_provider_start (provider))
        goto start_failed;

      GST_OBJECT_LOCK (monitor);
    }
    started = g_list_prepend (started, provider);
    pending = g_list_delete_link (pending, pending);

    if (monitor->priv->cookie != cookie)
      goto again;
  }
  monitor->priv->started = TRUE;
  GST_OBJECT_UNLOCK (monitor);

  g_list_free_full (started, gst_object_unref);

  return TRUE;

start_failed:
  {
    GST_OBJECT_LOCK (monitor);
    gst_bus_set_flushing (monitor->priv->bus, TRUE);
    GST_OBJECT_UNLOCK (monitor);

    while (started) {
      GstDeviceProvider *provider = started->data;

      gst_device_provider_stop (provider);
      gst_object_unref (provider);

      started = g_list_delete_link (started, started);
    }
    return FALSE;
  }
}

/**
 * gst_device_monitor_stop:
 * @monitor: A #GstDeviceProvider
 *
 * Stops monitoring the devices.
 *
 * Since: 1.4
 */
void
gst_device_monitor_stop (GstDeviceMonitor * monitor)
{
  guint i;
  GList *started = NULL;

  g_return_if_fail (GST_IS_DEVICE_MONITOR (monitor));

  gst_bus_set_flushing (monitor->priv->bus, TRUE);

  GST_OBJECT_LOCK (monitor);
  for (i = 0; i < monitor->priv->providers->len; i++) {
    GstDeviceProvider *provider =
        g_ptr_array_index (monitor->priv->providers, i);

    started = g_list_prepend (started, gst_object_ref (provider));
  }
  GST_OBJECT_UNLOCK (monitor);

  while (started) {
    GstDeviceProvider *provider = started->data;

    if (gst_device_provider_can_monitor (provider))
      gst_device_provider_stop (provider);

    started = g_list_delete_link (started, started);
    gst_object_unref (provider);
  }

  GST_OBJECT_LOCK (monitor);
  monitor->priv->started = FALSE;
  GST_OBJECT_UNLOCK (monitor);

}

static void
provider_hidden (GstDeviceProvider * provider, const gchar * hidden,
    GstDeviceMonitor * monitor)
{
  GST_OBJECT_LOCK (monitor);
  monitor->priv->hidden =
      g_list_prepend (monitor->priv->hidden, g_strdup (hidden));
  GST_OBJECT_UNLOCK (monitor);
}

static void
provider_unhidden (GstDeviceProvider * provider, const gchar * hidden,
    GstDeviceMonitor * monitor)
{
  GList *find;

  GST_OBJECT_LOCK (monitor);
  find =
      g_list_find_custom (monitor->priv->hidden, hidden,
      (GCompareFunc) g_strcmp0);
  if (find) {
    g_free (find->data);
    monitor->priv->hidden = g_list_delete_link (monitor->priv->hidden, find);
  }
  GST_OBJECT_UNLOCK (monitor);
}

/**
 * gst_device_monitor_add_filter:
 * @monitor: a device monitor
 * @classes: (allow-none): device classes to use as filter or %NULL for any class
 * @caps: (allow-none): the #GstCaps to filter or %NULL for ANY
 *
 * Adds a filter for which #GstDevice will be monitored, any device that matches
 * all these classes and the #GstCaps will be returned.
 *
 * If this function is called multiple times to add more filters, each will be
 * matched independently. That is, adding more filters will not further restrict
 * what devices are matched.
 *
 * The #GstCaps supported by the device as returned by gst_device_get_caps() are
 * not intersected with caps filters added using this function.
 *
 * Filters must be added before the #GstDeviceMonitor is started.
 *
 * Returns: The id of the new filter or 0 if no provider matched the filter's
 *  classes.
 *
 * Since: 1.4
 */
guint
gst_device_monitor_add_filter (GstDeviceMonitor * monitor,
    const gchar * classes, GstCaps * caps)
{
  GList *factories = NULL;
  struct DeviceFilter *filter;
  guint id = 0;
  gboolean matched = FALSE;

  g_return_val_if_fail (GST_IS_DEVICE_MONITOR (monitor), 0);
  g_return_val_if_fail (!monitor->priv->started, 0);

  GST_OBJECT_LOCK (monitor);

  filter = g_slice_new0 (struct DeviceFilter);
  filter->id = monitor->priv->last_id++;
  if (caps)
    filter->caps = gst_caps_ref (caps);
  else
    filter->caps = gst_caps_new_any ();
  if (classes)
    filter->classesv = g_strsplit (classes, "/", 0);

  factories = gst_device_provider_factory_list_get_device_providers (1);

  while (factories) {
    GstDeviceProviderFactory *factory = factories->data;

    if (gst_device_provider_factory_has_classesv (factory, filter->classesv)) {
      GstDeviceProvider *provider;

      provider = gst_device_provider_factory_get (factory);

      if (provider) {
        guint i;

        for (i = 0; i < monitor->priv->providers->len; i++) {
          if (g_ptr_array_index (monitor->priv->providers, i) == provider) {
            gst_object_unref (provider);
            provider = NULL;
            matched = TRUE;
            break;
          }
        }
      }

      if (provider) {
        GstBus *bus = gst_device_provider_get_bus (provider);

        update_hidden_providers_list (&monitor->priv->hidden, provider);
        g_signal_connect (provider, "provider-hidden",
            (GCallback) provider_hidden, monitor);
        g_signal_connect (provider, "provider-unhidden",
            (GCallback) provider_unhidden, monitor);

        matched = TRUE;
        gst_bus_enable_sync_message_emission (bus);
        g_signal_connect (bus, "sync-message",
            G_CALLBACK (bus_sync_message), monitor);
        gst_object_unref (bus);
        g_ptr_array_add (monitor->priv->providers, provider);
        monitor->priv->cookie++;
      }
    }

    factories = g_list_remove (factories, factory);
    gst_object_unref (factory);
  }

  /* Ensure there is no leak here */
  g_assert (factories == NULL);

  if (matched)
    id = filter->id;
  g_ptr_array_add (monitor->priv->filters, filter);

  GST_OBJECT_UNLOCK (monitor);

  return id;
}

/**
 * gst_device_monitor_remove_filter:
 * @monitor: a device monitor
 * @filter_id: the id of the filter
 *
 * Removes a filter from the #GstDeviceMonitor using the id that was returned
 * by gst_device_monitor_add_filter().
 *
 * Returns: %TRUE of the filter id was valid, %FALSE otherwise
 *
 * Since: 1.4
 */
gboolean
gst_device_monitor_remove_filter (GstDeviceMonitor * monitor, guint filter_id)
{
  guint i, j;
  gboolean removed = FALSE;

  g_return_val_if_fail (GST_IS_DEVICE_MONITOR (monitor), FALSE);
  g_return_val_if_fail (!monitor->priv->started, FALSE);
  g_return_val_if_fail (filter_id > 0, FALSE);

  GST_OBJECT_LOCK (monitor);
  for (i = 0; i < monitor->priv->filters->len; i++) {
    struct DeviceFilter *filter = g_ptr_array_index (monitor->priv->filters, i);

    if (filter->id == filter_id) {
      g_ptr_array_remove_index (monitor->priv->filters, i);
      removed = TRUE;
      break;
    }
  }

  if (removed) {
    for (i = 0; i < monitor->priv->providers->len; i++) {
      GstDeviceProvider *provider =
          g_ptr_array_index (monitor->priv->providers, i);
      GstDeviceProviderFactory *factory =
          gst_device_provider_get_factory (provider);
      gboolean valid = FALSE;

      for (j = 0; j < monitor->priv->filters->len; j++) {
        struct DeviceFilter *filter =
            g_ptr_array_index (monitor->priv->filters, j);

        if (gst_device_provider_factory_has_classesv (factory,
                filter->classesv)) {
          valid = TRUE;
          break;
        }
      }

      if (!valid) {
        monitor->priv->cookie++;
        gst_device_monitor_remove (monitor, i);
        i--;
      }
    }
  }

  GST_OBJECT_UNLOCK (monitor);

  return removed;
}



/**
 * gst_device_monitor_new:
 *
 * Create a new #GstDeviceMonitor
 *
 * Returns: (transfer full): a new device monitor.
 *
 * Since: 1.4
 */
GstDeviceMonitor *
gst_device_monitor_new (void)
{
  return g_object_new (GST_TYPE_DEVICE_MONITOR, NULL);
}

/**
 * gst_device_monitor_get_bus:
 * @monitor: a #GstDeviceProvider
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
 * gst_device_monitor_get_providers:
 * @monitor: a #GstDeviceMonitor
 *
 * Get a list of the currently selected device provider factories.
 *
 * This
 *
 * Returns: (transfer full) (array zero-terminated=1) (element-type gchar*):
 *     A list of device provider factory names that are currently being
 *     monitored by @monitor or %NULL when nothing is being monitored.
 *
 * Since: 1.6
 */
gchar **
gst_device_monitor_get_providers (GstDeviceMonitor * monitor)
{
  guint i, len;
  gchar **res = NULL;

  g_return_val_if_fail (GST_IS_DEVICE_MONITOR (monitor), NULL);

  GST_OBJECT_LOCK (monitor);
  len = monitor->priv->providers->len;
  if (len == 0)
    goto done;

  res = g_new (gchar *, len + 1);

  for (i = 0; i < len; i++) {
    GstDeviceProvider *provider =
        g_ptr_array_index (monitor->priv->providers, i);
    GstDeviceProviderFactory *factory =
        gst_device_provider_get_factory (provider);

    res[i] = g_strdup (GST_OBJECT_NAME (factory));
  }
  res[i] = NULL;

done:
  GST_OBJECT_UNLOCK (monitor);

  return res;
}

/**
 * gst_device_monitor_set_show_all_devices:
 * @monitor: a #GstDeviceMonitor
 * @show_all: show all devices
 *
 * Set if all devices should be visible, even those devices from hidden
 * providers. Setting @show_all to true might show some devices multiple times.
 *
 * Since: 1.6
 */
void
gst_device_monitor_set_show_all_devices (GstDeviceMonitor * monitor,
    gboolean show_all)
{
  g_return_if_fail (GST_IS_DEVICE_MONITOR (monitor));

  GST_OBJECT_LOCK (monitor);
  monitor->priv->show_all = show_all;
  GST_OBJECT_UNLOCK (monitor);
}

/**
 * gst_device_monitor_get_show_all_devices:
 * @monitor: a #GstDeviceMonitor
 *
 * Get if @monitor is curretly showing all devices, even those from hidden
 * providers.
 *
 * Returns: %TRUE when all devices will be shown.
 *
 * Since: 1.6
 */
gboolean
gst_device_monitor_get_show_all_devices (GstDeviceMonitor * monitor)
{
  gboolean res;

  g_return_val_if_fail (GST_IS_DEVICE_MONITOR (monitor), FALSE);

  GST_OBJECT_LOCK (monitor);
  res = monitor->priv->show_all;
  GST_OBJECT_UNLOCK (monitor);

  return res;
}
