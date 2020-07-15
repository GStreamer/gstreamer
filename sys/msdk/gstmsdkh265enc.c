/* GStreamer Intel MSDK plugin
 * Copyright (c) 2016, Oblong Industries, Inc.
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
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/allocators/gstdmabuf.h>

#include "gstmsdkh265enc.h"

GST_DEBUG_CATEGORY_EXTERN (gst_msdkh265enc_debug);
#define GST_CAT_DEFAULT gst_msdkh265enc_debug

enum
{
#ifndef GST_REMOVE_DEPRECATED
  PROP_LOW_POWER = GST_MSDKENC_PROP_MAX,
  PROP_TILE_ROW,
#else
  PROP_TILE_ROW = GST_MSDKENC_PROP_MAX,
#endif
  PROP_TILE_COL,
  PROP_MAX_SLICE_SIZE,
  PROP_TUNE_MODE,
};

enum
{
  GST_MSDK_FLAG_LOW_POWER = 1 << 0,
  GST_MSDK_FLAG_TUNE_MODE = 1 << 1,
};

#define PROP_LOWPOWER_DEFAULT           FALSE
#define PROP_TILE_ROW_DEFAULT           1
#define PROP_TILE_COL_DEFAULT           1
#define PROP_MAX_SLICE_SIZE_DEFAULT     0
#define PROP_TUNE_MODE_DEFAULT          MFX_CODINGOPTION_UNKNOWN

#define RAW_FORMATS "NV12, I420, YV12, YUY2, UYVY, BGRA, P010_10LE, VUYA"
#define PROFILES    "main, main-10, main-444"
#define COMMON_FORMAT "{ " RAW_FORMATS " }"
#define PRFOLIE_STR   "{ " PROFILES " }"


#if (MFX_VERSION >= 1027)
#undef  COMMON_FORMAT
#undef  PRFOLIE_STR
#define FORMATS_1027    RAW_FORMATS ", Y410, Y210"
#define PROFILES_1027   PROFILES ", main-444-10, main-422-10"
#define COMMON_FORMAT   "{ " FORMATS_1027 " }"
#define PRFOLIE_STR     "{ " PROFILES_1027 " }"
#endif

#if (MFX_VERSION >= 1031)
#undef  COMMON_FORMAT
#undef  PRFOLIE_STR
#define FORMATS_1031    FORMATS_1027 ", P012_LE"
#define PROFILES_1031   PROFILES_1027  ", main-12"
#define COMMON_FORMAT   "{ " FORMATS_1031 " }"
#define PRFOLIE_STR     "{ " PROFILES_1031 " }"
#endif

#if (MFX_VERSION >= 1032)
#undef  COMMON_FORMAT
#undef  PRFOLIE_STR
#define FORMATS_1032    FORMATS_1031
#define PROFILES_1032   PROFILES_1031  ", screen-extended-main, " \
  "screen-extended-main-10, screen-extended-main-444, " \
  "screen-extended-main-444-10"
#define COMMON_FORMAT   "{ " FORMATS_1032 " }"
#define PRFOLIE_STR     "{ " PROFILES_1032 " }"
#endif

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_MSDK_CAPS_STR (COMMON_FORMAT,
            "{ NV12, P010_10LE }")));

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h265, "
        "framerate = (fraction) [0/1, MAX], "
        "width = (int) [ 1, MAX ], height = (int) [ 1, MAX ], "
        "stream-format = (string) byte-stream , alignment = (string) au , "
        "profile = (string) " PRFOLIE_STR)
    );

#define gst_msdkh265enc_parent_class parent_class
G_DEFINE_TYPE (GstMsdkH265Enc, gst_msdkh265enc, GST_TYPE_MSDKENC);

static void
gst_msdkh265enc_insert_sei (GstMsdkH265Enc * thiz, GstVideoCodecFrame * frame,
    GstMemory * sei_mem)
{
  GstBuffer *new_buffer;

  if (!thiz->parser)
    thiz->parser = gst_h265_parser_new ();

  new_buffer = gst_h265_parser_insert_sei (thiz->parser,
      frame->output_buffer, sei_mem);

  if (!new_buffer) {
    GST_WARNING_OBJECT (thiz, "Cannot insert SEI nal into AU buffer");
    return;
  }

  gst_buffer_unref (frame->output_buffer);
  frame->output_buffer = new_buffer;
}

static void
gst_msdkh265enc_add_cc (GstMsdkH265Enc * thiz, GstVideoCodecFrame * frame)
{
  GstVideoCaptionMeta *cc_meta;
  gpointer iter = NULL;
  GstBuffer *in_buf = frame->input_buffer;
  GstMemory *mem = NULL;

  if (thiz->cc_sei_array)
    g_array_set_size (thiz->cc_sei_array, 0);

  while ((cc_meta =
          (GstVideoCaptionMeta *) gst_buffer_iterate_meta_filtered (in_buf,
              &iter, GST_VIDEO_CAPTION_META_API_TYPE))) {
    GstH265SEIMessage sei;
    GstH265RegisteredUserData *rud;
    guint8 *data;

    if (cc_meta->caption_type != GST_VIDEO_CAPTION_TYPE_CEA708_RAW)
      continue;

    memset (&sei, 0, sizeof (GstH265SEIMessage));
    sei.payloadType = GST_H265_SEI_REGISTERED_USER_DATA;
    rud = &sei.payload.registered_user_data;

    rud->country_code = 181;
    rud->size = cc_meta->size + 10;

    data = g_malloc (rud->size);
    memcpy (data + 9, cc_meta->data, cc_meta->size);

    data[0] = 0;                /* 16-bits itu_t_t35_provider_code */
    data[1] = 49;
    data[2] = 'G';              /* 32-bits ATSC_user_identifier */
    data[3] = 'A';
    data[4] = '9';
    data[5] = '4';
    data[6] = 3;                /* 8-bits ATSC1_data_user_data_type_code */
    /* 8-bits:
     * 1 bit process_em_data_flag (0)
     * 1 bit process_cc_data_flag (1)
     * 1 bit additional_data_flag (0)
     * 5-bits cc_count
     */
    data[7] = ((cc_meta->size / 3) & 0x1f) | 0x40;
    data[8] = 255;              /* 8 bits em_data, unused */
    data[cc_meta->size + 9] = 255;      /* 8 marker bits */

    rud->data = data;

    if (!thiz->cc_sei_array) {
      thiz->cc_sei_array =
          g_array_new (FALSE, FALSE, sizeof (GstH265SEIMessage));
      g_array_set_clear_func (thiz->cc_sei_array,
          (GDestroyNotify) gst_h265_sei_free);
    }

    g_array_append_val (thiz->cc_sei_array, sei);
  }

  if (!thiz->cc_sei_array || !thiz->cc_sei_array->len)
    return;

  /* layer_id and temporal_id will be updated by parser later */
  mem = gst_h265_create_sei_memory (0, 1, 4, thiz->cc_sei_array);

  if (!mem) {
    GST_WARNING_OBJECT (thiz, "Cannot create SEI nal unit");
    return;
  }

  GST_DEBUG_OBJECT (thiz,
      "Inserting %d closed caption SEI message(s)", thiz->cc_sei_array->len);

  gst_msdkh265enc_insert_sei (thiz, frame, mem);
  gst_memory_unref (mem);
}

