/* GStreamer Intel MSDK plugin
 * Copyright (c) 2016, Oblong Industries, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "gstmsdkmpeg2enc.h"

GST_DEBUG_CATEGORY_EXTERN (gst_msdkmpeg2enc_debug);
#define GST_CAT_DEFAULT gst_msdkmpeg2enc_debug

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, "
        "framerate = (fraction) [0/1, MAX], "
        "width = (int) [ 1, MAX ], height = (int) [ 1, MAX ], "
        "mpegversion = (int) 2 , systemstream = (bool) false, "
        "profile = (string) { high, main, simple }")
    );

#define gst_msdkmpeg2enc_parent_class parent_class
G_DEFINE_TYPE (GstMsdkMPEG2Enc, gst_msdkmpeg2enc, GST_TYPE_MSDKENC);

static gboolean
gst_msdkmpeg2enc_set_format (GstMsdkEnc * encoder)
{
  GstMsdkMPEG2Enc *thiz = GST_MSDKMPEG2ENC (encoder);
  GstCaps *template_caps;
  GstCaps *allowed_caps = NULL;

  thiz->profile = 0;

  template_caps = gst_static_pad_template_get_caps (&src_factory);
  allowed_caps = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder));

  /* If downstream has ANY caps let encoder decide profile and level */
  if (allowed_caps == template_caps) {
    GST_INFO_OBJECT (thiz,
        "downstream has ANY caps, profile/level set to auto");
  } else if (allowed_caps) {
    GstStructure *s;
    const gchar *profile;

    if (gst_caps_is_empty (allowed_caps)) {
      gst_caps_unref (allowed_caps);
      gst_caps_unref (template_caps);
      return FALSE;
    }

    allowed_caps = gst_caps_make_writable (allowed_caps);
    allowed_caps = gst_caps_fixate (allowed_caps);
    s = gst_caps_get_structure (allowed_caps, 0);

    profile = gst_structure_get_string (s, "profile");
    if (profile) {
      if (!strcmp (profile, "high")) {
        thiz->profile = MFX_PROFILE_MPEG2_HIGH;
      } else if (!strcmp (profile, "main")) {
        thiz->profile = MFX_PROFILE_MPEG2_MAIN;
      } else if (!strcmp (profile, "simple")) {
        thiz->profile = MFX_PROFILE_MPEG2_SIMPLE;
      } else {
        g_assert_not_reached ();
      }
    }

    gst_caps_unref (allowed_caps);
  }

  gst_caps_unref (template_caps);

  return TRUE;
}

static gboolean
gst_msdkmpeg2enc_configure (GstMsdkEnc * encoder)
{
  GstMsdkMPEG2Enc *thiz = GST_MSDKMPEG2ENC (encoder);

  encoder->param.mfx.CodecId = MFX_CODEC_MPEG2;
  encoder->param.mfx.CodecProfile = thiz->profile;
  encoder->param.mfx.CodecLevel = 0;

  /* Enable Extended Coding options */
  gst_msdkenc_ensure_extended_coding_options (encoder);

  return TRUE;
}

static inline const gchar *
profile_to_string (gint profile)
{
  switch (profile) {
    case MFX_PROFILE_MPEG2_HIGH:
      return "high";
    case MFX_PROFILE_MPEG2_MAIN:
      return "main";
    case MFX_PROFILE_MPEG2_SIMPLE:
      return "simple";
    default:
      break;
  }

  return NULL;
}

static GstCaps *
gst_msdkmpeg2enc_set_src_caps (GstMsdkEnc * encoder)
{
  GstCaps *caps;
  GstStructure *structure;
  const gchar *profile;

  caps = gst_caps_from_string ("video/mpeg, mpegversion=2, systemstream=false");
  structure = gst_caps_get_structure (caps, 0);

  profile = profile_to_string (encoder->param.mfx.CodecProfile);
  if (profile)
    gst_structure_set (structure, "profile", G_TYPE_STRING, profile, NULL);

  return caps;
}

static void
gst_msdkmpeg2enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMsdkMPEG2Enc *thiz = GST_MSDKMPEG2ENC (object);

  if (!gst_msdkenc_set_common_property (object, prop_id, value, pspec))
    GST_WARNING_OBJECT (thiz, "Failed to set common encode property");
}

static void
gst_msdkmpeg2enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMsdkMPEG2Enc *thiz = GST_MSDKMPEG2ENC (object);

  if (!gst_msdkenc_get_common_property (object, prop_id, value, pspec))
    GST_WARNING_OBJECT (thiz, "Failed to get common encode property");
}

static void
gst_msdkmpeg2enc_class_init (GstMsdkMPEG2EncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstMsdkEncClass *encoder_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  encoder_class = GST_MSDKENC_CLASS (klass);

  gobject_class->set_property = gst_msdkmpeg2enc_set_property;
  gobject_class->get_property = gst_msdkmpeg2enc_get_property;

  encoder_class->set_format = gst_msdkmpeg2enc_set_format;
  encoder_class->configure = gst_msdkmpeg2enc_configure;
  encoder_class->set_src_caps = gst_msdkmpeg2enc_set_src_caps;

  gst_msdkenc_install_common_properties (encoder_class);

  gst_element_class_set_static_metadata (element_class,
      "Intel MSDK MPEG2 encoder",
      "Codec/Encoder/Video",
      "MPEG2 video encoder based on Intel Media SDK",
      "Josep Torra <jtorra@oblong.com>");

  gst_element_class_add_static_pad_template (element_class, &src_factory);
}

static void
gst_msdkmpeg2enc_init (GstMsdkMPEG2Enc * thiz)
{
}
