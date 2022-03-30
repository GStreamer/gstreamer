/* GStreamer
 *  Copyright (C) 2021 Intel Corporation
 *     Author: He Junyan <junyan.he@intel.com>
 *     Author: Víctor Jáquez <vjaquez@igalia.com>
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
 * SECTION:element-vah264enc
 * @title: vah264enc
 * @short_description: A VA-API based H264 video encoder
 *
 * vah264enc encodes raw video VA surfaces into H.264 bitstreams using
 * the installed and chosen [VA-API](https://01.org/linuxmedia/vaapi)
 * driver.
 *
 * The raw video frames in main memory can be imported into VA surfaces.
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 videotestsrc num-buffers=60 ! timeoverlay ! vah264enc ! h264parse ! mp4mux ! filesink location=test.mp4
 * ```
 *
 * Since: 1.22
 *
 */

 /* @TODO:
  * 1. Look ahead, which can optimize the slice type and QP.
  * 2. Field encoding.
  * 3. The stereo encoding such as the frame-packing or MVC.
  * 4. Weight prediction of B frame.
  */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif


#include "gstvah264enc.h"

#include <gst/codecparsers/gsth264bitwriter.h>
#include <gst/va/gstva.h>
#include <gst/va/vasurfaceimage.h>
#include <gst/video/video.h>
#include <va/va_drmcommon.h>

#include "vacompat.h"
#include "gstvaencoder.h"
#include "gstvacaps.h"
#include "gstvaprofile.h"
#include "gstvadisplay_priv.h"

GST_DEBUG_CATEGORY_STATIC (gst_va_h264enc_debug);
#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT gst_va_h264enc_debug
#else
#define GST_CAT_DEFAULT NULL
#endif

typedef struct _GstVaH264Enc GstVaH264Enc;
typedef struct _GstVaH264EncClass GstVaH264EncClass;
typedef struct _GstVaH264EncFrame GstVaH264EncFrame;
typedef struct _GstVaH264LevelLimits GstVaH264LevelLimits;

#define GST_VA_H264_ENC(obj)            ((GstVaH264Enc *) obj)
#define GST_VA_H264_ENC_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), G_TYPE_FROM_INSTANCE (obj), GstVaH264EncClass))
#define GST_VA_H264_ENC_CLASS(klass)    ((GstVaH264EncClass *) klass)

enum
{
  PROP_KEY_INT_MAX = 1,
  PROP_BFRAMES,
  PROP_IFRAMES,
  PROP_NUM_REF_FRAMES,
  PROP_B_PYRAMID,
  PROP_NUM_SLICES,
  PROP_MIN_QP,
  PROP_MAX_QP,
  PROP_QP_I,
  PROP_QP_P,
  PROP_QP_B,
  PROP_DCT8X8,
  PROP_CABAC,
  PROP_TRELLIS,
  PROP_MBBRC,
  PROP_BITRATE,
  PROP_TARGET_PERCENTAGE,
  PROP_TARGET_USAGE,
  PROP_RATE_CONTROL,
  PROP_CPB_SIZE,
  PROP_AUD,
  PROP_DEVICE_PATH,
  N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES];

/* Scale factor for bitrate (HRD bit_rate_scale: min = 6) */
#define SX_BITRATE 6
/* Scale factor for CPB size (HRD cpb_size_scale: min = 4) */
#define SX_CPB_SIZE 4
/* Maximum sizes for common headers (in bits) */
#define MAX_SPS_HDR_SIZE  16473
#define MAX_VUI_PARAMS_SIZE  210
#define MAX_HRD_PARAMS_SIZE  4103
#define MAX_PPS_HDR_SIZE  101
#define MAX_SLICE_HDR_SIZE  397 + 2572 + 6670 + 2402

#define MAX_GOP_SIZE  1024

static GstObjectClass *parent_class = NULL;

/* *INDENT-OFF* */
struct _GstVaH264EncClass
{
  GstVideoEncoderClass parent_class;

  GstVaCodecs codec;
  gchar *render_device_path;

  gboolean (*reconfig)       (GstVaH264Enc * encoder);
  gboolean (*push_frame)     (GstVaH264Enc * encoder,
                              GstVideoCodecFrame * frame,
                              gboolean last);
  gboolean (*pop_frame)      (GstVaH264Enc * encoder,
                              GstVideoCodecFrame ** out_frame);
  gboolean (*encode_frame)   (GstVaH264Enc * encoder,
                              GstVideoCodecFrame * frame);
};
/* *INDENT-ON* */

struct _GstVaH264Enc
{
  /*< private > */
  GstVideoEncoder parent_instance;

  GstVaDisplay *display;

  gint width;
  gint height;
  VAProfile profile;
  VAEntrypoint entrypoint;
  guint rt_format;
  guint codedbuf_size;
  /* properties */
  struct
  {
    /* kbps */
    guint bitrate;
    /* VA_RC_XXX */
    guint rc_ctrl;
    guint key_int_max;
    guint32 num_ref_frames;
    gboolean b_pyramid;
    guint32 num_bframes;
    guint32 num_iframes;
    guint32 min_qp;
    guint32 max_qp;
    guint32 qp_i;
    guint32 qp_p;
    guint32 qp_b;
    gboolean use_cabac;
    gboolean use_dct8x8;
    gboolean use_trellis;
    gboolean aud;
    guint32 mbbrc;
    guint32 num_slices;
    guint32 cpb_size;
    guint32 target_percentage;
    guint32 target_usage;
  } prop;

  GstVideoCodecState *input_state;
  GstVideoCodecState *output_state;
  GstCaps *in_caps;
  GstVideoInfo in_info;
  GstVideoInfo sinkpad_info;
  GstBufferPool *raw_pool;

  GstClockTime start_pts;
  GstClockTime frame_duration;
  /* Total frames we handled since reconfig. */
  guint input_frame_count;
  guint output_frame_count;

  GstVaEncoder *encoder;

  GQueue reorder_list;
  GQueue ref_list;

  GQueue output_list;

  /* H264 fields */
  gint mb_width;
  gint mb_height;
  guint8 level_idc;
  const gchar *level_str;
  /* Minimum Compression Ratio (A.3.1) */
  guint min_cr;
  gboolean use_cabac;
  gboolean use_dct8x8;
  gboolean use_trellis;
  guint32 num_slices;
  guint32 packed_headers;

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
    struct
    {
      guint8 slice_type;
      gboolean is_ref;
      guint8 pyramid_level;
      /* Only for b pyramid */
      gint left_ref_poc_diff;
      gint right_ref_poc_diff;
    } frame_types[MAX_GOP_SIZE];
    /* current index in the frames types map. */
    guint cur_frame_index;
    /* Number of ref frames within current GOP. H264's frame num. */
    gint cur_frame_num;
    /* Max frame num within a GOP. */
    guint32 max_frame_num;
    guint32 log2_max_frame_num;
    /* Max poc within a GOP. */
    guint32 max_pic_order_cnt;
    guint32 log2_max_pic_order_cnt;

    /* Total ref frames of list0 and list1. */
    guint32 num_ref_frames;
    guint32 ref_num_list0;
    guint32 ref_num_list1;

    guint num_reorder_frames;
  } gop;

  struct
  {
    guint target_usage;
    guint32 rc_ctrl_mode;

    guint32 min_qp;
    guint32 max_qp;
    guint32 qp_i;
    guint32 qp_p;
    guint32 qp_b;
    /* macroblock bitrate control */
    guint32 mbbrc;
    guint target_bitrate;
    guint target_percentage;
    guint max_bitrate;
    /* bitrate (bits) */
    guint max_bitrate_bits;
    guint target_bitrate_bits;
    /* length of CPB buffer */
    guint cpb_size;
    /* length of CPB buffer (bits) */
    guint cpb_length_bits;
  } rc;

  GstH264SPS sequence_hdr;
};

struct _GstVaH264EncFrame
{
  GstVaEncodePicture *picture;
  GstH264SliceType type;
  gboolean is_ref;
  guint pyramid_level;
  /* Only for b pyramid */
  gint left_ref_poc_diff;
  gint right_ref_poc_diff;

  gint poc;
  gint frame_num;
  gboolean last_frame;
  /* The pic_num will be marked as unused_for_reference, which is
   * replaced by this frame. -1 if we do not need to care about it
   * explicitly. */
  gint unused_for_reference_pic_num;

  /* The total frame count we handled. */
  guint total_frame_count;
};

/**
 * GstVaH264LevelLimits:
 * @name: the level name
 * @level_idc: the H.264 level_idc value
 * @MaxMBPS: the maximum macroblock processing rate (MB/sec)
 * @MaxFS: the maximum frame size (MBs)
 * @MaxDpbMbs: the maxium decoded picture buffer size (MBs)
 * @MaxBR: the maximum video bit rate (kbps)
 * @MaxCPB: the maximum CPB size (kbits)
 * @MinCR: the minimum Compression Ratio
 *
 * The data structure that describes the limits of an H.264 level.
 */
struct _GstVaH264LevelLimits
{
  const gchar *name;
  guint8 level_idc;
  guint32 MaxMBPS;
  guint32 MaxFS;
  guint32 MaxDpbMbs;
  guint32 MaxBR;
  guint32 MaxCPB;
  guint32 MinCR;
};

/* Table A-1 - Level limits */
/* *INDENT-OFF* */
static const GstVaH264LevelLimits _va_h264_level_limits[] = {
  /* level   idc   MaxMBPS   MaxFS   MaxDpbMbs  MaxBR   MaxCPB  MinCr */
  {  "1",    10,   1485,     99,     396,       64,     175,    2 },
  {  "1b",   11,   1485,     99,     396,       128,    350,    2 },
  {  "1.1",  11,   3000,     396,    900,       192,    500,    2 },
  {  "1.2",  12,   6000,     396,    2376,      384,    1000,   2 },
  {  "1.3",  13,   11880,    396,    2376,      768,    2000,   2 },
  {  "2",    20,   11880,    396,    2376,      2000,   2000,   2 },
  {  "2.1",  21,   19800,    792,    4752,      4000,   4000,   2 },
  {  "2.2",  22,   20250,    1620,   8100,      4000,   4000,   2 },
  {  "3",    30,   40500,    1620,   8100,      10000,  10000,  2 },
  {  "3.1",  31,   108000,   3600,   18000,     14000,  14000,  4 },
  {  "3.2",  32,   216000,   5120,   20480,     20000,  20000,  4 },
  {  "4",    40,   245760,   8192,   32768,     20000,  25000,  4 },
  {  "4.1",  41,   245760,   8192,   32768,     50000,  62500,  2 },
  {  "4.2",  42,   522240,   8704,   34816,     50000,  62500,  2 },
  {  "5",    50,   589824,   22080,  110400,    135000, 135000, 2 },
  {  "5.1",  51,   983040,   36864,  184320,    240000, 240000, 2 },
  {  "5.2",  52,   2073600,  36864,  184320,    240000, 240000, 2 },
  {  "6",    60,   4177920,  139264, 696320,    240000, 240000, 2 },
  {  "6.1",  61,   8355840,  139264, 696320,    480000, 480000, 2 },
  {  "6.2",  62,  16711680,  139264, 696320,    800000, 800000, 2 },
};
/* *INDENT-ON* */

static const gchar *
_slice_type_name (GstH264SliceType type)
{
  switch (type) {
    case GST_H264_P_SLICE:
      return "P";
    case GST_H264_B_SLICE:
      return "B";
    case GST_H264_I_SLICE:
      return "I";
    default:
      g_assert_not_reached ();
  }

  return NULL;
}

static const gchar *
_rate_control_get_name (guint32 rc_mode)
{
  switch (rc_mode) {
    case VA_RC_CBR:
      return "cbr";
    case VA_RC_VBR:
      return "vbr";
    case VA_RC_VCM:
      return "vcm";
    case VA_RC_CQP:
      return "cqp";
    default:
      return "unknown";
  }

  g_assert_not_reached ();
  return NULL;
}

static GstVaH264EncFrame *
gst_va_enc_frame_new (void)
{
  GstVaH264EncFrame *frame;

  frame = g_slice_new (GstVaH264EncFrame);
  frame->last_frame = FALSE;
  frame->frame_num = 0;
  frame->unused_for_reference_pic_num = -1;
  frame->picture = NULL;
  frame->total_frame_count = 0;

  return frame;
}

static void
gst_va_enc_frame_free (gpointer pframe)
{
  GstVaH264EncFrame *frame = pframe;
  g_clear_pointer (&frame->picture, gst_va_encode_picture_free);
  g_slice_free (GstVaH264EncFrame, frame);
}

static inline GstVaH264EncFrame *
_enc_frame (GstVideoCodecFrame * frame)
{
  GstVaH264EncFrame *enc_frame = gst_video_codec_frame_get_user_data (frame);
  g_assert (enc_frame);
  return enc_frame;
}

/* Normalizes bitrate (and CPB size) for HRD conformance */
static void
_calculate_bitrate_hrd (GstVaH264Enc * self)
{
  guint bitrate_bits, cpb_bits_size;

  /* Round down bitrate. This is a hard limit mandated by the user */
  g_assert (SX_BITRATE >= 6);
  bitrate_bits = (self->rc.max_bitrate * 1000) & ~((1U << SX_BITRATE) - 1);
  GST_DEBUG_OBJECT (self, "Max bitrate: %u bits/sec", bitrate_bits);
  self->rc.max_bitrate_bits = bitrate_bits;

  bitrate_bits = (self->rc.target_bitrate * 1000) & ~((1U << SX_BITRATE) - 1);
  GST_DEBUG_OBJECT (self, "Target bitrate: %u bits/sec", bitrate_bits);
  self->rc.target_bitrate_bits = bitrate_bits;

  if (self->rc.cpb_size > 0 && self->rc.cpb_size < (self->rc.max_bitrate / 2)) {
    GST_INFO_OBJECT (self, "Too small cpb_size: %d", self->rc.cpb_size);
    self->rc.cpb_size = 0;
  }

  if (self->rc.cpb_size == 0) {
    /* We cache 2 second coded data by default. */
    self->rc.cpb_size = self->rc.max_bitrate * 2;
    GST_INFO_OBJECT (self, "Adjust cpb_size to: %d", self->rc.cpb_size);
  }

  /* Round up CPB size. This is an HRD compliance detail */
  g_assert (SX_CPB_SIZE >= 4);
  cpb_bits_size = (self->rc.cpb_size * 1000) & ~((1U << SX_CPB_SIZE) - 1);

  GST_DEBUG_OBJECT (self, "HRD CPB size: %u bits", cpb_bits_size);
  self->rc.cpb_length_bits = cpb_bits_size;
}

/* Estimates a good enough bitrate if none was supplied */
static void
_ensure_rate_control (GstVaH264Enc * self)
{
  /* User can specify the properties of: "bitrate", "target-percentage",
   * "max-qp", "min-qp", "qpi", "qpp", "qpb", "mbbrc", "cpb-size",
   * "rate-control" and "target-usage" to control the RC behavior.
   *
   * "target-usage" is different from the others, it controls the encoding
   * speed and quality, while the others control encoding bit rate and
   * quality. The lower value has better quality(maybe bigger MV search
   * range) but slower speed, the higher value has faster speed but lower
   * quality.
   *
   * The possible composition to control the bit rate and quality:
   *
   * 1. CQP mode: "rate-control=cqp", then "qpi", "qpp" and "qpb"
   *    specify the QP of I/P/B frames respectively(within the
   *    "max-qp" and "min-qp" range). The QP will not change during
   *    the whole stream. Other properties are ignored.
   *
   * 2. CBR mode: "rate-control=CBR", then the "bitrate" specify the
   *    target bit rate and the "cpb-size" specifies the max coded
   *    picture buffer size to avoid overflow. If the "bitrate" is not
   *    set, it is calculated by the picture resolution and frame
   *    rate. If "cpb-size" is not set, it is set to the size of
   *    caching 2 second coded data. Encoder will try its best to make
   *    the QP with in the ["max-qp", "min-qp"] range. "mbbrc" can
   *    enable bit rate control in macro block level. Other paramters
   *    are ignored.
   *
   * 3. VBR mode: "rate-control=VBR", then the "bitrate" specify the
   *    target bit rate, "target-percentage" is used to calculate the
   *    max bit rate of VBR mode by ("bitrate" * 100) /
   *    "target-percentage". It is also used by driver to calculate
   *    the min bit rate. The "cpb-size" specifies the max coded
   *    picture buffer size to avoid overflow. If the "bitrate" is not
   *    set, the target bit rate will be calculated by the picture
   *    resolution and frame rate. Encoder will try its best to make
   *    the QP with in the ["max-qp", "min-qp"] range. "mbbrc" can
   *    enable bit rate control in macro block level. Other paramters
   *    are ignored.
   *
   * 4. VCM mode: "rate-control=VCM", then the "bitrate" specify the
   *    target bit rate, and encoder will try its best to make the QP
   *    with in the ["max-qp", "min-qp"] range. Other paramters are
   *    ignored.
   */

  guint bitrate;
  guint32 rc_mode;
  guint32 quality_level;

  quality_level = gst_va_encoder_get_quality_level (self->encoder,
      self->profile, self->entrypoint);
  if (self->rc.target_usage > quality_level) {
    GST_INFO_OBJECT (self, "User setting target-usage: %d is not supported, "
        "fallback to %d", self->rc.target_usage, quality_level);
    self->rc.target_usage = quality_level;

    self->prop.target_usage = self->rc.target_usage;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TARGET_USAGE]);
  }

  /* TODO: find a better heuristics to infer a nearer control mode */
  rc_mode = gst_va_encoder_get_rate_control_mode (self->encoder,
      self->profile, self->entrypoint);
  if (!(rc_mode & self->prop.rc_ctrl)) {
    GST_INFO_OBJECT (self, "The race control mode %s is not supported, "
        "fallback to %s mode",
        _rate_control_get_name (self->prop.rc_ctrl),
        _rate_control_get_name (VA_RC_CQP));
    self->rc.rc_ctrl_mode = VA_RC_CQP;

    self->prop.rc_ctrl = self->rc.rc_ctrl_mode;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RATE_CONTROL]);
  }

  if (self->rc.min_qp > self->rc.max_qp) {
    GST_INFO_OBJECT (self, "The min_qp %d is bigger than the max_qp %d, "
        "set it to the max_qp", self->rc.min_qp, self->rc.max_qp);
    self->rc.min_qp = self->rc.max_qp;

    self->prop.min_qp = self->rc.min_qp;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MIN_QP]);
  }

  /* Make all the qp in the valid range */
  if (self->rc.qp_i < self->rc.min_qp) {
    if (self->rc.qp_i != 26)
      GST_INFO_OBJECT (self, "The qp_i %d is smaller than the min_qp %d, "
          "set it to the min_qp", self->rc.qp_i, self->rc.min_qp);
    self->rc.qp_i = self->rc.min_qp;
  }
  if (self->rc.qp_i > self->rc.max_qp) {
    if (self->rc.qp_i != 26)
      GST_INFO_OBJECT (self, "The qp_i %d is bigger than the max_qp %d, "
          "set it to the max_qp", self->rc.qp_i, self->rc.max_qp);
    self->rc.qp_i = self->rc.max_qp;
  }

  if (self->rc.qp_p < self->rc.min_qp) {
    if (self->rc.qp_p != 26)
      GST_INFO_OBJECT (self, "The qp_p %d is smaller than the min_qp %d, "
          "set it to the min_qp", self->rc.qp_p, self->rc.min_qp);
    self->rc.qp_p = self->rc.min_qp;
  }
  if (self->rc.qp_p > self->rc.max_qp) {
    if (self->rc.qp_p != 26)
      GST_INFO_OBJECT (self, "The qp_p %d is bigger than the max_qp %d, "
          "set it to the max_qp", self->rc.qp_p, self->rc.max_qp);
    self->rc.qp_p = self->rc.max_qp;
  }

  if (self->rc.qp_b < self->rc.min_qp) {
    if (self->rc.qp_b != 26)
      GST_INFO_OBJECT (self, "The qp_b %d is smaller than the min_qp %d, "
          "set it to the min_qp", self->rc.qp_b, self->rc.min_qp);
    self->rc.qp_b = self->rc.min_qp;
  }
  if (self->rc.qp_b > self->rc.max_qp) {
    if (self->rc.qp_b != 26)
      GST_INFO_OBJECT (self, "The qp_b %d is bigger than the max_qp %d, "
          "set it to the max_qp", self->rc.qp_b, self->rc.max_qp);
    self->rc.qp_b = self->rc.max_qp;
  }

  bitrate = self->prop.bitrate;
  /* Calculate a bitrate is not set. */
  if ((self->rc.rc_ctrl_mode == VA_RC_CBR || self->rc.rc_ctrl_mode == VA_RC_VBR
          || self->rc.rc_ctrl_mode == VA_RC_VCM) && bitrate == 0) {
    /* Default compression: 48 bits per macroblock in "high-compression" mode */
    guint bits_per_mb = 48;
    guint64 factor;

    /* According to the literature and testing, CABAC entropy coding
     * mode could provide for +10% to +18% improvement in general,
     * thus estimating +15% here ; and using adaptive 8x8 transforms
     * in I-frames could bring up to +10% improvement. */
    if (!self->use_cabac)
      bits_per_mb += (bits_per_mb * 15) / 100;
    if (!self->use_dct8x8)
      bits_per_mb += (bits_per_mb * 10) / 100;

    factor = (guint64) self->mb_width * self->mb_height * bits_per_mb;
    bitrate = gst_util_uint64_scale (factor,
        GST_VIDEO_INFO_FPS_N (&self->in_info),
        GST_VIDEO_INFO_FPS_D (&self->in_info)) / 1000;
    GST_INFO_OBJECT (self, "target bitrate computed to %u kbps", bitrate);
  }

  /* Adjust the setting based on RC mode. */
  switch (self->rc.rc_ctrl_mode) {
    case VA_RC_CQP:
      self->rc.max_bitrate = 0;
      self->rc.target_bitrate = 0;
      self->rc.target_percentage = 0;
      self->rc.cpb_size = 0;
      break;
    case VA_RC_CBR:
      self->rc.max_bitrate = bitrate;
      self->rc.target_bitrate = bitrate;
      self->rc.target_percentage = 100;
      self->rc.qp_i = self->rc.qp_p = self->rc.qp_b = 26;
      break;
    case VA_RC_VBR:
      g_assert (self->rc.target_percentage >= 10);
      self->rc.max_bitrate = (guint) gst_util_uint64_scale_int (bitrate,
          100, self->rc.target_percentage);
      self->rc.target_bitrate = bitrate;
      self->rc.qp_i = self->rc.qp_p = self->rc.qp_b = 26;
      break;
    case VA_RC_VCM:
      self->rc.max_bitrate = bitrate;
      self->rc.target_bitrate = bitrate;
      self->rc.target_percentage = 0;
      self->rc.qp_i = self->rc.qp_p = self->rc.qp_b = 26;
      self->rc.cpb_size = 0;

      if (self->gop.num_bframes > 0) {
        GST_INFO_OBJECT (self, "VCM mode just support I/P mode, no B frame");
        self->gop.num_bframes = 0;
        self->gop.b_pyramid = FALSE;
      }
      break;
  }

  if (self->rc.rc_ctrl_mode != VA_RC_CQP)
    _calculate_bitrate_hrd (self);

  /* notifications */
  if (self->rc.cpb_size != self->prop.cpb_size) {
    self->prop.cpb_size = self->rc.cpb_size;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CPB_SIZE]);
  }

  if (self->prop.target_percentage != self->rc.target_percentage) {
    self->prop.target_percentage = self->rc.target_percentage;
    g_object_notify_by_pspec (G_OBJECT (self),
        properties[PROP_TARGET_PERCENTAGE]);
  }

  if (self->prop.qp_i != self->rc.qp_i) {
    self->prop.qp_i = self->rc.qp_i;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_QP_I]);
  }
  if (self->prop.qp_p != self->rc.qp_p) {
    self->prop.qp_p = self->rc.qp_p;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_QP_P]);
  }
  if (self->prop.qp_b != self->rc.qp_b) {
    self->prop.qp_b = self->rc.qp_b;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_QP_B]);
  }
}

