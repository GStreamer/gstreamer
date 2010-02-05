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
 * SECTION: metadataparsepng
 * @short_description: This module provides functions to parse PNG files
 *
 * This module parses a PNG stream finding XMP metadata chunks, and marking
 * them to be removed from the stream and saving the XMP chunk in a adapter.
 *
 * <refsect2>
 * <para>
 * #metadataparse_png_init must be called before any other function in this
 * module and must be paired with a call to #metadataparse_png_dispose.
 * #metadataparse_png_parse is used to parse the stream (find the metadata
 * chunks and the place it should be written to.
 * #metadataparse_png_lazy_update do nothing.
 * </para>
 * </refsect2>
 *
 * Last reviewed on 2008-01-24 (0.10.15)
 */

/*
 * includes
 */

#include "metadataparsepng.h"
#include "metadataparseutil.h"

#include <string.h>

GST_DEBUG_CATEGORY_EXTERN (gst_metadata_demux_debug);
#define GST_CAT_DEFAULT gst_metadata_demux_debug

/*
 * defines and macros
 */

/* returns the current byte, advance to the next one and decrease the size */
#define READ(buf, size) ( (size)--, *((buf)++) )

/*
 * static helper functions declaration
 */

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

/*
 * extern functions implementations
 */

/*
 * metadataparse_png_init:
 * @png_data: [in] png data handler to be inited
 * @exif_adpt: ignored
 * @iptc_adpt: ignored
 * @xmp_adpt: where to create/write an adapter to hold the XMP chunk found
 * @strip_chunks: Array of chunks (offset and size) marked for removal
 * @inject_chunks: Array of chunks (offset, data, size) marked for injection
 * @parse_only: TRUE if it should only find the chunks and write then to the
 * adapter (@xmp_adpt). Or FALSE if should also put
 * them on @strip_chunks.
 *
 * Init png data handle.
 * This function must be called before any other function from this module.
 * This function must not be called twice without call to
 * #metadataparse_png_dispose beteween them.
 * @see_also: #metadataparse_png_dispose #metadataparse_png_parse
 *
 * Returns: nothing
 */

void
metadataparse_png_init (PngParseData * png_data, GstAdapter ** exif_adpt,
    GstAdapter ** iptc_adpt, GstAdapter ** xmp_adpt,
    MetadataChunkArray * strip_chunks, MetadataChunkArray * inject_chunks,
    gboolean parse_only)
{
  png_data->state = PNG_PARSE_NULL;
  png_data->xmp_adapter = xmp_adpt;
  png_data->read = 0;

  png_data->strip_chunks = strip_chunks;

  png_data->parse_only = parse_only;

}

/*
 * metadataparse_png_dispose:
 * @png_data: [in] png data handler to be freed
 *
 * Call this function to free any resource allocated by
 * #metadataparse_png_init
 * @see_also: #metadataparse_png_init
 *
 * Returns: nothing
 */

void
metadataparse_png_dispose (PngParseData * png_data)
{
  png_data->xmp_adapter = NULL;
}

