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

GST_DEBUG_CATEGORY_STATIC (devicemonitor_debug);
#define GST_CAT_DEFAULT devicemonitor_debug

struct _GstDeviceMonitorPrivate
{
  gboolean started;
  GThread *start_thread;

  GstBus *bus;

  GPtrArray *providers;
  GPtrArray *filters;

  GSList *started_providers;

  guint last_id;
  GList *hidden;
  gboolean show_all;
};

#define DEFAULT_SHOW_ALL        FALSE

enum
{
  PROP_SHOW_ALL = 1,
};

G_DEFINE_TYPE_WITH_PRIVATE (GstDeviceMonitor, gst_device_monitor,
    GST_TYPE_OBJECT);

static void gst_device_monitor_dispose (GObject * object);

static guint gst_device_monitor_add_filter_unlocked (GstDeviceMonitor * monitor,
    const gchar * classes, GstCaps * caps);

static void
provider_hidden (GstDeviceProvider * provider, const gchar * hidden,
    GstDeviceMonitor * monitor);

static void
provider_unhidden (GstDeviceProvider * provider, const gchar * hidden,
    GstDeviceMonitor * monitor);

struct DeviceFilter
{
  guint id;

  gchar **classesv;
  GstCaps *caps;
};

static struct DeviceFilter *
device_filter_copy (struct DeviceFilter *filter)
{
  struct DeviceFilter *copy = g_new0 (struct DeviceFilter, 1);

  copy->classesv = g_strdupv (filter->classesv);
  copy->caps = filter->caps ? gst_caps_ref (filter->caps) : NULL;

  return copy;
}

