/* GStreamer
 * Copyright (C) <2017> Sean DuBois <sean@siobud.com>
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
/**
 * SECTION:element-av1enc
 *
 * AV1 Encoder.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 videotestsrc num-buffers=50 ! av1enc ! webmmux ! filesink location=av1.webm
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstav1enc.h"
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/base/base.h>

#define GST_AV1_ENC_APPLY_CODEC_CONTROL(av1enc, flag, value)             \
  if (av1enc->encoder_inited) {                                        \
    if (aom_codec_control (&av1enc->encoder, flag,                     \
            value) != AOM_CODEC_OK) {                                  \
      gst_av1_codec_error (&av1enc->encoder, "Failed to set " #flag);  \
    }                                                                  \
  }

GST_DEBUG_CATEGORY_STATIC (av1_enc_debug);
#define GST_CAT_DEFAULT av1_enc_debug

enum
{
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_CPU_USED
};

#define PROP_CPU_USED_DEFAULT 0

static void gst_av1_enc_finalize (GObject * object);
static void gst_av1_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_av1_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_av1_enc_start (GstVideoEncoder * encoder);
static gboolean gst_av1_enc_stop (GstVideoEncoder * encoder);
static gboolean gst_av1_enc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state);
static GstFlowReturn gst_av1_enc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame);
static gboolean gst_av1_enc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query);

static void gst_av1_enc_destroy_encoder (GstAV1Enc * av1enc);

#define gst_av1_enc_parent_class parent_class
G_DEFINE_TYPE (GstAV1Enc, gst_av1_enc, GST_TYPE_VIDEO_ENCODER);

/* *INDENT-OFF* */
static GstStaticPadTemplate gst_av1_enc_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
        GST_STATIC_CAPS ("video/x-raw, "
        "format = (string) \"I420\", "
        "framerate = (fraction) [0, MAX], "
        "width = (int) [ 4, MAX ], "
        "height = (int) [ 4, MAX ]")
    );
/* *INDENT-ON* */

static GstStaticPadTemplate gst_av1_enc_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-av1")
    );

