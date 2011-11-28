/* GStreamer
 * Copyright (C) <2008> Wim Taymans <wim.taymans@gmail.com>
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

#include "gstrtpvrawpay.h"

GST_DEBUG_CATEGORY_STATIC (rtpvrawpay_debug);
#define GST_CAT_DEFAULT (rtpvrawpay_debug)

static GstStaticPadTemplate gst_rtp_vraw_pay_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb, "
        "bpp = (int) 24, "
        "depth = (int) 24, "
        "endianness = (int) BIG_ENDIAN, "
        "red_mask =   (int) 0x00FF0000, "
        "green_mask = (int) 0x0000FF00, "
        "blue_mask =  (int) 0x000000FF, "
        "width = (int) [ 1, 32767 ], "
        "height = (int) [ 1, 32767 ]; "
        "video/x-raw-rgb, "
        "bpp = (int) 32, "
        "depth = (int) 32, "
        "endianness = (int) BIG_ENDIAN, "
        "red_mask =   (int) 0xFF000000, "
        "green_mask = (int) 0x00FF0000, "
        "blue_mask =  (int) 0x0000FF00, "
        "alpha_mask = (int) 0x000000FF, "
        "width = (int) [ 1, 32767 ], "
        "height = (int) [ 1, 32767 ]; "
        "video/x-raw-rgb, "
        "bpp = (int) 24, "
        "depth = (int) 24, "
        "endianness = (int) BIG_ENDIAN, "
        "red_mask =   (int) 0x000000FF, "
        "green_mask = (int) 0x0000FF00, "
        "blue_mask =  (int) 0x00FF0000, "
        "width = (int) [ 1, 32767 ], "
        "height = (int) [ 1, 32767 ]; "
        "video/x-raw-rgb, "
        "bpp = (int) 32, "
        "depth = (int) 32, "
        "endianness = (int) BIG_ENDIAN, "
        "red_mask =   (int) 0x0000FF00, "
        "green_mask = (int) 0x00FF0000, "
        "blue_mask =  (int) 0xFF000000, "
        "alpha_mask = (int) 0x000000FF, "
        "width = (int) [ 1, 32767 ], "
        "height = (int) [ 1, 32767 ]; "
        "video/x-raw-yuv, "
        "format = (fourcc) { AYUV, UYVY, I420, Y41B, UYVP }, "
        "width = (int) [ 1, 32767 ], " "height = (int) [ 1, 32767 ]; ")
    );

static GstStaticPadTemplate gst_rtp_vraw_pay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 90000, "
        "encoding-name = (string) \"RAW\","
        "sampling = (string) { \"RGB\", \"RGBA\", \"BGR\", \"BGRA\", "
        "\"YCbCr-4:4:4\", \"YCbCr-4:2:2\", \"YCbCr-4:2:0\", "
        "\"YCbCr-4:1:1\" },"
        /* we cannot express these as strings 
         * "width = (string) [1 32767],"
         * "height = (string) [1 32767],"
         */
        "depth = (string) { \"8\", \"10\", \"12\", \"16\" },"
        "colorimetry = (string) { \"BT601-5\", \"BT709-2\", \"SMPTE240M\" }"
        /* optional 
         * interlace = 
         * top-field-first = 
         * chroma-position = (string) 
         * gamma = (float)
         */
    )
    );

static gboolean gst_rtp_vraw_pay_setcaps (GstBaseRTPPayload * payload,
    GstCaps * caps);
static GstFlowReturn gst_rtp_vraw_pay_handle_buffer (GstBaseRTPPayload *
    payload, GstBuffer * buffer);

GST_BOILERPLATE (GstRtpVRawPay, gst_rtp_vraw_pay, GstBaseRTPPayload,
    GST_TYPE_BASE_RTP_PAYLOAD)

     static void gst_rtp_vraw_pay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_vraw_pay_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_vraw_pay_sink_template);

  gst_element_class_set_details_simple (element_class,
      "RTP Raw Video payloader", "Codec/Payloader/Network/RTP",
      "Payload raw video as RTP packets (RFC 4175)",
      "Wim Taymans <wim.taymans@gmail.com>");
}

