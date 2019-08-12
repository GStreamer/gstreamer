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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

/* for GValueArray... */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include "gstwebrtcstats.h"
#include "gstwebrtcbin.h"
#include "transportstream.h"
#include "transportreceivebin.h"
#include "utils.h"
#include "webrtctransceiver.h"

#define GST_CAT_DEFAULT gst_webrtc_stats_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static void
_init_debug (void)
{
  static gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_INIT (gst_webrtc_stats_debug, "webrtcstats", 0,
        "webrtcstats");
    g_once_init_leave (&_init, 1);
  }
}

static double
monotonic_time_as_double_milliseconds (void)
{
  return g_get_monotonic_time () / 1000.0;
}

static void
_set_base_stats (GstStructure * s, GstWebRTCStatsType type, double ts,
    const char *id)
{
  gchar *name = _enum_value_to_string (GST_TYPE_WEBRTC_STATS_TYPE,
      type);

  g_return_if_fail (name != NULL);

  gst_structure_set_name (s, name);
  gst_structure_set (s, "type", GST_TYPE_WEBRTC_STATS_TYPE, type, "timestamp",
      G_TYPE_DOUBLE, ts, "id", G_TYPE_STRING, id, NULL);

  g_free (name);
}

static GstStructure *
_get_peer_connection_stats (GstWebRTCBin * webrtc)
{
  GstStructure *s = gst_structure_new_empty ("unused");

  /* FIXME: datachannel */
  gst_structure_set (s, "data-channels-opened", G_TYPE_UINT, 0,
      "data-channels-closed", G_TYPE_UINT, 0, "data-channels-requested",
      G_TYPE_UINT, 0, "data-channels-accepted", G_TYPE_UINT, 0, NULL);

  return s;
}

#define CLOCK_RATE_VALUE_TO_SECONDS(v,r) ((double) v / (double) clock_rate)
#define FIXED_16_16_TO_DOUBLE(v) ((double) ((v & 0xffff0000) >> 16) + ((v & 0xffff) / 65536.0))
#define FIXED_32_32_TO_DOUBLE(v) ((double) ((v & G_GUINT64_CONSTANT (0xffffffff00000000)) >> 32) + ((v & G_GUINT64_CONSTANT (0xffffffff)) / 4294967296.0))

/* https://www.w3.org/TR/webrtc-stats/#inboundrtpstats-dict*
   https://www.w3.org/TR/webrtc-stats/#outboundrtpstats-dict* */