static GstFlowReturn
gst_msdkh265enc_pre_push (GstVideoEncoder * encoder, GstVideoCodecFrame * frame)
{
  GstMsdkH265Enc *thiz = GST_MSDKH265ENC (encoder);

  gst_msdkh265enc_add_cc (thiz, frame);

  return GST_FLOW_OK;
}

static gboolean
gst_msdkh265enc_set_format (GstMsdkEnc * encoder)
{
  GstMsdkH265Enc *thiz = GST_MSDKH265ENC (encoder);
  GstCaps *template_caps, *allowed_caps;

  g_free (thiz->profile_name);
  thiz->profile_name = NULL;

  allowed_caps = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder));

  if (!allowed_caps || gst_caps_is_empty (allowed_caps)) {
    if (allowed_caps)
      gst_caps_unref (allowed_caps);
    return FALSE;
  }

  template_caps = gst_static_pad_template_get_caps (&src_factory);

  if (gst_caps_is_equal (allowed_caps, template_caps)) {
    GST_INFO_OBJECT (thiz,
        "downstream have the same caps, profile set to auto");
  } else {
    GstStructure *s;
    const gchar *profile;

    allowed_caps = gst_caps_make_writable (allowed_caps);
    allowed_caps = gst_caps_fixate (allowed_caps);
    s = gst_caps_get_structure (allowed_caps, 0);
    profile = gst_structure_get_string (s, "profile");

    if (profile) {
      thiz->profile_name = g_strdup (profile);
    }
  }

  gst_caps_unref (allowed_caps);
  gst_caps_unref (template_caps);

  return TRUE;
}

