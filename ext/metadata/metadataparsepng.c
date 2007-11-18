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

static int
metadataparse_png_reading (PngData * png_data, guint8 ** buf,
    guint32 * bufsize, const guint32 offset, const guint8 * step_buf,
    guint8 ** next_start, guint32 * next_size);

static int
metadataparse_png_xmp (PngData * png_data, guint8 ** buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size);

static int
metadataparse_png_jump (PngData * png_data, guint8 ** buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size);

#define READ(buf, size) ( (size)--, *((buf)++) )

void
metadataparse_png_init (PngData * png_data, MetadataChunk * exif,
    MetadataChunk * iptc, MetadataChunk * xmp)
{
  png_data->state = PNG_NULL;
  png_data->xmp = xmp;
  png_data->read = 0;

  metadataparse_xmp_init ();
}

void
metadataparse_png_dispose (PngData * png_data)
{
  metadataparse_xmp_dispose ();

  png_data->xmp = NULL;
}

int
metadataparse_png_parse (PngData * png_data, guint8 * buf,
    guint32 * bufsize, const guint32 offset, guint8 ** next_start,
    guint32 * next_size)
{

  int ret = 0;
  guint8 mark[8];
  const guint8 *step_buf = buf;

  *next_start = buf;

  if (png_data->state == PNG_NULL) {

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

    png_data->state = PNG_READING;

  }

  while (ret == 0) {
    switch (png_data->state) {
      case PNG_READING:
        ret =
            metadataparse_png_reading (png_data, &buf, bufsize,
            offset, step_buf, next_start, next_size);
        break;
      case PNG_JUMPING:
        ret =
            metadataparse_png_jump (png_data, &buf, bufsize, next_start,
            next_size);
        break;
      case PNG_XMP:
        ret =
            metadataparse_png_xmp (png_data, &buf, bufsize, next_start,
            next_size);
        break;
      case PNG_DONE:
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
metadataparse_png_reading (PngData * png_data, guint8 ** buf,
    guint32 * bufsize, const guint32 offset, const guint8 * step_buf,
    guint8 ** next_start, guint32 * next_size)
{

  int ret = -1;
  guint8 mark[4];
  guint32 chunk_size = 0;

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

  if (mark[0] == 'I' && mark[1] == 'E' && mark[2] == 'N' && mark[3] == 'D') {
    ret = 0;
    png_data->state = PNG_DONE;
    goto done;
  }

  if (mark[0] == 'i' && mark[1] == 'T' && mark[2] == 'X' && mark[3] == 't') {
    if (chunk_size >= 22) {     /* "XML:com.adobe.xmp" plus some flags */
      if (*bufsize < 22) {
        *next_size = (*buf - *next_start) + 22;
        ret = 1;
        goto done;
      }

      if (png_data->xmp) {
        if (0 == memcmp (XmpHeader, *buf, 18)) {
          png_data->xmp->offset = (*buf - step_buf) + offset - 8;       /* maker + size */
          png_data->xmp->size = chunk_size + 12;        /* chunk size plus app marker plus crc */
          *buf += 22;           /* jump "XML:com.adobe.xmp" plus some flags */
          *bufsize -= 22;
          png_data->read = chunk_size - 22;     /* four CRC bytes at the end will be jumped after */
          png_data->state = PNG_XMP;
          ret = 0;
          goto done;
        }
      }
    }
  }

  /* just set jump sise  */
  png_data->read = chunk_size + 4;      /* four CRC bytes at the end */
  png_data->state = PNG_JUMPING;
  ret = 0;

done:

  return ret;


}

static int
metadataparse_png_jump (PngData * png_data, guint8 ** buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size)
{
  png_data->state = PNG_READING;
  return metadataparse_util_jump_chunk (&png_data->read, buf,
      bufsize, next_start, next_size);
}

static int
metadataparse_png_xmp (PngData * png_data, guint8 ** buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size)
{
  int ret;

  ret = metadataparse_util_hold_chunk (&png_data->read, buf,
      bufsize, next_start, next_size, &png_data->xmp->adapter);
  if (ret == 0) {
    /* jump four CRC bytes at the end of chunk */
    png_data->read = 4;
    png_data->state = PNG_JUMPING;
    /* if there is a second XMP chunk in the file it will be jumped */
    png_data->xmp = NULL;
  }
  return ret;

}
