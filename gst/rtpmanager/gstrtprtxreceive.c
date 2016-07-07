/* RTP Retransmission receiver element for GStreamer
 *
 * gstrtprtxreceive.c:
 *
 * Copyright (C) 2013 Collabora Ltd.
 *   @author Julien Isorce <julien.isorce@collabora.co.uk>
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
 * SECTION:element-rtprtxreceive
 * @see_also: rtprtxsend, rtpsession, rtpjitterbuffer
 *
 * The receiver will listen to the custom retransmission events from the
 * downstream jitterbuffer and will remember the SSRC1 of the stream and
 * seqnum that was requested. When it sees a packet with one of the stored
 * seqnum, it associates the SSRC2 of the stream with the SSRC1 of the
 * master stream. From then it knows that SSRC2 is the retransmission
 * stream of SSRC1. This algorithm is stated in RFC 4588. For this
 * algorithm to work, RFC4588 also states that no two pending retransmission
 * requests can exist for the same seqnum and different SSRCs or else it
 * would be impossible to associate the retransmission with the original
 * requester SSRC.
 * When the RTX receiver has associated the retransmission packets,
 * it can depayload and forward them to the source pad of the element.
 * RTX is SSRC-multiplexed. See #GstRtpRtxSend
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch-1.0 rtpsession name=rtpsession \
 *         audiotestsrc ! speexenc ! rtpspeexpay pt=97 ! rtprtxsend rtx-payload-type=99 ! \
 *             identity drop-probability=0.1 ! rtpsession.send_rtp_sink \
 *             rtpsession.send_rtp_src ! udpsink host="127.0.0.1" port=5000 \
 *         udpsrc port=5001 ! rtpsession.recv_rtcp_sink \
 *         rtpsession.send_rtcp_src ! udpsink host="127.0.0.1" port=5002 sync=false async=false
 * ]| Send audio stream through port 5000. (5001 and 5002 are just the rtcp link with the receiver)
 * |[
 * gst-launch-1.0 rtpsession name=rtpsession \
 *         udpsrc port=5000 caps="application/x-rtp,media=(string)audio,clock-rate=(int)44100,encoding-name=(string)SPEEX,encoding-params=(string)1,octet-align=(string)1" ! \
 *             rtpsession.recv_rtp_sink \
 *             rtpsession.recv_rtp_src ! rtprtxreceive rtx-payload-types="99" ! rtpjitterbuffer do-retransmission=true ! rtpspeexdepay ! \
 *             speexdec ! audioconvert ! autoaudiosink \
 *         rtpsession.send_rtcp_src ! udpsink host="127.0.0.1" port=5001 \
 *         udpsrc port=5002 ! rtpsession.recv_rtcp_sink sync=fakse async=false
 * ]| Receive audio stream from port 5000. (5001 and 5002 are just the rtcp link with the sender)
 * On sender side make sure to use a different payload type for the stream and
 * its associated retransmission stream (see #GstRtpRtxSend). Note that several retransmission streams can
 * have the same payload type so this is not deterministic. Actually the
 * rtprtxreceiver element does the association using seqnum values.
 * On receiver side set all the retransmission payload types (Those informations are retrieve
 * through SDP).
 * You should still hear a clear sound when setting drop-probability to something greater than 0.
 * The rtpjitterbuffer will generate a custom upstream event GstRTPRetransmissionRequest when
 * it assumes that one packet is missing. Then this request is translated to a FB NACK in the rtcp link
 * Finally the rtpsession of the sender side re-convert it in a GstRTPRetransmissionRequest that will
 * be handle by rtprtxsend.
 * When increasing this value it may be possible that even the retransmission stream would be dropped
 * so the receiver will ask to resend the packets again and again until it actually receive them.
 * If the value is too high the rtprtxsend will not be able to retrieve the packet in its list of
 * stored packets. For learning purpose you could try to increase the max-size-packets or max-size-time
 * rtprtxsender's properties.
 * Also note that you should use rtprtxsend through rtpbin and its set-aux-send property. See #GstRtpBin.
 * |[
 * gst-launch-1.0 rtpsession name=rtpsession0 \
 *         audiotestsrc wave=0 ! speexenc ! rtpspeexpay pt=97 ! rtprtxsend rtx-payload-type=99 seqnum-offset=1 ! \
 *             identity drop-probability=0.1 ! rtpsession0.send_rtp_sink \
 *             rtpsession0.send_rtp_src ! udpsink host="127.0.0.1" port=5000 \
 *         udpsrc port=5001 ! rtpsession0.recv_rtcp_sink \
 *         rtpsession0.send_rtcp_src ! udpsink host="127.0.0.1" port=5002 sync=false async=false \
 *                rtpsession name=rtpsession1 \
 *         audiotestsrc wave=0 ! speexenc ! rtpspeexpay pt=97 ! rtprtxsend rtx-payload-type=99 seqnum-offset=10 ! \
 *             identity drop-probability=0.1 ! rtpsession1.send_rtp_sink \
 *             rtpsession1.send_rtp_src ! udpsink host="127.0.0.1" port=5000 \
 *         udpsrc port=5004 ! rtpsession1.recv_rtcp_sink \
 *         rtpsession1.send_rtcp_src ! udpsink host="127.0.0.1" port=5002 sync=false async=false
 * ]| Send two audio streams to port 5000.
 * |[
 * gst-launch-1.0 rtpsession name=rtpsession
 *         udpsrc port=5000 caps="application/x-rtp,media=(string)audio,clock-rate=(int)44100,encoding-name=(string)SPEEX,encoding-params=(string)1,octet-align=(string)1" ! \
 *             rtpsession.recv_rtp_sink \
 *             rtpsession.recv_rtp_src ! rtprtxreceive rtx-payload-types="99" ! rtpssrcdemux name=demux \
 *             demux. ! queue ! rtpjitterbuffer do-retransmission=true ! rtpspeexdepay ! speexdec ! audioconvert ! autoaudiosink \
 *             demux. ! queue ! rtpjitterbuffer do-retransmission=true ! rtpspeexdepay ! speexdec ! audioconvert ! autoaudiosink \
 *         rtpsession.send_rtcp_src ! ! tee name=t ! queue ! udpsink host="127.0.0.1" port=5001 t. ! queue ! udpsink host="127.0.0.1" port=5004 \
 *         udpsrc port=5002 ! rtpsession.recv_rtcp_sink sync=fakse async=false
 * ]| Receive audio stream from port 5000.
 * On sender side the two streams have the same payload type for master streams, Same about retransmission streams.
 * The streams are sent to the network through two distincts sessions.
 * But we need to set a different seqnum-offset to make sure their seqnum navigate at a different rate like in concrete cases.
 * We could also choose the same seqnum offset but we would require to set a different initial seqnum value.
 * This is also why the rtprtxreceive can succeed to do the association between master and retransmission stream.
 * On receiver side the same session is used to receive the two streams. So the rtpssrcdemux is here to demultiplex
 * those two streams. The rtprtxreceive is responsible for reconstructing the original packets from the two retransmission streams.
 * You can play with the drop-probability value for one or both streams.
 * You should hear a clear sound. (after a few seconds the two streams wave feel synchronized)
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <string.h>
#include <stdlib.h>

#include "gstrtprtxreceive.h"

#define ASSOC_TIMEOUT (GST_SECOND)

GST_DEBUG_CATEGORY_STATIC (gst_rtp_rtx_receive_debug);
#define GST_CAT_DEFAULT gst_rtp_rtx_receive_debug

enum
{
  PROP_0,
  PROP_PAYLOAD_TYPE_MAP,
  PROP_NUM_RTX_REQUESTS,
  PROP_NUM_RTX_PACKETS,
  PROP_NUM_RTX_ASSOC_PACKETS
};

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static gboolean gst_rtp_rtx_receive_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static GstFlowReturn gst_rtp_rtx_receive_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);

static GstStateChangeReturn gst_rtp_rtx_receive_change_state (GstElement *
    element, GstStateChange transition);

static void gst_rtp_rtx_receive_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_rtx_receive_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_rtp_rtx_receive_finalize (GObject * object);

G_DEFINE_TYPE (GstRtpRtxReceive, gst_rtp_rtx_receive, GST_TYPE_ELEMENT);

static void
gst_rtp_rtx_receive_class_init (GstRtpRtxReceiveClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->get_property = gst_rtp_rtx_receive_get_property;
  gobject_class->set_property = gst_rtp_rtx_receive_set_property;
  gobject_class->finalize = gst_rtp_rtx_receive_finalize;

  g_object_class_install_property (gobject_class, PROP_PAYLOAD_TYPE_MAP,
      g_param_spec_boxed ("payload-type-map", "Payload Type Map",
          "Map of original payload types to their retransmission payload types",
          GST_TYPE_STRUCTURE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_NUM_RTX_REQUESTS,
      g_param_spec_uint ("num-rtx-requests", "Num RTX Requests",
          "Number of retransmission events received", 0, G_MAXUINT,
          0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_NUM_RTX_PACKETS,
      g_param_spec_uint ("num-rtx-packets", "Num RTX Packets",
          " Number of retransmission packets received", 0, G_MAXUINT,
          0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_NUM_RTX_ASSOC_PACKETS,
      g_param_spec_uint ("num-rtx-assoc-packets",
          "Num RTX Associated Packets", "Number of retransmission packets "
          "correctly associated with retransmission requests", 0, G_MAXUINT,
          0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (gstelement_class, &src_factory);
  gst_element_class_add_static_pad_template (gstelement_class, &sink_factory);

  gst_element_class_set_static_metadata (gstelement_class,
      "RTP Retransmission receiver", "Codec",
      "Receive retransmitted RTP packets according to RFC4588",
      "Julien Isorce <julien.isorce@collabora.co.uk>");

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_rtp_rtx_receive_change_state);
}

static void
gst_rtp_rtx_receive_reset (GstRtpRtxReceive * rtx)
{
  GST_OBJECT_LOCK (rtx);
  g_hash_table_remove_all (rtx->ssrc2_ssrc1_map);
  g_hash_table_remove_all (rtx->seqnum_ssrc1_map);
  rtx->num_rtx_requests = 0;
  rtx->num_rtx_packets = 0;
  rtx->num_rtx_assoc_packets = 0;
  GST_OBJECT_UNLOCK (rtx);
}

static void
gst_rtp_rtx_receive_finalize (GObject * object)
{
  GstRtpRtxReceive *rtx = GST_RTP_RTX_RECEIVE (object);

  g_hash_table_unref (rtx->ssrc2_ssrc1_map);
  g_hash_table_unref (rtx->seqnum_ssrc1_map);
  g_hash_table_unref (rtx->rtx_pt_map);
  if (rtx->rtx_pt_map_structure)
    gst_structure_free (rtx->rtx_pt_map_structure);

  G_OBJECT_CLASS (gst_rtp_rtx_receive_parent_class)->finalize (object);
}

typedef struct
{
  guint32 ssrc;
  GstClockTime time;
} SsrcAssoc;

static SsrcAssoc *
ssrc_assoc_new (guint32 ssrc, GstClockTime time)
{
  SsrcAssoc *assoc = g_slice_new (SsrcAssoc);

  assoc->ssrc = ssrc;
  assoc->time = time;

  return assoc;
}

static void
ssrc_assoc_free (SsrcAssoc * assoc)
{
  g_slice_free (SsrcAssoc, assoc);
}

static void
gst_rtp_rtx_receive_init (GstRtpRtxReceive * rtx)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (rtx);

  rtx->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
  GST_PAD_SET_PROXY_CAPS (rtx->srcpad);
  GST_PAD_SET_PROXY_ALLOCATION (rtx->srcpad);
  gst_pad_set_event_function (rtx->srcpad,
      GST_DEBUG_FUNCPTR (gst_rtp_rtx_receive_src_event));
  gst_element_add_pad (GST_ELEMENT (rtx), rtx->srcpad);

  rtx->sinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "sink"), "sink");
  GST_PAD_SET_PROXY_CAPS (rtx->sinkpad);
  GST_PAD_SET_PROXY_ALLOCATION (rtx->sinkpad);
  gst_pad_set_chain_function (rtx->sinkpad,
      GST_DEBUG_FUNCPTR (gst_rtp_rtx_receive_chain));
  gst_element_add_pad (GST_ELEMENT (rtx), rtx->sinkpad);

  rtx->ssrc2_ssrc1_map = g_hash_table_new (g_direct_hash, g_direct_equal);
  rtx->seqnum_ssrc1_map = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) ssrc_assoc_free);

  rtx->rtx_pt_map = g_hash_table_new (g_direct_hash, g_direct_equal);
}

static gboolean
gst_rtp_rtx_receive_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstRtpRtxReceive *rtx = GST_RTP_RTX_RECEIVE (parent);
  gboolean res;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_UPSTREAM:
    {
      const GstStructure *s = gst_event_get_structure (event);

      /* This event usually comes from the downstream gstrtpjitterbuffer */
      if (gst_structure_has_name (s, "GstRTPRetransmissionRequest")) {
        guint seqnum = 0;
        guint ssrc = 0;
        gpointer ssrc2 = 0;

        /* retrieve seqnum of the packet that need to be retransmitted */
        if (!gst_structure_get_uint (s, "seqnum", &seqnum))
          seqnum = -1;

        /* retrieve ssrc of the packet that need to be retransmitted
         * it's useful when reconstructing the original packet from the rtx packet */
        if (!gst_structure_get_uint (s, "ssrc", &ssrc))
          ssrc = -1;

        GST_DEBUG_OBJECT (rtx,
            "request seqnum: %" G_GUINT32_FORMAT ", ssrc: %" G_GUINT32_FORMAT,
            seqnum, ssrc);

        GST_OBJECT_LOCK (rtx);

        /* increase number of seen requests for our statistics */
        ++rtx->num_rtx_requests;

        /* First, we lookup in our map to see if we have already associate this
         * master stream ssrc with its retransmitted stream.
         * Every ssrc are unique so we can use the same hash table
         * for both retrieving the ssrc1 from ssrc2 and also ssrc2 from ssrc1
         */
        if (g_hash_table_lookup_extended (rtx->ssrc2_ssrc1_map,
                GUINT_TO_POINTER (ssrc), NULL, &ssrc2)
            && GPOINTER_TO_UINT (ssrc2) != GPOINTER_TO_UINT (ssrc)) {
          GST_DEBUG_OBJECT (rtx, "Retransmited stream %" G_GUINT32_FORMAT
              " already associated to its master", GPOINTER_TO_UINT (ssrc2));
        } else {
          SsrcAssoc *assoc;

          /* not already associated but also we have to check that we have not
           * already considered this request.
           */
          if (g_hash_table_lookup_extended (rtx->seqnum_ssrc1_map,
                  GUINT_TO_POINTER (seqnum), NULL, (gpointer *) & assoc)) {
            if (assoc->ssrc == ssrc) {
              /* do nothing because we have already considered this request
               * The jitter may be too impatient of the rtx packet has been
               * lost too.
               * It does not mean we reject the event, we still want to forward
               * the request to the gstrtpsession to be translater into a FB NACK
               */
              GST_DEBUG_OBJECT (rtx, "Duplicated request seqnum: %"
                  G_GUINT32_FORMAT ", ssrc1: %" G_GUINT32_FORMAT, seqnum, ssrc);
            } else {

              /* If the association attempt is larger than ASSOC_TIMEOUT,
               * then we give up on it, and try this one.
               */
              if (!GST_CLOCK_TIME_IS_VALID (rtx->last_time) ||
                  !GST_CLOCK_TIME_IS_VALID (assoc->time) ||
                  assoc->time + ASSOC_TIMEOUT < rtx->last_time) {
                /* From RFC 4588:
                 * the receiver MUST NOT have two outstanding requests for the
                 * same packet sequence number in two different original streams
                 * before the association is resolved. Otherwise it's impossible
                 * to associate a rtx stream and its master stream
                 */

                /* remove seqnum in order to reuse the spot */
                g_hash_table_remove (rtx->seqnum_ssrc1_map,
                    GUINT_TO_POINTER (seqnum));
                goto retransmit;
              } else {
                GST_DEBUG_OBJECT (rtx,
                    "reject request for seqnum %" G_GUINT32_FORMAT
                    " of master stream %" G_GUINT32_FORMAT, seqnum, ssrc);

                /* do not forward the event as we are rejecting this request */
                GST_OBJECT_UNLOCK (rtx);
                gst_event_unref (event);
                return TRUE;
              }
            }
          } else {
          retransmit:
            /* the request has not been already considered
             * insert it for the first time */
            g_hash_table_insert (rtx->seqnum_ssrc1_map,
                GUINT_TO_POINTER (seqnum),
                ssrc_assoc_new (ssrc, rtx->last_time));
          }
        }

        GST_DEBUG_OBJECT (rtx,
            "packet number %" G_GUINT32_FORMAT " of master stream %"
            G_GUINT32_FORMAT " needs to be retransmitted", seqnum, ssrc);

        GST_OBJECT_UNLOCK (rtx);
      }

      /* Transfer event upstream so that the request can acutally by translated
       * through gstrtpsession through the network */
      res = gst_pad_event_default (pad, parent, event);
      break;
    }
    default:
      res = gst_pad_event_default (pad, parent, event);
      break;
  }
  return res;
}