static void
gst_rtp_vraw_pay_class_init (GstRtpVRawPayClass * klass)
{
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  gstbasertppayload_class->set_caps = gst_rtp_vraw_pay_setcaps;
  gstbasertppayload_class->handle_buffer = gst_rtp_vraw_pay_handle_buffer;

  GST_DEBUG_CATEGORY_INIT (rtpvrawpay_debug, "rtpvrawpay", 0,
      "Raw video RTP Payloader");
}

static void
gst_rtp_vraw_pay_init (GstRtpVRawPay * rtpvrawpay, GstRtpVRawPayClass * klass)
{
}

static gboolean
gst_rtp_vraw_pay_setcaps (GstBaseRTPPayload * payload, GstCaps * caps)
{
  GstRtpVRawPay *rtpvrawpay;
  GstStructure *s;
  gboolean res;
  const gchar *name;
  gint width, height;
  gint yp, up, vp;
  gint pgroup, ystride, uvstride = 0, xinc, yinc;
  GstVideoFormat sampling;
  const gchar *depthstr, *samplingstr, *colorimetrystr;
  gchar *wstr, *hstr;
  gboolean interlaced;
  const gchar *color_matrix;
  gint depth;

  rtpvrawpay = GST_RTP_VRAW_PAY (payload);

  s = gst_caps_get_structure (caps, 0);

  /* start parsing the format */
  name = gst_structure_get_name (s);

  /* these values are the only thing we can do */
  depthstr = "8";

  /* parse common width/height */
  res = gst_structure_get_int (s, "width", &width);
  res &= gst_structure_get_int (s, "height", &height);
  if (!res)
    goto missing_dimension;

  if (!gst_structure_get_boolean (s, "interlaced", &interlaced))
    interlaced = FALSE;

  color_matrix = gst_structure_get_string (s, "color-matrix");
  colorimetrystr = "SMPTE240M";
  if (color_matrix) {
    if (g_str_equal (color_matrix, "sdtv")) {
      /* BT.601 implies a bit more than just color-matrix */
      colorimetrystr = "BT601-5";
    } else if (g_str_equal (color_matrix, "hdtv")) {
      colorimetrystr = "BT709-2";
    }
  }

  yp = up = vp = 0;
  xinc = yinc = 1;

  if (!strcmp (name, "video/x-raw-rgb")) {
    gint amask, rmask;
    gboolean has_alpha;

    has_alpha = gst_structure_get_int (s, "alpha_mask", &amask);
    depth = 8;

    if (!gst_structure_get_int (s, "red_mask", &rmask))
      goto unknown_mask;

    if (has_alpha) {
      pgroup = 4;
      ystride = width * 4;
      if (rmask == 0xFF000000) {
        sampling = GST_VIDEO_FORMAT_RGBA;
        samplingstr = "RGBA";
      } else {
        sampling = GST_VIDEO_FORMAT_BGRA;
        samplingstr = "BGRA";
      }
    } else {
      pgroup = 3;
      ystride = GST_ROUND_UP_4 (width * 3);
      if (rmask == 0x00FF0000) {
        sampling = GST_VIDEO_FORMAT_RGB;
        samplingstr = "RGB";
      } else {
        sampling = GST_VIDEO_FORMAT_BGR;
        samplingstr = "BGR";
      }
    }
  } else if (!strcmp (name, "video/x-raw-yuv")) {
    guint32 fourcc;

    if (!gst_structure_get_fourcc (s, "format", &fourcc))
      goto unknown_fourcc;

    GST_LOG_OBJECT (payload, "have fourcc %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (fourcc));

    switch (fourcc) {
      case GST_MAKE_FOURCC ('A', 'Y', 'U', 'V'):
        sampling = GST_VIDEO_FORMAT_AYUV;
        samplingstr = "YCbCr-4:4:4";
        pgroup = 3;
        ystride = width * 4;
        depth = 8;
        break;
      case GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'):
        sampling = GST_VIDEO_FORMAT_UYVY;
        samplingstr = "YCbCr-4:2:2";
        pgroup = 4;
        xinc = 2;
        ystride = GST_ROUND_UP_2 (width) * 2;
        depth = 8;
        break;
      case GST_MAKE_FOURCC ('Y', '4', '1', 'B'):
        sampling = GST_VIDEO_FORMAT_Y41B;
        samplingstr = "YCbCr-4:1:1";
        pgroup = 6;
        xinc = 4;
        ystride = GST_ROUND_UP_4 (width);
        uvstride = GST_ROUND_UP_8 (width) / 4;
        up = ystride * height;
        vp = up + uvstride * height;
        depth = 8;
        break;
      case GST_MAKE_FOURCC ('I', '4', '2', '0'):
        sampling = GST_VIDEO_FORMAT_I420;
        samplingstr = "YCbCr-4:2:0";
        pgroup = 6;
        xinc = yinc = 2;
        ystride = GST_ROUND_UP_4 (width);
        uvstride = GST_ROUND_UP_8 (width) / 2;
        up = ystride * GST_ROUND_UP_2 (height);
        vp = up + uvstride * GST_ROUND_UP_2 (height) / 2;
        depth = 8;
        break;
      case GST_MAKE_FOURCC ('U', 'Y', 'V', 'P'):
#define GST_VIDEO_FORMAT_UYVP GST_VIDEO_FORMAT_UYVY     /* FIXME */
        sampling = GST_VIDEO_FORMAT_UYVP;
        samplingstr = "YCbCr-4:2:2";
        pgroup = 4;
        xinc = 2;
        ystride = GST_ROUND_UP_2 (width) * 2;
        depth = 10;
        break;
      default:
        goto unknown_fourcc;
    }
  } else
    goto unknown_format;

  if (interlaced) {
    yinc *= 2;
  }
  if (depth == 10) {
    depthstr = "10";
  }

  rtpvrawpay->width = width;
  rtpvrawpay->height = height;
  rtpvrawpay->sampling = sampling;
  rtpvrawpay->pgroup = pgroup;
  rtpvrawpay->xinc = xinc;
  rtpvrawpay->yinc = yinc;
  rtpvrawpay->yp = yp;
  rtpvrawpay->up = up;
  rtpvrawpay->vp = vp;
  rtpvrawpay->ystride = ystride;
  rtpvrawpay->uvstride = uvstride;
  rtpvrawpay->interlaced = interlaced;
  rtpvrawpay->depth = depth;

  GST_DEBUG_OBJECT (payload, "width %d, height %d, sampling %d", width, height,
      sampling);
  GST_DEBUG_OBJECT (payload, "yp %d, up %d, vp %d", yp, up, vp);
  GST_DEBUG_OBJECT (payload, "pgroup %d, ystride %d, uvstride %d", pgroup,
      ystride, uvstride);

  wstr = g_strdup_printf ("%d", rtpvrawpay->width);
  hstr = g_strdup_printf ("%d", rtpvrawpay->height);

  gst_basertppayload_set_options (payload, "video", TRUE, "RAW", 90000);
  if (interlaced) {
    res = gst_basertppayload_set_outcaps (payload, "sampling", G_TYPE_STRING,
        samplingstr, "depth", G_TYPE_STRING, depthstr, "width", G_TYPE_STRING,
        wstr, "height", G_TYPE_STRING, hstr, "colorimetry", G_TYPE_STRING,
        colorimetrystr, "interlace", G_TYPE_STRING, "true", NULL);
  } else {
    res = gst_basertppayload_set_outcaps (payload, "sampling", G_TYPE_STRING,
        samplingstr, "depth", G_TYPE_STRING, depthstr, "width", G_TYPE_STRING,
        wstr, "height", G_TYPE_STRING, hstr, "colorimetry", G_TYPE_STRING,
        colorimetrystr, NULL);
  }
  g_free (wstr);
  g_free (hstr);

  return res;

  /* ERRORS */
unknown_mask:
  {
    GST_ERROR_OBJECT (payload, "unknown red mask specified");
    return FALSE;
  }
unknown_format:
  {
    GST_ERROR_OBJECT (payload, "unknown caps format");
    return FALSE;
  }
unknown_fourcc:
  {
    GST_ERROR_OBJECT (payload, "invalid or missing fourcc");
    return FALSE;
  }
missing_dimension:
  {
    GST_ERROR_OBJECT (payload, "missing width or height property");
    return FALSE;
  }
}

static GstFlowReturn
gst_rtp_vraw_pay_handle_buffer (GstBaseRTPPayload * payload, GstBuffer * buffer)
{
  GstRtpVRawPay *rtpvrawpay;
  GstFlowReturn ret = GST_FLOW_OK;
  guint line, offset;
  guint8 *data, *yp, *up, *vp;
  guint ystride, uvstride;
  guint size, pgroup;
  guint mtu;
  guint width, height;
  gint field;

  rtpvrawpay = GST_RTP_VRAW_PAY (payload);

  data = GST_BUFFER_DATA (buffer);
  size = GST_BUFFER_SIZE (buffer);

  GST_LOG_OBJECT (rtpvrawpay, "new frame of %u bytes", size);

  /* get pointer and strides of the planes */
  yp = data + rtpvrawpay->yp;
  up = data + rtpvrawpay->up;
  vp = data + rtpvrawpay->vp;

  ystride = rtpvrawpay->ystride;
  uvstride = rtpvrawpay->uvstride;

  mtu = GST_BASE_RTP_PAYLOAD_MTU (payload);

  /* amount of bytes for one pixel */
  pgroup = rtpvrawpay->pgroup;
  width = rtpvrawpay->width;
  height = rtpvrawpay->height;

  /* start with line 0, offset 0 */

  for (field = 0; field < 1 + rtpvrawpay->interlaced; field++) {
    line = field;
    offset = 0;

    /* write all lines */
    while (line < height) {
      guint left;
      GstBuffer *out;
      guint8 *outdata, *headers;
      gboolean next_line;
      guint length, cont, pixels;

      /* get the max allowed payload length size, we try to fill the complete MTU */
      left = gst_rtp_buffer_calc_payload_len (mtu, 0, 0);
      out = gst_rtp_buffer_new_allocate (left, 0, 0);

      if (field == 0) {
        GST_BUFFER_TIMESTAMP (out) = GST_BUFFER_TIMESTAMP (buffer);
      } else {
        GST_BUFFER_TIMESTAMP (out) = GST_BUFFER_TIMESTAMP (buffer) +
            GST_BUFFER_DURATION (buffer) / 2;
      }

      outdata = gst_rtp_buffer_get_payload (out);

      GST_LOG_OBJECT (rtpvrawpay, "created buffer of size %u for MTU %u", left,
          mtu);

      /*
       *   0                   1                   2                   3
       *   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
       *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       *  |   Extended Sequence Number    |            Length             |
       *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       *  |F|          Line No            |C|           Offset            |
       *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       *  |            Length             |F|          Line No            |
       *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       *  |C|           Offset            |                               .
       *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                               .
       *  .                                                               .
       *  .                 Two (partial) lines of video data             .
       *  .                                                               .
       *  +---------------------------------------------------------------+
       */

      /* need 2 bytes for the extended sequence number */
      *outdata++ = 0;
      *outdata++ = 0;
      left -= 2;

      /* the headers start here */
      headers = outdata;

      /* while we can fit at least one header and one pixel */
      while (left > (6 + pgroup)) {
        /* we need a 6 bytes header */
        left -= 6;

        /* get how may bytes we need for the remaining pixels */
        pixels = width - offset;
        length = (pixels * pgroup) / rtpvrawpay->xinc;

        if (left >= length) {
          /* pixels and header fit completely, we will write them and skip to the
           * next line. */
          next_line = TRUE;
        } else {
          /* line does not fit completely, see how many pixels fit */
          pixels = (left / pgroup) * rtpvrawpay->xinc;
          length = (pixels * pgroup) / rtpvrawpay->xinc;
          next_line = FALSE;
        }
        GST_LOG_OBJECT (rtpvrawpay, "filling %u bytes in %u pixels", length,
            pixels);
        left -= length;

        /* write length */
        *outdata++ = (length >> 8) & 0xff;
        *outdata++ = length & 0xff;

        /* write line no */
        *outdata++ = ((line >> 8) & 0x7f) | ((field << 7) & 0x80);
        *outdata++ = line & 0xff;

        if (next_line) {
          /* go to next line we do this here to make the check below easier */
          line += rtpvrawpay->yinc;
        }

        /* calculate continuation marker */
        cont = (left > (6 + pgroup) && line < height) ? 0x80 : 0x00;

        /* write offset and continuation marker */
        *outdata++ = ((offset >> 8) & 0x7f) | cont;
        *outdata++ = offset & 0xff;

        if (next_line) {
          /* reset offset */
          offset = 0;
          GST_LOG_OBJECT (rtpvrawpay, "go to next line %u", line);
        } else {
          offset += pixels;
          GST_LOG_OBJECT (rtpvrawpay, "next offset %u", offset);
        }

        if (!cont)
          break;
      }
      GST_LOG_OBJECT (rtpvrawpay, "consumed %u bytes",
          (guint) (outdata - headers));

      /* second pass, read headers and write the data */
      while (TRUE) {
        guint offs, lin;

        /* read length and cont */
        length = (headers[0] << 8) | headers[1];
        lin = ((headers[2] & 0x7f) << 8) | headers[3];
        offs = ((headers[4] & 0x7f) << 8) | headers[5];
        cont = headers[4] & 0x80;
        pixels = length / pgroup;
        headers += 6;

        GST_LOG_OBJECT (payload,
            "writing length %u, line %u, offset %u, cont %d", length, lin, offs,
            cont);

        switch (rtpvrawpay->sampling) {
          case GST_VIDEO_FORMAT_RGB:
          case GST_VIDEO_FORMAT_RGBA:
          case GST_VIDEO_FORMAT_BGR:
          case GST_VIDEO_FORMAT_BGRA:
          case GST_VIDEO_FORMAT_UYVY:
            offs /= rtpvrawpay->xinc;
            memcpy (outdata, yp + (lin * ystride) + (offs * pgroup), length);
            outdata += length;
            break;
          case GST_VIDEO_FORMAT_AYUV:
          {
            gint i;
            guint8 *datap;

            datap = yp + (lin * ystride) + (offs * 4);

            for (i = 0; i < pixels; i++) {
              *outdata++ = datap[2];
              *outdata++ = datap[1];
              *outdata++ = datap[3];
              datap += 4;
            }
            break;
          }
          case GST_VIDEO_FORMAT_I420:
          {
            gint i;
            guint uvoff;
            guint8 *yd1p, *yd2p, *udp, *vdp;

            yd1p = yp + (lin * ystride) + (offs);
            yd2p = yd1p + ystride;
            uvoff =
                (lin / rtpvrawpay->yinc * uvstride) + (offs / rtpvrawpay->xinc);
            udp = up + uvoff;
            vdp = vp + uvoff;

            for (i = 0; i < pixels; i++) {
              *outdata++ = *yd1p++;
              *outdata++ = *yd1p++;
              *outdata++ = *yd2p++;
              *outdata++ = *yd2p++;
              *outdata++ = *udp++;
              *outdata++ = *vdp++;
            }
            break;
          }
          case GST_VIDEO_FORMAT_Y41B:
          {
            gint i;
            guint uvoff;
            guint8 *ydp, *udp, *vdp;

            ydp = yp + (lin * ystride) + offs;
            uvoff =
                (lin / rtpvrawpay->yinc * uvstride) + (offs / rtpvrawpay->xinc);
            udp = up + uvoff;
            vdp = vp + uvoff;

            for (i = 0; i < pixels; i++) {
              *outdata++ = *udp++;
              *outdata++ = *ydp++;
              *outdata++ = *ydp++;
              *outdata++ = *vdp++;
              *outdata++ = *ydp++;
              *outdata++ = *ydp++;
            }
            break;
          }
          default:
            gst_buffer_unref (out);
            goto unknown_sampling;
        }

        if (!cont)
          break;
      }

      if (line >= height) {
        GST_LOG_OBJECT (rtpvrawpay, "field/frame complete, set marker");
        gst_rtp_buffer_set_marker (out, TRUE);
      }
      if (left > 0) {
        GST_LOG_OBJECT (rtpvrawpay, "we have %u bytes left", left);
        GST_BUFFER_SIZE (out) -= left;
      }

      /* push buffer */
      ret = gst_basertppayload_push (payload, out);
    }

  }
  gst_buffer_unref (buffer);

  return ret;

  /* ERRORS */
unknown_sampling:
  {
    GST_ELEMENT_ERROR (payload, STREAM, FORMAT,
        (NULL), ("unimplemented sampling"));
    gst_buffer_unref (buffer);
    return GST_FLOW_NOT_SUPPORTED;
  }
}

gboolean
gst_rtp_vraw_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpvrawpay",
      GST_RANK_SECONDARY, GST_TYPE_RTP_VRAW_PAY);
}
