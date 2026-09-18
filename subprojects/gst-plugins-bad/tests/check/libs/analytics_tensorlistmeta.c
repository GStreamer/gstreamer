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

#include <gst/check/gstcheck.h>
#include <gst/analytics/analytics.h>

static GstTensor *
make_correlation_tensor (const gchar * name)
{
  gsize dims[] = { 3, 2 };
  GstBuffer *data = gst_buffer_new_allocate (NULL, 6, NULL);

  gst_buffer_memset (data, 0, 42, 6);

  return gst_tensor_new_simple (g_quark_from_string (name),
      GST_TENSOR_DATA_TYPE_UINT8, data, GST_TENSOR_DIM_ORDER_ROW_MAJOR,
      G_N_ELEMENTS (dims), dims);
}

/* Provenance metadata keeps the same correlation ID after buffer copy */
GST_START_TEST (test_tensor_provenance_correlation)
{
  GstBuffer *source = gst_buffer_new ();
  GstBuffer *copy;
  GstAnalyticsRelationMeta *meta;
  GstAnalyticsRelationMeta *meta_from_copy;
  GstTensor *tensor = make_correlation_tensor ("input");
  GstAnalyticsTensorProvenanceMtd provenance_mtd, copied;
  gint64 correlation, correlation_from_copy;
  gboolean ret;

  meta = gst_buffer_add_analytics_relation_meta (source);
  ret = gst_analytics_relation_meta_add_tensor_provenance_mtd (meta,
      g_quark_from_static_string ("test-group"), tensor, &provenance_mtd);
  fail_unless (ret);

  correlation =
      gst_analytics_tensor_provenance_mtd_get_correlation_id (&provenance_mtd);
  fail_unless (correlation != 0);

  copy = gst_buffer_copy (source);
  meta_from_copy = gst_buffer_get_analytics_relation_meta (copy);
  ret = gst_analytics_relation_meta_get_tensor_provenance_mtd (meta_from_copy,
      provenance_mtd.id, &copied);
  fail_unless (ret);

  correlation_from_copy =
      gst_analytics_tensor_provenance_mtd_get_correlation_id (&copied);
  fail_unless_equals_int64 (correlation_from_copy, correlation);

  gst_tensor_free (tensor);
  gst_buffer_unref (source);
  gst_buffer_unref (copy);
}

GST_END_TEST;

/* Test tensor and provenance lookup */
GST_START_TEST (test_tensor_list_access)
{
  GstBuffer *buffer = gst_buffer_new ();
  GstTensorListMeta *meta;
  GstTensor **tensors = g_new (GstTensor *, 1);
  GstTensor **simple = g_new (GstTensor *, 1);
  gint64 left_ids[] = { 11, 12, 13 };
  gint64 right_ids[] = { 21, 22 };
  GstTensorProvenance provenance[] = {
    {g_quark_from_static_string ("left"), G_N_ELEMENTS (left_ids), left_ids},
    {g_quark_from_static_string ("right"), G_N_ELEMENTS (right_ids), right_ids}
  };
  GQuark depth = g_quark_from_static_string ("depth");
  gsize dims[] = { 3, 2 };
  const GstTensor *tensor;
  const GstTensor *tensor_from_meta;
  gboolean ret;
  gsize size_val;

  meta = gst_buffer_add_tensor_list_meta (buffer,
      g_quark_from_static_string ("test-group"));

  /* Add items with and without provenance */
  tensors[0] = make_correlation_tensor ("depth");
  ret = gst_tensor_list_meta_append_item_full (meta, 1, tensors,
      G_N_ELEMENTS (provenance), provenance);
  fail_unless (ret);

  simple[0] = make_correlation_tensor ("depth");
  ret = gst_tensor_list_meta_append_item (meta, 1, simple);
  fail_unless (ret);
  fail_unless_equals_int (meta->num_items, 2);

  tensor = gst_tensor_list_meta_get_typed_tensor (meta, 0, depth,
      GST_TENSOR_DATA_TYPE_UINT8, GST_TENSOR_DIM_ORDER_ROW_MAJOR,
      G_N_ELEMENTS (dims), dims);
  fail_unless (tensor != NULL);
  fail_unless_equals_int (tensor->id, depth);
  fail_unless (tensor == tensors[0]);

  /* "left" and "right" describe origins, not tensor IDs in this item. */
  size_val = gst_tensor_list_meta_get_num_provenance (meta, 0);
  fail_unless_equals_int (size_val, 2);
  for (gsize i = 0; i < G_N_ELEMENTS (provenance); i++) {
    GstTensorProvenance stored;

    ret = gst_tensor_list_meta_get_provenance (meta, 0, i, &stored);
    fail_unless (ret);
    fail_unless_equals_int (stored.key, provenance[i].key);

    /* The tensor ID is "depth", provenance keys are not tensor IDs */
    tensor_from_meta =
        gst_tensor_list_meta_get_typed_tensor (meta, 0, stored.key,
        GST_TENSOR_DATA_TYPE_UINT8, GST_TENSOR_DIM_ORDER_ROW_MAJOR,
        G_N_ELEMENTS (dims), dims);
    fail_unless (tensor_from_meta == NULL);

    fail_unless_equals_int (stored.num_correlation_ids,
        provenance[i].num_correlation_ids);
    fail_unless (memcmp (stored.correlation_ids, provenance[i].correlation_ids,
            stored.num_correlation_ids * sizeof (gint64)) == 0);
  }

  size_val = gst_tensor_list_meta_get_num_provenance (meta, 1);
  /* "simple" item has no associated provenance */
  fail_unless_equals_int (size_val, 0);
  gst_buffer_unref (buffer);
}