/*
 * metadataparse_png_parse:
 * @png_data: [in] png data handle
 * @buf: [in] data to be parsed
 * @bufsize: [in] size of @buf in bytes
 * @offset: is the offset where @buf starts from the beginnig of the whole
 * stream.
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
 * @see_also: #metadataparse_png_init
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
metadataparse_png_parse (PngParseData * png_data, guint8 * buf,
    guint32 * bufsize, const guint32 offset, guint8 ** next_start,
    guint32 * next_size)
{

  int ret = META_PARSING_DONE;
  guint8 mark[8];
  const guint8 *step_buf = buf;

  /* step_buf holds where buf starts. this const value will be passed to
     the nested parsing function, so those function knows how far they from
     the initial buffer. This is not related to the beginning of the whole
     stream, it is just related to the buf passed in this step to this
     function */

  *next_start = buf;

  if (png_data->state == PNG_PARSE_NULL) {

    /* only the first time this function is called it will verify the stream
       type to be sure it is a PNG */

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

    if (mark[0] != 0x89 || mark[1] != 0x50 || mark[2] != 0x4E ||
        mark[3] != 0x47 || mark[4] != 0x0D || mark[5] != 0x0A ||
        mark[6] != 0x1A || mark[7] != 0x0A) {
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

/*
 * metadataparse_png_lazy_update:
 * @png_data: [in] png data handle
 * 
 * This function do nothing
 * @see_also: #metadata_lazy_update
 *
 * Returns: nothing
 */

void
metadataparse_png_lazy_update (PngParseData * png_data)
{
  /* nothing to do */
}

/*
 * static helper functions implementation
 */

/*
 * metadataparse_png_reading:
 * @png_data: [in] png data handle
 * @buf: [in] data to be parsed. @buf will increment during the parsing step.
 * So it will hold the next byte to be read inside a parsing function or on
 * the next nested parsing function. And so, @bufsize will decrement.
 * @bufsize: [in] size of @buf in bytes. This value will decrement during the
 * parsing for the same reason that @buf will advance.
 * @offset: is the offset where @step_buf starts from the beginnig of the
 * stream
 * @step_buf: holds the pointer to the buffer passed to
 * #metadataparse_png_parse. It means that any point inside this function
 * the offset (related to the beginning of the whole stream) after the last 
 * byte read so far is "(*buf - step_buf) + offset"
 * @next_start: is a pointer after @step_buf which indicates where the next
 * call to #metadataparse_png_parse should start on the next call to this
 * function. It means, that after return, this function has
 * consumed *@next_start - @buf bytes. Which also means that @offset should
 * also be incremanted by (*@next_start - @buf) for the next time.
 * @next_size: [out] number of minimal bytes in @buf for the next call to this
 * function
 *
 * This function is used to parse a PNG stream step-by-step incrementally.
 * If this function finds a XMP chunk (or a chunk that should be
 * jumped), then it changes the state of the parsing process so that the
 * remaing parsing can be done by another more specialized function.
 * @see_also: #metadataparse_png_init #metadataparse_png_xmp
 * #metadataparse_png_jump
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

  /* FIXME: use FOURCECC macros */

  GST_DEBUG ("parsing png : 0x%02x%02x%02x%02x",
      mark[0], mark[1], mark[2], mark[3]);

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

        if (!png_data->parse_only) {
          MetadataChunk chunk;

          memset (&chunk, 0x00, sizeof (MetadataChunk));
          chunk.offset_orig = (*buf - step_buf) + offset - 8;   /* maker + size */
          chunk.size = chunk_size + 12; /* chunk size + app marker + crc */
          chunk.type = MD_CHUNK_XMP;

          metadata_chunk_array_append_sorted (png_data->strip_chunks, &chunk);
        }

        /* if adapter has been provided, prepare to hold chunk */
        if (png_data->xmp_adapter) {
          *buf += 22;           /* jump "XML:com.adobe.xmp" + some flags */
          *bufsize -= 22;
          /* four CRC bytes at the end will be jumped after */
          png_data->read = chunk_size - 22;
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

/*
 * metadataparse_png_xmp:
 * @png_data: [in] png data handle
 * @buf: [in] data to be parsed
 * @bufsize: [in] size of @buf in bytes
 * @next_start: look at #metadataparse_png_reading
 * @next_size: look at #metadataparse_png_reading
 * NOTE: To have a explanation of each parameters of this function look at
 * the documentation of #metadataparse_png_reading
 *
 * This function saves the XMP chunk to @png_data->xmp_adapter and makes the
 * parsing process point to the next buffer after the XMP chunk.
 * This function will be called by the parsing process 'cause at some point
 * #metadataparse_png_reading found out the XMP chunk, skipped the PNG
 * wrapper bytes and changed the state of parsing process to PNG_PARSE_XMP.
 * Which just happens if @png_data->parse_only is FALSE and there is a XMP
 * chunk into the stream and @png_data->xmp_adapter is not NULL.
 * This function will just be called once even if there is more than one XMP
 * chunk in the stream. This function do it by setting @png_data->xmp_adapter
 * to NULL.
 * After this function has completely parsed (hold) the chunk, it changes the
 * parsing state to PNG_PARSE_JUMP which makes
 * #metadataparse_png_jump to be called in order to jumo the remaing 4 CRC
 * bytes
 * @see_also: #metadataparse_util_hold_chunk #metadataparse_png_reading
 * #metadataparse_png_jump
 *
 * Returns:
 * <itemizedlist>
 * <listitem><para>%META_PARSING_ERROR
 * </para></listitem>
 * <listitem><para>%META_PARSING_DONE if the chunk bas been completely hold
 * </para></listitem>
 * <listitem><para>%META_PARSING_NEED_MORE_DATA if this function should be
 * called again (look @next_start and @next_size)
 * </para></listitem>
 * </itemizedlist>
 */

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

/*
 * metadataparse_png_jump:
 * @png_data: [in] png data handle
 * @buf: [in] data to be parsed
 * @bufsize: [in] size of @buf in bytes
 * @next_start: look at #metadataparse_png_reading
 * @next_size: look at #metadataparse_png_reading
 * NOTE: To have a explanation of each parameters of this function look at
 * the documentation of #metadataparse_png_reading
 *
 * This function just makes a chunk we are not interested in to be jumped.
 * This is done basically by incrementing  @next_start and @buf,
 * and decreasing @bufsize and setting the next parsing state properly.
 * @see_also: #metadataparse_png_reading #metadataparse_util_jump_chunk
 *
 * Returns:
 * <itemizedlist>
 * <listitem><para>%META_PARSING_DONE if bytes has been skiped and there is
 * still data in @buf
 * </para></listitem>
 * <listitem><para>%META_PARSING_NEED_MORE_DATA if the skiped bytes end at
 * some point after @buf + @bufsize
 * </para></listitem>
 * </itemizedlist>
 */

static MetadataParsingReturn
metadataparse_png_jump (PngParseData * png_data, guint8 ** buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size)
{
  png_data->state = PNG_PARSE_READING;
  return metadataparse_util_jump_chunk (&png_data->read, buf,
      bufsize, next_start, next_size);
}
