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
#  include "config.h"
#endif

#include <string.h>

#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtph264pay.h"

GST_DEBUG_CATEGORY_STATIC (rtph264pay_debug);
#define GST_CAT_DEFAULT (rtph264pay_debug)

/* references:
 *
 * RFC 3984
 */

/* elementfactory information */
static const GstElementDetails gst_rtp_h264pay_details =
GST_ELEMENT_DETAILS ("RTP packet payloader",
    "Codec/Payloader/Network",
    "Payload-encode H264 video into RTP packets (RFC 3984)",
    "Laurent Glayal <spglegle@yahoo.fr>");

static GstStaticPadTemplate gst_rtp_h264_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264")
    );

static GstStaticPadTemplate gst_rtp_h264_pay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 90000, " "encoding-name = (string) \"H264\"")
    );

static void gst_rtp_h264_pay_finalize (GObject * object);

static void gst_rtp_h264_pay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_h264_pay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_rtp_h264_pay_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_rtp_h264_pay_setcaps (GstBaseRTPPayload * basepayload,
    GstCaps * caps);
static GstFlowReturn gst_rtp_h264_pay_handle_buffer (GstBaseRTPPayload * pad,
    GstBuffer * buffer);

GST_BOILERPLATE (GstRtpH264Pay, gst_rtp_h264_pay, GstBaseRTPPayload,
    GST_TYPE_BASE_RTP_PAYLOAD);

static void
gst_rtp_h264_pay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_h264_pay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_h264_pay_sink_template));

  gst_element_class_set_details (element_class, &gst_rtp_h264pay_details);
}

static void
gst_rtp_h264_pay_class_init (GstRtpH264PayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  gobject_class->finalize = gst_rtp_h264_pay_finalize;
  gobject_class->set_property = gst_rtp_h264_pay_set_property;
  gobject_class->get_property = gst_rtp_h264_pay_get_property;

  gstelement_class->change_state = gst_rtp_h264_pay_change_state;

  gstbasertppayload_class->set_caps = gst_rtp_h264_pay_setcaps;
  gstbasertppayload_class->handle_buffer = gst_rtp_h264_pay_handle_buffer;

  GST_DEBUG_CATEGORY_INIT (rtph264pay_debug, "rtph264pay", 0,
      "H264 RTP Payloader");
}

static void
gst_rtp_h264_pay_init (GstRtpH264Pay * rtph264pay, GstRtpH264PayClass * klass)
{
}

static void
gst_rtp_h264_pay_finalize (GObject * object)
{
  GstRtpH264Pay *rtph264pay;

  rtph264pay = GST_RTP_H264_PAY (object);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}


static gboolean
gst_rtp_h264_pay_setcaps (GstBaseRTPPayload * basepayload, GstCaps * caps)
{
  GstRtpH264Pay *rtph264pay;

  rtph264pay = GST_RTP_H264_PAY (basepayload);

  gst_basertppayload_set_options (basepayload, "video", TRUE, "H264", 90000);
  gst_basertppayload_set_outcaps (basepayload, NULL);

  return TRUE;
}