GST_END_TEST;

/* Tensor and provenance are preserved after buffer copy */
GST_START_TEST (test_tensor_list_copy)
{
  GstBuffer *source = gst_buffer_new ();
  GstBuffer *dest;
  GQuark group = g_quark_from_static_string ("test-group");
  GstTensorListMeta *meta = gst_buffer_add_tensor_list_meta (source, group);
  GstTensorListMeta *copy;
  GstTensor **tensors = g_new (GstTensor *, 1);
  gint64 ids[] = { 11, 12, 13 };
  GstTensorProvenance provenance = {
    g_quark_from_static_string ("image"), G_N_ELEMENTS (ids), ids
  };
  const GstTensor *tensor;
  GstTensorProvenance stored;
  gsize dims[] = { 3, 2 };
  gboolean ret;
  gsize size_val;

  tensors[0] = make_correlation_tensor ("prediction");
  ret = gst_tensor_list_meta_append_item_full (meta,
      1, tensors, 1, &provenance);
  fail_unless (ret);

  dest = gst_buffer_copy (source);
  gst_buffer_unref (source);

  /* Check the copied tensor and provenance */
  copy = gst_buffer_get_tensor_list_meta (dest);
  fail_unless (copy != NULL);

  fail_unless_equals_int (copy->group_id, group);
  fail_unless_equals_int (copy->num_items, 1);

  tensor = gst_tensor_list_meta_get_typed_tensor (copy, 0,
      g_quark_from_static_string ("prediction"), GST_TENSOR_DATA_TYPE_UINT8,
      GST_TENSOR_DIM_ORDER_ROW_MAJOR, G_N_ELEMENTS (dims), dims);
  fail_unless (tensor != NULL);

  size_val = gst_tensor_list_meta_get_num_provenance (copy, 0);
  fail_unless_equals_int (size_val, 1);

  ret = gst_tensor_list_meta_get_provenance (copy, 0, 0, &stored);
  fail_unless (ret);
  fail_unless_equals_int (stored.key, provenance.key);
  fail_unless_equals_int (stored.num_correlation_ids, G_N_ELEMENTS (ids));
  fail_unless (memcmp (stored.correlation_ids, ids, sizeof (ids)) == 0);

  gst_buffer_unref (dest);
}

GST_END_TEST;

