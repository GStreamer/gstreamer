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

#include <gst/rtp/gstrtpbuffer.h>

#include <string.h>
#include <stdlib.h>
#include "gstrtpvrawdepay.h"

GST_DEBUG_CATEGORY_STATIC (rtpvrawdepay_debug);
#define GST_CAT_DEFAULT (rtpvrawdepay_debug)

static GstStaticPadTemplate gst_rtp_vraw_depay_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb; video/x-raw-yuv")
    );

static GstStaticPadTemplate gst_rtp_vraw_depay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 90000, " "encoding-name = (string) \"RAW\"")
    );

GST_BOILERPLATE (GstRtpVRawDepay, gst_rtp_vraw_depay, GstBaseRTPDepayload,
    GST_TYPE_BASE_RTP_DEPAYLOAD);

static gboolean gst_rtp_vraw_depay_setcaps (GstBaseRTPDepayload * depayload,
    GstCaps * caps);
static GstBuffer *gst_rtp_vraw_depay_process (GstBaseRTPDepayload * depayload,
    GstBuffer * buf);

static GstStateChangeReturn gst_rtp_vraw_depay_change_state (GstElement *
    element, GstStateChange transition);

static gboolean gst_rtp_vraw_depay_handle_event (GstBaseRTPDepayload * filter,
    GstEvent * event);

static void
gst_rtp_vraw_depay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_vraw_depay_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_vraw_depay_sink_template);

  gst_element_class_set_details_simple (element_class,
      "RTP Raw Video depayloader", "Codec/Depayloader/Network/RTP",
      "Extracts raw video from RTP packets (RFC 4175)",
      "Wim Taymans <wim.taymans@gmail.com>");
}

static void
gst_rtp_vraw_depay_class_init (GstRtpVRawDepayClass * klass)
{
  GstElementClass *gstelement_class;
  GstBaseRTPDepayloadClass *gstbasertpdepayload_class;

  gstelement_class = (GstElementClass *) klass;
  gstbasertpdepayload_class = (GstBaseRTPDepayloadClass *) klass;

  gstelement_class->change_state = gst_rtp_vraw_depay_change_state;

  gstbasertpdepayload_class->set_caps = gst_rtp_vraw_depay_setcaps;
  gstbasertpdepayload_class->process = gst_rtp_vraw_depay_process;
  gstbasertpdepayload_class->handle_event = gst_rtp_vraw_depay_handle_event;

  GST_DEBUG_CATEGORY_INIT (rtpvrawdepay_debug, "rtpvrawdepay", 0,
      "raw video RTP Depayloader");
}

static void
gst_rtp_vraw_depay_init (GstRtpVRawDepay * rtpvrawdepay,
    GstRtpVRawDepayClass * klass)
{
  /* needed because of GST_BOILERPLATE */
}

static void
gst_rtp_vraw_depay_reset (GstRtpVRawDepay * rtpvrawdepay)
{
  if (rtpvrawdepay->outbuf) {
    gst_buffer_unref (rtpvrawdepay->outbuf);
    rtpvrawdepay->outbuf = NULL;
  }
  rtpvrawdepay->timestamp = -1;
}

