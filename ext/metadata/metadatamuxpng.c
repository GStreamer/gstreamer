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

#include "metadatamuxpng.h"

#include <string.h>

static int
metadatamux_png_reading (PngMuxData * png_data, guint8 ** buf,
    guint32 * bufsize, const guint32 offset, const guint8 * step_buf,
    guint8 ** next_start, guint32 * next_size);

#define READ(buf, size) ( (size)--, *((buf)++) )

void
metadatamux_png_lazy_update (PngMuxData * jpeg_data)
{
  /* nothing to do */
}

void
metadatamux_png_init (PngMuxData * png_data, GstAdapter ** exif_adpt,
    GstAdapter ** iptc_adpt, GstAdapter ** xmp_adpt,
    MetadataChunkArray * strip_chunks, MetadataChunkArray * inject_chunks)
{
  png_data->state = PNG_MUX_NULL;
  png_data->xmp_adapter = xmp_adpt;
  png_data->read = 0;

  png_data->strip_chunks = strip_chunks;
  png_data->inject_chunks = inject_chunks;

}

void
metadatamux_png_dispose (PngMuxData * png_data)
{
  png_data->xmp_adapter = NULL;
}

int
metadatamux_png_parse (PngMuxData * png_data, guint8 * buf,
    guint32 * bufsize, const guint32 offset, guint8 ** next_start,
    guint32 * next_size)
{

  int ret = 0;
  guint8 mark[8];
  const guint8 *step_buf = buf;

  *next_start = buf;

  if (png_data->state == PNG_MUX_NULL) {

    if (*bufsize < 8) {
      *next_size = (buf - *next_start) + 8;
      ret = 1;
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
      ret = -1;
      goto done;
    }

    png_data->state = PNG_MUX_READING;

  }

  /* JUST UNTIL NOT IMPLEMENTED */
  return 0;

  while (ret == 0) {
    switch (png_data->state) {
      case PNG_MUX_READING:
        ret =
            metadatamux_png_reading (png_data, &buf, bufsize,
            offset, step_buf, next_start, next_size);
        break;
      case PNG_MUX_DONE:
        goto done;
        break;
      default:
        ret = -1;
        break;
    }
  }

done:

  return ret;

}


/* look for markers */
static int
metadatamux_png_reading (PngMuxData * png_data, guint8 ** buf,
    guint32 * bufsize, const guint32 offset, const guint8 * step_buf,
    guint8 ** next_start, guint32 * next_size)
{

  int ret = -1;
  guint8 mark[4];
  guint32 chunk_size = 0;
  MetadataChunk chunk;

  static const char XmpHeader[] = "XML:com.adobe.xmp";

  *next_start = *buf;

  if (*bufsize < 8) {
    *next_size = (*buf - *next_start) + 8;
    ret = 1;
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

  if (!(mark[0] == 'I' && mark[1] == 'H' && mark[2] == 'D' && mark[3] == 'R')) {
    ret = -1;
    png_data->state = PNG_MUX_NULL;
    goto done;
  }

  /* always inject after first chunk (IHDR) */

  memset (&chunk, 0x00, sizeof (MetadataChunk));
  /* 8(header) + 4(size) +4(id) + chunksize + 4(crc) */
  chunk.offset_orig = chunk_size + 20;
  chunk.type = MD_CHUNK_XMP;

  metadata_chunk_array_append_sorted (png_data->inject_chunks, &chunk);

  png_data->state = PNG_MUX_DONE;
  ret = 0;

done:

  return ret;


}
