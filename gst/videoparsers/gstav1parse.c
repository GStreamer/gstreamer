/* GStreamer
 * Copyright (C) 2020 He Junyan <junyan.he@intel.com>
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
 * SECTION:element-av1parse
 * @title: av1parse
 * @short_description: An AV1 stream parse.
 *
 * The minimal unit should be the BYTE.
 * There are four types of AV1 alignment in the AV1 stream.
 *
 * alignment: byte, obu, frame, tu
 *
 * 1. Aligned to byte. The basic and default one for input.
 * 2. Aligned to obu(Open Bitstream Units).
 * 3. Aligned to frame. The default one for output. This ensures that
 *    each buffer contains only one frame or frame header with the
 *    show_existing flag for the base or sub layer. It is useful for
 *    the decoder.
 * 4. Aligned to tu(Temporal Unit). A temporal unit consists of all the
 *    OBUs that are associated with a specific, distinct time instant.
 *    When scalability is disabled, it contains just exact one showing
 *    frame(may contain several unshowing frames). When scalability is
 *    enabled, it contains frames depending on the layer number. It should
 *    begin with a temporal delimiter obu. It may be useful for mux/demux
 *    to index the data of some timestamp.
 *
 * The annex B define a special format for the temporal unit. The size of
 * each temporal unit is extract out to the header of the buffer, and no
 * size field inside the each obu. There are two stream formats:
 *
 * stream-format: obu-stream, annexb
 *
 * 1. obu-stream. The basic and default one.
 * 2. annexb. A special stream of temporal unit. It also implies that the
 *    alignment should be TU.
 *
 * This AV1 parse implements the conversion between the alignments and the
 * stream-formats. If the input and output have the same alignment and the
 * same stream-format, it will check and bypass the data.
 *
 * ## Example launch line to generate annex B format AV1 stream:
 * ```
 * gst-launch-1.0 filesrc location=sample.av1 ! ivfparse ! av1parse !  \
 *   video/x-av1,alignment=\(string\)tu,stream-format=\(string\)annexb ! \
 *   filesink location=matroskamux ! filesink location=trans.mkv
 * ```
 *
 * Since: 1.20
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/base/gstbitreader.h>
#include <gst/base/gstbitwriter.h>
#include <gst/codecparsers/gstav1parser.h>
#include <gst/video/video.h>
#include "gstvideoparserselements.h"
#include "gstav1parse.h"

#include <string.h>

#define GST_AV1_MAX_LEB_128_SIZE 8

GST_DEBUG_CATEGORY (av1_parse_debug);
#define GST_CAT_DEFAULT av1_parse_debug

/* We combine the stream format and the alignment
   together. When stream format is annexb, the
   alignment must be TU. */
typedef enum
{
  GST_AV1_PARSE_ALIGN_ERROR = -1,
  GST_AV1_PARSE_ALIGN_NONE = 0,
  GST_AV1_PARSE_ALIGN_BYTE,
  GST_AV1_PARSE_ALIGN_OBU,
  GST_AV1_PARSE_ALIGN_FRAME,
  GST_AV1_PARSE_ALIGN_TEMPORAL_UNIT,
  GST_AV1_PARSE_ALIGN_TEMPORAL_UNIT_ANNEX_B,
} GstAV1ParseAligment;

struct _GstAV1Parse
{
  GstBaseParse parent;

  gint width;
  gint height;
  gint subsampling_x;
  gint subsampling_y;
  gboolean mono_chrome;
  guint8 bit_depth;
  gchar *colorimetry;
  GstAV1Profile profile;

  GstAV1ParseAligment in_align;
  GstAV1ParseAligment align;

  GstAV1Parser *parser;
  GstAdapter *cache_out;
  guint last_parsed_offset;
  GstAdapter *frame_cache;
  guint highest_spatial_id;
  gboolean update_caps;
  gboolean discont;
  gboolean header;
  gboolean keyframe;
};

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-av1"));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-av1, parsed = (boolean) true, "
        "stream-format=(string) { obu-stream, annexb }, "
        "alignment=(string) { obu, tu, frame }"));

#define parent_class gst_av1_parse_parent_class
G_DEFINE_TYPE (GstAV1Parse, gst_av1_parse, GST_TYPE_BASE_PARSE);
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (av1parse, "av1parse", GST_RANK_SECONDARY,
    GST_TYPE_AV1_PARSE, videoparsers_element_init (plugin));

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

static const gchar *
_obu_name (GstAV1OBUType type)
{
  switch (type) {
    case GST_AV1_OBU_SEQUENCE_HEADER:
      return "sequence header";
    case GST_AV1_OBU_TEMPORAL_DELIMITER:
      return "temporal delimiter";
    case GST_AV1_OBU_FRAME_HEADER:
      return "frame header";
    case GST_AV1_OBU_TILE_GROUP:
      return "tile group";
    case GST_AV1_OBU_METADATA:
      return "metadata";
    case GST_AV1_OBU_FRAME:
      return "frame";
    case GST_AV1_OBU_REDUNDANT_FRAME_HEADER:
      return "redundant frame header";
    case GST_AV1_OBU_TILE_LIST:
      return "tile list";
    case GST_AV1_OBU_PADDING:
      return "padding";
    default:
      return "unknown";
  }

  return NULL;
}

static guint32
_read_leb128 (guint8 * data, GstAV1ParserResult * retval, guint32 * comsumed)
{
  guint8 leb128_byte = 0;
  guint64 value = 0;
  gint i;
  gboolean result;
  GstBitReader br;
  guint32 cur_pos;

  gst_bit_reader_init (&br, data, 8);

  cur_pos = gst_bit_reader_get_pos (&br);
  for (i = 0; i < 8; i++) {
    leb128_byte = 0;
    result = gst_bit_reader_get_bits_uint8 (&br, &leb128_byte, 8);
    if (result == FALSE) {
      *retval = GST_AV1_PARSER_BITSTREAM_ERROR;
      return 0;
    }

    value |= (((gint) leb128_byte & 0x7f) << (i * 7));
    if (!(leb128_byte & 0x80))
      break;
  }

  *comsumed = (gst_bit_reader_get_pos (&br) - cur_pos) / 8;
  /* check for bitstream conformance see chapter4.10.5 */
  if (value < G_MAXUINT32) {
    *retval = GST_AV1_PARSER_OK;
    return (guint32) value;
  } else {
    GST_WARNING ("invalid leb128");
    *retval = GST_AV1_PARSER_BITSTREAM_ERROR;
    return 0;
  }
}

static gsize
_leb_size_in_bytes (guint64 value)
{
  gsize size = 0;
  do {
    ++size;
  } while ((value >>= 7) != 0);

  return size;
}

static gboolean
_write_leb128 (guint8 * data, guint * len, guint64 value)
{
  guint leb_size = _leb_size_in_bytes (value);
  guint i;

  if (value > G_MAXUINT32 || leb_size > GST_AV1_MAX_LEB_128_SIZE)
    return FALSE;

  for (i = 0; i < leb_size; ++i) {
    guint8 byte = value & 0x7f;
    value >>= 7;

    /* Signal that more bytes follow. */
    if (value != 0)
      byte |= 0x80;

    *(data + i) = byte;
  }

  *len = leb_size;
  return TRUE;
}

static gboolean gst_av1_parse_start (GstBaseParse * parse);
static gboolean gst_av1_parse_stop (GstBaseParse * parse);
static GstFlowReturn gst_av1_parse_handle_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, gint * skipsize);
static gboolean gst_av1_parse_set_sink_caps (GstBaseParse * parse,
    GstCaps * caps);
static GstCaps *gst_av1_parse_get_sink_caps (GstBaseParse * parse,
    GstCaps * filter);

