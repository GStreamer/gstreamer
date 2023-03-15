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
#include "../../../ges/ges-structured-interface.h"
#include <ges/ges.h>
#include <gst/check/gstcheck.h>

#define _assert_add(clip, child) \
  fail_unless (ges_container_add (GES_CONTAINER (clip), \
        GES_TIMELINE_ELEMENT (child)))

#define _assert_remove(clip, child) \
  fail_unless (ges_container_remove (GES_CONTAINER (clip), \
        GES_TIMELINE_ELEMENT (child)))

GST_START_TEST (test_object_properties)
{
  GESClip *clip;
  GESTrack *track;
  GESTimeline *timeline;
  GESLayer *layer;
  GESTrackElement *trackelement;

  ges_init ();

  track = GES_TRACK (ges_video_track_new ());
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
  g_object_set (clip, "start", (guint64) 42, "duration", (guint64) 51,
      "in-point", (guint64) 12, NULL);
  assert_equals_uint64 (_START (clip), 42);
  assert_equals_uint64 (_DURATION (clip), 51);
  assert_equals_uint64 (_INPOINT (clip), 12);

  ges_layer_add_clip (layer, GES_CLIP (clip));
  ges_timeline_commit (timeline);
  assert_num_children (clip, 1);
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
  nle_object_check (ges_track_element_get_nleobject (trackelement), 42, 51, 12,
      51, MIN_NLE_PRIO + TRANSITIONS_HEIGHT, TRUE);

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
  nle_object_check (ges_track_element_get_nleobject (trackelement), 420, 510,
      120, 510, MIN_NLE_PRIO + TRANSITIONS_HEIGHT, TRUE);


  /* This time, we move the trackelement to see if the changes move
   * along to the parent and the gnonlin clip */
  g_object_set (trackelement, "start", (guint64) 400, NULL);
  ges_timeline_commit (timeline);
  assert_equals_uint64 (_START (clip), 400);
  assert_equals_uint64 (_START (trackelement), 400);
  nle_object_check (ges_track_element_get_nleobject (trackelement), 400, 510,
      120, 510, MIN_NLE_PRIO + TRANSITIONS_HEIGHT, TRUE);

  _assert_remove (clip, trackelement);

  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_split_direct_bindings)
{
  GList *values;
  GstControlSource *source;
  GESTimeline *timeline;
  GESClip *clip, *splitclip;
  GstControlBinding *binding = NULL, *splitbinding;
  GstTimedValueControlSource *splitsource;
  GESLayer *layer;
  GESAsset *asset;
  GValue *tmpvalue;

  GESTrackElement *element;

  ges_init ();

  fail_unless ((timeline = ges_timeline_new ()));
  fail_unless ((layer = ges_layer_new ()));
  fail_unless (ges_timeline_add_track (timeline,
          GES_TRACK (ges_video_track_new ())));
  fail_unless (ges_timeline_add_layer (timeline, layer));

  asset = ges_asset_request (GES_TYPE_TEST_CLIP, NULL, NULL);
  clip = ges_layer_add_asset (layer, asset, 0, 10 * GST_SECOND, 10 * GST_SECOND,
      GES_TRACK_TYPE_UNKNOWN);
  g_object_unref (asset);

  CHECK_OBJECT_PROPS (clip, 0 * GST_SECOND, 10 * GST_SECOND, 10 * GST_SECOND);
  assert_num_children (clip, 1);
  check_layer (clip, 0);

  source = gst_interpolation_control_source_new ();
  g_object_set (source, "mode", GST_INTERPOLATION_MODE_LINEAR, NULL);
  element = GES_CONTAINER_CHILDREN (clip)->data;
  fail_unless (ges_track_element_set_control_source (element,
          source, "alpha", "direct"));

  gst_timed_value_control_source_set (GST_TIMED_VALUE_CONTROL_SOURCE (source),
      10 * GST_SECOND, 0.0);
  gst_timed_value_control_source_set (GST_TIMED_VALUE_CONTROL_SOURCE (source),
      20 * GST_SECOND, 1.0);

  binding = ges_track_element_get_control_binding (element, "alpha");
  tmpvalue = gst_control_binding_get_value (binding, 10 * GST_SECOND);
  assert_equals_int (g_value_get_double (tmpvalue), 0.0);
  g_value_unset (tmpvalue);
  g_free (tmpvalue);

  tmpvalue = gst_control_binding_get_value (binding, 20 * GST_SECOND);
  assert_equals_int (g_value_get_double (tmpvalue), 1.0);
  g_value_unset (tmpvalue);
  g_free (tmpvalue);

  splitclip = ges_clip_split (clip, 5 * GST_SECOND);
  CHECK_OBJECT_PROPS (splitclip, 5 * GST_SECOND, 15 * GST_SECOND,
      5 * GST_SECOND);
  check_layer (splitclip, 0);

  splitbinding =
      ges_track_element_get_control_binding (GES_CONTAINER_CHILDREN
      (splitclip)->data, "alpha");
  g_object_get (splitbinding, "control_source", &splitsource, NULL);

  values =
      gst_timed_value_control_source_get_all (GST_TIMED_VALUE_CONTROL_SOURCE
      (splitsource));
  assert_equals_int (g_list_length (values), 2);
  assert_equals_uint64 (((GstTimedValue *) values->data)->timestamp,
      15 * GST_SECOND);
  assert_equals_float (((GstTimedValue *) values->data)->value, 0.5);

  assert_equals_uint64 (((GstTimedValue *) values->next->data)->timestamp,
      20 * GST_SECOND);
  assert_equals_float (((GstTimedValue *) values->next->data)->value, 1.0);
  g_list_free (values);

  values =
      gst_timed_value_control_source_get_all (GST_TIMED_VALUE_CONTROL_SOURCE
      (source));
  assert_equals_int (g_list_length (values), 2);
  assert_equals_uint64 (((GstTimedValue *) values->data)->timestamp,
      10 * GST_SECOND);
  assert_equals_float (((GstTimedValue *) values->data)->value, 0.0);

  assert_equals_uint64 (((GstTimedValue *) values->next->data)->timestamp,
      15 * GST_SECOND);
  assert_equals_float (((GstTimedValue *) values->next->data)->value, 0.50);
  g_list_free (values);

  CHECK_OBJECT_PROPS (clip, 0 * GST_SECOND, 10 * GST_SECOND, 5 * GST_SECOND);
  check_layer (clip, 0);

  gst_object_unref (timeline);
  gst_object_unref (source);
  gst_object_unref (splitsource);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_split_direct_absolute_bindings)
{
  GList *values;
  GstControlSource *source;
  GESTimeline *timeline;
  GESClip *clip, *splitclip;
  GstControlBinding *binding = NULL, *splitbinding;
  GstTimedValueControlSource *splitsource;
  GESLayer *layer;
  GESAsset *asset;
  GValue *tmpvalue;

  GESTrackElement *element;

  ges_init ();

  fail_unless ((timeline = ges_timeline_new ()));
  fail_unless ((layer = ges_layer_new ()));
  fail_unless (ges_timeline_add_track (timeline,
          GES_TRACK (ges_video_track_new ())));
  fail_unless (ges_timeline_add_layer (timeline, layer));

  asset = ges_asset_request (GES_TYPE_TEST_CLIP, NULL, NULL);
  clip = ges_layer_add_asset (layer, asset, 0, 10 * GST_SECOND, 10 * GST_SECOND,
      GES_TRACK_TYPE_UNKNOWN);
  g_object_unref (asset);

  CHECK_OBJECT_PROPS (clip, 0 * GST_SECOND, 10 * GST_SECOND, 10 * GST_SECOND);
  assert_num_children (clip, 1);
  check_layer (clip, 0);

  source = gst_interpolation_control_source_new ();
  g_object_set (source, "mode", GST_INTERPOLATION_MODE_LINEAR, NULL);
  element = GES_CONTAINER_CHILDREN (clip)->data;
  fail_unless (ges_track_element_set_control_source (element,
          source, "posx", "direct-absolute"));

  gst_timed_value_control_source_set (GST_TIMED_VALUE_CONTROL_SOURCE (source),
      10 * GST_SECOND, 0);
  gst_timed_value_control_source_set (GST_TIMED_VALUE_CONTROL_SOURCE (source),
      20 * GST_SECOND, 500);

  binding = ges_track_element_get_control_binding (element, "posx");
  tmpvalue = gst_control_binding_get_value (binding, 10 * GST_SECOND);
  assert_equals_int (g_value_get_int (tmpvalue), 0);
  g_value_unset (tmpvalue);
  g_free (tmpvalue);

  tmpvalue = gst_control_binding_get_value (binding, 20 * GST_SECOND);
  assert_equals_int (g_value_get_int (tmpvalue), 500);
  g_value_unset (tmpvalue);
  g_free (tmpvalue);

  splitclip = ges_clip_split (clip, 5 * GST_SECOND);
  CHECK_OBJECT_PROPS (splitclip, 5 * GST_SECOND, 15 * GST_SECOND,
      5 * GST_SECOND);
  check_layer (splitclip, 0);

  splitbinding =
      ges_track_element_get_control_binding (GES_CONTAINER_CHILDREN
      (splitclip)->data, "posx");
  g_object_get (splitbinding, "control_source", &splitsource, NULL);

  values =
      gst_timed_value_control_source_get_all (GST_TIMED_VALUE_CONTROL_SOURCE
      (splitsource));
  assert_equals_int (g_list_length (values), 2);
  assert_equals_uint64 (((GstTimedValue *) values->data)->timestamp,
      15 * GST_SECOND);
  assert_equals_float (((GstTimedValue *) values->data)->value, 250.0);

  assert_equals_uint64 (((GstTimedValue *) values->next->data)->timestamp,
      20 * GST_SECOND);
  assert_equals_float (((GstTimedValue *) values->next->data)->value, 500.0);
  g_list_free (values);

  values =
      gst_timed_value_control_source_get_all (GST_TIMED_VALUE_CONTROL_SOURCE
      (source));
  assert_equals_int (g_list_length (values), 2);
  assert_equals_uint64 (((GstTimedValue *) values->data)->timestamp,
      10 * GST_SECOND);
  assert_equals_float (((GstTimedValue *) values->data)->value, 0.0);

  assert_equals_uint64 (((GstTimedValue *) values->next->data)->timestamp,
      15 * GST_SECOND);
  assert_equals_float (((GstTimedValue *) values->next->data)->value, 250.0);
  g_list_free (values);

  CHECK_OBJECT_PROPS (clip, 0 * GST_SECOND, 10 * GST_SECOND, 5 * GST_SECOND);
  check_layer (clip, 0);

  gst_object_unref (timeline);
  gst_object_unref (source);
  gst_object_unref (splitsource);

  ges_deinit ();
}

GST_END_TEST;

static GESTimelineElement *
_find_auto_transition (GESTrack * track, GESClip * from_clip, GESClip * to_clip)
{
  GstClockTime start, end;
  GList *tmp, *track_els;
  GESTimelineElement *ret = NULL;
  GESLayer *layer0, *layer1;

  layer0 = ges_clip_get_layer (from_clip);
  layer1 = ges_clip_get_layer (to_clip);

  fail_unless (layer0 == layer1, "%" GES_FORMAT " and %" GES_FORMAT " do not "
      "share the same layer", GES_ARGS (from_clip), GES_ARGS (to_clip));
  gst_object_unref (layer1);

  start = GES_TIMELINE_ELEMENT_START (to_clip);
  end = GES_TIMELINE_ELEMENT_START (from_clip)
      + GES_TIMELINE_ELEMENT_DURATION (from_clip);

  fail_if (end <= start, "%" GES_FORMAT " starts after %" GES_FORMAT " ends",
      GES_ARGS (to_clip), GES_ARGS (from_clip));

  track_els = ges_track_get_elements (track);

  for (tmp = track_els; tmp; tmp = tmp->next) {
    GESTimelineElement *el = tmp->data;
    if (GES_IS_TRANSITION (el) && el->start == start
        && (el->start + el->duration) == end) {
      fail_if (ret, "Found two transitions %" GES_FORMAT " and %" GES_FORMAT
          " between %" GES_FORMAT " and %" GES_FORMAT " in track %"
          GST_PTR_FORMAT, GES_ARGS (el), GES_ARGS (ret), GES_ARGS (from_clip),
          GES_ARGS (to_clip), track);
      ret = el;
    }
  }
  fail_unless (ret, "Found no transitions between %" GES_FORMAT " and %"
      GES_FORMAT " in track %" GST_PTR_FORMAT, GES_ARGS (from_clip),
      GES_ARGS (to_clip), track);

  g_list_free_full (track_els, gst_object_unref);

  fail_unless (GES_IS_CLIP (ret->parent), "Transition %" GES_FORMAT
      " between %" GES_FORMAT " and %" GES_FORMAT " in track %"
      GST_PTR_FORMAT " has no parent clip", GES_ARGS (ret),
      GES_ARGS (from_clip), GES_ARGS (to_clip), track);

  layer1 = ges_clip_get_layer (GES_CLIP (ret->parent));

  fail_unless (layer0 == layer1, "Transition %" GES_FORMAT " between %"
      GES_FORMAT " and %" GES_FORMAT " in track %" GST_PTR_FORMAT
      " belongs to layer %" GST_PTR_FORMAT " rather than %" GST_PTR_FORMAT,
      GES_ARGS (ret), GES_ARGS (from_clip), GES_ARGS (to_clip), track,
      layer1, layer0);

  gst_object_unref (layer0);
  gst_object_unref (layer1);

  return ret;
}

GST_START_TEST (test_split_with_auto_transitions)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESTrack *tracks[3];
  GESTimelineElement *found;
  GESTimelineElement *prev_trans[3];
  GESTimelineElement *post_trans[3];
  GESAsset *asset;
  GESClip *clip, *split, *prev, *post;
  guint i;

  ges_init ();

  timeline = ges_timeline_new ();

  ges_timeline_set_auto_transition (timeline, TRUE);

  tracks[0] = GES_TRACK (ges_audio_track_new ());
  tracks[1] = GES_TRACK (ges_audio_track_new ());
  tracks[2] = GES_TRACK (ges_video_track_new ());

  for (i = 0; i < 3; i++)
    fail_unless (ges_timeline_add_track (timeline, tracks[i]));

  layer = ges_timeline_append_layer (timeline);
  asset = ges_asset_request (GES_TYPE_TEST_CLIP, NULL, NULL);

  prev = ges_layer_add_asset (layer, asset, 0, 0, 10, GES_TRACK_TYPE_UNKNOWN);
  clip = ges_layer_add_asset (layer, asset, 5, 0, 20, GES_TRACK_TYPE_UNKNOWN);
  post = ges_layer_add_asset (layer, asset, 20, 0, 10, GES_TRACK_TYPE_UNKNOWN);

  fail_unless (prev);
  fail_unless (clip);
  fail_unless (post);

  for (i = 0; i < 3; i++) {
    prev_trans[i] = _find_auto_transition (tracks[i], prev, clip);
    post_trans[i] = _find_auto_transition (tracks[i], clip, post);
    /* 3 sources, 2 auto-transitions */
    assert_num_in_track (tracks[i], 5);

  }

  /* cannot split within a transition */
  fail_if (ges_clip_split (clip, 5));
  fail_if (ges_clip_split (clip, 20));

  /* we should keep the same auto-transitions during a split */
  split = ges_clip_split (clip, 15);
  fail_unless (split);

  for (i = 0; i < 3; i++) {
    found = _find_auto_transition (tracks[i], prev, clip);
    fail_unless (found == prev_trans[i], "Transition between %" GES_FORMAT
        " and %" GES_FORMAT " changed", GES_ARGS (prev), GES_ARGS (clip));

    found = _find_auto_transition (tracks[i], split, post);
    fail_unless (found == post_trans[i], "Transition between %" GES_FORMAT
        " and %" GES_FORMAT " changed", GES_ARGS (clip), GES_ARGS (post));
  }

  gst_object_unref (timeline);
  gst_object_unref (asset);

  ges_deinit ();
}

GST_END_TEST;

static GPtrArray *
_select_none (GESTimeline * timeline, GESClip * clip,
    GESTrackElement * track_element, guint * called_p)
{
  (*called_p)++;
  return NULL;
}

static GPtrArray *
_select_track (GESTimeline * timeline, GESClip * clip,
    GESTrackElement * track_element, GESTrack ** track_p)
{
  GPtrArray *tracks = g_ptr_array_new ();
  fail_unless (track_p);
  fail_unless (*track_p);
  g_ptr_array_insert (tracks, -1, gst_object_ref (*track_p));
  *track_p = NULL;
  return tracks;
}

GST_START_TEST (test_split_object)
{
  GESTimeline *timeline;
  GESTrack *track1, *track2, *effect_track;
  GESLayer *layer;
  GESClip *clip, *splitclip;
  GList *splittrackelements;
  GESTrackElement *trackelement1, *trackelement2, *effect1, *effect2,
      *splittrackelement;
  guint32 priority1, priority2, effect_priority1, effect_priority2;
  guint selection_called = 0;
  const gchar *meta;

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
  CHECK_OBJECT_PROPS (clip, 42, 12, 50);

  ges_layer_add_clip (layer, GES_CLIP (clip));
  ges_timeline_commit (timeline);
  assert_num_children (clip, 2);
  trackelement1 = GES_CONTAINER_CHILDREN (clip)->data;
  fail_unless (trackelement1 != NULL);
  fail_unless (GES_TIMELINE_ELEMENT_PARENT (trackelement1) ==
      GES_TIMELINE_ELEMENT (clip));
  ges_meta_container_set_string (GES_META_CONTAINER (trackelement1), "test_key",
      "test_value");

  trackelement2 = GES_CONTAINER_CHILDREN (clip)->next->data;
  fail_unless (trackelement2 != NULL);
  fail_unless (GES_TIMELINE_ELEMENT_PARENT (trackelement2) ==
      GES_TIMELINE_ELEMENT (clip));

  effect1 = GES_TRACK_ELEMENT (ges_effect_new ("agingtv"));
  _assert_add (clip, effect1);

  effect2 = GES_TRACK_ELEMENT (ges_effect_new ("vertigotv"));
  _assert_add (clip, effect2);

  /* Check that trackelement has the same properties */
  CHECK_OBJECT_PROPS (trackelement1, 42, 12, 50);
  CHECK_OBJECT_PROPS (trackelement2, 42, 12, 50);
  CHECK_OBJECT_PROPS (effect1, 42, 0, 50);
  CHECK_OBJECT_PROPS (effect2, 42, 0, 50);

  /* And let's also check that it propagated correctly to GNonLin */
  nle_object_check (ges_track_element_get_nleobject (trackelement1), 42, 50, 12,
      50, MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 2, TRUE);
  nle_object_check (ges_track_element_get_nleobject (trackelement2), 42, 50, 12,
      50, MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 2, TRUE);

  track1 = ges_track_element_get_track (trackelement1);
  fail_unless (track1);
  track2 = ges_track_element_get_track (trackelement2);
  fail_unless (track2);
  fail_unless (track1 != track2);
  effect_track = ges_track_element_get_track (effect1);
  fail_unless (effect_track);
  fail_unless (ges_track_element_get_track (effect2) == effect_track);

  priority1 = GES_TIMELINE_ELEMENT_PRIORITY (trackelement1);
  priority2 = GES_TIMELINE_ELEMENT_PRIORITY (trackelement2);
  effect_priority1 = GES_TIMELINE_ELEMENT_PRIORITY (effect1);
  effect_priority2 = GES_TIMELINE_ELEMENT_PRIORITY (effect2);

  fail_unless (priority1 == priority2);
  fail_unless (priority1 > effect_priority2);
  fail_unless (effect_priority2 > effect_priority1);

  ges_timeline_element_set_child_properties (GES_TIMELINE_ELEMENT (clip),
      "font-desc", "Normal", "posx", 30, "posy", 50, "alpha", 0.1,
      "freq", 449.0, "scratch-lines", 2, "zoom-speed", 1.05, NULL);

  /* splitting should avoid track selection */
  g_signal_connect (timeline, "select-tracks-for-object",
      G_CALLBACK (_select_none), &selection_called);

  splitclip = ges_clip_split (clip, 67);
  fail_unless (GES_IS_CLIP (splitclip));
  fail_unless (splitclip != clip);

  fail_if (selection_called);

  CHECK_OBJECT_PROPS (clip, 42, 12, 25);
  CHECK_OBJECT_PROPS (trackelement1, 42, 12, 25);
  CHECK_OBJECT_PROPS (trackelement1, 42, 12, 25);
  CHECK_OBJECT_PROPS (effect1, 42, 0, 25);
  CHECK_OBJECT_PROPS (effect2, 42, 0, 25);

  CHECK_OBJECT_PROPS (splitclip, 67, 37, 25);

  assert_equal_children_properties (splitclip, clip);

  splittrackelements = GES_CONTAINER_CHILDREN (splitclip);
  fail_unless_equals_int (g_list_length (splittrackelements), 4);

  /* first is the effects */
  splittrackelement = GES_TRACK_ELEMENT (splittrackelements->data);
  fail_unless (GES_IS_TRACK_ELEMENT (splittrackelement));
  CHECK_OBJECT_PROPS (splittrackelement, 67, 0, 25);

  assert_equal_children_properties (splittrackelement, effect1);
  fail_unless (ges_track_element_get_track (splittrackelement) == effect_track);
  fail_unless (ges_track_element_get_track (effect1) == effect_track);
  /* +3 priority from layer */
  assert_equals_int (GES_TIMELINE_ELEMENT_PRIORITY (splittrackelement),
      effect_priority1 + 3);
  fail_unless (GES_TIMELINE_ELEMENT_PRIORITY (effect1) == effect_priority1);

  fail_unless (splittrackelement != trackelement1);
  fail_unless (splittrackelement != trackelement2);
  fail_unless (splittrackelement != effect1);
  fail_unless (splittrackelement != effect2);

  splittrackelement = GES_TRACK_ELEMENT (splittrackelements->next->data);
  fail_unless (GES_IS_TRACK_ELEMENT (splittrackelement));
  CHECK_OBJECT_PROPS (splittrackelement, 67, 0, 25);

  assert_equal_children_properties (splittrackelement, effect2);
  fail_unless (ges_track_element_get_track (splittrackelement) == effect_track);
  fail_unless (ges_track_element_get_track (effect2) == effect_track);
  assert_equals_int (GES_TIMELINE_ELEMENT_PRIORITY (splittrackelement),
      effect_priority2 + 3);
  fail_unless (GES_TIMELINE_ELEMENT_PRIORITY (effect2) == effect_priority2);

  fail_unless (splittrackelement != trackelement1);
  fail_unless (splittrackelement != trackelement2);
  fail_unless (splittrackelement != effect1);
  fail_unless (splittrackelement != effect2);

  splittrackelement = GES_TRACK_ELEMENT (splittrackelements->next->next->data);
  fail_unless (GES_IS_TRACK_ELEMENT (splittrackelement));
  CHECK_OBJECT_PROPS (splittrackelement, 67, 37, 25);

  /* core elements have swapped order in the clip, this is ok since they
   * share the same priority */
  assert_equal_children_properties (splittrackelement, trackelement1);
  fail_unless (ges_track_element_get_track (splittrackelement) == track1);
  fail_unless (ges_track_element_get_track (trackelement1) == track1);
  assert_equals_int (GES_TIMELINE_ELEMENT_PRIORITY (splittrackelement),
      priority1 + 3);
  fail_unless (GES_TIMELINE_ELEMENT_PRIORITY (trackelement1) == priority1);
  meta = ges_meta_container_get_string (GES_META_CONTAINER (splittrackelement),
      "test_key");
  fail_unless_equals_string (meta, "test_value");

  fail_unless (splittrackelement != trackelement1);
  fail_unless (splittrackelement != trackelement2);
  fail_unless (splittrackelement != effect1);
  fail_unless (splittrackelement != effect2);

  splittrackelement =
      GES_TRACK_ELEMENT (splittrackelements->next->next->next->data);
  fail_unless (GES_IS_TRACK_ELEMENT (splittrackelement));
  CHECK_OBJECT_PROPS (splittrackelement, 67, 37, 25);

  assert_equal_children_properties (splittrackelement, trackelement2);
  fail_unless (ges_track_element_get_track (splittrackelement) == track2);
  fail_unless (ges_track_element_get_track (trackelement2) == track2);
  assert_equals_int (GES_TIMELINE_ELEMENT_PRIORITY (splittrackelement),
      priority2 + 3);
  fail_unless (GES_TIMELINE_ELEMENT_PRIORITY (trackelement2) == priority2);

  fail_unless (splittrackelement != trackelement1);
  fail_unless (splittrackelement != trackelement2);
  fail_unless (splittrackelement != effect1);
  fail_unless (splittrackelement != effect2);

  /* We own the only ref */
  ASSERT_OBJECT_REFCOUNT (splitclip, "1 ref for us + 1 for the timeline", 2);
  /* 1 ref for the Clip, 1 ref for the Track and 2 ref for the timeline
   * (1 for the "all_element" hashtable, another for the sequence of TrackElement*/
  ASSERT_OBJECT_REFCOUNT (splittrackelement,
      "1 ref for the Clip, 1 ref for the Track and 1 ref for the timeline", 3);

  check_destroyed (G_OBJECT (timeline), G_OBJECT (splitclip), clip,
      splittrackelement, NULL);

  ges_deinit ();
}

GST_END_TEST;

typedef struct
{
  gboolean duration_cb_called;
  gboolean clip_added_cb_called;
  gboolean track_selected_cb_called;
  GESClip *clip;
} SplitOrderData;

static void
_track_selected_cb (GESTimelineElement * el, GParamSpec * spec,
    SplitOrderData * data)
{
  GESClip *clip = GES_CLIP (el->parent);

  fail_unless (data->clip == clip, "Parent is %" GES_FORMAT " rather than %"
      GES_FORMAT, GES_ARGS (clip), GES_ARGS (data->clip));

  fail_unless (data->duration_cb_called, "notify::duration not emitted "
      "for neighbour of %" GES_FORMAT, GES_ARGS (data->clip));
  fail_unless (data->clip_added_cb_called, "child-added not emitted for %"
      GES_FORMAT, GES_ARGS (data->clip));

  data->track_selected_cb_called = TRUE;
}

static void
_child_added_cb (GESClip * clip, GESTimelineElement * child,
    SplitOrderData * data)
{
  fail_unless (data->clip == clip, "Received %" GES_FORMAT " rather than %"
      GES_FORMAT, GES_ARGS (clip), GES_ARGS (data->clip));

  g_signal_connect (child, "notify::track", G_CALLBACK (_track_selected_cb),
      data);
}

static void
_clip_added_cb (GESLayer * layer, GESClip * clip, SplitOrderData * data)
{
  GList *tmp;

  data->clip = clip;

  fail_unless (data->duration_cb_called, "notify::duration not emitted "
      "for neighbour of %" GES_FORMAT, GES_ARGS (data->clip));
  /* only called once */
  fail_if (data->clip_added_cb_called, "clip-added already emitted for %"
      GES_FORMAT, GES_ARGS (data->clip));
  fail_if (data->track_selected_cb_called, "track selection already "
      "occurred for %" GES_FORMAT, GES_ARGS (data->clip));

  data->clip_added_cb_called = TRUE;

  g_signal_connect (clip, "child-added", G_CALLBACK (_child_added_cb), data);

  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = tmp->next)
    g_signal_connect (tmp->data, "notify::track",
        G_CALLBACK (_track_selected_cb), data);
}

