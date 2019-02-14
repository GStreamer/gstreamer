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

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_nv_h265_enc_debug);
#define GST_CAT_DEFAULT gst_nv_h265_enc_debug

#if HAVE_NVENC_GST_GL
#include <cuda.h>
#include <cuda_runtime_api.h>
#include <cuda_gl_interop.h>
#include <gst/gl/gl.h>
#endif

#define parent_class gst_nv_h265_enc_parent_class
G_DEFINE_TYPE (GstNvH265Enc, gst_nv_h265_enc, GST_TYPE_NV_BASE_ENC);

#if HAVE_NVENC_GST_GL
#define GL_CAPS_STR \
  ";" \
  "video/x-raw(memory:GLMemory), " \
  "format = (string) { NV12, Y444 }, " \
  "width = (int) [ 16, 4096 ], height = (int) [ 16, 2160 ], " \
  "framerate = (fraction) [0, MAX] "
#else
#define GL_CAPS_STR ""
#endif

/* *INDENT-OFF* */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, " "format = (string) { NV12, I420 }, "       // TODO: YV12, Y444 support
        "width = (int) [ 16, 4096 ], height = (int) [ 16, 2160 ], "
        "framerate = (fraction) [0, MAX] "
        GL_CAPS_STR
    ));

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h265, "
        "width = (int) [ 1, 4096 ], height = (int) [ 1, 2160 ], "
        "framerate = (fraction) [0/1, MAX], "
        "stream-format = (string) byte-stream, "
        "alignment = (string) au, "
        "profile = (string) { main }") // TODO: a couple of others
    );
/* *INDENT-ON* */

static gboolean gst_nv_h265_enc_open (GstVideoEncoder * enc);
static gboolean gst_nv_h265_enc_close (GstVideoEncoder * enc);
static GstCaps *gst_nv_h265_enc_getcaps (GstVideoEncoder * enc,
    GstCaps * filter);
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
gst_nv_h265_enc_class_init (GstNvH265EncClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoEncoderClass *videoenc_class = GST_VIDEO_ENCODER_CLASS (klass);
  GstNvBaseEncClass *nvenc_class = GST_NV_BASE_ENC_CLASS (klass);

  gobject_class->set_property = gst_nv_h265_enc_set_property;
  gobject_class->get_property = gst_nv_h265_enc_get_property;
  gobject_class->finalize = gst_nv_h265_enc_finalize;

  videoenc_class->open = GST_DEBUG_FUNCPTR (gst_nv_h265_enc_open);
  videoenc_class->close = GST_DEBUG_FUNCPTR (gst_nv_h265_enc_close);

  videoenc_class->getcaps = GST_DEBUG_FUNCPTR (gst_nv_h265_enc_getcaps);

  nvenc_class->codec_id = NV_ENC_CODEC_HEVC_GUID;
  nvenc_class->set_encoder_config = gst_nv_h265_enc_set_encoder_config;
  nvenc_class->set_src_caps = gst_nv_h265_enc_set_src_caps;
  nvenc_class->set_pic_params = gst_nv_h265_enc_set_pic_params;

  gst_element_class_add_static_pad_template (element_class, &sink_factory);
  gst_element_class_add_static_pad_template (element_class, &src_factory);

  gst_element_class_set_static_metadata (element_class,
      "NVENC HEVC Video Encoder",
      "Codec/Encoder/Video/Hardware",
      "Encode HEVC video streams using NVIDIA's hardware-accelerated NVENC encoder API",
      "Tim-Philipp Müller <tim@centricular.com>, "
      "Matthew Waters <matthew@centricular.com>, "
      "Seungha Yang <pudding8757@gmail.com>");

  GST_DEBUG_CATEGORY_INIT (gst_nv_h265_enc_debug,
      "nvh265enc", 0, "Nvidia HEVC encoder");
}

static void
gst_nv_h265_enc_init (GstNvH265Enc * nvenc)
{
}

static void
gst_nv_h265_enc_finalize (GObject * obj)
{
  G_OBJECT_CLASS (gst_nv_h265_enc_parent_class)->finalize (obj);
}

static gboolean
_get_supported_profiles (GstNvH265Enc * nvenc)
{
  NVENCSTATUS nv_ret;
  GUID profile_guids[64];
  GValue list = G_VALUE_INIT;
  GValue val = G_VALUE_INIT;
  guint i, n, n_profiles;

  nv_ret =
      NvEncGetEncodeProfileGUIDCount (GST_NV_BASE_ENC (nvenc)->encoder,
      NV_ENC_CODEC_HEVC_GUID, &n);
  if (nv_ret != NV_ENC_SUCCESS)
    return FALSE;

  nv_ret =
      NvEncGetEncodeProfileGUIDs (GST_NV_BASE_ENC (nvenc)->encoder,
      NV_ENC_CODEC_HEVC_GUID, profile_guids, G_N_ELEMENTS (profile_guids), &n);
  if (nv_ret != NV_ENC_SUCCESS)
    return FALSE;

  n_profiles = 0;
  g_value_init (&list, GST_TYPE_LIST);
  for (i = 0; i < n; i++) {
    g_value_init (&val, G_TYPE_STRING);

    if (gst_nvenc_cmp_guid (profile_guids[i], NV_ENC_HEVC_PROFILE_MAIN_GUID)) {
      g_value_set_static_string (&val, "main");
      gst_value_list_append_value (&list, &val);
      n_profiles++;
    }
    /* TODO: map MAIN10, FREXT */

    g_value_unset (&val);
  }

  if (n_profiles == 0)
    return FALSE;

  GST_OBJECT_LOCK (nvenc);
  g_free (nvenc->supported_profiles);
  nvenc->supported_profiles = g_memdup (&list, sizeof (GValue));
  GST_OBJECT_UNLOCK (nvenc);

  return TRUE;
}

