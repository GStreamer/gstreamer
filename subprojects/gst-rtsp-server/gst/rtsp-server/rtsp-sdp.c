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

#define GLIB_DISABLE_DEPRECATION_WARNINGS

/**
 * SECTION:rtsp-sdp
 * @short_description: Make SDP messages
 * @see_also: #GstRTSPMedia
 *
 * Last reviewed on 2013-07-11 (1.0.0)
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <gst/net/net.h>
#include <gst/sdp/gstmikey.h>

#include "rtsp-sdp.h"

static gboolean
get_info_from_tags (GstPad * pad, GstEvent ** event, gpointer user_data)
{
  GstSDPMedia *media = (GstSDPMedia *) user_data;

  if (GST_EVENT_TYPE (*event) == GST_EVENT_TAG) {
    GstTagList *tags;
    guint bitrate = 0;

    gst_event_parse_tag (*event, &tags);

    if (gst_tag_list_get_scope (tags) != GST_TAG_SCOPE_STREAM)
      return TRUE;

    if (!gst_tag_list_get_uint (tags, GST_TAG_MAXIMUM_BITRATE,
            &bitrate) || bitrate == 0)
      if (!gst_tag_list_get_uint (tags, GST_TAG_BITRATE, &bitrate) ||
          bitrate == 0)
        return TRUE;

    /* set bandwidth (kbits/s) */
    gst_sdp_media_add_bandwidth (media, GST_SDP_BWTYPE_AS, bitrate / 1000);

    return FALSE;

  }

  return TRUE;
}

static void
update_sdp_from_tags (GstRTSPStream * stream, GstSDPMedia * stream_media)
{
  GstPad *src_pad;

  src_pad = gst_rtsp_stream_get_srcpad (stream);
  if (!src_pad)
    return;

  gst_pad_sticky_events_foreach (src_pad, get_info_from_tags, stream_media);

  gst_object_unref (src_pad);
}

static guint
get_roc_from_stats (GstStructure * stats, guint ssrc)
{
  const GValue *va, *v;
  guint i, len;
  /* initialize roc to something different than 0, so if we don't get
     the proper ROC from the encoder, streaming should fail initially. */
  guint roc = -1;

  va = gst_structure_get_value (stats, "streams");
  if (!va || !G_VALUE_HOLDS (va, GST_TYPE_ARRAY)) {
    GST_WARNING ("stats doesn't have a valid 'streams' field");
    return 0;
  }

  len = gst_value_array_get_size (va);

  /* look if there's any SSRC that matches. */
  for (i = 0; i < len; i++) {
    GstStructure *stream;
    v = gst_value_array_get_value (va, i);
    if (v && (stream = g_value_get_boxed (v))) {
      guint stream_ssrc;
      gst_structure_get_uint (stream, "ssrc", &stream_ssrc);
      if (stream_ssrc == ssrc) {
        gst_structure_get_uint (stream, "roc", &roc);
        break;
      }
    }
  }

  return roc;
}

static gboolean
mikey_add_crypto_sessions (GstRTSPStream * stream, GstMIKEYMessage * msg)
{
  guint i;
  GObject *session;
  GstElement *encoder;
  GValueArray *sources;
  gboolean roc_found;

  encoder = gst_rtsp_stream_get_srtp_encoder (stream);
  if (encoder == NULL) {
    GST_ERROR ("unable to get SRTP encoder from stream %p", stream);
    return FALSE;
  }

  session = gst_rtsp_stream_get_rtpsession (stream);
  if (session == NULL) {
    GST_ERROR ("unable to get RTP session from stream %p", stream);
    gst_object_unref (encoder);
    return FALSE;
  }

  roc_found = FALSE;
  g_object_get (session, "sources", &sources, NULL);
  for (i = 0; sources && (i < sources->n_values); i++) {
    GValue *val;
    GObject *source;
    guint32 ssrc;
    gboolean is_sender;

    val = g_value_array_get_nth (sources, i);
    source = (GObject *) g_value_get_object (val);

    g_object_get (source, "ssrc", &ssrc, "is-sender", &is_sender, NULL);

    if (is_sender) {
      guint32 roc = -1;
      GstStructure *stats;

      g_object_get (encoder, "stats", &stats, NULL);

      if (stats) {
        roc = get_roc_from_stats (stats, ssrc);
        gst_structure_free (stats);
      }

      roc_found = !!(roc != -1);
      if (!roc_found) {
        GST_ERROR ("unable to obtain ROC for stream %p with SSRC %u",
            stream, ssrc);
        goto cleanup;
      }

      GST_INFO ("stream %p with SSRC %u has a ROC of %u", stream, ssrc, roc);

      gst_mikey_message_add_cs_srtp (msg, 0, ssrc, roc);
    }
  }

cleanup:
  {
    g_value_array_free (sources);

    gst_object_unref (encoder);
    g_object_unref (session);
    return roc_found;
  }
}