static void
_disconnect_cbs (GESLayer * layer, GESClip * clip, SplitOrderData * data)
{
  GList *tmp;
  g_signal_handlers_disconnect_by_func (clip, _child_added_cb, data);
  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = tmp->next)
    g_signal_handlers_disconnect_by_func (tmp->data, _track_selected_cb, data);
}

static void
_duration_cb (GObject * object, GParamSpec * pspec, SplitOrderData * data)
{
  /* only called once */
  fail_if (data->duration_cb_called, "notify::duration of neighbour %"
      GES_FORMAT " already emitted ", GES_ARGS (object));
  fail_if (data->clip_added_cb_called, "clip-added already emitted");
  fail_if (data->track_selected_cb_called, "track selection already "
      "occurred");

  data->duration_cb_called = TRUE;
}

GST_START_TEST (test_split_ordering)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESClip *clip, *splitclip;
  SplitOrderData data;

  ges_init ();

  timeline = ges_timeline_new_audio_video ();

  layer = ges_timeline_append_layer (timeline);

  clip = GES_CLIP (ges_test_clip_new ());
  assert_set_duration (clip, 10);

  /* test order when adding clip to a layer */
  /* don't care about duration yet */
  data.duration_cb_called = TRUE;
  data.clip_added_cb_called = FALSE;
  data.track_selected_cb_called = FALSE;
  data.clip = NULL;

  g_signal_connect (layer, "clip-added", G_CALLBACK (_clip_added_cb), &data);

  fail_unless (ges_layer_add_clip (layer, clip));

  fail_unless (data.duration_cb_called);
  fail_unless (data.clip_added_cb_called);
  fail_unless (data.track_selected_cb_called);
  fail_unless (data.clip == clip);

  /* now check for the same ordering when splitting, which the original
   * clip shrinking before the new one is added to the layer */
  data.duration_cb_called = FALSE;
  data.clip_added_cb_called = FALSE;
  data.track_selected_cb_called = FALSE;
  data.clip = NULL;

  g_signal_connect (clip, "notify::duration", G_CALLBACK (_duration_cb), &data);

  splitclip = ges_clip_split (clip, 5);

  fail_unless (splitclip);
  fail_unless (data.duration_cb_called);
  fail_unless (data.clip_added_cb_called);
  fail_unless (data.track_selected_cb_called);
  fail_unless (data.clip == splitclip);

  /* disconnect since track of children will change when timeline is
   * freed */
  _disconnect_cbs (layer, clip, &data);
  _disconnect_cbs (layer, splitclip, &data);

  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

#define _assert_higher_priority(el, higher) \
{ \
  if (higher) { \
    guint32 el_prio = GES_TIMELINE_ELEMENT_PRIORITY (el); \
    guint32 higher_prio = GES_TIMELINE_ELEMENT_PRIORITY (higher); \
    fail_unless (el_prio > higher_prio, "%s does not have a higher " \
        "priority than %s (%u vs %u)", GES_TIMELINE_ELEMENT_NAME (el), \
        GES_TIMELINE_ELEMENT_NAME (higher), el_prio, higher_prio); \
  } \
}

#define _assert_regroup_fails(clip_list) \
{ \
  GESContainer *regrouped = ges_container_group (clip_list); \
  fail_unless (GES_IS_GROUP (regrouped)); \
  assert_equals_int (g_list_length (regrouped->children), \
      g_list_length (clip_list)); \
  g_list_free_full (ges_container_ungroup (regrouped, FALSE), \
      gst_object_unref); \
}

GST_START_TEST (test_clip_group_ungroup)
{
  GESAsset *asset;
  GESTimeline *timeline;
  GESClip *clip, *video_clip, *audio_clip;
  GESTrackElement *el;
  GList *containers, *tmp;
  GESLayer *layer;
  GESContainer *regrouped_clip;
  GESTrack *audio_track, *video_track;
  guint selection_called = 0;
  struct
  {
    GESTrackElement *element;
    GESTrackElement *higher_priority;
  } audio_els[2];
  struct
  {
    GESTrackElement *element;
    GESTrackElement *higher_priority;
  } video_els[3];
  guint i, j;
  const gchar *name;
  GESTrackType type;

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
  ASSERT_OBJECT_REFCOUNT (clip, "1 layer + 1 timeline.all_els", 2);
  assert_num_children (clip, 2);
  CHECK_OBJECT_PROPS (clip, 0, 0, 10);

  el = GES_TRACK_ELEMENT (ges_effect_new ("audioecho"));
  ges_track_element_set_track_type (el, GES_TRACK_TYPE_AUDIO);
  _assert_add (clip, el);

  el = GES_TRACK_ELEMENT (ges_effect_new ("agingtv"));
  ges_track_element_set_track_type (el, GES_TRACK_TYPE_VIDEO);
  _assert_add (clip, el);

  el = GES_TRACK_ELEMENT (ges_effect_new ("videobalance"));
  ges_track_element_set_track_type (el, GES_TRACK_TYPE_VIDEO);
  _assert_add (clip, el);

  assert_num_children (clip, 5);
  CHECK_OBJECT_PROPS (clip, 0, 0, 10);

  i = j = 0;
  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = tmp->next) {
    el = tmp->data;
    type = ges_track_element_get_track_type (el);
    if (type == GES_TRACK_TYPE_AUDIO) {
      fail_unless (i < G_N_ELEMENTS (audio_els));
      audio_els[i].element = el;
      fail_unless (ges_track_element_get_track (el) == audio_track,
          "%s not in audio track", GES_TIMELINE_ELEMENT_NAME (el));
      if (i == 0)
        audio_els[i].higher_priority = NULL;
      else
        audio_els[i].higher_priority = audio_els[i - 1].element;
      _assert_higher_priority (el, audio_els[i].higher_priority);
      i++;
    }
    if (type == GES_TRACK_TYPE_VIDEO) {
      fail_unless (j < G_N_ELEMENTS (video_els));
      video_els[j].element = el;
      fail_unless (ges_track_element_get_track (el) == video_track,
          "%s not in video track", GES_TIMELINE_ELEMENT_NAME (el));
      if (j == 0)
        video_els[j].higher_priority = NULL;
      else
        video_els[j].higher_priority = video_els[j - 1].element;
      _assert_higher_priority (el, video_els[j].higher_priority);
      j++;
    }
  }
  fail_unless (i == G_N_ELEMENTS (audio_els));
  fail_unless (j == G_N_ELEMENTS (video_els));
  assert_num_in_track (audio_track, 2);
  assert_num_in_track (video_track, 3);

  /* group and ungroup should avoid track selection */
  g_signal_connect (timeline, "select-tracks-for-object",
      G_CALLBACK (_select_none), &selection_called);

  containers = ges_container_ungroup (GES_CONTAINER (clip), FALSE);

  fail_if (selection_called);

  video_clip = NULL;
  audio_clip = NULL;

  assert_equals_int (g_list_length (containers), 2);

  type = ges_clip_get_supported_formats (containers->data);
  if (type == GES_TRACK_TYPE_VIDEO)
    video_clip = containers->data;
  if (type == GES_TRACK_TYPE_AUDIO)
    audio_clip = containers->data;

  type = ges_clip_get_supported_formats (containers->next->data);
  if (type == GES_TRACK_TYPE_VIDEO)
    video_clip = containers->next->data;
  if (type == GES_TRACK_TYPE_AUDIO)
    audio_clip = containers->next->data;

  fail_unless (video_clip);
  fail_unless (audio_clip);
  fail_unless (video_clip == clip || audio_clip == clip);

  assert_layer (video_clip, layer);
  assert_num_children (video_clip, 3);
  fail_unless (GES_TIMELINE_ELEMENT_TIMELINE (video_clip) == timeline);
  CHECK_OBJECT_PROPS (video_clip, 0, 0, 10);
  ASSERT_OBJECT_REFCOUNT (video_clip, "1 for the layer + 1 for the timeline + "
      "1 in containers list", 3);

  assert_layer (audio_clip, layer);
  assert_num_children (audio_clip, 2);
  fail_unless (GES_TIMELINE_ELEMENT_TIMELINE (audio_clip) == timeline);
  CHECK_OBJECT_PROPS (audio_clip, 0, 0, 10);
  ASSERT_OBJECT_REFCOUNT (audio_clip, "1 for the layer + 1 for the timeline + "
      "1 in containers list", 3);

  for (i = 0; i < G_N_ELEMENTS (audio_els); i++) {
    el = audio_els[i].element;
    name = GES_TIMELINE_ELEMENT_NAME (el);
    fail_unless (ges_track_element_get_track (el) == audio_track,
        "%s not in audio track", name);
    fail_unless (GES_TIMELINE_ELEMENT_PARENT (el) ==
        GES_TIMELINE_ELEMENT (audio_clip), "%s not in the audio clip", name);
    ASSERT_OBJECT_REFCOUNT (el,
        "1 for the track + 1 for the container " "+ 1 for the timeline", 3);
    _assert_higher_priority (el, audio_els[i].higher_priority);
  }
  for (i = 0; i < G_N_ELEMENTS (video_els); i++) {
    el = video_els[i].element;
    name = GES_TIMELINE_ELEMENT_NAME (el);
    fail_unless (ges_track_element_get_track (el) == video_track,
        "%s not in video track", name);
    fail_unless (GES_TIMELINE_ELEMENT_PARENT (el) ==
        GES_TIMELINE_ELEMENT (video_clip), "%s not in the video clip", name);
    ASSERT_OBJECT_REFCOUNT (el,
        "1 for the track + 1 for the container " "+ 1 for the timeline", 3);
    _assert_higher_priority (el, video_els[i].higher_priority);
  }
  assert_num_in_track (audio_track, 2);
  assert_num_in_track (video_track, 3);

  assert_set_start (video_clip, 10);
  CHECK_OBJECT_PROPS (video_clip, 10, 0, 10);
  CHECK_OBJECT_PROPS (audio_clip, 0, 0, 10);

  _assert_regroup_fails (containers);

  assert_set_start (video_clip, 0);
  assert_set_inpoint (video_clip, 10);
  CHECK_OBJECT_PROPS (video_clip, 0, 10, 10);
  CHECK_OBJECT_PROPS (audio_clip, 0, 0, 10);

  _assert_regroup_fails (containers);

  assert_set_inpoint (video_clip, 0);
  assert_set_duration (video_clip, 15);
  CHECK_OBJECT_PROPS (video_clip, 0, 0, 15);
  CHECK_OBJECT_PROPS (audio_clip, 0, 0, 10);

  _assert_regroup_fails (containers);

  assert_set_duration (video_clip, 10);
  CHECK_OBJECT_PROPS (video_clip, 0, 0, 10);
  CHECK_OBJECT_PROPS (audio_clip, 0, 0, 10);

  regrouped_clip = ges_container_group (containers);

  fail_if (selection_called);

  assert_is_type (regrouped_clip, GES_TYPE_CLIP);
  assert_num_children (regrouped_clip, 5);
  assert_equals_int (ges_clip_get_supported_formats (GES_CLIP (regrouped_clip)),
      GES_TRACK_TYPE_VIDEO | GES_TRACK_TYPE_AUDIO);
  g_list_free_full (containers, gst_object_unref);

  assert_layer (regrouped_clip, layer);

  for (i = 0; i < G_N_ELEMENTS (audio_els); i++) {
    el = audio_els[i].element;
    name = GES_TIMELINE_ELEMENT_NAME (el);
    fail_unless (ges_track_element_get_track (el) == audio_track,
        "%s not in audio track", name);
    fail_unless (GES_TIMELINE_ELEMENT_PARENT (el) ==
        GES_TIMELINE_ELEMENT (regrouped_clip), "%s not in the regrouped clip",
        name);
    ASSERT_OBJECT_REFCOUNT (el,
        "1 for the track + 1 for the container " "+ 1 for the timeline", 3);
    _assert_higher_priority (el, audio_els[i].higher_priority);
  }
  for (i = 0; i < G_N_ELEMENTS (video_els); i++) {
    el = video_els[i].element;
    name = GES_TIMELINE_ELEMENT_NAME (el);
    fail_unless (ges_track_element_get_track (el) == video_track,
        "%s not in video track", name);
    fail_unless (GES_TIMELINE_ELEMENT_PARENT (el) ==
        GES_TIMELINE_ELEMENT (regrouped_clip), "%s not in the regrouped clip",
        name);
    ASSERT_OBJECT_REFCOUNT (el,
        "1 for the track + 1 for the container " "+ 1 for the timeline", 3);
    _assert_higher_priority (el, video_els[i].higher_priority);
  }
  assert_num_in_track (audio_track, 2);
  assert_num_in_track (video_track, 3);

  gst_object_unref (timeline);
  gst_object_unref (asset);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_clip_can_group)
{
  GESTimeline *timeline;
  GESLayer *layer1, *layer2;
  GESTrack *track1, *track2, *track3, *select_track;
  GESAsset *asset1, *asset2, *asset3;
  GESContainer *container;
  GESClip *clip1, *clip2, *clip3, *grouped;
  GList *clips = NULL;

  ges_init ();

  timeline = ges_timeline_new ();

  track1 = GES_TRACK (ges_audio_track_new ());
  track2 = GES_TRACK (ges_video_track_new ());
  track3 = GES_TRACK (ges_video_track_new ());

  fail_unless (ges_timeline_add_track (timeline, track1));
  fail_unless (ges_timeline_add_track (timeline, track2));

  layer1 = ges_timeline_append_layer (timeline);
  layer2 = ges_timeline_append_layer (timeline);

  asset1 = ges_asset_request (GES_TYPE_TEST_CLIP, NULL, NULL);
  asset2 = ges_asset_request (GES_TYPE_TEST_CLIP, "width=700", NULL);
  asset3 =
      ges_asset_request (GES_TYPE_EFFECT_CLIP, "audioecho || agingtv", NULL);

  /* fail if different layer */
  clip1 = ges_layer_add_asset (layer1, asset1, 0, 0, 10, GES_TRACK_TYPE_VIDEO);
  fail_unless (clip1);
  assert_num_children (clip1, 1);
  assert_num_in_track (track1, 0);
  assert_num_in_track (track2, 1);

  clip2 = ges_layer_add_asset (layer2, asset1, 0, 0, 10, GES_TRACK_TYPE_AUDIO);
  fail_unless (clip2);
  assert_num_children (clip2, 1);
  assert_num_in_track (track1, 1);
  assert_num_in_track (track2, 1);

  clips = g_list_append (clips, clip1);
  clips = g_list_append (clips, clip2);

  _assert_regroup_fails (clips);

  g_list_free (clips);
  clips = NULL;

  gst_object_ref (clip1);
  gst_object_ref (clip2);
  fail_unless (ges_layer_remove_clip (layer1, clip1));
  fail_unless (ges_layer_remove_clip (layer2, clip2));
  assert_num_children (clip1, 1);
  assert_num_children (clip2, 1);
  gst_object_unref (clip1);
  gst_object_unref (clip2);
  assert_num_in_track (track1, 0);
  assert_num_in_track (track2, 0);

  /* fail if different asset */
  clip1 = ges_layer_add_asset (layer1, asset1, 0, 0, 10, GES_TRACK_TYPE_VIDEO);
  fail_unless (clip1);
  assert_num_children (clip1, 1);

  clip2 = ges_layer_add_asset (layer1, asset2, 0, 0, 10, GES_TRACK_TYPE_AUDIO);
  fail_unless (clip2);
  assert_num_children (clip2, 1);
  assert_num_in_track (track1, 1);
  assert_num_in_track (track2, 1);

  clips = g_list_append (clips, clip1);
  clips = g_list_append (clips, clip2);

  _assert_regroup_fails (clips);

  g_list_free (clips);
  clips = NULL;

  fail_unless (ges_layer_remove_clip (layer1, clip1));
  fail_unless (ges_layer_remove_clip (layer1, clip2));

  /* fail if sharing track */
  clip1 = ges_layer_add_asset (layer1, asset3, 0, 0, 10, GES_TRACK_TYPE_VIDEO);
  fail_unless (clip1);
  assert_num_children (clip1, 1);

  clip2 = ges_layer_add_asset (layer1, asset3, 0, 0, 10, GES_TRACK_TYPE_VIDEO);
  fail_unless (clip2);
  assert_num_children (clip2, 1);
  assert_num_in_track (track1, 0);
  assert_num_in_track (track2, 2);

  clips = g_list_append (clips, clip1);
  clips = g_list_append (clips, clip2);

  _assert_regroup_fails (clips);

  g_list_free (clips);
  clips = NULL;

  fail_unless (ges_layer_remove_clip (layer1, clip1));
  fail_unless (ges_layer_remove_clip (layer1, clip2));

  clip1 = ges_layer_add_asset (layer1, asset1, 0, 0, 10, GES_TRACK_TYPE_VIDEO);
  fail_unless (clip1);
  assert_num_children (clip1, 1);
  assert_num_in_track (track1, 0);
  assert_num_in_track (track2, 1);

  clip2 = ges_layer_add_asset (layer1, asset2, 0, 0, 10, GES_TRACK_TYPE_AUDIO);
  fail_unless (clip2);
  assert_num_children (clip2, 1);
  assert_num_in_track (track1, 1);
  assert_num_in_track (track2, 1);

  clips = g_list_append (clips, clip1);
  clips = g_list_append (clips, clip2);

  _assert_regroup_fails (clips);

  g_list_free (clips);
  clips = NULL;

  fail_unless (ges_layer_remove_clip (layer1, clip1));
  fail_unless (ges_layer_remove_clip (layer1, clip2));

  /* can group if same asset but different tracks */
  clip1 = ges_layer_add_asset (layer1, asset2, 0, 0, 10, GES_TRACK_TYPE_VIDEO);
  fail_unless (clip1);
  _assert_add (clip1, ges_effect_new ("agingtv"));
  assert_num_children (clip1, 2);

  clip2 = ges_layer_add_asset (layer1, asset2, 0, 0, 10, GES_TRACK_TYPE_AUDIO);
  fail_unless (clip2);
  assert_num_children (clip2, 1);

  fail_unless (ges_timeline_add_track (timeline, track3));
  assert_num_children (clip1, 2);
  assert_num_children (clip2, 1);
  assert_num_in_track (track1, 1);
  assert_num_in_track (track2, 2);
  assert_num_in_track (track3, 0);

  select_track = track3;
  g_signal_connect (timeline, "select-tracks-for-object",
      G_CALLBACK (_select_track), &select_track);

  clip3 = ges_layer_add_asset (layer1, asset2, 0, 0, 10, GES_TRACK_TYPE_VIDEO);
  fail_unless (select_track == NULL);
  assert_num_children (clip1, 2);
  assert_num_children (clip2, 1);
  assert_num_children (clip3, 1);
  assert_num_in_track (track1, 1);
  assert_num_in_track (track2, 2);
  assert_num_in_track (track3, 1);

  clips = g_list_append (clips, clip1);
  clips = g_list_append (clips, clip2);
  clips = g_list_append (clips, clip3);

  container = ges_container_group (clips);

  fail_unless (GES_IS_CLIP (container));
  grouped = GES_CLIP (container);
  assert_num_children (grouped, 4);
  assert_num_in_track (track1, 1);
  assert_num_in_track (track2, 2);
  assert_num_in_track (track3, 1);

  fail_unless (ges_clip_get_supported_formats (grouped),
      GES_TRACK_TYPE_VIDEO | GES_TRACK_TYPE_AUDIO);
  fail_unless (ges_extractable_get_asset (GES_EXTRACTABLE (grouped))
      == asset2);
  CHECK_OBJECT_PROPS (grouped, 0, 0, 10);

  g_list_free (clips);

  clips = ges_layer_get_clips (layer1);
  fail_unless (g_list_length (clips), 1);
  fail_unless (GES_CLIP (clips->data) == grouped);
  g_list_free_full (clips, gst_object_unref);

  gst_object_unref (asset1);
  gst_object_unref (asset2);
  gst_object_unref (asset3);

  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_adding_children_to_track)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESTrack *track1, *track2;
  GESClip *clip, *clip2;
  GESAsset *asset;
  GESTrackElement *source, *effect, *effect2, *added, *added2, *added3;
  GstControlSource *ctrl_source;
  guint selection_called = 0;
  GError *error = NULL;

  ges_init ();

  timeline = ges_timeline_new ();
  ges_timeline_set_auto_transition (timeline, TRUE);
  track1 = GES_TRACK (ges_video_track_new ());
  track2 = GES_TRACK (ges_video_track_new ());

  /* only add two for now */
  fail_unless (ges_timeline_add_track (timeline, track1));

  layer = ges_timeline_append_layer (timeline);

  asset = ges_asset_request (GES_TYPE_TEST_CLIP, NULL, NULL);

  clip = ges_layer_add_asset (layer, asset, 0, 0, 10, GES_TRACK_TYPE_UNKNOWN);
  fail_unless (clip);
  assert_num_children (clip, 1);
  assert_num_in_track (track1, 1);
  assert_num_in_track (track2, 0);
  source = GES_CONTAINER_CHILDREN (clip)->data;
  fail_unless (ges_track_element_get_track (source) == track1);

  effect = GES_TRACK_ELEMENT (ges_effect_new ("agingtv"));
  _assert_add (clip, effect);
  effect2 = GES_TRACK_ELEMENT (ges_effect_new ("vertigotv"));
  _assert_add (clip, effect2);
  assert_num_children (clip, 3);
  assert_num_in_track (track1, 3);
  assert_num_in_track (track2, 0);
  fail_unless (ges_track_element_get_track (effect) == track1);
  fail_unless (ges_track_element_get_track (effect2) == track1);

  ges_timeline_element_set_child_properties (GES_TIMELINE_ELEMENT (clip),
      "font-desc", "Normal", "posx", 30, "posy", 50, "alpha", 0.1,
      "freq", 449.0, "scratch-lines", 2, NULL);

  ctrl_source = GST_CONTROL_SOURCE (gst_interpolation_control_source_new ());
  g_object_set (G_OBJECT (ctrl_source), "mode",
      GST_INTERPOLATION_MODE_CUBIC, NULL);
  fail_unless (gst_timed_value_control_source_set
      (GST_TIMED_VALUE_CONTROL_SOURCE (ctrl_source), 0, 20.0));
  fail_unless (gst_timed_value_control_source_set
      (GST_TIMED_VALUE_CONTROL_SOURCE (ctrl_source), 5, 45.0));
  fail_unless (ges_track_element_set_control_source (source, ctrl_source,
          "posx", "direct-absolute"));
  gst_object_unref (ctrl_source);

  ctrl_source = GST_CONTROL_SOURCE (gst_interpolation_control_source_new ());
  g_object_set (G_OBJECT (ctrl_source), "mode",
      GST_INTERPOLATION_MODE_LINEAR, NULL);
  fail_unless (gst_timed_value_control_source_set
      (GST_TIMED_VALUE_CONTROL_SOURCE (ctrl_source), 2, 0.1));
  fail_unless (gst_timed_value_control_source_set
      (GST_TIMED_VALUE_CONTROL_SOURCE (ctrl_source), 5, 0.7));
  fail_unless (gst_timed_value_control_source_set
      (GST_TIMED_VALUE_CONTROL_SOURCE (ctrl_source), 8, 0.3));
  fail_unless (ges_track_element_set_control_source (source, ctrl_source,
          "alpha", "direct"));
  gst_object_unref (ctrl_source);

  ctrl_source = GST_CONTROL_SOURCE (gst_interpolation_control_source_new ());
  g_object_set (G_OBJECT (ctrl_source), "mode",
      GST_INTERPOLATION_MODE_NONE, NULL);
  fail_unless (gst_timed_value_control_source_set
      (GST_TIMED_VALUE_CONTROL_SOURCE (ctrl_source), 0, 1.0));
  fail_unless (gst_timed_value_control_source_set
      (GST_TIMED_VALUE_CONTROL_SOURCE (ctrl_source), 4, 7.0));
  fail_unless (gst_timed_value_control_source_set
      (GST_TIMED_VALUE_CONTROL_SOURCE (ctrl_source), 8, 3.0));
  fail_unless (ges_track_element_set_control_source (effect, ctrl_source,
          "scratch-lines", "direct-absolute"));
  gst_object_unref (ctrl_source);

  /* can't add to a track that does not belong to the timeline */
  fail_if (ges_clip_add_child_to_track (clip, source, track2, &error));
  assert_num_children (clip, 3);
  fail_unless (ges_track_element_get_track (source) == track1);
  assert_num_in_track (track1, 3);
  assert_num_in_track (track2, 0);
  /* programming/usage error gives no error code/message */
  fail_if (error);

  /* can't add the clip to a track that already contains our source */
  fail_if (ges_clip_add_child_to_track (clip, source, track1, &error));
  assert_num_children (clip, 3);
  fail_unless (ges_track_element_get_track (source) == track1);
  assert_num_in_track (track1, 3);
  assert_num_in_track (track2, 0);
  fail_if (error);

  /* can't remove a core element from its track whilst a non-core sits
   * above it */
  fail_if (ges_track_remove_element (track1, source));
  assert_num_children (clip, 3);
  fail_unless (ges_track_element_get_track (source) == track1);
  assert_num_in_track (track1, 3);
  assert_num_in_track (track2, 0);

  /* can not add to the same track as it is currently in */
  fail_if (ges_clip_add_child_to_track (clip, effect, track1, &error));
  fail_unless (ges_track_element_get_track (effect) == track1);
  assert_num_in_track (track1, 3);
  assert_num_in_track (track2, 0);
  fail_if (error);

  /* adding another video track, select-tracks-for-object will do nothing
   * since no each track element is already part of a track */
  fail_unless (ges_timeline_add_track (timeline, track2));
  assert_num_children (clip, 3);
  assert_num_in_track (track1, 3);
  assert_num_in_track (track2, 0);

  /* can not add effect to a track that does not contain a core child */
  fail_if (ges_clip_add_child_to_track (clip, effect, track2, &error));
  assert_num_children (clip, 3);
  assert_num_in_track (track1, 3);
  assert_num_in_track (track2, 0);
  fail_if (error);

  /* can add core */

  added = ges_clip_add_child_to_track (clip, source, track2, &error);
  fail_unless (added);
  assert_num_children (clip, 4);
  fail_unless (added != source);
  fail_unless (ges_track_element_get_track (source) == track1);
  fail_unless (ges_track_element_get_track (added) == track2);
  assert_num_in_track (track1, 3);
  assert_num_in_track (track2, 1);
  fail_if (error);

  assert_equal_children_properties (added, source);
  assert_equal_bindings (added, source);

  /* can now add non-core */
  assert_equals_int (0,
      ges_clip_get_top_effect_index (clip, GES_BASE_EFFECT (effect)));
  assert_equals_int (1,
      ges_clip_get_top_effect_index (clip, GES_BASE_EFFECT (effect2)));

  added2 = ges_clip_add_child_to_track (clip, effect, track2, &error);
  fail_unless (added2);
  fail_if (error);
  assert_num_children (clip, 5);
  fail_unless (added2 != effect);
  fail_unless (ges_track_element_get_track (effect) == track1);
  fail_unless (ges_track_element_get_track (added2) == track2);
  assert_num_in_track (track1, 3);
  assert_num_in_track (track2, 2);

  assert_equal_children_properties (added2, effect);
  assert_equal_bindings (added2, effect);

  assert_equals_int (0,
      ges_clip_get_top_effect_index (clip, GES_BASE_EFFECT (effect)));
  assert_equals_int (1,
      ges_clip_get_top_effect_index (clip, GES_BASE_EFFECT (added2)));
  assert_equals_int (2,
      ges_clip_get_top_effect_index (clip, GES_BASE_EFFECT (effect2)));

  added3 = ges_clip_add_child_to_track (clip, effect2, track2, &error);
  fail_unless (added3);
  fail_if (error);
  assert_num_children (clip, 6);
  fail_unless (added3 != effect2);
  fail_unless (ges_track_element_get_track (effect2) == track1);
  fail_unless (ges_track_element_get_track (added3) == track2);
  assert_num_in_track (track1, 3);
  assert_num_in_track (track2, 3);

  assert_equal_children_properties (added3, effect2);
  assert_equal_bindings (added3, effect2);

  /* priorities within new track match that in previous track! */
  assert_equals_int (0,
      ges_clip_get_top_effect_index (clip, GES_BASE_EFFECT (effect)));
  assert_equals_int (1,
      ges_clip_get_top_effect_index (clip, GES_BASE_EFFECT (added2)));
  assert_equals_int (2,
      ges_clip_get_top_effect_index (clip, GES_BASE_EFFECT (effect2)));
  assert_equals_int (3,
      ges_clip_get_top_effect_index (clip, GES_BASE_EFFECT (added3)));

  /* removing core from the container, empties the non-core from their
   * tracks */
  gst_object_ref (added);
  _assert_remove (clip, added);
  assert_num_children (clip, 5);
  fail_unless (ges_track_element_get_track (source) == track1);
  fail_if (ges_track_element_get_track (added));
  fail_if (ges_track_element_get_track (added2));
  fail_unless (GES_TIMELINE_ELEMENT_PARENT (added) == NULL);
  fail_unless (GES_TIMELINE_ELEMENT_PARENT (added2) ==
      GES_TIMELINE_ELEMENT (clip));
  assert_num_in_track (track1, 3);
  assert_num_in_track (track2, 0);
  gst_object_unref (added);

  _assert_remove (clip, added2);
  _assert_remove (clip, added3);
  assert_num_children (clip, 3);
  assert_num_in_track (track1, 3);
  assert_num_in_track (track2, 0);

  /* remove from layer empties all children from the tracks */
  gst_object_ref (clip);

  fail_unless (ges_layer_remove_clip (layer, clip));
  assert_num_children (clip, 3);
  fail_if (ges_track_element_get_track (source));
  fail_if (ges_track_element_get_track (effect));
  assert_num_in_track (track1, 0);
  assert_num_in_track (track2, 0);

  /* add different sources to the layer */
  fail_unless (ges_layer_add_asset (layer, asset, 0, 0, 10,
          GES_TRACK_TYPE_UNKNOWN));
  fail_unless (ges_layer_add_asset (layer, asset, 20, 0, 10,
          GES_TRACK_TYPE_UNKNOWN));
  fail_unless (clip2 = ges_layer_add_asset (layer, asset, 25, 0, 10,
          GES_TRACK_TYPE_UNKNOWN));
  assert_num_children (clip2, 2);
  /* 3 sources + 1 transition */
  assert_num_in_track (track1, 4);
  assert_num_in_track (track2, 4);

  /* removing the track from the timeline empties it of track elements */
  gst_object_ref (track2);
  fail_unless (ges_timeline_remove_track (timeline, track2));
  /* but children remain in the clips */
  assert_num_children (clip2, 2);
  assert_num_in_track (track1, 4);
  assert_num_in_track (track2, 0);
  gst_object_unref (track2);

  /* add clip back in, but don't select any tracks */
  g_signal_connect (timeline, "select-tracks-for-object",
      G_CALLBACK (_select_none), &selection_called);

  /* can add the clip to the layer, despite a source existing between
   * 0 and 10 because the clip will not fill any track */
  /* NOTE: normally this would be useless because it would not trigger
   * the creation of any core children. But clip currently still has
   * its core children */
  fail_unless (ges_layer_add_clip (layer, clip));
  gst_object_unref (clip);

  /* one call for each child */
  assert_equals_int (selection_called, 3);

  fail_if (ges_track_element_get_track (source));
  fail_if (ges_track_element_get_track (effect));
  assert_num_children (clip, 3);
  assert_num_in_track (track1, 4);

  /* can not add the source to the track because it would overlap another
   * source */
  fail_if (ges_clip_add_child_to_track (clip, source, track1, &error));
  assert_num_children (clip, 3);
  assert_num_in_track (track1, 4);
  assert_GESError (error, GES_ERROR_INVALID_OVERLAP_IN_TRACK);

  /* can not add source at time 23 because it would result in three
   * overlapping sources in the track */
  assert_set_start (clip, 23);
  fail_if (ges_clip_add_child_to_track (clip, source, track1, &error));
  assert_num_children (clip, 3);
  assert_num_in_track (track1, 4);
  assert_GESError (error, GES_ERROR_INVALID_OVERLAP_IN_TRACK);

  /* can add at 5, with overlap */
  assert_set_start (clip, 5);
  added = ges_clip_add_child_to_track (clip, source, track1, &error);
  /* added is the source since it was not already in a track */
  fail_unless (added == source);
  fail_if (error);
  assert_num_children (clip, 3);
  /* 4 sources + 2 transitions */
  assert_num_in_track (track1, 6);

  /* also add effect */
  added = ges_clip_add_child_to_track (clip, effect, track1, &error);
  /* added is the source since it was not already in a track */
  fail_unless (added == effect);
  fail_if (error);
  assert_num_children (clip, 3);
  assert_num_in_track (track1, 7);

  added = ges_clip_add_child_to_track (clip, effect2, track1, &error);
  /* added is the source since it was not already in a track */
  fail_unless (added == effect2);
  fail_if (error);
  assert_num_children (clip, 3);
  assert_num_in_track (track1, 8);

  assert_equals_int (0,
      ges_clip_get_top_effect_index (clip, GES_BASE_EFFECT (effect)));
  assert_equals_int (1,
      ges_clip_get_top_effect_index (clip, GES_BASE_EFFECT (effect2)));

  gst_object_unref (timeline);
  gst_object_unref (asset);

  ges_deinit ();
}

