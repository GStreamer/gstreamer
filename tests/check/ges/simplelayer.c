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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "test-utils.h"
#include <ges/ges.h>
#include <gst/check/gstcheck.h>

static gboolean
my_fill_track_func (GESClip * object,
    GESTrackObject * trobject, GstElement * gnlobj, gpointer user_data)
{
  GstElement *src;

  GST_DEBUG ("timelineobj:%p, trackobjec:%p, gnlobj:%p",
      object, trobject, gnlobj);

  /* Let's just put a fakesource in for the time being */
  src = gst_element_factory_make ("fakesrc", NULL);

  /* If this fails... that means that there already was something
   * in it */
  fail_unless (gst_bin_add (GST_BIN (gnlobj), src));

  return TRUE;
}

static gboolean
arbitrary_fill_track_func (GESClip * object,
    GESTrackObject * trobject, GstElement * gnlobj, gpointer user_data)
{
  GstElement *src;

  g_assert (user_data);

  GST_DEBUG ("element:%s, timelineobj:%p, trackobjects:%p, gnlobj:%p,",
      (const gchar *) user_data, object, trobject, gnlobj);

  /* interpret user_data as name of element to create */
  src = gst_element_factory_make (user_data, NULL);

  /* If this fails... that means that there already was something
   * in it */
  fail_unless (gst_bin_add (GST_BIN (gnlobj), src));

  return TRUE;
}

GST_START_TEST (test_gsl_add)
{
  GESTimeline *timeline;
  GESTimelineLayer *layer;
  GESTrack *track;
  GESCustomSourceClip *source;
  GESClip *source2;
  gint result;

  ges_init ();
  /* This is the simplest scenario ever */

  /* Timeline and 1 Layer */
  timeline = ges_timeline_new ();
  layer = (GESTimelineLayer *) ges_simple_timeline_layer_new ();
  fail_unless (ges_timeline_add_layer (timeline, layer));
  track = ges_track_new (GES_TRACK_TYPE_CUSTOM, gst_caps_ref (GST_CAPS_ANY));
  fail_unless (ges_timeline_add_track (timeline, track));

  source = ges_custom_source_clip_new (my_fill_track_func, NULL);
  fail_unless (source != NULL);
  g_object_set (source, "duration", GST_SECOND, "start", (guint64) 42, NULL);
  fail_unless_equals_uint64 (_DURATION (source), GST_SECOND);
  fail_unless_equals_uint64 (_START (source), 42);

  fail_unless (ges_simple_timeline_layer_add_object (GES_SIMPLE_TIMELINE_LAYER
          (layer), GES_CLIP (source), -1));
  fail_unless (ges_clip_get_layer (GES_CLIP (source)) == layer);
  fail_unless_equals_uint64 (_DURATION (source), GST_SECOND);
  fail_unless_equals_uint64 (_START (source), 0);

  /* test nth */
  source2 =
      ges_simple_timeline_layer_nth ((GESSimpleTimelineLayer *) layer, -1);
  fail_if (source2);
  source2 = ges_simple_timeline_layer_nth ((GESSimpleTimelineLayer *) layer, 2);
  fail_if (source2);
  source2 = ges_simple_timeline_layer_nth ((GESSimpleTimelineLayer *) layer, 0);
  fail_unless ((GESClip *) source == source2);

  /* test position */

  result = ges_simple_timeline_layer_index ((GESSimpleTimelineLayer *) layer,
      source2);
  fail_unless_equals_int (result, 0);
  result = ges_simple_timeline_layer_index ((GESSimpleTimelineLayer *) layer,
      (GESClip *) NULL);
  fail_unless_equals_int (result, -1);

  fail_unless (ges_timeline_layer_remove_object (layer, GES_CLIP (source)));
  fail_unless (ges_timeline_remove_track (timeline, track));
  fail_unless (ges_timeline_remove_layer (timeline, layer));
  g_object_unref (timeline);
}

GST_END_TEST;

typedef struct
{
  gint new;
  gint old;
} siginfo;

static void
object_moved_cb (GESSimpleTimelineLayer * layer,
    GESClip * object, gint old, gint new, gpointer user)
{
  siginfo *info;
  info = (siginfo *) user;
  info->new = new;
  info->old = old;
}

