/*
 * GStreamer gstreamer-onnxelement
 * Copyright (C) 2021 Collabora Ltd
 *
 * gstonnxelement.c
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstonnxelement.h"

GType
gst_onnx_optimization_level_get_type (void)
{
  static GType onnx_optimization_type = 0;

  if (g_once_init_enter (&onnx_optimization_type)) {
    static GEnumValue optimization_level_types[] = {
      {GST_ONNX_OPTIMIZATION_LEVEL_DISABLE_ALL, "Disable all optimization",
          "disable-all"},
      {GST_ONNX_OPTIMIZATION_LEVEL_ENABLE_BASIC,
            "Enable basic optimizations (redundant node removals))",
          "enable-basic"},
      {GST_ONNX_OPTIMIZATION_LEVEL_ENABLE_EXTENDED,
            "Enable extended optimizations (redundant node removals + node fusions)",
          "enable-extended"},
      {GST_ONNX_OPTIMIZATION_LEVEL_ENABLE_ALL,
          "Enable all possible optimizations", "enable-all"},
      {0, NULL, NULL},
    };

    GType temp = g_enum_register_static ("GstOnnxOptimizationLevel",
        optimization_level_types);

    g_once_init_leave (&onnx_optimization_type, temp);
  }

  return onnx_optimization_type;
}

GType
gst_onnx_execution_provider_get_type (void)
{
  static GType onnx_execution_type = 0;

  if (g_once_init_enter (&onnx_execution_type)) {
    static GEnumValue execution_provider_types[] = {
      {GST_ONNX_EXECUTION_PROVIDER_CPU, "CPU execution provider",
          "cpu"},
      {GST_ONNX_EXECUTION_PROVIDER_CUDA,
            "CUDA execution provider",
          "cuda"},
      {0, NULL, NULL},
    };

    GType temp = g_enum_register_static ("GstOnnxExecutionProvider",
        execution_provider_types);

    g_once_init_leave (&onnx_execution_type, temp);
  }

  return onnx_execution_type;
}

GType
gst_ml_model_input_image_format_get_type (void)
{
  static GType ml_model_input_image_format = 0;

  if (g_once_init_enter (&ml_model_input_image_format)) {
    static GEnumValue ml_model_input_image_format_types[] = {
      {GST_ML_MODEL_INPUT_IMAGE_FORMAT_HWC,
            "Height Width Channel (HWC) a.k.a. interleaved image data format",
          "hwc"},
      {GST_ML_MODEL_INPUT_IMAGE_FORMAT_CHW,
            "Channel Height Width (CHW) a.k.a. planar image data format",
          "chw"},
      {0, NULL, NULL},
    };

    GType temp = g_enum_register_static ("GstMlModelInputImageFormat",
        ml_model_input_image_format_types);

    g_once_init_leave (&ml_model_input_image_format, temp);
  }

  return ml_model_input_image_format;
}
