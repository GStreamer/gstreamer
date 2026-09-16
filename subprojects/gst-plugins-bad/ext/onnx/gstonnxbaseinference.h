/* GStreamer
 * Copyright (C) 2026 Seungha Yang <seungha@centricular.com>
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

#pragma once

#include <gst/base/gstbasetransform.h>
#include <gst/analytics/analytics.h>
#include <onnxruntime_c_api.h>
#include "gstonnximporter.h"

G_BEGIN_DECLS

typedef struct _GstOnnxBaseInference GstOnnxBaseInference;
typedef struct _GstOnnxBaseInferenceClass GstOnnxBaseInferenceClass;
typedef struct _GstOnnxBaseInferencePrivate GstOnnxBaseInferencePrivate;

#define GST_TYPE_ONNX_BASE_INFERENCE            (gst_onnx_base_inference_get_type ())
#define GST_ONNX_BASE_INFERENCE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_ONNX_BASE_INFERENCE, GstOnnxBaseInference))
#define GST_ONNX_BASE_INFERENCE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_ONNX_BASE_INFERENCE, GstOnnxBaseInferenceClass))
#define GST_IS_ONNX_BASE_INFERENCE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_ONNX_BASE_INFERENCE))
#define GST_IS_ONNX_BASE_INFERENCE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_ONNX_BASE_INFERENCE))
#define GST_ONNX_BASE_INFERENCE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_ONNX_BASE_INFERENCE, GstOnnxBaseInferenceClass))

#define GST_ONNX_MAX_DIMS 4

typedef struct
{
  gchar *onnx_name;
  gchar *modelinfo_name;
  GQuark id;
  GstTensorDataType data_type;
  ONNXTensorElementDataType data_type_onnx;
  GstTensorDimOrder dims_order;
  gsize num_dims;
  gint64 dims[GST_ONNX_MAX_DIMS];
} GstOnnxModelInput;

typedef struct _GstOnnxModelInfo
{
  GQuark group_id;
  const GArray *inputs;
  const GstStructure *output_tensorgroups;
} GstOnnxModelInfo;

typedef enum
{
  GST_ONNX_EXECUTION_PROVIDER_CPU,
  GST_ONNX_EXECUTION_PROVIDER_CUDA,
  GST_ONNX_EXECUTION_PROVIDER_VSI,
  GST_ONNX_EXECUTION_PROVIDER_MIGRAPHX,
  GST_ONNX_EXECUTION_PROVIDER_HIP,
  GST_ONNX_EXECUTION_PROVIDER_DIRECTML,
  GST_ONNX_EXECUTION_PROVIDER_VITIS_AI,
  GST_ONNX_EXECUTION_PROVIDER_COREML,
} GstOnnxExecutionProvider;

struct _GstOnnxBaseInference
{
  GstBaseTransform parent;

  const OrtApi *api;
  GstOnnxExecutionProvider execution_provider;

  GstOnnxBaseInferencePrivate *priv;
};

struct _GstOnnxBaseInferenceClass
{
  GstBaseTransformClass parent_class;

  GstOnnxImporterProcType proc_type;

  void (*reset) (GstOnnxBaseInference * base);

  gboolean (*process_model_info) (GstOnnxBaseInference * base,
                                  GstAnalyticsModelInfo * modelinfo,
                                  const GstOnnxModelInfo * parsed_info,
                                  GstOnnxImporter * importer);

  GstFlowReturn (*start_inference) (GstOnnxBaseInference * base,
                                    GstBuffer * buffer,
                                    const GArray * model_inputs,
                                    gsize * num_iterations);

  GstFlowReturn (*prepare_input_tensors) (GstOnnxBaseInference * base,
                                          GstBuffer * buffer,
                                          const GArray * model_inputs,
                                          gsize iteration,
                                          GPtrArray * input_tensors);

  void (*unprepare_input_tensors) (GstOnnxBaseInference * base,
                                   GstBuffer * buffer,
                                   GPtrArray * input_tensors);

  GstFlowReturn (*end_inference) (GstOnnxBaseInference * base,
                                  GstBuffer * buffer,
                                  GPtrArray * results,
                                  gsize num_tensors,
                                  gboolean attach_meta);
};

GType gst_onnx_base_inference_get_type (void);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstOnnxBaseInference, gst_object_unref)

G_END_DECLS
