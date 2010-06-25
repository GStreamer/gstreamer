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
 * SECTION: metadataparsejpeg
 * @short_description: This module provides functions to parse JPEG files
 *
 * This module parses a JPEG stream finding metadata chunks, and marking them
 * to be removed from the stream and saving them in a adapter.
 *
 * <refsect2>
 * <para>
 * #metadataparse_jpeg_init must be called before any other function in this
 * module and must be paired with a call to #metadataparse_jpeg_dispose.
 * #metadataparse_jpeg_parse is used to parse the stream (find the metadata
 * chunks and the place it should be written to.
 * #metadataparse_jpeg_lazy_update do nothing.
 * </para>
 * <para>
 * This module tries to find metadata chunk until it reaches the "start of scan
 * image". So if the metadata chunk, which could be EXIF, XMP or IPTC (inside 
 * Photoshop), is after the "start of scan image" it will not be found. This is
 * 'cause of performance reason and 'cause we believe that files with metadata
 * chunk after the "scan of image" chunk are very bad practice, so we don't
 * worry about them.
 * </para>
 * <para>
 * If it is working in non-parse_only mode, and the first chunk is a EXIF
 * instead of a JFIF chunk, the EXIF chunk will be marked for removal and a new
 * JFIF chunk will be create and marked to be injected as the first chunk.
 * </para>
 * </refsect2>
 *
 * Last reviewed on 2008-01-24 (0.10.15)
 */

/*
 * includes
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif
#include <string.h>

#include "metadataparsejpeg.h"
#include "metadataparseutil.h"
#include "metadataexif.h"
#include "metadataxmp.h"

#ifdef HAVE_IPTC
#include <libiptcdata/iptc-jpeg.h>
#include "metadataiptc.h"
#endif

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
metadataparse_jpeg_reading (JpegParseData * jpeg_data, guint8 ** buf,
    guint32 * bufsize, const guint32 offset, const guint8 * step_buf,
    guint8 ** next_start, guint32 * next_size);

static MetadataParsingReturn
metadataparse_jpeg_exif (JpegParseData * jpeg_data, guint8 ** buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size);

#ifdef HAVE_IPTC
static MetadataParsingReturn
metadataparse_jpeg_iptc (JpegParseData * jpeg_data, guint8 ** buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size);
#endif

static MetadataParsingReturn
metadataparse_jpeg_xmp (JpegParseData * jpeg_data, guint8 ** buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size);

static MetadataParsingReturn
metadataparse_jpeg_jump (JpegParseData * jpeg_data, guint8 ** buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size);

/*
 * extern functions implementations
 */

/*
 * metadataparse_jpeg_init:
 * @jpeg_data: [in] jpeg data handler to be inited
 * @exif_adpt: where to create/write an adapter to hold the EXIF chunk found
 * @iptc_adpt: where to create/write an adapter to hold the IPTC chunk found
 * @xmp_adpt: where to create/write an adapter to hold the XMP chunk found
 * @strip_chunks: Array of chunks (offset and size) marked for removal
 * @inject_chunks: Array of chunks (offset, data, size) marked for injection
 * @parse_only: TRUE if it should only find the chunks and write then to the
 * adapter (@exif_adpt, @iptc_adpt, @xmp_adpt). Or FALSE if should also put
 * them on @strip_chunks.
 *
 * Init jpeg data handle.
 * This function must be called before any other function from this module.
 * This function must not be called twice without call to
 * #metadataparse_jpeg_dispose beteween them.
 * @see_also: #metadataparse_jpeg_dispose #metadataparse_jpeg_parse
 *
 * Returns: nothing
 */

void
metadataparse_jpeg_init (JpegParseData * jpeg_data, GstAdapter ** exif_adpt,
    GstAdapter ** iptc_adpt, GstAdapter ** xmp_adpt,
    MetadataChunkArray * strip_chunks, MetadataChunkArray * inject_chunks,
    gboolean parse_only)
{
  jpeg_data->state = JPEG_PARSE_NULL;
  jpeg_data->exif_adapter = exif_adpt;
  jpeg_data->iptc_adapter = iptc_adpt;
  jpeg_data->xmp_adapter = xmp_adpt;
  jpeg_data->read = 0;
  jpeg_data->jfif_found = FALSE;

  jpeg_data->strip_chunks = strip_chunks;
  jpeg_data->inject_chunks = inject_chunks;

  jpeg_data->parse_only = parse_only;

}

