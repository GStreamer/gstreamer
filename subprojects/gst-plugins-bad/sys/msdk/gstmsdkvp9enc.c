/*
 * GStreamer Intel MSDK plugin
 * Copyright (c) 2019 Intel Corporation. All rights reserved.
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

/**
 * SECTION: element-msdkvp9enc
 * @title: msdkvp9enc
 * @short_description: Intel MSDK VP9 encoder
 *
 * VP9 video encoder based on Intel MFX
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 videotestsrc num-buffers=90 ! msdkvp9enc ! matroskamux ! filesink location=output.webm
 * ```
 *
 * Since: 1.18
 *
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/allocators/gstdmabuf.h>

#include "gstmsdkvp9enc.h"

GST_DEBUG_CATEGORY_EXTERN (gst_msdkvp9enc_debug);
#define GST_CAT_DEFAULT gst_msdkvp9enc_debug

#define RAW_FORMATS "NV12, I420, YV12, YUY2, UYVY, BGRA, P010_10LE, VUYA"
#define PROFILES    "0, 1, 2"

#if (MFX_VERSION >= 1027)
#define COMMON_FORMAT "{ " RAW_FORMATS ", Y410 }"
#define SRC_PROFILES  "{ " PROFILES ", 3 }"
#else
#define COMMON_FORMAT "{ " RAW_FORMATS " }"
#define SRC_PROFILES  "{ " PROFILES " }"
#endif

#ifdef _WIN32
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_MSDK_CAPS_STR (COMMON_FORMAT,
            "{ NV12, P010_10LE }") "; "
        GST_MSDK_CAPS_MAKE_WITH_D3D11_FEATURE ("{ NV12, P010_10LE }")));
#else
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_MSDK_CAPS_STR (COMMON_FORMAT,
            "{ NV12, P010_10LE }") "; "
        GST_MSDK_CAPS_MAKE_WITH_VA_FEATURE ("NV12")));
#endif

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-vp9, "
        "framerate = (fraction) [0/1, MAX], "
        "width = (int) [ 1, MAX ], height = (int) [ 1, MAX ], "
        "profile = (string) " SRC_PROFILES)
    );

#define gst_msdkvp9enc_parent_class parent_class
G_DEFINE_TYPE (GstMsdkVP9Enc, gst_msdkvp9enc, GST_TYPE_MSDKENC);

static gboolean
gst_msdkvp9enc_set_format (GstMsdkEnc * encoder)
{
  GstMsdkVP9Enc *thiz = GST_MSDKVP9ENC (encoder);
  GstCaps *template_caps;
  GstCaps *allowed_caps = NULL;

  thiz->profile = MFX_PROFILE_VP9_0;
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
      if (!strcmp (profile, "3")) {
        thiz->profile = MFX_PROFILE_VP9_3;
      } else if (!strcmp (profile, "2")) {
        thiz->profile = MFX_PROFILE_VP9_2;
      } else if (!strcmp (profile, "1")) {
        thiz->profile = MFX_PROFILE_VP9_1;
      } else if (!strcmp (profile, "0")) {
        thiz->profile = MFX_PROFILE_VP9_0;
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
gst_msdkvp9enc_configure (GstMsdkEnc * encoder)
{
  GstMsdkVP9Enc *vp9enc = GST_MSDKVP9ENC (encoder);
  mfxSession session;

  if (encoder->hardware) {
    session = gst_msdk_context_get_session (encoder->context);

    if (!gst_msdk_load_plugin (session, &MFX_PLUGINID_VP9E_HW, 1, "msdkvp9enc"))
      return FALSE;
  }

  encoder->num_extra_frames = encoder->async_depth - 1;
  encoder->param.mfx.CodecId = MFX_CODEC_VP9;
  encoder->param.mfx.CodecLevel = 0;

  switch (encoder->param.mfx.FrameInfo.FourCC) {
#if (MFX_VERSION >= 1027)
    case MFX_FOURCC_Y410:
      encoder->param.mfx.CodecProfile = MFX_PROFILE_VP9_3;
      break;
#endif

    case MFX_FOURCC_P010:
      encoder->param.mfx.CodecProfile = MFX_PROFILE_VP9_2;
      break;

    case MFX_FOURCC_AYUV:
      encoder->param.mfx.CodecProfile = MFX_PROFILE_VP9_1;
      break;

    default:
      encoder->param.mfx.CodecProfile = MFX_PROFILE_VP9_0;
      break;
  }

  /* As the frame width and height is rounded up to 128 and 32 since commit 8daac1c,
   * so the width, height for initialization should be rounded up to 128 and 32
   * too because VP9 encoder in MSDK will do some check on width and height.
   */
  encoder->param.mfx.FrameInfo.Width =
      GST_ROUND_UP_128 (encoder->param.mfx.FrameInfo.CropW);
  encoder->param.mfx.FrameInfo.Height =
      GST_ROUND_UP_32 (encoder->param.mfx.FrameInfo.CropH);

  /* Always turn on this flag for VP9 */
  encoder->param.mfx.LowPower = MFX_CODINGOPTION_ON;

  /* Enable Extended coding options */
  gst_msdkenc_ensure_extended_coding_options (encoder);

  memset (&vp9enc->ext_vp9, 0, sizeof (vp9enc->ext_vp9));
  vp9enc->ext_vp9.Header.BufferId = MFX_EXTBUFF_VP9_PARAM;
  vp9enc->ext_vp9.Header.BufferSz = sizeof (vp9enc->ext_vp9);
  vp9enc->ext_vp9.WriteIVFHeaders = MFX_CODINGOPTION_OFF;

  gst_msdkenc_add_extra_param (encoder, (mfxExtBuffer *) & vp9enc->ext_vp9);

  return TRUE;
}

