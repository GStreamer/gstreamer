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

#include "metadataxmp.h"
#include "metadataparseutil.h"
#include "metadatatags.h"

GST_DEBUG_CATEGORY (gst_metadata_xmp_debug);
#define GST_CAT_DEFAULT gst_metadata_xmp_debug

#ifndef HAVE_XMP

void
metadataparse_xmp_tag_list_add (GstTagList * taglist, GstTagMergeMode mode,
    GstAdapter * adapter, MetadataTagMapping mapping)
{

  if (mapping & METADATA_TAG_MAP_WHOLECHUNK) {
    GST_LOG ("XMP not defined, here I should send just one tag as whole chunk");
    metadataparse_util_tag_list_add_chunk (taglist, mode, GST_TAG_XMP, adapter);
  }

}

gboolean
metadataparse_xmp_init (void)
{
  return TRUE;
}

void
metadataparse_xmp_dispose (void)
{
  return;
}

void
metadatamux_xmp_create_chunk_from_tag_list (guint8 ** buf, guint32 * size,
    const GstTagList * taglist)
{
  /* do nothing */
}

#else /* ifndef HAVE_XMP */

#include <xmp.h>
#include <string.h>

#define XMP_SCHEMA_NODE 0x80000000UL

void
metadataparse_xmp_iter_array (XmpPtr xmp, const char *schema, const char *path);

static void
metadataparse_xmp_iter_simple (const char *schema, const char *path,
    const char *value);

static void metadataparse_xmp_iter (XmpPtr xmp, XmpIteratorPtr iter);

gboolean
metadataparse_xmp_init (void)
{
  return xmp_init ();
}

void
metadataparse_xmp_dispose (void)
{
  xmp_terminate ();
}

void
metadataparse_xmp_tag_list_add (GstTagList * taglist, GstTagMergeMode mode,
    GstAdapter * adapter, MetadataTagMapping mapping)
{
  const guint8 *buf;
  guint32 size;
  XmpPtr xmp = NULL;
  XmpIteratorPtr xmp_iter = NULL;

  if (adapter == NULL || (size = gst_adapter_available (adapter)) == 0) {
    goto done;
  }

  /* add chunk tag */
  if (mapping & METADATA_TAG_MAP_WHOLECHUNK)
    metadataparse_util_tag_list_add_chunk (taglist, mode, GST_TAG_XMP, adapter);

  if (!(mapping & METADATA_TAG_MAP_INDIVIDUALS))
    goto done;

  buf = gst_adapter_peek (adapter, size);

  xmp = xmp_new (buf, size);
  if (!xmp)
    goto done;

  xmp_iter = xmp_iterator_new (xmp, NULL, NULL, XMP_ITER_JUSTCHILDREN);

  if (!xmp_iter)
    goto done;

  metadataparse_xmp_iter (xmp, xmp_iter);

done:

  if (xmp_iter) {
    xmp_iterator_free (xmp_iter);
  }

  if (xmp) {
    xmp_free (xmp);
  }

  return;

}

void
metadataparse_xmp_iter_simple_qual (const char *schema, const char *path,
    const char *value)
{
  GString *string = g_string_new (path);
  gchar *ch;

  ch = string->str + string->len - 3;
  while (ch != string->str) {
    if (*ch == '[') {
      *ch = '\0';
    }
    --ch;
  }

  GST_LOG ("  %s = %s\n", string->str, value);
  g_string_free (string, TRUE);
}

void
metadataparse_xmp_iter_simple (const char *schema, const char *path,
    const char *value)
{
  GST_LOG ("  %s = %s\n", path, value);
}

void
metadataparse_xmp_iter_array (XmpPtr xmp, const char *schema, const char *path)
{

  XmpIteratorPtr xmp_iter = NULL;

  xmp_iter = xmp_iterator_new (xmp, schema, path, XMP_ITER_JUSTCHILDREN);

  if (xmp_iter) {
    metadataparse_xmp_iter (xmp, xmp_iter);

    xmp_iterator_free (xmp_iter);
  }

}

void
metadataparse_xmp_iter (XmpPtr xmp, XmpIteratorPtr iter)
{
  XmpStringPtr xstr_schema = xmp_string_new ();
  XmpStringPtr xstr_path = xmp_string_new ();
  XmpStringPtr xstr_prop = xmp_string_new ();
  uint32_t opt = 0;

  while (xmp_iterator_next (iter, xstr_schema, xstr_path, xstr_prop, &opt)) {
    const char *schema = xmp_string_cstr (xstr_schema);
    const char *path = xmp_string_cstr (xstr_path);
    const char *value = xmp_string_cstr (xstr_prop);

    if (XMP_IS_NODE_SCHEMA (opt)) {
      GST_LOG ("%s\n", schema);
      metadataparse_xmp_iter_array (xmp, schema, path);
    } else if (XMP_IS_PROP_SIMPLE (opt)) {
      if (strcmp (path, "") != 0) {
        if (XMP_HAS_PROP_QUALIFIERS (opt)) {
          metadataparse_xmp_iter_simple_qual (schema, path, value);
        } else {
          metadataparse_xmp_iter_simple (schema, path, value);
        }
      }
    } else if (XMP_IS_PROP_ARRAY (opt)) {
      if (XMP_IS_ARRAY_ALTTEXT (opt)) {
        metadataparse_xmp_iter_array (xmp, schema, path);
        xmp_iterator_skip (iter, XMP_ITER_SKIPSUBTREE);
      } else {
        metadataparse_xmp_iter_array (xmp, schema, path);
        xmp_iterator_skip (iter, XMP_ITER_SKIPSUBTREE);
      }
    }



  }

  if (xstr_prop) {
    xmp_string_free (xstr_prop);
  }
  if (xstr_path) {
    xmp_string_free (xstr_path);
  }
  if (xstr_schema) {
    xmp_string_free (xstr_schema);
  }
}

void
metadatamux_xmp_create_chunk_from_tag_list (guint8 ** buf, guint32 * size,
    const GstTagList * taglist)
{
  GstBuffer *xmp_chunk = NULL;
  const GValue *val = NULL;

  if (!(buf && size))
    goto done;
  if (*buf) {
    g_free (*buf);
    *buf = NULL;
  }
  *size = 0;

  val = gst_tag_list_get_value_index (taglist, GST_TAG_XMP, 0);
  if (val) {
    xmp_chunk = gst_value_get_buffer (val);
    if (xmp_chunk) {
      *size = GST_BUFFER_SIZE (xmp_chunk);
      *buf = g_new (guint8, *size);
      memcpy (*buf, GST_BUFFER_DATA (xmp_chunk), *size);
    }
  }

done:

  return;
}

#endif /* else (ifndef HAVE_XMP) */
