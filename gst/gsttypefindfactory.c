/* GStreamer
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * gsttypefindfactory.c: typefinding subsystem
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
 * SECTION:gsttypefindfactory
 * @short_description: Information about registered typefind functions
 *
 * These functions allow querying informations about registered typefind
 * functions. How to create and register these functions is described in
 * the section <link linkend="gstreamer-Writing-typefind-functions">
 * "Writing typefind functions"</link>.
 *
 * <example>
 *   <title>how to write a simple typefinder</title>
 *   <programlisting>
 *   typedef struct {
 *     guint8 *data;
 *     guint size;
 *     guint probability;
 *     GstCaps *data;
 *   } MyTypeFind;
 *   static void
 *   my_peek (gpointer data, gint64 offset, guint size)
 *   {
 *     MyTypeFind *find = (MyTypeFind *) data;
 *     if (offset &gt;= 0 &amp;&amp; offset + size &lt;= find->size) {
 *       return find->data + offset;
 *     }
 *     return NULL;
 *   }
 *   static void
 *   my_suggest (gpointer data, guint probability, GstCaps *caps)
 *   {
 *     MyTypeFind *find = (MyTypeFind *) data;
 *     if (probability &gt; find->probability) {
 *       find->probability = probability;
 *       gst_caps_replace (&amp;find->caps, caps);
 *     }
 *   }
 *   static GstCaps *
 *   find_type (guint8 *data, guint size)
 *   {
 *     GList *walk, *type_list;
 *     MyTypeFind find = {data, size, 0, NULL};
 *     GstTypeFind gst_find = {my_peek, my_suggest, &amp;find, };
 *     walk = type_list = gst_type_find_factory_get_list ();
 *     while (walk) {
 *       GstTypeFindFactory *factory = GST_TYPE_FIND_FACTORY (walk->data);
 *       walk = g_list_next (walk)
 *       gst_type_find_factory_call_function (factory, &amp;gst_find);
 *     }
 *     g_list_free (type_list);
 *     return find.caps;
 *   };
 *   </programlisting>
 * </example>
 *
 * The above example shows how to write a very simple typefinder that
 * identifies the given data. You can get quite a bit more complicated than
 * that though.
 *
 * Last reviewed on 2005-11-09 (0.9.4)
 */

#include "gst_private.h"
#include "gstinfo.h"
#include "gsttypefind.h"
#include "gsttypefindfactory.h"
#include "gstregistry.h"

GST_DEBUG_CATEGORY (gst_type_find_debug);
#define GST_CAT_DEFAULT gst_type_find_debug

static void gst_type_find_factory_class_init (gpointer g_class,
    gpointer class_data);
static void gst_type_find_factory_init (GTypeInstance * instance,
    gpointer g_class);
static void gst_type_find_factory_dispose (GObject * object);

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
  GObjectClass *object_class = G_OBJECT_CLASS (g_class);

  parent_class = g_type_class_peek_parent (g_class);

  object_class->dispose = gst_type_find_factory_dispose;
}

static void
gst_type_find_factory_init (GTypeInstance * instance, gpointer g_class)
{
  GstTypeFindFactory *factory = GST_TYPE_FIND_FACTORY (instance);

  factory->user_data = factory;
  factory->user_data_notify = NULL;
}

static void
gst_type_find_factory_dispose (GObject * object)
{
  GstTypeFindFactory *factory = GST_TYPE_FIND_FACTORY (object);

  if (factory->caps) {
    gst_caps_unref (factory->caps);
    factory->caps = NULL;
  }
  if (factory->extensions) {
    g_strfreev (factory->extensions);
    factory->extensions = NULL;
  }
  if (factory->user_data_notify && factory->user_data) {
    factory->user_data_notify (factory->user_data);
    factory->user_data = NULL;
  }
}

/**
 * gst_type_find_factory_get_list:
 *
 * Gets the list of all registered typefind factories. You must free the
 * list using g_list_free.
 *
 * Returns: the list of all registered #GstTypeFindFactory.
 */
GList *
gst_type_find_factory_get_list (void)
{
  return gst_registry_get_feature_list (gst_registry_get_default (),
      GST_TYPE_TYPE_FIND_FACTORY);
}

/**
 * gst_type_find_factory_get_caps:
 * @factory: A #GstTypeFindFactory
 *
 * Gets the #GstCaps associated with a typefind factory.
 *
 * Returns: The #GstCaps associated with this factory
 */
GstCaps *
gst_type_find_factory_get_caps (GstTypeFindFactory * factory)
{
  g_return_val_if_fail (GST_IS_TYPE_FIND_FACTORY (factory), NULL);

  return factory->caps;
}

/**
 * gst_type_find_factory_get_extensions:
 * @factory: A #GstTypeFindFactory
 *
 * Gets the extensions associated with a #GstTypeFindFactory. The returned
 * array should not be changed. If you need to change stuff in it, you should
 * copy it using g_stdupv().  This function may return NULL to indicate
 * a 0-length list.
 *
 * Returns: a NULL-terminated array of extensions associated with this factory
 */
gchar **
gst_type_find_factory_get_extensions (GstTypeFindFactory * factory)
{
  g_return_val_if_fail (GST_IS_TYPE_FIND_FACTORY (factory), NULL);

  return factory->extensions;
}

/**
 * gst_type_find_factory_call_function:
 * @factory: A #GstTypeFindFactory
 * @find: A properly setup #GstTypeFind entry. The get_data and suggest_type
 *        members must be set.
 *
 * Calls the #GstTypeFindFunction associated with this factory.
 */
void
gst_type_find_factory_call_function (GstTypeFindFactory * factory,
    GstTypeFind * find)
{
  GstTypeFindFactory *new_factory;

  g_return_if_fail (GST_IS_TYPE_FIND_FACTORY (factory));
  g_return_if_fail (find != NULL);
  g_return_if_fail (find->peek != NULL);
  g_return_if_fail (find->suggest != NULL);

  new_factory =
      GST_TYPE_FIND_FACTORY (gst_plugin_feature_load (GST_PLUGIN_FEATURE
          (factory)));
  if (new_factory) {
    g_assert (new_factory->function != NULL);

    new_factory->function (find, new_factory->user_data);
    gst_object_unref (new_factory);
  }
}
