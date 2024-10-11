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

#include "gstlcevch264enc.h"

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, lcevc = (boolean) true")
    );

struct _GstLcevcH264Enc
{
  GstLcevcEncoder parent;
};

#define gst_lcevc_h264_enc_parent_class parent_class
G_DEFINE_TYPE (GstLcevcH264Enc, gst_lcevc_h264_enc, GST_TYPE_LCEVC_ENCODER);

GST_ELEMENT_REGISTER_DEFINE (lcevch264enc, "lcevch264enc",
    GST_RANK_PRIMARY, GST_TYPE_LCEVC_H264_ENC);

static const gchar *
gst_lecevc_h264_enc_get_eil_plugin_name (GstLcevcEncoder * enc)
{
  return "x264";
}

static GstCaps *
gst_lecevc_h264_enc_get_output_caps (GstLcevcEncoder * enc)
{
  return gst_caps_new_simple ("video/x-h264",
      "lcevc", G_TYPE_BOOLEAN, TRUE, NULL);
}

static void
gst_lcevc_h264_enc_class_init (GstLcevcH264EncClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstLcevcEncoderClass *le_class = GST_LCEVC_ENCODER_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_static_metadata (element_class,
      "H.264 LCEVC Encoder", "Codec/Encoder/Video",
      "Encoder that internally uses EIL plugins to encode LCEVC H.264 video",
      "Julian Bouzas <julian.bouzas@collabora.com>");

  le_class->get_eil_plugin_name = gst_lecevc_h264_enc_get_eil_plugin_name;
  le_class->get_output_caps = gst_lecevc_h264_enc_get_output_caps;
}

static void
gst_lcevc_h264_enc_init (GstLcevcH264Enc * self)
{
}
