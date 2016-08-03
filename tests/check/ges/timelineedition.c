/* GStreamer Editing Services
 *
 * Copyright (C) <2012> Thibault Saunier <thibault.saunier@collabora.com>
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

#define DEEP_CHECK(element, start, inpoint, duration)                          \
{                                                                              \
  GList *track_elements, *tmp;                                                 \
                                                                               \
  assert_equals_uint64 (_START (element), start);                              \
  assert_equals_uint64 (_INPOINT (element), inpoint);                          \
  assert_equals_uint64 (_DURATION (element), duration);                        \
                                                                               \
  track_elements = GES_CONTAINER_CHILDREN (element);                           \
  for (tmp = track_elements; tmp; tmp = tmp->next) {                           \
    assert_equals_uint64 (_START (tmp->data), start);                          \
    assert_equals_uint64 (_INPOINT (tmp->data), inpoint);                      \
    assert_equals_uint64 (_DURATION (tmp->data), duration);                    \
  }                                                                            \
}

GST_START_TEST (test_basic_timeline_edition)
{
  GESAsset *asset;
  GESTrack *track;
  GESTimeline *timeline;
  GESLayer *layer;
  GESTrackElement *trackelement, *trackelement1, *trackelement2;
  GESContainer *clip, *clip1, *clip2;

  track = GES_TRACK (ges_audio_track_new ());
  fail_unless (track != NULL);

  timeline = ges_timeline_new ();
  fail_unless (timeline != NULL);
  fail_unless (ges_timeline_add_track (timeline, track));

  layer = ges_layer_new ();
  fail_unless (layer != NULL);
  fail_unless (ges_timeline_add_layer (timeline, layer));

  asset = ges_asset_request (GES_TYPE_TEST_CLIP, NULL, NULL);
  fail_unless (GES_IS_ASSET (asset));

  /**
   * Our timeline
   *
   * inpoints 0-------   0--------      0-----------
   *          |  clip  |  |  clip1  |     |     clip2  |
   * time     0------- 10 --------20    50---------60
   */
  clip = GES_CONTAINER (ges_layer_add_asset (layer, asset, 0, 0, 10,
          GES_TRACK_TYPE_UNKNOWN));
  trackelement = GES_CONTAINER_CHILDREN (clip)->data;
  fail_unless (GES_IS_TRACK_ELEMENT (trackelement));

  clip1 = GES_CONTAINER (ges_layer_add_asset (layer, asset, 10, 0, 10,
          GES_TRACK_TYPE_UNKNOWN));
  trackelement1 = GES_CONTAINER_CHILDREN (clip1)->data;
  fail_unless (GES_IS_TRACK_ELEMENT (trackelement1));

  clip2 = GES_CONTAINER (ges_layer_add_asset (layer, asset, 50, 0, 60,
          GES_TRACK_TYPE_UNKNOWN));
  trackelement2 = GES_CONTAINER_CHILDREN (clip2)->data;
  fail_unless (GES_IS_TRACK_ELEMENT (trackelement2));

  CHECK_OBJECT_PROPS (trackelement, 0, 0, 10);
  CHECK_OBJECT_PROPS (trackelement1, 10, 0, 10);
  CHECK_OBJECT_PROPS (trackelement2, 50, 0, 60);

  /**
   * Simple rippling clip to: 10
   *
   * New timeline:
   * ------------
   *
   * inpoints 0-------   0--------      0-----------
   *          |  clip  |  |  clip1  |     |   clip2    |
   * time    10------- 20 --------30    60---------120
   */
  fail_unless (ges_container_edit (clip, NULL, -1, GES_EDIT_MODE_RIPPLE,
          GES_EDGE_NONE, 10) == TRUE);
  CHECK_OBJECT_PROPS (trackelement, 10, 0, 10);
  CHECK_OBJECT_PROPS (trackelement1, 20, 0, 10);
  CHECK_OBJECT_PROPS (trackelement2, 60, 0, 60);


  /* FIXME find a way to check that we are using the same MovingContext
   * inside the GESTrack */
  fail_unless (ges_container_edit (clip1, NULL, -1, GES_EDIT_MODE_RIPPLE,
          GES_EDGE_NONE, 40) == TRUE);
  CHECK_OBJECT_PROPS (trackelement, 10, 0, 10);
  CHECK_OBJECT_PROPS (trackelement1, 40, 0, 10);
  CHECK_OBJECT_PROPS (trackelement2, 80, 0, 60);

  /**
   * Rippling clip1 back to: 20 (getting to the exact same timeline as before
   */
  fail_unless (ges_container_edit (clip1, NULL, -1, GES_EDIT_MODE_RIPPLE,
          GES_EDGE_NONE, 20) == TRUE);
  CHECK_OBJECT_PROPS (trackelement, 10, 0, 10);
  CHECK_OBJECT_PROPS (trackelement1, 20, 0, 10);
  CHECK_OBJECT_PROPS (trackelement2, 60, 0, 60);

  /**
   * Simple move clip to: 27 and clip2 to 35
   *
   * New timeline:
   * ------------
   *                    0------------
   * inpoints   0-------|---  clip 0--|----------
   *            |  clip1 27 -|-----|-37   clip2   |
   * time      20-----------30   35-------------120
   */
  fail_unless (ges_container_edit (clip, NULL, -1, GES_EDIT_MODE_NORMAL,
          GES_EDGE_NONE, 27) == TRUE);
  fail_unless (ges_container_edit (clip2, NULL, -1, GES_EDIT_MODE_NORMAL,
          GES_EDGE_NONE, 35) == TRUE);
  CHECK_OBJECT_PROPS (trackelement, 27, 0, 10);
  CHECK_OBJECT_PROPS (trackelement1, 20, 0, 10);
  CHECK_OBJECT_PROPS (trackelement2, 35, 0, 60);

  /**
   * Trim start clip to: 32 and clip2 to 35
   *
   * New timeline:
   * ------------
   *                           5--------
   * inpoints   0-----------   | clip 0--|----------
   *            |  clip1     |  32----|-37   clip2   |
   * time      20-----------30      35-------------120
   */
  fail_unless (ges_container_edit (clip, NULL, -1, GES_EDIT_MODE_TRIM,
          GES_EDGE_START, 32) == TRUE);
  CHECK_OBJECT_PROPS (trackelement, 32, 5, 5);
  CHECK_OBJECT_PROPS (trackelement1, 20, 0, 10);
  CHECK_OBJECT_PROPS (trackelement2, 35, 0, 60);

  /* Ripple end clip to 42
   * New timeline:
   * ------------
   *                           5--------
   * inpoints   0-----------   | clip 0--|----------
   *            |  clip1     |  32----|-42   clip2   |
   * time      20-----------30      35-------------120
   */
  fail_unless (ges_container_edit (clip, NULL, -1, GES_EDIT_MODE_RIPPLE,
          GES_EDGE_END, 42) == TRUE);
  CHECK_OBJECT_PROPS (trackelement, 32, 5, 10);
  CHECK_OBJECT_PROPS (trackelement1, 20, 0, 10);
  CHECK_OBJECT_PROPS (trackelement2, 35, 0, 60);

  /**
   * New timeline:
   * ------------
   * inpoints 0-------     5-------- 0-----------
   *          |  clip1 |    |  clip1  ||  clip2    |
   * time    20-------30  32--------52 ---------112
   */
  fail_unless (ges_container_edit (clip2, NULL, -1, GES_EDIT_MODE_NORMAL,
          GES_EDGE_NONE, 42) == TRUE);
  fail_unless (ges_container_edit (clip, NULL, -1, GES_EDIT_MODE_RIPPLE,
          GES_EDGE_END, 52) == TRUE);
  CHECK_OBJECT_PROPS (trackelement, 32, 5, 20);
  CHECK_OBJECT_PROPS (trackelement1, 20, 0, 10);
  CHECK_OBJECT_PROPS (trackelement2, 52, 0, 60);

  /**
   * New timeline:
   * ------------
   * inpoints 0-------     5-------- 0------------
   *          |  clip1 |    |  clip   ||    clip2    |
   * time    20-------40  42--------62 ---------122
   */
  fail_unless (ges_container_edit (clip1, NULL, -1, GES_EDIT_MODE_RIPPLE,
          GES_EDGE_END, 40) == TRUE);
  CHECK_OBJECT_PROPS (trackelement, 42, 5, 20);
  CHECK_OBJECT_PROPS (trackelement1, 20, 0, 20);
  CHECK_OBJECT_PROPS (trackelement2, 62, 0, 60);

  /**
   * New timeline:
   * ------------
   * inpoints 0-------  3-------- 0------------
   *          |  clip1 ||  clip   ||    clip2    |
   * time    20-------40 --------62 ---------122
   */
  fail_unless (ges_container_edit (clip, NULL, -1, GES_EDIT_MODE_TRIM,
          GES_EDGE_START, 40) == TRUE);
  CHECK_OBJECT_PROPS (trackelement, 40, 3, 22);
  CHECK_OBJECT_PROPS (trackelement1, 20, 0, 20);
  CHECK_OBJECT_PROPS (trackelement2, 62, 0, 60);
  /**
   * New timeline:
   * ------------
   * inpoints 0------- 0-------- 0-----------
   *          |  clip1 ||   clip  ||  clip2     |
   * time    20------ 25 ------ 62 ---------122
   */
  fail_if (ges_container_edit (clip, NULL, -1, GES_EDIT_MODE_ROLL,
          GES_EDGE_START, 25));
  CHECK_OBJECT_PROPS (trackelement, 40, 3, 22);
  CHECK_OBJECT_PROPS (trackelement1, 20, 0, 20);
  CHECK_OBJECT_PROPS (trackelement2, 62, 0, 60);

  /* Make sure that not doing anything when not able to roll */
  fail_if (ges_container_edit (clip1, NULL, -1, GES_EDIT_MODE_ROLL,
          GES_EDGE_END, 65) == TRUE, 0);
  CHECK_OBJECT_PROPS (trackelement, 40, 3, 22);
  CHECK_OBJECT_PROPS (trackelement1, 20, 0, 20);
  CHECK_OBJECT_PROPS (trackelement2, 62, 0, 60);

  gst_object_unref (timeline);
}

