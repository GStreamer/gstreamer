/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstbytestreams.c: Utility functions: gtk_get_property stuff, etc.
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

#include <stdio.h>
#include <string.h>

#include "gstbytestreams.h"

/**
 * gst_bytestream_new:
 * @pad: the pad to attach the bytstream to
 *
 * creates a bytestream from the given pad
 *
 * Returns: a new #GstByteStream object
 */
GstByteStream*
gst_bytestream_new (GstPad *pad)
{
  GstByteStream *bs = g_new(GstByteStream, 1);

  bs->pad = pad;
  bs->data = NULL;
  bs->size = 0;
  bs->index = 0;

  return bs;
}

void
gst_bytestream_destroy (GstByteStream *bs)
{
  if (bs->data) {
    g_free(bs->data);
  }
  g_free(bs);
}

static void 
gst_bytestream_bytes_fill (GstByteStream *bs, guint64 len) 
{
  size_t oldlen;
  GstBuffer *buf;

  while ((bs->index + len) > bs->size) {
    buf = gst_pad_pull(bs->pad);
    oldlen = bs->size - bs->index;
    memmove(bs->data, bs->data + bs->index, oldlen);
    bs->size = oldlen + GST_BUFFER_SIZE(buf);
    bs->index = 0;
    bs->data = realloc(bs->data, bs->size);
    if (!bs->data) {
      fprintf(stderr, "realloc failed: d:%p s:%d\n", bs->data, bs->size);
    }
    memcpy(bs->data + oldlen, GST_BUFFER_DATA(buf), GST_BUFFER_SIZE(buf));
  }
  g_assert ((bs->index + len) <= bs->size);
}

guint8*
gst_bytestream_bytes_peek (GstByteStream *bs, guint64 len)
{
  g_return_val_if_fail (len > 0, NULL);

  gst_bytestream_bytes_fill (bs, len);

  return gst_bytestream_pos (bs);
}

guint8*
gst_bytestream_bytes_read (GstByteStream *bs, guint64 len)
{
  guint8 *ptr;

  g_return_val_if_fail (len > 0, NULL);

  gst_bytestream_bytes_fill(bs, len);
  ptr = gst_bytestream_pos(bs);
  bs->index += len;

  return ptr;
}

gboolean
gst_bytestream_bytes_seek (GstByteStream *bs, guint64 offset)
{
  return FALSE;
}

void
gst_bytestream_bytes_flush (GstByteStream *bs, guint64 len)
{
  gst_bytestream_bytes_read(bs, len);
}

