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
  /* If this fails... that means that there already was something
   * in it */
  fail_unless (gst_bin_add (GST_BIN (gnlobj), src));

  return TRUE;
}

#define gnl_object_check(gnlobj, start, duration, mstart, mduration, priority, active) { \
  guint64 pstart, pdur, pmstart, pmdur;					\
  guint32 pprio;							\
  gboolean pact;							\
  g_object_get (gnlobj, "start", &pstart, "duration", &pdur,		\
		"media-start", &pmstart, "media-duration", &pmdur,	\
		"priority", &pprio, "active", &pact,			\
		NULL);							\
  assert_equals_uint64 (pstart, start);					\
  assert_equals_uint64 (pdur, duration);					\
  assert_equals_uint64 (pmstart, mstart);					\
  assert_equals_uint64 (pmdur, mduration);					\
  assert_equals_int (pprio, priority);					\
  assert_equals_int (pact, active);					\
  }


GST_START_TEST (test_object_properties)
{
  GESTrack *track;
  GESTrackObject *trackobject;
  GESTimelineObject *object;

  ges_init ();

  track = ges_track_new (GES_TRACK_TYPE_CUSTOM, GST_CAPS_ANY);
  fail_unless (track != NULL);

  object =
      (GESTimelineObject *) ges_custom_timeline_source_new (my_fill_track_func,
      NULL);
  fail_unless (object != NULL);

  /* Set some properties */
  g_object_set (object, "start", (guint64) 42, "duration", (guint64) 51,
      "in-point", (guint64) 12, NULL);
  assert_equals_uint64 (GES_TIMELINE_OBJECT_START (object), 42);
  assert_equals_uint64 (GES_TIMELINE_OBJECT_DURATION (object), 51);
  assert_equals_uint64 (GES_TIMELINE_OBJECT_INPOINT (object), 12);

  trackobject = ges_timeline_object_create_track_object (object, track);
  ges_timeline_object_add_track_object (object, trackobject);
  fail_unless (trackobject != NULL);
  fail_unless (ges_track_object_set_track (trackobject, track));

  /* Check that trackobject has the same properties */
  assert_equals_uint64 (GES_TRACK_OBJECT_START (trackobject), 42);
  assert_equals_uint64 (GES_TRACK_OBJECT_DURATION (trackobject), 51);
  assert_equals_uint64 (GES_TRACK_OBJECT_INPOINT (trackobject), 12);

  /* And let's also check that it propagated correctly to GNonLin */
  gnl_object_check (ges_track_object_get_gnlobject (trackobject), 42, 51, 12,
      51, 0, TRUE);

  /* Change more properties, see if they propagate */
  g_object_set (object, "start", (guint64) 420, "duration", (guint64) 510,
      "in-point", (guint64) 120, NULL);
  assert_equals_uint64 (GES_TIMELINE_OBJECT_START (object), 420);
  assert_equals_uint64 (GES_TIMELINE_OBJECT_DURATION (object), 510);
  assert_equals_uint64 (GES_TIMELINE_OBJECT_INPOINT (object), 120);
  assert_equals_uint64 (GES_TRACK_OBJECT_START (trackobject), 420);
  assert_equals_uint64 (GES_TRACK_OBJECT_DURATION (trackobject), 510);
  assert_equals_uint64 (GES_TRACK_OBJECT_INPOINT (trackobject), 120);

  /* And let's also check that it propagated correctly to GNonLin */
  gnl_object_check (ges_track_object_get_gnlobject (trackobject), 420, 510, 120,
      510, 0, TRUE);


  /* This time, we move the trackobject to see if the changes move
   * along to the parent and the gnonlin object */
  g_object_set (trackobject, "start", (guint64) 400, NULL);
  assert_equals_uint64 (GES_TIMELINE_OBJECT_START (object), 400);
  assert_equals_uint64 (GES_TRACK_OBJECT_START (trackobject), 400);
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

  track = ges_track_new (GES_TRACK_TYPE_CUSTOM, GST_CAPS_ANY);
  fail_unless (track != NULL);

  object =
      (GESTimelineObject *) ges_custom_timeline_source_new (my_fill_track_func,
      NULL);
  fail_unless (object != NULL);

  /* Set some properties */
  g_object_set (object, "start", (guint64) 42, "duration", (guint64) 51,
      "in-point", (guint64) 12, NULL);
  assert_equals_uint64 (GES_TIMELINE_OBJECT_START (object), 42);
  assert_equals_uint64 (GES_TIMELINE_OBJECT_DURATION (object), 51);
  assert_equals_uint64 (GES_TIMELINE_OBJECT_INPOINT (object), 12);

  trackobject = ges_timeline_object_create_track_object (object, track);
  ges_timeline_object_add_track_object (object, trackobject);
  fail_unless (trackobject != NULL);
  fail_unless (ges_track_object_set_track (trackobject, track));

  /* Check that trackobject has the same properties */
  assert_equals_uint64 (GES_TRACK_OBJECT_START (trackobject), 42);
  assert_equals_uint64 (GES_TRACK_OBJECT_DURATION (trackobject), 51);
  assert_equals_uint64 (GES_TRACK_OBJECT_INPOINT (trackobject), 12);

  /* And let's also check that it propagated correctly to GNonLin */
  gnl_object_check (ges_track_object_get_gnlobject (trackobject), 42, 51, 12,
      51, 0, TRUE);

  /* This time we unlock the trackobject and make sure it doesn't propagate */
  ges_track_object_set_locked (trackobject, FALSE);

  /* Change more properties, they will be set on the GESTimelineObject */
  g_object_set (object, "start", (guint64) 420, "duration", (guint64) 510,
      "in-point", (guint64) 120, NULL);
  assert_equals_uint64 (GES_TIMELINE_OBJECT_START (object), 420);
  assert_equals_uint64 (GES_TIMELINE_OBJECT_DURATION (object), 510);
  assert_equals_uint64 (GES_TIMELINE_OBJECT_INPOINT (object), 120);
  /* ... but not on the GESTrackObject since it was unlocked... */
  assert_equals_uint64 (GES_TRACK_OBJECT_START (trackobject), 42);
  assert_equals_uint64 (GES_TRACK_OBJECT_DURATION (trackobject), 51);
  assert_equals_uint64 (GES_TRACK_OBJECT_INPOINT (trackobject), 12);
  /* ... and neither on the GNonLin object */
  gnl_object_check (ges_track_object_get_gnlobject (trackobject), 42, 51, 12,
      51, 0, TRUE);

  /* When unlocked, moving the GESTrackObject won't move the GESTimelineObject
   * either */
  /* This time, we move the trackobject to see if the changes move
   * along to the parent and the gnonlin object */
  g_object_set (trackobject, "start", (guint64) 400, NULL);
  assert_equals_uint64 (GES_TIMELINE_OBJECT_START (object), 420);
  assert_equals_uint64 (GES_TRACK_OBJECT_START (trackobject), 400);
  gnl_object_check (ges_track_object_get_gnlobject (trackobject), 400, 51, 12,
      51, 0, TRUE);


  ges_timeline_object_release_track_object (object, trackobject);

  g_object_unref (object);
  g_object_unref (track);
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
