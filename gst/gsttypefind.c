/* GStreamer
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * gsttypefind.h: typefinding subsystem
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

#include "gstinfo.h"
#include "gsttypefind.h"
#include "gstregistrypool.h"

GST_DEBUG_CATEGORY_STATIC (gst_type_find_debug);
#define GST_CAT_DEFAULT gst_type_find_debug

static void gst_type_find_factory_class_init (gpointer g_class,
    gpointer class_data);
static void gst_type_find_factory_init (GTypeInstance * instance,
    gpointer g_class);
static void gst_type_find_factory_dispose (GObject * object);

static void gst_type_find_factory_unload_thyself (GstPluginFeature * feature);

static void gst_type_find_load_plugin (GstTypeFind * find, gpointer data);

static GstPluginFeatureClass *parent_class = NULL;

GType
gst_type_find_factory_get_type (void)
{
  static GType typefind_type = 0;

  if (!typefind_type) {
    static const GTypeInfo typefind_info = {
      sizeof (GstTypeFindFactoryClass),
      NULL,
      NULL,
      gst_type_find_factory_class_init,
      NULL,
      NULL,
      sizeof (GstTypeFindFactory),
      0,
      gst_type_find_factory_init,
      NULL
    };
    typefind_type = g_type_register_static (GST_TYPE_PLUGIN_FEATURE,
	"GstTypeFindFactory", &typefind_info, 0);
    GST_DEBUG_CATEGORY_INIT (gst_type_find_debug, "GST_TYPEFIND",
	GST_DEBUG_FG_GREEN, "typefinding subsystem");
  }

  return typefind_type;
}
static void
gst_type_find_factory_class_init (gpointer g_class, gpointer class_data)
{
  GstPluginFeatureClass *gstpluginfeature_class =
      GST_PLUGIN_FEATURE_CLASS (g_class);
  GObjectClass *object_class = G_OBJECT_CLASS (g_class);

  parent_class = g_type_class_peek_parent (g_class);

  object_class->dispose = gst_type_find_factory_dispose;

  gstpluginfeature_class->unload_thyself =
      GST_DEBUG_FUNCPTR (gst_type_find_factory_unload_thyself);
}
static void
gst_type_find_factory_init (GTypeInstance * instance, gpointer g_class)
{
  GstTypeFindFactory *factory = GST_TYPE_FIND_FACTORY (instance);

  factory->user_data = factory;
  factory->function = gst_type_find_load_plugin;
}
static void
gst_type_find_factory_dispose (GObject * object)
{
  GstTypeFindFactory *factory = GST_TYPE_FIND_FACTORY (object);

  if (factory->caps) {
    gst_caps_free (factory->caps);
    factory->caps = NULL;
  }
  if (factory->extensions) {
    g_strfreev (factory->extensions);
    factory->extensions = NULL;
  }
}
static void
gst_type_find_factory_unload_thyself (GstPluginFeature * feature)
{
  GstTypeFindFactory *factory = GST_TYPE_FIND_FACTORY (feature);

  factory->function = gst_type_find_load_plugin;
  factory->user_data = factory;
}
static void
gst_type_find_load_plugin (GstTypeFind * find, gpointer data)
{
  GstTypeFindFactory *factory = GST_TYPE_FIND_FACTORY (data);

  GST_DEBUG_OBJECT (factory, "need to load typefind function %s",
      GST_PLUGIN_FEATURE_NAME (factory));

  if (gst_plugin_feature_ensure_loaded (GST_PLUGIN_FEATURE (factory))) {
    if (factory->function == gst_type_find_load_plugin) {
      /* looks like we didn't get a real typefind function */
      g_warning ("could not load valid typefind function for feature '%s'\n",
	  GST_PLUGIN_FEATURE_NAME (factory));
    } else {
      g_assert (factory->function);
      gst_type_find_factory_call_function (factory, find);
    }
  }
}

/**
 * gst_type_find_factory_get_list:
 *
 * Gets the list of all registered typefind factories. You must free the
 * list using g_list_free.
 * 
 * Returns: the list of all registered typefind factories
 */
GList *
gst_type_find_factory_get_list (void)
{
  return gst_registry_pool_feature_list (GST_TYPE_TYPE_FIND_FACTORY);
}

/**
 * gst_type_find_factory_get_caps:
 * @factory: a factory
 * 
 * Gets the caps associated with a typefind factory.
 *
 * Returns: the #GstCaps associated with this factory
 */
const GstCaps *
gst_type_find_factory_get_caps (const GstTypeFindFactory * factory)
{
  g_return_val_if_fail (GST_IS_TYPE_FIND_FACTORY (factory), NULL);

  return factory->caps;
}

/**
 * gst_type_find_factory_get_extensions:
 * @factory: a factory
 * 
 * Gets the extensions associated with a typefind factory. The returned
 * array should not be changed. If you need to change stuff in it, you should
 * copy it using g_stdupv().  This function may return NULL to indicate
 * a 0-length list.
 *
 * Returns: a NULL-terminated array of extensions associated with this factory
 */
