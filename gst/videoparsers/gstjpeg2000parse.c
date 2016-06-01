/* GStreamer JPEG 2000 Parser
 * Copyright (C) <2016> Grok Image Compession Inc.
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

/* SOC marker plus minimum size of SIZ marker */
#define GST_JPEG2000_PARSE_MIN_FRAME_SIZE (4+36)
#define GST_JPEG2000_PARSE_J2K_MAGIC 0xFF4FFF51
#define GST_JPEG2000_PARSE_SIZE_OF_J2K_MAGIC 4

GST_DEBUG_CATEGORY (jpeg2000_parse_debug);
#define GST_CAT_DEFAULT jpeg2000_parse_debug

static GstStaticPadTemplate srctemplate =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/x-jpc,"
        " width = (int)[1, MAX], height = (int)[1, MAX],"
        "colorspace = (string) { sRGB, sYUV, GRAY }," "parsed = (boolean) true")
    );

static GstStaticPadTemplate sinktemplate =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/x-jpc,"
        "colorspace = (string) { sRGB, sYUV, GRAY }")
    );

#define parent_class gst_jpeg2000_parse_parent_class
G_DEFINE_TYPE (GstJPEG2000Parse, gst_jpeg2000_parse, GST_TYPE_BASE_PARSE);

static gboolean gst_jpeg2000_parse_start (GstBaseParse * parse);
static gboolean gst_jpeg2000_parse_event (GstBaseParse * parse,
    GstEvent * event);
static GstFlowReturn gst_jpeg2000_parse_handle_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, gint * skipsize);

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
  parse_class->start = GST_DEBUG_FUNCPTR (gst_jpeg2000_parse_start);
  parse_class->sink_event = GST_DEBUG_FUNCPTR (gst_jpeg2000_parse_event);
  parse_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_jpeg2000_parse_handle_frame);
}

static void
gst_jpeg2000_parse_init (GstJPEG2000Parse * jpeg2000parse)
{
  GST_PAD_SET_ACCEPT_INTERSECT (GST_BASE_PARSE_SINK_PAD (jpeg2000parse));
  GST_PAD_SET_ACCEPT_TEMPLATE (GST_BASE_PARSE_SINK_PAD (jpeg2000parse));
}

