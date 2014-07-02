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
/**
 * SECTION:rtsp-sdp
 * @short_description: Make SDP messages
 * @see_also: #GstRTSPMedia
 *
 * Last reviewed on 2013-07-11 (1.0.0)
 */

#include <string.h>

#include <gst/sdp/gstmikey.h>

#include "rtsp-sdp.h"

#define AES_128_KEY_LEN 16
#define AES_256_KEY_LEN 32

#define HMAC_32_KEY_LEN 4
#define HMAC_80_KEY_LEN 10

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

  gst_pad_sticky_events_foreach (src_pad, get_info_from_tags, stream_media);

  gst_object_unref (src_pad);
}

static guint8
enc_key_length_from_cipher_name (const gchar * cipher)
{
  if (g_strcmp0 (cipher, "aes-128-icm") == 0)
    return AES_128_KEY_LEN;
  else if (g_strcmp0 (cipher, "aes-256-icm") == 0)
    return AES_256_KEY_LEN;
  else {
    GST_ERROR ("encryption algorithm '%s' not supported", cipher);
    return 0;
  }
}

static guint8
auth_key_length_from_auth_name (const gchar * auth)
{
  if (g_strcmp0 (auth, "hmac-sha1-32") == 0)
    return HMAC_32_KEY_LEN;
  else if (g_strcmp0 (auth, "hmac-sha1-80") == 0)
    return HMAC_80_KEY_LEN;
  else {
    GST_ERROR ("authentication algorithm '%s' not supported", auth);
    return 0;
  }
}

