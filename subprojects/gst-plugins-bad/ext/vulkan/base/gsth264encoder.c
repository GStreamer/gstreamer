/* GStreamer
 * Copyright (C) 2021 Intel Corporation
 *    Author: He Junyan <junyan.he@intel.com>
 * Copyright (C) 2023 Michael Grzeschik <m.grzeschik@pengutronix.de>
 * Copyright (C) 2021, 2025 Igalia, S.L.
 *    Author: Stéphane Cerveau <scerveau@igalia.com>
 *    Author: Víctor J́áquez <ceyusa@igalia.com>
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
 * SECTION:gsth264encoder
 * @title: GstH264Encoder
 * @short_description: Base class to implement stateless H.264 encoders
 *
 * This H.264 encoder base class helps in for the generation of GOPs (Group of
 * Pictures) using I, P and B frames, along with SPS and PPS proposals. The
 * subclass is expected to implement the rate control algorithms and the
 * specific accelerator logic.
 *
 * + Extended profile isn't supported.
 * + Only progressive frames are supported (not interlaced)
 * * Neither intra profiles are fully supported
 */

/* ToDo:
* + add SEI message support */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gsth264encoder.h"

/**
 * GST_FLOW_OUTPUT_NOT_READY:
 *
 * A #GstFlowReturn for not ready operations
 */
#define GST_FLOW_OUTPUT_NOT_READY GST_FLOW_CUSTOM_SUCCESS_2

GST_DEBUG_CATEGORY (gst_h264_encoder_debug);
#define GST_CAT_DEFAULT gst_h264_encoder_debug

#define H264ENC_IDR_PERIOD_DEFAULT        0
#define H264ENC_B_FRAMES_DEFAULT          0
#define H264ENC_I_FRAMES_DEFAULT          0
#define H264ENC_NUM_REF_FRAMES_DEFAULT    3
#define H264ENC_B_PYRAMID_DEFAULT         FALSE

typedef struct _GstH264EncoderPrivate GstH264EncoderPrivate;

/* *INDENT-OFF* */
/* Table A-1 - Level limits */
static const GstH264LevelDescriptor _h264_levels[] = {
  /* level   idc                  MaxMBPS   MaxFS   MaxDpbMbs MaxBR   MaxCPB  MinCr */
  { "1",     GST_H264_LEVEL_L1,   1485,     99,     396,      64,     175,    2 },
  { "1b",    GST_H264_LEVEL_L1B,  1485,     99,     396,      128,    350,    2 },
  { "1.1",   GST_H264_LEVEL_L1_1, 3000,     396,    900,      192,    500,    2 },
  { "1.2",   GST_H264_LEVEL_L1_2, 6000,     396,    2376,     384,    1000,   2 },
  { "1.3",   GST_H264_LEVEL_L1_3, 11880,    396,    2376,     768,    2000,   2 },
  { "2",     GST_H264_LEVEL_L2,   11880,    396,    2376,     2000,   2000,   2 },
  { "2.1",   GST_H264_LEVEL_L2_1, 19800,    792,    4752,     4000,   4000,   2 },
  { "2.2",   GST_H264_LEVEL_L2_2, 20250,    1620,   8100,     4000,   4000,   2 },
  { "3",     GST_H264_LEVEL_L3,   40500,    1620,   8100,     10000,  10000,  2 },
  { "3.1",   GST_H264_LEVEL_L3_1, 108000,   3600,   18000,    14000,  14000,  4 },
  { "3.2",   GST_H264_LEVEL_L3_2, 216000,   5120,   20480,    20000,  20000,  4 },
  { "4",     GST_H264_LEVEL_L4,   245760,   8192,   32768,    20000,  25000,  4 },
  { "4.1",   GST_H264_LEVEL_L4_1, 245760,   8192,   32768,    50000,  62500,  2 },
  { "4.2",   GST_H264_LEVEL_L4_2, 522240,   8704,   34816,    50000,  62500,  2 },
  { "5",     GST_H264_LEVEL_L5,   589824,   22080,  110400,   135000, 135000, 2 },
  { "5.1",   GST_H264_LEVEL_L5_1, 983040,   36864,  184320,   240000, 240000, 2 },
  { "5.2",   GST_H264_LEVEL_L5_2, 2073600,  36864,  184320,   240000, 240000, 2 },
  { "6",     GST_H264_LEVEL_L6,   4177920,  139264, 696320,   240000, 240000, 2 },
  { "6.1",   GST_H264_LEVEL_L6_1, 8355840,  139264, 696320,   480000, 480000, 2 },
  { "6.2",   GST_H264_LEVEL_L6_2, 16711680, 139264, 696320,   800000, 800000, 2 },
};

/* Table A-2 - CPB BR NAL factor + H.10.2.1 (r) */
static const struct {
  GstH264Profile profile;
  int cpb_br_nal_factor;
} _h264_nal_factors[] = {
    { GST_H264_PROFILE_BASELINE,       1200 },
    { GST_H264_PROFILE_MAIN,           1200 },
    { GST_H264_PROFILE_EXTENDED,       1200 },
    { GST_H264_PROFILE_STEREO_HIGH,    1500 },
    { GST_H264_PROFILE_MULTIVIEW_HIGH, 1500 },
    { GST_H264_PROFILE_HIGH,           1500 },
    { GST_H264_PROFILE_HIGH10,         3600 },
    { GST_H264_PROFILE_HIGH_422,       4800 },
    { GST_H264_PROFILE_HIGH_444,       4800 },
};

/* TABLE E-1 Meaning of sample aspect ratio indicator */
static const struct {
  int num;
  int den;
} _h264_aspect_ratio[] = {
  {   0,  1 },
  {   1,  1 },
  {  12, 11 },
  {  10, 11 },
  {  16, 11 },
  {  40, 33 },
  {  24, 11 },
  {  20, 11 },
  {  32, 11 },
  {  80, 33 },
  {  18, 11 },
  {  15, 11 },
  {  64, 33 },
  { 160, 99 },
  {   4,  3 },
  {   3,  2 },
  {   2,  1 },
};
/* *INDENT-ON* */

enum
{
  PROP_IDR_PERIOD = 1,          /* aka PROP_KEY_INT_MAX */
  PROP_BFRAMES,
  PROP_IFRAMES,
  PROP_NUM_REF_FRAMES,
  PROP_B_PYRAMID,
  N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES];

struct _GstH264EncoderPrivate
{
  GstVideoCodecState *input_state;

  struct
  {
    guint max_num_reference_list0;
    guint max_num_reference_list1;
    guint preferred_output_delay;
  } config;

  struct
  {
    guint32 idr_period;
    guint num_iframes;
    guint num_bframes;
    guint num_ref_frames;
    gboolean b_pyramid;
  } prop;

  struct
  {
    GstH264Profile profile;
    GstH264Level level;
  } stream;

  struct
  {
    /* frames between two IDR [idr, ...., idr) */
    guint32 idr_period;
    /* How may IDRs we have encoded */
    guint32 total_idr_count;
    /* frames between I/P and P frames [I, B, B, .., B, P) */
    guint32 ip_period;
    /* frames between I frames [I, B, B, .., B, P, ..., I), open GOP */
    guint32 i_period;
    /* B frames between I/P and P. */
    guint32 num_bframes;
    /* Use B pyramid structure in the GOP. */
    gboolean b_pyramid;
    /* Level 0 is the simple B not acting as ref. */
    guint32 highest_pyramid_level;
    /* If open GOP, I frames within a GOP. */
    guint32 num_iframes;
    /* A map of all frames types within a GOP. */
    GArray *frame_map;
    /* current index in the frames types map. */
    guint32 cur_frame_index;
    /* Number of ref frames within current GOP. H264's frame num. */
    guint32 cur_frame_num;
    /* Max frame num within a GOP. */
    guint32 max_frame_num;
    guint32 log2_max_frame_num;
    /* Max poc within a GOP. */
    guint32 max_pic_order_cnt;
    guint32 log2_max_poc_lsb;

    /* Total ref frames of list0 and list1. */
    guint32 num_ref_frames;
    guint32 ref_num_list0;
    guint32 ref_num_list1;

    guint num_reorder_frames;
    guint max_dec_frame_buffering;
    guint max_num_ref_frames;

    GstVideoCodecFrame *last_keyframe;
  } gop;

  /* current params */
  struct
  {
    GstH264SPS sps;
    GstH264PPS pps;
  } params;

  GstClockTime frame_duration;
  guint fps_n;
  guint fps_d;

  GQueue output_list;
  GQueue ref_list;
  GQueue reorder_list;
  GstVecDeque *dts_queue;

  GArray *ref_list0;
  GArray *ref_list1;

  gboolean is_live;
  gboolean need_configure;
};

/**
 * GstH264Encoder:
 *
 * Opaque #GstH264Encoder data structure.
 *
 * Since: 1.28
 */

#define parent_class gst_h264_encoder_parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstH264Encoder, gst_h264_encoder,
    GST_TYPE_VIDEO_ENCODER,
    G_ADD_PRIVATE (GstH264Encoder);
    GST_DEBUG_CATEGORY_INIT (gst_h264_encoder_debug, "h264encoder", 0,
        "H264 Video Encoder"));

GST_DEFINE_MINI_OBJECT_TYPE (GstH264EncoderFrame, gst_h264_encoder_frame);

#define update_property(type, obj, old_val, new_val, prop_id)         \
static inline void                                                    \
gst_h264_encoder_update_property_##type (GstH264Encoder * encoder, type * old_val, type new_val, guint prop_id) \
{ \
 GST_OBJECT_LOCK (encoder);                     \
 if (*old_val == new_val) {                     \
   GST_OBJECT_UNLOCK (encoder);                 \
   return;                                      \
 }                                                                      \
 *old_val = new_val;                                                    \
 GST_OBJECT_UNLOCK (encoder);                                           \
 if (prop_id > 0)                                                       \
   g_object_notify_by_pspec (G_OBJECT (encoder), properties[prop_id]);  \
}

update_property (guint, obj, old_val, new_val, prop_id);
update_property (gboolean, obj, old_val, new_val, prop_id);

#undef update_property

#define update_property_uint(obj, old_val, new_val, prop_id)      \
  gst_h264_encoder_update_property_guint (obj, old_val, new_val, prop_id)
#define update_property_bool(obj, old_val, new_val, prop_id)        \
  gst_h264_encoder_update_property_gboolean (obj, old_val, new_val, prop_id)

#define _GET_PRIV(obj) gst_h264_encoder_get_instance_private (obj)
#define _GET_FRAME(codec_frame) GST_H264_ENCODER_FRAME (gst_video_codec_frame_get_user_data (codec_frame))

static void
gst_h264_encoder_frame_free (GstMiniObject * mini_object)
{
  GstH264EncoderFrame *frame = GST_H264_ENCODER_FRAME (mini_object);

  GST_TRACE ("Free frame %p", frame);
  if (frame->user_data_destroy_notify)
    frame->user_data_destroy_notify (frame->user_data);

  g_free (frame);
}

/**
 * gst_h264_encoder_frame_new:
 *
 * Create new #GstH264EncoderFrame
 *
 * Returns: a new #GstH264EncoderFrame
 */
GstH264EncoderFrame *
gst_h264_encoder_frame_new (void)
{
  GstH264EncoderFrame *frame;

  frame = g_new (GstH264EncoderFrame, 1);

  /* *INDENT-OFF* */
  *frame = (GstH264EncoderFrame) {
    .gop_frame_num = 0,
    .unused_for_reference_pic_num = -1,
  };
  /* *INDENT-ON */

  gst_mini_object_init (GST_MINI_OBJECT_CAST (frame), 0,
      GST_TYPE_H264_ENCODER_FRAME, NULL, NULL, gst_h264_encoder_frame_free);

  GST_TRACE ("New frame %p", frame);

  return frame;
}

/**
 * gst_h264_encoder_frame_set_user_data:
 * @frame: a #GstH264EncoderFrame
 * @user_data: private data
 * @notify: (closure user_data): a #GDestroyNotify
 *
 * Sets @user_data on the frame and the #GDestroyNotify that will be called when
 * the frame is freed. Allows to attach private data by the subclass to frames.
 *
 * If a @user_data was previously set, then the previous set @notify will be called
 * before the @user_data is replaced.
 */
void
gst_h264_encoder_frame_set_user_data (GstH264EncoderFrame * frame,
    gpointer user_data, GDestroyNotify notify)
{
  if (frame->user_data_destroy_notify)
    frame->user_data_destroy_notify (frame->user_data);

  frame->user_data = user_data;
  frame->user_data_destroy_notify = notify;
}

/**
 * gst_h264_encoder_frame_get_user_data:
 * @frame: a #GstH264EncoderFrame
 *
 * Gets private data set on the frame by the subclass via
 * gst_video_codec_frame_set_user_data() previously.
 *
 * Returns: (transfer none): The previously set user_data
 */


struct PyramidInfo
{
  guint level;
  gint left_ref_poc_diff;
  gint right_ref_poc_diff;
};

/* recursive function */
static void
gst_h264_encoder_set_pyramid_info (struct PyramidInfo *info, guint len,
    guint current_level, guint highest_level)
{
  guint index;

  g_assert (len >= 1 && len <= 31);

  if (current_level == highest_level || len == 1) {
    for (index = 0; index < len; index++) {
      info[index].level = current_level;
      info[index].left_ref_poc_diff = (index + 1) * -2;
      info[index].right_ref_poc_diff = (len - index) * 2;
    }

    return;
  }

  index = len / 2;
  info[index].level = current_level;
  info[index].left_ref_poc_diff = (index + 1) * -2;
  info[index].right_ref_poc_diff = (len - index) * 2;

  current_level++;

  if (index > 0) {
    gst_h264_encoder_set_pyramid_info (info, index, current_level,
        highest_level);
  }

  if (index + 1 < len) {
    gst_h264_encoder_set_pyramid_info (&info[index + 1], len - (index + 1),
        current_level, highest_level);
  }
}

