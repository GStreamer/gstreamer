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

#include "metadatamuxjpeg.h"

#include <string.h>

#include <libiptcdata/iptc-jpeg.h>

static int
metadatamux_jpeg_reading (JpegMuxData * jpeg_data, guint8 ** buf,
    guint32 * bufsize, const guint32 offset, const guint8 * step_buf,
    guint8 ** next_start, guint32 * next_size);

#define READ(buf, size) ( (size)--, *((buf)++) )

static void
metadatamux_wrap_chunk (MetadataChunk * chunk, guint8 * buf, guint32 buf_size,
    guint8 a, guint8 b)
{
  guint8 *data = g_new (guint8, 4 + buf_size + chunk->size);

  memcpy (data + 4 + buf_size, chunk->data, chunk->size);
  g_free (chunk->data);
  chunk->data = data;
  chunk->size += 4 + buf_size;
  data[0] = a;
  data[1] = b;
  data[2] = (chunk->size - 2) >> 8;
  data[3] = (chunk->size - 2) & 0x00FF;
  if (buf && buf_size) {
    memcpy (data + 4, buf, buf_size);
  }
}

void
metadatamux_jpeg_lazy_update (JpegMuxData * jpeg_data)
{
  gsize i;
  gboolean has_exif = FALSE;

  for (i = 0; i < jpeg_data->inject_chunks->len; ++i) {
    if (jpeg_data->inject_chunks->chunk[i].size > 0 &&
        jpeg_data->inject_chunks->chunk[i].data) {
      switch (jpeg_data->inject_chunks->chunk[i].type) {
        case MD_CHUNK_EXIF:
          metadatamux_wrap_chunk (&jpeg_data->inject_chunks->chunk[i], NULL, 0,
              0xFF, 0xE1);
          has_exif = TRUE;
          break;
        case MD_CHUNK_IPTC:
        {
          unsigned int size = jpeg_data->inject_chunks->chunk[i].size + 256;
          unsigned char *buf = g_new (guint8, size);

          size = iptc_jpeg_ps3_save_iptc (NULL, 0,
              jpeg_data->inject_chunks->chunk[i].data,
              jpeg_data->inject_chunks->chunk[i].size, buf, size);
          if (size > 0) {
            g_free (jpeg_data->inject_chunks->chunk[i].data);
            jpeg_data->inject_chunks->chunk[i].data = buf;
            jpeg_data->inject_chunks->chunk[i].size = size;
            metadatamux_wrap_chunk (&jpeg_data->inject_chunks->chunk[i], NULL,
                0, 0xFF, 0xED);
          } else {
            GST_ERROR ("Invalid IPTC chunk\n");
            g_free (buf);
            /* FIXME: remove entry from list */
          }
        }
          break;
        case MD_CHUNK_XMP:
        {
          static const char XmpHeader[] = "http://ns.adobe.com/xap/1.0/";

          metadatamux_wrap_chunk (&jpeg_data->inject_chunks->chunk[i],
              XmpHeader, sizeof (XmpHeader), 0xFF, 0xE1);
        }
          break;
        default:
          break;
      }
    }
  }
  if (!has_exif) {
    /* EXIF not injected so not strip JFIF anymore */
    metadata_chunk_array_clear (jpeg_data->strip_chunks);
  }

}

void
metadatamux_jpeg_init (JpegMuxData * jpeg_data, GstAdapter ** exif_adpt,
    GstAdapter ** iptc_adpt, GstAdapter ** xmp_adpt,
    MetadataChunkArray * strip_chunks, MetadataChunkArray * inject_chunks)
{
  jpeg_data->state = JPEG_MUX_NULL;

  jpeg_data->strip_chunks = strip_chunks;
  jpeg_data->inject_chunks = inject_chunks;

}

void
metadatamux_jpeg_dispose (JpegMuxData * jpeg_data)
{
  jpeg_data->inject_chunks = NULL;
  jpeg_data->strip_chunks = NULL;

  jpeg_data->state = JPEG_MUX_NULL;
}

