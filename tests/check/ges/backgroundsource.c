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

#define DEFAULT_VOLUME 1.0

GST_START_TEST (test_test_source_basic)
{
  GESTestClip *source;

  ges_init ();

  source = ges_test_clip_new ();
  fail_unless (source != NULL);

  gst_object_unref (source);
}

GST_END_TEST;

GST_START_TEST (test_test_source_properties)
{
  GESClip *clip;
  GESTrack *track;
  GESTimeline *timeline;
  GESLayer *layer;
  GESTrackElement *trackelement;

  ges_init ();

  track = ges_track_new (GES_TRACK_TYPE_AUDIO, gst_caps_ref (GST_CAPS_ANY));
  fail_unless (track != NULL);

  layer = ges_layer_new ();
  fail_unless (layer != NULL);

  timeline = ges_timeline_new ();
  fail_unless (timeline != NULL);
  fail_unless (ges_timeline_add_layer (timeline, layer));
  fail_unless (ges_timeline_add_track (timeline, track));

  clip = (GESClip *) ges_test_clip_new ();
  fail_unless (clip != NULL);

  /* Set some properties */
  GST_DEBUG ("Setting start duration and inpoint to %" GST_PTR_FORMAT, clip);
  g_object_set (clip, "start", (guint64) 42, "duration", (guint64) 51,
      "in-point", (guint64) 12, NULL);
  assert_equals_uint64 (_START (clip), 42);
  assert_equals_uint64 (_DURATION (clip), 51);
  assert_equals_uint64 (_INPOINT (clip), 12);

  ges_layer_add_clip (layer, GES_CLIP (clip));
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (clip)), 1);
  trackelement = GES_CONTAINER_CHILDREN (clip)->data;
  fail_unless (trackelement != NULL);
  fail_unless (GES_TIMELINE_ELEMENT_PARENT (trackelement) ==
      GES_TIMELINE_ELEMENT (clip));
  fail_unless (ges_track_element_get_track (trackelement) == track);

  /* Check that trackelement has the same properties */
  assert_equals_uint64 (_START (trackelement), 42);
  assert_equals_uint64 (_DURATION (trackelement), 51);
  assert_equals_uint64 (_INPOINT (trackelement), 12);

  fail_unless (ges_timeline_commit (timeline));
  /* And let's also check that it propagated correctly to GNonLin */
  gnl_object_check (ges_track_element_get_gnlobject (trackelement), 42, 51, 12,
      51, MIN_GNL_PRIO, TRUE);

  /* Change more properties, see if they propagate */
  g_object_set (clip, "start", (guint64) 420, "duration", (guint64) 510,
      "in-point", (guint64) 120, NULL);
  assert_equals_uint64 (_START (clip), 420);
  assert_equals_uint64 (_DURATION (clip), 510);
  assert_equals_uint64 (_INPOINT (clip), 120);
  assert_equals_uint64 (_START (trackelement), 420);
  assert_equals_uint64 (_DURATION (trackelement), 510);
  assert_equals_uint64 (_INPOINT (trackelement), 120);

  fail_unless (ges_timeline_commit (timeline));
  /* And let's also check that it propagated correctly to GNonLin */
  gnl_object_check (ges_track_element_get_gnlobject (trackelement), 420, 510,
      120, 510, MIN_GNL_PRIO + 0, TRUE);

  /* Test mute support */
  g_object_set (clip, "mute", TRUE, NULL);
  fail_unless (ges_timeline_commit (timeline));
  gnl_object_check (ges_track_element_get_gnlobject (trackelement), 420, 510,
      120, 510, MIN_GNL_PRIO + 0, FALSE);
  g_object_set (clip, "mute", FALSE, NULL);
  fail_unless (ges_timeline_commit (timeline));
  gnl_object_check (ges_track_element_get_gnlobject (trackelement), 420, 510,
      120, 510, MIN_GNL_PRIO + 0, TRUE);

  ges_container_remove (GES_CONTAINER (clip),
      GES_TIMELINE_ELEMENT (trackelement));
  gst_object_unref (clip);
}

GST_END_TEST;