static guint
_get_h264_cpb_nal_factor (VAProfile profile)
{
  guint f;

  /* Table A-2 */
  switch (profile) {
    case VAProfileH264High:
      f = 1500;
      break;
    case VAProfileH264ConstrainedBaseline:
    case VAProfileH264Main:
      f = 1200;
      break;
    case VAProfileH264MultiviewHigh:
    case VAProfileH264StereoHigh:
      f = 1500;                 /* H.10.2.1 (r) */
      break;
    default:
      g_assert_not_reached ();
      f = 1200;
      break;
  }
  return f;
}

/* Derives the level from the currently set limits */
static gboolean
_calculate_level (GstVaH264Enc * self)
{
  const guint cpb_factor = _get_h264_cpb_nal_factor (self->profile);
  guint i, PicSizeMbs, MaxDpbMbs, MaxMBPS;

  PicSizeMbs = self->mb_width * self->mb_height;
  MaxDpbMbs = PicSizeMbs * (self->gop.num_ref_frames + 1);
  MaxMBPS = gst_util_uint64_scale_int_ceil (PicSizeMbs,
      GST_VIDEO_INFO_FPS_N (&self->in_info),
      GST_VIDEO_INFO_FPS_D (&self->in_info));

  for (i = 0; i < G_N_ELEMENTS (_va_h264_level_limits); i++) {
    const GstVaH264LevelLimits *const limits = &_va_h264_level_limits[i];
    if (PicSizeMbs <= limits->MaxFS && MaxDpbMbs <= limits->MaxDpbMbs
        && MaxMBPS <= limits->MaxMBPS && (!self->rc.max_bitrate_bits
            || self->rc.max_bitrate_bits <= (limits->MaxBR * 1000 * cpb_factor))
        && (!self->rc.cpb_length_bits
            || self->rc.cpb_length_bits <=
            (limits->MaxCPB * 1000 * cpb_factor))) {

      self->level_idc = _va_h264_level_limits[i].level_idc;
      self->level_str = _va_h264_level_limits[i].name;
      self->min_cr = _va_h264_level_limits[i].MinCR;

      return TRUE;
    }
  }

  GST_ERROR_OBJECT (self,
      "failed to find a suitable level matching codec config");
  return FALSE;
}

static void
_validate_parameters (GstVaH264Enc * self)
{
  gint32 max_slices;

  /* Ensure the num_slices provided by the user not exceed the limit
   * of the number of slices permitted by the stream and by the
   * hardware. */
  g_assert (self->num_slices >= 1);
  max_slices = gst_va_encoder_get_max_slice_num (self->encoder,
      self->profile, self->entrypoint);
  if (self->num_slices > max_slices)
    self->num_slices = max_slices;
  /* The stream size limit. */
  if (self->num_slices > ((self->mb_width * self->mb_height + 1) / 2))
    self->num_slices = ((self->mb_width * self->mb_height + 1) / 2);

  if (self->prop.num_slices != self->num_slices) {
    self->prop.num_slices = self->num_slices;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NUM_SLICES]);
  }

  /* Ensure trellis. */
  if (self->use_trellis &&
      !gst_va_encoder_has_trellis (self->encoder, self->profile,
          self->entrypoint)) {
    GST_INFO_OBJECT (self, "The trellis is not supported");
    self->use_trellis = FALSE;
  }

  if (self->prop.use_trellis != self->use_trellis) {
    self->prop.use_trellis = self->use_trellis;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TRELLIS]);
  }
}

/* Get log2_max_frame_num_minus4, log2_max_pic_order_cnt_lsb_minus4
 * value, shall be in the range of 0 to 12, inclusive. */
static guint
_get_log2_max_num (guint num)
{
  guint ret = 0;

  while (num) {
    ++ret;
    num >>= 1;
  }

  /* shall be in the range of 0+4 to 12+4, inclusive. */
  if (ret < 4) {
    ret = 4;
  } else if (ret > 16) {
    ret = 16;
  }
  return ret;
}

static void
_print_gop_structure (GstVaH264Enc * self)
{
#ifndef GST_DISABLE_GST_DEBUG
  GString *str;
  gint i;

  if (gst_debug_category_get_threshold (GST_CAT_DEFAULT) < GST_LEVEL_INFO)
    return;

  str = g_string_new (NULL);

  g_string_append_printf (str, "[ ");

  for (i = 0; i < self->gop.idr_period; i++) {
    if (i == 0) {
      g_string_append_printf (str, "IDR");
      continue;
    } else {
      g_string_append_printf (str, ", ");
    }

    g_string_append_printf (str, "%s",
        _slice_type_name (self->gop.frame_types[i].slice_type));

    if (self->gop.b_pyramid
        && self->gop.frame_types[i].slice_type == GST_H264_B_SLICE) {
      g_string_append_printf (str, "<L%d (%d, %d)>",
          self->gop.frame_types[i].pyramid_level,
          self->gop.frame_types[i].left_ref_poc_diff,
          self->gop.frame_types[i].right_ref_poc_diff);
    }

    if (self->gop.frame_types[i].is_ref) {
      g_string_append_printf (str, "(ref)");
    }

  }

  g_string_append_printf (str, " ]");

  GST_INFO_OBJECT (self, "GOP size: %d, forward reference %d, backward"
      " reference %d, GOP structure: %s", self->gop.idr_period,
      self->gop.ref_num_list0, self->gop.ref_num_list1, str->str);

  g_string_free (str, TRUE);
#endif
}

struct PyramidInfo
{
  guint level;
  gint left_ref_poc_diff;
  gint right_ref_poc_diff;
};

static void
_set_pyramid_info (struct PyramidInfo *info, guint len,
    guint current_level, guint highest_level)
{
  guint index;

  g_assert (len >= 1);

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

  if (index > 0)
    _set_pyramid_info (info, index, current_level, highest_level);

  if (index + 1 < len)
    _set_pyramid_info (&info[index + 1], len - (index + 1),
        current_level, highest_level);
}

static void
_create_gop_frame_types (GstVaH264Enc * self)
{
  guint i;
  guint i_frames = self->gop.num_iframes;
  struct PyramidInfo pyramid_info[31] = { 0, };

  if (self->gop.highest_pyramid_level > 0) {
    g_assert (self->gop.num_bframes > 0);
    _set_pyramid_info (pyramid_info, self->gop.num_bframes,
        0, self->gop.highest_pyramid_level);
  }

  g_assert (self->gop.idr_period <= MAX_GOP_SIZE);
  for (i = 0; i < self->gop.idr_period; i++) {
    if (i == 0) {
      self->gop.frame_types[i].slice_type = GST_H264_I_SLICE;
      self->gop.frame_types[i].is_ref = TRUE;
      continue;
    }

    /* Intra only stream. */
    if (self->gop.ip_period == 0) {
      self->gop.frame_types[i].slice_type = GST_H264_I_SLICE;
      self->gop.frame_types[i].is_ref = FALSE;
      continue;
    }

    if (i % self->gop.ip_period) {
      guint pyramid_index =
          i % self->gop.ip_period - 1 /* The first P or IDR */ ;

      self->gop.frame_types[i].slice_type = GST_H264_B_SLICE;
      self->gop.frame_types[i].pyramid_level =
          pyramid_info[pyramid_index].level;
      self->gop.frame_types[i].is_ref =
          (self->gop.frame_types[i].pyramid_level <
          self->gop.highest_pyramid_level);
      self->gop.frame_types[i].left_ref_poc_diff =
          pyramid_info[pyramid_index].left_ref_poc_diff;
      self->gop.frame_types[i].right_ref_poc_diff =
          pyramid_info[pyramid_index].right_ref_poc_diff;
      continue;
    }

    if (self->gop.i_period && i % self->gop.i_period == 0 && i_frames > 0) {
      /* Replace P with I. */
      self->gop.frame_types[i].slice_type = GST_H264_I_SLICE;
      self->gop.frame_types[i].is_ref = TRUE;
      i_frames--;
      continue;
    }

    self->gop.frame_types[i].slice_type = GST_H264_P_SLICE;
    self->gop.frame_types[i].is_ref = TRUE;
  }

  /* Force the last one to be a P */
  if (self->gop.idr_period > 1 && self->gop.ip_period > 0) {
    self->gop.frame_types[self->gop.idr_period - 1].slice_type =
        GST_H264_P_SLICE;
    self->gop.frame_types[self->gop.idr_period - 1].is_ref = TRUE;
  }
}

/* Consider the idr_period, num_bframes, L0/L1 reference number.
 * TODO: Load some preset fixed GOP structure.
 * TODO: Skip this if in lookahead mode. */
static void
_generate_gop_structure (GstVaH264Enc * self)
{
  guint32 list0, list1, gop_ref_num;
  gint32 p_frames;

  /* If not set, generate a idr every second */
  if (self->gop.idr_period == 0) {
    self->gop.idr_period = (GST_VIDEO_INFO_FPS_N (&self->in_info)
        + GST_VIDEO_INFO_FPS_D (&self->in_info) - 1) /
        GST_VIDEO_INFO_FPS_D (&self->in_info);
  }

  /* Do not use a too huge GOP size. */
  if (self->gop.idr_period > 1024) {
    self->gop.idr_period = 1024;
    GST_INFO_OBJECT (self, "Lowering the GOP size to %d", self->gop.idr_period);
  }

  if (self->gop.idr_period != self->prop.key_int_max) {
    self->prop.key_int_max = self->gop.idr_period;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_KEY_INT_MAX]);
  }

  /* Prefer have more than 1 refs for the GOP which is not very small. */
  if (self->gop.idr_period > 8) {
    if (self->gop.num_bframes > (self->gop.idr_period - 1) / 2) {
      self->gop.num_bframes = (self->gop.idr_period - 1) / 2;
      GST_INFO_OBJECT (self, "Lowering the number of num_bframes to %d",
          self->gop.num_bframes);
    }
  } else {
    /* beign and end should be ref */
    if (self->gop.num_bframes > self->gop.idr_period - 1 - 1) {
      if (self->gop.idr_period > 1) {
        self->gop.num_bframes = self->gop.idr_period - 1 - 1;
      } else {
        self->gop.num_bframes = 0;
      }
      GST_INFO_OBJECT (self, "Lowering the number of num_bframes to %d",
          self->gop.num_bframes);
    }
  }

  if (!gst_va_encoder_get_max_num_reference (self->encoder, self->profile,
          self->entrypoint, &list0, &list1)) {
    GST_INFO_OBJECT (self, "Failed to get the max num reference");
    list0 = 1;
    list1 = 0;
  }

  if (list0 > self->gop.num_ref_frames)
    list0 = self->gop.num_ref_frames;
  if (list1 > self->gop.num_ref_frames)
    list1 = self->gop.num_ref_frames;

  if (list0 == 0) {
    GST_INFO_OBJECT (self,
        "No reference support, fallback to intra only stream");

    /* It does not make sense that if only the list1 exists. */
    self->gop.num_ref_frames = 0;

    self->gop.ip_period = 0;
    self->gop.num_bframes = 0;
    self->gop.b_pyramid = FALSE;
    self->gop.highest_pyramid_level = 0;
    self->gop.num_iframes = self->gop.idr_period - 1 /* The idr */ ;
    self->gop.ref_num_list0 = 0;
    self->gop.ref_num_list1 = 0;
    goto create_poc;
  }

  if (self->gop.num_ref_frames <= 1) {
    GST_INFO_OBJECT (self, "The number of reference frames is only %d,"
        " no B frame allowed, fallback to I/P mode", self->gop.num_ref_frames);
    self->gop.num_bframes = 0;
    list1 = 0;
  }

  /* b_pyramid needs at least 1 ref for B, besides the I/P */
  if (self->gop.b_pyramid && self->gop.num_ref_frames <= 2) {
    GST_INFO_OBJECT (self, "The number of reference frames is only %d,"
        " not enough for b_pyramid", self->gop.num_ref_frames);
    self->gop.b_pyramid = FALSE;
  }

  if (list1 == 0 && self->gop.num_bframes > 0) {
    GST_INFO_OBJECT (self,
        "No hw reference support for list 1, fallback to I/P mode");
    self->gop.num_bframes = 0;
    self->gop.b_pyramid = FALSE;
  }

  /* I/P mode, no list1 needed. */
  if (self->gop.num_bframes == 0)
    list1 = 0;

  /* Not enough B frame, no need for b_pyramid. */
  if (self->gop.num_bframes <= 1)
    self->gop.b_pyramid = FALSE;

  /* b pyramid has only one backward ref. */
  if (self->gop.b_pyramid)
    list1 = 1;

  if (self->gop.num_ref_frames > list0 + list1) {
    self->gop.num_ref_frames = list0 + list1;
    GST_INFO_OBJECT (self, "HW limits, lowering the number of reference"
        " frames to %d", self->gop.num_ref_frames);
  }

  /* How many possible refs within a GOP. */
  gop_ref_num = (self->gop.idr_period + self->gop.num_bframes) /
      (self->gop.num_bframes + 1);
  /* The end ref */
  if (self->gop.num_bframes > 0
      /* frame_num % (self->gop.num_bframes + 1) happens to be the end P */
      && (self->gop.idr_period % (self->gop.num_bframes + 1) != 1))
    gop_ref_num++;

  /* Adjust reference num based on B frames and B pyramid. */
  if (self->gop.num_bframes == 0) {
    self->gop.b_pyramid = FALSE;
    self->gop.ref_num_list0 = self->gop.num_ref_frames;
    self->gop.ref_num_list1 = 0;
  } else if (self->gop.b_pyramid) {
    guint b_frames = self->gop.num_bframes;
    guint b_refs;

    /* b pyramid has only one backward ref. */
    g_assert (list1 == 1);
    self->gop.ref_num_list1 = list1;
    self->gop.ref_num_list0 =
        self->gop.num_ref_frames - self->gop.ref_num_list1;

    b_frames = b_frames / 2;
    b_refs = 0;
    while (b_frames) {
      /* At least 1 B ref for each level, plus begin and end 2 P/I */
      b_refs += 1;
      if (b_refs + 2 > self->gop.num_ref_frames)
        break;

      self->gop.highest_pyramid_level++;
      b_frames = b_frames / 2;
    }

    GST_INFO_OBJECT (self, "pyramid level is %d",
        self->gop.highest_pyramid_level);
  } else {
    /* We prefer list0. Backward refs have more latency. */
    self->gop.ref_num_list1 = 1;
    self->gop.ref_num_list0 =
        self->gop.num_ref_frames - self->gop.ref_num_list1;
    /* Balance the forward and backward refs, but not cause a big latency. */
    while ((self->gop.num_bframes * self->gop.ref_num_list1 <= 16)
        && (self->gop.ref_num_list1 <= gop_ref_num)
        && (self->gop.ref_num_list1 < list1)
        && (self->gop.ref_num_list0 / self->gop.ref_num_list1 > 4)) {
      self->gop.ref_num_list0--;
      self->gop.ref_num_list1++;
    }
  }

  /* It's OK, keep slots for GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME frame. */
  if (self->gop.ref_num_list0 > gop_ref_num)
    GST_DEBUG_OBJECT (self, "num_ref_frames %d is bigger than gop_ref_num %d",
        self->gop.ref_num_list0, gop_ref_num);

  /* Include the ref picture itself. */
  self->gop.ip_period = 1 + self->gop.num_bframes;

  p_frames = gop_ref_num - 1 /* IDR */ ;
  if (p_frames < 0)
    p_frames = 0;
  if (self->gop.num_iframes > p_frames) {
    self->gop.num_iframes = p_frames;
    GST_INFO_OBJECT (self, "Too many I frames insertion, lowering it to %d",
        self->gop.num_iframes);
  }

  if (self->gop.num_iframes > 0) {
    guint total_i_frames = self->gop.num_iframes + 1 /* IDR */ ;
    self->gop.i_period =
        (gop_ref_num / total_i_frames) * (self->gop.num_bframes + 1);
  }

