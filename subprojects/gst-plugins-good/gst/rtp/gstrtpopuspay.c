/*
 * Opus Payloader Gst Element
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

/**
 * SECTION:element-rtpopuspay
 * @title: rtpopuspay
 *
 * rtpopuspay encapsulates Opus-encoded audio data into RTP packets following
 * the payload format described in RFC 7587.
 *
 * In addition to the RFC, which assumes only mono and stereo payload,
 * the element supports multichannel Opus audio streams using a non-standardized
 * SDP config and "MULTIOPUS" codec developed by Google for libwebrtc. When the
 * input data have more than 2 channels, rtpopuspay will add extra fields to
 * output caps that can be used to generate SDP in the syntax understood by
 * libwebrtc. For example in the case of 5.1 audio:
 *
 * |[
 *  a=rtpmap:96 multiopus/48000/6
 *  a=fmtp:96 num_streams=4;coupled_streams=2;channel_mapping=0,4,1,2,3,5
 * ]|
 *
 * See https://webrtc-review.googlesource.com/c/src/+/129768 for more details on
 * multichannel Opus in libwebrtc.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>

#include <gst/rtp/gstrtpbuffer.h>
#include <gst/audio/audio.h>

#include "gstrtpelements.h"
#include "gstrtpopuspay.h"
#include "gstrtputils.h"

GST_DEBUG_CATEGORY_STATIC (rtpopuspay_debug);
#define GST_CAT_DEFAULT (rtpopuspay_debug)

enum
{
  PROP_0,
  PROP_DTX,
};

#define DEFAULT_DTX FALSE

static GstStaticPadTemplate gst_rtp_opus_pay_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-opus, channel-mapping-family = (int) 0;"
        "audio/x-opus, channel-mapping-family = (int) 0, channels = (int) [1, 2];"
        "audio/x-opus, channel-mapping-family = (int) 1, channels = (int) [3, 255]")
    );

static GstStaticPadTemplate gst_rtp_opus_pay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 48000, "
        "encoding-name = (string) { \"OPUS\", \"X-GST-OPUS-DRAFT-SPITTKA-00\", \"MULTIOPUS\" }")
    );

static gboolean gst_rtp_opus_pay_setcaps (GstRTPBasePayload * payload,
    GstCaps * caps);
static GstCaps *gst_rtp_opus_pay_getcaps (GstRTPBasePayload * payload,
    GstPad * pad, GstCaps * filter);
static GstFlowReturn gst_rtp_opus_pay_handle_buffer (GstRTPBasePayload *
    payload, GstBuffer * buffer);

G_DEFINE_TYPE (GstRtpOPUSPay, gst_rtp_opus_pay, GST_TYPE_RTP_BASE_PAYLOAD);
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (rtpopuspay, "rtpopuspay",
    GST_RANK_PRIMARY, GST_TYPE_RTP_OPUS_PAY, rtp_element_init (plugin));

#define GST_RTP_OPUS_PAY_CAST(obj) ((GstRtpOPUSPay *)(obj))

static void
gst_rtp_opus_pay_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstRtpOPUSPay *self = GST_RTP_OPUS_PAY (object);

  switch (prop_id) {
    case PROP_DTX:
      self->dtx = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_opus_pay_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstRtpOPUSPay *self = GST_RTP_OPUS_PAY (object);

  switch (prop_id) {
    case PROP_DTX:
      g_value_set_boolean (value, self->dtx);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_rtp_opus_pay_change_state (GstElement * element, GstStateChange transition)
{
  GstRtpOPUSPay *self = GST_RTP_OPUS_PAY (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      self->marker = TRUE;
      break;
    default:
      break;
  }

  ret =
      GST_ELEMENT_CLASS (gst_rtp_opus_pay_parent_class)->change_state (element,
      transition);

  switch (transition) {
    default:
      break;
  }

  return ret;
}

static void
gst_rtp_opus_pay_class_init (GstRtpOPUSPayClass * klass)
{
  GstRTPBasePayloadClass *gstbasertppayload_class;
  GstElementClass *element_class;
  GObjectClass *gobject_class;

  gstbasertppayload_class = (GstRTPBasePayloadClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);
  gobject_class = (GObjectClass *) klass;

  element_class->change_state = gst_rtp_opus_pay_change_state;

  gstbasertppayload_class->set_caps = gst_rtp_opus_pay_setcaps;
  gstbasertppayload_class->get_caps = gst_rtp_opus_pay_getcaps;
  gstbasertppayload_class->handle_buffer = gst_rtp_opus_pay_handle_buffer;

  gobject_class->set_property = gst_rtp_opus_pay_set_property;
  gobject_class->get_property = gst_rtp_opus_pay_get_property;

  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_opus_pay_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_opus_pay_sink_template);

  /**
   * GstRtpOPUSPay:dtx:
   *
   * If enabled, the payloader will not transmit empty packets.
   *
   * Since: 1.20
   */
  g_object_class_install_property (gobject_class, PROP_DTX,
      g_param_spec_boolean ("dtx", "Discontinuous Transmission",
          "If enabled, the payloader will not transmit empty packets",
          DEFAULT_DTX,
          G_PARAM_READWRITE | GST_PARAM_MUTABLE_PLAYING |
          G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element_class,
      "RTP Opus payloader",
      "Codec/Payloader/Network/RTP",
      "Puts Opus audio in RTP packets",
      "Danilo Cesar Lemes de Paula <danilo.cesar@collabora.co.uk>");

  GST_DEBUG_CATEGORY_INIT (rtpopuspay_debug, "rtpopuspay", 0,
      "Opus RTP Payloader");
}

