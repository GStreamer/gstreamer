/* GStreamer JPEG 2000 Parser
 * Copyright (C) <2016-2017> Grok Image Compression Inc.
 *  @author Aaron Boxer <boxerab@gmail.com>
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

#include "gstjpeg2000parse.h"
#include <gst/base/base.h>

/* Not used at the moment
static gboolean gst_jpeg2000_parse_is_cinema(guint16 rsiz)   {
	return ((rsiz >= GST_JPEG2000_PARSE_PROFILE_CINEMA_2K) && (rsiz <= GST_JPEG2000_PARSE_PROFILE_CINEMA_S4K));
}
static gboolean gst_jpeg2000_parse_is_storage(guint16 rsiz)   {
	return (rsiz == GST_JPEG2000_PARSE_PROFILE_CINEMA_LTS);
}
*/
static gboolean
gst_jpeg2000_parse_is_broadcast (guint16 rsiz)
{
  return ((rsiz >= GST_JPEG2000_PARSE_PROFILE_BC_SINGLE) &&
      (rsiz <= ((GST_JPEG2000_PARSE_PROFILE_BC_MULTI_R) | (0x000b)))
      && ((rsiz & (~GST_JPEG2000_PARSE_PROFILE_BC_MASK)) == 0));
}

static gboolean
gst_jpeg2000_parse_is_imf (guint16 rsiz)
{
  return ((rsiz >= GST_JPEG2000_PARSE_PROFILE_IMF_2K)
      && (rsiz <= ((GST_JPEG2000_PARSE_PROFILE_IMF_8K_R) | (0x009b))));
}

static gboolean
gst_jpeg2000_parse_is_part_2 (guint16 rsiz)
{
  return (rsiz & GST_JPEG2000_PARSE_PROFILE_PART2);
}



static void
gst_jpeg2000_parse_get_subsampling (GstJPEG2000Sampling sampling, guint8 * dx,
    guint8 * dy)
{
  *dx = 1;
  *dy = 1;
  if (sampling == GST_JPEG2000_SAMPLING_YBR422) {
    *dx = 2;
  } else if (sampling == GST_JPEG2000_SAMPLING_YBR420) {
    *dx = 2;
    *dy = 2;
  } else if (sampling == GST_JPEG2000_SAMPLING_YBR410) {
    *dx = 4;
    *dy = 2;
  }
}

#define GST_JPEG2000_JP2_SIZE_OF_BOX_ID  	4
#define GST_JPEG2000_JP2_SIZE_OF_BOX_LEN	4
#define GST_JPEG2000_MARKER_SIZE  	4


/* J2C has 8 bytes preceding J2K magic: 4 for size of box, and 4 for fourcc */
#define GST_JPEG2000_PARSE_SIZE_OF_J2C_PREFIX_BYTES (GST_JPEG2000_JP2_SIZE_OF_BOX_LEN +  GST_JPEG2000_JP2_SIZE_OF_BOX_ID)

/* SOC marker plus minimum size of SIZ marker */
#define GST_JPEG2000_PARSE_MIN_FRAME_SIZE (GST_JPEG2000_MARKER_SIZE + GST_JPEG2000_PARSE_SIZE_OF_J2C_PREFIX_BYTES + 36)

#define GST_JPEG2000_PARSE_J2K_MAGIC 0xFF4FFF51

GST_DEBUG_CATEGORY (jpeg2000_parse_debug);
#define GST_CAT_DEFAULT jpeg2000_parse_debug

static GstStaticPadTemplate srctemplate =
    GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/x-jpc,"
        " width = (int)[1, MAX], height = (int)[1, MAX],"
        GST_JPEG2000_SAMPLING_LIST ","
        GST_JPEG2000_COLORSPACE_LIST ","
        " profile = (int)[0, 49151],"
        " parsed = (boolean) true;"
        "image/x-j2c,"
        " width = (int)[1, MAX], height = (int)[1, MAX],"
        GST_JPEG2000_SAMPLING_LIST ","
        GST_JPEG2000_COLORSPACE_LIST ","
        " profile = (int)[0, 49151]," " parsed = (boolean) true")
    );