static gboolean
gst_msdkh265enc_configure (GstMsdkEnc * encoder)
{
  GstMsdkH265Enc *h265enc = GST_MSDKH265ENC (encoder);
  mfxSession session;
  const mfxPluginUID *uid;

  session = gst_msdk_context_get_session (encoder->context);

  if (encoder->hardware)
    uid = &MFX_PLUGINID_HEVCE_HW;
  else
    uid = &MFX_PLUGINID_HEVCE_SW;

  if (!gst_msdk_load_plugin (session, uid, 1, "msdkh265enc"))
    return FALSE;

  encoder->param.mfx.CodecId = MFX_CODEC_HEVC;

  if (h265enc->profile_name) {
    encoder->param.mfx.CodecProfile = MFX_PROFILE_HEVC_MAIN;

    if (!strcmp (h265enc->profile_name, "main-10"))
      encoder->param.mfx.CodecProfile = MFX_PROFILE_HEVC_MAIN10;
    else if (!strcmp (h265enc->profile_name, "main-444") ||
        !strcmp (h265enc->profile_name, "main-422-10") ||
        !strcmp (h265enc->profile_name, "main-444-10") ||
        !strcmp (h265enc->profile_name, "main-12"))
      encoder->param.mfx.CodecProfile = MFX_PROFILE_HEVC_REXT;

#if (MFX_VERSION >= 1032)
    else if (!strcmp (h265enc->profile_name, "screen-extended-main") ||
        !strcmp (h265enc->profile_name, "screen-extended-main-10") ||
        !strcmp (h265enc->profile_name, "screen-extended-main-444") ||
        !strcmp (h265enc->profile_name, "screen-extended-main-444-10"))
      encoder->param.mfx.CodecProfile = MFX_PROFILE_HEVC_SCC;
#endif
  } else {
    switch (encoder->param.mfx.FrameInfo.FourCC) {
      case MFX_FOURCC_P010:
        encoder->param.mfx.CodecProfile = MFX_PROFILE_HEVC_MAIN10;
        break;
      case MFX_FOURCC_AYUV:
      case MFX_FOURCC_YUY2:
#if (MFX_VERSION >= 1027)
      case MFX_FOURCC_Y410:
      case MFX_FOURCC_Y210:
#endif
#if (MFX_VERSION >= 1031)
      case MFX_FOURCC_P016:
#endif
        encoder->param.mfx.CodecProfile = MFX_PROFILE_HEVC_REXT;
        break;
      default:
        encoder->param.mfx.CodecProfile = MFX_PROFILE_HEVC_MAIN;
    }
  }

  /* IdrInterval field of MediaSDK HEVC encoder behaves differently
   * than other encoders. IdrInteval == 1 indicate every
   * I-frame should be an IDR, IdrInteval == 2 means every other
   * I-frame is an IDR etc. So we generalize the behaviour of property
   * "i-frames" by incrementing the value by one in each case*/
  encoder->param.mfx.IdrInterval += 1;

  /* Enable Extended coding options */
  encoder->option2.MaxSliceSize = h265enc->max_slice_size;
  gst_msdkenc_ensure_extended_coding_options (encoder);

  if (h265enc->num_tile_rows > 1 || h265enc->num_tile_cols > 1) {
    h265enc->ext_tiles.Header.BufferId = MFX_EXTBUFF_HEVC_TILES;
    h265enc->ext_tiles.Header.BufferSz = sizeof (h265enc->ext_tiles);
    h265enc->ext_tiles.NumTileRows = h265enc->num_tile_rows;
    h265enc->ext_tiles.NumTileColumns = h265enc->num_tile_cols;

    gst_msdkenc_add_extra_param (encoder,
        (mfxExtBuffer *) & h265enc->ext_tiles);

    /* Set a valid value to NumSlice */
    if (encoder->param.mfx.NumSlice == 0)
      encoder->param.mfx.NumSlice =
          h265enc->num_tile_rows * h265enc->num_tile_cols;
  }

  encoder->param.mfx.LowPower = h265enc->tune_mode;

  return TRUE;
}