static void
gst_rtp_opus_pay_init (GstRtpOPUSPay * rtpopuspay)
{
  rtpopuspay->dtx = DEFAULT_DTX;
}

static gboolean
gst_rtp_opus_pay_setcaps (GstRTPBasePayload * payload, GstCaps * caps)
{
  gboolean res;
  GstCaps *src_caps;
  GstStructure *s, *outcaps;
  const char *encoding_name = "OPUS";
  gint channels = 2;
  gint rate;
  gchar *encoding_params;

  outcaps = gst_structure_new_empty ("unused");

  src_caps = gst_pad_get_allowed_caps (GST_RTP_BASE_PAYLOAD_SRCPAD (payload));
  if (src_caps) {
    GstStructure *s;
    const GValue *value;

    s = gst_caps_get_structure (src_caps, 0);

    if (gst_structure_has_field (s, "encoding-name")) {
      GValue default_value = G_VALUE_INIT;

      g_value_init (&default_value, G_TYPE_STRING);
      g_value_set_static_string (&default_value, encoding_name);

      value = gst_structure_get_value (s, "encoding-name");
      if (!gst_value_can_intersect (&default_value, value))
        encoding_name = "X-GST-OPUS-DRAFT-SPITTKA-00";
    }
    gst_caps_unref (src_caps);
  }

  s = gst_caps_get_structure (caps, 0);
  if (gst_structure_get_int (s, "channels", &channels)) {
    if (channels > 2) {
      /* Implies channel-mapping-family = 1. */

      gint stream_count, coupled_count;
      const GValue *channel_mapping_array;

      /* libwebrtc only supports "multiopus" when channels > 2. Mono and stereo
       * sound must always be payloaded according to RFC 7587. */
      encoding_name = "MULTIOPUS";

      if (gst_structure_get_int (s, "stream-count", &stream_count)) {
        char *num_streams = g_strdup_printf ("%d", stream_count);
        gst_structure_set (outcaps, "num_streams", G_TYPE_STRING, num_streams,
            NULL);
        g_free (num_streams);
      }
      if (gst_structure_get_int (s, "coupled-count", &coupled_count)) {
        char *coupled_streams = g_strdup_printf ("%d", coupled_count);
        gst_structure_set (outcaps, "coupled_streams", G_TYPE_STRING,
            coupled_streams, NULL);
        g_free (coupled_streams);
      }

      channel_mapping_array = gst_structure_get_value (s, "channel-mapping");
      if (GST_VALUE_HOLDS_ARRAY (channel_mapping_array)) {
        GString *str = g_string_new (NULL);
        guint i;

        for (i = 0; i < gst_value_array_get_size (channel_mapping_array); ++i) {
          if (i != 0) {
            g_string_append_c (str, ',');
          }
          g_string_append_printf (str, "%d",
              g_value_get_int (gst_value_array_get_value (channel_mapping_array,
                      i)));
        }

        gst_structure_set (outcaps, "channel_mapping", G_TYPE_STRING, str->str,
            NULL);

        g_string_free (str, TRUE);
      }
    } else {
      gst_structure_set (outcaps, "sprop-stereo", G_TYPE_STRING,
          (channels == 2) ? "1" : "0", NULL);
      /* RFC 7587 requires the number of channels always be 2. */
      channels = 2;
    }
  }

  encoding_params = g_strdup_printf ("%d", channels);
  gst_structure_set (outcaps, "encoding-params", G_TYPE_STRING,
      encoding_params, NULL);
  g_free (encoding_params);

  if (gst_structure_get_int (s, "rate", &rate)) {
    gchar *sprop_maxcapturerate = g_strdup_printf ("%d", rate);

    gst_structure_set (outcaps, "sprop-maxcapturerate", G_TYPE_STRING,
        sprop_maxcapturerate, NULL);

    g_free (sprop_maxcapturerate);
  }

  gst_rtp_base_payload_set_options (payload, "audio", FALSE,
      encoding_name, 48000);

  res = gst_rtp_base_payload_set_outcaps_structure (payload, outcaps);

  gst_structure_free (outcaps);

  return res;
}