/**
 * gst_rtsp_sdp_make_media:
 * @sdp: a #GstRTSPMessage
 * @info: a #GstSDPInfo
 * @stream: a #GstRTSPStream
 * @caps: a #GstCaps
 * @profile: a #GstRTSPProfile
 *
 * Creates a #GstSDPMedia from the parameters and stores it in @sdp.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.14
 */
gboolean
gst_rtsp_sdp_make_media (GstSDPMessage * sdp, GstSDPInfo * info,
    GstRTSPStream * stream, GstCaps * caps, GstRTSPProfile profile)
{
  GstSDPMedia *smedia;
  gchar *tmp;
  GstRTSPLowerTrans ltrans;
  GSocketFamily family;
  const gchar *addrtype, *proto;
  gchar *address;
  guint ttl;
  GstClockTime rtx_time;
  gchar *base64;
  GstMIKEYMessage *mikey_msg;

  gst_sdp_media_new (&smedia);

  if (gst_sdp_media_set_media_from_caps (caps, smedia) != GST_SDP_OK) {
    goto caps_error;
  }

  gst_sdp_media_set_port_info (smedia, 0, 1);

  switch (profile) {
    case GST_RTSP_PROFILE_AVP:
      proto = "RTP/AVP";
      break;
    case GST_RTSP_PROFILE_AVPF:
      proto = "RTP/AVPF";
      break;
    case GST_RTSP_PROFILE_SAVP:
      proto = "RTP/SAVP";
      break;
    case GST_RTSP_PROFILE_SAVPF:
      proto = "RTP/SAVPF";
      break;
    default:
      proto = "udp";
      break;
  }
  gst_sdp_media_set_proto (smedia, proto);

  if (info->is_ipv6) {
    addrtype = "IP6";
    family = G_SOCKET_FAMILY_IPV6;
  } else {
    addrtype = "IP4";
    family = G_SOCKET_FAMILY_IPV4;
  }

  ltrans = gst_rtsp_stream_get_protocols (stream);
  if (ltrans == GST_RTSP_LOWER_TRANS_UDP_MCAST) {
    GstRTSPAddress *addr;

    addr = gst_rtsp_stream_get_multicast_address (stream, family);
    if (addr == NULL)
      goto no_multicast;

    address = g_strdup (addr->address);
    ttl = addr->ttl;
    gst_rtsp_address_free (addr);
  } else {
    ttl = 16;
    if (info->is_ipv6)
      address = g_strdup ("::");
    else
      address = g_strdup ("0.0.0.0");
  }

  /* for the c= line */
  gst_sdp_media_add_connection (smedia, "IN", addrtype, address, ttl, 1);
  g_free (address);

  /* the config uri */
  tmp = gst_rtsp_stream_get_control (stream);
  gst_sdp_media_add_attribute (smedia, "control", tmp);
  g_free (tmp);

  /* check for srtp */
  mikey_msg = gst_mikey_message_new_from_caps (caps);
  if (mikey_msg) {
    /* add policy '0' for all sending SSRC */
    if (!mikey_add_crypto_sessions (stream, mikey_msg)) {
      gst_mikey_message_unref (mikey_msg);
      goto crypto_sessions_error;
    }

    base64 = gst_mikey_message_base64_encode (mikey_msg);
    if (base64) {
      tmp = g_strdup_printf ("mikey %s", base64);
      g_free (base64);
      gst_sdp_media_add_attribute (smedia, "key-mgmt", tmp);
      g_free (tmp);
    }

    gst_mikey_message_unref (mikey_msg);
  }

  /* RFC 7273 clock signalling */
  if (gst_rtsp_stream_is_sender (stream)) {
    GstBin *joined_bin = gst_rtsp_stream_get_joined_bin (stream);
    GstClock *clock = gst_element_get_clock (GST_ELEMENT_CAST (joined_bin));
    gchar *ts_refclk = NULL;
    gchar *mediaclk = NULL;
    guint rtptime, clock_rate;
    GstClockTime running_time, base_time, clock_time;
    GstRTSPPublishClockMode publish_clock_mode =
        gst_rtsp_stream_get_publish_clock_mode (stream);

    if (!gst_rtsp_stream_get_rtpinfo (stream, &rtptime, NULL, &clock_rate,
            &running_time))
      goto clock_signalling_cleanup;
    base_time = gst_element_get_base_time (GST_ELEMENT_CAST (joined_bin));
    g_assert (base_time != GST_CLOCK_TIME_NONE);
    clock_time = running_time + base_time;

    if (publish_clock_mode != GST_RTSP_PUBLISH_CLOCK_MODE_NONE && clock) {
      if (GST_IS_NTP_CLOCK (clock) || GST_IS_PTP_CLOCK (clock)) {
        if (publish_clock_mode == GST_RTSP_PUBLISH_CLOCK_MODE_CLOCK_AND_OFFSET) {
          guint32 mediaclk_offset;

          /* Calculate RTP time at the clock's epoch. That's the direct offset */
          clock_time =
              gst_util_uint64_scale (clock_time, clock_rate, GST_SECOND);

          clock_time &= 0xffffffff;
          mediaclk_offset =
              G_GUINT64_CONSTANT (0xffffffff) + rtptime - clock_time;
          mediaclk = g_strdup_printf ("direct=%u", (guint32) mediaclk_offset);
        }

        if (GST_IS_NTP_CLOCK (clock)) {
          gchar *ntp_address;
          guint ntp_port;

          g_object_get (clock, "address", &ntp_address, "port", &ntp_port,
              NULL);

          if (ntp_port == 123)
            ts_refclk = g_strdup_printf ("ntp=%s", ntp_address);
          else
            ts_refclk = g_strdup_printf ("ntp=%s:%u", ntp_address, ntp_port);

          g_free (ntp_address);
        } else {
          guint64 ptp_clock_id;
          guint ptp_domain;

          g_object_get (clock, "grandmaster-clock-id", &ptp_clock_id, "domain",
              &ptp_domain, NULL);

          if (ptp_domain != 0)
            ts_refclk =
                g_strdup_printf
                ("ptp=IEEE1588-2008:%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X:%u",
                (guint) (ptp_clock_id >> 56) & 0xff,
                (guint) (ptp_clock_id >> 48) & 0xff,
                (guint) (ptp_clock_id >> 40) & 0xff,
                (guint) (ptp_clock_id >> 32) & 0xff,
                (guint) (ptp_clock_id >> 24) & 0xff,
                (guint) (ptp_clock_id >> 16) & 0xff,
                (guint) (ptp_clock_id >> 8) & 0xff,
                (guint) (ptp_clock_id >> 0) & 0xff, ptp_domain);
          else
            ts_refclk =
                g_strdup_printf
                ("ptp=IEEE1588-2008:%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X",
                (guint) (ptp_clock_id >> 56) & 0xff,
                (guint) (ptp_clock_id >> 48) & 0xff,
                (guint) (ptp_clock_id >> 40) & 0xff,
                (guint) (ptp_clock_id >> 32) & 0xff,
                (guint) (ptp_clock_id >> 24) & 0xff,
                (guint) (ptp_clock_id >> 16) & 0xff,
                (guint) (ptp_clock_id >> 8) & 0xff,
                (guint) (ptp_clock_id >> 0) & 0xff);
        }
      }
    }
  clock_signalling_cleanup:
    if (clock)
      gst_object_unref (clock);

    if (!ts_refclk)
      ts_refclk = g_strdup ("local");
    if (!mediaclk)
      mediaclk = g_strdup ("sender");

    gst_sdp_media_add_attribute (smedia, "ts-refclk", ts_refclk);
    gst_sdp_media_add_attribute (smedia, "mediaclk", mediaclk);
    g_free (ts_refclk);
    g_free (mediaclk);
    gst_object_unref (joined_bin);
  }

  update_sdp_from_tags (stream, smedia);

  if (profile == GST_RTSP_PROFILE_AVPF || profile == GST_RTSP_PROFILE_SAVPF) {
    if ((rtx_time = gst_rtsp_stream_get_retransmission_time (stream))) {
      /* ssrc multiplexed retransmit functionality */
      guint rtx_pt = gst_rtsp_stream_get_retransmission_pt (stream);

      if (rtx_pt == 0) {
        g_warning ("failed to find an available dynamic payload type. "
            "Not adding retransmission");
      } else {
        gchar *tmp;
        GstStructure *s;
        gint caps_pt, caps_rate;

        s = gst_caps_get_structure (caps, 0);
        if (s == NULL)
          goto no_caps_info;

        /* get payload type and clock rate */
        gst_structure_get_int (s, "payload", &caps_pt);
        gst_structure_get_int (s, "clock-rate", &caps_rate);

        tmp = g_strdup_printf ("%d", rtx_pt);
        gst_sdp_media_add_format (smedia, tmp);
        g_free (tmp);

        tmp = g_strdup_printf ("%d rtx/%d", rtx_pt, caps_rate);
        gst_sdp_media_add_attribute (smedia, "rtpmap", tmp);
        g_free (tmp);

        tmp =
            g_strdup_printf ("%d apt=%d;rtx-time=%" G_GINT64_FORMAT, rtx_pt,
            caps_pt, GST_TIME_AS_MSECONDS (rtx_time));
        gst_sdp_media_add_attribute (smedia, "fmtp", tmp);
        g_free (tmp);
      }
    }

    if (gst_rtsp_stream_get_ulpfec_percentage (stream)) {
      guint ulpfec_pt = gst_rtsp_stream_get_ulpfec_pt (stream);

      if (ulpfec_pt == 0) {
        g_warning ("failed to find an available dynamic payload type. "
            "Not adding ulpfec");
      } else {
        gchar *tmp;
        GstStructure *s;
        gint caps_pt, caps_rate;

        s = gst_caps_get_structure (caps, 0);
        if (s == NULL)
          goto no_caps_info;

        /* get payload type and clock rate */
        gst_structure_get_int (s, "payload", &caps_pt);
        gst_structure_get_int (s, "clock-rate", &caps_rate);

        tmp = g_strdup_printf ("%d", ulpfec_pt);
        gst_sdp_media_add_format (smedia, tmp);
        g_free (tmp);

        tmp = g_strdup_printf ("%d ulpfec/%d", ulpfec_pt, caps_rate);
        gst_sdp_media_add_attribute (smedia, "rtpmap", tmp);
        g_free (tmp);

        tmp = g_strdup_printf ("%d apt=%d", ulpfec_pt, caps_pt);
        gst_sdp_media_add_attribute (smedia, "fmtp", tmp);
        g_free (tmp);
      }
    }
  }

  /* RFC5576 Source-specific media attributes */
  {
    GObject *session;
    guint ssrc;
    GstStructure *sdes;
    const gchar *cname;
    gchar *ssrc_cname;

    session = gst_rtsp_stream_get_rtpsession (stream);
    if (session) {
      g_object_get (session, "sdes", &sdes, NULL);

      cname = gst_structure_get_string (sdes, "cname");
      gst_rtsp_stream_get_ssrc (stream, &ssrc);

      if (cname) {
        ssrc_cname = g_strdup_printf ("%u cname:%s", ssrc, cname);
        gst_sdp_media_add_attribute (smedia, "ssrc", ssrc_cname);
        g_free (ssrc_cname);
      } else {
        GST_ERROR ("unable to get CNAME for stream %p", stream);
      }
      gst_structure_free (sdes);
      g_object_unref (session);
    } else {
      GST_ERROR ("unable to get RTP session from stream %p", stream);
    }
  }

  gst_sdp_message_add_media (sdp, smedia);
  gst_sdp_media_free (smedia);

  return TRUE;

  /* ERRORS */
caps_error:
  {
    gst_sdp_media_free (smedia);
    GST_ERROR ("unable to set media from caps for stream %d",
        gst_rtsp_stream_get_index (stream));
    return FALSE;
  }
no_multicast:
  {
    gst_sdp_media_free (smedia);
    GST_ERROR ("stream %d has no multicast address",
        gst_rtsp_stream_get_index (stream));
    return FALSE;
  }
no_caps_info:
  {
    gst_sdp_media_free (smedia);
    GST_ERROR ("caps for stream %d have no structure",
        gst_rtsp_stream_get_index (stream));
    return FALSE;
  }
crypto_sessions_error:
  {
    gst_sdp_media_free (smedia);
    GST_ERROR ("unable to add MIKEY crypto sessions for stream %d",
        gst_rtsp_stream_get_index (stream));
    return FALSE;
  }
}

