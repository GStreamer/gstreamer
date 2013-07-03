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
my_fill_track_func (GESClip * clip,
    GESTrackElement * track_element, GstElement * gnlobj, gpointer user_data)
{
  GstElement *src;

  GST_DEBUG ("timelineobj:%p, trackelementec:%p, gnlobj:%p",
      clip, track_element, gnlobj);

  /* Let's just put a fakesource in for the time being */
  src = gst_element_factory_make ("fakesrc", NULL);
  /* If this fails... that means that there already was something
   * in it */
  fail_unless (gst_bin_add (GST_BIN (gnlobj), src));

  return TRUE;
}

GST_START_TEST (test_object_properties)
{
  GESClip *clip;
  GESTrack *track;
  GESTimeline *timeline;
  GESLayer *layer;
  GESTrackElement *trackelement;

  ges_init ();

  track = ges_track_new (GES_TRACK_TYPE_CUSTOM, gst_caps_ref (GST_CAPS_ANY));
  fail_unless (track != NULL);

  layer = ges_layer_new ();
  fail_unless (layer != NULL);
  timeline = ges_timeline_new ();
  fail_unless (timeline != NULL);
  fail_unless (ges_timeline_add_layer (timeline, layer));
  fail_unless (ges_timeline_add_track (timeline, track));

  clip = (GESClip *) ges_custom_source_clip_new (my_fill_track_func, NULL);
  fail_unless (clip != NULL);

  /* Set some properties */
  g_object_set (clip, "start", (guint64) 42, "duration", (guint64) 51,
      "in-point", (guint64) 12, NULL);
  assert_equals_uint64 (_START (clip), 42);
  assert_equals_uint64 (_DURATION (clip), 51);
  assert_equals_uint64 (_INPOINT (clip), 12);

  ges_layer_add_clip (layer, GES_CLIP (clip));
  ges_timeline_commit (timeline);
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

  /* And let's also check that it propagated correctly to GNonLin */
  ges_timeline_commit (timeline);
  gnl_object_check (ges_track_element_get_gnlobject (trackelement), 420, 510,
      120, 510, MIN_GNL_PRIO + 0, TRUE);


  /* This time, we move the trackelement to see if the changes move
   * along to the parent and the gnonlin clip */
  g_object_set (trackelement, "start", (guint64) 400, NULL);
  ges_timeline_commit (timeline);
  assert_equals_uint64 (_START (clip), 400);
  assert_equals_uint64 (_START (trackelement), 400);
  gnl_object_check (ges_track_element_get_gnlobject (trackelement), 400, 510,
      120, 510, MIN_GNL_PRIO + 0, TRUE);

  ges_container_remove (GES_CONTAINER (clip),
      GES_TIMELINE_ELEMENT (trackelement));

  gst_object_unref (timeline);
}

GST_END_TEST;

GST_START_TEST (test_split_object)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESClip *clip, *splitclip;
  GList *splittrackelements;
  GESTrackElement *trackelement, *splittrackelement;

  ges_init ();

  layer = ges_layer_new ();
  fail_unless (layer != NULL);
  timeline = ges_timeline_new_audio_video ();
  fail_unless (timeline != NULL);
  fail_unless (ges_timeline_add_layer (timeline, layer));
  ASSERT_OBJECT_REFCOUNT (timeline, "timeline", 1);

  clip = GES_CLIP (ges_test_clip_new ());
  fail_unless (clip != NULL);
  ASSERT_OBJECT_REFCOUNT (timeline, "timeline", 1);

  /* Set some properties */
  g_object_set (clip, "start", (guint64) 42, "duration", (guint64) 50,
      "in-point", (guint64) 12, NULL);
  ASSERT_OBJECT_REFCOUNT (timeline, "timeline", 1);
  assert_equals_uint64 (_START (clip), 42);
  assert_equals_uint64 (_DURATION (clip), 50);
  assert_equals_uint64 (_INPOINT (clip), 12);

  ges_layer_add_clip (layer, GES_CLIP (clip));
  ges_timeline_commit (timeline);
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (clip)), 2);
  trackelement = GES_CONTAINER_CHILDREN (clip)->data;
  fail_unless (trackelement != NULL);
  fail_unless (GES_TIMELINE_ELEMENT_PARENT (trackelement) ==
      GES_TIMELINE_ELEMENT (clip));

  /* Check that trackelement has the same properties */
  assert_equals_uint64 (_START (trackelement), 42);
  assert_equals_uint64 (_DURATION (trackelement), 50);
  assert_equals_uint64 (_INPOINT (trackelement), 12);

  /* And let's also check that it propagated correctly to GNonLin */
  gnl_object_check (ges_track_element_get_gnlobject (trackelement), 42, 50, 12,
      50, MIN_GNL_PRIO, TRUE);

  splitclip = ges_clip_split (clip, 67);
  fail_unless (GES_IS_CLIP (splitclip));

  assert_equals_uint64 (_START (clip), 42);
  assert_equals_uint64 (_DURATION (clip), 25);
  assert_equals_uint64 (_INPOINT (clip), 12);

  assert_equals_uint64 (_START (splitclip), 67);
  assert_equals_uint64 (_DURATION (splitclip), 25);
  assert_equals_uint64 (_INPOINT (splitclip), 37);

  splittrackelements = GES_CONTAINER_CHILDREN (splitclip);
  fail_unless_equals_int (g_list_length (splittrackelements), 2);

  splittrackelement = GES_TRACK_ELEMENT (splittrackelements->data);
  fail_unless (GES_IS_TRACK_ELEMENT (splittrackelement));
  assert_equals_uint64 (_START (splittrackelement), 67);
  assert_equals_uint64 (_DURATION (splittrackelement), 25);
  assert_equals_uint64 (_INPOINT (splittrackelement), 37);

  fail_unless (splittrackelement != trackelement);
  fail_unless (splitclip != clip);

  splittrackelement = GES_TRACK_ELEMENT (splittrackelements->next->data);
  fail_unless (GES_IS_TRACK_ELEMENT (splittrackelement));
  assert_equals_uint64 (_START (splittrackelement), 67);
  assert_equals_uint64 (_DURATION (splittrackelement), 25);
  assert_equals_uint64 (_INPOINT (splittrackelement), 37);

  fail_unless (splittrackelement != trackelement);
  fail_unless (splitclip != clip);

  /* We own the only ref */
  ASSERT_OBJECT_REFCOUNT (splitclip, "splitclip", 1);
  /* 1 ref for the Clip, 1 ref for the Track and 1 ref for the timeline */
  ASSERT_OBJECT_REFCOUNT (splittrackelement, "splittrackelement", 3);

  check_destroyed (G_OBJECT (timeline), G_OBJECT (splitclip), clip,
      splittrackelement, NULL);
}

