/* GStreamer
 * Copyright (C) 2008 Wim Taymans <wim.taymans at gmail.com>
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

#include "rtsp-media.h"

static void gst_rtsp_media_bin_finalize (GObject * obj);

G_DEFINE_TYPE (GstRTSPMediaBin, gst_rtsp_media_bin, G_TYPE_OBJECT);

static void
gst_rtsp_media_bin_class_init (GstRTSPMediaBinClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_rtsp_media_bin_finalize;
}

static void
gst_rtsp_media_bin_init (GstRTSPMediaBin * bin)
{
  bin->streams = g_array_new (FALSE, TRUE, sizeof (GstRTSPMediaStream *));
}

static void
gst_rtsp_media_stream_free (GstRTSPMediaStream *stream)
{
}

static void
gst_rtsp_media_bin_finalize (GObject * obj)
{
  GstRTSPMediaBin *bin;
  guint i;

  bin = GST_RTSP_MEDIA_BIN (obj);

  for (i = 0; i < bin->streams->len; i++) {
    GstRTSPMediaStream *stream;

    stream = g_array_index (bin->streams, GstRTSPMediaStream *, i);

    gst_rtsp_media_stream_free (stream);
  }
  g_array_free (bin->streams, TRUE);

  G_OBJECT_CLASS (gst_rtsp_media_bin_parent_class)->finalize (obj);
}

/**
 * gst_rtsp_media_bin_n_streams:
 * @media: a #GstRTSPMediaBin
 *
 * Get the number of streams in this mediabin.
 *
 * Returns: The number of streams.
 */
guint
gst_rtsp_media_bin_n_streams (GstRTSPMediaBin *bin)
{
  g_return_val_if_fail (GST_IS_RTSP_MEDIA_BIN (bin), 0);

  return bin->streams->len;
}

/**
 * gst_rtsp_media_bin_get_stream:
 * @bin: a #GstRTSPMediaBin
 * @idx: the stream index
 *
 * Retrieve the stream with index @idx from @bin.
 *
 * Returns: the #GstRTSPMediaStream at index @idx.
 */
GstRTSPMediaStream *
gst_rtsp_media_bin_get_stream (GstRTSPMediaBin *bin, guint idx)
{
  GstRTSPMediaStream *res;
  
  g_return_val_if_fail (GST_IS_RTSP_MEDIA_BIN (bin), NULL);
  g_return_val_if_fail (idx < bin->streams->len, NULL);

  res = g_array_index (bin->streams, GstRTSPMediaStream *, idx);

  return res;
}

