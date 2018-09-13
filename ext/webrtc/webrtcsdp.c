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

#include "webrtcsdp.h"

#include "utils.h"
#include "gstwebrtcbin.h"

#include <string.h>

#define IS_EMPTY_SDP_ATTRIBUTE(val) (val == NULL || g_strcmp0(val, "") == 0)

const gchar *
_sdp_source_to_string (SDPSource source)
{
  switch (source) {
    case SDP_LOCAL:
      return "local";
    case SDP_REMOTE:
      return "remote";
    default:
      return "none";
  }
}

static gboolean
_check_valid_state_for_sdp_change (GstWebRTCBin * webrtc, SDPSource source,
    GstWebRTCSDPType type, GError ** error)
{
  GstWebRTCSignalingState state = webrtc->signaling_state;
#define STATE(val) GST_WEBRTC_SIGNALING_STATE_ ## val
#define TYPE(val) GST_WEBRTC_SDP_TYPE_ ## val

  if (source == SDP_LOCAL && type == TYPE (OFFER) && state == STATE (STABLE))
    return TRUE;
  if (source == SDP_LOCAL && type == TYPE (OFFER)
      && state == STATE (HAVE_LOCAL_OFFER))
    return TRUE;
  if (source == SDP_LOCAL && type == TYPE (ANSWER)
      && state == STATE (HAVE_REMOTE_OFFER))
    return TRUE;
  if (source == SDP_LOCAL && type == TYPE (PRANSWER)
      && state == STATE (HAVE_REMOTE_OFFER))
    return TRUE;
  if (source == SDP_LOCAL && type == TYPE (PRANSWER)
      && state == STATE (HAVE_LOCAL_PRANSWER))
    return TRUE;

  if (source == SDP_REMOTE && type == TYPE (OFFER) && state == STATE (STABLE))
    return TRUE;
  if (source == SDP_REMOTE && type == TYPE (OFFER)
      && state == STATE (HAVE_REMOTE_OFFER))
    return TRUE;
  if (source == SDP_REMOTE && type == TYPE (ANSWER)
      && state == STATE (HAVE_LOCAL_OFFER))
    return TRUE;
  if (source == SDP_REMOTE && type == TYPE (PRANSWER)
      && state == STATE (HAVE_LOCAL_OFFER))
    return TRUE;
  if (source == SDP_REMOTE && type == TYPE (PRANSWER)
      && state == STATE (HAVE_REMOTE_PRANSWER))
    return TRUE;

  {
    gchar *state = _enum_value_to_string (GST_TYPE_WEBRTC_SIGNALING_STATE,
        webrtc->signaling_state);
    gchar *type_str = _enum_value_to_string (GST_TYPE_WEBRTC_SDP_TYPE, type);
    g_set_error (error, GST_WEBRTC_BIN_ERROR,
        GST_WEBRTC_BIN_ERROR_INVALID_STATE,
        "Not in the correct state (%s) for setting %s %s description", state,
        _sdp_source_to_string (source), type_str);
    g_free (state);
    g_free (type_str);
  }

  return FALSE;

#undef STATE
#undef TYPE
}

