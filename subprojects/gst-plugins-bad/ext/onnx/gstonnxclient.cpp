/*
 * GStreamer gstreamer-onnxclient
 * Copyright (C) 2021 Collabora Ltd
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
#include <providers/cpu/cpu_provider_factory.h>
#ifdef GST_ML_ONNX_RUNTIME_HAVE_CUDA
#include <providers/cuda/cuda_provider_factory.h>
#endif
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <cmath>
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

GstMlOutputNodeInfo::GstMlOutputNodeInfo (void):index
  (GST_ML_NODE_INDEX_DISABLED),
  type (ONNXTensorElementDataType::ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
{
}

GstOnnxClient::GstOnnxClient ():session (nullptr),
      width (0),
      height (0),
      channels (0),
      dest (nullptr),
      m_provider (GST_ONNX_EXECUTION_PROVIDER_CPU),
      inputImageFormat (GST_ML_MODEL_INPUT_IMAGE_FORMAT_HWC),
      fixedInputImageSize (true)
{
    for (size_t i = 0; i < GST_ML_OUTPUT_NODE_NUMBER_OF; ++i)
      outputNodeIndexToFunction[i] = (GstMlOutputNodeFunction) i;
}

GstOnnxClient::~GstOnnxClient ()
{
    outputNames.clear();
    delete session;
    delete[]dest;
}

Ort::Env & GstOnnxClient::getEnv (void)
{
    static Ort::Env env (OrtLoggingLevel::ORT_LOGGING_LEVEL_WARNING,
        "GstOnnxNamespace");

    return env;
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

std::string GstOnnxClient::getOutputNodeName (GstMlOutputNodeFunction nodeType)
{
    switch (nodeType) {
      case GST_ML_OUTPUT_NODE_FUNCTION_DETECTION:
        return "detection";
        break;
      case GST_ML_OUTPUT_NODE_FUNCTION_BOUNDING_BOX:
        return "bounding box";
        break;
      case GST_ML_OUTPUT_NODE_FUNCTION_SCORE:
        return "score";
        break;
      case GST_ML_OUTPUT_NODE_FUNCTION_CLASS:
        return "label";
        break;
      case GST_ML_OUTPUT_NODE_NUMBER_OF:
        g_assert_not_reached();
        GST_WARNING("Invalid parameter");
        break;
    };

    return "";
}

void GstOnnxClient::setInputImageFormat (GstMlModelInputImageFormat format)
{
    inputImageFormat = format;
}

GstMlModelInputImageFormat GstOnnxClient::getInputImageFormat (void)
{
    return inputImageFormat;
}

std::vector< const char *> GstOnnxClient::getOutputNodeNames (void)
{
    if (!outputNames.empty() && outputNamesRaw.size() != outputNames.size()) {
        outputNamesRaw.resize(outputNames.size());
        for (size_t i = 0; i < outputNamesRaw.size(); i++) {
          outputNamesRaw[i] = outputNames[i].get();
        }
    }

    return outputNamesRaw;
}

void GstOnnxClient::setOutputNodeIndex (GstMlOutputNodeFunction node,
      gint index)
{
    g_assert (index < GST_ML_OUTPUT_NODE_NUMBER_OF);
    outputNodeInfo[node].index = index;
    if (index != GST_ML_NODE_INDEX_DISABLED)
      outputNodeIndexToFunction[index] = node;
}

gint GstOnnxClient::getOutputNodeIndex (GstMlOutputNodeFunction node)
{
    return outputNodeInfo[node].index;
}

void GstOnnxClient::setOutputNodeType (GstMlOutputNodeFunction node,
      ONNXTensorElementDataType type)
{
    outputNodeInfo[node].type = type;
}

ONNXTensorElementDataType
      GstOnnxClient::getOutputNodeType (GstMlOutputNodeFunction node)
{
    return outputNodeInfo[node].type;
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
      // for debugging
      //sessionOptions.SetIntraOpNumThreads (1);
      sessionOptions.SetGraphOptimizationLevel (onnx_optim);
      m_provider = provider;
      switch (m_provider) {
        case GST_ONNX_EXECUTION_PROVIDER_CUDA:
#ifdef GST_ML_ONNX_RUNTIME_HAVE_CUDA
          Ort::ThrowOnError (OrtSessionOptionsAppendExecutionProvider_CUDA
              (sessionOptions, 0));
#else
          GST_ERROR ("ONNX CUDA execution provider not supported");
          return false;
#endif
          break;
        default:
          break;

      };
      session =
          new Ort::Session (getEnv (), modelFile.c_str (), sessionOptions);
      auto inputTypeInfo = session->GetInputTypeInfo (0);
      std::vector < int64_t > inputDims =
          inputTypeInfo.GetTensorTypeAndShapeInfo ().GetShape ();
      if (inputImageFormat == GST_ML_MODEL_INPUT_IMAGE_FORMAT_HWC) {
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
        auto type_info = session->GetOutputTypeInfo (i);
        auto tensor_info = type_info.GetTensorTypeAndShapeInfo ();

        if (i < GST_ML_OUTPUT_NODE_NUMBER_OF) {
          auto function = outputNodeIndexToFunction[i];
          outputNodeInfo[function].type = tensor_info.GetElementType ();
        }
      }
    }
    catch (Ort::Exception & ortex) {
      GST_ERROR ("%s", ortex.what ());
      return false;
    }

    return true;
}

std::vector < GstMlBoundingBox > GstOnnxClient::run (uint8_t * img_data,
      GstVideoMeta * vmeta, std::string labelPath, float scoreThreshold)
{
    auto type = getOutputNodeType (GST_ML_OUTPUT_NODE_FUNCTION_CLASS);
    return (type ==
        ONNXTensorElementDataType::ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) ?
          doRun < float >(img_data, vmeta, labelPath, scoreThreshold)
            : doRun < int >(img_data, vmeta, labelPath, scoreThreshold);
}

void GstOnnxClient::parseDimensions (GstVideoMeta * vmeta)
{
    int32_t newWidth = fixedInputImageSize ? width : vmeta->width;
    int32_t newHeight = fixedInputImageSize ? height : vmeta->height;

    if (!dest || width * height < newWidth * newHeight) {
      delete[] dest;
      dest = new uint8_t[newWidth * newHeight * channels];
    }
    width = newWidth;
    height = newHeight;
}

template < typename T > std::vector < GstMlBoundingBox >
      GstOnnxClient::doRun (uint8_t * img_data, GstVideoMeta * vmeta,
      std::string labelPath, float scoreThreshold)
{
    std::vector < GstMlBoundingBox > boundingBoxes;
    if (!img_data)
      return boundingBoxes;

    parseDimensions (vmeta);

    Ort::AllocatorWithDefaultOptions allocator;
    auto inputName = session->GetInputNameAllocated (0, allocator);
    auto inputTypeInfo = session->GetInputTypeInfo (0);
    std::vector < int64_t > inputDims =
        inputTypeInfo.GetTensorTypeAndShapeInfo ().GetShape ();
    inputDims[0] = 1;
    if (inputImageFormat == GST_ML_MODEL_INPUT_IMAGE_FORMAT_HWC) {
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
    switch (vmeta->format) {
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
    uint32_t stride = vmeta->stride[0];
    if (inputImageFormat == GST_ML_MODEL_INPUT_IMAGE_FORMAT_HWC) {
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
    std::vector < const char *>inputNames { inputName.get () };

    std::vector < Ort::Value > modelOutput = session->Run (Ort::RunOptions { nullptr},
        inputNames.data (),
        inputTensors.data (), 1, outputNamesRaw.data (), outputNamesRaw.size ());

    auto numDetections =
        modelOutput[getOutputNodeIndex
        (GST_ML_OUTPUT_NODE_FUNCTION_DETECTION)].GetTensorMutableData < float >();
    auto bboxes =
        modelOutput[getOutputNodeIndex
        (GST_ML_OUTPUT_NODE_FUNCTION_BOUNDING_BOX)].GetTensorMutableData < float >();
    auto scores =
        modelOutput[getOutputNodeIndex
        (GST_ML_OUTPUT_NODE_FUNCTION_SCORE)].GetTensorMutableData < float >();
    T *labelIndex = nullptr;
    if (getOutputNodeIndex (GST_ML_OUTPUT_NODE_FUNCTION_CLASS) !=
        GST_ML_NODE_INDEX_DISABLED) {
      labelIndex =
          modelOutput[getOutputNodeIndex
          (GST_ML_OUTPUT_NODE_FUNCTION_CLASS)].GetTensorMutableData < T > ();
    }
    if (labels.empty () && !labelPath.empty ())
      labels = ReadLabels (labelPath);

    for (int i = 0; i < numDetections[0]; ++i) {
      if (scores[i] > scoreThreshold) {
        std::string label = "";

        if (labelIndex && !labels.empty ())
          label = labels[labelIndex[i] - 1];
        auto score = scores[i];
        auto y0 = bboxes[i * 4] * height;
        auto x0 = bboxes[i * 4 + 1] * width;
        auto bheight = bboxes[i * 4 + 2] * height - y0;
        auto bwidth = bboxes[i * 4 + 3] * width - x0;
        boundingBoxes.push_back (GstMlBoundingBox (label, score, x0, y0, bwidth,
                bheight));
      }
    }
    return boundingBoxes;
}

std::vector < std::string >
    GstOnnxClient::ReadLabels (const std::string & labelsFile)
{
    std::vector < std::string > labels;
    std::string line;
    std::ifstream fp (labelsFile);
    while (std::getline (fp, line))
      labels.push_back (line);

    return labels;
  }
}
