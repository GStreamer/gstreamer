/*
 * GStreamer gstreamer-onnxclient
 * Copyright (C) 2021-2023 Collabora Ltd
 *
 * gstonnxclient.cpp
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

#include "gstonnxclient.h"
#include <onnxruntime_c_api.h>

#ifdef HAVE_VSI_NPU
#include <core/providers/vsinpu/vsinpu_provider_factory.h>
#endif

#ifdef CPUPROVIDER_IN_SUBDIR
#include <core/providers/cpu/cpu_provider_factory.h>
#else
#include <cpu_provider_factory.h>
#endif

#include <sstream>
#include <cstring>

#define GST_CAT_DEFAULT onnx_inference_debug

/* FIXME: to be replaced by ModelInfo files */
#define GST_MODEL_OBJECT_DETECTOR_BOXES "ssd-mobilenet-v1-variant-1-out-boxes"
#define GST_MODEL_OBJECT_DETECTOR_SCORES "ssd-mobilenet-v1-variant-1-out-scores"
#define GST_MODEL_OBJECT_DETECTOR_NUM_DETECTIONS "generic-variant-1-out-count"
#define GST_MODEL_OBJECT_DETECTOR_CLASSES "ssd-mobilenet-v1-variant-1-out-classes"

namespace GstOnnxNamespace
{
  template < typename T >
      std::ostream & operator<< (std::ostream & os, const std::vector < T > &v)
  {
    os << "[";
    for (size_t i = 0; i < v.size (); ++i)
    {
      os << v[i];
      if (i != v.size () - 1)
      {
        os << ", ";
      }
    }
    os << "]";

    return os;
  }

const gint ONNX_TO_GST_TENSOR_DATATYPE[] = {
  -1,                                   /* ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED*/
  GST_TENSOR_DATA_TYPE_FLOAT32,         /* ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT*/
  GST_TENSOR_DATA_TYPE_UINT8,           /* ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8*/
  GST_TENSOR_DATA_TYPE_INT8,            /* ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8*/
  GST_TENSOR_DATA_TYPE_UINT16,          /* ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT16*/
  GST_TENSOR_DATA_TYPE_INT16,           /* ONNX_TENSOR_ELEMENT_DATA_TYPE_INT16*/
  GST_TENSOR_DATA_TYPE_INT32,           /* ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32*/
  GST_TENSOR_DATA_TYPE_INT64,           /* ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64*/
  GST_TENSOR_DATA_TYPE_STRING,          /* ONNX_TENSOR_ELEMENT_DATA_TYPE_STRING*/
  GST_TENSOR_DATA_TYPE_BOOL,            /* ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL*/
  GST_TENSOR_DATA_TYPE_FLOAT16,         /* ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16*/
  GST_TENSOR_DATA_TYPE_FLOAT64,         /* ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE*/
  GST_TENSOR_DATA_TYPE_UINT32,          /* ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT32*/
  GST_TENSOR_DATA_TYPE_UINT64,          /* ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT64*/
  GST_TENSOR_DATA_TYPE_COMPLEX64,       /* ONNX_TENSOR_ELEMENT_DATA_TYPE_COMPLEX64*/
  GST_TENSOR_DATA_TYPE_COMPLEX128,      /* ONNX_TENSOR_ELEMENT_DATA_TYPE_COMPLEX128*/
  GST_TENSOR_DATA_TYPE_BFLOAT16,        /* ONNX_TENSOR_ELEMENT_DATA_TYPE_BFLOAT16*/
  GST_TENSOR_DATA_TYPE_FLOAT8E4M3FN,    /* ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT8E4M3FN*/
  GST_TENSOR_DATA_TYPE_FLOAT8E4M3FNUZ,  /* ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT8E4M3FNUZ*/
  GST_TENSOR_DATA_TYPE_FLOAT8E5M2,      /* ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT8E5M2*/
  GST_TENSOR_DATA_TYPE_FLOAT8E5M2FNUZ,  /* ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT8E5M2FNUZ*/
  GST_TENSOR_DATA_TYPE_UINT4,           /* ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT4*/
  GST_TENSOR_DATA_TYPE_INT4,            /* ONNX_TENSOR_ELEMENT_DATA_TYPE_INT4*/
};

GstOnnxClient::GstOnnxClient (GstElement *debug_parent):
      api(nullptr),
      debug_parent(debug_parent),
      env(nullptr),
      session (nullptr),
      memory_info(nullptr),
      allocator(nullptr),
      width (0),
      height (0),
      channels (0),
      dest (nullptr),
      m_provider (GST_ONNX_EXECUTION_PROVIDER_CPU),
      inputImageFormat (GST_ML_INPUT_IMAGE_FORMAT_HWC),
      inputDatatype (GST_TENSOR_DATA_TYPE_UINT8),
      inputDatatypeSize (sizeof (uint8_t)),
      fixedInputImageSize (false),
      inputTensorOffset (0.0),
      inputTensorScale (1.0)
       {
    api = OrtGetApiBase()->GetApi(ORT_API_VERSION);
  }

