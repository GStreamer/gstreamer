/* GStreamer H.266 Parse
 *
 * Copyright (C) 2024 Intel Corporation
 *    Author: He Junyan <junyan.he@intel.com>
 *    Author: Zhong Hongcheng <spartazhc@gmail.com>
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
/*
 * SECTION:element-h266parse
 * @title: h266parse
 * @short_description: The H266/VVC stream parse.
 *
 * `h266parse` can detect and parse the h266/VVC NALs and implements the
 * conversion between the alignments and the stream-formats.
 *
 * The alignments can be: nal and au.
 * The stream-formats can be: byte-stream, vvc1 and vvi1.
 *
 * ## Example launch line:
 * ```
 * gst-launch-1.0 filesrc location=sample.h266 ! h266parse !  \
 *   video/x-h266,alignment=\(string\)au,stream-format=\(string\)byte-stream ! \
 *   filesink location=result.h266
 * ```
 *
 * Since: 1.26
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/base/base.h>
#include <gst/pbutils/pbutils.h>
#include "gstvideoparserselements.h"
#include "gsth266parse.h"

#include <string.h>

GST_DEBUG_CATEGORY (h266_parse_debug);
#define GST_CAT_DEFAULT h266_parse_debug

#define DEFAULT_CONFIG_INTERVAL      (0)

enum
{
  PROP_0,
  PROP_CONFIG_INTERVAL
};

enum
{
  GST_H266_PARSE_FORMAT_NONE,
  GST_H266_PARSE_FORMAT_VVC1,
  GST_H266_PARSE_FORMAT_VVI1,
  GST_H266_PARSE_FORMAT_BYTE
};

enum
{
  GST_H266_PARSE_ALIGN_NONE = 0,
  GST_H266_PARSE_ALIGN_NAL,
  GST_H266_PARSE_ALIGN_AU
};

enum
{
  GST_H266_PARSE_STATE_GOT_SPS = 1 << 0,
  GST_H266_PARSE_STATE_GOT_PPS = 1 << 1,
  GST_H266_PARSE_STATE_GOT_SLICE = 1 << 2,

  GST_H266_PARSE_STATE_VALID_SPS_PPS = (GST_H266_PARSE_STATE_GOT_SPS |
      GST_H266_PARSE_STATE_GOT_PPS),
  GST_H266_PARSE_STATE_VALID_PICTURE =
      (GST_H266_PARSE_STATE_VALID_SPS_PPS | GST_H266_PARSE_STATE_GOT_SLICE)
};

enum
{
  GST_H266_PARSE_SEI_EXPIRED = 0,
  GST_H266_PARSE_SEI_ACTIVE = 1,
  GST_H266_PARSE_SEI_PARSED = 2,
};

enum
{
  GST_H266_PARSE_PROGRESSIVE_ONLY = 0,
  GST_H266_PARSE_INTERLACED_ONLY = 1,
  /* Depend on frame-field-info SEI for each picture. */
  GST_H266_PARSE_FFI = 2,
};

/* *INDENT-OFF* */
struct _GstH266ParsePrivate
{
  /* Private structures are limited to a size of 64KiB
   * see https://gitlab.gnome.org/GNOME/glib/-/merge_requests/1558
   */
  struct
  {
    GstH266VPS vps;
    GstH266SPS sps;
    GstH266PPS pps;
    GstH266APS aps;
    GstH266PicHdr ph;
  } *cache;
};
/* *INDENT-ON* */

#define GST_H266_PARSE_STATE_VALID(parse, expected_state) \
  (((parse)->state & (expected_state)) == (expected_state))

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h266"));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h266, parsed = (boolean) true, "
        "stream-format=(string) { byte-stream }, "
        "alignment=(string) { au, nal }"));

#define parent_class gst_h266_parse_parent_class
G_DEFINE_TYPE_WITH_CODE (GstH266Parse, gst_h266_parse,
    GST_TYPE_BASE_PARSE, G_ADD_PRIVATE (GstH266Parse));
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (h266parse, "h266parse",
    GST_RANK_SECONDARY, GST_TYPE_H266_PARSE,
    videoparsers_element_init (plugin));

static void gst_h266_parse_finalize (GObject * object);

static gboolean gst_h266_parse_start (GstBaseParse * parse);
static gboolean gst_h266_parse_stop (GstBaseParse * parse);
static GstFlowReturn gst_h266_parse_handle_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, gint * skipsize);
static GstFlowReturn gst_h266_parse_parse_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame);
static GstFlowReturn gst_h266_parse_pre_push_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame);
static void gst_h266_parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_h266_parse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_h266_parse_set_caps (GstBaseParse * parse, GstCaps * caps);
static GstCaps *gst_h266_parse_get_caps (GstBaseParse * parse,
    GstCaps * filter);

