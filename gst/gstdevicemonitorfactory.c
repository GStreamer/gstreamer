/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * gstdevicemonitorfactory.c: GstDeviceMonitorFactory object, support routines
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
 * SECTION:gstdevicemonitorfactory
 * @short_description: Create GstDeviceMonitors from a factory
 * @see_also: #GstDeviceMonitor, #GstPlugin, #GstPluginFeature, #GstPadTemplate.
 *
 * #GstDeviceMonitorFactory is used to create instances of device monitors. A
 * GstDeviceMonitorfactory can be added to a #GstPlugin as it is also a
 * #GstPluginFeature.
 *
 * Use the gst_device_monitor_factory_find() and gst_device_monitor_factory_create()
 * functions to create device monitor instances or use gst_device_monitor_factory_make() as a
 * convenient shortcut.
 *
 * Since: 1.4
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gst_private.h"

#include "gstdevicemonitorfactory.h"
#include "gst.h"

#include "glib-compat-private.h"

GST_DEBUG_CATEGORY_STATIC (device_monitor_factory_debug);
#define GST_CAT_DEFAULT device_monitor_factory_debug

static void gst_device_monitor_factory_finalize (GObject * object);
static void gst_device_monitor_factory_cleanup (GstDeviceMonitorFactory *
    factory);

/* static guint gst_device_monitor_factory_signals[LAST_SIGNAL] = { 0 }; */

/* this is defined in gstelement.c */
extern GQuark __gst_devicemonitorclass_factory;

#define _do_init \
{ \
  GST_DEBUG_CATEGORY_INIT (device_monitor_factory_debug, "GST_DEVICE_MONITOR_FACTORY", \
      GST_DEBUG_BOLD | GST_DEBUG_FG_WHITE | GST_DEBUG_BG_RED, \
      "device monitor factories keep information about installed device monitors"); \
}

G_DEFINE_TYPE_WITH_CODE (GstDeviceMonitorFactory, gst_device_monitor_factory,
    GST_TYPE_PLUGIN_FEATURE, _do_init);

static void
gst_device_monitor_factory_class_init (GstDeviceMonitorFactoryClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = gst_device_monitor_factory_finalize;
}

static void
gst_device_monitor_factory_init (GstDeviceMonitorFactory * factory)
{
}

static void
gst_device_monitor_factory_finalize (GObject * object)
{
  GstDeviceMonitorFactory *factory = GST_DEVICE_MONITOR_FACTORY (object);
  GstDeviceMonitor *monitor;

  gst_device_monitor_factory_cleanup (factory);

  monitor = g_atomic_pointer_get (&factory->monitor);
  if (monitor)
    gst_object_unref (monitor);

  G_OBJECT_CLASS (gst_device_monitor_factory_parent_class)->finalize (object);
}

/**
 * gst_device_monitor_factory_find:
 * @name: name of factory to find
 *
 * Search for an device monitor factory of the given name. Refs the returned
 * device monitor factory; caller is responsible for unreffing.
 *
 * Returns: (transfer full): #GstDeviceMonitorFactory if found, NULL otherwise
 *
 * Since: 1.4
 */
GstDeviceMonitorFactory *
gst_device_monitor_factory_find (const gchar * name)
{
  GstPluginFeature *feature;

  g_return_val_if_fail (name != NULL, NULL);

  feature = gst_registry_find_feature (gst_registry_get (), name,
      GST_TYPE_DEVICE_MONITOR_FACTORY);
  if (feature)
    return GST_DEVICE_MONITOR_FACTORY (feature);

  /* this isn't an error, for instance when you query if an device monitor factory is
   * present */
  GST_LOG ("no such device monitor factory \"%s\"", name);

  return NULL;
}

static void
gst_device_monitor_factory_cleanup (GstDeviceMonitorFactory * factory)
{
  if (factory->metadata) {
    gst_structure_free ((GstStructure *) factory->metadata);
    factory->metadata = NULL;
  }
  if (factory->type) {
    factory->type = G_TYPE_INVALID;
  }
}

#define CHECK_METADATA_FIELD(klass, name, key)                                 \
  G_STMT_START {                                                               \
    const gchar *metafield = gst_device_monitor_class_get_metadata (klass, key);      \
    if (G_UNLIKELY (metafield == NULL || *metafield == '\0')) {                \
      g_warning ("Device monitor factory metadata for '%s' has no valid %s field", name, key);    \
      goto detailserror;                                                       \
    } \
  } G_STMT_END;

