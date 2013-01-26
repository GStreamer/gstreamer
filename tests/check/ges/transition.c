
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
  GESTransitionClip *tr1, *tr2;
  GESTrackElement *trackelement;
  GESTrack *track;

  ges_init ();

  track = ges_track_video_raw_new ();
  fail_unless (track != 0);

  tr1 = ges_transition_clip_new (GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE);
  fail_unless (tr1 != 0);
  fail_unless (tr1->vtype == GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE);

  tr2 = ges_transition_clip_new_for_nick ((gchar *) "bar-wipe-lr");
  fail_unless (tr2 != 0);
  fail_unless (tr2->vtype == 1);

  /* Make sure track object is created and vtype is set */
  trackelement = ges_clip_create_track_element (GES_CLIP (tr2), track->type);
  ges_clip_add_track_element (GES_CLIP (tr2), trackelement);

  fail_unless (trackelement != NULL);
  fail_unless (ges_track_video_transition_get_transition_type
      (GES_TRACK_VIDEO_TRANSITION (trackelement)) == 1);

  fail_unless (ges_clip_release_track_element (GES_CLIP
          (tr2), trackelement) == TRUE);

  g_object_unref (track);
  g_object_unref (tr1);
  g_object_unref (tr2);
}

GST_END_TEST;

GST_START_TEST (test_transition_properties)
{
  GESTrack *track;
  GESTrackElement *trackelement;
  GESClip *object;

  ges_init ();

  object =
      GES_CLIP (ges_transition_clip_new
      (GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE));

  track = ges_track_video_raw_new ();
  fail_unless (track != NULL);

  /* Set some properties */
  g_object_set (object, "start", (guint64) 42, "duration", (guint64) 51,
      "in-point", (guint64) 12, NULL);

  assert_equals_uint64 (_START (object), 42);
  assert_equals_uint64 (_DURATION (object), 51);
  assert_equals_uint64 (_INPOINT (object), 12);

  trackelement = ges_clip_create_track_element (object, track->type);
  ges_clip_add_track_element (object, trackelement);
  fail_unless (trackelement != NULL);
  fail_unless (ges_track_element_set_track (trackelement, track));

  /* Check that trackelement has the same properties */
  assert_equals_uint64 (_START (trackelement), 42);
  assert_equals_uint64 (_DURATION (trackelement), 51);
  assert_equals_uint64 (_INPOINT (trackelement), 12);

  /* And let's also check that it propagated correctly to GNonLin */
  gnl_object_check (ges_track_element_get_gnlobject (trackelement), 42, 51, 12,
      51, 0, TRUE);

  /* Change more properties, see if they propagate */
  g_object_set (object, "start", (guint64) 420, "duration", (guint64) 510,
      "in-point", (guint64) 120, NULL);
  assert_equals_uint64 (_START (object), 420);
  assert_equals_uint64 (_DURATION (object), 510);
  assert_equals_uint64 (_INPOINT (object), 120);
  assert_equals_uint64 (_START (trackelement), 420);
  assert_equals_uint64 (_DURATION (trackelement), 510);
  assert_equals_uint64 (_INPOINT (trackelement), 120);

  /* And let's also check that it propagated correctly to GNonLin */
  gnl_object_check (ges_track_element_get_gnlobject (trackelement), 420, 510,
      120, 510, 0, TRUE);

  /* test changing vtype */
  GST_DEBUG ("Setting to crossfade");
  g_object_set (object, "vtype", GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE,
      NULL);
  assert_equals_int (GES_TRANSITION_CLIP (object)->vtype,
      GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE);
  assert_equals_int (ges_track_video_transition_get_transition_type
      (GES_TRACK_VIDEO_TRANSITION (trackelement)),
      GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE);

  /* Check that changing from crossfade to anything else fails (it should
   * still be using crossfade */
  GST_DEBUG ("Setting back to 1 (should fail)");
  g_object_set (object, "vtype", 1, NULL);

  assert_equals_int (GES_TRANSITION_CLIP (object)->vtype, 1);
  assert_equals_int (ges_track_video_transition_get_transition_type
      (GES_TRACK_VIDEO_TRANSITION (trackelement)), 1);

  GST_DEBUG ("Releasing track object");
  ges_clip_release_track_element (object, trackelement);

  g_object_set (object, "vtype", 1, NULL);

  GST_DEBUG ("creating track object");
  trackelement = ges_clip_create_track_element (object, track->type);
  ges_clip_add_track_element (object, trackelement);
  fail_unless (trackelement != NULL);
  fail_unless (ges_track_element_set_track (trackelement, track));

  /* The new track object should have taken the previously set transition
   * type (in this case 1) */
  GST_DEBUG ("Setting to vtype:1");
  assert_equals_int (ges_track_video_transition_get_transition_type
      (GES_TRACK_VIDEO_TRANSITION (trackelement)), 1);
  assert_equals_int (GES_TRANSITION_CLIP (object)->vtype, 1);

  ges_clip_release_track_element (object, trackelement);
  g_object_unref (object);
  g_object_unref (track);
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
