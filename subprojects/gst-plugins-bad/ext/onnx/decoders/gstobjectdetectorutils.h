/*
 * GStreamer gstreamer-objectdetectorutils
 * Copyright (C) 2023 Collabora Ltd
 *
 * gstobjectdetectorutils.h
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
#ifndef __GST_OBJECT_DETECTOR_UTILS_H__
#define __GST_OBJECT_DETECTOR_UTILS_H__

#include <gst/gst.h>
#include <string>
#include <vector>

#include "gstml.h"
#include "tensor/gsttensormeta.h"

/* Object detection tensor id strings */
#define GST_MODEL_OBJECT_DETECTOR_BOXES "Gst.Model.ObjectDetector.Boxes"
#define GST_MODEL_OBJECT_DETECTOR_SCORES "Gst.Model.ObjectDetector.Scores"
#define GST_MODEL_OBJECT_DETECTOR_NUM_DETECTIONS "Gst.Model.ObjectDetector.NumDetections"
#define GST_MODEL_OBJECT_DETECTOR_CLASSES "Gst.Model.ObjectDetector.Classes"


/**
 * GstMlBoundingBox:
 *
 * @label label
 * @score detection confidence
 * @x0 top left hand x coordinate
 * @y0 top left hand y coordinate
 * @width width
 * @height height
 *
 * Since: 1.20
 */
struct GstMlBoundingBox {
	GstMlBoundingBox(std::string lbl, float score, float _x0, float _y0,
			float _width, float _height);
	GstMlBoundingBox();
	std::string label;
	float score;
	float x0;
	float y0;
	float width;
	float height;
};

namespace GstObjectDetectorUtils {
  const int GstObjectDetectorMaxNodes = 4;
  class GstObjectDetectorUtils {
  public:
	 GstObjectDetectorUtils(void);
    ~GstObjectDetectorUtils(void) = default;
    std::vector < GstMlBoundingBox > run(int32_t w, int32_t h,
    									GstTensorMeta *tmeta,
                                          std::string labelPath,
                                          float scoreThreshold);
  private:
    template < typename T > std::vector < GstMlBoundingBox >
    doRun(int32_t w, int32_t h,
    		GstTensorMeta *tmeta, std::string labelPath,
            float scoreThreshold);
    std::vector < std::string > ReadLabels(const std::string & labelsFile);
  };
}

#endif                          /* __GST_OBJECT_DETECTOR_UTILS_H__ */