static gboolean
_check_sdp_crypto (GstWebRTCBin * webrtc, SDPSource source,
    GstWebRTCSessionDescription * sdp, GError ** error)
{
  const gchar *message_fingerprint, *fingerprint;
  const GstSDPKey *key;
  int i;

  key = gst_sdp_message_get_key (sdp->sdp);
  if (!IS_EMPTY_SDP_ATTRIBUTE (key->data)) {
    g_set_error_literal (error, GST_WEBRTC_BIN_ERROR,
        GST_WEBRTC_BIN_ERROR_BAD_SDP, "sdp contains a k line");
    return FALSE;
  }

  message_fingerprint = fingerprint =
      gst_sdp_message_get_attribute_val (sdp->sdp, "fingerprint");
  for (i = 0; i < gst_sdp_message_medias_len (sdp->sdp); i++) {
    const GstSDPMedia *media = gst_sdp_message_get_media (sdp->sdp, i);
    const gchar *media_fingerprint =
        gst_sdp_media_get_attribute_val (media, "fingerprint");

    if (!IS_EMPTY_SDP_ATTRIBUTE (message_fingerprint)
        && !IS_EMPTY_SDP_ATTRIBUTE (media_fingerprint)) {
      g_set_error (error, GST_WEBRTC_BIN_ERROR,
          GST_WEBRTC_BIN_ERROR_FINGERPRINT,
          "No fingerprint lines in sdp for media %u", i);
      return FALSE;
    }
    if (IS_EMPTY_SDP_ATTRIBUTE (fingerprint)) {
      fingerprint = media_fingerprint;
    }
    if (!IS_EMPTY_SDP_ATTRIBUTE (media_fingerprint)
        && g_strcmp0 (fingerprint, media_fingerprint) != 0) {
      g_set_error (error, GST_WEBRTC_BIN_ERROR,
          GST_WEBRTC_BIN_ERROR_FINGERPRINT,
          "Fingerprint in media %u differs from %s fingerprint. "
          "\'%s\' != \'%s\'", i, message_fingerprint ? "global" : "previous",
          fingerprint, media_fingerprint);
      return FALSE;
    }
  }

  return TRUE;
}

#if 0
static gboolean
_session_has_attribute_key (const GstSDPMessage * msg, const gchar * key)
{
  int i;
  for (i = 0; i < gst_sdp_message_attributes_len (msg); i++) {
    const GstSDPAttribute *attr = gst_sdp_message_get_attribute (msg, i);

    if (g_strcmp0 (attr->key, key) == 0)
      return TRUE;
  }

  return FALSE;
}

static gboolean
_session_has_attribute_key_value (const GstSDPMessage * msg, const gchar * key,
    const gchar * value)
{
  int i;
  for (i = 0; i < gst_sdp_message_attributes_len (msg); i++) {
    const GstSDPAttribute *attr = gst_sdp_message_get_attribute (msg, i);

    if (g_strcmp0 (attr->key, key) == 0 && g_strcmp0 (attr->value, value) == 0)
      return TRUE;
  }

  return FALSE;
}

static gboolean
_check_trickle_ice (GstSDPMessage * msg, GError ** error)
{
  if (!_session_has_attribute_key_value (msg, "ice-options", "trickle")) {
    g_set_error_literal (error, GST_WEBRTC_BIN_ERROR,
        GST_WEBRTC_BIN_ERROR_BAD_SDP,
        "No required \'a=ice-options:trickle\' line in sdp");
  }
  return TRUE;
}
#endif
gboolean
_media_has_attribute_key (const GstSDPMedia * media, const gchar * key)
{
  int i;
  for (i = 0; i < gst_sdp_media_attributes_len (media); i++) {
    const GstSDPAttribute *attr = gst_sdp_media_get_attribute (media, i);

    if (g_strcmp0 (attr->key, key) == 0)
      return TRUE;
  }

  return FALSE;
}

static gboolean
_media_has_mid (const GstSDPMedia * media, guint media_idx, GError ** error)
{
  const gchar *mid = gst_sdp_media_get_attribute_val (media, "mid");
  if (IS_EMPTY_SDP_ATTRIBUTE (mid)) {
    g_set_error (error, GST_WEBRTC_BIN_ERROR, GST_WEBRTC_BIN_ERROR_BAD_SDP,
        "media %u is missing or contains an empty \'mid\' attribute",
        media_idx);
    return FALSE;
  }
  return TRUE;
}

static const gchar *
_media_get_ice_ufrag (const GstSDPMessage * msg, guint media_idx)
{
  const gchar *ice_ufrag;

  ice_ufrag = gst_sdp_message_get_attribute_val (msg, "ice-ufrag");
  if (IS_EMPTY_SDP_ATTRIBUTE (ice_ufrag)) {
    const GstSDPMedia *media = gst_sdp_message_get_media (msg, media_idx);
    ice_ufrag = gst_sdp_media_get_attribute_val (media, "ice-ufrag");
    if (IS_EMPTY_SDP_ATTRIBUTE (ice_ufrag))
      return NULL;
  }
  return ice_ufrag;
}

