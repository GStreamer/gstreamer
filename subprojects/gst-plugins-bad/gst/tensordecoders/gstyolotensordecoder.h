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


#ifndef __GST_YOLO_TENSOR_DECODER_H__
#define __GST_YOLO_TENSOR_DECODER_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/base/base.h>
#include <gst/analytics/analytics.h>

GType gst_yolo_tensor_decoder_get_type (void);

#define GST_TYPE_YOLO_TENSOR_DECODER (gst_yolo_tensor_decoder_get_type ())
#define GST_YOLO_TENSOR_DECODER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_YOLO_TENSOR_DECODER, GstYoloTensorDecoder))
#define GST_YOLO_TENSOR_DECODER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_YOLO_TENSOR_DECODER, GstYoloTensorDecoderClass))
#define GST_IS_YOLO_TENSOR_DECODER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_YOLO_TENSOR_DECODER))
#define GST_IS_YOLO_TENSOR_DECODER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_YOLO_TENSOR_DECODER))
#define GST_YOLO_TENSOR_DECODER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_YOLO_TENSOR_DECODER, GstYoloTensorDecoderClass))

typedef struct _GstYoloTensorDecoder GstYoloTensorDecoder;
typedef struct _GstYoloTensorDecoderClass GstYoloTensorDecoderClass;

typedef struct _BBox
{
  gint x;
  gint y;
  guint w;
  guint h;
} BBox;

struct _GstYoloTensorDecoder
{
  GstBaseTransform basetransform;
  /* Box confidence threshold */
  gfloat box_confi_thresh;
  /* Class confidence threshold */
  gfloat cls_confi_thresh;
  /* Intersection-of-Union threshold */
  gfloat iou_thresh;
  /* Maximum detection/mask */
  gsize max_detection;
  /* Candidates with a class confidence level above threshold. */
  GArray *sel_candidates;
  /* Final candidates selected that respect class confidence level,
   * NMS and maximum detection. */
  GPtrArray *selected;
  /* Video Info */
  GstVideoInfo video_info;
  /* Labels file */
  gchar *label_file;
  /* Labels */
  GArray *labels;
};

struct _GstYoloTensorDecoderClass
{
  GstBaseTransformClass parent_class;

  void (*object_found) (GstYoloTensorDecoder *self,
      GstAnalyticsRelationMeta *rmeta, BBox *bb, gfloat confidence,
      GQuark class_quark, const gfloat *candidate_masks, gsize offset,
      guint count);
};

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GstYoloTensorDecoder, g_object_unref)

GST_ELEMENT_REGISTER_DECLARE (yolo_tensor_decoder)

gboolean
gst_yolo_tensor_decoder_decode_f32 (GstYoloTensorDecoder * self,
    GstAnalyticsRelationMeta * rmeta, const GstTensor * detections_tensor,
    guint num_masks);

#endif /* __GST_YOLO_TENSOR_DECODER_H__ */
