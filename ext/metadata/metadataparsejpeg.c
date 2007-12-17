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

#include "metadataparsejpeg.h"

#include <string.h>

#ifdef HAVE_IPTC
#include <libiptcdata/iptc-jpeg.h>
#endif

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

#define READ(buf, size) ( (size)--, *((buf)++) )

void
metadataparse_jpeg_lazy_update (JpegParseData * jpeg_data)
{
  /* nothing to do */
}

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

void
metadataparse_jpeg_dispose (JpegParseData * jpeg_data)
{
  jpeg_data->exif_adapter = NULL;
  jpeg_data->iptc_adapter = NULL;
  jpeg_data->xmp_adapter = NULL;
}

MetadataParsingReturn
metadataparse_jpeg_parse (JpegParseData * jpeg_data, guint8 * buf,
    guint32 * bufsize, const guint32 offset, guint8 ** next_start,
    guint32 * next_size)
{

  int ret = META_PARSING_DONE;
  guint8 mark[2] = { 0x00, 0x00 };
  const guint8 *step_buf = buf;

  *next_start = buf;

  if (jpeg_data->state == JPEG_PARSE_NULL) {

    if (*bufsize < 2) {
      *next_size = (buf - *next_start) + 2;
      ret = META_PARSING_NEED_MORE_DATA;
      goto done;
    }

    mark[0] = READ (buf, *bufsize);
    mark[1] = READ (buf, *bufsize);

    if (mark[0] != 0xFF || mark[1] != 0xD8) {
      ret = META_PARSING_ERROR;
      goto done;
    }

    jpeg_data->state = JPEG_PARSE_READING;

  }

  while (ret == META_PARSING_DONE) {
    switch (jpeg_data->state) {
      case JPEG_PARSE_READING:
        ret =
            metadataparse_jpeg_reading (jpeg_data, &buf, bufsize,
            offset, step_buf, next_start, next_size);
        break;
      case JPEG_PARSE_JUMPING:
        ret =
            metadataparse_jpeg_jump (jpeg_data, &buf, bufsize, next_start,
            next_size);
        break;
      case JPEG_PARSE_EXIF:
        ret =
            metadataparse_jpeg_exif (jpeg_data, &buf, bufsize, next_start,
            next_size);
        break;
      case JPEG_PARSE_IPTC:
#ifdef HAVE_IPTC
        ret =
            metadataparse_jpeg_iptc (jpeg_data, &buf, bufsize, next_start,
            next_size);
#endif
        break;
      case JPEG_PARSE_XMP:
        ret =
            metadataparse_jpeg_xmp (jpeg_data, &buf, bufsize, next_start,
            next_size);
        break;
      case JPEG_PARSE_DONE:
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
  static const unsigned char ExifHeader[] =
      { 0x45, 0x78, 0x69, 0x66, 0x00, 0x00 };
  static const char IptcHeader[] = "Photoshop 3.0";
  static const char XmpHeader[] = "http://ns.adobe.com/xap/1.0/";

  *next_start = *buf;

  if (*bufsize < 2) {
    *next_size = (*buf - *next_start) + 2;
    ret = META_PARSING_NEED_MORE_DATA;
    goto done;
  }

  mark[0] = READ (*buf, *bufsize);
  mark[1] = READ (*buf, *bufsize);

  if (mark[0] == 0xFF) {
    if (mark[1] == 0xD9) {      /* end of image */
      ret = META_PARSING_DONE;
      jpeg_data->state = JPEG_PARSE_DONE;
      goto done;
    } else if (mark[1] == 0xDA) {       /* start of scan, lets not look behinf of this */
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

    if (mark[1] == 0xE0) {      /* may be JFIF */

      if (chunk_size >= 16) {
        if (*bufsize < 14) {
          *next_size = (*buf - *next_start) + 14;
          ret = META_PARSING_NEED_MORE_DATA;
          goto done;
        }

        if (0 == memcmp (JfifHeader, *buf, 5)) {
          jpeg_data->jfif_found = TRUE;
        }
      }

    } else if (mark[1] == 0xE1) {       /* may be it is Exif or XMP */

      if (chunk_size >= 8) {    /* size2 'EXIF' 0x00 0x00 */
        guint8 ch;

        if (*bufsize < 6) {
          *next_size = (*buf - *next_start) + 6;
          ret = META_PARSING_NEED_MORE_DATA;
          goto done;
        }

        if (0 == memcmp (ExifHeader, *buf, 6)) {
          MetadataChunk chunk;

          if (!jpeg_data->parse_only) {

            memset (&chunk, 0x00, sizeof (MetadataChunk));
            chunk.offset_orig = (*buf - step_buf) + offset - 4; /* maker + size */
            chunk.size = chunk_size + 2;        /* chunk size plus app marker */
            chunk.type = MD_CHUNK_EXIF;

            metadata_chunk_array_append_sorted (jpeg_data->strip_chunks,
                &chunk);

          }

          if (!jpeg_data->jfif_found) {
            /* only inject if no JFIF has been found */

            static const guint8 segment[] = { 0xff, 0xe0, 0x00, 0x10,
              0x4a, 0x46, 0x49, 0x46, 0x00,
              0x01, 0x02,
              0x00, 0x00, 0x01, 0x00, 0x01,
              0x00, 0x00
            };

            if (!jpeg_data->parse_only) {

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
        if (chunk_size >= 31) { /* size2 "http://ns.adobe.com/xap/1.0/" */
          if (*bufsize < 29) {
            *next_size = (*buf - *next_start) + 29;
            ret = META_PARSING_NEED_MORE_DATA;
            goto done;
          }

          if (0 == memcmp (XmpHeader, *buf, 29)) {

            if (!jpeg_data->parse_only) {

              MetadataChunk chunk;

              memset (&chunk, 0x00, sizeof (MetadataChunk));
              chunk.offset_orig = (*buf - step_buf) + offset - 4;       /* maker + size */
              chunk.size = chunk_size + 2;      /* chunk size plus app marker */
              chunk.type = MD_CHUNK_XMP;

              metadata_chunk_array_append_sorted (jpeg_data->strip_chunks,
                  &chunk);

            }

            /* if adapter has been provided, prepare to hold chunk */
            if (jpeg_data->xmp_adapter) {
              *buf += 29;
              *bufsize -= 29;
              jpeg_data->read = chunk_size - 2 - 29;
              jpeg_data->state = JPEG_PARSE_XMP;
              ret = META_PARSING_DONE;
              goto done;
            }
          }
        }
      }
    }
#ifdef HAVE_IPTC
    else if (mark[1] == 0xED) { /* may be it is photoshop and may be there is iptc */
      if (chunk_size >= 16) {   /* size2 "Photoshop 3.0" */

        if (*bufsize < 14) {
          *next_size = (*buf - *next_start) + 14;
          ret = META_PARSING_NEED_MORE_DATA;
          goto done;
        }


        if (0 == memcmp (IptcHeader, *buf, 14)) {

          if (!jpeg_data->parse_only) {

            MetadataChunk chunk;

            memset (&chunk, 0x00, sizeof (MetadataChunk));
            chunk.offset_orig = (*buf - step_buf) + offset - 4; /* maker + size */
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

static MetadataParsingReturn
metadataparse_jpeg_jump (JpegParseData * jpeg_data, guint8 ** buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size)
{
  jpeg_data->state = JPEG_PARSE_READING;
  return metadataparse_util_jump_chunk (&jpeg_data->read, buf,
      bufsize, next_start, next_size);
}
