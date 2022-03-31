/*
 * GStreamer Intel MSDK plugin
 * Copyright (c) 2022 Intel Corporation. All rights reserved.
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

#include <gst/allocators/gstdmabuf.h>

#include "gstmsdkav1enc.h"

GST_DEBUG_CATEGORY_EXTERN (gst_msdkav1enc_debug);
#define GST_CAT_DEFAULT gst_msdkav1enc_debug

#define RAW_FORMATS "NV12, P010_10LE"
#define PROFILES    "main"

#define COMMON_FORMAT "{ " RAW_FORMATS " }"
#define SRC_PROFILES  "{ " PROFILES " }"

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_MSDK_CAPS_STR (COMMON_FORMAT,
            COMMON_FORMAT)));

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-av1, "
        "framerate = (fraction) [0/1, MAX], "
        "width = (int) [ 1, MAX ], height = (int) [ 1, MAX ], "
        "profile = (string) " SRC_PROFILES)
    );

#define gst_msdkav1enc_parent_class parent_class
G_DEFINE_TYPE (GstMsdkAV1Enc, gst_msdkav1enc, GST_TYPE_MSDKENC);

static gboolean
gst_msdkav1enc_set_format (GstMsdkEnc * encoder)
{
  GstMsdkAV1Enc *thiz = GST_MSDKAV1ENC (encoder);
  GstCaps *template_caps;
  GstCaps *allowed_caps = NULL;

  thiz->profile = MFX_PROFILE_AV1_MAIN;

  allowed_caps = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder));

  if (!allowed_caps || gst_caps_is_empty (allowed_caps)) {
    if (allowed_caps)
      gst_caps_unref (allowed_caps);
    return FALSE;
  }

  template_caps = gst_static_pad_template_get_caps (&src_factory);

  /* If downstream has ANY caps let encoder decide profile and level */
  if (gst_caps_is_equal (allowed_caps, template_caps)) {
    GST_INFO_OBJECT (thiz,
        "downstream has ANY caps, profile/level set to auto");
  } else {
    GstStructure *s;
    const gchar *profile;

    allowed_caps = gst_caps_make_writable (allowed_caps);
    allowed_caps = gst_caps_fixate (allowed_caps);
    s = gst_caps_get_structure (allowed_caps, 0);
    profile = gst_structure_get_string (s, "profile");

    if (profile) {
      if (!strcmp (profile, "main"))
        thiz->profile = MFX_PROFILE_AV1_MAIN;
      else
        g_assert_not_reached ();
    }
  }

  gst_caps_unref (allowed_caps);
  gst_caps_unref (template_caps);

  return TRUE;
}

static gboolean
gst_msdkav1enc_configure (GstMsdkEnc * encoder)
{
  GstMsdkAV1Enc *av1enc = GST_MSDKAV1ENC (encoder);

  encoder->num_extra_frames = encoder->async_depth - 1;
  encoder->param.mfx.CodecId = MFX_CODEC_AV1;
  encoder->param.mfx.CodecLevel = 0;

  switch (encoder->param.mfx.FrameInfo.FourCC) {
    case MFX_FOURCC_NV12:
    case MFX_FOURCC_P010:
      encoder->param.mfx.CodecProfile = MFX_PROFILE_AV1_MAIN;
      break;

    default:
      g_assert_not_reached ();
      break;
  }

  encoder->param.mfx.FrameInfo.Width =
      GST_ROUND_UP_128 (encoder->param.mfx.FrameInfo.CropW);
  encoder->param.mfx.FrameInfo.Height =
      GST_ROUND_UP_32 (encoder->param.mfx.FrameInfo.CropH);

  /* Always turn on this flag for AV1 */
  /* encoder->param.mfx.LowPower = MFX_CODINGOPTION_ON; */

  /* Enable Extended coding options */
  encoder->option3.GPB = MFX_CODINGOPTION_OFF;
  encoder->enable_extopt3 = TRUE;

  gst_msdkenc_ensure_extended_coding_options (encoder);

  memset (&av1enc->ext_av1_bs_param, 0, sizeof (av1enc->ext_av1_bs_param));
  av1enc->ext_av1_bs_param.Header.BufferId = MFX_EXTBUFF_AV1_BITSTREAM_PARAM;
  av1enc->ext_av1_bs_param.Header.BufferSz = sizeof (av1enc->ext_av1_bs_param);
  av1enc->ext_av1_bs_param.WriteIVFHeaders = MFX_CODINGOPTION_OFF;
  gst_msdkenc_add_extra_param (encoder,
      (mfxExtBuffer *) & av1enc->ext_av1_bs_param);

  memset (&av1enc->ext_av1_res_param, 0, sizeof (av1enc->ext_av1_res_param));
  av1enc->ext_av1_res_param.Header.BufferId = MFX_EXTBUFF_AV1_RESOLUTION_PARAM;
  av1enc->ext_av1_res_param.Header.BufferSz =
      sizeof (av1enc->ext_av1_res_param);
  av1enc->ext_av1_res_param.FrameWidth = encoder->param.mfx.FrameInfo.CropW;
  av1enc->ext_av1_res_param.FrameHeight = encoder->param.mfx.FrameInfo.CropH;
  gst_msdkenc_add_extra_param (encoder,
      (mfxExtBuffer *) & av1enc->ext_av1_res_param);

  return TRUE;
}

static inline const gchar *
profile_to_string (gint profile)
{
  switch (profile) {
    case MFX_PROFILE_AV1_MAIN:
      return "main";
    default:
      break;
  }

  return NULL;
}