create_poc:
  /* init max_frame_num, max_poc */
  self->gop.log2_max_frame_num = _get_log2_max_num (self->gop.idr_period);
  self->gop.max_frame_num = (1 << self->gop.log2_max_frame_num);
  self->gop.log2_max_pic_order_cnt = self->gop.log2_max_frame_num + 1;
  self->gop.max_pic_order_cnt = (1 << self->gop.log2_max_pic_order_cnt);
  self->gop.num_reorder_frames = self->gop.b_pyramid ?
      self->gop.highest_pyramid_level * 2 + 1 /* the last P frame. */ :
      self->gop.ref_num_list1;
  /* Should not exceed the max ref num. */
  self->gop.num_reorder_frames =
      MIN (self->gop.num_reorder_frames, self->gop.num_ref_frames);
  self->gop.num_reorder_frames = MIN (self->gop.num_reorder_frames, 16);

  _create_gop_frame_types (self);
  _print_gop_structure (self);

  /* notifications */
  if (self->prop.num_ref_frames != self->gop.num_ref_frames) {
    self->prop.num_ref_frames = self->gop.num_ref_frames;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NUM_REF_FRAMES]);
  }

  if (self->prop.num_iframes != self->gop.num_iframes) {
    self->prop.num_iframes = self->gop.num_iframes;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IFRAMES]);
  }

}

static void
_calculate_coded_size (GstVaH264Enc * self)
{
  guint codedbuf_size = 0;

  if (self->profile == VAProfileH264High
      || self->profile == VAProfileH264MultiviewHigh
      || self->profile == VAProfileH264StereoHigh) {
    /* The number of bits of macroblock_layer( ) data for any macroblock
       is not greater than 128 + RawMbBits */
    guint RawMbBits = 0;
    guint BitDepthY = 8;
    guint BitDepthC = 8;
    guint MbWidthC = 8;
    guint MbHeightC = 8;

    switch (self->rt_format) {
      case VA_RT_FORMAT_YUV420:
        BitDepthY = 8;
        BitDepthC = 8;
        MbWidthC = 8;
        MbHeightC = 8;
        break;
      case VA_RT_FORMAT_YUV422:
        BitDepthY = 8;
        BitDepthC = 8;
        MbWidthC = 8;
        MbHeightC = 16;
        break;
      case VA_RT_FORMAT_YUV444:
        BitDepthY = 8;
        BitDepthC = 8;
        MbWidthC = 16;
        MbHeightC = 16;
        break;
      case VA_RT_FORMAT_YUV400:
        BitDepthY = 8;
        BitDepthC = 0;
        MbWidthC = 0;
        MbHeightC = 0;
        break;
      case VA_RT_FORMAT_YUV420_10:
        BitDepthY = 10;
        BitDepthC = 10;
        MbWidthC = 8;
        MbHeightC = 8;
      case VA_RT_FORMAT_YUV422_10:
        BitDepthY = 10;
        BitDepthC = 10;
        MbWidthC = 8;
        MbHeightC = 16;
      case VA_RT_FORMAT_YUV444_10:
        BitDepthY = 10;
        BitDepthC = 10;
        MbWidthC = 16;
        MbHeightC = 16;
        break;
      default:
        g_assert_not_reached ();
        break;
    }

    /* The variable RawMbBits is derived as
     * RawMbBits = 256 * BitDepthY + 2 * MbWidthC * MbHeightC * BitDepthC */
    RawMbBits = 256 * BitDepthY + 2 * MbWidthC * MbHeightC * BitDepthC;
    codedbuf_size = (self->mb_width * self->mb_height) * (128 + RawMbBits) / 8;
  } else {
    /* The number of bits of macroblock_layer( ) data for any macroblock
     * is not greater than 3200 */
    codedbuf_size = (self->mb_width * self->mb_height) * (3200 / 8);
  }

  /* Account for SPS header */
  /* XXX: exclude scaling lists, MVC/SVC extensions */
  codedbuf_size += 4 /* start code */  + GST_ROUND_UP_8 (MAX_SPS_HDR_SIZE +
      MAX_VUI_PARAMS_SIZE + 2 * MAX_HRD_PARAMS_SIZE) / 8;

  /* Account for PPS header */
  /* XXX: exclude slice groups, scaling lists, MVC/SVC extensions */
  codedbuf_size += 4 + GST_ROUND_UP_8 (MAX_PPS_HDR_SIZE) / 8;

  /* Account for slice header */
  codedbuf_size +=
      self->num_slices * (4 + GST_ROUND_UP_8 (MAX_SLICE_HDR_SIZE) / 8);

  /* Add 5% for safety */
  self->codedbuf_size = (guint) ((gfloat) codedbuf_size * 1.05);

  GST_DEBUG_OBJECT (self, "Calculate codedbuf size: %u", self->codedbuf_size);
}

static guint
_get_rtformat (GstVaH264Enc * self, GstVideoFormat format)
{
  guint chroma;

  chroma = gst_va_chroma_from_video_format (format);

  /* Check whether the rtformat is supported. */
  if (chroma != VA_RT_FORMAT_YUV420) {
    GST_ERROR_OBJECT (self, "Unsupported chroma for video format: %s",
        gst_video_format_to_string (format));
    return 0;
  }

  return chroma;
}

static gboolean
_init_packed_headers (GstVaH264Enc * self)
{
  guint32 packed_headers;
  guint32 desired_packed_headers = VA_ENC_PACKED_HEADER_SEQUENCE        /* SPS */
      | VA_ENC_PACKED_HEADER_PICTURE    /* PPS */
      | VA_ENC_PACKED_HEADER_SLICE      /* Slice headers */
      | VA_ENC_PACKED_HEADER_RAW_DATA;  /* SEI, AUD, etc. */

  self->packed_headers = 0;

  packed_headers = gst_va_encoder_get_packed_headers (self->encoder,
      self->profile, self->entrypoint);

  if (packed_headers == 0)
    return FALSE;

  if (desired_packed_headers & ~packed_headers) {
    GST_INFO_OBJECT (self, "Driver does not support some wanted packed headers "
        "(wanted %#x, found %#x)", desired_packed_headers, packed_headers);
  }

  self->packed_headers = desired_packed_headers & packed_headers;

  return TRUE;
}


static gboolean
_decide_profile (GstVaH264Enc * self)
{
  gboolean ret = FALSE;
  GstVideoFormat in_format;
  VAProfile profile;
  guint rt_format;
  GstCaps *allowed_caps = NULL;
  guint num_structures, i;
  GstStructure *structure;
  const GValue *v_profile;
  GPtrArray *candidates = NULL;
  gchar *profile_name;

  candidates = g_ptr_array_new_with_free_func (g_free);

  /* First, check whether the downstream requires a specified profile. */
  allowed_caps = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (self));
  if (!allowed_caps)
    allowed_caps = gst_pad_query_caps (GST_VIDEO_ENCODER_SRC_PAD (self), NULL);

  if (allowed_caps && !gst_caps_is_empty (allowed_caps)) {
    num_structures = gst_caps_get_size (allowed_caps);
    for (i = 0; i < num_structures; i++) {
      structure = gst_caps_get_structure (allowed_caps, i);
      v_profile = gst_structure_get_value (structure, "profile");
      if (!v_profile)
        continue;

      if (G_VALUE_HOLDS_STRING (v_profile)) {
        profile_name = g_strdup (g_value_get_string (v_profile));
        g_ptr_array_add (candidates, profile_name);
      } else if (GST_VALUE_HOLDS_LIST (v_profile)) {
        guint j;

        for (j = 0; j < gst_value_list_get_size (v_profile); j++) {
          const GValue *p = gst_value_list_get_value (v_profile, j);
          if (!p)
            continue;

          profile_name = g_strdup (g_value_get_string (p));
          g_ptr_array_add (candidates, profile_name);
        }
      }
    }
  }

  if (candidates->len == 0) {
    GST_ERROR_OBJECT (self, "No available profile in caps");
    ret = FALSE;
    goto out;
  }

  in_format = GST_VIDEO_INFO_FORMAT (&self->in_info);
  rt_format = _get_rtformat (self, in_format);
  if (!rt_format) {
    GST_ERROR_OBJECT (self, "unsupported video format %s",
        gst_video_format_to_string (in_format));
    ret = FALSE;
    goto out;
  }

  /* Find the suitable profile by features and check the HW
   * support. */
  ret = FALSE;
  for (i = 0; i < candidates->len; i++) {
    profile_name = g_ptr_array_index (candidates, i);

    /* dct8x8 require at least high profile. */
    if (self->use_dct8x8) {
      if (!g_strstr_len (profile_name, -1, "high"))
        continue;
    }

    /* cabac require at least main profile. */
    if (self->use_cabac) {
      if (!g_strstr_len (profile_name, -1, "main")
          && !g_strstr_len (profile_name, -1, "high"))
        continue;
    }

    /* baseline only support I/P mode. */
    if (self->gop.num_bframes > 0) {
      if (g_strstr_len (profile_name, -1, "baseline"))
        continue;
    }

    profile = gst_va_profile_from_name (H264, profile_name);
    if (profile == VAProfileNone)
      continue;

    if (!gst_va_encoder_has_profile_and_entrypoint (self->encoder,
            profile, VAEntrypointEncSlice))
      continue;

    if ((rt_format & gst_va_encoder_get_rtformat (self->encoder,
                profile, VAEntrypointEncSlice)) == 0)
      continue;

    self->profile = profile;
    self->entrypoint = VAEntrypointEncSlice;
    self->rt_format = rt_format;
    ret = TRUE;
    goto out;
  }

  /* Just use the first HW available profile and disable features if
   * needed. */
  profile_name = NULL;
  for (i = 0; i < candidates->len; i++) {
    profile_name = g_ptr_array_index (candidates, i);
    profile = gst_va_profile_from_name (H264, profile_name);
    if (profile == VAProfileNone)
      continue;

    if (!gst_va_encoder_has_profile_and_entrypoint (self->encoder,
            profile, VAEntrypointEncSlice))
      continue;

    if ((rt_format & gst_va_encoder_get_rtformat (self->encoder,
                profile, VAEntrypointEncSlice)) == 0)
      continue;

    self->profile = profile;
    self->entrypoint = VAEntrypointEncSlice;
    self->rt_format = rt_format;
    ret = TRUE;
  }

  if (ret == FALSE)
    goto out;

  if (self->use_dct8x8 && !g_strstr_len (profile_name, -1, "high")) {
    GST_INFO_OBJECT (self, "Disable dct8x8, profile %s does not support it",
        gst_va_profile_name (self->profile));
    self->use_dct8x8 = FALSE;
    self->prop.use_dct8x8 = FALSE;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DCT8X8]);
  }

  if (self->use_cabac && (!g_strstr_len (profile_name, -1, "main")
          && !g_strstr_len (profile_name, -1, "high"))) {
    GST_INFO_OBJECT (self, "Disable cabac, profile %s does not support it",
        gst_va_profile_name (self->profile));
    self->use_cabac = FALSE;
    self->prop.use_cabac = FALSE;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CABAC]);
  }

  if (self->gop.num_bframes > 0 && g_strstr_len (profile_name, -1, "baseline")) {
    GST_INFO_OBJECT (self, "No B frames, profile %s does not support it",
        gst_va_profile_name (self->profile));
    self->gop.num_bframes = 0;
    self->gop.b_pyramid = 0;
  }

out:
  g_clear_pointer (&candidates, g_ptr_array_unref);
  g_clear_pointer (&allowed_caps, gst_caps_unref);

  if (ret) {
    GST_INFO_OBJECT (self, "Select the profile %s",
        gst_va_profile_name (profile));
  } else {
    GST_ERROR_OBJECT (self, "Failed to find an available profile");
  }

  return ret;
}

/* Clear all the info of last reconfig and set the fields based on
 * property. The reconfig may change these fields because of the
 * profile/level and HW limitation. */
static void
gst_va_h264_enc_reset_state (GstVaH264Enc * self)
{
  self->width = 0;
  self->height = 0;
  self->profile = VAProfileNone;
  self->entrypoint = 0;
  self->rt_format = 0;
  self->codedbuf_size = 0;

  self->frame_duration = GST_CLOCK_TIME_NONE;
  self->input_frame_count = 0;
  self->output_frame_count = 0;

  self->level_idc = 0;
  self->level_str = NULL;
  self->mb_width = 0;
  self->mb_height = 0;
  self->use_cabac = self->prop.use_cabac;
  self->use_dct8x8 = self->prop.use_dct8x8;
  self->use_trellis = self->prop.use_trellis;
  self->num_slices = self->prop.num_slices;

  self->gop.idr_period = self->prop.key_int_max;
  self->gop.i_period = 0;
  self->gop.total_idr_count = 0;
  self->gop.ip_period = 0;
  self->gop.num_bframes = self->prop.num_bframes;
  self->gop.b_pyramid = self->prop.b_pyramid;
  self->gop.highest_pyramid_level = 0;
  self->gop.num_iframes = self->prop.num_iframes;
  memset (self->gop.frame_types, 0, sizeof (self->gop.frame_types));
  self->gop.cur_frame_index = 0;
  self->gop.cur_frame_num = 0;
  self->gop.max_frame_num = 0;
  self->gop.log2_max_frame_num = 0;
  self->gop.max_pic_order_cnt = 0;
  self->gop.log2_max_pic_order_cnt = 0;
  self->gop.num_ref_frames = self->prop.num_ref_frames;
  self->gop.ref_num_list0 = 0;
  self->gop.ref_num_list1 = 0;
  self->gop.num_reorder_frames = 0;

  self->rc.rc_ctrl_mode = self->prop.rc_ctrl;
  self->rc.min_qp = self->prop.min_qp;
  self->rc.max_qp = self->prop.max_qp;
  self->rc.qp_i = self->prop.qp_i;
  self->rc.qp_p = self->prop.qp_p;
  self->rc.qp_b = self->prop.qp_b;
  self->rc.mbbrc = self->prop.mbbrc;
  self->rc.max_bitrate = 0;
  self->rc.target_bitrate = 0;
  self->rc.target_percentage = self->prop.target_percentage;
  self->rc.target_usage = self->prop.target_usage;
  self->rc.max_bitrate_bits = 0;
  self->rc.target_bitrate_bits = 0;
  self->rc.cpb_size = self->prop.cpb_size;
  self->rc.cpb_length_bits = 0;

  memset (&self->sequence_hdr, 0, sizeof (GstH264SPS));
}

static gboolean
gst_va_h264_enc_reconfig (GstVaH264Enc * self)
{
  gst_va_h264_enc_reset_state (self);

  self->width = GST_VIDEO_INFO_WIDTH (&self->in_info);
  self->height = GST_VIDEO_INFO_HEIGHT (&self->in_info);

  self->mb_width = GST_ROUND_UP_16 (self->width) / 16;
  self->mb_height = GST_ROUND_UP_16 (self->height) / 16;

  /* Frame rate is needed for rate control and PTS setting. */
  if (GST_VIDEO_INFO_FPS_N (&self->in_info) == 0
      || GST_VIDEO_INFO_FPS_D (&self->in_info) == 0) {
    GST_INFO_OBJECT (self, "Unknown framerate, just set to 30 fps");
    GST_VIDEO_INFO_FPS_N (&self->in_info) = 30;
    GST_VIDEO_INFO_FPS_D (&self->in_info) = 1;
  }
  self->frame_duration = gst_util_uint64_scale (GST_SECOND,
      GST_VIDEO_INFO_FPS_D (&self->in_info),
      GST_VIDEO_INFO_FPS_N (&self->in_info));

  GST_DEBUG_OBJECT (self, "resolution:%dx%d, MB size: %dx%d,"
      " frame duration is %" GST_TIME_FORMAT,
      self->width, self->height, self->mb_width, self->mb_height,
      GST_TIME_ARGS (self->frame_duration));

  if (!_decide_profile (self))
    return FALSE;

  _validate_parameters (self);

  _ensure_rate_control (self);

  if (!_calculate_level (self))
    return FALSE;

  _generate_gop_structure (self);
  _calculate_coded_size (self);

  /* notifications */
  /* num_bframes are modified several times before */
  if (self->prop.num_bframes != self->gop.num_bframes) {
    self->prop.num_bframes = self->gop.num_bframes;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BFRAMES]);
  }

  if (self->prop.b_pyramid != self->gop.b_pyramid) {
    self->prop.b_pyramid = self->gop.b_pyramid;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_B_PYRAMID]);
  }

  if (!_init_packed_headers (self))
    return FALSE;

  return TRUE;
}

static gboolean
gst_va_h264_enc_push_frame (GstVaH264Enc * self, GstVideoCodecFrame * gst_frame,
    gboolean last)
{
  GstVaH264EncFrame *frame;

  g_return_val_if_fail (self->gop.cur_frame_index <= self->gop.idr_period,
      FALSE);

  if (gst_frame) {
    /* Begin a new GOP, should have a empty reorder_list. */
    if (self->gop.cur_frame_index == self->gop.idr_period) {
      g_assert (g_queue_is_empty (&self->reorder_list));
      self->gop.cur_frame_index = 0;
      self->gop.cur_frame_num = 0;
    }

    frame = _enc_frame (gst_frame);
    frame->poc =
        ((self->gop.cur_frame_index * 2) % self->gop.max_pic_order_cnt);

    if (self->gop.cur_frame_index == 0) {
      g_assert (frame->poc == 0);
      GST_LOG_OBJECT (self, "system_frame_number: %d, an IDR frame, starts"
          " a new GOP", gst_frame->system_frame_number);

      g_queue_clear_full (&self->ref_list,
          (GDestroyNotify) gst_video_codec_frame_unref);
    }

    frame->type = self->gop.frame_types[self->gop.cur_frame_index].slice_type;
    frame->is_ref = self->gop.frame_types[self->gop.cur_frame_index].is_ref;
    frame->pyramid_level =
        self->gop.frame_types[self->gop.cur_frame_index].pyramid_level;
    frame->left_ref_poc_diff =
        self->gop.frame_types[self->gop.cur_frame_index].left_ref_poc_diff;
    frame->right_ref_poc_diff =
        self->gop.frame_types[self->gop.cur_frame_index].right_ref_poc_diff;

    if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (gst_frame)) {
      GST_DEBUG_OBJECT (self, "system_frame_number: %d, a force key frame,"
          " promote its type from %s to %s", gst_frame->system_frame_number,
          _slice_type_name (frame->type), _slice_type_name (GST_H264_I_SLICE));
      frame->type = GST_H264_I_SLICE;
      frame->is_ref = TRUE;
    }

    GST_LOG_OBJECT (self, "Push frame, system_frame_number: %d, poc %d, "
        "frame type %s", gst_frame->system_frame_number, frame->poc,
        _slice_type_name (frame->type));

    self->gop.cur_frame_index++;
    g_queue_push_tail (&self->reorder_list,
        gst_video_codec_frame_ref (gst_frame));
  }

  /* ensure the last one a non-B and end the GOP. */
  if (last && self->gop.cur_frame_index < self->gop.idr_period) {
    GstVideoCodecFrame *last_frame;

    /* Ensure next push will start a new GOP. */
    self->gop.cur_frame_index = self->gop.idr_period;

    if (!g_queue_is_empty (&self->reorder_list)) {
      last_frame = g_queue_peek_tail (&self->reorder_list);
      frame = _enc_frame (last_frame);
      if (frame->type == GST_H264_B_SLICE) {
        frame->type = GST_H264_P_SLICE;
        frame->is_ref = TRUE;
      }
    }
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
  GstVaH264EncFrame *frame = _enc_frame (data);
  struct RefFramesCount *count = (struct RefFramesCount *) user_data;

  g_assert (frame->poc != count->poc);
  if (frame->poc > count->poc)
    count->num++;
}

