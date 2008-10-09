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

#include <gst/rtsp/gstrtsptransport.h>

#include "rtsp-media.h"

#ifndef __GST_RTSP_SESSION_H__
#define __GST_RTSP_SESSION_H__

G_BEGIN_DECLS

#define GST_TYPE_RTSP_SESSION              (gst_rtsp_session_get_type ())
#define GST_IS_RTSP_SESSION(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_RTSP_SESSION))
#define GST_IS_RTSP_SESSION_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_RTSP_SESSION))
#define GST_RTSP_SESSION_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_RTSP_SESSION, GstRTSPSessionClass))
#define GST_RTSP_SESSION(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_RTSP_SESSION, GstRTSPSession))
#define GST_RTSP_SESSION_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_RTSP_SESSION, GstRTSPSessionClass))
#define GST_RTSP_SESSION_CAST(obj)         ((GstRTSPSession*)(obj))
#define GST_RTSP_SESSION_CLASS_CAST(klass) ((GstRTSPSessionClass*)(klass))

typedef struct _GstRTSPSession GstRTSPSession;
typedef struct _GstRTSPSessionClass GstRTSPSessionClass;

typedef struct _GstRTSPSessionStream GstRTSPSessionStream;
typedef struct _GstRTSPSessionMedia GstRTSPSessionMedia;

/**
 * GstRTSPSessionStream:
 *
 * Configuration of a stream.
 */
struct _GstRTSPSessionStream
{
  guint idx;

  /* the owner media */
  GstRTSPSessionMedia *media;

  GstRTSPMediaStream *media_stream;

  /* client and server transports */
  gchar *destination;
  GstRTSPTransport *client_trans;
  GstRTSPTransport *server_trans;

  /* pads on the rtpbin */
  GstPad       *recv_rtcp_sink;
  GstPad       *send_rtp_sink;
  GstPad       *send_rtp_src;
  GstPad       *send_rtcp_src;

  /* sinks used for sending and receiving RTP and RTCP, they share sockets */
  GstElement   *udpsrc[2];
  GstElement   *udpsink[2];
};

/**
 * GstRTSPSessionMedia:
 *
 * State of a client session regarding a specific media.
 */
struct _GstRTSPSessionMedia
{
  /* the owner session */
  GstRTSPSession *session;

  /* the media we are handling */
  GstRTSPMedia *media;

  /* the pipeline for the media */
  GstElement   *pipeline;

  /* RTP session manager */
  GstElement   *rtpbin;

  /* for TCP transport */
  GstElement   *fdsink;

  /* configuration for the different streams */
  GList        *streams;
};

/**
 * GstRTSPSession:
 *
 * Session information kept by the server for a specific client.
 */
struct _GstRTSPSession {
  GObject       parent;

  gchar        *sessionid;

  GList        *medias;
};

struct _GstRTSPSessionClass {
  GObjectClass  parent_class;
};

GType                  gst_rtsp_session_get_type             (void);

GstRTSPSession *       gst_rtsp_session_new                  (const gchar *sessionid);

GstRTSPSessionMedia *  gst_rtsp_session_get_media            (GstRTSPSession *sess,
                                                              GstRTSPMedia *media);
GstRTSPSessionStream * gst_rtsp_session_get_stream           (GstRTSPSessionMedia *media,
                                                              guint idx);

GstStateChangeReturn   gst_rtsp_session_media_play           (GstRTSPSessionMedia *media);
GstStateChangeReturn   gst_rtsp_session_media_pause          (GstRTSPSessionMedia *media);
GstStateChangeReturn   gst_rtsp_session_media_stop           (GstRTSPSessionMedia *media);

GstRTSPTransport *     gst_rtsp_session_stream_set_transport (GstRTSPSessionStream *stream,
                                                              const gchar *destination,
                                                              GstRTSPTransport *ct);

G_END_DECLS

#endif /* __GST_RTSP_SESSION_H__ */
