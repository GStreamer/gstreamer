/* GStreamer
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

#include "gstrtpg729pay.h"
#include <gst/rtp/gstrtpbuffer.h>

/* elementfactory information */
static GstElementDetails gst_rtpg729pay_details = {
  "RTP Payloader for G729 Audio",
  "Codec/Payloader/Network",
  "Packetize G729 audio streams into RTP packets",
  "Laurent Glayal <spglegle@yahoo.fr>"
};

GST_DEBUG_CATEGORY_STATIC (rtpg729pay_debug);
#define GST_CAT_DEFAULT (rtpg729pay_debug)

static GstStaticPadTemplate gst_rtpg729pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/G729, channels=(int)1, rate=(int)8000")
    );

static GstStaticPadTemplate gst_rtpg729pay_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 8000, " "encoding-name = (string) \"G729\";"
        "application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_G729_STRING ", "
        "clock-rate = (int) 8000")
    );

static gboolean gst_rtpg729pay_setcaps (GstBaseRTPPayload * payload,
    GstCaps * caps);

GST_BOILERPLATE (GstRtpG729Pay, gst_rtpg729pay, GstBaseRTPAudioPayload,
    GST_TYPE_BASE_RTP_AUDIO_PAYLOAD);

static void
gst_rtpg729pay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpg729pay_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpg729pay_src_template));
  gst_element_class_set_details (element_class, &gst_rtpg729pay_details);
}

static void
gst_rtpg729pay_class_init (GstRtpG729PayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_BASE_RTP_PAYLOAD);

  gstbasertppayload_class->set_caps = gst_rtpg729pay_setcaps;

  GST_DEBUG_CATEGORY_INIT (rtpg729pay_debug, "rtpg729pay", 0,
      "G729 audio RTP payloader");
}

static void
gst_rtpg729pay_init (GstRtpG729Pay * rtpg729pay, GstRtpG729PayClass * klass)
{
  GstBaseRTPPayload *basertppayload;
  GstBaseRTPAudioPayload *basertpaudiopayload;

  basertppayload = GST_BASE_RTP_PAYLOAD (rtpg729pay);
  basertpaudiopayload = GST_BASE_RTP_AUDIO_PAYLOAD (rtpg729pay);

  /* we don't set the payload type, it should be set by the application using
   * the pt property or the default 96 will be used */
  basertppayload->clock_rate = 8000;

  /* tell basertpaudiopayload that this is a frame based codec */
  gst_base_rtp_audio_payload_set_frame_based (basertpaudiopayload);
  gst_basertppayload_set_options (basertppayload, "audio", FALSE, "G729", 8000);
  gst_base_rtp_audio_payload_set_frame_options (basertpaudiopayload, 10, 10);
}

static gboolean
gst_rtpg729pay_setcaps (GstBaseRTPPayload * basertppayload, GstCaps * caps)
{
  GstRtpG729Pay *rtpg729pay;
  GstBaseRTPAudioPayload *basertpaudiopayload;
  gboolean ret;
  GstStructure *structure;
  const char *payload_name;

  rtpg729pay = GST_RTP_G729_PAY (basertppayload);
  basertpaudiopayload = GST_BASE_RTP_AUDIO_PAYLOAD (basertppayload);

  structure = gst_caps_get_structure (caps, 0);

  payload_name = gst_structure_get_name (structure);
  if (g_strcasecmp ("audio/G729", payload_name) != 0)
    goto wrong_name;

  ret = gst_basertppayload_set_outcaps (basertppayload, NULL);

  return ret;

  /* ERRORS */
wrong_name:
  {
    GST_ERROR_OBJECT (rtpg729pay, "wrong name, expected 'audio/G729', got '%s'",
        payload_name);
    return FALSE;
  }
}

gboolean
gst_rtp_g729_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpg729pay",
      GST_RANK_NONE, GST_TYPE_RTP_G729_PAY);
}
