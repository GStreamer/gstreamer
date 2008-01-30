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

/*
 * SECTION: metadatamuxpng
 * @short_description: This module provides functions to parse PNG files in
 * order to write metadata to it.
 *
 * This module parses a PNG stream to find the places in which XMP metadata
 * chunks would be written. It also wraps metadata chunks with PNG marks
 * according to the specification.
 *
 * <refsect2>
 * <para>
 * #metadatamux_png_init must be called before any other function in this
 * module and must be paired with a call to #metadatamux_png_dispose.
 * #metadatamux_png_parse is used to parse the stream (find the place
 * metadata chunks should be written to).
 * #metadatamux_png_lazy_update do nothing.
 * </para>
 * <para>
 * EXIF chunks will always be the first chunk (replaces JFIF). IPTC and XMP
 * chunks will be placed or second chunk (after JFIF or EXIF) or third chunk
 * if both (IPTC and XMP) are written to the file.
 * </para>
 * <para>
 * When a EXIF chunk is written to the PNG stream, if there is a JFIF chunk
 * as the first chunk, it will be stripped out.
 * </para>
 * </refsect2>
 *
 * Last reviewed on 2008-01-24 (0.10.15)
 */

/*
 * includes
 */

#include "metadatamuxpng.h"

#include <string.h>

/*
 * defines and macros
 */

#define READ(buf, size) ( (size)--, *((buf)++) )

/*
 * static helper functions declaration
 */

static MetadataParsingReturn
metadatamux_png_reading (PngMuxData * png_data, guint8 ** buf,
    guint32 * bufsize, const guint32 offset, const guint8 * step_buf,
    guint8 ** next_start, guint32 * next_size);

static void metadatamux_make_crc_table (guint32 crc_table[]);

static guint32 metadatamux_update_crc (guint32 crc, guint8 * buf, guint32 len);

static guint32 metadatamux_calc_crc (guint8 * buf, guint32 len);

static void metadatamux_wrap_xmp_chunk (MetadataChunk * chunk);

/*
 * extern functions implementations
 */

/*
 * metadatamux_png_init:
 * @png_data: [in] png data handler to be inited
 * @strip_chunks: Array of chunks (offset and size) marked for removal
 * @inject_chunks: Array of chunks (offset, data, size) marked for injection
 * adapter (@exif_adpt, @iptc_adpt, @xmp_adpt). Or FALSE if should also put
 * them on @strip_chunks.
 *
 * Init png data handle.
 * This function must be called before any other function from this module.
 * This function must not be called twice without call to
 * #metadatamux_png_dispose beteween them.
 * @see_also: #metadatamux_png_dispose #metadatamux_png_parse
 *
 * Returns: nothing
 */

void
metadatamux_png_init (PngMuxData * png_data,
    MetadataChunkArray * strip_chunks, MetadataChunkArray * inject_chunks)
{
  png_data->state = PNG_MUX_NULL;

  png_data->strip_chunks = strip_chunks;
  png_data->inject_chunks = inject_chunks;
}

/*
 * metadatamux_png_dispose:
 * png_data: [in] png data handler to be freed
 *
 * Call this function to free any resource allocated by #metadatamux_png_init
 * @see_also: #metadatamux_png_init
 *
 * Returns: nothing
 */

void
metadatamux_png_dispose (PngMuxData * png_data)
{
  png_data->strip_chunks = NULL;
  png_data->inject_chunks = NULL;

  png_data->state = PNG_MUX_NULL;
}