static void
device_filter_free (struct DeviceFilter *filter)
{
  g_strfreev (filter->classesv);

  if (filter->caps)
    gst_caps_unref (filter->caps);

  g_free (filter);
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

  object_class->get_property = gst_device_monitor_get_property;
  object_class->set_property = gst_device_monitor_set_property;
  object_class->dispose = gst_device_monitor_dispose;

  GST_DEBUG_CATEGORY_INIT (devicemonitor_debug, "devicemonitor", 0,
      "debugging info for the device monitor");
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

static GstBusSyncReply
bus_sync_message (GstBus * bus, GstMessage * message,
    GstDeviceMonitor * monitor)
{
  GstMessageType type = GST_MESSAGE_TYPE (message);

  if (type == GST_MESSAGE_DEVICE_ADDED || type == GST_MESSAGE_DEVICE_REMOVED ||
      type == GST_MESSAGE_DEVICE_CHANGED) {
    gboolean matches = TRUE;
    GstDevice *device;
    GstDeviceProvider *provider;

    if (type == GST_MESSAGE_DEVICE_ADDED)
      gst_message_parse_device_added (message, &device);
    else if (type == GST_MESSAGE_DEVICE_REMOVED)
      gst_message_parse_device_removed (message, &device);
    else
      gst_message_parse_device_changed (message, &device, NULL);

    GST_OBJECT_LOCK (monitor);
    provider = GST_DEVICE_PROVIDER (GST_MESSAGE_SRC (message));
    if (provider &&
        is_provider_hidden (monitor, monitor->priv->hidden, provider)) {
      matches = FALSE;
    } else {
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
    }
    GST_OBJECT_UNLOCK (monitor);

    gst_object_unref (device);

    if (matches)
      gst_bus_post (monitor->priv->bus, gst_message_ref (message));
  }

  gst_message_unref (message);

  return GST_BUS_DROP;
}


static void
gst_device_monitor_init (GstDeviceMonitor * self)
{
  self->priv = gst_device_monitor_get_instance_private (self);

  self->priv->show_all = DEFAULT_SHOW_ALL;

  self->priv->bus = gst_bus_new ();
  gst_bus_set_flushing (self->priv->bus, TRUE);

  self->priv->providers = g_ptr_array_new ();
  self->priv->filters = g_ptr_array_new_with_free_func (
      (GDestroyNotify) device_filter_free);
  self->priv->started_providers = NULL;
  self->priv->started = FALSE;
  self->priv->start_thread = NULL;

  self->priv->last_id = 1;
}


static void
gst_device_monitor_remove_provider (GstDeviceMonitor * self, guint i)
{
  GstDeviceProvider *provider = g_ptr_array_index (self->priv->providers, i);
  GstBus *bus;

  g_ptr_array_remove_index (self->priv->providers, i);

  bus = gst_device_provider_get_bus (provider);
  gst_bus_set_sync_handler (bus, NULL, NULL, NULL);
  gst_object_unref (bus);

  g_signal_handlers_disconnect_by_func (provider, provider_hidden, self);
  g_signal_handlers_disconnect_by_func (provider, provider_unhidden, self);

  gst_object_unref (provider);
}

static void
gst_device_monitor_dispose (GObject * object)
{
  GstDeviceMonitor *self = GST_DEVICE_MONITOR (object);

  if (self->priv->started) {
    g_critical ("gst_device_monitor_dispose: disposed without stopping, "
        "started providers were leaked");
  }

  /*
   * We ensure that dispose is never called while we still hold a reference to
   * the GThread running monitor_thread_func() by having the func hold
   * a reference to the monitor. The func will release the monitor's reference
   * to the GThread before releasing its own reference to the monitor.
   *
   * When stopping, _stop() will steal the monitor's reference to the GThread
   * and join it, achieving the same result.
   */
  g_warn_if_fail (!self->priv->start_thread);

  if (self->priv->providers) {
    while (self->priv->providers->len)
      gst_device_monitor_remove_provider (self, self->priv->providers->len - 1);
    g_ptr_array_unref (self->priv->providers);
    self->priv->providers = NULL;
  }

  if (self->priv->filters) {
    g_ptr_array_unref (self->priv->filters);
    self->priv->filters = NULL;
  }

  if (self->priv->hidden) {
    g_list_free_full (self->priv->hidden, g_free);
    self->priv->hidden = NULL;
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
 * Returns: (transfer full) (element-type GstDevice) (nullable): a #GList of
 *   #GstDevice
 *
 * Since: 1.4
 */

GList *
gst_device_monitor_get_devices (GstDeviceMonitor * monitor)
{
  GQueue providers = G_QUEUE_INIT, filters = G_QUEUE_INIT;
  GList *hidden = NULL;
  GQueue devices = G_QUEUE_INIT;
  GList *l;
  guint i;

  g_return_val_if_fail (GST_IS_DEVICE_MONITOR (monitor), NULL);

  GST_OBJECT_LOCK (monitor);

  if (monitor->priv->filters->len == 0) {
    GST_OBJECT_UNLOCK (monitor);
    GST_WARNING_OBJECT (monitor, "No filters have been set");
    return NULL;
  }

  if (monitor->priv->providers->len == 0) {
    GST_OBJECT_UNLOCK (monitor);
    GST_WARNING_OBJECT (monitor, "No providers match the current filters");
    return NULL;
  }

  for (i = 0; i < monitor->priv->providers->len; i++) {
    GstDeviceProvider *provider =
        g_ptr_array_index (monitor->priv->providers, i);

    update_hidden_providers_list (&hidden, provider);
  }

  /* Create a copy of all current providers and filters while keeping the lock
   * and afterwards unlock and work with this snapshot */
  for (i = 0; i < monitor->priv->providers->len; i++) {
    GstDeviceProvider *provider =
        g_ptr_array_index (monitor->priv->providers, i);

    if (!is_provider_hidden (monitor, hidden, provider)) {
      g_queue_push_tail (&providers, gst_object_ref (provider));
    }
  }

  for (i = 0; i < monitor->priv->filters->len; i++) {
    struct DeviceFilter *filter = g_ptr_array_index (monitor->priv->filters, i);

    g_queue_push_tail (&filters, device_filter_copy (filter));
  }
  GST_OBJECT_UNLOCK (monitor);

  for (l = providers.head; l; l = l->next) {
    GstDeviceProvider *provider = l->data;
    GList *tmpdev, *item, *filter_item;

    tmpdev = gst_device_provider_get_devices (provider);

    for (item = tmpdev; item; item = item->next) {
      GstDevice *dev = GST_DEVICE (item->data);
      GstCaps *caps = gst_device_get_caps (dev);

      for (filter_item = filters.head; filter_item;
          filter_item = filter_item->next) {
        struct DeviceFilter *filter = filter_item->data;

        if (gst_caps_can_intersect (filter->caps, caps) &&
            gst_device_has_classesv (dev, filter->classesv)) {
          g_queue_push_tail (&devices, gst_object_ref (dev));
          break;
        }
      }
      gst_caps_unref (caps);
    }

    g_list_free_full (tmpdev, gst_object_unref);
  }
  g_list_free_full (hidden, g_free);

  g_queue_clear_full (&providers, (GDestroyNotify) gst_object_unref);
  g_queue_clear_full (&filters, (GDestroyNotify) device_filter_free);

  return devices.head;
}

static gpointer
monitor_thread_func (gpointer data)
{
  GSList *started = NULL;
  GQueue pending = G_QUEUE_INIT;
  GstDeviceMonitor *monitor = data;
  GstDeviceProvider *provider;

  GST_OBJECT_LOCK (monitor);

  if (!monitor->priv->started) {
    GST_OBJECT_UNLOCK (monitor);
    goto done;
  }

  for (int i = 0; i < monitor->priv->providers->len; i++) {
    provider = g_ptr_array_index (monitor->priv->providers, i);
    g_queue_push_tail (&pending, gst_object_ref (provider));
  }

  while ((provider = g_queue_pop_head (&pending))) {
    GST_OBJECT_UNLOCK (monitor);

    if (gst_device_provider_start (provider)) {
      started = g_slist_prepend (started, provider);
    } else {
      gst_clear_object (&provider);
    }

    GST_OBJECT_LOCK (monitor);

    /* We were stopped while unlocked in this iteration. Stop the provider
     * we just started, dispose all pending providers, and return without
     * modifying any priv members */
    if (!monitor->priv->started) {
      /* Assert that the monitor lost its reference to this GThread when it
       * was stopped */
      g_assert (monitor->priv->start_thread == NULL);
      GST_OBJECT_UNLOCK (monitor);

      if (provider) {
        gst_device_provider_stop (provider);
        gst_clear_object (&provider);
      }

      g_queue_clear_full (&pending, gst_object_unref);

      /* Free the list we prepended above by detaching it from the rest of the
       * list, which is monitor->priv->started_providers. This is safe because
       * this is a singly-linked list. */
      started->next = NULL;
      g_slist_free (started);
      started = NULL;

      goto done;
    }

    if (started) {
      monitor->priv->started_providers = started;
    }
  }

  if (monitor->priv->started_providers == NULL) {
    monitor->priv->started = FALSE;
  }

  g_clear_pointer (&monitor->priv->start_thread, g_thread_unref);
  GST_OBJECT_UNLOCK (monitor);

done:
  gst_bus_post (monitor->priv->bus,
      gst_message_new_device_monitor_started (GST_OBJECT (monitor),
          started != NULL));
  gst_object_unref (monitor);

  return NULL;
}

/**
 * gst_device_monitor_start:
 * @monitor: A #GstDeviceMonitor
 *
 * Starts monitoring the devices, once this has succeeded, the
 * %GST_MESSAGE_DEVICE_ADDED and %GST_MESSAGE_DEVICE_REMOVED messages
 * will be emitted on the bus when the list of devices changes.
 *
 * Since 1.28, device providers are started asynchronously and
 * %GST_MESSAGE_DEVICE_MONITOR_STARTED will be emitted once the initial list
 * of devices has been populated, signalling that monitor startup has
 * completed.
 *
 * The monitor will hold a strong reference to itself while it is populating
 * devices asynchronously, so you must call gst_device_monitor_stop() before
 * unreffing if you want monitoring to stop immediately.
 *
 * Returns: %TRUE if the device monitoring could be started, i.e. at least a
 *     single device provider was started successfully.
 *
 * Since: 1.4
 */

gboolean
gst_device_monitor_start (GstDeviceMonitor * monitor)
{
  gboolean ret;

  g_return_val_if_fail (GST_IS_DEVICE_MONITOR (monitor), FALSE);

  GST_OBJECT_LOCK (monitor);

  if (monitor->priv->started) {
    GST_OBJECT_UNLOCK (monitor);
    GST_DEBUG_OBJECT (monitor, "Monitor started already");
    return TRUE;
  }

  /* This is cleared alongside priv->started being set to FALSE */
  g_assert (monitor->priv->started_providers == NULL);

  if (monitor->priv->filters->len == 0) {
    GST_WARNING_OBJECT (monitor, "No filters have been set, will expose all "
        "devices found");
    gst_device_monitor_add_filter_unlocked (monitor, NULL, NULL);
  }

  if (monitor->priv->providers->len == 0) {
    GST_OBJECT_UNLOCK (monitor);
    GST_WARNING_OBJECT (monitor, "No providers match the current filters");
    return FALSE;
  }

  monitor->priv->start_thread = g_thread_try_new ("gst_device_monitor_start",
      monitor_thread_func, monitor, NULL);

  if ((ret = monitor->priv->start_thread != NULL)) {
    // Yield a reference to the GThreadFunc
    gst_object_ref (monitor);

    monitor->priv->started = TRUE;
    gst_bus_set_flushing (monitor->priv->bus, FALSE);
  }

  GST_OBJECT_UNLOCK (monitor);

  return ret;
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
  GSList *started = NULL;
  GThread *thread = NULL;

  g_return_if_fail (GST_IS_DEVICE_MONITOR (monitor));

  gst_bus_set_flushing (monitor->priv->bus, TRUE);

  GST_OBJECT_LOCK (monitor);
  if (!monitor->priv->started) {
    GST_DEBUG_OBJECT (monitor, "Monitor was not started yet");
    GST_OBJECT_UNLOCK (monitor);
    return;
  }

  started = monitor->priv->started_providers;
  monitor->priv->started_providers = NULL;
  /* This will cancel monitor_thread_func() if it is running */
  monitor->priv->started = FALSE;
  /* Steal GThread reference from the monitor */
  thread = monitor->priv->start_thread;
  monitor->priv->start_thread = NULL;
  GST_OBJECT_UNLOCK (monitor);

  if (thread != NULL) {
    /* Thread has been cancelled above, so this join should return quickly */
    g_thread_join (thread);
  }

  while (started) {
    GstDeviceProvider *provider = started->data;

    gst_device_provider_stop (provider);

    started = g_slist_delete_link (started, started);
    gst_object_unref (provider);
  }
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
  guint id;

  g_return_val_if_fail (GST_IS_DEVICE_MONITOR (monitor), 0);
  g_return_val_if_fail (!monitor->priv->started, 0);

  GST_OBJECT_LOCK (monitor);
  id = gst_device_monitor_add_filter_unlocked (monitor, classes, caps);
  GST_OBJECT_UNLOCK (monitor);

  return id;
}

static guint
gst_device_monitor_add_filter_unlocked (GstDeviceMonitor * monitor,
    const gchar * classes, GstCaps * caps)
{
  GList *factories = NULL;
  struct DeviceFilter *filter;
  guint id = 0;
  gboolean matched = FALSE;

  filter = g_new0 (struct DeviceFilter, 1);
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
        gst_bus_set_sync_handler (bus, (GstBusSyncHandler) bus_sync_message,
            monitor, NULL);
        gst_object_unref (bus);
        g_ptr_array_add (monitor->priv->providers, provider);
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
        gst_device_monitor_remove_provider (monitor, i);
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
  GstDeviceMonitor *monitor;

  monitor = g_object_new (GST_TYPE_DEVICE_MONITOR, NULL);

  /* Clear floating flag */
  gst_object_ref_sink (monitor);

  return monitor;
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
 * Get if @monitor is currently showing all devices, even those from hidden
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