static const gchar *
_media_get_ice_pwd (const GstSDPMessage * msg, guint media_idx)
{
  const gchar *ice_pwd;

  ice_pwd = gst_sdp_message_get_attribute_val (msg, "ice-pwd");
  if (IS_EMPTY_SDP_ATTRIBUTE (ice_pwd)) {
    const GstSDPMedia *media = gst_sdp_message_get_media (msg, media_idx);
    ice_pwd = gst_sdp_media_get_attribute_val (media, "ice-pwd");
    if (IS_EMPTY_SDP_ATTRIBUTE (ice_pwd))
      return NULL;
  }
  return ice_pwd;
}

static gboolean
_media_has_setup (const GstSDPMedia * media, guint media_idx, GError ** error)
{
  static const gchar *valid_setups[] = { "actpass", "active", "passive", NULL };
  const gchar *setup = gst_sdp_media_get_attribute_val (media, "setup");
  if (IS_EMPTY_SDP_ATTRIBUTE (setup)) {
    g_set_error (error, GST_WEBRTC_BIN_ERROR, GST_WEBRTC_BIN_ERROR_BAD_SDP,
        "media %u is missing or contains an empty \'setup\' attribute",
        media_idx);
    return FALSE;
  }
  if (!g_strv_contains (valid_setups, setup)) {
    g_set_error (error, GST_WEBRTC_BIN_ERROR, GST_WEBRTC_BIN_ERROR_BAD_SDP,
        "media %u contains unknown \'setup\' attribute, \'%s\'", media_idx,
        setup);
    return FALSE;
  }
  return TRUE;
}

