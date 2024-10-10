/* GStreamer
 *  Copyright (C) 2024 Centricular Ltd
 *     Author: Jochen Henneberg <jochen@centricular.com>
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
 * SECTION:element-vavp8enc
 * @title: vavp8enc
 * @short_description: A VA-API based VP8 video encoder
 *
 * vavp8enc encodes raw video VA surfaces into VP8 bitstreams using
 * the installed and chosen [VA-API](https://01.org/linuxmedia/vaapi)
 * driver.
 *
 * The raw video frames in main memory can be imported into VA surfaces.
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 videotestsrc num-buffers=60 ! timeoverlay ! vavp8enc ! mp4mux ! filesink location=test.mp4
 * ```
 *
 * Since: 1.26
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvavp8enc.h"

#include <math.h>
#include <gst/codecparsers/gstvp8parser.h>

#include "gstvabaseenc.h"
#include "gstvapluginutils.h"

GST_DEBUG_CATEGORY_STATIC (gst_va_vp8enc_debug);
#define GST_CAT_DEFAULT gst_va_vp8enc_debug

#define GST_VA_VP8_ENC(obj)            ((GstVaVp8Enc *) obj)
#define GST_VA_VP8_ENC_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), G_TYPE_FROM_INSTANCE (obj), GstVaVp8EncClass))
#define GST_VA_VP8_ENC_CLASS(klass)    ((GstVaVp8EncClass *) klass)

typedef struct _GstVaVp8Enc GstVaVp8Enc;
typedef struct _GstVaVp8EncClass GstVaVp8EncClass;
typedef struct _GstVaVp8EncFrame GstVaVp8EncFrame;
typedef struct _GstVaVp8GFGroup GstVaVp8GFGroup;

enum
{
  PROP_KEYFRAME_INT = 1,
  PROP_BITRATE,
  PROP_TARGET_PERCENTAGE,
  PROP_TARGET_USAGE,
  PROP_CPB_SIZE,
  PROP_MBBRC,
  PROP_QP,
  PROP_MIN_QP,
  PROP_MAX_QP,
  PROP_LOOP_FILTER_LEVEL,
  PROP_SHARPNESS_LEVEL,
  PROP_RATE_CONTROL,
  N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES];

static GstObjectClass *parent_class = NULL;

#define DEFAULT_BASE_QINDEX        60
#define DEFAULT_TARGET_PERCENTAGE  66
#define DEFAULT_LOOP_FILTER_LEVEL  10

#define MAX_FRAME_WIDTH         4096
#define MAX_FRAME_HEIGHT        4096
#define MAX_KEY_FRAME_INTERVAL  1024

#define FRAME_TYPE_INVALID -1
#define FRAME_NUM_INVALID  -1

struct _GstVaVp8EncFrame
{
  GstVaEncFrame base;
  GstVp8FrameType type;
  gint frame_num;
};

struct _GstVaVp8EncClass
{
  GstVaBaseEncClass parent_class;

  GType rate_control_type;
  char rate_control_type_name[64];
  GEnumValue rate_control[16];
};

struct _GstVaVp8Enc
{
  /*< private > */
  GstVaBaseEnc parent;

  /* properties */
  struct
  {
    /* kbps */
    guint bitrate;
    /* VA_RC_XXX */
    guint32 rc_ctrl;
    guint32 cpb_size;
    guint32 target_percentage;
    guint32 target_usage;
    guint keyframe_interval;
    guint32 qp;
    guint32 min_qp;
    guint32 max_qp;
    guint32 mbbrc;
    gint32 filter_level;
    guint32 sharpness_level;
  } prop;

  struct
  {
    guint keyframe_interval;
    gint frame_num;
    /* Only one reference frame is kept here thought VP8 has support
       for golden and alternate reference frames. This is for
       simplicity and because the decision for golden/altref frames
       without manual interaction with the codec and knowledge of the
       frame content does not seem meaningful. */
    GstVideoCodecFrame *last_ref;
  } gop;

  struct
  {
    guint target_usage;
    guint32 target_percentage;
    guint32 cpb_size;
    guint32 cpb_length_bits;
    guint32 rc_ctrl_mode;
    guint max_bitrate;
    guint max_bitrate_bits;
    guint target_bitrate;
    guint target_bitrate_bits;
    guint32 base_qindex;
    guint32 min_qindex;
    guint32 max_qindex;
    guint32 mbbrc;
    gint32 filter_level;
    guint32 sharpness_level;
  } rc;
};

static GstVaVp8EncFrame *
gst_va_vp8_enc_frame_new (void)
{
  GstVaVp8EncFrame *frame;

  frame = g_new (GstVaVp8EncFrame, 1);
  frame->type = FRAME_TYPE_INVALID;
  frame->base.picture = NULL;
  frame->frame_num = FRAME_NUM_INVALID;

  return frame;
}

static void
gst_va_vp8_enc_frame_free (gpointer pframe)
{
  GstVaVp8EncFrame *frame = pframe;

  g_clear_pointer (&frame->base.picture, gst_va_encode_picture_free);
  g_free (frame);
}

static gboolean
gst_va_vp8_enc_new_frame (GstVaBaseEnc * base, GstVideoCodecFrame * frame)
{
  GstVaVp8EncFrame *frame_in;

  frame_in = gst_va_vp8_enc_frame_new ();
  gst_va_set_enc_frame (frame, (GstVaEncFrame *) frame_in,
      gst_va_vp8_enc_frame_free);

  return TRUE;
}

static inline GstVaVp8EncFrame *
_enc_frame (GstVideoCodecFrame * frame)
{
  GstVaVp8EncFrame *enc_frame = gst_video_codec_frame_get_user_data (frame);

  g_assert (enc_frame);

  return enc_frame;
}

static void
_update_ref_frame (GstVaVp8Enc * self, GstVideoCodecFrame ** ref_frame,
    GstVideoCodecFrame * frame)
{
  if (*ref_frame)
    gst_video_codec_frame_unref (*ref_frame);

  if (frame) {
    *ref_frame = gst_video_codec_frame_ref (frame);
  } else {
    *ref_frame = NULL;
  }
}

static gboolean
gst_va_vp8_enc_reorder_frame (GstVaBaseEnc * base, GstVideoCodecFrame * frame,
    gboolean bump_all, GstVideoCodecFrame ** out_frame)
{
  GstVaVp8Enc *self = GST_VA_VP8_ENC (base);
  GstVaVp8EncFrame *va_frame;

  if (bump_all) {
    g_return_val_if_fail (frame == NULL, FALSE);
    _update_ref_frame (self, &self->gop.last_ref, NULL);
    self->gop.frame_num = FRAME_NUM_INVALID;
    goto out;
  }

  /* No reorder - if there is no new frame there will be no new output
     frame. */
  if (frame == NULL)
    goto out;

  va_frame = _enc_frame (frame);
  self->gop.frame_num++;

  if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (frame))
    self->gop.frame_num = 0;

  if (self->gop.frame_num == self->gop.keyframe_interval)
    self->gop.frame_num = 0;

  if (self->gop.frame_num == 0) {
    va_frame->type = GST_VP8_KEY_FRAME;
    _update_ref_frame (self, &self->gop.last_ref, NULL);
  } else {
    va_frame->type = GST_VP8_INTER_FRAME;
  }

  va_frame->frame_num = self->gop.frame_num;
  *out_frame = frame;

  GST_LOG_OBJECT (self, "pop frame: system_frame_number %d,"
      " frame_num: %d, frame_type %s", (*out_frame)->system_frame_number,
      va_frame->frame_num, va_frame->type ? "Inter" : "Intra");

