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

#include <gst/gst.h>
#include <gst/rtsp/gstrtsprange.h>
#include <gst/rtsp/gstrtspurl.h>

#ifndef __GST_RTSP_MEDIA_H__
#define __GST_RTSP_MEDIA_H__

G_BEGIN_DECLS

/* types for the media */
#define GST_TYPE_RTSP_MEDIA              (gst_rtsp_media_get_type ())
#define GST_IS_RTSP_MEDIA(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_RTSP_MEDIA))
#define GST_IS_RTSP_MEDIA_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_RTSP_MEDIA))
#define GST_RTSP_MEDIA_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_RTSP_MEDIA, GstRTSPMediaClass))
#define GST_RTSP_MEDIA(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_RTSP_MEDIA, GstRTSPMedia))
#define GST_RTSP_MEDIA_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_RTSP_MEDIA, GstRTSPMediaClass))
#define GST_RTSP_MEDIA_CAST(obj)         ((GstRTSPMedia*)(obj))
#define GST_RTSP_MEDIA_CLASS_CAST(klass) ((GstRTSPMediaClass*)(klass))

typedef struct _GstRTSPMediaStream GstRTSPMediaStream;
typedef struct _GstRTSPMedia GstRTSPMedia;
typedef struct _GstRTSPMediaClass GstRTSPMediaClass;
typedef struct _GstRTSPMediaTrans GstRTSPMediaTrans;

/**
 * GstRTSPMediaTrans:
 * @idx: a stream index
 * @transport: a transport description
 *
 * A Transport description for stream @idx
 */
struct _GstRTSPMediaTrans {
  guint idx;

  GstRTSPTransport *transport;
};

/**
 * GstRTSPMediaStream:
 *
 * @media: the owner #GstRTSPMedia
 * @srcpad: the srcpad of the stream
 * @payloader: the payloader of the format
 * @prepared: if the stream is prepared for streaming
 * @server_port: the server udp ports
 * @recv_rtp_sink: sinkpad for RTP buffers
 * @recv_rtcp_sink: sinkpad for RTCP buffers
 * @recv_rtp_src: srcpad for RTP buffers
 * @recv_rtcp_src: srcpad for RTCP buffers
 * @udpsrc: the udp source elements for RTP/RTCP
 * @udpsink: the udp sink elements for RTP/RTCP
 * @caps_sig: the signal id for detecting caps
 * @caps: the caps of the stream
 *
 * The definition of a media stream. The streams are identified by @id.
 */
struct _GstRTSPMediaStream {
  GstPad       *srcpad;
  GstElement   *payloader;
  gboolean      prepared;

  /* pads on the rtpbin */
  GstPad       *recv_rtcp_sink;
  GstPad       *send_rtp_sink;
  GstPad       *send_rtp_src;
  GstPad       *send_rtcp_src;

  /* sinks used for sending and receiving RTP and RTCP, they share
   * sockets */
  GstElement   *udpsrc[2];
  GstElement   *udpsink[2];

  /* server ports for sending/receiving */
  GstRTSPRange  server_port;

  /* the caps of the stream */
  gulong        caps_sig;
  GstCaps      *caps;
};

/**
 * GstRTSPMedia:
 * @shared: if this media can be shared between clients
 * @element: the data providing element
 * @stream: the different streams provided by @element
 * @prepared: if the media is prepared for streaming
 * @pipeline: the toplevel pipeline
 * @rtpbin: the rtpbin
 * @multifdsink: multifdsink element for TCP transport
 *
 * A class that contains the GStreamer element along with a list of
 * #GstRTSPediaStream objects that can produce data.
 *
 * This object is usually created from a #GstRTSPMediaFactory.
 */
struct _GstRTSPMedia {
  GObject       parent;

  gboolean      shared;
  gboolean      complete;

  GstElement   *element;
  GArray       *streams;
  gboolean      prepared;

  /* the pipeline for the media */
  GstElement   *pipeline;

  /* RTP session manager */
  GstElement   *rtpbin;

  /* for TCP transport */
  GstElement   *multifdsink;

  /* the range of media */
  GstRTSPTimeRange range;
};

struct _GstRTSPMediaClass {
  GObjectClass  parent_class;
};

GType                 gst_rtsp_media_get_type         (void);

/* creating the media */
GstRTSPMedia *        gst_rtsp_media_new              (void);

void                  gst_rtsp_media_set_shared       (GstRTSPMedia *media, gboolean shared);
gboolean              gst_rtsp_media_is_shared        (GstRTSPMedia *media);

/* prepare the media for playback */
gboolean              gst_rtsp_media_prepare          (GstRTSPMedia *media);

/* dealing with the media */
guint                 gst_rtsp_media_n_streams        (GstRTSPMedia *media);
GstRTSPMediaStream *  gst_rtsp_media_get_stream       (GstRTSPMedia *media, guint idx);

gboolean              gst_rtsp_media_play             (GstRTSPMedia *media, GArray *trans);
gboolean              gst_rtsp_media_pause            (GstRTSPMedia *media, GArray *trans);
gboolean              gst_rtsp_media_stop             (GstRTSPMedia *media, GArray *trans);

G_END_DECLS

#endif /* __GST_RTSP_MEDIA_H__ */
