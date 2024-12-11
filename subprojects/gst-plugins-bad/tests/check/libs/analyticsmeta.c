/* GStreamer
 * Copyright (C) 2022 Collabora Ltd
 * Copyright (C) 2024 Intel Corporation
 *
 * analyticmeta.c
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

GST_START_TEST (test_add_classification_meta)
{
  /* Verify we can create a relation metadata
   * and attach it classification mtd
   */
  GstBuffer *buf;
  GstAnalyticsRelationMeta *rmeta;
  GstAnalyticsClsMtd cls_mtd;
  gfloat conf_lvl[] = { 0.5f, 0.5f };
  GQuark class_quarks[2];
  gboolean ret;

  class_quarks[0] = g_quark_from_string ("dog");
  class_quarks[1] = g_quark_from_string ("cat");

  buf = gst_buffer_new ();
  rmeta = gst_buffer_add_analytics_relation_meta (buf);
  ret = gst_analytics_relation_meta_add_cls_mtd (rmeta, 2, conf_lvl,
      class_quarks, &cls_mtd);
  fail_unless (ret == TRUE);
  gst_buffer_unref (buf);
}

GST_END_TEST;

GST_START_TEST (test_meta_pooled)
{
  GstBufferPool *pool;
  GstStructure *config;
  GstBuffer *buf;
  GstAnalyticsRelationMeta *rmeta1, *rmeta2;

  pool = gst_buffer_pool_new ();
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, NULL, 1, 1, 1);
  gst_buffer_pool_set_config (pool, config);
  gst_buffer_pool_set_active (pool, TRUE);

  gst_buffer_pool_acquire_buffer (pool, &buf, NULL);
  rmeta1 = gst_buffer_add_analytics_relation_meta (buf);
  gst_buffer_unref (buf);

  gst_buffer_pool_acquire_buffer (pool, &buf, NULL);

  rmeta2 = gst_buffer_add_analytics_relation_meta (buf);

  fail_unless (rmeta1 == rmeta2);
  gst_buffer_unref (buf);
  gst_object_unref (pool);
}

GST_END_TEST;

GST_START_TEST (test_classification_meta_classes)
{
  /* Verify we can retrieve classification data
   * from the relation metadata
   */
  GstBuffer *buf;
  GstAnalyticsRelationMeta *rmeta;
  gboolean ret;
  GQuark class_quarks[2];
  GstAnalyticsClsMtd cls_mtd, cls_mtd2;

  class_quarks[0] = g_quark_from_string ("dog");
  class_quarks[1] = g_quark_from_string ("cat");

  buf = gst_buffer_new ();
  rmeta = gst_buffer_add_analytics_relation_meta (buf);
  gfloat conf_lvl[] = { 0.6f, 0.4f };
  ret = gst_analytics_relation_meta_add_cls_mtd (rmeta, 2, conf_lvl,
      class_quarks, &cls_mtd);
  fail_unless (ret == TRUE);
  fail_unless (gst_analytics_relation_get_length (rmeta) == 1);

  gint dogIndex = gst_analytics_cls_mtd_get_index_by_quark (&cls_mtd,
      class_quarks[0]);
  fail_unless (dogIndex == 0);
  gfloat confLvl = gst_analytics_cls_mtd_get_level (&cls_mtd, dogIndex);
  GST_LOG ("dog:%f", confLvl);
  assert_equals_float (confLvl, 0.6f);

  gint catIndex = gst_analytics_cls_mtd_get_index_by_quark (&cls_mtd,
      g_quark_from_string ("cat"));
  confLvl = gst_analytics_cls_mtd_get_level (&cls_mtd, catIndex);
  GST_LOG ("Cat:%f", confLvl);
  assert_equals_float (confLvl, 0.4f);
  assert_equals_int (gst_analytics_mtd_get_id ((GstAnalyticsMtd *) & cls_mtd),
      0);

  conf_lvl[0] = 0.1f;
  conf_lvl[1] = 0.9f;
  ret =
      gst_analytics_relation_meta_add_cls_mtd (rmeta, 2, conf_lvl,
      class_quarks, &cls_mtd2);
  fail_unless (ret == TRUE);
  fail_unless (gst_analytics_relation_get_length (rmeta) == 2);

  dogIndex = gst_analytics_cls_mtd_get_index_by_quark (&cls_mtd2,
      class_quarks[0]);
  confLvl = gst_analytics_cls_mtd_get_level (&cls_mtd2, dogIndex);
  assert_equals_float (confLvl, 0.1f);

  catIndex = gst_analytics_cls_mtd_get_index_by_quark (&cls_mtd2,
      class_quarks[0]);
  confLvl = gst_analytics_cls_mtd_get_level (&cls_mtd2, catIndex);
  assert_equals_float (confLvl, 0.1f);

  /* Verify the relation meta contain the correct number of relatable metadata */
  fail_unless (gst_analytics_relation_get_length (rmeta) == 2);

  /* Verify first relatable metadata has the correct id. */
  assert_equals_int (gst_analytics_mtd_get_id ((GstAnalyticsMtd *) & cls_mtd),
      0);

  /* Verify second relatable metadata has the correct id. */
  assert_equals_int (gst_analytics_mtd_get_id (
          (GstAnalyticsMtd *) & cls_mtd2), 1);

  gst_buffer_unref (buf);
}

GST_END_TEST;

GST_START_TEST (test_add_relation_meta)
{
  /* Verify we set a relation between relatable metadata. */

  GstBuffer *buf;
  GstAnalyticsClsMtd cls_mtd[3];
  GstAnalyticsRelationMetaInitParams init_params = { 5, 150 };
  GstAnalyticsRelationMeta *relations;
  GQuark class_quarks[2];
  gboolean ret;

  buf = gst_buffer_new ();
  relations = gst_buffer_add_analytics_relation_meta_full (buf, &init_params);

  gfloat conf_lvl[] = { 0.6f, 0.4f };
  class_quarks[0] = g_quark_from_string ("dog");
  class_quarks[1] = g_quark_from_string ("cat");

  ret = gst_analytics_relation_meta_add_cls_mtd (relations, 2, conf_lvl,
      class_quarks, &cls_mtd[0]);
  fail_unless (ret == TRUE);
  gst_analytics_mtd_get_id ((GstAnalyticsMtd *)
      & cls_mtd[0]);

  conf_lvl[0] = 0.6f;
  conf_lvl[1] = 0.4f;

  class_quarks[0] = g_quark_from_string ("plant");
  class_quarks[1] = g_quark_from_string ("animal");


  ret = gst_analytics_relation_meta_add_cls_mtd (relations, 2, conf_lvl,
      class_quarks, &cls_mtd[1]);
  gst_analytics_mtd_get_id ((GstAnalyticsMtd *)
      & cls_mtd[1]);

  fail_unless (gst_analytics_relation_meta_set_relation (relations,
          GST_ANALYTICS_REL_TYPE_IS_PART_OF, cls_mtd[0].id,
          cls_mtd[1].id) == TRUE);

  gst_buffer_unref (buf);
}

GST_END_TEST;

GST_START_TEST (test_add_relation_inefficiency_reporting_cases)
{
  /*
   * Verify inefficiency of relation order is reported.
   */
  GstBuffer *buf;
  GstAnalyticsClsMtd cls_mtd[3];
  GstAnalyticsRelationMetaInitParams init_params = { 2, 10 };
  GstAnalyticsRelationMeta *relations;
  gboolean ret;
  GQuark class_quarks[2];


  buf = gst_buffer_new ();
  relations = gst_buffer_add_analytics_relation_meta_full (buf, &init_params);

  gfloat conf_lvl[] = { 0.6f, 0.4f };

  class_quarks[0] = g_quark_from_string ("dog");
  class_quarks[1] = g_quark_from_string ("cat");

  ret = gst_analytics_relation_meta_add_cls_mtd (relations, 2, conf_lvl,
      class_quarks, &cls_mtd[0]);
  fail_unless (gst_analytics_relation_get_length (relations) == 1);
  fail_unless (ret == TRUE);

  conf_lvl[0] = 0.6f;
  conf_lvl[1] = 0.4f;
  class_quarks[0] = g_quark_from_string ("plant");
  class_quarks[1] = g_quark_from_string ("animal");

  ret = gst_analytics_relation_meta_add_cls_mtd (relations, 2, conf_lvl,
      class_quarks, &cls_mtd[1]);
  fail_unless (gst_analytics_relation_get_length (relations) == 2);
  fail_unless (ret == TRUE);

  conf_lvl[0] = 0.6f;
  conf_lvl[1] = 0.4f;
  class_quarks[0] = g_quark_from_string ("male");
  class_quarks[1] = g_quark_from_string ("female");

  ret = gst_analytics_relation_meta_add_cls_mtd (relations, 2, conf_lvl,
      class_quarks, &cls_mtd[2]);
  fail_unless (gst_analytics_relation_get_length (relations) == 3);
  fail_unless (ret == TRUE);

  fail_unless (gst_analytics_relation_meta_set_relation (relations,
          GST_ANALYTICS_REL_TYPE_IS_PART_OF, cls_mtd[0].id, cls_mtd[1].id)
      == TRUE);
  fail_unless (gst_analytics_relation_meta_set_relation (relations,
          GST_ANALYTICS_REL_TYPE_IS_PART_OF, cls_mtd[0].id, cls_mtd[2].id)
      == TRUE);

  gst_buffer_unref (buf);
}

