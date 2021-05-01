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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "test-utils.h"
#include <ges/ges.h>
#include <gst/check/gstcheck.h>
#include <gst/controller/gstdirectcontrolbinding.h>
#include <gst/controller/gstinterpolationcontrolsource.h>

GMainLoop *mainloop;

static void
project_loaded_cb (GESProject * project, GESTimeline * timeline,
    GMainLoop * mainloop)
{
  g_main_loop_quit (mainloop);
}

GST_START_TEST (test_project_simple)
{
  gchar *id;
  GESProject *project;
  GESTimeline *timeline;

  ges_init ();

  mainloop = g_main_loop_new (NULL, FALSE);
  project = GES_PROJECT (ges_asset_request (GES_TYPE_TIMELINE, NULL, NULL));
  fail_unless (GES_IS_PROJECT (project));
  assert_equals_string (ges_asset_get_id (GES_ASSET (project)), "project-0");
  g_signal_connect (project, "loaded", (GCallback) project_loaded_cb, mainloop);

  timeline = GES_TIMELINE (ges_asset_extract (GES_ASSET (project), NULL));
  g_main_loop_run (mainloop);

  fail_unless (GES_IS_TIMELINE (timeline));
  id = ges_extractable_get_id (GES_EXTRACTABLE (timeline));
  assert_equals_string (id, "project-0");
  ASSERT_OBJECT_REFCOUNT (timeline, "We own the only ref", 1);

  g_free (id);
  gst_object_unref (project);
  gst_object_unref (timeline);
  g_main_loop_unref (mainloop);
  g_signal_handlers_disconnect_by_func (project, (GCallback) project_loaded_cb,
      mainloop);

  ges_deinit ();
}

GST_END_TEST;

static void
asset_removed_add_cb (GESProject * project, GESAsset * asset, gboolean * called)
{
  *called = TRUE;
}

static void
asset_created_cb (GObject * source, GAsyncResult * res, GESAsset ** asset)
{
  GError *error = NULL;
  *asset = ges_asset_request_finish (res, &error);

  fail_unless (error == NULL);
  g_main_loop_quit (mainloop);
}

GST_START_TEST (test_project_add_assets)
{
  GESProject *project;
  GESAsset *asset;
  gboolean added_cb_called = FALSE;
  gboolean removed_cb_called = FALSE;

  ges_init ();

  mainloop = g_main_loop_new (NULL, FALSE);
  project = GES_PROJECT (ges_asset_request (GES_TYPE_TIMELINE, NULL, NULL));
  fail_unless (GES_IS_PROJECT (project));

  g_signal_connect (project, "asset-added",
      (GCallback) asset_removed_add_cb, &added_cb_called);
  g_signal_connect (project, "asset-removed",
      (GCallback) asset_removed_add_cb, &removed_cb_called);

  ges_asset_request_async (GES_TYPE_TEST_CLIP, NULL, NULL,
      (GAsyncReadyCallback) asset_created_cb, &asset);
  g_main_loop_run (mainloop);
  g_main_loop_unref (mainloop);

  fail_unless (GES_IS_ASSET (asset));

  fail_unless (ges_project_add_asset (project, asset));
  fail_unless (added_cb_called);
  ASSERT_OBJECT_REFCOUNT (project, "The project", 2);
  ASSERT_OBJECT_REFCOUNT (asset, "The asset (1 for project and one for "
      "us + 1 cache)", 3);

  fail_unless (ges_project_remove_asset (project, asset));
  fail_unless (removed_cb_called);

  g_signal_handlers_disconnect_by_func (project,
      (GCallback) asset_removed_add_cb, &added_cb_called);
  g_signal_handlers_disconnect_by_func (project,
      (GCallback) asset_removed_add_cb, &removed_cb_called);

  gst_object_unref (asset);
  gst_object_unref (project);
  ASSERT_OBJECT_REFCOUNT (asset, "The asset (1 ref in cache)", 1);
  ASSERT_OBJECT_REFCOUNT (project, "The project (1 ref in cache)", 1);

  ges_deinit ();
}

GST_END_TEST;

static void
error_loading_asset_cb (GESProject * project, GError * error, gchar * id,
    GType extractable_type, GMainLoop * mainloop)
{
  fail_unless (g_error_matches (error, GST_PARSE_ERROR,
          GST_PARSE_ERROR_NO_SUCH_ELEMENT));
  g_main_loop_quit (mainloop);
}