static void
_get_stats_from_rtp_source_stats (GstWebRTCBin * webrtc,
    const GstStructure * source_stats, const gchar * codec_id,
    const gchar * transport_id, GstStructure * s)
{
  guint ssrc, fir, pli, nack, jitter;
  int lost, clock_rate;
  guint64 packets, bytes;
  gboolean internal;
  double ts;

  gst_structure_get_double (s, "timestamp", &ts);
  gst_structure_get (source_stats, "ssrc", G_TYPE_UINT, &ssrc, "clock-rate",
      G_TYPE_INT, &clock_rate, "internal", G_TYPE_BOOLEAN, &internal, NULL);

  if (internal) {
    GstStructure *r_in, *out;
    gchar *out_id, *r_in_id;

    out_id = g_strdup_printf ("rtp-outbound-stream-stats_%u", ssrc);
    r_in_id = g_strdup_printf ("rtp-remote-inbound-stream-stats_%u", ssrc);

    r_in = gst_structure_new_empty (r_in_id);
    _set_base_stats (r_in, GST_WEBRTC_STATS_REMOTE_INBOUND_RTP, ts, r_in_id);

    /* RTCStreamStats */
    gst_structure_set (r_in, "local-id", G_TYPE_STRING, out_id, NULL);
    gst_structure_set (r_in, "ssrc", G_TYPE_UINT, ssrc, NULL);
    gst_structure_set (r_in, "codec-id", G_TYPE_STRING, codec_id, NULL);
    gst_structure_set (r_in, "transport-id", G_TYPE_STRING, transport_id, NULL);
    /* XXX: mediaType, trackId, sliCount, qpSum */

    if (gst_structure_get_uint64 (source_stats, "packets-received", &packets))
      gst_structure_set (r_in, "packets-received", G_TYPE_UINT64, packets,
          NULL);
    if (gst_structure_get_int (source_stats, "packets-lost", &lost))
      gst_structure_set (r_in, "packets-lost", G_TYPE_INT, lost, NULL);
    if (gst_structure_get_uint (source_stats, "jitter", &jitter))
      gst_structure_set (r_in, "jitter", G_TYPE_DOUBLE,
          CLOCK_RATE_VALUE_TO_SECONDS (jitter, clock_rate), NULL);

/* XXX: RTCReceivedRTPStreamStats
    double             fractionLost;
    unsigned long      packetsDiscarded;
    unsigned long      packetsFailedDecryption;
    unsigned long      packetsRepaired;
    unsigned long      burstPacketsLost;
    unsigned long      burstPacketsDiscarded;
    unsigned long      burstLossCount;
    unsigned long      burstDiscardCount;
    double             burstLossRate;
    double             burstDiscardRate;
    double             gapLossRate;
    double             gapDiscardRate;
*/

    /* RTCRemoteInboundRTPStreamStats */
    /* XXX: framesDecoded, lastPacketReceivedTimestamp */

    out = gst_structure_new_empty (out_id);
    _set_base_stats (out, GST_WEBRTC_STATS_OUTBOUND_RTP, ts, out_id);

    /* RTCStreamStats */
    gst_structure_set (out, "ssrc", G_TYPE_UINT, ssrc, NULL);
    gst_structure_set (out, "codec-id", G_TYPE_STRING, codec_id, NULL);
    gst_structure_set (out, "transport-id", G_TYPE_STRING, transport_id, NULL);
    if (gst_structure_get_uint (source_stats, "sent-fir-count", &fir))
      gst_structure_set (out, "fir-count", G_TYPE_UINT, fir, NULL);
    if (gst_structure_get_uint (source_stats, "sent-pli-count", &pli))
      gst_structure_set (out, "pli-count", G_TYPE_UINT, pli, NULL);
    if (gst_structure_get_uint (source_stats, "sent-nack-count", &nack))
      gst_structure_set (out, "nack-count", G_TYPE_UINT, nack, NULL);
    /* XXX: mediaType, trackId, sliCount, qpSum */

/* RTCSentRTPStreamStats */
    if (gst_structure_get_uint64 (source_stats, "octets-sent", &bytes))
      gst_structure_set (out, "bytes-sent", G_TYPE_UINT64, bytes, NULL);
    if (gst_structure_get_uint64 (source_stats, "packets-sent", &packets))
      gst_structure_set (out, "packets-sent", G_TYPE_UINT64, packets, NULL);
/* XXX:
    unsigned long      packetsDiscardedOnSend;
    unsigned long long bytesDiscardedOnSend;
*/

    /* RTCOutboundRTPStreamStats */
    gst_structure_set (out, "remote-id", G_TYPE_STRING, r_in_id, NULL);
/* XXX:
    DOMHighResTimeStamp lastPacketSentTimestamp;
    double              targetBitrate;
    unsigned long       framesEncoded;
    double              totalEncodeTime;
    double              averageRTCPInterval;
*/
    gst_structure_set (s, out_id, GST_TYPE_STRUCTURE, out, NULL);
    gst_structure_set (s, r_in_id, GST_TYPE_STRUCTURE, r_in, NULL);

    gst_structure_free (out);
    gst_structure_free (r_in);

    g_free (out_id);
    g_free (r_in_id);
  } else {
    GstStructure *in, *r_out;
    gchar *r_out_id, *in_id;
    gboolean have_rb = FALSE, have_sr = FALSE;

    gst_structure_get (source_stats, "have-rb", G_TYPE_BOOLEAN, &have_rb,
        "have-sr", G_TYPE_BOOLEAN, &have_sr, NULL);

    in_id = g_strdup_printf ("rtp-inbound-stream-stats_%u", ssrc);
    r_out_id = g_strdup_printf ("rtp-remote-outbound-stream-stats_%u", ssrc);

    in = gst_structure_new_empty (in_id);
    _set_base_stats (in, GST_WEBRTC_STATS_INBOUND_RTP, ts, in_id);

    /* RTCStreamStats */
    gst_structure_set (in, "ssrc", G_TYPE_UINT, ssrc, NULL);
    gst_structure_set (in, "codec-id", G_TYPE_STRING, codec_id, NULL);
    gst_structure_set (in, "transport-id", G_TYPE_STRING, transport_id, NULL);
    if (gst_structure_get_uint (source_stats, "recv-fir-count", &fir))
      gst_structure_set (in, "fir-count", G_TYPE_UINT, fir, NULL);
    if (gst_structure_get_uint (source_stats, "recv-pli-count", &pli))
      gst_structure_set (in, "pli-count", G_TYPE_UINT, pli, NULL);
    if (gst_structure_get_uint (source_stats, "recv-nack-count", &nack))
      gst_structure_set (in, "nack-count", G_TYPE_UINT, nack, NULL);
    /* XXX: mediaType, trackId, sliCount, qpSum */

    /* RTCReceivedRTPStreamStats */
    if (gst_structure_get_uint64 (source_stats, "packets-received", &packets))
      gst_structure_set (in, "packets-received", G_TYPE_UINT64, packets, NULL);
    if (gst_structure_get_uint64 (source_stats, "octets-received", &bytes))
      gst_structure_set (in, "bytes-received", G_TYPE_UINT64, bytes, NULL);
    if (gst_structure_get_int (source_stats, "packets-lost", &lost))
      gst_structure_set (in, "packets-lost", G_TYPE_INT, lost, NULL);
    if (gst_structure_get_uint (source_stats, "jitter", &jitter))
      gst_structure_set (in, "jitter", G_TYPE_DOUBLE,
          CLOCK_RATE_VALUE_TO_SECONDS (jitter, clock_rate), NULL);
/*
    RTCReceivedRTPStreamStats
    double             fractionLost;
    unsigned long      packetsDiscarded;
    unsigned long      packetsFailedDecryption;
    unsigned long      packetsRepaired;
    unsigned long      burstPacketsLost;
    unsigned long      burstPacketsDiscarded;
    unsigned long      burstLossCount;
    unsigned long      burstDiscardCount;
    double             burstLossRate;
    double             burstDiscardRate;
    double             gapLossRate;
    double             gapDiscardRate;
*/

    /* RTCInboundRTPStreamStats */
    gst_structure_set (in, "remote-id", G_TYPE_STRING, r_out_id, NULL);
    /* XXX: framesDecoded, lastPacketReceivedTimestamp */

    r_out = gst_structure_new_empty (r_out_id);
    _set_base_stats (r_out, GST_WEBRTC_STATS_REMOTE_OUTBOUND_RTP, ts, r_out_id);
    /* RTCStreamStats */
    gst_structure_set (r_out, "ssrc", G_TYPE_UINT, ssrc, NULL);
    gst_structure_set (r_out, "codec-id", G_TYPE_STRING, codec_id, NULL);
    gst_structure_set (r_out, "transport-id", G_TYPE_STRING, transport_id,
        NULL);
    if (have_rb) {
      guint32 rtt;
      if (gst_structure_get_uint (source_stats, "rb-round-trip", &rtt)) {
        /* 16.16 fixed point to double */
        double val = FIXED_16_16_TO_DOUBLE (rtt);
        gst_structure_set (r_out, "round-trip-time", G_TYPE_DOUBLE, val, NULL);
      }
    } else {
      /* default values */
      gst_structure_set (r_out, "round-trip-time", G_TYPE_DOUBLE, 0.0, NULL);
    }
    /* XXX: mediaType, trackId, sliCount, qpSum */

/* RTCSentRTPStreamStats */
    if (have_sr) {
      if (gst_structure_get_uint64 (source_stats, "sr-octet-count", &bytes))
        gst_structure_set (r_out, "bytes-sent", G_TYPE_UINT64, bytes, NULL);
      if (gst_structure_get_uint64 (source_stats, "sr-packet-count", &packets))
        gst_structure_set (r_out, "packets-sent", G_TYPE_UINT64, packets, NULL);
    }
/* XXX:
    unsigned long      packetsDiscardedOnSend;
    unsigned long long bytesDiscardedOnSend;
*/

    if (have_sr) {
      guint64 ntptime;
      if (gst_structure_get_uint64 (source_stats, "sr-ntptime", &ntptime)) {
        /* 16.16 fixed point to double */
        double val = FIXED_32_32_TO_DOUBLE (ntptime);
        gst_structure_set (r_out, "remote-timestamp", G_TYPE_DOUBLE, val, NULL);
      }
    } else {
      /* default values */
      gst_structure_set (r_out, "remote-timestamp", G_TYPE_DOUBLE, 0.0, NULL);
    }

    gst_structure_set (r_out, "local-id", G_TYPE_STRING, in_id, NULL);

    gst_structure_set (s, in_id, GST_TYPE_STRUCTURE, in, NULL);
    gst_structure_set (s, r_out_id, GST_TYPE_STRUCTURE, r_out, NULL);

    gst_structure_free (in);
    gst_structure_free (r_out);

    g_free (in_id);
    g_free (r_out_id);
  }
}