GST_END_TEST;

GST_START_TEST (test_query_relation_meta_cases)
{
  /* Verify we can query existence of direct and indirect relation */
  GstBuffer *buf;
  GstAnalyticsClsMtd cls_mtd[3];
  GstAnalyticsRelationMetaInitParams init_params = { 2, 150 };
  GstAnalyticsRelationMeta *relations;
  gboolean ret;
  GQuark class_quarks[2];

  buf = gst_buffer_new ();
  relations = gst_buffer_add_analytics_relation_meta_full (buf, &init_params);

  gfloat conf_lvl[] = { 0.6f, 0.4f };

  class_quarks[0] = g_quark_from_string ("dog");
  class_quarks[1] = g_quark_from_string ("cat");

  ret = gst_analytics_relation_meta_add_cls_mtd (relations, 2, conf_lvl,
      class_quarks, &cls_mtd[0]);
  fail_unless (ret == TRUE);

  conf_lvl[0] = 0.6f;
  conf_lvl[1] = 0.4f;
  class_quarks[0] = g_quark_from_string ("plant");
  class_quarks[1] = g_quark_from_string ("animal");

  ret = gst_analytics_relation_meta_add_cls_mtd (relations, 2, conf_lvl,
      class_quarks, &cls_mtd[1]);
  fail_unless (ret == TRUE);

  conf_lvl[0] = 0.6f;
  conf_lvl[1] = 0.4f;
  class_quarks[0] = g_quark_from_string ("male");
  class_quarks[1] = g_quark_from_string ("female");

  ret = gst_analytics_relation_meta_add_cls_mtd (relations, 2, conf_lvl,
      class_quarks, &cls_mtd[2]);
  fail_unless (ret == TRUE);

  // Pet is part of kingdom
  gst_analytics_relation_meta_set_relation (relations,
      GST_ANALYTICS_REL_TYPE_IS_PART_OF, cls_mtd[0].id, cls_mtd[1].id);

  // Kingdom contain pet
  gst_analytics_relation_meta_set_relation (relations,
      GST_ANALYTICS_REL_TYPE_CONTAIN, cls_mtd[1].id, cls_mtd[0].id);

  // Pet contain gender
  gst_analytics_relation_meta_set_relation (relations,
      GST_ANALYTICS_REL_TYPE_CONTAIN, cls_mtd[0].id, cls_mtd[2].id);

  /* Query if pet relate kingdom through a IS_PART relation with a maximum
   * relation span of 1. Max relation span of 1 mean they directly related.*/
  gboolean exist = gst_analytics_relation_meta_exist (relations, cls_mtd[0].id,
      cls_mtd[1].id, 1, GST_ANALYTICS_REL_TYPE_IS_PART_OF, NULL);
  fail_unless (exist == TRUE);

  /* Query if pet relate to gender through a IS_PART relation. */
  exist = gst_analytics_relation_meta_exist (relations, cls_mtd[0].id,
      cls_mtd[2].id, 1, GST_ANALYTICS_REL_TYPE_IS_PART_OF, NULL);
  fail_unless (exist == FALSE);

  /* Query if pet relate to kingdom through a CONTAIN relation. */
  exist = gst_analytics_relation_meta_exist (relations, cls_mtd[0].id,
      cls_mtd[1].id, 1, GST_ANALYTICS_REL_TYPE_CONTAIN, NULL);
  fail_unless (exist == FALSE);

  GstAnalyticsRelTypes cond =
      GST_ANALYTICS_REL_TYPE_IS_PART_OF | GST_ANALYTICS_REL_TYPE_CONTAIN |
      GST_ANALYTICS_REL_TYPE_RELATE_TO;

  /* Query if pet relate to gender through IS_PART or CONTAIN or
   * RELATE_TO relation. */
  exist = gst_analytics_relation_meta_exist (relations, cls_mtd[0].id,
      cls_mtd[2].id, 1, cond, NULL);
  fail_unless (exist == TRUE);

  /* Query if pet relate to kindom through CONTAIN or RELATE_TO relation */
  cond = GST_ANALYTICS_REL_TYPE_CONTAIN | GST_ANALYTICS_REL_TYPE_RELATE_TO;
  exist = gst_analytics_relation_meta_exist (relations, cls_mtd[0].id,
      cls_mtd[1].id, 1, cond, NULL);
  fail_unless (exist == FALSE);

  /* Query if kingdom relate to gender through a CONTAIN relation with a maximum
   * relation span of 1. */
  exist = gst_analytics_relation_meta_exist (relations, cls_mtd[1].id,
      cls_mtd[2].id, 1, GST_ANALYTICS_REL_TYPE_CONTAIN, NULL);

  /* We expect this to fail because kingdom relate to gender CONTAIN relations
   * but indirectly (via pet) and we set the max relation span to 1*/
  fail_unless (exist == FALSE);

  /* Same has previous check but using INFINIT relation span */
  exist = gst_analytics_relation_meta_exist (relations, cls_mtd[1].id,
      cls_mtd[2].id, GST_INF_RELATION_SPAN, GST_ANALYTICS_REL_TYPE_CONTAIN,
      NULL);
  fail_unless (exist == TRUE);

  exist = gst_analytics_relation_meta_exist (relations, cls_mtd[2].id,
      cls_mtd[1].id, GST_INF_RELATION_SPAN, GST_ANALYTICS_REL_TYPE_CONTAIN,
      NULL);
  fail_unless (exist == FALSE);

  gst_buffer_unref (buf);
}

GST_END_TEST;

GST_START_TEST (test_path_relation_meta)
{
  /* Verify we can retrieve relation path */
  GstBuffer *buf;
  GstAnalyticsClsMtd cls_mtd[3];
  GstAnalyticsRelationMetaInitParams init_params = { 2, 150 };
  GstAnalyticsRelationMeta *relations;
  gboolean ret;
  GQuark class_quarks[2];

  buf = gst_buffer_new ();
  relations = gst_buffer_add_analytics_relation_meta_full (buf, &init_params);

  gfloat conf_lvl[] = { 0.6f, 0.4f };
  class_quarks[0] = g_quark_from_string ("dog");
  class_quarks[1] = g_quark_from_string ("cat");

  ret = gst_analytics_relation_meta_add_cls_mtd (relations, 2, conf_lvl,
      class_quarks, &cls_mtd[0]);
  fail_unless (ret == TRUE);
  fail_unless (cls_mtd[0].id == 0);

  conf_lvl[0] = 0.6f;
  conf_lvl[1] = 0.4f;
  class_quarks[0] = g_quark_from_string ("plant");
  class_quarks[1] = g_quark_from_string ("animal");

  ret = gst_analytics_relation_meta_add_cls_mtd (relations, 2, conf_lvl,
      class_quarks, &cls_mtd[1]);
  fail_unless (ret == TRUE);
  fail_unless (cls_mtd[1].id == 1);

  conf_lvl[0] = 0.6f;
  conf_lvl[1] = 0.4f;
  class_quarks[0] = g_quark_from_string ("male");
  class_quarks[1] = g_quark_from_string ("female");

  ret = gst_analytics_relation_meta_add_cls_mtd (relations, 2, conf_lvl,
      class_quarks, &cls_mtd[2]);
  fail_unless (ret == TRUE);
  fail_unless (cls_mtd[2].id == 2);

  // Pet is part of kingdom
  gst_analytics_relation_meta_set_relation (relations,
      GST_ANALYTICS_REL_TYPE_IS_PART_OF, cls_mtd[0].id, cls_mtd[1].id);

  // Kingdom contain pet
  gst_analytics_relation_meta_set_relation (relations,
      GST_ANALYTICS_REL_TYPE_CONTAIN, cls_mtd[1].id, cls_mtd[0].id);

  // Pet contain gender
  gst_analytics_relation_meta_set_relation (relations,
      GST_ANALYTICS_REL_TYPE_CONTAIN, cls_mtd[0].id, cls_mtd[2].id);

  GArray *path = NULL;
  GstAnalyticsRelTypes cond = GST_ANALYTICS_REL_TYPE_CONTAIN;
  gboolean exist = gst_analytics_relation_meta_exist (relations, cls_mtd[0].id,
      cls_mtd[2].id, GST_INF_RELATION_SPAN, cond, &path);
  if (exist) {
    fail_unless (path != NULL);
    gint i;
    guint path_check_ids[] = { 0, 2 };
    fail_unless (path->len == 2);
    for (i = 0; i < path->len; i++) {
      fail_unless (path_check_ids[i] == g_array_index (path, guint, i));
    }
    g_array_free (g_steal_pointer (&path), TRUE);
    fail_unless (i == 2);
  }

  cond = GST_ANALYTICS_REL_TYPE_CONTAIN;
  exist = gst_analytics_relation_meta_exist (relations, cls_mtd[1].id,
      cls_mtd[2].id, GST_INF_RELATION_SPAN, cond, &path);
  if (exist) {
    gint i;
    guint path_check_ids[] = { 1, 0, 2 };
    fail_unless (path != NULL);
    fail_unless (path->len == 3);
    for (i = 0; i < path->len; i++) {
      fail_unless (path_check_ids[i] == g_array_index (path, guint, i));
    }
    g_array_free (g_steal_pointer (&path), TRUE);
    fail_unless (i == 3);
  }
  gst_buffer_unref (buf);
}

