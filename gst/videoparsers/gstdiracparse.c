/* GStreamer
 * Copyright (C) 2010 David Schleef <ds@schleef.org>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
/**
 * SECTION:element-gstdiracparse
 *
 * The gstdiracparse element does FIXME stuff.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v fakesrc ! gstdiracparse ! FIXME ! fakesink
 * ]|
 * FIXME Describe what the pipeline does.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbytereader.h>
#include <string.h>
#include "gstdiracparse.h"
#include "dirac_parse.h"

/* prototypes */


static void gst_dirac_parse_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_dirac_parse_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_dirac_parse_dispose (GObject * object);
static void gst_dirac_parse_finalize (GObject * object);

static gboolean gst_dirac_parse_start (GstBaseParse * parse);
static gboolean gst_dirac_parse_stop (GstBaseParse * parse);
static gboolean gst_dirac_parse_set_sink_caps (GstBaseParse * parse,
    GstCaps * caps);
static GstCaps *gst_dirac_parse_get_sink_caps (GstBaseParse * parse);
static gboolean gst_dirac_parse_check_valid_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, guint * framesize, gint * skipsize);
static GstFlowReturn gst_dirac_parse_parse_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame);
static gboolean gst_dirac_parse_convert (GstBaseParse * parse,
    GstFormat src_format, gint64 src_value, GstFormat dest_format,
    gint64 * dest_value);
static gboolean gst_dirac_parse_event (GstBaseParse * parse, GstEvent * event);
static gboolean gst_dirac_parse_src_event (GstBaseParse * parse,
    GstEvent * event);
static GstFlowReturn gst_dirac_parse_pre_push_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame);

enum
{
  PROP_0
};

/* pad templates */

static GstStaticPadTemplate gst_dirac_parse_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-dirac")
    );

static GstStaticPadTemplate gst_dirac_parse_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-dirac, parsed=(boolean)TRUE, "
        "width=(int)[1,MAX], height=(int)[1,MAX], "
        "framerate=(fraction)[0/1,MAX], "
        "pixel-aspect-ratio=(fraction)[0/1,MAX], "
        "interlaced=(boolean){TRUE,FALSE}, "
        "profile=(int)[0,MAX], level=(int)[0,MAX]")
    );

/* class initialization */

GST_BOILERPLATE (GstDiracParse, gst_dirac_parse, GstBaseParse,
    GST_TYPE_BASE_PARSE);

static void
gst_dirac_parse_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &gst_dirac_parse_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_dirac_parse_sink_template);

  gst_element_class_set_details_simple (element_class, "Dirac parser",
      "Codec/Parser/Video", "Parses Dirac streams",
      "David Schleef <ds@schleef.org>");
}

static void
gst_dirac_parse_class_init (GstDiracParseClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseParseClass *base_parse_class = GST_BASE_PARSE_CLASS (klass);

  gobject_class->set_property = gst_dirac_parse_set_property;
  gobject_class->get_property = gst_dirac_parse_get_property;
  gobject_class->dispose = gst_dirac_parse_dispose;
  gobject_class->finalize = gst_dirac_parse_finalize;
  base_parse_class->start = GST_DEBUG_FUNCPTR (gst_dirac_parse_start);
  base_parse_class->stop = GST_DEBUG_FUNCPTR (gst_dirac_parse_stop);
  base_parse_class->set_sink_caps =
      GST_DEBUG_FUNCPTR (gst_dirac_parse_set_sink_caps);
  base_parse_class->get_sink_caps =
      GST_DEBUG_FUNCPTR (gst_dirac_parse_get_sink_caps);
  base_parse_class->check_valid_frame =
      GST_DEBUG_FUNCPTR (gst_dirac_parse_check_valid_frame);
  base_parse_class->parse_frame =
      GST_DEBUG_FUNCPTR (gst_dirac_parse_parse_frame);
  base_parse_class->convert = GST_DEBUG_FUNCPTR (gst_dirac_parse_convert);
  base_parse_class->event = GST_DEBUG_FUNCPTR (gst_dirac_parse_event);
  base_parse_class->src_event = GST_DEBUG_FUNCPTR (gst_dirac_parse_src_event);
  base_parse_class->pre_push_frame =
      GST_DEBUG_FUNCPTR (gst_dirac_parse_pre_push_frame);

}

static void
gst_dirac_parse_init (GstDiracParse * diracparse,
    GstDiracParseClass * diracparse_class)
{
  gst_base_parse_set_min_frame_size (GST_BASE_PARSE (diracparse), 13);

}

