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

#ifndef __GST_WEBRTC_BIN_H__
#define __GST_WEBRTC_BIN_H__

#include <gst/sdp/sdp.h>
#include "fwd.h"
#include "gstwebrtcice.h"
#include "transportstream.h"

G_BEGIN_DECLS

GType gst_webrtc_bin_pad_get_type(void);
#define GST_TYPE_WEBRTC_BIN_PAD            (gst_webrtc_bin_pad_get_type())
#define GST_WEBRTC_BIN_PAD(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_WEBRTC_BIN_PAD,GstWebRTCBinPad))
#define GST_IS_WEBRTC_BIN_PAD(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_WEBRTC_BIN_PAD))
#define GST_WEBRTC_BIN_PAD_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_WEBRTC_BIN_PAD,GstWebRTCBinPadClass))
#define GST_IS_WEBRTC_BIN_PAD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_WEBRTC_BIN_PAD))
#define GST_WEBRTC_BIN_PAD_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_WEBRTC_BIN_PAD,GstWebRTCBinPadClass))

typedef struct _GstWebRTCBinPad GstWebRTCBinPad;
typedef struct _GstWebRTCBinPadClass GstWebRTCBinPadClass;

struct _GstWebRTCBinPad
{
  GstGhostPad           parent;

  guint                 mlineindex;

  GstWebRTCRTPTransceiver *trans;
  gulong                block_id;

  GstCaps              *received_caps;
};

struct _GstWebRTCBinPadClass
{
  GstGhostPadClass      parent_class;
};

GType gst_webrtc_bin_get_type(void);
#define GST_TYPE_WEBRTC_BIN            (gst_webrtc_bin_get_type())
#define GST_WEBRTC_BIN(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_WEBRTC_BIN,GstWebRTCBin))
#define GST_IS_WEBRTC_BIN(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_WEBRTC_BIN))
#define GST_WEBRTC_BIN_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_WEBRTC_BIN,GstWebRTCBinClass))
#define GST_IS_WEBRTC_BIN_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_WEBRTC_BIN))
#define GST_WEBRTC_BIN_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_WEBRTC_BIN,GstWebRTCBinClass))

struct _GstWebRTCBin
{
  GstBin                            parent;

  GstElement                       *rtpbin;
  GstElement                       *rtpfunnel;

  GstWebRTCSignalingState           signaling_state;
  GstWebRTCICEGatheringState        ice_gathering_state;
  GstWebRTCICEConnectionState       ice_connection_state;
  GstWebRTCPeerConnectionState      peer_connection_state;

  GstWebRTCSessionDescription      *current_local_description;
  GstWebRTCSessionDescription      *pending_local_description;
  GstWebRTCSessionDescription      *current_remote_description;
  GstWebRTCSessionDescription      *pending_remote_description;

  GstWebRTCBundlePolicy             bundle_policy;
  GstWebRTCICETransportPolicy       ice_transport_policy;

  GstWebRTCBinPrivate              *priv;
};

struct _GstWebRTCBinClass
{
  GstBinClass           parent_class;
};

struct _GstWebRTCBinPrivate
{
  guint max_sink_pad_serial;

  gboolean bundle;
  GArray *transceivers;
  GArray *session_mid_map;
  GArray *transports;
  GArray *data_channels;
  /* list of data channels we've received a sctp stream for but no data
   * channel protocol for */
  GArray *pending_data_channels;

  GstWebRTCSCTPTransport *sctp_transport;
  TransportStream *data_channel_transport;

  GstWebRTCICE *ice;
  GArray *ice_stream_map;
  GArray *pending_ice_candidates;

  /* peerconnection variables */
  gboolean is_closed;
  gboolean need_negotiation;

  /* peerconnection helper thread for promises */
  GMainContext *main_context;
  GMainLoop *loop;
  GThread *thread;
  GMutex pc_lock;
  GCond pc_cond;

  gboolean running;
  gboolean async_pending;

  GList *pending_pads;
  GList *pending_sink_transceivers;

  /* count of the number of media streams we've offered for uniqueness */
  /* FIXME: overflow? */
  guint media_counter;
  /* the number of times create_offer has been called for the version field */
  guint offer_count;
  GstWebRTCSessionDescription *last_generated_offer;
  GstWebRTCSessionDescription *last_generated_answer;

  GstStructure *stats;
};

typedef void (*GstWebRTCBinFunc) (GstWebRTCBin * webrtc, gpointer data);

typedef struct
{
  GstWebRTCBin *webrtc;
  GstWebRTCBinFunc op;
  gpointer data;
  GDestroyNotify notify;
//  GstPromise *promise;      /* FIXME */
} GstWebRTCBinTask;

void            gst_webrtc_bin_enqueue_task             (GstWebRTCBin * pc,
                                                         GstWebRTCBinFunc func,
                                                         gpointer data,
                                                         GDestroyNotify notify);

G_END_DECLS

#endif /* __GST_WEBRTC_BIN_H__ */
