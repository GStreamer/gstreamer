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

#include "metadataparseexif.h"

GST_DEBUG_CATEGORY (gst_metadata_parse_exif_debug);
#define GST_CAT_DEFAULT gst_metadata_parse_exif_debug

void
metadataparse_exif_tags_register (void)
{
}

#ifndef HAVE_EXIF

void
metadataparse_exif_tag_list_add (GstTagList * taglist, GstTagMergeMode mode,
    GstAdapter * adapter)
{

  GST_LOG ("EXIF not defined, here I should send just one tag as whole chunk");

}

#else /* ifndef HAVE_EXIF */

#include <libexif/exif-data.h>

static void
exif_data_foreach_content_func (ExifContent * content, void *callback_data);

static void exif_content_foreach_entry_func (ExifEntry * entry, void *);

void
metadataparse_exif_tag_list_add (GstTagList * taglist, GstTagMergeMode mode,
    GstAdapter * adapter)
{
  const guint8 *buf;
  guint32 size;
  ExifData *exif = NULL;

  if (adapter == NULL || (size = gst_adapter_available (adapter)) == 0) {
    goto done;
  }

  buf = gst_adapter_peek (adapter, size);

  exif = exif_data_new_from_data (buf, size);
  if (exif == NULL) {
    goto done;
  }

  exif_data_foreach_content (exif, exif_data_foreach_content_func,
      (void *) taglist);

done:

  if (exif)
    exif_data_unref (exif);

  return;

}

static void
exif_data_foreach_content_func (ExifContent * content, void *user_data)
{
  ExifIfd ifd = exif_content_get_ifd (content);
  GstTagList *taglist = (GstTagList *) user_data;

  GST_LOG ("\n  Content %p: %s (ifd=%d)", content, exif_ifd_get_name (ifd),
      ifd);
  exif_content_foreach_entry (content, exif_content_foreach_entry_func,
      user_data);
}

static void
exif_content_foreach_entry_func (ExifEntry * entry, void *user_data)
{
  char buf[2048];
  GstTagList *taglist = (GstTagList *) user_data;

  GST_LOG ("\n    Entry %p: %s (%s)\n"
      "      Size, Comps: %d, %d\n"
      "      Value: %s\n"
      "      Title: %s\n"
      "      Description: %s\n",
      entry,
      exif_tag_get_name_in_ifd (entry->tag, EXIF_IFD_0),
      exif_format_get_name (entry->format),
      entry->size,
      (int) (entry->components),
      exif_entry_get_value (entry, buf, sizeof (buf)),
      exif_tag_get_title_in_ifd (entry->tag, EXIF_IFD_0),
      exif_tag_get_description_in_ifd (entry->tag, EXIF_IFD_0));
}

#endif /* else (ifndef HAVE_EXIF) */