gchar **
gst_type_find_factory_get_extensions (const GstTypeFindFactory * factory)
{
  g_return_val_if_fail (GST_IS_TYPE_FIND_FACTORY (factory), NULL);

  return factory->extensions;
}

/**
 * gst_type_find_factory_call_function:
 * @factory: a factory
 * @find: a properly setup #GstTypeFind entry. The get_data and suggest_type
 *        members must be set.
 * 
 * Calls the typefinding function associated with this factory.
 */
void
gst_type_find_factory_call_function (const GstTypeFindFactory * factory,
    GstTypeFind * find)
{
  g_return_if_fail (GST_IS_TYPE_FIND_FACTORY (factory));
  g_return_if_fail (find != NULL);
  g_return_if_fail (find->peek != NULL);
  g_return_if_fail (find->suggest != NULL);

  /* should never happen */
  g_assert (factory->function != NULL);

  factory->function (find, factory->user_data);
}

/**
 * gst_type_find_register:
 * @plugin: the GstPlugin to register with
 * @name: the name for registering
 * @rank: rank (or importance) of this typefind function
 * @func: the function to use for typefinding
 * @extensions: optional extensions that could belong to this type
 * @possible_caps: optionally the caps that could be returned when typefinding succeeds
 * @data: optional user data. This user data must be available until the plugin 
 *	  is unloaded.
 *
 * Registers a new typefind function to be used for typefinding. After 
 * registering this function will be available for typefinding.
 * This function is typically called during an element's plugin initialization.
 *
 * Returns: TRUE on success, FALSE otherwise
 */
gboolean
gst_type_find_register (GstPlugin * plugin, const gchar * name, guint rank,
    GstTypeFindFunction func, gchar ** extensions,
    const GstCaps * possible_caps, gpointer data)
{
  GstTypeFindFactory *factory;

  g_return_val_if_fail (plugin != NULL, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (func != NULL, FALSE);

  GST_INFO ("registering typefind function for %s", name);
  factory =
      GST_TYPE_FIND_FACTORY (gst_registry_pool_find_feature (name,
	  GST_TYPE_TYPE_FIND_FACTORY));
  if (!factory) {
    factory = g_object_new (GST_TYPE_TYPE_FIND_FACTORY, NULL);
    GST_DEBUG_OBJECT (factory, "using new typefind factory for %s", name);
    g_assert (GST_IS_TYPE_FIND_FACTORY (factory));
    gst_plugin_feature_set_name (GST_PLUGIN_FEATURE (factory), name);
  } else {
    GST_DEBUG_OBJECT (factory, "using old typefind factory for %s", name);
  }

  gst_plugin_feature_set_rank (GST_PLUGIN_FEATURE (factory), rank);
  if (factory->extensions)
    g_strfreev (factory->extensions);

  factory->extensions = g_strdupv (extensions);
  gst_caps_replace (&factory->caps, gst_caps_copy (possible_caps));
  factory->function = func;
  factory->user_data = data;

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

/*** typefind function interface **********************************************/

/**
 * gst_type_find_peek:
 * @find: the find object the function was called with
 * @offset: the offset
 * @size: the number of bytes to return
 *
 * Returns size bytes of the stream to identify beginning at offset. If offset 
 * is a positive number, the offset is relative to the beginning of the stream,
 * if offset is a negative number the offset is relative to the end of the 
 * stream. The returned memory is valid until the typefinding function returns
 * and must not be freed.
 * If NULL is returned, that data is not available.
 *
 * Returns: the requested data or NULL if that data is not available.
 */
guint8 *
gst_type_find_peek (GstTypeFind * find, gint64 offset, guint size)
{
  g_return_val_if_fail (find->peek != NULL, NULL);

  return find->peek (find->data, offset, size);
}

/**
 * gst_type_find_suggest:
 * @find: the find object the function was called with
 * @probability: the probability in percent that the suggestion is right
 * @caps: the fixed caps to suggest
 *
 * If a typefind function calls this function it suggests the caps with the
 * given probability. A typefind function may supply different suggestions
 * in one call.
 * It is up to the caller of the typefind function to interpret these values.
 */
void
gst_type_find_suggest (GstTypeFind * find, guint probability,
    const GstCaps * caps)
{
  g_return_if_fail (find->suggest != NULL);
  g_return_if_fail (probability <= 100);
  g_return_if_fail (caps != NULL);
  g_return_if_fail (gst_caps_is_fixed (caps));

  find->suggest (find->data, probability, caps);
}

/**
 * gst_type_find_get_length:
 * @find: the find object the function was called with
 *
 * Get the length of the data stream.
 * 
 * Returns: the length of the data stream or 0 if it is not available.
 */
guint64
gst_type_find_get_length (GstTypeFind * find)
{
  if (find->get_length == NULL)
    return 0;

  return find->get_length (find->data);
}