static GstFlowReturn
gst_rtp_h264_pay_handle_buffer (GstBaseRTPPayload * basepayload,
    GstBuffer * buffer)
{

  GstRtpH264Pay *rtph264pay;
  GstFlowReturn ret;
  guint size, idxdata;
  GstBuffer *outbuf;
  guint8 *payload, *data, *pdata;
  guint8 nalType;
  GstClockTime timestamp;
  guint packet_len, payload_len, mtu;

  rtph264pay = GST_RTP_H264_PAY (basepayload);
  mtu = GST_BASE_RTP_PAYLOAD_MTU (rtph264pay);

  size = GST_BUFFER_SIZE (buffer);
  data = GST_BUFFER_DATA (buffer);
  timestamp = GST_BUFFER_TIMESTAMP (buffer);

  GST_DEBUG_OBJECT (basepayload, "got %d bytes", size);

  /* H264 stream analysis */
  pdata = data;
  idxdata = size;
  while (idxdata > 5 &&
      (pdata[0] != 0x00 || pdata[1] != 0x00 || pdata[2] != 0x1 ||
          (pdata[3] & 0x1f) < 1 || (pdata[3] & 0x1f) > 23)
      ) {
    pdata++;
    idxdata--;
    GST_DEBUG_OBJECT (basepayload, "idxdata=%d", idxdata);
  }

  if (idxdata < 5) {
    GST_DEBUG_OBJECT (basepayload,
        "Returning GST_FLOW_OK without creating RTP packet");
    return GST_FLOW_OK;
  }

  pdata += 3;
  idxdata -= 3;

  nalType = pdata[0] & 0x1f;
  GST_DEBUG_OBJECT (basepayload, "Processing Buffer with NAL TYPE=%d", nalType);

  packet_len = gst_rtp_buffer_calc_packet_len (idxdata, 0, 0);

  if (packet_len < mtu) {
    GST_DEBUG_OBJECT (basepayload,
        "NAL Unit fit in one packet datasize=%d mtu=%d", idxdata, mtu);
    /* will fit in one packet */
    outbuf = gst_rtp_buffer_new_allocate (idxdata, 0, 0);
    GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
    gst_rtp_buffer_set_marker (outbuf, 1);

    payload = gst_rtp_buffer_get_payload (outbuf);
    GST_DEBUG_OBJECT (basepayload, "Copying %d bytes to outbuf", idxdata);
    memcpy (payload, pdata, idxdata);
    gst_buffer_unref (buffer);
    ret = gst_basertppayload_push (basepayload, outbuf);
    return ret;
  } else {
    GST_DEBUG_OBJECT (basepayload,
        "NAL Unit DOES NOT fit in one packet datasize=%d mtu=%d", idxdata, mtu);

    /* Fragmentation Units FU-A */
    guint8 nalHeader;
    guint limitedSize;

    int ii = 0, start = 1, end = 0, first = 0;

    nalHeader = *pdata;
    pdata++;
    idxdata--;

    ret = GST_FLOW_OK;

    GST_DEBUG_OBJECT (basepayload, "Using FU-A fragmentation for data size=%d",
        idxdata);

    payload_len = gst_rtp_buffer_calc_payload_len (mtu - 2, 0, 0);      /* We keep 2 bytes for FU indicator and FU Header */

    while (end == 0) {
      limitedSize = idxdata < payload_len ? idxdata : payload_len;
      GST_DEBUG_OBJECT (basepayload,
          "Inside  FU-A fragmentation limitedSize=%d iteration=%d", limitedSize,
          ii);

      outbuf = gst_rtp_buffer_new_allocate (limitedSize + 2, 0, 0);
      GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
      gst_rtp_buffer_set_marker (outbuf, end);
      payload = gst_rtp_buffer_get_payload (outbuf);

      if (limitedSize == idxdata) {
        GST_DEBUG_OBJECT (basepayload, "end idxdata=%d iteration=%d", idxdata,
            ii);
        end = 1;
      }

      /* FU indicator */
      payload[0] = (nalHeader & 0x60) | 28;

      /* FU Header */
      payload[1] = (start << 7) | (end << 6) | (nalHeader & 0x1f);

      memcpy (&payload[2], pdata + first, limitedSize);
      GST_DEBUG_OBJECT (basepayload,
          "recorded %d payload bytes into packet iteration=%d", limitedSize + 2,
          ii);

      ret = gst_basertppayload_push (basepayload, outbuf);
      if (ret != GST_FLOW_OK)
        break;

      idxdata -= limitedSize;
      first += limitedSize;
      ii++;
      start = 0;
    }

    gst_buffer_unref (buffer);
    return ret;
  }

  GST_ELEMENT_ERROR (basepayload, STREAM, FORMAT,
      (NULL), ("Should not be there !!"));
  gst_buffer_unref (buffer);

  return GST_FLOW_ERROR;

}

static void
gst_rtp_h264_pay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpH264Pay *rtph264pay;

  rtph264pay = GST_RTP_H264_PAY (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_h264_pay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRtpH264Pay *rtph264pay;

  rtph264pay = GST_RTP_H264_PAY (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_rtp_h264_pay_change_state (GstElement * element, GstStateChange transition)
{
  GstRtpH264Pay *rtph264pay;
  GstStateChangeReturn ret;

  rtph264pay = GST_RTP_H264_PAY (element);

  switch (transition) {
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
gst_rtp_h264_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtph264pay",
      GST_RANK_NONE, GST_TYPE_RTP_H264_PAY);
}
