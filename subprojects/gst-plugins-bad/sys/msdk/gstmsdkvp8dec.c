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

/**
 * SECTION:element-msdkvp8dec
 * @title: msdkvp8dec
 * @short_description: Intel MSDK VP8 decoder
 *
 * VP8 video decoder based on Intel MFX
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 filesrc location=sample.webm ! matroskademux ! msdkvp8dec ! glimagesink
 * ```
 *
 * Since: 1.14
 *
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "gstmsdkvp8dec.h"

#include <mfxvp8.h>

GST_DEBUG_CATEGORY_EXTERN (gst_msdkvp8dec_debug);
#define GST_CAT_DEFAULT gst_msdkvp8dec_debug

#define GST_MSDKVP8DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), G_TYPE_FROM_INSTANCE (obj), GstMsdkVP8Dec))
#define GST_MSDKVP8DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), G_TYPE_FROM_CLASS (klass),  GstMsdkVP8DecClass))
#define GST_IS_MSDKVP8DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), G_TYPE_FROM_INSTANCE (obj)))
#define GST_IS_MSDKVP8DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), G_TYPE_FROM_CLASS (klass)))

/* *INDENT-OFF* */
static const gchar *doc_src_caps_str =
    GST_VIDEO_CAPS_MAKE ("{ NV12 }") " ;"
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("memory:DMABuf", "{ NV12 }") " ;"
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("memory:VAMemory", "{ NV12 }");
/* *INDENT-ON* */

static const gchar *doc_sink_caps_str = "video/x-vp8";

static GstElementClass *parent_class = NULL;

static gboolean
gst_msdkvp8dec_configure (GstMsdkDec * decoder)
{
  GstMsdkVP8Dec *vp8dec = GST_MSDKVP8DEC (decoder);
  mfxSession session;
  const mfxPluginUID *uid;

  session = gst_msdk_context_get_session (decoder->context);

  uid = &MFX_PLUGINID_VP8D_HW;

  if (!gst_msdk_load_plugin (session, uid, 1, "msdkvp8dec"))
    return FALSE;

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

static gboolean
gst_msdkvp8dec_preinit_decoder (GstMsdkDec * decoder)
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

static gboolean
gst_msdkvp8dec_postinit_decoder (GstMsdkDec * decoder)
{
  /* Force the structure to MFX_PICSTRUCT_PROGRESSIVE if it is unknow to
   * work-around MSDK issue:
   * https://github.com/Intel-Media-SDK/MediaSDK/issues/1139
   */
  if (decoder->param.mfx.FrameInfo.PicStruct == MFX_PICSTRUCT_UNKNOWN)
    decoder->param.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;

  return TRUE;
}

static void
gst_msdkvp8dec_class_init (gpointer klass, gpointer data)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstMsdkDecClass *decoder_class;
  MsdkDecCData *cdata = data;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  decoder_class = GST_MSDKDEC_CLASS (klass);

  gobject_class->set_property = gst_msdkdec_vp8_set_property;
  gobject_class->get_property = gst_msdkdec_vp8_get_property;

  decoder_class->configure = GST_DEBUG_FUNCPTR (gst_msdkvp8dec_configure);
  decoder_class->preinit_decoder =
      GST_DEBUG_FUNCPTR (gst_msdkvp8dec_preinit_decoder);
  decoder_class->postinit_decoder =
      GST_DEBUG_FUNCPTR (gst_msdkvp8dec_postinit_decoder);

  gst_element_class_set_static_metadata (element_class,
      "Intel MSDK VP8 decoder",
      "Codec/Decoder/Video/Hardware",
      "VP8 video decoder based on " MFX_API_SDK,
      "Hyunjun Ko <zzoon@igalia.com>");

  gst_msdkdec_prop_install_output_oder_property (gobject_class);

  gst_msdkcaps_pad_template_init (element_class,
      cdata->sink_caps, cdata->src_caps, doc_sink_caps_str, doc_src_caps_str);

  gst_caps_unref (cdata->sink_caps);
  gst_caps_unref (cdata->src_caps);
  g_free (cdata);
}

static void
gst_msdkvp8dec_init (GTypeInstance * instance, gpointer g_class)
{
  GstMsdkVP8Dec *thiz = GST_MSDKVP8DEC (instance);
  thiz->output_order = PROP_OUTPUT_ORDER_DEFAULT;
}

gboolean
gst_msdkvp8dec_register (GstPlugin * plugin,
    GstMsdkContext * context, GstCaps * sink_caps,
    GstCaps * src_caps, guint rank)
{
  GType type;
  MsdkDecCData *cdata;
  gchar *type_name, *feature_name;
  gboolean ret = FALSE;

  GTypeInfo type_info = {
    .class_size = sizeof (GstMsdkVP8DecClass),
    .class_init = gst_msdkvp8dec_class_init,
    .instance_size = sizeof (GstMsdkVP8Dec),
    .instance_init = gst_msdkvp8dec_init
  };

  cdata = g_new (MsdkDecCData, 1);
  cdata->sink_caps = gst_caps_ref (sink_caps);
  cdata->src_caps = gst_caps_ref (src_caps);

  GST_MINI_OBJECT_FLAG_SET (cdata->sink_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
  GST_MINI_OBJECT_FLAG_SET (cdata->src_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);

  type_info.class_data = cdata;

  type_name = g_strdup ("GstMsdkVP8Dec");
  feature_name = g_strdup ("msdkvp8dec");

  type = g_type_register_static (GST_TYPE_MSDKDEC, type_name, &type_info, 0);
  if (type)
    ret = gst_element_register (plugin, feature_name, rank, type);

  g_free (type_name);
  g_free (feature_name);

  return ret;
}
