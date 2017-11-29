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

#ifndef __GST_WEBRTC_FWD_H__
#define __GST_WEBRTC_FWD_H__

#ifndef GST_USE_UNSTABLE_API
#warning "The WebRTC library from gst-plugins-bad is unstable API and may change in future."
#warning "You can define GST_USE_UNSTABLE_API to avoid this warning."
#endif

#include <gst/gst.h>

#ifndef GST_WEBRTC_API
#define GST_WEBRTC_API GST_EXPORT
#endif

#include <gst/webrtc/webrtc-enumtypes.h>

typedef struct _GstWebRTCDTLSTransport GstWebRTCDTLSTransport;
typedef struct _GstWebRTCDTLSTransportClass GstWebRTCDTLSTransportClass;

typedef struct _GstWebRTCICETransport GstWebRTCICETransport;
typedef struct _GstWebRTCICETransportClass GstWebRTCICETransportClass;

typedef struct _GstWebRTCRTPReceiver GstWebRTCRTPReceiver;
typedef struct _GstWebRTCRTPReceiverClass GstWebRTCRTPReceiverClass;

typedef struct _GstWebRTCRTPSender GstWebRTCRTPSender;
typedef struct _GstWebRTCRTPSenderClass GstWebRTCRTPSenderClass;

typedef struct _GstWebRTCSessionDescription GstWebRTCSessionDescription;

typedef struct _GstWebRTCRTPTransceiver GstWebRTCRTPTransceiver;
typedef struct _GstWebRTCRTPTransceiverClass GstWebRTCRTPTransceiverClass;

/**
 * GstWebRTCDTLSTransportState:
 * GST_WEBRTC_DTLS_TRANSPORT_STATE_NEW: new
 * GST_WEBRTC_DTLS_TRANSPORT_STATE_CLOSED: closed
 * GST_WEBRTC_DTLS_TRANSPORT_STATE_FAILED: failed
 * GST_WEBRTC_DTLS_TRANSPORT_STATE_CONNECTING: connecting
 * GST_WEBRTC_DTLS_TRANSPORT_STATE_CONNECTED: connected
 */
typedef enum /*< underscore_name=gst_webrtc_dtls_transport_state >*/
{
  GST_WEBRTC_DTLS_TRANSPORT_STATE_NEW,
  GST_WEBRTC_DTLS_TRANSPORT_STATE_CLOSED,
  GST_WEBRTC_DTLS_TRANSPORT_STATE_FAILED,
  GST_WEBRTC_DTLS_TRANSPORT_STATE_CONNECTING,
  GST_WEBRTC_DTLS_TRANSPORT_STATE_CONNECTED,
} GstWebRTCDTLSTransportState;

/**
 * GstWebRTCICEGatheringState:
 * GST_WEBRTC_ICE_GATHERING_STATE_NEW: new
 * GST_WEBRTC_ICE_GATHERING_STATE_GATHERING: gathering
 * GST_WEBRTC_ICE_GATHERING_STATE_COMPLETE: complete
 *
 * See <ulink url="http://w3c.github.io/webrtc-pc/#dom-rtcicegatheringstate">http://w3c.github.io/webrtc-pc/#dom-rtcicegatheringstate</ulink>
 */
typedef enum /*< underscore_name=gst_webrtc_ice_gathering_state >*/
{
  GST_WEBRTC_ICE_GATHERING_STATE_NEW,
  GST_WEBRTC_ICE_GATHERING_STATE_GATHERING,
  GST_WEBRTC_ICE_GATHERING_STATE_COMPLETE,
} GstWebRTCICEGatheringState; /*< underscore_name=gst_webrtc_ice_gathering_state >*/

/**
 * GstWebRTCICEConnectionState:
 * GST_WEBRTC_ICE_CONNECTION_STATE_NEW: new
 * GST_WEBRTC_ICE_CONNECTION_STATE_CHECKING: checking
 * GST_WEBRTC_ICE_CONNECTION_STATE_CONNECTED: connected
 * GST_WEBRTC_ICE_CONNECTION_STATE_COMPLETED: completed
 * GST_WEBRTC_ICE_CONNECTION_STATE_FAILED: failed
 * GST_WEBRTC_ICE_CONNECTION_STATE_DISCONNECTED: disconnected
 * GST_WEBRTC_ICE_CONNECTION_STATE_CLOSED: closed
 *
 * See <ulink url="http://w3c.github.io/webrtc-pc/#dom-rtciceconnectionstate">http://w3c.github.io/webrtc-pc/#dom-rtciceconnectionstate</ulink>
 */