#if 0
static gboolean
_media_has_dtls_id (const GstSDPMedia * media, guint media_idx, GError ** error)
{
  const gchar *dtls_id = gst_sdp_media_get_attribute_val (media, "ice-pwd");
  if (IS_EMPTY_SDP_ATTRIBUTE (dtls_id)) {
    g_set_error (error, GST_WEBRTC_BIN_ERROR, GST_WEBRTC_BIN_ERROR_BAD_SDP,
        "media %u is missing or contains an empty \'dtls-id\' attribute",
        media_idx);
    return FALSE;
  }
  return TRUE;
}
#endif
gboolean
validate_sdp (GstWebRTCBin * webrtc, SDPSource source,
    GstWebRTCSessionDescription * sdp, GError ** error)
{
  const gchar *group, *bundle_ice_ufrag = NULL, *bundle_ice_pwd = NULL;
  gchar **group_members = NULL;
  gboolean is_bundle = FALSE;
  int i;

  if (!_check_valid_state_for_sdp_change (webrtc, source, sdp->type, error))
    return FALSE;
  if (!_check_sdp_crypto (webrtc, source, sdp, error))
    return FALSE;
/* not explicitly required
  if (ICE && !_check_trickle_ice (sdp->sdp))
    return FALSE;*/
  group = gst_sdp_message_get_attribute_val (sdp->sdp, "group");
  is_bundle = group && g_str_has_prefix (group, "BUNDLE");
  if (is_bundle)
    group_members = g_strsplit (&group[6], " ", -1);

  for (i = 0; i < gst_sdp_message_medias_len (sdp->sdp); i++) {
    const GstSDPMedia *media = gst_sdp_message_get_media (sdp->sdp, i);
    const gchar *mid;
    gboolean media_in_bundle = FALSE;
    if (!_media_has_mid (media, i, error))
      goto fail;
    mid = gst_sdp_media_get_attribute_val (media, "mid");
    media_in_bundle = is_bundle
        && g_strv_contains ((const gchar **) group_members, mid);
    if (!_media_get_ice_ufrag (sdp->sdp, i)) {
      g_set_error (error, GST_WEBRTC_BIN_ERROR, GST_WEBRTC_BIN_ERROR_BAD_SDP,
          "media %u is missing or contains an empty \'ice-ufrag\' attribute",
          i);
      goto fail;
    }
    if (!_media_get_ice_pwd (sdp->sdp, i)) {
      g_set_error (error, GST_WEBRTC_BIN_ERROR, GST_WEBRTC_BIN_ERROR_BAD_SDP,
          "media %u is missing or contains an empty \'ice-pwd\' attribute", i);
      goto fail;
    }
    if (!_media_has_setup (media, i, error))
      goto fail;
    /* check paramaters in bundle are the same */
    if (media_in_bundle) {
      const gchar *ice_ufrag =
          gst_sdp_media_get_attribute_val (media, "ice-ufrag");
      const gchar *ice_pwd = gst_sdp_media_get_attribute_val (media, "ice-pwd");
      if (!bundle_ice_ufrag)
        bundle_ice_ufrag = ice_ufrag;
      else if (g_strcmp0 (bundle_ice_ufrag, ice_ufrag) != 0) {
        g_set_error (error, GST_WEBRTC_BIN_ERROR, GST_WEBRTC_BIN_ERROR_BAD_SDP,
            "media %u has different ice-ufrag values in bundle. "
            "%s != %s", i, bundle_ice_ufrag, ice_ufrag);
        goto fail;
      }
      if (!bundle_ice_pwd) {
        bundle_ice_pwd = ice_pwd;
      } else if (g_strcmp0 (bundle_ice_pwd, ice_pwd) != 0) {
        g_set_error (error, GST_WEBRTC_BIN_ERROR, GST_WEBRTC_BIN_ERROR_BAD_SDP,
            "media %u has different ice-pwd values in bundle. "
            "%s != %s", i, bundle_ice_pwd, ice_pwd);
        goto fail;
      }
    }
  }

  g_strfreev (group_members);

  return TRUE;

fail:
  g_strfreev (group_members);
  return FALSE;
}

GstWebRTCRTPTransceiverDirection
_get_direction_from_media (const GstSDPMedia * media)
{
  GstWebRTCRTPTransceiverDirection new_dir =
      GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_NONE;
  int i;

  for (i = 0; i < gst_sdp_media_attributes_len (media); i++) {
    const GstSDPAttribute *attr = gst_sdp_media_get_attribute (media, i);

    if (g_strcmp0 (attr->key, "sendonly") == 0) {
      if (new_dir != GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_NONE) {
        GST_ERROR ("Multiple direction attributes");
        return GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_NONE;
      }
      new_dir = GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY;
    } else if (g_strcmp0 (attr->key, "sendrecv") == 0) {
      if (new_dir != GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_NONE) {
        GST_ERROR ("Multiple direction attributes");
        return GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_NONE;
      }
      new_dir = GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDRECV;
    } else if (g_strcmp0 (attr->key, "recvonly") == 0) {
      if (new_dir != GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_NONE) {
        GST_ERROR ("Multiple direction attributes");
        return GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_NONE;
      }
      new_dir = GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY;
    } else if (g_strcmp0 (attr->key, "inactive") == 0) {
      if (new_dir != GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_NONE) {
        GST_ERROR ("Multiple direction attributes");
        return GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_NONE;
      }
      new_dir = GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_INACTIVE;
    }
  }

  return new_dir;
}