static gboolean
gst_jpeg2000_parse_start (GstBaseParse * parse)
{
  GstJPEG2000Parse *jpeg2000parse = GST_JPEG2000_PARSE (parse);
  guint i;
  GST_DEBUG_OBJECT (jpeg2000parse, "start");
  gst_base_parse_set_min_frame_size (parse, GST_JPEG2000_PARSE_MIN_FRAME_SIZE);

  jpeg2000parse->width = 0;
  jpeg2000parse->height = 0;

  for (i = 0; i < GST_JPEG2000_PARSE_MAX_SUPPORTED_COMPONENTS; ++i) {
    jpeg2000parse->dx[i] = 0;
    jpeg2000parse->dy[i] = 0;
  }

  jpeg2000parse->sampling = GST_RTP_SAMPLING_NONE;
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
  const gchar *colorspace = NULL;
  guint x0, y0, x1, y1;
  guint width, height;
  gboolean dimensions_changed = FALSE;
  guint8 dx[GST_JPEG2000_PARSE_MAX_SUPPORTED_COMPONENTS];
  guint8 dy[GST_JPEG2000_PARSE_MAX_SUPPORTED_COMPONENTS];       /* sub-sampling factors */
  gboolean subsampling_changed = FALSE;
  guint16 numcomps;
  guint16 compno;
  const char *sampling = NULL;
  GstRtpSampling samplingEnum = GST_RTP_SAMPLING_NONE;
  guint magic_offset = 0;

  if (!gst_buffer_map (frame->buffer, &map, GST_MAP_READ)) {
    GST_ERROR_OBJECT (jpeg2000parse, "Unable to map buffer");
    return GST_FLOW_ERROR;
  }

  gst_byte_reader_init (&reader, map.data, map.size);

  /* skip to beginning of frame */
  magic_offset = gst_byte_reader_masked_scan_uint32 (&reader, 0xffffffff,
      GST_JPEG2000_PARSE_J2K_MAGIC, 0, gst_byte_reader_get_remaining (&reader));

  if (magic_offset == -1) {
    *skipsize =
        gst_byte_reader_get_size (&reader) -
        GST_JPEG2000_PARSE_SIZE_OF_J2K_MAGIC;
    goto beach;
  } else {
    GST_DEBUG_OBJECT (jpeg2000parse, "Found magic at offset = %d",
        magic_offset);
    if (magic_offset > 0) {
      *skipsize = magic_offset;
      goto beach;
    }
  }

  /* 2 to skip marker size, and another 2 to skip rsiz field */
  if (!gst_byte_reader_skip (&reader,
          GST_JPEG2000_PARSE_SIZE_OF_J2K_MAGIC + 2 + 2))
    goto beach;

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
    ret = GST_FLOW_NOT_NEGOTIATED;
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

  if (numcomps > GST_JPEG2000_PARSE_MAX_SUPPORTED_COMPONENTS) {
    GST_ELEMENT_ERROR (jpeg2000parse, STREAM, DECODE, NULL,
        ("Unsupported number of components %d", numcomps));
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto beach;
  }

  current_caps = gst_pad_get_current_caps (GST_BASE_PARSE_SINK_PAD (parse));
  if (!current_caps) {
    GST_ERROR_OBJECT (jpeg2000parse, "Unable to get current caps");
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto beach;
  }

  current_caps_struct = gst_caps_get_structure (current_caps, 0);
  if (!current_caps_struct) {
    GST_ERROR_OBJECT (jpeg2000parse, "Unable to get structure of current caps");
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto beach;
  }

  /* colorspace is a required field */
  colorspace = gst_structure_get_string (current_caps_struct, "colorspace");
  if (!colorspace) {
    GST_ERROR_OBJECT (jpeg2000parse, "Missing color space");
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto beach;
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

  /* now we can set the dimensions and sub-sampling */
  if (width != jpeg2000parse->width || height != jpeg2000parse->height) {
    dimensions_changed = TRUE;
  }
  jpeg2000parse->width = width;
  jpeg2000parse->height = height;

  for (compno = 0; compno < numcomps; ++compno) {
    if (dx[compno] != jpeg2000parse->dx[compno]
        || dy[compno] != jpeg2000parse->dy[compno]) {
      subsampling_changed = TRUE;
    }
    jpeg2000parse->dx[compno] = dx[compno];
    jpeg2000parse->dx[compno] = dy[compno];
  }

  /* we do not set sampling field for sub-sampled RGB or monochrome */
  for (compno = 0; compno < numcomps; ++compno) {
    if (strcmp (colorspace, "sYUV") && (dx[compno] > 1 || dy[compno] > 1)) {
      GST_WARNING_OBJECT (jpeg2000parse,
          "Unable to set sampling field for sub-sampled RGB or monochrome color spaces");
      goto set_caps;
    }

  }

  /* sanity check on sub-sampling */
  if (dx[1] != dx[2] || dy[1] != dy[2]) {
    GST_WARNING_OBJECT (jpeg2000parse,
        "Unable to set sampling field because chroma channel sub-sampling factors are not equal");
    goto set_caps;
  }

  if (!strcmp (colorspace, "sYUV")) {
    /* reject sub-sampled YUVA image */
    if (numcomps == 4) {
      guint i;
      for (i = 0; i < 4; ++i) {
        if (dx[i] > 1 || dy[i] > 1) {
          GST_WARNING_OBJECT (jpeg2000parse,
              "Unable to set sampling field for sub-sampled YUVA images");
          goto set_caps;
        }
      }

      sampling = GST_RTP_J2K_YBRA;
      samplingEnum = GST_RTP_SAMPLING_YBRA;

    } else if (numcomps == 3) {
      /* use sub-sampling from U chroma channel */
      if (dx[1] == 1 && dy[1] == 1) {
        sampling = GST_RTP_J2K_YBR444;
        samplingEnum = GST_RTP_SAMPLING_YBR444;
      } else if (dx[1] == 2 && dy[1] == 2) {
        sampling = GST_RTP_J2K_YBR420;
        samplingEnum = GST_RTP_SAMPLING_YBR420;
      } else if (dx[1] == 4 && dy[1] == 2) {
        sampling = GST_RTP_J2K_YBR410;
        samplingEnum = GST_RTP_SAMPLING_YBR410;
      } else if (dx[1] == 2 && dy[1] == 1) {
        sampling = GST_RTP_J2K_YBR422;
        samplingEnum = GST_RTP_SAMPLING_YBR422;
      } else {
        GST_WARNING_OBJECT (jpeg2000parse,
            "Unable to set sampling field for sub-sampling factors %d,%d",
            dx[1], dy[1]);
        goto set_caps;
      }
    }
  } else if (!strcmp (colorspace, "GRAY")) {
    sampling = GST_RTP_J2K_GRAYSCALE;
    samplingEnum = GST_RTP_SAMPLING_GRAYSCALE;
  } else {
    if (numcomps == 4) {
      sampling = GST_RTP_J2K_RGBA;
      samplingEnum = GST_RTP_SAMPLING_RGBA;
    } else {
      sampling = GST_RTP_J2K_RGB;
      samplingEnum = GST_RTP_SAMPLING_RGB;
    }
  }

set_caps:

  if (dimensions_changed || subsampling_changed) {
    GstCaps *src_caps = NULL;
    gint fr_num, fr_denom;

    src_caps =
        gst_caps_new_simple (gst_structure_get_name (current_caps_struct),
        "width", G_TYPE_INT, jpeg2000parse->width, "height", G_TYPE_INT,
        jpeg2000parse->height, "colorspace", G_TYPE_STRING, colorspace, NULL);


    if (sampling) {
      gst_caps_set_simple (src_caps, "sampling", G_TYPE_STRING, sampling, NULL);
    }

    if (gst_structure_get_fraction (current_caps_struct, "framerate", &fr_num,
            &fr_denom)) {
      gst_caps_set_simple (src_caps, "framerate", GST_TYPE_FRACTION, fr_num,
          fr_denom, NULL);
    } else {
      GST_WARNING_OBJECT (jpeg2000parse, "No framerate set");
    }


    if (!gst_pad_set_caps (GST_BASE_PARSE_SRC_PAD (parse), src_caps)) {
      GST_ERROR_OBJECT (jpeg2000parse, "Unable to set source caps");
      ret = GST_FLOW_NOT_NEGOTIATED;
      gst_caps_unref (src_caps);
      goto beach;
    }

    gst_caps_unref (src_caps);
    jpeg2000parse->sampling = samplingEnum;
  }

  /* look for EOC end of codestream marker  */
  eoc_offset = gst_byte_reader_masked_scan_uint32 (&reader, 0x0000ffff,
      0xFFD9, 0, gst_byte_reader_get_remaining (&reader));

  if (eoc_offset != -1) {
    /* add 4 for eoc marker and eoc marker size */
    guint frame_size = gst_byte_reader_get_pos (&reader) + eoc_offset + 4;
    GST_DEBUG_OBJECT (jpeg2000parse,
        "Found EOC at offset = %d, frame size = %d", eoc_offset, frame_size);

    if (frame_size < gst_byte_reader_get_size (&reader))
      goto beach;

    gst_caps_unref (current_caps);
    gst_buffer_unmap (frame->buffer, &map);
    return gst_base_parse_finish_frame (parse, frame, frame_size);
  }

beach:
  if (current_caps)
    gst_caps_unref (current_caps);
  gst_buffer_unmap (frame->buffer, &map);
  return ret;

}
