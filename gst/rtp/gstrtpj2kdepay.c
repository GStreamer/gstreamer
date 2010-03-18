/* GStreamer
 * Copyright (C) <2009> Wim Taymans <wim.taymans@gmail.com>
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
#include "gstrtpj2kdepay.h"

GST_DEBUG_CATEGORY_STATIC (rtpj2kdepay_debug);
#define GST_CAT_DEFAULT (rtpj2kdepay_debug)

static GstStaticPadTemplate gst_rtp_j2k_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/x-jpc")
    );

static GstStaticPadTemplate gst_rtp_j2k_depay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 90000, " "encoding-name = (string) \"JPEG2000\"")
    );

GST_BOILERPLATE (GstRtpJ2KDepay, gst_rtp_j2k_depay, GstBaseRTPDepayload,
    GST_TYPE_BASE_RTP_DEPAYLOAD);

static void gst_rtp_j2k_depay_finalize (GObject * object);

static GstStateChangeReturn
gst_rtp_j2k_depay_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_rtp_j2k_depay_setcaps (GstBaseRTPDepayload * depayload,
    GstCaps * caps);
static GstBuffer *gst_rtp_j2k_depay_process (GstBaseRTPDepayload * depayload,
    GstBuffer * buf);

static void
gst_rtp_j2k_depay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_j2k_depay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_j2k_depay_sink_template));

  gst_element_class_set_details_simple (element_class,
      "RTP JPEG 2000 depayloader", "Codec/Depayloader/Network",
      "Extracts JPEG 2000 video from RTP packets (RFC 5371)",
      "Wim Taymans <wim.taymans@gmail.com>");
}

static void
gst_rtp_j2k_depay_class_init (GstRtpJ2KDepayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPDepayloadClass *gstbasertpdepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertpdepayload_class = (GstBaseRTPDepayloadClass *) klass;

  gobject_class->finalize = gst_rtp_j2k_depay_finalize;

  gstelement_class->change_state = gst_rtp_j2k_depay_change_state;

  gstbasertpdepayload_class->set_caps = gst_rtp_j2k_depay_setcaps;
  gstbasertpdepayload_class->process = gst_rtp_j2k_depay_process;

  GST_DEBUG_CATEGORY_INIT (rtpj2kdepay_debug, "rtpj2kdepay", 0,
      "J2K Video RTP Depayloader");
}

static void
gst_rtp_j2k_depay_init (GstRtpJ2KDepay * rtpj2kdepay,
    GstRtpJ2KDepayClass * klass)
{
  rtpj2kdepay->adapter = gst_adapter_new ();
}

static void
gst_rtp_j2k_depay_finalize (GObject * object)
{
  GstRtpJ2KDepay *rtpj2kdepay;

  rtpj2kdepay = GST_RTP_J2K_DEPAY (object);

  g_object_unref (rtpj2kdepay->adapter);
  rtpj2kdepay->adapter = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_rtp_j2k_depay_setcaps (GstBaseRTPDepayload * depayload, GstCaps * caps)
{
  GstStructure *structure;
  gint clock_rate;
  GstCaps *outcaps;
  gboolean res;

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "clock-rate", &clock_rate))
    clock_rate = 90000;
  depayload->clock_rate = clock_rate;

  outcaps =
      gst_caps_new_simple ("image/x-jpc", "framerate", GST_TYPE_FRACTION, 0, 1,
      "fields", G_TYPE_INT, 1, "fourcc", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('s',
          'Y', 'U', 'V'), NULL);
  res = gst_pad_set_caps (depayload->srcpad, outcaps);
  gst_caps_unref (outcaps);

  return res;
}

static GstBuffer *
gst_rtp_j2k_depay_process (GstBaseRTPDepayload * depayload, GstBuffer * buf)
{
  GstRtpJ2KDepay *rtpj2kdepay;
  GstBuffer *outbuf;
  guint8 *payload;
  guint frag_offset;

  rtpj2kdepay = GST_RTP_J2K_DEPAY (depayload);

  /* flush everything on discont for now */
  if (GST_BUFFER_IS_DISCONT (buf)) {
    GST_DEBUG_OBJECT (rtpj2kdepay, "DISCONT, flushing data");
    gst_adapter_clear (rtpj2kdepay->adapter);
    rtpj2kdepay->need_header = TRUE;
  }

  if (gst_rtp_buffer_get_payload_len (buf) < 8)
    goto empty_packet;

  payload = gst_rtp_buffer_get_payload (buf);

  /*
   *  0                   1                   2                   3
   *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |tp |MHF|mh_id|T|     priority  |           tile number         |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |reserved       |             fragment offset                   |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   */
  frag_offset = (payload[5] << 16) | (payload[6] << 8) | payload[7];

  GST_DEBUG_OBJECT (rtpj2kdepay, "frag %u", frag_offset);

  if (rtpj2kdepay->need_header) {
    if (frag_offset != 0)
      goto waiting_header;

    rtpj2kdepay->need_header = FALSE;
  }

  /* take JPEG 2000 data, push in the adapter */
  outbuf = gst_rtp_buffer_get_payload_subbuffer (buf, 8, -1);
  gst_adapter_push (rtpj2kdepay->adapter, outbuf);
  outbuf = NULL;

  if (gst_rtp_buffer_get_marker (buf)) {
    guint avail;
    guint8 end[2];
    guint8 *data;

    /* last buffer take all data out of the adapter */
    avail = gst_adapter_available (rtpj2kdepay->adapter);
    GST_DEBUG_OBJECT (rtpj2kdepay, "marker set, last buffer");

    /* take the last bytes of the JPEG 2000 data to see if there is an EOC
     * marker */
    gst_adapter_copy (rtpj2kdepay->adapter, end, avail - 2, 2);

    if (end[0] != 0xff && end[1] != 0xd9) {
      GST_DEBUG_OBJECT (rtpj2kdepay, "no EOC marker, adding one");

      /* no EOI marker, add one */
      outbuf = gst_buffer_new_and_alloc (2);
      data = GST_BUFFER_DATA (outbuf);
      data[0] = 0xff;
      data[1] = 0xd9;

      gst_adapter_push (rtpj2kdepay->adapter, outbuf);
      avail += 2;
    }
    outbuf = gst_adapter_take_buffer (rtpj2kdepay->adapter, avail);

    GST_DEBUG_OBJECT (rtpj2kdepay, "returning %u bytes", avail);
  }

  return outbuf;

  /* ERRORS */
empty_packet:
  {
    GST_ELEMENT_WARNING (rtpj2kdepay, STREAM, DECODE,
        ("Empty Payload."), (NULL));
    return NULL;
  }
waiting_header:
  {
    GST_DEBUG_OBJECT (rtpj2kdepay, "we are waiting for a header");
    return NULL;
  }
}

static GstStateChangeReturn
gst_rtp_j2k_depay_change_state (GstElement * element, GstStateChange transition)
{
  GstRtpJ2KDepay *rtpj2kdepay;
  GstStateChangeReturn ret;

  rtpj2kdepay = GST_RTP_J2K_DEPAY (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_adapter_clear (rtpj2kdepay->adapter);
      rtpj2kdepay->need_header = TRUE;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_adapter_clear (rtpj2kdepay->adapter);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }
  return ret;
}

gboolean
gst_rtp_j2k_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpj2kdepay",
      GST_RANK_MARGINAL, GST_TYPE_RTP_J2K_DEPAY);
}