GST_START_TEST (test_project_unexistant_effect)
{
  GESProject *project;
  gboolean added_cb_called = FALSE;
  gboolean removed_cb_called = FALSE;

  ges_init ();

  project = GES_PROJECT (ges_asset_request (GES_TYPE_TIMELINE, NULL, NULL));
  fail_unless (GES_IS_PROJECT (project));

  mainloop = g_main_loop_new (NULL, FALSE);
  g_signal_connect (project, "asset-added",
      (GCallback) asset_removed_add_cb, &added_cb_called);
  g_signal_connect (project, "asset-removed",
      (GCallback) asset_removed_add_cb, &removed_cb_called);
  g_signal_connect (project, "error-loading-asset",
      (GCallback) error_loading_asset_cb, mainloop);

  fail_unless (ges_project_create_asset (project, "nowaythiselementexists",
          GES_TYPE_EFFECT));
  g_main_loop_run (mainloop);

  /* And.... try again! */
  fail_if (ges_project_create_asset (project, "nowaythiselementexists",
          GES_TYPE_EFFECT));

  fail_if (added_cb_called);
  fail_if (removed_cb_called);

  ASSERT_OBJECT_REFCOUNT (project, "The project", 2);
  gst_object_unref (project);
  g_main_loop_unref (mainloop);

  ASSERT_OBJECT_REFCOUNT (project, "The project (1 ref in cache)", 1);

  ges_deinit ();
}

GST_END_TEST;

static void
asset_added_cb (GESProject * project, GESAsset * asset)
{
  gchar *uri = ges_test_file_uri ("audio_video.ogg");
  GstDiscovererInfo *info;

  if (ges_asset_get_extractable_type (asset) == GES_TYPE_EFFECT) {
    assert_equals_string (ges_asset_get_id (asset), "video agingtv");
  } else {
    info = ges_uri_clip_asset_get_info (GES_URI_CLIP_ASSET (asset));
    fail_unless (GST_IS_DISCOVERER_INFO (info));
    assert_equals_string (ges_asset_get_id (asset), uri);
  }

  g_free (uri);
}

static gchar *
_set_new_uri (GESProject * project, GError * error, GESAsset * wrong_asset)
{
  fail_unless (!g_strcmp0 (ges_asset_get_id (wrong_asset),
          "file:///test/not/exisiting"));

  return ges_test_file_uri ("audio_video.ogg");
}

static void
_test_project (GESProject * project, GESTimeline * timeline)
{
  guint a_meta;
  gchar *media_uri;
  GESTrack *track;
  const GList *profiles;
  GstEncodingContainerProfile *profile;
  GList *tracks, *tmp, *tmptrackelement, *clips;

  fail_unless (GES_IS_TIMELINE (timeline));
  assert_equals_int (g_list_length (timeline->layers), 2);

  assert_equals_string (ges_meta_container_get_string (GES_META_CONTAINER
          (project), "name"), "Example project");
  clips = ges_layer_get_clips (GES_LAYER (timeline->layers->data));
  fail_unless (ges_meta_container_get_uint (GES_META_CONTAINER
          (timeline->layers->data), "a", &a_meta));
  assert_equals_int (a_meta, 3);
  assert_equals_int (g_list_length (clips), 1);
  media_uri = ges_test_file_uri ("audio_video.ogg");
  assert_equals_string (ges_asset_get_id (ges_extractable_get_asset
          (GES_EXTRACTABLE (clips->data))), media_uri);
  g_free (media_uri);
  g_list_free_full (clips, gst_object_unref);

  /* Check tracks and the objects  they contain */
  tracks = ges_timeline_get_tracks (timeline);
  assert_equals_int (g_list_length (tracks), 2);
  for (tmp = tracks; tmp; tmp = tmp->next) {
    GList *trackelements;
    track = GES_TRACK (tmp->data);

    trackelements = ges_track_get_elements (track);
    GST_DEBUG_OBJECT (track, "Testing track");
    switch (track->type) {
      case GES_TRACK_TYPE_VIDEO:
        assert_equals_int (g_list_length (trackelements), 2);
        for (tmptrackelement = trackelements; tmptrackelement;
            tmptrackelement = tmptrackelement->next) {
          GESTrackElement *trackelement =
              GES_TRACK_ELEMENT (tmptrackelement->data);

          if (GES_IS_BASE_EFFECT (trackelement)) {
            guint nb_scratch_lines;

            ges_timeline_element_get_child_properties (tmptrackelement->data,
                "scratch-lines", &nb_scratch_lines, NULL);
            assert_equals_int (nb_scratch_lines, 12);

            nle_object_check (ges_track_element_get_nleobject (trackelement),
                0, 1000000000, 0, 1000000000, MIN_NLE_PRIO + TRANSITIONS_HEIGHT,
                TRUE);
          } else {
            nle_object_check (ges_track_element_get_nleobject (trackelement),
                0, 1000000000, 0, 1000000000,
                MIN_NLE_PRIO + TRANSITIONS_HEIGHT + 1, TRUE);
          }
        }
        break;
      case GES_TRACK_TYPE_AUDIO:
        assert_equals_int (g_list_length (trackelements), 2);
        break;
      default:
        g_assert (1);
    }

    g_list_free_full (trackelements, gst_object_unref);

  }
  g_list_free_full (tracks, gst_object_unref);

  /* Now test the encoding profile */
  profiles = ges_project_list_encoding_profiles (project);
  assert_equals_int (g_list_length ((GList *) profiles), 1);
  profile = profiles->data;
  fail_unless (GST_IS_ENCODING_CONTAINER_PROFILE (profile));
  profiles = gst_encoding_container_profile_get_profiles (profile);
  assert_equals_int (g_list_length ((GList *) profiles), 2);
}