typedef enum /*< underscore_name=gst_webrtc_ice_connection_state >*/
{
  GST_WEBRTC_ICE_CONNECTION_STATE_NEW,
  GST_WEBRTC_ICE_CONNECTION_STATE_CHECKING,
  GST_WEBRTC_ICE_CONNECTION_STATE_CONNECTED,
  GST_WEBRTC_ICE_CONNECTION_STATE_COMPLETED,
  GST_WEBRTC_ICE_CONNECTION_STATE_FAILED,
  GST_WEBRTC_ICE_CONNECTION_STATE_DISCONNECTED,
  GST_WEBRTC_ICE_CONNECTION_STATE_CLOSED,
} GstWebRTCICEConnectionState;

/**
 * GstWebRTCSignalingState:
 * GST_WEBRTC_SIGNALING_STATE_STABLE: stable
 * GST_WEBRTC_SIGNALING_STATE_CLOSED: closed
 * GST_WEBRTC_SIGNALING_STATE_HAVE_LOCAL_OFFER: have-local-offer
 * GST_WEBRTC_SIGNALING_STATE_HAVE_REMOTE_OFFER: have-remote-offer
 * GST_WEBRTC_SIGNALING_STATE_HAVE_LOCAL_PRANSWER: have-local-pranswer
 * GST_WEBRTC_SIGNALING_STATE_HAVE_REMOTE_PRANSWER: have-remote-pranswer
 *
 * See <ulink url="http://w3c.github.io/webrtc-pc/#dom-rtcsignalingstate">http://w3c.github.io/webrtc-pc/#dom-rtcsignalingstate</ulink>
 */
typedef enum /*< underscore_name=gst_webrtc_signaling_state >*/
{
  GST_WEBRTC_SIGNALING_STATE_STABLE,
  GST_WEBRTC_SIGNALING_STATE_CLOSED,
  GST_WEBRTC_SIGNALING_STATE_HAVE_LOCAL_OFFER,
  GST_WEBRTC_SIGNALING_STATE_HAVE_REMOTE_OFFER,
  GST_WEBRTC_SIGNALING_STATE_HAVE_LOCAL_PRANSWER,
  GST_WEBRTC_SIGNALING_STATE_HAVE_REMOTE_PRANSWER,
} GstWebRTCSignalingState;

/**
 * GstWebRTCPeerConnectionState:
 * GST_WEBRTC_PEER_CONNECTION_STATE_NEW: new
 * GST_WEBRTC_PEER_CONNECTION_STATE_CONNECTING: connecting
 * GST_WEBRTC_PEER_CONNECTION_STATE_CONNECTED: connected
 * GST_WEBRTC_PEER_CONNECTION_STATE_DISCONNECTED: disconnected
 * GST_WEBRTC_PEER_CONNECTION_STATE_FAILED: failed
 * GST_WEBRTC_PEER_CONNECTION_STATE_CLOSED: closed
 *
 * See <ulink url="http://w3c.github.io/webrtc-pc/#dom-rtcpeerconnectionstate">http://w3c.github.io/webrtc-pc/#dom-rtcpeerconnectionstate</ulink>
 */
typedef enum /*< underscore_name=gst_webrtc_peer_connection_state >*/
{
  GST_WEBRTC_PEER_CONNECTION_STATE_NEW,
  GST_WEBRTC_PEER_CONNECTION_STATE_CONNECTING,
  GST_WEBRTC_PEER_CONNECTION_STATE_CONNECTED,
  GST_WEBRTC_PEER_CONNECTION_STATE_DISCONNECTED,
  GST_WEBRTC_PEER_CONNECTION_STATE_FAILED,
  GST_WEBRTC_PEER_CONNECTION_STATE_CLOSED,
} GstWebRTCPeerConnectionState;

/**
 * GstWebRTCICERole:
 * GST_WEBRTC_ICE_ROLE_CONTROLLED: controlled
 * GST_WEBRTC_ICE_ROLE_CONTROLLING: controlling
 */
typedef enum /*< underscore_name=gst_webrtc_ice_role >*/
{
  GST_WEBRTC_ICE_ROLE_CONTROLLED,
  GST_WEBRTC_ICE_ROLE_CONTROLLING,
} GstWebRTCICERole;

/**
 * GstWebRTCICEComponent:
 * GST_WEBRTC_ICE_COMPONENT_RTP,
 * GST_WEBRTC_ICE_COMPONENT_RTCP,
 */
typedef enum /*< underscore_name=gst_webrtc_ice_component >*/
{
  GST_WEBRTC_ICE_COMPONENT_RTP,
  GST_WEBRTC_ICE_COMPONENT_RTCP,
} GstWebRTCICEComponent;

/**
 * GstWebRTCSDPType:
 * GST_WEBRTC_SDP_TYPE_OFFER: offer
 * GST_WEBRTC_SDP_TYPE_PRANSWER: pranswer
 * GST_WEBRTC_SDP_TYPE_ANSWER: answer
 * GST_WEBRTC_SDP_TYPE_ROLLBACK: rollback
 *
 * See <ulink url="http://w3c.github.io/webrtc-pc/#rtcsdptype">http://w3c.github.io/webrtc-pc/#rtcsdptype</ulink>
 */