out:
  return TRUE;
}

static void
gst_va_vp8_enc_reset_state (GstVaBaseEnc * base)
{
  GstVaVp8Enc *self = GST_VA_VP8_ENC (base);

  GST_VA_BASE_ENC_CLASS (parent_class)->reset_state (base);

  GST_OBJECT_LOCK (self);
  self->rc.rc_ctrl_mode = self->prop.rc_ctrl;
  self->rc.target_usage = self->prop.target_usage;
  self->rc.base_qindex = self->prop.qp;
  self->rc.min_qindex = self->prop.min_qp;
  self->rc.max_qindex = self->prop.max_qp;
  self->rc.target_percentage = self->prop.target_percentage;
  self->rc.cpb_size = self->prop.cpb_size;
  self->rc.mbbrc = self->prop.mbbrc;
  self->rc.filter_level = self->prop.filter_level;
  self->rc.sharpness_level = self->prop.sharpness_level;

  self->gop.keyframe_interval = self->prop.keyframe_interval;
  self->gop.frame_num = FRAME_NUM_INVALID;
  GST_OBJECT_UNLOCK (self);

  self->rc.max_bitrate = 0;
  self->rc.target_bitrate = 0;
  self->rc.max_bitrate_bits = 0;
  self->rc.cpb_length_bits = 0;
}

#define update_property(type, obj, old_val, new_val, prop_id)           \
  gst_va_base_enc_update_property_##type (obj, old_val, new_val, properties[prop_id])
#define update_property_uint(obj, old_val, new_val, prop_id)    \
  update_property (uint, obj, old_val, new_val, prop_id)

static gboolean
_vp8_generate_gop_structure (GstVaVp8Enc * self)
{
  GstVaBaseEnc *base = GST_VA_BASE_ENC (self);

  /* If not set, generate a key frame every 2 seconds. */
  if (self->gop.keyframe_interval == 0) {
    self->gop.keyframe_interval = (2 * GST_VIDEO_INFO_FPS_N (&base->in_info)
        + GST_VIDEO_INFO_FPS_D (&base->in_info) - 1) /
        GST_VIDEO_INFO_FPS_D (&base->in_info);
  }

  if (self->gop.keyframe_interval > MAX_KEY_FRAME_INTERVAL)
    self->gop.keyframe_interval = MAX_KEY_FRAME_INTERVAL;

  update_property_uint (base, &self->prop.keyframe_interval,
      self->gop.keyframe_interval, PROP_KEYFRAME_INT);

  return TRUE;
}

static void
_vp8_calculate_coded_size (GstVaVp8Enc * self)
{
  GstVaBaseEnc *base = GST_VA_BASE_ENC (self);
  gint width = GST_ROUND_UP_16 (base->width);
  gint height = GST_ROUND_UP_16 (base->height);

  /* Safe choice, 2 times 4:2:0 framesize plus header. */
  base->codedbuf_size = 3 * width * height + 1278;
  GST_INFO_OBJECT (self, "Calculate codedbuf size: %u", base->codedbuf_size);
}

/* Normalizes bitrate (and CPB size) for HRD conformance. */
static void
_vp8_calculate_bitrate_hrd (GstVaVp8Enc * self)
{
  self->rc.max_bitrate_bits = self->rc.max_bitrate * 1000;
  GST_DEBUG_OBJECT (self, "Max bitrate: %u bits/sec",
      self->rc.max_bitrate_bits);

  self->rc.target_bitrate_bits = self->rc.target_bitrate * 1000;
  GST_DEBUG_OBJECT (self, "Target bitrate: %u bits/sec",
      self->rc.target_bitrate_bits);

  if (self->rc.cpb_size > 0 && self->rc.cpb_size < (self->rc.max_bitrate / 2)) {
    GST_INFO_OBJECT (self, "Too small cpb_size: %d", self->rc.cpb_size);

    /* Cache 2s coded data by default. */
    self->rc.cpb_size = self->rc.max_bitrate * 2;
    GST_INFO_OBJECT (self, "Adjust cpb_size to: %d", self->rc.cpb_size);
  } else if (self->rc.cpb_size == 0) {
    self->rc.cpb_size = self->rc.target_bitrate;
  }

  self->rc.cpb_length_bits = self->rc.cpb_size * 1000;
  GST_DEBUG_OBJECT (self, "HRD CPB size: %u bits", self->rc.cpb_length_bits);
}

static guint
_vp8_adjust_loopfilter_level_based_on_qindex (guint qindex)
{
  /* This magic has been copied from the vp9 encoder. */
  if (qindex >= 40) {
    return (gint32) (-18.98682 + 0.3967082 * (gfloat) qindex +
        0.0005054 * pow ((float) qindex - 127.5, 2) -
        9.692e-6 * pow ((float) qindex - 127.5, 3));
  } else {
    return qindex / 4;
  }
}

