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

static MetadataParsingReturn
metadata_parse_none (MetaData * meta_data, const guint8 * buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size);

/*
 * extern functions implementations
 */

/*
 * Init metadata handle vars.
 * This function must becalled before any other function from this module.
 * This functoin must not be called twice without call 'metadata_dispose'
 * beteween them.
 * meta_data [in]: metadata handler to be inited
 * parse [in]: pass TRUE for demuxing and FALSE for muxing
 * options [in]: which types of metadata will be processed (EXIF, IPTC and/or XMP).
 *  Look at 'MetaOptions' to see the available options.
 */
void
metadata_init (MetaData * meta_data, const MetaOptions options)
{
  meta_data->state = STATE_NULL;
  meta_data->img_type = IMG_NONE;
  meta_data->options = options;
  meta_data->offset_orig = 0;
  meta_data->exif_adapter = NULL;
  meta_data->iptc_adapter = NULL;
  meta_data->xmp_adapter = NULL;

  if (meta_data->options & META_OPT_DEMUX) {
    /* when parsing we will probably strip only 3 chunk (exif, iptc and xmp)
       so we use 4 just in case there is more than one chunk of them.
       But this is just for convinience, 'cause the chunk_array incriases dinamically */
    metadata_chunk_array_init (&meta_data->strip_chunks, 4);
    /* at most 1 chunk will be injected (JPEG JFIF) */
    metadata_chunk_array_init (&meta_data->inject_chunks, 1);
  } else {
    /* at most 1 chunk will be striped (JPEG JFIF) */
    metadata_chunk_array_init (&meta_data->strip_chunks, 1);
    /* at most 3 chunk will be injected (EXIF, IPTC, XMP) */
    metadata_chunk_array_init (&meta_data->inject_chunks, 3);
  }

}

/*
 * Dispose medadata handler data.
 * Call this function to free any resource allocated by 'metadata_init'
 */
