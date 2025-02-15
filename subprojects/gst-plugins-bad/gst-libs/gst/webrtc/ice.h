/* GStreamer
 * Copyright (C) 2017 Matthew Waters <matthew@centricular.com>
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

#ifndef __GST_WEBRTC_ICE_H__
#define __GST_WEBRTC_ICE_H__

#include <gst/webrtc/webrtc_fwd.h>

G_BEGIN_DECLS

GST_WEBRTC_API
GType gst_webrtc_ice_get_type(void);
#define GST_TYPE_WEBRTC_ICE            (gst_webrtc_ice_get_type())
#define GST_WEBRTC_ICE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_WEBRTC_ICE,GstWebRTCICE))
#define GST_IS_WEBRTC_ICE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_WEBRTC_ICE))
#define GST_WEBRTC_ICE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_WEBRTC_ICE,GstWebRTCICEClass))
#define GST_IS_WEBRTC_ICE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_WEBRTC_ICE))
#define GST_WEBRTC_ICE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_WEBRTC_ICE,GstWebRTCICEClass))

struct _GstWebRTCICE
{
  GstObject                          parent;

  GstWebRTCICEGatheringState         ice_gathering_state;
  GstWebRTCICEConnectionState        ice_connection_state;

  /*< protected >*/
  guint                              min_rtp_port;
  guint                              max_rtp_port;

  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstWebRTCICECandidateStats:
 * @ipaddr: A string containing the address of the candidate. This value may be
 *          an IPv4 address, an IPv6 address, or a fully-qualified domain name
 * @port: The network port number used by the candidate
 * @stream_id: A string that uniquely identifies the object that is being
 *             monitored to produce this set of statistics
 * @type: The candidate type
 * @proto: A string specifying the protocol (tcp or udp) used to transmit data
 *         on the @port
 * @replay_proto: A string identifying the protocol used by the endpoint for
 *                communicating with the TURN server; valid values are tcp, udp, and tls
 * @prio: The candidate's priority, corresponding to RTCIceCandidate.priority
 * @url: For local candidates, the url property is the URL of the ICE server
 *       from which the candidate was received
 * @foundation: The ICE foundation as defined in RFC5245 section 15.1 (Since: 1.28)
 * @related_address: The ICE rel-addr defined in RFC5245 section 15.1 Only
 *                   set for serverreflexive, peerreflexive and relay candidates. (Since: 1.28)
 * @related_port: The ICE rel-addr defined in RFC5245 section 15.1. Only set
 *                for serverreflexive, peerreflexive and relay candidates. (Since: 1.28)
 * @username_fragment: The ICE username fragment as defined in RFC5245 section 7.1.2.3 (Since: 1.28)
 * @tcp_type: The ICE candidate TCP type, (Since: 1.28)
 *
 * Since: 1.22
 */
struct _GstWebRTCICECandidateStats
{
  gchar                            *ipaddr;
  guint                             port;
  guint                             stream_id;
  const gchar                      *type;
  const gchar                      *proto;
  const gchar                      *relay_proto;
  guint                             prio;
  gchar                            *url;

  /**
   * GstWebRTCICECandidateStats.ABI: (attributes doc.skip=true)
   *
   * ABI compatibility union
   *
   * Since: 1.28
   */
  union {
    /**
     * GstWebRTCICECandidateStats.ABI.abi: (attributes doc.skip=true)
     *
     * ABI compatibility struct
     *
     * Since: 1.28
     */
    struct {
      /**
       * GstWebRTCICECandidateStats.ABI.abi.foundation:
       *
       * The foundation of the ICE candidate.
       *
       * Since: 1.28
       */
      gchar *foundation;

      /**
       * GstWebRTCICECandidateStats.ABI.abi.related_address:
       *
       * The related address (STUN or TURN server) of the candidate. Will be
       * NULL for host candidates.
       *
       * Since: 1.28
       */
      gchar *related_address;

      /**
       * GstWebRTCICECandidateStats.ABI.abi.related_port:
       *
       * The related port (STUN or TURN server) of the candidate. Will be
       * 0 for host candidates.
       *
       * Since: 1.28
       */
      guint related_port;