  GstOnnxClient::~GstOnnxClient () {
    destroySession();
    delete[]dest;
  }

  int32_t GstOnnxClient::getWidth (void)
  {
    return width;
  }

  int32_t GstOnnxClient::getHeight (void)
  {
    return height;
  }

  int32_t GstOnnxClient::getChannels (void)
  {
    return channels;
  }

  bool GstOnnxClient::isFixedInputImageSize (void)
  {
    return fixedInputImageSize;
  }

  void GstOnnxClient::setInputImageFormat (GstMlInputImageFormat format)
  {
    inputImageFormat = format;
  }

  GstMlInputImageFormat GstOnnxClient::getInputImageFormat (void)
  {
    return inputImageFormat;
  }

  void GstOnnxClient::setInputImageDatatype(GstTensorDataType datatype)
  {
    inputDatatype = datatype;
    switch (inputDatatype) {
      case GST_TENSOR_DATA_TYPE_UINT8:
        inputDatatypeSize = sizeof (uint8_t);
        break;
      case GST_TENSOR_DATA_TYPE_UINT16:
        inputDatatypeSize = sizeof (uint16_t);
        break;
      case GST_TENSOR_DATA_TYPE_UINT32:
        inputDatatypeSize = sizeof (uint32_t);
        break;
      case GST_TENSOR_DATA_TYPE_INT32:
        inputDatatypeSize = sizeof (int32_t);
        break;
      case GST_TENSOR_DATA_TYPE_FLOAT16:
        inputDatatypeSize = 2;
        break;
      case GST_TENSOR_DATA_TYPE_FLOAT32:
        inputDatatypeSize = sizeof (float);
        break;
    default:
        g_error ("Data type %d not handled", inputDatatype);
        break;
    };
  }

  void GstOnnxClient::setInputImageOffset (float offset)
  {
    inputTensorOffset = offset;
  }

  float GstOnnxClient::getInputImageOffset ()
  {
    return inputTensorOffset;
  }

  void GstOnnxClient::setInputImageScale (float scale)
  {
    inputTensorScale = scale;
  }

  float GstOnnxClient::getInputImageScale ()
  {
    return inputTensorScale;
  }

  GstTensorDataType GstOnnxClient::getInputImageDatatype(void)
  {
    return inputDatatype;
  }

  bool GstOnnxClient::hasSession (void)
  {
    return session != nullptr;
  }

  bool GstOnnxClient::setTensorDescDatatype(ONNXTensorElementDataType dt,
                                            GstStructure *tensor_desc)
  {
    GValue val = G_VALUE_INIT;
    GstTensorDataType gst_dt;

    g_value_init(&val, G_TYPE_STRING);

    if (dt > ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED &&
        dt <= ONNX_TENSOR_ELEMENT_DATA_TYPE_INT4) {
      gst_dt = (GstTensorDataType)ONNX_TO_GST_TENSOR_DATATYPE [dt];
      g_value_set_string (&val, gst_tensor_data_type_get_name(gst_dt));
    } else {
      GST_ERROR_OBJECT (debug_parent, "Unexpected datatype: %d", dt);
      g_value_unset (&val);
      return false;
    }

    gst_structure_take_value(tensor_desc, "type", &val);
    g_value_unset(&val);
    return true;
  }