GST_END_TEST;

static void
child_removed_cb (GESClip * clip, GESTimelineElement * effect,
    gboolean * called)
{
  ASSERT_OBJECT_REFCOUNT (effect, "1 test ref + 1 keeping alive ref + "
      "emission ref", 3);
  *called = TRUE;
}

GST_START_TEST (test_clip_refcount_remove_child)
{
  GESClip *clip;
  GESTrack *track;
  gboolean called;
  GESTrackElement *effect, *source;
  GESTimeline *timeline;
  GESLayer *layer;

  ges_init ();

  timeline = ges_timeline_new ();
  track = GES_TRACK (ges_audio_track_new ());
  fail_unless (ges_timeline_add_track (timeline, track));

  layer = ges_timeline_append_layer (timeline);
  clip = GES_CLIP (ges_test_clip_new ());
  fail_unless (ges_layer_add_clip (layer, clip));

  assert_num_children (clip, 1);
  assert_num_in_track (track, 1);

  source = GES_CONTAINER_CHILDREN (clip)->data;
  ASSERT_OBJECT_REFCOUNT (source, "1 for the container + 1 for the track"
      " + 1 timeline", 3);

  effect = GES_TRACK_ELEMENT (ges_effect_new ("identity"));
  fail_unless (ges_track_add_element (track, effect));
  assert_num_in_track (track, 2);
  ASSERT_OBJECT_REFCOUNT (effect, "1 for the track + 1 timeline", 2);

  _assert_add (clip, effect);
  assert_num_children (clip, 2);
  ASSERT_OBJECT_REFCOUNT (effect, "1 for the container + 1 for the track"
      " + 1 timeline", 3);

  fail_unless (ges_track_remove_element (track, effect));
  ASSERT_OBJECT_REFCOUNT (effect, "1 for the container", 1);

  g_signal_connect (clip, "child-removed", G_CALLBACK (child_removed_cb),
      &called);
  gst_object_ref (effect);
  _assert_remove (clip, effect);
  fail_unless (called == TRUE);
  ASSERT_OBJECT_REFCOUNT (effect, "1 test ref", 1);
  gst_object_unref (effect);

  check_destroyed (G_OBJECT (timeline), G_OBJECT (track),
      G_OBJECT (layer), G_OBJECT (clip), G_OBJECT (source), NULL);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_clip_find_track_element)
{
  GESClip *clip;
  GList *foundelements;
  GESTimeline *timeline;
  GESLayer *layer;
  GESTrack *track, *track1, *track2;
  guint selection_called = 0;

  GESTrackElement *effect, *effect1, *effect2, *foundelem, *video_source;

  ges_init ();

  track = GES_TRACK (ges_audio_track_new ());
  track1 = GES_TRACK (ges_audio_track_new ());
  track2 = GES_TRACK (ges_video_track_new ());

  timeline = ges_timeline_new ();
  fail_unless (ges_timeline_add_track (timeline, track));
  fail_unless (ges_timeline_add_track (timeline, track1));
  fail_unless (ges_timeline_add_track (timeline, track2));

  layer = ges_timeline_append_layer (timeline);
  clip = GES_CLIP (ges_test_clip_new ());

  /* should have a source in every track */
  fail_unless (ges_layer_add_clip (layer, clip));
  assert_num_children (clip, 3);
  assert_num_in_track (track, 1);
  assert_num_in_track (track1, 1);
  assert_num_in_track (track2, 1);

  g_signal_connect (timeline, "select-tracks-for-object",
      G_CALLBACK (_select_none), &selection_called);

  effect = GES_TRACK_ELEMENT (ges_effect_new ("audio identity"));
  fail_unless (ges_track_add_element (track, effect));
  _assert_add (clip, effect);

  effect1 = GES_TRACK_ELEMENT (ges_effect_new ("audio identity"));
  fail_unless (ges_track_add_element (track1, effect1));
  _assert_add (clip, effect1);

  effect2 = GES_TRACK_ELEMENT (ges_effect_new ("identity"));
  fail_unless (ges_track_add_element (track2, effect2));
  _assert_add (clip, effect2);

  fail_if (selection_called);
  assert_num_children (clip, 6);
  assert_num_in_track (track, 2);
  assert_num_in_track (track1, 2);
  assert_num_in_track (track2, 2);

  foundelem = ges_clip_find_track_element (clip, track, GES_TYPE_EFFECT);
  fail_unless (foundelem == effect);
  gst_object_unref (foundelem);

  foundelem = ges_clip_find_track_element (clip, track1, GES_TYPE_EFFECT);
  fail_unless (foundelem == effect1);
  gst_object_unref (foundelem);

  foundelem = ges_clip_find_track_element (clip, track2, GES_TYPE_EFFECT);
  fail_unless (foundelem == effect2);
  gst_object_unref (foundelem);

  foundelem = ges_clip_find_track_element (clip, NULL, GES_TYPE_TRANSITION);
  fail_unless (foundelem == NULL);

  foundelem = ges_clip_find_track_element (clip, track, GES_TYPE_TRANSITION);
  fail_unless (foundelem == NULL);

  foundelem = ges_clip_find_track_element (clip, track1, GES_TYPE_TRANSITION);
  fail_unless (foundelem == NULL);

  foundelem = ges_clip_find_track_element (clip, track2, GES_TYPE_TRANSITION);
  fail_unless (foundelem == NULL);

  foundelem = ges_clip_find_track_element (clip, track, GES_TYPE_SOURCE);
  fail_unless (GES_IS_AUDIO_TEST_SOURCE (foundelem));
  gst_object_unref (foundelem);

  foundelem = ges_clip_find_track_element (clip, track1, GES_TYPE_SOURCE);
  fail_unless (GES_IS_AUDIO_TEST_SOURCE (foundelem));
  gst_object_unref (foundelem);

  foundelem = ges_clip_find_track_element (clip, track2, GES_TYPE_SOURCE);
  fail_unless (GES_IS_VIDEO_TEST_SOURCE (foundelem));
  gst_object_unref (foundelem);

  video_source = ges_clip_find_track_element (clip, NULL,
      GES_TYPE_VIDEO_TEST_SOURCE);
  fail_unless (foundelem == video_source);
  gst_object_unref (video_source);


  foundelements = ges_clip_find_track_elements (clip, NULL,
      GES_TRACK_TYPE_AUDIO, G_TYPE_NONE);
  fail_unless_equals_int (g_list_length (foundelements), 4);
  g_list_free_full (foundelements, gst_object_unref);

  foundelements = ges_clip_find_track_elements (clip, NULL,
      GES_TRACK_TYPE_VIDEO, G_TYPE_NONE);
  fail_unless_equals_int (g_list_length (foundelements), 2);
  g_list_free_full (foundelements, gst_object_unref);

  foundelements = ges_clip_find_track_elements (clip, NULL,
      GES_TRACK_TYPE_UNKNOWN, GES_TYPE_SOURCE);
  fail_unless_equals_int (g_list_length (foundelements), 3);
  fail_unless (g_list_find (foundelements, video_source));
  g_list_free_full (foundelements, gst_object_unref);

  foundelements = ges_clip_find_track_elements (clip, NULL,
      GES_TRACK_TYPE_UNKNOWN, GES_TYPE_EFFECT);
  fail_unless_equals_int (g_list_length (foundelements), 3);
  fail_unless (g_list_find (foundelements, effect));
  fail_unless (g_list_find (foundelements, effect1));
  fail_unless (g_list_find (foundelements, effect2));
  g_list_free_full (foundelements, gst_object_unref);

  foundelements = ges_clip_find_track_elements (clip, NULL,
      GES_TRACK_TYPE_VIDEO, GES_TYPE_SOURCE);
  fail_unless_equals_int (g_list_length (foundelements), 1);
  fail_unless (foundelements->data == video_source);
  g_list_free_full (foundelements, gst_object_unref);

  foundelements = ges_clip_find_track_elements (clip, track2,
      GES_TRACK_TYPE_UNKNOWN, GES_TYPE_SOURCE);
  fail_unless_equals_int (g_list_length (foundelements), 1);
  fail_unless (foundelements->data == video_source);
  g_list_free_full (foundelements, gst_object_unref);

  foundelements = ges_clip_find_track_elements (clip, track2,
      GES_TRACK_TYPE_UNKNOWN, G_TYPE_NONE);
  fail_unless_equals_int (g_list_length (foundelements), 2);
  fail_unless (g_list_find (foundelements, effect2));
  fail_unless (g_list_find (foundelements, video_source));
  g_list_free_full (foundelements, gst_object_unref);

  foundelements = ges_clip_find_track_elements (clip, track1,
      GES_TRACK_TYPE_UNKNOWN, GES_TYPE_EFFECT);
  fail_unless_equals_int (g_list_length (foundelements), 1);
  fail_unless (foundelements->data == effect1);
  g_list_free_full (foundelements, gst_object_unref);

  /* NOTE: search in *either* track or track type
   * TODO 2.0: this should be an AND condition, rather than OR */
  foundelements = ges_clip_find_track_elements (clip, track,
      GES_TRACK_TYPE_VIDEO, G_TYPE_NONE);
  fail_unless_equals_int (g_list_length (foundelements), 4);
  fail_unless (g_list_find (foundelements, effect));
  fail_unless (g_list_find (foundelements, effect2));
  fail_unless (g_list_find (foundelements, video_source));
  g_list_free_full (foundelements, gst_object_unref);

  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_effects_priorities)
{
  GESClip *clip;
  GESTimeline *timeline;
  GESTrack *audio_track, *video_track;
  GESLayer *layer, *layer1;

  GESTrackElement *effect, *effect1, *effect2;

  ges_init ();

  clip = GES_CLIP (ges_test_clip_new ());
  audio_track = GES_TRACK (ges_audio_track_new ());
  video_track = GES_TRACK (ges_video_track_new ());

  timeline = ges_timeline_new ();
  fail_unless (ges_timeline_add_track (timeline, audio_track));
  fail_unless (ges_timeline_add_track (timeline, video_track));

  layer = ges_timeline_append_layer (timeline);
  layer1 = ges_timeline_append_layer (timeline);

  ges_layer_add_clip (layer, clip);

  effect = GES_TRACK_ELEMENT (ges_effect_new ("agingtv"));
  _assert_add (clip, effect);

  effect1 = GES_TRACK_ELEMENT (ges_effect_new ("agingtv"));
  _assert_add (clip, effect1);

  effect2 = GES_TRACK_ELEMENT (ges_effect_new ("agingtv"));
  _assert_add (clip, effect2);

  fail_unless_equals_int (MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 0,
      _PRIORITY (effect));
  fail_unless_equals_int (MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 1,
      _PRIORITY (effect1));
  fail_unless_equals_int (MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 2,
      _PRIORITY (effect2));

  fail_unless (ges_clip_set_top_effect_index (clip, GES_BASE_EFFECT (effect),
          2));
  fail_unless_equals_int (MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 0,
      _PRIORITY (effect1));
  fail_unless_equals_int (MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 1,
      _PRIORITY (effect2));
  fail_unless_equals_int (MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 2,
      _PRIORITY (effect));

  fail_unless (ges_clip_set_top_effect_index (clip, GES_BASE_EFFECT (effect),
          0));
  fail_unless_equals_int (MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 0,
      _PRIORITY (effect));
  fail_unless_equals_int (MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 1,
      _PRIORITY (effect1));
  fail_unless_equals_int (MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 2,
      _PRIORITY (effect2));

  fail_unless (ges_clip_move_to_layer (clip, layer1));
  fail_unless_equals_int (LAYER_HEIGHT + MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 0,
      _PRIORITY (effect));
  fail_unless_equals_int (LAYER_HEIGHT + MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 1,
      _PRIORITY (effect1));
  fail_unless_equals_int (LAYER_HEIGHT + MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 2,
      _PRIORITY (effect2));

  fail_unless (ges_clip_set_top_effect_index (clip, GES_BASE_EFFECT (effect),
          2));
  fail_unless_equals_int (LAYER_HEIGHT + MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 0,
      _PRIORITY (effect1));
  fail_unless_equals_int (LAYER_HEIGHT + MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 1,
      _PRIORITY (effect2));
  fail_unless_equals_int (LAYER_HEIGHT + MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 2,
      _PRIORITY (effect));

  fail_unless (ges_clip_set_top_effect_index (clip, GES_BASE_EFFECT (effect),
          0));
  fail_unless_equals_int (LAYER_HEIGHT + MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 0,
      _PRIORITY (effect));
  fail_unless_equals_int (LAYER_HEIGHT + MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 1,
      _PRIORITY (effect1));
  fail_unless_equals_int (LAYER_HEIGHT + MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 2,
      _PRIORITY (effect2));

  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

static void
_count_cb (GObject * obj, GParamSpec * pspec, gint * count)
{
  *count = *count + 1;
}

#define _assert_children_time_setter(clip, child, prop, setter, val1, val2) \
{ \
  gint clip_count = 0; \
  gint child_count = 0; \
  gchar *notify_name = g_strconcat ("notify::", prop, NULL); \
  gchar *clip_name = GES_TIMELINE_ELEMENT_NAME (clip); \
  gchar *child_name = NULL; \
  g_signal_connect (clip, notify_name, G_CALLBACK (_count_cb), \
      &clip_count); \
  if (child) { \
    child_name = GES_TIMELINE_ELEMENT_NAME (child); \
    g_signal_connect (child, notify_name, G_CALLBACK (_count_cb), \
        &child_count); \
  } \
  \
  fail_unless (setter (GES_TIMELINE_ELEMENT (clip), val1), \
      "Failed to set the %s property for clip %s", prop, clip_name); \
  assert_clip_children_time_val (clip, prop, val1); \
  \
  fail_unless (clip_count == 1, "The callback for the %s property was " \
      "called %i times for clip %s, rather than once", \
      prop, clip_count, clip_name); \
  if (child) { \
    fail_unless (child_count == 1, "The callback for the %s property " \
        "was called %i times for the child %s of clip %s, rather than " \
        "once", prop, child_count, child_name, clip_name); \
  } \
  \
  clip_count = 0; \
  if (child) { \
    child_count = 0; \
    fail_unless (setter (GES_TIMELINE_ELEMENT (child), val2), \
        "Failed to set the %s property for the child %s of clip %s", \
        prop, child_name, clip_name); \
    fail_unless (child_count == 1, "The callback for the %s property " \
        "was called %i more times for the child %s of clip %s, rather " \
        "than once more", prop, child_count, child_name, clip_name); \
  } else { \
    fail_unless (setter (GES_TIMELINE_ELEMENT (clip), val2), \
      "Failed to set the %s property for clip %s", prop, clip_name); \
  } \
  assert_clip_children_time_val (clip, prop, val2); \
  \
  fail_unless (clip_count == 1, "The callback for the %s property " \
      "was called %i more times for clip %s, rather than once more", \
      prop, clip_count, clip_name); \
  assert_equals_int (g_signal_handlers_disconnect_by_func (clip, \
          G_CALLBACK (_count_cb), &clip_count), 1); \
  if (child) { \
    assert_equals_int (g_signal_handlers_disconnect_by_func (child, \
            G_CALLBACK (_count_cb), &child_count), 1); \
  } \
  \
  g_free (notify_name); \
}

static void
_test_children_time_setting_on_clip (GESClip * clip, GESTrackElement * child)
{
  _assert_children_time_setter (clip, child, "in-point",
      ges_timeline_element_set_inpoint, 11, 101);
  _assert_children_time_setter (clip, child, "in-point",
      ges_timeline_element_set_inpoint, 51, 1);
  _assert_children_time_setter (clip, child, "start",
      ges_timeline_element_set_start, 12, 102);
  _assert_children_time_setter (clip, child, "start",
      ges_timeline_element_set_start, 52, 2);
  _assert_children_time_setter (clip, child, "duration",
      ges_timeline_element_set_duration, 13, 103);
  _assert_children_time_setter (clip, child, "duration",
      ges_timeline_element_set_duration, 53, 3);
}

GST_START_TEST (test_children_time_setters)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESClip *clips[] = { NULL, NULL };
  gint i;

  ges_init ();

  timeline = ges_timeline_new_audio_video ();
  fail_unless (timeline);

  layer = ges_timeline_append_layer (timeline);

  clips[0] =
      GES_CLIP (ges_transition_clip_new
      (GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE));
  clips[1] = GES_CLIP (ges_test_clip_new ());

  for (i = 0; i < G_N_ELEMENTS (clips); i++) {
    GESClip *clip = clips[i];
    GESContainer *group = GES_CONTAINER (ges_group_new ());
    GList *children;
    GESTrackElement *child;
    /* no children */
    _test_children_time_setting_on_clip (clip, NULL);
    /* child in timeline */
    fail_unless (ges_layer_add_clip (layer, clip));
    children = GES_CONTAINER_CHILDREN (clip);
    fail_unless (children);
    child = GES_TRACK_ELEMENT (children->data);
    /* make sure the child can have its in-point set */
    ges_track_element_set_has_internal_source (child, TRUE);
    _test_children_time_setting_on_clip (clip, child);
    /* clip in a group */
    _assert_add (group, clip);
    _test_children_time_setting_on_clip (clip, child);
    /* group is removed from the timeline and destroyed when empty */
    _assert_remove (group, clip);
    /* child not in timeline */
    gst_object_ref (clip);
    fail_unless (ges_layer_remove_clip (layer, clip));
    children = GES_CONTAINER_CHILDREN (clip);
    fail_unless (children);
    child = GES_TRACK_ELEMENT (children->data);
    ges_track_element_set_has_internal_source (child, TRUE);
    _test_children_time_setting_on_clip (clip, child);
    gst_object_unref (clip);
  }
  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_not_enough_internal_content_for_core)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESAsset *asset;
  GError *error = NULL;

  ges_init ();

  timeline = ges_timeline_new_audio_video ();
  layer = ges_timeline_append_layer (timeline);

  asset = ges_asset_request (GES_TYPE_TEST_CLIP, "max-duration=30", NULL);
  fail_unless (asset);

  fail_if (ges_layer_add_asset_full (layer, asset, 0, 31, 10,
          GES_TRACK_TYPE_UNKNOWN, &error));
  assert_GESError (error, GES_ERROR_NOT_ENOUGH_INTERNAL_CONTENT);

  gst_object_unref (timeline);
  gst_object_unref (asset);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_can_add_effect)
{
  struct CanAddEffectData
  {
    GESClip *clip;
    gboolean can_add_effect;
  } clips[6];
  guint i;
  gchar *uri;

  ges_init ();

  uri = ges_test_get_audio_video_uri ();

  clips[0] = (struct CanAddEffectData) {
    GES_CLIP (ges_test_clip_new ()), TRUE
  };
  clips[1] = (struct CanAddEffectData) {
    GES_CLIP (ges_uri_clip_new (uri)), TRUE
  };
  clips[2] = (struct CanAddEffectData) {
    GES_CLIP (ges_title_clip_new ()), TRUE
  };
  clips[3] = (struct CanAddEffectData) {
    GES_CLIP (ges_effect_clip_new ("agingtv", "audioecho")), TRUE
  };
  clips[4] = (struct CanAddEffectData) {
    GES_CLIP (ges_transition_clip_new
        (GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE)), FALSE
  };
  clips[5] = (struct CanAddEffectData) {
    GES_CLIP (ges_text_overlay_clip_new ()), FALSE
  };

  g_free (uri);

  for (i = 0; i < G_N_ELEMENTS (clips); i++) {
    GESClip *clip = clips[i].clip;
    GESTimelineElement *effect =
        GES_TIMELINE_ELEMENT (ges_effect_new ("agingtv"));
    gst_object_ref_sink (effect);
    gst_object_ref_sink (clip);
    fail_unless (clip);
    if (clips[i].can_add_effect)
      fail_unless (ges_container_add (GES_CONTAINER (clip), effect),
          "Could not add an effect to clip %s",
          GES_TIMELINE_ELEMENT_NAME (clip));
    else
      fail_if (ges_container_add (GES_CONTAINER (clip), effect),
          "Could add an effect to clip %s, but we expect this to fail",
          GES_TIMELINE_ELEMENT_NAME (clip));
    gst_object_unref (effect);
    gst_object_unref (clip);
  }

  ges_deinit ();
}

GST_END_TEST;

#define _assert_active(el, active) \
  fail_unless (ges_track_element_is_active (el) == active)

#define _assert_set_active(el, active) \
  fail_unless (ges_track_element_set_active (el, active))