static void
gst_h264_encoder_create_gop_frame_map (GstH264Encoder * self)
{
  GstH264EncoderPrivate *priv = _GET_PRIV (self);
  guint i;
  guint i_frames = priv->gop.num_iframes;
  struct PyramidInfo pyramid_info[31] = { 0, };
  GstH264GOPFrame *gop_frame;

  if (priv->gop.highest_pyramid_level > 0) {
    g_assert (priv->gop.num_bframes > 0);
    gst_h264_encoder_set_pyramid_info (pyramid_info, priv->gop.num_bframes,
        0, priv->gop.highest_pyramid_level);
  }

  if (!priv->gop.frame_map) {
    priv->gop.frame_map = g_array_sized_new (TRUE, TRUE,
        sizeof (GstH264GOPFrame), priv->gop.idr_period);
  } else {
    priv->gop.frame_map = g_array_set_size (priv->gop.frame_map,
        priv->gop.idr_period);
  }

  for (i = 0; i < priv->gop.idr_period; i++) {
    gop_frame = &g_array_index (priv->gop.frame_map, GstH264GOPFrame, i);

    if (i == 0) {
      gop_frame->slice_type = GST_H264_I_SLICE;
      gop_frame->is_ref = TRUE;
      continue;
    }

    /* Intra only stream. */
    if (priv->gop.ip_period == 0) {
      gop_frame->slice_type = GST_H264_I_SLICE;
      gop_frame->is_ref = FALSE;
      continue;
    }

    if (i % priv->gop.ip_period) {
      guint pyramid_index =
          i % priv->gop.ip_period - 1 /* The first P or IDR */ ;

      gop_frame->slice_type = GST_H264_B_SLICE;
      gop_frame->pyramid_level = pyramid_info[pyramid_index].level;
      gop_frame->is_ref =
          (gop_frame->pyramid_level < priv->gop.highest_pyramid_level);
      gop_frame->left_ref_poc_diff =
          pyramid_info[pyramid_index].left_ref_poc_diff;
      gop_frame->right_ref_poc_diff =
          pyramid_info[pyramid_index].right_ref_poc_diff;
      continue;
    }

    if (priv->gop.i_period && i % priv->gop.i_period == 0 && i_frames > 0) {
      /* Replace P with I. */
      gop_frame->slice_type = GST_H264_I_SLICE;
      gop_frame->is_ref = TRUE;
      i_frames--;
      continue;
    }

    gop_frame->slice_type = GST_H264_P_SLICE;
    gop_frame->is_ref = TRUE;
  }

  /* Force the last one to be a P */
  if (priv->gop.idr_period > 1 && priv->gop.ip_period > 0) {
    gop_frame = &g_array_index (priv->gop.frame_map, GstH264GOPFrame,
        priv->gop.idr_period - 1);

    gop_frame->slice_type = GST_H264_P_SLICE;
    gop_frame->is_ref = TRUE;
  }
}

static void
gst_h264_encoder_print_gop_structure (GstH264Encoder * self)
{
#ifndef GST_DISABLE_GST_DEBUG
  GstH264EncoderPrivate *priv = _GET_PRIV (self);
  GString *str;
  guint i;

  if (gst_debug_category_get_threshold (GST_CAT_DEFAULT) < GST_LEVEL_INFO)
    return;

  str = g_string_new (NULL);

  g_string_append_printf (str, "[ ");

  for (i = 0; i < priv->gop.idr_period; i++) {
    GstH264GOPFrame *gop_frame =
        &g_array_index (priv->gop.frame_map, GstH264GOPFrame, i);
    if (i == 0) {
      g_string_append_printf (str, "IDR");
      continue;
    } else {
      g_string_append_printf (str, ", ");
    }

    g_string_append_printf (str, "%s",
        gst_h264_slice_type_to_string (gop_frame->slice_type));

    if (priv->gop.b_pyramid && gop_frame->slice_type == GST_H264_B_SLICE) {
      g_string_append_printf (str, "<L%d (%d, %d)>",
          gop_frame->pyramid_level,
          gop_frame->left_ref_poc_diff, gop_frame->right_ref_poc_diff);
    }

    if (gop_frame->is_ref) {
      g_string_append_printf (str, "(ref)");
    }
  }

  g_string_append_printf (str, " ]");

  GST_INFO_OBJECT (self, "GOP size: %d, forward reference %d, backward"
      " reference %d, GOP structure: %s", priv->gop.idr_period,
      priv->gop.ref_num_list0, priv->gop.ref_num_list1, str->str);

  g_string_free (str, TRUE);
#endif
}

/*
 * TODO:
 * + Load some preset fixed GOP structure.
 * + Skip this if in lookahead mode.
 */
static void
gst_h264_encoder_generate_gop_structure (GstH264Encoder * self)
{
  GstH264EncoderPrivate *priv = _GET_PRIV (self);
  guint32 list0, list1, gop_ref_num;
  gint32 p_frames;

  if (priv->stream.profile == GST_H264_PROFILE_BASELINE)
    priv->gop.num_bframes = 0;

  /* If not set, generate a idr every second */
  if (priv->gop.idr_period == 0) {
    priv->gop.idr_period = (priv->fps_n + priv->fps_d - 1) / priv->fps_d;
  }

  /* Prefer have more than 1 reference for the GOP which is not very small. */
  if (priv->gop.idr_period > 8) {
    if (priv->gop.num_bframes > (priv->gop.idr_period - 1) / 2) {
      priv->gop.num_bframes = (priv->gop.idr_period - 1) / 2;
      GST_INFO_OBJECT (self, "Lowering the number of num_bframes to %d",
          priv->gop.num_bframes);
    }
  } else {
    /* begin and end should be reference */
    if (priv->gop.num_bframes > priv->gop.idr_period - 1 - 1) {
      if (priv->gop.idr_period > 1) {
        priv->gop.num_bframes = priv->gop.idr_period - 1 - 1;
      } else {
        priv->gop.num_bframes = 0;
      }
      GST_INFO_OBJECT (self, "Lowering the number of num_bframes to %d",
          priv->gop.num_bframes);
    }
  }

  list0 = MIN (priv->config.max_num_reference_list0, priv->gop.num_ref_frames);
  list1 = MIN (priv->config.max_num_reference_list1, priv->gop.num_ref_frames);

  if (list0 == 0) {
    GST_INFO_OBJECT (self,
        "No reference support, fallback to intra only stream");

    /* It does not make sense that if only the list1 exists. */
    priv->gop.num_ref_frames = 0;

    priv->gop.ip_period = 0;
    priv->gop.num_bframes = 0;
    priv->gop.b_pyramid = FALSE;
    priv->gop.highest_pyramid_level = 0;
    priv->gop.num_iframes = priv->gop.idr_period - 1 /* The idr */ ;
    priv->gop.ref_num_list0 = 0;
    priv->gop.ref_num_list1 = 0;
    goto create_poc;
  }

  if (priv->gop.num_ref_frames <= 1) {
    GST_INFO_OBJECT (self, "The number of reference frames is only %d,"
        " no B frame allowed, fallback to I/P mode", priv->gop.num_ref_frames);
    priv->gop.num_bframes = 0;
    list1 = 0;
  }

  /* b_pyramid needs at least 1 ref for B, besides the I/P */
  if (priv->gop.b_pyramid && priv->gop.num_ref_frames <= 1) {
    GST_INFO_OBJECT (self, "The number of reference frames is only %d,"
        " not enough for b_pyramid", priv->gop.num_ref_frames);
    priv->gop.b_pyramid = FALSE;
  }

  if (list1 == 0 && priv->gop.num_bframes > 0) {
    GST_INFO_OBJECT (self,
        "No max reference count for list 1, fallback to I/P mode");
    priv->gop.num_bframes = 0;
    priv->gop.b_pyramid = FALSE;
  }

  /* I/P mode, no list1 needed. */
  if (priv->gop.num_bframes == 0)
    list1 = 0;

  /* Not enough B frame, no need for b_pyramid. */
  if (priv->gop.num_bframes <= 1)
    priv->gop.b_pyramid = FALSE;

  /* b pyramid has only one backward reference. */
  if (priv->gop.b_pyramid)
    list1 = 1;

  if (priv->gop.num_ref_frames > list0 + list1) {
    priv->gop.num_ref_frames = list0 + list1;
    GST_WARNING_OBJECT (self, "number of reference frames is bigger than max "
        "reference count. Lowered number of reference frames to %d",
        priv->gop.num_ref_frames);
  }

  /* How many possible refs within a GOP. */
  gop_ref_num = (priv->gop.idr_period + priv->gop.num_bframes) /
      (priv->gop.num_bframes + 1);

  /* The end reference. */
  if (priv->gop.num_bframes > 0
      /* frame_num % (priv->gop.num_bframes + 1) happens to be the end P */
      && (priv->gop.idr_period % (priv->gop.num_bframes + 1) != 1))
    gop_ref_num++;

  /* Adjust reference num based on B frames and B pyramid. */
  if (priv->gop.num_bframes == 0) {
    priv->gop.b_pyramid = FALSE;
    priv->gop.ref_num_list0 = priv->gop.num_ref_frames;
    priv->gop.ref_num_list1 = 0;
  } else if (priv->gop.b_pyramid) {
    guint b_frames = priv->gop.num_bframes;

    /* b pyramid has only one backward ref. */
    g_assert (list1 == 1);
    priv->gop.ref_num_list1 = list1;
    priv->gop.ref_num_list0 =
        MIN (priv->gop.num_ref_frames - priv->gop.ref_num_list1, list0);

    b_frames = b_frames / 2;
    while (b_frames) {
      /* All the reference pictures and the current picture should be in the
         DPB. So each B level as reference, plus the IDR or P in both ends and
         the current picture should not exceed the max_dpb_size. */
      if (priv->gop.highest_pyramid_level + 2 + 1 == 16)
        break;

      priv->gop.highest_pyramid_level++;
      b_frames = b_frames / 2;
    }

    GST_INFO_OBJECT (self, "pyramid level is %d",
        priv->gop.highest_pyramid_level);
  } else {
    /* We prefer list0. Backward references have more latency. */
    priv->gop.ref_num_list1 = 1;
    priv->gop.ref_num_list0 =
        priv->gop.num_ref_frames - priv->gop.ref_num_list1;
    /* Balance the forward and backward references, but not cause a big
       latency. */
    while ((priv->gop.num_bframes * priv->gop.ref_num_list1 <= 16)
        && (priv->gop.ref_num_list1 <= gop_ref_num)
        && (priv->gop.ref_num_list1 < list1)
        && (priv->gop.ref_num_list0 / priv->gop.ref_num_list1 > 4)) {
      priv->gop.ref_num_list0--;
      priv->gop.ref_num_list1++;
    }

    if (priv->gop.ref_num_list0 > list0)
      priv->gop.ref_num_list0 = list0;
  }

  /* It's OK, keep slots for GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME frame. */
  if (priv->gop.ref_num_list0 > gop_ref_num) {
    GST_DEBUG_OBJECT (self, "num_ref_frames %d is bigger than gop_ref_num %d",
        priv->gop.ref_num_list0, gop_ref_num);
  }

  /* Include the reference picture itself. */
  priv->gop.ip_period = 1 + priv->gop.num_bframes;

  p_frames = MAX (gop_ref_num - 1 /* IDR */, 0);
  if (priv->gop.num_iframes > p_frames) {
    priv->gop.num_iframes = p_frames;
    GST_INFO_OBJECT (self, "Too many I frames insertion, lowering it to %d",
        priv->gop.num_iframes);
  }

  if (priv->gop.num_iframes > 0) {
    guint total_i_frames = priv->gop.num_iframes + 1 /* IDR */ ;
    priv->gop.i_period =
        (gop_ref_num / total_i_frames) * (priv->gop.num_bframes + 1);
  }

create_poc:
  /* initialize max_frame_num and max_poc. */
  priv->gop.log2_max_frame_num = 4;
  while ((1 << priv->gop.log2_max_frame_num) <= priv->gop.idr_period)
    priv->gop.log2_max_frame_num++;

  priv->gop.max_frame_num = (1 << priv->gop.log2_max_frame_num);
  priv->gop.log2_max_poc_lsb = priv->gop.log2_max_frame_num + 1;

  /* 8.2.1.1 Decoding process for picture order count type 0: For intra only
     stream, because all frames are non-reference, poc is easy to wrap. Need to
     increase the max poc. */
  if (priv->gop.ip_period == 0)
    priv->gop.log2_max_poc_lsb++;
  priv->gop.max_pic_order_cnt = (1 << priv->gop.log2_max_poc_lsb);

  /* Intra only stream. */
  if (priv->gop.ip_period == 0) {
    priv->gop.num_reorder_frames = 0;

    priv->gop.max_dec_frame_buffering = 1 + 1;  /* IDR and current frame. */
    priv->gop.max_num_ref_frames = 0;
  } else {
    priv->gop.num_reorder_frames = MIN (16, priv->gop.b_pyramid ?
        priv->gop.highest_pyramid_level + 1 /* the last P frame. */ :
        priv->gop.num_bframes > 0 ? priv->gop.ref_num_list1 : 0);

    priv->gop.max_dec_frame_buffering = MIN (16,
        MAX (priv->gop.num_ref_frames + 1, priv->gop.b_pyramid
        ? priv->gop.highest_pyramid_level + 2 + 1
        : priv->gop.num_reorder_frames + 1));

    priv->gop.max_num_ref_frames = priv->gop.max_dec_frame_buffering - 1;
  }

  /* logic from x264 -- keep it in order to support open GOPs in the future */
#if 0
  {
    /* number of refs + current frame */
    guint max_frame_num =
        priv->gop.max_dec_frame_buffering * (priv->gop.b_pyramid ? 2 : 1) + 1;

    priv->gop.log2_max_frame_num = 4;
    while ((1 << priv->gop.log2_max_frame_num) <= max_frame_num)
      priv->gop.log2_max_frame_num++;

    priv->gop.max_frame_num = (1 << priv->gop.log2_max_frame_num);

    if (priv->gop.num_bframes > 0) { /* poc_type == 0 */
      gint32 max_delta_poc =
          (priv->gop.num_bframes + 2) * (priv->gop.b_pyramid ? 2 : 1) * 2;
      priv->gop.log2_max_poc_lsb = 4;
      while ((1 << priv->gop.log2_max_poc_lsb) <= max_delta_poc * 2)
        priv->gop.log2_max_poc_lsb++;
    }

    priv->gop.max_pic_order_cnt = (1 << priv->gop.log2_max_poc_lsb);
  }
#endif

  gst_h264_encoder_create_gop_frame_map (self);
  gst_h264_encoder_print_gop_structure (self);

  /* updates & notifications */
  update_property_uint (self, &priv->prop.idr_period, priv->gop.idr_period,
      PROP_IDR_PERIOD);
  update_property_uint (self, &priv->prop.num_ref_frames,
      priv->gop.num_ref_frames, PROP_NUM_REF_FRAMES);
  update_property_uint (self, &priv->prop.num_iframes, priv->gop.num_iframes,
      PROP_IFRAMES);
  update_property_bool (self, &priv->prop.b_pyramid, priv->gop.b_pyramid,
      PROP_B_PYRAMID);
  update_property_uint (self, &priv->prop.num_bframes, priv->gop.num_bframes,
      PROP_BFRAMES);
}

static inline void
gst_h264_encoder_flush_lists (GstH264Encoder * self)
{
  GstH264EncoderPrivate *priv = _GET_PRIV (self);

  g_queue_clear_full (&priv->output_list,
      (GDestroyNotify) gst_video_codec_frame_unref);
  g_queue_clear_full (&priv->ref_list,
      (GDestroyNotify) gst_video_codec_frame_unref);
  g_queue_clear_full (&priv->reorder_list,
      (GDestroyNotify) gst_video_codec_frame_unref);

  g_clear_pointer (&priv->gop.frame_map, g_array_unref);
  g_clear_pointer (&priv->dts_queue, gst_vec_deque_free);

  g_clear_pointer (&priv->ref_list0, g_array_unref);
  g_clear_pointer (&priv->ref_list1, g_array_unref);
}

static gboolean
gst_h264_encoder_start (GstVideoEncoder * encoder)
{
  /* Set the minimum pts to some huge value (1000 hours). This keeps
   * the dts at the start of the stream from needing to be negative. */
  gst_video_encoder_set_min_pts (encoder, GST_SECOND * 60 * 60 * 1000);
  return TRUE;
}