  bool GstOnnxClient::createSession (std::string modelFile,
      GstOnnxOptimizationLevel optim, GstOnnxExecutionProvider provider,
                                     GstStructure * tensors)
  {
    OrtStatus *status = nullptr;
    OrtSessionOptions* session_options = nullptr;
    OrtTypeInfo* input_type_info = nullptr;
    const OrtTensorTypeAndShapeInfo* input_tensor_info = nullptr;
    OrtModelMetadata* metadata = nullptr;
    GraphOptimizationLevel onnx_optim;
    size_t num_input_dims;
    int64_t *input_dims;
    ONNXTensorElementDataType elementType;
    size_t output_count;
    char* input_name = nullptr;

    if (session)
      return true;

    // Create session options
    status = api->CreateSessionOptions(&session_options);
    if (status) {
      GST_ERROR_OBJECT(debug_parent, "Failed to create session options: %s",
			 api->GetErrorMessage(status));
      goto error;
    }

    // Set graph optimization level
    switch (optim) {
    case GST_ONNX_OPTIMIZATION_LEVEL_DISABLE_ALL:
      onnx_optim = GraphOptimizationLevel::ORT_DISABLE_ALL;
      break;
    case GST_ONNX_OPTIMIZATION_LEVEL_ENABLE_BASIC:
      onnx_optim = GraphOptimizationLevel::ORT_ENABLE_BASIC;
      break;
    case GST_ONNX_OPTIMIZATION_LEVEL_ENABLE_EXTENDED:
      onnx_optim = GraphOptimizationLevel::ORT_ENABLE_EXTENDED;
      break;
    case GST_ONNX_OPTIMIZATION_LEVEL_ENABLE_ALL:
      onnx_optim = GraphOptimizationLevel::ORT_ENABLE_ALL;
      break;
    default:
      onnx_optim = GraphOptimizationLevel::ORT_ENABLE_EXTENDED;
      break;
    };

    status = api->SetSessionGraphOptimizationLevel(session_options, onnx_optim);
    if (status) {
      GST_ERROR_OBJECT(debug_parent, "Failed to set optimization level: %s",
		       api->GetErrorMessage (status));
      goto error;
    }

    // Set execution provider
    m_provider = provider;
    switch (m_provider) {
    case GST_ONNX_EXECUTION_PROVIDER_CUDA:
      {
        OrtCUDAProviderOptionsV2 *cuda_options = nullptr;
        status = api->CreateCUDAProviderOptions(&cuda_options);
        if (status) {
          GST_WARNING_OBJECT (debug_parent,
              "Failed to create CUDA provider - dropping back to CPU: %s",
			      api->GetErrorMessage (status));
          api->ReleaseStatus(status);
          status = OrtSessionOptionsAppendExecutionProvider_CPU(session_options, 1);
        } else {
          status = api->SessionOptionsAppendExecutionProvider_CUDA_V2(
              session_options, cuda_options);
          api->ReleaseCUDAProviderOptions(cuda_options);
          if (status) {
            GST_WARNING_OBJECT (debug_parent,
                "Failed to append CUDA provider - dropping back to CPU: %s",
			 api->GetErrorMessage(status));
            api->ReleaseStatus(status);
            status = OrtSessionOptionsAppendExecutionProvider_CPU(session_options, 1);
          }
        }
      }
      break;
#ifdef HAVE_VSI_NPU
    case GST_ONNX_EXECUTION_PROVIDER_VSI:
      status = OrtSessionOptionsAppendExecutionProvider_VSINPU(session_options);
      if (status) {
        GST_ERROR_OBJECT(debug_parent, "Failed to set VSINPU AI execution provider:"
			   " %s", api->GetErrorMessage(status));
        goto error;
      }
      api->DisableCpuMemArena(session_options);
      break;
#endif
    default:
      status = OrtSessionOptionsAppendExecutionProvider_CPU(session_options, 1);
      if (status) {
        GST_ERROR_OBJECT(debug_parent, "Failed to append CPU provider: %s",
			 api->GetErrorMessage(status));
        goto error;
      }
      break;
    }

    // Create environment
    status = api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "GstOnnxNamespace", &env);
    if (status) {
      GST_ERROR_OBJECT(debug_parent, "Failed to create environment: %s",
			 api->GetErrorMessage(status));
      goto error;
    }

    status = api->DisableTelemetryEvents(env);
    if (status) {
      api->ReleaseStatus(status);
      status = nullptr;
    }

    // Create session
    status = api->CreateSession(env, modelFile.c_str(), session_options, &session);
    if (status) {
      GST_ERROR_OBJECT(debug_parent, "Failed to create session: %s",
			 api->GetErrorMessage(status));
      goto error;
    }

    api->ReleaseSessionOptions(session_options);
    session_options = nullptr;

    // Get allocator
    status = api->GetAllocatorWithDefaultOptions(&allocator);
    if (status) {
      GST_ERROR_OBJECT(debug_parent, "Failed to get allocator: %s",
			 api->GetErrorMessage(status));
      goto error;
    }

    // Get input info
    status = api->SessionGetInputTypeInfo(session, 0, &input_type_info);
    if (status) {
      GST_ERROR_OBJECT(debug_parent, "Failed to get input type info: %s",
			 api->GetErrorMessage(status));
      goto error;
    }