GST_START_TEST (test_gsl_move_simple)
{
  GESTimeline *timeline;
  GESTimelineLayer *layer;
  GESTrack *track;
  GESCustomSourceClip *source1, *source2;
  siginfo info = { 0, 0 };

  ges_init ();

  /* Timeline and 1 Layer */
  timeline = ges_timeline_new ();
  layer = (GESTimelineLayer *) ges_simple_timeline_layer_new ();
  fail_unless (ges_timeline_add_layer (timeline, layer));
  track = ges_track_new (GES_TRACK_TYPE_CUSTOM, gst_caps_new_any ());
  fail_unless (ges_timeline_add_track (timeline, track));
  g_signal_connect (G_OBJECT (layer), "object-moved", G_CALLBACK
      (object_moved_cb), &info);

  /* Create two 1s sources */
  source1 = ges_custom_source_clip_new (my_fill_track_func, NULL);
  g_object_set (source1, "duration", GST_SECOND, "start", (guint64) 42, NULL);
  fail_unless_equals_uint64 (_DURATION (source1), GST_SECOND);
  source2 = ges_custom_source_clip_new (my_fill_track_func, NULL);
  g_object_set (source2, "duration", GST_SECOND, "start", (guint64) 42, NULL);
  fail_unless_equals_uint64 (_DURATION (source2), GST_SECOND);

  /* Add source to any position */
  GST_DEBUG ("Adding the source to the timeline layer");
  fail_unless (ges_simple_timeline_layer_add_object (GES_SIMPLE_TIMELINE_LAYER
          (layer), GES_CLIP (source1), -1));
  fail_unless_equals_uint64 (_START (source1), 0);

  /* Add source2 to the end */
  GST_DEBUG ("Adding the source to the timeline layer");
  fail_unless (ges_simple_timeline_layer_add_object (GES_SIMPLE_TIMELINE_LAYER
          (layer), GES_CLIP (source2), -1));
  fail_unless_equals_uint64 (_START (source1), 0);
  fail_unless_equals_uint64 (_START (source2), GST_SECOND);

  /* Move source2 before source 1 (newpos:0) */
  fail_unless (ges_simple_timeline_layer_move_object (GES_SIMPLE_TIMELINE_LAYER
          (layer), GES_CLIP (source2), 0));
  fail_unless_equals_uint64 (_START (source1), GST_SECOND);
  fail_unless_equals_uint64 (_START (source2), 0);
  fail_unless_equals_int (info.new, 0);
  fail_unless_equals_int (info.old, 1);

  /* Move source2 after source 1 (newpos:0) */
  fail_unless (ges_simple_timeline_layer_move_object (GES_SIMPLE_TIMELINE_LAYER
          (layer), GES_CLIP (source2), 1));
  fail_unless_equals_uint64 (_START (source1), 0);
  fail_unless_equals_uint64 (_START (source2), GST_SECOND);
  fail_unless_equals_int (info.new, 1);
  fail_unless_equals_int (info.old, 0);

  /* Move source1 to end (newpos:-1) */
  fail_unless (ges_simple_timeline_layer_move_object (GES_SIMPLE_TIMELINE_LAYER
          (layer), GES_CLIP (source1), -1));
  fail_unless_equals_uint64 (_START (source1), GST_SECOND);
  fail_unless_equals_uint64 (_START (source2), 0);
  /* position will be decremented, this is expected */
  fail_unless_equals_int (info.new, -1);
  fail_unless_equals_int (info.old, 0);

  /* remove source1, source2 should be moved to the beginning */
  g_object_ref (source1);
  fail_unless (ges_timeline_layer_remove_object (layer, GES_CLIP (source1)));
  fail_unless_equals_uint64 (_START (source2), 0);

  g_object_set (source1, "start", (guint64) 42, NULL);

  /* re-add source1... using the normal API, it should be added to the end */
  fail_unless (ges_timeline_layer_add_object (layer, GES_CLIP (source1)));
  fail_unless_equals_uint64 (_START (source2), 0);
  fail_unless_equals_uint64 (_START (source1), GST_SECOND);

  /* remove source1 ... */
  fail_unless (ges_timeline_layer_remove_object (layer, GES_CLIP (source1)));
  fail_unless_equals_uint64 (_START (source2), 0);
  /* ... and source2 */
  fail_unless (ges_timeline_layer_remove_object (layer, GES_CLIP (source2)));

  fail_unless (ges_timeline_remove_track (timeline, track));
  fail_unless (ges_timeline_remove_layer (timeline, layer));
  g_object_unref (timeline);
}

GST_END_TEST;

static void
valid_notify_cb (GObject * obj, GParamSpec * unused G_GNUC_UNUSED, gint * count)
{
  (*count)++;
}

