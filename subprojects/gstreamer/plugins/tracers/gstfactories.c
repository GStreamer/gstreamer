/* GStreamer
 * Copyright (C) 2021 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.com>
 *
 * gstfactories.c: A trace to log which plugin & factories are being used
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
/**
 * SECTION:tracer-factories
 * @short_description: log plugin and factories
 *
 * A tracing module that logs which plugins and factories are being used.
 *
 * This tracing module is particularly useful in conjuction with the `gst-stats`
 * program to generate a list of plugins and elements that are loaded by a
 * particular application to generate a minimal custom build of GStreamer.
 *
 * As a very simple example, you can run your application like this:
 * ```
 * $ GST_TRACERS=factories GST_DEBUG=GST_TRACER:7 gst-launch-1.0 audiotestsrc num-buffers=10 ! fakesink 2> log.txt
 * ...
 * $ gst-stats-1.0 log.txt
 * Plugins used: audiotestsrc;coreelements
 * Elements: audiotestsrc:audiotestsrc;coreelements:fakesink
 * Device-providers:
 * Typefinds:
 * Dynamic-types:
 * ```
 *
 * Based on this information, one can build a minimal, yet sufficient
 * build of GStreamer with a configuration like this one:
 * ```
 * meson setup builddir -Dgst-full-elements="audiotestsrc:audiotestsrc;coreelements:fakesink"
 * ```
 *
 * Since: 1.20
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstfactories.h"

G_DEFINE_TYPE (GstFactoriesTracer, gst_factories_tracer, GST_TYPE_TRACER);

static GstTracerRecord *tr_factory_used;

static void
do_element_new (GstFactoriesTracer * self, GstClockTime ts,
    GstElement * element)
{
  const gchar *plugin_name;
  const gchar *factory_name;
  GstPluginFeature *feature;
  GstElementFactory *factory = gst_element_get_factory (element);
  const gchar *source_module_name = "Unknown";
  GstPlugin *plugin;

  if (factory == NULL)
    return;

  feature = GST_PLUGIN_FEATURE (factory);

  factory_name = gst_plugin_feature_get_name (feature);
  plugin_name = gst_plugin_feature_get_plugin_name (feature);

  if (factory_name == NULL)
    factory_name = "";
  if (plugin_name == NULL)
    plugin_name = "";

  plugin = gst_plugin_feature_get_plugin (feature);
  if (plugin)
    source_module_name = gst_plugin_get_source (plugin);

  gst_tracer_record_log (tr_factory_used,
      (guint64) (guintptr) g_thread_self (), ts, "element", factory_name,
      plugin_name, source_module_name);

  g_clear_object (&plugin);
}

static void
do_plugin_feature_loaded (GstFactoriesTracer * self, GstClockTime ts,
    GstPluginFeature * feature)
{
  const gchar *plugin_name;
  const gchar *factory_name;
  const gchar *factory_type;
  const gchar *source_module_name = "Unknown";
  GstPlugin *plugin;

  /* Only care about elements when one is created */
  if (GST_IS_ELEMENT_FACTORY (feature))
    return;

  if (GST_IS_TYPE_FIND_FACTORY (feature))
    factory_type = "typefind";
  else if (GST_IS_DEVICE_PROVIDER_FACTORY (feature))
    factory_type = "device-provider";
  else if (GST_IS_DYNAMIC_TYPE_FACTORY (feature))
    factory_type = "dynamic-type";
  else
    g_assert_not_reached ();

  factory_name = gst_plugin_feature_get_name (feature);
  plugin_name = gst_plugin_feature_get_plugin_name (feature);

  if (factory_name == NULL)
    factory_name = "";
  if (plugin_name == NULL)
    plugin_name = "";

  plugin = gst_plugin_feature_get_plugin (feature);
  if (plugin)
    source_module_name = gst_plugin_get_source (plugin);
  if (source_module_name == NULL)
    source_module_name = "";

  gst_tracer_record_log (tr_factory_used,
      (guint64) (guintptr) g_thread_self (), ts, factory_type, factory_name,
      plugin_name, source_module_name);

  g_clear_object (&plugin);
}

static void
gst_factories_tracer_class_init (GstFactoriesTracerClass * klass)
{
  /* announce trace formats */
  /* *INDENT-OFF* */
  tr_factory_used = gst_tracer_record_new ("factory-used.class",
      "thread-id", GST_TYPE_STRUCTURE, gst_structure_new ("scope",
          "type", G_TYPE_GTYPE, G_TYPE_UINT64,
          "related-to", GST_TYPE_TRACER_VALUE_SCOPE, GST_TRACER_VALUE_SCOPE_THREAD,
          NULL),
      "ts", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_UINT64,
          "description", G_TYPE_STRING, "event ts",
          NULL),
      "factory-type", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_STRING,
          "description", G_TYPE_STRING, "type name of the factory",
          NULL),
      "factory", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_STRING,
          "description", G_TYPE_STRING, "name of the object factory",
          NULL),
      "plugin", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_STRING,
          "description", G_TYPE_STRING, "name of the plugin",
          NULL),
      "source-module", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_STRING,
          "description", G_TYPE_STRING, "name of the source module this feature is from",
          NULL),
     NULL);
  /* *INDENT-ON* */

  GST_OBJECT_FLAG_SET (tr_factory_used, GST_OBJECT_FLAG_MAY_BE_LEAKED);
}

static void
gst_factories_tracer_init (GstFactoriesTracer * self)
{
  GstTracer *tracer = GST_TRACER (self);

  gst_tracing_register_hook (tracer, "element-new",
      G_CALLBACK (do_element_new));
  gst_tracing_register_hook (tracer, "plugin-feature-loaded",
      G_CALLBACK (do_plugin_feature_loaded));
}
