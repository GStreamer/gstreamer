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

#ifndef __GST_BUFFER_MEMORY_H__
#define __GST_BUFFER_MEMORY_H__

#include <gst/gst.h>

G_BEGIN_DECLS

struct _GstBufferMemoryMap
{
  /* private datas */

  GstBuffer *buf;
  GstMemory *mem;
  GstMapInfo map;
  guint index;
  gsize total_size;

  /* public datas */

  /* data of the currently mapped memory */
  const guint8 *data;
  guint offset;

  /* size of the currently mapped memory */
  gsize size;

  /* When advancing through the data with gst_buffer_memory_advance_bytes ()
   * the data field is also advanced and the size field decreased with the
   * corresponding number of bytes. If all the bytes from the currently mapped
   * GstMemory have been consumed then a new GstMemory will be mapped and data
   * and size fileds will be updated.
   * */
};
typedef struct _GstBufferMemoryMap GstBufferMemoryMap;

G_GNUC_INTERNAL
gboolean gst_buffer_memory_map (GstBuffer * buffer, GstBufferMemoryMap * map);

G_GNUC_INTERNAL
gboolean gst_buffer_memory_advance_bytes (GstBufferMemoryMap * map, gsize size);

G_GNUC_INTERNAL
void gst_buffer_memory_unmap (GstBufferMemoryMap * map);

G_END_DECLS

#endif /* __GST_BUFFER_MEMORY_H__ */
