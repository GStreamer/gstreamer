/* GStreamer
 * Copyright (C) <2014> Wim Taymans <wim.taymans@gmail.com>
 *
 * gstmikey.h: various helper functions to manipulate mikey messages
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
 * SECTION:gstmikey
 * @short_description: Helper methods for dealing with MIKEY messages
 *
 * <refsect2>
 * <para>
 * The GstMIKEY helper functions makes it easy to parse and create MIKEY
 * messages.
 * </para>
 * </refsect2>
 *
 * Last reviewed on 2014-03-20 (1.3.0)
 */

#include <string.h>

#include "gstmikey.h"

#define INIT_ARRAY(field, type, init_func)              \
G_STMT_START {                                          \
  if (field)                                            \
    g_array_set_size ((field), 0);                      \
  else {                                                \
    (field) = g_array_new (FALSE, TRUE, sizeof (type)); \
    g_array_set_clear_func ((field), (GDestroyNotify)init_func);        \
  }                                                     \
} G_STMT_END

#define FREE_ARRAY(field)         \
G_STMT_START {                    \
  if (field)                      \
    g_array_free ((field), TRUE); \
  (field) = NULL;                 \
} G_STMT_END

#define INIT_MEMDUP(field, data, len)            \
G_STMT_START {                                   \
  g_free ((field));                              \
  (field) = g_memdup (data, len);                \
} G_STMT_END
#define FREE_MEMDUP(field)                       \
G_STMT_START {                                   \
  g_free ((field));                              \
  (field) = NULL;                                \
} G_STMT_END


/* Key data transport payload (KEMAC) */
static guint
get_mac_len (GstMIKEYMacAlg mac_alg)
{
  guint len;

  switch (mac_alg) {
    case GST_MIKEY_MAC_NULL:
      len = 0;                  /* no MAC key */
      break;
    case GST_MIKEY_MAC_HMAC_SHA_1_160:
      len = 20;                 /* 160 bits key */
      break;
    default:
      len = -1;
      break;
  }
  return len;
}

/**
 * gst_mikey_payload_kemac_set:
 * @payload: a #GstMIKEYPayload
 * @enc_alg: the #GstMIKEYEncAlg
 * @enc_len: the length of @enc_data
 * @enc_data: the encrypted data
 * @mac_alg: a #GstMIKEYMacAlg
 * @mac: the MAC
 *
 * Set the KEMAC parameters. @payload should point to a #GST_MIKEY_PT_KEMAC
 * payload.
 *
 * Returns: %TRUE on success
 */
gboolean
gst_mikey_payload_kemac_set (GstMIKEYPayload * payload,
    GstMIKEYEncAlg enc_alg, guint16 enc_len, const guint8 * enc_data,
    GstMIKEYMacAlg mac_alg, const guint8 * mac)
{
  GstMIKEYPayloadKEMAC *p = (GstMIKEYPayloadKEMAC *) payload;
  guint mac_len;

  g_return_val_if_fail (payload != NULL, FALSE);
  g_return_val_if_fail (payload->type == GST_MIKEY_PT_KEMAC, FALSE);

  if ((mac_len = get_mac_len (mac_alg)) == -1)
    return FALSE;

  p->enc_alg = enc_alg;
  p->enc_len = enc_len;
  INIT_MEMDUP (p->enc_data, enc_data, enc_len);
  p->mac_alg = mac_alg;
  INIT_MEMDUP (p->mac, mac, mac_len);

  return TRUE;
}

static void
gst_mikey_payload_kemac_clear (GstMIKEYPayloadKEMAC * payload)
{
  FREE_MEMDUP (payload->enc_data);
  FREE_MEMDUP (payload->mac);
}

static GstMIKEYPayloadKEMAC *
gst_mikey_payload_kemac_copy (const GstMIKEYPayloadKEMAC * payload)
{
  GstMIKEYPayloadKEMAC *copy = g_slice_dup (GstMIKEYPayloadKEMAC, payload);
  gst_mikey_payload_kemac_set (&copy->pt, payload->enc_alg, payload->enc_len,
      payload->enc_data, payload->mac_alg, payload->mac);
  return copy;
}

/* Envelope data payload (PKE) */
/**
 * gst_mikey_payload_pke_set:
 * @payload: a #GstMIKEYPayload
 * @C: envelope key cache indicator
 * @data_len: the length of @data
 * @data: the encrypted envelope key
 *
 * Set the PKE values in @payload. @payload must be of type
 * #GST_MIKEY_PT_PKE.
 *
 * Returns: %TRUE on success
 */
gboolean
gst_mikey_payload_pke_set (GstMIKEYPayload * payload, GstMIKEYCacheType C,
    guint16 data_len, const guint8 * data)
{
  GstMIKEYPayloadPKE *p = (GstMIKEYPayloadPKE *) payload;

  g_return_val_if_fail (payload != NULL, FALSE);
  g_return_val_if_fail (payload->type == GST_MIKEY_PT_PKE, FALSE);

  p->C = C;
  p->data_len = data_len;
  INIT_MEMDUP (p->data, data, data_len);

  return TRUE;
}

static void
gst_mikey_payload_pke_clear (GstMIKEYPayloadPKE * payload)
{
  FREE_MEMDUP (payload->data);
}

static GstMIKEYPayloadPKE *
gst_mikey_payload_pke_copy (const GstMIKEYPayloadPKE * payload)
{
  GstMIKEYPayloadPKE *copy = g_slice_dup (GstMIKEYPayloadPKE, payload);
  gst_mikey_payload_pke_set (&copy->pt, payload->C, payload->data_len,
      payload->data);
  return copy;
}

/* DH data payload (DH) */
/* Signature payload (SIGN) */

/* Timestamp payload (T) */
static guint
get_ts_len (GstMIKEYTSType type)
{
  guint len;

  switch (type) {
    case GST_MIKEY_TS_TYPE_NTP_UTC:
    case GST_MIKEY_TS_TYPE_NTP:
      len = 8;
      break;
    case GST_MIKEY_TS_TYPE_COUNTER:
      len = 4;
      break;
    default:
      len = -1;
      break;
  }
  return len;
}

