/* GStreamer
 * Copyright (C) <2007> Wim Taymans <wim@fluendo.com>
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

#include "gstrtpL16pay.h"

GST_DEBUG_CATEGORY_STATIC (rtpL16pay_debug);
#define GST_CAT_DEFAULT (rtpL16pay_debug)

/* elementfactory information */
static const GstElementDetails gst_rtp_L16_pay_details =
GST_ELEMENT_DETAILS ("RTP packet payloader",
    "Codec/Payloader/Network",
    "Payload-encode Raw audio into RTP packets (RFC 3551)",
    "Wim Taymans <wim@fluendo.com>");

static GstStaticPadTemplate gst_rtp_L16_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BIG_ENDIAN, "
        "signed = (boolean) true, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, MAX ]")
    );

static GstStaticPadTemplate gst_rtp_L16_pay_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) [ 96, 127 ], "
        "clock-rate = (int) [ 1, MAX ], "
        "encoding-name = (string) \"L16\", "
        "channels = (int) [ 1, MAX ], "
        "rate = (int) [ 1, MAX ];"
        "application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) { " GST_RTP_PAYLOAD_L16_STEREO_STRING ", "
        GST_RTP_PAYLOAD_L16_MONO_STRING " }," "clock-rate = (int) 44100")
    );

static void gst_rtp_L16_pay_class_init (GstRtpL16PayClass * klass);
static void gst_rtp_L16_pay_base_init (GstRtpL16PayClass * klass);
static void gst_rtp_L16_pay_init (GstRtpL16Pay * rtpL16pay);
static void gst_rtp_L16_pay_finalize (GObject * object);

static gboolean gst_rtp_L16_pay_setcaps (GstBaseRTPPayload * basepayload,
    GstCaps * caps);
static GstFlowReturn gst_rtp_L16_pay_handle_buffer (GstBaseRTPPayload * pad,
    GstBuffer * buffer);

static GstBaseRTPPayloadClass *parent_class = NULL;

static GType
gst_rtp_L16_pay_get_type (void)
{
  static GType rtpL16pay_type = 0;

  if (!rtpL16pay_type) {
    static const GTypeInfo rtpL16pay_info = {
      sizeof (GstRtpL16PayClass),
      (GBaseInitFunc) gst_rtp_L16_pay_base_init,
      NULL,
      (GClassInitFunc) gst_rtp_L16_pay_class_init,
      NULL,
      NULL,
      sizeof (GstRtpL16Pay),
      0,
      (GInstanceInitFunc) gst_rtp_L16_pay_init,
    };

    rtpL16pay_type =
        g_type_register_static (GST_TYPE_BASE_RTP_PAYLOAD, "GstRtpL16Pay",
        &rtpL16pay_info, 0);
  }
  return rtpL16pay_type;
}

static void
gst_rtp_L16_pay_base_init (GstRtpL16PayClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_L16_pay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_L16_pay_sink_template));

  gst_element_class_set_details (element_class, &gst_rtp_L16_pay_details);
}

static void
gst_rtp_L16_pay_class_init (GstRtpL16PayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_rtp_L16_pay_finalize;

  gstbasertppayload_class->set_caps = gst_rtp_L16_pay_setcaps;
  gstbasertppayload_class->handle_buffer = gst_rtp_L16_pay_handle_buffer;

  GST_DEBUG_CATEGORY_INIT (rtpL16pay_debug, "rtpL16pay", 0,
      "L16 RTP Payloader");
}

static void
gst_rtp_L16_pay_init (GstRtpL16Pay * rtpL16pay)
{
  rtpL16pay->adapter = gst_adapter_new ();
}

