/* GStreamer unit test for YOLO tensor decoders
 *
 * Copyright (C) 2026 Collabora Ltd.
 *  @author: Olivier Crête <olivier.crete@collabora.com>
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
# include <config.h>
#endif

#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>
#include <gst/video/video.h>
#include <gst/analytics/analytics.h>

#include <math.h>
#include <string.h>

#define TEST_EPSILON 1e-4f

typedef struct
{
  gfloat x;
  gfloat y;
  gfloat w;
  gfloat h;
  const gfloat *class_confidences;
  const gfloat *extras;
} DetectionCandidate;

static gboolean
approx_equal (gfloat a, gfloat b, gfloat epsilon)
{
  return fabsf (a - b) <= epsilon;
}

static GstHarness *
setup_decoder_harness (const gchar * element_name, gint width, gint height)
{
  GstHarness *h;
  gchar *caps_str;

  h = gst_harness_new (element_name);
  gst_harness_play (h);

  caps_str = g_strdup_printf ("video/x-raw,format=RGBA,width=%d,height=%d,"
      "framerate=30/1", width, height);
  gst_harness_set_src_caps_str (h, caps_str);
  g_free (caps_str);

  return h;
}

static GstTensor *
make_detections_tensor (const gchar * tensor_id, gsize num_classes,
    gsize num_extras, const DetectionCandidate * candidates, gsize n_candidates)
{
  GstTensor *tensor;
  GstBuffer *tensor_data;
  GstMapInfo map;
  gfloat *raw_data;
  gsize dims[3];
  gsize fields;
  gsize total_values;
  gsize data_size;
  gsize c_idx;
  gsize class_idx;
  gsize extra_idx;

  fields = 4 + num_classes + num_extras;
  total_values = fields * n_candidates;
  data_size = total_values * sizeof (gfloat);

  tensor_data = gst_buffer_new_allocate (NULL, data_size, NULL);
  gst_buffer_map (tensor_data, &map, GST_MAP_WRITE);
  raw_data = (gfloat *) map.data;

  for (c_idx = 0; c_idx < n_candidates; c_idx++) {
    raw_data[c_idx] = candidates[c_idx].x;
    raw_data[n_candidates + c_idx] = candidates[c_idx].y;
    raw_data[(2 * n_candidates) + c_idx] = candidates[c_idx].w;
    raw_data[(3 * n_candidates) + c_idx] = candidates[c_idx].h;

    for (class_idx = 0; class_idx < num_classes; class_idx++) {
      raw_data[((4 + class_idx) * n_candidates) + c_idx] =
          candidates[c_idx].class_confidences[class_idx];
    }

    for (extra_idx = 0; extra_idx < num_extras; extra_idx++) {
      raw_data[((4 + num_classes + extra_idx) * n_candidates) + c_idx] =
          candidates[c_idx].extras[extra_idx];
    }
  }

  gst_buffer_unmap (tensor_data, &map);

  dims[0] = 1;
  dims[1] = fields;
  dims[2] = n_candidates;

  tensor = gst_tensor_new_simple (g_quark_from_string (tensor_id),
      GST_TENSOR_DATA_TYPE_FLOAT32, tensor_data,
      GST_TENSOR_DIM_ORDER_COL_MAJOR, 3, dims);

  return tensor;
}

static GstTensor *
make_logits_tensor (gsize num_masks, gsize mask_w, gsize mask_h,
    const gfloat * logits_values)
{
  GstTensor *tensor;
  GstBuffer *tensor_data;
  GstMapInfo map;
  gsize dims[4];
  gsize total_values;
  gsize data_size;

  total_values = num_masks * mask_w * mask_h;
  data_size = total_values * sizeof (gfloat);

  tensor_data = gst_buffer_new_allocate (NULL, data_size, NULL);
  gst_buffer_map (tensor_data, &map, GST_MAP_WRITE);
  memcpy (map.data, logits_values, data_size);
  gst_buffer_unmap (tensor_data, &map);

  dims[0] = 1;
  dims[1] = num_masks;
  dims[2] = mask_w;
  dims[3] = mask_h;

  tensor = gst_tensor_new_simple (g_quark_from_string
      ("yolo-v8-segmentation-out-protos"), GST_TENSOR_DATA_TYPE_FLOAT32,
      tensor_data, GST_TENSOR_DIM_ORDER_COL_MAJOR, 4, dims);

  return tensor;
}

static void
attach_tensors_meta (GstBuffer * buffer, GstTensor ** tensors,
    gsize num_tensors)
{
  GstTensorMeta *tmeta;
  GstTensor **meta_tensors;
  gsize i;

  tmeta = gst_buffer_add_tensor_meta (buffer);
  meta_tensors = g_new (GstTensor *, num_tensors);

  for (i = 0; i < num_tensors; i++)
    meta_tensors[i] = tensors[i];

  gst_tensor_meta_set (tmeta, num_tensors, meta_tensors);
}

static guint
count_mtd_type (GstAnalyticsRelationMeta * rmeta, GstAnalyticsMtdType type)
{
  gpointer state;
  GstAnalyticsMtd mtd;
  guint count;

  state = NULL;
  count = 0;

  while (gst_analytics_relation_meta_iterate (rmeta, &state, type, &mtd))
    count++;

  return count;
}

static guint
count_od_mtds_with_type (GstAnalyticsRelationMeta * rmeta, GQuark obj_type)
{
  gpointer state;
  GstAnalyticsODMtd od_mtd;
  guint count;

  state = NULL;
  count = 0;

  while (gst_analytics_relation_meta_iterate (rmeta, &state,
          gst_analytics_od_mtd_get_mtd_type (), &od_mtd)) {
    if (gst_analytics_od_mtd_get_obj_type (&od_mtd) == obj_type)
      count++;
  }

  return count;
}

static gboolean
find_od_by_location (GstAnalyticsRelationMeta * rmeta, gint exp_x, gint exp_y,
    gint exp_w, gint exp_h, GstAnalyticsODMtd * out_od_mtd)
{
  gpointer state;
  GstAnalyticsODMtd od_mtd;
  gint x, y, w, height;
  gfloat conf;

  state = NULL;
  while (gst_analytics_relation_meta_iterate (rmeta, &state,
          gst_analytics_od_mtd_get_mtd_type (), &od_mtd)) {
    fail_unless (gst_analytics_od_mtd_get_location (&od_mtd, &x, &y, &w,
            &height, &conf));
    if (x == exp_x && y == exp_y && w == exp_w && height == exp_h) {
      if (out_od_mtd != NULL)
        *out_od_mtd = od_mtd;
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean
get_single_od_mtd (GstAnalyticsRelationMeta * rmeta, GstAnalyticsODMtd * od_mtd)
{
  gpointer state;
  GstAnalyticsODMtd first_od_mtd;
  GstAnalyticsODMtd next_od_mtd;
  gboolean has_one;

  state = NULL;
  has_one = gst_analytics_relation_meta_iterate (rmeta, &state,
      gst_analytics_od_mtd_get_mtd_type (), &first_od_mtd);
  if (!has_one)
    return FALSE;

  if (gst_analytics_relation_meta_iterate (rmeta, &state,
          gst_analytics_od_mtd_get_mtd_type (), &next_od_mtd))
    return FALSE;

  *od_mtd = first_od_mtd;
  return TRUE;
}

static gboolean
get_single_segmentation_mtd (GstAnalyticsRelationMeta * rmeta,
    GstAnalyticsSegmentationMtd * seg_mtd)
{
  gpointer state;
  GstAnalyticsSegmentationMtd first_seg_mtd;
  GstAnalyticsSegmentationMtd next_seg_mtd;
  gboolean has_one;

  state = NULL;
  has_one = gst_analytics_relation_meta_iterate (rmeta, &state,
      gst_analytics_segmentation_mtd_get_mtd_type (), &first_seg_mtd);
  if (!has_one)
    return FALSE;

  if (gst_analytics_relation_meta_iterate (rmeta, &state,
          gst_analytics_segmentation_mtd_get_mtd_type (), &next_seg_mtd))
    return FALSE;

  *seg_mtd = first_seg_mtd;
  return TRUE;
}

static gchar *
get_labels_path (void)
{
  return g_build_filename (GST_TENSORDECODERS_TEST_DATA_PATH, "yolo_labels.txt",
      NULL);
}

/* Test that a single valid detection candidate produces one OD metadata entry with expected bbox and confidence */
GST_START_TEST (test_basic_single_detection)
{
  GstHarness *h;
  GstBuffer *inbuf;
  GstBuffer *outbuf;
  GstAnalyticsRelationMeta *rmeta;
  GstAnalyticsODMtd od_mtd;
  GstTensor *detections;
  GstTensor *tensors[1];
  DetectionCandidate candidate;
  gfloat class_confidences[1];
  gint x, y, w, height;
  gfloat confidence;

  class_confidences[0] = 0.9f;
  candidate.x = 320.0f;
  candidate.y = 240.0f;
  candidate.w = 100.0f;
  candidate.h = 50.0f;
  candidate.class_confidences = class_confidences;
  candidate.extras = NULL;

  detections = make_detections_tensor ("yolo-v8-out", 1, 0, &candidate, 1);
  tensors[0] = detections;

  inbuf = gst_buffer_new ();
  attach_tensors_meta (inbuf, tensors, 1);

  h = setup_decoder_harness ("yolov8tensordec", 640, 480);
  outbuf = gst_harness_push_and_pull (h, inbuf);
  fail_unless (outbuf != NULL);

  rmeta = gst_buffer_get_analytics_relation_meta (outbuf);
  fail_unless (rmeta != NULL);
  fail_unless_equals_int (count_mtd_type (rmeta,
          gst_analytics_od_mtd_get_mtd_type ()), 1);
  fail_unless (get_single_od_mtd (rmeta, &od_mtd));
  fail_unless (gst_analytics_od_mtd_get_location (&od_mtd, &x, &y, &w, &height,
          &confidence));
  fail_unless_equals_int (x, 270);
  fail_unless_equals_int (y, 215);
  fail_unless_equals_int (w, 100);
  fail_unless_equals_int (height, 50);
  fail_unless (approx_equal (confidence, 0.9f, TEST_EPSILON));
  fail_unless (gst_analytics_od_mtd_get_obj_type (&od_mtd) ==
      g_quark_from_static_string ("Yolo-None"));

  gst_buffer_unref (outbuf);
  gst_harness_teardown (h);
}

