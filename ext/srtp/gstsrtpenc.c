/*
 * GStreamer - GStreamer SRTP encoder
 *
 * Copyright 2009-2011 Collabora Ltd.
 *  @author: Gabriel Millaire <gabriel.millaire@collabora.com>
 *  @author: Olivier Crete <olivier.crete@collabora.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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

/**
 * SECTION:gst-plugin-bad-plugins-srtpenc
 * @see_also: srtpdec
 *
 * gstrtpenc acts as an encoder that adds security to RTP and RTCP
 * packets in the form of encryption and authentication. It outs SRTP
 * and SRTCP.
 *
 * An application can request multiple RTP and RTCP pads to protect,
 * but every sink pad requested must receive packets from the same
 * source (identical SSRC). If a packet received contains a different
 * SSRC, a warning is emited and the valid SSRC is forced on the packet.
 *
 * This element uses libsrtp library. When receiving the first packet,
 * the library is initialized with a new stream (based on the SSRC). It
 * uses the default RTP and RTCP encryption and authentication mechanisms,
 * unless the user has set the relevant properties first. It also uses
 * a master key that MUST be set by property (key) at the beginning. The
 * master key must be of a maximum length of 30 characters. The
 * encryption and authentication mecanisms available are :
 *
 * Encryption (properties rtp-cipher and rtcp-cipher)
 * - AES_128_ICM (default, maximum security)
 * - STRONGHOLD_CIPHER (same as AES_128_ICM)
 * - NULL
 *
 * Authentication (properties rtp-auth and rtcp-auth)
 * - HMAC_SHA1 (default, maximum protection)
 * - STRONGHOLD_AUTH (same as HMAC_SHA1)
 * - NULL
 *
 * Note that for SRTP protection, authentication is mandatory (non-null)
 * if encryption is used (non-null).
 *
 * When requested to create a sink pad, a linked source pad is created.
 * Each packet received is first analysed (checked for valid SSRC) then
 * its buffer is protected with libsrtp, then pushed on the source pad.
 * If protection failed or the stream could not be created, the buffer
 * is dropped and a warning is emitted. The packets pushed on the source
 * pad are of type 'application/x-srtp' or 'application/x-srtcp'.
 *
 * When the maximum usage of the master key is reached, a soft-limit
 * signal is sent to the user. The user must then set a new master key
 * by property. If the hard limit is reached, a flag is set and every
 * subsequent packet is dropped, until a new key is set and the stream
 * has been updated.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/gst.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>
#include <string.h>

#include "gstsrtpenc.h"

#include "gstsrtp.h"
#include "gstsrtp-enumtypes.h"

GST_DEBUG_CATEGORY_STATIC (gst_srtp_enc_debug);
#define GST_CAT_DEFAULT gst_srtp_enc_debug

/* Properties default values */
#define DEFAULT_MASTER_KEY      NULL
#define DEFAULT_RTP_CIPHER      GST_SRTP_CIPHER_AES_128_ICM
#define DEFAULT_RTP_AUTH        GST_SRTP_AUTH_HMAC_SHA1_80
#define DEFAULT_RTCP_CIPHER     DEFAULT_RTP_CIPHER
#define DEFAULT_RTCP_AUTH       DEFAULT_RTP_AUTH
#define DEFAULT_RANDOM_KEY      FALSE

#define HAS_CRYPTO(filter) (filter->rtp_cipher != GST_SRTP_CIPHER_NULL || \
      filter->rtcp_cipher != GST_SRTP_CIPHER_NULL ||                      \
      filter->rtp_auth != GST_SRTP_AUTH_NULL ||                           \
      filter->rtcp_auth != GST_SRTP_AUTH_NULL)