static void
gst_h266_parse_class_init (GstH266ParseClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseParseClass *parse_class = GST_BASE_PARSE_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (h266_parse_debug, "h266parse", 0, "h266 parser");

  gobject_class->finalize = gst_h266_parse_finalize;
  gobject_class->set_property = gst_h266_parse_set_property;
  gobject_class->get_property = gst_h266_parse_get_property;

  /**
   * h266parse:config-interval:
   *
   * The interval in seconds to send the VPS, SPS and PPS insertion.
   *
   * Since: 1.26
   */
  g_object_class_install_property (gobject_class, PROP_CONFIG_INTERVAL,
      g_param_spec_int ("config-interval",
          "VPS SPS PPS Send Interval",
          "Send VPS, SPS and PPS Insertion Interval in seconds (sprop "
          "parameter sets will be multiplexed in the data stream when "
          "detected.) (0 = disabled, -1 = send with every IDR frame)",
          -1, 3600, DEFAULT_CONFIG_INTERVAL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  /* Override BaseParse vfuncs */
  parse_class->start = GST_DEBUG_FUNCPTR (gst_h266_parse_start);
  parse_class->stop = GST_DEBUG_FUNCPTR (gst_h266_parse_stop);
  parse_class->handle_frame = GST_DEBUG_FUNCPTR (gst_h266_parse_handle_frame);
  parse_class->pre_push_frame =
      GST_DEBUG_FUNCPTR (gst_h266_parse_pre_push_frame);
  parse_class->set_sink_caps = GST_DEBUG_FUNCPTR (gst_h266_parse_set_caps);
  parse_class->get_sink_caps = GST_DEBUG_FUNCPTR (gst_h266_parse_get_caps);

  gst_element_class_add_static_pad_template (gstelement_class, &srctemplate);
  gst_element_class_add_static_pad_template (gstelement_class, &sinktemplate);

  gst_element_class_set_static_metadata (gstelement_class, "H.266 parser",
      "Codec/Parser/Converter/Video",
      "Parses H.266 streams", "Hongcheng Zhong");
}

static void
gst_h266_parse_init (GstH266Parse * h266parse)
{
  h266parse->priv = gst_h266_parse_get_instance_private (h266parse);

  h266parse->priv->cache = g_malloc (sizeof (*h266parse->priv->cache));

  h266parse->frame_out = gst_adapter_new ();
  gst_base_parse_set_pts_interpolation (GST_BASE_PARSE (h266parse), FALSE);
  gst_base_parse_set_infer_ts (GST_BASE_PARSE (h266parse), FALSE);
  GST_PAD_SET_ACCEPT_INTERSECT (GST_BASE_PARSE_SINK_PAD (h266parse));
  GST_PAD_SET_ACCEPT_TEMPLATE (GST_BASE_PARSE_SINK_PAD (h266parse));
}

static void
gst_h266_parse_finalize (GObject * object)
{
  GstH266Parse *h266parse = GST_H266_PARSE (object);

  gst_video_clear_user_data_unregistered (&h266parse->user_data_unregistered,
      TRUE);

  g_object_unref (h266parse->frame_out);
  g_free (h266parse->priv->cache);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_h266_parse_reset_frame (GstH266Parse * h266parse)
{
  GST_LOG_OBJECT (h266parse, "reset frame");

  /* done parsing; reset state */
  h266parse->current_off = -1;
  h266parse->update_caps = FALSE;
  h266parse->idr_pos = -1;
  h266parse->keyframe = FALSE;
  h266parse->predicted = FALSE;
  h266parse->bidirectional = FALSE;
  h266parse->header = FALSE;
  h266parse->have_vps_in_frame = FALSE;
  h266parse->have_sps_in_frame = FALSE;
  h266parse->have_pps_in_frame = FALSE;
  h266parse->have_aps_in_frame = FALSE;
  gst_adapter_clear (h266parse->frame_out);
}

static void
gst_h266_parse_reset_stream_info (GstH266Parse * h266parse)
{
  gint i, j;

  h266parse->width = 0;
  h266parse->height = 0;
  h266parse->fps_num = 0;
  h266parse->fps_den = 0;
  h266parse->upstream_par_n = -1;
  h266parse->upstream_par_d = -1;
  h266parse->parsed_par_n = 0;
  h266parse->parsed_par_n = 0;
  h266parse->parsed_colorimetry.range = GST_VIDEO_COLOR_RANGE_UNKNOWN;
  h266parse->parsed_colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_UNKNOWN;
  h266parse->parsed_colorimetry.transfer = GST_VIDEO_TRANSFER_UNKNOWN;
  h266parse->parsed_colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_UNKNOWN;
  h266parse->have_pps = FALSE;
  h266parse->have_sps = FALSE;
  h266parse->have_vps = FALSE;
  h266parse->have_aps = FALSE;
  h266parse->align = GST_H266_PARSE_ALIGN_NONE;
  h266parse->format = GST_H266_PARSE_FORMAT_NONE;
  h266parse->transform = FALSE;
  h266parse->nal_length_size = 4;
  h266parse->packetized = FALSE;
  h266parse->push_codec = FALSE;
  h266parse->first_frame = TRUE;
  memset (&h266parse->sei_frame_field, 0, sizeof (GstH266FrameFieldInfo));
  h266parse->interlaced_mode = GST_H266_PARSE_PROGRESSIVE_ONLY;

  gst_buffer_replace (&h266parse->codec_data, NULL);
  gst_buffer_replace (&h266parse->codec_data_in, NULL);

  gst_h266_parse_reset_frame (h266parse);
  h266parse->picture_start = FALSE;

  for (i = 0; i < GST_H266_MAX_VPS_COUNT; i++)
    gst_buffer_replace (&h266parse->vps_nals[i], NULL);
  for (i = 0; i < GST_H266_MAX_SPS_COUNT; i++)
    gst_buffer_replace (&h266parse->sps_nals[i], NULL);
  for (i = 0; i < GST_H266_MAX_PPS_COUNT; i++)
    gst_buffer_replace (&h266parse->pps_nals[i], NULL);
  for (i = 0; i < GST_H266_APS_TYPE_MAX; i++) {
    for (j = 0; j < GST_H266_MAX_APS_COUNT; j++)
      gst_buffer_replace (&h266parse->aps_nals[i][j], NULL);
  }

  gst_video_mastering_display_info_init (&h266parse->mastering_display_info);
  h266parse->mastering_display_info_state = GST_H266_PARSE_SEI_EXPIRED;

  gst_video_content_light_level_init (&h266parse->content_light_level);
  h266parse->content_light_level_state = GST_H266_PARSE_SEI_EXPIRED;
}

static void
gst_h266_parse_reset (GstH266Parse * h266parse)
{
  h266parse->last_report = GST_CLOCK_TIME_NONE;

  h266parse->pending_key_unit_ts = GST_CLOCK_TIME_NONE;
  gst_event_replace (&h266parse->force_key_unit_event, NULL);

  h266parse->discont = FALSE;
  h266parse->discard_bidirectional = FALSE;
  h266parse->marker = FALSE;

  gst_h266_parse_reset_stream_info (h266parse);
}

static gboolean
gst_h266_parse_start (GstBaseParse * parse)
{
  GstH266Parse *h266parse = GST_H266_PARSE (parse);

  GST_DEBUG_OBJECT (parse, "start");
  gst_h266_parse_reset (h266parse);

  h266parse->nalparser = gst_h266_parser_new ();
  h266parse->state = 0;

  gst_base_parse_set_min_frame_size (parse, 5);

  return TRUE;
}

static gboolean
gst_h266_parse_stop (GstBaseParse * parse)
{
  GstH266Parse *h266parse = GST_H266_PARSE (parse);

  GST_DEBUG_OBJECT (parse, "stop");
  gst_h266_parse_reset (h266parse);

  gst_h266_parser_free (h266parse->nalparser);

  return TRUE;
}

static const gchar *
gst_h266_parse_get_string (GstH266Parse * parse, gboolean format, gint code)
{
  if (format) {
    switch (code) {
      case GST_H266_PARSE_FORMAT_VVC1:
        return "vvc1";
      case GST_H266_PARSE_FORMAT_VVI1:
        return "vvi1";
      case GST_H266_PARSE_FORMAT_BYTE:
        return "byte-stream";
      default:
        return "none";
    }
  } else {
    switch (code) {
      case GST_H266_PARSE_ALIGN_NAL:
        return "nal";
      case GST_H266_PARSE_ALIGN_AU:
        return "au";
      default:
        return "none";
    }
  }
}

static void
gst_h266_parse_format_from_caps (GstH266Parse * parse, GstCaps * caps,
    guint * format, guint * align)
{
  g_return_if_fail (gst_caps_is_fixed (caps));

  GST_DEBUG_OBJECT (parse, "parsing caps: %" GST_PTR_FORMAT, caps);

  if (format)
    *format = GST_H266_PARSE_FORMAT_NONE;

  if (align)
    *align = GST_H266_PARSE_ALIGN_NONE;

  if (caps && gst_caps_get_size (caps) > 0) {
    GstStructure *s = gst_caps_get_structure (caps, 0);
    const gchar *str = NULL;

    if (format) {
      if ((str = gst_structure_get_string (s, "stream-format"))) {
        if (strcmp (str, "byte-stream") == 0)
          *format = GST_H266_PARSE_FORMAT_BYTE;
        else if (strcmp (str, "vvc1") == 0)
          *format = GST_H266_PARSE_FORMAT_VVC1;
        else if (strcmp (str, "vvi1") == 0)
          *format = GST_H266_PARSE_FORMAT_VVI1;
      }
    }

    if (align) {
      if ((str = gst_structure_get_string (s, "alignment"))) {
        if (strcmp (str, "au") == 0)
          *align = GST_H266_PARSE_ALIGN_AU;
        else if (strcmp (str, "nal") == 0)
          *align = GST_H266_PARSE_ALIGN_NAL;
      }
    }
  }
}

/* check downstream caps to configure format and alignment */
static void
gst_h266_parse_negotiate (GstH266Parse * h266parse, gint in_format,
    GstCaps * in_caps)
{
  GstCaps *caps;
  guint format = GST_H266_PARSE_FORMAT_NONE;
  guint align = GST_H266_PARSE_ALIGN_NONE;

  g_return_if_fail ((in_caps == NULL) || gst_caps_is_fixed (in_caps));

  caps = gst_pad_get_allowed_caps (GST_BASE_PARSE_SRC_PAD (h266parse));
  GST_DEBUG_OBJECT (h266parse, "allowed caps: %" GST_PTR_FORMAT, caps);

  /* concentrate on leading structure, since decodebin parser
   * capsfilter always includes parser template caps */
  if (caps) {
    caps = gst_caps_truncate (caps);
    GST_DEBUG_OBJECT (h266parse, "negotiating with caps: %" GST_PTR_FORMAT,
        caps);
  }

  if (in_caps && caps) {
    if (gst_caps_can_intersect (in_caps, caps)) {
      GST_DEBUG_OBJECT (h266parse, "downstream accepts upstream caps");
      gst_h266_parse_format_from_caps (h266parse, in_caps, &format, &align);
      gst_caps_unref (caps);
      caps = NULL;
    }
  }

  /* FIXME We could fail the negotiation immediately if caps are empty */
  if (caps && !gst_caps_is_empty (caps)) {
    /* fixate to avoid ambiguity with lists when parsing */
    caps = gst_caps_fixate (caps);
    gst_h266_parse_format_from_caps (h266parse, caps, &format, &align);
  }

  /* default */
  if (!format)
    format = GST_H266_PARSE_FORMAT_BYTE;
  if (!align)
    align = GST_H266_PARSE_ALIGN_AU;

  GST_DEBUG_OBJECT (h266parse, "selected format %s, alignment %s",
      gst_h266_parse_get_string (h266parse, TRUE, format),
      gst_h266_parse_get_string (h266parse, FALSE, align));

  h266parse->format = format;
  h266parse->align = align;

  h266parse->transform = in_format != h266parse->format ||
      align == GST_H266_PARSE_ALIGN_AU;
  GST_DEBUG_OBJECT (h266parse, "transform: %s",
      h266parse->transform ? "true" : "false");

  if (caps)
    gst_caps_unref (caps);
}

static GstBuffer *
gst_h266_parse_wrap_nal (GstH266Parse * h266parse, guint format, guint8 * data,
    guint size)
{
  GstBuffer *buf;
  guint nl = h266parse->nal_length_size;
  guint32 tmp;

  GST_LOG_OBJECT (h266parse, "nal length %d", size);

  buf = gst_buffer_new_allocate (NULL, 4 + size, NULL);
  if (format == GST_H266_PARSE_FORMAT_VVC1 ||
      format == GST_H266_PARSE_FORMAT_VVI1) {
    tmp = GUINT32_TO_BE (size << (32 - 8 * nl));
  } else {
    /* the start code */
    nl = 4;
    tmp = GUINT32_TO_BE (1);
  }

  gst_buffer_fill (buf, 0, &tmp, sizeof (guint32));
  gst_buffer_fill (buf, nl, data, size);
  gst_buffer_set_size (buf, size + nl);

  return buf;
}

static void
gst_h266_parse_store_nal (GstH266Parse * h266parse, guint id,
    GstH266NalUnitType naltype, GstH266APSType params_type,
    GstH266NalUnit * nalu)
{
  GstBuffer *buf, **store;
  guint size = nalu->size, store_size;

  if (naltype == GST_H266_NAL_VPS) {
    store_size = GST_H266_MAX_VPS_COUNT;
    store = h266parse->vps_nals;
    GST_LOG_OBJECT (h266parse, "storing vps %u", id);
  } else if (naltype == GST_H266_NAL_SPS) {
    store_size = GST_H266_MAX_SPS_COUNT;
    store = h266parse->sps_nals;
    GST_LOG_OBJECT (h266parse, "storing sps %u", id);
  } else if (naltype == GST_H266_NAL_PPS) {
    store_size = GST_H266_MAX_PPS_COUNT;
    store = h266parse->pps_nals;
    GST_LOG_OBJECT (h266parse, "storing pps %u", id);
  } else if (naltype == GST_H266_NAL_PREFIX_APS ||
      naltype == GST_H266_NAL_SUFFIX_APS) {
    store_size = GST_H266_MAX_APS_COUNT;
    store = h266parse->aps_nals[params_type];
    GST_LOG_OBJECT (h266parse, "storing aps %u", id);
  } else {
    g_return_if_reached ();
  }

  if (id >= store_size) {
    GST_DEBUG_OBJECT (h266parse, "unable to store nal, id out-of-range %d", id);
    return;
  }

  buf = gst_buffer_new_allocate (NULL, size, NULL);
  gst_buffer_fill (buf, 0, nalu->data + nalu->offset, size);

  /* Indicate that buffer contain a header needed for decoding */
  if (naltype >= GST_H266_NAL_VPS && naltype <= GST_H266_NAL_PPS)
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_HEADER);

  if (store[id])
    gst_buffer_unref (store[id]);

  store[id] = buf;
}

#ifndef GST_DISABLE_GST_DEBUG
static const gchar *nal_names[] = {
  [GST_H266_NAL_SLICE_TRAIL] = "TRAIL",
  [GST_H266_NAL_SLICE_STSA] = "STSA",
  [GST_H266_NAL_SLICE_RADL] = "RADL",
  [GST_H266_NAL_SLICE_RASL] = "RASL",
  [4] = "Invalid (4)",
  [5] = "Invalid (5)",
  [6] = "Invalid (6)",
  [GST_H266_NAL_SLICE_IDR_W_RADL] = "IDR_W_RADL",
  [GST_H266_NAL_SLICE_IDR_N_LP] = "IDR_N_LP",
  [GST_H266_NAL_SLICE_CRA] = "CRA",
  [GST_H266_NAL_SLICE_GDR] = "GDR",
  [11] = "Invalid (11)",
  [GST_H266_NAL_OPI] = "OPI",
  [GST_H266_NAL_DCI] = "DCI",
  [GST_H266_NAL_VPS] = "VPS",
  [GST_H266_NAL_SPS] = "SPS",
  [GST_H266_NAL_PPS] = "PPS",
  [GST_H266_NAL_PREFIX_APS] = "PREFIX_APS",
  [GST_H266_NAL_SUFFIX_APS] = "SUFFIX_APS",
  [GST_H266_NAL_PH] = "PH",
  [GST_H266_NAL_AUD] = "AUD",
  [GST_H266_NAL_EOS] = "EOS",
  [GST_H266_NAL_EOB] = "EOB",
  [GST_H266_NAL_PREFIX_SEI] = "PREFIX_SEI",
  [GST_H266_NAL_SUFFIX_SEI] = "SUFFIX_SEI",
  [GST_H266_NAL_FD] = "FD"
};

static const inline gchar *
_nal_name (GstH266NalUnitType nal_type)
{
  if (nal_type <= GST_H266_NAL_FD)
    return nal_names[nal_type];
  return "Invalid";
}
#endif

static void
gst_h266_parse_process_sei (GstH266Parse * h266parse, GstH266NalUnit * nalu)
{
  GstH266SEIMessage sei;
  GstH266Parser *nalparser = h266parse->nalparser;
  GstH266ParserResult pres;
  GArray *messages;
  guint i;

  pres = gst_h266_parser_parse_sei (nalparser, nalu, &messages);
  if (pres != GST_H266_PARSER_OK)
    GST_WARNING_OBJECT (h266parse, "failed to parse one or more SEI message");

  /* Even if pres != GST_H266_PARSER_OK, some message could have been parsed
   * and stored in messages.
   * TODO: make use of SEI data.
   */
  for (i = 0; i < messages->len; i++) {
    sei = g_array_index (messages, GstH266SEIMessage, i);
    switch (sei.payloadType) {
      case GST_H266_SEI_BUF_PERIOD:
        /* FIXME */
        break;
      case GST_H266_SEI_PIC_TIMING:
        /* FIXME */
        break;
        /* FIXME */
      case GST_H266_SEI_DU_INFO:
        break;
      case GST_H266_SEI_SCALABLE_NESTING:
        /* FIXME */
        break;
      case GST_H266_SEI_SUBPIC_LEVEL_INFO:
        /* FIXME */
        break;
      default:
        break;
    }
  }

  g_array_free (messages, TRUE);
}

/* Update the position for an IDR picture, which may also contain
   PH, prefix SEI and prefix APS.*/
static void
update_idr_pos (GstH266Parse * h266parse, GstH266NalUnit * nalu)
{
  gint pos;

  if (h266parse->transform)
    pos = gst_adapter_available (h266parse->frame_out);
  else
    pos = nalu->sc_offset;

  if (h266parse->idr_pos == -1) {
    h266parse->idr_pos = pos;
  } else {
    g_assert (pos > h266parse->idr_pos);
  }

  GST_LOG_OBJECT (h266parse, "find %s in frame at offset %d, "
      "set idr_pos to %d", _nal_name (nalu->type), pos, h266parse->idr_pos);
}

/* caller guarantees 2 bytes of nal payload */
static gboolean
gst_h266_parse_process_nal (GstH266Parse * h266parse, GstH266NalUnit * nalu)
{
  GstH266VPS *vps = &h266parse->priv->cache->vps;
  GstH266SPS *sps = &h266parse->priv->cache->sps;
  GstH266PPS *pps = &h266parse->priv->cache->pps;
  GstH266APS *aps = &h266parse->priv->cache->aps;
  GstH266PicHdr *ph = &h266parse->priv->cache->ph;
  guint nal_type;
  GstH266Parser *nalparser = h266parse->nalparser;
  GstH266ParserResult pres = GST_H266_PARSER_ERROR;

  /* nothing to do for broken input */
  if (G_UNLIKELY (nalu->size < 2)) {
    GST_DEBUG_OBJECT (h266parse, "not processing nal size %u", nalu->size);
    return TRUE;
  }

  /* we have a peek as well */
  nal_type = nalu->type;

  GST_LOG_OBJECT (h266parse, "processing nal of type %u %s, size %u",
      nal_type, _nal_name (nal_type), nalu->size);

  switch (nal_type) {
    case GST_H266_NAL_VPS:
      /* *INDENT-OFF* */
      *vps = (GstH266VPS) { 0, };
      /* *INDENT-ON* */
      pres = gst_h266_parser_parse_vps (nalparser, nalu, vps);
      if (pres != GST_H266_PARSER_OK) {
        GST_WARNING_OBJECT (h266parse, "failed to parse VPS");
        return FALSE;
      }

      GST_DEBUG_OBJECT (h266parse, "triggering src caps check");
      h266parse->update_caps = TRUE;
      h266parse->have_vps = TRUE;
      h266parse->have_vps_in_frame = TRUE;
      if (h266parse->push_codec && h266parse->have_sps && h266parse->have_pps) {
        /* VPS/SPS/PPS found in stream before the first pre_push_frame, no need
         * to forcibly push at start */
        GST_INFO_OBJECT (h266parse, "have VPS/SPS/PPS in stream");
        h266parse->push_codec = FALSE;
        h266parse->have_vps = FALSE;
        h266parse->have_sps = FALSE;
        h266parse->have_pps = FALSE;
        h266parse->have_aps = FALSE;
      }

      gst_h266_parse_store_nal (h266parse, vps->vps_id, nal_type, -1, nalu);
      h266parse->header = TRUE;
      break;
    case GST_H266_NAL_SPS:
      /* *INDENT-OFF* */
      *sps = (GstH266SPS) { 0, };
      /* *INDENT-ON* */
      /* reset state, everything else is obsolete */
      h266parse->state &= GST_H266_PARSE_STATE_GOT_PPS;

      pres = gst_h266_parser_parse_sps (nalparser, nalu, sps);
      if (pres != GST_H266_PARSER_OK) {
        GST_WARNING_OBJECT (h266parse, "failed to parse SPS:");
        h266parse->state |= GST_H266_PARSE_STATE_GOT_SPS;
        h266parse->header = TRUE;
        return FALSE;
      }

      GST_DEBUG_OBJECT (h266parse, "triggering src caps check");
      h266parse->update_caps = TRUE;
      h266parse->have_sps = TRUE;
      h266parse->have_sps_in_frame = TRUE;
      if (h266parse->push_codec && h266parse->have_pps) {
        /* SPS and PPS found in stream before the first pre_push_frame, no need
         * to forcibly push at start */
        GST_INFO_OBJECT (h266parse, "have SPS/PPS in stream");
        h266parse->push_codec = FALSE;
        h266parse->have_sps = FALSE;
        h266parse->have_pps = FALSE;
      }

      gst_h266_parse_store_nal (h266parse, sps->sps_id, nal_type, -1, nalu);
      h266parse->header = TRUE;
      h266parse->state |= GST_H266_PARSE_STATE_GOT_SPS;
      break;
    case GST_H266_NAL_PPS:
      /* *INDENT-OFF* */
      *pps = (GstH266PPS) { 0, };
      /* *INDENT-ON* */
      /* expected state: got-sps */
      h266parse->state &= GST_H266_PARSE_STATE_GOT_SPS;
      if (!GST_H266_PARSE_STATE_VALID (h266parse, GST_H266_PARSE_STATE_GOT_SPS))
        return FALSE;

      pres = gst_h266_parser_parse_pps (nalparser, nalu, pps);

      /* arranged for a fallback pps.pps_id, so use that one and only warn */
      if (pres != GST_H266_PARSER_OK) {
        GST_WARNING_OBJECT (h266parse, "failed to parse PPS:");
        if (pres != GST_H266_PARSER_BROKEN_LINK)
          return FALSE;
      }

      /* parameters might have changed, force caps check */
      if (!h266parse->have_pps) {
        GST_DEBUG_OBJECT (h266parse, "triggering src caps check");
        h266parse->update_caps = TRUE;
      }
      h266parse->have_pps = TRUE;
      h266parse->have_pps_in_frame = TRUE;
      if (h266parse->push_codec && h266parse->have_sps) {
        /* SPS and PPS found in stream before the first pre_push_frame, no need
         * to forcibly push at start */
        GST_INFO_OBJECT (h266parse, "have SPS/PPS in stream");
        h266parse->push_codec = FALSE;
        h266parse->have_sps = FALSE;
        h266parse->have_pps = FALSE;
      }

      gst_h266_parse_store_nal (h266parse, pps->pps_id, nal_type, -1, nalu);
      h266parse->header = TRUE;
      h266parse->state |= GST_H266_PARSE_STATE_GOT_PPS;
      break;
    case GST_H266_NAL_PREFIX_APS:
    case GST_H266_NAL_SUFFIX_APS:
      /* *INDENT-OFF* */
      *aps = (GstH266APS) { 0, };
      /* *INDENT-ON* */
      /* expected state: got-sps and pps */
      if (!GST_H266_PARSE_STATE_VALID (h266parse,
              GST_H266_PARSE_STATE_VALID_SPS_PPS))
        return FALSE;

      pres = gst_h266_parser_parse_aps (nalparser, nalu, aps);
      if (pres != GST_H266_PARSER_OK) {
        GST_WARNING_OBJECT (h266parse, "failed to parse APS:");
        if (pres != GST_H266_PARSER_BROKEN_LINK)
          return FALSE;
      }

      h266parse->have_aps_in_frame = TRUE;

      gst_h266_parse_store_nal (h266parse, aps->aps_id, nal_type,
          aps->params_type, nalu);
      h266parse->header = TRUE;

      if (nal_type == GST_H266_NAL_PREFIX_APS)
        update_idr_pos (h266parse, nalu);

      break;
    case GST_H266_NAL_PREFIX_SEI:
    case GST_H266_NAL_SUFFIX_SEI:
      /* expected state: got-sps */
      if (!GST_H266_PARSE_STATE_VALID (h266parse, GST_H266_PARSE_STATE_GOT_SPS))
        return FALSE;

      h266parse->header = TRUE;
      gst_h266_parse_process_sei (h266parse, nalu);

      /* update idr pos */
      if (nal_type == GST_H266_NAL_PREFIX_SEI)
        update_idr_pos (h266parse, nalu);

      break;
    case GST_H266_NAL_PH:
    {
      /* *INDENT-OFF* */
      *ph = (GstH266PicHdr) { 0, };
      /* *INDENT-ON* */
      /* expected state: got-sps and pps */
      if (!GST_H266_PARSE_STATE_VALID (h266parse,
              GST_H266_PARSE_STATE_VALID_SPS_PPS))
        return FALSE;

      pres = gst_h266_parser_parse_picture_hdr (nalparser, nalu, ph);
      if (pres != GST_H266_PARSER_OK) {
        GST_WARNING_OBJECT (h266parse, "failed to parse PH:");
        if (pres != GST_H266_PARSER_BROKEN_LINK)
          return FALSE;
      }

      if (ph->gdr_or_irap_pic_flag) {
        if (h266parse->mastering_display_info_state ==
            GST_H266_PARSE_SEI_PARSED)
          h266parse->mastering_display_info_state = GST_H266_PARSE_SEI_ACTIVE;
        else if (h266parse->mastering_display_info_state ==
            GST_H266_PARSE_SEI_ACTIVE)
          h266parse->mastering_display_info_state = GST_H266_PARSE_SEI_EXPIRED;

        if (h266parse->content_light_level_state == GST_H266_PARSE_SEI_PARSED)
          h266parse->content_light_level_state = GST_H266_PARSE_SEI_ACTIVE;
        else if (h266parse->content_light_level_state ==
            GST_H266_PARSE_SEI_ACTIVE)
          h266parse->content_light_level_state = GST_H266_PARSE_SEI_EXPIRED;
      }

      if (ph->gdr_or_irap_pic_flag || h266parse->push_codec)
        update_idr_pos (h266parse, nalu);

      break;
    }
    case GST_H266_NAL_SLICE_TRAIL:
    case GST_H266_NAL_SLICE_STSA:
    case GST_H266_NAL_SLICE_RADL:
    case GST_H266_NAL_SLICE_RASL:
    case GST_H266_NAL_SLICE_IDR_W_RADL:
    case GST_H266_NAL_SLICE_IDR_N_LP:
    case GST_H266_NAL_SLICE_CRA:
    case GST_H266_NAL_SLICE_GDR:
    {
      GstH266SliceHdr slice;
      gboolean is_irap_or_gdr;

      /* expected state: got-sps|got-pps */
      h266parse->state &= GST_H266_PARSE_STATE_VALID_SPS_PPS;
      if (!GST_H266_PARSE_STATE_VALID (h266parse,
              GST_H266_PARSE_STATE_VALID_SPS_PPS))
        return FALSE;

      /* This is similar to the GOT_SLICE state, but is only reset when the
       * AU is complete. This is used to keep track of AU */
      h266parse->picture_start = TRUE;

      pres = gst_h266_parser_parse_slice_hdr (nalparser, nalu, &slice);

      if (pres == GST_H266_PARSER_OK) {
        if (GST_H266_IS_I_SLICE (&slice))
          h266parse->keyframe = TRUE;
        else if (GST_H266_IS_P_SLICE (&slice))
          h266parse->predicted = TRUE;
        else if (GST_H266_IS_B_SLICE (&slice))
          h266parse->bidirectional = TRUE;

        h266parse->state |= GST_H266_PARSE_STATE_GOT_SLICE;
      }

      GST_LOG_OBJECT (h266parse, "parse result %d, "
          "picture_header_in_slice_header_flag: %u, slice type: %u",
          pres, slice.picture_header_in_slice_header_flag, slice.slice_type);

      is_irap_or_gdr = GST_H266_IS_NAL_TYPE_IRAP (nal_type) ||
          GST_H266_IS_NAL_TYPE_GDR (nal_type);

      /* if slice.picture_header_in_slice_header_flag == 0, PH will do this. */
      if (is_irap_or_gdr && slice.picture_header_in_slice_header_flag) {
        if (h266parse->mastering_display_info_state ==
            GST_H266_PARSE_SEI_PARSED)
          h266parse->mastering_display_info_state = GST_H266_PARSE_SEI_ACTIVE;
        else if (h266parse->mastering_display_info_state ==
            GST_H266_PARSE_SEI_ACTIVE)
          h266parse->mastering_display_info_state = GST_H266_PARSE_SEI_EXPIRED;

        if (h266parse->content_light_level_state == GST_H266_PARSE_SEI_PARSED)
          h266parse->content_light_level_state = GST_H266_PARSE_SEI_ACTIVE;
        else if (h266parse->content_light_level_state ==
            GST_H266_PARSE_SEI_ACTIVE)
          h266parse->content_light_level_state = GST_H266_PARSE_SEI_EXPIRED;
      }

      if (is_irap_or_gdr || h266parse->push_codec)
        update_idr_pos (h266parse, nalu);

      break;
    }
    case GST_H266_NAL_AUD:
    {
      GstH266AUD aud;

      /* Just accumulate AU Delimiter, whether it's before SPS or not */
      pres = gst_h266_parser_parse_aud (nalparser, nalu, &aud);
      if (pres != GST_H266_PARSER_OK) {
        GST_WARNING_OBJECT (h266parse, "failed to parse AUD:");
        return FALSE;
      }
      break;
    }
    default:
      pres = gst_h266_parser_parse_nal (nalparser, nalu);
      if (pres != GST_H266_PARSER_OK)
        return FALSE;
      break;
  }

  /* if VVC output needed, collect properly prefixed nal in adapter,
   * and use that to replace outgoing buffer data later on */
  if (h266parse->transform) {
    GstBuffer *buf;

    GST_LOG_OBJECT (h266parse, "collecting NAL in VVC frame");
    buf = gst_h266_parse_wrap_nal (h266parse, h266parse->format,
        nalu->data + nalu->offset, nalu->size);
    gst_adapter_push (h266parse->frame_out, buf);
  }

  return TRUE;
}

/* Caller guarantees at least 3 bytes of nal payload for each nal returns
 * TRUE if next_nal indicates that nal terminates the previous AU. */
static inline gboolean
gst_h266_parse_collect_nal (GstH266Parse * h266parse, const guint8 * data,
    guint size, GstH266NalUnit * nalu)
{
  GstH266NalUnitType nal_type = nalu->type;
  gboolean complete;

  /* determine if AU complete */
  GST_LOG_OBJECT (h266parse, "next nal type: %d %s (picture started %i)",
      nal_type, _nal_name (nal_type), h266parse->picture_start);

  /* EOB or EOS end the stream, so end the current frame. */
  complete = (nal_type == GST_H266_NAL_EOS || nal_type == GST_H266_NAL_EOB);

  /* 7.4.2.4.3 */
  complete = h266parse->picture_start &&
      (nal_type == GST_H266_NAL_AUD || nal_type == GST_H266_NAL_OPI ||
      nal_type == GST_H266_NAL_DCI || nal_type == GST_H266_NAL_VPS ||
      nal_type == GST_H266_NAL_SPS || nal_type == GST_H266_NAL_PPS ||
      nal_type == GST_H266_NAL_PREFIX_APS || nal_type == GST_H266_NAL_PH ||
      nal_type == GST_H266_NAL_PREFIX_SEI ||
      /* Undefined nal type */
      nal_type == 26 || nal_type == 28 || nal_type == 29);

  /* 7.4.2.4.3: The value of nuh_layer_id of the VCL NAL unit is less than
     or equal to the nuh_layer_id of the previous picture in decoding order,
     it starts an AU. */
  if (h266parse->picture_start && nalu->size > nalu->header_bytes &&
      nalu->layer_id <= h266parse->last_nuh_layer_id) {
    if (nal_type >= GST_H266_NAL_SLICE_TRAIL
        && nal_type <= GST_H266_NAL_SLICE_GDR) {
      /* Check the picture_header_in_slice_header_flag:
         7.4.2.4.4: When a picture consists of more than one VCL NAL unit,
         a PH NAL unit shall be present in the PU.
         So when picture_header_in_slice_header_flag is 1, the picture
         should only contain one slice. */
      complete |= (nalu->data[nalu->offset + 2] & 0x80);
    } else if (nal_type == GST_H266_NAL_PH) {
      complete = TRUE;
    }
  }

  GST_LOG_OBJECT (h266parse, "au complete: %d", complete);

  if (complete)
    h266parse->picture_start = FALSE;

  return complete;
}

static GstFlowReturn
gst_h266_parse_handle_frame_packetized (GstBaseParse * parse,
    GstBaseParseFrame * frame)
{
  return GST_FLOW_NOT_SUPPORTED;
}

static GstFlowReturn
gst_h266_parse_handle_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, gint * skipsize)
{
  GstH266Parse *h266parse = GST_H266_PARSE (parse);
  GstBuffer *buffer = frame->buffer;
  GstMapInfo map;
  guint8 *data;
  gsize size;
  gint current_off = 0;
  gboolean drain, nonext;
  GstH266Parser *nalparser = h266parse->nalparser;
  GstH266NalUnit nalu;
  GstH266ParserResult pres;
  gint framesize;

  if (G_UNLIKELY (GST_BUFFER_FLAG_IS_SET (frame->buffer,
              GST_BUFFER_FLAG_DISCONT))) {
    h266parse->discont = TRUE;
  }

  /* delegate in packetized case, no skipping should be needed */
  if (h266parse->packetized)
    return gst_h266_parse_handle_frame_packetized (parse, frame);

  gst_buffer_map (buffer, &map, GST_MAP_READ);
  data = map.data;
  size = map.size;

  /* expect at least 3 bytes start_code, and 2 bytes NALU header.
   * the length of the NALU payload can be zero.
   * (e.g. EOS/EOB placed at the end of an AU.) */
  if (G_UNLIKELY (size < 5)) {
    gst_buffer_unmap (buffer, &map);
    *skipsize = 1;
    return GST_FLOW_OK;
  }

  /* need to configure aggregation */
  if (G_UNLIKELY (h266parse->format == GST_H266_PARSE_FORMAT_NONE))
    gst_h266_parse_negotiate (h266parse, GST_H266_PARSE_FORMAT_BYTE, NULL);

  /* avoid stale cached parsing state */
  if (frame->flags & GST_BASE_PARSE_FRAME_FLAG_NEW_FRAME) {
    GST_LOG_OBJECT (h266parse, "parsing new frame");
    gst_h266_parse_reset_frame (h266parse);
  } else {
    GST_LOG_OBJECT (h266parse, "resuming frame parsing");
  }

  /* Always consume the entire input buffer when in_align == ALIGN_AU */
  drain = GST_BASE_PARSE_DRAINING (parse)
      || h266parse->in_align == GST_H266_PARSE_ALIGN_AU;
  nonext = FALSE;

  current_off = h266parse->current_off;
  if (current_off < 0)
    current_off = 0;

  /* The parser is being drain, but no new data was added, just prentend this
   * AU is complete */
  if (drain && current_off == size) {
    GST_LOG_OBJECT (h266parse, "draining with no new data");
    nalu.size = 0;
    nalu.offset = current_off;
    goto end;
  }

  g_assert (current_off < size);
  GST_LOG_OBJECT (h266parse, "last parse position %d", current_off);

  /* check for initial skip */
  if (h266parse->current_off == -1) {
    pres = gst_h266_parser_identify_nalu_unchecked (nalparser,
        data, current_off, size, &nalu);
    switch (pres) {
      case GST_H266_PARSER_OK:
        if (nalu.sc_offset > 0) {
          *skipsize = nalu.sc_offset;
          goto skip;
        }
        break;
      case GST_H266_PARSER_NO_NAL:
        /* start code may have up to 4 bytes, and we may also get that return
         * value if only one of the two header bytes are present, make sure
         * not to skip too much */
        *skipsize = size > 5 ? size - 5 : 0;
        goto skip;
      default:
        /* should not really occur either */
        GST_ELEMENT_ERROR (h266parse, STREAM, FORMAT,
            ("Error parsing H.266 stream"), ("Invalid H.266 stream"));
        goto invalid_stream;
    }

    /* Ensure we use the TS of the first NAL. This avoids broken timestamp in
     * the case of a miss-placed filler byte. */
    gst_base_parse_set_ts_at_offset (parse, nalu.offset);
  }

  while (TRUE) {
    pres = gst_h266_parser_identify_nalu (nalparser, data, current_off,
        size, &nalu);

    switch (pres) {
      case GST_H266_PARSER_OK:
        GST_LOG_OBJECT (h266parse, "complete nal (offset, size): (%u, %u)",
            nalu.offset, nalu.size);
        break;
      case GST_H266_PARSER_NO_NAL_END:
        /* In NAL alignment, assume the NAL is complete */
        if (h266parse->in_align == GST_H266_PARSE_ALIGN_NAL ||
            h266parse->in_align == GST_H266_PARSE_ALIGN_AU) {
          nonext = TRUE;
          nalu.size = size - nalu.offset;
          GST_LOG_OBJECT (h266parse,
              "in_align(%s), assume complete nal (offset, size): (%u, %u)",
              gst_h266_parse_get_string (h266parse, FALSE, h266parse->in_align),
              nalu.offset, nalu.size);
          break;
        }

        GST_DEBUG_OBJECT (h266parse, "not a complete nal found at offset %u",
            nalu.offset);

        /* if draining, accept it as complete nal */
        if (drain) {
          nonext = TRUE;
          nalu.size = size - nalu.offset;
          GST_DEBUG_OBJECT (h266parse, "draining, accepting with size %u",
              nalu.size);
          /* if it's not too short at least */
          if (nalu.size < 3)
            goto broken;
          break;
        }
        /* otherwise need more */
        goto more;
      case GST_H266_PARSER_BROKEN_LINK:
        /* should not really occur */
        GST_ELEMENT_ERROR (h266parse, STREAM, FORMAT,
            ("Error parsing H.266 stream"),
            ("The link to structure needed for the parsing couldn't be found"));
        goto invalid_stream;
      case GST_H266_PARSER_ERROR:
        /* should not really occur either */
        GST_ELEMENT_ERROR (h266parse, STREAM, FORMAT,
            ("Error parsing H.266 stream"), ("Invalid H.266 stream"));
        goto invalid_stream;
      case GST_H266_PARSER_NO_NAL:
        GST_ELEMENT_ERROR (h266parse, STREAM, FORMAT,
            ("Error parsing H.266 stream"), ("No H.266 NAL unit found"));
        goto invalid_stream;
      case GST_H266_PARSER_BROKEN_DATA:
        GST_WARNING_OBJECT (h266parse, "input stream is corrupt; "
            "it contains a NAL unit of length %u", nalu.size);
      broken:
        /* broken nal at start -> arrange to skip it,
         * otherwise have it terminate current au
         * (and so it will be skipped on next frame round) */
        if (current_off == 0) {
          GST_DEBUG_OBJECT (h266parse, "skipping broken nal");
          *skipsize = nalu.offset;
          goto skip;
        } else {
          GST_LOG_OBJECT (h266parse, "terminating au");
          nalu.size = 0;
          nalu.offset = nalu.sc_offset;
          goto end;
        }
      default:
        g_assert_not_reached ();
        break;
    }

    GST_LOG_OBJECT (h266parse, "%p complete nal found. Off: %u, Size: %u",
        data, nalu.offset, nalu.size);

    if (gst_h266_parse_collect_nal (h266parse, data, size, &nalu)) {
      /* complete current frame, if it exist */
      if (current_off > 0) {
        nalu.offset = nalu.sc_offset;
        /* Include the EOS and EOB in the current frame. */
        if (nalu.type == GST_H266_NAL_EOS || nalu.type == GST_H266_NAL_EOB)
          nalu.offset += nalu.size;

        nalu.size = 0;
        h266parse->marker = TRUE;
        break;
      }
    }

    if (!gst_h266_parse_process_nal (h266parse, &nalu)) {
      GST_WARNING_OBJECT (h266parse,
          "broken/invalid nal Type: %d %s, Size: %u will be dropped",
          nalu.type, _nal_name (nalu.type), nalu.size);
      *skipsize = nalu.size;
      goto skip;
    }

    /* Do not push immediatly if we don't have all headers. This ensure that
     * our caps are complete, avoiding a renegotiation. APS does not change
     * stream level information, not included here. */
    if (h266parse->align == GST_H266_PARSE_ALIGN_NAL &&
        !GST_H266_PARSE_STATE_VALID (h266parse,
            GST_H266_PARSE_STATE_VALID_SPS_PPS))
      frame->flags |= GST_BASE_PARSE_FRAME_FLAG_QUEUE;

    if (nonext) {
      /* If there is a marker flag, or input is AU, we know this is complete */
      if (GST_BUFFER_FLAG_IS_SET (frame->buffer, GST_BUFFER_FLAG_MARKER) ||
          h266parse->in_align == GST_H266_PARSE_ALIGN_AU) {
        h266parse->marker = TRUE;
        break;
      }

      /* or if we are draining or producing NALs */
      if (drain || h266parse->align == GST_H266_PARSE_ALIGN_NAL)
        break;

      current_off = nalu.offset + nalu.size;
      goto more;
    }

    /* If the output is NAL, we are done */
    if (h266parse->align == GST_H266_PARSE_ALIGN_NAL)
      break;

    GST_LOG_OBJECT (h266parse, "Looking for more");
    current_off = nalu.offset + nalu.size;

    /* expect at least 3 bytes start_code, and 2 bytes NALU header.
     * the length of the NALU payload can be zero.
     * (e.g. EOS/EOB placed at the end of an AU.) */
    if (G_UNLIKELY (size - current_off < 5)) {
      /* Finish the frame if there is no more data in the stream */
      if (drain)
        break;

      goto more;
    }
  }

end:
  framesize = nalu.offset + nalu.size;

  gst_buffer_unmap (buffer, &map);

  gst_h266_parse_parse_frame (parse, frame);

  return gst_base_parse_finish_frame (parse, frame, framesize);

more:
  *skipsize = 0;

  /* Restart parsing from here next time */
  if (current_off > 0)
    h266parse->current_off = current_off;

  /* Fall-through. */
out:
  gst_buffer_unmap (buffer, &map);
  return GST_FLOW_OK;

skip:
  GST_LOG_OBJECT (h266parse, "skipping %d", *skipsize);
  /* If we are collecting access units, we need to preserve the initial
   * config headers (SPS, PPS et al.) and only reset the frame if another
   * slice NAL was received. This means that broken pictures are discarded */
  if (h266parse->align != GST_H266_PARSE_ALIGN_AU ||
      !(h266parse->state & GST_H266_PARSE_STATE_VALID_SPS_PPS) ||
      (h266parse->state & GST_H266_PARSE_STATE_GOT_SLICE))
    gst_h266_parse_reset_frame (h266parse);
  goto out;

invalid_stream:
  gst_buffer_unmap (buffer, &map);
  return GST_FLOW_ERROR;
}

static void
gst_h266_parse_get_par (GstH266Parse * h266parse, gint * num, gint * den)
{
  if (h266parse->upstream_par_n != -1 && h266parse->upstream_par_d != -1) {
    *num = h266parse->upstream_par_n;
    *den = h266parse->upstream_par_d;
  } else {
    *num = h266parse->parsed_par_n;
    *den = h266parse->parsed_par_d;
  }
}

static const gchar *
digit_to_string (guint digit)
{
  static const char itoa[][2] = {
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"
  };

  if (G_LIKELY (digit < 10))
    return itoa[digit];
  else
    return NULL;
}

static const gchar *
get_tier_string (guint8 tier_flag)
{
  const gchar *tier = NULL;

  if (tier_flag)
    tier = "high";
  else
    tier = "main";

  return tier;
}

/* The level numbers in this table are in the form of "majorNum.minorNum",
 * and the value of general_level_idc for each of the levels is equal to
 * majorNum * 16 + minorNum * 3. */
static const gchar *
get_level_string (guint8 level_idc)
{
  if (level_idc == 0)
    return NULL;
  else if (level_idc % 16 == 0)
    return digit_to_string (level_idc / 16);
  else {
    switch (level_idc) {
      case GST_H266_LEVEL_L2_1:
        return "2.1";
        break;
      case GST_H266_LEVEL_L3_1:
        return "3.1";
        break;
      case GST_H266_LEVEL_L4_1:
        return "4.1";
        break;
      case GST_H266_LEVEL_L5_1:
        return "5.1";
        break;
      case GST_H266_LEVEL_L5_2:
        return "5.2";
        break;
      case GST_H266_LEVEL_L6_1:
        return "6.1";
        break;
      case GST_H266_LEVEL_L6_2:
        return "6.2";
        break;
      case GST_H266_LEVEL_L6_3:
        return "6.3";
        break;
      default:
        return NULL;
    }
  }
}

static GstBuffer *
gst_h266_parse_make_codec_data_general_constraint_info (GstH266ProfileTierLevel
    * pft, guint8 num_sublayers)
{
  GstBitWriter *biw = gst_bit_writer_new_with_size (12, FALSE);

#define WRITE_GCI_U8(val, nbits) G_STMT_START { \
  gst_bit_writer_put_bits_uint8(biw, val, nbits); \
} G_STMT_END;

  WRITE_GCI_U8 (pft->frame_only_constraint_flag, 1);
  WRITE_GCI_U8 (pft->multilayer_enabled_flag, 1);
  if (!pft->general_constraints_info.present_flag) {
    WRITE_GCI_U8 (0, 6);
  } else {
    GstH266GeneralConstraintsInfo *gci = &pft->general_constraints_info;
    WRITE_GCI_U8 (gci->present_flag, 1);
    WRITE_GCI_U8 (gci->intra_only_constraint_flag, 1);
    WRITE_GCI_U8 (gci->all_layers_independent_constraint_flag, 1);
    WRITE_GCI_U8 (gci->one_au_only_constraint_flag, 1);
    WRITE_GCI_U8 (gci->sixteen_minus_max_bitdepth_constraint_idc, 4);
    WRITE_GCI_U8 (gci->three_minus_max_chroma_format_constraint_idc, 2);
    WRITE_GCI_U8 (gci->no_mixed_nalu_types_in_pic_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_trail_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_stsa_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_rasl_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_radl_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_idr_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_cra_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_gdr_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_aps_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_idr_rpl_constraint_flag, 1);
    WRITE_GCI_U8 (gci->one_tile_per_pic_constraint_flag, 1);
    WRITE_GCI_U8 (gci->pic_header_in_slice_header_constraint_flag, 1);
    WRITE_GCI_U8 (gci->one_slice_per_pic_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_rectangular_slice_constraint_flag, 1);
    WRITE_GCI_U8 (gci->one_slice_per_subpic_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_subpic_info_constraint_flag, 1);
    WRITE_GCI_U8 (gci->three_minus_max_log2_ctu_size_constraint_idc, 2);
    WRITE_GCI_U8 (gci->no_partition_constraints_override_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_mtt_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_qtbtt_dual_tree_intra_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_palette_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_ibc_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_isp_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_mrl_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_mip_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_cclm_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_ref_pic_resampling_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_res_change_in_clvs_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_weighted_prediction_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_ref_wraparound_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_temporal_mvp_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_sbtmvp_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_amvr_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_bdof_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_smvd_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_dmvr_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_mmvd_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_affine_motion_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_prof_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_bcw_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_ciip_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_gpm_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_luma_transform_size_64_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_transform_skip_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_bdpcm_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_mts_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_lfnst_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_joint_cbcr_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_sbt_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_act_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_explicit_scaling_list_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_dep_quant_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_sign_data_hiding_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_cu_qp_delta_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_chroma_qp_offset_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_sao_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_alf_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_ccalf_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_lmcs_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_ladf_constraint_flag, 1);
    WRITE_GCI_U8 (gci->no_virtual_boundaries_constraint_flag, 1);

    if (gci->all_rap_pictures_constraint_flag ||
        gci->no_extended_precision_processing_constraint_flag ||
        gci->no_ts_residual_coding_rice_constraint_flag ||
        gci->no_rrc_rice_extension_constraint_flag ||
        gci->no_persistent_rice_adaptation_constraint_flag ||
        gci->no_reverse_last_sig_coeff_constraint_flag) {
      WRITE_GCI_U8 (6, 8);
      WRITE_GCI_U8 (gci->all_rap_pictures_constraint_flag, 1);
      WRITE_GCI_U8 (gci->no_extended_precision_processing_constraint_flag, 1);
      WRITE_GCI_U8 (gci->no_ts_residual_coding_rice_constraint_flag, 1);
      WRITE_GCI_U8 (gci->no_rrc_rice_extension_constraint_flag, 1);
      WRITE_GCI_U8 (gci->no_persistent_rice_adaptation_constraint_flag, 1);
      WRITE_GCI_U8 (gci->no_reverse_last_sig_coeff_constraint_flag, 1);
    } else {
      WRITE_GCI_U8 (0, 8);
    }

    gst_bit_writer_align_bytes (biw, 0);
  }

#undef WRITE_GCI_U8

  return gst_bit_writer_free_and_get_buffer (biw);
}