/* Estimates a good enough bitrate if none was supplied. */
static gboolean
_vp8_ensure_rate_control (GstVaVp8Enc * self)
{
  /* User can specify the properties of: "bitrate", "target-percentage",
   * "max-qp", "min-qp", "qp", "loop-filter-level", "sharpness-level",
   * "mbbrc", "cpb-size", "rate-control" and "target-usage" to control
   * the RC behavior.
   *
   * "target-usage" is different from the others, it controls the encoding
   * speed and quality, while the others control encoding bit rate and
   * quality. The lower value has better quality(maybe bigger MV search
   * range) but slower speed, the higher value has faster speed but lower
   * quality. It is valid for all modes.
   *
   * The possible composition to control the bit rate and quality:
   *
   * 1. CQP mode: "rate-control=cqp", then "qp"(the qindex in VP8) specify
   *    the QP of frames(within the "max-qp" and "min-qp" range). The QP
   *    will not change during the whole stream. "loop-filter-level" and
   *    "sharpness-level" together determine how much the filtering can
   *    change the sample values. Other properties related to rate control
   *    are ignored.
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
   */

  GstVaBaseEnc *base = GST_VA_BASE_ENC (self);
  guint bitrate;
  guint32 rc_ctrl, rc_mode, quality_level;

  quality_level = gst_va_encoder_get_quality_level (base->encoder,
      base->profile, GST_VA_BASE_ENC_ENTRYPOINT (base));
  if (self->rc.target_usage > quality_level) {
    GST_INFO_OBJECT (self, "User setting target-usage: %d is not supported, "
        "fallback to %d", self->rc.target_usage, quality_level);
    self->rc.target_usage = quality_level;

    update_property_uint (base, &self->prop.target_usage, self->rc.target_usage,
        PROP_TARGET_USAGE);
  }

  GST_OBJECT_LOCK (self);
  rc_ctrl = self->prop.rc_ctrl;
  GST_OBJECT_UNLOCK (self);

  if (rc_ctrl != VA_RC_NONE) {
    rc_mode = gst_va_encoder_get_rate_control_mode (base->encoder,
        base->profile, GST_VA_BASE_ENC_ENTRYPOINT (base));

    if (!(rc_mode & rc_ctrl)) {
      guint32 defval =
          G_PARAM_SPEC_ENUM (properties[PROP_RATE_CONTROL])->default_value;
      GST_INFO_OBJECT (self, "The rate control mode %i is not supported, "
          "fallback to %i mode", rc_ctrl, defval);
      self->rc.rc_ctrl_mode = defval;

      update_property_uint (base, &self->prop.rc_ctrl, self->rc.rc_ctrl_mode,
          PROP_RATE_CONTROL);
    }
  } else {
    self->rc.rc_ctrl_mode = VA_RC_NONE;
  }

  if (self->rc.min_qindex > self->rc.max_qindex) {
    GST_INFO_OBJECT (self, "The min_qindex %d is bigger than the max_qindex"
        " %d, set it to the max_qindex", self->rc.min_qindex,
        self->rc.max_qindex);
    self->rc.min_qindex = self->rc.max_qindex;

    update_property_uint (base, &self->prop.min_qp, self->rc.min_qindex,
        PROP_MIN_QP);
  }

  /* Make the qp in the valid range. */
  if (self->rc.base_qindex < self->rc.min_qindex) {
    if (self->rc.base_qindex != DEFAULT_BASE_QINDEX)
      GST_INFO_OBJECT (self, "The base_qindex %d is smaller than the"
          " min_qindex %d, set it to the min_qindex", self->rc.base_qindex,
          self->rc.min_qindex);
    self->rc.base_qindex = self->rc.min_qindex;
  }
  if (self->rc.base_qindex > self->rc.max_qindex) {
    if (self->rc.base_qindex != DEFAULT_BASE_QINDEX)
      GST_INFO_OBJECT (self, "The base_qindex %d is bigger than the"
          " max_qindex %d, set it to the max_qindex", self->rc.base_qindex,
          self->rc.max_qindex);
    self->rc.base_qindex = self->rc.max_qindex;
  }

  /* Calculate the loop filter level. */
  if (self->rc.rc_ctrl_mode == VA_RC_CQP) {
    if (self->rc.filter_level == -1)
      self->rc.filter_level =
          _vp8_adjust_loopfilter_level_based_on_qindex (self->rc.base_qindex);
  }

  GST_OBJECT_LOCK (self);
  bitrate = self->prop.bitrate;
  GST_OBJECT_UNLOCK (self);

  /* Calculate a bitrate if it is not set. */
  if ((self->rc.rc_ctrl_mode == VA_RC_CBR || self->rc.rc_ctrl_mode == VA_RC_VBR)
      && bitrate == 0) {
    guint64 factor;
    guint bits_per_pix;

    bits_per_pix = 24;
    factor = (guint64) base->width * base->height * bits_per_pix / 16;
    bitrate = gst_util_uint64_scale (factor,
        GST_VIDEO_INFO_FPS_N (&base->in_info),
        GST_VIDEO_INFO_FPS_D (&base->in_info)) / 1000;

    GST_INFO_OBJECT (self, "target bitrate computed to %u kbps", bitrate);
  }

  /* Adjust the setting based on RC mode. */
  switch (self->rc.rc_ctrl_mode) {
    case VA_RC_NONE:
    case VA_RC_CQP:
      bitrate = 0;
      self->rc.max_bitrate = 0;
      self->rc.target_bitrate = 0;
      self->rc.target_percentage = 0;
      self->rc.cpb_size = 0;
      self->rc.mbbrc = 0;
      break;
    case VA_RC_CBR:
      self->rc.max_bitrate = bitrate;
      self->rc.target_bitrate = bitrate;
      self->rc.target_percentage = 100;
      self->rc.base_qindex = DEFAULT_BASE_QINDEX;
      self->rc.filter_level = DEFAULT_LOOP_FILTER_LEVEL;
      self->rc.sharpness_level = 0;
      break;
    case VA_RC_VBR:
      self->rc.base_qindex = DEFAULT_BASE_QINDEX;
      self->rc.target_percentage = MAX (10, self->rc.target_percentage);
      self->rc.max_bitrate = (guint) gst_util_uint64_scale_int (bitrate,
          100, self->rc.target_percentage);
      self->rc.target_bitrate = bitrate;
      self->rc.filter_level = DEFAULT_LOOP_FILTER_LEVEL;
      self->rc.sharpness_level = 0;
      break;
    default:
      GST_WARNING_OBJECT (self, "Unsupported rate control");
      return FALSE;
      break;
  }

  GST_DEBUG_OBJECT (self, "Max bitrate: %u kbps, target bitrate: %u kbps",
      self->rc.max_bitrate, self->rc.target_bitrate);

  if (self->rc.rc_ctrl_mode == VA_RC_CBR || self->rc.rc_ctrl_mode == VA_RC_VBR)
    _vp8_calculate_bitrate_hrd (self);

  /* update & notifications */
  update_property_uint (base, &self->prop.bitrate, bitrate, PROP_BITRATE);
  update_property_uint (base, &self->prop.cpb_size, self->rc.cpb_size,
      PROP_CPB_SIZE);
  update_property_uint (base, &self->prop.target_percentage,
      self->rc.target_percentage, PROP_TARGET_PERCENTAGE);
  update_property_uint (base, &self->prop.qp, self->rc.base_qindex, PROP_QP);
  update_property_uint (base, ((guint *) (&self->prop.filter_level)),
      self->rc.filter_level, PROP_LOOP_FILTER_LEVEL);
  update_property_uint (base, &self->prop.sharpness_level,
      self->rc.sharpness_level, PROP_SHARPNESS_LEVEL);
  update_property_uint (base, &self->prop.mbbrc, self->rc.mbbrc, PROP_MBBRC);

  return TRUE;
}

