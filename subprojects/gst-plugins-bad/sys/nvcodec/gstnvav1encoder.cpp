/* GStreamer
 * Copyright (C) 2024 Seungha Yang <seungha@centricular.com>
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

#include "gstnvav1encoder.h"
#include <string>
#include <set>
#include <string.h>
#include <vector>

GST_DEBUG_CATEGORY_STATIC (gst_nv_av1_encoder_debug);
#define GST_CAT_DEFAULT gst_nv_av1_encoder_debug

static GTypeClass *parent_class = nullptr;

enum
{
  PROP_0,
  PROP_ADAPTER_LUID,
  PROP_CUDA_DEVICE_ID,

  /* init params */
  PROP_PRESET,
  PROP_TUNE,
  PROP_MULTI_PASS,
  PROP_WEIGHTED_PRED,

  /* encoding config */
  PROP_GOP_SIZE,
  PROP_B_FRAMES,

  /* rate-control params */
  PROP_RATE_CONTROL,

  PROP_QP_CONST_I,
  PROP_QP_CONST_P,
  PROP_QP_CONST_B,

  PROP_BITRATE,
  PROP_MAX_BITRATE,
  PROP_VBV_BUFFER_SIZE,

  PROP_RC_LOOKAHEAD,
  PROP_I_ADAPT,
  PROP_B_ADAPT,
  PROP_SPATIAL_AQ,
  PROP_TEMPORAL_AQ,
  PROP_ZEROLATENCY,
  PROP_NON_REF_P,
  PROP_STRICT_GOP,
  PROP_AQ_STRENGTH,

  PROP_QP_MIN_I,
  PROP_QP_MIN_P,
  PROP_QP_MIN_B,

  PROP_QP_MAX_I,
  PROP_QP_MAX_P,
  PROP_QP_MAX_B,

  PROP_CONST_QUALITY,
};

#define DEFAULT_PRESET            GST_NV_ENCODER_PRESET_DEFAULT
#define DEFAULT_TUNE              GST_NV_ENCODER_TUNE_DEFAULT
#define DEFAULT_MULTI_PASS        GST_NV_ENCODER_MULTI_PASS_DEFAULT
#define DEFAULT_WEIGHTED_PRED     FALSE
#define DEFAULT_GOP_SIZE          75
#define DEFAULT_B_FRAMES          0
#define DEFAULT_RATE_CONTROL      GST_NV_ENCODER_RC_MODE_DEFAULT
#define DEFAULT_QP                -1
#define DEFAULT_BITRATE           0
#define DEFAULT_MAX_BITRATE       0
#define DEFAULT_VBV_BUFFER_SIZE   0
#define DEFAULT_RC_LOOKAHEAD      0
#define DEFAULT_I_ADAPT           FALSE
#define DEFAULT_B_ADAPT           FALSE
#define DEFAULT_SPATIAL_AQ        FALSE
#define DEFAULT_TEMPORAL_AQ       FALSE
#define DEFAULT_ZEROLATENCY       FALSE
#define DEFAULT_NON_REF_P         FALSE
#define DEFAULT_STRICT_GOP        FALSE
#define DEFAULT_AQ_STRENGTH       FALSE
#define DEFAULT_CONST_QUALITY     0

typedef struct _GstNvAv1Encoder
{
  GstNvEncoder parent;
  GMutex prop_lock;

  gboolean init_param_updated;
  gboolean rc_param_updated;
  gboolean bitrate_updated;

  GstNvEncoderDeviceMode selected_device_mode;

  /* Properties */
  guint cuda_device_id;
  gint64 adapter_luid;

  GstNvEncoderPreset preset;
  GstNvEncoderMultiPass multipass;
  GstNvEncoderTune tune;
  gboolean weighted_pred;

  gint gop_size;
  guint bframes;

  GstNvEncoderRCMode rc_mode;
  gint qp_const_i;
  gint qp_const_p;
  gint qp_const_b;
  guint bitrate;
  guint max_bitrate;
  guint vbv_buffer_size;
  guint rc_lookahead;
  gboolean i_adapt;
  gboolean b_adapt;
  gboolean spatial_aq;
  gboolean temporal_aq;
  gboolean zero_reorder_delay;
  gboolean non_ref_p;
  gboolean strict_gop;
  guint aq_strength;
  gint qp_min_i;
  gint qp_min_p;
  gint qp_min_b;
  gint qp_max_i;
  gint qp_max_p;
  gint qp_max_b;
  gdouble const_quality;
} GstNvAv1Encoder;

typedef struct _GstNvAv1EncoderClass
{
  GstNvEncoderClass parent_class;

  guint cuda_device_id;
  gint64 adapter_luid;

  GstNvEncoderDeviceMode device_mode;

  /* representative device caps */
  GstNvEncoderDeviceCaps device_caps;

  /* auto gpu select mode */
  guint cuda_device_id_list[8];
  guint cuda_device_id_size;

  gint64 adapter_luid_list[8];
  guint adapter_luid_size;
} GstNvAv1EncoderClass;

#define GST_NV_AV1_ENCODER(object) ((GstNvAv1Encoder *) (object))
#define GST_NV_AV1_ENCODER_GET_CLASS(object) \
    (G_TYPE_INSTANCE_GET_CLASS ((object),G_TYPE_FROM_INSTANCE (object),GstNvAv1EncoderClass))

static void gst_nv_av1_encoder_finalize (GObject * object);
static void gst_nv_av1_encoder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_nv_av1_encoder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_nv_av1_encoder_set_format (GstNvEncoder * encoder,
    GstVideoCodecState * state, gpointer session,
    NV_ENC_INITIALIZE_PARAMS * init_params, NV_ENC_CONFIG * config);
static gboolean gst_nv_av1_encoder_set_output_state (GstNvEncoder * encoder,
    GstVideoCodecState * state, gpointer session);
static GstNvEncoderReconfigure
gst_nv_av1_encoder_check_reconfigure (GstNvEncoder * encoder,
    NV_ENC_CONFIG * config);
static gboolean gst_nv_av1_encoder_select_device (GstNvEncoder * encoder,
    const GstVideoInfo * info, GstBuffer * buffer,
    GstNvEncoderDeviceData * data);
static guint gst_nv_av1_encoder_calculate_min_buffers (GstNvEncoder * encoder);

