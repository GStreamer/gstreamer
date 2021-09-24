/* GStreamer Editing Services
 *
 * Copyright (C) <2019> Mathieu Duponchelle <mathieu@centricular.com>
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

#include "test-utils.h"
#include <ges/ges.h>
#include <gst/check/gstcheck.h>

GST_START_TEST (test_add)
{
  GESMarkerList *markerlist;
  GESMarker *marker;
  ges_init ();

  markerlist = ges_marker_list_new ();
  marker = ges_marker_list_add (markerlist, 42);

  ASSERT_OBJECT_REFCOUNT (marker, "marker list", 1);

  g_object_ref (marker);

  ASSERT_OBJECT_REFCOUNT (marker, "marker list + local ref", 2);

  g_object_unref (markerlist);

  ASSERT_OBJECT_REFCOUNT (marker, "local ref", 1);

  g_object_unref (marker);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_remove)
{
  GESMarkerList *markerlist;
  GESMarker *marker;
  ges_init ();

  markerlist = ges_marker_list_new ();
  marker = ges_marker_list_add (markerlist, 42);

  g_object_ref (marker);

  fail_unless_equals_int (ges_marker_list_size (markerlist), 1);

  fail_unless (ges_marker_list_remove (markerlist, marker));
  fail_unless_equals_int (ges_marker_list_size (markerlist), 0);

  ASSERT_OBJECT_REFCOUNT (marker, "local ref", 1);

  fail_if (ges_marker_list_remove (markerlist, marker));

  g_object_unref (marker);

  g_object_unref (markerlist);

  ges_deinit ();
}

GST_END_TEST;


static void
marker_added_cb (GESMarkerList * mlist, GstClockTime position,
    GESMarker * marker, gboolean * called)
{
  fail_unless_equals_int (position, 42);

  ASSERT_OBJECT_REFCOUNT (marker, "local ref + signal", 2);
  *called = TRUE;
}

GST_START_TEST (test_signal_marker_added)
{
  GESMarkerList *mlist;
  GESMarker *marker;
  gboolean called = FALSE;

  ges_init ();

  mlist = ges_marker_list_new ();
  g_signal_connect (mlist, "marker-added", G_CALLBACK (marker_added_cb),
      &called);
  marker = ges_marker_list_add (mlist, 42);
  ASSERT_OBJECT_REFCOUNT (marker, "local ref", 1);
  fail_unless (called == TRUE);
  g_signal_handlers_disconnect_by_func (mlist, marker_added_cb, &called);

  g_object_unref (mlist);

  ges_deinit ();
}

GST_END_TEST;


static void
marker_removed_cb (GESMarkerList * mlist, GESMarker * marker, gboolean * called)
{
  ASSERT_OBJECT_REFCOUNT (marker, "local ref + signal", 2);
  *called = TRUE;
}

GST_START_TEST (test_signal_marker_removed)
{
  GESMarkerList *mlist;
  GESMarker *marker;
  gboolean called = FALSE;

  ges_init ();

  mlist = ges_marker_list_new ();
  marker = ges_marker_list_add (mlist, 42);

  ASSERT_OBJECT_REFCOUNT (marker, "local ref", 1);

  g_signal_connect (mlist, "marker-removed", G_CALLBACK (marker_removed_cb),
      &called);

  fail_unless (ges_marker_list_remove (mlist, marker));

  fail_unless (called == TRUE);

  g_signal_handlers_disconnect_by_func (mlist, marker_removed_cb, &called);

  g_object_unref (mlist);

  ges_deinit ();
}

GST_END_TEST;


static void
marker_moved_cb (GESMarkerList * mlist, GstClockTime prev_position,
    GstClockTime position, GESMarker * marker, gboolean * called)
{
  fail_unless_equals_int (prev_position, 10);
  fail_unless_equals_int (position, 42);

  ASSERT_OBJECT_REFCOUNT (marker, "local ref + signal", 2);
  *called = TRUE;
}

GST_START_TEST (test_signal_marker_moved)
{
  GESMarkerList *mlist;
  GESMarker *marker;
  gboolean called = FALSE;

  ges_init ();

  mlist = ges_marker_list_new ();
  g_signal_connect (mlist, "marker-moved", G_CALLBACK (marker_moved_cb),
      &called);

  marker = ges_marker_list_add (mlist, 10);
  ASSERT_OBJECT_REFCOUNT (marker, "local ref", 1);

  fail_unless (ges_marker_list_move (mlist, marker, 42));

  fail_unless (called == TRUE);

  g_signal_handlers_disconnect_by_func (mlist, marker_moved_cb, &called);

  g_object_unref (mlist);

  ges_deinit ();
}

GST_END_TEST;


GST_START_TEST (test_get_markers)
{
  GESMarkerList *markerlist;
  GESMarker *marker1, *marker2, *marker3, *marker4;
  GList *markers;

  ges_init ();

  markerlist = ges_marker_list_new ();
  marker1 = ges_marker_list_add (markerlist, 0);
  marker2 = ges_marker_list_add (markerlist, 10);
  marker3 = ges_marker_list_add (markerlist, 20);
  marker4 = ges_marker_list_add (markerlist, 30);

  markers = ges_marker_list_get_markers (markerlist);

  ASSERT_OBJECT_REFCOUNT (marker1, "local ref + markers", 2);
  ASSERT_OBJECT_REFCOUNT (marker2, "local ref + markers", 2);
  ASSERT_OBJECT_REFCOUNT (marker3, "local ref + markers", 2);
  ASSERT_OBJECT_REFCOUNT (marker4, "local ref + markers", 2);

  fail_unless (g_list_index (markers, marker1) == 0);
  fail_unless (g_list_index (markers, marker2) == 1);
  fail_unless (g_list_index (markers, marker3) == 2);
  fail_unless (g_list_index (markers, marker4) == 3);

  g_list_foreach (markers, (GFunc) gst_object_unref, NULL);
  g_list_free (markers);

  g_object_unref (markerlist);

  ges_deinit ();
}

GST_END_TEST;


GST_START_TEST (test_move_marker)
{
  GESMarkerList *markerlist;
  GESMarker *marker_a, *marker_b;
  GstClockTime position;
  GList *range;

  ges_init ();

  markerlist = ges_marker_list_new ();

  marker_a = ges_marker_list_add (markerlist, 10);
  marker_b = ges_marker_list_add (markerlist, 30);

  fail_unless (ges_marker_list_move (markerlist, marker_a, 20));

  g_object_get (marker_a, "position", &position, NULL);
  fail_unless_equals_int (position, 20);

  range = ges_marker_list_get_markers (markerlist);

  fail_unless (g_list_index (range, marker_a) == 0);
  fail_unless (g_list_index (range, marker_b) == 1);

  g_list_foreach (range, (GFunc) gst_object_unref, NULL);
  g_list_free (range);

  fail_unless (ges_marker_list_move (markerlist, marker_a, 35));

  range = ges_marker_list_get_markers (markerlist);

  fail_unless (g_list_index (range, marker_b) == 0);
  fail_unless (g_list_index (range, marker_a) == 1);

  g_list_foreach (range, (GFunc) gst_object_unref, NULL);
  g_list_free (range);

  fail_unless (ges_marker_list_move (markerlist, marker_a, 30));

  g_object_get (marker_a, "position", &position, NULL);
  fail_unless_equals_int (position, 30);

  g_object_get (marker_b, "position", &position, NULL);
  fail_unless_equals_int (position, 30);

  fail_unless_equals_int (ges_marker_list_size (markerlist), 2);

  ges_marker_list_remove (markerlist, marker_a);

  fail_unless (!ges_marker_list_move (markerlist, marker_a, 20));

  g_object_unref (markerlist);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_serialize_deserialize_in_timeline)
{
  GESMarkerList *markerlist1, *markerlist2;
  gchar *metas1, *metas2;
  GESTimeline *timeline1, *timeline2;

  ges_init ();

  timeline1 = ges_timeline_new_audio_video ();

  markerlist1 = ges_marker_list_new ();
  ges_marker_list_add (markerlist1, 0);
  ges_marker_list_add (markerlist1, 10);

  ASSERT_OBJECT_REFCOUNT (markerlist1, "local ref", 1);

  fail_unless (ges_meta_container_set_marker_list (GES_META_CONTAINER
          (timeline1), "ges-test", markerlist1));

  ASSERT_OBJECT_REFCOUNT (markerlist1, "GstStructure + local ref", 2);

  markerlist2 =
      ges_meta_container_get_marker_list (GES_META_CONTAINER (timeline1),
      "ges-test");

  fail_unless (markerlist1 == markerlist2);

  ASSERT_OBJECT_REFCOUNT (markerlist1, "GstStructure + getter + local ref", 3);

  g_object_unref (markerlist2);

  ASSERT_OBJECT_REFCOUNT (markerlist1, "GstStructure + local ref", 2);

  timeline2 = ges_timeline_new_audio_video ();

  metas1 = ges_meta_container_metas_to_string (GES_META_CONTAINER (timeline1));
  ges_meta_container_add_metas_from_string (GES_META_CONTAINER (timeline2),
      metas1);
  metas2 = ges_meta_container_metas_to_string (GES_META_CONTAINER (timeline2));

  fail_unless_equals_string (metas1, metas2);

  g_free (metas1);
  g_free (metas2);

  fail_unless (ges_meta_container_set_marker_list (GES_META_CONTAINER
          (timeline1), "ges-test", NULL));

  ASSERT_OBJECT_REFCOUNT (markerlist1, "local ref", 1);

  g_object_unref (markerlist1);
  g_object_unref (timeline1);
  g_object_unref (timeline2);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_serialize_deserialize_in_value)
{
  GESMarkerList *markerlist1, *markerlist2;
  GESMarker *marker;
  gchar *serialized, *cmp;
  const gchar *str_val;
  guint uint_val;
  const gchar *test_string = "test \" string";
  GList *markers;
  guint64 position;
  GValue val1 = G_VALUE_INIT, val2 = G_VALUE_INIT;
  GESMarkerFlags flags;

  ges_init ();

  g_value_init (&val1, GES_TYPE_MARKER_LIST);
  g_value_init (&val2, GES_TYPE_MARKER_LIST);

  markerlist1 = ges_marker_list_new ();
  g_object_set (markerlist1, "flags", GES_MARKER_FLAG_SNAPPABLE, NULL);
  marker = ges_marker_list_add (markerlist1, 0);
  fail_unless (ges_meta_container_set_string (GES_META_CONTAINER (marker),
          "str-val", test_string));
  marker = ges_marker_list_add (markerlist1, 10);
  fail_unless (ges_meta_container_set_string (GES_META_CONTAINER (marker),
          "first", test_string));
  fail_unless (ges_meta_container_set_uint (GES_META_CONTAINER (marker),
          "second", 43));

  ASSERT_OBJECT_REFCOUNT (markerlist1, "local ref", 1);

  g_value_set_instance (&val1, markerlist1);
  ASSERT_OBJECT_REFCOUNT (markerlist1, "GValue + local ref", 2);

  serialized = gst_value_serialize (&val1);
  fail_unless (serialized != NULL);
  GST_DEBUG ("serialized to %s", serialized);
  fail_unless (gst_value_deserialize (&val2, serialized));
  cmp = gst_value_serialize (&val2);
  fail_unless_equals_string (cmp, serialized);

  markerlist2 = GES_MARKER_LIST (g_value_get_object (&val2));
  ASSERT_OBJECT_REFCOUNT (markerlist2, "GValue", 1);

  g_object_get (markerlist2, "flags", &flags, NULL);
  fail_unless (flags == GES_MARKER_FLAG_SNAPPABLE);

  fail_unless_equals_int (ges_marker_list_size (markerlist2), 2);
  markers = ges_marker_list_get_markers (markerlist2);
  marker = GES_MARKER (markers->data);
  fail_unless (marker != NULL);

  g_object_get (marker, "position", &position, NULL);
  fail_unless_equals_uint64 (position, 0);
  str_val =
      ges_meta_container_get_string (GES_META_CONTAINER (marker), "str-val");
  fail_unless_equals_string (str_val, test_string);

  marker = GES_MARKER (markers->next->data);
  fail_unless (marker != NULL);
  fail_unless (markers->next->next == NULL);

  g_object_get (marker, "position", &position, NULL);
  fail_unless_equals_uint64 (position, 10);
  str_val =
      ges_meta_container_get_string (GES_META_CONTAINER (marker), "first");
  fail_unless_equals_string (str_val, test_string);
  fail_unless (ges_meta_container_get_uint (GES_META_CONTAINER (marker),
          "second", &uint_val));
  fail_unless_equals_int (uint_val, 43);

  g_list_free_full (markers, g_object_unref);
  g_value_unset (&val1);
  g_value_unset (&val2);
  ASSERT_OBJECT_REFCOUNT (markerlist1, "local ref", 1);
  g_object_unref (markerlist1);
  g_free (serialized);
  g_free (cmp);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_marker_color)
{
  GESMarkerList *mlist;
  GESMarker *marker;
  const guint yellow_rgb = 16776960;
  guint color;

  ges_init ();

  mlist = ges_marker_list_new ();
  marker = ges_marker_list_add (mlist, 0);
  /* getting the color should fail since no value should be set yet */
  fail_unless (ges_meta_container_get_meta (GES_META_CONTAINER (marker),
          GES_META_MARKER_COLOR) == NULL);
  /* trying to set the color field to something other than a uint should
   * fail */
  fail_unless (ges_meta_container_set_float (GES_META_CONTAINER (marker),
          GES_META_MARKER_COLOR, 0.0) == FALSE);
  fail_unless (ges_meta_container_set_uint (GES_META_CONTAINER (marker),
          GES_META_MARKER_COLOR, yellow_rgb));
  fail_unless (ges_meta_container_get_uint (GES_META_CONTAINER (marker),
          GES_META_MARKER_COLOR, &color));
  fail_unless_equals_int (color, yellow_rgb);

  g_object_unref (mlist);

  ges_deinit ();
}

GST_END_TEST;

static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges-marker-list");
  TCase *tc_chain = tcase_create ("markerlist");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_add);
  tcase_add_test (tc_chain, test_remove);
  tcase_add_test (tc_chain, test_signal_marker_added);
  tcase_add_test (tc_chain, test_signal_marker_removed);
  tcase_add_test (tc_chain, test_signal_marker_moved);
  tcase_add_test (tc_chain, test_get_markers);
  tcase_add_test (tc_chain, test_move_marker);
  tcase_add_test (tc_chain, test_serialize_deserialize_in_timeline);
  tcase_add_test (tc_chain, test_serialize_deserialize_in_value);
  tcase_add_test (tc_chain, test_marker_color);

  return s;
}

GST_CHECK_MAIN (ges);
