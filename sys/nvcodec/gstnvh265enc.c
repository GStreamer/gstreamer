/* GStreamer NVENC plugin
 * Copyright (C) 2015 Centricular Ltd
 * Copyright (C) 2018 Seungha Yang <pudding8757@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstnvh265enc.h"

#include <gst/pbutils/codec-utils.h>
#include <gst/base/gstbytewriter.h>

#include <string.h>

typedef struct
{
  GstCaps *sink_caps;
  GstCaps *src_caps;
  gboolean is_default;
} GstNvH265EncClassData;

GST_DEBUG_CATEGORY_STATIC (gst_nv_h265_enc_debug);
#define GST_CAT_DEFAULT gst_nv_h265_enc_debug

static GstElementClass *parent_class = NULL;

enum
{
  PROP_0,
  PROP_AUD,
  PROP_WEIGHTED_PRED,
  PROP_VBV_BUFFER_SIZE,
  PROP_RC_LOOKAHEAD,
  PROP_TEMPORAL_AQ,
  PROP_BFRAMES,
  PROP_B_ADAPT,
};

#define DEFAULT_AUD TRUE
#define DEFAULT_WEIGHTED_PRED FALSE
#define DEFAULT_VBV_BUFFER_SIZE 0
#define DEFAULT_RC_LOOKAHEAD 0
#define DEFAULT_TEMPORAL_AQ FALSE
#define DEFAULT_BFRAMES 0
#define DEFAULT_B_ADAPT FALSE

static gboolean gst_nv_h265_enc_open (GstVideoEncoder * enc);
static gboolean gst_nv_h265_enc_close (GstVideoEncoder * enc);
static gboolean gst_nv_h265_enc_stop (GstVideoEncoder * enc);
static gboolean gst_nv_h265_enc_set_src_caps (GstNvBaseEnc * nvenc,
    GstVideoCodecState * state);
static gboolean gst_nv_h265_enc_set_encoder_config (GstNvBaseEnc * nvenc,
    GstVideoCodecState * state, NV_ENC_CONFIG * config);
static gboolean gst_nv_h265_enc_set_pic_params (GstNvBaseEnc * nvenc,
    GstVideoCodecFrame * frame, NV_ENC_PIC_PARAMS * pic_params);
static void gst_nv_h265_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_nv_h265_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_nv_h265_enc_finalize (GObject * obj);

static void
gst_nv_h265_enc_class_init (GstNvH265EncClass * klass, gpointer data)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoEncoderClass *videoenc_class = GST_VIDEO_ENCODER_CLASS (klass);
  GstNvBaseEncClass *nvenc_class = GST_NV_BASE_ENC_CLASS (klass);
  GstNvEncDeviceCaps *device_caps = &nvenc_class->device_caps;
  GstNvH265EncClassData *cdata = (GstNvH265EncClassData *) data;
  gchar *long_name;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_nv_h265_enc_set_property;
  gobject_class->get_property = gst_nv_h265_enc_get_property;
  gobject_class->finalize = gst_nv_h265_enc_finalize;

  videoenc_class->open = GST_DEBUG_FUNCPTR (gst_nv_h265_enc_open);
  videoenc_class->close = GST_DEBUG_FUNCPTR (gst_nv_h265_enc_close);
  videoenc_class->stop = GST_DEBUG_FUNCPTR (gst_nv_h265_enc_stop);

  nvenc_class->codec_id = NV_ENC_CODEC_HEVC_GUID;
  nvenc_class->set_encoder_config = gst_nv_h265_enc_set_encoder_config;
  nvenc_class->set_src_caps = gst_nv_h265_enc_set_src_caps;
  nvenc_class->set_pic_params = gst_nv_h265_enc_set_pic_params;

  g_object_class_install_property (gobject_class, PROP_AUD,
      g_param_spec_boolean ("aud", "AUD",
          "Use AU (Access Unit) delimiter", DEFAULT_AUD,
          G_PARAM_READWRITE | GST_PARAM_MUTABLE_PLAYING |
          G_PARAM_STATIC_STRINGS));

  if (device_caps->weighted_prediction) {
    g_object_class_install_property (gobject_class, PROP_WEIGHTED_PRED,
        g_param_spec_boolean ("weighted-pred", "Weighted Pred",
            "Weighted Prediction "
            "(Exposed only if supported by device)", DEFAULT_WEIGHTED_PRED,
            G_PARAM_READWRITE | GST_PARAM_MUTABLE_PLAYING |
            G_PARAM_STATIC_STRINGS));
  }

  if (device_caps->custom_vbv_bufsize) {
    g_object_class_install_property (gobject_class,
        PROP_VBV_BUFFER_SIZE,
        g_param_spec_uint ("vbv-buffer-size", "VBV Buffer Size",
            "VBV(HRD) Buffer Size in kbits (0 = NVENC default) "
            "(Exposed only if supported by device)", 0, G_MAXUINT,
            DEFAULT_VBV_BUFFER_SIZE,
            G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
            G_PARAM_STATIC_STRINGS));
  }

  if (device_caps->lookahead) {
    g_object_class_install_property (gobject_class, PROP_RC_LOOKAHEAD,
        g_param_spec_uint ("rc-lookahead", "Rate Control Lookahead",
            "Number of frames for frame type lookahead "
            "(Exposed only if supported by device)", 0, 32,
            DEFAULT_RC_LOOKAHEAD,
            G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
            G_PARAM_STATIC_STRINGS));
  }

  if (device_caps->temporal_aq) {
    g_object_class_install_property (gobject_class, PROP_TEMPORAL_AQ,
        g_param_spec_boolean ("temporal-aq", "Temporal AQ",
            "Temporal Adaptive Quantization "
            "(Exposed only if supported by device)", DEFAULT_TEMPORAL_AQ,
            G_PARAM_READWRITE | GST_PARAM_MUTABLE_PLAYING |
            G_PARAM_STATIC_STRINGS));
  }

  if (device_caps->bframes > 0) {
    g_object_class_install_property (gobject_class, PROP_BFRAMES,
        g_param_spec_uint ("bframes", "B-Frames",
            "Number of B-frames between I and P "
            "(Exposed only if supported by device)", 0, device_caps->bframes,
            DEFAULT_BFRAMES,
            G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
            G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class, PROP_B_ADAPT,
        g_param_spec_boolean ("b-adapt", "B Adapt",
            "Enable adaptive B-frame insert when lookahead is enabled "
            "(Exposed only if supported by device)",
            DEFAULT_B_ADAPT,
            G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
            G_PARAM_STATIC_STRINGS));
  }

  if (cdata->is_default)
    long_name = g_strdup ("NVENC HEVC Video Encoder");
  else
    long_name = g_strdup_printf ("NVENC HEVC Video Encoder with device %d",
        nvenc_class->cuda_device_id);

  gst_element_class_set_metadata (element_class, long_name,
      "Codec/Encoder/Video/Hardware",
      "Encode HEVC video streams using NVIDIA's hardware-accelerated NVENC encoder API",
      "Tim-Philipp Müller <tim@centricular.com>, "
      "Matthew Waters <matthew@centricular.com>, "
      "Seungha Yang <pudding8757@gmail.com>");
  g_free (long_name);

  GST_DEBUG_CATEGORY_INIT (gst_nv_h265_enc_debug,
      "nvh265enc", 0, "Nvidia HEVC encoder");

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          cdata->sink_caps));
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          cdata->src_caps));

  gst_caps_unref (cdata->sink_caps);
  gst_caps_unref (cdata->src_caps);
  g_free (cdata);
}

static void
gst_nv_h265_enc_init (GstNvH265Enc * nvenc)
{
  GstNvBaseEnc *baseenc = GST_NV_BASE_ENC (nvenc);

  nvenc->aud = DEFAULT_AUD;

  /* device capability dependent properties */
  baseenc->weighted_pred = DEFAULT_WEIGHTED_PRED;
  baseenc->vbv_buffersize = DEFAULT_VBV_BUFFER_SIZE;
  baseenc->rc_lookahead = DEFAULT_RC_LOOKAHEAD;
  baseenc->temporal_aq = DEFAULT_TEMPORAL_AQ;
  baseenc->bframes = DEFAULT_BFRAMES;
  baseenc->b_adapt = DEFAULT_B_ADAPT;
}

