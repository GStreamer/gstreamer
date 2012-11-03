/*
 * Opus Depayloader Gst Element
 *
 *   @author: Danilo Cesar Lemes de Paula <danilo.cesar@collabora.co.uk>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <gst/rtp/gstrtpbuffer.h>
#include "gstrtpopusdepay.h"

GST_DEBUG_CATEGORY_STATIC (rtpopusdepay_debug);
#define GST_CAT_DEFAULT (rtpopusdepay_debug)

static GstStaticPadTemplate gst_rtp_opus_depay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ","
        "clock-rate = (int) 48000, "
        "encoding-name = (string) \"X-GST-OPUS-DRAFT-SPITTKA-00\"")
    );

static GstStaticPadTemplate gst_rtp_opus_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-opus")
    );

static GstBuffer *gst_rtp_opus_depay_process (GstRTPBaseDepayload * depayload,
    GstBuffer * buf);
static gboolean gst_rtp_opus_depay_setcaps (GstRTPBaseDepayload * depayload,
    GstCaps * caps);

G_DEFINE_TYPE (GstRTPOpusDepay, gst_rtp_opus_depay,
    GST_TYPE_RTP_BASE_DEPAYLOAD);

static void
gst_rtp_opus_depay_class_init (GstRTPOpusDepayClass * klass)
{
  GstRTPBaseDepayloadClass *gstbasertpdepayload_class;
  GstElementClass *element_class;

  element_class = GST_ELEMENT_CLASS (klass);
  gstbasertpdepayload_class = (GstRTPBaseDepayloadClass *) klass;

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_opus_depay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_opus_depay_sink_template));
  gst_element_class_set_static_metadata (element_class,
      "RTP Opus packet depayloader", "Codec/Depayloader/Network/RTP",
      "Extracts Opus audio from RTP packets",
      "Danilo Cesar Lemes de Paula <danilo.cesar@collabora.co.uk>");

  gstbasertpdepayload_class->process = gst_rtp_opus_depay_process;
  gstbasertpdepayload_class->set_caps = gst_rtp_opus_depay_setcaps;

  GST_DEBUG_CATEGORY_INIT (rtpopusdepay_debug, "rtpopusdepay", 0,
      "Opus RTP Depayloader");
}

static void
gst_rtp_opus_depay_init (GstRTPOpusDepay * rtpopusdepay)
{

}

static gboolean
gst_rtp_opus_depay_setcaps (GstRTPBaseDepayload * depayload, GstCaps * caps)
{
  GstCaps *srccaps;
  gboolean ret;

  srccaps = gst_caps_new_empty_simple ("audio/x-opus");
  ret = gst_pad_set_caps (GST_RTP_BASE_DEPAYLOAD_SRCPAD (depayload), srccaps);

  GST_DEBUG_OBJECT (depayload,
      "set caps on source: %" GST_PTR_FORMAT " (ret=%d)", srccaps, ret);
  gst_caps_unref (srccaps);

  depayload->clock_rate = 48000;

  return ret;
}

static GstBuffer *
gst_rtp_opus_depay_process (GstRTPBaseDepayload * depayload, GstBuffer * buf)
{
  GstBuffer *outbuf;
  GstRTPBuffer rtpbuf = { NULL, };

  gst_rtp_buffer_map (buf, GST_MAP_READ, &rtpbuf);
  outbuf = gst_rtp_buffer_get_payload_buffer (&rtpbuf);
  gst_rtp_buffer_unmap (&rtpbuf);

  return outbuf;
}
