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
#include "config.h"
#ifdef HAVE_FFMPEG_UNINSTALLED
#include <avformat.h>
#else
#include <ffmpeg/avformat.h>
#endif

#include <gst/gst.h>
#include <gst/bytestream.h>


typedef struct _GstProtocolInfo GstProtocolInfo;

struct _GstProtocolInfo {
  GstPad 	*pad;

  int 		 flags;
  GstByteStream *bs;
  gboolean 	 eos;
};

static int 
gst_open (URLContext *h,
	  const char *filename,
	  int         flags)
{
  GstProtocolInfo *info;
  GstPad *pad;

  info = g_new0 (GstProtocolInfo, 1);
  info->flags = flags;

  /* we don't support R/W together */
  if (flags != URL_RDONLY &&
      flags != URL_WRONLY) {
    g_warning ("Only read-only or write-only are supported");
    return -EINVAL;
  }

  if (sscanf (&filename[12], "%p", &pad) != 1) {
    g_warning ("could not decode pad from %s", filename);
    return -EIO;
  }

  /* make sure we're a pad and that we're of the right type */
  g_return_val_if_fail (GST_IS_PAD (pad), -EINVAL);

  switch (flags) {
    case URL_RDONLY:
      g_return_val_if_fail (GST_PAD_IS_SINK (pad), -EINVAL);
      info->bs = gst_bytestream_new (pad);
      break;
    case URL_WRONLY:
      g_return_val_if_fail (GST_PAD_IS_SRC (pad), -EINVAL);
      info->bs = NULL;
      break;
  }

  info->eos = FALSE;

  h->priv_data = (void *) info;

  return 0;
}

static int 
gst_read (URLContext    *h,
	  unsigned char *buf,
	  int            size)
{
  GstByteStream *bs;
  guint32 total;
  guint8 *data;
  GstProtocolInfo *info;

  info = (GstProtocolInfo *) h->priv_data;

  g_return_val_if_fail (info->flags == URL_RDONLY, -EIO);

  bs = info->bs;

  if (info->eos) 
    return 0;

  total = gst_bytestream_peek_bytes (bs, &data, size);

  if (total < size) {
    GstEvent *event;
    guint32 remaining;

    gst_bytestream_get_status (bs, &remaining, &event);

    if (!event) {
      g_warning ("gstffmpegprotocol: no bytestream event");
      return total;
    }

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_DISCONTINUOUS:
        gst_bytestream_flush_fast (bs, remaining);
      case GST_EVENT_EOS:
	info->eos = TRUE;
	break;
      default:
        break;
    }
    gst_event_unref (event);
  }
  
  memcpy (buf, data, total);
  gst_bytestream_flush (bs, total);

  return total;
}

static int
gst_write (URLContext    *h,
	   unsigned char *buf,
	   int            size)
{
  GstProtocolInfo *info;
  GstBuffer *outbuf;

  info = (GstProtocolInfo *) h->priv_data;

  g_return_val_if_fail (info->flags == URL_WRONLY, -EIO);

  /* create buffer and push data further */
  outbuf = gst_buffer_new_and_alloc (size);
  GST_BUFFER_SIZE (outbuf) = size;
  memcpy (GST_BUFFER_DATA (outbuf), buf, size);

  gst_pad_push (info->pad, GST_DATA (outbuf));

  return 0;
}

static offset_t
gst_seek (URLContext *h,
	  offset_t    pos,
	  int         whence)
{
  GstSeekType seek_type = 0;
  GstProtocolInfo *info;

  info = (GstProtocolInfo *) h->priv_data;

  switch (whence) {
    case SEEK_SET:
      seek_type = GST_SEEK_METHOD_SET;
      break;
    case SEEK_CUR:
      seek_type = GST_SEEK_METHOD_CUR;
      break;
    case SEEK_END:
      seek_type = GST_SEEK_METHOD_END;
      break;
    default:
      g_assert (0);
      break;
  }

  switch (info->flags) {
    case URL_RDONLY:
      gst_bytestream_seek (info->bs, pos, seek_type);
      break;

    case URL_WRONLY: {
      GstEvent *event = gst_event_new_seek (seek_type, pos);
      gst_pad_push (info->pad, GST_DATA (event));
    }
      break;
  }

  return 0;
}

static int
gst_close (URLContext *h)
{
  GstProtocolInfo *info;

  info = (GstProtocolInfo *) h->priv_data;

  switch (info->flags) {
    case URL_WRONLY: {
      /* send EOS - that closes down the stream */
      GstEvent *event = gst_event_new (GST_EVENT_EOS);
      gst_pad_push (info->pad, GST_DATA (event));
    }
      break;

    case URL_RDONLY:
      /* unref bytestream */
      gst_bytestream_destroy (info->bs);
      break;
  }

  /* clean up data */
  g_free (info);

  return 0;
}

URLProtocol gstreamer_protocol = {
  .name      = "gstreamer",
  .url_open  = gst_open,
  .url_read  = gst_read,
  .url_write = gst_write,
  .url_seek  = gst_seek,
  .url_close = gst_close,
};

