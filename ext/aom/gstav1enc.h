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


#ifndef __GST_AV1_ENC_H__
#define __GST_AV1_ENC_H__


#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideoencoder.h>

#include <aom/aom_codec.h>
#include <aom/aom_encoder.h>
#include <aom/aomcx.h>

G_BEGIN_DECLS
#define GST_TYPE_AV1_ENC \
  (gst_av1_enc_get_type())
#define GST_AV1_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AV1_ENC,GstAV1Enc))
#define GST_AV1_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AV1_ENC,GstAV1EncClass))
#define GST_IS_AV1_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AV1_ENC))
#define GST_IS_AV1_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AV1_ENC))
#define GST_AV1_ENC_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_AV1_ENC, GstAV1EncClass))
#define GST_AV1_ENC_CAST(obj) \
  ((GstAV1Enc *) (obj))

typedef struct _GstAV1Enc GstAV1Enc;
typedef struct _GstAV1EncClass GstAV1EncClass;

/**
 * GstAV1EncResizeMode:
 * @GST_AV1_ENC_RESIZE_NONE: No frame resizing allowed
 * @GST_AV1_ENC_RESIZE_FIXED: All frames are coded at the specified scale
 * @GST_AV1_ENC_RESIZE_RANDOM: All frames are coded at a random scale
 *
 * Frame resize mode
 */
typedef enum
{
  GST_AV1_ENC_RESIZE_NONE = 0,
  GST_AV1_ENC_RESIZE_FIXED = 1,
  GST_AV1_ENC_RESIZE_RANDOM = 2,
  GST_AV1_ENC_RESIZE_MODES
} GstAV1EncResizeMode;

/**
 * GstAV1EncSuperresMode
 * @GST_AV1_ENC_SUPERRES_NONE: No frame superres allowed
 * @GST_AV1_ENC_SUPERRES_FIXED: All frames are coded at the specified scale and
 *   super-resolved
 * @GST_AV1_ENC_SUPERRES_QTHRESH: Superres scale for a frame is determined based
 *   on q_index
 *
 * Frame super-resolution mode
 */
typedef enum
{
  GST_AV1_ENC_SUPERRES_NONE = 0,
  GST_AV1_ENC_SUPERRES_FIXED = 1,
  GST_AV1_ENC_SUPERRES_RANDOM = 2,
  GST_AV1_ENC_SUPERRES_QTHRESH = 3,
  GST_AV1_ENC_SUPERRES_MODES
} GstAV1EncSuperresMode;

/**
 * GstAV1EncEndUsageMode
 * @GST_AV1_ENC_END_USAGE_VBR: Variable Bit Rate Mode
 * @GST_AV1_ENC_END_USAGE_CBR: Constant Bit Rate Mode
 * @GST_AV1_ENC_END_USAGE_CQ: Constrained Quality Mode
 * @GST_AV1_ENC_END_USAGE_Q: Constant Quality Mode
 *
 * Rate control algorithm to use
 */

typedef enum
{
  GST_AV1_ENC_END_USAGE_VBR = 0,
  GST_AV1_ENC_END_USAGE_CBR = 1,
  GST_AV1_ENC_END_USAGE_CQ = 2,
  GST_AV1_ENC_END_USAGE_Q = 3,
  GST_AV1_ENC_END_USAGE_MODES
} GstAV1EncEndUsageMode;

struct _GstAV1Enc
{
  GstVideoEncoder base_video_encoder;

  /* properties */
  guint keyframe_dist;
  gint cpu_used;

  /* state */
  gboolean encoder_inited;
  GstVideoCodecState *input_state;
  aom_codec_enc_cfg_t aom_cfg;
  aom_codec_ctx_t encoder;
  aom_img_fmt_t format;
  GMutex encoder_lock;

  gboolean target_bitrate_set;
};

struct _GstAV1EncClass
{
  GstVideoEncoderClass parent_class;
  /*supported aom algo*/
  aom_codec_iface_t *codec_algo;
};

GType gst_av1_enc_get_type (void);

G_END_DECLS
#endif /* __GST_AV1_ENC_H__ */