static gboolean
gst_h264_encoder_stop (GstVideoEncoder * encoder)
{
  GstH264Encoder *self = GST_H264_ENCODER (encoder);
  GstH264EncoderPrivate *priv = _GET_PRIV (self);

  gst_h264_encoder_flush_lists (self);

  g_clear_pointer (&priv->input_state, gst_video_codec_state_unref);

  return TRUE;
}

static void
gst_h264_encoder_reset (GstH264Encoder * self)
{
  GstH264EncoderPrivate *priv = _GET_PRIV (self);
  GstH264EncoderClass *klass = GST_H264_ENCODER_GET_CLASS (self);


  GST_OBJECT_LOCK (self);
  priv->gop.idr_period = priv->prop.idr_period;
  priv->gop.num_ref_frames = priv->prop.num_ref_frames;
  priv->gop.num_bframes = priv->prop.num_bframes;
  priv->gop.num_iframes = priv->prop.num_iframes;
  priv->gop.b_pyramid = priv->prop.b_pyramid;
  GST_OBJECT_UNLOCK (self);

  priv->stream.profile = GST_H264_PROFILE_INVALID;
  priv->stream.level = 0;

  priv->gop.i_period = 0;
  priv->gop.total_idr_count = 0;
  priv->gop.ip_period = 0;
  priv->gop.highest_pyramid_level = 0;
  if (priv->gop.frame_map)
    g_array_set_size (priv->gop.frame_map, 0);
  priv->gop.cur_frame_index = 0;
  priv->gop.cur_frame_num = 0;
  priv->gop.max_frame_num = 0;
  priv->gop.log2_max_frame_num = 0;
  priv->gop.max_pic_order_cnt = 0;
  priv->gop.log2_max_poc_lsb = 0;
  priv->gop.ref_num_list0 = 0;
  priv->gop.ref_num_list1 = 0;
  priv->gop.num_reorder_frames = 0;
  priv->gop.max_dec_frame_buffering = 0;
  priv->gop.max_num_ref_frames = 0;
  priv->gop.last_keyframe = NULL;

  gst_h264_sps_clear (&priv->params.sps);
  gst_h264_pps_clear (&priv->params.pps);

  g_atomic_int_set (&priv->need_configure, FALSE);

  if (klass->reset)
    klass->reset (self);
}

static gboolean
gst_h264_encoder_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state)
{
  GstH264Encoder *self = GST_H264_ENCODER (encoder);
  GstH264EncoderPrivate *priv = _GET_PRIV (self);
  GstQuery *query;

  if (priv->input_state)
    gst_video_codec_state_unref (priv->input_state);
  priv->input_state = gst_video_codec_state_ref (state);

  priv->fps_d = GST_VIDEO_INFO_FPS_D (&priv->input_state->info);
  priv->fps_n = GST_VIDEO_INFO_FPS_N (&priv->input_state->info);

  /* if still image */
  if (priv->fps_d == 0 || priv->fps_n == 0) {
    priv->fps_d = 1;
    priv->fps_n = 30;
  }

  /* in case live streaming, we should run on low-latency mode */
  priv->is_live = FALSE;
  query = gst_query_new_latency ();
  if (gst_pad_peer_query (GST_VIDEO_ENCODER_SINK_PAD (encoder), query))
    gst_query_parse_latency (query, &priv->is_live, NULL, NULL);
  gst_query_unref (query);

  g_atomic_int_set (&priv->need_configure, TRUE);

  return TRUE;
}

