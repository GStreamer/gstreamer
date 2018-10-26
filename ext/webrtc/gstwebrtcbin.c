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

#include "gstwebrtcbin.h"
#include "gstwebrtcstats.h"
#include "transportstream.h"
#include "transportreceivebin.h"
#include "utils.h"
#include "webrtcsdp.h"
#include "webrtctransceiver.h"
#include "webrtcdatachannel.h"
#include "sctptransport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RANDOM_SESSION_ID \
    ((((((guint64) g_random_int()) << 32) | \
       (guint64) g_random_int ())) & \
    G_GUINT64_CONSTANT (0x7fffffffffffffff))

#define PC_GET_LOCK(w) (&w->priv->pc_lock)
#define PC_LOCK(w) (g_mutex_lock (PC_GET_LOCK(w)))
#define PC_UNLOCK(w) (g_mutex_unlock (PC_GET_LOCK(w)))

#define PC_GET_COND(w) (&w->priv->pc_cond)
#define PC_COND_WAIT(w) (g_cond_wait(PC_GET_COND(w), PC_GET_LOCK(w)))
#define PC_COND_BROADCAST(w) (g_cond_broadcast(PC_GET_COND(w)))
#define PC_COND_SIGNAL(w) (g_cond_signal(PC_GET_COND(w)))

/*
 * This webrtcbin implements the majority of the W3's peerconnection API and
 * implementation guide where possible. Generating offers, answers and setting
 * local and remote SDP's are all supported.  Both media descriptions and
 * descriptions involving data channels are supported.
 *
 * Each input/output pad is equivalent to a Track in W3 parlance which are
 * added/removed from the bin.  The number of requested sink pads is the number
 * of streams that will be sent to the receiver and will be associated with a
 * GstWebRTCRTPTransceiver (very similar to W3 RTPTransceiver's).
 *
 * On the receiving side, RTPTransceiver's are created in response to setting
 * a remote description.  Output pads for the receiving streams in the set
 * description are also created.
 */

/*
 * TODO:
 * assert sending payload type matches the stream
 * reconfiguration (of anything)
 * LS groups
 * balanced bundle policy
 * setting custom DTLS certificates
 *
 * seperate session id's from mlineindex properly
 * how to deal with replacing a input/output track/stream
 */

static void _update_need_negotiation (GstWebRTCBin * webrtc);

#define GST_CAT_DEFAULT gst_webrtc_bin_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static gboolean
_have_nice_elements (GstWebRTCBin * webrtc)
{
  GstPluginFeature *feature;

  feature = gst_registry_lookup_feature (gst_registry_get (), "nicesrc");
  if (feature) {
    gst_object_unref (feature);
  } else {
    GST_ELEMENT_ERROR (webrtc, CORE, MISSING_PLUGIN, NULL,
        ("%s", "libnice elements are not available"));
    return FALSE;
  }

  feature = gst_registry_lookup_feature (gst_registry_get (), "nicesink");
  if (feature) {
    gst_object_unref (feature);
  } else {
    GST_ELEMENT_ERROR (webrtc, CORE, MISSING_PLUGIN, NULL,
        ("%s", "libnice elements are not available"));
    return FALSE;
  }

  return TRUE;
}

static gboolean
_have_sctp_elements (GstWebRTCBin * webrtc)
{
  GstPluginFeature *feature;

  feature = gst_registry_lookup_feature (gst_registry_get (), "sctpdec");
  if (feature) {
    gst_object_unref (feature);
  } else {
    GST_ELEMENT_ERROR (webrtc, CORE, MISSING_PLUGIN, NULL,
        ("%s", "sctp elements are not available"));
    return FALSE;
  }

  feature = gst_registry_lookup_feature (gst_registry_get (), "sctpenc");
  if (feature) {
    gst_object_unref (feature);
  } else {
    GST_ELEMENT_ERROR (webrtc, CORE, MISSING_PLUGIN, NULL,
        ("%s", "sctp elements are not available"));
    return FALSE;
  }

  return TRUE;
}

static gboolean
_have_dtls_elements (GstWebRTCBin * webrtc)
{
  GstPluginFeature *feature;

  feature = gst_registry_lookup_feature (gst_registry_get (), "dtlsdec");
  if (feature) {
    gst_object_unref (feature);
  } else {
    GST_ELEMENT_ERROR (webrtc, CORE, MISSING_PLUGIN, NULL,
        ("%s", "dtls elements are not available"));
    return FALSE;
  }

  feature = gst_registry_lookup_feature (gst_registry_get (), "dtlsenc");
  if (feature) {
    gst_object_unref (feature);
  } else {
    GST_ELEMENT_ERROR (webrtc, CORE, MISSING_PLUGIN, NULL,
        ("%s", "dtls elements are not available"));
    return FALSE;
  }

  return TRUE;
}

GQuark
gst_webrtc_bin_error_quark (void)
{
  return g_quark_from_static_string ("gst-webrtc-bin-error-quark");
}

G_DEFINE_TYPE (GstWebRTCBinPad, gst_webrtc_bin_pad, GST_TYPE_GHOST_PAD);

static void
gst_webrtc_bin_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_webrtc_bin_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_webrtc_bin_pad_finalize (GObject * object)
{
  GstWebRTCBinPad *pad = GST_WEBRTC_BIN_PAD (object);

  if (pad->trans)
    gst_object_unref (pad->trans);
  pad->trans = NULL;

  if (pad->received_caps)
    gst_caps_unref (pad->received_caps);
  pad->received_caps = NULL;

  G_OBJECT_CLASS (gst_webrtc_bin_pad_parent_class)->finalize (object);
}

static void
gst_webrtc_bin_pad_class_init (GstWebRTCBinPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->get_property = gst_webrtc_bin_pad_get_property;
  gobject_class->set_property = gst_webrtc_bin_pad_set_property;
  gobject_class->finalize = gst_webrtc_bin_pad_finalize;
}

static GstCaps *
_transport_stream_get_caps_for_pt (TransportStream * stream, guint pt)
{
  guint i, len;

  len = stream->ptmap->len;
  for (i = 0; i < len; i++) {
    PtMapItem *item = &g_array_index (stream->ptmap, PtMapItem, i);
    if (item->pt == pt)
      return item->caps;
  }
  return NULL;
}

static gint
_transport_stream_get_pt (TransportStream * stream, const gchar * encoding_name)
{
  guint i;
  gint ret = 0;

  for (i = 0; i < stream->ptmap->len; i++) {
    PtMapItem *item = &g_array_index (stream->ptmap, PtMapItem, i);
    if (!gst_caps_is_empty (item->caps)) {
      GstStructure *s = gst_caps_get_structure (item->caps, 0);
      if (!g_strcmp0 (gst_structure_get_string (s, "encoding-name"),
              encoding_name)) {
        ret = item->pt;
        break;
      }
    }
  }

  return ret;
}

static gboolean
gst_webrtcbin_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstWebRTCBinPad *wpad = GST_WEBRTC_BIN_PAD (pad);

  if (GST_EVENT_TYPE (event) == GST_EVENT_CAPS) {
    GstCaps *caps;
    gboolean do_update;

    gst_event_parse_caps (event, &caps);
    do_update = (!wpad->received_caps
        || gst_caps_is_equal (wpad->received_caps, caps));
    gst_caps_replace (&wpad->received_caps, caps);

    if (do_update)
      _update_need_negotiation (GST_WEBRTC_BIN (parent));
  }

  return gst_pad_event_default (pad, parent, event);
}

static void
gst_webrtc_bin_pad_init (GstWebRTCBinPad * pad)
{
}

static GstWebRTCBinPad *
gst_webrtc_bin_pad_new (const gchar * name, GstPadDirection direction)
{
  GstWebRTCBinPad *pad =
      g_object_new (gst_webrtc_bin_pad_get_type (), "name", name, "direction",
      direction, NULL);

  gst_pad_set_event_function (GST_PAD (pad), gst_webrtcbin_sink_event);

  if (!gst_ghost_pad_construct (GST_GHOST_PAD (pad))) {
    gst_object_unref (pad);
    return NULL;
  }

  GST_DEBUG_OBJECT (pad, "new visible pad with direction %s",
      direction == GST_PAD_SRC ? "src" : "sink");
  return pad;
}

#define gst_webrtc_bin_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstWebRTCBin, gst_webrtc_bin, GST_TYPE_BIN,
    G_ADD_PRIVATE (GstWebRTCBin)
    GST_DEBUG_CATEGORY_INIT (gst_webrtc_bin_debug, "webrtcbin", 0,
        "webrtcbin element");
    );

static GstPad *_connect_input_stream (GstWebRTCBin * webrtc,
    GstWebRTCBinPad * pad);

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rtp"));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("application/x-rtp"));

enum
{
  SIGNAL_0,
  CREATE_OFFER_SIGNAL,
  CREATE_ANSWER_SIGNAL,
  SET_LOCAL_DESCRIPTION_SIGNAL,
  SET_REMOTE_DESCRIPTION_SIGNAL,
  ADD_ICE_CANDIDATE_SIGNAL,
  ON_NEGOTIATION_NEEDED_SIGNAL,
  ON_ICE_CANDIDATE_SIGNAL,
  ON_NEW_TRANSCEIVER_SIGNAL,
  GET_STATS_SIGNAL,
  ADD_TRANSCEIVER_SIGNAL,
  GET_TRANSCEIVERS_SIGNAL,
  ADD_TURN_SERVER_SIGNAL,
  CREATE_DATA_CHANNEL_SIGNAL,
  ON_DATA_CHANNEL_SIGNAL,
  LAST_SIGNAL,
};

enum
{
  PROP_0,
  PROP_CONNECTION_STATE,
  PROP_SIGNALING_STATE,
  PROP_ICE_GATHERING_STATE,
  PROP_ICE_CONNECTION_STATE,
  PROP_LOCAL_DESCRIPTION,
  PROP_CURRENT_LOCAL_DESCRIPTION,
  PROP_PENDING_LOCAL_DESCRIPTION,
  PROP_REMOTE_DESCRIPTION,
  PROP_CURRENT_REMOTE_DESCRIPTION,
  PROP_PENDING_REMOTE_DESCRIPTION,
  PROP_STUN_SERVER,
  PROP_TURN_SERVER,
  PROP_BUNDLE_POLICY,
};

static guint gst_webrtc_bin_signals[LAST_SIGNAL] = { 0 };

static GstWebRTCDTLSTransport *
_transceiver_get_transport (GstWebRTCRTPTransceiver * trans)
{
  if (trans->sender) {
    return trans->sender->transport;
  } else if (trans->receiver) {
    return trans->receiver->transport;
  }

  return NULL;
}

static GstWebRTCDTLSTransport *
_transceiver_get_rtcp_transport (GstWebRTCRTPTransceiver * trans)
{
  if (trans->sender) {
    return trans->sender->rtcp_transport;
  } else if (trans->receiver) {
    return trans->receiver->rtcp_transport;
  }

  return NULL;
}

typedef struct
{
  guint session_id;
  GstWebRTCICEStream *stream;
} IceStreamItem;

/* FIXME: locking? */
GstWebRTCICEStream *
_find_ice_stream_for_session (GstWebRTCBin * webrtc, guint session_id)
{
  int i;

  for (i = 0; i < webrtc->priv->ice_stream_map->len; i++) {
    IceStreamItem *item =
        &g_array_index (webrtc->priv->ice_stream_map, IceStreamItem, i);

    if (item->session_id == session_id) {
      GST_TRACE_OBJECT (webrtc, "Found ice stream id %" GST_PTR_FORMAT " for "
          "session %u", item->stream, session_id);
      return item->stream;
    }
  }

  GST_TRACE_OBJECT (webrtc, "No ice stream available for session %u",
      session_id);
  return NULL;
}

void
_add_ice_stream_item (GstWebRTCBin * webrtc, guint session_id,
    GstWebRTCICEStream * stream)
{
  IceStreamItem item = { session_id, stream };

  GST_TRACE_OBJECT (webrtc, "adding ice stream %" GST_PTR_FORMAT " for "
      "session %u", stream, session_id);
  g_array_append_val (webrtc->priv->ice_stream_map, item);
}

typedef struct
{
  guint session_id;
  gchar *mid;
} SessionMidItem;

static void
clear_session_mid_item (SessionMidItem * item)
{
  g_free (item->mid);
}

typedef gboolean (*FindTransceiverFunc) (GstWebRTCRTPTransceiver * p1,
    gconstpointer data);

static GstWebRTCRTPTransceiver *
_find_transceiver (GstWebRTCBin * webrtc, gconstpointer data,
    FindTransceiverFunc func)
{
  int i;

  for (i = 0; i < webrtc->priv->transceivers->len; i++) {
    GstWebRTCRTPTransceiver *transceiver =
        g_array_index (webrtc->priv->transceivers, GstWebRTCRTPTransceiver *,
        i);

    if (func (transceiver, data))
      return transceiver;
  }

  return NULL;
}

static gboolean
match_for_mid (GstWebRTCRTPTransceiver * trans, const gchar * mid)
{
  return g_strcmp0 (trans->mid, mid) == 0;
}

static gboolean
transceiver_match_for_mline (GstWebRTCRTPTransceiver * trans, guint * mline)
{
  return trans->mline == *mline;
}

static GstWebRTCRTPTransceiver *
_find_transceiver_for_mline (GstWebRTCBin * webrtc, guint mlineindex)
{
  GstWebRTCRTPTransceiver *trans;

  trans = _find_transceiver (webrtc, &mlineindex,
      (FindTransceiverFunc) transceiver_match_for_mline);

  GST_TRACE_OBJECT (webrtc,
      "Found transceiver %" GST_PTR_FORMAT " for mlineindex %u", trans,
      mlineindex);

  return trans;
}

typedef gboolean (*FindTransportFunc) (TransportStream * p1,
    gconstpointer data);

static TransportStream *
_find_transport (GstWebRTCBin * webrtc, gconstpointer data,
    FindTransportFunc func)
{
  int i;

  for (i = 0; i < webrtc->priv->transports->len; i++) {
    TransportStream *stream =
        g_array_index (webrtc->priv->transports, TransportStream *,
        i);

    if (func (stream, data))
      return stream;
  }

  return NULL;
}

static gboolean
match_stream_for_session (TransportStream * trans, guint * session)
{
  return trans->session_id == *session;
}

static TransportStream *
_find_transport_for_session (GstWebRTCBin * webrtc, guint session_id)
{
  TransportStream *stream;

  stream = _find_transport (webrtc, &session_id,
      (FindTransportFunc) match_stream_for_session);

  GST_TRACE_OBJECT (webrtc,
      "Found transport %" GST_PTR_FORMAT " for session %u", stream, session_id);

  return stream;
}

typedef gboolean (*FindPadFunc) (GstWebRTCBinPad * p1, gconstpointer data);

static GstWebRTCBinPad *
_find_pad (GstWebRTCBin * webrtc, gconstpointer data, FindPadFunc func)
{
  GstElement *element = GST_ELEMENT (webrtc);
  GList *l;

  GST_OBJECT_LOCK (webrtc);
  l = element->pads;
  for (; l; l = g_list_next (l)) {
    if (!GST_IS_WEBRTC_BIN_PAD (l->data))
      continue;
    if (func (l->data, data)) {
      gst_object_ref (l->data);
      GST_OBJECT_UNLOCK (webrtc);
      return l->data;
    }
  }

  l = webrtc->priv->pending_pads;
  for (; l; l = g_list_next (l)) {
    if (!GST_IS_WEBRTC_BIN_PAD (l->data))
      continue;
    if (func (l->data, data)) {
      gst_object_ref (l->data);
      GST_OBJECT_UNLOCK (webrtc);
      return l->data;
    }
  }
  GST_OBJECT_UNLOCK (webrtc);

  return NULL;
}

typedef gboolean (*FindDataChannelFunc) (GstWebRTCDataChannel * p1,
    gconstpointer data);

static GstWebRTCDataChannel *
_find_data_channel (GstWebRTCBin * webrtc, gconstpointer data,
    FindDataChannelFunc func)
{
  int i;

  for (i = 0; i < webrtc->priv->data_channels->len; i++) {
    GstWebRTCDataChannel *channel =
        g_array_index (webrtc->priv->data_channels, GstWebRTCDataChannel *,
        i);

    if (func (channel, data))
      return channel;
  }

  return NULL;
}

static gboolean
data_channel_match_for_id (GstWebRTCDataChannel * channel, gint * id)
{
  return channel->id == *id;
}

static GstWebRTCDataChannel *
_find_data_channel_for_id (GstWebRTCBin * webrtc, gint id)
{
  GstWebRTCDataChannel *channel;

  channel = _find_data_channel (webrtc, &id,
      (FindDataChannelFunc) data_channel_match_for_id);

  GST_TRACE_OBJECT (webrtc,
      "Found data channel %" GST_PTR_FORMAT " for id %i", channel, id);

  return channel;
}

static void
_add_pad_to_list (GstWebRTCBin * webrtc, GstWebRTCBinPad * pad)
{
  GST_OBJECT_LOCK (webrtc);
  webrtc->priv->pending_pads = g_list_prepend (webrtc->priv->pending_pads, pad);
  GST_OBJECT_UNLOCK (webrtc);
}

static void
_remove_pending_pad (GstWebRTCBin * webrtc, GstWebRTCBinPad * pad)
{
  GST_OBJECT_LOCK (webrtc);
  webrtc->priv->pending_pads = g_list_remove (webrtc->priv->pending_pads, pad);
  GST_OBJECT_UNLOCK (webrtc);
}

static void
_add_pad (GstWebRTCBin * webrtc, GstWebRTCBinPad * pad)
{
  _remove_pending_pad (webrtc, pad);

  if (webrtc->priv->running)
    gst_pad_set_active (GST_PAD (pad), TRUE);
  gst_element_add_pad (GST_ELEMENT (webrtc), GST_PAD (pad));
}

static void
_remove_pad (GstWebRTCBin * webrtc, GstWebRTCBinPad * pad)
{
  _remove_pending_pad (webrtc, pad);

  gst_element_remove_pad (GST_ELEMENT (webrtc), GST_PAD (pad));
}

typedef struct
{
  GstPadDirection direction;
  guint mlineindex;
} MLineMatch;

static gboolean
pad_match_for_mline (GstWebRTCBinPad * pad, const MLineMatch * match)
{
  return GST_PAD_DIRECTION (pad) == match->direction
      && pad->mlineindex == match->mlineindex;
}

static GstWebRTCBinPad *
_find_pad_for_mline (GstWebRTCBin * webrtc, GstPadDirection direction,
    guint mlineindex)
{
  MLineMatch m = { direction, mlineindex };

  return _find_pad (webrtc, &m, (FindPadFunc) pad_match_for_mline);
}

typedef struct
{
  GstPadDirection direction;
  GstWebRTCRTPTransceiver *trans;
} TransMatch;

static gboolean
pad_match_for_transceiver (GstWebRTCBinPad * pad, TransMatch * m)
{
  return GST_PAD_DIRECTION (pad) == m->direction && pad->trans == m->trans;
}

static GstWebRTCBinPad *
_find_pad_for_transceiver (GstWebRTCBin * webrtc, GstPadDirection direction,
    GstWebRTCRTPTransceiver * trans)
{
  TransMatch m = { direction, trans };

  return _find_pad (webrtc, &m, (FindPadFunc) pad_match_for_transceiver);
}

#if 0
static gboolean
match_for_ssrc (GstWebRTCBinPad * pad, guint * ssrc)
{
  return pad->ssrc == *ssrc;
}

static gboolean
match_for_pad (GstWebRTCBinPad * pad, GstWebRTCBinPad * other)
{
  return pad == other;
}
#endif

static gboolean
_unlock_pc_thread (GMutex * lock)
{
  g_mutex_unlock (lock);
  return G_SOURCE_REMOVE;
}

static gpointer
_gst_pc_thread (GstWebRTCBin * webrtc)
{
  PC_LOCK (webrtc);
  webrtc->priv->main_context = g_main_context_new ();
  webrtc->priv->loop = g_main_loop_new (webrtc->priv->main_context, FALSE);

  PC_COND_BROADCAST (webrtc);
  g_main_context_invoke (webrtc->priv->main_context,
      (GSourceFunc) _unlock_pc_thread, PC_GET_LOCK (webrtc));

  /* Having the thread be the thread default GMainContext will break the
   * required queue-like ordering (from W3's peerconnection spec) of re-entrant
   * tasks */
  g_main_loop_run (webrtc->priv->loop);

  PC_LOCK (webrtc);
  g_main_context_unref (webrtc->priv->main_context);
  webrtc->priv->main_context = NULL;
  g_main_loop_unref (webrtc->priv->loop);
  webrtc->priv->loop = NULL;
  PC_COND_BROADCAST (webrtc);
  PC_UNLOCK (webrtc);

  return NULL;
}

static void
_start_thread (GstWebRTCBin * webrtc)
{
  PC_LOCK (webrtc);
  webrtc->priv->thread = g_thread_new ("gst-pc-ops",
      (GThreadFunc) _gst_pc_thread, webrtc);

  while (!webrtc->priv->loop)
    PC_COND_WAIT (webrtc);
  webrtc->priv->is_closed = FALSE;
  PC_UNLOCK (webrtc);
}

static void
_stop_thread (GstWebRTCBin * webrtc)
{
  PC_LOCK (webrtc);
  webrtc->priv->is_closed = TRUE;
  g_main_loop_quit (webrtc->priv->loop);
  while (webrtc->priv->loop)
    PC_COND_WAIT (webrtc);
  PC_UNLOCK (webrtc);

  g_thread_unref (webrtc->priv->thread);
}

static gboolean
_execute_op (GstWebRTCBinTask * op)
{
  PC_LOCK (op->webrtc);
  if (op->webrtc->priv->is_closed) {
    GST_DEBUG_OBJECT (op->webrtc,
        "Peerconnection is closed, aborting execution");
    goto out;
  }

  op->op (op->webrtc, op->data);

out:
  PC_UNLOCK (op->webrtc);
  return G_SOURCE_REMOVE;
}

static void
_free_op (GstWebRTCBinTask * op)
{
  if (op->notify)
    op->notify (op->data);
  g_free (op);
}

void
gst_webrtc_bin_enqueue_task (GstWebRTCBin * webrtc, GstWebRTCBinFunc func,
    gpointer data, GDestroyNotify notify)
{
  GstWebRTCBinTask *op;
  GSource *source;

  g_return_if_fail (GST_IS_WEBRTC_BIN (webrtc));

  if (webrtc->priv->is_closed) {
    GST_DEBUG_OBJECT (webrtc, "Peerconnection is closed, aborting execution");
    if (notify)
      notify (data);
    return;
  }
  op = g_new0 (GstWebRTCBinTask, 1);
  op->webrtc = webrtc;
  op->op = func;
  op->data = data;
  op->notify = notify;

  source = g_idle_source_new ();
  g_source_set_priority (source, G_PRIORITY_DEFAULT);
  g_source_set_callback (source, (GSourceFunc) _execute_op, op,
      (GDestroyNotify) _free_op);
  g_source_attach (source, webrtc->priv->main_context);
  g_source_unref (source);
}

