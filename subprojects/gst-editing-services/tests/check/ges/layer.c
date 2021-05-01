/* GStreamer Editing Services
 * Copyright (C) 2010 Edward Hervey <bilboed@bilboed.com>
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

#define LAYER_HEIGHT 1000

GST_START_TEST (test_layer_properties)
{
  GESTimeline *timeline;
  GESLayer *layer, *layer1;
  GESTrack *track;
  GESTrackElement *trackelement;
  GESClip *clip;

  ges_init ();

  /* Timeline and 1 Layer */
  timeline = ges_timeline_new ();

  /* The default priority is 0 */
  fail_unless ((layer = ges_timeline_append_layer (timeline)));
  fail_unless_equals_int (ges_layer_get_priority (layer), 0);

  fail_if (g_object_is_floating (layer));

  fail_unless ((layer1 = ges_timeline_append_layer (timeline)));
  fail_unless_equals_int (ges_layer_get_priority (layer1), 1);

  track = GES_TRACK (ges_video_track_new ());
  fail_unless (track != NULL);
  fail_unless (ges_timeline_add_track (timeline, track));

  clip = (GESClip *) ges_test_clip_new ();
  fail_unless (clip != NULL);

  /* Set some properties */
  g_object_set (clip, "start", (guint64) 42, "duration", (gint64) 51,
      "in-point", (guint64) 12, NULL);
  assert_equals_uint64 (_START (clip), 42);
  assert_equals_uint64 (_DURATION (clip), 51);
  assert_equals_uint64 (_INPOINT (clip), 12);
  assert_equals_uint64 (_PRIORITY (clip), 0);

  /* Add the clip to the timeline */
  fail_unless (g_object_is_floating (clip));
  fail_unless (ges_layer_add_clip (layer, GES_CLIP (clip)));
  fail_if (g_object_is_floating (clip));
  trackelement = ges_clip_find_track_element (clip, track, G_TYPE_NONE);
  fail_unless (trackelement != NULL);

  /* This is not a SimpleLayer, therefore the properties shouldn't have changed */
  assert_equals_uint64 (_START (clip), 42);
  assert_equals_uint64 (_DURATION (clip), 51);
  assert_equals_uint64 (_INPOINT (clip), 12);
  assert_equals_uint64 (_PRIORITY (clip), 1);
  ges_timeline_commit (timeline);
  nle_object_check (ges_track_element_get_nleobject (trackelement), 42, 51, 12,
      51, MIN_NLE_PRIO + TRANSITIONS_HEIGHT, TRUE);

  /* Change the priority of the layer */
  g_object_set (layer, "priority", 1, NULL);
  assert_equals_int (ges_layer_get_priority (layer), 1);
  assert_equals_uint64 (_PRIORITY (clip), 1);
  ges_timeline_commit (timeline);
  nle_object_check (ges_track_element_get_nleobject (trackelement), 42, 51, 12,
      51, LAYER_HEIGHT + MIN_NLE_PRIO + TRANSITIONS_HEIGHT, TRUE);

  /* Change it to an insanely high value */
  g_object_set (layer, "priority", 31, NULL);
  assert_equals_int (ges_layer_get_priority (layer), 31);
  assert_equals_uint64 (_PRIORITY (clip), 1);
  ges_timeline_commit (timeline);
  nle_object_check (ges_track_element_get_nleobject (trackelement), 42, 51, 12,
      51, MIN_NLE_PRIO + TRANSITIONS_HEIGHT + LAYER_HEIGHT * 31, TRUE);

  /* and back to 0 */
  fail_unless (ges_timeline_move_layer (timeline, layer, 0));
  assert_equals_int (ges_layer_get_priority (layer), 0);
  assert_equals_uint64 (_PRIORITY (clip), 1);
  ges_timeline_commit (timeline);
  nle_object_check (ges_track_element_get_nleobject (trackelement), 42, 51, 12,
      51, MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 0, TRUE);

  gst_object_unref (trackelement);
  fail_unless (ges_layer_remove_clip (layer, clip));
  fail_unless (ges_timeline_remove_track (timeline, track));
  fail_unless (ges_timeline_remove_layer (timeline, layer));
  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_layer_priorities)
{
  GESTrack *track;
  GESTimeline *timeline;
  GESLayer *layer1, *layer2, *layer3;
  GESTrackElement *trackelement1, *trackelement2, *trackelement3;
  GESClip *clip1, *clip2, *clip3;
  GstElement *nleobj1, *nleobj2, *nleobj3;
  guint prio1, prio2, prio3;
  GList *objs;

  ges_init ();

  /* Timeline and 3 Layer */
  timeline = ges_timeline_new ();
  fail_unless ((layer1 = ges_timeline_append_layer (timeline)));
  fail_unless ((layer2 = ges_timeline_append_layer (timeline)));
  fail_unless ((layer3 = ges_timeline_append_layer (timeline)));
  fail_unless_equals_int (ges_layer_get_priority (layer1), 0);
  fail_unless_equals_int (ges_layer_get_priority (layer2), 1);
  fail_unless_equals_int (ges_layer_get_priority (layer3), 2);

  track = GES_TRACK (ges_video_track_new ());
  fail_unless (track != NULL);
  fail_unless (ges_timeline_add_track (timeline, track));

  clip1 = GES_CLIP (ges_test_clip_new ());
  clip2 = GES_CLIP (ges_test_clip_new ());
  clip3 = GES_CLIP (ges_test_clip_new ());
  fail_unless (clip1 != NULL);
  fail_unless (clip2 != NULL);
  fail_unless (clip3 != NULL);

  g_object_set (clip1, "start", (guint64) 0, "duration", (gint64) 10, NULL);
  g_object_set (clip2, "start", (guint64) 10, "duration", (gint64) 10, NULL);
  g_object_set (clip3, "start", (guint64) 20, "duration", (gint64) 10, NULL);

  /* Add objects to the timeline */
  fail_unless (ges_layer_add_clip (layer1, clip1));
  trackelement1 = ges_clip_find_track_element (clip1, track, G_TYPE_NONE);
  fail_unless (trackelement1 != NULL);

  fail_unless (ges_layer_add_clip (layer2, clip2));
  trackelement2 = ges_clip_find_track_element (clip2, track, G_TYPE_NONE);
  fail_unless (trackelement2 != NULL);

  fail_unless (ges_layer_add_clip (layer3, clip3));
  trackelement3 = ges_clip_find_track_element (clip3, track, G_TYPE_NONE);
  fail_unless (trackelement3 != NULL);

  ges_timeline_commit (timeline);
  assert_equals_int (_PRIORITY (clip1), 1);
  nleobj1 = ges_track_element_get_nleobject (trackelement1);
  fail_unless (nleobj1 != NULL);
  g_object_get (nleobj1, "priority", &prio1, NULL);
  assert_equals_int (prio1, MIN_NLE_PRIO + TRANSITIONS_HEIGHT);

  assert_equals_int (_PRIORITY (clip2), 1);
  nleobj2 = ges_track_element_get_nleobject (trackelement2);
  fail_unless (nleobj2 != NULL);
  g_object_get (nleobj2, "priority", &prio2, NULL);
  /* clip2 is on the second layer and has a priority of 1 */
  assert_equals_int (prio2, MIN_NLE_PRIO + LAYER_HEIGHT + 1);

  /* We do not take into account users set priorities */
  assert_equals_int (_PRIORITY (clip3), 1);

  nleobj3 = ges_track_element_get_nleobject (trackelement3);
  fail_unless (nleobj3 != NULL);

  /* clip3 is on the third layer and has a priority of LAYER_HEIGHT + 1
   * it priority must have the maximum priority of this layer*/
  g_object_get (nleobj3, "priority", &prio3, NULL);
  assert_equals_int (prio3, 1 + MIN_NLE_PRIO + LAYER_HEIGHT * 2);

  /* Move layers around */
  fail_unless (ges_timeline_move_layer (timeline, layer1, 2));
  ges_timeline_commit (timeline);

  /* And check the new priorities */
  assert_equals_int (ges_layer_get_priority (layer1), 2);
  assert_equals_int (ges_layer_get_priority (layer2), 0);
  assert_equals_int (ges_layer_get_priority (layer3), 1);
  assert_equals_int (_PRIORITY (clip1), 1);
  assert_equals_int (_PRIORITY (clip2), 1);
  assert_equals_int (_PRIORITY (clip3), 1);
  g_object_get (nleobj1, "priority", &prio1, NULL);
  g_object_get (nleobj2, "priority", &prio2, NULL);
  g_object_get (nleobj3, "priority", &prio3, NULL);
  assert_equals_int (prio1,
      2 * LAYER_HEIGHT + MIN_NLE_PRIO + TRANSITIONS_HEIGHT);
  assert_equals_int (prio2, MIN_NLE_PRIO + 1);
  assert_equals_int (prio3, LAYER_HEIGHT + MIN_NLE_PRIO + TRANSITIONS_HEIGHT);

  /* And move objects around */
  fail_unless (ges_clip_move_to_layer (clip2, layer1));
  fail_unless (ges_clip_move_to_layer (clip3, layer1));
  ges_timeline_commit (timeline);

  objs = ges_layer_get_clips (layer1);
  assert_equals_int (g_list_length (objs), 3);
  fail_unless (ges_layer_get_clips (layer2) == NULL);
  fail_unless (ges_layer_get_clips (layer3) == NULL);
  g_list_free_full (objs, gst_object_unref);

  /*  Check their priorities (layer1 priority is now 2) */
  assert_equals_int (_PRIORITY (clip1), 1);
  assert_equals_int (_PRIORITY (clip2), 2);
  assert_equals_int (_PRIORITY (clip3), 3);
  g_object_get (nleobj1, "priority", &prio1, NULL);
  g_object_get (nleobj2, "priority", &prio2, NULL);
  g_object_get (nleobj3, "priority", &prio3, NULL);
  assert_equals_int (prio1,
      2 * LAYER_HEIGHT + MIN_NLE_PRIO + TRANSITIONS_HEIGHT);
  assert_equals_int (prio2,
      2 * LAYER_HEIGHT + 1 + MIN_NLE_PRIO + TRANSITIONS_HEIGHT);
  assert_equals_int (prio3,
      2 * LAYER_HEIGHT + 2 + MIN_NLE_PRIO + TRANSITIONS_HEIGHT);

  gst_object_unref (trackelement1);
  gst_object_unref (trackelement2);
  gst_object_unref (trackelement3);
  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_timeline_auto_transition)
{
  GESAsset *asset;
  GESTimeline *timeline;
  GESLayer *layer, *layer1, *layer2;

  ges_init ();

  asset = ges_asset_request (GES_TYPE_TEST_CLIP, NULL, NULL);
  fail_unless (GES_IS_ASSET (asset));
  gst_object_unref (asset);

  GST_DEBUG ("Create timeline");
  timeline = ges_timeline_new_audio_video ();
  assert_is_type (timeline, GES_TYPE_TIMELINE);

  GST_DEBUG ("Create layers");
  layer = ges_layer_new ();
  assert_is_type (layer, GES_TYPE_LAYER);
  layer1 = ges_layer_new ();
  assert_is_type (layer, GES_TYPE_LAYER);
  layer2 = ges_layer_new ();
  assert_is_type (layer, GES_TYPE_LAYER);

  GST_DEBUG ("Set auto-transition to the layers");
  ges_layer_set_auto_transition (layer, TRUE);
  ges_layer_set_auto_transition (layer1, TRUE);
  ges_layer_set_auto_transition (layer2, TRUE);

  GST_DEBUG ("Add layers to the timeline");
  ges_timeline_add_layer (timeline, layer);
  ges_timeline_add_layer (timeline, layer1);
  ges_timeline_add_layer (timeline, layer2);

  GST_DEBUG ("Check that auto-transition was properly set to the layers");
  fail_unless (ges_layer_get_auto_transition (layer));
  fail_unless (ges_layer_get_auto_transition (layer1));
  fail_unless (ges_layer_get_auto_transition (layer2));

  GST_DEBUG ("Set timeline auto-transition property to FALSE");
  ges_timeline_set_auto_transition (timeline, FALSE);

  GST_DEBUG
      ("Check that layers auto-transition has the same value as timeline");
  fail_if (ges_layer_get_auto_transition (layer));
  fail_if (ges_layer_get_auto_transition (layer1));
  fail_if (ges_layer_get_auto_transition (layer2));

  GST_DEBUG ("Set timeline auto-transition property to TRUE");
  ges_timeline_set_auto_transition (timeline, TRUE);

  GST_DEBUG
      ("Check that layers auto-transition has the same value as timeline");
  fail_unless (ges_layer_get_auto_transition (layer));
  fail_unless (ges_layer_get_auto_transition (layer1));
  fail_unless (ges_layer_get_auto_transition (layer2));

  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_single_layer_automatic_transition)
{
  GESAsset *asset;
  GESTimeline *timeline;
  GList *objects;
  GESClip *transition;
  GESLayer *layer;
  GESTimelineElement *src, *src1, *src2;

  ges_init ();

  asset = ges_asset_request (GES_TYPE_TEST_CLIP, NULL, NULL);
  fail_unless (GES_IS_ASSET (asset));

  GST_DEBUG ("Create timeline");
  timeline = ges_timeline_new_audio_video ();
  assert_is_type (timeline, GES_TYPE_TIMELINE);

  GST_DEBUG ("Create first layer");
  layer = ges_layer_new ();
  assert_is_type (layer, GES_TYPE_LAYER);

  GST_DEBUG ("Add first layer to timeline");
  fail_unless (ges_timeline_add_layer (timeline, layer));

  GST_DEBUG ("Set auto transition to first layer");
  ges_layer_set_auto_transition (layer, TRUE);

  GST_DEBUG ("Check that auto-transition was properly set");
  fail_unless (ges_layer_get_auto_transition (layer));

  GST_DEBUG ("Adding assets to first layer");
  GST_DEBUG ("Adding clip from 0 -- 1000 to first layer");
  src = GES_TIMELINE_ELEMENT (ges_layer_add_asset (layer, asset, 0, 0,
          1000, GES_TRACK_TYPE_UNKNOWN));
  fail_unless (GES_IS_CLIP (src));

  GST_DEBUG ("Adding clip from 500 -- 1000 to first layer");
  src1 = GES_TIMELINE_ELEMENT (ges_layer_add_asset (layer, asset, 500,
          0, 1000, GES_TRACK_TYPE_UNKNOWN));
  fail_unless (GES_IS_CLIP (src1));

  /*
   *        500__transition__1000
   * 0___________src_________1000
   *        500___________src1_________1500
   */
  GST_DEBUG ("Checking src timing values");
  assert_equals_uint64 (_START (src), 0);
  assert_equals_uint64 (_DURATION (src), 1000);
  assert_equals_uint64 (_START (src1), 500);
  assert_equals_uint64 (_DURATION (src1), 1500 - 500);
  ges_timeline_commit (timeline);

  GST_DEBUG ("Checking that a transition has been added");
  objects = ges_layer_get_clips (layer);
  assert_equals_int (g_list_length (objects), 4);
  assert_is_type (objects->data, GES_TYPE_TEST_CLIP);

  transition = objects->next->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  assert_equals_uint64 (_START (transition), 500);
  assert_equals_uint64 (_DURATION (transition), 500);

  transition = objects->next->next->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  assert_equals_uint64 (_START (transition), 500);
  assert_equals_uint64 (_DURATION (transition), 500);
  g_list_free_full (objects, gst_object_unref);
  ASSERT_OBJECT_REFCOUNT (transition, "layer + timeline", 2);

  GST_DEBUG ("Moving first source to 250");
  ges_timeline_element_set_start (src, 250);

  /*
   *           600_____transition______1500
   *           600___________src_________1600
   *        500___________src1_________1500
   */
  GST_DEBUG ("Checking src timing values");
  CHECK_OBJECT_PROPS (src, 250, 0, 1000);
  CHECK_OBJECT_PROPS (src1, 500, 0, 1000);

  objects = ges_layer_get_clips (layer);
  assert_equals_int (g_list_length (objects), 4);
  transition = objects->next->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  CHECK_OBJECT_PROPS (transition, 500, 0, 750);

  transition = objects->next->next->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  CHECK_OBJECT_PROPS (transition, 500, 0, 750);
  g_list_free_full (objects, gst_object_unref);

  fail_if (ges_timeline_element_set_start (src1, 250));

  fail_if (ges_container_edit (GES_CONTAINER (src), NULL, -1,
          GES_EDIT_MODE_TRIM, GES_EDGE_START, 500));
  CHECK_OBJECT_PROPS (src, 250, 0, 1000);
  CHECK_OBJECT_PROPS (src1, 500, 0, 1000);
  fail_if (ges_timeline_element_trim (src, 500));
  CHECK_OBJECT_PROPS (src, 250, 0, 1000);
  CHECK_OBJECT_PROPS (src1, 500, 0, 1000);
  fail_if (ges_timeline_element_trim (src, 750));
  CHECK_OBJECT_PROPS (src, 250, 0, 1000);
  CHECK_OBJECT_PROPS (src1, 500, 0, 1000);
  fail_if (ges_timeline_element_set_start (src, 500));
  CHECK_OBJECT_PROPS (src, 250, 0, 1000);
  CHECK_OBJECT_PROPS (src1, 500, 0, 1000);

  /*
   *           600_____transition______1500
   *           600___________src_________1600
   *        500___________src1_________1500
   */
  ges_timeline_element_set_start (src, 600);
  CHECK_OBJECT_PROPS (src, 600, 0, 1000);
  CHECK_OBJECT_PROPS (src1, 500, 0, 1000);
  objects = ges_layer_get_clips (layer);
  assert_equals_int (g_list_length (objects), 4);
  transition = objects->next->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  CHECK_OBJECT_PROPS (transition, 600, 0, 900);
  g_list_free_full (objects, gst_object_unref);

  GST_DEBUG ("Adding asset to first layer");
  GST_DEBUG ("Adding clip from 1250 -- 1000 to first layer");
  fail_if (ges_layer_add_asset (layer, asset, 1250, 0,
          1000, GES_TRACK_TYPE_UNKNOWN));

  /*
   *                                    1500___________src2________2000
   *                                    1500_trans_1600
   *           600______________src________________1600
   *           600_____transition______1500
   *        500___________src1_________1500
   */
  src2 = GES_TIMELINE_ELEMENT (ges_layer_add_asset (layer, asset, 1500, 0,
          500, GES_TRACK_TYPE_UNKNOWN));
  assert_is_type (src2, GES_TYPE_TEST_CLIP);

  CHECK_OBJECT_PROPS (src, 600, 0, 1000);
  CHECK_OBJECT_PROPS (src1, 500, 0, 1000);
  CHECK_OBJECT_PROPS (src2, 1500, 0, 500);
  objects = ges_layer_get_clips (layer);
  transition = objects->next->next->data;
  assert_equals_int (g_list_length (objects), 7);
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  CHECK_OBJECT_PROPS (transition, 600, 0, 900);
  transition = objects->next->next->next->next->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  CHECK_OBJECT_PROPS (transition, 1500, 0, 100);

  g_list_free_full (objects, gst_object_unref);

  gst_object_unref (timeline);
  gst_object_unref (asset);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_multi_layer_automatic_transition)
{
  GESAsset *asset;
  GESTimeline *timeline;
  GList *objects, *current;
  GESClip *transition;
  GESLayer *layer, *layer1;
  GESTimelineElement *src, *src1, *src2, *src3;

  ges_init ();

  asset = ges_asset_request (GES_TYPE_TEST_CLIP, NULL, NULL);
  fail_unless (GES_IS_ASSET (asset));

  GST_DEBUG ("Create timeline");
  timeline = ges_timeline_new_audio_video ();
  assert_is_type (timeline, GES_TYPE_TIMELINE);

  GST_DEBUG ("Create first layer");
  layer = ges_layer_new ();
  assert_is_type (layer, GES_TYPE_LAYER);

  GST_DEBUG ("Add first layer to timeline");
  fail_unless (ges_timeline_add_layer (timeline, layer));

  GST_DEBUG ("Append a new layer to the timeline");
  layer1 = ges_timeline_append_layer (timeline);
  assert_is_type (layer1, GES_TYPE_LAYER);

  GST_DEBUG ("Set auto transition to first layer");
  ges_layer_set_auto_transition (layer, TRUE);

  GST_DEBUG ("Check that auto-transition was properly set");
  fail_unless (ges_layer_get_auto_transition (layer));
  fail_if (ges_layer_get_auto_transition (layer1));

  GST_DEBUG ("Adding assets to first layer");
  GST_DEBUG ("Adding clip from 0 -- 1000 to first layer");
  src = GES_TIMELINE_ELEMENT (ges_layer_add_asset (layer, asset, 0, 0,
          1000, GES_TRACK_TYPE_UNKNOWN));
  fail_unless (GES_IS_CLIP (src));

  GST_DEBUG ("Adding clip from 500 -- 1000 to first layer");
  src1 = GES_TIMELINE_ELEMENT (ges_layer_add_asset (layer, asset, 500,
          0, 1000, GES_TRACK_TYPE_UNKNOWN));
  ges_timeline_commit (timeline);
  fail_unless (GES_IS_CLIP (src1));

  /*
   *        500__transition__1000
   * 0___________src_________1000
   *        500___________src1_________1500
   */
  GST_DEBUG ("Checking src timing values");
  assert_equals_uint64 (_START (src), 0);
  assert_equals_uint64 (_DURATION (src), 1000);
  assert_equals_uint64 (_START (src1), 500);
  assert_equals_uint64 (_DURATION (src1), 1500 - 500);

  GST_DEBUG ("Checking that a transition has been added");
  current = objects = ges_layer_get_clips (layer);
  assert_equals_int (g_list_length (objects), 4);
  assert_is_type (current->data, GES_TYPE_TEST_CLIP);

  current = current->next;
  transition = current->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  assert_equals_uint64 (_START (transition), 500);
  assert_equals_uint64 (_DURATION (transition), 500);

  current = current->next;
  transition = current->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  assert_equals_uint64 (_START (transition), 500);
  assert_equals_uint64 (_DURATION (transition), 500);
  g_list_free_full (objects, gst_object_unref);
  ASSERT_OBJECT_REFCOUNT (transition, "layer + timeline", 2);

  GST_DEBUG ("Adding clip 2 from 500 -- 1000 to second layer");
  src2 = GES_TIMELINE_ELEMENT (ges_layer_add_asset (layer1, asset, 0,
          0, 1000, GES_TRACK_TYPE_UNKNOWN));
  GST_DEBUG ("Adding clip 3 from 500 -- 1000 to second layer");
  src3 = GES_TIMELINE_ELEMENT (ges_layer_add_asset (layer1, asset, 500,
          0, 1000, GES_TRACK_TYPE_UNKNOWN));
  assert_is_type (src3, GES_TYPE_TEST_CLIP);

  /*        500__transition__1000
   * 0___________src_________1000
   *        500___________src1_________1500
   *----------------------------------------------------
   * 0___________src2_________1000
   *        500___________src3_________1500         Layer1
   */
  GST_DEBUG ("Checking src timing values");
  assert_equals_uint64 (_START (src), 0);
  assert_equals_uint64 (_DURATION (src), 1000);
  assert_equals_uint64 (_START (src1), 500);
  assert_equals_uint64 (_DURATION (src1), 1500 - 500);
  assert_equals_uint64 (_START (src2), 0);
  assert_equals_uint64 (_DURATION (src2), 1000);
  assert_equals_uint64 (_START (src3), 500);
  assert_equals_uint64 (_DURATION (src3), 1500 - 500);

  GST_DEBUG ("Checking transitions on first layer");
  current = objects = ges_layer_get_clips (layer);
  assert_equals_int (g_list_length (objects), 4);
  assert_is_type (current->data, GES_TYPE_TEST_CLIP);

  current = current->next;
  transition = current->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  assert_equals_uint64 (_START (transition), 500);
  assert_equals_uint64 (_DURATION (transition), 500);

  current = current->next;
  transition = current->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  assert_equals_uint64 (_START (transition), 500);
  assert_equals_uint64 (_DURATION (transition), 500);
  g_list_free_full (objects, gst_object_unref);
  ASSERT_OBJECT_REFCOUNT (transition, "layer + timeline", 2);

  GST_DEBUG ("Checking transitions on second layer");
  current = objects = ges_layer_get_clips (layer1);
  assert_equals_int (g_list_length (objects), 2);
  fail_unless (current->data == src2);
  fail_unless (current->next->data == src3);
  g_list_free_full (objects, gst_object_unref);

  GST_DEBUG
      ("Set auto transition to second layer, a new transition should be added");
  ges_layer_set_auto_transition (layer1, TRUE);

  /*        500__transition__1000
   * 0___________src_________1000
   *        500___________src1_________1500
   *----------------------------------------------------
   *        500__transition__1000
   * 0__________src2_________1000
   *        500___________src3_________1500         Layer1
   */
  GST_DEBUG ("Checking src timing values");
  assert_equals_uint64 (_START (src), 0);
  assert_equals_uint64 (_DURATION (src), 1000);
  assert_equals_uint64 (_START (src1), 500);
  assert_equals_uint64 (_DURATION (src1), 1500 - 500);
  assert_equals_uint64 (_START (src2), 0);
  assert_equals_uint64 (_DURATION (src2), 1000);
  assert_equals_uint64 (_START (src3), 500);
  assert_equals_uint64 (_DURATION (src3), 1500 - 500);

  GST_DEBUG ("Checking transitions on first layer");
  current = objects = ges_layer_get_clips (layer);
  assert_equals_int (g_list_length (objects), 4);
  assert_is_type (current->data, GES_TYPE_TEST_CLIP);

  current = current->next;
  transition = current->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  assert_equals_uint64 (_START (transition), 500);
  assert_equals_uint64 (_DURATION (transition), 500);

  current = current->next;
  transition = current->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  assert_equals_uint64 (_START (transition), 500);
  assert_equals_uint64 (_DURATION (transition), 500);
  g_list_free_full (objects, gst_object_unref);
  ASSERT_OBJECT_REFCOUNT (transition, "layer + timeline", 2);

  GST_DEBUG ("Checking transitions has been added on second layer");
  current = objects = ges_layer_get_clips (layer1);
  assert_equals_int (g_list_length (objects), 4);
  assert_is_type (current->data, GES_TYPE_TEST_CLIP);

  current = current->next;
  transition = current->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  assert_equals_uint64 (_START (transition), 500);
  assert_equals_uint64 (_DURATION (transition), 500);

  current = current->next;
  transition = current->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  assert_equals_uint64 (_START (transition), 500);
  assert_equals_uint64 (_DURATION (transition), 500);
  g_list_free_full (objects, gst_object_unref);
  ASSERT_OBJECT_REFCOUNT (transition, "layer + timeline", 2);

  GST_DEBUG ("Moving src3 to 1000. should remove transition");
  ges_timeline_element_set_start (src3, 1000);

  /*        500__transition__1000
   * 0___________src_________1000
   *        500___________src1_________1500                           Layer
   *----------------------------------------------------
   * 0__________src2_________1000
   *                         1000___________src3_________2000         Layer1
   */
  GST_DEBUG ("Checking src timing values");
  assert_equals_uint64 (_START (src), 0);
  assert_equals_uint64 (_DURATION (src), 1000);
  assert_equals_uint64 (_START (src1), 500);
  assert_equals_uint64 (_DURATION (src1), 1500 - 500);
  assert_equals_uint64 (_START (src2), 0);
  assert_equals_uint64 (_DURATION (src2), 1000);
  assert_equals_uint64 (_START (src3), 1000);
  assert_equals_uint64 (_DURATION (src3), 2000 - 1000);

  GST_DEBUG ("Checking transitions on first layer");
  current = objects = ges_layer_get_clips (layer);
  assert_equals_int (g_list_length (objects), 4);
  assert_is_type (current->data, GES_TYPE_TEST_CLIP);

  current = current->next;
  transition = current->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  assert_equals_uint64 (_START (transition), 500);
  assert_equals_uint64 (_DURATION (transition), 500);

  current = current->next;
  transition = current->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  assert_equals_uint64 (_START (transition), 500);
  assert_equals_uint64 (_DURATION (transition), 500);
  g_list_free_full (objects, gst_object_unref);
  ASSERT_OBJECT_REFCOUNT (transition, "layer + timeline", 2);

  GST_DEBUG ("Checking transitions has been removed on second layer");
  current = objects = ges_layer_get_clips (layer1);
  assert_equals_int (g_list_length (objects), 2);
  fail_unless (current->data == src2);
  fail_unless (current->next->data == src3);
  g_list_free_full (objects, gst_object_unref);
  ASSERT_OBJECT_REFCOUNT (transition, "layer + timeline", 2);

  GST_DEBUG ("Moving src3 to first layer, should add a transition");
  ges_clip_move_to_layer (GES_CLIP (src3), layer);

  /*        500__transition__1000
   * 0___________src_________1000
   *        500___________src1_________1500
   *                         1000___________src3_________2000   Layer
   *                         1000__tr__1500
   *----------------------------------------------------
   * 0__________src2_________1000                               Layer1
   */
  GST_DEBUG ("Checking src timing values");
  assert_equals_uint64 (_START (src), 0);
  assert_equals_uint64 (_DURATION (src), 1000);
  assert_equals_uint64 (_START (src1), 500);
  assert_equals_uint64 (_DURATION (src1), 1500 - 500);
  assert_equals_uint64 (_START (src2), 0);
  assert_equals_uint64 (_DURATION (src2), 1000);
  assert_equals_uint64 (_START (src3), 1000);
  assert_equals_uint64 (_DURATION (src3), 2000 - 1000);

  GST_DEBUG ("Checking transitions on first layer");
  current = objects = ges_layer_get_clips (layer);
  assert_equals_int (g_list_length (objects), 7);
  fail_unless (current->data == src);

  current = current->next;
  transition = current->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  assert_equals_uint64 (_START (transition), 500);
  assert_equals_uint64 (_DURATION (transition), 500);

  current = current->next;
  transition = current->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  assert_equals_uint64 (_START (transition), 500);
  assert_equals_uint64 (_DURATION (transition), 500);

  current = current->next;
  fail_unless (current->data == src1);

  current = current->next;
  transition = current->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  assert_equals_uint64 (_START (transition), 1000);
  assert_equals_uint64 (_DURATION (transition), 1500 - 1000);

  current = current->next;
  transition = current->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  assert_equals_uint64 (_START (transition), 1000);
  assert_equals_uint64 (_DURATION (transition), 1500 - 1000);

  current = current->next;
  fail_unless (current->data == src3);

  g_list_free_full (objects, gst_object_unref);
  ASSERT_OBJECT_REFCOUNT (transition, "layer + timeline", 2);

  GST_DEBUG ("Checking second layer");
  current = objects = ges_layer_get_clips (layer1);
  assert_equals_int (g_list_length (objects), 1);
  fail_unless (current->data == src2);
  g_list_free_full (objects, gst_object_unref);
  ASSERT_OBJECT_REFCOUNT (transition, "layer + timeline", 2);

  GST_DEBUG
      ("Moving src to second layer, should remove first transition on first layer");
  fail_if (ges_clip_move_to_layer (GES_CLIP (src), layer1));

  /*        500___________src1_________1500
   *                         1000___________src3_________2000   Layer
   *                         1000__tr__1500
   *----------------------------------------------------
   * 0___________src_________1000
   * 0__________src2_________1000                               Layer1
   */
  GST_DEBUG ("Checking src timing values");
  assert_equals_uint64 (_START (src), 0);
  assert_equals_uint64 (_DURATION (src), 1000);
  assert_equals_uint64 (_START (src1), 500);
  assert_equals_uint64 (_DURATION (src1), 1500 - 500);
  assert_equals_uint64 (_START (src2), 0);
  assert_equals_uint64 (_DURATION (src2), 1000);
  assert_equals_uint64 (_START (src3), 1000);
  assert_equals_uint64 (_DURATION (src3), 2000 - 1000);

  GST_DEBUG ("Checking transitions on first layer");
  current = objects = ges_layer_get_clips (layer);
  assert_equals_int (g_list_length (objects), 7);

  g_list_free_full (objects, gst_object_unref);
  ASSERT_OBJECT_REFCOUNT (transition, "layer + timeline", 2);

  GST_DEBUG ("Edit src to first layer start=1500");
  ges_container_edit (GES_CONTAINER (src), NULL, 0,
      GES_EDIT_MODE_NORMAL, GES_EDGE_NONE, 1500);
  /*                                   1500___________src_________2500
   *                                   1500______tr______2000
   *        500___________src1_________1500                 ^
   *                         1000_________^_src3_________2000   Layer
   *                         1000__tr__1500
   *---------------------------------------------------------------------------
   * 0__________src2_________1000                               Layer1
   */
  GST_DEBUG ("Checking src timing values");
  assert_equals_uint64 (_START (src), 1500);
  assert_equals_uint64 (_DURATION (src), 1000);
  assert_equals_uint64 (_START (src1), 500);
  assert_equals_uint64 (_DURATION (src1), 1500 - 500);
  assert_equals_uint64 (_START (src2), 0);
  assert_equals_uint64 (_DURATION (src2), 1000);
  assert_equals_uint64 (_START (src3), 1000);
  assert_equals_uint64 (_DURATION (src3), 2000 - 1000);

  GST_DEBUG ("Checking transitions on first layer");
  current = objects = ges_layer_get_clips (layer);
  assert_equals_int (g_list_length (objects), 7);
  fail_unless (current->data == src1);

  current = current->next;
  transition = current->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  assert_equals_uint64 (_START (transition), 1000);
  assert_equals_uint64 (_DURATION (transition), 500);

  current = current->next;
  transition = current->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  assert_equals_uint64 (_START (transition), 1000);
  assert_equals_uint64 (_DURATION (transition), 500);

  current = current->next;
  fail_unless (current->data == src3);

  current = current->next;
  transition = current->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  assert_equals_uint64 (_START (transition), 1500);
  assert_equals_uint64 (_DURATION (transition), 500);

  current = current->next;
  transition = current->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  assert_equals_uint64 (_START (transition), 1500);
  assert_equals_uint64 (_DURATION (transition), 500);

  current = current->next;
  fail_unless (current->data == src);
  g_list_free_full (objects, gst_object_unref);
  ASSERT_OBJECT_REFCOUNT (transition, "layer + timeline", 2);

  GST_DEBUG ("Checking second layer");
  current = objects = ges_layer_get_clips (layer1);
  assert_equals_int (g_list_length (objects), 1);
  assert_is_type (current->data, GES_TYPE_TEST_CLIP);
  g_list_free_full (objects, gst_object_unref);
  ASSERT_OBJECT_REFCOUNT (transition, "layer + timeline", 2);

  GST_DEBUG ("Ripple src1 to 700");
  ges_container_edit (GES_CONTAINER (src1), NULL, 0,
      GES_EDIT_MODE_RIPPLE, GES_EDGE_NONE, 700);

  /*                                           1700___________src_________2700
   *                                           1700__tr__2000
   *                700___________src1_________1700
   *                                1200___________src3_________2200   Layer
   *                                1200___tr__1700
   *---------------------------------------------------------------------------
   * 0__________src2_________1000                               Layer1
   */
  GST_DEBUG ("Checking src timing values");
  assert_equals_uint64 (_START (src), 1700);
  assert_equals_uint64 (_DURATION (src), 1000);
  assert_equals_uint64 (_START (src1), 700);
  assert_equals_uint64 (_DURATION (src1), 1700 - 700);
  assert_equals_uint64 (_START (src2), 0);
  assert_equals_uint64 (_DURATION (src2), 1000);
  assert_equals_uint64 (_START (src3), 1200);
  assert_equals_uint64 (_DURATION (src3), 2200 - 1200);

  GST_DEBUG ("Checking transitions on first layer");
  current = objects = ges_layer_get_clips (layer);
  assert_equals_int (g_list_length (objects), 7);
  fail_unless (current->data == src1);

  current = current->next;
  transition = current->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  assert_equals_uint64 (_START (transition), 1200);
  assert_equals_uint64 (_DURATION (transition), 1700 - 1200);

  current = current->next;
  transition = current->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  assert_equals_uint64 (_START (transition), 1200);
  assert_equals_uint64 (_DURATION (transition), 1700 - 1200);

  current = current->next;
  fail_unless (current->data == src3);

  current = current->next;
  transition = current->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  assert_equals_uint64 (_START (transition), 1700);
  assert_equals_uint64 (_DURATION (transition), 2200 - 1700);

  current = current->next;
  transition = current->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  assert_equals_uint64 (_START (transition), 1700);
  assert_equals_uint64 (_DURATION (transition), 2200 - 1700);

  current = current->next;
  fail_unless (current->data == src);
  g_list_free_full (objects, gst_object_unref);
  ASSERT_OBJECT_REFCOUNT (transition, "layer + timeline", 2);

  GST_DEBUG ("Checking second layer");
  current = objects = ges_layer_get_clips (layer1);
  assert_equals_int (g_list_length (objects), 1);
  assert_is_type (current->data, GES_TYPE_TEST_CLIP);
  g_list_free_full (objects, gst_object_unref);
  ASSERT_OBJECT_REFCOUNT (transition, "layer + timeline", 2);

  gst_object_unref (timeline);
  gst_object_unref (asset);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_layer_activate_automatic_transition)
{
  GESAsset *asset, *transition_asset;
  GESTimeline *timeline;
  GESLayer *layer;
  GList *objects, *current;
  GESClip *transition;
  GESTimelineElement *src, *src1, *src2, *src3;

  ges_init ();

  asset = ges_asset_request (GES_TYPE_TEST_CLIP, NULL, NULL);
  transition_asset =
      ges_asset_request (GES_TYPE_TRANSITION_CLIP, "crossfade", NULL);
  fail_unless (GES_IS_ASSET (asset));

  GST_DEBUG ("Create timeline");
  timeline = ges_timeline_new_audio_video ();
  assert_is_type (timeline, GES_TYPE_TIMELINE);

  GST_DEBUG ("Append a layer to the timeline");
  layer = ges_timeline_append_layer (timeline);
  assert_is_type (layer, GES_TYPE_LAYER);

  GST_DEBUG ("Adding clip from 0 -- 1000 to layer");
  src = GES_TIMELINE_ELEMENT (ges_layer_add_asset (layer, asset, 0, 0,
          1000, GES_TRACK_TYPE_UNKNOWN));
  fail_unless (GES_IS_CLIP (src));

  GST_DEBUG ("Adding clip from 500 -- 1000 to first layer");
  src1 = GES_TIMELINE_ELEMENT (ges_layer_add_asset (layer, asset, 500,
          0, 1000, GES_TRACK_TYPE_UNKNOWN));
  fail_unless (GES_IS_CLIP (src1));

  GST_DEBUG ("Adding clip from 1000 -- 2000 to layer");
  src2 = GES_TIMELINE_ELEMENT (ges_layer_add_asset (layer, asset, 1000,
          0, 1000, GES_TRACK_TYPE_UNKNOWN));
  fail_unless (GES_IS_CLIP (src2));

  GST_DEBUG ("Adding clip from 2000 -- 2500 to layer");
  src3 = GES_TIMELINE_ELEMENT (ges_layer_add_asset (layer, asset, 2000,
          0, 500, GES_TRACK_TYPE_UNKNOWN));
  fail_unless (GES_IS_CLIP (src3));

  /*
   * 0___________src_________1000
   *        500___________src1_________1500
   *                         1000____src2_______2000
   *                                            2000_______src2_____2500
   */
  GST_DEBUG ("Checking src timing values");
  assert_equals_uint64 (_START (src), 0);
  assert_equals_uint64 (_DURATION (src), 1000);
  assert_equals_uint64 (_START (src1), 500);
  assert_equals_uint64 (_DURATION (src1), 1500 - 500);
  assert_equals_uint64 (_START (src2), 1000);
  assert_equals_uint64 (_DURATION (src2), 1000);
  assert_equals_uint64 (_START (src3), 2000);
  assert_equals_uint64 (_DURATION (src3), 500);

  GST_DEBUG ("Checking that no transition has been added");
  current = objects = ges_layer_get_clips (layer);
  assert_equals_int (g_list_length (objects), 4);
  assert_is_type (current->data, GES_TYPE_TEST_CLIP);
  g_list_free_full (objects, gst_object_unref);

  GST_DEBUG ("Adding transition from 1000 -- 1500 to layer");
  transition =
      GES_CLIP (ges_layer_add_asset (layer,
          transition_asset, 1000, 0, 500, GES_TRACK_TYPE_VIDEO));
  g_object_unref (transition_asset);
  fail_unless (GES_IS_TRANSITION_CLIP (transition));
  objects = GES_CONTAINER_CHILDREN (transition);
  assert_equals_int (g_list_length (objects), 1);

  GST_DEBUG ("Checking the transitions");
  /*
   * 0___________src_________1000
   *        500___________src1_________1500
   *                         1000__tr__1500 (1 of the 2 tracks only)
   *                         1000____src2_______2000
   *                                            2000_______src3_____2500
   */
  current = objects = ges_layer_get_clips (layer);
  assert_equals_int (g_list_length (objects), 5);
  current = current->next;
  assert_is_type (current->data, GES_TYPE_TEST_CLIP);
  current = current->next;
  assert_is_type (current->data, GES_TYPE_TRANSITION_CLIP);
  current = current->next;
  assert_is_type (current->data, GES_TYPE_TEST_CLIP);
  current = current->next;
  assert_is_type (current->data, GES_TYPE_TEST_CLIP);
  g_list_free_full (objects, gst_object_unref);

  ges_layer_set_auto_transition (layer, TRUE);
  /*
   * 0___________src_________1000
   *        500______tr______1000
   *        500___________src1_________1500
   *                         1000__tr__1500
   *                         1000____src2_______2000
   *                                            2000_______src3_____2500
   */
  current = objects = ges_layer_get_clips (layer);
  assert_equals_int (g_list_length (objects), 8);
  assert_equals_uint64 (_START (src), 0);
  assert_equals_uint64 (_DURATION (src), 1000);
  assert_equals_uint64 (_START (src1), 500);
  assert_equals_uint64 (_DURATION (src1), 1500 - 500);
  assert_equals_uint64 (_START (src2), 1000);
  assert_equals_uint64 (_DURATION (src2), 1000);
  assert_equals_uint64 (_START (src3), 2000);
  assert_equals_uint64 (_DURATION (src3), 500);

  GST_DEBUG ("Checking transitions");
  fail_unless (current->data == src);

  current = current->next;
  transition = current->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  assert_equals_uint64 (_START (transition), 500);
  assert_equals_uint64 (_DURATION (transition), 500);

  current = current->next;
  transition = current->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  assert_equals_uint64 (_START (transition), 500);
  assert_equals_uint64 (_DURATION (transition), 500);

  current = current->next;
  fail_unless (current->data == src1);

  current = current->next;
  transition = current->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  assert_equals_uint64 (_START (transition), 1000);
  assert_equals_uint64 (_DURATION (transition), 500);

  current = current->next;
  transition = current->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  assert_equals_uint64 (_START (transition), 1000);
  assert_equals_uint64 (_DURATION (transition), 500);

  current = current->next;
  fail_unless (current->data == src2);

  current = current->next;
  fail_unless (current->data == src3);
  g_list_free_full (objects, gst_object_unref);
  ASSERT_OBJECT_REFCOUNT (transition, "layer + timeline", 2);

  GST_DEBUG ("Moving src2 to 1200, check everything updates properly");
  ges_timeline_element_set_start (src2, 1200);
  ges_timeline_commit (timeline);
  /*
   * 0___________src_________1000
   *        500______tr______1000
   *        500___________src1_________1500
   *                           1200_tr_1500
   *                           1200____src2_______2200
   *                                          !__tr__^
   *                                          2000_______src3_____2500
   */
  current = objects = ges_layer_get_clips (layer);
  assert_equals_int (g_list_length (objects), 10);
  assert_equals_uint64 (_START (src), 0);
  assert_equals_uint64 (_DURATION (src), 1000);
  assert_equals_uint64 (_START (src1), 500);
  assert_equals_uint64 (_DURATION (src1), 1500 - 500);
  assert_equals_uint64 (_START (src2), 1200);
  assert_equals_uint64 (_DURATION (src2), 1000);
  assert_equals_uint64 (_START (src3), 2000);
  assert_equals_uint64 (_DURATION (src3), 500);

  GST_DEBUG ("Checking transitions");
  fail_unless (current->data == src);

  current = current->next;
  transition = current->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  assert_equals_uint64 (_START (transition), 500);
  assert_equals_uint64 (_DURATION (transition), 500);

  current = current->next;
  transition = current->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  assert_equals_uint64 (_START (transition), 500);
  assert_equals_uint64 (_DURATION (transition), 500);

  current = current->next;
  fail_unless (current->data == src1);

  current = current->next;
  transition = current->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  assert_equals_uint64 (_START (transition), 1200);
  assert_equals_uint64 (_DURATION (transition), 300);

  current = current->next;
  transition = current->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  assert_equals_uint64 (_START (transition), 1200);
  assert_equals_uint64 (_DURATION (transition), 300);

  current = current->next;
  fail_unless (current->data == src2);

  current = current->next;
  transition = current->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  assert_equals_uint64 (_START (transition), 2000);
  assert_equals_uint64 (_DURATION (transition), 200);

  current = current->next;
  transition = current->data;
  assert_is_type (transition, GES_TYPE_TRANSITION_CLIP);
  assert_equals_uint64 (_START (transition), 2000);
  assert_equals_uint64 (_DURATION (transition), 200);

  current = current->next;
  fail_unless (current->data == src3);
  g_list_free_full (objects, gst_object_unref);
  ASSERT_OBJECT_REFCOUNT (transition, "layer + timeline", 2);


  gst_object_unref (timeline);
  gst_object_unref (asset);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_layer_meta_string)
{
  GESTimeline *timeline;
  GESLayer *layer;
  const gchar *result;

  ges_init ();

  timeline = ges_timeline_new_audio_video ();
  layer = ges_layer_new ();
  ges_timeline_add_layer (timeline, layer);

  ges_meta_container_set_string (GES_META_CONTAINER (layer),
      "ges-test", "blub");

  fail_unless ((result = ges_meta_container_get_string (GES_META_CONTAINER
              (layer), "ges-test")) != NULL);

  assert_equals_string (result, "blub");

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_layer_meta_boolean)
{
  GESTimeline *timeline;
  GESLayer *layer;
  gboolean result;

  ges_init ();

  timeline = ges_timeline_new_audio_video ();
  layer = ges_layer_new ();
  ges_timeline_add_layer (timeline, layer);

  ges_meta_container_set_boolean (GES_META_CONTAINER (layer), "ges-test", TRUE);

  fail_unless (ges_meta_container_get_boolean (GES_META_CONTAINER
          (layer), "ges-test", &result));

  fail_unless (result);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_layer_meta_int)
{
  GESTimeline *timeline;
  GESLayer *layer;
  gint result;

  ges_init ();

  timeline = ges_timeline_new_audio_video ();
  layer = ges_layer_new ();
  ges_timeline_add_layer (timeline, layer);

  ges_meta_container_set_int (GES_META_CONTAINER (layer), "ges-test", 1234);

  fail_unless (ges_meta_container_get_int (GES_META_CONTAINER (layer),
          "ges-test", &result));

  assert_equals_int (result, 1234);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_layer_meta_uint)
{
  GESTimeline *timeline;
  GESLayer *layer;
  guint result;

  ges_init ();

  timeline = ges_timeline_new_audio_video ();
  layer = ges_layer_new ();
  ges_timeline_add_layer (timeline, layer);

  ges_meta_container_set_uint (GES_META_CONTAINER (layer), "ges-test", 42);

  fail_unless (ges_meta_container_get_uint (GES_META_CONTAINER
          (layer), "ges-test", &result));

  assert_equals_int (result, 42);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_layer_meta_int64)
{
  GESTimeline *timeline;
  GESLayer *layer;
  gint64 result;

  ges_init ();

  timeline = ges_timeline_new_audio_video ();
  layer = ges_layer_new ();
  ges_timeline_add_layer (timeline, layer);

  ges_meta_container_set_int64 (GES_META_CONTAINER (layer), "ges-test", 1234);

  fail_unless (ges_meta_container_get_int64 (GES_META_CONTAINER (layer),
          "ges-test", &result));

  assert_equals_int64 (result, 1234);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_layer_meta_uint64)
{
  GESTimeline *timeline;
  GESLayer *layer;
  guint64 result;

  ges_init ();

  timeline = ges_timeline_new_audio_video ();
  layer = ges_layer_new ();
  ges_timeline_add_layer (timeline, layer);

  ges_meta_container_set_uint64 (GES_META_CONTAINER (layer), "ges-test", 42);

  fail_unless (ges_meta_container_get_uint64 (GES_META_CONTAINER
          (layer), "ges-test", &result));

  assert_equals_uint64 (result, 42);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_layer_meta_float)
{
  GESTimeline *timeline;
  GESLayer *layer;
  gfloat result;

  ges_init ();

  timeline = ges_timeline_new_audio_video ();
  layer = ges_layer_new ();
  ges_timeline_add_layer (timeline, layer);

  fail_unless (ges_meta_container_set_float (GES_META_CONTAINER (layer),
          "ges-test", 23.456));

  fail_unless (ges_meta_container_get_float (GES_META_CONTAINER (layer),
          "ges-test", &result));

  assert_equals_float (result, 23.456f);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_layer_meta_double)
{
  GESTimeline *timeline;
  GESLayer *layer;
  gdouble result;

  ges_init ();

  timeline = ges_timeline_new_audio_video ();
  layer = ges_layer_new ();
  ges_timeline_add_layer (timeline, layer);

  ges_meta_container_set_double (GES_META_CONTAINER (layer),
      "ges-test", 23.456);

  fail_unless (ges_meta_container_get_double (GES_META_CONTAINER
          (layer), "ges-test", &result));
  fail_unless (result == 23.456);

  //TODO CHECK
  assert_equals_float (result, 23.456);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_layer_meta_date)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GDate *input;
  GDate *result;

  ges_init ();

  timeline = ges_timeline_new_audio_video ();
  layer = ges_layer_new ();
  ges_timeline_add_layer (timeline, layer);

  input = g_date_new_dmy (1, 1, 2012);

  ges_meta_container_set_date (GES_META_CONTAINER (layer), "ges-test", input);

  fail_unless (ges_meta_container_get_date (GES_META_CONTAINER
          (layer), "ges-test", &result));

  fail_unless (g_date_compare (result, input) == 0);

  g_date_free (input);
  g_date_free (result);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_layer_meta_date_time)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GstDateTime *input;
  GstDateTime *result = NULL;

  ges_init ();

  timeline = ges_timeline_new_audio_video ();
  layer = ges_layer_new ();
  ges_timeline_add_layer (timeline, layer);

  input = gst_date_time_new_from_unix_epoch_local_time (123456789);

  fail_unless (ges_meta_container_set_date_time (GES_META_CONTAINER
          (layer), "ges-test", input));

  fail_unless (ges_meta_container_get_date_time (GES_META_CONTAINER
          (layer), "ges-test", &result));

  fail_unless (result != NULL);
  fail_unless (gst_date_time_get_day (input) == gst_date_time_get_day (result));
  fail_unless (gst_date_time_get_hour (input) ==
      gst_date_time_get_hour (result));

  gst_date_time_unref (input);
  gst_date_time_unref (result);

  ges_deinit ();
}