static void
gst_nv_h265_enc_finalize (GObject * obj)
{
  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static gboolean
gst_nv_h265_enc_open (GstVideoEncoder * enc)
{
  GstNvBaseEnc *base = GST_NV_BASE_ENC (enc);

  if (!GST_VIDEO_ENCODER_CLASS (parent_class)->open (enc))
    return FALSE;

  /* Check if HEVC is supported */
  {
    uint32_t i, num = 0;
    GUID guids[16];

    NvEncGetEncodeGUIDs (base->encoder, guids, G_N_ELEMENTS (guids), &num);

    for (i = 0; i < num; ++i) {
      if (gst_nvenc_cmp_guid (guids[i], NV_ENC_CODEC_HEVC_GUID))
        break;
    }
    GST_INFO_OBJECT (enc, "HEVC encoding %ssupported", (i == num) ? "un" : "");
    if (i == num) {
      gst_nv_h265_enc_close (enc);
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean
gst_nv_h265_enc_close (GstVideoEncoder * enc)
{
  return GST_VIDEO_ENCODER_CLASS (parent_class)->close (enc);
}

static void
gst_nv_h265_enc_clear_stream_data (GstNvH265Enc * h265enc)
{
  gint i;

  if (!h265enc->sei_payload)
    return;

  for (i = 0; i < h265enc->num_sei_payload; i++)
    g_free (h265enc->sei_payload[i].payload);

  g_free (h265enc->sei_payload);
  h265enc->sei_payload = NULL;
  h265enc->num_sei_payload = 0;
}

static gboolean
gst_nv_h265_enc_stop (GstVideoEncoder * enc)
{
  GstNvH265Enc *h265enc = (GstNvH265Enc *) enc;

  gst_nv_h265_enc_clear_stream_data (h265enc);

  return GST_VIDEO_ENCODER_CLASS (parent_class)->stop (enc);
}

static gboolean
gst_nv_h265_enc_set_level_tier_and_profile (GstNvH265Enc * nvenc,
    GstCaps * caps)
{
#define N_BYTES_VPS 128
  guint8 vps[N_BYTES_VPS];
  NV_ENC_SEQUENCE_PARAM_PAYLOAD spp = { 0, };
  NVENCSTATUS nv_ret;
  guint32 seq_size;

  spp.version = gst_nvenc_get_sequence_param_payload_version ();
  spp.inBufferSize = N_BYTES_VPS;
  spp.spsId = 0;
  spp.ppsId = 0;
  spp.spsppsBuffer = &vps;
  spp.outSPSPPSPayloadSize = &seq_size;
  nv_ret = NvEncGetSequenceParams (GST_NV_BASE_ENC (nvenc)->encoder, &spp);
  if (nv_ret != NV_ENC_SUCCESS) {
    GST_ELEMENT_ERROR (nvenc, STREAM, ENCODE, ("Encode header failed."),
        ("NvEncGetSequenceParams return code=%d", nv_ret));
    return FALSE;
  }

  if (seq_size < 8) {
    GST_ELEMENT_ERROR (nvenc, STREAM, ENCODE, ("Encode header failed."),
        ("NvEncGetSequenceParams returned incomplete data"));
    return FALSE;
  }

  GST_MEMDUMP ("Header", spp.spsppsBuffer, seq_size);

  /* skip nal header and identifier */
  gst_codec_utils_h265_caps_set_level_tier_and_profile (caps,
      &vps[6], seq_size - 6);

  return TRUE;

#undef N_BYTES_VPS
}

static gboolean
gst_nv_h265_enc_set_src_caps (GstNvBaseEnc * nvenc, GstVideoCodecState * state)
{
  GstNvH265Enc *h265enc = (GstNvH265Enc *) nvenc;
  GstVideoCodecState *out_state;
  GstStructure *s;
  GstCaps *out_caps;

  out_caps = gst_caps_new_empty_simple ("video/x-h265");
  s = gst_caps_get_structure (out_caps, 0);

  /* TODO: add support for hvc1,hev1 format as well */
  gst_structure_set (s, "stream-format", G_TYPE_STRING, "byte-stream",
      "alignment", G_TYPE_STRING, "au", NULL);

  if (!gst_nv_h265_enc_set_level_tier_and_profile (h265enc, out_caps)) {
    gst_caps_unref (out_caps);
    return FALSE;
  }

  out_state = gst_video_encoder_set_output_state (GST_VIDEO_ENCODER (nvenc),
      out_caps, state);

  GST_INFO_OBJECT (nvenc, "output caps: %" GST_PTR_FORMAT, out_state->caps);

  /* encoder will keep it around for us */
  gst_video_codec_state_unref (out_state);

  /* TODO: would be nice to also send some tags with the codec name */
  return TRUE;
}

static guint8 *
gst_nv_h265_enc_create_mastering_display_sei_nal (GstNvH265Enc * h265enc,
    GstVideoMasteringDisplayInfo * minfo, guint * size)
{
  guint sei_size;
  guint16 primary_x[3];
  guint16 primary_y[3];
  guint16 white_x, white_y;
  guint32 max_luma;
  guint32 min_luma;
  const guint chroma_scale = 50000;
  const guint luma_scale = 10000;
  gint i;
  GstByteWriter br;

  GST_DEBUG_OBJECT (h265enc, "Apply mastering display info");
  GST_LOG_OBJECT (h265enc, "\tRed  (%u/%u, %u/%u)", minfo->Rx_n,
      minfo->Rx_d, minfo->Ry_n, minfo->Ry_d);
  GST_LOG_OBJECT (h265enc, "\tGreen(%u/%u, %u/%u)", minfo->Gx_n,
      minfo->Gx_d, minfo->Gy_n, minfo->Gy_d);
  GST_LOG_OBJECT (h265enc, "\tBlue (%u/%u, %u/%u)", minfo->Bx_n,
      minfo->Bx_d, minfo->By_n, minfo->By_d);
  GST_LOG_OBJECT (h265enc, "\tWhite(%u/%u, %u/%u)", minfo->Wx_n,
      minfo->Wx_d, minfo->Wy_n, minfo->Wy_d);
  GST_LOG_OBJECT (h265enc,
      "\tmax_luminance:(%u/%u), min_luminance:(%u/%u)",
      minfo->max_luma_n, minfo->max_luma_d, minfo->min_luma_n,
      minfo->min_luma_d);

  primary_x[0] =
      (guint16) gst_util_uint64_scale_round (minfo->Gx_n, chroma_scale,
      minfo->Gx_d);
  primary_x[1] =
      (guint16) gst_util_uint64_scale_round (minfo->Bx_n, chroma_scale,
      minfo->Bx_d);
  primary_x[2] =
      (guint16) gst_util_uint64_scale_round (minfo->Rx_n, chroma_scale,
      minfo->Rx_d);

  primary_y[0] =
      (guint16) gst_util_uint64_scale_round (minfo->Gy_n, chroma_scale,
      minfo->Gy_d);
  primary_y[1] =
      (guint16) gst_util_uint64_scale_round (minfo->By_n, chroma_scale,
      minfo->By_d);
  primary_y[2] =
      (guint16) gst_util_uint64_scale_round (minfo->Ry_n, chroma_scale,
      minfo->Ry_d);

  white_x =
      (guint16) gst_util_uint64_scale_round (minfo->Wx_n, chroma_scale,
      minfo->Wx_d);
  white_y =
      (guint16) gst_util_uint64_scale_round (minfo->Wy_n, chroma_scale,
      minfo->Wy_d);
  max_luma =
      (guint32) gst_util_uint64_scale_round (minfo->max_luma_n, luma_scale,
      minfo->max_luma_d);
  min_luma =
      (guint32) gst_util_uint64_scale_round (minfo->min_luma_n, luma_scale,
      minfo->min_luma_d);

  /* x, y 16bits per RGB channel
   * x, y 16bits white point
   * max, min luminance 32bits
   */
  sei_size = (2 * 2 * 3) + (2 * 2) + (4 * 2);
  gst_byte_writer_init_with_size (&br, sei_size, TRUE);

  for (i = 0; i < 3; i++) {
    gst_byte_writer_put_uint16_be (&br, primary_x[i]);
    gst_byte_writer_put_uint16_be (&br, primary_y[i]);
  }

  gst_byte_writer_put_uint16_be (&br, white_x);
  gst_byte_writer_put_uint16_be (&br, white_y);

  gst_byte_writer_put_uint32_be (&br, max_luma);
  gst_byte_writer_put_uint32_be (&br, min_luma);

  *size = sei_size;

  return gst_byte_writer_reset_and_get_data (&br);
}

static guint8 *
gst_nv_h265_enc_create_content_light_level_sei_nal (GstNvH265Enc * h265enc,
    GstVideoContentLightLevel * linfo, guint * size)
{
  gdouble val;
  guint sei_size;
  GstByteWriter br;

  GST_DEBUG_OBJECT (h265enc, "Apply content light level");
  GST_LOG_OBJECT (h265enc, "content light level found");
  GST_LOG_OBJECT (h265enc,
      "\tmaxCLL:(%u/%u), maxFALL:(%u/%u)", linfo->maxCLL_n, linfo->maxCLL_d,
      linfo->maxFALL_n, linfo->maxFALL_d);

  /* maxCLL and maxFALL per 16bits */
  sei_size = 2 * 2;
  gst_byte_writer_init_with_size (&br, sei_size, TRUE);

  gst_util_fraction_to_double (linfo->maxCLL_n, linfo->maxCLL_d, &val);
  gst_byte_writer_put_uint16_be (&br, (guint16) val);

  gst_util_fraction_to_double (linfo->maxFALL_n, linfo->maxFALL_d, &val);
  gst_byte_writer_put_uint16_be (&br, (guint16) val);

  *size = sei_size;

  return gst_byte_writer_reset_and_get_data (&br);
}

static gboolean
gst_nv_h265_enc_set_encoder_config (GstNvBaseEnc * nvenc,
    GstVideoCodecState * state, NV_ENC_CONFIG * config)
{
  GstNvH265Enc *h265enc = (GstNvH265Enc *) nvenc;
  GstCaps *allowed_caps, *template_caps;
  GUID selected_profile = NV_ENC_CODEC_PROFILE_AUTOSELECT_GUID;
  int level_idc = NV_ENC_LEVEL_AUTOSELECT;
  GstVideoInfo *info = &state->info;
  NV_ENC_CONFIG_HEVC *hevc_config = &config->encodeCodecConfig.hevcConfig;
  NV_ENC_CONFIG_HEVC_VUI_PARAMETERS *vui = &hevc_config->hevcVUIParameters;

  template_caps =
      gst_pad_get_pad_template_caps (GST_VIDEO_ENCODER_SRC_PAD (h265enc));
  allowed_caps = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (h265enc));

  if (template_caps == allowed_caps) {
    GST_INFO_OBJECT (h265enc, "downstream has ANY caps");
  } else if (allowed_caps) {
    GstStructure *s;
    const gchar *profile;
    const gchar *level;

    if (gst_caps_is_empty (allowed_caps)) {
      gst_caps_unref (allowed_caps);
      gst_caps_unref (template_caps);
      return FALSE;
    }

    allowed_caps = gst_caps_make_writable (allowed_caps);
    allowed_caps = gst_caps_fixate (allowed_caps);
    s = gst_caps_get_structure (allowed_caps, 0);

    profile = gst_structure_get_string (s, "profile");
    /* FIXME: only support main profile only for now */
    if (profile) {
      if (!strcmp (profile, "main")) {
        selected_profile = NV_ENC_HEVC_PROFILE_MAIN_GUID;
      } else if (g_str_has_prefix (profile, "main-10")) {
        selected_profile = NV_ENC_HEVC_PROFILE_MAIN10_GUID;
      } else if (g_str_has_prefix (profile, "main-444")) {
        selected_profile = NV_ENC_HEVC_PROFILE_FREXT_GUID;
      } else {
        g_assert_not_reached ();
      }
    }

    level = gst_structure_get_string (s, "level");
    if (level)
      /* matches values stored in NV_ENC_LEVEL */
      level_idc = gst_codec_utils_h265_get_level_idc (level);

    gst_caps_unref (allowed_caps);
  }
  gst_caps_unref (template_caps);

  /* override some defaults */
  GST_LOG_OBJECT (h265enc, "setting parameters");
  config->profileGUID = selected_profile;
  hevc_config->level = level_idc;
  hevc_config->idrPeriod = config->gopLength;

  config->encodeCodecConfig.hevcConfig.chromaFormatIDC = 1;
  if (GST_VIDEO_INFO_FORMAT (info) == GST_VIDEO_FORMAT_Y444 ||
      GST_VIDEO_INFO_FORMAT (info) == GST_VIDEO_FORMAT_Y444_16LE ||
      GST_VIDEO_INFO_FORMAT (info) == GST_VIDEO_FORMAT_Y444_16BE ||
      GST_VIDEO_INFO_FORMAT (info) == GST_VIDEO_FORMAT_VUYA) {
    GST_DEBUG_OBJECT (h265enc, "have Y444 input, setting config accordingly");
    config->profileGUID = NV_ENC_HEVC_PROFILE_FREXT_GUID;
    config->encodeCodecConfig.hevcConfig.chromaFormatIDC = 3;
    if (GST_VIDEO_INFO_FORMAT (info) == GST_VIDEO_FORMAT_Y444_16LE ||
        GST_VIDEO_INFO_FORMAT (info) == GST_VIDEO_FORMAT_Y444_16BE)
      config->encodeCodecConfig.hevcConfig.pixelBitDepthMinus8 = 2;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  } else if (GST_VIDEO_INFO_FORMAT (info) == GST_VIDEO_FORMAT_P010_10LE) {
#else
  } else if (GST_VIDEO_INFO_FORMAT (info) == GST_VIDEO_FORMAT_P010_10BE) {
#endif
    config->profileGUID = NV_ENC_HEVC_PROFILE_MAIN10_GUID;
    config->encodeCodecConfig.hevcConfig.pixelBitDepthMinus8 = 2;
  }

  hevc_config->outputAUD = h265enc->aud;

  vui->videoSignalTypePresentFlag = 1;
  /* NOTE: vui::video_format represents the video format before
   * being encoded such as PAL, NTSC, SECAM, and MAC. That's not much informal
   * and can be inferred with resolution and framerate by any application.
   */
  /* Unspecified video format (5) */
  vui->videoFormat = 5;

  if (info->colorimetry.range == GST_VIDEO_COLOR_RANGE_0_255) {
    vui->videoFullRangeFlag = 1;
  } else {
    vui->videoFullRangeFlag = 0;
  }

  vui->colourDescriptionPresentFlag = 1;
  vui->colourMatrix = gst_video_color_matrix_to_iso (info->colorimetry.matrix);
  vui->colourPrimaries =
      gst_video_color_primaries_to_iso (info->colorimetry.primaries);
  vui->transferCharacteristics =
      gst_video_color_transfer_to_iso (info->colorimetry.transfer);

  gst_nv_h265_enc_clear_stream_data (h265enc);

  {
    GstVideoMasteringDisplayInfo minfo;
    GstVideoContentLightLevel linfo;
    gboolean have_mastering;
    gboolean have_cll;
    guint size;
    gint i = 0;

    have_mastering =
        gst_video_mastering_display_info_from_caps (&minfo, state->caps);
    have_cll = gst_video_content_light_level_from_caps (&linfo, state->caps);

    if (have_mastering)
      h265enc->num_sei_payload++;
    if (have_cll)
      h265enc->num_sei_payload++;

    h265enc->sei_payload =
        g_new0 (NV_ENC_SEI_PAYLOAD, h265enc->num_sei_payload);

    if (have_mastering) {
      h265enc->sei_payload[i].payload =
          gst_nv_h265_enc_create_mastering_display_sei_nal (h265enc,
          &minfo, &size);
      h265enc->sei_payload[i].payloadSize = size;
      h265enc->sei_payload[i].payloadType = 137;
      i++;
    }

    if (have_cll) {
      h265enc->sei_payload[i].payload =
          gst_nv_h265_enc_create_content_light_level_sei_nal (h265enc,
          &linfo, &size);
      h265enc->sei_payload[i].payloadSize = size;
      h265enc->sei_payload[i].payloadType = 144;
    }
  }

  return TRUE;
}

static gboolean
gst_nv_h265_enc_set_pic_params (GstNvBaseEnc * enc, GstVideoCodecFrame * frame,
    NV_ENC_PIC_PARAMS * pic_params)
{
  GstNvH265Enc *h265enc = (GstNvH265Enc *) enc;

  /* encode whole picture in one single slice */
  pic_params->codecPicParams.hevcPicParams.sliceMode = 0;
  pic_params->codecPicParams.hevcPicParams.sliceModeData = 0;

  if (h265enc->sei_payload) {
    pic_params->codecPicParams.hevcPicParams.seiPayloadArray =
        h265enc->sei_payload;
    pic_params->codecPicParams.hevcPicParams.seiPayloadArrayCnt =
        h265enc->num_sei_payload;
  }

  return TRUE;
}

static void
gst_nv_h265_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstNvH265Enc *self = (GstNvH265Enc *) object;
  GstNvBaseEnc *nvenc = GST_NV_BASE_ENC (object);
  GstNvBaseEncClass *klass = GST_NV_BASE_ENC_GET_CLASS (object);
  GstNvEncDeviceCaps *device_caps = &klass->device_caps;
  gboolean reconfig = FALSE;

  switch (prop_id) {
    case PROP_AUD:
    {
      gboolean aud;

      aud = g_value_get_boolean (value);
      if (aud != self->aud) {
        self->aud = aud;
        reconfig = TRUE;
      }
      break;
    }
    case PROP_WEIGHTED_PRED:
      if (!device_caps->weighted_prediction) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      } else {
        nvenc->weighted_pred = g_value_get_boolean (value);
        reconfig = TRUE;
      }
      break;
    case PROP_VBV_BUFFER_SIZE:
      if (!device_caps->custom_vbv_bufsize) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      } else {
        nvenc->vbv_buffersize = g_value_get_uint (value);
        reconfig = TRUE;
      }
      break;
    case PROP_RC_LOOKAHEAD:
      if (!device_caps->lookahead) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      } else {
        nvenc->rc_lookahead = g_value_get_uint (value);
        reconfig = TRUE;
      }
      break;
    case PROP_TEMPORAL_AQ:
      if (!device_caps->temporal_aq) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      } else {
        nvenc->temporal_aq = g_value_get_boolean (value);
        reconfig = TRUE;
      }
      break;
    case PROP_BFRAMES:
      if (!device_caps->bframes) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      } else {
        nvenc->bframes = g_value_get_uint (value);
        reconfig = TRUE;
      }
      break;
    case PROP_B_ADAPT:
      if (!device_caps->bframes) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      } else {
        nvenc->b_adapt = g_value_get_boolean (value);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  if (reconfig)
    gst_nv_base_enc_schedule_reconfig (GST_NV_BASE_ENC (self));
}

