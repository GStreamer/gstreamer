/* GStreamer
 *  Copyright (C) <2025> V-Nova International Limited
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

#include "gstlcevch265enc.h"

#define GST_LCEVC_H265_ENC_CAPS              \
    "video/x-h265, "                         \
    "lcevc = (boolean) true, "               \
    "stream-format = (string) byte-stream, " \
    "alignment = (string) au"

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_LCEVC_H265_ENC_CAPS)
    );

struct _GstLcevcH265Enc
{
  GstLcevcEncoder parent;
};

#define gst_lcevc_h265_enc_parent_class parent_class
G_DEFINE_TYPE (GstLcevcH265Enc, gst_lcevc_h265_enc, GST_TYPE_LCEVC_ENCODER);

GST_ELEMENT_REGISTER_DEFINE (lcevch265enc, "lcevch265enc",
    GST_RANK_PRIMARY, GST_TYPE_LCEVC_H265_ENC);

static const gchar *
gst_lecevc_h265_enc_get_eil_plugin_name (GstLcevcEncoder * enc)
{
  return "x265";
}

static GstCaps *
gst_lecevc_h265_enc_get_output_caps (GstLcevcEncoder * enc)
{
  return gst_static_caps_get (&src_template.static_caps);
}

static void
gst_lcevc_h265_enc_class_init (GstLcevcH265EncClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstLcevcEncoderClass *le_class = GST_LCEVC_ENCODER_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_static_metadata (element_class,
      "H.265 LCEVC Encoder", "Codec/Encoder/Video",
      "Encoder that internally uses EIL plugins to encode LCEVC H.265 video",
      "Julian Bouzas <julian.bouzas@collabora.com>");

  le_class->get_eil_plugin_name = gst_lecevc_h265_enc_get_eil_plugin_name;
  le_class->get_output_caps = gst_lecevc_h265_enc_get_output_caps;
}

static void
gst_lcevc_h265_enc_init (GstLcevcH265Enc * self)
{
}
