/* Farsight
 * Copyright (C) 2006 Marcel Moreaux <marcelm@spacelabs.nl>
 *           (C) 2008 Wim Taymans <wim.taymans@gmail.com>
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

#include <stdlib.h>
#include <string.h>
#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtpdvpay.h"

GST_DEBUG_CATEGORY (rtpdvpay_debug);
#define GST_CAT_DEFAULT (rtpdvpay_debug)

#define DEFAULT_MODE GST_DV_PAY_MODE_VIDEO
enum
{
  PROP_0,
  PROP_MODE
};

/* takes both system and non-system streams */
static GstStaticPadTemplate gst_rtp_dv_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-dv")
    );

static GstStaticPadTemplate gst_rtp_dv_pay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) { \"video\", \"audio\" } ,"
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "encoding-name = (string) \"DV\", "
        "clock-rate = (int) 90000,"
        "encode = (string) { \"SD-VCR/525-60\", \"SD-VCR/625-50\", \"HD-VCR/1125-60\","
        "\"HD-VCR/1250-50\", \"SDL-VCR/525-60\", \"SDL-VCR/625-50\","
        "\"306M/525-60\", \"306M/625-50\", \"314M-25/525-60\","
        "\"314M-25/625-50\", \"314M-50/525-60\", \"314M-50/625-50\" }"
        /* optional parameters can't go in the template
         * "audio = (string) { \"bundled\", \"none\" }"
         */
    )
    );

static gboolean gst_rtp_dv_pay_setcaps (GstBaseRTPPayload * payload,
    GstCaps * caps);
static GstFlowReturn gst_rtp_dv_pay_handle_buffer (GstBaseRTPPayload * payload,
    GstBuffer * buffer);

#define GST_TYPE_DV_PAY_MODE (gst_dv_pay_mode_get_type())
static GType
gst_dv_pay_mode_get_type (void)
{
  static GType dv_pay_mode_type = 0;
  static const GEnumValue dv_pay_modes[] = {
    {GST_DV_PAY_MODE_VIDEO, "Video only", "video"},
    {GST_DV_PAY_MODE_BUNDLED, "Video and Audio bundled", "bundled"},
    {GST_DV_PAY_MODE_AUDIO, "Audio only", "audio"},
    {0, NULL, NULL},
  };

  if (!dv_pay_mode_type) {
    dv_pay_mode_type = g_enum_register_static ("GstDVPayMode", dv_pay_modes);
  }
  return dv_pay_mode_type;
}


static void gst_dv_pay_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_dv_pay_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

GST_BOILERPLATE (GstRTPDVPay, gst_rtp_dv_pay, GstBaseRTPPayload,
    GST_TYPE_BASE_RTP_PAYLOAD)

     static void gst_rtp_dv_pay_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_dv_pay_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_dv_pay_src_template);
  gst_element_class_set_details_simple (element_class, "RTP DV Payloader",
      "Codec/Payloader/Network/RTP",
      "Payloads DV into RTP packets (RFC 3189)",
      "Marcel Moreaux <marcelm@spacelabs.nl>, Wim Taymans <wim.taymans@gmail.com>");
}

