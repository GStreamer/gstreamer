/* GStreamer
 * Copyright (C) 2021 Intel Corporation
 *    Author: He Junyan <junyan.he@intel.com>
 * Copyright (C) 2026 Igalia, S.L.
 *    Author: Víctor Jáquez <vjaquez@igalia.com>
 *    Author: Stéphane Cerveau <scerveau@igalia.com>
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
 * SECTION:gsth265encoder
 * @title: GstH265Encoder
 * @short_description: Base class to implement stateless H.265 encoders
 *
 * This H.265 encoder base class helps in for the generation of GOPs (Group of
 * Pictures) using I, P and B frames, along with SPS and PPS proposals. The
 * subclass is expected to implement the rate control algorithms and the
 * specific accelerator logic.
 *
 * Limitations:
 * + Only progressive frames are supported (not interlaced)
 * + Multi-slice encoding is not yet implemented
 * + Scalable and multi-layer bitstreams are not supported
 * + Motion Vector Prediction (MVP) is not fully implemented
 * + Buffer size calculations are approximate (header overhead estimation)
 * + Some profile tables contain placeholder values (multiview, scalable, 3D)
 *
 * Since: 1.30
 */

/* ToDo:
* + add SEI message support */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gsth265encoder.h"

/**
 * GST_FLOW_OUTPUT_NOT_READY:
 *
 * A #GstFlowReturn for not ready operations
 */
#define GST_FLOW_OUTPUT_NOT_READY GST_FLOW_CUSTOM_SUCCESS_2

GST_DEBUG_CATEGORY (gst_h265_encoder_debug);
#define GST_CAT_DEFAULT gst_h265_encoder_debug

#define H265ENC_IDR_PERIOD_DEFAULT        0
#define H265ENC_B_FRAMES_DEFAULT          0
#define H265ENC_I_FRAMES_DEFAULT          0
#define H265ENC_NUM_REF_FRAMES_DEFAULT    3
#define H265ENC_B_PYRAMID_DEFAULT         FALSE

typedef struct _GstH265EncoderPrivate GstH265EncoderPrivate;

/* *INDENT-OFF* */
/* Table A-1 - Level limits */
static const GstH265LevelDescriptor _h265_levels[] = {
  /*                                                   slice_segments_per_picture                       cr_main  */
  /* name     level_idc         max_luma_ps  cpb_main cpb_high   |     MTR MTC max_luma_sr br_main br_high  | cr_high */
  {  "1",     GST_H265_LEVEL_L1,   36864,       350,    0,       16,   1,  1,   552960,     128,    0,      2, 2 },
  {  "2",     GST_H265_LEVEL_L2,   122880,      1500,   0,       16,   1,  1,   3686400,    1500,   0,      2, 2 },
  {  "2.1",   GST_H265_LEVEL_L2_1, 245760,      3000,   0,       20,   1,  1,   7372800,    3000,   0,      2, 2 },
  {  "3",     GST_H265_LEVEL_L3,   552960,      6000,   0,       30,   2,  2,   16588800,   6000,   0,      2, 2 },
  {  "3.1",   GST_H265_LEVEL_L3_1, 983040,      10000,  0,       40,   3,  3,   33177600,   10000,  0,      2, 2 },
  {  "4",     GST_H265_LEVEL_L4,   2228224,     12000,  30000,   75,   5,  5,   66846720,   12000,  30000,  4, 4 },
  {  "4.1",   GST_H265_LEVEL_L4_1, 2228224,     20000,  50000,   75,   5,  5,   133693440,  20000,  50000,  4, 4 },
  {  "5",     GST_H265_LEVEL_L5,   8912896,     25000,  100000,  200,  11, 10,  267386880,  25000,  100000, 6, 4 },
  {  "5.1",   GST_H265_LEVEL_L5_1, 8912896,     40000,  160000,  200,  11, 10,  534773760,  40000,  160000, 8, 4 },
  {  "5.2",   GST_H265_LEVEL_L5_2, 8912896,     60000,  240000,  200,  11, 10,  1069547520, 60000,  240000, 8, 4 },
  {  "6",     GST_H265_LEVEL_L6,   35651584,    60000,  240000,  600,  22, 20,  1069547520, 60000,  240000, 8, 4 },
  {  "6.1",   GST_H265_LEVEL_L6_1, 35651584,    120000, 480000,  600,  22, 20,  2139095040, 120000, 480000, 8, 4 },
  {  "6.2",   GST_H265_LEVEL_L6_2, 35651584,    240000, 800000,  600,  22, 20,  4278190080, 240000, 800000, 6, 4 },
};

/* Table A-2 - CPB BR NAL factor + H.10.2.1 (r) */
typedef struct _GstH265ProfileDescriptor {
  GstH265Profile profile;
  GstH265ProfileIDC profile_idc;
  gboolean high_throughput;

  /* Tables A.2, A.3 and A.5. */
  guint8 max_14bit;
  guint8 max_12bit;
  guint8 max_10bit;
  guint8 max_8bit;
  guint8 max_422chroma;
  guint8 max_420chroma;
  guint8 max_monochrome;
  guint8 intra;
  guint8 one_picture_only;
  guint8 lower_bit_rate;

  /* Table A.8. */
  guint16 cpb_vcl_factor;
  guint16 cpb_nal_factor;
  float format_capability_factor;
  float min_cr_scale_factor;
  guint8 max_dpb_pic_buf;
} GstH265ProfileDescriptor;

/* XXX == 2 => it's ignored by the profile */
static const GstH265ProfileDescriptor _h265_profiles[] = {
  { GST_H265_PROFILE_MAIN, GST_H265_PROFILE_IDC_MAIN,
    FALSE, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1000, 1100, 1.500, 1.0, 6 },
  { GST_H265_PROFILE_MAIN_STILL_PICTURE, GST_H265_PROFILE_IDC_MAIN_STILL_PICTURE,
    FALSE, 2, 2, 2, 2, 2, 2, 2, 1, 1, 2, 1000, 1100, 1.500, 1.0, 6 },
  { GST_H265_PROFILE_MAIN_10, GST_H265_PROFILE_IDC_MAIN_10,
    FALSE, 2, 2, 2, 2, 2, 2, 2, 2, 0, 2, 1000, 1100, 1.875, 1.0, 6 },
  /* { GST_H265_PROFILE_MAIN_10_STILL_PICTURE, GST_H265_PROFILE_IDC_MAIN_10,
     FALSE, 2, 2, 2, 2, 2, 2, 2, 2, 1, 2, 1000, 1100, 1.875, 1.0, 6 }, */
  { GST_H265_PROFILE_MONOCHROME, GST_H265_PROFILE_IDC_FORMAT_RANGE_EXTENSION,
    FALSE, 2, 1, 1, 1, 1, 1, 1, 0, 0, 1,  667,  733, 1.000, 1.0, 6 },
  { GST_H265_PROFILE_MONOCHROME_10, GST_H265_PROFILE_IDC_FORMAT_RANGE_EXTENSION,
    FALSE, 2, 1, 1, 0, 1, 1, 1, 0, 0, 1,  833,  917, 1.250, 1.0, 6 },
  { GST_H265_PROFILE_MONOCHROME_12, GST_H265_PROFILE_IDC_FORMAT_RANGE_EXTENSION,
    FALSE, 2, 1, 0, 0, 1, 1, 1, 0, 0, 1, 1000, 1100, 1.500, 1.0, 6 },
  { GST_H265_PROFILE_MONOCHROME_16, GST_H265_PROFILE_IDC_FORMAT_RANGE_EXTENSION,
    FALSE, 2, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1333, 1467, 2.000, 1.0, 6 },
  { GST_H265_PROFILE_MAIN_12, GST_H265_PROFILE_IDC_FORMAT_RANGE_EXTENSION,
    FALSE, 2, 1, 0, 0, 1, 1, 0, 0, 0, 1, 1500, 1650, 2.250, 1.0, 6 },
  { GST_H265_PROFILE_MAIN_422_10, GST_H265_PROFILE_IDC_FORMAT_RANGE_EXTENSION,
    FALSE, 2, 1, 1, 0, 1, 0, 0, 0, 0, 1, 1667, 1833, 2.500, 0.5, 6 },
  { GST_H265_PROFILE_MAIN_422_12, GST_H265_PROFILE_IDC_FORMAT_RANGE_EXTENSION,
    FALSE, 2, 1, 0, 0, 1, 0, 0, 0, 0, 1, 2000, 2200, 3.000, 0.5, 6 },
  { GST_H265_PROFILE_MAIN_444, GST_H265_PROFILE_IDC_FORMAT_RANGE_EXTENSION,
    FALSE, 2, 1, 1, 1, 0, 0, 0, 0, 0, 1, 2000, 2200, 3.000, 0.5, 6 },
  { GST_H265_PROFILE_MAIN_444_10, GST_H265_PROFILE_IDC_FORMAT_RANGE_EXTENSION,
    FALSE, 2, 1, 1, 0, 0, 0, 0, 0, 0, 1, 2500, 2750, 3.750, 0.5, 6 },
  { GST_H265_PROFILE_MAIN_444_12, GST_H265_PROFILE_IDC_FORMAT_RANGE_EXTENSION,
    FALSE, 2, 1, 0, 0, 0, 0, 0, 0, 0, 1, 3000, 3300, 4.500, 0.5, 6 },
  { GST_H265_PROFILE_MAIN_INTRA, GST_H265_PROFILE_IDC_FORMAT_RANGE_EXTENSION,
    FALSE, 2, 1, 1, 1, 1, 1, 0, 1, 0, 2, 1000, 1100, 1.500, 1.0, 6 },
  { GST_H265_PROFILE_MAIN_10_INTRA, GST_H265_PROFILE_IDC_FORMAT_RANGE_EXTENSION,
    FALSE, 2, 1, 1, 0, 1, 1, 0, 1, 0, 2, 1000, 1100, 1.875, 1.0, 6 },
  { GST_H265_PROFILE_MAIN_12_INTRA, GST_H265_PROFILE_IDC_FORMAT_RANGE_EXTENSION,
    FALSE, 2, 1, 0, 0, 1, 1, 0, 1, 0, 2, 1500, 1650, 2.250, 1.0, 6 },
  { GST_H265_PROFILE_MAIN_422_10_INTRA, GST_H265_PROFILE_IDC_FORMAT_RANGE_EXTENSION,
    FALSE, 2, 1, 1, 0, 1, 0, 0, 1, 0, 2, 1667, 1833, 2.500, 0.5, 6 },
  { GST_H265_PROFILE_MAIN_422_12_INTRA, GST_H265_PROFILE_IDC_FORMAT_RANGE_EXTENSION,
    FALSE, 2, 1, 0, 0, 1, 0, 0, 1, 0, 2, 2000, 2200, 3.000, 0.5, 6 },
  { GST_H265_PROFILE_MAIN_444_INTRA, GST_H265_PROFILE_IDC_FORMAT_RANGE_EXTENSION,
    FALSE, 2, 1, 1, 1, 0, 0, 0, 1, 0, 2, 2000, 2200, 3.000, 0.5, 6 },
  { GST_H265_PROFILE_MAIN_444_10_INTRA, GST_H265_PROFILE_IDC_FORMAT_RANGE_EXTENSION,
    FALSE, 2, 1, 1, 0, 0, 0, 0, 1, 0, 2, 2500, 2750, 3.750, 0.5, 6 },
  { GST_H265_PROFILE_MAIN_444_12_INTRA, GST_H265_PROFILE_IDC_FORMAT_RANGE_EXTENSION,
    FALSE, 2, 1, 0, 0, 0, 0, 0, 1, 0, 2, 3000, 3300, 4.500, 0.5, 6 },
  { GST_H265_PROFILE_MAIN_444_16_INTRA, GST_H265_PROFILE_IDC_FORMAT_RANGE_EXTENSION,
    FALSE, 2, 0, 0, 0, 0, 0, 0, 1, 0, 2, 4000, 4400, 6.000, 0.5, 6 },
  { GST_H265_PROFILE_MAIN_444_STILL_PICTURE, GST_H265_PROFILE_IDC_FORMAT_RANGE_EXTENSION,
    FALSE, 2, 1, 1, 1, 0, 0, 0, 1, 1, 2, 2000, 2200, 3.000, 0.5, 6 },
  { GST_H265_PROFILE_MAIN_444_16_STILL_PICTURE, GST_H265_PROFILE_IDC_FORMAT_RANGE_EXTENSION,
    FALSE, 2, 0, 0, 0, 0, 0, 0, 1, 1, 2, 4000, 4400, 6.000, 0.5, 6 },
  { GST_H265_PROFILE_HIGH_THROUGHPUT_444, GST_H265_PROFILE_IDC_HIGH_THROUGHPUT,
    TRUE,  1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 2000, 2200, 3.000, 0.5, 6 },
  { GST_H265_PROFILE_HIGH_THROUGHPUT_444_10, GST_H265_PROFILE_IDC_HIGH_THROUGHPUT,
    TRUE,  1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 2500, 2750, 3.750, 0.5, 6 },
  { GST_H265_PROFILE_HIGH_THROUGHPUT_444_14, GST_H265_PROFILE_IDC_HIGH_THROUGHPUT,
    TRUE,  1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 3500, 3850, 5.250, 0.5, 6 },
  { GST_H265_PROFILE_HIGH_THROUGHPUT_444_16_INTRA, GST_H265_PROFILE_IDC_HIGH_THROUGHPUT,
    TRUE,  0, 0, 0, 0, 0, 0, 0, 1, 0, 2, 4000, 4400, 6.000, 0.5, 6 },
  { GST_H265_PROFILE_SCREEN_EXTENDED_MAIN, GST_H265_PROFILE_IDC_SCREEN_CONTENT_CODING,
    FALSE, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1000, 1100, 1.500, 1.0, 7 },
  { GST_H265_PROFILE_SCREEN_EXTENDED_MAIN_10, GST_H265_PROFILE_IDC_SCREEN_CONTENT_CODING,
    FALSE, 1, 1, 1, 0, 1, 1, 0, 0, 0, 1, 1000, 1100, 1.875, 1.0, 7 },
  { GST_H265_PROFILE_SCREEN_EXTENDED_MAIN_444, GST_H265_PROFILE_IDC_SCREEN_CONTENT_CODING,
    FALSE, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 2000, 2200, 3.000, 0.5, 7 },
  { GST_H265_PROFILE_SCREEN_EXTENDED_MAIN_444_10, GST_H265_PROFILE_IDC_SCREEN_CONTENT_CODING,
    FALSE, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 2500, 2750, 3.750, 0.5, 7  },
  { GST_H265_PROFILE_SCREEN_EXTENDED_HIGH_THROUGHPUT_444, GST_H265_PROFILE_IDC_HIGH_THROUGHPUT_SCREEN_CONTENT_CODING_EXTENSION,
    TRUE,  1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 2000, 2200, 3.000, 0.5, 7 },
  { GST_H265_PROFILE_SCREEN_EXTENDED_HIGH_THROUGHPUT_444_10, GST_H265_PROFILE_IDC_HIGH_THROUGHPUT_SCREEN_CONTENT_CODING_EXTENSION,
    TRUE,  1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 2500, 2750, 3.750, 0.5, 7 },
  { GST_H265_PROFILE_SCREEN_EXTENDED_HIGH_THROUGHPUT_444_14, GST_H265_PROFILE_IDC_HIGH_THROUGHPUT_SCREEN_CONTENT_CODING_EXTENSION,
    TRUE,  1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 3500, 3850, 5.250, 0.5, 7 },
  /* currently unsupported */
  /* { GST_H265_PROFILE_MULTIVIEW_MAIN, GST_H265_PROFILE_IDC_MULTIVIEW_MAIN, */
  /*   FALSE, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1000, 1100, 1.500, 1.0, 6 }, */
  /* { GST_H265_PROFILE_SCALABLE_MAIN, GST_H265_PROFILE_IDC_SCALABLE_MAIN, */
  /*   FALSE, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1000, 1100, 1.500, 1.0, 6 }, */
  /* { GST_H265_PROFILE_SCALABLE_MAIN_10, GST_H265_PROFILE_IDC_SCALABLE_MAIN, */
  /*   FALSE, 2, 2, 2, 2, 2, 2, 2, 2, 0, 2, 1000, 1100, 1.875, 1.0, 6 }, */
  /* { GST_H265_PROFILE_SCALABLE_MONOCHROME, GST_H265_PROFILE_IDC_SCALABLE_FORMAT_RANGE_EXTENSION, */
  /*   FALSE, 2, 1, 1, 1, 1, 1, 1, 0, 0, 1,  667,  733, 1.000, 1.0, 6 }, */
  /* { GST_H265_PROFILE_SCALABLE_MONOCHROME_12, GST_H265_PROFILE_IDC_SCALABLE_FORMAT_RANGE_EXTENSION, */
  /*   FALSE, 2, 1, 0, 0, 1, 1, 1, 0, 0, 1, 1000, 1100, 1.500, 1.0, 6 }, */
  /* { GST_H265_PROFILE_SCALABLE_MONOCHROME_16, GST_H265_PROFILE_IDC_SCALABLE_FORMAT_RANGE_EXTENSION, */
  /*   FALSE, 2, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1333, 1467, 2.000, 1.0, 6 }, */
  /* { GST_H265_PROFILE_SCALABLE_MAIN_444, GST_H265_PROFILE_IDC_SCALABLE_FORMAT_RANGE_EXTENSION, */
  /*   FALSE, 2, 1, 1, 1, 0, 0, 0, 0, 0, 1, 2000, 2200, 3.000, 0.5, 6 }, */
  /* { GST_H265_PROFILE_3D_MAIN, GST_H265_PROFILE_IDC_3D_MAIN, */
  /*   FALSE, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1000, 1100, 1.500, 1.0, 6 }, */
};

/* TABLE E-1 Meaning of sample aspect ratio indicator */
static const struct {
  int num;
  int den;
} _h265_aspect_ratio[] = {
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

struct _GstH265EncoderPrivate
{
  GstVideoCodecState *input_state;

  struct
  {
    guint max_num_reference_list0;
    guint max_num_reference_list1;
    gboolean b_pyramid_supported;
    guint preferred_output_delay;
    gint surface_width;
    gint surface_height;
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
    GstH265Profile profile;
    GstH265Level level;
    GstH265Tier tier;
  } stream;

  struct
  {
    GstH26XGOPParameters params;
    GstH26XGOPMapper *mapper;

    /* Max poc within a GOP. */
    guint32 max_poc;
    guint32 log2_max_poc;

    /* Total ref frames of list0 and list1. */
    guint32 num_ref_frames;
    guint32 num_ref_forward;
    guint32 num_ref_backward;

    guint num_reorder_frames;
    guint max_dec_pic_buffering;

    GstVideoCodecFrame *last_keyframe;
  } gop;

  /* current params */
  struct
  {
    GstH265ProfileTierLevel ptl;
    GstH265SPS sps;
    GstH265PPS pps;
    GstH265VPS vps;
  } params;

  GstClockTime frame_duration;
  guint fps_n;
  guint fps_d;

  GQueue output_list;
  GQueue ref_list;
  GQueue reorder_list;
  GstVecDeque *dts_queue;

  GArray *list_forward;
  GArray *list_backward;
  GArray *list_negative;
  GArray *list_positive;

  gboolean is_live;
  gboolean need_configure;
};

#define parent_class gst_h265_encoder_parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstH265Encoder, gst_h265_encoder,
    GST_TYPE_VIDEO_ENCODER,
    G_ADD_PRIVATE (GstH265Encoder);
    GST_DEBUG_CATEGORY_INIT (gst_h265_encoder_debug, "h265encoder", 0,
        "H265 Video Encoder"));

GST_DEFINE_MINI_OBJECT_TYPE (GstH265EncoderFrame, gst_h265_encoder_frame);

#define update_property(type, obj, old_val, new_val, prop_id)         \
static inline void                                                    \
gst_h265_encoder_update_property_##type (GstH265Encoder * encoder, type * old_val, type new_val, guint prop_id) \
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
  gst_h265_encoder_update_property_guint (obj, old_val, new_val, prop_id)
#define update_property_bool(obj, old_val, new_val, prop_id)        \
  gst_h265_encoder_update_property_gboolean (obj, old_val, new_val, prop_id)

#define _GET_PRIV(obj) gst_h265_encoder_get_instance_private (obj)
#define _GET_FRAME(codec_frame) GST_H265_ENCODER_FRAME (gst_video_codec_frame_get_user_data (codec_frame))

static void
gst_h265_encoder_frame_free (GstMiniObject * mini_object)
{
  GstH265EncoderFrame *frame = GST_H265_ENCODER_FRAME (mini_object);

  GST_TRACE ("Free frame %p", frame);
  if (frame->user_data_destroy_notify)
    frame->user_data_destroy_notify (frame->user_data);

  g_free (frame);
}

/**
 * gst_h265_encoder_frame_new:
 *
 * Create new #GstH265EncoderFrame
 *
 * Returns: a new #GstH265EncoderFrame
 */
GstH265EncoderFrame *
gst_h265_encoder_frame_new (void)
{
  GstH265EncoderFrame *frame;

  frame = g_new (GstH265EncoderFrame, 1);

  frame->poc = 0;
  frame->user_data = NULL;
  frame->user_data_destroy_notify = NULL;

  gst_mini_object_init (GST_MINI_OBJECT_CAST (frame), 0,
      GST_TYPE_H265_ENCODER_FRAME, NULL, NULL, gst_h265_encoder_frame_free);

  GST_TRACE ("New frame %p", frame);

  return frame;
}

/**
 * gst_h265_encoder_frame_set_user_data:
 * @frame: a #GstH265EncoderFrame
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
gst_h265_encoder_frame_set_user_data (GstH265EncoderFrame * frame,
    gpointer user_data, GDestroyNotify notify)
{
  if (frame->user_data_destroy_notify)
    frame->user_data_destroy_notify (frame->user_data);

  frame->user_data = user_data;
  frame->user_data_destroy_notify = notify;
}

/**
 * gst_h265_encoder_frame_get_user_data:
 * @frame: a #GstH265EncoderFrame
 *
 * Gets private data set on the frame by the subclass via
 * gst_video_codec_frame_set_user_data() previously.
 *
 * Returns: (transfer none): The previously set user_data
 */