/**
 * gst_mikey_payload_t_set:
 * @payload: a #GstMIKEYPayload
 * @type: the #GstMIKEYTSType
 * @ts_value: the timestamp value
 *
 * Set the timestamp in a #GST_MIKEY_PT_T @payload.
 *
 * Returns: %TRUE on success
 */
gboolean
gst_mikey_payload_t_set (GstMIKEYPayload * payload,
    GstMIKEYTSType type, const guint8 * ts_value)
{
  GstMIKEYPayloadT *p = (GstMIKEYPayloadT *) payload;
  guint ts_len;

  g_return_val_if_fail (payload != NULL, FALSE);
  g_return_val_if_fail (payload->type == GST_MIKEY_PT_T, FALSE);

  if ((ts_len = get_ts_len (type)) == -1)
    return FALSE;

  p->type = type;
  INIT_MEMDUP (p->ts_value, ts_value, ts_len);

  return TRUE;
}

static void
gst_mikey_payload_t_clear (GstMIKEYPayloadT * payload)
{
  FREE_MEMDUP (payload->ts_value);
}

static GstMIKEYPayloadT *
gst_mikey_payload_t_copy (const GstMIKEYPayloadT * payload)
{
  GstMIKEYPayloadT *copy = g_slice_dup (GstMIKEYPayloadT, payload);
  gst_mikey_payload_t_set (&copy->pt, payload->type, payload->ts_value);
  return copy;
}

/* ID payload (ID) */
/* Certificate Payload (CERT) */
/* Cert hash payload (CHASH)*/
/* Ver msg payload (V) */
/* Security Policy payload (SP)*/
static void
param_clear (GstMIKEYPayloadSPParam * param)
{
  FREE_MEMDUP (param->val);
}

/**
 * gst_mikey_payload_sp_set:
 * @payload: a #GstMIKEYPayload
 * @policy: the policy number
 * @proto: a #GstMIKEYSecProto
 *
 * Set the Security Policy parameters for @payload.
 *
 * Returns: %TRUE on success
 */
gboolean
gst_mikey_payload_sp_set (GstMIKEYPayload * payload,
    guint policy, GstMIKEYSecProto proto)
{
  GstMIKEYPayloadSP *p = (GstMIKEYPayloadSP *) payload;

  g_return_val_if_fail (payload != NULL, FALSE);
  g_return_val_if_fail (payload->type == GST_MIKEY_PT_SP, FALSE);

  p->policy = policy;
  p->proto = proto;
  INIT_ARRAY (p->params, GstMIKEYPayloadSPParam, param_clear);

  return TRUE;
}

static void
gst_mikey_payload_sp_clear (GstMIKEYPayloadSP * payload)
{
  FREE_ARRAY (payload->params);
}

static GstMIKEYPayloadSP *
gst_mikey_payload_sp_copy (const GstMIKEYPayloadSP * payload)
{
  guint i, len;
  GstMIKEYPayloadSP *copy = g_slice_dup (GstMIKEYPayloadSP, payload);
  gst_mikey_payload_sp_set (&copy->pt, payload->policy, payload->proto);
  len = payload->params->len;
  for (i = 0; i < len; i++) {
    GstMIKEYPayloadSPParam *param = &g_array_index (payload->params,
        GstMIKEYPayloadSPParam, i);
    gst_mikey_payload_sp_add_param (&copy->pt, param->type, param->len,
        param->val);
  }
  return copy;
}

/**
 * gst_mikey_payload_sp_get_n_params:
 * @payload: a #GstMIKEYPayload
 *
 * Get the number of security policy parameters in a #GST_MIKEY_PT_SP
 * @payload.
 *
 * Returns: the number of parameters in @payload
 */
guint
gst_mikey_payload_sp_get_n_params (const GstMIKEYPayload * payload)
{
  GstMIKEYPayloadSP *p = (GstMIKEYPayloadSP *) payload;

  g_return_val_if_fail (payload != NULL, 0);
  g_return_val_if_fail (payload->type == GST_MIKEY_PT_SP, 0);

  return p->params->len;

}

/**
 * gst_mikey_payload_sp_get_param:
 * @payload: a #GstMIKEYPayload
 * @idx: an index
 *
 * Get the Security Policy parameter in a #GST_MIKEY_PT_SP @payload
 * at @idx.
 *
 * Returns: the #GstMIKEYPayloadSPParam at @idx in @payload
 */
const GstMIKEYPayloadSPParam *
gst_mikey_payload_sp_get_param (const GstMIKEYPayload * payload, guint idx)
{
  GstMIKEYPayloadSP *p = (GstMIKEYPayloadSP *) payload;

  g_return_val_if_fail (payload != NULL, NULL);
  g_return_val_if_fail (payload->type == GST_MIKEY_PT_SP, NULL);

  return &g_array_index (p->params, GstMIKEYPayloadSPParam, idx);
}

/**
 * gst_mikey_payload_sp_remove_param:
 * @payload: a #GstMIKEYPayload
 * @idx: an index
 *
 * Remove the Security Policy parameters from a #GST_MIKEY_PT_SP
 * @payload at @idx.
 *
 * Returns: %TRUE on success
 */
gboolean
gst_mikey_payload_sp_remove_param (GstMIKEYPayload * payload, guint idx)
{
  GstMIKEYPayloadSP *p = (GstMIKEYPayloadSP *) payload;

  g_return_val_if_fail (payload != NULL, FALSE);
  g_return_val_if_fail (payload->type == GST_MIKEY_PT_SP, FALSE);

  g_array_remove_index (p->params, idx);

  return TRUE;
}

/**
 * gst_mikey_payload_sp_add_param:
 * @payload: a #GstMIKEYPayload
 * @type: a type
 * @len: a length
 * @val: @len bytes of data
 *
 * Add a new parameter to the #GST_MIKEY_PT_SP @payload with @type, @len
 * and @val.
 *
 * Returns: %TRUE on success
 */
gboolean
gst_mikey_payload_sp_add_param (GstMIKEYPayload * payload,
    guint8 type, guint8 len, const guint8 * val)
{
  GstMIKEYPayloadSPParam param = { 0 };
  GstMIKEYPayloadSP *p = (GstMIKEYPayloadSP *) payload;

  g_return_val_if_fail (payload != NULL, FALSE);
  g_return_val_if_fail (payload->type == GST_MIKEY_PT_SP, FALSE);

  param.type = type;
  param.len = len;
  INIT_MEMDUP (param.val, val, len);

  g_array_append_val (p->params, param);

  return TRUE;
}