static void
gst_av1_parse_reset (GstAV1Parse * self)
{
  self->width = 0;
  self->height = 0;
  self->subsampling_x = -1;
  self->subsampling_y = -1;
  self->mono_chrome = FALSE;
  self->profile = GST_AV1_PROFILE_UNDEFINED;
  self->bit_depth = 0;
  self->align = GST_AV1_PARSE_ALIGN_NONE;
  self->in_align = GST_AV1_PARSE_ALIGN_NONE;
  self->discont = TRUE;
  self->header = FALSE;
  self->keyframe = FALSE;
  self->last_parsed_offset = 0;
  self->highest_spatial_id = 0;
  g_clear_pointer (&self->colorimetry, g_free);
  g_clear_pointer (&self->parser, gst_av1_parser_free);
  gst_adapter_clear (self->cache_out);
  gst_adapter_clear (self->frame_cache);
}

static void
gst_av1_parse_init (GstAV1Parse * self)
{
  gst_base_parse_set_pts_interpolation (GST_BASE_PARSE (self), FALSE);
  gst_base_parse_set_infer_ts (GST_BASE_PARSE (self), FALSE);

  GST_PAD_SET_ACCEPT_INTERSECT (GST_BASE_PARSE_SINK_PAD (self));
  GST_PAD_SET_ACCEPT_TEMPLATE (GST_BASE_PARSE_SINK_PAD (self));

  self->cache_out = gst_adapter_new ();
  self->frame_cache = gst_adapter_new ();
}