GST_END_TEST;

GST_START_TEST (test_clip_group_ungroup)
{
  GESAsset *asset;
  GESTimeline *timeline;
  GESClip *clip, *clip2;
  GList *containers, *tmp;
  GESLayer *layer;
  GESContainer *regrouped_clip;
  GESTrack *audio_track, *video_track;

  ges_init ();

  timeline = ges_timeline_new ();
  layer = ges_layer_new ();
  audio_track = GES_TRACK (ges_audio_track_new ());
  video_track = GES_TRACK (ges_video_track_new ());

  fail_unless (ges_timeline_add_track (timeline, audio_track));
  fail_unless (ges_timeline_add_track (timeline, video_track));
  fail_unless (ges_timeline_add_layer (timeline, layer));

  asset = ges_asset_request (GES_TYPE_TEST_CLIP, NULL, NULL);
  assert_is_type (asset, GES_TYPE_ASSET);

  clip = ges_layer_add_asset (layer, asset, 0, 0, 10, GES_TRACK_TYPE_UNKNOWN);
  ASSERT_OBJECT_REFCOUNT (clip, "1 layer", 1);
  assert_equals_uint64 (_START (clip), 0);
  assert_equals_uint64 (_INPOINT (clip), 0);
  assert_equals_uint64 (_DURATION (clip), 10);
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (clip)), 2);

  containers = ges_container_ungroup (GES_CONTAINER (clip), FALSE);
  assert_equals_int (g_list_length (containers), 2);
  fail_unless (clip == containers->data);
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (clip)), 1);
  assert_equals_uint64 (_START (clip), 0);
  assert_equals_uint64 (_INPOINT (clip), 0);
  assert_equals_uint64 (_DURATION (clip), 10);
  ASSERT_OBJECT_REFCOUNT (clip, "1 for the layer + 1 in containers list", 2);

  clip2 = containers->next->data;
  fail_if (clip2 == clip);
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (clip2)), 1);
  assert_equals_uint64 (_START (clip2), 0);
  assert_equals_uint64 (_INPOINT (clip2), 0);
  assert_equals_uint64 (_DURATION (clip2), 10);
  ASSERT_OBJECT_REFCOUNT (clip2, "1 for the layer + 1 in containers list", 2);

  tmp = ges_track_get_elements (audio_track);
  assert_equals_int (g_list_length (tmp), 1);
  ASSERT_OBJECT_REFCOUNT (tmp->data, "1 for the track + 1 for the container "
      "+ 1 for the timeline + 1 in tmp list", 4);
  assert_equals_int (ges_track_element_get_track_type (tmp->data),
      GES_TRACK_TYPE_AUDIO);
  assert_equals_int (ges_clip_get_supported_formats (GES_CLIP
          (ges_timeline_element_get_parent (tmp->data))), GES_TRACK_TYPE_AUDIO);
  g_list_free_full (tmp, gst_object_unref);
  tmp = ges_track_get_elements (video_track);
  assert_equals_int (g_list_length (tmp), 1);
  ASSERT_OBJECT_REFCOUNT (tmp->data, "1 for the track + 1 for the container "
      "+ 1 for the timeline + 1 in tmp list", 4);
  assert_equals_int (ges_track_element_get_track_type (tmp->data),
      GES_TRACK_TYPE_VIDEO);
  assert_equals_int (ges_clip_get_supported_formats (GES_CLIP
          (ges_timeline_element_get_parent (tmp->data))), GES_TRACK_TYPE_VIDEO);
  g_list_free_full (tmp, gst_object_unref);

  ges_timeline_element_set_start (GES_TIMELINE_ELEMENT (clip), 10);
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (clip)), 1);
  assert_equals_uint64 (_START (clip), 10);
  assert_equals_uint64 (_INPOINT (clip), 0);
  assert_equals_uint64 (_DURATION (clip), 10);
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (clip2)), 1);
  assert_equals_uint64 (_START (clip2), 0);
  assert_equals_uint64 (_INPOINT (clip2), 0);
  assert_equals_uint64 (_DURATION (clip2), 10);

  regrouped_clip = ges_container_group (containers);
  fail_unless (regrouped_clip == NULL);

  ges_timeline_element_set_start (GES_TIMELINE_ELEMENT (clip), 0);
  regrouped_clip = ges_container_group (containers);
  assert_is_type (regrouped_clip, GES_TYPE_CLIP);
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (regrouped_clip)),
      2);
  assert_equals_int (ges_clip_get_supported_formats (GES_CLIP (regrouped_clip)),
      GES_TRACK_TYPE_VIDEO | GES_TRACK_TYPE_AUDIO);
  g_list_free_full (containers, gst_object_unref);

  GST_DEBUG ("Check clips in the layer");
  tmp = ges_layer_get_clips (layer);
  assert_equals_int (g_list_length (tmp), 1);
  g_list_free_full (tmp, gst_object_unref);

  GST_DEBUG ("Check TrackElement in audio track");
  tmp = ges_track_get_elements (audio_track);
  assert_equals_int (g_list_length (tmp), 1);
  assert_equals_int (ges_track_element_get_track_type (tmp->data),
      GES_TRACK_TYPE_AUDIO);
  fail_unless (GES_CONTAINER (ges_timeline_element_get_parent (tmp->data)) ==
      regrouped_clip);
  g_list_free_full (tmp, gst_object_unref);

  GST_DEBUG ("Check TrackElement in video track");
  tmp = ges_track_get_elements (video_track);
  assert_equals_int (g_list_length (tmp), 1);
  ASSERT_OBJECT_REFCOUNT (tmp->data, "1 for the track + 1 for the container "
      "+ 1 for the timeline + 1 in tmp list", 4);
  assert_equals_int (ges_track_element_get_track_type (tmp->data),
      GES_TRACK_TYPE_VIDEO);
  fail_unless (GES_CONTAINER (ges_timeline_element_get_parent (tmp->data)) ==
      regrouped_clip);
  g_list_free_full (tmp, gst_object_unref);

  gst_object_unref (timeline);
}