static GstStaticPadTemplate sinktemplate =
    GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/jp2;image/x-jpc;image/x-j2c"));

#define parent_class gst_jpeg2000_parse_parent_class
G_DEFINE_TYPE (GstJPEG2000Parse, gst_jpeg2000_parse, GST_TYPE_BASE_PARSE);

static gboolean gst_jpeg2000_parse_start (GstBaseParse * parse);
static gboolean gst_jpeg2000_parse_event (GstBaseParse * parse,
    GstEvent * event);
static GstFlowReturn gst_jpeg2000_parse_handle_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, gint * skipsize);
static gboolean gst_jpeg2000_parse_set_sink_caps (GstBaseParse * parse,
    GstCaps * caps);

static void
gst_jpeg2000_parse_class_init (GstJPEG2000ParseClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstBaseParseClass *parse_class = GST_BASE_PARSE_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (jpeg2000_parse_debug, "jpeg2000parse", 0,
      "jpeg 2000 parser");

  gst_element_class_add_static_pad_template (gstelement_class, &srctemplate);
  gst_element_class_add_static_pad_template (gstelement_class, &sinktemplate);
  gst_element_class_set_static_metadata (gstelement_class, "JPEG 2000 parser",
      "Codec/Parser/Video/Image",
      "Parses JPEG 2000 files", "Aaron Boxer <boxerab@gmail.com>");

  /* Override BaseParse vfuncs */
  parse_class->set_sink_caps =
      GST_DEBUG_FUNCPTR (gst_jpeg2000_parse_set_sink_caps);
  parse_class->start = GST_DEBUG_FUNCPTR (gst_jpeg2000_parse_start);
  parse_class->sink_event = GST_DEBUG_FUNCPTR (gst_jpeg2000_parse_event);
  parse_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_jpeg2000_parse_handle_frame);
}

static gboolean
gst_jpeg2000_parse_start (GstBaseParse * parse)
{
  GstJPEG2000Parse *jpeg2000parse = GST_JPEG2000_PARSE (parse);
  GST_DEBUG_OBJECT (jpeg2000parse, "start");
  gst_base_parse_set_min_frame_size (parse, GST_JPEG2000_PARSE_MIN_FRAME_SIZE);

  jpeg2000parse->width = 0;
  jpeg2000parse->height = 0;

  jpeg2000parse->sampling = GST_JPEG2000_SAMPLING_NONE;
  jpeg2000parse->colorspace = GST_JPEG2000_COLORSPACE_NONE;
  jpeg2000parse->codec_format = GST_JPEG2000_PARSE_NO_CODEC;
  return TRUE;
}


static void
gst_jpeg2000_parse_init (GstJPEG2000Parse * jpeg2000parse)
{
  GST_PAD_SET_ACCEPT_INTERSECT (GST_BASE_PARSE_SINK_PAD (jpeg2000parse));
  GST_PAD_SET_ACCEPT_TEMPLATE (GST_BASE_PARSE_SINK_PAD (jpeg2000parse));
}

static gboolean
gst_jpeg2000_parse_set_sink_caps (GstBaseParse * parse, GstCaps * caps)
{
  GstJPEG2000Parse *jpeg2000parse = GST_JPEG2000_PARSE (parse);
  GstStructure *caps_struct = gst_caps_get_structure (caps, 0);

  if (gst_structure_has_name (caps_struct, "image/jp2")) {
    jpeg2000parse->codec_format = GST_JPEG2000_PARSE_JP2;
  } else if (gst_structure_has_name (caps_struct, "image/x-j2c")) {
    jpeg2000parse->codec_format = GST_JPEG2000_PARSE_J2C;
  } else if (gst_structure_has_name (caps_struct, "image/x-jpc")) {
    jpeg2000parse->codec_format = GST_JPEG2000_PARSE_JPC;
  }

  return TRUE;
}

static gboolean
gst_jpeg2000_parse_event (GstBaseParse * parse, GstEvent * event)
{
  gboolean res = GST_BASE_PARSE_CLASS (parent_class)->sink_event (parse, event);
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      gst_base_parse_set_min_frame_size (parse,
          GST_JPEG2000_PARSE_MIN_FRAME_SIZE);
      break;
    default:
      break;
  }
  return res;
}

