/* GStreamer Editing Services
 * Copyright (C) 2010 Brandon Lewis <brandon.lewis@collabora.co.uk>
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

GST_START_TEST (test_test_source_basic)
{
  GESTestClip *source;

  ges_init ();

  source = ges_test_clip_new ();
  fail_unless (source != NULL);

  g_object_unref (source);
}

GST_END_TEST;

GST_START_TEST (test_test_source_properties)
{
  GESTrack *track;
  GESTrackElement *trackelement;
  GESClip *object;

  ges_init ();

  track = ges_track_new (GES_TRACK_TYPE_AUDIO, gst_caps_ref (GST_CAPS_ANY));
  fail_unless (track != NULL);

  object = (GESClip *)
      ges_test_clip_new ();
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

  /* Test mute support */
  g_object_set (object, "mute", TRUE, NULL);
  gnl_object_check (ges_track_element_get_gnlobject (trackelement), 420, 510,
      120, 510, 0, FALSE);
  g_object_set (object, "mute", FALSE, NULL);
  gnl_object_check (ges_track_element_get_gnlobject (trackelement), 420, 510,
      120, 510, 0, TRUE);

  ges_clip_release_track_element (object, trackelement);
  g_object_unref (object);
}

GST_END_TEST;

GST_START_TEST (test_test_source_in_layer)
{
  GESTimeline *timeline;
  GESTimelineLayer *layer;
  GESTrack *a, *v;
  GESTrackElement *trobj;
  GESTestClip *source;
  GESVideoTestPattern ptrn;
  gdouble freq, volume;

  ges_init ();

  timeline = ges_timeline_new ();
  layer = (GESTimelineLayer *) ges_simple_timeline_layer_new ();
  a = ges_track_audio_raw_new ();
  v = ges_track_video_raw_new ();

  ges_timeline_add_track (timeline, a);
  ges_timeline_add_track (timeline, v);
  ges_timeline_add_layer (timeline, layer);

  source = ges_test_clip_new_for_nick ((gchar *) "red");
  g_object_get (source, "vpattern", &ptrn, NULL);
  assert_equals_int (ptrn, GES_VIDEO_TEST_PATTERN_RED);

  g_object_set (source, "duration", (guint64) GST_SECOND, NULL);

  ges_simple_timeline_layer_add_object ((GESSimpleTimelineLayer *) layer,
      (GESClip *) source, 0);

  /* specifically test the vpattern property */
  g_object_set (source, "vpattern", (gint) GES_VIDEO_TEST_PATTERN_WHITE, NULL);
  g_object_get (source, "vpattern", &ptrn, NULL);
  assert_equals_int (ptrn, GES_VIDEO_TEST_PATTERN_WHITE);

  trobj =
      ges_clip_find_track_element (GES_CLIP (source), v,
      GES_TYPE_VIDEO_TEST_SOURCE);

  g_assert (GES_IS_VIDEO_TEST_SOURCE (trobj));

  ptrn = (ges_video_test_source_get_pattern ((GESVideoTestSource *)
          trobj));
  assert_equals_int (ptrn, GES_VIDEO_TEST_PATTERN_WHITE);
  g_object_unref (trobj);

  /* test audio properties as well */

  trobj = ges_clip_find_track_element (GES_CLIP (source),
      a, GES_TYPE_AUDIO_TEST_SOURCE);
  g_assert (GES_IS_AUDIO_TEST_SOURCE (trobj));
  assert_equals_float (ges_test_clip_get_frequency (source), 440);
  assert_equals_float (ges_test_clip_get_volume (source), 0);

  g_object_get (source, "freq", &freq, "volume", &volume, NULL);
  assert_equals_float (freq, 440);
  assert_equals_float (volume, 0);


  freq = ges_audio_test_source_get_freq (GES_AUDIO_TEST_SOURCE (trobj));
  volume = ges_audio_test_source_get_volume (GES_AUDIO_TEST_SOURCE (trobj));
  g_assert (freq == 440);
  g_assert (volume == 0);


  g_object_set (source, "freq", (gdouble) 2000, "volume", (gdouble) 0.5, NULL);

  g_object_get (source, "freq", &freq, "volume", &volume, NULL);
  assert_equals_float (freq, 2000);
  assert_equals_float (volume, 0.5);

  freq = ges_audio_test_source_get_freq (GES_AUDIO_TEST_SOURCE (trobj));
  volume = ges_audio_test_source_get_volume (GES_AUDIO_TEST_SOURCE (trobj));
  g_assert (freq == 2000);
  g_assert (volume == 0.5);

  g_object_unref (trobj);

  ges_timeline_layer_remove_object (layer, (GESClip *) source);

  GST_DEBUG ("removing the layer");

  g_object_unref (timeline);
}

GST_END_TEST;

static gint
find_composition_func (const GValue * velement)
{
  GstElement *element = g_value_get_object (velement);
  GstElementFactory *fac = gst_element_get_factory (element);
  const gchar *name = gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (fac));

  if (g_strcmp0 (name, "gnlcomposition") == 0)
    return 0;

  return 1;
}

