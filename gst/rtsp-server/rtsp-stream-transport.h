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
#include <gst/base/base.h>
#include <gst/rtsp/gstrtsprange.h>
#include <gst/rtsp/gstrtspurl.h>

#ifndef __GST_RTSP_STREAM_TRANSPORT_H__
#define __GST_RTSP_STREAM_TRANSPORT_H__

#include "rtsp-server-prelude.h"

G_BEGIN_DECLS

/* types for the media */
#define GST_TYPE_RTSP_STREAM_TRANSPORT              (gst_rtsp_stream_transport_get_type ())
#define GST_IS_RTSP_STREAM_TRANSPORT(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_RTSP_STREAM_TRANSPORT))
#define GST_IS_RTSP_STREAM_TRANSPORT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_RTSP_STREAM_TRANSPORT))
#define GST_RTSP_STREAM_TRANSPORT_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_RTSP_STREAM_TRANSPORT, GstRTSPStreamTransportClass))
#define GST_RTSP_STREAM_TRANSPORT(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_RTSP_STREAM_TRANSPORT, GstRTSPStreamTransport))
#define GST_RTSP_STREAM_TRANSPORT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_RTSP_STREAM_TRANSPORT, GstRTSPStreamTransportClass))
#define GST_RTSP_STREAM_TRANSPORT_CAST(obj)         ((GstRTSPStreamTransport*)(obj))
#define GST_RTSP_STREAM_TRANSPORT_CLASS_CAST(klass) ((GstRTSPStreamTransportClass*)(klass))

typedef struct _GstRTSPStreamTransport GstRTSPStreamTransport;
typedef struct _GstRTSPStreamTransportClass GstRTSPStreamTransportClass;
typedef struct _GstRTSPStreamTransportPrivate GstRTSPStreamTransportPrivate;

#include "rtsp-stream.h"

/**
 * GstRTSPSendFunc:
 * @buffer: a #GstBuffer
 * @channel: a channel
 * @user_data: user data
 *
 * Function registered with gst_rtsp_stream_transport_set_callbacks() and
 * called when @buffer must be sent on @channel.
 *
 * Returns: %TRUE on success
 */
typedef gboolean (*GstRTSPSendFunc)      (GstBuffer *buffer, guint8 channel, gpointer user_data);

/**
 * GstRTSPSendListFunc:
 * @buffer_list: a #GstBufferList
 * @channel: a channel
 * @user_data: user data
 *
 * Function registered with gst_rtsp_stream_transport_set_callbacks() and
 * called when @buffer_list must be sent on @channel.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.16
 */
typedef gboolean (*GstRTSPSendListFunc)      (GstBufferList *buffer_list, guint8 channel, gpointer user_data);

/**
 * GstRTSPKeepAliveFunc:
 * @user_data: user data
 *
 * Function registered with gst_rtsp_stream_transport_set_keepalive() and called
 * when the stream is active.
 */
typedef void     (*GstRTSPKeepAliveFunc) (gpointer user_data);

/**
 * GstRTSPMessageSentFunc:
 * @user_data: user data
 *
 * Function registered with gst_rtsp_stream_transport_set_message_sent()
 * and called when a message has been sent on the transport.
 */
typedef void     (*GstRTSPMessageSentFunc) (gpointer user_data);

/**
 * GstRTSPMessageSentFuncFull:
 * @user_data: user data
 *
 * Function registered with gst_rtsp_stream_transport_set_message_sent_full()
 * and called when a message has been sent on the transport.
 *
 * Since: 1.18
 */
typedef void     (*GstRTSPMessageSentFuncFull) (GstRTSPStreamTransport *trans, gpointer user_data);

/**
 * GstRTSPStreamTransport:
 * @parent: parent instance
 *
 * A Transport description for a stream
 */
struct _GstRTSPStreamTransport {
  GObject              parent;

