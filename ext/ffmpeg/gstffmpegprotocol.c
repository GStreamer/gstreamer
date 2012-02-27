/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *               <2006> Edward Hervey <bilboed@bilboed.com>
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
#include <libavformat/avformat.h>
#endif

#include <gst/gst.h>

#include "gstffmpeg.h"
#include "gstffmpegpipe.h"

typedef struct _GstProtocolInfo GstProtocolInfo;

struct _GstProtocolInfo
{
  GstPad *pad;

  guint64 offset;
  gboolean eos;
  gint set_streamheader;
};

static int
gst_ffmpegdata_open (URLContext * h, const char *filename, int flags)
{
  GstProtocolInfo *info;
  GstPad *pad;

  GST_LOG ("Opening %s", filename);

  info = g_new0 (GstProtocolInfo, 1);

  info->set_streamheader = flags & GST_FFMPEG_URL_STREAMHEADER;
  flags &= ~GST_FFMPEG_URL_STREAMHEADER;
  h->flags &= ~GST_FFMPEG_URL_STREAMHEADER;

  /* we don't support R/W together */
  if (flags != URL_RDONLY && flags != URL_WRONLY) {
    GST_WARNING ("Only read-only or write-only are supported");
    return -EINVAL;
  }

  if (sscanf (&filename[12], "%p", &pad) != 1) {
    GST_WARNING ("could not decode pad from %s", filename);
    return -EIO;
  }

  /* make sure we're a pad and that we're of the right type */
  g_return_val_if_fail (GST_IS_PAD (pad), -EINVAL);

  switch (flags) {
    case URL_RDONLY:
      g_return_val_if_fail (GST_PAD_IS_SINK (pad), -EINVAL);
      break;
    case URL_WRONLY:
      g_return_val_if_fail (GST_PAD_IS_SRC (pad), -EINVAL);
      break;
  }

  info->eos = FALSE;
  info->pad = pad;
  info->offset = 0;

  h->priv_data = (void *) info;
  h->is_streamed = FALSE;
  h->max_packet_size = 0;

  return 0;
}

static int
gst_ffmpegdata_peek (URLContext * h, unsigned char *buf, int size)
{
  GstProtocolInfo *info;
  GstBuffer *inbuf = NULL;
  GstFlowReturn ret;
  int total = 0;

  g_return_val_if_fail (h->flags == URL_RDONLY, AVERROR (EIO));
  info = (GstProtocolInfo *) h->priv_data;

  GST_DEBUG ("Pulling %d bytes at position %" G_GUINT64_FORMAT, size,
      info->offset);

  ret = gst_pad_pull_range (info->pad, info->offset, (guint) size, &inbuf);

  switch (ret) {
    case GST_FLOW_OK:
      total = (gint) gst_buffer_get_size (inbuf);
      gst_buffer_extract (inbuf, 0, buf, total);
      gst_buffer_unref (inbuf);
      break;
    case GST_FLOW_EOS:
      total = 0;
      break;
    case GST_FLOW_FLUSHING:
      total = -1;
      break;
    default:
    case GST_FLOW_ERROR:
      total = -2;
      break;
  }

  GST_DEBUG ("Got %d (%s) return result %d", ret, gst_flow_get_name (ret),
      total);

  return total;
}

static int
gst_ffmpegdata_read (URLContext * h, unsigned char *buf, int size)
{
  gint res;
  GstProtocolInfo *info;

  info = (GstProtocolInfo *) h->priv_data;

  GST_DEBUG ("Reading %d bytes of data at position %" G_GUINT64_FORMAT, size,
      info->offset);

  res = gst_ffmpegdata_peek (h, buf, size);
  if (res >= 0)
    info->offset += res;

  GST_DEBUG ("Returning %d bytes", res);

  return res;
}

static int
gst_ffmpegdata_write (URLContext * h, const unsigned char *buf, int size)
{
  GstProtocolInfo *info;
  GstBuffer *outbuf;

  GST_DEBUG ("Writing %d bytes", size);
  info = (GstProtocolInfo *) h->priv_data;

  g_return_val_if_fail (h->flags != URL_RDONLY, -EIO);

  /* create buffer and push data further */
  outbuf = gst_buffer_new_and_alloc (size);

  gst_buffer_fill (outbuf, 0, buf, size);

  if (gst_pad_push (info->pad, outbuf) != GST_FLOW_OK)
    return 0;

  info->offset += size;
  return size;
}

