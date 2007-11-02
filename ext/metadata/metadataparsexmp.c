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

#include "metadataparsexmp.h"
#include "metadataparseutil.h"

GST_DEBUG_CATEGORY (gst_metadata_parse_xmp_debug);
#define GST_CAT_DEFAULT gst_metadata_parse_xmp_debug

#define GST_TAG_XMP "xmp"

void
metadataparse_xmp_tags_register (void)
{
  gst_tag_register (GST_TAG_XMP, GST_TAG_FLAG_META,
      GST_TYPE_BUFFER, GST_TAG_XMP, "xmp metadata chunk", NULL);
}

#ifndef HAVE_XMP

void
metadataparse_xmp_tag_list_add (GstTagList * taglist, GstTagMergeMode mode,
    GstAdapter * adapter)
{

  GST_LOG ("XMP not defined, here I should send just one tag as whole chunk");

  metadataparse_util_tag_list_add_chunk (taglist, mode, GST_TAG_XMP, adapter);

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

#else /* ifndef HAVE_XMP */

#include <xmp.h>

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
    GstAdapter * adapter)
{
  const guint8 *buf;
  guint32 size;
  XmpPtr *xmp = NULL;
  XmpStringPtr xmp_str = NULL;

  if (adapter == NULL || (size = gst_adapter_available (adapter)) == 0) {
    goto done;
  }

  /* add chunk tag */
  metadataparse_util_tag_list_add_chunk (taglist, mode, GST_TAG_XMP, adapter);

  buf = gst_adapter_peek (adapter, size);

  xmp = xmp_new (buf, size);
  if (!xmp)
    goto done;

  xmp_str = xmp_string_new ();
  if (!xmp_str)
    goto done;

  xmp_serialize (xmp, xmp_str, XMP_SERIAL_ENCODEUTF8, 2);

  GST_LOG (xmp_string_cstr (xmp_str));

done:

  if (xmp_str) {
    xmp_string_free (xmp_str);
  }

  if (xmp) {
    xmp_free (xmp);
  }

  return;

}

#endif /* else (ifndef HAVE_XMP) */