GST_END_TEST;

GST_START_TEST (test_snapping)
{
  GESTrack *track;
  GESTimeline *timeline;
  GESTrackElement *trackelement, *trackelement1, *trackelement2;
  GESContainer *clip, *clip1, *clip2;
  GESLayer *layer;
  GList *trackelements;

  track = GES_TRACK (ges_video_track_new ());
  fail_unless (track != NULL);

  timeline = ges_timeline_new ();
  fail_unless (timeline != NULL);

  fail_unless (ges_timeline_add_track (timeline, track));

  clip = GES_CONTAINER (ges_test_clip_new ());
  clip1 = GES_CONTAINER (ges_test_clip_new ());
  clip2 = GES_CONTAINER (ges_test_clip_new ());

  fail_unless (clip && clip1 && clip2);

  /**
   * Our timeline
   * ------------
   * inpoints 0------- 0-------- 0-----------
   *          |  clip1 ||   clip  ||  clip2     |
   * time    20------ 25 ------ 62 ---------122
   */
  g_object_set (clip, "start", (guint64) 25, "duration", (guint64) 37,
      "in-point", (guint64) 0, NULL);
  g_object_set (clip1, "start", (guint64) 20, "duration", (guint64) 15,
      "in-point", (guint64) 0, NULL);
  g_object_set (clip2, "start", (guint64) 62, "duration", (guint64) 60,
      "in-point", (guint64) 0, NULL);

  fail_unless ((layer = ges_timeline_append_layer (timeline)) != NULL);
  assert_equals_int (ges_layer_get_priority (layer), 0);


  fail_unless (ges_layer_add_clip (layer, GES_CLIP (clip)));
  fail_unless ((trackelements = GES_CONTAINER_CHILDREN (clip)) != NULL);
  fail_unless ((trackelement =
          GES_TRACK_ELEMENT (trackelements->data)) != NULL);
  fail_unless (ges_track_element_get_track (trackelement) == track);
  assert_equals_uint64 (_DURATION (trackelement), 37);

  ASSERT_OBJECT_REFCOUNT (trackelement, "track + timeline + clip", 4);
  ASSERT_OBJECT_REFCOUNT (clip, "layer + timeline", 2);

  fail_unless (ges_layer_add_clip (layer, GES_CLIP (clip1)));
  fail_unless ((trackelements = GES_CONTAINER_CHILDREN (clip1)) != NULL);
  fail_unless ((trackelement1 =
          GES_TRACK_ELEMENT (trackelements->data)) != NULL);
  fail_unless (ges_track_element_get_track (trackelement1) == track);
  assert_equals_uint64 (_DURATION (trackelement1), 15);

  /* Same ref logic */
  ASSERT_OBJECT_REFCOUNT (trackelement1, "First trackelement", 4);
  ASSERT_OBJECT_REFCOUNT (clip1, "First clip", 2);

  fail_unless (ges_layer_add_clip (layer, GES_CLIP (clip2)));
  fail_unless ((trackelements = GES_CONTAINER_CHILDREN (clip2)) != NULL);
  fail_unless ((trackelement2 =
          GES_TRACK_ELEMENT (trackelements->data)) != NULL);
  fail_unless (ges_track_element_get_track (trackelement2) == track);
  assert_equals_uint64 (_DURATION (trackelement2), 60);

  /* Same ref logic */
  ASSERT_OBJECT_REFCOUNT (trackelement2, "First trackelement", 4);
  ASSERT_OBJECT_REFCOUNT (clip2, "First clip", 2);

  /* Snaping to edge, so no move */
  g_object_set (timeline, "snapping-distance", (guint64) 3, NULL);
  fail_unless (ges_container_edit (clip1, NULL, -1, GES_EDIT_MODE_TRIM,
          GES_EDGE_END, 27) == TRUE);
  CHECK_OBJECT_PROPS (trackelement, 25, 0, 37);
  CHECK_OBJECT_PROPS (trackelement1, 20, 0, 5);
  CHECK_OBJECT_PROPS (trackelement2, 62, 0, 60);

  /* Snaping to edge, so no move */
  fail_if (ges_container_edit (clip1, NULL, -1, GES_EDIT_MODE_TRIM,
          GES_EDGE_END, 27));
  CHECK_OBJECT_PROPS (trackelement, 25, 0, 37);
  CHECK_OBJECT_PROPS (trackelement1, 20, 0, 5);
  CHECK_OBJECT_PROPS (trackelement2, 62, 0, 60);

  /**
   * New timeline:
   * ------------
   *                    0----------- 0-------------
   * inpoints   0-------|--   clip   ||   clip2      |
   *            |  clip1 25-|------- 62 -----------122
   * time      20----------30
   */
  g_object_set (timeline, "snapping-distance", (guint64) 0, NULL);
  ges_timeline_element_set_duration (GES_TIMELINE_ELEMENT (clip1), 10);
  CHECK_OBJECT_PROPS (trackelement, 25, 0, 37);
  CHECK_OBJECT_PROPS (trackelement1, 20, 0, 10);
  CHECK_OBJECT_PROPS (trackelement2, 62, 0, 60);

  /**
   * New timeline(the "layers" are just to help reading diagram, nothing else):
   * ------------
   *                    0----------
   *                    |   clip    |
   *                    25---------62
   * inpoints   0----------------------- 10--------
   *            |       clip1            ||  clip2   |
   * time      20---------------------- 72 --------122
   */
  /* Rolling involves only neighbour that are currently snapping */
  fail_unless (ges_timeline_element_roll_end (GES_TIMELINE_ELEMENT (clip1),
          62));
  fail_unless (ges_timeline_element_roll_end (GES_TIMELINE_ELEMENT (clip1),
          72) == TRUE);
  CHECK_OBJECT_PROPS (trackelement, 25, 0, 37);
  CHECK_OBJECT_PROPS (trackelement1, 20, 0, 52);
  CHECK_OBJECT_PROPS (trackelement2, 72, 10, 50);

  /**
   *                    0----------
   *                    |   clip    |
   *                    25---------62
   * inpoints           5--------------- 10--------
   *                    |     clip1      ||  clip2   |
   * time               25------------- 72 --------122
   */
  g_object_set (timeline, "snapping-distance", (guint64) 4, NULL);
  fail_unless (ges_timeline_element_trim (GES_TIMELINE_ELEMENT (clip1),
          28) == TRUE);
  CHECK_OBJECT_PROPS (trackelement, 25, 0, 37);
  CHECK_OBJECT_PROPS (trackelement1, 25, 5, 47);
  CHECK_OBJECT_PROPS (trackelement2, 72, 10, 50);

  /**
   *                    0----------
   *                    |   clip    |
   *                    25---------62
   * inpoints           5---------- 0---------
   *                    |  clip1    ||  clip2   |
   * time               25-------- 62 --------122
   */
  fail_unless (ges_timeline_element_roll_start (GES_TIMELINE_ELEMENT (clip2),
          59) == TRUE);
  CHECK_OBJECT_PROPS (trackelement, 25, 0, 37);
  CHECK_OBJECT_PROPS (trackelement1, 25, 5, 37);
  CHECK_OBJECT_PROPS (trackelement2, 62, 0, 60);

   /**
   * inpoints           0----------5---------- 0----------
   *                    |   clip    ||  clip1    ||  clip2   |
   * time               25---------62-------- 99 --------170
   */
  fail_unless (ges_timeline_element_ripple (GES_TIMELINE_ELEMENT (clip1),
          58) == TRUE);
  CHECK_OBJECT_PROPS (trackelement, 25, 0, 37);
  CHECK_OBJECT_PROPS (trackelement1, 62, 5, 37);
  CHECK_OBJECT_PROPS (trackelement2, 99, 0, 60);

  /**
   * inpoints     0----------5----------     0----------
   *              |   clip    ||  clip1    |   |  clip2    |
   * time         25---------62-------- 99  110--------170
   */
  ges_timeline_element_set_start (GES_TIMELINE_ELEMENT (clip2), 110);
  CHECK_OBJECT_PROPS (trackelement, 25, 0, 37);
  CHECK_OBJECT_PROPS (trackelement1, 62, 5, 37);
  CHECK_OBJECT_PROPS (trackelement2, 110, 0, 60);

  /**
   * inpoints     0----------5    5 --------- 0----------
   *              |   clip    |    |  clip1    ||  clip2    |
   * time         25---------62   73---------110--------170
   */
  fail_unless (ges_container_edit (clip1, NULL, -1, GES_EDIT_MODE_NORMAL,
          GES_EDGE_NONE, 72) == TRUE);
  CHECK_OBJECT_PROPS (trackelement, 25, 0, 37);
  CHECK_OBJECT_PROPS (trackelement1, 73, 5, 37);
  CHECK_OBJECT_PROPS (trackelement2, 110, 0, 60);

  /**
   * inpoints     0----------5----------     0----------
   *              |   clip    ||  clip1    |   |  clip2    |
   * time         25---------62-------- 99  110--------170
   */
  fail_unless (ges_container_edit (clip1, NULL, -1, GES_EDIT_MODE_NORMAL,
          GES_EDGE_NONE, 58) == TRUE);
  CHECK_OBJECT_PROPS (trackelement, 25, 0, 37);
  CHECK_OBJECT_PROPS (trackelement1, 62, 5, 37);
  CHECK_OBJECT_PROPS (trackelement2, 110, 0, 60);


  /**
   * inpoints     0----------5---------- 0----------
   *              |   clip    ||  clip1   ||  clip2    |
   * time         25---------62--------110--------170
   */
  g_object_set (clip1, "duration", (guint64) 46, NULL);
  CHECK_OBJECT_PROPS (trackelement, 25, 0, 37);
  CHECK_OBJECT_PROPS (trackelement1, 62, 5, 48);
  CHECK_OBJECT_PROPS (trackelement2, 110, 0, 60);

  /**
   * inpoints     5----------- 0--------- 0----------
   *              |   clip1    ||  clip2   ||  clip     |
   * time         62---------110--------170--------207
   */
  g_object_set (clip, "start", (guint64) 168, NULL);
  CHECK_OBJECT_PROPS (trackelement, 170, 0, 37);
  CHECK_OBJECT_PROPS (trackelement1, 62, 5, 48);
  CHECK_OBJECT_PROPS (trackelement2, 110, 0, 60);

  /* Check we didn't lose/screwed any references */
  ASSERT_OBJECT_REFCOUNT (trackelement, "First trackelement", 4);
  ASSERT_OBJECT_REFCOUNT (trackelement1, "Second trackelement", 4);
  ASSERT_OBJECT_REFCOUNT (trackelement2, "Third trackelement", 4);
  ASSERT_OBJECT_REFCOUNT (clip, "First clip", 2);
  ASSERT_OBJECT_REFCOUNT (clip1, "Second clip", 2);
  ASSERT_OBJECT_REFCOUNT (clip2, "Third clip", 2);

  check_destroyed (G_OBJECT (timeline), G_OBJECT (trackelement),
      trackelement1, trackelement2, clip, clip1, clip2, layer, NULL);
}

