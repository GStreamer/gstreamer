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

#include <string.h>

#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtph263ppay.h"

#define DEFAULT_FRAGMENTATION_MODE   GST_FRAGMENTATION_MODE_NORMAL

enum
{
  PROP_0,
  PROP_FRAGMENTATION_MODE
};

#define GST_TYPE_FRAGMENTATION_MODE (gst_fragmentation_mode_get_type())
static GType
gst_fragmentation_mode_get_type (void)
{
  static GType fragmentation_mode_type = 0;
  static const GEnumValue fragmentation_mode[] = {
    {GST_FRAGMENTATION_MODE_NORMAL, "Normal", "normal"},
    {GST_FRAGMENTATION_MODE_SYNC, "Fragment at sync points", "sync"},
    {0, NULL, NULL},
  };

  if (!fragmentation_mode_type) {
    fragmentation_mode_type =
        g_enum_register_static ("GstFragmentationMode", fragmentation_mode);
  }
  return fragmentation_mode_type;
}


GST_DEBUG_CATEGORY_STATIC (rtph263ppay_debug);
#define GST_CAT_DEFAULT rtph263ppay_debug

static GstStaticPadTemplate gst_rtp_h263p_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h263, " "variant = (string) \"itu\" ")
    );

static GstStaticPadTemplate gst_rtp_h263p_pay_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 90000, " "encoding-name = (string) \"H263-1998\"; "
        "application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 90000, " "encoding-name = (string) \"H263-2000\"")
    );

static void gst_rtp_h263p_pay_finalize (GObject * object);

static void gst_rtp_h263p_pay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_h263p_pay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_rtp_h263p_pay_setcaps (GstBaseRTPPayload * payload,
    GstCaps * caps);
static GstFlowReturn gst_rtp_h263p_pay_handle_buffer (GstBaseRTPPayload *
    payload, GstBuffer * buffer);

GST_BOILERPLATE (GstRtpH263PPay, gst_rtp_h263p_pay, GstBaseRTPPayload,
    GST_TYPE_BASE_RTP_PAYLOAD);

static void
gst_rtp_h263p_pay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_h263p_pay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_h263p_pay_sink_template));

  gst_element_class_set_details_simple (element_class, "RTP H263 payloader",
      "Codec/Payloader/Network/RTP",
      "Payload-encodes H263/+/++ video in RTP packets (RFC 4629)",
      "Wim Taymans <wim.taymans@gmail.com>");
}

