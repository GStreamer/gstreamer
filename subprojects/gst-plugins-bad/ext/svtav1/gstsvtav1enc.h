/*
* Copyright(c) 2019 Intel Corporation
*     Authors: Jun Tian <jun.tian@intel.com> Xavier Hallade <xavier.hallade@intel.com>
* SPDX - License - Identifier: LGPL-2.1-or-later
*/

#ifndef _GST_SVTAV1ENC_H_
#define _GST_SVTAV1ENC_H_

#include <string.h>
#include <gst/video/video.h>
#include <gst/video/gstvideoencoder.h>

#include <EbSvtAv1.h>
#include <EbSvtAv1Enc.h>


G_BEGIN_DECLS
#define GST_TYPE_SVTAV1ENC \
  (gst_svtav1enc_get_type())
#define GST_SVTAV1ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SVTAV1ENC,GstSvtAv1Enc))
#define GST_SVTAV1ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SVTAV1ENC,GstSvtHevcEncClass))
#define GST_IS_SVTAV1ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SVTAV1ENC))
#define GST_IS_SVTAV1ENC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SVTAV1ENC))

typedef struct _GstSvtAv1Enc
{
  GstVideoEncoder video_encoder;

  /* SVT-AV1 Encoder Handle */
  EbComponentType *svt_encoder;

  /* GStreamer Codec state */
  GstVideoCodecState *state;

  /* SVT-AV1 configuration */
  EbSvtAv1EncConfiguration *svt_config;

  EbBufferHeaderType *input_buf;

  long long int frame_count;
  int dts_offset;
} GstSvtAv1Enc;

typedef struct _GstSvtAv1EncClass
{
  GstVideoEncoderClass video_encoder_class;
} GstSvtAv1EncClass;

GType gst_svtav1enc_get_type (void);

G_END_DECLS
#endif