/* RAND payload (RAND) */
/**
 * gst_mikey_payload_rand_set:
 * @payload: a #GstMIKEYPayload
 * @len: the length of @rand
 * @rand: random values
 *
 * Set the random values in a #GST_MIKEY_PT_RAND @payload.
 *
 * Returns: %TRUE on success
 */
gboolean
gst_mikey_payload_rand_set (GstMIKEYPayload * payload, guint8 len,
    const guint8 * rand)
{
  GstMIKEYPayloadRAND *p = (GstMIKEYPayloadRAND *) payload;

  g_return_val_if_fail (payload != NULL, FALSE);
  g_return_val_if_fail (payload->type == GST_MIKEY_PT_RAND, FALSE);

  p->len = len;
  INIT_MEMDUP (p->rand, rand, len);;

  return TRUE;
}

static void
gst_mikey_payload_rand_clear (GstMIKEYPayloadRAND * payload)
{
  FREE_MEMDUP (payload->rand);
}

static GstMIKEYPayloadRAND *
gst_mikey_payload_rand_copy (const GstMIKEYPayloadRAND * payload)
{
  GstMIKEYPayloadRAND *copy = g_slice_dup (GstMIKEYPayloadRAND, payload);
  gst_mikey_payload_rand_set (&copy->pt, payload->len, payload->rand);
  return copy;
}

/* Error payload (ERR) */
/* Key data sub-payload */
/* Key validity data */
/* General Extension Payload */

/**
 * gst_mikey_payload_new:
 * @type: a #GstMIKEYPayloadType
 *
 * Make a new #GstMIKEYPayload with @type.
 *
 * Returns: a new #GstMIKEYPayload or %NULL on failure.
 */
GstMIKEYPayload *
gst_mikey_payload_new (GstMIKEYPayloadType type)
{
  guint len = 0;
  GstMIKEYPayloadClearFunc clear;
  GstMIKEYPayloadCopyFunc copy;
  GstMIKEYPayload *result;

  switch (type) {
    case GST_MIKEY_PT_KEMAC:
      len = sizeof (GstMIKEYPayloadKEMAC);
      clear = (GstMIKEYPayloadClearFunc) gst_mikey_payload_kemac_clear;
      copy = (GstMIKEYPayloadCopyFunc) gst_mikey_payload_kemac_copy;
      break;
    case GST_MIKEY_PT_T:
      len = sizeof (GstMIKEYPayloadT);
      clear = (GstMIKEYPayloadClearFunc) gst_mikey_payload_t_clear;
      copy = (GstMIKEYPayloadCopyFunc) gst_mikey_payload_t_copy;
      break;
    case GST_MIKEY_PT_PKE:
      len = sizeof (GstMIKEYPayloadPKE);
      clear = (GstMIKEYPayloadClearFunc) gst_mikey_payload_pke_clear;
      copy = (GstMIKEYPayloadCopyFunc) gst_mikey_payload_pke_copy;
      break;
    case GST_MIKEY_PT_DH:
    case GST_MIKEY_PT_SIGN:
    case GST_MIKEY_PT_ID:
    case GST_MIKEY_PT_CERT:
    case GST_MIKEY_PT_CHASH:
    case GST_MIKEY_PT_V:
    case GST_MIKEY_PT_SP:
      len = sizeof (GstMIKEYPayloadSP);
      clear = (GstMIKEYPayloadClearFunc) gst_mikey_payload_sp_clear;
      copy = (GstMIKEYPayloadCopyFunc) gst_mikey_payload_sp_copy;
      break;
    case GST_MIKEY_PT_RAND:
      len = sizeof (GstMIKEYPayloadRAND);
      clear = (GstMIKEYPayloadClearFunc) gst_mikey_payload_rand_clear;
      copy = (GstMIKEYPayloadCopyFunc) gst_mikey_payload_rand_copy;
      break;
    case GST_MIKEY_PT_ERR:
    case GST_MIKEY_PT_KEY_DATA:
    case GST_MIKEY_PT_GEN_EXT:
    case GST_MIKEY_PT_LAST:
      break;
  }
  if (len == 0)
    return NULL;

  result = g_slice_alloc0 (len);
  result->type = type;
  result->len = len;
  result->clear_func = clear;
  result->copy_func = copy;

  return result;
}

/**
 * gst_mikey_payload_copy:
 * @payload: a #GstMIKEYPayload
 *
 * Copy @payload.
 *
 * Returns: a new #GstMIKEYPayload that is a copy of @payload
 */
GstMIKEYPayload *
gst_mikey_payload_copy (const GstMIKEYPayload * payload)
{
  g_return_val_if_fail (payload != NULL, NULL);
  g_return_val_if_fail (payload->copy_func != NULL, NULL);

  return payload->copy_func (payload);
}

/**
 * gst_mikey_payload_free:
 * @payload: a #GstMIKEYPayload
 *
 * Free @payload
 *
 * Returns: %TRUE on success
 */
gboolean
gst_mikey_payload_free (GstMIKEYPayload * payload)
{
  g_return_val_if_fail (payload != NULL, FALSE);

  if (payload->clear_func)
    payload->clear_func (payload);
  g_slice_free1 (payload->len, payload);

  return TRUE;
}

static void
payload_destroy (GstMIKEYPayload ** payload)
{
  gst_mikey_payload_free (*payload);
}

/**
 * gst_mikey_message_new:
 *
 * Make a new MIKEY message.
 *
 * Returns: a new #GstMIKEYMessage on success
 */
GstMIKEYMessage *
gst_mikey_message_new (void)
{
  GstMIKEYMessage *result;

  result = g_slice_new0 (GstMIKEYMessage);

  INIT_ARRAY (result->map_info, GstMIKEYMapSRTP, NULL);
  INIT_ARRAY (result->payloads, GstMIKEYPayload *, payload_destroy);

  return result;
}

