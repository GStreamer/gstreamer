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

#include "metadataiptc.h"
#include "metadataparseutil.h"
#include "metadatatags.h"

GST_DEBUG_CATEGORY (gst_metadata_iptc_debug);
#define GST_CAT_DEFAULT gst_metadata_iptc_debug

#ifndef HAVE_IPTC

void
metadataparse_iptc_tag_list_add (GstTagList * taglist, GstTagMergeMode mode,
    GstAdapter * adapter, MetadataTagMapping mapping)
{

  GST_LOG ("IPTC not defined, here I should send just one tag as whole chunk");

  if (mapping & METADATA_TAG_MAP_WHOLECHUNK)
    metadataparse_util_tag_list_add_chunk (taglist, mode, GST_TAG_IPTC,
        adapter);

}


void
metadatamux_iptc_create_chunk_from_tag_list (guint8 ** buf, guint32 * size,
    const GstTagList * taglist)
{
  /* do nothing */
}

#else /* ifndef HAVE_IPTC */

#include <iptc-data.h>

static void
iptc_data_foreach_dataset_func (IptcDataSet * dataset, void *user_data);

void
metadataparse_iptc_tag_list_add (GstTagList * taglist, GstTagMergeMode mode,
    GstAdapter * adapter, MetadataTagMapping mapping)
{
  const guint8 *buf;
  guint32 size;
  IptcData *iptc = NULL;

  if (adapter == NULL || (size = gst_adapter_available (adapter)) == 0) {
    goto done;
  }

  /* add chunk tag */
  if (mapping & METADATA_TAG_MAP_WHOLECHUNK)
    metadataparse_util_tag_list_add_chunk (taglist, mode, GST_TAG_IPTC,
        adapter);

  if (!(mapping & METADATA_TAG_MAP_INDIVIDUALS))
    goto done;

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

void
metadatamux_iptc_create_chunk_from_tag_list (guint8 ** buf, guint32 * size,
    const GstTagList * taglist)
{
  GstBuffer *iptc_chunk = NULL;
  const GValue *val = NULL;

  if (!(buf && size))
    goto done;
  if (*buf) {
    g_free (*buf);
    *buf = NULL;
  }
  *size = 0;

  val = gst_tag_list_get_value_index (taglist, GST_TAG_IPTC, 0);
  if (val) {
    iptc_chunk = gst_value_get_buffer (val);
    if (iptc_chunk) {
      *size = GST_BUFFER_SIZE (iptc_chunk);
      *buf = g_new (guint8, *size);
      memcpy (*buf, GST_BUFFER_DATA (iptc_chunk), *size);
    }
  }

done:

  return;
}

#endif /* else (ifndef HAVE_IPTC) */
