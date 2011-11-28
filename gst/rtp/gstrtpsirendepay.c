/*
 * Siren Depayloader Gst Element
 *
 *   @author: Youness Alaoui <kakaroto@kakaroto.homelinux.net>
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
#include <stdlib.h>
#include <gst/rtp/gstrtpbuffer.h>
#include "gstrtpsirendepay.h"

static GstStaticPadTemplate gst_rtp_siren_depay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 16000, "
        "encoding-name = (string) \"SIREN\", " "dct-length = (int) 320")
    );

static GstStaticPadTemplate gst_rtp_siren_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-siren, " "dct-length = (int) 320")
    );

static GstBuffer *gst_rtp_siren_depay_process (GstBaseRTPDepayload * depayload,
    GstBuffer * buf);
static gboolean gst_rtp_siren_depay_setcaps (GstBaseRTPDepayload * depayload,
    GstCaps * caps);

GST_BOILERPLATE (GstRTPSirenDepay, gst_rtp_siren_depay, GstBaseRTPDepayload,
    GST_TYPE_BASE_RTP_DEPAYLOAD);

static void
gst_rtp_siren_depay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_siren_depay_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_siren_depay_sink_template);
  gst_element_class_set_details_simple (element_class,
      "RTP Siren packet depayloader", "Codec/Depayloader/Network/RTP",
      "Extracts Siren audio from RTP packets",
      "Philippe Kalaf <philippe.kalaf@collabora.co.uk>");
}

static void
gst_rtp_siren_depay_class_init (GstRTPSirenDepayClass * klass)
{
  GstBaseRTPDepayloadClass *gstbasertpdepayload_class;

  gstbasertpdepayload_class = (GstBaseRTPDepayloadClass *) klass;

  gstbasertpdepayload_class->process = gst_rtp_siren_depay_process;
  gstbasertpdepayload_class->set_caps = gst_rtp_siren_depay_setcaps;
}

static void
gst_rtp_siren_depay_init (GstRTPSirenDepay * rtpsirendepay,
    GstRTPSirenDepayClass * klass)
{

}

static gboolean
gst_rtp_siren_depay_setcaps (GstBaseRTPDepayload * depayload, GstCaps * caps)
{
  GstCaps *srccaps;
  gboolean ret;

  srccaps = gst_caps_new_simple ("audio/x-siren",
      "dct-length", G_TYPE_INT, 320, NULL);
  ret = gst_pad_set_caps (GST_BASE_RTP_DEPAYLOAD_SRCPAD (depayload), srccaps);

  GST_DEBUG ("set caps on source: %" GST_PTR_FORMAT " (ret=%d)", srccaps, ret);
  gst_caps_unref (srccaps);

  /* always fixed clock rate of 16000 */
  depayload->clock_rate = 16000;

  return ret;
}

static GstBuffer *
gst_rtp_siren_depay_process (GstBaseRTPDepayload * depayload, GstBuffer * buf)
{
  GstBuffer *outbuf;

  outbuf = gst_rtp_buffer_get_payload_buffer (buf);

  return outbuf;
}

gboolean
gst_rtp_siren_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpsirendepay",
      GST_RANK_SECONDARY, GST_TYPE_RTP_SIREN_DEPAY);
}