/* https://www.w3.org/TR/webrtc/#dom-rtciceconnectionstate */
static GstWebRTCICEConnectionState
_collate_ice_connection_states (GstWebRTCBin * webrtc)
{
#define STATE(val) GST_WEBRTC_ICE_CONNECTION_STATE_ ## val
  GstWebRTCICEConnectionState any_state = 0;
  gboolean all_closed = TRUE;
  int i;

  for (i = 0; i < webrtc->priv->transceivers->len; i++) {
    GstWebRTCRTPTransceiver *rtp_trans =
        g_array_index (webrtc->priv->transceivers, GstWebRTCRTPTransceiver *,
        i);
    WebRTCTransceiver *trans = WEBRTC_TRANSCEIVER (rtp_trans);
    TransportStream *stream = trans->stream;
    GstWebRTCICETransport *transport, *rtcp_transport;
    GstWebRTCICEConnectionState ice_state;
    gboolean rtcp_mux = FALSE;

    if (rtp_trans->stopped)
      continue;
    if (!rtp_trans->mid)
      continue;

    g_object_get (stream, "rtcp-mux", &rtcp_mux, NULL);

    transport = _transceiver_get_transport (rtp_trans)->transport;

    /* get transport state */
    g_object_get (transport, "state", &ice_state, NULL);
    any_state |= (1 << ice_state);
    if (ice_state != STATE (CLOSED))
      all_closed = FALSE;

    rtcp_transport = _transceiver_get_rtcp_transport (rtp_trans)->transport;

    if (!rtcp_mux && rtcp_transport && transport != rtcp_transport) {
      g_object_get (rtcp_transport, "state", &ice_state, NULL);
      any_state |= (1 << ice_state);
      if (ice_state != STATE (CLOSED))
        all_closed = FALSE;
    }
  }

  GST_TRACE_OBJECT (webrtc, "ICE connection state: 0x%x", any_state);

  if (webrtc->priv->is_closed) {
    GST_TRACE_OBJECT (webrtc, "returning closed");
    return STATE (CLOSED);
  }
  /* Any of the RTCIceTransport s are in the failed state. */
  if (any_state & (1 << STATE (FAILED))) {
    GST_TRACE_OBJECT (webrtc, "returning failed");
    return STATE (FAILED);
  }
  /* Any of the RTCIceTransport s are in the disconnected state and
   * none of them are in the failed state. */
  if (any_state & (1 << STATE (DISCONNECTED))) {
    GST_TRACE_OBJECT (webrtc, "returning disconnected");
    return STATE (DISCONNECTED);
  }
  /* Any of the RTCIceTransport's are in the checking state and none of them
   * are in the failed or disconnected state. */
  if (any_state & (1 << STATE (CHECKING))) {
    GST_TRACE_OBJECT (webrtc, "returning checking");
    return STATE (CHECKING);
  }
  /* Any of the RTCIceTransport s are in the new state and none of them are
   * in the checking, failed or disconnected state, or all RTCIceTransport's
   * are in the closed state. */
  if ((any_state & (1 << STATE (NEW))) || all_closed) {
    GST_TRACE_OBJECT (webrtc, "returning new");
    return STATE (NEW);
  }
  /* All RTCIceTransport s are in the connected, completed or closed state
   * and at least one of them is in the connected state. */
  if (any_state & (1 << STATE (CONNECTED) | 1 << STATE (COMPLETED) | 1 <<
          STATE (CLOSED)) && any_state & (1 << STATE (CONNECTED))) {
    GST_TRACE_OBJECT (webrtc, "returning connected");
    return STATE (CONNECTED);
  }
  /* All RTCIceTransport s are in the completed or closed state and at least
   * one of them is in the completed state. */
  if (any_state & (1 << STATE (COMPLETED) | 1 << STATE (CLOSED))
      && any_state & (1 << STATE (COMPLETED))) {
    GST_TRACE_OBJECT (webrtc, "returning connected");
    return STATE (CONNECTED);
  }

  GST_FIXME ("unspecified situation, returning new");
  return STATE (NEW);
#undef STATE
}

/* https://www.w3.org/TR/webrtc/#dom-rtcicegatheringstate */
static GstWebRTCICEGatheringState
_collate_ice_gathering_states (GstWebRTCBin * webrtc)
{
#define STATE(val) GST_WEBRTC_ICE_GATHERING_STATE_ ## val
  GstWebRTCICEGatheringState any_state = 0;
  gboolean all_completed = webrtc->priv->transceivers->len > 0;
  int i;

  for (i = 0; i < webrtc->priv->transceivers->len; i++) {
    GstWebRTCRTPTransceiver *rtp_trans =
        g_array_index (webrtc->priv->transceivers, GstWebRTCRTPTransceiver *,
        i);
    WebRTCTransceiver *trans = WEBRTC_TRANSCEIVER (rtp_trans);
    TransportStream *stream = trans->stream;
    GstWebRTCICETransport *transport, *rtcp_transport;
    GstWebRTCICEGatheringState ice_state;
    gboolean rtcp_mux = FALSE;

    if (rtp_trans->stopped)
      continue;
    if (!rtp_trans->mid)
      continue;

    g_object_get (stream, "rtcp-mux", &rtcp_mux, NULL);

    transport = _transceiver_get_transport (rtp_trans)->transport;

    /* get gathering state */
    g_object_get (transport, "gathering-state", &ice_state, NULL);
    any_state |= (1 << ice_state);
    if (ice_state != STATE (COMPLETE))
      all_completed = FALSE;

    rtcp_transport = _transceiver_get_rtcp_transport (rtp_trans)->transport;

    if (!rtcp_mux && rtcp_transport && rtcp_transport != transport) {
      g_object_get (rtcp_transport, "gathering-state", &ice_state, NULL);
      any_state |= (1 << ice_state);
      if (ice_state != STATE (COMPLETE))
        all_completed = FALSE;
    }
  }

  GST_TRACE_OBJECT (webrtc, "ICE gathering state: 0x%x", any_state);

  /* Any of the RTCIceTransport s are in the gathering state. */
  if (any_state & (1 << STATE (GATHERING))) {
    GST_TRACE_OBJECT (webrtc, "returning gathering");
    return STATE (GATHERING);
  }
  /* At least one RTCIceTransport exists, and all RTCIceTransport s are in
   * the completed gathering state. */
  if (all_completed) {
    GST_TRACE_OBJECT (webrtc, "returning complete");
    return STATE (COMPLETE);
  }

  /* Any of the RTCIceTransport s are in the new gathering state and none
   * of the transports are in the gathering state, or there are no transports. */
  GST_TRACE_OBJECT (webrtc, "returning new");
  return STATE (NEW);
#undef STATE
}

/* https://www.w3.org/TR/webrtc/#rtcpeerconnectionstate-enum */
static GstWebRTCPeerConnectionState
_collate_peer_connection_states (GstWebRTCBin * webrtc)
{
#define STATE(v) GST_WEBRTC_PEER_CONNECTION_STATE_ ## v
#define ICE_STATE(v) GST_WEBRTC_ICE_CONNECTION_STATE_ ## v
#define DTLS_STATE(v) GST_WEBRTC_DTLS_TRANSPORT_STATE_ ## v
  GstWebRTCICEConnectionState any_ice_state = 0;
  GstWebRTCDTLSTransportState any_dtls_state = 0;
  int i;

  for (i = 0; i < webrtc->priv->transceivers->len; i++) {
    GstWebRTCRTPTransceiver *rtp_trans =
        g_array_index (webrtc->priv->transceivers, GstWebRTCRTPTransceiver *,
        i);
    WebRTCTransceiver *trans = WEBRTC_TRANSCEIVER (rtp_trans);
    TransportStream *stream = trans->stream;
    GstWebRTCDTLSTransport *transport, *rtcp_transport;
    GstWebRTCICEGatheringState ice_state;
    GstWebRTCDTLSTransportState dtls_state;
    gboolean rtcp_mux = FALSE;

    if (rtp_trans->stopped)
      continue;
    if (!rtp_trans->mid)
      continue;

    g_object_get (stream, "rtcp-mux", &rtcp_mux, NULL);
    transport = _transceiver_get_transport (rtp_trans);

    /* get transport state */
    g_object_get (transport, "state", &dtls_state, NULL);
    any_dtls_state |= (1 << dtls_state);
    g_object_get (transport->transport, "state", &ice_state, NULL);
    any_ice_state |= (1 << ice_state);

    rtcp_transport = _transceiver_get_rtcp_transport (rtp_trans);

    if (!rtcp_mux && rtcp_transport && rtcp_transport != transport) {
      g_object_get (rtcp_transport, "state", &dtls_state, NULL);
      any_dtls_state |= (1 << dtls_state);
      g_object_get (rtcp_transport->transport, "state", &ice_state, NULL);
      any_ice_state |= (1 << ice_state);
    }
  }

  GST_TRACE_OBJECT (webrtc, "ICE connection state: 0x%x. DTLS connection "
      "state: 0x%x", any_ice_state, any_dtls_state);

  /* The RTCPeerConnection object's [[ isClosed]] slot is true.  */
  if (webrtc->priv->is_closed) {
    GST_TRACE_OBJECT (webrtc, "returning closed");
    return STATE (CLOSED);
  }

  /* Any of the RTCIceTransport s or RTCDtlsTransport s are in a failed state. */
  if (any_ice_state & (1 << ICE_STATE (FAILED))) {
    GST_TRACE_OBJECT (webrtc, "returning failed");
    return STATE (FAILED);
  }
  if (any_dtls_state & (1 << DTLS_STATE (FAILED))) {
    GST_TRACE_OBJECT (webrtc, "returning failed");
    return STATE (FAILED);
  }

  /* Any of the RTCIceTransport's or RTCDtlsTransport's are in the connecting
   * or checking state and none of them is in the failed state. */
  if (any_ice_state & (1 << ICE_STATE (CHECKING))) {
    GST_TRACE_OBJECT (webrtc, "returning connecting");
    return STATE (CONNECTING);
  }
  if (any_dtls_state & (1 << DTLS_STATE (CONNECTING))) {
    GST_TRACE_OBJECT (webrtc, "returning connecting");
    return STATE (CONNECTING);
  }

  /* Any of the RTCIceTransport's or RTCDtlsTransport's are in the disconnected
   * state and none of them are in the failed or connecting or checking state. */
  if (any_ice_state & (1 << ICE_STATE (DISCONNECTED))) {
    GST_TRACE_OBJECT (webrtc, "returning disconnected");
    return STATE (DISCONNECTED);
  }

  /* All RTCIceTransport's and RTCDtlsTransport's are in the connected,
   * completed or closed state and at least of them is in the connected or
   * completed state. */
  if (!(any_ice_state & ~(1 << ICE_STATE (CONNECTED) | 1 <<
              ICE_STATE (COMPLETED) | 1 << ICE_STATE (CLOSED)))
      && !(any_dtls_state & ~(1 << DTLS_STATE (CONNECTED) | 1 <<
              DTLS_STATE (CLOSED)))
      && (any_ice_state & (1 << ICE_STATE (CONNECTED) | 1 <<
              ICE_STATE (COMPLETED))
          || any_dtls_state & (1 << DTLS_STATE (CONNECTED)))) {
    GST_TRACE_OBJECT (webrtc, "returning connected");
    return STATE (CONNECTED);
  }

  /* Any of the RTCIceTransport's or RTCDtlsTransport's are in the new state
   * and none of the transports are in the connecting, checking, failed or
   * disconnected state, or all transports are in the closed state. */
  if (!(any_ice_state & ~(1 << ICE_STATE (CLOSED)))) {
    GST_TRACE_OBJECT (webrtc, "returning new");
    return STATE (NEW);
  }
  if ((any_ice_state & (1 << ICE_STATE (NEW))
          || any_dtls_state & (1 << DTLS_STATE (NEW)))
      && !(any_ice_state & (1 << ICE_STATE (CHECKING) | 1 << ICE_STATE (FAILED)
              | (1 << ICE_STATE (DISCONNECTED))))
      && !(any_dtls_state & (1 << DTLS_STATE (CONNECTING) | 1 <<
              DTLS_STATE (FAILED)))) {
    GST_TRACE_OBJECT (webrtc, "returning new");
    return STATE (NEW);
  }

  GST_FIXME_OBJECT (webrtc, "Undefined situation detected, returning new");
  return STATE (NEW);
#undef DTLS_STATE
#undef ICE_STATE
#undef STATE
}

static void
_update_ice_gathering_state_task (GstWebRTCBin * webrtc, gpointer data)
{
  GstWebRTCICEGatheringState old_state = webrtc->ice_gathering_state;
  GstWebRTCICEGatheringState new_state;

  new_state = _collate_ice_gathering_states (webrtc);

  if (new_state != webrtc->ice_gathering_state) {
    gchar *old_s, *new_s;

    old_s = _enum_value_to_string (GST_TYPE_WEBRTC_ICE_GATHERING_STATE,
        old_state);
    new_s = _enum_value_to_string (GST_TYPE_WEBRTC_ICE_GATHERING_STATE,
        new_state);
    GST_INFO_OBJECT (webrtc, "ICE gathering state change from %s(%u) to %s(%u)",
        old_s, old_state, new_s, new_state);
    g_free (old_s);
    g_free (new_s);

    webrtc->ice_gathering_state = new_state;
    PC_UNLOCK (webrtc);
    g_object_notify (G_OBJECT (webrtc), "ice-gathering-state");
    PC_LOCK (webrtc);
  }
}

static void
_update_ice_gathering_state (GstWebRTCBin * webrtc)
{
  gst_webrtc_bin_enqueue_task (webrtc, _update_ice_gathering_state_task, NULL,
      NULL);
}

static void
_update_ice_connection_state_task (GstWebRTCBin * webrtc, gpointer data)
{
  GstWebRTCICEConnectionState old_state = webrtc->ice_connection_state;
  GstWebRTCICEConnectionState new_state;

  new_state = _collate_ice_connection_states (webrtc);

  if (new_state != old_state) {
    gchar *old_s, *new_s;

    old_s = _enum_value_to_string (GST_TYPE_WEBRTC_ICE_CONNECTION_STATE,
        old_state);
    new_s = _enum_value_to_string (GST_TYPE_WEBRTC_ICE_CONNECTION_STATE,
        new_state);
    GST_INFO_OBJECT (webrtc,
        "ICE connection state change from %s(%u) to %s(%u)", old_s, old_state,
        new_s, new_state);
    g_free (old_s);
    g_free (new_s);

    webrtc->ice_connection_state = new_state;
    PC_UNLOCK (webrtc);
    g_object_notify (G_OBJECT (webrtc), "ice-connection-state");
    PC_LOCK (webrtc);
  }
}

static void
_update_ice_connection_state (GstWebRTCBin * webrtc)
{
  gst_webrtc_bin_enqueue_task (webrtc, _update_ice_connection_state_task, NULL,
      NULL);
}

static void
_update_peer_connection_state_task (GstWebRTCBin * webrtc, gpointer data)
{
  GstWebRTCPeerConnectionState old_state = webrtc->peer_connection_state;
  GstWebRTCPeerConnectionState new_state;

  new_state = _collate_peer_connection_states (webrtc);

  if (new_state != old_state) {
    gchar *old_s, *new_s;

    old_s = _enum_value_to_string (GST_TYPE_WEBRTC_PEER_CONNECTION_STATE,
        old_state);
    new_s = _enum_value_to_string (GST_TYPE_WEBRTC_PEER_CONNECTION_STATE,
        new_state);
    GST_INFO_OBJECT (webrtc,
        "Peer connection state change from %s(%u) to %s(%u)", old_s, old_state,
        new_s, new_state);
    g_free (old_s);
    g_free (new_s);

    webrtc->peer_connection_state = new_state;
    PC_UNLOCK (webrtc);
    g_object_notify (G_OBJECT (webrtc), "connection-state");
    PC_LOCK (webrtc);
  }
}

static void
_update_peer_connection_state (GstWebRTCBin * webrtc)
{
  gst_webrtc_bin_enqueue_task (webrtc, _update_peer_connection_state_task,
      NULL, NULL);
}

static gboolean
_all_sinks_have_caps (GstWebRTCBin * webrtc)
{
  GList *l;
  gboolean res = FALSE;

  GST_OBJECT_LOCK (webrtc);
  l = GST_ELEMENT (webrtc)->pads;
  for (; l; l = g_list_next (l)) {
    if (!GST_IS_WEBRTC_BIN_PAD (l->data))
      continue;
    if (!GST_WEBRTC_BIN_PAD (l->data)->received_caps)
      goto done;
  }

  l = webrtc->priv->pending_pads;
  for (; l; l = g_list_next (l)) {
    if (!GST_IS_WEBRTC_BIN_PAD (l->data))
      goto done;
  }

  res = TRUE;

done:
  GST_OBJECT_UNLOCK (webrtc);
  return res;
}

/* http://w3c.github.io/webrtc-pc/#dfn-check-if-negotiation-is-needed */
static gboolean
_check_if_negotiation_is_needed (GstWebRTCBin * webrtc)
{
  int i;

  GST_LOG_OBJECT (webrtc, "checking if negotiation is needed");

  /* We can't negotiate until we have received caps on all our sink pads,
   * as we will need the ssrcs in our offer / answer */
  if (!_all_sinks_have_caps (webrtc)) {
    GST_LOG_OBJECT (webrtc,
        "no negotiation possible until caps have been received on all sink pads");
    return FALSE;
  }

  /* If any implementation-specific negotiation is required, as described at
   * the start of this section, return "true".
   * FIXME */
  /* FIXME: emit when input caps/format changes? */

  /* If connection has created any RTCDataChannel's, and no m= section has
   * been negotiated yet for data, return "true". 
   * FIXME */

  if (!webrtc->current_local_description) {
    GST_LOG_OBJECT (webrtc, "no local description set");
    return TRUE;
  }

  if (!webrtc->current_remote_description) {
    GST_LOG_OBJECT (webrtc, "no remote description set");
    return TRUE;
  }

  for (i = 0; i < webrtc->priv->transceivers->len; i++) {
    GstWebRTCRTPTransceiver *trans;

    trans =
        g_array_index (webrtc->priv->transceivers, GstWebRTCRTPTransceiver *,
        i);

    if (trans->stopped) {
      /* FIXME: If t is stopped and is associated with an m= section according to
       * [JSEP] (section 3.4.1.), but the associated m= section is not yet
       * rejected in connection's currentLocalDescription or
       * currentRemoteDescription , return "true". */
      GST_FIXME_OBJECT (webrtc,
          "check if the transceiver is rejected in descriptions");
    } else {
      const GstSDPMedia *media;
      GstWebRTCRTPTransceiverDirection local_dir, remote_dir;

      if (trans->mline == -1) {
        GST_LOG_OBJECT (webrtc, "unassociated transceiver %i %" GST_PTR_FORMAT,
            i, trans);
        return TRUE;
      }
      /* internal inconsistency */
      g_assert (trans->mline <
          gst_sdp_message_medias_len (webrtc->current_local_description->sdp));
      g_assert (trans->mline <
          gst_sdp_message_medias_len (webrtc->current_remote_description->sdp));

      /* FIXME: msid handling
       * If t's direction is "sendrecv" or "sendonly", and the associated m=
       * section in connection's currentLocalDescription doesn't contain an
       * "a=msid" line, return "true". */

      media =
          gst_sdp_message_get_media (webrtc->current_local_description->sdp,
          trans->mline);
      local_dir = _get_direction_from_media (media);

      media =
          gst_sdp_message_get_media (webrtc->current_remote_description->sdp,
          trans->mline);
      remote_dir = _get_direction_from_media (media);

      if (webrtc->current_local_description->type == GST_WEBRTC_SDP_TYPE_OFFER) {
        /* If connection's currentLocalDescription if of type "offer", and
         * the direction of the associated m= section in neither the offer
         * nor answer matches t's direction, return "true". */

        if (local_dir != trans->direction && remote_dir != trans->direction) {
          GST_LOG_OBJECT (webrtc,
              "transceiver direction doesn't match description");
          return TRUE;
        }
      } else if (webrtc->current_local_description->type ==
          GST_WEBRTC_SDP_TYPE_ANSWER) {
        GstWebRTCRTPTransceiverDirection intersect_dir;

        /* If connection's currentLocalDescription if of type "answer", and
         * the direction of the associated m= section in the answer does not
         * match t's direction intersected with the offered direction (as
         * described in [JSEP] (section 5.3.1.)), return "true". */

        /* remote is the offer, local is the answer */
        intersect_dir = _intersect_answer_directions (remote_dir, local_dir);

        if (intersect_dir != trans->direction) {
          GST_LOG_OBJECT (webrtc,
              "transceiver direction doesn't match description");
          return TRUE;
        }
      }
    }
  }

  GST_LOG_OBJECT (webrtc, "no negotiation needed");
  return FALSE;
}

static void
_check_need_negotiation_task (GstWebRTCBin * webrtc, gpointer unused)
{
  if (webrtc->priv->need_negotiation) {
    GST_TRACE_OBJECT (webrtc, "emitting on-negotiation-needed");
    PC_UNLOCK (webrtc);
    g_signal_emit (webrtc, gst_webrtc_bin_signals[ON_NEGOTIATION_NEEDED_SIGNAL],
        0);
    PC_LOCK (webrtc);
  }
}

/* http://w3c.github.io/webrtc-pc/#dfn-update-the-negotiation-needed-flag */
static void
_update_need_negotiation (GstWebRTCBin * webrtc)
{
  /* If connection's [[isClosed]] slot is true, abort these steps. */
  if (webrtc->priv->is_closed)
    return;
  /* If connection's signaling state is not "stable", abort these steps. */
  if (webrtc->signaling_state != GST_WEBRTC_SIGNALING_STATE_STABLE)
    return;

  /* If the result of checking if negotiation is needed is "false", clear the
   * negotiation-needed flag by setting connection's [[ needNegotiation]] slot
   * to false, and abort these steps. */
  if (!_check_if_negotiation_is_needed (webrtc)) {
    webrtc->priv->need_negotiation = FALSE;
    return;
  }
  /* If connection's [[needNegotiation]] slot is already true, abort these steps. */
  if (webrtc->priv->need_negotiation)
    return;
  /* Set connection's [[needNegotiation]] slot to true. */
  webrtc->priv->need_negotiation = TRUE;
  /* Queue a task to check connection's [[ needNegotiation]] slot and, if still
   * true, fire a simple event named negotiationneeded at connection. */
  gst_webrtc_bin_enqueue_task (webrtc, _check_need_negotiation_task, NULL,
      NULL);
}

static GstCaps *
_find_codec_preferences (GstWebRTCBin * webrtc, GstWebRTCRTPTransceiver * trans,
    GstPadDirection direction, guint media_idx)
{
  GstCaps *ret = NULL;

  GST_LOG_OBJECT (webrtc, "retreiving codec preferences from %" GST_PTR_FORMAT,
      trans);

  if (trans && trans->codec_preferences) {
    GST_LOG_OBJECT (webrtc, "Using codec preferences: %" GST_PTR_FORMAT,
        trans->codec_preferences);
    ret = gst_caps_ref (trans->codec_preferences);
  } else {
    GstWebRTCBinPad *pad = _find_pad_for_mline (webrtc, direction, media_idx);
    if (pad) {
      GstCaps *caps = NULL;

      if (pad->received_caps) {
        caps = gst_caps_ref (pad->received_caps);
      } else if ((caps = gst_pad_get_current_caps (GST_PAD (pad)))) {
        GST_LOG_OBJECT (webrtc, "Using current pad caps: %" GST_PTR_FORMAT,
            caps);
      } else {
        if ((caps = gst_pad_peer_query_caps (GST_PAD (pad), NULL)))
          GST_LOG_OBJECT (webrtc, "Using peer query caps: %" GST_PTR_FORMAT,
              caps);
      }
      if (caps)
        ret = caps;
      gst_object_unref (pad);
    }
  }

  return ret;
}

static GstCaps *
_add_supported_attributes_to_caps (GstWebRTCBin * webrtc,
    WebRTCTransceiver * trans, const GstCaps * caps)
{
  GstCaps *ret;
  guint i;

  ret = gst_caps_make_writable (caps);

  for (i = 0; i < gst_caps_get_size (ret); i++) {
    GstStructure *s = gst_caps_get_structure (ret, i);

    if (trans->do_nack)
      if (!gst_structure_has_field (s, "rtcp-fb-nack"))
        gst_structure_set (s, "rtcp-fb-nack", G_TYPE_BOOLEAN, TRUE, NULL);

    if (!gst_structure_has_field (s, "rtcp-fb-nack-pli"))
      gst_structure_set (s, "rtcp-fb-nack-pli", G_TYPE_BOOLEAN, TRUE, NULL);
    /* FIXME: is this needed? */
    /*if (!gst_structure_has_field (s, "rtcp-fb-transport-cc"))
       gst_structure_set (s, "rtcp-fb-nack-pli", G_TYPE_BOOLEAN, TRUE, NULL); */

    /* FIXME: codec-specific paramters? */
  }

  return ret;
}

static void
_on_ice_transport_notify_state (GstWebRTCICETransport * transport,
    GParamSpec * pspec, GstWebRTCBin * webrtc)
{
  _update_ice_connection_state (webrtc);
  _update_peer_connection_state (webrtc);
}

static void
_on_ice_transport_notify_gathering_state (GstWebRTCICETransport * transport,
    GParamSpec * pspec, GstWebRTCBin * webrtc)
{
  _update_ice_gathering_state (webrtc);
}

static void
_on_dtls_transport_notify_state (GstWebRTCDTLSTransport * transport,
    GParamSpec * pspec, GstWebRTCBin * webrtc)
{
  _update_peer_connection_state (webrtc);
}

