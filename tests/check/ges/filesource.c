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

  source = ges_timeline_filesource_new (TEST_URI);
  fail_unless (source != NULL);

  /* Make sure the object was properly set */
  g_object_get (source, "uri", &uri, NULL);
  fail_unless (g_ascii_strcasecmp (uri, TEST_URI) == 0);
  g_free (uri);

  trackobject =
      ges_timeline_object_create_track_object (GES_TIMELINE_OBJECT (source),
      track);
  fail_unless (trackobject != NULL);

  /* The track holds a reference to the object */
  ASSERT_OBJECT_REFCOUNT (trackobject, "Track Object", 2);

  fail_unless (ges_timeline_object_release_track_object (GES_TIMELINE_OBJECT
          (source), trackobject) == TRUE);

  ASSERT_OBJECT_REFCOUNT (trackobject, "Track Object", 1);

  g_object_unref (source);
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