GST_END_TEST;

/* Test that candidates below the class-confidence-threshold are filtered out */
GST_START_TEST (test_below_confidence_threshold_dropped)
{
  GstHarness *h;
  GstBuffer *inbuf;
  GstBuffer *outbuf;
  GstAnalyticsRelationMeta *rmeta;
  GstTensor *detections;
  GstTensor *tensors[1];
  DetectionCandidate candidate;
  gfloat class_confidences[1];

  class_confidences[0] = 0.6f;
  candidate.x = 200.0f;
  candidate.y = 100.0f;
  candidate.w = 40.0f;
  candidate.h = 20.0f;
  candidate.class_confidences = class_confidences;
  candidate.extras = NULL;

  detections = make_detections_tensor ("yolo-v8-out", 1, 0, &candidate, 1);
  tensors[0] = detections;

  inbuf = gst_buffer_new ();
  attach_tensors_meta (inbuf, tensors, 1);

  h = setup_decoder_harness ("yolov8tensordec", 640, 480);
  g_object_set (h->element, "class-confidence-threshold", 0.8f, NULL);

  outbuf = gst_harness_push_and_pull (h, inbuf);
  fail_unless (outbuf != NULL);

  rmeta = gst_buffer_get_analytics_relation_meta (outbuf);
  fail_unless (rmeta != NULL);
  fail_unless_equals_int (gst_analytics_relation_get_length (rmeta), 0);
  fail_unless_equals_int (count_mtd_type (rmeta,
          gst_analytics_od_mtd_get_mtd_type ()), 0);

  gst_buffer_unref (outbuf);
  gst_harness_teardown (h);
}

GST_END_TEST;

/* Test that the highest-confidence class is selected and mapped to the expected label */
GST_START_TEST (test_multiple_classes_pick_max)
{
  GstHarness *h;
  GstBuffer *inbuf;
  GstBuffer *outbuf;
  GstAnalyticsRelationMeta *rmeta;
  GstAnalyticsODMtd od_mtd;
  GstTensor *detections;
  GstTensor *tensors[1];
  DetectionCandidate candidate;
  gfloat class_confidences[4];
  gchar *labels_path;

  class_confidences[0] = 0.1f;
  class_confidences[1] = 0.8f;
  class_confidences[2] = 0.2f;
  class_confidences[3] = 0.3f;
  candidate.x = 120.0f;
  candidate.y = 80.0f;
  candidate.w = 40.0f;
  candidate.h = 20.0f;
  candidate.class_confidences = class_confidences;
  candidate.extras = NULL;

  detections = make_detections_tensor ("yolo-v8-out", 4, 0, &candidate, 1);
  tensors[0] = detections;
  inbuf = gst_buffer_new ();
  attach_tensors_meta (inbuf, tensors, 1);

  labels_path = get_labels_path ();

  h = setup_decoder_harness ("yolov8tensordec", 640, 480);
  g_object_set (h->element, "label-file", labels_path, NULL);

  outbuf = gst_harness_push_and_pull (h, inbuf);
  fail_unless (outbuf != NULL);
  rmeta = gst_buffer_get_analytics_relation_meta (outbuf);
  fail_unless (rmeta != NULL);
  fail_unless (get_single_od_mtd (rmeta, &od_mtd));
  fail_unless (gst_analytics_od_mtd_get_obj_type (&od_mtd) ==
      g_quark_from_static_string ("car"));

  gst_buffer_unref (outbuf);
  gst_harness_teardown (h);
  g_free (labels_path);
}