/*
 * metadataparse_jpeg_dispose:
 * @jpeg_data: [in] jpeg data handler to be freed
 *
 * Call this function to free any resource allocated by
 * #metadataparse_jpeg_init
 * @see_also: #metadataparse_jpeg_init
 *
 * Returns: nothing
 */

void
metadataparse_jpeg_dispose (JpegParseData * jpeg_data)
{
  jpeg_data->exif_adapter = NULL;
  jpeg_data->iptc_adapter = NULL;
  jpeg_data->xmp_adapter = NULL;
}

/*
 * metadataparse_jpeg_parse:
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
 * @see_also: #metadataparse_jpeg_init
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
metadataparse_jpeg_parse (JpegParseData * jpeg_data, guint8 * buf,
    guint32 * bufsize, const guint32 offset, guint8 ** next_start,
    guint32 * next_size)
{
  int ret = META_PARSING_DONE;
  const guint8 *step_buf = buf;

  /* step_buf holds where buf starts. this const value will be passed to
     the nested parsing function, so those function knows how far they from
     the initial buffer. This is not related to the beginning of the whole
     stream, it is just related to the buf passed in this step to this
     function */

  *next_start = buf;

  if (jpeg_data->state == JPEG_PARSE_NULL) {
    guint8 mark[2];

    /* only the first time this function is called it will verify the stream
       type to be sure it is a JPEG */

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
    jpeg_data->state = JPEG_PARSE_READING;
  }

  while (ret == META_PARSING_DONE) {
    switch (jpeg_data->state) {
      case JPEG_PARSE_READING:
        GST_DEBUG ("start reading");
        ret =
            metadataparse_jpeg_reading (jpeg_data, &buf, bufsize,
            offset, step_buf, next_start, next_size);
        break;
      case JPEG_PARSE_JUMPING:
        GST_DEBUG ("jump");
        ret =
            metadataparse_jpeg_jump (jpeg_data, &buf, bufsize, next_start,
            next_size);
        break;
      case JPEG_PARSE_EXIF:
        GST_DEBUG ("parse exif");
        ret =
            metadataparse_jpeg_exif (jpeg_data, &buf, bufsize, next_start,
            next_size);
        break;
      case JPEG_PARSE_IPTC:
        GST_DEBUG ("parse iptc");
#ifdef HAVE_IPTC
        ret =
            metadataparse_jpeg_iptc (jpeg_data, &buf, bufsize, next_start,
            next_size);
#endif
        break;
      case JPEG_PARSE_XMP:
        GST_DEBUG ("parse xmp");
        ret =
            metadataparse_jpeg_xmp (jpeg_data, &buf, bufsize, next_start,
            next_size);
        break;
      case JPEG_PARSE_DONE:
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
 * metadataparse_jpeg_lazy_update:
 * @jpeg_data: [in] jpeg data handle
 * 
 * This function do nothing
 * @see_also: #metadata_lazy_update
 *
 * Returns: nothing
 */

void
metadataparse_jpeg_lazy_update (JpegParseData * jpeg_data)
{
  /* nothing to do */
}

/*
 * static helper functions implementation
 */

/*
 * metadataparse_jpeg_reading:
 * @jpeg_data: [in] jpeg data handle
 * @buf: [in] data to be parsed. @buf will increment during the parsing step.
 * So it will hold the next byte to be read inside a parsing function or on
 * the next nested parsing function. And so, @bufsize will decrement.
 * @bufsize: [in] size of @buf in bytes. This value will decrement during the
 * parsing for the same reason that @buf will advance.
 * @offset: is the offset where @step_buf starts from the beginnig of the
 * stream
 * @step_buf: holds the pointer to the buffer passed to
 * #metadataparse_jpeg_parse. It means that any point inside this function
 * the offset (related to the beginning of the whole stream) after the last 
 * byte read so far is "(*buf - step_buf) + offset"
 * @next_start: is a pointer after @step_buf which indicates where the next
 * call to #metadataparse_jpeg_parse should start on the next call to this
 * function. It means, that after return, this function has
 * consumed *@next_start - @buf bytes. Which also means that @offset should
 * also be incremanted by (*@next_start - @buf) for the next time.
 * @next_size: [out] number of minimal bytes in @buf for the next call to this
 * function
 *
 * This function is used to parse a JPEG stream step-by-step incrementally.
 * If this function finds a EXIF, IPTC or XMP chunk (or a chunk that should be
 * jumped), then it changes the state of the parsing process so that the
 * remaing parsing can be done by another more specialized function.
 * @see_also: #metadataparse_jpeg_init #metadataparse_jpeg_exif
 * #metadataparse_jpeg_iptc #metadataparse_jpeg_xmp #metadataparse_jpeg_jump
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
metadataparse_jpeg_reading (JpegParseData * jpeg_data, guint8 ** buf,
    guint32 * bufsize, const guint32 offset, const guint8 * step_buf,
    guint8 ** next_start, guint32 * next_size)
{

  int ret = META_PARSING_ERROR;
  guint8 mark[2] = { 0x00, 0x00 };
  guint16 chunk_size = 0;

  static const char JfifHeader[] = "JFIF";

  *next_start = *buf;

  if (*bufsize < 2) {
    *next_size = (*buf - *next_start) + 2;
    ret = META_PARSING_NEED_MORE_DATA;
    goto done;
  }

  mark[0] = READ (*buf, *bufsize);
  mark[1] = READ (*buf, *bufsize);

  GST_DEBUG ("parsing JPEG marker : 0x%02x%02x", mark[0], mark[1]);

  if (mark[0] == 0xFF) {
    if (mark[1] == 0xD9) {      /* EOI - end of image */
      ret = META_PARSING_DONE;
      jpeg_data->state = JPEG_PARSE_DONE;
      goto done;
    } else if (mark[1] == 0xDA) {       /* SOS - start of scan */
      /* start of scan image, lets not look behind of this */
      ret = META_PARSING_DONE;
      jpeg_data->state = JPEG_PARSE_DONE;
      goto done;
    }

    if (*bufsize < 2) {
      *next_size = (*buf - *next_start) + 2;
      ret = META_PARSING_NEED_MORE_DATA;
      goto done;
    }

    chunk_size = READ (*buf, *bufsize) << 8;
    chunk_size += READ (*buf, *bufsize);

    if (mark[1] == 0xE0) {      /* APP0 - may be JFIF */

      /* FIXME: whats the 14 ? according to
       * http://en.wikipedia.org/wiki/JFIF#JFIF_segment_format
       * its the jfif segment without thumbnails
       */
      if (chunk_size >= 14 + 2) {
        if (*bufsize < 14) {
          *next_size = (*buf - *next_start) + 14;
          ret = META_PARSING_NEED_MORE_DATA;
          goto done;
        }

        if (0 == memcmp (JfifHeader, *buf, sizeof (JfifHeader))) {
          jpeg_data->jfif_found = TRUE;
        }
      }

    } else if (mark[1] == 0xE1) {       /* APP1 - may be it is Exif or XMP */

      if (chunk_size >= sizeof (EXIF_HEADER) + 2) {
        if (*bufsize < sizeof (EXIF_HEADER)) {
          *next_size = (*buf - *next_start) + sizeof (EXIF_HEADER);
          ret = META_PARSING_NEED_MORE_DATA;
          goto done;
        }

        if (0 == memcmp (EXIF_HEADER, *buf, sizeof (EXIF_HEADER))) {
          MetadataChunk chunk;

          if (!jpeg_data->parse_only) {

            memset (&chunk, 0x00, sizeof (MetadataChunk));

            chunk.offset_orig = (*buf - step_buf) + offset - 4; /* 4 == maker + size */

            chunk.size = chunk_size + 2;        /* chunk size plus app marker */
            chunk.type = MD_CHUNK_EXIF;

            metadata_chunk_array_append_sorted (jpeg_data->strip_chunks,
                &chunk);

          }

          if (!jpeg_data->jfif_found) {
            /* only inject if no JFIF has been found */

            if (!jpeg_data->parse_only) {

              static const guint8 segment[] = { 0xff, 0xe0, 0x00, 0x10,
                0x4a, 0x46, 0x49, 0x46, 0x00,
                0x01, 0x02,
                0x00, 0x00, 0x01, 0x00, 0x01,
                0x00, 0x00
              };

              memset (&chunk, 0x00, sizeof (MetadataChunk));
              chunk.offset_orig = 2;
              chunk.size = 18;
              chunk.type = MD_CHUNK_UNKNOWN;
              chunk.data = g_new (guint8, 18);
              memcpy (chunk.data, segment, 18);

              metadata_chunk_array_append_sorted (jpeg_data->inject_chunks,
                  &chunk);

            }

          }

          if (jpeg_data->exif_adapter) {

            jpeg_data->read = chunk_size - 2;
            jpeg_data->state = JPEG_PARSE_EXIF;
            ret = META_PARSING_DONE;
            goto done;
          }
        }
        if (chunk_size >= sizeof (XMP_HEADER) + 2) {
          if (*bufsize < sizeof (XMP_HEADER)) {
            *next_size = (*buf - *next_start) + sizeof (XMP_HEADER);
            ret = META_PARSING_NEED_MORE_DATA;
            goto done;
          }

          if (0 == memcmp (XMP_HEADER, *buf, sizeof (XMP_HEADER))) {

            if (!jpeg_data->parse_only) {

              MetadataChunk chunk;

              memset (&chunk, 0x00, sizeof (MetadataChunk));
              chunk.offset_orig = (*buf - step_buf) + offset - 4;       /* 4 == maker + size */
              chunk.size = chunk_size + 2;      /* chunk size plus app marker */
              chunk.type = MD_CHUNK_XMP;

              metadata_chunk_array_append_sorted (jpeg_data->strip_chunks,
                  &chunk);

            }

            /* if adapter has been provided, prepare to hold chunk */
            if (jpeg_data->xmp_adapter) {
              *buf += sizeof (XMP_HEADER);
              *bufsize -= sizeof (XMP_HEADER);
              jpeg_data->read = chunk_size - 2 - sizeof (XMP_HEADER);
              jpeg_data->state = JPEG_PARSE_XMP;
              ret = META_PARSING_DONE;
              goto done;
            }
          }
        }
      }
    }
#ifdef HAVE_IPTC
    else if (mark[1] == 0xED) {
      /* may be it is photoshop and may be there is iptc */

      if (chunk_size >= 16) {   /* size2 "Photoshop 3.0" */

        if (*bufsize < 14) {
          *next_size = (*buf - *next_start) + 14;
          ret = META_PARSING_NEED_MORE_DATA;
          goto done;
        }

        if (0 == memcmp (PHOTOSHOP_HEADER, *buf, sizeof (PHOTOSHOP_HEADER))) {

          if (!jpeg_data->parse_only) {

            MetadataChunk chunk;

            memset (&chunk, 0x00, sizeof (MetadataChunk));
            chunk.offset_orig = (*buf - step_buf) + offset - 4; /* 4 == maker + size */
            chunk.size = chunk_size + 2;        /* chunk size plus app marker */
            chunk.type = MD_CHUNK_IPTC;

            metadata_chunk_array_append_sorted (jpeg_data->strip_chunks,
                &chunk);

          }

          /* if adapter has been provided, prepare to hold chunk */
          if (jpeg_data->iptc_adapter) {
            jpeg_data->read = chunk_size - 2;
            jpeg_data->state = JPEG_PARSE_IPTC;
            ret = META_PARSING_DONE;
            goto done;
          }
        }
      }
    }
#endif /* #ifdef HAVE_IPTC */

    /* just set jump sise  */
    jpeg_data->read = chunk_size - 2;
    jpeg_data->state = JPEG_PARSE_JUMPING;
    ret = META_PARSING_DONE;

  } else {
    /* invalid JPEG chunk */
    ret = META_PARSING_ERROR;
  }


