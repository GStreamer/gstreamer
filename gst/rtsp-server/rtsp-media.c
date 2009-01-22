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

static void gst_rtsp_media_finalize (GObject * obj);

G_DEFINE_TYPE (GstRTSPMedia, gst_rtsp_media, G_TYPE_OBJECT);

static void
gst_rtsp_media_class_init (GstRTSPMediaClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_rtsp_media_finalize;
}

static void
gst_rtsp_media_init (GstRTSPMedia * media)
{
  media->streams = g_array_new (FALSE, TRUE, sizeof (GstRTSPMediaStream *));
}

static void
gst_rtsp_media_stream_free (GstRTSPMediaStream *stream)
{
}

static void
gst_rtsp_media_finalize (GObject * obj)
{
  GstRTSPMedia *media;
  guint i;

  media = GST_RTSP_MEDIA (obj);

  for (i = 0; i < media->streams->len; i++) {
    GstRTSPMediaStream *stream;

    stream = g_array_index (media->streams, GstRTSPMediaStream *, i);

    gst_rtsp_media_stream_free (stream);
  }
  g_array_free (media->streams, TRUE);

  G_OBJECT_CLASS (gst_rtsp_media_parent_class)->finalize (obj);
}

/**
 * gst_rtsp_media_n_streams:
 * @media: a #GstRTSPMedia
 *
 * Get the number of streams in this media.
 *
 * Returns: The number of streams.
 */
guint
gst_rtsp_media_n_streams (GstRTSPMedia *media)
{
  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), 0);

  return media->streams->len;
}

/**
 * gst_rtsp_media_get_stream:
 * @media: a #GstRTSPMedia
 * @idx: the stream index
 *
 * Retrieve the stream with index @idx from @media.
 *
 * Returns: the #GstRTSPMediaStream at index @idx.
 */
GstRTSPMediaStream *
gst_rtsp_media_get_stream (GstRTSPMedia *media, guint idx)
{
  GstRTSPMediaStream *res;
  
  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), NULL);
  g_return_val_if_fail (idx < media->streams->len, NULL);

  res = g_array_index (media->streams, GstRTSPMediaStream *, idx);

  return res;
}