/* Replace input tensors with outputs while keeping input provenance */
GST_START_TEST (test_tensor_list_append_item_from)
{
  GstBuffer *src = gst_buffer_new ();
  GstBuffer *dst = gst_buffer_new ();
  GstTensorListMeta *source = gst_buffer_add_tensor_list_meta (src, 0);
  GstTensorListMeta *dest = gst_buffer_add_tensor_list_meta (dst, 0);
  GstTensor **inputs = g_new (GstTensor *, 1);
  GstTensor **outputs = g_new (GstTensor *, 1);
  gint64 image_ids[] = { 11, 12 };
  gint64 aux_ids[] = { 21 };
  GstTensorProvenance provenance[] = {
    {g_quark_from_static_string ("image"), G_N_ELEMENTS (image_ids), image_ids},
    {g_quark_from_static_string ("aux"), G_N_ELEMENTS (aux_ids), aux_ids}
  };
  GstTensorProvenance copied;
  gsize size_val;
  gboolean ret;
  const GstTensor *tensor_from_meta;

  /* Create an input item and replace its tensor */
  inputs[0] = make_correlation_tensor ("input");
  outputs[0] = make_correlation_tensor ("output");
  ret = gst_tensor_list_meta_append_item_full (source, 1, inputs,
      G_N_ELEMENTS (provenance), provenance);
  fail_unless (ret);

  ret = gst_tensor_list_meta_append_item_from (dest, 1, outputs, source, 0);
  fail_unless (ret);

  fail_unless_equals_int (source->num_items, 1);

  tensor_from_meta = gst_tensor_list_meta_get_typed_tensor (source, 0,
      inputs[0]->id, GST_TENSOR_DATA_TYPE_UINT8,
      GST_TENSOR_DIM_ORDER_ROW_MAJOR, inputs[0]->num_dims, inputs[0]->dims);
  fail_unless (tensor_from_meta == inputs[0]);

  tensor_from_meta = gst_tensor_list_meta_get_typed_tensor (dest, 0,
      outputs[0]->id, GST_TENSOR_DATA_TYPE_UINT8,
      GST_TENSOR_DIM_ORDER_ROW_MAJOR, outputs[0]->num_dims, outputs[0]->dims);
  fail_unless (tensor_from_meta == outputs[0]);

  fail_unless_equals_string (g_quark_to_string (outputs[0]->id), "output");

  size_val = gst_tensor_list_meta_get_num_provenance (dest, 0);
  fail_unless_equals_int (size_val, G_N_ELEMENTS (provenance));

  /* Input provenance is still valid after releasing the source */
  gst_buffer_unref (src);
  for (gsize i = 0; i < G_N_ELEMENTS (provenance); i++) {
    ret = gst_tensor_list_meta_get_provenance (dest, 0, i, &copied);
    fail_unless (ret);
    fail_unless_equals_int (copied.key, provenance[i].key);
    fail_unless_equals_int (copied.num_correlation_ids,
        provenance[i].num_correlation_ids);
    fail_unless (memcmp (copied.correlation_ids, provenance[i].correlation_ids,
            copied.num_correlation_ids * sizeof (gint64)) == 0);
  }

  gst_buffer_unref (dst);
}

GST_END_TEST;

static GstTensor *
make_scenario_tensor (const gchar * name, gsize num_dims, gsize * dims)
{
  gsize size = 1;

  for (gsize i = 0; i < num_dims; i++)
    size *= dims[i];

  return gst_tensor_new_simple (g_quark_from_string (name),
      GST_TENSOR_DATA_TYPE_UINT8, gst_buffer_new_allocate (NULL, size, NULL),
      GST_TENSOR_DIM_ORDER_ROW_MAJOR, num_dims, dims);
}

