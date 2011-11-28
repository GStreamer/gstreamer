/* GStreamer
 * Copyright (C) <2010> Wim Taymans <wim.taymans@gmail.com>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>

#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtpac3pay.h"

GST_DEBUG_CATEGORY_STATIC (rtpac3pay_debug);
#define GST_CAT_DEFAULT (rtpac3pay_debug)

static GstStaticPadTemplate gst_rtp_ac3_pay_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/ac3; " "audio/x-ac3; ")
    );

static GstStaticPadTemplate gst_rtp_ac3_pay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) { 32000, 44100, 48000 }, "
        "encoding-name = (string) \"AC3\"")
    );

static void gst_rtp_ac3_pay_finalize (GObject * object);

static GstStateChangeReturn gst_rtp_ac3_pay_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_rtp_ac3_pay_setcaps (GstBaseRTPPayload * payload,
    GstCaps * caps);
static gboolean gst_rtp_ac3_pay_handle_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_rtp_ac3_pay_flush (GstRtpAC3Pay * rtpac3pay);
static GstFlowReturn gst_rtp_ac3_pay_handle_buffer (GstBaseRTPPayload * payload,
    GstBuffer * buffer);

GST_BOILERPLATE (GstRtpAC3Pay, gst_rtp_ac3_pay, GstBaseRTPPayload,
    GST_TYPE_BASE_RTP_PAYLOAD)

     static void gst_rtp_ac3_pay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_ac3_pay_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_ac3_pay_sink_template);

  gst_element_class_set_details_simple (element_class,
      "RTP AC3 audio payloader", "Codec/Payloader/Network/RTP",
      "Payload AC3 audio as RTP packets (RFC 4184)",
      "Wim Taymans <wim.taymans@gmail.com>");
}

static void
gst_rtp_ac3_pay_class_init (GstRtpAC3PayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  gobject_class->finalize = gst_rtp_ac3_pay_finalize;

  gstelement_class->change_state = gst_rtp_ac3_pay_change_state;

  gstbasertppayload_class->set_caps = gst_rtp_ac3_pay_setcaps;
  gstbasertppayload_class->handle_event = gst_rtp_ac3_pay_handle_event;
  gstbasertppayload_class->handle_buffer = gst_rtp_ac3_pay_handle_buffer;

  GST_DEBUG_CATEGORY_INIT (rtpac3pay_debug, "rtpac3pay", 0,
      "AC3 Audio RTP Depayloader");
}

static void
gst_rtp_ac3_pay_init (GstRtpAC3Pay * rtpac3pay, GstRtpAC3PayClass * klass)
{
  rtpac3pay->adapter = gst_adapter_new ();
}

static void
gst_rtp_ac3_pay_finalize (GObject * object)
{
  GstRtpAC3Pay *rtpac3pay;

  rtpac3pay = GST_RTP_AC3_PAY (object);

  g_object_unref (rtpac3pay->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_rtp_ac3_pay_reset (GstRtpAC3Pay * pay)
{
  pay->first_ts = -1;
  pay->duration = 0;
  gst_adapter_clear (pay->adapter);
  GST_DEBUG_OBJECT (pay, "reset depayloader");
}

static gboolean
gst_rtp_ac3_pay_setcaps (GstBaseRTPPayload * payload, GstCaps * caps)
{
  gboolean res;
  gint rate;
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "rate", &rate))
    rate = 90000;               /* default */

  gst_basertppayload_set_options (payload, "audio", TRUE, "AC3", rate);
  res = gst_basertppayload_set_outcaps (payload, NULL);

  return res;
}

static gboolean
gst_rtp_ac3_pay_handle_event (GstPad * pad, GstEvent * event)
{
  GstRtpAC3Pay *rtpac3pay;

  rtpac3pay = GST_RTP_AC3_PAY (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      /* make sure we push the last packets in the adapter on EOS */
      gst_rtp_ac3_pay_flush (rtpac3pay);
      break;
    case GST_EVENT_FLUSH_STOP:
      gst_rtp_ac3_pay_reset (rtpac3pay);
      break;
    default:
      break;
  }

  gst_object_unref (rtpac3pay);

  /* FALSE to let the parent handle the event as well */
  return FALSE;
}

struct frmsize_s
{
  guint16 bit_rate;
  guint16 frm_size[3];
};

