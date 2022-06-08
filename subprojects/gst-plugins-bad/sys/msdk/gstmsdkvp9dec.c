/* GStreamer Intel MSDK plugin
 * Copyright (c) 2018, Intel Corporation
 * All rights reserved.
 *
 * Author: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
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
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGDECE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * SECTION: element-msdkvp9dec
 * @title: msdkvp9dec
 * @short_description: Intel MSDK VP9 decoderr
 *
 * VP9 video decoder based on Intel MFX
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 filesrc location=sample.webm ! matroskademux ! msdkvp9dec ! glimagesink
 * ```
 *
 * Since: 1.16
 *
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "gstmsdkvp9dec.h"
#include "gstmsdkvideomemory.h"

GST_DEBUG_CATEGORY_EXTERN (gst_msdkvp9dec_debug);
#define GST_CAT_DEFAULT gst_msdkvp9dec_debug

#define COMMON_FORMAT "{ NV12, P010_10LE, VUYA, Y410, P012_LE, Y412_LE }"
#define SUPPORTED_VA_FORMAT "{ NV12 }"

#ifndef _WIN32
#define VA_SRC_CAPS_STR \
    "; " GST_MSDK_CAPS_MAKE_WITH_VA_FEATURE (SUPPORTED_VA_FORMAT)
#else
#define VA_SRC_CAPS_STR ""
#endif

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-vp9")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_MSDK_CAPS_STR (COMMON_FORMAT, COMMON_FORMAT)
        VA_SRC_CAPS_STR));

#define gst_msdkvp9dec_parent_class parent_class
G_DEFINE_TYPE (GstMsdkVP9Dec, gst_msdkvp9dec, GST_TYPE_MSDKDEC);

static gboolean
gst_msdkvp9dec_configure (GstMsdkDec * decoder)
{
  GstMsdkVP9Dec *vp9dec = GST_MSDKVP9DEC (decoder);
  mfxSession session;
  const mfxPluginUID *uid;

  session = gst_msdk_context_get_session (decoder->context);

  uid = &MFX_PLUGINID_VP9D_HW;

  if (!gst_msdk_load_plugin (session, uid, 1, "msdkvp9dec"))
    return FALSE;

  decoder->param.mfx.CodecId = MFX_CODEC_VP9;
  /* Replaced with width and height rounded up to 16 */
  decoder->param.mfx.FrameInfo.Width =
      GST_ROUND_UP_16 (decoder->param.mfx.FrameInfo.CropW);
  decoder->param.mfx.FrameInfo.Height =
      GST_ROUND_UP_16 (decoder->param.mfx.FrameInfo.CropH);

  decoder->force_reset_on_res_change = FALSE;

  /* This is a deprecated attribute in msdk-2017 version, but some
   * customers still using this for low-latency streaming of non-b-frame
   * encoded streams */
  decoder->param.mfx.DecodedOrder = vp9dec->output_order;
  return TRUE;
}

static void
gst_msdkdec_vp9_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMsdkVP9Dec *thiz = GST_MSDKVP9DEC (object);
  GstState state;

  GST_OBJECT_LOCK (thiz);
  state = GST_STATE (thiz);

  if (!gst_msdkdec_prop_check_state (state, pspec)) {
    GST_WARNING_OBJECT (thiz, "setting property in wrong state");
    GST_OBJECT_UNLOCK (thiz);
    return;
  }
  switch (prop_id) {
    case GST_MSDKDEC_PROP_OUTPUT_ORDER:
      thiz->output_order = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (thiz);
  return;
}

static void
gst_msdkdec_vp9_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMsdkVP9Dec *thiz = GST_MSDKVP9DEC (object);

  GST_OBJECT_LOCK (thiz);
  switch (prop_id) {
    case GST_MSDKDEC_PROP_OUTPUT_ORDER:
      g_value_set_enum (value, thiz->output_order);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (thiz);
}

static gboolean
gst_msdkvp9dec_preinit_decoder (GstMsdkDec * decoder)
{
  decoder->param.mfx.FrameInfo.Width =
      GST_ROUND_UP_16 (decoder->param.mfx.FrameInfo.Width);
  decoder->param.mfx.FrameInfo.Height =
      GST_ROUND_UP_16 (decoder->param.mfx.FrameInfo.Height);

  decoder->param.mfx.FrameInfo.PicStruct =
      decoder->param.mfx.FrameInfo.PicStruct ? decoder->param.mfx.
      FrameInfo.PicStruct : MFX_PICSTRUCT_PROGRESSIVE;

  return TRUE;
}

static void
gst_msdkvp9dec_class_init (GstMsdkVP9DecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstMsdkDecClass *decoder_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  decoder_class = GST_MSDKDEC_CLASS (klass);

  gobject_class->set_property = gst_msdkdec_vp9_set_property;
  gobject_class->get_property = gst_msdkdec_vp9_get_property;

  decoder_class->configure = GST_DEBUG_FUNCPTR (gst_msdkvp9dec_configure);
  decoder_class->preinit_decoder =
      GST_DEBUG_FUNCPTR (gst_msdkvp9dec_preinit_decoder);

  gst_element_class_set_static_metadata (element_class,
      "Intel MSDK VP9 decoder",
      "Codec/Decoder/Video/Hardware",
      "VP9 video decoder based on " MFX_API_SDK,
      "Sreerenj Balachandran <sreerenj.balachandran@intel.com>");

  gst_msdkdec_prop_install_output_oder_property (gobject_class);

  gst_element_class_add_static_pad_template (element_class, &sink_factory);
  gst_element_class_add_static_pad_template (element_class, &src_factory);
}

static void
gst_msdkvp9dec_init (GstMsdkVP9Dec * thiz)
{
  thiz->output_order = PROP_OUTPUT_ORDER_DEFAULT;
}