/*
 * metadatamux_png_parse:
 * @png_data: [in] png data handle
 * @buf: [in] data to be parsed
 * @bufsize: [in] size of @buf in bytes
 * @offset: is the offset where @buf starts from the beginnig of the whole 
 * stream
 * @next_start: is a pointer after @buf which indicates where @buf should start
 * on the next call to this function. It means, that after returning, this
 * function has consumed *@next_start - @buf bytes. Which also means 
 * that @offset should also be incremanted by (*@next_start - @buf) for the
 * next time.
 * @next_size: [out] number of minimal bytes in @buf for the next call to this
 * function
 *
 * This function is used to parse a PNG stream step-by-step incrementally.
 * Basically this function works like a state machine, that will run in a loop
 * while there is still bytes in @buf to be read or it has finished parsing.
 * If the it hasn't parsed yet and there is no more data in @buf, then the
 * current state is saved and a indication will be make about the buffer to
 * be passed by the caller function.
 * @see_also: #metadatamux_png_init
 *
 * Returns:
 * <itemizedlist>
 * <listitem><para>%META_PARSING_ERROR
 * </para></listitem>
 * <listitem><para>%META_PARSING_DONE if parse has finished. Now strip and
 * inject chunks has been found
 * </para></listitem>
 * <listitem><para>%META_PARSING_NEED_MORE_DATA if this function should be
 * called again (look @next_start and @next_size)
 * </para></listitem>
 * </itemizedlist>
 */

MetadataParsingReturn
metadatamux_png_parse (PngMuxData * png_data, guint8 * buf,
    guint32 * bufsize, const guint32 offset, guint8 ** next_start,
    guint32 * next_size)
{

  int ret = META_PARSING_DONE;
  guint8 mark[8];
  const guint8 *step_buf = buf;

  *next_start = buf;

  if (png_data->state == PNG_MUX_NULL) {

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

    png_data->state = PNG_MUX_READING;

  }

  while (ret == META_PARSING_DONE) {
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
        ret = META_PARSING_ERROR;
        break;
    }
  }

done:

  return ret;

}

/*
 * metadatamux_png_lazy_update:
 * @png_data: [in] png data handle
 * 
 * This function wrap metadata chunk with proper PNG bytes.
 * @see_also: #metadata_lazy_update
 *
 * Returns: nothing
 */

void
metadatamux_png_lazy_update (PngMuxData * png_data)
{
  gsize i;

  for (i = 0; i < png_data->inject_chunks->len; ++i) {
    if (png_data->inject_chunks->chunk[i].size > 0 &&
        png_data->inject_chunks->chunk[i].data) {
      switch (png_data->inject_chunks->chunk[i].type) {
        case MD_CHUNK_XMP:
        {
          metadatamux_wrap_xmp_chunk (&png_data->inject_chunks->chunk[i]);
        }
          break;
        default:
          GST_ERROR ("Unexpected chunk for PNG muxer.");
          break;
      }
    }
  }
}


/*
 * static helper functions implementation
 */

/*
 * metadatamux_png_reading:
 * @png_data: [in] png data handle
 * @buf: [in] data to be parsed. @buf will increment during the parsing step.
 * So it will hold the next byte to be read inside a parsing function or on
 * the next nested parsing function. And so, @bufsize will decrement.
 * @bufsize: [in] size of @buf in bytes. This value will decrement during the
 * parsing for the same reason that @buf will advance.
 * @offset: is the offset where @step_buf starts from the beginnig of the
 * stream
 * @step_buf: holds the pointer to the buffer passed to
 * #metadatamux_png_parse. It means that any point inside this function
 * the offset (related to the beginning of the whole stream) after the last 
 * byte read so far is "(*buf - step_buf) + offset"
 * @next_start: is a pointer after @step_buf which indicates where the next
 * call to #metadatamux_png_parse should start on the next call to this
 * function. It means, that after return, this function has
 * consumed *@next_start - @buf bytes. Which also means that @offset should
 * also be incremanted by (*@next_start - @buf) for the next time.
 * @next_size: [out] number of minimal bytes in @buf for the next call to this
 * function
 *
 * This function is used to parse a PNG stream step-by-step incrementally.
 * If this function quickly finds the place (offset) in which EXIF, IPTC and
 * XMP chunk should be written to.
 * The found places are written to @png_data->inject_chunks
 * @see_also: #metadatamux_png_init
 *
 * Returns:
 * <itemizedlist>
 * <listitem><para>%META_PARSING_ERROR
 * </para></listitem>
 * <listitem><para>%META_PARSING_DONE if parse has finished. Now strip and
 * inject chunks has been found. Or some chunk has been found and should be
 * held or jumped.
 * </para></listitem>
 * <listitem><para>%META_PARSING_NEED_MORE_DATA if this function should be
 * called again (look @next_start and @next_size)
 * </para></listitem>
 * </itemizedlist>
 */


