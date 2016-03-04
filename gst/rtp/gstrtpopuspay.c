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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>

#include <gst/rtp/gstrtpbuffer.h>
#include <gst/audio/audio.h>

#include "gstrtpopuspay.h"

GST_DEBUG_CATEGORY_STATIC (rtpopuspay_debug);
#define GST_CAT_DEFAULT (rtpopuspay_debug)


static GstStaticPadTemplate gst_rtp_opus_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS
    ("audio/x-opus, channels = (int) [1, 2], channel-mapping-family = (int) 0")
    );

static GstStaticPadTemplate gst_rtp_opus_pay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 48000, "
        "encoding-params = (string) \"2\", "
        "encoding-name = (string) { \"OPUS\", \"X-GST-OPUS-DRAFT-SPITTKA-00\" }")
    );

static gboolean gst_rtp_opus_pay_setcaps (GstRTPBasePayload * payload,
    GstCaps * caps);
static GstCaps *gst_rtp_opus_pay_getcaps (GstRTPBasePayload * payload,
    GstPad * pad, GstCaps * filter);
static GstFlowReturn gst_rtp_opus_pay_handle_buffer (GstRTPBasePayload *
    payload, GstBuffer * buffer);

G_DEFINE_TYPE (GstRtpOPUSPay, gst_rtp_opus_pay, GST_TYPE_RTP_BASE_PAYLOAD);

static void
gst_rtp_opus_pay_class_init (GstRtpOPUSPayClass * klass)
{
  GstRTPBasePayloadClass *gstbasertppayload_class;
  GstElementClass *element_class;

  gstbasertppayload_class = (GstRTPBasePayloadClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);

  gstbasertppayload_class->set_caps = gst_rtp_opus_pay_setcaps;
  gstbasertppayload_class->get_caps = gst_rtp_opus_pay_getcaps;
  gstbasertppayload_class->handle_buffer = gst_rtp_opus_pay_handle_buffer;

  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_opus_pay_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_opus_pay_sink_template);

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
}

static gboolean
gst_rtp_opus_pay_setcaps (GstRTPBasePayload * payload, GstCaps * caps)
{
  gboolean res;
  GstCaps *src_caps;
  GstStructure *s;
  char *encoding_name;
  gint channels, rate;
  const char *sprop_stereo = NULL;
  char *sprop_maxcapturerate = NULL;

  src_caps = gst_pad_get_allowed_caps (GST_RTP_BASE_PAYLOAD_SRCPAD (payload));
  if (src_caps) {
    src_caps = gst_caps_make_writable (src_caps);
    src_caps = gst_caps_truncate (src_caps);
    s = gst_caps_get_structure (src_caps, 0);
    gst_structure_fixate_field_string (s, "encoding-name", "OPUS");
    encoding_name = g_strdup (gst_structure_get_string (s, "encoding-name"));
    gst_caps_unref (src_caps);
  } else {
    encoding_name = g_strdup ("X-GST-OPUS-DRAFT-SPITTKA-00");
  }

  s = gst_caps_get_structure (caps, 0);
  if (gst_structure_get_int (s, "channels", &channels)) {
    if (channels > 2) {
      GST_ERROR_OBJECT (payload,
          "More than 2 channels with channel-mapping-family=0 is invalid");
      return FALSE;
    } else if (channels == 2) {
      sprop_stereo = "1";
    } else {
      sprop_stereo = "0";
    }
  }

  if (gst_structure_get_int (s, "rate", &rate)) {
    sprop_maxcapturerate = g_strdup_printf ("%d", rate);
  }

  gst_rtp_base_payload_set_options (payload, "audio", FALSE,
      encoding_name, 48000);
  g_free (encoding_name);

  if (sprop_maxcapturerate && sprop_stereo) {
    res =
        gst_rtp_base_payload_set_outcaps (payload, "sprop-maxcapturerate",
        G_TYPE_STRING, sprop_maxcapturerate, "sprop-stereo", G_TYPE_STRING,
        sprop_stereo, NULL);
  } else if (sprop_maxcapturerate) {
    res =
        gst_rtp_base_payload_set_outcaps (payload, "sprop-maxcapturerate",
        G_TYPE_STRING, sprop_maxcapturerate, NULL);
  } else if (sprop_stereo) {
    res =
        gst_rtp_base_payload_set_outcaps (payload, "sprop-stereo",
        G_TYPE_STRING, sprop_stereo, NULL);
  } else {
    res = gst_rtp_base_payload_set_outcaps (payload, NULL);
  }

  g_free (sprop_maxcapturerate);

  return res;
}

