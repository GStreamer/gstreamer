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

G_DEFINE_TYPE (GstRTSPMedia, gst_rtsp_media, G_TYPE_OBJECT);

static void gst_rtsp_media_finalize (GObject * obj);

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

  g_free (media->location);
  gst_rtsp_url_free (media->url);

  for (i = 0; i < media->streams->len; i++) {
    GstRTSPMediaStream *stream;

    stream = g_array_index (media->streams, GstRTSPMediaStream *, i);

    gst_rtsp_media_stream_free (stream);
  }
  g_array_free (media->streams, TRUE);

  G_OBJECT_CLASS (gst_rtsp_media_parent_class)->finalize (obj);
}

/**
 * gst_rtsp_media_new:
 * @location: the URL of the media
 *
 * Create a new #GstRTSPMedia instance.
 *
 * Returns: a new #GstRTSPMedia object or %NULL when location did not contain a
 * valid or understood URL.
 */
GstRTSPMedia *
gst_rtsp_media_new (const gchar *location)
{
  GstRTSPMedia *result;
  GstRTSPUrl *url;

  url = NULL;

  if (gst_rtsp_url_parse (location, &url) != GST_RTSP_OK)
    goto invalid_url;

  result = g_object_new (GST_TYPE_RTSP_MEDIA, NULL);
  result->location = g_strdup (location);
  result->url = url;

  return result;

  /* ERRORS */
invalid_url:
  {
    return NULL;
  }
}

static void
caps_notify (GstPad * pad, GParamSpec * unused, GstRTSPMediaStream * stream)
{
  if (stream->caps)
    gst_caps_unref (stream->caps);
  if ((stream->caps = GST_PAD_CAPS (pad)))
    gst_caps_ref (stream->caps);
}

/**
 *
 * STREAMING CONFIGURATION
 *
 * gst_rtsp_media_prepare:
 * @media: a #GstRTSPMedia
 * @bin: the parent bin to create the elements in.
 *
 * Prepare the media object so that it creates its streams. Implementations
 * should crate the needed gstreamer elements and add them to @bin. No state
 * changes should be performed on them yet.
 *
 * One or more GstRTSPMediaStream objects should be added to @media with the
 * srcpad member set to a source pad that produces buffer of type 
 * application/x-rtp.
 *
 * Returns: %TRUE if the media could be prepared.
 */
gboolean
gst_rtsp_media_prepare (GstRTSPMedia *media, GstBin *bin)
{
  GstRTSPMediaStream *stream;
  GstElement *pay, *element;
  GstPad * pad;
  gint i;

  /* if we're already prepared we must exit */
  g_return_val_if_fail (media->prepared == FALSE, FALSE);

  g_print ("%s\n", media->url->abspath);

  if (g_str_has_prefix (media->url->abspath, "/camera")) {
    /* live */
    element = gst_parse_launch ("( "
	"v4l2src ! video/x-raw-yuv,width=352,height=288,framerate=15/1 ! "
	"queue ! videorate ! ffmpegcolorspace ! "
	"x264enc bitrate=300 ! rtph264pay name=pay0 pt=96 "
	"alsasrc ! audio/x-raw-int,rate=8000 ! queue ! "
	"amrnbenc ! rtpamrpay name=pay1 pt=97 "
	")", NULL);
  }
  else if (g_str_has_prefix (media->url->abspath, "/h264")) {
    /* transcode h264 */
    element = gst_parse_launch ("( uridecodebin "
	"uri=file:///home/cschalle/Videos/mi2.avi ! "
	"x264enc bitrate=300 ! rtph264pay name=pay0 )", NULL);
  }
  else if (g_str_has_prefix (media->url->abspath, "/theora")) {
    /* transcode theora */
    element = gst_parse_launch ("( uridecodebin "
  	"uri=file:///home/wim/data/mi2.avi ! "
	"theoraenc ! rtptheorapay name=pay0 )", NULL);
  }
  else if (g_str_has_prefix (media->url->abspath, "/macpclinux")) {
    /* theora/vorbis */
    element = gst_parse_launch ("( filesrc "
	"location=/home/cschalle/Videos/mac_pc_linux_2.ogg ! oggdemux name=d ! "
	"queue ! theoraparse ! rtptheorapay name=pay0 "
	"d. ! queue ! vorbisparse ! rtpvorbispay name=pay1 )", NULL);
  }
  else if (g_str_has_prefix (media->url->abspath, "/rtspproxy")) {
    /* proxy RTSP transcode */
    element = gst_parse_launch ("( uridecodebin "
	"uri=rtsp://ia300135.us.archive.org:554/0/items/uncovered_interviews/uncovered_interviews_3_256kb.mp4 ! "
	"x264enc bitrate=1800 ! rtph264pay name=pay0 )", NULL);
  }
  else if (g_str_has_prefix (media->url->abspath, "/httpproxy")) {
    /* proxy HTTP transcode */
    element = gst_parse_launch ("( uridecodebin "
	"uri=http://movies.apple.com/movies/fox/maxpayne/maxpayne-tlre_h480.mov name=d "
	"d. ! queue ! x264enc bitrate=1800 ! rtph264pay name=pay0 pt=96 "
	"d. ! queue ! faac ! rtpmp4gpay name=pay1 pt=97 )", NULL);
  }
  else
    return FALSE;

  gst_bin_add (bin, element);

  for (i = 0; ; i++) {
    gchar *name;

    name = g_strdup_printf ("pay%d", i);

    if (!(pay = gst_bin_get_by_name (GST_BIN (element), name))) {
      g_free (name);
      break;
    }
    
    /* create the stream */
    stream = g_new0 (GstRTSPMediaStream, 1);
    stream->media = media;
    stream->element = element;
    stream->payloader = pay;
    stream->idx = media->streams->len;

    pad = gst_element_get_static_pad (pay, "src");

    stream->srcpad = gst_ghost_pad_new (name, pad);
    gst_element_add_pad (stream->element, stream->srcpad);

    stream->caps_sig = g_signal_connect (pad, "notify::caps", (GCallback) caps_notify, stream);
    gst_object_unref (pad);

    /* add stream now */
    g_array_append_val (media->streams, stream);
    gst_object_unref (pay);

    g_free (name);
  }

  media->prepared = TRUE;

  return TRUE;
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
  g_return_val_if_fail (media->prepared, 0);

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
  g_return_val_if_fail (media->prepared, 0);
  g_return_val_if_fail (idx < media->streams->len, NULL);

  res = g_array_index (media->streams, GstRTSPMediaStream *, idx);

  return res;
}