static inline const gchar *
profile_to_string (gint profile)
{
  switch (profile) {
    case MFX_PROFILE_VP9_3:
      return "3";
    case MFX_PROFILE_VP9_2:
      return "2";
    case MFX_PROFILE_VP9_1:
      return "1";
    case MFX_PROFILE_VP9_0:
      return "0";
    default:
      break;
  }

  return NULL;
}

static GstCaps *
gst_msdkvp9enc_set_src_caps (GstMsdkEnc * encoder)
{
  GstCaps *caps;
  GstStructure *structure;
  const gchar *profile;

  caps = gst_caps_new_empty_simple ("video/x-vp9");
  structure = gst_caps_get_structure (caps, 0);

  profile = profile_to_string (encoder->param.mfx.CodecProfile);
  if (profile)
    gst_structure_set (structure, "profile", G_TYPE_STRING, profile, NULL);

  return caps;
}

static void
gst_msdkvp9enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMsdkVP9Enc *thiz = GST_MSDKVP9ENC (object);

  if (!gst_msdkenc_set_common_property (object, prop_id, value, pspec))
    GST_WARNING_OBJECT (thiz, "Failed to set common encode property");
}

static void
gst_msdkvp9enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMsdkVP9Enc *thiz = GST_MSDKVP9ENC (object);

  if (!gst_msdkenc_get_common_property (object, prop_id, value, pspec))
    GST_WARNING_OBJECT (thiz, "Failed to get common encode property");
}

static void
gst_msdkvp9enc_class_init (GstMsdkVP9EncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstMsdkEncClass *encoder_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  encoder_class = GST_MSDKENC_CLASS (klass);

  gobject_class->set_property = gst_msdkvp9enc_set_property;
  gobject_class->get_property = gst_msdkvp9enc_get_property;

  encoder_class->set_format = gst_msdkvp9enc_set_format;
  encoder_class->configure = gst_msdkvp9enc_configure;
  encoder_class->set_src_caps = gst_msdkvp9enc_set_src_caps;
  encoder_class->qp_max = 255;
  encoder_class->qp_min = 0;

  gst_msdkenc_install_common_properties (encoder_class);

  gst_element_class_set_static_metadata (element_class,
      "Intel MSDK VP9 encoder",
      "Codec/Encoder/Video/Hardware",
      "VP9 video encoder based on " MFX_API_SDK,
      "Haihao Xiang <haihao.xiang@intel.com>");

  gst_element_class_add_static_pad_template (element_class, &sink_factory);
  gst_element_class_add_static_pad_template (element_class, &src_factory);
}

static void
gst_msdkvp9enc_init (GstMsdkVP9Enc * thiz)
{
}
