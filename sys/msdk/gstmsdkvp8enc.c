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

#ifdef HAVE_LIBMFX
#  include <mfx/mfxplugin.h>
#  include <mfx/mfxvp8.h>
#else
#  include "mfxplugin.h"
#  include "mfxvp8.h"
#endif

#include "gstmsdkvp8enc.h"

GST_DEBUG_CATEGORY_EXTERN (gst_msdkvp8enc_debug);
#define GST_CAT_DEFAULT gst_msdkvp8enc_debug

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-vp8, "
        "framerate = (fraction) [0/1, MAX], "
        "width = (int) [ 1, MAX ], height = (int) [ 1, MAX ], "
        "profile = (string) { 0, 1, 2, 3 }")
    );

#define gst_msdkvp8enc_parent_class parent_class
G_DEFINE_TYPE (GstMsdkVP8Enc, gst_msdkvp8enc, GST_TYPE_MSDKENC);

static gboolean
gst_msdkvp8enc_set_format (GstMsdkEnc * encoder)
{
  GstMsdkVP8Enc *thiz = GST_MSDKVP8ENC (encoder);
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
      if (!strcmp (profile, "3")) {
        thiz->profile = MFX_PROFILE_VP8_3;
      } else if (!strcmp (profile, "2")) {
        thiz->profile = MFX_PROFILE_VP8_2;
      } else if (!strcmp (profile, "1")) {
        thiz->profile = MFX_PROFILE_VP8_1;
      } else if (!strcmp (profile, "0")) {
        thiz->profile = MFX_PROFILE_VP8_0;
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
gst_msdkvp8enc_configure (GstMsdkEnc * encoder)
{
  GstMsdkVP8Enc *thiz = GST_MSDKVP8ENC (encoder);
  mfxSession session;
  mfxStatus status;

  if (encoder->hardware) {
    session = gst_msdk_context_get_session (encoder->context);
    status = MFXVideoUSER_Load (session, &MFX_PLUGINID_VP8E_HW, 1);
    if (status < MFX_ERR_NONE) {
      GST_ERROR_OBJECT (thiz, "Media SDK Plugin load failed (%s)",
          msdk_status_to_string (status));
      return FALSE;
    } else if (status > MFX_ERR_NONE) {
      GST_WARNING_OBJECT (thiz, "Media SDK Plugin load warning: %s",
          msdk_status_to_string (status));
    }
  }

  encoder->param.mfx.CodecId = MFX_CODEC_VP8;
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
    case MFX_PROFILE_VP8_3:
      return "3";
    case MFX_PROFILE_VP8_2:
      return "2";
    case MFX_PROFILE_VP8_1:
      return "1";
    case MFX_PROFILE_VP8_0:
      return "0";
    default:
      break;
  }

  return NULL;
}

static GstCaps *
gst_msdkvp8enc_set_src_caps (GstMsdkEnc * encoder)
{
  GstCaps *caps;
  GstStructure *structure;
  const gchar *profile;

  caps = gst_caps_new_empty_simple ("video/x-vp8");
  structure = gst_caps_get_structure (caps, 0);

  profile = profile_to_string (encoder->param.mfx.CodecProfile);
  if (profile)
    gst_structure_set (structure, "profile", G_TYPE_STRING, profile, NULL);

  return caps;
}

static void
gst_msdkvp8enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMsdkVP8Enc *thiz = GST_MSDKVP8ENC (object);

  if (!gst_msdkenc_set_common_property (object, prop_id, value, pspec))
    GST_WARNING_OBJECT (thiz, "Failed to set common encode property");
}

static void
gst_msdkvp8enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMsdkVP8Enc *thiz = GST_MSDKVP8ENC (object);

  if (!gst_msdkenc_get_common_property (object, prop_id, value, pspec))
    GST_WARNING_OBJECT (thiz, "Failed to get common encode property");
}

static void
gst_msdkvp8enc_class_init (GstMsdkVP8EncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstMsdkEncClass *encoder_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  encoder_class = GST_MSDKENC_CLASS (klass);

  gobject_class->set_property = gst_msdkvp8enc_set_property;
  gobject_class->get_property = gst_msdkvp8enc_get_property;

  encoder_class->set_format = gst_msdkvp8enc_set_format;
  encoder_class->configure = gst_msdkvp8enc_configure;
  encoder_class->set_src_caps = gst_msdkvp8enc_set_src_caps;

  gst_msdkenc_install_common_properties (encoder_class);

  gst_element_class_set_static_metadata (element_class,
      "Intel MSDK VP8 encoder",
      "Codec/Encoder/Video",
      "VP8 video encoder based on Intel Media SDK",
      "Josep Torra <jtorra@oblong.com>");

  gst_element_class_add_static_pad_template (element_class, &src_factory);
}

static void
gst_msdkvp8enc_init (GstMsdkVP8Enc * thiz)
{
}
