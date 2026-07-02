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
#include <stdarg.h>

/* This test uri will eventually have to be fixed */
#define TEST_URI "http://nowhere/blahblahblah"


static gchar *av_uri;
static gchar *image_uri;
GMainLoop *mainloop;

typedef struct _AssetUri
{
  const gchar *uri;
  GESAsset *asset;
} AssetUri;

static void
asset_created_cb (GObject * source, GAsyncResult * res, gpointer udata)
{
  GList *tracks, *tmp;
  GESAsset *asset;
  GESLayer *layer;
  GESUriClip *tlfs;

  GError *error = NULL;

  asset = ges_asset_request_finish (res, &error);
  ASSERT_OBJECT_REFCOUNT (asset, "1 for us + for the cache + 1 taken "
      "by g_task", 3);
  fail_unless (error == NULL);
  fail_if (asset == NULL);
  fail_if (g_strcmp0 (ges_asset_get_id (asset), av_uri));

  layer = GES_LAYER (g_async_result_get_user_data (res));
  tlfs = GES_URI_CLIP (ges_layer_add_asset (layer,
          asset, 0, 0, GST_CLOCK_TIME_NONE, GES_TRACK_TYPE_UNKNOWN));
  fail_unless (GES_IS_URI_CLIP (tlfs));
  fail_if (g_strcmp0 (ges_uri_clip_get_uri (tlfs), av_uri));
  assert_equals_uint64 (_DURATION (tlfs), GST_SECOND);

  fail_unless (ges_clip_get_supported_formats
      (GES_CLIP (tlfs)) & GES_TRACK_TYPE_VIDEO);
  fail_unless (ges_clip_get_supported_formats
      (GES_CLIP (tlfs)) & GES_TRACK_TYPE_AUDIO);

  tracks = ges_timeline_get_tracks (ges_layer_get_timeline (layer));
  for (tmp = tracks; tmp; tmp = tmp->next) {
    GList *trackelements = ges_track_get_elements (GES_TRACK (tmp->data));

    assert_equals_int (g_list_length (trackelements), 1);
    fail_unless (GES_IS_VIDEO_URI_SOURCE (trackelements->data)
        || GES_IS_AUDIO_URI_SOURCE (trackelements->data));
    g_list_free_full (trackelements, gst_object_unref);
  }
  g_list_free_full (tracks, gst_object_unref);

  gst_object_unref (asset);
  g_main_loop_quit (mainloop);
}