static WebRTCTransceiver *
_create_webrtc_transceiver (GstWebRTCBin * webrtc,
    GstWebRTCRTPTransceiverDirection direction, guint mline)
{
  WebRTCTransceiver *trans;
  GstWebRTCRTPTransceiver *rtp_trans;
  GstWebRTCRTPSender *sender;
  GstWebRTCRTPReceiver *receiver;

  sender = gst_webrtc_rtp_sender_new ();
  receiver = gst_webrtc_rtp_receiver_new ();
  trans = webrtc_transceiver_new (webrtc, sender, receiver);
  rtp_trans = GST_WEBRTC_RTP_TRANSCEIVER (trans);
  rtp_trans->direction = direction;
  rtp_trans->mline = mline;

  g_array_append_val (webrtc->priv->transceivers, trans);

  gst_object_unref (sender);
  gst_object_unref (receiver);

  g_signal_emit (webrtc, gst_webrtc_bin_signals[ON_NEW_TRANSCEIVER_SIGNAL],
      0, trans);

  return trans;
}

static TransportStream *
_create_transport_channel (GstWebRTCBin * webrtc, guint session_id)
{
  GstWebRTCDTLSTransport *transport;
  TransportStream *ret;

  /* FIXME: how to parametrize the sender and the receiver */
  ret = transport_stream_new (webrtc, session_id);
  transport = ret->transport;

  g_signal_connect (G_OBJECT (transport->transport), "notify::state",
      G_CALLBACK (_on_ice_transport_notify_state), webrtc);
  g_signal_connect (G_OBJECT (transport->transport),
      "notify::gathering-state",
      G_CALLBACK (_on_ice_transport_notify_gathering_state), webrtc);
  g_signal_connect (G_OBJECT (transport), "notify::state",
      G_CALLBACK (_on_dtls_transport_notify_state), webrtc);

  if ((transport = ret->rtcp_transport)) {
    g_signal_connect (G_OBJECT (transport->transport),
        "notify::state", G_CALLBACK (_on_ice_transport_notify_state), webrtc);
    g_signal_connect (G_OBJECT (transport->transport),
        "notify::gathering-state",
        G_CALLBACK (_on_ice_transport_notify_gathering_state), webrtc);
    g_signal_connect (G_OBJECT (transport), "notify::state",
        G_CALLBACK (_on_dtls_transport_notify_state), webrtc);
  }

  GST_TRACE_OBJECT (webrtc,
      "Create transport %" GST_PTR_FORMAT " for session %u", ret, session_id);

  return ret;
}

static gboolean
_message_media_is_datachannel (const GstSDPMessage * msg, guint media_id)
{
  const GstSDPMedia *media;

  if (!msg)
    return FALSE;

  if (gst_sdp_message_medias_len (msg) <= media_id)
    return FALSE;

  media = gst_sdp_message_get_media (msg, media_id);

  if (g_strcmp0 (gst_sdp_media_get_media (media), "application") != 0)
    return FALSE;

  if (gst_sdp_media_formats_len (media) != 1)
    return FALSE;

  if (g_strcmp0 (gst_sdp_media_get_format (media, 0),
          "webrtc-datachannel") != 0)
    return FALSE;

  return TRUE;
}

static TransportStream *
_get_or_create_rtp_transport_channel (GstWebRTCBin * webrtc, guint session_id)
{
  TransportStream *ret;
  gchar *pad_name;

  ret = _find_transport_for_session (webrtc, session_id);

  if (!ret) {
    ret = _create_transport_channel (webrtc, session_id);
    gst_bin_add (GST_BIN (webrtc), GST_ELEMENT (ret->send_bin));
    gst_bin_add (GST_BIN (webrtc), GST_ELEMENT (ret->receive_bin));
    g_array_append_val (webrtc->priv->transports, ret);

    pad_name = g_strdup_printf ("recv_rtcp_sink_%u", ret->session_id);
    if (!gst_element_link_pads (GST_ELEMENT (ret->receive_bin), "rtcp_src",
            GST_ELEMENT (webrtc->rtpbin), pad_name))
      g_warn_if_reached ();
    g_free (pad_name);

    pad_name = g_strdup_printf ("send_rtcp_src_%u", ret->session_id);
    if (!gst_element_link_pads (GST_ELEMENT (webrtc->rtpbin), pad_name,
            GST_ELEMENT (ret->send_bin), "rtcp_sink"))
      g_warn_if_reached ();
    g_free (pad_name);
  }

  gst_element_sync_state_with_parent (GST_ELEMENT (ret->send_bin));
  gst_element_sync_state_with_parent (GST_ELEMENT (ret->receive_bin));

  return ret;
}

/* this is called from the webrtc thread with the pc lock held */
static void
_on_data_channel_ready_state (GstWebRTCDataChannel * channel,
    GParamSpec * pspec, GstWebRTCBin * webrtc)
{
  GstWebRTCDataChannelState ready_state;
  guint i;

  g_object_get (channel, "ready-state", &ready_state, NULL);

  if (ready_state == GST_WEBRTC_DATA_CHANNEL_STATE_OPEN) {
    gboolean found = FALSE;

    for (i = 0; i < webrtc->priv->pending_data_channels->len; i++) {
      GstWebRTCDataChannel *c;

      c = g_array_index (webrtc->priv->pending_data_channels,
          GstWebRTCDataChannel *, i);
      if (c == channel) {
        found = TRUE;
        g_array_remove_index (webrtc->priv->pending_data_channels, i);
        break;
      }
    }
    if (found == FALSE) {
      GST_FIXME_OBJECT (webrtc, "Received open for unknown data channel");
      return;
    }

    g_array_append_val (webrtc->priv->data_channels, channel);

    g_signal_emit (webrtc, gst_webrtc_bin_signals[ON_DATA_CHANNEL_SIGNAL], 0,
        gst_object_ref (channel));
  }
}

static void
_link_data_channel_to_sctp (GstWebRTCBin * webrtc,
    GstWebRTCDataChannel * channel)
{
  if (webrtc->priv->sctp_transport && !channel->sctp_transport) {
    gint id;

    g_object_get (channel, "id", &id, NULL);

    if (webrtc->priv->sctp_transport->association_established && id != -1) {
      gchar *pad_name;

      gst_webrtc_data_channel_set_sctp_transport (channel,
          webrtc->priv->sctp_transport);
      pad_name = g_strdup_printf ("sink_%u", id);
      if (!gst_element_link_pads (channel->appsrc, "src",
              channel->sctp_transport->sctpenc, pad_name))
        g_warn_if_reached ();
      g_free (pad_name);
    }
  }
}

static void
_on_sctpdec_pad_added (GstElement * sctpdec, GstPad * pad,
    GstWebRTCBin * webrtc)
{
  GstWebRTCDataChannel *channel;
  guint stream_id;
  GstPad *sink_pad;

  if (sscanf (GST_PAD_NAME (pad), "src_%u", &stream_id) != 1)
    return;

  PC_LOCK (webrtc);
  channel = _find_data_channel_for_id (webrtc, stream_id);
  if (!channel) {
    channel = g_object_new (GST_TYPE_WEBRTC_DATA_CHANNEL, NULL);
    channel->id = stream_id;
    channel->webrtcbin = webrtc;

    gst_bin_add (GST_BIN (webrtc), channel->appsrc);
    gst_bin_add (GST_BIN (webrtc), channel->appsink);

    gst_element_sync_state_with_parent (channel->appsrc);
    gst_element_sync_state_with_parent (channel->appsink);

    _link_data_channel_to_sctp (webrtc, channel);

    g_array_append_val (webrtc->priv->pending_data_channels, channel);
  }

  g_signal_connect (channel, "notify::ready-state",
      G_CALLBACK (_on_data_channel_ready_state), webrtc);

  sink_pad = gst_element_get_static_pad (channel->appsink, "sink");
  if (gst_pad_link (pad, sink_pad) != GST_PAD_LINK_OK)
    GST_WARNING_OBJECT (channel, "Failed to link sctp pad %s with channel %"
        GST_PTR_FORMAT, GST_PAD_NAME (pad), channel);
  gst_object_unref (sink_pad);
  PC_UNLOCK (webrtc);
}

static void
_on_sctp_state_notify (GstWebRTCSCTPTransport * sctp, GParamSpec * pspec,
    GstWebRTCBin * webrtc)
{
  GstWebRTCSCTPTransportState state;

  g_object_get (sctp, "state", &state, NULL);

  if (state == GST_WEBRTC_SCTP_TRANSPORT_STATE_CONNECTED) {
    int i;

    PC_LOCK (webrtc);
    GST_DEBUG_OBJECT (webrtc, "SCTP association established");

    for (i = 0; i < webrtc->priv->data_channels->len; i++) {
      GstWebRTCDataChannel *channel;

      channel =
          g_array_index (webrtc->priv->data_channels, GstWebRTCDataChannel *,
          i);

      _link_data_channel_to_sctp (webrtc, channel);

      if (!channel->negotiated && !channel->opened)
        gst_webrtc_data_channel_start_negotiation (channel);
    }
    PC_UNLOCK (webrtc);
  }
}

static TransportStream *
_get_or_create_data_channel_transports (GstWebRTCBin * webrtc, guint session_id)
{
  if (!webrtc->priv->data_channel_transport) {
    TransportStream *stream;
    GstWebRTCSCTPTransport *sctp_transport;
    int i;

    stream = _find_transport_for_session (webrtc, session_id);

    if (!stream) {
      stream = _create_transport_channel (webrtc, session_id);
      gst_bin_add (GST_BIN (webrtc), GST_ELEMENT (stream->send_bin));
      gst_bin_add (GST_BIN (webrtc), GST_ELEMENT (stream->receive_bin));
      g_array_append_val (webrtc->priv->transports, stream);
    }

    webrtc->priv->data_channel_transport = stream;

    g_object_set (stream, "rtcp-mux", TRUE, NULL);

    if (!(sctp_transport = webrtc->priv->sctp_transport)) {
      sctp_transport = gst_webrtc_sctp_transport_new ();
      sctp_transport->transport =
          g_object_ref (webrtc->priv->data_channel_transport->transport);
      sctp_transport->webrtcbin = webrtc;

      gst_bin_add (GST_BIN (webrtc), sctp_transport->sctpdec);
      gst_bin_add (GST_BIN (webrtc), sctp_transport->sctpenc);
    }

    g_signal_connect (sctp_transport->sctpdec, "pad-added",
        G_CALLBACK (_on_sctpdec_pad_added), webrtc);
    g_signal_connect (sctp_transport, "notify::state",
        G_CALLBACK (_on_sctp_state_notify), webrtc);

    if (!gst_element_link_pads (GST_ELEMENT (stream->receive_bin), "data_src",
            GST_ELEMENT (sctp_transport->sctpdec), "sink"))
      g_warn_if_reached ();

    if (!gst_element_link_pads (GST_ELEMENT (sctp_transport->sctpenc), "src",
            GST_ELEMENT (stream->send_bin), "data_sink"))
      g_warn_if_reached ();

    for (i = 0; i < webrtc->priv->data_channels->len; i++) {
      GstWebRTCDataChannel *channel;

      channel =
          g_array_index (webrtc->priv->data_channels, GstWebRTCDataChannel *,
          i);

      _link_data_channel_to_sctp (webrtc, channel);
    }

    gst_element_sync_state_with_parent (GST_ELEMENT (stream->send_bin));
    gst_element_sync_state_with_parent (GST_ELEMENT (stream->receive_bin));

    if (!webrtc->priv->sctp_transport) {
      gst_element_sync_state_with_parent (GST_ELEMENT
          (sctp_transport->sctpdec));
      gst_element_sync_state_with_parent (GST_ELEMENT
          (sctp_transport->sctpenc));
    }

    webrtc->priv->sctp_transport = sctp_transport;
  }

  return webrtc->priv->data_channel_transport;
}

static TransportStream *
_get_or_create_transport_stream (GstWebRTCBin * webrtc, guint session_id,
    gboolean is_datachannel)
{
  if (is_datachannel)
    return _get_or_create_data_channel_transports (webrtc, session_id);
  else
    return _get_or_create_rtp_transport_channel (webrtc, session_id);
}

static guint
g_array_find_uint (GArray * array, guint val)
{
  guint i;

  for (i = 0; i < array->len; i++) {
    if (g_array_index (array, guint, i) == val)
      return i;
  }

  return G_MAXUINT;
}

static gboolean
_pick_available_pt (GArray * reserved_pts, guint * i)
{
  gboolean ret = FALSE;

  for (*i = 96; *i <= 127; (*i)++) {
    if (g_array_find_uint (reserved_pts, *i) == G_MAXUINT) {
      g_array_append_val (reserved_pts, *i);
      ret = TRUE;
      break;
    }
  }

  return ret;
}

static gboolean
_pick_fec_payload_types (GstWebRTCBin * webrtc, WebRTCTransceiver * trans,
    GArray * reserved_pts, gint clockrate, gint * rtx_target_pt,
    GstSDPMedia * media)
{
  gboolean ret = TRUE;

  if (trans->fec_type == GST_WEBRTC_FEC_TYPE_NONE)
    goto done;

  if (trans->fec_type == GST_WEBRTC_FEC_TYPE_ULP_RED && clockrate != -1) {
    guint pt;
    gchar *str;

    if (!(ret = _pick_available_pt (reserved_pts, &pt)))
      goto done;

    /* https://tools.ietf.org/html/rfc5109#section-14.1 */

    str = g_strdup_printf ("%u", pt);
    gst_sdp_media_add_format (media, str);
    g_free (str);
    str = g_strdup_printf ("%u red/%d", pt, clockrate);
    gst_sdp_media_add_attribute (media, "rtpmap", str);
    g_free (str);

    *rtx_target_pt = pt;

    if (!(ret = _pick_available_pt (reserved_pts, &pt)))
      goto done;

    str = g_strdup_printf ("%u", pt);
    gst_sdp_media_add_format (media, str);
    g_free (str);
    str = g_strdup_printf ("%u ulpfec/%d", pt, clockrate);
    gst_sdp_media_add_attribute (media, "rtpmap", str);
    g_free (str);
  }

done:
  return ret;
}

static gboolean
_pick_rtx_payload_types (GstWebRTCBin * webrtc, WebRTCTransceiver * trans,
    GArray * reserved_pts, gint clockrate, gint target_pt, guint target_ssrc,
    GstSDPMedia * media)
{
  gboolean ret = TRUE;

  if (trans->local_rtx_ssrc_map)
    gst_structure_free (trans->local_rtx_ssrc_map);

  trans->local_rtx_ssrc_map =
      gst_structure_new_empty ("application/x-rtp-ssrc-map");

  if (trans->do_nack) {
    guint pt;
    gchar *str;

    if (!(ret = _pick_available_pt (reserved_pts, &pt)))
      goto done;

    /* https://tools.ietf.org/html/rfc4588#section-8.6 */

    str = g_strdup_printf ("%u", target_ssrc);
    gst_structure_set (trans->local_rtx_ssrc_map, str, G_TYPE_UINT,
        g_random_int (), NULL);
    g_free (str);

    str = g_strdup_printf ("%u", pt);
    gst_sdp_media_add_format (media, str);
    g_free (str);

    str = g_strdup_printf ("%u rtx/%d", pt, clockrate);
    gst_sdp_media_add_attribute (media, "rtpmap", str);
    g_free (str);

    str = g_strdup_printf ("%u apt=%d", pt, target_pt);
    gst_sdp_media_add_attribute (media, "fmtp", str);
    g_free (str);
  }

done:
  return ret;
}

/* https://tools.ietf.org/html/rfc5576#section-4.2 */
static gboolean
_media_add_rtx_ssrc_group (GQuark field_id, const GValue * value,
    GstSDPMedia * media)
{
  gchar *str;

  str =
      g_strdup_printf ("FID %s %u", g_quark_to_string (field_id),
      g_value_get_uint (value));
  gst_sdp_media_add_attribute (media, "ssrc-group", str);

  g_free (str);

  return TRUE;
}

typedef struct
{
  GstSDPMedia *media;
  GstWebRTCBin *webrtc;
  WebRTCTransceiver *trans;
} RtxSsrcData;

static gboolean
_media_add_rtx_ssrc (GQuark field_id, const GValue * value, RtxSsrcData * data)
{
  gchar *str;
  GstStructure *sdes;
  const gchar *cname;

  g_object_get (data->webrtc->rtpbin, "sdes", &sdes, NULL);
  /* http://www.freesoft.org/CIE/RFC/1889/24.htm */
  cname = gst_structure_get_string (sdes, "cname");

  /* https://tools.ietf.org/html/draft-ietf-mmusic-msid-16 */
  str =
      g_strdup_printf ("%u msid:%s %s", g_value_get_uint (value),
      cname, GST_OBJECT_NAME (data->trans));
  gst_sdp_media_add_attribute (data->media, "ssrc", str);
  g_free (str);

  str = g_strdup_printf ("%u cname:%s", g_value_get_uint (value), cname);
  gst_sdp_media_add_attribute (data->media, "ssrc", str);
  g_free (str);

  gst_structure_free (sdes);

  return TRUE;
}

static void
_media_add_ssrcs (GstSDPMedia * media, GstCaps * caps, GstWebRTCBin * webrtc,
    WebRTCTransceiver * trans)
{
  guint i;
  RtxSsrcData data = { media, webrtc, trans };
  const gchar *cname;
  GstStructure *sdes;

  g_object_get (webrtc->rtpbin, "sdes", &sdes, NULL);
  /* http://www.freesoft.org/CIE/RFC/1889/24.htm */
  cname = gst_structure_get_string (sdes, "cname");

  if (trans->local_rtx_ssrc_map)
    gst_structure_foreach (trans->local_rtx_ssrc_map,
        (GstStructureForeachFunc) _media_add_rtx_ssrc_group, media);

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    const GstStructure *s = gst_caps_get_structure (caps, i);
    guint ssrc;

    if (gst_structure_get_uint (s, "ssrc", &ssrc)) {
      gchar *str;

      /* https://tools.ietf.org/html/draft-ietf-mmusic-msid-16 */
      str =
          g_strdup_printf ("%u msid:%s %s", ssrc, cname,
          GST_OBJECT_NAME (trans));
      gst_sdp_media_add_attribute (media, "ssrc", str);
      g_free (str);

      str = g_strdup_printf ("%u cname:%s", ssrc, cname);
      gst_sdp_media_add_attribute (media, "ssrc", str);
      g_free (str);
    }
  }

  gst_structure_free (sdes);

  if (trans->local_rtx_ssrc_map)
    gst_structure_foreach (trans->local_rtx_ssrc_map,
        (GstStructureForeachFunc) _media_add_rtx_ssrc, &data);
}

/* based off https://tools.ietf.org/html/draft-ietf-rtcweb-jsep-18#section-5.2.1 */
static gboolean
sdp_media_from_transceiver (GstWebRTCBin * webrtc, GstSDPMedia * media,
    GstWebRTCRTPTransceiver * trans, GstWebRTCSDPType type, guint media_idx,
    GString * bundled_mids, guint bundle_idx, gboolean bundle_only)
{
  /* TODO:
   * rtp header extensions
   * ice attributes
   * rtx
   * fec
   * msid-semantics
   * msid
   * dtls fingerprints
   * multiple dtls fingerprints https://tools.ietf.org/html/draft-ietf-mmusic-4572-update-05
   */
  gchar *direction, *sdp_mid;
  GstCaps *caps;
  int i;

  /* "An m= section is generated for each RtpTransceiver that has been added
   * to the Bin, excluding any stopped RtpTransceivers." */
  if (trans->stopped)
    return FALSE;
  if (trans->direction == GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_NONE
      || trans->direction == GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_INACTIVE)
    return FALSE;

  gst_sdp_media_set_port_info (media, bundle_only ? 0 : 9, 0);
  gst_sdp_media_set_proto (media, "UDP/TLS/RTP/SAVPF");
  gst_sdp_media_add_connection (media, "IN", "IP4", "0.0.0.0", 0, 0);

  if (bundle_only) {
    gst_sdp_media_add_attribute (media, "bundle-only", NULL);
  }

  /* FIXME: negotiate this */
  /* FIXME: when bundle_only, these should not be added:
   * https://tools.ietf.org/html/draft-ietf-mmusic-sdp-bundle-negotiation-52#section-7.1.3
   * However, this causes incompatibilities with current versions
   * of the major browsers */
  gst_sdp_media_add_attribute (media, "rtcp-mux", "");
  gst_sdp_media_add_attribute (media, "rtcp-rsize", NULL);

  direction =
      _enum_value_to_string (GST_TYPE_WEBRTC_RTP_TRANSCEIVER_DIRECTION,
      trans->direction);
  gst_sdp_media_add_attribute (media, direction, "");
  g_free (direction);

  if (type == GST_WEBRTC_SDP_TYPE_OFFER) {
    caps = _find_codec_preferences (webrtc, trans, GST_PAD_SINK, media_idx);
    caps =
        _add_supported_attributes_to_caps (webrtc, WEBRTC_TRANSCEIVER (trans),
        caps);
  } else if (type == GST_WEBRTC_SDP_TYPE_ANSWER) {
    caps = _find_codec_preferences (webrtc, trans, GST_PAD_SRC, media_idx);
    /* FIXME: add rtcp-fb paramaters */
  } else {
    g_assert_not_reached ();
  }

  if (!caps || gst_caps_is_empty (caps) || gst_caps_is_any (caps)) {
    GST_WARNING_OBJECT (webrtc, "no caps available for transceiver, skipping");
    if (caps)
      gst_caps_unref (caps);
    return FALSE;
  }

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstCaps *format = gst_caps_new_empty ();
    const GstStructure *s = gst_caps_get_structure (caps, i);

    gst_caps_append_structure (format, gst_structure_copy (s));

    GST_DEBUG_OBJECT (webrtc, "Adding %u-th caps %" GST_PTR_FORMAT
        " to %u-th media", i, format, media_idx);

    /* this only looks at the first structure so we loop over the given caps
     * and add each structure inside it piecemeal */
    gst_sdp_media_set_media_from_caps (format, media);

    gst_caps_unref (format);
  }

  if (type == GST_WEBRTC_SDP_TYPE_OFFER) {
    GArray *reserved_pts = g_array_new (FALSE, FALSE, sizeof (guint));
    const GstStructure *s = gst_caps_get_structure (caps, 0);
    gint clockrate = -1;
    gint rtx_target_pt;
    gint original_rtx_target_pt;        /* Workaround chrome bug: https://bugs.chromium.org/p/webrtc/issues/detail?id=6196 */
    guint rtx_target_ssrc = -1;

    if (gst_structure_get_int (s, "payload", &rtx_target_pt))
      g_array_append_val (reserved_pts, rtx_target_pt);

    original_rtx_target_pt = rtx_target_pt;

    if (!gst_structure_get_int (s, "clock-rate", &clockrate))
      GST_WARNING_OBJECT (webrtc,
          "Caps %" GST_PTR_FORMAT " are missing clock-rate", caps);
    if (!gst_structure_get_uint (s, "ssrc", &rtx_target_ssrc))
      GST_WARNING_OBJECT (webrtc, "Caps %" GST_PTR_FORMAT " are missing ssrc",
          caps);

    _pick_fec_payload_types (webrtc, WEBRTC_TRANSCEIVER (trans), reserved_pts,
        clockrate, &rtx_target_pt, media);
    _pick_rtx_payload_types (webrtc, WEBRTC_TRANSCEIVER (trans), reserved_pts,
        clockrate, rtx_target_pt, rtx_target_ssrc, media);
    if (original_rtx_target_pt != rtx_target_pt)
      _pick_rtx_payload_types (webrtc, WEBRTC_TRANSCEIVER (trans), reserved_pts,
          clockrate, original_rtx_target_pt, rtx_target_ssrc, media);
    g_array_free (reserved_pts, TRUE);
  }

  _media_add_ssrcs (media, caps, webrtc, WEBRTC_TRANSCEIVER (trans));

  /* Some identifier; we also add the media name to it so it's identifiable */
  sdp_mid = g_strdup_printf ("%s%u", gst_sdp_media_get_media (media),
      webrtc->priv->media_counter++);
  gst_sdp_media_add_attribute (media, "mid", sdp_mid);
  g_free (sdp_mid);

  if (trans->sender) {
    gchar *cert, *fingerprint, *val;

    if (!trans->sender->transport) {
      TransportStream *item;

      item =
          _get_or_create_transport_stream (webrtc,
          bundled_mids ? bundle_idx : media_idx, FALSE);

      webrtc_transceiver_set_transport (WEBRTC_TRANSCEIVER (trans), item);
    }

    g_object_get (trans->sender->transport, "certificate", &cert, NULL);

    fingerprint =
        _generate_fingerprint_from_certificate (cert, G_CHECKSUM_SHA256);
    g_free (cert);
    val =
        g_strdup_printf ("%s %s",
        _g_checksum_to_webrtc_string (G_CHECKSUM_SHA256), fingerprint);
    g_free (fingerprint);

    gst_sdp_media_add_attribute (media, "fingerprint", val);
    g_free (val);
  }

  gst_caps_unref (caps);

  return TRUE;
}