    status = api->CastTypeInfoToTensorInfo(input_type_info, &input_tensor_info);
    if (status) {
      GST_ERROR_OBJECT(debug_parent, "Failed to cast type info: %s",
			 api->GetErrorMessage(status));
      goto error;
    }

    status = api->GetDimensionsCount(input_tensor_info, &num_input_dims);
    if (status) {
      GST_ERROR_OBJECT(debug_parent, "Failed to get dimensions count: %s",
			 api->GetErrorMessage(status));
      goto error;
    }

    input_dims = (int64_t *) g_alloca (num_input_dims * sizeof (int64_t));
    status = api->GetDimensions(input_tensor_info, input_dims, num_input_dims);
    if (status) {
      GST_ERROR_OBJECT(debug_parent, "Failed to get dimensions: %s",
			 api->GetErrorMessage(status));
      goto error;
    }

    if (inputImageFormat == GST_ML_INPUT_IMAGE_FORMAT_HWC) {
      height = input_dims[1];
      width = input_dims[2];
      channels = input_dims[3];
    } else {
      channels = input_dims[1];
      height = input_dims[2];
      width = input_dims[3];
    }

    fixedInputImageSize = width > 0 && height > 0;

    status = api->SessionGetOutputCount(session, &output_count);
    if (status) {
      api->ReleaseStatus(status);
      status = nullptr;
      output_count = 0;
    }
    GST_DEBUG_OBJECT (debug_parent, "Number of Output Nodes: %zu", output_count);

    status = api->GetTensorElementType(input_tensor_info, &elementType);
    if (status) {
      GST_ERROR_OBJECT(debug_parent, "Failed to get element type: %s",
			 api->GetErrorMessage(status));
      goto error;
    }

    api->ReleaseTypeInfo(input_type_info);
    input_type_info = nullptr;

    switch (elementType) {
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8:
      setInputImageDatatype(GST_TENSOR_DATA_TYPE_UINT8);
      break;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:
      setInputImageDatatype(GST_TENSOR_DATA_TYPE_FLOAT32);
      break;
    default:
      GST_ERROR_OBJECT(debug_parent,
		       "Only input tensors of type int8 and float are supported");
      goto error;
    }

    // Get input name
    status = api->SessionGetInputName(session, 0, allocator, &input_name);
    if (status) {
      GST_ERROR_OBJECT(debug_parent, "Failed to get input name: %s",
			 api->GetErrorMessage(status));
      goto error;
    }
    GST_DEBUG_OBJECT (debug_parent, "Input name: %s", input_name);
    allocator->Free(allocator, input_name);

    // Get output names
    for (size_t i = 0; i < output_count; ++i) {
      char* output_name;
      status = api->SessionGetOutputName(session, i, allocator, &output_name);
      if (status) {
        GST_ERROR_OBJECT(debug_parent, "Failed to get output name %zu", i);
        api->ReleaseStatus(status);
        status = nullptr;
        continue;
      }
      GST_DEBUG_OBJECT (debug_parent, "Output name %lu:%s", i, output_name);
      outputNames.push_back(output_name);
    }

    // Look up tensor ids
    status = api->SessionGetModelMetadata(session, &metadata);
    if (status) {
      GST_ERROR_OBJECT(debug_parent, "Failed to get model metadata: %s",
			 api->GetErrorMessage(status));
      api->ReleaseStatus(status);
      status = nullptr;
    }

