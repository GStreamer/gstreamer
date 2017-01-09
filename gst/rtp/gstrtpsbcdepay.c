/*
 * GStreamer RTP SBC depayloader
 *
 * Copyright (C) 2012  Collabora Ltd.
 *   @author: Arun Raghavan <arun.raghavan@collabora.co.uk>
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
#include <config.h>
#endif

#include <gst/rtp/gstrtpbuffer.h>
#include <gst/audio/audio.h>
#include "gstrtpsbcdepay.h"
#include "gstrtputils.h"

GST_DEBUG_CATEGORY_STATIC (rtpsbcdepay_debug);
#define GST_CAT_DEFAULT (rtpsbcdepay_debug)

static GstStaticPadTemplate gst_rtp_sbc_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-sbc, "
        "rate = (int) { 16000, 32000, 44100, 48000 }, "
        "channels = (int) [ 1, 2 ], "
        "mode = (string) { mono, dual, stereo, joint }, "
        "blocks = (int) { 4, 8, 12, 16 }, "
        "subbands = (int) { 4, 8 }, "
        "allocation-method = (string) { snr, loudness }, "
        "bitpool = (int) [ 2, 64 ]")
    );

static GstStaticPadTemplate gst_rtp_sbc_depay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) audio,"
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) { 16000, 32000, 44100, 48000 },"
        "encoding-name = (string) SBC")
    );

#define gst_rtp_sbc_depay_parent_class parent_class
G_DEFINE_TYPE (GstRtpSbcDepay, gst_rtp_sbc_depay, GST_TYPE_RTP_BASE_DEPAYLOAD);

static void gst_rtp_sbc_depay_finalize (GObject * object);

static gboolean gst_rtp_sbc_depay_setcaps (GstRTPBaseDepayload * base,
    GstCaps * caps);
static GstBuffer *gst_rtp_sbc_depay_process (GstRTPBaseDepayload * base,
    GstRTPBuffer * rtp);

static void
gst_rtp_sbc_depay_class_init (GstRtpSbcDepayClass * klass)
{
  GstRTPBaseDepayloadClass *gstbasertpdepayload_class =
      GST_RTP_BASE_DEPAYLOAD_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_rtp_sbc_depay_finalize;

  gstbasertpdepayload_class->set_caps = gst_rtp_sbc_depay_setcaps;
  gstbasertpdepayload_class->process_rtp_packet = gst_rtp_sbc_depay_process;

  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_sbc_depay_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_sbc_depay_sink_template);

  GST_DEBUG_CATEGORY_INIT (rtpsbcdepay_debug, "rtpsbcdepay", 0,
      "SBC Audio RTP Depayloader");

  gst_element_class_set_static_metadata (element_class,
      "RTP SBC audio depayloader",
      "Codec/Depayloader/Network/RTP",
      "Extracts SBC audio from RTP packets",
      "Arun Raghavan <arun.raghavan@collabora.co.uk>");
}

static void
gst_rtp_sbc_depay_init (GstRtpSbcDepay * rtpsbcdepay)
{
  rtpsbcdepay->adapter = gst_adapter_new ();
}

static void
gst_rtp_sbc_depay_finalize (GObject * object)
{
  GstRtpSbcDepay *depay = GST_RTP_SBC_DEPAY (object);

  gst_object_unref (depay->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* FIXME: This duplicates similar functionality rtpsbcpay, but there isn't a
 * simple way to consolidate the two. This is best done by moving the function
 * to the codec-utils library in gst-plugins-base when these elements move to
 * GStreamer. */
static int
gst_rtp_sbc_depay_get_params (GstRtpSbcDepay * depay, const guint8 * data,
    gint size, int *framelen, int *samples)
{
  int blocks, channel_mode, channels, subbands, bitpool;
  int length;

  if (size < 3) {
    /* Not enough data for the header */
    return -1;
  }

  /* Sanity check */
  if (data[0] != 0x9c) {
    GST_WARNING_OBJECT (depay, "Bad packet: couldn't find syncword");
    return -2;
  }

  blocks = (data[1] >> 4) & 0x3;
  blocks = (blocks + 1) * 4;
  channel_mode = (data[1] >> 2) & 0x3;
  channels = channel_mode ? 2 : 1;
  subbands = (data[1] & 0x1);
  subbands = (subbands + 1) * 4;
  bitpool = data[2];

  length = 4 + ((4 * subbands * channels) / 8);

  if (channel_mode == 0 || channel_mode == 1) {
    /* Mono || Dual channel */
    length += ((blocks * channels * bitpool)
        + 4 /* round up */ ) / 8;
  } else {
    /* Stereo || Joint stereo */
    gboolean joint = (channel_mode == 3);

    length += ((joint * subbands) + (blocks * bitpool)
        + 4 /* round up */ ) / 8;
  }

  *framelen = length;
  *samples = blocks * subbands;

  return 0;
}

