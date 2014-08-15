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
      "by g_simple_async_result_complete_in_idle", 3);
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

  fail_unless (ges_init ());

  mainloop = g_main_loop_new (NULL, FALSE);

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
      51, MIN_NLE_PRIO, TRUE);

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
      120, 510, MIN_NLE_PRIO + 0, TRUE);

  /* Test mute support */
  g_object_set (clip, "mute", TRUE, NULL);
  ges_timeline_commit (timeline);
  nle_object_check (ges_track_element_get_nleobject (trackelement), 420, 510,
      120, 510, MIN_NLE_PRIO + 0, FALSE);
  g_object_set (clip, "mute", FALSE, NULL);
  ges_timeline_commit (timeline);
  nle_object_check (ges_track_element_get_nleobject (trackelement), 420, 510,
      120, 510, MIN_NLE_PRIO + 0, TRUE);

  ges_container_remove (GES_CONTAINER (clip),
      GES_TIMELINE_ELEMENT (trackelement));

  gst_object_unref (timeline);
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
  fail_unless (GES_IS_IMAGE_SOURCE (track_element));

  ASSERT_OBJECT_REFCOUNT (track_element, "1 in track, 1 in clip 2 in timeline",
      4);

  gst_object_unref (timeline);
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

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = ges_suite ();

  gst_check_init (&argc, &argv);

  av_uri = ges_test_get_audio_video_uri ();
  image_uri = ges_test_get_image_uri ();

  nf = gst_check_run_suite (s, "ges", __FILE__);

  g_free (av_uri);
  g_free (image_uri);

  return nf;
}
