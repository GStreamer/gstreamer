/* GStreamer NVENC plugin
 * Copyright (C) 2015 Centricular Ltd
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

#ifndef __GST_NV_BASE_ENC_H_INCLUDED__
#define __GST_NV_BASE_ENC_H_INCLUDED__

#include "gstnvenc.h"

#include <gst/video/gstvideoencoder.h>

#define GST_TYPE_NV_BASE_ENC \
  (gst_nv_base_enc_get_type())
#define GST_NV_BASE_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_NV_BASE_ENC,GstNvBaseEnc))
#define GST_NV_BASE_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_NV_BASE_ENC,GstNvBaseEncClass))
#define GST_NV_BASE_ENC_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_NV_BASE_ENC,GstNvBaseEncClass))
#define GST_IS_NV_BASE_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_NV_BASE_ENC))
#define GST_IS_NV_BASE_ENC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_NV_BASE_ENC))

typedef enum {
  GST_NV_PRESET_DEFAULT,
  GST_NV_PRESET_HP,
  GST_NV_PRESET_HQ,
/* FIXME: problematic GST_NV_PRESET_BD, */
  GST_NV_PRESET_LOW_LATENCY_DEFAULT,
  GST_NV_PRESET_LOW_LATENCY_HQ,
  GST_NV_PRESET_LOW_LATENCY_HP,
  GST_NV_PRESET_LOSSLESS_DEFAULT,
  GST_NV_PRESET_LOSSLESS_HP,
} GstNvPreset;

typedef enum {
  GST_NV_RC_MODE_DEFAULT,
  GST_NV_RC_MODE_CONSTQP,
  GST_NV_RC_MODE_CBR,
  GST_NV_RC_MODE_VBR,
  GST_NV_RC_MODE_VBR_MINQP,
} GstNvRCMode;

typedef struct {
  GstVideoEncoder video_encoder;

  /* properties */
  guint           cuda_device_id;
  GstNvPreset     preset_enum;
  GUID            selected_preset;
  GstNvRCMode     rate_control_mode;
  gint            qp_min;
  gint            qp_max;
  gint            qp_const;
  guint           bitrate;
  gint            gop_size;

  CUcontext       cuda_ctx;
  void          * encoder;

  /* the supported input formats */
  GValue        * input_formats;                  /* OBJECT LOCK */

  GstVideoCodecState *input_state;
  volatile gint       reconfig;                   /* ATOMIC */
  gboolean            gl_input;

  /* allocated buffers */
  gpointer          *input_bufs;   /* array of n_allocs input buffers  */
  NV_ENC_OUTPUT_PTR *output_bufs;  /* array of n_allocs output buffers */
  guint              n_bufs;

  /* input and output buffers currently available */
  GAsyncQueue    *in_bufs_pool;
  GAsyncQueue    *bitstream_pool;

  /* output bufs in use (input bufs in use are tracked via the codec frames) */
  GAsyncQueue    *bitstream_queue;

  /* we spawn a thread that does the (blocking) waits for output buffers
   * to become available, so we can continue to feed data to the encoder
   * while we wait */
  GThread        *bitstream_thread;

  /* supported interlacing input modes.
   * 0 = none, 1 = fields, 2 = interleaved */
  gint            interlace_modes;

  void           *display;            /* GstGLDisplay */
  void           *other_context;      /* GstGLContext */

  /* the maximum buffer size the encoder is configured for */
  guint               max_encode_width;
  guint               max_encode_height;

  GstVideoInfo        input_info;     /* buffer configuration for buffers sent to NVENC */

  GstFlowReturn   last_flow;          /* ATOMIC */
} GstNvBaseEnc;

typedef struct {
  GstVideoEncoderClass video_encoder_class;

  GUID codec_id;

  gboolean (*set_src_caps)       (GstNvBaseEnc * nvenc,
                                  GstVideoCodecState * state);
  gboolean (*set_pic_params)     (GstNvBaseEnc * nvenc,
                                  GstVideoCodecFrame * frame,
                                  NV_ENC_PIC_PARAMS * pic_params);
  gboolean (*set_encoder_config) (GstNvBaseEnc * nvenc,
                                  GstVideoCodecState * state,
                                  NV_ENC_CONFIG * config);
} GstNvBaseEncClass;

G_GNUC_INTERNAL
GType gst_nv_base_enc_get_type (void);


void gst_nv_base_enc_get_max_encode_size      (GstNvBaseEnc * nvenc,
                                               guint * max_width,
                                               guint * max_height);
void gst_nv_base_enc_set_max_encode_size      (GstNvBaseEnc * nvenc,
                                               guint max_width,
                                               guint max_height);

#endif /* __GST_NV_BASE_ENC_H_INCLUDED__ */
