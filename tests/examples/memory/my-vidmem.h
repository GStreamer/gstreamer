/* GStreamer
 * Copyright (C) 2012 Wim Taymans <wim.taymans@gmail.be>
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

#include <gst/gst.h>

/* example of an allocator that needs a custom alloc function */
void my_vidmem_init (void);

GstMemory * my_vidmem_alloc             (guint format, guint width, guint height);

gboolean    my_is_vidmem                (GstMemory *mem);

void        my_vidmem_get_format        (GstMemory *mem, guint *format,
                                         guint *width, guint *height);

