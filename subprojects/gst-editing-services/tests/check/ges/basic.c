/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <bilboed@bilboed.com>
 *               2012 Collabora Ltd.
 *                 Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
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


GST_START_TEST (test_ges_scenario)
{
  GESTimeline *timeline;
  GESLayer *layer, *tmp_layer;
  GESTrack *track;
  GESTestClip *source;
  GESTrackElement *trackelement;
  GList *trackelements, *layers, *tracks;

  ges_init ();
  /* This is the simplest scenario ever */

  /* Timeline and 1 Layer */
  GST_DEBUG ("Create a timeline");
  timeline = ges_timeline_new ();
  fail_unless (timeline != NULL);

  GST_DEBUG ("Create a layer");
  layer = ges_layer_new ();
  fail_unless (layer != NULL);

  GST_DEBUG ("Add the layer to the timeline");
  fail_unless (ges_timeline_add_layer (timeline, layer));
  /* The timeline steals our reference to the layer */
  ASSERT_OBJECT_REFCOUNT (layer, "layer", 1);
  fail_unless (layer->timeline == timeline);

  layers = ges_timeline_get_layers (timeline);
  fail_unless (g_list_find (layers, layer) != NULL);
  g_list_free_full (layers, gst_object_unref);

  /* Give the Timeline a Track */
  GST_DEBUG ("Create a Track");
  track = GES_TRACK (ges_video_track_new ());
  fail_unless (track != NULL);

  GST_DEBUG ("Add the track to the timeline");
  fail_unless (ges_timeline_add_track (timeline, track));

  /* The timeline steals the reference to the track */
  ASSERT_OBJECT_REFCOUNT (track, "track", 1);
  fail_unless (ges_track_get_timeline (track) == timeline);
  fail_unless ((gpointer) GST_ELEMENT_PARENT (track) == (gpointer) timeline);

  /* Create a source and add it to the Layer */
  GST_DEBUG ("Creating a source");
  source = ges_test_clip_new ();
  fail_unless (source != NULL);

  /* The source will be floating before added to the layer... */
  fail_unless (g_object_is_floating (source));
  GST_DEBUG ("Adding the source to the timeline layer");
  fail_unless (ges_layer_add_clip (layer, GES_CLIP (source)));
  fail_if (g_object_is_floating (source));
  tmp_layer = ges_clip_get_layer (GES_CLIP (source));
  fail_unless (tmp_layer == layer);
  /* The timeline stole our reference */
  ASSERT_OBJECT_REFCOUNT (source, "source + 1 timeline", 2);
  gst_object_unref (tmp_layer);
  ASSERT_OBJECT_REFCOUNT (layer, "layer", 1);

  /* Make sure the associated TrackElement is in the Track */
  trackelements = GES_CONTAINER_CHILDREN (source);
  fail_unless (trackelements != NULL);
  trackelement = GES_TRACK_ELEMENT (trackelements->data);
  /* There are 3 references:
   * 1 by the clip
   * 1 by the track
   * 1 by the timeline */
  ASSERT_OBJECT_REFCOUNT (trackelement, "trackelement", 3);
  /* There are 3 references:
   * 1 by the clip
   * 3 by the timeline
   * 1 by the track */
  ASSERT_OBJECT_REFCOUNT (trackelement, "trackelement", 3);
  fail_unless (ges_track_element_get_track (trackelement) == track);

  GST_DEBUG ("Remove the Clip from the layer");

  /* Now remove the clip */
  gst_object_ref (source);
  ASSERT_OBJECT_REFCOUNT (layer, "layer", 1);
  fail_unless (ges_layer_remove_clip (layer, GES_CLIP (source)));
  /* track elements emptied from the track, but stay in clip */
  fail_unless (GES_TIMELINE_ELEMENT_PARENT (trackelement) ==
      GES_TIMELINE_ELEMENT (source));
  fail_unless (ges_track_element_get_track (trackelement) == NULL);
  ASSERT_OBJECT_REFCOUNT (source, "source", 1);
  ASSERT_OBJECT_REFCOUNT (layer, "layer", 1);
  tmp_layer = ges_clip_get_layer (GES_CLIP (source));
  fail_unless (tmp_layer == NULL);
  gst_object_unref (source);

  GST_DEBUG ("Removing track from the timeline");
  /* Remove the track from the timeline */
  gst_object_ref (track);
  fail_unless (ges_timeline_remove_track (timeline, track));
  assert_num_in_track (track, 0);

  tracks = ges_timeline_get_tracks (timeline);
  fail_unless (tracks == NULL);
  ASSERT_OBJECT_REFCOUNT (track, "track", 1);
  gst_object_unref (track);

  GST_DEBUG ("Removing layer from the timeline");
  /* Remove the layer from the timeline */
  gst_object_ref (layer);
  fail_unless (ges_timeline_remove_layer (timeline, layer));
  fail_unless (layer->timeline == NULL);

  layers = ges_timeline_get_layers (timeline);
  fail_unless (layers == NULL);
  ASSERT_OBJECT_REFCOUNT (layer, "layer", 1);
  gst_object_unref (layer);

  /* Finally clean up our object */
  ASSERT_OBJECT_REFCOUNT (timeline, "timeline", 1);
  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

/* very similar to the above, except we add the clip to the layer
 * and then add it to the timeline.
 */

#define _CREATE_SOURCE(layer, clip, start, duration) \
{ \
  GESAsset *asset = ges_asset_request (GES_TYPE_TEST_CLIP, NULL, NULL); \
  GST_DEBUG ("Creating a source"); \
  fail_unless (clip = ges_layer_add_asset (layer, asset, start, 0, \
        duration, GES_TRACK_TYPE_UNKNOWN)); \
  assert_layer(clip, layer); \
  ASSERT_OBJECT_REFCOUNT (layer, "layer", 1); \
  gst_object_unref (asset); \
}

GST_START_TEST (test_ges_timeline_add_layer)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESTrack *track;
  GESClip *s1, *s2, *s3;
  GList *trackelements, *layers;
  GESTrackElement *trackelement;

  ges_init ();

  /* Timeline and 1 Layer */
  GST_DEBUG ("Create a timeline");
  timeline = ges_timeline_new ();
  fail_unless (timeline != NULL);

  GST_DEBUG ("Create a layer");
  layer = ges_layer_new ();
  fail_unless (layer != NULL);
  /* Give the Timeline a Track */
  GST_DEBUG ("Create a Track");
  track = GES_TRACK (ges_video_track_new ());
  fail_unless (track != NULL);

  GST_DEBUG ("Add the track to the timeline");
  fail_unless (ges_timeline_add_track (timeline, track));
  /* The timeline steals the reference to the track */
  ASSERT_OBJECT_REFCOUNT (track, "track", 1);
  fail_unless (ges_track_get_timeline (track) == timeline);
  fail_unless ((gpointer) GST_ELEMENT_PARENT (track) == (gpointer) timeline);

  GST_DEBUG ("Add the layer to the timeline");
  fail_unless (ges_timeline_add_layer (timeline, layer));
  /* The timeline steals our reference to the layer */
  ASSERT_OBJECT_REFCOUNT (layer, "layer", 1);
  fail_unless (layer->timeline == timeline);
  layers = ges_timeline_get_layers (timeline);
  fail_unless (g_list_find (layers, layer) != NULL);
  g_list_free_full (layers, gst_object_unref);

  _CREATE_SOURCE (layer, s1, 0, 10);
  ASSERT_OBJECT_REFCOUNT (layer, "1 for the timeline", 1);
  _CREATE_SOURCE (layer, s2, 20, 10);
  ASSERT_OBJECT_REFCOUNT (layer, "1 for the timeline", 1);
  _CREATE_SOURCE (layer, s3, 40, 10);
  ASSERT_OBJECT_REFCOUNT (layer, "1 for the timeline", 1);

  /* Make sure the associated TrackElements are in the Track */
  trackelements = GES_CONTAINER_CHILDREN (s1);
  fail_unless (trackelements != NULL);
  trackelement = GES_TRACK_ELEMENT (trackelements->data);
  /* There are 3 references:
   * 1 by the clip
   * 1 by the trackelement
   * 1 by the timeline */
  ASSERT_OBJECT_REFCOUNT (trackelement, "trackelement", 3);
  /* There are 3 references:
   * 1 by the clip
   * 1 by the timeline
   * 1 by the trackelement */
  ASSERT_OBJECT_REFCOUNT (trackelement, "trackelement", 3);

  trackelements = GES_CONTAINER_CHILDREN (s2);
  trackelement = GES_TRACK_ELEMENT (trackelements->data);
  fail_unless (trackelements != NULL);

  /* There are 3 references:
   * 1 by the clip
   * 1 by the timeline
   * 1 by the trackelement */
  ASSERT_OBJECT_REFCOUNT (GES_TRACK_ELEMENT (trackelement), "trackelement", 3);

  trackelements = GES_CONTAINER_CHILDREN (s3);
  trackelement = GES_TRACK_ELEMENT (trackelements->data);
  fail_unless (trackelements != NULL);

  /* There are 3 references:
   * 1 by the clip
   * 1 by the timeline
   * 2 by the trackelement */
  ASSERT_OBJECT_REFCOUNT (trackelement, "trackelement", 3);

  /* theoretically this is all we need to do to ensure cleanup */
  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

/* this time we add the layer before we add the track. */

#define _assert_child_in_track(clip, child_type, track) \
{ \
  GESTrackElement *el; \
  GList *tmp = ges_clip_find_track_elements (clip, NULL, \
      GES_TRACK_TYPE_UNKNOWN, child_type); \
  fail_unless (g_list_length (tmp), 1); \
  el = tmp->data; \
  g_list_free_full (tmp, gst_object_unref); \
  fail_unless (ges_track_element_get_track (el) == track); \
  if (track) \
    ASSERT_OBJECT_REFCOUNT (el, "1 clip + 1 track + 1 timeline", 3); \
  else \
    ASSERT_OBJECT_REFCOUNT (el, "1 clip", 1); \
}

#define _assert_no_child_in_track(clip, track) \
  fail_if (ges_clip_find_track_elements (clip, track, \
        GES_TRACK_TYPE_UNKNOWN, G_TYPE_NONE));

#define _assert_add_track(timeline, track) \
{ \
  GList *tmp; \
  GST_DEBUG ("Adding " #track " to the timeline"); \
  fail_unless (ges_timeline_add_track (timeline, track)); \
  ASSERT_OBJECT_REFCOUNT (track, #track, 1); \
  fail_unless (ges_track_get_timeline (track) == timeline); \
  fail_unless ((gpointer) GST_ELEMENT_PARENT (track) \
      == (gpointer) timeline); \
  tmp = ges_timeline_get_tracks (timeline); \
  fail_unless (g_list_find (tmp, track), #track " not found in tracks"); \
  g_list_free_full (tmp, gst_object_unref); \
}

#define _remove_sources(clip, expect_num) \
{ \
  GList *tmp, *els; \
  els = ges_clip_find_track_elements (clip, NULL, GES_TRACK_TYPE_UNKNOWN, \
      GES_TYPE_SOURCE); \
  assert_equals_int (g_list_length (els), expect_num); \
  for (tmp = els; tmp; tmp = tmp->next) \
    fail_unless (ges_container_remove (GES_CONTAINER (clip), tmp->data)); \
  g_list_free_full (els, gst_object_unref); \
}

#define _remove_from_track(clip, track, expect_num) \
{ \
  GList *tmp, *els; \
  els = ges_clip_find_track_elements (clip, track, GES_TRACK_TYPE_UNKNOWN, \
      G_TYPE_NONE); \
  assert_equals_int (g_list_length (els), expect_num); \
  for (tmp = els; tmp; tmp = tmp->next) \
    fail_unless (ges_track_remove_element (track, tmp->data)); \
  g_list_free_full (els, gst_object_unref); \
}

GST_START_TEST (test_ges_timeline_add_layer_first)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESTrack *track, *track1, *track2, *track3;
  GESClip *s1, *s2, *s3;
  GList *layers;

  ges_init ();

  /* Timeline and 1 Layer */
  GST_DEBUG ("Create a timeline");
  timeline = ges_timeline_new ();
  fail_unless (timeline != NULL);

  GST_DEBUG ("Create a layer");
  layer = ges_layer_new ();
  fail_unless (layer != NULL);
  /* Give the Timeline a Track */
  GST_DEBUG ("Create a Track");
  track = GES_TRACK (ges_video_track_new ());
  fail_unless (track != NULL);

  _CREATE_SOURCE (layer, s1, 0, 10);
  _CREATE_SOURCE (layer, s2, 20, 10);
  _CREATE_SOURCE (layer, s3, 40, 10);

  fail_unless (ges_container_add (GES_CONTAINER (s1),
          GES_TIMELINE_ELEMENT (ges_effect_new ("agingtv"))));
  assert_num_children (s1, 1);
  assert_num_children (s2, 0);
  assert_num_children (s3, 0);

  GST_DEBUG ("Add the layer to the timeline");
  fail_unless (ges_timeline_add_layer (timeline, layer));
  /* The timeline steals our reference to the layer */
  ASSERT_OBJECT_REFCOUNT (layer, "layer", 1);
  fail_unless (layer->timeline == timeline);
  layers = ges_timeline_get_layers (timeline);
  fail_unless (g_list_find (layers, layer) != NULL);
  g_list_free_full (layers, gst_object_unref);

  /* core children not created yet since no tracks */
  assert_num_children (s1, 1);
  assert_num_children (s2, 0);
  assert_num_children (s3, 0);

  _assert_add_track (timeline, track);
  /* 3 sources, 1 effect */
  assert_num_in_track (track, 4);

  /* Make sure the associated TrackElements are in the Track */
  assert_num_children (s1, 2);
  _assert_child_in_track (s1, GES_TYPE_EFFECT, track);
  _assert_child_in_track (s1, GES_TYPE_VIDEO_TEST_SOURCE, track);

  assert_num_children (s2, 1);
  _assert_child_in_track (s2, GES_TYPE_VIDEO_TEST_SOURCE, track);

  assert_num_children (s3, 1);
  _assert_child_in_track (s3, GES_TYPE_VIDEO_TEST_SOURCE, track);

  /* adding an audio track should create new audio sources */

  track1 = GES_TRACK (ges_audio_track_new ());
  _assert_add_track (timeline, track1);
  /* other track stays the same */
  assert_num_in_track (track, 4);
  /* 3 sources */
  assert_num_in_track (track1, 3);

  /* one new core child */
  assert_num_children (s1, 3);
  _assert_child_in_track (s1, GES_TYPE_EFFECT, track);
  _assert_child_in_track (s1, GES_TYPE_VIDEO_TEST_SOURCE, track);
  _assert_child_in_track (s1, GES_TYPE_AUDIO_TEST_SOURCE, track1);

  assert_num_children (s2, 2);
  _assert_child_in_track (s2, GES_TYPE_VIDEO_TEST_SOURCE, track);
  _assert_child_in_track (s2, GES_TYPE_AUDIO_TEST_SOURCE, track1);

  assert_num_children (s3, 2);
  _assert_child_in_track (s3, GES_TYPE_VIDEO_TEST_SOURCE, track);
  _assert_child_in_track (s3, GES_TYPE_AUDIO_TEST_SOURCE, track1);

  /* adding another track should not prompt the change anything
   * unrelated to the new track */

  /* remove the core children from s1 */
  _remove_sources (s1, 2);

  /* only have effect left, and not in any track */
  assert_num_children (s1, 1);
  /* effect is emptied from its track, since the corresponding core child
   * was removed */
  _assert_child_in_track (s1, GES_TYPE_EFFECT, NULL);

  assert_num_in_track (track, 2);
  assert_num_in_track (track1, 2);

  track2 = GES_TRACK (ges_video_track_new ());
  _assert_add_track (timeline, track2);
  /* other tracks stay the same */
  assert_num_in_track (track, 2);
  assert_num_in_track (track1, 2);
  /* 1 sources + 1 effect */
  assert_num_in_track (track2, 2);

  /* s1 only has a child created for the new track, not the other two */
  assert_num_children (s1, 2);
  _assert_child_in_track (s1, GES_TYPE_EFFECT, track2);
  _assert_child_in_track (s1, GES_TYPE_VIDEO_TEST_SOURCE, track2);
  _assert_no_child_in_track (s1, track);
  _assert_no_child_in_track (s1, track1);

  /* other clips stay the same since their children were already created
   * with set tracks */
  assert_num_children (s2, 2);
  _assert_child_in_track (s2, GES_TYPE_VIDEO_TEST_SOURCE, track);
  _assert_child_in_track (s2, GES_TYPE_AUDIO_TEST_SOURCE, track1);
  _assert_no_child_in_track (s2, track2);

  assert_num_children (s3, 2);
  _assert_child_in_track (s3, GES_TYPE_VIDEO_TEST_SOURCE, track);
  _assert_child_in_track (s3, GES_TYPE_AUDIO_TEST_SOURCE, track1);
  _assert_no_child_in_track (s3, track2);

  /* same with an audio track */

  /* remove the core child from s1 */
  _remove_sources (s1, 1);

  assert_num_children (s1, 1);
  _assert_child_in_track (s1, GES_TYPE_EFFECT, NULL);

  assert_num_in_track (track, 2);
  assert_num_in_track (track1, 2);
  assert_num_in_track (track2, 0);

  /* unset the core tracks for s2 */
  _remove_from_track (s2, track, 1);
  _remove_from_track (s2, track1, 1);
  /* but keep children in clip */
  assert_num_children (s2, 2);

  assert_num_in_track (track, 1);
  assert_num_in_track (track1, 1);
  assert_num_in_track (track2, 0);

  track3 = GES_TRACK (ges_audio_track_new ());
  _assert_add_track (timeline, track3);
  /* other tracks stay the same */
  assert_num_in_track (track, 1);
  assert_num_in_track (track1, 1);
  assert_num_in_track (track2, 0);
  /* 2 sources */
  assert_num_in_track (track3, 2);

  /* s1 creates core for the new track, but effect does not have a track
   * set since the new track is not a video track */
  assert_num_children (s1, 2);
  _assert_child_in_track (s1, GES_TYPE_AUDIO_TEST_SOURCE, track3);
  _assert_child_in_track (s1, GES_TYPE_EFFECT, NULL);
  _assert_no_child_in_track (s1, track);
  _assert_no_child_in_track (s1, track1);
  _assert_no_child_in_track (s1, track2);

  /* s2 audio core is in the new track, but video remains trackless */
  assert_num_children (s2, 2);
  _assert_child_in_track (s2, GES_TYPE_AUDIO_TEST_SOURCE, track3);
  _assert_child_in_track (s2, GES_TYPE_VIDEO_TEST_SOURCE, NULL);
  _assert_no_child_in_track (s1, track);
  _assert_no_child_in_track (s1, track1);
  _assert_no_child_in_track (s1, track2);

  /* s3 remains the same since core already had tracks */
  assert_num_children (s3, 2);
  _assert_child_in_track (s3, GES_TYPE_VIDEO_TEST_SOURCE, track);
  _assert_child_in_track (s3, GES_TYPE_AUDIO_TEST_SOURCE, track1);
  _assert_no_child_in_track (s3, track2);
  _assert_no_child_in_track (s3, track3);

  /* theoretically this is all we need to do to ensure cleanup */
  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_ges_timeline_remove_track)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESTrack *track;
  GESClip *s1, *s2, *s3;
  GESTrackElement *t1, *t2, *t3;
  GList *trackelements, *tmp, *layers;

  ges_init ();

  /* Timeline and 1 Layer */
  GST_DEBUG ("Create a timeline");
  timeline = ges_timeline_new ();
  fail_unless (timeline != NULL);

  GST_DEBUG ("Create a layer");
  layer = ges_layer_new ();
  fail_unless (layer != NULL);
  /* Give the Timeline a Track */
  GST_DEBUG ("Create a Track");
  track = GES_TRACK (ges_video_track_new ());
  fail_unless (track != NULL);

  _CREATE_SOURCE (layer, s1, 0, 10);
  ASSERT_OBJECT_REFCOUNT (layer, "1 for the timeline", 1);
  _CREATE_SOURCE (layer, s2, 20, 10);
  ASSERT_OBJECT_REFCOUNT (layer, "1 for the timeline", 1);
  _CREATE_SOURCE (layer, s3, 40, 10);
  ASSERT_OBJECT_REFCOUNT (layer, "1 for the timeline", 1);

  GST_DEBUG ("Add the layer to the timeline");
  fail_unless (ges_timeline_add_layer (timeline, layer));
  /* The timeline steals our reference to the layer */
  ASSERT_OBJECT_REFCOUNT (layer, "layer", 1);
  fail_unless (layer->timeline == timeline);

  layers = ges_timeline_get_layers (timeline);
  fail_unless (g_list_find (layers, layer) != NULL);
  g_list_free_full (layers, gst_object_unref);
  ASSERT_OBJECT_REFCOUNT (layer, "1 for the timeline", 1);

  GST_DEBUG ("Add the track to the timeline");
  fail_unless (ges_timeline_add_track (timeline, track));
  ASSERT_OBJECT_REFCOUNT (track, "track", 1);
  fail_unless (ges_track_get_timeline (track) == timeline);
  fail_unless ((gpointer) GST_ELEMENT_PARENT (track) == (gpointer) timeline);

  /* Make sure the associated TrackElements are in the Track */
  trackelements = GES_CONTAINER_CHILDREN (s1);
  fail_unless (trackelements != NULL);
  t1 = GES_TRACK_ELEMENT ((trackelements)->data);
  for (tmp = trackelements; tmp; tmp = tmp->next) {
    /* There are 3 references held:
     * 1 by the clip
     * 1 by the track
     * 1 by the timeline */
    ASSERT_OBJECT_REFCOUNT (GES_TRACK_ELEMENT (tmp->data), "trackelement", 3);
  }
  /* There are 3 references held:
   * 1 by the container
   * 1 by the track
   * 1 by the timeline */
  ASSERT_OBJECT_REFCOUNT (t1, "trackelement", 3);

  trackelements = GES_CONTAINER_CHILDREN (s2);
  fail_unless (trackelements != NULL);
  t2 = GES_TRACK_ELEMENT (trackelements->data);
  for (tmp = trackelements; tmp; tmp = tmp->next) {
    /* There are 3 references held:
     * 1 by the clip
     * 1 by the track
     * 1 by the timeline */
    ASSERT_OBJECT_REFCOUNT (GES_TRACK_ELEMENT (tmp->data), "trackelement", 3);
  }
  /* There are 3 references held:
   * 1 by the container
   * 1 by the track
   * 1 by the timeline */
  ASSERT_OBJECT_REFCOUNT (t2, "t2", 3);

  trackelements = GES_CONTAINER_CHILDREN (s3);
  fail_unless (trackelements != NULL);
  t3 = GES_TRACK_ELEMENT (trackelements->data);
  for (tmp = trackelements; tmp; tmp = tmp->next) {
    /* There are 3 references held:
     * 1 by the clip
     * 1 by the track
     * 1 by the timeline */
    ASSERT_OBJECT_REFCOUNT (GES_TRACK_ELEMENT (tmp->data), "trackelement", 3);
  }
  /* There are 3 references held:
   * 1 by the container
   * 1 by the track
   * 1 by the timeline */
  ASSERT_OBJECT_REFCOUNT (t3, "t3", 3);

  fail_unless (ges_track_element_get_track (t1) == track);
  fail_unless (ges_track_element_get_track (t2) == track);
  fail_unless (ges_track_element_get_track (t3) == track);

  /* remove the track and check that the track elements have been released */
  gst_object_ref (track);
  fail_unless (ges_timeline_remove_track (timeline, track));
  assert_num_in_track (track, 0);
  gst_object_unref (track);
  fail_unless (ges_track_element_get_track (t1) == NULL);
  fail_unless (ges_track_element_get_track (t2) == NULL);
  fail_unless (ges_track_element_get_track (t3) == NULL);

  ASSERT_OBJECT_REFCOUNT (t1, "trackelement", 1);
  ASSERT_OBJECT_REFCOUNT (t2, "trackelement", 1);
  ASSERT_OBJECT_REFCOUNT (t3, "trackelement", 1);
  ASSERT_OBJECT_REFCOUNT (layer, "1 for the timeline", 1);
  ASSERT_OBJECT_REFCOUNT (timeline, "1 for the us", 1);
  tmp = ges_layer_get_clips (layer);
  assert_equals_int (g_list_length (tmp), 3);
  g_list_free_full (tmp, (GDestroyNotify) gst_object_unref);

  gst_check_objects_destroyed_on_unref (G_OBJECT (timeline),
      G_OBJECT (layer), t1, t2, t3, NULL);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_ges_timeline_remove_layer)
{
  GESTimeline *timeline;
  GESLayer *layer0, *layer1, *layer2, *layer3;
  GESTrack *track;
  GESClip *s1, *s2, *s3, *s4, *s5;
  GList *tmp, *clips, *clip, *layers;

  ges_init ();

  timeline = ges_timeline_new ();

  layer0 = ges_timeline_append_layer (timeline);
  layer1 = ges_timeline_append_layer (timeline);
  layer2 = ges_timeline_append_layer (timeline);

  assert_equals_int (ges_layer_get_priority (layer0), 0);
  assert_equals_int (ges_layer_get_priority (layer1), 1);
  assert_equals_int (ges_layer_get_priority (layer2), 2);

  track = GES_TRACK (ges_video_track_new ());
  fail_unless (ges_timeline_add_track (timeline, track));

  _CREATE_SOURCE (layer0, s1, 0, 10);
  _CREATE_SOURCE (layer1, s2, 0, 10);
  _CREATE_SOURCE (layer1, s3, 10, 20);
  _CREATE_SOURCE (layer2, s4, 0, 10);
  _CREATE_SOURCE (layer2, s5, 10, 20);

  assert_num_in_track (track, 5);

  gst_object_ref (layer1);
  fail_unless (ges_timeline_remove_layer (timeline, layer1));
  /* check removed, and rest of the layers stay */
  layers = ges_timeline_get_layers (timeline);
  fail_if (g_list_find (layers, layer1));
  fail_unless (g_list_find (layers, layer0));
  fail_unless (g_list_find (layers, layer2));
  g_list_free_full (layers, gst_object_unref);
  /* keeps its layer priority */
  assert_equals_int (ges_layer_get_priority (layer1), 1);

  /* Rest also keep their layer priority */
  /* NOTE: it may be better to resync the layer priorities to plug the
   * gap, but this way we leave the gap open to add the layer back in */
  assert_equals_int (ges_layer_get_priority (layer0), 0);
  assert_equals_int (ges_layer_get_priority (layer2), 2);
  /* clip children removed from track */
  assert_num_in_track (track, 3);

  fail_unless (ges_layer_get_timeline (layer1) == NULL);
  clips = ges_layer_get_clips (layer1);
  for (clip = clips; clip; clip = clip->next) {
    fail_unless (GES_TIMELINE_ELEMENT_TIMELINE (clip->data) == NULL);
    for (tmp = GES_CONTAINER_CHILDREN (clip->data); tmp; tmp = tmp->next) {
      GESTrackElement *el = GES_TRACK_ELEMENT (tmp->data);
      fail_unless (GES_TIMELINE_ELEMENT_TIMELINE (el) == NULL);
      fail_unless (ges_track_element_get_track (el) == NULL);
    }
  }
  g_list_free_full (clips, gst_object_unref);

  /* layer2 children have same layer priority */
  clips = ges_layer_get_clips (layer2);
  for (clip = clips; clip; clip = clip->next) {
    fail_unless (GES_TIMELINE_ELEMENT_TIMELINE (clip->data) == timeline);
    assert_equals_int (GES_TIMELINE_ELEMENT_LAYER_PRIORITY (clip->data), 2);
    for (tmp = GES_CONTAINER_CHILDREN (clip->data); tmp; tmp = tmp->next) {
      GESTrackElement *el = GES_TRACK_ELEMENT (tmp->data);
      fail_unless (GES_TIMELINE_ELEMENT_TIMELINE (el) == timeline);
      fail_unless (ges_track_element_get_track (el) == track);
      assert_equals_int (GES_TIMELINE_ELEMENT_LAYER_PRIORITY (el), 2);
    }
  }
  g_list_free_full (clips, gst_object_unref);

  /* layer0 stays the same */
  clips = ges_layer_get_clips (layer0);
  for (clip = clips; clip; clip = clip->next) {
    fail_unless (GES_TIMELINE_ELEMENT_TIMELINE (clip->data) == timeline);
    assert_equals_int (GES_TIMELINE_ELEMENT_LAYER_PRIORITY (clip->data), 0);
    for (tmp = GES_CONTAINER_CHILDREN (clip->data); tmp; tmp = tmp->next) {
      GESTrackElement *el = GES_TRACK_ELEMENT (tmp->data);
      fail_unless (GES_TIMELINE_ELEMENT_TIMELINE (el) == timeline);
      fail_unless (ges_track_element_get_track (el) == track);
      assert_equals_int (GES_TIMELINE_ELEMENT_LAYER_PRIORITY (el), 0);
    }
  }
  g_list_free_full (clips, gst_object_unref);

  /* can add a new layer with the correct priority */
  layer3 = ges_timeline_append_layer (timeline);

  assert_equals_int (ges_layer_get_priority (layer0), 0);
  assert_equals_int (ges_layer_get_priority (layer2), 2);
  assert_equals_int (ges_layer_get_priority (layer3), 3);

  gst_object_unref (layer1);
  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

typedef struct
{
  GESClip *clips[4];
  guint num_calls[4];
  GESTrackElement *effects[3];
  GESTrack *tr1, *tr2;
  guint num_unrecognised;
} SelectTracksData;

static GPtrArray *
select_tracks_cb (GESTimeline * timeline, GESClip * clip,
    GESTrackElement * track_element, SelectTracksData * data)
{
  GPtrArray *ret = g_ptr_array_new ();
  gboolean track1 = FALSE;
  gboolean track2 = FALSE;
  guint i;
  gboolean recognise_clip = FALSE;

  for (i = 0; i < 4; i++) {
    if (clip == data->clips[i]) {
      data->num_calls[i]++;
      recognise_clip = TRUE;
    }
  }

  if (!recognise_clip) {
    GST_DEBUG_OBJECT (timeline, "unrecognised clip %" GES_FORMAT " for "
        "track element %" GES_FORMAT, GES_ARGS (clip),
        GES_ARGS (track_element));
    data->num_unrecognised++;
    return ret;
  }

  if (GES_IS_BASE_EFFECT (track_element)) {
    if (track_element == data->effects[0]) {
      track1 = TRUE;
    } else if (track_element == data->effects[1]) {
      track1 = TRUE;
      track2 = TRUE;
    } else if (track_element == data->effects[2]) {
      track2 = TRUE;
    } else {
      GST_DEBUG_OBJECT (timeline, "unrecognised effect %" GES_FORMAT,
          GES_ARGS (track_element));
      data->num_unrecognised++;
    }
  } else if (GES_IS_SOURCE (track_element)) {
    if (clip == data->clips[0] || clip == data->clips[1])
      track1 = TRUE;
    if (clip == data->clips[1] || clip == data->clips[2])
      track2 = TRUE;
    /* clips[3] has no tracks selected */
  } else {
    GST_DEBUG_OBJECT (timeline, "unrecognised track element %" GES_FORMAT,
        GES_ARGS (track_element));
    data->num_unrecognised++;
  }

  if (track1)
    g_ptr_array_add (ret, gst_object_ref (data->tr1));
  if (track2)
    g_ptr_array_add (ret, gst_object_ref (data->tr2));

  return ret;
}

GST_START_TEST (test_ges_timeline_multiple_tracks)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESTrack *track1, *track2;
  GESClip *s1, *s2, *s3, *s4, *transition;
  GESTrackElement *e1, *e2, *e3, *el, *el2, *e_copy;
  gboolean found_e1 = FALSE, found_e2 = FALSE, found_e3 = FALSE;
  GList *trackelements, *tmp, *layers;
  GstControlSource *ctrl_source;
  SelectTracksData st_data;

  ges_init ();

  /* Timeline and 1 Layer */
  GST_DEBUG ("Create a timeline");
  timeline = ges_timeline_new ();
  fail_unless (timeline != NULL);
  ges_timeline_set_auto_transition (timeline, TRUE);

  GST_DEBUG ("Create a layer");
  layer = ges_layer_new ();
  fail_unless (layer != NULL);
  /* Give the Timeline a Track */
  GST_DEBUG ("Create Track 1");
  track1 = GES_TRACK (ges_video_track_new ());
  fail_unless (track1 != NULL);
  GST_DEBUG ("Create Track 2");
  track2 = GES_TRACK (ges_video_track_new ());
  fail_unless (track2 != NULL);

  GST_DEBUG ("Add the track 1 to the timeline");
  fail_unless (ges_timeline_add_track (timeline, track1));
  ASSERT_OBJECT_REFCOUNT (track1, "track", 1);
  fail_unless (ges_track_get_timeline (track1) == timeline);
  fail_unless ((gpointer) GST_ELEMENT_PARENT (track1) == (gpointer) timeline);

  GST_DEBUG ("Add the track 2 to the timeline");
  fail_unless (ges_timeline_add_track (timeline, track2));
  ASSERT_OBJECT_REFCOUNT (track2, "track", 1);
  fail_unless (ges_track_get_timeline (track2) == timeline);
  fail_unless ((gpointer) GST_ELEMENT_PARENT (track2) == (gpointer) timeline);

  /* adding to the layer before it is part of the timeline does not
   * trigger track selection */
  /* s1 and s3 can overlap since they are destined for different tracks */
  /* s2 will overlap both */
  /* s4 destined for no track */
  _CREATE_SOURCE (layer, s1, 0, 12);
  _CREATE_SOURCE (layer, s2, 5, 10);
  _CREATE_SOURCE (layer, s3, 0, 10);
  _CREATE_SOURCE (layer, s4, 0, 20);

  e1 = GES_TRACK_ELEMENT (ges_effect_new ("videobalance"));
  fail_unless (ges_container_add (GES_CONTAINER (s2),
          GES_TIMELINE_ELEMENT (e1)));
  e2 = GES_TRACK_ELEMENT (ges_effect_new ("agingtv ! vertigotv"));
  fail_unless (ges_container_add (GES_CONTAINER (s2),
          GES_TIMELINE_ELEMENT (e2)));
  e3 = GES_TRACK_ELEMENT (ges_effect_new ("alpha"));
  fail_unless (ges_container_add (GES_CONTAINER (s2),
          GES_TIMELINE_ELEMENT (e3)));
  assert_equals_int (0,
      ges_clip_get_top_effect_index (s2, GES_BASE_EFFECT (e1)));
  assert_equals_int (1,
      ges_clip_get_top_effect_index (s2, GES_BASE_EFFECT (e2)));
  assert_equals_int (2,
      ges_clip_get_top_effect_index (s2, GES_BASE_EFFECT (e3)));

  assert_num_children (s1, 0);
  assert_num_children (s2, 3);
  assert_num_children (s3, 0);

  ges_timeline_element_set_child_properties (GES_TIMELINE_ELEMENT (s2),
      "scratch-lines", 2, "speed", 50.0, NULL);

  ctrl_source = GST_CONTROL_SOURCE (gst_interpolation_control_source_new ());
  g_object_set (G_OBJECT (ctrl_source), "mode",
      GST_INTERPOLATION_MODE_NONE, NULL);
  fail_unless (gst_timed_value_control_source_set
      (GST_TIMED_VALUE_CONTROL_SOURCE (ctrl_source), 0, 1.0));
  fail_unless (gst_timed_value_control_source_set
      (GST_TIMED_VALUE_CONTROL_SOURCE (ctrl_source), 4, 7.0));
  fail_unless (gst_timed_value_control_source_set
      (GST_TIMED_VALUE_CONTROL_SOURCE (ctrl_source), 8, 3.0));
  fail_unless (ges_track_element_set_control_source (e2, ctrl_source,
          "scratch-lines", "direct-absolute"));
  gst_object_unref (ctrl_source);

  st_data.tr1 = track1;
  st_data.tr2 = track2;
  st_data.clips[0] = s1;
  st_data.clips[1] = s2;
  st_data.clips[2] = s3;
  st_data.clips[3] = s4;
  st_data.num_calls[0] = 0;
  st_data.num_calls[1] = 0;
  st_data.num_calls[2] = 0;
  st_data.num_calls[3] = 0;
  st_data.effects[0] = e1;
  st_data.effects[1] = e2;
  st_data.effects[2] = e3;
  st_data.num_unrecognised = 0;

  g_signal_connect (timeline, "select-tracks-for-object",
      G_CALLBACK (select_tracks_cb), &st_data);

  /* adding layer to the timeline will trigger track selection, this */
  GST_DEBUG ("Add the layer to the timeline");
  fail_unless (ges_timeline_add_layer (timeline, layer));
  /* The timeline steals our reference to the layer */
  ASSERT_OBJECT_REFCOUNT (layer, "layer", 1);
  fail_unless (layer->timeline == timeline);

  layers = ges_timeline_get_layers (timeline);
  fail_unless (g_list_find (layers, layer) != NULL);
  g_list_free_full (layers, gst_object_unref);

  fail_unless (ges_layer_get_auto_transition (layer));

  assert_equals_int (st_data.num_unrecognised, 0);

  /* Make sure the associated TrackElements are in the Track */
  assert_num_children (s1, 1);
  el = GES_CONTAINER_CHILDREN (s1)->data;
  fail_unless (GES_IS_SOURCE (el));
  fail_unless (ges_track_element_get_track (el) == track1);
  ASSERT_OBJECT_REFCOUNT (el, "1 timeline + 1 track + 1 clip", 3);
  /* called once for source */
  assert_equals_int (st_data.num_calls[0], 1);

  /* 2 sources + 4 effects */
  assert_num_children (s2, 6);
  trackelements = GES_CONTAINER_CHILDREN (s2);
  /* sources at the end */
  el = g_list_nth_data (trackelements, 5);
  fail_unless (GES_IS_SOURCE (el));
  el2 = g_list_nth_data (trackelements, 4);
  fail_unless (GES_IS_SOURCE (el2));

  /* font-desc is originally "", but on setting switches to Normal, so we
   * set it explicitly */
  ges_timeline_element_set_child_properties (GES_TIMELINE_ELEMENT (el),
      "font-desc", "Normal", NULL);
  assert_equal_children_properties (el, el2);
  assert_equal_bindings (el, el2);

  assert_equals_int (GES_TIMELINE_ELEMENT_PRIORITY (el),
      GES_TIMELINE_ELEMENT_PRIORITY (el2));

  /* check one in each track */
  fail_unless (ges_track_element_get_track (el)
      != ges_track_element_get_track (el2));
  fail_unless (ges_track_element_get_track (el) == track1
      || ges_track_element_get_track (el2) == track1);
  fail_unless (ges_track_element_get_track (el) == track2
      || ges_track_element_get_track (el2) == track2);

  /* effects */
  e_copy = NULL;
  for (tmp = trackelements; tmp; tmp = tmp->next) {
    el = tmp->data;
    ASSERT_OBJECT_REFCOUNT (el, "1 timeline + 1 track + 1 clip", 3);
    if (GES_IS_BASE_EFFECT (el)) {
      if (el == e1) {
        fail_if (found_e1);
        found_e1 = TRUE;
      } else if (el == e2) {
        fail_if (found_e2);
        found_e2 = TRUE;
      } else if (el == e3) {
        fail_if (found_e3);
        found_e3 = TRUE;
      } else {
        fail_if (e_copy);
        e_copy = el;
      }
    }
  }
  fail_unless (found_e1);
  fail_unless (found_e2);
  fail_unless (found_e3);
  fail_unless (e_copy);

  fail_unless (ges_track_element_get_track (e1) == track1);
  fail_unless (ges_track_element_get_track (e3) == track2);

  assert_equal_children_properties (e2, e_copy);
  assert_equal_bindings (e2, e_copy);

  /* check one in each track */
  fail_unless (ges_track_element_get_track (e2)
      != ges_track_element_get_track (e_copy));
  fail_unless (ges_track_element_get_track (e2) == track1
      || ges_track_element_get_track (e_copy) == track1);
  fail_unless (ges_track_element_get_track (e2) == track2
      || ges_track_element_get_track (e_copy) == track2);

  /* e2 copy placed next to e2 in top effect list */
  assert_equals_int (0,
      ges_clip_get_top_effect_index (s2, GES_BASE_EFFECT (e1)));
  assert_equals_int (1,
      ges_clip_get_top_effect_index (s2, GES_BASE_EFFECT (e2)));
  assert_equals_int (2,
      ges_clip_get_top_effect_index (s2, GES_BASE_EFFECT (e_copy)));
  assert_equals_int (3,
      ges_clip_get_top_effect_index (s2, GES_BASE_EFFECT (e3)));

  /* called 4 times: 1 for source, and 1 for each effect (3) */
  assert_equals_int (st_data.num_calls[1], 4);

  assert_num_children (s3, 1);
  el = GES_CONTAINER_CHILDREN (s3)->data;
  fail_unless (GES_IS_SOURCE (el));
  fail_unless (ges_track_element_get_track (el) == track2);
  ASSERT_OBJECT_REFCOUNT (el, "1 timeline + 1 track + 1 clip", 3);
  /* called once for source */
  assert_equals_int (st_data.num_calls[2], 1);

  /* one child but no track */
  assert_num_children (s4, 1);
  el = GES_CONTAINER_CHILDREN (s4)->data;
  fail_unless (GES_IS_SOURCE (el));
  fail_unless (ges_track_element_get_track (el) == NULL);
  ASSERT_OBJECT_REFCOUNT (el, "1 clip", 1);
  /* called once for source (where no track was selected) */
  assert_equals_int (st_data.num_calls[0], 1);

  /* 2 sources + 1 transition + 2 effects */
  assert_num_in_track (track1, 5);
  assert_num_in_track (track2, 5);

  el = NULL;
  trackelements = ges_track_get_elements (track1);
  for (tmp = trackelements; tmp; tmp = tmp->next) {
    if (GES_IS_VIDEO_TRANSITION (tmp->data)) {
      fail_if (el);
      el = tmp->data;
    }
  }
  g_list_free_full (trackelements, gst_object_unref);
  fail_unless (GES_IS_CLIP (GES_TIMELINE_ELEMENT_PARENT (el)));
  transition = GES_CLIP (GES_TIMELINE_ELEMENT_PARENT (el));
  assert_layer (transition, layer);

  CHECK_OBJECT_PROPS (transition, 5, 0, 7);
  CHECK_OBJECT_PROPS (el, 5, 0, 7);
  fail_unless (ges_track_element_get_track (el) == track1);
  /* make sure we can change the transition type */
  fail_unless (ges_video_transition_set_transition_type (GES_VIDEO_TRANSITION
          (el), GES_VIDEO_STANDARD_TRANSITION_TYPE_BARNDOOR_H));

  el = NULL;
  trackelements = ges_track_get_elements (track2);
  for (tmp = trackelements; tmp; tmp = tmp->next) {
    if (GES_IS_VIDEO_TRANSITION (tmp->data)) {
      fail_if (el);
      el = tmp->data;
    }
  }
  g_list_free_full (trackelements, gst_object_unref);
  fail_unless (GES_IS_CLIP (GES_TIMELINE_ELEMENT_PARENT (el)));
  transition = GES_CLIP (GES_TIMELINE_ELEMENT_PARENT (el));
  assert_layer (transition, layer);

  CHECK_OBJECT_PROPS (transition, 5, 0, 5);
  CHECK_OBJECT_PROPS (el, 5, 0, 5);
  fail_unless (ges_track_element_get_track (el) == track2);
  /* make sure we can change the transition type */
  fail_unless (ges_video_transition_set_transition_type (GES_VIDEO_TRANSITION
          (el), GES_VIDEO_STANDARD_TRANSITION_TYPE_BARNDOOR_H));

  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_ges_pipeline_change_state)
{
  GstState state;
  GESAsset *asset;
  GESLayer *layer;
  GESTimeline *timeline;
  GESPipeline *pipeline;

  ges_init ();

  layer = ges_layer_new ();
  timeline = ges_timeline_new_audio_video ();
  fail_unless (ges_timeline_add_layer (timeline, layer));

  pipeline = ges_test_create_pipeline (timeline);

  asset = ges_asset_request (GES_TYPE_TEST_CLIP, NULL, NULL);
  ges_layer_add_asset (layer, asset, 0, 0, 10, GES_TRACK_TYPE_UNKNOWN);
  gst_object_unref (asset);

  ges_timeline_commit (timeline);
  ASSERT_SET_STATE (GST_ELEMENT (pipeline), GST_STATE_PLAYING,
      GST_STATE_CHANGE_ASYNC);
  fail_unless (gst_element_get_state (GST_ELEMENT (pipeline), &state, NULL,
          GST_CLOCK_TIME_NONE) == GST_STATE_CHANGE_SUCCESS);
  fail_unless (state == GST_STATE_PLAYING);
  ASSERT_SET_STATE (GST_ELEMENT (pipeline), GST_STATE_NULL,
      GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (pipeline);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_ges_timeline_element_name)
{
  GESClip *clip, *clip1, *clip2, *clip3, *clip4, *clip5;
  GESAsset *asset;
  GESTimeline *timeline;
  GESLayer *layer;

  ges_init ();

  timeline = ges_timeline_new_audio_video ();
  layer = ges_layer_new ();
  fail_unless (ges_timeline_add_layer (timeline, layer));

  asset = ges_asset_request (GES_TYPE_TEST_CLIP, NULL, NULL);
  clip = ges_layer_add_asset (layer, asset, 0, 0, 10, GES_TRACK_TYPE_UNKNOWN);
  fail_unless_equals_string (GES_TIMELINE_ELEMENT_NAME (clip), "testclip0");


  clip1 = GES_CLIP (ges_test_clip_new ());
  fail_unless_equals_string (GES_TIMELINE_ELEMENT_NAME (clip1), "testclip1");

  ges_timeline_element_set_name (GES_TIMELINE_ELEMENT (clip1), "testclip1");
  fail_unless_equals_string (GES_TIMELINE_ELEMENT_NAME (clip1), "testclip1");

  /* Check that trying to set to a name that is already used leads to
   * a change in the name */
  ges_timeline_element_set_name (GES_TIMELINE_ELEMENT (clip), "testclip1");
  fail_unless_equals_string (GES_TIMELINE_ELEMENT_NAME (clip), "testclip2");

  ges_timeline_element_set_name (GES_TIMELINE_ELEMENT (clip1), "testclip4");
  fail_unless_equals_string (GES_TIMELINE_ELEMENT_NAME (clip1), "testclip4");

  clip2 = GES_CLIP (ges_test_clip_new ());
  fail_unless_equals_string (GES_TIMELINE_ELEMENT_NAME (clip2), "testclip5");
  ges_timeline_element_set_name (GES_TIMELINE_ELEMENT (clip2), NULL);
  fail_unless_equals_string (GES_TIMELINE_ELEMENT_NAME (clip2), "testclip6");

  clip3 = GES_CLIP (ges_test_clip_new ());
  fail_unless_equals_string (GES_TIMELINE_ELEMENT_NAME (clip3), "testclip7");
  ges_timeline_element_set_name (GES_TIMELINE_ELEMENT (clip3), "testclip5");
  fail_unless_equals_string (GES_TIMELINE_ELEMENT_NAME (clip3), "testclip8");

  clip4 = GES_CLIP (ges_test_clip_new ());
  fail_unless_equals_string (GES_TIMELINE_ELEMENT_NAME (clip4), "testclip9");


  clip5 = GES_CLIP (ges_test_clip_new ());
  ges_timeline_element_set_name (GES_TIMELINE_ELEMENT (clip5),
      "Something I want!");
  fail_unless_equals_string (GES_TIMELINE_ELEMENT_NAME (clip5),
      "Something I want!");

  gst_object_unref (asset);

  gst_object_unref (clip1);
  gst_object_unref (clip2);
  gst_object_unref (clip3);
  gst_object_unref (clip4);
  gst_object_unref (clip5);
  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges-basic");
  TCase *tc_chain = tcase_create ("basic");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_ges_scenario);
  tcase_add_test (tc_chain, test_ges_timeline_add_layer);
  tcase_add_test (tc_chain, test_ges_timeline_add_layer_first);
  tcase_add_test (tc_chain, test_ges_timeline_remove_track);
  tcase_add_test (tc_chain, test_ges_timeline_remove_layer);
  tcase_add_test (tc_chain, test_ges_timeline_multiple_tracks);
  tcase_add_test (tc_chain, test_ges_pipeline_change_state);
  tcase_add_test (tc_chain, test_ges_timeline_element_name);

  return s;
}

GST_CHECK_MAIN (ges);
