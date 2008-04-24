/* GStreamer
 * Copyright (C) <2006> Philippe Khalaf <burger@speedy.org>
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
#include "config.h"
#endif

#include "gstrtpilbcpay.h"
#include <gst/rtp/gstrtpbuffer.h>

/* elementfactory information */
static GstElementDetails gst_rtpilbcpay_details = {
  "RTP Payloader for iLBC Audio",
  "Codec/Payloader/Network",
  "Packetize iLBC audio streams into RTP packets",
  "Philippe Kalaf <philippe.kalaf@collabora.co.uk>"
};

GST_DEBUG_CATEGORY_STATIC (rtpilbcpay_debug);
#define GST_CAT_DEFAULT (rtpilbcpay_debug)

static GstStaticPadTemplate gst_rtpilbcpay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-iLBC, " "mode = (int) {20, 30}")
    );

static GstStaticPadTemplate gst_rtpilbcpay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 8000, "
        "encoding-name = (string) \"ILBC\", "
        "mode = (string) { \"20\", \"30\" }")
    );

static gboolean gst_rtpilbcpay_setcaps (GstBaseRTPPayload * payload,
    GstCaps * caps);

GST_BOILERPLATE (GstRTPILBCPay, gst_rtpilbcpay, GstBaseRTPAudioPayload,
    GST_TYPE_BASE_RTP_AUDIO_PAYLOAD);

static void
gst_rtpilbcpay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpilbcpay_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpilbcpay_src_template));
  gst_element_class_set_details (element_class, &gst_rtpilbcpay_details);
}

static void
gst_rtpilbcpay_class_init (GstRTPILBCPayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_BASE_RTP_PAYLOAD);

  gstbasertppayload_class->set_caps = gst_rtpilbcpay_setcaps;

  GST_DEBUG_CATEGORY_INIT (rtpilbcpay_debug, "rtpilbcpay", 0,
      "iLBC audio RTP payloader");
}

static void
gst_rtpilbcpay_init (GstRTPILBCPay * rtpilbcpay, GstRTPILBCPayClass * klass)
{
  GstBaseRTPPayload *basertppayload;
  GstBaseRTPAudioPayload *basertpaudiopayload;

  basertppayload = GST_BASE_RTP_PAYLOAD (rtpilbcpay);
  basertpaudiopayload = GST_BASE_RTP_AUDIO_PAYLOAD (rtpilbcpay);

  /* we don't set the payload type, it should be set by the application using
   * the pt property or the default 96 will be used */
  basertppayload->clock_rate = 8000;

  rtpilbcpay->mode = -1;

  /* tell basertpaudiopayload that this is a frame based codec */
  gst_base_rtp_audio_payload_set_frame_based (basertpaudiopayload);
}

static gboolean
gst_rtpilbcpay_setcaps (GstBaseRTPPayload * basertppayload, GstCaps * caps)
{
  GstRTPILBCPay *rtpilbcpay;
  GstBaseRTPAudioPayload *basertpaudiopayload;
  gboolean ret;
  gint mode;
  gchar *mode_str;
  GstStructure *structure;
  const char *payload_name;

  rtpilbcpay = GST_RTP_ILBC_PAY (basertppayload);
  basertpaudiopayload = GST_BASE_RTP_AUDIO_PAYLOAD (basertppayload);

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "mode", &mode);
  if (mode != 20 && mode != 30)
    goto wrong_mode;

  payload_name = gst_structure_get_name (structure);
  if (g_ascii_strcasecmp ("audio/x-iLBC", payload_name))
    goto wrong_caps;

  gst_basertppayload_set_options (basertppayload, "audio", TRUE, "ILBC", 8000);
  /* set options for this frame based audio codec */
  gst_base_rtp_audio_payload_set_frame_options (basertpaudiopayload,
      mode, mode == 30 ? 50 : 38);


  mode_str = g_strdup_printf ("%d", mode);
  ret =
      gst_basertppayload_set_outcaps (basertppayload, "mode", G_TYPE_STRING,
      mode_str, NULL);
  g_free (mode_str);

  if (mode != rtpilbcpay->mode && rtpilbcpay->mode != -1)
    goto mode_changed;

  rtpilbcpay->mode = mode;

  return ret;

  /* ERRORS */
wrong_mode:
  {
    GST_ERROR_OBJECT (rtpilbcpay, "mode must be 20 or 30, received %d", mode);
    return FALSE;
  }
wrong_caps:
  {
    GST_ERROR_OBJECT (rtpilbcpay, "expected audio/x-iLBC, received %s",
        payload_name);
    return FALSE;
  }
mode_changed:
  {
    GST_ERROR_OBJECT (rtpilbcpay, "Mode has changed from %d to %d! "
        "Mode cannot change while streaming", rtpilbcpay->mode, mode);
    return FALSE;
  }
}

gboolean
gst_rtp_ilbc_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpilbcpay",
      GST_RANK_NONE, GST_TYPE_RTP_ILBC_PAY);
}
