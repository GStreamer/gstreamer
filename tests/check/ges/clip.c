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
    GESTrackElement * trobject, GstElement * gnlobj, gpointer user_data)
{
  GstElement *src;

  GST_DEBUG ("timelineobj:%p, trackelementec:%p, gnlobj:%p",
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
  GESTrackElement *trackelement;
  GESClip *object;

  ges_init ();

  track = ges_track_new (GES_TRACK_TYPE_CUSTOM, gst_caps_ref (GST_CAPS_ANY));
  fail_unless (track != NULL);

  object = (GESClip *) ges_custom_source_clip_new (my_fill_track_func, NULL);
  fail_unless (object != NULL);

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


  /* This time, we move the trackelement to see if the changes move
   * along to the parent and the gnonlin object */
  g_object_set (trackelement, "start", (guint64) 400, NULL);
  assert_equals_uint64 (_START (object), 400);
  assert_equals_uint64 (_START (trackelement), 400);
  gnl_object_check (ges_track_element_get_gnlobject (trackelement), 400, 510,
      120, 510, 0, TRUE);

  ges_clip_release_track_element (object, trackelement);

  g_object_unref (object);
  g_object_unref (track);
}

GST_END_TEST;

GST_START_TEST (test_object_properties_unlocked)
{
  GESTrack *track;
  GESTrackElement *trackelement;
  GESClip *object;

  ges_init ();

  track = ges_track_new (GES_TRACK_TYPE_CUSTOM, gst_caps_ref (GST_CAPS_ANY));
  fail_unless (track != NULL);

  object = (GESClip *) ges_custom_source_clip_new (my_fill_track_func, NULL);
  fail_unless (object != NULL);

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

  /* This time we unlock the trackelement and make sure it doesn't propagate */
  ges_track_element_set_locked (trackelement, FALSE);

  /* Change more properties, they will be set on the GESClip */
  g_object_set (object, "start", (guint64) 420, "duration", (guint64) 510,
      "in-point", (guint64) 120, NULL);
  assert_equals_uint64 (_START (object), 420);
  assert_equals_uint64 (_DURATION (object), 510);
  assert_equals_uint64 (_INPOINT (object), 120);
  /* ... but not on the GESTrackElement since it was unlocked... */
  assert_equals_uint64 (_START (trackelement), 42);
  assert_equals_uint64 (_DURATION (trackelement), 51);
  assert_equals_uint64 (_INPOINT (trackelement), 12);
  /* ... and neither on the GNonLin object */
  gnl_object_check (ges_track_element_get_gnlobject (trackelement), 42, 51, 12,
      51, 0, TRUE);

  /* When unlocked, moving the GESTrackElement won't move the GESClip
   * either */
  /* This time, we move the trackelement to see if the changes move
   * along to the parent and the gnonlin object */
  g_object_set (trackelement, "start", (guint64) 400, NULL);
  assert_equals_uint64 (_START (object), 420);
  assert_equals_uint64 (_START (trackelement), 400);
  gnl_object_check (ges_track_element_get_gnlobject (trackelement), 400, 51, 12,
      51, 0, TRUE);


  ges_clip_release_track_element (object, trackelement);

  g_object_unref (object);
  g_object_unref (track);
}

GST_END_TEST;

GST_START_TEST (test_split_object)
{
  GESTrack *track;
  GESTrackElement *trackelement, *splittrackelement;
  GESClip *object, *splitobj;
  GList *splittrackelements;

  ges_init ();

  track = ges_track_new (GES_TRACK_TYPE_CUSTOM, gst_caps_ref (GST_CAPS_ANY));
  fail_unless (track != NULL);

  object = (GESClip *) ges_custom_source_clip_new (my_fill_track_func, NULL);
  fail_unless (object != NULL);

  /* Set some properties */
  g_object_set (object, "start", (guint64) 42, "duration", (guint64) 50,
      "in-point", (guint64) 12, NULL);
  assert_equals_uint64 (_START (object), 42);
  assert_equals_uint64 (_DURATION (object), 50);
  assert_equals_uint64 (_INPOINT (object), 12);

  trackelement = ges_clip_create_track_element (object, track->type);
  ges_clip_add_track_element (object, trackelement);
  fail_unless (trackelement != NULL);
  fail_unless (ges_track_element_set_track (trackelement, track));

  /* Check that trackelement has the same properties */
  assert_equals_uint64 (_START (trackelement), 42);
  assert_equals_uint64 (_DURATION (trackelement), 50);
  assert_equals_uint64 (_INPOINT (trackelement), 12);

  /* And let's also check that it propagated correctly to GNonLin */
  gnl_object_check (ges_track_element_get_gnlobject (trackelement), 42, 50, 12,
      50, 0, TRUE);

  splitobj = ges_clip_split (object, 67);
  fail_unless (GES_IS_CLIP (splitobj));

  assert_equals_uint64 (_START (object), 42);
  assert_equals_uint64 (_DURATION (object), 25);
  assert_equals_uint64 (_INPOINT (object), 12);

  assert_equals_uint64 (_START (splitobj), 67);
  assert_equals_uint64 (_DURATION (splitobj), 25);
  assert_equals_uint64 (_INPOINT (splitobj), 37);

  splittrackelements = ges_clip_get_track_elements (splitobj);
  fail_unless_equals_int (g_list_length (splittrackelements), 1);

  splittrackelement = GES_TRACK_ELEMENT (splittrackelements->data);
  fail_unless (GES_IS_TRACK_ELEMENT (splittrackelement));
  assert_equals_uint64 (_START (splittrackelement), 67);
  assert_equals_uint64 (_DURATION (splittrackelement), 25);
  assert_equals_uint64 (_INPOINT (splittrackelement), 37);

  fail_unless (splittrackelement != trackelement);
  fail_unless (splitobj != object);

  /* We own the only ref */
  ASSERT_OBJECT_REFCOUNT (splitobj, "splitobj", 1);
  /* 1 ref for the Clip, 1 ref for the Track and 1 in splittrackelements */
  ASSERT_OBJECT_REFCOUNT (splittrackelement, "splittrackelement", 3);

  g_object_unref (track);
  g_object_unref (splitobj);
  g_object_unref (object);

  ASSERT_OBJECT_REFCOUNT (splittrackelement, "splittrackelement", 1);
  g_list_free_full (splittrackelements, g_object_unref);
}

GST_END_TEST;

static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges-clip");
  TCase *tc_chain = tcase_create ("clip");

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