static GstVideoCodecFrame *
_pop_pyramid_b_frame (GstVaH264Enc * self)
{
  guint i;
  gint index = -1;
  GstVaH264EncFrame *b_vaframe;
  GstVideoCodecFrame *b_frame;
  struct RefFramesCount count;

  g_assert (self->gop.ref_num_list1 == 1);

  b_frame = NULL;
  b_vaframe = NULL;

  /* Find the lowest level with smallest poc. */
  for (i = 0; i < g_queue_get_length (&self->reorder_list); i++) {
    GstVaH264EncFrame *vaf;
    GstVideoCodecFrame *f;

    f = g_queue_peek_nth (&self->reorder_list, i);

    if (!b_frame) {
      b_frame = f;
      b_vaframe = _enc_frame (b_frame);
      index = i;
      continue;
    }

    vaf = _enc_frame (f);
    if (b_vaframe->pyramid_level < vaf->pyramid_level) {
      b_frame = f;
      b_vaframe = vaf;
      index = i;
      continue;
    }

    if (b_vaframe->poc > vaf->poc) {
      b_frame = f;
      b_vaframe = vaf;
      index = i;
    }
  }

again:
  /* Check whether its refs are already poped. */
  g_assert (b_vaframe->left_ref_poc_diff != 0);
  g_assert (b_vaframe->right_ref_poc_diff != 0);
  for (i = 0; i < g_queue_get_length (&self->reorder_list); i++) {
    GstVaH264EncFrame *vaf;
    GstVideoCodecFrame *f;

    f = g_queue_peek_nth (&self->reorder_list, i);

    if (f == b_frame)
      continue;

    vaf = _enc_frame (f);
    if (vaf->poc == b_vaframe->poc + b_vaframe->left_ref_poc_diff
        || vaf->poc == b_vaframe->poc + b_vaframe->right_ref_poc_diff) {
      b_frame = f;
      b_vaframe = vaf;
      index = i;
      goto again;
    }
  }

  /* Ensure we already have enough backward refs */
  count.num = 0;
  count.poc = b_vaframe->poc;
  g_queue_foreach (&self->ref_list, (GFunc) _count_backward_ref_num, &count);
  if (count.num >= self->gop.ref_num_list1) {
    GstVideoCodecFrame *f;

    /* it will unref at pop_frame */
    f = g_queue_pop_nth (&self->reorder_list, index);
    g_assert (f == b_frame);
  } else {
    b_frame = NULL;
  }

  return b_frame;
}

static gboolean
gst_va_h264_enc_pop_frame (GstVaH264Enc * self, GstVideoCodecFrame ** out_frame)
{
  GstVaH264EncFrame *vaframe;
  GstVideoCodecFrame *frame;
  struct RefFramesCount count;

  g_return_val_if_fail (self->gop.cur_frame_index <= self->gop.idr_period,
      FALSE);

  *out_frame = NULL;

  if (g_queue_is_empty (&self->reorder_list))
    return TRUE;

  /* Return the last pushed non-B immediately. */
  frame = g_queue_peek_tail (&self->reorder_list);
  vaframe = _enc_frame (frame);
  if (vaframe->type != GST_H264_B_SLICE) {
    frame = g_queue_pop_tail (&self->reorder_list);
    goto get_one;
  }

  if (self->gop.b_pyramid) {
    frame = _pop_pyramid_b_frame (self);
    if (frame == NULL)
      return TRUE;
    goto get_one;
  }

  g_assert (self->gop.ref_num_list1 > 0);

  /* If GOP end, pop anyway. */
  if (self->gop.cur_frame_index == self->gop.idr_period) {
    frame = g_queue_pop_head (&self->reorder_list);
    goto get_one;
  }

  /* Ensure we already have enough backward refs */
  frame = g_queue_peek_head (&self->reorder_list);
  vaframe = _enc_frame (frame);
  count.num = 0;
  count.poc = vaframe->poc;
  g_queue_foreach (&self->ref_list, _count_backward_ref_num, &count);
  if (count.num >= self->gop.ref_num_list1) {
    frame = g_queue_pop_head (&self->reorder_list);
    goto get_one;
  }

  return TRUE;

get_one:
  g_assert (self->gop.cur_frame_num < self->gop.max_frame_num);

  vaframe = _enc_frame (frame);
  vaframe->frame_num = self->gop.cur_frame_num;

  /* Add the frame number for ref frames. */
  if (vaframe->is_ref)
    self->gop.cur_frame_num++;

  if (vaframe->frame_num == 0)
    self->gop.total_idr_count++;

  if (self->gop.b_pyramid && vaframe->type == GST_H264_B_SLICE) {
    GST_LOG_OBJECT (self, "pop a pyramid B frame with system_frame_number:"
        " %d, poc: %d, frame num: %d, is_ref: %s, level %d",
        frame->system_frame_number, vaframe->poc, vaframe->frame_num,
        vaframe->is_ref ? "true" : "false", vaframe->pyramid_level);
  } else {
    GST_LOG_OBJECT (self, "pop a frame with system_frame_number: %d,"
        " frame type: %s, poc: %d, frame num: %d, is_ref: %s",
        frame->system_frame_number, _slice_type_name (vaframe->type),
        vaframe->poc, vaframe->frame_num, vaframe->is_ref ? "true" : "false");
  }

  /* unref frame popped from queue or pyramid b_frame */
  gst_video_codec_frame_unref (frame);
  *out_frame = frame;
  return TRUE;
}

static inline gboolean
_fill_sps (GstVaH264Enc * self, VAEncSequenceParameterBufferH264 * seq_param)
{
  GstH264Profile profile;
  guint32 constraint_set0_flag, constraint_set1_flag;
  guint32 constraint_set2_flag, constraint_set3_flag;
  guint32 max_dec_frame_buffering;

  /* let max_num_ref_frames <= MaxDpbFrames. */
  max_dec_frame_buffering =
      MIN (self->gop.num_ref_frames + 1 /* Last frame before bump */ ,
      16 /* DPB_MAX_SIZE */ );

  constraint_set0_flag = 0;
  constraint_set1_flag = 0;
  constraint_set2_flag = 0;
  constraint_set3_flag = 0;

  switch (self->profile) {
    case VAProfileH264ConstrainedBaseline:
      profile = GST_H264_PROFILE_BASELINE;
      /* A.2.1 (baseline profile constraints) */
      constraint_set0_flag = 1;
      constraint_set1_flag = 1;
      break;
    case VAProfileH264Main:
      profile = GST_H264_PROFILE_MAIN;
      /* A.2.2 (main profile constraints) */
      constraint_set1_flag = 1;
      break;
    case VAProfileH264High:
    case VAProfileH264MultiviewHigh:
    case VAProfileH264StereoHigh:
      profile = GST_H264_PROFILE_HIGH;
      break;
    default:
      return FALSE;
  }

  /* seq_scaling_matrix_present_flag not supported now */
  g_assert (seq_param->seq_fields.bits.seq_scaling_matrix_present_flag == 0);
  /* pic_order_cnt_type only support 0 now */
  g_assert (seq_param->seq_fields.bits.pic_order_cnt_type == 0);
  /* only progressive frames encoding is supported now */
  g_assert (seq_param->seq_fields.bits.frame_mbs_only_flag);

  /* *INDENT-OFF* */
  GST_DEBUG_OBJECT (self, "filling SPS");
  self->sequence_hdr = (GstH264SPS) {
    .id = 0,
    .profile_idc = profile,
    .constraint_set0_flag = constraint_set0_flag,
    .constraint_set1_flag = constraint_set1_flag,
    .constraint_set2_flag = constraint_set2_flag,
    .constraint_set3_flag = constraint_set3_flag,
    .level_idc = self->level_idc,

    .chroma_format_idc = seq_param->seq_fields.bits.chroma_format_idc,
    .bit_depth_luma_minus8 = seq_param->bit_depth_luma_minus8,
    .bit_depth_chroma_minus8 = seq_param->bit_depth_chroma_minus8,

    .log2_max_frame_num_minus4 =
        seq_param->seq_fields.bits.log2_max_frame_num_minus4,
    .pic_order_cnt_type = seq_param->seq_fields.bits.pic_order_cnt_type,
    .log2_max_pic_order_cnt_lsb_minus4 =
        seq_param->seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4,

    .num_ref_frames = seq_param->max_num_ref_frames,
    .gaps_in_frame_num_value_allowed_flag = 0,
    .pic_width_in_mbs_minus1 = seq_param->picture_width_in_mbs - 1,
    .pic_height_in_map_units_minus1 =
        (seq_param->seq_fields.bits.frame_mbs_only_flag ?
            seq_param->picture_height_in_mbs - 1 :
            seq_param->picture_height_in_mbs / 2 - 1),
    .frame_mbs_only_flag = seq_param->seq_fields.bits.frame_mbs_only_flag,
    .mb_adaptive_frame_field_flag = 0,
    .direct_8x8_inference_flag =
        seq_param->seq_fields.bits.direct_8x8_inference_flag,
    .frame_cropping_flag = seq_param->frame_cropping_flag,
    .frame_crop_left_offset = seq_param->frame_crop_left_offset,
    .frame_crop_right_offset = seq_param->frame_crop_right_offset,
    .frame_crop_top_offset = seq_param->frame_crop_top_offset,
    .frame_crop_bottom_offset = seq_param->frame_crop_bottom_offset,

    .vui_parameters_present_flag = seq_param->vui_parameters_present_flag,
    .vui_parameters = {
      .aspect_ratio_info_present_flag =
          seq_param->vui_fields.bits.aspect_ratio_info_present_flag,
      .aspect_ratio_idc = seq_param->aspect_ratio_idc,
      .sar_width = seq_param->sar_width,
      .sar_height = seq_param->sar_height,
      .overscan_info_present_flag = 0,
      .overscan_appropriate_flag = 0,
      .chroma_loc_info_present_flag = 0,
      .timing_info_present_flag =
          seq_param->vui_fields.bits.timing_info_present_flag,
      .num_units_in_tick = seq_param->num_units_in_tick,
      .time_scale = seq_param->time_scale,
      .fixed_frame_rate_flag = seq_param->vui_fields.bits.fixed_frame_rate_flag,

      /* We do not write hrd and no need for buffering period SEI. */
      .nal_hrd_parameters_present_flag = 0,
      .vcl_hrd_parameters_present_flag = 0,

      .low_delay_hrd_flag = seq_param->vui_fields.bits.low_delay_hrd_flag,
      .pic_struct_present_flag = 1,
      .bitstream_restriction_flag =
          seq_param->vui_fields.bits.bitstream_restriction_flag,
      .motion_vectors_over_pic_boundaries_flag =
          seq_param->vui_fields.bits.motion_vectors_over_pic_boundaries_flag,
      .max_bytes_per_pic_denom = 2,
      .max_bits_per_mb_denom = 1,
      .log2_max_mv_length_horizontal =
          seq_param->vui_fields.bits.log2_max_mv_length_horizontal,
      .log2_max_mv_length_vertical =
          seq_param->vui_fields.bits.log2_max_mv_length_vertical,
      .num_reorder_frames = self->gop.num_reorder_frames,
      .max_dec_frame_buffering = max_dec_frame_buffering,
    },
  };
  /* *INDENT-ON* */

  return TRUE;
}

static gboolean
_add_sequence_header (GstVaH264Enc * self, GstVaH264EncFrame * frame)
{
  gsize size;
#define SPS_SIZE 4 + GST_ROUND_UP_8 (MAX_SPS_HDR_SIZE + MAX_VUI_PARAMS_SIZE + \
    2 * MAX_HRD_PARAMS_SIZE) / 8
  guint8 packed_sps[SPS_SIZE] = { 0, };
#undef SPS_SIZE

  size = sizeof (packed_sps);
  if (gst_h264_bit_writer_sps (&self->sequence_hdr, TRUE, packed_sps,
          &size) != GST_H264_BIT_WRITER_OK) {
    GST_ERROR_OBJECT (self, "Failed to generate the sequence header");
    return FALSE;
  }

  if (!gst_va_encoder_add_packed_header (self->encoder, frame->picture,
          VAEncPackedHeaderSequence, packed_sps, size, FALSE)) {
    GST_ERROR_OBJECT (self, "Failed to add the packed sequence header");
    return FALSE;
  }

  return TRUE;
}

static inline void
_fill_sequence_param (GstVaH264Enc * self,
    VAEncSequenceParameterBufferH264 * sequence)
{
  gboolean direct_8x8_inference_flag = TRUE;

  g_assert (self->gop.log2_max_frame_num >= 4);
  g_assert (self->gop.log2_max_pic_order_cnt >= 4);

  /* A.2.3 Extended profile:
   * Sequence parameter sets shall have direct_8x8_inference_flag
   * equal to 1.
   *
   * A.3.3 Profile-specific level limits:
   * direct_8x8_inference_flag is not relevant to the Baseline,
   * Constrained Baseline, Constrained High, High 10 Intra, High 4:2:2
   * Intra, High 4:4:4 Intra, and CAVLC 4:4:4 Intra profiles as these
   * profiles do not allow B slice types, and
   * direct_8x8_inference_flag is equal to 1 for all levels of the
   * Extended profile. Table A-4.  We only have constrained baseline
   * here. */
  if (self->profile == VAProfileH264ConstrainedBaseline)
    direct_8x8_inference_flag = FALSE;

  /* *INDENT-OFF* */
  *sequence = (VAEncSequenceParameterBufferH264) {
    .seq_parameter_set_id = 0,
    .level_idc = self->level_idc,
    .intra_period =
        self->gop.i_period > 0 ? self->gop.i_period : self->gop.idr_period,
    .intra_idr_period = self->gop.idr_period,
    .ip_period = self->gop.ip_period,
    .bits_per_second = self->rc.target_bitrate_bits,
    .max_num_ref_frames = self->gop.num_ref_frames,
    .picture_width_in_mbs = self->mb_width,
    .picture_height_in_mbs = self->mb_height,

    .seq_fields.bits = {
      /* Only support 4:2:0 now. */
      .chroma_format_idc = 1,
      .frame_mbs_only_flag = 1,
      .mb_adaptive_frame_field_flag = FALSE,
      .seq_scaling_matrix_present_flag = FALSE,
      .direct_8x8_inference_flag = direct_8x8_inference_flag,
      .log2_max_frame_num_minus4 = self->gop.log2_max_frame_num - 4,
      .pic_order_cnt_type = 0,
      .log2_max_pic_order_cnt_lsb_minus4 = self->gop.log2_max_pic_order_cnt - 4,
    },
    .bit_depth_luma_minus8 = 0,
    .bit_depth_chroma_minus8 = 0,

    .vui_parameters_present_flag = TRUE,
    .vui_fields.bits = {
      .aspect_ratio_info_present_flag = TRUE,
      .timing_info_present_flag = TRUE,
      .bitstream_restriction_flag = TRUE,
      .log2_max_mv_length_horizontal = 15,
      .log2_max_mv_length_vertical = 15,
      .fixed_frame_rate_flag = 1,
      .low_delay_hrd_flag = 0,
      .motion_vectors_over_pic_boundaries_flag = TRUE,
    },
    .aspect_ratio_idc = 0xff,
    /* FIXME: what if no framerate info is provided */
    .sar_width = GST_VIDEO_INFO_PAR_N (&self->in_info),
    .sar_height = GST_VIDEO_INFO_PAR_D (&self->in_info),
    .num_units_in_tick = GST_VIDEO_INFO_FPS_D (&self->in_info),
    .time_scale = GST_VIDEO_INFO_FPS_N (&self->in_info) * 2,
  };
  /* *INDENT-ON* */

  /* frame_cropping_flag */
  if (self->width & 15 || self->height & 15) {
    static const guint SubWidthC[] = { 1, 2, 2, 1 };
    static const guint SubHeightC[] = { 1, 2, 1, 1 };
    const guint CropUnitX =
        SubWidthC[sequence->seq_fields.bits.chroma_format_idc];
    const guint CropUnitY =
        SubHeightC[sequence->seq_fields.bits.chroma_format_idc] *
        (2 - sequence->seq_fields.bits.frame_mbs_only_flag);

    sequence->frame_cropping_flag = 1;
    sequence->frame_crop_left_offset = 0;
    sequence->frame_crop_right_offset = (16 * self->mb_width -
        self->width) / CropUnitX;
    sequence->frame_crop_top_offset = 0;
    sequence->frame_crop_bottom_offset = (16 * self->mb_height -
        self->height) / CropUnitY;
  }
}

static gboolean
_add_sequence_parameter (GstVaH264Enc * self, GstVaEncodePicture * picture,
    VAEncSequenceParameterBufferH264 * sequence)
{
  if (!gst_va_encoder_add_param (self->encoder, picture,
          VAEncSequenceParameterBufferType, sequence, sizeof (*sequence))) {
    GST_ERROR_OBJECT (self, "Failed to create the sequence parameter");
    return FALSE;
  }

  return TRUE;
}

static gboolean
_add_rate_control_parameter (GstVaH264Enc * self, GstVaEncodePicture * picture)
{
  uint32_t window_size;
  struct VAEncMiscParameterRateControlWrap
  {
    VAEncMiscParameterType type;
    VAEncMiscParameterRateControl rate_control;
  } rate_control;

  if (self->rc.rc_ctrl_mode == VA_RC_CQP)
    return TRUE;

  window_size = self->rc.rc_ctrl_mode == VA_RC_VBR ?
      self->rc.max_bitrate_bits / 2 : self->rc.max_bitrate_bits;

  /* *INDENT-OFF* */
  rate_control = (struct VAEncMiscParameterRateControlWrap) {
    .type = VAEncMiscParameterTypeRateControl,
    .rate_control = {
      .bits_per_second = self->rc.max_bitrate_bits,
      .target_percentage = self->rc.target_percentage,
      .window_size = window_size,
      .initial_qp = self->rc.qp_i,
      .min_qp = self->rc.min_qp,
      .max_qp = self->rc.max_qp,
      .rc_flags.bits.mb_rate_control = self->rc.mbbrc,
      .quality_factor = 0,
    },
  };
  /* *INDENT-ON* */

  if (!gst_va_encoder_add_param (self->encoder, picture,
          VAEncMiscParameterBufferType, &rate_control, sizeof (rate_control))) {
    GST_ERROR_OBJECT (self, "Failed to create the race control parameter");
    return FALSE;
  }

  return TRUE;
}

static gboolean
_add_hrd_parameter (GstVaH264Enc * self, GstVaEncodePicture * picture)
{
  /* *INDENT-OFF* */
  struct
  {
    VAEncMiscParameterType type;
    VAEncMiscParameterHRD hrd;
  } hrd = {
    .type = VAEncMiscParameterTypeHRD,
    .hrd = {
      .buffer_size = self->rc.cpb_length_bits,
      .initial_buffer_fullness = self->rc.cpb_length_bits / 2,
    },
  };
  /* *INDENT-ON* */

  if (self->rc.rc_ctrl_mode == VA_RC_CQP || self->rc.rc_ctrl_mode == VA_RC_VCM)
    return TRUE;

  g_assert (self->rc.max_bitrate_bits > 0);


  if (!gst_va_encoder_add_param (self->encoder, picture,
          VAEncMiscParameterBufferType, &hrd, sizeof (hrd))) {
    GST_ERROR_OBJECT (self, "Failed to create the HRD parameter");
    return FALSE;
  }

  return TRUE;
}

static gboolean
_add_quality_level_parameter (GstVaH264Enc * self, GstVaEncodePicture * picture)
{
  /* *INDENT-OFF* */
  struct
  {
    VAEncMiscParameterType type;
    VAEncMiscParameterBufferQualityLevel ql;
  } quality_level = {
    .type = VAEncMiscParameterTypeQualityLevel,
    .ql.quality_level = self->rc.target_usage,
  };
  /* *INDENT-ON* */

  if (self->rc.target_usage == 0)
    return TRUE;

  if (!gst_va_encoder_add_param (self->encoder, picture,
          VAEncMiscParameterBufferType, &quality_level,
          sizeof (quality_level))) {
    GST_ERROR_OBJECT (self, "Failed to create the quality level parameter");
    return FALSE;
  }

  return TRUE;
}

static gboolean
_add_frame_rate_parameter (GstVaH264Enc * self, GstVaEncodePicture * picture)
{
  /* *INDENT-OFF* */
  struct
  {
    VAEncMiscParameterType type;
    VAEncMiscParameterFrameRate fr;
  } framerate = {
    .type = VAEncMiscParameterTypeFrameRate,
    /* denominator = framerate >> 16 & 0xffff;
     * numerator   = framerate & 0xffff; */
    .fr.framerate = (GST_VIDEO_INFO_FPS_N (&self->in_info) & 0xffff) |
        ((GST_VIDEO_INFO_FPS_D (&self->in_info) & 0xffff) << 16)
  };
  /* *INDENT-ON* */

  if (!gst_va_encoder_add_param (self->encoder, picture,
          VAEncMiscParameterBufferType, &framerate, sizeof (framerate))) {
    GST_ERROR_OBJECT (self, "Failed to create the frame rate parameter");
    return FALSE;
  }

  return TRUE;
}