static void
gst_av1_parse_finalize (GObject * object)
{
  GstAV1Parse *self = GST_AV1_PARSE (object);

  gst_av1_parse_reset (self);
  g_object_unref (self->cache_out);
  g_object_unref (self->frame_cache);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_av1_parse_class_init (GstAV1ParseClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseParseClass *parse_class = GST_BASE_PARSE_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->finalize = gst_av1_parse_finalize;
  parse_class->start = GST_DEBUG_FUNCPTR (gst_av1_parse_start);
  parse_class->stop = GST_DEBUG_FUNCPTR (gst_av1_parse_stop);
  parse_class->handle_frame = GST_DEBUG_FUNCPTR (gst_av1_parse_handle_frame);
  parse_class->set_sink_caps = GST_DEBUG_FUNCPTR (gst_av1_parse_set_sink_caps);
  parse_class->get_sink_caps = GST_DEBUG_FUNCPTR (gst_av1_parse_get_sink_caps);

  gst_element_class_add_static_pad_template (element_class, &srctemplate);
  gst_element_class_add_static_pad_template (element_class, &sinktemplate);

  gst_element_class_set_static_metadata (element_class, "AV1 parser",
      "Codec/Parser/Converter/Video",
      "Parses AV1 streams", "He Junyan <junyan.he@intel.com>");

  GST_DEBUG_CATEGORY_INIT (av1_parse_debug, "av1parse", 0, "av1 parser");
}

static gboolean
gst_av1_parse_start (GstBaseParse * parse)
{
  GstAV1Parse *self = GST_AV1_PARSE (parse);

  GST_DEBUG_OBJECT (self, "start");

  gst_av1_parse_reset (self);
  self->parser = gst_av1_parser_new ();

  /* At least the OBU header. */
  gst_base_parse_set_min_frame_size (parse, 1);

  return TRUE;
}

static gboolean
gst_av1_parse_stop (GstBaseParse * parse)
{
  GstAV1Parse *self = GST_AV1_PARSE (parse);

  GST_DEBUG_OBJECT (self, "stop");
  g_clear_pointer (&self->parser, gst_av1_parser_free);

  return TRUE;
}

static const gchar *
gst_av1_parse_profile_to_string (GstAV1Profile profile)
{
  switch (profile) {
    case GST_AV1_PROFILE_0:
      return "0";
    case GST_AV1_PROFILE_1:
      return "1";
    case GST_AV1_PROFILE_2:
      return "2";
    default:
      break;
  }

  return NULL;
}

static GstAV1Profile
gst_av1_parse_profile_from_string (const gchar * profile)
{
  if (!profile)
    return GST_AV1_PROFILE_UNDEFINED;

  if (g_strcmp0 (profile, "0") == 0)
    return GST_AV1_PROFILE_0;
  else if (g_strcmp0 (profile, "1") == 0)
    return GST_AV1_PROFILE_1;
  else if (g_strcmp0 (profile, "2") == 0)
    return GST_AV1_PROFILE_2;

  return GST_AV1_PROFILE_UNDEFINED;
}

static const gchar *
gst_av1_parse_alignment_to_steam_format_string (GstAV1ParseAligment align)
{
  switch (align) {
    case GST_AV1_PARSE_ALIGN_BYTE:
      return "obu-stream";
    case GST_AV1_PARSE_ALIGN_OBU:
    case GST_AV1_PARSE_ALIGN_TEMPORAL_UNIT:
    case GST_AV1_PARSE_ALIGN_FRAME:
      return "obu-stream";
    case GST_AV1_PARSE_ALIGN_TEMPORAL_UNIT_ANNEX_B:
      return "annexb";
    default:
      GST_WARNING ("Unrecognized steam format");
      break;
  }

  return NULL;
}

static const gchar *
gst_av1_parse_alignment_to_string (GstAV1ParseAligment align)
{
  switch (align) {
    case GST_AV1_PARSE_ALIGN_BYTE:
      return "byte";
    case GST_AV1_PARSE_ALIGN_OBU:
      return "obu";
    case GST_AV1_PARSE_ALIGN_TEMPORAL_UNIT:
    case GST_AV1_PARSE_ALIGN_TEMPORAL_UNIT_ANNEX_B:
      return "tu";
    case GST_AV1_PARSE_ALIGN_FRAME:
      return "frame";
    default:
      GST_WARNING ("Unrecognized alignment");
      break;
  }

  return NULL;
}

static GstAV1ParseAligment
gst_av1_parse_alignment_from_string (const gchar * align,
    const gchar * stream_format)
{
  if (!align && !stream_format)
    return GST_AV1_PARSE_ALIGN_NONE;

  if (stream_format) {
    if (g_strcmp0 (stream_format, "annexb") == 0) {
      if (align && g_strcmp0 (align, "tu") != 0) {
        /* annex b stream must align to TU. */
        return GST_AV1_PARSE_ALIGN_ERROR;
      } else {
        return GST_AV1_PARSE_ALIGN_TEMPORAL_UNIT_ANNEX_B;
      }
    } else if (g_strcmp0 (stream_format, "obu-stream") != 0) {
      /* unrecognized */
      return GST_AV1_PARSE_ALIGN_NONE;
    }

    /* stream-format is obu-stream, depends on align */
  }

  if (align) {
    if (g_strcmp0 (align, "byte") == 0) {
      return GST_AV1_PARSE_ALIGN_BYTE;
    } else if (g_strcmp0 (align, "obu") == 0) {
      return GST_AV1_PARSE_ALIGN_OBU;
    } else if (g_strcmp0 (align, "tu") == 0) {
      return GST_AV1_PARSE_ALIGN_TEMPORAL_UNIT;
    } else if (g_strcmp0 (align, "frame") == 0) {
      return GST_AV1_PARSE_ALIGN_FRAME;
    } else {
      /* unrecognized */
      return GST_AV1_PARSE_ALIGN_NONE;
    }
  }

  return GST_AV1_PARSE_ALIGN_NONE;
}

static GstAV1ParseAligment
gst_av1_parse_alignment_from_caps (GstCaps * caps)
{
  GstAV1ParseAligment align;

  align = GST_AV1_PARSE_ALIGN_NONE;

  GST_DEBUG ("parsing caps: %" GST_PTR_FORMAT, caps);

  if (caps && gst_caps_get_size (caps) > 0) {
    GstStructure *s = gst_caps_get_structure (caps, 0);
    const gchar *str_align = NULL;
    const gchar *str_stream = NULL;

    str_align = gst_structure_get_string (s, "alignment");
    str_stream = gst_structure_get_string (s, "stream-format");

    if (str_align || str_stream)
      align = gst_av1_parse_alignment_from_string (str_align, str_stream);
  }

  return align;
}

static void
gst_av1_parse_update_src_caps (GstAV1Parse * self, GstCaps * caps)
{
  GstCaps *sink_caps, *src_caps;
  GstCaps *final_caps = NULL;
  GstStructure *s = NULL;
  gint width, height;
  gint par_n = 0, par_d = 0;
  gint fps_n = 0, fps_d = 0;
  const gchar *profile = NULL;

  if (G_UNLIKELY (!gst_pad_has_current_caps (GST_BASE_PARSE_SRC_PAD (self))))
    self->update_caps = TRUE;

  if (!self->update_caps)
    return;

  /* if this is being called from the first _setcaps call, caps on the sinkpad
   * aren't set yet and so they need to be passed as an argument */
  if (caps)
    sink_caps = gst_caps_ref (caps);
  else
    sink_caps = gst_pad_get_current_caps (GST_BASE_PARSE_SINK_PAD (self));

  /* carry over input caps as much as possible; override with our own stuff */
  if (!sink_caps)
    sink_caps = gst_caps_new_empty_simple ("video/x-av1");
  else
    s = gst_caps_get_structure (sink_caps, 0);

  final_caps = gst_caps_copy (sink_caps);

  if (s && gst_structure_has_field (s, "width") &&
      gst_structure_has_field (s, "height")) {
    gst_structure_get_int (s, "width", &width);
    gst_structure_get_int (s, "height", &height);
  } else {
    width = self->width;
    height = self->height;
  }

  if (width > 0 && height > 0)
    gst_caps_set_simple (final_caps, "width", G_TYPE_INT, width,
        "height", G_TYPE_INT, height, NULL);

  if (s && gst_structure_get_fraction (s, "pixel-aspect-ratio", &par_n, &par_d)) {
    if (par_n != 0 && par_d != 0) {
      gst_caps_set_simple (final_caps, "pixel-aspect-ratio",
          GST_TYPE_FRACTION, par_n, par_d, NULL);
    }
  }

  if (s && gst_structure_has_field (s, "framerate")) {
    gst_structure_get_fraction (s, "framerate", &fps_n, &fps_d);
  }

  if (fps_n > 0 && fps_d > 0) {
    gst_caps_set_simple (final_caps, "framerate",
        GST_TYPE_FRACTION, fps_n, fps_d, NULL);
    gst_base_parse_set_frame_rate (GST_BASE_PARSE (self), fps_n, fps_d, 0, 0);
  }

  /* When not RGB, the chroma format is needed. */
  if (self->colorimetry == NULL ||
      (g_strcmp0 (self->colorimetry, GST_VIDEO_COLORIMETRY_SRGB) != 0)) {
    const gchar *chroma_format = NULL;

    if (self->subsampling_x == 1 && self->subsampling_y == 1) {
      if (!self->mono_chrome) {
        chroma_format = "4:2:0";
      } else {
        chroma_format = "4:0:0";
      }
    } else if (self->subsampling_x == 1 && self->subsampling_y == 0) {
      chroma_format = "4:2:2";
    } else if (self->subsampling_x == 0 && self->subsampling_y == 0) {
      chroma_format = "4:4:4";
    }

    if (chroma_format)
      gst_caps_set_simple (final_caps,
          "chroma-format", G_TYPE_STRING, chroma_format, NULL);
  }

  if (self->bit_depth)
    gst_caps_set_simple (final_caps,
        "bit-depth-luma", G_TYPE_UINT, self->bit_depth,
        "bit-depth-chroma", G_TYPE_UINT, self->bit_depth, NULL);

  if (self->colorimetry && (!s || !gst_structure_has_field (s, "colorimetry")))
    gst_caps_set_simple (final_caps,
        "colorimetry", G_TYPE_STRING, self->colorimetry, NULL);

  g_assert (self->align > GST_AV1_PARSE_ALIGN_NONE);
  gst_caps_set_simple (final_caps, "parsed", G_TYPE_BOOLEAN, TRUE,
      "stream-format", G_TYPE_STRING,
      gst_av1_parse_alignment_to_steam_format_string (self->align),
      "alignment", G_TYPE_STRING,
      gst_av1_parse_alignment_to_string (self->align), NULL);

  profile = gst_av1_parse_profile_to_string (self->profile);
  if (profile)
    gst_caps_set_simple (final_caps, "profile", G_TYPE_STRING, profile, NULL);

  src_caps = gst_pad_get_current_caps (GST_BASE_PARSE_SRC_PAD (self));

  if (!(src_caps && gst_caps_is_strictly_equal (src_caps, final_caps))) {
    GST_DEBUG_OBJECT (self, "Update src caps %" GST_PTR_FORMAT, final_caps);
    gst_pad_set_caps (GST_BASE_PARSE_SRC_PAD (self), final_caps);
  }

  gst_clear_caps (&src_caps);
  gst_caps_unref (final_caps);
  gst_caps_unref (sink_caps);

  self->update_caps = FALSE;
}

/* check downstream caps to configure format and alignment */
static void
gst_av1_parse_negotiate (GstAV1Parse * self, GstCaps * in_caps)
{
  GstCaps *caps;
  GstAV1ParseAligment align = self->align;

  caps = gst_pad_get_allowed_caps (GST_BASE_PARSE_SRC_PAD (self));
  GST_DEBUG_OBJECT (self, "allowed caps: %" GST_PTR_FORMAT, caps);

  /* concentrate on leading structure, since decodebin parser
   * capsfilter always includes parser template caps */
  if (caps) {
    caps = gst_caps_truncate (caps);
    GST_DEBUG_OBJECT (self, "negotiating with caps: %" GST_PTR_FORMAT, caps);
  }

  /* Both upsteam and downstream support, best */
  if (in_caps && caps) {
    if (gst_caps_can_intersect (in_caps, caps)) {
      GST_DEBUG_OBJECT (self, "downstream accepts upstream caps");
      align = gst_av1_parse_alignment_from_caps (in_caps);
      gst_clear_caps (&caps);
    }
  }
  if (align != GST_AV1_PARSE_ALIGN_NONE)
    goto done;

  /* Select first one of downstream support */
  if (caps && !gst_caps_is_empty (caps)) {
    /* fixate to avoid ambiguity with lists when parsing */
    caps = gst_caps_fixate (caps);
    align = gst_av1_parse_alignment_from_caps (caps);
  }
  if (align != GST_AV1_PARSE_ALIGN_NONE)
    goto done;

  /* default */
  if (align == GST_AV1_PARSE_ALIGN_NONE)
    align = GST_AV1_PARSE_ALIGN_FRAME;

done:
  self->align = align;
  GST_INFO_OBJECT (self, "selected alignment %s",
      gst_av1_parse_alignment_to_string (align));

  gst_clear_caps (&caps);
}

static GstCaps *
gst_av1_parse_get_sink_caps (GstBaseParse * parse, GstCaps * filter)
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
  } else {
    peercaps = gst_pad_peer_query_caps (GST_BASE_PARSE_SRC_PAD (parse), NULL);
  }

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

