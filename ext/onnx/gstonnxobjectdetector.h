/*
 * GStreamer gstreamer-onnxobjectdetector
 * Copyright (C) 2021 Collabora Ltd
 *
 * gstonnxobjectdetector.h
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

#ifndef __GST_ONNX_OBJECT_DETECTOR_H__
#define __GST_ONNX_OBJECT_DETECTOR_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>
#include "gstonnxelement.h"

G_BEGIN_DECLS

#define GST_TYPE_ONNX_OBJECT_DETECTOR            (gst_onnx_object_detector_get_type())
G_DECLARE_FINAL_TYPE (GstOnnxObjectDetector, gst_onnx_object_detector, GST, ONNX_OBJECT_DETECTOR, GstBaseTransform)
#define GST_ONNX_OBJECT_DETECTOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ONNX_OBJECT_DETECTOR,GstOnnxObjectDetector))
#define GST_ONNX_OBJECT_DETECTOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_ONNX_OBJECT_DETECTOR,GstOnnxObjectDetectorClass))
#define GST_ONNX_OBJECT_DETECTOR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_ONNX_OBJECT_DETECTOR,GstOnnxObjectDetectorClass))
#define GST_IS_ONNX_OBJECT_DETECTOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ONNX_OBJECT_DETECTOR))
#define GST_IS_ONNX_OBJECT_DETECTOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_ONNX_OBJECT_DETECTOR))

#define GST_ONNX_OBJECT_DETECTOR_META_NAME "onnx-object_detector"
#define GST_ONNX_OBJECT_DETECTOR_META_PARAM_NAME "extra-data"
#define GST_ONNX_OBJECT_DETECTOR_META_FIELD_LABEL "label"
#define GST_ONNX_OBJECT_DETECTOR_META_FIELD_SCORE "score"

/**
 * GstOnnxObjectDetector:
 *
 * @model_file model file
 * @label_file label file
 * @score_threshold score threshold
 * @confidence_threshold confidence threshold
 * @iou_threhsold iou threshold
 * @optimization_level ONNX optimization level
 * @execution_provider: ONNX execution provider
 * @onnx_ptr opaque pointer to ONNX implementation
 *
 * Since: 1.20
 */
struct _GstOnnxObjectDetector
{
  GstBaseTransform basetransform;
  gchar *model_file;
  gchar *label_file;
  gfloat score_threshold;
  gfloat confidence_threshold;
  gfloat iou_threshold;
  GstOnnxOptimizationLevel optimization_level;
  GstOnnxExecutionProvider execution_provider;
  gpointer onnx_ptr;
  gboolean onnx_disabled;

  void (*process) (GstOnnxObjectDetector * onnx_object_detector,
      GstVideoFrame * inframe, GstVideoFrame * outframe);
};

/**
 * GstOnnxObjectDetectorClass:
 *
 * @parent_class base transform base class
 *
 * Since: 1.20
 */
struct _GstOnnxObjectDetectorClass
{
  GstBaseTransformClass parent_class;
};

GST_ELEMENT_REGISTER_DECLARE (onnx_object_detector)

G_END_DECLS

#endif /* __GST_ONNX_OBJECT_DETECTOR_H__ */