/* byte together vvc codec data based on collected vps, pps and sps so far */
static GstBuffer *
gst_h266_parse_make_codec_data (GstH266Parse * h266parse)
{
  GstBuffer *nal;
  GstH266SPS *sps;
  gint i;
  guint vps_size = 0, sps_size = 0, pps_size = 0;
  guint16 num_vps = 0, num_sps = 0, num_pps = 0;
  gboolean found = FALSE;
  guint8 num_arrays = 0;
  gint nl;
  GstH266ProfileTierLevel *pft = NULL;
  GstByteWriter bw;
  guint8 array_completeness;
  gboolean ptl_present_flag;
  guint8 num_sublayers = 0;


  for (i = 0; i < GST_H266_MAX_VPS_COUNT; i++) {
    if ((nal = h266parse->vps_nals[i])) {
      num_vps++;
      vps_size += 2 + gst_buffer_get_size (nal);
    }
  }
  if (num_vps > 0)
    num_arrays++;

  for (i = 0; i < GST_H266_MAX_SPS_COUNT; i++) {
    if ((nal = h266parse->sps_nals[i])) {
      num_sps++;
      found = TRUE;
      sps_size += 2 + gst_buffer_get_size (nal);
    }
  }
  if (num_sps > 0)
    num_arrays++;

  for (i = 0; i < GST_H266_MAX_PPS_COUNT; i++) {
    if ((nal = h266parse->pps_nals[i])) {
      num_pps++;
      pps_size += 2 + gst_buffer_get_size (nal);
    }
  }
  if (num_pps > 0)
    num_arrays++;

  GST_DEBUG_OBJECT (h266parse,
      "constructing codec_data: num_vps=%d num_sps=%d, num_pps=%d",
      num_vps, num_sps, num_pps);

  if (!found)
    return NULL;

  sps = h266parse->nalparser->last_sps;
  if (!sps)
    return NULL;

  gst_byte_writer_init_with_size (&bw, 16 + (3 * num_arrays) + vps_size +
      sps_size + pps_size, FALSE);

  nl = h266parse->nal_length_size;
  if (sps->ptl_dpb_hrd_params_present_flag) {
    pft = &sps->profile_tier_level;
    num_sublayers = sps->max_sublayers_minus1 + 1;
  } else if (h266parse->nalparser->last_vps
      && h266parse->nalparser->last_vps->pt_present_flag[0]) {
    pft = &h266parse->nalparser->last_vps->profile_tier_level[0];
    num_sublayers = h266parse->nalparser->last_vps->max_sublayers_minus1 + 1;
  }

  /* reserved(5) = 11111 | LengthSizeMinusOne(2) | ptl_present_flag(1) */
  ptl_present_flag = pft != NULL;
  gst_byte_writer_put_uint8 (&bw,
      (0x1F << 3) | (((guint8) nl - 1) << 1) | ptl_present_flag);

  if (ptl_present_flag) {
    /* It's unclear where to get constant_frame_rate from. */
    guint8 constant_frame_rate = 1;
    guint8 chroma_format_idc = sps->chroma_format_idc;
    GstBuffer *pci;

    /* ols_idx(9) | num_sublayers(3) | constant_frame_rate(2) | chroma_format_idc(2) */
    /* FIXME: OPI isn't parsed so we don't store an ols_idx in the parser and just write 0 here. */
    guint16 ols_idx = 0;
    gst_byte_writer_put_uint16_be (&bw,
        (ols_idx << 7) | (num_sublayers << 4) |
        (constant_frame_rate << 2) | chroma_format_idc);

    /* bit_depth_minus8(3) | reserved(5) = 11111 */
    gst_byte_writer_put_uint8 (&bw, (sps->bitdepth_minus8 << 5) | 0x1F);

    /* VvcPTLRecord */
    pci =
        gst_h266_parse_make_codec_data_general_constraint_info (pft,
        num_sublayers);
    /* reserved(2) = 0 | num_bytes_constraint_info(6) */
    gst_byte_writer_put_uint8 (&bw, gst_buffer_get_size (pci));

    /* general_profile_idc(7) | general_tier_flag(1) */
    gst_byte_writer_put_uint8 (&bw,
        ((guint8) pft->profile_idc << 1) | pft->tier_flag);
    gst_byte_writer_put_uint8 (&bw, pft->level_idc);
    gst_byte_writer_put_buffer (&bw, pci, 0, -1);
    gst_buffer_unref (pci);

    if (num_sublayers > 1) {
      guint8 ptl_sublayer_level_present_flag = 0;
      for (i = num_sublayers - 2; i >= 0; i--)
        ptl_sublayer_level_present_flag |=
            (pft->sublayer_level_present_flag[i] << (5 + num_sublayers - i));
      gst_byte_writer_put_uint8 (&bw, ptl_sublayer_level_present_flag);

      for (i = num_sublayers - 2; i >= 0; i--)
        if (pft->sublayer_level_present_flag[i])
          gst_byte_writer_put_uint8 (&bw, pft->sublayer_level_idc[i]);
    }

    gst_byte_writer_put_uint8 (&bw, pft->num_sub_profiles);
    for (i = 0; i < pft->num_sub_profiles; i++)
      gst_byte_writer_put_uint32_be (&bw, pft->sub_profile_idc[i]);

    gst_byte_writer_put_uint16_be (&bw, sps->pic_width_max_in_luma_samples);
    gst_byte_writer_put_uint16_be (&bw, sps->pic_height_max_in_luma_samples);
    /* keep avg_frame_rate unspecified */
    gst_byte_writer_put_uint16_be (&bw, 0);
  }


  gst_byte_writer_put_uint8 (&bw, num_arrays);
  array_completeness = h266parse->format == GST_H266_PARSE_FORMAT_VVC1;

  /* VPS */
  if (num_vps > 0) {
    /* array_completeness(1) | reserved(2) = 0 | nal_unit_type */
    guint8 nal_unit_type = GST_H266_NAL_VPS;
    gst_byte_writer_put_uint8 (&bw, (array_completeness << 7) | nal_unit_type);
    gst_byte_writer_put_uint16_be (&bw, num_vps);
    for (i = 0; i < GST_H266_MAX_VPS_COUNT; i++) {
      if ((nal = h266parse->vps_nals[i])) {
        gsize nal_unit_length = gst_buffer_get_size (nal);
        gst_byte_writer_put_uint16_be (&bw, nal_unit_length);
        gst_byte_writer_put_buffer (&bw, nal, 0, nal_unit_length);
      }
    }
  }

  /* SPS */
  if (num_sps > 0) {
    /* array_completeness(1) | reserved(2) = 0 | nal_unit_type */
    guint8 nal_unit_type = GST_H266_NAL_SPS;
    gst_byte_writer_put_uint8 (&bw, (array_completeness << 7) | nal_unit_type);
    gst_byte_writer_put_uint16_be (&bw, num_sps);
    for (i = 0; i < GST_H266_MAX_SPS_COUNT; i++) {
      if ((nal = h266parse->sps_nals[i])) {
        gsize nal_unit_length = gst_buffer_get_size (nal);
        gst_byte_writer_put_uint16_be (&bw, nal_unit_length);
        gst_byte_writer_put_buffer (&bw, nal, 0, nal_unit_length);
      }
    }
  }

  /* PPS */
  if (num_pps > 0) {
    /* array_completeness(1) | reserved(2) = 0 | nal_unit_type */
    guint8 nal_unit_type = GST_H266_NAL_PPS;
    gst_byte_writer_put_uint8 (&bw, (array_completeness << 7) | nal_unit_type);
    gst_byte_writer_put_uint16_be (&bw, num_pps);
    for (i = 0; i < GST_H266_MAX_PPS_COUNT; i++) {
      if ((nal = h266parse->pps_nals[i])) {
        gsize nal_unit_length = gst_buffer_get_size (nal);
        gst_byte_writer_put_uint16_be (&bw, nal_unit_length);
        gst_byte_writer_put_buffer (&bw, nal, 0, nal_unit_length);
      }
    }
  }

  return gst_byte_writer_reset_and_get_buffer (&bw);
}

