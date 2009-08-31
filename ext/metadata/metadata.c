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

/**
 * SECTION: metadata
 * @short_description: This module provides high-level functions to parse files
 *
 * This module find out the stream type (JPEG or PNG), and provide functions to
 * the caller to know where are the metadata chunks and where should it be
 * written, as well, it gives the caller the metedata chunk to be written and
 * also gets a metadata chunk and wraps it according the strem type
 * specification.
 *
 * <refsect2>
 * <para>
 * #metadata_init must be called before any other function in this module and
 * must be paired with a call to #metadata_dispose. #metadata_parse is used to
 * parse the stream (find the metadata chunks and the place it should be
 * written to. And #metadata_lazy_update is used by muxers to wrap the metadata
 * chunk according the stream type specification. Actually after indentify the
 * stream type, the real jog of parsing is delivered to speciallized module.
 * See, #metadata[mux/parse][jpeg/png].[c/h] files.
 * </para>
 * </refsect2>
 *
 * Last reviewed on 2008-01-24 (0.10.15)
 */

/*
 * includes
 */

#include <string.h>

#include "metadata.h"

/*
 * static helper functions declaration
 */

static MetadataParsingReturn
metadata_parse_none (MetaData * meta_data, const guint8 * data,
    guint32 * data_size, guint8 ** next_start, guint32 * next_size);

/*
 * extern functions implementations
 */

/*
 * metadata_init:
 * @meta_data: [in] metadata handler to be inited
 * @options: [in] which types of metadata will be processed (EXIF, IPTC and/or
 * XMP) and how it will be handled (DEMUXING or MUXING). Look at #MetaOptions
 * to see the available options.
 *
 * Init metadata handle.
 * This function must be called before any other function from this module.
 * This function must not be called twice without call to #metadata_dispose
 * beteween them.
 * @see_also: #metadata_dispose #metadata_parse
 *
 * Returns: nothing
 */

void
metadata_init (MetaData ** meta_data, const MetaOptions options)
{

  if (meta_data == NULL)
    return;
  if ((*meta_data))
    metadata_dispose (meta_data);

  (*meta_data) = g_new (MetaData, 1);

  (*meta_data)->state = STATE_NULL;
  (*meta_data)->img_type = IMG_NONE;
  (*meta_data)->options = options;
  (*meta_data)->offset_orig = 0;
  (*meta_data)->exif_adapter = NULL;
  (*meta_data)->iptc_adapter = NULL;
  (*meta_data)->xmp_adapter = NULL;

  if ((*meta_data)->options & META_OPT_DEMUX) {
    /* when parsing we will probably strip only 3 chunk (exif, iptc and xmp)
       so we use 4 just in case there is more than one chunk of them.
       But this is just for convinience, 'cause the chunk_array increases
       dynamically */
    metadata_chunk_array_init (&(*meta_data)->strip_chunks, 4);
    /* at most 1 chunk will be injected (JPEG JFIF) */
    metadata_chunk_array_init (&(*meta_data)->inject_chunks, 1);
  } else {
    /* at most 1 chunk will be striped (JPEG JFIF) */
    metadata_chunk_array_init (&(*meta_data)->strip_chunks, 1);
    /* at most 3 chunk will be injected (EXIF, IPTC, XMP) */
    metadata_chunk_array_init (&(*meta_data)->inject_chunks, 3);
  }

}

/*
 * metadata_dispose:
 * @meta_data: [in] metadata handler to be freed
 *
 * Call this function to free any resource allocated by #metadata_init
 * @see_also: #metadata_init
 *
 * Returns: nothing
 */

void
metadata_dispose (MetaData ** meta_data)
{

  if (meta_data == NULL || (*meta_data) == NULL)
    return;

  switch ((*meta_data)->img_type) {
    case IMG_JPEG:
      if (G_LIKELY ((*meta_data)->options & META_OPT_DEMUX))
        metadataparse_jpeg_dispose (&(*meta_data)->format_data.jpeg_parse);
      else
        metadatamux_jpeg_dispose (&(*meta_data)->format_data.jpeg_mux);
      break;
    case IMG_PNG:
      if (G_LIKELY ((*meta_data)->options & META_OPT_DEMUX))
        metadataparse_png_dispose (&(*meta_data)->format_data.png_parse);
      else
        metadatamux_png_dispose (&(*meta_data)->format_data.png_mux);
      break;
    case IMG_NONE:
    default:
      break;
  }

  metadata_chunk_array_free (&(*meta_data)->strip_chunks);
  metadata_chunk_array_free (&(*meta_data)->inject_chunks);

  if ((*meta_data)->xmp_adapter) {
    g_object_unref ((*meta_data)->xmp_adapter);
    (*meta_data)->xmp_adapter = NULL;
  }

  if ((*meta_data)->iptc_adapter) {
    g_object_unref ((*meta_data)->iptc_adapter);
    (*meta_data)->iptc_adapter = NULL;
  }

  if ((*meta_data)->exif_adapter) {
    g_object_unref ((*meta_data)->exif_adapter);
    (*meta_data)->exif_adapter = NULL;
  }

  g_free (*meta_data);
  *meta_data = NULL;

}