GST_END_TEST;


GST_START_TEST (test_layer_meta_value)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GValue data = G_VALUE_INIT;
  const GValue *result;

  ges_init ();

  timeline = ges_timeline_new_audio_video ();
  layer = ges_layer_new ();
  ges_timeline_add_layer (timeline, layer);

  g_value_init (&data, G_TYPE_STRING);
  g_value_set_string (&data, "Hello world!");

  ges_meta_container_set_meta (GES_META_CONTAINER (layer),
      "ges-test-value", &data);

  result =
      ges_meta_container_get_meta (GES_META_CONTAINER (layer),
      "ges-test-value");
  assert_equals_string (g_value_get_string (result), "Hello world!");

  g_value_unset (&data);

  ges_deinit ();
}

GST_END_TEST;


GST_START_TEST (test_layer_meta_marker_list)
{
  GESTimeline *timeline;
  GESLayer *layer, *layer2;
  GESMarkerList *mlist, *mlist2;
  GESMarker *marker;
  gchar *metas1, *metas2;

  ges_init ();

  timeline = ges_timeline_new_audio_video ();
  layer = ges_layer_new ();
  ges_timeline_add_layer (timeline, layer);
  layer2 = ges_layer_new ();
  ges_timeline_add_layer (timeline, layer2);

  mlist = ges_marker_list_new ();
  marker = ges_marker_list_add (mlist, 42);
  ges_meta_container_set_string (GES_META_CONTAINER (marker), "bar", "baz");
  marker = ges_marker_list_add (mlist, 84);
  ges_meta_container_set_string (GES_META_CONTAINER (marker), "lorem",
      "ip\tsu\"m;");

  ASSERT_OBJECT_REFCOUNT (mlist, "local ref", 1);

  fail_unless (ges_meta_container_set_marker_list (GES_META_CONTAINER (layer),
          "foo", mlist));

  ASSERT_OBJECT_REFCOUNT (mlist, "GstStructure + local ref", 2);

  mlist2 =
      ges_meta_container_get_marker_list (GES_META_CONTAINER (layer), "foo");

  fail_unless (mlist == mlist2);

  ASSERT_OBJECT_REFCOUNT (mlist, "GstStructure + getter + local ref", 3);

  g_object_unref (mlist2);

  ASSERT_OBJECT_REFCOUNT (mlist, "GstStructure + local ref", 2);

  metas1 = ges_meta_container_metas_to_string (GES_META_CONTAINER (layer));
  ges_meta_container_add_metas_from_string (GES_META_CONTAINER (layer2),
      metas1);
  metas2 = ges_meta_container_metas_to_string (GES_META_CONTAINER (layer2));

  fail_unless_equals_string (metas1, metas2);
  g_free (metas1);
  g_free (metas2);

  fail_unless (ges_meta_container_set_marker_list (GES_META_CONTAINER (layer),
          "foo", NULL));

  ASSERT_OBJECT_REFCOUNT (mlist, "local ref", 1);

  g_object_unref (mlist);
  g_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_layer_meta_register)
{
  GESTimeline *timeline;
  GESLayer *layer;
  const gchar *result;

  ges_init ();

  timeline = ges_timeline_new_audio_video ();
  layer = ges_layer_new ();
  ges_timeline_add_layer (timeline, layer);

  fail_unless (ges_meta_container_register_meta_string (GES_META_CONTAINER
          (layer), GES_META_READABLE, "ges-test-value", "Hello world!"));

  result = ges_meta_container_get_string (GES_META_CONTAINER (layer),
      "ges-test-value");
  assert_equals_string (result, "Hello world!");

  fail_if (ges_meta_container_set_int (GES_META_CONTAINER (layer),
          "ges-test-value", 123456));

  result = ges_meta_container_get_string (GES_META_CONTAINER (layer),
      "ges-test-value");
  assert_equals_string (result, "Hello world!");

  ges_deinit ();
}