static void
make_media (GstSDPMessage * sdp, GstSDPInfo * info, GstRTSPMedia * media,
    GstRTSPStream * stream, GstStructure * s, GstRTSPProfile profile)
{
  GstSDPMedia *smedia;
  const gchar *caps_str, *caps_enc, *caps_params;
  gchar *tmp;
  gint caps_pt, caps_rate;
  guint n_fields, j;
  gboolean first;
  GString *fmtp;
  GstRTSPLowerTrans ltrans;
  GSocketFamily family;
  const gchar *addrtype, *proto;
  gchar *address;
  guint ttl;

  gst_sdp_media_new (&smedia);

  /* get media type and payload for the m= line */
  caps_str = gst_structure_get_string (s, "media");
  gst_sdp_media_set_media (smedia, caps_str);

  gst_structure_get_int (s, "payload", &caps_pt);
  tmp = g_strdup_printf ("%d", caps_pt);
  gst_sdp_media_add_format (smedia, tmp);
  g_free (tmp);

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

  /* get clock-rate, media type and params for the rtpmap attribute */
  gst_structure_get_int (s, "clock-rate", &caps_rate);
  caps_enc = gst_structure_get_string (s, "encoding-name");
  caps_params = gst_structure_get_string (s, "encoding-params");

  if (caps_enc) {
    if (caps_params)
      tmp = g_strdup_printf ("%d %s/%d/%s", caps_pt, caps_enc, caps_rate,
          caps_params);
    else
      tmp = g_strdup_printf ("%d %s/%d", caps_pt, caps_enc, caps_rate);

    gst_sdp_media_add_attribute (smedia, "rtpmap", tmp);
    g_free (tmp);
  }

  /* the config uri */
  tmp = gst_rtsp_stream_get_control (stream);
  gst_sdp_media_add_attribute (smedia, "control", tmp);
  g_free (tmp);


  /* check for srtp */
  do {
    GstBuffer *srtpkey;
    const GValue *val;
    const gchar *srtpcipher, *srtpauth, *srtcpcipher, *srtcpauth;
    GstMIKEYMessage *msg;
    GstMIKEYPayload *payload, *pkd;
    GBytes *bytes;
    GstMapInfo info;
    const guint8 *data;
    gsize size;
    gchar *base64;
    guint8 byte;
    guint32 ssrc;

    val = gst_structure_get_value (s, "srtp-key");
    if (val == NULL)
      break;

    srtpkey = gst_value_get_buffer (val);
    if (srtpkey == NULL)
      break;

    srtpcipher = gst_structure_get_string (s, "srtp-cipher");
    srtpauth = gst_structure_get_string (s, "srtp-auth");
    srtcpcipher = gst_structure_get_string (s, "srtcp-cipher");
    srtcpauth = gst_structure_get_string (s, "srtcp-auth");

    if (srtpcipher == NULL || srtpauth == NULL || srtcpcipher == NULL ||
        srtcpauth == NULL)
      break;

    msg = gst_mikey_message_new ();
    /* unencrypted MIKEY message, we send this over TLS so this is allowed */
    gst_mikey_message_set_info (msg, GST_MIKEY_VERSION, GST_MIKEY_TYPE_PSK_INIT,
        FALSE, GST_MIKEY_PRF_MIKEY_1, 0, GST_MIKEY_MAP_TYPE_SRTP);
    /* add policy '0' for our SSRC */
    gst_rtsp_stream_get_ssrc (stream, &ssrc);
    gst_mikey_message_add_cs_srtp (msg, 0, ssrc, 0);
    /* timestamp is now */
    gst_mikey_message_add_t_now_ntp_utc (msg);
    /* add some random data */
    gst_mikey_message_add_rand_len (msg, 16);

    /* the policy '0' is SRTP with the above discovered algorithms */
    payload = gst_mikey_payload_new (GST_MIKEY_PT_SP);
    gst_mikey_payload_sp_set (payload, 0, GST_MIKEY_SEC_PROTO_SRTP);

    /* only AES-CM is supported */
    byte = 1;
    gst_mikey_payload_sp_add_param (payload, GST_MIKEY_SP_SRTP_ENC_ALG, 1,
        &byte);
    /* Encryption key length */
    byte = enc_key_length_from_cipher_name (srtpcipher);
    gst_mikey_payload_sp_add_param (payload, GST_MIKEY_SP_SRTP_ENC_KEY_LEN, 1,
        &byte);
    /* only HMAC-SHA1 */
    gst_mikey_payload_sp_add_param (payload, GST_MIKEY_SP_SRTP_AUTH_ALG, 1,
        &byte);
    /* Authentication key length */
    byte = auth_key_length_from_auth_name (srtpauth);
    gst_mikey_payload_sp_add_param (payload, GST_MIKEY_SP_SRTP_AUTH_KEY_LEN, 1,
        &byte);
    /* we enable encryption on RTP and RTCP */
    gst_mikey_payload_sp_add_param (payload, GST_MIKEY_SP_SRTP_SRTP_ENC, 1,
        &byte);
    gst_mikey_payload_sp_add_param (payload, GST_MIKEY_SP_SRTP_SRTCP_ENC, 1,
        &byte);
    /* we enable authentication on RTP and RTCP */
    gst_mikey_payload_sp_add_param (payload, GST_MIKEY_SP_SRTP_SRTP_AUTH, 1,
        &byte);
    gst_mikey_message_add_payload (msg, payload);

    /* make unencrypted KEMAC */
    payload = gst_mikey_payload_new (GST_MIKEY_PT_KEMAC);
    gst_mikey_payload_kemac_set (payload, GST_MIKEY_ENC_NULL,
        GST_MIKEY_MAC_NULL);

    /* add the key in key data */
    pkd = gst_mikey_payload_new (GST_MIKEY_PT_KEY_DATA);
    gst_buffer_map (srtpkey, &info, GST_MAP_READ);
    gst_mikey_payload_key_data_set_key (pkd, GST_MIKEY_KD_TEK, info.size,
        info.data);
    gst_buffer_unmap (srtpkey, &info);
    /* add key data to KEMAC */
    gst_mikey_payload_kemac_add_sub (payload, pkd);
    gst_mikey_message_add_payload (msg, payload);

    /* now serialize this to bytes */
    bytes = gst_mikey_message_to_bytes (msg, NULL, NULL);
    gst_mikey_message_unref (msg);
    /* and make it into base64 */
    data = g_bytes_get_data (bytes, &size);
    base64 = g_base64_encode (data, size);
    g_bytes_unref (bytes);

    tmp = g_strdup_printf ("mikey %s", base64);
    g_free (base64);

    gst_sdp_media_add_attribute (smedia, "key-mgmt", tmp);
    g_free (tmp);
  } while (FALSE);

  /* collect all other properties and add them to fmtp or attributes */
  fmtp = g_string_new ("");
  g_string_append_printf (fmtp, "%d ", caps_pt);
  first = TRUE;
  n_fields = gst_structure_n_fields (s);
  for (j = 0; j < n_fields; j++) {
    const gchar *fname, *fval;

    fname = gst_structure_nth_field_name (s, j);

    /* filter out standard properties */
    if (!strcmp (fname, "media"))
      continue;
    if (!strcmp (fname, "payload"))
      continue;
    if (!strcmp (fname, "clock-rate"))
      continue;
    if (!strcmp (fname, "encoding-name"))
      continue;
    if (!strcmp (fname, "encoding-params"))
      continue;
    if (!strcmp (fname, "ssrc"))
      continue;
    if (!strcmp (fname, "clock-base"))
      continue;
    if (!strcmp (fname, "seqnum-base"))
      continue;
    if (g_str_has_prefix (fname, "srtp-"))
      continue;
    if (g_str_has_prefix (fname, "srtcp-"))
      continue;

    if (g_str_has_prefix (fname, "a-")) {
      /* attribute */
      if ((fval = gst_structure_get_string (s, fname)))
        gst_sdp_media_add_attribute (smedia, fname + 2, fval);
      continue;
    }
    if (g_str_has_prefix (fname, "x-")) {
      /* attribute */
      if ((fval = gst_structure_get_string (s, fname)))
        gst_sdp_media_add_attribute (smedia, fname, fval);
      continue;
    }

    if ((fval = gst_structure_get_string (s, fname))) {
      g_string_append_printf (fmtp, "%s%s=%s", first ? "" : ";", fname, fval);
      first = FALSE;
    }
  }
  if (!first) {
    tmp = g_string_free (fmtp, FALSE);
    gst_sdp_media_add_attribute (smedia, "fmtp", tmp);
    g_free (tmp);
  } else {
    g_string_free (fmtp, TRUE);
  }

  update_sdp_from_tags (stream, smedia);

  gst_sdp_message_add_media (sdp, smedia);
  gst_sdp_media_free (smedia);

  return;

  /* ERRORS */
no_multicast:
  {
    gst_sdp_media_free (smedia);
    g_warning ("ignoring stream %d without multicast address",
        gst_rtsp_stream_get_index (stream));
    return;
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

  n_streams = gst_rtsp_media_n_streams (media);

  rangestr = gst_rtsp_media_get_range_string (media, FALSE, GST_RTSP_RANGE_NPT);
  if (rangestr == NULL)
    goto not_prepared;

  gst_sdp_message_add_attribute (sdp, "range", rangestr);
  g_free (rangestr);

  for (i = 0; i < n_streams; i++) {
    GstRTSPStream *stream;
    GstCaps *caps;
    GstStructure *s;
    GstRTSPProfile profiles;
    guint mask;

    stream = gst_rtsp_media_get_stream (media, i);
    caps = gst_rtsp_stream_get_caps (stream);

    if (caps == NULL) {
      g_warning ("ignoring stream %d without media type", i);
      continue;
    }

    s = gst_caps_get_structure (caps, 0);
    if (s == NULL) {
      gst_caps_unref (caps);
      g_warning ("ignoring stream %d without media type", i);
      continue;
    }

    /* make a new media for each profile */
    profiles = gst_rtsp_stream_get_profiles (stream);
    mask = 1;
    while (profiles >= mask) {
      GstRTSPProfile prof = profiles & mask;

      if (prof)
        make_media (sdp, info, media, stream, s, prof);

      mask <<= 1;
    }
    gst_caps_unref (caps);
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

  return TRUE;

  /* ERRORS */
not_prepared:
  {
    GST_ERROR ("media %p is not prepared", media);
    return FALSE;
  }
}