done:

  return ret;


}

/*
 * metadataparse_jpeg_exif:
 * @jpeg_data: [in] jpeg data handle
 * @buf: [in] data to be parsed
 * @bufsize: [in] size of @buf in bytes
 * @next_start: look at #metadataparse_jpeg_reading
 * @next_size: look at #metadataparse_jpeg_reading
 * NOTE: To have a explanation of each parameters of this function look at
 * the documentation of #metadataparse_jpeg_reading
 *
 * This function saves the EXIF chunk to @jpeg_data->exif_adapter and makes the
 * parsing process point to the next buffer after the EXIF chunk.
 * This function will be called by the parsing process 'cause at some point
 * #metadataparse_jpeg_reading found out the EXIF chunk, skipped the JPEG
 * wrapper bytes and changed the state of parsing process to JPEG_PARSE_EXIF.
 * Which just happens if @jpeg_data->parse_only is FALSE and there is a EXIF
 * chunk into the stream and @jpeg_data->exif_adapter is not NULL.
 * This function will just be called once even if there is more than one EXIF
 * chunk in the stream. This function do it by setting @jpeg_data->exif_adapter
 * to NULL.
 * After this function has completely parsed (hold) the chunk, it changes the
 * parsing state back to JPEG_PARSE_READING which makes
 * #metadataparse_jpeg_reading to be called again
 * @see_also: #metadataparse_util_hold_chunk #metadataparse_jpeg_reading
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
metadataparse_jpeg_exif (JpegParseData * jpeg_data, guint8 ** buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size)
{
  int ret;

  ret = metadataparse_util_hold_chunk (&jpeg_data->read, buf,
      bufsize, next_start, next_size, jpeg_data->exif_adapter);
  if (ret == META_PARSING_DONE) {

    jpeg_data->state = JPEG_PARSE_READING;

    /* if there is a second Exif chunk in the file it will be jumped */
    jpeg_data->exif_adapter = NULL;
  }
  return ret;

}

