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

/* elementfactory information */
static const GstElementDetails rtpdepay_details =
GST_ELEMENT_DETAILS ("Dummy RTP session manager",
    "Codec/Depayloader/Network",
    "Accepts raw RTP and RTCP packets and sends them forward",
    "Wim Taymans <wim.taymans@gmail.com>");

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

static void gst_rtp_depay_class_init (GstRTPDepayClass * klass);
static void gst_rtp_depay_init (GstRTPDepay * rtpdepay);

static GstCaps *gst_rtp_depay_getcaps (GstPad * pad);
static GstFlowReturn gst_rtp_depay_chain_rtp (GstPad * pad, GstBuffer * buffer);
static GstFlowReturn gst_rtp_depay_chain_rtcp (GstPad * pad,
    GstBuffer * buffer);

static GstStateChangeReturn gst_rtp_depay_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;

/*static guint gst_rtp_depay_signals[LAST_SIGNAL] = { 0 };*/

GType
gst_rtp_depay_get_type (void)
{
  static GType rtpdepay_type = 0;

  if (!rtpdepay_type) {
    static const GTypeInfo rtpdepay_info = {
      sizeof (GstRTPDepayClass), NULL,
      NULL,
      (GClassInitFunc) gst_rtp_depay_class_init,
      NULL,
      NULL,
      sizeof (GstRTPDepay),
      0,
      (GInstanceInitFunc) gst_rtp_depay_init,
    };

    rtpdepay_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstRTPDepay", &rtpdepay_info,
        0);
  }
  return rtpdepay_type;
}

static void
gst_rtp_depay_class_init (GstRTPDepayClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtp_depay_src_rtp_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtp_depay_src_rtcp_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtp_depay_sink_rtp_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtp_depay_sink_rtcp_template));
  gst_element_class_set_details (gstelement_class, &rtpdepay_details);

  parent_class = g_type_class_peek_parent (klass);

  gstelement_class->change_state = gst_rtp_depay_change_state;

  GST_DEBUG_CATEGORY_INIT (rtpdepay_debug, "rtpdepay", 0, "RTP decoder");
}

static void
gst_rtp_depay_init (GstRTPDepay * rtpdepay)
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

static GstStateChangeReturn
gst_rtp_depay_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstRTPDepay *rtpdepay;

  rtpdepay = GST_RTP_DEPAY (element);

  /*
     switch (transition) {
     case GST_STATE_CHANGE_PAUSED_TO_READY:
     break;
     default:
     break;
     }
   */

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  /*
     switch (transition) {
     case GST_STATE_CHANGE_PAUSED_TO_READY:
     break;
     default:
     break;
     }
   */

  return ret;
}

gboolean
gst_rtp_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpdepay",
      GST_RANK_NONE, GST_TYPE_RTP_DEPAY);
}