static void
_add_properties (GESTimeline * timeline)
{
  GList *tracks;
  GList *tmp;

  tracks = ges_timeline_get_tracks (timeline);
  for (tmp = tracks; tmp; tmp = tmp->next) {
    GESTrack *track;
    GList *track_elements;
    GList *tmp_tck;

    track = GES_TRACK (tmp->data);
    switch (track->type) {
      case GES_TRACK_TYPE_VIDEO:
        track_elements = ges_track_get_elements (track);

        for (tmp_tck = track_elements; tmp_tck; tmp_tck = tmp_tck->next) {
          GESTrackElement *element = GES_TRACK_ELEMENT (tmp_tck->data);

          /* Adding keyframes */
          if (GES_IS_EFFECT (element)) {
            GstControlSource *source;
            GstControlBinding *tmp_binding, *binding;

            source = gst_interpolation_control_source_new ();

            /* Check binding creation and replacement */
            binding =
                ges_track_element_get_control_binding (element,
                "scratch-lines");
            fail_unless (binding == NULL);
            ges_track_element_set_control_source (element,
                source, "scratch-lines", "direct");
            tmp_binding =
                ges_track_element_get_control_binding (element,
                "scratch-lines");
            fail_unless (tmp_binding != NULL);
            ges_track_element_set_control_source (element,
                source, "scratch-lines", "direct");
            binding =
                ges_track_element_get_control_binding (element,
                "scratch-lines");
            fail_unless (binding != tmp_binding);


            g_object_set (source, "mode", GST_INTERPOLATION_MODE_LINEAR, NULL);
            gst_timed_value_control_source_set (GST_TIMED_VALUE_CONTROL_SOURCE
                (source), 0 * GST_SECOND, 0.);
            gst_timed_value_control_source_set (GST_TIMED_VALUE_CONTROL_SOURCE
                (source), 5 * GST_SECOND, 0.);
            gst_timed_value_control_source_set (GST_TIMED_VALUE_CONTROL_SOURCE
                (source), 10 * GST_SECOND, 1.);

            gst_object_unref (source);
          } else if (GES_IS_VIDEO_SOURCE (element)) {
            /* Adding children properties */
            gint posx = 42;
            ges_timeline_element_set_child_properties (GES_TIMELINE_ELEMENT
                (element), "posx", posx, NULL);
            ges_timeline_element_get_child_properties (GES_TIMELINE_ELEMENT
                (element), "posx", &posx, NULL);
            fail_unless_equals_int (posx, 42);
          }

        }
        g_list_free_full (track_elements, g_object_unref);
        break;
      default:
        break;
    }
  }

  g_list_free_full (tracks, g_object_unref);
}

