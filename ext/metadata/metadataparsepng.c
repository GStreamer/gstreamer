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

#include "metadataparsepng.h"

#include <string.h>

static MetadataParsingReturn
metadataparse_png_reading (PngParseData * png_data, guint8 ** buf,
    guint32 * bufsize, const guint32 offset, const guint8 * step_buf,
    guint8 ** next_start, guint32 * next_size);

static MetadataParsingReturn
metadataparse_png_xmp (PngParseData * png_data, guint8 ** buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size);

static MetadataParsingReturn
metadataparse_png_jump (PngParseData * png_data, guint8 ** buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size);

#define READ(buf, size) ( (size)--, *((buf)++) )

void
metadataparse_png_lazy_update (PngParseData * jpeg_data)
{
  /* nothing to do */
}

void
metadataparse_png_init (PngParseData * png_data, GstAdapter ** exif_adpt,
    GstAdapter ** iptc_adpt, GstAdapter ** xmp_adpt,
    MetadataChunkArray * strip_chunks, MetadataChunkArray * inject_chunks)
{
  png_data->state = PNG_PARSE_NULL;
  png_data->xmp_adapter = xmp_adpt;
  png_data->read = 0;

  png_data->strip_chunks = strip_chunks;
  png_data->inject_chunks = inject_chunks;

}

void
metadataparse_png_dispose (PngParseData * png_data)
{
  png_data->xmp_adapter = NULL;
}

MetadataParsingReturn
metadataparse_png_parse (PngParseData * png_data, guint8 * buf,
    guint32 * bufsize, const guint32 offset, guint8 ** next_start,
    guint32 * next_size)
{

  int ret = META_PARSING_DONE;
  guint8 mark[8];
  const guint8 *step_buf = buf;

  *next_start = buf;

  if (png_data->state == PNG_PARSE_NULL) {

    if (*bufsize < 8) {
      *next_size = (buf - *next_start) + 8;
      ret = META_PARSING_NEED_MORE_DATA;
      goto done;
    }

    mark[0] = READ (buf, *bufsize);
    mark[1] = READ (buf, *bufsize);
    mark[2] = READ (buf, *bufsize);
    mark[3] = READ (buf, *bufsize);
    mark[4] = READ (buf, *bufsize);
    mark[5] = READ (buf, *bufsize);
    mark[6] = READ (buf, *bufsize);
    mark[7] = READ (buf, *bufsize);

    if (mark[0] != 0x89 || mark[1] != 0x50 || mark[2] != 0x4E || mark[3] != 0x47
        || mark[4] != 0x0D || mark[5] != 0x0A || mark[6] != 0x1A
        || mark[7] != 0x0A) {
      ret = META_PARSING_ERROR;
      goto done;
    }

    png_data->state = PNG_PARSE_READING;

  }

  while (ret == META_PARSING_DONE) {
    switch (png_data->state) {
      case PNG_PARSE_READING:
        ret =
            metadataparse_png_reading (png_data, &buf, bufsize,
            offset, step_buf, next_start, next_size);
        break;
      case PNG_PARSE_JUMPING:
        ret =
            metadataparse_png_jump (png_data, &buf, bufsize, next_start,
            next_size);
        break;
      case PNG_PARSE_XMP:
        ret =
            metadataparse_png_xmp (png_data, &buf, bufsize, next_start,
            next_size);
        break;
      case PNG_PARSE_DONE:
        goto done;
        break;
      default:
        ret = META_PARSING_ERROR;
        break;
    }
  }

done:

  return ret;

}


/* look for markers */
static MetadataParsingReturn
metadataparse_png_reading (PngParseData * png_data, guint8 ** buf,
    guint32 * bufsize, const guint32 offset, const guint8 * step_buf,
    guint8 ** next_start, guint32 * next_size)
{

  int ret = META_PARSING_ERROR;
  guint8 mark[4];
  guint32 chunk_size = 0;

  static const char XmpHeader[] = "XML:com.adobe.xmp";

  *next_start = *buf;

  if (*bufsize < 8) {
    *next_size = (*buf - *next_start) + 8;
    ret = META_PARSING_NEED_MORE_DATA;
    goto done;
  }

  chunk_size = READ (*buf, *bufsize) << 24;
  chunk_size += READ (*buf, *bufsize) << 16;
  chunk_size += READ (*buf, *bufsize) << 8;
  chunk_size += READ (*buf, *bufsize);

  mark[0] = READ (*buf, *bufsize);
  mark[1] = READ (*buf, *bufsize);
  mark[2] = READ (*buf, *bufsize);
  mark[3] = READ (*buf, *bufsize);

  if (mark[0] == 'I' && mark[1] == 'E' && mark[2] == 'N' && mark[3] == 'D') {
    ret = META_PARSING_DONE;
    png_data->state = PNG_PARSE_DONE;
    goto done;
  }

  if (mark[0] == 'i' && mark[1] == 'T' && mark[2] == 'X' && mark[3] == 't') {
    if (chunk_size >= 22) {     /* "XML:com.adobe.xmp" plus some flags */
      if (*bufsize < 22) {
        *next_size = (*buf - *next_start) + 22;
        ret = META_PARSING_NEED_MORE_DATA;
        goto done;
      }

      if (0 == memcmp (XmpHeader, *buf, 18)) {
        MetadataChunk chunk;

        memset (&chunk, 0x00, sizeof (MetadataChunk));
        chunk.offset_orig = (*buf - step_buf) + offset - 8;     /* maker + size */
        chunk.size = chunk_size + 12;   /* chunk size plus app marker plus crc */
        chunk.type = MD_CHUNK_XMP;

        metadata_chunk_array_append_sorted (png_data->strip_chunks, &chunk);

        /* if adapter has been provided, prepare to hold chunk */
        if (png_data->xmp_adapter) {
          *buf += 22;           /* jump "XML:com.adobe.xmp" plus some flags */
          *bufsize -= 22;
          png_data->read = chunk_size - 22;     /* four CRC bytes at the end will be jumped after */
          png_data->state = PNG_PARSE_XMP;
          ret = META_PARSING_DONE;
          goto done;
        }
      }
    }
  }

  /* just set jump sise  */
  png_data->read = chunk_size + 4;      /* four CRC bytes at the end */
  png_data->state = PNG_PARSE_JUMPING;
  ret = META_PARSING_DONE;

done:

  return ret;


}

static MetadataParsingReturn
metadataparse_png_jump (PngParseData * png_data, guint8 ** buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size)
{
  png_data->state = PNG_PARSE_READING;
  return metadataparse_util_jump_chunk (&png_data->read, buf,
      bufsize, next_start, next_size);
}

static MetadataParsingReturn
metadataparse_png_xmp (PngParseData * png_data, guint8 ** buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size)
{
  int ret;

  ret = metadataparse_util_hold_chunk (&png_data->read, buf,
      bufsize, next_start, next_size, png_data->xmp_adapter);
  if (ret == META_PARSING_DONE) {
    /* jump four CRC bytes at the end of chunk */
    png_data->read = 4;
    png_data->state = PNG_PARSE_JUMPING;
    /* if there is a second XMP chunk in the file it will be jumped */
    png_data->xmp_adapter = NULL;
  }
  return ret;

}