/* TODO: use the options argument */
static GstSDPMessage *
_create_offer_task (GstWebRTCBin * webrtc, const GstStructure * options)
{
  GstSDPMessage *ret;
  int i;
  GString *bundled_mids = NULL;
  gchar *bundle_ufrag = NULL;
  gchar *bundle_pwd = NULL;

  gst_sdp_message_new (&ret);

  gst_sdp_message_set_version (ret, "0");
  {
    /* FIXME: session id and version need special handling depending on the state we're in */
    gchar *sess_id = g_strdup_printf ("%" G_GUINT64_FORMAT, RANDOM_SESSION_ID);
    gst_sdp_message_set_origin (ret, "-", sess_id, "0", "IN", "IP4", "0.0.0.0");
    g_free (sess_id);
  }
  gst_sdp_message_set_session_name (ret, "-");
  gst_sdp_message_add_time (ret, "0", "0", NULL);
  gst_sdp_message_add_attribute (ret, "ice-options", "trickle");

  if (webrtc->bundle_policy == GST_WEBRTC_BUNDLE_POLICY_MAX_BUNDLE) {
    bundled_mids = g_string_new ("BUNDLE");
  } else if (webrtc->bundle_policy == GST_WEBRTC_BUNDLE_POLICY_MAX_COMPAT) {
    bundled_mids = g_string_new ("BUNDLE");
  }

  if (webrtc->bundle_policy != GST_WEBRTC_BUNDLE_POLICY_NONE) {
    _generate_ice_credentials (&bundle_ufrag, &bundle_pwd);
  }

  /* for each rtp transceiver */
  for (i = 0; i < webrtc->priv->transceivers->len; i++) {
    GstWebRTCRTPTransceiver *trans;
    GstSDPMedia media = { 0, };
    gchar *ufrag, *pwd;
    gboolean bundle_only = bundled_mids
        && webrtc->bundle_policy == GST_WEBRTC_BUNDLE_POLICY_MAX_BUNDLE
        && i != 0;

    trans =
        g_array_index (webrtc->priv->transceivers, GstWebRTCRTPTransceiver *,
        i);

    gst_sdp_media_init (&media);
    /* mandated by JSEP */
    gst_sdp_media_add_attribute (&media, "setup", "actpass");

    /* FIXME: only needed when restarting ICE */
    if (webrtc->bundle_policy == GST_WEBRTC_BUNDLE_POLICY_NONE) {
      _generate_ice_credentials (&ufrag, &pwd);
    } else {
      ufrag = g_strdup (bundle_ufrag);
      pwd = g_strdup (bundle_pwd);
    }

    gst_sdp_media_add_attribute (&media, "ice-ufrag", ufrag);
    gst_sdp_media_add_attribute (&media, "ice-pwd", pwd);
    g_free (ufrag);
    g_free (pwd);

    if (sdp_media_from_transceiver (webrtc, &media, trans,
            GST_WEBRTC_SDP_TYPE_OFFER, i, bundled_mids, 0, bundle_only)) {
      if (bundled_mids) {
        const gchar *mid = gst_sdp_media_get_attribute_val (&media, "mid");

        g_assert (mid);
        g_string_append_printf (bundled_mids, " %s", mid);
      }
      gst_sdp_message_add_media (ret, &media);
    } else {
      gst_sdp_media_uninit (&media);
    }
  }

  /* add data channel support */
  if (webrtc->priv->data_channels->len > 0) {
    GstSDPMedia media = { 0, };
    gchar *ufrag, *pwd, *sdp_mid;
    gboolean bundle_only = bundled_mids
        && webrtc->bundle_policy == GST_WEBRTC_BUNDLE_POLICY_MAX_BUNDLE
        && webrtc->priv->transceivers->len != 0;

    gst_sdp_media_init (&media);
    /* mandated by JSEP */
    gst_sdp_media_add_attribute (&media, "setup", "actpass");

    /* FIXME: only needed when restarting ICE */
    if (webrtc->bundle_policy == GST_WEBRTC_BUNDLE_POLICY_NONE) {
      _generate_ice_credentials (&ufrag, &pwd);
    } else {
      ufrag = g_strdup (bundle_ufrag);
      pwd = g_strdup (bundle_pwd);
    }
    gst_sdp_media_add_attribute (&media, "ice-ufrag", ufrag);
    gst_sdp_media_add_attribute (&media, "ice-pwd", pwd);
    g_free (ufrag);
    g_free (pwd);

    gst_sdp_media_set_media (&media, "application");
    gst_sdp_media_set_port_info (&media, bundle_only ? 0 : 9, 0);
    gst_sdp_media_set_proto (&media, "UDP/DTLS/SCTP");
    gst_sdp_media_add_connection (&media, "IN", "IP4", "0.0.0.0", 0, 0);
    gst_sdp_media_add_format (&media, "webrtc-datachannel");

    if (bundle_only)
      gst_sdp_media_add_attribute (&media, "bundle-only", NULL);

    sdp_mid = g_strdup_printf ("%s%u", gst_sdp_media_get_media (&media),
        webrtc->priv->media_counter++);
    gst_sdp_media_add_attribute (&media, "mid", sdp_mid);
    if (bundled_mids)
      g_string_append_printf (bundled_mids, " %s", sdp_mid);
    g_free (sdp_mid);

    /* FIXME: negotiate this properly */
    gst_sdp_media_add_attribute (&media, "sctp-port", "5000");

    _get_or_create_data_channel_transports (webrtc,
        bundled_mids ? 0 : webrtc->priv->transceivers->len);
    {
      gchar *cert, *fingerprint, *val;

      g_object_get (webrtc->priv->sctp_transport->transport, "certificate",
          &cert, NULL);

      fingerprint =
          _generate_fingerprint_from_certificate (cert, G_CHECKSUM_SHA256);
      g_free (cert);
      val =
          g_strdup_printf ("%s %s",
          _g_checksum_to_webrtc_string (G_CHECKSUM_SHA256), fingerprint);
      g_free (fingerprint);

      gst_sdp_media_add_attribute (&media, "fingerprint", val);
      g_free (val);
    }

    gst_sdp_message_add_media (ret, &media);
  }

  if (bundled_mids) {
    gchar *mids = g_string_free (bundled_mids, FALSE);

    gst_sdp_message_add_attribute (ret, "group", mids);
    g_free (mids);
  }

  if (bundle_ufrag)
    g_free (bundle_ufrag);

  if (bundle_pwd)
    g_free (bundle_pwd);

  /* FIXME: pre-emptively setup receiving elements when needed */

  /* XXX: only true for the initial offerer */
  g_object_set (webrtc->priv->ice, "controller", TRUE, NULL);

  return ret;
}

static void
_media_add_fec (GstSDPMedia * media, WebRTCTransceiver * trans, GstCaps * caps,
    gint * rtx_target_pt)
{
  guint i;

  if (trans->fec_type == GST_WEBRTC_FEC_TYPE_NONE)
    return;

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    const GstStructure *s = gst_caps_get_structure (caps, i);

    if (gst_structure_has_name (s, "application/x-rtp")) {
      const gchar *encoding_name =
          gst_structure_get_string (s, "encoding-name");
      gint clock_rate;
      gint pt;

      if (gst_structure_get_int (s, "clock-rate", &clock_rate) &&
          gst_structure_get_int (s, "payload", &pt)) {
        if (!g_strcmp0 (encoding_name, "RED")) {
          gchar *str;

          str = g_strdup_printf ("%u", pt);
          gst_sdp_media_add_format (media, str);
          g_free (str);
          str = g_strdup_printf ("%u red/%d", pt, clock_rate);
          *rtx_target_pt = pt;
          gst_sdp_media_add_attribute (media, "rtpmap", str);
          g_free (str);
        } else if (!g_strcmp0 (encoding_name, "ULPFEC")) {
          gchar *str;

          str = g_strdup_printf ("%u", pt);
          gst_sdp_media_add_format (media, str);
          g_free (str);
          str = g_strdup_printf ("%u ulpfec/%d", pt, clock_rate);
          gst_sdp_media_add_attribute (media, "rtpmap", str);
          g_free (str);
        }
      }
    }
  }
}

static void
_media_add_rtx (GstSDPMedia * media, WebRTCTransceiver * trans,
    GstCaps * offer_caps, gint target_pt, guint target_ssrc)
{
  guint i;
  const GstStructure *s;

  if (trans->local_rtx_ssrc_map)
    gst_structure_free (trans->local_rtx_ssrc_map);

  trans->local_rtx_ssrc_map =
      gst_structure_new_empty ("application/x-rtp-ssrc-map");

  for (i = 0; i < gst_caps_get_size (offer_caps); i++) {
    s = gst_caps_get_structure (offer_caps, i);

    if (gst_structure_has_name (s, "application/x-rtp")) {
      const gchar *encoding_name =
          gst_structure_get_string (s, "encoding-name");
      const gchar *apt_str = gst_structure_get_string (s, "apt");
      gint apt;
      gint clock_rate;
      gint pt;

      if (!apt_str)
        continue;

      apt = atoi (apt_str);

      if (gst_structure_get_int (s, "clock-rate", &clock_rate) &&
          gst_structure_get_int (s, "payload", &pt) && apt == target_pt) {
        if (!g_strcmp0 (encoding_name, "RTX")) {
          gchar *str;

          str = g_strdup_printf ("%u", pt);
          gst_sdp_media_add_format (media, str);
          g_free (str);
          str = g_strdup_printf ("%u rtx/%d", pt, clock_rate);
          gst_sdp_media_add_attribute (media, "rtpmap", str);
          g_free (str);

          str = g_strdup_printf ("%d apt=%d", pt, apt);
          gst_sdp_media_add_attribute (media, "fmtp", str);
          g_free (str);

          str = g_strdup_printf ("%u", target_ssrc);
          gst_structure_set (trans->local_rtx_ssrc_map, str, G_TYPE_UINT,
              g_random_int (), NULL);
        }
      }
    }
  }
}

static void
_get_rtx_target_pt_and_ssrc_from_caps (GstCaps * answer_caps, gint * target_pt,
    guint * target_ssrc)
{
  const GstStructure *s = gst_caps_get_structure (answer_caps, 0);

  gst_structure_get_int (s, "payload", target_pt);
  gst_structure_get_uint (s, "ssrc", target_ssrc);
}

static gboolean
_parse_bundle (GstWebRTCBin * webrtc, GstSDPMessage * sdp, GStrv * bundled)
{
  const gchar *group;
  gboolean ret = FALSE;

  group = gst_sdp_message_get_attribute_val (sdp, "group");

  if (group && g_str_has_prefix (group, "BUNDLE ")) {
    *bundled = g_strsplit (group + strlen ("BUNDLE "), " ", 0);

    if (!(*bundled)[0]) {
      GST_ERROR_OBJECT (webrtc,
          "Invalid format for BUNDLE group, expected at least one mid (%s)",
          group);
      goto done;
    }
  } else {
    ret = TRUE;
    goto done;
  }

  ret = TRUE;

done:
  return ret;
}

static gboolean
_get_bundle_index (GstSDPMessage * sdp, GStrv bundled, guint * idx)
{
  gboolean ret = FALSE;
  guint i;

  for (i = 0; i < gst_sdp_message_medias_len (sdp); i++) {
    const GstSDPMedia *media = gst_sdp_message_get_media (sdp, i);
    const gchar *mid = gst_sdp_media_get_attribute_val (media, "mid");

    if (!g_strcmp0 (mid, bundled[0])) {
      *idx = i;
      ret = TRUE;
      break;
    }
  }

  return ret;
}

/* TODO: use the options argument */
static GstSDPMessage *
_create_answer_task (GstWebRTCBin * webrtc, const GstStructure * options)
{
  GstSDPMessage *ret = NULL;
  const GstWebRTCSessionDescription *pending_remote =
      webrtc->pending_remote_description;
  guint i;
  GStrv bundled = NULL;
  guint bundle_idx = 0;
  GString *bundled_mids = NULL;
  gchar *bundle_ufrag = NULL;
  gchar *bundle_pwd = NULL;

  if (!webrtc->pending_remote_description) {
    GST_ERROR_OBJECT (webrtc,
        "Asked to create an answer without a remote description");
    return NULL;
  }

  if (!_parse_bundle (webrtc, pending_remote->sdp, &bundled))
    goto out;

  if (bundled) {
    if (!_get_bundle_index (pending_remote->sdp, bundled, &bundle_idx)) {
      GST_ERROR_OBJECT (webrtc, "Bundle tag is %s but no media found matching",
          bundled[0]);
      goto out;
    }

    if (webrtc->bundle_policy != GST_WEBRTC_BUNDLE_POLICY_NONE) {
      bundled_mids = g_string_new ("BUNDLE");
    }

    _generate_ice_credentials (&bundle_ufrag, &bundle_pwd);
  }

  gst_sdp_message_new (&ret);

  /* FIXME: session id and version need special handling depending on the state we're in */
  gst_sdp_message_set_version (ret, "0");
  {
    const GstSDPOrigin *offer_origin =
        gst_sdp_message_get_origin (pending_remote->sdp);
    gst_sdp_message_set_origin (ret, "-", offer_origin->sess_id, "0", "IN",
        "IP4", "0.0.0.0");
  }
  gst_sdp_message_set_session_name (ret, "-");

  for (i = 0; i < gst_sdp_message_attributes_len (pending_remote->sdp); i++) {
    const GstSDPAttribute *attr =
        gst_sdp_message_get_attribute (pending_remote->sdp, i);

    if (g_strcmp0 (attr->key, "ice-options") == 0) {
      gst_sdp_message_add_attribute (ret, attr->key, attr->value);
    }
  }

  for (i = 0; i < gst_sdp_message_medias_len (pending_remote->sdp); i++) {
    GstSDPMedia *media = NULL;
    GstSDPMedia *offer_media;
    GstWebRTCRTPTransceiver *rtp_trans = NULL;
    WebRTCTransceiver *trans = NULL;
    GstWebRTCRTPTransceiverDirection offer_dir, answer_dir;
    GstWebRTCDTLSSetup offer_setup, answer_setup;
    GstCaps *offer_caps, *answer_caps = NULL;
    gchar *cert;
    guint j;
    guint k;
    gint target_pt = -1;
    gint original_target_pt = -1;
    guint target_ssrc = 0;
    gboolean bundle_only;

    offer_media =
        (GstSDPMedia *) gst_sdp_message_get_media (pending_remote->sdp, i);
    bundle_only = _media_has_attribute_key (offer_media, "bundle-only");

    gst_sdp_media_new (&media);
    if (bundle_only && webrtc->bundle_policy == GST_WEBRTC_BUNDLE_POLICY_NONE)
      gst_sdp_media_set_port_info (media, 0, 0);
    else
      gst_sdp_media_set_port_info (media, 9, 0);
    gst_sdp_media_add_connection (media, "IN", "IP4", "0.0.0.0", 0, 0);

    {
      /* FIXME: only needed when restarting ICE */
      gchar *ufrag, *pwd;
      if (!bundled) {
        _generate_ice_credentials (&ufrag, &pwd);
      } else {
        ufrag = g_strdup (bundle_ufrag);
        pwd = g_strdup (bundle_pwd);
      }
      gst_sdp_media_add_attribute (media, "ice-ufrag", ufrag);
      gst_sdp_media_add_attribute (media, "ice-pwd", pwd);
      g_free (ufrag);
      g_free (pwd);
    }

    for (j = 0; j < gst_sdp_media_attributes_len (offer_media); j++) {
      const GstSDPAttribute *attr =
          gst_sdp_media_get_attribute (offer_media, j);

      if (g_strcmp0 (attr->key, "mid") == 0
          || g_strcmp0 (attr->key, "rtcp-mux") == 0) {
        gst_sdp_media_add_attribute (media, attr->key, attr->value);
        /* FIXME: handle anything we want to keep */
      }
    }

    /* set the a=setup: attribute */
    offer_setup = _get_dtls_setup_from_media (offer_media);
    answer_setup = _intersect_dtls_setup (offer_setup);
    if (answer_setup == GST_WEBRTC_DTLS_SETUP_NONE) {
      GST_WARNING_OBJECT (webrtc, "Could not intersect offer setup with "
          "transceiver direction");
      goto rejected;
    }
    _media_replace_setup (media, answer_setup);

    if (g_strcmp0 (gst_sdp_media_get_media (offer_media), "application") == 0) {
      int sctp_port;

      if (gst_sdp_media_formats_len (offer_media) != 1) {
        GST_WARNING_OBJECT (webrtc, "Could not find a format in the m= line "
            "for webrtc-datachannel");
        goto rejected;
      }
      if (g_strcmp0 (gst_sdp_media_get_format (offer_media, 0),
              "webrtc-datachannel") != 0) {
        GST_WARNING_OBJECT (webrtc,
            "format field of data channel m= line "
            "is not \'webrtc-datachannel\'");
        goto rejected;
      }
      sctp_port = _get_sctp_port_from_media (offer_media);
      if (sctp_port == -1) {
        GST_WARNING_OBJECT (webrtc, "media does not contain a sctp port");
        goto rejected;
      }

      /* XXX: older browsers will produce a different SDP format for data
       * channel that is currently not parsed correctly */
      gst_sdp_media_set_proto (media, "UDP/DTLS/SCTP");

      gst_sdp_media_set_media (media, "application");
      gst_sdp_media_set_port_info (media, 9, 0);
      gst_sdp_media_add_format (media, "webrtc-datachannel");

      /* FIXME: negotiate this properly on renegotiation */
      gst_sdp_media_add_attribute (media, "sctp-port", "5000");

      _get_or_create_data_channel_transports (webrtc,
          bundled_mids ? bundle_idx : i);

      if (bundled_mids) {
        const gchar *mid = gst_sdp_media_get_attribute_val (media, "mid");

        g_assert (mid);
        g_string_append_printf (bundled_mids, " %s", mid);
      }

      {
        gchar *cert, *fingerprint, *val;

        g_object_get (webrtc->priv->sctp_transport->transport, "certificate",
            &cert, NULL);

        fingerprint =
            _generate_fingerprint_from_certificate (cert, G_CHECKSUM_SHA256);
        g_free (cert);
        val =
            g_strdup_printf ("%s %s",
            _g_checksum_to_webrtc_string (G_CHECKSUM_SHA256), fingerprint);
        g_free (fingerprint);

        gst_sdp_media_add_attribute (media, "fingerprint", val);
        g_free (val);
      }
    } else if (g_strcmp0 (gst_sdp_media_get_media (offer_media), "audio") == 0
        || g_strcmp0 (gst_sdp_media_get_media (offer_media), "video") == 0) {
      gst_sdp_media_set_proto (media, "UDP/TLS/RTP/SAVPF");

      offer_caps = gst_caps_new_empty ();
      for (j = 0; j < gst_sdp_media_formats_len (offer_media); j++) {
        guint pt = atoi (gst_sdp_media_get_format (offer_media, j));
        GstCaps *caps;

        caps = gst_sdp_media_get_caps_from_media (offer_media, pt);

        /* gst_sdp_media_get_caps_from_media() produces caps with name
         * "application/x-unknown" which will fail intersection with
         * "application/x-rtp" caps so mangle the returns caps to have the
         * correct name here */
        for (k = 0; k < gst_caps_get_size (caps); k++) {
          GstStructure *s = gst_caps_get_structure (caps, k);
          gst_structure_set_name (s, "application/x-rtp");
        }

        gst_caps_append (offer_caps, caps);
      }

      for (j = 0; j < webrtc->priv->transceivers->len; j++) {
        GstCaps *trans_caps;

        rtp_trans =
            g_array_index (webrtc->priv->transceivers,
            GstWebRTCRTPTransceiver *, j);
        trans_caps =
            _find_codec_preferences (webrtc, rtp_trans, GST_PAD_SINK, j);

        GST_TRACE_OBJECT (webrtc, "trying to compare %" GST_PTR_FORMAT
            " and %" GST_PTR_FORMAT, offer_caps, trans_caps);

        /* FIXME: technically this is a little overreaching as some fields we
         * we can deal with not having and/or we may have unrecognized fields
         * that we cannot actually support */
        if (trans_caps) {
          answer_caps = gst_caps_intersect (offer_caps, trans_caps);
          if (answer_caps && !gst_caps_is_empty (answer_caps)) {
            GST_LOG_OBJECT (webrtc,
                "found compatible transceiver %" GST_PTR_FORMAT
                " for offer media %u", trans, i);
            if (trans_caps)
              gst_caps_unref (trans_caps);
            break;
          } else {
            if (answer_caps) {
              gst_caps_unref (answer_caps);
              answer_caps = NULL;
            }
            if (trans_caps)
              gst_caps_unref (trans_caps);
            rtp_trans = NULL;
          }
        } else {
          rtp_trans = NULL;
        }
      }

      if (rtp_trans) {
        answer_dir = rtp_trans->direction;
        g_assert (answer_caps != NULL);
      } else {
        /* if no transceiver, then we only receive that stream and respond with
         * the exact same caps */
        /* FIXME: how to validate that subsequent elements can actually receive
         * this payload/format */
        answer_dir = GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY;
        answer_caps = gst_caps_ref (offer_caps);
      }

      if (!rtp_trans) {
        trans = _create_webrtc_transceiver (webrtc, answer_dir, i);
        rtp_trans = GST_WEBRTC_RTP_TRANSCEIVER (trans);
      } else {
        trans = WEBRTC_TRANSCEIVER (rtp_trans);
      }

      if (!trans->do_nack) {
        answer_caps = gst_caps_make_writable (answer_caps);
        for (k = 0; k < gst_caps_get_size (answer_caps); k++) {
          GstStructure *s = gst_caps_get_structure (answer_caps, k);
          gst_structure_remove_fields (s, "rtcp-fb-nack", NULL);
        }
      }

      gst_sdp_media_set_media_from_caps (answer_caps, media);

      _get_rtx_target_pt_and_ssrc_from_caps (answer_caps, &target_pt,
          &target_ssrc);

      original_target_pt = target_pt;

      _media_add_fec (media, trans, offer_caps, &target_pt);
      if (trans->do_nack) {
        _media_add_rtx (media, trans, offer_caps, target_pt, target_ssrc);
        if (target_pt != original_target_pt)
          _media_add_rtx (media, trans, offer_caps, original_target_pt,
              target_ssrc);
      }

      if (answer_dir != GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY)
        _media_add_ssrcs (media, answer_caps, webrtc,
            WEBRTC_TRANSCEIVER (rtp_trans));

      gst_caps_unref (answer_caps);
      answer_caps = NULL;

      /* set the new media direction */
      offer_dir = _get_direction_from_media (offer_media);
      answer_dir = _intersect_answer_directions (offer_dir, answer_dir);
      if (answer_dir == GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_NONE) {
        GST_WARNING_OBJECT (webrtc, "Could not intersect offer direction with "
            "transceiver direction");
        goto rejected;
      }
      _media_replace_direction (media, answer_dir);

      if (!trans->stream) {
        TransportStream *item;

        if (bundled_mids) {
          const gchar *mid = gst_sdp_media_get_attribute_val (media, "mid");
          item = _get_or_create_transport_stream (webrtc, bundle_idx, FALSE);

          g_assert (mid);
          g_string_append_printf (bundled_mids, " %s", mid);
        } else {
          item = _get_or_create_transport_stream (webrtc, i, FALSE);
        }
        webrtc_transceiver_set_transport (trans, item);
      }
      /* set the a=fingerprint: for this transport */
      g_object_get (trans->stream->transport, "certificate", &cert, NULL);

      {
        gchar *fingerprint, *val;

        fingerprint =
            _generate_fingerprint_from_certificate (cert, G_CHECKSUM_SHA256);
        g_free (cert);
        val =
            g_strdup_printf ("%s %s",
            _g_checksum_to_webrtc_string (G_CHECKSUM_SHA256), fingerprint);
        g_free (fingerprint);

        gst_sdp_media_add_attribute (media, "fingerprint", val);
        g_free (val);
      }

      gst_caps_unref (offer_caps);
    } else {
      GST_WARNING_OBJECT (webrtc, "unknown m= line media name");
      goto rejected;
    }

    if (0) {
    rejected:
      GST_INFO_OBJECT (webrtc, "media %u rejected", i);
      gst_sdp_media_free (media);
      gst_sdp_media_copy (offer_media, &media);
      gst_sdp_media_set_port_info (media, 0, 0);
    }
    gst_sdp_message_add_media (ret, media);
    gst_sdp_media_free (media);
  }

  if (bundled_mids) {
    gchar *mids = g_string_free (bundled_mids, FALSE);

    gst_sdp_message_add_attribute (ret, "group", mids);
    g_free (mids);
  }

  if (bundle_ufrag)
    g_free (bundle_ufrag);

  if (bundle_pwd)
    g_free (bundle_pwd);

  /* FIXME: can we add not matched transceivers? */

  /* XXX: only true for the initial offerer */
  g_object_set (webrtc->priv->ice, "controller", FALSE, NULL);

out:
  if (bundled)
    g_strfreev (bundled);

  return ret;
}

