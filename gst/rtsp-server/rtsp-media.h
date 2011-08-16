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

typedef gboolean (*GstRTSPSendFunc)      (GstBuffer *buffer, guint8 channel, gpointer user_data);
typedef gboolean (*GstRTSPSendListFunc)  (GstBufferList *blist, guint8 channel, gpointer user_data);
typedef void     (*GstRTSPKeepAliveFunc) (gpointer user_data);

/**
 * GstRTSPMediaTrans:
 * @idx: a stream index
 * @send_rtp: callback for sending RTP messages
 * @send_rtcp: callback for sending RTCP messages
 * @send_rtp_list: callback for sending RTP messages
 * @send_rtcp_list: callback for sending RTCP messages
 * @user_data: user data passed in the callbacks
 * @notify: free function for the user_data.
 * @keep_alive: keep alive callback
 * @ka_user_data: data passed to @keep_alive
 * @ka_notify: called when @ka_user_data is freed
 * @active: if we are actively sending
 * @timeout: if we timed out
 * @transport: a transport description
 * @rtpsource: the receiver rtp source object
 *
 * A Transport description for stream @idx
 */
struct _GstRTSPMediaTrans {
  guint idx;

  GstRTSPSendFunc      send_rtp;
  GstRTSPSendFunc      send_rtcp;
  GstRTSPSendListFunc  send_rtp_list;
  GstRTSPSendListFunc  send_rtcp_list;
  gpointer             user_data;
  GDestroyNotify       notify;

  GstRTSPKeepAliveFunc keep_alive;
  gpointer             ka_user_data;
  GDestroyNotify       ka_notify;
  gboolean             active;
  gboolean             timeout;

  GstRTSPTransport    *transport;

  GObject             *rtpsource;
};

#include "rtsp-auth.h"

/**
 * GstRTSPMediaStream:
 * @srcpad: the srcpad of the stream
 * @payloader: the payloader of the format
 * @prepared: if the stream is prepared for streaming
 * @recv_rtp_sink: sinkpad for RTP buffers
 * @recv_rtcp_sink: sinkpad for RTCP buffers
 * @send_rtp_src: srcpad for RTP buffers
 * @send_rtcp_src: srcpad for RTCP buffers
 * @udpsrc: the udp source elements for RTP/RTCP
 * @udpsink: the udp sink elements for RTP/RTCP
 * @appsrc: the app source elements for RTP/RTCP
 * @appsink: the app sink elements for RTP/RTCP
 * @server_port: the server ports for this stream
 * @caps_sig: the signal id for detecting caps
 * @caps: the caps of the stream
 * @tranports: the current transports being streamed
 *
 * The definition of a media stream. The streams are identified by @id.
 */
struct _GstRTSPMediaStream {
  GstPad       *srcpad;
  GstElement   *payloader;
  gboolean      prepared;

  /* pads on the rtpbin */
  GstPad       *recv_rtcp_sink;
  GstPad       *recv_rtp_sink;
  GstPad       *send_rtp_sink;
  GstPad       *send_rtp_src;
  GstPad       *send_rtcp_src;

  /* the RTPSession object */
  GObject      *session;

  /* sinks used for sending and receiving RTP and RTCP, they share
   * sockets */
  GstElement   *udpsrc[2];
  GstElement   *udpsink[2];
  /* for TCP transport */
  GstElement   *appsrc[2];
  GstElement   *appsink[2];

  GstElement   *tee[2];
  GstElement   *selector[2];

  /* server ports for sending/receiving */
  GstRTSPRange  server_port;

  /* the caps of the stream */
  gulong        caps_sig;
  GstCaps      *caps;

  /* transports we stream to */
  GList        *transports;
};

/**
 * GstRTSPMediaStatus:
 * @GST_RTSP_MEDIA_STATUS_UNPREPARED: media pipeline not prerolled
 * @GST_RTSP_MEDIA_STATUS_PREPARING: media pipeline is prerolling
 * @GST_RTSP_MEDIA_STATUS_PREPARED: media pipeline is prerolled
 * @GST_RTSP_MEDIA_STATUS_ERROR: media pipeline is in error
 *
 * The state of the media pipeline.
 */
typedef enum {
  GST_RTSP_MEDIA_STATUS_UNPREPARED = 0,
  GST_RTSP_MEDIA_STATUS_PREPARING  = 1,
  GST_RTSP_MEDIA_STATUS_PREPARED   = 2,
  GST_RTSP_MEDIA_STATUS_ERROR      = 3
} GstRTSPMediaStatus;

/**
 * GstRTSPMedia:
 * @lock: for protecting the object
 * @cond: for signaling the object
 * @shared: if this media can be shared between clients
 * @reusable: if this media can be reused after an unprepare
 * @protocols: the allowed lower transport for this stream
 * @reused: if this media has been reused
 * @is_ipv6: if this media is using ipv6
 * @element: the data providing element
 * @streams: the different streams provided by @element
 * @dynamic: list of dynamic elements managed by @element
 * @status: the status of the media pipeline
 * @active: the number of active connections
 * @pipeline: the toplevel pipeline
 * @fakesink: for making state changes async
 * @source: the bus watch for pipeline messages.
 * @id: the id of the watch
 * @is_live: if the pipeline is live
 * @buffering: if the pipeline is buffering
 * @target_state: the desired target state of the pipeline
 * @rtpbin: the rtpbin
 * @range: the range of the media being streamed
 *
 * A class that contains the GStreamer element along with a list of
 * #GstRTSPMediaStream objects that can produce data.
 *
 * This object is usually created from a #GstRTSPMediaFactory.
 */
