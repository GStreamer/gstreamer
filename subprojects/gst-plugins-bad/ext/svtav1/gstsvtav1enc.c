/*
* Copyright(c) 2019 Intel Corporation
*     Authors: Jun Tian <jun.tian@intel.com> Xavier Hallade <xavier.hallade@intel.com>
* SPDX - License - Identifier: LGPL-2.1-or-later
*/

/**
 * SECTION:element-gstsvtav1enc
 *
 * The svtav1enc element does AV1 encoding using Scalable
 * Video Technology for AV1 Encoder (SVT-AV1 Encoder).
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -e videotestsrc ! video/x-raw ! svtav1enc ! matroskamux ! filesink location=out.mkv
 * ]|
 * Encodes test input into AV1 compressed data which is then packaged in out.mkv
 * </refsect2>
 */

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideoencoder.h>
#include "gstsvtav1enc.h"

GST_DEBUG_CATEGORY_STATIC (gst_svtav1enc_debug_category);
#define GST_CAT_DEFAULT gst_svtav1enc_debug_category

/* prototypes */
static void gst_svtav1enc_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_svtav1enc_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_svtav1enc_dispose (GObject * object);
static void gst_svtav1enc_finalize (GObject * object);

gboolean gst_svtav1enc_allocate_svt_buffers (GstSvtAv1Enc * svtav1enc);
void gst_svthevenc_deallocate_svt_buffers (GstSvtAv1Enc * svtav1enc);
static gboolean gst_svtav1enc_configure_svt (GstSvtAv1Enc * svtav1enc);
static GstFlowReturn gst_svtav1enc_encode (GstSvtAv1Enc * svtav1enc,
    GstVideoCodecFrame * frame);
static gboolean gst_svtav1enc_send_eos (GstSvtAv1Enc * svtav1enc);
static GstFlowReturn gst_svtav1enc_dequeue_encoded_frames (GstSvtAv1Enc *
    svtav1enc, gboolean closing_encoder, gboolean output_frames);

static gboolean gst_svtav1enc_stop (GstVideoEncoder * encoder);
static gboolean gst_svtav1enc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state);
static GstFlowReturn gst_svtav1enc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame);
static GstFlowReturn gst_svtav1enc_finish (GstVideoEncoder * encoder);
static gboolean gst_svtav1enc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query);
static gboolean gst_svtav1enc_flush (GstVideoEncoder * encoder);

/* helpers */
gint compare_video_code_frame_and_pts (const void *video_codec_frame_ptr,
    const void *pts_ptr);

enum
{
  PROP_0,
  PROP_ENCMODE,
  PROP_B_PYRAMID,
  PROP_P_FRAMES,
  PROP_PRED_STRUCTURE,
  PROP_GOP_SIZE,
  PROP_INTRA_REFRESH,
  PROP_QP,
  PROP_QP_MAX,
  PROP_QP_MIN,
  PROP_DEBLOCKING,
  PROP_RC_MODE,
  PROP_BITRATE,
  PROP_LOOKAHEAD,
  PROP_SCD,
  PROP_CORES,
  PROP_SOCKET
};

#define PROP_RC_MODE_CQP 0
#define PROP_RC_MODE_VBR 1

#define PROP_ENCMODE_DEFAULT                8
#define PROP_HIERARCHICAL_LEVEL_DEFAULT     4
#define PROP_P_FRAMES_DEFAULT               0
#define PROP_PRED_STRUCTURE_DEFAULT         2
#define PROP_GOP_SIZE_DEFAULT               -1
#define PROP_INTRA_REFRESH_DEFAULT          1
#define PROP_QP_DEFAULT                     50
#define PROP_DEBLOCKING_DEFAULT             TRUE
#define PROP_RC_MODE_DEFAULT                PROP_RC_MODE_CQP
#define PROP_BITRATE_DEFAULT                7000000
#define PROP_QP_MAX_DEFAULT                 63
#define PROP_QP_MIN_DEFAULT                 0
#define PROP_LOOKAHEAD_DEFAULT              (unsigned int)-1
#define PROP_SCD_DEFAULT                    FALSE
#define PROP_AUD_DEFAULT                    FALSE
#define PROP_CORES_DEFAULT                  0
#define PROP_SOCKET_DEFAULT                 -1