struct create_sdp
{
  GstStructure *options;
  GstPromise *promise;
  GstWebRTCSDPType type;
};

static void
_create_sdp_task (GstWebRTCBin * webrtc, struct create_sdp *data)
{
  GstWebRTCSessionDescription *desc = NULL;
  GstSDPMessage *sdp = NULL;
  GstStructure *s = NULL;

  GST_INFO_OBJECT (webrtc, "creating %s sdp with options %" GST_PTR_FORMAT,
      gst_webrtc_sdp_type_to_string (data->type), data->options);

  if (data->type == GST_WEBRTC_SDP_TYPE_OFFER)
    sdp = _create_offer_task (webrtc, data->options);
  else if (data->type == GST_WEBRTC_SDP_TYPE_ANSWER)
    sdp = _create_answer_task (webrtc, data->options);
  else {
    g_assert_not_reached ();
    goto out;
  }

  if (sdp) {
    desc = gst_webrtc_session_description_new (data->type, sdp);
    s = gst_structure_new ("application/x-gst-promise",
        gst_webrtc_sdp_type_to_string (data->type),
        GST_TYPE_WEBRTC_SESSION_DESCRIPTION, desc, NULL);
  }

out:
  PC_UNLOCK (webrtc);
  gst_promise_reply (data->promise, s);
  PC_LOCK (webrtc);

  if (desc)
    gst_webrtc_session_description_free (desc);
}

static void
_free_create_sdp_data (struct create_sdp *data)
{
  if (data->options)
    gst_structure_free (data->options);
  gst_promise_unref (data->promise);
  g_free (data);
}

static void
gst_webrtc_bin_create_offer (GstWebRTCBin * webrtc,
    const GstStructure * options, GstPromise * promise)
{
  struct create_sdp *data = g_new0 (struct create_sdp, 1);

  if (options)
    data->options = gst_structure_copy (options);
  data->promise = gst_promise_ref (promise);
  data->type = GST_WEBRTC_SDP_TYPE_OFFER;

  gst_webrtc_bin_enqueue_task (webrtc, (GstWebRTCBinFunc) _create_sdp_task,
      data, (GDestroyNotify) _free_create_sdp_data);
}

static void
gst_webrtc_bin_create_answer (GstWebRTCBin * webrtc,
    const GstStructure * options, GstPromise * promise)
{
  struct create_sdp *data = g_new0 (struct create_sdp, 1);

  if (options)
    data->options = gst_structure_copy (options);
  data->promise = gst_promise_ref (promise);
  data->type = GST_WEBRTC_SDP_TYPE_ANSWER;

  gst_webrtc_bin_enqueue_task (webrtc, (GstWebRTCBinFunc) _create_sdp_task,
      data, (GDestroyNotify) _free_create_sdp_data);
}

static GstWebRTCBinPad *
_create_pad_for_sdp_media (GstWebRTCBin * webrtc, GstPadDirection direction,
    guint media_idx)
{
  GstWebRTCBinPad *pad;
  gchar *pad_name;

  pad_name =
      g_strdup_printf ("%s_%u", direction == GST_PAD_SRC ? "src" : "sink",
      media_idx);
  pad = gst_webrtc_bin_pad_new (pad_name, direction);
  g_free (pad_name);
  pad->mlineindex = media_idx;

  return pad;
}

static GstWebRTCRTPTransceiver *
_find_transceiver_for_sdp_media (GstWebRTCBin * webrtc,
    const GstSDPMessage * sdp, guint media_idx)
{
  const GstSDPMedia *media = gst_sdp_message_get_media (sdp, media_idx);
  GstWebRTCRTPTransceiver *ret = NULL;
  int i;

  for (i = 0; i < gst_sdp_media_attributes_len (media); i++) {
    const GstSDPAttribute *attr = gst_sdp_media_get_attribute (media, i);

    if (g_strcmp0 (attr->key, "mid") == 0) {
      if ((ret =
              _find_transceiver (webrtc, attr->value,
                  (FindTransceiverFunc) match_for_mid)))
        goto out;
    }
  }

  ret = _find_transceiver (webrtc, &media_idx,
      (FindTransceiverFunc) transceiver_match_for_mline);

out:
  GST_TRACE_OBJECT (webrtc, "Found transceiver %" GST_PTR_FORMAT, ret);
  return ret;
}

static GstPad *
_connect_input_stream (GstWebRTCBin * webrtc, GstWebRTCBinPad * pad)
{
/*
 * Not-bundle case:
 *
 * ,-------------------------webrtcbin-------------------------,
 * ;                                                           ;
 * ;          ,-------rtpbin-------,   ,--transport_send_%u--, ;
 * ;          ;    send_rtp_src_%u o---o rtp_sink            ; ;
 * ;          ;                    ;   ;                     ; ;
 * ;          ;   send_rtcp_src_%u o---o rtcp_sink           ; ;
 * ; sink_%u  ;                    ;   '---------------------' ;
 * o----------o send_rtp_sink_%u   ;                           ;
 * ;          '--------------------'                           ;
 * '--------------------- -------------------------------------'
 */

/*
 * Bundle case:
 * ,--------------------------------webrtcbin--------------------------------,
 * ;                                                                         ;
 * ;                        ,-------rtpbin-------,   ,--transport_send_%u--, ;
 * ;                        ;    send_rtp_src_%u o---o rtp_sink            ; ;
 * ;                        ;                    ;   ;                     ; ;
 * ;                        ;   send_rtcp_src_%u o---o rtcp_sink           ; ;
 * ; sink_%u ,---funnel---, ;                    ;   '---------------------' ;
 * o---------o sink_%u    ; ;                    ;                           ;
 * ; sink_%u ;        src o-o send_rtp_sink_%u   ;                           ;
 * o---------o sink_%u    ; ;                    ;                           ;
 * ;         '------------' '--------------------'                           ;
 * '-------------------------------------------------------------------------'
 */
  GstPadTemplate *rtp_templ;
  GstPad *rtp_sink;
  gchar *pad_name;
  WebRTCTransceiver *trans;

  g_return_val_if_fail (pad->trans != NULL, NULL);

  GST_INFO_OBJECT (pad, "linking input stream %u", pad->mlineindex);

  trans = WEBRTC_TRANSCEIVER (pad->trans);

  g_assert (trans->stream);

  if (!webrtc->rtpfunnel) {
    rtp_templ =
        _find_pad_template (webrtc->rtpbin, GST_PAD_SINK, GST_PAD_REQUEST,
        "send_rtp_sink_%u");
    g_assert (rtp_templ);

    pad_name = g_strdup_printf ("send_rtp_sink_%u", pad->mlineindex);
    rtp_sink =
        gst_element_request_pad (webrtc->rtpbin, rtp_templ, pad_name, NULL);
    g_free (pad_name);
    gst_ghost_pad_set_target (GST_GHOST_PAD (pad), rtp_sink);
    gst_object_unref (rtp_sink);

    pad_name = g_strdup_printf ("send_rtp_src_%u", pad->mlineindex);
    if (!gst_element_link_pads (GST_ELEMENT (webrtc->rtpbin), pad_name,
            GST_ELEMENT (trans->stream->send_bin), "rtp_sink"))
      g_warn_if_reached ();
    g_free (pad_name);
  } else {
    gchar *pad_name = g_strdup_printf ("sink_%u", pad->mlineindex);
    GstPad *funnel_sinkpad =
        gst_element_get_request_pad (webrtc->rtpfunnel, pad_name);

    gst_ghost_pad_set_target (GST_GHOST_PAD (pad), funnel_sinkpad);

    g_free (pad_name);
    gst_object_unref (funnel_sinkpad);
  }

  gst_element_sync_state_with_parent (GST_ELEMENT (trans->stream->send_bin));

  return GST_PAD (pad);
}

/* output pads are receiving elements */
static void
_connect_output_stream (GstWebRTCBin * webrtc,
    TransportStream * stream, guint session_id)
{
/*
 * ,------------------------webrtcbin------------------------,
 * ;                             ,---------rtpbin---------,  ;
 * ; ,-transport_receive_%u--,   ;                        ;  ;
 * ; ;               rtp_src o---o recv_rtp_sink_%u       ;  ;
 * ; ;                       ;   ;                        ;  ;
 * ; ;              rtcp_src o---o recv_rtcp_sink_%u      ;  ;
 * ; '-----------------------'   ;                        ;  ; src_%u
 * ;                             ;  recv_rtp_src_%u_%u_%u o--o
 * ;                             '------------------------'  ;
 * '---------------------------------------------------------'
 */
  gchar *pad_name;

  GST_INFO_OBJECT (webrtc, "linking output stream %u", session_id);

  pad_name = g_strdup_printf ("recv_rtp_sink_%u", session_id);
  if (!gst_element_link_pads (GST_ELEMENT (stream->receive_bin),
          "rtp_src", GST_ELEMENT (webrtc->rtpbin), pad_name))
    g_warn_if_reached ();
  g_free (pad_name);

  gst_element_sync_state_with_parent (GST_ELEMENT (stream->receive_bin));
}

typedef struct
{
  guint mlineindex;
  gchar *candidate;
} IceCandidateItem;

static void
_clear_ice_candidate_item (IceCandidateItem ** item)
{
  g_free ((*item)->candidate);
  g_free (*item);
}

static void
_add_ice_candidate (GstWebRTCBin * webrtc, IceCandidateItem * item)
{
  GstWebRTCICEStream *stream;

  stream = _find_ice_stream_for_session (webrtc, item->mlineindex);
  if (stream == NULL) {
    GST_WARNING_OBJECT (webrtc, "Unknown mline %u, ignoring", item->mlineindex);
    return;
  }

  GST_LOG_OBJECT (webrtc, "adding ICE candidate with mline:%u, %s",
      item->mlineindex, item->candidate);

  gst_webrtc_ice_add_candidate (webrtc->priv->ice, stream, item->candidate);
}

static gboolean
_filter_sdp_fields (GQuark field_id, const GValue * value,
    GstStructure * new_structure)
{
  if (!g_str_has_prefix (g_quark_to_string (field_id), "a-")) {
    gst_structure_id_set_value (new_structure, field_id, value);
  }
  return TRUE;
}

static void
_update_transceiver_from_sdp_media (GstWebRTCBin * webrtc,
    const GstSDPMessage * sdp, guint media_idx,
    TransportStream * stream, GstWebRTCRTPTransceiver * rtp_trans,
    GStrv bundled, guint bundle_idx, gboolean * should_connect_bundle_stream)
{
  WebRTCTransceiver *trans = WEBRTC_TRANSCEIVER (rtp_trans);
  GstWebRTCRTPTransceiverDirection prev_dir = rtp_trans->current_direction;
  GstWebRTCRTPTransceiverDirection new_dir;
  const GstSDPMedia *media = gst_sdp_message_get_media (sdp, media_idx);
  GstWebRTCDTLSSetup new_setup;
  gboolean new_rtcp_mux, new_rtcp_rsize;
  int i;

  rtp_trans->mline = media_idx;

  for (i = 0; i < gst_sdp_media_attributes_len (media); i++) {
    const GstSDPAttribute *attr = gst_sdp_media_get_attribute (media, i);

    if (g_strcmp0 (attr->key, "mid") == 0) {
      g_free (rtp_trans->mid);
      rtp_trans->mid = g_strdup (attr->value);
    }
  }

  {
    const GstSDPMedia *local_media, *remote_media;
    GstWebRTCRTPTransceiverDirection local_dir, remote_dir;
    GstWebRTCDTLSSetup local_setup, remote_setup;
    guint i, len;
    const gchar *proto;
    GstCaps *global_caps;

    local_media =
        gst_sdp_message_get_media (webrtc->current_local_description->sdp,
        media_idx);
    remote_media =
        gst_sdp_message_get_media (webrtc->current_remote_description->sdp,
        media_idx);

    local_setup = _get_dtls_setup_from_media (local_media);
    remote_setup = _get_dtls_setup_from_media (remote_media);
    new_setup = _get_final_setup (local_setup, remote_setup);
    if (new_setup == GST_WEBRTC_DTLS_SETUP_NONE)
      return;

    local_dir = _get_direction_from_media (local_media);
    remote_dir = _get_direction_from_media (remote_media);
    new_dir = _get_final_direction (local_dir, remote_dir);

    if (new_dir == GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_NONE)
      return;

    /* get proto */
    proto = gst_sdp_media_get_proto (media);
    if (proto != NULL) {
      /* Parse global SDP attributes once */
      global_caps = gst_caps_new_empty_simple ("application/x-unknown");
      GST_DEBUG_OBJECT (webrtc, "mapping sdp session level attributes to caps");
      gst_sdp_message_attributes_to_caps (sdp, global_caps);
      GST_DEBUG_OBJECT (webrtc, "mapping sdp media level attributes to caps");
      gst_sdp_media_attributes_to_caps (media, global_caps);

      if (!bundled) {
        /* clear the ptmap */
        g_array_set_size (stream->ptmap, 0);
      }

      len = gst_sdp_media_formats_len (media);
      for (i = 0; i < len; i++) {
        GstCaps *caps, *outcaps;
        GstStructure *s;
        PtMapItem item;
        gint pt;
        guint j;

        pt = atoi (gst_sdp_media_get_format (media, i));

        GST_DEBUG_OBJECT (webrtc, " looking at %d pt: %d", i, pt);

        /* convert caps */
        caps = gst_sdp_media_get_caps_from_media (media, pt);
        if (caps == NULL) {
          GST_WARNING_OBJECT (webrtc, " skipping pt %d without caps", pt);
          continue;
        }

        /* Merge in global caps */
        /* Intersect will merge in missing fields to the current caps */
        outcaps = gst_caps_intersect (caps, global_caps);
        gst_caps_unref (caps);

        s = gst_caps_get_structure (outcaps, 0);
        gst_structure_set_name (s, "application/x-rtp");
        if (!g_strcmp0 (gst_structure_get_string (s, "encoding-name"),
                "ULPFEC"))
          gst_structure_set (s, "is-fec", G_TYPE_BOOLEAN, TRUE, NULL);

        item.caps = gst_caps_new_empty ();

        for (j = 0; j < gst_caps_get_size (outcaps); j++) {
          GstStructure *s = gst_caps_get_structure (outcaps, j);
          GstStructure *filtered =
              gst_structure_new_empty (gst_structure_get_name (s));

          gst_structure_foreach (s,
              (GstStructureForeachFunc) _filter_sdp_fields, filtered);
          gst_caps_append_structure (item.caps, filtered);
        }

        item.pt = pt;
        gst_caps_unref (outcaps);

        g_array_append_val (stream->ptmap, item);
      }

      gst_caps_unref (global_caps);
    }

    if (prev_dir != GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_NONE
        && prev_dir != new_dir) {
      GST_FIXME_OBJECT (webrtc, "implement transceiver direction changes");
      return;
    }

    if (!bundled || bundle_idx == media_idx) {
      new_rtcp_mux = _media_has_attribute_key (local_media, "rtcp-mux")
          && _media_has_attribute_key (remote_media, "rtcp-mux");
      new_rtcp_rsize = _media_has_attribute_key (local_media, "rtcp-rsize")
          && _media_has_attribute_key (remote_media, "rtcp-rsize");

      {
        GObject *session;
        g_signal_emit_by_name (webrtc->rtpbin, "get-internal-session",
            media_idx, &session);
        if (session) {
          g_object_set (session, "rtcp-reduced-size", new_rtcp_rsize, NULL);
          g_object_unref (session);
        }
      }

      g_object_set (stream, "rtcp-mux", new_rtcp_mux, NULL);
    }
  }

  if (new_dir != prev_dir) {
    GST_TRACE_OBJECT (webrtc, "transceiver direction change");

    if (new_dir == GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY ||
        new_dir == GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDRECV) {
      GstWebRTCBinPad *pad =
          _find_pad_for_mline (webrtc, GST_PAD_SINK, media_idx);
      if (pad) {
        GST_DEBUG_OBJECT (webrtc, "found existing send pad %" GST_PTR_FORMAT
            " for transceiver %" GST_PTR_FORMAT, pad, trans);
        g_assert (pad->trans == rtp_trans);
        g_assert (pad->mlineindex == media_idx);
        gst_object_unref (pad);
      } else {
        GST_DEBUG_OBJECT (webrtc,
            "creating new pad send pad for transceiver %" GST_PTR_FORMAT,
            trans);
        pad = _create_pad_for_sdp_media (webrtc, GST_PAD_SINK, media_idx);
        pad->trans = gst_object_ref (rtp_trans);
        _connect_input_stream (webrtc, pad);
        _add_pad (webrtc, pad);
      }
    }
    if (new_dir == GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY ||
        new_dir == GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDRECV) {
      GstWebRTCBinPad *pad =
          _find_pad_for_mline (webrtc, GST_PAD_SRC, media_idx);
      if (pad) {
        GST_DEBUG_OBJECT (webrtc, "found existing receive pad %" GST_PTR_FORMAT
            " for transceiver %" GST_PTR_FORMAT, pad, trans);
        g_assert (pad->trans == rtp_trans);
        g_assert (pad->mlineindex == media_idx);
        gst_object_unref (pad);
      } else {
        GST_DEBUG_OBJECT (webrtc,
            "creating new receive pad for transceiver %" GST_PTR_FORMAT, trans);
        pad = _create_pad_for_sdp_media (webrtc, GST_PAD_SRC, media_idx);
        pad->trans = gst_object_ref (rtp_trans);

        if (!trans->stream) {
          TransportStream *item;

          item =
              _get_or_create_transport_stream (webrtc,
              bundled ? bundle_idx : media_idx, FALSE);
          webrtc_transceiver_set_transport (trans, item);
        }

        if (!bundled)
          _connect_output_stream (webrtc, trans->stream, media_idx);
        else
          *should_connect_bundle_stream = TRUE;
        /* delay adding the pad until rtpbin creates the recv output pad
         * to ghost to so queries/events travel through the pipeline correctly
         * as soon as the pad is added */
        _add_pad_to_list (webrtc, pad);
      }

      transport_receive_bin_set_receive_state (stream->receive_bin,
          RECEIVE_STATE_PASS);
    } else if (!bundled) {
      transport_receive_bin_set_receive_state (stream->receive_bin,
          RECEIVE_STATE_DROP);
    }

    if (!bundled || bundle_idx == media_idx)
      g_object_set (stream, "dtls-client",
          new_setup == GST_WEBRTC_DTLS_SETUP_ACTIVE, NULL);

    rtp_trans->mline = media_idx;
    rtp_trans->current_direction = new_dir;
  }
}

/* must be called with the pc lock held */
static gint
_generate_data_channel_id (GstWebRTCBin * webrtc)
{
  gboolean is_client;
  gint new_id = -1, max_channels = 0;

  if (webrtc->priv->sctp_transport) {
    g_object_get (webrtc->priv->sctp_transport, "max-channels", &max_channels,
        NULL);
  }
  if (max_channels <= 0) {
    max_channels = 65534;
  }

  g_object_get (webrtc->priv->sctp_transport->transport, "client", &is_client,
      NULL);

  /* TODO: a better search algorithm */
  do {
    GstWebRTCDataChannel *channel;

    new_id++;

    if (new_id < 0 || new_id >= max_channels) {
      /* exhausted id space */
      GST_WARNING_OBJECT (webrtc, "Could not find a suitable "
          "data channel id (max %i)", max_channels);
      return -1;
    }

    /* client must generate even ids, server must generate odd ids */
    if (new_id % 2 == ! !is_client)
      continue;

    channel = _find_data_channel_for_id (webrtc, new_id);
    if (!channel)
      break;
  } while (TRUE);

  return new_id;
}

static void
_update_data_channel_from_sdp_media (GstWebRTCBin * webrtc,
    const GstSDPMessage * sdp, guint media_idx, TransportStream * stream)
{
  const GstSDPMedia *local_media, *remote_media;
  GstWebRTCDTLSSetup local_setup, remote_setup, new_setup;
  TransportReceiveBin *receive;
  int local_port, remote_port;
  guint64 local_max_size, remote_max_size, max_size;
  int i;

  local_media =
      gst_sdp_message_get_media (webrtc->current_local_description->sdp,
      media_idx);
  remote_media =
      gst_sdp_message_get_media (webrtc->current_remote_description->sdp,
      media_idx);

  local_setup = _get_dtls_setup_from_media (local_media);
  remote_setup = _get_dtls_setup_from_media (remote_media);
  new_setup = _get_final_setup (local_setup, remote_setup);
  if (new_setup == GST_WEBRTC_DTLS_SETUP_NONE)
    return;

  /* data channel is always rtcp-muxed to avoid generating ICE candidates
   * for RTCP */
  g_object_set (stream, "rtcp-mux", TRUE, "dtls-client",
      new_setup == GST_WEBRTC_DTLS_SETUP_ACTIVE, NULL);

  local_port = _get_sctp_port_from_media (local_media);
  remote_port = _get_sctp_port_from_media (local_media);
  if (local_port == -1 || remote_port == -1)
    return;

  if (0 == (local_max_size =
          _get_sctp_max_message_size_from_media (local_media)))
    local_max_size = G_MAXUINT64;
  if (0 == (remote_max_size =
          _get_sctp_max_message_size_from_media (remote_media)))
    remote_max_size = G_MAXUINT64;
  max_size = MIN (local_max_size, remote_max_size);

  webrtc->priv->sctp_transport->max_message_size = max_size;

  g_object_set (webrtc->priv->sctp_transport->sctpdec, "local-sctp-port",
      local_port, NULL);
  g_object_set (webrtc->priv->sctp_transport->sctpenc, "remote-sctp-port",
      remote_port, NULL);

  for (i = 0; i < webrtc->priv->data_channels->len; i++) {
    GstWebRTCDataChannel *channel;

    channel =
        g_array_index (webrtc->priv->data_channels, GstWebRTCDataChannel *, i);

    if (channel->id == -1)
      channel->id = _generate_data_channel_id (webrtc);
    if (channel->id == -1)
      GST_ELEMENT_WARNING (webrtc, RESOURCE, NOT_FOUND,
          ("%s", "Failed to generate an identifier for a data channel"), NULL);

    if (webrtc->priv->sctp_transport->association_established
        && !channel->negotiated && !channel->opened) {
      _link_data_channel_to_sctp (webrtc, channel);
      gst_webrtc_data_channel_start_negotiation (channel);
    }
  }

  receive = TRANSPORT_RECEIVE_BIN (stream->receive_bin);
  transport_receive_bin_set_receive_state (receive, RECEIVE_STATE_PASS);
}

static gboolean
_find_compatible_unassociated_transceiver (GstWebRTCRTPTransceiver * p1,
    gconstpointer data)
{
  if (p1->mid)
    return FALSE;
  if (p1->mline != -1)
    return FALSE;

  return TRUE;
}

static void
_connect_rtpfunnel (GstWebRTCBin * webrtc, guint session_id)
{
  gchar *pad_name;
  GstPad *queue_srcpad;
  GstPad *rtp_sink;
  TransportStream *stream = _find_transport_for_session (webrtc, session_id);
  GstElement *queue;

  g_assert (stream);

  if (webrtc->rtpfunnel)
    goto done;

  webrtc->rtpfunnel = gst_element_factory_make ("rtpfunnel", NULL);
  gst_bin_add (GST_BIN (webrtc), webrtc->rtpfunnel);
  gst_element_sync_state_with_parent (webrtc->rtpfunnel);

  queue = gst_element_factory_make ("queue", NULL);
  gst_bin_add (GST_BIN (webrtc), queue);
  gst_element_sync_state_with_parent (queue);

  gst_element_link (webrtc->rtpfunnel, queue);

  queue_srcpad = gst_element_get_static_pad (queue, "src");

  pad_name = g_strdup_printf ("send_rtp_sink_%d", session_id);
  rtp_sink = gst_element_get_request_pad (webrtc->rtpbin, pad_name);
  g_free (pad_name);
  gst_pad_link (queue_srcpad, rtp_sink);
  gst_object_unref (queue_srcpad);
  gst_object_unref (rtp_sink);

  pad_name = g_strdup_printf ("send_rtp_src_%d", session_id);
  if (!gst_element_link_pads (GST_ELEMENT (webrtc->rtpbin), pad_name,
          GST_ELEMENT (stream->send_bin), "rtp_sink"))
    g_warn_if_reached ();
  g_free (pad_name);

done:
  return;
}