static GstJPEG2000ParseFormats
format_from_media_type (const GstStructure * structure)
{
  const char *media_type = gst_structure_get_name (structure);
  if (!strcmp (media_type, "image/x-j2c"))
    return GST_JPEG2000_PARSE_J2C;
  if (!strcmp (media_type, "image/x-jpc"))
    return GST_JPEG2000_PARSE_JPC;
  if (!strcmp (media_type, "image/x-jp2"))
    return GST_JPEG2000_PARSE_JP2;
  return GST_JPEG2000_PARSE_NO_CODEC;
}

/* check downstream caps to configure media type */
static void
gst_jpeg2000_parse_negotiate (GstJPEG2000Parse * parse, GstCaps * in_caps)
{
  GstCaps *caps;
  guint codec_format = GST_JPEG2000_PARSE_NO_CODEC;

  g_return_if_fail ((in_caps == NULL) || gst_caps_is_fixed (in_caps));

  caps = gst_pad_get_allowed_caps (GST_BASE_PARSE_SRC_PAD (parse));
  GST_DEBUG_OBJECT (parse, "allowed caps: %" GST_PTR_FORMAT, caps);

  /* concentrate on leading structure, since decodebin parser
   * capsfilter always includes parser template caps */
  if (caps) {
    caps = gst_caps_truncate (caps);
    GST_DEBUG_OBJECT (parse, "negotiating with caps: %" GST_PTR_FORMAT, caps);
  }

  if (in_caps && caps) {
    if (gst_caps_can_intersect (in_caps, caps)) {
      GST_DEBUG_OBJECT (parse, "downstream accepts upstream caps");
      codec_format =
          format_from_media_type (gst_caps_get_structure (in_caps, 0));
      gst_caps_unref (caps);
      caps = NULL;
    }
  }

  if (caps && !gst_caps_is_empty (caps)) {
    /* fixate to avoid ambiguity with lists when parsing */
    caps = gst_caps_fixate (caps);
    codec_format = format_from_media_type (gst_caps_get_structure (caps, 0));
  }

  GST_DEBUG_OBJECT (parse, "selected codec format %d", codec_format);

  parse->codec_format = codec_format;

  if (caps)
    gst_caps_unref (caps);
}

static const char *
media_type_from_codec_format (GstJPEG2000ParseFormats f)
{
  switch (f) {
    case GST_JPEG2000_PARSE_J2C:
      return "image/x-j2c";
    case GST_JPEG2000_PARSE_JP2:
      return "image/x-jp2";
    case GST_JPEG2000_PARSE_JPC:
      return "image/x-jpc";
    default:
      g_assert_not_reached ();
      return "invalid/x-invalid";
  }
}