static gboolean
gst_rtp_vraw_depay_setcaps (GstBaseRTPDepayload * depayload, GstCaps * caps)
{
  GstStructure *structure;
  GstRtpVRawDepay *rtpvrawdepay;
  gint clock_rate;
  const gchar *str, *type;
  gint format, width, height, pgroup, xinc, yinc;
  guint ystride, uvstride, yp, up, vp, outsize;
  GstCaps *srccaps;
  guint32 fourcc = 0;
  gboolean res;
  gint rmask = 0, gmask = 0, bmask = 0, amask = 0, bpp = 0, depth = 0;

  rtpvrawdepay = GST_RTP_VRAW_DEPAY (depayload);

  structure = gst_caps_get_structure (caps, 0);

  yp = up = vp = uvstride = 0;
  xinc = yinc = 1;

  if (!gst_structure_get_int (structure, "clock-rate", &clock_rate))
    clock_rate = 90000;         /* default */
  depayload->clock_rate = clock_rate;

  if (!(str = gst_structure_get_string (structure, "width")))
    goto no_width;
  width = atoi (str);

  if (!(str = gst_structure_get_string (structure, "height")))
    goto no_height;
  height = atoi (str);

  /* optional interlace value but we don't handle interlaced
   * formats yet */
  if (gst_structure_get_string (structure, "interlace"))
    goto interlaced;

  if (!(str = gst_structure_get_string (structure, "sampling")))
    goto no_sampling;

  if (!strcmp (str, "RGB")) {
    format = GST_VIDEO_FORMAT_RGB;
    pgroup = 3;
    ystride = GST_ROUND_UP_4 (width * 3);
    outsize = ystride * height;
    type = "video/x-raw-rgb";
    rmask = 0x00ff0000;
    gmask = 0x0000ff00;
    bmask = 0x000000ff;
    depth = 24;
    bpp = 24;
  } else if (!strcmp (str, "RGBA")) {
    format = GST_VIDEO_FORMAT_RGBA;
    pgroup = 4;
    ystride = width * 4;
    outsize = ystride * height;
    type = "video/x-raw-rgb";
    rmask = 0xff000000;
    gmask = 0x00ff0000;
    bmask = 0x0000ff00;
    amask = 0x000000ff;
    depth = 32;
    bpp = 32;
  } else if (!strcmp (str, "BGR")) {
    format = GST_VIDEO_FORMAT_BGR;
    pgroup = 3;
    ystride = GST_ROUND_UP_4 (width * 3);
    outsize = ystride * height;
    type = "video/x-raw-rgb";
    rmask = 0x000000ff;
    gmask = 0x0000ff00;
    bmask = 0x00ff0000;
    depth = 24;
    bpp = 24;
  } else if (!strcmp (str, "BGRA")) {
    format = GST_VIDEO_FORMAT_BGRA;
    pgroup = 4;
    ystride = width * 4;
    outsize = ystride * height;
    type = "video/x-raw-rgb";
    rmask = 0x0000ff00;
    gmask = 0x00ff0000;
    bmask = 0xff000000;
    amask = 0x000000ff;
    depth = 32;
    bpp = 32;
  } else if (!strcmp (str, "YCbCr-4:4:4")) {
    format = GST_VIDEO_FORMAT_AYUV;
    pgroup = 3;
    ystride = width * 4;
    outsize = ystride * height;
    type = "video/x-raw-yuv";
    fourcc = GST_MAKE_FOURCC ('A', 'Y', 'U', 'V');
  } else if (!strcmp (str, "YCbCr-4:2:2")) {
    format = GST_VIDEO_FORMAT_UYVY;
    pgroup = 4;
    ystride = GST_ROUND_UP_2 (width) * 2;
    outsize = ystride * height;
    type = "video/x-raw-yuv";
    fourcc = GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y');
    xinc = 2;
  } else if (!strcmp (str, "YCbCr-4:2:0")) {
    format = GST_VIDEO_FORMAT_I420;
    pgroup = 6;
    ystride = GST_ROUND_UP_4 (width);
    uvstride = GST_ROUND_UP_8 (width) / 2;
    up = ystride * GST_ROUND_UP_2 (height);
    vp = up + uvstride * GST_ROUND_UP_2 (height) / 2;
    outsize = vp + uvstride * GST_ROUND_UP_2 (height) / 2;
    type = "video/x-raw-yuv";
    fourcc = GST_MAKE_FOURCC ('I', '4', '2', '0');
    xinc = yinc = 2;
  } else if (!strcmp (str, "YCbCr-4:1:1")) {
    format = GST_VIDEO_FORMAT_Y41B;
    pgroup = 6;
    ystride = GST_ROUND_UP_4 (width);
    uvstride = GST_ROUND_UP_8 (width) / 4;
    up = ystride * height;
    vp = up + uvstride * height;
    outsize = vp + uvstride * height;
    type = "video/x-raw-yuv";
    fourcc = GST_MAKE_FOURCC ('Y', '4', '1', 'B');
    xinc = 4;
  } else
    goto unknown_format;

  rtpvrawdepay->width = width;
  rtpvrawdepay->height = height;
  rtpvrawdepay->format = format;
  rtpvrawdepay->yp = yp;
  rtpvrawdepay->up = up;
  rtpvrawdepay->vp = vp;
  rtpvrawdepay->pgroup = pgroup;
  rtpvrawdepay->xinc = xinc;
  rtpvrawdepay->yinc = yinc;
  rtpvrawdepay->ystride = ystride;
  rtpvrawdepay->uvstride = uvstride;
  rtpvrawdepay->outsize = outsize;

  srccaps = gst_caps_new_simple (type,
      "width", G_TYPE_INT, width,
      "height", G_TYPE_INT, height,
      "format", GST_TYPE_FOURCC, fourcc,
      "framerate", GST_TYPE_FRACTION, 0, 1, NULL);

  if (!strcmp (type, "video/x-raw-rgb")) {
    gst_caps_set_simple (srccaps,
        "endianness", G_TYPE_INT, G_BIG_ENDIAN,
        "red_mask", G_TYPE_INT, rmask,
        "green_mask", G_TYPE_INT, gmask,
        "blue_mask", G_TYPE_INT, bmask,
        "bpp", G_TYPE_INT, bpp, "depth", G_TYPE_INT, depth, NULL);

    if (amask > 0) {
      gst_caps_set_simple (srccaps, "alpha_mask", G_TYPE_INT, amask, NULL);
    }
  }

  res = gst_pad_set_caps (GST_BASE_RTP_DEPAYLOAD_SRCPAD (depayload), srccaps);
  gst_caps_unref (srccaps);

  GST_DEBUG_OBJECT (depayload, "width %d, height %d, format %d", width, height,
      format);
  GST_DEBUG_OBJECT (depayload, "yp %d, up %d, vp %d", yp, up, vp);
  GST_DEBUG_OBJECT (depayload, "xinc %d, yinc %d", xinc, yinc);
  GST_DEBUG_OBJECT (depayload, "pgroup %d, ystride %d, uvstride %d", pgroup,
      ystride, uvstride);
  GST_DEBUG_OBJECT (depayload, "outsize %u", outsize);

  return res;

  /* ERRORS */
no_width:
  {
    GST_ERROR_OBJECT (depayload, "no width specified");
    return FALSE;
  }
no_height:
  {
    GST_ERROR_OBJECT (depayload, "no height specified");
    return FALSE;
  }
interlaced:
  {
    GST_ERROR_OBJECT (depayload, "interlaced formats not supported yet");
    return FALSE;
  }
no_sampling:
  {
    GST_ERROR_OBJECT (depayload, "no sampling specified");
    return FALSE;
  }
unknown_format:
  {
    GST_ERROR_OBJECT (depayload, "unknown sampling format '%s'", str);
    return FALSE;
  }
}