static inline const gchar *
level_to_string (gint level)
{
  switch (level) {
    case MFX_LEVEL_HEVC_1:
      return "1";
    case MFX_LEVEL_HEVC_2:
      return "2";
    case MFX_LEVEL_HEVC_21:
      return "2.1";
    case MFX_LEVEL_HEVC_3:
      return "3";
    case MFX_LEVEL_HEVC_31:
      return "3.1";
    case MFX_LEVEL_HEVC_4:
      return "4";
    case MFX_LEVEL_HEVC_41:
      return "4.1";
    case MFX_LEVEL_HEVC_5:
      return "5";
    case MFX_LEVEL_HEVC_51:
      return "5.1";
    case MFX_LEVEL_HEVC_52:
      return "5.2";
    case MFX_LEVEL_HEVC_6:
      return "6";
    case MFX_LEVEL_HEVC_61:
      return "6.1";
    case MFX_LEVEL_HEVC_62:
      return "6.2";
    default:
      break;
  }

  return NULL;
}

static GstCaps *
gst_msdkh265enc_set_src_caps (GstMsdkEnc * encoder)
{
  GstMsdkH265Enc *thiz = GST_MSDKH265ENC (encoder);
  GstCaps *caps;
  GstStructure *structure;
  const gchar *level;

  caps = gst_caps_new_empty_simple ("video/x-h265");
  structure = gst_caps_get_structure (caps, 0);

  gst_structure_set (structure, "stream-format", G_TYPE_STRING, "byte-stream",
      NULL);

  gst_structure_set (structure, "alignment", G_TYPE_STRING, "au", NULL);

  if (thiz->profile_name)
    gst_structure_set (structure, "profile", G_TYPE_STRING, thiz->profile_name,
        NULL);
  else {
    switch (encoder->param.mfx.FrameInfo.FourCC) {
      case MFX_FOURCC_P010:
        gst_structure_set (structure, "profile", G_TYPE_STRING, "main-10",
            NULL);
        break;
      case MFX_FOURCC_AYUV:
        gst_structure_set (structure, "profile", G_TYPE_STRING, "main-444",
            NULL);
        break;
      case MFX_FOURCC_YUY2:
        /* The profile is main-422-10 for 8-bit 422 */
        gst_structure_set (structure, "profile", G_TYPE_STRING, "main-422-10",
            NULL);
        break;
#if (MFX_VERSION >= 1027)
      case MFX_FOURCC_Y410:
        gst_structure_set (structure, "profile", G_TYPE_STRING, "main-444-10",
            NULL);
        break;
      case MFX_FOURCC_Y210:
        gst_structure_set (structure, "profile", G_TYPE_STRING, "main-422-10",
            NULL);
        break;
#endif
#if (MFX_VERSION >= 1031)
      case MFX_FOURCC_P016:
        gst_structure_set (structure, "profile", G_TYPE_STRING, "main-12",
            NULL);
        break;
#endif
      default:
        gst_structure_set (structure, "profile", G_TYPE_STRING, "main", NULL);
        break;
    }
  }

  level = level_to_string (encoder->param.mfx.CodecLevel);
  if (level)
    gst_structure_set (structure, "level", G_TYPE_STRING, level, NULL);

  return caps;
}