/**
 * gst_mikey_message_new_from_bytes:
 * @bytes: a #GBytes
 *
 * Make a new #GstMIKEYMessage from @bytes.
 *
 * Returns: a new #GstMIKEYMessage
 */
GstMIKEYMessage *
gst_mikey_message_new_from_bytes (GBytes * bytes)
{
  gconstpointer data;
  gsize size;

  g_return_val_if_fail (bytes != NULL, NULL);

  data = g_bytes_get_data (bytes, &size);
  return gst_mikey_message_new_from_data (data, size);
}

/**
 * gst_mikey_message_free:
 * @msg: a #GstMIKEYMessage
 *
 * Free all resources allocated in @msg.
 */
void
gst_mikey_message_free (GstMIKEYMessage * msg)
{
  g_return_if_fail (msg != NULL);

  FREE_ARRAY (msg->map_info);
  FREE_ARRAY (msg->payloads);

  g_slice_free (GstMIKEYMessage, msg);
}

/**
 * gst_mikey_message_set_info:
 * @msg: a #GstMIKEYMessage
 * @version: a version
 * @type: a #GstMIKEYType
 * @V: verify flag
 * @prf_func: the #GstMIKEYPRFFunc function to use
 * @CSB_id: the Crypto Session Bundle id
 * @map_type: the #GstMIKEYCSIDMapType
 *
 * Set the information in @msg.
 *
 * Returns: %TRUE on success
 */
gboolean
gst_mikey_message_set_info (GstMIKEYMessage * msg, guint8 version,
    GstMIKEYType type, gboolean V, GstMIKEYPRFFunc prf_func, guint32 CSB_id,
    GstMIKEYMapType map_type)
{
  g_return_val_if_fail (msg != NULL, FALSE);

  msg->version = version;
  msg->type = type;
  msg->V = V;
  msg->prf_func = prf_func;
  msg->CSB_id = CSB_id;
  msg->map_type = map_type;

  return TRUE;
}

/**
 * gst_mikey_message_get_n_cs:
 * @msg: a #GstMIKEYMessage
 *
 * Get the number of crypto sessions in @msg.
 *
 * Returns: the number of crypto sessions
 */
guint
gst_mikey_message_get_n_cs (const GstMIKEYMessage * msg)
{
  g_return_val_if_fail (msg != NULL, 0);

  return msg->map_info->len;
}

/**
 * gst_mikey_message_get_cs_srtp:
 * @msg: a #GstMIKEYMessage
 * @idx: an index
 *
 * Get the policy information of @msg at @idx.
 *
 * Returns: a #GstMIKEYMapSRTP
 */
const GstMIKEYMapSRTP *
gst_mikey_message_get_cs_srtp (const GstMIKEYMessage * msg, guint idx)
{
  g_return_val_if_fail (msg != NULL, NULL);
  g_return_val_if_fail (msg->map_type == GST_MIKEY_MAP_TYPE_SRTP, NULL);

  return &g_array_index (msg->map_info, GstMIKEYMapSRTP, idx);
}

/**
 * gst_mikey_message_insert_cs_srtp:
 * @msg: a #GstMIKEYMessage
 * @idx: the index to insert at
 * @map: the map info
 *
 * Insert a Crypto Session map for SRTP in @msg at @idx
 *
 * When @idx is -1, the policy will be appended.
 *
 * Returns: %TRUE on success
 */
gboolean
gst_mikey_message_insert_cs_srtp (GstMIKEYMessage * msg, gint idx,
    const GstMIKEYMapSRTP * map)
{
  g_return_val_if_fail (msg != NULL, FALSE);
  g_return_val_if_fail (msg->map_type == GST_MIKEY_MAP_TYPE_SRTP, FALSE);
  g_return_val_if_fail (map != NULL, FALSE);

  if (idx == -1)
    g_array_append_val (msg->map_info, *map);
  else
    g_array_insert_val (msg->map_info, idx, *map);

  return TRUE;
}

/**
 * gst_mikey_message_replace_cs_srtp:
 * @msg: a #GstMIKEYMessage
 * @idx: the index to insert at
 * @map: the map info
 *
 * Replace a Crypto Session map for SRTP in @msg at @idx with @map.
 *
 * Returns: %TRUE on success
 */
gboolean
gst_mikey_message_replace_cs_srtp (GstMIKEYMessage * msg, gint idx,
    const GstMIKEYMapSRTP * map)
{
  g_return_val_if_fail (msg != NULL, FALSE);
  g_return_val_if_fail (msg->map_type == GST_MIKEY_MAP_TYPE_SRTP, FALSE);
  g_return_val_if_fail (map != NULL, FALSE);

  g_array_index (msg->map_info, GstMIKEYMapSRTP, idx) = *map;

  return TRUE;
}

/**
 * gst_mikey_message_remove_cs_srtp:
 * @msg: a #GstMIKEYMessage
 * @idx: the index to remove
 *
 * Remove the SRTP policy at @idx.
 *
 * Returns: %TRUE on success
 */
gboolean
gst_mikey_message_remove_cs_srtp (GstMIKEYMessage * msg, gint idx)
{
  g_return_val_if_fail (msg != NULL, FALSE);
  g_return_val_if_fail (msg->map_type == GST_MIKEY_MAP_TYPE_SRTP, FALSE);

  g_array_remove_index (msg->map_info, idx);

  return TRUE;
}

/**
 * gst_mikey_message_add_cs_srtp:
 * @msg: a #GstMIKEYMessage
 * @policy: The security policy applied for the stream with @ssrc
 * @ssrc: the SSRC that must be used for the stream
 * @roc: current rollover counter
 *
 * Add a Crypto policy for SRTP to @msg.
 *
 * Returns: %TRUE on success
 */
gboolean
gst_mikey_message_add_cs_srtp (GstMIKEYMessage * msg, guint8 policy,
    guint32 ssrc, guint32 roc)
{
  GstMIKEYMapSRTP val;

  g_return_val_if_fail (msg != NULL, FALSE);
  g_return_val_if_fail (msg->map_type == GST_MIKEY_MAP_TYPE_SRTP, FALSE);

  val.policy = policy;
  val.ssrc = ssrc;
  val.roc = roc;

  return gst_mikey_message_insert_cs_srtp (msg, -1, &val);
}