static void
_check_properties (GESTimeline * timeline)
{
  GList *tracks;
  GList *tmp;

  tracks = ges_timeline_get_tracks (timeline);
  for (tmp = tracks; tmp; tmp = tmp->next) {
    GESTrack *track;
    GList *track_elements;
    GList *tmp_tck;

    track = GES_TRACK (tmp->data);
    switch (track->type) {
      case GES_TRACK_TYPE_VIDEO:
        track_elements = ges_track_get_elements (track);

        for (tmp_tck = track_elements; tmp_tck; tmp_tck = tmp_tck->next) {
          GESTrackElement *element = GES_TRACK_ELEMENT (tmp_tck->data);
          /* Checking keyframes */
          if (GES_IS_EFFECT (element)) {
            GstControlBinding *binding;
            GstControlSource *source;
            GList *timed_values, *tmpvalue;
            GstTimedValue *value;

            binding =
                ges_track_element_get_control_binding (element,
                "scratch-lines");
            fail_unless (binding != NULL);
            g_object_get (binding, "control-source", &source, NULL);
            fail_unless (source != NULL);

            /* Now check keyframe position */
            tmpvalue = timed_values =
                gst_timed_value_control_source_get_all
                (GST_TIMED_VALUE_CONTROL_SOURCE (source));
            value = tmpvalue->data;
            fail_unless (value->value == 0.);
            fail_unless (value->timestamp == 0 * GST_SECOND);
            tmpvalue = tmpvalue->next;
            value = tmpvalue->data;
            fail_unless (value->value == 0.);
            fail_unless (value->timestamp == 5 * GST_SECOND);
            tmpvalue = tmpvalue->next;
            value = tmpvalue->data;
            fail_unless (value->value == 1.);
            fail_unless (value->timestamp == 10 * GST_SECOND);
            g_list_free (timed_values);
            gst_object_unref (source);
          }
          /* Checking children properties */
          else if (GES_IS_VIDEO_SOURCE (element)) {
            /* Init 'posx' with a wrong value */
            gint posx = 27;
            ges_timeline_element_get_child_properties (GES_TIMELINE_ELEMENT
                (element), "posx", &posx, NULL);
            fail_unless_equals_int (posx, 42);
          }
        }
        g_list_free_full (track_elements, g_object_unref);
        break;
      default:
        break;
    }
  }

  g_list_free_full (tracks, g_object_unref);
}

