/*
 * Copyright (C) 2014 SUMOMO Computer Association.
 *     Author: ayaka <ayaka@soulik.info>
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

#ifndef __GST_V4L2_VIDEO_ENC_H__
#define __GST_V4L2_VIDEO_ENC_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideoencoder.h>
#include <gst/video/gstvideometa.h>

#include <gstv4l2object.h>
#include <gstv4l2bufferpool.h>
#include <gstv4l2codec.h>

G_BEGIN_DECLS
#define GST_TYPE_V4L2_VIDEO_ENC \
  (gst_v4l2_video_enc_get_type())
#define GST_V4L2_VIDEO_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_V4L2_VIDEO_ENC,GstV4l2VideoEnc))
#define GST_V4L2_VIDEO_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_V4L2_VIDEO_ENC,GstV4l2VideoEncClass))
#define GST_IS_V4L2_VIDEO_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_V4L2_VIDEO_ENC))
#define GST_IS_V4L2_VIDEO_ENC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_V4L2_VIDEO_ENC))
#define GST_V4L2_VIDEO_ENC_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_V4L2_VIDEO_ENC, GstV4l2VideoEncClass))

typedef struct _GstV4l2VideoEnc GstV4l2VideoEnc;
typedef struct _GstV4l2VideoEncClass GstV4l2VideoEncClass;

struct _GstV4l2VideoEnc
{
  GstVideoEncoder parent;

  /* < private > */
  GstV4l2Object *v4l2output;
  GstV4l2Object *v4l2capture;

  /* pads */
  GstCaps *probed_srccaps;
  GstCaps *probed_sinkcaps;

  /* State */
  GstVideoCodecState *input_state;
  gboolean active;
  GstFlowReturn output_flow;

};

struct _GstV4l2VideoEncClass
{
  GstVideoEncoderClass parent_class;

  gchar *default_device;
  const char *codec_name;
  const GstV4l2Codec *codec;
};

GType gst_v4l2_video_enc_get_type (void);


gboolean gst_v4l2_is_video_enc (GstCaps * sink_caps, GstCaps * src_caps,
    GstCaps * codec_caps);

void gst_v4l2_video_enc_register (GstPlugin * plugin, GType type,
    const char *codec_name, const gchar * basename, const gchar * device_path,
    const GstV4l2Codec * codec, gint video_fd,
    GstCaps * sink_caps, GstCaps *codec_caps, GstCaps * src_caps);

G_END_DECLS
#endif /* __GST_V4L2_VIDEO_ENC_H__ */