static gboolean
_add_trellis_parameter (GstVaH264Enc * self, GstVaEncodePicture * picture)
{
  /* *INDENT-OFF* */
  struct
  {
    VAEncMiscParameterType type;
    VAEncMiscParameterQuantization tr;
  } trellis = {
    .type = VAEncMiscParameterTypeQuantization,
    .tr.quantization_flags.bits = {
       .disable_trellis = 0,
       .enable_trellis_I = 1,
       .enable_trellis_B = 1,
       .enable_trellis_P = 1,
    },
  };
  /* *INDENT-ON* */

  if (!self->use_trellis)
    return TRUE;

  if (!gst_va_encoder_add_param (self->encoder, picture,
          VAEncMiscParameterBufferType, &trellis, sizeof (trellis))) {
    GST_ERROR_OBJECT (self, "Failed to create the trellis parameter");
    return FALSE;
  }

  return TRUE;
}

static inline gboolean
_fill_picture_parameter (GstVaH264Enc * self, GstVaH264EncFrame * frame,
    VAEncPictureParameterBufferH264 * pic_param)
{
  guint i;

  /* *INDENT-OFF* */
  *pic_param = (VAEncPictureParameterBufferH264) {
    .CurrPic.picture_id = gst_va_encode_picture_get_reconstruct_surface (frame->picture),
    .CurrPic.TopFieldOrderCnt = frame->poc,
    .coded_buf = frame->picture->coded_buffer,
    /* Only support one sps and pps now. */
    .pic_parameter_set_id = 0,
    .seq_parameter_set_id = 0,
    /* means last encoding picture, EOS nal added. */
    .last_picture = frame->last_frame,
    .frame_num = frame->frame_num,

    .pic_init_qp = self->rc.qp_i,
    /* Use slice's these fields to control ref num. */
    .num_ref_idx_l0_active_minus1 = 0,
    .num_ref_idx_l1_active_minus1 = 0,
    .chroma_qp_index_offset = 0,
    .second_chroma_qp_index_offset = 0,
    /* picture fields */
    .pic_fields.bits.idr_pic_flag = (frame->frame_num == 0),
    .pic_fields.bits.reference_pic_flag = frame->is_ref,
    .pic_fields.bits.entropy_coding_mode_flag = self->use_cabac,
    .pic_fields.bits.weighted_pred_flag = 0,
    .pic_fields.bits.weighted_bipred_idc = 0,
    .pic_fields.bits.constrained_intra_pred_flag = 0,
    .pic_fields.bits.transform_8x8_mode_flag = self->use_dct8x8,
    /* enable debloking */
    .pic_fields.bits.deblocking_filter_control_present_flag = 1,
    .pic_fields.bits.redundant_pic_cnt_present_flag = 0,
    /* bottom_field_pic_order_in_frame_present_flag */
    .pic_fields.bits.pic_order_present_flag = 0,
    .pic_fields.bits.pic_scaling_matrix_present_flag = 0,
  };
  /* *INDENT-ON* */

  /* Non I frame, construct reference list. */
  i = 0;
  if (frame->type != GST_H264_I_SLICE) {
    GstVaH264EncFrame *f;

    if (g_queue_is_empty (&self->ref_list)) {
      GST_ERROR_OBJECT (self, "No reference found for frame type %s",
          _slice_type_name (frame->type));
      return FALSE;
    }

    g_assert (g_queue_get_length (&self->ref_list) <= self->gop.num_ref_frames);

    /* ref frames in queue are already sorted by frame_num. */
    for (; i < g_queue_get_length (&self->ref_list); i++) {
      f = _enc_frame (g_queue_peek_nth (&self->ref_list, i));

      pic_param->ReferenceFrames[i].picture_id =
          gst_va_encode_picture_get_reconstruct_surface (f->picture);
      pic_param->ReferenceFrames[i].TopFieldOrderCnt = f->poc;
      pic_param->ReferenceFrames[i].flags =
          VA_PICTURE_H264_SHORT_TERM_REFERENCE;
      pic_param->ReferenceFrames[i].frame_idx = f->frame_num;
    }
  }
  for (; i < 16; ++i)
    pic_param->ReferenceFrames[i].picture_id = VA_INVALID_ID;

  return TRUE;
};

static gboolean
_add_picture_parameter (GstVaH264Enc * self, GstVaH264EncFrame * frame,
    VAEncPictureParameterBufferH264 * pic_param)
{
  if (!gst_va_encoder_add_param (self->encoder, frame->picture,
          VAEncPictureParameterBufferType, pic_param,
          sizeof (VAEncPictureParameterBufferH264))) {
    GST_ERROR_OBJECT (self, "Failed to create the picture parameter");
    return FALSE;
  }

  return TRUE;
}

static void
_fill_pps (VAEncPictureParameterBufferH264 * pic_param, GstH264SPS * sps,
    GstH264PPS * pps)
{
  /* *INDENT-OFF* */
  *pps = (GstH264PPS) {
    .id = 0,
    .sequence = sps,
    .entropy_coding_mode_flag =
        pic_param->pic_fields.bits.entropy_coding_mode_flag,
    .pic_order_present_flag =
        pic_param->pic_fields.bits.pic_order_present_flag,
    .num_slice_groups_minus1 = 0,

    .num_ref_idx_l0_active_minus1 = pic_param->num_ref_idx_l0_active_minus1,
    .num_ref_idx_l1_active_minus1 = pic_param->num_ref_idx_l1_active_minus1,

    .weighted_pred_flag = pic_param->pic_fields.bits.weighted_pred_flag,
    .weighted_bipred_idc = pic_param->pic_fields.bits.weighted_bipred_idc,
    .pic_init_qp_minus26 = pic_param->pic_init_qp - 26,
    .pic_init_qs_minus26 = 0,
    .chroma_qp_index_offset = pic_param->chroma_qp_index_offset,
    .deblocking_filter_control_present_flag =
        pic_param->pic_fields.bits.deblocking_filter_control_present_flag,
    .constrained_intra_pred_flag =
        pic_param->pic_fields.bits.constrained_intra_pred_flag,
    .redundant_pic_cnt_present_flag =
        pic_param->pic_fields.bits.redundant_pic_cnt_present_flag,
    .transform_8x8_mode_flag =
        pic_param->pic_fields.bits.transform_8x8_mode_flag,
    /* unsupport scaling lists */
    .pic_scaling_matrix_present_flag = 0,
    .second_chroma_qp_index_offset = pic_param->second_chroma_qp_index_offset,
  };
  /* *INDENT-ON* */
}

static gboolean
_add_picture_header (GstVaH264Enc * self, GstVaH264EncFrame * frame,
    GstH264PPS * pps)
{
#define PPS_SIZE 4 + GST_ROUND_UP_8 (MAX_PPS_HDR_SIZE) / 8
  guint8 packed_pps[PPS_SIZE] = { 0, };
#undef PPS_SIZE
  gsize size;

  size = sizeof (packed_pps);
  if (gst_h264_bit_writer_pps (pps, TRUE, packed_pps,
          &size) != GST_H264_BIT_WRITER_OK) {
    GST_ERROR_OBJECT (self, "Failed to generate the picture header");
    return FALSE;
  }

  if (!gst_va_encoder_add_packed_header (self->encoder, frame->picture,
          VAEncPackedHeaderPicture, packed_pps, size, FALSE)) {
    GST_ERROR_OBJECT (self, "Failed to add the packed picture header");
    return FALSE;
  }

  return TRUE;
}

static gboolean
_add_one_slice (GstVaH264Enc * self, GstVaH264EncFrame * frame,
    gint start_mb, gint mb_size,
    VAEncSliceParameterBufferH264 * slice,
    GstVaH264EncFrame * list0[16], guint list0_num,
    GstVaH264EncFrame * list1[16], guint list1_num)
{
  int8_t slice_qp_delta = 0;
  gint i;

  /* *INDENT-OFF* */
  if (self->rc.rc_ctrl_mode == VA_RC_CQP) {
    if (frame->type == GST_H264_P_SLICE) {
      slice_qp_delta = self->rc.qp_p - self->rc.qp_i;
    } else if (frame->type == GST_H264_B_SLICE) {
      slice_qp_delta = (int8_t) (self->rc.qp_b - self->rc.qp_i);
    }
    g_assert (slice_qp_delta <= 51 && slice_qp_delta >= -51);
  }

  *slice = (VAEncSliceParameterBufferH264) {
    .macroblock_address = start_mb,
    .num_macroblocks = mb_size,
    .macroblock_info = VA_INVALID_ID,
    .slice_type = (uint8_t) frame->type,
    /* Only one parameter set supported now. */
    .pic_parameter_set_id = 0,
    .idr_pic_id = self->gop.total_idr_count,
    .pic_order_cnt_lsb = frame->poc,
    /* Not support top/bottom. */
    .delta_pic_order_cnt_bottom = 0,
    .delta_pic_order_cnt[0] = 0,
    .delta_pic_order_cnt[1] = 0,

    .direct_spatial_mv_pred_flag = TRUE,
    /* .num_ref_idx_active_override_flag = , */
    /* .num_ref_idx_l0_active_minus1 = , */
    /* .num_ref_idx_l1_active_minus1 = , */
    /* Set the reference list later. */

    .luma_log2_weight_denom = 0,
    .chroma_log2_weight_denom = 0,
    .luma_weight_l0_flag = 0,
    .chroma_weight_l0_flag = 0,
    .luma_weight_l1_flag = 0,
    .chroma_weight_l1_flag = 0,

    .cabac_init_idc = 0,
    /* Just use picture default setting. */
    .slice_qp_delta = slice_qp_delta,

    .disable_deblocking_filter_idc = 0,
    .slice_alpha_c0_offset_div2 = 2,
    .slice_beta_offset_div2 = 2,
  };
  /* *INDENT-ON* */

  if (frame->type == GST_H264_B_SLICE || frame->type == GST_H264_P_SLICE) {
    slice->num_ref_idx_active_override_flag = (list0_num > 0 || list1_num > 0);
    slice->num_ref_idx_l0_active_minus1 = list0_num > 0 ? list0_num - 1 : 0;
    if (frame->type == GST_H264_B_SLICE)
      slice->num_ref_idx_l1_active_minus1 = list1_num > 0 ? list1_num - 1 : 0;
  }

  i = 0;
  if (frame->type != GST_H264_I_SLICE) {
    for (; i < list0_num; i++) {
      slice->RefPicList0[i].picture_id =
          gst_va_encode_picture_get_reconstruct_surface (list0[i]->picture);
      slice->RefPicList0[i].TopFieldOrderCnt = list0[i]->poc;
      slice->RefPicList0[i].flags |= VA_PICTURE_H264_SHORT_TERM_REFERENCE;
      slice->RefPicList0[i].frame_idx = list0[i]->frame_num;
    }
  }
  for (; i < G_N_ELEMENTS (slice->RefPicList0); ++i) {
    slice->RefPicList0[i].picture_id = VA_INVALID_SURFACE;
    slice->RefPicList0[i].flags = VA_PICTURE_H264_INVALID;
  }

  i = 0;
  if (frame->type == GST_H264_B_SLICE) {
    for (; i < list1_num; i++) {
      slice->RefPicList1[i].picture_id =
          gst_va_encode_picture_get_reconstruct_surface (list1[i]->picture);
      slice->RefPicList1[i].TopFieldOrderCnt = list1[i]->poc;
      slice->RefPicList1[i].flags |= VA_PICTURE_H264_SHORT_TERM_REFERENCE;
      slice->RefPicList1[i].frame_idx = list1[i]->frame_num;
    }
  }
  for (; i < G_N_ELEMENTS (slice->RefPicList1); ++i) {
    slice->RefPicList1[i].picture_id = VA_INVALID_SURFACE;
    slice->RefPicList1[i].flags = VA_PICTURE_H264_INVALID;
  }

  if (!gst_va_encoder_add_param (self->encoder, frame->picture,
          VAEncSliceParameterBufferType, slice,
          sizeof (VAEncSliceParameterBufferH264))) {
    GST_ERROR_OBJECT (self, "Failed to create the slice parameter");
    return FALSE;
  }

  return TRUE;
}

static gint
_poc_asc_compare (const GstVaH264EncFrame ** a, const GstVaH264EncFrame ** b)
{
  return (*a)->poc - (*b)->poc;
}

static gint
_poc_des_compare (const GstVaH264EncFrame ** a, const GstVaH264EncFrame ** b)
{
  return (*b)->poc - (*a)->poc;
}

static gint
_frame_num_asc_compare (const GstVaH264EncFrame ** a,
    const GstVaH264EncFrame ** b)
{
  return (*a)->frame_num - (*b)->frame_num;
}

static gint
_frame_num_des_compare (const GstVaH264EncFrame ** a,
    const GstVaH264EncFrame ** b)
{
  return (*b)->frame_num - (*a)->frame_num;
}

/* If all the pic_num in the same order, OK. */
static gboolean
_ref_list_need_reorder (GstVaH264EncFrame * list[16], guint list_num,
    gboolean is_asc)
{
  guint i;
  gint pic_num_diff;

  if (list_num <= 1)
    return FALSE;

  for (i = 1; i < list_num; i++) {
    pic_num_diff = list[i]->frame_num - list[i - 1]->frame_num;
    g_assert (pic_num_diff != 0);

    if (pic_num_diff > 0 && !is_asc)
      return TRUE;

    if (pic_num_diff < 0 && is_asc)
      return TRUE;
  }

  return FALSE;
}

static void
_insert_ref_pic_list_modification (GstH264SliceHdr * slice_hdr,
    GstVaH264EncFrame * list[16], guint list_num, gboolean is_asc)
{
  GstVaH264EncFrame *list_by_pic_num[16] = { };
  guint modification_num, i;
  GstH264RefPicListModification *ref_pic_list_modification = NULL;
  gint pic_num_diff, pic_num_lx_pred;

  memcpy (list_by_pic_num, list, sizeof (GstVaH264EncFrame *) * list_num);

  if (is_asc) {
    g_qsort_with_data (list_by_pic_num, list_num, sizeof (gpointer),
        (GCompareDataFunc) _frame_num_asc_compare, NULL);
  } else {
    g_qsort_with_data (list_by_pic_num, list_num, sizeof (gpointer),
        (GCompareDataFunc) _frame_num_des_compare, NULL);
  }

  modification_num = 0;
  for (i = 0; i < list_num; i++) {
    if (list_by_pic_num[i]->poc != list[i]->poc)
      modification_num = i + 1;
  }
  g_assert (modification_num > 0);

  if (is_asc) {
    slice_hdr->ref_pic_list_modification_flag_l1 = 1;
    slice_hdr->n_ref_pic_list_modification_l1 =
        modification_num + 1 /* The end operation. */ ;
    ref_pic_list_modification = slice_hdr->ref_pic_list_modification_l1;
  } else {
    slice_hdr->ref_pic_list_modification_flag_l0 = 1;
    slice_hdr->n_ref_pic_list_modification_l0 =
        modification_num + 1 /* The end operation. */ ;
    ref_pic_list_modification = slice_hdr->ref_pic_list_modification_l0;
  }

  pic_num_lx_pred = slice_hdr->frame_num;
  for (i = 0; i < modification_num; i++) {
    pic_num_diff = list[i]->frame_num - pic_num_lx_pred;
    /* For the nex loop. */
    pic_num_lx_pred = list[i]->frame_num;

    g_assert (pic_num_diff != 0);

    if (pic_num_diff > 0) {
      ref_pic_list_modification->modification_of_pic_nums_idc = 1;
      ref_pic_list_modification->value.abs_diff_pic_num_minus1 =
          pic_num_diff - 1;
    } else {
      ref_pic_list_modification->modification_of_pic_nums_idc = 0;
      ref_pic_list_modification->value.abs_diff_pic_num_minus1 =
          (-pic_num_diff) - 1;
    }

    ref_pic_list_modification++;
  }

  ref_pic_list_modification->modification_of_pic_nums_idc = 3;
}

static void
_insert_ref_pic_marking_for_unused_frame (GstH264SliceHdr * slice_hdr,
    gint cur_frame_num, gint unused_frame_num)
{
  GstH264RefPicMarking *refpicmarking;

  slice_hdr->dec_ref_pic_marking.adaptive_ref_pic_marking_mode_flag = 1;
  slice_hdr->dec_ref_pic_marking.n_ref_pic_marking = 2;

  refpicmarking = &slice_hdr->dec_ref_pic_marking.ref_pic_marking[0];

  refpicmarking->memory_management_control_operation = 1;
  refpicmarking->difference_of_pic_nums_minus1 =
      cur_frame_num - unused_frame_num - 1;

  refpicmarking = &slice_hdr->dec_ref_pic_marking.ref_pic_marking[1];
  refpicmarking->memory_management_control_operation = 0;
}

static gboolean
_add_slice_header (GstVaH264Enc * self, GstVaH264EncFrame * frame,
    GstH264PPS * pps, VAEncSliceParameterBufferH264 * slice,
    GstVaH264EncFrame * list0[16], guint list0_num,
    GstVaH264EncFrame * list1[16], guint list1_num)
{
  GstH264SliceHdr slice_hdr;
  gsize size;
  GstH264NalUnitType nal_type = GST_H264_NAL_SLICE;
#define SLICE_HDR_SIZE 4 + GST_ROUND_UP_8 (MAX_SLICE_HDR_SIZE) / 8
  guint8 packed_slice_hdr[SLICE_HDR_SIZE] = { 0, };
#undef SLICE_HDR_SIZE

  if (frame->frame_num == 0)
    nal_type = GST_H264_NAL_SLICE_IDR;

  /* *INDENT-OFF* */
  slice_hdr = (GstH264SliceHdr) {
    .first_mb_in_slice = slice->macroblock_address,
    .type = slice->slice_type,
    .pps = pps,
    .frame_num = frame->frame_num,
    /* interlaced not supported now. */
    .field_pic_flag = 0,
    .bottom_field_flag = 0,
    .idr_pic_id = (frame->frame_num == 0 ? slice->idr_pic_id : 0),
    /* only pic_order_cnt_type 1 is supported now. */
    .pic_order_cnt_lsb = slice->pic_order_cnt_lsb,
    .delta_pic_order_cnt_bottom = slice->delta_pic_order_cnt_bottom,
     /* Only for B frame. */
    .direct_spatial_mv_pred_flag =
        (frame->type == GST_H264_B_SLICE ?
         slice->direct_spatial_mv_pred_flag : 0),

    .num_ref_idx_active_override_flag = slice->num_ref_idx_active_override_flag,
    .num_ref_idx_l0_active_minus1 = slice->num_ref_idx_l0_active_minus1,
    .num_ref_idx_l1_active_minus1 = slice->num_ref_idx_l1_active_minus1,
    /* Calculate it later. */
    .ref_pic_list_modification_flag_l0 = 0,
    .ref_pic_list_modification_flag_l1 = 0,
    /* We have weighted_pred_flag and weighted_bipred_idc 0 here, no
     * need weight_table. */

    .dec_ref_pic_marking = {
      .no_output_of_prior_pics_flag = 0,
      .long_term_reference_flag = 0,
      /* If not sliding_window, we set it later. */
      .adaptive_ref_pic_marking_mode_flag = 0,
    },

    .cabac_init_idc = slice->cabac_init_idc,
    .slice_qp_delta = slice->slice_qp_delta,

    .disable_deblocking_filter_idc = slice->disable_deblocking_filter_idc,
    .slice_alpha_c0_offset_div2 = slice->slice_alpha_c0_offset_div2,
    .slice_beta_offset_div2 = slice->slice_beta_offset_div2,
  };
  /* *INDENT-ON* */

  /* Reorder the ref lists if needed. */
  if (list0_num > 1) {
    /* list0 is in poc descend order now. */
    if (_ref_list_need_reorder (list0, list0_num, FALSE))
      _insert_ref_pic_list_modification (&slice_hdr, list0, list0_num, FALSE);
  }

  if (list0_num > 1) {
    /* list0 is in poc ascend order now. */
    if (_ref_list_need_reorder (list1, list1_num, TRUE)) {
      _insert_ref_pic_list_modification (&slice_hdr, list1, list1_num, TRUE);
    }
  }

  /* Mark the unused reference explicitly which this frame replaces. */
  if (frame->unused_for_reference_pic_num >= 0) {
    g_assert (frame->is_ref);
    _insert_ref_pic_marking_for_unused_frame (&slice_hdr, frame->frame_num,
        frame->unused_for_reference_pic_num);
  }

  size = sizeof (packed_slice_hdr);
  if (gst_h264_bit_writer_slice_hdr (&slice_hdr, TRUE, nal_type, frame->is_ref,
          packed_slice_hdr, &size) != GST_H264_BIT_WRITER_OK) {
    GST_ERROR_OBJECT (self, "Failed to generate the slice header");
    return FALSE;
  }

  if (!gst_va_encoder_add_packed_header (self->encoder, frame->picture,
          VAEncPackedHeaderSlice, packed_slice_hdr, size, FALSE)) {
    GST_ERROR_OBJECT (self, "Failed to add the packed slice header");
    return FALSE;
  }

  return TRUE;
}