void
gst_dirac_parse_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  g_return_if_fail (GST_IS_DIRAC_PARSE (object));

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_dirac_parse_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  g_return_if_fail (GST_IS_DIRAC_PARSE (object));

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_dirac_parse_dispose (GObject * object)
{
  g_return_if_fail (GST_IS_DIRAC_PARSE (object));

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

void
gst_dirac_parse_finalize (GObject * object)
{
  g_return_if_fail (GST_IS_DIRAC_PARSE (object));

  /* clean up object here */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}


static gboolean
gst_dirac_parse_start (GstBaseParse * parse)
{
  gst_base_parse_set_min_frame_size (parse, 13);

  return TRUE;
}

static gboolean
gst_dirac_parse_stop (GstBaseParse * parse)
{
  return TRUE;
}

static gboolean
gst_dirac_parse_set_sink_caps (GstBaseParse * parse, GstCaps * caps)
{
  /* Called when sink caps are set */
  return TRUE;
}

static gboolean
gst_dirac_parse_check_valid_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, guint * framesize, gint * skipsize)
{
  GstByteReader reader = GST_BYTE_READER_INIT_FROM_BUFFER (frame->buffer);
  int off;
  guint32 next_header;
  guint8 *data;
  int size;
  gboolean have_picture = FALSE;
  int offset;

  data = GST_BUFFER_DATA (frame->buffer);
  size = GST_BUFFER_SIZE (frame->buffer);

  if (G_UNLIKELY (size < 13))
    return FALSE;

  GST_DEBUG ("%d: %02x %02x %02x %02x", size, data[0], data[1], data[2],
      data[3]);

  if (GST_READ_UINT32_BE (data) != 0x42424344) {
    off = gst_byte_reader_masked_scan_uint32 (&reader, 0xffffffff,
        0x42424344, 0, GST_BUFFER_SIZE (frame->buffer));

    if (off < 0) {
      *skipsize = GST_BUFFER_SIZE (frame->buffer) - 3;
      return FALSE;
    }

    GST_LOG_OBJECT (parse, "possible sync at buffer offset %d", off);

    GST_DEBUG ("skipping %d", off);
    *skipsize = off;
    return FALSE;
  }

  /* have sync, parse chunks */

  offset = 0;
  while (!have_picture) {
    GST_DEBUG ("offset %d:", offset);

    if (offset + 13 >= size) {
      *framesize = offset + 13;
      return FALSE;
    }

    GST_DEBUG ("chunk type %02x", data[offset + 4]);

    if (GST_READ_UINT32_BE (data + offset) != 0x42424344) {
      GST_DEBUG ("bad header");
      *skipsize = 3;
      return FALSE;
    }

    next_header = GST_READ_UINT32_BE (data + offset + 5);
    GST_DEBUG ("next_header %d", next_header);
    if (next_header == 0)
      next_header = 13;

    if (SCHRO_PARSE_CODE_IS_PICTURE (data[offset + 4])) {
      have_picture = TRUE;
    }

    offset += next_header;
    if (offset >= size) {
      *framesize = offset;
      return FALSE;
    }
  }

  *framesize = offset;
  GST_DEBUG ("framesize %d", *framesize);

  return TRUE;
}

static GstFlowReturn
gst_dirac_parse_parse_frame (GstBaseParse * parse, GstBaseParseFrame * frame)
{
  GstDiracParse *diracparse = GST_DIRAC_PARSE (parse);
  guint8 *data;
  int size;

  /* Called when processing incoming buffers.  Function should parse
     a checked frame. */
  /* MUST implement */

  data = GST_BUFFER_DATA (frame->buffer);
  size = GST_BUFFER_SIZE (frame->buffer);

  //GST_ERROR("got here %d", size);

  if (data[4] == SCHRO_PARSE_CODE_SEQUENCE_HEADER) {
    GstCaps *caps;
    DiracSequenceHeader sequence_header;
    int ret;

    ret = dirac_sequence_header_parse (&sequence_header, data + 13, size - 13);
    if (ret) {
      memcpy (&diracparse->sequence_header, &sequence_header,
          sizeof (sequence_header));
      caps = gst_caps_new_simple ("video/x-dirac",
          "width", G_TYPE_INT, sequence_header.width,
          "height", G_TYPE_INT, sequence_header.height,
          "framerate", GST_TYPE_FRACTION,
          sequence_header.frame_rate_numerator,
          sequence_header.frame_rate_denominator,
          "pixel-aspect-ratio", GST_TYPE_FRACTION,
          sequence_header.aspect_ratio_numerator,
          sequence_header.aspect_ratio_denominator,
          "interlaced", G_TYPE_BOOLEAN, sequence_header.interlaced,
          "profile", G_TYPE_INT, sequence_header.profile,
          "level", G_TYPE_INT, sequence_header.level, NULL);
      gst_pad_set_caps (GST_BASE_PARSE_SRC_PAD (parse), caps);
      gst_caps_unref (caps);

      gst_base_parse_set_frame_rate (parse,
          sequence_header.frame_rate_numerator,
          sequence_header.frame_rate_denominator, 0, 0);
    }
  }

  gst_buffer_set_caps (frame->buffer,
      GST_PAD_CAPS (GST_BASE_PARSE_SRC_PAD (parse)));

  gst_base_parse_set_min_frame_size (parse, 13);

  return GST_FLOW_OK;
}

static gboolean
gst_dirac_parse_convert (GstBaseParse * parse, GstFormat src_format,
    gint64 src_value, GstFormat dest_format, gint64 * dest_value)
{
  /* Convert between formats */

  return FALSE;
}

static gboolean
gst_dirac_parse_event (GstBaseParse * parse, GstEvent * event)
{
  /* Sink pad event handler */

  return FALSE;
}

static gboolean
gst_dirac_parse_src_event (GstBaseParse * parse, GstEvent * event)
{
  /* Src pad event handler */

  return FALSE;
}

static GstFlowReturn
gst_dirac_parse_pre_push_frame (GstBaseParse * parse, GstBaseParseFrame * frame)
{

  return GST_FLOW_OK;
}

static GstCaps *
gst_dirac_parse_get_sink_caps (GstBaseParse * parse)
{
  GstCaps *peercaps;
  GstCaps *res;

  peercaps = gst_pad_get_allowed_caps (GST_BASE_PARSE_SRC_PAD (parse));
  if (peercaps) {
    guint i, n;

    /* Remove the parsed field */
    peercaps = gst_caps_make_writable (peercaps);
    n = gst_caps_get_size (peercaps);
    for (i = 0; i < n; i++) {
      GstStructure *s = gst_caps_get_structure (peercaps, i);
      gst_structure_remove_field (s, "parsed");
    }

    res =
        gst_caps_intersect_full (peercaps,
        gst_pad_get_pad_template_caps (GST_BASE_PARSE_SINK_PAD (parse)),
        GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (peercaps);
  } else {
    res =
        gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASE_PARSE_SINK_PAD
            (parse)));
  }

  return res;
}