static GstFlowReturn
gst_jpeg2000_parse_handle_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, gint * skipsize)
{
  GstJPEG2000Parse *jpeg2000parse = GST_JPEG2000_PARSE (parse);
  GstMapInfo map;
  GstByteReader reader;
  GstFlowReturn ret = GST_FLOW_OK;
  guint eoc_offset = 0;
  GstCaps *current_caps = NULL;
  GstStructure *current_caps_struct = NULL;
  GstJPEG2000Colorspace colorspace = GST_JPEG2000_COLORSPACE_NONE;
  guint x0, y0, x1, y1;
  guint width = 0, height = 0;
  guint8 dx[GST_JPEG2000_PARSE_MAX_SUPPORTED_COMPONENTS];
  guint8 dy[GST_JPEG2000_PARSE_MAX_SUPPORTED_COMPONENTS];
  guint16 numcomps;
  guint16 capabilities = 0;
  guint16 profile = 0;
  gboolean validate_main_level = FALSE;
  guint8 main_level = 0;
  guint8 sub_level = 0;
  guint16 compno;
  GstJPEG2000Sampling parsed_sampling = GST_JPEG2000_SAMPLING_NONE;
  const gchar *sink_sampling_string = NULL;
  GstJPEG2000Sampling sink_sampling = GST_JPEG2000_SAMPLING_NONE;
  GstJPEG2000Sampling source_sampling = GST_JPEG2000_SAMPLING_NONE;
  guint magic_offset = 0;
  guint j2c_box_id_offset = 0;
  guint num_prefix_bytes = 0;   /* number of bytes to skip before actual code stream */
  GstCaps *src_caps = NULL;
  guint frame_size = 0;
  gboolean is_j2c;
  gboolean parsed_j2c_4cc = FALSE;

  if (!gst_buffer_map (frame->buffer, &map, GST_MAP_READ)) {
    GST_ERROR_OBJECT (jpeg2000parse, "Unable to map buffer");
    return GST_FLOW_ERROR;
  }
  gst_byte_reader_init (&reader, map.data, map.size);

  /* try to get from caps */
  if (jpeg2000parse->codec_format == GST_JPEG2000_PARSE_NO_CODEC)
    gst_jpeg2000_parse_negotiate (jpeg2000parse, NULL);

  /* if we can't get from caps, then try to parse */
  if (jpeg2000parse->codec_format == GST_JPEG2000_PARSE_NO_CODEC) {
    /* check for "jp2c" box */
    /* both jp2 and j2c will be found with this scan, and both will be treated as j2c format */
    j2c_box_id_offset = gst_byte_reader_masked_scan_uint32 (&reader, 0xffffffff,
        GST_MAKE_FOURCC ('j', 'p', '2', 'c'), 0,
        gst_byte_reader_get_remaining (&reader));
    parsed_j2c_4cc = TRUE;
    is_j2c = j2c_box_id_offset != -1;
    jpeg2000parse->codec_format =
        is_j2c ? GST_JPEG2000_PARSE_J2C : GST_JPEG2000_PARSE_JPC;

  } else {
    /* for now, just treat JP2 as J2C */
    if (jpeg2000parse->codec_format == GST_JPEG2000_PARSE_JP2) {
      jpeg2000parse->codec_format = GST_JPEG2000_PARSE_J2C;
    }
    is_j2c = jpeg2000parse->codec_format == GST_JPEG2000_PARSE_J2C;
  }

  num_prefix_bytes = GST_JPEG2000_MARKER_SIZE;
  if (is_j2c) {
    num_prefix_bytes +=
        GST_JPEG2000_JP2_SIZE_OF_BOX_LEN + GST_JPEG2000_JP2_SIZE_OF_BOX_ID;
    /* check for "jp2c" (may have already parsed j2c_box_id_offset if caps are empty) */
    if (!parsed_j2c_4cc) {
      j2c_box_id_offset =
          gst_byte_reader_masked_scan_uint32 (&reader, 0xffffffff,
          GST_MAKE_FOURCC ('j', 'p', '2', 'c'), 0,
          gst_byte_reader_get_remaining (&reader));
    }

    if (j2c_box_id_offset == -1) {
      GST_ELEMENT_ERROR (jpeg2000parse, STREAM, DECODE, NULL,
          ("Missing contiguous code stream box for j2c stream"));
      ret = GST_FLOW_ERROR;
      goto beach;
    }
  }

  /* Look for magic. If found, skip to beginning of frame */
  magic_offset = gst_byte_reader_masked_scan_uint32 (&reader, 0xffffffff,
      GST_JPEG2000_PARSE_J2K_MAGIC, 0, gst_byte_reader_get_remaining (&reader));
  if (magic_offset == -1) {
    *skipsize = gst_byte_reader_get_size (&reader) - num_prefix_bytes;
    goto beach;
  }

  /* see if we need to skip any bytes at beginning of frame */
  GST_DEBUG_OBJECT (jpeg2000parse, "Found magic at offset = %d", magic_offset);
  if (magic_offset > 0) {
    *skipsize = magic_offset;
    /* J2C has 8 bytes preceding J2K magic */
    if (is_j2c)
      *skipsize -= GST_JPEG2000_PARSE_SIZE_OF_J2C_PREFIX_BYTES;
    if (*skipsize > 0)
      goto beach;
  }

  if (is_j2c) {
    /* sanity check on box id offset */
    if (j2c_box_id_offset + GST_JPEG2000_JP2_SIZE_OF_BOX_ID != magic_offset) {
      GST_ELEMENT_ERROR (jpeg2000parse, STREAM, DECODE, NULL,
          ("Corrupt contiguous code stream box for j2c stream"));
      ret = GST_FLOW_ERROR;
      goto beach;
    }

    /* check that we have enough bytes for the J2C box length */
    if (j2c_box_id_offset < GST_JPEG2000_JP2_SIZE_OF_BOX_LEN) {
      *skipsize = gst_byte_reader_get_size (&reader) - num_prefix_bytes;
      goto beach;
    }

    if (!gst_byte_reader_skip (&reader,
            j2c_box_id_offset - GST_JPEG2000_JP2_SIZE_OF_BOX_LEN))
      goto beach;

    /* read the box length, and adjust num_prefix_bytes accordingly  */
    if (!gst_byte_reader_get_uint32_be (&reader, &frame_size))
      goto beach;
    num_prefix_bytes -= GST_JPEG2000_JP2_SIZE_OF_BOX_LEN;

    /* bail out if not enough data for frame */
    if ((gst_byte_reader_get_size (&reader) < frame_size))
      goto beach;
  }

  /* 2 to skip marker size */
  if (!gst_byte_reader_skip (&reader, num_prefix_bytes + 2))
    goto beach;

  if (!gst_byte_reader_get_uint16_be (&reader, &capabilities))
    goto beach;

  profile = capabilities & GST_JPEG2000_PARSE_PROFILE_MASK;
  if (!gst_jpeg2000_parse_is_part_2 (capabilities)) {
    if ((profile > GST_JPEG2000_PARSE_PROFILE_CINEMA_LTS)
        && !gst_jpeg2000_parse_is_broadcast (profile)
        && !gst_jpeg2000_parse_is_imf (profile)) {
      GST_ELEMENT_ERROR (jpeg2000parse, STREAM, DECODE, NULL,
          ("Unrecognized JPEG 2000 profile %d", profile));
      ret = GST_FLOW_ERROR;
      goto beach;
    }
    if (gst_jpeg2000_parse_is_broadcast (profile)) {
      main_level = capabilities & 0xF;
      validate_main_level = TRUE;
    } else if (gst_jpeg2000_parse_is_imf (profile)) {
      main_level = capabilities & 0xF;
      validate_main_level = TRUE;
      sub_level = (capabilities >> 4) & 0xF;
      if (sub_level > 9) {
        GST_ELEMENT_ERROR (jpeg2000parse, STREAM, DECODE, NULL,
            ("Sub level %d is invalid", sub_level));
        ret = GST_FLOW_ERROR;
        goto beach;
      }
    }
    if (validate_main_level && main_level > 11) {
      GST_ELEMENT_ERROR (jpeg2000parse, STREAM, DECODE, NULL,
          ("Main level %d is invalid", main_level));
      ret = GST_FLOW_ERROR;
      goto beach;

    }
  }


  if (!gst_byte_reader_get_uint32_be (&reader, &x1))
    goto beach;

  if (!gst_byte_reader_get_uint32_be (&reader, &y1))
    goto beach;

  if (!gst_byte_reader_get_uint32_be (&reader, &x0))
    goto beach;

  if (!gst_byte_reader_get_uint32_be (&reader, &y0))
    goto beach;

  /* sanity check on image dimensions */
  if (x1 < x0 || y1 < y0) {
    GST_ELEMENT_ERROR (jpeg2000parse, STREAM, DECODE, NULL,
        ("Nonsensical image dimensions %d,%d,%d,%d", x0, y0, x1, y1));
    ret = GST_FLOW_ERROR;
    goto beach;
  }

  width = x1 - x0;
  height = y1 - y0;

  GST_DEBUG_OBJECT (jpeg2000parse, "Parsed image dimensions %d,%d", width,
      height);

  /* skip tile dimensions */
  if (!gst_byte_reader_skip (&reader, 4 * 4))
    goto beach;

  /* read number of components */
  if (!gst_byte_reader_get_uint16_be (&reader, &numcomps))
    goto beach;

  if (numcomps == 0 || numcomps > GST_JPEG2000_PARSE_MAX_SUPPORTED_COMPONENTS) {
    GST_ELEMENT_ERROR (jpeg2000parse, STREAM, DECODE, NULL,
        ("Unsupported number of components %d", numcomps));
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto beach;
  }

  current_caps = gst_pad_get_current_caps (GST_BASE_PARSE_SINK_PAD (parse));
  if (current_caps) {
    const gchar *colorspace_string = NULL;
    current_caps_struct = gst_caps_get_structure (current_caps, 0);
    if (!current_caps_struct) {
      GST_ERROR_OBJECT (jpeg2000parse,
          "Unable to get structure of current caps struct");
      ret = GST_FLOW_NOT_NEGOTIATED;
      goto beach;
    }

    colorspace_string = gst_structure_get_string
        (current_caps_struct, "colorspace");
    if (colorspace_string)
      colorspace = gst_jpeg2000_colorspace_from_string (colorspace_string);
    sink_sampling_string = gst_structure_get_string
        (current_caps_struct, "sampling");
    if (sink_sampling_string)
      sink_sampling = gst_jpeg2000_sampling_from_string (sink_sampling_string);

  } else {
    /* guess color space based on number of components       */
    if (numcomps == 0 || numcomps > 4) {
      GST_ERROR_OBJECT (jpeg2000parse,
          "Unable to guess color space from number of components %d", numcomps);
      ret = GST_FLOW_NOT_NEGOTIATED;
      goto beach;
    }
    colorspace =
        (numcomps >=
        3) ? GST_JPEG2000_COLORSPACE_RGB : GST_JPEG2000_COLORSPACE_GRAY;
    if (numcomps == 4) {
      GST_WARNING_OBJECT (jpeg2000parse, "No caps available: assuming RGBA");
    } else if (numcomps == 3) {
      GST_WARNING_OBJECT (jpeg2000parse, "No caps available: assuming RGB");
    } else if (numcomps == 2) {
      GST_WARNING_OBJECT (jpeg2000parse,
          "No caps available: assuming grayscale with alpha");
    }

  }

  for (compno = 0; compno < numcomps; ++compno) {

    /* skip Ssiz (precision and signed/unsigned bit )  */
    if (!gst_byte_reader_skip (&reader, 1))
      goto beach;

    if (!gst_byte_reader_get_uint8 (&reader, dx + compno))
      goto beach;

    if (!gst_byte_reader_get_uint8 (&reader, dy + compno))
      goto beach;

    GST_DEBUG_OBJECT (jpeg2000parse,
        "Parsed sub-sampling %d,%d for component %d", dx[compno], dy[compno],
        compno);
  }

  /*** sanity check on sub-sampling *****/
  if (dx[0] != 1 || dy[0] != 1) {
    GST_WARNING_OBJECT (jpeg2000parse, "Sub-sampled luma channel");
  }
  if (dx[1] != dx[2] || dy[1] != dy[2]) {
    GST_WARNING_OBJECT (jpeg2000parse,
        "Chroma channel sub-sampling factors are not equal");
  }
  for (compno = 0; compno < numcomps; ++compno) {
    if (colorspace != GST_JPEG2000_COLORSPACE_NONE
        && (colorspace != GST_JPEG2000_COLORSPACE_YUV)
        && (dx[compno] > 1 || dy[compno] > 1)) {
      GST_WARNING_OBJECT (jpeg2000parse,
          "Sub-sampled RGB or monochrome color spaces");
    }
    if (sink_sampling != GST_JPEG2000_SAMPLING_NONE) {
      guint8 dx_caps, dy_caps;
      gst_jpeg2000_parse_get_subsampling (sink_sampling, &dx_caps, &dy_caps);
      if (dx_caps != dx[compno] || dy_caps != dy[compno]) {
        GstJPEG2000Colorspace inferred_colorspace =
            GST_JPEG2000_COLORSPACE_NONE;
        GST_WARNING_OBJECT (jpeg2000parse,
            "Sink caps sub-sampling %d,%d for channel %d does not match stream sub-sampling %d,%d",
            dx_caps, dy_caps, compno, dx[compno], dy[compno]);
        /* try to guess correct color space */
        if (gst_jpeg2000_sampling_is_mono (sink_sampling))
          inferred_colorspace = GST_JPEG2000_COLORSPACE_GRAY;
        else if (gst_jpeg2000_sampling_is_rgb (sink_sampling))
          inferred_colorspace = GST_JPEG2000_COLORSPACE_RGB;
        else if (gst_jpeg2000_sampling_is_yuv (sink_sampling))
          inferred_colorspace = GST_JPEG2000_COLORSPACE_YUV;
        else if (colorspace)
          inferred_colorspace = colorspace;
        if (inferred_colorspace != GST_JPEG2000_COLORSPACE_NONE) {
          sink_sampling = GST_JPEG2000_SAMPLING_NONE;
          colorspace = inferred_colorspace;
          break;
        } else {
          /* unrecognized sink_sampling and no colorspace */
          GST_ERROR_OBJECT (jpeg2000parse,
              "Unrecognized sink sampling field and no sink colorspace field");
          ret = GST_FLOW_NOT_NEGOTIATED;
          goto beach;
        }
      }
    }
  }
  /*************************************/

  /* if colorspace is present, we can work out the parsed_sampling field */
  if (colorspace != GST_JPEG2000_COLORSPACE_NONE) {
    if (colorspace == GST_JPEG2000_COLORSPACE_YUV) {
      if (numcomps == 4) {
        guint i;
        parsed_sampling = GST_JPEG2000_SAMPLING_YBRA4444_EXT;
        for (i = 0; i < 4; ++i) {
          if (dx[i] > 1 || dy[i] > 1) {
            GST_WARNING_OBJECT (jpeg2000parse, "Sub-sampled YUVA images");
          }
        }
      } else if (numcomps == 3) {
        /* use sub-sampling from U chroma channel */
        if (dx[1] == 1 && dy[1] == 1) {
          parsed_sampling = GST_JPEG2000_SAMPLING_YBR444;
        } else if (dx[1] == 2 && dy[1] == 2) {
          parsed_sampling = GST_JPEG2000_SAMPLING_YBR420;
        } else if (dx[1] == 4 && dy[1] == 2) {
          parsed_sampling = GST_JPEG2000_SAMPLING_YBR410;
        } else if (dx[1] == 2 && dy[1] == 1) {
          parsed_sampling = GST_JPEG2000_SAMPLING_YBR422;
        } else {
          GST_WARNING_OBJECT (jpeg2000parse,
              "Unsupported sub-sampling factors %d,%d", dx[1], dy[1]);
          /* best effort */
          parsed_sampling = GST_JPEG2000_SAMPLING_YBR444;
        }
      }
    } else if (colorspace == GST_JPEG2000_COLORSPACE_GRAY) {
      parsed_sampling = GST_JPEG2000_SAMPLING_GRAYSCALE;
    } else {
      parsed_sampling =
          (numcomps ==
          4) ? GST_JPEG2000_SAMPLING_RGBA : GST_JPEG2000_SAMPLING_RGB;
    }
  } else {
    if (gst_jpeg2000_sampling_is_mono (sink_sampling)) {
      colorspace = GST_JPEG2000_COLORSPACE_GRAY;
    } else if (gst_jpeg2000_sampling_is_rgb (sink_sampling)) {
      colorspace = GST_JPEG2000_COLORSPACE_RGB;
    } else {
      /* best effort */
      colorspace = GST_JPEG2000_COLORSPACE_YUV;
    }
  }

  gst_jpeg2000_parse_negotiate (jpeg2000parse, current_caps);

  /* now we can set the source caps, if something has changed */
  source_sampling =
      sink_sampling !=
      GST_JPEG2000_SAMPLING_NONE ? sink_sampling : parsed_sampling;
  if (width != jpeg2000parse->width || height != jpeg2000parse->height
      || jpeg2000parse->sampling != source_sampling
      || jpeg2000parse->colorspace != colorspace) {
    gint fr_num = 0, fr_denom = 0;

    jpeg2000parse->width = width;
    jpeg2000parse->height = height;
    jpeg2000parse->sampling = source_sampling;
    jpeg2000parse->colorspace = colorspace;

    src_caps =
        gst_caps_new_simple (media_type_from_codec_format
        (jpeg2000parse->codec_format),
        "width", G_TYPE_INT, width,
        "height", G_TYPE_INT, height,
        "colorspace", G_TYPE_STRING,
        gst_jpeg2000_colorspace_to_string (colorspace), "sampling",
        G_TYPE_STRING, gst_jpeg2000_sampling_to_string (source_sampling),
        "profile", G_TYPE_UINT, profile, NULL);

    if (gst_jpeg2000_parse_is_broadcast (capabilities)
        || gst_jpeg2000_parse_is_imf (capabilities)) {
      gst_caps_set_simple (src_caps, "main-level", G_TYPE_UINT, main_level,
          NULL);
      if (gst_jpeg2000_parse_is_imf (capabilities)) {
        gst_caps_set_simple (src_caps, "sub-level", G_TYPE_UINT, sub_level,
            NULL);
      }
    }

    if (current_caps_struct) {
      const gchar *caps_string = gst_structure_get_string
          (current_caps_struct, "colorimetry");
      if (caps_string) {
        gst_caps_set_simple (src_caps, "colorimetry", G_TYPE_STRING,
            caps_string, NULL);
      }
      caps_string = gst_structure_get_string
          (current_caps_struct, "interlace-mode");
      if (caps_string) {
        gst_caps_set_simple (src_caps, "interlace-mode", G_TYPE_STRING,
            caps_string, NULL);
      }
      caps_string = gst_structure_get_string
          (current_caps_struct, "field-order");
      if (caps_string) {
        gst_caps_set_simple (src_caps, "field-order", G_TYPE_STRING,
            caps_string, NULL);
      }
      caps_string = gst_structure_get_string
          (current_caps_struct, "multiview-mode");
      if (caps_string) {
        gst_caps_set_simple (src_caps, "multiview-mode", G_TYPE_STRING,
            caps_string, NULL);
      }
      caps_string = gst_structure_get_string
          (current_caps_struct, "chroma-site");
      if (caps_string) {
        gst_caps_set_simple (src_caps, "chroma-site", G_TYPE_STRING,
            caps_string, NULL);
      }
      if (gst_structure_get_fraction (current_caps_struct, "framerate", &fr_num,
              &fr_denom)) {
        gst_caps_set_simple (src_caps, "framerate", GST_TYPE_FRACTION, fr_num,
            fr_denom, NULL);
      } else {
        GST_WARNING_OBJECT (jpeg2000parse, "No framerate set");
      }
    }

    if (!gst_pad_set_caps (GST_BASE_PARSE_SRC_PAD (parse), src_caps)) {
      GST_ERROR_OBJECT (jpeg2000parse, "Unable to set source caps");
      ret = GST_FLOW_NOT_NEGOTIATED;
      gst_caps_unref (src_caps);
      goto beach;
    }
    gst_caps_unref (src_caps);
  }
  /*************************************************/

  /* look for EOC to mark frame end */
  /* look for EOC end of codestream marker  */
  eoc_offset = gst_byte_reader_masked_scan_uint32 (&reader, 0x0000ffff,
      0xFFD9, 0, gst_byte_reader_get_remaining (&reader));

  if (eoc_offset != -1) {
    /* add 4 for eoc marker and eoc marker size */
    guint eoc_frame_size = gst_byte_reader_get_pos (&reader) + eoc_offset + 4;
    GST_DEBUG_OBJECT (jpeg2000parse,
        "Found EOC at offset = %d, frame size = %d", eoc_offset,
        eoc_frame_size);

    /* bail out if not enough data for frame */
    if (gst_byte_reader_get_size (&reader) < eoc_frame_size)
      goto beach;

    if (frame_size && frame_size != eoc_frame_size) {
      GST_WARNING_OBJECT (jpeg2000parse,
          "Frame size %d from contiguous code size does not equal frame size %d signalled by eoc",
          frame_size, eoc_frame_size);
    }
    frame_size = eoc_frame_size;
  } else {
    goto beach;
  }

  /* clean up and finish frame */
  if (current_caps)
    gst_caps_unref (current_caps);
  gst_buffer_unmap (frame->buffer, &map);
  return gst_base_parse_finish_frame (parse, frame, frame_size);

beach:
  if (current_caps)
    gst_caps_unref (current_caps);
  gst_buffer_unmap (frame->buffer, &map);
  return ret;
}