/*
 * metadata_parse:
 * @meta_data: [in] metadata handle
 * @buf: [in] data to be parsed
 * @buf_size: [in] size of @buf in bytes
 * @next_offset: [out] number of bytes to jump from the begining of @buf in
 * the next call. i.e, 0 (zero) mean that in the next call this function @buf
 * must have the same data (probably resized, see @next_size)
 * @next_size: [out] number of minimal bytes in @buf for the next call to this
 * function
 *
 * This function is used to parse the stream step-by-step incrementaly, which
 * means, discover the stream type and find the metadata chunks
 * (#META_OPT_DEMUX),  or point out where metadata chunks should be written 
 * (#META_OPT_MUX). It is important to notice that there could be both strip
 * and inject chunks in both demuxing and muxing modes.
 * @see_also: #metadata_init #META_DATA_STRIP_CHUNKS #META_DATA_INJECT_CHUNKS
 *
 * Returns:
 * <itemizedlist>
 * <listitem><para>%META_PARSING_ERROR
 * </para></listitem>
 * <listitem><para>%META_PARSING_DONE if parse has finished. Now strip and
 * inject chunks has been found
 * </para></listitem>
 * <listitem><para>%META_PARSING_NEED_MORE_DATA if this function should be
 * called again (look @next_offset and @next_size)
 * </para></listitem>
 * </itemizedlist>
 */

MetadataParsingReturn
metadata_parse (MetaData * meta_data, const guint8 * buf,
    guint32 buf_size, guint32 * next_offset, guint32 * next_size)
{

  int ret = META_PARSING_DONE;

  guint8 *next_start = (guint8 *) buf;

  if (meta_data->state == STATE_NULL) {
    ret =
        metadata_parse_none (meta_data, buf, &buf_size, &next_start, next_size);
    if (ret == META_PARSING_DONE)
      meta_data->state = STATE_READING;
    else
      goto done;
  }

  switch (meta_data->img_type) {
    case IMG_JPEG:
      if (G_LIKELY (meta_data->options & META_OPT_DEMUX)) {
        GST_DEBUG ("parsing jpeg");
        ret =
            metadataparse_jpeg_parse (&meta_data->format_data.jpeg_parse,
            (guint8 *) buf, &buf_size, meta_data->offset_orig, &next_start,
            next_size);
      } else {
        GST_DEBUG ("formatting jpeg");
        ret =
            metadatamux_jpeg_parse (&meta_data->format_data.jpeg_mux,
            (guint8 *) buf, &buf_size, meta_data->offset_orig, &next_start,
            next_size);
      }
      break;
    case IMG_PNG:
      if (G_LIKELY (meta_data->options & META_OPT_DEMUX)) {
        GST_DEBUG ("parsing png");
        ret =
            metadataparse_png_parse (&meta_data->format_data.png_parse,
            (guint8 *) buf, &buf_size, meta_data->offset_orig, &next_start,
            next_size);
      } else {
        GST_DEBUG ("formatting png");
        ret =
            metadatamux_png_parse (&meta_data->format_data.png_mux,
            (guint8 *) buf, &buf_size, meta_data->offset_orig, &next_start,
            next_size);
      }
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
  GST_DEBUG ("parsing/formatting done : %d", ret);

  return ret;
}

/*
 * metadata_lazy_update:
 * @meta_data: [in] metata handle
 *
 * This function must be called after #metadata_parse and after the element
 * has modified the segments (chunks)
 * Data written to #META_DATA_INJECT_CHUNKS will be properly wrapped
 * This function is really important in case of muxing because it gives the
 * oportunity to muxers:
 * 1:  to frame new segments
 *   ex: in case of JPEG it can wrap the EXIF chunk (created using tags) with
 *       chunk id and chunk size
 * 2: to decide if some chunks should still be striped/injected
 *   ex: if there is no EXIF chunk to be inserted, the muxer decides to not
 *       strip JFIF anymore
 * @see_also: #metadata_parse #META_DATA_INJECT_CHUNKS
 *
 * Returns: nothing
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
 * static helper functions implementation
 */

/*
 * metadata_parse_none:
 * @meta_data: [in] metata handle
 * @buf: [in] data to be parsed
 * @buf_size: [in] size of @buf in bytes
 * @next_offset: [out] number of bytes to jump from the begining of @buf in
 * the next call. i.e, 0 (zero) mean that in the next call this function @buf
 * must have the same data (probably resized, see @next_size)
 * @next_size: [out] number of minimal bytes in @buf for the next call to this
 * function
 *
 * Parse the fisrt bytes of the stream to identify the stream type
 * @see_also: metadata_parse
 *
 * Returns:
 * <itemizedlist>
 * <listitem><para>%META_PARSING_ERROR none of the alloed strem types (JPEG,
 * PNG) has been identified
 * </para></listitem>
 * <listitem><para>%META_PARSING_DONE if the stream type has been identified
 * </para></listitem>
 * <listitem><para>%META_PARSING_NEED_MORE_DATA if this function should be
 * called again (look @next_offset and @next_size)
 * </para></listitem>
 * </itemizedlist>
 */
static MetadataParsingReturn
metadata_parse_none (MetaData * meta_data, const guint8 * buf,
    guint32 * buf_size, guint8 ** next_start, guint32 * next_size)
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
  if (*buf_size < 3) {
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
  if (*buf_size < 8) {
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