/* Filter signals and args */
enum
{
  SIGNAL_SOFT_LIMIT,
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_MKEY,
  PROP_RTP_CIPHER,
  PROP_RTP_AUTH,
  PROP_RTCP_CIPHER,
  PROP_RTCP_AUTH,
  PROP_RANDOM_KEY
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate rtp_sink_template =
GST_STATIC_PAD_TEMPLATE ("rtp_sink_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static GstStaticPadTemplate rtp_src_template =
GST_STATIC_PAD_TEMPLATE ("rtp_src_%d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("application/x-srtp")
    );

static GstStaticPadTemplate rtcp_sink_template =
GST_STATIC_PAD_TEMPLATE ("rtcp_sink_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rtcp")
    );

static GstStaticPadTemplate rtcp_src_template =
GST_STATIC_PAD_TEMPLATE ("rtcp_src_%d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("application/x-srtcp")
    );

G_DEFINE_TYPE (GstSrtpEnc, gst_srtp_enc, GST_TYPE_ELEMENT);

static guint gst_srtp_enc_signals[LAST_SIGNAL] = { 0 };

static void gst_srtp_enc_dispose (GObject * object);

static void gst_srtp_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_srtp_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_srtp_enc_sink_query_rtp (GstPad * pad, GstObject * parent,
    GstQuery * query);
static gboolean gst_srtp_enc_sink_query_rtcp (GstPad * pad, GstObject * parent,
    GstQuery * query);

static GstIterator *gst_srtp_enc_iterate_internal_links_rtp (GstPad * pad,
    GstObject * parent);
static GstIterator *gst_srtp_enc_iterate_internal_links_rtcp (GstPad * pad,
    GstObject * parent);

static GstFlowReturn gst_srtp_enc_chain_rtp (GstPad * pad, GstObject * parent,
    GstBuffer * buf);
static GstFlowReturn gst_srtp_enc_chain_rtcp (GstPad * pad, GstObject * parent,
    GstBuffer * buf);

static gboolean gst_srtp_enc_sink_event_rtp (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_srtp_enc_sink_event_rtcp (GstPad * pad, GstObject * parent,
    GstEvent * event);

static GstStateChangeReturn gst_srtp_enc_change_state (GstElement * element,
    GstStateChange transition);

static GstPad *gst_srtp_enc_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps);

static void gst_srtp_enc_release_pad (GstElement * element, GstPad * pad);

struct GstSrtpEncPads
{
  guint ssrc;