static gboolean
gst_va_vp8_enc_reconfig (GstVaBaseEnc * base)
{
  GstVaBaseEncClass *klass = GST_VA_BASE_ENC_GET_CLASS (base);
  GstVideoEncoder *venc = GST_VIDEO_ENCODER (base);
  GstVaVp8Enc *self = GST_VA_VP8_ENC (base);
  GstCaps *out_caps, *reconf_caps = NULL;
  GstVideoCodecState *output_state;
  GstVideoFormat format, reconf_format = GST_VIDEO_FORMAT_UNKNOWN;
  const GstVideoFormatInfo *format_info;
  gboolean do_renegotiation = TRUE, do_reopen, need_negotiation;
  guint max_ref_frames, max_surfaces = 0, codedbuf_size, latency_num;
  gint width, height;
  GstClockTime latency;

  width = GST_VIDEO_INFO_WIDTH (&base->in_info);
  height = GST_VIDEO_INFO_HEIGHT (&base->in_info);
  format = GST_VIDEO_INFO_FORMAT (&base->in_info);
  codedbuf_size = base->codedbuf_size;
  latency_num = base->preferred_output_delay;

  /* VP8 only support 4:2:0 formats so check that first */
  format_info = gst_video_format_get_info (format);
  if (GST_VIDEO_FORMAT_INFO_W_SUB (format_info, 1) != 1 ||
      GST_VIDEO_FORMAT_INFO_H_SUB (format_info, 1) != 1)
    return FALSE;

  need_negotiation =
      !gst_va_encoder_get_reconstruct_pool_config (base->encoder, &reconf_caps,
      &max_surfaces);

  if (!need_negotiation && reconf_caps) {
    GstVideoInfo vi;
    if (!gst_video_info_from_caps (&vi, reconf_caps))
      return FALSE;
    reconf_format = GST_VIDEO_INFO_FORMAT (&vi);
  }

  /* First check */
  do_reopen = !(format == reconf_format && width == base->width
      && height == base->height && self->prop.rc_ctrl == self->rc.rc_ctrl_mode);

  if (do_reopen && gst_va_encoder_is_open (base->encoder))
    gst_va_encoder_close (base->encoder);

  gst_va_base_enc_reset_state (base);

  if (base->is_live) {
    base->preferred_output_delay = 0;
  } else {
    base->preferred_output_delay = 1;
  }

  base->profile = VAProfileVP8Version0_3;
  base->width = width;
  base->height = height;

  /* Frame rate is needed for rate control and PTS setting. */
  if (GST_VIDEO_INFO_FPS_N (&base->in_info) == 0
      || GST_VIDEO_INFO_FPS_D (&base->in_info) == 0) {
    GST_INFO_OBJECT (self, "Unknown framerate, just set to 30 fps");
    GST_VIDEO_INFO_FPS_N (&base->in_info) = 30;
    GST_VIDEO_INFO_FPS_D (&base->in_info) = 1;
  }
  base->frame_duration = gst_util_uint64_scale (GST_SECOND,
      GST_VIDEO_INFO_FPS_D (&base->in_info),
      GST_VIDEO_INFO_FPS_N (&base->in_info));

  GST_DEBUG_OBJECT (self, "resolution:%dx%d, frame duration is %"
      GST_TIME_FORMAT, base->width, base->height,
      GST_TIME_ARGS (base->frame_duration));

  if (!_vp8_ensure_rate_control (self))
    return FALSE;

  if (!_vp8_generate_gop_structure (self))
    return FALSE;

  _vp8_calculate_coded_size (self);

  /* Let the downstream know the new latency. */
  if (latency_num != base->preferred_output_delay + 1) {
    need_negotiation = TRUE;
    latency_num = base->preferred_output_delay + 1;
  }

  /* Set the latency */
  latency = gst_util_uint64_scale (latency_num,
      GST_VIDEO_INFO_FPS_D (&base->input_state->info) * GST_SECOND,
      GST_VIDEO_INFO_FPS_N (&base->input_state->info));
  gst_video_encoder_set_latency (venc, latency, latency);

  max_ref_frames = GST_VP8_MAX_REF_FRAMES;
  max_ref_frames += base->preferred_output_delay;
  base->min_buffers = max_ref_frames;
  max_ref_frames += 3;          /* scratch frames */

  /* Second check after calculations. */
  do_reopen |= !(codedbuf_size == base->codedbuf_size);
  if (do_reopen && gst_va_encoder_is_open (base->encoder))
    gst_va_encoder_close (base->encoder);

  if (!gst_va_encoder_is_open (base->encoder)
      && !gst_va_encoder_open (base->encoder, base->profile,
          GST_VIDEO_INFO_FORMAT (&base->in_info), base->rt_format,
          base->width, base->height, base->codedbuf_size,
          max_ref_frames, self->rc.rc_ctrl_mode, 0)) {
    GST_ERROR_OBJECT (self, "Failed to open the VA encoder.");
    return FALSE;
  }

  /* Add some tags. */
  gst_va_base_enc_add_codec_tag (base, "VP8");

  out_caps = gst_va_profile_caps (base->profile, klass->entrypoint);
  g_assert (out_caps);
  out_caps = gst_caps_fixate (out_caps);

  gst_caps_set_simple (out_caps, "width", G_TYPE_INT, base->width,
      "height", G_TYPE_INT, base->height, NULL);

  if (!need_negotiation) {
    output_state = gst_video_encoder_get_output_state (venc);
    do_renegotiation = TRUE;
    if (output_state) {
      do_renegotiation = !gst_caps_is_subset (output_state->caps, out_caps);
      gst_video_codec_state_unref (output_state);
    }
    if (!do_renegotiation) {
      gst_caps_unref (out_caps);
      return TRUE;
    }
  }

  GST_DEBUG_OBJECT (self, "output caps is %" GST_PTR_FORMAT, out_caps);

  output_state =
      gst_video_encoder_set_output_state (venc, out_caps, base->input_state);
  gst_video_codec_state_unref (output_state);

  if (!gst_video_encoder_negotiate (venc)) {
    GST_ERROR_OBJECT (self, "Failed to negotiate with the downstream");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_va_vp8_enc_flush (GstVideoEncoder * venc)
{
  GstVaVp8Enc *self = GST_VA_VP8_ENC (venc);

  _update_ref_frame (self, &self->gop.last_ref, NULL);
  self->gop.frame_num = FRAME_NUM_INVALID;

  return GST_VIDEO_ENCODER_CLASS (parent_class)->flush (venc);
}

static void
_vp8_fill_sequence_param (GstVaVp8Enc * self,
    VAEncSequenceParameterBufferVP8 * sequence)
{
  GstVaBaseEnc *base = GST_VA_BASE_ENC (self);
  /* *INDENT-OFF* */
  *sequence = (VAEncSequenceParameterBufferVP8) {
    .frame_width = base->width,
    .frame_height = base->height,
    .frame_width_scale = 0,
    .frame_height_scale = 0,
    .error_resilient = 0,
    .kf_auto = 0,
    .kf_min_dist = 0,
    .kf_max_dist = 0,
    .bits_per_second = self->rc.target_bitrate_bits,
    .intra_period = self->gop.keyframe_interval,
    .reference_frames = {VA_INVALID_SURFACE, VA_INVALID_SURFACE,
                         VA_INVALID_SURFACE, VA_INVALID_SURFACE},
  };
  /* *INDENT-ON* */
}

static gboolean
_vp8_add_sequence_param (GstVaVp8Enc * self, GstVaEncodePicture * picture,
    VAEncSequenceParameterBufferVP8 * sequence)
{
  GstVaBaseEnc *base = GST_VA_BASE_ENC (self);

  if (!gst_va_encoder_add_param (base->encoder, picture,
          VAEncSequenceParameterBufferType, sequence, sizeof (*sequence))) {
    GST_ERROR_OBJECT (self, "Failed to create the sequence parameter");
    return FALSE;
  }

  return TRUE;
}

static gboolean
_vp8_fill_quant_param (GstVaVp8Enc * self, GstVaVp8EncFrame * va_frame,
    VAQMatrixBufferVP8 * quant_param)
{
  int i, q;

  q = self->rc.base_qindex;
  /* A hint for the driver to use a higher qindex for the key frame */
  if (va_frame->type == GST_VP8_KEY_FRAME)
    q = MIN (q + 5, self->rc.max_qindex);

  for (i = 0; i < 4; i++)
    quant_param->quantization_index[i] = q;
  for (i = 0; i < 5; i++)
    quant_param->quantization_index_delta[i] = 0;

  return TRUE;
}

static gboolean
_vp8_fill_frame_param (GstVaVp8Enc * self, GstVaVp8EncFrame * va_frame,
    VAEncPictureParameterBufferVP8 * pic_param)
{
  /* *INDENT-OFF* */
  *pic_param = (VAEncPictureParameterBufferVP8) {
    .reconstructed_frame =
        gst_va_encode_picture_get_reconstruct_surface (va_frame->base.picture),
    /* Set it later for inter frame. */
    .ref_last_frame = VA_INVALID_SURFACE,
    .ref_gf_frame = VA_INVALID_SURFACE,
    .ref_arf_frame = VA_INVALID_SURFACE,

    .coded_buf = va_frame->base.picture->coded_buffer,
    .ref_flags.bits = {
      .force_kf = (va_frame->type == GST_VP8_KEY_FRAME),
      /* Set all the refs later if inter frame. */
      .no_ref_last = (va_frame->type == GST_VP8_KEY_FRAME),
      .no_ref_gf = (va_frame->type == GST_VP8_KEY_FRAME),
      .no_ref_arf = (va_frame->type == GST_VP8_KEY_FRAME),
      .temporal_id = 0,
      .first_ref = 0,
      .second_ref = 0,
      .reserved = 0,
    },
    .pic_flags.bits = {
      .frame_type = (va_frame->type == GST_VP8_INTER_FRAME),
      .version = 0, /* bicubic */
      .show_frame = 1,
      .color_space = 0,
      .recon_filter_type = 0, /* bicubic */
      .loop_filter_type = 0,
      .auto_partitions = 0,
      .num_token_partitions = 0,
      .clamping_type = 0,
      .segmentation_enabled = 0,
      .update_mb_segmentation_map = 0,
      .update_segment_feature_data = 0,
      .loop_filter_adj_enable = 0,
      .refresh_entropy_probs = 0,
      .refresh_golden_frame = 1,
      .refresh_alternate_frame = 1,
      .refresh_last = 1,
      .copy_buffer_to_golden = 0,
      .copy_buffer_to_alternate = 0,
      .sign_bias_golden = 0,
      .sign_bias_alternate = 0,
      .mb_no_coeff_skip = 0,
      .forced_lf_adjustment = (va_frame->type == GST_VP8_INTER_FRAME),
    },
    .loop_filter_level = {0, }, /* set later */
    .ref_lf_delta = {0, },
    .mode_lf_delta = {0, },
    .sharpness_level = self->rc.sharpness_level,
    .clamp_qindex_high = 127,
    .clamp_qindex_low = 0,
    .va_reserved = {0, }
  };
  /* *INDENT-ON* */

  for (gint i = 0; i < 4; ++i)
    pic_param->loop_filter_level[i] = self->rc.filter_level;

  if (va_frame->type == GST_VP8_INTER_FRAME) {
    g_assert (self->gop.last_ref != NULL);

    pic_param->ref_last_frame = gst_va_encode_picture_get_reconstruct_surface
        (_enc_frame (self->gop.last_ref)->base.picture);
    pic_param->ref_gf_frame = pic_param->ref_arf_frame =
        pic_param->ref_last_frame;
  }

  return TRUE;
}

static gboolean
_vp8_encode_frame (GstVaVp8Enc * self, GstVaVp8EncFrame * va_frame)
{
  GstVaBaseEnc *base = GST_VA_BASE_ENC (self);
  VAEncPictureParameterBufferVP8 pic_param;
  VAQMatrixBufferVP8 quant_param;

  if (!_vp8_fill_frame_param (self, va_frame, &pic_param)) {
    GST_ERROR_OBJECT (self, "Fails to fill the frame parameter.");
    return FALSE;
  }

  if (!gst_va_encoder_add_param (base->encoder, va_frame->base.picture,
          VAEncPictureParameterBufferType, &pic_param, sizeof (pic_param))) {
    GST_ERROR_OBJECT (self, "Failed to create the frame parameter");
    return FALSE;
  }

  if (!_vp8_fill_quant_param (self, va_frame, &quant_param)) {
    GST_ERROR_OBJECT (self, "Fails to fill the quantization parameter.");
    return FALSE;
  }

  if (!gst_va_encoder_add_param (base->encoder, va_frame->base.picture,
          VAQMatrixBufferType, &quant_param, sizeof (quant_param))) {
    GST_ERROR_OBJECT (self, "Failed to create the quantization parameter");
    return FALSE;
  }

  if (!gst_va_encoder_encode (base->encoder, va_frame->base.picture)) {
    GST_ERROR_OBJECT (self, "Encode frame error");
    return FALSE;
  }

  return TRUE;
}

static GstFlowReturn
gst_va_vp8_enc_encode_frame (GstVaBaseEnc * base,
    GstVideoCodecFrame * gst_frame, gboolean is_last)
{
  GstVaVp8Enc *self = GST_VA_VP8_ENC (base);
  GstVaVp8EncFrame *va_frame = _enc_frame (gst_frame);
  VAEncSequenceParameterBufferVP8 seq_param;

  GST_LOG_OBJECT (self, "Encode frame.");

  g_assert (va_frame->base.picture == NULL);
  va_frame->base.picture = gst_va_encode_picture_new (base->encoder,
      gst_frame->input_buffer);

  if (va_frame->frame_num == 0) {
    _vp8_fill_sequence_param (self, &seq_param);
    if (!_vp8_add_sequence_param (self, va_frame->base.picture, &seq_param))
      return GST_FLOW_ERROR;

    if (!gst_va_base_enc_add_rate_control_parameter (base,
            va_frame->base.picture, self->rc.rc_ctrl_mode,
            self->rc.max_bitrate_bits, self->rc.target_percentage,
            self->rc.base_qindex, self->rc.min_qindex, self->rc.max_qindex,
            self->rc.mbbrc))
      return GST_FLOW_ERROR;

    if (!gst_va_base_enc_add_quality_level_parameter (base,
            va_frame->base.picture, self->rc.target_usage))
      return GST_FLOW_ERROR;

    if (!gst_va_base_enc_add_frame_rate_parameter (base,
            va_frame->base.picture))
      return GST_FLOW_ERROR;

    if (!gst_va_base_enc_add_hrd_parameter (base, va_frame->base.picture,
            self->rc.rc_ctrl_mode, self->rc.cpb_length_bits))
      return GST_FLOW_ERROR;
  }

  if (!_vp8_encode_frame (self, va_frame)) {
    GST_ERROR_OBJECT (self, "Fails to encode one frame.");
    return GST_FLOW_ERROR;
  }

  /* The last frame will always change to this. */
  _update_ref_frame (self, &self->gop.last_ref, gst_frame);

  g_queue_push_tail (&base->output_list, gst_video_codec_frame_ref (gst_frame));
  return GST_FLOW_OK;
}

static gboolean
gst_va_vp8_enc_prepare_output (GstVaBaseEnc * base,
    GstVideoCodecFrame * frame, gboolean * complete)
{
  GstVaVp8EncFrame *frame_enc;
  GstBuffer *buf;

  frame_enc = _enc_frame (frame);

  GST_LOG_OBJECT (base, "Prepare to output: frame system_frame_number: %d,"
      "frame_num: %d, frame type: %s", frame->system_frame_number,
      frame_enc->frame_num, frame_enc->type ? "Inter" : "Intra");

  buf = gst_va_base_enc_create_output_buffer (base, frame_enc->base.picture,
      NULL, 0);
  if (!buf) {
    GST_ERROR_OBJECT (base, "Failed to create output buffer");
    return FALSE;
  }

  *complete = TRUE;

  GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_MARKER);
  if (frame_enc->frame_num == 0) {
    GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
    GST_BUFFER_FLAG_UNSET (buf, GST_BUFFER_FLAG_DELTA_UNIT);
  } else {
    GST_VIDEO_CODEC_FRAME_UNSET_SYNC_POINT (frame);
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT);
  }

  gst_buffer_replace (&frame->output_buffer, buf);
  gst_clear_buffer (&buf);

  return TRUE;
}