  /*< private >*/
  GstRTSPStreamTransportPrivate *priv;
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstRTSPStreamTransportClass {
  GObjectClass parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GST_RTSP_SERVER_API
GType                    gst_rtsp_stream_transport_get_type (void);

GST_RTSP_SERVER_API
GstRTSPStreamTransport * gst_rtsp_stream_transport_new           (GstRTSPStream *stream,
                                                                  GstRTSPTransport *tr);

GST_RTSP_SERVER_API
GstRTSPStream *          gst_rtsp_stream_transport_get_stream    (GstRTSPStreamTransport *trans);

GST_RTSP_SERVER_API
void                     gst_rtsp_stream_transport_set_transport (GstRTSPStreamTransport *trans,
                                                                  GstRTSPTransport * tr);

GST_RTSP_SERVER_API
const GstRTSPTransport * gst_rtsp_stream_transport_get_transport (GstRTSPStreamTransport *trans);

GST_RTSP_SERVER_API
void                     gst_rtsp_stream_transport_set_url       (GstRTSPStreamTransport *trans,
                                                                  const GstRTSPUrl * url);

GST_RTSP_SERVER_API
const GstRTSPUrl *       gst_rtsp_stream_transport_get_url       (GstRTSPStreamTransport *trans);


GST_RTSP_SERVER_API
gchar *                  gst_rtsp_stream_transport_get_rtpinfo   (GstRTSPStreamTransport *trans,
                                                                  GstClockTime start_time);

GST_RTSP_SERVER_API
void                     gst_rtsp_stream_transport_set_callbacks (GstRTSPStreamTransport *trans,
                                                                  GstRTSPSendFunc send_rtp,
                                                                  GstRTSPSendFunc send_rtcp,
                                                                  gpointer user_data,
                                                                  GDestroyNotify  notify);

GST_RTSP_SERVER_API
void                     gst_rtsp_stream_transport_set_list_callbacks (GstRTSPStreamTransport *trans,
                                                                       GstRTSPSendListFunc send_rtp_list,
                                                                       GstRTSPSendListFunc send_rtcp_list,
                                                                       gpointer user_data,
                                                                       GDestroyNotify  notify);

GST_RTSP_SERVER_API
void                     gst_rtsp_stream_transport_set_keepalive (GstRTSPStreamTransport *trans,
                                                                  GstRTSPKeepAliveFunc keep_alive,
                                                                  gpointer user_data,
                                                                  GDestroyNotify  notify);

GST_RTSP_SERVER_API
void                     gst_rtsp_stream_transport_set_message_sent (GstRTSPStreamTransport *trans,
                                                                     GstRTSPMessageSentFunc message_sent,
                                                                     gpointer user_data,
                                                                     GDestroyNotify  notify);

GST_RTSP_SERVER_API
void                     gst_rtsp_stream_transport_set_message_sent_full (GstRTSPStreamTransport *trans,
                                                                          GstRTSPMessageSentFuncFull message_sent,
                                                                          gpointer user_data,
                                                                          GDestroyNotify  notify);
GST_RTSP_SERVER_API
void                     gst_rtsp_stream_transport_keep_alive    (GstRTSPStreamTransport *trans);

GST_RTSP_SERVER_API
void                     gst_rtsp_stream_transport_message_sent  (GstRTSPStreamTransport *trans);

GST_RTSP_SERVER_API
gboolean                 gst_rtsp_stream_transport_set_active    (GstRTSPStreamTransport *trans,
                                                                  gboolean active);

GST_RTSP_SERVER_API
void                     gst_rtsp_stream_transport_set_timed_out (GstRTSPStreamTransport *trans,
                                                                  gboolean timedout);

GST_RTSP_SERVER_API
gboolean                 gst_rtsp_stream_transport_is_timed_out  (GstRTSPStreamTransport *trans);

GST_RTSP_SERVER_API
gboolean                 gst_rtsp_stream_transport_send_rtp      (GstRTSPStreamTransport *trans,
                                                                  GstBuffer *buffer);

GST_RTSP_SERVER_API
gboolean                 gst_rtsp_stream_transport_send_rtcp     (GstRTSPStreamTransport *trans,
                                                                  GstBuffer *buffer);

GST_RTSP_SERVER_API
gboolean                 gst_rtsp_stream_transport_send_rtp_list (GstRTSPStreamTransport *trans,
                                                                  GstBufferList *buffer_list);

GST_RTSP_SERVER_API
gboolean                 gst_rtsp_stream_transport_send_rtcp_list(GstRTSPStreamTransport *trans,
                                                                  GstBufferList *buffer_list);

GST_RTSP_SERVER_API
GstFlowReturn            gst_rtsp_stream_transport_recv_data     (GstRTSPStreamTransport *trans,
                                                                  guint channel, GstBuffer *buffer);

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstRTSPStreamTransport, gst_object_unref)
#endif

G_END_DECLS

#endif /* __GST_RTSP_STREAM_TRANSPORT_H__ */