GST_END_TEST;

/* Test that NMS removes a lower-confidence overlapping candidate */
GST_START_TEST (test_nms_drops_overlapping_lower_confidence)
{
  GstHarness *h;
  GstBuffer *inbuf;
  GstBuffer *outbuf;
  GstAnalyticsRelationMeta *rmeta;
  GstTensor *detections;
  GstTensor *tensors[1];
  DetectionCandidate candidates[2];
  gfloat class_0[1];
  gfloat class_1[1];
  gboolean has_first;
  gboolean has_second;

  class_0[0] = 0.95f;
  class_1[0] = 0.80f;

  candidates[0].x = 100.0f;
  candidates[0].y = 100.0f;
  candidates[0].w = 40.0f;
  candidates[0].h = 40.0f;
  candidates[0].class_confidences = class_0;
  candidates[0].extras = NULL;

  candidates[1].x = 105.0f;
  candidates[1].y = 105.0f;
  candidates[1].w = 40.0f;
  candidates[1].h = 40.0f;
  candidates[1].class_confidences = class_1;
  candidates[1].extras = NULL;

  detections = make_detections_tensor ("yolo-v8-out", 1, 0, candidates, 2);
  tensors[0] = detections;
  inbuf = gst_buffer_new ();
  attach_tensors_meta (inbuf, tensors, 1);

  h = setup_decoder_harness ("yolov8tensordec", 640, 480);
  g_object_set (h->element, "iou-threshold", 0.3f, NULL);

  outbuf = gst_harness_push_and_pull (h, inbuf);
  fail_unless (outbuf != NULL);
  rmeta = gst_buffer_get_analytics_relation_meta (outbuf);
  fail_unless (rmeta != NULL);
  fail_unless_equals_int (count_mtd_type (rmeta,
          gst_analytics_od_mtd_get_mtd_type ()), 1);

  has_first = find_od_by_location (rmeta, 80, 80, 40, 40, NULL);
  has_second = find_od_by_location (rmeta, 85, 85, 40, 40, NULL);
  fail_unless (has_first);
  fail_if (has_second);

  gst_buffer_unref (outbuf);
  gst_harness_teardown (h);
}

GST_END_TEST;

/* Test that NMS keeps distant candidates */
GST_START_TEST (test_nms_keeps_distant_boxes)
{
  GstHarness *h;
  GstBuffer *inbuf;
  GstBuffer *outbuf;
  GstAnalyticsRelationMeta *rmeta;
  GstTensor *detections;
  GstTensor *tensors[1];
  DetectionCandidate candidates[2];
  gfloat class_0[1];
  gfloat class_1[1];

  class_0[0] = 0.90f;
  class_1[0] = 0.88f;

  candidates[0].x = 100.0f;
  candidates[0].y = 100.0f;
  candidates[0].w = 30.0f;
  candidates[0].h = 30.0f;
  candidates[0].class_confidences = class_0;
  candidates[0].extras = NULL;

  candidates[1].x = 240.0f;
  candidates[1].y = 240.0f;
  candidates[1].w = 30.0f;
  candidates[1].h = 30.0f;
  candidates[1].class_confidences = class_1;
  candidates[1].extras = NULL;

  detections = make_detections_tensor ("yolo-v8-out", 1, 0, candidates, 2);
  tensors[0] = detections;
  inbuf = gst_buffer_new ();
  attach_tensors_meta (inbuf, tensors, 1);

  h = setup_decoder_harness ("yolov8tensordec", 640, 480);
  g_object_set (h->element, "iou-threshold", 0.3f, NULL);

  outbuf = gst_harness_push_and_pull (h, inbuf);
  fail_unless (outbuf != NULL);
  rmeta = gst_buffer_get_analytics_relation_meta (outbuf);
  fail_unless (rmeta != NULL);
  fail_unless_equals_int (count_mtd_type (rmeta,
          gst_analytics_od_mtd_get_mtd_type ()), 2);
  fail_unless (find_od_by_location (rmeta, 85, 85, 30, 30, NULL));
  fail_unless (find_od_by_location (rmeta, 225, 225, 30, 30, NULL));

  gst_buffer_unref (outbuf);
  gst_harness_teardown (h);
}

GST_END_TEST;

/* Test that max-detections caps the number of selected candidates */
GST_START_TEST (test_max_detections_limit)
{
  GstHarness *h;
  GstBuffer *inbuf;
  GstBuffer *outbuf;
  GstAnalyticsRelationMeta *rmeta;
  GstTensor *detections;
  GstTensor *tensors[1];
  DetectionCandidate candidates[3];
  gfloat class_0[1];
  gfloat class_1[1];
  gfloat class_2[1];

  class_0[0] = 0.95f;
  class_1[0] = 0.85f;
  class_2[0] = 0.75f;

  candidates[0].x = 100.0f;
  candidates[0].y = 100.0f;
  candidates[0].w = 30.0f;
  candidates[0].h = 30.0f;
  candidates[0].class_confidences = class_0;
  candidates[0].extras = NULL;

  candidates[1].x = 200.0f;
  candidates[1].y = 200.0f;
  candidates[1].w = 30.0f;
  candidates[1].h = 30.0f;
  candidates[1].class_confidences = class_1;
  candidates[1].extras = NULL;

  candidates[2].x = 300.0f;
  candidates[2].y = 300.0f;
  candidates[2].w = 30.0f;
  candidates[2].h = 30.0f;
  candidates[2].class_confidences = class_2;
  candidates[2].extras = NULL;

  detections = make_detections_tensor ("yolo-v8-out", 1, 0, candidates, 3);
  tensors[0] = detections;
  inbuf = gst_buffer_new ();
  attach_tensors_meta (inbuf, tensors, 1);

  h = setup_decoder_harness ("yolov8tensordec", 640, 480);
  g_object_set (h->element, "max-detections", 2u, NULL);

  outbuf = gst_harness_push_and_pull (h, inbuf);
  fail_unless (outbuf != NULL);
  rmeta = gst_buffer_get_analytics_relation_meta (outbuf);
  fail_unless (rmeta != NULL);
  fail_unless_equals_int (count_mtd_type (rmeta,
          gst_analytics_od_mtd_get_mtd_type ()), 2);
  fail_unless (find_od_by_location (rmeta, 85, 85, 30, 30, NULL));
  fail_unless (find_od_by_location (rmeta, 185, 185, 30, 30, NULL));
  fail_if (find_od_by_location (rmeta, 285, 285, 30, 30, NULL));

  gst_buffer_unref (outbuf);
  gst_harness_teardown (h);
}

