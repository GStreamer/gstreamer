/* GStreamer Intel MSDK plugin
 * Copyright (c) 2016, Oblong Industries, Inc.
 * Copyright (c) 2018, Intel Corporation
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

/**
 * SECTION: plugin-msdk
 *
 * Since: 1.12
 *
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "gstmsdkh264dec.h"
#include "gstmsdkh264enc.h"
#include "gstmsdkh265dec.h"
#include "gstmsdkh265enc.h"
#include "gstmsdkmjpegdec.h"
#include "gstmsdkmjpegenc.h"
#include "gstmsdkmpeg2dec.h"
#include "gstmsdkmpeg2enc.h"
#include "gstmsdkvp8dec.h"
#ifdef USE_MSDK_VP9_ENC
#include "gstmsdkvp9enc.h"
#endif
#include "gstmsdkvc1dec.h"
#ifdef USE_MSDK_VP9_DEC
#include "gstmsdkvp9dec.h"
#endif
#ifdef USE_MSDK_AV1_DEC
#include "gstmsdkav1dec.h"
#endif
#ifdef USE_MSDK_AV1_ENC
#include "gstmsdkav1enc.h"
#endif
#include "gstmsdkvpp.h"

GST_DEBUG_CATEGORY (gst_msdk_debug);
GST_DEBUG_CATEGORY (gst_msdkdec_debug);
GST_DEBUG_CATEGORY (gst_msdkenc_debug);
GST_DEBUG_CATEGORY (gst_msdkvpp_debug);
GST_DEBUG_CATEGORY (gst_msdkh264dec_debug);
GST_DEBUG_CATEGORY (gst_msdkh264enc_debug);
GST_DEBUG_CATEGORY (gst_msdkh265dec_debug);
GST_DEBUG_CATEGORY (gst_msdkh265enc_debug);
GST_DEBUG_CATEGORY (gst_msdkmjpegdec_debug);
GST_DEBUG_CATEGORY (gst_msdkmjpegenc_debug);
GST_DEBUG_CATEGORY (gst_msdkmpeg2enc_debug);
GST_DEBUG_CATEGORY (gst_msdkmpeg2dec_debug);
GST_DEBUG_CATEGORY (gst_msdkvp8dec_debug);
GST_DEBUG_CATEGORY (gst_msdkvc1dec_debug);
GST_DEBUG_CATEGORY (gst_msdkvp9dec_debug);
GST_DEBUG_CATEGORY (gst_msdkvp9enc_debug);
GST_DEBUG_CATEGORY (gst_msdkav1dec_debug);
GST_DEBUG_CATEGORY (gst_msdkav1enc_debug);

static void
plugin_add_dependencies (GstPlugin * plugin)
{
#ifndef _WIN32
  const gchar *env_vars[] = { "LIBVA_DRIVER_NAME", NULL };
  const gchar *kernel_paths[] = { "/dev/dri", NULL };
  const gchar *kernel_names[] = { "card", "render", NULL };

  /* features get updated upon changes in /dev/dri/card* */
  gst_plugin_add_dependency (plugin, NULL, kernel_paths, kernel_names,
      GST_PLUGIN_DEPENDENCY_FLAG_FILE_NAME_IS_PREFIX);

  /* features get updated upon changes in VA environment variables */
  gst_plugin_add_dependency (plugin, env_vars, NULL, NULL,
      GST_PLUGIN_DEPENDENCY_FLAG_NONE);

  /* features get updated upon changes in default VA drivers
   * directory */
  gst_plugin_add_dependency_simple (plugin, "LIBVA_DRIVERS_PATH",
      VA_DRIVERS_PATH, "_drv_video.so",
      GST_PLUGIN_DEPENDENCY_FLAG_FILE_NAME_IS_SUFFIX |
      GST_PLUGIN_DEPENDENCY_FLAG_PATHS_ARE_DEFAULT_ONLY);
#endif
}

static gboolean
_register_encoder (GstPlugin * plugin, guint codec_id)
{
  gboolean ret = TRUE;

  switch (codec_id) {
    case MFX_CODEC_AVC:
      ret = gst_element_register (plugin, "msdkh264enc", GST_RANK_NONE,
          GST_TYPE_MSDKH264ENC);
      break;
    case MFX_CODEC_HEVC:
      ret = gst_element_register (plugin, "msdkh265enc", GST_RANK_NONE,
          GST_TYPE_MSDKH265ENC);
      break;
    case MFX_CODEC_MPEG2:
      ret = gst_element_register (plugin, "msdkmpeg2enc", GST_RANK_NONE,
          GST_TYPE_MSDKMPEG2ENC);
      break;
#ifdef USE_MSDK_VP9_ENC
    case MFX_CODEC_VP9:
      ret = gst_element_register (plugin, "msdkvp9enc", GST_RANK_NONE,
          GST_TYPE_MSDKVP9ENC);
      break;
#endif
#ifdef USE_MSDK_AV1_ENC
    case MFX_CODEC_AV1:
      ret = gst_element_register (plugin, "msdkav1enc", GST_RANK_NONE,
          GST_TYPE_MSDKAV1ENC);
      break;
#endif
    case MFX_CODEC_JPEG:
      ret = gst_element_register (plugin, "msdkmjpegenc", GST_RANK_NONE,
          GST_TYPE_MSDKMJPEGENC);
      break;
    default:
      ret = FALSE;
      break;
  }

  return ret;
}