static void
gst_rtp_h263p_pay_class_init (GstRtpH263PPayClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gobject_class = (GObjectClass *) klass;
  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  gobject_class->finalize = gst_rtp_h263p_pay_finalize;
  gobject_class->set_property = gst_rtp_h263p_pay_set_property;
  gobject_class->get_property = gst_rtp_h263p_pay_get_property;

  gstbasertppayload_class->set_caps = gst_rtp_h263p_pay_setcaps;
  gstbasertppayload_class->handle_buffer = gst_rtp_h263p_pay_handle_buffer;

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_FRAGMENTATION_MODE, g_param_spec_enum ("fragmentation-mode",
          "Fragmentation Mode",
          "Packet Fragmentation Mode", GST_TYPE_FRAGMENTATION_MODE,
          DEFAULT_FRAGMENTATION_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (rtph263ppay_debug, "rtph263ppay",
      0, "rtph263ppay (RFC 4629)");
}

static void
gst_rtp_h263p_pay_init (GstRtpH263PPay * rtph263ppay,
    GstRtpH263PPayClass * klass)
{
  rtph263ppay->adapter = gst_adapter_new ();

  rtph263ppay->fragmentation_mode = DEFAULT_FRAGMENTATION_MODE;
}

static void
gst_rtp_h263p_pay_finalize (GObject * object)
{
  GstRtpH263PPay *rtph263ppay;

  rtph263ppay = GST_RTP_H263P_PAY (object);

  g_object_unref (rtph263ppay->adapter);
  rtph263ppay->adapter = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_rtp_h263p_pay_setcaps (GstBaseRTPPayload * payload, GstCaps * caps)
{
  gboolean res;
  GstCaps *peercaps;
  gchar *encoding_name = NULL;

  g_return_val_if_fail (gst_caps_is_fixed (caps), FALSE);

  peercaps = gst_pad_peer_get_caps (GST_BASE_RTP_PAYLOAD_SRCPAD (payload));
  if (peercaps) {
    GstCaps *intersect = gst_caps_intersect (peercaps,
        gst_pad_get_pad_template_caps (GST_BASE_RTP_PAYLOAD_SRCPAD (payload)));

    gst_caps_unref (peercaps);
    if (!gst_caps_is_empty (intersect)) {
      GstStructure *s = gst_caps_get_structure (intersect, 0);
      encoding_name = g_strdup (gst_structure_get_string (s, "encoding-name"));
    }
    gst_caps_unref (intersect);
  }

  if (!encoding_name)
    encoding_name = g_strdup ("H263-1998");

  gst_basertppayload_set_options (payload, "video", TRUE,
      (gchar *) encoding_name, 90000);
  res = gst_basertppayload_set_outcaps (payload, NULL);
  g_free (encoding_name);

  return res;
}

static void
gst_rtp_h263p_pay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpH263PPay *rtph263ppay;

  rtph263ppay = GST_RTP_H263P_PAY (object);

  switch (prop_id) {
    case PROP_FRAGMENTATION_MODE:
      rtph263ppay->fragmentation_mode = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_h263p_pay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRtpH263PPay *rtph263ppay;

  rtph263ppay = GST_RTP_H263P_PAY (object);

  switch (prop_id) {
    case PROP_FRAGMENTATION_MODE:
      g_value_set_enum (value, rtph263ppay->fragmentation_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstFlowReturn
gst_rtp_h263p_pay_flush (GstRtpH263PPay * rtph263ppay)
{
  guint avail;
  GstBuffer *outbuf;
  GstFlowReturn ret;
  gboolean fragmented;

  avail = gst_adapter_available (rtph263ppay->adapter);
  if (avail == 0)
    return GST_FLOW_OK;

  fragmented = FALSE;
  /* This algorithm assumes the H263/+/++ encoder sends complete frames in each
   * buffer */
  /* With Fragmentation Mode at GST_FRAGMENTATION_MODE_NORMAL:
   *  This algorithm implements the Follow-on packets method for packetization.
   *  This assumes low packet loss network. 
   * With Fragmentation Mode at GST_FRAGMENTATION_MODE_SYNC:
   *  This algorithm separates large frames at synchronisation points (Segments)
   *  (See RFC 4629 section 6). It would be interesting to have a property such as network
   *  quality to select between both packetization methods */
  /* TODO Add VRC supprt (See RFC 4629 section 5.2) */

  while (avail > 0) {
    guint towrite;
    guint8 *payload;
    guint payload_len;
    gint header_len;
    guint next_gop = 0;
    gboolean found_gob = FALSE;

    if (rtph263ppay->fragmentation_mode == GST_FRAGMENTATION_MODE_SYNC) {
      /* start after 1st gop possible */
      guint parsed_len = 3;
      const guint8 *parse_data = NULL;

      parse_data = gst_adapter_peek (rtph263ppay->adapter, avail);

      /* Check if we have a gob or eos , eossbs */
      /* FIXME EOS and EOSSBS packets should never contain any gobs and vice-versa */
      if (avail >= 3 && *parse_data == 0 && *(parse_data + 1) == 0
          && *(parse_data + 2) >= 0x80) {
        GST_DEBUG_OBJECT (rtph263ppay, " Found GOB header");
        found_gob = TRUE;
      }
      /* Find next and cut the packet accordingly */
      /* TODO we should get as many gobs as possible until MTU is reached, this
       * code seems to just get one GOB per packet */
      while (parsed_len + 2 < avail) {
        if (parse_data[parsed_len] == 0 && parse_data[parsed_len + 1] == 0
            && parse_data[parsed_len + 2] >= 0x80) {
          next_gop = parsed_len;
          GST_DEBUG_OBJECT (rtph263ppay, " Next GOB Detected at :  %d",
              next_gop);
          break;
        }
        parsed_len++;
      }
    }

    /* for picture start frames (non-fragmented), we need to remove the first
     * two 0x00 bytes and set P=1 */
    header_len = (fragmented && !found_gob) ? 2 : 0;

    towrite = MIN (avail, gst_rtp_buffer_calc_payload_len
        (GST_BASE_RTP_PAYLOAD_MTU (rtph263ppay) - header_len, 0, 0));

    if (next_gop > 0)
      towrite = MIN (next_gop, towrite);

    payload_len = header_len + towrite;

    outbuf = gst_rtp_buffer_new_allocate (payload_len, 0, 0);
    /* last fragment gets the marker bit set */
    gst_rtp_buffer_set_marker (outbuf, avail > towrite ? 0 : 1);

    payload = gst_rtp_buffer_get_payload (outbuf);

    gst_adapter_copy (rtph263ppay->adapter, &payload[header_len], 0, towrite);

    /*  0                   1
     *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * |   RR    |P|V|   PLEN    |PEBIT|
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     */
    /* if fragmented or gop header , write p bit =1 */
    payload[0] = (fragmented && !found_gob) ? 0x00 : 0x04;
    payload[1] = 0;

    GST_BUFFER_TIMESTAMP (outbuf) = rtph263ppay->first_timestamp;
    GST_BUFFER_DURATION (outbuf) = rtph263ppay->first_duration;

    gst_adapter_flush (rtph263ppay->adapter, towrite);

    ret = gst_basertppayload_push (GST_BASE_RTP_PAYLOAD (rtph263ppay), outbuf);

    avail -= towrite;
    fragmented = TRUE;
  }

  return ret;
}

static GstFlowReturn
gst_rtp_h263p_pay_handle_buffer (GstBaseRTPPayload * payload,
    GstBuffer * buffer)
{
  GstRtpH263PPay *rtph263ppay;
  GstFlowReturn ret;

  rtph263ppay = GST_RTP_H263P_PAY (payload);

  rtph263ppay->first_timestamp = GST_BUFFER_TIMESTAMP (buffer);
  rtph263ppay->first_duration = GST_BUFFER_DURATION (buffer);

  /* we always encode and flush a full picture */
  gst_adapter_push (rtph263ppay->adapter, buffer);
  ret = gst_rtp_h263p_pay_flush (rtph263ppay);

  return ret;
}

gboolean
gst_rtp_h263p_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtph263ppay",
      GST_RANK_SECONDARY, GST_TYPE_RTP_H263P_PAY);
}