GST_END_TEST;

/* Test that label-file mapping is applied for multiple class winners */
GST_START_TEST (test_label_file_applied)
{
  GstHarness *h;
  GstBuffer *inbuf;
  GstBuffer *outbuf;
  GstAnalyticsRelationMeta *rmeta;
  GstTensor *detections;
  GstTensor *tensors[1];
  DetectionCandidate candidates[2];
  gfloat class_0[4];
  gfloat class_1[4];
  gchar *labels_path;
  guint person_count;
  guint dog_count;

  class_0[0] = 0.95f;
  class_0[1] = 0.1f;
  class_0[2] = 0.05f;
  class_0[3] = 0.01f;

  class_1[0] = 0.1f;
  class_1[1] = 0.2f;
  class_1[2] = 0.91f;
  class_1[3] = 0.3f;

  candidates[0].x = 80.0f;
  candidates[0].y = 80.0f;
  candidates[0].w = 20.0f;
  candidates[0].h = 20.0f;
  candidates[0].class_confidences = class_0;
  candidates[0].extras = NULL;

  candidates[1].x = 160.0f;
  candidates[1].y = 160.0f;
  candidates[1].w = 20.0f;
  candidates[1].h = 20.0f;
  candidates[1].class_confidences = class_1;
  candidates[1].extras = NULL;

  detections = make_detections_tensor ("yolo-v8-out", 4, 0, candidates, 2);
  tensors[0] = detections;
  inbuf = gst_buffer_new ();
  attach_tensors_meta (inbuf, tensors, 1);

  labels_path = get_labels_path ();

  h = setup_decoder_harness ("yolov8tensordec", 640, 480);
  g_object_set (h->element, "label-file", labels_path, NULL);

  outbuf = gst_harness_push_and_pull (h, inbuf);
  fail_unless (outbuf != NULL);
  rmeta = gst_buffer_get_analytics_relation_meta (outbuf);
  fail_unless (rmeta != NULL);
  fail_unless_equals_int (count_mtd_type (rmeta,
          gst_analytics_od_mtd_get_mtd_type ()), 2);

  person_count = count_od_mtds_with_type (rmeta, g_quark_from_static_string
      ("person"));
  dog_count = count_od_mtds_with_type (rmeta, g_quark_from_static_string
      ("dog"));
  fail_unless_equals_int (person_count, 1);
  fail_unless_equals_int (dog_count, 1);

  gst_buffer_unref (outbuf);
  gst_harness_teardown (h);
  g_free (labels_path);
}

GST_END_TEST;

/* Test that invalid candidates are rejected while valid ones remain */
GST_START_TEST (test_invalid_bbox_rejected)
{
  GstHarness *h;
  GstBuffer *inbuf;
  GstBuffer *outbuf;
  GstAnalyticsRelationMeta *rmeta;
  GstTensor *detections;
  GstTensor *tensors[1];
  DetectionCandidate candidates[3];
  gfloat class_0[1];
  gfloat class_1[1];
  gfloat class_2[1];

  class_0[0] = 0.95f;
  class_1[0] = 0.90f;
  class_2[0] = 0.92f;

  candidates[0].x = 120.0f;
  candidates[0].y = 120.0f;
  candidates[0].w = 0.0f;
  candidates[0].h = 40.0f;
  candidates[0].class_confidences = class_0;
  candidates[0].extras = NULL;

  candidates[1].x = 1200.0f;
  candidates[1].y = 120.0f;
  candidates[1].w = 30.0f;
  candidates[1].h = 30.0f;
  candidates[1].class_confidences = class_1;
  candidates[1].extras = NULL;

  candidates[2].x = 320.0f;
  candidates[2].y = 200.0f;
  candidates[2].w = 50.0f;
  candidates[2].h = 60.0f;
  candidates[2].class_confidences = class_2;
  candidates[2].extras = NULL;

  detections = make_detections_tensor ("yolo-v8-out", 1, 0, candidates, 3);
  tensors[0] = detections;
  inbuf = gst_buffer_new ();
  attach_tensors_meta (inbuf, tensors, 1);

  h = setup_decoder_harness ("yolov8tensordec", 640, 480);
  outbuf = gst_harness_push_and_pull (h, inbuf);
  fail_unless (outbuf != NULL);

  rmeta = gst_buffer_get_analytics_relation_meta (outbuf);
  fail_unless (rmeta != NULL);
  fail_unless_equals_int (count_mtd_type (rmeta,
          gst_analytics_od_mtd_get_mtd_type ()), 1);
  fail_unless (find_od_by_location (rmeta, 295, 170, 50, 60, NULL));

  gst_buffer_unref (outbuf);
  gst_harness_teardown (h);
}

GST_END_TEST;

/* Test that buffers without tensor meta pass through without analytics relation meta */
GST_START_TEST (test_missing_tensor_meta)
{
  GstHarness *h;
  GstBuffer *inbuf;
  GstBuffer *outbuf;
  GstAnalyticsRelationMeta *rmeta;

  h = setup_decoder_harness ("yolov8tensordec", 640, 480);
  inbuf = gst_buffer_new ();

  outbuf = gst_harness_push_and_pull (h, inbuf);
  fail_unless (outbuf != NULL);

  rmeta = gst_buffer_get_analytics_relation_meta (outbuf);
  fail_unless (rmeta == NULL);

  gst_buffer_unref (outbuf);
  gst_harness_teardown (h);
}

GST_END_TEST;

/* Test that tensors with fewer than five fields are ignored */
GST_START_TEST (test_tensor_dims_too_small)
{
  GstHarness *h;
  GstBuffer *inbuf;
  GstBuffer *outbuf;
  GstAnalyticsRelationMeta *rmeta;
  GstTensor *detections;
  GstTensor *tensors[1];
  DetectionCandidate candidate;

  candidate.x = 120.0f;
  candidate.y = 120.0f;
  candidate.w = 40.0f;
  candidate.h = 20.0f;
  candidate.class_confidences = NULL;
  candidate.extras = NULL;

  detections = make_detections_tensor ("yolo-v8-out", 0, 0, &candidate, 1);
  tensors[0] = detections;

  inbuf = gst_buffer_new ();
  attach_tensors_meta (inbuf, tensors, 1);

  h = setup_decoder_harness ("yolov8tensordec", 640, 480);
  outbuf = gst_harness_push_and_pull (h, inbuf);
  fail_unless (outbuf != NULL);

  rmeta = gst_buffer_get_analytics_relation_meta (outbuf);
  fail_unless (rmeta == NULL);

  gst_buffer_unref (outbuf);
  gst_harness_teardown (h);
}

GST_END_TEST;

