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

#include "metadataparseutil.h"

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