/* *INDENT-OFF* */
static const gchar *sink_caps_str =
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_VA,
        "{ NV12 }") " ;"
    GST_VIDEO_CAPS_MAKE ("{ NV12 }");
/* *INDENT-ON* */

static const gchar *src_caps_str = "video/x-vp8";

static gpointer
_register_debug_category (gpointer data)
{
  GST_DEBUG_CATEGORY_INIT (gst_va_vp8enc_debug, "vavp8enc", 0,
      "VA vp8 encoder");

  return NULL;
}

static void
gst_va_vp8_enc_init (GTypeInstance * instance, gpointer g_class)
{
  GstVaVp8Enc *self = GST_VA_VP8_ENC (instance);

  /* default values */
  self->prop.bitrate = 0;
  self->prop.target_usage = 4;
  self->prop.cpb_size = 0;
  self->prop.target_percentage = DEFAULT_TARGET_PERCENTAGE;
  self->prop.keyframe_interval = MAX_KEY_FRAME_INTERVAL;
  self->prop.qp = DEFAULT_BASE_QINDEX;
  self->prop.min_qp = 0;
  self->prop.max_qp = 127;
  self->prop.mbbrc = 0;
  self->prop.filter_level = -1;
  self->prop.sharpness_level = 0;

  if (properties[PROP_RATE_CONTROL]) {
    self->prop.rc_ctrl =
        G_PARAM_SPEC_ENUM (properties[PROP_RATE_CONTROL])->default_value;
  } else {
    self->prop.rc_ctrl = VA_RC_NONE;
  }
}