static GstFlowReturn
gst_h264_encoder_finish_frame (GstH264Encoder * self,
    GstVideoCodecFrame * frame)
{
  GstH264EncoderPrivate *priv = _GET_PRIV (self);
  GstH264EncoderClass *base_class = GST_H264_ENCODER_GET_CLASS (self);
  GstH264EncoderFrame *h264_frame = _GET_FRAME (frame);
  GstFlowReturn ret;

  if (gst_vec_deque_get_length (priv->dts_queue) > 0)
    frame->dts =
        *((GstClockTime *) gst_vec_deque_pop_head_struct (priv->dts_queue));
  else
    frame->dts = GST_CLOCK_TIME_NONE;

  if (base_class->prepare_output) {
    ret = base_class->prepare_output (self, frame);
    if (ret == GST_FLOW_ERROR)
      goto prepare_error;
    else if (ret == GST_FLOW_OUTPUT_NOT_READY)
      return GST_FLOW_OK;
  }

  if (h264_frame->poc == 0) {
    GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
    GST_BUFFER_FLAG_UNSET (frame->output_buffer, GST_BUFFER_FLAG_DELTA_UNIT);
    GST_BUFFER_FLAG_SET (frame->output_buffer, GST_BUFFER_FLAG_HEADER);
  } else {
    GST_VIDEO_CODEC_FRAME_UNSET_SYNC_POINT (frame);
    GST_BUFFER_FLAG_SET (frame->output_buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  }

  GST_LOG_OBJECT (self, "Push to downstream: frame system_frame_number: %d,"
      " pts: %" GST_TIME_FORMAT ", dts: %" GST_TIME_FORMAT
      " duration: %" GST_TIME_FORMAT ", buffer size: %" G_GSIZE_FORMAT,
      frame->system_frame_number, GST_TIME_ARGS (frame->pts),
      GST_TIME_ARGS (frame->dts), GST_TIME_ARGS (frame->duration),
      frame->output_buffer ? gst_buffer_get_size (frame->output_buffer) : 0);

  return gst_video_encoder_finish_frame (GST_VIDEO_ENCODER (self), frame);

prepare_error:
  {
    GST_ERROR_OBJECT (self, "Failed to prepare output");
    gst_clear_buffer (&frame->output_buffer);
    ret = gst_video_encoder_finish_frame (GST_VIDEO_ENCODER (self), frame);
    if (ret != GST_FLOW_OK)
      GST_WARNING_OBJECT (self, "Failed to drop unprepared frame");

    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_h264_encoder_reorder_lists_push (GstH264Encoder * self,
    GstVideoCodecFrame * frame, gboolean last)
{
  GstH264EncoderFrame *h264_frame;
  GstH264EncoderPrivate *priv = _GET_PRIV (self);
  gboolean add_cached_key_frame = FALSE;

  g_return_val_if_fail (priv->gop.cur_frame_index <= priv->gop.idr_period,
      FALSE);

  if (frame) {
    h264_frame = _GET_FRAME (frame);

    /* Force to insert the key frame inside a GOP, just end the current
     * GOP and start a new one. */
    if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (frame) &&
        !(priv->gop.cur_frame_index == 0 ||
            priv->gop.cur_frame_index == priv->gop.idr_period)) {
      GST_DEBUG_OBJECT (self, "system_frame_number: %u is a force key "
          "frame(IDR), begin a new GOP.", frame->system_frame_number);

      h264_frame->type =
          g_array_index (priv->gop.frame_map, GstH264GOPFrame, 0);
      h264_frame->poc = 0;
      h264_frame->force_idr = TRUE;

      /* The previous key frame should be already be poped out. */
      g_assert (priv->gop.last_keyframe == NULL);

      /* An empty reorder list, start the new GOP immediately. */
      if (g_queue_is_empty (&priv->reorder_list)) {
        priv->gop.cur_frame_index = 1;
        priv->gop.cur_frame_num = 0;
        g_queue_clear_full (&priv->ref_list,
            (GDestroyNotify) gst_video_codec_frame_unref);
        last = FALSE;
      } else {
        /* Cache the key frame and end the current GOP.
         * Next time calling this push() without frame, start the new GOP. */
        priv->gop.last_keyframe = frame;
        last = TRUE;
      }

      add_cached_key_frame = TRUE;
    } else {
      /* Begin a new GOP, should have a empty reorder_list. */
      if (priv->gop.cur_frame_index == priv->gop.idr_period) {
        g_assert (g_queue_is_empty (&priv->reorder_list));
        priv->gop.cur_frame_index = 0;
        priv->gop.cur_frame_num = 0;
      }

      if (priv->gop.cur_frame_index == 0) {
        g_assert (h264_frame->poc == 0);
        GST_LOG_OBJECT (self, "system_frame_number: %d, an IDR frame, starts"
            " a new GOP", frame->system_frame_number);

        g_queue_clear_full (&priv->ref_list,
            (GDestroyNotify) gst_video_codec_frame_unref);
      }

      h264_frame->type = g_array_index (priv->gop.frame_map, GstH264GOPFrame,
          priv->gop.cur_frame_index);
      h264_frame->poc =
          (priv->gop.cur_frame_index * 2) % priv->gop.max_pic_order_cnt;

      GST_LOG_OBJECT (self, "Push frame, system_frame_number: %d, poc %d, "
          "frame type %s", frame->system_frame_number, h264_frame->poc,
          gst_h264_slice_type_to_string (h264_frame->type.slice_type));

      priv->gop.cur_frame_index++;

      g_queue_push_tail (&priv->reorder_list,
          gst_video_codec_frame_ref (frame));
    }
  } else if (priv->gop.last_keyframe) {
    g_assert (priv->gop.last_keyframe ==
        g_queue_peek_tail (&priv->reorder_list));

    if (g_queue_get_length (&priv->reorder_list) == 1) {
      /* The last cached key frame begins a new GOP */
      priv->gop.cur_frame_index = 1;
      priv->gop.cur_frame_num = 0;
      priv->gop.last_keyframe = NULL;
      g_queue_clear_full (&priv->ref_list,
          (GDestroyNotify) gst_video_codec_frame_unref);
    }
  }

  /* ensure the last one a non-B and end the GOP. */
  if (last && priv->gop.cur_frame_index < priv->gop.idr_period) {
    GstVideoCodecFrame *last_frame;

    /* Ensure next push will start a new GOP. */
    priv->gop.cur_frame_index = priv->gop.idr_period;

    if (!g_queue_is_empty (&priv->reorder_list)) {
      last_frame = g_queue_peek_tail (&priv->reorder_list);
      h264_frame = _GET_FRAME (last_frame);
      if (h264_frame->type.slice_type == GST_H264_B_SLICE) {
        h264_frame->type.slice_type = GST_H264_P_SLICE;
        h264_frame->type.is_ref = TRUE;
      }
    }
  }

  /* Insert the cached next key frame after ending the current GOP. */
  if (add_cached_key_frame) {
    g_queue_push_tail (&priv->reorder_list, gst_video_codec_frame_ref (frame));
  }

  return TRUE;
}

struct RefFramesCount
{
  gint poc;
  guint num;
};

static void
_count_backward_ref_num (gpointer data, gpointer user_data)
{
  GstH264EncoderFrame *frame = _GET_FRAME (data);
  struct RefFramesCount *count = (struct RefFramesCount *) user_data;

  g_assert (frame->poc != count->poc);
  if (frame->poc > count->poc)
    count->num++;
}

static GstVideoCodecFrame *
_pop_pyramid_b_frame (GstH264Encoder * self, guint gop_len)
{
  GstH264EncoderPrivate *priv = _GET_PRIV (self);
  guint i;
  gint index = -1;
  GstH264EncoderFrame *h264_frame, *b_h264_frame;
  GstVideoCodecFrame *frame, *b_frame;
  struct RefFramesCount count;

  g_assert (priv->gop.ref_num_list1 == 1);

  b_frame = NULL;
  b_h264_frame = NULL;

  /* Find the lowest level with smallest poc. */
  for (i = 0; i < gop_len; i++) {

    frame = g_queue_peek_nth (&priv->reorder_list, i);

    if (!b_frame) {
      b_frame = frame;
      b_h264_frame = _GET_FRAME (b_frame);
      index = i;
      continue;
    }

    h264_frame = _GET_FRAME (frame);
    if (b_h264_frame->type.pyramid_level < h264_frame->type.pyramid_level) {
      b_frame = frame;
      b_h264_frame = h264_frame;
      index = i;
      continue;
    }

    if (b_h264_frame->poc > h264_frame->poc) {
      b_frame = frame;
      b_h264_frame = h264_frame;
      index = i;
    }
  }

again:
  /* Check whether its refs are already poped. */
  g_assert (b_h264_frame->type.left_ref_poc_diff != 0);
  g_assert (b_h264_frame->type.right_ref_poc_diff != 0);

  for (i = 0; i < gop_len; i++) {
    GstH264EncoderFrame *h264_frame;
    GstVideoCodecFrame *frame;

    frame = g_queue_peek_nth (&priv->reorder_list, i);

    if (frame == b_frame)
      continue;

    h264_frame = _GET_FRAME (frame);
    if (h264_frame->poc == b_h264_frame->poc
        + b_h264_frame->type.left_ref_poc_diff
        || h264_frame->poc == b_h264_frame->poc
        + b_h264_frame->type.right_ref_poc_diff) {
      b_frame = frame;
      b_h264_frame = h264_frame;
      index = i;
      goto again;
    }
  }

  /* Ensure we already have enough backward refs */
  count.num = 0;
  count.poc = b_h264_frame->poc;
  g_queue_foreach (&priv->ref_list, (GFunc) _count_backward_ref_num, &count);
  if (count.num >= priv->gop.ref_num_list1) {
    GstVideoCodecFrame *frame;

    /* it will unref at pop_frame */
    frame = g_queue_pop_nth (&priv->reorder_list, index);
    g_assert (frame == b_frame);
  } else {
    b_frame = NULL;
  }

  return b_frame;
}

static gboolean
gst_h264_encoder_reorder_lists_pop (GstH264Encoder * self,
    GstVideoCodecFrame ** out_frame)
{
  GstH264EncoderPrivate *priv = _GET_PRIV (self);
  GstH264EncoderFrame *h264_frame;
  GstVideoCodecFrame *frame;
  struct RefFramesCount count;
  guint gop_len;

  g_return_val_if_fail (priv->gop.cur_frame_index <= priv->gop.idr_period,
      FALSE);

  *out_frame = NULL;

  if (g_queue_is_empty (&priv->reorder_list))
    return TRUE;

  gop_len = g_queue_get_length (&priv->reorder_list);

  if (priv->gop.last_keyframe && gop_len > 1)
    gop_len--;

  /* Return the last pushed non-B immediately. */
  frame = g_queue_peek_nth (&priv->reorder_list, gop_len - 1);
  h264_frame = _GET_FRAME (frame);
  if (h264_frame->type.slice_type != GST_H264_B_SLICE) {
    frame = g_queue_pop_nth (&priv->reorder_list, gop_len - 1);
    goto get_one;
  }

  if (priv->gop.b_pyramid) {
    frame = _pop_pyramid_b_frame (self, gop_len);
    if (!frame)
      return TRUE;
    goto get_one;
  }

  g_assert (priv->gop.ref_num_list1 > 0);

  /* If GOP end, pop anyway. */
  if (priv->gop.cur_frame_index == priv->gop.idr_period) {
    frame = g_queue_pop_head (&priv->reorder_list);
    goto get_one;
  }

  /* Ensure we already have enough backward refs */
  frame = g_queue_peek_head (&priv->reorder_list);
  h264_frame = _GET_FRAME (frame);
  count.num = 0;
  count.poc = h264_frame->poc;
  g_queue_foreach (&priv->ref_list, _count_backward_ref_num, &count);
  if (count.num >= priv->gop.ref_num_list1) {
    frame = g_queue_pop_head (&priv->reorder_list);
    goto get_one;
  }

  return TRUE;

get_one:
  g_assert (priv->gop.cur_frame_num < priv->gop.max_frame_num);

  h264_frame = _GET_FRAME (frame);
  h264_frame->gop_frame_num = priv->gop.cur_frame_num;

  /* Add the frame number for ref frames. */
  if (h264_frame->type.is_ref) {
    if (!g_uint_checked_add (&priv->gop.cur_frame_num, priv->gop.cur_frame_num,
            1))
      return FALSE;
  }

  /* used to identify idr_pic_id, incremented only when are two consecutive
   * IDR */
  if (h264_frame->gop_frame_num == 0) {
    if (!g_uint_checked_add (&priv->gop.total_idr_count,
            priv->gop.total_idr_count, 1))
      return FALSE;
  }

  h264_frame->idr_pic_id = priv->gop.total_idr_count;

  if (priv->gop.b_pyramid && h264_frame->type.slice_type == GST_H264_B_SLICE) {
    GST_LOG_OBJECT (self, "pop a pyramid B frame with system_frame_number:"
        " %d, poc: %d, frame num: %d, is_ref: %s, level %d",
        frame->system_frame_number, h264_frame->poc,
        h264_frame->gop_frame_num, h264_frame->type.is_ref ? "true" : "false",
        h264_frame->type.pyramid_level);
  } else {
    GST_LOG_OBJECT (self, "pop a frame with system_frame_number: %d,"
        " frame type: %s, poc: %d, frame num: %d, is_ref: %s",
        frame->system_frame_number,
        gst_h264_slice_type_to_string (h264_frame->type.slice_type),
        h264_frame->poc, h264_frame->gop_frame_num,
        h264_frame->type.is_ref ? "true" : "false");
  }

  /* unref frame popped from queue or pyramid b_frame */
  gst_video_codec_frame_unref (frame);
  *out_frame = frame;
  return TRUE;
}

static gboolean
gst_h264_encoder_reorder_frame (GstH264Encoder * self,
    GstVideoCodecFrame * frame, gboolean bump_all,
    GstVideoCodecFrame ** out_frame)
{
  if (!gst_h264_encoder_reorder_lists_push (self, frame, bump_all)) {
    GST_ERROR_OBJECT (self, "Failed to push the input frame"
        " system_frame_number: %d into the reorder list",
        frame->system_frame_number);

    *out_frame = NULL;
    return FALSE;
  }

  if (!gst_h264_encoder_reorder_lists_pop (self, out_frame)) {
    GST_ERROR_OBJECT (self, "Failed to pop the frame from the reorder list");
    *out_frame = NULL;
    return FALSE;
  }

  return TRUE;
}

static void
_update_ref_pic_marking_for_unused_frame (GstH264SliceHdr * slice_hdr,
    GstH264EncoderFrame * frame)
{
  GstH264RefPicMarking *refpicmarking;

  slice_hdr->dec_ref_pic_marking.adaptive_ref_pic_marking_mode_flag = 1;
  slice_hdr->dec_ref_pic_marking.n_ref_pic_marking = 2;

  refpicmarking = &slice_hdr->dec_ref_pic_marking.ref_pic_marking[0];

  refpicmarking->memory_management_control_operation = 1;
  refpicmarking->difference_of_pic_nums_minus1 =
      frame->gop_frame_num - frame->unused_for_reference_pic_num - 1;

  refpicmarking = &slice_hdr->dec_ref_pic_marking.ref_pic_marking[1];
  refpicmarking->memory_management_control_operation = 0;
}

static gint
_frame_num_asc_compare (const GstH264EncoderFrame ** a,
    const GstH264EncoderFrame ** b)
{
  return (*a)->gop_frame_num - (*b)->gop_frame_num;
}

static gint
_frame_num_desc_compare (const GstH264EncoderFrame ** a,
    const GstH264EncoderFrame ** b)
{
  return (*b)->gop_frame_num - (*a)->gop_frame_num;
}

static void
_update_ref_pic_list_modification (GstH264SliceHdr * slice_hdr, GArray * list,
    gboolean is_asc)
{
  GArray *list_by_pic_num;
  guint modified, i;
  GstH264RefPicListModification *ref_pic_list_modification = NULL;
  guint16 pic_num_lx_pred;

  list_by_pic_num = g_array_copy (list);

  if (is_asc)
    g_array_sort (list_by_pic_num, (GCompareFunc) _frame_num_asc_compare);
  else
    g_array_sort (list_by_pic_num, (GCompareFunc) _frame_num_desc_compare);

  modified = 0;
  for (i = 0; i < list->len; i++) {
    GstH264EncoderFrame *frame_poc =
        g_array_index (list, GstH264EncoderFrame *, i);
    GstH264EncoderFrame *frame_framenum =
        g_array_index (list_by_pic_num, GstH264EncoderFrame *, i);

    if (frame_poc->poc != frame_framenum->poc)
      modified++;
  }

  g_array_unref (list_by_pic_num);

  if (modified == 0)
    return;

  if (is_asc) {
    slice_hdr->ref_pic_list_modification_flag_l1 = 1;
    slice_hdr->n_ref_pic_list_modification_l1 = modified + 1;   /* The end operation */
    ref_pic_list_modification = slice_hdr->ref_pic_list_modification_l1;
  } else {
    slice_hdr->ref_pic_list_modification_flag_l0 = 1;
    slice_hdr->n_ref_pic_list_modification_l0 = modified + 1;   /* The end operation */
    ref_pic_list_modification = slice_hdr->ref_pic_list_modification_l0;
  }

  pic_num_lx_pred = slice_hdr->frame_num;
  for (i = 0; i < modified; i++) {
    GstH264EncoderFrame *frame = g_array_index (list, GstH264EncoderFrame *, i);
    gint pic_num_diff = frame->gop_frame_num - pic_num_lx_pred;

    g_assert (pic_num_diff != 0);

    ref_pic_list_modification[i] = (GstH264RefPicListModification) {
      .modification_of_pic_nums_idc = pic_num_diff > 0 ? 1 : 0,
      .value.abs_diff_pic_num_minus1 = ABS (pic_num_diff) - 1,
    };

    /* For the nex loop. */
    pic_num_lx_pred = frame->gop_frame_num;
  }

  /* *INDENT-OFF* */
  ref_pic_list_modification[i] = (GstH264RefPicListModification) {
    .modification_of_pic_nums_idc = 3,
  };
  /* *INDENT-ON* */
}

/* If all the pic_num in the same order, OK. */
static gboolean
_ref_list_need_reorder (GArray * list, gboolean is_asc)
{
  guint i;

  if (list->len <= 1)
    return FALSE;

  for (i = 1; i < list->len; i++) {
    GstH264EncoderFrame *frame = g_array_index (list, GstH264EncoderFrame *, i);
    GstH264EncoderFrame *prev_frame =
        g_array_index (list, GstH264EncoderFrame *, i - 1);
    gint pic_num_diff = frame->gop_frame_num - prev_frame->gop_frame_num;
    g_assert (pic_num_diff != 0);

    if (pic_num_diff > 0 && !is_asc)
      return TRUE;

    if (pic_num_diff < 0 && is_asc)
      return TRUE;
  }

  return FALSE;
}

static void
gst_h264_encoder_slicehdr_init (GstH264Encoder * self,
    GstH264EncoderFrame * frame, GstH264SliceHdr * slice_hdr)
{
  GstH264EncoderPrivate *priv = _GET_PRIV (self);

  g_assert (priv->params.sps.separate_colour_plane_flag == 0);
  /* only progressive so far */
  g_assert (priv->params.sps.frame_mbs_only_flag == 1);

  g_assert (priv->params.pps.pic_order_present_flag == 0);
  g_assert (priv->params.pps.redundant_pic_cnt_present_flag == 0);

  /* *INDENT-OFF* */
  *slice_hdr = (GstH264SliceHdr) {
    .first_mb_in_slice = 0, /* XXX: update if multiple slices */
    .type = frame->type.slice_type,
    .pps = &priv->params.pps,

    /* if seq->separate_colour_plane_flag */
    .colour_plane_id = 0,

    .frame_num = frame->gop_frame_num,

    /* interlaced not supported now. */
    .field_pic_flag = 0,
    .bottom_field_flag = 0,

    /* if nal_unit.type == IDR */
    .idr_pic_id =
        frame->gop_frame_num == 0 ? frame->idr_pic_id : 0,

    /* if seq->pic_order_cnt_type == 0 */
    /* only pic_order_cnt_type 1 is supported now. */
    .pic_order_cnt_lsb = frame->poc,
    /* if seq->pic_order_present_flag && !field_pic_flag: Not support
     * top/bottom. */
    .delta_pic_order_cnt_bottom = 0,

    .delta_pic_order_cnt = { 0, 0 },
    .redundant_pic_cnt = 0,

    /* if slice_type == B_SLICE */
    .direct_spatial_mv_pred_flag =
        frame->type.slice_type == GST_H264_B_SLICE ? 1 : 0,

    .num_ref_idx_l0_active_minus1 = 0,     /* defined later */
    .num_ref_idx_l1_active_minus1 = 0,     /* defined later */
    .num_ref_idx_active_override_flag = 0, /* defined later */

    /* Calculate it later. */
    .ref_pic_list_modification_flag_l0 = 0,
    .n_ref_pic_list_modification_l0 = 0,
    .ref_pic_list_modification_l0 = { { 0, }, },
    .ref_pic_list_modification_flag_l1 = 0,
    .n_ref_pic_list_modification_l1 = 0,
    .ref_pic_list_modification_l1 = { { 0, }, },

    /* We have weighted_pred_flag and weighted_bipred_idc 0 here, no
     * need weight_table. */
    .pred_weight_table = { 0, },
    /* if nal_unit.ref_idc != 0 */
    .dec_ref_pic_marking = { 0, },

    .cabac_init_idc = 0,
    .slice_qp_delta = 0, /* XXX: update it if rate control  */

    .disable_deblocking_filter_idc = 0,
    .slice_alpha_c0_offset_div2 = 2,
    .slice_beta_offset_div2 = 2,

    .slice_group_change_cycle = 0,

    /* Size of the slice_header() in bits */
    .header_size = 0,

    /* Number of emulation prevention bytes (EPB) in this slice_header() */
    .n_emulation_prevention_bytes = 0,
    .sp_for_switch_flag = 0,

    .pic_order_cnt_bit_size = 0,
  };
  /* *INDENT-ON* */

  if (frame->type.slice_type == GST_H264_B_SLICE
      || frame->type.slice_type == GST_H264_P_SLICE) {
    slice_hdr->num_ref_idx_active_override_flag =
        priv->ref_list0->len > 0 || priv->ref_list1->len > 0;
    slice_hdr->num_ref_idx_l0_active_minus1 =
        priv->ref_list0->len > 0 ? priv->ref_list0->len - 1 : 0;
    if (frame->type.slice_type == GST_H264_B_SLICE) {
      slice_hdr->num_ref_idx_l1_active_minus1 =
          priv->ref_list1->len > 0 ? priv->ref_list1->len - 1 : 0;
    }
  }

  /* Reorder the ref lists if needed. */
  if (_ref_list_need_reorder (priv->ref_list0, FALSE))
    _update_ref_pic_list_modification (slice_hdr, priv->ref_list0, FALSE);

  /* Mark the unused reference explicitly which this frame replaces. */
  if (frame->unused_for_reference_pic_num >= 0)
    _update_ref_pic_marking_for_unused_frame (slice_hdr, frame);
}

static gint
_sort_by_frame_num (gconstpointer a, gconstpointer b, gpointer user_data)
{
  GstH264EncoderFrame *frame1 = _GET_FRAME ((GstVideoCodecFrame *) a);
  GstH264EncoderFrame *frame2 = _GET_FRAME ((GstVideoCodecFrame *) b);

  g_assert (frame1->gop_frame_num != frame2->gop_frame_num);

  return frame1->gop_frame_num - frame2->gop_frame_num;
}

static GstVideoCodecFrame *
gst_h264_encoder_find_unused_reference_frame (GstH264Encoder * self,
    GstH264EncoderFrame * h264_frame)
{
  GstH264EncoderPrivate *priv = _GET_PRIV (self);
  GstH264EncoderFrame *b_h264_frame;
  GstVideoCodecFrame *b_frame;
  guint i;

  /* We still have more space. */
  if (g_queue_get_length (&priv->ref_list) <
      priv->gop.max_dec_frame_buffering - 1)
    return NULL;

  /* Not b_pyramid, sliding window is enough. */
  if (!priv->gop.b_pyramid)
    return g_queue_peek_head (&priv->ref_list);

  /* I/P frame, just using sliding window. */
  if (h264_frame->type.slice_type != GST_H264_B_SLICE)
    return g_queue_peek_head (&priv->ref_list);

  /* Choose the B frame with lowest POC. */
  b_frame = NULL;
  b_h264_frame = NULL;
  for (i = 0; i < g_queue_get_length (&priv->ref_list); i++) {
    GstH264EncoderFrame *h264frame;
    GstVideoCodecFrame *frame;

    frame = g_queue_peek_nth (&priv->ref_list, i);
    h264frame = _GET_FRAME (frame);
    if (h264frame->type.slice_type != GST_H264_B_SLICE)
      continue;

    if (!b_frame) {
      b_frame = frame;
      b_h264_frame = _GET_FRAME (b_frame);
      continue;
    }

    b_h264_frame = _GET_FRAME (b_frame);
    g_assert (h264frame->poc != b_h264_frame->poc);
    if (h264frame->poc < b_h264_frame->poc) {
      b_frame = frame;
      b_h264_frame = _GET_FRAME (b_frame);
    }
  }

  /* No B frame as ref. */
  if (!b_frame)
    return g_queue_peek_head (&priv->ref_list);

  if (b_frame != g_queue_peek_head (&priv->ref_list)) {
    b_h264_frame = _GET_FRAME (b_frame);
    h264_frame->unused_for_reference_pic_num = b_h264_frame->gop_frame_num;
    GST_LOG_OBJECT (self, "The frame with POC: %d, pic_num %d will be"
        " replaced by the frame with POC: %d, pic_num %d explicitly by"
        " using memory_management_control_operation=1",
        b_h264_frame->poc, b_h264_frame->gop_frame_num,
        h264_frame->poc, h264_frame->gop_frame_num);
  }

  return b_frame;
}

static gint
_poc_asc_compare (const GstH264EncoderFrame ** a,
    const GstH264EncoderFrame ** b)
{
  return (*a)->poc - (*b)->poc;
}

static gint
_poc_desc_compare (const GstH264EncoderFrame ** a,
    const GstH264EncoderFrame ** b)
{
  return (*b)->poc - (*a)->poc;
}

static GstFlowReturn
gst_h264_encoder_encode_frame_with_ref_lists (GstH264Encoder * self,
    GstVideoCodecFrame * frame)
{
  GstH264EncoderClass *klass = GST_H264_ENCODER_GET_CLASS (self);
  GstH264EncoderPrivate *priv = _GET_PRIV (self);
  GstH264EncoderFrame *h264_frame;
  GArray *list0, *list1;
  GstH264SliceHdr slice_hdr;
  gint i;

  g_return_val_if_fail (frame, FALSE);

  h264_frame = _GET_FRAME (frame);

  list0 = priv->ref_list0;
  list1 = priv->ref_list1;

  g_array_set_size (list0, 0);
  g_array_set_size (list1, 0);

  /* Non I frame, construct reference list. */
  if (h264_frame->type.slice_type != GST_H264_I_SLICE) {
    g_assert (g_queue_get_length (&priv->ref_list) <
        priv->gop.max_dec_frame_buffering);

    GST_INFO_OBJECT (self, "Default RefPicList0 for fn=%u/poc=%d:",
        h264_frame->gop_frame_num, h264_frame->poc);
    for (i = g_queue_get_length (&priv->ref_list) - 1; i >= 0; i--) {
      GstVideoCodecFrame *ref_frame;
      GstH264EncoderFrame *ref_h264_frame;

      ref_frame = g_queue_peek_nth (&priv->ref_list, i);
      ref_h264_frame = _GET_FRAME (ref_frame);
      if (ref_h264_frame->poc > h264_frame->poc)
        continue;

      GST_INFO_OBJECT (self, "  fn=%u/poc=%d:", ref_h264_frame->gop_frame_num,
          ref_h264_frame->poc);
      g_array_append_val (list0, ref_h264_frame);
    }

    /* reorder to select the nearest forward frames. */
    g_array_sort (list0, (GCompareFunc) _poc_desc_compare);

    if (list0->len > priv->gop.ref_num_list0)
      g_array_set_size (list0, priv->gop.ref_num_list0);
  }

  if (h264_frame->type.slice_type == GST_H264_B_SLICE) {
    GST_INFO_OBJECT (self, "Default RefPicList1 for fn=%u/poc=%d:",
        h264_frame->gop_frame_num, h264_frame->poc);
    for (i = 0; i < g_queue_get_length (&priv->ref_list); i++) {
      GstH264EncoderFrame *ref_h264_frame;
      GstVideoCodecFrame *ref_frame;

      ref_frame = g_queue_peek_nth (&priv->ref_list, i);
      ref_h264_frame = _GET_FRAME (ref_frame);
      if (ref_h264_frame->poc < h264_frame->poc)
        continue;

      GST_INFO_OBJECT (self, "  fn=%d/poc=%d",
          ref_h264_frame->gop_frame_num, ref_h264_frame->poc);
      g_array_append_val (list1, ref_h264_frame);
    }

    /* reorder to select the nearest backward frames. */
    g_array_sort (list1, (GCompareFunc) _poc_asc_compare);

    if (list1->len > priv->gop.ref_num_list1)
      g_array_set_size (list1, priv->gop.ref_num_list1);
  }

  g_assert (list0->len + list1->len <= priv->gop.num_ref_frames);

  gst_h264_encoder_slicehdr_init (self, h264_frame, &slice_hdr);

  g_assert (klass->encode_frame);
  return klass->encode_frame (self, frame, h264_frame, &slice_hdr, list0,
      list1);
}

static GstFlowReturn
gst_h264_encoder_encode_frame (GstH264Encoder * self,
    GstVideoCodecFrame * frame, gboolean is_last)
{
  GstH264EncoderPrivate *priv = _GET_PRIV (self);
  GstH264EncoderFrame *h264_frame;
  GstVideoCodecFrame *unused_ref = NULL;
  GstFlowReturn ret;

  h264_frame = _GET_FRAME (frame);
  h264_frame->last_frame = is_last;

  if (h264_frame->type.is_ref) {
    unused_ref =
        gst_h264_encoder_find_unused_reference_frame (self, h264_frame);
  }

  ret = gst_h264_encoder_encode_frame_with_ref_lists (self, frame);
  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (self, "Failed to encode the frame: %s",
        gst_flow_get_name (ret));
    return ret;
  }

  g_queue_push_tail (&priv->output_list, gst_video_codec_frame_ref (frame));

  if (h264_frame->type.is_ref) {
    if (unused_ref) {
      if (!g_queue_remove (&priv->ref_list, unused_ref))
        g_assert_not_reached ();

      gst_video_codec_frame_unref (unused_ref);
    }

    /* Add it into the reference list. */
    g_queue_push_tail (&priv->ref_list, gst_video_codec_frame_ref (frame));
    g_queue_sort (&priv->ref_list, _sort_by_frame_num, NULL);

    g_assert (g_queue_get_length (&priv->ref_list) <
        priv->gop.max_dec_frame_buffering);
  }

  return ret;
}

static GstFlowReturn
gst_h264_encoder_finish_last_frame (GstH264Encoder * self)
{
  GstH264EncoderPrivate *priv = _GET_PRIV (self);
  GstVideoCodecFrame *frame;
  GstFlowReturn ret;
  guint32 system_frame_number;

  if (g_queue_is_empty (&priv->output_list))
    return GST_FLOW_OUTPUT_NOT_READY;

  /* TODO: check if the output buffer is ready */

  frame = g_queue_pop_head (&priv->output_list);
  system_frame_number = frame->system_frame_number;

  gst_video_codec_frame_unref (frame);

  ret = gst_h264_encoder_finish_frame (self, frame);

  if (ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (self, "fails to push one buffer, system_frame_number "
        "%d: %s", system_frame_number, gst_flow_get_name (ret));
  }

  return ret;
}

static GstFlowReturn
gst_h264_encoder_drain (GstH264Encoder * self)
{
  GstVideoEncoder *encoder = GST_VIDEO_ENCODER (self);
  GstH264EncoderPrivate *priv = _GET_PRIV (self);
  GstFlowReturn ret = GST_FLOW_OK;
  GstVideoCodecFrame *frame = NULL;
  gboolean is_last;

  GST_DEBUG_OBJECT (self, "Encoder is draining");

  /* Kickout all cached frames */
  if (!gst_h264_encoder_reorder_frame (self, NULL, TRUE, &frame)) {
    ret = GST_FLOW_ERROR;
    goto error_and_purge_all;
  }

  while (frame) {
    is_last = g_queue_is_empty (&priv->reorder_list);
    ret = gst_h264_encoder_encode_frame (self, frame, is_last);
    if (ret != GST_FLOW_OK)
      goto error_and_purge_all;

    frame = NULL;

    ret = gst_h264_encoder_finish_last_frame (self);
    if (ret != GST_FLOW_OK)
      goto error_and_purge_all;

    if (!gst_h264_encoder_reorder_frame (self, NULL, TRUE, &frame)) {
      ret = GST_FLOW_ERROR;
      goto error_and_purge_all;
    }
  }

  g_assert (g_queue_is_empty (&priv->reorder_list));

  /* Output all frames. */
  while (!g_queue_is_empty (&priv->output_list)) {
    ret = gst_h264_encoder_finish_last_frame (self);
    if (ret != GST_FLOW_OK)
      goto error_and_purge_all;
  }

  /* Also clear the reference list. */
  g_queue_clear_full (&priv->ref_list,
      (GDestroyNotify) gst_video_codec_frame_unref);

  return GST_FLOW_OK;

error_and_purge_all:
  if (frame) {
    gst_clear_buffer (&frame->output_buffer);
    gst_video_encoder_finish_frame (encoder, frame);
  }

  if (!g_queue_is_empty (&priv->output_list)) {
    GST_WARNING_OBJECT (self, "Still %d frame in the output list"
        " after drain", g_queue_get_length (&priv->output_list));
    while (!g_queue_is_empty (&priv->output_list)) {
      frame = g_queue_pop_head (&priv->output_list);
      gst_video_codec_frame_unref (frame);
      gst_clear_buffer (&frame->output_buffer);
      gst_video_encoder_finish_frame (encoder, frame);
    }
  }

  if (!g_queue_is_empty (&priv->reorder_list)) {
    GST_WARNING_OBJECT (self, "Still %d frame in the reorder list"
        " after drain", g_queue_get_length (&priv->reorder_list));
    while (!g_queue_is_empty (&priv->reorder_list)) {
      frame = g_queue_pop_head (&priv->reorder_list);
      gst_video_codec_frame_unref (frame);
      gst_clear_buffer (&frame->output_buffer);
      gst_video_encoder_finish_frame (encoder, frame);
    }
  }

  /* Also clear the reference list. */
  g_queue_clear_full (&priv->ref_list,
      (GDestroyNotify) gst_video_codec_frame_unref);

  gst_vec_deque_clear (priv->dts_queue);

  return ret;
}

enum
{
  GST_CHROMA_420 = 1,
  GST_CHROMA_422 = 2,
  GST_CHROMA_444 = 3,
  GST_CHROMA_INVALID = 0xFF,
};

static guint8
_h264_get_chroma_idc (GstVideoInfo * info)
{
  gint w_sub, h_sub;

  if (!GST_VIDEO_FORMAT_INFO_IS_YUV (info->finfo))
    return GST_CHROMA_INVALID;

  w_sub = 1 << GST_VIDEO_FORMAT_INFO_W_SUB (info->finfo, 1);
  h_sub = 1 << GST_VIDEO_FORMAT_INFO_H_SUB (info->finfo, 1);

  if (w_sub == 2 && h_sub == 2)
    return GST_CHROMA_420;
  else if (w_sub == 2 && h_sub == 1)
    return GST_CHROMA_422;
  else if (w_sub == 1 && h_sub == 1)
    return GST_CHROMA_444;
  return GST_CHROMA_INVALID;
}

static const struct
{
  const char *name;
  GstH264Level level;
} _h264_level_map[] = {
  {"1", GST_H264_LEVEL_L1},
  {"1b", GST_H264_LEVEL_L1B},
  {"1.1", GST_H264_LEVEL_L1_1},
  {"1.2", GST_H264_LEVEL_L1_2},
  {"1.3", GST_H264_LEVEL_L1_3},
  {"2", GST_H264_LEVEL_L2},
  {"2.1", GST_H264_LEVEL_L2_1},
  {"2.2", GST_H264_LEVEL_L2_2},
  {"3", GST_H264_LEVEL_L3},
  {"3.1", GST_H264_LEVEL_L3_1},
  {"3.2", GST_H264_LEVEL_L3_2},
  {"4", GST_H264_LEVEL_L4},
  {"4.1", GST_H264_LEVEL_L4_1},
  {"4.2", GST_H264_LEVEL_L4_2},
  {"5", GST_H264_LEVEL_L5},
  {"5.1", GST_H264_LEVEL_L5_1},
  {"5.2", GST_H264_LEVEL_L5_2},
  {"6", GST_H264_LEVEL_L6},
  {"6.1", GST_H264_LEVEL_L6_1},
  {"6.2", GST_H264_LEVEL_L6_2},
};

static guint8
_h264_get_level_idc (const gchar * level)
{
  if (!level)
    return 0;

  for (int i = 0; i < G_N_ELEMENTS (_h264_level_map); i++) {
    if (strcmp (level, _h264_level_map[i].name) == 0)
      return _h264_level_map[i].level;
  }

  return 0;
}

static GstH264Profile
gst_h264_encoder_profile_from_string (const char *profile)
{
  if (g_strcmp0 (profile, "constrained-baseline") == 0)
    return GST_H264_PROFILE_BASELINE;
  return gst_h264_profile_from_string (profile);
}

struct ProfileCandidate
{
  const char *profile_name;
  GstH264Profile profile;
  guint level;
};

static GstFlowReturn
gst_h264_encoder_negotiate_default (GstH264Encoder * self,
    GstVideoCodecState * in_state, GstH264Profile * profile,
    GstH264Level * level)
{
  GstCaps *allowed_caps;
  guint i, num_structures, num_candidates = 0;
  guint8 chroma, bit_depth_luma;
  struct ProfileCandidate candidates[16] =
      { {NULL, GST_H264_PROFILE_INVALID, 0}, };

  allowed_caps = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (self));
  if (!allowed_caps)
    return GST_FLOW_NOT_LINKED;
  if (gst_caps_is_empty (allowed_caps)) {
    gst_caps_unref (allowed_caps);
    return GST_FLOW_NOT_NEGOTIATED;
  }

  num_structures = gst_caps_get_size (allowed_caps);
  for (i = 0; i < num_structures; i++) {
    GstStructure *structure = gst_caps_get_structure (allowed_caps, i);
    const GValue *profiles = gst_structure_get_value (structure, "profile"),
        *level = gst_structure_get_value (structure, "level");
    struct ProfileCandidate *candidate;

    if (!profile)
      continue;

    candidate = &candidates[num_candidates];

    if (G_VALUE_HOLDS_STRING (profiles)) {
      candidate->profile_name = g_value_get_string (profiles);
      candidate->profile =
          gst_h264_encoder_profile_from_string (candidate->profile_name);
      candidate->level = level ?
          _h264_get_level_idc (g_value_get_string (level)) : 0;
      num_candidates++;
    } else if (GST_VALUE_HOLDS_LIST (profiles)) {
      for (guint j = 0; j < gst_value_list_get_size (profiles); j++) {
        const GValue *profile = gst_value_list_get_value (profiles, j);

        candidate->profile_name = g_value_get_string (profile);
        candidate->profile =
            gst_h264_encoder_profile_from_string (candidate->profile_name);
        candidate->level = level ?
            _h264_get_level_idc (g_value_get_string (level)) : 0;
        num_candidates++;
      }
    }

    if (num_candidates == G_N_ELEMENTS (candidates))
      break;
  }

  gst_caps_unref (allowed_caps);

  if (num_candidates == 0) {
    GST_ERROR_OBJECT (self, "Source caps with no profile");
    return GST_FLOW_NOT_NEGOTIATED;
  }

  chroma = _h264_get_chroma_idc (&in_state->info);
  if (chroma == GST_CHROMA_INVALID)
    return GST_FLOW_NOT_NEGOTIATED;
  bit_depth_luma = GST_VIDEO_INFO_COMP_DEPTH (&in_state->info, 0);

  /* let's just pick the best one according to the input */
  for (i = 0; i < num_candidates; i++) {
    struct ProfileCandidate *candidate = &candidates[i];

    if (candidate->profile < *profile)
      continue;
    if (candidate->profile < GST_H264_PROFILE_HIGH_444
        && chroma == GST_CHROMA_444) {
      GST_INFO_OBJECT (self, "Profile %s doesn't supports 4:4:4",
          candidate->profile_name);
      continue;
    }
    if (candidate->profile < GST_H264_PROFILE_HIGH_422
        && chroma >= GST_CHROMA_422) {
      GST_INFO_OBJECT (self, "Profile %s doesn't supports 4:2:2",
          candidate->profile_name);
      continue;
    }
    if (candidate->profile < GST_H264_PROFILE_HIGH10 && bit_depth_luma > 8) {
      GST_INFO_OBJECT (self, "Profile %s doesn't support a bit depth of %d",
          candidate->profile_name, bit_depth_luma);
      continue;
    }

    *profile = candidates[i].profile;
    *level = candidates[i].level;
  }

  if (*profile == GST_H264_PROFILE_INVALID) {
    GST_ERROR_OBJECT (self, "No valid profile found");
    return GST_FLOW_NOT_NEGOTIATED;
  }

  return GST_FLOW_OK;
}

