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

/* sample pipeline: gst-launch-1.0 filesrc location=video.wmv ! asfdemux ! vc1parse !  msdkvc1dec ! videoconvert ! xvimagesink */

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
        "profile = (string) {simple, main}" ";"
        "video/x-wmv, "
        "framerate = (fraction) [0/1, MAX], "
        "width = (int) [ 1, MAX ], height = (int) [ 1, MAX ], "
        "wmvversion= (int) 3, "
        "format= (string) WVC1, "
        "header-format= (string) asf, "
        "stream-format= (string) bdu, " "profile = (string) advanced" ";")
    );

#define gst_msdkvc1dec_parent_class parent_class
G_DEFINE_TYPE (GstMsdkVC1Dec, gst_msdkvc1dec, GST_TYPE_MSDKDEC);

static gboolean
gst_msdkvc1dec_configure (GstMsdkDec * decoder)
{
  GstMsdkVC1Dec *vc1dec = GST_MSDKVC1DEC (decoder);
  GstBuffer *buffer;
  GstCaps *caps;
  GstStructure *structure;
  const gchar *profile_str;

  caps = decoder->input_state->caps;
  if (!caps)
    return FALSE;

  structure = gst_caps_get_structure (caps, 0);
  if (!structure)
    return FALSE;

  decoder->param.mfx.CodecId = MFX_CODEC_VC1;

  profile_str = gst_structure_get_string (structure, "profile");

  if (!strcmp (profile_str, "simple"))
    decoder->param.mfx.CodecProfile = MFX_PROFILE_VC1_SIMPLE;
  else if (!strcmp (profile_str, "main"))
    decoder->param.mfx.CodecProfile = MFX_PROFILE_VC1_MAIN;
  else {
    decoder->param.mfx.CodecProfile = MFX_PROFILE_VC1_ADVANCED;
    /* asf advanced profile codec-data has 1 byte in the begining
     * which is the ASF binding byte. MediaSDK can't recognize this
     * byte, so discard it */
    if (decoder->input_state->codec_data) {
      buffer = gst_buffer_copy_region (decoder->input_state->codec_data,
          GST_BUFFER_COPY_DEEP | GST_BUFFER_COPY_MEMORY, 1,
          gst_buffer_get_size (decoder->input_state->codec_data) - 1);
      gst_adapter_push (decoder->adapter, buffer);
    }

    decoder->is_packetized = FALSE;
  }

  /* This is a deprecated attribute in msdk-2017 version, but some
   * customers still using this for low-latency streaming of non-b-frame
   * encoded streams */
  decoder->param.mfx.DecodedOrder = vc1dec->output_order;
  return TRUE;

  return TRUE;
}

static void
gst_msdkdec_vc1_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMsdkVC1Dec *thiz = GST_MSDKVC1DEC (object);
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
gst_msdkdec_vc1_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMsdkVC1Dec *thiz = GST_MSDKVC1DEC (object);

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

static void
gst_msdkvc1dec_class_init (GstMsdkVC1DecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstMsdkDecClass *decoder_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  decoder_class = GST_MSDKDEC_CLASS (klass);

  gobject_class->set_property = gst_msdkdec_vc1_set_property;
  gobject_class->get_property = gst_msdkdec_vc1_get_property;

  decoder_class->configure = GST_DEBUG_FUNCPTR (gst_msdkvc1dec_configure);

  gst_element_class_set_static_metadata (element_class,
      "Intel MSDK VC1 decoder",
      "Codec/Decoder/Video/Hardware",
      "VC1/WMV video decoder based on Intel Media SDK",
      "Sreerenj Balachandran <sreerenj.balachandran@intel.com>");

  gst_msdkdec_prop_install_output_oder_property (gobject_class);

  gst_element_class_add_static_pad_template (element_class, &sink_factory);
}

static void
gst_msdkvc1dec_init (GstMsdkVC1Dec * thiz)
{
  thiz->output_order = PROP_OUTPUT_ORDER_DEFAULT;
}
