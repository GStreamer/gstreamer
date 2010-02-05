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
 * SECTION: metadatamuxjpeg
 * @short_description: This module provides functions to parse JPEG files in
 * order to write metadata to it.
 *
 * This module parses a JPEG stream to find the places in which metadata (EXIF,
 * IPTC, XMP) chunks would be written. It also wraps metadata chunks with JPEG
 * marks according to the specification.
 *
 * <refsect2>
 * <para>
 * #metadatamux_jpeg_init must be called before any other function in this
 * module and must be paired with a call to #metadatamux_jpeg_dispose.
 * #metadatamux_jpeg_parse is used to parse the stream (find the place
 * metadata chunks should be written to).
 * #metadatamux_jpeg_lazy_update do nothing.
 * </para>
 * <para>
 * EXIF chunks will always be the first chunk (replaces JFIF). IPTC and XMP
 * chunks will be placed or second chunk (after JFIF or EXIF) or third chunk
 * if both (IPTC and XMP) are written to the file.
 * </para>
 * <para>
 * When a EXIF chunk is written to the JPEG stream, if there is a JFIF chunk
 * as the first chunk, it will be stripped out.
 * </para>
 * </refsect2>
 *
 * Last reviewed on 2008-01-24 (0.10.15)
 */

/*
 * includes
 */

#include "metadatamuxjpeg.h"
#include "metadataxmp.h"

#include <string.h>

#ifdef HAVE_IPTC
#include <libiptcdata/iptc-jpeg.h>
#endif

GST_DEBUG_CATEGORY_EXTERN (gst_metadata_mux_debug);
#define GST_CAT_DEFAULT gst_metadata_mux_debug

/*
 * defines and macros
 */

#define READ(buf, size) ( (size)--, *((buf)++) )

/*
 * static helper functions declaration
 */

static MetadataParsingReturn
metadatamux_jpeg_reading (JpegMuxData * jpeg_data, guint8 ** buf,
    guint32 * bufsize, const guint32 offset, const guint8 * step_buf,
    guint8 ** next_start, guint32 * next_size);

static void
metadatamux_wrap_chunk (MetadataChunk * chunk, const guint8 * buf,
    guint32 buf_size, guint8 a, guint8 b);

#ifdef HAVE_IPTC
static gboolean
metadatamux_wrap_iptc_with_ps3 (unsigned char **buf, unsigned int *buf_size);
#endif /* #ifdef HAVE_IPTC */


/*
 * extern functions implementations
 */

/*
 * metadatamux_jpeg_init:
 * @jpeg_data: [in] jpeg data handler to be inited
 * @strip_chunks: Array of chunks (offset and size) marked for removal
 * @inject_chunks: Array of chunks (offset, data, size) marked for injection
 * adapter (@exif_adpt, @iptc_adpt, @xmp_adpt). Or FALSE if should also put
 * them on @strip_chunks.
 *
 * Init jpeg data handle.
 * This function must be called before any other function from this module.
 * This function must not be called twice without call to
 * #metadatamux_jpeg_dispose beteween them.
 * @see_also: #metadatamux_jpeg_dispose #metadatamux_jpeg_parse
 *
 * Returns: nothing
 */

void
metadatamux_jpeg_init (JpegMuxData * jpeg_data,
    MetadataChunkArray * strip_chunks, MetadataChunkArray * inject_chunks)
{
  jpeg_data->state = JPEG_MUX_NULL;

  jpeg_data->strip_chunks = strip_chunks;
  jpeg_data->inject_chunks = inject_chunks;

}

/*
 * metadatamux_jpeg_dispose:
 * @jpeg_data: [in] jpeg data handler to be freed
 *
 * Call this function to free any resource allocated by #metadatamux_jpeg_init
 * @see_also: #metadatamux_jpeg_init
 *
 * Returns: nothing
 */

void
metadatamux_jpeg_dispose (JpegMuxData * jpeg_data)
{
  jpeg_data->inject_chunks = NULL;
  jpeg_data->strip_chunks = NULL;

  jpeg_data->state = JPEG_MUX_NULL;
}

