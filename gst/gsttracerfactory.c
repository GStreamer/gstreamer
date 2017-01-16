/* GStreamer
 * Copyright (C) 2013 Stefan Sauer <ensonic@users.sf.net>
 *
 * gsttracerfactory.c: tracing subsystem
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
 * SECTION:gsttracerfactory
 * @title: GstTracerFactory
 * @short_description: Information about registered tracer functions
 *
 * Use gst_tracer_factory_get_list() to get a list of tracer factories known to
 * GStreamer.
 */

#include "gst_private.h"
#include "gstinfo.h"
#include "gsttracerfactory.h"
#include "gstregistry.h"

GST_DEBUG_CATEGORY (tracer_debug);
#define GST_CAT_DEFAULT tracer_debug

#define _do_init \
{ \
  GST_DEBUG_CATEGORY_INIT (tracer_debug, "GST_TRACER", \
      GST_DEBUG_FG_BLUE, "tracing subsystem"); \
}

#define gst_tracer_factory_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstTracerFactory, gst_tracer_factory,
    GST_TYPE_PLUGIN_FEATURE, _do_init);

static void
gst_tracer_factory_class_init (GstTracerFactoryClass * klass)
{
}

static void
gst_tracer_factory_init (GstTracerFactory * factory)
{
}

/**
 * gst_tracer_factory_get_list:
 *
 * Gets the list of all registered tracer factories. You must free the
 * list using gst_plugin_feature_list_free().
 *
 * The returned factories are sorted by factory name.
 *
 * Free-function: gst_plugin_feature_list_free
 *
 * Returns: (transfer full) (element-type Gst.TracerFactory): the list of all
 *     registered #GstTracerFactory.
 *
 * Since: 1.8
 */
GList *
gst_tracer_factory_get_list (void)
{
  return gst_registry_get_feature_list (gst_registry_get (),
      GST_TYPE_TRACER_FACTORY);
}
