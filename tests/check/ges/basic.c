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

GST_START_TEST (test_ges_init)
{
  /* Yes, I know.. minimalistic... */
  ges_init ();
}

GST_END_TEST;

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
  g_list_foreach (layers, (GFunc) gst_object_unref, NULL);
  g_list_free (layers);

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
   * 2 by the timeline */
  ASSERT_OBJECT_REFCOUNT (trackelement, "trackelement", 4);
  /* There are 3 references:
   * 1 by the clip
   * 2 by the timeline
   * 1 by the track */
  ASSERT_OBJECT_REFCOUNT (trackelement, "trackelement", 4);

  GST_DEBUG ("Remove the Clip from the layer");

  /* Now remove the clip */
  gst_object_ref (source);
  ASSERT_OBJECT_REFCOUNT (layer, "layer", 1);
  fail_unless (ges_layer_remove_clip (layer, GES_CLIP (source)));
  ASSERT_OBJECT_REFCOUNT (source, "source", 1);
  ASSERT_OBJECT_REFCOUNT (layer, "layer", 1);
  tmp_layer = ges_clip_get_layer (GES_CLIP (source));
  fail_unless (tmp_layer == NULL);
  gst_object_unref (source);

  GST_DEBUG ("Removing track from the timeline");
  /* Remove the track from the timeline */
  gst_object_ref (track);
  fail_unless (ges_timeline_remove_track (timeline, track));
  fail_unless (ges_track_get_timeline (track) == NULL);

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
}

GST_END_TEST;

/* very similar to the above, except we add the clip to the layer
 * and then add it to the timeline.
 */

GST_START_TEST (test_ges_timeline_add_layer)
{
  GESTimeline *timeline;
  GESLayer *layer, *tmp_layer;
  GESTrack *track;
  GESTestClip *s1, *s2, *s3;
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

  /* Create a source and add it to the Layer */
  GST_DEBUG ("Creating a source");
  s1 = ges_test_clip_new ();
  fail_unless (s1 != NULL);
  fail_unless (ges_layer_add_clip (layer, GES_CLIP (s1)));
  tmp_layer = ges_clip_get_layer (GES_CLIP (s1));
  fail_unless (tmp_layer == layer);
  ASSERT_OBJECT_REFCOUNT (layer, "layer", 2);
  gst_object_unref (tmp_layer);

  GST_DEBUG ("Creating a source");
  s2 = ges_test_clip_new ();
  fail_unless (s2 != NULL);
  fail_unless (ges_layer_add_clip (layer, GES_CLIP (s2)));
  tmp_layer = ges_clip_get_layer (GES_CLIP (s2));
  fail_unless (tmp_layer == layer);
  ASSERT_OBJECT_REFCOUNT (layer, "layer", 2);
  gst_object_unref (tmp_layer);

  GST_DEBUG ("Creating a source");
  s3 = ges_test_clip_new ();
  fail_unless (s3 != NULL);
  fail_unless (ges_layer_add_clip (layer, GES_CLIP (s3)));
  tmp_layer = ges_clip_get_layer (GES_CLIP (s3));
  fail_unless (tmp_layer == layer);
  ASSERT_OBJECT_REFCOUNT (layer, "layer", 2);
  gst_object_unref (tmp_layer);

  GST_DEBUG ("Add the layer to the timeline");
  fail_unless (ges_timeline_add_layer (timeline, layer));
  /* The timeline steals our reference to the layer */
  ASSERT_OBJECT_REFCOUNT (layer, "layer", 1);
  fail_unless (layer->timeline == timeline);
  layers = ges_timeline_get_layers (timeline);
  fail_unless (g_list_find (layers, layer) != NULL);
  g_list_foreach (layers, (GFunc) gst_object_unref, NULL);
  g_list_free (layers);

  /* Make sure the associated TrackElements are in the Track */
  trackelements = GES_CONTAINER_CHILDREN (s1);
  fail_unless (trackelements != NULL);
  trackelement = GES_TRACK_ELEMENT (trackelements->data);
  /* There are 3 references:
   * 1 by the clip
   * 1 by the trackelement
   * 2 by the timeline */
  ASSERT_OBJECT_REFCOUNT (trackelement, "trackelement", 4);
  /* There are 3 references:
   * 1 by the clip
   * 1 by the timeline
   * 2 by the trackelement */
  ASSERT_OBJECT_REFCOUNT (trackelement, "trackelement", 4);

  trackelements = GES_CONTAINER_CHILDREN (s2);
  trackelement = GES_TRACK_ELEMENT (trackelements->data);
  fail_unless (trackelements != NULL);

  /* There are 3 references:
   * 1 by the clip
   * 1 by the timeline
   * 2 by the trackelement */
  ASSERT_OBJECT_REFCOUNT (GES_TRACK_ELEMENT (trackelement), "trackelement", 4);

  trackelements = GES_CONTAINER_CHILDREN (s3);
  trackelement = GES_TRACK_ELEMENT (trackelements->data);
  fail_unless (trackelements != NULL);

  /* There are 3 references:
   * 1 by the clip
   * 1 by the timeline
   * 2 by the trackelement */
  ASSERT_OBJECT_REFCOUNT (trackelement, "trackelement", 4);

  /* theoretically this is all we need to do to ensure cleanup */
  gst_object_unref (timeline);
}