void
metadata_dispose (MetaData * meta_data)
{

  switch (meta_data->img_type) {
    case IMG_JPEG:
      if (G_LIKELY (meta_data->options & META_OPT_DEMUX))
        metadataparse_jpeg_dispose (&meta_data->format_data.jpeg_parse);
      else
        metadatamux_jpeg_dispose (&meta_data->format_data.jpeg_mux);
      break;
    case IMG_PNG:
      if (G_LIKELY (meta_data->options & META_OPT_DEMUX))
        metadataparse_png_dispose (&meta_data->format_data.png_parse);
      else
        metadatamux_png_dispose (&meta_data->format_data.png_mux);
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
 * meta_data [in]: metata handle
 * buf [in]: data to be parsed
 * bufsize [in]: size of data in bytes
 * next_offset [out]: number of bytes to jump from the begining of 'buf' in the next call.
 *  i.e, 0 (zero) mean that in the next call to function "buf" must have the same
 *  data (probably resized, see 'size')
 * size [out]: number of minimal bytes in buf for the next call to this function
 * return:
 *   META_PARSING_ERROR
 *   META_PARSING_DONE
 *   META_PARSING_NEED_MORE_DATA (look 'next_offset' and 'size')
 * when this function returns 0 you have strip and inject chunks ready to use
 * If you change the contents of strip and inject chunks, you have to call
 *   'metadata_lazy_update' (this is the case when muxing)
 * see MetaData->strip_chunks and MetaData->inject_chunks
 */

MetadataParsingReturn
metadata_parse (MetaData * meta_data, const guint8 * buf,
    guint32 bufsize, guint32 * next_offset, guint32 * next_size)
{

  int ret = META_PARSING_DONE;

  guint8 *next_start = (guint8 *) buf;

  if (meta_data->state == STATE_NULL) {
    ret =
        metadata_parse_none (meta_data, buf, &bufsize, &next_start, next_size);
    if (ret == META_PARSING_DONE)
      meta_data->state = STATE_READING;
    else
      goto done;
  }

  switch (meta_data->img_type) {
    case IMG_JPEG:
      if (G_LIKELY (meta_data->options & META_OPT_DEMUX))
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
      if (G_LIKELY (meta_data->options & META_OPT_DEMUX))
        ret =
            metadataparse_png_parse (&meta_data->format_data.png_parse,
            (guint8 *) buf, &bufsize, meta_data->offset_orig, &next_start,
            next_size);
      else
        ret =
            metadatamux_png_parse (&meta_data->format_data.png_mux,
            (guint8 *) buf, &bufsize, meta_data->offset_orig, &next_start,
            next_size);
      break;
    default:
      /* unexpected */
      ret = META_PARSING_ERROR;
      goto done;
      break;
  }

  *next_offset = next_start - buf;
  meta_data->offset_orig += *next_offset;

done:

  if (ret == META_PARSING_DONE) {
    meta_data->state = STATE_DONE;
  }

  return ret;
}

/*
 * This function must be called after 'metadata_parse' and after the element has modified the 'segments'.
 * This function is really importante in case o muxing 'cause:
 * 1- 'cause gives the oportunity to muxers to wrapper new segments with apropriate bytes
 *   ex: in case of JPEG it can wrap the EXIF chunk (created using tags) with chunk id and chunk size
 * 2- 'cause gives the oportunity to muxer to decide if some chunks should still be striped/injected
 *   ex: if there is no EXIF chunk to be inserted, the muxer decides to not strip JFIF anymore
 * see MetaData->strip_chunks and MetaData->inject_chunks
 */

void
metadata_lazy_update (MetaData * meta_data)
{
  switch (meta_data->img_type) {
    case IMG_JPEG:
      if (G_LIKELY (meta_data->options & META_OPT_DEMUX))
        metadataparse_jpeg_lazy_update (&meta_data->format_data.jpeg_parse);
      else
        metadatamux_jpeg_lazy_update (&meta_data->format_data.jpeg_mux);
      break;
    case IMG_PNG:
      if (G_LIKELY (meta_data->options & META_OPT_DEMUX))
        metadataparse_png_lazy_update (&meta_data->format_data.png_parse);
      else
        metadatamux_png_lazy_update (&meta_data->format_data.png_mux);
      break;
    default:
      /* unexpected */
      break;
  }

}


/*
 * static functions implementation
 */

/*
 * Find out the type of the stream
 */
static MetadataParsingReturn
metadata_parse_none (MetaData * meta_data, const guint8 * buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size)
{

  int ret = META_PARSING_ERROR;
  GstAdapter **exif = NULL;
  GstAdapter **iptc = NULL;
  GstAdapter **xmp = NULL;

  *next_start = (guint8 *) buf;

  meta_data->img_type = IMG_NONE;

  /*
   * Be sure of add checking for more types in order from the
   * less to more bytes need to detect the stream type.
   */

  /* we need at least 3 bytes to see if it is JPEG */
  if (*bufsize < 3) {
    *next_size = 3;
    ret = META_PARSING_NEED_MORE_DATA;
    goto done;
  }

  if (meta_data->options & META_OPT_EXIF)
    exif = &meta_data->exif_adapter;
  if (meta_data->options & META_OPT_IPTC)
    iptc = &meta_data->iptc_adapter;
  if (meta_data->options & META_OPT_XMP)
    xmp = &meta_data->xmp_adapter;

  if (buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF) {
    if (G_LIKELY (meta_data->options & META_OPT_DEMUX))
      metadataparse_jpeg_init (&meta_data->format_data.jpeg_parse, exif, iptc,
          xmp, &meta_data->strip_chunks, &meta_data->inject_chunks,
          meta_data->options & META_OPT_PARSE_ONLY);
    else
      metadatamux_jpeg_init (&meta_data->format_data.jpeg_mux,
          &meta_data->strip_chunks, &meta_data->inject_chunks);
    ret = META_PARSING_DONE;
    meta_data->img_type = IMG_JPEG;
    goto done;
  }

  /* we need at least 8 bytes to see if it is PNG */
  if (*bufsize < 8) {
    *next_size = 8;
    ret = META_PARSING_NEED_MORE_DATA;
    goto done;
  }

  if (buf[0] == 0x89 && buf[1] == 0x50 && buf[2] == 0x4E && buf[3] == 0x47 &&
      buf[4] == 0x0D && buf[5] == 0x0A && buf[6] == 0x1A && buf[7] == 0x0A) {
    if (G_LIKELY (meta_data->options & META_OPT_DEMUX))
      metadataparse_png_init (&meta_data->format_data.png_parse, exif, iptc,
          xmp, &meta_data->strip_chunks, &meta_data->inject_chunks,
          meta_data->options & META_OPT_PARSE_ONLY);
    else
      metadatamux_png_init (&meta_data->format_data.png_mux,
          &meta_data->strip_chunks, &meta_data->inject_chunks);
    ret = META_PARSING_DONE;
    meta_data->img_type = IMG_PNG;
    goto done;
  }

done:

  return ret;
}