int
metadatamux_jpeg_parse (JpegMuxData * jpeg_data, guint8 * buf,
    guint32 * bufsize, const guint32 offset, guint8 ** next_start,
    guint32 * next_size)
{

  int ret = 0;
  guint8 mark[2] = { 0x00, 0x00 };
  const guint8 *step_buf = buf;

  *next_start = buf;

  if (jpeg_data->state == JPEG_MUX_NULL) {

    if (*bufsize < 2) {
      *next_size = (buf - *next_start) + 2;
      ret = 1;
      goto done;
    }

    mark[0] = READ (buf, *bufsize);
    mark[1] = READ (buf, *bufsize);

    if (mark[0] != 0xFF || mark[1] != 0xD8) {
      ret = -1;
      goto done;
    }

    jpeg_data->state = JPEG_MUX_READING;

  }

  while (ret == 0) {
    switch (jpeg_data->state) {
      case JPEG_MUX_READING:
        ret =
            metadatamux_jpeg_reading (jpeg_data, &buf, bufsize,
            offset, step_buf, next_start, next_size);
        break;
      case JPEG_MUX_DONE:
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
metadatamux_jpeg_reading (JpegMuxData * jpeg_data, guint8 ** buf,
    guint32 * bufsize, const guint32 offset, const guint8 * step_buf,
    guint8 ** next_start, guint32 * next_size)
{

  int ret = -1;
  guint8 mark[2] = { 0x00, 0x00 };
  guint16 chunk_size = 0;
  gint64 new_chunk_offset = 0;
  MetadataChunk chunk;
  gboolean jfif_found = FALSE;

  static const char JfifHeader[] = "JFIF";
  static const unsigned char ExifHeader[] =
      { 0x45, 0x78, 0x69, 0x66, 0x00, 0x00 };
  static const char IptcHeader[] = "Photoshop 3.0";
  static const char XmpHeader[] = "http://ns.adobe.com/xap/1.0/";

  *next_start = *buf;

  if (*bufsize < 2) {
    *next_size = (*buf - *next_start) + 2;
    ret = 1;
    goto done;
  }

  mark[0] = READ (*buf, *bufsize);
  mark[1] = READ (*buf, *bufsize);

  if (mark[0] == 0xFF) {

    chunk_size = READ (*buf, *bufsize) << 8;
    chunk_size += READ (*buf, *bufsize);

    if (mark[1] == 0xE0) {      /* may be JFIF */

      if (chunk_size >= 16) {
        if (*bufsize < 5) {
          *next_size = (*buf - *next_start) + 5;
          ret = 1;
          goto done;
        }

        if (0 == memcmp (JfifHeader, *buf, 5)) {
          jfif_found = TRUE;
        }
      } else {
        /* FIXME: should we check if the first chunk is EXIF? */
      }

    }

    if (!jfif_found) {
      ret = -1;
      goto done;
    }

    new_chunk_offset = 2;

    /* EXIF will always be in the begining */

    memset (&chunk, 0x00, sizeof (MetadataChunk));
    chunk.offset_orig = 2;
    chunk.type = MD_CHUNK_EXIF;
    metadata_chunk_array_append_sorted (jpeg_data->inject_chunks, &chunk);

    if (jfif_found) {
      /* remove JFIF chunk */
      /* this acation can be canceled with lazy update if no Exif is add */

      memset (&chunk, 0x00, sizeof (MetadataChunk));
      chunk.offset_orig = 2;
      chunk.size = chunk_size + 2;      /* chunk size plus app marker */
      chunk.type = MD_CHUNK_UNKNOWN;

      metadata_chunk_array_append_sorted (jpeg_data->strip_chunks, &chunk);

      new_chunk_offset = chunk.offset_orig + chunk.size;
    }

    /* IPTC */

    memset (&chunk, 0x00, sizeof (MetadataChunk));
    chunk.offset_orig = new_chunk_offset;
    chunk.type = MD_CHUNK_IPTC;
    metadata_chunk_array_append_sorted (jpeg_data->inject_chunks, &chunk);

    /* XMP */

    memset (&chunk, 0x00, sizeof (MetadataChunk));
    chunk.offset_orig = new_chunk_offset;
    chunk.type = MD_CHUNK_XMP;
    metadata_chunk_array_append_sorted (jpeg_data->inject_chunks, &chunk);

    jpeg_data->state = JPEG_MUX_DONE;
    ret = 0;

  } else {
    /* invalid JPEG chunk */
    ret = -1;
  }


done:

  return ret;


}