/*
 * metadataparse_jpeg_iptc:
 *
 * Look at #metadataparse_jpeg_exif. This function has the same behavior as
 * that. The only difference is that this function also cut out others
 * PhotoShop data and only holds IPTC data in it.
 *
 */

#ifdef HAVE_IPTC
static MetadataParsingReturn
metadataparse_jpeg_iptc (JpegParseData * jpeg_data, guint8 ** buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size)
{

  int ret;

  ret = metadataparse_util_hold_chunk (&jpeg_data->read, buf,
      bufsize, next_start, next_size, jpeg_data->iptc_adapter);


  if (ret == META_PARSING_DONE) {

    const guint8 *buf;
    guint32 size;
    unsigned int iptc_len;
    int res;

    jpeg_data->state = JPEG_PARSE_READING;

    size = gst_adapter_available (*jpeg_data->iptc_adapter);
    buf = gst_adapter_peek (*jpeg_data->iptc_adapter, size);

    /* FIXME: currently we are throwing away others PhotoShop data */
    res = iptc_jpeg_ps3_find_iptc (buf, size, &iptc_len);

    if (res < 0) {
      /* error */
      ret = META_PARSING_ERROR;
    } else if (res == 0) {
      /* no iptc data found */
      gst_adapter_clear (*jpeg_data->iptc_adapter);
    } else {
      gst_adapter_flush (*jpeg_data->iptc_adapter, res);
      size = gst_adapter_available (*jpeg_data->iptc_adapter);
      if (size > iptc_len) {
        GstBuffer *buf;

        buf = gst_adapter_take_buffer (*jpeg_data->iptc_adapter, iptc_len);
        gst_adapter_clear (*jpeg_data->iptc_adapter);
        gst_adapter_push (*jpeg_data->iptc_adapter, buf);
      }
    }

    /* if there is a second Iptc chunk in the file it will be jumped */
    jpeg_data->iptc_adapter = NULL;

  }

  return ret;

}
#endif

