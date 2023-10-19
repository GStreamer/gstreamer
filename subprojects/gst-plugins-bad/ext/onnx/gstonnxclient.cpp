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
#include <cpu_provider_factory.h>
#include <sstream>

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

GstOnnxClient::GstOnnxClient ():session (nullptr),
      width (0),
      height (0),
      channels (0),
      dest (nullptr),
      m_provider (GST_ONNX_EXECUTION_PROVIDER_CPU),
      inputImageFormat (GST_ML_INPUT_IMAGE_FORMAT_HWC),
      fixedInputImageSize (false) {
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

  bool GstOnnxClient::createSession (std::string modelFile,
      GstOnnxOptimizationLevel optim, GstOnnxExecutionProvider provider)
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
      GST_DEBUG ("Number of Output Nodes: %d",
          (gint) session->GetOutputCount ());

      Ort::AllocatorWithDefaultOptions allocator;
      auto input_name = session->GetInputNameAllocated (0, allocator);
      GST_DEBUG ("Input name: %s", input_name.get ());

      for (size_t i = 0; i < session->GetOutputCount (); ++i) {
        auto output_name = session->GetOutputNameAllocated (i, allocator);
        GST_DEBUG ("Output name %lu:%s", i, output_name.get ());
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
        GST_WARNING ("Failed to get allocator: %s", errorString);

        // Clean up the error status
        Ort::GetApi ().ReleaseStatus (status);

        return false;
      } else {
      for (auto & name:outputNamesRaw) {
        Ort::AllocatedStringPtr res =
            metaData.LookupCustomMetadataMapAllocated (name, ortAllocator);
        if (res) {
          GQuark quark = g_quark_from_static_string (res.get ());
          outputIds.push_back (quark);
        } else {
          GST_ERROR ("Failed to look up id for key %s", name);

          return false;
        }
      }
      }
    }
    catch (Ort::Exception & ortex) {
      GST_ERROR ("%s", ortex.what ());
      return false;
    }

    return true;
  }

  void GstOnnxClient::parseDimensions (GstVideoInfo vinfo)
  {
    int32_t newWidth = fixedInputImageSize ? width : vinfo.width;
    int32_t newHeight = fixedInputImageSize ? height : vinfo.height;

    if (!dest || width * height < newWidth * newHeight) {
      delete[]dest;
      dest = new uint8_t[newWidth * newHeight * channels];
    }
    width = newWidth;
    height = newHeight;
  }

