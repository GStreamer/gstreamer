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

#include <string.h>

#include "metadataparse.h"

/*
 *static declarations
 */

static int
metadataparse_parse_none (ParseData * parse_data, const guint8 * buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size);

/*
 * extern implementation
 */

void
metadataparse_init (ParseData * parse_data)
{
  parse_data->state = STATE_NULL;
  parse_data->img_type = IMG_NONE;
  parse_data->option = PARSE_OPT_ALL;
  parse_data->offset_orig = 0;
  parse_data->exif_adapter = NULL;
  parse_data->iptc_adapter = NULL;
  parse_data->xmp_adapter = NULL;
  metadata_chunk_array_init (&parse_data->strip_chunks, 4);
  metadata_chunk_array_init (&parse_data->inject_chunks, 1);
}

/*
 * offset: number of bytes to jump (just a hint to jump a chunk)
 * size: number of bytes to read on next call (just a hint to get all chunk)
 * return:
 *   -1 -> error
 *    0 -> done
 *    1 -> need more data
 */
int
metadataparse_parse (ParseData * parse_data, const guint8 * buf,
    guint32 bufsize, guint32 * next_offset, guint32 * next_size)
{

  int ret = 0;

  guint8 *next_start = (guint8 *) buf;

  if (parse_data->state == STATE_NULL) {
    ret =
        metadataparse_parse_none (parse_data, buf, &bufsize, &next_start,
        next_size);
    if (ret == 0)
      parse_data->state = STATE_READING;
    else
      goto done;
  }

  switch (parse_data->img_type) {
    case IMG_JPEG:
      ret =
          metadataparse_jpeg_parse (&parse_data->format_data.jpeg,
          (guint8 *) buf, &bufsize, parse_data->offset_orig, &next_start,
          next_size);
      break;
    case IMG_PNG:
      ret =
          metadataparse_png_parse (&parse_data->format_data.png,
          (guint8 *) buf, &bufsize, parse_data->offset_orig, &next_start,
          next_size);
      break;
    default:
      /* unexpected */
      ret = -1;
      goto done;
      break;
  }

  *next_offset = next_start - buf;
  parse_data->offset_orig += *next_offset;

done:

  if (ret == 0) {
    parse_data->state = STATE_DONE;
  }

  return ret;
}

void
metadataparse_dispose (ParseData * parse_data)
{

  switch (parse_data->img_type) {
    case IMG_JPEG:
      metadataparse_jpeg_dispose (&parse_data->format_data.jpeg);
      break;
    case IMG_PNG:
      metadataparse_png_dispose (&parse_data->format_data.png);
      break;
  }

  metadata_chunk_array_free (&parse_data->strip_chunks);
  metadata_chunk_array_free (&parse_data->inject_chunks);

  if (parse_data->xmp_adapter) {
    gst_object_unref (parse_data->xmp_adapter);
    parse_data->xmp_adapter = NULL;
  }

  if (parse_data->iptc_adapter) {
    gst_object_unref (parse_data->iptc_adapter);
    parse_data->iptc_adapter = NULL;
  }

  if (parse_data->exif_adapter) {
    gst_object_unref (parse_data->exif_adapter);
    parse_data->exif_adapter = NULL;
  }

}

/*
 * static implementation
 */

/* find image type */
static int
metadataparse_parse_none (ParseData * parse_data, const guint8 * buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size)
{

  int ret = -1;
  GstAdapter **exif = NULL;
  GstAdapter **iptc = NULL;
  GstAdapter **xmp = NULL;

  *next_start = (guint8 *) buf;

  parse_data->img_type = IMG_NONE;

  if (*bufsize < 3) {
    *next_size = 3;
    ret = 1;
    goto done;
  }

  if (parse_data->option & PARSE_OPT_EXIF)
    exif = &parse_data->exif_adapter;
  if (parse_data->option & PARSE_OPT_IPTC)
    iptc = &parse_data->iptc_adapter;
  if (parse_data->option & PARSE_OPT_XMP)
    xmp = &parse_data->xmp_adapter;

  if (buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF) {
    metadataparse_jpeg_init (&parse_data->format_data.jpeg, exif, iptc, xmp,
        &parse_data->strip_chunks, &parse_data->inject_chunks);
    ret = 0;
    parse_data->img_type = IMG_JPEG;
    goto done;
  }

  if (*bufsize < 8) {
    *next_size = 8;
    ret = 1;
    goto done;
  }

  if (buf[0] == 0x89 && buf[1] == 0x50 && buf[2] == 0x4E && buf[3] == 0x47 &&
      buf[4] == 0x0D && buf[5] == 0x0A && buf[6] == 0x1A && buf[7] == 0x0A) {
    metadataparse_png_init (&parse_data->format_data.png, exif, iptc, xmp,
        &parse_data->strip_chunks, &parse_data->inject_chunks);
    ret = 0;
    parse_data->img_type = IMG_PNG;
    goto done;
  }

done:

  return ret;
}