static int64_t
gst_ffmpegdata_seek (URLContext * h, int64_t pos, int whence)
{
  GstProtocolInfo *info;
  guint64 newpos = 0, oldpos;

  GST_DEBUG ("Seeking to %" G_GINT64_FORMAT ", whence=%d",
      (gint64) pos, whence);

  info = (GstProtocolInfo *) h->priv_data;

  /* TODO : if we are push-based, we need to return sensible info */

  switch (h->flags) {
    case URL_RDONLY:
    {
      /* sinkpad */
      switch (whence) {
        case SEEK_SET:
          newpos = (guint64) pos;
          break;
        case SEEK_CUR:
          newpos = info->offset + pos;
          break;
        case SEEK_END:
        case AVSEEK_SIZE:
          /* ffmpeg wants to know the current end position in bytes ! */
        {
          gint64 duration;

          GST_DEBUG ("Seek end");

          if (gst_pad_is_linked (info->pad))
            if (gst_pad_query_duration (GST_PAD_PEER (info->pad),
                    GST_FORMAT_BYTES, &duration))
              newpos = ((guint64) duration) + pos;
        }
          break;
        default:
          g_assert (0);
          break;
      }
      /* FIXME : implement case for push-based behaviour */
      if (whence != AVSEEK_SIZE)
        info->offset = newpos;
    }
      break;
    case URL_WRONLY:
    {
      GstSegment segment;

      oldpos = info->offset;

      /* srcpad */
      switch (whence) {
        case SEEK_SET:
        {
          info->offset = (guint64) pos;
          break;
        }
        case SEEK_CUR:
          info->offset += pos;
          break;
        default:
          break;
      }
      newpos = info->offset;

      if (newpos != oldpos) {
        gst_segment_init (&segment, GST_FORMAT_BYTES);
        segment.start = newpos;
        segment.time = newpos;
        gst_pad_push_event (info->pad, gst_event_new_segment (&segment));
      }
      break;
    }
    default:
      g_assert (0);
      break;
  }

  GST_DEBUG ("Now at offset %" G_GUINT64_FORMAT " (returning %" G_GUINT64_FORMAT
      ")", info->offset, newpos);
  return newpos;
}

static int
gst_ffmpegdata_close (URLContext * h)
{
  GstProtocolInfo *info;

  info = (GstProtocolInfo *) h->priv_data;
  if (info == NULL)
    return 0;

  GST_LOG ("Closing file");

  switch (h->flags) {
    case URL_WRONLY:
    {
      /* send EOS - that closes down the stream */
      gst_pad_push_event (info->pad, gst_event_new_eos ());
      break;
    }
    default:
      break;
  }

  /* clean up data */
  g_free (info);
  h->priv_data = NULL;

  return 0;
}


URLProtocol gstreamer_protocol = {
  /*.name = */ "gstreamer",
  /*.url_open = */ gst_ffmpegdata_open,
  /*.url_read = */ gst_ffmpegdata_read,
  /*.url_write = */ gst_ffmpegdata_write,
  /*.url_seek = */ gst_ffmpegdata_seek,
  /*.url_close = */ gst_ffmpegdata_close,
};


/* specialized protocol for cross-thread pushing,
 * based on ffmpeg's pipe protocol */

static int
gst_ffmpeg_pipe_open (URLContext * h, const char *filename, int flags)
{
  GstFFMpegPipe *ffpipe;

  GST_LOG ("Opening %s", filename);

  /* we don't support W together */
  if (flags != URL_RDONLY) {
    GST_WARNING ("Only read-only is supported");
    return -EINVAL;
  }

  if (sscanf (&filename[10], "%p", &ffpipe) != 1) {
    GST_WARNING ("could not decode pipe info from %s", filename);
    return -EIO;
  }

  /* sanity check */
  g_return_val_if_fail (GST_IS_ADAPTER (ffpipe->adapter), -EINVAL);

  h->priv_data = (void *) ffpipe;
  h->is_streamed = TRUE;
  h->max_packet_size = 0;

  return 0;
}

static int
gst_ffmpeg_pipe_read (URLContext * h, unsigned char *buf, int size)
{
  GstFFMpegPipe *ffpipe;
  guint available;

  ffpipe = (GstFFMpegPipe *) h->priv_data;

  GST_LOG ("requested size %d", size);

  GST_FFMPEG_PIPE_MUTEX_LOCK (ffpipe);

  GST_LOG ("requested size %d", size);

  while ((available = gst_adapter_available (ffpipe->adapter)) < size
      && !ffpipe->eos) {
    GST_DEBUG ("Available:%d, requested:%d", available, size);
    ffpipe->needed = size;
    GST_FFMPEG_PIPE_SIGNAL (ffpipe);
    GST_FFMPEG_PIPE_WAIT (ffpipe);
  }

  size = MIN (available, size);
  if (size) {
    GST_LOG ("Getting %d bytes", size);
    gst_adapter_copy (ffpipe->adapter, buf, 0, size);
    gst_adapter_flush (ffpipe->adapter, size);
    GST_LOG ("%" G_GSIZE_FORMAT " bytes left in adapter",
        gst_adapter_available (ffpipe->adapter));
    ffpipe->needed = 0;
  }
  GST_FFMPEG_PIPE_MUTEX_UNLOCK (ffpipe);

  return size;
}

static int
gst_ffmpeg_pipe_close (URLContext * h)
{
  GST_LOG ("Closing pipe");

  h->priv_data = NULL;

  return 0;
}

URLProtocol gstpipe_protocol = {
  "gstpipe",
  gst_ffmpeg_pipe_open,
  gst_ffmpeg_pipe_read,
  NULL,
  NULL,
  gst_ffmpeg_pipe_close,
};