static gint
gst_msdkav1enc_find_show_frame (GstMsdkAV1Enc * thiz, guint8 * data, gsize size,
    gsize * offset)
{
  guint8 *end;
  guint32 consumed;
  GstAV1OBU obu;
  GstAV1ParserResult res;

  if (!data || !size)
    return -1;

  end = data + size;
  *offset = 0;
  while (data < end) {
    res = gst_av1_parser_identify_one_obu (thiz->parser,
        data, end - data, &obu, &consumed);
    if (res != GST_AV1_PARSER_OK)
      return -1;

    *offset += consumed;
    switch (obu.obu_type) {
      case GST_AV1_OBU_FRAME_HEADER:
        /* check show_existing_frame flag */
        if (0x80 & *(obu.data))
          return 1;
      case GST_AV1_OBU_FRAME:
        /* check show_frame flag */
        if (0x10 & *(obu.data))
          return 1;
      default:
        break;
    }
    data += consumed;
  }
  return 0;
}

static gboolean
gst_msdkav1enc_pre_finish (GstMsdkEnc * encoder, GstBuffer ** buf,
    guint8 * data, gsize size)
{
  GstMsdkAV1Enc *thiz = GST_MSDKAV1ENC (encoder);
  gsize offset = 0;
  gint ret = 0;
  gsize avail_size;
  GstBuffer *adapt_buf = NULL;

  *buf = NULL;

  if (data && size) {
    adapt_buf = gst_buffer_new_allocate (NULL, size, NULL);
    gst_buffer_fill (adapt_buf, 0, data, size);
    gst_adapter_push (thiz->adapter, adapt_buf);
  }

  avail_size = gst_adapter_available (thiz->adapter);
  if (avail_size) {
    guint8 *parse_data = (guint8 *) gst_adapter_map (thiz->adapter, avail_size);
    ret = gst_msdkav1enc_find_show_frame (thiz,
        parse_data, avail_size, &offset);

    gst_adapter_unmap (thiz->adapter);

    if (ret == 1) {
      *buf = gst_adapter_take_buffer (thiz->adapter, offset);
      return TRUE;
    } else if (ret == 0) {
      return TRUE;
    } else {
      return FALSE;
    }
  }

  return TRUE;
}

static void
gst_msdkav1enc_flush_frames (GstMsdkEnc * encoder)
{
  GstVideoCodecFrame *frame;
  GstBuffer *out_buf = NULL;

  while (1) {
    if (!gst_msdkav1enc_pre_finish (encoder, &out_buf, NULL, 0))
      break;
    if (!out_buf)
      break;
    frame = gst_video_encoder_get_oldest_frame (GST_VIDEO_ENCODER (encoder));
    frame->output_buffer = out_buf;
    gst_video_codec_frame_unref (frame);
    gst_video_encoder_finish_frame (GST_VIDEO_ENCODER (encoder), frame);
  }

  return;
}

static GstCaps *
gst_msdkav1enc_set_src_caps (GstMsdkEnc * encoder)
{
  GstCaps *caps;
  GstStructure *structure;
  const gchar *profile;

  caps = gst_caps_new_empty_simple ("video/x-av1");
  structure = gst_caps_get_structure (caps, 0);

  profile = profile_to_string (encoder->param.mfx.CodecProfile);
  if (profile)
    gst_structure_set (structure, "profile", G_TYPE_STRING, profile, NULL);

  return caps;
}

static void
gst_msdkav1enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMsdkAV1Enc *thiz = GST_MSDKAV1ENC (object);

  if (!gst_msdkenc_set_common_property (object, prop_id, value, pspec))
    GST_WARNING_OBJECT (thiz, "Failed to set common encode property");
}

static void
gst_msdkav1enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMsdkAV1Enc *thiz = GST_MSDKAV1ENC (object);

  if (gst_msdkenc_get_common_property (object, prop_id, value, pspec))
    GST_WARNING_OBJECT (thiz, "Failed to get common encode property");
}

static void
gst_msdkav1enc_finalize (GObject * object)
{
  GstMsdkAV1Enc *thiz = GST_MSDKAV1ENC (object);

  if (thiz->parser)
    gst_av1_parser_free (thiz->parser);

  if (thiz->adapter)
    gst_adapter_clear (thiz->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_msdkav1enc_class_init (GstMsdkAV1EncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstMsdkEncClass *encoder_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  encoder_class = GST_MSDKENC_CLASS (klass);

  gobject_class->finalize = gst_msdkav1enc_finalize;
  gobject_class->set_property = gst_msdkav1enc_set_property;
  gobject_class->get_property = gst_msdkav1enc_get_property;

  encoder_class->set_format = gst_msdkav1enc_set_format;
  encoder_class->configure = gst_msdkav1enc_configure;
  encoder_class->set_src_caps = gst_msdkav1enc_set_src_caps;
  encoder_class->qp_max = 255;
  encoder_class->qp_min = 0;
  encoder_class->pre_finish = gst_msdkav1enc_pre_finish;
  encoder_class->flush_frames = gst_msdkav1enc_flush_frames;

  gst_msdkenc_install_common_properties (encoder_class);

  gst_element_class_set_static_metadata (element_class,
      "Intel MSDK AV1 encoder",
      "Codec/Encoder/Video/Hardware",
      "AV1 video encoder based on Intel Media SDK",
      "Haihao Xiang <haihao.xiang@intel.com>, "
      "Mengkejiergeli Ba <mengkejiergeli.ba@intel.com>");

  gst_element_class_add_static_pad_template (element_class, &sink_factory);
  gst_element_class_add_static_pad_template (element_class, &src_factory);
}

static void
gst_msdkav1enc_init (GstMsdkAV1Enc * thiz)
{
  thiz->adapter = gst_adapter_new ();
  thiz->parser = gst_av1_parser_new ();
}
