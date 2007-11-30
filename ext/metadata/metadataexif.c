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

#include "metadataexif.h"
#include "metadataparseutil.h"
#include "metadatatags.h"

GST_DEBUG_CATEGORY (gst_metadata_exif_debug);
#define GST_CAT_DEFAULT gst_metadata_exif_debug

#ifndef HAVE_EXIF

void
metadataparse_exif_tag_list_add (GstTagList * taglist, GstTagMergeMode mode,
    GstAdapter * adapter, MetadataTagMapping mapping)
{

  GST_LOG ("EXIF not defined, here I should send just one tag as whole chunk");

  if (mapping & METADATA_TAG_MAP_WHOLECHUNK)
    metadataparse_util_tag_list_add_chunk (taglist, mode, GST_TAG_EXIF,
        adapter);

}

void
metadatamux_exif_create_chunk_from_tag_list (guint8 ** buf, guint32 * size,
    GstTagList * taglist)
{
  /* do nothing */
}

#else /* ifndef HAVE_EXIF */

#include <libexif/exif-data.h>

typedef struct _tag_MEUserData
{
  GstTagList *taglist;
  GstTagMergeMode mode;
} MEUserData;

typedef struct _tag_MapIntStr
{
  ExifTag exif;
  const gchar *str;
  GType type;
} MapIntStr;

static void
exif_data_foreach_content_func (ExifContent * content, void *callback_data);

static void exif_content_foreach_entry_func (ExifEntry * entry, void *);

const gchar *
metadataparse_exif_get_tag_from_exif (ExifTag exif, GType * type)
{
  /* FIXEME: sorted with binary search */
  static const MapIntStr array[] = {
    {EXIF_TAG_MAKE, GST_TAG_DEVICE_MAKE, G_TYPE_STRING},
    {EXIF_TAG_MODEL, GST_TAG_DEVICE_MODEL, G_TYPE_STRING},
    {0, NULL, G_TYPE_NONE}
  };
  int i = 0;

  while (array[i].exif) {
    if (exif == array[i].exif)
      break;
    ++i;
  }

  *type = array[i].type;
  return array[i].str;

}

void
metadataparse_exif_tag_list_add (GstTagList * taglist, GstTagMergeMode mode,
    GstAdapter * adapter, MetadataTagMapping mapping)
{
  const guint8 *buf;
  guint32 size;
  ExifData *exif = NULL;
  MEUserData user_data = { taglist, mode };

  if (adapter == NULL || (size = gst_adapter_available (adapter)) == 0) {
    goto done;
  }

  /* add chunk tag */
  if (mapping & METADATA_TAG_MAP_WHOLECHUNK)
    metadataparse_util_tag_list_add_chunk (taglist, mode, GST_TAG_EXIF,
        adapter);

  if (!(mapping & METADATA_TAG_MAP_INDIVIDUALS))
    goto done;

  buf = gst_adapter_peek (adapter, size);

  exif = exif_data_new_from_data (buf, size);
  if (exif == NULL) {
    goto done;
  }

  exif_data_foreach_content (exif, exif_data_foreach_content_func,
      (void *) &user_data);

done:

  if (exif)
    exif_data_unref (exif);

  return;

}

static void
exif_data_foreach_content_func (ExifContent * content, void *user_data)
{
  ExifIfd ifd = exif_content_get_ifd (content);

  GST_LOG ("\n  Content %p: %s (ifd=%d)", content, exif_ifd_get_name (ifd),
      ifd);
  exif_content_foreach_entry (content, exif_content_foreach_entry_func,
      user_data);
}

static void
exif_content_foreach_entry_func (ExifEntry * entry, void *user_data)
{
  char buf[2048];
  MEUserData *meudata = (MEUserData *) user_data;
  GType type;
  const gchar *tag = metadataparse_exif_get_tag_from_exif (entry->tag, &type);

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

  if (tag) {
    /* FIXME: create a generic function for this */
    /* could also be used with entry->format */
    if (type == G_TYPE_STRING)
      gst_tag_list_add (meudata->taglist, meudata->mode, tag,
          exif_entry_get_value (entry, buf, sizeof (buf)), NULL);
  }

}

/*
 *
 */

void
metadatamux_exif_create_chunk_from_tag_list (guint8 ** buf, guint32 * size,
    GstTagList * taglist)
{
  ExifData *ed = NULL;
  GstBuffer *exif_chunk = NULL;
  const GValue *val = NULL;

  if (!(buf && size))
    goto done;
  if (*buf) {
    g_free (*buf);
    *buf = NULL;
  }
  *size = 0;

  val = gst_tag_list_get_value_index (taglist, GST_TAG_EXIF, 0);
  if (val) {
    exif_chunk = gst_value_get_buffer (val);
    if (exif_chunk) {
      ed = exif_data_new_from_data (GST_BUFFER_DATA (exif_chunk),
          GST_BUFFER_SIZE (exif_chunk));
    }
  }

  if (!ed) {
    ed = exif_data_new ();
    exif_data_set_data_type (ed, EXIF_DATA_TYPE_COMPRESSED);
    exif_data_fix (ed);
  }

  /* FIXME: consider individual tags */

  exif_data_save_data (ed, buf, size);


done:

  if (ed)
    exif_data_unref (ed);

  return;
}

#endif /* else (ifndef HAVE_EXIF) */
