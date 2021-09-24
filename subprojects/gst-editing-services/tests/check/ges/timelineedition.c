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
  CHECK_OBJECT_PROPS (element, start, inpoint, duration)                      \
                                                                               \
  track_elements = GES_CONTAINER_CHILDREN (element);                           \
  for (tmp = track_elements; tmp; tmp = tmp->next) {                           \
    CHECK_OBJECT_PROPS (tmp->data, start, inpoint, duration)                      \
  }                                                                            \
}

#define CHECK_CLIP(element, start, inpoint, duration, layer_prio) \
{ \
  DEEP_CHECK(element, start, inpoint, duration);\
  check_layer (element, layer_prio); \
}\


GST_START_TEST (test_basic_timeline_edition)
{
  GESAsset *asset;
  GESTrack *track;
  GESTimeline *timeline;
  GESLayer *layer;
  GESTrackElement *trackelement, *trackelement1, *trackelement2;
  GESContainer *clip, *clip1, *clip2;

  ges_init ();

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
  g_object_unref (asset);
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

  ges_deinit ();
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

  ges_init ();

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

  ASSERT_OBJECT_REFCOUNT (trackelement, "track + timeline + clip", 3);
  ASSERT_OBJECT_REFCOUNT (clip, "layer + timeline", 2);

  fail_unless (ges_layer_add_clip (layer, GES_CLIP (clip1)));
  fail_unless ((trackelements = GES_CONTAINER_CHILDREN (clip1)) != NULL);
  fail_unless ((trackelement1 =
          GES_TRACK_ELEMENT (trackelements->data)) != NULL);
  fail_unless (ges_track_element_get_track (trackelement1) == track);
  assert_equals_uint64 (_DURATION (trackelement1), 15);

  /* Same ref logic */
  ASSERT_OBJECT_REFCOUNT (trackelement1, "First trackelement", 3);
  ASSERT_OBJECT_REFCOUNT (clip1, "First clip", 2);

  fail_unless (ges_layer_add_clip (layer, GES_CLIP (clip2)));
  fail_unless ((trackelements = GES_CONTAINER_CHILDREN (clip2)) != NULL);
  fail_unless ((trackelement2 =
          GES_TRACK_ELEMENT (trackelements->data)) != NULL);
  fail_unless (ges_track_element_get_track (trackelement2) == track);
  assert_equals_uint64 (_DURATION (trackelement2), 60);

  /* Same ref logic */
  ASSERT_OBJECT_REFCOUNT (trackelement2, "First trackelement", 3);
  ASSERT_OBJECT_REFCOUNT (clip2, "First clip", 2);

  /* Snaping to edge, so no move */
  g_object_set (timeline, "snapping-distance", (guint64) 3, NULL);
  fail_unless (ges_container_edit (clip1, NULL, -1, GES_EDIT_MODE_TRIM,
          GES_EDGE_END, 27) == TRUE);
  CHECK_OBJECT_PROPS (trackelement, 25, 0, 37);
  CHECK_OBJECT_PROPS (trackelement1, 20, 0, 5);
  CHECK_OBJECT_PROPS (trackelement2, 62, 0, 60);

  /* Snaping to edge, so no move */
  ges_container_edit (clip1, NULL, -1, GES_EDIT_MODE_TRIM, GES_EDGE_END, 27);
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
  DEEP_CHECK (clip, 25, 0, 37);
  DEEP_CHECK (clip1, 20, 0, 10);
  DEEP_CHECK (clip2, 62, 0, 60);

  /* clip and clip1 would fully overlap ... forbiden */
  fail_if (ges_timeline_element_roll_end (GES_TIMELINE_ELEMENT (clip1), 62));
  DEEP_CHECK (clip, 25, 0, 37);
  DEEP_CHECK (clip1, 20, 0, 10);
  DEEP_CHECK (clip2, 62, 0, 60);
  fail_if (ges_timeline_element_roll_end (GES_TIMELINE_ELEMENT (clip1),
          72) == TRUE);
  DEEP_CHECK (clip, 25, 0, 37);
  DEEP_CHECK (clip1, 20, 0, 10);
  DEEP_CHECK (clip2, 62, 0, 60);

  /**
   *                        30-------+0-------------+
   * inpoints   0-----------5  clip  ||  clip2      |
   *            |  clip1    |------- 62 -----------122
   * time      20----------30
   */
  g_object_set (timeline, "snapping-distance", (guint64) 4, NULL);
  fail_unless (ges_timeline_element_trim (GES_TIMELINE_ELEMENT (clip),
          28) == TRUE);
  DEEP_CHECK (clip, 30, 5, 32);
  DEEP_CHECK (clip1, 20, 0, 10);
  DEEP_CHECK (clip2, 62, 0, 60);

  /**
   *                        30-------+0-------------+
   * inpoints   0-----------5  clip  ||  clip2      |
   *            |  clip1    |------- 62 -----------122
   * time      20----------30
   */
  fail_unless (ges_timeline_element_set_inpoint (GES_TIMELINE_ELEMENT (clip2),
          5));
  DEEP_CHECK (clip, 30, 5, 32);
  DEEP_CHECK (clip1, 20, 0, 10);
  DEEP_CHECK (clip2, 62, 5, 60);

  /**
   *                        30-------+0-------------+
   * inpoints   0-----------5  clip  ||  clip2      |
   *            |  clip1    |------- 62 -----------122
   * time      20----------30
   */
  /* Moving clip1 to 26 would lead to snapping to 30, and clip1 and clip
   * would fully overlap */
  fail_if (ges_container_edit (clip1, NULL, -1, GES_EDIT_MODE_NORMAL,
          GES_EDGE_NONE, 26) == TRUE);
  DEEP_CHECK (clip, 30, 5, 32);
  DEEP_CHECK (clip1, 20, 0, 10);
  DEEP_CHECK (clip2, 62, 5, 60);

   /**
   *                        30-------+0-------------+
   * inpoints               5  clip  ||  clip2      |-------------+
   *                        +------- 62 -----------122  clip1     |
   * time                                           +------------132
   * Check that clip1 snaps with the end of clip2 */
  fail_unless (ges_container_edit (clip1, NULL, -1, GES_EDIT_MODE_NORMAL,
          GES_EDGE_NONE, 125) == TRUE);
  DEEP_CHECK (clip, 30, 5, 32);
  DEEP_CHECK (clip1, 122, 0, 10);
  DEEP_CHECK (clip2, 62, 5, 60);

  /* Check we didn't lose/screwed any references */
  ASSERT_OBJECT_REFCOUNT (trackelement, "First trackelement", 3);
  ASSERT_OBJECT_REFCOUNT (trackelement1, "Second trackelement", 3);
  ASSERT_OBJECT_REFCOUNT (trackelement2, "Third trackelement", 3);
  ASSERT_OBJECT_REFCOUNT (clip, "First clip", 2);
  ASSERT_OBJECT_REFCOUNT (clip1, "Second clip", 2);
  ASSERT_OBJECT_REFCOUNT (clip2, "Third clip", 2);

  check_destroyed (G_OBJECT (timeline), G_OBJECT (trackelement),
      trackelement1, trackelement2, clip, clip1, clip2, layer, NULL);

  ges_deinit ();
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
  GList *assets, *tmp;
  GMainLoop *mainloop;
  GESClipAsset *asset;
  GESProject *project;
  GESTimeline *timeline;
  GESLayer *layer;
  GESTimelineElement *element;
  gchar *uri;

  ges_init ();

  uri = ges_test_file_uri ("audio_video.ogg");

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
  g_list_free_full (assets, g_object_unref);

  tmp = ges_layer_get_clips (layer);
  element = tmp->data;

  DEEP_CHECK (element, 0, 0, 10);
  ges_container_edit (GES_CONTAINER (element), NULL, -1, GES_EDIT_MODE_TRIM,
      GES_EDGE_START, 5);
  DEEP_CHECK (element, 5, 5, 5);
  g_list_free_full (tmp, g_object_unref);

  g_main_loop_unref (mainloop);
  gst_object_unref (timeline);
  gst_object_unref (project);

  ges_deinit ();
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

  ges_init ();

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
  assert_equals_int (GES_TIMELINE_ELEMENT_LAYER_PRIORITY (clip), 2);

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

  /* Roll end clip back to 35 */
  /* Can not move to the first layer as clip2 should move to a layer with priority < 0 */
  fail_if (ges_container_edit (clip, NULL, 0, GES_EDIT_MODE_RIPPLE,
          GES_EDGE_END, 52));
  CHECK_OBJECT_PROPS (trackelement, 32, 5, 3);
  CHECK_OBJECT_PROPS (trackelement1, 20, 0, 10);
  CHECK_OBJECT_PROPS (trackelement2, 35, 0, 60);
  assert_equals_int (GES_TIMELINE_ELEMENT_LAYER_PRIORITY (clip), 2);

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
  fail_unless (ges_container_edit (clip, NULL, -1, GES_EDIT_MODE_RIPPLE,
          GES_EDGE_END, 52) == TRUE);
  CHECK_OBJECT_PROPS (trackelement, 32, 5, 20);
  CHECK_OBJECT_PROPS (trackelement1, 20, 0, 10);
  CHECK_OBJECT_PROPS (trackelement2, 52, 0, 60);
  assert_equals_int (GES_TIMELINE_ELEMENT_LAYER_PRIORITY (clip), 2);


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
  ASSERT_OBJECT_REFCOUNT (trackelement, "First trackelement", 3);
  ASSERT_OBJECT_REFCOUNT (trackelement1, "Second trackelement", 3);
  ASSERT_OBJECT_REFCOUNT (trackelement2, "Third trackelement", 3);
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
  ges_container_edit (clip1, NULL, -1, GES_EDIT_MODE_TRIM, GES_EDGE_END, 27);
  CHECK_OBJECT_PROPS (trackelement, 25, 0, 37);
  CHECK_OBJECT_PROPS (trackelement1, 20, 0, 5);
  CHECK_OBJECT_PROPS (trackelement2, 62, 0, 60);

  /* Snaping to edge, so no move */
  ges_container_edit (clip1, NULL, -1, GES_EDIT_MODE_TRIM, GES_EDGE_END, 27);
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
   * -------------------------------------------------
   * inpoints   0----------------------- 10--------
   *            |       clip1           ||  clip2  |
   * time      20---------------------- 72 --------122
   */
  /* Rolling involves only neighbours that are currently snapping */
  ges_container_edit (clip1, NULL, -1, GES_EDIT_MODE_ROLL, GES_EDGE_END, 62);
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

  ges_deinit ();
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

  ges_init ();

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
  CHECK_CLIP (c, 0, 0, 10, 0);
  CHECK_CLIP (c1, 10, 0, 10, 1);
  CHECK_CLIP (c2, 20, 0, 10, 1);
  CHECK_OBJECT_PROPS (group, 0, 0, 30);

  c3 = ges_layer_add_asset (layer, asset, 30, 0, 20, GES_TRACK_TYPE_UNKNOWN);
  c4 = ges_layer_add_asset (layer1, asset, 40, 0, 20, GES_TRACK_TYPE_UNKNOWN);
  c5 = ges_layer_add_asset (layer2, asset, 50, 0, 20, GES_TRACK_TYPE_UNKNOWN);

  CHECK_CLIP (c3, 30, 0, 20, 0);
  CHECK_CLIP (c4, 40, 0, 20, 1);
  CHECK_CLIP (c5, 50, 0, 20, 2);
  check_layer (c, 0);
  check_layer (c1, 1);
  check_layer (c2, 1);

  fail_unless (ges_container_edit (GES_CONTAINER (c), NULL, -1,
          GES_EDIT_MODE_RIPPLE, GES_EDGE_NONE, 10) == TRUE);

  CHECK_CLIP (c, 10, 0, 10, 0);
  CHECK_CLIP (c1, 20, 0, 10, 1);
  CHECK_CLIP (c2, 30, 0, 10, 1);
  CHECK_CLIP (c3, 40, 0, 20, 0);
  CHECK_CLIP (c4, 50, 0, 20, 1);
  CHECK_CLIP (c5, 60, 0, 20, 2);

  fail_unless (ges_container_edit (GES_CONTAINER (c), NULL, 1,
          GES_EDIT_MODE_RIPPLE, GES_EDGE_NONE, 10) == TRUE);
  CHECK_CLIP (c, 10, 0, 10, 1);
  CHECK_CLIP (c1, 20, 0, 10, 2);
  CHECK_CLIP (c2, 30, 0, 10, 2);
  CHECK_CLIP (c3, 40, 0, 20, 1);
  CHECK_CLIP (c4, 50, 0, 20, 2);
  CHECK_CLIP (c5, 60, 0, 20, 3);

  fail_if (ges_container_edit (GES_CONTAINER (c1), NULL, 2,
          GES_EDIT_MODE_RIPPLE, GES_EDGE_END, 40) == TRUE);
  CHECK_CLIP (c, 10, 0, 10, 1);
  CHECK_CLIP (c1, 20, 0, 10, 2);
  CHECK_CLIP (c2, 30, 0, 10, 2);
  CHECK_CLIP (c3, 40, 0, 20, 1);
  CHECK_CLIP (c4, 50, 0, 20, 2);
  CHECK_CLIP (c5, 60, 0, 20, 3);
  fail_unless (ges_container_edit (GES_CONTAINER (c), NULL, 0,
          GES_EDIT_MODE_RIPPLE, GES_EDGE_NONE, 0) == TRUE);
  CHECK_CLIP (c, 0, 0, 10, 0);
  CHECK_CLIP (c1, 10, 0, 10, 1);
  CHECK_CLIP (c2, 20, 0, 10, 1);
  CHECK_CLIP (c3, 30, 0, 20, 0);
  CHECK_CLIP (c4, 40, 0, 20, 1);
  CHECK_CLIP (c5, 50, 0, 20, 2);
  CHECK_OBJECT_PROPS (group, 0, 0, 30);

  fail_unless (ges_container_edit (GES_CONTAINER (c), NULL, 0,
          GES_EDIT_MODE_TRIM, GES_EDGE_START, 5) == TRUE);
  CHECK_CLIP (c, 5, 5, 5, 0);
  CHECK_CLIP (c1, 10, 0, 10, 1);
  CHECK_CLIP (c2, 20, 0, 10, 1);
  CHECK_CLIP (c3, 30, 0, 20, 0);
  CHECK_CLIP (c4, 40, 0, 20, 1);
  CHECK_CLIP (c5, 50, 0, 20, 2);
  CHECK_OBJECT_PROPS (group, 5, 0, 25);

  gst_object_unref (timeline);
  gst_object_unref (asset);

  ges_deinit ();
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

  ges_init ();

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

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_marker_snapping)
{
  GESTrack *track;
  GESTimeline *timeline;
  GESTrackElement *trackelement1, *trackelement2;
  GESContainer *clip1, *clip2;
  GESLayer *layer;
  GList *trackelements;
  GESMarkerList *marker_list1, *marker_list2;

  ges_init ();

  track = GES_TRACK (ges_video_track_new ());
  fail_unless (track != NULL);

  timeline = ges_timeline_new ();
  fail_unless (timeline != NULL);

  fail_unless (ges_timeline_add_track (timeline, track));

  clip1 = GES_CONTAINER (ges_test_clip_new ());
  clip2 = GES_CONTAINER (ges_test_clip_new ());

  fail_unless (clip1 && clip2);

  /**
   * Our timeline
   * ------------
   *               30
   * markers  -----|----------------
   *          |  clip1  ||  clip2  |
   * time    20 ------- 50 ------ 110
   *
   */
  g_object_set (clip1, "start", (guint64) 20, "duration", (guint64) 30,
      "in-point", (guint64) 0, NULL);
  g_object_set (clip2, "start", (guint64) 50, "duration", (guint64) 60,
      "in-point", (guint64) 0, NULL);

  fail_unless ((layer = ges_timeline_append_layer (timeline)) != NULL);
  assert_equals_int (ges_layer_get_priority (layer), 0);

  fail_unless (ges_layer_add_clip (layer, GES_CLIP (clip1)));
  fail_unless ((trackelements = GES_CONTAINER_CHILDREN (clip1)) != NULL);
  fail_unless ((trackelement1 =
          GES_TRACK_ELEMENT (trackelements->data)) != NULL);
  fail_unless (ges_track_element_get_track (trackelement1) == track);

  fail_unless (ges_layer_add_clip (layer, GES_CLIP (clip2)));
  fail_unless ((trackelements = GES_CONTAINER_CHILDREN (clip2)) != NULL);
  fail_unless ((trackelement2 =
          GES_TRACK_ELEMENT (trackelements->data)) != NULL);
  fail_unless (ges_track_element_get_track (trackelement2) == track);

  marker_list1 = ges_marker_list_new ();
  g_object_set (marker_list1, "flags", GES_MARKER_FLAG_SNAPPABLE, NULL);
  ges_marker_list_add (marker_list1, 10);
  ges_marker_list_add (marker_list1, 20);
  fail_unless (ges_meta_container_set_marker_list (GES_META_CONTAINER
          (trackelement1), "ges-test", marker_list1));

  /**
   * Snapping clip2 to a marker on clip1
   * ------------
   *               30 40
   * markers  -----|--|--
   *          |  clip1  |
   * time    20 ------ 50
   *              -----------
   *              |  clip2  |
   *             30 ------ 90
   */
  g_object_set (timeline, "snapping-distance", (guint64) 3, NULL);
  /* Move within 2 units of marker timestamp */
  fail_unless (ges_container_edit (clip2, NULL, -1, GES_EDIT_MODE_NORMAL,
          GES_EDGE_NONE, 32) == TRUE);
  /* Clip nr. 2 should snap to marker at timestamp 30 */
  DEEP_CHECK (clip1, 20, 0, 30);
  DEEP_CHECK (clip2, 30, 0, 60);

  /**
   * Snapping clip1 to a marker on clip2
   * ------------
   *                           90
   * markers                 --|--------
   *                         |  clip1  |
   * time                   80 ------ 110
   * markers      ----------|--
   *              |   clip2   |
   *             30 -------- 90
   */
  marker_list2 = ges_marker_list_new ();
  g_object_set (marker_list2, "flags", GES_MARKER_FLAG_SNAPPABLE, NULL);
  ges_marker_list_add (marker_list2, 40);
  ges_marker_list_add (marker_list2, 50);
  fail_unless (ges_meta_container_set_marker_list (GES_META_CONTAINER
          (trackelement2), "ges-test", marker_list2));

  fail_unless (ges_container_edit (clip1, NULL, -1, GES_EDIT_MODE_NORMAL,
          GES_EDGE_NONE, 77) == TRUE);
  DEEP_CHECK (clip1, 80, 0, 30);
  DEEP_CHECK (clip2, 30, 0, 60);

  /**
   * Checking if clip's own markers are properly ignored when snapping
   * (moving clip1 close to where one of its markers is)
   * ------------
   *                     100     112     122
   * markers              |     --|-------|--
   *                old m.pos.  |   clip1   |
   * time                      102 ------- 132
   */
  fail_unless (ges_container_edit (clip1, NULL, -1, GES_EDIT_MODE_NORMAL,
          GES_EDGE_NONE, 102) == TRUE);
  DEEP_CHECK (clip1, 102, 0, 30);
  DEEP_CHECK (clip2, 30, 0, 60);

  /**
   * Checking if non-snappable marker lists are correctly ignored.
   * (moving clip1 close to clip2's non-snappable marker)
   */
  g_object_set (marker_list2, "flags", GES_MARKER_FLAG_NONE, NULL);
  fail_unless (ges_container_edit (clip1, NULL, -1, GES_EDIT_MODE_NORMAL,
          GES_EDGE_NONE, 82) == TRUE);
  DEEP_CHECK (clip1, 82, 0, 30);
  DEEP_CHECK (clip2, 30, 0, 60);

  g_object_unref (marker_list1);
  g_object_unref (marker_list2);
  check_destroyed (G_OBJECT (timeline), G_OBJECT (trackelement1),
      trackelement2, clip1, clip2, layer, marker_list1, marker_list2, NULL);
  ges_deinit ();
}

GST_END_TEST;

static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges-timeline-edition");
  TCase *tc_chain = tcase_create ("timeline-edition");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_basic_timeline_edition);
  tcase_add_test (tc_chain, test_snapping);
  tcase_add_test (tc_chain, test_timeline_edition_mode);
  tcase_add_test (tc_chain, test_simple_triming);
  tcase_add_test (tc_chain, test_groups);
  tcase_add_test (tc_chain, test_snapping_groups);
  tcase_add_test (tc_chain, test_marker_snapping);

  return s;
}

GST_CHECK_MAIN (ges);