static GstH266Profile
gst_h266_parse_guess_profile (GstH266Parse * h266parse,
    GstH266SPS * sps, gboolean strict)
{
  gboolean flag_restriction = sps->palette_enabled_flag ||
      sps->range_params.extended_precision_flag ||
      sps->range_params.ts_residual_coding_rice_present_in_sh_flag ||
      sps->range_params.rrc_rice_extension_flag ||
      sps->range_params.persistent_rice_adaptation_enabled_flag ||
      sps->range_params.reverse_last_sig_coeff_enabled_flag;

  flag_restriction = (flag_restriction && strict);

  /* Guess the profile based on Table A.1 */
  if (sps->profile_tier_level.multilayer_enabled_flag && strict) {
    /* No main 12 for multilayer. */
    if (sps->bitdepth_minus8 > 2)
      return GST_H266_PROFILE_INVALID;

    if (sps->chroma_format_idc <= 1)
      return GST_H266_PROFILE_MULTILAYER_MAIN_10;

    if (sps->chroma_format_idc <= 3)
      return GST_H266_PROFILE_MULTILAYER_MAIN_10_444;
  } else {
    if (sps->chroma_format_idc <= 1 && !flag_restriction) {
      if (sps->bitdepth_minus8 <= 2) {
        return GST_H266_PROFILE_MAIN_10;
      } else if (sps->bitdepth_minus8 <= 4) {
        return GST_H266_PROFILE_MAIN_12;
      }
    } else if (sps->chroma_format_idc <= 3) {
      if (sps->bitdepth_minus8 <= 2) {
        return GST_H266_PROFILE_MAIN_10_444;
      } else if (sps->bitdepth_minus8 <= 4) {
        return GST_H266_PROFILE_MAIN_12_444;
      } else if (sps->bitdepth_minus8 <= 8) {
        return GST_H266_PROFILE_MAIN_16_444;
      }
    }
  }

  if (!strict)
    return GST_H266_PROFILE_MAIN_10;

  return GST_H266_PROFILE_INVALID;
}

