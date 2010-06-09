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

/* This test uri will eventually have to be fixed */
#define TEST_URI "blahblahblah"

GST_START_TEST (test_filesource_basic)
{
  GESTrack *track;
  GESTrackObject *trackobject;
  GESTimelineFileSource *source;
  gchar *uri;

  ges_init ();

  track = ges_track_new (GES_TRACK_TYPE_CUSTOM, GST_CAPS_ANY);
  fail_unless (track != NULL);

  source = ges_timeline_filesource_new ((gchar *) TEST_URI);
  fail_unless (source != NULL);

  /* Make sure the object was properly set */
  g_object_get (source, "uri", &uri, NULL);
  fail_unless (g_ascii_strcasecmp (uri, TEST_URI) == 0);
  g_free (uri);

  /* Make sure no track object is created for an incompatible
   * track. */
  trackobject =
      ges_timeline_object_create_track_object (GES_TIMELINE_OBJECT (source),
      track);
  fail_unless (trackobject == NULL);

  /* Make sure the track object is created for a compatible track. */
  g_object_set (source, "supported-formats", GES_TRACK_TYPE_CUSTOM, NULL);
  trackobject =
      ges_timeline_object_create_track_object (GES_TIMELINE_OBJECT (source),
      track);
  fail_unless (trackobject != NULL);

  /* The track holds a reference to the object */
  ASSERT_OBJECT_REFCOUNT (trackobject, "Track Object", 1);

  fail_unless (ges_timeline_object_release_track_object (GES_TIMELINE_OBJECT
          (source), trackobject) == TRUE);

  g_object_unref (source);
  g_object_unref (track);
}

GST_END_TEST;

#define gnl_object_check(gnlobj, start, duration, mstart, mduration, priority, active) { \
  guint64 pstart, pdur, pmstart, pmdur, pprio, pact;			\
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


GST_START_TEST (test_filesource_properties)
{
  GESTrack *track;
  GESTrackObject *trackobject;
  GESTimelineObject *object;

  ges_init ();

  track = ges_track_new (GES_TRACK_TYPE_AUDIO, GST_CAPS_ANY);
  fail_unless (track != NULL);

  object = (GESTimelineObject *)
      ges_timeline_filesource_new ((gchar *)
      "crack:///there/is/no/way/this/exists");
  fail_unless (object != NULL);

  /* Set some properties */
  g_object_set (object, "start", (guint64) 42, "duration", (guint64) 51,
      "in-point", (guint64) 12, "supported-formats", GES_TRACK_TYPE_AUDIO,
      NULL);
  assert_equals_uint64 (GES_TIMELINE_OBJECT_START (object), 42);
  assert_equals_uint64 (GES_TIMELINE_OBJECT_DURATION (object), 51);
  assert_equals_uint64 (GES_TIMELINE_OBJECT_INPOINT (object), 12);

  trackobject = ges_timeline_object_create_track_object (object, track);
  fail_unless (trackobject != NULL);
  fail_unless (ges_track_object_set_track (trackobject, track));

  /* Check that trackobject has the same properties */
  assert_equals_uint64 (GES_TRACK_OBJECT_START (trackobject), 42);
  assert_equals_uint64 (GES_TRACK_OBJECT_DURATION (trackobject), 51);
  assert_equals_uint64 (GES_TRACK_OBJECT_INPOINT (trackobject), 12);

  /* And let's also check that it propagated correctly to GNonLin */
  gnl_object_check (trackobject->gnlobject, 42, 51, 12, 51, 0, TRUE);

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
  gnl_object_check (trackobject->gnlobject, 420, 510, 120, 510, 0, TRUE);

  /* Test mute support */
  g_object_set (object, "mute", TRUE, NULL);
  gnl_object_check (trackobject->gnlobject, 420, 510, 120, 510, 0, FALSE);
  g_object_set (object, "mute", FALSE, NULL);
  gnl_object_check (trackobject->gnlobject, 420, 510, 120, 510, 0, TRUE);

  ges_timeline_object_release_track_object (object, trackobject);

  g_object_unref (object);
  g_object_unref (track);
}

GST_END_TEST;



static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges");
  TCase *tc_chain = tcase_create ("filesource");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_filesource_basic);
  tcase_add_test (tc_chain, test_filesource_properties);

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