static gboolean
_update_transceivers_from_sdp (GstWebRTCBin * webrtc, SDPSource source,
    GstWebRTCSessionDescription * sdp)
{
  int i;
  gboolean ret = FALSE;
  GStrv bundled = NULL;
  guint bundle_idx = 0;
  gboolean should_connect_bundle_stream = FALSE;
  TransportStream *bundle_stream = NULL;

  if (!_parse_bundle (webrtc, sdp->sdp, &bundled))
    goto done;

  if (bundled) {

    if (!_get_bundle_index (sdp->sdp, bundled, &bundle_idx)) {
      GST_ERROR_OBJECT (webrtc, "Bundle tag is %s but no media found matching",
          bundled[0]);
      goto done;
    }

    bundle_stream = _get_or_create_transport_stream (webrtc, bundle_idx,
        _message_media_is_datachannel (sdp->sdp, bundle_idx));

    g_array_set_size (bundle_stream->ptmap, 0);

    _connect_rtpfunnel (webrtc, bundle_idx);
  }

  for (i = 0; i < gst_sdp_message_medias_len (sdp->sdp); i++) {
    const GstSDPMedia *media = gst_sdp_message_get_media (sdp->sdp, i);
    TransportStream *stream;
    GstWebRTCRTPTransceiver *trans;
    guint transport_idx;

    /* skip rejected media */
    if (gst_sdp_media_get_port (media) == 0)
      continue;

    if (bundled)
      transport_idx = bundle_idx;
    else
      transport_idx = i;

    trans = _find_transceiver_for_sdp_media (webrtc, sdp->sdp, i);

    stream = _get_or_create_transport_stream (webrtc, transport_idx,
        _message_media_is_datachannel (sdp->sdp, transport_idx));

    if (trans)
      webrtc_transceiver_set_transport ((WebRTCTransceiver *) trans, stream);

    if (source == SDP_LOCAL && sdp->type == GST_WEBRTC_SDP_TYPE_OFFER && !trans) {
      GST_ERROR ("State mismatch.  Could not find local transceiver by mline.");
      goto done;
    } else {
      if (g_strcmp0 (gst_sdp_media_get_media (media), "audio") == 0 ||
          g_strcmp0 (gst_sdp_media_get_media (media), "video") == 0) {
        if (trans) {
          _update_transceiver_from_sdp_media (webrtc, sdp->sdp, i, stream,
              trans, bundled, bundle_idx, &should_connect_bundle_stream);
        } else {
          trans = _find_transceiver (webrtc, NULL,
              (FindTransceiverFunc) _find_compatible_unassociated_transceiver);
          /* XXX: default to the advertised direction in the sdp for new
           * transceviers.  The spec doesn't actually say what happens here, only
           * that calls to setDirection will change the value.  Nothing about
           * a default value when the transceiver is created internally */
          if (!trans) {
            trans =
                GST_WEBRTC_RTP_TRANSCEIVER (_create_webrtc_transceiver (webrtc,
                    _get_direction_from_media (media), i));
          }
          _update_transceiver_from_sdp_media (webrtc, sdp->sdp, i, stream,
              trans, bundled, bundle_idx, &should_connect_bundle_stream);
        }
      } else if (_message_media_is_datachannel (sdp->sdp, i)) {
        _update_data_channel_from_sdp_media (webrtc, sdp->sdp, i, stream);
      } else {
        GST_ERROR_OBJECT (webrtc, "Unknown media type in SDP at index %u", i);
      }
    }
  }

  if (should_connect_bundle_stream) {
    g_assert (bundle_stream);
    _connect_output_stream (webrtc, bundle_stream, bundle_idx);
  }

  ret = TRUE;

done:
  if (bundled)
    g_strfreev (bundled);
  return ret;
}

static void
_get_ice_credentials_from_sdp_media (const GstSDPMessage * sdp, guint media_idx,
    gchar ** ufrag, gchar ** pwd)
{
  int i;

  *ufrag = NULL;
  *pwd = NULL;

  {
    /* search in the corresponding media section */
    const GstSDPMedia *media = gst_sdp_message_get_media (sdp, media_idx);
    const gchar *tmp_ufrag =
        gst_sdp_media_get_attribute_val (media, "ice-ufrag");
    const gchar *tmp_pwd = gst_sdp_media_get_attribute_val (media, "ice-pwd");
    if (tmp_ufrag && tmp_pwd) {
      *ufrag = g_strdup (tmp_ufrag);
      *pwd = g_strdup (tmp_pwd);
      return;
    }
  }

  /* then in the sdp message itself */
  for (i = 0; i < gst_sdp_message_attributes_len (sdp); i++) {
    const GstSDPAttribute *attr = gst_sdp_message_get_attribute (sdp, i);

    if (g_strcmp0 (attr->key, "ice-ufrag") == 0) {
      g_assert (!*ufrag);
      *ufrag = g_strdup (attr->value);
    } else if (g_strcmp0 (attr->key, "ice-pwd") == 0) {
      g_assert (!*pwd);
      *pwd = g_strdup (attr->value);
    }
  }
  if (!*ufrag && !*pwd) {
    /* Check in the medias themselves. According to JSEP, they should be
     * identical FIXME: only for bundle-d streams */
    for (i = 0; i < gst_sdp_message_medias_len (sdp); i++) {
      const GstSDPMedia *media = gst_sdp_message_get_media (sdp, i);
      const gchar *tmp_ufrag =
          gst_sdp_media_get_attribute_val (media, "ice-ufrag");
      const gchar *tmp_pwd = gst_sdp_media_get_attribute_val (media, "ice-pwd");
      if (tmp_ufrag && tmp_pwd) {
        *ufrag = g_strdup (tmp_ufrag);
        *pwd = g_strdup (tmp_pwd);
        break;
      }
    }
  }
}

struct set_description
{
  GstPromise *promise;
  SDPSource source;
  GstWebRTCSessionDescription *sdp;
};

/* http://w3c.github.io/webrtc-pc/#set-description */
static void
_set_description_task (GstWebRTCBin * webrtc, struct set_description *sd)
{
  GstWebRTCSignalingState new_signaling_state = webrtc->signaling_state;
  GError *error = NULL;
  GStrv bundled = NULL;
  guint bundle_idx = 0;
  guint i;

  {
    gchar *state = _enum_value_to_string (GST_TYPE_WEBRTC_SIGNALING_STATE,
        webrtc->signaling_state);
    gchar *type_str =
        _enum_value_to_string (GST_TYPE_WEBRTC_SDP_TYPE, sd->sdp->type);
    gchar *sdp_text = gst_sdp_message_as_text (sd->sdp->sdp);
    GST_INFO_OBJECT (webrtc, "Attempting to set %s %s in the %s state",
        _sdp_source_to_string (sd->source), type_str, state);
    GST_TRACE_OBJECT (webrtc, "SDP contents\n%s", sdp_text);
    g_free (sdp_text);
    g_free (state);
    g_free (type_str);
  }

  if (!validate_sdp (webrtc, sd->source, sd->sdp, &error)) {
    GST_ERROR_OBJECT (webrtc, "%s", error->message);
    g_clear_error (&error);
    goto out;
  }

  if (webrtc->priv->is_closed) {
    GST_WARNING_OBJECT (webrtc, "we are closed");
    goto out;
  }

  if (!_parse_bundle (webrtc, sd->sdp->sdp, &bundled))
    goto out;

  if (bundled) {
    if (!_get_bundle_index (sd->sdp->sdp, bundled, &bundle_idx)) {
      GST_ERROR_OBJECT (webrtc, "Bundle tag is %s but no media found matching",
          bundled[0]);
      goto out;
    }
  }

  switch (sd->sdp->type) {
    case GST_WEBRTC_SDP_TYPE_OFFER:{
      if (sd->source == SDP_LOCAL) {
        if (webrtc->pending_local_description)
          gst_webrtc_session_description_free
              (webrtc->pending_local_description);
        webrtc->pending_local_description =
            gst_webrtc_session_description_copy (sd->sdp);
        new_signaling_state = GST_WEBRTC_SIGNALING_STATE_HAVE_LOCAL_OFFER;
      } else {
        if (webrtc->pending_remote_description)
          gst_webrtc_session_description_free
              (webrtc->pending_remote_description);
        webrtc->pending_remote_description =
            gst_webrtc_session_description_copy (sd->sdp);
        new_signaling_state = GST_WEBRTC_SIGNALING_STATE_HAVE_REMOTE_OFFER;
      }
      break;
    }
    case GST_WEBRTC_SDP_TYPE_ANSWER:{
      if (sd->source == SDP_LOCAL) {
        if (webrtc->current_local_description)
          gst_webrtc_session_description_free
              (webrtc->current_local_description);
        webrtc->current_local_description =
            gst_webrtc_session_description_copy (sd->sdp);

        if (webrtc->current_remote_description)
          gst_webrtc_session_description_free
              (webrtc->current_remote_description);
        webrtc->current_remote_description = webrtc->pending_remote_description;
        webrtc->pending_remote_description = NULL;
      } else {
        if (webrtc->current_remote_description)
          gst_webrtc_session_description_free
              (webrtc->current_remote_description);
        webrtc->current_remote_description =
            gst_webrtc_session_description_copy (sd->sdp);

        if (webrtc->current_local_description)
          gst_webrtc_session_description_free
              (webrtc->current_local_description);
        webrtc->current_local_description = webrtc->pending_local_description;
        webrtc->pending_local_description = NULL;
      }

      if (webrtc->pending_local_description)
        gst_webrtc_session_description_free (webrtc->pending_local_description);
      webrtc->pending_local_description = NULL;

      if (webrtc->pending_remote_description)
        gst_webrtc_session_description_free
            (webrtc->pending_remote_description);
      webrtc->pending_remote_description = NULL;

      new_signaling_state = GST_WEBRTC_SIGNALING_STATE_STABLE;
      break;
    }
    case GST_WEBRTC_SDP_TYPE_ROLLBACK:{
      GST_FIXME_OBJECT (webrtc, "rollbacks are completely untested");
      if (sd->source == SDP_LOCAL) {
        if (webrtc->pending_local_description)
          gst_webrtc_session_description_free
              (webrtc->pending_local_description);
        webrtc->pending_local_description = NULL;
      } else {
        if (webrtc->pending_remote_description)
          gst_webrtc_session_description_free
              (webrtc->pending_remote_description);
        webrtc->pending_remote_description = NULL;
      }

      new_signaling_state = GST_WEBRTC_SIGNALING_STATE_STABLE;
      break;
    }
    case GST_WEBRTC_SDP_TYPE_PRANSWER:{
      GST_FIXME_OBJECT (webrtc, "pranswers are completely untested");
      if (sd->source == SDP_LOCAL) {
        if (webrtc->pending_local_description)
          gst_webrtc_session_description_free
              (webrtc->pending_local_description);
        webrtc->pending_local_description =
            gst_webrtc_session_description_copy (sd->sdp);

        new_signaling_state = GST_WEBRTC_SIGNALING_STATE_HAVE_LOCAL_PRANSWER;
      } else {
        if (webrtc->pending_remote_description)
          gst_webrtc_session_description_free
              (webrtc->pending_remote_description);
        webrtc->pending_remote_description =
            gst_webrtc_session_description_copy (sd->sdp);

        new_signaling_state = GST_WEBRTC_SIGNALING_STATE_HAVE_REMOTE_PRANSWER;
      }
      break;
    }
  }

  if (new_signaling_state != webrtc->signaling_state) {
    gchar *from = _enum_value_to_string (GST_TYPE_WEBRTC_SIGNALING_STATE,
        webrtc->signaling_state);
    gchar *to = _enum_value_to_string (GST_TYPE_WEBRTC_SIGNALING_STATE,
        new_signaling_state);
    GST_TRACE_OBJECT (webrtc, "notify signaling-state from %s "
        "to %s", from, to);
    webrtc->signaling_state = new_signaling_state;
    PC_UNLOCK (webrtc);
    g_object_notify (G_OBJECT (webrtc), "signaling-state");
    PC_LOCK (webrtc);

    g_free (from);
    g_free (to);
  }

  if (sd->sdp->type == GST_WEBRTC_SDP_TYPE_ROLLBACK) {
    /* FIXME:
     * If the mid value of an RTCRtpTransceiver was set to a non-null value 
     * by the RTCSessionDescription that is being rolled back, set the mid
     * value of that transceiver to null, as described by [JSEP]
     * (section 4.1.7.2.).
     * If an RTCRtpTransceiver was created by applying the
     * RTCSessionDescription that is being rolled back, and a track has not
     * been attached to it via addTrack, remove that transceiver from
     * connection's set of transceivers, as described by [JSEP]
     * (section 4.1.7.2.).
     * Restore the value of connection's [[ sctpTransport]] internal slot
     * to its value at the last stable signaling state.
     */
  }

  if (webrtc->signaling_state == GST_WEBRTC_SIGNALING_STATE_STABLE) {
    gboolean prev_need_negotiation = webrtc->priv->need_negotiation;
    GList *tmp;

    /* media modifications */
    _update_transceivers_from_sdp (webrtc, sd->source, sd->sdp);

    for (tmp = webrtc->priv->pending_sink_transceivers; tmp; tmp = tmp->next) {
      GstWebRTCBinPad *pad = GST_WEBRTC_BIN_PAD (tmp->data);
      const GstSDPMedia *media;

      media = gst_sdp_message_get_media (sd->sdp->sdp, pad->mlineindex);
      /* skip rejected media */
      /* FIXME: arrange for an appropriate flow return */
      if (gst_sdp_media_get_port (media) == 0)
        continue;

      _connect_input_stream (webrtc, pad);
      gst_pad_remove_probe (GST_PAD (pad), pad->block_id);
      pad->block_id = 0;
    }

    g_list_free_full (webrtc->priv->pending_sink_transceivers,
        (GDestroyNotify) gst_object_unref);
    webrtc->priv->pending_sink_transceivers = NULL;

    /* If connection's signaling state is now stable, update the
     * negotiation-needed flag. If connection's [[ needNegotiation]] slot
     * was true both before and after this update, queue a task to check
     * connection's [[needNegotiation]] slot and, if still true, fire a
     * simple event named negotiationneeded at connection.*/
    _update_need_negotiation (webrtc);
    if (prev_need_negotiation && webrtc->priv->need_negotiation) {
      _check_need_negotiation_task (webrtc, NULL);
    }
  }

  for (i = 0; i < gst_sdp_message_medias_len (sd->sdp->sdp); i++) {
    gchar *ufrag, *pwd;
    TransportStream *item;

    item =
        _get_or_create_transport_stream (webrtc, bundled ? bundle_idx : i,
        _message_media_is_datachannel (sd->sdp->sdp, bundled ? bundle_idx : i));

    if (sd->source == SDP_REMOTE) {
      const GstSDPMedia *media = gst_sdp_message_get_media (sd->sdp->sdp, i);
      guint j;

      for (j = 0; j < gst_sdp_media_attributes_len (media); j++) {
        const GstSDPAttribute *attr = gst_sdp_media_get_attribute (media, j);

        if (g_strcmp0 (attr->key, "ssrc") == 0) {
          GStrv split = g_strsplit (attr->value, " ", 0);
          guint32 ssrc;

          if (split[0] && sscanf (split[0], "%u", &ssrc) && split[1]
              && g_str_has_prefix (split[1], "cname:")) {
            SsrcMapItem ssrc_item;

            ssrc_item.media_idx = i;
            ssrc_item.ssrc = ssrc;
            g_array_append_val (item->remote_ssrcmap, ssrc_item);
          }
          g_strfreev (split);
        }
      }
    }

    if (bundled && bundle_idx != i)
      continue;

    _get_ice_credentials_from_sdp_media (sd->sdp->sdp, i, &ufrag, &pwd);

    if (sd->source == SDP_LOCAL)
      gst_webrtc_ice_set_local_credentials (webrtc->priv->ice,
          item->stream, ufrag, pwd);
    else
      gst_webrtc_ice_set_remote_credentials (webrtc->priv->ice,
          item->stream, ufrag, pwd);
    g_free (ufrag);
    g_free (pwd);
  }

  for (i = 0; i < webrtc->priv->ice_stream_map->len; i++) {
    IceStreamItem *item =
        &g_array_index (webrtc->priv->ice_stream_map, IceStreamItem, i);

    gst_webrtc_ice_gather_candidates (webrtc->priv->ice, item->stream);
  }

  if (webrtc->current_local_description && webrtc->current_remote_description) {
    int i;

    for (i = 0; i < webrtc->priv->pending_ice_candidates->len; i++) {
      IceCandidateItem *item =
          g_array_index (webrtc->priv->pending_ice_candidates,
          IceCandidateItem *, i);

      _add_ice_candidate (webrtc, item);
    }
    g_array_set_size (webrtc->priv->pending_ice_candidates, 0);
  }

out:
  if (bundled)
    g_strfreev (bundled);

  PC_UNLOCK (webrtc);
  gst_promise_reply (sd->promise, NULL);
  PC_LOCK (webrtc);
}

static void
_free_set_description_data (struct set_description *sd)
{
  if (sd->promise)
    gst_promise_unref (sd->promise);
  if (sd->sdp)
    gst_webrtc_session_description_free (sd->sdp);
  g_free (sd);
}

static void
gst_webrtc_bin_set_remote_description (GstWebRTCBin * webrtc,
    GstWebRTCSessionDescription * remote_sdp, GstPromise * promise)
{
  struct set_description *sd;

  if (remote_sdp == NULL)
    goto bad_input;
  if (remote_sdp->sdp == NULL)
    goto bad_input;

  sd = g_new0 (struct set_description, 1);
  if (promise != NULL)
    sd->promise = gst_promise_ref (promise);
  sd->source = SDP_REMOTE;
  sd->sdp = gst_webrtc_session_description_copy (remote_sdp);

  gst_webrtc_bin_enqueue_task (webrtc, (GstWebRTCBinFunc) _set_description_task,
      sd, (GDestroyNotify) _free_set_description_data);

  return;

bad_input:
  {
    gst_promise_reply (promise, NULL);
    g_return_if_reached ();
  }
}

static void
gst_webrtc_bin_set_local_description (GstWebRTCBin * webrtc,
    GstWebRTCSessionDescription * local_sdp, GstPromise * promise)
{
  struct set_description *sd;

  if (local_sdp == NULL)
    goto bad_input;
  if (local_sdp->sdp == NULL)
    goto bad_input;

  sd = g_new0 (struct set_description, 1);
  if (promise != NULL)
    sd->promise = gst_promise_ref (promise);
  sd->source = SDP_LOCAL;
  sd->sdp = gst_webrtc_session_description_copy (local_sdp);

  gst_webrtc_bin_enqueue_task (webrtc, (GstWebRTCBinFunc) _set_description_task,
      sd, (GDestroyNotify) _free_set_description_data);

  return;

bad_input:
  {
    gst_promise_reply (promise, NULL);
    g_return_if_reached ();
  }
}

static void
_add_ice_candidate_task (GstWebRTCBin * webrtc, IceCandidateItem * item)
{
  if (!webrtc->current_local_description || !webrtc->current_remote_description) {
    IceCandidateItem *new = g_new0 (IceCandidateItem, 1);
    new->mlineindex = item->mlineindex;
    new->candidate = g_strdup (item->candidate);

    g_array_append_val (webrtc->priv->pending_ice_candidates, new);
  } else {
    _add_ice_candidate (webrtc, item);
  }
}

static void
_free_ice_candidate_item (IceCandidateItem * item)
{
  _clear_ice_candidate_item (&item);
}

static void
gst_webrtc_bin_add_ice_candidate (GstWebRTCBin * webrtc, guint mline,
    const gchar * attr)
{
  IceCandidateItem *item;

  item = g_new0 (IceCandidateItem, 1);
  item->mlineindex = mline;
  if (!g_ascii_strncasecmp (attr, "a=candidate:", 12))
    item->candidate = g_strdup (attr);
  else if (!g_ascii_strncasecmp (attr, "candidate:", 10))
    item->candidate = g_strdup_printf ("a=%s", attr);
  gst_webrtc_bin_enqueue_task (webrtc,
      (GstWebRTCBinFunc) _add_ice_candidate_task, item,
      (GDestroyNotify) _free_ice_candidate_item);
}

static void
_on_ice_candidate_task (GstWebRTCBin * webrtc, IceCandidateItem * item)
{
  const gchar *cand = item->candidate;

  if (!g_ascii_strncasecmp (cand, "a=candidate:", 12)) {
    /* stripping away "a=" */
    cand += 2;
  }

  GST_TRACE_OBJECT (webrtc, "produced ICE candidate for mline:%u and %s",
      item->mlineindex, cand);

  PC_UNLOCK (webrtc);
  g_signal_emit (webrtc, gst_webrtc_bin_signals[ON_ICE_CANDIDATE_SIGNAL],
      0, item->mlineindex, cand);
  PC_LOCK (webrtc);
}

static void
_on_ice_candidate (GstWebRTCICE * ice, guint session_id,
    gchar * candidate, GstWebRTCBin * webrtc)
{
  IceCandidateItem *item = g_new0 (IceCandidateItem, 1);

  item->mlineindex = session_id;
  item->candidate = g_strdup (candidate);

  gst_webrtc_bin_enqueue_task (webrtc,
      (GstWebRTCBinFunc) _on_ice_candidate_task, item,
      (GDestroyNotify) _free_ice_candidate_item);
}

/* https://www.w3.org/TR/webrtc/#dfn-stats-selection-algorithm */
static GstStructure *
_get_stats_from_selector (GstWebRTCBin * webrtc, gpointer selector)
{
  if (selector)
    GST_FIXME_OBJECT (webrtc, "Implement stats selection");

  return gst_structure_copy (webrtc->priv->stats);
}

struct get_stats
{
  GstPad *pad;
  GstPromise *promise;
};

static void
_free_get_stats (struct get_stats *stats)
{
  if (stats->pad)
    gst_object_unref (stats->pad);
  if (stats->promise)
    gst_promise_unref (stats->promise);
  g_free (stats);
}

/* https://www.w3.org/TR/webrtc/#dom-rtcpeerconnection-getstats() */
static void
_get_stats_task (GstWebRTCBin * webrtc, struct get_stats *stats)
{
  GstStructure *s;
  gpointer selector = NULL;

  gst_webrtc_bin_update_stats (webrtc);

  if (stats->pad) {
    GstWebRTCBinPad *wpad = GST_WEBRTC_BIN_PAD (stats->pad);

    if (wpad->trans) {
      if (GST_PAD_DIRECTION (wpad) == GST_PAD_SRC) {
        selector = wpad->trans->receiver;
      } else {
        selector = wpad->trans->sender;
      }
    }
  }

  s = _get_stats_from_selector (webrtc, selector);
  gst_promise_reply (stats->promise, s);
}

static void
gst_webrtc_bin_get_stats (GstWebRTCBin * webrtc, GstPad * pad,
    GstPromise * promise)
{
  struct get_stats *stats;

  g_return_if_fail (promise != NULL);
  g_return_if_fail (pad == NULL || GST_IS_WEBRTC_BIN_PAD (pad));

  stats = g_new0 (struct get_stats, 1);
  stats->promise = gst_promise_ref (promise);
  /* FIXME: check that pad exists in element */
  if (pad)
    stats->pad = gst_object_ref (pad);

  gst_webrtc_bin_enqueue_task (webrtc, (GstWebRTCBinFunc) _get_stats_task,
      stats, (GDestroyNotify) _free_get_stats);
}

static GstWebRTCRTPTransceiver *
gst_webrtc_bin_add_transceiver (GstWebRTCBin * webrtc,
    GstWebRTCRTPTransceiverDirection direction, GstCaps * caps)
{
  WebRTCTransceiver *trans;
  GstWebRTCRTPTransceiver *rtp_trans;

  g_return_val_if_fail (direction != GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_NONE,
      NULL);

  trans = _create_webrtc_transceiver (webrtc, direction, -1);
  rtp_trans = GST_WEBRTC_RTP_TRANSCEIVER (trans);
  if (caps)
    rtp_trans->codec_preferences = gst_caps_ref (caps);

  return gst_object_ref (trans);
}