#ifndef GST_DISABLE_GST_DEBUG
#define SPS_MEMBERS(F) \
  F(id)                                                                 \
  F(profile_idc)                                                        \
  F(constraint_set0_flag)                                               \
  F(constraint_set1_flag)                                               \
  F(constraint_set2_flag)                                               \
  F(constraint_set3_flag)                                               \
  F(constraint_set4_flag)                                               \
  F(constraint_set5_flag)                                               \
  F(level_idc)                                                          \
  F(chroma_format_idc)                                                  \
  F(separate_colour_plane_flag)                                         \
  F(bit_depth_luma_minus8)                                              \
  F(bit_depth_chroma_minus8)                                            \
  F(qpprime_y_zero_transform_bypass_flag)                               \
  F(scaling_matrix_present_flag)                                        \
  F(log2_max_frame_num_minus4)                                          \
  F(pic_order_cnt_type)                                                 \
  F(log2_max_pic_order_cnt_lsb_minus4)                                  \
  F(delta_pic_order_always_zero_flag)                                   \
  F(offset_for_non_ref_pic)                                             \
  F(offset_for_top_to_bottom_field)                                     \
  F(num_ref_frames_in_pic_order_cnt_cycle)                              \
  F(num_ref_frames)                                                     \
  F(gaps_in_frame_num_value_allowed_flag)                               \
  F(pic_width_in_mbs_minus1)                                            \
  F(pic_height_in_map_units_minus1)                                     \
  F(frame_mbs_only_flag)                                                \
  F(mb_adaptive_frame_field_flag)                                       \
  F(direct_8x8_inference_flag)                                          \
  F(frame_cropping_flag)                                                \
  F(frame_crop_left_offset)                                             \
  F(frame_crop_right_offset)                                            \
  F(frame_crop_top_offset)                                              \
  F(frame_crop_bottom_offset)                                           \
  F(vui_parameters_present_flag)                                        \
  F(vui_parameters.aspect_ratio_info_present_flag)                      \
  F(vui_parameters.aspect_ratio_idc)                                    \
  F(vui_parameters.sar_width)                                           \
  F(vui_parameters.sar_height)                                          \
  F(vui_parameters.overscan_info_present_flag)                          \
  F(vui_parameters.overscan_appropriate_flag)                           \
  F(vui_parameters.chroma_loc_info_present_flag)                        \
  F(vui_parameters.timing_info_present_flag)                            \
  F(vui_parameters.num_units_in_tick)                                   \
  F(vui_parameters.time_scale)                                          \
  F(vui_parameters.fixed_frame_rate_flag)                               \
  F(vui_parameters.nal_hrd_parameters_present_flag)                     \
  F(vui_parameters.vcl_hrd_parameters_present_flag)                     \
  F(vui_parameters.low_delay_hrd_flag)                                  \
  F(vui_parameters.pic_struct_present_flag)                             \
  F(vui_parameters.bitstream_restriction_flag)                          \
  F(vui_parameters.motion_vectors_over_pic_boundaries_flag)             \
  F(vui_parameters.max_bytes_per_pic_denom)                             \
  F(vui_parameters.max_bits_per_mb_denom)                               \
  F(vui_parameters.log2_max_mv_length_horizontal)                       \
  F(vui_parameters.log2_max_mv_length_vertical)                         \
  F(vui_parameters.num_reorder_frames)                                  \
  F(vui_parameters.max_dec_frame_buffering)
