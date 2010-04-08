/* GStreamer
 * Copyright (C) 2009 Wim Taymans <wim.taymans@gmail.com>
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

/**
 * SECTION:element-rtpj2kpay
 *
 * Payload encode JPEG 2000 pictures into RTP packets according to RFC 5371.
 * For detailed information see: http://www.rfc-editor.org/rfc/rfc5371.txt
 *
 * The payloader takes a JPEG 2000 picture, scans the header for packetization
 * units and constructs the RTP packet header followed by the actual JPEG 2000
 * codestream.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtpj2kpay.h"

static GstStaticPadTemplate gst_rtp_j2k_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/x-jpc")
    );

static GstStaticPadTemplate gst_rtp_j2k_pay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "  media = (string) \"video\", "
        "  payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "  clock-rate = (int) 90000, "
        "  encoding-name = (string) \"JPEG2000\"")
    );

GST_DEBUG_CATEGORY_STATIC (rtpj2kpay_debug);
#define GST_CAT_DEFAULT (rtpj2kpay_debug)

/*
 * RtpJ2KMarker:
 * @J2K_MARKER: Prefix for JPEG 2000 marker
 * @J2K_MARKER_SOC: Start of Codestream
 * @J2K_MARKER_SOT: Start of tile
 * @J2K_MARKER_EOC: End of Codestream
 *
 * Identifers for markers in JPEG 2000 codestreams
 */
typedef enum
{
  J2K_MARKER = 0xFF,
  J2K_MARKER_SOC = 0x4F,
  J2K_MARKER_SOT = 0x90,
  J2K_MARKER_EOC = 0xD9
} RtpJ2KMarker;

enum
{
  PROP_0,
  PROP_LAST
};

/*
 * RtpJ2KHeader:
 * @tp: type (0 progressive, 1 odd field, 2 even field)
 * @MHF: Main Header Flag
 * @mh_id: Main Header Identification
 * @T: Tile field invalidation flag
 * @priority: priority
 * @tile number: the tile number of the payload
 * @reserved: set to 0
 * @fragment offset: the byte offset of the current payload
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |tp |MHF|mh_id|T|     priority  |           tile number         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |reserved       |             fragment offset                   |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
typedef struct
{
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  guint T:1;
  guint mh_id:3;
  guint MHF:2;
  guint tp:2;
#elif G_BYTE_ORDER == G_BIG_ENDIAN
  guint tp:2;
  guint MHF:2;
  guint mh_id:3;
  guint T:1;
#else
#error "G_BYTE_ORDER should be big or little endian."
#endif
  guint priority:8;
  guint tile:16;
  guint reserved:8;
  guint offset:24;
} RtpJ2KHeader;

static gboolean gst_rtp_j2k_pay_setcaps (GstBaseRTPPayload * basepayload,
    GstCaps * caps);

static GstFlowReturn gst_rtp_j2k_pay_handle_buffer (GstBaseRTPPayload * pad,
    GstBuffer * buffer);

GST_BOILERPLATE (GstRtpJ2KPay, gst_rtp_j2k_pay, GstBaseRTPPayload,
    GST_TYPE_BASE_RTP_PAYLOAD);

static void
gst_rtp_j2k_pay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_j2k_pay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_j2k_pay_sink_template));

  gst_element_class_set_details_simple (element_class,
      "RTP JPEG 2000 payloader", "Codec/Payloader/Network",
      "Payload-encodes JPEG 2000 pictures into RTP packets (RFC 5371)",
      "Wim Taymans <wim.taymans@gmail.com>");
}

static void
gst_rtp_j2k_pay_class_init (GstRtpJ2KPayClass * klass)
{
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  gstbasertppayload_class->set_caps = gst_rtp_j2k_pay_setcaps;
  gstbasertppayload_class->handle_buffer = gst_rtp_j2k_pay_handle_buffer;

  GST_DEBUG_CATEGORY_INIT (rtpj2kpay_debug, "rtpj2kpay", 0,
      "JPEG 2000 RTP Payloader");
}

static void
gst_rtp_j2k_pay_init (GstRtpJ2KPay * pay, GstRtpJ2KPayClass * klass)
{
}

static gboolean
gst_rtp_j2k_pay_setcaps (GstBaseRTPPayload * basepayload, GstCaps * caps)
{
  GstStructure *caps_structure = gst_caps_get_structure (caps, 0);
  GstRtpJ2KPay *pay;
  gint width = 0, height = 0;
  gboolean res;

  pay = GST_RTP_J2K_PAY (basepayload);

  /* these properties are not mandatory, we can get them from the stream */
  if (gst_structure_get_int (caps_structure, "height", &height)) {
    pay->height = height;
  }
  if (gst_structure_get_int (caps_structure, "width", &width)) {
    pay->width = width;
  }

  gst_basertppayload_set_options (basepayload, "video", TRUE, "JPEG2000",
      90000);
  res = gst_basertppayload_set_outcaps (basepayload, NULL);

  return res;
}


static guint
gst_rtp_j2k_pay_header_size (const guint8 * data, guint offset)
{
  return data[offset] << 8 | data[offset + 1];
}

static RtpJ2KMarker
gst_rtp_j2k_pay_scan_marker (const guint8 * data, guint size, guint * offset)
{
  while ((data[(*offset)++] != J2K_MARKER) && ((*offset) < size));

  if (G_UNLIKELY ((*offset) >= size)) {
    GST_LOG ("end of data, return EOC");
    return J2K_MARKER_EOC;
  } else {
    guint8 marker = data[(*offset)++];
    GST_LOG ("found %02x marker", marker);
    return marker;
  }
}