/* https://www.w3.org/TR/webrtc-stats/#candidatepair-dict* */
static gchar *
_get_stats_from_ice_transport (GstWebRTCBin * webrtc,
    GstWebRTCICETransport * transport, GstStructure * s)
{
  GstStructure *stats;
  gchar *id;
  double ts;

  gst_structure_get_double (s, "timestamp", &ts);

  id = g_strdup_printf ("ice-candidate-pair_%s", GST_OBJECT_NAME (transport));
  stats = gst_structure_new_empty (id);
  _set_base_stats (stats, GST_WEBRTC_STATS_TRANSPORT, ts, id);

/* XXX: RTCIceCandidatePairStats
    DOMString                     transportId;
    DOMString                     localCandidateId;
    DOMString                     remoteCandidateId;
    RTCStatsIceCandidatePairState state;
    unsigned long long            priority;
    boolean                       nominated;
    unsigned long                 packetsSent;
    unsigned long                 packetsReceived;
    unsigned long long            bytesSent;
    unsigned long long            bytesReceived;
    DOMHighResTimeStamp           lastPacketSentTimestamp;
    DOMHighResTimeStamp           lastPacketReceivedTimestamp;
    DOMHighResTimeStamp           firstRequestTimestamp;
    DOMHighResTimeStamp           lastRequestTimestamp;
    DOMHighResTimeStamp           lastResponseTimestamp;
    double                        totalRoundTripTime;
    double                        currentRoundTripTime;
    double                        availableOutgoingBitrate;
    double                        availableIncomingBitrate;
    unsigned long                 circuitBreakerTriggerCount;
    unsigned long long            requestsReceived;
    unsigned long long            requestsSent;
    unsigned long long            responsesReceived;
    unsigned long long            responsesSent;
    unsigned long long            retransmissionsReceived;
    unsigned long long            retransmissionsSent;
    unsigned long long            consentRequestsSent;
    DOMHighResTimeStamp           consentExpiredTimestamp;
*/

/* XXX: RTCIceCandidateStats
    DOMString           transportId;
    boolean             isRemote;
    RTCNetworkType      networkType;
    DOMString           ip;
    long                port;
    DOMString           protocol;
    RTCIceCandidateType candidateType;
    long                priority;
    DOMString           url;
    DOMString           relayProtocol;
    boolean             deleted = false;
};
*/

  gst_structure_set (s, id, GST_TYPE_STRUCTURE, stats, NULL);
  gst_structure_free (stats);

  return id;
}