static inline void
ensure_and_adjust_gop_parameters (GstH265Encoder * self)
{
  GstH265EncoderPrivate *priv = _GET_PRIV (self);

  /* If not set, generate a idr every second */
  if (priv->gop.params.idr_period == 0) {
    priv->gop.params.idr_period =
        ((guint64) priv->fps_n + (guint64) priv->fps_d - 1) / priv->fps_d;
  }

  /* Prefer have more than 1 reference for the GOP which is not very small. */
  if (priv->gop.params.idr_period > 8) {
    if (priv->gop.params.num_bframes > (priv->gop.params.idr_period - 1) / 2) {
      priv->gop.params.num_bframes = (priv->gop.params.idr_period - 1) / 2;
      GST_INFO_OBJECT (self, "Lowering the number of num_bframes to %d",
          priv->gop.params.num_bframes);
    }
  } else {
    /* begin and end should be reference */
    if (priv->gop.params.num_bframes > priv->gop.params.idr_period - 1 - 1) {
      if (priv->gop.params.idr_period > 1) {
        priv->gop.params.num_bframes = priv->gop.params.idr_period - 1 - 1;
      } else {
        priv->gop.params.num_bframes = 0;
      }
      GST_INFO_OBJECT (self, "Lowering the number of num_bframes to %d",
          priv->gop.params.num_bframes);
    }
  }
}

enum GOPConfigResult
{
  GOPConfigResultIntraOnly,
  GOPConfigResultClosed,
};

static inline enum GOPConfigResult
configure_reference_counts (GstH265Encoder * self,
    guint32 * n_forward, guint32 * n_backward)
{
  GstH265EncoderPrivate *priv = _GET_PRIV (self);

  *n_forward = priv->config.max_num_reference_list0;
  *n_backward = priv->config.max_num_reference_list1;

  if (*n_forward == 0 && *n_backward == 0) {
    GST_WARNING_OBJECT (self,
        "Maximum number of references in both lists cannot be zero");
    *n_forward = 1;
    *n_backward = 0;
  }

  *n_forward = MIN (*n_forward, priv->gop.num_ref_frames);
  *n_backward = MIN (*n_backward, priv->gop.num_ref_frames);

  if (*n_forward == 0) {
    GST_INFO_OBJECT (self,
        "No reference support, fallback to intra only stream");

    /* It does not make sense that if only the list1 exists. */
    priv->gop.num_ref_frames = 0;

    priv->gop.params.ip_period = 0;
    priv->gop.params.num_bframes = 0;
    priv->gop.params.b_pyramid = FALSE;
    priv->gop.params.highest_pyramid_level = 0;
    priv->gop.params.num_iframes = priv->gop.params.idr_period - 1;     /* The idr */
    priv->gop.num_ref_forward = 0;
    priv->gop.num_ref_backward = 0;
    return GOPConfigResultIntraOnly;
  }

  if (priv->gop.num_ref_frames <= 1) {
    GST_INFO_OBJECT (self, "The number of reference frames is only %d,"
        " no B frame allowed, fallback to I/P mode", priv->gop.num_ref_frames);
    priv->gop.params.num_bframes = 0;
    *n_backward = 0;
  }

  /* B pyramid needs B pictures as references in both lists. */
  if (priv->gop.params.b_pyramid && !priv->config.b_pyramid_supported) {
    GST_INFO_OBJECT (self, "The accelerator does not support B pictures as "
        "references, disabling b-pyramid");
    priv->gop.params.b_pyramid = FALSE;
  }

  /* b_pyramid needs at least 1 ref for B, besides the I/P */
  if (priv->gop.params.b_pyramid && priv->gop.num_ref_frames <= 1) {
    GST_INFO_OBJECT (self, "The number of reference frames is only %d,"
        " not enough for b_pyramid", priv->gop.num_ref_frames);
    priv->gop.params.b_pyramid = FALSE;
  }

  if (*n_backward == 0 && priv->gop.params.num_bframes > 0) {
    GST_INFO_OBJECT (self,
        "No max reference count for list 1, fallback to I/P mode");
    priv->gop.params.num_bframes = 0;
    priv->gop.params.b_pyramid = FALSE;
  }

  /* I/P mode, no list1 needed. */
  if (priv->gop.params.num_bframes == 0)
    *n_backward = 0;

  /* Not enough B frame, no need for b_pyramid. */
  if (priv->gop.params.num_bframes <= 1)
    priv->gop.params.b_pyramid = FALSE;

  if (priv->gop.num_ref_frames > *n_forward + *n_backward) {
    priv->gop.num_ref_frames = *n_forward + *n_backward;
    GST_WARNING_OBJECT (self, "number of reference frames is bigger than max "
        "reference count. Lowered number of reference frames to %d",
        priv->gop.num_ref_frames);
  }
  priv->gop.num_ref_frames = MIN (priv->gop.num_ref_frames, 15);

  return GOPConfigResultClosed;
}

static inline guint32
calculate_gop_ref_num (GstH265Encoder * self)
{
  GstH265EncoderPrivate *priv = _GET_PRIV (self);
  guint32 bframes_plus1, total_frames, gop_ref_num;

  bframes_plus1 = priv->gop.params.num_bframes + 1;
  total_frames = priv->gop.params.idr_period + priv->gop.params.num_bframes;

  /* How many possible refs within a GOP. */
  gop_ref_num = total_frames / bframes_plus1;

  /* The end reference. */
  if (priv->gop.params.num_bframes > 0
      /* frame_num % (priv->gop.num_bframes + 1) happens to be the end P */
      && (priv->gop.params.idr_period % bframes_plus1 != 1))
    gop_ref_num++;

  return gop_ref_num;
}

static inline void
adjust_reference_distribution (GstH265Encoder * self,
    guint32 n_forward, guint32 n_backward, guint32 gop_ref_num)
{
  GstH265EncoderPrivate *priv = _GET_PRIV (self);

  /* Adjust reference num based on B frames and B pyramid. */
  if (priv->gop.params.num_bframes == 0) {
    priv->gop.params.b_pyramid = FALSE;
    priv->gop.num_ref_forward = priv->gop.num_ref_frames;
    priv->gop.num_ref_backward = 0;
  } else if (priv->gop.params.b_pyramid) {
    guint b_frames = priv->gop.params.num_bframes;

    /* b pyramid has only one backward reference */
    priv->gop.num_ref_backward = 1;
    priv->gop.num_ref_forward =
        MIN (priv->gop.num_ref_frames - priv->gop.num_ref_backward, n_forward);

    b_frames /= 2;
    while (b_frames) {
      /* All the reference pictures and the current picture should be in the
         DPB. So each B level as reference, plus the IDR or P in both ends and
         the current picture should not exceed the max_dec_pic_buffering. */
      if (priv->gop.params.highest_pyramid_level + 2 + 1 == 16)
        break;

      priv->gop.params.highest_pyramid_level++;
      b_frames /= 2;
    }

    GST_INFO_OBJECT (self, "pyramid level is %d",
        priv->gop.params.highest_pyramid_level);
  } else {
    /* We prefer list0. Backward references have more latency. */
    priv->gop.num_ref_backward = 1;

    priv->gop.num_ref_forward =
        priv->gop.num_ref_frames - priv->gop.num_ref_backward;

    /* Balance the forward and backward references, but not cause a big
       latency. */
    while ((priv->gop.params.num_bframes * priv->gop.num_ref_backward <= 16)
        && (priv->gop.num_ref_backward <= gop_ref_num)
        && (priv->gop.num_ref_backward < n_backward)
        && (priv->gop.num_ref_forward / priv->gop.num_ref_backward > 4)) {
      priv->gop.num_ref_forward--;
      priv->gop.num_ref_backward++;
    }

    priv->gop.num_ref_forward = MIN (priv->gop.num_ref_forward, n_forward);
  }
}

/*
 * Consider the idr_period, num_bframes, L0/L1 reference number.
 *
 * TODO:
 * + Load some preset fixed GOP structure.
 * + Skip this if in lookahead mode.
 */
static gboolean
gst_h265_encoder_generate_gop_structure_internal (GstH265Encoder * self)
{
  GstH265EncoderPrivate *priv = _GET_PRIV (self);
  guint32 n_forward, n_backward, gop_ref_num, p_frames, log2_max_poc;
  gboolean ret;

  /* FIXME: max number of references in list0 should be set before generating
   * the GOP mapper. And L0 size can be zero for hardware that only process
   * intra-frame */

  if (priv->gop.mapper && gst_h26x_gop_mapper_is_generated (priv->gop.mapper)) {
    GST_INFO_OBJECT (self, "GOP Mapper is already generated. Reset it first.");
    return TRUE;
  }

  ensure_and_adjust_gop_parameters (self);

  if (configure_reference_counts (self, &n_forward,
          &n_backward) == GOPConfigResultClosed) {
    gop_ref_num = calculate_gop_ref_num (self);

    adjust_reference_distribution (self, n_forward, n_backward, gop_ref_num);

    /* It's OK, keep slots for GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME frame. */
    if (priv->gop.num_ref_forward > gop_ref_num) {
      GST_DEBUG_OBJECT (self, "num_ref_frames %d is bigger than gop_ref_num %d",
          priv->gop.num_ref_forward, gop_ref_num);
    }

    /* Include the reference picture itself. */
    priv->gop.params.ip_period = 1 + priv->gop.params.num_bframes;

    p_frames = MAX (gop_ref_num - 1 /* IDR */ , 0);
    if (priv->gop.params.num_iframes > p_frames) {
      priv->gop.params.num_iframes = p_frames;
      GST_INFO_OBJECT (self, "Too many I frames insertion, lowering it to %d",
          priv->gop.params.num_iframes);
    }

    if (priv->gop.params.num_iframes > 0) {
      guint total_i_frames = priv->gop.params.num_iframes + 1;  /* IDR */
      priv->gop.params.i_period =
          (gop_ref_num / total_i_frames) * (priv->gop.params.num_bframes + 1);
    }
  }

  /* initialize max_frame_num and max_poc. */
  log2_max_poc = 4;
  while ((1 << log2_max_poc) <= priv->gop.params.idr_period)
    log2_max_poc++;

  /* With b-pyramid, B reference frames are stored with a POC smaller than the
     following anchor picture, so the POC LSB difference between consecutive
     pictures in decoding order can approach half a GOP. Clause 8.3.1 computes
     PicOrderCntMsb from that difference: if it reaches MaxPicOrderCntLsb / 2
     a wrong MSB is added or subtracted and the reference POC becomes invalid.
     Grow the LSB range so that half of it always covers the widest POC span
     between any two pictures of a GOP (0 .. idr_period - 1). */
  if (priv->gop.params.b_pyramid) {
    while ((1 << (log2_max_poc - 1)) < priv->gop.params.idr_period)
      log2_max_poc++;
  }

  priv->gop.log2_max_poc = log2_max_poc;
  priv->gop.max_poc = (1 << priv->gop.log2_max_poc);

  priv->gop.num_reorder_frames = priv->gop.params.b_pyramid ? priv->gop.params.highest_pyramid_level + 1        /* the last P frame */
      : priv->gop.num_ref_backward;
  priv->gop.num_reorder_frames = MIN (priv->gop.num_reorder_frames, 16);

  /* Intra only streams: the IDR picture is a reference and occupies the DPB
     together with the current picture. */
  if (priv->gop.params.ip_period == 0) {
    priv->gop.max_dec_pic_buffering = 1 + 1;    /* IDR and current frame. */
  } else {
    priv->gop.max_dec_pic_buffering = MIN (priv->gop.params.b_pyramid
        ? priv->gop.params.highest_pyramid_level + 2 + 1
        : priv->gop.num_ref_frames + 1, 16);
  }

  GST_INFO_OBJECT (self, "GOP size: %d, forward reference %d, backward"
      " reference %d", priv->gop.params.idr_period, priv->gop.num_ref_forward,
      priv->gop.num_ref_backward);

  if (!priv->gop.mapper)
    priv->gop.mapper = gst_h26x_gop_mapper_new ();
  ret = gst_h26x_gop_mapper_set_params (priv->gop.mapper, &priv->gop.params);
  if (!ret) {
    GST_ERROR_OBJECT (self, "Invalid GOP parameters");
    return FALSE;
  }
  gst_h26x_gop_mapper_generate (priv->gop.mapper);

  /* updates & notifications */
  update_property_uint (self, &priv->prop.idr_period,
      priv->gop.params.idr_period, PROP_IDR_PERIOD);
  update_property_uint (self, &priv->prop.num_ref_frames,
      priv->gop.num_ref_frames, PROP_NUM_REF_FRAMES);
  update_property_uint (self, &priv->prop.num_iframes,
      priv->gop.params.num_iframes, PROP_IFRAMES);
  update_property_bool (self, &priv->prop.b_pyramid,
      priv->gop.params.b_pyramid, PROP_B_PYRAMID);
  update_property_uint (self, &priv->prop.num_bframes,
      priv->gop.params.num_bframes, PROP_BFRAMES);

  return TRUE;
}

static inline void
gst_h265_encoder_flush_lists (GstH265Encoder * self)
{
  GstH265EncoderPrivate *priv = _GET_PRIV (self);

  g_queue_clear_full (&priv->output_list,
      (GDestroyNotify) gst_video_codec_frame_unref);
  g_queue_clear_full (&priv->ref_list,
      (GDestroyNotify) gst_video_codec_frame_unref);
  g_queue_clear_full (&priv->reorder_list,
      (GDestroyNotify) gst_video_codec_frame_unref);
}

static gboolean
gst_h265_encoder_start (GstVideoEncoder * encoder)
{
  /* Set the minimum pts to some huge value (1000 hours). This keeps
   * the dts at the start of the stream from needing to be negative. */
  gst_video_encoder_set_min_pts (encoder, GST_SECOND * 60 * 60 * 1000);
  return TRUE;
}

static gboolean
gst_h265_encoder_stop (GstVideoEncoder * encoder)
{
  GstH265Encoder *self = GST_H265_ENCODER (encoder);
  GstH265EncoderPrivate *priv = _GET_PRIV (self);

  gst_h265_encoder_flush_lists (self);

  gst_vec_deque_clear (priv->dts_queue);

  g_clear_pointer (&priv->input_state, gst_video_codec_state_unref);

  return TRUE;
}

static void
gst_h265_encoder_reset (GstH265Encoder * self)
{
  GstH265EncoderPrivate *priv = _GET_PRIV (self);
  GstH265EncoderClass *klass = GST_H265_ENCODER_GET_CLASS (self);

  GST_OBJECT_LOCK (self);
  priv->gop.params.idr_period = priv->prop.idr_period;
  priv->gop.num_ref_frames = priv->prop.num_ref_frames;
  priv->gop.params.num_bframes = priv->prop.num_bframes;
  priv->gop.params.num_iframes = priv->prop.num_iframes;
  priv->gop.params.b_pyramid = priv->prop.b_pyramid;
  /* H.265 POC increments by 1 per frame. */
  priv->gop.params.poc_step = 1;
  GST_OBJECT_UNLOCK (self);

  priv->stream.profile = GST_H265_PROFILE_INVALID;
  priv->stream.level = 0;

  priv->gop.params.i_period = 0;
  priv->gop.params.ip_period = 0;
  priv->gop.params.highest_pyramid_level = 0;
  priv->gop.max_poc = 0;
  priv->gop.log2_max_poc = 0;
  priv->gop.num_ref_forward = 0;
  priv->gop.num_ref_backward = 0;
  priv->gop.num_reorder_frames = 0;
  priv->gop.max_dec_pic_buffering = 0;
  priv->gop.last_keyframe = NULL;

  if (priv->gop.mapper)
    gst_h26x_gop_mapper_reset (priv->gop.mapper);

  /* *INDENT-OFF* */
  priv->params.sps = (GstH265SPS) { 0, };
  priv->params.pps = (GstH265PPS) { 0, };
  priv->params.vps = (GstH265VPS) { 0, };
  /* *INDENT-ON* */

  g_atomic_int_set (&priv->need_configure, FALSE);

  if (klass->reset)
    klass->reset (self);
}