GST_END_TEST;

/* this time we add the layer before we add the track. */

GST_START_TEST (test_ges_timeline_add_layer_first)
{
  GESTimeline *timeline;
  GESLayer *layer, *tmp_layer;
  GESTrack *track;
  GESTestClip *s1, *s2, *s3;
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

  /* Create a source and add it to the Layer */
  GST_DEBUG ("Creating a source");
  s1 = ges_test_clip_new ();
  fail_unless (s1 != NULL);
  fail_unless (ges_layer_add_clip (layer, GES_CLIP (s1)));
  tmp_layer = ges_clip_get_layer (GES_CLIP (s1));
  fail_unless (tmp_layer == layer);
  gst_object_unref (tmp_layer);

  GST_DEBUG ("Creating a source");
  s2 = ges_test_clip_new ();
  fail_unless (s2 != NULL);
  fail_unless (ges_layer_add_clip (layer, GES_CLIP (s2)));
  tmp_layer = ges_clip_get_layer (GES_CLIP (s2));
  fail_unless (tmp_layer == layer);
  gst_object_unref (tmp_layer);

  GST_DEBUG ("Creating a source");
  s3 = ges_test_clip_new ();
  fail_unless (s3 != NULL);
  fail_unless (ges_layer_add_clip (layer, GES_CLIP (s3)));
  tmp_layer = ges_clip_get_layer (GES_CLIP (s3));
  fail_unless (tmp_layer == layer);
  gst_object_unref (tmp_layer);

  GST_DEBUG ("Add the layer to the timeline");
  fail_unless (ges_timeline_add_layer (timeline, layer));
  /* The timeline steals our reference to the layer */
  ASSERT_OBJECT_REFCOUNT (layer, "layer", 1);
  fail_unless (layer->timeline == timeline);
  layers = ges_timeline_get_layers (timeline);
  fail_unless (g_list_find (layers, layer) != NULL);
  g_list_foreach (layers, (GFunc) gst_object_unref, NULL);
  g_list_free (layers);

  GST_DEBUG ("Add the track to the timeline");
  fail_unless (ges_timeline_add_track (timeline, track));
  ASSERT_OBJECT_REFCOUNT (track, "track", 1);
  fail_unless (ges_track_get_timeline (track) == timeline);
  fail_unless ((gpointer) GST_ELEMENT_PARENT (track) == (gpointer) timeline);

  /* Make sure the associated TrackElements are in the Track */
  trackelements = GES_CONTAINER_CHILDREN (s1);
  fail_unless (trackelements != NULL);
  for (tmp = trackelements; tmp; tmp = tmp->next) {
    /* Each object has 3 references:
     * 1 by the clip
     * 1 by the track
     * 2 by the timeline */
    ASSERT_OBJECT_REFCOUNT (GES_TRACK_ELEMENT (tmp->data), "trackelement", 4);
  }

  trackelements = GES_CONTAINER_CHILDREN (s2);
  fail_unless (trackelements != NULL);
  for (tmp = trackelements; tmp; tmp = tmp->next) {
    /* Each object has 3 references:
     * 1 by the clip
     * 1 by the track
     * 2 by the timeline */
    ASSERT_OBJECT_REFCOUNT (GES_TRACK_ELEMENT (tmp->data), "trackelement", 4);
  }

  trackelements = GES_CONTAINER_CHILDREN (s3);
  fail_unless (trackelements != NULL);
  for (tmp = trackelements; tmp; tmp = tmp->next) {
    /* Each object has 3 references:
     * 1 by the clip
     * 1 by the track
     * 2 by the timeline */
    ASSERT_OBJECT_REFCOUNT (GES_TRACK_ELEMENT (tmp->data), "trackelement", 4);
  }

  /* theoretically this is all we need to do to ensure cleanup */
  gst_object_unref (timeline);
}

