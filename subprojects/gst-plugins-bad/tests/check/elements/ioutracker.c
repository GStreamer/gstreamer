/*
 * GStreamer gstreamer-ioutracker
 * Copyright (C) 2025 Collabora Ltd.
 *  author: Olivier Crête <olivier.crete@collabora.com>
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
#include <gst/check/gstharness.h>

#include <gst/analytics/analytics.h>

static GstHarness *
setup_ioutracker (void)
{
  GstHarness *h = gst_harness_new ("ioutracker");

  gst_harness_play (h);
  gst_harness_set_src_caps (h, gst_caps_new_empty_simple ("video/x-raw"));

  return h;
}

static GstBuffer *
create_buffer (GstClockTime ts, guint x, guint y, gint h, gint w,
    GstAnalyticsODMtd * od_mtd)
{
  GstBuffer *b = gst_buffer_new ();
  GstAnalyticsRelationMeta *rmeta = gst_buffer_add_analytics_relation_meta (b);

  GST_BUFFER_PTS (b) = GST_BUFFER_DTS (b) = ts;

  gst_analytics_relation_meta_add_od_mtd (rmeta, 0, x, y, w, h, 1.0, od_mtd);

  return b;
}

GST_START_TEST (test_no_intersection)
{
  GstHarness *h = setup_ioutracker ();
  GstBuffer *b;
  GstAnalyticsRelationMeta *rmeta;
  GstAnalyticsODMtd od_mtd;
  GstAnalyticsTrackingMtd t_mtd, t_mtd2;
  guint64 tracking_id;
  guint64 tracking_id1;
  GstClockTime tracking_first_seen, tracking_last_seen;
  gboolean tracking_lost;
  gpointer state = NULL;
  gboolean has_1 = FALSE;
  gboolean has_2 = FALSE;

  b = gst_harness_push_and_pull (h, create_buffer (0, 0, 0, 10, 10, &od_mtd));
  fail_unless (b);

  rmeta = gst_buffer_get_analytics_relation_meta (b);
  fail_unless (rmeta == od_mtd.meta);
  fail_unless_equals_int (gst_analytics_relation_get_length (rmeta), 2);

  fail_unless (gst_analytics_relation_meta_get_direct_related (rmeta,
          od_mtd.id, GST_ANALYTICS_REL_TYPE_RELATE_TO,
          gst_analytics_tracking_mtd_get_mtd_type (), NULL, &t_mtd));


  fail_unless (gst_analytics_tracking_mtd_get_info (&t_mtd,
          &tracking_id, &tracking_first_seen, &tracking_last_seen,
          &tracking_lost));
  fail_unless_equals_int64 (tracking_first_seen, 0);
  fail_unless_equals_int64 (tracking_last_seen, 0);
  fail_unless (tracking_lost == FALSE);
  tracking_id1 = tracking_id;

  gst_buffer_unref (b);

  /* Now send a second buffer in a separate location */
  b = gst_harness_push_and_pull (h, create_buffer (10, 20, 20, 10, 10,
          &od_mtd));
  fail_unless (b);

  /* Now one object and 2 tracks */
  fail_unless_equals_int (gst_analytics_relation_get_length (od_mtd.meta), 3);

  fail_unless (gst_analytics_relation_meta_get_direct_related (od_mtd.meta,
          od_mtd.id, GST_ANALYTICS_REL_TYPE_RELATE_TO,
          gst_analytics_tracking_mtd_get_mtd_type (), NULL, &t_mtd));

  fail_unless (gst_analytics_tracking_mtd_get_info (&t_mtd,
          &tracking_id, &tracking_first_seen, &tracking_last_seen,
          &tracking_lost));
  fail_unless_equals_int64 (tracking_first_seen, 10);
  fail_unless_equals_int64 (tracking_last_seen, 10);
  fail_unless (tracking_id != tracking_id1);
  fail_unless (tracking_lost == FALSE);

  while (gst_analytics_relation_meta_iterate (od_mtd.meta,
          &state, gst_analytics_tracking_mtd_get_mtd_type (), &t_mtd2)) {
    if (t_mtd.id == t_mtd2.id) {
      fail_unless (has_1 == FALSE);
      has_1 = TRUE;
      continue;
    }

    fail_unless (has_2 == FALSE);
    has_2 = TRUE;

    fail_unless (gst_analytics_tracking_mtd_get_info (&t_mtd2,
            &tracking_id, &tracking_first_seen, &tracking_last_seen,
            &tracking_lost));
    fail_unless_equals_int64 (tracking_first_seen, 0);
    fail_unless_equals_int64 (tracking_last_seen, 0);
    fail_unless_equals_int64 (tracking_id, tracking_id1);
    fail_unless (tracking_lost == FALSE);
  }
  fail_unless (has_1);
  fail_unless (has_2);

  gst_buffer_unref (b);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_intersection)
{
  GstHarness *h = setup_ioutracker ();
  GstBuffer *b;
  GstAnalyticsODMtd od_mtd;
  GstAnalyticsTrackingMtd t_mtd;
  guint64 tracking_id;
  guint64 tracking_id1;
  GstClockTime tracking_first_seen, tracking_last_seen;
  gboolean tracking_lost;

  g_object_set (h->element, "iou-score-threshold", 0.4, NULL);

  b = gst_harness_push_and_pull (h, create_buffer (0, 0, 0, 10, 10, &od_mtd));
  fail_unless (b);

  fail_unless_equals_int (gst_analytics_relation_get_length (od_mtd.meta), 2);

  fail_unless (gst_analytics_relation_meta_get_direct_related (od_mtd.meta,
          od_mtd.id, GST_ANALYTICS_REL_TYPE_RELATE_TO,
          gst_analytics_tracking_mtd_get_mtd_type (), NULL, &t_mtd));

  fail_unless (gst_analytics_tracking_mtd_get_info (&t_mtd,
          &tracking_id1, &tracking_first_seen, &tracking_last_seen,
          &tracking_lost));
  fail_unless_equals_int64 (tracking_first_seen, 0);
  fail_unless_equals_int64 (tracking_last_seen, 0);
  fail_unless (tracking_lost == FALSE);

  gst_buffer_unref (b);

  /* Now send a second buffer with large intersection */
  b = gst_harness_push_and_pull (h, create_buffer (10, 0, 4, 10, 10, &od_mtd));
  fail_unless (b);

  /* Now 1 object and 1 track */
  fail_unless_equals_int (gst_analytics_relation_get_length (od_mtd.meta), 2);

  fail_unless (gst_analytics_relation_meta_get_direct_related (od_mtd.meta,
          od_mtd.id, GST_ANALYTICS_REL_TYPE_RELATE_TO,
          gst_analytics_tracking_mtd_get_mtd_type (), NULL, &t_mtd));

  fail_unless (gst_analytics_tracking_mtd_get_info (&t_mtd,
          &tracking_id, &tracking_first_seen, &tracking_last_seen,
          &tracking_lost));
  fail_unless_equals_int64 (tracking_first_seen, 0);
  fail_unless_equals_int64 (tracking_last_seen, 10);
  fail_unless_equals_int64 (tracking_id, tracking_id1);
  fail_unless (tracking_lost == FALSE);

  gst_buffer_unref (b);

  /* Now send a third buffer with large intersection */
  b = gst_harness_push_and_pull (h, create_buffer (20, 0, 8, 10, 10, &od_mtd));
  fail_unless (b);

  /* Now 1 object and 1 track */
  fail_unless_equals_int (gst_analytics_relation_get_length (od_mtd.meta), 2);

  fail_unless (gst_analytics_relation_meta_get_direct_related (od_mtd.meta,
          od_mtd.id, GST_ANALYTICS_REL_TYPE_RELATE_TO,
          gst_analytics_tracking_mtd_get_mtd_type (), NULL, &t_mtd));

  fail_unless (gst_analytics_tracking_mtd_get_info (&t_mtd,
          &tracking_id, &tracking_first_seen, &tracking_last_seen,
          &tracking_lost));
  fail_unless_equals_int64 (tracking_first_seen, 0);
  fail_unless_equals_int64 (tracking_last_seen, 20);
  fail_unless_equals_int64 (tracking_id, tracking_id1);
  fail_unless (tracking_lost == FALSE);

  gst_buffer_unref (b);

  /* Now send a for buffer with large intersection with the 3rd,
   * but none with the original one.
   */
  b = gst_harness_push_and_pull (h, create_buffer (30, 0, 12, 10, 10, &od_mtd));
  fail_unless (b);

  /* Now 1 object and 1 track */
  fail_unless_equals_int (gst_analytics_relation_get_length (od_mtd.meta), 2);

  fail_unless (gst_analytics_relation_meta_get_direct_related (od_mtd.meta,
          od_mtd.id, GST_ANALYTICS_REL_TYPE_RELATE_TO,
          gst_analytics_tracking_mtd_get_mtd_type (), NULL, &t_mtd));

  fail_unless (gst_analytics_tracking_mtd_get_info (&t_mtd,
          &tracking_id, &tracking_first_seen, &tracking_last_seen,
          &tracking_lost));
  fail_unless_equals_int64 (tracking_first_seen, 0);
  fail_unless_equals_int64 (tracking_last_seen, 30);
  fail_unless_equals_int64 (tracking_id, tracking_id1);
  fail_unless (tracking_lost == FALSE);

  gst_buffer_unref (b);


  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_lost)
{
  GstHarness *h = setup_ioutracker ();
  GstBuffer *b;
  GstAnalyticsRelationMeta *rmeta;
  GstAnalyticsODMtd od_mtd;
  GstAnalyticsTrackingMtd t_mtd;
  guint64 tracking_id;
  guint64 tracking_id1;
  GstClockTime tracking_first_seen, tracking_last_seen;
  gboolean tracking_lost;
  gpointer state = NULL;

  g_object_set (h->element, "min-frame-count-for-lost-track", 2, NULL);

  b = gst_harness_push_and_pull (h, create_buffer (0, 0, 0, 10, 10, &od_mtd));
  fail_unless (b);
  fail_unless_equals_int (gst_analytics_relation_get_length (od_mtd.meta), 2);

  fail_unless (gst_analytics_relation_meta_get_direct_related (od_mtd.meta,
          od_mtd.id, GST_ANALYTICS_REL_TYPE_RELATE_TO,
          gst_analytics_tracking_mtd_get_mtd_type (), NULL, &t_mtd));

  fail_unless (gst_analytics_tracking_mtd_get_info (&t_mtd,
          &tracking_id, &tracking_first_seen, &tracking_last_seen,
          &tracking_lost));
  fail_unless_equals_int64 (tracking_first_seen, 0);
  fail_unless_equals_int64 (tracking_last_seen, 0);
  fail_unless (tracking_lost == FALSE);
  tracking_id1 = tracking_id;
  gst_buffer_unref (b);

  /* Now send a second buffer with no meta */
  b = gst_buffer_new ();
  GST_BUFFER_PTS (b) = GST_BUFFER_DTS (b) = 10;
  b = gst_harness_push_and_pull (h, b);
  fail_unless (b);
  rmeta = gst_buffer_get_analytics_relation_meta (b);
  fail_unless (rmeta);

  /* Now one object and 2 tracks */
  fail_unless_equals_int (gst_analytics_relation_get_length (rmeta), 1);

  fail_unless (gst_analytics_relation_meta_iterate (rmeta, &state,
          gst_analytics_tracking_mtd_get_mtd_type (), &t_mtd));

  fail_unless (gst_analytics_tracking_mtd_get_info (&t_mtd,
          &tracking_id, &tracking_first_seen, &tracking_last_seen,
          &tracking_lost));
  fail_unless_equals_int64 (tracking_first_seen, 0);
  fail_unless_equals_int64 (tracking_last_seen, 0);
  fail_unless_equals_int64 (tracking_id, tracking_id1);
  fail_unless (tracking_lost == FALSE);
  gst_buffer_unref (b);

  /* Now send a third buffer with no meta */
  b = gst_buffer_new ();
  GST_BUFFER_PTS (b) = GST_BUFFER_DTS (b) = 20;
  b = gst_harness_push_and_pull (h, b);
  fail_unless (b);
  rmeta = gst_buffer_get_analytics_relation_meta (b);
  fail_unless (rmeta);

  /* Now one object and 2 tracks */
  fail_unless_equals_int (gst_analytics_relation_get_length (rmeta), 1);

  state = NULL;
  fail_unless (gst_analytics_relation_meta_iterate (rmeta, &state,
          gst_analytics_tracking_mtd_get_mtd_type (), &t_mtd));

  fail_unless (gst_analytics_tracking_mtd_get_info (&t_mtd,
          &tracking_id, &tracking_first_seen, &tracking_last_seen,
          &tracking_lost));
  fail_unless_equals_int64 (tracking_first_seen, 0);
  fail_unless_equals_int64 (tracking_last_seen, 0);
  fail_unless_equals_int64 (tracking_id, tracking_id1);
  /* The track is lost */
  fail_unless (tracking_lost == TRUE);
  gst_buffer_unref (b);

  /* Now send a fourth buffer with no meta */
  b = gst_buffer_new ();
  GST_BUFFER_PTS (b) = GST_BUFFER_DTS (b) = 30;
  b = gst_harness_push_and_pull (h, b);
  fail_unless (b);
  rmeta = gst_buffer_get_analytics_relation_meta (b);
  fail_unless (rmeta == NULL);
  gst_buffer_unref (b);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_catch_up)
{
  GstHarness *h = setup_ioutracker ();
  GstBuffer *b;
  GstAnalyticsODMtd od_mtd;
  GstAnalyticsTrackingMtd t_mtd;
  guint64 tracking_id;
  guint64 tracking_id1;
  GstClockTime tracking_first_seen, tracking_last_seen;
  gboolean tracking_lost;
  GstAnalyticsRelationMeta *rmeta;
  gpointer state = NULL;
  GstClockTime ts;

  g_object_set (h->element, "iou-score-threshold", 0.2,
      "min-frame-count-for-lost-track", 10, NULL);
  /* Send a firs buffer */

  b = gst_harness_push_and_pull (h, create_buffer (0, 0, 0, 10, 10, &od_mtd));
  fail_unless (b);

  fail_unless_equals_int (gst_analytics_relation_get_length (od_mtd.meta), 2);

  fail_unless (gst_analytics_relation_meta_get_direct_related (od_mtd.meta,
          od_mtd.id, GST_ANALYTICS_REL_TYPE_RELATE_TO,
          gst_analytics_tracking_mtd_get_mtd_type (), NULL, &t_mtd));

  fail_unless (gst_analytics_tracking_mtd_get_info (&t_mtd,
          &tracking_id1, &tracking_first_seen, &tracking_last_seen,
          &tracking_lost));
  fail_unless_equals_int64 (tracking_first_seen, 0);
  fail_unless_equals_int64 (tracking_last_seen, 0);
  fail_unless (tracking_lost == FALSE);

  gst_buffer_unref (b);

  /* Now send a second buffer with an intersection */
  b = gst_harness_push_and_pull (h, create_buffer (10, 0, 6, 10, 10, &od_mtd));
  fail_unless (b);

  /* Now 1 object and 1 track */
  fail_unless_equals_int (gst_analytics_relation_get_length (od_mtd.meta), 2);

  fail_unless (gst_analytics_relation_meta_get_direct_related (od_mtd.meta,
          od_mtd.id, GST_ANALYTICS_REL_TYPE_RELATE_TO,
          gst_analytics_tracking_mtd_get_mtd_type (), NULL, &t_mtd));

  fail_unless (gst_analytics_tracking_mtd_get_info (&t_mtd,
          &tracking_id, &tracking_first_seen, &tracking_last_seen,
          &tracking_lost));
  fail_unless_equals_int64 (tracking_first_seen, 0);
  fail_unless_equals_int64 (tracking_last_seen, 10);
  fail_unless_equals_int64 (tracking_id, tracking_id1);
  fail_unless (tracking_lost == FALSE);

  gst_buffer_unref (b);

  /* Now send a few buffers with no meta */
  for (ts = 20; ts < 50; ts += 10) {

    b = gst_buffer_new ();
    GST_BUFFER_PTS (b) = GST_BUFFER_DTS (b) = ts;
    b = gst_harness_push_and_pull (h, b);
    fail_unless (b);
    rmeta = gst_buffer_get_analytics_relation_meta (b);
    fail_unless (rmeta);

    /* Now has 1 track */
    fail_unless_equals_int (gst_analytics_relation_get_length (rmeta), 1);

    state = NULL;
    fail_unless (gst_analytics_relation_meta_iterate (rmeta, &state,
            gst_analytics_tracking_mtd_get_mtd_type (), &t_mtd));

    fail_unless (gst_analytics_tracking_mtd_get_info (&t_mtd,
            &tracking_id, &tracking_first_seen, &tracking_last_seen,
            &tracking_lost));
    fail_unless_equals_int64 (tracking_first_seen, 0);
    fail_unless_equals_int64 (tracking_last_seen, 10);
    fail_unless_equals_int64 (tracking_id, tracking_id1);
    fail_unless (tracking_lost == FALSE);
    gst_buffer_unref (b);
  }

  /* Now send a sixth buffer with no intersection,
   * but we expect the prediction to catch up
   */
  b = gst_harness_push_and_pull (h, create_buffer (50, 0, 16, 10, 10, &od_mtd));
  fail_unless (b);

  /* Now 1 object and 1 track */
  fail_unless_equals_int (gst_analytics_relation_get_length (od_mtd.meta), 2);

  fail_unless (gst_analytics_relation_meta_get_direct_related (od_mtd.meta,
          od_mtd.id, GST_ANALYTICS_REL_TYPE_RELATE_TO,
          gst_analytics_tracking_mtd_get_mtd_type (), NULL, &t_mtd));

  fail_unless (gst_analytics_tracking_mtd_get_info (&t_mtd,
          &tracking_id, &tracking_first_seen, &tracking_last_seen,
          &tracking_lost));
  fail_unless_equals_int64 (tracking_first_seen, 0);
  fail_unless_equals_int64 (tracking_last_seen, 50);
  fail_unless_equals_int64 (tracking_id, tracking_id1);
  fail_unless (tracking_lost == FALSE);

  gst_buffer_unref (b);


  gst_harness_teardown (h);
}

GST_END_TEST;


static Suite *
ioutracker_suite (void)
{
  Suite *s;
  TCase *tc_chain;

  s = suite_create ("ioutracker");
  tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_no_intersection);
  tcase_add_test (tc_chain, test_intersection);
  tcase_add_test (tc_chain, test_lost);
  tcase_add_test (tc_chain, test_catch_up);

  return s;
}

GST_CHECK_MAIN (ioutracker);