/* Match inference outputs with source ODs after ROI skip and reorder */
GST_START_TEST (test_tensor_list_roi_provenance)
{
  GstBuffer *video_buffer = gst_buffer_new ();
  GstAnalyticsRelationMeta *relations =
      gst_buffer_add_analytics_relation_meta (video_buffer);
  GQuark group = g_quark_from_static_string ("recognizer");
  GQuark image = g_quark_from_static_string ("image");
  GstTensorListMeta *inputs =
      gst_buffer_add_tensor_list_meta (video_buffer, group);
  GstTensorListMeta *outputs;
  GstAnalyticsODMtd od_a, skipped_od, od_b;
  GstAnalyticsTensorProvenanceMtd provenance_mtd_a, provenance_mtd_b;
  GstTensor **input_a = g_new (GstTensor *, 1);
  GstTensor **input_b = g_new (GstTensor *, 1);
  gint64 correlation_a, correlation_b;
  GstTensorProvenance provenance_a, provenance_b;
  gsize dims[] = { 1, 1, 2, 4 };
  guint expected_od_ids[2];
  gboolean ret;

  /* Create three ODs, but produce recognition inputs only for A and B */
  ret = gst_analytics_relation_meta_add_od_mtd (relations,
      g_quark_from_static_string ("text"), 10, 10, 40, 20, 0.9f, &od_a);
  fail_unless (ret);

  ret = gst_analytics_relation_meta_add_od_mtd (relations,
      g_quark_from_static_string ("text"), 0, 0, 1, 1, 0.9f, &skipped_od);
  fail_unless (ret);

  ret = gst_analytics_relation_meta_add_od_mtd (relations,
      g_quark_from_static_string ("text"), 60, 10, 40, 20, 0.9f, &od_b);
  fail_unless (ret);

  input_a[0] = make_scenario_tensor ("image", G_N_ELEMENTS (dims), dims);
  input_b[0] = make_scenario_tensor ("image", G_N_ELEMENTS (dims), dims);

  ret = gst_analytics_relation_meta_add_tensor_provenance_mtd (relations,
      group, input_a[0], &provenance_mtd_a);
  fail_unless (ret);

  ret = gst_analytics_relation_meta_add_tensor_provenance_mtd (relations,
      group, input_b[0], &provenance_mtd_b);
  fail_unless (ret);

  ret = gst_analytics_relation_meta_set_relation (relations,
      GST_ANALYTICS_REL_TYPE_RELATE_TO, provenance_mtd_a.id, od_a.id);
  fail_unless (ret);

  ret = gst_analytics_relation_meta_set_relation (relations,
      GST_ANALYTICS_REL_TYPE_RELATE_TO, provenance_mtd_b.id, od_b.id);
  fail_unless (ret);

  /* Store each accepted ROI provenance correlation ID in its own input item */
  correlation_a =
      gst_analytics_tensor_provenance_mtd_get_correlation_id
      (&provenance_mtd_a);
  correlation_b =
      gst_analytics_tensor_provenance_mtd_get_correlation_id
      (&provenance_mtd_b);

  provenance_a = (GstTensorProvenance) {
  image, 1, &correlation_a};
  provenance_b = (GstTensorProvenance) {
  image, 1, &correlation_b};

  ret = gst_tensor_list_meta_append_item_full (inputs, 1, input_a,
      1, &provenance_a);
  fail_unless (ret);

  ret = gst_tensor_list_meta_append_item_full (inputs, 1, input_b,
      1, &provenance_b);
  fail_unless (ret);

  outputs = gst_buffer_add_tensor_list_meta (video_buffer, group);

  /* Complete B before A, the rejected OD has no tensor item at all */
  expected_od_ids[0] = od_b.id;
  expected_od_ids[1] = od_a.id;
  for (gsize i = 0; i < 2; i++) {
    GstTensor **output = g_new (GstTensor *, 1);

    output[0] = make_correlation_tensor ("logits");
    ret = gst_tensor_list_meta_append_item_from (outputs, 1, output,
        inputs, 1 - i);
    fail_unless (ret);
  }

  ret = gst_buffer_remove_meta (video_buffer, (GstMeta *) inputs);
  fail_unless (ret);

  /* Resolve provenance metadata, then follow its relation to the OD */
  for (gsize i = 0; i < outputs->num_items; i++) {
    GstTensorProvenance stored;
    GstAnalyticsTensorProvenanceMtd provenance_mtd;
    gpointer state = NULL;
    guint matches = 0;

    ret = gst_tensor_list_meta_get_provenance (outputs, i, 0, &stored);
    fail_unless (ret);
    fail_unless_equals_int (stored.key, image);
    fail_unless_equals_int (stored.num_correlation_ids, 1);
    while (gst_analytics_relation_meta_iterate (relations, &state,
            gst_analytics_tensor_provenance_mtd_get_mtd_type (),
            &provenance_mtd)) {
      GstAnalyticsODMtd od;
      gpointer related_state = NULL;

      if (gst_analytics_tensor_provenance_mtd_get_correlation_id
          (&provenance_mtd) != stored.correlation_ids[0])
        continue;

      ret = gst_analytics_relation_meta_get_direct_related (relations,
          provenance_mtd.id, GST_ANALYTICS_REL_TYPE_RELATE_TO,
          gst_analytics_od_mtd_get_mtd_type (), &related_state,
          (GstAnalyticsMtd *) & od);
      fail_unless (ret);
      fail_unless_equals_int (od.id, expected_od_ids[i]);
      matches++;
    }

    fail_unless_equals_int (matches, 1);
  }

  gst_buffer_unref (video_buffer);
}

GST_END_TEST;

