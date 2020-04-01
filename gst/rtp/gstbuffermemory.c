/* GStreamer
 * Copyright (C) 2020 Ognyan Tonchev <ognyan at axis dot com>
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

#include "gstbuffermemory.h"

gboolean
gst_buffer_memory_map (GstBuffer * buffer, GstBufferMemoryMap * map)
{
  GstMemory *mem;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);
  g_return_val_if_fail (map != NULL, FALSE);

  if (gst_buffer_n_memory (buffer) == 0) {
    GST_DEBUG ("no memory blocks in buffer");
    return FALSE;
  }

  mem = gst_buffer_get_memory (buffer, 0);

  if (!gst_memory_map (mem, &map->map, GST_MAP_READ)) {
    GST_ERROR ("failed to map memory");
    gst_memory_unref (mem);
    return FALSE;
  }

  map->buf = buffer;
  map->mem = mem;
  map->data = map->map.data;
  map->size = map->map.size;
  map->index = 0;
  map->total_size = gst_buffer_get_size (buffer);
  map->offset = 0;

  return TRUE;
}

static gboolean
buffer_memory_map_next (GstBufferMemoryMap * map)
{
  if (!map->mem)
    return FALSE;

  gst_memory_unmap (map->mem, &map->map);
  gst_memory_unref (map->mem);
  map->mem = NULL;
  map->data = NULL;
  map->size = 0;

  map->index++;

  if (map->index >= gst_buffer_n_memory (map->buf)) {
    GST_DEBUG ("no more memory blocks in buffer");
    return FALSE;
  }

  map->mem = gst_buffer_get_memory (map->buf, map->index);

  if (!gst_memory_map (map->mem, &map->map, GST_MAP_READ)) {
    GST_ERROR ("failed to map memory");
    gst_memory_unref (map->mem);
    map->mem = NULL;
    return FALSE;
  }

  map->data = map->map.data;
  map->size = map->map.size;

  return TRUE;
}

gboolean
gst_buffer_memory_advance_bytes (GstBufferMemoryMap * map, gsize size)
{
  gsize offset = size;

  g_return_val_if_fail (map != NULL, FALSE);

  map->offset += size;

  while (offset >= map->size) {
    offset -= map->size;
    GST_DEBUG ("switching memory");
    if (!buffer_memory_map_next (map))
      return FALSE;
  }

  map->data += offset;
  map->size -= offset;

  return TRUE;
}

void
gst_buffer_memory_unmap (GstBufferMemoryMap * map)
{
  g_return_if_fail (map != NULL);

  if (map->mem) {
    gst_memory_unmap (map->mem, &map->map);
    gst_memory_unref (map->mem);
    map->mem = NULL;
  }
}
