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

#include "metadatatypes.h"

#include <string.h>

void
metadata_chunk_array_init (MetadataChunkArray * array, gsize alloc_size)
{
  array->len = 0;
  array->chunk = g_new0 (MetadataChunk, alloc_size);
  array->allocated_len = alloc_size;
}

void
metadata_chunk_array_free (MetadataChunkArray * array)
{
  metadata_chunk_array_clear (array);
  array->allocated_len = 0;
  if (array->chunk) {
    g_free (array->chunk);
    array->chunk = NULL;
  }
}

void
metadata_chunk_array_clear (MetadataChunkArray * array)
{
  while (array->len) {
    array->len--;
    if (array->chunk[array->len].data) {
      g_free (array->chunk[array->len].data);
    }
  }
}

void
metadata_chunk_array_append (MetadataChunkArray * array, MetadataChunk * chunk)
{
  if (array->len == array->allocated_len) {
    array->allocated_len += 2;
    array->chunk = g_realloc (array->chunk, array->allocated_len);
  }
  memcpy (&array->chunk[array->len], chunk, sizeof (MetadataChunk));
  ++array->len;
}

void
metadata_chunk_array_append_sorted (MetadataChunkArray * array,
    MetadataChunk * chunk)
{
  gint32 i, pos;

  if (array->len == array->allocated_len) {
    array->allocated_len += 2;
    array->chunk = g_realloc (array->chunk, array->allocated_len);
  }
  pos = array->len;
  for (i = array->len - 1; i >= 0; --i) {
    if (chunk->offset_orig > array->chunk[i].offset_orig) {
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