/* Copy fixed header and extension. Replace current ssrc by ssrc1,
 * remove OSN and replace current seq num by OSN.
 * Copy memory to avoid to manually copy each rtp buffer field.
 */
static GstBuffer *
_gst_rtp_buffer_new_from_rtx (GstRTPBuffer * rtp, guint32 ssrc1,
    guint16 orign_seqnum, guint8 origin_payload_type)
{
  GstMemory *mem = NULL;
  GstRTPBuffer new_rtp = GST_RTP_BUFFER_INIT;
  GstBuffer *new_buffer = gst_buffer_new ();
  GstMapInfo map;
  guint payload_len = 0;

  /* copy fixed header */
  mem = gst_memory_copy (rtp->map[0].memory,
      (guint8 *) rtp->data[0] - rtp->map[0].data, rtp->size[0]);
  gst_buffer_append_memory (new_buffer, mem);

  /* copy extension if any */
  if (rtp->size[1]) {
    mem = gst_memory_copy (rtp->map[1].memory,
        (guint8 *) rtp->data[1] - rtp->map[1].data, rtp->size[1]);
    gst_buffer_append_memory (new_buffer, mem);
  }

  /* copy payload and remove OSN */
  payload_len = rtp->size[2] - 2;
  mem = gst_allocator_alloc (NULL, payload_len, NULL);

  gst_memory_map (mem, &map, GST_MAP_WRITE);
  if (rtp->size[2])
    memcpy (map.data, (guint8 *) rtp->data[2] + 2, payload_len);
  gst_memory_unmap (mem, &map);
  gst_buffer_append_memory (new_buffer, mem);

  /* the sender always constructs rtx packets without padding,
   * But the receiver can still receive rtx packets with padding.
   * So just copy it.
   */
  if (rtp->size[3]) {
    guint pad_len = rtp->size[3];

    mem = gst_allocator_alloc (NULL, pad_len, NULL);

    gst_memory_map (mem, &map, GST_MAP_WRITE);
    map.data[pad_len - 1] = pad_len;
    gst_memory_unmap (mem, &map);

    gst_buffer_append_memory (new_buffer, mem);
  }

  /* set ssrc and seq num */
  gst_rtp_buffer_map (new_buffer, GST_MAP_WRITE, &new_rtp);
  gst_rtp_buffer_set_ssrc (&new_rtp, ssrc1);
  gst_rtp_buffer_set_seq (&new_rtp, orign_seqnum);
  gst_rtp_buffer_set_payload_type (&new_rtp, origin_payload_type);
  gst_rtp_buffer_unmap (&new_rtp);

  gst_buffer_copy_into (new_buffer, rtp->buffer,
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);
  GST_BUFFER_FLAG_SET (new_buffer, GST_RTP_BUFFER_FLAG_RETRANSMISSION);

  return new_buffer;
}