static void
_deref_and_unref (GstObject ** object)
{
  if (object)
    gst_object_unref (*object);
}

static GArray *
gst_webrtc_bin_get_transceivers (GstWebRTCBin * webrtc)
{
  GArray *arr = g_array_new (FALSE, TRUE, sizeof (gpointer));
  int i;

  g_array_set_clear_func (arr, (GDestroyNotify) _deref_and_unref);

  for (i = 0; i < webrtc->priv->transceivers->len; i++) {
    GstWebRTCRTPTransceiver *trans =
        g_array_index (webrtc->priv->transceivers, GstWebRTCRTPTransceiver *,
        i);
    gst_object_ref (trans);
    g_array_append_val (arr, trans);
  }

  return arr;
}

static gboolean
gst_webrtc_bin_add_turn_server (GstWebRTCBin * webrtc, const gchar * uri)
{
  g_return_val_if_fail (GST_IS_WEBRTC_BIN (webrtc), FALSE);
  g_return_val_if_fail (uri != NULL, FALSE);

  GST_DEBUG_OBJECT (webrtc, "Adding turn server: %s", uri);

  return gst_webrtc_ice_add_turn_server (webrtc->priv->ice, uri);
}

static gboolean
copy_sticky_events (GstPad * pad, GstEvent ** event, gpointer user_data)
{
  GstPad *gpad = GST_PAD_CAST (user_data);

  GST_DEBUG_OBJECT (gpad, "store sticky event %" GST_PTR_FORMAT, *event);
  gst_pad_store_sticky_event (gpad, *event);

  return TRUE;
}

static GstWebRTCDataChannel *
gst_webrtc_bin_create_data_channel (GstWebRTCBin * webrtc, const gchar * label,
    GstStructure * init_params)
{
  gboolean ordered;
  gint max_packet_lifetime;
  gint max_retransmits;
  const gchar *protocol;
  gboolean negotiated;
  gint id;
  GstWebRTCPriorityType priority;
  GstWebRTCDataChannel *ret;
  gint max_channels = 65534;

  g_return_val_if_fail (GST_IS_WEBRTC_BIN (webrtc), NULL);
  g_return_val_if_fail (label != NULL, NULL);
  g_return_val_if_fail (strlen (label) <= 65535, NULL);
  g_return_val_if_fail (webrtc->priv->is_closed != TRUE, NULL);

  if (!init_params
      || !gst_structure_get_boolean (init_params, "ordered", &ordered))
    ordered = TRUE;
  if (!init_params
      || !gst_structure_get_int (init_params, "max-packet-lifetime",
          &max_packet_lifetime))
    max_packet_lifetime = -1;
  if (!init_params
      || !gst_structure_get_boolean (init_params, "max-retransmits",
          &max_retransmits))
    max_retransmits = -1;
  /* both retransmits and lifetime cannot be set */
  g_return_val_if_fail ((max_packet_lifetime == -1)
      || (max_retransmits == -1), NULL);

  if (!init_params
      || !(protocol = gst_structure_get_string (init_params, "protocol")))
    protocol = "";
  g_return_val_if_fail (strlen (protocol) <= 65535, NULL);

  if (!init_params
      || !gst_structure_get_boolean (init_params, "negotiated", &negotiated))
    negotiated = FALSE;
  if (!negotiated || !init_params
      || !gst_structure_get_int (init_params, "id", &id))
    id = -1;
  if (negotiated)
    g_return_val_if_fail (id != -1, NULL);
  g_return_val_if_fail (id < 65535, NULL);

  if (!init_params
      || !gst_structure_get_enum (init_params, "priority",
          GST_TYPE_WEBRTC_PRIORITY_TYPE, (gint *) & priority))
    priority = GST_WEBRTC_PRIORITY_TYPE_LOW;

  /* FIXME: clamp max-retransmits and max-packet-lifetime */

  if (webrtc->priv->sctp_transport) {
    /* Let transport be the connection's [[SctpTransport]] slot.
     *
     * If the [[DataChannelId]] slot is not null, transport is in 
     * connected state and [[DataChannelId]] is greater or equal to the
     * transport's [[MaxChannels]] slot, throw an OperationError.
     */
    g_object_get (webrtc->priv->sctp_transport, "max-channels", &max_channels,
        NULL);

    g_return_val_if_fail (id <= max_channels, NULL);
  }

  if (!_have_nice_elements (webrtc) || !_have_dtls_elements (webrtc) ||
      !_have_sctp_elements (webrtc))
    return NULL;

  PC_LOCK (webrtc);
  /* check if the id has been used already */
  if (id != -1) {
    GstWebRTCDataChannel *channel = _find_data_channel_for_id (webrtc, id);
    if (channel) {
      GST_ELEMENT_WARNING (webrtc, LIBRARY, SETTINGS,
          ("Attempting to add a data channel with a duplicate ID: %i", id),
          NULL);
      PC_UNLOCK (webrtc);
      return NULL;
    }
  } else if (webrtc->current_local_description
      && webrtc->current_remote_description && webrtc->priv->sctp_transport
      && webrtc->priv->sctp_transport->transport) {
    /* else we can only generate an id if we're configured already.  The other
     * case for generating an id is on sdp setting */
    id = _generate_data_channel_id (webrtc);
    if (id == -1) {
      GST_ELEMENT_WARNING (webrtc, RESOURCE, NOT_FOUND,
          ("%s", "Failed to generate an identifier for a data channel"), NULL);
      PC_UNLOCK (webrtc);
      return NULL;
    }
  }

  ret = g_object_new (GST_TYPE_WEBRTC_DATA_CHANNEL, "label", label,
      "ordered", ordered, "max-packet-lifetime", max_packet_lifetime,
      "max-retransmits", max_retransmits, "protocol", protocol,
      "negotiated", negotiated, "id", id, "priority", priority, NULL);

  if (ret) {
    gst_bin_add (GST_BIN (webrtc), ret->appsrc);
    gst_bin_add (GST_BIN (webrtc), ret->appsink);

    gst_element_sync_state_with_parent (ret->appsrc);
    gst_element_sync_state_with_parent (ret->appsink);

    ret = gst_object_ref (ret);
    ret->webrtcbin = webrtc;
    g_array_append_val (webrtc->priv->data_channels, ret);
    _link_data_channel_to_sctp (webrtc, ret);
    if (webrtc->priv->sctp_transport &&
        webrtc->priv->sctp_transport->association_established
        && !ret->negotiated)
      gst_webrtc_data_channel_start_negotiation (ret);
  }

  PC_UNLOCK (webrtc);
  return ret;
}

/* === rtpbin signal implementations === */

static void
on_rtpbin_pad_added (GstElement * rtpbin, GstPad * new_pad,
    GstWebRTCBin * webrtc)
{
  gchar *new_pad_name = NULL;

  new_pad_name = gst_pad_get_name (new_pad);
  GST_TRACE_OBJECT (webrtc, "new rtpbin pad %s", new_pad_name);
  if (g_str_has_prefix (new_pad_name, "recv_rtp_src_")) {
    guint32 session_id = 0, ssrc = 0, pt = 0;
    GstWebRTCRTPTransceiver *rtp_trans;
    WebRTCTransceiver *trans;
    TransportStream *stream;
    GstWebRTCBinPad *pad;
    guint media_idx = 0;
    guint i;

    if (sscanf (new_pad_name, "recv_rtp_src_%u_%u_%u", &session_id, &ssrc,
            &pt) != 3) {
      g_critical ("Invalid rtpbin pad name \'%s\'", new_pad_name);
      return;
    }

    stream = _find_transport_for_session (webrtc, session_id);
    if (!stream)
      g_warn_if_reached ();

    media_idx = session_id;

    for (i = 0; i < stream->remote_ssrcmap->len; i++) {
      SsrcMapItem *item =
          &g_array_index (stream->remote_ssrcmap, SsrcMapItem, i);
      if (item->ssrc == ssrc) {
        media_idx = item->media_idx;
        break;
      }
    }

    rtp_trans = _find_transceiver_for_mline (webrtc, media_idx);
    if (!rtp_trans)
      g_warn_if_reached ();
    trans = WEBRTC_TRANSCEIVER (rtp_trans);
    g_assert (trans->stream == stream);

    pad = _find_pad_for_transceiver (webrtc, GST_PAD_SRC, rtp_trans);

    GST_TRACE_OBJECT (webrtc, "found pad %" GST_PTR_FORMAT
        " for rtpbin pad name %s", pad, new_pad_name);
    if (!pad)
      g_warn_if_reached ();
    gst_ghost_pad_set_target (GST_GHOST_PAD (pad), GST_PAD (new_pad));

    if (webrtc->priv->running)
      gst_pad_set_active (GST_PAD (pad), TRUE);
    gst_pad_sticky_events_foreach (new_pad, copy_sticky_events, pad);
    gst_element_add_pad (GST_ELEMENT (webrtc), GST_PAD (pad));
    _remove_pending_pad (webrtc, pad);

    gst_object_unref (pad);
  }
  g_free (new_pad_name);
}

/* only used for the receiving streams */
static GstCaps *
on_rtpbin_request_pt_map (GstElement * rtpbin, guint session_id, guint pt,
    GstWebRTCBin * webrtc)
{
  TransportStream *stream;
  GstCaps *ret;

  GST_DEBUG_OBJECT (webrtc, "getting pt map for pt %d in session %d", pt,
      session_id);

  stream = _find_transport_for_session (webrtc, session_id);
  if (!stream)
    goto unknown_session;

  if ((ret = _transport_stream_get_caps_for_pt (stream, pt)))
    gst_caps_ref (ret);

  GST_TRACE_OBJECT (webrtc, "Found caps %" GST_PTR_FORMAT " for pt %d in "
      "session %d", ret, pt, session_id);

  return ret;

unknown_session:
  {
    GST_DEBUG_OBJECT (webrtc, "unknown session %d", session_id);
    return NULL;
  }
}

static GstElement *
on_rtpbin_request_aux_sender (GstElement * rtpbin, guint session_id,
    GstWebRTCBin * webrtc)
{
  TransportStream *stream;
  GstStructure *pt_map = gst_structure_new_empty ("application/x-rtp-pt-map");
  GstElement *ret = NULL;
  GstWebRTCRTPTransceiver *trans;

  stream = _find_transport_for_session (webrtc, session_id);
  trans = _find_transceiver (webrtc, &session_id,
      (FindTransceiverFunc) transceiver_match_for_mline);

  if (stream) {
    guint i;

    for (i = 0; i < stream->ptmap->len; i++) {
      PtMapItem *item = &g_array_index (stream->ptmap, PtMapItem, i);
      if (!gst_caps_is_empty (item->caps)) {
        GstStructure *s = gst_caps_get_structure (item->caps, 0);
        gint pt;
        const gchar *apt_str = gst_structure_get_string (s, "apt");

        if (!apt_str)
          continue;

        if (!g_strcmp0 (gst_structure_get_string (s, "encoding-name"), "RTX") &&
            gst_structure_get_int (s, "payload", &pt)) {
          gst_structure_set (pt_map, apt_str, G_TYPE_UINT, pt, NULL);
        }
      }
    }
  }

  if (gst_structure_n_fields (pt_map)) {
    GstElement *rtx;
    GstPad *pad;
    gchar *name;

    GST_INFO ("creating AUX sender");
    ret = gst_bin_new (NULL);
    rtx = gst_element_factory_make ("rtprtxsend", NULL);
    g_object_set (rtx, "payload-type-map", pt_map, "max-size-packets", 500,
        NULL);

    if (WEBRTC_TRANSCEIVER (trans)->local_rtx_ssrc_map)
      g_object_set (rtx, "ssrc-map",
          WEBRTC_TRANSCEIVER (trans)->local_rtx_ssrc_map, NULL);

    gst_bin_add (GST_BIN (ret), rtx);

    pad = gst_element_get_static_pad (rtx, "src");
    name = g_strdup_printf ("src_%u", session_id);
    gst_element_add_pad (ret, gst_ghost_pad_new (name, pad));
    g_free (name);
    gst_object_unref (pad);

    pad = gst_element_get_static_pad (rtx, "sink");
    name = g_strdup_printf ("sink_%u", session_id);
    gst_element_add_pad (ret, gst_ghost_pad_new (name, pad));
    g_free (name);
    gst_object_unref (pad);
  }

  gst_structure_free (pt_map);

  return ret;
}

static GstElement *
on_rtpbin_request_aux_receiver (GstElement * rtpbin, guint session_id,
    GstWebRTCBin * webrtc)
{
  GstElement *ret = NULL;
  GstElement *prev = NULL;
  GstPad *sinkpad = NULL;
  TransportStream *stream;
  gint red_pt = 0;
  gint rtx_pt = 0;

  stream = _find_transport_for_session (webrtc, session_id);

  if (stream) {
    red_pt = _transport_stream_get_pt (stream, "RED");
    rtx_pt = _transport_stream_get_pt (stream, "RTX");
  }

  if (red_pt || rtx_pt)
    ret = gst_bin_new (NULL);

  if (rtx_pt) {
    GstCaps *rtx_caps = _transport_stream_get_caps_for_pt (stream, rtx_pt);
    GstElement *rtx = gst_element_factory_make ("rtprtxreceive", NULL);
    GstStructure *pt_map;
    const GstStructure *s = gst_caps_get_structure (rtx_caps, 0);

    gst_bin_add (GST_BIN (ret), rtx);

    pt_map = gst_structure_new_empty ("application/x-rtp-pt-map");
    gst_structure_set (pt_map, gst_structure_get_string (s, "apt"), G_TYPE_UINT,
        rtx_pt, NULL);
    g_object_set (rtx, "payload-type-map", pt_map, NULL);

    sinkpad = gst_element_get_static_pad (rtx, "sink");

    prev = rtx;
  }

  if (red_pt) {
    GstElement *rtpreddec = gst_element_factory_make ("rtpreddec", NULL);

    GST_DEBUG_OBJECT (webrtc, "Creating RED decoder for pt %d in session %u",
        red_pt, session_id);

    gst_bin_add (GST_BIN (ret), rtpreddec);

    g_object_set (rtpreddec, "pt", red_pt, NULL);

    if (prev)
      gst_element_link (prev, rtpreddec);
    else
      sinkpad = gst_element_get_static_pad (rtpreddec, "sink");

    prev = rtpreddec;
  }

  if (sinkpad) {
    gchar *name = g_strdup_printf ("sink_%u", session_id);
    GstPad *ghost = gst_ghost_pad_new (name, sinkpad);
    g_free (name);
    gst_object_unref (sinkpad);
    gst_element_add_pad (ret, ghost);
  }

  if (prev) {
    gchar *name = g_strdup_printf ("src_%u", session_id);
    GstPad *srcpad = gst_element_get_static_pad (prev, "src");
    GstPad *ghost = gst_ghost_pad_new (name, srcpad);
    g_free (name);
    gst_object_unref (srcpad);
    gst_element_add_pad (ret, ghost);
  }

  return ret;
}

static GstElement *
on_rtpbin_request_fec_decoder (GstElement * rtpbin, guint session_id,
    GstWebRTCBin * webrtc)
{
  TransportStream *stream;
  GstElement *ret = NULL;
  gint pt = 0;
  GObject *internal_storage;

  stream = _find_transport_for_session (webrtc, session_id);

  /* TODO: for now, we only support ulpfec, but once we support
   * more algorithms, if the remote may use more than one algorithm,
   * we will want to do the following:
   *
   * + Return a bin here, with the relevant FEC decoders plugged in
   *   and their payload type set to 0
   * + Enable the decoders by setting the payload type only when
   *   we detect it (by connecting to ptdemux:new-payload-type for
   *   example)
   */
  if (stream)
    pt = _transport_stream_get_pt (stream, "ULPFEC");

  if (pt) {
    GST_DEBUG_OBJECT (webrtc, "Creating ULPFEC decoder for pt %d in session %u",
        pt, session_id);
    ret = gst_element_factory_make ("rtpulpfecdec", NULL);
    g_signal_emit_by_name (webrtc->rtpbin, "get-internal-storage", session_id,
        &internal_storage);

    g_object_set (ret, "pt", pt, "storage", internal_storage, NULL);
    g_object_unref (internal_storage);
  }

  return ret;
}

static GstElement *
on_rtpbin_request_fec_encoder (GstElement * rtpbin, guint session_id,
    GstWebRTCBin * webrtc)
{
  GstElement *ret = NULL;
  GstElement *prev = NULL;
  TransportStream *stream;
  guint ulpfec_pt = 0;
  guint red_pt = 0;
  GstPad *sinkpad = NULL;
  GstWebRTCRTPTransceiver *trans;

  stream = _find_transport_for_session (webrtc, session_id);
  trans = _find_transceiver (webrtc, &session_id,
      (FindTransceiverFunc) transceiver_match_for_mline);

  if (stream) {
    ulpfec_pt = _transport_stream_get_pt (stream, "ULPFEC");
    red_pt = _transport_stream_get_pt (stream, "RED");
  }

  if (ulpfec_pt || red_pt)
    ret = gst_bin_new (NULL);

  if (ulpfec_pt) {
    GstElement *fecenc = gst_element_factory_make ("rtpulpfecenc", NULL);
    GstCaps *caps = _transport_stream_get_caps_for_pt (stream, ulpfec_pt);

    GST_DEBUG_OBJECT (webrtc,
        "Creating ULPFEC encoder for session %d with pt %d", session_id,
        ulpfec_pt);

    gst_bin_add (GST_BIN (ret), fecenc);
    sinkpad = gst_element_get_static_pad (fecenc, "sink");
    g_object_set (fecenc, "pt", ulpfec_pt, "percentage",
        WEBRTC_TRANSCEIVER (trans)->fec_percentage, NULL);


    if (caps && !gst_caps_is_empty (caps)) {
      const GstStructure *s = gst_caps_get_structure (caps, 0);
      const gchar *media = gst_structure_get_string (s, "media");

      if (!g_strcmp0 (media, "video"))
        g_object_set (fecenc, "multipacket", TRUE, NULL);
    }

    prev = fecenc;
  }

  if (red_pt) {
    GstElement *redenc = gst_element_factory_make ("rtpredenc", NULL);

    GST_DEBUG_OBJECT (webrtc, "Creating RED encoder for session %d with pt %d",
        session_id, red_pt);

    gst_bin_add (GST_BIN (ret), redenc);
    if (prev)
      gst_element_link (prev, redenc);
    else
      sinkpad = gst_element_get_static_pad (redenc, "sink");

    g_object_set (redenc, "pt", red_pt, "allow-no-red-blocks", TRUE, NULL);

    prev = redenc;
  }

  if (sinkpad) {
    GstPad *ghost = gst_ghost_pad_new ("sink", sinkpad);
    gst_object_unref (sinkpad);
    gst_element_add_pad (ret, ghost);
  }

  if (prev) {
    GstPad *srcpad = gst_element_get_static_pad (prev, "src");
    GstPad *ghost = gst_ghost_pad_new ("src", srcpad);
    gst_object_unref (srcpad);
    gst_element_add_pad (ret, ghost);
  }

  return ret;
}

static void
on_rtpbin_ssrc_active (GstElement * rtpbin, guint session_id, guint ssrc,
    GstWebRTCBin * webrtc)
{
}

static void
on_rtpbin_new_jitterbuffer (GstElement * rtpbin, GstElement * jitterbuffer,
    guint session_id, guint ssrc, GstWebRTCBin * webrtc)
{
  GstWebRTCRTPTransceiver *trans;

  trans = _find_transceiver (webrtc, &session_id,
      (FindTransceiverFunc) transceiver_match_for_mline);

  if (trans) {
    /* We don't set do-retransmission on rtpbin as we want per-session control */
    g_object_set (jitterbuffer, "do-retransmission",
        WEBRTC_TRANSCEIVER (trans)->do_nack, NULL);
  } else {
    g_assert_not_reached ();
  }
}

static void
on_rtpbin_new_storage (GstElement * rtpbin, GstElement * storage,
    guint session_id, GstWebRTCBin * webrtc)
{
  /* TODO: when exposing latency, set size-time based on that */
  g_object_set (storage, "size-time", (guint64) 250 * GST_MSECOND, NULL);
}

static GstElement *
_create_rtpbin (GstWebRTCBin * webrtc)
{
  GstElement *rtpbin;

  if (!(rtpbin = gst_element_factory_make ("rtpbin", "rtpbin")))
    return NULL;

  /* mandated by WebRTC */
  gst_util_set_object_arg (G_OBJECT (rtpbin), "rtp-profile", "savpf");

  g_object_set (rtpbin, "do-lost", TRUE, NULL);

  g_signal_connect (rtpbin, "pad-added", G_CALLBACK (on_rtpbin_pad_added),
      webrtc);
  g_signal_connect (rtpbin, "request-pt-map",
      G_CALLBACK (on_rtpbin_request_pt_map), webrtc);
  g_signal_connect (rtpbin, "request-aux-sender",
      G_CALLBACK (on_rtpbin_request_aux_sender), webrtc);
  g_signal_connect (rtpbin, "request-aux-receiver",
      G_CALLBACK (on_rtpbin_request_aux_receiver), webrtc);
  g_signal_connect (rtpbin, "new-storage",
      G_CALLBACK (on_rtpbin_new_storage), webrtc);
  g_signal_connect (rtpbin, "request-fec-decoder",
      G_CALLBACK (on_rtpbin_request_fec_decoder), webrtc);
  g_signal_connect (rtpbin, "request-fec-encoder",
      G_CALLBACK (on_rtpbin_request_fec_encoder), webrtc);
  g_signal_connect (rtpbin, "on-ssrc-active",
      G_CALLBACK (on_rtpbin_ssrc_active), webrtc);
  g_signal_connect (rtpbin, "new-jitterbuffer",
      G_CALLBACK (on_rtpbin_new_jitterbuffer), webrtc);

  return rtpbin;
}

static GstStateChangeReturn
gst_webrtc_bin_change_state (GstElement * element, GstStateChange transition)
{
  GstWebRTCBin *webrtc = GST_WEBRTC_BIN (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  GST_DEBUG ("changing state: %s => %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:{
      if (!_have_nice_elements (webrtc) || !_have_dtls_elements (webrtc))
        return GST_STATE_CHANGE_FAILURE;
      _start_thread (webrtc);
      _update_need_negotiation (webrtc);
      break;
    }
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      webrtc->priv->running = TRUE;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      /* Mangle the return value to NO_PREROLL as that's what really is
       * occurring here however cannot be propagated correctly due to nicesrc
       * requiring that it be in PLAYING already in order to send/receive
       * correctly :/ */
      ret = GST_STATE_CHANGE_NO_PREROLL;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      webrtc->priv->running = FALSE;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      _stop_thread (webrtc);
      break;
    default:
      break;
  }

  return ret;
}

static GstPadProbeReturn
pad_block (GstPad * pad, GstPadProbeInfo * info, gpointer unused)
{
  GST_LOG_OBJECT (pad, "blocking pad with data %" GST_PTR_FORMAT, info->data);

  return GST_PAD_PROBE_OK;
}

static GstPad *
gst_webrtc_bin_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name, const GstCaps * caps)
{
  GstWebRTCBin *webrtc = GST_WEBRTC_BIN (element);
  GstWebRTCBinPad *pad = NULL;
  guint serial;

  if (!_have_nice_elements (webrtc) || !_have_dtls_elements (webrtc))
    return NULL;

  if (templ->direction == GST_PAD_SINK ||
      g_strcmp0 (templ->name_template, "sink_%u") == 0) {
    GstWebRTCRTPTransceiver *trans;

    GST_OBJECT_LOCK (webrtc);
    if (name == NULL || strlen (name) < 6 || !g_str_has_prefix (name, "sink_")) {
      /* no name given when requesting the pad, use next available int */
      serial = webrtc->priv->max_sink_pad_serial++;
    } else {
      /* parse serial number from requested padname */
      serial = g_ascii_strtoull (&name[5], NULL, 10);
      if (serial > webrtc->priv->max_sink_pad_serial)
        webrtc->priv->max_sink_pad_serial = serial;
    }
    GST_OBJECT_UNLOCK (webrtc);

    pad = _create_pad_for_sdp_media (webrtc, GST_PAD_SINK, serial);
    trans = _find_transceiver_for_mline (webrtc, serial);
    if (!trans)
      trans =
          GST_WEBRTC_RTP_TRANSCEIVER (_create_webrtc_transceiver (webrtc,
              GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDRECV, serial));
    pad->trans = gst_object_ref (trans);

    pad->block_id = gst_pad_add_probe (GST_PAD (pad), GST_PAD_PROBE_TYPE_BLOCK |
        GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST,
        (GstPadProbeCallback) pad_block, NULL, NULL);
    webrtc->priv->pending_sink_transceivers =
        g_list_append (webrtc->priv->pending_sink_transceivers,
        gst_object_ref (pad));
    _add_pad (webrtc, pad);
  }

  return GST_PAD (pad);
}

