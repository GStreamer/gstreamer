/*
 * GStreamer gstreamer-objectdetectorutils
 * Copyright (C) 2023 Collabora Ltd
 *
 * gstobjectdetectorutils.cpp
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

#include "gstobjectdetectorutils.h"

#include <gio/gio.h>


char **
read_labels (const char * labels_file)
{
  GPtrArray *array;
  GFile *file = g_file_new_for_path (labels_file);
  GFileInputStream *file_stream;
  GDataInputStream *data_stream;
  GError *error = NULL;
  gchar *line;

  file_stream = g_file_read (file, NULL, &error);
  g_object_unref (file);
  if (!file_stream) {
    GST_WARNING ("Could not open file %s: %s\n", labels_file,
		 error->message);
    g_clear_error (&error);
    return NULL;
  }

  data_stream = g_data_input_stream_new (G_INPUT_STREAM (file_stream));
  g_object_unref (file_stream);

  array = g_ptr_array_new();

  while ((line = g_data_input_stream_read_line (data_stream, NULL, NULL,
						&error)))
    g_ptr_array_add (array, line);

  g_object_unref (data_stream);

  if (error) {
    GST_WARNING ("Could not open file %s: %s", labels_file, error->message);
    g_ptr_array_free (array, TRUE);
    g_clear_error (&error);
    return NULL;
  }

  if (array->len == 0) {
    g_ptr_array_free (array, TRUE);
    return NULL;
  }

  g_ptr_array_add (array, NULL);

  return (char **) g_ptr_array_free (array, FALSE);
}


GstMlBoundingBox::GstMlBoundingBox (std::string lbl, float score, float _x0,
    float _y0, float _width, float _height):
label (lbl),
score (score),
x0 (_x0),
y0 (_y0),
width (_width),
height (_height)
{
}

GstMlBoundingBox::GstMlBoundingBox ():
GstMlBoundingBox ("", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f)
{
}

namespace GstObjectDetectorUtils
{

  GstObjectDetectorUtils::GstObjectDetectorUtils ()
  {
  }


  std::vector < GstMlBoundingBox > GstObjectDetectorUtils::run (int32_t w,
      int32_t h, GstTensorMeta * tmeta, gchar **labels,
      float scoreThreshold)
  {

    auto classIndex = gst_tensor_meta_get_index_from_id (tmeta,
        g_quark_from_static_string (GST_MODEL_OBJECT_DETECTOR_CLASSES));
    if (classIndex == GST_TENSOR_MISSING_ID) {
      GST_ERROR ("Missing class tensor id");
      return std::vector < GstMlBoundingBox > ();
    }
    auto type = tmeta->tensor[classIndex].type;
    return (type == GST_TENSOR_TYPE_FLOAT32) ?
        doRun < float >(w, h, tmeta, labels, scoreThreshold)
    : doRun < int >(w, h, tmeta, labels, scoreThreshold);
  }

  template < typename T > std::vector < GstMlBoundingBox >
      GstObjectDetectorUtils::doRun (int32_t w, int32_t h,
      GstTensorMeta * tmeta, char **labels, float scoreThreshold)
  {
    std::vector < GstMlBoundingBox > boundingBoxes;
    GstMapInfo map_info[GstObjectDetectorMaxNodes];
    GstMemory *memory[GstObjectDetectorMaxNodes] = { NULL };
    gint index;
    T *numDetections = nullptr, *bboxes = nullptr, *scores =
        nullptr, *labelIndex = nullptr;

    // number of detections
    index = gst_tensor_meta_get_index_from_id (tmeta,
        g_quark_from_static_string (GST_MODEL_OBJECT_DETECTOR_NUM_DETECTIONS));
    if (index == GST_TENSOR_MISSING_ID) {
      GST_WARNING ("Missing tensor data for tensor index %d", index);
      goto cleanup;
    }
    memory[index] = gst_buffer_peek_memory (tmeta->tensor[index].data, 0);
    if (!memory[index]) {
      GST_WARNING ("Missing tensor data for tensor index %d", index);
      goto cleanup;
    }
    if (!gst_memory_map (memory[index], map_info + index, GST_MAP_READ)) {
      GST_WARNING ("Failed to map tensor memory for index %d", index);
      goto cleanup;
    }
    numDetections = (T *) map_info[index].data;

    // bounding boxes
    index =
        gst_tensor_meta_get_index_from_id (tmeta,
					   g_quark_from_static_string(GST_MODEL_OBJECT_DETECTOR_BOXES));
    if (index == GST_TENSOR_MISSING_ID) {
      GST_WARNING ("Missing tensor data for tensor index %d", index);
      goto cleanup;
    }
    memory[index] = gst_buffer_peek_memory (tmeta->tensor[index].data, 0);
    if (!memory[index]) {
      GST_WARNING ("Failed to map tensor memory for index %d", index);
      goto cleanup;
    }
    if (!gst_memory_map (memory[index], map_info + index, GST_MAP_READ)) {
      GST_ERROR ("Failed to map GstMemory");
      goto cleanup;
    }
    bboxes = (T *) map_info[index].data;

    // scores
    index =
        gst_tensor_meta_get_index_from_id (tmeta,
        g_quark_from_static_string (GST_MODEL_OBJECT_DETECTOR_SCORES));
    if (index == GST_TENSOR_MISSING_ID) {
      GST_ERROR ("Missing scores tensor id");
      goto cleanup;
    }
    memory[index] = gst_buffer_peek_memory (tmeta->tensor[index].data, 0);
    if (!memory[index]) {
      GST_WARNING ("Missing tensor data for tensor index %d", index);
      goto cleanup;
    }
    if (!gst_memory_map (memory[index], map_info + index, GST_MAP_READ)) {
      GST_ERROR ("Failed to map GstMemory");
      goto cleanup;
    }
    scores = (T *) map_info[index].data;

    // optional label
    labelIndex = nullptr;
    index =
        gst_tensor_meta_get_index_from_id (tmeta,
        g_quark_from_static_string (GST_MODEL_OBJECT_DETECTOR_CLASSES));
    if (index != GST_TENSOR_MISSING_ID) {
      memory[index] = gst_buffer_peek_memory (tmeta->tensor[index].data, 0);
      if (!memory[index]) {
        GST_WARNING ("Missing tensor data for tensor index %d", index);
        goto cleanup;
      }
      if (!gst_memory_map (memory[index], map_info + index, GST_MAP_READ)) {
        GST_ERROR ("Failed to map GstMemory");
        goto cleanup;
      }
      labelIndex = (T *) map_info[index].data;
    }

    for (int i = 0; i < numDetections[0]; ++i) {
      if (scores[i] > scoreThreshold) {
        std::string label = "";

        if (labels && labelIndex)
          label = labels[(int)labelIndex[i] - 1];
        auto score = scores[i];
        auto y0 = bboxes[i * 4] * h;
        auto x0 = bboxes[i * 4 + 1] * w;
        auto bheight = bboxes[i * 4 + 2] * h - y0;
        auto bwidth = bboxes[i * 4 + 3] * w - x0;
        boundingBoxes.push_back (GstMlBoundingBox (label, score, x0, y0, bwidth,
                bheight));
      }
    }

  cleanup:
    for (int i = 0; i < GstObjectDetectorMaxNodes; ++i) {
      if (memory[i])
        gst_memory_unmap (memory[i], map_info + i);

    }

    return boundingBoxes;
  }

}
