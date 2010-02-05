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
 * SECTION: metadataparseutil
 * @short_description: This module has some util function for parsing.
 *
 * Last reviewed on 2008-01-24 (0.10.15)
 */

/*
 * includes
 */

#include "metadataparseutil.h"
#include <string.h>

GST_DEBUG_CATEGORY_EXTERN (gst_metadata_demux_debug);
#define GST_CAT_DEFAULT gst_metadata_demux_debug

/*
 * extern functions implementations
 */

/*
 * metadataparse_util_tag_list_add_chunk:
 * @taglist: tag list in which the whole chunk tag will be added
 * @mode: GStreamer merge mode
 * @name: name of the tag (ex: GST_TAG_EXIF, GST_TAG_IPTC, GST_TAG_XMP)
 * @adapter: contains the whole chunk to be added as tag to @taglist
 *
 * This function get all the bytes from @adapter, create a GST_BUFFER, copy
 * the bytes to it and then add it to @taglist as a tage @name using a
 * merge @mode.
 *
 * Returns: nothing.
 */

void
metadataparse_util_tag_list_add_chunk (GstTagList * taglist,
    GstTagMergeMode mode, const gchar * name, GstAdapter * adapter)
{
  GstBuffer *buf;
  guint size;

  if (adapter && (size = gst_adapter_available (adapter))) {

    buf = gst_buffer_new_and_alloc (size);

    gst_adapter_copy (adapter, GST_BUFFER_DATA (buf), 0, size);

    gst_tag_list_add (taglist, mode, name, buf, NULL);

    gst_buffer_unref (buf);
  }

}

/*
 * metadataparse_util_hold_chunk:
 * @read: number of bytes that still need to be hold
 * @buf: [in] data to be parsed
 * @bufsize: [in] size of @buf in bytes
 * @next_start: indicates a pointer after the @buf where the next parsing step
 * should start from
 * @next_size: indicates the minimal size of the the buffer to be given on
 * the next call to the parser
 * @adapter: adapter to hold the chunk
 * NOTE: To have a explanation of each parameters of this function look at the
 * documentation of #metadataparse_jpeg_reading or #metadataparse_png_reading
 *
 * This function holds a chunk into the adapter. If there is enough bytes
 * (*@read > *@bufsize), then it just hold and make the parser continue after
 * the chunk by setting @next_start properly. Otherwise, if there is not
 * enough bytes in @buf, it just set @next_start and @next_size, to make the
 * parse return META_PARSING_NEED_MORE_DATA and request the caller the proper
 * offset and size, so in the sencond time this function is called it should
 * (or must) have enough data hold the whole chunk.
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

MetadataParsingReturn
metadataparse_util_hold_chunk (guint32 * read, guint8 ** buf,
    guint32 * bufsize, guint8 ** next_start,
    guint32 * next_size, GstAdapter ** adapter)
{
  int ret;

  if (*read > *bufsize) {
    *next_start = *buf;
    *next_size = *read;
    ret = META_PARSING_NEED_MORE_DATA;
  } else {
    GstBuffer *gst_buf;

    if (NULL == *adapter) {
      *adapter = gst_adapter_new ();
    }
    gst_buf = gst_buffer_new_and_alloc (*read);
    memcpy (GST_BUFFER_DATA (gst_buf), *buf, *read);
    gst_adapter_push (*adapter, gst_buf);

    *next_start = *buf + *read;
    *buf += *read;
    *bufsize -= *read;
    *read = 0;
    ret = META_PARSING_DONE;
  }

  return ret;
}

/*
 * metadataparse_util_jump_chunk:
 * NOTE: To have a explanation of each parameters of this function look at
 * the documentation of #metadataparse_util_hold_chunk
 *
 * This function works in the same way as #metadataparse_util_hold_chunk, but
 * just skip the bytes instead of also hold it
 *
 */

MetadataParsingReturn
metadataparse_util_jump_chunk (guint32 * read, guint8 ** buf,
    guint32 * bufsize, guint8 ** next_start, guint32 * next_size)
{
  int ret;

  if (*read > *bufsize) {
    *read -= *bufsize;
    *next_size = 2;
    *next_start = *buf + *bufsize + *read;
    *read = 0;
    *bufsize = 0;
    ret = META_PARSING_NEED_MORE_DATA;
  } else {
    *next_start = *buf + *read;
    *buf += *read;
    *bufsize -= *read;
    *read = 0;
    ret = META_PARSING_DONE;
  }
  return ret;
}
