/* VP8
 * Copyright (C) 2006 David Schleef <ds@schleef.org>
 * Copyright (C) 2010 Entropy Wave Inc
 * Copyright (C) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */
#ifndef __GST_VP8_ENC_H__
#define __GST_VP8_ENC_H__

#include <gst/gst.h>
#include <gst/video/gstbasevideoencoder.h>

/* FIXME: Undef HAVE_CONFIG_H because vpx_codec.h uses it,
 * which causes compilation failures */
#ifdef HAVE_CONFIG_H
#undef HAVE_CONFIG_H
#endif

#include <vpx/vpx_encoder.h>
#include <vpx/vp8cx.h>

G_BEGIN_DECLS

#define GST_TYPE_VP8_ENC \
  (gst_vp8_enc_get_type())
#define GST_VP8_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VP8_ENC,GstVP8Enc))
#define GST_VP8_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VP8_ENC,GstVP8EncClass))
#define GST_IS_VP8_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VP8_ENC))
#define GST_IS_VP8_ENC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VP8_ENC))

typedef struct _GstVP8Enc GstVP8Enc;
typedef struct _GstVP8EncClass GstVP8EncClass;

struct _GstVP8Enc
{
  GstBaseVideoEncoder base_video_encoder;

  /* < private > */
  vpx_codec_ctx_t encoder;

  /* properties */
  int bitrate;
  enum vpx_rc_mode mode;
  unsigned int minsection_pct;
  unsigned int maxsection_pct;
  int min_quantizer;
  int max_quantizer;
  double quality;
  gboolean error_resilient;
  int max_latency;
  int max_keyframe_distance;
  int speed;
  int threads;
  enum vpx_enc_pass multipass_mode;
  gchar *multipass_cache_file;
  GByteArray *first_pass_cache_content;
  vpx_fixed_buf_t last_pass_cache_content;
  gboolean auto_alt_ref_frames;
  unsigned int lag_in_frames;
  int sharpness;
  int noise_sensitivity;
#ifdef HAVE_VP8ENC_TUNING
  vp8e_tuning tuning;
#endif
  int static_threshold;
  gboolean drop_frame;
  gboolean resize_allowed;
  gboolean partitions;

  /* state */
  gboolean inited;

  vpx_image_t image;

  int n_frames;
  int keyframe_distance;
};

struct _GstVP8EncClass
{
  GstBaseVideoEncoderClass base_video_encoder_class;
};

GType gst_vp8_enc_get_type (void);

G_END_DECLS

#endif /* __GST_VP8_ENC_H__ */
