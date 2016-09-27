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
#include <mfxplugin.h>

#include "gstmsdkh265enc.h"

GST_DEBUG_CATEGORY_EXTERN (gst_msdkh265enc_debug);
#define GST_CAT_DEFAULT gst_msdkh265enc_debug

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h265, "
        "framerate = (fraction) [0/1, MAX], "
        "width = (int) [ 1, MAX ], height = (int) [ 1, MAX ], "
        "stream-format = (string) byte-stream , alignment = (string) au , "
        "profile = (string) main")
    );

#define gst_msdkh265enc_parent_class parent_class
G_DEFINE_TYPE (GstMsdkH265Enc, gst_msdkh265enc, GST_TYPE_MSDKENC);

static gboolean
gst_msdkh265enc_set_format (GstMsdkEnc * encoder)
{
  return TRUE;
}

static gboolean
gst_msdkh265enc_configure (GstMsdkEnc * encoder)
{
  GstMsdkH265Enc *h265enc = GST_MSDKH265ENC (encoder);
  mfxSession session;
  mfxStatus status;
  const mfxPluginUID *uid;

  session = msdk_context_get_session (encoder->context);

  if (encoder->hardware)
    uid = &MFX_PLUGINID_HEVCE_HW;
  else
    uid = &MFX_PLUGINID_HEVCE_SW;

  status = MFXVideoUSER_Load (session, uid, 1);
  if (status < MFX_ERR_NONE) {
    GST_ERROR_OBJECT (h265enc, "Media SDK Plugin load failed (%s)",
        msdk_status_to_string (status));
    return FALSE;
  } else if (status > MFX_ERR_NONE) {
    GST_WARNING_OBJECT (h265enc, "Media SDK Plugin load warning: %s",
        msdk_status_to_string (status));
  }

  encoder->param.mfx.CodecId = MFX_CODEC_HEVC;
  encoder->param.mfx.CodecProfile = MFX_PROFILE_HEVC_MAIN;

  return TRUE;
}

static inline const gchar *
level_to_string (gint level)
{
  switch (level) {
    case MFX_LEVEL_HEVC_1:
      return "1";
    case MFX_LEVEL_HEVC_2:
      return "2";
    case MFX_LEVEL_HEVC_21:
      return "2.1";
    case MFX_LEVEL_HEVC_3:
      return "3";
    case MFX_LEVEL_HEVC_31:
      return "3.1";
    case MFX_LEVEL_HEVC_4:
      return "4";
    case MFX_LEVEL_HEVC_41:
      return "4.1";
    case MFX_LEVEL_HEVC_5:
      return "5";
    case MFX_LEVEL_HEVC_51:
      return "5.1";
    case MFX_LEVEL_HEVC_52:
      return "5.2";
    case MFX_LEVEL_HEVC_6:
      return "6";
    case MFX_LEVEL_HEVC_61:
      return "6.1";
    case MFX_LEVEL_HEVC_62:
      return "6.2";
    default:
      break;
  }

  return NULL;
}

static GstCaps *
gst_msdkh265enc_set_src_caps (GstMsdkEnc * encoder)
{
  GstCaps *caps;
  GstStructure *structure;
  const gchar *level;

  caps = gst_caps_new_empty_simple ("video/x-h265");
  structure = gst_caps_get_structure (caps, 0);

  gst_structure_set (structure, "stream-format", G_TYPE_STRING, "byte-stream",
      NULL);

  gst_structure_set (structure, "alignment", G_TYPE_STRING, "au", NULL);

  gst_structure_set (structure, "profile", G_TYPE_STRING, "main", NULL);

  level = level_to_string (encoder->param.mfx.CodecLevel);
  if (level)
    gst_structure_set (structure, "level", G_TYPE_STRING, level, NULL);

  return caps;
}

static void
gst_msdkh265enc_class_init (GstMsdkH265EncClass * klass)
{
  GstElementClass *element_class;
  GstMsdkEncClass *encoder_class;

  element_class = GST_ELEMENT_CLASS (klass);
  encoder_class = GST_MSDKENC_CLASS (klass);

  encoder_class->set_format = gst_msdkh265enc_set_format;
  encoder_class->configure = gst_msdkh265enc_configure;
  encoder_class->set_src_caps = gst_msdkh265enc_set_src_caps;

  gst_element_class_set_static_metadata (element_class,
      "Intel MSDK H265 encoder",
      "Codec/Encoder/Video",
      "H265 video encoder based on Intel Media SDK",
      "Josep Torra <jtorra@oblong.com>");

  gst_element_class_add_static_pad_template (element_class, &src_factory);
}

static void
gst_msdkh265enc_init (GstMsdkH265Enc * thiz)
{
}