GST_END_TEST;

static void
asset_added_cb (GESProject * project, GESAsset * asset, void *mainloop)
{
  GstDiscovererInfo *info;

  info = ges_uri_clip_asset_get_info (GES_URI_CLIP_ASSET (asset));
  fail_unless (GST_IS_DISCOVERER_INFO (info));

  g_main_loop_quit ((GMainLoop *) mainloop);
}

GST_START_TEST (test_simple_triming)
{
  GList *assets;
  GMainLoop *mainloop;
  GESClipAsset *asset;
  GESProject *project;
  GESTimeline *timeline;
  GESLayer *layer;
  GESTimelineElement *element;

  gchar *uri = ges_test_file_uri ("audio_video.ogg");

  project = ges_project_new (NULL);

  mainloop = g_main_loop_new (NULL, FALSE);

  g_signal_connect (project, "asset-added", (GCallback) asset_added_cb,
      mainloop);
  ges_project_create_asset (project, uri, GES_TYPE_URI_CLIP);
  g_free (uri);

  g_main_loop_run (mainloop);

  /* the asset is now loaded */
  timeline = ges_timeline_new_audio_video ();
  assets = ges_project_list_assets (project, GES_TYPE_CLIP);

  assert_equals_int (g_list_length (assets), 1);
  asset = assets->data;

  layer = ges_layer_new ();
  ges_timeline_add_layer (timeline, layer);

  ges_layer_add_asset (layer, GES_ASSET (asset), 0, 0, 10,
      ges_clip_asset_get_supported_formats (asset));

  element = ges_layer_get_clips (layer)->data;

  DEEP_CHECK (element, 0, 0, 10);
  ges_container_edit (GES_CONTAINER (element), NULL, -1, GES_EDIT_MODE_TRIM,
      GES_EDGE_START, 5);
  DEEP_CHECK (element, 5, 5, 5);

  g_main_loop_unref (mainloop);
  gst_object_unref (timeline);
  gst_object_unref (project);
}

GST_END_TEST;