GST_END_TEST;

GST_START_TEST (test_cyclic_relation_meta)
{
  /* Verify we can discover cycle in relation and not report multiple time
   * the same node and get into an infinit exploration */

  GstBuffer *buf;
  GstAnalyticsClsMtd cls_mtd[3];
  GstAnalyticsRelationMetaInitParams init_params = { 2, 150 };
  GstAnalyticsRelationMeta *relations;
  gfloat conf_lvl[2];
  GQuark class_quarks[2];

  class_quarks[0] = g_quark_from_string ("attr1");
  class_quarks[1] = g_quark_from_string ("attr2");

  buf = gst_buffer_new ();
  relations = gst_buffer_add_analytics_relation_meta_full (buf, &init_params);

  conf_lvl[0] = 0.5f;
  conf_lvl[1] = 0.5f;
  gst_analytics_relation_meta_add_cls_mtd (relations, 2, conf_lvl,
      class_quarks, &cls_mtd[0]);

  gst_analytics_relation_meta_add_cls_mtd (relations, 2, conf_lvl,
      class_quarks, &cls_mtd[1]);

  gst_analytics_relation_meta_add_cls_mtd (relations, 2, conf_lvl,
      class_quarks, &cls_mtd[2]);

  // (0) -> (1)
  gst_analytics_relation_meta_set_relation (relations,
      GST_ANALYTICS_REL_TYPE_IS_PART_OF, cls_mtd[0].id, cls_mtd[1].id);

  // (1)->(2)
  gst_analytics_relation_meta_set_relation (relations,
      GST_ANALYTICS_REL_TYPE_IS_PART_OF, cls_mtd[1].id, cls_mtd[2].id);

  // (2) -> (0)
  gst_analytics_relation_meta_set_relation (relations,
      GST_ANALYTICS_REL_TYPE_IS_PART_OF, cls_mtd[2].id, cls_mtd[0].id);

  GArray *path = NULL;
  GstAnalyticsRelTypes cond = GST_ANALYTICS_REL_TYPE_CONTAIN;
  gboolean exist = gst_analytics_relation_meta_exist (relations, cls_mtd[0].id,
      cls_mtd[2].id, GST_INF_RELATION_SPAN, cond, &path);
  fail_unless (exist == FALSE);

  cond = GST_ANALYTICS_REL_TYPE_IS_PART_OF;
  exist =
      gst_analytics_relation_meta_exist (relations, cls_mtd[0].id,
      cls_mtd[2].id, GST_INF_RELATION_SPAN, cond, &path);
  fail_unless (exist == TRUE);
  if (exist) {
    gint i;
    guint path_ids[] = { 0, 1, 2 };
    fail_unless (path->len == 3);
    for (i = 0; i < path->len; i++) {
      fail_unless (path_ids[i] == g_array_index (path, guint, i));
    }
    g_array_free (g_steal_pointer (&path), TRUE);
    fail_unless (i == 3);
  }

  gst_buffer_unref (buf);
}

GST_END_TEST;

GST_START_TEST (test_add_od_meta)
{
  /* Verity we can add Object Detection relatable metadata to a relation
   * metadata */
  GstBuffer *buf;
  GstAnalyticsRelationMetaInitParams init_params = { 5, 150 };
  GstAnalyticsRelationMeta *rmeta;
  GstAnalyticsODMtd od_mtd;
  gboolean ret;
  buf = gst_buffer_new ();

  rmeta = gst_buffer_add_analytics_relation_meta_full (buf, &init_params);

  GQuark type = g_quark_from_string ("dog");
  gint x = 20;
  gint y = 20;
  gint w = 10;
  gint h = 15;
  gfloat loc_conf_lvl = 0.6f;
  ret = gst_analytics_relation_meta_add_od_mtd (rmeta, type, x, y,
      w, h, loc_conf_lvl, &od_mtd);
  fail_unless (ret == TRUE);
  gst_buffer_unref (buf);
}

GST_END_TEST;

GST_START_TEST (test_add_oriented_od_meta)
{
  /* Verity we can add Oriented Object Detection relatable metadata to a relation
   * metadata */
  GstBuffer *buf;
  GstAnalyticsRelationMetaInitParams init_params = { 5, 150 };
  GstAnalyticsRelationMeta *rmeta;
  GstAnalyticsODMtd od_mtd;
  gboolean ret;
  buf = gst_buffer_new ();

  rmeta = gst_buffer_add_analytics_relation_meta_full (buf, &init_params);

  GQuark type = g_quark_from_string ("dog");
  gint x = 20;
  gint y = 20;
  gint w = 10;
  gint h = 15;
  gfloat r = 0.785f;
  gfloat loc_conf_lvl = 0.6f;
  ret = gst_analytics_relation_meta_add_oriented_od_mtd (rmeta, type, x, y,
      w, h, r, loc_conf_lvl, &od_mtd);
  fail_unless (ret == TRUE);
  gst_buffer_unref (buf);
}

GST_END_TEST;

GST_START_TEST (test_od_meta_fields)
{
  /* Verify we can readback fields of object detection metadata */
  GstBuffer *buf;
  GstAnalyticsRelationMetaInitParams init_params = { 5, 150 };
  GstAnalyticsRelationMeta *rmeta;
  GstAnalyticsODMtd od_mtd;
  gboolean ret;
  buf = gst_buffer_new ();

  rmeta = gst_buffer_add_analytics_relation_meta_full (buf, &init_params);

  GQuark type = g_quark_from_string ("dog");
  gint x = 21;
  gint y = 20;
  gint w = 10;
  gint h = 15;
  gfloat loc_conf_lvl = 0.6f;
  ret = gst_analytics_relation_meta_add_od_mtd (rmeta, type, x, y,
      w, h, loc_conf_lvl, &od_mtd);

  fail_unless (ret == TRUE);

  gint _x, _y, _w, _h;
  gfloat _loc_conf_lvl;
  gst_analytics_od_mtd_get_location (&od_mtd, &_x, &_y, &_w, &_h,
      &_loc_conf_lvl);

  fail_unless (_x == x);
  fail_unless (_y == y);
  fail_unless (_w == w);
  fail_unless (_h == h);
  fail_unless (_loc_conf_lvl == loc_conf_lvl);

  _loc_conf_lvl = -200.0;       // dirty this var by setting invalid value.
  gst_analytics_od_mtd_get_confidence_lvl (&od_mtd, &_loc_conf_lvl);

  fail_unless (_loc_conf_lvl == loc_conf_lvl);

  gst_buffer_unref (buf);
}

GST_END_TEST;

GST_START_TEST (test_oriented_od_meta_fields)
{
  /* Verify we can readback fields of object detection metadata */
  GstBuffer *buf;
  GstAnalyticsRelationMetaInitParams init_params = { 5, 150 };
  GstAnalyticsRelationMeta *rmeta;
  GstAnalyticsODMtd od_mtd;
  gboolean ret;
  buf = gst_buffer_new ();

  rmeta = gst_buffer_add_analytics_relation_meta_full (buf, &init_params);

  GQuark type = g_quark_from_string ("dog");
  gint x = 21;
  gint y = 20;
  gint w = 10;
  gint h = 15;
  gfloat r = 0.785f;
  gfloat loc_conf_lvl = 0.6f;
  ret = gst_analytics_relation_meta_add_oriented_od_mtd (rmeta, type, x, y,
      w, h, r, loc_conf_lvl, &od_mtd);

  fail_unless (ret == TRUE);

  gint _x, _y, _w, _h;
  gfloat _r, _loc_conf_lvl;
  gst_analytics_od_mtd_get_oriented_location (&od_mtd, &_x, &_y, &_w, &_h, &_r,
      &_loc_conf_lvl);

  fail_unless (_x == x);
  fail_unless (_y == y);
  fail_unless (_w == w);
  fail_unless (_h == h);
  fail_unless (_r == r);
  fail_unless (_loc_conf_lvl == loc_conf_lvl);

  _loc_conf_lvl = -200.0;       // dirty this var by setting invalid value.
  gst_analytics_od_mtd_get_confidence_lvl (&od_mtd, &_loc_conf_lvl);

  fail_unless (_loc_conf_lvl == loc_conf_lvl);

  gst_buffer_unref (buf);
}

