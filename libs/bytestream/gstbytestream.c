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

static inline guint64
gst_bytestream_fill (GstByteStream *bs, guint64 len)
{
  GstBuffer *buf;

  g_print ("fill  %08llx %08llx\n", len, bs->pos);

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
   
  return len;
}

static inline void
gst_bytestream_shrink (GstByteStream *bs, guint64 len)
{
  GstBuffer *newbuf;

  bs->index += len;
  bs->pos += len;

  if ((GST_BUFFER_SIZE (bs->buffer) - bs->index) > 1024 * 1024) {
    g_print ("shrink%08llx %08llx, %08llx\n", len, bs->pos,
		  GST_BUFFER_SIZE (bs->buffer) - bs->index);

    newbuf = gst_buffer_create_sub (bs->buffer, bs->index, 
		  GST_BUFFER_SIZE (bs->buffer) - bs->index);
    bs->size = GST_BUFFER_SIZE (newbuf);
    gst_buffer_unref (bs->buffer);
    bs->index = 0;

    bs->buffer = newbuf;
  }
}

GstBuffer*
gst_bytestream_peek (GstByteStream *bs, guint64 len)
{
  GstBuffer *buf;

  g_print ("peek  %08llx %08llx\n", len, bs->pos);

  len = gst_bytestream_fill (bs, len);

  buf = gst_buffer_create_sub (bs->buffer, bs->index, len);

  return buf;
}

GstBuffer*                    
gst_bytestream_read (GstByteStream *bs, guint64 len)
{
  GstBuffer *buf;

  g_print ("read  %08llx %08llx\n", len, bs->pos);

  buf = gst_bytestream_peek (bs, len);

  gst_bytestream_shrink (bs, GST_BUFFER_SIZE (buf));

  return buf;
}

gboolean
gst_bytestream_seek (GstByteStream *bs, guint64 offset)
{
  return FALSE;
}

gint64
gst_bytestream_flush (GstByteStream *bs, guint64 len)
{
  guint64 outlen;

  g_print ("flush %08llx %08llx\n", len, bs->pos);

  if (len == 0)
    return len;

  outlen = gst_bytestream_fill (bs, len);

  gst_bytestream_shrink (bs, outlen);

  return outlen;
}