static gboolean
gst_av1_parse_set_sink_caps (GstBaseParse * parse, GstCaps * caps)
{
  GstAV1Parse *self = GST_AV1_PARSE (parse);
  GstStructure *str;
  GstAV1ParseAligment align;
  GstCaps *in_caps = NULL;
  const gchar *profile;

  str = gst_caps_get_structure (caps, 0);

  /* accept upstream info if provided */
  gst_structure_get_int (str, "width", &self->width);
  gst_structure_get_int (str, "height", &self->height);
  profile = gst_structure_get_string (str, "profile");
  if (profile)
    self->profile = gst_av1_parse_profile_from_string (profile);

  /* get upstream align from caps */
  align = gst_av1_parse_alignment_from_caps (caps);
  if (align == GST_AV1_PARSE_ALIGN_ERROR) {
    GST_ERROR_OBJECT (self, "Sink caps %" GST_PTR_FORMAT " set stream-format"
        " and alignment conflict.", caps);
    return FALSE;
  }

  in_caps = gst_caps_copy (caps);
  /* default */
  if (align == GST_AV1_PARSE_ALIGN_NONE)
    gst_caps_set_simple (in_caps, "alignment", G_TYPE_STRING,
        gst_av1_parse_alignment_to_string (GST_AV1_PARSE_ALIGN_BYTE), NULL);

  /* negotiate with downstream, set output align */
  gst_av1_parse_negotiate (self, in_caps);

  self->update_caps = TRUE;

  /* if all of decoder's capability related values are provided
   * by upstream, update src caps now */
  if (self->width > 0 && self->height > 0 && profile)
    gst_av1_parse_update_src_caps (self, in_caps);

  gst_caps_unref (in_caps);

  self->in_align = align;

  if (self->in_align == GST_AV1_PARSE_ALIGN_TEMPORAL_UNIT_ANNEX_B) {
    gst_av1_parser_reset (self->parser, TRUE);
  } else {
    gst_av1_parser_reset (self->parser, FALSE);
  }

  return TRUE;
}

static GstFlowReturn
gst_av1_parse_push_data (GstAV1Parse * self, GstBaseParseFrame * frame,
    guint32 finish_sz, gboolean frame_finished)
{
  gsize sz;
  GstBuffer *buf;
  GstBuffer *buffer = frame->buffer;
  GstFlowReturn ret = GST_FLOW_OK;

  /* Need to generate the final TU annex-b format */
  if (self->align == GST_AV1_PARSE_ALIGN_TEMPORAL_UNIT_ANNEX_B) {
    guint8 size_data[GST_AV1_MAX_LEB_128_SIZE];
    guint size_len = 0;
    guint len;

    /* When push a TU, it must also be a frame end. */
    g_assert (frame_finished);

    /* Still some left in the frame cache */
    len = gst_adapter_available (self->frame_cache);
    if (len) {
      buf = gst_adapter_take_buffer (self->frame_cache, len);

      /* frame_unit_size */
      _write_leb128 (size_data, &size_len, len);

      gst_adapter_push (self->cache_out,
          gst_buffer_new_memdup (size_data, size_len));
      gst_adapter_push (self->cache_out, buf);
    }

    len = gst_adapter_available (self->cache_out);
    if (len) {
      buf = gst_adapter_take_buffer (self->cache_out, len);

      /* temporal_unit_size */
      _write_leb128 (size_data, &size_len, len);

      gst_adapter_push (self->cache_out,
          gst_buffer_new_memdup (size_data, size_len));
      gst_adapter_push (self->cache_out, buf);
    }
  }

  sz = gst_adapter_available (self->cache_out);
  if (sz) {
    buf = gst_adapter_take_buffer (self->cache_out, sz);
    gst_buffer_copy_into (buf, buffer, GST_BUFFER_COPY_METADATA, 0, -1);
    if (self->discont) {
      GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
      self->discont = FALSE;
    }
    if (self->header) {
      GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_HEADER);
      self->header = FALSE;
    }
    if (self->keyframe) {
      GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
      self->keyframe = FALSE;
    }

    if (frame_finished)
      GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_MARKER);

    gst_buffer_replace (&frame->out_buffer, buf);
    gst_buffer_unref (buf);

    gst_av1_parse_update_src_caps (self, NULL);
    GST_LOG_OBJECT (self, "comsumed %d, output one buffer with size %"
        G_GSSIZE_FORMAT, finish_sz, sz);
    ret = gst_base_parse_finish_frame (GST_BASE_PARSE (self), frame, finish_sz);
  }

  return ret;
}

static void
gst_av1_parse_convert_to_annexb (GstAV1Parse * self, GstAV1OBU * obu,
    gboolean frame_complete)
{
  guint8 size_data[GST_AV1_MAX_LEB_128_SIZE];
  guint size_len = 0;
  GstBitWriter bs;
  GstBuffer *buf, *buf2;
  guint8 *data;
  guint len, len2, offset;

  /* obu_length */
  _write_leb128 (size_data, &size_len,
      obu->obu_size + 1 + obu->header.obu_extention_flag);

  gst_bit_writer_init_with_size (&bs, 128, FALSE);
  /* obu_forbidden_bit */
  gst_bit_writer_put_bits_uint8 (&bs, 0, 1);
  /* obu_type */
  gst_bit_writer_put_bits_uint8 (&bs, obu->obu_type, 4);
  /* obu_extension_flag */
  gst_bit_writer_put_bits_uint8 (&bs, obu->header.obu_extention_flag, 1);
  /* obu_has_size_field */
  gst_bit_writer_put_bits_uint8 (&bs, 0, 1);
  /* obu_reserved_1bit */
  gst_bit_writer_put_bits_uint8 (&bs, 0, 1);
  if (obu->header.obu_extention_flag) {
    /* temporal_id */
    gst_bit_writer_put_bits_uint8 (&bs, obu->header.obu_temporal_id, 3);
    /* spatial_id */
    gst_bit_writer_put_bits_uint8 (&bs, obu->header.obu_spatial_id, 2);
    /* extension_header_reserved_3bits */
    gst_bit_writer_put_bits_uint8 (&bs, 0, 3);
  }
  g_assert (GST_BIT_WRITER_BIT_SIZE (&bs) % 8 == 0);

  len = size_len;
  len += GST_BIT_WRITER_BIT_SIZE (&bs) / 8;
  len += obu->obu_size;

  data = g_malloc (len);
  offset = 0;

  memcpy (data + offset, size_data, size_len);
  offset += size_len;

  memcpy (data + offset, GST_BIT_WRITER_DATA (&bs),
      GST_BIT_WRITER_BIT_SIZE (&bs) / 8);
  offset += GST_BIT_WRITER_BIT_SIZE (&bs) / 8;

  memcpy (data + offset, obu->data, obu->obu_size);

  /* The buf of this OBU */
  buf = gst_buffer_new_wrapped (data, len);

  gst_adapter_push (self->frame_cache, buf);

  if (frame_complete) {
    len2 = gst_adapter_available (self->frame_cache);
    buf2 = gst_adapter_take_buffer (self->frame_cache, len2);

    /* frame_unit_size */
    _write_leb128 (size_data, &size_len, len2);
    gst_adapter_push (self->cache_out,
        gst_buffer_new_memdup (size_data, size_len));

    gst_adapter_push (self->cache_out, buf2);
  }

  gst_bit_writer_reset (&bs);
}

