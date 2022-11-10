/*
 * GStreamer gstreamer-onnxclient
 * Copyright (C) 2021 Collabora Ltd
 *
 * gstonnxclient.h
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
#ifndef __GST_ONNX_CLIENT_H__
#define __GST_ONNX_CLIENT_H__

#include <gst/gst.h>
#include <onnxruntime_cxx_api.h>
#include <gst/video/video.h>
#include "gstonnxelement.h"
#include <string>
#include <vector>

namespace GstOnnxNamespace {
  enum GstMlOutputNodeFunction {
    GST_ML_OUTPUT_NODE_FUNCTION_DETECTION,
    GST_ML_OUTPUT_NODE_FUNCTION_BOUNDING_BOX,
    GST_ML_OUTPUT_NODE_FUNCTION_SCORE,
    GST_ML_OUTPUT_NODE_FUNCTION_CLASS,
    GST_ML_OUTPUT_NODE_NUMBER_OF,
  };

  const gint GST_ML_NODE_INDEX_DISABLED = -1;

  struct GstMlOutputNodeInfo {
    GstMlOutputNodeInfo(void);
	gint index;
    ONNXTensorElementDataType type;
  };

  struct GstMlBoundingBox {
    GstMlBoundingBox(std::string lbl,
                     float score,
                     float _x0,
                     float _y0,
                     float _width,
                     float _height):label(lbl),
      score(score), x0(_x0), y0(_y0), width(_width), height(_height) {
    }
    GstMlBoundingBox():GstMlBoundingBox("", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f) {
    }
    std::string label;
    float score;
    float x0;
    float y0;
    float width;
    float height;
  };

  class GstOnnxClient {
  public:
    GstOnnxClient(void);
    ~GstOnnxClient(void);
    bool createSession(std::string modelFile, GstOnnxOptimizationLevel optim,
                       GstOnnxExecutionProvider provider);
    bool hasSession(void);
    void setInputImageFormat(GstMlModelInputImageFormat format);
    GstMlModelInputImageFormat getInputImageFormat(void);
    void setOutputNodeIndex(GstMlOutputNodeFunction nodeType, gint index);
    gint getOutputNodeIndex(GstMlOutputNodeFunction nodeType);
    void setOutputNodeType(GstMlOutputNodeFunction nodeType,
                           ONNXTensorElementDataType type);
    ONNXTensorElementDataType getOutputNodeType(GstMlOutputNodeFunction type);
    std::string getOutputNodeName(GstMlOutputNodeFunction nodeType);
    std::vector < GstMlBoundingBox > run(uint8_t * img_data,
                                          GstVideoMeta * vmeta,
                                          std::string labelPath,
                                          float scoreThreshold);
    std::vector < GstMlBoundingBox > &getBoundingBoxes(void);
    std::vector < const char *>getOutputNodeNames(void);
    bool isFixedInputImageSize(void);
    int32_t getWidth(void);
    int32_t getHeight(void);
  private:
    void parseDimensions(GstVideoMeta * vmeta);
    template < typename T > std::vector < GstMlBoundingBox >
    doRun(uint8_t * img_data, GstVideoMeta * vmeta, std::string labelPath,
            float scoreThreshold);
    std::vector < std::string > ReadLabels(const std::string & labelsFile);
    Ort::Env & getEnv(void);
    Ort::Session * session;
    int32_t width;
    int32_t height;
    int32_t channels;
    uint8_t *dest;
    GstOnnxExecutionProvider m_provider;
    std::vector < Ort::Value > modelOutput;
    std::vector < std::string > labels;
    // !! indexed by function
    GstMlOutputNodeInfo outputNodeInfo[GST_ML_OUTPUT_NODE_NUMBER_OF];
    // !! indexed by array index
	size_t outputNodeIndexToFunction[GST_ML_OUTPUT_NODE_NUMBER_OF];
    std::vector < const char *> outputNamesRaw;
    std::vector < Ort::AllocatedStringPtr > outputNames;
    GstMlModelInputImageFormat inputImageFormat;
    bool fixedInputImageSize;
  };
}

#endif                          /* __GST_ONNX_CLIENT_H__ */