static GArray *
get_compatible_profiles (GstH266Profile profile)
{
  GstH266Profile p;
  GArray *profiles = NULL;

  profiles = g_array_new (FALSE, FALSE, sizeof (GstH266Profile));

  g_array_append_val (profiles, profile);

  switch (profile) {
    case GST_H266_PROFILE_MAIN_10:
    {
      /* A.3.1 */
      p = GST_H266_PROFILE_MAIN_10_STILL_PICTURE;
      g_array_append_val (profiles, p);
      break;
    }
    case GST_H266_PROFILE_MAIN_10_444:
    {
      /* A.3.2 */
      p = GST_H266_PROFILE_MAIN_10;
      g_array_append_val (profiles, p);
      p = GST_H266_PROFILE_MAIN_10_444_STILL_PICTURE;
      g_array_append_val (profiles, p);
      p = GST_H266_PROFILE_MAIN_10_STILL_PICTURE;
      g_array_append_val (profiles, p);
      break;
    }
    case GST_H266_PROFILE_MAIN_10_444_STILL_PICTURE:
    {
      /* A.3.2 */
      p = GST_H266_PROFILE_MAIN_10_STILL_PICTURE;
      g_array_append_val (profiles, p);
      break;
    }
    case GST_H266_PROFILE_MULTILAYER_MAIN_10:
    {
      /* A.3.3 */
      p = GST_H266_PROFILE_MAIN_10;
      g_array_append_val (profiles, p);
      p = GST_H266_PROFILE_MAIN_10_STILL_PICTURE;
      g_array_append_val (profiles, p);
      break;
    }
    case GST_H266_PROFILE_MULTILAYER_MAIN_10_444:
    {
      /* A.3.4 */
      p = GST_H266_PROFILE_MULTILAYER_MAIN_10;
      g_array_append_val (profiles, p);
      p = GST_H266_PROFILE_MAIN_10_444;
      g_array_append_val (profiles, p);
      p = GST_H266_PROFILE_MAIN_10;
      g_array_append_val (profiles, p);
      p = GST_H266_PROFILE_MAIN_10_444_STILL_PICTURE;
      g_array_append_val (profiles, p);
      p = GST_H266_PROFILE_MAIN_10_STILL_PICTURE;
      g_array_append_val (profiles, p);
      break;
    }
    case GST_H266_PROFILE_MAIN_12:
    {
      /* A.3.5 */
      p = GST_H266_PROFILE_MAIN_10;
      g_array_append_val (profiles, p);
      p = GST_H266_PROFILE_MAIN_10_STILL_PICTURE;
      g_array_append_val (profiles, p);
      p = GST_H266_PROFILE_MAIN_12_INTRA;
      g_array_append_val (profiles, p);
      p = GST_H266_PROFILE_MAIN_12_STILL_PICTURE;
      g_array_append_val (profiles, p);
      break;
    }
    case GST_H266_PROFILE_MAIN_16_444:
    {
      /* A.3.5 */
      p = GST_H266_PROFILE_MAIN_16_444_INTRA;
      g_array_append_val (profiles, p);
      p = GST_H266_PROFILE_MAIN_16_444_STILL_PICTURE;
      g_array_append_val (profiles, p);
    }
      /* FALLTHROUGH */
    case GST_H266_PROFILE_MAIN_12_444:
    {
      /* A.3.5 */
      p = GST_H266_PROFILE_MAIN_10;
      g_array_append_val (profiles, p);
      p = GST_H266_PROFILE_MAIN_10_STILL_PICTURE;
      g_array_append_val (profiles, p);
      p = GST_H266_PROFILE_MAIN_10_444;
      g_array_append_val (profiles, p);
      p = GST_H266_PROFILE_MAIN_10_444_STILL_PICTURE;
      g_array_append_val (profiles, p);
      p = GST_H266_PROFILE_MAIN_12;
      g_array_append_val (profiles, p);
      p = GST_H266_PROFILE_MAIN_12_INTRA;
      g_array_append_val (profiles, p);
      p = GST_H266_PROFILE_MAIN_12_STILL_PICTURE;
      g_array_append_val (profiles, p);
      p = GST_H266_PROFILE_MAIN_12_444;
      g_array_append_val (profiles, p);
      p = GST_H266_PROFILE_MAIN_12_444_INTRA;
      g_array_append_val (profiles, p);
      p = GST_H266_PROFILE_MAIN_12_444_STILL_PICTURE;
      g_array_append_val (profiles, p);
      break;
    }
    case GST_H266_PROFILE_MAIN_12_INTRA:
    {
      /* A.3.5 */
      p = GST_H266_PROFILE_MAIN_10_STILL_PICTURE;
      g_array_append_val (profiles, p);
      p = GST_H266_PROFILE_MAIN_12_STILL_PICTURE;
      g_array_append_val (profiles, p);
      break;
    }
    case GST_H266_PROFILE_MAIN_16_444_INTRA:
    {
      /* A.3.5 */
      p = GST_H266_PROFILE_MAIN_16_444_STILL_PICTURE;
      g_array_append_val (profiles, p);
    }
      /* FALLTHROUGH */
    case GST_H266_PROFILE_MAIN_12_444_INTRA:
    {
      /* A.3.5 */
      p = GST_H266_PROFILE_MAIN_10_STILL_PICTURE;
      g_array_append_val (profiles, p);
      p = GST_H266_PROFILE_MAIN_10_444_STILL_PICTURE;
      g_array_append_val (profiles, p);
      p = GST_H266_PROFILE_MAIN_12_INTRA;
      g_array_append_val (profiles, p);
      p = GST_H266_PROFILE_MAIN_12_444_INTRA;
      g_array_append_val (profiles, p);
      p = GST_H266_PROFILE_MAIN_12_STILL_PICTURE;
      g_array_append_val (profiles, p);
      p = GST_H266_PROFILE_MAIN_12_444_STILL_PICTURE;
      g_array_append_val (profiles, p);
      break;
    }
    case GST_H266_PROFILE_MAIN_16_444_STILL_PICTURE:
    {
      /* A.3.5 */
      p = GST_H266_PROFILE_MAIN_12_444_STILL_PICTURE;
      g_array_append_val (profiles, p);
    }
      /* FALLTHROUGH */
    case GST_H266_PROFILE_MAIN_12_444_STILL_PICTURE:
    {
      /* A.3.5 */
      p = GST_H266_PROFILE_MAIN_10_444_STILL_PICTURE;
      g_array_append_val (profiles, p);
      p = GST_H266_PROFILE_MAIN_12_STILL_PICTURE;
      g_array_append_val (profiles, p);
    }
      /* FALLTHROUGH */
    case GST_H266_PROFILE_MAIN_12_STILL_PICTURE:
    {
      /* A.3.5 */
      p = GST_H266_PROFILE_MAIN_10_STILL_PICTURE;
      g_array_append_val (profiles, p);
      break;
    }

    default:
      break;
  }

  if (profiles->len == 0) {
    g_array_unref (profiles);
    profiles = NULL;
  }

  return profiles;
}

static GstH266Profile
get_common_profile (GstH266Profile a, GstH266Profile b)
{
  GArray *profiles;
  GstH266Profile p;
  guint i;

  profiles = get_compatible_profiles (a);
  if (profiles) {
    for (i = 0; i < profiles->len; i++) {
      p = g_array_index (profiles, GstH266Profile, i);
      if (p == b) {
        g_array_unref (profiles);
        return a;
      }
    }
    g_array_unref (profiles);
  }

  profiles = get_compatible_profiles (b);
  if (profiles) {
    for (i = 0; i < profiles->len; i++) {
      p = g_array_index (profiles, GstH266Profile, i);
      if (p == a) {
        g_array_unref (profiles);
        return b;
      }
    }
    g_array_unref (profiles);
  }

  return GST_H266_PROFILE_INVALID;
}

/* if downstream didn't support the exact profile indicated in sps header,
   check for the compatible profiles also. */
static void
gst_h266_parse_ensure_compatible_profiles (GstH266Parse * h266parse,
    GstCaps * caps, GstH266SPS * sps, GstH266Profile profile)
{
  GstCaps *peer_caps;

  g_return_if_fail (profile != GST_H266_PROFILE_INVALID);

  peer_caps = gst_pad_get_current_caps (GST_BASE_PARSE_SRC_PAD (h266parse));
  if (!peer_caps || !gst_caps_can_intersect (caps, peer_caps)) {
    GstCaps *filter_caps = gst_caps_new_empty_simple ("video/x-h266");

    if (peer_caps)
      gst_caps_unref (peer_caps);

    peer_caps = gst_pad_peer_query_caps (GST_BASE_PARSE_SRC_PAD (h266parse),
        filter_caps);

    gst_caps_unref (filter_caps);
  }

  if (peer_caps && !gst_caps_can_intersect (caps, peer_caps)) {
    GstCaps *compat_caps = NULL;
    GstStructure *structure;
    GArray *profiles;

    profiles = get_compatible_profiles (profile);
    if (profiles) {
      guint i;
      GstH266Profile p;
      GValue compat_profiles = G_VALUE_INIT;
      GValue value = G_VALUE_INIT;
      const gchar *profile_str;

      g_value_init (&compat_profiles, GST_TYPE_LIST);

      compat_caps = gst_caps_new_empty_simple ("video/x-h266");

      for (i = 0; i < profiles->len; i++) {
        p = g_array_index (profiles, GstH266Profile, i);
        profile_str = gst_h266_profile_to_string (p);
        g_assert (profile_str);

        g_value_init (&value, G_TYPE_STRING);
        g_value_set_string (&value, profile_str);
        gst_value_list_append_value (&compat_profiles, &value);
        g_value_unset (&value);
      }

      gst_caps_set_value (caps, "profile", &compat_profiles);
      g_value_unset (&compat_profiles);
      g_array_unref (profiles);
    }

    if (compat_caps != NULL) {
      GstCaps *res_caps = NULL;

      res_caps = gst_caps_intersect (peer_caps, compat_caps);
      if (res_caps && !gst_caps_is_empty (res_caps)) {
        const gchar *profile_str = NULL;

        res_caps = gst_caps_fixate (res_caps);
        structure = gst_caps_get_structure (res_caps, 0);
        profile_str = gst_structure_get_string (structure, "profile");
        if (profile_str) {
          gst_caps_set_simple (caps, "profile", G_TYPE_STRING, profile_str,
              NULL);
          GST_DEBUG_OBJECT (h266parse,
              "Setting compatible profile %s to the caps", profile_str);
        }
      }
      if (res_caps)
        gst_caps_unref (res_caps);

      gst_caps_unref (compat_caps);
    }
  }

  if (peer_caps)
    gst_caps_unref (peer_caps);
}

