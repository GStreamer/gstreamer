/*
 * Opus Depayloader Gst Element
 *
 *   @author: Danilo Cesar Lemes de Paula <danilo.cesar@collabora.co.uk>
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
#  include "config.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/audio/audio.h>
#include "gstrtpelements.h"
#include "gstrtpopusdepay.h"
#include "gstrtputils.h"

GST_DEBUG_CATEGORY_STATIC (rtpopusdepay_debug);
#define GST_CAT_DEFAULT (rtpopusdepay_debug)

static GstStaticPadTemplate gst_rtp_opus_depay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ","
        "clock-rate = (int) 48000, "
        "encoding-name = (string) { \"OPUS\", \"X-GST-OPUS-DRAFT-SPITTKA-00\", \"MULTIOPUS\" }")
    );

static GstStaticPadTemplate gst_rtp_opus_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-opus, channel-mapping-family = (int) [ 0, 1 ]")
    );

static GstBuffer *gst_rtp_opus_depay_process (GstRTPBaseDepayload * depayload,
    GstRTPBuffer * rtp_buffer);
static gboolean gst_rtp_opus_depay_setcaps (GstRTPBaseDepayload * depayload,
    GstCaps * caps);

G_DEFINE_TYPE (GstRTPOpusDepay, gst_rtp_opus_depay,
    GST_TYPE_RTP_BASE_DEPAYLOAD);
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (rtpopusdepay, "rtpopusdepay",
    GST_RANK_PRIMARY, GST_TYPE_RTP_OPUS_DEPAY, rtp_element_init (plugin));

static void
gst_rtp_opus_depay_class_init (GstRTPOpusDepayClass * klass)
{
  GstRTPBaseDepayloadClass *gstbasertpdepayload_class;
  GstElementClass *element_class;

  element_class = GST_ELEMENT_CLASS (klass);
  gstbasertpdepayload_class = (GstRTPBaseDepayloadClass *) klass;

  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_opus_depay_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_opus_depay_sink_template);
  gst_element_class_set_static_metadata (element_class,
      "RTP Opus packet depayloader", "Codec/Depayloader/Network/RTP",
      "Extracts Opus audio from RTP packets",
      "Danilo Cesar Lemes de Paula <danilo.cesar@collabora.co.uk>");

  gstbasertpdepayload_class->process_rtp_packet = gst_rtp_opus_depay_process;
  gstbasertpdepayload_class->set_caps = gst_rtp_opus_depay_setcaps;

  GST_DEBUG_CATEGORY_INIT (rtpopusdepay_debug, "rtpopusdepay", 0,
      "Opus RTP Depayloader");
}

static void
gst_rtp_opus_depay_init (GstRTPOpusDepay * rtpopusdepay)
{

}

static gboolean
gst_rtp_opus_depay_setcaps (GstRTPBaseDepayload * depayload, GstCaps * caps)
{
  GstCaps *srccaps;
  GstStructure *s;
  gboolean ret;
  const gchar *sprop_maxcapturerate;
  /* Default unless overridden by sprop_maxcapturerate */
  gint rate = 48000;

  srccaps = gst_caps_new_empty_simple ("audio/x-opus");

  s = gst_caps_get_structure (caps, 0);

  if (g_str_equal (gst_structure_get_string (s, "encoding-name"), "MULTIOPUS")) {
    gint channels;
    gint stream_count;
    gint coupled_count;
    const gchar *encoding_params;
    const gchar *num_streams;
    const gchar *coupled_streams;
    const gchar *channel_mapping;
    gchar *endptr;

    if (!gst_structure_has_field_typed (s, "encoding-params", G_TYPE_STRING) ||
        !gst_structure_has_field_typed (s, "num_streams", G_TYPE_STRING) ||
        !gst_structure_has_field_typed (s, "coupled_streams", G_TYPE_STRING) ||
        !gst_structure_has_field_typed (s, "channel_mapping", G_TYPE_STRING)) {
      GST_WARNING_OBJECT (depayload, "Encoding name 'MULTIOPUS' requires "
          "encoding-params, num_streams, coupled_streams and channel_mapping "
          "as string fields in caps.");
      goto reject_caps;
    }

    gst_caps_set_simple (srccaps, "channel-mapping-family", G_TYPE_INT, 1,
        NULL);

    encoding_params = gst_structure_get_string (s, "encoding-params");
    channels = g_ascii_strtoull (encoding_params, &endptr, 10);
    if (*endptr != '\0' || channels > 255) {
      GST_WARNING_OBJECT (depayload, "Invalid encoding-params value '%s'",
          encoding_params);
      goto reject_caps;
    }
    gst_caps_set_simple (srccaps, "channels", G_TYPE_INT, channels, NULL);

    num_streams = gst_structure_get_string (s, "num_streams");
    stream_count = g_ascii_strtoull (num_streams, &endptr, 10);
    if (*endptr != '\0' || stream_count > channels) {
      GST_WARNING_OBJECT (depayload, "Invalid num_streams value '%s'",
          num_streams);
      goto reject_caps;
    }
    gst_caps_set_simple (srccaps, "stream-count", G_TYPE_INT, stream_count,
        NULL);

    coupled_streams = gst_structure_get_string (s, "coupled_streams");
    coupled_count = g_ascii_strtoull (coupled_streams, &endptr, 10);
    if (*endptr != '\0' || coupled_count > stream_count) {
      GST_WARNING_OBJECT (depayload, "Invalid coupled_streams value '%s'",
          coupled_streams);
      goto reject_caps;
    }
    gst_caps_set_simple (srccaps, "coupled-count", G_TYPE_INT, coupled_count,
        NULL);

    channel_mapping = gst_structure_get_string (s, "channel_mapping");
    {
      gchar **split;
      gchar **ptr;
      GValue mapping = G_VALUE_INIT;
      GValue v = G_VALUE_INIT;

      split = g_strsplit (channel_mapping, ",", -1);

      g_value_init (&mapping, GST_TYPE_ARRAY);
      g_value_init (&v, G_TYPE_INT);

      for (ptr = split; *ptr; ++ptr) {
        gint channel = g_ascii_strtoull (*ptr, &endptr, 10);
        if (*endptr != '\0' || channel > channels) {
          GST_WARNING_OBJECT (depayload, "Invalid channel_mapping value '%s'",
              channel_mapping);
          g_value_unset (&mapping);
          break;
        }
        g_value_set_int (&v, channel);
        gst_value_array_append_value (&mapping, &v);
      }

      g_value_unset (&v);
      g_strfreev (split);

      if (G_IS_VALUE (&mapping)) {
        gst_caps_set_value (srccaps, "channel-mapping", &mapping);
        g_value_unset (&mapping);
      } else {
        goto reject_caps;
      }
    }
  } else {
    const gchar *sprop_stereo;

    gst_caps_set_simple (srccaps, "channel-mapping-family", G_TYPE_INT, 0,
        NULL);

    if ((sprop_stereo = gst_structure_get_string (s, "sprop-stereo"))) {
      if (strcmp (sprop_stereo, "0") == 0)
        gst_caps_set_simple (srccaps, "channels", G_TYPE_INT, 1, NULL);
      else if (strcmp (sprop_stereo, "1") == 0)
        gst_caps_set_simple (srccaps, "channels", G_TYPE_INT, 2, NULL);
      else
        GST_WARNING_OBJECT (depayload, "Unknown sprop-stereo value '%s'",
            sprop_stereo);
    } else {
      /* Although sprop-stereo defaults to mono as per RFC 7587, this just means
         that the signal is likely mono and can be safely downmixed, it may
         still be stereo at times. */
      gst_caps_set_simple (srccaps, "channels", G_TYPE_INT, 2, NULL);
    }
  }

  if ((sprop_maxcapturerate =
          gst_structure_get_string (s, "sprop-maxcapturerate"))) {
    gchar *tailptr;
    gulong tmp_rate;

    tmp_rate = strtoul (sprop_maxcapturerate, &tailptr, 10);
    if (tmp_rate > INT_MAX || *tailptr != '\0') {
      GST_WARNING_OBJECT (depayload,
          "Failed to parse sprop-maxcapturerate value '%s'",
          sprop_maxcapturerate);
    } else {
      /* Valid rate from sprop, let's use it */
      rate = tmp_rate;
    }
  }

  gst_caps_set_simple (srccaps, "rate", G_TYPE_INT, rate, NULL);

  ret = gst_pad_set_caps (GST_RTP_BASE_DEPAYLOAD_SRCPAD (depayload), srccaps);

  GST_DEBUG_OBJECT (depayload,
      "set caps on source: %" GST_PTR_FORMAT " (ret=%d)", srccaps, ret);
  gst_caps_unref (srccaps);

  depayload->clock_rate = 48000;

  return ret;

reject_caps:
  gst_caps_unref (srccaps);

  return FALSE;
}

static GstBuffer *
gst_rtp_opus_depay_process (GstRTPBaseDepayload * depayload,
    GstRTPBuffer * rtp_buffer)
{
  GstBuffer *outbuf;

  outbuf = gst_rtp_buffer_get_payload_buffer (rtp_buffer);

  gst_rtp_drop_non_audio_meta (depayload, outbuf);

  return outbuf;
}
