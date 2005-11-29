/* GStreamer
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * gsttypefind.c: typefinding subsystem
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
 * SECTION:gsttypefind
 * @short_description: Stream type detection
 *
 * The following functions allow you to detect the media type of an unknown
 * stream.
 *
 * Last reviewed on 2005-11-09 (0.9.4)
 */

#include "gst_private.h"
#include "gstinfo.h"
#include "gsttypefind.h"
#include "gstregistry.h"
#include "gsttypefindfactory.h"

GST_DEBUG_CATEGORY_EXTERN (gst_type_find_debug);
#define GST_CAT_DEFAULT gst_type_find_debug

/**
 * gst_type_find_register:
 * @plugin: A #GstPlugin.
 * @name: The name for registering
 * @rank: The rank (or importance) of this typefind function
 * @func: The #GstTypeFindFunction to use
 * @extensions: Optional extensions that could belong to this type
 * @possible_caps: Optionally the caps that could be returned when typefinding succeeds
 * @data: Optional user data. This user data must be available until the plugin
 *	  is unloaded.
 * @data_notify: a #GDestroyNotify that will be called on @data when the plugin
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
    const GstCaps * possible_caps, gpointer data, GDestroyNotify data_notify)
{
  GstTypeFindFactory *factory;

  g_return_val_if_fail (plugin != NULL, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (func != NULL, FALSE);

  GST_INFO ("registering typefind function for %s", name);

  factory = g_object_new (GST_TYPE_TYPE_FIND_FACTORY, NULL);
  GST_DEBUG_OBJECT (factory, "using new typefind factory for %s", name);
  g_assert (GST_IS_TYPE_FIND_FACTORY (factory));
  gst_plugin_feature_set_name (GST_PLUGIN_FEATURE (factory), name);

  gst_plugin_feature_set_rank (GST_PLUGIN_FEATURE (factory), rank);
  if (factory->extensions)
    g_strfreev (factory->extensions);

  factory->extensions = g_strdupv (extensions);
  gst_caps_replace (&factory->caps, (GstCaps *) possible_caps);
  factory->function = func;
  factory->user_data = data;
  factory->user_data_notify = data_notify;
  GST_PLUGIN_FEATURE (factory)->plugin_name = g_strdup (plugin->desc.name);
  GST_PLUGIN_FEATURE (factory)->loaded = TRUE;

  gst_registry_add_feature (gst_registry_get_default (),
      GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

/*** typefind function interface **********************************************/

/**
 * gst_type_find_peek:
 * @find: The #GstTypeFind object the function was called with
 * @offset: The offset
 * @size: The number of bytes to return
 *
 * Returns the @size bytes of the stream to identify beginning at offset. If
 * offset is a positive number, the offset is relative to the beginning of the
 * stream, if offset is a negative number the offset is relative to the end of
 * the stream. The returned memory is valid until the typefinding function
 * returns and must not be freed.
 *
 * Returns: the requested data, or NULL if that data is not available.
 */
guint8 *
gst_type_find_peek (GstTypeFind * find, gint64 offset, guint size)
{
  g_return_val_if_fail (find->peek != NULL, NULL);

  return find->peek (find->data, offset, size);
}

/**
 * gst_type_find_suggest:
 * @find: The #GstTypeFind object the function was called with
 * @probability: The probability in percent that the suggestion is right
 * @caps: The fixed #GstCaps to suggest
 *
 * If a #GstTypeFindFunction calls this function it suggests the caps with the
 * given probability. A #GstTypeFindFunction may supply different suggestions
 * in one call.
 * It is up to the caller of the #GstTypeFindFunction to interpret these values.
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
 * @find: The #GstTypeFind the function was called with
 *
 * Get the length of the data stream.
 *
 * Returns: The length of the data stream, or 0 if it is not available.
 */
guint64
gst_type_find_get_length (GstTypeFind * find)
{
  if (find->get_length == NULL)
    return 0;

  return find->get_length (find->data);
}