static guint
get_interlaced_mode (GstH266SPS * sps)
{
  const GstH266VUIParams *vui;

  /* Default not interlaced */
  if (!sps)
    return GST_H266_PARSE_PROGRESSIVE_ONLY;

  /* Equal to 1 indicates that the CLVS conveys pictures that represent fields.
     Equal to 0 may be frame stream or field-pair interlaced stream if
     frame-field information SEI message appears. */
  if (sps->field_seq_flag)
    return GST_H266_PARSE_INTERLACED_ONLY;

  /* NOTE 1 Decoders may ignore the values of general_progressive_source_flag
     and general_interlaced_source_flag for purposes other than determining the
     value to be inferred for frame_field_info_present_flag when
     vui_parameters_present_flag is equal to 0. */
  if (!sps->vui_parameters_present_flag)
    return GST_H266_PARSE_PROGRESSIVE_ONLY;

  vui = &sps->vui_params;

  /* D.12.6: */
  if (!vui->progressive_source_flag && vui->interlaced_source_flag)
    return GST_H266_PARSE_INTERLACED_ONLY;

  if (vui->progressive_source_flag && !vui->interlaced_source_flag)
    return GST_H266_PARSE_PROGRESSIVE_ONLY;

  /* Unknown or unspecified or specified by external means.
     We just assume not interlaced. */
  if (!vui->progressive_source_flag && !vui->interlaced_source_flag)
    return GST_H266_PARSE_PROGRESSIVE_ONLY;

  /* SPEC: When vui_progressive_source_flag and vui_interlaced_source_flag
     in the vui_parameters() syntax structure are both equal to 1, for each
     picture associated with the vui_parameters() syntax structure, a
     frame-field information SEI message associated with the picture shall
     be present. */
  /* Rely on the last frame field info SEI.
     That may be not precise if the SEIs declare the frame and
     field mode differently for each picture. */
  return GST_H266_PARSE_FFI;
}

static void
gst_h266_parse_update_src_caps (GstH266Parse * h266parse, GstCaps * caps)
{
  GstH266SPS *sps = NULL;
  GstCaps *sink_caps, *src_caps;
  gboolean modified = FALSE;
  gint width, height;
  GstBuffer *buf = NULL;
  GstStructure *s = NULL;

  if (G_UNLIKELY (!gst_pad_has_current_caps (GST_BASE_PARSE_SRC_PAD
              (h266parse))))
    modified = TRUE;
  else if (G_UNLIKELY (!h266parse->update_caps))
    return;

  /* if this is being called from the first _setcaps call, caps on the sinkpad
   * aren't set yet and so they need to be passed as an argument */
  if (caps)
    sink_caps = gst_caps_ref (caps);
  else
    sink_caps = gst_pad_get_current_caps (GST_BASE_PARSE_SINK_PAD (h266parse));

  /* carry over input caps as much as possible; override with our own stuff */
  if (!sink_caps)
    sink_caps = gst_caps_new_empty_simple ("video/x-h266");
  else
    s = gst_caps_get_structure (sink_caps, 0);

  sps = h266parse->nalparser->last_sps;
  GST_DEBUG_OBJECT (h266parse, "sps: %p", sps);

  /* only codec-data for nice-and-clean au aligned packetized vvc format */
  if ((h266parse->format == GST_H266_PARSE_FORMAT_VVC1
          || h266parse->format == GST_H266_PARSE_FORMAT_VVI1)
      && h266parse->align == GST_H266_PARSE_ALIGN_AU) {
    buf = gst_h266_parse_make_codec_data (h266parse);
    if (buf && h266parse->codec_data) {
      GstMapInfo map;

      gst_buffer_map (buf, &map, GST_MAP_READ);
      if (map.size != gst_buffer_get_size (h266parse->codec_data) ||
          gst_buffer_memcmp (h266parse->codec_data, 0, map.data, map.size))
        modified = TRUE;

      gst_buffer_unmap (buf, &map);
    } else {
      if (!buf && h266parse->codec_data_in)
        buf = gst_buffer_ref (h266parse->codec_data_in);
      modified = TRUE;
    }
  }

  caps = NULL;
  if (G_UNLIKELY (!sps)) {
    caps = gst_caps_copy (sink_caps);
  } else {
    gint crop_width, crop_height;
    const gchar *chroma_format = NULL;
    GstH266VPS *vps = sps->vps;
    GstH266VUIParams *vui = &sps->vui_params;
    gchar *colorimetry = NULL;
    guint interlaced_mode;

    GST_DEBUG_OBJECT (h266parse, "vps: %p", vps);

    interlaced_mode = get_interlaced_mode (sps);
    if (G_UNLIKELY (h266parse->interlaced_mode != interlaced_mode)) {
      h266parse->interlaced_mode = interlaced_mode;
      GST_INFO_OBJECT (h266parse, "interlaced mode changes to %d",
          h266parse->interlaced_mode);
      modified = TRUE;
    }

    if (sps->conformance_window_flag) {
      crop_width = sps->crop_rect_width;
      crop_height = sps->crop_rect_height;
    } else {
      crop_width = sps->max_width;
      crop_height = sps->max_height;
    }

    if (interlaced_mode == GST_H266_PARSE_INTERLACED_ONLY) {
      crop_height *= 2;
    }

    if (G_UNLIKELY (h266parse->width != crop_width ||
            h266parse->height != crop_height)) {
      h266parse->width = crop_width;
      h266parse->height = crop_height;
      GST_INFO_OBJECT (h266parse, "resolution changed %dx%d",
          h266parse->width, h266parse->height);
      modified = TRUE;
    }

    if (!h266parse->framerate_from_caps) {
      gint fps_num, fps_den;

      /* 0/1 is set as the default in the codec parser */
      fps_num = 0, fps_den = 1;

      if (!(sps->fps_num == 0 && sps->fps_den == 1)) {
        fps_num = sps->fps_num;
        fps_den = sps->fps_den;
      }

      if (interlaced_mode == GST_H266_PARSE_INTERLACED_ONLY) {
        gint new_fps_num, new_fps_den;

        if (!gst_util_fraction_multiply (fps_num, fps_den, 1, 2, &new_fps_num,
                &new_fps_den)) {
          GST_WARNING_OBJECT (h266parse, "Error calculating the new framerate"
              " - integer overflow; setting it to 0/1");
          fps_num = 0;
          fps_den = 1;
        } else {
          fps_num = new_fps_num;
          fps_den = new_fps_den;
        }
      }

      if (G_UNLIKELY (h266parse->fps_num != fps_num
              || h266parse->fps_den != fps_den)) {
        GST_INFO_OBJECT (h266parse, "framerate changed %d/%d", fps_num,
            fps_den);
        h266parse->fps_num = fps_num;
        h266parse->fps_den = fps_den;
        modified = TRUE;
      }
    }

    if (vui->aspect_ratio_info_present_flag) {
      if (G_UNLIKELY ((h266parse->parsed_par_n != vui->par_n)
              && (h266parse->parsed_par_d != sps->vui_params.par_d))) {
        h266parse->parsed_par_n = vui->par_n;
        h266parse->parsed_par_d = vui->par_d;
        GST_INFO_OBJECT (h266parse, "pixel aspect ratio has been changed %d/%d",
            h266parse->parsed_par_n, h266parse->parsed_par_d);
        modified = TRUE;
      }
    }

    if (vui->colour_description_present_flag) {
      GstVideoColorimetry ci = { 0, };
      gchar *old_colorimetry = NULL;

      if (vui->full_range_flag)
        ci.range = GST_VIDEO_COLOR_RANGE_0_255;
      else
        ci.range = GST_VIDEO_COLOR_RANGE_16_235;

      ci.matrix = gst_video_color_matrix_from_iso (vui->matrix_coeffs);
      ci.transfer =
          gst_video_transfer_function_from_iso (vui->transfer_characteristics);
      ci.primaries = gst_video_color_primaries_from_iso (vui->colour_primaries);

      old_colorimetry =
          gst_video_colorimetry_to_string (&h266parse->parsed_colorimetry);
      colorimetry = gst_video_colorimetry_to_string (&ci);

      if (colorimetry && g_strcmp0 (old_colorimetry, colorimetry)) {
        GST_INFO_OBJECT (h266parse,
            "colorimetry has been changed from %s to %s",
            GST_STR_NULL (old_colorimetry), colorimetry);
        h266parse->parsed_colorimetry = ci;
        modified = TRUE;
      }

      g_free (old_colorimetry);
    }

    if (G_UNLIKELY (modified || h266parse->update_caps)) {
      gint fps_num = h266parse->fps_num;
      gint fps_den = h266parse->fps_den;
      GstClockTime latency = 0;

      caps = gst_caps_copy (sink_caps);

      /* sps should give this but upstream overrides */
      if (s && gst_structure_has_field (s, "width"))
        gst_structure_get_int (s, "width", &width);
      else
        width = h266parse->width;

      if (s && gst_structure_has_field (s, "height"))
        gst_structure_get_int (s, "height", &height);
      else
        height = h266parse->height;

      gst_caps_set_simple (caps, "width", G_TYPE_INT, width,
          "height", G_TYPE_INT, height, NULL);

      h266parse->framerate_from_caps = FALSE;
      /* upstream overrides */
      if (s && gst_structure_has_field (s, "framerate")) {
        gst_structure_get_fraction (s, "framerate", &fps_num, &fps_den);
        if (fps_den > 0)
          h266parse->framerate_from_caps = TRUE;
      }

      /* but not necessarily or reliably this */
      if (fps_den > 0) {
        GstStructure *s2;
        GstClockTime val;

        GST_INFO_OBJECT (h266parse, "setting framerate in caps");
        gst_caps_set_simple (caps, "framerate",
            GST_TYPE_FRACTION, fps_num, fps_den, NULL);
        s2 = gst_caps_get_structure (caps, 0);
        gst_structure_get_fraction (s2, "framerate", &h266parse->parsed_fps_n,
            &h266parse->parsed_fps_d);
        gst_base_parse_set_frame_rate (GST_BASE_PARSE (h266parse),
            fps_num, fps_den, 0, 0);
        val = interlaced_mode == GST_H266_PARSE_INTERLACED_ONLY ?
            GST_SECOND / 2 : GST_SECOND;

        /* If we know the frame duration, and if we are not in one of the zero
         * latency pattern, add one frame of latency */
        if (fps_num > 0 &&
            h266parse->in_align != GST_H266_PARSE_ALIGN_AU &&
            !(h266parse->in_align == GST_H266_PARSE_ALIGN_NAL &&
                h266parse->align == GST_H266_PARSE_ALIGN_NAL))
          latency = gst_util_uint64_scale (val, fps_den, fps_num);

        gst_base_parse_set_latency (GST_BASE_PARSE (h266parse), latency,
            latency);
      }

      switch (sps->chroma_format_idc) {
        case 0:
          chroma_format = "4:0:0";
          break;
        case 1:
          chroma_format = "4:2:0";
          break;
        case 2:
          chroma_format = "4:2:2";
          break;
        case 3:
          chroma_format = "4:4:4";
          break;
        default:
          break;
      }

      if (chroma_format)
        /* VVC specifies sps_bitdepth_minus8 for both luma and chroma */
        gst_caps_set_simple (caps, "chroma-format", G_TYPE_STRING,
            chroma_format, "bit-depth-luma", G_TYPE_UINT,
            sps->bitdepth_minus8 + 8, "bit-depth-chroma", G_TYPE_UINT,
            sps->bitdepth_minus8 + 8, NULL);

      if (colorimetry && (!s || !gst_structure_has_field (s, "colorimetry"))) {
        gst_caps_set_simple (caps, "colorimetry", G_TYPE_STRING, colorimetry,
            NULL);
      }
    }
    g_free (colorimetry);
  }

  if (caps) {
    gint par_n, par_d;
    const gchar *mdi_str = NULL;
    const gchar *cll_str = NULL;
    gboolean codec_data_modified = FALSE;
    GstStructure *st;

    gst_caps_set_simple (caps, "parsed", G_TYPE_BOOLEAN, TRUE,
        "stream-format", G_TYPE_STRING,
        gst_h266_parse_get_string (h266parse, TRUE, h266parse->format),
        "alignment", G_TYPE_STRING,
        gst_h266_parse_get_string (h266parse, FALSE, h266parse->align), NULL);

    gst_h266_parse_get_par (h266parse, &par_n, &par_d);

    width = 0;
    height = 0;
    st = gst_caps_get_structure (caps, 0);
    gst_structure_get_int (st, "width", &width);
    gst_structure_get_int (st, "height", &height);

    /* If no resolution info, do not consider aspect ratio */
    if (par_n != 0 && par_d != 0 && width > 0 && height > 0 &&
        (!s || !gst_structure_has_field (s, "pixel-aspect-ratio"))) {
      GST_INFO_OBJECT (h266parse, "PAR %d/%d", par_n, par_d);
      gst_caps_set_simple (caps, "pixel-aspect-ratio", GST_TYPE_FRACTION,
          par_n, par_d, NULL);
    }

    /* set profile and level in caps */
    if (sps) {
      const gchar *profile, *tier, *level;
      GstH266Profile p, p_sink;

      p_sink = GST_H266_PROFILE_INVALID;
      if (s && gst_structure_has_field (s, "profile")) {
        const gchar *profile_sink = gst_structure_get_string (s, "profile");
        p_sink = gst_h266_profile_from_string (profile_sink);
      }

      p = sps->profile_tier_level.profile_idc;
      profile = gst_h266_profile_to_string (p);

      if (profile == NULL) {
        p = p_sink;
        profile = gst_h266_profile_to_string (p);
      }

      if (profile == NULL) {
        p = gst_h266_parse_guess_profile (h266parse, sps, TRUE);
        if (p == GST_H266_PROFILE_INVALID) {
          p = gst_h266_parse_guess_profile (h266parse, sps, FALSE);
          GST_WARNING_OBJECT (h266parse,
              "Fail to recognize profile idc: %d, guess it as %s.",
              sps->profile_tier_level.profile_idc,
              gst_h266_profile_to_string (p));
        }
        profile = gst_h266_profile_to_string (p);
      }
      g_assert (profile != NULL);

      /* If profile from SPS is different from sink caps, try to find
         the more general one, and trust ourself if not found. */
      if (p != p_sink) {
        GstH266Profile tmp;

        tmp = get_common_profile (p, p_sink);

        GST_INFO_OBJECT (h266parse,
            "Upstream profile (%s) is different than in SPS (%s). Using %s.",
            gst_h266_profile_to_string (p_sink), gst_h266_profile_to_string (p),
            tmp != GST_H266_PROFILE_INVALID ? gst_h266_profile_to_string (tmp) :
            gst_h266_profile_to_string (p));

        if (tmp != GST_H266_PROFILE_INVALID)
          p = tmp;
      }

      gst_caps_set_simple (caps, "profile", G_TYPE_STRING, profile, NULL);

      tier = get_tier_string (sps->profile_tier_level.tier_flag);
      if (tier != NULL)
        gst_caps_set_simple (caps, "tier", G_TYPE_STRING, tier, NULL);

      level = get_level_string (sps->profile_tier_level.level_idc);
      if (level != NULL)
        gst_caps_set_simple (caps, "level", G_TYPE_STRING, level, NULL);

      gst_h266_parse_ensure_compatible_profiles (h266parse, caps, sps, p);
    }

    if (s)
      mdi_str = gst_structure_get_string (s, "mastering-display-info");
    if (mdi_str) {
      gst_caps_set_simple (caps, "mastering-display-info", G_TYPE_STRING,
          mdi_str, NULL);
    } else if (h266parse->mastering_display_info_state !=
        GST_H266_PARSE_SEI_EXPIRED &&
        !gst_video_mastering_display_info_add_to_caps
        (&h266parse->mastering_display_info, caps)) {
      GST_WARNING_OBJECT (h266parse,
          "Couldn't set mastering display info to caps");
    }

    if (s)
      cll_str = gst_structure_get_string (s, "content-light-level");
    if (cll_str) {
      gst_caps_set_simple (caps, "content-light-level", G_TYPE_STRING, cll_str,
          NULL);
    } else if (h266parse->content_light_level_state !=
        GST_H266_PARSE_SEI_EXPIRED &&
        !gst_video_content_light_level_add_to_caps
        (&h266parse->content_light_level, caps)) {
      GST_WARNING_OBJECT (h266parse,
          "Couldn't set content light level to caps");
    }

    src_caps = gst_pad_get_current_caps (GST_BASE_PARSE_SRC_PAD (h266parse));

    if (src_caps) {
      GstStructure *src_caps_str = gst_caps_get_structure (src_caps, 0);

      /* use codec data from old caps for comparison if we have pushed frame
       * for now. we don't want to resend caps if everything is same except
       * codec data.
       * However, if the updated sps/pps is not in bitstream, we should put
       * it on bitstream. */
      if (gst_structure_has_field (src_caps_str, "codec_data")) {
        const GValue *codec_data_value =
            gst_structure_get_value (src_caps_str, "codec_data");

        if (!GST_VALUE_HOLDS_BUFFER (codec_data_value)) {
          GST_WARNING_OBJECT (h266parse, "codec_data does not hold buffer");
        } else if (!h266parse->first_frame) {
          /* If there is no pushed frame before, we can update caps without
           * worry. But updating codec_data in the middle of frames
           * (especially on non-keyframe) might make downstream be confused.
           * Therefore we are setting old codec data (i.e., was pushed to
           * downstream previously) to new caps candidate here for
           * gst_caps_is_strictly_equal() to be returned TRUE if only
           * the codec_data is different, and to avoid re-sending caps it
           * that case.
           */
          gst_caps_set_value (caps, "codec_data", codec_data_value);

          /* check for codec_data update to re-send sps/pps inband data if
           * current frame has no sps/pps but upstream codec_data was updated.
           * Note that have_vps_in_frame is skipped here since it's optional. */
          if ((!h266parse->have_sps_in_frame || !h266parse->have_pps_in_frame)
              && buf) {
            GstBuffer *codec_data_buf = gst_value_get_buffer (codec_data_value);
            GstMapInfo map;

            gst_buffer_map (buf, &map, GST_MAP_READ);
            if (map.size != gst_buffer_get_size (codec_data_buf) ||
                gst_buffer_memcmp (codec_data_buf, 0, map.data, map.size)) {
              codec_data_modified = TRUE;
            }

            gst_buffer_unmap (buf, &map);
          }
        }
      } else if (!buf) {
        GstStructure *s;
        /* remove any left-over codec-data hanging around */
        s = gst_caps_get_structure (caps, 0);
        gst_structure_remove_field (s, "codec_data");
      }
    }

    if (!(src_caps && gst_caps_is_strictly_equal (src_caps, caps))) {
      /* update codec data to new value */
      if (buf) {
        gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER, buf, NULL);
        gst_buffer_replace (&h266parse->codec_data, buf);
        gst_buffer_unref (buf);
        buf = NULL;
      } else {
        GstStructure *s;
        /* remove any left-over codec-data hanging around */
        s = gst_caps_get_structure (caps, 0);
        gst_structure_remove_field (s, "codec_data");
        gst_buffer_replace (&h266parse->codec_data, NULL);
      }

      gst_pad_set_caps (GST_BASE_PARSE_SRC_PAD (h266parse), caps);
    } else if (codec_data_modified) {
      GST_DEBUG_OBJECT (h266parse,
          "Only codec_data is different, need inband vps/sps/pps update.");
      /* this will insert updated codec_data with next idr */
      h266parse->push_codec = TRUE;
    }

    if (src_caps)
      gst_caps_unref (src_caps);
    gst_caps_unref (caps);
  }

  gst_caps_unref (sink_caps);
  if (buf)
    gst_buffer_unref (buf);
}

