/* GStreamer
 * Copyright (C) <2006> Wim Taymans <wim.taymans@gmail.com>
 * Copyright (C) <2014> Jurgen Slowack <jurgenslowack@gmail.com>
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
#include <gst/pbutils/pbutils.h>
#include <gst/video/video.h>

/* Included to not duplicate gst_rtp_h265_add_vps_sps_pps () */
#include "gstrtph265depay.h"

#include "gstrtph265pay.h"
#include "gstrtputils.h"

GST_DEBUG_CATEGORY_STATIC (rtph265pay_debug);
#define GST_CAT_DEFAULT (rtph265pay_debug)

/* references:
 *
 * Internet Draft RTP Payload Format for High Efficiency Video Coding
 *
 *                   draft-ietf-payload-rtp-h265-03.txt
 *
 * This draft will be replaced with an RFC, so some details may change.
 *
 */

static GstStaticPadTemplate gst_rtp_h265_pay_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
        /* only hvc1 and byte-stream formats supported for now */
        "video/x-h265, stream-format = (string) hvc1, alignment = (string) au; "
        /* "video/x-h265, "
           "stream-format = (string) hev1, alignment = (string) au; " */
        "video/x-h265, stream-format = (string) byte-stream, "
        "alignment = (string) { nal, au }")
    );

static GstStaticPadTemplate gst_rtp_h265_pay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 90000, " "encoding-name = (string) \"H265\"")
                /** optional parameters **/
    /* "profile-space = (int) [ 0, 3 ], " */
    /* "profile-id = (int) [ 0, 31 ], " */
    /* "tier-flag = (int) [ 0, 1 ], " */
    /* "level-id = (int) [ 0, 255 ], " */
    /* "interop-constraints = (string) ANY, " */
    /* "profile-compatibility-indicator = (string) ANY, " */
    /* "sprop-sub-layer-id = (int) [ 0, 6 ], " */
    /* "recv-sub-layer-id = (int) [ 0, 6 ], " */
    /* "max-recv-level-id = (int) [ 0, 255 ], " */
    /* "tx-mode = (string) {MST , SST}, " */
    /* "sprop-vps = (string) ANY, " */
    /* "sprop-sps = (string) ANY, " */
    /* "sprop-pps = (string) ANY, " */
    /* "sprop-sei = (string) ANY, " */
    /* "max-lsr = (int) ANY, " *//* MUST be in the range of MaxLumaSR to 16 * MaxLumaSR, inclusive */
    /* "max-lps = (int) ANY, " *//* MUST be in the range of MaxLumaPS to 16 * MaxLumaPS, inclusive */
    /* "max-cpb = (int) ANY, " *//* MUST be in the range of MaxCPB to 16 * MaxCPB, inclusive */
    /* "max-dpb = (int) [1, 16], " */
    /* "max-br = (int) ANY, " *//* MUST be in the range of MaxBR to 16 * MaxBR, inclusive, for the highest level */
    /* "max-tr = (int) ANY, " *//* MUST be in the range of MaxTileRows to 16 * MaxTileRows, inclusive, for the highest level */
    /* "max-tc = (int) ANY, " *//* MUST be in the range of MaxTileCols to 16 * MaxTileCols, inclusive, for the highest level */
    /* "max-fps = (int) ANY, " */
    /* "sprop-max-don-diff = (int) [0, 32767], " */
    /* "sprop-depack-buf-nalus = (int) [0, 32767], " */
    /* "sprop-depack-buf-nalus = (int) [0, 4294967295], " */
    /* "depack-buf-cap = (int) [1, 4294967295], " */
    /* "sprop-segmentation-id = (int) [0, 3], " */
    /* "sprop-spatial-segmentation-idc = (string) ANY, " */
    /* "dec-parallel-cap = (string) ANY, " */
    );

#define DEFAULT_CONFIG_INTERVAL		      0

enum
{
  PROP_0,
  PROP_CONFIG_INTERVAL
};

#define IS_ACCESS_UNIT(x) (((x) >= 0x00) && ((x) < 0x20))

static void gst_rtp_h265_pay_finalize (GObject * object);

static void gst_rtp_h265_pay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_h265_pay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstCaps *gst_rtp_h265_pay_getcaps (GstRTPBasePayload * payload,
    GstPad * pad, GstCaps * filter);
static gboolean gst_rtp_h265_pay_setcaps (GstRTPBasePayload * basepayload,
    GstCaps * caps);
static GstFlowReturn gst_rtp_h265_pay_handle_buffer (GstRTPBasePayload * pad,
    GstBuffer * buffer);
static gboolean gst_rtp_h265_pay_sink_event (GstRTPBasePayload * payload,
    GstEvent * event);
static GstStateChangeReturn gst_rtp_h265_pay_change_state (GstElement *
    element, GstStateChange transition);

#define gst_rtp_h265_pay_parent_class parent_class
G_DEFINE_TYPE (GstRtpH265Pay, gst_rtp_h265_pay, GST_TYPE_RTP_BASE_PAYLOAD);

static void
gst_rtp_h265_pay_class_init (GstRtpH265PayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstRTPBasePayloadClass *gstrtpbasepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstrtpbasepayload_class = (GstRTPBasePayloadClass *) klass;

  gobject_class->set_property = gst_rtp_h265_pay_set_property;
  gobject_class->get_property = gst_rtp_h265_pay_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_CONFIG_INTERVAL,
      g_param_spec_int ("config-interval",
          "VPS SPS PPS Send Interval",
          "Send VPS, SPS and PPS Insertion Interval in seconds (sprop parameter sets "
          "will be multiplexed in the data stream when detected.) "
          "(0 = disabled, -1 = send with every IDR frame)",
          -1, 3600, DEFAULT_CONFIG_INTERVAL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );

  gobject_class->finalize = gst_rtp_h265_pay_finalize;

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_rtp_h265_pay_src_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_rtp_h265_pay_sink_template);

  gst_element_class_set_static_metadata (gstelement_class, "RTP H265 payloader",
      "Codec/Payloader/Network/RTP",
      "Payload-encode H265 video into RTP packets (RFC 7798)",
      "Jurgen Slowack <jurgenslowack@gmail.com>");

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_rtp_h265_pay_change_state);

  gstrtpbasepayload_class->get_caps = gst_rtp_h265_pay_getcaps;
  gstrtpbasepayload_class->set_caps = gst_rtp_h265_pay_setcaps;
  gstrtpbasepayload_class->handle_buffer = gst_rtp_h265_pay_handle_buffer;
  gstrtpbasepayload_class->sink_event = gst_rtp_h265_pay_sink_event;

  GST_DEBUG_CATEGORY_INIT (rtph265pay_debug, "rtph265pay", 0,
      "H265 RTP Payloader");
}