      /**
       * GstWebRTCICECandidateStats.ABI.abi.username_fragment:
       *
       * The ICE username for this candidate.
       *
       * Since: 1.28
       */
      gchar *username_fragment;

      /**
       * GstWebRTCICECandidateStats.ABI.abi.tcp_type:
       *
       * The type of TCP candidate. Will be NULL if the candidate is not a TCP
       * candidate.
       *
       * Since: 1.28
       */
      GstWebRTCICETcpCandidateType tcp_type;
    } abi;
    /*< private >*/
    gpointer _gst_reserved[GST_PADDING_LARGE];
  } ABI;
};

/**
 * GST_WEBRTC_ICE_CANDIDATE_STATS_ADDRESS:
 *
 * Since: 1.28
 */
#define GST_WEBRTC_ICE_CANDIDATE_STATS_ADDRESS(c) ((c)->ipaddr)
/**
 * GST_WEBRTC_ICE_CANDIDATE_STATS_PORT:
 *
 * Since: 1.28
 */
#define GST_WEBRTC_ICE_CANDIDATE_STATS_PORT(c) ((c)->port)
/**
 * GST_WEBRTC_ICE_CANDIDATE_STATS_STREAM_ID:
 *
 * Since: 1.28
 */
#define GST_WEBRTC_ICE_CANDIDATE_STATS_STREAM_ID(c) ((c)->stream_id)
/**
 * GST_WEBRTC_ICE_CANDIDATE_STATS_TYPE:
 *
 * Since: 1.28
 */
#define GST_WEBRTC_ICE_CANDIDATE_STATS_TYPE(c) ((c)->type)
/**
 * GST_WEBRTC_ICE_CANDIDATE_STATS_PROTOCOL:
 *
 * Since: 1.28
 */
#define GST_WEBRTC_ICE_CANDIDATE_STATS_PROTOCOL(c) ((c)->proto)
/**
 * GST_WEBRTC_ICE_CANDIDATE_STATS_RELAY_PROTOCOL:
 *
 * Since: 1.28
 */
#define GST_WEBRTC_ICE_CANDIDATE_STATS_RELAY_PROTOCOL(c) ((c)->relay_proto)
/**
 * GST_WEBRTC_ICE_CANDIDATE_STATS_PRIORITY:
 *
 * Since: 1.28
 */
#define GST_WEBRTC_ICE_CANDIDATE_STATS_PRIORITY(c) ((c)->prio)
/**
 * GST_WEBRTC_ICE_CANDIDATE_STATS_URL:
 *
 * Since: 1.28
 */
#define GST_WEBRTC_ICE_CANDIDATE_STATS_URL(c) ((c)->url)

/**
 * GST_WEBRTC_ICE_CANDIDATE_STATS_FOUNDATION:
 *
 * Since: 1.28
 */
#define GST_WEBRTC_ICE_CANDIDATE_STATS_FOUNDATION(c) ((c)->ABI.abi.foundation)
/**
 * GST_WEBRTC_ICE_CANDIDATE_STATS_RELATED_ADDRESS:
 *
 * Since: 1.28
 */
#define GST_WEBRTC_ICE_CANDIDATE_STATS_RELATED_ADDRESS(c) ((c)->ABI.abi.related_address)
/**
 * GST_WEBRTC_ICE_CANDIDATE_STATS_RELATED_PORT:
 *
 * Since: 1.28
 */
#define GST_WEBRTC_ICE_CANDIDATE_STATS_RELATED_PORT(c) ((c)->ABI.abi.related_port)
/**
 * GST_WEBRTC_ICE_CANDIDATE_STATS_USERNAME_FRAGMENT:
 *
 * Since: 1.28
 */
#define GST_WEBRTC_ICE_CANDIDATE_STATS_USERNAME_FRAGMENT(c) ((c)->ABI.abi.username_fragment)
/**
 * GST_WEBRTC_ICE_CANDIDATE_STATS_TCP_TYPE:
 *
 * Since: 1.28
 */