/* sends a codec NAL downstream, decorating and transforming as needed.
 * No ownership is taken of @nal */
static GstFlowReturn
gst_h266_parse_push_codec_buffer (GstH266Parse * parse, GstBuffer * nal,
    GstBuffer * buffer)
{
  GstMapInfo map;

  gst_buffer_map (nal, &map, GST_MAP_READ);
  nal = gst_h266_parse_wrap_nal (parse, parse->format, map.data, map.size);
  gst_buffer_unmap (nal, &map);

  if (parse->discont) {
    GST_BUFFER_FLAG_SET (nal, GST_BUFFER_FLAG_DISCONT);
    parse->discont = FALSE;
  }

  GST_BUFFER_PTS (nal) = GST_BUFFER_PTS (buffer);
  GST_BUFFER_DTS (nal) = GST_BUFFER_DTS (buffer);
  GST_BUFFER_DURATION (nal) = 0;

  return gst_pad_push (GST_BASE_PARSE_SRC_PAD (parse), nal);
}

static gboolean
gst_h266_parse_handle_vps_sps_pps_aps_nals (GstH266Parse * parse,
    GstBuffer * buffer, GstBaseParseFrame * frame)
{
  GstBuffer *codec_nal;
  gint i, j;
  gboolean send_done = FALSE;

  if (parse->have_vps_in_frame && parse->have_sps_in_frame
      && parse->have_pps_in_frame) {
    GST_DEBUG_OBJECT (parse, "VPS/SPS/PPS already exist in frame, "
        "no need to insert.");
    return TRUE;
  }

  if (parse->align == GST_H266_PARSE_ALIGN_NAL) {
    /* send separate config NAL buffer one by one. */
    GST_DEBUG_OBJECT (parse, "- sending VPS/SPS/PPS/APS");

    for (i = 0; i < GST_H266_MAX_VPS_COUNT; i++) {
      if ((codec_nal = parse->vps_nals[i])) {
        GST_DEBUG_OBJECT (parse, "sending VPS nal");
        gst_h266_parse_push_codec_buffer (parse, codec_nal, buffer);
        send_done = TRUE;
      }
    }

    for (i = 0; i < GST_H266_MAX_SPS_COUNT; i++) {
      if ((codec_nal = parse->sps_nals[i])) {
        GST_DEBUG_OBJECT (parse, "sending SPS nal");
        gst_h266_parse_push_codec_buffer (parse, codec_nal, buffer);
        send_done = TRUE;
      }
    }

    for (i = 0; i < GST_H266_MAX_PPS_COUNT; i++) {
      if ((codec_nal = parse->pps_nals[i])) {
        GST_DEBUG_OBJECT (parse, "sending PPS nal");
        gst_h266_parse_push_codec_buffer (parse, codec_nal, buffer);
        send_done = TRUE;
      }
    }

    for (i = 0; i < GST_H266_APS_TYPE_MAX; i++) {
      for (j = 0; j < GST_H266_MAX_APS_COUNT; j++) {
        if ((codec_nal = parse->aps_nals[i][j])) {
          GST_DEBUG_OBJECT (parse, "sending APS nal");
          gst_h266_parse_push_codec_buffer (parse, codec_nal, buffer);
          send_done = TRUE;
        }
      }
    }
  } else {
    /* insert config NALs into AU */
    GstByteWriter bw;
    GstBuffer *new_buf;
    const gboolean bs = parse->format == GST_H266_PARSE_FORMAT_BYTE;
    const gint nls = 4 - parse->nal_length_size;
    gboolean ok;

    gst_byte_writer_init_with_size (&bw, gst_buffer_get_size (buffer), FALSE);

    g_assert (parse->idr_pos > 0);
    ok = gst_byte_writer_put_buffer (&bw, buffer, 0, parse->idr_pos);

    GST_DEBUG_OBJECT (parse, "- inserting VPS/SPS/PPS.");

    for (i = 0; i < GST_H266_MAX_VPS_COUNT; i++) {
      if ((codec_nal = parse->vps_nals[i])) {
        gsize nal_size = gst_buffer_get_size (codec_nal);

        GST_DEBUG_OBJECT (parse, "inserting VPS nal.");

        if (bs) {
          /* Write the start code. */
          ok &= gst_byte_writer_put_uint32_be (&bw, 0x01);
        } else {
          ok &= gst_byte_writer_put_uint32_be (&bw, (nal_size << (nls * 8)));
          ok &= gst_byte_writer_set_pos (&bw,
              gst_byte_writer_get_pos (&bw) - nls);
        }

        ok &= gst_byte_writer_put_buffer (&bw, codec_nal, 0, nal_size);
        send_done = TRUE;
      }
    }

    for (i = 0; i < GST_H266_MAX_SPS_COUNT; i++) {
      if ((codec_nal = parse->sps_nals[i])) {
        gsize nal_size = gst_buffer_get_size (codec_nal);

        GST_DEBUG_OBJECT (parse, "inserting SPS nal.");

        if (bs) {
          /* Write the start code. */
          ok &= gst_byte_writer_put_uint32_be (&bw, 0x01);
        } else {
          ok &= gst_byte_writer_put_uint32_be (&bw, (nal_size << (nls * 8)));
          ok &= gst_byte_writer_set_pos (&bw,
              gst_byte_writer_get_pos (&bw) - nls);
        }

        ok &= gst_byte_writer_put_buffer (&bw, codec_nal, 0, nal_size);
        send_done = TRUE;
      }
    }

    for (i = 0; i < GST_H266_MAX_PPS_COUNT; i++) {
      if ((codec_nal = parse->pps_nals[i])) {
        gsize nal_size = gst_buffer_get_size (codec_nal);

        GST_DEBUG_OBJECT (parse, "inserting PPS nal.");

        if (bs) {
          /* Write the start code. */
          ok &= gst_byte_writer_put_uint32_be (&bw, 0x01);
        } else {
          ok &= gst_byte_writer_put_uint32_be (&bw, (nal_size << (nls * 8)));
          ok &= gst_byte_writer_set_pos (&bw,
              gst_byte_writer_get_pos (&bw) - nls);
        }

        ok &= gst_byte_writer_put_buffer (&bw, codec_nal, 0, nal_size);
        send_done = TRUE;
      }
    }

    for (i = 0; i < GST_H266_APS_TYPE_MAX; i++) {
      for (j = 0; j < GST_H266_MAX_APS_COUNT; j++) {
        if ((codec_nal = parse->aps_nals[i][j])) {
          gsize nal_size = gst_buffer_get_size (codec_nal);

          GST_DEBUG_OBJECT (parse, "inserting APS nal.");

          if (bs) {
            /* Write the start code. */
            ok &= gst_byte_writer_put_uint32_be (&bw, 0x01);
          } else {
            ok &= gst_byte_writer_put_uint32_be (&bw, (nal_size << (nls * 8)));
            ok &= gst_byte_writer_set_pos (&bw,
                gst_byte_writer_get_pos (&bw) - nls);
          }

          ok &= gst_byte_writer_put_buffer (&bw, codec_nal, 0, nal_size);
          send_done = TRUE;
        }
      }
    }

    ok &= gst_byte_writer_put_buffer (&bw, buffer, parse->idr_pos, -1);

    /* collect result and push */
    new_buf = gst_byte_writer_reset_and_get_buffer (&bw);
    gst_buffer_copy_into (new_buf, buffer, GST_BUFFER_COPY_METADATA, 0, -1);
    /* should already be keyframe/IDR, but it may not have been,
     * so mark it as such to avoid being discarded by picky decoder */
    GST_BUFFER_FLAG_UNSET (new_buf, GST_BUFFER_FLAG_DELTA_UNIT);
    gst_buffer_replace (&frame->out_buffer, new_buf);
    gst_buffer_unref (new_buf);

    /* some result checking seems to make some compilers happy */
    if (G_UNLIKELY (!ok))
      GST_ERROR_OBJECT (parse, "failed to insert VPS/SPS/PPS.");
  }

  return send_done;
}

static GstEvent *
check_pending_key_unit_event (GstEvent * pending_event, GstSegment * segment,
    GstClockTime timestamp, guint flags, GstClockTime pending_key_unit_ts)
{
  GstClockTime running_time, stream_time;
  gboolean all_headers;
  guint count;
  GstEvent *event = NULL;

  g_return_val_if_fail (segment != NULL, NULL);

  if (pending_event == NULL)
    goto out;

  if (GST_CLOCK_TIME_IS_VALID (pending_key_unit_ts) &&
      timestamp == GST_CLOCK_TIME_NONE)
    goto out;

  running_time = gst_segment_to_running_time (segment,
      GST_FORMAT_TIME, timestamp);

  GST_INFO ("now %" GST_TIME_FORMAT " wanted %" GST_TIME_FORMAT,
      GST_TIME_ARGS (running_time), GST_TIME_ARGS (pending_key_unit_ts));

  if (GST_CLOCK_TIME_IS_VALID (pending_key_unit_ts) &&
      running_time < pending_key_unit_ts)
    goto out;

  if (flags & GST_BUFFER_FLAG_DELTA_UNIT) {
    GST_DEBUG ("pending force key unit, waiting for keyframe");
    goto out;
  }

  stream_time = gst_segment_to_stream_time (segment,
      GST_FORMAT_TIME, timestamp);

  if (!gst_video_event_parse_upstream_force_key_unit (pending_event,
          NULL, &all_headers, &count)) {
    gst_video_event_parse_downstream_force_key_unit (pending_event, NULL,
        NULL, NULL, &all_headers, &count);
  }

  event =
      gst_video_event_new_downstream_force_key_unit (timestamp, stream_time,
      running_time, all_headers, count);
  gst_event_set_seqnum (event, gst_event_get_seqnum (pending_event));

out:
  return event;
}

static void
gst_h266_parse_prepare_key_unit (GstH266Parse * parse, GstEvent * event)
{
  GstClockTime running_time;
  guint count;
#ifndef GST_DISABLE_GST_DEBUG
  gboolean have_vps, have_sps, have_pps, have_aps;
  gint i, j;
#endif

  parse->pending_key_unit_ts = GST_CLOCK_TIME_NONE;
  gst_event_replace (&parse->force_key_unit_event, NULL);

  gst_video_event_parse_downstream_force_key_unit (event,
      NULL, NULL, &running_time, NULL, &count);

  GST_INFO_OBJECT (parse, "pushing downstream force-key-unit event %d "
      "%" GST_TIME_FORMAT " count %d", gst_event_get_seqnum (event),
      GST_TIME_ARGS (running_time), count);

  gst_pad_push_event (GST_BASE_PARSE_SRC_PAD (parse), event);

#ifndef GST_DISABLE_GST_DEBUG
  have_vps = have_sps = have_pps = FALSE;
  for (i = 0; i < GST_H266_MAX_VPS_COUNT; i++) {
    if (parse->vps_nals[i] != NULL) {
      have_vps = TRUE;
      break;
    }
  }
  for (i = 0; i < GST_H266_MAX_SPS_COUNT; i++) {
    if (parse->sps_nals[i] != NULL) {
      have_sps = TRUE;
      break;
    }
  }
  for (i = 0; i < GST_H266_MAX_PPS_COUNT; i++) {
    if (parse->pps_nals[i] != NULL) {
      have_pps = TRUE;
      break;
    }
  }
  for (i = 0; i < GST_H266_APS_TYPE_MAX; i++) {
    for (j = 0; j < GST_H266_MAX_APS_COUNT; j++) {
      if (parse->aps_nals[i][j] != NULL) {
        have_aps = TRUE;
        break;
      }
    }
  }

  GST_INFO_OBJECT (parse,
      "preparing key unit, have vps %d, have sps %d, have pps %d, have_aps %d",
      have_vps, have_sps, have_pps, have_aps);
#endif

  /* set push_codec to TRUE so that pre_push_frame sends VPS/SPS/PPS again */
  parse->push_codec = TRUE;
}