/**
 * gst_device_monitor_register:
 * @plugin: (allow-none): #GstPlugin to register the device monitor with, or NULL for
 *     a static device monitor.
 * @name: name of device monitors of this type
 * @rank: rank of device monitor (higher rank means more importance when autoplugging)
 * @type: GType of device monitor to register
 *
 * Create a new device monitorfactory capable of instantiating objects of the
 * @type and add the factory to @plugin.
 *
 * Returns: TRUE, if the registering succeeded, FALSE on error
 *
 * Since: 1.4
 */
gboolean
gst_device_monitor_register (GstPlugin * plugin, const gchar * name, guint rank,
    GType type)
{
  GstPluginFeature *existing_feature;
  GstRegistry *registry;
  GstDeviceMonitorFactory *factory;
  GstDeviceMonitorClass *klass;

  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (g_type_is_a (type, GST_TYPE_DEVICE_MONITOR), FALSE);

  registry = gst_registry_get ();

  /* check if feature already exists, if it exists there is no need to update it
   * when the registry is getting updated, outdated plugins and all their
   * features are removed and readded.
   */
  existing_feature = gst_registry_lookup_feature (registry, name);
  if (existing_feature) {
    GST_DEBUG_OBJECT (registry, "update existing feature %p (%s)",
        existing_feature, name);
    factory = GST_DEVICE_MONITOR_FACTORY_CAST (existing_feature);
    factory->type = type;
    existing_feature->loaded = TRUE;
    g_type_set_qdata (type, __gst_devicemonitorclass_factory, factory);
    gst_object_unref (existing_feature);
    return TRUE;
  }

  factory =
      GST_DEVICE_MONITOR_FACTORY_CAST (g_object_newv
      (GST_TYPE_DEVICE_MONITOR_FACTORY, 0, NULL));
  gst_plugin_feature_set_name (GST_PLUGIN_FEATURE_CAST (factory), name);
  GST_LOG_OBJECT (factory, "Created new device monitorfactory for type %s",
      g_type_name (type));

  /* provide info needed during class structure setup */
  g_type_set_qdata (type, __gst_devicemonitorclass_factory, factory);
  klass = GST_DEVICE_MONITOR_CLASS (g_type_class_ref (type));

  CHECK_METADATA_FIELD (klass, name, GST_ELEMENT_METADATA_LONGNAME);
  CHECK_METADATA_FIELD (klass, name, GST_ELEMENT_METADATA_KLASS);
  CHECK_METADATA_FIELD (klass, name, GST_ELEMENT_METADATA_DESCRIPTION);
  CHECK_METADATA_FIELD (klass, name, GST_ELEMENT_METADATA_AUTHOR);

  factory->type = type;
  factory->metadata = gst_structure_copy ((GstStructure *) klass->metadata);

  if (plugin && plugin->desc.name) {
    GST_PLUGIN_FEATURE_CAST (factory)->plugin_name = plugin->desc.name;
    GST_PLUGIN_FEATURE_CAST (factory)->plugin = plugin;
    g_object_add_weak_pointer ((GObject *) plugin,
        (gpointer *) & GST_PLUGIN_FEATURE_CAST (factory)->plugin);
  } else {
    GST_PLUGIN_FEATURE_CAST (factory)->plugin_name = "NULL";
    GST_PLUGIN_FEATURE_CAST (factory)->plugin = NULL;
  }
  gst_plugin_feature_set_rank (GST_PLUGIN_FEATURE_CAST (factory), rank);
  GST_PLUGIN_FEATURE_CAST (factory)->loaded = TRUE;

  gst_registry_add_feature (registry, GST_PLUGIN_FEATURE_CAST (factory));

  return TRUE;

  /* ERRORS */
detailserror:
  {
    gst_device_monitor_factory_cleanup (factory);
    return FALSE;
  }
}

/**
 * gst_device_monitor_factory_get:
 * @factory: factory to instantiate
 *
 * Returns the device monitor of the type defined by the given device
 * monitorfactory.
 *
 * Returns: (transfer full): the #GstDeviceMonitor or NULL if the
 *     device monitor couldn't be created
 *
 * Since: 1.4
 */
