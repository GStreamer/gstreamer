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
#include <gst/rtsp/rtsp.h>
#include <gio/gio.h>

#ifndef __GST_RTSP_STREAM_H__
#define __GST_RTSP_STREAM_H__

#include "rtsp-server-prelude.h"

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
typedef struct _GstRTSPStreamPrivate GstRTSPStreamPrivate;

#include "rtsp-stream-transport.h"
#include "rtsp-address-pool.h"
#include "rtsp-session.h"
#include "rtsp-media.h"

/**
 * GstRTSPStream:
 *
 * The definition of a media stream.
 */
struct _GstRTSPStream {
  GObject       parent;

  /*< private >*/
  GstRTSPStreamPrivate *priv;
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstRTSPStreamClass {
  GObjectClass parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GST_RTSP_SERVER_API
GType             gst_rtsp_stream_get_type         (void);

GST_RTSP_SERVER_API
GstRTSPStream *   gst_rtsp_stream_new              (guint idx, GstElement *payloader,
                                                    GstPad *pad);

GST_RTSP_SERVER_API
guint             gst_rtsp_stream_get_index        (GstRTSPStream *stream);

GST_RTSP_SERVER_API
guint             gst_rtsp_stream_get_pt           (GstRTSPStream *stream);

GST_RTSP_SERVER_API
GstPad *          gst_rtsp_stream_get_srcpad       (GstRTSPStream *stream);

GST_RTSP_SERVER_API
GstPad *          gst_rtsp_stream_get_sinkpad      (GstRTSPStream *stream);

GST_RTSP_SERVER_API
void              gst_rtsp_stream_set_control      (GstRTSPStream *stream, const gchar *control);

GST_RTSP_SERVER_API
gchar *           gst_rtsp_stream_get_control      (GstRTSPStream *stream);

GST_RTSP_SERVER_API
gboolean          gst_rtsp_stream_has_control      (GstRTSPStream *stream, const gchar *control);

GST_RTSP_SERVER_API
void              gst_rtsp_stream_set_mtu          (GstRTSPStream *stream, guint mtu);

GST_RTSP_SERVER_API
guint             gst_rtsp_stream_get_mtu          (GstRTSPStream *stream);

GST_RTSP_SERVER_API
void              gst_rtsp_stream_set_dscp_qos     (GstRTSPStream *stream, gint dscp_qos);

GST_RTSP_SERVER_API
gint              gst_rtsp_stream_get_dscp_qos     (GstRTSPStream *stream);

GST_RTSP_SERVER_API
gboolean          gst_rtsp_stream_is_transport_supported  (GstRTSPStream *stream,
                                                           GstRTSPTransport *transport);

GST_RTSP_SERVER_API
void              gst_rtsp_stream_set_profiles     (GstRTSPStream *stream, GstRTSPProfile profiles);

GST_RTSP_SERVER_API
GstRTSPProfile    gst_rtsp_stream_get_profiles     (GstRTSPStream *stream);

GST_RTSP_SERVER_API
void              gst_rtsp_stream_set_protocols    (GstRTSPStream *stream, GstRTSPLowerTrans protocols);

GST_RTSP_SERVER_API
GstRTSPLowerTrans gst_rtsp_stream_get_protocols    (GstRTSPStream *stream);

GST_RTSP_SERVER_API
void              gst_rtsp_stream_set_address_pool (GstRTSPStream *stream, GstRTSPAddressPool *pool);

GST_RTSP_SERVER_API
GstRTSPAddressPool *
                  gst_rtsp_stream_get_address_pool (GstRTSPStream *stream);

GST_RTSP_SERVER_API
void              gst_rtsp_stream_set_multicast_iface (GstRTSPStream *stream, const gchar * multicast_iface);

GST_RTSP_SERVER_API
gchar *           gst_rtsp_stream_get_multicast_iface (GstRTSPStream *stream);

GST_RTSP_SERVER_API
GstRTSPAddress *  gst_rtsp_stream_reserve_address  (GstRTSPStream *stream,
                                                    const gchar * address,
                                                    guint port,
                                                    guint n_ports,
                                                    guint ttl);

GST_RTSP_SERVER_API
gboolean          gst_rtsp_stream_join_bin         (GstRTSPStream *stream,
                                                    GstBin *bin, GstElement *rtpbin,
                                                    GstState state);

GST_RTSP_SERVER_API
gboolean          gst_rtsp_stream_leave_bin        (GstRTSPStream *stream,
                                                    GstBin *bin, GstElement *rtpbin);

GST_RTSP_SERVER_API
GstBin *          gst_rtsp_stream_get_joined_bin   (GstRTSPStream *stream);

GST_RTSP_SERVER_API
gboolean          gst_rtsp_stream_set_blocked      (GstRTSPStream * stream,
                                                    gboolean blocked);

GST_RTSP_SERVER_API
gboolean          gst_rtsp_stream_is_blocking      (GstRTSPStream * stream);


GST_RTSP_SERVER_API
gboolean          gst_rtsp_stream_unblock_linked   (GstRTSPStream * stream);

GST_RTSP_SERVER_API
void              gst_rtsp_stream_set_client_side (GstRTSPStream *stream, gboolean client_side);

GST_RTSP_SERVER_API
gboolean          gst_rtsp_stream_is_client_side (GstRTSPStream *stream);

GST_RTSP_SERVER_API
void              gst_rtsp_stream_get_server_port  (GstRTSPStream *stream,
                                                    GstRTSPRange *server_port,
                                                    GSocketFamily family);

GST_RTSP_SERVER_API
GstRTSPAddress *  gst_rtsp_stream_get_multicast_address (GstRTSPStream *stream,
                                                         GSocketFamily family);


GST_RTSP_SERVER_API
GObject *         gst_rtsp_stream_get_rtpsession   (GstRTSPStream *stream);

GST_RTSP_SERVER_API
GstElement *      gst_rtsp_stream_get_srtp_encoder (GstRTSPStream *stream);

GST_RTSP_SERVER_API
void              gst_rtsp_stream_get_ssrc         (GstRTSPStream *stream,
                                                    guint *ssrc);

GST_RTSP_SERVER_API
gboolean          gst_rtsp_stream_get_rtpinfo      (GstRTSPStream *stream,
                                                    guint *rtptime, guint *seq,
                                                    guint *clock_rate,
                                                    GstClockTime *running_time);

GST_RTSP_SERVER_API
gboolean          gst_rtsp_stream_get_rates        (GstRTSPStream * stream,
                                                    gdouble * rate,
                                                    gdouble * applied_rate);

GST_RTSP_SERVER_API
GstCaps *         gst_rtsp_stream_get_caps         (GstRTSPStream *stream);

GST_RTSP_SERVER_API
GstFlowReturn     gst_rtsp_stream_recv_rtp         (GstRTSPStream *stream,
                                                    GstBuffer *buffer);

GST_RTSP_SERVER_API
GstFlowReturn     gst_rtsp_stream_recv_rtcp        (GstRTSPStream *stream,
                                                    GstBuffer *buffer);

GST_RTSP_SERVER_API
gboolean          gst_rtsp_stream_add_transport    (GstRTSPStream *stream,
                                                    GstRTSPStreamTransport *trans);

GST_RTSP_SERVER_API
gboolean          gst_rtsp_stream_remove_transport (GstRTSPStream *stream,
                                                    GstRTSPStreamTransport *trans);

GST_RTSP_SERVER_API
GSocket *         gst_rtsp_stream_get_rtp_socket   (GstRTSPStream *stream,
                                                    GSocketFamily family);

GST_RTSP_SERVER_API
GSocket *         gst_rtsp_stream_get_rtcp_socket  (GstRTSPStream *stream,
                                                    GSocketFamily family);

GST_RTSP_SERVER_API
GSocket *         gst_rtsp_stream_get_rtp_multicast_socket (GstRTSPStream *stream,
                                                            GSocketFamily family);

GST_RTSP_SERVER_API
GSocket *         gst_rtsp_stream_get_rtcp_multicast_socket (GstRTSPStream *stream,
                                                             GSocketFamily family);

GST_RTSP_SERVER_API
gboolean          gst_rtsp_stream_add_multicast_client_address (GstRTSPStream * stream,
                                                                const gchar * destination,
                                                                guint rtp_port,
                                                                guint rtcp_port,
                                                                GSocketFamily family);

GST_RTSP_SERVER_API
gchar *           gst_rtsp_stream_get_multicast_client_addresses (GstRTSPStream * stream);

GST_RTSP_SERVER_API
gboolean          gst_rtsp_stream_update_crypto    (GstRTSPStream * stream,
                                                    guint ssrc, GstCaps * crypto);

GST_RTSP_SERVER_API
gboolean          gst_rtsp_stream_query_position   (GstRTSPStream * stream,
                                                    gint64 * position);

GST_RTSP_SERVER_API
gboolean          gst_rtsp_stream_query_stop       (GstRTSPStream * stream,
                                                    gint64 * stop);

GST_RTSP_SERVER_API
gboolean          gst_rtsp_stream_seekable         (GstRTSPStream *stream);

GST_RTSP_SERVER_API
void              gst_rtsp_stream_set_seqnum_offset          (GstRTSPStream *stream, guint16 seqnum);

GST_RTSP_SERVER_API
guint16           gst_rtsp_stream_get_current_seqnum          (GstRTSPStream *stream);

GST_RTSP_SERVER_API
void              gst_rtsp_stream_set_retransmission_time     (GstRTSPStream *stream, GstClockTime time);

GST_RTSP_SERVER_API
GstClockTime      gst_rtsp_stream_get_retransmission_time     (GstRTSPStream *stream);

GST_RTSP_SERVER_API
guint             gst_rtsp_stream_get_retransmission_pt       (GstRTSPStream * stream);

GST_RTSP_SERVER_API
void              gst_rtsp_stream_set_retransmission_pt       (GstRTSPStream * stream,
                                                               guint rtx_pt);

GST_RTSP_SERVER_API
void              gst_rtsp_stream_set_buffer_size  (GstRTSPStream *stream, guint size);

GST_RTSP_SERVER_API
guint             gst_rtsp_stream_get_buffer_size  (GstRTSPStream *stream);

GST_RTSP_SERVER_API
void              gst_rtsp_stream_set_pt_map                 (GstRTSPStream * stream, guint pt, GstCaps * caps);

GST_RTSP_SERVER_API
GstElement *      gst_rtsp_stream_request_aux_sender         (GstRTSPStream * stream, guint sessid);

GST_RTSP_SERVER_API
GstElement *      gst_rtsp_stream_request_aux_receiver       (GstRTSPStream * stream, guint sessid);

GST_RTSP_SERVER_API
gboolean          gst_rtsp_stream_allocate_udp_sockets       (GstRTSPStream * stream, GSocketFamily family,
                                                              GstRTSPTransport *transport, gboolean use_client_settings);

GST_RTSP_SERVER_API
void                    gst_rtsp_stream_set_publish_clock_mode (GstRTSPStream * stream, GstRTSPPublishClockMode mode);

GST_RTSP_SERVER_API
GstRTSPPublishClockMode gst_rtsp_stream_get_publish_clock_mode (GstRTSPStream * stream);

GST_RTSP_SERVER_API
gboolean                gst_rtsp_stream_set_max_mcast_ttl  (GstRTSPStream *stream, guint ttl);

GST_RTSP_SERVER_API
guint             gst_rtsp_stream_get_max_mcast_ttl  (GstRTSPStream *stream);

GST_RTSP_SERVER_API
gboolean          gst_rtsp_stream_verify_mcast_ttl  (GstRTSPStream *stream, guint ttl);

GST_RTSP_SERVER_API
void              gst_rtsp_stream_set_bind_mcast_address  (GstRTSPStream * stream, gboolean bind_mcast_addr);

GST_RTSP_SERVER_API
gboolean          gst_rtsp_stream_is_bind_mcast_address (GstRTSPStream * stream);

GST_RTSP_SERVER_API
gboolean          gst_rtsp_stream_complete_stream (GstRTSPStream * stream, const GstRTSPTransport * transport);

GST_RTSP_SERVER_API
gboolean           gst_rtsp_stream_is_complete (GstRTSPStream * stream);

GST_RTSP_SERVER_API
gboolean           gst_rtsp_stream_is_sender (GstRTSPStream * stream);

GST_RTSP_SERVER_API
gboolean           gst_rtsp_stream_is_receiver (GstRTSPStream * stream);

GST_RTSP_SERVER_API
gboolean           gst_rtsp_stream_handle_keymgmt (GstRTSPStream *stream, const gchar *keymgmt);

/* ULP Forward Error Correction (RFC 5109) */
GST_RTSP_SERVER_API
gboolean           gst_rtsp_stream_get_ulpfec_enabled (GstRTSPStream *stream);

GST_RTSP_SERVER_API
void               gst_rtsp_stream_set_ulpfec_pt (GstRTSPStream *stream, guint pt);

GST_RTSP_SERVER_API
guint              gst_rtsp_stream_get_ulpfec_pt (GstRTSPStream *stream);

GST_RTSP_SERVER_API
GstElement *       gst_rtsp_stream_request_ulpfec_decoder (GstRTSPStream *stream, GstElement *rtpbin, guint sessid);

GST_RTSP_SERVER_API
GstElement *       gst_rtsp_stream_request_ulpfec_encoder (GstRTSPStream *stream, guint sessid);

GST_RTSP_SERVER_API
void               gst_rtsp_stream_set_ulpfec_percentage (GstRTSPStream *stream, guint percentage);

GST_RTSP_SERVER_API
guint              gst_rtsp_stream_get_ulpfec_percentage (GstRTSPStream *stream);

GST_RTSP_SERVER_API
void               gst_rtsp_stream_set_rate_control (GstRTSPStream * stream, gboolean enabled);

GST_RTSP_SERVER_API
gboolean           gst_rtsp_stream_get_rate_control (GstRTSPStream * stream);

GST_RTSP_SERVER_API
void               gst_rtsp_stream_unblock_rtcp (GstRTSPStream * stream);

/**
 * GstRTSPStreamTransportFilterFunc:
 * @stream: a #GstRTSPStream object
 * @trans: a #GstRTSPStreamTransport in @stream
 * @user_data: user data that has been given to gst_rtsp_stream_transport_filter()
 *
 * This function will be called by the gst_rtsp_stream_transport_filter(). An
 * implementation should return a value of #GstRTSPFilterResult.
 *
 * When this function returns #GST_RTSP_FILTER_REMOVE, @trans will be removed
 * from @stream.
 *
 * A return value of #GST_RTSP_FILTER_KEEP will leave @trans untouched in
 * @stream.
 *
 * A value of #GST_RTSP_FILTER_REF will add @trans to the result #GList of
 * gst_rtsp_stream_transport_filter().
 *
 * Returns: a #GstRTSPFilterResult.
 */
typedef GstRTSPFilterResult (*GstRTSPStreamTransportFilterFunc) (GstRTSPStream *stream,
                                                                 GstRTSPStreamTransport *trans,
                                                                 gpointer user_data);

GST_RTSP_SERVER_API
GList *                gst_rtsp_stream_transport_filter  (GstRTSPStream *stream,
                                                          GstRTSPStreamTransportFilterFunc func,
                                                          gpointer user_data);

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstRTSPStream, gst_object_unref)
#endif

G_END_DECLS

#endif /* __GST_RTSP_STREAM_H__ */
