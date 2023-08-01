/* GStreamer
 *  Copyright (C) <2024> V-Nova International Limited
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
#include "config.h"
#endif

#include "gstlcevch264decodebin.h"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, lcevc = (boolean) true")
    );

struct _GstLcevcH264DecodeBin
{
  GstLcevcDecodeBin parent;
};

#define gst_lcevc_h264_decode_bin_parent_class parent_class
G_DEFINE_TYPE (GstLcevcH264DecodeBin, gst_lcevc_h264_decode_bin,
    GST_TYPE_LCEVC_DECODE_BIN);

GST_ELEMENT_REGISTER_DEFINE (lcevch264decodebin, "lcevch264decodebin",
    GST_RANK_PRIMARY + GST_LCEVC_DECODE_BIN_RANK_OFFSET,
    GST_TYPE_LCEVC_H264_DECODE_BIN);

static GstCaps *
gst_lcevc_h264_decode_bin_get_base_decoder_sink_caps (GstLcevcDecodeBin * base)
{
  return gst_caps_new_simple ("video/x-h264",
      "lcevc", G_TYPE_BOOLEAN, FALSE, NULL);
}

static void
gst_lcevc_h264_decode_bin_class_init (GstLcevcH264DecodeBinClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstLcevcDecodeBinClass *ldb_class = GST_LCEVC_DECODE_BIN_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class, &sink_template);

  gst_element_class_set_static_metadata (element_class,
      "H264 Lcevc Decode Bin", "Codec/Decoder/Video",
      "Wrapper bin to decode H264 with LCEVC data.",
      "Julian Bouzas <julian.bouzas@collabora.com>");

  ldb_class->get_base_decoder_sink_caps =
      gst_lcevc_h264_decode_bin_get_base_decoder_sink_caps;
}

static void
gst_lcevc_h264_decode_bin_init (GstLcevcH264DecodeBin * self)
{
}