/*
 * metadatamux_jpeg_parse:
 * @jpeg_data: [in] jpeg data handle
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
 * This function is used to parse a JPEG stream step-by-step incrementally.
 * Basically this function works like a state machine, that will run in a loop
 * while there is still bytes in @buf to be read or it has finished parsing.
 * If the it hasn't parsed yet and there is no more data in @buf, then the
 * current state is saved and a indication will be make about the buffer to
 * be passed by the caller function.
 * @see_also: #metadatamux_jpeg_init
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
metadatamux_jpeg_parse (JpegMuxData * jpeg_data, guint8 * buf,
    guint32 * bufsize, const guint32 offset, guint8 ** next_start,
    guint32 * next_size)
{

  int ret = META_PARSING_DONE;
  const guint8 *step_buf = buf;

  *next_start = buf;

  if (jpeg_data->state == JPEG_MUX_NULL) {
    guint8 mark[2];

    if (*bufsize < 2) {
      GST_INFO ("need more data");
      *next_size = (buf - *next_start) + 2;
      ret = META_PARSING_NEED_MORE_DATA;
      goto done;
    }

    mark[0] = READ (buf, *bufsize);
    mark[1] = READ (buf, *bufsize);

    if (mark[0] != 0xFF || mark[1] != 0xD8) {
      GST_INFO ("missing marker");
      ret = META_PARSING_ERROR;
      goto done;
    }

    jpeg_data->state = JPEG_MUX_READING;

  }

  while (ret == META_PARSING_DONE) {
    switch (jpeg_data->state) {
      case JPEG_MUX_READING:
        GST_DEBUG ("start reading");
        ret =
            metadatamux_jpeg_reading (jpeg_data, &buf, bufsize,
            offset, step_buf, next_start, next_size);
        break;
      case JPEG_MUX_DONE:
        goto done;
        break;
      default:
        GST_INFO ("invalid parser state");
        ret = META_PARSING_ERROR;
        break;
    }
  }

done:
  GST_INFO ("finishing: %d", ret);
  return ret;
}

/*
 * metadatamux_jpeg_lazy_update:
 * @jpeg_data: [in] jpeg data handle
 * 
 * This function wrap metadata chunk with proper JPEG marks. In case of IPTC
 * it will be wrapped by PhotoShop and then by JPEG mark.
 * @see_also: #metadata_lazy_update
 *
 * Returns: nothing
 */