static void
gst_nv_av1_encoder_class_init (GstNvAv1EncoderClass * klass, gpointer data)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto element_class = GST_ELEMENT_CLASS (klass);
  auto nvenc_class = GST_NV_ENCODER_CLASS (klass);
  auto cdata = (GstNvEncoderClassData *) data;
  auto dev_caps = &cdata->device_caps;
  GParamFlags param_flags = (GParamFlags) (G_PARAM_READWRITE |
      GST_PARAM_MUTABLE_PLAYING | G_PARAM_STATIC_STRINGS);
  GParamFlags conditional_param_flags = (GParamFlags) (G_PARAM_READWRITE |
      GST_PARAM_CONDITIONALLY_AVAILABLE | GST_PARAM_MUTABLE_PLAYING |
      G_PARAM_STATIC_STRINGS);

  parent_class = (GTypeClass *) g_type_class_peek_parent (klass);

  object_class->finalize = gst_nv_av1_encoder_finalize;
  object_class->set_property = gst_nv_av1_encoder_set_property;
  object_class->get_property = gst_nv_av1_encoder_get_property;

  switch (cdata->device_mode) {
    case GST_NV_ENCODER_DEVICE_CUDA:
      g_object_class_install_property (object_class, PROP_CUDA_DEVICE_ID,
          g_param_spec_uint ("cuda-device-id", "CUDA Device ID",
              "CUDA device ID of associated GPU",
              0, G_MAXINT, 0,
              (GParamFlags) (GST_PARAM_DOC_SHOW_DEFAULT |
                  G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
      break;
    case GST_NV_ENCODER_DEVICE_D3D11:
      g_object_class_install_property (object_class, PROP_ADAPTER_LUID,
          g_param_spec_int64 ("adapter-luid", "Adapter LUID",
              "DXGI Adapter LUID (Locally Unique Identifier) of associated GPU",
              G_MININT64, G_MAXINT64, 0,
              (GParamFlags) (GST_PARAM_DOC_SHOW_DEFAULT |
                  G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
      break;
    case GST_NV_ENCODER_DEVICE_AUTO_SELECT:
      if (cdata->cuda_device_id_size > 0) {
        g_object_class_install_property (object_class, PROP_CUDA_DEVICE_ID,
            g_param_spec_uint ("cuda-device-id", "CUDA Device ID",
                "CUDA device ID to use",
                0, G_MAXINT, 0,
                (GParamFlags) (conditional_param_flags |
                    GST_PARAM_DOC_SHOW_DEFAULT)));
      }
      if (cdata->adapter_luid_size > 0) {
        g_object_class_install_property (object_class, PROP_ADAPTER_LUID,
            g_param_spec_int64 ("adapter-luid", "Adapter LUID",
                "DXGI Adapter LUID (Locally Unique Identifier) to use",
                G_MININT64, G_MAXINT64, 0,
                (GParamFlags) (conditional_param_flags |
                    GST_PARAM_DOC_SHOW_DEFAULT)));
      }
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  g_object_class_install_property (object_class, PROP_PRESET,
      g_param_spec_enum ("preset", "Encoding Preset",
          "Encoding Preset", GST_TYPE_NV_ENCODER_PRESET,
          DEFAULT_PRESET, param_flags));

  g_object_class_install_property (object_class, PROP_TUNE,
      g_param_spec_enum ("tune", "Tune",
          "Encoding tune", GST_TYPE_NV_ENCODER_TUNE,
          DEFAULT_TUNE, param_flags));

  g_object_class_install_property (object_class, PROP_MULTI_PASS,
      g_param_spec_enum ("multi-pass", "Multi Pass",
          "Multi pass encoding", GST_TYPE_NV_ENCODER_MULTI_PASS,
          DEFAULT_MULTI_PASS, param_flags));
  if (dev_caps->weighted_prediction) {
    g_object_class_install_property (object_class, PROP_WEIGHTED_PRED,
        g_param_spec_boolean ("weighted-pred", "Weighted Pred",
            "Enables Weighted Prediction", DEFAULT_WEIGHTED_PRED,
            conditional_param_flags));
  }
  g_object_class_install_property (object_class, PROP_GOP_SIZE,
      g_param_spec_int ("gop-size", "GOP size",
          "Number of frames between intra frames (-1 = infinite)",
          -1, G_MAXINT, DEFAULT_GOP_SIZE, param_flags));
  if (dev_caps->max_bframes > 0) {
    g_object_class_install_property (object_class, PROP_B_FRAMES,
        g_param_spec_uint ("bframes", "B Frames",
            "Number of B-frames between I and P", 0, dev_caps->max_bframes,
            DEFAULT_B_FRAMES, conditional_param_flags));
  }
  g_object_class_install_property (object_class, PROP_RATE_CONTROL,
      g_param_spec_enum ("rc-mode", "RC Mode", "Rate Control Mode",
          GST_TYPE_NV_ENCODER_RC_MODE, DEFAULT_RATE_CONTROL, param_flags));
  g_object_class_install_property (object_class, PROP_QP_CONST_I,
      g_param_spec_int ("qp-const-i", "QP I",
          "Constant QP value for I frame (-1 = default)", -1, 255,
          DEFAULT_QP, param_flags));
  g_object_class_install_property (object_class, PROP_QP_CONST_P,
      g_param_spec_int ("qp-const-p", "QP P",
          "Constant QP value for P frame (-1 = default)", -1, 255,
          DEFAULT_QP, param_flags));
  g_object_class_install_property (object_class, PROP_QP_CONST_B,
      g_param_spec_int ("qp-const-b", "QP B",
          "Constant QP value for B frame (-1 = default)", -1, 255,
          DEFAULT_QP, param_flags));
  g_object_class_install_property (object_class, PROP_BITRATE,
      g_param_spec_uint ("bitrate", "Bitrate",
          "Bitrate in kbit/sec (0 = automatic)", 0, 2000 * 1024,
          DEFAULT_BITRATE, param_flags));
  g_object_class_install_property (object_class, PROP_MAX_BITRATE,
      g_param_spec_uint ("max-bitrate", "Max Bitrate",
          "Maximum Bitrate in kbit/sec (ignored in CBR mode)", 0, 2000 * 1024,
          DEFAULT_MAX_BITRATE, param_flags));
  if (dev_caps->custom_vbv_buf_size) {
    g_object_class_install_property (object_class,
        PROP_VBV_BUFFER_SIZE,
        g_param_spec_uint ("vbv-buffer-size", "VBV Buffer Size",
            "VBV(HRD) Buffer Size in kbits (0 = NVENC default)",
            0, G_MAXUINT, DEFAULT_VBV_BUFFER_SIZE, conditional_param_flags));
  }
  if (dev_caps->lookahead) {
    g_object_class_install_property (object_class, PROP_RC_LOOKAHEAD,
        g_param_spec_uint ("rc-lookahead", "Rate Control Lookahead",
            "Number of frames for frame type lookahead",
            0, 32, DEFAULT_RC_LOOKAHEAD, conditional_param_flags));
    g_object_class_install_property (object_class, PROP_I_ADAPT,
        g_param_spec_boolean ("i-adapt", "I Adapt",
            "Enable adaptive I-frame insert when lookahead is enabled",
            DEFAULT_I_ADAPT, conditional_param_flags));
    if (dev_caps->max_bframes > 0) {
      g_object_class_install_property (object_class, PROP_B_ADAPT,
          g_param_spec_boolean ("b-adapt", "B Adapt",
              "Enable adaptive B-frame insert when lookahead is enabled",
              DEFAULT_B_ADAPT, conditional_param_flags));
    }
  }
  g_object_class_install_property (object_class, PROP_SPATIAL_AQ,
      g_param_spec_boolean ("spatial-aq", "Spatial AQ",
          "Spatial Adaptive Quantization", DEFAULT_SPATIAL_AQ, param_flags));
  if (dev_caps->temporal_aq) {
    g_object_class_install_property (object_class, PROP_TEMPORAL_AQ,
        g_param_spec_boolean ("temporal-aq", "Temporal AQ",
            "Temporal Adaptive Quantization", DEFAULT_TEMPORAL_AQ,
            conditional_param_flags));
  }
  g_object_class_install_property (object_class, PROP_ZEROLATENCY,
      g_param_spec_boolean ("zerolatency", "Zerolatency",
          "Zero latency operation (no reordering delay)",
          DEFAULT_ZEROLATENCY, param_flags));
  g_object_class_install_property (object_class, PROP_NON_REF_P,
      g_param_spec_boolean ("nonref-p", "Nonref P",
          "Automatic insertion of non-reference P-frames", DEFAULT_NON_REF_P,
          param_flags));
  g_object_class_install_property (object_class, PROP_STRICT_GOP,
      g_param_spec_boolean ("strict-gop", "Strict GOP",
          "Minimize GOP-to-GOP rate fluctuations", DEFAULT_STRICT_GOP,
          param_flags));
  g_object_class_install_property (object_class, PROP_AQ_STRENGTH,
      g_param_spec_uint ("aq-strength", "AQ Strength",
          "Adaptive Quantization Strength when spatial-aq is enabled"
          " from 1 (low) to 15 (aggressive), (0 = autoselect)",
          0, 15, DEFAULT_AQ_STRENGTH, param_flags));
  g_object_class_install_property (object_class, PROP_QP_MIN_I,
      g_param_spec_int ("qp-min-i", "QP Min I",
          "Minimum QP value for I frame, (-1 = automatic)", -1, 255,
          DEFAULT_QP, param_flags));
  g_object_class_install_property (object_class, PROP_QP_MIN_P,
      g_param_spec_int ("qp-min-p", "QP Min P",
          "Minimum QP value for P frame, (-1 = automatic)", -1, 255,
          DEFAULT_QP, param_flags));
  g_object_class_install_property (object_class, PROP_QP_MIN_B,
      g_param_spec_int ("qp-min-b", "QP Min B",
          "Minimum QP value for B frame, (-1 = automatic)", -1, 255,
          DEFAULT_QP, param_flags));
  g_object_class_install_property (object_class, PROP_QP_MAX_I,
      g_param_spec_int ("qp-max-i", "QP Max I",
          "Maximum QP value for I frame, (-1 = automatic)", -1, 255,
          DEFAULT_QP, param_flags));
  g_object_class_install_property (object_class, PROP_QP_MAX_P,
      g_param_spec_int ("qp-max-p", "QP Max P",
          "Maximum QP value for P frame, (-1 = automatic)", -1, 255,
          DEFAULT_QP, param_flags));
  g_object_class_install_property (object_class, PROP_QP_MAX_B,
      g_param_spec_int ("qp-max-b", "Max QP B",
          "Maximum QP value for B frame, (-1 = automatic)", -1, 255,
          DEFAULT_QP, param_flags));
  g_object_class_install_property (object_class, PROP_CONST_QUALITY,
      g_param_spec_double ("const-quality", "Constant Quality",
          "Target Constant Quality level for VBR mode (0 = automatic)",
          0, 51, DEFAULT_CONST_QUALITY, param_flags));

  switch (cdata->device_mode) {
    case GST_NV_ENCODER_DEVICE_CUDA:
      gst_element_class_set_static_metadata (element_class,
          "NVENC AV1 Video Encoder CUDA Mode",
          "Codec/Encoder/Video/Hardware",
          "Encode AV1 video streams using NVCODEC API CUDA Mode",
          "Seungha Yang <seungha@centricular.com>");
      break;
    case GST_NV_ENCODER_DEVICE_D3D11:
      gst_element_class_set_static_metadata (element_class,
          "NVENC AV1 Video Encoder Direct3D11 Mode",
          "Codec/Encoder/Video/Hardware",
          "Encode AV1 video streams using NVCODEC API Direct3D11 Mode",
          "Seungha Yang <seungha@centricular.com>");
      break;
    case GST_NV_ENCODER_DEVICE_AUTO_SELECT:
      gst_element_class_set_static_metadata (element_class,
          "NVENC AV1 Video Encoder Auto GPU select Mode",
          "Codec/Encoder/Video/Hardware",
          "Encode AV1 video streams using NVCODEC API auto GPU select Mode",
          "Seungha Yang <seungha@centricular.com>");
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          cdata->sink_caps));
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          cdata->src_caps));

  nvenc_class->set_format = GST_DEBUG_FUNCPTR (gst_nv_av1_encoder_set_format);
  nvenc_class->set_output_state =
      GST_DEBUG_FUNCPTR (gst_nv_av1_encoder_set_output_state);
  nvenc_class->check_reconfigure =
      GST_DEBUG_FUNCPTR (gst_nv_av1_encoder_check_reconfigure);
  nvenc_class->select_device =
      GST_DEBUG_FUNCPTR (gst_nv_av1_encoder_select_device);
  nvenc_class->calculate_min_buffers =
      GST_DEBUG_FUNCPTR (gst_nv_av1_encoder_calculate_min_buffers);

  klass->device_caps = cdata->device_caps;
  klass->cuda_device_id = cdata->cuda_device_id;
  klass->adapter_luid = cdata->adapter_luid;
  klass->device_mode = cdata->device_mode;
  klass->cuda_device_id_size = cdata->cuda_device_id_size;
  klass->adapter_luid_size = cdata->adapter_luid_size;
  memcpy (klass->cuda_device_id_list, cdata->cuda_device_id_list,
      sizeof (klass->cuda_device_id_list));
  memcpy (klass->adapter_luid_list, cdata->adapter_luid_list,
      sizeof (klass->adapter_luid_list));

  gst_nv_encoder_class_data_unref (cdata);
}

static void
gst_nv_av1_encoder_init (GstNvAv1Encoder * self)
{
  auto klass = GST_NV_AV1_ENCODER_GET_CLASS (self);

  g_mutex_init (&self->prop_lock);

  self->selected_device_mode = klass->device_mode;
  self->cuda_device_id = klass->cuda_device_id;
  self->adapter_luid = klass->adapter_luid;
  self->preset = DEFAULT_PRESET;
  self->tune = DEFAULT_TUNE;
  self->multipass = DEFAULT_MULTI_PASS;
  self->weighted_pred = DEFAULT_WEIGHTED_PRED;
  self->gop_size = DEFAULT_GOP_SIZE;
  self->bframes = DEFAULT_B_FRAMES;
  self->rc_mode = DEFAULT_RATE_CONTROL;
  self->qp_const_i = DEFAULT_QP;
  self->qp_const_p = DEFAULT_QP;
  self->qp_const_b = DEFAULT_QP;
  self->bitrate = DEFAULT_BITRATE;
  self->max_bitrate = DEFAULT_MAX_BITRATE;
  self->vbv_buffer_size = DEFAULT_VBV_BUFFER_SIZE;
  self->rc_lookahead = DEFAULT_RC_LOOKAHEAD;
  self->i_adapt = DEFAULT_I_ADAPT;
  self->b_adapt = DEFAULT_B_ADAPT;
  self->spatial_aq = DEFAULT_SPATIAL_AQ;
  self->temporal_aq = DEFAULT_TEMPORAL_AQ;
  self->zero_reorder_delay = DEFAULT_ZEROLATENCY;
  self->non_ref_p = DEFAULT_NON_REF_P;
  self->strict_gop = DEFAULT_STRICT_GOP;
  self->aq_strength = DEFAULT_AQ_STRENGTH;
  self->qp_min_i = DEFAULT_QP;
  self->qp_min_p = DEFAULT_QP;
  self->qp_min_b = DEFAULT_QP;
  self->qp_max_i = DEFAULT_QP;
  self->qp_max_p = DEFAULT_QP;
  self->qp_max_b = DEFAULT_QP;
  self->const_quality = DEFAULT_CONST_QUALITY;

  gst_nv_encoder_set_device_mode (GST_NV_ENCODER (self), klass->device_mode,
      klass->cuda_device_id, klass->adapter_luid);
}

static void
gst_nv_av1_encoder_finalize (GObject * object)
{
  auto self = GST_NV_AV1_ENCODER (object);

  g_mutex_clear (&self->prop_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

typedef enum
{
  UPDATE_INIT_PARAM,
  UPDATE_RC_PARAM,
  UPDATE_BITRATE,
} PropUpdateLevel;

static void
update_boolean (GstNvAv1Encoder * self, gboolean * old_val,
    const GValue * new_val, PropUpdateLevel level)
{
  gboolean val = g_value_get_boolean (new_val);

  if (*old_val == val)
    return;

  *old_val = val;
  switch (level) {
    case UPDATE_INIT_PARAM:
      self->init_param_updated = TRUE;
      break;
    case UPDATE_RC_PARAM:
      self->rc_param_updated = TRUE;
      break;
    case UPDATE_BITRATE:
      self->bitrate_updated = TRUE;
      break;
  }
}

static void
update_int (GstNvAv1Encoder * self, gint * old_val,
    const GValue * new_val, PropUpdateLevel level)
{
  gint val = g_value_get_int (new_val);

  if (*old_val == val)
    return;

  *old_val = val;
  switch (level) {
    case UPDATE_INIT_PARAM:
      self->init_param_updated = TRUE;
      break;
    case UPDATE_RC_PARAM:
      self->rc_param_updated = TRUE;
      break;
    case UPDATE_BITRATE:
      self->bitrate_updated = TRUE;
      break;
  }
}

static void
update_uint (GstNvAv1Encoder * self, guint * old_val,
    const GValue * new_val, PropUpdateLevel level)
{
  guint val = g_value_get_uint (new_val);

  if (*old_val == val)
    return;

  *old_val = val;
  switch (level) {
    case UPDATE_INIT_PARAM:
      self->init_param_updated = TRUE;
      break;
    case UPDATE_RC_PARAM:
      self->rc_param_updated = TRUE;
      break;
    case UPDATE_BITRATE:
      self->bitrate_updated = TRUE;
      break;
  }
}

static void
update_double (GstNvAv1Encoder * self, gdouble * old_val,
    const GValue * new_val, PropUpdateLevel level)
{
  gdouble val = g_value_get_double (new_val);

  if (*old_val == val)
    return;

  *old_val = val;
  switch (level) {
    case UPDATE_INIT_PARAM:
      self->init_param_updated = TRUE;
      break;
    case UPDATE_RC_PARAM:
      self->rc_param_updated = TRUE;
      break;
    case UPDATE_BITRATE:
      self->bitrate_updated = TRUE;
      break;
  }
}

static void
gst_nv_av1_encoder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  auto self = GST_NV_AV1_ENCODER (object);
  auto klass = GST_NV_AV1_ENCODER_GET_CLASS (self);

  g_mutex_lock (&self->prop_lock);
  switch (prop_id) {
    case PROP_ADAPTER_LUID:{
      gint64 adapter_luid = g_value_get_int64 (value);
      gboolean is_valid = FALSE;

      for (guint i = 0; i < klass->adapter_luid_size; i++) {
        if (klass->adapter_luid_list[i] == adapter_luid) {
          self->adapter_luid = adapter_luid;
          is_valid = TRUE;
          break;
        }
      }

      if (!is_valid)
        g_warning ("%" G_GINT64_FORMAT " is not a valid adapter luid",
            adapter_luid);
      break;
    }
    case PROP_CUDA_DEVICE_ID:{
      guint cuda_device_id = g_value_get_uint (value);
      gboolean is_valid = FALSE;

      for (guint i = 0; i < klass->cuda_device_id_size; i++) {
        if (klass->cuda_device_id_list[i] == cuda_device_id) {
          self->cuda_device_id = cuda_device_id;
          is_valid = TRUE;
          break;
        }
      }

      if (!is_valid)
        g_warning ("%d is not a valid cuda device id", cuda_device_id);
      break;
    }
    case PROP_PRESET:{
      GstNvEncoderPreset preset = (GstNvEncoderPreset) g_value_get_enum (value);
      if (preset != self->preset) {
        self->preset = preset;
        self->init_param_updated = TRUE;
      }
      break;
    }
    case PROP_TUNE:{
      GstNvEncoderTune tune = (GstNvEncoderTune) g_value_get_enum (value);
      if (tune != self->tune) {
        self->tune = tune;
        self->init_param_updated = TRUE;
      }
      break;
    }
    case PROP_MULTI_PASS:{
      GstNvEncoderMultiPass multipass =
          (GstNvEncoderMultiPass) g_value_get_enum (value);
      if (multipass != self->multipass) {
        self->multipass = multipass;
        self->init_param_updated = TRUE;
      }
      break;
    }
    case PROP_WEIGHTED_PRED:
      update_boolean (self, &self->weighted_pred, value, UPDATE_INIT_PARAM);
      break;
    case PROP_GOP_SIZE:
      update_int (self, &self->gop_size, value, UPDATE_INIT_PARAM);
      break;
    case PROP_B_FRAMES:
      update_uint (self, &self->bframes, value, UPDATE_INIT_PARAM);
      break;
    case PROP_RATE_CONTROL:{
      GstNvEncoderRCMode mode = (GstNvEncoderRCMode) g_value_get_enum (value);
      if (mode != self->rc_mode) {
        self->rc_mode = mode;
        self->rc_param_updated = TRUE;
      }
      break;
    }
    case PROP_QP_CONST_I:
      update_int (self, &self->qp_const_i, value, UPDATE_RC_PARAM);
      break;
    case PROP_QP_CONST_P:
      update_int (self, &self->qp_const_p, value, UPDATE_RC_PARAM);
      break;
    case PROP_QP_CONST_B:
      update_int (self, &self->qp_const_b, value, UPDATE_RC_PARAM);
      break;
    case PROP_BITRATE:
      update_uint (self, &self->bitrate, value, UPDATE_BITRATE);
      break;
    case PROP_MAX_BITRATE:
      update_uint (self, &self->max_bitrate, value, UPDATE_BITRATE);
      break;
    case PROP_VBV_BUFFER_SIZE:
      update_uint (self, &self->vbv_buffer_size, value, UPDATE_RC_PARAM);
      break;
    case PROP_RC_LOOKAHEAD:
      /* rc-lookahead update requires pool size change */
      update_uint (self, &self->rc_lookahead, value, UPDATE_INIT_PARAM);
      break;
    case PROP_I_ADAPT:
      update_boolean (self, &self->i_adapt, value, UPDATE_RC_PARAM);
      break;
    case PROP_B_ADAPT:
      update_boolean (self, &self->b_adapt, value, UPDATE_RC_PARAM);
      break;
    case PROP_SPATIAL_AQ:
      update_boolean (self, &self->spatial_aq, value, UPDATE_RC_PARAM);
      break;
    case PROP_TEMPORAL_AQ:
      update_boolean (self, &self->temporal_aq, value, UPDATE_RC_PARAM);
      break;
    case PROP_ZEROLATENCY:
      update_boolean (self, &self->zero_reorder_delay, value, UPDATE_RC_PARAM);
      break;
    case PROP_NON_REF_P:
      update_boolean (self, &self->non_ref_p, value, UPDATE_RC_PARAM);
      break;
    case PROP_STRICT_GOP:
      update_boolean (self, &self->strict_gop, value, UPDATE_RC_PARAM);
      break;
    case PROP_AQ_STRENGTH:
      update_uint (self, &self->aq_strength, value, UPDATE_RC_PARAM);
      break;
    case PROP_QP_MIN_I:
      update_int (self, &self->qp_min_i, value, UPDATE_RC_PARAM);
      break;
    case PROP_QP_MIN_P:
      update_int (self, &self->qp_min_p, value, UPDATE_RC_PARAM);
      break;
    case PROP_QP_MIN_B:
      update_int (self, &self->qp_min_b, value, UPDATE_RC_PARAM);
      break;
    case PROP_QP_MAX_I:
      update_int (self, &self->qp_max_i, value, UPDATE_RC_PARAM);
      break;
    case PROP_QP_MAX_P:
      update_int (self, &self->qp_max_p, value, UPDATE_RC_PARAM);
      break;
    case PROP_QP_MAX_B:
      update_int (self, &self->qp_max_b, value, UPDATE_RC_PARAM);
      break;
    case PROP_CONST_QUALITY:
      update_double (self, &self->const_quality, value, UPDATE_RC_PARAM);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  g_mutex_unlock (&self->prop_lock);
}

static void
gst_nv_av1_encoder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  auto self = GST_NV_AV1_ENCODER (object);

  switch (prop_id) {
    case PROP_ADAPTER_LUID:
      g_value_set_int64 (value, self->adapter_luid);
      break;
    case PROP_CUDA_DEVICE_ID:
      g_value_set_uint (value, self->cuda_device_id);
      break;
    case PROP_PRESET:
      g_value_set_enum (value, self->preset);
      break;
    case PROP_TUNE:
      g_value_set_enum (value, self->tune);
      break;
    case PROP_MULTI_PASS:
      g_value_set_enum (value, self->multipass);
      break;
    case PROP_WEIGHTED_PRED:
      g_value_set_boolean (value, self->weighted_pred);
      break;
    case PROP_GOP_SIZE:
      g_value_set_int (value, self->gop_size);
      break;
    case PROP_B_FRAMES:
      g_value_set_uint (value, self->bframes);
      break;
    case PROP_RATE_CONTROL:
      g_value_set_enum (value, self->rc_mode);
      break;
    case PROP_QP_CONST_I:
      g_value_set_int (value, self->qp_const_i);
      break;
    case PROP_QP_CONST_P:
      g_value_set_int (value, self->qp_const_p);
      break;
    case PROP_QP_CONST_B:
      g_value_set_int (value, self->qp_const_b);
      break;
    case PROP_BITRATE:
      g_value_set_uint (value, self->bitrate);
      break;
    case PROP_MAX_BITRATE:
      g_value_set_uint (value, self->max_bitrate);
      break;
    case PROP_VBV_BUFFER_SIZE:
      g_value_set_uint (value, self->vbv_buffer_size);
      break;
    case PROP_RC_LOOKAHEAD:
      g_value_set_uint (value, self->rc_lookahead);
      break;
    case PROP_I_ADAPT:
      g_value_set_boolean (value, self->i_adapt);
      break;
    case PROP_B_ADAPT:
      g_value_set_boolean (value, self->b_adapt);
      break;
    case PROP_SPATIAL_AQ:
      g_value_set_boolean (value, self->spatial_aq);
      break;
    case PROP_TEMPORAL_AQ:
      g_value_set_boolean (value, self->temporal_aq);
      break;
    case PROP_ZEROLATENCY:
      g_value_set_boolean (value, self->zero_reorder_delay);
      break;
    case PROP_NON_REF_P:
      g_value_set_boolean (value, self->non_ref_p);
      break;
    case PROP_STRICT_GOP:
      g_value_set_boolean (value, self->strict_gop);
      break;
    case PROP_AQ_STRENGTH:
      g_value_set_uint (value, self->aq_strength);
      break;
    case PROP_QP_MIN_I:
      g_value_set_int (value, self->qp_min_i);
      break;
    case PROP_QP_MIN_P:
      g_value_set_int (value, self->qp_min_p);
      break;
    case PROP_QP_MIN_B:
      g_value_set_int (value, self->qp_min_b);
      break;
    case PROP_QP_MAX_I:
      g_value_set_int (value, self->qp_max_i);
      break;
    case PROP_QP_MAX_P:
      g_value_set_int (value, self->qp_max_p);
      break;
    case PROP_QP_MAX_B:
      g_value_set_int (value, self->qp_max_b);
      break;
    case PROP_CONST_QUALITY:
      g_value_set_double (value, self->const_quality);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_nv_av1_encoder_set_format (GstNvEncoder * encoder,
    GstVideoCodecState * state, gpointer session,
    NV_ENC_INITIALIZE_PARAMS * init_params, NV_ENC_CONFIG * config)
{
  auto self = GST_NV_AV1_ENCODER (encoder);
  auto klass = GST_NV_AV1_ENCODER_GET_CLASS (self);
  auto dev_caps = &klass->device_caps;
  NV_ENC_RC_PARAMS *rc_params;
  GstVideoInfo *info = &state->info;
  NVENCSTATUS status;
  NV_ENC_PRESET_CONFIG preset_config = { 0, };
  gboolean bframe_aborted = FALSE;
  gboolean weight_pred_aborted = FALSE;
  gboolean vbv_buffer_size_aborted = FALSE;
  gboolean lookahead_aborted = FALSE;
  gboolean temporal_aq_aborted = FALSE;
  guint bitdepth_minus8 = GST_VIDEO_INFO_COMP_DEPTH (info, 0) - 8;

  g_mutex_lock (&self->prop_lock);

  if (klass->device_mode == GST_NV_ENCODER_DEVICE_AUTO_SELECT) {
    GstNvEncoderDeviceCaps dcaps;

    gst_nv_encoder_get_encoder_caps (session, &NV_ENC_CODEC_AV1_GUID, &dcaps);
    if (self->bframes > 0 && !dcaps.max_bframes) {
      self->bframes = 0;
      bframe_aborted = TRUE;

      GST_INFO_OBJECT (self, "B-frame was enabled but not support by device");
    }

    if (self->weighted_pred && !dcaps.weighted_prediction) {
      self->weighted_pred = FALSE;
      weight_pred_aborted = TRUE;

      GST_INFO_OBJECT (self,
          "Weighted prediction was enabled but not support by device");
    }

    if (self->vbv_buffer_size && !dcaps.custom_vbv_buf_size) {
      self->vbv_buffer_size = 0;
      vbv_buffer_size_aborted = TRUE;

      GST_INFO_OBJECT (self,
          "VBV buffer size was specified but not supported by device");
    }

    if (self->rc_lookahead && !dcaps.lookahead) {
      self->rc_lookahead = 0;
      lookahead_aborted = TRUE;

      GST_INFO_OBJECT (self,
          "VBV buffer size was specified but not supported by device");
    }

    if (self->temporal_aq && !dcaps.temporal_aq) {
      self->temporal_aq = FALSE;
      temporal_aq_aborted = TRUE;

      GST_INFO_OBJECT (self,
          "temporal-aq was enabled but not supported by device");
    }
  }

  init_params->version = gst_nvenc_get_initialize_params_version ();
  init_params->encodeGUID = NV_ENC_CODEC_AV1_GUID;

  init_params->encodeWidth = GST_VIDEO_INFO_WIDTH (info);
  init_params->maxEncodeWidth = GST_VIDEO_INFO_WIDTH (info);
  init_params->encodeHeight = GST_VIDEO_INFO_HEIGHT (info);
  init_params->maxEncodeHeight = GST_VIDEO_INFO_HEIGHT (info);
  init_params->enablePTD = TRUE;
  if (dev_caps->async_encoding_support)
    init_params->enableEncodeAsync = 1;
  if (info->fps_d > 0 && info->fps_n > 0) {
    init_params->frameRateNum = info->fps_n;
    init_params->frameRateDen = info->fps_d;
  } else {
    init_params->frameRateNum = 0;
    init_params->frameRateDen = 1;
  }

  init_params->enableWeightedPrediction = self->weighted_pred;

  init_params->darWidth = GST_VIDEO_INFO_WIDTH (info);
  init_params->darHeight = GST_VIDEO_INFO_HEIGHT (info);

  GstNvEncoderPresetOptions in_opt = { };
  GstNvEncoderPresetOptionsNative out_opt = { };
  in_opt.preset = self->preset;
  in_opt.tune = self->tune;
  in_opt.rc_mode = self->rc_mode;
  in_opt.multi_pass = self->multipass;
  GstNvEncoderPresetResolution resolution = GST_NV_ENCODER_PRESET_720;
  auto frame_size = info->width * info->height;
  if (frame_size >= 3840 * 2160)
    resolution = GST_NV_ENCODER_PRESET_2160;
  else if (frame_size >= 1920 * 1080)
    resolution = GST_NV_ENCODER_PRESET_1080;

  gst_nv_encoder_preset_to_native (resolution, &in_opt, &out_opt);
  init_params->presetGUID = out_opt.preset;
  init_params->tuningInfo = out_opt.tune;

  preset_config.version = gst_nvenc_get_preset_config_version ();
  preset_config.presetCfg.version = gst_nvenc_get_config_version ();

  status = NvEncGetEncodePresetConfigEx (session, NV_ENC_CODEC_AV1_GUID,
      init_params->presetGUID, init_params->tuningInfo, &preset_config);
  if (!gst_nv_enc_result (status, self)) {
    GST_ERROR_OBJECT (self, "Failed to get preset config");
    g_mutex_unlock (&self->prop_lock);
    return FALSE;
  }

  *config = preset_config.presetCfg;
  if (self->gop_size < 0) {
    config->gopLength = NVENC_INFINITE_GOPLENGTH;
    config->frameIntervalP = 1;
  } else if (self->gop_size > 0) {
    config->gopLength = self->gop_size;
    /* frameIntervalP
     * 0: All Intra frames
     * 1: I/P only
     * 2: IBP
     * 3: IBBP
     */
    config->frameIntervalP = self->bframes + 1;
  } else {
    /* gop size == 0 means all intra frames */
    config->gopLength = 1;
    config->frameIntervalP = 0;
  }

  rc_params = &config->rcParams;

  rc_params->rateControlMode = out_opt.rc_mode;
  rc_params->multiPass = out_opt.multi_pass;

  if (self->bitrate)
    rc_params->averageBitRate = self->bitrate * 1024;
  if (self->max_bitrate)
    rc_params->maxBitRate = self->max_bitrate * 1024;
  if (self->vbv_buffer_size)
    rc_params->vbvBufferSize = self->vbv_buffer_size * 1024;

  if (self->qp_min_i >= 0) {
    rc_params->enableMinQP = TRUE;
    rc_params->minQP.qpIntra = self->qp_min_i;
    if (self->qp_min_p >= 0) {
      rc_params->minQP.qpInterP = self->qp_min_p;
    } else {
      rc_params->minQP.qpInterP = rc_params->minQP.qpIntra;
    }
    if (self->qp_min_b >= 0) {
      rc_params->minQP.qpInterB = self->qp_min_b;
    } else {
      rc_params->minQP.qpInterB = rc_params->minQP.qpInterP;
    }
  }

  if (self->qp_max_i >= 0) {
    rc_params->enableMaxQP = TRUE;
    rc_params->maxQP.qpIntra = self->qp_max_i;
    if (self->qp_max_p >= 0) {
      rc_params->maxQP.qpInterP = self->qp_max_p;
    } else {
      rc_params->maxQP.qpInterP = rc_params->maxQP.qpIntra;
    }
    if (self->qp_max_b >= 0) {
      rc_params->maxQP.qpInterB = self->qp_max_b;
    } else {
      rc_params->maxQP.qpInterB = rc_params->maxQP.qpInterP;
    }
  }

  if (rc_params->rateControlMode == NV_ENC_PARAMS_RC_CONSTQP) {
    if (self->qp_const_i >= 0)
      rc_params->constQP.qpIntra = self->qp_const_i;
    if (self->qp_const_p >= 0)
      rc_params->constQP.qpInterP = self->qp_const_p;
    if (self->qp_const_b >= 0)
      rc_params->constQP.qpInterB = self->qp_const_b;
  }

  if (self->spatial_aq) {
    rc_params->enableAQ = TRUE;
    rc_params->aqStrength = self->aq_strength;
  }

  rc_params->enableTemporalAQ = self->temporal_aq;

  if (self->rc_lookahead) {
    rc_params->enableLookahead = 1;
    rc_params->lookaheadDepth = self->rc_lookahead;
    rc_params->disableIadapt = !self->i_adapt;
    rc_params->disableBadapt = !self->b_adapt;
  }

  rc_params->strictGOPTarget = self->strict_gop;
  rc_params->enableNonRefP = self->non_ref_p;
  rc_params->zeroReorderDelay = self->zero_reorder_delay;

  if (self->const_quality) {
    guint scaled = (gint) (self->const_quality * 256.0);

    rc_params->targetQuality = (guint8) (scaled >> 8);
    rc_params->targetQualityLSB = (guint8) (scaled & 0xff);
  }
  self->init_param_updated = FALSE;
  self->bitrate_updated = FALSE;
  self->rc_param_updated = FALSE;

  config->version = gst_nvenc_get_config_version ();
  config->profileGUID = NV_ENC_AV1_PROFILE_MAIN_GUID;

  NV_ENC_CONFIG_AV1 *av1_config = &config->encodeCodecConfig.av1Config;
  av1_config->level = NV_ENC_LEVEL_AV1_AUTOSELECT;
  av1_config->tier = NV_ENC_TIER_AV1_0;
  /* TODO: property */
  av1_config->minPartSize = NV_ENC_AV1_PART_SIZE_AUTOSELECT;
  av1_config->maxPartSize = NV_ENC_AV1_PART_SIZE_AUTOSELECT;
  av1_config->outputAnnexBFormat = FALSE;
  av1_config->enableTimingInfo = FALSE;
  av1_config->enableDecoderModelInfo = FALSE;
  /* TODO: Maybe useful for debugging, but not required for decoding */
  av1_config->enableFrameIdNumbers = FALSE;
  av1_config->disableSeqHdr = FALSE;
  av1_config->repeatSeqHdr = TRUE;
  /* TODO: property */
  av1_config->enableIntraRefresh = FALSE;
  /* TODO: main profile only for now */
  av1_config->chromaFormatIDC = 1;
  av1_config->enableBitstreamPadding = FALSE;
  /* TODO: property and configure tile info accordingly */
  av1_config->enableCustomTileConfig = FALSE;
  /* TODO: property, support user specified film grain params */
  av1_config->enableFilmGrainParams = FALSE;
  av1_config->inputPixelBitDepthMinus8 = bitdepth_minus8;
  av1_config->pixelBitDepthMinus8 = bitdepth_minus8;
  av1_config->idrPeriod = config->gopLength;

  /* TODO: support intra refresh */
  av1_config->intraRefreshPeriod = 0;
  av1_config->intraRefreshCnt = 0;

  av1_config->maxNumRefFramesInDPB = 0;
  av1_config->numFwdRefs = NV_ENC_NUM_REF_FRAMES_AUTOSELECT;
  av1_config->numBwdRefs = NV_ENC_NUM_REF_FRAMES_AUTOSELECT;

  GstVideoColorimetry cinfo;
  if (GST_VIDEO_INFO_IS_YUV (info)) {
    cinfo = info->colorimetry;
  } else {
    /* Other formats will be converted 4:2:0 YUV by runtime */
    gst_video_colorimetry_from_string (&cinfo, GST_VIDEO_COLORIMETRY_BT709);
  }

  av1_config->colorPrimaries = (NV_ENC_VUI_COLOR_PRIMARIES)
      gst_video_color_primaries_to_iso (cinfo.primaries);
  av1_config->transferCharacteristics = (NV_ENC_VUI_TRANSFER_CHARACTERISTIC)
      gst_video_transfer_function_to_iso (cinfo.transfer);
  av1_config->matrixCoefficients = (NV_ENC_VUI_MATRIX_COEFFS)
      gst_video_color_matrix_to_iso (cinfo.matrix);
  if (cinfo.range == GST_VIDEO_COLOR_RANGE_0_255) {
    av1_config->colorRange = 1;
  } else {
    av1_config->colorRange = 0;
  }

  g_mutex_unlock (&self->prop_lock);

  if (bframe_aborted)
    g_object_notify (G_OBJECT (self), "b-frames");
  if (weight_pred_aborted)
    g_object_notify (G_OBJECT (self), "weighted-pred");
  if (vbv_buffer_size_aborted)
    g_object_notify (G_OBJECT (self), "vbv-buffer-size");
  if (lookahead_aborted)
    g_object_notify (G_OBJECT (self), "rc-lookahead");
  if (temporal_aq_aborted)
    g_object_notify (G_OBJECT (self), "temporal-aq");

  return TRUE;
}

static gboolean
gst_nv_av1_encoder_set_output_state (GstNvEncoder * encoder,
    GstVideoCodecState * state, gpointer session)
{
  auto self = GST_NV_AV1_ENCODER (encoder);
  auto venc = GST_VIDEO_ENCODER (encoder);

  auto caps = gst_caps_new_simple ("video/x-av1", "stream-format",
      G_TYPE_STRING, "obu-stream", "alignment", G_TYPE_STRING, "tu",
      "profile", G_TYPE_STRING, "main", nullptr);

  auto output_state = gst_video_encoder_set_output_state (venc, caps, state);
  if (GST_VIDEO_INFO_IS_RGB (&state->info)) {
    /* Format converted by runtime */
    gst_video_colorimetry_from_string (&output_state->info.colorimetry,
        GST_VIDEO_COLORIMETRY_BT709);
    output_state->info.chroma_site = GST_VIDEO_CHROMA_SITE_H_COSITED;
  }

  GST_INFO_OBJECT (self, "Output caps: %" GST_PTR_FORMAT, output_state->caps);
  gst_video_codec_state_unref (output_state);

  auto tags = gst_tag_list_new_empty ();
  gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE, GST_TAG_ENCODER,
      "nvav1enc", nullptr);

  gst_video_encoder_merge_tags (GST_VIDEO_ENCODER (encoder),
      tags, GST_TAG_MERGE_REPLACE);
  gst_tag_list_unref (tags);

  return TRUE;
}

static GstNvEncoderReconfigure
gst_nv_av1_encoder_check_reconfigure (GstNvEncoder * encoder,
    NV_ENC_CONFIG * config)
{
  auto self = GST_NV_AV1_ENCODER (encoder);
  GstNvEncoderReconfigure reconfig = GST_NV_ENCODER_RECONFIGURE_NONE;

  /* Dynamic RC param update is not tested, do soft-reconfigure only for
   * bitrate update */
  g_mutex_lock (&self->prop_lock);
  if (self->init_param_updated || self->rc_param_updated) {
    reconfig = GST_NV_ENCODER_RECONFIGURE_FULL;
    goto done;
  }

  if (self->bitrate_updated) {
    auto klass = GST_NV_AV1_ENCODER_GET_CLASS (self);
    if (klass->device_caps.dyn_bitrate_change > 0) {
      config->rcParams.averageBitRate = self->bitrate * 1024;
      config->rcParams.maxBitRate = self->max_bitrate * 1024;
      reconfig = GST_NV_ENCODER_RECONFIGURE_BITRATE;
    } else {
      reconfig = GST_NV_ENCODER_RECONFIGURE_FULL;
    }
  }

done:
  self->init_param_updated = FALSE;
  self->rc_param_updated = FALSE;
  self->bitrate_updated = FALSE;
  g_mutex_unlock (&self->prop_lock);

  return reconfig;
}

static gboolean
gst_nv_av1_encoder_select_device (GstNvEncoder * encoder,
    const GstVideoInfo * info, GstBuffer * buffer,
    GstNvEncoderDeviceData * data)
{
  auto self = GST_NV_AV1_ENCODER (encoder);
  auto klass = GST_NV_AV1_ENCODER_GET_CLASS (self);
  GstMemory *mem;

  memset (data, 0, sizeof (GstNvEncoderDeviceData));

  g_assert (klass->device_mode == GST_NV_ENCODER_DEVICE_AUTO_SELECT);

  mem = gst_buffer_peek_memory (buffer, 0);
  if (klass->cuda_device_id_size > 0 && gst_is_cuda_memory (mem)) {
    GstCudaMemory *cmem = GST_CUDA_MEMORY_CAST (mem);
    GstCudaContext *context = cmem->context;
    guint device_id;
    gboolean found = FALSE;

    g_object_get (context, "cuda-device-id", &device_id, nullptr);

    data->device_mode = GST_NV_ENCODER_DEVICE_CUDA;
    self->selected_device_mode = GST_NV_ENCODER_DEVICE_CUDA;

    for (guint i = 0; i < klass->cuda_device_id_size; i++) {
      if (klass->cuda_device_id_list[i] == device_id) {
        data->cuda_device_id = device_id;
        found = TRUE;
        break;
      }
    }

    if (!found) {
      GST_INFO_OBJECT (self,
          "Upstream CUDA device is not in supported device list");
      data->cuda_device_id = self->cuda_device_id;
    } else {
      data->device = (GstObject *) gst_object_ref (context);
    }

    if (data->cuda_device_id != self->cuda_device_id) {
      self->cuda_device_id = data->cuda_device_id;
      g_object_notify (G_OBJECT (self), "cuda-device-id");
    }

    return TRUE;
  }
#ifdef G_OS_WIN32
  if (klass->adapter_luid_size > 0 && gst_is_d3d11_memory (mem)) {
    GstD3D11Memory *dmem = GST_D3D11_MEMORY_CAST (mem);
    GstD3D11Device *device = dmem->device;
    gint64 adapter_luid;
    gboolean found = FALSE;

    g_object_get (device, "adapter-luid", &adapter_luid, nullptr);

    data->device_mode = GST_NV_ENCODER_DEVICE_D3D11;
    self->selected_device_mode = GST_NV_ENCODER_DEVICE_D3D11;

    for (guint i = 0; i < klass->cuda_device_id_size; i++) {
      if (klass->adapter_luid_list[i] == adapter_luid) {
        data->adapter_luid = adapter_luid;
        found = TRUE;
        break;
      }
    }

    if (!found) {
      GST_INFO_OBJECT (self,
          "Upstream D3D11 device is not in supported device list");
      data->adapter_luid = self->adapter_luid;
    } else {
      data->device = (GstObject *) gst_object_ref (device);
    }

    if (data->adapter_luid != self->adapter_luid) {
      self->adapter_luid = data->adapter_luid;
      g_object_notify (G_OBJECT (self), "adapter-luid");
    }

    return TRUE;
  }
#endif

  if (klass->cuda_device_id_size > 0 &&
      (self->selected_device_mode != GST_NV_ENCODER_DEVICE_D3D11)) {
    GST_INFO_OBJECT (self, "Upstream is system memory, use CUDA mode");
    data->device_mode = GST_NV_ENCODER_DEVICE_CUDA;
    data->cuda_device_id = self->cuda_device_id;
  } else {
    GST_INFO_OBJECT (self, "Upstream is system memory, use CUDA mode");
    data->device_mode = GST_NV_ENCODER_DEVICE_D3D11;
    data->adapter_luid = self->adapter_luid;
  }

  self->selected_device_mode = data->device_mode;

  return TRUE;
}

static guint
gst_nv_av1_encoder_calculate_min_buffers (GstNvEncoder * encoder)
{
  auto self = GST_NV_AV1_ENCODER (encoder);
  guint num_buffers;

  /* At least 4 surfaces are required as documented by Nvidia Encoder guide */
  num_buffers = 4;

  /* lookahead depth */
  num_buffers += self->rc_lookahead;

  /* B frames + 1 */
  num_buffers += self->bframes + 1;

  return num_buffers;
}

static GstNvEncoderClassData *
gst_nv_av1_encoder_create_class_data (GstObject * device, gpointer session,
    GstNvEncoderDeviceMode device_mode)
{
  NVENCSTATUS status;
  GstNvEncoderDeviceCaps dev_caps = { 0, };
  GUID profile_guids[16];
  NV_ENC_BUFFER_FORMAT input_formats[16];
  guint32 profile_guid_count = 0;
  guint32 input_format_count = 0;
  std::string sink_caps_str;
  std::string src_caps_str;
  std::string format_str;
  std::set < std::string > formats;
  std::string profile_str;
  std::string resolution_str;
  GstNvEncoderClassData *cdata;
  GstCaps *sink_caps;
  GstCaps *system_caps;
  NV_ENC_PRESET_CONFIG preset_config = { 0, };

  preset_config.version = gst_nvenc_get_preset_config_version ();
  preset_config.presetCfg.version = gst_nvenc_get_config_version ();

  status = NvEncGetEncodePresetConfigEx (session, NV_ENC_CODEC_AV1_GUID,
      NV_ENC_PRESET_P4_GUID, NV_ENC_TUNING_INFO_HIGH_QUALITY, &preset_config);
  if (status != NV_ENC_SUCCESS) {
    GST_WARNING_OBJECT (device, "New preset is not supported");
    return nullptr;
  }

  status = NvEncGetEncodeProfileGUIDs (session, NV_ENC_CODEC_AV1_GUID,
      profile_guids, G_N_ELEMENTS (profile_guids), &profile_guid_count);
  if (status != NV_ENC_SUCCESS || profile_guid_count == 0) {
    GST_WARNING_OBJECT (device, "Unable to get supported profiles");
    return nullptr;
  }

  status = NvEncGetInputFormats (session, NV_ENC_CODEC_AV1_GUID, input_formats,
      G_N_ELEMENTS (input_formats), &input_format_count);
  if (status != NV_ENC_SUCCESS || input_format_count == 0) {
    GST_WARNING_OBJECT (device, "Unable to get supported input formats");
    return nullptr;
  }

  gst_nv_encoder_get_encoder_caps (session, &NV_ENC_CODEC_AV1_GUID, &dev_caps);

  for (guint32 i = 0; i < input_format_count; i++) {
    switch (input_formats[i]) {
      case NV_ENC_BUFFER_FORMAT_NV12:
        formats.insert ("NV12");
        break;
      case NV_ENC_BUFFER_FORMAT_YUV420_10BIT:
        if (dev_caps.supports_10bit_encode)
          formats.insert ("P010_10LE");
        break;
      case NV_ENC_BUFFER_FORMAT_AYUV:
        formats.insert ("VUYA");
        break;
      case NV_ENC_BUFFER_FORMAT_ABGR:
        formats.insert ("RGBA");
        formats.insert ("RGBx");
        break;
      case NV_ENC_BUFFER_FORMAT_ARGB:
        formats.insert ("BGRA");
        formats.insert ("BGRx");
        break;
      case NV_ENC_BUFFER_FORMAT_ABGR10:
        if (dev_caps.supports_10bit_encode)
          formats.insert ("RGB10A2_LE");
        break;
      default:
        break;
    }
  }

  if (formats.empty ()) {
    GST_WARNING_OBJECT (device, "Empty supported input format");
    return nullptr;
  }
#define APPEND_STRING(dst,set,str) G_STMT_START { \
  if (set.find(str) != set.end()) { \
    if (!first) \
      dst += ", "; \
    dst += str; \
    first = false; \
  } \
} G_STMT_END

  if (formats.size () == 1) {
    format_str = "format = (string) " + *(formats.begin ());
  } else {
    bool first = true;

    format_str = "format = (string) { ";
    APPEND_STRING (format_str, formats, "NV12");
    APPEND_STRING (format_str, formats, "P010_10LE");
    APPEND_STRING (format_str, formats, "VUYA");
    APPEND_STRING (format_str, formats, "RGBA");
    APPEND_STRING (format_str, formats, "RGBx");
    APPEND_STRING (format_str, formats, "BGRA");
    APPEND_STRING (format_str, formats, "BGRx");
    APPEND_STRING (format_str, formats, "RGB10A2_LE");
    format_str += " }";
  }
#undef APPEND_STRING

  resolution_str = "width = (int) [ " +
      std::to_string (GST_ROUND_UP_16 (dev_caps.width_min))
      + ", " + std::to_string (dev_caps.width_max) + " ]";
  resolution_str += ", height = (int) [ " +
      std::to_string (GST_ROUND_UP_16 (dev_caps.height_min))
      + ", " + std::to_string (dev_caps.height_max) + " ]";

  sink_caps_str = "video/x-raw, " + format_str + ", " + resolution_str;

  src_caps_str = "video/x-av1, " + resolution_str + ", profile = (string) main"
      + ", stream-format = (string) obu-stream, alignment = (string) tu";

  system_caps = gst_caps_from_string (sink_caps_str.c_str ());
  sink_caps = gst_caps_copy (system_caps);
#ifdef G_OS_WIN32
  if (device_mode == GST_NV_ENCODER_DEVICE_D3D11) {
    gst_caps_set_features (sink_caps, 0,
        gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, nullptr));
  }
#endif

  if (device_mode == GST_NV_ENCODER_DEVICE_CUDA) {
    gst_caps_set_features (sink_caps, 0,
        gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY, nullptr));
#ifdef HAVE_CUDA_GST_GL
    GstCaps *gl_caps = gst_caps_copy (system_caps);
    gst_caps_set_features (gl_caps, 0,
        gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_GL_MEMORY, nullptr));
    gst_caps_append (sink_caps, gl_caps);
#endif
  }

  gst_caps_append (sink_caps, system_caps);

  cdata = gst_nv_encoder_class_data_new ();
  cdata->sink_caps = sink_caps;
  cdata->src_caps = gst_caps_from_string (src_caps_str.c_str ());
  cdata->device_caps = dev_caps;
  cdata->device_mode = device_mode;

  /* *INDENT-OFF* */
  for (const auto &iter: formats)
    cdata->formats = g_list_append (cdata->formats, g_strdup (iter.c_str()));
  /* *INDENT-ON* */

  if (device_mode == GST_NV_ENCODER_DEVICE_D3D11)
    g_object_get (device, "adapter-luid", &cdata->adapter_luid, nullptr);

  if (device_mode == GST_NV_ENCODER_DEVICE_CUDA)
    g_object_get (device, "cuda-device-id", &cdata->cuda_device_id, nullptr);

  /* class data will be leaked if the element never gets instantiated */
  GST_MINI_OBJECT_FLAG_SET (cdata->sink_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
  GST_MINI_OBJECT_FLAG_SET (cdata->src_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);

  return cdata;
}

GstNvEncoderClassData *
gst_nv_av1_encoder_register_cuda (GstPlugin * plugin, GstCudaContext * context,
    guint rank)
{
  NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS session_params = { 0, };
  gpointer session;
  NVENCSTATUS status;
  GstNvEncoderClassData *cdata;

  GST_DEBUG_CATEGORY_INIT (gst_nv_av1_encoder_debug, "nvav1encoder", 0,
      "nvav1encoder");

  session_params.version =
      gst_nvenc_get_open_encode_session_ex_params_version ();
  session_params.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
  session_params.device = gst_cuda_context_get_handle (context);
  session_params.apiVersion = gst_nvenc_get_api_version ();

  status = NvEncOpenEncodeSessionEx (&session_params, &session);
  if (status != NV_ENC_SUCCESS) {
    GST_WARNING_OBJECT (context, "Failed to open session");
    return nullptr;
  }

  cdata = gst_nv_av1_encoder_create_class_data (GST_OBJECT (context), session,
      GST_NV_ENCODER_DEVICE_CUDA);
  NvEncDestroyEncoder (session);

  if (!cdata)
    return nullptr;

  gst_nv_encoder_class_data_ref (cdata);

  GType type;
  gchar *type_name;
  gchar *feature_name;
  GTypeInfo type_info = {
    sizeof (GstNvAv1EncoderClass),
    nullptr,
    nullptr,
    (GClassInitFunc) gst_nv_av1_encoder_class_init,
    nullptr,
    cdata,
    sizeof (GstNvAv1Encoder),
    0,
    (GInstanceInitFunc) gst_nv_av1_encoder_init,
  };

  type_name = g_strdup ("GstNvAv1Enc");
  feature_name = g_strdup ("nvav1enc");

  gint index = 0;
  while (g_type_from_name (type_name)) {
    index++;
    g_free (type_name);
    g_free (feature_name);
    type_name = g_strdup_printf ("GstNvAv1Device%dEnc", index);
    feature_name = g_strdup_printf ("nvav1device%denc", index);
  }

  type = g_type_register_static (GST_TYPE_NV_ENCODER, type_name,
      &type_info, (GTypeFlags) 0);

  if (rank > 0 && index != 0)
    rank--;

  if (index != 0)
    gst_element_type_set_skip_documentation (type);

  if (!gst_element_register (plugin, feature_name, rank, type))
    GST_WARNING ("Failed to register plugin '%s'", type_name);

  g_free (type_name);
  g_free (feature_name);

  return cdata;
}

#ifdef G_OS_WIN32
GstNvEncoderClassData *
gst_nv_av1_encoder_register_d3d11 (GstPlugin * plugin, GstD3D11Device * device,
    guint rank)
{
  NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS session_params = { 0, };
  gpointer session;
  NVENCSTATUS status;
  GstNvEncoderClassData *cdata;

  GST_DEBUG_CATEGORY_INIT (gst_nv_av1_encoder_debug, "nvav1encoder", 0,
      "nvav1encoder");

  session_params.version =
      gst_nvenc_get_open_encode_session_ex_params_version ();
  session_params.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX;
  session_params.device = gst_d3d11_device_get_device_handle (device);
  session_params.apiVersion = gst_nvenc_get_api_version ();

  status = NvEncOpenEncodeSessionEx (&session_params, &session);
  if (status != NV_ENC_SUCCESS) {
    GST_WARNING_OBJECT (device, "Failed to open session");
    return nullptr;
  }

  cdata = gst_nv_av1_encoder_create_class_data (GST_OBJECT (device), session,
      GST_NV_ENCODER_DEVICE_D3D11);
  NvEncDestroyEncoder (session);

  if (!cdata)
    return nullptr;

  gst_nv_encoder_class_data_ref (cdata);

  GType type;
  gchar *type_name;
  gchar *feature_name;
  GTypeInfo type_info = {
    sizeof (GstNvAv1EncoderClass),
    nullptr,
    nullptr,
    (GClassInitFunc) gst_nv_av1_encoder_class_init,
    nullptr,
    cdata,
    sizeof (GstNvAv1Encoder),
    0,
    (GInstanceInitFunc) gst_nv_av1_encoder_init,
  };

  type_name = g_strdup ("GstNvD3D11Av1Enc");
  feature_name = g_strdup ("nvd3d11av1enc");

  gint index = 0;
  while (g_type_from_name (type_name)) {
    index++;
    g_free (type_name);
    g_free (feature_name);
    type_name = g_strdup_printf ("GstNvD3D11Av1Device%dEnc", index);
    feature_name = g_strdup_printf ("nvd3d11av1device%denc", index);
  }

  type = g_type_register_static (GST_TYPE_NV_ENCODER, type_name,
      &type_info, (GTypeFlags) 0);

  if (rank > 0 && index != 0)
    rank--;

  if (index != 0)
    gst_element_type_set_skip_documentation (type);

  if (!gst_element_register (plugin, feature_name, rank, type))
    GST_WARNING ("Failed to register plugin '%s'", type_name);

  g_free (type_name);
  g_free (feature_name);

  return cdata;
}
#endif

void
gst_nv_av1_encoder_register_auto_select (GstPlugin * plugin,
    GList * device_caps_list, guint rank)
{
  std::set < std::string > formats;
  std::string sink_caps_str;
  std::string src_caps_str;
  std::string format_str;
  std::string resolution_str;
  GList *iter;
  guint adapter_luid_size = 0;
  gint64 adapter_luid_list[8] = { 0, };
  guint cuda_device_id_size = 0;
  guint cuda_device_id_list[8] = { 0, };
  GstNvEncoderDeviceCaps dev_caps;
  GstNvEncoderClassData *cdata;
  GstCaps *sink_caps = nullptr;
  GstCaps *system_caps;

  GST_DEBUG_CATEGORY_INIT (gst_nv_av1_encoder_debug, "nvav1encoder", 0,
      "nvav1encoder");

  for (iter = device_caps_list; iter; iter = g_list_next (iter)) {
    GstNvEncoderClassData *cdata = (GstNvEncoderClassData *) iter->data;
    GList *walk;

    for (walk = cdata->formats; walk; walk = g_list_next (walk))
      formats.insert ((gchar *) walk->data);

    if (cdata->device_mode == GST_NV_ENCODER_DEVICE_D3D11 &&
        adapter_luid_size <= G_N_ELEMENTS (adapter_luid_list) - 1) {
      adapter_luid_list[adapter_luid_size] = cdata->adapter_luid;
      adapter_luid_size++;
    }

    if (cdata->device_mode == GST_NV_ENCODER_DEVICE_CUDA &&
        cuda_device_id_size <= G_N_ELEMENTS (cuda_device_id_list) - 1) {
      cuda_device_id_list[cuda_device_id_size] = cdata->cuda_device_id;
      cuda_device_id_size++;
    }

    if (iter == device_caps_list) {
      dev_caps = cdata->device_caps;
    } else {
      gst_nv_encoder_merge_device_caps (&dev_caps, &cdata->device_caps,
          &dev_caps);
    }
  }

  g_list_free_full (device_caps_list,
      (GDestroyNotify) gst_nv_encoder_class_data_unref);
  if (formats.empty ())
    return;

#define APPEND_STRING(dst,set,str) G_STMT_START { \
  if (set.find(str) != set.end()) { \
    if (!first) \
      dst += ", "; \
    dst += str; \
    first = false; \
  } \
} G_STMT_END

  if (formats.size () == 1) {
    format_str = "format = (string) " + *(formats.begin ());
  } else {
    bool first = true;

    format_str = "format = (string) { ";
    APPEND_STRING (format_str, formats, "NV12");
    APPEND_STRING (format_str, formats, "P010_10LE");
    APPEND_STRING (format_str, formats, "VUYA");
    APPEND_STRING (format_str, formats, "RGBA");
    APPEND_STRING (format_str, formats, "RGBx");
    APPEND_STRING (format_str, formats, "BGRA");
    APPEND_STRING (format_str, formats, "BGRx");
    APPEND_STRING (format_str, formats, "RGB10A2_LE");
    format_str += " }";
  }
#undef APPEND_STRING

  resolution_str = "width = (int) [ " +
      std::to_string (GST_ROUND_UP_16 (dev_caps.width_min))
      + ", " + std::to_string (dev_caps.width_max) + " ]";
  resolution_str += ", height = (int) [ " +
      std::to_string (GST_ROUND_UP_16 (dev_caps.height_min))
      + ", " + std::to_string (dev_caps.height_max) + " ]";


  sink_caps_str = "video/x-raw, " + format_str + ", " + resolution_str;
  src_caps_str = "video/x-av1, " + resolution_str + ", profile = (string) main"
      + ", stream-format = (string) obu-stream, alignment = (string) tu";

  system_caps = gst_caps_from_string (sink_caps_str.c_str ());
  sink_caps = gst_caps_new_empty ();

  if (cuda_device_id_size > 0) {
    GstCaps *cuda_caps = gst_caps_copy (system_caps);
    gst_caps_set_features (cuda_caps, 0,
        gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY, nullptr));
    gst_caps_append (sink_caps, cuda_caps);
  }
#ifdef G_OS_WIN32
  if (adapter_luid_size > 0) {
    GstCaps *d3d11_caps = gst_caps_copy (system_caps);
    gst_caps_set_features (d3d11_caps, 0,
        gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, nullptr));
    gst_caps_append (sink_caps, d3d11_caps);
  }