  GstPad *sinkpad;
  GstPad *srcpad;
};


/* initialize the srtpenc's class
 */
static void
gst_srtp_enc_class_init (GstSrtpEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&rtp_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&rtp_sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&rtcp_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&rtcp_sink_template));

  gst_element_class_set_static_metadata (gstelement_class, "SRTP encoder",
      "Filter/Network/SRTP",
      "A SRTP and SRTCP encoder",
      "Gabriel Millaire <millaire.gabriel@collabora.com>");


  /* Install callbacks */
  gobject_class->set_property = gst_srtp_enc_set_property;
  gobject_class->get_property = gst_srtp_enc_get_property;
  gobject_class->dispose = gst_srtp_enc_dispose;
  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_srtp_enc_request_new_pad);
  gstelement_class->release_pad = GST_DEBUG_FUNCPTR (gst_srtp_enc_release_pad);
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_srtp_enc_change_state);

  /* Install properties */
  g_object_class_install_property (gobject_class, PROP_MKEY,
      g_param_spec_boxed ("key", "Key", "Master key (of "
          G_STRINGIFY (SRTP_MASTER_KEY_LEN) " bytes)",
          GST_TYPE_BUFFER, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject_class, PROP_RTP_CIPHER,
      g_param_spec_enum ("rtp-cipher", "RTP Cipher", "RTP Cipher",
          GST_TYPE_SRTP_CIPHER_TYPE, DEFAULT_RTP_CIPHER,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_RTP_AUTH,
      g_param_spec_enum ("rtp-auth", "RTP Authentication",
          "RTP Authentication", GST_TYPE_SRTP_AUTH_TYPE, DEFAULT_RTP_AUTH,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_RTCP_CIPHER,
      g_param_spec_enum ("rtcp-cipher", "RTCP Cipher",
          "RTCP Cipher", GST_TYPE_SRTP_CIPHER_TYPE, DEFAULT_RTCP_CIPHER,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_RTCP_AUTH,
      g_param_spec_enum ("rtcp-auth", "RTCP Authentication",
          "RTCP Authentication", GST_TYPE_SRTP_AUTH_TYPE, DEFAULT_RTCP_AUTH,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_RANDOM_KEY,
      g_param_spec_boolean ("random-key", "Generate random key",
          "Generate a random key if TRUE",
          DEFAULT_RANDOM_KEY, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstSrtpEnc::soft-limit:
   * @gstsrtpenc: the element on which the signal is emitted
   * @ssrc: The unique SSRC of the stream
   *
   * Signal emited when the stream with @ssrc has reached the soft
   * limit of utilisation of it's master encryption key. User should
   * provide a new key by setting the #GstSrtpEnc:key property.
   */
  gst_srtp_enc_signals[SIGNAL_SOFT_LIMIT] =
      g_signal_new ("soft-limit", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);
}


/* initialize the new element
 */
static void
gst_srtp_enc_init (GstSrtpEnc * filter)
{
  filter->key_changed = TRUE;
  filter->first_session = TRUE;
  filter->key = DEFAULT_MASTER_KEY;
  filter->rtp_cipher = DEFAULT_RTP_CIPHER;
  filter->rtp_auth = DEFAULT_RTP_AUTH;
  filter->rtcp_cipher = DEFAULT_RTCP_CIPHER;
  filter->rtcp_auth = DEFAULT_RTCP_AUTH;

  filter->ssrcs_set = g_hash_table_new (g_direct_hash, g_direct_equal);
}


/* Create stream
 */
static gboolean
check_new_stream_locked (GstSrtpEnc * filter, guint32 ssrc)
{
  err_status_t ret;
  srtp_policy_t policy;
  GstMapInfo map;
  guchar tmp[1];

  memset (&policy, 0, sizeof (srtp_policy_t));

  /* check if we already have that stream */
  if (g_hash_table_lookup (filter->ssrcs_set, GUINT_TO_POINTER (ssrc)))
    return TRUE;

  GST_OBJECT_LOCK (filter);

  if (HAS_CRYPTO (filter)) {
    if (filter->key == NULL) {
      GST_OBJECT_UNLOCK (filter);
      GST_ELEMENT_ERROR (filter, LIBRARY, SETTINGS,
          ("Cipher is not NULL, key must be set"),
          ("Cipher is not NULL, key must be set"));
      return FALSE;
    }
    if (gst_buffer_get_size (filter->key) != SRTP_MASTER_KEY_LEN) {
      GST_OBJECT_UNLOCK (filter);
      GST_ELEMENT_ERROR (filter, LIBRARY, SETTINGS,
          ("Master key size is wrong"),
          ("Expected master key of %d bytes, but received %" G_GSIZE_FORMAT
              " bytes", SRTP_MASTER_KEY_LEN,
              gst_buffer_get_size (filter->key)));
      return FALSE;
    }
  }

  GST_DEBUG_OBJECT (filter, "Setting RTP/RTCP policy to %d / %d",
      filter->rtp_cipher, filter->rtcp_cipher);
  set_crypto_policy_cipher_auth (filter->rtp_cipher, filter->rtp_auth,
      &policy.rtp);
  set_crypto_policy_cipher_auth (filter->rtcp_cipher, filter->rtcp_auth,
      &policy.rtcp);

  if (HAS_CRYPTO (filter)) {
    gst_buffer_map (filter->key, &map, GST_MAP_READ);
    policy.key = (guchar *) map.data;
  } else {
    policy.key = tmp;
  }

  policy.ssrc.value = ssrc;
  policy.ssrc.type = ssrc_specific;
  policy.next = NULL;

  /* If it is the first stream, create the session
   * If not, add the stream to the session
   */
  if (filter->first_session)
    ret = srtp_create (&filter->session, &policy);
  else
    ret = srtp_add_stream (filter->session, &policy);

  if (HAS_CRYPTO (filter))
    gst_buffer_unmap (filter->key, &map);

  g_hash_table_insert (filter->ssrcs_set, GUINT_TO_POINTER (ssrc),
      GUINT_TO_POINTER (1));

  GST_OBJECT_UNLOCK (filter);

  return ret == err_status_ok;
}

/* Release ressources and set default values
 */
static void
gst_srtp_enc_reset (GstSrtpEnc * filter)
{
  GST_OBJECT_LOCK (filter);

  if (!filter->first_session)
    srtp_dealloc (filter->session);

  filter->first_session = TRUE;
  filter->key_changed = FALSE;

  g_hash_table_remove_all (filter->ssrcs_set);

  GST_OBJECT_UNLOCK (filter);
}

/* Create sinkpad to receive RTP packets from encers
 * and a srcpad for the RTP packets
 */
static GstPad *
create_rtp_sink (GstSrtpEnc * filter, const gchar * name)
{
  gchar *sinkpadname, *srcpadname;
  struct GstSrtpEncPads *priv;
  gint nb = 0;

  priv = g_slice_new0 (struct GstSrtpEncPads);

  GST_DEBUG_OBJECT (filter, "creating RTP sink pad");
  priv->sinkpad = gst_pad_new_from_static_template (&rtp_sink_template, name);

  sinkpadname = gst_pad_get_name (priv->sinkpad);
  sscanf (sinkpadname, "rtp_sink_%d", &nb);
  srcpadname = g_strdup_printf ("rtp_src_%d", nb);

  GST_DEBUG_OBJECT (filter, "creating RTP source pad");
  priv->srcpad =
      gst_pad_new_from_static_template (&rtp_src_template, srcpadname);
  g_free (srcpadname);
  g_free (sinkpadname);

  gst_pad_set_element_private (priv->sinkpad, priv);
  gst_pad_set_element_private (priv->srcpad, priv);

  gst_pad_set_query_function (priv->sinkpad,
      GST_DEBUG_FUNCPTR (gst_srtp_enc_sink_query_rtp));
  gst_pad_set_iterate_internal_links_function (priv->sinkpad,
      GST_DEBUG_FUNCPTR (gst_srtp_enc_iterate_internal_links_rtp));
  gst_pad_set_chain_function (priv->sinkpad,
      GST_DEBUG_FUNCPTR (gst_srtp_enc_chain_rtp));
  gst_pad_set_event_function (priv->sinkpad,
      GST_DEBUG_FUNCPTR (gst_srtp_enc_sink_event_rtp));
  gst_pad_set_active (priv->sinkpad, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (filter), priv->sinkpad);

  gst_pad_set_iterate_internal_links_function (priv->srcpad,
      GST_DEBUG_FUNCPTR (gst_srtp_enc_iterate_internal_links_rtp));
  gst_pad_set_active (priv->srcpad, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (filter), priv->srcpad);

  return priv->sinkpad;
}

/* Create sinkpad to receive RTCP packets from encers
 * and a srcpad for the RTCP packets
 */
static GstPad *
create_rtcp_sink (GstSrtpEnc * filter, const gchar * name)
{
  gchar *sinkpadname, *srcpadname;
  struct GstSrtpEncPads *priv;
  gint nb = 0;

  priv = g_slice_new0 (struct GstSrtpEncPads);

  GST_DEBUG_OBJECT (filter, "creating RTCP sink pad");
  priv->sinkpad = gst_pad_new_from_static_template (&rtcp_sink_template, name);

  sinkpadname = gst_pad_get_name (priv->sinkpad);
  sscanf (sinkpadname, "rtcp_sink_%d", &nb);
  srcpadname = g_strdup_printf ("rtcp_src_%d", nb);

  GST_DEBUG_OBJECT (filter, "creating RTCP source pad");
  priv->srcpad =
      gst_pad_new_from_static_template (&rtcp_src_template, srcpadname);
  g_free (srcpadname);
  g_free (sinkpadname);

  gst_pad_set_element_private (priv->sinkpad, priv);
  gst_pad_set_element_private (priv->srcpad, priv);

  gst_pad_set_query_function (priv->sinkpad,
      GST_DEBUG_FUNCPTR (gst_srtp_enc_sink_query_rtcp));
  gst_pad_set_iterate_internal_links_function (priv->sinkpad,
      GST_DEBUG_FUNCPTR (gst_srtp_enc_iterate_internal_links_rtcp));
  gst_pad_set_chain_function (priv->sinkpad,
      GST_DEBUG_FUNCPTR (gst_srtp_enc_chain_rtcp));
  gst_pad_set_event_function (priv->sinkpad,
      GST_DEBUG_FUNCPTR (gst_srtp_enc_sink_event_rtcp));
  gst_pad_set_active (priv->sinkpad, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (filter), priv->sinkpad);

  gst_pad_set_iterate_internal_links_function (priv->srcpad,
      GST_DEBUG_FUNCPTR (gst_srtp_enc_iterate_internal_links_rtcp));
  gst_pad_set_active (priv->srcpad, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (filter), priv->srcpad);

  return priv->sinkpad;
}

/* Handling new pad request
 */
static GstPad *
gst_srtp_enc_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps)
{
  GstElementClass *klass;
  GstSrtpEnc *filter;

  filter = GST_SRTP_ENC (element);
  klass = GST_ELEMENT_GET_CLASS (element);

  GST_INFO_OBJECT (element, "New pad requested");

  if (templ == gst_element_class_get_pad_template (klass, "rtp_sink_%d"))
    return create_rtp_sink (filter, name);

  if (templ == gst_element_class_get_pad_template (klass, "rtcp_sink_%d"))
    return create_rtcp_sink (filter, name);

  GST_ERROR_OBJECT (element, "Could not find specified template");
  return NULL;
}

/* Dispose
 */
static void
gst_srtp_enc_dispose (GObject * object)
{
  GstSrtpEnc *filter = GST_SRTP_ENC (object);
  GstIterator *it;
  GValue val = { 0 };

  GST_DEBUG_OBJECT (object, "Dispose...");

  it = gst_element_iterate_sink_pads (GST_ELEMENT_CAST (object));
  while (gst_iterator_next (it, &val) == GST_ITERATOR_OK) {
    gst_srtp_enc_release_pad (GST_ELEMENT_CAST (object),
        g_value_get_object (&val));
    g_value_unset (&val);
    gst_iterator_resync (it);
  }
  gst_iterator_free (it);

  if (filter->key)
    gst_buffer_unref (filter->key);
  filter->key = NULL;

  if (filter->ssrcs_set)
    g_hash_table_unref (filter->ssrcs_set);
  filter->ssrcs_set = NULL;

  G_OBJECT_CLASS (gst_srtp_enc_parent_class)->dispose (object);
}

static void
gst_srtp_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSrtpEnc *filter = GST_SRTP_ENC (object);

  GST_OBJECT_LOCK (filter);

  switch (prop_id) {
    case PROP_MKEY:
      if (filter->key)
        gst_buffer_unref (filter->key);
      filter->key = g_value_dup_boxed (value);
      filter->key_changed = TRUE;
      GST_INFO_OBJECT (object, "Set property: key=[%p]", filter->key);
      break;

    case PROP_RTP_CIPHER:
      filter->rtp_cipher = g_value_get_enum (value);
      GST_INFO_OBJECT (object, "Set property: rtp cipher=%d",
          filter->rtp_cipher);
      break;
    case PROP_RTP_AUTH:
      filter->rtp_auth = g_value_get_enum (value);
      GST_INFO_OBJECT (object, "Set property: rtp auth=%d", filter->rtp_auth);
      break;

    case PROP_RTCP_CIPHER:
      filter->rtcp_cipher = g_value_get_enum (value);
      GST_INFO_OBJECT (object, "Set property: rtcp cipher=%d",
          filter->rtcp_cipher);
      break;

    case PROP_RTCP_AUTH:
      filter->rtcp_auth = g_value_get_enum (value);
      GST_INFO_OBJECT (object, "Set property: rtcp auth=%d", filter->rtcp_auth);
      break;

    case PROP_RANDOM_KEY:
      filter->random_key = g_value_get_boolean (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (filter);
}

static void
gst_srtp_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSrtpEnc *filter = GST_SRTP_ENC (object);
  GST_OBJECT_LOCK (filter);

  switch (prop_id) {
    case PROP_MKEY:
      if (filter->key)
        g_value_set_boxed (value, filter->key);
      break;
    case PROP_RTP_CIPHER:
      g_value_set_enum (value, filter->rtp_cipher);
      break;
    case PROP_RTCP_CIPHER:
      g_value_set_enum (value, filter->rtcp_cipher);
      break;
    case PROP_RTP_AUTH:
      g_value_set_enum (value, filter->rtp_auth);
      break;
    case PROP_RTCP_AUTH:
      g_value_set_enum (value, filter->rtcp_auth);
      break;
    case PROP_RANDOM_KEY:
      g_value_set_boolean (value, filter->random_key);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (filter);
}

/* Returns the source pad linked with the sink pad
 */
static GstPad *
get_rtp_other_pad (GstPad * pad)
{
  struct GstSrtpEncPads *priv = gst_pad_get_element_private (pad);

  if (!priv)
    return NULL;

  if (pad == priv->srcpad)
    return priv->sinkpad;
  else if (pad == priv->sinkpad)
    return priv->srcpad;

  g_assert_not_reached ();
  return NULL;
}

/* Release a sink pad and it's linked source pad
 */
static void
gst_srtp_enc_release_pad (GstElement * element, GstPad * sinkpad)
{
  struct GstSrtpEncPads *priv;

  GST_INFO_OBJECT (element, "Releasing pad %s:%s",
      GST_DEBUG_PAD_NAME (sinkpad));

  priv = gst_pad_get_element_private (sinkpad);
  gst_pad_set_element_private (sinkpad, NULL);

  g_assert (priv);

  /* deactivate from source to sink */
  gst_pad_set_active (priv->srcpad, FALSE);
  gst_pad_set_active (priv->sinkpad, FALSE);

  /* remove pads */
  gst_element_remove_pad (element, priv->srcpad);
  gst_element_remove_pad (element, priv->sinkpad);

  g_slice_free (struct GstSrtpEncPads, priv);
}

/* Common setcaps function
 * Handles the link with other elements
 */
static gboolean
gst_srtp_enc_sink_setcaps (GstPad * pad, GstSrtpEnc * filter,
    GstCaps * caps, gboolean is_rtcp)
{
  GstPad *otherpad = NULL;
  GstStructure *ps = NULL;
  gboolean ret = FALSE;
  struct GstSrtpEncPads *priv = gst_pad_get_element_private (pad);

  g_return_val_if_fail (gst_caps_is_fixed (caps), FALSE);

  caps = gst_caps_copy (caps);

  ps = gst_caps_get_structure (caps, 0);

  GST_DEBUG_OBJECT (pad, "Sink caps: %" GST_PTR_FORMAT, caps);

  if (is_rtcp)
    gst_structure_set_name (ps, "application/x-srtcp");
  else
    gst_structure_set_name (ps, "application/x-srtp");

  GST_OBJECT_LOCK (filter);

  if (HAS_CRYPTO (filter))
    gst_structure_set (ps, "srtp-key", GST_TYPE_BUFFER, filter->key, NULL);

  /* Add srtp-specific params to source caps */
  gst_structure_set (ps,
      "srtp-cipher", G_TYPE_STRING,
      enum_nick_from_value (GST_TYPE_SRTP_CIPHER_TYPE, filter->rtp_cipher),
      "srtp-auth", G_TYPE_STRING,
      enum_nick_from_value (GST_TYPE_SRTP_AUTH_TYPE, filter->rtp_auth),
      "srtcp-cipher", G_TYPE_STRING,
      enum_nick_from_value (GST_TYPE_SRTP_CIPHER_TYPE, filter->rtcp_cipher),
      "srtcp-auth", G_TYPE_STRING,
      enum_nick_from_value (GST_TYPE_SRTP_AUTH_TYPE, filter->rtcp_auth), NULL);

  GST_OBJECT_UNLOCK (filter);

  GST_DEBUG_OBJECT (pad, "Source caps: %" GST_PTR_FORMAT, caps);

  /* Set caps on source pad */
  otherpad = priv->srcpad;

  ret = gst_pad_set_caps (otherpad, caps);

  gst_caps_unref (caps);

  return ret;
}

static gboolean
gst_srtp_enc_sink_query (GstPad * pad, GstObject * parent, GstQuery * query,
    gboolean is_rtcp)
{
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter = NULL;
      GstCaps *other_filter = NULL;
      GstPad *otherpad;
      GstCaps *other_caps;
      GstCaps *ret;
      GstCaps *template_caps;
      int i;

      otherpad = get_rtp_other_pad (pad);

      gst_query_parse_caps (query, &filter);
      if (filter) {
        other_filter = gst_caps_copy (filter);

        for (i = 0; i < gst_caps_get_size (other_filter); i++) {
          GstStructure *ps = gst_caps_get_structure (other_filter, i);
          if (is_rtcp)
            gst_structure_set_name (ps, "application/x-srtcp");
          else
            gst_structure_set_name (ps, "application/x-srtp");
        }
      }

      other_caps = gst_pad_peer_query_caps (otherpad, other_filter);

      if (other_filter)
        gst_caps_unref (other_filter);

      if (!other_caps)
        goto return_template;

      template_caps = gst_pad_get_pad_template_caps (otherpad);
      ret = gst_caps_intersect_full (other_caps, template_caps,
          GST_CAPS_INTERSECT_FIRST);
      gst_caps_unref (other_caps);
      gst_caps_unref (template_caps);

      ret = gst_caps_make_writable (ret);

      for (i = 0; i < gst_caps_get_size (ret); i++) {
        GstStructure *ps = gst_caps_get_structure (ret, i);
        if (is_rtcp)
          gst_structure_set_name (ps, "application/x-rtcp");
        else
          gst_structure_set_name (ps, "application/x-rtp");
        gst_structure_remove_fields (ps, "srtp-key", "srtp-cipher", "srtp-auth",
            "srtcp-cipher", "srtcp-auth", NULL);
      }

      gst_query_set_caps_result (query, ret);
      return TRUE;
    return_template:

      ret = gst_pad_get_pad_template_caps (pad);
      gst_query_set_caps_result (query, ret);
      gst_caps_unref (ret);

      return TRUE;
    }
    default:
      return gst_pad_query_default (pad, parent, query);
  }
}

static gboolean
gst_srtp_enc_sink_query_rtp (GstPad * pad, GstObject * parent, GstQuery * query)
{
  return gst_srtp_enc_sink_query (pad, parent, query, FALSE);
}

static gboolean
gst_srtp_enc_sink_query_rtcp (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  return gst_srtp_enc_sink_query (pad, parent, query, TRUE);
}

static GstIterator *
gst_srtp_enc_iterate_internal_links (GstPad * pad, GstObject * parent,
    gboolean is_rtcp)
{
  GstSrtpEnc *filter = GST_SRTP_ENC (parent);
  GstPad *otherpad = NULL;
  GstIterator *it = NULL;

  otherpad = get_rtp_other_pad (pad);

  if (otherpad) {
    GValue val = { 0 };

    g_value_init (&val, GST_TYPE_PAD);
    g_value_set_object (&val, otherpad);
    it = gst_iterator_new_single (GST_TYPE_PAD, &val);
    g_value_unset (&val);
  } else {
    GST_ELEMENT_ERROR (GST_ELEMENT_CAST (filter), CORE, PAD, (NULL),
        ("Unable to get linked pad"));
  }

  return it;
}

static GstIterator *
gst_srtp_enc_iterate_internal_links_rtp (GstPad * pad, GstObject * parent)
{
  return gst_srtp_enc_iterate_internal_links (pad, parent, FALSE);
}

static GstIterator *
gst_srtp_enc_iterate_internal_links_rtcp (GstPad * pad, GstObject * parent)
{
  return gst_srtp_enc_iterate_internal_links (pad, parent, TRUE);
}


static void
gst_srtp_enc_replace_random_key (GstSrtpEnc * filter)
{
  guint i;
  GstMapInfo map;

  GST_DEBUG_OBJECT (filter, "Generating random key");

  if (filter->key)
    gst_buffer_unref (filter->key);

  filter->key = gst_buffer_new_allocate (NULL, 16 + 14, NULL);

  gst_buffer_map (filter->key, &map, GST_MAP_WRITE);
  for (i = 0; i < map.size; i += 4)
    GST_WRITE_UINT32_BE (map.data + i, g_random_int ());
  gst_buffer_unmap (filter->key, &map);

  filter->key_changed = TRUE;
}

static GstFlowReturn
gst_srtp_enc_chain (GstPad * pad, GstObject * parent, GstBuffer * buf,
    gboolean is_rtcp)
{
  GstSrtpEnc *filter = GST_SRTP_ENC (parent);
  GstFlowReturn ret = GST_FLOW_OK;
  GstPad *otherpad = NULL;
  err_status_t err = err_status_ok;
  gint size_max, size;
  GstBuffer *bufout = NULL;
  struct GstSrtpEncPads *priv = gst_pad_get_element_private (pad);
  guint32 ssrc;
  gboolean do_setcaps = FALSE;
  GstMapInfo mapin, mapout;

  if (!priv)
    goto fail;

  if (!is_rtcp) {
    GstRTPBuffer rtpbuf = GST_RTP_BUFFER_INIT;

    if (!gst_rtp_buffer_map (buf, GST_MAP_READ, &rtpbuf)) {
      GST_ELEMENT_ERROR (filter, STREAM, WRONG_TYPE, (NULL),
          ("Could not map RTP buffer"));
      ret = GST_FLOW_ERROR;
      goto out;
    }

    ssrc = gst_rtp_buffer_get_ssrc (&rtpbuf);

    gst_rtp_buffer_unmap (&rtpbuf);
  } else if (!rtcp_buffer_get_ssrc (buf, &ssrc)) {
    GST_ELEMENT_ERROR (filter, STREAM, WRONG_TYPE, (NULL),
        ("No SSRC found in buffer, dropping"));
    ret = GST_FLOW_ERROR;
    goto out;
  }

  do_setcaps = filter->key_changed;
  if (filter->key_changed)
    gst_srtp_enc_reset (filter);

  if (!check_new_stream_locked (filter, ssrc)) {
    GST_ELEMENT_ERROR (filter, LIBRARY, INIT,
        ("Could not initialize SRTP encoder"),
        ("Failed to add stream to SRTP encoder"));
    ret = GST_FLOW_ERROR;
    goto out;
  }
  priv->ssrc = ssrc;

  GST_OBJECT_LOCK (filter);

  /* Update source caps if asked */
  if (do_setcaps) {
    GstCaps *caps;
    GST_OBJECT_UNLOCK (filter);

    caps = gst_pad_get_current_caps (pad);
    if (!gst_srtp_enc_sink_setcaps (pad, filter, caps, is_rtcp)) {
      gst_caps_unref (caps);
      ret = GST_FLOW_NOT_NEGOTIATED;
      goto out;
    }
    gst_caps_unref (caps);

    GST_OBJECT_LOCK (filter);
  }

  if (!HAS_CRYPTO (filter)) {
    GST_OBJECT_UNLOCK (filter);
    otherpad = get_rtp_other_pad (pad);
    return gst_pad_push (otherpad, buf);
  }


  /* Create a bigger buffer to add protection */
  size_max = gst_buffer_get_size (buf) + SRTP_MAX_TRAILER_LEN + 10;
  bufout = gst_buffer_new_allocate (NULL, size_max, NULL);

  gst_buffer_map (buf, &mapin, GST_MAP_READ);
  gst_buffer_map (bufout, &mapout, GST_MAP_READWRITE);

  size = mapin.size;
  memcpy (mapout.data, mapin.data, mapin.size);

  gst_buffer_unmap (buf, &mapin);

  gst_srtp_init_event_reporter ();

  if (is_rtcp)
    err = srtp_protect_rtcp (filter->session, mapout.data, &size);
  else
    err = srtp_protect (filter->session, mapout.data, &size);

  gst_buffer_unmap (bufout, &mapout);

  GST_OBJECT_UNLOCK (filter);

  if (err == err_status_ok) {
    /* Buffer protected */
    gst_buffer_set_size (bufout, size);
    gst_buffer_copy_into (bufout, buf, GST_BUFFER_COPY_METADATA, 0, -1);

    GST_LOG_OBJECT (pad, "Encing %s buffer of size %d",
        is_rtcp ? "RTCP" : "RTP", size);

    /* Push buffer to source pad */
    otherpad = get_rtp_other_pad (pad);
    ret = gst_pad_push (otherpad, bufout);
    bufout = NULL;

    if (ret != GST_FLOW_OK)
      goto out;

  } else if (err == err_status_key_expired) {

    GST_ELEMENT_ERROR (GST_ELEMENT_CAST (filter), STREAM, ENCODE,
        ("Key usage limit has been reached"),
        ("Unable to protect buffer (hard key usage limit reached)"));
    gst_buffer_unref (bufout);
    goto fail;

  } else {
    /* srtp_protect failed */
    GST_ELEMENT_ERROR (filter, LIBRARY, FAILED, (NULL),
        ("Unable to protect buffer (protect failed) code %d", err));
    gst_buffer_unref (bufout);
    goto fail;
  }

  if (gst_srtp_get_soft_limit_reached ()) {
    g_signal_emit (filter, gst_srtp_enc_signals[SIGNAL_SOFT_LIMIT], 0, ssrc);
    if (filter->random_key && !filter->key_changed)
      gst_srtp_enc_replace_random_key (filter);
  }

out:

  gst_buffer_unref (buf);

  return ret;

fail:
  ret = GST_FLOW_ERROR;
  goto out;
}

static GstFlowReturn
gst_srtp_enc_chain_rtp (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  return gst_srtp_enc_chain (pad, parent, buf, FALSE);
}

static GstFlowReturn
gst_srtp_enc_chain_rtcp (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  return gst_srtp_enc_chain (pad, parent, buf, TRUE);
}


/* Change state
 */
static GstStateChangeReturn
gst_srtp_enc_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn res;
  GstSrtpEnc *filter;

  filter = GST_SRTP_ENC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (filter->rtp_cipher != GST_SRTP_CIPHER_NULL ||
          filter->rtcp_cipher != GST_SRTP_CIPHER_NULL ||
          filter->rtp_auth != GST_SRTP_AUTH_NULL ||
          filter->rtcp_auth != GST_SRTP_AUTH_NULL) {
        if (!filter->key) {
          if (filter->random_key) {
            gst_srtp_enc_replace_random_key (filter);
          } else {
            GST_ERROR_OBJECT (element, "Need a key to get to READY");
            return GST_STATE_CHANGE_FAILURE;
          }
        }
      }
      if ((filter->rtcp_cipher != NULL_CIPHER)
          && (filter->rtcp_auth == NULL_AUTH)) {
        GST_ERROR_OBJECT (filter,
            "RTCP authentication can't be NULL if encryption is not NULL.");
        return GST_STATE_CHANGE_FAILURE;
      }
      if (!filter->first_session)
        gst_srtp_enc_reset (filter);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  res = GST_ELEMENT_CLASS (gst_srtp_enc_parent_class)->change_state (element,
      transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_srtp_enc_reset (filter);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return res;
}

static gboolean
gst_srtp_enc_sink_event (GstPad * pad, GstObject * parent, GstEvent * event,
    gboolean is_rtcp)
{
  GstSrtpEnc *filter = GST_SRTP_ENC (parent);
  gboolean ret;
  GstPad *otherpad;

  otherpad = get_rtp_other_pad (pad);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      GST_DEBUG_OBJECT (pad, "Encing event Flush stop (%d)",
          GST_EVENT_TYPE (event));
      gst_srtp_enc_reset (filter);
      ret = gst_pad_push_event (otherpad, event);
      break;
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      ret = gst_srtp_enc_sink_setcaps (pad, filter, caps, is_rtcp);
      break;
    }
    default:
      GST_DEBUG_OBJECT (pad, "Encing event default (%d)",
          GST_EVENT_TYPE (event));
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }

  return ret;
}

static gboolean
gst_srtp_enc_sink_event_rtp (GstPad * pad, GstObject * parent, GstEvent * event)
{
  return gst_srtp_enc_sink_event (pad, parent, event, FALSE);
}

static gboolean
gst_srtp_enc_sink_event_rtcp (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  return gst_srtp_enc_sink_event (pad, parent, event, TRUE);
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
gboolean
gst_srtp_enc_plugin_init (GstPlugin * srtpenc)
{
  GST_DEBUG_CATEGORY_INIT (gst_srtp_enc_debug, "srtpenc", 0, "SRTP Enc");

  return gst_element_register (srtpenc, "srtpenc", GST_RANK_NONE,
      GST_TYPE_SRTP_ENC);
}