static void
gst_va_vp8_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVaVp8Enc *const self = GST_VA_VP8_ENC (object);
  GstVaBaseEnc *base = GST_VA_BASE_ENC (self);
  GstVaEncoder *encoder = NULL;
  gboolean no_effect;

  gst_object_replace ((GstObject **) (&encoder), (GstObject *) base->encoder);
  no_effect = (encoder && gst_va_encoder_is_open (encoder));
  if (encoder)
    gst_object_unref (encoder);

  GST_OBJECT_LOCK (self);

  switch (prop_id) {
    case PROP_KEYFRAME_INT:
      self->prop.keyframe_interval = g_value_get_uint (value);
      break;
    case PROP_QP:
      self->prop.qp = g_value_get_uint (value);
      no_effect = FALSE;
      g_atomic_int_set (&GST_VA_BASE_ENC (self)->reconf, TRUE);
      break;
    case PROP_MAX_QP:
      self->prop.max_qp = g_value_get_uint (value);
      break;
    case PROP_MIN_QP:
      self->prop.min_qp = g_value_get_uint (value);
      break;
    case PROP_BITRATE:
      self->prop.bitrate = g_value_get_uint (value);
      no_effect = FALSE;
      g_atomic_int_set (&GST_VA_BASE_ENC (self)->reconf, TRUE);
      break;
    case PROP_TARGET_USAGE:
      self->prop.target_usage = g_value_get_uint (value);
      no_effect = FALSE;
      g_atomic_int_set (&GST_VA_BASE_ENC (self)->reconf, TRUE);
      break;
    case PROP_TARGET_PERCENTAGE:
      self->prop.target_percentage = g_value_get_uint (value);
      no_effect = FALSE;
      g_atomic_int_set (&GST_VA_BASE_ENC (self)->reconf, TRUE);
      break;
    case PROP_CPB_SIZE:
      self->prop.cpb_size = g_value_get_uint (value);
      no_effect = FALSE;
      g_atomic_int_set (&GST_VA_BASE_ENC (self)->reconf, TRUE);
      break;
    case PROP_RATE_CONTROL:
      self->prop.rc_ctrl = g_value_get_enum (value);
      no_effect = FALSE;
      g_atomic_int_set (&GST_VA_BASE_ENC (self)->reconf, TRUE);
      break;
    case PROP_LOOP_FILTER_LEVEL:
      self->prop.filter_level = g_value_get_int (value);
      no_effect = FALSE;
      g_atomic_int_set (&GST_VA_BASE_ENC (self)->reconf, TRUE);
      break;
    case PROP_SHARPNESS_LEVEL:
      self->prop.sharpness_level = g_value_get_uint (value);
      no_effect = FALSE;
      g_atomic_int_set (&GST_VA_BASE_ENC (self)->reconf, TRUE);
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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }

  GST_OBJECT_UNLOCK (self);

  if (no_effect) {
#ifndef GST_DISABLE_GST_DEBUG
    GST_WARNING_OBJECT (self, "Property `%s` change may not take effect "
        "until the next encoder reconfig.", pspec->name);
#endif
  }
}

