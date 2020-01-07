/* GStreamer
 * Copyright (C) 2020 Seungha Yang <seungha.yang@navercorp.com>
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
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

#ifndef __GST_MF_VIDEO_ENC_H__
#define __GST_MF_VIDEO_ENC_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include "gstmfutils.h"
#include "gstmftransform.h"

G_BEGIN_DECLS

#define GST_TYPE_MF_VIDEO_ENC           (gst_mf_video_enc_get_type())
#define GST_MF_VIDEO_ENC(obj)           (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MF_VIDEO_ENC,GstMFVideoEnc))
#define GST_MF_VIDEO_ENC_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_MF_VIDEO_ENC,GstMFVideoEncClass))
#define GST_MF_VIDEO_ENC_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_MF_VIDEO_ENC,GstMFVideoEncClass))
#define GST_IS_MF_VIDEO_ENC(obj)        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MF_VIDEO_ENC))
#define GST_IS_MF_VIDEO_ENC_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_MF_VIDEO_ENC))

typedef struct _GstMFVideoEnc GstMFVideoEnc;
typedef struct _GstMFVideoEncClass GstMFVideoEncClass;

struct _GstMFVideoEnc
{
  GstVideoEncoder parent;

  GstMFTransform *transform;

  GstVideoCodecState *input_state;
};

struct _GstMFVideoEncClass
{
  GstVideoEncoderClass parent_class;

  GUID codec_id;
  guint32 enum_flags;
  guint device_index;
  gboolean can_force_keyframe;

  gboolean (*set_option)    (GstMFVideoEnc * mfenc,
                             IMFMediaType * output_type);

  gboolean (*set_src_caps)  (GstMFVideoEnc * mfenc,
                             GstVideoCodecState * state,
                             IMFMediaType * output_type);
};

GType gst_mf_video_enc_get_type (void);

G_END_DECLS

#endif /* __GST_MF_VIDEO_ENC_H__ */