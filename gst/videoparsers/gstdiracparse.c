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
#include "gstdiracparse.h"

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
    GST_STATIC_CAPS ("video/x-dirac, parsed=(boolean)FALSE")
    );

static GstStaticPadTemplate gst_dirac_parse_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-dirac, parsed=(boolean)TRUE")
    );

/* class initialization */

GST_BOILERPLATE (GstDiracParse, gst_dirac_parse, GstBaseParse,
    GST_TYPE_BASE_PARSE);

static void
gst_dirac_parse_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_dirac_parse_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_dirac_parse_sink_template));

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
  GstDiracParse *diracparse;

  g_return_if_fail (GST_IS_DIRAC_PARSE (object));
  diracparse = GST_DIRAC_PARSE (object);

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
  GstDiracParse *diracparse;

  g_return_if_fail (GST_IS_DIRAC_PARSE (object));
  diracparse = GST_DIRAC_PARSE (object);

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_dirac_parse_dispose (GObject * object)
{
  GstDiracParse *diracparse;

  g_return_if_fail (GST_IS_DIRAC_PARSE (object));
  diracparse = GST_DIRAC_PARSE (object);

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

void
gst_dirac_parse_finalize (GObject * object)
{
  GstDiracParse *diracparse;

  g_return_if_fail (GST_IS_DIRAC_PARSE (object));
  diracparse = GST_DIRAC_PARSE (object);

  /* clean up object here */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}


static gboolean
gst_dirac_parse_start (GstBaseParse * parse)
{
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
gst_dirac_parse_frame_header (GstDiracParse * diracparse,
    GstBuffer * buffer, guint * framesize)
{
  int next_header;

  next_header = GST_READ_UINT32_BE (GST_BUFFER_DATA (buffer) + 5);

  *framesize = next_header;
  return TRUE;
}

static gboolean
gst_dirac_parse_check_valid_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, guint * framesize, gint * skipsize)
{
  GstByteReader reader = GST_BYTE_READER_INIT_FROM_BUFFER (frame->buffer);
  GstDiracParse *diracparse = GST_DIRAC_PARSE (parse);
  int off;
  guint32 next_header;
  gboolean lost_sync;
  gboolean draining;

  if (G_UNLIKELY (GST_BUFFER_SIZE (frame->buffer) < 13))
    return FALSE;

  off = gst_byte_reader_masked_scan_uint32 (&reader, 0xffffffff,
      0x42424344, 0, GST_BUFFER_SIZE (frame->buffer));

  if (off < 0) {
    *skipsize = GST_BUFFER_SIZE (frame->buffer) - 3;
    return FALSE;
  }

  GST_LOG_OBJECT (parse, "possible sync at buffer offset %d", off);

  if (off > 0) {
    GST_ERROR ("skipping %d", off);
    *skipsize = off;
    return FALSE;
  }

  if (!gst_dirac_parse_frame_header (diracparse, frame->buffer, framesize)) {
    GST_ERROR ("bad header");
    *skipsize = 3;
    return FALSE;
  }

  GST_LOG ("framesize %d", *framesize);

  lost_sync = GST_BASE_PARSE_LOST_SYNC (frame);
  draining = GST_BASE_PARSE_DRAINING (frame);

  if (lost_sync && !draining) {
    guint32 next_sync_word = 0;

    next_header = GST_READ_UINT32_BE (GST_BUFFER_DATA (frame->buffer) + 5);
    GST_LOG ("next header %d", next_header);

    if (!gst_byte_reader_skip (&reader, next_header) ||
        !gst_byte_reader_get_uint32_be (&reader, &next_sync_word)) {
      gst_base_parse_set_min_frame_size (parse, next_header + 4);
      *skipsize = 0;
      return FALSE;
    } else {
      if (next_sync_word != 0x42424344) {
        *skipsize = 3;
        return FALSE;
      } else {
        gst_base_parse_set_min_frame_size (parse, next_header);

      }
    }
  }

  return TRUE;
}

static GstFlowReturn
gst_dirac_parse_parse_frame (GstBaseParse * parse, GstBaseParseFrame * frame)
{
  //GstDiracParse * diracparse = GST_DIRAC_PARSE (parse);

  /* Called when processing incoming buffers.  Function should parse
     a checked frame. */
  /* MUST implement */

  if (GST_PAD_CAPS (GST_BASE_PARSE_SRC_PAD (parse)) == NULL) {
    GstCaps *caps = gst_caps_new_simple ("video/x-dirac", NULL);

    gst_buffer_set_caps (frame->buffer, caps);
    gst_pad_set_caps (GST_BASE_PARSE_SRC_PAD (parse), caps);
    gst_caps_unref (caps);

  }

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