static gboolean
_register_decoder (GstPlugin * plugin, guint codec_id)
{
  gboolean ret = TRUE;

  switch (codec_id) {
    case MFX_CODEC_AVC:
      ret = gst_element_register (plugin, "msdkh264dec", GST_RANK_NONE,
          GST_TYPE_MSDKH264DEC);
      break;
    case MFX_CODEC_HEVC:
      ret = gst_element_register (plugin, "msdkh265dec", GST_RANK_NONE,
          GST_TYPE_MSDKH265DEC);
      break;
    case MFX_CODEC_MPEG2:
      ret = gst_element_register (plugin, "msdkmpeg2dec", GST_RANK_NONE,
          GST_TYPE_MSDKMPEG2DEC);
      break;
    case MFX_CODEC_VP8:
      ret = gst_element_register (plugin, "msdkvp8dec", GST_RANK_NONE,
          GST_TYPE_MSDKVP8DEC);
      break;
#ifdef USE_MSDK_VP9_DEC
    case MFX_CODEC_VP9:
      ret = gst_element_register (plugin, "msdkvp9dec", GST_RANK_NONE,
          GST_TYPE_MSDKVP9DEC);
      break;
#endif
#ifdef USE_MSDK_AV1_DEC
    case MFX_CODEC_AV1:
      ret = gst_element_register (plugin, "msdkav1dec", GST_RANK_NONE,
          GST_TYPE_MSDKAV1DEC);
      break;
#endif
    case MFX_CODEC_JPEG:
      ret = gst_element_register (plugin, "msdkmjpegdec", GST_RANK_NONE,
          GST_TYPE_MSDKMJPEGDEC);
      break;
    case MFX_CODEC_VC1:
      ret = gst_element_register (plugin, "msdkvc1dec", GST_RANK_NONE,
          GST_TYPE_MSDKVC1DEC);
      break;
    default:
      ret = FALSE;
      break;
  }

  return ret;
}

#if (MFX_VERSION >= 2000)

static void
_register_encoders (GstPlugin * plugin,
    GstMsdkContext * context, mfxEncoderDescription * enc_desc)
{
  for (guint c = 0; c < enc_desc->NumCodecs; c++) {
    if (!_register_encoder (plugin, enc_desc->Codecs[c].CodecID)) {
      GST_WARNING ("Failed to register %" GST_FOURCC_FORMAT " ENC",
          GST_FOURCC_ARGS (enc_desc->Codecs[c].CodecID));
    }
  }
}

static void
_register_decoders (GstPlugin * plugin,
    GstMsdkContext * context, mfxDecoderDescription * dec_desc)
{
  for (guint c = 0; c < dec_desc->NumCodecs; c++) {
    if (!_register_decoder (plugin, dec_desc->Codecs[c].CodecID)) {
      GST_WARNING ("Failed to register %" GST_FOURCC_FORMAT " DEC",
          GST_FOURCC_ARGS (dec_desc->Codecs[c].CodecID));
    }
  }
}

static void
_register_vpp (GstPlugin * plugin,
    GstMsdkContext * context, mfxVPPDescription * vpp_desc)
{
  gst_element_register (plugin, "msdkvpp", GST_RANK_NONE, GST_TYPE_MSDKVPP);
}

#else

static const guint enc_codecs[] = {
  MFX_CODEC_AVC,
  MFX_CODEC_HEVC,
  MFX_CODEC_MPEG2,
#ifdef USE_MSDK_VP9_ENC
  MFX_CODEC_VP9,
#endif
#ifdef USE_MSDK_AV1_ENC
  MFX_CODEC_AV1,
#endif
  MFX_CODEC_JPEG
};

static const guint dec_codecs[] = {
  MFX_CODEC_AVC,
  MFX_CODEC_HEVC,
  MFX_CODEC_MPEG2,
  MFX_CODEC_VP8,
#ifdef USE_MSDK_VP9_DEC
  MFX_CODEC_VP9,
#endif
#ifdef USE_MSDK_AV1_DEC
  MFX_CODEC_AV1,
#endif
  MFX_CODEC_JPEG,
  MFX_CODEC_VC1
};

