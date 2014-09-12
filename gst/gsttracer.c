/* GStreamer
 * Copyright (C) 2013 Stefan Sauer <ensonic@users.sf.net>
 *
 * gsttracer.c: tracer base class
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
 * SECTION:gsttracer
 * @short_description: Tracing base class
 *
 * Tracing modules will subclass #GstTracer and register through
 * gst_tracer_register(). Modules can attach to various hook-types - see
 * #GstTracerHook. When invoked they receive hook specific contextual data, 
 * which they must not modify.
 */

#define GST_USE_UNSTABLE_API

#include "gst_private.h"
#include "gstenumtypes.h"
#include "gsttracer.h"
#include "gsttracerfactory.h"

GST_DEBUG_CATEGORY_EXTERN (tracer_debug);
#define GST_CAT_DEFAULT tracer_debug

/* tracing plugins base class */

enum
{
  PROP_0,
  PROP_PARAMS,
  PROP_MASK,
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

static void gst_tracer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_tracer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

struct _GstTracerPrivate
{
  const gchar *params;
  GstTracerHook mask;
};

#define gst_tracer_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE (GstTracer, gst_tracer, GST_TYPE_OBJECT);

static void
gst_tracer_class_init (GstTracerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gst_tracer_set_property;
  gobject_class->get_property = gst_tracer_get_property;

  properties[PROP_PARAMS] =
      g_param_spec_string ("params", "Params", "Extra configuration parameters",
      NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);

  properties[PROP_MASK] =
      g_param_spec_flags ("mask", "Mask", "Event mask", GST_TYPE_TRACER_HOOK,
      GST_TRACER_HOOK_NONE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, PROP_LAST, properties);
  g_type_class_add_private (klass, sizeof (GstTracerPrivate));
}

static void
gst_tracer_init (GstTracer * tracer)
{
  tracer->priv = G_TYPE_INSTANCE_GET_PRIVATE (tracer, GST_TYPE_TRACER,
      GstTracerPrivate);
}

static void
gst_tracer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTracer *self = GST_TRACER_CAST (object);

  switch (prop_id) {
    case PROP_PARAMS:
      self->priv->params = g_value_get_string (value);
      break;
    case PROP_MASK:
      self->priv->mask = g_value_get_flags (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_tracer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstTracer *self = GST_TRACER_CAST (object);

  switch (prop_id) {
    case PROP_PARAMS:
      g_value_set_string (value, self->priv->params);
      break;
    case PROP_MASK:
      g_value_set_flags (value, self->priv->mask);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

void
gst_tracer_invoke (GstTracer * self, GstTracerMessageId mid, va_list var_args)
{
  GstTracerClass *klass = GST_TRACER_GET_CLASS (self);

  g_return_if_fail (klass->invoke);

  klass->invoke (self, mid, var_args);
}

/* tracing modules */

gboolean
gst_tracer_register (GstPlugin * plugin, const gchar * name, GType type)
{
  GstPluginFeature *existing_feature;
  GstRegistry *registry;
  GstTracerFactory *factory;

  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (g_type_is_a (type, GST_TYPE_TRACER), FALSE);

  registry = gst_registry_get ();
  /* check if feature already exists, if it exists there is no need to update it
   * when the registry is getting updated, outdated plugins and all their
   * features are removed and readded.
   */
  existing_feature = gst_registry_lookup_feature (registry, name);
  if (existing_feature) {
    GST_DEBUG_OBJECT (registry, "update existing feature %p (%s)",
        existing_feature, name);
    factory = GST_TRACER_FACTORY_CAST (existing_feature);
    factory->type = type;
    existing_feature->loaded = TRUE;
    gst_object_unref (existing_feature);
    return TRUE;
  }

  factory = g_object_newv (GST_TYPE_TRACER_FACTORY, 0, NULL);
  GST_DEBUG_OBJECT (factory, "new tracer factory for %s", name);

  gst_plugin_feature_set_name (GST_PLUGIN_FEATURE_CAST (factory), name);
  gst_plugin_feature_set_rank (GST_PLUGIN_FEATURE_CAST (factory),
      GST_RANK_NONE);

  factory->type = type;
  GST_DEBUG_OBJECT (factory, "tracer factory for %u:%s",
      (guint) type, g_type_name (type));

  if (plugin && plugin->desc.name) {
    GST_PLUGIN_FEATURE_CAST (factory)->plugin_name = plugin->desc.name; /* interned string */
    GST_PLUGIN_FEATURE_CAST (factory)->plugin = plugin;
    g_object_add_weak_pointer ((GObject *) plugin,
        (gpointer *) & GST_PLUGIN_FEATURE_CAST (factory)->plugin);
  } else {
    GST_PLUGIN_FEATURE_CAST (factory)->plugin_name = "NULL";
    GST_PLUGIN_FEATURE_CAST (factory)->plugin = NULL;
  }
  GST_PLUGIN_FEATURE_CAST (factory)->loaded = TRUE;

  gst_registry_add_feature (gst_registry_get (),
      GST_PLUGIN_FEATURE_CAST (factory));

  return TRUE;
}

/* tracing module helpers */

void
gst_tracer_log_trace (GstStructure * s)
{
  GST_TRACE ("%" GST_PTR_FORMAT, s);
  /* expands to:
     gst_debug_log_valist (
     GST_CAT_DEFAULT, GST_LEVEL_TRACE,
     file, func, line, object
     "%" GST_PTR_FORMAT, s);
     // does it make sense to use the {file, line, func} from the tracer hook?
     // a)
     // - we'd need to pass them in the macros to gst_tracer_dispatch()
     // - and each tracer needs to grab them from the va_list and pass them here
     // b)
     // - we create a content in dispatch, pass that to the tracer
     // - and the tracer will pass that here
     // ideally we also use *our* ts instead of the one that
     // gst_debug_log_default() will pick
   */
  gst_structure_free (s);
}