typedef struct
{
  GstRtpOPUSPay *pay;
  GstBuffer *outbuf;
} CopyMetaData;

static gboolean
foreach_metadata (GstBuffer * inbuf, GstMeta ** meta, gpointer user_data)
{
  CopyMetaData *data = user_data;
  GstRtpOPUSPay *pay = data->pay;
  GstBuffer *outbuf = data->outbuf;
  const GstMetaInfo *info = (*meta)->info;
  const gchar *const *tags = gst_meta_api_type_get_tags (info->api);

  if (!tags || (g_strv_length ((gchar **) tags) == 1
          && gst_meta_api_type_has_tag (info->api,
              g_quark_from_string (GST_META_TAG_AUDIO_STR)))) {
    GstMetaTransformCopy copy_data = { FALSE, 0, -1 };
    GST_DEBUG_OBJECT (pay, "copy metadata %s", g_type_name (info->api));
    /* simply copy then */
    info->transform_func (outbuf, *meta, inbuf,
        _gst_meta_transform_copy, &copy_data);
  } else {
    GST_DEBUG_OBJECT (pay, "not copying metadata %s", g_type_name (info->api));
  }

  return TRUE;
}

static GstFlowReturn
gst_rtp_opus_pay_handle_buffer (GstRTPBasePayload * basepayload,
    GstBuffer * buffer)
{
  GstBuffer *outbuf;
  GstClockTime pts, dts, duration;
  CopyMetaData data;

  pts = GST_BUFFER_PTS (buffer);
  dts = GST_BUFFER_DTS (buffer);
  duration = GST_BUFFER_DURATION (buffer);

  outbuf = gst_rtp_buffer_new_allocate (0, 0, 0);
  data.pay = GST_RTP_OPUS_PAY (basepayload);
  data.outbuf = outbuf;
  gst_buffer_foreach_meta (buffer, foreach_metadata, &data);
  outbuf = gst_buffer_append (outbuf, buffer);

  GST_BUFFER_PTS (outbuf) = pts;
  GST_BUFFER_DTS (outbuf) = dts;
  GST_BUFFER_DURATION (outbuf) = duration;

  /* Push out */
  return gst_rtp_base_payload_push (basepayload, outbuf);
}

static GstCaps *
gst_rtp_opus_pay_getcaps (GstRTPBasePayload * payload,
    GstPad * pad, GstCaps * filter)
{
  GstCaps *caps, *peercaps, *tcaps;
  GstStructure *s;
  const gchar *stereo;

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

  s = gst_caps_get_structure (peercaps, 0);
  stereo = gst_structure_get_string (s, "stereo");
  if (stereo != NULL) {
    caps = gst_caps_make_writable (caps);

    if (!strcmp (stereo, "1")) {
      GstCaps *caps2 = gst_caps_copy (caps);

      gst_caps_set_simple (caps, "channels", G_TYPE_INT, 2, NULL);
      gst_caps_set_simple (caps2, "channels", G_TYPE_INT, 1, NULL);
      caps = gst_caps_merge (caps, caps2);
    } else if (!strcmp (stereo, "0")) {
      GstCaps *caps2 = gst_caps_copy (caps);

      gst_caps_set_simple (caps, "channels", G_TYPE_INT, 1, NULL);
      gst_caps_set_simple (caps2, "channels", G_TYPE_INT, 2, NULL);
      caps = gst_caps_merge (caps, caps2);
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

gboolean
gst_rtp_opus_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpopuspay",
      GST_RANK_PRIMARY, GST_TYPE_RTP_OPUS_PAY);
}