#define DIR(val) GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_ ## val
GstWebRTCRTPTransceiverDirection
_intersect_answer_directions (GstWebRTCRTPTransceiverDirection offer,
    GstWebRTCRTPTransceiverDirection answer)
{
  if (offer == DIR (SENDONLY) && answer == DIR (SENDRECV))
    return DIR (RECVONLY);
  if (offer == DIR (SENDONLY) && answer == DIR (RECVONLY))
    return DIR (RECVONLY);
  if (offer == DIR (RECVONLY) && answer == DIR (SENDRECV))
    return DIR (SENDONLY);
  if (offer == DIR (RECVONLY) && answer == DIR (SENDONLY))
    return DIR (SENDONLY);
  if (offer == DIR (SENDRECV) && answer == DIR (SENDRECV))
    return DIR (SENDRECV);
  if (offer == DIR (SENDRECV) && answer == DIR (SENDONLY))
    return DIR (SENDONLY);
  if (offer == DIR (SENDRECV) && answer == DIR (RECVONLY))
    return DIR (RECVONLY);

  return DIR (NONE);
}

void
_media_replace_direction (GstSDPMedia * media,
    GstWebRTCRTPTransceiverDirection direction)
{
  gchar *dir_str;
  int i;

  dir_str =
      _enum_value_to_string (GST_TYPE_WEBRTC_RTP_TRANSCEIVER_DIRECTION,
      direction);

  for (i = 0; i < gst_sdp_media_attributes_len (media); i++) {
    const GstSDPAttribute *attr = gst_sdp_media_get_attribute (media, i);

    if (g_strcmp0 (attr->key, "sendonly") == 0
        || g_strcmp0 (attr->key, "sendrecv") == 0
        || g_strcmp0 (attr->key, "recvonly") == 0) {
      GstSDPAttribute new_attr = { 0, };
      GST_TRACE ("replace %s with %s", attr->key, dir_str);
      gst_sdp_attribute_set (&new_attr, dir_str, "");
      gst_sdp_media_replace_attribute (media, i, &new_attr);
      return;
    }
  }

  GST_TRACE ("add %s", dir_str);
  gst_sdp_media_add_attribute (media, dir_str, "");
  g_free (dir_str);
}