static MetadataParsingReturn
metadatamux_png_reading (PngMuxData * png_data, guint8 ** buf,
    guint32 * bufsize, const guint32 offset, const guint8 * step_buf,
    guint8 ** next_start, guint32 * next_size)
{

  int ret = META_PARSING_ERROR;
  guint8 mark[4];
  guint32 chunk_size = 0;
  MetadataChunk chunk;

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

  if (!(mark[0] == 'I' && mark[1] == 'H' && mark[2] == 'D' && mark[3] == 'R')) {
    ret = META_PARSING_ERROR;
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
  ret = META_PARSING_DONE;

done:

  return ret;


}

/*
 * metadatamux_make_crc_table:
 * @crc_table: table to be written to.
 *
 * Creates a startup CRC table. For optimization it should be done only once.
 * @see_also: #metadatamux_update_crc
 *
 * Returns: nothing.
 */

static void
metadatamux_make_crc_table (guint32 crc_table[])
{
  guint32 c;
  guint16 n, k;

  for (n = 0; n < 256; n++) {
    c = (guint32) n;
    for (k = 0; k < 8; k++) {
      if (c & 1)
        c = 0xedb88320L ^ (c >> 1);
      else
        c = c >> 1;
    }
    crc_table[n] = c;
  }
}

/*
 * metadatamux_update_crc:
 * @crc: seed to calculate the CRC
 * @buf: data to calculate the CRC for
 * @len: size in bytes of @buf
 *
 * Calculates the CRC of a data buffer for a seed @crc.
 * @see_also: #metadatamux_make_crc_table #metadatamux_calc_crc
 *
 * Returns: the CRC of the bytes buf[0..len-1].
 */

static guint32
metadatamux_update_crc (guint32 crc, guint8 * buf, guint32 len)
{
  guint32 c = crc;
  guint32 n;
  guint32 crc_table[256];

  /* FIXME:  make_crc_table should be done once in life 
     for speed up. It could be written hard coded to a file */
  metadatamux_make_crc_table (crc_table);

  for (n = 0; n < len; n++) {
    c = crc_table[(c ^ buf[n]) & 0xff] ^ (c >> 8);
  }
  return c;
}

/*
 * metadatamux_calc_crc:
 * @buf: data to calculate the CRC for
 * @len: size in bytes of @buf
 *
 * Calculates the CRC of a data buffer.
 *
 * Returns: the CRC of the bytes buf[0..len-1].
 */

static guint32
metadatamux_calc_crc (guint8 * buf, guint32 len)
{
  return metadatamux_update_crc (0xffffffffL, buf, len) ^ 0xffffffffL;
}


/*
 * metadatamux_wrap_xmp_chunk:
 * @chunk: chunk to be wrapped
 *
 * Wraps a XMP chunk with proper PNG bytes (mark, size and crc in the end)
 *
 * Returns: nothing
 */

static void
metadatamux_wrap_xmp_chunk (MetadataChunk * chunk)
{
  static const char XmpHeader[] = "XML:com.adobe.xmp";
  guint8 *data = NULL;
  guint32 crc;

  data = g_new (guint8, 12 + 18 + 4 + chunk->size);

  memcpy (data + 8, XmpHeader, 18);
  memset (data + 8 + 18, 0x00, 4);
  memcpy (data + 8 + 18 + 4, chunk->data, chunk->size);
  g_free (chunk->data);
  chunk->data = data;
  chunk->size += 18 + 4;
  data[0] = (chunk->size >> 24) & 0xFF;
  data[1] = (chunk->size >> 16) & 0xFF;
  data[2] = (chunk->size >> 8) & 0xFF;
  data[3] = chunk->size & 0xFF;
  data[4] = 'i';
  data[5] = 'T';
  data[6] = 'X';
  data[7] = 't';
  crc = metadatamux_calc_crc (data + 4, chunk->size + 4);
  data[chunk->size + 8] = (crc >> 24) & 0xFF;
  data[chunk->size + 9] = (crc >> 16) & 0xFF;
  data[chunk->size + 10] = (crc >> 8) & 0xFF;
  data[chunk->size + 11] = crc & 0xFF;

  chunk->size += 12;

}