static void
gst_av1_parse_convert_from_annexb (GstAV1Parse * self, GstAV1OBU * obu)
{
  guint8 size_data[GST_AV1_MAX_LEB_128_SIZE];
  guint size_len = 0;
  GstBuffer *buf;
  guint len, offset;
  guint8 *data;
  GstBitWriter bs;

  _write_leb128 (size_data, &size_len, obu->obu_size);

  /* obu_header */
  len = 1;
  if (obu->header.obu_extention_flag)
    len += 1;
  len += size_len;
  len += obu->obu_size;

  gst_bit_writer_init_with_size (&bs, 128, FALSE);
  /* obu_forbidden_bit */
  gst_bit_writer_put_bits_uint8 (&bs, 0, 1);
  /* obu_type */
  gst_bit_writer_put_bits_uint8 (&bs, obu->obu_type, 4);
  /* obu_extension_flag */
  gst_bit_writer_put_bits_uint8 (&bs, obu->header.obu_extention_flag, 1);
  /* obu_has_size_field */
  gst_bit_writer_put_bits_uint8 (&bs, 1, 1);
  /* obu_reserved_1bit */
  gst_bit_writer_put_bits_uint8 (&bs, 0, 1);
  if (obu->header.obu_extention_flag) {
    /* temporal_id */
    gst_bit_writer_put_bits_uint8 (&bs, obu->header.obu_temporal_id, 3);
    /* spatial_id */
    gst_bit_writer_put_bits_uint8 (&bs, obu->header.obu_spatial_id, 2);
    /* extension_header_reserved_3bits */
    gst_bit_writer_put_bits_uint8 (&bs, 0, 3);
  }
  g_assert (GST_BIT_WRITER_BIT_SIZE (&bs) % 8 == 0);

  data = g_malloc (len);
  offset = 0;
  memcpy (data + offset, GST_BIT_WRITER_DATA (&bs),
      GST_BIT_WRITER_BIT_SIZE (&bs) / 8);
  offset += GST_BIT_WRITER_BIT_SIZE (&bs) / 8;

  memcpy (data + offset, size_data, size_len);
  offset += size_len;

  memcpy (data + offset, obu->data, obu->obu_size);

  buf = gst_buffer_new_wrapped (data, len);
  gst_adapter_push (self->cache_out, buf);

  gst_bit_writer_reset (&bs);
}

static void
gst_av1_parse_cache_one_obu (GstAV1Parse * self, GstAV1OBU * obu,
    guint8 * data, guint32 size, gboolean frame_complete)
{
  gboolean need_convert = FALSE;
  GstBuffer *buf;

  if (self->in_align != self->align
      && (self->in_align == GST_AV1_PARSE_ALIGN_TEMPORAL_UNIT_ANNEX_B
          || self->align == GST_AV1_PARSE_ALIGN_TEMPORAL_UNIT_ANNEX_B))
    need_convert = TRUE;

  if (need_convert) {
    if (self->in_align == GST_AV1_PARSE_ALIGN_TEMPORAL_UNIT_ANNEX_B) {
      gst_av1_parse_convert_from_annexb (self, obu);
    } else {
      gst_av1_parse_convert_to_annexb (self, obu, frame_complete);
    }
  } else if (self->align == GST_AV1_PARSE_ALIGN_TEMPORAL_UNIT_ANNEX_B) {
    g_assert (self->in_align == GST_AV1_PARSE_ALIGN_TEMPORAL_UNIT_ANNEX_B);
    gst_av1_parse_convert_to_annexb (self, obu, frame_complete);
  } else {
    buf = gst_buffer_new_memdup (data, size);
    gst_adapter_push (self->cache_out, buf);
  }
}

static GstAV1ParserResult
gst_av1_parse_handle_sequence_obu (GstAV1Parse * self, GstAV1OBU * obu)
{
  GstAV1SequenceHeaderOBU seq_header;
  GstAV1ParserResult res;
  guint i;
  guint val;

  res = gst_av1_parser_parse_sequence_header_obu (self->parser,
      obu, &seq_header);
  if (res != GST_AV1_PARSER_OK)
    return res;

  if (self->width != seq_header.max_frame_width_minus_1 + 1) {
    self->width = seq_header.max_frame_width_minus_1 + 1;
    self->update_caps = TRUE;
  }
  if (self->height != seq_header.max_frame_height_minus_1 + 1) {
    self->height = seq_header.max_frame_height_minus_1 + 1;
    self->update_caps = TRUE;
  }

  if (seq_header.color_config.color_description_present_flag) {
    GstVideoColorimetry cinfo;
    gboolean have_cinfo = TRUE;
    gchar *colorimetry = NULL;

    if (have_cinfo) {
      if (seq_header.color_config.color_range)
        cinfo.range = GST_VIDEO_COLOR_RANGE_16_235;
      else
        cinfo.range = GST_VIDEO_COLOR_RANGE_0_255;

      cinfo.matrix = gst_video_color_matrix_from_iso
          (seq_header.color_config.matrix_coefficients);
      cinfo.transfer = gst_video_transfer_function_from_iso
          (seq_header.color_config.transfer_characteristics);
      cinfo.primaries = gst_video_color_primaries_from_iso
          (seq_header.color_config.color_primaries);
      colorimetry = gst_video_colorimetry_to_string (&cinfo);
    }

    if (g_strcmp0 (colorimetry, self->colorimetry)) {
      g_clear_pointer (&self->colorimetry, g_free);
      self->colorimetry = colorimetry;
      self->update_caps = TRUE;
    }
  }

  if (self->subsampling_x != seq_header.color_config.subsampling_x) {
    self->subsampling_x = seq_header.color_config.subsampling_x;
    self->update_caps = TRUE;
  }

  if (self->subsampling_y != seq_header.color_config.subsampling_y) {
    self->subsampling_y = seq_header.color_config.subsampling_y;
    self->update_caps = TRUE;
  }

  if (self->mono_chrome != seq_header.color_config.mono_chrome) {
    self->mono_chrome = seq_header.color_config.mono_chrome;
    self->update_caps = TRUE;
  }

  if (self->bit_depth != seq_header.bit_depth) {
    self->bit_depth = seq_header.bit_depth;
    self->update_caps = TRUE;
  }

  if (self->profile != seq_header.seq_profile) {
    self->profile = seq_header.seq_profile;
    self->update_caps = TRUE;
  }

  val = (self->parser->state.operating_point_idc >> 8) & 0x0f;
  for (i = 0; i < (1 << GST_AV1_MAX_SPATIAL_LAYERS); i++) {
    if (val & (1 << i))
      self->highest_spatial_id = i;
  }

  return GST_AV1_PARSER_OK;
}