static const struct frmsize_s frmsizecod_tbl[] = {
  {32, {64, 69, 96}},
  {32, {64, 70, 96}},
  {40, {80, 87, 120}},
  {40, {80, 88, 120}},
  {48, {96, 104, 144}},
  {48, {96, 105, 144}},
  {56, {112, 121, 168}},
  {56, {112, 122, 168}},
  {64, {128, 139, 192}},
  {64, {128, 140, 192}},
  {80, {160, 174, 240}},
  {80, {160, 175, 240}},
  {96, {192, 208, 288}},
  {96, {192, 209, 288}},
  {112, {224, 243, 336}},
  {112, {224, 244, 336}},
  {128, {256, 278, 384}},
  {128, {256, 279, 384}},
  {160, {320, 348, 480}},
  {160, {320, 349, 480}},
  {192, {384, 417, 576}},
  {192, {384, 418, 576}},
  {224, {448, 487, 672}},
  {224, {448, 488, 672}},
  {256, {512, 557, 768}},
  {256, {512, 558, 768}},
  {320, {640, 696, 960}},
  {320, {640, 697, 960}},
  {384, {768, 835, 1152}},
  {384, {768, 836, 1152}},
  {448, {896, 975, 1344}},
  {448, {896, 976, 1344}},
  {512, {1024, 1114, 1536}},
  {512, {1024, 1115, 1536}},
  {576, {1152, 1253, 1728}},
  {576, {1152, 1254, 1728}},
  {640, {1280, 1393, 1920}},
  {640, {1280, 1394, 1920}}
};

static GstFlowReturn
gst_rtp_ac3_pay_flush (GstRtpAC3Pay * rtpac3pay)
{
  guint avail, FT, NF, mtu;
  GstBuffer *outbuf;
  GstFlowReturn ret;

  /* the data available in the adapter is either smaller
   * than the MTU or bigger. In the case it is smaller, the complete
   * adapter contents can be put in one packet. In the case the
   * adapter has more than one MTU, we need to split the AC3 data
   * over multiple packets. */
  avail = gst_adapter_available (rtpac3pay->adapter);

  ret = GST_FLOW_OK;

  FT = 0;
  /* number of frames */
  NF = rtpac3pay->NF;

  mtu = GST_BASE_RTP_PAYLOAD_MTU (rtpac3pay);

  GST_LOG_OBJECT (rtpac3pay, "flushing %u bytes", avail);

  while (avail > 0) {
    guint towrite;
    guint8 *payload;
    guint payload_len;
    guint packet_len;

    /* this will be the total length of the packet */
    packet_len = gst_rtp_buffer_calc_packet_len (2 + avail, 0, 0);

    /* fill one MTU or all available bytes */
    towrite = MIN (packet_len, mtu);

    /* this is the payload length */
    payload_len = gst_rtp_buffer_calc_payload_len (towrite, 0, 0);

    /* create buffer to hold the payload */
    outbuf = gst_rtp_buffer_new_allocate (payload_len, 0, 0);

    if (FT == 0) {
      /* check if it all fits */
      if (towrite < packet_len) {
        guint maxlen;

        GST_LOG_OBJECT (rtpac3pay, "we need to fragment");
        /* check if we will be able to put at least 5/8th of the total
         * frame in this first frame. */
        if ((avail * 5) / 8 >= (payload_len - 2))
          FT = 1;
        else
          FT = 2;
        /* check how many fragments we will need */
        maxlen = gst_rtp_buffer_calc_payload_len (mtu - 2, 0, 0);
        NF = (avail + maxlen - 1) / maxlen;
      }
    } else if (FT != 3) {
      /* remaining fragment */
      FT = 3;
    }

    /*
     *  0                   1
     *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * |    MBZ    | FT|       NF      |
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     *
     * FT: 0: one or more complete frames
     *     1: initial 5/8 fragment
     *     2: initial fragment not 5/8
     *     3: other fragment
     * NF: amount of frames if FT = 0, else number of fragments.
     */
    GST_LOG_OBJECT (rtpac3pay, "FT %u, NF %u", FT, NF);
    payload = gst_rtp_buffer_get_payload (outbuf);
    payload[0] = (FT & 3);
    payload[1] = NF;
    payload_len -= 2;

    gst_adapter_copy (rtpac3pay->adapter, &payload[2], 0, payload_len);
    gst_adapter_flush (rtpac3pay->adapter, payload_len);

    avail -= payload_len;
    if (avail == 0)
      gst_rtp_buffer_set_marker (outbuf, TRUE);

    GST_BUFFER_TIMESTAMP (outbuf) = rtpac3pay->first_ts;
    GST_BUFFER_DURATION (outbuf) = rtpac3pay->duration;

    ret = gst_basertppayload_push (GST_BASE_RTP_PAYLOAD (rtpac3pay), outbuf);
  }

  return ret;
}