/* Test that decoder properties can be set and read back */
GST_START_TEST (test_properties_roundtrip)
{
  GstElement *element;
  gchar *labels_path;
  gchar *label_file = NULL;
  gfloat box_conf;
  gfloat class_conf;
  gfloat iou;
  guint max_det;

  element = gst_element_factory_make ("yolov8tensordec", NULL);
  fail_unless (element != NULL);

  labels_path = get_labels_path ();

  g_object_set (element,
      "box-confidence-threshold", 0.51f,
      "class-confidence-threshold", 0.62f,
      "iou-threshold", 0.33f, "max-detections", 17u,
      "label-file", labels_path, NULL);

  g_object_get (element,
      "box-confidence-threshold", &box_conf,
      "class-confidence-threshold", &class_conf,
      "iou-threshold", &iou,
      "max-detections", &max_det, "label-file", &label_file, NULL);

  fail_unless (approx_equal (box_conf, 0.51f, TEST_EPSILON));
  fail_unless (approx_equal (class_conf, 0.62f, TEST_EPSILON));
  fail_unless (approx_equal (iou, 0.33f, TEST_EPSILON));
  fail_unless_equals_int (max_det, 17);
  fail_unless (g_strcmp0 (label_file, labels_path) == 0);

  g_free (label_file);
  g_free (labels_path);
  gst_object_unref (element);
}

GST_END_TEST;

/* Test accept-caps behavior for yolov8tensordec sink pad */
GST_START_TEST (test_accept_caps)
{
  GstHarness *h;
  GstPad *sinkpad;
  GstCaps *template_caps;
  GstCaps *plain_video_caps;
  GstCaps *audio_caps;

  h = gst_harness_new ("yolov8tensordec");
  sinkpad = gst_element_get_static_pad (h->element, "sink");

  template_caps = gst_pad_get_pad_template_caps (sinkpad);
  template_caps = gst_caps_fixate (template_caps);
  fail_unless (gst_caps_is_fixed (template_caps));
  fail_unless (gst_pad_query_accept_caps (sinkpad, template_caps));

  plain_video_caps =
      gst_caps_from_string
      ("video/x-raw,format=RGBA,width=640,height=480,framerate=30/1");
  fail_unless (gst_pad_query_accept_caps (sinkpad, plain_video_caps));

  audio_caps = gst_caps_from_string ("audio/x-raw,format=S16LE,rate=48000");
  fail_if (gst_pad_query_accept_caps (sinkpad, audio_caps));

  gst_caps_unref (template_caps);
  gst_caps_unref (plain_video_caps);
  gst_caps_unref (audio_caps);
  gst_object_unref (sinkpad);
  gst_harness_teardown (h);
}

GST_END_TEST;

/* Test that OBB decoder stores oriented bounding boxes with rotation angle */
GST_START_TEST (test_obb_basic_decode)
{
  GstHarness *h;
  GstBuffer *inbuf;
  GstBuffer *outbuf;
  GstAnalyticsRelationMeta *rmeta;
  GstAnalyticsODMtd od_mtd;
  GstTensor *detections;
  GstTensor *tensors[1];
  DetectionCandidate candidate;
  gfloat class_confidences[1];
  gfloat extras[1];
  gint x, y, w, height;
  gfloat confidence;
  gfloat rotation;

  class_confidences[0] = 0.9f;
  extras[0] = 0.5f;

  candidate.x = 150.0f;
  candidate.y = 120.0f;
  candidate.w = 40.0f;
  candidate.h = 20.0f;
  candidate.class_confidences = class_confidences;
  candidate.extras = extras;

  detections = make_detections_tensor ("yolo-v8-obb-out", 1, 1, &candidate, 1);
  tensors[0] = detections;
  inbuf = gst_buffer_new ();
  attach_tensors_meta (inbuf, tensors, 1);

  h = setup_decoder_harness ("yoloobbv8tensordec", 300, 300);
  outbuf = gst_harness_push_and_pull (h, inbuf);
  fail_unless (outbuf != NULL);

  rmeta = gst_buffer_get_analytics_relation_meta (outbuf);
  fail_unless (rmeta != NULL);
  fail_unless_equals_int (count_mtd_type (rmeta,
          gst_analytics_od_mtd_get_mtd_type ()), 1);
  fail_unless (get_single_od_mtd (rmeta, &od_mtd));
  fail_unless (gst_analytics_od_mtd_get_oriented_location (&od_mtd, &x, &y, &w,
          &height, &rotation, &confidence));
  fail_unless_equals_int (x, 130);
  fail_unless_equals_int (y, 110);
  fail_unless_equals_int (w, 40);
  fail_unless_equals_int (height, 20);
  fail_unless (approx_equal (rotation, 0.5f, TEST_EPSILON));
  fail_unless (approx_equal (confidence, 0.9f, TEST_EPSILON));

  gst_buffer_unref (outbuf);
  gst_harness_teardown (h);
}

GST_END_TEST;

/* Test that OBB tensors with fewer than six fields are ignored */
GST_START_TEST (test_obb_dims_too_small)
{
  GstHarness *h;
  GstBuffer *inbuf;
  GstBuffer *outbuf;
  GstAnalyticsRelationMeta *rmeta;
  GstTensor *detections;
  GstTensor *tensors[1];
  DetectionCandidate candidate;
  gfloat class_confidences[1];

  class_confidences[0] = 0.9f;
  candidate.x = 100.0f;
  candidate.y = 100.0f;
  candidate.w = 40.0f;
  candidate.h = 20.0f;
  candidate.class_confidences = class_confidences;
  candidate.extras = NULL;

  detections = make_detections_tensor ("yolo-v8-obb-out", 1, 0, &candidate, 1);
  tensors[0] = detections;
  inbuf = gst_buffer_new ();
  attach_tensors_meta (inbuf, tensors, 1);

  h = setup_decoder_harness ("yoloobbv8tensordec", 300, 300);
  outbuf = gst_harness_push_and_pull (h, inbuf);
  fail_unless (outbuf != NULL);

  rmeta = gst_buffer_get_analytics_relation_meta (outbuf);
  fail_unless (rmeta == NULL);

  gst_buffer_unref (outbuf);
  gst_harness_teardown (h);
}

GST_END_TEST;