GST_END_TEST;

GST_START_TEST (test_add_non_oriented_get_oriented_od_meta_fields)
{
  /* Verify we can readback fields of object detection metadata */
  GstBuffer *buf;
  GstAnalyticsRelationMetaInitParams init_params = { 5, 150 };
  GstAnalyticsRelationMeta *rmeta;
  GstAnalyticsODMtd od_mtd;
  gboolean ret;
  buf = gst_buffer_new ();

  rmeta = gst_buffer_add_analytics_relation_meta_full (buf, &init_params);

  GQuark type = g_quark_from_string ("dog");
  gint x = 21;
  gint y = 20;
  gint w = 10;
  gint h = 15;
  gfloat loc_conf_lvl = 0.6f;
  ret = gst_analytics_relation_meta_add_od_mtd (rmeta, type, x, y,
      w, h, loc_conf_lvl, &od_mtd);

  fail_unless (ret == TRUE);

  gint _x, _y, _w, _h;
  gfloat _r, _loc_conf_lvl;
  gst_analytics_od_mtd_get_oriented_location (&od_mtd, &_x, &_y, &_w, &_h, &_r,
      &_loc_conf_lvl);

  fail_unless (_x == x);
  fail_unless (_y == y);
  fail_unless (_w == w);
  fail_unless (_h == h);
  fail_unless (_r == 0);
  fail_unless (_loc_conf_lvl == loc_conf_lvl);

  _loc_conf_lvl = -200.0;       // dirty this var by setting invalid value.
  gst_analytics_od_mtd_get_confidence_lvl (&od_mtd, &_loc_conf_lvl);

  fail_unless (_loc_conf_lvl == loc_conf_lvl);

  gst_buffer_unref (buf);
}

GST_END_TEST;

GST_START_TEST (test_add_oriented_get_non_oriented_od_meta_fields)
{
  /* Verify we can readback fields of object detection metadata */
  GstBuffer *buf;
  GstAnalyticsRelationMeta *rmeta;
  GstAnalyticsODMtd od_mtd;
  gboolean ret;
  gint _x, _y, _w, _h;
  gfloat _loc_conf_lvl;

  buf = gst_buffer_new ();
  rmeta = gst_buffer_add_analytics_relation_meta (buf);

  struct
  {
    gint x, y, w, h;
    gfloat r;
    gint expected_x, expected_y, expected_w, expected_h;
  } test_cases[] = {
    {500, 300, 200, 100, 0.0f, 500, 300, 200, 100},
    {600, 400, 200, 100, 0.785f, 594, 344, 212, 212},
    {400, 400, 150, 100, 1.570f, 425, 375, 100, 150},
    {400, 300, 200, 100, 2.268f, 397, 241, 206, 218},
    {400, 400, 200, 100, 3.142f, 400, 400, 200, 100},
    {300, 300, 150, 100, 4.712f, 325, 275, 100, 150},
    {500, 400, 1, 100, 0.785f, 465, 415, 71, 71},
    {400, 500, 100, 1, 2.268f, 417, 461, 65, 77},
    {400, 400, 100, 100, 6.283f, 400, 400, 100, 100},
  };

  gfloat loc_conf_lvl = 0.9f;
  for (gsize i = 0; i < G_N_ELEMENTS (test_cases); i++) {
    gint x = test_cases[i].x;
    gint y = test_cases[i].y;
    gint w = test_cases[i].w;
    gint h = test_cases[i].h;
    gfloat r = test_cases[i].r;

    gint expected_x = test_cases[i].expected_x;
    gint expected_y = test_cases[i].expected_y;
    gint expected_w = test_cases[i].expected_w;
    gint expected_h = test_cases[i].expected_h;

    ret =
        gst_analytics_relation_meta_add_oriented_od_mtd (rmeta,
        g_quark_from_string ("dog"), x, y, w, h, r, loc_conf_lvl, &od_mtd);
    fail_unless (ret == TRUE);

    gst_analytics_od_mtd_get_location (&od_mtd, &_x, &_y, &_w, &_h,
        &_loc_conf_lvl);

    fail_unless (_x == expected_x);
    fail_unless (_y == expected_y);
    fail_unless (_w == expected_w);
    fail_unless (_h == expected_h);
    fail_unless (_loc_conf_lvl == loc_conf_lvl);
  }

  gst_buffer_unref (buf);
}

GST_END_TEST;

GST_START_TEST (test_od_cls_relation)
{
  /* Verify we can add a object detection and classification metadata to
   * a relation metadata */

  GstBuffer *buf;
  GstAnalyticsClsMtd cls_mtd;
  GstAnalyticsODMtd od_mtd;
  /* We intentionally set buffer small than required to verify sanity
   * with re-allocation */
  GstAnalyticsRelationMetaInitParams init_params = { 5, 150 };
  GstAnalyticsRelationMeta *rmeta;
  gboolean ret;
  GArray *path = NULL;
  gboolean exist;
  gint _x, _y, _w, _h;
  gfloat _loc_conf_lvl;
  GQuark class_quarks[2];

  buf = gst_buffer_new ();
  rmeta = gst_buffer_add_analytics_relation_meta_full (buf, &init_params);

  gfloat conf_lvl[] = { 0.7f, 0.3f };
  class_quarks[0] = g_quark_from_string ("dog");
  class_quarks[1] = g_quark_from_string ("cat");

  gst_analytics_relation_meta_add_cls_mtd (rmeta, 2, conf_lvl,
      class_quarks, &cls_mtd);

  GQuark type = g_quark_from_string ("dog");
  gint x = 21;
  gint y = 20;
  gint w = 10;
  gint h = 15;
  gfloat loc_conf_lvl = 0.6f;
  gst_analytics_relation_meta_add_od_mtd (rmeta, type, x, y, w, h,
      loc_conf_lvl, &od_mtd);

  ret = gst_analytics_relation_meta_set_relation (rmeta,
      GST_ANALYTICS_REL_TYPE_CONTAIN, od_mtd.id, cls_mtd.id);
  fail_unless (ret == TRUE);

  ret = gst_analytics_relation_meta_set_relation (rmeta,
      GST_ANALYTICS_REL_TYPE_IS_PART_OF, cls_mtd.id, od_mtd.id);
  fail_unless (ret == TRUE);

  /* Verify OD relate to CLS only through a CONTAIN relation */
  exist = gst_analytics_relation_meta_exist (rmeta,
      od_mtd.id, cls_mtd.id, GST_INF_RELATION_SPAN,
      GST_ANALYTICS_REL_TYPE_IS_PART_OF, NULL);
  fail_unless (exist == FALSE);

  exist = gst_analytics_relation_meta_exist (rmeta,
      od_mtd.id, cls_mtd.id, GST_INF_RELATION_SPAN,
      GST_ANALYTICS_REL_TYPE_CONTAIN, &path);
  fail_unless (exist == TRUE);

  /* Query the relation path and verify it is correct */
  guint ids[2];
  gint i;
  fail_unless (path->len == 2);
  for (i = 0; i < path->len; i++) {
    ids[i] = g_array_index (path, guint, i);
    GST_LOG ("id=%u", ids[i]);
  }
  g_array_free (g_steal_pointer (&path), TRUE);
  fail_unless (ids[0] == 1);
  fail_unless (ids[1] == 0);

  GstAnalyticsMtd rlt_mtd;
  exist = gst_analytics_relation_meta_get_mtd (rmeta, ids[0], 0, &rlt_mtd);
  fail_unless (exist == TRUE);

  GstAnalyticsMtdType mtd_type = gst_analytics_mtd_get_mtd_type (&rlt_mtd);

  /* Verify relatable meta with id == 1 is of type Object Detection */
  fail_unless (mtd_type == gst_analytics_od_mtd_get_mtd_type ());

  gst_analytics_od_mtd_get_location ((GstAnalyticsODMtd *) & rlt_mtd, &_x, &_y,
      &_w, &_h, &_loc_conf_lvl);
  fail_unless (_x == x);
  fail_unless (_y == y);
  fail_unless (_w == w);
  fail_unless (_h == h);
  fail_unless (_loc_conf_lvl == loc_conf_lvl);

  GST_LOG ("mtd_type:%s", gst_analytics_mtd_type_get_name (mtd_type));

  exist = gst_analytics_relation_meta_get_mtd (rmeta, ids[1], 0, &rlt_mtd);
  fail_unless (exist == TRUE);
  mtd_type = gst_analytics_mtd_get_mtd_type (&rlt_mtd);

  /* Verify relatable meta with id == 0 is of type classification */
  fail_unless (mtd_type == gst_analytics_cls_mtd_get_mtd_type ());
  gint index =
      gst_analytics_cls_mtd_get_index_by_quark ((GstAnalyticsClsMtd *) &
      rlt_mtd,
      g_quark_from_string ("dog"));
  gfloat lvl =
      gst_analytics_cls_mtd_get_level ((GstAnalyticsClsMtd *) & rlt_mtd, index);
  GST_LOG ("dog %f [%d, %d %d, %d", lvl, _x, _y, _w, _h);

  fail_unless (lvl == 0.7f);
  index =
      gst_analytics_cls_mtd_get_index_by_quark ((GstAnalyticsClsMtd *) &
      rlt_mtd, g_quark_from_string ("cat"));
  lvl =
      gst_analytics_cls_mtd_get_level ((GstAnalyticsClsMtd *) & rlt_mtd, index);
  fail_unless (lvl == 0.3f);

  GST_LOG ("mtd_type:%s", gst_analytics_mtd_type_get_name (mtd_type));
  GST_LOG ("cat %f [%d, %d %d, %d", lvl, _x, _y, _w, _h);

  gst_buffer_unref (buf);
}

