/* GStreamer
 * Copyright (C) <2005> Wim Taymans <wim.taymans@gmail.com>
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
/* Element-Checklist-Version: 5 */

#include "gstrtpdepay.h"

GST_DEBUG_CATEGORY_STATIC (rtpdepay_debug);
#define GST_CAT_DEFAULT (rtpdepay_debug)

static GstStaticPadTemplate gst_rtp_depay_src_rtp_template =
GST_STATIC_PAD_TEMPLATE ("srcrtp",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static GstStaticPadTemplate gst_rtp_depay_src_rtcp_template =
GST_STATIC_PAD_TEMPLATE ("srcrtcp",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp")
    );

static GstStaticPadTemplate gst_rtp_depay_sink_rtp_template =
GST_STATIC_PAD_TEMPLATE ("sinkrtp",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static GstStaticPadTemplate gst_rtp_depay_sink_rtcp_template =
GST_STATIC_PAD_TEMPLATE ("sinkrtcp",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp")
    );

static GstCaps *gst_rtp_depay_getcaps (GstPad * pad);
static GstFlowReturn gst_rtp_depay_chain_rtp (GstPad * pad, GstBuffer * buffer);
static GstFlowReturn gst_rtp_depay_chain_rtcp (GstPad * pad,
    GstBuffer * buffer);

GST_BOILERPLATE (GstRTPDepay, gst_rtp_depay, GstElement, GST_TYPE_ELEMENT);

static void
gst_rtp_depay_base_init (gpointer klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_rtp_depay_src_rtp_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_rtp_depay_src_rtcp_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_rtp_depay_sink_rtp_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_rtp_depay_sink_rtcp_template);
  gst_element_class_set_details_simple (gstelement_class,
      "Dummy RTP session manager", "Codec/Depayloader/Network/RTP",
      "Accepts raw RTP and RTCP packets and sends them forward",
      "Wim Taymans <wim.taymans@gmail.com>");
}

static void
gst_rtp_depay_class_init (GstRTPDepayClass * klass)
{
  GST_DEBUG_CATEGORY_INIT (rtpdepay_debug, "rtpdepay", 0, "RTP decoder");
}

static void
gst_rtp_depay_init (GstRTPDepay * rtpdepay, GstRTPDepayClass * klass)
{
  /* the input rtp pad */
  rtpdepay->sink_rtp =
      gst_pad_new_from_static_template (&gst_rtp_depay_sink_rtp_template,
      "sinkrtp");
  gst_element_add_pad (GST_ELEMENT (rtpdepay), rtpdepay->sink_rtp);
  gst_pad_set_getcaps_function (rtpdepay->sink_rtp, gst_rtp_depay_getcaps);
  gst_pad_set_chain_function (rtpdepay->sink_rtp, gst_rtp_depay_chain_rtp);

  /* the input rtcp pad */
  rtpdepay->sink_rtcp =
      gst_pad_new_from_static_template (&gst_rtp_depay_sink_rtcp_template,
      "sinkrtcp");
  gst_element_add_pad (GST_ELEMENT (rtpdepay), rtpdepay->sink_rtcp);
  gst_pad_set_chain_function (rtpdepay->sink_rtcp, gst_rtp_depay_chain_rtcp);

  /* the output rtp pad */
  rtpdepay->src_rtp =
      gst_pad_new_from_static_template (&gst_rtp_depay_src_rtp_template,
      "srcrtp");
  gst_pad_set_getcaps_function (rtpdepay->src_rtp, gst_rtp_depay_getcaps);
  gst_element_add_pad (GST_ELEMENT (rtpdepay), rtpdepay->src_rtp);

  /* the output rtcp pad */
  rtpdepay->src_rtcp =
      gst_pad_new_from_static_template (&gst_rtp_depay_src_rtcp_template,
      "srcrtcp");
  gst_element_add_pad (GST_ELEMENT (rtpdepay), rtpdepay->src_rtcp);
}

static GstCaps *
gst_rtp_depay_getcaps (GstPad * pad)
{
  GstRTPDepay *src;
  GstPad *other;
  GstCaps *caps;

  src = GST_RTP_DEPAY (GST_PAD_PARENT (pad));

  other = pad == src->src_rtp ? src->sink_rtp : src->src_rtp;

  caps = gst_pad_peer_get_caps (other);

  if (caps == NULL)
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));

  return caps;
}

static GstFlowReturn
gst_rtp_depay_chain_rtp (GstPad * pad, GstBuffer * buffer)
{
  GstRTPDepay *src;

  src = GST_RTP_DEPAY (GST_PAD_PARENT (pad));

  GST_DEBUG ("got rtp packet");
  return gst_pad_push (src->src_rtp, buffer);
}

static GstFlowReturn
gst_rtp_depay_chain_rtcp (GstPad * pad, GstBuffer * buffer)
{
  GST_DEBUG ("got rtcp packet");

  gst_buffer_unref (buffer);
  return GST_FLOW_OK;
}

gboolean
gst_rtp_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpdepay",
      GST_RANK_SECONDARY, GST_TYPE_RTP_DEPAY);
}
