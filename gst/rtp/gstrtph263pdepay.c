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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>

#include <gst/rtp/gstrtpbuffer.h>
#include "gstrtph263pdepay.h"

/* elementfactory information */
static const GstElementDetails gst_rtp_h263pdepay_details =
GST_ELEMENT_DETAILS ("RTP packet depayloader",
    "Codec/Depayloader/Network",
    "Extracts H263+ video from RTP packets (RFC 2429)",
    "Wim Taymans <wim@fluendo.com>");

/* RtpH263PDepay signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_FREQUENCY
};

static GstStaticPadTemplate gst_rtp_h263p_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h263, "
        "variant = (string) \"itu\", " "h263version = (string) \"h263p\"")
    );

static GstStaticPadTemplate gst_rtp_h263p_depay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 90000, " "encoding-name = (string) \"H263-1998\"")
    );

GST_BOILERPLATE (GstRtpH263PDepay, gst_rtp_h263p_depay, GstBaseRTPDepayload,
    GST_TYPE_BASE_RTP_DEPAYLOAD);

static void gst_rtp_h263p_depay_finalize (GObject * object);
static void gst_rtp_h263p_depay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_h263p_depay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_rtp_h263p_depay_change_state (GstElement *
    element, GstStateChange transition);

static GstBuffer *gst_rtp_h263p_depay_process (GstBaseRTPDepayload * depayload,
    GstBuffer * buf);
gboolean gst_rtp_h263p_depay_setcaps (GstBaseRTPDepayload * filter,
    GstCaps * caps);

static void
gst_rtp_h263p_depay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_h263p_depay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_h263p_depay_sink_template));


  gst_element_class_set_details (element_class, &gst_rtp_h263pdepay_details);
}

static void
gst_rtp_h263p_depay_class_init (GstRtpH263PDepayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPDepayloadClass *gstbasertpdepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertpdepayload_class = (GstBaseRTPDepayloadClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gstbasertpdepayload_class->process = gst_rtp_h263p_depay_process;
  gstbasertpdepayload_class->set_caps = gst_rtp_h263p_depay_setcaps;

  gobject_class->finalize = gst_rtp_h263p_depay_finalize;

  gobject_class->set_property = gst_rtp_h263p_depay_set_property;
  gobject_class->get_property = gst_rtp_h263p_depay_get_property;

  gstelement_class->change_state = gst_rtp_h263p_depay_change_state;
}

static void
gst_rtp_h263p_depay_init (GstRtpH263PDepay * rtph263pdepay,
    GstRtpH263PDepayClass * klass)
{
  rtph263pdepay->adapter = gst_adapter_new ();
}

static void
gst_rtp_h263p_depay_finalize (GObject * object)
{
  GstRtpH263PDepay *rtph263pdepay;

  rtph263pdepay = GST_RTP_H263P_DEPAY (object);

  g_object_unref (rtph263pdepay->adapter);
  rtph263pdepay->adapter = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

gboolean
gst_rtp_h263p_depay_setcaps (GstBaseRTPDepayload * filter, GstCaps * caps)
{

  GstStructure *structure = gst_caps_get_structure (caps, 0);
  gint clock_rate = 90000;      /* default */

  gst_structure_get_int (structure, "clock-rate", &clock_rate);
  filter->clock_rate = clock_rate;

  return TRUE;
}


static GstBuffer *
gst_rtp_h263p_depay_process (GstBaseRTPDepayload * depayload, GstBuffer * buf)
{

  GstRtpH263PDepay *rtph263pdepay;
  GstBuffer *outbuf;

  rtph263pdepay = GST_RTP_H263P_DEPAY (depayload);

  if (!gst_rtp_buffer_validate (buf))
    goto bad_packet;

  {
    gint payload_len;
    guint8 *payload;
    gboolean P, V, M;
    guint32 timestamp;
    guint header_len;
    guint8 PLEN;

    payload_len = gst_rtp_buffer_get_payload_len (buf);
    payload = gst_rtp_buffer_get_payload (buf);

    header_len = 2;

    M = gst_rtp_buffer_get_marker (buf);
    P = (payload[0] & 0x04) == 0x04;
    V = (payload[0] & 0x02) == 0x02;
    PLEN = ((payload[0] & 0x1) << 5) | (payload[1] >> 3);

    if (V) {
      header_len++;
    }
    if (PLEN) {
      header_len += PLEN;
    }

    if (P) {
      header_len -= 2;
      payload[header_len] = 0;
      payload[header_len + 1] = 0;
    }

    /* FIXME do not ignore the VRC header (See RFC 2429 section 4.2) */
    /* strip off header */
    payload += header_len;
    payload_len -= header_len;

    timestamp = gst_rtp_buffer_get_timestamp (buf);

    if (M) {
      /* frame is completed: append to previous, push it out */
      guint len;
      guint avail;
      guint8 *data;

      avail = gst_adapter_available (rtph263pdepay->adapter);

      len = avail + payload_len;
      outbuf = gst_buffer_new_and_alloc (len + (len % 4) + 4);
      memset (GST_BUFFER_DATA (outbuf) + len, 0, (len % 4) + 4);
      GST_BUFFER_SIZE (outbuf) = len;

      /* prepend previous data */
      if (avail > 0) {
        data = (guint8 *) gst_adapter_peek (rtph263pdepay->adapter, avail);
        memcpy (GST_BUFFER_DATA (outbuf), data, avail);
        gst_adapter_flush (rtph263pdepay->adapter, avail);
      }
      memcpy (GST_BUFFER_DATA (outbuf) + avail, payload, payload_len);

      GST_BUFFER_TIMESTAMP (outbuf) =
          timestamp * GST_SECOND / depayload->clock_rate;

      gst_buffer_set_caps (outbuf,
          (GstCaps *) gst_pad_get_pad_template_caps (depayload->srcpad));

      return outbuf;

    } else {
      /* frame not completed: store in adapter */
      outbuf = gst_buffer_new_and_alloc (payload_len);

      memcpy (GST_BUFFER_DATA (outbuf), payload, payload_len);

      gst_adapter_push (rtph263pdepay->adapter, outbuf);
    }
  }
  return NULL;

bad_packet:
  {
    GST_ELEMENT_WARNING (rtph263pdepay, STREAM, DECODE,
        ("Packet did not validate"), (NULL));
    return NULL;
  }
}

static void
gst_rtp_h263p_depay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpH263PDepay *rtph263pdepay;

  rtph263pdepay = GST_RTP_H263P_DEPAY (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_h263p_depay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRtpH263PDepay *rtph263pdepay;

  rtph263pdepay = GST_RTP_H263P_DEPAY (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_rtp_h263p_depay_change_state (GstElement * element,
    GstStateChange transition)
{
  GstRtpH263PDepay *rtph263pdepay;
  GstStateChangeReturn ret;

  rtph263pdepay = GST_RTP_H263P_DEPAY (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_adapter_clear (rtph263pdepay->adapter);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }
  return ret;
}

gboolean
gst_rtp_h263p_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtph263pdepay",
      GST_RANK_MARGINAL, GST_TYPE_RTP_H263P_DEPAY);
}