GST_END_TEST;

GST_START_TEST (test_multi_od_cls_relation)
{
  GstBuffer *buf;
  GstAnalyticsClsMtd cls_mtd[3];
  GstAnalyticsODMtd od_mtd[2];
  GstAnalyticsRelationMetaInitParams init_params = { 5, 150 };
  GstAnalyticsRelationMeta *rmeta;
  guint cls_id, ids[2], i;
  gboolean ret;
  const gint dog_cls_index = 0;
  const gint cat_cls_index = 1;
  gfloat cls_conf_lvl[2], lvl;
  GArray *path = NULL;
  gfloat _loc_conf_lvl;
  gint x, _x, y, _y, w, _w, h, _h;
  GstAnalyticsMtdType mtd_type;
  GQuark cls_type;
  GstAnalyticsMtd mtd;
  gpointer state = NULL;
  GQuark class_quarks[2];
  class_quarks[0] = g_quark_from_string ("dog");
  class_quarks[1] = g_quark_from_string ("cat");


  buf = gst_buffer_new ();
  rmeta = gst_buffer_add_analytics_relation_meta_full (buf, &init_params);

  /* Define first relation ObjectDetection -contain-> Classification */
  cls_conf_lvl[dog_cls_index] = 0.7f;
  cls_conf_lvl[cat_cls_index] = 0.3f;

  gst_analytics_relation_meta_add_cls_mtd (rmeta, 2, cls_conf_lvl,
      class_quarks, &cls_mtd[0]);

  cls_type = g_quark_from_string ("dog");
  x = 21;
  y = 20;
  w = 10;
  h = 15;
  gfloat loc_conf_lvl = 0.6f;
  gst_analytics_relation_meta_add_od_mtd (rmeta, cls_type, x, y, w,
      h, loc_conf_lvl, &od_mtd[0]);

  ret = gst_analytics_relation_meta_set_relation (rmeta,
      GST_ANALYTICS_REL_TYPE_CONTAIN, od_mtd[0].id, cls_mtd[0].id);
  fail_unless (ret == TRUE);
  GST_LOG ("Set rel Obj:%d -c-> Cls:%d", od_mtd[0].id, cls_mtd[0].id);

  /* Define second relation ObjectDetection -contain-> Classification */
  cls_conf_lvl[dog_cls_index] = 0.1f;
  cls_conf_lvl[cat_cls_index] = 0.9f;
  gst_analytics_relation_meta_add_cls_mtd (rmeta, 2, cls_conf_lvl,
      class_quarks, &cls_mtd[1]);

  cls_type = g_quark_from_string ("cat");
  x = 50;
  y = 21;
  w = 11;
  h = 16;
  loc_conf_lvl = 0.7f;
  gst_analytics_relation_meta_add_od_mtd (rmeta, cls_type, x, y, w,
      h, loc_conf_lvl, &od_mtd[1]);

  ret = gst_analytics_relation_meta_set_relation (rmeta,
      GST_ANALYTICS_REL_TYPE_CONTAIN, od_mtd[1].id, cls_mtd[1].id);

  GST_LOG ("Set rel Obj:%d -c-> Cls:%d", od_mtd[1].id, cls_mtd[1].id);
  fail_unless (ret == TRUE);

  /* Query relations */

  /* Query relation between first object detection and first classification
   * and verify they are only related by CONTAIN relation OD relate to
   * CLASSIFICATION through a CONTAIN relation. */
  gboolean exist =
      gst_analytics_relation_meta_exist (rmeta, od_mtd[0].id, cls_mtd[0].id,
      GST_INF_RELATION_SPAN,
      GST_ANALYTICS_REL_TYPE_IS_PART_OF, NULL);
  fail_unless (exist == FALSE);

  exist =
      gst_analytics_relation_meta_exist (rmeta, od_mtd[0].id, cls_mtd[0].id,
      GST_INF_RELATION_SPAN, GST_ANALYTICS_REL_TYPE_CONTAIN, NULL);
  fail_unless (exist == TRUE);


  /* Query relation between second object detection and second classification
   * and verify they are only related by CONTAIN relation OD relate to
   * CLASSIFICATION through a CONTAIN relation.
   */
  exist =
      gst_analytics_relation_meta_exist (rmeta, od_mtd[1].id, cls_mtd[1].id,
      GST_INF_RELATION_SPAN, GST_ANALYTICS_REL_TYPE_CONTAIN, &path);
  fail_unless (exist == TRUE);

  /* Verify relation path between OD second (id=3) and Cls second (id=2)
   * is correct
   */
  fail_unless (path->len == 2);
  for (i = 0; i < path->len; i++) {
    ids[i] = g_array_index (path, guint, i);
    GST_LOG ("id=%u", ids[i]);
  }
  g_array_free (g_steal_pointer (&path), TRUE);
  fail_unless (ids[0] == 3);
  fail_unless (ids[1] == 2);

  /* Verify the relatable metadata 3 is of correct type
   * (ObjectDetection). Verify it describe the correct
   * the correct data.
   */
  gst_analytics_relation_meta_get_mtd (rmeta, ids[0], 0, &mtd);
  mtd_type = gst_analytics_mtd_get_mtd_type (&mtd);
  fail_unless (mtd_type == gst_analytics_od_mtd_get_mtd_type ());

  gst_analytics_od_mtd_get_location ((GstAnalyticsODMtd *) & mtd, &_x, &_y, &_w,
      &_h, &_loc_conf_lvl);
  fail_unless (_x == 50);
  fail_unless (_y == 21);
  fail_unless (_w == 11);
  fail_unless (_h == 16);
  fail_unless (_loc_conf_lvl == 0.7f);

  GST_LOG ("mtd_type:%s", gst_analytics_mtd_type_get_name (mtd_type));

  /* Verify the relatable metadata 2 is of correct type
   * (ObjectDetection).
   */
  gst_analytics_relation_meta_get_mtd (rmeta, ids[1], 0, &mtd);
  mtd_type = gst_analytics_mtd_get_mtd_type (&mtd);
  fail_unless (mtd_type == gst_analytics_cls_mtd_get_mtd_type ());

  /* Verify data of the CLASSIFICATION retrieved */
  gint index =
      gst_analytics_cls_mtd_get_index_by_quark ((GstAnalyticsClsMtd *) & mtd,
      g_quark_from_string ("dog"));
  lvl = gst_analytics_cls_mtd_get_level ((GstAnalyticsClsMtd *) & mtd, index);
  GST_LOG ("dog %f [%d, %d %d, %d", lvl, _x, _y, _w, _h);
  fail_unless (lvl == 0.1f);

  /* Verify data of the CLASSIFICATION retrieved */
  index =
      gst_analytics_cls_mtd_get_index_by_quark ((GstAnalyticsClsMtd *) & mtd,
      g_quark_from_string ("cat"));
  lvl = gst_analytics_cls_mtd_get_level ((GstAnalyticsClsMtd *) & mtd, index);
  GST_LOG ("mtd_type:%s", gst_analytics_mtd_type_get_name (mtd_type));
  GST_LOG ("cat %f [%d, %d %d, %d", lvl, _x, _y, _w, _h);
  fail_unless (lvl == 0.9f);

  /* Retrieve relatable metadata related to the first object detection
   * through a CONTAIN relation of type CLASSIFICATION
   * Verify it's the first classification metadata
   */
  gst_analytics_relation_meta_get_direct_related (rmeta, od_mtd[0].id,
      GST_ANALYTICS_REL_TYPE_CONTAIN, gst_analytics_cls_mtd_get_mtd_type (),
      &state, &mtd);

  cls_id = gst_analytics_mtd_get_id (&mtd);
  GST_LOG ("Obj:%d -> Cls:%d", od_mtd[0].id, cls_id);
  fail_unless (cls_id == cls_mtd[0].id);

  state = NULL;
  /* Retrieve relatable metadata related to the second object detection
   * through a CONTAIN relation of type CLASSIFICATION
   * Verify it's the first classification metadata
   */
  gst_analytics_relation_meta_get_direct_related (rmeta, od_mtd[1].id,
      GST_ANALYTICS_REL_TYPE_CONTAIN, gst_analytics_cls_mtd_get_mtd_type (),
      &state, &mtd);
  cls_id = gst_analytics_mtd_get_id (&mtd);

  GST_LOG ("Obj:%d -> Cls:%d", od_mtd[1].id, cls_id);
  fail_unless (cls_id == cls_mtd[1].id);

  cls_conf_lvl[dog_cls_index] = 0.2f;
  cls_conf_lvl[cat_cls_index] = 0.8f;
  class_quarks[0] = g_quark_from_string ("canine");
  class_quarks[1] = g_quark_from_string ("feline");
  gst_analytics_relation_meta_add_cls_mtd (rmeta, 2, cls_conf_lvl,
      class_quarks, &cls_mtd[2]);

  ret = gst_analytics_relation_meta_set_relation (rmeta,
      GST_ANALYTICS_REL_TYPE_CONTAIN, od_mtd[1].id, cls_mtd[2].id);

  state = NULL;
  ret = gst_analytics_relation_meta_get_direct_related (rmeta, od_mtd[1].id,
      GST_ANALYTICS_REL_TYPE_CONTAIN, gst_analytics_od_mtd_get_mtd_type (),
      &state, &mtd);

  fail_unless (ret == FALSE);

  gst_buffer_unref (buf);
}