#define GST_WEBRTC_ICE_CANDIDATE_STATS_TCP_TYPE(c) ((c)->ABI.abi.tcp_type)

/**
 * GstWebRTCICECandidate:
 * @candidate: String carrying the candidate-attribute as defined in
 *   section 15.1 of RFC5245
 * @component: The assigned network component of the candidate (1 for RTP
 *   2 for RTCP).
 * @sdp_mid: The media stream "identification-tag" defined in [RFC5888] for the
 *   media component this candidate is associated with.
 * @sdp_mline_index: The index (starting at zero) of the media description in
 *   the SDP this candidate is associated with.
 * @stats: The #GstWebRTCICECandidateStats associated to this candidate.
 *
 * Since: 1.28
 */
struct _GstWebRTCICECandidate {
  gchar                             *candidate;
  gint                               component;
  gchar                             *sdp_mid;
  gint                               sdp_mline_index; /* Set to -1 if unknown. */
  GstWebRTCICECandidateStats        *stats;

  gpointer _gst_reserved[GST_PADDING_LARGE];
};

struct _GstWebRTCICECandidatePair {
  GstWebRTCICECandidate *local;
  GstWebRTCICECandidate *remote;
};

/**
 * GstWebRTCICEOnCandidateFunc:
 * @ice: The #GstWebRTCICE
 * @stream_id: The stream id
 * @candidate: The discovered candidate
 * @user_data: User data that was set by #gst_webrtc_ice_set_on_ice_candidate
 *
 * Callback function to be triggered on discovery of a new candidate
 * Since: 1.22
 */
typedef void (*GstWebRTCICEOnCandidateFunc) (GstWebRTCICE * ice, guint stream_id, const gchar * candidate, gpointer user_data);

struct _GstWebRTCICEClass {
  GstObjectClass parent_class;
  GstWebRTCICEStream * (*add_stream)                   (GstWebRTCICE * ice,
                                                        guint session_id);
  GstWebRTCICETransport * (*find_transport)            (GstWebRTCICE * ice,
                                                        GstWebRTCICEStream * stream,
                                                        GstWebRTCICEComponent component);
  gboolean (*gather_candidates)                        (GstWebRTCICE * ice,
                                                        GstWebRTCICEStream * stream);
  void (*add_candidate)                                (GstWebRTCICE * ice,
                                                        GstWebRTCICEStream * stream,
                                                        const gchar * candidate,
                                                        GstPromise * promise);
  gboolean (*set_local_credentials)                    (GstWebRTCICE * ice,
                                                        GstWebRTCICEStream * stream,
                                                        const gchar * ufrag,
                                                        const gchar * pwd);
  gboolean (*set_remote_credentials)                   (GstWebRTCICE * ice,
                                                        GstWebRTCICEStream * stream,
                                                        const gchar * ufrag,
                                                        const gchar * pwd);
  gboolean (*add_turn_server)                          (GstWebRTCICE * ice,
                                                        const gchar * uri);
  void (*set_is_controller)                            (GstWebRTCICE * ice,
                                                        gboolean controller);
  gboolean (*get_is_controller)                        (GstWebRTCICE * ice);
  void (*set_force_relay)                              (GstWebRTCICE * ice,
                                                        gboolean force_relay);
  void (*set_stun_server)                              (GstWebRTCICE * ice,
                                                        const gchar * uri);
  gchar * (*get_stun_server)                           (GstWebRTCICE * ice);
  void (*set_turn_server)                              (GstWebRTCICE * ice,
                                                        const gchar * uri);
  gchar * (*get_turn_server)                           (GstWebRTCICE * ice);

  /**
   * GstWebRTCICEClass::set_http_proxy:
   * @ice: a #GstWebRTCICE
   * @uri: (transfer none): URI of the HTTP proxy of the form
   *   http://[username:password@]hostname[:port]
   *
   * Set HTTP Proxy to be used when connecting to TURN server.
   *
   * Since: 1.22
   */
  void (*set_http_proxy)                               (GstWebRTCICE * ice,
                                                        const gchar * uri);