/* adding/retrieving payloads */
/**
 * gst_mikey_message_get_n_payloads:
 * @msg: a #GstMIKEYMessage
 *
 * Get the number of payloads in @msg.
 *
 * Returns: the number of payloads in @msg
 */
guint
gst_mikey_message_get_n_payloads (const GstMIKEYMessage * msg)
{
  g_return_val_if_fail (msg != NULL, 0);

  return msg->payloads->len;
}

/**
 * gst_mikey_message_get_payload:
 * @msg: a #GstMIKEYMessage
 * @idx: an index
 *
 * Get the #GstMIKEYPayload at @idx in @msg
 *
 * Returns: the #GstMIKEYPayload at @idx
 */
const GstMIKEYPayload *
gst_mikey_message_get_payload (const GstMIKEYMessage * msg, guint idx)
{
  g_return_val_if_fail (msg != NULL, NULL);

  if (idx >= msg->payloads->len)
    return NULL;

  return g_array_index (msg->payloads, GstMIKEYPayload *, idx);
}

/**
 * gst_mikey_message_find_payload:
 * @msg: a #GstMIKEYMessage
 * @type: a #GstMIKEYPayloadType
 * @nth: payload to find
 *
 * Find the @nth occurence of the payload with @type in @msg.
 *
 * Returns: the @nth #GstMIKEYPayload of @type.
 */
const GstMIKEYPayload *
gst_mikey_message_find_payload (const GstMIKEYMessage * msg,
    GstMIKEYPayloadType type, guint idx)
{
  guint i, len, count;

  count = 0;
  len = msg->payloads->len;
  for (i = 0; i < len; i++) {
    GstMIKEYPayload *payload =
        g_array_index (msg->payloads, GstMIKEYPayload *, i);

    if (payload->type == type) {
      if (count == idx)
        return payload;

      count++;
    }
  }
  return NULL;
}

/**
 * gst_mikey_message_remove_payload:
 * @msg: a #GstMIKEYMessage
 * @idx: an index
 *
 * Remove the payload in @msg at @idx
 *
 * Returns: %TRUE on success
 */
gboolean
gst_mikey_message_remove_payload (GstMIKEYMessage * msg, guint idx)
{
  g_return_val_if_fail (msg != NULL, FALSE);

  g_array_remove_index (msg->payloads, idx);

  return TRUE;
}

/**
 * gst_mikey_message_insert_payload:
 * @msg: a #GstMIKEYMessage
 * @idx: an index
 * @payload: a #GstMIKEYPayload
 *
 * Insert the @payload at index @idx in @msg. If @idx is -1, the payload
 * will be appended to @msg.
 *
 * Returns: %TRUE on success
 */
gboolean
gst_mikey_message_insert_payload (GstMIKEYMessage * msg, guint idx,
    GstMIKEYPayload * payload)
{
  g_return_val_if_fail (msg != NULL, FALSE);
  g_return_val_if_fail (payload != NULL, FALSE);

  if (idx == -1)
    g_array_append_val (msg->payloads, payload);
  else
    g_array_insert_val (msg->payloads, idx, payload);

  return TRUE;
}

/**
 * gst_mikey_message_add_payload:
 * @msg: a #GstMIKEYMessage
 * @payload: a #GstMIKEYPayload
 *
 * Add a new payload to @msg.
 *
 * Returns: %TRUE on success
 */
gboolean
gst_mikey_message_add_payload (GstMIKEYMessage * msg, GstMIKEYPayload * payload)
{
  return gst_mikey_message_insert_payload (msg, -1, payload);
}

/**
 * gst_mikey_message_replace_payload:
 * @msg: a #GstMIKEYMessage
 * @idx: an index
 * @payload: a #GstMIKEYPayload
 *
 * Replace the payload at @idx in @msg with @payload.
 *
 * Returns: %TRUE on success
 */
gboolean
gst_mikey_message_replace_payload (GstMIKEYMessage * msg, guint idx,
    GstMIKEYPayload * payload)
{
  GstMIKEYPayload *p;

  g_return_val_if_fail (msg != NULL, FALSE);
  g_return_val_if_fail (payload != NULL, FALSE);

  p = g_array_index (msg->payloads, GstMIKEYPayload *, idx);
  gst_mikey_payload_free (p);
  g_array_index (msg->payloads, GstMIKEYPayload *, idx) = p;

  return TRUE;
}

/**
 * gst_mikey_message_add_kemac:
 * @msg: a #GstMIKEYMessage
 * @enc_alg: the encryption algorithm used to encrypt @enc_data
 * @enc_len: the length of @enc_data
 * @enc_data: the encrypted key sub-payloads
 * @mac_alg: specifies the authentication algorithm used
 * @mac: the message authentication code of the entire message
 *
 * Inserts a new KEMAC payload to @msg with the given parameters.
 *
 * Returns: %TRUE on success
 */
gboolean
gst_mikey_message_add_kemac (GstMIKEYMessage * msg, GstMIKEYEncAlg enc_alg,
    guint16 enc_len, const guint8 * enc_data, GstMIKEYMacAlg mac_alg,
    const guint8 * mac)
{
  GstMIKEYPayload *p;

  g_return_val_if_fail (msg != NULL, FALSE);

  p = gst_mikey_payload_new (GST_MIKEY_PT_KEMAC);
  if (!gst_mikey_payload_kemac_set (p,
          enc_alg, enc_len, enc_data, mac_alg, mac))
    return FALSE;

  return gst_mikey_message_insert_payload (msg, -1, p);
}

/**
 * gst_mikey_message_add_pke
 * @msg: a #GstMIKEYMessage
 * @C: envelope key cache indicator
 * @data_len: the length of @data
 * @data: the encrypted envelope key
 *
 * Add a new PKE payload to @msg with the given parameters.
 *
 * Returns: %TRUE on success
 */
gboolean
gst_mikey_message_add_pke (GstMIKEYMessage * msg, GstMIKEYCacheType C,
    guint16 data_len, const guint8 * data)
{
  GstMIKEYPayload *p;

  g_return_val_if_fail (msg != NULL, FALSE);

  p = gst_mikey_payload_new (GST_MIKEY_PT_PKE);
  if (!gst_mikey_payload_pke_set (p, C, data_len, data))
    return FALSE;

  return gst_mikey_message_insert_payload (msg, -1, p);
}