GST_END_TEST;

GST_START_TEST (test_add_tracking_meta)
{
  /* Verify we can add tracking relatable meta to relation meta */
  GstBuffer *buf1, *buf2;
  GstAnalyticsRelationMetaInitParams init_params = { 5, 150 };
  GstAnalyticsRelationMeta *rmeta;
  GstAnalyticsTrackingMtd tracking_mtd;
  guint tracking_id;
  GstClockTime tracking_observation_time_1;
  gboolean ret;

  /* Verify we can add multiple trackings to relation metadata
   */

  buf1 = gst_buffer_new ();
  rmeta = gst_buffer_add_analytics_relation_meta_full (buf1, &init_params);
  tracking_id = 1;
  tracking_observation_time_1 = GST_BUFFER_TIMESTAMP (buf1);
  ret = gst_analytics_relation_meta_add_tracking_mtd (rmeta, tracking_id,
      tracking_observation_time_1, &tracking_mtd);
  fail_unless (ret == TRUE);

  gst_buffer_unref (buf1);

  buf2 = gst_buffer_new ();
  rmeta = gst_buffer_add_analytics_relation_meta_full (buf2, &init_params);
  tracking_id = 1;
  ret = gst_analytics_relation_meta_add_tracking_mtd (rmeta, tracking_id,
      tracking_observation_time_1, &tracking_mtd);
  fail_unless (ret == TRUE);

  gst_buffer_unref (buf2);
}

GST_END_TEST;

GST_START_TEST (test_verify_mtd_clear)
{
  /* This test use segmentation mtd but it's a general functionality of
   * analytics-meta that _mtd_clear is called when buffer is freed.
   * _mtd_clear should be called regardless if the buffer where relation-meta
   * is attached is from a pool or not. This test verify that _mtd_clear is
   * called when buffer where relation-meta is attached it not from a pool.
   */
  GstBuffer *vbuf, *mbuf;
  GstAnalyticsRelationMetaInitParams init_params = { 5, 150 };
  GstAnalyticsRelationMeta *rmeta;
  GstBufferPool *mpool;
  GstCaps *caps;
  GstStructure *config;
  GstVideoInfo minfo;
  GstAnalyticsSegmentationMtd smtd;
  GstBufferPoolAcquireParams pool_acq_params = { 0, };
  pool_acq_params.flags = GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT;

  vbuf = gst_buffer_new ();
  rmeta = gst_buffer_add_analytics_relation_meta_full (vbuf, &init_params);

  /* Create pool for segmentation masks */
  gst_video_info_init (&minfo);
  gst_video_info_set_format (&minfo, GST_VIDEO_FORMAT_GRAY8, 32, 32);
  caps = gst_video_info_to_caps (&minfo);
  mpool = gst_video_buffer_pool_new ();
  config = gst_buffer_pool_get_config (mpool);

  /* Here we intentionnaly create a pool of only one element to validate the
   * buffer used to store the masks is returned to the pool when the video
   * buffer to which it is attached is unreffed with the intention of having
   * gst_buffer_pool_acquire_buffer (mpool,...) fail if it didn't happen.*/
  gst_buffer_pool_config_set_params (config, caps, minfo.size, 0, 1);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_set_config (mpool, config);
  gst_buffer_pool_set_active (mpool, TRUE);
  gst_caps_unref (caps);

  fail_unless (gst_buffer_pool_acquire_buffer (mpool, &mbuf,
          &pool_acq_params) == GST_FLOW_OK);

  /* Here we pretend the masks contain 2 region types [2,3] */
  static const gsize region_count = 2;
  guint region_ids[] = { 2, 3 };

  gst_analytics_relation_meta_add_segmentation_mtd (rmeta, mbuf,
      GST_SEGMENTATION_TYPE_INSTANCE, region_count, region_ids, 0, 0, 0, 0,
      &smtd);

  /* This _unref will dispose vbuf and also mbuf to mpool
   * because GstAnalyticsSegmentationMtd define a
   * GstAnalyticsMtdImpl::mtd_meta_clear */
  gst_buffer_unref (vbuf);

  /* This will succeed because mbuf was returned to the pool. If this
   * test fail it highlight a memory managemnt failure in analytics-meta.*/
  fail_unless (gst_buffer_pool_acquire_buffer (mpool, &mbuf,
          &pool_acq_params) == GST_FLOW_OK);

  gst_buffer_unref (mbuf);
  gst_buffer_pool_set_active (mpool, FALSE);
  gst_object_unref (mpool);
}

GST_END_TEST;

GST_START_TEST (test_add_segmentation_meta)
{
  /*
   * This a very simple test that add a segmentation analytics-meta
   * to a buffer. In this test the masks have the same resolution as
   * the buffer. It verify that masks (GstBuffer) memory management by
   * validating the GstBuffer was returned to the pool by
   */
  GstBuffer *vbuf, *mbuf;
  GstAnalyticsRelationMetaInitParams init_params = { 5, 150 };
  GstAnalyticsRelationMeta *rmeta;
  GstBufferPool *vpool, *mpool;
  GstCaps *caps;
  GstStructure *config;
  GstVideoInfo vinfo, minfo;
  GstAnalyticsSegmentationMtd smtd;

  /* Create a pool for video frames */
  gst_video_info_init (&vinfo);
  vpool = gst_video_buffer_pool_new ();
  config = gst_buffer_pool_get_config (vpool);
  gst_video_info_set_format (&vinfo, GST_VIDEO_FORMAT_RGBA, 32, 32);
  caps = gst_video_info_to_caps (&vinfo);
  gst_buffer_pool_config_set_params (config, caps, vinfo.size, 0, 0);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_set_config (vpool, config);
  gst_buffer_pool_set_active (vpool, TRUE);
  gst_caps_unref (caps);

  fail_unless (gst_buffer_pool_acquire_buffer (vpool, &vbuf, NULL) ==
      GST_FLOW_OK);

  rmeta = gst_buffer_add_analytics_relation_meta_full (vbuf, &init_params);

  /* Create pool for segmentation masks */
  gst_video_info_init (&minfo);
  gst_video_info_set_format (&minfo, GST_VIDEO_FORMAT_GRAY8, 32, 32);
  caps = gst_video_info_to_caps (&minfo);
  mpool = gst_video_buffer_pool_new ();
  config = gst_buffer_pool_get_config (mpool);

  /* Here we intentionnaly create a pool of only one element to validate the
   * buffer used to store the masks is returned to the pool when the video
   * buffer to which it is attached is unreffed with the intention of having
   * gst_buffer_pool_acquire_buffer (mpool,...) fail if it didn't happen.*/
  gst_buffer_pool_config_set_params (config, caps, minfo.size, 0, 1);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_set_config (mpool, config);
  gst_buffer_pool_set_active (mpool, TRUE);
  gst_caps_unref (caps);

  fail_unless (gst_buffer_pool_acquire_buffer (mpool, &mbuf, NULL) ==
      GST_FLOW_OK);

  /* Here we pretend the masks contain 2 region types [2,3] */
  static const gsize region_count = 2;
  guint region_ids[] = { 2, 3 };

  gst_analytics_relation_meta_add_segmentation_mtd (rmeta, mbuf,
      GST_SEGMENTATION_TYPE_INSTANCE, region_count, region_ids, 0, 0, 0, 0,
      &smtd);

  /* This _unref will return vbuf to vpool and also mbuf to mpool
   * because GstAnalyticsSegmentationMtd define a
   * GstAnalyticsMtdImpl::mtd_meta_clear */
  gst_buffer_unref (vbuf);

  /* This will succeed because mbuf was returned to the pool. If this
   * test fail it highlight a memory managemnt failure in analytics-meta.*/
  fail_unless (gst_buffer_pool_acquire_buffer (mpool, &mbuf, NULL) ==
      GST_FLOW_OK);

  gst_buffer_unref (mbuf);
  gst_buffer_pool_set_active (mpool, FALSE);
  gst_object_unref (mpool);
  gst_buffer_pool_set_active (vpool, FALSE);
  gst_object_unref (vpool);
}