GST_START_TEST (test_timeline_edition_mode)
{
  GESTrack *track;
  GESTimeline *timeline;
  GESTrackElement *trackelement, *trackelement1, *trackelement2;
  GESContainer *clip, *clip1, *clip2;
  GESLayer *layer, *layer1, *layer2;
  GList *trackelements, *layers, *tmp;

  track = GES_TRACK (ges_video_track_new ());
  fail_unless (track != NULL);

  timeline = ges_timeline_new ();
  fail_unless (timeline != NULL);

  fail_unless (ges_timeline_add_track (timeline, track));

  clip = GES_CONTAINER (ges_test_clip_new ());
  clip1 = GES_CONTAINER (ges_test_clip_new ());
  clip2 = GES_CONTAINER (ges_test_clip_new ());

  fail_unless (clip && clip1 && clip2);

  /**
   * Our timeline
   *
   *          0-------
   * layer:   |  clip  |
   *          0-------10
   *
   *                   0--------     0-----------
   * layer1:           |  clip1  |    |     clip2  |
   *                  10--------20   50---------60
   */
  g_object_set (clip, "start", (guint64) 0, "duration", (guint64) 10,
      "in-point", (guint64) 0, NULL);
  g_object_set (clip1, "start", (guint64) 10, "duration", (guint64) 10,
      "in-point", (guint64) 0, NULL);
  g_object_set (clip2, "start", (guint64) 50, "duration", (guint64) 60,
      "in-point", (guint64) 0, NULL);

  fail_unless ((layer = ges_timeline_append_layer (timeline)) != NULL);
  assert_equals_int (ges_layer_get_priority (layer), 0);


  fail_unless (ges_layer_add_clip (layer, GES_CLIP (clip)));
  fail_unless ((trackelements = GES_CONTAINER_CHILDREN (clip)) != NULL);
  fail_unless ((trackelement =
          GES_TRACK_ELEMENT (trackelements->data)) != NULL);
  fail_unless (ges_track_element_get_track (trackelement) == track);
  assert_equals_uint64 (_DURATION (trackelement), 10);

  /* Add a new layer and add clipects to it */
  fail_unless ((layer1 = ges_timeline_append_layer (timeline)) != NULL);
  fail_unless (layer != layer1);
  assert_equals_int (ges_layer_get_priority (layer1), 1);

  fail_unless (ges_layer_add_clip (layer1, GES_CLIP (clip1)));
  fail_unless ((trackelements = GES_CONTAINER_CHILDREN (clip1)) != NULL);
  fail_unless ((trackelement1 =
          GES_TRACK_ELEMENT (trackelements->data)) != NULL);
  fail_unless (ges_track_element_get_track (trackelement1) == track);
  assert_equals_uint64 (_DURATION (trackelement1), 10);

  fail_unless (ges_layer_add_clip (layer1, GES_CLIP (clip2)));
  fail_unless ((trackelements = GES_CONTAINER_CHILDREN (clip2)) != NULL);
  fail_unless ((trackelement2 =
          GES_TRACK_ELEMENT (trackelements->data)) != NULL);
  fail_unless (ges_track_element_get_track (trackelement2) == track);
  assert_equals_uint64 (_DURATION (trackelement2), 60);

  /**
   * Simple rippling clip to: 10
   *
   * New timeline:
   * ------------
   *
   * inpoints 0-------
   *          |  clip  |
   * time    10-------20
   *
   *                   0--------      0-----------
   *                   |  clip1  |     |   clip2    |
   *                  20--------30    60--------120
   */
  fail_unless (ges_container_edit (clip, NULL, -1, GES_EDIT_MODE_RIPPLE,
          GES_EDGE_NONE, 10) == TRUE);
  CHECK_OBJECT_PROPS (trackelement, 10, 0, 10);
  CHECK_OBJECT_PROPS (trackelement1, 20, 0, 10);
  CHECK_OBJECT_PROPS (trackelement2, 60, 0, 60);


  /* FIXME find a way to check that we are using the same MovingContext
   * inside the GESTimeline */
  fail_unless (ges_container_edit (clip1, NULL, 3, GES_EDIT_MODE_RIPPLE,
          GES_EDGE_NONE, 40) == TRUE);
  CHECK_OBJECT_PROPS (trackelement, 10, 0, 10);
  CHECK_OBJECT_PROPS (trackelement1, 40, 0, 10);
  CHECK_OBJECT_PROPS (trackelement2, 80, 0, 60);
  layer2 = ges_clip_get_layer (GES_CLIP (clip1));
  assert_equals_int (ges_layer_get_priority (layer2), 3);
  /* clip2 should have moved layer too */
  fail_unless (ges_clip_get_layer (GES_CLIP (clip2)) == layer2);
  /* We got 2 reference to the same clipect, unref them */
  gst_object_unref (layer2);
  gst_object_unref (layer2);

  /**
   * Rippling clip1 back to: 20 (getting to the exact same timeline as before
   */
  fail_unless (ges_container_edit (clip1, NULL, 1, GES_EDIT_MODE_RIPPLE,
          GES_EDGE_NONE, 20) == TRUE);
  CHECK_OBJECT_PROPS (trackelement, 10, 0, 10);
  CHECK_OBJECT_PROPS (trackelement1, 20, 0, 10);
  CHECK_OBJECT_PROPS (trackelement2, 60, 0, 60);
  layer2 = ges_clip_get_layer (GES_CLIP (clip1));
  assert_equals_int (ges_layer_get_priority (layer2), 1);
  /* clip2 should have moved layer too */
  fail_unless (ges_clip_get_layer (GES_CLIP (clip2)) == layer2);
  /* We got 2 reference to the same clipect, unref them */
  gst_object_unref (layer2);
  gst_object_unref (layer2);

  /**
   * Simple move clip to 27 and clip2 to 35
   *
   * New timeline:
   * ------------
   *
   * inpoints 0-------
   *          |  clip  |
   * time    27-------37
   *
   *                   0--------   0-----------
   *                   |  clip1  |  |   clip2    |
   *                  20--------30 35---------95
   */
  fail_unless (ges_container_edit (clip, NULL, -1, GES_EDIT_MODE_NORMAL,
          GES_EDGE_NONE, 27) == TRUE);
  fail_unless (ges_container_edit (clip2, NULL, -1, GES_EDIT_MODE_NORMAL,
          GES_EDGE_NONE, 35) == TRUE);
  CHECK_OBJECT_PROPS (trackelement, 27, 0, 10);
  CHECK_OBJECT_PROPS (trackelement1, 20, 0, 10);
  CHECK_OBJECT_PROPS (trackelement2, 35, 0, 60);

  /**
   * Simple trimming start clip to: 32
   *
   * New timeline:
   * ------------
   *
   *                      5-------
   * layer 0:             |  clip  |
   *                     32-------37
   *
   *               0--------      0-----------
   * layer 1       |  clip1  |     |   clip2    |
   *              20--------30    35---------95
   */
  fail_unless (ges_container_edit (clip, NULL, -1, GES_EDIT_MODE_TRIM,
          GES_EDGE_START, 32) == TRUE);
  CHECK_OBJECT_PROPS (trackelement, 32, 5, 5);
  CHECK_OBJECT_PROPS (trackelement1, 20, 0, 10);
  CHECK_OBJECT_PROPS (trackelement2, 35, 0, 60);

  /* Ripple end clip to 35 and move to layer 2
   * New timeline:
   * ------------
   *
   *            0--------          0-----------
   * layer 1:   |  clip1  |         |   clip2    |
   *            20--------30       35---------95
   *
   *                        5------
   * layer 2:               |  clip |
   *                       32------35
   */
  fail_unless (ges_container_edit (clip, NULL, 2, GES_EDIT_MODE_RIPPLE,
          GES_EDGE_END, 35) == TRUE);
  CHECK_OBJECT_PROPS (trackelement, 32, 5, 3);
  CHECK_OBJECT_PROPS (trackelement1, 20, 0, 10);
  CHECK_OBJECT_PROPS (trackelement2, 35, 0, 60);
  layer = ges_clip_get_layer (GES_CLIP (clip));
  assert_equals_int (ges_layer_get_priority (layer), 2);
  gst_object_unref (layer);

  /* Roll end clip to 50
   * New timeline:
   * ------------
   *
   *            0--------          0-----------
   * layer 1:   |  clip1  |         |   clip2    |
   *            20--------30       50---------95
   *
   *                        5------
   * layer 2:               |  clip |
   *                       32------50
   */
  fail_unless (ges_container_edit (clip, NULL, 2, GES_EDIT_MODE_ROLL,
          GES_EDGE_END, 50) == TRUE);
  CHECK_OBJECT_PROPS (trackelement, 32, 5, 18);
  CHECK_OBJECT_PROPS (trackelement1, 20, 0, 10);
  CHECK_OBJECT_PROPS (trackelement2, 50, 15, 45);
  layer = ges_clip_get_layer (GES_CLIP (clip));
  assert_equals_int (ges_layer_get_priority (layer), 2);
  gst_object_unref (layer);

  /* Roll end clip back to 35
   * New timeline:
   * ------------
   *
   *            0--------          0-----------
   * layer 1:   |  clip1  |         |   clip2    |
   *            20--------30       35---------95
   *
   *                        5------
   * layer 2:               |  clip |
   *                       32------35
   */
  fail_unless (ges_container_edit (clip, NULL, 2, GES_EDIT_MODE_ROLL,
          GES_EDGE_END, 35) == TRUE);
  CHECK_OBJECT_PROPS (trackelement, 32, 5, 3);
  CHECK_OBJECT_PROPS (trackelement1, 20, 0, 10);
  CHECK_OBJECT_PROPS (trackelement2, 35, 0, 60);
  layer = ges_clip_get_layer (GES_CLIP (clip));
  assert_equals_int (ges_layer_get_priority (layer), 2);
  gst_object_unref (layer);

  /* Ripple clip end to 52
   * New timeline:
   * ------------
   *
   *            0--------          0----------
   * layer 1:   |  clip1  |         |   clip2   |
   *            20-------30       52---------112
   *
   *                        5------
   * layer 2:               |  clip |
   *                       32------52
   *
   */
  /* Can not move to the first layer as clip2 should move to a layer with priority < 0 */
  fail_unless (ges_container_edit (clip, NULL, 0, GES_EDIT_MODE_RIPPLE,
          GES_EDGE_END, 52) == TRUE);
  CHECK_OBJECT_PROPS (trackelement, 32, 5, 20);
  CHECK_OBJECT_PROPS (trackelement1, 20, 0, 10);
  CHECK_OBJECT_PROPS (trackelement2, 52, 0, 60)
      layer = ges_clip_get_layer (GES_CLIP (clip));
  assert_equals_int (ges_layer_get_priority (layer), 2);
  gst_object_unref (layer);


  /* Little check that we have 4 layers in the timeline */
  layers = ges_timeline_get_layers (timeline);
  assert_equals_int (g_list_length (layers), 4);

  /* Some refcount checkings */
  /*  We have a reference to each layer in layers */
  for (tmp = layers; tmp; tmp = tmp->next)
    ASSERT_OBJECT_REFCOUNT (layer, "Layer", 2);
  g_list_free_full (layers, gst_object_unref);

  /* We have 3 references:
   *  track  + timeline  + clip
   */
  ASSERT_OBJECT_REFCOUNT (trackelement, "First trackelement", 4);
  ASSERT_OBJECT_REFCOUNT (trackelement1, "Second trackelement", 4);
  ASSERT_OBJECT_REFCOUNT (trackelement2, "Third trackelement", 4);
  ASSERT_OBJECT_REFCOUNT (clip, "First clip", 2);
  ASSERT_OBJECT_REFCOUNT (clip1, "Second clip", 2);
  ASSERT_OBJECT_REFCOUNT (clip2, "Third clip", 2);

  /* Ripple clip end to 52
   * New timeline:
   * ------------
   *
   *            0--------          0-----------
   * layer 0:   |  clip1  |         |   clip2    |
   *            20-------40       62----------112
   *
   *                        5------
   * layer 1:               |  clip |
   *                       42------60
   *
   */
  fail_unless (ges_container_edit (clip1, NULL, 0, GES_EDIT_MODE_RIPPLE,
          GES_EDGE_END, 40) == TRUE);
  CHECK_OBJECT_PROPS (trackelement, 42, 5, 20);
  CHECK_OBJECT_PROPS (trackelement1, 20, 0, 20);
  CHECK_OBJECT_PROPS (trackelement2, 62, 0, 60);

  /* Check that movement between layer has been done properly */
  layer1 = ges_clip_get_layer (GES_CLIP (clip));
  layer = ges_clip_get_layer (GES_CLIP (clip1));
  assert_equals_int (ges_layer_get_priority (layer1), 1);
  assert_equals_int (ges_layer_get_priority (layer), 0);
  fail_unless (ges_clip_get_layer (GES_CLIP (clip2)) == layer);
  gst_object_unref (layer1);
  /* We have 2 references to @layer that we do not need anymore */ ;
  gst_object_unref (layer);
  gst_object_unref (layer);

  /* Trim clip start to 40
   * New timeline:
   * ------------
   *
   *            0--------          0-----------
   * layer 0:   |  clip1  |         |   clip2    |
   *            20-------40       62---------112
   *
   *                      0------
   * layer 1:             |  clip |
   *                     40------62
   *
   */
  fail_unless (ges_container_edit (clip, NULL, -1, GES_EDIT_MODE_TRIM,
          GES_EDGE_START, 40) == TRUE);
  CHECK_OBJECT_PROPS (trackelement, 40, 3, 22);
  CHECK_OBJECT_PROPS (trackelement1, 20, 0, 20);
  CHECK_OBJECT_PROPS (trackelement2, 62, 0, 60);

  /* Roll clip end to 25
   * New timeline:
   * ------------
   *
   *            0--------          0-----------
   * layer 0:   |  clip1  |         |   clip2    |
   *            20-------25       62---------112
   *
   *                      0------
   * layer 1:             |  clip |
   *                     25------62
   *
   */
  ges_timeline_element_set_inpoint (GES_TIMELINE_ELEMENT (clip), 15);
  fail_unless (ges_container_edit (clip1, NULL, -1, GES_EDIT_MODE_ROLL,
          GES_EDGE_END, 25) == TRUE);
  CHECK_OBJECT_PROPS (trackelement, 25, 0, 37);
  CHECK_OBJECT_PROPS (trackelement1, 20, 0, 5);
  CHECK_OBJECT_PROPS (trackelement2, 62, 0, 60);

  /* Make sure that not doing anything when not able to roll */
  fail_if (ges_container_edit (clip, NULL, -1, GES_EDIT_MODE_ROLL,
          GES_EDGE_START, 65));
  fail_if (ges_container_edit (clip1, NULL, -1, GES_EDIT_MODE_ROLL,
          GES_EDGE_END, 65));
  CHECK_OBJECT_PROPS (trackelement, 25, 0, 37);
  CHECK_OBJECT_PROPS (trackelement1, 20, 0, 5);
  CHECK_OBJECT_PROPS (trackelement2, 62, 0, 60);

  /* Snaping to edge, so no move */
  g_object_set (timeline, "snapping-distance", (guint64) 3, NULL);
  fail_if (ges_container_edit (clip1, NULL, -1, GES_EDIT_MODE_TRIM,
          GES_EDGE_END, 27));
  CHECK_OBJECT_PROPS (trackelement, 25, 0, 37);
  CHECK_OBJECT_PROPS (trackelement1, 20, 0, 5);
  CHECK_OBJECT_PROPS (trackelement2, 62, 0, 60);

  /* Snaping to edge, so no move */
  fail_if (ges_container_edit (clip1, NULL, -1, GES_EDIT_MODE_TRIM,
          GES_EDGE_END, 27));

  CHECK_OBJECT_PROPS (trackelement, 25, 0, 37);
  CHECK_OBJECT_PROPS (trackelement1, 20, 0, 5);
  CHECK_OBJECT_PROPS (trackelement2, 62, 0, 60);

  /**
   * New timeline:
   * ------------
   *                    0----------- 0-------------
   * inpoints   0-------|--   clip  ||   clip2      |
   *            |  clip1 25-|------- 62 -----------122
   * time      20----------30
   */
  g_object_set (timeline, "snapping-distance", (guint64) 0, NULL);
  fail_unless (ges_container_edit (clip1, NULL, -1, GES_EDIT_MODE_TRIM,
          GES_EDGE_END, 30) == TRUE);
  CHECK_OBJECT_PROPS (trackelement, 25, 0, 37);
  CHECK_OBJECT_PROPS (trackelement1, 20, 0, 10);
  CHECK_OBJECT_PROPS (trackelement2, 62, 0, 60);

  /**
   * New timeline
   * ------------
   *                    0----------
   *                    |   clip   |
   *                    25---------62
   * inpoints   0----------------------- 10--------
   *            |       clip1           ||  clip2  |
   * time      20---------------------- 72 --------122
   */
  /* Rolling involves only neighbours that are currently snapping */
  fail_unless (ges_container_edit (clip1, NULL, -1, GES_EDIT_MODE_ROLL,
          GES_EDGE_END, 62) == TRUE);
  fail_unless (ges_container_edit (clip1, NULL, -1, GES_EDIT_MODE_ROLL,
          GES_EDGE_END, 72) == TRUE);
  CHECK_OBJECT_PROPS (trackelement, 25, 0, 37);
  CHECK_OBJECT_PROPS (trackelement1, 20, 0, 52);
  CHECK_OBJECT_PROPS (trackelement2, 72, 10, 50);

  /* Test Snapping */
  /**
   *                    0----------
   *                    |   clip   |
   *                    25---------62
   * inpoints           5--------------- 10--------
   *                    |     clip1     ||  clip2  |
   * time               25------------- 72 --------122
   */
  g_object_set (timeline, "snapping-distance", (guint64) 4, NULL);
  fail_unless (ges_container_edit (clip1, NULL, -1, GES_EDIT_MODE_TRIM,
          GES_EDGE_START, 28) == TRUE);
  CHECK_OBJECT_PROPS (trackelement, 25, 0, 37);
  CHECK_OBJECT_PROPS (trackelement1, 25, 5, 47);
  CHECK_OBJECT_PROPS (trackelement2, 72, 10, 50);

  /**
   *                    0----------
   *                    |   clip   |
   *                    25---------62
   * inpoints           5---------- 0---------
   *                    |  clip1   ||  clip2  |
   * time               25-------- 62 --------122
   */
  fail_unless (ges_container_edit (clip2, NULL, -1, GES_EDIT_MODE_ROLL,
          GES_EDGE_START, 59) == TRUE);
  CHECK_OBJECT_PROPS (trackelement, 25, 0, 37);
  CHECK_OBJECT_PROPS (trackelement1, 25, 5, 37);
  CHECK_OBJECT_PROPS (trackelement2, 62, 0, 60);

}