static GstAV1ParserResult
gst_av1_parse_handle_one_obu (GstAV1Parse * self, GstAV1OBU * obu,
    gboolean * frame_complete)
{
  GstAV1ParserResult res = GST_AV1_PARSER_OK;
  GstAV1MetadataOBU metadata;
  GstAV1FrameHeaderOBU frame_header;
  GstAV1TileListOBU tile_list;
  GstAV1TileGroupOBU tile_group;
  GstAV1FrameOBU frame;

  *frame_complete = FALSE;

  switch (obu->obu_type) {
    case GST_AV1_OBU_TEMPORAL_DELIMITER:
      res = gst_av1_parser_parse_temporal_delimiter_obu (self->parser, obu);
      break;
    case GST_AV1_OBU_SEQUENCE_HEADER:
      res = gst_av1_parse_handle_sequence_obu (self, obu);
      break;
    case GST_AV1_OBU_REDUNDANT_FRAME_HEADER:
      res = gst_av1_parser_parse_frame_header_obu (self->parser, obu,
          &frame_header);
      break;
    case GST_AV1_OBU_FRAME_HEADER:
      res = gst_av1_parser_parse_frame_header_obu (self->parser, obu,
          &frame_header);
      break;
    case GST_AV1_OBU_FRAME:
      res = gst_av1_parser_parse_frame_obu (self->parser, obu, &frame);
      break;
    case GST_AV1_OBU_METADATA:
      res = gst_av1_parser_parse_metadata_obu (self->parser, obu, &metadata);
      break;
    case GST_AV1_OBU_TILE_GROUP:
      res =
          gst_av1_parser_parse_tile_group_obu (self->parser, obu, &tile_group);
      break;
    case GST_AV1_OBU_TILE_LIST:
      res = gst_av1_parser_parse_tile_list_obu (self->parser, obu, &tile_list);
      break;
    case GST_AV1_OBU_PADDING:
      break;
    default:
      GST_WARNING_OBJECT (self, "an unrecognized obu type %d", obu->obu_type);
      res = GST_AV1_PARSER_BITSTREAM_ERROR;
      break;
  }

  GST_LOG_OBJECT (self, "parsing the obu %s, result is %d",
      _obu_name (obu->obu_type), res);
  if (res != GST_AV1_PARSER_OK)
    goto out;

  /* 7.5:
     All OBU extension headers that are contained in the same temporal
     unit and have the same spatial_id value must have the same temporal_id
     value.
     And
     OBUs with spatial level IDs (spatial_id) greater than 0 must
     appear within a temporal unit in increasing order of the spatial
     level ID values. */
  if (obu->header.obu_spatial_id > self->highest_spatial_id) {
    GST_WARNING_OBJECT (self,
        "spatial_id %d is bigger than highest_spatial_id %d",
        obu->header.obu_spatial_id, self->highest_spatial_id);
    res = GST_AV1_PARSER_BITSTREAM_ERROR;
    goto out;
  }

  if (obu->obu_type == GST_AV1_OBU_SEQUENCE_HEADER)
    self->header = TRUE;

  if (obu->obu_type == GST_AV1_OBU_FRAME_HEADER
      || obu->obu_type == GST_AV1_OBU_FRAME
      || obu->obu_type == GST_AV1_OBU_REDUNDANT_FRAME_HEADER) {
    GstAV1FrameHeaderOBU *fh = &frame_header;

    if (obu->obu_type == GST_AV1_OBU_FRAME)
      fh = &frame.frame_header;

    /* if a show_existing_frame case, only update key frame.
       otherwise, update all type of frame.  */
    if (!fh->show_existing_frame || fh->frame_type == GST_AV1_KEY_FRAME)
      res = gst_av1_parser_reference_frame_update (self->parser, fh);

    if (res != GST_AV1_PARSER_OK)
      GST_WARNING_OBJECT (self, "update frame get result %d", res);

    if (fh->show_existing_frame)
      *frame_complete = TRUE;

    if (fh->frame_type == GST_AV1_KEY_FRAME)
      self->keyframe = TRUE;
  }

  if (obu->obu_type == GST_AV1_OBU_TILE_GROUP
      || obu->obu_type == GST_AV1_OBU_FRAME) {
    GstAV1TileGroupOBU *tg = &tile_group;

    if (obu->obu_type == GST_AV1_OBU_FRAME)
      tg = &frame.tile_group;

    if (tg->tg_end == tg->num_tiles - 1)
      *frame_complete = TRUE;
  }

out:
  if (res != GST_AV1_PARSER_OK) {
    /* Some verbose OBU can be skip */
    if (obu->obu_type == GST_AV1_OBU_REDUNDANT_FRAME_HEADER) {
      GST_WARNING_OBJECT (self, "Ignore a verbose %s OBU parsing error",
          _obu_name (obu->obu_type));
      res = GST_AV1_PARSER_OK;
    }
  }

  return res;
}

static GstFlowReturn
gst_av1_parse_handle_obu_to_obu (GstBaseParse * parse,
    GstBaseParseFrame * frame, gint * skipsize)
{
  GstAV1Parse *self = GST_AV1_PARSE (parse);
  GstMapInfo map_info;
  GstAV1OBU obu;
  GstFlowReturn ret = GST_FLOW_OK;
  GstAV1ParserResult res;
  GstBuffer *buffer = gst_buffer_ref (frame->buffer);
  guint32 consumed;
  gboolean frame_complete;

  if (!gst_buffer_map (buffer, &map_info, GST_MAP_READ)) {
    *skipsize = 0;
    GST_ERROR_OBJECT (parse, "Couldn't map incoming buffer");
    return GST_FLOW_ERROR;
  }

  consumed = 0;
  frame_complete = FALSE;
  res = gst_av1_parser_identify_one_obu (self->parser, map_info.data,
      map_info.size, &obu, &consumed);
  if (res == GST_AV1_PARSER_OK)
    res = gst_av1_parse_handle_one_obu (self, &obu, &frame_complete);

  g_assert (consumed <= map_info.size);

  if (res == GST_AV1_PARSER_BITSTREAM_ERROR) {
    if (consumed) {
      *skipsize = consumed;
    } else {
      *skipsize = map_info.size;
    }
    GST_WARNING_OBJECT (parse, "Parse obu error, discard %d.", *skipsize);
    ret = GST_FLOW_OK;
    goto out;
  } else if (res == GST_AV1_PARSER_NO_MORE_DATA) {
    *skipsize = 0;

    if (self->in_align == GST_AV1_PARSE_ALIGN_OBU) {
      /* The buffer is already aligned to OBU, should not happen. */
      if (consumed) {
        *skipsize = consumed;
      } else {
        *skipsize = map_info.size;
      }
      GST_WARNING_OBJECT (parse, "Parse obu need more data, discard %d.",
          *skipsize);
    }
    ret = GST_FLOW_OK;
    goto out;
  } else if (res == GST_AV1_PARSER_DROP) {
    GST_DEBUG_OBJECT (parse, "Drop %d data", consumed);
    *skipsize = consumed;
    ret = GST_FLOW_OK;
    goto out;
  } else if (res != GST_AV1_PARSER_OK) {
    GST_ERROR_OBJECT (parse, "Parse obu get unexpect error %d", res);
    *skipsize = 0;
    ret = GST_FLOW_ERROR;
    goto out;
  }

  g_assert (consumed);

  gst_av1_parse_update_src_caps (self, NULL);
  if (self->discont) {
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
    self->discont = FALSE;
  }
  if (self->header) {
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_HEADER);
    self->header = FALSE;
  }
  /* happen to be a frame boundary */
  if (frame_complete)
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_MARKER);

  GST_LOG_OBJECT (self, "Output one buffer with size %d", consumed);
  ret = gst_base_parse_finish_frame (parse, frame, consumed);
  *skipsize = 0;

out:
  gst_buffer_unmap (buffer, &map_info);
  gst_buffer_unref (buffer);
  return ret;
}