GST_END_TEST;

static void
test_foreach (const GESMetaContainer * container, const gchar * key,
    GValue * value, gpointer user_data)
{
  fail_unless ((0 == g_strcmp0 (key, "some-string")) ||
      (0 == g_strcmp0 (key, "some-int")) || (0 == g_strcmp0 (key, "volume")));
}

GST_START_TEST (test_layer_meta_foreach)
{
  GESTimeline *timeline;
  GESLayer *layer;

  ges_init ();

  timeline = ges_timeline_new_audio_video ();
  layer = ges_layer_new ();
  ges_timeline_add_layer (timeline, layer);

  ges_meta_container_set_string (GES_META_CONTAINER (layer),
      "some-string", "some-content");

  ges_meta_container_set_int (GES_META_CONTAINER (layer), "some-int", 123456);

  ges_meta_container_foreach (GES_META_CONTAINER (layer),
      (GESMetaForeachFunc) test_foreach, NULL);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_layer_get_clips_in_interval)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESClip *clip, *clip2, *clip3;
  GList *objects, *current;

  ges_init ();

  timeline = ges_timeline_new_audio_video ();
  layer = ges_layer_new ();
  ges_timeline_add_layer (timeline, layer);

  clip = (GESClip *) ges_test_clip_new ();
  fail_unless (clip != NULL);
  g_object_set (clip, "start", (guint64) 10, "duration", (gint64) 30, NULL);
  assert_equals_uint64 (_START (clip), 10);
  assert_equals_uint64 (_DURATION (clip), 30);

  ges_layer_add_clip (layer, GES_CLIP (clip));

  /* Clip's start lies between the interval */
  current = objects = ges_layer_get_clips_in_interval (layer, 0, 30);
  assert_equals_int (g_list_length (objects), 1);
  fail_unless (current->data == GES_TIMELINE_ELEMENT (clip));
  g_list_free_full (objects, gst_object_unref);

  current = objects = ges_layer_get_clips_in_interval (layer, 0, 11);
  assert_equals_int (g_list_length (objects), 1);
  fail_unless (current->data == GES_TIMELINE_ELEMENT (clip));
  g_list_free_full (objects, gst_object_unref);

  /* Clip's end lies between the interval */
  current = objects = ges_layer_get_clips_in_interval (layer, 30, 50);
  assert_equals_int (g_list_length (objects), 1);
  fail_unless (current->data == GES_TIMELINE_ELEMENT (clip));
  g_list_free_full (objects, gst_object_unref);

  current = objects = ges_layer_get_clips_in_interval (layer, 39, 50);
  assert_equals_int (g_list_length (objects), 1);
  fail_unless (current->data == GES_TIMELINE_ELEMENT (clip));
  g_list_free_full (objects, gst_object_unref);

  /* Clip exactly overlaps the interval */
  current = objects = ges_layer_get_clips_in_interval (layer, 10, 40);
  assert_equals_int (g_list_length (objects), 1);
  fail_unless (current->data == GES_TIMELINE_ELEMENT (clip));
  g_list_free_full (objects, gst_object_unref);

  /* Clip completely inside the interval */
  current = objects = ges_layer_get_clips_in_interval (layer, 0, 50);
  assert_equals_int (g_list_length (objects), 1);
  fail_unless (current->data == GES_TIMELINE_ELEMENT (clip));
  g_list_free_full (objects, gst_object_unref);

  /* Interval completely inside the clip duration */
  current = objects = ges_layer_get_clips_in_interval (layer, 20, 30);
  assert_equals_int (g_list_length (objects), 1);
  fail_unless (current->data == GES_TIMELINE_ELEMENT (clip));
  g_list_free_full (objects, gst_object_unref);

  /* No intersecting clip */
  objects = ges_layer_get_clips_in_interval (layer, 0, 10);
  assert_equals_int (g_list_length (objects), 0);

  objects = ges_layer_get_clips_in_interval (layer, 40, 50);
  assert_equals_int (g_list_length (objects), 0);

  /* Multiple intersecting clips */
  clip2 = (GESClip *) ges_test_clip_new ();
  fail_unless (clip2 != NULL);
  g_object_set (clip2, "start", (guint64) 50, "duration", (gint64) 10, NULL);
  assert_equals_uint64 (_START (clip2), 50);
  assert_equals_uint64 (_DURATION (clip2), 10);

  ges_layer_add_clip (layer, GES_CLIP (clip2));

  clip3 = (GESClip *) ges_test_clip_new ();
  fail_unless (clip3 != NULL);
  g_object_set (clip3, "start", (guint64) 0, "duration", (gint64) 5, NULL);
  assert_equals_uint64 (_START (clip3), 0);
  assert_equals_uint64 (_DURATION (clip3), 5);

  ges_layer_add_clip (layer, GES_CLIP (clip3));

  /**
  * Our timeline:
  * -------------
  *
  *          |--------    0---------------     0---------       |
  * layer:   |  clip3 |   |     clip     |     |  clip2  |      |
  *          |-------05  10-------------40    50--------60      |
  *          |--------------------------------------------------|
  *
  */

  current = objects = ges_layer_get_clips_in_interval (layer, 4, 52);
  assert_equals_int (g_list_length (objects), 3);
  fail_unless (current->data == GES_TIMELINE_ELEMENT (clip3));
  current = current->next;
  fail_unless (current->data == GES_TIMELINE_ELEMENT (clip));
  current = current->next;
  fail_unless (current->data == GES_TIMELINE_ELEMENT (clip2));
  g_list_free_full (objects, gst_object_unref);

  current = objects = ges_layer_get_clips_in_interval (layer, 39, 65);
  assert_equals_int (g_list_length (objects), 2);
  fail_unless (current->data == GES_TIMELINE_ELEMENT (clip));
  current = current->next;
  fail_unless (current->data == GES_TIMELINE_ELEMENT (clip2));
  g_list_free_full (objects, gst_object_unref);

  ges_deinit ();
}

