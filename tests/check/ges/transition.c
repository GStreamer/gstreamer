
/* GStreamer Editing Services
 * Copyright (C) 2010 Edward Hervey <brandon@collabora.co.uk>
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

/* This test uri will eventually have to be fixed */
#define TEST_URI "blahblahblah"

GST_START_TEST (test_transition_basic)
{
  GESTrack *track;
  GESTimeline *timeline;
  GESLayer *layer;
  GESTransitionClip *tr1, *tr2;
  GESTrackElement *trackelement;

  ges_init ();

  track = GES_TRACK (ges_video_track_new ());
  layer = ges_layer_new ();
  timeline = ges_timeline_new ();
  fail_unless (track != NULL);
  fail_unless (layer != NULL);
  fail_unless (timeline != NULL);
  fail_unless (ges_timeline_add_layer (timeline, layer));
  fail_unless (ges_timeline_add_track (timeline, track));
  ASSERT_OBJECT_REFCOUNT (timeline, "timeline", 1);

  tr1 = ges_transition_clip_new (GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE);
  fail_unless (tr1 != 0);
  fail_unless (tr1->vtype == GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE);

  tr2 = ges_transition_clip_new_for_nick ((gchar *) "bar-wipe-lr");
  fail_unless (tr2 != 0);
  fail_unless (tr2->vtype == 1);

  /* Make sure track element is created and vtype is set */
  fail_unless (ges_layer_add_clip (layer, GES_CLIP (tr2)));
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (tr2)), 1);
  trackelement = GES_CONTAINER_CHILDREN (tr2)->data;
  fail_unless (trackelement != NULL);
  fail_unless (ges_video_transition_get_transition_type
      (GES_VIDEO_TRANSITION (trackelement)) == 1);

  gst_object_unref (timeline);
}

GST_END_TEST;

GST_START_TEST (test_transition_properties)
{
  GESClip *clip;
  GESTrack *track;
  GESTimeline *timeline;
  GESLayer *layer;
  GESTrackElement *trackelement;

  ges_init ();

  clip = GES_CLIP (ges_transition_clip_new
      (GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE));

  track = GES_TRACK (ges_video_track_new ());
  layer = ges_layer_new ();
  timeline = ges_timeline_new ();
  fail_unless (track != NULL);
  fail_unless (layer != NULL);
  fail_unless (timeline != NULL);
  fail_unless (ges_timeline_add_layer (timeline, layer));
  fail_unless (ges_timeline_add_track (timeline, track));
  ASSERT_OBJECT_REFCOUNT (timeline, "timeline", 1);

  /* Set some properties */
  g_object_set (clip, "start", (guint64) 42, "duration", (guint64) 51,
      "in-point", (guint64) 12, NULL);

  assert_equals_uint64 (_START (clip), 42);
  assert_equals_uint64 (_DURATION (clip), 51);
  assert_equals_uint64 (_INPOINT (clip), 12);

  fail_unless (ges_layer_add_clip (layer, GES_CLIP (clip)));
  ges_timeline_commit (timeline);
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (clip)), 1);
  trackelement = GES_CONTAINER_CHILDREN (clip)->data;
  fail_unless (trackelement != NULL);

  /* Check that trackelement has the same properties */
  assert_equals_uint64 (_START (trackelement), 42);
  assert_equals_uint64 (_DURATION (trackelement), 51);
  assert_equals_uint64 (_INPOINT (trackelement), 12);

  /* And let's also check that it propagated correctly to GNonLin */
  gnl_object_check (ges_track_element_get_gnlobject (trackelement), 42, 51, 12,
      51, MIN_GNL_PRIO, TRUE);

  /* Change more properties, see if they propagate */
  g_object_set (clip, "start", (guint64) 420, "duration", (guint64) 510,
      "in-point", (guint64) 120, NULL);
  ges_timeline_commit (timeline);
  assert_equals_uint64 (_START (clip), 420);
  assert_equals_uint64 (_DURATION (clip), 510);
  assert_equals_uint64 (_INPOINT (clip), 120);
  assert_equals_uint64 (_START (trackelement), 420);
  assert_equals_uint64 (_DURATION (trackelement), 510);
  assert_equals_uint64 (_INPOINT (trackelement), 120);

  /* And let's also check that it propagated correctly to GNonLin */
  gnl_object_check (ges_track_element_get_gnlobject (trackelement), 420, 510,
      120, 510, MIN_GNL_PRIO + 0, TRUE);

  /* test changing vtype */
  GST_DEBUG ("Setting to crossfade");
  g_object_set (clip, "vtype", GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE,
      NULL);
  assert_equals_int (GES_TRANSITION_CLIP (clip)->vtype,
      GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE);
  assert_equals_int (ges_video_transition_get_transition_type
      (GES_VIDEO_TRANSITION (trackelement)),
      GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE);

  /* Check that changing from crossfade to anything else fails (it should
   * still be using crossfade */
  GST_DEBUG ("Setting back to 1 (should fail)");
  g_object_set (clip, "vtype", 1, NULL);

  assert_equals_int (GES_TRANSITION_CLIP (clip)->vtype, 1);
  assert_equals_int (ges_video_transition_get_transition_type
      (GES_VIDEO_TRANSITION (trackelement)), 1);

  GST_DEBUG ("Removing clip from layer");
  gst_object_ref (clip);        /* We do not want it to be destroyed */
  ges_layer_remove_clip (layer, clip);

  g_object_set (clip, "vtype", 1, NULL);
  GST_DEBUG ("Read it to the layer");
  fail_unless (ges_layer_add_clip (layer, GES_CLIP (clip)));
  g_object_unref (clip);
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (clip)), 1);
  trackelement = GES_CONTAINER_CHILDREN (clip)->data;
  fail_unless (trackelement != NULL);

  /* The new track element should have taken the previously set transition
   * type (in this case 1) */
  GST_DEBUG ("Setting to vtype:1");
  assert_equals_int (ges_video_transition_get_transition_type
      (GES_VIDEO_TRANSITION (trackelement)), 1);
  assert_equals_int (GES_TRANSITION_CLIP (clip)->vtype, 1);

  check_destroyed (G_OBJECT (timeline), G_OBJECT (track), clip, NULL);
}

GST_END_TEST;



static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges-transition");
  TCase *tc_chain = tcase_create ("transition");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_transition_basic);
  tcase_add_test (tc_chain, test_transition_properties);

  return s;
}

GST_CHECK_MAIN (ges);