/**
 * gst_mikey_message_add_t
 * @msg: a #GstMIKEYMessage
 * @type: specifies the timestamp type used
 * @ts_value: The timestamp value of the specified @type
 *
 * Add a new T payload to @msg with the given parameters.
 *
 * Returns: %TRUE on success
 */
gboolean
gst_mikey_message_add_t (GstMIKEYMessage * msg, GstMIKEYTSType type,
    const guint8 * ts_value)
{
  GstMIKEYPayload *p;

  g_return_val_if_fail (msg != NULL, FALSE);

  p = gst_mikey_payload_new (GST_MIKEY_PT_T);
  if (!gst_mikey_payload_t_set (p, type, ts_value))
    return FALSE;

  return gst_mikey_message_insert_payload (msg, -1, p);
}

/**
 * gst_mikey_message_add_t_now_ntp_utc:
 * @msg: a #GstMIKEYMessage
 *
 * Add a new T payload to @msg that contains the current time
 * in NTP-UTC format.
 *
 * Returns: %TRUE on success
 */
gboolean
gst_mikey_message_add_t_now_ntp_utc (GstMIKEYMessage * msg)
{
  gint64 now;
  guint64 ntptime;
  guint8 bytes[8];

  now = g_get_real_time ();

  /* convert clock time to NTP time. upper 32 bits should contain the seconds
   * and the lower 32 bits, the fractions of a second. */
  ntptime = gst_util_uint64_scale (now, (G_GINT64_CONSTANT (1) << 32),
      GST_USECOND);
  /* conversion from UNIX timestamp (seconds since 1970) to NTP (seconds
   * since 1900). */
  ntptime += (G_GUINT64_CONSTANT (2208988800) << 32);
  GST_WRITE_UINT64_BE (bytes, ntptime);

  return gst_mikey_message_add_t (msg, GST_MIKEY_TS_TYPE_NTP_UTC, bytes);
}

/**
 * gst_mikey_message_add_rand:
 * @msg: a #GstMIKEYMessage
 * @len: the length of @rand
 * @rand: random data
 *
 * Add a new RAND payload to @msg with the given parameters.
 *
 * Returns: %TRUE on success
 */
gboolean
gst_mikey_message_add_rand (GstMIKEYMessage * msg, guint8 len,
    const guint8 * rand)
{
  GstMIKEYPayload *p;

  g_return_val_if_fail (msg != NULL, FALSE);
  g_return_val_if_fail (len != 0 && rand != NULL, FALSE);

  p = gst_mikey_payload_new (GST_MIKEY_PT_RAND);
  if (!gst_mikey_payload_rand_set (p, len, rand))
    return FALSE;

  return gst_mikey_message_insert_payload (msg, -1, p);
}

/**
 * gst_mikey_message_add_rand_len:
 * @msg: a #GstMIKEYMessage
 * @len: length
 *
 * Add a new RAND payload to @msg with @len random bytes.
 *
 * Returns: %TRUE on success
 */
gboolean
gst_mikey_message_add_rand_len (GstMIKEYMessage * msg, guint8 len)
{
  GstMIKEYPayloadRAND *p;
  guint i;

  p = (GstMIKEYPayloadRAND *) gst_mikey_payload_new (GST_MIKEY_PT_RAND);
  p->len = len;
  p->rand = g_malloc (len);
  for (i = 0; i < len; i++)
    p->rand[i] = g_random_int_range (0, 256);

  return gst_mikey_message_add_payload (msg, &p->pt);
}

/**
 * gst_mikey_message_to_bytes:
 * @msg: a #GstMIKEYMessage
 *
 * Convert @msg to a #GBytes.
 *
 * Returns: a new #GBytes for @msg.
 */
