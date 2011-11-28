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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/rtp/gstrtpbuffer.h>

#include <string.h>
#include "gstrtpmp4vdepay.h"

GST_DEBUG_CATEGORY_STATIC (rtpmp4vdepay_debug);
#define GST_CAT_DEFAULT (rtpmp4vdepay_debug)

static GstStaticPadTemplate gst_rtp_mp4v_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg,"
        "mpegversion=(int) 4," "systemstream=(boolean)false")
    );

static GstStaticPadTemplate gst_rtp_mp4v_depay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) [1, MAX ], " "encoding-name = (string) \"MP4V-ES\""
        /* All optional parameters
         *
         * "profile-level-id=[1,MAX]"
         * "config=" 
         */
    )
    );

GST_BOILERPLATE (GstRtpMP4VDepay, gst_rtp_mp4v_depay, GstBaseRTPDepayload,
    GST_TYPE_BASE_RTP_DEPAYLOAD);

static void gst_rtp_mp4v_depay_finalize (GObject * object);

static gboolean gst_rtp_mp4v_depay_setcaps (GstBaseRTPDepayload * depayload,
    GstCaps * caps);
static GstBuffer *gst_rtp_mp4v_depay_process (GstBaseRTPDepayload * depayload,
    GstBuffer * buf);

static GstStateChangeReturn gst_rtp_mp4v_depay_change_state (GstElement *
    element, GstStateChange transition);


static void
gst_rtp_mp4v_depay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_mp4v_depay_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_mp4v_depay_sink_template);

  gst_element_class_set_details_simple (element_class,
      "RTP MPEG4 video depayloader", "Codec/Depayloader/Network/RTP",
      "Extracts MPEG4 video from RTP packets (RFC 3016)",
      "Wim Taymans <wim.taymans@gmail.com>");
}

static void
gst_rtp_mp4v_depay_class_init (GstRtpMP4VDepayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPDepayloadClass *gstbasertpdepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertpdepayload_class = (GstBaseRTPDepayloadClass *) klass;

  gobject_class->finalize = gst_rtp_mp4v_depay_finalize;

  gstelement_class->change_state = gst_rtp_mp4v_depay_change_state;

  gstbasertpdepayload_class->process = gst_rtp_mp4v_depay_process;
  gstbasertpdepayload_class->set_caps = gst_rtp_mp4v_depay_setcaps;

  GST_DEBUG_CATEGORY_INIT (rtpmp4vdepay_debug, "rtpmp4vdepay", 0,
      "MPEG4 video RTP Depayloader");
}

static void
gst_rtp_mp4v_depay_init (GstRtpMP4VDepay * rtpmp4vdepay,
    GstRtpMP4VDepayClass * klass)
{
  rtpmp4vdepay->adapter = gst_adapter_new ();
}

static void
gst_rtp_mp4v_depay_finalize (GObject * object)
{
  GstRtpMP4VDepay *rtpmp4vdepay;

  rtpmp4vdepay = GST_RTP_MP4V_DEPAY (object);

  g_object_unref (rtpmp4vdepay->adapter);
  rtpmp4vdepay->adapter = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_rtp_mp4v_depay_setcaps (GstBaseRTPDepayload * depayload, GstCaps * caps)
{
  GstStructure *structure;
  GstCaps *srccaps;
  const gchar *str;
  gint clock_rate;
  gboolean res;

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "clock-rate", &clock_rate))
    clock_rate = 90000;         /* default */
  depayload->clock_rate = clock_rate;

  srccaps = gst_caps_new_simple ("video/mpeg",
      "mpegversion", G_TYPE_INT, 4,
      "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);

  if ((str = gst_structure_get_string (structure, "config"))) {
    GValue v = { 0 };

    g_value_init (&v, GST_TYPE_BUFFER);
    if (gst_value_deserialize (&v, str)) {
      GstBuffer *buffer;

      buffer = gst_value_get_buffer (&v);
      gst_caps_set_simple (srccaps,
          "codec_data", GST_TYPE_BUFFER, buffer, NULL);
      /* caps takes ref */
      g_value_unset (&v);
    } else {
      g_warning ("cannot convert config to buffer");
    }
  }
  res = gst_pad_set_caps (depayload->srcpad, srccaps);
  gst_caps_unref (srccaps);

  return res;
}

static GstBuffer *
gst_rtp_mp4v_depay_process (GstBaseRTPDepayload * depayload, GstBuffer * buf)
{
  GstRtpMP4VDepay *rtpmp4vdepay;
  GstBuffer *outbuf;

  rtpmp4vdepay = GST_RTP_MP4V_DEPAY (depayload);

  /* flush remaining data on discont */
  if (GST_BUFFER_IS_DISCONT (buf))
    gst_adapter_clear (rtpmp4vdepay->adapter);

  outbuf = gst_rtp_buffer_get_payload_buffer (buf);
  gst_adapter_push (rtpmp4vdepay->adapter, outbuf);

  /* if this was the last packet of the VOP, create and push a buffer */
  if (gst_rtp_buffer_get_marker (buf)) {
    guint avail;

    avail = gst_adapter_available (rtpmp4vdepay->adapter);

    outbuf = gst_adapter_take_buffer (rtpmp4vdepay->adapter, avail);

    GST_DEBUG ("gst_rtp_mp4v_depay_chain: pushing buffer of size %d",
        GST_BUFFER_SIZE (outbuf));

    return outbuf;
  }
  return NULL;
}

static GstStateChangeReturn
gst_rtp_mp4v_depay_change_state (GstElement * element,
    GstStateChange transition)
{
  GstRtpMP4VDepay *rtpmp4vdepay;
  GstStateChangeReturn ret;

  rtpmp4vdepay = GST_RTP_MP4V_DEPAY (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_adapter_clear (rtpmp4vdepay->adapter);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    default:
      break;
  }
  return ret;
}

gboolean
gst_rtp_mp4v_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpmp4vdepay",
      GST_RANK_SECONDARY, GST_TYPE_RTP_MP4V_DEPAY);
}