static gboolean
gst_rtp_sbc_depay_setcaps (GstRTPBaseDepayload * base, GstCaps * caps)
{
  GstRtpSbcDepay *depay = GST_RTP_SBC_DEPAY (base);
  GstStructure *structure;
  GstCaps *outcaps, *oldcaps;

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "clock-rate", &depay->rate))
    goto bad_caps;

  outcaps = gst_caps_new_simple ("audio/x-sbc", "rate", G_TYPE_INT,
      depay->rate, NULL);

  gst_pad_set_caps (GST_RTP_BASE_DEPAYLOAD_SRCPAD (base), outcaps);

  oldcaps = gst_pad_get_current_caps (GST_RTP_BASE_DEPAYLOAD_SINKPAD (base));
  if (oldcaps && !gst_caps_can_intersect (oldcaps, caps)) {
    /* Caps have changed, flush old data */
    gst_adapter_clear (depay->adapter);
  }

  gst_caps_unref (outcaps);
  if (oldcaps)
    gst_caps_unref (oldcaps);

  return TRUE;

bad_caps:
  GST_WARNING_OBJECT (depay, "Can't support the caps we got: %"
      GST_PTR_FORMAT, caps);
  return FALSE;
}

static GstBuffer *
gst_rtp_sbc_depay_process (GstRTPBaseDepayload * base, GstRTPBuffer * rtp)
{
  GstRtpSbcDepay *depay = GST_RTP_SBC_DEPAY (base);
  GstBuffer *data = NULL;

  gboolean fragment, start, last;
  guint8 nframes;
  guint8 *payload;
  guint payload_len;

  GST_LOG_OBJECT (depay, "Got %" G_GSIZE_FORMAT " bytes",
      gst_buffer_get_size (rtp->buffer));

  if (gst_rtp_buffer_get_marker (rtp)) {
    /* Marker isn't supposed to be set */
    GST_WARNING_OBJECT (depay, "Marker bit was set");
    goto bad_packet;
  }

  payload = gst_rtp_buffer_get_payload (rtp);
  payload_len = gst_rtp_buffer_get_payload_len (rtp);

  fragment = payload[0] & 0x80;
  start = payload[0] & 0x40;
  last = payload[0] & 0x20;
  nframes = payload[0] & 0x0f;

  payload += 1;
  payload_len -= 1;

  data = gst_rtp_buffer_get_payload_subbuffer (rtp, 1, -1);

  if (fragment) {
    /* Got a packet with a fragment */
    GST_LOG_OBJECT (depay, "Got fragment");

    if (start && gst_adapter_available (depay->adapter)) {
      GST_WARNING_OBJECT (depay, "Missing last fragment");
      gst_adapter_clear (depay->adapter);

    } else if (!start && !gst_adapter_available (depay->adapter)) {
      GST_WARNING_OBJECT (depay, "Missing start fragment");
      gst_buffer_unref (data);
      data = NULL;
      goto out;
    }

    gst_adapter_push (depay->adapter, data);

    if (last) {
      data = gst_adapter_take_buffer (depay->adapter,
          gst_adapter_available (depay->adapter));
      gst_rtp_drop_meta (GST_ELEMENT_CAST (depay), data,
          g_quark_from_static_string (GST_META_TAG_AUDIO_STR));
    } else
      data = NULL;

  } else {
    /* !fragment */
    gint framelen, samples;

    GST_LOG_OBJECT (depay, "Got %d frames", nframes);

    if (gst_rtp_sbc_depay_get_params (depay, payload,
            payload_len, &framelen, &samples) < 0) {
      gst_adapter_clear (depay->adapter);
      goto bad_packet;
    }

    GST_LOG_OBJECT (depay, "Got payload of %d", payload_len);

    if (nframes * framelen > (gint) payload_len) {
      GST_WARNING_OBJECT (depay, "Short packet");
      goto bad_packet;
    } else if (nframes * framelen < (gint) payload_len) {
      GST_WARNING_OBJECT (depay, "Junk at end of packet");
    }
  }

out:
  return data;

bad_packet:
  GST_ELEMENT_WARNING (depay, STREAM, DECODE,
      ("Received invalid RTP payload, dropping"), (NULL));
  goto out;
}

gboolean
gst_rtp_sbc_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpsbcdepay", GST_RANK_SECONDARY,
      GST_TYPE_RTP_SBC_DEPAY);
}