static GstFlowReturn
gst_rtp_opus_pay_handle_buffer (GstRTPBasePayload * basepayload,
    GstBuffer * buffer)
{
  GstRtpOPUSPay *self = GST_RTP_OPUS_PAY_CAST (basepayload);
  GstBuffer *outbuf;
  GstClockTime pts, dts, duration;

  /* DTX packets are zero-length frames, with a 1 or 2-bytes header */
  if (self->dtx && gst_buffer_get_size (buffer) <= 2) {
    GST_LOG_OBJECT (self,
        "discard empty buffer as DTX is enabled: %" GST_PTR_FORMAT, buffer);
    self->marker = TRUE;
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }

  pts = GST_BUFFER_PTS (buffer);
  dts = GST_BUFFER_DTS (buffer);
  duration = GST_BUFFER_DURATION (buffer);

  outbuf = gst_rtp_base_payload_allocate_output_buffer (basepayload, 0, 0, 0);

  gst_rtp_copy_audio_meta (basepayload, outbuf, buffer);

  outbuf = gst_buffer_append (outbuf, buffer);

  GST_BUFFER_PTS (outbuf) = pts;
  GST_BUFFER_DTS (outbuf) = dts;
  GST_BUFFER_DURATION (outbuf) = duration;

  if (self->marker) {
    GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;

    gst_rtp_buffer_map (outbuf, GST_MAP_READWRITE, &rtp);
    gst_rtp_buffer_set_marker (&rtp, TRUE);
    gst_rtp_buffer_unmap (&rtp);

    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_MARKER);
    self->marker = FALSE;
  }

  /* Push out */
  return gst_rtp_base_payload_push (basepayload, outbuf);
}

static GstCaps *
gst_rtp_opus_pay_getcaps (GstRTPBasePayload * payload,
    GstPad * pad, GstCaps * filter)
{
  GstStructure *s;
  int channel_mapping_family = 0;
  GstCaps *caps, *peercaps, *tcaps, *tempcaps;
  static GstStaticCaps opus_static_caps = GST_STATIC_CAPS ("application/x-rtp, "
      "encoding-name=(string) { \"OPUS\", \"X-GST-OPUS-DRAFT-SPITTKA-00\"}");
  static GstStaticCaps multiopus_static_caps =
      GST_STATIC_CAPS ("application/x-rtp, encoding-name=(string)MULTIOPUS");

  if (pad == GST_RTP_BASE_PAYLOAD_SRCPAD (payload))
    return
        GST_RTP_BASE_PAYLOAD_CLASS (gst_rtp_opus_pay_parent_class)->get_caps
        (payload, pad, filter);

  tcaps = gst_pad_get_pad_template_caps (GST_RTP_BASE_PAYLOAD_SRCPAD (payload));
  peercaps = gst_pad_peer_query_caps (GST_RTP_BASE_PAYLOAD_SRCPAD (payload),
      tcaps);
  gst_caps_unref (tcaps);
  if (!peercaps)
    return
        GST_RTP_BASE_PAYLOAD_CLASS (gst_rtp_opus_pay_parent_class)->get_caps
        (payload, pad, filter);

  if (gst_caps_is_empty (peercaps))
    return peercaps;

  caps = gst_pad_get_pad_template_caps (GST_RTP_BASE_PAYLOAD_SINKPAD (payload));

  tempcaps = gst_static_caps_get (&opus_static_caps);
  if (!gst_caps_can_intersect (peercaps, tempcaps)) {
    GstCaps *multiopuscaps = gst_caps_new_simple ("audio/x-opus",
        "channel-mapping-family", G_TYPE_INT, 1,
        "channels", GST_TYPE_INT_RANGE, 3, 255,
        NULL);
    GstCaps *intersect_caps;

    intersect_caps = gst_caps_intersect_full (caps, multiopuscaps,
        GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    gst_caps_unref (multiopuscaps);
    caps = intersect_caps;
  }
  gst_caps_unref (tempcaps);

  tempcaps = gst_static_caps_get (&multiopus_static_caps);
  if (!gst_caps_can_intersect (peercaps, tempcaps)) {
    GstCaps *opuscaps = gst_caps_new_simple ("audio/x-opus",
        "channel-mapping-family", G_TYPE_INT, 0,
        "channels", GST_TYPE_INT_RANGE, 1, 2,
        NULL);
    GstCaps *intersect_caps;

    intersect_caps = gst_caps_intersect_full (caps, opuscaps,
        GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    gst_caps_unref (opuscaps);
    caps = intersect_caps;
  }
  gst_caps_unref (tempcaps);

  s = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (s, "channel-mapping-family", &channel_mapping_family);
  if (channel_mapping_family == 0) {
    GstStructure *sp = gst_caps_get_structure (peercaps, 0);
    const gchar *stereo = gst_structure_get_string (sp, "stereo");

    if (stereo != NULL) {
      guint channels = 0;

      if (!strcmp (stereo, "1"))
        channels = 2;
      else if (!strcmp (stereo, "0"))
        channels = 1;

      if (channels) {
        GstCaps *caps2 = gst_caps_copy_nth (caps, 0);

        gst_caps_set_simple (caps2, "channels", G_TYPE_INT, channels, NULL);
        caps = gst_caps_make_writable (caps);
        caps = gst_caps_merge (caps2, caps);
      }
    }
  }
  gst_caps_unref (peercaps);

  if (filter) {
    GstCaps *tmp = gst_caps_intersect_full (caps, filter,
        GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    caps = tmp;
  }

  GST_DEBUG_OBJECT (payload, "Returning caps: %" GST_PTR_FORMAT, caps);
  return caps;
}