struct _GstRTSPMedia {
  GObject            parent;

  GMutex            *lock;
  GCond             *cond;

  gboolean           shared;
  gboolean           reusable;
  GstRTSPLowerTrans  protocols;
  gboolean           reused;
  gboolean           is_ipv6;
  gboolean           eos_shutdown;
  guint              buffer_size;
  GstRTSPAuth       *auth;
  gchar             *multicast_group;

  GstElement        *element;
  GArray            *streams;
  GList             *dynamic;
  GstRTSPMediaStatus status;
  gint               active;
  gboolean           eos_pending;
  gboolean           adding;

  /* the pipeline for the media */
  GstElement        *pipeline;
  GstElement        *fakesink;
  GSource           *source;
  guint              id;

  gboolean           is_live;
  gboolean           buffering;
  GstState           target_state;

  /* RTP session manager */
  GstElement        *rtpbin;

  /* the range of media */
  GstRTSPTimeRange   range;
};

/**
 * GstRTSPMediaClass:
 * @context: the main context for dispatching messages
 * @loop: the mainloop for message.
 * @thread: the thread dispatching messages.
 * @handle_message: handle a message
 * @unprepare: the default implementation sets the pipeline's state
 *             to GST_STATE_NULL.
 *
 * The RTSP media class
 */
struct _GstRTSPMediaClass {
  GObjectClass  parent_class;

  /* thread for the mainloop */
  GMainContext *context;
  GMainLoop    *loop;
  GThread      *thread;

  /* vmethods */
  gboolean     (*handle_message)  (GstRTSPMedia *media, GstMessage *message);
  gboolean     (*unprepare)       (GstRTSPMedia *media);

  /* signals */
  gboolean     (*prepared)        (GstRTSPMedia *media);
  gboolean     (*unprepared)      (GstRTSPMedia *media);

  gboolean     (*new_state)       (GstRTSPMedia *media, GstState state);
};

GType                 gst_rtsp_media_get_type         (void);

/* creating the media */
GstRTSPMedia *        gst_rtsp_media_new              (void);

void                  gst_rtsp_media_set_shared       (GstRTSPMedia *media, gboolean shared);
gboolean              gst_rtsp_media_is_shared        (GstRTSPMedia *media);

void                  gst_rtsp_media_set_reusable     (GstRTSPMedia *media, gboolean reusable);
gboolean              gst_rtsp_media_is_reusable      (GstRTSPMedia *media);

void                  gst_rtsp_media_set_protocols    (GstRTSPMedia *media, GstRTSPLowerTrans protocols);
GstRTSPLowerTrans     gst_rtsp_media_get_protocols    (GstRTSPMedia *media);

void                  gst_rtsp_media_set_eos_shutdown (GstRTSPMedia *media, gboolean eos_shutdown);
gboolean              gst_rtsp_media_is_eos_shutdown  (GstRTSPMedia *media);

void                  gst_rtsp_media_set_auth         (GstRTSPMedia *media, GstRTSPAuth *auth);
GstRTSPAuth *         gst_rtsp_media_get_auth         (GstRTSPMedia *media);

void                  gst_rtsp_media_set_buffer_size  (GstRTSPMedia *media, guint size);
guint                 gst_rtsp_media_get_buffer_size  (GstRTSPMedia *media);

void                  gst_rtsp_media_set_multicast_group (GstRTSPMedia *media, const gchar * mc);
gchar *               gst_rtsp_media_get_multicast_group (GstRTSPMedia *media);


/* prepare the media for playback */
gboolean              gst_rtsp_media_prepare          (GstRTSPMedia *media);
gboolean              gst_rtsp_media_is_prepared      (GstRTSPMedia *media);
gboolean              gst_rtsp_media_unprepare        (GstRTSPMedia *media);

/* dealing with the media */
guint                 gst_rtsp_media_n_streams        (GstRTSPMedia *media);
GstRTSPMediaStream *  gst_rtsp_media_get_stream       (GstRTSPMedia *media, guint idx);

gboolean              gst_rtsp_media_seek             (GstRTSPMedia *media, GstRTSPTimeRange *range);
gchar *               gst_rtsp_media_get_range_string (GstRTSPMedia *media, gboolean play);

GstFlowReturn         gst_rtsp_media_stream_rtp       (GstRTSPMediaStream *stream, GstBuffer *buffer);
GstFlowReturn         gst_rtsp_media_stream_rtcp      (GstRTSPMediaStream *stream, GstBuffer *buffer);

gboolean              gst_rtsp_media_set_state        (GstRTSPMedia *media, GstState state, GArray *transports);

void                  gst_rtsp_media_remove_elements  (GstRTSPMedia *media);

void                  gst_rtsp_media_trans_cleanup    (GstRTSPMediaTrans *trans);

G_END_DECLS

#endif /* __GST_RTSP_MEDIA_H__ */
