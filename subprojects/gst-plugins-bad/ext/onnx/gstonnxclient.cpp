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
#include <onnxruntime_cxx_api.h>
#include <cpu_provider_factory.h>
#include <sstream>

#define GST_CAT_DEFAULT onnx_inference_debug

/* FIXME: share this with tensordecoders, somehow? */
#define GST_MODEL_OBJECT_DETECTOR_BOXES "Gst.Model.ObjectDetector.Boxes"
#define GST_MODEL_OBJECT_DETECTOR_SCORES "Gst.Model.ObjectDetector.Scores"
#define GST_MODEL_OBJECT_DETECTOR_NUM_DETECTIONS "Gst.Model.ObjectDetector.NumDetections"
#define GST_MODEL_OBJECT_DETECTOR_CLASSES "Gst.Model.ObjectDetector.Classes"

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

GstOnnxClient::GstOnnxClient (GstElement *debug_parent):debug_parent(debug_parent),
      session (nullptr),
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
  }

  GstOnnxClient::~GstOnnxClient () {
    delete session;
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

  std::vector < const char *>GstOnnxClient::genOutputNamesRaw (void)
  {
    if (!outputNames.empty () && outputNamesRaw.size () != outputNames.size ()) {
      outputNamesRaw.resize (outputNames.size ());
      for (size_t i = 0; i < outputNamesRaw.size (); i++)
        outputNamesRaw[i] = outputNames[i].get ();
    }

    return outputNamesRaw;
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
    if (session)
      return true;

    GraphOptimizationLevel onnx_optim;
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

    try {
      Ort::SessionOptions sessionOptions;
      const auto & api = Ort::GetApi ();
      // for debugging
      //sessionOptions.SetIntraOpNumThreads (1);
      sessionOptions.SetGraphOptimizationLevel (onnx_optim);
      m_provider = provider;
      switch (m_provider) {
        case GST_ONNX_EXECUTION_PROVIDER_CUDA:
        try {
          OrtCUDAProviderOptionsV2 *cuda_options = nullptr;
          Ort::ThrowOnError (api.CreateCUDAProviderOptions (&cuda_options));
          std::unique_ptr < OrtCUDAProviderOptionsV2,
              decltype (api.ReleaseCUDAProviderOptions) >
              rel_cuda_options (cuda_options, api.ReleaseCUDAProviderOptions);
          Ort::ThrowOnError (api.SessionOptionsAppendExecutionProvider_CUDA_V2
              (static_cast < OrtSessionOptions * >(sessionOptions),
                  rel_cuda_options.get ()));
        }
          catch (Ort::Exception & ortex) {
            GST_WARNING
                ("Failed to create CUDA provider - dropping back to CPU");
            Ort::ThrowOnError (OrtSessionOptionsAppendExecutionProvider_CPU
                (sessionOptions, 1));
          }
          break;
        default:
          Ort::ThrowOnError (OrtSessionOptionsAppendExecutionProvider_CPU
              (sessionOptions, 1));
          break;
      };
      env =
          Ort::Env (OrtLoggingLevel::ORT_LOGGING_LEVEL_WARNING,
          "GstOnnxNamespace");
      env.DisableTelemetryEvents();
      session = new Ort::Session (env, modelFile.c_str (), sessionOptions);
      auto inputTypeInfo = session->GetInputTypeInfo (0);
      std::vector < int64_t > inputDims =
          inputTypeInfo.GetTensorTypeAndShapeInfo ().GetShape ();
      if (inputImageFormat == GST_ML_INPUT_IMAGE_FORMAT_HWC) {
        height = inputDims[1];
        width = inputDims[2];
        channels = inputDims[3];
      } else {
        channels = inputDims[1];
        height = inputDims[2];
        width = inputDims[3];
      }

      fixedInputImageSize = width > 0 && height > 0;
      GST_DEBUG_OBJECT (debug_parent, "Number of Output Nodes: %d",
          (gint) session->GetOutputCount ());

      ONNXTensorElementDataType elementType =
          inputTypeInfo.GetTensorTypeAndShapeInfo ().GetElementType ();

      switch (elementType) {
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8:
          setInputImageDatatype(GST_TENSOR_DATA_TYPE_UINT8);
          break;
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:
          setInputImageDatatype(GST_TENSOR_DATA_TYPE_FLOAT32);
          break;
        default:
          GST_ERROR_OBJECT (debug_parent,
            "Only input tensors of type int8 and floatare supported");
          return false;
        }

      Ort::AllocatorWithDefaultOptions allocator;
      auto input_name = session->GetInputNameAllocated (0, allocator);
      GST_DEBUG_OBJECT (debug_parent, "Input name: %s", input_name.get ());

      for (size_t i = 0; i < session->GetOutputCount (); ++i) {
        auto output_name = session->GetOutputNameAllocated (i, allocator);
        GST_DEBUG_OBJECT (debug_parent, "Output name %lu:%s", i, output_name.get ());
        outputNames.push_back (std::move (output_name));
      }
      genOutputNamesRaw ();

      // look up tensor ids
      auto metaData = session->GetModelMetadata ();
      OrtAllocator *ortAllocator;
      auto status =
          Ort::GetApi ().GetAllocatorWithDefaultOptions (&ortAllocator);
      if (status) {
        // Handle the error case
        const char *errorString = Ort::GetApi ().GetErrorMessage (status);
        GST_WARNING_OBJECT (debug_parent, "Failed to get allocator: %s", errorString);

        // Clean up the error status
        Ort::GetApi ().ReleaseStatus (status);

        return false;
      }

      size_t i = 0;
      for (auto & name:outputNamesRaw) {

        Ort::TypeInfo ti = session->GetOutputTypeInfo(i++);
        auto type_shape = ti.GetTensorTypeAndShapeInfo();
        auto card = type_shape.GetDimensionsCount();
        auto type = type_shape.GetElementType();
        Ort::AllocatedStringPtr res =
          metaData.LookupCustomMetadataMapAllocated (name, ortAllocator);
        if (res)
        {
          GQuark quark = g_quark_from_string (res.get ());
          outputIds.push_back (quark);
        } else if (g_str_has_prefix (name, "detection_scores")) {
          GQuark quark = g_quark_from_static_string (GST_MODEL_OBJECT_DETECTOR_SCORES);
          GST_INFO_OBJECT(debug_parent,
              "No custom metadata for key '%s', assuming %s",
              name, GST_MODEL_OBJECT_DETECTOR_SCORES);
          outputIds.push_back (quark);
        } else if (g_str_has_prefix(name, "detection_boxes")) {
          GQuark quark = g_quark_from_static_string (GST_MODEL_OBJECT_DETECTOR_BOXES);
          GST_INFO_OBJECT(debug_parent,
              "No custom metadata for key '%s', assuming %s",
              name, GST_MODEL_OBJECT_DETECTOR_BOXES);
          outputIds.push_back (quark);
        } else if (g_str_has_prefix(name, "detection_classes")) {
          GQuark quark = g_quark_from_static_string (GST_MODEL_OBJECT_DETECTOR_CLASSES);
          GST_INFO_OBJECT(debug_parent,
              "No custom metadata for key '%s', assuming %s",
              name, GST_MODEL_OBJECT_DETECTOR_CLASSES);
          outputIds.push_back (quark);
        } else if (g_str_has_prefix(name, "num_detections")) {
          GQuark quark = g_quark_from_static_string (GST_MODEL_OBJECT_DETECTOR_NUM_DETECTIONS);
          GST_INFO_OBJECT(debug_parent,
              "No custom metadata for key '%s', assuming %s",
              name, GST_MODEL_OBJECT_DETECTOR_NUM_DETECTIONS);
          outputIds.push_back (quark);
        } else {
          GST_ERROR_OBJECT (debug_parent, "Failed to look up id for key %s",
              name);
          return false;
        }

        /* tensor description */
        GstStructure *tensor_desc = gst_structure_new_empty("tensor/strided");

        /* Setting dims */
        GValue val_dims = G_VALUE_INIT, val = G_VALUE_INIT;
        gst_value_array_init(&val_dims, card);
        g_value_init(&val, G_TYPE_INT);

        for (auto &dim : type_shape.GetShape()) {
          g_value_set_int(&val, dim > 0 ? dim : 0);
          gst_value_array_append_value(&val_dims, &val);
        }
        gst_structure_take_value(tensor_desc, "dims", &val_dims);
        g_value_unset(&val_dims);
        g_value_unset(&val);

        /* Setting datatype */
        if (!setTensorDescDatatype(type, tensor_desc))
          return false;

        /* Setting tensors caps */
        gst_structure_set(tensors, res.get(), GST_TYPE_CAPS,
                          gst_caps_new_full(tensor_desc, NULL), NULL);
      }
    }
    catch (Ort::Exception & ortex) {
      GST_ERROR_OBJECT (debug_parent, "%s", ortex.what ());
      return false;
    }

    return true;
  }

  void GstOnnxClient::destroySession (void)
  {
    if (session == NULL)
      return;

    delete session;
    session = NULL;
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
  GstOnnxClient::copy_tensors_to_meta (std::vector < Ort::Value >
      &outputs, GstBuffer * buffer)
  {
    size_t num_tensors = outputNamesRaw.size ();
    GstTensorMeta *tmeta = gst_buffer_add_tensor_meta (buffer);
    tmeta->num_tensors = num_tensors;
    tmeta->tensors = g_new (GstTensor *, num_tensors);
    bool hasIds = outputIds.size () == num_tensors;
    for (size_t i = 0; i < num_tensors; i++) {
      Ort::Value outputTensor = std::move (outputs[i]);

      ONNXTensorElementDataType tensorType =
          outputTensor.GetTensorTypeAndShapeInfo ().GetElementType ();

      auto tensorShape = outputTensor.GetTensorTypeAndShapeInfo ().GetShape ();
      GstTensor *tensor = gst_tensor_alloc (tensorShape.size ());
      tmeta->tensors[i] = tensor;

      if (hasIds)
        tensor->id = outputIds[i];
      else
        tensor->id = 0;
      tensor->num_dims = tensorShape.size ();

      for (size_t j = 0; j < tensorShape.size (); ++j)
        tensor->dims[j] = tensorShape[j];

      size_t numElements =
          outputTensor.GetTensorTypeAndShapeInfo ().GetElementCount ();

      if (tensorType == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        size_t buffer_size = 0;

        buffer_size = numElements * sizeof (float);
        tensor->data = gst_buffer_new_allocate (NULL, buffer_size, NULL);
        gst_buffer_fill (tensor->data, 0, outputTensor.GetTensorData < float >(),
            buffer_size);
        tensor->data_type = GST_TENSOR_DATA_TYPE_FLOAT32;
      } else if (tensorType == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32) {
        size_t buffer_size = 0;

        buffer_size = numElements * sizeof (int);
        tensor->data = gst_buffer_new_allocate (NULL, buffer_size, NULL);
        gst_buffer_fill (tensor->data, 0, outputTensor.GetTensorData < float >(),
            buffer_size);
        tensor->data_type = GST_TENSOR_DATA_TYPE_INT32;
      } else {
        GST_ERROR_OBJECT (debug_parent,
            "Output tensor is not FLOAT32 or INT32, not supported");
        gst_buffer_remove_meta (buffer, (GstMeta *) tmeta);
        return NULL;
      }
    }

    return tmeta;

  }

  std::vector < Ort::Value > GstOnnxClient::run (uint8_t * img_data,
      GstVideoInfo vinfo)
  {
    std::vector < Ort::Value > modelOutput;
    doRun (img_data, vinfo, modelOutput);

    return modelOutput;
  }

  bool GstOnnxClient::doRun (uint8_t * img_data, GstVideoInfo vinfo,
      std::vector < Ort::Value > &modelOutput)
  {
    if (!img_data)
      return false;

    Ort::AllocatorWithDefaultOptions allocator;
    auto inputName = session->GetInputNameAllocated (0, allocator);
    auto inputTypeInfo = session->GetInputTypeInfo (0);
    std::vector < int64_t > inputDims =
        inputTypeInfo.GetTensorTypeAndShapeInfo ().GetShape ();
    inputDims[0] = 1;
    if (inputImageFormat == GST_ML_INPUT_IMAGE_FORMAT_HWC) {
      inputDims[1] = height;
      inputDims[2] = width;
    } else {
      inputDims[2] = height;
      inputDims[3] = width;
    }

    std::ostringstream buffer;
    buffer << inputDims;
    GST_DEBUG_OBJECT (debug_parent, "Input dimensions: %s", buffer.str ().c_str ());

    // copy video frame
    uint8_t *srcPtr[3] = { img_data, img_data + 1, img_data + 2 };
    uint32_t srcSamplesPerPixel = 3;
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
    uint32_t stride = vinfo.stride[0];
    const size_t inputTensorSize = width * height * channels * inputDatatypeSize;
    auto memoryInfo =
        Ort::MemoryInfo::CreateCpu (OrtAllocatorType::OrtArenaAllocator,
        OrtMemType::OrtMemTypeDefault);

    std::vector < Ort::Value > inputTensors;

    switch (inputDatatype) {
      case GST_TENSOR_DATA_TYPE_UINT8:
        uint8_t *src_data;
        if (inputTensorOffset == 00 && inputTensorScale == 1.0) {
          src_data = img_data;
        } else {
          convert_image_remove_alpha (
            dest, inputImageFormat, srcPtr, srcSamplesPerPixel, stride,
            (uint8_t)inputTensorOffset, (uint8_t)inputTensorScale);
          src_data = dest;
        }

        inputTensors.push_back (Ort::Value::CreateTensor < uint8_t > (
              memoryInfo, src_data, inputTensorSize, inputDims.data (),
              inputDims.size ()));
        break;
      case GST_TENSOR_DATA_TYPE_FLOAT32: {
        convert_image_remove_alpha ((float*)dest, inputImageFormat , srcPtr,
        srcSamplesPerPixel, stride, (float)inputTensorOffset, (float)
        inputTensorScale);
        inputTensors.push_back (Ort::Value::CreateTensor < float > (
              memoryInfo, (float*)dest, inputTensorSize, inputDims.data (),
              inputDims.size ()));
        }
        break;
      default:
        break;
    }

    std::vector < const char *>inputNames { inputName.get () };
    modelOutput = session->Run (Ort::RunOptions {nullptr},
        inputNames.data (),
        inputTensors.data (), 1, outputNamesRaw.data (),
        outputNamesRaw.size ());

    return true;
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