GST_END_TEST;


static void
child_removed_cb (GESClip * clip, GESTimelineElement * effect,
    gboolean * called)
{
  ASSERT_OBJECT_REFCOUNT (effect, "Keeping alive ref + emission ref", 2);
  *called = TRUE;
}

GST_START_TEST (test_clip_refcount_remove_child)
{
  GESClip *clip;
  GESTrack *track;
  gboolean called;
  GESTrackElement *effect;

  ges_init ();

  clip = GES_CLIP (ges_test_clip_new ());
  track = GES_TRACK (ges_audio_track_new ());
  effect = GES_TRACK_ELEMENT (ges_effect_new ("identity"));

  fail_unless (ges_track_add_element (track, effect));
  fail_unless (ges_container_add (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (effect)));
  ASSERT_OBJECT_REFCOUNT (effect, "1 for the container + 1 for the track", 2);

  fail_unless (ges_track_remove_element (track, effect));
  ASSERT_OBJECT_REFCOUNT (effect, "1 for the container + 1 for the track", 1);

  g_signal_connect (clip, "child-removed", G_CALLBACK (child_removed_cb),
      &called);
  fail_unless (ges_container_remove (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (effect)));
  fail_unless (called == TRUE);
  fail_if (G_IS_OBJECT (effect));

  check_destroyed (G_OBJECT (track), NULL, NULL);
  check_destroyed (G_OBJECT (clip), NULL, NULL);
}

GST_END_TEST;

static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges-clip");
  TCase *tc_chain = tcase_create ("clip");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_object_properties);
  tcase_add_test (tc_chain, test_split_object);
  tcase_add_test (tc_chain, test_clip_group_ungroup);
  tcase_add_test (tc_chain, test_clip_refcount_remove_child);

  return s;
}

GST_CHECK_MAIN (ges);