GST_START_TEST (test_children_active)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESClip *clip;
  GESTrack *track0, *track1, *select_track;
  GESTrackElement *effect0, *effect1, *effect2, *effect3;
  GESTrackElement *source0, *source1;

  ges_init ();

  timeline = ges_timeline_new ();

  track0 = GES_TRACK (ges_video_track_new ());
  track1 = GES_TRACK (ges_video_track_new ());

  fail_unless (ges_timeline_add_track (timeline, track0));
  fail_unless (ges_timeline_add_track (timeline, track1));

  layer = ges_timeline_append_layer (timeline);

  clip = GES_CLIP (ges_test_clip_new ());

  fail_unless (ges_layer_add_clip (layer, clip));

  assert_num_children (clip, 2);

  source0 =
      ges_clip_find_track_element (clip, track0, GES_TYPE_VIDEO_TEST_SOURCE);
  source1 =
      ges_clip_find_track_element (clip, track1, GES_TYPE_VIDEO_TEST_SOURCE);

  fail_unless (source0);
  fail_unless (source1);

  gst_object_unref (source0);
  gst_object_unref (source1);

  _assert_active (source0, TRUE);
  _assert_active (source1, TRUE);

  _assert_set_active (source0, FALSE);

  _assert_active (source0, FALSE);
  _assert_active (source1, TRUE);

  select_track = track0;
  g_signal_connect (timeline, "select-tracks-for-object",
      G_CALLBACK (_select_track), &select_track);

  /* add an active effect should become inactive to match the core */
  effect0 = GES_TRACK_ELEMENT (ges_effect_new ("videobalance"));
  _assert_active (effect0, TRUE);

  _assert_add (clip, effect0);
  fail_if (select_track);

  _assert_active (source0, FALSE);
  _assert_active (effect0, FALSE);
  _assert_active (source1, TRUE);

  /* adding inactive to track with inactive core does nothing */
  effect1 = GES_TRACK_ELEMENT (ges_effect_new ("vertigotv"));
  _assert_active (effect1, TRUE);
  _assert_set_active (effect1, FALSE);
  _assert_active (effect1, FALSE);

  select_track = track0;
  _assert_add (clip, effect1);
  fail_if (select_track);

  _assert_active (source0, FALSE);
  _assert_active (effect0, FALSE);
  _assert_active (effect1, FALSE);
  _assert_active (source1, TRUE);

  /* adding active to track with active core does nothing */
  effect2 = GES_TRACK_ELEMENT (ges_effect_new ("agingtv"));
  _assert_active (effect2, TRUE);

  select_track = track1;
  _assert_add (clip, effect2);
  fail_if (select_track);

  _assert_active (source0, FALSE);
  _assert_active (effect0, FALSE);
  _assert_active (effect1, FALSE);
  _assert_active (source1, TRUE);
  _assert_active (effect2, TRUE);

  /* adding inactive to track with active core does nothing */
  effect3 = GES_TRACK_ELEMENT (ges_effect_new ("alpha"));
  _assert_active (effect3, TRUE);
  _assert_set_active (effect3, FALSE);
  _assert_active (effect3, FALSE);

  select_track = track1;
  _assert_add (clip, effect3);
  fail_if (select_track);

  _assert_active (source0, FALSE);
  _assert_active (effect0, FALSE);
  _assert_active (effect1, FALSE);
  _assert_active (source1, TRUE);
  _assert_active (effect2, TRUE);
  _assert_active (effect3, FALSE);

  /* activate a core does not change non-core */
  _assert_set_active (source0, TRUE);

  _assert_active (source0, TRUE);
  _assert_active (effect0, FALSE);
  _assert_active (effect1, FALSE);
  _assert_active (source1, TRUE);
  _assert_active (effect2, TRUE);
  _assert_active (effect3, FALSE);

  /* but de-activating a core will de-activate the non-core */
  _assert_set_active (source1, FALSE);

  _assert_active (source0, TRUE);
  _assert_active (effect0, FALSE);
  _assert_active (effect1, FALSE);
  _assert_active (source1, FALSE);
  _assert_active (effect2, FALSE);
  _assert_active (effect3, FALSE);

  /* activate a non-core will activate the core */
  _assert_set_active (effect3, TRUE);

  _assert_active (source0, TRUE);
  _assert_active (effect0, FALSE);
  _assert_active (effect1, FALSE);
  _assert_active (source1, TRUE);
  _assert_active (effect2, FALSE);
  _assert_active (effect3, TRUE);

  /* if core is already active, nothing else happens */
  _assert_set_active (effect0, TRUE);

  _assert_active (source0, TRUE);
  _assert_active (effect0, TRUE);
  _assert_active (effect1, FALSE);
  _assert_active (source1, TRUE);
  _assert_active (effect2, FALSE);
  _assert_active (effect3, TRUE);

  _assert_set_active (effect1, TRUE);

  _assert_active (source0, TRUE);
  _assert_active (effect0, TRUE);
  _assert_active (effect1, TRUE);
  _assert_active (source1, TRUE);
  _assert_active (effect2, FALSE);
  _assert_active (effect3, TRUE);

  _assert_set_active (effect2, TRUE);

  _assert_active (source0, TRUE);
  _assert_active (effect0, TRUE);
  _assert_active (effect1, TRUE);
  _assert_active (source1, TRUE);
  _assert_active (effect2, TRUE);
  _assert_active (effect3, TRUE);

  /* de-activate a core will de-active all the non-core */
  _assert_set_active (source0, FALSE);

  _assert_active (source0, FALSE);
  _assert_active (effect0, FALSE);
  _assert_active (effect1, FALSE);
  _assert_active (source1, TRUE);
  _assert_active (effect2, TRUE);
  _assert_active (effect3, TRUE);

  /* de-activate a non-core does nothing else */
  _assert_set_active (effect3, FALSE);

  _assert_active (source0, FALSE);
  _assert_active (effect0, FALSE);
  _assert_active (effect1, FALSE);
  _assert_active (source1, TRUE);
  _assert_active (effect2, TRUE);
  _assert_active (effect3, FALSE);

  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_children_inpoint)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESTimelineElement *clip, *child0, *child1, *effect;
  GList *children;

  ges_init ();

  timeline = ges_timeline_new_audio_video ();
  fail_unless (timeline);

  layer = ges_timeline_append_layer (timeline);

  clip = GES_TIMELINE_ELEMENT (ges_test_clip_new ());

  assert_set_start (clip, 5);
  assert_set_duration (clip, 20);
  assert_set_inpoint (clip, 30);

  CHECK_OBJECT_PROPS (clip, 5, 30, 20);

  fail_unless (ges_layer_add_clip (layer, GES_CLIP (clip)));

  /* clip now has children */
  children = GES_CONTAINER_CHILDREN (clip);
  fail_unless (children);
  child0 = children->data;
  fail_unless (children->next);
  child1 = children->next->data;
  fail_unless (children->next->next == NULL);

  fail_unless (ges_track_element_has_internal_source (GES_TRACK_ELEMENT
          (child0)));
  fail_unless (ges_track_element_has_internal_source (GES_TRACK_ELEMENT
          (child1)));

  CHECK_OBJECT_PROPS (clip, 5, 30, 20);
  CHECK_OBJECT_PROPS (child0, 5, 30, 20);
  CHECK_OBJECT_PROPS (child1, 5, 30, 20);

  /* add a non-core element */
  effect = GES_TIMELINE_ELEMENT (ges_effect_new ("agingtv"));
  fail_if (ges_track_element_has_internal_source (GES_TRACK_ELEMENT (effect)));
  /* allow us to choose our own in-point */
  ges_track_element_set_has_internal_source (GES_TRACK_ELEMENT (effect), TRUE);
  assert_set_start (effect, 104);
  assert_set_duration (effect, 53);
  assert_set_inpoint (effect, 67);

  /* adding the effect will change its start and duration, but not its
   * in-point */
  _assert_add (clip, effect);

  CHECK_OBJECT_PROPS (clip, 5, 30, 20);
  CHECK_OBJECT_PROPS (child0, 5, 30, 20);
  CHECK_OBJECT_PROPS (child1, 5, 30, 20);
  CHECK_OBJECT_PROPS (effect, 5, 67, 20);

  /* register child0 as having no internal source, which means its
   * in-point will be set to 0 */
  ges_track_element_set_has_internal_source (GES_TRACK_ELEMENT (child0), FALSE);

  CHECK_OBJECT_PROPS (clip, 5, 30, 20);
  CHECK_OBJECT_PROPS (child0, 5, 0, 20);
  CHECK_OBJECT_PROPS (child1, 5, 30, 20);
  CHECK_OBJECT_PROPS (effect, 5, 67, 20);

  /* should not be able to set the in-point to non-zero */
  assert_fail_set_inpoint (child0, 40);

  CHECK_OBJECT_PROPS (clip, 5, 30, 20);
  CHECK_OBJECT_PROPS (child0, 5, 0, 20);
  CHECK_OBJECT_PROPS (child1, 5, 30, 20);
  CHECK_OBJECT_PROPS (effect, 5, 67, 20);

  /* when we set the in-point on a core-child with an internal source we
   * also set the clip and siblings with the same features */
  assert_set_inpoint (child1, 50);

  CHECK_OBJECT_PROPS (clip, 5, 50, 20);
  /* child with no internal source not changed */
  CHECK_OBJECT_PROPS (child0, 5, 0, 20);
  CHECK_OBJECT_PROPS (child1, 5, 50, 20);
  /* non-core no changed */
  CHECK_OBJECT_PROPS (effect, 5, 67, 20);

  /* setting back to having internal source will put in sync with the
   * in-point of the clip */
  ges_track_element_set_has_internal_source (GES_TRACK_ELEMENT (child0), TRUE);

  CHECK_OBJECT_PROPS (clip, 5, 50, 20);
  CHECK_OBJECT_PROPS (child0, 5, 50, 20);
  CHECK_OBJECT_PROPS (child1, 5, 50, 20);
  CHECK_OBJECT_PROPS (effect, 5, 67, 20);

  assert_set_inpoint (child0, 40);

  CHECK_OBJECT_PROPS (clip, 5, 40, 20);
  CHECK_OBJECT_PROPS (child0, 5, 40, 20);
  CHECK_OBJECT_PROPS (child1, 5, 40, 20);
  CHECK_OBJECT_PROPS (effect, 5, 67, 20);

  /* setting in-point on effect shouldn't change any other siblings */
  assert_set_inpoint (effect, 77);

  CHECK_OBJECT_PROPS (clip, 5, 40, 20);
  CHECK_OBJECT_PROPS (child0, 5, 40, 20);
  CHECK_OBJECT_PROPS (child1, 5, 40, 20);
  CHECK_OBJECT_PROPS (effect, 5, 77, 20);

  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_children_max_duration)
{
  GESTimeline *timeline;
  GESLayer *layer;
  gchar *uri;
  GESTimelineElement *child0, *child1, *effect;
  guint i;
  GstClockTime max_duration, new_max;
  GList *children;
  struct
  {
    GESTimelineElement *clip;
    GstClockTime max_duration;
  } clips[] = {
    {
        NULL, GST_SECOND}, {
        NULL, GST_CLOCK_TIME_NONE}
  };

  ges_init ();

  timeline = ges_timeline_new_audio_video ();
  fail_unless (timeline);

  layer = ges_timeline_append_layer (timeline);

  uri = ges_test_get_audio_video_uri ();
  clips[0].clip = GES_TIMELINE_ELEMENT (ges_uri_clip_new (uri));
  fail_unless (clips[0].clip);
  g_free (uri);

  clips[1].clip = GES_TIMELINE_ELEMENT (ges_test_clip_new ());

  for (i = 0; i < G_N_ELEMENTS (clips); i++) {
    GESTimelineElement *clip = clips[i].clip;

    max_duration = clips[i].max_duration;
    fail_unless_equals_uint64 (_MAX_DURATION (clip), max_duration);
    assert_set_start (clip, 5);
    assert_set_duration (clip, 20);
    assert_set_inpoint (clip, 30);

    /* can set the max duration the clip to anything whilst it has
     * no core child */
    assert_set_max_duration (clip, 150);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, 150);

    /* add a non-core element */
    effect = GES_TIMELINE_ELEMENT (ges_effect_new ("agingtv"));
    fail_if (ges_track_element_has_internal_source (GES_TRACK_ELEMENT
            (effect)));
    /* allow us to choose our own max-duration */
    ges_track_element_set_has_internal_source (GES_TRACK_ELEMENT (effect),
        TRUE);
    assert_set_start (effect, 104);
    assert_set_duration (effect, 53);
    assert_set_max_duration (effect, 400);

    /* adding the effect will change its start and duration, but not its
     * max-duration (or in-point) */
    _assert_add (clip, effect);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, 150);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    /* only non-core, so can still set the max-duration */
    assert_set_max_duration (clip, 200);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, 200);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    /* removing should not change the max-duration we set on the clip */
    gst_object_ref (effect);
    _assert_remove (clip, effect);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, 200);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    _assert_add (clip, effect);
    gst_object_unref (effect);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, 200);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    /* now add to a layer to create the core children */
    fail_unless (ges_layer_add_clip (layer, GES_CLIP (clip)));

    children = GES_CONTAINER_CHILDREN (clip);
    fail_unless (children);
    fail_unless (GES_TIMELINE_ELEMENT (children->data) == effect);
    fail_unless (children->next);
    child0 = children->next->data;
    fail_unless (children->next->next);
    child1 = children->next->next->data;
    fail_unless (children->next->next->next == NULL);

    fail_unless (ges_track_element_has_internal_source (GES_TRACK_ELEMENT
            (child0)));
    fail_unless (ges_track_element_has_internal_source (GES_TRACK_ELEMENT
            (child1)));

    if (GES_IS_URI_CLIP (clip))
      new_max = max_duration;
    else
      /* need a valid clock time that is not too large */
      new_max = 500;

    /* added children do not change the clip's max-duration, but will
     * instead set it to the minimum value of its children */
    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, max_duration);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, max_duration);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, max_duration);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    /* when setting max_duration of core children, clip will take the
     * minimum value */
    assert_set_max_duration (child0, new_max - 1);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, new_max - 1);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, new_max - 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, max_duration);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    assert_set_max_duration (child1, new_max - 2);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, new_max - 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    assert_set_max_duration (child0, new_max + 1);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, new_max + 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    /* can not set in-point above max_duration, nor max_duration below
     * in-point */

    assert_fail_set_max_duration (child0, 29);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, new_max + 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    assert_fail_set_max_duration (child1, 29);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, new_max + 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    assert_fail_set_max_duration (clip, 29);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, new_max + 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    /* can't set the inpoint to (new_max), even though it is lower than
     * our own max-duration (new_max + 1) because it is too high for our
     * sibling child1 */
    assert_fail_set_inpoint (child0, new_max);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, new_max + 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    assert_fail_set_inpoint (child1, new_max);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, new_max + 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    assert_fail_set_inpoint (clip, new_max);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, new_max + 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    /* setting below new_max is ok */
    assert_set_inpoint (child0, 15);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 15, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 15, 20, new_max + 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 15, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    assert_set_inpoint (child1, 25);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 25, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 25, 20, new_max + 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 25, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    assert_set_inpoint (clip, 30);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, new_max + 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    /* non-core has no effect */
    assert_set_max_duration (effect, new_max + 500);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, new_max + 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, new_max + 500);

    /* can set the in-point of non-core to be higher than the max_duration
     * of the clip */
    assert_set_inpoint (effect, new_max + 2);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, new_max + 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (effect, 5, new_max + 2, 20, new_max + 500);

    /* but not higher than our own */
    assert_fail_set_inpoint (effect, new_max + 501);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, new_max + 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (effect, 5, new_max + 2, 20, new_max + 500);

    assert_fail_set_max_duration (effect, new_max + 1);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, new_max + 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (effect, 5, new_max + 2, 20, new_max + 500);

    assert_set_inpoint (effect, 0);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, new_max + 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, new_max + 500);

    assert_set_max_duration (effect, 400);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, new_max + 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    /* setting on the clip will set all the core children to the same
     * value */
    assert_set_max_duration (clip, 180);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, 180);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, 180);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, 180);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    /* register child0 as having no internal source, which means its
     * in-point will be set to 0 and max-duration set to
     * GST_CLOCK_TIME_NONE */
    ges_track_element_set_has_internal_source (GES_TRACK_ELEMENT (child0),
        FALSE);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, 180);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 0, 20, GST_CLOCK_TIME_NONE);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, 180);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    /* should not be able to set the max-duration to a valid time */
    assert_fail_set_max_duration (child0, 40);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, 180);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 0, 20, GST_CLOCK_TIME_NONE);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, 180);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    /* same with child1 */
    /* clock time of the clip should now be GST_CLOCK_TIME_NONE */
    ges_track_element_set_has_internal_source (GES_TRACK_ELEMENT (child1),
        FALSE);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, GST_CLOCK_TIME_NONE);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 0, 20, GST_CLOCK_TIME_NONE);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 0, 20, GST_CLOCK_TIME_NONE);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    /* should not be able to set the max of the clip to anything else
     * when it has no core children with an internal source */
    assert_fail_set_max_duration (clip, 150);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, GST_CLOCK_TIME_NONE);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 0, 20, GST_CLOCK_TIME_NONE);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 0, 20, GST_CLOCK_TIME_NONE);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    /* setting back to having an internal source will not immediately
     * change the max-duration (unlike in-point) */
    ges_track_element_set_has_internal_source (GES_TRACK_ELEMENT (child0),
        TRUE);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, GST_CLOCK_TIME_NONE);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, GST_CLOCK_TIME_NONE);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 0, 20, GST_CLOCK_TIME_NONE);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    /* can now set the max-duration, which will effect the clip */
    assert_set_max_duration (child0, 140);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, 140);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, 140);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 0, 20, GST_CLOCK_TIME_NONE);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    ges_track_element_set_has_internal_source (GES_TRACK_ELEMENT (child1),
        TRUE);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, 140);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, 140);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, GST_CLOCK_TIME_NONE);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    assert_set_max_duration (child1, 130);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, 130);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, 140);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, 130);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    /* removing a child may change the max_duration of the clip */
    gst_object_ref (child0);
    gst_object_ref (child1);
    gst_object_ref (effect);

    /* removing non-core does nothing */
    _assert_remove (clip, effect);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, 130);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, 140);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, 130);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    /* new minimum max-duration for the clip when we remove child1 */
    _assert_remove (clip, child1);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, 140);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, 140);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, 130);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    /* with no core-children, the max-duration of the clip is set to
     * GST_CLOCK_TIME_NONE */
    _assert_remove (clip, child0);

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, GST_CLOCK_TIME_NONE);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, 140);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, 130);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    fail_unless (ges_layer_remove_clip (layer, GES_CLIP (clip)));

    gst_object_unref (child0);
    gst_object_unref (child1);
    gst_object_unref (effect);
  }

  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

#define _assert_duration_limit(clip, expect) \
  assert_equals_uint64 (ges_clip_get_duration_limit (GES_CLIP (clip)), expect)

