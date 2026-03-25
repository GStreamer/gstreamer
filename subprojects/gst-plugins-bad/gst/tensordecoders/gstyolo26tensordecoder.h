/*
 * GStreamer gstreamer-yolo26tensordecoder
 * Copyright (C) 2026 Collabora Ltd.
 *
 * gstyolo26tensordecoder.h
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

#ifndef __GST_YOLO26_TENSOR_DECODER_H__
#define __GST_YOLO26_TENSOR_DECODER_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/base/base.h>

G_BEGIN_DECLS

#define GST_TYPE_YOLO26_TENSOR_DECODER            (gst_yolo26_tensor_decoder_get_type())
G_DECLARE_FINAL_TYPE (GstYolo26TensorDecoder,
    gst_yolo26_tensor_decoder, GST, YOLO26_TENSOR_DECODER, GstBaseTransform)

/**
 * GstYolo26TensorDecoder:
 *
 * Since: 1.30
 */
struct _GstYolo26TensorDecoder
{
  GstBaseTransform basetransform;

  /* Confidence threshold for candidate filtering. */
  gfloat score_threshold;

  /* Video info from negotiated caps. */
  GstVideoInfo video_info;

  /* Optional labels file path and parsed labels. */
  gchar *label_file;
  GArray *labels;
};

/**
 * GstYolo26TensorDecoderClass:
 *
 * @parent_class base transform base class
 *
 * Since: 1.30
 */
struct _GstYolo26TensorDecoderClass
{
  GstBaseTransformClass parent_class;
};

GST_ELEMENT_REGISTER_DECLARE (yolo26_tensor_decoder)

G_END_DECLS

#endif /* __GST_YOLO26_TENSOR_DECODER_H__ */
