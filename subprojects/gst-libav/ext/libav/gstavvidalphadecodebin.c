/* GStreamer
 * Copyright (C) 2026 Dominique Leroux <dominique.p.leroux@gmail.com>
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
 * SECTION:element-avdec_vp8alphadecodebin
 * @title: Wrapper to decode VP8 with alpha using avdec_vp8
 *
 * Uses two `avdec_vp8` instances to decode the color and alpha streams from
 * VP8 video with codec alpha, then combines them into one decoded video stream.
 *
 * Since: 1.30
 */

/**
 * SECTION:element-avdec_vp9alphadecodebin
 * @title: Wrapper to decode VP9 with alpha using avdec_vp9
 *
 * Uses two `avdec_vp9` instances to decode the color and alpha streams from
 * VP9 video with codec alpha, then combines them into one decoded video stream.
 *
 * Since: 1.30
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstavvidalphadecodebin.h"

static GstStaticPadTemplate vp8_sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-vp8, codec-alpha = (boolean) true")
    );

static GstStaticPadTemplate vp9_sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-vp9, codec-alpha = (boolean) true, "
        "alignment = super-frame")
    );

struct _GstAVDecVp8AlphaDecodeBin
{
  GstAlphaDecodeBin parent;
};

struct _GstAVDecVp9AlphaDecodeBin
{
  GstAlphaDecodeBin parent;
};

G_DEFINE_TYPE (GstAVDecVp8AlphaDecodeBin, gst_avdec_vp8_alpha_decode_bin,
    GST_TYPE_ALPHA_DECODE_BIN);

static gboolean
gst_avdec_vp8_alpha_decode_bin_register (GstPlugin * plugin)
{
  GstElementFactory *factory = gst_element_factory_find ("avdec_vp8");

  if (!factory)
    return TRUE;

  gst_object_unref (factory);

  return gst_element_register (plugin, "avdec_vp8alphadecodebin",
      GST_RANK_MARGINAL + GST_ALPHA_DECODE_BIN_RANK_OFFSET,
      GST_TYPE_AVDEC_VP8_ALPHA_DECODE_BIN);
}

GST_ELEMENT_REGISTER_DEFINE_CUSTOM (avdec_vp8_alpha_decode_bin,
    gst_avdec_vp8_alpha_decode_bin_register);

static void
gst_avdec_vp8_alpha_decode_bin_class_init (GstAVDecVp8AlphaDecodeBinClass *
    klass)
{
  GstAlphaDecodeBinClass *adbin_class = (GstAlphaDecodeBinClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;

  gst_alpha_decode_bin_class_set_role_factory_name (adbin_class,
      GST_ALPHA_DECODE_BIN_ROLE_DEMUX, "codecalphademux");
  gst_alpha_decode_bin_class_set_role_factory_name (adbin_class,
      GST_ALPHA_DECODE_BIN_ROLE_COLOR_DECODER, "avdec_vp8");
  gst_alpha_decode_bin_class_set_role_factory_name (adbin_class,
      GST_ALPHA_DECODE_BIN_ROLE_ALPHA_DECODER, "avdec_vp8");
  gst_alpha_decode_bin_class_set_role_factory_name (adbin_class,
      GST_ALPHA_DECODE_BIN_ROLE_COMBINE, "alphacombine");

  gst_element_class_add_static_pad_template (element_class, &vp8_sink_template);

  gst_element_class_set_static_metadata (element_class,
      "libav VP8 Alpha Decoder", "Codec/Decoder/Video",
      "Wrapper bin to decode VP8 with alpha stream using libav.",
      "Dominique Leroux <dominique.p.leroux@gmail.com>");
}

static void
gst_avdec_vp8_alpha_decode_bin_init (GstAVDecVp8AlphaDecodeBin * self)
{
}

G_DEFINE_TYPE (GstAVDecVp9AlphaDecodeBin, gst_avdec_vp9_alpha_decode_bin,
    GST_TYPE_ALPHA_DECODE_BIN);

static gboolean
gst_avdec_vp9_alpha_decode_bin_register (GstPlugin * plugin)
{
  GstElementFactory *factory = gst_element_factory_find ("avdec_vp9");

  if (!factory)
    return TRUE;

  gst_object_unref (factory);

  return gst_element_register (plugin, "avdec_vp9alphadecodebin",
      GST_RANK_MARGINAL + GST_ALPHA_DECODE_BIN_RANK_OFFSET,
      GST_TYPE_AVDEC_VP9_ALPHA_DECODE_BIN);
}

GST_ELEMENT_REGISTER_DEFINE_CUSTOM (avdec_vp9_alpha_decode_bin,
    gst_avdec_vp9_alpha_decode_bin_register);

static void
gst_avdec_vp9_alpha_decode_bin_class_init (GstAVDecVp9AlphaDecodeBinClass *
    klass)
{
  GstAlphaDecodeBinClass *adbin_class = (GstAlphaDecodeBinClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;

  gst_alpha_decode_bin_class_set_role_factory_name (adbin_class,
      GST_ALPHA_DECODE_BIN_ROLE_DEMUX, "codecalphademux");
  gst_alpha_decode_bin_class_set_role_factory_name (adbin_class,
      GST_ALPHA_DECODE_BIN_ROLE_COLOR_DECODER, "avdec_vp9");
  gst_alpha_decode_bin_class_set_role_factory_name (adbin_class,
      GST_ALPHA_DECODE_BIN_ROLE_ALPHA_DECODER, "avdec_vp9");
  gst_alpha_decode_bin_class_set_role_factory_name (adbin_class,
      GST_ALPHA_DECODE_BIN_ROLE_COMBINE, "alphacombine");

  gst_element_class_add_static_pad_template (element_class, &vp9_sink_template);

  gst_element_class_set_static_metadata (element_class,
      "libav VP9 Alpha Decoder", "Codec/Decoder/Video",
      "Wrapper bin to decode VP9 with alpha stream using libav.",
      "Dominique Leroux <dominique.p.leroux@gmail.com>");
}

static void
gst_avdec_vp9_alpha_decode_bin_init (GstAVDecVp9AlphaDecodeBin * self)
{
}