static void
gst_rtp_dv_pay_class_init (GstRTPDVPayClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gobject_class = (GObjectClass *) klass;
  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  gobject_class->set_property = gst_dv_pay_set_property;
  gobject_class->get_property = gst_dv_pay_get_property;

  gstbasertppayload_class->set_caps = gst_rtp_dv_pay_setcaps;
  gstbasertppayload_class->handle_buffer = gst_rtp_dv_pay_handle_buffer;

  g_object_class_install_property (gobject_class, PROP_MODE,
      g_param_spec_enum ("mode", "Mode",
          "The payload mode of payloading",
          GST_TYPE_DV_PAY_MODE, DEFAULT_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (rtpdvpay_debug, "rtpdvpay", 0, "DV RTP Payloader");
}

static void
gst_rtp_dv_pay_init (GstRTPDVPay * rtpdvpay, GstRTPDVPayClass * klass)
{
}

static void
gst_dv_pay_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstRTPDVPay *rtpdvpay = GST_RTP_DV_PAY (object);

  switch (prop_id) {
    case PROP_MODE:
      rtpdvpay->mode = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dv_pay_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstRTPDVPay *rtpdvpay = GST_RTP_DV_PAY (object);

  switch (prop_id) {
    case PROP_MODE:
      g_value_set_enum (value, rtpdvpay->mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_rtp_dv_pay_setcaps (GstBaseRTPPayload * payload, GstCaps * caps)
{
  /* We don't do anything here, but we could check if it's a system stream and if
   * it's not, default to sending the video only. We will negotiate downstream
   * caps when we get to see the first frame. */

  return TRUE;
}

static gboolean
gst_dv_pay_negotiate (GstRTPDVPay * rtpdvpay, guint8 * data, guint size)
{
  const gchar *encode, *media;
  gboolean audio_bundled, res;

  if ((data[3] & 0x80) == 0) {  /* DSF flag */
    /* it's an NTSC format */
    if ((data[80 * 5 + 48 + 3] & 0x4) && (data[80 * 5 + 48] == 0x60)) { /* 4:2:2 sampling */
      /* NTSC 50Mbps */
      encode = "314M-25/525-60";
    } else {                    /* 4:1:1 sampling */
      /* NTSC 25Mbps */
      encode = "SD-VCR/525-60";
    }
  } else {
    /* it's a PAL format */
    if ((data[80 * 5 + 48 + 3] & 0x4) && (data[80 * 5 + 48] == 0x60)) { /* 4:2:2 sampling */
      /* PAL 50Mbps */
      encode = "314M-50/625-50";
    } else if ((data[5] & 0x07) == 0) { /* APT flag */
      /* PAL 25Mbps 4:2:0 */
      encode = "SD-VCR/625-50";
    } else
      /* PAL 25Mbps 4:1:1 */
      encode = "314M-25/625-50";
  }

  media = "video";
  audio_bundled = FALSE;

  switch (rtpdvpay->mode) {
    case GST_DV_PAY_MODE_AUDIO:
      media = "audio";
      break;
    case GST_DV_PAY_MODE_BUNDLED:
      audio_bundled = TRUE;
      break;
    default:
      break;
  }
  gst_basertppayload_set_options (GST_BASE_RTP_PAYLOAD (rtpdvpay), media, TRUE,
      "DV", 90000);

  if (audio_bundled) {
    res = gst_basertppayload_set_outcaps (GST_BASE_RTP_PAYLOAD (rtpdvpay),
        "encode", G_TYPE_STRING, encode,
        "audio", G_TYPE_STRING, "bundled", NULL);
  } else {
    res = gst_basertppayload_set_outcaps (GST_BASE_RTP_PAYLOAD (rtpdvpay),
        "encode", G_TYPE_STRING, encode, NULL);
  }
  return res;
}

static gboolean
include_dif (GstRTPDVPay * rtpdvpay, guint8 * data)
{
  gint block_type;
  gboolean res;

  block_type = data[0] >> 5;

  switch (block_type) {
    case 0:                    /* Header block */
    case 1:                    /* Subcode block */
    case 2:                    /* VAUX block */
      /* always include these blocks */
      res = TRUE;
      break;
    case 3:                    /* Audio block */
      /* never include audio if we are doing video only */
      if (rtpdvpay->mode == GST_DV_PAY_MODE_VIDEO)
        res = FALSE;
      else
        res = TRUE;
      break;
    case 4:                    /* Video block */
      /* never include video if we are doing audio only */
      if (rtpdvpay->mode == GST_DV_PAY_MODE_AUDIO)
        res = FALSE;
      else
        res = TRUE;
      break;
    default:                   /* Something bogus, just ignore */
      res = FALSE;
      break;
  }
  return res;
}

/* Get a DV frame, chop it up in pieces, and push the pieces to the RTP layer.
 */
static GstFlowReturn
gst_rtp_dv_pay_handle_buffer (GstBaseRTPPayload * basepayload,
    GstBuffer * buffer)
{
  GstRTPDVPay *rtpdvpay;
  guint max_payload_size;
  GstBuffer *outbuf;
  GstFlowReturn ret = GST_FLOW_OK;
  gint hdrlen;
  guint size;
  guint8 *data;
  guint8 *dest;
  guint filled;

  rtpdvpay = GST_RTP_DV_PAY (basepayload);

  hdrlen = gst_rtp_buffer_calc_header_len (0);
  /* DV frames are made up from a bunch of DIF blocks. DIF blocks are 80 bytes
   * each, and we should put an integral number of them in each RTP packet.
   * Therefore, we round the available room down to the nearest multiple of 80.
   *
   * The available room is just the packet MTU, minus the RTP header length. */
  max_payload_size = ((GST_BASE_RTP_PAYLOAD_MTU (rtpdvpay) - hdrlen) / 80) * 80;

  /* The length of the buffer to transmit. */
  size = GST_BUFFER_SIZE (buffer);
  data = GST_BUFFER_DATA (buffer);

  GST_DEBUG_OBJECT (rtpdvpay,
      "DV RTP payloader got buffer of %u bytes, splitting in %u byte "
      "payload fragments, at time %" GST_TIME_FORMAT, size, max_payload_size,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)));

  if (!rtpdvpay->negotiated) {
    gst_dv_pay_negotiate (rtpdvpay, data, size);
    /* if we have not yet scanned the stream for its type, do so now */
    rtpdvpay->negotiated = TRUE;
  }

  outbuf = NULL;
  dest = NULL;
  filled = 0;

  /* while we have a complete DIF chunks left */
  while (size >= 80) {
    /* Allocate a new buffer, set the timestamp */
    if (outbuf == NULL) {
      outbuf = gst_rtp_buffer_new_allocate (max_payload_size, 0, 0);
      GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buffer);
      dest = gst_rtp_buffer_get_payload (outbuf);
      filled = 0;
    }

    /* inspect the DIF chunk, if we don't need to include it, skip to the next one. */
    if (include_dif (rtpdvpay, data)) {
      /* copy data in packet */
      memcpy (dest, data, 80);

      dest += 80;
      filled += 80;
    }

    /* go to next dif chunk */
    size -= 80;
    data += 80;

    /* push out the buffer if the next one would exceed the max packet size or
     * when we are pushing the last packet */
    if (filled + 80 > max_payload_size || size < 80) {
      if (size < 160) {
        guint hlen;

        /* set marker */
        gst_rtp_buffer_set_marker (outbuf, TRUE);

        /* shrink buffer to last packet */
        hlen = gst_rtp_buffer_get_header_len (outbuf);
        gst_rtp_buffer_set_packet_len (outbuf, hlen + filled);
      }
      /* Push out the created piece, and check for errors. */
      ret = gst_basertppayload_push (basepayload, outbuf);
      if (ret != GST_FLOW_OK)
        break;

      outbuf = NULL;
    }
  }
  gst_buffer_unref (buffer);

  return ret;
}

gboolean
gst_rtp_dv_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpdvpay",
      GST_RANK_SECONDARY, GST_TYPE_RTP_DV_PAY);
}
