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
my_fill_track_func (GESTimelineObject * object,
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

GST_START_TEST (test_object_properties)
{
  GESTrack *track;
  GESTrackObject *trackobject;
  GESTimelineObject *object;

  ges_init ();

  track = ges_track_new (GES_TRACK_TYPE_CUSTOM, gst_caps_ref (GST_CAPS_ANY));
  fail_unless (track != NULL);

  object =
      (GESTimelineObject *) ges_custom_timeline_source_new (my_fill_track_func,
      NULL);
  fail_unless (object != NULL);

  /* Set some properties */
  g_object_set (object, "start", (guint64) 42, "duration", (guint64) 51,
      "in-point", (guint64) 12, NULL);
  assert_equals_uint64 (_START (object), 42);
  assert_equals_uint64 (_DURATION (object), 51);
  assert_equals_uint64 (_INPOINT (object), 12);

  trackobject = ges_timeline_object_create_track_object (object, track->type);
  ges_timeline_object_add_track_object (object, trackobject);
  fail_unless (trackobject != NULL);
  fail_unless (ges_track_object_set_track (trackobject, track));

  /* Check that trackobject has the same properties */
  assert_equals_uint64 (_START (trackobject), 42);
  assert_equals_uint64 (_DURATION (trackobject), 51);
  assert_equals_uint64 (_INPOINT (trackobject), 12);

  /* And let's also check that it propagated correctly to GNonLin */
  gnl_object_check (ges_track_object_get_gnlobject (trackobject), 42, 51, 12,
      51, 0, TRUE);

  /* Change more properties, see if they propagate */
  g_object_set (object, "start", (guint64) 420, "duration", (guint64) 510,
      "in-point", (guint64) 120, NULL);
  assert_equals_uint64 (_START (object), 420);
  assert_equals_uint64 (_DURATION (object), 510);
  assert_equals_uint64 (_INPOINT (object), 120);
  assert_equals_uint64 (_START (trackobject), 420);
  assert_equals_uint64 (_DURATION (trackobject), 510);
  assert_equals_uint64 (_INPOINT (trackobject), 120);

  /* And let's also check that it propagated correctly to GNonLin */
  gnl_object_check (ges_track_object_get_gnlobject (trackobject), 420, 510, 120,
      510, 0, TRUE);


  /* This time, we move the trackobject to see if the changes move
   * along to the parent and the gnonlin object */
  g_object_set (trackobject, "start", (guint64) 400, NULL);
  assert_equals_uint64 (_START (object), 400);
  assert_equals_uint64 (_START (trackobject), 400);
  gnl_object_check (ges_track_object_get_gnlobject (trackobject), 400, 510, 120,
      510, 0, TRUE);

  ges_timeline_object_release_track_object (object, trackobject);

  g_object_unref (object);
  g_object_unref (track);
}

GST_END_TEST;

GST_START_TEST (test_object_properties_unlocked)
{
  GESTrack *track;
  GESTrackObject *trackobject;
  GESTimelineObject *object;

  ges_init ();

  track = ges_track_new (GES_TRACK_TYPE_CUSTOM, gst_caps_ref (GST_CAPS_ANY));
  fail_unless (track != NULL);

  object =
      (GESTimelineObject *) ges_custom_timeline_source_new (my_fill_track_func,
      NULL);
  fail_unless (object != NULL);

  /* Set some properties */
  g_object_set (object, "start", (guint64) 42, "duration", (guint64) 51,
      "in-point", (guint64) 12, NULL);
  assert_equals_uint64 (_START (object), 42);
  assert_equals_uint64 (_DURATION (object), 51);
  assert_equals_uint64 (_INPOINT (object), 12);

  trackobject = ges_timeline_object_create_track_object (object, track->type);
  ges_timeline_object_add_track_object (object, trackobject);
  fail_unless (trackobject != NULL);
  fail_unless (ges_track_object_set_track (trackobject, track));

  /* Check that trackobject has the same properties */
  assert_equals_uint64 (_START (trackobject), 42);
  assert_equals_uint64 (_DURATION (trackobject), 51);
  assert_equals_uint64 (_INPOINT (trackobject), 12);

  /* And let's also check that it propagated correctly to GNonLin */
  gnl_object_check (ges_track_object_get_gnlobject (trackobject), 42, 51, 12,
      51, 0, TRUE);

  /* This time we unlock the trackobject and make sure it doesn't propagate */
  ges_track_object_set_locked (trackobject, FALSE);

  /* Change more properties, they will be set on the GESTimelineObject */
  g_object_set (object, "start", (guint64) 420, "duration", (guint64) 510,
      "in-point", (guint64) 120, NULL);
  assert_equals_uint64 (_START (object), 420);
  assert_equals_uint64 (_DURATION (object), 510);
  assert_equals_uint64 (_INPOINT (object), 120);
  /* ... but not on the GESTrackObject since it was unlocked... */
  assert_equals_uint64 (_START (trackobject), 42);
  assert_equals_uint64 (_DURATION (trackobject), 51);
  assert_equals_uint64 (_INPOINT (trackobject), 12);
  /* ... and neither on the GNonLin object */
  gnl_object_check (ges_track_object_get_gnlobject (trackobject), 42, 51, 12,
      51, 0, TRUE);

  /* When unlocked, moving the GESTrackObject won't move the GESTimelineObject
   * either */
  /* This time, we move the trackobject to see if the changes move
   * along to the parent and the gnonlin object */
  g_object_set (trackobject, "start", (guint64) 400, NULL);
  assert_equals_uint64 (_START (object), 420);
  assert_equals_uint64 (_START (trackobject), 400);
  gnl_object_check (ges_track_object_get_gnlobject (trackobject), 400, 51, 12,
      51, 0, TRUE);


  ges_timeline_object_release_track_object (object, trackobject);

  g_object_unref (object);
  g_object_unref (track);
}

GST_END_TEST;

GST_START_TEST (test_split_object)
{
  GESTrack *track;
  GESTrackObject *trackobject, *splittckobj;
  GESTimelineObject *object, *splitobj;
  GList *splittckobjs;

  ges_init ();

  track = ges_track_new (GES_TRACK_TYPE_CUSTOM, gst_caps_ref (GST_CAPS_ANY));
  fail_unless (track != NULL);

  object =
      (GESTimelineObject *) ges_custom_timeline_source_new (my_fill_track_func,
      NULL);
  fail_unless (object != NULL);

  /* Set some properties */
  g_object_set (object, "start", (guint64) 42, "duration", (guint64) 50,
      "in-point", (guint64) 12, NULL);
  assert_equals_uint64 (_START (object), 42);
  assert_equals_uint64 (_DURATION (object), 50);
  assert_equals_uint64 (_INPOINT (object), 12);

  trackobject = ges_timeline_object_create_track_object (object, track->type);
  ges_timeline_object_add_track_object (object, trackobject);
  fail_unless (trackobject != NULL);
  fail_unless (ges_track_object_set_track (trackobject, track));

  /* Check that trackobject has the same properties */
  assert_equals_uint64 (_START (trackobject), 42);
  assert_equals_uint64 (_DURATION (trackobject), 50);
  assert_equals_uint64 (_INPOINT (trackobject), 12);

  /* And let's also check that it propagated correctly to GNonLin */
  gnl_object_check (ges_track_object_get_gnlobject (trackobject), 42, 50, 12,
      50, 0, TRUE);

  splitobj = ges_timeline_object_split (object, 67);
  fail_unless (GES_IS_TIMELINE_OBJECT (splitobj));

  assert_equals_uint64 (_START (object), 42);
  assert_equals_uint64 (_DURATION (object), 25);
  assert_equals_uint64 (_INPOINT (object), 12);

  assert_equals_uint64 (_START (splitobj), 67);
  assert_equals_uint64 (_DURATION (splitobj), 25);
  assert_equals_uint64 (_INPOINT (splitobj), 37);

  splittckobjs = ges_timeline_object_get_track_objects (splitobj);
  fail_unless_equals_int (g_list_length (splittckobjs), 1);

  splittckobj = GES_TRACK_OBJECT (splittckobjs->data);
  fail_unless (GES_IS_TRACK_OBJECT (splittckobj));
  assert_equals_uint64 (_START (splittckobj), 67);
  assert_equals_uint64 (_DURATION (splittckobj), 25);
  assert_equals_uint64 (_INPOINT (splittckobj), 37);

  fail_unless (splittckobj != trackobject);
  fail_unless (splitobj != object);

  /* We own the only ref */
  ASSERT_OBJECT_REFCOUNT (splitobj, "splitobj", 1);
  /* 1 ref for the TimelineObject, 1 ref for the Track and 1 in splittckobjs */
  ASSERT_OBJECT_REFCOUNT (splittckobj, "splittckobj", 3);

  g_object_unref (track);
  g_object_unref (splitobj);
  g_object_unref (object);

  ASSERT_OBJECT_REFCOUNT (splittckobj, "splittckobj", 1);
  g_list_free_full (splittckobjs, g_object_unref);
}

GST_END_TEST;

static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges-timeline-object");
  TCase *tc_chain = tcase_create ("timeline-object");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_object_properties);
  tcase_add_test (tc_chain, test_object_properties_unlocked);
  tcase_add_test (tc_chain, test_split_object);

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
