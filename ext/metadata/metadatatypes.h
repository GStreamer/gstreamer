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

#ifndef __METADATATYPES_H__
#define __METADATATYPES_H__

/*
 * includes
 */

#include <glib.h>

G_BEGIN_DECLS
/*
 * enum and types
 */
/* *INDENT-OFF* */

typedef enum _tag_MetadataParsingReturn {
  META_PARSING_ERROR          = -1,
  META_PARSING_DONE           = 0,
  META_PARSING_NEED_MORE_DATA = 1
} MetadataParsingReturn;

/* *INDENT-ON* */
    typedef enum _tag_MetadataChunkType
{
  MD_CHUNK_UNKNOWN,
  MD_CHUNK_EXIF,
  MD_CHUNK_IPTC,
  MD_CHUNK_XMP
} MetadataChunkType;

typedef struct _tag_MetadataChunk
{
  gint64 offset_orig;           /* from the beginning of original file */
  /*here just for convinience (filled by element) offset in new stream */
  gint64 offset;
  guint32 size;                 /* chunk or buffer size */
  guint8 *data;
  MetadataChunkType type;       /* used by mux to see what tags to insert here */
} MetadataChunk;

typedef struct _tag_MetadataChunkArray
{
  MetadataChunk *chunk;
  gsize len;                    /* number of chunks into aray */
  gsize allocated_len;          /* number of slots into the array to store chunks */
} MetadataChunkArray;

/*
 * external function prototypes
 */

extern void
metadata_chunk_array_init (MetadataChunkArray * array, gsize alloc_size);

extern void metadata_chunk_array_free (MetadataChunkArray * array);

extern void metadata_chunk_array_clear (MetadataChunkArray * array);

extern void
metadata_chunk_array_append (MetadataChunkArray * array, MetadataChunk * chunk);

extern void
metadata_chunk_array_append_sorted (MetadataChunkArray * array,
    MetadataChunk * chunk);

extern void metadata_chunk_array_remove_zero_size (MetadataChunkArray * array);

extern void
metadata_chunk_array_remove_by_index (MetadataChunkArray * array, guint32 i);

G_END_DECLS
#endif /* __METADATATYPES_H__ */
