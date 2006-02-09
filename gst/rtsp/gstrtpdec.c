/* GStreamer
 * Copyright (C) <2005> Wim Taymans <wim@fluendo.com>
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

#include "gstrtpdec.h"

GST_DEBUG_CATEGORY (rtpdec_debug);
#define GST_CAT_DEFAULT (rtpdec_debug)

/* elementfactory information */
static GstElementDetails rtpdec_details = GST_ELEMENT_DETAILS ("RTP Decoder",
    "Codec/Parser/Network",
    "Accepts raw RTP and RTCP packets and sends them forward",
    "Wim Taymans <wim@fluendo.com>");

/* GstRTPDec signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_SKIP
      /* FILL ME */
};

static GstStaticPadTemplate gst_rtpdec_src_rtp_template =
GST_STATIC_PAD_TEMPLATE ("srcrtp",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static GstStaticPadTemplate gst_rtpdec_src_rtcp_template =
GST_STATIC_PAD_TEMPLATE ("srcrtcp",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp")
    );

static GstStaticPadTemplate gst_rtpdec_sink_rtp_template =
GST_STATIC_PAD_TEMPLATE ("sinkrtp",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static GstStaticPadTemplate gst_rtpdec_sink_rtcp_template =
GST_STATIC_PAD_TEMPLATE ("sinkrtcp",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp")
    );

static void gst_rtpdec_class_init (gpointer g_class);
static void gst_rtpdec_init (GstRTPDec * rtpdec);

static GstCaps *gst_rtpdec_getcaps (GstPad * pad);
static GstFlowReturn gst_rtpdec_chain_rtp (GstPad * pad, GstBuffer * buffer);
static GstFlowReturn gst_rtpdec_chain_rtcp (GstPad * pad, GstBuffer * buffer);

static void gst_rtpdec_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_rtpdec_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_rtpdec_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;

/*static guint gst_rtpdec_signals[LAST_SIGNAL] = { 0 };*/

GType
gst_rtpdec_get_type (void)
{
  static GType rtpdec_type = 0;

  if (!rtpdec_type) {
    static const GTypeInfo rtpdec_info = {
      sizeof (GstRTPDecClass), NULL,
      NULL,
      (GClassInitFunc) gst_rtpdec_class_init,
      NULL,
      NULL,
      sizeof (GstRTPDec),
      0,
      (GInstanceInitFunc) gst_rtpdec_init,
    };

    rtpdec_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstRTPDec", &rtpdec_info, 0);
  }
  return rtpdec_type;
}

static void
gst_rtpdec_class_init (gpointer g_class)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstRTPDecClass *klass;

  klass = (GstRTPDecClass *) g_class;
  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtpdec_src_rtp_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtpdec_src_rtcp_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtpdec_sink_rtp_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtpdec_sink_rtcp_template));
  gst_element_class_set_details (gstelement_class, &rtpdec_details);

  gobject_class->set_property = gst_rtpdec_set_property;
  gobject_class->get_property = gst_rtpdec_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SKIP, g_param_spec_int ("skip", "skip", "skip", G_MININT, G_MAXINT, 0, G_PARAM_READWRITE));      /* CHECKME */

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gstelement_class->change_state = gst_rtpdec_change_state;

  GST_DEBUG_CATEGORY_INIT (rtpdec_debug, "rtpdec", 0, "RTP decoder");
}

static void
gst_rtpdec_init (GstRTPDec * rtpdec)
{
  /* the input rtp pad */
  rtpdec->sink_rtp =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_rtpdec_sink_rtp_template), "sinkrtp");
  gst_element_add_pad (GST_ELEMENT (rtpdec), rtpdec->sink_rtp);
  gst_pad_set_getcaps_function (rtpdec->sink_rtp, gst_rtpdec_getcaps);
  gst_pad_set_chain_function (rtpdec->sink_rtp, gst_rtpdec_chain_rtp);

  /* the input rtcp pad */
  rtpdec->sink_rtcp =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_rtpdec_sink_rtcp_template), "sinkrtcp");
  gst_element_add_pad (GST_ELEMENT (rtpdec), rtpdec->sink_rtcp);
  gst_pad_set_chain_function (rtpdec->sink_rtcp, gst_rtpdec_chain_rtcp);

  /* the output rtp pad */
  rtpdec->src_rtp =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_rtpdec_src_rtp_template), "srcrtp");
  gst_pad_set_getcaps_function (rtpdec->src_rtp, gst_rtpdec_getcaps);
  gst_element_add_pad (GST_ELEMENT (rtpdec), rtpdec->src_rtp);

  /* the output rtcp pad */
  rtpdec->src_rtcp =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_rtpdec_src_rtcp_template), "srcrtcp");
  gst_element_add_pad (GST_ELEMENT (rtpdec), rtpdec->src_rtcp);
}

static GstCaps *
gst_rtpdec_getcaps (GstPad * pad)
{
  GstRTPDec *src;
  GstPad *other;
  GstCaps *caps;

  src = GST_RTPDEC (GST_PAD_PARENT (pad));

  other = pad == src->src_rtp ? src->sink_rtp : src->src_rtp;

  caps = gst_pad_peer_get_caps (other);

  if (caps == NULL)
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));

  return caps;
}

static GstFlowReturn
gst_rtpdec_chain_rtp (GstPad * pad, GstBuffer * buffer)
{
  GstRTPDec *src;

  src = GST_RTPDEC (GST_PAD_PARENT (pad));

  GST_DEBUG ("got rtp packet");
  return gst_pad_push (src->src_rtp, buffer);
}

static GstFlowReturn
gst_rtpdec_chain_rtcp (GstPad * pad, GstBuffer * buffer)
{
  GST_DEBUG ("got rtcp packet");

  gst_buffer_unref (buffer);
  return GST_FLOW_OK;
}

static void
gst_rtpdec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRTPDec *src;

  src = GST_RTPDEC (object);

  switch (prop_id) {
    case ARG_SKIP:
      break;
    default:
      break;
  }
}

static void
gst_rtpdec_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRTPDec *src;

  src = GST_RTPDEC (object);

  switch (prop_id) {
    case ARG_SKIP:
      break;
    default:
      break;
  }
}

static GstStateChangeReturn
gst_rtpdec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstRTPDec *rtpdec;

  rtpdec = GST_RTPDEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    default:
      break;
  }

  return ret;
}