GBytes *
gst_mikey_message_to_bytes (GstMIKEYMessage * msg)
{
  GByteArray *arr = NULL;
  guint8 *data;
  GstMIKEYPayload *next_payload;
  guint i, n_cs, n_payloads;
#define ENSURE_SIZE(n)                          \
G_STMT_START {                                  \
  guint offset = data - arr->data;              \
  g_byte_array_set_size (arr, offset + n);      \
  data = arr->data + offset;                    \
} G_STMT_END
  arr = g_byte_array_new ();
  data = arr->data;

  n_payloads = msg->payloads->len;
  if (n_payloads == 0)
    next_payload = 0;
  else
    next_payload = g_array_index (msg->payloads, GstMIKEYPayload *, 0);

  n_cs = msg->map_info->len;
  /*                      1                   2                   3
   *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * !  version      !  data type    ! next payload  !V! PRF func    !
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * !                         CSB ID                                !
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * ! #CS           ! CS ID map type! CS ID map info                ~
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   */
  ENSURE_SIZE (10 + 9 * n_cs);
  data[0] = msg->version;
  data[1] = msg->type;
  data[2] = next_payload ? next_payload->type : GST_MIKEY_PT_LAST;
  data[3] = (msg->V ? 0x80 : 0x00) | (msg->prf_func & 0x7f);
  GST_WRITE_UINT32_BE (&data[4], msg->CSB_id);
  data[8] = n_cs;
  data[9] = msg->map_type;
  data += 10;

  for (i = 0; i < n_cs; i++) {
    GstMIKEYMapSRTP *info = &g_array_index (msg->map_info, GstMIKEYMapSRTP, i);
    /*                      1                   2                   3
     *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * ! Policy_no_1   ! SSRC_1                                        !
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * ! SSRC_1 (cont) ! ROC_1                                         !
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * ! ROC_1 (cont)  ! Policy_no_2   ! SSRC_2                        !
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * ! SSRC_2 (cont)                 ! ROC_2                         !
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * ! ROC_2 (cont)                  !                               :
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ ...
     */
    data[0] = info->policy;
    GST_WRITE_UINT32_BE (&data[1], info->ssrc);
    GST_WRITE_UINT32_BE (&data[5], info->roc);
    data += 9;
  }
  for (i = 0; i < n_payloads; i++) {
    GstMIKEYPayload *payload =
        g_array_index (msg->payloads, GstMIKEYPayload *, i);

    if (i + 1 < n_payloads)
      next_payload = g_array_index (msg->payloads, GstMIKEYPayload *, i + 1);
    else
      next_payload = NULL;

    switch (payload->type) {
      case GST_MIKEY_PT_KEMAC:
      {
        GstMIKEYPayloadKEMAC *p = (GstMIKEYPayloadKEMAC *) payload;
        guint mac_len;

        if ((mac_len = get_mac_len (p->mac_alg)) == -1)
          break;

        /*                  1                   2                   3
         *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         * ! Next payload  ! Encr alg      ! Encr data len                 !
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         * !                        Encr data                              ~
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         * ! Mac alg       !        MAC                                    ~
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         */
        ENSURE_SIZE (5 + p->enc_len + mac_len);
        data[0] = next_payload ? next_payload->type : GST_MIKEY_PT_LAST;
        data[1] = p->enc_alg;
        GST_WRITE_UINT16_BE (&data[2], p->enc_len);
        memcpy (&data[4], p->enc_data, p->enc_len);
        data += p->enc_len;
        data[4] = p->mac_alg;
        memcpy (&data[5], p->mac, mac_len);
        data += 5 + mac_len;
        break;
      }
      case GST_MIKEY_PT_T:
      {
        GstMIKEYPayloadT *p = (GstMIKEYPayloadT *) payload;
        guint ts_len;

        if ((ts_len = get_ts_len (p->type)) == -1)
          break;

        /*                      1                   2                   3
         *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         * ! Next Payload  !   TS type     ! TS value                      ~
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         */
        ENSURE_SIZE (2 + ts_len);
        data[0] = next_payload ? next_payload->type : GST_MIKEY_PT_LAST;
        data[1] = p->type;
        memcpy (&data[2], p->ts_value, ts_len);
        data += 2 + ts_len;
        break;
      }
      case GST_MIKEY_PT_PKE:
      {
        guint16 clen;
        GstMIKEYPayloadPKE *p = (GstMIKEYPayloadPKE *) payload;
        /*                      1                   2                   3
         *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         * ! Next Payload  ! C ! Data len                  ! Data          ~
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         */
        ENSURE_SIZE (3 + p->data_len);
        data[0] = next_payload ? next_payload->type : GST_MIKEY_PT_LAST;
        clen = (p->C << 14) || (p->data_len & 0x3fff);
        GST_WRITE_UINT16_BE (&data[1], clen);
        memcpy (&data[3], p->data, p->data_len);
        data += 3 + p->data_len;
        break;
      }
      case GST_MIKEY_PT_DH:
      case GST_MIKEY_PT_SIGN:
      case GST_MIKEY_PT_ID:
      case GST_MIKEY_PT_CERT:
      case GST_MIKEY_PT_CHASH:
      case GST_MIKEY_PT_V:
        break;
      case GST_MIKEY_PT_SP:
      {
        GstMIKEYPayloadSP *p = (GstMIKEYPayloadSP *) payload;
        guint len, plen, i;

        plen = 0;
        len = p->params->len;
        for (i = 0; i < len; i++) {
          GstMIKEYPayloadSPParam *param = &g_array_index (p->params,
              GstMIKEYPayloadSPParam, i);
          plen += 2 + param->len;
        }
        /*                      1                   2                   3
         *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         * ! Next payload  ! Policy no     ! Prot type     ! Policy param  ~
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         * ~ length (cont) ! Policy param                                  ~
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         */
        ENSURE_SIZE (5 + plen);
        data[0] = next_payload ? next_payload->type : GST_MIKEY_PT_LAST;
        data[1] = p->policy;
        data[2] = p->proto;
        GST_WRITE_UINT16_BE (&data[3], plen);
        data += 5;
        for (i = 0; i < len; i++) {
          GstMIKEYPayloadSPParam *param = &g_array_index (p->params,
              GstMIKEYPayloadSPParam, i);
          /*                     1                   2                   3
           * 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
           * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
           * ! Type          ! Length        ! Value                         ~
           * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
           */
          data[0] = param->type;
          data[1] = param->len;
          memcpy (&data[2], param->val, param->len);
          data += 2 + param->len;
        }
        break;
      }
      case GST_MIKEY_PT_RAND:
      {
        GstMIKEYPayloadRAND *p = (GstMIKEYPayloadRAND *) payload;
        /*                      1                   2                   3
         *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         * ! Next payload  ! RAND len      ! RAND                          ~
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         */
        ENSURE_SIZE (2 + p->len);
        data[0] = next_payload ? next_payload->type : GST_MIKEY_PT_LAST;
        data[1] = p->len;
        memcpy (&data[2], p->rand, p->len);
        data += 2 + p->len;
        break;
      }
      case GST_MIKEY_PT_ERR:
      case GST_MIKEY_PT_KEY_DATA:
      case GST_MIKEY_PT_GEN_EXT:
      case GST_MIKEY_PT_LAST:
        break;
    }
  }
#undef ENSURE_SIZE

  return g_byte_array_free_to_bytes (arr);
}

/**
 * gst_mikey_message_new_from_data:
 * @data: bytes to read
 * @size: length of @data
 *
 * Parse @size bytes from @data into a #GstMIKEYMessage
 *
 * Returns: a #GstMIKEYMessage on success or %NULL when parsing failed
 */