// copy tensor data to a GstTensorMeta
  GstTensorMeta *GstOnnxClient::copy_tensors_to_meta (std::vector < Ort::Value >
      &outputs, GstBuffer * buffer)
  {
    size_t num_tensors = outputNamesRaw.size ();
    GstTensorMeta *tmeta = (GstTensorMeta *) gst_buffer_add_meta (buffer,
        gst_tensor_meta_get_info (),
        NULL);
    tmeta->num_tensors = num_tensors;
    tmeta->tensor = (GstTensor *) g_malloc (num_tensors * sizeof (GstTensor));
    bool hasIds = outputIds.size () == num_tensors;
    for (size_t i = 0; i < num_tensors; i++) {
      Ort::Value outputTensor = std::move (outputs[i]);

      ONNXTensorElementDataType tensorType =
          outputTensor.GetTensorTypeAndShapeInfo ().GetElementType ();

      GstTensor *tensor = &tmeta->tensor[i];
      if (hasIds)
        tensor->id = outputIds[i];
      tensor->data = gst_buffer_new ();
      auto tensorShape = outputTensor.GetTensorTypeAndShapeInfo ().GetShape ();
      tensor->num_dims = tensorShape.size ();
      tensor->dims = g_new (int64_t, tensor->num_dims);

      for (size_t j = 0; j < tensorShape.size (); ++j) {
        tensor->dims[j] = tensorShape[j];
      }

      size_t numElements =
          outputTensor.GetTensorTypeAndShapeInfo ().GetElementCount ();

      size_t buffer_size = 0;
      guint8 *buffer_data = NULL;
      if (tensorType == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        buffer_size = numElements * sizeof (float);

        // Allocate memory for the buffer data
        buffer_data = (guint8 *) malloc (buffer_size);
        if (buffer_data == NULL) {
          GST_ERROR ("Failed to allocate memory");
          return NULL;
        }
        // Copy the data from the source buffer to the allocated memory
        memcpy (buffer_data, outputTensor.GetTensorData < float >(),
            buffer_size);
        tensor->type = GST_TENSOR_TYPE_FLOAT32;
      } else if (tensorType == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32) {
        buffer_size = numElements * sizeof (int);

        // Allocate memory for the buffer data
        guint8 *buffer_data = (guint8 *) malloc (buffer_size);
        if (buffer_data == NULL) {
          GST_ERROR ("Failed to allocate memory");
          return NULL;
        }
        // Copy the data from the source buffer to the allocated memory
        memcpy (buffer_data, outputTensor.GetTensorData < int >(),
            buffer_size);
        tensor->type = GST_TENSOR_TYPE_INT32;
      }
      if (buffer_data) {

        // Create a GstMemory object from the allocated memory
        GstMemory *memory = gst_memory_new_wrapped ((GstMemoryFlags) 0,
            buffer_data, buffer_size, 0, buffer_size, NULL, NULL);

        // Append the GstMemory object to the GstBuffer
        gst_buffer_append_memory (tmeta->tensor[i].data, memory);
      }
    }

    return tmeta;

  }

  std::vector < Ort::Value > GstOnnxClient::run (uint8_t * img_data,
      GstVideoInfo vinfo) {
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
    GST_DEBUG ("Input dimensions: %s", buffer.str ().c_str ());

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
    size_t destIndex = 0;
    uint32_t stride = vinfo.stride[0];
    if (inputImageFormat == GST_ML_INPUT_IMAGE_FORMAT_HWC) {
      for (int32_t j = 0; j < height; ++j) {
        for (int32_t i = 0; i < width; ++i) {
          for (int32_t k = 0; k < channels; ++k) {
            dest[destIndex++] = *srcPtr[k];
            srcPtr[k] += srcSamplesPerPixel;
          }
        }
        // correct for stride
        for (uint32_t k = 0; k < 3; ++k)
          srcPtr[k] += stride - srcSamplesPerPixel * width;
      }
    } else {
      size_t frameSize = width * height;
      uint8_t *destPtr[3] = { dest, dest + frameSize, dest + 2 * frameSize };
      for (int32_t j = 0; j < height; ++j) {
        for (int32_t i = 0; i < width; ++i) {
          for (int32_t k = 0; k < channels; ++k) {
            destPtr[k][destIndex] = *srcPtr[k];
            srcPtr[k] += srcSamplesPerPixel;
          }
          destIndex++;
        }
        // correct for stride
        for (uint32_t k = 0; k < 3; ++k)
          srcPtr[k] += stride - srcSamplesPerPixel * width;
      }
    }

    const size_t inputTensorSize = width * height * channels;
    auto memoryInfo =
        Ort::MemoryInfo::CreateCpu (OrtAllocatorType::OrtArenaAllocator,
        OrtMemType::OrtMemTypeDefault);
    std::vector < Ort::Value > inputTensors;
    inputTensors.push_back (Ort::Value::CreateTensor < uint8_t > (memoryInfo,
            dest, inputTensorSize, inputDims.data (), inputDims.size ()));
    std::vector < const char *>inputNames
    {
    inputName.get ()};

    modelOutput = session->Run (Ort::RunOptions {
        nullptr},
        inputNames.data (),
        inputTensors.data (), 1, outputNamesRaw.data (),
        outputNamesRaw.size ());

    return true;
  }

}