static gboolean
gst_nv_h265_enc_open (GstVideoEncoder * enc)
{
  GstNvH265Enc *nvenc = GST_NV_H265_ENC (enc);

  if (!GST_VIDEO_ENCODER_CLASS (gst_nv_h265_enc_parent_class)->open (enc))
    return FALSE;

  /* Check if HEVC is supported */
  {
    uint32_t i, num = 0;
    GUID guids[16];

    NvEncGetEncodeGUIDs (GST_NV_BASE_ENC (nvenc)->encoder, guids,
        G_N_ELEMENTS (guids), &num);

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

  /* query supported input formats */
  if (!_get_supported_profiles (nvenc)) {
    GST_WARNING_OBJECT (nvenc, "No supported encoding profiles");
    gst_nv_h265_enc_close (enc);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_nv_h265_enc_close (GstVideoEncoder * enc)
{
  GstNvH265Enc *nvenc = GST_NV_H265_ENC (enc);

  GST_OBJECT_LOCK (nvenc);
  g_free (nvenc->supported_profiles);
  nvenc->supported_profiles = NULL;
  GST_OBJECT_UNLOCK (nvenc);

  return GST_VIDEO_ENCODER_CLASS (gst_nv_h265_enc_parent_class)->close (enc);
}

static GstCaps *
gst_nv_h265_enc_getcaps (GstVideoEncoder * enc, GstCaps * filter)
{
  GstNvH265Enc *nvenc = GST_NV_H265_ENC (enc);
  GstCaps *supported_incaps = NULL;
  GstCaps *template_caps, *caps;
  GValue *input_formats = GST_NV_BASE_ENC (enc)->input_formats;

  GST_OBJECT_LOCK (nvenc);

  if (input_formats != NULL) {
    template_caps = gst_pad_get_pad_template_caps (enc->sinkpad);
    supported_incaps = gst_caps_copy (template_caps);
    gst_caps_set_value (supported_incaps, "format", input_formats);

    GST_LOG_OBJECT (enc, "codec input caps %" GST_PTR_FORMAT, supported_incaps);
    GST_LOG_OBJECT (enc, "   template caps %" GST_PTR_FORMAT, template_caps);
    caps = gst_caps_intersect (template_caps, supported_incaps);
    gst_caps_unref (template_caps);
    gst_caps_unref (supported_incaps);
    supported_incaps = caps;
    GST_LOG_OBJECT (enc, "  supported caps %" GST_PTR_FORMAT, supported_incaps);
  }

  GST_OBJECT_UNLOCK (nvenc);

  caps = gst_video_encoder_proxy_getcaps (enc, supported_incaps, filter);

  if (supported_incaps)
    gst_caps_unref (supported_incaps);

  GST_DEBUG_OBJECT (nvenc, "  returning caps %" GST_PTR_FORMAT, caps);

  return caps;
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

  spp.version = NV_ENC_SEQUENCE_PARAM_PAYLOAD_VER;
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
  GstNvH265Enc *h265enc = GST_NV_H265_ENC (nvenc);
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

static gboolean
gst_nv_h265_enc_set_encoder_config (GstNvBaseEnc * nvenc,
    GstVideoCodecState * state, NV_ENC_CONFIG * config)
{
  GstNvH265Enc *h265enc = GST_NV_H265_ENC (nvenc);
  GstCaps *allowed_caps, *template_caps;
  GUID selected_profile = NV_ENC_CODEC_PROFILE_AUTOSELECT_GUID;
  int level_idc = NV_ENC_LEVEL_AUTOSELECT;

  template_caps = gst_static_pad_template_get_caps (&src_factory);
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
  config->encodeCodecConfig.hevcConfig.level = level_idc;
  config->encodeCodecConfig.hevcConfig.idrPeriod = config->gopLength;

  /* FIXME: make property */
  config->encodeCodecConfig.hevcConfig.outputAUD = 1;

  return TRUE;
}

static gboolean
gst_nv_h265_enc_set_pic_params (GstNvBaseEnc * enc, GstVideoCodecFrame * frame,
    NV_ENC_PIC_PARAMS * pic_params)
{
  /* encode whole picture in one single slice */
  pic_params->codecPicParams.hevcPicParams.sliceMode = 0;
  pic_params->codecPicParams.hevcPicParams.sliceModeData = 0;

  return TRUE;
}

static void
gst_nv_h265_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_nv_h265_enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