GST_START_TEST (test_test_source_in_layer)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESTrack *a, *v;
  GESTrackElement *track_element;
  GESTestClip *source;
  GESVideoTestPattern ptrn;
  gdouble freq, volume;

  ges_init ();

  timeline = ges_timeline_new ();
  layer = ges_layer_new ();
  a = GES_TRACK (ges_audio_track_new ());
  v = GES_TRACK (ges_video_track_new ());

  ges_timeline_add_track (timeline, a);
  ges_timeline_add_track (timeline, v);
  ges_timeline_add_layer (timeline, layer);

  source = ges_test_clip_new_for_nick ((gchar *) "red");
  g_object_get (source, "vpattern", &ptrn, NULL);
  assert_equals_int (ptrn, GES_VIDEO_TEST_PATTERN_RED);

  g_object_set (source, "duration", (guint64) GST_SECOND, NULL);

  ges_layer_add_clip (layer, (GESClip *) source);

  /* specifically test the vpattern property */
  g_object_set (source, "vpattern", (gint) GES_VIDEO_TEST_PATTERN_WHITE, NULL);
  g_object_get (source, "vpattern", &ptrn, NULL);
  assert_equals_int (ptrn, GES_VIDEO_TEST_PATTERN_WHITE);

  track_element =
      ges_clip_find_track_element (GES_CLIP (source), v,
      GES_TYPE_VIDEO_TEST_SOURCE);

  g_assert (GES_IS_VIDEO_TEST_SOURCE (track_element));

  ptrn = (ges_video_test_source_get_pattern ((GESVideoTestSource *)
          track_element));
  assert_equals_int (ptrn, GES_VIDEO_TEST_PATTERN_WHITE);
  gst_object_unref (track_element);

  /* test audio properties as well */

  track_element = ges_clip_find_track_element (GES_CLIP (source),
      a, GES_TYPE_AUDIO_TEST_SOURCE);
  g_assert (GES_IS_AUDIO_TEST_SOURCE (track_element));
  assert_equals_float (ges_test_clip_get_frequency (source), 440);
  assert_equals_float (ges_test_clip_get_volume (source), DEFAULT_VOLUME);

  g_object_get (source, "freq", &freq, "volume", &volume, NULL);
  assert_equals_float (freq, 440);
  assert_equals_float (volume, DEFAULT_VOLUME);


  freq = ges_audio_test_source_get_freq (GES_AUDIO_TEST_SOURCE (track_element));
  volume =
      ges_audio_test_source_get_volume (GES_AUDIO_TEST_SOURCE (track_element));
  g_assert (freq == 440);
  g_assert (volume == DEFAULT_VOLUME);

  g_object_set (source, "freq", (gdouble) 2000, "volume", (gdouble) 0.5, NULL);
  g_object_get (source, "freq", &freq, "volume", &volume, NULL);
  assert_equals_float (freq, 2000);
  assert_equals_float (volume, 0.5);

  freq = ges_audio_test_source_get_freq (GES_AUDIO_TEST_SOURCE (track_element));
  volume =
      ges_audio_test_source_get_volume (GES_AUDIO_TEST_SOURCE (track_element));
  g_assert (freq == 2000);
  g_assert (volume == 0.5);

  gst_object_unref (track_element);

  ges_layer_remove_clip (layer, (GESClip *) source);

  GST_DEBUG ("removing the layer");

  gst_object_unref (timeline);
}

GST_END_TEST;