GST_START_TEST (test_duration_limit)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESClip *clip;
  GESTrackElement *video_source, *audio_source;
  GESTrackElement *effect1, *effect2, *effect3;
  GESTrack *track1, *track2;
  gint limit_notify_count = 0;
  gint duration_notify_count = 0;

  ges_init ();

  timeline = ges_timeline_new ();
  track1 = GES_TRACK (ges_video_track_new ());
  track2 = GES_TRACK (ges_audio_track_new ());

  fail_unless (ges_timeline_add_track (timeline, track1));
  fail_unless (ges_timeline_add_track (timeline, track2));
  /* add track3 later */

  layer = ges_timeline_append_layer (timeline);

  clip = GES_CLIP (ges_test_clip_new ());
  g_signal_connect (clip, "notify::duration-limit", G_CALLBACK (_count_cb),
      &limit_notify_count);
  g_signal_connect (clip, "notify::duration", G_CALLBACK (_count_cb),
      &duration_notify_count);

  /* no limit to begin with */
  _assert_duration_limit (clip, GST_CLOCK_TIME_NONE);

  /* add effects */
  effect1 = GES_TRACK_ELEMENT (ges_effect_new ("textoverlay"));
  ges_track_element_set_has_internal_source (effect1, TRUE);

  effect2 = GES_TRACK_ELEMENT (ges_effect_new ("agingtv"));
  ges_track_element_set_has_internal_source (effect2, TRUE);

  effect3 = GES_TRACK_ELEMENT (ges_effect_new ("audioecho"));
  ges_track_element_set_has_internal_source (effect3, TRUE);

  _assert_add (clip, effect1);
  _assert_add (clip, effect2);
  _assert_add (clip, effect3);
  assert_num_children (clip, 3);
  _assert_duration_limit (clip, GST_CLOCK_TIME_NONE);
  assert_equals_int (limit_notify_count, 0);
  assert_equals_int (duration_notify_count, 0);

  /* no change in duration limit whilst children are not in any track */
  assert_set_max_duration (effect1, 20);
  _assert_duration_limit (clip, GST_CLOCK_TIME_NONE);
  assert_equals_int (limit_notify_count, 0);
  assert_equals_int (duration_notify_count, 0);

  assert_set_inpoint (effect1, 5);
  _assert_duration_limit (clip, GST_CLOCK_TIME_NONE);
  assert_equals_int (limit_notify_count, 0);
  assert_equals_int (duration_notify_count, 0);

  /* set a duration that will be above the duration-limit */
  assert_set_duration (clip, 20);
  assert_equals_int (duration_notify_count, 1);

  /* add to layer to create sources */
  fail_unless (ges_layer_add_clip (layer, clip));

  /* duration-limit changes once because of effect1 */
  _assert_duration_limit (clip, 15);
  assert_equals_int (limit_notify_count, 1);
  assert_equals_int (duration_notify_count, 2);
  /* duration has automatically been set to the duration-limit */
  CHECK_OBJECT_PROPS_MAX (clip, 0, 0, 15, GST_CLOCK_TIME_NONE);

  assert_num_children (clip, 5);
  assert_num_in_track (track1, 3);
  assert_num_in_track (track2, 2);

  video_source = ges_clip_find_track_element (clip, track1, GES_TYPE_SOURCE);
  fail_unless (video_source);
  gst_object_unref (video_source);

  audio_source = ges_clip_find_track_element (clip, track2, GES_TYPE_SOURCE);
  fail_unless (audio_source);
  gst_object_unref (audio_source);

  CHECK_OBJECT_PROPS_MAX (video_source, 0, 0, 15, GST_CLOCK_TIME_NONE);
  fail_unless (ges_track_element_get_track (video_source) == track1);
  fail_unless (ges_track_element_get_track_type (video_source) ==
      GES_TRACK_TYPE_VIDEO);
  CHECK_OBJECT_PROPS_MAX (audio_source, 0, 0, 15, GST_CLOCK_TIME_NONE);
  fail_unless (ges_track_element_get_track (audio_source) == track2);
  fail_unless (ges_track_element_get_track_type (audio_source) ==
      GES_TRACK_TYPE_AUDIO);
  CHECK_OBJECT_PROPS_MAX (effect1, 0, 5, 15, 20);
  fail_unless (ges_track_element_get_track (effect1) == track1);
  CHECK_OBJECT_PROPS_MAX (effect2, 0, 0, 15, GST_CLOCK_TIME_NONE);
  fail_unless (ges_track_element_get_track (effect2) == track1);
  CHECK_OBJECT_PROPS_MAX (effect3, 0, 0, 15, GST_CLOCK_TIME_NONE);
  fail_unless (ges_track_element_get_track (effect3) == track2);

  /* Make effect1 inactive, which will remove the duration-limit */
  fail_unless (ges_track_element_set_active (effect1, FALSE));
  _assert_duration_limit (clip, GST_CLOCK_TIME_NONE);
  assert_equals_int (limit_notify_count, 2);
  /* duration is unchanged */
  assert_equals_int (duration_notify_count, 2);
  CHECK_OBJECT_PROPS_MAX (clip, 0, 0, 15, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (video_source, 0, 0, 15, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (audio_source, 0, 0, 15, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect1, 0, 5, 15, 20);
  CHECK_OBJECT_PROPS_MAX (effect2, 0, 0, 15, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect3, 0, 0, 15, GST_CLOCK_TIME_NONE);

  /* changing the in-point does not change the duration limit whilst
   * there is no max-duration */
  assert_set_inpoint (clip, 10);
  _assert_duration_limit (clip, GST_CLOCK_TIME_NONE);
  assert_equals_int (limit_notify_count, 2);
  assert_equals_int (duration_notify_count, 2);
  CHECK_OBJECT_PROPS_MAX (clip, 0, 10, 15, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (video_source, 0, 10, 15, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (audio_source, 0, 10, 15, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect1, 0, 5, 15, 20);
  CHECK_OBJECT_PROPS_MAX (effect2, 0, 0, 15, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect3, 0, 0, 15, GST_CLOCK_TIME_NONE);

  assert_set_max_duration (video_source, 40);
  _assert_duration_limit (clip, 30);
  assert_equals_int (limit_notify_count, 3);
  assert_equals_int (duration_notify_count, 2);
  CHECK_OBJECT_PROPS_MAX (clip, 0, 10, 15, 40);
  CHECK_OBJECT_PROPS_MAX (video_source, 0, 10, 15, 40);
  CHECK_OBJECT_PROPS_MAX (audio_source, 0, 10, 15, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect1, 0, 5, 15, 20);
  CHECK_OBJECT_PROPS_MAX (effect2, 0, 0, 15, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect3, 0, 0, 15, GST_CLOCK_TIME_NONE);

  /* set in-point using child */
  assert_set_inpoint (audio_source, 15);
  _assert_duration_limit (clip, 25);
  assert_equals_int (limit_notify_count, 4);
  assert_equals_int (duration_notify_count, 2);
  CHECK_OBJECT_PROPS_MAX (clip, 0, 15, 15, 40);
  CHECK_OBJECT_PROPS_MAX (video_source, 0, 15, 15, 40);
  CHECK_OBJECT_PROPS_MAX (audio_source, 0, 15, 15, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect1, 0, 5, 15, 20);
  CHECK_OBJECT_PROPS_MAX (effect2, 0, 0, 15, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect3, 0, 0, 15, GST_CLOCK_TIME_NONE);

  /* set max-duration of core children */
  assert_set_max_duration (clip, 60);
  _assert_duration_limit (clip, 45);
  assert_equals_int (limit_notify_count, 5);
  assert_equals_int (duration_notify_count, 2);
  CHECK_OBJECT_PROPS_MAX (clip, 0, 15, 15, 60);
  CHECK_OBJECT_PROPS_MAX (video_source, 0, 15, 15, 60);
  CHECK_OBJECT_PROPS_MAX (audio_source, 0, 15, 15, 60);
  CHECK_OBJECT_PROPS_MAX (effect1, 0, 5, 15, 20);
  CHECK_OBJECT_PROPS_MAX (effect2, 0, 0, 15, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect3, 0, 0, 15, GST_CLOCK_TIME_NONE);

  /* can set duration up to the limit */
  assert_set_duration (clip, 45);
  _assert_duration_limit (clip, 45);
  assert_equals_int (limit_notify_count, 5);
  assert_equals_int (duration_notify_count, 3);
  CHECK_OBJECT_PROPS_MAX (clip, 0, 15, 45, 60);
  CHECK_OBJECT_PROPS_MAX (video_source, 0, 15, 45, 60);
  CHECK_OBJECT_PROPS_MAX (audio_source, 0, 15, 45, 60);
  /* effect1 has a duration that exceeds max-duration - in-point
   * ok since it is currently inactive */
  CHECK_OBJECT_PROPS_MAX (effect1, 0, 5, 45, 20);
  CHECK_OBJECT_PROPS_MAX (effect2, 0, 0, 45, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect3, 0, 0, 45, GST_CLOCK_TIME_NONE);

  /* limit the effects */
  assert_set_max_duration (effect2, 70);
  _assert_duration_limit (clip, 45);
  assert_equals_int (limit_notify_count, 5);
  assert_equals_int (duration_notify_count, 3);
  CHECK_OBJECT_PROPS_MAX (clip, 0, 15, 45, 60);
  CHECK_OBJECT_PROPS_MAX (video_source, 0, 15, 45, 60);
  CHECK_OBJECT_PROPS_MAX (audio_source, 0, 15, 45, 60);
  CHECK_OBJECT_PROPS_MAX (effect1, 0, 5, 45, 20);
  CHECK_OBJECT_PROPS_MAX (effect2, 0, 0, 45, 70);
  CHECK_OBJECT_PROPS_MAX (effect3, 0, 0, 45, GST_CLOCK_TIME_NONE);

  assert_set_inpoint (effect2, 40);
  _assert_duration_limit (clip, 30);
  assert_equals_int (limit_notify_count, 6);
  assert_equals_int (duration_notify_count, 4);
  CHECK_OBJECT_PROPS_MAX (clip, 0, 15, 30, 60);
  CHECK_OBJECT_PROPS_MAX (video_source, 0, 15, 30, 60);
  CHECK_OBJECT_PROPS_MAX (audio_source, 0, 15, 30, 60);
  CHECK_OBJECT_PROPS_MAX (effect1, 0, 5, 30, 20);
  CHECK_OBJECT_PROPS_MAX (effect2, 0, 40, 30, 70);
  CHECK_OBJECT_PROPS_MAX (effect3, 0, 0, 30, GST_CLOCK_TIME_NONE);

  /* no change */
  assert_set_max_duration (effect3, 35);
  _assert_duration_limit (clip, 30);
  assert_equals_int (limit_notify_count, 6);
  assert_equals_int (duration_notify_count, 4);
  CHECK_OBJECT_PROPS_MAX (clip, 0, 15, 30, 60);
  CHECK_OBJECT_PROPS_MAX (video_source, 0, 15, 30, 60);
  CHECK_OBJECT_PROPS_MAX (audio_source, 0, 15, 30, 60);
  CHECK_OBJECT_PROPS_MAX (effect1, 0, 5, 30, 20);
  CHECK_OBJECT_PROPS_MAX (effect2, 0, 40, 30, 70);
  CHECK_OBJECT_PROPS_MAX (effect3, 0, 0, 30, 35);

  /* make effect1 active again */
  fail_unless (ges_track_element_set_active (effect1, TRUE));
  _assert_duration_limit (clip, 15);
  assert_equals_int (limit_notify_count, 7);
  assert_equals_int (duration_notify_count, 5);
  CHECK_OBJECT_PROPS_MAX (clip, 0, 15, 15, 60);
  CHECK_OBJECT_PROPS_MAX (video_source, 0, 15, 15, 60);
  CHECK_OBJECT_PROPS_MAX (audio_source, 0, 15, 15, 60);
  CHECK_OBJECT_PROPS_MAX (effect1, 0, 5, 15, 20);
  CHECK_OBJECT_PROPS_MAX (effect2, 0, 40, 15, 70);
  CHECK_OBJECT_PROPS_MAX (effect3, 0, 0, 15, 35);

  /* removing effect2 from track does not change limit */
  fail_unless (ges_track_remove_element (track1, effect2));
  _assert_duration_limit (clip, 15);
  assert_equals_int (limit_notify_count, 7);
  assert_equals_int (duration_notify_count, 5);
  CHECK_OBJECT_PROPS_MAX (clip, 0, 15, 15, 60);
  CHECK_OBJECT_PROPS_MAX (video_source, 0, 15, 15, 60);
  CHECK_OBJECT_PROPS_MAX (audio_source, 0, 15, 15, 60);
  CHECK_OBJECT_PROPS_MAX (effect1, 0, 5, 15, 20);
  CHECK_OBJECT_PROPS_MAX (effect3, 0, 0, 15, 35);
  /* no track */
  CHECK_OBJECT_PROPS_MAX (effect2, 0, 40, 15, 70);

  /* removing effect1 does */
  fail_unless (ges_track_remove_element (track1, effect1));
  _assert_duration_limit (clip, 35);
  assert_equals_int (limit_notify_count, 8);
  assert_equals_int (duration_notify_count, 5);
  CHECK_OBJECT_PROPS_MAX (clip, 0, 15, 15, 60);
  CHECK_OBJECT_PROPS_MAX (video_source, 0, 15, 15, 60);
  CHECK_OBJECT_PROPS_MAX (audio_source, 0, 15, 15, 60);
  CHECK_OBJECT_PROPS_MAX (effect3, 0, 0, 15, 35);
  /* no track */
  CHECK_OBJECT_PROPS_MAX (effect1, 0, 5, 15, 20);
  CHECK_OBJECT_PROPS_MAX (effect2, 0, 40, 15, 70);

  /* add back */
  fail_unless (ges_track_add_element (track1, effect2));
  _assert_duration_limit (clip, 30);
  assert_equals_int (limit_notify_count, 9);
  assert_equals_int (duration_notify_count, 5);
  CHECK_OBJECT_PROPS_MAX (clip, 0, 15, 15, 60);
  CHECK_OBJECT_PROPS_MAX (audio_source, 0, 15, 15, 60);
  CHECK_OBJECT_PROPS_MAX (effect3, 0, 0, 15, 35);
  CHECK_OBJECT_PROPS_MAX (video_source, 0, 15, 15, 60);
  CHECK_OBJECT_PROPS_MAX (effect2, 0, 40, 15, 70);
  /* no track */
  CHECK_OBJECT_PROPS_MAX (effect1, 0, 5, 15, 20);

  assert_set_duration (clip, 20);
  _assert_duration_limit (clip, 30);
  assert_equals_int (limit_notify_count, 9);
  assert_equals_int (duration_notify_count, 6);
  CHECK_OBJECT_PROPS_MAX (clip, 0, 15, 20, 60);
  CHECK_OBJECT_PROPS_MAX (audio_source, 0, 15, 20, 60);
  CHECK_OBJECT_PROPS_MAX (effect3, 0, 0, 20, 35);
  CHECK_OBJECT_PROPS_MAX (video_source, 0, 15, 20, 60);
  CHECK_OBJECT_PROPS_MAX (effect2, 0, 40, 20, 70);
  /* no track */
  CHECK_OBJECT_PROPS_MAX (effect1, 0, 5, 20, 20);

  fail_unless (ges_track_add_element (track1, effect1));
  _assert_duration_limit (clip, 15);
  assert_equals_int (limit_notify_count, 10);
  assert_equals_int (duration_notify_count, 7);
  CHECK_OBJECT_PROPS_MAX (clip, 0, 15, 15, 60);
  CHECK_OBJECT_PROPS_MAX (audio_source, 0, 15, 15, 60);
  CHECK_OBJECT_PROPS_MAX (effect3, 0, 0, 15, 35);
  CHECK_OBJECT_PROPS_MAX (video_source, 0, 15, 15, 60);
  CHECK_OBJECT_PROPS_MAX (effect1, 0, 5, 15, 20);
  CHECK_OBJECT_PROPS_MAX (effect2, 0, 40, 15, 70);

  gst_object_ref (clip);
  fail_unless (ges_layer_remove_clip (layer, clip));

  assert_num_in_track (track1, 0);
  assert_num_in_track (track2, 0);
  assert_num_children (clip, 5);

  _assert_duration_limit (clip, GST_CLOCK_TIME_NONE);
  /* may have several changes in duration limit as the children are
   * emptied from their tracks */
  fail_unless (limit_notify_count > 10);
  assert_equals_int (duration_notify_count, 7);
  /* none in any track */
  CHECK_OBJECT_PROPS_MAX (clip, 0, 15, 15, 60);
  CHECK_OBJECT_PROPS_MAX (audio_source, 0, 15, 15, 60);
  CHECK_OBJECT_PROPS_MAX (effect3, 0, 0, 15, 35);
  CHECK_OBJECT_PROPS_MAX (video_source, 0, 15, 15, 60);
  CHECK_OBJECT_PROPS_MAX (effect1, 0, 5, 15, 20);
  CHECK_OBJECT_PROPS_MAX (effect2, 0, 40, 15, 70);

  gst_object_unref (timeline);
  gst_object_unref (clip);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_can_set_duration_limit)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESClip *clip;
  GESTrackElement *source0, *source1;
  GESTrackElement *effect0, *effect1, *effect2;
  GESTrack *track0, *track1;
  gint limit_notify_count = 0;
  GError *error = NULL;

  ges_init ();

  timeline = ges_timeline_new ();
  track0 = GES_TRACK (ges_video_track_new ());
  track1 = GES_TRACK (ges_audio_track_new ());

  fail_unless (ges_timeline_add_track (timeline, track0));
  fail_unless (ges_timeline_add_track (timeline, track1));
  /* add track3 later */

  layer = ges_timeline_append_layer (timeline);

  /* place a dummy clip at the start of the layer */
  clip = GES_CLIP (ges_test_clip_new ());
  assert_set_start (clip, 0);
  assert_set_duration (clip, 20);

  fail_unless (ges_layer_add_clip (layer, clip));

  /* the clip we will be editing overlaps the first clip at its start */
  clip = GES_CLIP (ges_test_clip_new ());

  g_signal_connect (clip, "notify::duration-limit", G_CALLBACK (_count_cb),
      &limit_notify_count);

  assert_set_start (clip, 10);
  assert_set_duration (clip, 20);

  fail_unless (ges_layer_add_clip (layer, clip));

  source0 =
      ges_clip_find_track_element (clip, track0, GES_TYPE_VIDEO_TEST_SOURCE);
  source1 =
      ges_clip_find_track_element (clip, track1, GES_TYPE_AUDIO_TEST_SOURCE);

  fail_unless (source0);
  fail_unless (source1);

  gst_object_unref (source0);
  gst_object_unref (source1);

  assert_equals_int (limit_notify_count, 0);
  _assert_duration_limit (clip, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 0, 20, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 0, 20, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 0, 20, GST_CLOCK_TIME_NONE);

  assert_set_inpoint (clip, 16);

  assert_equals_int (limit_notify_count, 0);
  _assert_duration_limit (clip, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 16, 20, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 16, 20, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 16, 20, GST_CLOCK_TIME_NONE);

  assert_set_max_duration (clip, 36);

  assert_equals_int (limit_notify_count, 1);
  _assert_duration_limit (clip, 20);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 16, 20, 36);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 16, 20, 36);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 16, 20, 36);

  /* add effects */
  effect0 = GES_TRACK_ELEMENT (ges_effect_new ("agingtv"));
  effect1 = GES_TRACK_ELEMENT (ges_effect_new ("vertigotv"));
  effect2 = GES_TRACK_ELEMENT (ges_effect_new ("alpha"));

  ges_track_element_set_has_internal_source (effect0, TRUE);
  fail_unless (ges_track_element_has_internal_source (effect1) == FALSE);
  ges_track_element_set_has_internal_source (effect2, TRUE);

  assert_set_max_duration (effect0, 10);
  /* already set the track */
  fail_unless (ges_track_add_element (track0, effect0));
  /* cannot add because it would cause the duration-limit to go to 10,
   * causing a full overlap with the clip at the beginning of the layer */

  gst_object_ref (effect0);
  fail_if (ges_container_add (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (effect0)));

  assert_equals_int (limit_notify_count, 1);
  _assert_duration_limit (clip, 20);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 16, 20, 36);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 16, 20, 36);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 16, 20, 36);

  /* removing from the track and adding will work, but track selection
   * will fail */
  fail_unless (ges_track_remove_element (track0, effect0));

  _assert_add (clip, effect0);
  fail_if (ges_track_element_get_track (effect0));
  gst_object_unref (effect0);

  assert_equals_int (limit_notify_count, 1);
  _assert_duration_limit (clip, 20);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 16, 20, 36);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 16, 20, 36);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 16, 20, 36);
  CHECK_OBJECT_PROPS_MAX (effect0, 10, 0, 20, 10);

  fail_if (ges_clip_add_child_to_track (clip, effect0, track0, &error));
  assert_GESError (error, GES_ERROR_INVALID_OVERLAP_IN_TRACK);

  /* set max-duration to 11 and we are fine to select a track */
  assert_set_max_duration (effect0, 11);
  assert_equals_int (limit_notify_count, 1);
  _assert_duration_limit (clip, 20);

  fail_unless (ges_clip_add_child_to_track (clip, effect0, track0,
          &error) == effect0);
  fail_if (error);

  assert_equals_int (limit_notify_count, 2);
  _assert_duration_limit (clip, 11);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 16, 11, 36);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 16, 11, 36);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 16, 11, 36);
  CHECK_OBJECT_PROPS_MAX (effect0, 10, 0, 11, 11);

  /* cannot set duration above the limit */
  assert_fail_set_duration (clip, 12);
  assert_fail_set_duration (source0, 12);
  assert_fail_set_duration (effect0, 12);

  /* allow the max_duration to increase again */
  assert_set_max_duration (effect0, 25);

  assert_equals_int (limit_notify_count, 3);
  _assert_duration_limit (clip, 20);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 16, 11, 36);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 16, 11, 36);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 16, 11, 36);
  CHECK_OBJECT_PROPS_MAX (effect0, 10, 0, 11, 25);

  assert_set_duration (clip, 20);

  assert_equals_int (limit_notify_count, 3);
  _assert_duration_limit (clip, 20);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 16, 20, 36);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 16, 20, 36);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 16, 20, 36);
  CHECK_OBJECT_PROPS_MAX (effect0, 10, 0, 20, 25);

  /* add another effect */
  _assert_add (clip, effect1);
  fail_unless (ges_track_element_get_track (effect1) == track0);

  assert_equals_int (limit_notify_count, 3);
  _assert_duration_limit (clip, 20);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 16, 20, 36);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 16, 20, 36);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 16, 20, 36);
  CHECK_OBJECT_PROPS_MAX (effect0, 10, 0, 20, 25);
  CHECK_OBJECT_PROPS_MAX (effect1, 10, 0, 20, GST_CLOCK_TIME_NONE);

  /* make source0 inactive and reduce its max-duration
   * note that this causes effect0 and effect1 to also become in-active */
  _assert_set_active (source0, FALSE);
  _assert_active (source0, FALSE);
  _assert_active (effect0, FALSE);
  _assert_active (effect1, FALSE);

  assert_equals_int (limit_notify_count, 3);
  _assert_duration_limit (clip, 20);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 16, 20, 36);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 16, 20, 36);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 16, 20, 36);
  CHECK_OBJECT_PROPS_MAX (effect0, 10, 0, 20, 25);
  CHECK_OBJECT_PROPS_MAX (effect1, 10, 0, 20, GST_CLOCK_TIME_NONE);

  /* can set a low max duration whilst the source is inactive */
  assert_set_max_duration (source0, 26);

  assert_equals_int (limit_notify_count, 3);
  _assert_duration_limit (clip, 20);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 16, 20, 26);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 16, 20, 26);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 16, 20, 36);
  CHECK_OBJECT_PROPS_MAX (effect0, 10, 0, 20, 25);
  CHECK_OBJECT_PROPS_MAX (effect1, 10, 0, 20, GST_CLOCK_TIME_NONE);

  /* add the last effect */
  assert_set_inpoint (effect2, 7);
  assert_set_max_duration (effect2, 17);
  _assert_active (effect2, TRUE);

  /* safe to add because the source is inactive */
  assert_equals_int (limit_notify_count, 3);
  _assert_add (clip, effect2);
  assert_equals_int (limit_notify_count, 3);
  _assert_active (source0, FALSE);
  _assert_active (effect0, FALSE);
  _assert_active (effect1, FALSE);
  _assert_active (effect2, FALSE);

  fail_unless (ges_track_element_get_track (effect2) == track0);

  assert_equals_int (limit_notify_count, 3);
  _assert_duration_limit (clip, 20);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 16, 20, 26);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 16, 20, 26);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 16, 20, 36);
  CHECK_OBJECT_PROPS_MAX (effect0, 10, 0, 20, 25);
  CHECK_OBJECT_PROPS_MAX (effect1, 10, 0, 20, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect2, 10, 7, 20, 17);

  /* want to make the source and its effects active again */
  assert_set_inpoint (source0, 6);
  assert_set_max_duration (effect2, 33);

  assert_equals_int (limit_notify_count, 4);
  _assert_duration_limit (clip, 30);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 6, 20, 36);
  CHECK_OBJECT_PROPS_MAX (effect0, 10, 0, 20, 25);
  CHECK_OBJECT_PROPS_MAX (effect1, 10, 0, 20, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect2, 10, 7, 20, 33);

  _assert_set_active (source0, TRUE);
  _assert_set_active (effect0, TRUE);
  _assert_set_active (effect1, TRUE);
  _assert_set_active (effect2, TRUE);

  assert_equals_int (limit_notify_count, 5);
  _assert_duration_limit (clip, 20);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 6, 20, 36);
  CHECK_OBJECT_PROPS_MAX (effect0, 10, 0, 20, 25);
  CHECK_OBJECT_PROPS_MAX (effect1, 10, 0, 20, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect2, 10, 7, 20, 33);

  /* cannot set in-point of clip to 16, nor of either source */
  assert_fail_set_inpoint (clip, 16);
  assert_fail_set_inpoint (source0, 16);
  assert_fail_set_inpoint (source1, 16);

  assert_equals_int (limit_notify_count, 5);
  _assert_duration_limit (clip, 20);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 6, 20, 36);
  CHECK_OBJECT_PROPS_MAX (effect0, 10, 0, 20, 25);
  CHECK_OBJECT_PROPS_MAX (effect1, 10, 0, 20, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect2, 10, 7, 20, 33);

  /* can set just below */
  assert_set_inpoint (source1, 15);

  assert_equals_int (limit_notify_count, 6);
  _assert_duration_limit (clip, 11);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 15, 11, 26);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 15, 11, 26);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 15, 11, 36);
  CHECK_OBJECT_PROPS_MAX (effect0, 10, 0, 11, 25);
  CHECK_OBJECT_PROPS_MAX (effect1, 10, 0, 11, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect2, 10, 7, 11, 33);

  assert_set_inpoint (clip, 6);
  assert_set_duration (clip, 20);

  assert_equals_int (limit_notify_count, 7);
  _assert_duration_limit (clip, 20);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 6, 20, 36);
  CHECK_OBJECT_PROPS_MAX (effect0, 10, 0, 20, 25);
  CHECK_OBJECT_PROPS_MAX (effect1, 10, 0, 20, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect2, 10, 7, 20, 33);

  /* cannot set in-point of non-core in a way that would cause limit to
   * drop */
  assert_fail_set_inpoint (effect2, 23);
  assert_fail_set_inpoint (effect0, 15);

  assert_equals_int (limit_notify_count, 7);
  _assert_duration_limit (clip, 20);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 6, 20, 36);
  CHECK_OBJECT_PROPS_MAX (effect0, 10, 0, 20, 25);
  CHECK_OBJECT_PROPS_MAX (effect1, 10, 0, 20, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect2, 10, 7, 20, 33);

  /* can set just below */
  assert_set_inpoint (effect2, 22);

  assert_equals_int (limit_notify_count, 8);
  _assert_duration_limit (clip, 11);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 6, 11, 26);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 6, 11, 26);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 6, 11, 36);
  CHECK_OBJECT_PROPS_MAX (effect0, 10, 0, 11, 25);
  CHECK_OBJECT_PROPS_MAX (effect1, 10, 0, 11, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect2, 10, 22, 11, 33);

  assert_set_inpoint (effect2, 7);
  assert_set_duration (clip, 20);

  assert_equals_int (limit_notify_count, 9);
  _assert_duration_limit (clip, 20);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 6, 20, 36);
  CHECK_OBJECT_PROPS_MAX (effect0, 10, 0, 20, 25);
  CHECK_OBJECT_PROPS_MAX (effect1, 10, 0, 20, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect2, 10, 7, 20, 33);

  /* same but with max-duration */

  /* core */
  assert_fail_set_max_duration (clip, 16);
  assert_fail_set_max_duration (source0, 16);
  assert_fail_set_max_duration (source1, 16);

  assert_equals_int (limit_notify_count, 9);
  _assert_duration_limit (clip, 20);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 6, 20, 36);
  CHECK_OBJECT_PROPS_MAX (effect0, 10, 0, 20, 25);
  CHECK_OBJECT_PROPS_MAX (effect1, 10, 0, 20, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect2, 10, 7, 20, 33);

  assert_set_max_duration (source1, 17);

  assert_equals_int (limit_notify_count, 10);
  _assert_duration_limit (clip, 11);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 6, 11, 17);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 6, 11, 26);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 6, 11, 17);
  CHECK_OBJECT_PROPS_MAX (effect0, 10, 0, 11, 25);
  CHECK_OBJECT_PROPS_MAX (effect1, 10, 0, 11, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect2, 10, 7, 11, 33);

  assert_set_max_duration (source1, 30);
  assert_set_duration (clip, 20);

  assert_equals_int (limit_notify_count, 11);
  _assert_duration_limit (clip, 20);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 6, 20, 30);
  CHECK_OBJECT_PROPS_MAX (effect0, 10, 0, 20, 25);
  CHECK_OBJECT_PROPS_MAX (effect1, 10, 0, 20, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect2, 10, 7, 20, 33);

  assert_set_max_duration (clip, 17);

  assert_equals_int (limit_notify_count, 12);
  _assert_duration_limit (clip, 11);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 6, 11, 17);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 6, 11, 17);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 6, 11, 17);
  CHECK_OBJECT_PROPS_MAX (effect0, 10, 0, 11, 25);
  CHECK_OBJECT_PROPS_MAX (effect1, 10, 0, 11, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect2, 10, 7, 11, 33);

  assert_set_max_duration (clip, 26);
  assert_set_duration (clip, 20);

  assert_equals_int (limit_notify_count, 13);
  _assert_duration_limit (clip, 20);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (effect0, 10, 0, 20, 25);
  CHECK_OBJECT_PROPS_MAX (effect1, 10, 0, 20, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect2, 10, 7, 20, 33);

  /* non-core */
  assert_fail_set_max_duration (effect0, 10);
  assert_fail_set_max_duration (effect2, 17);

  assert_equals_int (limit_notify_count, 13);
  _assert_duration_limit (clip, 20);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (effect0, 10, 0, 20, 25);
  CHECK_OBJECT_PROPS_MAX (effect1, 10, 0, 20, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect2, 10, 7, 20, 33);

  assert_set_max_duration (effect2, 18);

  assert_equals_int (limit_notify_count, 14);
  _assert_duration_limit (clip, 11);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 6, 11, 26);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 6, 11, 26);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 6, 11, 26);
  CHECK_OBJECT_PROPS_MAX (effect0, 10, 0, 11, 25);
  CHECK_OBJECT_PROPS_MAX (effect1, 10, 0, 11, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect2, 10, 7, 11, 18);

  assert_set_max_duration (effect2, 33);
  assert_set_duration (clip, 20);

  assert_equals_int (limit_notify_count, 15);
  _assert_duration_limit (clip, 20);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (effect0, 10, 0, 20, 25);
  CHECK_OBJECT_PROPS_MAX (effect1, 10, 0, 20, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect2, 10, 7, 20, 33);

  /* test setting active */
  _assert_active (effect2, TRUE);
  _assert_set_active (effect2, FALSE);
  assert_set_max_duration (effect2, 17);

  assert_equals_int (limit_notify_count, 15);
  _assert_duration_limit (clip, 20);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (effect0, 10, 0, 20, 25);
  CHECK_OBJECT_PROPS_MAX (effect1, 10, 0, 20, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect2, 10, 7, 20, 17);

  fail_if (ges_track_element_set_active (effect2, TRUE));

  assert_equals_int (limit_notify_count, 15);
  _assert_duration_limit (clip, 20);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (effect0, 10, 0, 20, 25);
  CHECK_OBJECT_PROPS_MAX (effect1, 10, 0, 20, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect2, 10, 7, 20, 17);

  assert_set_inpoint (effect2, 6);
  _assert_set_active (effect2, TRUE);

  assert_equals_int (limit_notify_count, 16);
  _assert_duration_limit (clip, 11);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 6, 11, 26);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 6, 11, 26);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 6, 11, 26);
  CHECK_OBJECT_PROPS_MAX (effect0, 10, 0, 11, 25);
  CHECK_OBJECT_PROPS_MAX (effect1, 10, 0, 11, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect2, 10, 6, 11, 17);

  /* make source0 in-active */
  _assert_active (source0, TRUE);
  _assert_set_active (source0, FALSE);
  _assert_active (source0, FALSE);
  _assert_active (effect0, FALSE);
  _assert_active (effect1, FALSE);
  _assert_active (effect2, FALSE);

  assert_set_duration (source0, 20);

  assert_equals_int (limit_notify_count, 17);
  _assert_duration_limit (clip, 20);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (effect0, 10, 0, 20, 25);
  CHECK_OBJECT_PROPS_MAX (effect1, 10, 0, 20, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect2, 10, 6, 20, 17);

  /* lower duration-limit for source for when it becomes active */
  assert_set_max_duration (source0, 16);

  assert_equals_int (limit_notify_count, 17);
  _assert_duration_limit (clip, 20);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 6, 20, 16);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 6, 20, 16);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (effect0, 10, 0, 20, 25);
  CHECK_OBJECT_PROPS_MAX (effect1, 10, 0, 20, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect2, 10, 6, 20, 17);

  /* cannot make the source active */
  fail_if (ges_track_element_set_active (source0, TRUE));

  assert_equals_int (limit_notify_count, 17);
  _assert_duration_limit (clip, 20);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 6, 20, 16);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 6, 20, 16);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (effect0, 10, 0, 20, 25);
  CHECK_OBJECT_PROPS_MAX (effect1, 10, 0, 20, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect2, 10, 6, 20, 17);

  /* nor can we make the effects active because this would activate the
   * source */
  fail_if (ges_track_element_set_active (effect0, TRUE));
  fail_if (ges_track_element_set_active (effect1, TRUE));
  fail_if (ges_track_element_set_active (effect2, TRUE));

  assert_equals_int (limit_notify_count, 17);
  _assert_duration_limit (clip, 20);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 6, 20, 16);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 6, 20, 16);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 6, 20, 26);
  CHECK_OBJECT_PROPS_MAX (effect0, 10, 0, 20, 25);
  CHECK_OBJECT_PROPS_MAX (effect1, 10, 0, 20, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect2, 10, 6, 20, 17);

  /* allow it to just succeed */
  assert_set_inpoint (source0, 5);

  assert_equals_int (limit_notify_count, 18);
  _assert_duration_limit (clip, 21);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 5, 20, 16);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 5, 20, 16);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 5, 20, 26);
  CHECK_OBJECT_PROPS_MAX (effect0, 10, 0, 20, 25);
  CHECK_OBJECT_PROPS_MAX (effect1, 10, 0, 20, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect2, 10, 6, 20, 17);

  _assert_set_active (effect1, TRUE);

  assert_equals_int (limit_notify_count, 19);
  _assert_duration_limit (clip, 11);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 5, 11, 16);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 5, 11, 16);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 5, 11, 26);
  CHECK_OBJECT_PROPS_MAX (effect0, 10, 0, 11, 25);
  CHECK_OBJECT_PROPS_MAX (effect1, 10, 0, 11, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (effect2, 10, 6, 11, 17);

  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

#define _assert_set_rate(element, prop_name, rate, val) \
{ \
  GError *error = NULL; \
  if (G_VALUE_TYPE (&val) == G_TYPE_DOUBLE) \
    g_value_set_double (&val, rate); \
  else if (G_VALUE_TYPE (&val) == G_TYPE_FLOAT) \
    g_value_set_float (&val, rate); \
  \
  fail_unless (ges_timeline_element_set_child_property_full ( \
        GES_TIMELINE_ELEMENT (element), prop_name, &val, &error)); \
  fail_if (error); \
  g_value_reset (&val); \
}

#define _assert_fail_set_rate(element, prop_name, rate, val, code) \
{ \
  GError * error = NULL; \
  if (G_VALUE_TYPE (&val) == G_TYPE_DOUBLE) \
    g_value_set_double (&val, rate); \
  else if (G_VALUE_TYPE (&val) == G_TYPE_FLOAT) \
    g_value_set_float (&val, rate); \
  \
  fail_if (ges_timeline_element_set_child_property_full ( \
        GES_TIMELINE_ELEMENT (element), prop_name, &val, &error)); \
  assert_GESError (error, code); \
  g_value_reset (&val); \
}

#define _assert_rate_equal(element, prop_name, rate, val) \
{ \
  gdouble found = -1.0; \
  fail_unless (ges_timeline_element_get_child_property ( \
        GES_TIMELINE_ELEMENT (element), prop_name, &val)); \
  \
  if (G_VALUE_TYPE (&val) == G_TYPE_DOUBLE) \
    found = g_value_get_double (&val); \
  else if (G_VALUE_TYPE (&val) == G_TYPE_FLOAT) \
    found = g_value_get_float (&val); \
  \
  fail_unless (found == rate, "found %s: %g != expected: %g", found, \
      prop_name, rate); \
  g_value_reset (&val); \
}

GST_START_TEST (test_rate_effects_duration_limit)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESClip *clip;
  GESTrackElement *source0, *source1;
  GESTrackElement *overlay0, *overlay1, *videorate, *pitch;
  GESTrack *track0, *track1;
  gint limit_notify_count = 0;
  GValue fval = G_VALUE_INIT;
  GValue dval = G_VALUE_INIT;

  ges_init ();

  g_value_init (&fval, G_TYPE_FLOAT);
  g_value_init (&dval, G_TYPE_DOUBLE);

  timeline = ges_timeline_new ();
  track0 = GES_TRACK (ges_video_track_new ());
  track1 = GES_TRACK (ges_audio_track_new ());

  fail_unless (ges_timeline_add_track (timeline, track0));
  fail_unless (ges_timeline_add_track (timeline, track1));

  layer = ges_timeline_append_layer (timeline);

  /* place a dummy clip at the start of the layer */
  clip = GES_CLIP (ges_test_clip_new ());
  assert_set_start (clip, 0);
  assert_set_duration (clip, 26);

  fail_unless (ges_layer_add_clip (layer, clip));

  /* the clip we will be editing overlaps first clip by 16 at its start */
  clip = GES_CLIP (ges_test_clip_new ());

  g_signal_connect (clip, "notify::duration-limit", G_CALLBACK (_count_cb),
      &limit_notify_count);

  assert_set_start (clip, 10);
  assert_set_duration (clip, 64);

  fail_unless (ges_layer_add_clip (layer, clip));

  source0 =
      ges_clip_find_track_element (clip, track0, GES_TYPE_VIDEO_TEST_SOURCE);
  source1 =
      ges_clip_find_track_element (clip, track1, GES_TYPE_AUDIO_TEST_SOURCE);

  fail_unless (source0);
  fail_unless (source1);

  gst_object_unref (source0);
  gst_object_unref (source1);

  assert_equals_int (limit_notify_count, 0);
  _assert_duration_limit (clip, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 0, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 0, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 0, 64, GST_CLOCK_TIME_NONE);

  assert_set_inpoint (clip, 13);

  assert_equals_int (limit_notify_count, 0);
  _assert_duration_limit (clip, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 13, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 13, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 13, 64, GST_CLOCK_TIME_NONE);

  assert_set_max_duration (clip, 77);

  assert_equals_int (limit_notify_count, 1);
  _assert_duration_limit (clip, 64);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 13, 64, 77);

  /* add effects */
  overlay0 = GES_TRACK_ELEMENT (ges_effect_new ("textoverlay"));
  ges_track_element_set_has_internal_source (overlay0, TRUE);

  videorate = GES_TRACK_ELEMENT (ges_effect_new ("videorate"));
  fail_unless (ges_base_effect_is_time_effect (GES_BASE_EFFECT (videorate)));

  overlay1 = GES_TRACK_ELEMENT (ges_effect_new ("textoverlay"));
  ges_track_element_set_has_internal_source (overlay1, TRUE);

  pitch = GES_TRACK_ELEMENT (ges_effect_new ("pitch"));
  fail_unless (ges_base_effect_is_time_effect (GES_BASE_EFFECT (pitch)));

  /* add overlay1 at highest priority */
  _assert_add (clip, overlay1);

  assert_equals_int (limit_notify_count, 1);
  _assert_duration_limit (clip, 64);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (overlay1, 10, 0, 64, GST_CLOCK_TIME_NONE);

  _assert_set_rate (videorate, "rate", 4.0, dval);
  _assert_rate_equal (videorate, "rate", 4.0, dval);
  fail_unless (ges_track_add_element (track0, videorate));

  /* cannot add videorate as it would cause the duration-limit to drop
   * to 16, causing a full overlap */
  /* track keeps alive */
  fail_if (ges_container_add (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (videorate)));

  /* setting to 1.0 makes it work again */
  _assert_set_rate (videorate, "rate", 1.0, dval);
  _assert_add (clip, videorate);

  assert_equals_int (limit_notify_count, 1);
  _assert_duration_limit (clip, 64);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (videorate, 10, 0, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (overlay1, 10, 0, 64, GST_CLOCK_TIME_NONE);
  _assert_rate_equal (videorate, "rate", 1.0, dval);

  /* add second overlay at lower priority */
  _assert_add (clip, overlay0);

  assert_equals_int (limit_notify_count, 1);
  _assert_duration_limit (clip, 64);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (overlay0, 10, 0, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (videorate, 10, 0, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (overlay1, 10, 0, 64, GST_CLOCK_TIME_NONE);
  _assert_rate_equal (videorate, "rate", 1.0, dval);

  /* also add a pitch element in another track */
  _assert_add (clip, pitch);
  _assert_set_rate (pitch, "rate", 1.0, fval);
  _assert_set_rate (pitch, "tempo", 1.0, fval);

  assert_equals_int (limit_notify_count, 1);
  _assert_duration_limit (clip, 64);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (overlay0, 10, 0, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (videorate, 10, 0, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (overlay1, 10, 0, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (pitch, 10, 0, 64, GST_CLOCK_TIME_NONE);
  _assert_rate_equal (videorate, "rate", 1.0, dval);
  _assert_rate_equal (pitch, "rate", 1.0, fval);
  _assert_rate_equal (pitch, "tempo", 1.0, fval);

  fail_unless (ges_track_element_get_track (overlay0) == track0);
  fail_unless (ges_track_element_get_track (videorate) == track0);
  fail_unless (ges_track_element_get_track (overlay1) == track0);
  fail_unless (ges_track_element_get_track (pitch) == track1);

  /* flow in track0:
   * source0 -> overlay0 -> videorate -> overlay1 -> timeline output
   *
   * flow in track1:
   * source1 -> pitch -> timeline output
   */

  /* cannot set the rates to 4.0 since this would cause a full overlap */
  _assert_fail_set_rate (videorate, "rate", 4.0, dval,
      GES_ERROR_INVALID_OVERLAP_IN_TRACK);
  _assert_fail_set_rate (pitch, "rate", 4.0, fval,
      GES_ERROR_INVALID_OVERLAP_IN_TRACK);
  _assert_fail_set_rate (pitch, "tempo", 4.0, fval,
      GES_ERROR_INVALID_OVERLAP_IN_TRACK);

  assert_equals_int (limit_notify_count, 1);
  _assert_duration_limit (clip, 64);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (overlay0, 10, 0, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (videorate, 10, 0, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (overlay1, 10, 0, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (pitch, 10, 0, 64, GST_CLOCK_TIME_NONE);
  _assert_rate_equal (videorate, "rate", 1.0, dval);
  _assert_rate_equal (pitch, "rate", 1.0, fval);
  _assert_rate_equal (pitch, "tempo", 1.0, fval);

  /* limit overlay0 */
  assert_set_max_duration (overlay0, 91);

  assert_equals_int (limit_notify_count, 1);
  _assert_duration_limit (clip, 64);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (overlay0, 10, 0, 64, 91);
  CHECK_OBJECT_PROPS_MAX (videorate, 10, 0, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (overlay1, 10, 0, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (pitch, 10, 0, 64, GST_CLOCK_TIME_NONE);
  _assert_rate_equal (videorate, "rate", 1.0, dval);
  _assert_rate_equal (pitch, "rate", 1.0, fval);
  _assert_rate_equal (pitch, "tempo", 1.0, fval);

  assert_set_inpoint (overlay0, 59);

  assert_equals_int (limit_notify_count, 2);
  _assert_duration_limit (clip, 32);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 13, 32, 77);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 13, 32, 77);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 13, 32, 77);
  CHECK_OBJECT_PROPS_MAX (overlay0, 10, 59, 32, 91);
  CHECK_OBJECT_PROPS_MAX (videorate, 10, 0, 32, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (overlay1, 10, 0, 32, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (pitch, 10, 0, 32, GST_CLOCK_TIME_NONE);
  _assert_rate_equal (videorate, "rate", 1.0, dval);
  _assert_rate_equal (pitch, "rate", 1.0, fval);
  _assert_rate_equal (pitch, "tempo", 1.0, fval);

  /* can set pitch rate to 2.0, but not videorate rate because videorate
   * shares a track with overlay0 */

  _assert_set_rate (pitch, "rate", 2.0, fval);
  assert_equals_int (limit_notify_count, 2);
  _assert_fail_set_rate (videorate, "rate", 2.0, dval,
      GES_ERROR_INVALID_OVERLAP_IN_TRACK);
  assert_equals_int (limit_notify_count, 2);
  /* can't set tempo to 2.0 since overall effect would bring duration
   * limit too low */
  _assert_fail_set_rate (pitch, "tempo", 2.0, fval,
      GES_ERROR_INVALID_OVERLAP_IN_TRACK);

  assert_equals_int (limit_notify_count, 2);
  _assert_duration_limit (clip, 32);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 13, 32, 77);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 13, 32, 77);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 13, 32, 77);
  CHECK_OBJECT_PROPS_MAX (overlay0, 10, 59, 32, 91);
  CHECK_OBJECT_PROPS_MAX (videorate, 10, 0, 32, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (overlay1, 10, 0, 32, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (pitch, 10, 0, 32, GST_CLOCK_TIME_NONE);
  _assert_rate_equal (videorate, "rate", 1.0, dval);
  _assert_rate_equal (pitch, "rate", 2.0, fval);
  _assert_rate_equal (pitch, "tempo", 1.0, fval);

  /* cannot set in-point of clip because pitch would cause limit to go
   * to 16 */
  assert_fail_set_inpoint (clip, 45);
  /* same for max-duration of source1 */
  assert_fail_set_max_duration (source1, 45);

  assert_equals_int (limit_notify_count, 2);
  _assert_duration_limit (clip, 32);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 13, 32, 77);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 13, 32, 77);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 13, 32, 77);
  CHECK_OBJECT_PROPS_MAX (overlay0, 10, 59, 32, 91);
  CHECK_OBJECT_PROPS_MAX (videorate, 10, 0, 32, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (overlay1, 10, 0, 32, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (pitch, 10, 0, 32, GST_CLOCK_TIME_NONE);
  _assert_rate_equal (videorate, "rate", 1.0, dval);
  _assert_rate_equal (pitch, "rate", 2.0, fval);
  _assert_rate_equal (pitch, "tempo", 1.0, fval);

  /* can set rate to 0.5 */
  _assert_set_rate (videorate, "rate", 0.5, dval);

  /* no change yet, since pitch rate is still 2.0 */
  assert_equals_int (limit_notify_count, 2);
  _assert_duration_limit (clip, 32);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 13, 32, 77);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 13, 32, 77);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 13, 32, 77);
  CHECK_OBJECT_PROPS_MAX (overlay0, 10, 59, 32, 91);
  CHECK_OBJECT_PROPS_MAX (videorate, 10, 0, 32, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (overlay1, 10, 0, 32, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (pitch, 10, 0, 32, GST_CLOCK_TIME_NONE);
  _assert_rate_equal (videorate, "rate", 0.5, dval);
  _assert_rate_equal (pitch, "rate", 2.0, fval);
  _assert_rate_equal (pitch, "tempo", 1.0, fval);

  _assert_set_rate (pitch, "rate", 0.5, fval);

  assert_equals_int (limit_notify_count, 3);
  /* duration-limit is 64 because overlay0 only has 32 nanoseconds of
   * content, stretched to 64 by videorate */
  _assert_duration_limit (clip, 64);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 13, 32, 77);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 13, 32, 77);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 13, 32, 77);
  CHECK_OBJECT_PROPS_MAX (overlay0, 10, 59, 32, 91);
  CHECK_OBJECT_PROPS_MAX (videorate, 10, 0, 32, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (overlay1, 10, 0, 32, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (pitch, 10, 0, 32, GST_CLOCK_TIME_NONE);
  _assert_rate_equal (videorate, "rate", 0.5, dval);
  _assert_rate_equal (pitch, "rate", 0.5, fval);
  _assert_rate_equal (pitch, "tempo", 1.0, fval);

  /* setting the max-duration of the sources does not change the limit
   * since the limit on overlay0 is fine.
   * Note that pitch handles the unlimited duration (GST_CLOCK_TIME_NONE)
   * without any problems */
  assert_set_max_duration (clip, GST_CLOCK_TIME_NONE);

  assert_equals_int (limit_notify_count, 3);
  _assert_duration_limit (clip, 64);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 13, 32, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 13, 32, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 13, 32, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (overlay0, 10, 59, 32, 91);
  CHECK_OBJECT_PROPS_MAX (videorate, 10, 0, 32, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (overlay1, 10, 0, 32, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (pitch, 10, 0, 32, GST_CLOCK_TIME_NONE);
  _assert_rate_equal (videorate, "rate", 0.5, dval);
  _assert_rate_equal (pitch, "rate", 0.5, fval);
  _assert_rate_equal (pitch, "tempo", 1.0, fval);

  assert_set_max_duration (clip, 77);

  assert_equals_int (limit_notify_count, 3);
  _assert_duration_limit (clip, 64);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 13, 32, 77);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 13, 32, 77);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 13, 32, 77);
  CHECK_OBJECT_PROPS_MAX (overlay0, 10, 59, 32, 91);
  CHECK_OBJECT_PROPS_MAX (videorate, 10, 0, 32, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (overlay1, 10, 0, 32, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (pitch, 10, 0, 32, GST_CLOCK_TIME_NONE);
  _assert_rate_equal (videorate, "rate", 0.5, dval);
  _assert_rate_equal (pitch, "rate", 0.5, fval);
  _assert_rate_equal (pitch, "tempo", 1.0, fval);

  /* limit overlay1. It should not be changes by the videorate element
   * since it acts at a lower priority
   * first make it last longer, so no change in duration-limit */

  assert_set_max_duration (overlay1, 81);

  assert_equals_int (limit_notify_count, 3);
  _assert_duration_limit (clip, 64);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 13, 32, 77);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 13, 32, 77);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 13, 32, 77);
  CHECK_OBJECT_PROPS_MAX (overlay0, 10, 59, 32, 91);
  CHECK_OBJECT_PROPS_MAX (videorate, 10, 0, 32, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (overlay1, 10, 0, 32, 81);
  CHECK_OBJECT_PROPS_MAX (pitch, 10, 0, 32, GST_CLOCK_TIME_NONE);
  _assert_rate_equal (videorate, "rate", 0.5, dval);
  _assert_rate_equal (pitch, "rate", 0.5, fval);
  _assert_rate_equal (pitch, "tempo", 1.0, fval);

  /* now make it shorter */
  assert_set_inpoint (overlay1, 51);

  assert_equals_int (limit_notify_count, 4);
  _assert_duration_limit (clip, 30);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 13, 30, 77);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 13, 30, 77);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 13, 30, 77);
  CHECK_OBJECT_PROPS_MAX (overlay0, 10, 59, 30, 91);
  CHECK_OBJECT_PROPS_MAX (videorate, 10, 0, 30, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (overlay1, 10, 51, 30, 81);
  CHECK_OBJECT_PROPS_MAX (pitch, 10, 0, 30, GST_CLOCK_TIME_NONE);
  _assert_rate_equal (videorate, "rate", 0.5, dval);
  _assert_rate_equal (pitch, "rate", 0.5, fval);
  _assert_rate_equal (pitch, "tempo", 1.0, fval);

  /* remove the overlay0 limit */
  assert_set_max_duration (overlay0, GST_CLOCK_TIME_NONE);

  /* no change because of overlay1 */
  assert_equals_int (limit_notify_count, 4);
  _assert_duration_limit (clip, 30);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 13, 30, 77);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 13, 30, 77);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 13, 30, 77);
  CHECK_OBJECT_PROPS_MAX (overlay0, 10, 59, 30, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (videorate, 10, 0, 30, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (overlay1, 10, 51, 30, 81);
  CHECK_OBJECT_PROPS_MAX (pitch, 10, 0, 30, GST_CLOCK_TIME_NONE);
  _assert_rate_equal (videorate, "rate", 0.5, dval);
  _assert_rate_equal (pitch, "rate", 0.5, fval);
  _assert_rate_equal (pitch, "tempo", 1.0, fval);

  assert_set_max_duration (overlay1, GST_CLOCK_TIME_NONE);

  assert_equals_int (limit_notify_count, 5);
  _assert_duration_limit (clip, 128);
  /* can set up to the limit */
  assert_set_duration (clip, 128);

  CHECK_OBJECT_PROPS_MAX (clip, 10, 13, 128, 77);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 13, 128, 77);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 13, 128, 77);
  CHECK_OBJECT_PROPS_MAX (overlay0, 10, 59, 128, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (videorate, 10, 0, 128, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (overlay1, 10, 51, 128, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (pitch, 10, 0, 128, GST_CLOCK_TIME_NONE);
  _assert_rate_equal (videorate, "rate", 0.5, dval);
  _assert_rate_equal (pitch, "rate", 0.5, fval);
  _assert_rate_equal (pitch, "tempo", 1.0, fval);

  /* tempo contributes the same factor as rate */

  _assert_set_rate (pitch, "tempo", 2.0, fval);

  assert_equals_int (limit_notify_count, 6);
  _assert_duration_limit (clip, 64);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (overlay0, 10, 59, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (videorate, 10, 0, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (overlay1, 10, 51, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (pitch, 10, 0, 64, GST_CLOCK_TIME_NONE);
  _assert_rate_equal (videorate, "rate", 0.5, dval);
  _assert_rate_equal (pitch, "rate", 0.5, fval);
  _assert_rate_equal (pitch, "tempo", 2.0, fval);

  _assert_set_rate (videorate, "rate", 0.1, dval);
  assert_equals_int (limit_notify_count, 6);
  _assert_set_rate (pitch, "tempo", 0.5, fval);

  assert_equals_int (limit_notify_count, 7);
  _assert_duration_limit (clip, 256);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (overlay0, 10, 59, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (videorate, 10, 0, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (overlay1, 10, 51, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (pitch, 10, 0, 64, GST_CLOCK_TIME_NONE);
  _assert_rate_equal (videorate, "rate", 0.1, dval);
  _assert_rate_equal (pitch, "rate", 0.5, fval);
  _assert_rate_equal (pitch, "tempo", 0.5, fval);

  _assert_set_rate (pitch, "tempo", 1.0, fval);
  assert_equals_int (limit_notify_count, 8);
  _assert_set_rate (videorate, "rate", 0.5, dval);

  assert_equals_int (limit_notify_count, 8);
  _assert_duration_limit (clip, 128);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (overlay0, 10, 59, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (videorate, 10, 0, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (overlay1, 10, 51, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (pitch, 10, 0, 64, GST_CLOCK_TIME_NONE);
  _assert_rate_equal (videorate, "rate", 0.5, dval);
  _assert_rate_equal (pitch, "rate", 0.5, fval);
  _assert_rate_equal (pitch, "tempo", 1.0, fval);

  /* make videorate in-active */
  fail_unless (ges_track_element_set_active (videorate, FALSE));

  assert_equals_int (limit_notify_count, 9);
  _assert_duration_limit (clip, 64);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (overlay0, 10, 59, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (videorate, 10, 0, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (overlay1, 10, 51, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (pitch, 10, 0, 64, GST_CLOCK_TIME_NONE);
  _assert_rate_equal (videorate, "rate", 0.5, dval);
  _assert_rate_equal (pitch, "rate", 0.5, fval);
  _assert_rate_equal (pitch, "tempo", 1.0, fval);

  fail_unless (ges_track_element_set_active (videorate, TRUE));

  assert_equals_int (limit_notify_count, 10);
  _assert_duration_limit (clip, 128);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (overlay0, 10, 59, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (videorate, 10, 0, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (overlay1, 10, 51, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (pitch, 10, 0, 64, GST_CLOCK_TIME_NONE);
  _assert_rate_equal (videorate, "rate", 0.5, dval);
  _assert_rate_equal (pitch, "rate", 0.5, fval);
  _assert_rate_equal (pitch, "tempo", 1.0, fval);

  /* removing pitch, same effect as making inactive */
  _assert_remove (clip, pitch);

  assert_equals_int (limit_notify_count, 11);
  _assert_duration_limit (clip, 64);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (overlay0, 10, 59, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (videorate, 10, 0, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (overlay1, 10, 51, 64, GST_CLOCK_TIME_NONE);
  _assert_rate_equal (videorate, "rate", 0.5, dval);

  /* no max-duration will give unlimited limit */
  assert_set_max_duration (source1, GST_CLOCK_TIME_NONE);

  assert_equals_int (limit_notify_count, 12);
  _assert_duration_limit (clip, 128);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 13, 64, 77);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 13, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (overlay0, 10, 59, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (videorate, 10, 0, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (overlay1, 10, 51, 64, GST_CLOCK_TIME_NONE);
  _assert_rate_equal (videorate, "rate", 0.5, dval);

  assert_set_max_duration (source0, GST_CLOCK_TIME_NONE);

  assert_equals_int (limit_notify_count, 13);
  _assert_duration_limit (clip, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (clip, 10, 13, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (source0, 10, 13, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (source1, 10, 13, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (overlay0, 10, 59, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (videorate, 10, 0, 64, GST_CLOCK_TIME_NONE);
  CHECK_OBJECT_PROPS_MAX (overlay1, 10, 51, 64, GST_CLOCK_TIME_NONE);
  _assert_rate_equal (videorate, "rate", 0.5, dval);

  gst_object_unref (timeline);

  g_value_unset (&fval);
  g_value_unset (&dval);

  ges_deinit ();
}

GST_END_TEST;


GST_START_TEST (test_children_properties_contain)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESClip *clip;
  GList *tmp;
  GParamSpec **clips_child_props, **childrens_child_props = NULL;
  guint num_clips_props, num_childrens_props = 0;

  ges_init ();

  timeline = ges_timeline_new_audio_video ();
  layer = ges_timeline_append_layer (timeline);
  clip = GES_CLIP (ges_test_clip_new ());
  assert_set_duration (clip, 50);

  fail_unless (ges_layer_add_clip (layer, clip));

  clips_child_props =
      ges_timeline_element_list_children_properties (GES_TIMELINE_ELEMENT
      (clip), &num_clips_props);
  fail_unless (clips_child_props);
  fail_unless (num_clips_props);

  fail_unless (GES_CONTAINER_CHILDREN (clip));

  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = tmp->next)
    childrens_child_props =
        append_children_properties (childrens_child_props, tmp->data,
        &num_childrens_props);

  assert_property_list_match (clips_child_props, num_clips_props,
      childrens_child_props, num_childrens_props);

  free_children_properties (clips_child_props, num_clips_props);
  free_children_properties (childrens_child_props, num_childrens_props);

  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

static gboolean
_has_child_property (GESTimelineElement * element, GParamSpec * property)
{
  gboolean has_prop = FALSE;
  guint num_props, i;
  GParamSpec **props =
      ges_timeline_element_list_children_properties (element, &num_props);
  for (i = 0; i < num_props; i++) {
    if (props[i] == property)
      has_prop = TRUE;
    g_param_spec_unref (props[i]);
  }
  g_free (props);
  return has_prop;
}

typedef struct
{
  GstElement *child;
  GParamSpec *property;
  guint num_calls;
} PropChangedData;

#define _INIT_PROP_CHANGED_DATA(data) \
  data.child = NULL; \
  data.property = NULL; \
  data.num_calls = 0;

static void
_prop_changed_cb (GESTimelineElement * element, GstElement * child,
    GParamSpec * property, PropChangedData * data)
{
  data->num_calls++;
  data->property = property;
  data->child = child;
}

#define _assert_prop_changed_data(element, data, num_cmp, chld_cmp, prop_cmp) \
  fail_unless (num_cmp == data.num_calls, \
      "%s: num calls to callback (%u) not the expected %u", element->name, \
      data.num_calls, num_cmp); \
  fail_unless (prop_cmp == data.property, \
      "%s: property %s is not the expected property %s", element->name, \
      data.property->name, prop_cmp ? ((GParamSpec *)prop_cmp)->name : NULL); \
  fail_unless (chld_cmp == data.child, \
      "%s: child %s is not the expected child %s", element->name, \
      GST_ELEMENT_NAME (data.child), \
      chld_cmp ? GST_ELEMENT_NAME (chld_cmp) : NULL);

#define _assert_int_val_child_prop(element, val, int_cmp, prop, prop_name) \
  g_value_init (&val, G_TYPE_INT); \
  ges_timeline_element_get_child_property_by_pspec (element, prop, &val); \
  assert_equals_int (g_value_get_int (&val), int_cmp); \
  g_value_unset (&val); \
  g_value_init (&val, G_TYPE_INT); \
  fail_unless (ges_timeline_element_get_child_property ( \
        element, prop_name, &val)); \
  assert_equals_int (g_value_get_int (&val), int_cmp); \
  g_value_unset (&val); \

GST_START_TEST (test_children_properties_change)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESTimelineElement *clip, *child;
  PropChangedData clip_add_data, clip_remove_data, clip_notify_data,
      child_add_data, child_remove_data, child_notify_data;
  GstElement *sub_child;
  GParamSpec *prop1, *prop2, *prop3;
  GValue val = G_VALUE_INIT;
  gint num_buffs;

  ges_init ();

  timeline = ges_timeline_new_audio_video ();
  layer = ges_timeline_append_layer (timeline);
  clip = GES_TIMELINE_ELEMENT (ges_test_clip_new ());
  assert_set_duration (clip, 50);

  fail_unless (ges_layer_add_clip (layer, GES_CLIP (clip)));
  fail_unless (GES_CONTAINER_CHILDREN (clip));
  child = GES_CONTAINER_CHILDREN (clip)->data;

  /* fake sub-child */
  sub_child = gst_element_factory_make ("fakesink", "sub-child");
  fail_unless (sub_child);
  gst_object_ref_sink (sub_child);
  prop1 = g_object_class_find_property (G_OBJECT_GET_CLASS (sub_child),
      "num-buffers");
  fail_unless (prop1);
  prop2 = g_object_class_find_property (G_OBJECT_GET_CLASS (sub_child), "dump");
  fail_unless (prop2);
  prop3 = g_object_class_find_property (G_OBJECT_GET_CLASS (sub_child),
      "silent");
  fail_unless (prop2);

  _INIT_PROP_CHANGED_DATA (clip_add_data);
  _INIT_PROP_CHANGED_DATA (clip_remove_data);
  _INIT_PROP_CHANGED_DATA (clip_notify_data);
  _INIT_PROP_CHANGED_DATA (child_add_data);
  _INIT_PROP_CHANGED_DATA (child_remove_data);
  _INIT_PROP_CHANGED_DATA (child_notify_data);
  g_signal_connect (clip, "child-property-added",
      G_CALLBACK (_prop_changed_cb), &clip_add_data);
  g_signal_connect (clip, "child-property-removed",
      G_CALLBACK (_prop_changed_cb), &clip_remove_data);
  g_signal_connect (clip, "deep-notify",
      G_CALLBACK (_prop_changed_cb), &clip_notify_data);
  g_signal_connect (child, "child-property-added",
      G_CALLBACK (_prop_changed_cb), &child_add_data);
  g_signal_connect (child, "child-property-removed",
      G_CALLBACK (_prop_changed_cb), &child_remove_data);
  g_signal_connect (child, "deep-notify",
      G_CALLBACK (_prop_changed_cb), &child_notify_data);

  /* adding to child should also add it to the parent clip */
  fail_unless (ges_timeline_element_add_child_property (child, prop1,
          G_OBJECT (sub_child)));

  fail_unless (_has_child_property (child, prop1));
  fail_unless (_has_child_property (clip, prop1));

  _assert_prop_changed_data (clip, clip_add_data, 1, sub_child, prop1);
  _assert_prop_changed_data (clip, clip_remove_data, 0, NULL, NULL);
  _assert_prop_changed_data (clip, clip_notify_data, 0, NULL, NULL);
  _assert_prop_changed_data (child, child_add_data, 1, sub_child, prop1);
  _assert_prop_changed_data (child, child_remove_data, 0, NULL, NULL);
  _assert_prop_changed_data (child, child_notify_data, 0, NULL, NULL);

  fail_unless (ges_timeline_element_add_child_property (child, prop2,
          G_OBJECT (sub_child)));

  fail_unless (_has_child_property (child, prop2));
  fail_unless (_has_child_property (clip, prop2));

  _assert_prop_changed_data (clip, clip_add_data, 2, sub_child, prop2);
  _assert_prop_changed_data (clip, clip_remove_data, 0, NULL, NULL);
  _assert_prop_changed_data (clip, clip_notify_data, 0, NULL, NULL);
  _assert_prop_changed_data (child, child_add_data, 2, sub_child, prop2);
  _assert_prop_changed_data (child, child_remove_data, 0, NULL, NULL);
  _assert_prop_changed_data (child, child_notify_data, 0, NULL, NULL);

  /* adding to parent does not add to the child */

  fail_unless (ges_timeline_element_add_child_property (clip, prop3,
          G_OBJECT (sub_child)));

  fail_if (_has_child_property (child, prop3));
  fail_unless (_has_child_property (clip, prop3));

  _assert_prop_changed_data (clip, clip_add_data, 3, sub_child, prop3);
  _assert_prop_changed_data (clip, clip_remove_data, 0, NULL, NULL);
  _assert_prop_changed_data (clip, clip_notify_data, 0, NULL, NULL);
  _assert_prop_changed_data (child, child_add_data, 2, sub_child, prop2);
  _assert_prop_changed_data (child, child_remove_data, 0, NULL, NULL);
  _assert_prop_changed_data (child, child_notify_data, 0, NULL, NULL);

  /* both should be notified of a change in the value */

  g_object_set (G_OBJECT (sub_child), "num-buffers", 100, NULL);

  _assert_prop_changed_data (clip, clip_add_data, 3, sub_child, prop3);
  _assert_prop_changed_data (clip, clip_remove_data, 0, NULL, NULL);
  _assert_prop_changed_data (clip, clip_notify_data, 1, sub_child, prop1);
  _assert_prop_changed_data (child, child_add_data, 2, sub_child, prop2);
  _assert_prop_changed_data (child, child_remove_data, 0, NULL, NULL);
  _assert_prop_changed_data (child, child_notify_data, 1, sub_child, prop1);

  _assert_int_val_child_prop (clip, val, 100, prop1,
      "GstFakeSink::num-buffers");
  _assert_int_val_child_prop (child, val, 100, prop1,
      "GstFakeSink::num-buffers");

  g_value_init (&val, G_TYPE_INT);
  g_value_set_int (&val, 79);
  ges_timeline_element_set_child_property_by_pspec (clip, prop1, &val);
  g_value_unset (&val);

  _assert_prop_changed_data (clip, clip_add_data, 3, sub_child, prop3);
  _assert_prop_changed_data (clip, clip_remove_data, 0, NULL, NULL);
  _assert_prop_changed_data (clip, clip_notify_data, 2, sub_child, prop1);
  _assert_prop_changed_data (child, child_add_data, 2, sub_child, prop2);
  _assert_prop_changed_data (child, child_remove_data, 0, NULL, NULL);
  _assert_prop_changed_data (child, child_notify_data, 2, sub_child, prop1);

  _assert_int_val_child_prop (clip, val, 79, prop1, "GstFakeSink::num-buffers");
  _assert_int_val_child_prop (child, val, 79, prop1,
      "GstFakeSink::num-buffers");
  g_object_get (G_OBJECT (sub_child), "num-buffers", &num_buffs, NULL);
  assert_equals_int (num_buffs, 79);

  g_value_init (&val, G_TYPE_INT);
  g_value_set_int (&val, 97);
  fail_unless (ges_timeline_element_set_child_property (child,
          "GstFakeSink::num-buffers", &val));
  g_value_unset (&val);

  _assert_prop_changed_data (clip, clip_add_data, 3, sub_child, prop3);
  _assert_prop_changed_data (clip, clip_remove_data, 0, NULL, NULL);
  _assert_prop_changed_data (clip, clip_notify_data, 3, sub_child, prop1);
  _assert_prop_changed_data (child, child_add_data, 2, sub_child, prop2);
  _assert_prop_changed_data (child, child_remove_data, 0, NULL, NULL);
  _assert_prop_changed_data (child, child_notify_data, 3, sub_child, prop1);

  _assert_int_val_child_prop (clip, val, 97, prop1, "GstFakeSink::num-buffers");
  _assert_int_val_child_prop (child, val, 97, prop1,
      "GstFakeSink::num-buffers");
  g_object_get (G_OBJECT (sub_child), "num-buffers", &num_buffs, NULL);
  assert_equals_int (num_buffs, 97);

  /* remove a property from the child, removes from the parent */

  fail_unless (ges_timeline_element_remove_child_property (child, prop2));

  _assert_prop_changed_data (clip, clip_add_data, 3, sub_child, prop3);
  _assert_prop_changed_data (clip, clip_remove_data, 1, sub_child, prop2);
  _assert_prop_changed_data (clip, clip_notify_data, 3, sub_child, prop1);
  _assert_prop_changed_data (child, child_add_data, 2, sub_child, prop2);
  _assert_prop_changed_data (child, child_remove_data, 1, sub_child, prop2);
  _assert_prop_changed_data (child, child_notify_data, 3, sub_child, prop1);

  fail_if (_has_child_property (child, prop2));
  fail_if (_has_child_property (clip, prop2));

  /* removing from parent doesn't remove from child */

  fail_unless (ges_timeline_element_remove_child_property (clip, prop1));

  _assert_prop_changed_data (clip, clip_add_data, 3, sub_child, prop3);
  _assert_prop_changed_data (clip, clip_remove_data, 2, sub_child, prop1);
  _assert_prop_changed_data (clip, clip_notify_data, 3, sub_child, prop1);
  _assert_prop_changed_data (child, child_add_data, 2, sub_child, prop2);
  _assert_prop_changed_data (child, child_remove_data, 1, sub_child, prop2);
  _assert_prop_changed_data (child, child_notify_data, 3, sub_child, prop1);

  fail_unless (_has_child_property (child, prop1));
  fail_if (_has_child_property (clip, prop1));

  /* but still safe to remove it from the child later */

  fail_unless (ges_timeline_element_remove_child_property (child, prop1));

  _assert_prop_changed_data (clip, clip_add_data, 3, sub_child, prop3);
  _assert_prop_changed_data (clip, clip_remove_data, 2, sub_child, prop1);
  _assert_prop_changed_data (clip, clip_notify_data, 3, sub_child, prop1);
  _assert_prop_changed_data (child, child_add_data, 2, sub_child, prop2);
  _assert_prop_changed_data (child, child_remove_data, 2, sub_child, prop1);
  _assert_prop_changed_data (child, child_notify_data, 3, sub_child, prop1);

  fail_if (_has_child_property (child, prop1));
  fail_if (_has_child_property (clip, prop1));

  gst_object_unref (sub_child);
  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

static GESTimelineElement *
_el_with_child_prop (GESTimelineElement * clip, GObject * prop_child,
    GParamSpec * prop)
{
  GList *tmp;
  GESTimelineElement *child = NULL;
  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = tmp->next) {
    GObject *found_child;
    GParamSpec *found_prop;
    if (ges_timeline_element_lookup_child (tmp->data, prop->name,
            &found_child, &found_prop)) {
      if (found_child == prop_child && found_prop == prop) {
        g_param_spec_unref (found_prop);
        g_object_unref (found_child);
        child = tmp->data;
        break;
      }
      g_param_spec_unref (found_prop);
      g_object_unref (found_child);
    }
  }
  return child;
}

static GstTimedValue *
_new_timed_value (GstClockTime time, gdouble val)
{
  GstTimedValue *tmval = g_new0 (GstTimedValue, 1);
  tmval->value = val;
  tmval->timestamp = time;
  return tmval;
}

#define _assert_binding(element, prop_name, child, timed_vals, mode) \
{ \
  GstInterpolationMode found_mode; \
  GSList *tmp1; \
  GList *tmp2; \
  guint i; \
  GList *found_timed_vals; \
  GObject *found_object = NULL; \
  GstControlSource *source = NULL; \
  GstControlBinding *binding = ges_track_element_get_control_binding ( \
      GES_TRACK_ELEMENT (element), prop_name); \
  fail_unless (binding, "No control binding found for %s on %s", \
      prop_name, GES_TIMELINE_ELEMENT_NAME (element)); \
  g_object_get (G_OBJECT (binding), "control-source", &source, \
      "object", &found_object, NULL); \
  \
  if (child) \
    fail_unless (found_object == child); \
  g_object_unref (found_object); \
  \
  fail_unless (GST_IS_INTERPOLATION_CONTROL_SOURCE (source)); \
  found_timed_vals = gst_timed_value_control_source_get_all ( \
      GST_TIMED_VALUE_CONTROL_SOURCE (source)); \
  \
  for (i = 0, tmp1 = timed_vals, tmp2 = found_timed_vals; tmp1 && tmp2; \
      tmp1 = tmp1->next, tmp2 = tmp2->next, i++) { \
    GstTimedValue *val1 = tmp1->data, *val2 = tmp2->data; \
    gdouble diff = (val1->value > val2->value) ? \
        val1->value - val2->value : val2->value - val1->value; \
    fail_unless (val1->timestamp == val2->timestamp && diff < 0.0001, \
        "The %ith timed value (%lu: %g) does not match the found timed " \
        "value (%lu: %g)", i, val1->timestamp, val1->value, \
        val2->timestamp, val2->value); \
  } \
  fail_unless (tmp1 == NULL, "Found too few timed values"); \
  fail_unless (tmp2 == NULL, "Found too many timed values"); \
  \
  g_list_free (found_timed_vals); \
  g_object_get (G_OBJECT (source), "mode", &found_mode, NULL); \
  fail_unless (found_mode == mode); \
  g_object_unref (source); \
}

GST_START_TEST (test_copy_paste_children_properties)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESTimelineElement *clip, *copy, *pasted, *track_el, *pasted_el;
  GObject *sub_child, *pasted_sub_child;
  GParamSpec **orig_props;
  guint num_orig_props;
  GParamSpec *prop, *found_prop;
  GValue val = G_VALUE_INIT;
  GstControlSource *source;
  GSList *timed_vals;

  ges_init ();

  timeline = ges_timeline_new_audio_video ();
  layer = ges_timeline_append_layer (timeline);
  clip = GES_TIMELINE_ELEMENT (ges_source_clip_new_time_overlay ());
  assert_set_duration (clip, 50);

  fail_unless (ges_layer_add_clip (layer, GES_CLIP (clip)));

  /* get children properties */
  orig_props =
      ges_timeline_element_list_children_properties (clip, &num_orig_props);
  fail_unless (num_orig_props);

  /* font-desc is originally "", but on setting switches to Normal, so we
   * set it explicitly */
  ges_timeline_element_set_child_properties (clip, "font-desc", "Normal",
      "posx", 30, "posy", 50, "alpha", 0.1, "freq", 449.0, NULL);

  /* focus on one property */
  fail_unless (ges_timeline_element_lookup_child (clip, "posx",
          &sub_child, &prop));
  _assert_int_val_child_prop (clip, val, 30, prop, "posx");

  /* find the track element where the child property comes from */
  fail_unless (track_el = _el_with_child_prop (clip, sub_child, prop));
  _assert_int_val_child_prop (track_el, val, 30, prop, "posx");
  ges_track_element_set_auto_clamp_control_sources (GES_TRACK_ELEMENT
      (track_el), FALSE);

  /* set a control binding */
  timed_vals = g_slist_prepend (NULL, _new_timed_value (200, 5));
  timed_vals = g_slist_prepend (timed_vals, _new_timed_value (40, 50));
  timed_vals = g_slist_prepend (timed_vals, _new_timed_value (20, 10));
  timed_vals = g_slist_prepend (timed_vals, _new_timed_value (0, 20));

  source = GST_CONTROL_SOURCE (gst_interpolation_control_source_new ());
  g_object_set (G_OBJECT (source), "mode", GST_INTERPOLATION_MODE_CUBIC, NULL);
  fail_unless (gst_timed_value_control_source_set_from_list
      (GST_TIMED_VALUE_CONTROL_SOURCE (source), timed_vals));

  fail_unless (ges_track_element_set_control_source (GES_TRACK_ELEMENT
          (track_el), source, "posx", "direct-absolute"));

  g_object_unref (source);

  /* check the control binding */
  _assert_binding (track_el, "posx", sub_child, timed_vals,
      GST_INTERPOLATION_MODE_CUBIC);

  /* copy and paste */
  fail_unless (copy = ges_timeline_element_copy (clip, TRUE));
  fail_unless (pasted = ges_timeline_element_paste (copy, 30));

  gst_object_unref (copy);
  gst_object_unref (pasted);

  /* test that the new clip has the same child properties */
  assert_equal_children_properties (clip, pasted);

  /* get the details for the copied 'prop' property */
  fail_unless (ges_timeline_element_lookup_child (pasted,
          "posx", &pasted_sub_child, &found_prop));
  fail_unless (found_prop == prop);
  g_param_spec_unref (found_prop);
  fail_unless (G_OBJECT_TYPE (pasted_sub_child) == G_OBJECT_TYPE (sub_child));

  _assert_int_val_child_prop (pasted, val, 30, prop, "posx");

  /* get the associated child */
  fail_unless (pasted_el =
      _el_with_child_prop (pasted, pasted_sub_child, prop));
  _assert_int_val_child_prop (pasted_el, val, 30, prop, "posx");

  assert_equal_children_properties (track_el, pasted_el);

  /* check the control binding on the pasted element */
  _assert_binding (pasted_el, "posx", pasted_sub_child, timed_vals,
      GST_INTERPOLATION_MODE_CUBIC);

  assert_equal_bindings (pasted_el, track_el);

  /* free */
  g_slist_free_full (timed_vals, g_free);

  free_children_properties (orig_props, num_orig_props);

  g_param_spec_unref (prop);
  g_object_unref (pasted_sub_child);
  g_object_unref (sub_child);
  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

#define _THREE_TIMED_VALS(timed_vals, tm1, val1, tm2, val2, tm3, val3) \
  if (timed_vals) \
    g_slist_free_full (timed_vals, g_free); \
  timed_vals = g_slist_prepend (NULL, _new_timed_value (tm3, val3)); \
  timed_vals = g_slist_prepend (timed_vals, _new_timed_value (tm2, val2)); \
  timed_vals = g_slist_prepend (timed_vals, _new_timed_value (tm1, val1));

#define _TWO_TIMED_VALS(timed_vals, tm1, val1, tm2, val2) \
  if (timed_vals) \
    g_slist_free_full (timed_vals, g_free); \
  timed_vals = g_slist_prepend (NULL, _new_timed_value (tm2, val2)); \
  timed_vals = g_slist_prepend (timed_vals, _new_timed_value (tm1, val1));

#define _assert_control_source(obj, prop, vals) \
  _assert_binding (obj, prop, NULL, vals, GST_INTERPOLATION_MODE_LINEAR);

GST_START_TEST (test_children_property_bindings_with_rate_effects)
{
  GESTimeline *timeline;
  GESTrack *track;
  GESLayer *layer;
  GESClip *clip;
  GESTrackElement *video_source, *rate0, *rate1, *overlay;
  GstControlSource *ctrl_source;
  GSList *video_source_vals = NULL, *overlay_vals = NULL;
  GValue value = G_VALUE_INIT;
  GstControlBinding *binding;

  ges_init ();

  g_value_init (&value, G_TYPE_DOUBLE);

  timeline = ges_timeline_new ();
  track = GES_TRACK (ges_video_track_new ());
  fail_unless (ges_timeline_add_track (timeline, track));

  layer = ges_timeline_append_layer (timeline);

  clip = GES_CLIP (ges_test_clip_new ());
  assert_set_duration (clip, 4);
  assert_set_start (clip, 20);
  assert_set_inpoint (clip, 3);

  fail_unless (ges_layer_add_clip (layer, clip));

  video_source = ges_clip_find_track_element (clip, track, GES_TYPE_SOURCE);
  fail_unless (video_source);
  gst_object_unref (video_source);

  rate0 = GES_TRACK_ELEMENT (ges_effect_new ("videorate rate=0.5"));
  rate1 = GES_TRACK_ELEMENT (ges_effect_new ("videorate rate=4.0"));
  overlay = GES_TRACK_ELEMENT (ges_effect_new ("textoverlay"));
  ges_track_element_set_has_internal_source (overlay, TRUE);
  assert_set_inpoint (overlay, 9);

  fail_unless (ges_clip_add_top_effect (clip, GES_BASE_EFFECT (rate0), -1,
          NULL));
  fail_unless (ges_clip_add_top_effect (clip, GES_BASE_EFFECT (overlay), 0,
          NULL));
  fail_unless (ges_clip_add_top_effect (clip, GES_BASE_EFFECT (rate1), 0,
          NULL));

  fail_unless (ges_track_element_get_auto_clamp_control_sources (video_source));
  fail_unless (ges_track_element_get_auto_clamp_control_sources (overlay));

  /* source's alpha property */
  _THREE_TIMED_VALS (video_source_vals, 1, 0.7, 7, 1.0, 15, 0.2);

  ctrl_source = GST_CONTROL_SOURCE (gst_interpolation_control_source_new ());
  g_object_set (G_OBJECT (ctrl_source), "mode",
      GST_INTERPOLATION_MODE_LINEAR, NULL);
  fail_unless (gst_timed_value_control_source_set_from_list
      (GST_TIMED_VALUE_CONTROL_SOURCE (ctrl_source), video_source_vals));

  fail_unless (ges_track_element_set_control_source (video_source, ctrl_source,
          "alpha", "direct"));
  gst_object_unref (ctrl_source);

  /* values have been clamped between its in-point:3 and its
   * out-point:11 (4ns in timeline is 8ns in source) */
  _THREE_TIMED_VALS (video_source_vals, 3, 0.8, 7, 1.0, 11, 0.6);
  _assert_control_source (video_source, "alpha", video_source_vals);

  /* overlay's xpos property */
  _THREE_TIMED_VALS (overlay_vals, 9, 12, 17, 16, 25, 8);

  ctrl_source = GST_CONTROL_SOURCE (gst_interpolation_control_source_new ());
  g_object_set (G_OBJECT (ctrl_source), "mode",
      GST_INTERPOLATION_MODE_LINEAR, NULL);
  fail_unless (gst_timed_value_control_source_set_from_list
      (GST_TIMED_VALUE_CONTROL_SOURCE (ctrl_source), overlay_vals));

  fail_unless (ges_track_element_set_control_source (overlay, ctrl_source,
          "xpos", "direct-absolute"));
  gst_object_unref (ctrl_source);

  /* unchanged since values are at the edges already
   * in-point:9 out-point:25 (4ns in timeline is 16ns in source) */
  _assert_control_source (overlay, "xpos", overlay_vals);

  /* setting the in-point changes the in-point and out-point */
  /* increase in-point */
  assert_set_inpoint (video_source, 5);

  _THREE_TIMED_VALS (video_source_vals, 5, 0.9, 7, 1.0, 13, 0.4);
  _assert_control_source (video_source, "alpha", video_source_vals);

  /* decrease in-point */
  assert_set_inpoint (overlay, 7);

  _THREE_TIMED_VALS (overlay_vals, 7, 11, 17, 16, 23, 10);
  _assert_control_source (overlay, "xpos", overlay_vals);

  /* when trimming start, out-point should stay the same */
  fail_unless (ges_timeline_element_edit_full (GES_TIMELINE_ELEMENT (clip),
          -1, GES_EDIT_MODE_TRIM, GES_EDGE_START, 19, NULL));

  /* in-point of video_source now 3 */
  _THREE_TIMED_VALS (video_source_vals, 3, 0.8, 7, 1.0, 13, 0.4);
  _assert_control_source (video_source, "alpha", video_source_vals);

  /* in-point of video_source now 3 */
  _THREE_TIMED_VALS (overlay_vals, 3, 9, 17, 16, 23, 10);
  _assert_control_source (overlay, "xpos", overlay_vals);

  /* trim forwards */
  fail_unless (ges_timeline_element_edit_full (GES_TIMELINE_ELEMENT (clip),
          -1, GES_EDIT_MODE_TRIM, GES_EDGE_START, 20, NULL));

  /* in-point of video_source now 5 again */
  _THREE_TIMED_VALS (video_source_vals, 5, 0.9, 7, 1.0, 13, 0.4);
  _assert_control_source (video_source, "alpha", video_source_vals);

  /* in-point of overlay now 7 again */
  _THREE_TIMED_VALS (overlay_vals, 7, 11, 17, 16, 23, 10);
  _assert_control_source (overlay, "xpos", overlay_vals);

  /* trim end */
  fail_unless (ges_timeline_element_edit_full (GES_TIMELINE_ELEMENT (clip),
          -1, GES_EDIT_MODE_TRIM, GES_EDGE_END, 25, NULL));

  /* out-point of video_source now 15 */
  _THREE_TIMED_VALS (video_source_vals, 5, 0.9, 7, 1.0, 15, 0.2);
  _assert_control_source (video_source, "alpha", video_source_vals);

  /* out-point of overlay now 27 */
  _THREE_TIMED_VALS (overlay_vals, 7, 11, 17, 16, 27, 6);
  _assert_control_source (overlay, "xpos", overlay_vals);

  /* trim backwards */
  fail_unless (ges_timeline_element_edit_full (GES_TIMELINE_ELEMENT (clip),
          -1, GES_EDIT_MODE_TRIM, GES_EDGE_END, 23, NULL));

  /* out-point of video_source now 11 */
  _THREE_TIMED_VALS (video_source_vals, 5, 0.9, 7, 1.0, 11, 0.6);
  _assert_control_source (video_source, "alpha", video_source_vals);

  /* in-point of overlay now 19 */
  _THREE_TIMED_VALS (overlay_vals, 7, 11, 17, 16, 19, 14);
  _assert_control_source (overlay, "xpos", overlay_vals);

  /* changing the rate changes the out-point */
  _assert_set_rate (rate0, "rate", 1.0, value);

  /* out-point of video_source now 17 */
  _THREE_TIMED_VALS (video_source_vals, 5, 0.9, 7, 1.0, 17, 0.0);
  _assert_control_source (video_source, "alpha", video_source_vals);

  /* unchanged for overlay, which is after rate0 */
  _assert_control_source (overlay, "xpos", overlay_vals);

  /* change back */
  _assert_set_rate (rate0, "rate", 0.5, value);

  _THREE_TIMED_VALS (video_source_vals, 5, 0.9, 7, 1.0, 11, 0.6);
  _assert_control_source (video_source, "alpha", video_source_vals);

  /* unchanged for overlay, which is after rate0 */
  _assert_control_source (overlay, "xpos", overlay_vals);

  /* make inactive */
  fail_unless (ges_track_element_set_active (rate0, FALSE));

  _THREE_TIMED_VALS (video_source_vals, 5, 0.9, 7, 1.0, 17, 0.0);
  _assert_control_source (video_source, "alpha", video_source_vals);

  /* unchanged for overlay, which is after rate0 */
  _assert_control_source (overlay, "xpos", overlay_vals);

  /* make active again */
  fail_unless (ges_track_element_set_active (rate0, TRUE));

  _THREE_TIMED_VALS (video_source_vals, 5, 0.9, 7, 1.0, 11, 0.6);
  _assert_control_source (video_source, "alpha", video_source_vals);

  /* unchanged for overlay, which is after rate0 */
  _assert_control_source (overlay, "xpos", overlay_vals);

  /* change order */
  fail_unless (ges_clip_set_top_effect_index (clip, GES_BASE_EFFECT (overlay),
          2));

  /* video source unchanged */
  _assert_control_source (video_source, "alpha", video_source_vals);

  /* new out-point is 13
   * new value is interpolated between the previous value
   * (at time 7, value 11) and the *final* value (at time 19, value 14)
   * Not the middle value at time 17, value 16! */
  _TWO_TIMED_VALS (overlay_vals, 7, 11, 13, 12.5);
  _assert_control_source (overlay, "xpos", overlay_vals);

  /* removing time effect changes out-point */
  gst_object_ref (rate0);
  fail_unless (ges_clip_remove_top_effect (clip, GES_BASE_EFFECT (rate0),
          NULL));

  _THREE_TIMED_VALS (video_source_vals, 5, 0.9, 7, 1.0, 17, 0.0);
  _assert_control_source (video_source, "alpha", video_source_vals);

  _TWO_TIMED_VALS (overlay_vals, 7, 11, 19, 14);
  _assert_control_source (overlay, "xpos", overlay_vals);

  /* adding also changes it */
  fail_unless (ges_clip_add_top_effect (clip, GES_BASE_EFFECT (rate0), 2,
          NULL));

  _THREE_TIMED_VALS (video_source_vals, 5, 0.9, 7, 1.0, 11, 0.6);
  _assert_control_source (video_source, "alpha", video_source_vals);

  /* unchanged for overlay */
  _assert_control_source (overlay, "xpos", overlay_vals);

  /* new value will use the value already set at in-point if possible */

  assert_set_inpoint (video_source, 7);

  _TWO_TIMED_VALS (video_source_vals, 7, 1.0, 13, 0.4);
  _assert_control_source (video_source, "alpha", video_source_vals);

  /* same with out-point for overlay */
  binding = ges_track_element_get_control_binding (overlay, "xpos");
  fail_unless (binding);
  g_object_get (binding, "control-source", &ctrl_source, NULL);

  fail_unless (gst_timed_value_control_source_set
      (GST_TIMED_VALUE_CONTROL_SOURCE (ctrl_source), 11, 5));
  gst_object_unref (ctrl_source);
  _THREE_TIMED_VALS (overlay_vals, 7, 11, 11, 5, 19, 14);

  _assert_control_source (overlay, "xpos", overlay_vals);

  fail_unless (ges_timeline_element_edit_full (GES_TIMELINE_ELEMENT (clip),
          -1, GES_EDIT_MODE_TRIM, GES_EDGE_END, 21, NULL));

  _TWO_TIMED_VALS (video_source_vals, 7, 1.0, 9, 0.8);
  _assert_control_source (video_source, "alpha", video_source_vals);

  /* overlay uses existing value, rather than an interpolation */
  _TWO_TIMED_VALS (overlay_vals, 7, 11, 11, 5);
  _assert_control_source (overlay, "xpos", overlay_vals);

  g_slist_free_full (video_source_vals, g_free);
  g_slist_free_full (overlay_vals, g_free);

  gst_object_unref (timeline);
  g_value_unset (&value);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_unchanged_after_layer_add_failure)
{
  GList *found;
  GESTrack *track;
  GESTimeline *timeline;
  GESLayer *layer;
  GESClip *clip0, *clip1;
  GESTimelineElement *effect, *source;

  ges_init ();

  timeline = ges_timeline_new ();
  layer = ges_timeline_append_layer (timeline);

  /* two video tracks */
  track = GES_TRACK (ges_video_track_new ());
  fail_unless (ges_timeline_add_track (timeline, track));

  track = GES_TRACK (ges_video_track_new ());
  fail_unless (ges_timeline_add_track (timeline, track));

  clip0 = GES_CLIP (ges_test_clip_new ());
  clip1 = GES_CLIP (ges_test_clip_new ());

  gst_object_ref (clip0);
  gst_object_ref (clip1);

  assert_set_start (clip0, 0);
  assert_set_duration (clip0, 10);
  assert_set_start (clip1, 0);
  assert_set_duration (clip1, 10);

  effect = GES_TIMELINE_ELEMENT (ges_effect_new ("agingtv"));
  _assert_add (clip1, effect);

  assert_num_children (clip0, 0);
  assert_num_children (clip1, 1);

  fail_unless (ges_layer_add_clip (layer, clip0));

  assert_num_children (clip0, 2);
  assert_num_children (clip1, 1);

  fail_unless (GES_CONTAINER_CHILDREN (clip1)->data == effect);

  /* addition should fail since sources would fully overlap */
  fail_if (ges_layer_add_clip (layer, clip1));

  /* children should be the same */
  assert_num_children (clip0, 2);
  assert_num_children (clip1, 1);

  fail_unless (GES_CONTAINER_CHILDREN (clip1)->data == effect);

  /* should be able to add again once we have fixed the problem */
  fail_unless (ges_layer_remove_clip (layer, clip0));

  assert_num_children (clip0, 2);
  assert_num_children (clip1, 1);

  fail_unless (ges_layer_add_clip (layer, clip1));

  assert_num_children (clip0, 2);
  /* now has two sources and two effects */
  assert_num_children (clip1, 4);

  found = ges_clip_find_track_elements (clip1, NULL, GES_TRACK_TYPE_VIDEO,
      GES_TYPE_VIDEO_SOURCE);
  fail_unless_equals_int (g_list_length (found), 2);
  g_list_free_full (found, gst_object_unref);

  found = ges_clip_find_track_elements (clip1, NULL, GES_TRACK_TYPE_VIDEO,
      GES_TYPE_EFFECT);
  fail_unless_equals_int (g_list_length (found), 2);
  g_list_free_full (found, gst_object_unref);

  /* similarly cannot add clip0 back, and children should not change */
  /* remove the extra source */
  _assert_remove (clip0, GES_CONTAINER_CHILDREN (clip0)->data);
  assert_num_children (clip0, 1);
  source = GES_CONTAINER_CHILDREN (clip0)->data;

  fail_if (ges_layer_add_clip (layer, clip0));

  /* children should be the same */
  assert_num_children (clip0, 1);
  assert_num_children (clip1, 4);

  fail_unless (GES_CONTAINER_CHILDREN (clip0)->data == source);

  gst_object_unref (clip0);
  gst_object_unref (clip1);
  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

#define _assert_timeline_to_internal(clip, child, in, expect_out) \
{\
  GError *error = NULL; \
  GstClockTime found = ges_clip_get_internal_time_from_timeline_time ( \
        clip, child, (in) * GST_SECOND, &error); \
  GstClockTime expect =  expect_out * GST_SECOND; \
  fail_unless (found == expect, "Conversion from timeline time %" \
      GST_TIME_FORMAT " to the internal time of %" GES_FORMAT " gave %" \
      GST_TIME_FORMAT " rather than the expected %" GST_TIME_FORMAT \
      " (error: %s)", GST_TIME_ARGS ((in) * GST_SECOND), \
      GES_ARGS (child), GST_TIME_ARGS (found), GST_TIME_ARGS (expect), \
      error ? error->message : "None"); \
  fail_if (error); \
}

#define _assert_timeline_to_internal_fails(clip, child, in, error_code) \
{ \
  GError *error = NULL; \
  GstClockTime found = ges_clip_get_internal_time_from_timeline_time ( \
        clip, child, (in) * GST_SECOND, &error); \
  fail_if (GST_CLOCK_TIME_IS_VALID (found), "Conversion from timeline " \
      "time %" GST_TIME_FORMAT " to the internal time of %" GES_FORMAT \
      " successfully converted to %" GST_TIME_FORMAT " rather than " \
      "GST_CLOCK_TIME_NONE", GST_TIME_ARGS ((in) * GST_SECOND), \
      GES_ARGS (child), GST_TIME_ARGS (found)); \
  assert_GESError (error, error_code); \
}

#define _assert_internal_to_timeline(clip, child, in, expect_out) \
{\
  GError *error = NULL; \
  GstClockTime found = ges_clip_get_timeline_time_from_internal_time ( \
        clip, child, (in) * GST_SECOND, &error); \
  GstClockTime expect = expect_out * GST_SECOND; \
  fail_unless (found == expect, "Conversion from the internal time %" \
      GST_TIME_FORMAT " of %" GES_FORMAT " to the timeline time gave %" \
      GST_TIME_FORMAT " rather than the expected %" GST_TIME_FORMAT, \
      GST_TIME_ARGS ((in) * GST_SECOND), GES_ARGS (child), \
      GST_TIME_ARGS (found), GST_TIME_ARGS (expect)); \
  fail_if (error); \
}

#define _assert_internal_to_timeline_fails(clip, child, in, error_code) \
{\
  GError *error = NULL; \
  GstClockTime found = ges_clip_get_timeline_time_from_internal_time ( \
        clip, child, (in) * GST_SECOND, &error); \
  fail_if (GST_CLOCK_TIME_IS_VALID (found), "Conversion from the " \
      "internal time %" GST_TIME_FORMAT " of %" GES_FORMAT " to the " \
      "timeline time gave %" GST_TIME_FORMAT " rather than " \
      "GST_CLOCK_TIME_NONE", GST_TIME_ARGS ((in) * GST_SECOND), \
      GES_ARGS (child), GST_TIME_ARGS (found)); \
  assert_GESError (error, error_code); \
}

#define _assert_frame_to_timeline(clip, frame, expect_out) \
{\
  GError *error = NULL; \
  GstClockTime found = ges_clip_get_timeline_time_from_source_frame ( \
        clip, frame, &error); \
  GstClockTime expect = expect_out * GST_SECOND; \
  fail_unless (found == expect, "Conversion from the source frame %" \
      G_GINT64_FORMAT " to the timeline time gave %" GST_TIME_FORMAT \
      " rather than the expected %" GST_TIME_FORMAT, frame, \
      GST_TIME_ARGS (found), GST_TIME_ARGS (expect)); \
  fail_if (error); \
}

#define _assert_frame_to_timeline_fails(clip, frame, error_code) \
{\
  GError *error = NULL; \
  GstClockTime found = ges_clip_get_timeline_time_from_source_frame ( \
        clip, frame, &error); \
  fail_if (GST_CLOCK_TIME_IS_VALID (found), "Conversion from the " \
      "source frame %" G_GINT64_FORMAT " to the timeline time gave %" \
      GST_TIME_FORMAT " rather than the expected GST_CLOCK_TIME_NONE", \
      frame, GST_TIME_ARGS (found)); \
  assert_GESError (error, error_code); \
}

GST_START_TEST (test_convert_time)
{
  GESTimeline *timeline;
  GESTrack *track0, *track1;
  GESAsset *asset;
  GESLayer *layer;
  GESClip *clip;
  GESTrackElement *source0, *source1, *rate0, *rate1, *rate2, *overlay;
  GValue val = G_VALUE_INIT;

  ges_init ();

  asset = ges_asset_request (GES_TYPE_TEST_CLIP,
      "framerate=30/1, max-duration=93.0", NULL);
  fail_unless (asset);

  timeline = ges_timeline_new ();

  track0 = GES_TRACK (ges_video_track_new ());
  track1 = GES_TRACK (ges_video_track_new ());

  fail_unless (ges_timeline_add_track (timeline, track0));
  fail_unless (ges_timeline_add_track (timeline, track1));

  layer = ges_timeline_append_layer (timeline);

  clip = ges_layer_add_asset (layer, asset, 20 * GST_SECOND,
      13 * GST_SECOND, 10 * GST_SECOND, GES_TRACK_TYPE_VIDEO);
  fail_unless (clip);
  CHECK_OBJECT_PROPS_MAX (clip, 20 * GST_SECOND, 13 * GST_SECOND,
      10 * GST_SECOND, 93 * GST_SECOND);

  source0 =
      ges_clip_find_track_element (clip, track0, GES_TYPE_VIDEO_TEST_SOURCE);
  source1 =
      ges_clip_find_track_element (clip, track1, GES_TYPE_VIDEO_TEST_SOURCE);

  rate0 = GES_TRACK_ELEMENT (ges_effect_new ("videorate"));
  rate1 = GES_TRACK_ELEMENT (ges_effect_new ("videorate"));
  rate2 = GES_TRACK_ELEMENT (ges_effect_new ("videorate"));
  overlay = GES_TRACK_ELEMENT (ges_effect_new ("textoverlay"));
  ges_track_element_set_has_internal_source (overlay, TRUE);
  /* enough internal content to last 10 seconds at a rate of 4.0 */
  assert_set_inpoint (overlay, 7 * GST_SECOND);
  assert_set_max_duration (overlay, 50 * GST_SECOND);

  fail_unless (ges_track_add_element (track0, rate0));
  fail_unless (ges_track_add_element (track1, rate1));
  fail_unless (ges_track_add_element (track1, rate2));
  fail_unless (ges_track_add_element (track1, overlay));

  _assert_add (clip, rate0);
  _assert_add (clip, rate2);
  _assert_add (clip, overlay);
  _assert_add (clip, rate1);

  /* in track0:
   *
   * source0 -> rate0 -> out
   *
   * in track1:
   *
   * source1 -> rate1 -> overlay -> rate2 -> out
   */

  g_value_init (&val, G_TYPE_DOUBLE);

  _assert_rate_equal (rate0, "rate", 1.0, val);
  _assert_rate_equal (rate1, "rate", 1.0, val);
  _assert_rate_equal (rate2, "rate", 1.0, val);

  /* without rates */

  /* start of the clip */
  _assert_internal_to_timeline (clip, source0, 13, 20);
  _assert_internal_to_timeline (clip, source1, 13, 20);
  _assert_internal_to_timeline (clip, overlay, 7, 20);
  _assert_frame_to_timeline (clip, 390, 20);
  _assert_timeline_to_internal (clip, source0, 20, 13);
  _assert_timeline_to_internal (clip, source1, 20, 13);
  _assert_timeline_to_internal (clip, overlay, 20, 7);

  /* middle of the clip */
  _assert_internal_to_timeline (clip, source0, 18, 25);
  _assert_internal_to_timeline (clip, source1, 18, 25);
  _assert_internal_to_timeline (clip, overlay, 12, 25);
  _assert_frame_to_timeline (clip, 540, 25);
  _assert_timeline_to_internal (clip, source0, 25, 18);
  _assert_timeline_to_internal (clip, source1, 25, 18);
  _assert_timeline_to_internal (clip, overlay, 25, 12);

  /* end of the clip */
  _assert_internal_to_timeline (clip, source0, 23, 30);
  _assert_internal_to_timeline (clip, source1, 23, 30);
  _assert_internal_to_timeline (clip, overlay, 17, 30);
  _assert_frame_to_timeline (clip, 690, 30);
  _assert_timeline_to_internal (clip, source0, 30, 23);
  _assert_timeline_to_internal (clip, source1, 30, 23);
  _assert_timeline_to_internal (clip, overlay, 30, 17);

  /* beyond the end of the clip */
  /* exceeds the max-duration of the elements, but that is ok */
  _assert_internal_to_timeline (clip, source0, 123, 130);
  _assert_internal_to_timeline (clip, source1, 123, 130);
  _assert_internal_to_timeline (clip, overlay, 117, 130);
  _assert_frame_to_timeline (clip, 3690, 130);
  _assert_timeline_to_internal (clip, source0, 130, 123);
  _assert_timeline_to_internal (clip, source1, 130, 123);
  _assert_timeline_to_internal (clip, overlay, 130, 117);

  /* before the start of the clip */
  _assert_internal_to_timeline (clip, source0, 8, 15);
  _assert_internal_to_timeline (clip, source1, 8, 15);
  _assert_internal_to_timeline (clip, overlay, 2, 15);
  _assert_frame_to_timeline (clip, 240, 15);
  _assert_timeline_to_internal (clip, source0, 15, 8);
  _assert_timeline_to_internal (clip, source1, 15, 8);
  _assert_timeline_to_internal (clip, overlay, 15, 2);

  /* too early for overlay */
  _assert_timeline_to_internal (clip, source0, 10, 3);
  _assert_timeline_to_internal (clip, source1, 10, 3);
  _assert_timeline_to_internal_fails (clip, overlay, 10,
      GES_ERROR_NEGATIVE_TIME);

  /* too early for sources */
  _assert_timeline_to_internal_fails (clip, source0, 5,
      GES_ERROR_NEGATIVE_TIME);
  _assert_timeline_to_internal_fails (clip, source1, 5,
      GES_ERROR_NEGATIVE_TIME);
  _assert_timeline_to_internal_fails (clip, overlay, 5,
      GES_ERROR_NEGATIVE_TIME);

  assert_set_start (clip, 10 * GST_SECOND);

  /* too early in the timeline */
  _assert_internal_to_timeline_fails (clip, source0, 2,
      GES_ERROR_NEGATIVE_TIME);
  _assert_internal_to_timeline_fails (clip, source1, 2,
      GES_ERROR_NEGATIVE_TIME);
  _assert_internal_to_timeline (clip, overlay, 2, 5);
  _assert_frame_to_timeline_fails (clip, 60, GES_ERROR_INVALID_FRAME_NUMBER);

  assert_set_start (clip, 6 * GST_SECOND);
  _assert_internal_to_timeline_fails (clip, source0, 6,
      GES_ERROR_NEGATIVE_TIME);
  _assert_internal_to_timeline_fails (clip, source1, 6,
      GES_ERROR_NEGATIVE_TIME);
  _assert_internal_to_timeline_fails (clip, overlay, 0,
      GES_ERROR_NEGATIVE_TIME);
  _assert_frame_to_timeline_fails (clip, 180, GES_ERROR_INVALID_FRAME_NUMBER);

  assert_set_start (clip, 20 * GST_SECOND);

  /* now with rate effects
   * Note, they are currently out of sync */
  _assert_set_rate (rate0, "rate", 0.5, val);
  _assert_set_rate (rate1, "rate", 2.0, val);
  _assert_set_rate (rate2, "rate", 4.0, val);

  CHECK_OBJECT_PROPS_MAX (clip, 20 * GST_SECOND, 13 * GST_SECOND,
      10 * GST_SECOND, 93 * GST_SECOND);

  /* start of the clip is the same */
  _assert_internal_to_timeline (clip, source0, 13, 20);
  _assert_internal_to_timeline (clip, source1, 13, 20);
  _assert_internal_to_timeline (clip, overlay, 7, 20);
  _assert_timeline_to_internal (clip, source0, 20, 13);
  _assert_timeline_to_internal (clip, source1, 20, 13);
  _assert_timeline_to_internal (clip, overlay, 20, 7);

  /* middle is different */
  /* 5 seconds in the timeline is 2.5 seconds into the source */
  _assert_internal_to_timeline (clip, source0, 15.5, 25);
  /* 5 seconds in the timeline is 40 seconds into the source */
  _assert_internal_to_timeline (clip, source1, 53, 25);
  /* 5 seconds in the timeline is 20 seconds into the source */
  _assert_internal_to_timeline (clip, overlay, 27, 25);
  /* reverse */
  _assert_timeline_to_internal (clip, source0, 25, 15.5);
  _assert_timeline_to_internal (clip, source1, 25, 53);
  _assert_timeline_to_internal (clip, overlay, 25, 27);

  /* end is different */
  _assert_internal_to_timeline (clip, source0, 18, 30);
  _assert_internal_to_timeline (clip, source1, 93, 30);
  _assert_internal_to_timeline (clip, overlay, 47, 30);
  _assert_timeline_to_internal (clip, source0, 30, 18);
  _assert_timeline_to_internal (clip, source1, 30, 93);
  _assert_timeline_to_internal (clip, overlay, 30, 47);

  /* beyond end is different */
  _assert_internal_to_timeline (clip, source0, 68, 130);
  _assert_internal_to_timeline (clip, source1, 893, 130);
  _assert_internal_to_timeline (clip, overlay, 447, 130);
  _assert_timeline_to_internal (clip, source0, 130, 68);
  _assert_timeline_to_internal (clip, source1, 130, 893);
  _assert_timeline_to_internal (clip, overlay, 130, 447);

  /* before the start */
  _assert_internal_to_timeline (clip, source0, 12.5, 19);
  _assert_internal_to_timeline (clip, source1, 5, 19);
  _assert_internal_to_timeline (clip, overlay, 3, 19);
  _assert_timeline_to_internal (clip, source0, 19, 12.5);
  _assert_timeline_to_internal (clip, source1, 19, 5);
  _assert_timeline_to_internal (clip, overlay, 19, 3);

  /* too early for source1 and overlay */
  _assert_internal_to_timeline (clip, source0, 12, 18);
  _assert_timeline_to_internal (clip, source0, 18, 12);
  _assert_timeline_to_internal_fails (clip, source1, 18,
      GES_ERROR_NEGATIVE_TIME);
  _assert_timeline_to_internal_fails (clip, overlay, 18,
      GES_ERROR_NEGATIVE_TIME);

  assert_set_inpoint (overlay, 8 * GST_SECOND);
  /* now fine */
  _assert_internal_to_timeline (clip, overlay, 0, 18);
  _assert_timeline_to_internal (clip, overlay, 18, 0);

  assert_set_inpoint (overlay, 7 * GST_SECOND);

  /* still not too early for source0 */
  _assert_internal_to_timeline (clip, source0, 5.5, 5);
  _assert_timeline_to_internal (clip, source0, 5, 5.5);
  _assert_timeline_to_internal_fails (clip, source1, 5,
      GES_ERROR_NEGATIVE_TIME);
  _assert_timeline_to_internal_fails (clip, overlay, 5,
      GES_ERROR_NEGATIVE_TIME);

  _assert_internal_to_timeline (clip, source0, 3, 0);
  _assert_timeline_to_internal (clip, source0, 0, 3);
  _assert_timeline_to_internal_fails (clip, source1, 5,
      GES_ERROR_NEGATIVE_TIME);
  _assert_timeline_to_internal_fails (clip, overlay, 5,
      GES_ERROR_NEGATIVE_TIME);

  /* too early for the timeline */
  _assert_internal_to_timeline_fails (clip, source0, 2,
      GES_ERROR_NEGATIVE_TIME);

  /* re-sync rates between tracks */
  _assert_set_rate (rate2, "rate", 0.25, val);

  CHECK_OBJECT_PROPS_MAX (clip, 20 * GST_SECOND, 13 * GST_SECOND,
      10 * GST_SECOND, 93 * GST_SECOND);

  /* start of the clip */
  _assert_internal_to_timeline (clip, source0, 13, 20);
  _assert_internal_to_timeline (clip, source1, 13, 20);
  _assert_internal_to_timeline (clip, overlay, 7, 20);
  _assert_frame_to_timeline (clip, 390, 20);
  _assert_timeline_to_internal (clip, source0, 20, 13);
  _assert_timeline_to_internal (clip, source1, 20, 13);
  _assert_timeline_to_internal (clip, overlay, 20, 7);

  /* middle of the clip */
  _assert_internal_to_timeline (clip, source0, 15.5, 25);
  _assert_internal_to_timeline (clip, source1, 15.5, 25);
  _assert_internal_to_timeline (clip, overlay, 8.25, 25);
  _assert_frame_to_timeline (clip, 465, 25);
  _assert_timeline_to_internal (clip, source0, 25, 15.5);
  _assert_timeline_to_internal (clip, source1, 25, 15.5);
  _assert_timeline_to_internal (clip, overlay, 25, 8.25);

  /* end of the clip */
  _assert_internal_to_timeline (clip, source0, 18, 30);
  _assert_internal_to_timeline (clip, source1, 18, 30);
  _assert_internal_to_timeline (clip, overlay, 9.5, 30);
  _assert_frame_to_timeline (clip, 540, 30);
  _assert_timeline_to_internal (clip, source0, 30, 18);
  _assert_timeline_to_internal (clip, source1, 30, 18);
  _assert_timeline_to_internal (clip, overlay, 30, 9.5);

  /* beyond the end of the clip */
  /* exceeds the max-duration of the elements, but that is ok */
  _assert_internal_to_timeline (clip, source0, 68, 130);
  _assert_internal_to_timeline (clip, source1, 68, 130);
  _assert_internal_to_timeline (clip, overlay, 34.5, 130);
  _assert_frame_to_timeline (clip, 2040, 130);
  _assert_timeline_to_internal (clip, source0, 130, 68);
  _assert_timeline_to_internal (clip, source1, 130, 68);
  _assert_timeline_to_internal (clip, overlay, 130, 34.5);

  /* before the start of the clip */
  _assert_internal_to_timeline (clip, source0, 10.5, 15);
  _assert_internal_to_timeline (clip, source1, 10.5, 15);
  _assert_internal_to_timeline (clip, overlay, 5.75, 15);
  _assert_frame_to_timeline (clip, 315, 15);
  _assert_timeline_to_internal (clip, source0, 15, 10.5);
  _assert_timeline_to_internal (clip, source1, 15, 10.5);
  _assert_timeline_to_internal (clip, overlay, 15, 5.75);

  /* not too early */
  _assert_internal_to_timeline (clip, source0, 3, 0);
  _assert_internal_to_timeline (clip, source1, 3, 0);
  _assert_internal_to_timeline (clip, overlay, 2, 0);
  _assert_frame_to_timeline (clip, 90, 0);
  _assert_timeline_to_internal (clip, source0, 0, 3);
  _assert_timeline_to_internal (clip, source1, 0, 3);
  _assert_timeline_to_internal (clip, overlay, 0, 2);

  /* too early for timeline */
  _assert_internal_to_timeline_fails (clip, source0, 2,
      GES_ERROR_NEGATIVE_TIME);
  _assert_internal_to_timeline_fails (clip, source1, 2,
      GES_ERROR_NEGATIVE_TIME);
  _assert_internal_to_timeline_fails (clip, overlay, 1,
      GES_ERROR_NEGATIVE_TIME);
  _assert_frame_to_timeline_fails (clip, 89, GES_ERROR_INVALID_FRAME_NUMBER);

  assert_set_start (clip, 30 * GST_SECOND);
  /* timeline times have shifted by 10 */
  _assert_timeline_to_internal (clip, source0, 10, 3);
  _assert_timeline_to_internal (clip, source1, 10, 3);
  _assert_timeline_to_internal (clip, overlay, 10, 2);

  _assert_timeline_to_internal (clip, source0, 4, 0);
  _assert_timeline_to_internal (clip, source1, 4, 0);
  _assert_timeline_to_internal (clip, overlay, 2, 0);
  /* too early for internal */
  _assert_timeline_to_internal_fails (clip, source0, 3,
      GES_ERROR_NEGATIVE_TIME);
  _assert_timeline_to_internal_fails (clip, source1, 3,
      GES_ERROR_NEGATIVE_TIME);
  _assert_timeline_to_internal_fails (clip, overlay, 1,
      GES_ERROR_NEGATIVE_TIME);

  g_value_unset (&val);
  gst_object_unref (asset);
  gst_object_unref (timeline);

  ges_deinit ();
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
  tcase_add_test (tc_chain, test_split_ordering);
  tcase_add_test (tc_chain, test_split_direct_bindings);
  tcase_add_test (tc_chain, test_split_direct_absolute_bindings);
  tcase_add_test (tc_chain, test_split_with_auto_transitions);
  tcase_add_test (tc_chain, test_clip_group_ungroup);
  tcase_add_test (tc_chain, test_clip_can_group);
  tcase_add_test (tc_chain, test_adding_children_to_track);
  tcase_add_test (tc_chain, test_clip_refcount_remove_child);
  tcase_add_test (tc_chain, test_clip_find_track_element);
  tcase_add_test (tc_chain, test_effects_priorities);
  tcase_add_test (tc_chain, test_children_time_setters);
  tcase_add_test (tc_chain, test_not_enough_internal_content_for_core);
  tcase_add_test (tc_chain, test_can_add_effect);
  tcase_add_test (tc_chain, test_children_active);
  tcase_add_test (tc_chain, test_children_inpoint);
  tcase_add_test (tc_chain, test_children_max_duration);
  tcase_add_test (tc_chain, test_duration_limit);
  tcase_add_test (tc_chain, test_can_set_duration_limit);
  tcase_add_test (tc_chain, test_rate_effects_duration_limit);
  tcase_add_test (tc_chain, test_children_properties_contain);
  tcase_add_test (tc_chain, test_children_properties_change);
  tcase_add_test (tc_chain, test_copy_paste_children_properties);
  tcase_add_test (tc_chain, test_children_property_bindings_with_rate_effects);
  tcase_add_test (tc_chain, test_unchanged_after_layer_add_failure);
  tcase_add_test (tc_chain, test_convert_time);

  return s;
}

GST_CHECK_MAIN (ges);