  /**
   * GstWebRTCICEClass::get_http_proxy:
   * @ice: a #GstWebRTCICE
   *
   * Get HTTP Proxy to be used when connecting to TURN server.
   *
   * Returns: (transfer full): URI of the HTTP proxy of the form
   *   http://[username:password@]hostname[:port]
   *
   * Since: 1.22
   */
  gchar * (*get_http_proxy)                            (GstWebRTCICE * ice);

  void (*set_tos)                                      (GstWebRTCICE * ice,
                                                        GstWebRTCICEStream * stream,
                                                        guint tos);
  void (*set_on_ice_candidate)                         (GstWebRTCICE * ice,
                                                        GstWebRTCICEOnCandidateFunc func,
                                                        gpointer user_data,
                                                        GDestroyNotify notify);
  GstWebRTCICECandidateStats** (*get_local_candidates)(GstWebRTCICE * ice,
                                                       GstWebRTCICEStream * stream);
  GstWebRTCICECandidateStats**(*get_remote_candidates)(GstWebRTCICE * ice,
                                                       GstWebRTCICEStream * stream);
  gboolean (*get_selected_pair)                       (GstWebRTCICE * ice,
                                                       GstWebRTCICEStream * stream,
                                                       GstWebRTCICECandidateStats ** local_stats,
                                                       GstWebRTCICECandidateStats ** remote_stats);

  /**
   * GstWebRTCICEClass::close:
   * @ice: a #GstWebRTCICE
   * @promise: (transfer full) (nullable): a #GstPromise to be notified when the task is
   * complete.
   *
   * Invoke the close procedure as specified in
   * https://www.w3.org/TR/webrtc/#dom-rtcpeerconnection-close.
   *
   * Since: 1.28
   */
  void (*close)                                       (GstWebRTCICE * ice,
                                                       GstPromise * promise);

  gpointer _gst_reserved[GST_PADDING - 1];
};

GST_WEBRTC_API
GstWebRTCICEStream *        gst_webrtc_ice_add_stream               (GstWebRTCICE * ice,
                                                                     guint session_id) G_GNUC_WARN_UNUSED_RESULT;

GST_WEBRTC_API
GstWebRTCICETransport *     gst_webrtc_ice_find_transport           (GstWebRTCICE * ice,
                                                                     GstWebRTCICEStream * stream,
                                                                     GstWebRTCICEComponent component) G_GNUC_WARN_UNUSED_RESULT;


GST_WEBRTC_API
gboolean                    gst_webrtc_ice_gather_candidates        (GstWebRTCICE * ice,
                                                                     GstWebRTCICEStream * stream);

/* FIXME: GstStructure-ize the candidate */
GST_WEBRTC_API
void                        gst_webrtc_ice_add_candidate            (GstWebRTCICE * ice,
                                                                     GstWebRTCICEStream * stream,
                                                                     const gchar * candidate,
                                                                     GstPromise * promise);

GST_WEBRTC_API
gboolean                    gst_webrtc_ice_set_local_credentials    (GstWebRTCICE * ice,
                                                                     GstWebRTCICEStream * stream,
                                                                     const gchar * ufrag,
                                                                     const gchar * pwd);

GST_WEBRTC_API
gboolean                    gst_webrtc_ice_set_remote_credentials   (GstWebRTCICE * ice,
                                                                     GstWebRTCICEStream * stream,
                                                                     const gchar * ufrag,
                                                                     const gchar * pwd);

GST_WEBRTC_API
gboolean                    gst_webrtc_ice_add_turn_server          (GstWebRTCICE * ice,
                                                                     const gchar * uri);


GST_WEBRTC_API
void                        gst_webrtc_ice_set_is_controller        (GstWebRTCICE * ice,
                                                                     gboolean controller);

GST_WEBRTC_API
gboolean                    gst_webrtc_ice_get_is_controller        (GstWebRTCICE * ice);

GST_WEBRTC_API
void                        gst_webrtc_ice_set_force_relay          (GstWebRTCICE * ice,
                                                                     gboolean force_relay);

