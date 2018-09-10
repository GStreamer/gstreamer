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

#include "transportreceivebin.h"
#include "utils.h"

/*
 * ,----------------------------transport_receive_%u----------------------------,
 * ;     (rtp/data)                                                             ;
 * ;  ,---nicesrc----,  ,-capsfilter-,  ,---dtlssrtpdec---,       ,--funnel--,  ;
 * ;  ;          src o--o sink   src o--o sink    rtp_src o-------o sink_0   ;  ;
 * ;  '--------------'  '------------'  ;                 ;       ;      src o--o rtp_src
 * ;                                    ;        rtcp_src o---, ,-o sink_1   ;  ;
 * ;                                    ;                 ;   ; ; '----------'  ;
 * ;                                    ;        data_src o-, ; ; ,--funnel--,  ;
 * ;                                    '-----------------' ; '-+-o sink_0   ;  ;
 * ;                                    ,---dtlssrtpdec---, ; ,-' ;      src o--o rtcp_src
 * ;       (rtcp)                       ;         rtp_src o-+-' ,-o sink_1   ;  ;
 * ;  ,---nicesrc----,  ,-capsfilter-,  ;                 ; ;   ; '----------'  ;
 * ;  ;          src o--o sink   src o--o sink   rtcp_src o-+---' ,--funnel--,  ;
 * ;  '--------------'  '------------'  ;                 ; '-----o sink_0   ;  ;
 * ;                                    ;        data_src o-,     ;      src o--o data_src
 * ;                                    '-----------------' '-----o sink_1   ;  ;
 * ;                                                              '----------'  ;
 * '----------------------------------------------------------------------------'
 *
 * Do we really wnat to be *that* permissive in what we accept?
 *
 * FIXME: When and how do we want to clear the possibly stored buffers?
 */

#define GST_CAT_DEFAULT gst_webrtc_transport_receive_bin_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define transport_receive_bin_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (TransportReceiveBin, transport_receive_bin,
    GST_TYPE_BIN,
    GST_DEBUG_CATEGORY_INIT (gst_webrtc_transport_receive_bin_debug,
        "webrtctransportreceivebin", 0, "webrtctransportreceivebin");
    );