GST_START_TEST (test_gsl_with_transitions)
{
  GESTimeline *timeline;
  GESTimelineLayer *layer;
  GESTrack *track;
  GESCustomSourceClip *source1, *source2, *source3, *source4;
  GESTransitionClip *tr1, *tr2, *tr3, *tr4, *tr5;
  GESSimpleTimelineLayer *gstl;
  gboolean valid;
  gint count = 0;

  ges_init ();

  /* Timeline and 1 Layer */
  timeline = ges_timeline_new ();
  layer = (GESTimelineLayer *) ges_simple_timeline_layer_new ();

  g_signal_connect (G_OBJECT (layer), "notify::valid",
      G_CALLBACK (valid_notify_cb), &count);

  fail_unless (ges_timeline_add_layer (timeline, layer));
  ges_timeline_layer_set_priority (layer, 0);

  track = ges_track_new (GES_TRACK_TYPE_VIDEO, gst_caps_new_any ());
  fail_unless (ges_timeline_add_track (timeline, track));

  track = ges_track_new (GES_TRACK_TYPE_AUDIO, gst_caps_new_any ());
  fail_unless (ges_timeline_add_track (timeline, track));


#define ELEMENT "videotestsrc"

  /* Create four 1s sources */
  source1 = ges_custom_source_clip_new (arbitrary_fill_track_func,
      (gpointer) ELEMENT);
  g_object_set (source1, "duration", GST_SECOND, "start", (guint64) 42, NULL);
  fail_unless_equals_uint64 (_DURATION (source1), GST_SECOND);

  /* make this source taller than the others, so we can check that
   * gstlrecalculate handles this properly */

  source2 = ges_custom_source_clip_new (arbitrary_fill_track_func,
      (gpointer) ELEMENT);
  g_object_set (source2, "duration", GST_SECOND, "start", (guint64) 42, NULL);
  GES_CLIP (source2)->height = 4;
  fail_unless_equals_uint64 (_DURATION (source2), GST_SECOND);

  source3 = ges_custom_source_clip_new (arbitrary_fill_track_func,
      (gpointer) ELEMENT);
  g_object_set (source3, "duration", GST_SECOND, "start", (guint64) 42, NULL);
  fail_unless_equals_uint64 (_DURATION (source3), GST_SECOND);

  source4 = ges_custom_source_clip_new (arbitrary_fill_track_func,
      (gpointer) ELEMENT);
  g_object_set (source4, "duration", GST_SECOND, "start", (guint64) 42, NULL);
  fail_unless_equals_uint64 (_DURATION (source4), GST_SECOND);

  /* create half-second transitions */

#define HALF_SECOND ((guint64) (0.5 * GST_SECOND))
#define SECOND(a) ((guint64) (a * GST_SECOND))

  tr1 = ges_transition_clip_new (GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE);
  g_object_set (tr1, "duration", HALF_SECOND, "start", (guint64) 42, NULL);
  fail_unless_equals_uint64 (_DURATION (tr1), HALF_SECOND);

  tr2 = ges_transition_clip_new (GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE);
  g_object_set (tr2, "duration", HALF_SECOND, "start", (guint64) 42, NULL);
  fail_unless_equals_uint64 (_DURATION (tr2), HALF_SECOND);

  tr3 = ges_transition_clip_new (GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE);
  g_object_set (tr3, "duration", HALF_SECOND, "start", (guint64) 42, NULL);
  fail_unless_equals_uint64 (_DURATION (tr3), HALF_SECOND);

  tr4 = ges_transition_clip_new (GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE);
  g_object_set (tr4, "duration", HALF_SECOND, "start", (guint64) 42, NULL);
  fail_unless_equals_uint64 (_DURATION (tr4), HALF_SECOND);

  tr5 = ges_transition_clip_new (GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE);
  g_object_set (tr5, "duration", HALF_SECOND, "start", (guint64) 42, NULL);
  fail_unless_equals_uint64 (_DURATION (tr5), HALF_SECOND);

  /*   simple test scenario with several sources in layer */
  /*   [0     0.5     1       1.5     2       2.5     3]  */
  /* 0                                                    */
  /* 1        [1-tr1--]                                   */
  /* 2 [0--source1----][3-tr2--]                          */
  /* 3        [2---source2-----]                          */
  /* 4        [2---source2-----]                          */
  /* 5        [2---source2-----]                          */
  /* 6        [2---source2-----]                          */
  /* 7                 [4---source3---]                   */
  /* 8                                [5---source4-----]  */


  gstl = GES_SIMPLE_TIMELINE_LAYER (layer);

  /* add objects in sequence */

  GST_DEBUG ("Adding source1");

  fail_unless (ges_simple_timeline_layer_add_object (gstl,
          GES_CLIP (source1), -1));
  fail_unless_equals_uint64 (_DURATION (source1), GST_SECOND);
  fail_unless_equals_uint64 (_START (source1), 0);
  fail_unless_equals_uint64 (_PRIORITY (source1), 2);

  GST_DEBUG ("Adding tr1");

  fail_unless (ges_simple_timeline_layer_add_object (gstl, GES_CLIP (tr1), -1));
  fail_unless_equals_uint64 (_DURATION (source1), GST_SECOND);
  fail_unless_equals_uint64 (_START (source1), 0);
  fail_unless_equals_uint64 (_PRIORITY (source1), 2);
  fail_unless_equals_uint64 (_DURATION (tr1), HALF_SECOND);
  fail_unless_equals_uint64 (_START (tr1), HALF_SECOND);
  fail_unless_equals_uint64 (_PRIORITY (tr1), 1);

  GST_DEBUG ("Adding source2");

  fail_unless (ges_simple_timeline_layer_add_object (gstl,
          GES_CLIP (source2), -1));
  fail_unless_equals_uint64 (_DURATION (source1), GST_SECOND);
  fail_unless_equals_uint64 (_START (source1), 0);
  fail_unless_equals_uint64 (_PRIORITY (source1), 2);
  fail_unless_equals_uint64 (_DURATION (tr1), HALF_SECOND);
  fail_unless_equals_uint64 (_START (tr1), HALF_SECOND);
  fail_unless_equals_uint64 (_PRIORITY (tr1), 1);
  fail_unless_equals_uint64 (_DURATION (source2), GST_SECOND);
  fail_unless_equals_uint64 (_START (source2), HALF_SECOND);
  fail_unless_equals_uint64 (_PRIORITY (source2), 3);

  /* add the third source before the second transition */

  GST_DEBUG ("Adding source3");

  fail_unless (ges_simple_timeline_layer_add_object (gstl,
          GES_CLIP (source3), -1));
  fail_unless_equals_uint64 (_DURATION (source1), GST_SECOND);
  fail_unless_equals_uint64 (_START (source1), 0);
  fail_unless_equals_uint64 (_PRIORITY (source1), 2);
  fail_unless_equals_uint64 (_DURATION (tr1), HALF_SECOND);
  fail_unless_equals_uint64 (_START (tr1), HALF_SECOND);
  fail_unless_equals_uint64 (_PRIORITY (tr1), 1);
  fail_unless_equals_uint64 (_DURATION (source2), GST_SECOND);
  fail_unless_equals_uint64 (_START (source2), HALF_SECOND);
  fail_unless_equals_uint64 (_PRIORITY (source2), 3);
  fail_unless_equals_uint64 (_DURATION (source3), GST_SECOND);
  fail_unless_equals_uint64 (_START (source3), SECOND (1.5));
  fail_unless_equals_uint64 (_PRIORITY (source3), 7);

  /* now add the second transition */

  GST_DEBUG ("Adding tr2");

  fail_unless (ges_simple_timeline_layer_add_object (gstl, GES_CLIP (tr2), 3));
  fail_unless_equals_uint64 (_DURATION (source1), GST_SECOND);
  fail_unless_equals_uint64 (_START (source1), 0);
  fail_unless_equals_uint64 (_PRIORITY (source1), 2);
  fail_unless_equals_uint64 (_DURATION (tr1), HALF_SECOND);
  fail_unless_equals_uint64 (_START (tr1), HALF_SECOND);
  fail_unless_equals_uint64 (_PRIORITY (tr1), 1);
  fail_unless_equals_uint64 (_DURATION (source2), GST_SECOND);
  fail_unless_equals_uint64 (_START (source2), HALF_SECOND);
  fail_unless_equals_uint64 (_PRIORITY (source2), 3);
  fail_unless_equals_uint64 (_DURATION (tr2), HALF_SECOND);
  fail_unless_equals_uint64 (_START (tr2), GST_SECOND);
  fail_unless_equals_uint64 (_PRIORITY (tr2), 2);
  fail_unless_equals_uint64 (_DURATION (source3), GST_SECOND);
  fail_unless_equals_uint64 (_START (source3), GST_SECOND);
  fail_unless_equals_uint64 (_PRIORITY (source3), 7);

  /* fourth source */

  GST_DEBUG ("Adding source4");

  fail_unless (ges_simple_timeline_layer_add_object (gstl,
          GES_CLIP (source4), -1));
  fail_unless_equals_uint64 (_DURATION (source1), GST_SECOND);
  fail_unless_equals_uint64 (_START (source1), 0);
  fail_unless_equals_uint64 (_PRIORITY (source1), 2);
  fail_unless_equals_uint64 (_DURATION (tr1), HALF_SECOND);
  fail_unless_equals_uint64 (_START (tr1), HALF_SECOND);
  fail_unless_equals_uint64 (_PRIORITY (tr1), 1);
  fail_unless_equals_uint64 (_DURATION (source2), GST_SECOND);
  fail_unless_equals_uint64 (_START (source2), HALF_SECOND);
  fail_unless_equals_uint64 (_PRIORITY (source2), 3);
  fail_unless_equals_uint64 (_DURATION (tr2), HALF_SECOND);
  fail_unless_equals_uint64 (_START (tr2), GST_SECOND);
  fail_unless_equals_uint64 (_PRIORITY (tr2), 2);
  fail_unless_equals_uint64 (_DURATION (source3), GST_SECOND);
  fail_unless_equals_uint64 (_START (source3), GST_SECOND);
  fail_unless_equals_uint64 (_PRIORITY (source3), 7);
  fail_unless_equals_uint64 (_DURATION (source4), GST_SECOND);
  fail_unless_equals_uint64 (_START (source4), SECOND (2));
  fail_unless_equals_uint64 (_PRIORITY (source4), 8);

  /* check that any insertion which might result in two adjacent transitions
   * will fail */

  GST_DEBUG ("Checking wrong insertion of tr3");

  fail_if (ges_simple_timeline_layer_add_object (gstl, GES_CLIP (tr3), 1));

  fail_if (ges_simple_timeline_layer_add_object (gstl, GES_CLIP (tr3), 2));

  fail_if (ges_simple_timeline_layer_add_object (gstl, GES_CLIP (tr3), 3));

  fail_if (ges_simple_timeline_layer_add_object (gstl, GES_CLIP (tr3), 4));

  /* check that insertions which don't cause problems still work */

  GST_DEBUG ("Checking correct insertion of tr3");

  fail_unless (ges_simple_timeline_layer_add_object (gstl, GES_CLIP (tr3), 5));

  /* at this point the layer should still be valid */
  g_object_get (G_OBJECT (layer), "valid", &valid, NULL);
  fail_unless (valid);
  fail_unless_equals_int (count, 3);

  GST_DEBUG ("Checking correct insertion of tr4");

  fail_unless (ges_simple_timeline_layer_add_object (gstl, GES_CLIP (tr4), -1));

  GST_DEBUG ("Checking correct insertion of tr5");

  fail_unless (ges_simple_timeline_layer_add_object (gstl, GES_CLIP (tr5), 0));

  /* removals which result in two or more adjacent transitions will also
   * print a warning on the console. This is expected */

  GST_DEBUG ("Removing source1");

  fail_unless (ges_timeline_layer_remove_object (layer, GES_CLIP (source1)));

  /* layer should now be invalid */

  g_object_get (G_OBJECT (layer), "valid", &valid, NULL);
  fail_unless (!valid);
  fail_unless_equals_int (count, 4);

  GST_DEBUG ("Removing source2/3/4");

  fail_unless (ges_timeline_layer_remove_object (layer, GES_CLIP (source2)));
  fail_unless (ges_timeline_layer_remove_object (layer, GES_CLIP (source3)));
  fail_unless (ges_timeline_layer_remove_object (layer, GES_CLIP (source4)));

  g_object_get (G_OBJECT (layer), "valid", &valid, NULL);
  fail_unless (!valid);
  fail_unless_equals_int (count, 4);

  GST_DEBUG ("Removing transitions");

  fail_unless (ges_timeline_layer_remove_object (layer, GES_CLIP (tr1)));
  fail_unless (ges_timeline_layer_remove_object (layer, GES_CLIP (tr2)));
  fail_unless (ges_timeline_layer_remove_object (layer, GES_CLIP (tr3)));
  fail_unless (ges_timeline_layer_remove_object (layer, GES_CLIP (tr4)));
  fail_unless (ges_timeline_layer_remove_object (layer, GES_CLIP (tr5)));

  GST_DEBUG ("done removing transition");

  g_object_unref (timeline);
}

GST_END_TEST;

static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges-simple-timeline-layer");
  TCase *tc_chain = tcase_create ("basic");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_gsl_add);
  tcase_add_test (tc_chain, test_gsl_move_simple);
  tcase_add_test (tc_chain, test_gsl_with_transitions);

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
