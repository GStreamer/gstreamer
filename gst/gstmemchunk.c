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

#include <string.h>		/* memset */

#include "gstlog.h"
#include "gstutils.h"
#include "gstmemchunk.h"
#include "gsttrashstack.h"

#define GST_MEM_CHUNK_AREA(chunk) 	(((GstMemChunkElement*)(chunk))->area)
#define GST_MEM_CHUNK_DATA(chunk) 	((gpointer)(((GstMemChunkElement*)(chunk)) + 1))
#define GST_MEM_CHUNK_LINK(mem) 	((GstMemChunkElement*)((guint8*)(mem) - sizeof (GstMemChunkElement)))

typedef struct _GstMemChunkElement GstMemChunkElement;

struct _GstMemChunkElement
{
  GstTrashStackElement   elem;		/* make sure we can safely push it on the trashstack */
  gpointer	 	 area;		/* pointer to data areas */
};

struct _GstMemChunk
{
  GstTrashStack  stack;

  gchar         *name;
  gulong         area_size;
  gulong         chunk_size;
  gulong         atom_size;
  gboolean       cleanup;
};

/*******************************************************
 *         area size
 * +-------------------------------------------------------+
 *   chunk size
 * +-----------------+
 *
 * !next!area|data... !next!area!data.... !next!area!data...
 *  !                  ^ !                 ^ !
 *  +------------------+ +-----------------+ +--------> NULL
 *
 */
static gboolean
populate (GstMemChunk *mem_chunk) 
{
  guint8 *area;
  guint i;

  if (mem_chunk->cleanup)
    return FALSE;
 
  area = (guint8 *) g_malloc0 (mem_chunk->area_size);

  for (i=0; i < mem_chunk->area_size; i += mem_chunk->chunk_size) { 
    GST_MEM_CHUNK_AREA (area + i) = area;
    gst_trash_stack_push (&mem_chunk->stack, area + i);
  }

  return TRUE;
}

/**
 * gst_mem_chunk_new:
 * @name: the name of the chunk
 * @atom_size: the size of the allocated atoms
 * @area_size: the initial size of the memory area
 * @type: the allocation strategy to use
 *
 * Creates a new memchunk that will allocate atom_sized memchunks.
 * The initial area is set to area_size and will grow automatically 
 * when it is too small (with a small overhead when that happens)
 *
 * Returns: a new #GstMemChunk
 */
GstMemChunk*
gst_mem_chunk_new (gchar* name, gint atom_size, gulong area_size, gint type)
{
  GstMemChunk *mem_chunk;

  g_return_val_if_fail (atom_size > 0, NULL);
  g_return_val_if_fail (area_size >= (guint) atom_size, NULL);

  mem_chunk = g_malloc (sizeof (GstMemChunk));

  mem_chunk->chunk_size = atom_size + sizeof (GstMemChunkElement);
  area_size = (area_size/atom_size) * mem_chunk->chunk_size;

  mem_chunk->name = g_strdup (name);
  mem_chunk->atom_size = atom_size;
  mem_chunk->area_size = area_size;
  mem_chunk->cleanup = FALSE;
  gst_trash_stack_init (&mem_chunk->stack);

  populate (mem_chunk);

  return mem_chunk;
}

static gboolean
free_area (gpointer key, gpointer value, gpointer user_data)
{
  g_free (key);

  return TRUE;
}

/**
 * gst_mem_chunk_destroy:
 * @mem_chunk: the GstMemChunk to destroy
 *
 * Free the memory allocated by the memchunk
 */
void
gst_mem_chunk_destroy (GstMemChunk *mem_chunk) 
{
  GHashTable *elements = g_hash_table_new (NULL, NULL);
  gpointer data;

  mem_chunk->cleanup = TRUE;

  data = gst_mem_chunk_alloc (mem_chunk);
  while (data) {
    GstMemChunkElement *elem = GST_MEM_CHUNK_LINK (data);

    g_hash_table_insert (elements, GST_MEM_CHUNK_AREA (elem), NULL); 
    
    data = gst_mem_chunk_alloc (mem_chunk);
  } 
  g_hash_table_foreach_remove (elements, free_area, NULL);

  g_hash_table_destroy (elements);
  g_free (mem_chunk->name);
  g_free (mem_chunk);
}

/**
 * gst_mem_chunk_alloc:
 * @mem_chunk: the mem chunk to use
 *
 * Allocate a new memory region from the chunk. The size
 * of the allocated memory was specified when the memchunk
 * was created.
 *
 * Returns: a pointer to the allocated memory region.
 */
gpointer
gst_mem_chunk_alloc (GstMemChunk *mem_chunk)
{
  GstMemChunkElement *chunk;
  
  g_return_val_if_fail (mem_chunk != NULL, NULL);

again:
  chunk = gst_trash_stack_pop (&mem_chunk->stack);
  /* chunk is empty, try to refill */
  if (!chunk) {
    if (populate (mem_chunk))
      goto again;
    else 
      return NULL;
  }

  return GST_MEM_CHUNK_DATA (chunk);
}

/**
 * gst_mem_chunk_alloc0:
 * @mem_chunk: the mem chunk to use
 *
 * Allocate a new memory region from the chunk. The size
 * of the allocated memory was specified when the memchunk
 * was created. The memory will be set to all zeroes.
 *
 * Returns: a pointer to the allocated memory region.
 */
gpointer
gst_mem_chunk_alloc0 (GstMemChunk *mem_chunk)
{
  gpointer mem = gst_mem_chunk_alloc (mem_chunk);

  if (mem)
    memset (mem, 0, mem_chunk->atom_size);
  
  return mem;
}

/**
 * gst_mem_chunk_free:
 * @mem_chunk: the mem chunk to use
 * @mem: the memory region to hand back to the chunk
 *
 * Free the memeory region allocated from the chunk.
 */
void
gst_mem_chunk_free (GstMemChunk *mem_chunk, gpointer mem)
{
  GstMemChunkElement *chunk;
  
  g_return_if_fail (mem_chunk != NULL);
  g_return_if_fail (mem != NULL);

  chunk = GST_MEM_CHUNK_LINK (mem);

  gst_trash_stack_push (&mem_chunk->stack, chunk);
}