#if 0
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
  GList *tmp;
  GESTrack *track;
  GESTimeline *timeline;
  GstElement *composition;
  GESLayer *layer;
  GESClip *clip, *clip1, *clip2;

  GstElement *gnlsrc, *gnlsrc1, *gap = NULL;
  GESTrackElement *trackelement, *trackelement1, *trackelement2;

  ges_init ();

  track = GES_TRACK (ges_audio_track_new ());
  fail_unless (track != NULL);

  composition = find_composition (track);
  fail_unless (composition != NULL);

  layer = ges_layer_new ();
  fail_unless (layer != NULL);

  timeline = ges_timeline_new ();
  fail_unless (timeline != NULL);
  fail_unless (ges_timeline_add_layer (timeline, layer));
  fail_unless (ges_timeline_add_track (timeline, track));

  clip = GES_CLIP (ges_test_clip_new ());
  fail_unless (clip != NULL);

  /* Set some properties */
  g_object_set (clip, "start", (guint64) 0, "duration", (guint64) 5, NULL);
  assert_equals_uint64 (_START (clip), 0);
  assert_equals_uint64 (_DURATION (clip), 5);

  ges_layer_add_clip (layer, GES_CLIP (clip));
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (clip)), 1);
  trackelement = GES_CONTAINER_CHILDREN (clip)->data;
  fail_unless (trackelement != NULL);
  fail_unless (ges_track_element_get_track (trackelement) == track);

  gnlsrc = ges_track_element_get_gnlobject (trackelement);
  fail_unless (gnlsrc != NULL);

  /* Check that trackelement has the same properties */
  assert_equals_uint64 (_START (trackelement), 0);
  assert_equals_uint64 (_DURATION (trackelement), 5);

  /* Check no gap were wrongly added
   * 2: 1 for the trackelement and 1 for the mixer */
  assert_equals_int (g_list_length (GST_BIN_CHILDREN (composition)), 2);

  clip1 = GES_CLIP (ges_test_clip_new ());
  fail_unless (clip1 != NULL);

  g_object_set (clip1, "start", (guint64) 15, "duration", (guint64) 5, NULL);
  assert_equals_uint64 (_START (clip1), 15);
  assert_equals_uint64 (_DURATION (clip1), 5);

  ges_layer_add_clip (layer, GES_CLIP (clip1));
  ges_timeline_commit (timeline);
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (clip1)), 1);
  trackelement1 = GES_CONTAINER_CHILDREN (clip1)->data;
  fail_unless (trackelement1 != NULL);
  fail_unless (ges_track_element_get_track (trackelement1) == track);
  gnlsrc1 = ges_track_element_get_gnlobject (trackelement1);
  fail_unless (gnlsrc1 != NULL);

  /* Check that trackelement1 has the same properties */
  assert_equals_uint64 (_START (trackelement1), 15);
  assert_equals_uint64 (_DURATION (trackelement1), 5);

  /* Check the gap as properly been added */
  assert_equals_int (g_list_length (GST_BIN_CHILDREN (composition)), 4);

  for (tmp = GST_BIN_CHILDREN (composition); tmp; tmp = tmp->next) {
    guint prio;
    GstElement *tmp_gnlobj = GST_ELEMENT (tmp->data);

    g_object_get (tmp_gnlobj, "priority", &prio, NULL);
    if (tmp_gnlobj != gnlsrc && tmp_gnlobj != gnlsrc1 && prio == 1) {
      gap = tmp_gnlobj;
    }
  }
  fail_unless (gap != NULL);
  gap_object_check (gap, 5, 10, 1);

  clip2 = GES_CLIP (ges_test_clip_new ());
  fail_unless (clip2 != NULL);
  g_object_set (clip2, "start", (guint64) 35, "duration", (guint64) 5, NULL);
  ges_layer_add_clip (layer, GES_CLIP (clip2));
  fail_unless (ges_timeline_commit (timeline));
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (clip2)), 1);
  trackelement2 = GES_CONTAINER_CHILDREN (clip2)->data;
  fail_unless (trackelement2 != NULL);
  fail_unless (ges_track_element_get_track (trackelement2) == track);
  assert_equals_uint64 (_START (trackelement2), 35);
  assert_equals_uint64 (_DURATION (trackelement2), 5);
  assert_equals_int (g_list_length (GST_BIN_CHILDREN (composition)), 6);

  gst_object_unref (timeline);
}

GST_END_TEST;

GST_START_TEST (test_gap_filling_empty_track)
{
  GESAsset *asset;
  GESTrack *track;
  GESTimeline *timeline;
  GstElement *gap;
  GstElement *composition;
  GESLayer *layer;
  GESClip *clip;

  ges_init ();

  track = GES_TRACK (ges_audio_track_new ());

  layer = ges_layer_new ();
  timeline = ges_timeline_new ();
  fail_unless (timeline != NULL);
  fail_unless (ges_timeline_add_layer (timeline, layer));
  fail_unless (ges_timeline_add_track (timeline, track));
  fail_unless (ges_timeline_add_track (timeline,
          GES_TRACK (ges_video_track_new ())));

  /* Set some properties */
  asset = ges_asset_request (GES_TYPE_TEST_CLIP, NULL, NULL);
  clip = ges_layer_add_asset (layer, asset, 0, 0, 10, GES_TRACK_TYPE_VIDEO);
  ges_timeline_commit (timeline);
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (clip)), 1);

  /* We should not have created any TrackElement in the audio track */
  fail_unless (ges_track_get_elements (track) == NULL);

  /* Check that a gap was properly added */
  composition = find_composition (track);
  /* We also have an adder in that composition */
  assert_equals_int (g_list_length (GST_BIN_CHILDREN (composition)), 2);

  gap = GST_BIN_CHILDREN (composition)->data;
  fail_unless (gap != NULL);
  gap_object_check (gap, 0, 10, 1);
  fail_unless (ges_timeline_commit (timeline));

  gst_object_unref (timeline);
}

GST_END_TEST;
#endif

static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges-backgroundsource");
  TCase *tc_chain = tcase_create ("backgroundsource");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_test_source_basic);
  tcase_add_test (tc_chain, test_test_source_properties);
  tcase_add_test (tc_chain, test_test_source_in_layer);

#if 0
  tcase_add_test (tc_chain, test_gap_filling_basic);
  tcase_add_test (tc_chain, test_gap_filling_empty_track);
#endif

  return s;
}

GST_CHECK_MAIN (ges);
