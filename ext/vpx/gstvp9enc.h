/* VP9
 * Copyright (C) 2006 David Schleef <ds@schleef.org>
 * Copyright (C) 2010 Entropy Wave Inc
 * Copyright (C) 2010-2013 Sebastian Dröge <slomo@circular-chaos.org>
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
 *
 */
#ifndef __GST_VP9_ENC_H__
#define __GST_VP9_ENC_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_VP9_ENCODER

#include <gst/gst.h>
#include <gst/video/gstvideoencoder.h>

/* FIXME: Undef HAVE_CONFIG_H because vpx_codec.h uses it,
 * which causes compilation failures */
#ifdef HAVE_CONFIG_H
#undef HAVE_CONFIG_H
#endif

#include <vpx/vpx_encoder.h>
#include <vpx/vp8cx.h>

G_BEGIN_DECLS

#define GST_TYPE_VP9_ENC \
  (gst_vp9_enc_get_type())
#define GST_VP9_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VP9_ENC,GstVP9Enc))
#define GST_VP9_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VP9_ENC,GstVP9EncClass))
#define GST_IS_VP9_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VP9_ENC))
#define GST_IS_VP9_ENC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VP9_ENC))

typedef struct _GstVP9Enc GstVP9Enc;
typedef struct _GstVP9EncClass GstVP9EncClass;

struct _GstVP9Enc
{
  GstVideoEncoder base_video_encoder;

  /* < private > */
  vpx_codec_ctx_t encoder;
  GMutex encoder_lock;

  /* properties */
  vpx_codec_enc_cfg_t cfg;
  gboolean have_default_config;
  gboolean rc_target_bitrate_set;
  gint n_ts_target_bitrate;
  gint n_ts_rate_decimator;
  gint n_ts_layer_id;
  /* Global two-pass options */
  gchar *multipass_cache_file;
  GByteArray *first_pass_cache_content;

  /* Encode parameter */
  gint64 deadline;

  /* Controls */
  VPX_SCALING_MODE h_scaling_mode;
  VPX_SCALING_MODE v_scaling_mode;
  int cpu_used;
  gboolean enable_auto_alt_ref;
  unsigned int noise_sensitivity;
  unsigned int sharpness;
  unsigned int static_threshold;
  vp8e_token_partitions token_partitions;
  unsigned int arnr_maxframes;
  unsigned int arnr_strength;
  unsigned int arnr_type;
  vp8e_tuning tuning;
  unsigned int cq_level;
  unsigned int max_intra_bitrate_pct;
  /* Timebase - a value of 0 will use the framerate */
  unsigned int timebase_n;
  unsigned int timebase_d;

  /* state */
  gboolean inited;

  vpx_image_t image;

  int n_frames;

  GstVideoCodecState *input_state;
};

struct _GstVP9EncClass
{
  GstVideoEncoderClass base_video_encoder_class;
};

GType gst_vp9_enc_get_type (void);

G_END_DECLS

#endif

#endif /* __GST_VP9_ENC_H__ */