GstMIKEYMessage *
gst_mikey_message_new_from_data (gconstpointer data, gsize size)
{
  GstMIKEYMessage *msg;
#define CHECK_SIZE(n) if (size < (n)) goto short_data;
#define ADVANCE(n) (d += (n), size -= (n));
  guint n_cs, i;
  const guint8 *d = data;
  guint8 next_payload;

  g_return_val_if_fail (data != NULL, NULL);

  msg = gst_mikey_message_new ();
  /*                      1                   2                   3
   *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * !  version      !  data type    ! next payload  !V! PRF func    !
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * !                         CSB ID                                !
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * ! #CS           ! CS ID map type! CS ID map info                ~
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   */
  CHECK_SIZE (10);
  msg->version = d[0];
  if (msg->version != GST_MIKEY_VERSION)
    goto unknown_version;

  msg->type = d[1];
  next_payload = d[2];
  msg->V = d[3] & 0x80 ? TRUE : FALSE;
  msg->prf_func = d[3] & 0x7f;
  msg->CSB_id = GST_READ_UINT32_BE (&d[4]);
  n_cs = d[8];
  msg->map_type = d[9];
  ADVANCE (10);

  CHECK_SIZE (n_cs * 9);
  for (i = 0; i < n_cs; i++) {
    GstMIKEYMapSRTP map;

    /*                      1                   2                   3
     *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * ! Policy_no_1   ! SSRC_1                                        !
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * ! SSRC_1 (cont) ! ROC_1                                         !
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * ! ROC_1 (cont)  ! Policy_no_2   ! SSRC_2                        ~
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     */
    map.policy = d[0];
    map.ssrc = GST_READ_UINT32_BE (&d[1]);
    map.roc = GST_READ_UINT32_BE (&d[5]);
    gst_mikey_message_insert_cs_srtp (msg, -1, &map);
    ADVANCE (9);
  }

  while (next_payload != GST_MIKEY_PT_LAST) {
    switch (next_payload) {
      case GST_MIKEY_PT_KEMAC:
      {
        guint mac_len;
        GstMIKEYEncAlg enc_alg;
        guint16 enc_len;
        const guint8 *enc_data;
        GstMIKEYMacAlg mac_alg;
        const guint8 *mac;
        /*                  1                   2                   3
         *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         * ! Next payload  ! Encr alg      ! Encr data len                 !
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         * !                        Encr data                              ~
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         * ! Mac alg       !        MAC                                    ~
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         */
        CHECK_SIZE (5);
        next_payload = d[0];
        enc_alg = d[1];
        enc_len = GST_READ_UINT16_BE (&d[2]);
        CHECK_SIZE (5 + enc_len);
        enc_data = &d[4];
        ADVANCE (enc_len);
        mac_alg = d[4];
        if ((mac_len = get_mac_len (mac_alg)) == -1)
          goto invalid_data;
        CHECK_SIZE (5 + mac_len);
        mac = &d[5];
        ADVANCE (5 + mac_len);

        gst_mikey_message_add_kemac (msg, enc_alg, enc_len, enc_data,
            mac_alg, mac);
        break;
      }
      case GST_MIKEY_PT_T:
      {
        GstMIKEYTSType type;
        guint ts_len;
        const guint8 *ts_value;
        /*                      1                   2                   3
         *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         * ! Next Payload  !   TS type     ! TS value                      ~
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         */
        CHECK_SIZE (2);
        next_payload = d[0];
        type = d[1];
        if ((ts_len = get_ts_len (type)) == -1)
          goto invalid_data;
        CHECK_SIZE (2 + ts_len);
        ts_value = &d[2];
        ADVANCE (2 + ts_len);

        gst_mikey_message_add_t (msg, type, ts_value);
        break;
      }
      case GST_MIKEY_PT_PKE:
      {
        guint8 C;
        guint16 clen, data_len;
        const guint8 *data;
        /*                      1                   2                   3
         *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         * ! Next Payload  ! C ! Data len                  ! Data          ~
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         */
        CHECK_SIZE (3);
        next_payload = d[0];
        clen = GST_READ_UINT16_BE (&d[1]);
        C = clen >> 14;
        data_len = clen & 0x3fff;
        CHECK_SIZE (3 + data_len);
        data = &d[3];
        ADVANCE (3 + data_len);

        gst_mikey_message_add_pke (msg, C, data_len, data);
        break;
      }
      case GST_MIKEY_PT_DH:
      case GST_MIKEY_PT_SIGN:
      case GST_MIKEY_PT_ID:
      case GST_MIKEY_PT_CERT:
      case GST_MIKEY_PT_CHASH:
      case GST_MIKEY_PT_V:
        break;
      case GST_MIKEY_PT_SP:
      {
        GstMIKEYPayload *p;
        guint8 policy;
        GstMIKEYSecProto proto;
        guint16 plen;
        /*                      1                   2                   3
         *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         * ! Next payload  ! Policy no     ! Prot type     ! Policy param  ~
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         * ~ length (cont) ! Policy param                                  ~
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         */
        CHECK_SIZE (5);
        next_payload = d[0];
        policy = d[1];
        proto = d[2];
        plen = GST_READ_UINT16_BE (&d[3]);
        ADVANCE (5);

        p = gst_mikey_payload_new (GST_MIKEY_PT_SP);
        gst_mikey_payload_sp_set (p, policy, proto);

        CHECK_SIZE (plen);
        while (plen) {
          guint8 type, len;

          CHECK_SIZE (2);
          /*                     1                   2                   3
           * 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
           * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
           * ! Type          ! Length        ! Value                         ~
           * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
           */
          type = d[0];
          len = d[1];
          CHECK_SIZE (2 + len);
          gst_mikey_payload_sp_add_param (p, type, len, &d[2]);
          ADVANCE (2 + len);
          plen -= 2 + len;
        }
        gst_mikey_message_add_payload (msg, p);
        break;
      }
      case GST_MIKEY_PT_RAND:
      {
        guint8 len;
        const guint8 *rand;
        /*                      1                   2                   3
         *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         * ! Next payload  ! RAND len      ! RAND                          ~
         * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
         */
        CHECK_SIZE (2);
        next_payload = d[0];
        len = d[1];
        CHECK_SIZE (2 + len);
        rand = &d[2];
        ADVANCE (2 + len);

        gst_mikey_message_add_rand (msg, len, rand);
        break;
      }
      case GST_MIKEY_PT_ERR:
      case GST_MIKEY_PT_KEY_DATA:
      case GST_MIKEY_PT_GEN_EXT:
      case GST_MIKEY_PT_LAST:
        break;
    }
  }

  return msg;

  /* ERRORS */
short_data:
  {
    GST_DEBUG ("not enough data");
    gst_mikey_message_free (msg);
    return NULL;
  }
unknown_version:
  {
    GST_DEBUG ("unknown version");
    gst_mikey_message_free (msg);
    return NULL;
  }
invalid_data:
  {
    GST_DEBUG ("invalid data");
    gst_mikey_message_free (msg);
    return NULL;
  }
}