/* Store multiple provenance metadata IDs for a batched tensor */
GST_START_TEST (test_batched_tensor_provenance)
{
  GstBuffer *buffer = gst_buffer_new ();
  GstAnalyticsRelationMeta *relations =
      gst_buffer_add_analytics_relation_meta (buffer);
  GQuark group = g_quark_from_static_string ("batched-detector");
  GstTensorListMeta *list = gst_buffer_add_tensor_list_meta (buffer, group);
  GstTensor **tensors = g_new (GstTensor *, 1);
  gsize dims[] = { 3, 3, 2, 2 };
  gint64 correlations[3];
  GstTensorProvenance provenance = {
    g_quark_from_static_string ("image"), 3, correlations
  };
  GstTensorProvenance stored;
  gboolean ret;

  /* Create three sample provenance records for the single [3,C,H,W] tensor. */
  tensors[0] = make_scenario_tensor ("image", G_N_ELEMENTS (dims), dims);
  for (gsize i = 0; i < 3; i++) {
    GstAnalyticsTensorProvenanceMtd provenance_mtd;

    ret = gst_analytics_relation_meta_add_tensor_provenance_mtd (relations,
        group, tensors[0], &provenance_mtd);
    fail_unless (ret);
    correlations[i] =
        gst_analytics_tensor_provenance_mtd_get_correlation_id
        (&provenance_mtd);
  }

  /* Keep the provenance metadata IDs in producer-defined order */
  ret = gst_tensor_list_meta_append_item_full (list, 1, tensors,
      1, &provenance);
  fail_unless (ret);

  fail_unless_equals_int (list->num_items, 1);

  /* Check the provenance metadata ID order */
  ret = gst_tensor_list_meta_get_provenance (list, 0, 0, &stored);
  fail_unless (ret);
  fail_unless_equals_int (stored.key, provenance.key);
  fail_unless_equals_int (stored.num_correlation_ids, 3);
  fail_unless (memcmp (stored.correlation_ids, correlations,
          sizeof (correlations)) == 0);

  gst_buffer_unref (buffer);
}

GST_END_TEST;

/* Keep provenance from multiple inputs after tensor replacement */
GST_START_TEST (test_multi_input_provenance)
{
  GstBuffer *buffer = gst_buffer_new ();
  GstAnalyticsRelationMeta *relations =
      gst_buffer_add_analytics_relation_meta (buffer);
  GQuark group = g_quark_from_static_string ("multi-input-model");
  GstTensorListMeta *inputs = gst_buffer_add_tensor_list_meta (buffer, group);
  GstTensorListMeta *outputs;
  GstTensor **input_tensors = g_new (GstTensor *, 2);
  GstTensor **output_tensors = g_new (GstTensor *, 1);
  GstAnalyticsTensorProvenanceMtd image_provenance_mtd,
      auxiliary_provenance_mtd;
  gint64 image_id, auxiliary_id;
  GstTensorProvenance provenance[2];
  gboolean ret;
  gsize size_val;

  /* Create separate provenance records for image and auxiliary inputs */
  input_tensors[0] = make_correlation_tensor ("image");
  input_tensors[1] = make_correlation_tensor ("auxiliary-input");

  ret = gst_analytics_relation_meta_add_tensor_provenance_mtd (relations,
      group, input_tensors[0], &image_provenance_mtd);
  fail_unless (ret);

  ret = gst_analytics_relation_meta_add_tensor_provenance_mtd (relations,
      group, input_tensors[1], &auxiliary_provenance_mtd);
  fail_unless (ret);

  image_id =
      gst_analytics_tensor_provenance_mtd_get_correlation_id
      (&image_provenance_mtd);
  auxiliary_id =
      gst_analytics_tensor_provenance_mtd_get_correlation_id
      (&auxiliary_provenance_mtd);
  provenance[0] = (GstTensorProvenance) {
  input_tensors[0]->id, 1, &image_id};

  provenance[1] = (GstTensorProvenance) {
  input_tensors[1]->id, 1, &auxiliary_id};

  ret = gst_tensor_list_meta_append_item_full (inputs, 2, input_tensors,
      2, provenance);
  fail_unless (ret);

  /* Replace two input tensors with one output tensor */
  outputs = gst_buffer_add_tensor_list_meta (buffer, group);
  output_tensors[0] = make_correlation_tensor ("prediction");

  ret = gst_tensor_list_meta_append_item_from (outputs, 1,
      output_tensors, inputs, 0);
  fail_unless (ret);

  ret = gst_buffer_remove_meta (buffer, (GstMeta *) inputs);
  fail_unless (ret);

  /* Both input provenance entries are preserved */
  size_val = gst_tensor_list_meta_get_num_provenance (outputs, 0);
  fail_unless_equals_int (size_val, 2);

  for (gsize i = 0; i < 2; i++) {
    GstTensorProvenance stored;

    ret = gst_tensor_list_meta_get_provenance (outputs, 0, i, &stored);
    fail_unless (ret);
    fail_unless_equals_int (stored.key, provenance[i].key);
    fail_unless_equals_int (stored.num_correlation_ids, 1);
    fail_unless_equals_int64 (stored.correlation_ids[0],
        provenance[i].correlation_ids[0]);
  }

  gst_buffer_unref (buffer);
}

