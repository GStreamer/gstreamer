/* GStreamer Intel MSDK plugin
 * Copyright (c) 2018, Intel Corporation
 * All rights reserved.
 *
 * Sreerenj Balachandran <sreerenj.balachandran@intel.com>
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

#include "gstmsdkvc1dec.h"

GST_DEBUG_CATEGORY_EXTERN (gst_msdkvc1dec_debug);
#define GST_CAT_DEFAULT gst_msdkvc1dec_debug

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-wmv, "
        "framerate = (fraction) [0/1, MAX], "
        "width = (int) [ 1, MAX ], height = (int) [ 1, MAX ], "
        "wmvversion= (int) 3, "
        "format= (string) WMV3, "
        "header-format= (string) none, "
        "stream-format= (string) sequence-layer-frame-layer, "
        "profile = (string) {simple, main}")
    );

#define gst_msdkvc1dec_parent_class parent_class
G_DEFINE_TYPE (GstMsdkVC1Dec, gst_msdkvc1dec, GST_TYPE_MSDKDEC);

static gboolean
gst_msdkvc1dec_configure (GstMsdkDec * decoder)
{
  GstCaps *caps;
  GstStructure *structure;
  const gchar *profile_str;

  caps = decoder->input_state->caps;
  if (!caps)
    return FALSE;

  structure = gst_caps_get_structure (caps, 0);
  if (!structure)
    return FALSE;

  profile_str = gst_structure_get_string (structure, "profile");

  decoder->param.mfx.CodecId = MFX_CODEC_VC1;

  if (!strcmp (profile_str, "simple"))
    decoder->param.mfx.CodecProfile = MFX_PROFILE_VC1_SIMPLE;
  else if (!strcmp (profile_str, "main"))
    decoder->param.mfx.CodecProfile = MFX_PROFILE_VC1_MAIN;
  else
    return FALSE;

  return TRUE;
}

static void
gst_msdkvc1dec_class_init (GstMsdkVC1DecClass * klass)
{
  GstElementClass *element_class;
  GstMsdkDecClass *decoder_class;

  element_class = GST_ELEMENT_CLASS (klass);
  decoder_class = GST_MSDKDEC_CLASS (klass);

  decoder_class->configure = GST_DEBUG_FUNCPTR (gst_msdkvc1dec_configure);

  gst_element_class_set_static_metadata (element_class,
      "Intel MSDK VC1 decoder",
      "Codec/Decoder/Video",
      "VC1/WMV video decoder based on Intel Media SDK",
      "Sreerenj Balachandran <sreerenj.balachandran@intel.com>");

  gst_element_class_add_static_pad_template (element_class, &sink_factory);
}

static void
gst_msdkvc1dec_init (GstMsdkVC1Dec * thiz)
{
}