GstWebRTCRTPTransceiverDirection
_get_final_direction (GstWebRTCRTPTransceiverDirection local_dir,
    GstWebRTCRTPTransceiverDirection remote_dir)
{
  GstWebRTCRTPTransceiverDirection new_dir;
  new_dir = DIR (NONE);
  switch (local_dir) {
    case DIR (INACTIVE):
      new_dir = DIR (INACTIVE);
      break;
    case DIR (SENDONLY):
      if (remote_dir == DIR (SENDONLY)) {
        GST_ERROR ("remote SDP has the same directionality. "
            "This is not legal.");
        return DIR (NONE);
      } else if (remote_dir == DIR (INACTIVE)) {
        new_dir = DIR (INACTIVE);
      } else {
        new_dir = DIR (SENDONLY);
      }
      break;
    case DIR (RECVONLY):
      if (remote_dir == DIR (RECVONLY)) {
        GST_ERROR ("remote SDP has the same directionality. "
            "This is not legal.");
        return DIR (NONE);
      } else if (remote_dir == DIR (INACTIVE)) {
        new_dir = DIR (INACTIVE);
      } else {
        new_dir = DIR (RECVONLY);
      }
      break;
    case DIR (SENDRECV):
      if (remote_dir == DIR (INACTIVE)) {
        new_dir = DIR (INACTIVE);
      } else if (remote_dir == DIR (SENDONLY)) {
        new_dir = DIR (RECVONLY);
      } else if (remote_dir == DIR (RECVONLY)) {
        new_dir = DIR (SENDONLY);
      } else if (remote_dir == DIR (SENDRECV)) {
        new_dir = DIR (SENDRECV);
      }
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  if (new_dir == DIR (NONE)) {
    GST_ERROR ("Abnormal situation!");
    return DIR (NONE);
  }

  return new_dir;
}

#undef DIR

#define SETUP(val) GST_WEBRTC_DTLS_SETUP_ ## val
GstWebRTCDTLSSetup
_get_dtls_setup_from_media (const GstSDPMedia * media)
{
  int i;

  for (i = 0; i < gst_sdp_media_attributes_len (media); i++) {
    const GstSDPAttribute *attr = gst_sdp_media_get_attribute (media, i);

    if (g_strcmp0 (attr->key, "setup") == 0) {
      if (g_strcmp0 (attr->value, "actpass") == 0) {
        return SETUP (ACTPASS);
      } else if (g_strcmp0 (attr->value, "active") == 0) {
        return SETUP (ACTIVE);
      } else if (g_strcmp0 (attr->value, "passive") == 0) {
        return SETUP (PASSIVE);
      } else {
        GST_ERROR ("unknown setup value %s", attr->value);
        return SETUP (NONE);
      }
    }
  }

  GST_LOG ("no setup attribute in media");
  return SETUP (NONE);
}

GstWebRTCDTLSSetup
_intersect_dtls_setup (GstWebRTCDTLSSetup offer)
{
  switch (offer) {
    case SETUP (NONE):         /* default is active */
    case SETUP (ACTPASS):
    case SETUP (PASSIVE):
      return SETUP (ACTIVE);
    case SETUP (ACTIVE):
      return SETUP (PASSIVE);
    default:
      return SETUP (NONE);
  }
}

void
_media_replace_setup (GstSDPMedia * media, GstWebRTCDTLSSetup setup)
{
  gchar *setup_str;
  int i;

  setup_str = _enum_value_to_string (GST_TYPE_WEBRTC_DTLS_SETUP, setup);

  for (i = 0; i < gst_sdp_media_attributes_len (media); i++) {
    const GstSDPAttribute *attr = gst_sdp_media_get_attribute (media, i);

    if (g_strcmp0 (attr->key, "setup") == 0) {
      GstSDPAttribute new_attr = { 0, };
      GST_TRACE ("replace setup:%s with setup:%s", attr->value, setup_str);
      gst_sdp_attribute_set (&new_attr, "setup", setup_str);
      gst_sdp_media_replace_attribute (media, i, &new_attr);
      return;
    }
  }

  GST_TRACE ("add setup:%s", setup_str);
  gst_sdp_media_add_attribute (media, "setup", setup_str);
  g_free (setup_str);
}

GstWebRTCDTLSSetup
_get_final_setup (GstWebRTCDTLSSetup local_setup,
    GstWebRTCDTLSSetup remote_setup)
{
  GstWebRTCDTLSSetup new_setup;

  new_setup = SETUP (NONE);
  switch (local_setup) {
    case SETUP (NONE):
      /* someone's done a bad job of mangling the SDP. or bugs */
      g_critical ("Received a locally generated sdp without a parseable "
          "\'a=setup\' line.  This indicates a bug somewhere.  Bailing");
      return SETUP (NONE);
    case SETUP (ACTIVE):
      if (remote_setup == SETUP (ACTIVE)) {
        GST_ERROR ("remote SDP has the same "
            "\'a=setup:active\' attribute. This is not legal");
        return SETUP (NONE);
      }
      new_setup = SETUP (ACTIVE);
      break;
    case SETUP (PASSIVE):
      if (remote_setup == SETUP (PASSIVE)) {
        GST_ERROR ("remote SDP has the same "
            "\'a=setup:passive\' attribute. This is not legal");
        return SETUP (NONE);
      }
      new_setup = SETUP (PASSIVE);
      break;
    case SETUP (ACTPASS):
      if (remote_setup == SETUP (ACTPASS)) {
        GST_ERROR ("remote SDP has the same "
            "\'a=setup:actpass\' attribute. This is not legal");
        return SETUP (NONE);
      }
      if (remote_setup == SETUP (ACTIVE))
        new_setup = SETUP (PASSIVE);
      else if (remote_setup == SETUP (PASSIVE))
        new_setup = SETUP (ACTIVE);
      else if (remote_setup == SETUP (NONE)) {
        /* XXX: what to do here? */
        GST_WARNING ("unspecified situation. local: "
            "\'a=setup:actpass\' remote: none/unparseable");
        new_setup = SETUP (ACTIVE);
      }
      break;
    default:
      g_assert_not_reached ();
      return SETUP (NONE);
  }
  if (new_setup == SETUP (NONE)) {
    GST_ERROR ("Abnormal situation!");
    return SETUP (NONE);
  }

  return new_setup;
}

#undef SETUP

gchar *
_generate_fingerprint_from_certificate (gchar * certificate,
    GChecksumType checksum_type)
{
  gchar **lines, *line;
  guchar *tmp, *decoded, *digest;
  GChecksum *checksum;
  GString *fingerprint;
  gsize decoded_length, digest_size;
  gint state = 0;
  guint save = 0;
  int i;

  g_return_val_if_fail (certificate != NULL, NULL);

  /* 1. decode the certificate removing newlines and the certificate header
   * and footer */
  decoded = tmp = g_new0 (guchar, (strlen (certificate) / 4) * 3 + 3);
  lines = g_strsplit (certificate, "\n", 0);
  for (i = 0, line = lines[i]; line; line = lines[++i]) {
    if (line[0] && !g_str_has_prefix (line, "-----"))
      tmp += g_base64_decode_step (line, strlen (line), tmp, &state, &save);
  }
  g_strfreev (lines);
  decoded_length = tmp - decoded;

  /* 2. compute a checksum of the decoded certificate */
  checksum = g_checksum_new (checksum_type);
  digest_size = g_checksum_type_get_length (checksum_type);
  digest = g_new (guint8, digest_size);
  g_checksum_update (checksum, decoded, decoded_length);
  g_checksum_get_digest (checksum, digest, &digest_size);
  g_free (decoded);

  /* 3. hex encode the checksum separated with ':'s */
  fingerprint = g_string_new (NULL);
  for (i = 0; i < digest_size; i++) {
    if (i)
      g_string_append (fingerprint, ":");
    g_string_append_printf (fingerprint, "%02X", digest[i]);
  }

  g_free (digest);
  g_checksum_free (checksum);

  return g_string_free (fingerprint, FALSE);
}

#define DEFAULT_ICE_UFRAG_LEN 32
#define DEFAULT_ICE_PASSWORD_LEN 32
static const gchar *ice_credential_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ" "abcdefghijklmnopqrstuvwxyz" "0123456789" "+/";

void
_generate_ice_credentials (gchar ** ufrag, gchar ** password)
{
  int i;

  *ufrag = g_malloc0 (DEFAULT_ICE_UFRAG_LEN + 1);
  for (i = 0; i < DEFAULT_ICE_UFRAG_LEN; i++)
    (*ufrag)[i] =
        ice_credential_chars[g_random_int_range (0,
            strlen (ice_credential_chars))];

  *password = g_malloc0 (DEFAULT_ICE_PASSWORD_LEN + 1);
  for (i = 0; i < DEFAULT_ICE_PASSWORD_LEN; i++)
    (*password)[i] =
        ice_credential_chars[g_random_int_range (0,
            strlen (ice_credential_chars))];
}

int
_get_sctp_port_from_media (const GstSDPMedia * media)
{
  int sctpmap = -1, i;

  for (i = 0; i < gst_sdp_media_attributes_len (media); i++) {
    const GstSDPAttribute *attr = gst_sdp_media_get_attribute (media, i);

    if (g_strcmp0 (attr->key, "sctp-port") == 0) {
      return atoi (attr->value);
    } else if (g_strcmp0 (attr->key, "sctpmap") == 0) {
      sctpmap = atoi (attr->value);
    }
  }

  if (sctpmap >= 0)
    GST_LOG ("no sctp-port attribute in media");
  return sctpmap;
}

guint64
_get_sctp_max_message_size_from_media (const GstSDPMedia * media)
{
  int i;

  for (i = 0; i < gst_sdp_media_attributes_len (media); i++) {
    const GstSDPAttribute *attr = gst_sdp_media_get_attribute (media, i);

    if (g_strcmp0 (attr->key, "max-message-size") == 0)
      return atoi (attr->value);
  }

  return 65536;
}