GST_START_TEST (test_project_add_properties)
{
  GESProject *project;
  GESTimeline *timeline;
  gchar *uri;

  ges_init ();

  uri = ges_test_file_uri ("test-properties.xges");
  project = ges_project_new (uri);
  g_free (uri);
  mainloop = g_main_loop_new (NULL, FALSE);

  /* Connect the signals */
  g_signal_connect (project, "loaded", (GCallback) project_loaded_cb, mainloop);
  g_signal_connect (project, "missing-uri", (GCallback) _set_new_uri, NULL);

  /* Now extract a timeline from it */
  GST_LOG ("Loading project");
  timeline = GES_TIMELINE (ges_asset_extract (GES_ASSET (project), NULL));

  g_main_loop_run (mainloop);

  GST_LOG ("Test first loading");


  _add_properties (timeline);

  uri = ges_test_get_tmp_uri ("test-properties-save.xges");
  fail_unless (ges_project_save (project, timeline, uri, NULL, TRUE, NULL));
  gst_object_unref (timeline);
  gst_object_unref (project);

  project = ges_project_new (uri);
  g_free (uri);

  ASSERT_OBJECT_REFCOUNT (project, "Our + cache", 2);

  g_signal_connect (project, "loaded", (GCallback) project_loaded_cb, mainloop);

  GST_LOG ("Loading saved project");
  timeline = GES_TIMELINE (ges_asset_extract (GES_ASSET (project), NULL));
  fail_unless (GES_IS_TIMELINE (timeline));

  g_main_loop_run (mainloop);

  _check_properties (timeline);

  gst_object_unref (timeline);
  gst_object_unref (project);

  g_main_loop_unref (mainloop);
  g_signal_handlers_disconnect_by_func (project, (GCallback) project_loaded_cb,
      mainloop);
  g_signal_handlers_disconnect_by_func (project, (GCallback) asset_added_cb,
      NULL);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_project_load_xges)
{
  gboolean saved;
  GESProject *loaded_project, *saved_project;
  GESTimeline *timeline;
  GESAsset *formatter_asset;
  gchar *uri;
  GList *tmp;

  ges_init ();

  uri = ges_test_file_uri ("test-project.xges");
  loaded_project = ges_project_new (uri);
  mainloop = g_main_loop_new (NULL, FALSE);
  fail_unless (GES_IS_PROJECT (loaded_project));

  /* Connect the signals */
  g_signal_connect (loaded_project, "asset-added", (GCallback) asset_added_cb,
      NULL);
  g_signal_connect (loaded_project, "loaded", (GCallback) project_loaded_cb,
      mainloop);

  /* Make sure we update the project's dummy URL to some actual URL */
  g_signal_connect (loaded_project, "missing-uri", (GCallback) _set_new_uri,
      NULL);

  /* Now extract a timeline from it */
  GST_LOG ("Loading project");
  timeline =
      GES_TIMELINE (ges_asset_extract (GES_ASSET (loaded_project), NULL));
  fail_unless (GES_IS_TIMELINE (timeline));
  tmp = ges_project_get_loading_assets (loaded_project);
  assert_equals_int (g_list_length (tmp), 1);
  g_list_free_full (tmp, g_object_unref);

  g_main_loop_run (mainloop);
  GST_LOG ("Test first loading");
  _test_project (loaded_project, timeline);
  g_free (uri);

  uri = ges_test_get_tmp_uri ("test-project_TMP.xges");
  formatter_asset = ges_asset_request (GES_TYPE_FORMATTER, "ges", NULL);
  saved =
      ges_project_save (loaded_project, timeline, uri, formatter_asset, TRUE,
      NULL);
  fail_unless (saved);
  gst_object_unref (timeline);

  saved_project = ges_project_new (uri);
  ASSERT_OBJECT_REFCOUNT (saved_project, "Our + cache", 2);
  g_signal_connect (saved_project, "asset-added", (GCallback) asset_added_cb,
      NULL);
  g_signal_connect (saved_project, "loaded", (GCallback) project_loaded_cb,
      mainloop);

  GST_LOG ("Loading saved project");
  timeline = GES_TIMELINE (ges_asset_extract (GES_ASSET (saved_project), NULL));
  fail_unless (GES_IS_TIMELINE (timeline));
  g_main_loop_run (mainloop);
  _test_project (saved_project, timeline);

  fail_unless (ges_meta_container_get_string (GES_META_CONTAINER
          (loaded_project), GES_META_FORMAT_VERSION));
  fail_unless_equals_string (ges_meta_container_get_string (GES_META_CONTAINER
          (loaded_project), GES_META_FORMAT_VERSION),
      ges_meta_container_get_string (GES_META_CONTAINER (loaded_project),
          GES_META_FORMAT_VERSION));
  gst_object_unref (timeline);
  gst_object_unref (saved_project);
  gst_object_unref (loaded_project);
  g_free (uri);

  ASSERT_OBJECT_REFCOUNT (saved_project, "Still 1 ref for asset cache", 1);

  g_main_loop_unref (mainloop);
  g_signal_handlers_disconnect_by_func (saved_project,
      (GCallback) project_loaded_cb, mainloop);
  g_signal_handlers_disconnect_by_func (saved_project,
      (GCallback) asset_added_cb, NULL);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_project_auto_transition)
{
  GList *layers, *tmp;
  GESProject *project;
  GESTimeline *timeline;
  GESLayer *layer = NULL;
  GESAsset *formatter_asset;
  gboolean saved;
  gchar *tmpuri, *uri;

  ges_init ();

  uri = ges_test_file_uri ("test-auto-transition.xges");
  project = ges_project_new (uri);
  mainloop = g_main_loop_new (NULL, FALSE);
  fail_unless (GES_IS_PROJECT (project));

  /* Connect the signals */
  g_signal_connect (project, "loaded", (GCallback) project_loaded_cb, mainloop);
  g_signal_connect (project, "missing-uri", (GCallback) _set_new_uri, NULL);

  /* Now extract a timeline from it */
  GST_LOG ("Loading project");
  timeline = GES_TIMELINE (ges_asset_extract (GES_ASSET (project), NULL));

  g_main_loop_run (mainloop);

  /* Check timeline and layers auto-transition, must be FALSE */
  fail_if (ges_timeline_get_auto_transition (timeline));
  layers = ges_timeline_get_layers (timeline);
  for (tmp = layers; tmp; tmp = tmp->next) {
    layer = tmp->data;
    fail_if (ges_layer_get_auto_transition (layer));
  }

  g_list_free_full (layers, gst_object_unref);
  g_free (uri);

  /* Set timeline and layers auto-transition to TRUE */
  ges_timeline_set_auto_transition (timeline, TRUE);

  tmpuri = ges_test_get_tmp_uri ("test-auto-transition-save.xges");
  formatter_asset = ges_asset_request (GES_TYPE_FORMATTER, "ges", NULL);
  saved =
      ges_project_save (project, timeline, tmpuri, formatter_asset, TRUE, NULL);
  fail_unless (saved);

  gst_object_unref (timeline);
  gst_object_unref (project);

  project = ges_project_new (tmpuri);

  ASSERT_OBJECT_REFCOUNT (project, "Our + cache", 2);

  g_signal_connect (project, "loaded", (GCallback) project_loaded_cb, mainloop);

  GST_LOG ("Loading saved project");
  timeline = GES_TIMELINE (ges_asset_extract (GES_ASSET (project), NULL));
  fail_unless (GES_IS_TIMELINE (timeline));

  g_main_loop_run (mainloop);

  /* Check timeline and layers auto-transition, must be TRUE  */
  fail_unless (ges_timeline_get_auto_transition (timeline));
  layers = ges_timeline_get_layers (timeline);
  for (tmp = layers; tmp; tmp = tmp->next) {
    layer = tmp->data;
    fail_unless (ges_layer_get_auto_transition (layer));
  }

  g_list_free_full (layers, gst_object_unref);
  gst_object_unref (timeline);
  gst_object_unref (project);
  g_free (tmpuri);

  g_main_loop_unref (mainloop);
  g_signal_handlers_disconnect_by_func (project, (GCallback) project_loaded_cb,
      mainloop);
  g_signal_handlers_disconnect_by_func (project, (GCallback) asset_added_cb,
      NULL);

  ges_deinit ();
}

