/*
 * GStreamer gstreamer-onnxelement
 * Copyright (C) 2021 Collabora Ltd
 *
 * gstonnxelement.h
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

#ifndef __GST_ONNX_ELEMENT_H__
#define __GST_ONNX_ELEMENT_H__

#include <gst/gst.h>

typedef enum
{
  GST_ONNX_OPTIMIZATION_LEVEL_DISABLE_ALL,
  GST_ONNX_OPTIMIZATION_LEVEL_ENABLE_BASIC,
  GST_ONNX_OPTIMIZATION_LEVEL_ENABLE_EXTENDED,
  GST_ONNX_OPTIMIZATION_LEVEL_ENABLE_ALL,
} GstOnnxOptimizationLevel;

typedef enum
{
  GST_ONNX_EXECUTION_PROVIDER_CPU,
  GST_ONNX_EXECUTION_PROVIDER_CUDA,
} GstOnnxExecutionProvider;

typedef enum {
  /* Height Width Channel (a.k.a. interleaved) format */
  GST_ML_MODEL_INPUT_IMAGE_FORMAT_HWC,

  /* Channel Height Width  (a.k.a. planar) format */
  GST_ML_MODEL_INPUT_IMAGE_FORMAT_CHW,
} GstMlModelInputImageFormat;


G_BEGIN_DECLS

GType gst_onnx_optimization_level_get_type (void);
#define GST_TYPE_ONNX_OPTIMIZATION_LEVEL (gst_onnx_optimization_level_get_type ())

GType gst_onnx_execution_provider_get_type (void);
#define GST_TYPE_ONNX_EXECUTION_PROVIDER (gst_onnx_execution_provider_get_type ())

GType gst_ml_model_input_image_format_get_type (void);
#define GST_TYPE_ML_MODEL_INPUT_IMAGE_FORMAT (gst_ml_model_input_image_format_get_type ())

G_END_DECLS

#endif