GST_END_TEST;

GST_START_TEST (test_groups)
{
  GESAsset *asset;
  GESTimeline *timeline;
  GESGroup *group;
  GESLayer *layer, *layer1, *layer2, *layer3;
  GESClip *c, *c1, *c2, *c3, *c4, *c5;

  GList *clips = NULL;

  timeline = ges_timeline_new_audio_video ();

  /* Our timeline
   *
   *    --0------------10-Group-----20---------------30-----------------------70
   *      | +-----------+                             |+-----------50         |
   * L    | |    C      |                             ||     C3    |          |
   *      | +-----------+                             |+-----------+          |
   *    --|-------------------------------------------|-----40----------------|
   *      |            +------------+ +-------------+ |      +--------60      |
   * L1   |            |     C1     | |     C2      | |      |     C4 |       |
   *      |            +------------+ +-------------+ |      +--------+       |
   *    --|-------------------------------------------|-----------------------|
   *      |                                           |             +--------+|
   * L2   |                                           |             |  c5    ||
   *      |                                           |             +--------+|
   *    --+-------------------------------------------+-----------------------+
   *
   * L3
   *
   *    -----------------------------------------------------------------------
   */

  layer = ges_timeline_append_layer (timeline);
  layer1 = ges_timeline_append_layer (timeline);
  layer2 = ges_timeline_append_layer (timeline);
  layer3 = ges_timeline_append_layer (timeline);
  assert_equals_int (ges_layer_get_priority (layer3), 3);
  asset = ges_asset_request (GES_TYPE_TEST_CLIP, NULL, NULL);

  c = ges_layer_add_asset (layer, asset, 0, 0, 10, GES_TRACK_TYPE_UNKNOWN);
  c1 = ges_layer_add_asset (layer1, asset, 10, 0, 10, GES_TRACK_TYPE_UNKNOWN);
  c2 = ges_layer_add_asset (layer1, asset, 20, 0, 10, GES_TRACK_TYPE_UNKNOWN);
  clips = g_list_prepend (clips, c);
  clips = g_list_prepend (clips, c1);
  clips = g_list_prepend (clips, c2);
  group = GES_GROUP (ges_container_group (clips));
  fail_unless (GES_TIMELINE_ELEMENT_TIMELINE (group) == timeline);
  g_list_free (clips);

  fail_unless (GES_IS_GROUP (group));
  CHECK_OBJECT_PROPS (c, 0, 0, 10);
  CHECK_OBJECT_PROPS (c1, 10, 0, 10);
  CHECK_OBJECT_PROPS (c2, 20, 0, 10);
  CHECK_OBJECT_PROPS (group, 0, 0, 30);

  c3 = ges_layer_add_asset (layer, asset, 30, 0, 20, GES_TRACK_TYPE_UNKNOWN);
  c4 = ges_layer_add_asset (layer1, asset, 40, 0, 20, GES_TRACK_TYPE_UNKNOWN);
  c5 = ges_layer_add_asset (layer2, asset, 50, 0, 20, GES_TRACK_TYPE_UNKNOWN);

  CHECK_OBJECT_PROPS (c3, 30, 0, 20);
  CHECK_OBJECT_PROPS (c4, 40, 0, 20);
  CHECK_OBJECT_PROPS (c5, 50, 0, 20);
  check_layer (c, 0);
  check_layer (c1, 1);
  check_layer (c2, 1);
  check_layer (c3, 0);
  check_layer (c4, 1);
  check_layer (c5, 2);

  fail_unless (ges_container_edit (GES_CONTAINER (c), NULL, -1,
          GES_EDIT_MODE_RIPPLE, GES_EDGE_NONE, 10) == TRUE);

  CHECK_OBJECT_PROPS (c, 10, 0, 10);
  CHECK_OBJECT_PROPS (c1, 20, 0, 10);
  CHECK_OBJECT_PROPS (c2, 30, 0, 10);
  CHECK_OBJECT_PROPS (c3, 40, 0, 20);
  CHECK_OBJECT_PROPS (c4, 50, 0, 20);
  CHECK_OBJECT_PROPS (c5, 60, 0, 20);
  check_layer (c, 0);
  check_layer (c1, 1);
  check_layer (c2, 1);
  check_layer (c3, 0);
  check_layer (c4, 1);
  check_layer (c5, 2);

  fail_unless (ges_container_edit (GES_CONTAINER (c), NULL, 1,
          GES_EDIT_MODE_RIPPLE, GES_EDGE_NONE, 10) == TRUE);
  CHECK_OBJECT_PROPS (c, 10, 0, 10);
  CHECK_OBJECT_PROPS (c1, 20, 0, 10);
  CHECK_OBJECT_PROPS (c2, 30, 0, 10);
  CHECK_OBJECT_PROPS (c3, 40, 0, 20);
  CHECK_OBJECT_PROPS (c4, 50, 0, 20);
  CHECK_OBJECT_PROPS (c5, 60, 0, 20);
  check_layer (c, 1);
  check_layer (c1, 2);
  check_layer (c2, 2);
  check_layer (c3, 1);
  check_layer (c4, 2);
  check_layer (c5, 3);

  fail_unless (ges_container_edit (GES_CONTAINER (c1), NULL, 2,
          GES_EDIT_MODE_RIPPLE, GES_EDGE_END, 40) == TRUE);
  CHECK_OBJECT_PROPS (c, 10, 0, 10);
  CHECK_OBJECT_PROPS (c1, 20, 0, 20);
  CHECK_OBJECT_PROPS (c2, 40, 0, 10);
  CHECK_OBJECT_PROPS (c3, 50, 0, 20);
  CHECK_OBJECT_PROPS (c4, 60, 0, 20);
  CHECK_OBJECT_PROPS (c5, 70, 0, 20);
  check_layer (c, 1);
  check_layer (c1, 2);
  check_layer (c2, 2);
  check_layer (c3, 1);
  check_layer (c4, 2);
  check_layer (c5, 3);

  fail_unless (ges_container_edit (GES_CONTAINER (c1), NULL, 2,
          GES_EDIT_MODE_RIPPLE, GES_EDGE_END, 30) == TRUE);
  CHECK_OBJECT_PROPS (c, 10, 0, 10);
  CHECK_OBJECT_PROPS (c1, 20, 0, 10);
  CHECK_OBJECT_PROPS (c2, 30, 0, 10);
  CHECK_OBJECT_PROPS (c3, 40, 0, 20);
  CHECK_OBJECT_PROPS (c4, 50, 0, 20);
  CHECK_OBJECT_PROPS (c5, 60, 0, 20);
  check_layer (c, 1);
  check_layer (c1, 2);
  check_layer (c2, 2);
  check_layer (c3, 1);
  check_layer (c4, 2);
  check_layer (c5, 3);

  fail_unless (ges_container_edit (GES_CONTAINER (c), NULL, 0,
          GES_EDIT_MODE_RIPPLE, GES_EDGE_NONE, 0) == TRUE);
  CHECK_OBJECT_PROPS (c, 0, 0, 10);
  CHECK_OBJECT_PROPS (c1, 10, 0, 10);
  CHECK_OBJECT_PROPS (c2, 20, 0, 10);
  CHECK_OBJECT_PROPS (c3, 30, 0, 20);
  CHECK_OBJECT_PROPS (c4, 40, 0, 20);
  CHECK_OBJECT_PROPS (c5, 50, 0, 20);
  check_layer (c, 0);
  check_layer (c1, 1);
  check_layer (c2, 1);
  check_layer (c3, 0);
  check_layer (c4, 1);
  check_layer (c5, 2);

  fail_unless (ges_container_edit (GES_CONTAINER (c2), NULL, -1,
          GES_EDIT_MODE_ROLL, GES_EDGE_END, 40) == TRUE);
  CHECK_OBJECT_PROPS (c, 0, 0, 10);
  CHECK_OBJECT_PROPS (c1, 10, 0, 10);
  CHECK_OBJECT_PROPS (c2, 20, 0, 20);
  CHECK_OBJECT_PROPS (c3, 40, 10, 10);
  CHECK_OBJECT_PROPS (c4, 40, 0, 20);
  CHECK_OBJECT_PROPS (c5, 50, 0, 20);
  CHECK_OBJECT_PROPS (group, 0, 0, 40);
  check_layer (c, 0);
  check_layer (c1, 1);
  check_layer (c2, 1);
  check_layer (c3, 0);
  check_layer (c4, 1);
  check_layer (c5, 2);

  fail_unless (ges_container_edit (GES_CONTAINER (c), NULL, 0,
          GES_EDIT_MODE_TRIM, GES_EDGE_START, 5) == TRUE);
  CHECK_OBJECT_PROPS (c, 5, 5, 5);
  CHECK_OBJECT_PROPS (c1, 10, 0, 10);
  CHECK_OBJECT_PROPS (c2, 20, 0, 20);
  CHECK_OBJECT_PROPS (c3, 40, 10, 10);
  CHECK_OBJECT_PROPS (c4, 40, 0, 20);
  CHECK_OBJECT_PROPS (c5, 50, 0, 20);
  CHECK_OBJECT_PROPS (group, 5, 0, 35);
  check_layer (c, 0);
  check_layer (c1, 1);
  check_layer (c2, 1);
  check_layer (c3, 0);
  check_layer (c4, 1);
  check_layer (c5, 2);

  gst_object_unref (timeline);
  gst_object_unref (asset);
}