static void
_register_encoders (GstPlugin * plugin, GstMsdkContext * context)
{
  for (guint c = 0; c < G_N_ELEMENTS (enc_codecs); c++) {
    if (!_register_encoder (plugin, enc_codecs[c])) {
      GST_WARNING ("Failed to register %" GST_FOURCC_FORMAT " ENC",
          GST_FOURCC_ARGS (enc_codecs[c]));
    }
  }
}

static void
_register_decoders (GstPlugin * plugin, GstMsdkContext * context)
{
  for (guint c = 0; c < G_N_ELEMENTS (dec_codecs); c++) {
    if (!_register_decoder (plugin, dec_codecs[c])) {
      GST_WARNING ("Failed to register %" GST_FOURCC_FORMAT " DEC",
          GST_FOURCC_ARGS (dec_codecs[c]));
    }
  }
}

static void
_register_vpp (GstPlugin * plugin, GstMsdkContext * context)
{
  gst_element_register (plugin, "msdkvpp", GST_RANK_NONE, GST_TYPE_MSDKVPP);
}

#endif

static gboolean
plugin_init (GstPlugin * plugin)
{
  GstMsdkContext *context;

  GST_DEBUG_CATEGORY_INIT (gst_msdk_debug, "msdk", 0, "msdk");
  GST_DEBUG_CATEGORY_INIT (gst_msdkdec_debug, "msdkdec", 0, "msdkdec");
  GST_DEBUG_CATEGORY_INIT (gst_msdkenc_debug, "msdkenc", 0, "msdkenc");
  GST_DEBUG_CATEGORY_INIT (gst_msdkh264dec_debug, "msdkh264dec", 0,
      "msdkh264dec");
  GST_DEBUG_CATEGORY_INIT (gst_msdkh264enc_debug, "msdkh264enc", 0,
      "msdkh264enc");
  GST_DEBUG_CATEGORY_INIT (gst_msdkvpp_debug, "msdkvpp", 0, "msdkvpp");
  GST_DEBUG_CATEGORY_INIT (gst_msdkh265dec_debug, "msdkh265dec", 0,
      "msdkh265dec");
  GST_DEBUG_CATEGORY_INIT (gst_msdkh265enc_debug, "msdkh265enc", 0,
      "msdkh265enc");
  GST_DEBUG_CATEGORY_INIT (gst_msdkmjpegdec_debug, "msdkmjpegdec", 0,
      "msdkmjpegdec");
  GST_DEBUG_CATEGORY_INIT (gst_msdkmjpegenc_debug, "msdkmjpegenc", 0,
      "msdkmjpegenc");
  GST_DEBUG_CATEGORY_INIT (gst_msdkmpeg2enc_debug, "msdkmpeg2enc", 0,
      "msdkmpeg2enc");
  GST_DEBUG_CATEGORY_INIT (gst_msdkmpeg2dec_debug, "msdkmpeg2dec", 0,
      "msdkmpeg2dec");
  GST_DEBUG_CATEGORY_INIT (gst_msdkvp8dec_debug, "msdkvp8dec", 0, "msdkvp8dec");
  GST_DEBUG_CATEGORY_INIT (gst_msdkvc1dec_debug, "msdkvc1dec", 0, "msdkvc1dec");
  GST_DEBUG_CATEGORY_INIT (gst_msdkvp9dec_debug, "msdkvp9dec", 0, "msdkvp9dec");
  GST_DEBUG_CATEGORY_INIT (gst_msdkvp9enc_debug, "msdkvp9enc", 0, "msdkvp9enc");
  GST_DEBUG_CATEGORY_INIT (gst_msdkav1dec_debug, "msdkav1dec", 0, "msdkav1dec");
  GST_DEBUG_CATEGORY_INIT (gst_msdkav1enc_debug, "msdkav1enc", 0, "msdkav1enc");

  plugin_add_dependencies (plugin);

  context = gst_msdk_context_new (TRUE);
  if (!context)
    return TRUE;                /* return TRUE to avoid getting blacklisted */

#if (MFX_VERSION >= 2000)
  mfxImplDescription *desc = (mfxImplDescription *)
      msdk_get_impl_description (gst_msdk_context_get_loader (context),
      gst_msdk_context_get_impl_idx (context));

  if (desc) {
    _register_encoders (plugin, context, &desc->Enc);
    _register_decoders (plugin, context, &desc->Dec);
    _register_vpp (plugin, context, &desc->VPP);

    msdk_release_impl_description (gst_msdk_context_get_loader (context), desc);
  }
#else
  _register_encoders (plugin, context);
  _register_decoders (plugin, context);
  _register_vpp (plugin, context);
#endif

  gst_object_unref (context);

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    msdk,
    "MFX API (" MFX_API_SDK ") based elements",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