GST_END_TEST;

/* Create two tensor items for two persons across three video frames */
GST_START_TEST (test_temporal_processing_instances)
{
  GstBuffer *temporal_buffer = gst_buffer_new ();
  GstAnalyticsBatchMeta *batch =
      gst_buffer_add_analytics_batch_meta (temporal_buffer);
  GQuark group = g_quark_from_static_string ("person-action-model");
  GQuark video = g_quark_from_static_string ("video");
  GstTensorListMeta *list =
      gst_buffer_add_tensor_list_meta (temporal_buffer, group);
  gsize dims[] = { 1, 3, 3, 2, 2 };
  GstTensor *person_a =
      make_scenario_tensor ("video", G_N_ELEMENTS (dims), dims);
  GstTensor *person_b =
      make_scenario_tensor ("video", G_N_ELEMENTS (dims), dims);
  gint64 correlations_a[3], correlations_b[3];
  GstTensorProvenance provenance_a = { video, 3, correlations_a };
  GstTensorProvenance provenance_b = { video, 3, correlations_b };
  GstTensor **tensors_a = g_new (GstTensor *, 1);
  GstTensor **tensors_b = g_new (GstTensor *, 1);
  GstTensorProvenance stored_a, stored_b;
  gboolean ret;

  /* Each source frame contains separate OD/provenance pairs for persons A and B */
  batch->n_streams = 1;
  batch->streams = g_new0 (GstAnalyticsBatchStream, 1);
  batch->streams[0].n_objects = 3;
  batch->streams[0].objects = g_new (GstMiniObject *, 3);

  for (gsize i = 0; i < 3; i++) {
    GstBuffer *frame = gst_buffer_new ();
    GstAnalyticsRelationMeta *relations =
        gst_buffer_add_analytics_relation_meta (frame);
    GstAnalyticsODMtd od_a, od_b;
    GstAnalyticsTensorProvenanceMtd provenance_mtd_a, provenance_mtd_b;

    GST_BUFFER_PTS (frame) = (i + 1) * GST_SECOND;

    ret = gst_analytics_relation_meta_add_od_mtd (relations,
        g_quark_from_static_string ("person"), 10, 10, 20, 40, 0.9f, &od_a);
    fail_unless (ret);

    ret = gst_analytics_relation_meta_add_od_mtd (relations,
        g_quark_from_static_string ("person"), 60, 10, 20, 40, 0.9f, &od_b);
    fail_unless (ret);

    ret = gst_analytics_relation_meta_add_tensor_provenance_mtd (relations,
        group, person_a, &provenance_mtd_a);
    fail_unless (ret);

    ret = gst_analytics_relation_meta_add_tensor_provenance_mtd (relations,
        group, person_b, &provenance_mtd_b);
    fail_unless (ret);

    ret = gst_analytics_relation_meta_set_relation (relations,
        GST_ANALYTICS_REL_TYPE_RELATE_TO, provenance_mtd_a.id, od_a.id);
    fail_unless (ret);

    ret = gst_analytics_relation_meta_set_relation (relations,
        GST_ANALYTICS_REL_TYPE_RELATE_TO, provenance_mtd_b.id, od_b.id);
    fail_unless (ret);

    correlations_a[i] =
        gst_analytics_tensor_provenance_mtd_get_correlation_id
        (&provenance_mtd_a);
    correlations_b[i] =
        gst_analytics_tensor_provenance_mtd_get_correlation_id
        (&provenance_mtd_b);

    batch->streams[0].objects[i] = GST_MINI_OBJECT_CAST (frame);
  }

  /* Keep the provenance metadata sequence for each person */
  tensors_a[0] = person_a;
  tensors_b[0] = person_b;

  ret = gst_tensor_list_meta_append_item_full (list, 1, tensors_a, 1,
      &provenance_a);
  fail_unless (ret);

  ret = gst_tensor_list_meta_append_item_full (list, 1, tensors_b, 1,
      &provenance_b);
  fail_unless (ret);

  /* Check the provenance metadata sequence for each person */
  fail_unless_equals_int (batch->streams[0].n_objects, 3);
  fail_unless_equals_int (list->num_items, 2);

  fail_unless (gst_tensor_list_meta_get_typed_tensor (list, 0, video,
          GST_TENSOR_DATA_TYPE_UINT8, GST_TENSOR_DIM_ORDER_ROW_MAJOR,
          G_N_ELEMENTS (dims), dims) != NULL);

  ret = gst_tensor_list_meta_get_provenance (list, 0, 0, &stored_a);
  fail_unless (ret);
  fail_unless_equals_int (stored_a.key, video);
  fail_unless_equals_int (stored_a.num_correlation_ids, 3);
  fail_unless (memcmp (stored_a.correlation_ids, correlations_a,
          sizeof (correlations_a)) == 0);

  ret = gst_tensor_list_meta_get_provenance (list, 1, 0, &stored_b);
  fail_unless (ret);
  fail_unless_equals_int (stored_b.key, video);
  fail_unless_equals_int (stored_b.num_correlation_ids, 3);
  fail_unless (memcmp (stored_b.correlation_ids, correlations_b,
          sizeof (correlations_b)) == 0);

  gst_buffer_unref (temporal_buffer);
}