GST_END_TEST;

GST_START_TEST (test_associate_segmentation_meta)
{
  /* This test verify that classification can be associated to segmentation.
   * More specifically we use a grayscale image that contain 2 set of
   * imbricated pattern. The segmentation problem is simplified by having a
   * specific value for each region. In a sense the original image is already
   * segmented to avoid to do a real segmentation, since the goal of this test
   * is to very segmentation analytics-meta API.
   *
   * Original image: 32x24 grayscale
   * Segmentation input: 16x16 grayscale
   * Segmentation output: 16x16 tensor (where each value correspond to a region
   * id in the input)
   */

  GstBuffer *vbuf, *mbuf;
  GstAnalyticsRelationMetaInitParams init_params = { 5, 150 };
  GstAnalyticsRelationMeta *rmeta;
  GstBufferPool *vpool, *mpool;
  GstCaps *caps;
  GstStructure *config;
  GstVideoInfo vinfo, minfo;
  GstAnalyticsSegmentationMtd smtd;
  GstAnalyticsClsMtd clsmtd;

  /* Create a pool for video frames */
  gst_video_info_init (&vinfo);
  vpool = gst_video_buffer_pool_new ();
  config = gst_buffer_pool_get_config (vpool);
  gst_video_info_set_format (&vinfo, GST_VIDEO_FORMAT_GRAY8, 32, 24);
  caps = gst_video_info_to_caps (&vinfo);
  gst_buffer_pool_config_set_params (config, caps, vinfo.size, 0, 0);
  gst_buffer_pool_set_config (vpool, config);
  gst_buffer_pool_set_active (vpool, TRUE);
  gst_caps_unref (caps);

  fail_unless (gst_buffer_pool_acquire_buffer (vpool, &vbuf, NULL) ==
      GST_FLOW_OK);

  /* This image a 32 x 24, GRAY8  that contain  └ and ┐ that are imbricated.
   * └ is formed by pixels values of: 9 and 7.
   * ┐ is formed by pixels values of: 8 and 6.
   * 9 and 8 are imbricated and 7 and 6 are impbricated. Pixel values [6,7,8,9],
   * have no importance.
   */

  guint8 img[] = {
    /*        0|0|0|0|0|0|0|0|0|0|1|1|1|1|1|1|1|1|1|1|2|2|2|2|2|2|2|2|2|2|3|3  */
    /*        0|1|2|3|4|5|6|7|8|9|0|1|2|3|4|5|6|7|8|9|0|1|2|3|4|5|6|7|8|9|0|1  */
    /* 0  */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 1  */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 2  */ 0, 0, 9, 9, 9, 9, 8, 8, 8, 8, 8, 8, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 3  */ 0, 0, 9, 9, 9, 9, 8, 8, 8, 8, 8, 8, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 4  */ 0, 0, 9, 9, 9, 9, 8, 8, 8, 8, 8, 8, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 5  */ 0, 0, 9, 9, 9, 9, 8, 8, 8, 8, 8, 8, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 6  */ 0, 0, 9, 9, 9, 9, 9, 9, 9, 9, 8, 8, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 7  */ 0, 0, 9, 9, 9, 9, 9, 9, 9, 9, 8, 8, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 8  */ 0, 0, 9, 9, 9, 9, 9, 9, 9, 9, 8, 8, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 9  */ 0, 0, 9, 9, 9, 9, 9, 9, 9, 9, 8, 8, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 10 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 11 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 12 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 7, 7, 7, 6, 6,
    6, 6, 6, 6, 6, 6, 0, 0, 0, 0,
    /* 13 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 7, 7, 7, 6, 6,
    6, 6, 6, 6, 6, 6, 0, 0, 0, 0,
    /* 14 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 7, 7, 7, 6, 6,
    6, 6, 6, 6, 6, 6, 0, 0, 0, 0,
    /* 15 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 7, 7, 7, 6, 6,
    6, 6, 6, 6, 6, 6, 0, 0, 0, 0,
    /* 16 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 7, 7, 7, 7, 7,
    7, 7, 6, 6, 6, 6, 0, 0, 0, 0,
    /* 17 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 7, 7, 7, 7, 7,
    7, 7, 6, 6, 6, 6, 0, 0, 0, 0,
    /* 18 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 7, 7, 7, 7, 7,
    7, 7, 6, 6, 6, 6, 0, 0, 0, 0,
    /* 19 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 7, 7, 7, 7, 7,
    7, 7, 6, 6, 6, 6, 0, 0, 0, 0,
    /* 20 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 21 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 22 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 23 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0
  };

  /* Pre-processing step 1 will convert the original image to segmentation
   * processing format.
   *
   * Decimation (32x24) -> (16x12)
   0|0|0|0|0|0|0|0|0|0|1|1|1|1|1|1|
   0|1|2|3|4|5|6|7|8|9|0|1|2|3|4|5|

   0      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   1      0,9,9,8,8,8,8,0,0,0,0,0,0,0,0,0,
   2      0,9,9,8,8,8,8,0,0,0,0,0,0,0,0,0,
   3      0,9,9,9,9,8,8,0,0,0,0,0,0,0,0,0,
   4      0,9,9,9,9,8,8,0,0,0,0,0,0,0,0,0,
   5      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   6      0,0,0,0,0,0,0,0,7,7,6,6,6,6,0,0,
   7      0,0,0,0,0,0,0,0,7,7,6,6,6,6,0,0,
   8      0,0,0,0,0,0,0,0,7,7,7,7,6,6,0,0,
   9      0,0,0,0,0,0,0,0,7,7,7,7,6,6,0,0,
   10     0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   11     0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, */


  /* Pre-processing step 2: Add padding match segmentation input format
   * Padding top-bottom (16x12) -> (16x16)
   *
   0|0|0|0|0|0|0|0|0|0|1|1|1|1|1|1|
   0|1|2|3|4|5|6|7|8|9|0|1|2|3|4|5|

   0      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  Padding line
   1      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  Padding line
   1      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   3      0,9,9,8,8,8,8,0,0,0,0,0,0,0,0,0,
   4      0,9,9,8,8,8,8,0,0,0,0,0,0,0,0,0,
   5      0,9,9,9,9,8,8,0,0,0,0,0,0,0,0,0,
   6      0,9,9,9,9,8,8,0,0,0,0,0,0,0,0,0,
   7      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   8      0,0,0,0,0,0,0,0,7,7,6,6,6,6,0,0,
   9      0,0,0,0,0,0,0,0,7,7,6,6,6,6,0,0,
   10     0,0,0,0,0,0,0,0,7,7,7,7,6,6,0,0,
   11     0,0,0,0,0,0,0,0,7,7,7,7,6,6,0,0,
   12     0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   13     0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   14     0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  Padding line
   15     0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  Padding line */


  /* Post-processing remove area of the output that correspond to padding
   * area in the input. In the following array 2, 3, 4, 5 correspond to
   * segmented region ids.*/
  guint8 post_proc_segmasks[] = {
    /*        0|0|0|0|0|0|0|0|0|0|1|1|1|1|1|1| */
    /*        0|1|2|3|4|5|6|7|8|9|0|1|2|3|4|5| */
    /* 0  */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 1  */ 0, 2, 2, 3, 3, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 2  */ 0, 2, 2, 3, 3, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 3  */ 0, 2, 2, 2, 2, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 4  */ 0, 2, 2, 2, 2, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 5  */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 6  */ 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 5, 5, 5, 5, 0, 0,
    /* 7  */ 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 5, 5, 5, 5, 0, 0,
    /* 8  */ 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 4, 5, 5, 0, 0,
    /* 9  */ 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 4, 5, 5, 0, 0,
    /* 10 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 11 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
  };

  /* Creating analytics-relation meta to host analytics results */
  rmeta = gst_buffer_add_analytics_relation_meta_full (vbuf, &init_params);

  /* Create pool for segmentation masks */
  gst_video_info_init (&minfo);
  gst_video_info_set_format (&minfo, GST_VIDEO_FORMAT_GRAY8, 32, 24);
  caps = gst_video_info_to_caps (&minfo);
  mpool = gst_video_buffer_pool_new ();
  config = gst_buffer_pool_get_config (mpool);

  gst_buffer_pool_config_set_params (config, caps, minfo.size, 1, 0);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_set_config (mpool, config);
  gst_buffer_pool_set_active (mpool, TRUE);
  gst_caps_unref (caps);

  fail_unless (gst_buffer_pool_acquire_buffer (mpool, &mbuf, NULL) ==
      GST_FLOW_OK);

  gst_buffer_fill (mbuf, 0, post_proc_segmasks, 32 * 24);

  /* Masks contain 5 region types [0,1,2,3,4]. We intentionnally change the
   * order of region ids relative to their appearance in the output to
   * show that API does not depend on any order or continuity. */
  static const gsize region_count = 5;
  guint region_ids[5];

  /* Confidence levels are irrelevant in this context. */
  gfloat confi[] = { 1.0, 1.0, 1.0, 1.0, 1.0 };
  GQuark classes[5];

  /* It's important that region index and classification index match. This
   * is how a region is associated to a specific class.
   *
   * Region-id-0 correspond to region-index-0 is associated to class
   * "background".
   * Region-id-5 correspond to region-index-1 is associated to class
   * "top-right-corner".  ┐
   * Region-id-2 correspond to region-index-2 is associated to class
   * "bottom-left-corner". └
   * Region-id-3 correspond to region-index-3 is associated to class
   * "top-right-corner". ┐
   * Region-id-4 correspond to region-index-4 is associated to class
   * "bottom-left-corner". └  */
  region_ids[0] = 0;
  classes[0] = g_quark_from_string ("background");

  region_ids[1] = 5;
  classes[1] = g_quark_from_string ("top-right-corner");

  region_ids[2] = 2;
  classes[2] = g_quark_from_string ("bottom-left-corner");

  region_ids[3] = 3;
  classes[3] = g_quark_from_string ("top-right-corner");

  region_ids[4] = 4;
  classes[4] = g_quark_from_string ("bottom-left-corner");

  gst_analytics_relation_meta_add_segmentation_mtd (rmeta, mbuf,
      GST_SEGMENTATION_TYPE_INSTANCE, region_count, region_ids, 0, 0, 0, 0,
      &smtd);

  gst_analytics_relation_meta_add_cls_mtd (rmeta, region_count, confi,
      classes, &clsmtd);

  gst_analytics_relation_meta_set_relation (rmeta,
      GST_ANALYTICS_REL_TYPE_RELATE_TO, smtd.id, clsmtd.id);


  /* Generate a truth vector for segmented region and associated class. */
  guint8 truth_vector_segmentation_id[768];
  GQuark truth_vector_segmentation_classes[768];

  for (gsize i = 0; i < 768; i++) {
    if (img[i] == 9) {
      truth_vector_segmentation_id[i] = 2;
      truth_vector_segmentation_classes[i] =
          g_quark_from_string ("bottom-left-corner");
    } else if (img[i] == 8) {
      truth_vector_segmentation_id[i] = 3;
      truth_vector_segmentation_classes[i] =
          g_quark_from_string ("top-right-corner");
    } else if (img[i] == 7) {
      truth_vector_segmentation_id[i] = 4;
      truth_vector_segmentation_classes[i] =
          g_quark_from_string ("bottom-left-corner");
    } else if (img[i] == 6) {
      truth_vector_segmentation_id[i] = 5;
      truth_vector_segmentation_classes[i] =
          g_quark_from_string ("top-right-corner");
    } else {
      truth_vector_segmentation_id[i] = 0;
      truth_vector_segmentation_classes[i] = g_quark_from_string ("background");
    }
  }

  /* Verify segmentation analytics-meta and associated classification
   * match truth vectors */
  gsize idx;
  GstBufferMapInfo mmap_info;   /* mask map info */
  gst_buffer_map (mbuf, &mmap_info, GST_MAP_READ);
  for (gsize r = 0; r < 24; r++) {
    gsize mr = r / 2;
    for (gsize c = 0; c < 32; c++) {
      gsize mc = c / 2;
      gsize mask_idx = mr * 16 + mc;
      gsize img_idx = r * 32 + c;

      fail_unless (mmap_info.data[mask_idx] ==
          truth_vector_segmentation_id[img_idx]);

      /* Retrieve segmentation region index */
      fail_unless (gst_analytics_segmentation_mtd_get_region_index (&smtd,
              &idx, mmap_info.data[mask_idx]) == TRUE);

      /* Check that the _get_region_id() API is consistent */
      fail_unless (gst_analytics_segmentation_mtd_get_region_id (&smtd,
              idx) == mmap_info.data[mask_idx]);

      /* Retrieve classification associated with region */
      fail_unless (gst_analytics_relation_meta_get_direct_related (rmeta,
              smtd.id, GST_ANALYTICS_REL_TYPE_RELATE_TO,
              gst_analytics_cls_mtd_get_mtd_type (), NULL, &clsmtd));

      /* Retrive class associated with segmentation region */
      fail_unless (gst_analytics_cls_mtd_get_length (&clsmtd) ==
          gst_analytics_segmentation_mtd_get_region_count (&smtd));

      /* Retrieve class associated with segmentation region */
      fail_unless (gst_analytics_cls_mtd_get_quark (&clsmtd, idx) ==
          truth_vector_segmentation_classes[img_idx]);
    }
  }
  gst_buffer_unmap (mbuf, &mmap_info);


  /* This _unref will return vbuf to vpool and also mbuf to mpool
   * because GstAnalyticsSegmentationMtd define a
   * GstAnalyticsMtdImpl::mtd_meta_clear */
  gst_buffer_unref (vbuf);

  gst_buffer_pool_set_active (mpool, FALSE);
  gst_object_unref (mpool);
  gst_buffer_pool_set_active (vpool, FALSE);
  gst_object_unref (vpool);
}

GST_END_TEST;

static Suite *
analyticmeta_suite (void)
{

  Suite *s;
  TCase *tc_chain_cls;
  TCase *tc_chain_relation;
  TCase *tc_chain_od;
  TCase *tc_chain_od_cls;
  TCase *tc_chain_tracking;
  TCase *tc_chain_segmentation;

  s = suite_create ("Analytic Meta Library");

  tc_chain_cls = tcase_create ("Classification Mtd");
  suite_add_tcase (s, tc_chain_cls);
  tcase_add_test (tc_chain_cls, test_add_classification_meta);
  tcase_add_test (tc_chain_cls, test_classification_meta_classes);

  tcase_add_test (tc_chain_cls, test_meta_pooled);

  tc_chain_relation = tcase_create ("Relation Meta");
  suite_add_tcase (s, tc_chain_relation);
  tcase_add_test (tc_chain_relation, test_add_relation_meta);
  tcase_add_test (tc_chain_relation,
      test_add_relation_inefficiency_reporting_cases);
  tcase_add_test (tc_chain_relation, test_query_relation_meta_cases);
  tcase_add_test (tc_chain_relation, test_path_relation_meta);
  tcase_add_test (tc_chain_relation, test_cyclic_relation_meta);
  tcase_add_test (tc_chain_relation, test_verify_mtd_clear);

  tc_chain_od = tcase_create ("Object Detection Mtd");
  suite_add_tcase (s, tc_chain_od);
  tcase_add_test (tc_chain_od, test_add_od_meta);
  tcase_add_test (tc_chain_od, test_add_oriented_od_meta);
  tcase_add_test (tc_chain_od, test_od_meta_fields);
  tcase_add_test (tc_chain_od, test_oriented_od_meta_fields);
  tcase_add_test (tc_chain_od,
      test_add_non_oriented_get_oriented_od_meta_fields);
  tcase_add_test (tc_chain_od,
      test_add_oriented_get_non_oriented_od_meta_fields);

  tc_chain_od_cls = tcase_create ("Object Detection <-> Classification Mtd");
  suite_add_tcase (s, tc_chain_od_cls);
  tcase_add_test (tc_chain_od_cls, test_od_cls_relation);
  tcase_add_test (tc_chain_od_cls, test_multi_od_cls_relation);

  tc_chain_tracking = tcase_create ("Tracking Mtd");
  suite_add_tcase (s, tc_chain_tracking);
  tcase_add_test (tc_chain_tracking, test_add_tracking_meta);

  tc_chain_segmentation = tcase_create ("Segmentation Mtd");
  suite_add_tcase (s, tc_chain_segmentation);
  tcase_add_test (tc_chain_segmentation, test_add_segmentation_meta);
  tcase_add_test (tc_chain_segmentation, test_associate_segmentation_meta);

  return s;
}

GST_CHECK_MAIN (analyticmeta);