static void
gst_av1_enc_class_init (GstAV1EncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstVideoEncoderClass *venc_class;

  gobject_class = (GObjectClass *) klass;
  element_class = (GstElementClass *) klass;
  venc_class = (GstVideoEncoderClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_av1_enc_finalize;
  gobject_class->set_property = gst_av1_enc_set_property;
  gobject_class->get_property = gst_av1_enc_get_property;

  gst_element_class_add_static_pad_template (element_class,
      &gst_av1_enc_sink_pad_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_av1_enc_src_pad_template);
  gst_element_class_set_static_metadata (element_class, "AV1 Encoder",
      "Codec/Encoder/Video", "Encode AV1 video streams",
      "Sean DuBois <sean@siobud.com>");

  venc_class->start = gst_av1_enc_start;
  venc_class->stop = gst_av1_enc_stop;
  venc_class->set_format = gst_av1_enc_set_format;
  venc_class->handle_frame = gst_av1_enc_handle_frame;
  venc_class->propose_allocation = gst_av1_enc_propose_allocation;

  klass->codec_algo = &aom_codec_av1_cx_algo;
  GST_DEBUG_CATEGORY_INIT (av1_enc_debug, "av1enc", 0, "AV1 encoding element");

  g_object_class_install_property (gobject_class, PROP_CPU_USED,
      g_param_spec_int ("cpu-used", "CPU Used",
          "CPU Used. A Value greater than 0 will increase encoder speed at the expense of quality.",
          0, 8, PROP_CPU_USED_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_av1_codec_error (aom_codec_ctx_t * ctx, const char *s)
{
  const char *detail = aom_codec_error_detail (ctx);

  GST_ERROR ("%s: %s %s", s, aom_codec_error (ctx), detail ? detail : "");
}

static void
gst_av1_enc_init (GstAV1Enc * av1enc)
{
  GST_PAD_SET_ACCEPT_TEMPLATE (GST_VIDEO_ENCODER_SINK_PAD (av1enc));

  av1enc->encoder_inited = FALSE;

  av1enc->keyframe_dist = 30;
  av1enc->cpu_used = PROP_CPU_USED_DEFAULT;

  g_mutex_init (&av1enc->encoder_lock);
}

static void
gst_av1_enc_finalize (GObject * object)
{
  GstAV1Enc *av1enc = GST_AV1_ENC (object);

  if (av1enc->input_state) {
    gst_video_codec_state_unref (av1enc->input_state);
  }
  av1enc->input_state = NULL;

  gst_av1_enc_destroy_encoder (av1enc);
  g_mutex_clear (&av1enc->encoder_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_av1_enc_set_latency (GstAV1Enc * av1enc)
{
  GstClockTime latency =
      gst_util_uint64_scale (av1enc->aom_cfg.g_lag_in_frames, 1 * GST_SECOND,
      30);
  gst_video_encoder_set_latency (GST_VIDEO_ENCODER (av1enc), latency, latency);

  GST_WARNING_OBJECT (av1enc, "Latency unimplemented");
}

static const gchar *
gst_av1_enc_get_aom_rc_mode_name (enum aom_rc_mode rc_mode)
{
  switch (rc_mode) {
    case AOM_VBR:
      return "VBR (Variable Bit Rate)";
    case AOM_CBR:
      return "CBR (Constant Bit Rate)";
    case AOM_CQ:
      return "CQ (Constrained Quality)";
    case AOM_Q:
      return "Q (Constant Quality)";
    default:
      return "<UNKNOWN>";
  }
}

static void
gst_av1_enc_debug_encoder_cfg (struct aom_codec_enc_cfg *cfg)
{
  GST_DEBUG ("g_usage : %u", cfg->g_usage);
  GST_DEBUG ("g_threads : %u", cfg->g_threads);
  GST_DEBUG ("g_profile : %u", cfg->g_profile);
  GST_DEBUG ("g_w x g_h : %u x %u", cfg->g_w, cfg->g_h);
  GST_DEBUG ("g_bit_depth : %d", cfg->g_bit_depth);
  GST_DEBUG ("g_input_bit_depth : %u", cfg->g_input_bit_depth);
  GST_DEBUG ("g_timebase : %d / %d", cfg->g_timebase.num, cfg->g_timebase.den);
  GST_DEBUG ("g_error_resilient : 0x%x", cfg->g_error_resilient);
  GST_DEBUG ("g_pass : %d", cfg->g_pass);
  GST_DEBUG ("g_lag_in_frames : %u", cfg->g_lag_in_frames);
  GST_DEBUG ("rc_dropframe_thresh : %u", cfg->rc_dropframe_thresh);
  GST_DEBUG ("rc_resize_mode : %u", cfg->rc_resize_mode);
  GST_DEBUG ("rc_resize_denominator : %u", cfg->rc_resize_denominator);
  GST_DEBUG ("rc_resize_kf_denominator : %u", cfg->rc_resize_kf_denominator);
  GST_DEBUG ("rc_superres_mode : %u", cfg->rc_superres_mode);
  GST_DEBUG ("rc_superres_denominator : %u", cfg->rc_superres_denominator);
  GST_DEBUG ("rc_superres_kf_denominator : %u",
      cfg->rc_superres_kf_denominator);
  GST_DEBUG ("rc_superres_qthresh : %u", cfg->rc_superres_qthresh);
  GST_DEBUG ("rc_superres_kf_qthresh : %u", cfg->rc_superres_kf_qthresh);
  GST_DEBUG ("rc_end_usage : %s",
      gst_av1_enc_get_aom_rc_mode_name (cfg->rc_end_usage));
  /* rc_twopass_stats_in */
  /* rc_firstpass_mb_stats_in */
  GST_DEBUG ("rc_target_bitrate : %u (kbps)", cfg->rc_target_bitrate);
  GST_DEBUG ("rc_min_quantizer : %u", cfg->rc_min_quantizer);
  GST_DEBUG ("rc_max_quantizer : %u", cfg->rc_max_quantizer);
  GST_DEBUG ("rc_undershoot_pct : %u", cfg->rc_undershoot_pct);
  GST_DEBUG ("rc_overshoot_pct : %u", cfg->rc_overshoot_pct);
  GST_DEBUG ("rc_buf_sz : %u (ms)", cfg->rc_buf_sz);
  GST_DEBUG ("rc_buf_initial_sz : %u (ms)", cfg->rc_buf_initial_sz);
  GST_DEBUG ("rc_buf_optimal_sz : %u (ms)", cfg->rc_buf_optimal_sz);
  GST_DEBUG ("rc_2pass_vbr_bias_pct : %u (%%)", cfg->rc_2pass_vbr_bias_pct);
  GST_DEBUG ("rc_2pass_vbr_minsection_pct : %u (%%)",
      cfg->rc_2pass_vbr_minsection_pct);
  GST_DEBUG ("rc_2pass_vbr_maxsection_pct : %u (%%)",
      cfg->rc_2pass_vbr_maxsection_pct);
  GST_DEBUG ("kf_mode : %u", cfg->kf_mode);
  GST_DEBUG ("kf_min_dist : %u", cfg->kf_min_dist);
  GST_DEBUG ("kf_max_dist : %u", cfg->kf_max_dist);
  GST_DEBUG ("large_scale_tile : %u", cfg->large_scale_tile);
  /* Tile-related values */
}

static gboolean
gst_av1_enc_set_format (GstVideoEncoder * encoder, GstVideoCodecState * state)
{
  GstVideoCodecState *output_state;
  GstAV1Enc *av1enc = GST_AV1_ENC_CAST (encoder);
  GstAV1EncClass *av1enc_class = GST_AV1_ENC_GET_CLASS (av1enc);

  output_state =
      gst_video_encoder_set_output_state (encoder,
      gst_pad_get_pad_template_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder)),
      state);
  gst_video_codec_state_unref (output_state);

  if (av1enc->input_state) {
    gst_video_codec_state_unref (av1enc->input_state);
  }
  av1enc->input_state = gst_video_codec_state_ref (state);

  gst_av1_enc_set_latency (av1enc);

  g_mutex_lock (&av1enc->encoder_lock);
  if (aom_codec_enc_config_default (av1enc_class->codec_algo, &av1enc->aom_cfg,
          0)) {
    gst_av1_codec_error (&av1enc->encoder,
        "Failed to get default codec config.");
    return FALSE;
  }
  GST_DEBUG_OBJECT (av1enc, "Got default encoder config");
  gst_av1_enc_debug_encoder_cfg (&av1enc->aom_cfg);


  av1enc->aom_cfg.g_w = av1enc->input_state->info.width;
  av1enc->aom_cfg.g_h = av1enc->input_state->info.height;
  av1enc->aom_cfg.g_timebase.num = av1enc->input_state->info.fps_d;
  av1enc->aom_cfg.g_timebase.den = av1enc->input_state->info.fps_n;
  /* FIXME : Make configuration properties */
  av1enc->aom_cfg.rc_target_bitrate = 3000;
  av1enc->aom_cfg.g_error_resilient = AOM_ERROR_RESILIENT_DEFAULT;

  GST_DEBUG_OBJECT (av1enc, "Calling encoder init with config:");
  gst_av1_enc_debug_encoder_cfg (&av1enc->aom_cfg);

  if (aom_codec_enc_init (&av1enc->encoder, av1enc_class->codec_algo,
          &av1enc->aom_cfg, 0)) {
    gst_av1_codec_error (&av1enc->encoder, "Failed to initialize encoder");
    return FALSE;
  }
  av1enc->encoder_inited = TRUE;

  GST_AV1_ENC_APPLY_CODEC_CONTROL (av1enc, AOME_SET_CPUUSED, av1enc->cpu_used);
  g_mutex_unlock (&av1enc->encoder_lock);

  return TRUE;
}

static GstFlowReturn
gst_av1_enc_process (GstAV1Enc * encoder)
{
  aom_codec_iter_t iter = NULL;
  const aom_codec_cx_pkt_t *pkt;
  GstVideoCodecFrame *frame;
  GstVideoEncoder *video_encoder;

  video_encoder = GST_VIDEO_ENCODER (encoder);

  while ((pkt = aom_codec_get_cx_data (&encoder->encoder, &iter)) != NULL) {
    if (pkt->kind == AOM_CODEC_STATS_PKT) {
      GST_WARNING_OBJECT (encoder, "Unhandled stats packet");
    } else if (pkt->kind == AOM_CODEC_FPMB_STATS_PKT) {
      GST_WARNING_OBJECT (encoder, "Unhandled FPMB pkt");
    } else if (pkt->kind == AOM_CODEC_PSNR_PKT) {
      GST_WARNING_OBJECT (encoder, "Unhandled PSNR packet");
    } else if (pkt->kind == AOM_CODEC_CX_FRAME_PKT) {
      frame = gst_video_encoder_get_oldest_frame (video_encoder);
      g_assert (frame != NULL);
      if ((pkt->data.frame.flags & AOM_FRAME_IS_KEY) != 0) {
        GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
      } else {
        GST_VIDEO_CODEC_FRAME_UNSET_SYNC_POINT (frame);
      }

      frame->output_buffer =
          gst_buffer_new_wrapped (g_memdup (pkt->data.frame.buf,
              pkt->data.frame.sz), pkt->data.frame.sz);
      gst_video_encoder_finish_frame (video_encoder, frame);
    }
  }

  return GST_FLOW_OK;
}

static void
gst_av1_enc_fill_image (GstAV1Enc * enc, GstVideoFrame * frame,
    aom_image_t * image)
{
  image->planes[AOM_PLANE_Y] = GST_VIDEO_FRAME_COMP_DATA (frame, 0);
  image->planes[AOM_PLANE_U] = GST_VIDEO_FRAME_COMP_DATA (frame, 1);
  image->planes[AOM_PLANE_V] = GST_VIDEO_FRAME_COMP_DATA (frame, 2);

  image->stride[AOM_PLANE_Y] = GST_VIDEO_FRAME_COMP_STRIDE (frame, 0);
  image->stride[AOM_PLANE_U] = GST_VIDEO_FRAME_COMP_STRIDE (frame, 1);
  image->stride[AOM_PLANE_V] = GST_VIDEO_FRAME_COMP_STRIDE (frame, 2);
}

static GstFlowReturn
gst_av1_enc_handle_frame (GstVideoEncoder * encoder, GstVideoCodecFrame * frame)
{
  GstAV1Enc *av1enc = GST_AV1_ENC_CAST (encoder);
  aom_image_t raw;
  int flags = 0;
  GstFlowReturn ret = GST_FLOW_OK;
  GstVideoFrame vframe;

  if (!aom_img_alloc (&raw, AOM_IMG_FMT_I420, av1enc->aom_cfg.g_w,
          av1enc->aom_cfg.g_h, 1)) {
    GST_ERROR_OBJECT (encoder, "Failed to initialize encoder");
    return FALSE;
  }

  gst_video_frame_map (&vframe, &av1enc->input_state->info,
      frame->input_buffer, GST_MAP_READ);
  gst_av1_enc_fill_image (av1enc, &vframe, &raw);
  gst_video_frame_unmap (&vframe);

  if (av1enc->keyframe_dist >= 30) {
    av1enc->keyframe_dist = 0;
    flags |= AOM_EFLAG_FORCE_KF;
  }
  av1enc->keyframe_dist++;

  g_mutex_lock (&av1enc->encoder_lock);
  if (aom_codec_encode (&av1enc->encoder, &raw, frame->pts, 1, flags)
      != AOM_CODEC_OK) {
    gst_av1_codec_error (&av1enc->encoder, "Failed to encode frame");
    ret = GST_FLOW_ERROR;
  }
  g_mutex_unlock (&av1enc->encoder_lock);

  aom_img_free (&raw);
  gst_video_codec_frame_unref (frame);

  if (ret == GST_FLOW_ERROR) {
    return ret;
  }
  return gst_av1_enc_process (av1enc);
}

static void
gst_av1_enc_destroy_encoder (GstAV1Enc * av1enc)
{
  g_mutex_lock (&av1enc->encoder_lock);
  if (av1enc->encoder_inited) {
    aom_codec_destroy (&av1enc->encoder);
    av1enc->encoder_inited = FALSE;
  }
  g_mutex_unlock (&av1enc->encoder_lock);
}

static gboolean
gst_av1_enc_propose_allocation (GstVideoEncoder * encoder, GstQuery * query)
{
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return GST_VIDEO_ENCODER_CLASS (parent_class)->propose_allocation (encoder,
      query);
}

static void
gst_av1_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAV1Enc *av1enc = GST_AV1_ENC_CAST (object);

  GST_OBJECT_LOCK (av1enc);

  g_mutex_lock (&av1enc->encoder_lock);
  switch (prop_id) {
    case PROP_CPU_USED:
      av1enc->cpu_used = g_value_get_int (value);
      GST_AV1_ENC_APPLY_CODEC_CONTROL (av1enc, AOME_SET_CPUUSED,
          av1enc->cpu_used);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  g_mutex_unlock (&av1enc->encoder_lock);

  GST_OBJECT_UNLOCK (av1enc);
}

static void
gst_av1_enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstAV1Enc *av1enc = GST_AV1_ENC_CAST (object);

  GST_OBJECT_LOCK (av1enc);

  switch (prop_id) {
    case PROP_CPU_USED:
      g_value_set_int (value, av1enc->cpu_used);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (av1enc);
}

static gboolean
gst_av1_enc_start (GstVideoEncoder * encoder)
{
  return TRUE;
}

static gboolean
gst_av1_enc_stop (GstVideoEncoder * encoder)
{
  GstAV1Enc *av1enc = GST_AV1_ENC_CAST (encoder);

  if (av1enc->input_state) {
    gst_video_codec_state_unref (av1enc->input_state);
  }
  av1enc->input_state = NULL;

  gst_av1_enc_destroy_encoder (av1enc);

  return TRUE;
}
