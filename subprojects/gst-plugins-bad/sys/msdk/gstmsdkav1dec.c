/* GStreamer Intel MSDK plugin
 * Copyright (c) 2020, Intel Corporation
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
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGDECE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

 /**
 * SECTION: element-msdkav1dec
 * @title: msdkav1dec
 * @short_description: Intel MSDK AV1 decoder
 *
 * AV1 video decoder based on Intel MFX
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 filesrc location=sample.ivf ! ivfparse ! msdkav1dec ! glimagesink
 * ```
 *
 * Since: 1.20
 *
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "gstmsdkav1dec.h"
#include "gstmsdkvideomemory.h"

GST_DEBUG_CATEGORY_EXTERN (gst_msdkav1dec_debug);
#define GST_CAT_DEFAULT gst_msdkav1dec_debug

#define COMMON_FORMAT "{ NV12, P010_10LE, VUYA, Y410 }"

#ifndef _WIN32
#define VA_SRC_CAPS_STR \
    ";" GST_MSDK_CAPS_MAKE_WITH_VA_FEATURE ("{ NV12 }")
#else
#define VA_SRC_CAPS_STR ""
#endif

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-av1")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_MSDK_CAPS_STR (COMMON_FORMAT, COMMON_FORMAT)
        VA_SRC_CAPS_STR));

#define gst_msdkav1dec_parent_class parent_class
G_DEFINE_TYPE (GstMsdkAV1Dec, gst_msdkav1dec, GST_TYPE_MSDKDEC);

static gboolean
gst_msdkav1dec_configure (GstMsdkDec * decoder)
{
  decoder->param.mfx.CodecId = MFX_CODEC_AV1;
  /* Replaced with width and height rounded up to 16 */
  decoder->param.mfx.FrameInfo.Width =
      GST_ROUND_UP_16 (decoder->param.mfx.FrameInfo.CropW);
  decoder->param.mfx.FrameInfo.Height =
      GST_ROUND_UP_16 (decoder->param.mfx.FrameInfo.CropH);

  decoder->force_reset_on_res_change = FALSE;

  return TRUE;
}

static gboolean
gst_msdkav1dec_preinit_decoder (GstMsdkDec * decoder)
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
gst_msdkav1dec_class_init (GstMsdkAV1DecClass * klass)
{
  GstElementClass *element_class;
  GstMsdkDecClass *decoder_class;

  element_class = GST_ELEMENT_CLASS (klass);
  decoder_class = GST_MSDKDEC_CLASS (klass);

  decoder_class->configure = GST_DEBUG_FUNCPTR (gst_msdkav1dec_configure);
  decoder_class->preinit_decoder =
      GST_DEBUG_FUNCPTR (gst_msdkav1dec_preinit_decoder);

  gst_element_class_set_static_metadata (element_class,
      "Intel MSDK AV1 decoder",
      "Codec/Decoder/Video/Hardware",
      "AV1 video decoder based on " MFX_API_SDK,
      "Haihao Xiang <haihao.xiang@intel.com>");

  gst_element_class_add_static_pad_template (element_class, &sink_factory);
  gst_element_class_add_static_pad_template (element_class, &src_factory);
}

static void
gst_msdkav1dec_init (GstMsdkAV1Dec * thiz)
{
}