GST_END_TEST;

/* Copied provenance records keep their correlation ID but may differ in transform */
GST_START_TEST (test_tensor_provenance_multiple_copies)
{
  GstBuffer *source_buffer = gst_buffer_new ();
  GstBuffer *branch_a, *branch_b;
  GstBuffer *dest_buffer = gst_buffer_new ();
  GstAnalyticsRelationMeta *source =
      gst_buffer_add_analytics_relation_meta (source_buffer);
  GstAnalyticsRelationMeta *merged =
      gst_buffer_add_analytics_relation_meta (dest_buffer);
  GstTensor *tensor = make_correlation_tensor ("image");
  GstAnalyticsTensorProvenanceMtd original, provenance_mtd_a, provenance_mtd_b,
      provenance_mtd;
  gint64 correlation;
  gfloat transform_a[] = { 1, 0, 10, 0, 1, 0, 0, 0, 1 };
  gfloat transform_b[] = { 1, 0, 20, 0, 1, 0, 0, 0, 1 };
  guint matches = 0;
  gboolean found_a = FALSE, found_b = FALSE;
  gpointer state = NULL;
  gboolean ret;
  GstAnalyticsRelationMeta *relation_from_buf;

  ret = gst_analytics_relation_meta_add_tensor_provenance_mtd (source,
      g_quark_from_static_string ("detector"), tensor, &original);
  fail_unless (ret);

  correlation =
      gst_analytics_tensor_provenance_mtd_get_correlation_id (&original);

  /* Copy the provenance metadata into two buffers and set different transforms */
  branch_a = gst_buffer_copy (source_buffer);
  branch_b = gst_buffer_copy (source_buffer);

  relation_from_buf = gst_buffer_get_analytics_relation_meta (branch_a);
  ret =
      gst_analytics_relation_meta_get_tensor_provenance_mtd (relation_from_buf,
      original.id, &provenance_mtd_a);
  fail_unless (ret);

  relation_from_buf = gst_buffer_get_analytics_relation_meta (branch_b);
  ret =
      gst_analytics_relation_meta_get_tensor_provenance_mtd (relation_from_buf,
      original.id, &provenance_mtd_b);
  fail_unless (ret);

  ret = gst_analytics_tensor_provenance_mtd_set_spatial_transform
      (&provenance_mtd_a, transform_a);
  fail_unless (ret);

  ret = gst_analytics_tensor_provenance_mtd_set_spatial_transform
      (&provenance_mtd_b, transform_b);
  fail_unless (ret);

  /* Copy both provenance records into the same buffer */
  ret = gst_buffer_copy_into (dest_buffer, branch_a,
      GST_BUFFER_COPY_META, 0, -1);
  fail_unless (ret);

  ret = gst_buffer_copy_into (dest_buffer, branch_b,
      GST_BUFFER_COPY_META, 0, -1);
  fail_unless (ret);

  gst_buffer_unref (branch_a);
  gst_buffer_unref (branch_b);
  gst_buffer_unref (source_buffer);

  /* Both provenance records keep their correlation ID and their own transforms */
  while (gst_analytics_relation_meta_iterate (merged, &state,
          gst_analytics_tensor_provenance_mtd_get_mtd_type (),
          &provenance_mtd)) {
    gfloat transform[9];
    gint64 correlation_in_merged =
        gst_analytics_tensor_provenance_mtd_get_correlation_id
        (&provenance_mtd);

    fail_unless_equals_int64 (correlation_in_merged, correlation);

    ret = gst_analytics_tensor_provenance_mtd_get_spatial_transform
        (&provenance_mtd, transform);
    fail_unless (ret);

    found_a |= memcmp (transform, transform_a, sizeof (transform)) == 0;
    found_b |= memcmp (transform, transform_b, sizeof (transform)) == 0;

    matches++;
  }

  fail_unless_equals_int (matches, 2);
  fail_unless (found_a && found_b);

  gst_tensor_free (tensor);
  gst_buffer_unref (dest_buffer);
}

