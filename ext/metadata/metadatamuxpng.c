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
 * static global vars
 */

static const guint32 metadatamux_crc_table[256] = {
  0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
  0xe963a535,
  0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x09b64c2b,
  0x7eb17cbd,
  0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de,
  0x1adad47d,
  0x6ddde4eb, 0xf4d4b551, 0x83d385c7, 0x136c9856, 0x646ba8c0, 0xfd62f97a,
  0x8a65c9ec,
  0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e,
  0xd56041e4,
  0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa,
  0x42b2986c,
  0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
  0x26d930ac,
  0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599,
  0xb8bda50f,
  0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924, 0x2f6f7c87, 0x58684c11,
  0xc1611dab,
  0xb6662d3d, 0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589,
  0x06b6b51f,
  0x9fbfe4a5, 0xe8b8d433, 0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818,
  0x7f6a0dbb,
  0x086d3d2d, 0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8,
  0xf262004e,
  0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
  0x8bbeb8ea,
  0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65, 0x4db26158,
  0x3ab551ce,
  0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
  0x4369e96a,
  0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5, 0xaa0a4c5f,
  0xdd0d7cc9,
  0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3,
  0xb966d409,
  0xce61e49f, 0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17,
  0x2eb40d81,
  0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
  0xead54739,
  0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84, 0x0d6d6a3e,
  0x7a6a5aa8,
  0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1, 0xf00f9344, 0x8708a3d2,
  0x1e01f268,
  0x6906c2fe, 0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7, 0xfed41b76,
  0x89d32be0,
  0x10da7a5a, 0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
  0xd6d6a3e8,
  0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd,
  0x48b2364b,
  0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
  0x316e8eef,
  0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236, 0xcc0c7795,
  0xbb0b4703,
  0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04,
  0xc2d7ffa7,
  0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d, 0x9b64c2b0, 0xec63f226, 0x756aa39c,
  0x026d930a,
  0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14,
  0x7bb12bae,
  0x0cb61b38, 0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4,
  0xf1d4e242,
  0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
  0x88085ae6,
  0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69, 0x616bffd3,
  0x166ccf45,
  0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7,
  0x4969474d,
  0x3e6e77db, 0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0, 0xa9bcae53,
  0xdebb9ec5,
  0x47b2cf7f, 0x30b5ffe9, 0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6,
  0xbad03605,
  0xcdd70693, 0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02,
  0x2a6f2b94,
  0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};


/*
 * static helper functions declaration
 */

static MetadataParsingReturn
metadatamux_png_reading (PngMuxData * png_data, guint8 ** buf,
    guint32 * bufsize, const guint32 offset, const guint8 * step_buf,
    guint8 ** next_start, guint32 * next_size);

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
  MetadataChunkArray *chunks = png_data->inject_chunks;

  GST_INFO ("checking %" G_GSIZE_FORMAT " chunks", chunks->len);

  for (i = 0; i < chunks->len; ++i) {

    GST_INFO ("checking chunk[%" G_GSIZE_FORMAT "], type=%d, len=%u",
        i, chunks->chunk[i].type, chunks->chunk[i].size);

    if (chunks->chunk[i].size > 0 && chunks->chunk[i].data) {
      switch (chunks->chunk[i].type) {
        case MD_CHUNK_XMP:
          metadatamux_wrap_xmp_chunk (&chunks->chunk[i]);
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
 * metadatamux_update_crc:
 * @crc: seed to calculate the CRC
 * @buf: data to calculate the CRC for
 * @len: size in bytes of @buf
 *
 * Calculates the CRC of a data buffer for a seed @crc.
 * @see_also: #metadatamux_calc_crc
 *
 * Returns: the CRC of the bytes buf[0..len-1].
 */

static guint32
metadatamux_update_crc (guint32 crc, guint8 * buf, guint32 len)
{
  guint32 c = crc;
  guint32 n;

  for (n = 0; n < len; n++) {
    c = metadatamux_crc_table[(c ^ buf[n]) & 0xff] ^ (c >> 8);
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