/*
 * metadataparse_jpeg_xmp:
 *
 * Look at #metadataparse_jpeg_exif. This function has the same behavior as
 * that.
 *
 */

static MetadataParsingReturn
metadataparse_jpeg_xmp (JpegParseData * jpeg_data, guint8 ** buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size)
{
  int ret;

  ret = metadataparse_util_hold_chunk (&jpeg_data->read, buf,
      bufsize, next_start, next_size, jpeg_data->xmp_adapter);

  if (ret == META_PARSING_DONE) {
    jpeg_data->state = JPEG_PARSE_READING;
    /* if there is a second XMP chunk in the file it will be jumped */
    jpeg_data->xmp_adapter = NULL;
  }
  return ret;
}

/*
 * metadataparse_jpeg_jump:
 * @jpeg_data: [in] jpeg data handle
 * @buf: [in] data to be parsed
 * @bufsize: [in] size of @buf in bytes
 * @next_start: look at #metadataparse_jpeg_reading
 * @next_size: look at #metadataparse_jpeg_reading
 * NOTE: To have a explanation of each parameters of this function look at
 * the documentation of #metadataparse_jpeg_reading
 *
 * This function just makes a chunk we are not interested in to be jumped.
 * This is done basically by incrementing  @next_start and @buf,
 * and decreasing @bufsize and setting the next parsing state properly.
 * @see_also: #metadataparse_jpeg_reading #metadataparse_util_jump_chunk
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
metadataparse_jpeg_jump (JpegParseData * jpeg_data, guint8 ** buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size)
{
  jpeg_data->state = JPEG_PARSE_READING;
  return metadataparse_util_jump_chunk (&jpeg_data->read, buf,
      bufsize, next_start, next_size);
}