GST_END_TEST;

/* Test the rectangle helper with common tensor-to-video mappings */
GST_START_TEST (test_tensor_provenance_rectangle_mapping)
{
  const struct
  {
    GstVideoRectangle tensor;
    GstVideoRectangle video;
  } cases[] = {
    /* Identity and resize */
    {{0, 0, 640, 640}, {0, 0, 640, 640}},
    {{0, 0, 640, 640}, {0, 0, 800, 600}},
    /* Crop and resize */
    {{0, 0, 640, 640}, {100, 50, 800, 600}},
    /* Non-zero tensor origin */
    {{10, 20, 100, 50}, {-30, 40, 200, 150}},
    /* Content area of a padded tensor */
    {{0, 80, 640, 480}, {0, 0, 1280, 960}},
  };
  GstBuffer *buffer = gst_buffer_new ();
  GstAnalyticsRelationMeta *meta =
      gst_buffer_add_analytics_relation_meta (buffer);
  GstTensor *tensor = make_correlation_tensor ("input");
  GstAnalyticsTensorProvenanceMtd provenance_mtd;
  gboolean ret;

  ret = gst_analytics_relation_meta_add_tensor_provenance_mtd (meta,
      g_quark_from_static_string ("test-group"), tensor, &provenance_mtd);
  fail_unless (ret);

  for (guint i = 0; i < G_N_ELEMENTS (cases); i++) {
    const GstVideoRectangle *src = &cases[i].tensor;
    const GstVideoRectangle *dst = &cases[i].video;
    gfloat x, y, w, h;

    ret =
        gst_analytics_tensor_provenance_mtd_set_spatial_transform_from_rectangles
        (&provenance_mtd, src, dst);
    fail_unless (ret);

    /* Check a point in the mapped rectangle */
    x = src->x + src->w / 2.0f;
    y = src->y + src->h / 2.0f;

    ret =
        gst_analytics_tensor_provenance_mtd_transform_point (&provenance_mtd,
        &x, &y);
    fail_unless (ret);

    fail_unless_equals_float (x, dst->x + dst->w / 2.0f);
    fail_unless_equals_float (y, dst->y + dst->h / 2.0f);

    /* Check the whole rectangle */
    x = src->x;
    y = src->y;
    w = src->w;
    h = src->h;

    ret =
        gst_analytics_tensor_provenance_mtd_transform_rectangle
        (&provenance_mtd, &x, &y, &w, &h);
    fail_unless (ret);

    fail_unless_equals_float (x, dst->x);
    fail_unless_equals_float (y, dst->y);
    fail_unless_equals_float (w, dst->w);
    fail_unless_equals_float (h, dst->h);
  }

  gst_tensor_free (tensor);
  gst_buffer_unref (buffer);
}

GST_END_TEST;

static Suite *
analytics_tensorlistmeta_suite (void)
{
  Suite *s = suite_create ("Analytics TensorListMeta");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_add_test (tc, test_tensor_provenance_rectangle_mapping);
  tcase_add_test (tc, test_tensor_provenance_correlation);
  tcase_add_test (tc, test_tensor_list_access);
  tcase_add_test (tc, test_tensor_list_copy);
  tcase_add_test (tc, test_tensor_list_append_item_from);
  tcase_add_test (tc, test_tensor_list_roi_provenance);
  tcase_add_test (tc, test_batched_tensor_provenance);
  tcase_add_test (tc, test_multi_input_provenance);
  tcase_add_test (tc, test_temporal_processing_instances);
  tcase_add_test (tc, test_tensor_provenance_multiple_copies);

  return s;
}

GST_CHECK_MAIN (analytics_tensorlistmeta);
