/* GStreamer
 * Copyright (C) 2004 Wim Taymans <wim@fluendo.com>
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
 * Copyright (C) 2008 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 * SECTION:element-opusparse
 * @see_also: opusenc, opusdec
 *
 * This element parses OPUS packets.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v filesrc location=opusdata ! opusparse ! opusdec ! audioconvert ! audioresample ! alsasink
 * ]| Decode and plays an unmuxed Opus file.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <opus/opus.h>
#include "gstopusparse.h"

GST_DEBUG_CATEGORY_STATIC (opusparse_debug);
#define GST_CAT_DEFAULT opusparse_debug

#define MAX_PAYLOAD_BYTES 1500

static GstStaticPadTemplate opus_parse_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-opus, framed = (boolean) true")
    );

static GstStaticPadTemplate opus_parse_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-opus")
    );

G_DEFINE_TYPE (GstOpusParse, gst_opus_parse, GST_TYPE_BASE_PARSE);

static gboolean gst_opus_parse_start (GstBaseParse * parse);
static gboolean gst_opus_parse_check_valid_frame (GstBaseParse * base,
    GstBaseParseFrame * frame, guint * frame_size, gint * skip);

static void
gst_opus_parse_class_init (GstOpusParseClass * klass)
{
  GstBaseParseClass *bpclass;
  GstElementClass *element_class;

  bpclass = (GstBaseParseClass *) klass;
  element_class = (GstElementClass *) klass;

  bpclass->start = GST_DEBUG_FUNCPTR (gst_opus_parse_start);
  bpclass->check_valid_frame =
      GST_DEBUG_FUNCPTR (gst_opus_parse_check_valid_frame);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&opus_parse_src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&opus_parse_sink_factory));
  gst_element_class_set_details_simple (element_class, "Opus audio parser",
      "Codec/Parser/Audio",
      "parses opus audio streams",
      "Vincent Penquerc'h <vincent.penquerch@collabora.co.uk>");

  GST_DEBUG_CATEGORY_INIT (opusparse_debug, "opusparse", 0,
      "opus parsing element");
}

static void
gst_opus_parse_init (GstOpusParse * parse)
{
}

static gboolean
gst_opus_parse_start (GstBaseParse * base)
{
  GstOpusParse *parse = GST_OPUS_PARSE (base);
  GstCaps *caps;

  caps = gst_caps_new_empty_simple ("audio/x-opus");
  gst_pad_set_caps (GST_BASE_PARSE_SRC_PAD (GST_BASE_PARSE (parse)), caps);
  gst_caps_unref (caps);

  return TRUE;
}

static gboolean
gst_opus_parse_check_valid_frame (GstBaseParse * base,
    GstBaseParseFrame * frame, guint * frame_size, gint * skip)
{
  GstOpusParse *parse;
  guint8 *data;
  gsize size;
  guint32 packet_size;
  int ret = FALSE;
  int channels, bandwidth;

  parse = GST_OPUS_PARSE (base);

  data = gst_buffer_map (frame->buffer, &size, NULL, GST_MAP_READ);
  GST_DEBUG_OBJECT (parse, "Checking for frame, %u bytes in buffer", size);
  if (size < 4) {
    GST_DEBUG_OBJECT (parse, "Too small");
    goto beach;
  }
  packet_size = GST_READ_UINT32_BE (data);
  GST_DEBUG_OBJECT (parse, "Packet size: %u bytes", packet_size);
  if (packet_size > MAX_PAYLOAD_BYTES) {
    GST_DEBUG_OBJECT (parse, "Too large");
    goto beach;
  }
  if (packet_size > size - 4) {
    GST_DEBUG_OBJECT (parse, "Truncated");
    goto beach;
  }

  channels = opus_packet_get_nb_channels (data + 8);
  bandwidth = opus_packet_get_bandwidth (data + 8);
  if (channels < 0 || bandwidth < 0) {
    GST_DEBUG_OBJECT (parse, "It looked like a packet, but it is not");
    goto beach;
  }

  GST_DEBUG_OBJECT (parse, "Got Opus packet, %d bytes");

  *skip = 8;
  *frame_size = packet_size;
  ret = TRUE;

beach:
  gst_buffer_unmap (frame->buffer, data, size);
  return ret;
}
