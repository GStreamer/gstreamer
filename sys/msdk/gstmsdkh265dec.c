/* GStreamer Intel MSDK plugin
 * Copyright (c) 2016, Intel Corporation
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
#include <mfxplugin.h>

#include "gstmsdkh265dec.h"

GST_DEBUG_CATEGORY_EXTERN (gst_msdkh265dec_debug);
#define GST_CAT_DEFAULT gst_msdkh265dec_debug

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h265, "
        "framerate = (fraction) [0/1, MAX], "
        "width = (int) [ 1, MAX ], height = (int) [ 1, MAX ], "
        "stream-format = (string) byte-stream , alignment = (string) au , "
        "profile = (string) main ")
    );

#define gst_msdkh265dec_parent_class parent_class
G_DEFINE_TYPE (GstMsdkH265Dec, gst_msdkh265dec, GST_TYPE_MSDKDEC);

static gboolean
gst_msdkh265dec_configure (GstMsdkDec * decoder)
{
  GstMsdkH265Dec *h265dec = GST_MSDKH265DEC (decoder);
  mfxSession session;
  mfxStatus status;
  const mfxPluginUID *uid;

  session = msdk_context_get_session (decoder->context);

  if (decoder->hardware)
    uid = &MFX_PLUGINID_HEVCD_HW;
  else
    uid = &MFX_PLUGINID_HEVCD_SW;

  status = MFXVideoUSER_Load (session, uid, 1);
  if (status < MFX_ERR_NONE) {
    GST_ERROR_OBJECT (h265dec, "Media SDK Plugin load failed (%s)",
        msdk_status_to_string (status));
    return FALSE;
  } else if (status > MFX_ERR_NONE) {
    GST_WARNING_OBJECT (h265dec, "Media SDK Plugin load warning: %s",
        msdk_status_to_string (status));
  }

  decoder->param.mfx.CodecId = MFX_CODEC_HEVC;
  decoder->param.mfx.CodecProfile = MFX_PROFILE_HEVC_MAIN;
  return TRUE;
}

static void
gst_msdkh265dec_class_init (GstMsdkH265DecClass * klass)
{
  GstElementClass *element_class;
  GstMsdkDecClass *decoder_class;

  element_class = GST_ELEMENT_CLASS (klass);
  decoder_class = GST_MSDKDEC_CLASS (klass);

  decoder_class->configure = GST_DEBUG_FUNCPTR (gst_msdkh265dec_configure);

  gst_element_class_set_static_metadata (element_class,
      "Intel MSDK H265 decoder",
      "Codec/Decoder/Video",
      "H265 video decoder based on Intel Media SDK",
      "Scott D Phillips <scott.d.phillips@intel.com>");

  gst_element_class_add_static_pad_template (element_class, &sink_factory);
}

static void
gst_msdkh265dec_init (GstMsdkH265Dec * thiz)
{
}
