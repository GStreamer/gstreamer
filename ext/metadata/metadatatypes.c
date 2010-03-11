/*
 * GStreamer
 * Copyright 2007 Edgard Lima <edgard.lima@indt.org.br>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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

/*
 * SECTION: metadatatypes
 * @short_description: This module contains function to operates a list of
 * chunks
 *
 * Last reviewed on 2008-01-24 (0.10.15)
 */

/*
 * includes
 */

#include "metadatatypes.h"

#include <string.h>

/*
 * extern functions implementations
 */

/*
 * metadata_chunk_array_init:
 * @array: an array of chunks
 * @alloc_size: number of chunks that can be added to the array without futher
 * allocation
 *
 * Call this function before any other function in this module.
 * Nerver call this function a second time without call
 * #metadata_chunk_array_free beteween them
 * An example of use is:
 * int test() { MetadataChunkArray a; metadata_chunk_array_init(&a, 1); ... }
 *
 * Returns: nothing
 */

void
metadata_chunk_array_init (MetadataChunkArray * array, gsize alloc_size)
{
  array->len = 0;
  array->chunk = g_new0 (MetadataChunk, alloc_size);
  array->allocated_len = alloc_size;
}

/*
 * metadata_chunk_array_free:
 * @array: an array of chunks
 *
 * Call this function after have finished using the @array to free any internal
 * memory alocated by it. 
 *
 * Returns: nothing
 */

void
metadata_chunk_array_free (MetadataChunkArray * array)
{
  metadata_chunk_array_clear (array);
  array->allocated_len = 0;
  g_free (array->chunk);
  array->chunk = NULL;
}

/*
 * metadata_chunk_array_clear:
 * @array: an array of chunks
 *
 * Free memory allocated internally by each chunk and set the @array->len to 0
 * (zero). So, the number of chunks into the array will be zero,
 * but the number of slots into the array to strore chunks will be kept
 *
 * Returns: nothing
 */

void
metadata_chunk_array_clear (MetadataChunkArray * array)
{
  while (array->len) {
    array->len--;
    g_free (array->chunk[array->len].data);
  }
}

/*
 * metadata_chunk_array_append:
 * @array: an array of chunks
 * @chunk: chunk to be append
 *
 * Just append a @chunk to the end of the @array. The @array now will be the
 * owner of @chunk->data. Just call this function if you a sure the @array 
 * chunks will be sorted by @chunk->offset_orig anyway.
 * @see_also: #metadata_chunk_array_append_sorted
 *
 * Returns: nothing
 */

void
metadata_chunk_array_append (MetadataChunkArray * array, MetadataChunk * chunk)
{
  if (array->len == array->allocated_len) {
    array->allocated_len += 2;
    array->chunk =
        g_realloc (array->chunk, sizeof (MetadataChunk) * array->allocated_len);
  }
  memcpy (&array->chunk[array->len], chunk, sizeof (MetadataChunk));
  ++array->len;
}

/*
 * metadata_chunk_array_append_sorted:
 * @array: an array of chunks
 * @chunk: chunk to be append
 *
 * Append a @chunk sorted by @chunk->offset_orig the @array. The @array now
 * will be the owner of @chunk->data. This function supposes that @array 
 * is already sorted by @chunk->offset_orig.
 * @see_also: #metadata_chunk_array_append
 *
 * Returns: nothing
 */

void
metadata_chunk_array_append_sorted (MetadataChunkArray * array,
    MetadataChunk * chunk)
{
  gint32 i, pos;

  if (array->len == array->allocated_len) {
    array->allocated_len += 2;
    array->chunk =
        g_realloc (array->chunk, sizeof (MetadataChunk) * array->allocated_len);
  }
  pos = array->len;
  for (i = array->len - 1; i >= 0; --i) {
    if (chunk->offset_orig >= array->chunk[i].offset_orig) {
      break;
    }
  }
  pos = i + 1;
  if (pos < array->len) {
    memmove (&array->chunk[pos + 1], &array->chunk[pos],
        sizeof (MetadataChunk) * (array->len - pos));
  }
  memcpy (&array->chunk[pos], chunk, sizeof (MetadataChunk));
  ++array->len;

  return;

}

/*
 * metadata_chunk_array_remove_zero_size:
 * @array: an array of chunks
 *
 * This function removes all the chunks in @array that has 'chunk.size == 0'.
 * It is possible to have the 'chunk.data==NULL' and 'chunk.size != 0', those
 * chunks are used by muxer for lazy 'filling' and are not removed by this
 * function.
 *
 * Returns: nothing
 */

void
metadata_chunk_array_remove_zero_size (MetadataChunkArray * array)
{

  int i;

  for (i = 0; i < array->len;) {
    if (array->chunk[i].size == 0) {
      if (i < --array->len) {
        memmove (&array->chunk[i], &array->chunk[i + 1],
            sizeof (MetadataChunk) * (array->len - i));
      }
    } else {
      ++i;
    }
  }
}

/*
 * metadata_chunk_array_remove_by_index:
 * @array: an array of chunks
 * @i: index of chunk to be removed
 *
 * This function removes the chunk at index @i from @array
 *
 * Returns: nothing
 */

void
metadata_chunk_array_remove_by_index (MetadataChunkArray * array, guint32 i)
{

  if (i < array->len) {
    g_free (array->chunk[i].data);
    if (i < --array->len) {
      memmove (&array->chunk[i], &array->chunk[i + 1],
          sizeof (MetadataChunk) * (array->len - i));
    }
  }
}