/* Test that OBB NMS uses polygon IoU and keeps orthogonal overlapping boxes */
GST_START_TEST (test_obb_polygon_iou_nms)
{
  GstHarness *h;
  GstBuffer *inbuf;
  GstBuffer *outbuf;
  GstAnalyticsRelationMeta *rmeta;
  GstTensor *detections;
  GstTensor *tensors[1];
  DetectionCandidate candidates[2];
  gfloat class_0[1];
  gfloat class_1[1];
  gfloat extra_0[1];
  gfloat extra_1[1];
  gpointer state;
  GstAnalyticsODMtd od_mtd;
  gint x, y, w, height;
  gfloat confidence;
  gfloat rotation;
  guint seen_r0;
  guint seen_r90;

  class_0[0] = 0.9f;
  class_1[0] = 0.8f;
  extra_0[0] = 0.0f;
  extra_1[0] = (gfloat) (G_PI / 2.0);

  candidates[0].x = 100.0f;
  candidates[0].y = 100.0f;
  candidates[0].w = 60.0f;
  candidates[0].h = 10.0f;
  candidates[0].class_confidences = class_0;
  candidates[0].extras = extra_0;

  candidates[1].x = 100.0f;
  candidates[1].y = 100.0f;
  candidates[1].w = 60.0f;
  candidates[1].h = 10.0f;
  candidates[1].class_confidences = class_1;
  candidates[1].extras = extra_1;

  detections = make_detections_tensor ("yolo-v8-obb-out", 1, 1, candidates, 2);
  tensors[0] = detections;
  inbuf = gst_buffer_new ();
  attach_tensors_meta (inbuf, tensors, 1);

  h = setup_decoder_harness ("yoloobbv8tensordec", 300, 300);
  g_object_set (h->element, "iou-threshold", 0.3f, NULL);

  outbuf = gst_harness_push_and_pull (h, inbuf);
  fail_unless (outbuf != NULL);
  rmeta = gst_buffer_get_analytics_relation_meta (outbuf);
  fail_unless (rmeta != NULL);
  fail_unless_equals_int (count_mtd_type (rmeta,
          gst_analytics_od_mtd_get_mtd_type ()), 2);

  seen_r0 = 0;
  seen_r90 = 0;
  state = NULL;
  while (gst_analytics_relation_meta_iterate (rmeta, &state,
          gst_analytics_od_mtd_get_mtd_type (), &od_mtd)) {
    fail_unless (gst_analytics_od_mtd_get_oriented_location (&od_mtd, &x, &y,
            &w, &height, &rotation, &confidence));
    fail_unless_equals_int (x, 70);
    fail_unless_equals_int (y, 95);
    fail_unless_equals_int (w, 60);
    fail_unless_equals_int (height, 10);
    if (approx_equal (rotation, 0.0f, 1e-3f))
      seen_r0++;
    else if (approx_equal (rotation, (gfloat) (G_PI / 2.0), 1e-3f))
      seen_r90++;
  }
  fail_unless_equals_int (seen_r0, 1);
  fail_unless_equals_int (seen_r90, 1);

  gst_buffer_unref (outbuf);
  gst_harness_teardown (h);
}

GST_END_TEST;

/* Test accept-caps behavior for yoloobbv8tensordec sink pad */
GST_START_TEST (test_obb_accept_caps)
{
  GstElement *obb_element;
  GstPad *obb_sinkpad;
  GstCaps *obb_template_caps;
  GstCaps *audio_caps;

  obb_element = gst_element_factory_make ("yoloobbv8tensordec", NULL);
  fail_unless (obb_element != NULL);

  obb_sinkpad = gst_element_get_static_pad (obb_element, "sink");
  obb_template_caps = gst_pad_get_pad_template_caps (obb_sinkpad);
  obb_template_caps = gst_caps_fixate (obb_template_caps);
  fail_unless (gst_caps_is_fixed (obb_template_caps));

  fail_unless (gst_pad_query_accept_caps (obb_sinkpad, obb_template_caps));
  audio_caps = gst_caps_from_string ("audio/x-raw,format=S16LE,rate=48000");
  fail_if (gst_pad_query_accept_caps (obb_sinkpad, audio_caps));

  gst_caps_unref (obb_template_caps);
  gst_caps_unref (audio_caps);
  gst_object_unref (obb_sinkpad);
  gst_object_unref (obb_element);
}

GST_END_TEST;

/* Test that segmentation decoder creates both OD and segmentation metadata and links them */
GST_START_TEST (test_seg_basic_decode)
{
  GstHarness *h;
  GstBuffer *inbuf;
  GstBuffer *outbuf;
  GstAnalyticsRelationMeta *rmeta;
  GstAnalyticsODMtd od_mtd;
  GstAnalyticsSegmentationMtd seg_mtd;
  GstTensor *detections;
  GstTensor *logits;
  GstTensor *tensors[2];
  DetectionCandidate candidate;
  gfloat class_confidences[1];
  gfloat extras[1];
  gfloat logits_values[16];
  guint i;
  GstBuffer *mask_buf;
  gint masks_x, masks_y;
  guint masks_w, masks_h;
  gboolean has_od_to_seg_relation;
  gboolean has_seg_to_od_relation;

  for (i = 0; i < G_N_ELEMENTS (logits_values); i++)
    logits_values[i] = 1.0f;

  class_confidences[0] = 0.95f;
  extras[0] = 2.0f;
  candidate.x = 2.0f;
  candidate.y = 2.0f;
  candidate.w = 2.0f;
  candidate.h = 2.0f;
  candidate.class_confidences = class_confidences;
  candidate.extras = extras;

  detections = make_detections_tensor ("yolo-v8-segmentation-out-detections",
      1, 1, &candidate, 1);
  logits = make_logits_tensor (1, 4, 4, logits_values);
  tensors[0] = detections;
  tensors[1] = logits;

  inbuf = gst_buffer_new ();
  attach_tensors_meta (inbuf, tensors, 2);

  h = setup_decoder_harness ("yolosegv8tensordec", 4, 4);
  outbuf = gst_harness_push_and_pull (h, inbuf);
  fail_unless (outbuf != NULL);
  rmeta = gst_buffer_get_analytics_relation_meta (outbuf);
  fail_unless (rmeta != NULL);
  fail_unless_equals_int (count_mtd_type (rmeta,
          gst_analytics_od_mtd_get_mtd_type ()), 1);
  fail_unless_equals_int (count_mtd_type (rmeta,
          gst_analytics_segmentation_mtd_get_mtd_type ()), 1);
  fail_unless (get_single_od_mtd (rmeta, &od_mtd));
  fail_unless (get_single_segmentation_mtd (rmeta, &seg_mtd));

  has_od_to_seg_relation = gst_analytics_relation_meta_exist (rmeta, od_mtd.id,
      seg_mtd.id, 1, GST_ANALYTICS_REL_TYPE_RELATE_TO, NULL);
  has_seg_to_od_relation = gst_analytics_relation_meta_exist (rmeta, seg_mtd.id,
      od_mtd.id, 1, GST_ANALYTICS_REL_TYPE_RELATE_TO, NULL);
  fail_unless (has_od_to_seg_relation);
  fail_unless (has_seg_to_od_relation);
  mask_buf = gst_analytics_segmentation_mtd_get_mask (&seg_mtd, &masks_x,
      &masks_y, &masks_w, &masks_h);
  fail_unless (mask_buf != NULL);
  fail_unless_equals_int (masks_x, 1);
  fail_unless_equals_int (masks_y, 1);
  fail_unless_equals_int (masks_w, 2);
  fail_unless_equals_int (masks_h, 2);
  gst_buffer_unref (mask_buf);

  gst_buffer_unref (outbuf);
  gst_harness_teardown (h);
}