GST_WEBRTC_API
void                        gst_webrtc_ice_set_stun_server          (GstWebRTCICE * ice,
                                                                     const gchar * uri);

GST_WEBRTC_API
gchar *                     gst_webrtc_ice_get_stun_server          (GstWebRTCICE * ice) G_GNUC_WARN_UNUSED_RESULT;

GST_WEBRTC_API
void                        gst_webrtc_ice_set_turn_server          (GstWebRTCICE * ice,
                                                                     const gchar * uri);

GST_WEBRTC_API
gchar *                     gst_webrtc_ice_get_turn_server          (GstWebRTCICE * ice) G_GNUC_WARN_UNUSED_RESULT;

GST_WEBRTC_API
void                        gst_webrtc_ice_set_http_proxy           (GstWebRTCICE * ice,
                                                                     const gchar * uri);

GST_WEBRTC_API
gchar *                     gst_webrtc_ice_get_http_proxy           (GstWebRTCICE * ice) G_GNUC_WARN_UNUSED_RESULT;

GST_WEBRTC_API
void                        gst_webrtc_ice_set_on_ice_candidate     (GstWebRTCICE * ice,
                                                                     GstWebRTCICEOnCandidateFunc func,
                                                                     gpointer user_data,
                                                                     GDestroyNotify notify);

GST_WEBRTC_API
void                        gst_webrtc_ice_set_tos                  (GstWebRTCICE * ice,
                                                                     GstWebRTCICEStream * stream,
                                                                     guint tos);

GST_WEBRTC_API
GstWebRTCICECandidateStats** gst_webrtc_ice_get_local_candidates    (GstWebRTCICE * ice,
                                                                     GstWebRTCICEStream * stream) G_GNUC_WARN_UNUSED_RESULT;

GST_WEBRTC_API
GstWebRTCICECandidateStats** gst_webrtc_ice_get_remote_candidates   (GstWebRTCICE * ice,
                                                                     GstWebRTCICEStream * stream) G_GNUC_WARN_UNUSED_RESULT;

#ifndef GST_DISABLE_DEPRECATED
GST_WEBRTC_DEPRECATED_FOR(gst_webrtc_ice_transport_get_selected_pair)
gboolean                    gst_webrtc_ice_get_selected_pair        (GstWebRTCICE * ice,
                                                                     GstWebRTCICEStream * stream,
                                                                     GstWebRTCICECandidateStats ** local_stats,
                                                                     GstWebRTCICECandidateStats ** remote_stats);
#endif

GST_WEBRTC_API
void                        gst_webrtc_ice_candidate_stats_free     (GstWebRTCICECandidateStats * stats);

GST_WEBRTC_API
GType                       gst_webrtc_ice_candidate_stats_get_type (void);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstWebRTCICE, gst_object_unref)

GST_WEBRTC_API
GstWebRTCICECandidateStats * gst_webrtc_ice_candidate_stats_copy   (GstWebRTCICECandidateStats *stats) G_GNUC_WARN_UNUSED_RESULT;

GST_WEBRTC_API
void                         gst_webrtc_ice_close                  (GstWebRTCICE * ice,
                                                                    GstPromise * promise);

GST_WEBRTC_API
void                        gst_webrtc_ice_candidate_free           (GstWebRTCICECandidate * candidate);

GST_WEBRTC_API
GType                       gst_webrtc_ice_candidate_get_type       (void);

GST_WEBRTC_API
GstWebRTCICECandidate *     gst_webrtc_ice_candidate_copy           (GstWebRTCICECandidate * candidate);

GST_WEBRTC_API
void                        gst_webrtc_ice_candidate_pair_free      (GstWebRTCICECandidatePair * pair);

GST_WEBRTC_API
GType                       gst_webrtc_ice_candidate_pair_get_type  (void);

GST_WEBRTC_API
GstWebRTCICECandidatePair * gst_webrtc_ice_candidate_pair_copy      (GstWebRTCICECandidatePair * pair);

G_END_DECLS

#endif /* __GST_WEBRTC_ICE_H__ */