GST_END_TEST;

GST_START_TEST (test_snapping_groups)
{
  GESAsset *asset;
  GESTimeline *timeline;
  GESGroup *group;
  GESLayer *layer, *layer1, *layer2, *layer3;
  GESClip *c, *c1, *c2, *c3, *c4, *c5;

  GList *clips = NULL;

  timeline = ges_timeline_new_audio_video ();
  g_object_set (timeline, "snapping-distance", (guint64) 3, NULL);

  /* Our timeline
   *
   *    --0------------10-Group-----20---------25-----30----------------------70
   *      | +-----------+                      |       +-----------50         |
   * L    | |    C      |                      |       |     C3    |          |
   *      | +-----------+                      |       +-----------+          |
   *    --|------------------------------------|------------40----------------|
   *      |            +------------+ +--------+             +--------60      |
   * L1   |            |     C1     | |     C2 |             |     C4 |       |
   *      |            +------------+ +--------+             +--------+       |
   *    --|------------------------------------+------------------------------|
   *      |                                                         +--------+|
   * L2   |                                                         |  c5    ||
   *      |                                                         +--------+|
   *    --+-------------------------------------------------------------------+
   *
   * L3
   *
   *    -----------------------------------------------------------------------
   */

  layer = ges_timeline_append_layer (timeline);
  layer1 = ges_timeline_append_layer (timeline);
  layer2 = ges_timeline_append_layer (timeline);
  layer3 = ges_timeline_append_layer (timeline);
  assert_equals_int (ges_layer_get_priority (layer3), 3);
  asset = ges_asset_request (GES_TYPE_TEST_CLIP, NULL, NULL);

  c = ges_layer_add_asset (layer, asset, 0, 0, 10, GES_TRACK_TYPE_UNKNOWN);
  c1 = ges_layer_add_asset (layer1, asset, 10, 0, 10, GES_TRACK_TYPE_UNKNOWN);
  c2 = ges_layer_add_asset (layer1, asset, 20, 0, 5, GES_TRACK_TYPE_UNKNOWN);
  clips = g_list_prepend (clips, c);
  clips = g_list_prepend (clips, c1);
  clips = g_list_prepend (clips, c2);
  group = GES_GROUP (ges_container_group (clips));
  fail_unless (GES_TIMELINE_ELEMENT_TIMELINE (group) == timeline);
  g_list_free (clips);

  fail_unless (GES_IS_GROUP (group));
  CHECK_OBJECT_PROPS (c, 0, 0, 10);
  CHECK_OBJECT_PROPS (c1, 10, 0, 10);
  CHECK_OBJECT_PROPS (c2, 20, 0, 5);
  CHECK_OBJECT_PROPS (group, 0, 0, 25);

  c3 = ges_layer_add_asset (layer, asset, 30, 0, 20, GES_TRACK_TYPE_UNKNOWN);
  c4 = ges_layer_add_asset (layer1, asset, 40, 0, 20, GES_TRACK_TYPE_UNKNOWN);
  c5 = ges_layer_add_asset (layer2, asset, 50, 0, 20, GES_TRACK_TYPE_UNKNOWN);

  CHECK_OBJECT_PROPS (c3, 30, 0, 20);
  CHECK_OBJECT_PROPS (c4, 40, 0, 20);
  CHECK_OBJECT_PROPS (c5, 50, 0, 20);
  check_layer (c, 0);
  check_layer (c1, 1);
  check_layer (c2, 1);
  check_layer (c3, 0);
  check_layer (c4, 1);
  check_layer (c5, 2);

  /* c2 should snap with C3 and make the group moving to 5 */
  fail_unless (ges_container_edit (GES_CONTAINER (c), NULL, -1,
          GES_EDIT_MODE_NORMAL, GES_EDGE_NONE, 3) == TRUE);

  DEEP_CHECK (c, 5, 0, 10);
  DEEP_CHECK (c1, 15, 0, 10);
  DEEP_CHECK (c2, 25, 0, 5);
  DEEP_CHECK (c2, 25, 0, 5);
  DEEP_CHECK (c4, 40, 0, 20);
  DEEP_CHECK (c5, 50, 0, 20);
  CHECK_OBJECT_PROPS (group, 5, 0, 25);
  check_layer (c, 0);
  check_layer (c1, 1);
  check_layer (c2, 1);
  check_layer (c3, 0);
  check_layer (c4, 1);
  check_layer (c5, 2);


  gst_object_unref (timeline);
  gst_object_unref (asset);
}

