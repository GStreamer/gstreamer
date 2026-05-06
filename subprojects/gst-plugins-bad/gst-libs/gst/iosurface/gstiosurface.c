/* GStreamer IOSurface Library
 * Copyright (C) 2026 Dominique Leroux <dominique.p.leroux@gmail.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstiosurface.h"

/**
 * SECTION:iosurface
 * @title: IOSurface
 * @short_description: IOSurface-backed memory helpers
 *
 * The IOSurface library provides helpers for detecting and querying
 * IOSurface-backed #GstMemory.
 *
 * The #GST_CAPS_FEATURE_MEMORY_IOSURFACE caps feature can be used on
 * `video/x-raw` caps to negotiate IOSurface-backed buffers.
 *
 * Since: 1.30
 */

typedef struct
{
  GType allocator_type;
  GstIOSurfaceMemoryQueryFunction query;
} GstIOSurfaceQueryEntry;

G_LOCK_DEFINE_STATIC (iosurface_query_functions);
static GArray *iosurface_query_functions;

/* Returns a process-lifetime query function. There is intentionally no
 * unregister operation, so callers can safely invoke the returned function
 * after releasing the query-functions lock.
 */
static GstIOSurfaceMemoryQueryFunction
gst_iosurface_memory_find_query_function (GstMemory * mem)
{
  GType allocator_type;
  GstIOSurfaceMemoryQueryFunction query = NULL;

  g_return_val_if_fail (mem != NULL, NULL);
  g_return_val_if_fail (mem->allocator != NULL, NULL);

  allocator_type = G_OBJECT_TYPE (mem->allocator);

  G_LOCK (iosurface_query_functions);
  if (iosurface_query_functions) {
    for (guint i = 0; i < iosurface_query_functions->len; i++) {
      GstIOSurfaceQueryEntry *entry =
          &g_array_index (iosurface_query_functions, GstIOSurfaceQueryEntry, i);

      if (g_type_is_a (allocator_type, entry->allocator_type)) {
        query = entry->query;
        break;
      }
    }
  }
  G_UNLOCK (iosurface_query_functions);

  return query;
}

/**
 * gst_is_iosurface_memory:
 * @mem: a #GstMemory
 *
 * Returns: %TRUE if @mem exposes an IOSurface.
 *
 * Since: 1.30
 */
gboolean
gst_is_iosurface_memory (GstMemory * mem)
{
  return gst_iosurface_memory_peek_surface (mem, NULL, NULL);
}

/**
 * gst_is_iosurface_buffer:
 * @buffer: a #GstBuffer
 *
 * Returns: %TRUE if @buffer is non-empty and every memory block in @buffer
 * exposes an IOSurface.
 *
 * Since: 1.30
 */
gboolean
gst_is_iosurface_buffer (GstBuffer * buffer)
{
  guint n_mem;

  g_return_val_if_fail (buffer != NULL, FALSE);

  n_mem = gst_buffer_n_memory (buffer);
  if (n_mem == 0)
    return FALSE;

  for (guint i = 0; i < n_mem; i++) {
    GstMemory *mem = gst_buffer_peek_memory (buffer, i);

    if (!gst_is_iosurface_memory (mem))
      return FALSE;
  }

  return TRUE;
}

/**
 * gst_iosurface_memory_peek_surface:
 * @mem: a #GstMemory
 * @surface: (out) (transfer none) (optional): the #IOSurfaceRef
 * @plane: (out) (optional): the IOSurface plane index represented by @mem
 *
 * Queries whether @mem is backed by an IOSurface and, if so, returns the
 * IOSurface and plane represented by this memory.
 * @surface and @plane are only modified if this function returns %TRUE.
 *
 * The returned IOSurface is owned by @mem and is valid for as long as @mem is
 * alive. Callers must call CFRetain() if they want to keep the IOSurfaceRef
 * beyond @mem's lifetime.
 *
 * Returns: %TRUE if @mem exposes an IOSurface.
 *
 * Since: 1.30
 */
gboolean
gst_iosurface_memory_peek_surface (GstMemory * mem, IOSurfaceRef * surface,
    guint * plane)
{
  GstIOSurfaceMemoryQueryFunction query =
      gst_iosurface_memory_find_query_function (mem);
  IOSurfaceRef queried_surface = NULL;
  guint queried_plane = G_MAXUINT;

  if (!query)
    return FALSE;

  if (!query (mem, &queried_surface, &queried_plane))
    return FALSE;

  if (!queried_surface || queried_plane == G_MAXUINT)
    return FALSE;

  if (surface)
    *surface = queried_surface;
  if (plane)
    *plane = queried_plane;

  return TRUE;
}

/**
 * gst_iosurface_memory_register_query_function:
 * @allocator_type: a #GstAllocator type
 * @query: function used to query IOSurface backing for memory allocated by
 *     @allocator_type
 *
 * Registers @query as the IOSurface query function for #GstMemory objects
 * allocated by @allocator_type.
 *
 * This function is intended for memory implementers rather than applications.
 * @query must remain valid for the lifetime of the process and must only
 * return borrowed IOSurface references owned by the queried memory.
 *
 * Since: 1.30
 */
void
gst_iosurface_memory_register_query_function (GType allocator_type,
    GstIOSurfaceMemoryQueryFunction query)
{
  GstIOSurfaceQueryEntry entry;

  g_return_if_fail (allocator_type != G_TYPE_INVALID);
  g_return_if_fail (query != NULL);

  G_LOCK (iosurface_query_functions);
  if (!iosurface_query_functions)
    iosurface_query_functions =
        g_array_new (FALSE, FALSE, sizeof (GstIOSurfaceQueryEntry));

  for (guint i = 0; i < iosurface_query_functions->len; i++) {
    GstIOSurfaceQueryEntry *existing =
        &g_array_index (iosurface_query_functions, GstIOSurfaceQueryEntry, i);

    if (existing->allocator_type == allocator_type) {
      existing->query = query;
      G_UNLOCK (iosurface_query_functions);
      return;
    }
  }

  entry.allocator_type = allocator_type;
  entry.query = query;
  g_array_append_val (iosurface_query_functions, entry);
  G_UNLOCK (iosurface_query_functions);
}
