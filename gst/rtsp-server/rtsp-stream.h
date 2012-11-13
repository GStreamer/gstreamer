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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <gst/gst.h>
#include <gst/rtsp/gstrtsprange.h>
#include <gst/rtsp/gstrtspurl.h>

#ifndef __GST_RTSP_STREAM_H__
#define __GST_RTSP_STREAM_H__

G_BEGIN_DECLS

/* types for the media stream */
#define GST_TYPE_RTSP_STREAM              (gst_rtsp_stream_get_type ())
#define GST_IS_RTSP_STREAM(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_RTSP_STREAM))
#define GST_IS_RTSP_STREAM_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_RTSP_STREAM))
#define GST_RTSP_STREAM_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_RTSP_STREAM, GstRTSPStreamClass))
#define GST_RTSP_STREAM(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_RTSP_STREAM, GstRTSPStream))
#define GST_RTSP_STREAM_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_RTSP_STREAM, GstRTSPStreamClass))
#define GST_RTSP_STREAM_CAST(obj)         ((GstRTSPStream*)(obj))
#define GST_RTSP_STREAM_CLASS_CAST(klass) ((GstRTSPStreamClass*)(klass))

typedef struct _GstRTSPStream GstRTSPStream;
typedef struct _GstRTSPStreamClass GstRTSPStreamClass;

#include "rtsp-stream-transport.h"

/**
 * GstRTSPStream:
 * @parent: the parent instance
 * @lock: mutex protecting the stream
 * @idx: the stream index
 * @srcpad: the srcpad of the stream
 * @payloader: the payloader of the format
 * @is_ipv6: should this stream be IPv6
 * @buffer_size: the UDP buffer size
 * @is_joined: if the stream is joined in a bin
 * @send_rtp_sink: sinkpad for sending RTP buffers
 * @recv_sink: sinkpad for receiving RTP/RTCP buffers
 * @send_src: srcpad for sending RTP/RTCP buffers
 * @session: the RTP session object
 * @udpsrc: the udp source elements for RTP/RTCP
 * @udpsink: the udp sink elements for RTP/RTCP
 * @appsrc: the app source elements for RTP/RTCP
 * @appqueue: the app queue elements for RTP/RTCP
 * @appsink: the app sink elements for RTP/RTCP
 * @tee: tee for the sending to udpsink and appsink
 * @funnel: tee for the receiving from udpsrc and appsrc
 * @server_port: the server ports for this stream
 * @caps_sig: the signal id for detecting caps
 * @caps: the caps of the stream
 * @n_active: the number of active transports in @transports
 * @transports: list of #GstStreamTransport being streamed to
 *
 * The definition of a media stream. The streams are identified by @idx.
 */
struct _GstRTSPStream {
  GObject       parent;

  GMutex        lock;
  guint         idx;
  GstPad       *srcpad;
  GstElement   *payloader;
  gboolean      is_ipv6;
  guint         buffer_size;
  gboolean      is_joined;

  /* pads on the rtpbin */
  GstPad       *send_rtp_sink;
  GstPad       *recv_sink[2];
  GstPad       *send_src[2];

  /* the RTPSession object */
  GObject      *session;

  /* sinks used for sending and receiving RTP and RTCP, they share
   * sockets */
  GstElement   *udpsrc[2];
  GstElement   *udpsink[2];
  /* for TCP transport */
  GstElement   *appsrc[2];
  GstElement   *appqueue[2];
  GstElement   *appsink[2];

  GstElement   *tee[2];
  GstElement   *funnel[2];

  /* server ports for sending/receiving */
  GstRTSPRange  server_port;

  /* the caps of the stream */
  gulong        caps_sig;
  GstCaps      *caps;

  /* transports we stream to */
  guint         n_active;
  GList        *transports;
};

struct _GstRTSPStreamClass {
  GObjectClass parent_class;
};

GType             gst_rtsp_stream_get_type         (void);

GstRTSPStream *   gst_rtsp_stream_new              (guint idx, GstElement *payloader,
                                                    GstPad *srcpad);

void              gst_rtsp_stream_set_mtu          (GstRTSPStream * stream, guint mtu);
guint             gst_rtsp_stream_get_mtu          (GstRTSPStream * stream);

gboolean          gst_rtsp_stream_join_bin         (GstRTSPStream * stream,
                                                    GstBin *bin, GstElement *rtpbin,
                                                    GstState state);
gboolean          gst_rtsp_stream_leave_bin        (GstRTSPStream * stream,
                                                    GstBin *bin, GstElement *rtpbin);

gboolean          gst_rtsp_stream_get_rtpinfo      (GstRTSPStream * stream,
                                                    guint *rtptime, guint * seq);

GstFlowReturn     gst_rtsp_stream_recv_rtp         (GstRTSPStream *stream,
                                                    GstBuffer *buffer);
GstFlowReturn     gst_rtsp_stream_recv_rtcp        (GstRTSPStream *stream,
                                                    GstBuffer *buffer);

gboolean          gst_rtsp_stream_add_transport    (GstRTSPStream *stream,
                                                    GstRTSPStreamTransport *trans);
gboolean          gst_rtsp_stream_remove_transport (GstRTSPStream *stream,
                                                    GstRTSPStreamTransport *trans);

G_END_DECLS

#endif /* __GST_RTSP_STREAM_H__ */