GST_START_TEST (test_filesource_basic)
{
  GESTimeline *timeline;
  GESLayer *layer;

  mainloop = g_main_loop_new (NULL, FALSE);

  ges_init ();

  timeline = ges_timeline_new_audio_video ();
  fail_unless (timeline != NULL);

  layer = ges_layer_new ();
  fail_unless (layer != NULL);
  fail_unless (ges_timeline_add_layer (timeline, layer));

  ges_asset_request_async (GES_TYPE_URI_CLIP,
      av_uri, NULL, asset_created_cb, layer);

  g_main_loop_run (mainloop);
  g_main_loop_unref (mainloop);
  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

static gboolean
create_asset (AssetUri * asset_uri)
{
  asset_uri->asset =
      GES_ASSET (ges_uri_clip_asset_request_sync (asset_uri->uri, NULL));
  g_main_loop_quit (mainloop);

  return FALSE;
}

GST_START_TEST (test_filesource_properties)
{
  GESClip *clip;
  GESTrack *track;
  AssetUri asset_uri;
  GESTimeline *timeline;
  GESUriClipAsset *asset;
  GESLayer *layer;
  GESTrackElement *trackelement;

  ges_init ();

  track = ges_track_new (GES_TRACK_TYPE_AUDIO, gst_caps_ref (GST_CAPS_ANY));
  fail_unless (track != NULL);

  layer = ges_layer_new ();
  fail_unless (layer != NULL);
  timeline = ges_timeline_new ();
  fail_unless (GES_IS_TIMELINE (timeline));
  fail_unless (ges_timeline_add_layer (timeline, layer));
  fail_unless (ges_timeline_add_track (timeline, track));
  ASSERT_OBJECT_REFCOUNT (timeline, "timeline", 1);

  mainloop = g_main_loop_new (NULL, FALSE);
  asset_uri.uri = av_uri;
  /* Right away request the asset synchronously */
  g_timeout_add (1, (GSourceFunc) create_asset, &asset_uri);
  g_main_loop_run (mainloop);

  asset = GES_URI_CLIP_ASSET (asset_uri.asset);
  fail_unless (GES_IS_ASSET (asset));
  clip = ges_layer_add_asset (layer, GES_ASSET (asset),
      42, 12, 51, GES_TRACK_TYPE_AUDIO);
  ges_timeline_commit (timeline);
  assert_is_type (clip, GES_TYPE_URI_CLIP);
  assert_equals_uint64 (_START (clip), 42);
  assert_equals_uint64 (_DURATION (clip), 51);
  assert_equals_uint64 (_INPOINT (clip), 12);

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
  nle_object_check (ges_track_element_get_nleobject (trackelement), 42, 51, 12,
      51, MIN_NLE_PRIO + TRANSITIONS_HEIGHT, TRUE);

  /* Change more properties, see if they propagate */
  g_object_set (clip, "start", (guint64) 420, "duration", (guint64) 510,
      "in-point", (guint64) 120, NULL);
  ges_timeline_commit (timeline);
  assert_equals_uint64 (_START (clip), 420);
  assert_equals_uint64 (_DURATION (clip), 510);
  assert_equals_uint64 (_INPOINT (clip), 120);
  assert_equals_uint64 (_START (trackelement), 420);
  assert_equals_uint64 (_DURATION (trackelement), 510);
  assert_equals_uint64 (_INPOINT (trackelement), 120);

  /* And let's also check that it propagated correctly to GNonLin */
  nle_object_check (ges_track_element_get_nleobject (trackelement), 420, 510,
      120, 510, MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 0, TRUE);

  /* Test mute support */
  g_object_set (clip, "mute", TRUE, NULL);
  ges_timeline_commit (timeline);
  nle_object_check (ges_track_element_get_nleobject (trackelement), 420, 510,
      120, 510, MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 0, FALSE);
  g_object_set (clip, "mute", FALSE, NULL);
  ges_timeline_commit (timeline);
  nle_object_check (ges_track_element_get_nleobject (trackelement), 420, 510,
      120, 510, MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 0, TRUE);

  ges_container_remove (GES_CONTAINER (clip),
      GES_TIMELINE_ELEMENT (trackelement));

  gst_object_unref (asset);
  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_filesource_images)
{
  GESClip *clip;
  GESAsset *asset;
  GESTrack *a, *v;
  GESUriClip *uriclip;
  AssetUri asset_uri;
  GESTimeline *timeline;
  GESLayer *layer;
  GESTrackElement *track_element;

  ges_init ();

  a = GES_TRACK (ges_audio_track_new ());
  v = GES_TRACK (ges_video_track_new ());

  layer = ges_layer_new ();
  fail_unless (layer != NULL);
  timeline = ges_timeline_new ();
  fail_unless (timeline != NULL);
  fail_unless (ges_timeline_add_layer (timeline, layer));
  fail_unless (ges_timeline_add_track (timeline, a));
  fail_unless (ges_timeline_add_track (timeline, v));
  ASSERT_OBJECT_REFCOUNT (timeline, "timeline", 1);

  mainloop = g_main_loop_new (NULL, FALSE);
  /* Right away request the asset synchronously */
  asset_uri.uri = image_uri;
  g_timeout_add (1, (GSourceFunc) create_asset, &asset_uri);
  g_main_loop_run (mainloop);

  asset = asset_uri.asset;
  fail_unless (GES_IS_ASSET (asset));
  fail_unless (ges_uri_clip_asset_is_image (GES_URI_CLIP_ASSET (asset)));
  uriclip = GES_URI_CLIP (ges_asset_extract (asset, NULL));
  fail_unless (GES_IS_URI_CLIP (uriclip));
  fail_unless (ges_clip_get_supported_formats (GES_CLIP (uriclip)) ==
      GES_TRACK_TYPE_VIDEO);
  clip = GES_CLIP (uriclip);
  fail_unless (ges_uri_clip_is_image (uriclip));
  ges_timeline_element_set_duration (GES_TIMELINE_ELEMENT (clip),
      1 * GST_SECOND);

  /* the returned track element should be an image source */
  /* the clip should not create any TrackElement in the audio track */
  ges_layer_add_clip (layer, GES_CLIP (clip));
  assert_equals_int (g_list_length (GES_CONTAINER_CHILDREN (clip)), 1);
  track_element = GES_CONTAINER_CHILDREN (clip)->data;
  fail_unless (track_element != NULL);
  fail_unless (GES_TIMELINE_ELEMENT_PARENT (track_element) ==
      GES_TIMELINE_ELEMENT (clip));
  fail_unless (ges_track_element_get_track (track_element) == v);
  fail_unless (GES_IS_VIDEO_URI_SOURCE (track_element));

  ASSERT_OBJECT_REFCOUNT (track_element, "1 in track, 1 in clip 2 in timeline",
      3);

  gst_object_unref (asset);
  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

/* A hermetic multi-stream URI: two audio streams and one video stream, so a
 * single URI exposes several streams of the same type - which is what stream
 * selection to distinct tracks is about. */
#define MULTI_STREAM_URI "testbin://audio+audio+video"

/* The @n-th stream asset of type @type, in discovery order. */
static GESUriSourceAsset *
_nth_stream_asset (GESUriClipAsset * asset, GESTrackType type, guint n)
{
  const GList *tmp;
  guint i = 0;

  for (tmp = ges_uri_clip_asset_get_stream_assets (asset); tmp; tmp = tmp->next) {
    GESUriSourceAsset *stream = tmp->data;

    if (ges_track_element_asset_get_track_type (GES_TRACK_ELEMENT_ASSET
            (stream))
        != type)
      continue;
    if (i++ == n)
      return stream;
  }
  return NULL;
}

/* The core source child extracted from stream asset @stream, or NULL. A core
 * source's extractable asset is the very stream asset it was created from, so
 * that is what ties a placed child back to its stream. */
static GESTrackElement *
_core_source_for_stream (GESClip * clip, GESUriSourceAsset * stream)
{
  GList *tmp, *children = ges_container_get_children (GES_CONTAINER (clip),
      FALSE);
  GESTrackElement *found = NULL;

  for (tmp = children; tmp; tmp = tmp->next) {
    GESTrackElement *src = tmp->data;

    if (ges_track_element_is_core (src)
        && ges_extractable_get_asset (GES_EXTRACTABLE (src)) ==
        GES_ASSET (stream)) {
      found = src;
      break;
    }
  }
  g_list_free_full (children, gst_object_unref);
  return found;
}

static guint
_n_core_sources (GESClip * clip)
{
  GList *tmp, *children = ges_container_get_children (GES_CONTAINER (clip),
      FALSE);
  guint n = 0;

  for (tmp = children; tmp; tmp = tmp->next)
    if (ges_track_element_is_core (tmp->data))
      n++;
  g_list_free_full (children, gst_object_unref);
  return n;
}

/* Route @clip's streams following the (stream, track) pairs, and return the
 * (transfer full) list of the sources created for the newly mapped streams. */
static GList *
_route (GESClip * clip, GESUriSourceAsset * first_stream, ...)
{
  GESSourceTrackMapBuilder *builder = ges_source_track_map_builder_new ();
  GESUriSourceAsset *stream = first_stream;
  GList *created = NULL;
  GError *error = NULL;
  va_list args;

  va_start (args, first_stream);
  while (stream) {
    GESTrack *track = va_arg (args, GESTrack *);

    ges_source_track_map_builder_add (builder, stream, track);
    stream = va_arg (args, GESUriSourceAsset *);
  }
  va_end (args);

  fail_unless (ges_uri_clip_set_source_track_map (GES_URI_CLIP (clip),
          ges_source_track_map_builder_build (builder), &created, &error));
  fail_unless (error == NULL);

  return created;
}

/* Each mapped stream, including several of the same type, lands in exactly the
 * track it was routed to. */
GST_START_TEST (test_filesource_stream_selection_routing)
{
  GESTimeline *timeline;
  GESTrack *audio0, *audio1, *video;
  GESLayer *layer;
  GESClip *clip;
  GESUriClipAsset *asset;
  GESUriSourceAsset *a0, *a1, *v0;
  GESSourceTrackMap *map;
  GList *created, *tracks;
  GError *error = NULL;

  ges_init ();

  asset = ges_uri_clip_asset_request_sync (MULTI_STREAM_URI, &error);
  fail_unless (asset != NULL);
  fail_unless (error == NULL);
  a0 = _nth_stream_asset (asset, GES_TRACK_TYPE_AUDIO, 0);
  a1 = _nth_stream_asset (asset, GES_TRACK_TYPE_AUDIO, 1);
  v0 = _nth_stream_asset (asset, GES_TRACK_TYPE_VIDEO, 0);
  fail_unless (a0 && a1 && v0 && a0 != a1);

  timeline = ges_timeline_new ();
  audio0 = GES_TRACK (ges_audio_track_new ());
  audio1 = GES_TRACK (ges_audio_track_new ());
  video = GES_TRACK (ges_video_track_new ());
  fail_unless (ges_timeline_add_track (timeline, audio0));
  fail_unless (ges_timeline_add_track (timeline, audio1));
  fail_unless (ges_timeline_add_track (timeline, video));
  layer = ges_timeline_append_layer (timeline);

  clip = GES_CLIP (ges_asset_extract (GES_ASSET (asset), NULL));
  ges_timeline_element_set_duration (GES_TIMELINE_ELEMENT (clip), GST_SECOND);

  created = _route (clip, a0, audio0, a1, audio1, v0, video, NULL);
  assert_equals_int (g_list_length (created), 3);
  g_list_free_full (created, gst_object_unref);

  fail_unless (ges_layer_add_clip (layer, clip));

  assert_equals_int (_n_core_sources (clip), 3);
  fail_unless (ges_track_element_get_track (_core_source_for_stream (clip,
              a0)) == audio0);
  fail_unless (ges_track_element_get_track (_core_source_for_stream (clip,
              a1)) == audio1);
  fail_unless (ges_track_element_get_track (_core_source_for_stream (clip,
              v0)) == video);

  /* the map the clip exposes mirrors the routing */
  map = ges_uri_clip_get_source_track_map (GES_URI_CLIP (clip));
  fail_unless (map != NULL);
  assert_equals_int (ges_source_track_map_get_size (map), 3);
  fail_unless (ges_source_track_map_contains (map, a0));
  fail_unless (ges_source_track_map_contains (map, a1));
  fail_unless (ges_source_track_map_contains (map, v0));
  tracks = ges_source_track_map_get_tracks (map, a0);
  assert_equals_int (g_list_length (tracks), 1);
  fail_unless (tracks->data == audio0);
  g_list_free_full (tracks, gst_object_unref);
  ges_source_track_map_unref (map);

  gst_object_unref (asset);
  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

/* Only the mapped streams produce a core child; the others are not created. */
GST_START_TEST (test_filesource_stream_selection_prune)
{
  GESTimeline *timeline;
  GESTrack *audio;
  GESLayer *layer;
  GESClip *clip;
  GESUriClipAsset *asset;
  GESUriSourceAsset *a0, *a1, *v0;
  GList *created;
  GError *error = NULL;

  ges_init ();

  asset = ges_uri_clip_asset_request_sync (MULTI_STREAM_URI, &error);
  fail_unless (asset != NULL);
  a0 = _nth_stream_asset (asset, GES_TRACK_TYPE_AUDIO, 0);
  a1 = _nth_stream_asset (asset, GES_TRACK_TYPE_AUDIO, 1);
  v0 = _nth_stream_asset (asset, GES_TRACK_TYPE_VIDEO, 0);
  fail_unless (a0 && a1 && v0);

  timeline = ges_timeline_new ();
  audio = GES_TRACK (ges_audio_track_new ());
  fail_unless (ges_timeline_add_track (timeline, audio));
  layer = ges_timeline_append_layer (timeline);

  clip = GES_CLIP (ges_asset_extract (GES_ASSET (asset), NULL));
  ges_timeline_element_set_duration (GES_TIMELINE_ELEMENT (clip), GST_SECOND);

  created = _route (clip, a0, audio, NULL);
  assert_equals_int (g_list_length (created), 1);
  g_list_free_full (created, gst_object_unref);

  fail_unless (ges_layer_add_clip (layer, clip));

  assert_equals_int (_n_core_sources (clip), 1);
  fail_unless (_core_source_for_stream (clip, a0) != NULL);
  fail_unless (_core_source_for_stream (clip, a1) == NULL);
  fail_unless (_core_source_for_stream (clip, v0) == NULL);

  gst_object_unref (asset);
  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

/* Routing one stream to two tracks: the created sources are 1:1 with the
 * routings (created[0] -> first track, created[1] -> second track), and a
 * property set on a created source before the clip joins a timeline follows it
 * into its track. */
GST_START_TEST (test_filesource_stream_selection_binding)
{
  GESTimeline *timeline;
  GESTrack *audio0, *audio1;
  GESLayer *layer;
  GESClip *clip;
  GESUriClipAsset *asset;
  GESUriSourceAsset *a0;
  GESTrackElement *first, *second;
  GList *created;
  GError *error = NULL;

  ges_init ();

  asset = ges_uri_clip_asset_request_sync (MULTI_STREAM_URI, &error);
  fail_unless (asset != NULL);
  a0 = _nth_stream_asset (asset, GES_TRACK_TYPE_AUDIO, 0);
  fail_unless (a0 != NULL);

  timeline = ges_timeline_new ();
  audio0 = GES_TRACK (ges_audio_track_new ());
  audio1 = GES_TRACK (ges_audio_track_new ());
  fail_unless (ges_timeline_add_track (timeline, audio0));
  fail_unless (ges_timeline_add_track (timeline, audio1));
  layer = ges_timeline_append_layer (timeline);

  clip = GES_CLIP (ges_asset_extract (GES_ASSET (asset), NULL));
  ges_timeline_element_set_duration (GES_TIMELINE_ELEMENT (clip), GST_SECOND);

  created = _route (clip, a0, audio0, a0, audio1, NULL);
  assert_equals_int (g_list_length (created), 2);
  first = gst_object_ref (created->data);
  second = gst_object_ref (created->next->data);
  g_list_free_full (created, gst_object_unref);

  /* set a property on the first source, before it is placed in any track */
  ges_track_element_set_active (first, FALSE);

  fail_unless (ges_layer_add_clip (layer, clip));

  fail_unless (ges_track_element_get_track (first) == audio0);
  fail_unless (ges_track_element_get_track (second) == audio1);
  fail_if (ges_track_element_is_active (first));
  fail_unless (ges_track_element_is_active (second));

  gst_object_unref (first);
  gst_object_unref (second);
  gst_object_unref (asset);
  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

/* Changing the map at runtime swaps, prunes and recreates sources, keeping the
 * identity of the sources that are only moved between tracks. */
GST_START_TEST (test_filesource_stream_selection_runtime_remap)
{
  GESTimeline *timeline;
  GESTrack *audio0, *audio1;
  GESLayer *layer;
  GESClip *clip;
  GESUriClipAsset *asset;
  GESUriSourceAsset *a0, *a1;
  GESTrackElement *src_a0, *src_a1;
  GList *created;
  GError *error = NULL;

  ges_init ();

  asset = ges_uri_clip_asset_request_sync (MULTI_STREAM_URI, &error);
  fail_unless (asset != NULL);
  a0 = _nth_stream_asset (asset, GES_TRACK_TYPE_AUDIO, 0);
  a1 = _nth_stream_asset (asset, GES_TRACK_TYPE_AUDIO, 1);
  fail_unless (a0 && a1 && a0 != a1);

  timeline = ges_timeline_new ();
  audio0 = GES_TRACK (ges_audio_track_new ());
  audio1 = GES_TRACK (ges_audio_track_new ());
  fail_unless (ges_timeline_add_track (timeline, audio0));
  fail_unless (ges_timeline_add_track (timeline, audio1));
  layer = ges_timeline_append_layer (timeline);

  clip = GES_CLIP (ges_asset_extract (GES_ASSET (asset), NULL));
  ges_timeline_element_set_duration (GES_TIMELINE_ELEMENT (clip), GST_SECOND);

  created = _route (clip, a0, audio0, a1, audio1, NULL);
  g_list_free_full (created, gst_object_unref);
  fail_unless (ges_layer_add_clip (layer, clip));

  src_a0 = gst_object_ref (_core_source_for_stream (clip, a0));
  src_a1 = gst_object_ref (_core_source_for_stream (clip, a1));
  fail_unless (ges_track_element_get_track (src_a0) == audio0);
  fail_unless (ges_track_element_get_track (src_a1) == audio1);

  /* swap the tracks: the same sources move, nothing is (re)created */
  created = _route (clip, a0, audio1, a1, audio0, NULL);
  fail_unless (created == NULL);
  fail_unless (_core_source_for_stream (clip, a0) == src_a0);
  fail_unless (_core_source_for_stream (clip, a1) == src_a1);
  fail_unless (ges_track_element_get_track (src_a0) == audio1);
  fail_unless (ges_track_element_get_track (src_a1) == audio0);

  /* prune a1: its source is removed, a0's is kept */
  created = _route (clip, a0, audio1, NULL);
  g_list_free_full (created, gst_object_unref);
  assert_equals_int (_n_core_sources (clip), 1);
  fail_unless (_core_source_for_stream (clip, a0) == src_a0);
  fail_unless (_core_source_for_stream (clip, a1) == NULL);

  /* bring a1 back, in the now free track: a fresh source, distinct from the
   * removed one */
  created = _route (clip, a0, audio1, a1, audio0, NULL);
  g_list_free_full (created, gst_object_unref);
  assert_equals_int (_n_core_sources (clip), 2);
  fail_unless (_core_source_for_stream (clip, a0) == src_a0);
  fail_unless (_core_source_for_stream (clip, a1) != NULL);
  fail_unless (_core_source_for_stream (clip, a1) != src_a1);

  gst_object_unref (src_a0);
  gst_object_unref (src_a1);
  gst_object_unref (asset);
  gst_object_unref (timeline);

  ges_deinit ();
}

GST_END_TEST;

static void
_project_loaded_cb (GESProject * project, GESTimeline * timeline,
    GMainLoop * mainloop)
{
  g_main_loop_quit (mainloop);
}

/* The routing survives an xges save/load round-trip: the reloaded clip has a
 * source-track-map and its two same-type streams stay in distinct tracks. */
GST_START_TEST (test_filesource_stream_selection_serialization)
{
  GESTimeline *timeline, *loaded;
  GESTrack *audio0, *audio1, *video;
  GESLayer *layer;
  GESClip *clip;
  GESProject *project;
  GESUriClipAsset *asset;
  GESUriSourceAsset *a0, *a1, *v0;
  GESSourceTrackMap *map;
  GMainLoop *mainloop;
  GList *created, *clips, *layers, *tmp;
  GESTrack *audio_tracks[2] = { NULL, NULL };
  GESTrack *video_track = NULL;
  guint n_audio = 0;
  gchar *uri;
  GError *error = NULL;

  ges_init ();

  asset = ges_uri_clip_asset_request_sync (MULTI_STREAM_URI, &error);
  fail_unless (asset != NULL);
  a0 = _nth_stream_asset (asset, GES_TRACK_TYPE_AUDIO, 0);
  a1 = _nth_stream_asset (asset, GES_TRACK_TYPE_AUDIO, 1);
  v0 = _nth_stream_asset (asset, GES_TRACK_TYPE_VIDEO, 0);

  timeline = ges_timeline_new ();
  audio0 = GES_TRACK (ges_audio_track_new ());
  audio1 = GES_TRACK (ges_audio_track_new ());
  video = GES_TRACK (ges_video_track_new ());
  fail_unless (ges_timeline_add_track (timeline, audio0));
  fail_unless (ges_timeline_add_track (timeline, audio1));
  fail_unless (ges_timeline_add_track (timeline, video));
  layer = ges_timeline_append_layer (timeline);

  clip = GES_CLIP (ges_asset_extract (GES_ASSET (asset), NULL));
  ges_timeline_element_set_duration (GES_TIMELINE_ELEMENT (clip), GST_SECOND);
  created = _route (clip, a0, audio0, a1, audio1, v0, video, NULL);
  g_list_free_full (created, gst_object_unref);
  fail_unless (ges_layer_add_clip (layer, clip));

  uri = ges_test_get_tmp_uri ("stream-selection.xges");
  fail_unless (ges_timeline_save_to_uri (timeline, uri, NULL, TRUE, &error));
  fail_unless (error == NULL);
  gst_object_unref (timeline);

  /* reload into a fresh timeline */
  mainloop = g_main_loop_new (NULL, FALSE);
  project = ges_project_new (uri);
  g_signal_connect (project, "loaded", (GCallback) _project_loaded_cb,
      mainloop);
  loaded = GES_TIMELINE (ges_asset_extract (GES_ASSET (project), NULL));
  fail_unless (GES_IS_TIMELINE (loaded));
  g_main_loop_run (mainloop);
  g_main_loop_unref (mainloop);

  layers = ges_timeline_get_layers (loaded);
  clips = ges_layer_get_clips (layers->data);
  g_list_free_full (layers, gst_object_unref);
  fail_unless (GES_IS_URI_CLIP (clips->data));
  clip = clips->data;

  map = ges_uri_clip_get_source_track_map (GES_URI_CLIP (clip));
  fail_unless (map != NULL);
  assert_equals_int (ges_source_track_map_get_size (map), 3);
  ges_source_track_map_unref (map);

  assert_equals_int (_n_core_sources (clip), 3);
  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = tmp->next) {
    GESTrackElement *src = tmp->data;

    if (!ges_track_element_is_core (src))
      continue;
    if (GES_IS_AUDIO_URI_SOURCE (src)) {
      fail_unless (n_audio < 2);
      audio_tracks[n_audio++] = ges_track_element_get_track (src);
    } else {
      fail_unless (GES_IS_VIDEO_URI_SOURCE (src));
      video_track = ges_track_element_get_track (src);
    }
  }
  /* the two audio streams stayed in two distinct audio tracks, and the video
   * stream in a third one - a default selection could not achieve that */
  assert_equals_int (n_audio, 2);
  fail_unless (audio_tracks[0] != NULL && audio_tracks[1] != NULL);
  fail_unless (audio_tracks[0] != audio_tracks[1]);
  fail_unless (video_track != NULL);
  fail_unless (video_track != audio_tracks[0]
      && video_track != audio_tracks[1]);

  g_list_free_full (clips, gst_object_unref);
  g_free (uri);
  gst_object_unref (asset);
  gst_object_unref (loaded);
  gst_object_unref (project);

  ges_deinit ();
}

GST_END_TEST;


static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges-filesource");
  TCase *tc_chain = tcase_create ("filesource");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_filesource_basic);
  tcase_add_test (tc_chain, test_filesource_images);
  tcase_add_test (tc_chain, test_filesource_properties);
  tcase_add_test (tc_chain, test_filesource_stream_selection_routing);
  tcase_add_test (tc_chain, test_filesource_stream_selection_prune);
  tcase_add_test (tc_chain, test_filesource_stream_selection_binding);
  tcase_add_test (tc_chain, test_filesource_stream_selection_runtime_remap);
  tcase_add_test (tc_chain, test_filesource_stream_selection_serialization);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s;

  gst_check_init (&argc, &argv);

  s = ges_suite ();

  av_uri = ges_test_get_audio_video_uri ();
  image_uri = ges_test_get_image_uri ();

  nf = gst_check_run_suite (s, "ges", __FILE__);

  g_free (av_uri);
  g_free (image_uri);

  return nf;
}