#endif

static void
gst_h264_sps_dump (GstH264Encoder * self, GstH264SPS * sps)
{
#ifndef GST_DISABLE_GST_DEBUG
#define SPS_STR(member) "  " G_STRINGIFY(member) " = %u\n"
#define SPS_VAL(member) sps->member,
  GST_INFO_OBJECT (self, "SPS\n" SPS_MEMBERS (SPS_STR) "%s",
      SPS_MEMBERS (SPS_VAL) "");
#undef SPS_STR
#undef SPS_VAL
#endif
}

#ifndef GST_DISABLE_GST_DEBUG
#define PPS_MEMBERS(F) \
  F(id)                                         \
  F(entropy_coding_mode_flag)                   \
  F(pic_order_present_flag)                     \
  F(num_slice_groups_minus1)                    \
  F(slice_group_map_type)                       \
  F(slice_group_change_direction_flag)          \
  F(slice_group_change_rate_minus1)             \
  F(pic_size_in_map_units_minus1)               \
  F(num_ref_idx_l0_active_minus1)               \
  F(num_ref_idx_l1_active_minus1)               \
  F(weighted_pred_flag)                         \
  F(weighted_bipred_idc)                        \
  F(pic_init_qp_minus26)                        \
  F(pic_init_qs_minus26)                        \
  F(chroma_qp_index_offset)                     \
  F(deblocking_filter_control_present_flag)     \
  F(constrained_intra_pred_flag)                \
  F(redundant_pic_cnt_present_flag)             \
  F(transform_8x8_mode_flag)                    \
  F(second_chroma_qp_index_offset)              \
  F(pic_scaling_matrix_present_flag)
#endif

static void
gst_h264_pps_dump (GstH264Encoder * self, GstH264PPS * pps)
{
#ifndef GST_DISABLE_GST_DEBUG
#define PPS_STR(member) "  " G_STRINGIFY(member) " = %u\n"
#define PPS_VAL(member) pps->member,
  GST_INFO_OBJECT (self, "PPS\n" PPS_MEMBERS (PPS_STR) "%s",
      PPS_MEMBERS (PPS_VAL) "");
#undef PPS_STR
#undef PPS_VAL
#endif
}

/* 7.4.2.1.1 Sequence parameter set data semantics */
static void
gst_h264_encoder_sps_init (GstH264Encoder * self)
{
  GstH264EncoderPrivate *priv = _GET_PRIV (self);
  GstVideoInfo *info;
  gint i, mb_width, mb_height;
  guint8 chroma_format_idc, bit_depth_luma, bit_depth_chroma,
      frame_cropping_flag, frame_crop_right_offset, frame_crop_bottom_offset,
      aspect_ratio_present_flag, aspect_ratio_idc, sar_width, sar_height,
      timing_info_present_flag, num_units_in_tick, time_scale,
      fixed_frame_rate_flag, constraint_set3_flag, constraint_set4_flag,
      constraint_set5_flag, level_idc, direct_8x8_inference_flag;

  info = &priv->input_state->info;

  GST_DEBUG_OBJECT (self, "filling SPS");

  chroma_format_idc = _h264_get_chroma_idc (info);
  mb_width = GST_ROUND_UP_16 (GST_VIDEO_INFO_WIDTH (info)) / 16;
  mb_height = GST_ROUND_UP_16 (GST_VIDEO_INFO_HEIGHT (info)) / 16;
  bit_depth_luma = GST_VIDEO_INFO_COMP_DEPTH (info, 0);
  bit_depth_chroma = GST_VIDEO_INFO_COMP_DEPTH (info, 1);

  if (GST_VIDEO_INFO_WIDTH (info) !=
      GST_ROUND_UP_16 (GST_VIDEO_INFO_WIDTH (info))
      || GST_VIDEO_INFO_HEIGHT (info) !=
      GST_ROUND_UP_16 (GST_VIDEO_INFO_HEIGHT (info))) {
    /* Table 6-1 */
    const guint SubWidthC[] = { 1, 2, 2, 1 };
    const guint SubHeightC[] = { 1, 2, 1, 1 };

    frame_cropping_flag = 1;
    frame_crop_right_offset = (16 * mb_width - GST_VIDEO_INFO_WIDTH (info))
        / SubWidthC[chroma_format_idc];
    frame_crop_bottom_offset = (16 * mb_height - GST_VIDEO_INFO_HEIGHT (info))
        / SubHeightC[chroma_format_idc];
  } else {
    frame_cropping_flag = frame_crop_right_offset = frame_crop_bottom_offset =
        0;
  }

  aspect_ratio_present_flag = aspect_ratio_idc = sar_width = sar_height = 0;

  if (GST_VIDEO_INFO_PAR_N (info) != 0 && GST_VIDEO_INFO_PAR_D (info) != 0) {
    aspect_ratio_present_flag = 1;
    for (i = 0; i < G_N_ELEMENTS (_h264_aspect_ratio); i++) {
      if (gst_util_fraction_compare (GST_VIDEO_INFO_PAR_N (info),
              GST_VIDEO_INFO_PAR_D (info), _h264_aspect_ratio[i].num,
              _h264_aspect_ratio[i].den) == 0) {
        aspect_ratio_idc = i;
        sar_width = sar_height = 0;
        break;
      }
    }

    /* Extended SAR */
    if (i >= G_N_ELEMENTS (_h264_aspect_ratio)) {
      aspect_ratio_idc = 0xff;
      sar_width = GST_VIDEO_INFO_PAR_N (info);
      sar_height = GST_VIDEO_INFO_PAR_D (info);
    }
  }

  if (GST_VIDEO_INFO_FPS_N (info) > 0 && GST_VIDEO_INFO_FPS_D (info) > 0) {
    timing_info_present_flag = 1;
    num_units_in_tick = GST_VIDEO_INFO_FPS_D (info);
    time_scale = 2 * GST_VIDEO_INFO_FPS_N (info);
    fixed_frame_rate_flag = 1;
  } else {
    timing_info_present_flag = num_units_in_tick = time_scale =
        fixed_frame_rate_flag = 0;
  }

  constraint_set3_flag = 0;
  if (priv->stream.level == GST_H264_LEVEL_L1B
      && (priv->stream.profile == GST_H264_PROFILE_BASELINE
          || priv->stream.profile == GST_H264_PROFILE_MAIN)) {
    constraint_set3_flag = 1;   /* level 1b with Baseline or Main profile is
                                 * signaled via constraint_set3 */
  }

  /* support intra profiles */
  if (priv->gop.idr_period == 1
      && priv->stream.profile >= GST_H264_PROFILE_HIGH)
    constraint_set3_flag = 1;

  constraint_set4_flag = 0;
  /* If profile_idc is equal to 77, 88, 100, or 110, constraint_set4_flag equal
   * to 1 indicates that the value of frame_mbs_only_flag is equal to 1 */
  /* and frame_mbs_only_flag is 1 since we don't support interlaced streams */
  if (priv->stream.profile == GST_H264_PROFILE_MAIN
      || priv->stream.profile == GST_H264_PROFILE_EXTENDED
      || priv->stream.profile == GST_H264_PROFILE_HIGH
      || priv->stream.profile == GST_H264_PROFILE_HIGH10)
    constraint_set4_flag = 1;

  constraint_set5_flag = 0;
  /* If profile_idc is equal to 77, 88, or 100, constraint_set5_flag equal to 1
   * indicates that B slice types are not present */
  if (priv->gop.num_bframes == 0
      && (priv->stream.profile == GST_H264_PROFILE_MAIN
          || priv->stream.profile == GST_H264_PROFILE_EXTENDED
          || priv->stream.profile == GST_H264_PROFILE_HIGH))
    constraint_set5_flag = 1;

  if (priv->stream.level >= GST_H264_LEVEL_L1B) {
    level_idc = priv->stream.level;
  } else {
    level_idc = 0;
  }

  g_assert (priv->gop.log2_max_poc_lsb >= 4);
  g_assert (priv->gop.log2_max_frame_num >= 4);

  /* A.2.3 Extended profile:
   *
   * Sequence parameter sets shall have direct_8x8_inference_flag equal to 1.
   *
   * A.3.3 Profile-specific level limits:
   *
   * direct_8x8_inference_flag is not relevant to the Baseline,
   * Constrained Baseline, Constrained High, High 10 Intra, High 4:2:2
   * Intra, High 4:4:4 Intra, and CAVLC 4:4:4 Intra profiles as these
   * profiles do not allow B slice types, and
   * direct_8x8_inference_flag is equal to 1 for all levels of the
   * Extended profile. Table A-4.  We only have constrained baseline
   * here. */
  direct_8x8_inference_flag =
      priv->stream.profile == GST_H264_PROFILE_BASELINE ? 0 : 1;

  priv->params.sps = (GstH264SPS) {
    /* *INDENT-OFF* */
    .id = 0,

    .profile_idc = priv->stream.profile,
    .constraint_set0_flag = priv->stream.profile == GST_H264_PROFILE_BASELINE,
    .constraint_set1_flag = priv->stream.profile <= GST_H264_PROFILE_MAIN,
    /* Extended profile not supported and not widely used */
    .constraint_set2_flag = 0,
    .constraint_set3_flag = constraint_set3_flag,
    .constraint_set4_flag = constraint_set4_flag,
    .constraint_set5_flag = constraint_set5_flag,
    /* override by implementation if 0 */
    .level_idc = level_idc,

    .chroma_format_idc = chroma_format_idc,
    .separate_colour_plane_flag = 0,
    .bit_depth_luma_minus8 = CLAMP (bit_depth_luma - 8, 0, 6),
    .bit_depth_chroma_minus8 = CLAMP (bit_depth_chroma - 8, 0, 6),
    .qpprime_y_zero_transform_bypass_flag = 0,

    .scaling_matrix_present_flag = 0,
    .scaling_lists_4x4 = { { 0, }, },
    .scaling_lists_8x8 = { { 0, }, },

    .log2_max_frame_num_minus4 =
        CLAMP ((gint) (priv->gop.log2_max_frame_num - 4), 0, 12),
    .pic_order_cnt_type = 0,

    /* if pic_order_cnt_type == 0 */
    .log2_max_pic_order_cnt_lsb_minus4 =
        CLAMP ((gint) (priv->gop.log2_max_poc_lsb - 4), 0, 12),
    /* else if pic_order_cnt_type == 1 */
    .delta_pic_order_always_zero_flag = 0,
    .offset_for_non_ref_pic = 0,
    .offset_for_top_to_bottom_field = 0,
    .num_ref_frames_in_pic_order_cnt_cycle = 0,
    .offset_for_ref_frame = { 0, },

    .num_ref_frames = priv->gop.max_num_ref_frames,
    .gaps_in_frame_num_value_allowed_flag = 0,
    .pic_width_in_mbs_minus1 = mb_width - 1,
    .pic_height_in_map_units_minus1 = mb_height - 1,
    .frame_mbs_only_flag = 1,

    .mb_adaptive_frame_field_flag = 0,

    /* override if implementation doesn't support it for profile */
    .direct_8x8_inference_flag = direct_8x8_inference_flag,

    .frame_cropping_flag = frame_cropping_flag,
    /* if frame_cropping_flag = 1 */
    .frame_crop_left_offset = 0,
    .frame_crop_right_offset = frame_crop_right_offset,
    .frame_crop_top_offset = 0,
    .frame_crop_bottom_offset = frame_crop_bottom_offset,

    .vui_parameters_present_flag = 1,
    .vui_parameters = {
      .aspect_ratio_info_present_flag = aspect_ratio_present_flag,
      .aspect_ratio_idc = aspect_ratio_idc,
      /* if aspect_ratio_idc == 255 */
      .sar_width = sar_width,
      .sar_height = sar_height,

      .overscan_info_present_flag = 0,
      /* if overscan_info_present_flag */
      .overscan_appropriate_flag = 0,

      .chroma_loc_info_present_flag = 0, /* chroma location isn't defined in GStreamer */
      .timing_info_present_flag = timing_info_present_flag,
      .num_units_in_tick = num_units_in_tick,
      .time_scale = time_scale,
      .fixed_frame_rate_flag = fixed_frame_rate_flag,

      /* We do not write hrd and no need for buffering period SEI. */
      /* TODO: support timing units  */
      .nal_hrd_parameters_present_flag = 0,
      .vcl_hrd_parameters_present_flag = 0,

      .low_delay_hrd_flag = 0,
      .pic_struct_present_flag = 1, /* Table E-6 */
      .bitstream_restriction_flag = 1,
      .motion_vectors_over_pic_boundaries_flag = 1,
      .max_bytes_per_pic_denom = 0, /* not present  */
      .max_bits_per_mb_denom = 0, /* not present */
      .log2_max_mv_length_horizontal = 15,
      .log2_max_mv_length_vertical = 15,
      .num_reorder_frames = priv->gop.num_reorder_frames,
      .max_dec_frame_buffering = priv->gop.max_dec_frame_buffering,
    },

    /* ... */
    /* *INDENT-ON* */
  };
}