GST_END_TEST;

static void
_set_track_element_width_height (GESTrackElement * trksrc, gint wvalue,
    gint hvalue)
{
  GValue width = { 0 };
  GValue height = { 0 };

  g_value_init (&width, G_TYPE_INT);
  g_value_init (&height, G_TYPE_INT);
  g_value_set_int (&width, wvalue);
  g_value_set_int (&height, hvalue);
  if (wvalue >= 0)
    ges_timeline_element_set_child_property (GES_TIMELINE_ELEMENT (trksrc),
        "width", &width);
  if (hvalue >= 0)
    ges_timeline_element_set_child_property (GES_TIMELINE_ELEMENT (trksrc),
        "height", &height);
}

static gboolean
check_frame_positioner_size (GESClip * clip, gint width, gint height)
{
  GESTrackElement *trksrc;
  GValue val_width = { 0 };
  GValue val_height = { 0 };
  gint real_width, real_height;

  trksrc = GES_CONTAINER_CHILDREN (clip)->data;
  if (!GES_IS_VIDEO_SOURCE (trksrc))
    return FALSE;

  g_value_init (&val_width, G_TYPE_INT);
  g_value_init (&val_height, G_TYPE_INT);

  ges_timeline_element_get_child_property (GES_TIMELINE_ELEMENT (trksrc),
      "width", &val_width);
  ges_timeline_element_get_child_property (GES_TIMELINE_ELEMENT (trksrc),
      "height", &val_height);

  real_width = g_value_get_int (&val_width);
  real_height = g_value_get_int (&val_height);

  assert_equals_int (real_width, width);
  assert_equals_int (real_height, height);

  return (width == real_width && height == real_height);
}