GST_END_TEST;

/* Test that segmentation mask pixels are generated from logits and coefficients as expected */
GST_START_TEST (test_seg_mask_pixel_values)
{
  GstHarness *h;
  GstBuffer *inbuf;
  GstBuffer *outbuf;
  GstAnalyticsRelationMeta *rmeta;
  GstAnalyticsSegmentationMtd seg_mtd;
  GstTensor *detections;
  GstTensor *logits;
  GstTensor *tensors[2];
  DetectionCandidate candidate;
  gfloat class_confidences[1];
  gfloat extras[1];
  gfloat logits_values[16];
  GstBuffer *mask_buf;
  GstMapInfo map_info;
  GstVideoMeta *vmeta;
  gint masks_x, masks_y;
  guint masks_w, masks_h;
  gboolean mapped;
  guint i;

  for (i = 0; i < G_N_ELEMENTS (logits_values); i++)
    logits_values[i] = -1.0f;
  /* Only positions (0,0) and (1,1) in the 4×4 logits grid are positive */
  logits_values[0] = 1.0f;
  logits_values[5] = 1.0f;

  class_confidences[0] = 0.99f;
  extras[0] = 2.0f;
  candidate.x = 1.0f;
  candidate.y = 1.0f;
  candidate.w = 2.0f;
  candidate.h = 2.0f;
  candidate.class_confidences = class_confidences;
  candidate.extras = extras;

  detections = make_detections_tensor ("yolo-v8-segmentation-out-detections",
      1, 1, &candidate, 1);
  logits = make_logits_tensor (1, 4, 4, logits_values);
  tensors[0] = detections;
  tensors[1] = logits;

  inbuf = gst_buffer_new ();
  attach_tensors_meta (inbuf, tensors, 2);

  h = setup_decoder_harness ("yolosegv8tensordec", 4, 4);
  outbuf = gst_harness_push_and_pull (h, inbuf);
  fail_unless (outbuf != NULL);

  rmeta = gst_buffer_get_analytics_relation_meta (outbuf);
  fail_unless (rmeta != NULL);
  fail_unless (get_single_segmentation_mtd (rmeta, &seg_mtd));

  mask_buf = gst_analytics_segmentation_mtd_get_mask (&seg_mtd, &masks_x,
      &masks_y, &masks_w, &masks_h);
  fail_unless (mask_buf != NULL);
  fail_unless_equals_int (masks_x, 0);
  fail_unless_equals_int (masks_y, 0);
  fail_unless_equals_int (masks_w, 2);
  fail_unless_equals_int (masks_h, 2);

  vmeta = gst_buffer_get_video_meta (mask_buf);
  fail_unless (vmeta != NULL);
  fail_unless_equals_int (vmeta->width, 2);
  fail_unless_equals_int (vmeta->height, 2);

  mapped = gst_buffer_map (mask_buf, &map_info, GST_MAP_READ);
  fail_unless (mapped);
  fail_unless_equals_int (map_info.data[0], 1);
  fail_unless_equals_int (map_info.data[1], 0);
  fail_unless_equals_int (map_info.data[2], 0);
  fail_unless_equals_int (map_info.data[3], 1);
  gst_buffer_unmap (mask_buf, &map_info);

  gst_buffer_unref (mask_buf);
  gst_buffer_unref (outbuf);
  gst_harness_teardown (h);
}

GST_END_TEST;

/* Test that landscape ROI offset is applied when mapping bbox coordinates to mask coordinates */
GST_START_TEST (test_seg_mask_roi_landscape)
{
  GstHarness *h;
  GstBuffer *inbuf;
  GstBuffer *outbuf;
  GstAnalyticsRelationMeta *rmeta;
  GstAnalyticsSegmentationMtd seg_mtd;
  GstTensor *detections;
  GstTensor *logits;
  GstTensor *tensors[2];
  DetectionCandidate candidate;
  gfloat class_confidences[1];
  gfloat extras[1];
  gfloat logits_values[64];
  guint x, y;
  guint idx;
  GstBuffer *mask_buf;
  GstMapInfo map_info;
  gboolean mapped;

  for (y = 0; y < 8; y++) {
    for (x = 0; x < 8; x++) {
      idx = y * 8 + x;
      logits_values[idx] = (y >= 3) ? 1.0f : -1.0f;
    }
  }

  class_confidences[0] = 0.99f;
  extras[0] = 5.0f;
  candidate.x = 3.0f;
  candidate.y = 2.0f;
  candidate.w = 2.0f;
  candidate.h = 2.0f;
  candidate.class_confidences = class_confidences;
  candidate.extras = extras;

  detections = make_detections_tensor ("yolo-v8-segmentation-out-detections",
      1, 1, &candidate, 1);
  logits = make_logits_tensor (1, 8, 8, logits_values);
  tensors[0] = detections;
  tensors[1] = logits;

  inbuf = gst_buffer_new ();
  attach_tensors_meta (inbuf, tensors, 2);

  h = setup_decoder_harness ("yolosegv8tensordec", 8, 4);
  outbuf = gst_harness_push_and_pull (h, inbuf);
  fail_unless (outbuf != NULL);

  rmeta = gst_buffer_get_analytics_relation_meta (outbuf);
  fail_unless (rmeta != NULL);
  fail_unless (get_single_segmentation_mtd (rmeta, &seg_mtd));

  mask_buf = gst_analytics_segmentation_mtd_get_mask (&seg_mtd, NULL, NULL,
      NULL, NULL);
  fail_unless (mask_buf != NULL);
  mapped = gst_buffer_map (mask_buf, &map_info, GST_MAP_READ);
  fail_unless (mapped);
  fail_unless_equals_int (map_info.data[0], 1);
  fail_unless_equals_int (map_info.data[1], 1);
  fail_unless_equals_int (map_info.data[2], 1);
  fail_unless_equals_int (map_info.data[3], 1);
  gst_buffer_unmap (mask_buf, &map_info);

  gst_buffer_unref (mask_buf);
  gst_buffer_unref (outbuf);
  gst_harness_teardown (h);
}

GST_END_TEST;

