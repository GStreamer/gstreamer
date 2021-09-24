/*
 *  gstvaapitexture.c - VA texture Hash map
 *
 *  Copyright (C) 2016 Intel Corporation
 *  Copyright (C) 2016 Igalia S.L.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

/**
 * SECTION:gstvaapitexturemap
 * @short_description: VA/GLX/EGL texture hash map abstraction
 */

#include "gstvaapitexturemap.h"

#define DEBUG 1
#include "gstvaapidebug.h"

/**
 * GstVaapiTextureMap:
 *
 * Base class for API-dependent texture map.
 */
struct _GstVaapiTextureMap
{
  GstObject parent_instance;

  /*< private > */
  GHashTable *texture_map;
};

/**
 * GstVaapiTextureMapClass:
 *
 * Base class for API-dependent texture map.
 */
struct _GstVaapiTextureMapClass
{
  GstObjectClass parent_class;
};

#define MAX_NUM_TEXTURE 10

G_DEFINE_TYPE (GstVaapiTextureMap, gst_vaapi_texture_map, GST_TYPE_OBJECT);

static void
gst_vaapi_texture_map_init (GstVaapiTextureMap * map)
{
  map->texture_map =
      g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      (GDestroyNotify) gst_mini_object_unref);
}

static void
gst_vaapi_texture_map_finalize (GObject * object)
{
  GstVaapiTextureMap *map = GST_VAAPI_TEXTURE_MAP (object);

  if (map->texture_map) {
    g_hash_table_remove_all (map->texture_map);
    g_hash_table_destroy (map->texture_map);
  }

  G_OBJECT_CLASS (gst_vaapi_texture_map_parent_class)->finalize (object);
}

static void
gst_vaapi_texture_map_class_init (GstVaapiTextureMapClass * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gst_vaapi_texture_map_finalize;
}

/**
 * gst_vaapi_texture_map_new:
 *
 * Creates a texture hash map.
 *
 * Return value: the newly created #GstVaapiTextureMap object
 */
GstVaapiTextureMap *
gst_vaapi_texture_map_new (void)
{
  GstVaapiTextureMap *map;

  map = g_object_new (GST_TYPE_VAAPI_TEXTURE_MAP, NULL);
  return map;
}

/**
 * gst_vaapi_texture_map_add:
 * @map: a #GstVaapiTextureMap instance
 * @texture: a #GstVaapiTexture instance to add
 * @id: the id of the GLTexture
 *
 * Adds @texture into the @map table.
 *
 * Returns: %TRUE if @texture was inserted correctly.
 **/
gboolean
gst_vaapi_texture_map_add (GstVaapiTextureMap * map, GstVaapiTexture * texture,
    guint id)
{
  g_return_val_if_fail (map != NULL, FALSE);
  g_return_val_if_fail (map->texture_map != NULL, FALSE);
  g_return_val_if_fail (texture != NULL, FALSE);

  if (g_hash_table_size (map->texture_map) > MAX_NUM_TEXTURE) {
    GST_WARNING ("Texture map is full");
    return FALSE;
  }

  g_hash_table_insert (map->texture_map, GUINT_TO_POINTER (id), texture);

  return TRUE;
}

/**
 * gst_vaapi_texture_map_lookup:
 * @map: a #GstVaapiTextureMap instance
 * @id: the id of the GLTexture
 *
 * Search for the #GstVaapiTexture associated with the GLTexture @id
 * in the @map.
 *
 * Returns: a pointer to #GstVaapiTexture if found; otherwise %NULL.
 **/
GstVaapiTexture *
gst_vaapi_texture_map_lookup (GstVaapiTextureMap * map, guint id)
{
  g_return_val_if_fail (map != NULL, NULL);
  g_return_val_if_fail (map->texture_map != NULL, NULL);

  return g_hash_table_lookup (map->texture_map, GUINT_TO_POINTER (id));
}

/**
 * gst_vaapi_texture_map_reset:
 * @map: a #GstVaapiTextureMap instance
 *
 * Removes all the #GstVaapiTexture in the @map.
 **/
void
gst_vaapi_texture_map_reset (GstVaapiTextureMap * map)
{
  g_return_if_fail (map != NULL);
  g_return_if_fail (map->texture_map != NULL);

  g_hash_table_remove_all (map->texture_map);
}
