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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include <errno.h>
#ifdef HAVE_FFMPEG_UNINSTALLED
#include <avformat.h>
#else
#include <ffmpeg/avformat.h>
#endif

#include <gst/gst.h>
#include <gst/bytestream/bytestream.h>

#include "gstffmpeg.h"

typedef struct _GstProtocolInfo GstProtocolInfo;

struct _GstProtocolInfo
{
  GstPad *pad;

  int flags;
  GstByteStream *bs;
  gboolean eos;
};

static int
gst_ffmpegdata_open (URLContext * h, const char *filename, int flags)
{
  GstProtocolInfo *info;
  GstPad *pad;

  info = g_new0 (GstProtocolInfo, 1);
  info->flags = flags;

  /* we don't support R/W together */
  if (flags != URL_RDONLY && flags != URL_WRONLY) {
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
  info->pad = pad;

  h->priv_data = (void *) info;
  h->is_streamed = FALSE;
  h->flags = 0;
  h->max_packet_size = 0;

  return 0;
}

static int
gst_ffmpegdata_peek (URLContext * h, unsigned char *buf, int size)
{
  GstByteStream *bs;
  guint32 total, request;
  guint8 *data;
  GstProtocolInfo *info;
  gboolean have_event;

  info = (GstProtocolInfo *) h->priv_data;

  g_return_val_if_fail (info->flags == URL_RDONLY, -EIO);

  bs = info->bs;

  if (info->eos)
    return 0;

  do {
    have_event = FALSE;

    /* prevent EOS */
    if (gst_bytestream_tell (bs) + size > gst_bytestream_length (bs)) {
      request = (int) (gst_bytestream_length (bs) - gst_bytestream_tell (bs));
      info->eos = TRUE;
    } else {
      request = size;
    }

    if (request > 0) {
      total = gst_bytestream_peek_bytes (bs, &data, request);
    } else {
      total = 0;
    }

    if (total < request) {
      GstEvent *event;
      guint32 remaining;

      gst_bytestream_get_status (bs, &remaining, &event);

      if (!event) {
        g_warning ("gstffmpegprotocol: no bytestream event");
        return total;
      }
      switch (GST_EVENT_TYPE (event)) {
        case GST_EVENT_DISCONTINUOUS:
          gst_event_unref (event);
          break;
        case GST_EVENT_EOS:
          g_warning ("Unexpected/unwanted eos in data function");
          info->eos = TRUE;
          have_event = TRUE;
          gst_event_unref (event);
          break;
        case GST_EVENT_FLUSH:
          gst_event_unref (event);
          break;
        case GST_EVENT_INTERRUPT:
          have_event = TRUE;
          gst_event_unref (event);
          break;
        default:
          gst_pad_event_default (info->pad, event);
          break;
      }
    }
  } while ((!info->eos && total != request) || have_event);

  memcpy (buf, data, total);

  return total;
}

static int
gst_ffmpegdata_read (URLContext * h, unsigned char *buf, int size)
{
  gint res;
  GstByteStream *bs;
  GstProtocolInfo *info;

  info = (GstProtocolInfo *) h->priv_data;
  bs = info->bs;
  res = gst_ffmpegdata_peek (h, buf, size);
  if (res > 0) {
    gst_bytestream_flush_fast (bs, res);
  }

  return res;
}

static int
gst_ffmpegdata_write (URLContext * h, unsigned char *buf, int size)
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

  return size;
}

static offset_t
gst_ffmpegdata_seek (URLContext * h, offset_t pos, int whence)
{
  GstSeekType seek_type = 0;
  GstProtocolInfo *info;
  guint64 newpos;

  info = (GstProtocolInfo *) h->priv_data;

  /* get data (typefind hack) */
  if (gst_bytestream_tell (info->bs) != gst_bytestream_length (info->bs)) {
    gchar buf;
    gst_ffmpegdata_peek (h, &buf, 1);
  }

  /* hack in ffmpeg to get filesize... */
  if (whence == SEEK_END && pos == -1)
    return gst_bytestream_length (info->bs) - 1;
  else if (whence == SEEK_END && pos == 0)
    return gst_bytestream_length (info->bs);
  /* another hack to get the current position... */
  else if (whence == SEEK_CUR && pos == 0)
    return gst_bytestream_tell (info->bs);

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
    case URL_RDONLY: {
      GstEvent *event;
      guint8 *data;
      guint32 remaining;

      /* handle discont */
      gst_bytestream_seek (info->bs, pos, seek_type);

      /* prevent eos */
      if (gst_bytestream_tell (info->bs) ==
              gst_bytestream_length (info->bs)) {
        info->eos = TRUE;
        break;
      }
      info->eos = FALSE;
      while (gst_bytestream_peek_bytes (info->bs, &data, 1) == 0) {
        gst_bytestream_get_status (info->bs, &remaining, &event);

        if (!event) {
          g_warning ("no data, no event - panic!");
          return -1;
        }

        switch (GST_EVENT_TYPE (event)) {
          case GST_EVENT_EOS:
            g_warning ("unexpected/unwanted EOS event after seek");
            info->eos = TRUE;
            gst_event_unref (event);
            return -1;
          case GST_EVENT_DISCONTINUOUS:
            gst_event_unref (event); /* we expect this */
            break;
          case GST_EVENT_FLUSH:
            break;
          case GST_EVENT_INTERRUPT:
            gst_event_unref (event);
            return -1;
          default:
            gst_pad_event_default (info->pad, event);
            break;
        }
      }
      newpos = gst_bytestream_tell (info->bs);
      break;
    }

    case URL_WRONLY:
      gst_pad_push (info->pad, GST_DATA (gst_event_new_seek (seek_type, pos)));
      /* this is screwy because there might be queues or scheduler-queued
       * buffers... Argh! */
      if (whence == SEEK_SET) {
        newpos = pos;
      } else {
        g_warning ("Writer reposition: implement me\n");
        newpos = 0;
      }
      break;

    default:
      g_assert (0);
      break;
  }

  return newpos;
}

static int
gst_ffmpegdata_close (URLContext * h)
{
  GstProtocolInfo *info;

  info = (GstProtocolInfo *) h->priv_data;

  switch (info->flags) {
    case URL_WRONLY:{
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
  .name = "gstreamer",
  .url_open = gst_ffmpegdata_open,
  .url_read = gst_ffmpegdata_read,
  .url_write = gst_ffmpegdata_write,
  .url_seek = gst_ffmpegdata_seek,
  .url_close = gst_ffmpegdata_close,
};