GstDeviceMonitor *
gst_device_monitor_factory_get (GstDeviceMonitorFactory * factory)
{
  GstDeviceMonitor *device_monitor;
  GstDeviceMonitorClass *oclass;
  GstDeviceMonitorFactory *newfactory;

  g_return_val_if_fail (factory != NULL, NULL);

  newfactory =
      GST_DEVICE_MONITOR_FACTORY (gst_plugin_feature_load (GST_PLUGIN_FEATURE
          (factory)));

  if (newfactory == NULL)
    goto load_failed;

  factory = newfactory;

  GST_INFO ("getting device monitor \"%s\"", GST_OBJECT_NAME (factory));

  if (factory->type == 0)
    goto no_type;

  device_monitor = g_atomic_pointer_get (&newfactory->monitor);
  if (device_monitor)
    return gst_object_ref (device_monitor);

  /* create an instance of the device monitor, cast so we don't assert on NULL
   * also set name as early as we can
   */
  device_monitor = GST_DEVICE_MONITOR_CAST (g_object_newv (factory->type, 0,
          NULL));
  if (G_UNLIKELY (device_monitor == NULL))
    goto no_device_monitor;

  /* fill in the pointer to the factory in the device monitor class. The
   * class will not be unreffed currently.
   * Be thread safe as there might be 2 threads creating the first instance of
   * an device monitor at the same moment
   */
  oclass = GST_DEVICE_MONITOR_GET_CLASS (device_monitor);
  if (!g_atomic_pointer_compare_and_exchange (&oclass->factory, NULL, factory))
    gst_object_unref (factory);

  gst_object_ref_sink (device_monitor);

  /* We use an atomic to make sure we don't create two in parallel */
  if (!g_atomic_pointer_compare_and_exchange (&newfactory->monitor, NULL,
          device_monitor)) {
    gst_object_unref (device_monitor);

    device_monitor = g_atomic_pointer_get (&newfactory->monitor);
  }

  GST_DEBUG ("created device monitor \"%s\"", GST_OBJECT_NAME (factory));

  return gst_object_ref (device_monitor);

  /* ERRORS */
load_failed:
  {
    GST_WARNING_OBJECT (factory,
        "loading plugin containing feature %s returned NULL!",
        GST_OBJECT_NAME (factory));
    return NULL;
  }
no_type:
  {
    GST_WARNING_OBJECT (factory, "factory has no type");
    gst_object_unref (factory);
    return NULL;
  }
no_device_monitor:
  {
    GST_WARNING_OBJECT (factory, "could not create device monitor");
    gst_object_unref (factory);
    return NULL;
  }
}

/**
 * gst_device_monitor_factory_get_by_name:
 * @factoryname: a named factory to instantiate
 *
 * Returns the device monitor of the type defined by the given device
 * monitor factory.
 *
 * Returns: (transfer full): a #GstDeviceMonitor or NULL if unable to
 * create device monitor
 *
 * Since: 1.4
 */
GstDeviceMonitor *
gst_device_monitor_factory_get_by_name (const gchar * factoryname)
{
  GstDeviceMonitorFactory *factory;
  GstDeviceMonitor *device_monitor;

  g_return_val_if_fail (factoryname != NULL, NULL);
  g_return_val_if_fail (gst_is_initialized (), NULL);

  GST_LOG ("gstdevicemonitorfactory: get_by_name \"%s\"", factoryname);

  factory = gst_device_monitor_factory_find (factoryname);
  if (factory == NULL)
    goto no_factory;

  GST_LOG_OBJECT (factory, "found factory %p", factory);
  device_monitor = gst_device_monitor_factory_get (factory);
  if (device_monitor == NULL)
    goto create_failed;

  gst_object_unref (factory);
  return device_monitor;

  /* ERRORS */
no_factory:
  {
    GST_INFO ("no such device monitor factory \"%s\"!", factoryname);
    return NULL;
  }
create_failed:
  {
    GST_INFO_OBJECT (factory, "couldn't create instance!");
    gst_object_unref (factory);
    return NULL;
  }
}

/**
 * gst_device_monitor_factory_get_device_monitor_type:
 * @factory: factory to get managed #GType from
 *
 * Get the #GType for device monitors managed by this factory. The type can
 * only be retrieved if the device monitor factory is loaded, which can be
 * assured with gst_plugin_feature_load().
 *
 * Returns: the #GType for device monitors managed by this factory or 0 if
 * the factory is not loaded.
 *
 * Since: 1.4
 */
GType
gst_device_monitor_factory_get_device_monitor_type (GstDeviceMonitorFactory *
    factory)
{
  g_return_val_if_fail (GST_IS_DEVICE_MONITOR_FACTORY (factory), 0);

  return factory->type;
}

/**
 * gst_device_monitor_factory_get_metadata:
 * @factory: a #GstDeviceMonitorFactory
 * @key: a key
 *
 * Get the metadata on @factory with @key.
 *
 * Returns: the metadata with @key on @factory or %NULL when there was no
 * metadata with the given @key.
 *
 * Since: 1.4
 */
const gchar *
gst_device_monitor_factory_get_metadata (GstDeviceMonitorFactory * factory,
    const gchar * key)
{
  return gst_structure_get_string ((GstStructure *) factory->metadata, key);
}

/**
 * gst_device_monitor_factory_get_metadata_keys:
 * @factory: a #GstDeviceMonitorFactory
 *
 * Get the available keys for the metadata on @factory.
 *
 * Returns: (transfer full) (element-type utf8) (array zero-terminated=1):
 * a %NULL-terminated array of key strings, or %NULL when there is no
 * metadata. Free with g_strfreev() when no longer needed.
 *
 * Since: 1.4
 */