/* pad templates */
static GstStaticPadTemplate gst_svtav1enc_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, "
        "format = (string) {I420, I420_10LE}, "
        "width = (int) [64, 3840], "
        "height = (int) [64, 2160], " "framerate = (fraction) [0, MAX]")
    );

static GstStaticPadTemplate gst_svtav1enc_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-av1, "
        "stream-format = (string) byte-stream, "
        "alignment = (string) au, "
        "width = (int) [64, 3840], "
        "height = (int) [64, 2160], " "framerate = (fraction) [0, MAX]")
    );

/* class initialization */
G_DEFINE_TYPE_WITH_CODE (GstSvtAv1Enc, gst_svtav1enc, GST_TYPE_VIDEO_ENCODER,
    GST_DEBUG_CATEGORY_INIT (gst_svtav1enc_debug_category, "svtav1enc", 0,
        "debug category for SVT-AV1 encoder element"));

/* this mutex is required to avoid race conditions in SVT-AV1 memory allocations, which aren't thread-safe */
G_LOCK_DEFINE_STATIC (init_mutex);

static void
gst_svtav1enc_class_init (GstSvtAv1EncClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstVideoEncoderClass *video_encoder_class = GST_VIDEO_ENCODER_CLASS (klass);

  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &gst_svtav1enc_src_pad_template);

  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &gst_svtav1enc_sink_pad_template);

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "SvtAv1Enc", "Codec/Encoder/Video",
      "Scalable Video Technology for AV1 Encoder (SVT-AV1 Encoder)",
      "Jun Tian <jun.tian@intel.com> Xavier Hallade <xavier.hallade@intel.com>");

  gobject_class->set_property = gst_svtav1enc_set_property;
  gobject_class->get_property = gst_svtav1enc_get_property;
  gobject_class->dispose = gst_svtav1enc_dispose;
  gobject_class->finalize = gst_svtav1enc_finalize;
  video_encoder_class->stop = GST_DEBUG_FUNCPTR (gst_svtav1enc_stop);
  video_encoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_svtav1enc_set_format);
  video_encoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_svtav1enc_handle_frame);
  video_encoder_class->finish = GST_DEBUG_FUNCPTR (gst_svtav1enc_finish);
  video_encoder_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_svtav1enc_propose_allocation);
  video_encoder_class->flush = GST_DEBUG_FUNCPTR (gst_svtav1enc_flush);

  g_object_class_install_property (gobject_class, PROP_ENCMODE,
      g_param_spec_uint ("speed", "speed (Encoder Mode)",
          "Quality vs density tradeoff point"
          " that the encoding is to be performed at"
          " (0 is the highest quality, 12 is the highest speed) ",
          0, 12, PROP_ENCMODE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_B_PYRAMID,
      g_param_spec_uint ("hierarchical-level", "Hierarchical levels",
          "3 : 4 - Level Hierarchy,"
          "4 : 5 - Level Hierarchy",
          3, 4, PROP_HIERARCHICAL_LEVEL_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  //g_object_class_install_property (gobject_class, PROP_P_FRAMES,
  //    g_param_spec_boolean ("p-frames", "P Frames",
  //        "Use P-frames in the base layer",
  //        PROP_P_FRAMES_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  //g_object_class_install_property (gobject_class, PROP_PRED_STRUCTURE,
  //    g_param_spec_uint ("pred-struct", "Prediction Structure",
  //        "0 : Low Delay P, 1 : Low Delay B"
  //        ", 2 : Random Access",
  //        0, 2, PROP_PRED_STRUCTURE_DEFAULT,
  //        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_GOP_SIZE,
      g_param_spec_int ("gop-size", "GOP size",
          "Period of Intra Frames insertion (-1 is auto)",
          -1, 251, PROP_GOP_SIZE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_INTRA_REFRESH,
      g_param_spec_int ("intra-refresh", "Intra refresh type",
          "CRA (open GOP)"
          "or IDR frames (closed GOP)",
          1, 2, PROP_INTRA_REFRESH_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_QP,
      g_param_spec_uint ("qp", "Quantization parameter",
          "Quantization parameter used in CQP mode",
          0, 63, PROP_QP_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DEBLOCKING,
      g_param_spec_boolean ("deblocking", "Deblock Filter",
          "Enable Deblocking Loop Filtering",
          PROP_DEBLOCKING_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_RC_MODE,
      g_param_spec_uint ("rc", "Rate-control mode",
          "0 : CQP, 1 : VBR",
          0, 1, PROP_RC_MODE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* TODO: add GST_PARAM_MUTABLE_PLAYING property and handle it? */
  g_object_class_install_property (gobject_class, PROP_BITRATE,
      g_param_spec_uint ("bitrate", "Target bitrate",
          "Target bitrate in bits/sec. Only used when in VBR mode",
          1, G_MAXUINT, PROP_BITRATE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_QP_MAX,
      g_param_spec_uint ("max-qp", "Max Quantization parameter",
          "Maximum QP value allowed for rate control use"
          " Only used in VBR mode.",
          0, 63, PROP_QP_MAX_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_QP_MIN,
      g_param_spec_uint ("min-qp", "Min Quantization parameter",
          "Minimum QP value allowed for rate control use"
          " Only used in VBR mode.",
          0, 63, PROP_QP_MIN_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_LOOKAHEAD,
      g_param_spec_int ("lookahead", "Look Ahead Distance",
          "Number of frames to look ahead. -1 lets the encoder pick a value",
          -1, 250, PROP_LOOKAHEAD_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SCD,
      g_param_spec_boolean ("scd", "Scene Change Detection",
          "Enable Scene Change Detection algorithm",
          PROP_SCD_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CORES,
      g_param_spec_uint ("cores", "Number of logical cores",
          "Number of logical cores to be used. 0: auto",
          0, UINT_MAX, PROP_CORES_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SOCKET,
      g_param_spec_int ("socket", "Target socket",
          "Target socket to run on. -1: all available",
          -1, 15, PROP_SOCKET_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_svtav1enc_init (GstSvtAv1Enc * svtav1enc)
{
  GST_OBJECT_LOCK (svtav1enc);
  svtav1enc->svt_config = g_malloc (sizeof (EbSvtAv1EncConfiguration));
  if (!svtav1enc->svt_config) {
    GST_ERROR_OBJECT (svtav1enc, "insufficient resources");
    GST_OBJECT_UNLOCK (svtav1enc);
    return;
  }
  memset (&svtav1enc->svt_encoder, 0, sizeof (svtav1enc->svt_encoder));
  svtav1enc->frame_count = 0;

  EbErrorType res =
      svt_av1_enc_init_handle(&svtav1enc->svt_encoder, NULL, svtav1enc->svt_config);
  if (res != EB_ErrorNone) {
    GST_ELEMENT_ERROR (svtav1enc, LIBRARY, INIT, (NULL), ("svt_av1_enc_init_handle failed with error %d", res));
    GST_OBJECT_UNLOCK (svtav1enc);
    return;
  }
  /* setting configuration here since svt_av1_enc_init_handle overrides it */
  GST_OBJECT_UNLOCK (svtav1enc);
}

void
gst_svtav1enc_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSvtAv1Enc *svtav1enc = GST_SVTAV1ENC (object);

  /* TODO: support reconfiguring on the fly when possible */
  if (svtav1enc->state) {
    GST_ERROR_OBJECT (svtav1enc,
        "encoder state has been set before properties, this isn't supported yet.");
    return;
  }

  GST_LOG_OBJECT (svtav1enc, "setting property %u", property_id);

  switch (property_id) {
    case PROP_ENCMODE:
      svtav1enc->svt_config->enc_mode = g_value_get_uint (value);
      break;
    case PROP_GOP_SIZE:
        svtav1enc->svt_config->intra_period_length = g_value_get_int(value) - 1;
        break;
    case PROP_INTRA_REFRESH:
        svtav1enc->svt_config->intra_refresh_type = g_value_get_int(value);
        break;
    case PROP_B_PYRAMID:
      svtav1enc->svt_config->hierarchical_levels = g_value_get_uint (value);
      break;
    case PROP_PRED_STRUCTURE:
        svtav1enc->svt_config->pred_structure = g_value_get_uint(value);
        break;



    case PROP_QP:
      svtav1enc->svt_config->qp = g_value_get_uint (value);
      break;
    case PROP_DEBLOCKING:
      svtav1enc->svt_config->enable_dlf_flag = g_value_get_boolean (value);
      break;
    case PROP_RC_MODE:
      svtav1enc->svt_config->rate_control_mode = g_value_get_uint (value);
      break;
    case PROP_BITRATE:
      svtav1enc->svt_config->target_bit_rate = g_value_get_uint (value) * 1000;
      break;
    case PROP_QP_MAX:
      svtav1enc->svt_config->max_qp_allowed = g_value_get_uint (value);
      break;
    case PROP_QP_MIN:
      svtav1enc->svt_config->min_qp_allowed = g_value_get_uint (value);
      break;
    case PROP_SCD:
      svtav1enc->svt_config->scene_change_detection =
          g_value_get_boolean (value);
      break;
    case PROP_CORES:
      svtav1enc->svt_config->logical_processors = g_value_get_uint (value);
      break;
    case PROP_SOCKET:
      svtav1enc->svt_config->target_socket = g_value_get_int (value);
      break;
    case PROP_LOOKAHEAD:
      svtav1enc->svt_config->look_ahead_distance = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_svtav1enc_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstSvtAv1Enc *svtav1enc = GST_SVTAV1ENC (object);

  GST_LOG_OBJECT (svtav1enc, "getting property %u", property_id);

  switch (property_id) {
    case PROP_ENCMODE:
      g_value_set_uint (value, svtav1enc->svt_config->enc_mode);
      break;
    case PROP_B_PYRAMID:
      g_value_set_uint (value, svtav1enc->svt_config->hierarchical_levels);
      break;




    case PROP_PRED_STRUCTURE:
      g_value_set_uint (value, svtav1enc->svt_config->pred_structure);
      break;
    case PROP_GOP_SIZE:
      g_value_set_int (value, svtav1enc->svt_config->intra_period_length + 1);
      break;
    case PROP_INTRA_REFRESH:
        g_value_set_int(value, svtav1enc->svt_config->intra_refresh_type);
      break;
    case PROP_QP:
      g_value_set_uint (value, svtav1enc->svt_config->qp);
      break;
    case PROP_DEBLOCKING:
      g_value_set_boolean (value, svtav1enc->svt_config->enable_dlf_flag == 1);
      break;
    case PROP_RC_MODE:
      g_value_set_uint (value, svtav1enc->svt_config->rate_control_mode);
      break;
    case PROP_BITRATE:
      g_value_set_uint (value, svtav1enc->svt_config->target_bit_rate / 1000);
      break;
    case PROP_QP_MAX:
      g_value_set_uint (value, svtav1enc->svt_config->max_qp_allowed);
      break;
    case PROP_QP_MIN:
      g_value_set_uint (value, svtav1enc->svt_config->min_qp_allowed);
      break;
    case PROP_SCD:
      g_value_set_boolean (value,
          svtav1enc->svt_config->scene_change_detection == 1);
      break;
    case PROP_CORES:
      g_value_set_uint (value, svtav1enc->svt_config->logical_processors);
      break;
    case PROP_SOCKET:
      g_value_set_int (value, svtav1enc->svt_config->target_socket);
      break;
    case PROP_LOOKAHEAD:
      g_value_set_int (value, svtav1enc->svt_config->look_ahead_distance);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_svtav1enc_dispose (GObject * object)
{
  GstSvtAv1Enc *svtav1enc = GST_SVTAV1ENC (object);

  GST_DEBUG_OBJECT (svtav1enc, "dispose");

  /* clean up as possible.  may be called multiple times */
  if (svtav1enc->state)
    gst_video_codec_state_unref (svtav1enc->state);
  svtav1enc->state = NULL;

  G_OBJECT_CLASS (gst_svtav1enc_parent_class)->dispose (object);
}

void
gst_svtav1enc_finalize (GObject * object)
{
  GstSvtAv1Enc *svtav1enc = GST_SVTAV1ENC (object);

  GST_DEBUG_OBJECT (svtav1enc, "finalizing svtav1enc");

  GST_OBJECT_LOCK (svtav1enc);
  svt_av1_enc_deinit_handle(svtav1enc->svt_encoder);
  svtav1enc->svt_encoder = NULL;
  g_free (svtav1enc->svt_config);
  GST_OBJECT_UNLOCK (svtav1enc);

  G_OBJECT_CLASS (gst_svtav1enc_parent_class)->finalize (object);
}

gboolean
gst_svtav1enc_allocate_svt_buffers (GstSvtAv1Enc * svtav1enc)
{
  svtav1enc->input_buf = g_malloc (sizeof (EbBufferHeaderType));
  if (!svtav1enc->input_buf) {
    GST_ERROR_OBJECT (svtav1enc, "insufficient resources");
    return FALSE;
  }
  svtav1enc->input_buf->p_buffer = g_malloc (sizeof (EbSvtIOFormat));
  if (!svtav1enc->input_buf->p_buffer) {
    GST_ERROR_OBJECT (svtav1enc, "insufficient resources");
    return FALSE;
  }
  memset(svtav1enc->input_buf->p_buffer, 0, sizeof(EbSvtIOFormat));
  svtav1enc->input_buf->size = sizeof (EbBufferHeaderType);
  svtav1enc->input_buf->p_app_private = NULL;
  svtav1enc->input_buf->pic_type = EB_AV1_INVALID_PICTURE;
  svtav1enc->input_buf->metadata = NULL;

  return TRUE;
}

void
gst_svthevenc_deallocate_svt_buffers (GstSvtAv1Enc * svtav1enc)
{
  if (svtav1enc->input_buf) {
    g_free (svtav1enc->input_buf->p_buffer);
    svtav1enc->input_buf->p_buffer = NULL;
    g_free (svtav1enc->input_buf);
    svtav1enc->input_buf = NULL;
  }
}

gboolean
gst_svtav1enc_configure_svt (GstSvtAv1Enc * svtav1enc)
{
  if (!svtav1enc->state) {
    GST_WARNING_OBJECT (svtav1enc, "no state, can't configure encoder yet");
    return FALSE;
  }

  /* set properties out of GstVideoInfo */
  GstVideoInfo *info = &svtav1enc->state->info;
  svtav1enc->svt_config->encoder_bit_depth = GST_VIDEO_INFO_COMP_DEPTH (info, 0);
  svtav1enc->svt_config->source_width = GST_VIDEO_INFO_WIDTH (info);
  svtav1enc->svt_config->source_height = GST_VIDEO_INFO_HEIGHT (info);
  svtav1enc->svt_config->frame_rate_numerator = GST_VIDEO_INFO_FPS_N (info)> 0 ? GST_VIDEO_INFO_FPS_N (info) : 1;
  svtav1enc->svt_config->frame_rate_denominator = GST_VIDEO_INFO_FPS_D (info) > 0 ? GST_VIDEO_INFO_FPS_D (info) : 1;
  GST_LOG_OBJECT(svtav1enc,
                "width %d, height %d, framerate %d",
                svtav1enc->svt_config->source_width,
                svtav1enc->svt_config->source_height,
                svtav1enc->svt_config->frame_rate_numerator / svtav1enc->svt_config->frame_rate_denominator);

  /* TODO: better handle HDR metadata when GStreamer will have such support
   * https://gitlab.freedesktop.org/gstreamer/gst-plugins-base/issues/400 */
  if (GST_VIDEO_INFO_COLORIMETRY (info).matrix == GST_VIDEO_COLOR_MATRIX_BT2020
      && GST_VIDEO_INFO_COMP_DEPTH (info, 0) > 8) {
    svtav1enc->svt_config->high_dynamic_range_input = TRUE;
  }

  EbErrorType res =
      svt_av1_enc_set_parameter(svtav1enc->svt_encoder, svtav1enc->svt_config);
  if (res != EB_ErrorNone) {
    GST_ELEMENT_ERROR (svtav1enc, LIBRARY, INIT, (NULL), ("svt_av1_enc_set_parameter failed with error %d", res));
    return FALSE;
  }
  return TRUE;
}

gboolean
gst_svtav1enc_start_svt (GstSvtAv1Enc * svtav1enc)
{
  G_LOCK (init_mutex);
  EbErrorType res = svt_av1_enc_init(svtav1enc->svt_encoder);
  G_UNLOCK (init_mutex);

  if (res != EB_ErrorNone) {
    GST_ELEMENT_ERROR (svtav1enc, LIBRARY, INIT, (NULL), ("svt_av1_enc_init failed with error %d", res));
    return FALSE;
  }
  return TRUE;
}

GstFlowReturn
gst_svtav1enc_encode (GstSvtAv1Enc * svtav1enc, GstVideoCodecFrame * frame)
{
  GstFlowReturn ret = GST_FLOW_OK;
  EbErrorType res = EB_ErrorNone;
  EbBufferHeaderType *input_buffer = svtav1enc->input_buf;
  EbSvtIOFormat *input_picture_buffer =
      (EbSvtIOFormat *) svtav1enc->input_buf->p_buffer;
  GstVideoFrame video_frame;

  if (!gst_video_frame_map (&video_frame, &svtav1enc->state->info,
          frame->input_buffer, GST_MAP_READ)) {
    GST_ELEMENT_ERROR (svtav1enc, LIBRARY, ENCODE, (NULL), ("couldn't map input frame"));
    return GST_FLOW_ERROR;
  }

  input_picture_buffer->y_stride =
      GST_VIDEO_FRAME_COMP_STRIDE (&video_frame,
      0) / GST_VIDEO_FRAME_COMP_PSTRIDE (&video_frame, 0);
  input_picture_buffer->cb_stride =
      GST_VIDEO_FRAME_COMP_STRIDE (&video_frame,
      1) / GST_VIDEO_FRAME_COMP_PSTRIDE (&video_frame, 1);
  input_picture_buffer->cr_stride =
      GST_VIDEO_FRAME_COMP_STRIDE (&video_frame,
      2) / GST_VIDEO_FRAME_COMP_PSTRIDE (&video_frame, 2);

  input_picture_buffer->luma = GST_VIDEO_FRAME_PLANE_DATA (&video_frame, 0);
  input_picture_buffer->cb = GST_VIDEO_FRAME_PLANE_DATA (&video_frame, 1);
  input_picture_buffer->cr = GST_VIDEO_FRAME_PLANE_DATA (&video_frame, 2);

  input_buffer->n_filled_len = GST_VIDEO_FRAME_SIZE (&video_frame);

  /* Fill in Buffers Header control data */
  input_buffer->flags = 0;
  input_buffer->p_app_private = NULL;
  input_buffer->pts = frame->pts;
  input_buffer->pic_type = EB_AV1_INVALID_PICTURE;

  if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (frame)) {
    input_buffer->pic_type = EB_AV1_KEY_PICTURE;
  }

  input_buffer->metadata = NULL;

  res = svt_av1_enc_send_picture(svtav1enc->svt_encoder, input_buffer);
  if (res != EB_ErrorNone) {
    GST_ELEMENT_ERROR (svtav1enc, LIBRARY, ENCODE, (NULL), ("error in sending picture to encoder"));
    ret = GST_FLOW_ERROR;
  }
  gst_video_frame_unmap (&video_frame);

  return ret;
}

gboolean
gst_svtav1enc_send_eos (GstSvtAv1Enc * svtav1enc)
{
  EbErrorType ret = EB_ErrorNone;

  EbBufferHeaderType input_buffer;
  input_buffer.n_alloc_len = 0;
  input_buffer.n_filled_len = 0;
  input_buffer.n_tick_count = 0;
  input_buffer.p_app_private = NULL;
  input_buffer.flags = EB_BUFFERFLAG_EOS;
  input_buffer.p_buffer = NULL;
  input_buffer.metadata = NULL;

  ret = svt_av1_enc_send_picture(svtav1enc->svt_encoder, &input_buffer);

  if (ret != EB_ErrorNone) {
    GST_ELEMENT_ERROR (svtav1enc, LIBRARY, ENCODE, (NULL), ("couldn't send EOS frame."));
    return FALSE;
  }

  return (ret == EB_ErrorNone);
}

gboolean
gst_svtav1enc_flush (GstVideoEncoder * encoder)
{
  GstFlowReturn ret =
      gst_svtav1enc_dequeue_encoded_frames (GST_SVTAV1ENC (encoder), TRUE,
      FALSE);

  return (ret != GST_FLOW_ERROR);
}

GstFlowReturn
gst_svtav1enc_dequeue_encoded_frames (GstSvtAv1Enc * svtav1enc,
    gboolean done_sending_pics, gboolean output_frames)
{
  GstFlowReturn ret = GST_FLOW_OK;
  EbErrorType res = EB_ErrorNone;
  gboolean encode_at_eos = FALSE;

  do {
    GList *pending_frames = NULL;
    GstVideoCodecFrame *frame = NULL;
    EbBufferHeaderType *output_buf = NULL;

    res =
        svt_av1_enc_get_packet(svtav1enc->svt_encoder, &output_buf,
        done_sending_pics);

    if (output_buf != NULL)
      encode_at_eos =
          ((output_buf->flags & EB_BUFFERFLAG_EOS) == EB_BUFFERFLAG_EOS);

    if (res == EB_ErrorMax) {
      GST_ELEMENT_ERROR (svtav1enc, LIBRARY, ENCODE, (NULL), ("encode failed"));
      return GST_FLOW_ERROR;
    } else if (res != EB_NoErrorEmptyQueue && output_frames && output_buf) {
      // AV1 has no frame re-ordering so always get the oldest frame
      frame = gst_video_encoder_get_oldest_frame (GST_VIDEO_ENCODER (svtav1enc));
      if (output_buf->pic_type == EB_AV1_KEY_PICTURE
          || output_buf->pic_type == EB_AV1_INTRA_ONLY_PICTURE) {
        GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
      }

      if ((ret = gst_video_encoder_allocate_output_frame(GST_VIDEO_ENCODER (svtav1enc), frame, output_buf->n_filled_len)) != GST_FLOW_OK) {
        svt_av1_enc_release_out_buffer(&output_buf);
        gst_video_codec_frame_unref (frame);
        return ret;
      }
      GST_BUFFER_FLAG_SET(frame->output_buffer, GST_BUFFER_FLAG_LIVE);
      gst_buffer_fill (frame->output_buffer, 0,
          output_buf->p_buffer, output_buf->n_filled_len);

      frame->pts = frame->output_buffer->pts = output_buf->pts;

      GST_LOG_OBJECT (svtav1enc, "#frame:%lld pts:%"
          G_GINT64_FORMAT " SliceType:%d\n", svtav1enc->frame_count,
           (frame->pts), output_buf->pic_type);

      svt_av1_enc_release_out_buffer(&output_buf);
      output_buf = NULL;

      ret = gst_video_encoder_finish_frame (GST_VIDEO_ENCODER (svtav1enc), frame);

      if (pending_frames != NULL) {
        g_list_free_full (pending_frames,
            (GDestroyNotify) gst_video_codec_frame_unref);
      }

      svtav1enc->frame_count++;
    }

  } while (res == EB_ErrorNone && !encode_at_eos);

  return ret;
}

static gboolean
gst_svtav1enc_stop (GstVideoEncoder * encoder)
{
  GstSvtAv1Enc *svtav1enc = GST_SVTAV1ENC (encoder);

  GST_DEBUG_OBJECT (svtav1enc, "stop");

  GstVideoCodecFrame *remaining_frame = NULL;
  while ((remaining_frame =
          gst_video_encoder_get_oldest_frame (encoder)) != NULL) {
    GST_WARNING_OBJECT (svtav1enc,
        "encoder is being stopped, dropping frame %d",
        remaining_frame->system_frame_number);
    remaining_frame->output_buffer = NULL;
    gst_video_encoder_finish_frame (encoder, remaining_frame);
  }

  GST_OBJECT_LOCK (svtav1enc);
  if (svtav1enc->state)
    gst_video_codec_state_unref (svtav1enc->state);
  svtav1enc->state = NULL;
  GST_OBJECT_UNLOCK (svtav1enc);

  GST_OBJECT_LOCK (svtav1enc);
  svt_av1_enc_deinit(svtav1enc->svt_encoder);
  /* Destruct the buffer memory pool */
  gst_svthevenc_deallocate_svt_buffers (svtav1enc);
  GST_OBJECT_UNLOCK (svtav1enc);

  return TRUE;
}

static gboolean
gst_svtav1enc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state)
{
  GstSvtAv1Enc *svtav1enc = GST_SVTAV1ENC (encoder);
  GstClockTime min_latency_frames = 0;
  GstCaps *src_caps = NULL;
  GstVideoCodecState *output_state;
  GST_DEBUG_OBJECT (svtav1enc, "set_format");

  if (svtav1enc->state)
    gst_video_codec_state_unref (svtav1enc->state);
  svtav1enc->state = gst_video_codec_state_ref (state);

  gst_svtav1enc_configure_svt (svtav1enc);
  gst_svtav1enc_allocate_svt_buffers (svtav1enc);
  gst_svtav1enc_start_svt (svtav1enc);

  uint32_t fps = svtav1enc->svt_config->frame_rate_numerator /
                    svtav1enc->svt_config->frame_rate_denominator;
  fps          = fps > 120 ? 120 : fps;
  fps          = fps < 24 ? 24 : fps;

  min_latency_frames = ((fps * 5) >> 2);

  gst_video_encoder_set_latency(encoder,
                                min_latency_frames * GST_SECOND /
                                    (svtav1enc->svt_config->frame_rate_numerator /
                                     svtav1enc->svt_config->frame_rate_denominator),
                                -1);

  src_caps =
      gst_static_pad_template_get_caps (&gst_svtav1enc_src_pad_template);
  output_state = gst_video_encoder_set_output_state (GST_VIDEO_ENCODER (encoder), src_caps,
      svtav1enc->state);
  gst_caps_unref (src_caps);
  gst_video_codec_state_unref (output_state);

  GST_DEBUG_OBJECT (svtav1enc, "output caps: %" GST_PTR_FORMAT,
      svtav1enc->state->caps);

  return gst_video_encoder_negotiate (encoder);
}

static GstFlowReturn
gst_svtav1enc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstSvtAv1Enc *svtav1enc = GST_SVTAV1ENC (encoder);
  GstFlowReturn ret = GST_FLOW_OK;

  GST_DEBUG_OBJECT (svtav1enc, "handle_frame");

  ret = gst_svtav1enc_encode (svtav1enc, frame);
  if (ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (svtav1enc, "gst_svtav1enc_encode returned %d", ret);
    return ret;
  }

  return gst_svtav1enc_dequeue_encoded_frames (svtav1enc, FALSE, TRUE);
}

static GstFlowReturn
gst_svtav1enc_finish (GstVideoEncoder * encoder)
{
  GstSvtAv1Enc *svtav1enc = GST_SVTAV1ENC (encoder);

  GST_DEBUG_OBJECT (svtav1enc, "finish");

  gst_svtav1enc_send_eos (svtav1enc);

  return gst_svtav1enc_dequeue_encoded_frames (svtav1enc, TRUE, TRUE);
}

static gboolean
gst_svtav1enc_propose_allocation (GstVideoEncoder * encoder, GstQuery * query)
{
  GstSvtAv1Enc *svtav1enc = GST_SVTAV1ENC (encoder);

  GST_DEBUG_OBJECT (svtav1enc, "propose_allocation");

  gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, NULL);

  return GST_VIDEO_ENCODER_CLASS (gst_svtav1enc_parent_class)->propose_allocation (encoder, query);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "svtav1enc", GST_RANK_SECONDARY,
      GST_TYPE_SVTAV1ENC);
}

#ifndef VERSION
#define VERSION "1.0"
#endif
#ifndef PACKAGE
#define PACKAGE "gstreamer-svt-av1"
#endif
#ifndef PACKAGE_NAME
#define PACKAGE_NAME "SVT-AV1 Encoder plugin for GStreamer"
#endif
#ifndef GST_PACKAGE_ORIGIN
#define GST_PACKAGE_ORIGIN "https://gitlab.com/AOMediaCodec"
#endif

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    svtav1enc,
    "Scalable Video Technology for AV1 Encoder (SVT-AV1 Encoder)",
    plugin_init, VERSION, "LGPL", PACKAGE_NAME, GST_PACKAGE_ORIGIN)