static gboolean
_add_aud (GstVaH264Enc * self, GstVaH264EncFrame * frame)
{
  guint8 aud_data[8] = { };
  gsize size;
  guint8 primary_pic_type = 0;

  switch (frame->type) {
    case GST_H264_I_SLICE:
      primary_pic_type = 0;
      break;
    case GST_H264_P_SLICE:
      primary_pic_type = 1;
      break;
    case GST_H264_B_SLICE:
      primary_pic_type = 2;
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  size = sizeof (aud_data);
  if (gst_h264_bit_writer_aud (primary_pic_type, TRUE, aud_data,
          &size) != GST_H264_BIT_WRITER_OK) {
    GST_ERROR_OBJECT (self, "Failed to generate the AUD");
    return FALSE;
  }

  if (!gst_va_encoder_add_packed_header (self->encoder, frame->picture,
          VAEncPackedHeaderRawData, aud_data, size, FALSE)) {
    GST_ERROR_OBJECT (self, "Failed to add the AUD");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_va_h264_enc_encode_frame (GstVaH264Enc * self,
    GstVideoCodecFrame * gst_frame)
{
  VAEncPictureParameterBufferH264 pic_param;
  GstH264PPS pps;
  GstVaH264EncFrame *list0[16] = { NULL, };
  guint list0_num = 0;
  GstVaH264EncFrame *list1[16] = { NULL, };
  guint list1_num = 0;
  guint slice_of_mbs, slice_mod_mbs, slice_start_mb, slice_mbs;
  gint i;
  GstVaH264EncFrame *frame;

  g_return_val_if_fail (gst_frame, FALSE);

  frame = _enc_frame (gst_frame);

  /* Repeat the SPS for IDR. */
  if (frame->poc == 0) {
    VAEncSequenceParameterBufferH264 sequence;

    if (!_add_rate_control_parameter (self, frame->picture))
      return FALSE;

    if (!_add_quality_level_parameter (self, frame->picture))
      return FALSE;

    if (!_add_frame_rate_parameter (self, frame->picture))
      return FALSE;

    if (!_add_hrd_parameter (self, frame->picture))
      return FALSE;

    if (!_add_trellis_parameter (self, frame->picture))
      return FALSE;

    _fill_sequence_param (self, &sequence);
    if (!_fill_sps (self, &sequence))
      return FALSE;

    if (!_add_sequence_parameter (self, frame->picture, &sequence))
      return FALSE;

    if ((self->packed_headers & VA_ENC_PACKED_HEADER_SEQUENCE)
        && !_add_sequence_header (self, frame))
      return FALSE;
  }

  if (self->prop.aud) {
    if ((self->packed_headers & VA_ENC_PACKED_HEADER_RAW_DATA)
        && !_add_aud (self, frame))
      return FALSE;
  }

  /* Non I frame, construct reference list. */
  if (frame->type != GST_H264_I_SLICE) {
    GstVaH264EncFrame *vaf;
    GstVideoCodecFrame *f;

    for (i = g_queue_get_length (&self->ref_list) - 1; i >= 0; i--) {
      f = g_queue_peek_nth (&self->ref_list, i);
      vaf = _enc_frame (f);
      if (vaf->poc > frame->poc)
        continue;

      list0[list0_num] = vaf;
      list0_num++;
    }

    /* reorder to select the most nearest forward frames. */
    g_qsort_with_data (list0, list0_num, sizeof (gpointer),
        (GCompareDataFunc) _poc_des_compare, NULL);

    if (list0_num > self->gop.ref_num_list0)
      list0_num = self->gop.ref_num_list0;
  }

  if (frame->type == GST_H264_B_SLICE) {
    GstVaH264EncFrame *vaf;
    GstVideoCodecFrame *f;

    for (i = 0; i < g_queue_get_length (&self->ref_list); i++) {
      f = g_queue_peek_nth (&self->ref_list, i);
      vaf = _enc_frame (f);
      if (vaf->poc < frame->poc)
        continue;

      list1[list1_num] = vaf;
      list1_num++;
    }

    /* reorder to select the most nearest backward frames. */
    g_qsort_with_data (list1, list1_num, sizeof (gpointer),
        (GCompareDataFunc) _poc_asc_compare, NULL);

    if (list1_num > self->gop.ref_num_list1)
      list1_num = self->gop.ref_num_list1;
  }

  g_assert (list0_num + list1_num <= self->gop.num_ref_frames);

  if (!_fill_picture_parameter (self, frame, &pic_param))
    return FALSE;
  if (!_add_picture_parameter (self, frame, &pic_param))
    return FALSE;
  _fill_pps (&pic_param, &self->sequence_hdr, &pps);

  if ((self->packed_headers & VA_ENC_PACKED_HEADER_PICTURE)
      && frame->type == GST_H264_I_SLICE
      && !_add_picture_header (self, frame, &pps))
    return FALSE;

  slice_of_mbs = self->mb_width * self->mb_height / self->num_slices;
  slice_mod_mbs = self->mb_width * self->mb_height % self->num_slices;
  slice_start_mb = 0;
  slice_mbs = 0;
  for (i = 0; i < self->num_slices; i++) {
    VAEncSliceParameterBufferH264 slice;

    slice_mbs = slice_of_mbs;
    /* divide the remainder to each equally */
    if (slice_mod_mbs) {
      slice_mbs++;
      slice_mod_mbs--;
    }

    if (!_add_one_slice (self, frame, slice_start_mb, slice_mbs, &slice,
            list0, list0_num, list1, list1_num))
      return FALSE;

    if ((self->packed_headers & VA_ENC_PACKED_HEADER_SLICE) &&
        (!_add_slice_header (self, frame, &pps, &slice, list0, list0_num, list1,
                list1_num)))
      return FALSE;

    slice_start_mb += slice_mbs;
  }

  if (!gst_va_encoder_encode (self->encoder, frame->picture)) {
    GST_ERROR_OBJECT (self, "Encode frame error");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_va_h264_enc_start (GstVideoEncoder * encoder)
{
  GstVaH264Enc *self = GST_VA_H264_ENC (encoder);

  /* Set the minimum pts to some huge value (1000 hours). This keeps
   * the dts at the start of the stream from needing to be
   * negative. */
  self->start_pts = GST_SECOND * 60 * 60 * 1000;
  gst_video_encoder_set_min_pts (encoder, self->start_pts);

  return TRUE;
}

static gboolean
gst_va_h264_enc_open (GstVideoEncoder * venc)
{
  GstVaH264Enc *encoder = GST_VA_H264_ENC (venc);
  GstVaH264EncClass *klass = GST_VA_H264_ENC_GET_CLASS (venc);
  gboolean ret = FALSE;

  if (!gst_va_ensure_element_data (venc, klass->render_device_path,
          &encoder->display))
    return FALSE;

  if (!g_atomic_pointer_get (&encoder->encoder)) {
    GstVaEncoder *va_encoder;

    va_encoder = gst_va_encoder_new (encoder->display, klass->codec);
    if (va_encoder)
      ret = TRUE;

    gst_object_replace ((GstObject **) (&encoder->encoder),
        (GstObject *) va_encoder);
    gst_clear_object (&va_encoder);
  } else {
    ret = TRUE;
  }

  return ret;
}

static gboolean
gst_va_h264_enc_close (GstVideoEncoder * venc)
{
  GstVaH264Enc *self = GST_VA_H264_ENC (venc);

  gst_va_h264_enc_reset_state (self);

  gst_clear_object (&self->encoder);
  gst_clear_object (&self->display);

  return TRUE;
}

static GstCaps *
gst_va_h264_enc_get_caps (GstVideoEncoder * venc, GstCaps * filter)
{
  GstVaH264Enc *self = GST_VA_H264_ENC (venc);
  GstCaps *caps = NULL, *tmp;

  if (self->encoder)
    caps = gst_va_encoder_get_sinkpad_caps (self->encoder);

  if (caps) {
    if (filter) {
      tmp = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
      gst_caps_unref (caps);
      caps = tmp;
    }
  } else {
    caps = gst_video_encoder_proxy_getcaps (venc, NULL, filter);
  }

  GST_LOG_OBJECT (self, "Returning caps %" GST_PTR_FORMAT, caps);
  return caps;
}

static void
_flush_all_frames (GstVideoEncoder * venc)
{
  GstVaH264Enc *self = GST_VA_H264_ENC (venc);

  g_queue_clear_full (&self->reorder_list,
      (GDestroyNotify) gst_video_codec_frame_unref);
  g_queue_clear_full (&self->output_list,
      (GDestroyNotify) gst_video_codec_frame_unref);
  g_queue_clear_full (&self->ref_list,
      (GDestroyNotify) gst_video_codec_frame_unref);
}

static gboolean
gst_va_h264_enc_flush (GstVideoEncoder * venc)
{
  GstVaH264Enc *self = GST_VA_H264_ENC (venc);

  _flush_all_frames (venc);

  /* begin from an IDR after flush. */
  self->gop.cur_frame_index = 0;
  self->gop.cur_frame_num = 0;

  return TRUE;
}

static gboolean
gst_va_h264_enc_stop (GstVideoEncoder * venc)
{
  GstVaH264Enc *const self = GST_VA_H264_ENC (venc);

  _flush_all_frames (venc);

  if (!gst_va_encoder_close (self->encoder)) {
    GST_ERROR_OBJECT (self, "Failed to close the VA encoder");
    return FALSE;
  }

  if (self->raw_pool)
    gst_buffer_pool_set_active (self->raw_pool, FALSE);
  gst_clear_object (&self->raw_pool);

  if (self->input_state)
    gst_video_codec_state_unref (self->input_state);
  self->input_state = NULL;
  if (self->output_state)
    gst_video_codec_state_unref (self->output_state);
  self->output_state = NULL;

  gst_clear_caps (&self->in_caps);

  return TRUE;
}

static gboolean
_try_import_buffer (GstVaH264Enc * self, GstBuffer * inbuf)
{
  VASurfaceID surface;

  /* The VA buffer. */
  surface = gst_va_buffer_get_surface (inbuf);
  if (surface != VA_INVALID_ID)
    return TRUE;

  /* TODO: DMA buffer. */

  return FALSE;
}

static GstBufferPool *
_get_sinkpad_pool (GstVaH264Enc * self)
{
  GstAllocator *allocator;
  GstAllocationParams params = { 0, };
  guint size, usage_hint = 0;
  GArray *surface_formats = NULL;
  GstCaps *caps;

  if (self->raw_pool)
    return self->raw_pool;

  g_assert (self->in_caps);
  caps = gst_caps_copy (self->in_caps);
  gst_caps_set_features_simple (caps,
      gst_caps_features_from_string (GST_CAPS_FEATURE_MEMORY_VA));

  gst_allocation_params_init (&params);

  size = GST_VIDEO_INFO_SIZE (&self->in_info);

  surface_formats = gst_va_encoder_get_surface_formats (self->encoder);

  allocator = gst_va_allocator_new (self->display, surface_formats);

  self->raw_pool = gst_va_pool_new_with_config (caps, size, 1, 0,
      usage_hint, GST_VA_FEATURE_AUTO, allocator, &params);
  if (!self->raw_pool) {
    gst_object_unref (allocator);
    return NULL;
  }

  gst_va_allocator_get_format (allocator, &self->sinkpad_info, NULL, NULL);

  gst_object_unref (allocator);

  gst_buffer_pool_set_active (self->raw_pool, TRUE);

  return self->raw_pool;
}

static GstFlowReturn
_import_input_buffer (GstVaH264Enc * self, GstBuffer * inbuf, GstBuffer ** buf)
{
  GstBuffer *buffer = NULL;
  GstBufferPool *pool;
  GstFlowReturn ret;
  GstVideoFrame in_frame, out_frame;
  gboolean imported, copied;

  imported = _try_import_buffer (self, inbuf);
  if (imported) {
    *buf = gst_buffer_ref (inbuf);
    return GST_FLOW_OK;
  }

  /* input buffer doesn't come from a vapool, thus it is required to
   * have a pool, grab from it a new buffer and copy the input
   * buffer to the new one */
  if (!(pool = _get_sinkpad_pool (self)))
    return GST_FLOW_ERROR;

  ret = gst_buffer_pool_acquire_buffer (pool, &buffer, NULL);
  if (ret != GST_FLOW_OK)
    return ret;

  GST_LOG_OBJECT (self, "copying input frame");

  if (!gst_video_frame_map (&in_frame, &self->in_info, inbuf, GST_MAP_READ))
    goto invalid_buffer;
  if (!gst_video_frame_map (&out_frame, &self->sinkpad_info, buffer,
          GST_MAP_WRITE)) {
    gst_video_frame_unmap (&in_frame);
    goto invalid_buffer;
  }

  copied = gst_video_frame_copy (&out_frame, &in_frame);

  gst_video_frame_unmap (&out_frame);
  gst_video_frame_unmap (&in_frame);

  if (!copied)
    goto invalid_buffer;

  /* strictly speaking this is not needed but let's play safe */
  if (!gst_buffer_copy_into (buffer, inbuf, GST_BUFFER_COPY_FLAGS |
          GST_BUFFER_COPY_TIMESTAMPS, 0, -1))
    return GST_FLOW_ERROR;

  *buf = buffer;

  return GST_FLOW_OK;

invalid_buffer:
  {
    GST_ELEMENT_WARNING (self, CORE, NOT_IMPLEMENTED, (NULL),
        ("invalid video buffer received"));
    if (buffer)
      gst_buffer_unref (buffer);
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
_push_buffer_to_downstream (GstVaH264Enc * self, GstVideoCodecFrame * frame)
{
  GstVaH264EncFrame *frame_enc;
  GstFlowReturn ret;
  guint coded_size;
  goffset offset;
  GstBuffer *buf;
  VASurfaceID surface;
  VACodedBufferSegment *seg, *seg_list;

  frame_enc = _enc_frame (frame);

  /* Wait for encoding to finish */
  surface = gst_va_encode_picture_get_raw_surface (frame_enc->picture);
  if (!va_sync_surface (self->display, surface))
    goto error;

  seg_list = NULL;
  if (!va_map_buffer (self->display, frame_enc->picture->coded_buffer,
          (gpointer *) & seg_list))
    goto error;

  if (!seg_list) {
    GST_WARNING_OBJECT (self, "coded buffer has no segment list");
    goto error;
  }

  coded_size = 0;
  for (seg = seg_list; seg; seg = seg->next)
    coded_size += seg->size;

  buf = gst_video_encoder_allocate_output_buffer (GST_VIDEO_ENCODER_CAST (self),
      coded_size);
  if (!buf) {
    GST_ERROR_OBJECT (self, "Failed to allocate output buffer, size %d",
        coded_size);
    goto error;
  }

  offset = 0;
  for (seg = seg_list; seg; seg = seg->next) {
    gsize write_size;

    write_size = gst_buffer_fill (buf, offset, seg->buf, seg->size);
    if (write_size != seg->size) {
      GST_WARNING_OBJECT (self, "Segment size is %d, but copied %"
          G_GSIZE_FORMAT, seg->size, write_size);
      break;
    }
    offset += seg->size;
  }

  va_unmap_buffer (self->display, frame_enc->picture->coded_buffer);

  frame->pts =
      self->start_pts + self->frame_duration * frame_enc->total_frame_count;
  /* The PTS should always be later than the DTS. */
  frame->dts = self->start_pts + self->frame_duration *
      ((gint64) self->output_frame_count -
      (gint64) self->gop.num_reorder_frames);
  self->output_frame_count++;
  frame->duration = self->frame_duration;

  gst_buffer_replace (&frame->output_buffer, buf);
  gst_clear_buffer (&buf);

  GST_LOG_OBJECT (self, "Push to downstream: frame system_frame_number: %d,"
      " pts: %" GST_TIME_FORMAT ", dts: %" GST_TIME_FORMAT
      " duration: %" GST_TIME_FORMAT ", buffer size: %" G_GSIZE_FORMAT,
      frame->system_frame_number, GST_TIME_ARGS (frame->pts),
      GST_TIME_ARGS (frame->dts), GST_TIME_ARGS (frame->duration),
      gst_buffer_get_size (frame->output_buffer));

  ret = gst_video_encoder_finish_frame (GST_VIDEO_ENCODER (self), frame);
  return ret;

error:
  gst_clear_buffer (&frame->output_buffer);
  gst_clear_buffer (&buf);
  gst_video_encoder_finish_frame (GST_VIDEO_ENCODER (self), frame);

  return GST_FLOW_ERROR;
}

static gboolean
_reorder_frame (GstVideoEncoder * venc, GstVideoCodecFrame * frame,
    gboolean bump_all, GstVideoCodecFrame ** out_frame)
{
  GstVaH264Enc *self = GST_VA_H264_ENC (venc);
  GstVaH264EncClass *klass = GST_VA_H264_ENC_GET_CLASS (self);

  g_assert (klass->push_frame);
  if (!klass->push_frame (self, frame, bump_all)) {
    GST_ERROR_OBJECT (self, "Failed to push the input frame"
        " system_frame_number: %d into the reorder list",
        frame->system_frame_number);

    *out_frame = NULL;
    return FALSE;
  }

  g_assert (klass->pop_frame);
  if (!klass->pop_frame (self, out_frame)) {
    GST_ERROR_OBJECT (self, "Failed to pop the frame from the reorder list");
    *out_frame = NULL;
    return FALSE;
  }

  return TRUE;
}

static gint
_sort_by_frame_num (gconstpointer a, gconstpointer b, gpointer user_data)
{
  GstVaH264EncFrame *frame1 = _enc_frame ((GstVideoCodecFrame *) a);
  GstVaH264EncFrame *frame2 = _enc_frame ((GstVideoCodecFrame *) b);

  g_assert (frame1->frame_num != frame2->frame_num);

  return frame1->frame_num - frame2->frame_num;
}

static GstVideoCodecFrame *
_find_unused_reference_frame (GstVaH264Enc * self, GstVaH264EncFrame * frame)
{
  GstVaH264EncFrame *b_vaframe;
  GstVideoCodecFrame *b_frame;
  guint i;

  /* We still have more space. */
  if (g_queue_get_length (&self->ref_list) < self->gop.num_ref_frames)
    return NULL;

  /* Not b_pyramid, sliding window is enough. */
  if (!self->gop.b_pyramid)
    return g_queue_peek_head (&self->ref_list);

  /* I/P frame, just using sliding window. */
  if (frame->type != GST_H264_B_SLICE)
    return g_queue_peek_head (&self->ref_list);

  /* Choose the B frame with lowest POC. */
  b_frame = NULL;
  b_vaframe = NULL;
  for (i = 0; i < g_queue_get_length (&self->ref_list); i++) {
    GstVaH264EncFrame *vaf;
    GstVideoCodecFrame *f;

    f = g_queue_peek_nth (&self->ref_list, i);
    vaf = _enc_frame (f);
    if (vaf->type != GST_H264_B_SLICE)
      continue;

    if (!b_frame) {
      b_frame = f;
      b_vaframe = _enc_frame (b_frame);
      continue;
    }

    b_vaframe = _enc_frame (b_frame);
    g_assert (vaf->poc != b_vaframe->poc);
    if (vaf->poc < b_vaframe->poc) {
      b_frame = f;
      b_vaframe = _enc_frame (b_frame);
    }
  }

  /* No B frame as ref. */
  if (!b_frame)
    return g_queue_peek_head (&self->ref_list);

  if (b_frame != g_queue_peek_head (&self->ref_list)) {
    b_vaframe = _enc_frame (b_frame);
    frame->unused_for_reference_pic_num = b_vaframe->frame_num;
    GST_LOG_OBJECT (self, "The frame with POC: %d, pic_num %d will be"
        " replaced by the frame with POC: %d, pic_num %d explicitly by"
        " using memory_management_control_operation=1",
        b_vaframe->poc, b_vaframe->frame_num, frame->poc, frame->frame_num);
  }

  return b_frame;
}

static GstFlowReturn
_encode_frame (GstVideoEncoder * venc, GstVideoCodecFrame * gst_frame)
{
  GstVaH264Enc *self = GST_VA_H264_ENC (venc);
  GstVaH264EncClass *klass = GST_VA_H264_ENC_GET_CLASS (self);
  GstVaH264EncFrame *frame;
  GstVideoCodecFrame *unused_ref = NULL;

  frame = _enc_frame (gst_frame);
  g_assert (frame->picture == NULL);
  frame->picture = gst_va_encode_picture_new (self->encoder,
      gst_frame->input_buffer);

  if (!frame->picture) {
    GST_ERROR_OBJECT (venc, "Failed to create the encode picture");
    return GST_FLOW_ERROR;
  }

  if (frame->is_ref)
    unused_ref = _find_unused_reference_frame (self, frame);

  if (!klass->encode_frame (self, gst_frame)) {
    GST_ERROR_OBJECT (venc, "Failed to encode the frame");
    return GST_FLOW_ERROR;
  }

  g_queue_push_tail (&self->output_list, gst_video_codec_frame_ref (gst_frame));

  if (frame->is_ref) {
    if (unused_ref) {
      if (!g_queue_remove (&self->ref_list, unused_ref))
        g_assert_not_reached ();

      gst_video_codec_frame_unref (unused_ref);
    }

    /* Add it into the reference list. */
    g_queue_push_tail (&self->ref_list, gst_video_codec_frame_ref (gst_frame));
    g_queue_sort (&self->ref_list, _sort_by_frame_num, NULL);

    g_assert (g_queue_get_length (&self->ref_list) <= self->gop.num_ref_frames);
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_va_h264_enc_handle_frame (GstVideoEncoder * venc,
    GstVideoCodecFrame * frame)
{
  GstVaH264Enc *self = GST_VA_H264_ENC (venc);
  GstFlowReturn ret;
  GstBuffer *in_buf = NULL;
  GstVaH264EncFrame *frame_in = NULL;
  GstVideoCodecFrame *frame_out, *frame_encode = NULL;

  GST_LOG_OBJECT (venc,
      "handle frame id %d, dts %" GST_TIME_FORMAT ", pts %" GST_TIME_FORMAT,
      frame->system_frame_number,
      GST_TIME_ARGS (GST_BUFFER_DTS (frame->input_buffer)),
      GST_TIME_ARGS (GST_BUFFER_PTS (frame->input_buffer)));

  ret = _import_input_buffer (self, frame->input_buffer, &in_buf);
  if (ret != GST_FLOW_OK)
    goto error_buffer_invalid;

  gst_buffer_replace (&frame->input_buffer, in_buf);
  gst_clear_buffer (&in_buf);

  frame_in = gst_va_enc_frame_new ();
  frame_in->total_frame_count = self->input_frame_count++;
  gst_video_codec_frame_set_user_data (frame, frame_in, gst_va_enc_frame_free);

  if (!_reorder_frame (venc, frame, FALSE, &frame_encode))
    goto error_reorder;

  /* pass it to reorder list and we should not use it again. */
  frame = NULL;

  while (frame_encode) {
    ret = _encode_frame (venc, frame_encode);
    if (ret != GST_FLOW_OK)
      goto error_encode;

    while (g_queue_get_length (&self->output_list) > 0) {
      frame_out = g_queue_pop_head (&self->output_list);
      gst_video_codec_frame_unref (frame_out);
      ret = _push_buffer_to_downstream (self, frame_out);
      if (ret != GST_FLOW_OK)
        goto error_push_buffer;
    }

    frame_encode = NULL;
    if (!_reorder_frame (venc, NULL, FALSE, &frame_encode))
      goto error_reorder;
  }

  return ret;

error_buffer_invalid:
  {
    GST_ELEMENT_ERROR (venc, STREAM, ENCODE,
        ("Failed to import the input frame."), (NULL));
    gst_clear_buffer (&in_buf);
    gst_clear_buffer (&frame->output_buffer);
    gst_video_encoder_finish_frame (venc, frame);
    return ret;
  }
error_reorder:
  {
    GST_ELEMENT_ERROR (venc, STREAM, ENCODE,
        ("Failed to reorder the input frame."), (NULL));
    if (frame) {
      gst_clear_buffer (&frame->output_buffer);
      gst_video_encoder_finish_frame (venc, frame);
    }
    return GST_FLOW_ERROR;
  }
error_encode:
  {
    GST_ELEMENT_ERROR (venc, STREAM, ENCODE,
        ("Failed to encode the frame."), (NULL));
    gst_clear_buffer (&frame_encode->output_buffer);
    gst_video_encoder_finish_frame (venc, frame_encode);
    return ret;
  }
error_push_buffer:
  GST_ERROR_OBJECT (self, "Failed to push the buffer");
  return ret;
}

static GstFlowReturn
gst_va_h264_enc_drain (GstVideoEncoder * venc)
{
  GstVaH264Enc *self = GST_VA_H264_ENC (venc);
  GstFlowReturn ret = GST_FLOW_OK;
  GstVideoCodecFrame *frame_enc = NULL;

  GST_DEBUG_OBJECT (self, "Encoder is draining");

  /* Kickout all cached frames */
  if (!_reorder_frame (venc, NULL, TRUE, &frame_enc)) {
    ret = GST_FLOW_ERROR;
    goto error_and_purge_all;
  }

  while (frame_enc) {
    if (g_queue_is_empty (&self->reorder_list))
      _enc_frame (frame_enc)->last_frame = TRUE;

    ret = _encode_frame (venc, frame_enc);
    if (ret != GST_FLOW_OK)
      goto error_and_purge_all;

    frame_enc = g_queue_pop_head (&self->output_list);
    gst_video_codec_frame_unref (frame_enc);
    ret = _push_buffer_to_downstream (self, frame_enc);
    frame_enc = NULL;
    if (ret != GST_FLOW_OK)
      goto error_and_purge_all;

    frame_enc = NULL;
    if (!_reorder_frame (venc, NULL, TRUE, &frame_enc)) {
      ret = GST_FLOW_ERROR;
      goto error_and_purge_all;
    }
  }

  g_assert (g_queue_is_empty (&self->reorder_list));

  /* Output all frames. */
  while (!g_queue_is_empty (&self->output_list)) {
    frame_enc = g_queue_pop_head (&self->output_list);
    gst_video_codec_frame_unref (frame_enc);
    ret = _push_buffer_to_downstream (self, frame_enc);
    frame_enc = NULL;
    if (ret != GST_FLOW_OK)
      goto error_and_purge_all;
  }

  /* Also clear the reference list. */
  g_queue_clear_full (&self->ref_list,
      (GDestroyNotify) gst_video_codec_frame_unref);

  return GST_FLOW_OK;

error_and_purge_all:
  if (frame_enc) {
    gst_clear_buffer (&frame_enc->output_buffer);
    gst_video_encoder_finish_frame (venc, frame_enc);
  }

  if (!g_queue_is_empty (&self->output_list)) {
    GST_WARNING_OBJECT (self, "Still %d frame in the output list"
        " after drain", g_queue_get_length (&self->output_list));
    while (!g_queue_is_empty (&self->output_list)) {
      frame_enc = g_queue_pop_head (&self->output_list);
      gst_video_codec_frame_unref (frame_enc);
      gst_clear_buffer (&frame_enc->output_buffer);
      gst_video_encoder_finish_frame (venc, frame_enc);
    }
  }

  if (!g_queue_is_empty (&self->reorder_list)) {
    GST_WARNING_OBJECT (self, "Still %d frame in the reorder list"
        " after drain", g_queue_get_length (&self->reorder_list));
    while (!g_queue_is_empty (&self->reorder_list)) {
      frame_enc = g_queue_pop_head (&self->reorder_list);
      gst_video_codec_frame_unref (frame_enc);
      gst_clear_buffer (&frame_enc->output_buffer);
      gst_video_encoder_finish_frame (venc, frame_enc);
    }
  }

  /* Also clear the reference list. */
  g_queue_clear_full (&self->ref_list,
      (GDestroyNotify) gst_video_codec_frame_unref);

  return ret;
}

static GstFlowReturn
gst_va_h264_enc_finish (GstVideoEncoder * venc)
{
  return gst_va_h264_enc_drain (venc);
}

static GstAllocator *
_allocator_from_caps (GstVaH264Enc * self, GstCaps * caps)
{
  GstAllocator *allocator = NULL;

  if (gst_caps_is_dmabuf (caps)) {
    allocator = gst_va_dmabuf_allocator_new (self->display);
  } else {
    GArray *surface_formats =
        gst_va_encoder_get_surface_formats (self->encoder);
    allocator = gst_va_allocator_new (self->display, surface_formats);
  }

  return allocator;
}

static gboolean
gst_va_h264_enc_propose_allocation (GstVideoEncoder * venc, GstQuery * query)
{
  GstVaH264Enc *self = GST_VA_H264_ENC (venc);
  GstAllocator *allocator = NULL;
  GstAllocationParams params = { 0, };
  GstBufferPool *pool;
  GstCaps *caps;
  GstVideoInfo info;
  gboolean need_pool = FALSE;
  guint size, usage_hint = 0;

  gst_query_parse_allocation (query, &caps, &need_pool);
  if (!caps)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (self, "Cannot parse caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  size = GST_VIDEO_INFO_SIZE (&info);

  gst_allocation_params_init (&params);

  if (!(allocator = _allocator_from_caps (self, caps)))
    return FALSE;

  pool = gst_va_pool_new_with_config (caps, size, 1, 0, usage_hint,
      GST_VA_FEATURE_AUTO, allocator, &params);
  if (!pool) {
    gst_object_unref (allocator);
    goto config_failed;
  }

  gst_query_add_allocation_param (query, allocator, &params);
  gst_query_add_allocation_pool (query, pool, size, 0, 0);

  GST_DEBUG_OBJECT (self,
      "proposing %" GST_PTR_FORMAT " with allocator %" GST_PTR_FORMAT,
      pool, allocator);

  gst_object_unref (allocator);
  gst_object_unref (pool);

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return TRUE;

  /* ERRORS */
config_failed:
  {
    GST_ERROR_OBJECT (self, "failed to set config");
    return FALSE;
  }
}

static void
gst_va_h264_enc_set_context (GstElement * element, GstContext * context)
{
  GstVaDisplay *old_display, *new_display;
  GstVaH264Enc *self = GST_VA_H264_ENC (element);
  GstVaH264EncClass *klass = GST_VA_H264_ENC_GET_CLASS (self);
  gboolean ret;

  old_display = self->display ? gst_object_ref (self->display) : NULL;

  ret = gst_va_handle_set_context (element, context, klass->render_device_path,
      &self->display);

  new_display = self->display ? gst_object_ref (self->display) : NULL;

  if (!ret || (old_display && new_display && old_display != new_display
          && self->encoder)) {
    GST_ELEMENT_WARNING (element, RESOURCE, BUSY,
        ("Can't replace VA display while operating"), (NULL));
  }

  gst_clear_object (&old_display);
  gst_clear_object (&new_display);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_va_h264_enc_set_format (GstVideoEncoder * venc, GstVideoCodecState * state)
{
  GstVaH264Enc *self = GST_VA_H264_ENC (venc);
  GstVaH264EncClass *klass = GST_VA_H264_ENC_GET_CLASS (self);
  GstCaps *out_caps;
  guint max_ref_frames;

  g_return_val_if_fail (state->caps != NULL, FALSE);

  if (self->input_state)
    gst_video_codec_state_unref (self->input_state);
  self->input_state = gst_video_codec_state_ref (state);

  gst_caps_replace (&self->in_caps, state->caps);

  if (!gst_video_info_from_caps (&self->in_info, self->in_caps))
    return FALSE;

  if (gst_va_h264_enc_drain (venc) != GST_FLOW_OK)
    return FALSE;

  if (!gst_va_encoder_close (self->encoder)) {
    GST_ERROR_OBJECT (self, "Failed to close the VA encoder");
    return FALSE;
  }

  g_assert (klass->reconfig);
  if (!klass->reconfig (self)) {
    GST_ERROR_OBJECT (self, "Reconfig the encoder error");
    return FALSE;
  }

  max_ref_frames = self->gop.num_ref_frames + 3 /* scratch frames */ ;
  if (!gst_va_encoder_open (self->encoder, self->profile, self->entrypoint,
          GST_VIDEO_INFO_FORMAT (&self->in_info), self->rt_format,
          self->mb_width * 16, self->mb_height * 16, self->codedbuf_size,
          max_ref_frames, self->rc.rc_ctrl_mode, self->packed_headers)) {
    GST_ERROR_OBJECT (self, "Failed to open the VA encoder.");
    return FALSE;
  }

  /* Add some tags */
  {
    GstTagList *tags = gst_tag_list_new_empty ();
    const gchar *encoder_name;
    guint bitrate = 0;

    g_object_get (venc, "bitrate", &bitrate, NULL);
    if (bitrate > 0)
      gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE, GST_TAG_NOMINAL_BITRATE,
          bitrate, NULL);

    if ((encoder_name =
            gst_element_class_get_metadata (GST_ELEMENT_GET_CLASS (venc),
                GST_ELEMENT_METADATA_LONGNAME)))
      gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE, GST_TAG_ENCODER,
          encoder_name, NULL);

    gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE, GST_TAG_CODEC, "H264", NULL);

    gst_video_encoder_merge_tags (venc, tags, GST_TAG_MERGE_REPLACE);
    gst_tag_list_unref (tags);
  }

  out_caps = gst_va_profile_caps (self->profile);
  g_assert (out_caps);
  out_caps = gst_caps_fixate (out_caps);

  if (self->level_str)
    gst_caps_set_simple (out_caps, "level", G_TYPE_STRING, self->level_str,
        NULL);

  gst_caps_set_simple (out_caps, "width", G_TYPE_INT, self->width,
      "height", G_TYPE_INT, self->height, "alignment", G_TYPE_STRING, "au",
      "stream-format", G_TYPE_STRING, "byte-stream", NULL);

  GST_DEBUG_OBJECT (self, "output caps is %" GST_PTR_FORMAT, out_caps);

  if (self->output_state)
    gst_video_codec_state_unref (self->output_state);
  self->output_state = gst_video_encoder_set_output_state (venc, out_caps,
      self->input_state);

  if (!gst_video_encoder_negotiate (venc)) {
    GST_ERROR_OBJECT (self, "Failed to negotiate with the downstream");
    return FALSE;
  }

  return TRUE;
}

static gboolean
_query_context (GstVaH264Enc * self, GstQuery * query)
{
  GstVaDisplay *display = NULL;
  gboolean ret;

  gst_object_replace ((GstObject **) & display, (GstObject *) self->display);
  ret = gst_va_handle_context_query (GST_ELEMENT_CAST (self), query, display);
  gst_clear_object (&display);

  return ret;
}

static gboolean
gst_va_h264_enc_src_query (GstVideoEncoder * venc, GstQuery * query)
{
  GstVaH264Enc *self = GST_VA_H264_ENC (venc);
  gboolean ret = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:{
      ret = _query_context (self, query);
      break;
    }
    case GST_QUERY_CAPS:{
      GstCaps *caps = NULL, *tmp, *filter = NULL;
      GstVaEncoder *va_encoder = NULL;
      gboolean fixed_caps;

      gst_object_replace ((GstObject **) & va_encoder,
          (GstObject *) self->encoder);

      gst_query_parse_caps (query, &filter);

      fixed_caps = GST_PAD_IS_FIXED_CAPS (GST_VIDEO_ENCODER_SRC_PAD (venc));

      if (!fixed_caps && va_encoder)
        caps = gst_va_encoder_get_srcpad_caps (va_encoder);

      gst_clear_object (&va_encoder);

      if (caps) {
        if (filter) {
          tmp = gst_caps_intersect_full (filter, caps,
              GST_CAPS_INTERSECT_FIRST);
          gst_caps_unref (caps);
          caps = tmp;
        }

        GST_LOG_OBJECT (self, "Returning caps %" GST_PTR_FORMAT, caps);
        gst_query_set_caps_result (query, caps);
        gst_caps_unref (caps);
        ret = TRUE;
        break;
      }
      /* else jump to default */
    }
    default:
      ret = GST_VIDEO_ENCODER_CLASS (parent_class)->src_query (venc, query);
      break;
  }

  return ret;
}

static gboolean
gst_va_h264_enc_sink_query (GstVideoEncoder * venc, GstQuery * query)
{
  GstVaH264Enc *self = GST_VA_H264_ENC (venc);

  if (GST_QUERY_TYPE (query) == GST_QUERY_CONTEXT)
    return _query_context (self, query);

  return GST_VIDEO_ENCODER_CLASS (parent_class)->sink_query (venc, query);
}

/* *INDENT-OFF* */
static const gchar *sink_caps_str =
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_VA,
        "{ NV12 }") " ;"
    GST_VIDEO_CAPS_MAKE ("{ NV12 }");
/* *INDENT-ON* */

static const gchar *src_caps_str = "video/x-h264";

static gpointer
_register_debug_category (gpointer data)
{
  GST_DEBUG_CATEGORY_INIT (gst_va_h264enc_debug, "vah264enc", 0,
      "VA h264 encoder");

  return NULL;
}

static void
gst_va_h264_enc_init (GTypeInstance * instance, gpointer g_class)
{
  GstVaH264Enc *self = GST_VA_H264_ENC (instance);

  g_queue_init (&self->reorder_list);
  g_queue_init (&self->ref_list);
  g_queue_init (&self->output_list);

  /* default values */
  self->prop.key_int_max = 0;
  self->prop.num_bframes = 0;
  self->prop.num_iframes = 0;
  self->prop.num_ref_frames = 3;
  self->prop.b_pyramid = FALSE;
  self->prop.num_slices = 1;
  self->prop.min_qp = 1;
  self->prop.max_qp = 51;
  self->prop.qp_i = 26;
  self->prop.qp_p = 26;
  self->prop.qp_b = 26;
  self->prop.use_dct8x8 = TRUE;
  self->prop.use_cabac = TRUE;
  self->prop.use_trellis = FALSE;
  self->prop.aud = FALSE;
  self->prop.mbbrc = 0;
  self->prop.bitrate = 0;
  self->prop.target_percentage = 66;
  self->prop.target_usage = 4;
  self->prop.rc_ctrl = VA_RC_CBR;
  self->prop.cpb_size = 0;
}

static void
gst_va_h264_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVaH264Enc *const self = GST_VA_H264_ENC (object);

  if (self->encoder && gst_va_encoder_is_open (self->encoder)) {
    GST_ERROR_OBJECT (object,
        "failed to set any property after encoding started");
    return;
  }

  GST_OBJECT_LOCK (self);

  switch (prop_id) {
    case PROP_KEY_INT_MAX:
      self->prop.key_int_max = g_value_get_uint (value);
      break;
    case PROP_BFRAMES:
      self->prop.num_bframes = g_value_get_uint (value);
      break;
    case PROP_IFRAMES:
      self->prop.num_iframes = g_value_get_uint (value);
      break;
    case PROP_NUM_REF_FRAMES:
      self->prop.num_ref_frames = g_value_get_uint (value);
      break;
    case PROP_B_PYRAMID:
      self->prop.b_pyramid = g_value_get_boolean (value);
      break;
    case PROP_NUM_SLICES:
      self->prop.num_slices = g_value_get_uint (value);
      break;
    case PROP_MIN_QP:
      self->prop.min_qp = g_value_get_uint (value);
      break;
    case PROP_MAX_QP:
      self->prop.max_qp = g_value_get_uint (value);
      break;
    case PROP_QP_I:
      self->prop.qp_i = g_value_get_uint (value);
      break;
    case PROP_QP_P:
      self->prop.qp_p = g_value_get_uint (value);
      break;
    case PROP_QP_B:
      self->prop.qp_b = g_value_get_uint (value);
      break;
    case PROP_DCT8X8:
      self->prop.use_dct8x8 = g_value_get_boolean (value);
      break;
    case PROP_CABAC:
      self->prop.use_cabac = g_value_get_boolean (value);
      break;
    case PROP_TRELLIS:
      self->prop.use_trellis = g_value_get_boolean (value);
      break;
    case PROP_AUD:
      self->prop.aud = g_value_get_boolean (value);
      break;
    case PROP_MBBRC:{
      /* Macroblock-level rate control.
       * 0: use default,
       * 1: always enable,
       * 2: always disable,
       * other: reserved. */
      switch (g_value_get_enum (value)) {
        case GST_VA_FEATURE_DISABLED:
          self->prop.mbbrc = 2;
          break;
        case GST_VA_FEATURE_ENABLED:
          self->prop.mbbrc = 1;
          break;
        case GST_VA_FEATURE_AUTO:
          self->prop.mbbrc = 0;
          break;
      }
      break;
    }
    case PROP_BITRATE:
      self->prop.bitrate = g_value_get_uint (value);
      break;
    case PROP_TARGET_PERCENTAGE:
      self->prop.target_percentage = g_value_get_uint (value);
      break;
    case PROP_TARGET_USAGE:
      self->prop.target_usage = g_value_get_uint (value);
      break;
    case PROP_RATE_CONTROL:
      self->prop.rc_ctrl = g_value_get_enum (value);
      break;
    case PROP_CPB_SIZE:
      self->prop.cpb_size = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }

  GST_OBJECT_UNLOCK (self);
}

static void
gst_va_h264_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVaH264Enc *const self = GST_VA_H264_ENC (object);

  GST_OBJECT_LOCK (self);

  switch (prop_id) {
    case PROP_KEY_INT_MAX:
      g_value_set_uint (value, self->prop.key_int_max);
      break;
    case PROP_BFRAMES:
      g_value_set_uint (value, self->prop.num_bframes);
      break;
    case PROP_IFRAMES:
      g_value_set_uint (value, self->prop.num_iframes);
      break;
    case PROP_NUM_REF_FRAMES:
      g_value_set_uint (value, self->prop.num_ref_frames);
      break;
    case PROP_B_PYRAMID:
      g_value_set_boolean (value, self->prop.b_pyramid);
      break;
    case PROP_NUM_SLICES:
      g_value_set_uint (value, self->prop.num_slices);
      break;
    case PROP_MIN_QP:
      g_value_set_uint (value, self->prop.min_qp);
      break;
    case PROP_MAX_QP:
      g_value_set_uint (value, self->prop.max_qp);
      break;
    case PROP_QP_I:
      g_value_set_uint (value, self->prop.qp_i);
      break;
    case PROP_QP_P:
      g_value_set_uint (value, self->prop.qp_p);
      break;
    case PROP_QP_B:
      g_value_set_uint (value, self->prop.qp_b);
      break;
    case PROP_DCT8X8:
      g_value_set_boolean (value, self->prop.use_dct8x8);
      break;
    case PROP_CABAC:
      g_value_set_boolean (value, self->prop.use_cabac);
      break;
    case PROP_TRELLIS:
      g_value_set_boolean (value, self->prop.use_trellis);
      break;
    case PROP_AUD:
      g_value_set_boolean (value, self->prop.aud);
      break;
    case PROP_MBBRC:
      g_value_set_enum (value, self->prop.mbbrc);
      break;
    case PROP_BITRATE:
      g_value_set_uint (value, self->prop.bitrate);
      break;
    case PROP_TARGET_PERCENTAGE:
      g_value_set_uint (value, self->prop.target_percentage);
      break;
    case PROP_TARGET_USAGE:
      g_value_set_uint (value, self->prop.target_usage);
      break;
    case PROP_RATE_CONTROL:
      g_value_set_enum (value, self->prop.rc_ctrl);
      break;
    case PROP_CPB_SIZE:
      g_value_set_uint (value, self->prop.cpb_size);
      break;
    case PROP_DEVICE_PATH:{
      if (!(self->display && GST_IS_VA_DISPLAY_DRM (self->display))) {
        g_value_set_string (value, NULL);
      } else {
        g_object_get_property (G_OBJECT (self->display), "path", value);
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }

  GST_OBJECT_UNLOCK (self);
}

struct CData
{
  gchar *render_device_path;
  gchar *description;
  GstCaps *sink_caps;
  GstCaps *src_caps;
};

static void
gst_va_h264_enc_class_init (gpointer g_klass, gpointer class_data)
{
  GstCaps *src_doc_caps, *sink_doc_caps;
  GObjectClass *const object_class = G_OBJECT_CLASS (g_klass);
  GstElementClass *const element_class = GST_ELEMENT_CLASS (g_klass);
  GstVideoEncoderClass *const venc_class = GST_VIDEO_ENCODER_CLASS (g_klass);
  GstVaH264EncClass *const klass = GST_VA_H264_ENC_CLASS (g_klass);
  GstPadTemplate *sink_pad_templ, *src_pad_templ;
  struct CData *cdata = class_data;
  gchar *long_name;

  parent_class = g_type_class_peek_parent (g_klass);

  klass->render_device_path = g_strdup (cdata->render_device_path);
  klass->codec = H264;

  if (cdata->description) {
    long_name = g_strdup_printf ("VA-API H.264 Encoder in %s",
        cdata->description);
  } else {
    long_name = g_strdup ("VA-API H.264 Encoder");
  }

  gst_element_class_set_metadata (element_class, long_name,
      "Codec/Encoder/Video/Hardware", "VA-API based H.264 video encoder",
      "He Junyan <junyan.he@intel.com>");

  sink_doc_caps = gst_caps_from_string (sink_caps_str);
  src_doc_caps = gst_caps_from_string (src_caps_str);

  sink_pad_templ = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      cdata->sink_caps);
  gst_element_class_add_pad_template (element_class, sink_pad_templ);

  gst_pad_template_set_documentation_caps (sink_pad_templ, sink_doc_caps);
  gst_caps_unref (sink_doc_caps);

  src_pad_templ = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      cdata->src_caps);
  gst_element_class_add_pad_template (element_class, src_pad_templ);

  gst_pad_template_set_documentation_caps (src_pad_templ, src_doc_caps);
  gst_caps_unref (src_doc_caps);

  object_class->set_property = gst_va_h264_enc_set_property;
  object_class->get_property = gst_va_h264_enc_get_property;

  element_class->set_context = GST_DEBUG_FUNCPTR (gst_va_h264_enc_set_context);
  venc_class->open = GST_DEBUG_FUNCPTR (gst_va_h264_enc_open);
  venc_class->start = GST_DEBUG_FUNCPTR (gst_va_h264_enc_start);
  venc_class->close = GST_DEBUG_FUNCPTR (gst_va_h264_enc_close);
  venc_class->stop = GST_DEBUG_FUNCPTR (gst_va_h264_enc_stop);
  venc_class->handle_frame = GST_DEBUG_FUNCPTR (gst_va_h264_enc_handle_frame);
  venc_class->finish = GST_DEBUG_FUNCPTR (gst_va_h264_enc_finish);
  venc_class->flush = GST_DEBUG_FUNCPTR (gst_va_h264_enc_flush);
  venc_class->set_format = GST_DEBUG_FUNCPTR (gst_va_h264_enc_set_format);
  venc_class->getcaps = GST_DEBUG_FUNCPTR (gst_va_h264_enc_get_caps);
  venc_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_va_h264_enc_propose_allocation);
  venc_class->src_query = GST_DEBUG_FUNCPTR (gst_va_h264_enc_src_query);
  venc_class->sink_query = GST_DEBUG_FUNCPTR (gst_va_h264_enc_sink_query);

  klass->reconfig = GST_DEBUG_FUNCPTR (gst_va_h264_enc_reconfig);
  klass->push_frame = GST_DEBUG_FUNCPTR (gst_va_h264_enc_push_frame);
  klass->pop_frame = GST_DEBUG_FUNCPTR (gst_va_h264_enc_pop_frame);
  klass->encode_frame = GST_DEBUG_FUNCPTR (gst_va_h264_enc_encode_frame);

  g_free (long_name);
  g_free (cdata->description);
  g_free (cdata->render_device_path);
  gst_caps_unref (cdata->src_caps);
  gst_caps_unref (cdata->sink_caps);
  g_free (cdata);

  /**
   * GstVaEncoder:key-int-max:
   *
   * The maximal distance between two keyframes.
   */
  properties[PROP_KEY_INT_MAX] = g_param_spec_uint ("key-int-max",
      "Key frame maximal interval",
      "The maximal distance between two keyframes. It decides the size of GOP"
      " (0: auto-calculate)", 0, MAX_GOP_SIZE, 0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  /**
   * GstVaH264Enc:b-frames:
   *
   * Number of B-frames between two reference frames.
   */
  properties[PROP_BFRAMES] = g_param_spec_uint ("b-frames", "B Frames",
      "Number of B frames between I and P reference frames", 0, 31, 0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  /**
   * GstVaH264Enc:i-frames:
   *
   * Force the number of i-frames insertion within one GOP.
   */
  properties[PROP_IFRAMES] = g_param_spec_uint ("i-frames", "I Frames",
      "Force the number of I frames insertion within one GOP, not including the "
      "first IDR frame", 0, 1023, 0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  /**
   * GstVaH264Enc:ref-frames:
   *
   * The number of reference frames.
   */
  properties[PROP_NUM_REF_FRAMES] = g_param_spec_uint ("ref-frames",
      "Number of Reference Frames",
      "Number of reference frames, including both the forward and the backward",
      0, 16, 3, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  /**
   * GstVaH264Enc:b-pyramid:
   *
   * Enable the b-pyramid reference structure in GOP.
   */
  properties[PROP_B_PYRAMID] = g_param_spec_boolean ("b-pyramid", "b pyramid",
      "Enable the b-pyramid reference structure in the GOP", FALSE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);
  /**
   * GstVaH264Enc:num-slices:
   *
   * The number of slices per frame.
   */
  properties[PROP_NUM_SLICES] = g_param_spec_uint ("num-slices",
      "Number of Slices", "Number of slices per frame", 1, 200, 1,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  /**
   * GstVaH264Enc:max-qp:
   *
   * The maximum quantizer value.
   */
  properties[PROP_MAX_QP] = g_param_spec_uint ("max-qp", "Maximum QP",
      "Maximum quantizer value for each frame", 0, 51, 51,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  /**
   * GstVaH264Enc:min-qp:
   *
   * The minimum quantizer value.
   */
  properties[PROP_MIN_QP] = g_param_spec_uint ("min-qp", "Minimum QP",
      "Minimum quantizer value for each frame", 0, 51, 1,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  /**
   * GstVaH264Enc:qpi:
   *
   * The quantizer value for I frame. In CQP mode, it specifies the QP of
   * I frame, in other mode, it specifies the init QP of all frames.
   */
  properties[PROP_QP_I] = g_param_spec_uint ("qpi", "I Frame QP",
      "The quantizer value for I frame. In CQP mode, it specifies the QP of I "
      "frame, in other mode, it specifies the init QP of all frames", 0, 51, 26,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  /**
   * GstVaH264Enc:qpp:
   *
   * The quantizer value for P frame. This is available only in CQP mode.
   */
  properties[PROP_QP_P] = g_param_spec_uint ("qpp",
      "The quantizer value for P frame",
      "The quantizer value for P frame. This is available only in CQP mode",
      0, 51, 26,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  /**
   * GstVaH264Enc:qpb:
   *
   * The quantizer value for B frame. This is available only in CQP mode.
   */
  properties[PROP_QP_B] = g_param_spec_uint ("qpb",
      "The quantizer value for B frame",
      "The quantizer value for B frame. This is available only in CQP mode",
      0, 51, 26,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  /**
   * GstVaH264Enc:dct8x8:
   *
   * Enable adaptive use of 8x8 transforms in I-frames. This improves
   * the compression ratio but requires high profile at least.
   */
  properties[PROP_DCT8X8] = g_param_spec_boolean ("dct8x8",
      "Enable 8x8 DCT",
      "Enable adaptive use of 8x8 transforms in I-frames", TRUE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  /**
   * GstVaH264Enc:cabac:
   *
   * It enables CABAC entropy coding mode to improve compression ratio,
   * but requires main profile at least.
   */
  properties[PROP_CABAC] = g_param_spec_boolean ("cabac", "Enable CABAC",
      "Enable CABAC entropy coding mode", TRUE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  /**
   * GstVaH264Enc:trellis:
   *
   * It enable the trellis quantization method.
   * Trellis is an improved quantization algorithm.
   */
  properties[PROP_TRELLIS] = g_param_spec_boolean ("trellis", "Enable trellis",
      "Enable the trellis quantization method", FALSE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  /**
   * GstVaH264Enc:aud:
   *
   * Insert the AU (Access Unit) delimeter for each frame.
   */
  properties[PROP_AUD] = g_param_spec_boolean ("aud", "Insert AUD",
      "Insert AU (Access Unit) delimeter for each frame", FALSE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  /**
   * GstVaH264Enc:mbbrc:
   *
   * Macroblock level bitrate control.
   * This is not compatible with Constant QP rate control.
   */
  properties[PROP_MBBRC] = g_param_spec_enum ("mbbrc",
      "Macroblock level Bitrate Control",
      "Macroblock level Bitrate Control. It is not compatible with CQP",
      GST_TYPE_VA_FEATURE, GST_VA_FEATURE_AUTO,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  /**
   * GstVaH264Enc:bitrate:
   *
   * The desired target bitrate, expressed in kbps.
   * This is not available in CQP mode.
   *
   * CBR: This applies equally to the minimum, maximum and target bitrate.
   * VBR: This applies to the target bitrate. The driver will use the
   * "target-percentage" together to calculate the minimum and maximum bitrate.
   * VCM: This applies to the target bitrate. The minimum and maximum bitrate
   * are not needed.
   */
  properties[PROP_BITRATE] = g_param_spec_uint ("bitrate", "Bitrate (kbps)",
      "The desired bitrate expressed in kbps (0: auto-calculate)",
      0, 2000 * 1024, 0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  /**
   * GstVaH264Enc:target-percentage:
   *
   * The target percentage of the max bitrate, and expressed in uint,
   * equal to "target percentage"*100.
   * "target percentage" = "target bitrate" * 100 / "max bitrate"
   * This is available only when rate-control is VBR.
   * The driver uses it to calculate the minimum and maximum bitrate.
   */
  properties[PROP_TARGET_PERCENTAGE] = g_param_spec_uint ("target-percentage",
      "target bitrate percentage",
      "The percentage for 'target bitrate'/'maximum bitrate' (Only in VBR)",
      50, 100, 66,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  /**
   * GstVaH264Enc:target-usage:
   *
   * The target usage of the encoder. It controls and balances the encoding
   * speed and the encoding quality. The lower value has better quality but
   * slower speed, the higher value has faster speed but lower quality.
   */
  properties[PROP_TARGET_USAGE] = g_param_spec_uint ("target-usage",
      "target usage",
      "The target usage to control and balance the encoding speed/quality",
      1, 7, 4, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  /**
   * GstVaH264Enc:cpb-size:
   *
   * The desired max CPB size in Kb (0: auto-calculate).
   */
  properties[PROP_CPB_SIZE] = g_param_spec_uint ("cpb-size",
      "max CPB size in Kb",
      "The desired max CPB size in Kb (0: auto-calculate)", 0, 2000 * 1024, 0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  /**
   * GstVaH264Enc:rate-control:
   *
   * The desired rate control mode for the encoder.
   */
  properties[PROP_RATE_CONTROL] = g_param_spec_enum ("rate-control",
      "rate control mode", "The desired rate control mode for the encoder",
      GST_TYPE_VA_ENCODER_RATE_CONTROL, VA_RC_CBR,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  properties[PROP_DEVICE_PATH] = g_param_spec_string ("device-path",
      "Device Path", "DRM device path", NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPERTIES, properties);

  gst_type_mark_as_plugin_api (gst_va_encoder_rate_control_get_type (), 0);

  /**
   * GstVaFeature:
   * @GST_VA_FEATURE_DISABLED: The feature is disabled.
   * @GST_VA_FEATURE_ENABLED: The feature is enabled.
   * @GST_VA_FEATURE_AUTO: The feature is enabled automatically.
   *
   * Since: 1.22
   */
  gst_type_mark_as_plugin_api (GST_TYPE_VA_FEATURE, 0);
}

static GstCaps *
_complete_src_caps (GstCaps * srccaps)
{
  GstCaps *caps = gst_caps_copy (srccaps);
  GValue val = G_VALUE_INIT;

  g_value_init (&val, G_TYPE_STRING);
  g_value_set_string (&val, "au");
  gst_caps_set_value (caps, "alignment", &val);
  g_value_unset (&val);

  g_value_init (&val, G_TYPE_STRING);
  g_value_set_string (&val, "byte-stream");
  gst_caps_set_value (caps, "stream-format", &val);
  g_value_unset (&val);

  return caps;
}

gboolean
gst_va_h264_enc_register (GstPlugin * plugin, GstVaDevice * device,
    GstCaps * sink_caps, GstCaps * src_caps, guint rank)
{
  static GOnce debug_once = G_ONCE_INIT;
  GType type;
  GTypeInfo type_info = {
    .class_size = sizeof (GstVaH264EncClass),
    .class_init = gst_va_h264_enc_class_init,
    .instance_size = sizeof (GstVaH264Enc),
    .instance_init = gst_va_h264_enc_init,
  };
  struct CData *cdata;
  gboolean ret;
  gchar *type_name, *feature_name;

  g_return_val_if_fail (GST_IS_PLUGIN (plugin), FALSE);
  g_return_val_if_fail (GST_IS_VA_DEVICE (device), FALSE);
  g_return_val_if_fail (GST_IS_CAPS (sink_caps), FALSE);
  g_return_val_if_fail (GST_IS_CAPS (src_caps), FALSE);

  cdata = g_new (struct CData, 1);
  cdata->description = NULL;
  cdata->render_device_path = g_strdup (device->render_device_path);
  cdata->sink_caps = gst_caps_ref (sink_caps);
  cdata->src_caps = _complete_src_caps (src_caps);

  /* class data will be leaked if the element never gets instantiated */
  GST_MINI_OBJECT_FLAG_SET (cdata->sink_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
  GST_MINI_OBJECT_FLAG_SET (cdata->src_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);

  type_info.class_data = cdata;
  type_name = g_strdup ("GstVaH264Enc");
  feature_name = g_strdup ("vah264enc");

  /* The first encoder to be registered should use a constant name,
   * like vah264enc, for any additional encoders, we create unique
   * names, using inserting the render device name. */
  if (g_type_from_name (type_name)) {
    gchar *basename = g_path_get_basename (device->render_device_path);
    g_free (type_name);
    g_free (feature_name);
    type_name = g_strdup_printf ("GstVa%sH264Enc", basename);
    feature_name = g_strdup_printf ("va%sh264enc", basename);
    cdata->description = basename;
    /* lower rank for non-first device */
    if (rank > 0)
      rank--;
  }

  g_once (&debug_once, _register_debug_category, NULL);
  type = g_type_register_static (GST_TYPE_VIDEO_ENCODER,
      type_name, &type_info, 0);
  ret = gst_element_register (plugin, feature_name, rank, type);

  g_free (type_name);
  g_free (feature_name);

  return ret;
}