static GstFlowReturn
gst_av1_parse_handle_to_small_and_equal_align (GstBaseParse * parse,
    GstBaseParseFrame * frame, gint * skipsize)
{
  GstAV1Parse *self = GST_AV1_PARSE (parse);
  GstMapInfo map_info;
  GstAV1OBU obu;
  GstFlowReturn ret = GST_FLOW_OK;
  GstAV1ParserResult res;
  GstBuffer *buffer = gst_buffer_ref (frame->buffer);
  guint32 total_consumed, consumed;
  gboolean frame_complete;

  if (!gst_buffer_map (buffer, &map_info, GST_MAP_READ)) {
    GST_ERROR_OBJECT (parse, "Couldn't map incoming buffer");
    return GST_FLOW_ERROR;
  }

  total_consumed = 0;
  frame_complete = FALSE;
again:
  while (total_consumed < map_info.size) {
    res = gst_av1_parser_identify_one_obu (self->parser,
        map_info.data + total_consumed, map_info.size - total_consumed,
        &obu, &consumed);
    if (res == GST_AV1_PARSER_OK)
      res = gst_av1_parse_handle_one_obu (self, &obu, &frame_complete);
    if (res != GST_AV1_PARSER_OK)
      break;

    if (obu.obu_type == GST_AV1_OBU_TEMPORAL_DELIMITER && total_consumed) {
      GST_DEBUG_OBJECT (self, "Encounter TD inside one %s aligned"
          " buffer, should not happen normally.",
          gst_av1_parse_alignment_to_string (self->in_align));
      frame_complete = TRUE;
      if (self->in_align == GST_AV1_PARSE_ALIGN_TEMPORAL_UNIT_ANNEX_B)
        gst_av1_parser_reset_annex_b (self->parser);
      /* Not include this TD obu, it should belong to the next TU or frame */
      break;
    }

    gst_av1_parse_cache_one_obu (self, &obu,
        map_info.data + total_consumed, consumed, frame_complete);

    total_consumed += consumed;

    if (self->align == GST_AV1_PARSE_ALIGN_OBU)
      break;

    if (self->align == GST_AV1_PARSE_ALIGN_FRAME && frame_complete)
      break;
  }

  if (res == GST_AV1_PARSER_BITSTREAM_ERROR) {
    /* Discard the whole frame */
    *skipsize = map_info.size;
    GST_WARNING_OBJECT (parse, "Parse obu error, discard %d", *skipsize);
    if (self->in_align == GST_AV1_PARSE_ALIGN_TEMPORAL_UNIT_ANNEX_B)
      gst_av1_parser_reset_annex_b (self->parser);
    ret = GST_FLOW_OK;
    goto out;
  } else if (res == GST_AV1_PARSER_NO_MORE_DATA) {
    /* Discard the whole buffer */
    *skipsize = map_info.size;
    GST_WARNING_OBJECT (parse, "Parse obu need more data, discard %d.",
        *skipsize);
    if (self->in_align == GST_AV1_PARSE_ALIGN_TEMPORAL_UNIT_ANNEX_B)
      gst_av1_parser_reset_annex_b (self->parser);
    ret = GST_FLOW_OK;
    goto out;
  } else if (res == GST_AV1_PARSER_DROP) {
    GST_DEBUG_OBJECT (parse, "Drop %d data", consumed);
    total_consumed += consumed;
    res = GST_AV1_PARSER_OK;
    goto again;
  } else if (res != GST_AV1_PARSER_OK) {
    GST_ERROR_OBJECT (parse, "Parse obu get unexpect error %d", res);
    *skipsize = 0;
    ret = GST_FLOW_ERROR;
    goto out;
  }

  g_assert (total_consumed >= map_info.size || frame_complete
      || self->align == GST_AV1_PARSE_ALIGN_OBU);

  if (total_consumed >= map_info.size && !frame_complete
      && self->align == GST_AV1_PARSE_ALIGN_FRAME) {
    /* Warning and still consider this frame as complete */
    GST_WARNING_OBJECT (self, "Exhaust the buffer but still incomplete frame,"
        " should not happend in %s alignment",
        gst_av1_parse_alignment_to_string (self->in_align));
  }

  ret = gst_av1_parse_push_data (self, frame, total_consumed, frame_complete);

out:
  gst_buffer_unmap (buffer, &map_info);
  gst_buffer_unref (buffer);
  return ret;
}

static GstFlowReturn
gst_av1_parse_handle_to_big_align (GstBaseParse * parse,
    GstBaseParseFrame * frame, gint * skipsize)
{
  GstAV1Parse *self = GST_AV1_PARSE (parse);
  GstMapInfo map_info;
  GstAV1OBU obu;
  GstFlowReturn ret = GST_FLOW_OK;
  GstAV1ParserResult res = GST_AV1_PARSER_OK;
  GstBuffer *buffer = gst_buffer_ref (frame->buffer);
  guint32 consumed;
  gboolean frame_complete;
  gboolean complete;

  g_assert (self->in_align <= GST_AV1_PARSE_ALIGN_FRAME);

  if (!gst_buffer_map (buffer, &map_info, GST_MAP_READ)) {
    *skipsize = 0;
    GST_ERROR_OBJECT (parse, "Couldn't map incoming buffer");
    return GST_FLOW_ERROR;
  }

  complete = FALSE;
again:
  while (self->last_parsed_offset < map_info.size) {
    res = gst_av1_parser_identify_one_obu (self->parser,
        map_info.data + self->last_parsed_offset,
        map_info.size - self->last_parsed_offset, &obu, &consumed);
    if (res == GST_AV1_PARSER_OK)
      res = gst_av1_parse_handle_one_obu (self, &obu, &frame_complete);
    if (res != GST_AV1_PARSER_OK)
      break;

    /* New TD come, always begin a new temporal unit or frame */
    if (obu.obu_type == GST_AV1_OBU_TEMPORAL_DELIMITER
        && (gst_adapter_available (self->cache_out) ||
            gst_adapter_available (self->frame_cache))) {
      complete = TRUE;
      break;
    }

    if (self->align == GST_AV1_PARSE_ALIGN_TEMPORAL_UNIT ||
        self->align == GST_AV1_PARSE_ALIGN_FRAME) {
      GstBuffer *buf = gst_buffer_copy_region (buffer, GST_BUFFER_COPY_ALL,
          self->last_parsed_offset, consumed);
      gst_adapter_push (self->cache_out, buf);
    } else if (self->align == GST_AV1_PARSE_ALIGN_TEMPORAL_UNIT_ANNEX_B) {
      gst_av1_parse_convert_to_annexb (self, &obu, frame_complete);
    } else {
      g_assert_not_reached ();
    }
    self->last_parsed_offset += consumed;

    if (self->align == GST_AV1_PARSE_ALIGN_FRAME && frame_complete)
      complete = TRUE;

    if (complete)
      break;
  }

  /* Finish a complete frame anyway */
  if (complete || GST_BASE_PARSE_DRAINING (parse)) {
    *skipsize = 0;

    /* push the left anyway if no error */
    if (res == GST_AV1_PARSER_OK)
      ret = gst_av1_parse_push_data (self, frame,
          self->last_parsed_offset, TRUE);

    self->last_parsed_offset = 0;

    goto out;
  }

  if (res == GST_AV1_PARSER_BITSTREAM_ERROR) {
    *skipsize = map_info.size;
    GST_WARNING_OBJECT (parse, "Parse obu error, discard whole buffer %d.",
        *skipsize);
    /* The adapter will be cleared in next loop because of 
       GST_BASE_PARSE_FRAME_FLAG_NEW_FRAME flag */
    ret = GST_FLOW_OK;
  } else if (res == GST_AV1_PARSER_NO_MORE_DATA) {
    *skipsize = 0;

    if (self->in_align >= GST_AV1_PARSE_ALIGN_OBU) {
      /* The buffer is already aligned to OBU, should not happen.
         The adapter will be cleared in next loop because of
         GST_BASE_PARSE_FRAME_FLAG_NEW_FRAME flag */
      *skipsize = map_info.size;
      GST_WARNING_OBJECT (parse,
          "Parse obu need more data, discard whole buffer %d.", *skipsize);
    }
    ret = GST_FLOW_OK;
  } else if (res == GST_AV1_PARSER_DROP) {
    GST_DEBUG_OBJECT (parse, "Drop %d data", consumed);
    self->last_parsed_offset += consumed;
    res = GST_AV1_PARSER_OK;
    goto again;
  } else if (res == GST_AV1_PARSER_OK) {
    /* Everything is correct but still not get a frame or tu,
       need more data */
    GST_DEBUG_OBJECT (parse, "Need more data");
    *skipsize = 0;
    ret = GST_FLOW_OK;
  } else {
    GST_ERROR_OBJECT (parse, "Parse obu get unexpect error %d", res);
    *skipsize = 0;
    ret = GST_FLOW_ERROR;
  }

out:
  gst_buffer_unmap (buffer, &map_info);
  gst_buffer_unref (buffer);
  return ret;
}