/* https://www.w3.org/TR/webrtc-stats/#dom-rtctransportstats */
static gchar *
_get_stats_from_dtls_transport (GstWebRTCBin * webrtc,
    GstWebRTCDTLSTransport * transport, GstStructure * s)
{
  GstStructure *stats;
  gchar *id;
  double ts;
  gchar *ice_id;

  gst_structure_get_double (s, "timestamp", &ts);

  id = g_strdup_printf ("transport-stats_%s", GST_OBJECT_NAME (transport));
  stats = gst_structure_new_empty (id);
  _set_base_stats (stats, GST_WEBRTC_STATS_TRANSPORT, ts, id);

/* XXX: RTCTransportStats
    unsigned long         packetsSent;
    unsigned long         packetsReceived;
    unsigned long long    bytesSent;
    unsigned long long    bytesReceived;
    DOMString             rtcpTransportStatsId;
    RTCIceRole            iceRole;
    RTCDtlsTransportState dtlsState;
    DOMString             selectedCandidatePairId;
    DOMString             localCertificateId;
    DOMString             remoteCertificateId;
*/

/* XXX: RTCCertificateStats
    DOMString fingerprint;
    DOMString fingerprintAlgorithm;
    DOMString base64Certificate;
    DOMString issuerCertificateId;
*/

/* XXX: RTCIceCandidateStats
    DOMString           transportId;
    boolean             isRemote;
    DOMString           ip;
    long                port;
    DOMString           protocol;
    RTCIceCandidateType candidateType;
    long                priority;
    DOMString           url;
    boolean             deleted = false;
*/

  gst_structure_set (s, id, GST_TYPE_STRUCTURE, stats, NULL);
  gst_structure_free (stats);

  ice_id = _get_stats_from_ice_transport (webrtc, transport->transport, s);
  g_free (ice_id);

  return id;
}

