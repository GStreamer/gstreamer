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
#include <stdlib.h>

#include <gst/gstinfo.h>
#include "gstbytestream.h"

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
  GstByteStream *bs = g_new (GstByteStream, 1);

  bs->pad = pad;
  bs->buffer = NULL;
  bs->index = 0;
  bs->pos = 0;
  bs->size = 0;

  return bs;
}

void
gst_bytestream_destroy (GstByteStream *bs)
{
  if (bs->buffer) {
    gst_buffer_unref (bs->buffer);
  }
  g_free (bs);
}

GstBuffer*
gst_bytestream_bytes_peek (GstByteStream *bs, guint64 len)
{
  GstBuffer *buf;

  while ((bs->index + len) > bs->size) {
    buf = gst_pad_pull (bs->pad);
    
    if (!bs->buffer) {
      bs->buffer = buf;
    }
    else {
      bs->buffer = gst_buffer_merge (bs->buffer, buf);
    }
    bs->size = GST_BUFFER_SIZE (bs->buffer);
  }

  buf = gst_buffer_create_sub (bs->buffer, bs->index, len);

  return buf;
}

GstBuffer*                    
gst_bytestream_bytes_read (GstByteStream *bs, guint64 len)
{
  GstBuffer *buf;

  buf = gst_bytestream_bytes_peek (bs, len);
  bs->index += len;
  bs->pos += len;

  return buf;
}

gboolean
gst_bytestream_bytes_seek (GstByteStream *bs, guint64 offset)
{
  return FALSE;
}

gint
gst_bytestream_bytes_flush (GstByteStream *bs, guint64 len)
{
  if (len == 0)
    return len;
  
  return GST_BUFFER_SIZE (gst_bytestream_bytes_read (bs, len));
}
