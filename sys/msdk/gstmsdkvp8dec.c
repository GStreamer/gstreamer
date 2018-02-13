/* GStreamer Intel MSDK plugin
 * Copyright (c) 2017, Intel Corporation
 * Copyright (c) 2017, Igalia S.L.
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

#include "gstmsdkvp8dec.h"

GST_DEBUG_CATEGORY_EXTERN (gst_msdkvp8dec_debug);
#define GST_CAT_DEFAULT gst_msdkvp8dec_debug

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-vp8")
    );

#define gst_msdkvp8dec_parent_class parent_class
G_DEFINE_TYPE (GstMsdkVP8Dec, gst_msdkvp8dec, GST_TYPE_MSDKDEC);

static gboolean
gst_msdkvp8dec_configure (GstMsdkDec * decoder)
{
  GstMsdkVP8Dec *vp8dec = GST_MSDKVP8DEC (decoder);
  mfxSession session;
  mfxStatus status;
  const mfxPluginUID *uid;

  session = gst_msdk_context_get_session (decoder->context);

  uid = &MFX_PLUGINID_VP8D_HW;

  status = MFXVideoUSER_Load (session, uid, 1);
  if (status < MFX_ERR_NONE) {
    GST_ERROR_OBJECT (vp8dec, "Media SDK Plugin load failed (%s)",
        msdk_status_to_string (status));
    return FALSE;
  } else if (status > MFX_ERR_NONE) {
    GST_WARNING_OBJECT (vp8dec, "Media SDK Plugin load warning: %s",
        msdk_status_to_string (status));
  }

  decoder->param.mfx.CodecId = MFX_CODEC_VP8;
  /* Replaced with width and height rounded up to 16 */
  decoder->param.mfx.FrameInfo.Width =
      GST_ROUND_UP_16 (decoder->param.mfx.FrameInfo.CropW);
  decoder->param.mfx.FrameInfo.Height =
      GST_ROUND_UP_16 (decoder->param.mfx.FrameInfo.CropH);

  return TRUE;
}

static void
gst_msdkvp8dec_class_init (GstMsdkVP8DecClass * klass)
{
  GstElementClass *element_class;
  GstMsdkDecClass *decoder_class;

  element_class = GST_ELEMENT_CLASS (klass);
  decoder_class = GST_MSDKDEC_CLASS (klass);

  decoder_class->configure = GST_DEBUG_FUNCPTR (gst_msdkvp8dec_configure);

  gst_element_class_set_static_metadata (element_class,
      "Intel MSDK VP8 decoder",
      "Codec/Decoder/Video",
      "VP8 video decoder based on Intel Media SDK",
      "Hyunjun Ko <zzoon@igalia.com>");

  gst_element_class_add_static_pad_template (element_class, &sink_factory);
}

static void
gst_msdkvp8dec_init (GstMsdkVP8Dec * thiz)
{
}