/* Try to recognize whether the input is annex-b format. */
static GstFlowReturn
gst_av1_parse_detect_alignment (GstBaseParse * parse,
    GstBaseParseFrame * frame, gint * skipsize)
{
  GstAV1Parse *self = GST_AV1_PARSE (parse);
  GstMapInfo map_info;
  GstAV1OBU obu;
  GstAV1ParserResult res;
  GstBuffer *buffer = gst_buffer_ref (frame->buffer);
  gboolean got_seq, got_frame;
  gboolean frame_complete;
  guint32 consumed, total_consumed;
  guint32 frame_sz;
  GstFlowReturn ret = GST_FLOW_OK;

  if (!gst_buffer_map (buffer, &map_info, GST_MAP_READ)) {
    *skipsize = 0;
    GST_ERROR_OBJECT (parse, "Couldn't map incoming buffer");
    return GST_FLOW_ERROR;
  }

  gst_av1_parser_reset (self->parser, FALSE);

  /* Detect the alignment obu first */
  got_seq = FALSE;
  got_frame = FALSE;
  total_consumed = 0;
again:
  while (total_consumed < map_info.size) {
    res = gst_av1_parser_identify_one_obu (self->parser,
        map_info.data + total_consumed, map_info.size - total_consumed,
        &obu, &consumed);
    if (res == GST_AV1_PARSER_OK)
      res = gst_av1_parse_handle_one_obu (self, &obu, &frame_complete);
    if (res != GST_AV1_PARSER_OK)
      break;

    total_consumed += consumed;

    if (obu.obu_type == GST_AV1_OBU_SEQUENCE_HEADER)
      got_seq = TRUE;
    if (obu.obu_type == GST_AV1_OBU_REDUNDANT_FRAME_HEADER ||
        obu.obu_type == GST_AV1_OBU_FRAME ||
        obu.obu_type == GST_AV1_OBU_FRAME_HEADER)
      got_frame = TRUE;

    if (got_seq || got_frame)
      break;
  }

  gst_av1_parser_reset (self->parser, FALSE);

  if (res == GST_AV1_PARSER_OK || res == GST_AV1_PARSER_NO_MORE_DATA) {
    *skipsize = 0;

    /* If succeed recognize seq or frame, we can decide,
       otherwise, just skipsize to 0 and get more data. */
    if (got_seq || got_frame)
      self->in_align = GST_AV1_PARSE_ALIGN_BYTE;

    ret = GST_FLOW_OK;
    goto out;
  } else if (res == GST_AV1_PARSER_DROP) {
    total_consumed += consumed;
    res = GST_AV1_PARSER_OK;
    goto again;
  }

  /* Try the annexb. The buffer should hold the whole frame, and
     the buffer start with the frame size in leb128() format. */
  if (map_info.size < 8) {
    /* Get more data. */
    *skipsize = 0;
    ret = GST_FLOW_OK;
    goto out;
  }

  frame_sz = _read_leb128 (map_info.data, &res, &consumed);
  if (frame_sz == 0 || res != GST_AV1_PARSER_OK) {
    /* Both modes does not match, we can decide a error */
    ret = GST_FLOW_ERROR;
    goto out;
  }

  if (frame_sz + consumed != map_info.size) {
    GST_DEBUG_OBJECT (self, "Buffer size %" G_GSSIZE_FORMAT ", frame size %d,"
        " consumed %d, does not match annex b format.",
        map_info.size, frame_sz, consumed);
    /* Both modes does not match, we can decide a error */
    ret = GST_FLOW_ERROR;
    goto out;
  }

  self->in_align = GST_AV1_PARSE_ALIGN_TEMPORAL_UNIT_ANNEX_B;
  gst_av1_parser_reset (self->parser, TRUE);
  ret = GST_FLOW_OK;

out:
  gst_buffer_unmap (buffer, &map_info);
  gst_buffer_unref (buffer);
  return ret;
}

static GstFlowReturn
gst_av1_parse_handle_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, gint * skipsize)
{
  GstAV1Parse *self = GST_AV1_PARSE (parse);
  GstFlowReturn ret = GST_FLOW_OK;
  guint in_level, out_level;

  if (GST_BUFFER_FLAG_IS_SET (frame->buffer, GST_BUFFER_FLAG_DISCONT))
    self->discont = TRUE;
  else
    self->discont = FALSE;

  GST_LOG_OBJECT (self, "Input frame size %" G_GSSIZE_FORMAT,
      gst_buffer_get_size (frame->buffer));

  /* avoid stale cached parsing state */
  if (frame->flags & GST_BASE_PARSE_FRAME_FLAG_NEW_FRAME) {
    GST_LOG_OBJECT (self, "parsing new frame");
    gst_adapter_clear (self->cache_out);
    gst_adapter_clear (self->frame_cache);
    self->last_parsed_offset = 0;
    self->header = FALSE;
    self->keyframe = FALSE;
  } else {
    GST_LOG_OBJECT (self, "resuming frame parsing");
  }

  /* When in pull mode, the sink pad has no caps, we may get the
     caps by query the upstream element */
  if (self->in_align == GST_AV1_PARSE_ALIGN_NONE) {
    GstCaps *upstream_caps;

    upstream_caps =
        gst_pad_peer_query_caps (GST_BASE_PARSE_SINK_PAD (self), NULL);
    if (upstream_caps) {
      if (!gst_caps_is_empty (upstream_caps)
          && !gst_caps_is_any (upstream_caps)) {
        GST_LOG_OBJECT (self, "upstream caps: %" GST_PTR_FORMAT, upstream_caps);
        /* fixate to avoid ambiguity with lists when parsing */
        upstream_caps = gst_caps_fixate (upstream_caps);
        self->in_align = gst_av1_parse_alignment_from_caps (upstream_caps);
      }

      gst_caps_unref (upstream_caps);

      gst_av1_parser_reset (self->parser,
          self->in_align == GST_AV1_PARSE_ALIGN_TEMPORAL_UNIT_ANNEX_B);
    }

    if (self->in_align != GST_AV1_PARSE_ALIGN_NONE)
      GST_LOG_OBJECT (self, "Query the upstream get the alignment %d",
          self->in_align);
  }

  if (self->in_align == GST_AV1_PARSE_ALIGN_NONE) {
    /* Only happend at the first time of handle_frame, and the
       alignment in the sink caps is unset. Try the default and
       if error, try the annex B. */
    ret = gst_av1_parse_detect_alignment (parse, frame, skipsize);
    if (ret == GST_FLOW_OK && self->in_align != GST_AV1_PARSE_ALIGN_NONE)
      GST_INFO_OBJECT (self, "Detect the input alignment %d", self->in_align);
  }

  if (self->in_align == GST_AV1_PARSE_ALIGN_NONE) {
    GST_ERROR_OBJECT (self, "Input alignment is unknown");
    return GST_FLOW_ERROR;
  }

  /* We may in pull mode and no caps is set */
  if (self->align == GST_AV1_PARSE_ALIGN_NONE)
    gst_av1_parse_negotiate (self, NULL);

  in_level = self->in_align;
  if (self->in_align == GST_AV1_PARSE_ALIGN_TEMPORAL_UNIT_ANNEX_B)
    in_level = GST_AV1_PARSE_ALIGN_TEMPORAL_UNIT;
  out_level = self->align;
  if (self->align == GST_AV1_PARSE_ALIGN_TEMPORAL_UNIT_ANNEX_B)
    out_level = GST_AV1_PARSE_ALIGN_TEMPORAL_UNIT;

  if (self->in_align <= GST_AV1_PARSE_ALIGN_OBU
      && self->align == GST_AV1_PARSE_ALIGN_OBU) {
    ret = gst_av1_parse_handle_obu_to_obu (parse, frame, skipsize);
  } else if (in_level < out_level) {
    ret = gst_av1_parse_handle_to_big_align (parse, frame, skipsize);
  } else {
    ret = gst_av1_parse_handle_to_small_and_equal_align (parse,
        frame, skipsize);
  }

  return ret;
}