static GstFlowReturn
gst_rtp_ac3_pay_handle_buffer (GstBaseRTPPayload * basepayload,
    GstBuffer * buffer)
{
  GstRtpAC3Pay *rtpac3pay;
  GstFlowReturn ret;
  guint size, avail, left, NF;
  guint8 *data, *p;
  guint packet_len;
  GstClockTime duration, timestamp;

  rtpac3pay = GST_RTP_AC3_PAY (basepayload);

  size = GST_BUFFER_SIZE (buffer);
  data = GST_BUFFER_DATA (buffer);
  duration = GST_BUFFER_DURATION (buffer);
  timestamp = GST_BUFFER_TIMESTAMP (buffer);

  if (GST_BUFFER_IS_DISCONT (buffer)) {
    GST_DEBUG_OBJECT (rtpac3pay, "DISCONT");
    gst_rtp_ac3_pay_reset (rtpac3pay);
  }

  /* count the amount of incomming packets */
  NF = 0;
  left = size;
  p = data;
  while (TRUE) {
    guint bsid, fscod, frmsizecod, frame_size;

    if (left < 6)
      break;

    if (p[0] != 0x0b || p[1] != 0x77)
      break;

    bsid = p[5] >> 3;
    if (bsid > 8)
      break;

    frmsizecod = p[4] & 0x3f;
    fscod = p[4] >> 6;

    GST_DEBUG_OBJECT (rtpac3pay, "fscod %u, %u", fscod, frmsizecod);

    if (fscod >= 3 || frmsizecod >= 38)
      break;

    frame_size = frmsizecod_tbl[frmsizecod].frm_size[fscod] * 2;
    if (frame_size > left)
      break;

    NF++;
    GST_DEBUG_OBJECT (rtpac3pay, "found frame %u of size %u", NF, frame_size);

    p += frame_size;
    left -= frame_size;
  }
  if (NF == 0)
    goto no_frames;

  avail = gst_adapter_available (rtpac3pay->adapter);

  /* get packet length of previous data and this new data,
   * payload length includes a 4 byte header */
  packet_len = gst_rtp_buffer_calc_packet_len (2 + avail + size, 0, 0);

  /* if this buffer is going to overflow the packet, flush what we
   * have. */
  if (gst_basertppayload_is_filled (basepayload,
          packet_len, rtpac3pay->duration + duration)) {
    ret = gst_rtp_ac3_pay_flush (rtpac3pay);
    avail = 0;
  } else {
    ret = GST_FLOW_OK;
  }

  if (avail == 0) {
    GST_DEBUG_OBJECT (rtpac3pay,
        "first packet, save timestamp %" GST_TIME_FORMAT,
        GST_TIME_ARGS (timestamp));
    rtpac3pay->first_ts = timestamp;
    rtpac3pay->duration = 0;
    rtpac3pay->NF = 0;
  }

  gst_adapter_push (rtpac3pay->adapter, buffer);
  rtpac3pay->duration += duration;
  rtpac3pay->NF += NF;

  return ret;

  /* ERRORS */
no_frames:
  {
    GST_WARNING_OBJECT (rtpac3pay, "no valid AC3 frames found");
    return GST_FLOW_OK;
  }
}

static GstStateChangeReturn
gst_rtp_ac3_pay_change_state (GstElement * element, GstStateChange transition)
{
  GstRtpAC3Pay *rtpac3pay;
  GstStateChangeReturn ret;

  rtpac3pay = GST_RTP_AC3_PAY (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_rtp_ac3_pay_reset (rtpac3pay);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_rtp_ac3_pay_reset (rtpac3pay);
      break;
    default:
      break;
  }
  return ret;
}

gboolean
gst_rtp_ac3_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpac3pay",
      GST_RANK_SECONDARY, GST_TYPE_RTP_AC3_PAY);
}