static GstFlowReturn
gst_h266_parse_pre_push_frame (GstBaseParse * parse, GstBaseParseFrame * frame)
{
  GstBuffer *buffer;
  GstBuffer *parse_buffer = NULL;
  GstEvent *event;
  GstH266Parse *h266parse = GST_H266_PARSE (parse);

  if (h266parse->first_frame) {
    GstTagList *taglist;
    GstCaps *caps;

    /* codec tag */
    caps = gst_pad_get_current_caps (GST_BASE_PARSE_SRC_PAD (parse));
    if (G_UNLIKELY (caps == NULL)) {
      if (GST_PAD_IS_FLUSHING (GST_BASE_PARSE_SRC_PAD (parse))) {
        GST_INFO_OBJECT (parse, "Src pad is flushing");
        return GST_FLOW_FLUSHING;
      } else {
        GST_INFO_OBJECT (parse, "Src pad is not negotiated!");
        return GST_FLOW_NOT_NEGOTIATED;
      }
    }

    taglist = gst_tag_list_new_empty ();
    gst_pb_utils_add_codec_description_to_tag_list (taglist,
        GST_TAG_VIDEO_CODEC, caps);
    gst_caps_unref (caps);

    gst_base_parse_merge_tags (parse, taglist, GST_TAG_MERGE_REPLACE);
    gst_tag_list_unref (taglist);

    /* also signals the end of first-frame processing */
    h266parse->first_frame = FALSE;
  }

  buffer = frame->buffer;

  if ((event = check_pending_key_unit_event (h266parse->force_key_unit_event,
              &parse->segment, GST_BUFFER_TIMESTAMP (buffer),
              GST_BUFFER_FLAGS (buffer), h266parse->pending_key_unit_ts))) {
    gst_h266_parse_prepare_key_unit (h266parse, event);
  }

  /* If aligned to nal, each nal will be pushed immediately,
     no idr accumulation. */
  if (h266parse->align == GST_H266_PARSE_ALIGN_NAL)
    g_assert (h266parse->idr_pos <= 0);

  /* periodic VPS/SPS/PPS sending */
  if (h266parse->interval > 0 || h266parse->push_codec) {
    GstClockTime timestamp = GST_BUFFER_TIMESTAMP (buffer);
    guint64 diff;
    gboolean initial_frame = FALSE;

    /* init */
    if (!GST_CLOCK_TIME_IS_VALID (h266parse->last_report)) {
      h266parse->last_report = timestamp;
      initial_frame = TRUE;
    }

    if (h266parse->idr_pos >= 0) {
      GST_LOG_OBJECT (h266parse, "IDR nal at offset %d", h266parse->idr_pos);

      if (timestamp > h266parse->last_report)
        diff = timestamp - h266parse->last_report;
      else
        diff = 0;

      GST_LOG_OBJECT (h266parse,
          "now %" GST_TIME_FORMAT ", last VPS/SPS/PPS %" GST_TIME_FORMAT,
          GST_TIME_ARGS (timestamp), GST_TIME_ARGS (h266parse->last_report));

      GST_DEBUG_OBJECT (h266parse,
          "interval since last VPS/SPS/PPS %" GST_TIME_FORMAT,
          GST_TIME_ARGS (diff));

      if (GST_TIME_AS_SECONDS (diff) >= h266parse->interval ||
          initial_frame || h266parse->push_codec) {
        GstClockTime new_ts;

        /* avoid overwriting a perfectly fine timestamp */
        new_ts = GST_CLOCK_TIME_IS_VALID (timestamp) ? timestamp :
            h266parse->last_report;

        if (gst_h266_parse_handle_vps_sps_pps_aps_nals (h266parse,
                buffer, frame)) {
          h266parse->last_report = new_ts;
        }
      }

      /* we pushed whatever we had */
      h266parse->push_codec = FALSE;
      h266parse->have_vps = FALSE;
      h266parse->have_sps = FALSE;
      h266parse->have_pps = FALSE;
      h266parse->have_aps = FALSE;
      h266parse->state &= GST_H266_PARSE_STATE_VALID_SPS_PPS;
    }
  } else if (h266parse->interval == -1) {
    if (h266parse->idr_pos >= 0) {
      GST_LOG_OBJECT (h266parse, "IDR nal at offset %d", h266parse->idr_pos);

      gst_h266_parse_handle_vps_sps_pps_aps_nals (h266parse, buffer, frame);

      /* we pushed whatever we had */
      h266parse->push_codec = FALSE;
      h266parse->have_vps = FALSE;
      h266parse->have_sps = FALSE;
      h266parse->have_pps = FALSE;
      h266parse->have_aps = FALSE;
      h266parse->state &= GST_H266_PARSE_STATE_VALID_SPS_PPS;
    }
  }

  if (frame->out_buffer) {
    parse_buffer = frame->out_buffer =
        gst_buffer_make_writable (frame->out_buffer);
  } else {
    parse_buffer = frame->buffer = gst_buffer_make_writable (frame->buffer);
  }

  if (h266parse->interlaced_mode != GST_H266_PARSE_PROGRESSIVE_ONLY &&
      h266parse->sei_frame_field.valid) {
    if (h266parse->interlaced_mode == GST_H266_PARSE_INTERLACED_ONLY)
      GST_BUFFER_FLAG_SET (parse_buffer, GST_VIDEO_BUFFER_FLAG_INTERLACED);

    if (h266parse->sei_frame_field.field_pic_flag) {
      GST_BUFFER_FLAG_SET (parse_buffer, GST_VIDEO_BUFFER_FLAG_INTERLACED);

      if (h266parse->sei_frame_field.bottom_field_flag) {
        GST_BUFFER_FLAG_SET (parse_buffer, GST_VIDEO_BUFFER_FLAG_BOTTOM_FIELD);
      } else {
        GST_BUFFER_FLAG_SET (parse_buffer, GST_VIDEO_BUFFER_FLAG_TOP_FIELD);
      }
    }
  }

  /* TODO: Handle the video_time_code_meta. */

  gst_video_push_user_data ((GstElement *) h266parse, &h266parse->user_data,
      parse_buffer);

  gst_video_push_user_data_unregistered ((GstElement *) h266parse,
      &h266parse->user_data_unregistered, parse_buffer);

  gst_h266_parse_reset_frame (h266parse);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_h266_parse_parse_frame (GstBaseParse * parse, GstBaseParseFrame * frame)
{
  GstH266Parse *h266parse;
  GstBuffer *buffer;
  guint av;

  h266parse = GST_H266_PARSE (parse);
  buffer = frame->buffer;

  gst_h266_parse_update_src_caps (h266parse, NULL);

  if (h266parse->keyframe)
    GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  else
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);

  if (h266parse->discard_bidirectional && h266parse->bidirectional)
    goto discard;

  if (h266parse->header)
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_HEADER);
  else
    GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_HEADER);

  if (h266parse->discont) {
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
    h266parse->discont = FALSE;
  }

  if (h266parse->marker) {
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_MARKER);
    h266parse->marker = FALSE;
  } else {
    GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_MARKER);
  }

  /* replace with transformed VVC output if applicable */
  av = gst_adapter_available (h266parse->frame_out);
  if (av) {
    GstBuffer *buf;

    buf = gst_adapter_take_buffer (h266parse->frame_out, av);
    gst_buffer_copy_into (buf, buffer, GST_BUFFER_COPY_METADATA, 0, -1);
    gst_buffer_replace (&frame->out_buffer, buf);
    gst_buffer_unref (buf);
  }

done:
  return GST_FLOW_OK;

discard:
  GST_DEBUG_OBJECT (h266parse, "Discarding bidirectional frame");
  frame->flags |= GST_BASE_PARSE_FRAME_FLAG_DROP;
  gst_h266_parse_reset_frame (h266parse);
  goto done;
}

static gboolean
gst_h266_parse_set_caps (GstBaseParse * parse, GstCaps * caps)
{
  GstH266Parse *h266parse;
  GstStructure *str;
  guint format, align;
  GstCaps *old_caps;
  GstBuffer *codec_data = NULL;
  const GValue *value;

  h266parse = GST_H266_PARSE (parse);

  /* reset */
  h266parse->push_codec = FALSE;

  old_caps = gst_pad_get_current_caps (GST_BASE_PARSE_SINK_PAD (parse));
  if (old_caps) {
    if (!gst_caps_is_equal (old_caps, caps))
      gst_h266_parse_reset_stream_info (h266parse);
    gst_caps_unref (old_caps);
  }

  str = gst_caps_get_structure (caps, 0);

  /* accept upstream info if provided */
  gst_structure_get_int (str, "width", &h266parse->width);
  gst_structure_get_int (str, "height", &h266parse->height);
  gst_structure_get_fraction (str, "framerate", &h266parse->fps_num,
      &h266parse->fps_den);
  gst_structure_get_fraction (str, "pixel-aspect-ratio",
      &h266parse->upstream_par_n, &h266parse->upstream_par_d);

  /* get upstream format and align from caps */
  gst_h266_parse_format_from_caps (h266parse, caps, &format, &align);

  /* packetized video has a codec_data */
  if (format != GST_H266_PARSE_FORMAT_BYTE &&
      (value = gst_structure_get_value (str, "codec_data"))) {

    GST_DEBUG_OBJECT (h266parse, "have packetized h266");
    /* make note for optional split processing */
    h266parse->packetized = TRUE;

    codec_data = gst_value_get_buffer (value);
    if (!codec_data)
      goto wrong_type;

    /* TODO: Need to refer to the new ISO/IEC 14496-15 to handle codec data. */
    goto vvcc_failed;

    /* don't confuse codec_data with inband vps/sps/pps */
    h266parse->have_vps_in_frame = FALSE;
    h266parse->have_sps_in_frame = FALSE;
    h266parse->have_pps_in_frame = FALSE;
    h266parse->have_aps_in_frame = FALSE;
  } else {
    GST_DEBUG_OBJECT (h266parse, "have bytestream h266");
    /* nothing to pre-process */
    h266parse->packetized = FALSE;
    /* we have 4 sync bytes */
    h266parse->nal_length_size = 4;

    if (format == GST_H266_PARSE_FORMAT_NONE) {
      format = GST_H266_PARSE_FORMAT_BYTE;
      align = GST_H266_PARSE_ALIGN_AU;
    }
  }

  {
    GstCaps *in_caps;

    /* prefer input type determined above */
    in_caps = gst_caps_new_simple ("video/x-h266",
        "parsed", G_TYPE_BOOLEAN, TRUE,
        "stream-format", G_TYPE_STRING,
        gst_h266_parse_get_string (h266parse, TRUE, format),
        "alignment", G_TYPE_STRING,
        gst_h266_parse_get_string (h266parse, FALSE, align), NULL);
    /* negotiate with downstream, sets ->format and ->align */
    gst_h266_parse_negotiate (h266parse, format, in_caps);
    gst_caps_unref (in_caps);
  }

  if (format == h266parse->format && align == h266parse->align) {
    /* we did parse codec-data and might supplement src caps */
    gst_h266_parse_update_src_caps (h266parse, caps);
  } else if (format == GST_H266_PARSE_FORMAT_VVC1
      || format == GST_H266_PARSE_FORMAT_VVI1) {
    /* if input != output, and input is vvc, must split before anything else */
    /* arrange to insert codec-data in-stream if needed.
     * src caps are only arranged for later on */
    h266parse->push_codec = TRUE;
    h266parse->have_vps = FALSE;
    h266parse->have_sps = FALSE;
    h266parse->have_pps = FALSE;
    h266parse->have_aps = FALSE;
    if (h266parse->align == GST_H266_PARSE_ALIGN_NAL)
      h266parse->split_packetized = TRUE;
    h266parse->packetized = TRUE;
  }

  h266parse->in_align = align;

  return TRUE;

  /* ERRORS */
vvcc_failed:
  {
    GST_DEBUG_OBJECT (h266parse, "Failed to parse vvcC data");
    goto refuse_caps;
  }
wrong_type:
  {
    GST_DEBUG_OBJECT (h266parse, "wrong codec-data type");
    goto refuse_caps;
  }
refuse_caps:
  {
    GST_WARNING_OBJECT (h266parse, "refused caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }
}

static void
remove_fields (GstCaps * caps, gboolean all)
{
  guint i, n;

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    GstStructure *s = gst_caps_get_structure (caps, i);

    if (all) {
      gst_structure_remove_field (s, "alignment");
      gst_structure_remove_field (s, "stream-format");
    }
    gst_structure_remove_field (s, "parsed");
  }
}

static GstCaps *
gst_h266_parse_get_caps (GstBaseParse * parse, GstCaps * filter)
{
  GstCaps *peercaps, *templ;
  GstCaps *res, *tmp, *pcopy;

  templ = gst_pad_get_pad_template_caps (GST_BASE_PARSE_SINK_PAD (parse));
  if (filter) {
    GstCaps *fcopy = gst_caps_copy (filter);
    /* Remove the fields we convert */
    remove_fields (fcopy, TRUE);
    peercaps = gst_pad_peer_query_caps (GST_BASE_PARSE_SRC_PAD (parse), fcopy);
    gst_caps_unref (fcopy);
  } else
    peercaps = gst_pad_peer_query_caps (GST_BASE_PARSE_SRC_PAD (parse), NULL);

  pcopy = gst_caps_copy (peercaps);
  remove_fields (pcopy, TRUE);

  res = gst_caps_intersect_full (pcopy, templ, GST_CAPS_INTERSECT_FIRST);
  gst_caps_unref (pcopy);
  gst_caps_unref (templ);

  if (filter) {
    GstCaps *tmp = gst_caps_intersect_full (res, filter,
        GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (res);
    res = tmp;
  }

  /* Try if we can put the downstream caps first */
  pcopy = gst_caps_copy (peercaps);
  remove_fields (pcopy, FALSE);
  tmp = gst_caps_intersect_full (pcopy, res, GST_CAPS_INTERSECT_FIRST);
  gst_caps_unref (pcopy);
  if (!gst_caps_is_empty (tmp))
    res = gst_caps_merge (tmp, res);
  else
    gst_caps_unref (tmp);

  gst_caps_unref (peercaps);
  return res;
}

static void
gst_h266_parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstH266Parse *parse;
  parse = GST_H266_PARSE (object);

  switch (prop_id) {
    case PROP_CONFIG_INTERVAL:
      parse->interval = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_h266_parse_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstH266Parse *parse;
  parse = GST_H266_PARSE (object);

  switch (prop_id) {
    case PROP_CONFIG_INTERVAL:
      g_value_set_int (value, parse->interval);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
