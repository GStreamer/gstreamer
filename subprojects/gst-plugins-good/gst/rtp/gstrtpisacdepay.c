/* GStreamer
 * Copyright (C) 2020 Collabora Ltd.
 *  Author: Guillaume Desmottes <guillaume.desmottes@collabora.com>, Collabora Ltd.
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

/**
 * SECTION:element-rtpisacdepay
 * @title: rtpisacdepay
 * @short_description: iSAC RTP Depayloader
 *
 * Since: 1.20
 *
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/audio/audio.h>

#include "gstrtpelements.h"
#include "gstrtpisacdepay.h"
#include "gstrtputils.h"

GST_DEBUG_CATEGORY_STATIC (rtpisacdepay_debug);
#define GST_CAT_DEFAULT (rtpisacdepay_debug)

static GstStaticPadTemplate gst_rtp_isac_depay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate =  (int) { 16000, 32000 }, "
        "encoding-name = (string) \"ISAC\"")
    );

static GstStaticPadTemplate gst_rtp_isac_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/isac, "
        "rate = (int) { 16000, 32000 }, " "channels = (int) 1")
    );

struct _GstRtpIsacDepay
{
  /*< private > */
  GstRTPBaseDepayload parent;

  guint64 packet;
};

#define gst_rtp_isac_depay_parent_class parent_class
G_DEFINE_TYPE (GstRtpIsacDepay, gst_rtp_isac_depay,
    GST_TYPE_RTP_BASE_DEPAYLOAD);
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (rtpisacdepay, "rtpisacdepay",
    GST_RANK_SECONDARY, GST_TYPE_RTP_ISAC_DEPAY, rtp_element_init (plugin));

static gboolean
gst_rtp_isac_depay_setcaps (GstRTPBaseDepayload * depayload, GstCaps * caps)
{
  GstCaps *src_caps;
  GstStructure *s;
  gint rate;
  gboolean ret;

  GST_DEBUG_OBJECT (depayload, "sink caps: %" GST_PTR_FORMAT, caps);

  s = gst_caps_get_structure (caps, 0);
  if (!gst_structure_get_int (s, "clock-rate", &rate)) {
    GST_ERROR_OBJECT (depayload, "Missing 'clock-rate' in caps");
    return FALSE;
  }

  src_caps = gst_caps_new_simple ("audio/isac",
      "channels", G_TYPE_INT, 1, "rate", G_TYPE_INT, rate, NULL);

  ret = gst_pad_set_caps (GST_RTP_BASE_DEPAYLOAD_SRCPAD (depayload), src_caps);

  GST_DEBUG_OBJECT (depayload,
      "set caps on source: %" GST_PTR_FORMAT " (ret=%d)", src_caps, ret);
  gst_caps_unref (src_caps);

  return ret;
}

static GstBuffer *
gst_rtp_isac_depay_process (GstRTPBaseDepayload * depayload,
    GstRTPBuffer * rtp_buffer)
{
  GstBuffer *outbuf;

  outbuf = gst_rtp_buffer_get_payload_buffer (rtp_buffer);

  gst_rtp_drop_non_audio_meta (depayload, outbuf);

  return outbuf;
}

static void
gst_rtp_isac_depay_class_init (GstRtpIsacDepayClass * klass)
{
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstRTPBaseDepayloadClass *depayload_class =
      (GstRTPBaseDepayloadClass *) klass;

  depayload_class->set_caps = gst_rtp_isac_depay_setcaps;
  depayload_class->process_rtp_packet = gst_rtp_isac_depay_process;

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_rtp_isac_depay_sink_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_rtp_isac_depay_src_template);

  gst_element_class_set_static_metadata (gstelement_class,
      "RTP iSAC depayloader", "Codec/Depayloader/Network/RTP",
      "Extracts iSAC audio from RTP packets",
      "Guillaume Desmottes <guillaume.desmottes@collabora.com>");

  GST_DEBUG_CATEGORY_INIT (rtpisacdepay_debug, "rtpisacdepay", 0,
      "iSAC RTP Depayloader");
}

static void
gst_rtp_isac_depay_init (GstRtpIsacDepay * rtpisacdepay)
{
}