/**
 * gst_rtsp_sdp_from_media:
 * @sdp: a #GstSDPMessage
 * @info: (transfer none): a #GstSDPInfo
 * @media: (transfer none): a #GstRTSPMedia
 *
 * Add @media specific info to @sdp. @info is used to configure the connection
 * information in the SDP.
 *
 * Returns: TRUE on success.
 */
gboolean
gst_rtsp_sdp_from_media (GstSDPMessage * sdp, GstSDPInfo * info,
    GstRTSPMedia * media)
{
  guint i, n_streams;
  gchar *rangestr;
  gboolean res;

  n_streams = gst_rtsp_media_n_streams (media);

  rangestr = gst_rtsp_media_get_range_string (media, FALSE, GST_RTSP_RANGE_NPT);
  if (rangestr == NULL)
    goto not_prepared;

  gst_sdp_message_add_attribute (sdp, "range", rangestr);
  g_free (rangestr);

  res = TRUE;
  for (i = 0; res && (i < n_streams); i++) {
    GstRTSPStream *stream;

    stream = gst_rtsp_media_get_stream (media, i);
    res = gst_rtsp_sdp_from_stream (sdp, info, stream);
    if (!res) {
      GST_ERROR ("could not get SDP from stream %p", stream);
      goto sdp_error;
    }
  }

  {
    GstNetTimeProvider *provider;

    if ((provider =
            gst_rtsp_media_get_time_provider (media, info->server_ip, 0))) {
      GstClock *clock;
      gchar *address, *str;
      gint port;

      g_object_get (provider, "clock", &clock, "address", &address, "port",
          &port, NULL);

      str = g_strdup_printf ("GstNetTimeProvider %s %s:%d %" G_GUINT64_FORMAT,
          g_type_name (G_TYPE_FROM_INSTANCE (clock)), address, port,
          gst_clock_get_time (clock));

      gst_sdp_message_add_attribute (sdp, "x-gst-clock", str);
      g_free (str);
      gst_object_unref (clock);
      g_free (address);
      gst_object_unref (provider);
    }
  }

  return res;

  /* ERRORS */
not_prepared:
  {
    GST_ERROR ("media %p is not prepared", media);
    return FALSE;
  }
sdp_error:
  {
    GST_ERROR ("could not get SDP from media %p", media);
    return FALSE;
  }
}

/**
 * gst_rtsp_sdp_from_stream:
 * @sdp: a #GstSDPMessage
 * @info: (transfer none): a #GstSDPInfo
 * @stream: (transfer none): a #GstRTSPStream
 *
 * Add info from @stream to @sdp.
 *
 * Returns: TRUE on success.
 */
gboolean
gst_rtsp_sdp_from_stream (GstSDPMessage * sdp, GstSDPInfo * info,
    GstRTSPStream * stream)
{
  GstCaps *caps;
  GstRTSPProfile profiles;
  guint mask;
  gboolean res;

  caps = gst_rtsp_stream_get_caps (stream);

  if (caps == NULL) {
    GST_ERROR ("stream %p has no caps", stream);
    return FALSE;
  }

  /* make a new media for each profile */
  profiles = gst_rtsp_stream_get_profiles (stream);
  mask = 1;
  res = TRUE;
  while (res && (profiles >= mask)) {
    GstRTSPProfile prof = profiles & mask;

    if (prof)
      res = gst_rtsp_sdp_make_media (sdp, info, stream, caps, prof);

    mask <<= 1;
  }
  gst_caps_unref (caps);

  return res;
}