static void
gst_nv_h265_enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstNvH265Enc *self = (GstNvH265Enc *) object;
  GstNvBaseEnc *nvenc = GST_NV_BASE_ENC (object);
  GstNvBaseEncClass *klass = GST_NV_BASE_ENC_GET_CLASS (object);
  GstNvEncDeviceCaps *device_caps = &klass->device_caps;

  switch (prop_id) {
    case PROP_AUD:
      g_value_set_boolean (value, self->aud);
      break;
    case PROP_WEIGHTED_PRED:
      if (!device_caps->weighted_prediction) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      } else {
        g_value_set_boolean (value, nvenc->weighted_pred);
      }
      break;
    case PROP_VBV_BUFFER_SIZE:
      if (!device_caps->custom_vbv_bufsize) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      } else {
        g_value_set_uint (value, nvenc->vbv_buffersize);
      }
      break;
    case PROP_RC_LOOKAHEAD:
      if (!device_caps->lookahead) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      } else {
        g_value_set_uint (value, nvenc->rc_lookahead);
      }
      break;
    case PROP_TEMPORAL_AQ:
      if (!device_caps->temporal_aq) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      } else {
        g_value_set_boolean (value, nvenc->temporal_aq);
      }
      break;
    case PROP_BFRAMES:
      if (!device_caps->bframes) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      } else {
        g_value_set_uint (value, nvenc->bframes);
      }
      break;
    case PROP_B_ADAPT:
      if (!device_caps->bframes) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      } else {
        g_value_set_boolean (value, nvenc->b_adapt);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

void
gst_nv_h265_enc_register (GstPlugin * plugin, guint device_id, guint rank,
    GstCaps * sink_caps, GstCaps * src_caps, GstNvEncDeviceCaps * device_caps)
{
  GType parent_type;
  GType type;
  gchar *type_name;
  gchar *feature_name;
  GstNvH265EncClassData *cdata;
  gboolean is_default = TRUE;
  GTypeInfo type_info = {
    sizeof (GstNvH265EncClass),
    NULL,
    NULL,
    (GClassInitFunc) gst_nv_h265_enc_class_init,
    NULL,
    NULL,
    sizeof (GstNvH265Enc),
    0,
    (GInstanceInitFunc) gst_nv_h265_enc_init,
  };

  parent_type = gst_nv_base_enc_register ("H265", device_id, device_caps);

  cdata = g_new0 (GstNvH265EncClassData, 1);
  cdata->sink_caps = gst_caps_ref (sink_caps);
  cdata->src_caps = gst_caps_ref (src_caps);
  type_info.class_data = cdata;

  type_name = g_strdup ("GstNvH265Enc");
  feature_name = g_strdup ("nvh265enc");

  if (g_type_from_name (type_name) != 0) {
    g_free (type_name);
    g_free (feature_name);
    type_name = g_strdup_printf ("GstNvH265Device%dEnc", device_id);
    feature_name = g_strdup_printf ("nvh265device%denc", device_id);
    is_default = FALSE;
  }

  cdata->is_default = is_default;
  type = g_type_register_static (parent_type, type_name, &type_info, 0);

  /* make lower rank than default device */
  if (rank > 0 && !is_default)
    rank--;

  if (!gst_element_register (plugin, feature_name, rank, type))
    GST_WARNING ("Failed to register plugin '%s'", type_name);

  g_free (type_name);
  g_free (feature_name);
}
