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

#include <mfxplugin.h>
#include <mfxvp8.h>

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

  /* This is a deprecated attribute in msdk-2017 version, but some
   * customers still using this for low-latency streaming of non-b-frame
   * encoded streams */
  decoder->param.mfx.DecodedOrder = vp8dec->output_order;
  return TRUE;
}

static void
gst_msdkdec_vp8_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMsdkVP8Dec *thiz = GST_MSDKVP8DEC (object);
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
gst_msdkdec_vp8_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMsdkVP8Dec *thiz = GST_MSDKVP8DEC (object);

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
gst_msdkvp8dec_class_init (GstMsdkVP8DecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstMsdkDecClass *decoder_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  decoder_class = GST_MSDKDEC_CLASS (klass);

  gobject_class->set_property = gst_msdkdec_vp8_set_property;
  gobject_class->get_property = gst_msdkdec_vp8_get_property;

  decoder_class->configure = GST_DEBUG_FUNCPTR (gst_msdkvp8dec_configure);

  gst_element_class_set_static_metadata (element_class,
      "Intel MSDK VP8 decoder",
      "Codec/Decoder/Video/Hardware",
      "VP8 video decoder based on Intel Media SDK",
      "Hyunjun Ko <zzoon@igalia.com>");

  gst_msdkdec_prop_install_output_oder_property (gobject_class);

  gst_element_class_add_static_pad_template (element_class, &sink_factory);
}

static void
gst_msdkvp8dec_init (GstMsdkVP8Dec * thiz)
{
  thiz->output_order = PROP_OUTPUT_ORDER_DEFAULT;
}