GST_END_TEST;

GST_START_TEST (test_ges_timeline_remove_track)
{
  GESTimeline *timeline;
  GESLayer *layer, *tmp_layer;
  GESTrack *track;
  GESTestClip *s1, *s2, *s3;
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

  /* Create a source and add it to the Layer */
  GST_DEBUG ("Creating a source");
  s1 = ges_test_clip_new ();
  fail_unless (s1 != NULL);
  fail_unless (ges_layer_add_clip (layer, GES_CLIP (s1)));
  tmp_layer = ges_clip_get_layer (GES_CLIP (s1));
  fail_unless (tmp_layer == layer);
  gst_object_unref (tmp_layer);
  ASSERT_OBJECT_REFCOUNT (layer, "1 for the timeline", 1);

  GST_DEBUG ("Creating a source");
  s2 = ges_test_clip_new ();
  fail_unless (s2 != NULL);
  fail_unless (ges_layer_add_clip (layer, GES_CLIP (s2)));
  tmp_layer = ges_clip_get_layer (GES_CLIP (s2));
  fail_unless (tmp_layer == layer);
  gst_object_unref (tmp_layer);
  ASSERT_OBJECT_REFCOUNT (layer, "1 for the timeline", 1);

  GST_DEBUG ("Creating a source");
  s3 = ges_test_clip_new ();
  fail_unless (s3 != NULL);
  fail_unless (ges_layer_add_clip (layer, GES_CLIP (s3)));
  tmp_layer = ges_clip_get_layer (GES_CLIP (s3));
  fail_unless (tmp_layer == layer);
  gst_object_unref (tmp_layer);
  ASSERT_OBJECT_REFCOUNT (layer, "1 for the timeline", 1);

  GST_DEBUG ("Add the layer to the timeline");
  fail_unless (ges_timeline_add_layer (timeline, layer));
  /* The timeline steals our reference to the layer */
  ASSERT_OBJECT_REFCOUNT (layer, "layer", 1);
  fail_unless (layer->timeline == timeline);

  layers = ges_timeline_get_layers (timeline);
  fail_unless (g_list_find (layers, layer) != NULL);
  g_list_foreach (layers, (GFunc) gst_object_unref, NULL);
  g_list_free (layers);
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
     * 2 by the timeline */
    ASSERT_OBJECT_REFCOUNT (GES_TRACK_ELEMENT (tmp->data), "trackelement", 4);
  }
  /* There are 3 references held:
   * 1 by the container
   * 1 by the track
   * 2 by the timeline */
  ASSERT_OBJECT_REFCOUNT (t1, "trackelement", 4);

  trackelements = GES_CONTAINER_CHILDREN (s2);
  fail_unless (trackelements != NULL);
  t2 = GES_TRACK_ELEMENT (trackelements->data);
  for (tmp = trackelements; tmp; tmp = tmp->next) {
    /* There are 3 references held:
     * 1 by the clip
     * 1 by the track
     * 2 by the timeline */
    ASSERT_OBJECT_REFCOUNT (GES_TRACK_ELEMENT (tmp->data), "trackelement", 4);
  }
  /* There are 3 references held:
   * 1 by the container
   * 1 by the track
   * 2 by the timeline */
  ASSERT_OBJECT_REFCOUNT (t2, "t2", 4);

  trackelements = GES_CONTAINER_CHILDREN (s3);
  fail_unless (trackelements != NULL);
  t3 = GES_TRACK_ELEMENT (trackelements->data);
  for (tmp = trackelements; tmp; tmp = tmp->next) {
    /* There are 3 references held:
     * 1 by the clip
     * 1 by the track
     * 2 by the timeline */
    ASSERT_OBJECT_REFCOUNT (GES_TRACK_ELEMENT (tmp->data), "trackelement", 4);
  }
  /* There are 3 references held:
   * 1 by the container
   * 1 by the track
   * 2 by the timeline */
  ASSERT_OBJECT_REFCOUNT (t3, "t3", 4);

  /* remove the track and check that the track elements have been released */
  fail_unless (ges_timeline_remove_track (timeline, track));

  ASSERT_OBJECT_REFCOUNT (t1, "trackelement", 2);
  ASSERT_OBJECT_REFCOUNT (t2, "trackelement", 2);
  ASSERT_OBJECT_REFCOUNT (t3, "trackelement", 2);
  ASSERT_OBJECT_REFCOUNT (layer, "1 for the timeline", 1);
  ASSERT_OBJECT_REFCOUNT (timeline, "1 for the us", 1);
  tmp = ges_layer_get_clips (layer);
  assert_equals_int (g_list_length (tmp), 3);
  g_list_foreach (tmp, (GFunc) gst_object_unref, NULL);

  gst_check_objects_destroyed_on_unref (G_OBJECT (timeline),
      G_OBJECT (layer), t1, t2, t3, NULL);
}

