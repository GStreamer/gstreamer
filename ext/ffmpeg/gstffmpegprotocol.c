/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#include <string.h>
#include <errno.h>
#include <libav/avformat.h>
#include <gst/gst.h>
#include <gst/bytestream/bytestream.h>


typedef struct _GstProtocolInfo GstProtocolInfo;

struct _GstProtocolInfo
{
  GstPad *pad;

  int flags;
  GstByteStream *bs;
};

static int 
gst_open (URLContext *h, const char *filename, int flags)
{
  GstProtocolInfo *info;
  GstPad *pad;

  info = g_new0 (GstProtocolInfo, 1);
  info->flags = flags;

  if (sscanf (&filename[12], "%p", &pad) != 1) {
    g_warning ("could not decode pad from %s", &filename[12]);
    return -EIO;
  }

  if (!GST_IS_PAD (pad)) {
    g_warning ("decoded string is not a pad, %s", &filename[12]);
    return -EIO;
  }

  info->bs = gst_bytestream_new (pad);
  
  h->priv_data = (void *) info;

  return 0;
}

static int 
gst_read (URLContext *h, unsigned char *buf, int size)
{
  GstByteStream *bs;
  guint32 total;
  guint8 *data;
  GstProtocolInfo *info;

  info = (GstProtocolInfo *) h->priv_data;
  bs = info->bs;

  total = gst_bytestream_peek_bytes (bs, &data, size);
  memcpy (buf, data, total);

  gst_bytestream_flush_fast (bs, total);

  return total;
}

static int gst_write(URLContext *h, unsigned char *buf, int size)
{
  g_print ("write %p, %d\n", buf, size);

  return 0;
}

static int gst_close(URLContext *h)
{
  return 0;
}

URLProtocol gstreamer_protocol = {
  "gstreamer",
  gst_open,
  gst_read,
  gst_write,
  NULL,
  gst_close,
};