static void
_get_stats_from_transport_channel (GstWebRTCBin * webrtc,
    TransportStream * stream, const gchar * codec_id, guint ssrc,
    GstStructure * s)
{
  GstWebRTCDTLSTransport *transport;
  GObject *rtp_session;
  GstStructure *rtp_stats;
  GValueArray *source_stats;
  gchar *transport_id;
  double ts;
  int i;

  gst_structure_get_double (s, "timestamp", &ts);

  transport = stream->transport;
  if (!transport)
    transport = stream->transport;
  if (!transport)
    return;

  g_signal_emit_by_name (webrtc->rtpbin, "get-internal-session",
      stream->session_id, &rtp_session);
  g_object_get (rtp_session, "stats", &rtp_stats, NULL);

  gst_structure_get (rtp_stats, "source-stats", G_TYPE_VALUE_ARRAY,
      &source_stats, NULL);

  GST_DEBUG_OBJECT (webrtc, "retrieving rtp stream stats from transport %"
      GST_PTR_FORMAT " rtp session %" GST_PTR_FORMAT " with %u rtp sources, "
      "transport %" GST_PTR_FORMAT, stream, rtp_session, source_stats->n_values,
      transport);

  transport_id = _get_stats_from_dtls_transport (webrtc, transport, s);

  /* construct stats objects */
  for (i = 0; i < source_stats->n_values; i++) {
    const GstStructure *stats;
    const GValue *val = g_value_array_get_nth (source_stats, i);
    guint stats_ssrc = 0;

    stats = gst_value_get_structure (val);

    /* skip foreign sources */
    gst_structure_get (stats, "ssrc", G_TYPE_UINT, &stats_ssrc, NULL);
    if (ssrc && stats_ssrc && ssrc != stats_ssrc)
      continue;

    _get_stats_from_rtp_source_stats (webrtc, stats, codec_id, transport_id, s);
  }

  g_object_unref (rtp_session);
  gst_structure_free (rtp_stats);
  g_value_array_free (source_stats);
  g_free (transport_id);
}