static void
gst_rtp_L16_pay_finalize (GObject * object)
{
  GstRtpL16Pay *rtpL16pay;

  rtpL16pay = GST_RTP_L16_PAY (object);

  g_object_unref (rtpL16pay->adapter);
  rtpL16pay->adapter = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_rtp_L16_pay_setcaps (GstBaseRTPPayload * basepayload, GstCaps * caps)
{
  GstRtpL16Pay *rtpL16pay;
  GstStructure *structure;
  gint channels, rate;
  gboolean res;

  rtpL16pay = GST_RTP_L16_PAY (basepayload);

  structure = gst_caps_get_structure (caps, 0);

  /* first parse input caps */
  if (!gst_structure_get_int (structure, "rate", &rate))
    goto no_rate;

  if (!gst_structure_get_int (structure, "channels", &channels))
    goto no_channels;

  gst_basertppayload_set_options (basepayload, "audio", TRUE, "L16", rate);
  res = gst_basertppayload_set_outcaps (basepayload,
      "channels", G_TYPE_INT, channels, "rate", G_TYPE_INT, rate, NULL);

  rtpL16pay->rate = rate;
  rtpL16pay->channels = channels;

  return res;

  /* ERRORS */
no_rate:
  {
    GST_DEBUG_OBJECT (rtpL16pay, "no rate given");
    return FALSE;
  }
no_channels:
  {
    GST_DEBUG_OBJECT (rtpL16pay, "no channels given");
    return FALSE;
  }
}

static GstFlowReturn
gst_rtp_L16_pay_flush (GstRtpL16Pay * rtpL16pay, guint len)
{
  GstBuffer *outbuf;
  guint8 *payload;
  GstFlowReturn ret;
  guint samples;
  GstClockTime duration;

  /* now alloc output buffer */
  outbuf = gst_rtp_buffer_new_allocate (len, 0, 0);

  /* get payload, this is now writable */
  payload = gst_rtp_buffer_get_payload (outbuf);

  /* copy and flush data out of adapter into the RTP payload */
  gst_adapter_copy (rtpL16pay->adapter, payload, 0, len);
  gst_adapter_flush (rtpL16pay->adapter, len);

  samples = len / (2 * rtpL16pay->channels);
  duration = gst_util_uint64_scale_int (samples, GST_SECOND, rtpL16pay->rate);

  GST_BUFFER_TIMESTAMP (outbuf) = rtpL16pay->first_ts;
  GST_BUFFER_DURATION (outbuf) = duration;

  /* increase count (in ts) of data pushed to basertppayload */
  if (GST_CLOCK_TIME_IS_VALID (rtpL16pay->first_ts))
    rtpL16pay->first_ts += duration;

  ret = gst_basertppayload_push (GST_BASE_RTP_PAYLOAD (rtpL16pay), outbuf);

  return ret;
}

static GstFlowReturn
gst_rtp_L16_pay_handle_buffer (GstBaseRTPPayload * basepayload,
    GstBuffer * buffer)
{
  GstRtpL16Pay *rtpL16pay;
  GstFlowReturn ret = GST_FLOW_OK;
  guint payload_len;
  GstClockTime timestamp;
  guint mtu, avail;

  rtpL16pay = GST_RTP_L16_PAY (basepayload);
  mtu = GST_BASE_RTP_PAYLOAD_MTU (rtpL16pay);

  timestamp = GST_BUFFER_TIMESTAMP (buffer);

  if (GST_BUFFER_IS_DISCONT (buffer))
    gst_adapter_clear (rtpL16pay->adapter);

  avail = gst_adapter_available (rtpL16pay->adapter);
  if (avail == 0) {
    rtpL16pay->first_ts = timestamp;
  }

  /* push buffer in adapter */
  gst_adapter_push (rtpL16pay->adapter, buffer);

  /* get payload len for MTU */
  payload_len = gst_rtp_buffer_calc_payload_len (mtu, 0, 0);

  /* flush complete MTU while we have enough data in the adapter */
  while (avail >= payload_len) {
    /* flush payload_len bytes */
    ret = gst_rtp_L16_pay_flush (rtpL16pay, payload_len);
    if (ret != GST_FLOW_OK)
      break;

    avail = gst_adapter_available (rtpL16pay->adapter);
  }
  return ret;
}

gboolean
gst_rtp_L16_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpL16pay",
      GST_RANK_NONE, GST_TYPE_RTP_L16_PAY);
}
