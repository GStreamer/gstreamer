/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <bilboed@bilboed.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <ges/ges.h>
#include <gst/check/gstcheck.h>

static gboolean
my_fill_track_func (GESTimelineObject * object,
    GESTrackObject * trobject, GstElement * gnlobj, gpointer user_data)
{
  GstElement *src;

  GST_DEBUG ("timelineobj:%p, trackobjec:%p, gnlobj:%p",
      object, trobject, gnlobj);

  /* Let's just put a fakesource in for the time being */
  src = gst_element_factory_make ("fakesrc", NULL);
  return gst_bin_add (GST_BIN (gnlobj), src);
}

GST_START_TEST (test_gsl_add)
{
  GESTimeline *timeline;
  GESTimelineLayer *layer;
  GESTrack *track;
  GESCustomTimelineSource *source;

  ges_init ();
  /* This is the simplest scenario ever */

  /* Timeline and 1 Layer */
  timeline = ges_timeline_new ();
  layer = (GESTimelineLayer *) ges_simple_timeline_layer_new ();
  fail_unless (ges_timeline_add_layer (timeline, layer));
  track = ges_track_new (GES_TRACK_TYPE_CUSTOM, GST_CAPS_ANY);
  fail_unless (ges_timeline_add_track (timeline, track));

  source = ges_custom_timeline_source_new (my_fill_track_func, NULL);
  fail_unless (source != NULL);
  g_object_set (source, "duration", GST_SECOND, "start", (guint64) 42, NULL);
  fail_unless_equals_uint64 (GES_TIMELINE_OBJECT_DURATION (source), GST_SECOND);
  fail_unless_equals_uint64 (GES_TIMELINE_OBJECT_START (source), 42);

  fail_unless (ges_simple_timeline_layer_add_object (GES_SIMPLE_TIMELINE_LAYER
          (layer), GES_TIMELINE_OBJECT (source), -1));
  fail_unless (GES_TIMELINE_OBJECT (source)->layer == layer);
  fail_unless_equals_uint64 (GES_TIMELINE_OBJECT_DURATION (source), GST_SECOND);
  fail_unless_equals_uint64 (GES_TIMELINE_OBJECT_START (source), 0);

  fail_unless (ges_timeline_layer_remove_object (layer,
          GES_TIMELINE_OBJECT (source)));
  fail_unless (ges_timeline_remove_track (timeline, track));
  fail_unless (ges_timeline_remove_layer (timeline, layer));
  g_object_unref (timeline);
}

GST_END_TEST;

GST_START_TEST (test_gsl_move_simple)
{
  GESTimeline *timeline;
  GESTimelineLayer *layer;
  GESTrack *track;
  GESCustomTimelineSource *source1, *source2;

  ges_init ();

  /* Timeline and 1 Layer */
  timeline = ges_timeline_new ();
  layer = (GESTimelineLayer *) ges_simple_timeline_layer_new ();
  fail_unless (ges_timeline_add_layer (timeline, layer));
  track = ges_track_new (GES_TRACK_TYPE_CUSTOM, GST_CAPS_ANY);
  fail_unless (ges_timeline_add_track (timeline, track));

  /* Create two 1s sources */
  source1 = ges_custom_timeline_source_new (my_fill_track_func, NULL);
  g_object_set (source1, "duration", GST_SECOND, "start", (guint64) 42, NULL);
  fail_unless_equals_uint64 (GES_TIMELINE_OBJECT_DURATION (source1),
      GST_SECOND);
  source2 = ges_custom_timeline_source_new (my_fill_track_func, NULL);
  g_object_set (source2, "duration", GST_SECOND, "start", (guint64) 42, NULL);
  fail_unless_equals_uint64 (GES_TIMELINE_OBJECT_DURATION (source2),
      GST_SECOND);

  /* Add source to any position */
  GST_DEBUG ("Adding the source to the timeline layer");
  fail_unless (ges_simple_timeline_layer_add_object (GES_SIMPLE_TIMELINE_LAYER
          (layer), GES_TIMELINE_OBJECT (source1), -1));
  fail_unless_equals_uint64 (GES_TIMELINE_OBJECT_START (source1), 0);

  /* Add source2 to the end */
  GST_DEBUG ("Adding the source to the timeline layer");
  fail_unless (ges_simple_timeline_layer_add_object (GES_SIMPLE_TIMELINE_LAYER
          (layer), GES_TIMELINE_OBJECT (source2), -1));
  fail_unless_equals_uint64 (GES_TIMELINE_OBJECT_START (source1), 0);
  fail_unless_equals_uint64 (GES_TIMELINE_OBJECT_START (source2), GST_SECOND);

  /* Move source2 before source 1 (newpos:0) */
  fail_unless (ges_simple_timeline_layer_move_object (GES_SIMPLE_TIMELINE_LAYER
          (layer), GES_TIMELINE_OBJECT (source2), 0));
  fail_unless_equals_uint64 (GES_TIMELINE_OBJECT_START (source1), GST_SECOND);
  fail_unless_equals_uint64 (GES_TIMELINE_OBJECT_START (source2), 0);

  /* Move source2 to end (newpos:-1) */
  fail_unless (ges_simple_timeline_layer_move_object (GES_SIMPLE_TIMELINE_LAYER
          (layer), GES_TIMELINE_OBJECT (source2), -1));
  fail_unless_equals_uint64 (GES_TIMELINE_OBJECT_START (source1), 0);
  fail_unless_equals_uint64 (GES_TIMELINE_OBJECT_START (source2), GST_SECOND);

  /* remove source1, source2 should be moved to the beginning */
  g_object_ref (source1);
  fail_unless (ges_timeline_layer_remove_object (layer,
          GES_TIMELINE_OBJECT (source1)));
  fail_unless_equals_uint64 (GES_TIMELINE_OBJECT_START (source2), 0);

  g_object_set (source1, "start", (guint64) 42, NULL);

  /* re-add source1... using the normal API, it should be added to the end */
  fail_unless (ges_timeline_layer_add_object (layer,
          GES_TIMELINE_OBJECT (source1)));
  fail_unless_equals_uint64 (GES_TIMELINE_OBJECT_START (source2), 0);
  fail_unless_equals_uint64 (GES_TIMELINE_OBJECT_START (source1), GST_SECOND);

  /* remove source1 ... */
  fail_unless (ges_timeline_layer_remove_object (layer,
          GES_TIMELINE_OBJECT (source1)));
  fail_unless_equals_uint64 (GES_TIMELINE_OBJECT_START (source2), 0);
  /* ... and source2 */
  fail_unless (ges_timeline_layer_remove_object (layer,
          GES_TIMELINE_OBJECT (source2)));

  fail_unless (ges_timeline_remove_track (timeline, track));
  fail_unless (ges_timeline_remove_layer (timeline, layer));
  g_object_unref (timeline);
}

GST_END_TEST;

static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges");
  TCase *tc_chain = tcase_create ("basic");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_gsl_add);
  tcase_add_test (tc_chain, test_gsl_move_simple);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = ges_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
