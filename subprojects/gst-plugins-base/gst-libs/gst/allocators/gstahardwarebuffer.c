/* GStreamer AHardwareBuffer Library
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

#include "gstahardwarebuffer.h"

/**
 * SECTION:gstahardwarebuffer
 * @title: AHardwareBuffer
 * @short_description: AHardwareBuffer-backed memory helpers
 *
 * The AHardwareBuffer helpers provide a common interface for detecting and
 * querying AHardwareBuffer-backed #GstMemory.
 *
 * The #GST_CAPS_FEATURE_MEMORY_AHARDWAREBUFFER caps feature can be used on
 * `video/x-raw` caps to negotiate AHardwareBuffer-backed buffers.
 *
 * Since: 1.30
 */

typedef struct
{
  GType allocator_type;
  GstAHardwareBufferMemoryQueryFunction query;
} GstAHardwareBufferQueryEntry;

G_LOCK_DEFINE_STATIC (ahardware_buffer_query_functions);
static GArray *ahardware_buffer_query_functions;

/* Returns a process-lifetime query function. There is intentionally no
 * unregister operation, so callers can safely invoke the returned function
 * after releasing the query-functions lock.
 */
static GstAHardwareBufferMemoryQueryFunction
gst_ahardware_buffer_memory_find_query_function (GstMemory * mem)
{
  GType allocator_type;
  GstAHardwareBufferMemoryQueryFunction query = NULL;

  g_return_val_if_fail (mem != NULL, NULL);
  g_return_val_if_fail (mem->allocator != NULL, NULL);

  allocator_type = G_OBJECT_TYPE (mem->allocator);

  G_LOCK (ahardware_buffer_query_functions);
  if (ahardware_buffer_query_functions) {
    for (guint i = 0; i < ahardware_buffer_query_functions->len; i++) {
      GstAHardwareBufferQueryEntry *entry =
          &g_array_index (ahardware_buffer_query_functions,
          GstAHardwareBufferQueryEntry, i);

      if (g_type_is_a (allocator_type, entry->allocator_type)) {
        query = entry->query;
        break;
      }
    }
  }
  G_UNLOCK (ahardware_buffer_query_functions);

  return query;
}

/**
 * gst_is_ahardware_buffer_memory:
 * @mem: a #GstMemory
 *
 * Returns: %TRUE if @mem exposes an AHardwareBuffer.
 *
 * Since: 1.30
 */
gboolean
gst_is_ahardware_buffer_memory (GstMemory * mem)
{
  return gst_ahardware_buffer_memory_peek_buffer (mem, NULL);
}

/**
 * gst_is_ahardware_buffer_buffer:
 * @buffer: a #GstBuffer
 *
 * Returns: %TRUE if @buffer is non-empty and every memory block in @buffer
 * exposes an AHardwareBuffer.
 *
 * Since: 1.30
 */
gboolean
gst_is_ahardware_buffer_buffer (GstBuffer * buffer)
{
  guint n_mem;

  g_return_val_if_fail (buffer != NULL, FALSE);

  n_mem = gst_buffer_n_memory (buffer);
  if (n_mem == 0)
    return FALSE;

  for (guint i = 0; i < n_mem; i++) {
    GstMemory *mem = gst_buffer_peek_memory (buffer, i);

    if (!gst_is_ahardware_buffer_memory (mem))
      return FALSE;
  }

  return TRUE;
}

/**
 * gst_ahardware_buffer_memory_peek_buffer:
 * @mem: a #GstMemory
 * @buffer: (out) (transfer none) (optional): the `AHardwareBuffer`
 *
 * Queries whether @mem is backed by an AHardwareBuffer and, if so, returns the
 * AHardwareBuffer represented by this memory.
 * @buffer is only modified if this function returns %TRUE.
 *
 * The returned AHardwareBuffer is owned by @mem and is valid for as long as
 * @mem is alive. Callers must call `AHardwareBuffer_acquire()` if they want to
 * keep the AHardwareBuffer beyond @mem's lifetime.
 *
 * Returns: %TRUE if @mem exposes an AHardwareBuffer.
 *
 * Since: 1.30
 */
gboolean
gst_ahardware_buffer_memory_peek_buffer (GstMemory * mem,
    AHardwareBuffer ** buffer)
{
  GstAHardwareBufferMemoryQueryFunction query =
      gst_ahardware_buffer_memory_find_query_function (mem);
  AHardwareBuffer *queried_buffer = NULL;

  if (!query)
    return FALSE;

  if (!query (mem, &queried_buffer))
    return FALSE;

  if (!queried_buffer)
    return FALSE;

  if (buffer)
    *buffer = queried_buffer;

  return TRUE;
}

/**
 * gst_ahardware_buffer_memory_register_query_function:
 * @allocator_type: a #GstAllocator type
 * @query: function used to query AHardwareBuffer backing for memory allocated
 *     by @allocator_type
 *
 * Registers @query as the AHardwareBuffer query function for #GstMemory
 * objects allocated by @allocator_type.
 *
 * This function is intended for memory implementers rather than applications.
 * @query must remain valid for the lifetime of the process and must only
 * return borrowed AHardwareBuffer references owned by the queried memory.
 *
 * Since: 1.30
 */
void
gst_ahardware_buffer_memory_register_query_function (GType allocator_type,
    GstAHardwareBufferMemoryQueryFunction query)
{
  GstAHardwareBufferQueryEntry entry;

  g_return_if_fail (allocator_type != G_TYPE_INVALID);
  g_return_if_fail (query != NULL);

  G_LOCK (ahardware_buffer_query_functions);
  if (!ahardware_buffer_query_functions)
    ahardware_buffer_query_functions =
        g_array_new (FALSE, FALSE, sizeof (GstAHardwareBufferQueryEntry));

  for (guint i = 0; i < ahardware_buffer_query_functions->len; i++) {
    GstAHardwareBufferQueryEntry *existing =
        &g_array_index (ahardware_buffer_query_functions,
        GstAHardwareBufferQueryEntry, i);

    if (existing->allocator_type == allocator_type) {
      existing->query = query;
      G_UNLOCK (ahardware_buffer_query_functions);
      return;
    }
  }

  entry.allocator_type = allocator_type;
  entry.query = query;
  g_array_append_val (ahardware_buffer_query_functions, entry);
  G_UNLOCK (ahardware_buffer_query_functions);
}