    if (metadata) {
      size_t i = 0;
      for (auto & name:outputNames) {
        OrtTypeInfo* output_type_info = nullptr;
        status = api->SessionGetOutputTypeInfo(session, i++, &output_type_info);
        if (status) {
          api->ReleaseStatus(status);
          status = nullptr;
          continue;
        }

        const OrtTensorTypeAndShapeInfo* output_tensor_info;
        status = api->CastTypeInfoToTensorInfo(output_type_info, &output_tensor_info);
        if (status) {
          api->ReleaseStatus(status);
          status = nullptr;
          api->ReleaseTypeInfo(output_type_info);
          continue;
        }

        size_t card;
        status = api->GetDimensionsCount(output_tensor_info, &card);
        if (status) {
          api->ReleaseStatus(status);
          status = nullptr;
          api->ReleaseTypeInfo(output_type_info);
          continue;
        }

        ONNXTensorElementDataType type;
        status = api->GetTensorElementType(output_tensor_info, &type);
        if (status) {
          api->ReleaseStatus(status);
          status = nullptr;
          api->ReleaseTypeInfo(output_type_info);
          continue;
        }

        char* res = nullptr;
        status = api->ModelMetadataLookupCustomMetadataMap(metadata, allocator, name, &res);

        if (!status && res) {
          GQuark quark = g_quark_from_string(res);
          outputIds.push_back(quark);
          allocator->Free(allocator, res);
        } else {
          if (status) {
            api->ReleaseStatus(status);
            status = nullptr;
          }

          if (g_str_has_prefix(name, "scores")) {
            GQuark quark = g_quark_from_static_string(GST_MODEL_OBJECT_DETECTOR_SCORES);
            GST_INFO_OBJECT(debug_parent,
                "No custom metadata for key '%s', assuming %s",
                name, GST_MODEL_OBJECT_DETECTOR_SCORES);
            outputIds.push_back(quark);
          } else if (g_str_has_prefix(name, "boxes")) {
            GQuark quark = g_quark_from_static_string(GST_MODEL_OBJECT_DETECTOR_BOXES);
            GST_INFO_OBJECT(debug_parent,
                "No custom metadata for key '%s', assuming %s",
                name, GST_MODEL_OBJECT_DETECTOR_BOXES);
            outputIds.push_back(quark);
          } else if (g_str_has_prefix(name, "detection_classes")) {
            GQuark quark = g_quark_from_static_string(GST_MODEL_OBJECT_DETECTOR_CLASSES);
            GST_INFO_OBJECT(debug_parent,
                "No custom metadata for key '%s', assuming %s",
                name, GST_MODEL_OBJECT_DETECTOR_CLASSES);
            outputIds.push_back(quark);
          } else if (g_str_has_prefix(name, "num_detections")) {
            GQuark quark = g_quark_from_static_string(GST_MODEL_OBJECT_DETECTOR_NUM_DETECTIONS);
            GST_INFO_OBJECT(debug_parent,
                "No custom metadata for key '%s', assuming %s",
                name, GST_MODEL_OBJECT_DETECTOR_NUM_DETECTIONS);
            outputIds.push_back(quark);
          } else {
            GST_ERROR_OBJECT(debug_parent, "Failed to look up id for key %s", name);
            api->ReleaseTypeInfo(output_type_info);
            goto error;
          }
        }

        GST_DEBUG_OBJECT(debug_parent, "Tensor %zu (%s) has id \"%s\"", i, name,
            g_quark_to_string(outputIds.back()));

        /* tensor description */
        GstStructure *tensor_desc = gst_structure_new_empty("tensor/strided");

        /* Setting dims */
        GValue val_dims = G_VALUE_INIT, val = G_VALUE_INIT;
        gst_value_array_init(&val_dims, card);
        g_value_init(&val, G_TYPE_INT);

        std::vector<int64_t> shape(card);
        status = api->GetDimensions(output_tensor_info, shape.data(), card);
        if (!status) {
          for (auto &dim : shape) {
            g_value_set_int(&val, dim > 0 ? dim : 0);
            gst_value_array_append_value(&val_dims, &val);
          }
        } else {
          api->ReleaseStatus(status);
          status = nullptr;
        }

        gst_structure_take_value(tensor_desc, "dims", &val_dims);
        g_value_unset(&val_dims);
        g_value_unset(&val);

        /* Setting datatype */
        if (!setTensorDescDatatype(type, tensor_desc)) {
          api->ReleaseTypeInfo(output_type_info);
          goto error;
        }

        /* Setting tensors caps */
        char* meta_key = nullptr;
        OrtStatus* lookup_status = api->ModelMetadataLookupCustomMetadataMap(metadata, allocator, name, &meta_key);
        if (!lookup_status && meta_key) {
          gst_structure_set(tensors, meta_key, GST_TYPE_CAPS,
                            gst_caps_new_full(tensor_desc, NULL), NULL);
          allocator->Free(allocator, meta_key);
        } else {
          if (lookup_status)
            api->ReleaseStatus(lookup_status);
          gst_structure_free(tensor_desc);
        }

        api->ReleaseTypeInfo(output_type_info);
      }
      api->ReleaseModelMetadata(metadata);
      metadata = nullptr;
    }

    // Create memory info for CPU
    status = api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memory_info);
    if (status) {
      GST_WARNING_OBJECT(debug_parent, "Failed to create memory info: %s",
			 api->GetErrorMessage(status));
      goto error;
    }

    return true;

