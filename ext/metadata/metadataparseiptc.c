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

#include "metadataparseiptc.h"

GST_DEBUG_CATEGORY (gst_metadata_parse_iptc_debug);
#define GST_CAT_DEFAULT gst_metadata_parse_iptc_debug

#define GST_TAG_IPTC "iptc"

void
metadataparse_iptc_tags_register (void)
{
  gst_tag_register (GST_TAG_IPTC, GST_TAG_FLAG_META,
      GST_TYPE_BUFFER, GST_TAG_IPTC, "iptc metadata chunk", NULL);
}

void
metadataparse_tag_list_add_chunk (GstTagList * taglist, GstTagMergeMode mode,
    const gchar * name, GstAdapter * adapter)
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

#ifndef HAVE_IPTC

void
metadataparse_iptc_tag_list_add (GstTagList * taglist, GstTagMergeMode mode,
    GstAdapter * adapter)
{

  GST_LOG ("IPTC not defined, here I should send just one tag as whole chunk");

  metadataparse_tag_list_add_chunk (taglist, mode, GST_TAG_IPTC, adapter);

}

#else /* ifndef HAVE_IPTC */

#include <iptc-data.h>

static void
iptc_data_foreach_dataset_func (IptcDataSet * dataset, void *user_data);

void
metadataparse_iptc_tag_list_add (GstTagList * taglist, GstTagMergeMode mode,
    GstAdapter * adapter)
{
  const guint8 *buf;
  guint32 size;
  IptcData *iptc = NULL;

  if (adapter == NULL || (size = gst_adapter_available (adapter)) == 0) {
    goto done;
  }

  /* add chunk tag */
  metadataparse_tag_list_add_chunk (taglist, mode, GST_TAG_IPTC, adapter);

  buf = gst_adapter_peek (adapter, size);

  iptc = iptc_data_new_from_data (buf, size);
  if (iptc == NULL) {
    goto done;
  }

  iptc_data_foreach_dataset (iptc, iptc_data_foreach_dataset_func,
      (void *) taglist);

done:

  if (iptc)
    iptc_data_unref (iptc);

  return;

}

static void
iptc_data_foreach_dataset_func (IptcDataSet * dataset, void *user_data)
{

  char *buf[256];
  GstTagList *taglist = (GstTagList *) user_data;

  GST_LOG ("name -> %s", iptc_tag_get_name (dataset->record, dataset->tag));
  GST_LOG ("title -> %s", iptc_tag_get_title (dataset->record, dataset->tag));
  GST_LOG ("description -> %s", iptc_tag_get_description (dataset->record,
          dataset->tag));
  GST_LOG ("value = %s", iptc_dataset_get_as_str (dataset, buf, 256));
}

#endif /* else (ifndef HAVE_IPTC) */
