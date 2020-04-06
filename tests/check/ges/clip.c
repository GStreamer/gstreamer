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

  ges_container_remove (GES_CONTAINER (clip),
      GES_TIMELINE_ELEMENT (trackelement));

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
  trackelement2 = GES_CONTAINER_CHILDREN (clip)->next->data;
  fail_unless (trackelement2 != NULL);
  fail_unless (GES_TIMELINE_ELEMENT_PARENT (trackelement2) ==
      GES_TIMELINE_ELEMENT (clip));

  effect1 = GES_TRACK_ELEMENT (ges_effect_new ("agingtv"));
  ges_container_add (GES_CONTAINER (clip), GES_TIMELINE_ELEMENT (effect1));

  effect2 = GES_TRACK_ELEMENT (ges_effect_new ("vertigotv"));
  ges_container_add (GES_CONTAINER (clip), GES_TIMELINE_ELEMENT (effect2));

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

  splittrackelement =
      GES_TRACK_ELEMENT (splittrackelements->next->next->next->data);
  fail_unless (GES_IS_TRACK_ELEMENT (splittrackelement));
  CHECK_OBJECT_PROPS (splittrackelement, 67, 37, 25);

  assert_equal_children_properties (splittrackelement, trackelement1);
  fail_unless (ges_track_element_get_track (splittrackelement) == track1);
  fail_unless (ges_track_element_get_track (trackelement1) == track1);
  assert_equals_int (GES_TIMELINE_ELEMENT_PRIORITY (splittrackelement),
      priority1 + 3);
  fail_unless (GES_TIMELINE_ELEMENT_PRIORITY (trackelement1) == priority2);

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
  fail_unless (ges_container_add (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (el)));

  el = GES_TRACK_ELEMENT (ges_effect_new ("agingtv"));
  ges_track_element_set_track_type (el, GES_TRACK_TYPE_VIDEO);
  fail_unless (ges_container_add (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (el)));

  el = GES_TRACK_ELEMENT (ges_effect_new ("videobalance"));
  ges_track_element_set_track_type (el, GES_TRACK_TYPE_VIDEO);
  fail_unless (ges_container_add (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (el)));

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

  ges_timeline_element_set_start (GES_TIMELINE_ELEMENT (video_clip), 10);
  CHECK_OBJECT_PROPS (video_clip, 10, 0, 10);
  CHECK_OBJECT_PROPS (audio_clip, 0, 0, 10);

  _assert_regroup_fails (containers);

  ges_timeline_element_set_start (GES_TIMELINE_ELEMENT (video_clip), 0);
  ges_timeline_element_set_inpoint (GES_TIMELINE_ELEMENT (video_clip), 10);
  CHECK_OBJECT_PROPS (video_clip, 0, 10, 10);
  CHECK_OBJECT_PROPS (audio_clip, 0, 0, 10);

  _assert_regroup_fails (containers);

  ges_timeline_element_set_inpoint (GES_TIMELINE_ELEMENT (video_clip), 0);
  ges_timeline_element_set_duration (GES_TIMELINE_ELEMENT (video_clip), 15);
  CHECK_OBJECT_PROPS (video_clip, 0, 0, 15);
  CHECK_OBJECT_PROPS (audio_clip, 0, 0, 10);

  _assert_regroup_fails (containers);

  ges_timeline_element_set_duration (GES_TIMELINE_ELEMENT (video_clip), 10);
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
  fail_unless (ges_container_add (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (effect)));
  effect2 = GES_TRACK_ELEMENT (ges_effect_new ("vertigotv"));
  fail_unless (ges_container_add (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (effect2)));
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
  fail_if (ges_clip_add_child_to_track (clip, source, track2, NULL));
  assert_num_children (clip, 3);
  fail_unless (ges_track_element_get_track (source) == track1);
  assert_num_in_track (track1, 3);
  assert_num_in_track (track2, 0);

  /* can't add the clip to a track that already contains our source */
  fail_if (ges_clip_add_child_to_track (clip, source, track1, NULL));
  assert_num_children (clip, 3);
  fail_unless (ges_track_element_get_track (source) == track1);
  assert_num_in_track (track1, 3);
  assert_num_in_track (track2, 0);

  /* can't remove a core element from its track whilst a non-core sits
   * above it */
  fail_if (ges_track_remove_element (track1, source));
  assert_num_children (clip, 3);
  fail_unless (ges_track_element_get_track (source) == track1);
  assert_num_in_track (track1, 3);
  assert_num_in_track (track2, 0);

  /* can not add to the same track as it is currently in */
  fail_if (ges_clip_add_child_to_track (clip, effect, track1, NULL));
  fail_unless (ges_track_element_get_track (effect) == track1);
  assert_num_in_track (track1, 3);
  assert_num_in_track (track2, 0);

  /* adding another video track, select-tracks-for-object will do nothing
   * since no each track element is already part of a track */
  fail_unless (ges_timeline_add_track (timeline, track2));
  assert_num_children (clip, 3);
  assert_num_in_track (track1, 3);
  assert_num_in_track (track2, 0);

  /* can not add effect to a track that does not contain a core child */
  fail_if (ges_clip_add_child_to_track (clip, effect, track2, NULL));
  assert_num_children (clip, 3);
  assert_num_in_track (track1, 3);
  assert_num_in_track (track2, 0);

  /* can add core */

  added = ges_clip_add_child_to_track (clip, source, track2, NULL);
  fail_unless (added);
  assert_num_children (clip, 4);
  fail_unless (added != source);
  fail_unless (ges_track_element_get_track (source) == track1);
  fail_unless (ges_track_element_get_track (added) == track2);
  assert_num_in_track (track1, 3);
  assert_num_in_track (track2, 1);

  assert_equal_children_properties (added, source);
  assert_equal_bindings (added, source);

  /* can now add non-core */
  assert_equals_int (0,
      ges_clip_get_top_effect_index (clip, GES_BASE_EFFECT (effect)));
  assert_equals_int (1,
      ges_clip_get_top_effect_index (clip, GES_BASE_EFFECT (effect2)));

  added2 = ges_clip_add_child_to_track (clip, effect, track2, NULL);
  fail_unless (added2);
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

  added3 = ges_clip_add_child_to_track (clip, effect2, track2, NULL);
  fail_unless (added3);
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
  fail_unless (ges_container_remove (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (added)));
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

  fail_unless (ges_container_remove (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (added2)));
  fail_unless (ges_container_remove (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (added3)));
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
  fail_if (ges_clip_add_child_to_track (clip, source, track1, NULL));
  assert_num_children (clip, 3);
  assert_num_in_track (track1, 4);

  /* can not add source at time 23 because it would result in three
   * overlapping sources in the track */
  fail_unless (ges_timeline_element_set_start (GES_TIMELINE_ELEMENT (clip),
          23));
  fail_if (ges_clip_add_child_to_track (clip, source, track1, NULL));
  assert_num_children (clip, 3);
  assert_num_in_track (track1, 4);

  /* can add at 5, with overlap */
  fail_unless (ges_timeline_element_set_start (GES_TIMELINE_ELEMENT (clip), 5));
  added = ges_clip_add_child_to_track (clip, source, track1, NULL);
  /* added is the source since it was not already in a track */
  fail_unless (added == source);
  assert_num_children (clip, 3);
  /* 4 sources + 2 transitions */
  assert_num_in_track (track1, 6);

  /* also add effect */
  added = ges_clip_add_child_to_track (clip, effect, track1, NULL);
  /* added is the source since it was not already in a track */
  fail_unless (added == effect);
  assert_num_children (clip, 3);
  assert_num_in_track (track1, 7);

  added = ges_clip_add_child_to_track (clip, effect2, track1, NULL);
  /* added is the source since it was not already in a track */
  fail_unless (added == effect2);
  assert_num_children (clip, 3);
  assert_num_in_track (track1, 8);

  assert_equals_int (0,
      ges_clip_get_top_effect_index (clip, GES_BASE_EFFECT (effect)));
  assert_equals_int (1,
      ges_clip_get_top_effect_index (clip, GES_BASE_EFFECT (effect2)));

  gst_object_unref (timeline);

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

  fail_unless (ges_container_add (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (effect)));
  assert_num_children (clip, 2);
  ASSERT_OBJECT_REFCOUNT (effect, "1 for the container + 1 for the track"
      " + 1 timeline", 3);

  fail_unless (ges_track_remove_element (track, effect));
  ASSERT_OBJECT_REFCOUNT (effect, "1 for the container", 1);

  g_signal_connect (clip, "child-removed", G_CALLBACK (child_removed_cb),
      &called);
  gst_object_ref (effect);
  fail_unless (ges_container_remove (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (effect)));
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

  effect = GES_TRACK_ELEMENT (ges_effect_new ("identity"));
  fail_unless (ges_track_add_element (track, effect));
  fail_unless (ges_container_add (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (effect)));

  effect1 = GES_TRACK_ELEMENT (ges_effect_new ("identity"));
  fail_unless (ges_track_add_element (track1, effect1));
  fail_unless (ges_container_add (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (effect1)));

  effect2 = GES_TRACK_ELEMENT (ges_effect_new ("identity"));
  fail_unless (ges_track_add_element (track2, effect2));
  fail_unless (ges_container_add (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (effect2)));

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
  fail_unless (ges_container_add (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (effect)));

  effect1 = GES_TRACK_ELEMENT (ges_effect_new ("agingtv"));
  fail_unless (ges_container_add (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (effect1)));

  effect2 = GES_TRACK_ELEMENT (ges_effect_new ("agingtv"));
  fail_unless (ges_container_add (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (effect2)));

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
    ges_container_add (group, GES_TIMELINE_ELEMENT (clip));
    _test_children_time_setting_on_clip (clip, child);
    /* group is removed from the timeline and destroyed when empty */
    ges_container_remove (group, GES_TIMELINE_ELEMENT (clip));
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
  GES_CLIP (ges_test_clip_new ()), TRUE};
  clips[1] = (struct CanAddEffectData) {
  GES_CLIP (ges_uri_clip_new (uri)), TRUE};
  clips[2] = (struct CanAddEffectData) {
  GES_CLIP (ges_title_clip_new ()), TRUE};
  clips[3] = (struct CanAddEffectData) {
  GES_CLIP (ges_effect_clip_new ("agingtv", "audioecho")), TRUE};
  clips[4] = (struct CanAddEffectData) {
  GES_CLIP (ges_transition_clip_new
        (GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE)), FALSE};
  clips[5] = (struct CanAddEffectData) {
  GES_CLIP (ges_text_overlay_clip_new ()), FALSE};

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

  fail_unless (ges_timeline_element_set_start (clip, 5));
  fail_unless (ges_timeline_element_set_duration (clip, 20));
  fail_unless (ges_timeline_element_set_inpoint (clip, 30));

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
  fail_unless (ges_timeline_element_set_start (effect, 104));
  fail_unless (ges_timeline_element_set_duration (effect, 53));
  fail_unless (ges_timeline_element_set_inpoint (effect, 67));

  /* adding the effect will change its start and duration, but not its
   * in-point */
  fail_unless (ges_container_add (GES_CONTAINER (clip), effect));

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
  fail_if (ges_timeline_element_set_inpoint (child0, 40));

  CHECK_OBJECT_PROPS (clip, 5, 30, 20);
  CHECK_OBJECT_PROPS (child0, 5, 0, 20);
  CHECK_OBJECT_PROPS (child1, 5, 30, 20);
  CHECK_OBJECT_PROPS (effect, 5, 67, 20);

  /* when we set the in-point on a core-child with an internal source we
   * also set the clip and siblings with the same features */
  fail_unless (ges_timeline_element_set_inpoint (child1, 50));

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

  fail_unless (ges_timeline_element_set_inpoint (child0, 40));

  CHECK_OBJECT_PROPS (clip, 5, 40, 20);
  CHECK_OBJECT_PROPS (child0, 5, 40, 20);
  CHECK_OBJECT_PROPS (child1, 5, 40, 20);
  CHECK_OBJECT_PROPS (effect, 5, 67, 20);

  /* setting in-point on effect shouldn't change any other siblings */
  fail_unless (ges_timeline_element_set_inpoint (effect, 77));

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
    fail_unless (ges_timeline_element_set_start (clip, 5));
    fail_unless (ges_timeline_element_set_duration (clip, 20));
    fail_unless (ges_timeline_element_set_inpoint (clip, 30));

    /* can set the max duration the clip to anything whilst it has
     * no core child */
    fail_unless (ges_timeline_element_set_max_duration (clip, 150));

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, 150);

    /* add a non-core element */
    effect = GES_TIMELINE_ELEMENT (ges_effect_new ("agingtv"));
    fail_if (ges_track_element_has_internal_source (GES_TRACK_ELEMENT
            (effect)));
    /* allow us to choose our own max-duration */
    ges_track_element_set_has_internal_source (GES_TRACK_ELEMENT (effect),
        TRUE);
    fail_unless (ges_timeline_element_set_start (effect, 104));
    fail_unless (ges_timeline_element_set_duration (effect, 53));
    fail_unless (ges_timeline_element_set_max_duration (effect, 400));

    /* adding the effect will change its start and duration, but not its
     * max-duration (or in-point) */
    fail_unless (ges_container_add (GES_CONTAINER (clip), effect));

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, 150);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    /* only non-core, so can still set the max-duration */
    fail_unless (ges_timeline_element_set_max_duration (clip, 200));

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, 200);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    /* removing should not change the max-duration we set on the clip */
    gst_object_ref (effect);
    fail_unless (ges_container_remove (GES_CONTAINER (clip), effect));

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, 200);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    fail_unless (ges_container_add (GES_CONTAINER (clip), effect));
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
    fail_unless (ges_timeline_element_set_max_duration (child0, new_max - 1));

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, new_max - 1);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, new_max - 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, max_duration);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    fail_unless (ges_timeline_element_set_max_duration (child1, new_max - 2));

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, new_max - 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    fail_unless (ges_timeline_element_set_max_duration (child0, new_max + 1));

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, new_max + 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    /* can not set in-point above max_duration, nor max_duration below
     * in-point */

    fail_if (ges_timeline_element_set_max_duration (child0, 29));

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, new_max + 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    fail_if (ges_timeline_element_set_max_duration (child1, 29));

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, new_max + 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    fail_if (ges_timeline_element_set_max_duration (clip, 29));

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, new_max + 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    /* can't set the inpoint to (new_max), even though it is lower than
     * our own max-duration (new_max + 1) because it is too high for our
     * sibling child1 */
    fail_if (ges_timeline_element_set_inpoint (child0, new_max));

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, new_max + 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    fail_if (ges_timeline_element_set_inpoint (child1, new_max));

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, new_max + 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    fail_if (ges_timeline_element_set_inpoint (clip, new_max));

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, new_max + 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    /* setting below new_max is ok */
    fail_unless (ges_timeline_element_set_inpoint (child0, 15));

    CHECK_OBJECT_PROPS_MAX (clip, 5, 15, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 15, 20, new_max + 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 15, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    fail_unless (ges_timeline_element_set_inpoint (child1, 25));

    CHECK_OBJECT_PROPS_MAX (clip, 5, 25, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 25, 20, new_max + 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 25, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    fail_unless (ges_timeline_element_set_inpoint (clip, 30));

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, new_max + 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    /* non-core has no effect */
    fail_unless (ges_timeline_element_set_max_duration (effect, new_max + 500));

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, new_max + 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, new_max + 500);

    /* can set the in-point of non-core to be higher than the max_duration
     * of the clip */
    fail_unless (ges_timeline_element_set_inpoint (effect, new_max + 2));

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, new_max + 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (effect, 5, new_max + 2, 20, new_max + 500);

    /* but not higher than our own */
    fail_if (ges_timeline_element_set_inpoint (effect, new_max + 501));

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, new_max + 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (effect, 5, new_max + 2, 20, new_max + 500);

    fail_if (ges_timeline_element_set_max_duration (effect, new_max + 1));

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, new_max + 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (effect, 5, new_max + 2, 20, new_max + 500);

    fail_unless (ges_timeline_element_set_inpoint (effect, 0));

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, new_max + 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, new_max + 500);

    fail_unless (ges_timeline_element_set_max_duration (effect, 400));

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, new_max + 1);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, new_max - 2);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    /* setting on the clip will set all the core children to the same
     * value */
    fail_unless (ges_timeline_element_set_max_duration (clip, 180));

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
    fail_if (ges_timeline_element_set_max_duration (child0, 40));

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
    fail_if (ges_timeline_element_set_max_duration (clip, 150));

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
    fail_unless (ges_timeline_element_set_max_duration (child0, 140));

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

    fail_unless (ges_timeline_element_set_max_duration (child1, 130));

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, 130);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, 140);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, 130);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    /* removing a child may change the max_duration of the clip */
    gst_object_ref (child0);
    gst_object_ref (child1);
    gst_object_ref (effect);

    /* removing non-core does nothing */
    fail_unless (ges_container_remove (GES_CONTAINER (clip), effect));

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, 130);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, 140);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, 130);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    /* new minimum max-duration for the clip when we remove child1 */
    fail_unless (ges_container_remove (GES_CONTAINER (clip), child1));

    CHECK_OBJECT_PROPS_MAX (clip, 5, 30, 20, 140);
    CHECK_OBJECT_PROPS_MAX (child0, 5, 30, 20, 140);
    CHECK_OBJECT_PROPS_MAX (child1, 5, 30, 20, 130);
    CHECK_OBJECT_PROPS_MAX (effect, 5, 0, 20, 400);

    /* with no core-children, the max-duration of the clip is set to
     * GST_CLOCK_TIME_NONE */
    fail_unless (ges_container_remove (GES_CONTAINER (clip), child0));

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
  ges_timeline_element_set_duration (GES_TIMELINE_ELEMENT (clip), 50);

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
  ges_timeline_element_set_duration (GES_TIMELINE_ELEMENT (clip), 50);

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
      prop_name, element->name); \
  g_object_get (G_OBJECT (binding), "control-source", &source, \
      "object", &found_object, NULL); \
  \
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
    fail_unless (val1->timestamp == val2->timestamp && \
        val1->value == val2->value, "The %ith timed value (%lu: %g) " \
        "does not match the found timed value (%lu: %g)", \
        i, val1->timestamp, val1->value, val2->timestamp, val2->value); \
  } \
  fail_unless (tmp1 == NULL, "Found too few timed values"); \
  fail_unless (tmp2 == NULL, "Found too many timed values"); \
  \
  g_list_free (found_timed_vals); \
  g_object_get (G_OBJECT (source), "mode", &found_mode, NULL); \
  fail_unless (found_mode == GST_INTERPOLATION_MODE_CUBIC); \
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
  clip = GES_TIMELINE_ELEMENT (ges_test_clip_new ());
  ges_timeline_element_set_duration (GES_TIMELINE_ELEMENT (clip), 50);

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



static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges-clip");
  TCase *tc_chain = tcase_create ("clip");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_object_properties);
  tcase_add_test (tc_chain, test_split_object);
  tcase_add_test (tc_chain, test_split_direct_bindings);
  tcase_add_test (tc_chain, test_split_direct_absolute_bindings);
  tcase_add_test (tc_chain, test_clip_group_ungroup);
  tcase_add_test (tc_chain, test_adding_children_to_track);
  tcase_add_test (tc_chain, test_clip_refcount_remove_child);
  tcase_add_test (tc_chain, test_clip_find_track_element);
  tcase_add_test (tc_chain, test_effects_priorities);
  tcase_add_test (tc_chain, test_children_time_setters);
  tcase_add_test (tc_chain, test_can_add_effect);
  tcase_add_test (tc_chain, test_children_inpoint);
  tcase_add_test (tc_chain, test_children_max_duration);
  tcase_add_test (tc_chain, test_children_properties_contain);
  tcase_add_test (tc_chain, test_children_properties_change);
  tcase_add_test (tc_chain, test_copy_paste_children_properties);

  return s;
}

GST_CHECK_MAIN (ges);
