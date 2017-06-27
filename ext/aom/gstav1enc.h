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

typedef struct _GstAV1Enc GstAV1Enc;
typedef struct _GstAV1EncClass GstAV1EncClass;

struct _GstAV1Enc
{
  GstVideoEncoder encoder;

  guint keyframe_dist;

  GstVideoCodecState *input_state;

  aom_codec_iface_t *codec_interface;
  aom_codec_enc_cfg_t aom_cfg;
  aom_codec_ctx_t codec;
};

struct _GstAV1EncClass
{
  GstVideoEncoderClass parent_class;
};

GType gst_av1_enc_get_type (void);

G_END_DECLS
#endif /* __GST_AV1_ENC_H__ */