/* Test that portrait ROI offset is applied when mapping bbox coordinates to mask coordinates */
GST_START_TEST (test_seg_mask_roi_portrait)
{
  GstHarness *h;
  GstBuffer *inbuf;
  GstBuffer *outbuf;
  GstAnalyticsRelationMeta *rmeta;
  GstAnalyticsSegmentationMtd seg_mtd;
  GstTensor *detections;
  GstTensor *logits;
  GstTensor *tensors[2];
  DetectionCandidate candidate;
  gfloat class_confidences[1];
  gfloat extras[1];
  gfloat logits_values[64];
  guint x, y;
  guint idx;
  GstBuffer *mask_buf;
  GstMapInfo map_info;
  gboolean mapped;

  for (y = 0; y < 8; y++) {
    for (x = 0; x < 8; x++) {
      idx = y * 8 + x;
      logits_values[idx] = (x >= 3) ? 1.0f : -1.0f;
    }
  }

  class_confidences[0] = 0.99f;
  extras[0] = 5.0f;
  candidate.x = 2.0f;
  candidate.y = 3.0f;
  candidate.w = 2.0f;
  candidate.h = 2.0f;
  candidate.class_confidences = class_confidences;
  candidate.extras = extras;

  detections = make_detections_tensor ("yolo-v8-segmentation-out-detections",
      1, 1, &candidate, 1);
  logits = make_logits_tensor (1, 8, 8, logits_values);
  tensors[0] = detections;
  tensors[1] = logits;

  inbuf = gst_buffer_new ();
  attach_tensors_meta (inbuf, tensors, 2);

  h = setup_decoder_harness ("yolosegv8tensordec", 4, 8);
  outbuf = gst_harness_push_and_pull (h, inbuf);
  fail_unless (outbuf != NULL);

  rmeta = gst_buffer_get_analytics_relation_meta (outbuf);
  fail_unless (rmeta != NULL);
  fail_unless (get_single_segmentation_mtd (rmeta, &seg_mtd));

  mask_buf = gst_analytics_segmentation_mtd_get_mask (&seg_mtd, NULL, NULL,
      NULL, NULL);
  fail_unless (mask_buf != NULL);
  mapped = gst_buffer_map (mask_buf, &map_info, GST_MAP_READ);
  fail_unless (mapped);
  fail_unless_equals_int (map_info.data[0], 1);
  fail_unless_equals_int (map_info.data[1], 1);
  fail_unless_equals_int (map_info.data[2], 1);
  fail_unless_equals_int (map_info.data[3], 1);
  gst_buffer_unmap (mask_buf, &map_info);

  gst_buffer_unref (mask_buf);
  gst_buffer_unref (outbuf);
  gst_harness_teardown (h);
}

GST_END_TEST;

/* Test that missing logits tensor causes segmentation decoder to skip without metadata */
GST_START_TEST (test_seg_missing_logits_tensor)
{
  GstHarness *h;
  GstBuffer *inbuf;
  GstBuffer *outbuf;
  GstAnalyticsRelationMeta *rmeta;
  GstTensor *detections;
  GstTensor *tensors[1];
  DetectionCandidate candidate;
  gfloat class_confidences[1];
  gfloat extras[1];

  class_confidences[0] = 0.95f;
  extras[0] = 2.0f;
  candidate.x = 2.0f;
  candidate.y = 2.0f;
  candidate.w = 2.0f;
  candidate.h = 2.0f;
  candidate.class_confidences = class_confidences;
  candidate.extras = extras;

  detections = make_detections_tensor ("yolo-v8-segmentation-out-detections",
      1, 1, &candidate, 1);
  tensors[0] = detections;

  inbuf = gst_buffer_new ();
  attach_tensors_meta (inbuf, tensors, 1);

  h = setup_decoder_harness ("yolosegv8tensordec", 4, 4);
  outbuf = gst_harness_push_and_pull (h, inbuf);
  fail_unless (outbuf != NULL);

  rmeta = gst_buffer_get_analytics_relation_meta (outbuf);
  fail_unless (rmeta == NULL);

  gst_buffer_unref (outbuf);
  gst_harness_teardown (h);
}

GST_END_TEST;

/* Test that segmentation detections tensor with insufficient dims is ignored */
GST_START_TEST (test_seg_dims_too_small)
{
  GstHarness *h;
  GstBuffer *inbuf;
  GstBuffer *outbuf;
  GstAnalyticsRelationMeta *rmeta;
  GstTensor *detections;
  GstTensor *logits;
  GstTensor *tensors[2];
  DetectionCandidate candidate;
  gfloat class_confidences[1];
  gfloat extras[1];
  gfloat logits_values[8];
  guint i;

  for (i = 0; i < G_N_ELEMENTS (logits_values); i++)
    logits_values[i] = 1.0f;

  class_confidences[0] = 0.95f;
  extras[0] = 2.0f;
  candidate.x = 2.0f;
  candidate.y = 2.0f;
  candidate.w = 2.0f;
  candidate.h = 2.0f;
  candidate.class_confidences = class_confidences;
  candidate.extras = extras;

  detections = make_detections_tensor ("yolo-v8-segmentation-out-detections",
      1, 1, &candidate, 1);
  logits = make_logits_tensor (2, 2, 2, logits_values);
  tensors[0] = detections;
  tensors[1] = logits;

  inbuf = gst_buffer_new ();
  attach_tensors_meta (inbuf, tensors, 2);

  h = setup_decoder_harness ("yolosegv8tensordec", 4, 4);
  outbuf = gst_harness_push_and_pull (h, inbuf);
  fail_unless (outbuf != NULL);

  rmeta = gst_buffer_get_analytics_relation_meta (outbuf);
  fail_unless (rmeta == NULL);

  gst_buffer_unref (outbuf);
  gst_harness_teardown (h);
}

GST_END_TEST;

static Suite *
yolotensordecoder_suite (void)
{
  Suite *s;
  TCase *tc;

  s = suite_create ("yolotensordecoder");
  tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_add_test (tc, test_basic_single_detection);
  tcase_add_test (tc, test_below_confidence_threshold_dropped);
  tcase_add_test (tc, test_multiple_classes_pick_max);
  tcase_add_test (tc, test_nms_drops_overlapping_lower_confidence);
  tcase_add_test (tc, test_nms_keeps_distant_boxes);
  tcase_add_test (tc, test_max_detections_limit);
  tcase_add_test (tc, test_label_file_applied);
  tcase_add_test (tc, test_invalid_bbox_rejected);
  tcase_add_test (tc, test_missing_tensor_meta);
  tcase_add_test (tc, test_tensor_dims_too_small);
  tcase_add_test (tc, test_properties_roundtrip);
  tcase_add_test (tc, test_accept_caps);

  tcase_add_test (tc, test_obb_basic_decode);
  tcase_add_test (tc, test_obb_dims_too_small);
  tcase_add_test (tc, test_obb_polygon_iou_nms);
  tcase_add_test (tc, test_obb_accept_caps);

  tcase_add_test (tc, test_seg_basic_decode);
  tcase_add_test (tc, test_seg_mask_pixel_values);
  tcase_add_test (tc, test_seg_mask_roi_landscape);
  tcase_add_test (tc, test_seg_mask_roi_portrait);
  tcase_add_test (tc, test_seg_missing_logits_tensor);
  tcase_add_test (tc, test_seg_dims_too_small);

  return s;
}

GST_CHECK_MAIN (yolotensordecoder);