/* https://www.w3.org/TR/webrtc-stats/#codec-dict* */
static void
_get_codec_stats_from_pad (GstWebRTCBin * webrtc, GstPad * pad,
    GstStructure * s, gchar ** out_id, guint * out_ssrc)
{
  GstStructure *stats;
  GstCaps *caps;
  gchar *id;
  double ts;
  guint ssrc = 0;

  gst_structure_get_double (s, "timestamp", &ts);

  stats = gst_structure_new_empty ("unused");
  id = g_strdup_printf ("codec-stats-%s", GST_OBJECT_NAME (pad));
  _set_base_stats (stats, GST_WEBRTC_STATS_CODEC, ts, id);

  caps = gst_pad_get_current_caps (pad);
  if (caps && gst_caps_is_fixed (caps)) {
    GstStructure *caps_s = gst_caps_get_structure (caps, 0);
    gint pt, clock_rate;

    if (gst_structure_get_int (caps_s, "payload", &pt))
      gst_structure_set (stats, "payload-type", G_TYPE_UINT, pt, NULL);

    if (gst_structure_get_int (caps_s, "clock-rate", &clock_rate))
      gst_structure_set (stats, "clock-rate", G_TYPE_UINT, clock_rate, NULL);

    if (gst_structure_get_uint (caps_s, "ssrc", &ssrc))
      gst_structure_set (stats, "ssrc", G_TYPE_UINT, ssrc, NULL);

    /* FIXME: codecType, mimeType, channels, sdpFmtpLine, implementation, transportId */
  }

  if (caps)
    gst_caps_unref (caps);

  gst_structure_set (s, id, GST_TYPE_STRUCTURE, stats, NULL);
  gst_structure_free (stats);

  if (out_id)
    *out_id = id;
  else
    g_free (id);

  if (out_ssrc)
    *out_ssrc = ssrc;
}

static gboolean
_get_stats_from_pad (GstWebRTCBin * webrtc, GstPad * pad, GstStructure * s)
{
  GstWebRTCBinPad *wpad = GST_WEBRTC_BIN_PAD (pad);
  TransportStream *stream;
  gchar *codec_id;
  guint ssrc;

  _get_codec_stats_from_pad (webrtc, pad, s, &codec_id, &ssrc);

  if (!wpad->trans)
    goto out;

  stream = WEBRTC_TRANSCEIVER (wpad->trans)->stream;
  if (!stream)
    goto out;

  _get_stats_from_transport_channel (webrtc, stream, codec_id, ssrc, s);

out:
  g_free (codec_id);
  return TRUE;
}

void
gst_webrtc_bin_update_stats (GstWebRTCBin * webrtc)
{
  GstStructure *s = gst_structure_new_empty ("application/x-webrtc-stats");
  double ts = monotonic_time_as_double_milliseconds ();
  GstStructure *pc_stats;

  _init_debug ();

  gst_structure_set (s, "timestamp", G_TYPE_DOUBLE, ts, NULL);

  /* FIXME: better unique IDs */
  /* FIXME: rate limitting stat updates? */
  /* FIXME: all stats need to be kept forever */

  GST_DEBUG_OBJECT (webrtc, "updating stats at time %f", ts);

  if ((pc_stats = _get_peer_connection_stats (webrtc))) {
    const gchar *id = "peer-connection-stats";
    _set_base_stats (pc_stats, GST_WEBRTC_STATS_PEER_CONNECTION, ts, id);
    gst_structure_set (s, id, GST_TYPE_STRUCTURE, pc_stats, NULL);
    gst_structure_free (pc_stats);
  }

  gst_element_foreach_pad (GST_ELEMENT (webrtc),
      (GstElementForeachPadFunc) _get_stats_from_pad, s);

  gst_structure_remove_field (s, "timestamp");

  if (webrtc->priv->stats)
    gst_structure_free (webrtc->priv->stats);
  webrtc->priv->stats = s;
}