gchar **
gst_device_monitor_factory_get_metadata_keys (GstDeviceMonitorFactory * factory)
{
  GstStructure *metadata;
  gchar **arr;
  gint i, num;

  g_return_val_if_fail (GST_IS_DEVICE_MONITOR_FACTORY (factory), NULL);

  metadata = (GstStructure *) factory->metadata;
  if (metadata == NULL)
    return NULL;

  num = gst_structure_n_fields (metadata);
  if (num == 0)
    return NULL;

  arr = g_new (gchar *, num + 1);
  for (i = 0; i < num; ++i) {
    arr[i] = g_strdup (gst_structure_nth_field_name (metadata, i));
  }
  arr[i] = NULL;
  return arr;
}

typedef struct
{
  GstDeviceMonitorFactoryListType type;
  GstRank minrank;
} FilterData;


/**
 * gst_device_monitor_factory_list_is_type:
 * @factory: a #GstDeviceMonitorFactory
 * @type: a #GstDeviceMonitorFactoryListType
 *
 * Check if @factory is of the given types.
 *
 * Returns: %TRUE if @factory is of @type.
 *
 * Since: 1.4
 */
gboolean
gst_device_monitor_factory_list_is_type (GstDeviceMonitorFactory * factory,
    GstDeviceMonitorFactoryListType type)
{
  gboolean res = FALSE;
  const gchar *klass;

  klass = gst_device_monitor_factory_get_metadata (factory,
      GST_ELEMENT_METADATA_KLASS);

  if (klass == NULL) {
    GST_ERROR_OBJECT (factory,
        "device monitor factory is missing klass identifiers");
    return res;
  }

  /* Filter by device monitor type first, as soon as it matches
   * one type, we skip all other tests */
  if (!res && (type & GST_DEVICE_MONITOR_FACTORY_TYPE_SINK))
    res = (strstr (klass, "Sink") != NULL);

  if (!res && (type & GST_DEVICE_MONITOR_FACTORY_TYPE_SRC))
    res = (strstr (klass, "Source") != NULL);

  /* Filter by media type now, we only test if it
   * matched any of the types above. */
  if (res
      && (type & (GST_DEVICE_MONITOR_FACTORY_TYPE_MEDIA_AUDIO |
              GST_DEVICE_MONITOR_FACTORY_TYPE_MEDIA_VIDEO |
              GST_DEVICE_MONITOR_FACTORY_TYPE_MEDIA_IMAGE)))
    res = ((type & GST_DEVICE_MONITOR_FACTORY_TYPE_MEDIA_AUDIO)
        && (strstr (klass, "Audio") != NULL))
        || ((type & GST_DEVICE_MONITOR_FACTORY_TYPE_MEDIA_VIDEO)
        && (strstr (klass, "Video") != NULL))
        || ((type & GST_DEVICE_MONITOR_FACTORY_TYPE_MEDIA_IMAGE)
        && (strstr (klass, "Image") != NULL));

  return res;
}

static gboolean
device_monitor_filter (GstPluginFeature * feature, FilterData * data)
{
  gboolean res;

  /* we only care about device monitor factories */
  if (G_UNLIKELY (!GST_IS_DEVICE_MONITOR_FACTORY (feature)))
    return FALSE;

  res = (gst_plugin_feature_get_rank (feature) >= data->minrank) &&
      gst_device_monitor_factory_list_is_type (GST_DEVICE_MONITOR_FACTORY_CAST
      (feature), data->type);

  return res;
}

/**
 * gst_device_monitor_factory_list_get_device_monitors:
 * @type: a #GstDeviceMonitorFactoryListType
 * @minrank: Minimum rank
 *
 * Get a list of factories that match the given @type. Only device monitors
 * with a rank greater or equal to @minrank will be returned.
 * The list of factories is returned by decreasing rank.
 *
 * Returns: (transfer full) (element-type Gst.DeviceMonitorFactory): a #GList of
 *     #GstDeviceMonitorFactory device monitors. Use gst_plugin_feature_list_free() after
 *     usage.
 *
 * Since: 1.4
 */
GList *gst_device_monitor_factory_list_get_device_monitors
    (GstDeviceMonitorFactoryListType type, GstRank minrank)
{
  GList *result;
  FilterData data;

  /* prepare type */
  data.type = type;
  data.minrank = minrank;

  /* get the feature list using the filter */
  result = gst_registry_feature_filter (gst_registry_get (),
      (GstPluginFeatureFilter) device_monitor_filter, FALSE, &data);

  /* sort on rank and name */
  result = g_list_sort (result, gst_plugin_feature_rank_compare_func);

  return result;
}