static void
gst_va_vp8_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVaVp8Enc *const self = GST_VA_VP8_ENC (object);

  GST_OBJECT_LOCK (self);

  switch (prop_id) {
    case PROP_KEYFRAME_INT:
      g_value_set_uint (value, self->prop.keyframe_interval);
      break;
    case PROP_QP:
      g_value_set_uint (value, self->prop.qp);
      break;
    case PROP_MIN_QP:
      g_value_set_uint (value, self->prop.min_qp);
      break;
    case PROP_MAX_QP:
      g_value_set_uint (value, self->prop.max_qp);
      break;
    case PROP_BITRATE:
      g_value_set_uint (value, self->prop.bitrate);
      break;
    case PROP_TARGET_USAGE:
      g_value_set_uint (value, self->prop.target_usage);
      break;
    case PROP_TARGET_PERCENTAGE:
      g_value_set_uint (value, self->prop.target_percentage);
      break;
    case PROP_CPB_SIZE:
      g_value_set_uint (value, self->prop.cpb_size);
      break;
    case PROP_RATE_CONTROL:
      g_value_set_enum (value, self->prop.rc_ctrl);
      break;
    case PROP_MBBRC:
      g_value_set_enum (value, self->prop.mbbrc);
      break;
    case PROP_LOOP_FILTER_LEVEL:
      g_value_set_int (value, self->prop.filter_level);
      break;
    case PROP_SHARPNESS_LEVEL:
      g_value_set_uint (value, self->prop.sharpness_level);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }

  GST_OBJECT_UNLOCK (self);
}

static void
gst_va_vp8_enc_class_init (gpointer g_klass, gpointer class_data)
{
  GstCaps *src_doc_caps, *sink_doc_caps;
  GstPadTemplate *sink_pad_templ, *src_pad_templ;
  GObjectClass *object_class = G_OBJECT_CLASS (g_klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_klass);
  GstVideoEncoderClass *venc_class = GST_VIDEO_ENCODER_CLASS (g_klass);
  GstVaBaseEncClass *va_enc_class = GST_VA_BASE_ENC_CLASS (g_klass);
  GstVaVp8EncClass *vavp8enc_class = GST_VA_VP8_ENC_CLASS (g_klass);
  GstVaDisplay *display;
  GstVaEncoder *encoder;
  struct CData *cdata = class_data;
  gchar *long_name;
  const gchar *name, *desc;
  gint n_props = N_PROPERTIES;
  GParamFlags param_flags =
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT;

  if (cdata->entrypoint == VAEntrypointEncSlice) {
    desc = "VA-API based VP8 video encoder";
    name = "VA-API VP8 Encoder";
  } else {
    desc = "VA-API based VP8 low power video encoder";
    name = "VA-API VP8 Low Power Encoder";
  }

  if (cdata->description)
    long_name = g_strdup_printf ("%s in %s", name, cdata->description);
  else
    long_name = g_strdup (name);

  gst_element_class_set_metadata (element_class, long_name,
      "Codec/Encoder/Video/Hardware", desc,
      "Jochen Henneberg <jochen@centricular.com>");

  sink_doc_caps = gst_caps_from_string (sink_caps_str);
  src_doc_caps = gst_caps_from_string (src_caps_str);

  parent_class = g_type_class_peek_parent (g_klass);

  va_enc_class->codec = VP8;
  va_enc_class->entrypoint = cdata->entrypoint;
  va_enc_class->render_device_path = g_strdup (cdata->render_device_path);
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

  object_class->set_property = gst_va_vp8_enc_set_property;
  object_class->get_property = gst_va_vp8_enc_get_property;

  venc_class->flush = GST_DEBUG_FUNCPTR (gst_va_vp8_enc_flush);
  va_enc_class->reset_state = GST_DEBUG_FUNCPTR (gst_va_vp8_enc_reset_state);
  va_enc_class->reconfig = GST_DEBUG_FUNCPTR (gst_va_vp8_enc_reconfig);
  va_enc_class->new_frame = GST_DEBUG_FUNCPTR (gst_va_vp8_enc_new_frame);
  va_enc_class->reorder_frame =
      GST_DEBUG_FUNCPTR (gst_va_vp8_enc_reorder_frame);
  va_enc_class->encode_frame = GST_DEBUG_FUNCPTR (gst_va_vp8_enc_encode_frame);
  va_enc_class->prepare_output =
      GST_DEBUG_FUNCPTR (gst_va_vp8_enc_prepare_output);

  {
    display = gst_va_display_platform_new (va_enc_class->render_device_path);
    encoder = gst_va_encoder_new (display, va_enc_class->codec,
        va_enc_class->entrypoint);
    if (gst_va_encoder_get_rate_control_enum (encoder,
            vavp8enc_class->rate_control)) {
      g_snprintf (vavp8enc_class->rate_control_type_name,
          G_N_ELEMENTS (vavp8enc_class->rate_control_type_name) - 1,
          "GstVaEncoderRateControl_%" GST_FOURCC_FORMAT "%s_%s",
          GST_FOURCC_ARGS (va_enc_class->codec),
          (va_enc_class->entrypoint == VAEntrypointEncSliceLP) ? "_LP" : "",
          g_path_get_basename (va_enc_class->render_device_path));
      vavp8enc_class->rate_control_type =
          g_enum_register_static (vavp8enc_class->rate_control_type_name,
          vavp8enc_class->rate_control);
      gst_type_mark_as_plugin_api (vavp8enc_class->rate_control_type, 0);
    }
    gst_object_unref (encoder);
    gst_object_unref (display);
  }

  g_free (long_name);
  g_free (cdata->description);
  g_free (cdata->render_device_path);
  gst_caps_unref (cdata->src_caps);
  gst_caps_unref (cdata->sink_caps);
  g_free (cdata);

  /**
   * GstVaVp8Enc:key-int-max:
   *
   * The maximal distance between two keyframes.
   */
  properties[PROP_KEYFRAME_INT] = g_param_spec_uint ("key-int-max",
      "Key frame maximal interval",
      "The maximal distance between two keyframes. It decides the size of GOP"
      " (0: auto-calculate)", 0, MAX_KEY_FRAME_INTERVAL, 0, param_flags);

  /**
   * GstVaVp8Enc:min-qp:
   *
   * The minimum quantizer value.
   */
  properties[PROP_MIN_QP] = g_param_spec_uint ("min-qp", "Minimum QP",
      "Minimum quantizer value for each frame", 0, 126, 0, param_flags);

  /**
   * GstVaVp8Enc:max-qp:
   *
   * The maximum quantizer value.
   */
  properties[PROP_MAX_QP] = g_param_spec_uint ("max-qp", "Maximum QP",
      "Maximum quantizer value for each frame", 1, 127, 127, param_flags);

  /**
   * GstVaVp8Enc:qp:
   *
   * The basic quantizer value for all frames.
   */
  properties[PROP_QP] = g_param_spec_uint ("qp", "The frame QP",
      "In CQP mode, it specifies the basic quantizer value for all frames. "
      "In other modes, it is ignored", 0, 255, DEFAULT_BASE_QINDEX,
      param_flags | GST_PARAM_MUTABLE_PLAYING);

  /**
   * GstVaVp8Enc:bitrate:
   *
   * The desired target bitrate, expressed in kbps.
   * This is not available in CQP mode.
   *
   * CBR: This applies equally to the minimum, maximum and target bitrate.
   * VBR: This applies to the target bitrate. The driver will use the
   * "target-percentage" together to calculate the minimum and maximum bitrate.
   */
  properties[PROP_BITRATE] = g_param_spec_uint ("bitrate", "Bitrate (kbps)",
      "The desired bitrate expressed in kbps (0: auto-calculate)",
      0, 2000 * 1024, 0, param_flags | GST_PARAM_MUTABLE_PLAYING);

  /**
   * GstVaVp8Enc:target-percentage:
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
      50, 100, 66, param_flags | GST_PARAM_MUTABLE_PLAYING);

  /**
   * GstVaVp8Enc:cpb-size:
   *
   * The desired max CPB size in Kb (0: auto-calculate).
   */
  properties[PROP_CPB_SIZE] = g_param_spec_uint ("cpb-size",
      "max CPB size in Kb",
      "The desired max CPB size in Kb (0: auto-calculate)", 0, 2000 * 1024, 0,
      param_flags | GST_PARAM_MUTABLE_PLAYING);

  /**
   * GstVaVp8Enc:target-usage:
   *
   * The target usage of the encoder. It controls and balances the encoding
   * speed and the encoding quality. The lower value has better quality but
   * slower speed, the higher value has faster speed but lower quality.
   */
  properties[PROP_TARGET_USAGE] = g_param_spec_uint ("target-usage",
      "target usage",
      "The target usage to control and balance the encoding speed/quality",
      1, 7, 4, param_flags | GST_PARAM_MUTABLE_PLAYING);

  /**
   * GstVaVp8Enc:mbbrc:
   *
   * Macroblock level bitrate control.
   * This is not compatible with Constant QP rate control.
   */
  properties[PROP_MBBRC] = g_param_spec_enum ("mbbrc",
      "Macroblock level Bitrate Control",
      "Macroblock level Bitrate Control. It is not compatible with CQP",
      GST_TYPE_VA_FEATURE, GST_VA_FEATURE_DISABLED, param_flags);

  /**
   * GstVaVp8Enc:loop-filter-level:
   *
   * Controls the deblocking filter strength, -1 means auto calculation.
   */
  properties[PROP_LOOP_FILTER_LEVEL] = g_param_spec_int ("loop-filter-level",
      "Loop Filter Level",
      "Controls the deblocking filter strength, -1 means auto calculation",
      -1, 63, -1, param_flags | GST_PARAM_MUTABLE_PLAYING);

  /**
   * GstVaVp8Enc:sharpness-level:
   *
   * Controls the deblocking filter sensitivity.
   */
  properties[PROP_SHARPNESS_LEVEL] = g_param_spec_uint ("sharpness-level",
      "Sharpness Level",
      "Controls the deblocking filter sensitivity",
      0, 7, 0, param_flags | GST_PARAM_MUTABLE_PLAYING);

  if (vavp8enc_class->rate_control_type > 0) {
    properties[PROP_RATE_CONTROL] = g_param_spec_enum ("rate-control",
        "rate control mode",
        "The desired rate control mode for the encoder",
        vavp8enc_class->rate_control_type,
        vavp8enc_class->rate_control[0].value,
        GST_PARAM_CONDITIONALLY_AVAILABLE | GST_PARAM_MUTABLE_PLAYING
        | param_flags);
  } else {
    n_props--;
    properties[PROP_RATE_CONTROL] = NULL;
  }

  g_object_class_install_properties (object_class, n_props, properties);

  /**
   * GstVaFeature:
   * @GST_VA_FEATURE_DISABLED: The feature is disabled.
   * @GST_VA_FEATURE_ENABLED: The feature is enabled.
   * @GST_VA_FEATURE_AUTO: The feature is enabled automatically.
   *
   * Since: 1.24
   */
  gst_type_mark_as_plugin_api (GST_TYPE_VA_FEATURE, 0);
}