GST_START_TEST (test_scaling)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESAsset *asset1, *asset2;
  GESClip *clip;
  GESTrack *trackv = GES_TRACK (ges_video_track_new ());
  GstCaps *caps =
      gst_caps_new_simple ("video/x-raw", "width", G_TYPE_INT, 1200, "height",
      G_TYPE_INT, 1000, NULL);

  timeline = ges_timeline_new ();
  ges_timeline_add_track (timeline, trackv);
  layer = ges_layer_new ();
  fail_unless (ges_timeline_add_layer (timeline, layer));

  g_object_set (layer, "auto-transition", TRUE, NULL);

  asset1 = GES_ASSET (ges_asset_request (GES_TYPE_TEST_CLIP, NULL, NULL));
  asset2 = GES_ASSET (ges_asset_request (GES_TYPE_TEST_CLIP, NULL, NULL));

  fail_unless (asset1 != NULL && asset2 != NULL);

  GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (timeline),
      GST_DEBUG_GRAPH_SHOW_ALL, "ges-integration-timeline");

  ges_track_set_restriction_caps (trackv, caps);
  gst_caps_unref (caps);

  GST_DEBUG ("adding clip, should be 1200 x 1000");
  clip =
      ges_layer_add_asset (layer, GES_ASSET (asset1), 0 * GST_SECOND,
      0 * GST_SECOND, 4 * GST_SECOND, GES_TRACK_TYPE_UNKNOWN);
  gst_object_unref (asset1);
  g_object_set (clip, "vpattern", (gint) GES_VIDEO_TEST_PATTERN_SMPTE75, NULL);

  /**
   * Our track: 1200 x 1000
   *
   * 0--------------0
   * | width : 1200 |
   * | height: 1000 |
   * 0--------------2
   */

  /* clip takes the size set on the track as a default */
  fail_unless (check_frame_positioner_size (clip, 1200, 1000));

  if (GES_IS_VIDEO_SOURCE (GES_CONTAINER_CHILDREN (clip)->data))
    _set_track_element_width_height (GES_CONTAINER_CHILDREN (clip)->data, 1024,
        768);

  GST_DEBUG ("Setting clip size, should be 1024 x 768");

  /**
   * Our timeline : 1200 x 1000
   *
   * 0--------------0
   * | width : 1024 |
   * | height: 768  |
   * 0--------------2
   */

  /* Clip has to comply to direct orders */
  fail_unless (check_frame_positioner_size (clip, 1024, 768));

  GST_DEBUG ("Changing caps, should still be 1024 x 768");

  caps =
      gst_caps_new_simple ("video/x-raw", "width", G_TYPE_INT, 1400, "height",
      G_TYPE_INT, 1200, NULL);
  ges_track_set_restriction_caps (trackv, caps);
  gst_caps_unref (caps);

  /**
   * Our timeline : 1400 x 1200
   *
   * 0--------------0
   * | width : 1024 |
   * | height: 768  |
   * 0--------------2
   */

  /* Clip still has to be the same size */
  fail_unless (check_frame_positioner_size (clip, 1024, 768));

  GST_DEBUG ("Setting width to 0, should be 1400 x 768");

  /* -1 means don't set it, only valid here */
  if (GES_IS_VIDEO_SOURCE (GES_CONTAINER_CHILDREN (clip)->data))
    _set_track_element_width_height (GES_CONTAINER_CHILDREN (clip)->data, 0,
        -1);

  /**
   * Our timeline : 1400 x 1200
   *
   * 0--------------0
   * | width : 1400 |
   * | height: 768  |
   * 0--------------2
   */

  /* Clip width was set to 0 so it has to use track width */
  /* Clip height is still directly set by the user */
  fail_unless (check_frame_positioner_size (clip, 1400, 768));

  GST_DEBUG ("Setting height to 0, should be 1400 x 1200");

  if (GES_IS_VIDEO_SOURCE (GES_CONTAINER_CHILDREN (clip)->data))
    _set_track_element_width_height (GES_CONTAINER_CHILDREN (clip)->data, -1,
        0);

  /**
   * Our timeline : 1400 x 1200
   *
   * 0--------------0
   * | width : 1400 |
   * | height: 1200 |
   * 0--------------2
   */

  /* Clip width still has to use track width */
  /* Clip height was set to 0 so it has to use track height */
  fail_unless (check_frame_positioner_size (clip, 1400, 1200));

  GST_DEBUG ("Removing restriction on track height, should be 1400 x 240");

  caps =
      gst_caps_new_simple ("video/x-raw", "width", G_TYPE_INT, 1400, "height",
      G_TYPE_INT, 0, NULL);
  ges_track_set_restriction_caps (trackv, caps);
  gst_caps_unref (caps);

  /**
   * Our timeline : 1400 x no restriction
   *
   * 0--------------0
   * | width : 1400 |
   * | height: 240  |
   * 0--------------2
   */

  /* Clip width still has to use track width */
  /* Clip height was set to 0 so it has to use natural clip height */
  fail_unless (check_frame_positioner_size (clip, 1400, 0));

  GST_DEBUG ("Removing restriction on track width, should be 320 x 240");

  caps = gst_caps_new_empty_simple ("video/x-raw");
  ges_track_set_restriction_caps (trackv, caps);
  gst_caps_unref (caps);

  /**
   * Our timeline : no restriction x no restriction
   *
   * 0--------------0
   * | width : 320  |
   * | height: 240  |
   * 0--------------2
   */

  /* Clip width was set to 0 so it has to use natural clip width */
  /* Clip height was set to 0 so it has to use natural clip height */
  fail_unless (check_frame_positioner_size (clip, 0, 0));


  /**
   * Our timeline : 320 * 240
   *
   * 0--------------0
   * | width : 320  |
   * | height: 240  |
   * 0--------------2
   */

  /* We set the restriction caps video size to the same as the video source
   * size. */
  caps = gst_caps_from_string ("video/x-raw,height=240,width=320");
  ges_track_set_restriction_caps (trackv, caps);
  gst_caps_unref (caps);
  _set_track_element_width_height (GES_CONTAINER_CHILDREN (clip)->data, 320,
      240);

  /* The video source has the same size as the track restriction caps but we
   * are changing the aspect ratio, the video should thus not be rescaled. */
  caps = gst_caps_from_string ("video/x-raw,height=1080,width=1920");
  ges_track_set_restriction_caps (trackv, caps);
  fail_unless (check_frame_positioner_size (clip, 320, 240));

  gst_object_unref (timeline);
}

GST_END_TEST;

static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges-timeline-edition");
  TCase *tc_chain = tcase_create ("timeline-edition");

  ges_init ();

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_basic_timeline_edition);
  tcase_add_test (tc_chain, test_snapping);
  tcase_add_test (tc_chain, test_timeline_edition_mode);
  tcase_add_test (tc_chain, test_simple_triming);
  tcase_add_test (tc_chain, test_groups);
  tcase_add_test (tc_chain, test_snapping_groups);
  tcase_add_test (tc_chain, test_scaling);

  return s;
}

GST_CHECK_MAIN (ges);