#endif

#ifdef HAVE_CUDA_GST_GL
  GstCaps *gl_caps = gst_caps_copy (system_caps);
  gst_caps_set_features (gl_caps, 0,
      gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_GL_MEMORY, nullptr));
  gst_caps_append (sink_caps, gl_caps);
#endif

  gst_caps_append (sink_caps, system_caps);

  cdata = gst_nv_encoder_class_data_new ();
  cdata->sink_caps = sink_caps;
  cdata->src_caps = gst_caps_from_string (src_caps_str.c_str ());
  cdata->device_caps = dev_caps;
  cdata->device_mode = GST_NV_ENCODER_DEVICE_AUTO_SELECT;
  cdata->adapter_luid = adapter_luid_list[0];
  cdata->adapter_luid_size = adapter_luid_size;
  memcpy (&cdata->adapter_luid_list,
      adapter_luid_list, sizeof (adapter_luid_list));
  cdata->cuda_device_id = cuda_device_id_list[0];
  cdata->cuda_device_id_size = cuda_device_id_size;
  memcpy (&cdata->cuda_device_id_list,
      cuda_device_id_list, sizeof (cuda_device_id_list));

  /* class data will be leaked if the element never gets instantiated */
  GST_MINI_OBJECT_FLAG_SET (cdata->sink_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
  GST_MINI_OBJECT_FLAG_SET (cdata->src_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);

  GType type;
  GTypeInfo type_info = {
    sizeof (GstNvAv1EncoderClass),
    nullptr,
    nullptr,
    (GClassInitFunc) gst_nv_av1_encoder_class_init,
    nullptr,
    cdata,
    sizeof (GstNvAv1Encoder),
    0,
    (GInstanceInitFunc) gst_nv_av1_encoder_init,
  };

  type = g_type_register_static (GST_TYPE_NV_ENCODER, "GstNvAutoGpuAv1Enc",
      &type_info, (GTypeFlags) 0);

  if (!gst_element_register (plugin, "nvautogpuav1enc", rank, type))
    GST_WARNING ("Failed to register plugin 'GstNvAutoGpuAv1Enc'");
}