static GstElement *
find_composition (GESTrack * track)
{
  GstIterator *it = gst_bin_iterate_recurse (GST_BIN (track));
  GValue val = { 0, };
  GstElement *ret = NULL;

  if (gst_iterator_find_custom (it, (GCompareFunc) find_composition_func, &val,
          NULL))
    ret = g_value_get_object (&val);

  g_value_unset (&val);
  gst_iterator_free (it);

  return ret;
}

#define gap_object_check(gnlobj, start, duration, priority)  \
{                                                            \
  guint64 pstart, pdur, pprio;                               \
  g_object_get (gnlobj, "start", &pstart, "duration", &pdur, \
    "priority", &pprio, NULL);                               \
  assert_equals_uint64 (pstart, start);                      \
  assert_equals_uint64 (pdur, duration);                     \
  assert_equals_int (pprio, priority);                       \
}
GST_START_TEST (test_gap_filling_basic)
{
  GESTrack *track;
  GESTrackElement *trackelement, *trackelement1, *trackelement2;
  /*GESTimelineLayer *layer; */
  GESClip *object, *object1, *object2;
  GstElement *gnlsrc, *gnlsrc1, *gap = NULL;
  GstElement *composition;
  GList *tmp;

  ges_init ();

  track = ges_track_audio_raw_new ();
  fail_unless (track != NULL);

  composition = find_composition (track);
  fail_unless (composition != NULL);

  object = GES_CLIP (ges_test_clip_new ());
  fail_unless (object != NULL);

  /* Set some properties */
  g_object_set (object, "start", (guint64) 0, "duration", (guint64) 5, NULL);
  assert_equals_uint64 (_START (object), 0);
  assert_equals_uint64 (_DURATION (object), 5);

  trackelement = ges_clip_create_track_element (object, track->type);
  ges_clip_add_track_element (object, trackelement);

  fail_unless (ges_track_add_element (track, trackelement));
  fail_unless (trackelement != NULL);
  gnlsrc = ges_track_element_get_gnlobject (trackelement);
  fail_unless (gnlsrc != NULL);

  /* Check that trackelement has the same properties */
  assert_equals_uint64 (_START (trackelement), 0);
  assert_equals_uint64 (_DURATION (trackelement), 5);

  /* Check no gap were wrongly added */
  assert_equals_int (g_list_length (GST_BIN_CHILDREN (composition)), 1);

  object1 = GES_CLIP (ges_test_clip_new ());
  fail_unless (object1 != NULL);

  g_object_set (object1, "start", (guint64) 15, "duration", (guint64) 5, NULL);
  assert_equals_uint64 (_START (object1), 15);
  assert_equals_uint64 (_DURATION (object1), 5);

  trackelement1 = ges_clip_create_track_element (object1, track->type);
  ges_clip_add_track_element (object1, trackelement1);
  fail_unless (ges_track_add_element (track, trackelement1));
  fail_unless (trackelement1 != NULL);
  gnlsrc1 = ges_track_element_get_gnlobject (trackelement1);
  fail_unless (gnlsrc1 != NULL);

  /* Check that trackelement1 has the same properties */
  assert_equals_uint64 (_START (trackelement1), 15);
  assert_equals_uint64 (_DURATION (trackelement1), 5);

  /* Check the gap as properly been added */
  assert_equals_int (g_list_length (GST_BIN_CHILDREN (composition)), 3);

  for (tmp = GST_BIN_CHILDREN (composition); tmp; tmp = tmp->next) {
    GstElement *tmp_gnlobj = GST_ELEMENT (tmp->data);

    if (tmp_gnlobj != gnlsrc && tmp_gnlobj != gnlsrc1) {
      gap = tmp_gnlobj;
    }
  }
  fail_unless (gap != NULL);
  gap_object_check (gap, 5, 10, 0)

      object2 = GES_CLIP (ges_test_clip_new ());
  fail_unless (object2 != NULL);
  g_object_set (object2, "start", (guint64) 35, "duration", (guint64) 5, NULL);
  trackelement2 = ges_clip_create_track_element (object2, track->type);
  ges_clip_add_track_element (object2, trackelement2);
  fail_unless (ges_track_add_element (track, trackelement2));
  fail_unless (trackelement2 != NULL);
  assert_equals_uint64 (_START (trackelement2), 35);
  assert_equals_uint64 (_DURATION (trackelement2), 5);
  assert_equals_int (g_list_length (GST_BIN_CHILDREN (composition)), 5);

  gst_object_unref (track);
}

GST_END_TEST;

static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges-backgroundsource");
  TCase *tc_chain = tcase_create ("backgroundsource");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_test_source_basic);
  tcase_add_test (tc_chain, test_test_source_properties);
  tcase_add_test (tc_chain, test_test_source_in_layer);
  tcase_add_test (tc_chain, test_gap_filling_basic);

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