typedef enum /*< underscore_name=gst_webrtc_sdp_type >*/
{
  GST_WEBRTC_SDP_TYPE_OFFER = 1,
  GST_WEBRTC_SDP_TYPE_PRANSWER,
  GST_WEBRTC_SDP_TYPE_ANSWER,
  GST_WEBRTC_SDP_TYPE_ROLLBACK,
} GstWebRTCSDPType;

/**
 * GstWebRTCRtpTransceiverDirection:
 * GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_NONE: none
 * GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_INACTIVE: inactive
 * GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY: sendonly
 * GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY: recvonly
 * GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDRECV: sendrecv
 */
typedef enum /*< underscore_name=gst_webrtc_rtp_transceiver_direction >*/
{
  GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_NONE,
  GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_INACTIVE,
  GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY,
  GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY,
  GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDRECV,
} GstWebRTCRTPTransceiverDirection;

/**
 * GstWebRTCDTLSSetup:
 * GST_WEBRTC_DTLS_SETUP_NONE: none
 * GST_WEBRTC_DTLS_SETUP_ACTPASS: actpass
 * GST_WEBRTC_DTLS_SETUP_ACTIVE: sendonly
 * GST_WEBRTC_DTLS_SETUP_PASSIVE: recvonly
 */
typedef enum /*< underscore_name=gst_webrtc_dtls_setup >*/
{
  GST_WEBRTC_DTLS_SETUP_NONE,
  GST_WEBRTC_DTLS_SETUP_ACTPASS,
  GST_WEBRTC_DTLS_SETUP_ACTIVE,
  GST_WEBRTC_DTLS_SETUP_PASSIVE,
} GstWebRTCDTLSSetup;

/**
 * GstWebRTCStatsType:
 * GST_WEBRTC_STATS_CODEC: codec
 * GST_WEBRTC_STATS_INBOUND_RTP: inbound-rtp
 * GST_WEBRTC_STATS_OUTBOUND_RTP: outbound-rtp
 * GST_WEBRTC_STATS_REMOTE_INBOUND_RTP: remote-inbound-rtp
 * GST_WEBRTC_STATS_REMOTE_OUTBOUND_RTP: remote-outbound-rtp
 * GST_WEBRTC_STATS_CSRC: csrc
 * GST_WEBRTC_STATS_PEER_CONNECTION: peer-connectiion
 * GST_WEBRTC_STATS_DATA_CHANNEL: data-channel
 * GST_WEBRTC_STATS_STREAM: stream
 * GST_WEBRTC_STATS_TRANSPORT: transport
 * GST_WEBRTC_STATS_CANDIDATE_PAIR: candidate-pair
 * GST_WEBRTC_STATS_LOCAL_CANDIDATE: local-candidate
 * GST_WEBRTC_STATS_REMOTE_CANDIDATE: remote-candidate
 * GST_WEBRTC_STATS_CERTIFICATE: certificate
 */
typedef enum /*< underscore_name=gst_webrtc_stats_type >*/
{
  GST_WEBRTC_STATS_CODEC = 1,
  GST_WEBRTC_STATS_INBOUND_RTP,
  GST_WEBRTC_STATS_OUTBOUND_RTP,
  GST_WEBRTC_STATS_REMOTE_INBOUND_RTP,
  GST_WEBRTC_STATS_REMOTE_OUTBOUND_RTP,
  GST_WEBRTC_STATS_CSRC,
  GST_WEBRTC_STATS_PEER_CONNECTION,
  GST_WEBRTC_STATS_DATA_CHANNEL,
  GST_WEBRTC_STATS_STREAM,
  GST_WEBRTC_STATS_TRANSPORT,
  GST_WEBRTC_STATS_CANDIDATE_PAIR,
  GST_WEBRTC_STATS_LOCAL_CANDIDATE,
  GST_WEBRTC_STATS_REMOTE_CANDIDATE,
  GST_WEBRTC_STATS_CERTIFICATE,
} GstWebRTCStatsType;

/**
 * GstWebRTCFECType:
 * GST_WEBRTC_FEC_TYPE_NONE: none
 * GST_WEBRTC_FEC_TYPE_ULP_RED: ulpfec + red
 */
typedef enum /*< underscore_name=gst_webrtc_fec_type >*/
{
  GST_WEBRTC_FEC_TYPE_NONE,
  GST_WEBRTC_FEC_TYPE_ULP_RED,
} GstWebRTCFECType;

#endif /* __GST_WEBRTC_FWD_H__ */