static void
gst_webrtc_bin_release_pad (GstElement * element, GstPad * pad)
{
  GstWebRTCBin *webrtc = GST_WEBRTC_BIN (element);
  GstWebRTCBinPad *webrtc_pad = GST_WEBRTC_BIN_PAD (pad);

  if (webrtc_pad->trans)
    gst_object_unref (webrtc_pad->trans);
  webrtc_pad->trans = NULL;

  _remove_pad (webrtc, webrtc_pad);
}

static void
gst_webrtc_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstWebRTCBin *webrtc = GST_WEBRTC_BIN (object);

  switch (prop_id) {
    case PROP_STUN_SERVER:
    case PROP_TURN_SERVER:
      g_object_set_property (G_OBJECT (webrtc->priv->ice), pspec->name, value);
      break;
    case PROP_BUNDLE_POLICY:
      if (g_value_get_enum (value) == GST_WEBRTC_BUNDLE_POLICY_BALANCED) {
        GST_ERROR_OBJECT (object, "Balanced bundle policy not implemented yet");
      } else {
        webrtc->bundle_policy = g_value_get_enum (value);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_webrtc_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstWebRTCBin *webrtc = GST_WEBRTC_BIN (object);

  PC_LOCK (webrtc);
  switch (prop_id) {
    case PROP_CONNECTION_STATE:
      g_value_set_enum (value, webrtc->peer_connection_state);
      break;
    case PROP_SIGNALING_STATE:
      g_value_set_enum (value, webrtc->signaling_state);
      break;
    case PROP_ICE_GATHERING_STATE:
      g_value_set_enum (value, webrtc->ice_gathering_state);
      break;
    case PROP_ICE_CONNECTION_STATE:
      g_value_set_enum (value, webrtc->ice_connection_state);
      break;
    case PROP_LOCAL_DESCRIPTION:
      if (webrtc->pending_local_description)
        g_value_set_boxed (value, webrtc->pending_local_description);
      else if (webrtc->current_local_description)
        g_value_set_boxed (value, webrtc->current_local_description);
      else
        g_value_set_boxed (value, NULL);
      break;
    case PROP_CURRENT_LOCAL_DESCRIPTION:
      g_value_set_boxed (value, webrtc->current_local_description);
      break;
    case PROP_PENDING_LOCAL_DESCRIPTION:
      g_value_set_boxed (value, webrtc->pending_local_description);
      break;
    case PROP_REMOTE_DESCRIPTION:
      if (webrtc->pending_remote_description)
        g_value_set_boxed (value, webrtc->pending_remote_description);
      else if (webrtc->current_remote_description)
        g_value_set_boxed (value, webrtc->current_remote_description);
      else
        g_value_set_boxed (value, NULL);
      break;
    case PROP_CURRENT_REMOTE_DESCRIPTION:
      g_value_set_boxed (value, webrtc->current_remote_description);
      break;
    case PROP_PENDING_REMOTE_DESCRIPTION:
      g_value_set_boxed (value, webrtc->pending_remote_description);
      break;
    case PROP_STUN_SERVER:
    case PROP_TURN_SERVER:
      g_object_get_property (G_OBJECT (webrtc->priv->ice), pspec->name, value);
      break;
    case PROP_BUNDLE_POLICY:
      g_value_set_enum (value, webrtc->bundle_policy);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  PC_UNLOCK (webrtc);
}

static void
_free_pending_pad (GstPad * pad)
{
  gst_object_unref (pad);
}

static void
gst_webrtc_bin_dispose (GObject * object)
{
  GstWebRTCBin *webrtc = GST_WEBRTC_BIN (object);

  if (webrtc->priv->ice)
    gst_object_unref (webrtc->priv->ice);
  webrtc->priv->ice = NULL;

  if (webrtc->priv->ice_stream_map)
    g_array_free (webrtc->priv->ice_stream_map, TRUE);
  webrtc->priv->ice_stream_map = NULL;

  g_clear_object (&webrtc->priv->sctp_transport);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_webrtc_bin_finalize (GObject * object)
{
  GstWebRTCBin *webrtc = GST_WEBRTC_BIN (object);

  if (webrtc->priv->transports)
    g_array_free (webrtc->priv->transports, TRUE);
  webrtc->priv->transports = NULL;

  if (webrtc->priv->transceivers)
    g_array_free (webrtc->priv->transceivers, TRUE);
  webrtc->priv->transceivers = NULL;

  if (webrtc->priv->data_channels)
    g_array_free (webrtc->priv->data_channels, TRUE);
  webrtc->priv->data_channels = NULL;

  if (webrtc->priv->pending_data_channels)
    g_array_free (webrtc->priv->pending_data_channels, TRUE);
  webrtc->priv->pending_data_channels = NULL;

  if (webrtc->priv->pending_ice_candidates)
    g_array_free (webrtc->priv->pending_ice_candidates, TRUE);
  webrtc->priv->pending_ice_candidates = NULL;

  if (webrtc->priv->session_mid_map)
    g_array_free (webrtc->priv->session_mid_map, TRUE);
  webrtc->priv->session_mid_map = NULL;

  if (webrtc->priv->pending_pads)
    g_list_free_full (webrtc->priv->pending_pads,
        (GDestroyNotify) _free_pending_pad);
  webrtc->priv->pending_pads = NULL;

  if (webrtc->priv->pending_sink_transceivers)
    g_list_free_full (webrtc->priv->pending_sink_transceivers,
        (GDestroyNotify) gst_object_unref);
  webrtc->priv->pending_sink_transceivers = NULL;

  if (webrtc->current_local_description)
    gst_webrtc_session_description_free (webrtc->current_local_description);
  webrtc->current_local_description = NULL;
  if (webrtc->pending_local_description)
    gst_webrtc_session_description_free (webrtc->pending_local_description);
  webrtc->pending_local_description = NULL;

  if (webrtc->current_remote_description)
    gst_webrtc_session_description_free (webrtc->current_remote_description);
  webrtc->current_remote_description = NULL;
  if (webrtc->pending_remote_description)
    gst_webrtc_session_description_free (webrtc->pending_remote_description);
  webrtc->pending_remote_description = NULL;

  if (webrtc->priv->stats)
    gst_structure_free (webrtc->priv->stats);
  webrtc->priv->stats = NULL;

  g_mutex_clear (PC_GET_LOCK (webrtc));
  g_cond_clear (PC_GET_COND (webrtc));

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_webrtc_bin_class_init (GstWebRTCBinClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;

  element_class->request_new_pad = gst_webrtc_bin_request_new_pad;
  element_class->release_pad = gst_webrtc_bin_release_pad;
  element_class->change_state = gst_webrtc_bin_change_state;

  gst_element_class_add_static_pad_template_with_gtype (element_class,
      &sink_template, GST_TYPE_WEBRTC_BIN_PAD);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_metadata (element_class, "WebRTC Bin",
      "Filter/Network/WebRTC", "A bin for webrtc connections",
      "Matthew Waters <matthew@centricular.com>");

  gobject_class->get_property = gst_webrtc_bin_get_property;
  gobject_class->set_property = gst_webrtc_bin_set_property;
  gobject_class->dispose = gst_webrtc_bin_dispose;
  gobject_class->finalize = gst_webrtc_bin_finalize;

  g_object_class_install_property (gobject_class,
      PROP_LOCAL_DESCRIPTION,
      g_param_spec_boxed ("local-description", "Local Description",
          "The local SDP description to use for this connection",
          GST_TYPE_WEBRTC_SESSION_DESCRIPTION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_REMOTE_DESCRIPTION,
      g_param_spec_boxed ("remote-description", "Remote Description",
          "The remote SDP description to use for this connection",
          GST_TYPE_WEBRTC_SESSION_DESCRIPTION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_STUN_SERVER,
      g_param_spec_string ("stun-server", "STUN Server",
          "The STUN server of the form stun://hostname:port",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_TURN_SERVER,
      g_param_spec_string ("turn-server", "TURN Server",
          "The TURN server of the form turn(s)://username:password@host:port. "
          "This is a convenience property, use #GstWebRTCBin::add-turn-server "
          "if you wish to use multiple TURN servers",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_CONNECTION_STATE,
      g_param_spec_enum ("connection-state", "Connection State",
          "The overall connection state of this element",
          GST_TYPE_WEBRTC_PEER_CONNECTION_STATE,
          GST_WEBRTC_PEER_CONNECTION_STATE_NEW,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_SIGNALING_STATE,
      g_param_spec_enum ("signaling-state", "Signaling State",
          "The signaling state of this element",
          GST_TYPE_WEBRTC_SIGNALING_STATE,
          GST_WEBRTC_SIGNALING_STATE_STABLE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_ICE_CONNECTION_STATE,
      g_param_spec_enum ("ice-connection-state", "ICE connection state",
          "The collective connection state of all ICETransport's",
          GST_TYPE_WEBRTC_ICE_CONNECTION_STATE,
          GST_WEBRTC_ICE_CONNECTION_STATE_NEW,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_ICE_GATHERING_STATE,
      g_param_spec_enum ("ice-gathering-state", "ICE gathering state",
          "The collective gathering state of all ICETransport's",
          GST_TYPE_WEBRTC_ICE_GATHERING_STATE,
          GST_WEBRTC_ICE_GATHERING_STATE_NEW,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_BUNDLE_POLICY,
      g_param_spec_enum ("bundle-policy", "Bundle Policy",
          "The policy to apply for bundling",
          GST_TYPE_WEBRTC_BUNDLE_POLICY,
          GST_WEBRTC_BUNDLE_POLICY_NONE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstWebRTCBin::create-offer:
   * @object: the #GstWebRtcBin
   * @options: create-offer options
   * @promise: a #GstPromise which will contain the offer
   */
  gst_webrtc_bin_signals[CREATE_OFFER_SIGNAL] =
      g_signal_new_class_handler ("create-offer", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_CALLBACK (gst_webrtc_bin_create_offer), NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_NONE, 2, GST_TYPE_STRUCTURE,
      GST_TYPE_PROMISE);

  /**
   * GstWebRTCBin::create-answer:
   * @object: the #GstWebRtcBin
   * @options: create-answer options
   * @promise: a #GstPromise which will contain the answer
   */
  gst_webrtc_bin_signals[CREATE_ANSWER_SIGNAL] =
      g_signal_new_class_handler ("create-answer", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_CALLBACK (gst_webrtc_bin_create_answer), NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_NONE, 2, GST_TYPE_STRUCTURE,
      GST_TYPE_PROMISE);

  /**
   * GstWebRTCBin::set-local-description:
   * @object: the #GstWebRtcBin
   * @type: the type of description being set
   * @sdp: a #GstSDPMessage description
   * @promise (allow-none): a #GstPromise to be notified when it's set
   */
  gst_webrtc_bin_signals[SET_LOCAL_DESCRIPTION_SIGNAL] =
      g_signal_new_class_handler ("set-local-description",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_CALLBACK (gst_webrtc_bin_set_local_description), NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_NONE, 2,
      GST_TYPE_WEBRTC_SESSION_DESCRIPTION, GST_TYPE_PROMISE);

  /**
   * GstWebRTCBin::set-remote-description:
   * @object: the #GstWebRtcBin
   * @type: the type of description being set
   * @sdp: a #GstSDPMessage description
   * @promise (allow-none): a #GstPromise to be notified when it's set
   */
  gst_webrtc_bin_signals[SET_REMOTE_DESCRIPTION_SIGNAL] =
      g_signal_new_class_handler ("set-remote-description",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_CALLBACK (gst_webrtc_bin_set_remote_description), NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_NONE, 2,
      GST_TYPE_WEBRTC_SESSION_DESCRIPTION, GST_TYPE_PROMISE);

  /**
   * GstWebRTCBin::add-ice-candidate:
   * @object: the #GstWebRtcBin
   * @ice-candidate: an ice candidate
   */
  gst_webrtc_bin_signals[ADD_ICE_CANDIDATE_SIGNAL] =
      g_signal_new_class_handler ("add-ice-candidate",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_CALLBACK (gst_webrtc_bin_add_ice_candidate), NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);

  /**
   * GstWebRTCBin::get-stats:
   * @object: the #GstWebRtcBin
   * @promise: a #GstPromise for the result
   *
   * The @promise will contain the result of retrieving the session statistics.
   * The structure will be named 'application/x-webrtc-stats and contain the
   * following based on the webrtc-stats spec available from
   * https://www.w3.org/TR/webrtc-stats/.  As the webrtc-stats spec is a draft
   * and is constantly changing these statistics may be changed to fit with
   * the latest spec.
   *
   * Each field key is a unique identifer for each RTCStats
   * (https://www.w3.org/TR/webrtc/#rtcstats-dictionary) value (another
   * GstStructure) in the RTCStatsReport
   * (https://www.w3.org/TR/webrtc/#rtcstatsreport-object).  Each supported
   * field in the RTCStats subclass is outlined below.
   *
   * Each statistics structure contains the following values as defined by
   * the RTCStats dictionary (https://www.w3.org/TR/webrtc/#rtcstats-dictionary).
   *
   *  "timestamp"           G_TYPE_DOUBLE               timestamp the statistics were generated
   *  "type"                GST_TYPE_WEBRTC_STATS_TYPE  the type of statistics reported
   *  "id"                  G_TYPE_STRING               unique identifier
   *
   * RTCCodecStats supported fields (https://w3c.github.io/webrtc-stats/#codec-dict*)
   *
   *  "payload-type"        G_TYPE_UINT                 the rtp payload number in use
   *  "clock-rate"          G_TYPE_UINT                 the rtp clock-rate
   *
   * RTCRTPStreamStats supported fields (https://w3c.github.io/webrtc-stats/#streamstats-dict*)
   *
   *  "ssrc"                G_TYPE_STRING               the rtp sequence src in use
   *  "transport-id"        G_TYPE_STRING               identifier for the associated RTCTransportStats for this stream
   *  "codec-id"            G_TYPE_STRING               identifier for the associated RTCCodecStats for this stream
   *  "fir-count"           G_TYPE_UINT                 FIR requests received by the sender (only for local statistics)
   *  "pli-count"           G_TYPE_UINT                 PLI requests received by the sender (only for local statistics)
   *  "nack-count"          G_TYPE_UINT                 NACK requests received by the sender (only for local statistics)
   *
   * RTCReceivedStreamStats supported fields (https://w3c.github.io/webrtc-stats/#receivedrtpstats-dict*)
   *
   *  "packets-received"     G_TYPE_UINT64              number of packets received (only for local inbound)
   *  "bytes-received"       G_TYPE_UINT64              number of bytes received (only for local inbound)
   *  "packets-lost"         G_TYPE_UINT                number of packets lost
   *  "jitter"               G_TYPE_DOUBLE              packet jitter measured in secondss
   *
   * RTCInboundRTPStreamStats supported fields (https://w3c.github.io/webrtc-stats/#inboundrtpstats-dict*)
   *
   *  "remote-id"           G_TYPE_STRING               identifier for the associated RTCRemoteOutboundRTPSTreamStats
   *
   * RTCRemoteInboundRTPStreamStats supported fields (https://w3c.github.io/webrtc-stats/#remoteinboundrtpstats-dict*)
   *
   *  "local-id"            G_TYPE_STRING               identifier for the associated RTCOutboundRTPSTreamStats
   *  "round-trip-time"     G_TYPE_DOUBLE               round trip time of packets measured in seconds
   *
   * RTCSentRTPStreamStats supported fields (https://w3c.github.io/webrtc-stats/#sentrtpstats-dict*)
   *
   *  "packets-sent"        G_TYPE_UINT64               number of packets sent (only for local outbound)
   *  "bytes-sent"          G_TYPE_UINT64               number of packets sent (only for local outbound)
   *
   * RTCOutboundRTPStreamStats supported fields (https://w3c.github.io/webrtc-stats/#outboundrtpstats-dict*)
   *
   *  "remote-id"           G_TYPE_STRING               identifier for the associated RTCRemoteInboundRTPSTreamStats
   *
   * RTCRemoteOutboundRTPStreamStats supported fields (https://w3c.github.io/webrtc-stats/#remoteoutboundrtpstats-dict*)
   *
   *  "local-id"            G_TYPE_STRING               identifier for the associated RTCInboundRTPSTreamStats
   *
   */
  gst_webrtc_bin_signals[GET_STATS_SIGNAL] =
      g_signal_new_class_handler ("get-stats",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_CALLBACK (gst_webrtc_bin_get_stats), NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_NONE, 2, GST_TYPE_PAD,
      GST_TYPE_PROMISE);

  /**
   * GstWebRTCBin::on-negotiation-needed:
   * @object: the #GstWebRtcBin
   */
  gst_webrtc_bin_signals[ON_NEGOTIATION_NEEDED_SIGNAL] =
      g_signal_new ("on-negotiation-needed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 0);

  /**
   * GstWebRTCBin::on-ice-candidate:
   * @object: the #GstWebRtcBin
   * @candidate: the ICE candidate
   */
  gst_webrtc_bin_signals[ON_ICE_CANDIDATE_SIGNAL] =
      g_signal_new ("on-ice-candidate", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);

  /**
   * GstWebRTCBin::on-new-transceiver:
   * @object: the #GstWebRtcBin
   * @candidate: the new #GstWebRTCRTPTransceiver
   */
  gst_webrtc_bin_signals[ON_NEW_TRANSCEIVER_SIGNAL] =
      g_signal_new ("on-new-transceiver", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 1, GST_TYPE_WEBRTC_RTP_TRANSCEIVER);

  /**
   * GstWebRTCBin::on-data-channel:
   * @object: the #GstWebRtcBin
   * @candidate: the new #GstWebRTCDataChannel
   */
  gst_webrtc_bin_signals[ON_DATA_CHANNEL_SIGNAL] =
      g_signal_new ("on-data-channel", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 1, GST_TYPE_WEBRTC_DATA_CHANNEL);

  /**
   * GstWebRTCBin::add-transceiver:
   * @object: the #GstWebRtcBin
   * @direction: the direction of the new transceiver
   * @caps: (allow none): the codec preferences for this transceiver
   *
   * Returns: the new #GstWebRTCRTPTransceiver
   */
  gst_webrtc_bin_signals[ADD_TRANSCEIVER_SIGNAL] =
      g_signal_new_class_handler ("add-transceiver", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_CALLBACK (gst_webrtc_bin_add_transceiver), NULL, NULL,
      g_cclosure_marshal_generic, GST_TYPE_WEBRTC_RTP_TRANSCEIVER, 2,
      GST_TYPE_WEBRTC_RTP_TRANSCEIVER_DIRECTION, GST_TYPE_CAPS);

  /**
   * GstWebRTCBin::get-transceivers:
   * @object: the #GstWebRtcBin
   *
   * Returns: a #GArray of #GstWebRTCRTPTransceivers
   */
  gst_webrtc_bin_signals[GET_TRANSCEIVERS_SIGNAL] =
      g_signal_new_class_handler ("get-transceivers", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_CALLBACK (gst_webrtc_bin_get_transceivers), NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_ARRAY, 0);

  /**
   * GstWebRTCBin::add-turn-server:
   * @object: the #GstWebRtcBin
   * @uri: The uri of the server of the form turn(s)://username:password@host:port
   *
   * Add a turn server to obtain ICE candidates from
   */
  gst_webrtc_bin_signals[ADD_TURN_SERVER_SIGNAL] =
      g_signal_new_class_handler ("add-turn-server", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_CALLBACK (gst_webrtc_bin_add_turn_server), NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_BOOLEAN, 1, G_TYPE_STRING);

  /*
   * GstWebRTCBin::create-data-channel:
   * @object: the #GstWebRtcBin
   * @label: the label for the data channel
   * @options: a #GstStructure of options for creating the data channel
   *
   * The options dictionary is the same format as the RTCDataChannelInit
   * members outlined https://www.w3.org/TR/webrtc/#dom-rtcdatachannelinit and
   * and reproduced below
   *
   *  ordered               G_TYPE_BOOLEAN        Whether the channal will send data with guarenteed ordering
   *  max-packet-lifetime   G_TYPE_INT            The time in milliseconds to attempt transmitting unacknowledged data. -1 for unset
   *  max-retransmits       G_TYPE_INT            The number of times data will be attempted to be transmitted without acknowledgement before dropping
   *  protocol              G_TYPE_STRING         The subprotocol used by this channel
   *  negotiated            G_TYPE_BOOLEAN        Whether the created data channel should not perform in-band chnanel announcment.  If %TRUE, then application must negotiate the channel itself and create the corresponding channel on the peer with the same id.
   *  id                    G_TYPE_INT            Override the default identifier selection of this channel
   *  priority              GST_TYPE_WEBRTC_PRIORITY_TYPE   The priority to use for this channel
   *
   * Returns: a new data channel object
   */
  gst_webrtc_bin_signals[CREATE_DATA_CHANNEL_SIGNAL] =
      g_signal_new_class_handler ("create-data-channel",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_CALLBACK (gst_webrtc_bin_create_data_channel), NULL, NULL,
      g_cclosure_marshal_generic, GST_TYPE_WEBRTC_DATA_CHANNEL, 2,
      G_TYPE_STRING, GST_TYPE_STRUCTURE);
}

static void
_deref_unparent_and_unref (GObject ** object)
{
  GstObject *obj = GST_OBJECT (*object);

  GST_OBJECT_PARENT (obj) = NULL;

  gst_object_unref (*object);
}

static void
_transport_free (GObject ** object)
{
  TransportStream *stream = (TransportStream *) * object;
  GstWebRTCBin *webrtc;

  webrtc = GST_WEBRTC_BIN (GST_OBJECT_PARENT (stream));

  if (stream->transport) {
    g_signal_handlers_disconnect_by_data (stream->transport->transport, webrtc);
    g_signal_handlers_disconnect_by_data (stream->transport, webrtc);
  }
  if (stream->rtcp_transport) {
    g_signal_handlers_disconnect_by_data (stream->rtcp_transport->transport,
        webrtc);
    g_signal_handlers_disconnect_by_data (stream->rtcp_transport, webrtc);
  }

  gst_object_unref (*object);
}

static void
gst_webrtc_bin_init (GstWebRTCBin * webrtc)
{
  webrtc->priv = gst_webrtc_bin_get_instance_private (webrtc);
  g_mutex_init (PC_GET_LOCK (webrtc));
  g_cond_init (PC_GET_COND (webrtc));

  webrtc->rtpbin = _create_rtpbin (webrtc);
  gst_bin_add (GST_BIN (webrtc), webrtc->rtpbin);

  webrtc->priv->transceivers = g_array_new (FALSE, TRUE, sizeof (gpointer));
  g_array_set_clear_func (webrtc->priv->transceivers,
      (GDestroyNotify) _deref_unparent_and_unref);

  webrtc->priv->transports = g_array_new (FALSE, TRUE, sizeof (gpointer));
  g_array_set_clear_func (webrtc->priv->transports,
      (GDestroyNotify) _transport_free);

  webrtc->priv->data_channels = g_array_new (FALSE, TRUE, sizeof (gpointer));
  g_array_set_clear_func (webrtc->priv->data_channels,
      (GDestroyNotify) _deref_and_unref);

  webrtc->priv->pending_data_channels =
      g_array_new (FALSE, TRUE, sizeof (gpointer));
  g_array_set_clear_func (webrtc->priv->pending_data_channels,
      (GDestroyNotify) _deref_and_unref);

  webrtc->priv->session_mid_map =
      g_array_new (FALSE, TRUE, sizeof (SessionMidItem));
  g_array_set_clear_func (webrtc->priv->session_mid_map,
      (GDestroyNotify) clear_session_mid_item);

  webrtc->priv->ice = gst_webrtc_ice_new ();
  g_signal_connect (webrtc->priv->ice, "on-ice-candidate",
      G_CALLBACK (_on_ice_candidate), webrtc);
  webrtc->priv->ice_stream_map =
      g_array_new (FALSE, TRUE, sizeof (IceStreamItem));
  webrtc->priv->pending_ice_candidates =
      g_array_new (FALSE, TRUE, sizeof (IceCandidateItem *));
  g_array_set_clear_func (webrtc->priv->pending_ice_candidates,
      (GDestroyNotify) _clear_ice_candidate_item);

  /* we start off closed until we move to READY */
  webrtc->priv->is_closed = TRUE;
}
