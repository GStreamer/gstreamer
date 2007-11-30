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

#include "metadata.h"

/*
 *static declarations
 */

static int
metadata_parse_none (MetaData * meta_data, const guint8 * buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size);

/*
 * extern implementation
 */

void
metadata_init (MetaData * meta_data, gboolean parse)
{
  meta_data->state = STATE_NULL;
  meta_data->img_type = IMG_NONE;
  meta_data->option = META_OPT_ALL;
  meta_data->offset_orig = 0;
  meta_data->exif_adapter = NULL;
  meta_data->iptc_adapter = NULL;
  meta_data->xmp_adapter = NULL;
  meta_data->parse = parse;

  if (parse) {
    metadata_chunk_array_init (&meta_data->strip_chunks, 4);
    metadata_chunk_array_init (&meta_data->inject_chunks, 1);
  } else {
    metadata_chunk_array_init (&meta_data->strip_chunks, 1);
    metadata_chunk_array_init (&meta_data->inject_chunks, 3);
  }

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
metadata_parse (MetaData * meta_data, const guint8 * buf,
    guint32 bufsize, guint32 * next_offset, guint32 * next_size)
{

  int ret = 0;

  guint8 *next_start = (guint8 *) buf;

  if (meta_data->state == STATE_NULL) {
    ret =
        metadata_parse_none (meta_data, buf, &bufsize, &next_start, next_size);
    if (ret == 0)
      meta_data->state = STATE_READING;
    else
      goto done;
  }

  switch (meta_data->img_type) {
    case IMG_JPEG:
      if (G_LIKELY (meta_data->parse))
        ret =
            metadataparse_jpeg_parse (&meta_data->format_data.jpeg_parse,
            (guint8 *) buf, &bufsize, meta_data->offset_orig, &next_start,
            next_size);
      else
        ret =
            metadatamux_jpeg_parse (&meta_data->format_data.jpeg_mux,
            (guint8 *) buf, &bufsize, meta_data->offset_orig, &next_start,
            next_size);
      break;
    case IMG_PNG:
      if (G_LIKELY (meta_data->parse))
        ret =
            metadataparse_png_parse (&meta_data->format_data.png_parse,
            (guint8 *) buf, &bufsize, meta_data->offset_orig, &next_start,
            next_size);
      /*
         else
         ret =
         metadatamux_png_parse (&meta_data->format_data.png_mux,
         (guint8 *) buf, &bufsize, meta_data->offset_orig, &next_start,
         next_size);
       */
      break;
    default:
      /* unexpected */
      ret = -1;
      goto done;
      break;
  }

  *next_offset = next_start - buf;
  meta_data->offset_orig += *next_offset;

done:

  if (ret == 0) {
    meta_data->state = STATE_DONE;
  }

  return ret;
}

void
metadata_dispose (MetaData * meta_data)
{

  switch (meta_data->img_type) {
    case IMG_JPEG:
      if (G_LIKELY (meta_data->parse))
        metadataparse_jpeg_dispose (&meta_data->format_data.jpeg_parse);
      else
        metadatamux_jpeg_dispose (&meta_data->format_data.jpeg_mux);
      break;
    case IMG_PNG:
      if (G_LIKELY (meta_data->parse))
        metadataparse_png_dispose (&meta_data->format_data.png_parse);
      /*
         else
         metadatamux_png_dispose (&meta_data->format_data.png_mux);
       */
      break;
  }

  metadata_chunk_array_free (&meta_data->strip_chunks);
  metadata_chunk_array_free (&meta_data->inject_chunks);

  if (meta_data->xmp_adapter) {
    gst_object_unref (meta_data->xmp_adapter);
    meta_data->xmp_adapter = NULL;
  }

  if (meta_data->iptc_adapter) {
    gst_object_unref (meta_data->iptc_adapter);
    meta_data->iptc_adapter = NULL;
  }

  if (meta_data->exif_adapter) {
    gst_object_unref (meta_data->exif_adapter);
    meta_data->exif_adapter = NULL;
  }

}

/*
 * static implementation
 */

/* find image type */
static int
metadata_parse_none (MetaData * meta_data, const guint8 * buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size)
{

  int ret = -1;
  GstAdapter **exif = NULL;
  GstAdapter **iptc = NULL;
  GstAdapter **xmp = NULL;

  *next_start = (guint8 *) buf;

  meta_data->img_type = IMG_NONE;

  if (*bufsize < 3) {
    *next_size = 3;
    ret = 1;
    goto done;
  }

  if (meta_data->option & META_OPT_EXIF)
    exif = &meta_data->exif_adapter;
  if (meta_data->option & META_OPT_IPTC)
    iptc = &meta_data->iptc_adapter;
  if (meta_data->option & META_OPT_XMP)
    xmp = &meta_data->xmp_adapter;

  if (buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF) {
    if (G_LIKELY (meta_data->parse))
      metadataparse_jpeg_init (&meta_data->format_data.jpeg_parse, exif, iptc,
          xmp, &meta_data->strip_chunks, &meta_data->inject_chunks);
    else
      metadatamux_jpeg_init (&meta_data->format_data.jpeg_mux, exif, iptc, xmp,
          &meta_data->strip_chunks, &meta_data->inject_chunks);
    ret = 0;
    meta_data->img_type = IMG_JPEG;
    goto done;
  }

  if (*bufsize < 8) {
    *next_size = 8;
    ret = 1;
    goto done;
  }

  if (buf[0] == 0x89 && buf[1] == 0x50 && buf[2] == 0x4E && buf[3] == 0x47 &&
      buf[4] == 0x0D && buf[5] == 0x0A && buf[6] == 0x1A && buf[7] == 0x0A) {
    if (G_LIKELY (meta_data->parse))
      metadataparse_png_init (&meta_data->format_data.png_parse, exif, iptc,
          xmp, &meta_data->strip_chunks, &meta_data->inject_chunks);
    /*
       else
       metadatamux_png_init (&meta_data->format_data.png_mux, exif, iptc, xmp,
       &meta_data->strip_chunks, &meta_data->inject_chunks);
     */
    ret = 0;
    meta_data->img_type = IMG_PNG;
    goto done;
  }

done:

  return ret;
}

void
metadata_lazy_update (MetaData * meta_data)
{
  switch (meta_data->img_type) {
    case IMG_JPEG:
      if (G_LIKELY (meta_data->parse))
        metadataparse_jpeg_lazy_update (&meta_data->format_data.jpeg_parse);
      else
        metadatamux_jpeg_lazy_update (&meta_data->format_data.jpeg_mux);
      break;
    case IMG_PNG:
      if (G_LIKELY (meta_data->parse))
        metadataparse_png_lazy_update (&meta_data->format_data.png_parse);
      /*
         else
         metadatamux_png_lazy_update (&meta_data->format_data.png_mux);
       */
      break;
    default:
      /* unexpected */
      break;
  }

}