static void
gst_rtp_h265_pay_init (GstRtpH265Pay * rtph265pay)
{
  rtph265pay->queue = g_array_new (FALSE, FALSE, sizeof (guint));
  rtph265pay->sps = g_ptr_array_new_with_free_func (
      (GDestroyNotify) gst_buffer_unref);
  rtph265pay->pps = g_ptr_array_new_with_free_func (
      (GDestroyNotify) gst_buffer_unref);
  rtph265pay->vps = g_ptr_array_new_with_free_func (
      (GDestroyNotify) gst_buffer_unref);
  rtph265pay->last_vps_sps_pps = -1;
  rtph265pay->vps_sps_pps_interval = DEFAULT_CONFIG_INTERVAL;

  rtph265pay->adapter = gst_adapter_new ();
}

static void
gst_rtp_h265_pay_clear_vps_sps_pps (GstRtpH265Pay * rtph265pay)
{
  g_ptr_array_set_size (rtph265pay->vps, 0);
  g_ptr_array_set_size (rtph265pay->sps, 0);
  g_ptr_array_set_size (rtph265pay->pps, 0);
}

static void
gst_rtp_h265_pay_finalize (GObject * object)
{
  GstRtpH265Pay *rtph265pay;

  rtph265pay = GST_RTP_H265_PAY (object);

  g_array_free (rtph265pay->queue, TRUE);

  g_ptr_array_free (rtph265pay->sps, TRUE);
  g_ptr_array_free (rtph265pay->pps, TRUE);
  g_ptr_array_free (rtph265pay->vps, TRUE);

  g_object_unref (rtph265pay->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static const gchar all_levels[][4] = {
  "1",
  "2",
  "2.1",
  "3",
  "3.1",
  "4",
  "4.1",
  "5",
  "5.1",
  "5.2",
  "6",
  "6.1",
  "6.2"
};

static gboolean
parse_field (GstStructure * s, const gchar * field, gulong min, gulong max,
    guint8 * result)
{
  const gchar *str;

  g_assert (result != NULL);

  str = gst_structure_get_string (s, field);
  if (str != NULL && *str != '\0') {
    gulong value;
    gchar *end;

    value = strtoul (str, &end, 10);
    if (*end == '\0' && value >= min && value <= max) {
      *result = (guint8) value;
    } else {
      return FALSE;
    }
  } else {
    return FALSE;
  }

  return TRUE;
}

static GstCaps *
gst_rtp_h265_pay_getcaps (GstRTPBasePayload * payload, GstPad * pad,
    GstCaps * filter)
{
  GstCaps *template_caps;
  GstCaps *allowed_caps;
  GstCaps *caps;
  GstCaps *icaps;
  guint i;

  allowed_caps =
      gst_pad_peer_query_caps (GST_RTP_BASE_PAYLOAD_SRCPAD (payload), NULL);

  if (allowed_caps == NULL)
    return NULL;

  template_caps =
      gst_static_pad_template_get_caps (&gst_rtp_h265_pay_sink_template);

  if (gst_caps_is_any (allowed_caps)) {
    caps = gst_caps_ref (template_caps);
    goto done;
  }

  if (gst_caps_is_empty (allowed_caps)) {
    caps = gst_caps_ref (allowed_caps);
    goto done;
  }

  caps = gst_caps_new_empty ();
  for (i = 0; i < gst_caps_get_size (allowed_caps); i++) {
    GstStructure *s = gst_caps_get_structure (allowed_caps, i);
    GstStructure *new_s = gst_structure_new_empty ("video/x-h265");
    guint8 ptl[12] = { 0, };
    guint8 value;

    if (parse_field (s, "profile-id", 0, 31, &value)) {
      const gchar *profile;

      ptl[0] = value;
      profile = gst_codec_utils_h265_get_profile (ptl, sizeof (ptl));
      if (profile != NULL) {
        GST_DEBUG_OBJECT (payload, "profile %s", profile);
        gst_structure_set (new_s, "profile", G_TYPE_STRING, profile, NULL);
      } else {
        GST_WARNING_OBJECT (payload, "invalid profile-id %d in caps", value);
      }
    } else {
      GST_DEBUG_OBJECT (payload, "no valid profile-id in caps");
    }

    if (parse_field (s, "tier-flag", 0, 1, &value)) {
      const gchar *tier;

      ptl[0] |= value << 5;
      tier = gst_codec_utils_h265_get_tier (ptl, sizeof (ptl));
      GST_DEBUG_OBJECT (payload, "tier %s", tier);
      gst_structure_set (new_s, "tier", G_TYPE_STRING, tier, NULL);
    } else {
      GST_DEBUG_OBJECT (payload, "no valid tier-flag in caps");
    }

    if (parse_field (s, "level-id", 0, 255, &value)) {
      const gchar *level;

      ptl[11] = value;
      level = gst_codec_utils_h265_get_level (ptl, sizeof (ptl));
      if (level != NULL) {
        GST_DEBUG_OBJECT (payload, "level %s", level);
        if (strcmp (level, "1") == 0) {
          gst_structure_set (new_s, "level", G_TYPE_STRING, level, NULL);
        } else {
          GValue levels = { 0, };
          GValue val = { 0, };
          int j;

          g_value_init (&levels, GST_TYPE_LIST);
          g_value_init (&val, G_TYPE_STRING);

          for (j = 0; j < G_N_ELEMENTS (all_levels); j++) {
            g_value_set_static_string (&val, all_levels[j]);
            gst_value_list_prepend_value (&levels, &val);
            if (!strcmp (level, all_levels[j]))
              break;
          }
          gst_structure_take_value (new_s, "level", &levels);
        }
      } else {
        GST_WARNING_OBJECT (payload, "invalid level-id %d in caps", value);
      }
    } else {
      GST_DEBUG_OBJECT (payload, "no valid level-id in caps");
    }

    caps = gst_caps_merge_structure (caps, new_s);
  }

  icaps = gst_caps_intersect (caps, template_caps);
  gst_caps_unref (caps);
  caps = icaps;

done:

  if (filter) {
    GstCaps *tmp;

    GST_DEBUG_OBJECT (payload, "Intersect %" GST_PTR_FORMAT " and filter %"
        GST_PTR_FORMAT, caps, filter);
    tmp = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    caps = tmp;
  }

  gst_caps_unref (template_caps);
  gst_caps_unref (allowed_caps);

  GST_LOG_OBJECT (payload, "returning caps %" GST_PTR_FORMAT, caps);
  return caps;
}

/* take the currently configured VPS, SPS and PPS lists and set them on the
 * caps */
static gboolean
gst_rtp_h265_pay_set_vps_sps_pps (GstRTPBasePayload * basepayload)
{
  GstRtpH265Pay *payloader = GST_RTP_H265_PAY (basepayload);
  gchar *set;
  GString *vps;
  GString *sps;
  GString *pps;
  guint count;
  gboolean res;
  GstMapInfo map;
  guint i;

  vps = g_string_new ("");
  sps = g_string_new ("");
  pps = g_string_new ("");
  count = 0;

  for (i = 0; i < payloader->vps->len; i++) {
    GstBuffer *vps_buf =
        GST_BUFFER_CAST (g_ptr_array_index (payloader->vps, i));

    gst_buffer_map (vps_buf, &map, GST_MAP_READ);
    set = g_base64_encode (map.data, map.size);
    gst_buffer_unmap (vps_buf, &map);

    g_string_append_printf (vps, "%s%s", i ? "," : "", set);
    g_free (set);
    count++;
  }
  for (i = 0; i < payloader->sps->len; i++) {
    GstBuffer *sps_buf =
        GST_BUFFER_CAST (g_ptr_array_index (payloader->sps, i));

    gst_buffer_map (sps_buf, &map, GST_MAP_READ);
    set = g_base64_encode (map.data, map.size);
    gst_buffer_unmap (sps_buf, &map);

    g_string_append_printf (sps, "%s%s", i ? "," : "", set);
    g_free (set);
    count++;
  }
  for (i = 0; i < payloader->pps->len; i++) {
    GstBuffer *pps_buf =
        GST_BUFFER_CAST (g_ptr_array_index (payloader->pps, i));

    gst_buffer_map (pps_buf, &map, GST_MAP_READ);
    set = g_base64_encode (map.data, map.size);
    gst_buffer_unmap (pps_buf, &map);

    g_string_append_printf (pps, "%s%s", i ? "," : "", set);
    g_free (set);
    count++;
  }

  if (G_LIKELY (count)) {
    /* combine into output caps */
    res = gst_rtp_base_payload_set_outcaps (basepayload,
        "sprop-vps", G_TYPE_STRING, vps->str,
        "sprop-sps", G_TYPE_STRING, sps->str,
        "sprop-pps", G_TYPE_STRING, pps->str, NULL);
  } else {
    res = gst_rtp_base_payload_set_outcaps (basepayload, NULL);
  }
  g_string_free (vps, TRUE);
  g_string_free (sps, TRUE);
  g_string_free (pps, TRUE);

  return res;
}


static gboolean
gst_rtp_h265_pay_setcaps (GstRTPBasePayload * basepayload, GstCaps * caps)
{
  GstRtpH265Pay *rtph265pay;
  GstStructure *str;
  const GValue *value;
  GstMapInfo map;
  guint8 *data;
  gsize size;
  GstBuffer *buffer;
  const gchar *alignment, *stream_format;
  guint8 num_arrays;

  rtph265pay = GST_RTP_H265_PAY (basepayload);

  str = gst_caps_get_structure (caps, 0);

  /* we can only set the output caps when we found the sprops and profile
   * NALs */
  gst_rtp_base_payload_set_options (basepayload, "video", TRUE, "H265", 90000);

  rtph265pay->alignment = GST_H265_ALIGNMENT_UNKNOWN;
  alignment = gst_structure_get_string (str, "alignment");
  if (alignment) {
    if (g_str_equal (alignment, "au"))
      rtph265pay->alignment = GST_H265_ALIGNMENT_AU;
    if (g_str_equal (alignment, "nal"))
      rtph265pay->alignment = GST_H265_ALIGNMENT_NAL;
  }

  rtph265pay->stream_format = GST_H265_STREAM_FORMAT_UNKNOWN;
  stream_format = gst_structure_get_string (str, "stream-format");
  if (stream_format) {
    if (g_str_equal (stream_format, "hvc1"))
      rtph265pay->stream_format = GST_H265_STREAM_FORMAT_HVC1;
    if (g_str_equal (stream_format, "hev1"))
      rtph265pay->stream_format = GST_H265_STREAM_FORMAT_HEV1;
    if (g_str_equal (stream_format, "byte-stream"))
      rtph265pay->stream_format = GST_H265_STREAM_FORMAT_BYTESTREAM;
  }

  /* packetized HEVC video has a codec_data */
  if ((value = gst_structure_get_value (str, "codec_data"))) {
    guint num_vps, num_sps, num_pps;
    gint i, j, nal_size;

    GST_DEBUG_OBJECT (rtph265pay, "have packetized h265");

    buffer = gst_value_get_buffer (value);

    gst_buffer_map (buffer, &map, GST_MAP_READ);
    data = map.data;
    size = map.size;

    /* parse the hevcC data */
    if (size < 23)
      goto hevcc_too_small;
    /* HEVCDecoderConfigurationVersion (must be 1) */
    if (data[0] != 1)
      goto wrong_version;

    /* profile_space | tier_flag | profile_idc */
    GST_DEBUG_OBJECT (rtph265pay, "profile %06x", data[1]);

    /* profile_compatibility_flags */
    for (i = 2; i < 6; i++) {
      for (j = 7; j >= 0; j--) {
        GST_DEBUG_OBJECT (rtph265pay, "profile_compatibility_flag %06x",
            (data[i] >> j) & 1);
      }
    }

    GST_DEBUG_OBJECT (rtph265pay, "progressive_source_flag %06x",
        (data[6] >> 7) & 1);
    GST_DEBUG_OBJECT (rtph265pay, "interlaced_source_flag %06x",
        (data[6] >> 6) & 1);
    GST_DEBUG_OBJECT (rtph265pay, "non_packed_constraint_flag %06x",
        (data[6] >> 5) & 1);
    GST_DEBUG_OBJECT (rtph265pay, "frame_only_constraint_flag %06x",
        (data[6] >> 4) & 1);

    GST_DEBUG_OBJECT (rtph265pay, "level_idc %06x", data[12]);

    GST_DEBUG_OBJECT (rtph265pay, "min_spatial_segmentation_idc %06x",
        ((data[13] ^ 0xf0) << 8) + data[14]);
    GST_DEBUG_OBJECT (rtph265pay, "parrallelismType %06x (ignored by paloader)",
        data[15]);

    GST_DEBUG_OBJECT (rtph265pay, "sps_chroma_format_idc %06x",
        data[16] ^ 0xfc);
    GST_DEBUG_OBJECT (rtph265pay, "bit_depth_luma_minus8 %06x",
        data[17] ^ 0xf8);
    GST_DEBUG_OBJECT (rtph265pay, "bit_depth_chroma_minus8 %06x",
        data[18] ^ 0xf8);
    GST_DEBUG_OBJECT (rtph265pay, "avgFrameRate %06x", data[19]);
    GST_DEBUG_OBJECT (rtph265pay, "avgFrameRate %06x", data[20]);

    /* constFrameRate(2 bits): 0, stream may or may not be of constant framerate
     * numTemporalLayers (3 bits): number of temporal layers, value from SPS
     * TemporalIdNested (1 bit): sps_temporal_id_nesting_flag from SPS
     * lengthSizeMinusOne (2 bits): plus 1 indicates the length of the NALUnitLength */
    GST_DEBUG_OBJECT (rtph265pay, "constFrameRate %06x",
        (data[21] >> 6) & 0x03);
    GST_DEBUG_OBJECT (rtph265pay, "numTemporalLayers %06x",
        (data[21] >> 3) & 0x07);
    GST_DEBUG_OBJECT (rtph265pay, "temporal_id_nesting_flag %06x",
        (data[21] >> 2) & 0x01);

    rtph265pay->nal_length_size = (data[21] & 0x3) + 1;
    GST_DEBUG_OBJECT (rtph265pay, "nal length %u", rtph265pay->nal_length_size);

    num_arrays = GST_READ_UINT8 (data + 22);

    data += 23;
    size -= 23;

    if (num_arrays > 0) {
      if ((data[0] & 0x3f) == 0x20) {   /* VPS */

        data++;
        num_vps = data[0] << 8 | data[1];
        data += 2;
        size -= 2;

        for (i = 0; i < num_vps; i++) {

          GstBuffer *vps_buf;

          if (size < 2)
            goto hevcc_error;

          nal_size = (data[0] << 8) | data[1];
          data += 2;
          size -= 2;

          GST_LOG_OBJECT (rtph265pay, "VPS %d size %d", i, nal_size);

          if (size < nal_size)
            goto hevcc_error;

          /* make a buffer out of it and add to VPS list */
          vps_buf = gst_buffer_new_and_alloc (nal_size);
          gst_buffer_fill (vps_buf, 0, data, nal_size);
          gst_rtp_h265_add_vps_sps_pps (GST_ELEMENT (rtph265pay),
              rtph265pay->vps, rtph265pay->sps, rtph265pay->pps, vps_buf);
          data += nal_size;
          size -= nal_size;
        }
      }

      --num_arrays;
    }

    if (num_arrays > 0) {
      if ((data[0] & 0x3f) == 0x21) {   /* SPS */

        data++;
        num_sps = data[0] << 8 | data[1];
        data += 2;
        size -= 2;

        for (i = 0; i < num_sps; i++) {

          GstBuffer *sps_buf;

          if (size < 2)
            goto hevcc_error;

          nal_size = (data[0] << 8) | data[1];
          data += 2;
          size -= 2;

          GST_LOG_OBJECT (rtph265pay, "SPS %d size %d", i, nal_size);

          if (size < nal_size)
            goto hevcc_error;

          /* make a buffer out of it and add to SPS list */
          sps_buf = gst_buffer_new_and_alloc (nal_size);
          gst_buffer_fill (sps_buf, 0, data, nal_size);
          gst_rtp_h265_add_vps_sps_pps (GST_ELEMENT (rtph265pay),
              rtph265pay->vps, rtph265pay->sps, rtph265pay->pps, sps_buf);
          data += nal_size;
          size -= nal_size;
        }
      }

      --num_arrays;
    }

    if (num_arrays > 0) {
      if ((data[0] & 0x3f) == 0x22) {   /* PPS */

        data++;
        num_pps = data[0] << 8 | data[1];
        data += 2;
        size -= 2;

        for (i = 0; i < num_pps; i++) {

          GstBuffer *pps_buf;

          if (size < 2)
            goto hevcc_error;

          nal_size = (data[0] << 8) | data[1];
          data += 2;
          size -= 2;

          GST_LOG_OBJECT (rtph265pay, "PPS %d size %d", i, nal_size);

          if (size < nal_size)
            goto hevcc_error;

          /* make a buffer out of it and add to PPS list */
          pps_buf = gst_buffer_new_and_alloc (nal_size);
          gst_buffer_fill (pps_buf, 0, data, nal_size);
          gst_rtp_h265_add_vps_sps_pps (GST_ELEMENT (rtph265pay),
              rtph265pay->vps, rtph265pay->sps, rtph265pay->pps, pps_buf);
          data += nal_size;
          size -= nal_size;
        }
      }

      --num_arrays;
    }

    /* and update the caps with the collected data */
    if (!gst_rtp_h265_pay_set_vps_sps_pps (basepayload))
      goto set_vps_sps_pps_failed;

    GST_DEBUG_OBJECT (rtph265pay, "Caps have been set");

    gst_buffer_unmap (buffer, &map);
  } else {
    GST_DEBUG_OBJECT (rtph265pay, "have bytestream h265");
  }

  return TRUE;

hevcc_too_small:
  {
    GST_ERROR_OBJECT (rtph265pay, "hevcC size %" G_GSIZE_FORMAT " < 7", size);
    goto error;
  }
wrong_version:
  {
    GST_ERROR_OBJECT (rtph265pay, "wrong hevcC version");
    goto error;
  }
hevcc_error:
  {
    GST_ERROR_OBJECT (rtph265pay, "hevcC too small ");
    goto error;
  }
set_vps_sps_pps_failed:
  {
    GST_ERROR_OBJECT (rtph265pay, "failed to set vps/sps/pps");
    goto error;
  }
error:
  {
    gst_buffer_unmap (buffer, &map);
    return FALSE;
  }
}

static guint
next_start_code (const guint8 * data, guint size)
{
  /* Boyer-Moore string matching algorithm, in a degenerative
   * sense because our search 'alphabet' is binary - 0 & 1 only.
   * This allow us to simplify the general BM algorithm to a very
   * simple form. */
  /* assume 1 is in the 3th byte */
  guint offset = 2;

  while (offset < size) {
    if (1 == data[offset]) {
      unsigned int shift = offset;

      if (0 == data[--shift]) {
        if (0 == data[--shift]) {
          return shift;
        }
      }
      /* The jump is always 3 because of the 1 previously matched.
       * All the 0's must be after this '1' matched at offset */
      offset += 3;
    } else if (0 == data[offset]) {
      /* maybe next byte is 1? */
      offset++;
    } else {
      /* can jump 3 bytes forward */
      offset += 3;
    }
    /* at each iteration, we rescan in a backward manner until
     * we match 0.0.1 in reverse order. Since our search string
     * has only 2 'alpabets' (i.e. 0 & 1), we know that any
     * mismatch will force us to shift a fixed number of steps */
  }
  GST_DEBUG ("Cannot find next NAL start code. returning %u", size);

  return size;
}

static gboolean
gst_rtp_h265_pay_decode_nal (GstRtpH265Pay * payloader,
    const guint8 * data, guint size, GstClockTime dts, GstClockTime pts)
{
  guint8 type;
  gboolean updated;

  /* default is no update */
  updated = FALSE;

  GST_DEBUG_OBJECT (payloader, "NAL payload size %u", size);

  type = (data[0] >> 1) & 0x3f;

  /* We record the timestamp of the last SPS/PPS so
   * that we can insert them at regular intervals and when needed. */
  if (GST_H265_NAL_VPS == type || GST_H265_NAL_SPS == type
      || GST_H265_NAL_PPS == type) {
    GstBuffer *nal;

    /* encode the entire NAL in base64 */
    GST_DEBUG_OBJECT (payloader, "found %s (type 0x%x), size %u",
        type == GST_H265_NAL_VPS ? "VPS" : type == GST_H265_NAL_SPS ?
        "SPS" : "PPS", type, size);

    nal = gst_buffer_new_allocate (NULL, size, NULL);
    gst_buffer_fill (nal, 0, data, size);

    updated = gst_rtp_h265_add_vps_sps_pps (GST_ELEMENT (payloader),
        payloader->vps, payloader->sps, payloader->pps, nal);

    /* remember when we last saw VPS */
    if (updated && pts != -1)
      payloader->last_vps_sps_pps = pts;
  } else {
    GST_DEBUG_OBJECT (payloader, "NALU type 0x%x, size %u", type, size);
  }

  return updated;
}

static GstFlowReturn
gst_rtp_h265_pay_payload_nal (GstRTPBasePayload * basepayload,
    GPtrArray * paybufs, GstClockTime dts, GstClockTime pts);

static GstFlowReturn
gst_rtp_h265_pay_send_vps_sps_pps (GstRTPBasePayload * basepayload,
    GstRtpH265Pay * rtph265pay, GstClockTime dts, GstClockTime pts)
{
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean sent_all_vps_sps_pps = TRUE;
  guint i;
  GPtrArray *bufs;

  bufs = g_ptr_array_new ();

  for (i = 0; i < rtph265pay->vps->len; i++) {
    GstBuffer *vps_buf =
        GST_BUFFER_CAST (g_ptr_array_index (rtph265pay->vps, i));

    GST_DEBUG_OBJECT (rtph265pay, "inserting VPS in the stream");
    g_ptr_array_add (bufs, gst_buffer_ref (vps_buf));
  }
  for (i = 0; i < rtph265pay->sps->len; i++) {
    GstBuffer *sps_buf =
        GST_BUFFER_CAST (g_ptr_array_index (rtph265pay->sps, i));

    GST_DEBUG_OBJECT (rtph265pay, "inserting SPS in the stream");
    g_ptr_array_add (bufs, gst_buffer_ref (sps_buf));
  }
  for (i = 0; i < rtph265pay->pps->len; i++) {
    GstBuffer *pps_buf =
        GST_BUFFER_CAST (g_ptr_array_index (rtph265pay->pps, i));

    GST_DEBUG_OBJECT (rtph265pay, "inserting PPS in the stream");
    g_ptr_array_add (bufs, gst_buffer_ref (pps_buf));
  }

  ret = gst_rtp_h265_pay_payload_nal (basepayload, bufs, dts, pts);
  if (ret != GST_FLOW_OK) {
    /* not critical but warn */
    GST_WARNING_OBJECT (basepayload, "failed pushing VPS/SPS/PPS");

    sent_all_vps_sps_pps = FALSE;
  }

  if (pts != -1 && sent_all_vps_sps_pps)
    rtph265pay->last_vps_sps_pps = pts;

  return ret;
}

static GstFlowReturn
gst_rtp_h265_pay_payload_nal (GstRTPBasePayload * basepayload,
    GPtrArray * paybufs, GstClockTime dts, GstClockTime pts)
{
  GstRtpH265Pay *rtph265pay;
  guint mtu;
  GstFlowReturn ret;
  gint i;
  gboolean sent_ps;

  rtph265pay = GST_RTP_H265_PAY (basepayload);
  mtu = GST_RTP_BASE_PAYLOAD_MTU (rtph265pay);

  /* should set src caps before pushing stuff,
   * and if we did not see enough VPS/SPS/PPS, that may not be the case */
  if (G_UNLIKELY (!gst_pad_has_current_caps (GST_RTP_BASE_PAYLOAD_SRCPAD
              (basepayload))))
    gst_rtp_h265_pay_set_vps_sps_pps (basepayload);

  ret = GST_FLOW_OK;
  sent_ps = FALSE;
  for (i = 0; i < paybufs->len; i++) {
    guint8 nalHeader[2];
    guint8 nalType;
    guint packet_len, payload_len;
    GstBuffer *paybuf;
    GstBuffer *outbuf;
    guint8 *payload;
    GstBufferList *outlist = NULL;
    gboolean send_ps;
    GstRTPBuffer rtp = { NULL };
    guint size;

    paybuf = g_ptr_array_index (paybufs, i);

    if (ret != GST_FLOW_OK) {
      /* unref buffers that will not be payloaded after a flow error */
      gst_buffer_unref (paybuf);
      continue;
    }

    size = gst_buffer_get_size (paybuf);
    gst_buffer_extract (paybuf, 0, nalHeader, 2);
    nalType = (nalHeader[0] >> 1) & 0x3f;

    GST_DEBUG_OBJECT (rtph265pay, "Processing Buffer with NAL TYPE=%d",
        nalType);

    send_ps = FALSE;

    /* check if we need to emit an VPS/SPS/PPS now */
    if ((nalType == GST_H265_NAL_SLICE_TRAIL_N)
        || (nalType == GST_H265_NAL_SLICE_TRAIL_R)
        || (nalType == GST_H265_NAL_SLICE_TSA_N)
        || (nalType == GST_H265_NAL_SLICE_TSA_R)
        || (nalType == GST_H265_NAL_SLICE_STSA_N)
        || (nalType == GST_H265_NAL_SLICE_STSA_R)
        || (nalType == GST_H265_NAL_SLICE_RASL_N)
        || (nalType == GST_H265_NAL_SLICE_RASL_R)
        || (nalType == GST_H265_NAL_SLICE_BLA_W_LP)
        || (nalType == GST_H265_NAL_SLICE_BLA_W_RADL)
        || (nalType == GST_H265_NAL_SLICE_BLA_N_LP)
        || (nalType == GST_H265_NAL_SLICE_IDR_W_RADL)
        || (nalType == GST_H265_NAL_SLICE_IDR_N_LP)
        || (nalType == GST_H265_NAL_SLICE_CRA_NUT)) {
      if (rtph265pay->vps_sps_pps_interval > 0) {
        if (rtph265pay->last_vps_sps_pps != -1) {
          guint64 diff;

          GST_LOG_OBJECT (rtph265pay,
              "now %" GST_TIME_FORMAT ", last VPS/SPS/PPS %" GST_TIME_FORMAT,
              GST_TIME_ARGS (pts),
              GST_TIME_ARGS (rtph265pay->last_vps_sps_pps));

          /* calculate diff between last SPS/PPS in milliseconds */
          if (pts > rtph265pay->last_vps_sps_pps)
            diff = pts - rtph265pay->last_vps_sps_pps;
          else
            diff = 0;

          GST_DEBUG_OBJECT (rtph265pay,
              "interval since last VPS/SPS/PPS %" GST_TIME_FORMAT,
              GST_TIME_ARGS (diff));

          /* bigger than interval, queue SPS/PPS */
          if (GST_TIME_AS_SECONDS (diff) >= rtph265pay->vps_sps_pps_interval) {
            GST_DEBUG_OBJECT (rtph265pay, "time to send VPS/SPS/PPS");
            send_ps = TRUE;
          }
        } else {
          /* no known previous SPS/PPS time, send now */
          GST_DEBUG_OBJECT (rtph265pay,
              "no previous VPS/SPS/PPS time, send now");
          send_ps = TRUE;
        }
      } else if (rtph265pay->vps_sps_pps_interval == -1
          && (nalType == GST_H265_NAL_SLICE_IDR_W_RADL
              || nalType == GST_H265_NAL_SLICE_IDR_N_LP)) {
        /* send VPS/SPS/PPS before every IDR frame */
        send_ps = TRUE;
      }
    }

    if (!sent_ps && (send_ps || rtph265pay->send_vps_sps_pps)) {
      /* we need to send SPS/PPS now first. FIXME, don't use the pts for
       * checking when we need to send SPS/PPS but convert to running_time
       * first */
      rtph265pay->send_vps_sps_pps = FALSE;
      sent_ps = TRUE;
      GST_DEBUG_OBJECT (rtph265pay, "sending VPS/SPS/PPS before current frame");
      ret =
          gst_rtp_h265_pay_send_vps_sps_pps (basepayload, rtph265pay, dts, pts);
      if (ret != GST_FLOW_OK) {
        gst_buffer_unref (paybuf);
        continue;
      }
    }

    packet_len = gst_rtp_buffer_calc_packet_len (size, 0, 0);

    if (packet_len < mtu) {
      GST_DEBUG_OBJECT (rtph265pay,
          "NAL Unit fit in one packet datasize=%d mtu=%d", size, mtu);
      /* will fit in one packet */

      /* use buffer lists
       * create buffer without payload containing only the RTP header
       * (memory block at index 0) */
      outbuf = gst_rtp_buffer_new_allocate (0, 0, 0);

      gst_rtp_buffer_map (outbuf, GST_MAP_WRITE, &rtp);

      /* only set the marker bit on packets containing access units */
      if (i == paybufs->len - 1
          && rtph265pay->alignment == GST_H265_ALIGNMENT_AU
          && IS_ACCESS_UNIT (nalType)) {
        gst_rtp_buffer_set_marker (&rtp, 1);
      }

      /* timestamp the outbuffer */
      GST_BUFFER_PTS (outbuf) = pts;
      GST_BUFFER_DTS (outbuf) = dts;

      /* insert payload memory block */
      gst_rtp_copy_meta (GST_ELEMENT_CAST (rtph265pay), outbuf, paybuf,
          g_quark_from_static_string (GST_META_TAG_VIDEO_STR));
      outbuf = gst_buffer_append (outbuf, paybuf);

      outlist = gst_buffer_list_new ();

      /* add the buffer to the buffer list */
      gst_buffer_list_add (outlist, outbuf);

      gst_rtp_buffer_unmap (&rtp);

      /* push the list to the next element in the pipe */
      ret = gst_rtp_base_payload_push_list (basepayload, outlist);
    } else {
      /* fragmentation Units */
      guint limitedSize;
      int ii = 0, start = 1, end = 0, pos = 0;

      GST_DEBUG_OBJECT (basepayload,
          "NAL Unit DOES NOT fit in one packet datasize=%d mtu=%d", size, mtu);

      pos += 2;
      size -= 2;

      GST_DEBUG_OBJECT (basepayload, "Using FU fragmentation for data size=%d",
          size);

      /* We keep 3 bytes for PayloadHdr and FU Header */
      payload_len = gst_rtp_buffer_calc_payload_len (mtu - 3, 0, 0);

      outlist = gst_buffer_list_new ();

      while (end == 0) {
        limitedSize = size < payload_len ? size : payload_len;
        GST_DEBUG_OBJECT (basepayload,
            "Inside  FU fragmentation limitedSize=%d iteration=%d", limitedSize,
            ii);

        /* use buffer lists
         * create buffer without payload containing only the RTP header
         * (memory block at index 0), and with space for PayloadHdr and FU header */
        outbuf = gst_rtp_buffer_new_allocate (3, 0, 0);

        gst_rtp_buffer_map (outbuf, GST_MAP_WRITE, &rtp);

        GST_BUFFER_DTS (outbuf) = dts;
        GST_BUFFER_PTS (outbuf) = pts;
        payload = gst_rtp_buffer_get_payload (&rtp);

        if (limitedSize == size) {
          GST_DEBUG_OBJECT (basepayload, "end size=%d iteration=%d", size, ii);
          end = 1;
        }

        /* PayloadHdr (type = 49) */
        payload[0] = (nalHeader[0] & 0x81) | (49 << 1);
        payload[1] = nalHeader[1];

        /* set the marker bit on the last packet of an access unit */
        if (IS_ACCESS_UNIT (nalType)) {
          gst_rtp_buffer_set_marker (&rtp,
              end && i == paybufs->len - 1
              && rtph265pay->alignment == GST_H265_ALIGNMENT_AU);
        }

        /* FU Header */
        payload[2] = (start << 7) | (end << 6) | (nalType & 0x3f);

        gst_rtp_buffer_unmap (&rtp);

        /* insert payload memory block */
        gst_rtp_copy_meta (GST_ELEMENT_CAST (rtph265pay), outbuf, paybuf,
            g_quark_from_static_string (GST_META_TAG_VIDEO_STR));
        gst_buffer_copy_into (outbuf, paybuf, GST_BUFFER_COPY_MEMORY, pos,
            limitedSize);
        /* add the buffer to the buffer list */
        gst_buffer_list_add (outlist, outbuf);

        size -= limitedSize;
        pos += limitedSize;
        ii++;
        start = 0;
      }

      ret = gst_rtp_base_payload_push_list (basepayload, outlist);
      gst_buffer_unref (paybuf);
    }
  }

  g_ptr_array_free (paybufs, TRUE);

  return ret;
}

static GstFlowReturn
gst_rtp_h265_pay_handle_buffer (GstRTPBasePayload * basepayload,
    GstBuffer * buffer)
{
  GstRtpH265Pay *rtph265pay;
  GstFlowReturn ret;
  gsize size;
  guint nal_len, i;
  GstMapInfo map;
  const guint8 *data;
  GstClockTime dts, pts;
  GArray *nal_queue;
  gboolean hevc;
  GstBuffer *paybuf = NULL;
  gsize skip;

  rtph265pay = GST_RTP_H265_PAY (basepayload);

  /* the input buffer contains one or more NAL units */

  hevc = (rtph265pay->stream_format == GST_H265_STREAM_FORMAT_HEV1)
      || (rtph265pay->stream_format == GST_H265_STREAM_FORMAT_HVC1);

  if (hevc) {
    /* In hevc mode, there is no adapter, so nothing to flush */
    if (buffer == NULL)
      return GST_FLOW_OK;
    gst_buffer_map (buffer, &map, GST_MAP_READ);
    data = map.data;
    size = map.size;
    pts = GST_BUFFER_PTS (buffer);
    dts = GST_BUFFER_DTS (buffer);
    GST_DEBUG_OBJECT (basepayload, "got %" G_GSIZE_FORMAT " bytes", size);
  } else {
    dts = gst_adapter_prev_dts (rtph265pay->adapter, NULL);
    pts = gst_adapter_prev_pts (rtph265pay->adapter, NULL);
    if (buffer) {
      if (!GST_CLOCK_TIME_IS_VALID (dts))
        dts = GST_BUFFER_DTS (buffer);
      if (!GST_CLOCK_TIME_IS_VALID (pts))
        pts = GST_BUFFER_PTS (buffer);

      gst_adapter_push (rtph265pay->adapter, buffer);
    }
    size = gst_adapter_available (rtph265pay->adapter);
    /* Nothing to do here if the adapter is empty, e.g. on EOS */
    if (size == 0)
      return GST_FLOW_OK;
    data = gst_adapter_map (rtph265pay->adapter, size);
    GST_DEBUG_OBJECT (basepayload,
        "got %" G_GSIZE_FORMAT " bytes (%" G_GSIZE_FORMAT ")", size,
        buffer ? gst_buffer_get_size (buffer) : 0);
  }

  ret = GST_FLOW_OK;

  /* now loop over all NAL units and put them in a packet
   * FIXME, we should really try to pack multiple NAL units into one RTP packet
   * if we can, especially for the config packets that wont't cause decoder
   * latency. */
  if (hevc) {
    guint nal_length_size;
    gsize offset = 0;
    GPtrArray *paybufs;

    paybufs = g_ptr_array_new ();
    nal_length_size = rtph265pay->nal_length_size;

    while (size > nal_length_size) {
      gint i;

      nal_len = 0;
      for (i = 0; i < nal_length_size; i++) {
        nal_len = ((nal_len << 8) + data[i]);
      }

      /* skip the length bytes, make sure we don't run past the buffer size */
      data += nal_length_size;
      offset += nal_length_size;
      size -= nal_length_size;

      if (size >= nal_len) {
        GST_DEBUG_OBJECT (basepayload, "got NAL of size %u", nal_len);
      } else {
        nal_len = size;
        GST_DEBUG_OBJECT (basepayload, "got incomplete NAL of size %u",
            nal_len);
      }

      paybuf = gst_buffer_copy_region (buffer, GST_BUFFER_COPY_ALL, offset,
          nal_len);
      g_ptr_array_add (paybufs, paybuf);

      data += nal_len;
      offset += nal_len;
      size -= nal_len;
    }
    ret = gst_rtp_h265_pay_payload_nal (basepayload, paybufs, dts, pts);
  } else {
    guint next;
    gboolean update = FALSE;
    GPtrArray *paybufs;

    /* get offset of first start code */
    next = next_start_code (data, size);

    /* skip to start code, if no start code is found, next will be size and we
     * will not collect data. */
    data += next;
    size -= next;
    nal_queue = rtph265pay->queue;
    skip = next;

    /* array must be empty when we get here */
    g_assert (nal_queue->len == 0);

    GST_DEBUG_OBJECT (basepayload,
        "found first start at %u, bytes left %" G_GSIZE_FORMAT, next, size);

    paybufs = g_ptr_array_new ();

    /* first pass to locate NALs and parse VPS/SPS/PPS */
    while (size > 4) {
      /* skip start code */
      data += 3;
      size -= 3;

      /* use next_start_code() to scan buffer.
       * next_start_code() returns the offset in data,
       * starting from zero to the first byte of 0.0.0.1
       * If no start code is found, it returns the value of the
       * 'size' parameter.
       * data is unchanged by the call to next_start_code()
       */
      next = next_start_code (data, size);

      if (next == size && buffer != NULL) {
        /* Didn't find the start of next NAL and it's not EOS,
         * handle it next time */
        break;
      }

      /* nal length is distance to next start code */
      nal_len = next;

      GST_DEBUG_OBJECT (basepayload, "found next start at %u of size %u", next,
          nal_len);

      /* We know our stream is a valid H265 NAL packet,
       * go parse it for VPS/SPS/PPS to enrich the caps */
      /* order: make sure to check nal */
      update = gst_rtp_h265_pay_decode_nal (rtph265pay, data, nal_len, dts, pts)
          || update;

      /* move to next NAL packet */
      data += nal_len;
      size -= nal_len;

      g_array_append_val (nal_queue, nal_len);
    }

    /* if has new VPS, SPS & PPS, update the output caps */
    if (G_UNLIKELY (update))
      if (!gst_rtp_h265_pay_set_vps_sps_pps (basepayload))
        goto caps_rejected;

    /* second pass to payload and push */

    if (nal_queue->len != 0)
      gst_adapter_flush (rtph265pay->adapter, skip);

    for (i = 0; i < nal_queue->len; i++) {
      guint size;

      nal_len = g_array_index (nal_queue, guint, i);
      /* skip start code */
      gst_adapter_flush (rtph265pay->adapter, 3);

      /* Trim the end unless we're the last NAL in the stream.
       * In case we're not at the end of the buffer we know the next block
       * starts with 0x000001 so all the 0x00 bytes at the end of this one are
       * trailing 0x0 that can be discarded */
      size = nal_len;
      data = gst_adapter_map (rtph265pay->adapter, size);
      if (i + 1 != nal_queue->len || buffer != NULL)
        for (; size > 1 && data[size - 1] == 0x0; size--)
          /* skip */ ;

      /* FIXME: We need to wait until the next packet or EOS to
       * actually payload the NAL so we can know if the current NAL is
       * the last one of an access unit or not if we are in bytestream mode
       */

      paybuf = gst_adapter_take_buffer (rtph265pay->adapter, size);
      g_assert (paybuf);
      g_ptr_array_add (paybufs, paybuf);

      /* move to next NAL packet */
      /* Skips the trailing zeros */
      gst_adapter_flush (rtph265pay->adapter, nal_len - size);
    }
    /* put the data in one or more RTP packets */
    ret = gst_rtp_h265_pay_payload_nal (basepayload, paybufs, dts, pts);
    g_array_set_size (nal_queue, 0);
  }

done:
  if (hevc) {
    gst_buffer_unmap (buffer, &map);
    gst_buffer_unref (buffer);
  } else {
    gst_adapter_unmap (rtph265pay->adapter);
  }

  return ret;

caps_rejected:
  {
    GST_WARNING_OBJECT (basepayload, "Could not set outcaps");
    g_array_set_size (nal_queue, 0);
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto done;
  }
}

static gboolean
gst_rtp_h265_pay_sink_event (GstRTPBasePayload * payload, GstEvent * event)
{
  gboolean res;
  const GstStructure *s;
  GstRtpH265Pay *rtph265pay = GST_RTP_H265_PAY (payload);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      gst_adapter_clear (rtph265pay->adapter);
      break;
    case GST_EVENT_CUSTOM_DOWNSTREAM:
      s = gst_event_get_structure (event);
      if (gst_structure_has_name (s, "GstForceKeyUnit")) {
        gboolean resend_codec_data;

        if (gst_structure_get_boolean (s, "all-headers",
                &resend_codec_data) && resend_codec_data)
          rtph265pay->send_vps_sps_pps = TRUE;
      }
      break;
    case GST_EVENT_EOS:
    {
      /* call handle_buffer with NULL to flush last NAL from adapter
       * in byte-stream mode
       */
      gst_rtp_h265_pay_handle_buffer (payload, NULL);
      break;
    }
    case GST_EVENT_STREAM_START:
      GST_DEBUG_OBJECT (rtph265pay,
          "New stream detected => Clear VPS, SPS and PPS");
      gst_rtp_h265_pay_clear_vps_sps_pps (rtph265pay);
      break;
    default:
      break;
  }

  res = GST_RTP_BASE_PAYLOAD_CLASS (parent_class)->sink_event (payload, event);

  return res;
}

static GstStateChangeReturn
gst_rtp_h265_pay_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstRtpH265Pay *rtph265pay = GST_RTP_H265_PAY (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      rtph265pay->send_vps_sps_pps = FALSE;
      gst_adapter_clear (rtph265pay->adapter);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      rtph265pay->last_vps_sps_pps = -1;
      gst_rtp_h265_pay_clear_vps_sps_pps (rtph265pay);
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_rtp_h265_pay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpH265Pay *rtph265pay;

  rtph265pay = GST_RTP_H265_PAY (object);

  switch (prop_id) {
    case PROP_CONFIG_INTERVAL:
      rtph265pay->vps_sps_pps_interval = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_h265_pay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRtpH265Pay *rtph265pay;

  rtph265pay = GST_RTP_H265_PAY (object);

  switch (prop_id) {
    case PROP_CONFIG_INTERVAL:
      g_value_set_int (value, rtph265pay->vps_sps_pps_interval);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_rtp_h265_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtph265pay",
      GST_RANK_SECONDARY, GST_TYPE_RTP_H265_PAY);
}
