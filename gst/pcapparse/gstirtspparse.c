/* GStreamer Interleaved RTSP parser
 * Copyright (C) 2011 Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>
 * Copyright (C) 2011 Nokia Corporation. All rights reserved.
 *   Contact: Stefan Kost <stefan.kost@nokia.com>
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
 * SECTION:element-irtspparse
 * @short_description: Interleaved RTSP parser
 * @see_also: #GstPcapParse
 *
 * This is an interleaved RTSP parser that allows extracting specific
 * so-called "channels" from received interleaved (TCP) RTSP data
 * (typically extracted from some network capture).
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-0.10 filesrc location=h264crasher.pcap ! pcapparse ! irtspparse
 * ! rtph264depay ! ffdec_h264 ! fakesink
 * ]| Read from a pcap dump file using filesrc, extract the raw TCP packets,
 * depayload and decode them.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstirtspparse.h"
#include <gst/base/gstbytereader.h>

GST_DEBUG_CATEGORY_STATIC (irtsp_parse_debug);
#define GST_CAT_DEFAULT irtsp_parse_debug

enum
{
  PROP_0,
  PROP_CHANNEL_ID
};


static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp ; application/x-rtcp"));

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void gst_irtsp_parse_finalize (GObject * object);

static gboolean gst_irtsp_parse_start (GstBaseParse * parse);
static gboolean gst_irtsp_parse_stop (GstBaseParse * parse);
static gboolean gst_irtsp_parse_check_valid_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, guint * size, gint * skipsize);
static GstFlowReturn gst_irtsp_parse_parse_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame);

static void gst_irtsp_parse_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_irtsp_parse_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

GST_BOILERPLATE (GstIRTSPParse, gst_irtsp_parse, GstBaseParse,
    GST_TYPE_BASE_PARSE);

static void
gst_irtsp_parse_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_details_simple (element_class, "IRTSPParse",
      "Raw/Parser",
      "Parses a raw interleaved RTSP stream",
      "Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>");
}

static void
gst_irtsp_parse_class_init (GstIRTSPParseClass * klass)
{
  GstBaseParseClass *parse_class = GST_BASE_PARSE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (irtsp_parse_debug, "irtspparse", 0,
      "Interleaved RTSP stream parser");

  object_class->finalize = gst_irtsp_parse_finalize;

  object_class->set_property = gst_irtsp_parse_set_property;
  object_class->get_property = gst_irtsp_parse_get_property;

  g_object_class_install_property (object_class, PROP_CHANNEL_ID,
      g_param_spec_int ("channel-id", "channel-id",
          "Channel Identifier", 0, 255,
          0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  parse_class->start = GST_DEBUG_FUNCPTR (gst_irtsp_parse_start);
  parse_class->stop = GST_DEBUG_FUNCPTR (gst_irtsp_parse_stop);
  parse_class->check_valid_frame =
      GST_DEBUG_FUNCPTR (gst_irtsp_parse_check_valid_frame);
  parse_class->parse_frame = GST_DEBUG_FUNCPTR (gst_irtsp_parse_parse_frame);
}

static void
gst_irtsp_parse_reset (GstIRTSPParse * IRTSPParse)
{
}

static void
gst_irtsp_parse_init (GstIRTSPParse * IRTSPParse, GstIRTSPParseClass * klass)
{
  gst_base_parse_set_min_frame_size (GST_BASE_PARSE (IRTSPParse), 4);
  gst_irtsp_parse_reset (IRTSPParse);
}

static void
gst_irtsp_parse_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_irtsp_parse_start (GstBaseParse * parse)
{
  GstIRTSPParse *IRTSPParse = GST_IRTSP_PARSE (parse);

  GST_DEBUG_OBJECT (parse, "starting");

  gst_irtsp_parse_reset (IRTSPParse);

  return TRUE;
}

static gboolean
gst_irtsp_parse_stop (GstBaseParse * parse)
{
  GST_DEBUG_OBJECT (parse, "stopping");

  return TRUE;
}

static gboolean
gst_irtsp_parse_check_valid_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, guint * framesize, gint * skipsize)
{
  GstIRTSPParse *IRTSPParse = GST_IRTSP_PARSE (parse);
  GstBuffer *buf = frame->buffer;
  GstByteReader reader = GST_BYTE_READER_INIT_FROM_BUFFER (buf);
  gint off;

  if (G_UNLIKELY (GST_BUFFER_SIZE (buf) < 4))
    return FALSE;

  off = gst_byte_reader_masked_scan_uint32 (&reader, 0xffff0000,
      0x24000000 + (IRTSPParse->channel_id << 16), 0, GST_BUFFER_SIZE (buf));

  GST_LOG_OBJECT (parse, "possible sync at buffer offset %d", off);

  /* didn't find anything that looks like a sync word, skip */
  if (off < 0) {
    *skipsize = GST_BUFFER_SIZE (buf) - 3;
    return FALSE;
  }

  /* possible frame header, but not at offset 0? skip bytes before sync */
  if (off > 0) {
    *skipsize = off;
    return FALSE;
  }

  *framesize = GST_READ_UINT16_BE (GST_BUFFER_DATA (frame->buffer) + 2) + 4;
  GST_LOG_OBJECT (parse, "got frame size %d", *framesize);

  return TRUE;
}

static GstFlowReturn
gst_irtsp_parse_parse_frame (GstBaseParse * parse, GstBaseParseFrame * frame)
{
  /* HACK HACK skip header.
   * could also ask baseparse to skip this,
   * but that would give us a discontinuity for free
   * which is a bit too much to have on all our packets */
  GST_BUFFER_DATA (frame->buffer) += 4;
  GST_BUFFER_SIZE (frame->buffer) -= 4;

  if (!GST_PAD_CAPS (GST_BASE_PARSE_SRC_PAD (parse))) {
    GstCaps *caps;

    caps = gst_caps_new_simple ("application/x-rtp", NULL);
    gst_pad_set_caps (GST_BASE_PARSE_SRC_PAD (parse), caps);
    gst_caps_unref (caps);
  }

  GST_BUFFER_FLAG_UNSET (frame->buffer, GST_BUFFER_FLAG_DISCONT);

  return GST_FLOW_OK;

}

static void
gst_irtsp_parse_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstIRTSPParse *IRTSPParse = GST_IRTSP_PARSE (object);

  switch (prop_id) {
    case PROP_CHANNEL_ID:
      IRTSPParse->channel_id = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_irtsp_parse_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstIRTSPParse *IRTSPParse = GST_IRTSP_PARSE (object);

  switch (prop_id) {
    case PROP_CHANNEL_ID:
      g_value_set_int (value, IRTSPParse->channel_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