static GstStaticPadTemplate rtp_sink_template =
GST_STATIC_PAD_TEMPLATE ("rtp_src",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp"));

static GstStaticPadTemplate rtcp_sink_template =
GST_STATIC_PAD_TEMPLATE ("rtcp_src",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp"));

static GstStaticPadTemplate data_sink_template =
GST_STATIC_PAD_TEMPLATE ("data_src",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

enum
{
  PROP_0,
  PROP_STREAM,
};

static const gchar *
_receive_state_to_string (ReceiveState state)
{
  switch (state) {
    case RECEIVE_STATE_BLOCK:
      return "block";
    case RECEIVE_STATE_DROP:
      return "drop";
    case RECEIVE_STATE_PASS:
      return "pass";
    default:
      return "Unknown";
  }
}

static GstPadProbeReturn
pad_block (GstPad * pad, GstPadProbeInfo * info, TransportReceiveBin * receive)
{
  GstPadProbeReturn ret;

  g_mutex_lock (&receive->pad_block_lock);
  while (receive->receive_state == RECEIVE_STATE_BLOCK) {
    g_cond_wait (&receive->pad_block_cond, &receive->pad_block_lock);
    GST_DEBUG_OBJECT (pad, "probe waited. new state %s",
        _receive_state_to_string (receive->receive_state));
  }
  ret = GST_PAD_PROBE_PASS;

  if (receive->receive_state == RECEIVE_STATE_DROP) {
    ret = GST_PAD_PROBE_DROP;
  } else if (receive->receive_state == RECEIVE_STATE_PASS) {
    ret = GST_PAD_PROBE_OK;
  }

  g_mutex_unlock (&receive->pad_block_lock);

  return ret;
}

void
transport_receive_bin_set_receive_state (TransportReceiveBin * receive,
    ReceiveState state)
{
  g_mutex_lock (&receive->pad_block_lock);
  receive->receive_state = state;
  GST_DEBUG_OBJECT (receive, "changing receive state to %s",
      _receive_state_to_string (state));
  g_cond_signal (&receive->pad_block_cond);
  g_mutex_unlock (&receive->pad_block_lock);
}

static void
transport_receive_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  TransportReceiveBin *receive = TRANSPORT_RECEIVE_BIN (object);

  GST_OBJECT_LOCK (receive);
  switch (prop_id) {
    case PROP_STREAM:
      /* XXX: weak-ref this? */
      receive->stream = TRANSPORT_STREAM (g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (receive);
}

static void
transport_receive_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  TransportReceiveBin *receive = TRANSPORT_RECEIVE_BIN (object);

  GST_OBJECT_LOCK (receive);
  switch (prop_id) {
    case PROP_STREAM:
      g_value_set_object (value, receive->stream);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (receive);
}

static void
transport_receive_bin_finalize (GObject * object)
{
  TransportReceiveBin *receive = TRANSPORT_RECEIVE_BIN (object);

  g_mutex_clear (&receive->pad_block_lock);
  g_cond_clear (&receive->pad_block_cond);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstStateChangeReturn
transport_receive_bin_change_state (GstElement * element,
    GstStateChange transition)
{
  TransportReceiveBin *receive = TRANSPORT_RECEIVE_BIN (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  GST_DEBUG ("changing state: %s => %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:{
      GstElement *elem;

      receive->rtp_block =
          _create_pad_block (GST_ELEMENT (receive), receive->rtp_src, 0, NULL,
          NULL);
      receive->rtp_block->block_id =
          gst_pad_add_probe (receive->rtp_src, GST_PAD_PROBE_TYPE_ALL_BOTH,
          (GstPadProbeCallback) pad_block, receive, NULL);

      /* XXX: because nice needs the nicesrc internal main loop running in order
       * correctly STUN... */
      /* FIXME: this races with the pad exposure later and may get not-linked */
      elem = receive->stream->transport->transport->src;
      gst_element_set_locked_state (elem, TRUE);
      gst_element_set_state (elem, GST_STATE_PLAYING);
      elem = receive->stream->rtcp_transport->transport->src;
      gst_element_set_locked_state (elem, TRUE);
      gst_element_set_state (elem, GST_STATE_PLAYING);
      break;
    }
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:{
      GstElement *elem;

      elem = receive->stream->transport->transport->src;
      gst_element_set_locked_state (elem, FALSE);
      gst_element_set_state (elem, GST_STATE_NULL);
      elem = receive->stream->rtcp_transport->transport->src;
      gst_element_set_locked_state (elem, FALSE);
      gst_element_set_state (elem, GST_STATE_NULL);

      if (receive->rtp_block)
        _free_pad_block (receive->rtp_block);
      receive->rtp_block = NULL;
      break;
    }
    default:
      break;
  }

  return ret;
}

static void
rtp_queue_overrun (GstElement * queue, TransportReceiveBin * receive)
{
  GST_WARNING_OBJECT (receive, "Internal receive queue overrun. Dropping data");
}

static void
transport_receive_bin_constructed (GObject * object)
{
  TransportReceiveBin *receive = TRANSPORT_RECEIVE_BIN (object);
  GstWebRTCDTLSTransport *transport;
  GstPad *ghost, *pad;
  GstElement *capsfilter, *funnel, *queue;
  GstCaps *caps;

  g_return_if_fail (receive->stream);

  /* link ice src, dtlsrtp together for rtp */
  transport = receive->stream->transport;
  gst_bin_add (GST_BIN (receive), GST_ELEMENT (transport->dtlssrtpdec));

  capsfilter = gst_element_factory_make ("capsfilter", NULL);
  caps = gst_caps_new_empty_simple ("application/x-rtp");
  g_object_set (capsfilter, "caps", caps, NULL);
  gst_caps_unref (caps);

  gst_bin_add (GST_BIN (receive), GST_ELEMENT (capsfilter));
  if (!gst_element_link_pads (capsfilter, "src", transport->dtlssrtpdec,
          "sink"))
    g_warn_if_reached ();

  gst_bin_add (GST_BIN (receive), GST_ELEMENT (transport->transport->src));

  if (!gst_element_link_pads (GST_ELEMENT (transport->transport->src), "src",
          GST_ELEMENT (capsfilter), "sink"))
    g_warn_if_reached ();

  /* link ice src, dtlsrtp together for rtcp */
  transport = receive->stream->rtcp_transport;
  gst_bin_add (GST_BIN (receive), GST_ELEMENT (transport->dtlssrtpdec));

  capsfilter = gst_element_factory_make ("capsfilter", NULL);
  caps = gst_caps_new_empty_simple ("application/x-rtcp");
  g_object_set (capsfilter, "caps", caps, NULL);
  gst_caps_unref (caps);

  gst_bin_add (GST_BIN (receive), GST_ELEMENT (capsfilter));
  if (!gst_element_link_pads (capsfilter, "src", transport->dtlssrtpdec,
          "sink"))
    g_warn_if_reached ();

  gst_bin_add (GST_BIN (receive), GST_ELEMENT (transport->transport->src));

  if (!gst_element_link_pads (GST_ELEMENT (transport->transport->src), "src",
          GST_ELEMENT (capsfilter), "sink"))
    g_warn_if_reached ();

  /* create funnel for rtp_src */
  funnel = gst_element_factory_make ("funnel", NULL);
  gst_bin_add (GST_BIN (receive), funnel);
  if (!gst_element_link_pads (receive->stream->transport->dtlssrtpdec,
          "rtp_src", funnel, "sink_0"))
    g_warn_if_reached ();
  if (!gst_element_link_pads (receive->stream->rtcp_transport->dtlssrtpdec,
          "rtp_src", funnel, "sink_1"))
    g_warn_if_reached ();

  queue = gst_element_factory_make ("queue", NULL);
  /* FIXME: make this configurable? */
  g_object_set (queue, "leaky", 2, "max-size-time", (guint64) 0,
      "max-size-buffers", 0, "max-size-bytes", 5 * 1024 * 1024, NULL);
  g_signal_connect (queue, "overrun", G_CALLBACK (rtp_queue_overrun), receive);
  gst_bin_add (GST_BIN (receive), queue);
  if (!gst_element_link_pads (funnel, "src", queue, "sink"))
    g_warn_if_reached ();

  pad = gst_element_get_static_pad (queue, "src");
  receive->rtp_src = gst_ghost_pad_new ("rtp_src", pad);

  gst_element_add_pad (GST_ELEMENT (receive), receive->rtp_src);
  gst_object_unref (pad);

  /* create funnel for rtcp_src */
  funnel = gst_element_factory_make ("funnel", NULL);
  gst_bin_add (GST_BIN (receive), funnel);
  if (!gst_element_link_pads (receive->stream->transport->dtlssrtpdec,
          "rtcp_src", funnel, "sink_0"))
    g_warn_if_reached ();
  if (!gst_element_link_pads (receive->stream->rtcp_transport->dtlssrtpdec,
          "rtcp_src", funnel, "sink_1"))
    g_warn_if_reached ();

  pad = gst_element_get_static_pad (funnel, "src");
  ghost = gst_ghost_pad_new ("rtcp_src", pad);
  gst_element_add_pad (GST_ELEMENT (receive), ghost);
  gst_object_unref (pad);

  /* create funnel for data_src */
  funnel = gst_element_factory_make ("funnel", NULL);
  gst_bin_add (GST_BIN (receive), funnel);
  if (!gst_element_link_pads (receive->stream->transport->dtlssrtpdec,
          "data_src", funnel, "sink_0"))
    g_warn_if_reached ();
  if (!gst_element_link_pads (receive->stream->rtcp_transport->dtlssrtpdec,
          "data_src", funnel, "sink_1"))
    g_warn_if_reached ();

  pad = gst_element_get_static_pad (funnel, "src");
  ghost = gst_ghost_pad_new ("data_src", pad);
  gst_element_add_pad (GST_ELEMENT (receive), ghost);
  gst_object_unref (pad);

  G_OBJECT_CLASS (parent_class)->constructed (object);
}

static void
transport_receive_bin_class_init (TransportReceiveBinClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;

  element_class->change_state = transport_receive_bin_change_state;

  gst_element_class_add_static_pad_template (element_class, &rtp_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &rtcp_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &data_sink_template);

  gst_element_class_set_metadata (element_class, "WebRTC Transport Receive Bin",
      "Filter/Network/WebRTC", "A bin for webrtc connections",
      "Matthew Waters <matthew@centricular.com>");

  gobject_class->constructed = transport_receive_bin_constructed;
  gobject_class->get_property = transport_receive_bin_get_property;
  gobject_class->set_property = transport_receive_bin_set_property;
  gobject_class->finalize = transport_receive_bin_finalize;

  g_object_class_install_property (gobject_class,
      PROP_STREAM,
      g_param_spec_object ("stream", "Stream",
          "The TransportStream for this receiveing bin",
          transport_stream_get_type (),
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

static void
transport_receive_bin_init (TransportReceiveBin * receive)
{
  g_mutex_init (&receive->pad_block_lock);
  g_cond_init (&receive->pad_block_cond);
}
