/*
 * GStreamer gstreamer-facedetectortensordecoder
 * Copyright (C) 2025 Collabora Ltd
 *
 * gstfacedetectortensordecoder.h
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

#ifndef __GST_FACE_DETECTOR_TENSOR_DECODER_H__
#define __GST_FACE_DETECTOR_TENSOR_DECODER_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/base/base.h>

G_BEGIN_DECLS
#define GST_TYPE_FACE_DETECTOR_TENSOR_DECODER            (gst_face_detector_tensor_decoder_get_type())
G_DECLARE_FINAL_TYPE (GstFaceDetectorTensorDecoder,
    gst_face_detector_tensor_decoder, GST, FACE_DETECTOR_TENSOR_DECODER,
    GstBaseTransform)

typedef struct
{
  guint16 index;
  gfloat *box;
  gfloat *score;
} Candidate;

/**
 * GstFaceDetectorTensorDecoder:
 *
 * Since: 1.28
 */
struct _GstFaceDetectorTensorDecoder
{
  GstBaseTransform basetransform;

  /* Confidence threshold. */
  gfloat score_threshold;

  /* Intersection-of-Union threshold. */
  gfloat iou_threshold;

  /* Video Info */
  GstVideoInfo video_info;

  /* Candidates with a class confidence level above threshold. */
  GPtrArray *sel_candidates;

  /* Final candidates selected that respect class confidence level,
  * NMS and maximum detection. */
  GPtrArray *selected;

  /* Candidates with a class confidence level and bounding boxes. */
  Candidate *candidates;
};

/**
 * GstFaceDetectorTensorDecoderClass:
 *
 * @parent_class base transform base class
 *
 * Since: 1.28
 */
struct _GstFaceDetectorTensorDecoderClass
{
  GstBaseTransformClass parent_class;
};

GST_ELEMENT_REGISTER_DECLARE (face_detector_tensor_decoder)
    G_END_DECLS
#endif /* __GST_FACE_DETECTOR_TENSOR_DECODER_H__ */