GST_END_TEST;

typedef struct
{
  GESTestClip **o1, **o2, **o3;
  GESTrack **tr1, **tr2;
} SelectTracksData;

static GPtrArray *
select_tracks_cb (GESTimeline * timeline, GESClip * clip,
    GESTrackElement * track_element, SelectTracksData * st_data)
{
  GESTrack *track;

  GPtrArray *ret = g_ptr_array_new ();
  track = (clip == (GESClip *) * st_data->o2) ? *st_data->tr2 : *st_data->tr1;

  gst_object_ref (track);

  g_ptr_array_add (ret, track);

  return ret;
}

GST_START_TEST (test_ges_timeline_multiple_tracks)
{
  GESTimeline *timeline;
  GESLayer *layer, *tmp_layer;
  GESTrack *track1, *track2;
  GESTestClip *s1, *s2, *s3;
  GESTrackElement *t1, *t2, *t3;
  GList *trackelements, *tmp, *layers;
  SelectTracksData st_data = { &s1, &s2, &s3, &track1, &track2 };

  ges_init ();

  /* Timeline and 1 Layer */
  GST_DEBUG ("Create a timeline");
  timeline = ges_timeline_new ();
  fail_unless (timeline != NULL);

  g_signal_connect (timeline, "select-tracks-for-object",
      G_CALLBACK (select_tracks_cb), &st_data);

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

  /* Create a source and add it to the Layer */
  GST_DEBUG ("Creating a source");
  s1 = ges_test_clip_new ();
  fail_unless (s1 != NULL);
  fail_unless (ges_layer_add_clip (layer, GES_CLIP (s1)));
  tmp_layer = ges_clip_get_layer (GES_CLIP (s1));
  fail_unless (tmp_layer == layer);
  gst_object_unref (tmp_layer);

  GST_DEBUG ("Creating a source");
  s2 = ges_test_clip_new ();
  fail_unless (s2 != NULL);
  fail_unless (ges_layer_add_clip (layer, GES_CLIP (s2)));
  tmp_layer = ges_clip_get_layer (GES_CLIP (s2));
  fail_unless (tmp_layer == layer);
  gst_object_unref (tmp_layer);

  GST_DEBUG ("Creating a source");
  s3 = ges_test_clip_new ();
  fail_unless (s3 != NULL);
  fail_unless (ges_layer_add_clip (layer, GES_CLIP (s3)));
  tmp_layer = ges_clip_get_layer (GES_CLIP (s3));
  fail_unless (tmp_layer == layer);
  gst_object_unref (tmp_layer);

  GST_DEBUG ("Add the layer to the timeline");
  fail_unless (ges_timeline_add_layer (timeline, layer));
  /* The timeline steals our reference to the layer */
  ASSERT_OBJECT_REFCOUNT (layer, "layer", 1);
  fail_unless (layer->timeline == timeline);

  layers = ges_timeline_get_layers (timeline);
  fail_unless (g_list_find (layers, layer) != NULL);
  g_list_foreach (layers, (GFunc) gst_object_unref, NULL);
  g_list_free (layers);

  /* Make sure the associated TrackElements are in the Track */
  trackelements = GES_CONTAINER_CHILDREN (s1);
  fail_unless (trackelements != NULL);
  t1 = GES_TRACK_ELEMENT ((trackelements)->data);
  for (tmp = trackelements; tmp; tmp = tmp->next) {
    /* There are 3 references held:
     * 1 by the clip
     * 1 by the track
     * 2 by the timeline */
    ASSERT_OBJECT_REFCOUNT (GES_TRACK_ELEMENT (tmp->data), "trackelement", 4);
    fail_unless (ges_track_element_get_track (tmp->data) == track1);
  }
  gst_object_ref (t1);
  /* There are 3 references held:
   * 1 by the container
   * 1 by the track
   * 2 by the timeline
   * 1 added by ourselves above (gst_object_ref (t1)) */
  ASSERT_OBJECT_REFCOUNT (t1, "trackelement", 5);

  trackelements = GES_CONTAINER_CHILDREN (s2);
  fail_unless (trackelements != NULL);
  t2 = GES_TRACK_ELEMENT (trackelements->data);
  for (tmp = trackelements; tmp; tmp = tmp->next) {
    /* There are 3 references held:
     * 1 by the clip
     * 1 by the track
     * 2 by the timeline */
    ASSERT_OBJECT_REFCOUNT (GES_TRACK_ELEMENT (tmp->data), "trackelement", 4);
    fail_unless (ges_track_element_get_track (tmp->data) == track2);
  }
  gst_object_ref (t2);
  /* There are 3 references held:
   * 1 by the container
   * 1 by the track
   * 2 by the timeline
   * 1 added by ourselves above (gst_object_ref (t2)) */
  ASSERT_OBJECT_REFCOUNT (t2, "t2", 5);

  trackelements = GES_CONTAINER_CHILDREN (s3);
  fail_unless (trackelements != NULL);
  t3 = GES_TRACK_ELEMENT (trackelements->data);
  for (tmp = trackelements; tmp; tmp = tmp->next) {
    /* There are 3 references held:
     * 1 by the clip
     * 1 by the track
     * 2 by the timeline */
    ASSERT_OBJECT_REFCOUNT (GES_TRACK_ELEMENT (tmp->data), "trackelement", 4);
    fail_unless (ges_track_element_get_track (tmp->data) == track1);
  }
  gst_object_ref (t3);
  /* There are 3 references held:
   * 1 by the container
   * 1 by the track
   * 2 by the timeline
   * 1 added by ourselves above (gst_object_ref (t3)) */
  ASSERT_OBJECT_REFCOUNT (t3, "t3", 5);

  gst_object_unref (t1);
  gst_object_unref (t2);
  gst_object_unref (t3);

  gst_object_unref (timeline);
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
}

GST_END_TEST;

static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges-basic");
  TCase *tc_chain = tcase_create ("basic");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_ges_init);
  tcase_add_test (tc_chain, test_ges_scenario);
  tcase_add_test (tc_chain, test_ges_timeline_add_layer);
  tcase_add_test (tc_chain, test_ges_timeline_add_layer_first);
  tcase_add_test (tc_chain, test_ges_timeline_remove_track);
  tcase_add_test (tc_chain, test_ges_timeline_multiple_tracks);
  tcase_add_test (tc_chain, test_ges_pipeline_change_state);
  tcase_add_test (tc_chain, test_ges_timeline_element_name);

  return s;
}

GST_CHECK_MAIN (ges);