GST_END_TEST;

/*  FIXME This test does not pass for some bad reason */
#if 0
static void
project_loaded_now_play_cb (GESProject * project, GESTimeline * timeline)
{
  GstBus *bus;
  GstMessage *message;
  gboolean carry_on = TRUE;

  GESPipeline *pipeline = ges_pipeline_new ();

  fail_unless (ges_pipeline_set_timeline (pipeline, timeline));

  bus = gst_element_get_bus (GST_ELEMENT (pipeline));
  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE);

  GST_DEBUG ("Let's poll the bus");

  while (carry_on) {
    message = gst_bus_poll (bus, GST_MESSAGE_ANY, GST_SECOND / 10);
    if (message) {
      GST_ERROR ("GOT MESSAGE: %" GST_PTR_FORMAT, message);
      switch (GST_MESSAGE_TYPE (message)) {
        case GST_MESSAGE_EOS:
          /* we should check if we really finished here */
          GST_WARNING ("Got an EOS, we did not even start!");
          carry_on = FALSE;
          fail_if (TRUE);
          break;
        case GST_MESSAGE_SEGMENT_START:
        case GST_MESSAGE_SEGMENT_DONE:
          /* We shouldn't see any segement messages, since we didn't do a segment seek */
          GST_WARNING ("Saw a Segment start/stop");
          fail_if (TRUE);
          break;
        case GST_MESSAGE_ERROR:
          fail_error_message (message);
          break;
        case GST_MESSAGE_ASYNC_DONE:
          GST_DEBUG ("prerolling done");
          carry_on = FALSE;
          break;
        default:
          break;
      }
      gst_mini_object_unref (GST_MINI_OBJECT (message));
    }
  }

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);
  gst_object_unref (pipeline);
  g_main_loop_quit (mainloop);
}


GST_START_TEST (test_load_xges_and_play)
{
  GESProject *project;
  GESTimeline *timeline;
  gchar *uri = ges_test_file_uri ("test-project_TMP.xges");

  project = ges_project_new (uri);
  fail_unless (GES_IS_PROJECT (project));

  mainloop = g_main_loop_new (NULL, FALSE);
  /* Connect the signals */
  g_signal_connect (project, "loaded", (GCallback) project_loaded_now_play_cb,
      NULL);

  /* Now extract a timeline from it */
  timeline = GES_TIMELINE (ges_asset_extract (GES_ASSET (project), NULL));
  fail_unless (GES_IS_TIMELINE (timeline));

  g_main_loop_run (mainloop);

  g_free (uri);
  gst_object_unref (project);
  gst_object_unref (timeline);
  g_main_loop_unref (mainloop);
}

GST_END_TEST;
#endif

static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges-project");
  TCase *tc_chain = tcase_create ("project");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_project_simple);
  tcase_add_test (tc_chain, test_project_add_assets);
  tcase_add_test (tc_chain, test_project_load_xges);
  tcase_add_test (tc_chain, test_project_add_properties);
  tcase_add_test (tc_chain, test_project_auto_transition);
  /*tcase_add_test (tc_chain, test_load_xges_and_play); */
  tcase_add_test (tc_chain, test_project_unexistant_effect);

  return s;
}

GST_CHECK_MAIN (ges);