void
metadatamux_jpeg_lazy_update (JpegMuxData * jpeg_data)
{
  gsize i;
  gboolean has_exif = FALSE;
  MetadataChunkArray *chunks = jpeg_data->inject_chunks;

  GST_INFO ("checking %" G_GSIZE_FORMAT " chunks", chunks->len);

  for (i = 0; i < chunks->len; ++i) {

    GST_INFO ("checking chunk[%" G_GSIZE_FORMAT "], type=%d, len=%u",
        i, chunks->chunk[i].type, chunks->chunk[i].size);

    if (chunks->chunk[i].size > 0 && chunks->chunk[i].data) {
      switch (chunks->chunk[i].type) {
        case MD_CHUNK_EXIF:
          metadatamux_wrap_chunk (&chunks->chunk[i], NULL, 0, 0xFF, 0xE1);
          has_exif = TRUE;
          break;
        case MD_CHUNK_IPTC:
#ifdef HAVE_IPTC
          if (metadatamux_wrap_iptc_with_ps3 (&chunks->chunk[i].data,
                  &chunks->chunk[i].size)) {
            metadatamux_wrap_chunk (&chunks->chunk[i], NULL, 0, 0xFF, 0xED);
          } else {
            GST_ERROR ("Invalid IPTC chunk\n");
            metadata_chunk_array_remove_by_index (chunks, i);
            continue;
          }
#endif /* #ifdef HAVE_IPTC */
          break;
        case MD_CHUNK_XMP:
          metadatamux_wrap_chunk (&chunks->chunk[i],
              (guint8 *) XMP_HEADER, sizeof (XMP_HEADER), 0xFF, 0xE1);
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



/*
 * static helper functions implementation
 */

/*
 * metadatamux_jpeg_reading:
 * @jpeg_data: [in] jpeg data handle
 * @buf: [in] data to be parsed. @buf will increment during the parsing step.
 * So it will hold the next byte to be read inside a parsing function or on
 * the next nested parsing function. And so, @bufsize will decrement.
 * @bufsize: [in] size of @buf in bytes. This value will decrement during the
 * parsing for the same reason that @buf will advance.
 * @offset: is the offset where @step_buf starts from the beginnig of the
 * stream
 * @step_buf: holds the pointer to the buffer passed to
 * #metadatamux_jpeg_parse. It means that any point inside this function
 * the offset (related to the beginning of the whole stream) after the last 
 * byte read so far is "(*buf - step_buf) + offset"
 * @next_start: is a pointer after @step_buf which indicates where the next
 * call to #metadatamux_jpeg_parse should start on the next call to this
 * function. It means, that after return, this function has
 * consumed *@next_start - @buf bytes. Which also means that @offset should
 * also be incremanted by (*@next_start - @buf) for the next time.
 * @next_size: [out] number of minimal bytes in @buf for the next call to this
 * function
 *
 * This function is used to parse a JPEG stream step-by-step incrementally.
 * If this function quickly finds the place (offset) in which EXIF, IPTC and
 * XMP chunk should be written to.
 * The found places are written to @jpeg_data->inject_chunks
 * @see_also: #metadatamux_jpeg_init
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
metadatamux_jpeg_reading (JpegMuxData * jpeg_data, guint8 ** buf,
    guint32 * bufsize, const guint32 offset, const guint8 * step_buf,
    guint8 ** next_start, guint32 * next_size)
{

  int ret = META_PARSING_ERROR;
  guint8 mark[2] = { 0x00, 0x00 };
  guint16 chunk_size = 0;
  gint64 new_chunk_offset = 0;
  MetadataChunk chunk;
  gboolean jfif_found = FALSE;

  static const char JfifHeader[] = "JFIF";

  *next_start = *buf;

  if (*bufsize < 2) {
    GST_INFO ("need more data");
    *next_size = (*buf - *next_start) + 2;
    ret = META_PARSING_NEED_MORE_DATA;
    goto done;
  }

  mark[0] = READ (*buf, *bufsize);
  mark[1] = READ (*buf, *bufsize);

  GST_DEBUG ("parsing JPEG marker : 0x%02x%02x", mark[0], mark[1]);

  if (mark[0] == 0xFF) {

    chunk_size = READ (*buf, *bufsize) << 8;
    chunk_size += READ (*buf, *bufsize);

    if (mark[1] == 0xE0) {      /* APP0 - may be JFIF */

      /* FIXME: whats the 14 ? according to
       * http://en.wikipedia.org/wiki/JFIF#JFIF_segment_format
       * its the jfif segment without thumbnails
       */
      if (chunk_size >= 14 + 2) {
        if (*bufsize < sizeof (JfifHeader)) {
          GST_INFO ("need more data");
          *next_size = (*buf - *next_start) + sizeof (JfifHeader);
          ret = META_PARSING_NEED_MORE_DATA;
          goto done;
        }

        if (0 == memcmp (JfifHeader, *buf, sizeof (JfifHeader))) {
          jfif_found = TRUE;
        }
      } else {
        /* FIXME: should we check if the first chunk is EXIF? */
        GST_INFO ("chunk size too small %u", chunk_size);
      }

    }
    if (!jfif_found) {
      GST_INFO ("no jfif found, will insert it as needed");
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
    ret = META_PARSING_DONE;

  } else {
    GST_INFO ("invalid JPEG chunk");
    ret = META_PARSING_ERROR;
  }


done:

  return ret;


}

/*
 * metadatamux_wrap_chunk:
 * @chunk: chunk to be wrapped
 * @buf: data to inject in the beginning of @chunk->data and after @a and @b
 * @buf_size: size in bytes of @buf
 * @a: together with @b forms the JPEG mark to be injected in the beginning
 * @b: look at @a
 *
 * Wraps a chunk if a JPEG mark (@a@b) and, if @buf_size > 0, with some data
 * (@buf)
 *
 * Returns: nothing
 */

static void
metadatamux_wrap_chunk (MetadataChunk * chunk, const guint8 * buf,
    guint32 buf_size, guint8 a, guint8 b)
{
  guint8 *data = g_new (guint8, 4 + buf_size + chunk->size);

  memcpy (data + 4 + buf_size, chunk->data, chunk->size);
  g_free (chunk->data);
  chunk->data = data;
  chunk->size += 4 + buf_size;
  data[0] = a;
  data[1] = b;
  data[2] = ((chunk->size - 2) >> 8) & 0xFF;
  data[3] = (chunk->size - 2) & 0xFF;
  if (buf && buf_size) {
    memcpy (data + 4, buf, buf_size);
  }
}

#ifdef HAVE_IPTC
static gboolean
metadatamux_wrap_iptc_with_ps3 (unsigned char **buf, unsigned int *buf_size)
{
  unsigned int out_size = *buf_size + 4096;
  unsigned char *outbuf = g_new (unsigned char, out_size);
  int size_written;
  gboolean ret = TRUE;

  size_written =
      iptc_jpeg_ps3_save_iptc (NULL, 0, *buf, *buf_size, outbuf, out_size);

  g_free (*buf);
  *buf = NULL;
  *buf_size = 0;

  if (size_written < 0) {
    g_free (outbuf);
    ret = FALSE;
  } else {
    *buf_size = size_written;
    *buf = outbuf;
  }

  return ret;

}
#endif /* #ifdef HAVE_IPTC */