error:
    if (status)
      api->ReleaseStatus(status);
    if (input_type_info)
      api->ReleaseTypeInfo(input_type_info);
    if (metadata)
      api->ReleaseModelMetadata(metadata);
    if (session_options)
      api->ReleaseSessionOptions(session_options);

    destroySession();
    return false;
  }

  void GstOnnxClient::destroySession (void)
  {
    if (session == NULL)
      return;

    // Clean up output names
    for (auto name : outputNames) {
      if (name)
        allocator->Free(allocator, name);
    }
    outputNames.clear();
    outputIds.clear();

    if (memory_info)
      api->ReleaseMemoryInfo(memory_info);

    api->ReleaseSession(session);
    session = NULL;

    if (env) {
      api->ReleaseEnv(env);
      env = NULL;
    }
  }

  void GstOnnxClient::parseDimensions (GstVideoInfo vinfo)
  {
    int32_t newWidth = fixedInputImageSize ? width : vinfo.width;
    int32_t newHeight = fixedInputImageSize ? height : vinfo.height;

    if (!fixedInputImageSize) {
      GST_WARNING_OBJECT (debug_parent, "Allocating before knowing model input size");
    }

    if (!dest || width * height < newWidth * newHeight) {
      delete[]dest;
      dest = new uint8_t[newWidth * newHeight * channels * inputDatatypeSize];
    }
    width = newWidth;
    height = newHeight;
  }

  // copy tensor data to a GstTensorMeta
  GstTensorMeta *
  GstOnnxClient::copy_tensors_to_meta (OrtValue **outputs, size_t num_outputs,
      GstBuffer * buffer)
  {
    OrtStatus* status = NULL;
    size_t num_tensors = num_outputs;
    GstTensorMeta *tmeta = gst_buffer_add_tensor_meta (buffer);
    tmeta->num_tensors = num_tensors;
    tmeta->tensors = g_new (GstTensor *, num_tensors);
    bool hasIds = outputIds.size () == num_tensors;

    for (size_t i = 0; i < num_tensors; i++) {
      OrtValue* outputTensor = outputs[i];

      OrtTensorTypeAndShapeInfo* tensor_info;
      status = api->GetTensorTypeAndShape(outputTensor, &tensor_info);
      if (status) {
        GST_ERROR_OBJECT(debug_parent, "Failed to get tensor info: %s",
            api->GetErrorMessage(status));
        api->ReleaseStatus(status);
        gst_buffer_remove_meta(buffer, (GstMeta *) tmeta);
        return NULL;
      }

      ONNXTensorElementDataType tensorType;
      status = api->GetTensorElementType(tensor_info, &tensorType);
      if (status) {
        GST_ERROR_OBJECT(debug_parent, "Failed to get tensor type: %s",
            api->GetErrorMessage(status));
        api->ReleaseStatus(status);
        api->ReleaseTensorTypeAndShapeInfo(tensor_info);
        gst_buffer_remove_meta(buffer, (GstMeta *) tmeta);
        return NULL;
      }

      size_t num_dims;
      status = api->GetDimensionsCount(tensor_info, &num_dims);
      if (status) {
        api->ReleaseStatus(status);
        api->ReleaseTensorTypeAndShapeInfo(tensor_info);
        gst_buffer_remove_meta(buffer, (GstMeta *) tmeta);
        return NULL;
      }

      std::vector<int64_t> tensorShape(num_dims);
      status = api->GetDimensions(tensor_info, tensorShape.data(), num_dims);
      if (status) {
        api->ReleaseStatus(status);
        api->ReleaseTensorTypeAndShapeInfo(tensor_info);
        gst_buffer_remove_meta(buffer, (GstMeta *) tmeta);
        return NULL;
      }

      GstTensor *tensor = gst_tensor_alloc(tensorShape.size());
      tmeta->tensors[i] = tensor;

      if (hasIds)
        tensor->id = outputIds[i];
      else
        tensor->id = 0;

      for (size_t j = 0; j < tensorShape.size(); ++j)
        tensor->dims[j] = tensorShape[j];

      size_t numElements;
      status = api->GetTensorShapeElementCount(tensor_info, &numElements);
      if (status) {
        api->ReleaseStatus(status);
        numElements = 1;
        for (auto dim : tensorShape)
          numElements *= dim;
      }

      api->ReleaseTensorTypeAndShapeInfo(tensor_info);

      void* tensor_data;
      status = api->GetTensorMutableData(outputTensor, &tensor_data);
      if (status) {
        GST_ERROR_OBJECT(debug_parent, "Failed to get tensor data: %s",
            api->GetErrorMessage(status));
        api->ReleaseStatus(status);
        gst_buffer_remove_meta(buffer, (GstMeta *) tmeta);
        return NULL;
      }

      if (tensorType == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        size_t buffer_size = numElements * sizeof(float);
        tensor->data = gst_buffer_new_allocate(NULL, buffer_size, NULL);
        gst_buffer_fill(tensor->data, 0, tensor_data, buffer_size);
        tensor->data_type = GST_TENSOR_DATA_TYPE_FLOAT32;
      } else if (tensorType == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32) {
        size_t buffer_size = numElements * sizeof(int);
        tensor->data = gst_buffer_new_allocate(NULL, buffer_size, NULL);
        gst_buffer_fill(tensor->data, 0, tensor_data, buffer_size);
        tensor->data_type = GST_TENSOR_DATA_TYPE_INT32;
      } else {
        GST_ERROR_OBJECT(debug_parent,
            "Output tensor is not FLOAT32 or INT32, not supported");
        gst_buffer_remove_meta(buffer, (GstMeta *) tmeta);
        return NULL;
      }
    }

    return tmeta;
  }

  OrtValue** GstOnnxClient::run (uint8_t * img_data, GstVideoInfo vinfo, size_t *num_outputs)
  {
    OrtStatus* status = nullptr;
    char* inputName = nullptr;
    OrtTypeInfo* input_type_info = nullptr;
    OrtValue* input_tensor = nullptr;
    OrtValue** output_tensors = nullptr;
    size_t output_count = outputNames.size();
    const OrtTensorTypeAndShapeInfo* input_tensor_info;
    size_t num_dims;
    int64_t *input_dims;
    std::ostringstream buffer;
    uint8_t *srcPtr[3];
    uint32_t srcSamplesPerPixel;
    uint32_t stride;
    size_t inputTensorSize;
    const char* input_names[1];
    
    if (!img_data) {
      GST_WARNING_OBJECT(debug_parent, "No image data provided");
      return nullptr;
    }

    status = api->SessionGetInputName(session, 0, allocator, &inputName);
    if (status) {
      GST_WARNING_OBJECT(debug_parent, "Failed to get input name");
      goto error;
    }

    status = api->SessionGetInputTypeInfo(session, 0, &input_type_info);
    if (status) {
      GST_WARNING_OBJECT(debug_parent, "Failed to get input type info");
      goto error;
    }

    status = api->CastTypeInfoToTensorInfo(input_type_info, &input_tensor_info);
    if (status) {
      GST_WARNING_OBJECT(debug_parent, "Failed to cast type info");
      goto error;
    }

    status = api->GetDimensionsCount(input_tensor_info, &num_dims);
    if (status) {
      GST_WARNING_OBJECT(debug_parent, "Failed to get dimensions count");
      goto error;
    }

    input_dims = (int64_t *) g_alloca (num_dims * sizeof(int64_t));
    status = api->GetDimensions(input_tensor_info, input_dims, num_dims);
    if (status) {
      GST_WARNING_OBJECT(debug_parent, "Failed to get dimensions");
      goto error;
    }

    api->ReleaseTypeInfo(input_type_info);
    input_type_info = nullptr;

    input_dims[0] = 1;
    if (inputImageFormat == GST_ML_INPUT_IMAGE_FORMAT_HWC) {
      input_dims[1] = height;
      input_dims[2] = width;
    } else {
      input_dims[2] = height;
      input_dims[3] = width;
    }

    buffer << input_dims;
    GST_LOG_OBJECT (debug_parent, "Input dimensions: %s", buffer.str ().c_str ());

    // copy video frame
    srcPtr[0] = img_data;
    srcPtr[1] = img_data + 1;
    srcPtr[2] = img_data + 2;
    srcSamplesPerPixel = 3;
    switch (vinfo.finfo->format) {
      case GST_VIDEO_FORMAT_RGBA:
        srcSamplesPerPixel = 4;
        break;
      case GST_VIDEO_FORMAT_BGRA:
        srcSamplesPerPixel = 4;
        srcPtr[0] = img_data + 2;
        srcPtr[1] = img_data + 1;
        srcPtr[2] = img_data + 0;
        break;
      case GST_VIDEO_FORMAT_ARGB:
        srcSamplesPerPixel = 4;
        srcPtr[0] = img_data + 1;
        srcPtr[1] = img_data + 2;
        srcPtr[2] = img_data + 3;
        break;
      case GST_VIDEO_FORMAT_ABGR:
        srcSamplesPerPixel = 4;
        srcPtr[0] = img_data + 3;
        srcPtr[1] = img_data + 2;
        srcPtr[2] = img_data + 1;
        break;
      case GST_VIDEO_FORMAT_BGR:
        srcPtr[0] = img_data + 2;
        srcPtr[1] = img_data + 1;
        srcPtr[2] = img_data + 0;
        break;
      default:
        break;
    }
    stride = vinfo.stride[0];
    inputTensorSize = width * height * channels * inputDatatypeSize;

    switch (inputDatatype) {
      case GST_TENSOR_DATA_TYPE_UINT8: {
        uint8_t *src_data;
        if (inputTensorOffset == 0.0 && inputTensorScale == 1.0) {
          src_data = img_data;
        } else {
          convert_image_remove_alpha(
            dest, inputImageFormat, srcPtr, srcSamplesPerPixel, stride,
            (uint8_t)inputTensorOffset, (uint8_t)inputTensorScale);
          src_data = dest;
        }

        status = api->CreateTensorWithDataAsOrtValue(
            memory_info, src_data, inputTensorSize,
            input_dims, num_dims,
            ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8, &input_tensor);
        }
        break;
      case GST_TENSOR_DATA_TYPE_FLOAT32: {
        convert_image_remove_alpha((float*)dest, inputImageFormat, srcPtr,
            srcSamplesPerPixel, stride, (float)inputTensorOffset,
            (float)inputTensorScale);
        
        status = api->CreateTensorWithDataAsOrtValue(
            memory_info, (float*)dest, inputTensorSize,
            input_dims, num_dims,
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input_tensor);
        }
        break;
      default:
        GST_WARNING_OBJECT(debug_parent, "Unsupported input datatype");
        goto error;
    }

    if (status) {
      GST_WARNING_OBJECT(debug_parent, "Failed to create input tensor");
      goto error;
    }

    input_names[0] = inputName;
    output_tensors = g_new0(OrtValue*, output_count);

    status = api->Run(session, nullptr, input_names, &input_tensor, 1,
                      outputNames.data(), output_count, output_tensors);

    if (status) {
      GST_WARNING_OBJECT(debug_parent, "Failed to run inference");
      goto error;
    }

    allocator->Free(allocator, inputName);
    api->ReleaseValue(input_tensor);
    *num_outputs = output_count;
    return output_tensors;

error:
    if (status)
      api->ReleaseStatus(status);
    if (inputName)
      allocator->Free(allocator, inputName);
    if (input_type_info)
      api->ReleaseTypeInfo(input_type_info);
    if (input_tensor)
      api->ReleaseValue(input_tensor);
    if (output_tensors) {
      for (size_t i = 0; i < output_count; i++) {
        if (output_tensors[i])
          api->ReleaseValue(output_tensors[i]);
      }
      g_free(output_tensors);
    }
    *num_outputs = 0;
    return nullptr;
  }

  template < typename T>
  void GstOnnxClient::convert_image_remove_alpha (T *dst,
      GstMlInputImageFormat hwc, uint8_t **srcPtr, uint32_t srcSamplesPerPixel,
      uint32_t stride, T offset, T div) {
    size_t destIndex = 0;
    T tmp;

    if (inputImageFormat == GST_ML_INPUT_IMAGE_FORMAT_HWC) {
      for (int32_t j = 0; j < height; ++j) {
        for (int32_t i = 0; i < width; ++i) {
          for (int32_t k = 0; k < channels; ++k) {
            tmp = *srcPtr[k];
            tmp += offset;
            dst[destIndex++] = (T)(tmp / div);
            srcPtr[k] += srcSamplesPerPixel;
          }
        }
        // correct for stride
        for (uint32_t k = 0; k < 3; ++k)
          srcPtr[k] += stride - srcSamplesPerPixel * width;
      }
    } else {
      size_t frameSize = width * height;
      T *destPtr[3] = { dst, dst + frameSize, dst + 2 * frameSize };
      for (int32_t j = 0; j < height; ++j) {
        for (int32_t i = 0; i < width; ++i) {
          for (int32_t k = 0; k < channels; ++k) {
            tmp = *srcPtr[k];
            tmp += offset;
            destPtr[k][destIndex] = (T)(tmp / div);
            srcPtr[k] += srcSamplesPerPixel;
          }
          destIndex++;
        }
        // correct for stride
        for (uint32_t k = 0; k < 3; ++k)
          srcPtr[k] += stride - srcSamplesPerPixel * width;
      }
    }
  }
}
