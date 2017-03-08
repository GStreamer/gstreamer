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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
/**
 * SECTION:element-irtspparse
 * @title: irtspparse
 * @short_description: Interleaved RTSP parser
 * @see_also: #GstPcapParse
 *
 * This is an interleaved RTSP parser that allows extracting specific
 * so-called "channels" from received interleaved (TCP) RTSP data
 * (typically extracted from some network capture).
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 filesrc location=h264crasher.pcap ! pcapparse ! irtspparse
 * ! rtph264depay ! ffdec_h264 ! fakesink
 * ]| Read from a pcap dump file using filesrc, extract the raw TCP packets,
 * depayload and decode them.
 *
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
static GstFlowReturn gst_irtsp_parse_handle_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, gint * skipsize);

static void gst_irtsp_parse_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_irtsp_parse_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

#define parent_class gst_irtsp_parse_parent_class
G_DEFINE_TYPE (GstIRTSPParse, gst_irtsp_parse, GST_TYPE_BASE_PARSE);

static void
gst_irtsp_parse_class_init (GstIRTSPParseClass * klass)
{
  GstBaseParseClass *parse_class = GST_BASE_PARSE_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
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
  parse_class->handle_frame = GST_DEBUG_FUNCPTR (gst_irtsp_parse_handle_frame);

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_static_metadata (element_class, "IRTSPParse",
      "Raw/Parser",
      "Parses a raw interleaved RTSP stream",
      "Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>");
}

static void
gst_irtsp_parse_reset (GstIRTSPParse * IRTSPParse)
{
}

static void
gst_irtsp_parse_init (GstIRTSPParse * IRTSPParse)
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

static GstFlowReturn
gst_irtsp_parse_handle_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, gint * skipsize)
{
  GstIRTSPParse *IRTSPParse = GST_IRTSP_PARSE (parse);
  GstBuffer *buf = frame->buffer;
  GstByteReader reader;
  gint off;
  GstMapInfo map;
  guint framesize;

  gst_buffer_map (buf, &map, GST_MAP_READ);
  if (G_UNLIKELY (map.size < 4))
    goto exit;

  gst_byte_reader_init (&reader, map.data, map.size);

  off = gst_byte_reader_masked_scan_uint32 (&reader, 0xffff0000,
      0x24000000 + (IRTSPParse->channel_id << 16), 0, map.size);

  GST_LOG_OBJECT (parse, "possible sync at buffer offset %d", off);

  /* didn't find anything that looks like a sync word, skip */
  if (off < 0) {
    *skipsize = map.size - 3;
    goto exit;
  }

  /* possible frame header, but not at offset 0? skip bytes before sync */
  if (off > 0) {
    *skipsize = off;
    goto exit;
  }

  framesize = GST_READ_UINT16_BE (map.data + 2) + 4;
  GST_LOG_OBJECT (parse, "got frame size %d", framesize);

  if (!gst_pad_has_current_caps (GST_BASE_PARSE_SRC_PAD (parse))) {
    GstCaps *caps;

    caps = gst_caps_new_empty_simple ("application/x-rtp");
    gst_pad_set_caps (GST_BASE_PARSE_SRC_PAD (parse), caps);
    gst_caps_unref (caps);
  }

  if (framesize <= map.size) {
    gst_buffer_unmap (buf, &map);
    /* HACK HACK skip header.
     * could also ask baseparse to skip this,
     * but that would give us a discontinuity for free
     * which is a bit too much to have on all our packets */
    frame->out_buffer = gst_buffer_copy (frame->buffer);
    gst_buffer_resize (frame->out_buffer, 4, -1);
    GST_BUFFER_FLAG_UNSET (frame->out_buffer, GST_BUFFER_FLAG_DISCONT);
    return gst_base_parse_finish_frame (parse, frame, framesize);
  }

exit:
  gst_buffer_unmap (buf, &map);
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