static GstBuffer *
gst_rtp_vraw_depay_process (GstBaseRTPDepayload * depayload, GstBuffer * buf)
{
  GstRtpVRawDepay *rtpvrawdepay;
  guint8 *payload, *data, *yp, *up, *vp, *headers;
  guint32 timestamp;
  guint cont, ystride, uvstride, pgroup, payload_len;
  gint width, height, xinc, yinc;

  rtpvrawdepay = GST_RTP_VRAW_DEPAY (depayload);

  timestamp = gst_rtp_buffer_get_timestamp (buf);

  if (timestamp != rtpvrawdepay->timestamp || rtpvrawdepay->outbuf == NULL) {
    GstBuffer *outbuf;
    GstFlowReturn ret;

    GST_LOG_OBJECT (depayload, "new frame with timestamp %u", timestamp);
    /* new timestamp, flush old buffer and create new output buffer */
    if (rtpvrawdepay->outbuf) {
      gst_base_rtp_depayload_push_ts (depayload, rtpvrawdepay->timestamp,
          rtpvrawdepay->outbuf);
      rtpvrawdepay->outbuf = NULL;
    }

    ret = gst_pad_alloc_buffer (depayload->srcpad, -1, rtpvrawdepay->outsize,
        GST_PAD_CAPS (depayload->srcpad), &outbuf);
    if (ret != GST_FLOW_OK)
      goto alloc_failed;

    /* clear timestamp from alloc... */
    GST_BUFFER_TIMESTAMP (outbuf) = -1;

    rtpvrawdepay->outbuf = outbuf;
    rtpvrawdepay->timestamp = timestamp;
  }

  data = GST_BUFFER_DATA (rtpvrawdepay->outbuf);

  /* get pointer and strides of the planes */
  yp = data + rtpvrawdepay->yp;
  up = data + rtpvrawdepay->up;
  vp = data + rtpvrawdepay->vp;

  ystride = rtpvrawdepay->ystride;
  uvstride = rtpvrawdepay->uvstride;
  pgroup = rtpvrawdepay->pgroup;
  width = rtpvrawdepay->width;
  height = rtpvrawdepay->height;
  xinc = rtpvrawdepay->xinc;
  yinc = rtpvrawdepay->yinc;

  payload = gst_rtp_buffer_get_payload (buf);
  payload_len = gst_rtp_buffer_get_payload_len (buf);

  if (payload_len < 3)
    goto short_packet;

  /* skip extended seqnum */
  payload += 2;
  payload_len -= 2;

  /* remember header position */
  headers = payload;

  /* find data start */
  do {
    if (payload_len < 6)
      goto short_packet;

    cont = payload[4] & 0x80;

    payload += 6;
    payload_len -= 6;
  } while (cont);

  while (TRUE) {
    guint length, line, offs, plen;
    guint8 *datap;

    /* stop when we run out of data */
    if (payload_len == 0)
      break;

    /* read length and cont. This should work because we iterated the headers
     * above. */
    length = (headers[0] << 8) | headers[1];
    line = ((headers[2] & 0x7f) << 8) | headers[3];
    offs = ((headers[4] & 0x7f) << 8) | headers[5];
    cont = headers[4] & 0x80;
    headers += 6;

    /* length must be a multiple of pgroup */
    if (length % pgroup != 0)
      goto wrong_length;

    if (length > payload_len)
      length = payload_len;

    /* sanity check */
    if (line > (height - yinc)) {
      GST_WARNING_OBJECT (depayload, "skipping line %d: out of range", line);
      goto next;
    }
    if (offs > (width - xinc)) {
      GST_WARNING_OBJECT (depayload, "skipping offset %d: out of range", offs);
      goto next;
    }

    /* calculate the maximim amount of bytes we can use per line */
    if (offs + ((length / pgroup) * xinc) > width) {
      plen = ((width - offs) * pgroup) / xinc;
      GST_WARNING_OBJECT (depayload, "clipping length %d, offset %d, plen %d",
          length, offs, plen);
    } else
      plen = length;

    GST_LOG_OBJECT (depayload,
        "writing length %u/%u, line %u, offset %u, remaining %u", plen, length,
        line, offs, payload_len);

    switch (rtpvrawdepay->format) {
      case GST_VIDEO_FORMAT_RGB:
      case GST_VIDEO_FORMAT_RGBA:
      case GST_VIDEO_FORMAT_BGR:
      case GST_VIDEO_FORMAT_BGRA:
      case GST_VIDEO_FORMAT_UYVY:
        /* samples are packed just like gstreamer packs them */
        offs /= xinc;
        datap = yp + (line * ystride) + (offs * pgroup);

        memcpy (datap, payload, plen);
        break;
      case GST_VIDEO_FORMAT_AYUV:
      {
        gint i;
        guint8 *p;

        datap = yp + (line * ystride) + (offs * 4);
        p = payload;

        /* samples are packed in order Cb-Y-Cr for both interlaced and
         * progressive frames */
        for (i = 0; i < plen; i += pgroup) {
          *datap++ = 0;
          *datap++ = p[1];
          *datap++ = p[0];
          *datap++ = p[2];
          p += pgroup;
        }
        break;
      }
      case GST_VIDEO_FORMAT_I420:
      {
        gint i;
        guint uvoff;
        guint8 *yd1p, *yd2p, *udp, *vdp, *p;

        yd1p = yp + (line * ystride) + (offs);
        yd2p = yd1p + ystride;
        uvoff = (line / yinc * uvstride) + (offs / xinc);

        udp = up + uvoff;
        vdp = vp + uvoff;
        p = payload;

        /* line 0/1: Y00-Y01-Y10-Y11-Cb00-Cr00 Y02-Y03-Y12-Y13-Cb01-Cr01 ...  */
        for (i = 0; i < plen; i += pgroup) {
          *yd1p++ = p[0];
          *yd1p++ = p[1];
          *yd2p++ = p[2];
          *yd2p++ = p[3];
          *udp++ = p[4];
          *vdp++ = p[5];
          p += pgroup;
        }
        break;
      }
      case GST_VIDEO_FORMAT_Y41B:
      {
        gint i;
        guint uvoff;
        guint8 *ydp, *udp, *vdp, *p;

        ydp = yp + (line * ystride) + (offs);
        uvoff = (line / yinc * uvstride) + (offs / xinc);

        udp = up + uvoff;
        vdp = vp + uvoff;
        p = payload;

        /* Samples are packed in order Cb0-Y0-Y1-Cr0-Y2-Y3 for both interlaced
         * and progressive scan lines */
        for (i = 0; i < plen; i += pgroup) {
          *udp++ = p[0];
          *ydp++ = p[1];
          *ydp++ = p[2];
          *vdp++ = p[3];
          *ydp++ = p[4];
          *ydp++ = p[5];
          p += pgroup;
        }
        break;
      }
      default:
        goto unknown_sampling;
    }

  next:
    if (!cont)
      break;

    payload += length;
    payload_len -= length;
  }

  if (gst_rtp_buffer_get_marker (buf)) {
    GST_LOG_OBJECT (depayload, "marker, flushing frame");
    if (rtpvrawdepay->outbuf) {
      gst_base_rtp_depayload_push_ts (depayload, timestamp,
          rtpvrawdepay->outbuf);
      rtpvrawdepay->outbuf = NULL;
    }
    rtpvrawdepay->timestamp = -1;
  }
  return NULL;

  /* ERRORS */
unknown_sampling:
  {
    GST_ELEMENT_ERROR (depayload, STREAM, FORMAT,
        (NULL), ("unimplemented sampling"));
    return NULL;
  }
alloc_failed:
  {
    GST_WARNING_OBJECT (depayload, "failed to alloc output buffer");
    return NULL;
  }
wrong_length:
  {
    GST_WARNING_OBJECT (depayload, "length not multiple of pgroup");
    return NULL;
  }
short_packet:
  {
    GST_WARNING_OBJECT (depayload, "short packet");
    return NULL;
  }
}

static gboolean
gst_rtp_vraw_depay_handle_event (GstBaseRTPDepayload * filter, GstEvent * event)
{
  gboolean ret;
  GstRtpVRawDepay *rtpvrawdepay;

  rtpvrawdepay = GST_RTP_VRAW_DEPAY (filter);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      gst_rtp_vraw_depay_reset (rtpvrawdepay);
      break;
    default:
      break;
  }

  ret =
      GST_BASE_RTP_DEPAYLOAD_CLASS (parent_class)->handle_event (filter, event);

  return ret;
}

static GstStateChangeReturn
gst_rtp_vraw_depay_change_state (GstElement * element,
    GstStateChange transition)
{
  GstRtpVRawDepay *rtpvrawdepay;
  GstStateChangeReturn ret;

  rtpvrawdepay = GST_RTP_VRAW_DEPAY (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_rtp_vraw_depay_reset (rtpvrawdepay);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_rtp_vraw_depay_reset (rtpvrawdepay);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }
  return ret;
}

gboolean
gst_rtp_vraw_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpvrawdepay",
      GST_RANK_SECONDARY, GST_TYPE_RTP_VRAW_DEPAY);
}