static guint
find_pu_end (GstRtpJ2KPay * pay, const guint8 * data, guint size,
    guint offset, RtpJ2KHeader * header)
{
  /* parse the j2k header for 'start of codestream' */
  while (offset < size) {
    GST_LOG_OBJECT (pay, "checking from offset %u", offset);
    switch (gst_rtp_j2k_pay_scan_marker (data, size, &offset)) {
      case J2K_MARKER_SOC:
        GST_DEBUG_OBJECT (pay, "found SOC at %u", offset);
        header->MHF = 1;
        break;
      case J2K_MARKER_SOT:
      {
        guint len, Psot;

        GST_DEBUG_OBJECT (pay, "found SOT at %u", offset);
        /* we found SOT but also had a header first */
        if (header->MHF)
          return offset - 2;

        /* parse SOT but do some sanity checks first */
        len = gst_rtp_j2k_pay_header_size (data, offset);
        GST_DEBUG_OBJECT (pay, "SOT length %u", len);
        if (len < 8)
          return size;
        if (offset + len >= size)
          return size;

        /* we have a valid tile number now, keep it in the header */
        header->T = 0;
        header->tile = GST_READ_UINT16_BE (&data[offset + 2]);

        /* get offset of next tile, if it's 0, it goes all the way to the end of
         * the data */
        Psot = GST_READ_UINT32_BE (&data[offset + 4]);
        if (Psot == 0)
          offset = size;
        else
          offset += Psot;
        GST_DEBUG_OBJECT (pay, "Isot %u, Psot %u", header->tile, Psot);
        break;
      }
      case J2K_MARKER_EOC:
        GST_DEBUG_OBJECT (pay, "found EOC");
        return offset;
      default:
        offset += gst_rtp_j2k_pay_header_size (data, offset);
        break;
    }
  }
  GST_DEBUG_OBJECT (pay, "reached end of data");
  return size;
}

static GstFlowReturn
gst_rtp_j2k_pay_handle_buffer (GstBaseRTPPayload * basepayload,
    GstBuffer * buffer)
{
  GstRtpJ2KPay *pay;
  GstClockTime timestamp;
  GstFlowReturn ret = GST_FLOW_ERROR;
  RtpJ2KHeader j2k_header;
  guint8 *data;
  guint size;
  guint mtu;
  guint offset;

  pay = GST_RTP_J2K_PAY (basepayload);
  mtu = GST_BASE_RTP_PAYLOAD_MTU (pay);

  size = GST_BUFFER_SIZE (buffer);
  data = GST_BUFFER_DATA (buffer);
  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  offset = 0;

  GST_LOG_OBJECT (pay, "got buffer size %u, timestamp %" GST_TIME_FORMAT, size,
      GST_TIME_ARGS (timestamp));

  /* do some header defaults first */
  j2k_header.tp = 0;            /* only progressive scan */
  j2k_header.MHF = 0;           /* no header */
  j2k_header.mh_id = 0;         /* always 0 for now */
  j2k_header.T = 1;             /* invalid tile */
  j2k_header.priority = 255;    /* always 255 for now */
  j2k_header.tile = 0;          /* no tile number */
  j2k_header.reserved = 0;

  do {
    GstBuffer *outbuf;
    guint8 *payload, *header;
    guint payload_size;
    guint pu_size, end;

    /* scan next packetization unit and fill in the header */
    end = find_pu_end (pay, data, size, offset, &j2k_header);
    pu_size = end - offset;

    GST_DEBUG_OBJECT (pay, "pu of size %u", pu_size);

    while (pu_size > 0) {
      guint packet_size;

      /* calculate the packet size */
      packet_size =
          gst_rtp_buffer_calc_packet_len (pu_size + sizeof (j2k_header), 0, 0);

      GST_DEBUG_OBJECT (pay, "needed packet size %u", packet_size);

      /* make sure it fits the MTU */
      packet_size = (packet_size < mtu ? packet_size : mtu);
      outbuf = gst_rtp_buffer_new_allocate_len (packet_size, 0, 0);
      GST_BUFFER_TIMESTAMP (outbuf) = timestamp;

      /* get pointer to header and size of the payload */
      header = gst_rtp_buffer_get_payload (outbuf);
      payload_size = gst_rtp_buffer_get_payload_len (outbuf);

      /* skip header and move to the payload */
      payload = header + sizeof (j2k_header);
      payload_size -= sizeof (j2k_header);

      /* copy payload */
      memcpy (payload, &data[offset], payload_size);

      pu_size -= payload_size;
      if (pu_size == 0) {
        /* reached the end of a packetization unit */
        if (j2k_header.MHF) {
          /* we were doing a header, see if all fit in one packet or if
           * we had to fragment it */
          if (offset == 0)
            j2k_header.MHF = 3;
          else
            j2k_header.MHF = 2;
        }
        if (end >= size)
          gst_rtp_buffer_set_marker (outbuf, TRUE);
      }

      /* copy the header and push the packet */
#if (G_BYTE_ORDER == G_LITTLE_ENDIAN)
      j2k_header.offset = ((offset & 0x0000FF) << 16) |
          ((offset & 0xFF0000) >> 16) | (offset & 0x00FF00);
#else
      j2k_header.offset = offset;
#endif
      memcpy (header, &j2k_header, sizeof (j2k_header));

      ret = gst_basertppayload_push (basepayload, outbuf);
      if (ret != GST_FLOW_OK)
        goto done;

      /* reset header for next round */
      j2k_header.MHF = 0;
      j2k_header.T = 1;
      j2k_header.tile = 0;

      offset += payload_size;
    }
    offset = end;
  } while (offset < size);

done:
  gst_buffer_unref (buffer);

  return ret;
}

gboolean
gst_rtp_j2k_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpj2kpay", GST_RANK_NONE,
      GST_TYPE_RTP_J2K_PAY);
}
