/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#ifndef __GST_MEM_CHUNK_H__
#define __GST_MEM_CHUNK_H__

#include <glib.h>

G_BEGIN_DECLS typedef struct _GstMemChunk GstMemChunk;

GstMemChunk *gst_mem_chunk_new (gchar * name,
    gint atom_size, gulong area_size, gint type);

void gst_mem_chunk_destroy (GstMemChunk * mem_chunk);

gpointer gst_mem_chunk_alloc (GstMemChunk * mem_chunk);
gpointer gst_mem_chunk_alloc0 (GstMemChunk * mem_chunk);
void gst_mem_chunk_free (GstMemChunk * mem_chunk, gpointer mem);

G_END_DECLS
#endif /* __GST_MEM_CHUNK_H__ */