static GstFlowReturn
gst_rtp_rtx_receive_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstRtpRtxReceive *rtx = GST_RTP_RTX_RECEIVE (parent);
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *new_buffer = NULL;
  guint32 ssrc = 0;
  gpointer ssrc1 = 0;
  guint32 ssrc2 = 0;
  guint16 seqnum = 0;
  guint16 orign_seqnum = 0;
  guint8 payload_type = 0;
  guint8 origin_payload_type = 0;
  gboolean is_rtx;
  gboolean drop = FALSE;

  /* map current rtp packet to parse its header */
  gst_rtp_buffer_map (buffer, GST_MAP_READ, &rtp);
  ssrc = gst_rtp_buffer_get_ssrc (&rtp);
  seqnum = gst_rtp_buffer_get_seq (&rtp);
  payload_type = gst_rtp_buffer_get_payload_type (&rtp);

  /* check if we have a retransmission packet (this information comes from SDP) */
  GST_OBJECT_LOCK (rtx);

  rtx->last_time = GST_BUFFER_PTS (buffer);

  is_rtx =
      g_hash_table_lookup_extended (rtx->rtx_pt_map,
      GUINT_TO_POINTER (payload_type), NULL, NULL);

  /* if the current packet is from a retransmission stream */
  if (is_rtx) {
    /* increase our statistic */
    ++rtx->num_rtx_packets;

    /* read OSN in the rtx payload */
    orign_seqnum = GST_READ_UINT16_BE (gst_rtp_buffer_get_payload (&rtp));
    origin_payload_type =
        GPOINTER_TO_UINT (g_hash_table_lookup (rtx->rtx_pt_map,
            GUINT_TO_POINTER (payload_type)));

    /* first we check if we already have associated this retransmission stream
     * to a master stream */
    if (g_hash_table_lookup_extended (rtx->ssrc2_ssrc1_map,
            GUINT_TO_POINTER (ssrc), NULL, &ssrc1)) {
      GST_DEBUG_OBJECT (rtx,
          "packet is from retransmission stream %" G_GUINT32_FORMAT
          " already associated to master stream %" G_GUINT32_FORMAT, ssrc,
          GPOINTER_TO_UINT (ssrc1));
      ssrc2 = ssrc;
    } else {
      SsrcAssoc *assoc;

      /* the current retransmitted packet has its rtx stream not already
       * associated to a master stream, so retrieve it from our request
       * history */
      if (g_hash_table_lookup_extended (rtx->seqnum_ssrc1_map,
              GUINT_TO_POINTER (orign_seqnum), NULL, (gpointer *) & assoc)) {
        GST_DEBUG_OBJECT (rtx,
            "associate retransmitted stream %" G_GUINT32_FORMAT
            " to master stream %" G_GUINT32_FORMAT " thanks to packet %"
            G_GUINT16_FORMAT "", ssrc, assoc->ssrc, orign_seqnum);
        ssrc1 = GUINT_TO_POINTER (assoc->ssrc);
        ssrc2 = ssrc;

        /* just put a guard */
        if (GPOINTER_TO_UINT (ssrc1) == ssrc2)
          GST_WARNING_OBJECT (rtx, "RTX receiver ssrc2_ssrc1_map bad state, "
              "ssrc %" G_GUINT32_FORMAT " are the same\n", ssrc);

        /* free the spot so that this seqnum can be used to do another
         * association */
        g_hash_table_remove (rtx->seqnum_ssrc1_map,
            GUINT_TO_POINTER (orign_seqnum));

        /* actually do the association between rtx stream and master stream */
        g_hash_table_insert (rtx->ssrc2_ssrc1_map, GUINT_TO_POINTER (ssrc2),
            ssrc1);

        /* also do the association between master stream and rtx stream
         * every ssrc are unique so we can use the same hash table
         * for both retrieving the ssrc1 from ssrc2 and also ssrc2 from ssrc1
         */
        g_hash_table_insert (rtx->ssrc2_ssrc1_map, ssrc1,
            GUINT_TO_POINTER (ssrc2));

      } else {
        /* we are not able to associate this rtx packet with a master stream */
        GST_DEBUG_OBJECT (rtx,
            "drop rtx packet because its orign_seqnum %" G_GUINT16_FORMAT
            " is not in pending retransmission requests", orign_seqnum);
        drop = TRUE;
      }
    }
  }

  /* if not dropped the packet was successfully associated */
  if (is_rtx && !drop)
    ++rtx->num_rtx_assoc_packets;

  GST_OBJECT_UNLOCK (rtx);

  /* just drop the packet if the association could not have been made */
  if (drop) {
    gst_rtp_buffer_unmap (&rtp);
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }

  /* create the retransmission packet */
  if (is_rtx)
    new_buffer =
        _gst_rtp_buffer_new_from_rtx (&rtp, GPOINTER_TO_UINT (ssrc1),
        orign_seqnum, origin_payload_type);

  gst_rtp_buffer_unmap (&rtp);

  /* push the packet */
  if (is_rtx) {
    gst_buffer_unref (buffer);
    GST_LOG_OBJECT (rtx, "push packet seqnum:%" G_GUINT16_FORMAT
        " from a restransmission stream ssrc2:%" G_GUINT32_FORMAT " (src %"
        G_GUINT32_FORMAT ")", orign_seqnum, ssrc2, GPOINTER_TO_UINT (ssrc1));
    ret = gst_pad_push (rtx->srcpad, new_buffer);
  } else {
    GST_LOG_OBJECT (rtx, "push packet seqnum:%" G_GUINT16_FORMAT
        " from a master stream ssrc: %" G_GUINT32_FORMAT, seqnum, ssrc);
    ret = gst_pad_push (rtx->srcpad, buffer);
  }

  return ret;
}