static void
gst_msdkh265enc_finalize (GObject * object)
{
  GstMsdkH265Enc *thiz = GST_MSDKH265ENC (object);

  if (thiz->parser)
    gst_h265_parser_free (thiz->parser);
  if (thiz->cc_sei_array)
    g_array_unref (thiz->cc_sei_array);

  g_free (thiz->profile_name);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_msdkh265enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMsdkH265Enc *thiz = GST_MSDKH265ENC (object);

  if (gst_msdkenc_set_common_property (object, prop_id, value, pspec))
    return;

  GST_OBJECT_LOCK (thiz);

  switch (prop_id) {
#ifndef GST_REMOVE_DEPRECATED
    case PROP_LOW_POWER:
      thiz->lowpower = g_value_get_boolean (value);
      thiz->prop_flag |= GST_MSDK_FLAG_LOW_POWER;

      /* Ignore it if user set tune mode explicitly */
      if (!(thiz->prop_flag & GST_MSDK_FLAG_TUNE_MODE))
        thiz->tune_mode =
            thiz->lowpower ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF;

      break;
#endif

    case PROP_TILE_ROW:
      thiz->num_tile_rows = g_value_get_uint (value);
      break;

    case PROP_TILE_COL:
      thiz->num_tile_cols = g_value_get_uint (value);
      break;

    case PROP_MAX_SLICE_SIZE:
      thiz->max_slice_size = g_value_get_uint (value);
      break;

    case PROP_TUNE_MODE:
      thiz->tune_mode = g_value_get_enum (value);
      thiz->prop_flag |= GST_MSDK_FLAG_TUNE_MODE;
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (thiz);
}

static void
gst_msdkh265enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMsdkH265Enc *thiz = GST_MSDKH265ENC (object);

  if (gst_msdkenc_get_common_property (object, prop_id, value, pspec))
    return;

  GST_OBJECT_LOCK (thiz);
  switch (prop_id) {
#ifndef GST_REMOVE_DEPRECATED
    case PROP_LOW_POWER:
      g_value_set_boolean (value, thiz->lowpower);
      break;
#endif

    case PROP_TILE_ROW:
      g_value_set_uint (value, thiz->num_tile_rows);
      break;

    case PROP_TILE_COL:
      g_value_set_uint (value, thiz->num_tile_cols);
      break;

    case PROP_MAX_SLICE_SIZE:
      g_value_set_uint (value, thiz->max_slice_size);
      break;

    case PROP_TUNE_MODE:
      g_value_set_enum (value, thiz->tune_mode);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (thiz);
}

static gboolean
gst_msdkh265enc_need_reconfig (GstMsdkEnc * encoder, GstVideoCodecFrame * frame)
{
  GstMsdkH265Enc *h265enc = GST_MSDKH265ENC (encoder);

  return gst_msdkenc_get_roi_params (encoder, frame, h265enc->roi);
}

static void
gst_msdkh265enc_set_extra_params (GstMsdkEnc * encoder,
    GstVideoCodecFrame * frame)
{
  GstMsdkH265Enc *h265enc = GST_MSDKH265ENC (encoder);

  if (h265enc->roi[0].NumROI)
    gst_msdkenc_add_extra_param (encoder, (mfxExtBuffer *) & h265enc->roi[0]);
}

static gboolean
gst_msdkh265enc_need_conversion (GstMsdkEnc * encoder, GstVideoInfo * info,
    GstVideoFormat * out_format)
{
  GstMsdkH265Enc *h265enc = GST_MSDKH265ENC (encoder);

  switch (GST_VIDEO_INFO_FORMAT (info)) {
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_P010_10LE:
    case GST_VIDEO_FORMAT_VUYA:
#if (MFX_VERSION >= 1027)
    case GST_VIDEO_FORMAT_Y410:
    case GST_VIDEO_FORMAT_Y210:
#endif
#if (MFX_VERSION >= 1031)
    case GST_VIDEO_FORMAT_P012_LE:
#endif
      return FALSE;

    case GST_VIDEO_FORMAT_YUY2:
#if (MFX_VERSION >= 1027)
      if (encoder->codename >= MFX_PLATFORM_ICELAKE &&
          h265enc->tune_mode == MFX_CODINGOPTION_OFF)
        return FALSE;
#endif
    default:
      if (GST_VIDEO_INFO_COMP_DEPTH (info, 0) == 10)
        *out_format = GST_VIDEO_FORMAT_P010_10LE;
      else
        *out_format = GST_VIDEO_FORMAT_NV12;
      return TRUE;
  }
}

static void
gst_msdkh265enc_class_init (GstMsdkH265EncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstVideoEncoderClass *videoencoder_class;
  GstMsdkEncClass *encoder_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  videoencoder_class = GST_VIDEO_ENCODER_CLASS (klass);
  encoder_class = GST_MSDKENC_CLASS (klass);

  gobject_class->finalize = gst_msdkh265enc_finalize;
  gobject_class->set_property = gst_msdkh265enc_set_property;
  gobject_class->get_property = gst_msdkh265enc_get_property;

  videoencoder_class->pre_push = gst_msdkh265enc_pre_push;

  encoder_class->set_format = gst_msdkh265enc_set_format;
  encoder_class->configure = gst_msdkh265enc_configure;
  encoder_class->set_src_caps = gst_msdkh265enc_set_src_caps;
  encoder_class->need_reconfig = gst_msdkh265enc_need_reconfig;
  encoder_class->set_extra_params = gst_msdkh265enc_set_extra_params;
  encoder_class->need_conversion = gst_msdkh265enc_need_conversion;

  gst_msdkenc_install_common_properties (encoder_class);

#ifndef GST_REMOVE_DEPRECATED
  g_object_class_install_property (gobject_class, PROP_LOW_POWER,
      g_param_spec_boolean ("low-power", "Low power",
          "Enable low power mode (DEPRECATED, use tune instead)",
          PROP_LOWPOWER_DEFAULT,
          G_PARAM_DEPRECATED | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif

  g_object_class_install_property (gobject_class, PROP_TILE_ROW,
      g_param_spec_uint ("num-tile-rows", "number of rows for tiled encoding",
          "number of rows for tiled encoding",
          1, 8192, PROP_TILE_ROW_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TILE_COL,
      g_param_spec_uint ("num-tile-cols",
          "number of columns for tiled encoding",
          "number of columns for tiled encoding", 1, 8192,
          PROP_TILE_COL_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_SLICE_SIZE,
      g_param_spec_uint ("max-slice-size", "Max Slice Size",
          "Maximum slice size in bytes (if enabled MSDK will ignore the control over num-slices)",
          0, G_MAXUINT32, PROP_MAX_SLICE_SIZE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TUNE_MODE,
      g_param_spec_enum ("tune", "Encoder tuning",
          "Encoder tuning option",
          gst_msdkenc_tune_mode_get_type (), PROP_TUNE_MODE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element_class,
      "Intel MSDK H265 encoder",
      "Codec/Encoder/Video/Hardware",
      "H265 video encoder based on Intel Media SDK",
      "Josep Torra <jtorra@oblong.com>");

  gst_element_class_add_static_pad_template (element_class, &sink_factory);
  gst_element_class_add_static_pad_template (element_class, &src_factory);
}

static void
gst_msdkh265enc_init (GstMsdkH265Enc * thiz)
{
  GstMsdkEnc *msdk_enc = (GstMsdkEnc *) thiz;
  thiz->lowpower = PROP_LOWPOWER_DEFAULT;
  thiz->num_tile_rows = PROP_TILE_ROW_DEFAULT;
  thiz->num_tile_cols = PROP_TILE_COL_DEFAULT;
  thiz->max_slice_size = PROP_MAX_SLICE_SIZE_DEFAULT;
  thiz->tune_mode = PROP_TUNE_MODE_DEFAULT;
  msdk_enc->num_extra_frames = 1;
}