static GstCaps *
_complete_src_caps (GstCaps * srccaps)
{
  GstCaps *caps = gst_caps_copy (srccaps);
  GValue val = G_VALUE_INIT;

  g_value_init (&val, G_TYPE_STRING);
  gst_caps_set_value (caps, "alignment", &val);
  g_value_unset (&val);

  return caps;
}

gboolean
gst_va_vp8_enc_register (GstPlugin * plugin, GstVaDevice * device,
    GstCaps * sink_caps, GstCaps * src_caps, guint rank,
    VAEntrypoint entrypoint)
{
  static GOnce debug_once = G_ONCE_INIT;
  GType type;
  GTypeInfo type_info = {
    .class_size = sizeof (GstVaVp8EncClass),
    .class_init = gst_va_vp8_enc_class_init,
    .instance_size = sizeof (GstVaVp8Enc),
    .instance_init = gst_va_vp8_enc_init,
  };
  struct CData *cdata;
  gboolean ret;
  gchar *type_name, *feature_name;

  g_return_val_if_fail (GST_IS_PLUGIN (plugin), FALSE);
  g_return_val_if_fail (GST_IS_VA_DEVICE (device), FALSE);
  g_return_val_if_fail (GST_IS_CAPS (sink_caps), FALSE);
  g_return_val_if_fail (GST_IS_CAPS (src_caps), FALSE);
  g_return_val_if_fail (entrypoint == VAEntrypointEncSlice ||
      entrypoint == VAEntrypointEncSliceLP, FALSE);

  cdata = g_new (struct CData, 1);
  cdata->entrypoint = entrypoint;
  cdata->description = NULL;
  cdata->render_device_path = g_strdup (device->render_device_path);
  cdata->sink_caps = gst_caps_ref (sink_caps);
  cdata->src_caps = _complete_src_caps (src_caps);

  /* Class data will be leaked if the element never gets instantiated. */
  GST_MINI_OBJECT_FLAG_SET (cdata->sink_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
  GST_MINI_OBJECT_FLAG_SET (cdata->src_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);

  type_info.class_data = cdata;
  if (entrypoint == VAEntrypointEncSlice) {
    gst_va_create_feature_name (device, "GstVaVP8Enc", "GstVa%sVP8Enc",
        &type_name, "vavp8enc", "va%svp8enc", &feature_name,
        &cdata->description, &rank);
  } else {
    gst_va_create_feature_name (device, "GstVaVP8LPEnc", "GstVa%sVP8LPEnc",
        &type_name, "vavp8lpenc", "va%svp8lpenc", &feature_name,
        &cdata->description, &rank);
  }

  g_once (&debug_once, _register_debug_category, NULL);
  type = g_type_register_static (GST_TYPE_VA_BASE_ENC,
      type_name, &type_info, 0);
  ret = gst_element_register (plugin, feature_name, rank, type);

  g_free (type_name);
  g_free (feature_name);

  return ret;
}