static void
gst_rtp_rtx_receive_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstRtpRtxReceive *rtx = GST_RTP_RTX_RECEIVE (object);

  switch (prop_id) {
    case PROP_PAYLOAD_TYPE_MAP:
      GST_OBJECT_LOCK (rtx);
      g_value_set_boxed (value, rtx->rtx_pt_map_structure);
      GST_OBJECT_UNLOCK (rtx);
      break;
    case PROP_NUM_RTX_REQUESTS:
      GST_OBJECT_LOCK (rtx);
      g_value_set_uint (value, rtx->num_rtx_requests);
      GST_OBJECT_UNLOCK (rtx);
      break;
    case PROP_NUM_RTX_PACKETS:
      GST_OBJECT_LOCK (rtx);
      g_value_set_uint (value, rtx->num_rtx_packets);
      GST_OBJECT_UNLOCK (rtx);
      break;
    case PROP_NUM_RTX_ASSOC_PACKETS:
      GST_OBJECT_LOCK (rtx);
      g_value_set_uint (value, rtx->num_rtx_assoc_packets);
      GST_OBJECT_UNLOCK (rtx);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
structure_to_hash_table_inv (GQuark field_id, const GValue * value,
    gpointer hash)
{
  const gchar *field_str;
  guint field_uint;
  guint value_uint;

  field_str = g_quark_to_string (field_id);
  field_uint = atoi (field_str);
  value_uint = g_value_get_uint (value);
  g_hash_table_insert ((GHashTable *) hash, GUINT_TO_POINTER (value_uint),
      GUINT_TO_POINTER (field_uint));

  return TRUE;
}

static void
gst_rtp_rtx_receive_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstRtpRtxReceive *rtx = GST_RTP_RTX_RECEIVE (object);

  switch (prop_id) {
    case PROP_PAYLOAD_TYPE_MAP:
      GST_OBJECT_LOCK (rtx);
      if (rtx->rtx_pt_map_structure)
        gst_structure_free (rtx->rtx_pt_map_structure);
      rtx->rtx_pt_map_structure = g_value_dup_boxed (value);
      g_hash_table_remove_all (rtx->rtx_pt_map);
      gst_structure_foreach (rtx->rtx_pt_map_structure,
          structure_to_hash_table_inv, rtx->rtx_pt_map);
      GST_OBJECT_UNLOCK (rtx);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_rtp_rtx_receive_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstRtpRtxReceive *rtx;

  rtx = GST_RTP_RTX_RECEIVE (element);

  switch (transition) {
    default:
      break;
  }

  ret =
      GST_ELEMENT_CLASS (gst_rtp_rtx_receive_parent_class)->change_state
      (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_rtp_rtx_receive_reset (rtx);
      break;
    default:
      break;
  }

  return ret;
}

gboolean
gst_rtp_rtx_receive_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_rtp_rtx_receive_debug, "rtprtxreceive", 0,
      "rtp retransmission receiver");

  return gst_element_register (plugin, "rtprtxreceive", GST_RANK_NONE,
      GST_TYPE_RTP_RTX_RECEIVE);
}