GST_END_TEST;

static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges-layer");
  TCase *tc_chain = tcase_create ("timeline-layer");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_layer_properties);
  tcase_add_test (tc_chain, test_layer_priorities);
  tcase_add_test (tc_chain, test_timeline_auto_transition);
  tcase_add_test (tc_chain, test_single_layer_automatic_transition);
  tcase_add_test (tc_chain, test_multi_layer_automatic_transition);
  tcase_add_test (tc_chain, test_layer_activate_automatic_transition);
  tcase_add_test (tc_chain, test_layer_meta_string);
  tcase_add_test (tc_chain, test_layer_meta_boolean);
  tcase_add_test (tc_chain, test_layer_meta_int);
  tcase_add_test (tc_chain, test_layer_meta_uint);
  tcase_add_test (tc_chain, test_layer_meta_int64);
  tcase_add_test (tc_chain, test_layer_meta_uint64);
  tcase_add_test (tc_chain, test_layer_meta_float);
  tcase_add_test (tc_chain, test_layer_meta_double);
  tcase_add_test (tc_chain, test_layer_meta_date);
  tcase_add_test (tc_chain, test_layer_meta_date_time);
  tcase_add_test (tc_chain, test_layer_meta_value);
  tcase_add_test (tc_chain, test_layer_meta_marker_list);
  tcase_add_test (tc_chain, test_layer_meta_register);
  tcase_add_test (tc_chain, test_layer_meta_foreach);
  tcase_add_test (tc_chain, test_layer_get_clips_in_interval);

  return s;
}

GST_CHECK_MAIN (ges);
