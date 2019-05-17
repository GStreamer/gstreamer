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

#ifndef __GST_NV_H264_ENC_H_INCLUDED__
#define __GST_NV_H264_ENC_H_INCLUDED__

#include "gstnvbaseenc.h"

#define GST_TYPE_NV_H264_ENC \
  (gst_nv_h264_enc_get_type())
#define GST_NV_H264_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_NV_H264_ENC,GstNvH264Enc))
#define GST_NV_H264_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_NV_H264_ENC,GstNvH264EncClass))
#define GST_NV_H264_ENC_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_NV_H264_ENC,GstNvH264EncClass))
#define GST_IS_NV_H264_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_NV_H264_ENC))
#define GST_IS_NV_H264_ENC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_NV_H264_ENC))

typedef struct {
  GstNvBaseEnc base_nvenc;

  /* the supported input formats */
  GValue        * supported_profiles;             /* OBJECT LOCK */

  GstVideoCodecState *input_state;
  gboolean            gl_input;

  /* supported interlacing input modes.
   * 0 = none, 1 = fields, 2 = interleaved */
  gint            interlace_modes;
} GstNvH264Enc;

typedef struct {
  GstNvBaseEncClass video_encoder_class;
} GstNvH264EncClass;

G_GNUC_INTERNAL
GType gst_nv_h264_enc_get_type (void);

#endif /* __GST_NV_H264_ENC_H_INCLUDED__ */
