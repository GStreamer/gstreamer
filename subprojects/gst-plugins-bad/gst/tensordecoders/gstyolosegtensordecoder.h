/*
 * GStreamer gstreamer-yolotensordecoder
 * Copyright (C) 2024 Collabora Ltd
 *  Authors: Daniel Morin <daniel.morin@collabora.com>
 *           Vineet Suryan <vineet.suryan@collabora.com>
 *           Santosh Mahto <santosh.mahto@collabora.com>
 *
 * gstyolotensordecoder.h
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


#ifndef __GST_YOLO_SEG_TENSOR_DECODER_H__
#define __GST_YOLO_SEG_TENSOR_DECODER_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/base/base.h>
#include "gstyolotensordecoder.h"

/* Yolo segmentation tensor decoder */
#define GST_TYPE_YOLO_SEG_TENSOR_DECODER (gst_yolo_seg_tensor_decoder_get_type ())

G_DECLARE_FINAL_TYPE (GstYoloSegTensorDecoder, gst_yolo_seg_tensor_decoder,
    GST, YOLO_SEG_TENSOR_DECODER, GstYoloTensorDecoder)

struct _GstYoloSegTensorDecoder
{
  GstYoloTensorDecoder parent;

  /* Mask width */
  guint mask_w;
  /* Mask height */
  guint mask_h;
  /* Mask length */
  gsize mask_length;

  /* Scaling factor to convert bounding-box coordinates to mask coordinates */
  gfloat bb2mask_gain;
  /* Region of the mask that contain valid segmentation information */
  BBox mask_roi;

  /* BufferPool for mask */
  GstBufferPool *mask_pool;

  /* Those are only valid during the call to
   * the base call gst_yolo_tensor_decoder_decode_f32
   */
  const GstTensor *logits_tensor;
  GstMapInfo map_info_logits;
};

GST_ELEMENT_REGISTER_DECLARE (yolo_seg_tensor_decoder)

#endif /* __GST_YOLO_SEG_TENSOR_DECODER_H__ */