static void
gst_h264_encoder_pps_init (GstH264Encoder * self)
{
  GstH264EncoderPrivate *priv = _GET_PRIV (self);
  GstH264SPS *sps = &priv->params.sps;

  /* *INDENT-OFF* */
  priv->params.pps = (GstH264PPS) {
    .id = 0,

    .sequence = sps,

    /* override by implementation if CABAC isn't supported or disabled */
    .entropy_coding_mode_flag = !(sps->profile_idc == GST_H264_PROFILE_BASELINE
        || sps->profile_idc == GST_H264_PROFILE_EXTENDED),

    .pic_order_present_flag = 0,

    .num_slice_groups_minus1 = 0,
    /* if num_slice_groups_minus1 > 0*/
    .slice_group_map_type = 0,
    /*  if slice_group_map_type == 0 */
    .run_length_minus1 = { 0, },
    /*  if slice_group_map_type == 2 */
    .top_left = { 0, },
    .bottom_right = { 0, },
    /*  if slice_group_map_type == [3, 4, 5] */
    .slice_group_change_direction_flag = 0,
    .slice_group_change_rate_minus1 = 0,
    /*  if slice_group_map_type == 6 */
    .pic_size_in_map_units_minus1 = 0,
    .slice_group_id = NULL,

    /* Use slice's these fields to control ref num. */
    .num_ref_idx_l0_active_minus1 = 0,
    .num_ref_idx_l1_active_minus1 = 0,

    .weighted_pred_flag = 0,
    .weighted_bipred_idc = 0,

    .pic_init_qp_minus26 = 0, /* XXX: defined by rate control QP I */
    .pic_init_qs_minus26 = 0,
    .chroma_qp_index_offset = 0,
    .second_chroma_qp_index_offset = 0,

    /* enable deblocking */
    .deblocking_filter_control_present_flag = 1,
    .constrained_intra_pred_flag = 0,
    .redundant_pic_cnt_present_flag = 0,

     /* override by implementation if supported or enabled */
    .transform_8x8_mode_flag = !(sps->profile_idc == GST_H264_PROFILE_BASELINE
        || sps->profile_idc == GST_H264_PROFILE_EXTENDED
        || sps->profile_idc == GST_H264_PROFILE_MAIN),

    /* unsupport scaling lists */
    .pic_scaling_matrix_present_flag = 0,
    .scaling_lists_4x4 = { { 0, }, },
    .scaling_lists_8x8 = { { 0, }, },
  };
  /* *INDENT-ON* */
}

static GstFlowReturn
gst_h264_encoder_configure (GstH264Encoder * self)
{
  GstH264EncoderPrivate *priv = _GET_PRIV (self);
  GstH264EncoderClass *klass = GST_H264_ENCODER_GET_CLASS (self);
  GstFlowReturn ret;

  if (!priv->input_state)
    return GST_FLOW_NOT_NEGOTIATED;

  if (gst_h264_encoder_drain (self) != GST_FLOW_OK)
    return GST_FLOW_ERROR;

  GST_LOG_OBJECT (self, "Configuring encoder");

  gst_h264_encoder_reset (self);

  ret = klass->negotiate (self, priv->input_state, &priv->stream.profile,
      &priv->stream.level);
  if (ret != GST_FLOW_OK)
    return ret;

  if (klass->new_sequence) {
    ret = klass->new_sequence (self, priv->input_state, priv->stream.profile,
        &priv->stream.level);
    if (ret != GST_FLOW_OK)
      return ret;
  }

  /* now we have the L0/L1 list sizes */
  gst_h264_encoder_generate_gop_structure (self);

  if (priv->stream.level == 0) {
    const GstH264LevelDescriptor *desc;

    desc = gst_h264_get_level_descriptor (priv->stream.profile, 0,
        &priv->input_state->info, priv->gop.max_dec_frame_buffering);
    if (!desc)
      return GST_FLOW_ERROR;

    priv->stream.level = desc->level_idc;
  }

  /* after gop generation */
  gst_h264_encoder_sps_init (self);
  gst_h264_encoder_pps_init (self);

  /* this has to be the last operation since it calls
   * gst_video_encoder_set_output() */
  g_assert (klass->new_parameters);
  ret = klass->new_parameters (self, &priv->params.sps, &priv->params.pps);

  if (ret != GST_FLOW_OK)
    return ret;

  /* latency */
  {
    GstVideoEncoder *encoder = GST_VIDEO_ENCODER (self);
    guint frames_latency =
        priv->config.preferred_output_delay + priv->gop.ip_period - 1;
    GstClockTime latency = gst_util_uint64_scale (frames_latency,
        priv->fps_d * GST_SECOND, priv->fps_n);
    gst_video_encoder_set_latency (encoder, latency, latency);
  }

  /* dump parameter sets after been overrode by implementation */
  gst_h264_sps_dump (self, &priv->params.sps);
  gst_h264_pps_dump (self, &priv->params.pps);

  return ret;
}

static inline void
gst_h264_encoder_push_dts (GstH264Encoder * self, GstVideoCodecFrame * frame)
{
  GstH264EncoderPrivate *priv = _GET_PRIV (self);
  guint max_reorder_num = priv->gop.num_reorder_frames;

  /* We need to manually insert max_reorder_num slots before the first frame to
     ensure DTS never bigger than PTS. */
  if (gst_vec_deque_get_length (priv->dts_queue) == 0 && max_reorder_num > 0) {
    GstClockTime dts_diff = 0, dts;

    if (GST_CLOCK_TIME_IS_VALID (frame->duration))
      dts_diff = frame->duration;

    if (GST_CLOCK_TIME_IS_VALID (priv->frame_duration))
      dts_diff = MAX (priv->frame_duration, dts_diff);

    while (max_reorder_num > 0) {
      if (GST_CLOCK_TIME_IS_VALID (frame->pts)) {
        dts = frame->pts - dts_diff * max_reorder_num;
      } else {
        dts = frame->pts;
      }

      gst_vec_deque_push_tail_struct (priv->dts_queue, &dts);
      max_reorder_num--;
    }
  }

  gst_vec_deque_push_tail_struct (priv->dts_queue, &frame->pts);
}


static inline GstFlowReturn
gst_h264_encoder_try_to_finish_all_frames (GstH264Encoder * self)
{
  GstFlowReturn ret;

  do {
    ret = gst_h264_encoder_finish_last_frame (self);
  } while (ret == GST_FLOW_OK);

  if (ret == GST_FLOW_OUTPUT_NOT_READY)
    ret = GST_FLOW_OK;

  return ret;
}

static GstFlowReturn
gst_h264_encoder_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstH264Encoder *self = GST_H264_ENCODER (encoder);
  GstH264EncoderPrivate *priv = _GET_PRIV (self);
  GstH264EncoderClass *klass = GST_H264_ENCODER_GET_CLASS (self);
  GstFlowReturn ret = GST_FLOW_ERROR;
  GstH264EncoderFrame *h264_frame;
  GstVideoCodecFrame *frame_encode = NULL;

  GST_LOG_OBJECT (encoder, "handle frame id %d, dts %" GST_TIME_FORMAT
      ", pts %" GST_TIME_FORMAT, frame->system_frame_number,
      GST_TIME_ARGS (GST_BUFFER_DTS (frame->input_buffer)),
      GST_TIME_ARGS (GST_BUFFER_PTS (frame->input_buffer)));

  if (g_atomic_int_compare_and_exchange (&priv->need_configure, TRUE, FALSE)) {
    if (gst_h264_encoder_configure (self) != GST_FLOW_OK) {
      gst_video_encoder_finish_frame (encoder, frame);
      return GST_FLOW_ERROR;
    }
  }

  h264_frame = gst_h264_encoder_frame_new ();
  gst_video_codec_frame_set_user_data (frame, h264_frame,
      gst_h264_encoder_frame_unref);
  gst_h264_encoder_push_dts (self, frame);

  if (klass->new_output) {
    ret = klass->new_output (self, frame, h264_frame);
    if (ret != GST_FLOW_OK)
      goto error_new_frame;
  }

  if (!gst_h264_encoder_reorder_frame (self, frame, FALSE, &frame_encode))
    goto error_reorder;

  /* pass it to reorder list and we should not use it again. */
  frame = NULL;

  if (frame_encode) {
    while (frame_encode) {
      ret = gst_h264_encoder_encode_frame (self, frame_encode, FALSE);
      if (ret != GST_FLOW_OK)
        goto error_encode;

      while (ret == GST_FLOW_OK && g_queue_get_length (&priv->output_list) >
          priv->config.preferred_output_delay)
        ret = gst_h264_encoder_finish_last_frame (self);

      if (ret != GST_FLOW_OK)
        goto error_push_buffer;

      /* Try to push out all ready frames. */
      ret = gst_h264_encoder_try_to_finish_all_frames (self);
      if (ret != GST_FLOW_OK)
        goto error_push_buffer;

      frame_encode = NULL;
      if (!gst_h264_encoder_reorder_frame (self, NULL, FALSE, &frame_encode))
        goto error_reorder;
    }
  } else {
    /* Try to push out all ready frames. */
    ret = gst_h264_encoder_try_to_finish_all_frames (self);
    if (ret != GST_FLOW_OK)
      goto error_push_buffer;
  }

  return ret;

error_new_frame:
  {
    GST_ELEMENT_ERROR (encoder, STREAM, ENCODE,
        ("Failed to create the input frame."), (NULL));
    gst_clear_buffer (&frame->output_buffer);
    gst_video_encoder_finish_frame (encoder, frame);
    return GST_FLOW_ERROR;
  }
error_reorder:
  {
    GST_ELEMENT_ERROR (encoder, STREAM, ENCODE,
        ("Failed to reorder the input frame."), (NULL));
    if (frame) {
      gst_clear_buffer (&frame->output_buffer);
      gst_video_encoder_finish_frame (encoder, frame);
    }
    return GST_FLOW_ERROR;
  }
error_encode:
  {
    GST_ELEMENT_ERROR (encoder, STREAM, ENCODE,
        ("Failed to encode the frame %s.", gst_flow_get_name (ret)), (NULL));
    gst_clear_buffer (&frame_encode->output_buffer);
    gst_video_encoder_finish_frame (encoder, frame_encode);
    return ret;
  }
error_push_buffer:
  {
    GST_ELEMENT_ERROR (encoder, STREAM, ENCODE,
        ("Failed to finish frame."), (NULL));
    return ret;
  }
}

static gboolean
gst_h264_encoder_flush (GstVideoEncoder * encoder)
{
  GstH264Encoder *self = GST_H264_ENCODER (encoder);
  GstH264EncoderPrivate *priv = _GET_PRIV (self);

  gst_h264_encoder_flush_lists (self);
  gst_vec_deque_clear (priv->dts_queue);

  /* begin from an IDR after flush. */
  priv->gop.cur_frame_index = 0;
  priv->gop.cur_frame_num = 0;
  priv->gop.last_keyframe = NULL;
  /* XXX: enough? */

  return TRUE;
}

static GstFlowReturn
gst_h264_encoder_finish (GstVideoEncoder * encoder)
{
  return gst_h264_encoder_drain (GST_H264_ENCODER (encoder));
}

static void
gst_h264_encoder_init (GstH264Encoder * self)
{
  GstH264EncoderPrivate *priv = _GET_PRIV (self);

  g_queue_init (&priv->output_list);
  g_queue_init (&priv->ref_list);
  g_queue_init (&priv->reorder_list);

  priv->dts_queue = gst_vec_deque_new_for_struct (sizeof (GstClockTime), 8);

  priv->config.max_num_reference_list0 = 1;
  priv->config.max_num_reference_list1 = 0;
  priv->config.preferred_output_delay = 0;

  priv->ref_list0 = g_array_sized_new (FALSE, TRUE,
      sizeof (GstH264EncoderFrame *), 16);
  priv->ref_list1 = g_array_sized_new (FALSE, TRUE,
      sizeof (GstH264EncoderFrame *), 16);

  /* default values */
  priv->prop.idr_period = H264ENC_IDR_PERIOD_DEFAULT;
  priv->prop.num_bframes = H264ENC_B_FRAMES_DEFAULT;
  priv->prop.num_iframes = H264ENC_I_FRAMES_DEFAULT;
  priv->prop.num_ref_frames = H264ENC_NUM_REF_FRAMES_DEFAULT;
  priv->prop.b_pyramid = H264ENC_B_PYRAMID_DEFAULT;
}