static gboolean
gst_h265_encoder_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state)
{
  GstH265Encoder *self = GST_H265_ENCODER (encoder);
  GstH265EncoderPrivate *priv = _GET_PRIV (self);
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

  priv->frame_duration =
      gst_util_uint64_scale (GST_SECOND, priv->fps_d, priv->fps_n);

  /* default values, they can be changed by subclass  */
  priv->config.surface_height =
      GST_ROUND_UP_16 (GST_VIDEO_INFO_HEIGHT (&priv->input_state->info));
  priv->config.surface_width =
      GST_ROUND_UP_16 (GST_VIDEO_INFO_WIDTH (&priv->input_state->info));

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
gst_h265_encoder_finish_frame (GstH265Encoder * self,
    GstVideoCodecFrame * frame)
{
  GstH265EncoderPrivate *priv = _GET_PRIV (self);
  GstH265EncoderClass *base_class = GST_H265_ENCODER_GET_CLASS (self);
  GstH265EncoderFrame *h265_frame = _GET_FRAME (frame);
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

  if (!frame->output_buffer)
    goto prepare_error;

  if (h265_frame->poc == 0) {
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

/* Table 7-7 */
static guint8
gop_2_slice (GstH26XGOPType type)
{
  switch (type) {
    case GST_H26X_GOP_TYPE_B:
      return GST_H265_B_SLICE;
    case GST_H26X_GOP_TYPE_P:
      return GST_H265_P_SLICE;
    case GST_H26X_GOP_TYPE_I:
      return GST_H265_I_SLICE;
    default:
      g_assert_not_reached ();
      return 255;
  }
}

static gboolean
gst_h265_encoder_reorder_lists_push (GstH265Encoder * self,
    GstVideoCodecFrame * frame, gboolean last)
{
  GstH265EncoderFrame *h265_frame;
  GstH265EncoderPrivate *priv = _GET_PRIV (self);
  gboolean add_cached_key_frame = FALSE;
  guint32 cur_frame_index;
  GstH26XGOP *next;

  if (!priv->gop.mapper)
    return TRUE;

  cur_frame_index = gst_h26x_gop_mapper_get_current_index (priv->gop.mapper);

  g_assert (cur_frame_index <= priv->gop.params.idr_period);

  if (frame) {
    h265_frame = _GET_FRAME (frame);

    /* Force to insert the key frame inside a GOP, just end the current
     * GOP and start a new one. */
    if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (frame) && cur_frame_index > 0
        && cur_frame_index < priv->gop.params.idr_period) {
      GST_DEBUG_OBJECT (self, "system_frame_number: %u is a force key "
          "frame(IDR), begin a new GOP.", frame->system_frame_number);

      gst_h26x_gop_mapper_reset_index (priv->gop.mapper);

      next = gst_h26x_gop_mapper_get_next (priv->gop.mapper);
      if (!next)
        return FALSE;
      h265_frame->gop = *next;
      h265_frame->poc = 0;

      /* The previous key frame should be already be poped out. */
      g_assert (priv->gop.last_keyframe == NULL);

      /* An empty reorder list, start the new GOP immediately. */
      if (g_queue_is_empty (&priv->reorder_list)) {
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
      /* Begin a new GOP, should have an empty reorder_list. */
      if (cur_frame_index == priv->gop.params.idr_period) {
        g_assert (g_queue_is_empty (&priv->reorder_list));
        cur_frame_index = 0;
      }

      if (cur_frame_index == 0) {
        g_assert (h265_frame->poc == 0);
        GST_LOG_OBJECT (self, "system_frame_number: %d, an IDR frame, starts"
            " a new GOP", frame->system_frame_number);

        g_queue_clear_full (&priv->ref_list,
            (GDestroyNotify) gst_video_codec_frame_unref);
      }

      next = gst_h26x_gop_mapper_get_next (priv->gop.mapper);
      if (!next)
        return FALSE;
      h265_frame->gop = *next;
      h265_frame->poc = cur_frame_index;

      GST_LOG_OBJECT (self, "Push frame, system_frame_number: %d, poc %d, "
          "frame type %s", frame->system_frame_number, h265_frame->poc,
          gst_h265_slice_type_to_string (gop_2_slice (h265_frame->gop.type)));

      g_queue_push_tail (&priv->reorder_list,
          gst_video_codec_frame_ref (frame));
    }
  } else if (priv->gop.last_keyframe) {
    g_assert (priv->gop.last_keyframe ==
        g_queue_peek_tail (&priv->reorder_list));

    if (g_queue_get_length (&priv->reorder_list) == 1) {
      /* The last cached key frame begins a new GOP */
      gst_h26x_gop_mapper_set_current_index (priv->gop.mapper, 0);
      priv->gop.last_keyframe = NULL;
      g_queue_clear_full (&priv->ref_list,
          (GDestroyNotify) gst_video_codec_frame_unref);
    }
  }

  cur_frame_index = gst_h26x_gop_mapper_get_current_index (priv->gop.mapper);

  /* ensure the last one a non-B and end the GOP. */
  if (last && cur_frame_index < priv->gop.params.idr_period) {
    GstVideoCodecFrame *last_frame;

    /* Ensure next push will start a new GOP. */
    gst_h26x_gop_mapper_set_current_index (priv->gop.mapper,
        priv->gop.params.idr_period);

    if (!g_queue_is_empty (&priv->reorder_list)) {
      last_frame = g_queue_peek_tail (&priv->reorder_list);
      h265_frame = _GET_FRAME (last_frame);
      if (GST_H26X_GOP_IS (&h265_frame->gop, B)) {
        h265_frame->gop.type = GST_H26X_GOP_TYPE_P;
        h265_frame->gop.is_ref = TRUE;
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
  GstH265EncoderFrame *frame = _GET_FRAME (data);
  struct RefFramesCount *count = (struct RefFramesCount *) user_data;

  g_assert (frame->poc != count->poc);
  if (frame->poc > count->poc)
    count->num++;
}

static GstVideoCodecFrame *
_pop_pyramid_b_frame (GstH265Encoder * self, guint gop_len)
{
  GstH265EncoderPrivate *priv = _GET_PRIV (self);
  guint i;
  gint index = -1;
  GstH265EncoderFrame *h265_frame, *b_h265_frame;
  GstVideoCodecFrame *frame, *b_frame;
  struct RefFramesCount count;

  g_assert (priv->gop.num_ref_backward == 1);

  b_frame = NULL;
  b_h265_frame = NULL;

  /* Find the lowest level with smallest poc. */
  for (i = 0; i < gop_len; i++) {

    frame = g_queue_peek_nth (&priv->reorder_list, i);

    if (!b_frame) {
      b_frame = frame;
      b_h265_frame = _GET_FRAME (b_frame);
      index = i;
      continue;
    }

    h265_frame = _GET_FRAME (frame);
    if (b_h265_frame->gop.pyramid_level < h265_frame->gop.pyramid_level) {
      b_frame = frame;
      b_h265_frame = h265_frame;
      index = i;
      continue;
    }

    if (b_h265_frame->poc > h265_frame->poc) {
      b_frame = frame;
      b_h265_frame = h265_frame;
      index = i;
    }
  }

again:
  /* Check whether its refs are already poped. */
  g_assert (b_h265_frame->gop.left_ref_poc_diff != 0);
  g_assert (b_h265_frame->gop.right_ref_poc_diff != 0);

  for (i = 0; i < gop_len; i++) {
    GstH265EncoderFrame *h265_frame;
    GstVideoCodecFrame *frame;

    frame = g_queue_peek_nth (&priv->reorder_list, i);

    if (frame == b_frame)
      continue;

    h265_frame = _GET_FRAME (frame);
    if (h265_frame->poc == b_h265_frame->poc
        + b_h265_frame->gop.left_ref_poc_diff
        || h265_frame->poc == b_h265_frame->poc
        + b_h265_frame->gop.right_ref_poc_diff) {
      b_frame = frame;
      b_h265_frame = h265_frame;
      index = i;
      goto again;
    }
  }

  /* Ensure we already have enough backward references */
  count.num = 0;
  count.poc = b_h265_frame->poc;
  g_queue_foreach (&priv->ref_list, (GFunc) _count_backward_ref_num, &count);
  if (count.num >= priv->gop.num_ref_backward /* 1 */ ) {
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
gst_h265_encoder_reorder_lists_pop (GstH265Encoder * self,
    GstVideoCodecFrame ** out_frame)
{
  GstH265EncoderPrivate *priv = _GET_PRIV (self);
  GstH265EncoderFrame *h265_frame;
  GstVideoCodecFrame *frame;
  struct RefFramesCount count;
  guint gop_len;
  guint32 cur_frame_index;

  if (!priv->gop.mapper)
    return TRUE;

  cur_frame_index = gst_h26x_gop_mapper_get_current_index (priv->gop.mapper);

  g_return_val_if_fail (cur_frame_index <= priv->gop.params.idr_period, FALSE);

  *out_frame = NULL;

  if (g_queue_is_empty (&priv->reorder_list))
    return TRUE;

  gop_len = g_queue_get_length (&priv->reorder_list);

  if (priv->gop.last_keyframe && gop_len > 1)
    gop_len--;

  /* Return the last pushed non-B immediately */
  frame = g_queue_peek_nth (&priv->reorder_list, gop_len - 1);
  h265_frame = _GET_FRAME (frame);
  if (!GST_H26X_GOP_IS (&h265_frame->gop, B)) {
    frame = g_queue_pop_nth (&priv->reorder_list, gop_len - 1);
    goto get_one;
  }

  if (priv->gop.params.b_pyramid) {
    frame = _pop_pyramid_b_frame (self, gop_len);
    if (!frame)
      return TRUE;
    goto get_one;
  }

  g_assert (priv->gop.num_ref_backward > 0);

  /* If GOP end, pop anyway  */
  if (cur_frame_index == priv->gop.params.idr_period) {
    frame = g_queue_pop_head (&priv->reorder_list);
    goto get_one;
  }

  /* Ensure we already have enough backward references */
  frame = g_queue_peek_head (&priv->reorder_list);
  h265_frame = _GET_FRAME (frame);
  count.num = 0;
  count.poc = h265_frame->poc;
  g_queue_foreach (&priv->ref_list, _count_backward_ref_num, &count);
  if (count.num >= priv->gop.num_ref_backward) {
    frame = g_queue_pop_head (&priv->reorder_list);
    goto get_one;
  }

  return TRUE;

get_one:
  h265_frame = _GET_FRAME (frame);

  if (priv->gop.params.b_pyramid && GST_H26X_GOP_IS (&h265_frame->gop, B)) {
    GST_LOG_OBJECT (self, "pop a pyramid B frame with system_frame_number:"
        " %d, poc: %d, %s, level %d",
        frame->system_frame_number, h265_frame->poc,
        h265_frame->gop.is_ref ? "ref" : "", h265_frame->gop.pyramid_level);
  } else {
    GST_LOG_OBJECT (self, "pop a frame with system_frame_number: %d,"
        " frame type: %s, poc: %d, %s",
        frame->system_frame_number,
        gst_h265_slice_type_to_string (gop_2_slice (h265_frame->gop.type)),
        h265_frame->poc, h265_frame->gop.is_ref ? "ref" : "");
  }

  /* unref frame popped from queue or pyramid b_frame */
  gst_video_codec_frame_unref (frame);
  *out_frame = frame;
  return TRUE;
}

static gboolean
gst_h265_encoder_reorder_frame (GstH265Encoder * self,
    GstVideoCodecFrame * frame, gboolean bump_all,
    GstVideoCodecFrame ** out_frame)
{
  if (!gst_h265_encoder_reorder_lists_push (self, frame, bump_all)) {
    GST_ERROR_OBJECT (self, "Failed to push the input frame"
        " system_frame_number: %d into the reorder list",
        frame->system_frame_number);

    *out_frame = NULL;
    return FALSE;
  }

  if (!gst_h265_encoder_reorder_lists_pop (self, out_frame)) {
    GST_ERROR_OBJECT (self, "Failed to pop the frame from the reorder list");
    *out_frame = NULL;
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_h265_encoder_construct_reference_lists (GstH265Encoder * self,
    GstH265EncoderFrame * frame)
{
  GstH265EncoderPrivate *priv = _GET_PRIV (self);
  GstH265EncoderFrame *ref_h265frame;
  GArray *forward, *backward, *positive, *negative;

  forward = priv->list_forward;
  backward = priv->list_backward;

  g_array_set_size (forward, 0);
  g_array_set_size (backward, 0);

  positive = priv->list_positive;
  negative = priv->list_negative;

  g_array_set_size (positive, 0);
  g_array_set_size (negative, 0);

  /* IDR pictures have no reference pictures, but non-IDR I frames (e.g.
   * reference I pictures inserted in the middle of a GOP) still need their
   * reference lists built so the short-term RPS keeps the earlier pictures
   * marked as references for the following inter frames (H.265 8.3.2). */
  if (GST_H26X_GOP_IS_IDR (&frame->gop))
    return TRUE;

  /* Construct forward reference list. The reference list is kept
   * sorted by ascending POC, so walking it from the tail yields the nearest
   * forward references first. */
  for (GList * l = g_queue_peek_tail_link (&priv->ref_list); l; l = l->prev) {
    ref_h265frame = _GET_FRAME (l->data);
    if (ref_h265frame->poc > frame->poc)
      continue;

    if (!ref_h265frame->gop.is_ref) {
      GST_WARNING_OBJECT (self, "element in forward list is not a reference");
      return FALSE;
    }
    g_array_append_val (forward, ref_h265frame);
  }

  for (gint i = 0; i < forward->len; i++) {
    gint poc = g_array_index (forward, GstH265EncoderFrame *, i)->poc;
    g_array_append_val (negative, poc);
  }

  if (forward->len > priv->gop.num_ref_forward)
    g_array_set_size (forward, priv->gop.num_ref_forward);

  /* TODO: if MVP enabled, fail if collocated_ref_idx >= forward->len,
   * else get collocated_poc = forward[collocated_ref_idx]->poc
   */

  if (!GST_H26X_GOP_IS (&frame->gop, B))
    return TRUE;

  for (GList * l = g_queue_peek_head_link (&priv->ref_list); l; l = l->next) {
    ref_h265frame = _GET_FRAME (l->data);
    if (ref_h265frame->poc < frame->poc)
      continue;

    if (!ref_h265frame->gop.is_ref) {
      GST_WARNING_OBJECT (self, "element in backward list is not a reference");
      return FALSE;
    }
    g_array_append_val (backward, ref_h265frame);
  }

  /* B frame, construct backward list */
  for (gint i = 0; i < backward->len; i++) {
    gint poc = g_array_index (backward, GstH265EncoderFrame *, i)->poc;
    g_array_append_val (positive, poc);
  }

  if (backward->len > priv->gop.num_ref_backward)
    g_array_set_size (backward, priv->gop.num_ref_backward);

  /* TODO: if MVP enabled, fail if collocated_ref_idx >= forward->len,
   * else get collocated_poc = backward[collocated_ref_idx]->poc
   */

  return TRUE;
}

static const GstH265ProfileDescriptor *
_h265_profile_descriptor (GstH265Profile profile)
{
  for (gint i = 0; i < G_N_ELEMENTS (_h265_profiles); i++) {
    if (_h265_profiles[i].profile == profile)
      return &_h265_profiles[i];
  }

  return NULL;
}

static GstH265ProfileIDC
gst_h265_encoder_get_profile_idc (GstH265Profile profile)
{
  const GstH265ProfileDescriptor *desc = _h265_profile_descriptor (profile);

  return desc ? desc->profile_idc : 0;
}

/* check if profile is for screen content coding */
static gboolean
_profile_is_scc (GstH265Profile profile)
{
  const GstH265ProfileDescriptor *desc = _h265_profile_descriptor (profile);

  if (!desc)
    return FALSE;

  return (desc->profile_idc == GST_H265_PROFILE_IDC_SCREEN_CONTENT_CODING
      || desc->profile_idc ==
      GST_H265_PROFILE_IDC_HIGH_THROUGHPUT_SCREEN_CONTENT_CODING_EXTENSION);
}

static gboolean
gst_h265_encoder_slicehdr_init (GstH265Encoder * self,
    GstH265EncoderFrame * frame, GstH265SliceHdr * slice_hdr)
{
  GstH265EncoderPrivate *priv = _GET_PRIV (self);

  /* only progressive so far */
  g_assert (priv->params.sps.separate_colour_plane_flag == 0);

  /* 7.4.7.1 General slice segment header semantics */
  /* *INDENT-OFF* */
  *slice_hdr = (GstH265SliceHdr) {
    /* TODO: multi-slices */
    .first_slice_segment_in_pic_flag = 1, /* 1 specifies that the slice segment
                                             is the first slice segment of the
                                             picture in decoding order */
    .no_output_of_prior_pics_flag = 0,    /* affects the output of
                                             previously-decoded pictures in the
                                             decoded picture buffer after the
                                             decoding of an IDR or a BLA picture
                                             that is not the first picture in
                                             the bitstream as specified in Annex
                                             C */
    .pps = &priv->params.pps,             /* specifies the value of
                                             pps_pic_parameter_set_id for the
                                             PPS in use */
    /* VA: features.dependent_slice_segment_flag
       Vulkan: VK_VIDEO_ENCODE_H265_STD_DEPENDENT_SLICE_SEGMENT_FLAG_SET_BIT_KHR  */
    .dependent_slice_segment_flag = 0,    /* 1 specifies that the value of each
                                             slice segment header syntax element
                                             that is not present is inferred to
                                             be equal to the value of the
                                             corresponding slice segment header
                                             syntax element in the slice
                                             header. */
    /* TODO: multi-slices */
    .segment_address = 0,                 /* specifies the address of the first
                                             CTB in the slice segment, in CTB
                                             raster scan of a picture */
    .type = gop_2_slice (frame->gop.type),
    /* Vulkan: different from StdVideoEncodeH265PictureInfo->StdVideoEncodeH265PictureInfoFlags  */
    .pic_output_flag = 0,                 /* affects the decoded picture output
                                             and removal processes as specified
                                             in Annex C */
    /* VA: features.colour_plane_id (forced to 0) */
    .colour_plane_id = 0,                 /* specifies the colour plane
                                             associated with the current slice
                                             RBSP when
                                             separate_colour_plane_flag is equal
                                             to 1 */
    /* = frame->poc if P || B */
    .pic_order_cnt_lsb = 0,               /* specifies the picture order count
                                             modulo MaxPicOrderCntLsb for the
                                             current picture. */
    /* VA / Vulkan: 0 */
    .short_term_ref_pic_set_sps_flag = 0, /* 0 specifies that the short-term RPS
                                             of the current picture is derived
                                             based on the st_ref_pic_set( )
                                             syntax structure that is directly
                                             included in the slice headers of
                                             the current picture. */
    /* VA / Vulkan: 0 */
    .short_term_ref_pic_set_idx = 0,      /* specifies the index, into the list
                                             of the st_ref_pic_set( ) syntax
                                             structures included in the active
                                             SPS, of the st_ref_pic_set( )
                                             syntax structure that is used for
                                             derivation of the short-term RPS of
                                             the current picture. */
    /* filled if  P || B */
    .short_term_ref_pic_sets = { 0, },    /* filled bellow */
    /* VA / Vulkan: 0 */
    .num_long_term_sps = 0,               /* specifies the number of entries in
                                             the long-term RPS of the current
                                             picture that are derived based on
                                             the candidate long-term reference
                                             pictures specified in the active
                                             SPS */
    /* VA / Vulkan: 0 */
    .num_long_term_pics = 0,              /* specifies the number of entries in
                                             the long-term RPS of the current
                                             picture that are directly signalled
                                             in the slice header.*/
    .lt_idx_sps = { 0, },                 /* specifies an index, into the list
                                             of candidate long-term reference
                                             pictures specified in the active
                                             SPS, of the i-th entry in the
                                             long-term RPS of the current
                                             picture. */
    .poc_lsb_lt = { 0, },                 /* specifies the value of the picture
                                             order count modulo
                                             MaxPicOrderCntLsb of the i-th entry
                                             in the long-term RPS of the current
                                             picture. */
    .used_by_curr_pic_lt_flag = { 0, },   /* equal to 0 specifies that the i-th
                                             entry in the long-term RPS of the
                                             current picture is not used for
                                             reference by the current
                                             picture. */
    .delta_poc_msb_present_flag = { 0, }, /* equal to 0 specifies that
                                             delta_poc_msb_cycle_lt[ i ] is not
                                             present*/
    .delta_poc_msb_cycle_lt = { 0, },     /* is used to determine the value of
                                             the most significant bits of the
                                             picture order count value of the
                                             i-th entry in the long-term RPS of
                                             the current picture */
    /* XXX: completed by subclass
       VA: enabled in features.bits.temporal_mvp
       Vulkan: VK_VIDEO_ENCODE_H265_STD_SPS_TEMPORAL_MVP_ENABLED_FLAG_SET_BIT_KHR */
    .temporal_mvp_enabled_flag = 0,       /* specifies whether temporal motion
                                             vector predictors can be used for
                                             inter prediction */
    /* XXX: completed by subclass */
    /* ffmpeg: units.raw_sps.sample_adaptive_offset_enabled_flag == VK_VIDEO_ENCODE_H265_STD_SAMPLE_ADAPTIVE_OFFSET_ENABLED_FLAG_SET_BIT_KHR */
    /* VA: features.bits.sao */
    .sao_luma_flag = 0,                   /* equal to 1 specifies that SAO is
                                             enabled for the luma component in
                                             the current slice */
    /* ffmpeg: units.raw_sps.sample_adaptive_offset_enabled_flag == VK_VIDEO_ENCODE_H265_STD_SAMPLE_ADAPTIVE_OFFSET_ENABLED_FLAG_SET_BIT_KHR */
    /* VA: features.bits.sao */
    .sao_chroma_flag = 0,                 /* equal to 1 specifies that SAO is
                                             enabled for the chroma component in
                                             the current slice */
    /* filled bellow */
    /* VA: = 1 if SCC else if P | B & list size > 0 || list size > 0 */
    /* vulkan-ffmpeg: 0  */
    .num_ref_idx_active_override_flag = 0, /* equal to 1 specifies that the
                                              syntax element
                                              num_ref_idx_l0_active_minus1 is
                                              present for P and B slices and
                                              that the syntax element
                                              num_ref_idx_l1_active_minus1 is
                                              present for B slices */
    /* vulkan-ffmpeg: units.raw_pps.num_ref_idx_l0_default_active_minus1 */
    .num_ref_idx_l0_active_minus1 = 0,    /* specifies the maximum reference
                                             index for reference picture list 0
                                             that may be used to decode the
                                             slice. num_ref_idx_l0_active_minus1
                                             shall be in the range of 0 to 14,
                                             inclusive. */
    /* vulkan-ffmpeg: units.raw_pps.num_ref_idx_l1_default_active_minus1 */
    .num_ref_idx_l1_active_minus1 = 0,    /* specifies the maximum reference
                                             index for reference picture list 1
                                             that may be used to decode the
                                             slice. num_ref_idx_l1_active_minus1
                                             shall be in the range of 0 to 14 */
    /* VA & vulkan-ffmpeg: 0 */
    .ref_pic_list_modification = { 0, },   /* TBD */
    .mvd_l1_zero_flag = 0,                 /* equal to 1 indicates that the
                                              mvd_coding( x0, y0, 1 ) syntax
                                              structure is not parsed and MvdL1[
                                              x0 ][ y0 ][ compIdx ] is set equal
                                              to 0 for compIdx = 0..1 */
    /* VA & vulkan-ffmpeg: 0 */
    .cabac_init_flag = 0,                  /* specifies the method for
                                              determining the initialization
                                              table used in the initialization
                                              process for context variables */
    /* XXX: completed by subclass */
    /* vulkan-ffmpeg: 1  */
    /* va: 1 if I else 1 if mvp enabled */
    .collocated_from_l0_flag = 0,          /* equal to 1 specifies that the
                                              collocated picture used for
                                              temporal motion vector prediction
                                              is derived from reference picture
                                              list 0. */
    /* va: 0xff if I else 0xff if not mvp enabled */
    /* vulkan-ffmpeg: 0 */
    .collocated_ref_idx = 0,                /* specifies the reference index of
                                               the collocated picture used for
                                               temporal motion vector
                                               prediction. */
    /* vulkan: VK_VIDEO_ENCODE_H265_CAPABILITY_PREDICTION_WEIGHT_TABLE_GENERATED_BIT_KHR */
    /* va & vulkna-ffmpeg : 0 */
    .pred_weight_table = { 0, },            /* TBD */
    /* va: 5 - slice_param->max_num_merge_cand (5) = 0  */
    .five_minus_max_num_merge_cand = 0,     /* specifies the maximum number of
                                               merging motion vector prediction
                                               (MVP) candidates supported in the
                                               slice subtracted from 5 */
    /* va: 0 */
    .use_integer_mv_flag = 0,               /* equal to 1 specifies that the
                                               resolution of motion vectors for
                                               inter prediction in the current
                                               slice is integer. */
    /* XXX: completed by subclass */
    /* va: CPQ => if P qp_p - qp_i; if B qp_b - qp_i */
    /* vulkan-ffmpgeg: vkslice.constantQp - (enc->units.raw_pps.init_qp_minus26 + 26) */
    .qp_delta = 0,                          /* specifies the initial value of
                                               QpY to be used for the coding
                                               blocks in the slice until
                                               modified by the value of
                                               CuQpDeltaVal in the coding unit
                                               layer */
    /* vulkan-ffmpeg && va: 0 */
    .cb_qp_offset = 0,                      /* specifies a difference to be
                                               added to the value of
                                               pps_cb_qp_offset when determining
                                               the value of the Qp′Cb
                                               quantization parameter. */
    /* vulkan-ffmpeg & va : 0 */
    .cr_qp_offset = 0,                      /* specifies a difference to be
                                               added to the value of
                                               pps_cr_qp_offset when determining
                                               the value of the Qp′Cr
                                               quantization parameter */
    /* vulkan-ffmpeg & va: 0  */
    .slice_act_y_qp_offset = 0,             /* specify offsets to the
                                               quantization parameter values qP
                                               derived in clause 8.6.2 for luma,
                                               Cb, and Cr components,
                                               respectively */
    /* vulkan-ffmpeg & va: 0  */
    .slice_act_cb_qp_offset = 0,
    /* vulkan-ffmpeg & va: 0  */
    .slice_act_cr_qp_offset = 0,
    /* vulkan-ffmpeg & va: 0  */
    .cu_chroma_qp_offset_enabled_flag = 0,  /* equal to 1 specifies that the
                                               cu_chroma_qp_offset_flag may be
                                               present in the transform unit
                                               syntax */
    /* vulkan-ffmpeg & va: 0  */
    .deblocking_filter_override_flag = 0,   /* equal to 1 specifies that
                                               deblocking parameters are present
                                               in the slice header */
    /* vulkan-ffmpeg & va: 0  */
    .deblocking_filter_disabled_flag = 0,   /* equal to 1 specifies that the
                                               operation of the deblocking
                                               filter is not applied for the
                                               current slice */
    /* vulkan-ffmpeg & va: 0  */
    .beta_offset_div2 = 0,                  /* specify the deblocking parameter
                                               offsets for β and tC (divided by
                                               2) for the current slice */
    /* vulkan-ffmpeg & va: 0  */
    .tc_offset_div2 = 0,
    /* va: 1 */
    /* vulkan-ffmpeg: 0 */
    .loop_filter_across_slices_enabled_flag = 0, /* equal to 1 specifies that
                                                    in-loop filtering operations
                                                    may be performed across the
                                                    left and upper boundaries of
                                                    the current slice */
    /* va: 0  */
    .num_entry_point_offsets = 0,           /* specifies the number of
                                               entry_point_offset_minus1[ i ]
                                               syntax elements in the slice
                                               header. */
    /* va: 0  */
    .offset_len_minus1 = 0,                 /* plus 1 specifies the length, in
                                               bits, of the
                                               entry_point_offset_minus1[ i ]
                                               syntax elements. */
    /* va: 0  */
    .entry_point_offset_minus1 = NULL,      /* plus 1 specifies the i-th entry
                                               point offset in bytes, and is
                                               represented by offset_len_minus1
                                               plus 1 bits */
    /* calculated values */
    .NumPocTotalCurr = 0,
    /* Size of the slice_header() in bits */
    .header_size = 0,
    /* Number of emulation prevention bytes (EPB) in this slice_header() */
    .n_emulation_prevention_bytes = 0,
    /* Size of short_term_ref_pic_set() after emulation preventation bytes are
       removed, in bits */
    .short_term_ref_pic_set_size = 0,
    /* The calculated size of the branch `if( long_term_ref_pics_present_flag )`
       inside `slice_segment_header()` syntax in bits. */
    .long_term_ref_pic_set_size = 0,
    .pps_id = priv->params.pps.id,
  };
  /* *INDENT-ON* */

  /* this is overrode by vah265enc */
  /* if (slice_hdr->dependent_slice_segment_flag) */
  /*   return TRUE; */

  /* H.265 8.3.1: pic_order_cnt_lsb = PicOrderCntVal % MaxPicOrderCntLsb */
  slice_hdr->pic_order_cnt_lsb = frame->poc &
      ((1 << (priv->params.sps.log2_max_pic_order_cnt_lsb_minus4 + 4)) - 1);

  /* IDR pictures have no reference pictures, but non-IDR I frames still
   * signal their short-term RPS, keeping the earlier pictures marked as
   * references for the following inter frames (H.265 8.3.2). */
  if (GST_H26X_GOP_IS_IDR (&frame->gop))
    return TRUE;

  /* Processing P || B || non-IDR I frames */

  GArray *positive = priv->list_positive;
  GArray *negative = priv->list_negative;

  if (!positive || !negative) {
    GST_ERROR_OBJECT (self, "Lists of negative or positive pictures are NULL");
    return FALSE;
  }

  /* Write reference set explicitly */
  /* *INDENT-OFF* */
  slice_hdr->short_term_ref_pic_sets = (GstH265ShortTermRefPicSet) {
    .inter_ref_pic_set_prediction_flag = 0,
    .NumDeltaPocs = positive->len + negative->len,
    .NumNegativePics = negative->len,
    .NumPositivePics = positive->len,
  };
  /* *INDENT-ON* */

  GArray *forward = priv->list_forward;
  GArray *backward = priv->list_backward;

  if (!forward || !backward) {
    GST_ERROR_OBJECT (self, "Forward or backward lists are NULL");
    return FALSE;
  }

  for (gint i = 0; i < negative->len; i++) {
    gint delta_poc = g_array_index (negative, gint, i) - frame->poc;
    g_assert (delta_poc < 0);

    slice_hdr->short_term_ref_pic_sets.DeltaPocS0[i] = delta_poc;
    slice_hdr->short_term_ref_pic_sets.UsedByCurrPicS0[i] = (i < forward->len);
  }

  for (gint i = 0; i < positive->len; i++) {
    gint delta_poc = g_array_index (positive, gint, i) - frame->poc;
    g_assert (delta_poc > 0);

    slice_hdr->short_term_ref_pic_sets.DeltaPocS1[i] = delta_poc;
    slice_hdr->short_term_ref_pic_sets.UsedByCurrPicS1[i] = (i < backward->len);
  }

  /* num_ref_idx_active_override_flag and num_ref_idx_lX_active_minus1 are
   * only present in the slice header syntax for P and B slices (H.265
   * 7.3.6.1). */
  if (GST_H26X_GOP_IS (&frame->gop, P) || GST_H26X_GOP_IS (&frame->gop, B)) {
    if (_profile_is_scc (priv->stream.profile)) {
      slice_hdr->num_ref_idx_active_override_flag = 1;
    } else {
      slice_hdr->num_ref_idx_active_override_flag =
          (forward->len > 0 || backward->len > 0);
    }
    /* H.265 limits num_ref_idx_l0/l1_active_minus1 to 0..14 */
    slice_hdr->num_ref_idx_l0_active_minus1 =
        forward->len > 0 ? MIN (forward->len - 1, 14) : 0;

    if (GST_H26X_GOP_IS (&frame->gop, B)) {
      slice_hdr->num_ref_idx_l1_active_minus1 =
          backward->len > 0 ? MIN (backward->len - 1, 14) : 0;
    }
  }

  return TRUE;
}

static GstVideoCodecFrame *
gst_h265_encoder_find_unused_reference_frame (GstH265Encoder * self,
    GstH265EncoderFrame * h265_frame)
{
  GstH265EncoderPrivate *priv = _GET_PRIV (self);
  GstH265EncoderFrame *b_h265_frame;
  GstVideoCodecFrame *b_frame;
  guint i;

  /* We still have more space. */
  if (g_queue_get_length (&priv->ref_list) <
      (priv->gop.max_dec_pic_buffering - 1))
    return NULL;

  /* Not b_pyramid, sliding window is enough */
  if (!priv->gop.params.b_pyramid)
    return g_queue_peek_head (&priv->ref_list);

  /* Non-B frame, just using sliding window */
  if (!GST_H26X_GOP_IS (&h265_frame->gop, B))
    return g_queue_peek_head (&priv->ref_list);

  /* Choose the B frame with lowest POC */
  b_frame = NULL;
  b_h265_frame = NULL;
  for (i = 0; i < g_queue_get_length (&priv->ref_list); i++) {
    GstH265EncoderFrame *h265frame;
    GstVideoCodecFrame *frame;

    frame = g_queue_peek_nth (&priv->ref_list, i);
    h265frame = _GET_FRAME (frame);
    if (!GST_H26X_GOP_IS (&h265frame->gop, B))
      continue;

    if (!b_frame) {
      b_frame = frame;
      b_h265_frame = _GET_FRAME (b_frame);
      continue;
    }

    g_assert (h265frame->poc != b_h265_frame->poc);
    if (h265frame->poc < b_h265_frame->poc) {
      b_frame = frame;
      b_h265_frame = _GET_FRAME (b_frame);
    }
  }

  /* No B frame as reference */
  if (!b_frame)
    return g_queue_peek_head (&priv->ref_list);

  if (b_frame != g_queue_peek_head (&priv->ref_list)) {
    b_h265_frame = _GET_FRAME (b_frame);
    GST_LOG_OBJECT (self, "Frame with POC: %d will be replaced by frame with "
        "POC: %d, explicitly", b_h265_frame->poc, h265_frame->poc);
  }

  return b_frame;
}

static GstFlowReturn
gst_h265_encoder_encode_frame_with_ref_lists (GstH265Encoder * self,
    GstVideoCodecFrame * frame)
{
  GstH265EncoderClass *klass = GST_H265_ENCODER_GET_CLASS (self);
  GstH265EncoderPrivate *priv = _GET_PRIV (self);
  GstH265EncoderFrame *h265_frame;
  GArray *forward, *backward;
  GstH265SliceHdr slice_hdr;

  g_return_val_if_fail (frame != NULL, GST_FLOW_ERROR);

  h265_frame = _GET_FRAME (frame);
  if (!h265_frame) {
    GST_ERROR_OBJECT (self, "frame without user data");
    return GST_FLOW_ERROR;
  }

  if (!gst_h265_encoder_construct_reference_lists (self, h265_frame))
    return GST_FLOW_ERROR;

  if (!gst_h265_encoder_slicehdr_init (self, h265_frame, &slice_hdr))
    return GST_FLOW_ERROR;

  forward = priv->list_forward;
  backward = priv->list_backward;

  g_assert (klass->encode_frame);
  return klass->encode_frame (self, frame, h265_frame, &slice_hdr, forward,
      backward);
}

static gint
_sort_by_poc (gconstpointer a, gconstpointer b, gpointer user_data)
{
  GstH265EncoderFrame *frame1 = _GET_FRAME ((GstVideoCodecFrame *) a);
  GstH265EncoderFrame *frame2 = _GET_FRAME ((GstVideoCodecFrame *) b);

  /* if pocs are equal something terrible wrong happened before */
  g_assert (frame1->poc != frame2->poc);
  return frame1->poc - frame2->poc;
}

static GstFlowReturn
gst_h265_encoder_encode_frame (GstH265Encoder * self,
    GstVideoCodecFrame * frame, gboolean is_last)
{
  GstH265EncoderPrivate *priv = _GET_PRIV (self);
  GstH265EncoderFrame *h265_frame;
  GstVideoCodecFrame *unused_ref = NULL;
  GstFlowReturn ret;

  h265_frame = _GET_FRAME (frame);
  h265_frame->last_frame = is_last;

  ret = gst_h265_encoder_encode_frame_with_ref_lists (self, frame);
  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (self, "Failed to encode the frame: %s",
        gst_flow_get_name (ret));
    return ret;
  }

  g_queue_push_tail (&priv->output_list, gst_video_codec_frame_ref (frame));

  if (h265_frame->gop.is_ref) {
    unused_ref =
        gst_h265_encoder_find_unused_reference_frame (self, h265_frame);

    if (unused_ref) {
      if (!g_queue_remove (&priv->ref_list, unused_ref))
        g_assert_not_reached ();

      gst_video_codec_frame_unref (unused_ref);
    }

    /* Add it into the reference list. */
    g_queue_insert_sorted (&priv->ref_list, gst_video_codec_frame_ref (frame),
        _sort_by_poc, NULL);

    g_assert (g_queue_get_length (&priv->ref_list) <
        priv->gop.max_dec_pic_buffering);
  }

  return ret;
}

static GstFlowReturn
gst_h265_encoder_finish_last_frame (GstH265Encoder * self)
{
  GstH265EncoderPrivate *priv = _GET_PRIV (self);
  GstVideoCodecFrame *frame;
  GstFlowReturn ret;
  guint32 system_frame_number;

  if (g_queue_is_empty (&priv->output_list))
    return GST_FLOW_OUTPUT_NOT_READY;

  frame = g_queue_pop_head (&priv->output_list);
  system_frame_number = frame->system_frame_number;
  gst_video_codec_frame_unref (frame);

  ret = gst_h265_encoder_finish_frame (self, frame);

  if (ret != GST_FLOW_OK) {
    GST_INFO_OBJECT (self, "failed to push one buffer, system_frame_number "
        "%d: %s", system_frame_number, gst_flow_get_name (ret));
  }

  return ret;
}

static GstFlowReturn
gst_h265_encoder_drain (GstH265Encoder * self)
{
  GstVideoEncoder *encoder = GST_VIDEO_ENCODER (self);
  GstH265EncoderPrivate *priv = _GET_PRIV (self);
  GstFlowReturn ret = GST_FLOW_OK;
  GstVideoCodecFrame *frame = NULL;
  gboolean is_last;

  GST_DEBUG_OBJECT (self, "Encoder is draining");

  /* Kickout all cached frames */
  if (!gst_h265_encoder_reorder_frame (self, NULL, TRUE, &frame)) {
    ret = GST_FLOW_ERROR;
    goto error_and_purge_all;
  }

  while (frame) {
    is_last = g_queue_is_empty (&priv->reorder_list);
    ret = gst_h265_encoder_encode_frame (self, frame, is_last);
    if (ret != GST_FLOW_OK)
      goto error_and_purge_all;

    frame = NULL;

    ret = gst_h265_encoder_finish_last_frame (self);
    if (ret != GST_FLOW_OK)
      goto error_and_purge_all;

    if (!gst_h265_encoder_reorder_frame (self, NULL, TRUE, &frame)) {
      ret = GST_FLOW_ERROR;
      goto error_and_purge_all;
    }
  }

  g_assert (g_queue_is_empty (&priv->reorder_list));

  /* Output all frames. */
  while (!g_queue_is_empty (&priv->output_list)) {
    ret = gst_h265_encoder_finish_last_frame (self);
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
  GST_CHROMA_400 = 0,
  GST_CHROMA_420 = 1,
  GST_CHROMA_422 = 2,
  GST_CHROMA_444 = 3,
  GST_CHROMA_INVALID = 0xFF,
};

static inline guint8
_chroma_idc_from_finfo (const GstVideoFormatInfo * finfo)
{
  gint w_sub, h_sub;

  /* XXX: GRAY isn't supported */
  if (!GST_VIDEO_FORMAT_INFO_IS_YUV (finfo))
    return GST_CHROMA_INVALID;

  w_sub = 1 << GST_VIDEO_FORMAT_INFO_W_SUB (finfo, 1);
  h_sub = 1 << GST_VIDEO_FORMAT_INFO_H_SUB (finfo, 1);

  if (w_sub == 2 && h_sub == 2)
    return GST_CHROMA_420;
  if (w_sub == 2 && h_sub == 1)
    return GST_CHROMA_422;
  if (w_sub == 1 && h_sub == 1)
    return GST_CHROMA_444;

  return GST_CHROMA_INVALID;
}

static guint8
_h265_get_chroma_idc (GstVideoInfo * info)
{
  return _chroma_idc_from_finfo (info->finfo);
}

static guint8
_h265_get_level_idc (const gchar * level)
{
  if (!level)
    return 0;

  for (int i = 0; i < G_N_ELEMENTS (_h265_levels); i++) {
    if (strcmp (level, _h265_levels[i].name) == 0)
      return _h265_levels[i].level_idc;
  }

  return 0;
}

/* *INDENT-OFF* */
/* TODO: merge with _h265_profiles */
static const struct {
  GstH265Profile profile;
  guint8 max_chroma;
  guint8 max_bit_depth;
} profile_chroma_map[] = {
  { GST_H265_PROFILE_MAIN,                                   GST_CHROMA_420, 8  },
  { GST_H265_PROFILE_MAIN_10,                                GST_CHROMA_420, 10 },
  { GST_H265_PROFILE_MAIN_STILL_PICTURE,                     GST_CHROMA_420, 8  },
  { GST_H265_PROFILE_MONOCHROME,                             GST_CHROMA_400, 8  },
  { GST_H265_PROFILE_MONOCHROME_12,                          GST_CHROMA_400, 12 },
  { GST_H265_PROFILE_MONOCHROME_16,                          GST_CHROMA_400, 16 },
  { GST_H265_PROFILE_MAIN_12,                                GST_CHROMA_420, 12 },
  { GST_H265_PROFILE_MAIN_422_10,                            GST_CHROMA_422, 10 },
  { GST_H265_PROFILE_MAIN_422_12,                            GST_CHROMA_422, 12 },
  { GST_H265_PROFILE_MAIN_444,                               GST_CHROMA_444, 8  },
  { GST_H265_PROFILE_MAIN_444_10,                            GST_CHROMA_444, 10 },
  { GST_H265_PROFILE_MAIN_444_12,                            GST_CHROMA_444, 12 },
  { GST_H265_PROFILE_MAIN_INTRA,                             GST_CHROMA_420, 8  },
  { GST_H265_PROFILE_MAIN_10_INTRA,                          GST_CHROMA_420, 10 },
  { GST_H265_PROFILE_MAIN_12_INTRA,                          GST_CHROMA_420, 12 },
  { GST_H265_PROFILE_MAIN_422_10_INTRA,                      GST_CHROMA_422, 10 },
  { GST_H265_PROFILE_MAIN_422_12_INTRA,                      GST_CHROMA_422, 12 },
  { GST_H265_PROFILE_MAIN_444_INTRA,                         GST_CHROMA_444, 8  },
  { GST_H265_PROFILE_MAIN_444_10_INTRA,                      GST_CHROMA_444, 10 },
  { GST_H265_PROFILE_MAIN_444_12_INTRA,                      GST_CHROMA_444, 12 },
  { GST_H265_PROFILE_MAIN_444_16_INTRA,                      GST_CHROMA_444, 16 },
  { GST_H265_PROFILE_MAIN_444_STILL_PICTURE,                 GST_CHROMA_444, 8  },
  { GST_H265_PROFILE_MAIN_444_16_STILL_PICTURE,              GST_CHROMA_444, 16 },
  { GST_H265_PROFILE_MONOCHROME_10,                          GST_CHROMA_400, 10 },
  { GST_H265_PROFILE_HIGH_THROUGHPUT_444,                    GST_CHROMA_444, 8  },
  { GST_H265_PROFILE_HIGH_THROUGHPUT_444_10,                 GST_CHROMA_444, 10 },
  { GST_H265_PROFILE_HIGH_THROUGHPUT_444_14,                 GST_CHROMA_444, 14 },
  { GST_H265_PROFILE_HIGH_THROUGHPUT_444_16_INTRA,           GST_CHROMA_444, 16 },
  { GST_H265_PROFILE_SCREEN_EXTENDED_MAIN,                   GST_CHROMA_420, 8  },
  { GST_H265_PROFILE_SCREEN_EXTENDED_MAIN_10,                GST_CHROMA_420, 10 },
  { GST_H265_PROFILE_SCREEN_EXTENDED_MAIN_444,               GST_CHROMA_444, 8  },
  { GST_H265_PROFILE_SCREEN_EXTENDED_MAIN_444_10,            GST_CHROMA_444, 10 },
  { GST_H265_PROFILE_SCREEN_EXTENDED_HIGH_THROUGHPUT_444,    GST_CHROMA_444, 8  },
  { GST_H265_PROFILE_SCREEN_EXTENDED_HIGH_THROUGHPUT_444_10, GST_CHROMA_444, 10 },
  { GST_H265_PROFILE_SCREEN_EXTENDED_HIGH_THROUGHPUT_444_14, GST_CHROMA_444, 14 },
  /* { GST_H265_PROFILE_MULTIVIEW_MAIN,                         GST_CHROMA_420, 8  }, */
  /* { GST_H265_PROFILE_SCALABLE_MAIN,                          GST_CHROMA_420, 8  }, */
  /* { GST_H265_PROFILE_SCALABLE_MAIN_10,                       GST_CHROMA_420, 10 }, */
  /* { GST_H265_PROFILE_SCALABLE_MONOCHROME,                    GST_CHROMA_400, 8  }, */
  /* { GST_H265_PROFILE_SCALABLE_MONOCHROME_12,                 GST_CHROMA_400, 12 }, */
  /* { GST_H265_PROFILE_SCALABLE_MONOCHROME_16,                 GST_CHROMA_400, 16 }, */
  /* { GST_H265_PROFILE_SCALABLE_MAIN_444,                      GST_CHROMA_444, 8  }, */
  /* { GST_H265_PROFILE_3D_MAIN,                                GST_CHROMA_420, 8  }, */
};
/* *INDENT-ON* */

static gboolean
_h265_profile_max_chroma (GstH265Profile profile, guint8 * max_chroma,
    guint8 * max_bit_depth)
{
  /* TODO: these data might be extracted from _265_profiles with profile tier
   * levels, rather this custom table */
  for (int i = 0; i < G_N_ELEMENTS (profile_chroma_map); i++) {
    if (profile == profile_chroma_map[i].profile) {
      *max_chroma = profile_chroma_map[i].max_chroma;
      *max_bit_depth = profile_chroma_map[i].max_bit_depth;
      return TRUE;
    }
  }

  return FALSE;
}

static GstH265Tier
_h265_get_tier (const char *tier)
{
  if (g_strcmp0 (tier, "main") == 0)
    return GST_H265_TIER_MAIN;
  if (g_strcmp0 (tier, "high") == 0)
    return GST_H265_TIER_HIGH;
  return GST_H265_TIER_INVALID;
}

struct ProfileCandidate
{
  char *profile_name;
  GstH265Profile profile;
  guint level;
  GstH265Tier tier;
};

static gboolean
_fill_profile_candidate (const GValue * profile, const GValue * level,
    const GValue * tier, struct ProfileCandidate *candidate)
{
  const char *profile_name = g_value_get_string (profile);
  candidate->profile = gst_h265_profile_from_string (profile_name);
  if (candidate->profile == GST_H265_PROFILE_INVALID)
    return FALSE;
  candidate->tier = tier && G_VALUE_HOLDS_STRING (tier) ?
      _h265_get_tier (g_value_get_string (tier)) : GST_H265_TIER_AUTO;
  if (candidate->tier == GST_H265_TIER_INVALID)
    return FALSE;

  candidate->level = level && G_VALUE_HOLDS_STRING (level) ?
      _h265_get_level_idc (g_value_get_string (level)) : 0;
  candidate->profile_name = g_strdup (profile_name);
  return TRUE;
}

static GstFlowReturn
gst_h265_encoder_negotiate_default (GstH265Encoder * self,
    GstVideoCodecState * in_state, GstH265Profile * profile,
    GstH265Level * level, GstH265Tier * tier)
{
  GstCaps *allowed_caps;
  guint i, num_structures, num_candidates = 0;
  guint8 chroma, bit_depth_luma;
  struct ProfileCandidate candidates[50] =
      { {NULL, GST_H265_PROFILE_INVALID, 0}, };

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
        *level = gst_structure_get_value (structure, "level"),
        *tier = gst_structure_get_value (structure, "tier");
    struct ProfileCandidate *candidate;

    if (!profiles)
      continue;

    if (G_VALUE_HOLDS_STRING (profiles)) {
      candidate = &candidates[num_candidates];
      if (_fill_profile_candidate (profiles, level, tier, candidate))
        num_candidates++;
    } else if (GST_VALUE_HOLDS_LIST (profiles)) {
      for (guint j = 0; j < gst_value_list_get_size (profiles); j++) {
        const GValue *profile = gst_value_list_get_value (profiles, j);

        candidate = &candidates[num_candidates];
        if (_fill_profile_candidate (profile, level, tier, candidate))
          num_candidates++;
        /* inner loop check */
        if (num_candidates == G_N_ELEMENTS (candidates))
          break;
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

  chroma = _h265_get_chroma_idc (&in_state->info);
  if (chroma == GST_CHROMA_INVALID) {
    GST_ERROR_OBJECT (self, "Source caps with invalid chroma");
    return GST_FLOW_NOT_NEGOTIATED;
  }
  bit_depth_luma = GST_VIDEO_INFO_COMP_DEPTH (&in_state->info, 0);

  /* let's just pick the best one according to the input */
  for (i = 0; i < num_candidates; i++) {
    struct ProfileCandidate *candidate = &candidates[i];
    guint8 max_chroma, max_bit_depth;

    if (candidate->profile < *profile)
      continue;

    if (!_h265_profile_max_chroma (candidate->profile, &max_chroma,
            &max_bit_depth)) {
      GST_INFO_OBJECT (self, "Profile %s is unsupported",
          candidate->profile_name);
      continue;
    }

    if (max_chroma < chroma || max_bit_depth < bit_depth_luma) {
      GST_INFO_OBJECT (self, "Profile %s doesn't support sink's chroma or bit "
          "depth", candidate->profile_name);
      continue;
    }

    *profile = candidate->profile;
    *level = candidate->level;
    *tier = candidate->tier;
    break;
  }

  for (i = 0; i < num_candidates; i++)
    g_free (candidates[i].profile_name);

  if (*profile == GST_H265_PROFILE_INVALID) {
    GST_ERROR_OBJECT (self, "No valid profile found");
    return GST_FLOW_NOT_NEGOTIATED;
  }

  return GST_FLOW_OK;
}

#ifndef GST_DISABLE_GST_DEBUG
#define SPS_MEMBERS(F) \
  F(id)                                                                 \
  F(vps_id)                                                             \
  F(max_sub_layers_minus1)                                              \
  F(temporal_id_nesting_flag)                                           \
  F(profile_tier_level.profile_space)                                   \
  F(profile_tier_level.tier_flag)                                       \
  F(profile_tier_level.profile_idc)                                     \
  F(profile_tier_level.progressive_source_flag)                         \
  F(profile_tier_level.interlaced_source_flag)                          \
  F(profile_tier_level.non_packed_constraint_flag)                      \
  F(profile_tier_level.frame_only_constraint_flag)                      \
  F(profile_tier_level.max_12bit_constraint_flag)                       \
  F(profile_tier_level.max_10bit_constraint_flag)                       \
  F(profile_tier_level.max_8bit_constraint_flag)                        \
  F(profile_tier_level.max_422chroma_constraint_flag)                   \
  F(profile_tier_level.max_420chroma_constraint_flag)                   \
  F(profile_tier_level.max_monochrome_constraint_flag)                  \
  F(profile_tier_level.intra_constraint_flag)                           \
  F(profile_tier_level.one_picture_only_constraint_flag)                \
  F(profile_tier_level.lower_bit_rate_constraint_flag)                  \
  F(profile_tier_level.max_14bit_constraint_flag)                       \
  F(profile_tier_level.level_idc)                                       \
  F(chroma_format_idc)                                                  \
  F(separate_colour_plane_flag)                                         \
  F(pic_width_in_luma_samples)                                          \
  F(pic_height_in_luma_samples)                                         \
  F(conformance_window_flag)                                            \
  F(conf_win_left_offset)                                               \
  F(conf_win_right_offset)                                              \
  F(conf_win_top_offset)                                                \
  F(conf_win_bottom_offset)                                             \
  F(bit_depth_luma_minus8)                                              \
  F(bit_depth_chroma_minus8)                                            \
  F(log2_max_pic_order_cnt_lsb_minus4)                                  \
  F(sub_layer_ordering_info_present_flag)                               \
  F(log2_min_luma_coding_block_size_minus3)                             \
  F(log2_diff_max_min_luma_coding_block_size)                           \
  F(log2_min_transform_block_size_minus2)                               \
  F(log2_diff_max_min_transform_block_size)                             \
  F(max_transform_hierarchy_depth_inter)                                \
  F(max_transform_hierarchy_depth_intra)                                \
  F(scaling_list_enabled_flag)                                          \
  F(scaling_list_data_present_flag)                                     \
  F(amp_enabled_flag)                                                   \
  F(sample_adaptive_offset_enabled_flag)                                \
  F(pcm_enabled_flag)                                                   \
  F(pcm_sample_bit_depth_luma_minus1)                                   \
  F(pcm_sample_bit_depth_chroma_minus1)                                 \
  F(log2_min_pcm_luma_coding_block_size_minus3)                         \
  F(log2_diff_max_min_pcm_luma_coding_block_size)                       \
  F(pcm_loop_filter_disabled_flag)                                      \
  F(num_short_term_ref_pic_sets)                                        \
  F(long_term_ref_pics_present_flag)                                    \
  F(num_long_term_ref_pics_sps)                                         \
  F(temporal_mvp_enabled_flag)                                          \
  F(strong_intra_smoothing_enabled_flag)                                \
  F(vui_parameters_present_flag)                                        \
  F(vui_params.aspect_ratio_info_present_flag)                          \
  F(vui_params.aspect_ratio_idc)                                        \
  F(vui_params.sar_width)                                               \
  F(vui_params.sar_height)                                              \
  F(vui_params.overscan_info_present_flag)                              \
  F(vui_params.overscan_appropriate_flag)                               \
  F(vui_params.video_signal_type_present_flag)                          \
  F(vui_params.video_format)                                            \
  F(vui_params.video_full_range_flag)                                   \
  F(vui_params.colour_description_present_flag)                         \
  F(vui_params.colour_primaries)                                        \
  F(vui_params.transfer_characteristics)                                \
  F(vui_params.matrix_coefficients)                                     \
  F(vui_params.chroma_loc_info_present_flag)                            \
  F(vui_params.chroma_sample_loc_type_top_field)                        \
  F(vui_params.chroma_sample_loc_type_bottom_field)                     \
  F(vui_params.neutral_chroma_indication_flag)                          \
  F(vui_params.field_seq_flag)                                          \
  F(vui_params.frame_field_info_present_flag)                           \
  F(vui_params.default_display_window_flag)                             \
  F(vui_params.def_disp_win_left_offset)                                \
  F(vui_params.def_disp_win_right_offset)                               \
  F(vui_params.def_disp_win_top_offset)                                 \
  F(vui_params.def_disp_win_bottom_offset)                              \
  F(vui_params.timing_info_present_flag)                                \
  F(vui_params.num_units_in_tick)                                       \
  F(vui_params.time_scale)                                              \
  F(vui_params.poc_proportional_to_timing_flag)                         \
  F(vui_params.num_ticks_poc_diff_one_minus1)                           \
  F(vui_params.hrd_parameters_present_flag)                             \
  F(vui_params.hrd_params.nal_hrd_parameters_present_flag)              \
  F(vui_params.hrd_params.vcl_hrd_parameters_present_flag)              \
  F(vui_params.hrd_params.sub_pic_hrd_params_present_flag)              \
  F(vui_params.hrd_params.tick_divisor_minus2)                          \
  F(vui_params.hrd_params.du_cpb_removal_delay_increment_length_minus1) \
  F(vui_params.hrd_params.dpb_output_delay_du_length_minus1)            \
  F(vui_params.hrd_params.bit_rate_scale)                               \
  F(vui_params.hrd_params.cpb_size_scale)                               \
  F(vui_params.hrd_params.cpb_size_du_scale)                            \
  F(vui_params.hrd_params.initial_cpb_removal_delay_length_minus1)      \
  F(vui_params.hrd_params.au_cpb_removal_delay_length_minus1)           \
  F(vui_params.hrd_params.dpb_output_delay_length_minus1)               \
  F(vui_params.bitstream_restriction_flag)                              \
  F(vui_params.tiles_fixed_structure_flag)                              \
  F(vui_params.motion_vectors_over_pic_boundaries_flag)                 \
  F(vui_params.restricted_ref_pic_lists_flag)                           \
  F(vui_params.min_spatial_segmentation_idc)                            \
  F(vui_params.max_bytes_per_pic_denom)                                 \
  F(vui_params.max_bits_per_min_cu_denom)                               \
  F(vui_params.log2_max_mv_length_horizontal)                           \
  F(vui_params.log2_max_mv_length_vertical)                             \
  F(sps_extension_flag)                                                 \
  F(sps_range_extension_flag)                                           \
  F(sps_multilayer_extension_flag)                                      \
  F(sps_3d_extension_flag)                                              \
  F(sps_scc_extension_flag)                                             \
  F(sps_extension_4bits)
#endif

static void
gst_h265_sps_dump (GstH265Encoder * self, GstH265SPS * sps)
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
  F(sps_id)                                     \
  F(dependent_slice_segments_enabled_flag)      \
  F(output_flag_present_flag)                   \
  F(num_extra_slice_header_bits)                \
  F(sign_data_hiding_enabled_flag)              \
  F(cabac_init_present_flag)                    \
  F(num_ref_idx_l0_default_active_minus1)       \
  F(num_ref_idx_l1_default_active_minus1)       \
  F(init_qp_minus26)                            \
  F(constrained_intra_pred_flag)                \
  F(transform_skip_enabled_flag)                \
  F(cu_qp_delta_enabled_flag)                   \
  F(diff_cu_qp_delta_depth)                     \
  F(cb_qp_offset)                               \
  F(cr_qp_offset)                               \
  F(slice_chroma_qp_offsets_present_flag)       \
  F(weighted_pred_flag)                         \
  F(weighted_bipred_flag)                       \
  F(transquant_bypass_enabled_flag)             \
  F(tiles_enabled_flag)                         \
  F(entropy_coding_sync_enabled_flag)           \
  F(num_tile_columns_minus1)                    \
  F(num_tile_rows_minus1)                       \
  F(uniform_spacing_flag)                       \
  F(loop_filter_across_tiles_enabled_flag)      \
  F(loop_filter_across_slices_enabled_flag)     \
  F(deblocking_filter_control_present_flag)     \
  F(deblocking_filter_override_enabled_flag)    \
  F(deblocking_filter_disabled_flag)            \
  F(beta_offset_div2)                           \
  F(tc_offset_div2)                             \
  F(scaling_list_data_present_flag)             \
  F(lists_modification_present_flag)            \
  F(log2_parallel_merge_level_minus2)           \
  F(slice_segment_header_extension_present_flag) \
  F(pps_extension_flag)                          \
  F(pps_range_extension_flag)                    \
  F(pps_multilayer_extension_flag)               \
  F(pps_3d_extension_flag)                       \
  F(pps_scc_extension_flag)                      \
  F(pps_extension_4bits)                         \
  F(pps_extension_params.log2_max_transform_skip_block_size_minus2) \
  F(pps_extension_params.cross_component_prediction_enabled_flag)   \
  F(pps_extension_params.chroma_qp_offset_list_enabled_flag)        \
  F(pps_extension_params.diff_cu_chroma_qp_offset_depth)            \
  F(pps_extension_params.chroma_qp_offset_list_len_minus1)          \
  F(pps_extension_params.log2_sao_offset_scale_luma)                \
  F(pps_extension_params.log2_sao_offset_scale_chroma)              \
  F(pps_scc_extension_params.pps_curr_pic_ref_enabled_flag)         \
  F(pps_scc_extension_params.residual_adaptive_colour_transform_enabled_flag) \
  F(pps_scc_extension_params.pps_slice_act_qp_offsets_present_flag) \
  F(pps_scc_extension_params.pps_act_y_qp_offset_plus5)             \
  F(pps_scc_extension_params.pps_act_cb_qp_offset_plus5)            \
  F(pps_scc_extension_params.pps_act_cr_qp_offset_plus3)            \
  F(pps_scc_extension_params.pps_palette_predictor_initializers_present_flag) \
  F(pps_scc_extension_params.pps_num_palette_predictor_initializer) \
  F(pps_scc_extension_params.monochrome_palette_flag)               \
  F(pps_scc_extension_params.luma_bit_depth_entry_minus8)           \
  F(pps_scc_extension_params.chroma_bit_depth_entry_minus8)
#endif

static void
gst_h265_pps_dump (GstH265Encoder * self, GstH265PPS * pps)
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

#ifndef GST_DISABLE_GST_DEBUG
#define VPS_MEMBERS(F) \
  F(id)                                       \
  F(base_layer_internal_flag)                 \
  F(base_layer_available_flag)                \
  F(max_layers_minus1)                        \
  F(max_sub_layers_minus1)                    \
  F(temporal_id_nesting_flag)                 \
  F(profile_tier_level.profile_space)         \
  F(profile_tier_level.tier_flag)             \
  F(profile_tier_level.profile_idc)           \
  F(profile_tier_level.progressive_source_flag)                         \
  F(profile_tier_level.interlaced_source_flag)                          \
  F(profile_tier_level.non_packed_constraint_flag)                      \
  F(profile_tier_level.frame_only_constraint_flag)                      \
  F(profile_tier_level.max_12bit_constraint_flag)                       \
  F(profile_tier_level.max_10bit_constraint_flag)                       \
  F(profile_tier_level.max_8bit_constraint_flag)                        \
  F(profile_tier_level.max_422chroma_constraint_flag)                   \
  F(profile_tier_level.max_420chroma_constraint_flag)                   \
  F(profile_tier_level.max_monochrome_constraint_flag)                  \
  F(profile_tier_level.intra_constraint_flag)                           \
  F(profile_tier_level.one_picture_only_constraint_flag)                \
  F(profile_tier_level.lower_bit_rate_constraint_flag)                  \
  F(profile_tier_level.max_14bit_constraint_flag)                       \
  F(profile_tier_level.level_idc)             \
  F(sub_layer_ordering_info_present_flag)     \
  F(max_layer_id)                             \
  F(num_layer_sets_minus1)                    \
  F(timing_info_present_flag)                 \
  F(num_units_in_tick)                        \
  F(time_scale)                               \
  F(poc_proportional_to_timing_flag)          \
  F(num_ticks_poc_diff_one_minus1)            \
  F(num_hrd_parameters)                       \
  F(hrd_layer_set_idx)                        \
  F(cprms_present_flag)                       \
  F(hrd_params.nal_hrd_parameters_present_flag)                         \
  F(hrd_params.vcl_hrd_parameters_present_flag)                         \
  F(hrd_params.sub_pic_hrd_params_present_flag)                         \
  F(hrd_params.tick_divisor_minus2)                                     \
  F(hrd_params.du_cpb_removal_delay_increment_length_minus1)            \
  F(hrd_params.dpb_output_delay_du_length_minus1)                       \
  F(hrd_params.bit_rate_scale)                                          \
  F(hrd_params.cpb_size_scale)                                          \
  F(hrd_params.cpb_size_du_scale)                                       \
  F(hrd_params.initial_cpb_removal_delay_length_minus1)                 \
  F(hrd_params.au_cpb_removal_delay_length_minus1)                      \
  F(hrd_params.dpb_output_delay_length_minus1)                          \
  F(vps_extension)
#endif

static void
gst_h265_vps_dump (GstH265Encoder * self, GstH265VPS * vps)
{
#ifndef GST_DISABLE_GST_DEBUG
#define VPS_STR(member) "  " G_STRINGIFY(member) " = %u\n"
#define VPS_VAL(member) vps->member,
  GST_INFO_OBJECT (self, "VPS\n" VPS_MEMBERS (VPS_STR) "%s",
      VPS_MEMBERS (VPS_VAL) "");
#undef VPS_STR
#undef VPS_VAL
#endif
}

static gboolean
_h265_update_ptl_constraints (GstH265Profile profile,
    GstH265ProfileTierLevel * ptl)
{
  const GstH265ProfileDescriptor *desc = _h265_profile_descriptor (profile);

  if (!desc)
    return FALSE;

#define set_flag(flag)                                  \
  if (desc->flag < 2)                                   \
    ptl->G_PASTE(flag, _constraint_flag) = desc->flag;
  set_flag (max_14bit);
  set_flag (max_12bit);
  set_flag (max_10bit);
  set_flag (max_8bit);
  set_flag (max_422chroma);
  set_flag (max_420chroma);
  set_flag (max_monochrome);
  set_flag (intra);
  set_flag (one_picture_only);
  set_flag (lower_bit_rate);
#undef set_flag

  return TRUE;
}

/* 7.3.3 Profile, tier and level syntax */
static gboolean
gst_h265_encoder_fill_profile_tier_level (GstH265Encoder * self,
    GstH265ProfileTierLevel * ptl)
{
  GstH265EncoderPrivate *priv = _GET_PRIV (self);
  guint8 profile = priv->stream.profile;
  guint8 level_idc = priv->stream.level;
  guint8 tier;
  guint8 profile_idc = gst_h265_encoder_get_profile_idc (profile);

  if (profile_idc == 0 || profile_idc >= 32) {
    GST_ERROR_OBJECT (self, "Invalid profile IDC: %d from profile: %d",
        profile_idc, profile);
    return FALSE;
  }

  if (!priv->input_state) {
    GST_ERROR_OBJECT (self, "No input state negotiated yet");
    return FALSE;
  }

  GstVideoInfo *info = &priv->input_state->info;
  guint8 bit_depth_luma = GST_VIDEO_INFO_COMP_DEPTH (info, 0);

  if (bit_depth_luma < 8 || bit_depth_luma > 16) {
    GST_WARNING_OBJECT (self, "Invalid input bit depth: %d", bit_depth_luma);
    return FALSE;
  }

  guint8 chroma = _h265_get_chroma_idc (info);
  if (chroma == GST_CHROMA_INVALID) {
    GST_WARNING_OBJECT (self, "Invalid input chroma subsampling");
    return FALSE;
  }

  if (priv->stream.tier == GST_H265_TIER_INVALID) {
    GST_WARNING_OBJECT (self, "Invalid tier");
    return FALSE;
  }

  tier = (priv->stream.tier == GST_H265_TIER_AUTO) ? 0xFF
      : (priv->stream.tier == GST_H265_TIER_HIGH) ? 1 : 0;

  /* The intra constraint flag is a property of the negotiated profile, not of
     the GOP structure. For profiles not handled by
     _h265_update_ptl_constraints() (Main and Main 10), fall back to an
     all-intra guess. */
  guint8 intra_constraint_flag = priv->gop.params.idr_period == 1;

  *ptl = (GstH265ProfileTierLevel) {
    /* *INDENT-OFF* */
    .profile_space = 0, /* shall be equal to 0 in bitstreams conforming to this
                           version of this Specification */
    .tier_flag = tier,
    .profile_idc = profile_idc, /* Annex A defined profiles if profile_space ==
                                   0 */
    .profile_compatibility_flag = { 0, }, /* Defined below */
    .progressive_source_flag = 1, /* progressive only */
    .interlaced_source_flag = 0,
    .non_packed_constraint_flag = 0, /* 1 specifies that there are no frame
                                        packing arrangement SEI messages */
    .frame_only_constraint_flag = 1, /* 1 specifies that field_seq_flag is equal
                                        to 0 */
    .max_14bit_constraint_flag = bit_depth_luma <= 14,
    .max_12bit_constraint_flag = bit_depth_luma <= 12,
    .max_10bit_constraint_flag = bit_depth_luma <= 10,
    .max_8bit_constraint_flag = bit_depth_luma <= 8,
    .max_422chroma_constraint_flag = chroma <= GST_CHROMA_422,
    .max_420chroma_constraint_flag = chroma <= GST_CHROMA_420,
    .max_monochrome_constraint_flag = chroma == GST_CHROMA_400,
    .intra_constraint_flag = intra_constraint_flag,
    .one_picture_only_constraint_flag = 0,
    .lower_bit_rate_constraint_flag = 1,
    .level_idc = level_idc, /* indicates a level to which the CVS conforms as
                               specified in Annex A*/
    /* TODO: scalable / multi-layer bitstreams */
    .sub_layer_profile_present_flag = { 0, },
    .sub_layer_level_present_flag = { 0, },
    .sub_layer_profile_space = { 0, },
    .sub_layer_tier_flag = { 0, },
    .sub_layer_profile_idc= { 0, },
    .sub_layer_profile_compatibility_flag = { { 0, }, },
    .sub_layer_progressive_source_flag = { 0, },
    .sub_layer_interlaced_source_flag = { 0, },
    .sub_layer_non_packed_constraint_flag = { 0, },
    .sub_layer_frame_only_constraint_flag = { 0, },
    .sub_layer_level_idc = { 0, },
    /* *INDENT-ON* */
  };

  /* Set compatibility flags according to H.265 specification annex A */
  ptl->profile_compatibility_flag[profile_idc] = 1;

  /* In A.3.2, NOTE: When general_profile_compatibility_flag[ 1 ] is equal
     to 1, general_profile_compatibility_flag[ 2 ] should also be equal to
     1. */
  if (ptl->profile_compatibility_flag[1] == 1)
    ptl->profile_compatibility_flag[2] = 1;

  /* In A.3.4, NOTE: When general_profile_compatibility_flag[ 3 ] is equal to 1,
     general_profile_compatibility_flag[ 1 ] and
     general_profile_compatibility_flag[ 2 ] should also be equal to 1. */
  if (ptl->profile_compatibility_flag[3] == 1) {
    ptl->profile_compatibility_flag[1] = 1;
    ptl->profile_compatibility_flag[2] = 1;
  }

  switch (profile_idc) {
    case GST_H265_PROFILE_IDC_MAIN:
    case GST_H265_PROFILE_IDC_MAIN_10:
      /* current profile tier level configuration */
      break;
    case GST_H265_PROFILE_IDC_MAIN_STILL_PICTURE:
    case GST_H265_PROFILE_IDC_FORMAT_RANGE_EXTENSION:
    case GST_H265_PROFILE_IDC_SCREEN_CONTENT_CODING:
    case GST_H265_PROFILE_IDC_HIGH_THROUGHPUT:
    case GST_H265_PROFILE_IDC_HIGH_THROUGHPUT_SCREEN_CONTENT_CODING_EXTENSION:
      if (!_h265_update_ptl_constraints (profile, ptl))
        goto unsupported_profile;
      break;
    case GST_H265_PROFILE_IDC_SCALABLE_FORMAT_RANGE_EXTENSION:
    case GST_H265_PROFILE_IDC_3D_MAIN:
    case GST_H265_PROFILE_IDC_MULTIVIEW_MAIN:
    case GST_H265_PROFILE_IDC_SCALABLE_MAIN:
    default:
      goto unsupported_profile;
  }

  return TRUE;

unsupported_profile:
  {
    GST_WARNING_OBJECT (self, "Unsupported profile: %s",
        gst_h265_profile_to_string (profile));
    return FALSE;
  }
}

/* 7.3.2.1 Video parameter set data semantics */
static void
gst_h265_encoder_vps_init (GstH265Encoder * self)
{
  GstH265EncoderPrivate *priv = _GET_PRIV (self);

  GST_DEBUG_OBJECT (self, "filling VPS");

  /* *INDENT-OFF* */
  priv->params.vps = (GstH265VPS) {
    .id = 0,
    .base_layer_internal_flag = 1,  /* base layer is present in the bitstream */
    .base_layer_available_flag = 1,
    .max_layers_minus1 = 0, /* only one layer allowed in each CVS */
    .max_sub_layers_minus1 = 0, /* only one sub-layer may be present in each
                                   CVS */
    .temporal_id_nesting_flag = 1, /* restricted inter prediction for CVSs */
    .profile_tier_level = priv->params.ptl,
    .sub_layer_ordering_info_present_flag = 0, /* buffering and timing
                                                  parameters apply to all
                                                  sub-layers */
    /* maximum required size of DPB per picture in each temporal layer */
    .max_dec_pic_buffering_minus1 = { priv->gop.max_dec_pic_buffering - 1, },
    /* maximum allowed number of pictures that need to be reordered */
    .max_num_reorder_pics = { priv->gop.num_reorder_frames, },
    .max_latency_increase_plus1 = { 0, }, /* no latency limits */
    .max_layer_id = 0, /* only one layer */
    .num_layer_sets_minus1 = 0, /* only one layer specified in VPS */
    .timing_info_present_flag = 1, /* timing params specified in VPS */
    .num_units_in_tick = priv->fps_d, /* number of time units of a clock
                                         operating at the frequency
                                         vps_time_scale that corresponds to one
                                         increment of a clock tick counter. */
    .time_scale = priv->fps_n, /* number of time units that pass in one
                                  second */
    .poc_proportional_to_timing_flag = 0, /* 0 indicates that the picture order
                                             count value for each picture in the
                                             CVS that is not the first picture
                                             in the CVS, in decoding order, may
                                             or may not be proportional to the
                                             output time of the picture relative
                                             to the output time of the first
                                             picture in the CVS. */
    .num_ticks_poc_diff_one_minus1 = 0, /* number of clock ticks between POC */
    .num_hrd_parameters = 0, /* number of HRD parameters in VPS */
    .hrd_layer_set_idx = 0,
    .cprms_present_flag = 0,
    .hrd_params = { 0, },
    .vps_extension = 0,
  };
  /* *INDENT-ON* */

  gst_h265_vps_dump (self, &priv->params.vps);
};

/* Table E.6 – Definition of HorizontalOffsetC and VerticalOffsetC as a function
 * of chroma_format_idc and ChromaLocType */
static guint8
_chroma_location (GstVideoChromaSite site)
{
  switch (site) {
    case GST_VIDEO_CHROMA_SITE_MPEG2:
      return 0;
    case GST_VIDEO_CHROMA_SITE_JPEG:
      return 1;
    case GST_VIDEO_CHROMA_SITE_DV:
      return 2;
    case GST_VIDEO_CHROMA_SITE_V_COSITED:
      return 3;
    default:
      return 0xff;
  };
}

/* 7.4.2.1.1 Sequence parameter set data semantics */
static gboolean
gst_h265_encoder_sps_init (GstH265Encoder * self)
{
  GstH265EncoderPrivate *priv = _GET_PRIV (self);
  GstVideoInfo *info;
  guint8 chroma_format_idc, bit_depth_luma, bit_depth_chroma,
      aspect_ratio_present_flag, aspect_ratio_idc, video_full_range_flag,
      colour_primaries, transfer_characteristics, matrix_coefficients,
      colour_description_present_flag, video_signal_type_present_flag,
      chroma_loc_info_presente_flag, chroma_sample_loc_type,
      sps_scc_extension_flag;
  guint16 sar_width, sar_height;
  guint32 conformance_window_flag, conf_win_left_offset, conf_win_right_offset,
      conf_win_top_offset, conf_win_bottom_offset;
  gint width, height;

  info = &priv->input_state->info;

  GST_DEBUG_OBJECT (self, "filling SPS");

  chroma_format_idc = _h265_get_chroma_idc (info);
  if (chroma_format_idc == GST_CHROMA_INVALID) {
    GST_WARNING_OBJECT (self, "Invalid format IDC");
    return FALSE;
  }

  bit_depth_luma = GST_VIDEO_INFO_COMP_DEPTH (info, 0);
  bit_depth_chroma = GST_VIDEO_INFO_COMP_DEPTH (info, 1);
  g_assert (bit_depth_luma >= 8 && bit_depth_chroma >= 8);

  if (GST_VIDEO_INFO_WIDTH (info) > priv->config.surface_width
      || GST_VIDEO_INFO_HEIGHT (info) > priv->config.surface_height) {
    GST_WARNING_OBJECT (self, "Luma sample size is bigger than frame size");
    return FALSE;
  }

  width = GST_VIDEO_INFO_WIDTH (info);
  height = GST_VIDEO_INFO_HEIGHT (info);

  /* frame cropping */
  conformance_window_flag = (width < priv->config.surface_width
      || height < priv->config.surface_height);
  conf_win_left_offset = conf_win_right_offset = conf_win_top_offset =
      conf_win_bottom_offset = 0;

  if (conformance_window_flag) {
    const guint SubWidthC[] = { 1, 2, 2, 1 };
    const guint SubHeightC[] = { 1, 2, 1, 1 };

    conformance_window_flag = 1;

    /* The conformance window must crop the coded picture down to the exact
       requested size. The offsets are in chroma samples, so the crop deltas
       have to align with the chroma subsampling or the integer division below
       would silently leave up to SubWidthC-1 luma samples uncropped and the
       decoded size would not match the caps. Fail instead of coding that. */
    if ((priv->config.surface_width - width) % SubWidthC[chroma_format_idc] != 0
        || (priv->config.surface_height -
            height) % SubHeightC[chroma_format_idc] != 0) {
      GST_WARNING_OBJECT (self,
          "Coded size is not aligned to the chroma subsampling");
      return FALSE;
    }

    conf_win_right_offset =
        (priv->config.surface_width - width) / SubWidthC[chroma_format_idc];
    conf_win_bottom_offset =
        (priv->config.surface_height - height) / SubHeightC[chroma_format_idc];
  }

  /* VUI parameters */
  /* Extended SAR by default */
  aspect_ratio_idc = 0xff;
  sar_width = GST_VIDEO_INFO_PAR_N (info);
  sar_height = GST_VIDEO_INFO_PAR_D (info);
  aspect_ratio_present_flag = (sar_width != 0 && sar_height != 0);

  if (aspect_ratio_present_flag) {
    for (gint i = 0; i < G_N_ELEMENTS (_h265_aspect_ratio); i++) {
      int num = _h265_aspect_ratio[i].num, den = _h265_aspect_ratio[i].den;
      if (gst_util_fraction_compare (sar_width, sar_height, num, den) == 0) {
        aspect_ratio_idc = i;
        sar_width = sar_height = 0;
        break;
      }
    }
  } else {
    aspect_ratio_idc = sar_width = sar_height = 0;
  }

  video_full_range_flag =
      (GST_VIDEO_INFO_COLORIMETRY (info).range == GST_VIDEO_COLOR_RANGE_0_255);
  colour_primaries =
      gst_video_color_primaries_to_iso (GST_VIDEO_INFO_COLORIMETRY
      (info).primaries);
  transfer_characteristics =
      gst_video_transfer_function_to_iso (GST_VIDEO_INFO_COLORIMETRY
      (info).transfer);
  matrix_coefficients =
      gst_video_color_matrix_to_iso (GST_VIDEO_INFO_COLORIMETRY (info).matrix);
  colour_description_present_flag = colour_primaries != 2
      || transfer_characteristics != 2 || matrix_coefficients != 2;
  video_signal_type_present_flag =
      GST_VIDEO_INFO_COLORIMETRY (info).range != GST_VIDEO_COLOR_RANGE_UNKNOWN
      || colour_description_present_flag;

  chroma_loc_info_presente_flag = 1;
  chroma_sample_loc_type = _chroma_location (GST_VIDEO_INFO_CHROMA_SITE (info));
  if (chroma_sample_loc_type == 0xff) {
    chroma_loc_info_presente_flag = 0;
    chroma_sample_loc_type = 0;
  }

  sps_scc_extension_flag = _profile_is_scc (priv->stream.profile);

  g_assert (priv->gop.log2_max_poc >= 4);

  /* Default values for H.265 encoder (single layer) */
  /* *INDENT-OFF* */
  priv->params.sps = (GstH265SPS) {
    .id = 0,
    .vps_id = priv->params.vps.id,
    .vps = &priv->params.vps,
    .max_sub_layers_minus1 = priv->params.vps.max_sub_layers_minus1,
    .temporal_id_nesting_flag = priv->params.vps.temporal_id_nesting_flag,
    .profile_tier_level = priv->params.ptl,
    .chroma_format_idc = chroma_format_idc,
    .separate_colour_plane_flag = 0, /* color components aren't coded
                                        separately */
    .pic_width_in_luma_samples = priv->config.surface_width, /* specifies the
                                                                width of each
                                                                decoded picture
                                                                in units of luma
                                                                samples */
    .pic_height_in_luma_samples = priv->config.surface_height, /* specifies the
                                                                  height of each
                                                                  decoded
                                                                  picture in
                                                                  units of luma
                                                                  samples */
    .conformance_window_flag = conformance_window_flag, /* 1 indicates that the
                                                           conformance cropping
                                                           window offset
                                                           parameters follow
                                                           next in the SPS*/
    /* if conformance_window_flag */
    /* specify the samples of the pictures in the CVS that are output from the
       decoding process, in terms of a rectangular region specified in picture
       coordinates for output.*/
    .conf_win_left_offset = conf_win_left_offset,
    .conf_win_right_offset = conf_win_right_offset,
    .conf_win_top_offset = conf_win_top_offset,
    .conf_win_bottom_offset = conf_win_bottom_offset,

    .bit_depth_luma_minus8 = bit_depth_luma - 8,
    .bit_depth_chroma_minus8 = bit_depth_chroma - 8,
    .log2_max_pic_order_cnt_lsb_minus4 =
        MAX (priv->gop.log2_max_poc - 4, 0),

    .sub_layer_ordering_info_present_flag =
        priv->params.vps.sub_layer_ordering_info_present_flag,
    /* filled bellow */
    /* .max_dec_pic_buffering_minus1 = { 0, }, */
    /* .max_num_reorder_pics = { 0, }, */
    /* .max_latency_increase_plus1 = { 0, }, */

    /* CTB size from 8x8 to 32x32 */
    .log2_min_luma_coding_block_size_minus3 = 0, /* minimum luma coding block
                                                    size */
    .log2_diff_max_min_luma_coding_block_size = 2, /* difference between the
                                                      maximum and minimum luma
                                                      coding block size */
    /* transform size from 4x4 to 32x32  */
    .log2_min_transform_block_size_minus2 = 0, /* minimum luma transform block
                                                  size */
    .log2_diff_max_min_transform_block_size = 3, /* difference between the
                                                    maximum and minimum luma
                                                    transform block size */
    /* full transform hierarchy allowed (2-5) */
    .max_transform_hierarchy_depth_inter = 3, /* maximum hierarchy depth for
                                                 transform units of coding units
                                                 coded in inter prediction
                                                 mode */
    .max_transform_hierarchy_depth_intra = 3, /* maximum hierarchy depth for
                                                 transform units of coding units
                                                 coded in intra prediction
                                                 mode. */

    .scaling_list_enabled_flag = 0, /* 0 specifies that scaling list is not used
                                       for the scaling process for transform
                                       coefficients */
    /* if scaling_list_enabled_flag */
    .scaling_list_data_present_flag = 0, /* 0 specifies that the
                                            scaling_list_data() syntax structure
                                            is not present */

    .scaling_list = { { 0, }, },

    .amp_enabled_flag = 1, /* 1 specifies that asymmetric motion partitions */
    .sample_adaptive_offset_enabled_flag = 1, /* 0 specifies that the sample
                                                 adaptive offset process is not
                                                 applied to the reconstructed
                                                 picture after the deblocking
                                                 filter process */
    .pcm_enabled_flag = 0, /* 0 specifies that Pulse Code Modulation-related
                              syntax is not present in the CVS */
    /* if pcm_enabled_flag */
    .pcm_sample_bit_depth_luma_minus1 = 0,
    .pcm_sample_bit_depth_chroma_minus1 = 0,
    .log2_min_pcm_luma_coding_block_size_minus3 = 0,
    .log2_diff_max_min_pcm_luma_coding_block_size = 0,
    .pcm_loop_filter_disabled_flag = 0,

    /* STRPSs should ideally be here rather than defined individually in each
       slice, but the structure isn't completely fixed so for now don't
       bother. */
    .num_short_term_ref_pic_sets = 0,
    .short_term_ref_pic_set = { { 0, }, },

    .long_term_ref_pics_present_flag = 0,
    /* if long_term_ref_pics_present_flag */
    .num_long_term_ref_pics_sps = 0,
    .lt_ref_pic_poc_lsb_sps = { 0, },
    .used_by_curr_pic_lt_sps_flag = { 0, },

    .temporal_mvp_enabled_flag = 0, /* 0 specifies that
                                       slice_temporal_mvp_enabled_flag is not
                                       present in slice headers and that
                                       temporal motion vector predictors are not
                                       used in the CVS */
    .strong_intra_smoothing_enabled_flag = 0, /* TODO: 1 specifies that
                                                 bi-linear interpolation is
                                                 conditionally used in the intra
                                                 prediction filtering process in
                                                 the CVS as specified in clause
                                                 8.4.4.2.3. */
    .vui_parameters_present_flag = 1,

    /* if vui_parameters_present_flag */
    .vui_params = {
      /* .parsed =  */
      .aspect_ratio_info_present_flag = aspect_ratio_present_flag,
      .aspect_ratio_idc = aspect_ratio_idc,
      /* if aspect_ratio_idc == 255 */
      .sar_width = sar_width,
      .sar_height = sar_height,

      .overscan_info_present_flag = 0, /* the preferred display method for the
                                          video signal is unspecified. */
      /* if overscan_info_present_flag */
      .overscan_appropriate_flag = 0, /* 0 indicates that the cropped decoded
                                         pictures output contain visually
                                         important information in the entire
                                         region out to the edges of the
                                         conformance cropping window of the
                                         picture, such that the cropped decoded
                                         pictures output should not be displayed
                                         using overscan. Instead, they should be
                                         displayed using either an exact match
                                         between the display area and the
                                         conformance cropping window, or using
                                         underscan*/

      .video_signal_type_present_flag = video_signal_type_present_flag,
      .video_format = 5, /* Table E-2 - Unspecified video format */
      .video_full_range_flag = video_full_range_flag,
      .colour_description_present_flag = colour_description_present_flag,
      .colour_primaries = colour_primaries,
      .transfer_characteristics = transfer_characteristics,
      .matrix_coefficients = matrix_coefficients,

      .chroma_loc_info_present_flag = chroma_loc_info_presente_flag,
      .chroma_sample_loc_type_top_field = chroma_sample_loc_type,
      .chroma_sample_loc_type_bottom_field = chroma_sample_loc_type,

      .neutral_chroma_indication_flag = 0, /* 0 provides no indication of
                                              decoded chroma sample values */
      .field_seq_flag = 0, /* 0 indicates that the CVS conveys pictures that
                              represent frames and that a picture timing SEI
                              message may or may not be present in any access
                              unit of the current CVS. */
      .frame_field_info_present_flag = 0, /* 0 specifies that the pic_struct
                                             syntax element is not present in
                                             picture timing SEI messages */
      .default_display_window_flag = 0, /* 0 indicates that the default display
                                           window parameters are not present */
      .def_disp_win_left_offset = 0,
      .def_disp_win_right_offset = 0,
      .def_disp_win_top_offset = 0,
      .def_disp_win_bottom_offset = 0,

      .timing_info_present_flag = priv->params.vps.timing_info_present_flag,
      /* if timing_info_present_flag */
      .num_units_in_tick = priv->params.vps.num_units_in_tick,
      .time_scale = priv->params.vps.time_scale,
      .poc_proportional_to_timing_flag = priv->params.vps.poc_proportional_to_timing_flag,
      /* if poc_proportional_to_timing_flag */
      .num_ticks_poc_diff_one_minus1 = priv->params.vps.num_ticks_poc_diff_one_minus1,

      .hrd_parameters_present_flag = 0,
      /* if hrd_parameters_present_flat */
      .hrd_params = { 0, },

      .bitstream_restriction_flag = 1, /* 1, specifies that the bitstream
                                          restriction parameters for the CVS are
                                          present. */
      /*  if bitstream_restriction_flag */
      .tiles_fixed_structure_flag = 0, /* 0 indicates that tiles syntax elements
                                          in different PPSs may or may not have
                                          the same value */
      .motion_vectors_over_pic_boundaries_flag = 1, /* 1 indicates that one or
                                                       more samples outside the
                                                       picture boundaries may be
                                                       used in inter
                                                       prediction. */
      .restricted_ref_pic_lists_flag = 1, /* 1 indicates that all P and B slices
                                             (when present) that belong to the
                                             same picture have an identical
                                             reference picture list 0 and that
                                             all B slices (when present) that
                                             belong to the same picture have an
                                             identical reference picture list
                                             1 */
      .min_spatial_segmentation_idc = 0,
      .max_bytes_per_pic_denom = 0,
      .max_bits_per_min_cu_denom = 0,
      /* Indicate the maximum absolute value of a decoded horizontal and
         vertical motion vector component, respectively, in quarter luma sample
         units, for all pictures in the CVS */
      .log2_max_mv_length_horizontal = 15,
      .log2_max_mv_length_vertical = 15,

      /* calculated values */
      /* .par_n = 0, */
      /* .par_d = 0, */
    },

    .sps_extension_flag = sps_scc_extension_flag,

    /* if sps_extension_present_flag */
    .sps_range_extension_flag = 0,
    .sps_multilayer_extension_flag = 0,
    .sps_3d_extension_flag = 0,
    .sps_scc_extension_flag = sps_scc_extension_flag,
    .sps_extension_4bits = 0,

    /* if sps_range_extension_flag */
    .sps_extension_params = { 0, },

    /* if sps_scc_extension_flag */
    .sps_scc_extension_params = {
        .sps_curr_pic_ref_enabled_flag = 0, /* 1 enables current picture
                                               referencing (IBC) in the CVS;
                                               the encoder does not use it, so
                                               0 leaves the bitstream
                                               decodable by non-SCC devices */
        .palette_mode_enabled_flag = 0,
        .palette_max_size = sps_scc_extension_flag ? 64 : 0,
        .delta_palette_max_predictor_size = sps_scc_extension_flag ? 32 : 0,
        .sps_palette_predictor_initializers_present_flag = 0,
        .sps_num_palette_predictor_initializer_minus1 = 0,
        .sps_palette_predictor_initializer = {{ 0, }},
        .motion_vector_resolution_control_idc = 0,
        .intra_boundary_filtering_disabled_flag = 0,
    },
  };
  /* *INDENT-ON* */

  /* copy max_dec_pic_buffering_minus1 from vps */
  for (int i = 0; i <= priv->params.sps.max_sub_layers_minus1; i++) {
    priv->params.sps.max_dec_pic_buffering_minus1[i] =
        priv->params.vps.max_dec_pic_buffering_minus1[i];
    priv->params.sps.max_num_reorder_pics[i] =
        priv->params.vps.max_num_reorder_pics[i];
    priv->params.sps.max_latency_increase_plus1[i] =
        priv->params.vps.max_latency_increase_plus1[i];
  }

  return TRUE;
}

/* 7.4.3.3.1 General picture parameter set RBSP semantics */
static void
gst_h265_encoder_pps_init (GstH265Encoder * self)
{
  GstH265EncoderPrivate *priv = _GET_PRIV (self);

  GST_DEBUG_OBJECT (self, "filling PPS");

  /* *INDENT-OFF* */
  priv->params.pps = (GstH265PPS) {
    .id = 0,
    .sps_id = 0,
    .sps = &priv->params.sps,

    .dependent_slice_segments_enabled_flag = 0, /* 0 specifies the absence of
                                                   the syntax element
                                                   dependent_slice_segment_flag
                                                   in the slice segment headers
                                                   for coded pictures referring
                                                   to the PPS. */
    .output_flag_present_flag = 0, /* 0 indicates that the pic_output_flag
                                      syntax element is not present in the
                                      associated slice header */
    .num_extra_slice_header_bits = 0, /* specifies the number of extra slice
                                         header bits that are present in the
                                         slice header RBSP for coded pictures
                                         referring to the PPS */
    .sign_data_hiding_enabled_flag = 0, /* 0 specifies that sign bit hiding is
                                           disabled. */
    .cabac_init_present_flag = 0, /* 0 specifies that cabac_init_flag is not
                                     present in slice headers referring to the
                                     PPS */
    /* XXX: update in the subclass! */
    .num_ref_idx_l0_default_active_minus1 = 0, /* specifies the inferred value
                                                  of
                                                  num_ref_idx_l0_active_minus1
                                                  for P and B slices with
                                                  num_ref_idx_active_override_flag
                                                  equal to 0 */
    /* XXX: update in the subclass! */
    .num_ref_idx_l1_default_active_minus1 = 0, /* specifies the inferred value
                                                  of
                                                  num_ref_idx_l1_active_minus1
                                                  for B slices with
                                                  num_ref_idx_active_override_flag
                                                  equal to 0. */
    /* XXX: update in the subclass! */
    .init_qp_minus26 = 0, /* default initial QP 26 unless overridden by the
                             subclass; stores the initial value of SliceQpY */
    .constrained_intra_pred_flag = 0, /* 0 specifies that intra prediction
                                         allows usage of residual data and
                                         decoded samples of neighbouring coding
                                         blocks coded using either intra or
                                         inter prediction modes */
    /* XXX: update in the subclass! */
    /* va: 1 by default (features) */
    /* ffmpeg-vulkan: !!(enc->caps.stdSyntaxFlags & VK_VIDEO_ENCODE_H265_STD_TRANSFORM_SKIP_ENABLED_FLAG_SET_BIT_KHR) */
    .transform_skip_enabled_flag = 0, /* 0 specifies that transform_skip_flag is
                                         not present in the residual coding
                                         syntax */
    /* XXX: update in the subclass! */
    /* va: features.bits.cu_qp_data / default: false if not CQP */
    /* ffmpeg-vulkan: enc->common.opts.rc_mode != VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR */
    .cu_qp_delta_enabled_flag = 0, /* 0 specifies that the
                                      diff_cu_qp_delta_depth syntax element is
                                      not present in the PPS and that
                                      cu_qp_delta_abs is not present in the
                                      transform unit syntax and the palette
                                      syntax */
    /* if cu_qp_delta_enabled_flag */
    /* va: 2 by default or sps.log2_diff_max_min_luma_coding_block_size */
    .diff_cu_qp_delta_depth = 0, /* specifies the difference between the luma
                                    CTB size and the minimum luma coding block
                                    size of coding units that convey
                                    cu_qp_delta_abs and
                                    cu_qp_delta_sign_flag. */

    /* va: doesn't offset picture */
    .cb_qp_offset = 0,
    .cr_qp_offset = 0,
    .slice_chroma_qp_offsets_present_flag = 0,
    .weighted_pred_flag = 0, /* equal to 0 specifies that weighted prediction is
                                not applied to P slices */
    .weighted_bipred_flag = 0, /* 0 specifies that the default weighted
                                  prediction is applied to B slices */
    /* va: features.bits.transquant_bypass != 0 */
    /* ffmpeg-vulkan: stdSyntaxFlags & VK_VIDEO_ENCODE_H265_STD_TRANSQUANT_BYPASS_ENABLED_FLAG_SET_BIT_KHR */
    .transquant_bypass_enabled_flag = 0,
    /* va: _is_tile_enabled */
    .tiles_enabled_flag = 0, /* 0 specifies that there is only one tile in each
                                picture referring to the PPS */
    .entropy_coding_sync_enabled_flag = 0,

    /* va: tiles parameters */
    .num_tile_columns_minus1 = 0,
    .num_tile_rows_minus1 = 0,
    /* va: 1 / ffmpeg: 0 */
    .uniform_spacing_flag = 0, /* 1 specifies that tile column boundaries and
                                  likewise tile row boundaries are distributed
                                  uniformly across the picture */
    /* if tiles_enabled_flag = 1 */
    /* va: partion.tile_ctu_{rows/cols}[i] */
    .column_width_minus1 = { 0, },
    .row_height_minus1 = { 0, },
    .loop_filter_across_tiles_enabled_flag = 0, /* 0 specifies that in-loop
                                                   filtering operations are not
                                                   performed across tile
                                                   boundaries in pictures
                                                   referring to the PPS */

    /* enabled if multi-slices */
    .loop_filter_across_slices_enabled_flag = 0, /* 0 specifies that in-loop
                                                    filtering operations are not
                                                    performed across left and
                                                    upper boundaries of slices
                                                    referring to the PPS */
    .deblocking_filter_control_present_flag = 0, /* 0 specifies the absence of
                                                    deblocking filter control
                                                    syntax elements in the
                                                    PPS */
    .deblocking_filter_override_enabled_flag = 0, /* 0 specifies the absence of
                                                     deblocking_filter_override_flag
                                                     in the slice headers for
                                                     pictures referring to the
                                                     PPS */
    .deblocking_filter_disabled_flag = 0, /* 0 specifies that deblocking filter
                                             is enabled for pictures referring
                                             to the PPS unless overridden for a
                                             slice */
    /* specify the default deblocking parameter offsets for β and tC (divided by
       2) that are applied for slices referring to the PPS, unless the default
       deblocking parameter offsets are overridden by the deblocking parameter
       offsets present in the slice headers of the slices referring to the
       PPS */
    .beta_offset_div2 = 0,
    .tc_offset_div2 = 0,


    .scaling_list_data_present_flag = 0,

    .scaling_list = { { 0, }, },

    /* Do not change the ref list */
    .lists_modification_present_flag = 0, /* 0 specifies that the syntax
                                             structure
                                             ref_pic_lists_modification( ) is
                                             not present in the slice segment
                                             header */
    .log2_parallel_merge_level_minus2 = 0,
    .slice_segment_header_extension_present_flag = 0,

    .pps_extension_flag = _profile_is_scc (priv->stream.profile),

    /* if pps_extension_flag */
    .pps_range_extension_flag = 0,
    .pps_multilayer_extension_flag = 0,
    .pps_3d_extension_flag = 0,
    .pps_scc_extension_flag = _profile_is_scc (priv->stream.profile),
    .pps_extension_4bits = 0,

    /* if pps_range_extension_flag */
    .pps_extension_params = { 0, },

    /* if pps_scc_extension_flag*/
    .pps_scc_extension_params = {
      .pps_curr_pic_ref_enabled_flag = 0, /* 1 enables current picture
                                             referencing (IBC); the encoder
                                             signals no IBC syntax, so 0 keeps
                                             the stream decodable by non-SCC
                                             devices */
      .residual_adaptive_colour_transform_enabled_flag = 0,
      .pps_slice_act_qp_offsets_present_flag = 0,
      .pps_act_y_qp_offset_plus5 = 0,
      .pps_act_cb_qp_offset_plus5 = 0,
      .pps_act_cr_qp_offset_plus3 = 0,
      .pps_palette_predictor_initializers_present_flag = 0,
      .pps_num_palette_predictor_initializer = 0,
      .monochrome_palette_flag = 0,
      .luma_bit_depth_entry_minus8 = 0,
      .chroma_bit_depth_entry_minus8 = 0,
      .pps_palette_predictor_initializer = { { 0, }, },
    },

    /* calculated values */
    /* .PicWidthInCtbsY = 0, */
    /* .PicHeightInCtbsY = 0, */
  };
  /* *INDENT-ON* */
}

static GstFlowReturn
gst_h265_encoder_configure (GstH265Encoder * self)
{
  GstH265EncoderPrivate *priv = _GET_PRIV (self);
  GstH265EncoderClass *klass = GST_H265_ENCODER_GET_CLASS (self);
  GstFlowReturn ret;

  if (!priv->input_state)
    return GST_FLOW_NOT_NEGOTIATED;

  if (gst_h265_encoder_drain (self) != GST_FLOW_OK)
    return GST_FLOW_ERROR;

  GST_LOG_OBJECT (self, "Configuring encoder");

  gst_h265_encoder_reset (self);

  ret = klass->negotiate (self, priv->input_state, &priv->stream.profile,
      &priv->stream.level, &priv->stream.tier);
  if (ret != GST_FLOW_OK)
    return ret;

  if (!gst_h265_encoder_fill_profile_tier_level (self, &priv->params.ptl))
    return GST_FLOW_ERROR;

  if (klass->new_sequence) {
    ret = klass->new_sequence (self, priv->input_state, &priv->params.ptl);
    if (ret != GST_FLOW_OK)
      return ret;

    if (priv->params.ptl.tier_flag != 0 && priv->params.ptl.tier_flag != 1) {
      GST_ERROR_OBJECT (self, "subclass didn't set a valid tier");
      return GST_FLOW_ERROR;
    }

    /* update stream parameters given the ptl from new_sequence() */
    priv->stream.profile =
        gst_h265_profile_tier_level_get_profile (&priv->params.ptl);
    priv->stream.level = priv->params.ptl.level_idc;
    priv->stream.tier = priv->params.ptl.tier_flag == 0 ? GST_H265_TIER_MAIN
        : GST_H265_TIER_HIGH;
  }

  if (priv->stream.profile == GST_H265_PROFILE_INVALID) {
    GST_ERROR_OBJECT (self, "Subclass didn't set a valid profile");
    return GST_FLOW_ERROR;
  }
  /* it's not possible here to calculate the auto level, since its dependencies.
   * It most be done by the subclass */
  if (priv->stream.level < GST_H265_LEVEL_L1) {
    GST_ERROR_OBJECT (self, "Subclass didn't set a valid level");
    return GST_FLOW_ERROR;
  }

  /* new_sequence() could have created the mapper but if not, let's create it
   * now, since we should have the L0/L1 list sizes */
  if (!gst_h265_encoder_generate_gop_structure_internal (self))
    return GST_FLOW_ERROR;

  /* after gop generation. Order is important! */
  gst_h265_encoder_vps_init (self);
  if (!gst_h265_encoder_sps_init (self))
    return GST_FLOW_ERROR;
  gst_h265_encoder_pps_init (self);

  /* this has to be the last operation since it calls
   * gst_video_encoder_set_output() */
  g_assert (klass->new_parameters);
  ret =
      klass->new_parameters (self, &priv->params.vps, &priv->params.sps,
      &priv->params.pps);

  if (ret != GST_FLOW_OK)
    return ret;

  /* latency */
  {
    GstVideoEncoder *encoder = GST_VIDEO_ENCODER (self);
    guint ip_period_minus1 =
        priv->gop.params.ip_period > 0 ? priv->gop.params.ip_period - 1 : 1;
    guint frames_latency =
        priv->config.preferred_output_delay + ip_period_minus1;
    GstClockTime latency = gst_util_uint64_scale (frames_latency,
        priv->fps_d * GST_SECOND, priv->fps_n);
    gst_video_encoder_set_latency (encoder, latency, latency);
  }

  /* dump parameter sets after been overrode by implementation */
  gst_h265_sps_dump (self, &priv->params.sps);
  gst_h265_pps_dump (self, &priv->params.pps);

  return ret;
}

static inline void
gst_h265_encoder_push_dts (GstH265Encoder * self, GstVideoCodecFrame * frame)
{
  GstH265EncoderPrivate *priv = _GET_PRIV (self);
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
      GstClockTimeDiff diff = dts_diff * max_reorder_num;
      if (GST_CLOCK_TIME_IS_VALID (frame->pts) && (diff <= frame->pts)) {
        dts = frame->pts - diff;
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
gst_h265_encoder_try_to_finish_all_frames (GstH265Encoder * self)
{
  GstFlowReturn ret;

  do {
    ret = gst_h265_encoder_finish_last_frame (self);
  } while (ret == GST_FLOW_OK);

  if (ret == GST_FLOW_OUTPUT_NOT_READY)
    ret = GST_FLOW_OK;

  return ret;
}

static GstFlowReturn
gst_h265_encoder_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstH265Encoder *self = GST_H265_ENCODER (encoder);
  GstH265EncoderPrivate *priv = _GET_PRIV (self);
  GstH265EncoderClass *klass = GST_H265_ENCODER_GET_CLASS (self);
  GstFlowReturn ret = GST_FLOW_ERROR;
  GstH265EncoderFrame *h265_frame;
  GstVideoCodecFrame *frame_encode = NULL;

  GST_LOG_OBJECT (encoder, "handle frame id %d, dts %" GST_TIME_FORMAT
      ", pts %" GST_TIME_FORMAT, frame->system_frame_number,
      GST_TIME_ARGS (GST_BUFFER_DTS (frame->input_buffer)),
      GST_TIME_ARGS (GST_BUFFER_PTS (frame->input_buffer)));

  if (g_atomic_int_compare_and_exchange (&priv->need_configure, TRUE, FALSE)) {
    if (gst_h265_encoder_configure (self) != GST_FLOW_OK) {
      gst_video_encoder_finish_frame (encoder, frame);
      return GST_FLOW_ERROR;
    }
  }

  h265_frame = gst_h265_encoder_frame_new ();
  gst_video_codec_frame_set_user_data (frame, h265_frame,
      gst_h265_encoder_frame_unref);
  gst_h265_encoder_push_dts (self, frame);

  if (klass->new_output) {
    ret = klass->new_output (self, frame, h265_frame);
    if (ret != GST_FLOW_OK)
      goto error_new_frame;
  }

  if (!gst_h265_encoder_reorder_frame (self, frame, FALSE, &frame_encode))
    goto error_reorder;

  /* pass it to reorder list and we should not use it again. */
  frame = NULL;

  if (frame_encode) {
    while (frame_encode) {
      ret = gst_h265_encoder_encode_frame (self, frame_encode, FALSE);
      if (ret != GST_FLOW_OK)
        goto error_encode;

      while (ret == GST_FLOW_OK && g_queue_get_length (&priv->output_list) >
          priv->config.preferred_output_delay)
        ret = gst_h265_encoder_finish_last_frame (self);

      if (ret != GST_FLOW_OK)
        return ret;

      /* Try to push out all ready frames. */
      ret = gst_h265_encoder_try_to_finish_all_frames (self);
      if (ret != GST_FLOW_OK)
        return ret;

      frame_encode = NULL;
      if (!gst_h265_encoder_reorder_frame (self, NULL, FALSE, &frame_encode))
        goto error_reorder;
    }
  } else {
    /* Try to push out all ready frames. */
    ret = gst_h265_encoder_try_to_finish_all_frames (self);
    if (ret != GST_FLOW_OK)
      return ret;
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
}

static gboolean
gst_h265_encoder_flush (GstVideoEncoder * encoder)
{
  GstH265Encoder *self = GST_H265_ENCODER (encoder);
  GstH265EncoderPrivate *priv = _GET_PRIV (self);

  gst_h265_encoder_flush_lists (self);
  gst_vec_deque_clear (priv->dts_queue);

  /* begin from an IDR after flush. */
  if (priv->gop.mapper)
    gst_h26x_gop_mapper_reset_index (priv->gop.mapper);

  priv->gop.last_keyframe = NULL;
  /* Reset GOP mapper and keyframe tracking; other state persists across flush. */

  return TRUE;
}

static GstFlowReturn
gst_h265_encoder_finish (GstVideoEncoder * encoder)
{
  return gst_h265_encoder_drain (GST_H265_ENCODER (encoder));
}

/* Derive the maximum chroma IDC and bit depth from a GstH265ProfileDescriptor.
 * Returns FALSE for MAIN-family profiles where constraint flags are all 2. */
static gboolean
_h265_desc_to_max_chroma_depth (const GstH265ProfileDescriptor * desc,
    guint8 * max_chroma, guint8 * max_bit_depth)
{
  /* MAIN-family profiles have all constraint flags set to 2 (not applicable) */
  if (desc->max_422chroma == 2)
    return FALSE;

  if (desc->max_monochrome == 1)
    *max_chroma = GST_CHROMA_400;
  else if (desc->max_420chroma == 1)
    *max_chroma = GST_CHROMA_420;
  else if (desc->max_422chroma == 1)
    *max_chroma = GST_CHROMA_422;
  else
    *max_chroma = GST_CHROMA_444;

  if (desc->max_8bit == 1)
    *max_bit_depth = 8;
  else if (desc->max_10bit == 1)
    *max_bit_depth = 10;
  else if (desc->max_12bit == 1)
    *max_bit_depth = 12;
  else if (desc->max_14bit == 1)
    *max_bit_depth = 14;
  else
    *max_bit_depth = 16;

  return TRUE;
}

/* Accumulate the maximum allowed chroma IDC and bit depth for the given
 * H.265 profile name.  Uses _h265_profiles[] as primary source,
 * profile_chroma_map as fallback for MAIN-family profiles. */
static void
_h265_check_formats (const gchar * str, guint8 * max_chroma,
    guint8 * max_bit_depth, gboolean * has_profile)
{
  GstH265Profile profile = gst_h265_profile_from_string (str);
  if (profile == GST_H265_PROFILE_INVALID)
    return;

  guint8 chroma = 0, depth = 0;
  const GstH265ProfileDescriptor *desc = _h265_profile_descriptor (profile);

  if ((!desc || !_h265_desc_to_max_chroma_depth (desc, &chroma, &depth))
      && !_h265_profile_max_chroma (profile, &chroma, &depth))
    return;

  *has_profile = TRUE;
  if (chroma > *max_chroma)
    *max_chroma = chroma;
  if (depth > *max_bit_depth)
    *max_bit_depth = depth;
}

static gboolean
_h265_format_is_valid (GstVideoFormat format, guint8 max_chroma,
    guint8 max_bit_depth)
{
  if (format == GST_VIDEO_FORMAT_UNKNOWN || format == GST_VIDEO_FORMAT_DMA_DRM)
    return FALSE;

  const GstVideoFormatInfo *info = gst_video_format_get_info (format);
  if (!info)
    return FALSE;

  guint8 chroma = _chroma_idc_from_finfo (info);
  if (chroma == GST_CHROMA_INVALID)
    return FALSE;

  guint8 depth = GST_VIDEO_FORMAT_INFO_DEPTH (info, 0);

  return (chroma <= max_chroma && depth <= max_bit_depth);
}

static gboolean
_h265_value_is_valid (const GValue * val, guint8 max_chroma,
    guint8 max_bit_depth)
{
  GstVideoFormat format;

  if (!G_VALUE_HOLDS_STRING (val))
    return FALSE;

  format = gst_video_format_from_string (g_value_get_string (val));
  return _h265_format_is_valid (format, max_chroma, max_bit_depth);
}

static gboolean
_h265_drm_value_is_valid (const GValue * val, guint8 max_chroma,
    guint8 max_bit_depth)
{
  if (!G_VALUE_HOLDS_STRING (val))
    return FALSE;

  guint64 modifier = 0;
  guint32 fourcc =
      gst_video_dma_drm_fourcc_from_string (g_value_get_string (val),
      &modifier);

  if (fourcc == 0 || modifier == 0xffffffffffffffff)
    return FALSE;

  GstVideoFormat format =
      gst_video_dma_drm_format_to_gst_format (fourcc, modifier);
  return _h265_format_is_valid (format, max_chroma, max_bit_depth);
}

static gboolean
_h265_update_caps_drm_format (GstStructure * s, guint8 max_chroma,
    guint8 max_bit_depth)
{
  const GValue *val = gst_structure_get_value (s, "drm-format");
  if (!val)
    return FALSE;

  if (G_VALUE_HOLDS_STRING (val)) {
    return _h265_drm_value_is_valid (val, max_chroma, max_bit_depth);
  } else if (GST_VALUE_HOLDS_LIST (val)) {
    GValue new_list = G_VALUE_INIT;

    guint len = gst_value_list_get_size (val);
    gst_value_list_init (&new_list, len);

    for (guint i = 0; i < len; i++) {
      const GValue *lval = gst_value_list_get_value (val, i);
      if (_h265_drm_value_is_valid (lval, max_chroma, max_bit_depth))
        gst_value_list_append_value (&new_list, lval);
    }

    guint new_len = gst_value_list_get_size (&new_list);
    if (new_len == 1) {
      const GValue *new_val = gst_value_list_get_value (&new_list, 0);
      gst_structure_set_value (s, "drm-format", new_val);
      g_value_unset (&new_list);
      return TRUE;
    } else if (new_len > 0) {
      gst_structure_set_value (s, "drm-format", &new_list);
      g_value_unset (&new_list);
      return TRUE;
    }

    g_value_unset (&new_list);
  }

  return FALSE;
}

static gboolean
_h265_update_caps_format (GstStructure * s, guint8 max_chroma,
    guint8 max_bit_depth)
{
  const GValue *val;
  GstVideoFormat format;

  val = gst_structure_get_value (s, "format");
  if (!val)
    return FALSE;

  if (G_VALUE_HOLDS_STRING (val)) {
    format = gst_video_format_from_string (g_value_get_string (val));
    if (format == GST_VIDEO_FORMAT_DMA_DRM)
      return _h265_update_caps_drm_format (s, max_chroma, max_bit_depth);
    return _h265_format_is_valid (format, max_chroma, max_bit_depth);
  } else if (GST_VALUE_HOLDS_LIST (val)) {
    GValue new_list = G_VALUE_INIT;

    guint len = gst_value_list_get_size (val);
    gst_value_list_init (&new_list, len);

    for (guint i = 0; i < len; i++) {
      const GValue *lval = gst_value_list_get_value (val, i);
      if (_h265_value_is_valid (lval, max_chroma, max_bit_depth))
        gst_value_list_append_value (&new_list, lval);
    }

    guint new_len = gst_value_list_get_size (&new_list);
    if (new_len == 1) {
      const GValue *new_val = gst_value_list_get_value (&new_list, 0);
      gst_structure_set_value (s, "format", new_val);
      g_value_unset (&new_list);
      return TRUE;
    } else if (new_len > 0) {
      gst_structure_set_value (s, "format", &new_list);
      g_value_unset (&new_list);
      return TRUE;
    }

    g_value_unset (&new_list);
  }

  return FALSE;
}

static GstCaps *
gst_h265_encoder_getcaps (GstVideoEncoder * encoder, GstCaps * filter)
{
  GstH265Encoder *self = GST_H265_ENCODER (encoder);
  GstCaps *templ_caps, *allowed, *supported_incaps, *fcaps;
  guint8 max_chroma = 0, max_bit_depth = 0;
  gboolean has_profile = FALSE;

  templ_caps =
      gst_pad_get_pad_template_caps (GST_VIDEO_ENCODER_SINK_PAD (encoder));
  allowed = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder));

  if (!allowed) {
    supported_incaps = templ_caps;
    goto bail;
  } else if (gst_caps_is_empty (allowed)) {
    gst_caps_unref (templ_caps);
    return allowed;
  }

  for (guint i = 0; i < gst_caps_get_size (allowed); i++) {
    const GstStructure *allowed_s = gst_caps_get_structure (allowed, i);
    const GValue *val = gst_structure_get_value (allowed_s, "profile");

    if (!val)
      continue;

    if (G_VALUE_HOLDS_STRING (val)) {
      _h265_check_formats (g_value_get_string (val), &max_chroma,
          &max_bit_depth, &has_profile);
    } else if (GST_VALUE_HOLDS_LIST (val)) {
      for (guint j = 0; j < gst_value_list_get_size (val); j++) {
        const GValue *lval = gst_value_list_get_value (val, j);
        if (G_VALUE_HOLDS_STRING (lval))
          _h265_check_formats (g_value_get_string (lval), &max_chroma,
              &max_bit_depth, &has_profile);
      }
    }
  }

  if (!has_profile) {
    supported_incaps = templ_caps;
    goto bail;
  }

  supported_incaps = gst_caps_copy (templ_caps);

  for (guint i = 0; i < gst_caps_get_size (supported_incaps); i++) {
    GstStructure *s = gst_caps_get_structure (supported_incaps, i);
    if (!_h265_update_caps_format (s, max_chroma, max_bit_depth)) {
      gst_caps_unref (supported_incaps);
      supported_incaps = templ_caps;
      goto bail;
    }
  }

  gst_caps_unref (templ_caps);

bail:
  GST_LOG_OBJECT (self, "supported caps %" GST_PTR_FORMAT, supported_incaps);
  fcaps = gst_video_encoder_proxy_getcaps (encoder, supported_incaps, filter);
  gst_clear_caps (&supported_incaps);
  gst_clear_caps (&allowed);

  GST_LOG_OBJECT (self, "proxy caps %" GST_PTR_FORMAT, fcaps);
  return fcaps;
}

static void
gst_h265_encoder_init (GstH265Encoder * self)
{
  GstH265EncoderPrivate *priv = _GET_PRIV (self);

  g_queue_init (&priv->output_list);
  g_queue_init (&priv->ref_list);
  g_queue_init (&priv->reorder_list);

  priv->dts_queue = gst_vec_deque_new_for_struct (sizeof (GstClockTime), 8);

  priv->frame_duration = GST_CLOCK_TIME_NONE;

  priv->config.max_num_reference_list0 = 1;
  priv->config.max_num_reference_list1 = 0;
  priv->config.b_pyramid_supported = TRUE;
  priv->config.preferred_output_delay = 0;

  priv->list_forward = g_array_sized_new (FALSE, TRUE,
      sizeof (GstH265EncoderFrame *), 16);
  priv->list_backward = g_array_sized_new (FALSE, TRUE,
      sizeof (GstH265EncoderFrame *), 16);
  priv->list_negative = g_array_sized_new (FALSE, FALSE, sizeof (gint), 16);
  priv->list_positive = g_array_sized_new (FALSE, FALSE, sizeof (gint), 16);

  /* default values */
  priv->prop.idr_period = H265ENC_IDR_PERIOD_DEFAULT;
  priv->prop.num_bframes = H265ENC_B_FRAMES_DEFAULT;
  priv->prop.num_iframes = H265ENC_I_FRAMES_DEFAULT;
  priv->prop.num_ref_frames = H265ENC_NUM_REF_FRAMES_DEFAULT;
  priv->prop.b_pyramid = H265ENC_B_PYRAMID_DEFAULT;
}

static void
gst_h265_encoder_dispose (GObject * object)
{
  GstH265Encoder *self = GST_H265_ENCODER (object);
  GstH265EncoderPrivate *priv = _GET_PRIV (self);

  gst_h265_encoder_flush_lists (self);

  gst_clear_object (&priv->gop.mapper);
  g_clear_pointer (&priv->dts_queue, gst_vec_deque_free);
  g_clear_pointer (&priv->list_forward, g_array_unref);
  g_clear_pointer (&priv->list_backward, g_array_unref);
  g_clear_pointer (&priv->list_negative, g_array_unref);
  g_clear_pointer (&priv->list_positive, g_array_unref);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_h265_encoder_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstH265Encoder *self = GST_H265_ENCODER (object);
  GstH265EncoderPrivate *priv = _GET_PRIV (self);

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
gst_h265_encoder_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstH265Encoder *self = GST_H265_ENCODER (object);
  GstH265EncoderPrivate *priv = _GET_PRIV (self);

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
gst_h265_encoder_class_init (GstH265EncoderClass * klass)
{
  GstVideoEncoderClass *encoder_class = GST_VIDEO_ENCODER_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamFlags param_flags =
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT;

  object_class->get_property = gst_h265_encoder_get_property;
  object_class->set_property = gst_h265_encoder_set_property;
  object_class->dispose = gst_h265_encoder_dispose;

  encoder_class->start = GST_DEBUG_FUNCPTR (gst_h265_encoder_start);
  encoder_class->stop = GST_DEBUG_FUNCPTR (gst_h265_encoder_stop);
  encoder_class->set_format = GST_DEBUG_FUNCPTR (gst_h265_encoder_set_format);
  encoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_h265_encoder_handle_frame);
  encoder_class->flush = GST_DEBUG_FUNCPTR (gst_h265_encoder_flush);
  encoder_class->finish = GST_DEBUG_FUNCPTR (gst_h265_encoder_finish);
  encoder_class->getcaps = GST_DEBUG_FUNCPTR (gst_h265_encoder_getcaps);

  klass->negotiate = GST_DEBUG_FUNCPTR (gst_h265_encoder_negotiate_default);

  /**
   * GstH265Encoder:idr-period:
   *
   * Maximum number of frames between two IDR frames. A higher value will result
   * in a larger IDR frame interval and thus slowdown seeking; a lower value will
   * result in a shorter IDR frame interval and thus improve seeking. As a rule
   * of thumb, the IDR period shouldn't be lower than the framerate of the video
   * multiplied by a factor between the range 1..10
   *
   * Set 0 for auto-calculate it.
   *
   * Since: 1.30
   */
  properties[PROP_IDR_PERIOD] = g_param_spec_uint ("idr-period",
      "Maximum GOP size", "Maximum number of frames between two IDR frames",
      0, MIN (G_MAXINT, (1 << 16) - 1), H265ENC_IDR_PERIOD_DEFAULT,
      param_flags);

  /**
   * GstH265Encoder:b-frames:
   *
   * Maximum number of consecutive B-Frames. B-Frames refer to both, the
   * previous and the following I-Frame (or P-Frame). This way B-Frames can
   * compress even more efficient that P-Frames.
   *
   * The availability of B-frames depends on the driver.
   *
   * Since: 1.30
   */
  properties[PROP_BFRAMES] = g_param_spec_uint ("b-frames", "B Frames",
      "Maximum number of consecutive B frames between I and P reference frames",
      0, 31, H265ENC_B_FRAMES_DEFAULT, param_flags);

  /**
   * GstH265Encoder:i-frames:
   *
   * Force the number of i-frames insertion within one GOP. More I-Frames will
   * increase the size of the video, but it will be more resilient to data
   * lose.
   *
   * Since: 1.30
   */
  properties[PROP_IFRAMES] = g_param_spec_uint ("i-frames", "I Frames",
      "Force the number of I frames insertion within one GOP, not including the "
      "first IDR frame", 0, G_MAXINT, H265ENC_I_FRAMES_DEFAULT, param_flags);

  /**
   * GstH265Encoder:num-ref-frames:
   *
   * The number of frames can be referenced by P-Frames and B-Frames. Higher
   * values will usually result in a more efficient compression, which means
   * better visual quality at the same file size, but it may require encoding
   * time.
   *
   * Since: 1.30
   */
  properties[PROP_NUM_REF_FRAMES] = g_param_spec_int ("num-ref-frames",
      "Number of reference frames", "Number of frames referenced by P and B "
      "frames", 0, 16, H265ENC_NUM_REF_FRAMES_DEFAULT, param_flags);

  /**
   * GstH265Encoder:b-pyramid:
   *
   * Enable the b-pyramid reference structure in GOP. It allows to make
   * references non-linearly in order to improve bitrate usage and quality. This
   * way B-Frames can refer to B-Frames.
   *
   * It only works with "high" profile.
   *
   * Since: 1.30
   */
  properties[PROP_B_PYRAMID] = g_param_spec_boolean ("b-pyramid", "b pyramid",
      "Enable the b-pyramid reference structure in the GOP",
      H265ENC_B_PYRAMID_DEFAULT, param_flags);

  g_object_class_install_properties (object_class, N_PROPERTIES, properties);
}

/**
 * gst_h265_encoder_set_max_num_references:
 * @self: A #GstH265Encoder
 * @list0: the maximum number of reference pictures for list L0
 * @list1: the maximum number of reference pictures for list L1
 *
 * Set the maximum number of reference pictures allowed by the accelerator.
 *
 * Since: 1.30
 */
void
gst_h265_encoder_set_max_num_references (GstH265Encoder * self, guint list0,
    guint list1)
{
  GstH265EncoderPrivate *priv;

  g_return_if_fail (GST_IS_H265_ENCODER (self));

  priv = _GET_PRIV (self);

  priv->config.max_num_reference_list0 = list0;
  priv->config.max_num_reference_list1 = list1;
}

/**
 * gst_h265_encoder_set_b_pyramid_supported:
 * @self: A #GstH265Encoder
 * @supported: whether the accelerator supports B pictures as references in
 *     both reference lists, L0 and L1
 *
 * Set whether the accelerator supports B pictures as references in both
 * reference lists, L0 and L1, as required by the B-pyramid reference
 * structure. If it is not supported, the B-pyramid will be disabled.
 *
 * Since: 1.30
 */
void
gst_h265_encoder_set_b_pyramid_supported (GstH265Encoder * self,
    gboolean supported)
{
  GstH265EncoderPrivate *priv;

  g_return_if_fail (GST_IS_H265_ENCODER (self));

  priv = _GET_PRIV (self);

  priv->config.b_pyramid_supported = supported;
}

/**
 * gst_h265_encoder_set_surface_size:
 * @self: A #GstH265Encoder
 * @width: picture width in luma samples
 * @height: picture height in luma samples
 *
 * Set the picture size in luma samples for the current segment.
 */
void
gst_h265_encoder_set_surface_size (GstH265Encoder * self, gint width,
    gint height)
{
  GstH265EncoderPrivate *priv;

  g_return_if_fail (GST_IS_H265_ENCODER (self));

  priv = _GET_PRIV (self);
  priv->config.surface_height = height;
  priv->config.surface_width = width;
}

/**
 * gst_h265_encoder_is_live:
 * @self: a #GstH265Encoder
 *
 * Returns: whether the current stream is live
 *
 * Since: 1.30
 */
gboolean
gst_h265_encoder_is_live (GstH265Encoder * self)
{
  GstH265EncoderPrivate *priv;

  g_return_val_if_fail (GST_IS_H265_ENCODER (self), FALSE);

  priv = _GET_PRIV (self);
  return priv->is_live;
}

/**
 * gst_h265_encoder_set_preferred_output_delay:
 * @self: a #GstH265Encoder
 * @delay: the number of frames to hold and process
 *
 * Some accelerators such as Intel VA-API has better performance if it holds a
 * group of frames to process.
 *
 * Since: 1.30
 */
void
gst_h265_encoder_set_preferred_output_delay (GstH265Encoder * self, guint delay)
{
  GstH265EncoderPrivate *priv;

  g_return_if_fail (GST_IS_H265_ENCODER (self));

  priv = _GET_PRIV (self);
  priv->config.preferred_output_delay = delay;
}

/**
 * gst_h265_encoder_reconfigure:
 * @self: a #GstH265Encoder
 * @force: whether if configuration will run now or for next input frame
 *
 * Through this method the subclass can request the encoder reconfiguration
 * and downstream renegotiation.
 *
 * Since: 1.30
 */
gboolean
gst_h265_encoder_reconfigure (GstH265Encoder * self, gboolean force)
{
  GstH265EncoderPrivate *priv;

  g_return_val_if_fail (GST_IS_H265_ENCODER (self), FALSE);

  priv = _GET_PRIV (self);

  if (!force) {
    g_atomic_int_set (&priv->need_configure, TRUE);
    return TRUE;
  } else {
    if (g_atomic_int_compare_and_exchange (&priv->need_configure, TRUE, FALSE)) {
      return (gst_h265_encoder_configure (self) == GST_FLOW_OK);
    }
    return TRUE;
  }
}

/**
 * gst_h265_encoder_get_idr_period:
 * @self: a #GstH265Encoder
 *
 * Returns the IDR period property without the marshalling burden of GObject
 * properties.
 *
 * Returns: the IDR period
 *
 * Since: 1.30
 */
guint32
gst_h265_encoder_get_idr_period (GstH265Encoder * self)
{
  GstH265EncoderPrivate *priv;
  guint32 ret;

  g_return_val_if_fail (GST_IS_H265_ENCODER (self), -1);

  priv = _GET_PRIV (self);

  GST_OBJECT_LOCK (self);
  ret = priv->prop.idr_period;
  GST_OBJECT_UNLOCK (self);

  return ret;
}

/**
 * gst_h265_encoder_get_num_b_frames:
 * @self: a #GstH265Encoder
 *
 * Returns the number of consecutive B-Frames without the marshalling burden of
 * GObject properties.
 *
 * Returns: the number of consecutive B-Frames
 *
 * Since: 1.30
 */
guint32
gst_h265_encoder_get_num_b_frames (GstH265Encoder * self)
{
  GstH265EncoderPrivate *priv;
  guint32 ret;

  g_return_val_if_fail (GST_IS_H265_ENCODER (self), -1);

  priv = _GET_PRIV (self);

  GST_OBJECT_LOCK (self);
  ret = priv->prop.num_bframes;
  GST_OBJECT_UNLOCK (self);

  return ret;
}

/**
 * gst_h265_encoder_gop_is_b_pyramid:
 * @self: a #GstH265Encoder
 *
 * Returns whether the GOP has a b-pyramid structure.
 *
 * Returns: %TRUE if GOP has a b-pyramid structure
 *
 * Since: 1.30
 */
gboolean
gst_h265_encoder_gop_is_b_pyramid (GstH265Encoder * self)
{
  GstH265EncoderPrivate *priv;
  gboolean ret;

  g_return_val_if_fail (GST_IS_H265_ENCODER (self), FALSE);

  priv = _GET_PRIV (self);

  GST_OBJECT_LOCK (self);
  ret = priv->prop.b_pyramid;
  GST_OBJECT_UNLOCK (self);

  return ret;
}

/**
 * gst_h265_encoder_generate_gop_structure:
 * @self: a #GstH265Encoder
 * @dpb_size: (out) (optional): the calculated DBP size
 *
 * Generate a GOP structure using the GstH26xMapper given the parameters.
 *
 * Returns: whether the GOP map could be generated.
 */
gboolean
gst_h265_encoder_generate_gop_structure (GstH265Encoder * self,
    guint * dpb_size)
{
  GstH265EncoderPrivate *priv;
  gboolean ret;

  g_return_val_if_fail (GST_IS_H265_ENCODER (self), FALSE);

  priv = _GET_PRIV (self);

  ret = gst_h265_encoder_generate_gop_structure_internal (self);
  if (ret) {
    if (dpb_size)
      *dpb_size = priv->gop.max_dec_pic_buffering;
  }

  return ret;
}

static const GstH265ProfileDescriptor *
gst_h265_get_profile_descriptor (const GstH265ProfileTierLevel * ptl)
{
  if (ptl->profile_space > 0)
    return NULL;

  for (int i = 0; i < G_N_ELEMENTS (_h265_profiles); i++) {
    const GstH265ProfileDescriptor *profile = &_h265_profiles[i];

    if (ptl->profile_idc > 0 && ptl->profile_idc != profile->profile_idc)
      continue;
    /* as profile_idc comes from _h265_profiles it cannot be out-of-bound */
    if (ptl->profile_compatibility_flag[profile->profile_idc] == 0)
      continue;

#define check_flag(flag) \
    if (profile->flag < 2) { \
      if (profile->flag != ptl->G_PASTE(flag, _constraint_flag)) \
        continue;  \
    }
    check_flag (max_14bit);
    check_flag (max_12bit);
    check_flag (max_10bit);
    check_flag (max_8bit);
    check_flag (max_422chroma);
    check_flag (max_420chroma);
    check_flag (max_monochrome);
    check_flag (intra);
    check_flag (one_picture_only);
    check_flag (lower_bit_rate);
#undef check_flag

    return profile;
  }

  return NULL;
}


/**
 * gst_h265_get_level_descriptor:
 * @ptl: a #GstH265ProfileTierLevel
 * @bitrate: bit rate in bits per second
 * @in_info: raw stream's #GstVideoInfo
 * @n_slices: the number of slices
 * @tile_rows: number of rows in tiles
 * @tile_cols: number of columns in tiles
 * @max_dec_pic_buffering: the max size of DPB
 *
 * Returns: #GStH265LevelDescriptor associated with associated with @ptl,
 *   @bitrate, framesize in @in_info, @n_slices, @tile_rows, @tile_cols, and
 *   @max_dec_pic_buffering. If no descriptor found, it returns %NULL.
 *
 * Since: 1.30
 */
const GstH265LevelDescriptor *
gst_h265_get_level_descriptor (const GstH265ProfileTierLevel * ptl,
    guint64 bitrate, GstVideoInfo * in_info, int n_slices, int tile_rows,
    int tile_cols, int max_dec_pic_buffering)
{
  const GstH265ProfileDescriptor *profile;
  gint pic_size, tier_flag, lbr_flag, hbr_factor, width, height;

  g_return_val_if_fail (in_info, NULL);

  if (ptl) {
    profile = gst_h265_get_profile_descriptor (ptl);
    tier_flag = ptl->tier_flag;
    lbr_flag = ptl->lower_bit_rate_constraint_flag;
  } else {
    profile = &_h265_profiles[0];       /* Main profile as default */
    tier_flag = 0;
    lbr_flag = profile->lower_bit_rate > 0;
  }

  if (!profile)
    profile = &_h265_profiles[0];       /* Main profile as default */

  width = GST_VIDEO_INFO_WIDTH (in_info);
  height = GST_VIDEO_INFO_HEIGHT (in_info);
  pic_size = width * height;

  if (profile->profile_idc == GST_H265_PROFILE_IDC_MAIN
      || profile->profile_idc == GST_H265_PROFILE_IDC_MAIN_10) {
    hbr_factor = 1;
  } else if (profile->high_throughput) {
    if (profile->intra)
      hbr_factor = 24 - 12 * lbr_flag;
    else
      hbr_factor = 6;
  } else {
    hbr_factor = 2 - lbr_flag;
  }

  for (int i = 0; i < G_N_ELEMENTS (_h265_levels); i++) {
    const GstH265LevelDescriptor *level = &_h265_levels[i];
    int max_br, max_dpb_size;

    if (tier_flag && !level->max_br_tier_high)
      continue;
    if (pic_size > level->max_luma_ps)
      continue;
    if (width * width > 8 * level->max_luma_ps)
      continue;
    if (height * height > 8 * level->max_luma_ps)
      continue;

    if (n_slices > level->max_slice_segments_per_picture)
      continue;
    if (tile_rows > level->max_tile_rows)
      continue;
    if (tile_cols > level->max_tile_columns)
      continue;

    if (tier_flag)
      max_br = level->max_br_tier_high;
    else
      max_br = level->max_br_tier_main;
    if (max_br == 0)
      continue;

    if (bitrate > (guint64) profile->cpb_nal_factor * hbr_factor * max_br)
      continue;

    if (pic_size <= (level->max_luma_ps >> 2))
      max_dpb_size = MIN (4 * profile->max_dpb_pic_buf, 16);
    else if (pic_size <= (level->max_luma_ps >> 1))
      max_dpb_size = MIN (2 * profile->max_dpb_pic_buf, 16);
    else if (pic_size <= (3 * level->max_luma_ps >> 2))
      max_dpb_size = MIN (4 * profile->max_dpb_pic_buf / 3, 16);
    else
      max_dpb_size = profile->max_dpb_pic_buf;
    if (max_dec_pic_buffering > max_dpb_size)
      continue;

    return level;
  }

  GST_ERROR ("Failed to find a suitable level");
  return NULL;
}

/* Maximum sizes for common headers (in bits) */
#define GST_H265_MAX_PROFILE_TIER_LEVEL_SIZE 684
#define GST_H265_MAX_VPS_HDR_SIZE 13781
#define GST_H265_MAX_SPS_HDR_SIZE 615
#define GST_H265_MAX_SHORT_TERM_REFPICSET_SIZE 55
#define GST_H265_MAX_VUI_PARAMS_SIZE 267
#define GST_H265_MAX_HRD_PARAMS_SIZE 8196
#define GST_H265_MAX_PPS_HDR_SIZE 274
#define GST_H265_MAX_SLICE_HDR_SIZE 33660

/**
 * gst_h265_calculate_coded_size:
 * @sps: (skip): the #GstH265SPS
 * @num_slices: number of slices to encode per frame
 *
 * Returns the calculated size of the encoded buffer.
 *
 * Since: 1.30
 */
gsize
gst_h265_calculate_coded_size (const GstH265SPS * sps, guint num_slices)
{
  gsize codedbuf_size = 0;
  guint i;

  g_return_val_if_fail (sps && num_slices >= 1, 0);

  codedbuf_size =
      sps->pic_width_in_luma_samples * sps->pic_height_in_luma_samples;

  switch (sps->chroma_format_idc) {
    case GST_CHROMA_400:
    case GST_CHROMA_420:
      codedbuf_size = codedbuf_size * 3 / 2;
      break;
    case GST_CHROMA_422:
      codedbuf_size = codedbuf_size * 2;
      break;
    case GST_CHROMA_444:
      codedbuf_size = codedbuf_size * 3;
      break;
    default:
      g_assert_not_reached ();
  }

  for (i = 0; i < G_N_ELEMENTS (_h265_levels); i++) {
    const GstH265LevelDescriptor *level = &_h265_levels[i];
    if (level->level_idc == sps->profile_tier_level.level_idc)
      break;
  }

  if (i == G_N_ELEMENTS (_h265_levels)) {
    GST_ERROR ("Invalid level IDC %d", sps->profile_tier_level.level_idc);
    return 0;
  }

  /* overflow chances are very low */
  codedbuf_size = gst_util_uint64_scale (codedbuf_size,
      8 + sps->bit_depth_luma_minus8, 8);
  g_assert (_h265_levels[i].min_cr_main >= 2);
  codedbuf_size =
      codedbuf_size / (_h265_levels[i].min_cr_main / 2 /* for safety */ );

  /* FIXME: Using only a rough approximation for bitstream headers.
   * Not taken into account: ScalingList, RefPicListModification,
   * PredWeightTable, which is not used now. */

  /* Calculate the maximum sizes for common headers (in bits) */

  /* Account for VPS header */
  codedbuf_size +=
      4 /* start code */  + GST_ROUND_UP_8 (GST_H265_MAX_VPS_HDR_SIZE +
      GST_H265_MAX_PROFILE_TIER_LEVEL_SIZE + GST_H265_MAX_HRD_PARAMS_SIZE) / 8;

  /* Account for SPS header */
  codedbuf_size +=
      4 + GST_ROUND_UP_8 (GST_H265_MAX_SPS_HDR_SIZE +
      GST_H265_MAX_PROFILE_TIER_LEVEL_SIZE +
      64 * GST_H265_MAX_SHORT_TERM_REFPICSET_SIZE +
      GST_H265_MAX_VUI_PARAMS_SIZE + GST_H265_MAX_HRD_PARAMS_SIZE) / 8;

  /* Account for PPS header */
  codedbuf_size += 4 + GST_ROUND_UP_8 (GST_H265_MAX_PPS_HDR_SIZE) / 8;

  /* Account for slice header */
  codedbuf_size +=
      (guint64) num_slices *(4 + GST_ROUND_UP_8 (GST_H265_MAX_SLICE_HDR_SIZE +
          GST_H265_MAX_SHORT_TERM_REFPICSET_SIZE) / 8);

  return codedbuf_size;
}