static void
gst_h264_encoder_dispose (GObject * object)
{
  gst_h264_encoder_flush_lists (GST_H264_ENCODER (object));

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_h264_encoder_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstH264Encoder *self = GST_H264_ENCODER (object);
  GstH264EncoderPrivate *priv = _GET_PRIV (self);

  GST_OBJECT_LOCK (self);
  switch (property_id) {
    case PROP_IDR_PERIOD:
      g_value_set_uint (value, priv->prop.idr_period);
      break;
    case PROP_BFRAMES:
      g_value_set_uint (value, priv->prop.num_bframes);
      break;
    case PROP_IFRAMES:
      g_value_set_uint (value, priv->prop.num_iframes);
      break;
    case PROP_NUM_REF_FRAMES:
      g_value_set_int (value, priv->prop.num_ref_frames);
      break;
    case PROP_B_PYRAMID:
      g_value_set_boolean (value, priv->prop.b_pyramid);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (self);
}

static void
gst_h264_encoder_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstH264Encoder *self = GST_H264_ENCODER (object);
  GstH264EncoderPrivate *priv = _GET_PRIV (self);

  GST_OBJECT_LOCK (self);
  switch (property_id) {
    case PROP_IDR_PERIOD:
      priv->prop.idr_period = g_value_get_uint (value);
      g_atomic_int_set (&priv->need_configure, TRUE);
      break;
    case PROP_BFRAMES:
      priv->prop.num_bframes = g_value_get_uint (value);
      g_atomic_int_set (&priv->need_configure, TRUE);
      break;
    case PROP_IFRAMES:
      priv->prop.num_iframes = g_value_get_uint (value);
      g_atomic_int_set (&priv->need_configure, TRUE);
      break;
    case PROP_NUM_REF_FRAMES:
      priv->prop.num_ref_frames = g_value_get_int (value);
      g_atomic_int_set (&priv->need_configure, TRUE);
      break;
    case PROP_B_PYRAMID:
      priv->prop.b_pyramid = g_value_get_boolean (value);
      g_atomic_int_set (&priv->need_configure, TRUE);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (self);
}

static void
gst_h264_encoder_class_init (GstH264EncoderClass * klass)
{
  GstVideoEncoderClass *encoder_class = GST_VIDEO_ENCODER_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamFlags param_flags =
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT;

  object_class->get_property = gst_h264_encoder_get_property;
  object_class->set_property = gst_h264_encoder_set_property;
  object_class->dispose = gst_h264_encoder_dispose;

  encoder_class->start = GST_DEBUG_FUNCPTR (gst_h264_encoder_start);
  encoder_class->stop = GST_DEBUG_FUNCPTR (gst_h264_encoder_stop);
  encoder_class->set_format = GST_DEBUG_FUNCPTR (gst_h264_encoder_set_format);
  encoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_h264_encoder_handle_frame);
  encoder_class->flush = GST_DEBUG_FUNCPTR (gst_h264_encoder_flush);
  encoder_class->finish = GST_DEBUG_FUNCPTR (gst_h264_encoder_finish);

  klass->negotiate = GST_DEBUG_FUNCPTR (gst_h264_encoder_negotiate_default);

  /**
   * GstH264Encoder:idr-period:
   *
   * Maximum number of frames between two IDR frames. A higher value will result
   * in a lager IDR frame interval and thus slowdown seeking; a lower value will
   * result in a shorter IDR frame interval and thus improve seeking. As a rule
   * of thumb, the IDR period shouldn't be lower than the framerate of the video
   * multiplied by a factor between the range 1..10
   *
   * Set 0 for auto-calculate it.
   *
   * Since: 1.28
   */
  properties[PROP_IDR_PERIOD] = g_param_spec_uint ("idr-period",
      "Maximum GOP size", "Maximum number of frames between two IDR frames",
      0, MIN (G_MAXINT, 1 << 30), H264ENC_IDR_PERIOD_DEFAULT, param_flags);

  /**
   * GstH264Encoder:b-frames:
   *
   * Maximum number of consecutive B-Frames. B-Frames refer to both, the
   * previous and the following I-Frame (or P-Frame). This way B-Frames can
   * compress even more efficient that P-Frames.
   *
   * The availability of B-frames depends on the driver.
   *
   * Since: 1.28
   */
  properties[PROP_BFRAMES] = g_param_spec_uint ("b-frames", "B Frames",
      "Maximum number of consecutive B frames between I and P reference frames",
      0, 31, H264ENC_B_FRAMES_DEFAULT, param_flags);

  /**
   * GstH264Encoder:i-frames:
   *
   * Force the number of i-frames insertion within one GOP. More I-Frames will
   * increase the size of the video, but it will be more resilient to data
   * lose.
   *
   * Since: 1.28
   */
  properties[PROP_IFRAMES] = g_param_spec_uint ("i-frames", "I Frames",
      "Force the number of I frames insertion within one GOP, not including the "
      "first IDR frame", 0, G_MAXINT, H264ENC_I_FRAMES_DEFAULT, param_flags);

  /**
   * GstH264Encoder:num-ref-frames:
   *
   * The number of frames can be referenced by P-Frames and B-Frames. Higher
   * values will usually result in a more efficient compression, which means
   * better visual quality at the same file size, but it may require encoding
   * time.
   *
   * Since: 1.28
   */
  properties[PROP_NUM_REF_FRAMES] = g_param_spec_int ("num-ref-frames",
      "Number of reference frames", "Number of frames referenced by P and B "
      "frames", 0, 16, H264ENC_NUM_REF_FRAMES_DEFAULT, param_flags);

  /**
   * GstH264Encoder:b-pyramid:
   *
   * Enable the b-pyramid reference structure in GOP. It allows to make
   * references non-linearly in order to improve bitrate usage and quality. This
   * way B-Frames can refer to B-Frames.
   *
   * It only works with "high" profile.
   *
   * Since: 1.28
   */
  properties[PROP_B_PYRAMID] = g_param_spec_boolean ("b-pyramid", "b pyramid",
      "Enable the b-pyramid reference structure in the GOP",
      H264ENC_B_PYRAMID_DEFAULT, param_flags);

  g_object_class_install_properties (object_class, N_PROPERTIES, properties);

  gst_type_mark_as_plugin_api (GST_TYPE_H264_ENCODER, 0);
}

/**
 * gst_h264_self_set_max_num_references:
 * @self: A #GstH264Encoder
 * @list0: the maximum number of reference pictures for list L0
 * @list1: the maximum number of reference pictures for list L1
 *
 * Set the maximum number of reference pictures allowed by the accelerator.
 */
void
gst_h264_encoder_set_max_num_references (GstH264Encoder * self, guint list0,
    guint list1)
{
  GstH264EncoderPrivate *priv;

  g_return_if_fail (GST_IS_H264_ENCODER (self));

  priv = _GET_PRIV (self);

  priv->config.max_num_reference_list0 = list0;
  priv->config.max_num_reference_list1 = list1;
}

/**
 * gst_h264_encoder_is_live:
 * @self: a #GstH264Encoder
 *
 * Returns: whether the current stream is live
 */
gboolean
gst_h264_encoder_is_live (GstH264Encoder * self)
{
  GstH264EncoderPrivate *priv;

  g_return_val_if_fail (GST_IS_H264_ENCODER (self), FALSE);

  priv = _GET_PRIV (self);
  return priv->is_live;
}

/**
 * gst_h264_encoder_set_preferred_output_delay:
 * @self: a #GstH264Encoder
 * @delay: the number of frames to hold and process
 *
 * Some accelerators such as Intel VA-API has better performance if it holds a
 * group of frames to process.
 */
void
gst_h264_encoder_set_preferred_output_delay (GstH264Encoder * self, guint delay)
{
  GstH264EncoderPrivate *priv;

  g_return_if_fail (GST_IS_H264_ENCODER (self));

  priv = _GET_PRIV (self);
  priv->config.preferred_output_delay = delay;
}

/**
 * gst_h264_encoder_reconfigure:
 * @self: a #GstH264Encoder
 * @force: whether if configuration will run now or for next input frame
 *
 * Through this method the subclass can request the encoder reconfiguration
 * and downstream renegotiation.
 */
gboolean
gst_h264_encoder_reconfigure (GstH264Encoder * self, gboolean force)
{
  GstH264EncoderPrivate *priv;

  g_return_val_if_fail (GST_IS_H264_ENCODER (self), FALSE);

  priv = _GET_PRIV (self);

  if (!force) {
    g_atomic_int_set (&priv->need_configure, TRUE);
    return TRUE;
  } else {
    if (g_atomic_int_compare_and_exchange (&priv->need_configure, TRUE, FALSE)) {
      return (gst_h264_encoder_configure (self) == GST_FLOW_OK);
    }
    return TRUE;
  }
}

/**
 * gst_h264_encoder_get_idr_period:
 * @self: a #GstH264Encoder
 *
 * Returns the IDR period property without the marshalling burden of GObject
 * properties.
 *
 * Returns: the IDR period
 */
guint32
gst_h264_encoder_get_idr_period (GstH264Encoder * self)
{
  GstH264EncoderPrivate *priv;
  guint32 ret;

  g_return_val_if_fail (GST_IS_H264_ENCODER (self), -1);

  priv = _GET_PRIV (self);

  GST_OBJECT_LOCK (self);
  ret = priv->prop.idr_period;
  GST_OBJECT_UNLOCK (self);

  return ret;
}

/**
 * gst_h264_encoder_get_num_b_frames:
 * @self: a #GstH264Encoder
 *
 * Returns the number of consecutive B-Frames without the marshalling burden of
 * GObject properties.
 *
 * Returns: the number of consecutive B-Frames
 */
guint32
gst_h264_encoder_get_num_b_frames (GstH264Encoder * self)
{
  GstH264EncoderPrivate *priv;
  guint32 ret;

  g_return_val_if_fail (GST_IS_H264_ENCODER (self), -1);

  priv = _GET_PRIV (self);

  GST_OBJECT_LOCK (self);
  ret = priv->prop.num_bframes;
  GST_OBJECT_UNLOCK (self);

  return ret;
}

/**
 * gst_h264_encoder_gop_is_b_pyramid:
 * @self: a #GstH264Encoder
 *
 * Returns whether the GOP has a b-pyramid structure.
 *
 * Returns: %TRUE if GOP has a b-pyramid structure
 */
gboolean
gst_h264_encoder_gop_is_b_pyramid (GstH264Encoder * self)
{
  GstH264EncoderPrivate *priv;
  gboolean ret;

  g_return_val_if_fail (GST_IS_H264_ENCODER (self), FALSE);

  priv = _GET_PRIV (self);

  GST_OBJECT_LOCK (self);
  ret = priv->prop.b_pyramid;
  GST_OBJECT_UNLOCK (self);

  return ret;
}

/**
 * gst_h264_get_cpb_nal_factor:
 * @profile: a #GstH264Profile
 *
 * The values comes from Table A-2 + H.10.2.1
 *
 * Returns: the bitrate NAL factor of the coded picture buffer.
 *
 * Since: 1.28
 */
guint
gst_h264_get_cpb_nal_factor (GstH264Profile profile)
{
  for (int i = 0; i < G_N_ELEMENTS (_h264_nal_factors); i++) {
    if (_h264_nal_factors[i].profile == profile)
      return _h264_nal_factors[i].cpb_br_nal_factor;
  }

  /* default to non-high profile */
  return 1200;
}

/**
 * gst_h264_get_level_descriptor:
 * @profile: a #GstH264Profile
 * @bitrate: bit rate in bytes per second
 * @in_info: raw stream's #GstVideoInfo
 * @max_dec_frame_buffering: the max size of DPB
 *
 * Returns: #GStH264LevelDescriptor associated with associated with @profile,
 *   @bitrate, framesize and framerate in @in_info, and
 *   @max_dec_frame_buffering. If no descriptor found, it returns %NULL.
 *
 * Since: 1.28
 */
const GstH264LevelDescriptor *
gst_h264_get_level_descriptor (GstH264Profile profile, guint64 bitrate,
    GstVideoInfo * in_info, int max_dec_frame_buffering)
{
  guint mbWidth, mbHeight, cpb_factor;
  guint32 i, picSizeMbs, maxMBPS;

  g_return_val_if_fail (in_info, NULL);

  cpb_factor = gst_h264_get_cpb_nal_factor (profile);
  mbWidth = GST_ROUND_UP_16 (GST_VIDEO_INFO_WIDTH (in_info)) / 16;
  mbHeight = GST_ROUND_UP_16 (GST_VIDEO_INFO_HEIGHT (in_info)) / 16;

  picSizeMbs = mbWidth * mbHeight;
  if (GST_VIDEO_INFO_FPS_N (in_info) > 0 && GST_VIDEO_INFO_FPS_D (in_info) > 0) {
    maxMBPS = gst_util_uint64_scale_int_ceil (picSizeMbs,
        GST_VIDEO_INFO_FPS_N (in_info), GST_VIDEO_INFO_FPS_D (in_info));
  } else {
    maxMBPS = 16;
  }

  for (i = 0; i < G_N_ELEMENTS (_h264_levels); i++) {
    const GstH264LevelDescriptor *level = &_h264_levels[i];

    if (bitrate > (guint64) level->max_br * cpb_factor)
      continue;
    if (picSizeMbs > level->max_fs)
      continue;
    if (picSizeMbs > 0) {
      gint max_dpb_frames = MIN (level->max_dpb_mbs / picSizeMbs, 16);
      if (max_dec_frame_buffering > max_dpb_frames)
        continue;

      if (maxMBPS > level->max_mbps)
        continue;
    }

    return level;
  }

  GST_ERROR ("Failed to find a suitable level: "
      "frame is too big or bitrate too high");
  return NULL;
}

/* Maximum sizes for common headers (in bits) */
#define MAX_SPS_HDR_SIZE    16473
#define MAX_VUI_PARAMS_SIZE 210
#define MAX_HRD_PARAMS_SIZE 4103
#define MAX_PPS_HDR_SIZE    101
#define MAX_SLICE_HDR_SIZE  397 + 2572 + 6670 + 2402

/**
 * gst_h264_calculate_coded_size:
 * @sps: the #GstH264SPS
 * @num_slices: number of slices to encode per frame
 *
 * Returns the calculated size of the encoded buffer.
 *
 * Since: 1.28
 */
gsize
gst_h264_calculate_coded_size (GstH264SPS * sps, guint num_slices)
{
  gsize codedbuf_size = 0;
  GstH264Profile profile;
  guint mb_width, mb_height, chroma_subsampling;

  g_return_val_if_fail (sps && num_slices >= 1, 0);

  profile = sps->profile_idc;
  chroma_subsampling = sps->chroma_format_idc;
  mb_width = sps->pic_width_in_mbs_minus1 + 1;
  mb_height = sps->pic_height_in_map_units_minus1 + 1;

  if (profile >= GST_H264_PROFILE_HIGH
      && profile <= GST_H264_PROFILE_STEREO_HIGH) {
    /* The number of bits of macroblock_layer( ) data for any macroblock
       is not greater than 128 + RawMbBits */
    guint RawMbBits, MbWidthC, MbHeightC;
    guint8 bit_depth_luma, bit_depth_chroma;

    bit_depth_luma = sps->bit_depth_luma_minus8 + 8;
    bit_depth_chroma = sps->bit_depth_chroma_minus8 + 8;

    switch (chroma_subsampling) {
      case GST_CHROMA_420:
        MbWidthC = 8;
        MbHeightC = 8;
        break;
      case GST_CHROMA_422:
        MbWidthC = 8;
        MbHeightC = 16;
        break;
      case GST_CHROMA_444:
        MbWidthC = 16;
        MbHeightC = 16;
        break;
      default:
        g_assert_not_reached ();
        break;
    }

    /* The variable RawMbBits is derived as
     * RawMbBits = 256 * BitDepthY + 2 * MbWidthC * MbHeightC * BitDepthC */
    RawMbBits =
        256 * bit_depth_luma + 2 * MbWidthC * MbHeightC * bit_depth_chroma;
    codedbuf_size = (mb_width * mb_height) * (128 + RawMbBits) / 8;
  } else {
    /* The number of bits of macroblock_layer( ) data for any macroblock
     * is not greater than 3200 */
    codedbuf_size = (mb_width * mb_height) * (3200 / 8);
  }

  /* Account for SPS header */
  /* XXX: exclude scaling lists, MVC/SVC extensions */
  codedbuf_size += 4 /* start code */  + GST_ROUND_UP_8 (MAX_SPS_HDR_SIZE +
      MAX_VUI_PARAMS_SIZE + 2 * MAX_HRD_PARAMS_SIZE) / 8;

  /* Account for PPS header */
  /* XXX: exclude slice groups, scaling lists, MVC/SVC extensions */
  codedbuf_size += 4 + GST_ROUND_UP_8 (MAX_PPS_HDR_SIZE) / 8;

  /* Account for slice header */
  codedbuf_size += num_slices * (4 + GST_ROUND_UP_8 (MAX_SLICE_HDR_SIZE) / 8);

  /* Add ceil 5% for safety */
  codedbuf_size = ((guint) (((gfloat) codedbuf_size * 1.05) + 1)) >> 0;

  return codedbuf_size;
}
