/* GStreamer Editing Services
 * Copyright (C) 2013 Mathieu Duponchelle <mduponchelle1@gmail.com>
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

/* *INDENT-OFF* */
static const char * const profile_specs[][4] = {
  { "application/ogg", "audio/x-vorbis", "video/x-theora", "ogv" },
  { "video/webm", "audio/x-vorbis", "video/x-vp8", "webm"},
};
/* *INDENT-ON* */

typedef enum
{
  PROFILE_NONE = -1,
  PROFILE_OGG,
  PROFILE_WEBM,
} EncodingProfileName;

typedef struct _PresetInfos
{
  const gchar *muxer_preset_name;
  const gchar *audio_preset_name;
  const gchar *video_preset_name;

  gsize expected_size;

} PresetInfos;

typedef struct SeekInfo
{
  GstClockTime position;        /* position to seek to */
  GstClockTime seeking_position;        /* position to do seek from */
} SeekInfo;

static GMainLoop *loop;
static GESTimelinePipeline *pipeline = NULL;
static gint64 seeked_position = GST_CLOCK_TIME_NONE;    /* last seeked position */
static gint64 seek_tol = 0.05 * GST_SECOND;     /* tolerance seek interval */
static GList *seeks;            /* list of seeks */
static gboolean got_async_done = FALSE;

/* This allow us to run the tests multiple times with different input files */
static const gchar *testfilename1 = NULL;
static const gchar *testfilename2 = NULL;
static const gchar *test_image_filename = NULL;
static EncodingProfileName current_profile = PROFILE_NONE;

#define DEFAULT_PROFILE_TYPE PROFILE_MP4
#define DURATION_TOLERANCE 0.1 * GST_SECOND

static SeekInfo *
new_seek_info (GstClockTime seeking_position, GstClockTime position)
{
  SeekInfo *info = g_slice_new0 (SeekInfo);
  info->seeking_position = seeking_position;
  info->position = position;
  return info;
}

static GstEncodingProfile *
create_profile (const char *container, const char *container_preset,
    const char *audio, const char *audio_preset, const char *video,
    const char *video_preset)
{
  GstEncodingContainerProfile *cprof = NULL;
  GstEncodingProfile *prof = NULL;
  GstCaps *caps;

  /* If we have both audio and video, we must have container */
  if (audio && video && !container)
    return NULL;

  if (container) {
    caps = gst_caps_from_string (container);
    cprof = gst_encoding_container_profile_new ("User profile", "User profile",
        caps, NULL);
    gst_caps_unref (caps);
    if (!cprof)
      return NULL;
    if (container_preset)
      gst_encoding_profile_set_preset ((GstEncodingProfile *) cprof,
          container_preset);
  }

  if (audio) {
    caps = gst_caps_from_string (audio);
    prof = (GstEncodingProfile *) gst_encoding_audio_profile_new (caps, NULL,
        NULL, 0);
    if (!prof)
      goto beach;
    if (audio_preset)
      gst_encoding_profile_set_preset (prof, audio_preset);
    if (cprof)
      gst_encoding_container_profile_add_profile (cprof, prof);
    gst_caps_unref (caps);
  }
  if (video) {
    caps = gst_caps_from_string (video);
    prof = (GstEncodingProfile *) gst_encoding_video_profile_new (caps, NULL,
        NULL, 0);
    if (!prof)
      goto beach;
    if (video_preset)
      gst_encoding_profile_set_preset (prof, video_preset);
    if (cprof)
      gst_encoding_container_profile_add_profile (cprof, prof);
    gst_caps_unref (caps);
  }

  return cprof ? (GstEncodingProfile *) cprof : (GstEncodingProfile *) prof;

beach:
  if (cprof)
    gst_encoding_profile_unref (cprof);
  else
    gst_encoding_profile_unref (prof);
  return NULL;
}

static GstEncodingProfile *
create_audio_video_profile (EncodingProfileName type)
{
  return create_profile (profile_specs[type][0], NULL, profile_specs[type][1],
      NULL, profile_specs[type][2], NULL);
}

/* This is used to specify a dot dumping after the target element started outputting buffers */
static const gchar *target_element = NULL;

static GstPadProbeReturn
dump_to_dot (GstPad * pad, GstPadProbeInfo * info)
{
  GST_DEBUG_BIN_TO_DOT_FILE (GST_BIN (pipeline), GST_DEBUG_GRAPH_SHOW_ALL,
      "pipelinestate");
  return (GST_PAD_PROBE_REMOVE);
}

static gboolean
my_bus_callback (GstBus * bus, GstMessage * message, gpointer data)
{
  gboolean *ret = (gboolean *) data;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_STATE_CHANGED:{
      GstState old_state, new_state;

      gst_message_parse_state_changed (message, &old_state, &new_state, NULL);
      /* HACK */
      if (new_state == GST_STATE_PLAYING
          && !g_strcmp0 (GST_MESSAGE_SRC_NAME (message), target_element))
        gst_pad_add_probe (gst_element_get_static_pad (GST_ELEMENT
                (GST_MESSAGE_SRC (message)), "src"), GST_PAD_PROBE_TYPE_BUFFER,
            (GstPadProbeCallback) dump_to_dot, NULL, NULL);
      break;
    }
    case GST_MESSAGE_ERROR:{
      GError *err;
      gchar *debug;

      gst_message_parse_error (message, &err, &debug);
      GST_ERROR ("Error: %s\n", err->message);
      g_error_free (err);
      g_free (debug);
      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_EOS:
      GST_INFO ("EOS\n");
      *ret = TRUE;
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_ASYNC_DONE:
      got_async_done = TRUE;
      if (GST_CLOCK_TIME_IS_VALID (seeked_position))
        seeked_position = GST_CLOCK_TIME_NONE;
      break;
    default:
      /* unhandled message */
      break;
  }
  return TRUE;
}

static gboolean
get_position (void)
{
  GList *tmp;
  gint64 position;
  gst_element_query_position (GST_ELEMENT (pipeline), GST_FORMAT_TIME,
      &position);
  tmp = seeks;

  GST_LOG ("Current position: %" GST_TIME_FORMAT, GST_TIME_ARGS (position));
  while (tmp) {
    SeekInfo *seek = tmp->data;
    if ((position >= (seek->seeking_position - seek_tol))
        && (position <= (seek->seeking_position + seek_tol))) {

      fail_if (GST_CLOCK_TIME_IS_VALID (seeked_position));
      got_async_done = FALSE;

      GST_INFO ("seeking to: %" GST_TIME_FORMAT,
          GST_TIME_ARGS (seek->position));

      seeked_position = seek->position;
      fail_unless (gst_element_seek_simple (GST_ELEMENT (pipeline),
              GST_FORMAT_TIME,
              GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE, seek->position));

      seeks = g_list_remove_link (seeks, tmp);
      g_slice_free (SeekInfo, seek);
      g_list_free (tmp);
      break;
    }
    tmp = tmp->next;
  }
  return TRUE;
}

static gboolean
check_rendered_file_properties (gchar * render_file, GstClockTime duration)
{
  GESUriClipAsset *asset;
  GstDiscovererInfo *info;
  GError *error = NULL;
  GstClockTime real_duration;

  /* TODO: extend these tests */

  GST_INFO ("Checking rendered file: %s", render_file);
  asset = ges_uri_clip_asset_request_sync (render_file, &error);

  info = ges_uri_clip_asset_get_info (GES_URI_CLIP_ASSET (asset));
  gst_object_unref (asset);

  if (!(GST_IS_DISCOVERER_INFO (info)))
    return FALSE;

  /* Let's not be too nazi */

  real_duration = gst_discoverer_info_get_duration (info);

  if ((duration < real_duration - DURATION_TOLERANCE)
      || (duration > real_duration + DURATION_TOLERANCE))
    return FALSE;

  gst_object_unref (info);

  return TRUE;
}

static gboolean
check_timeline (GESTimeline * timeline)
{
  GstBus *bus;
  gint64 duration;
  static gboolean ret;
  GstEncodingProfile *profile;
  gchar *render_uri = NULL;

  ret = FALSE;

  ges_timeline_commit (timeline);
  pipeline = ges_timeline_pipeline_new ();
  if (current_profile != PROFILE_NONE) {
    gchar *filename = g_strdup_printf ("render.%s",
        profile_specs[current_profile][3]);

    render_uri = ges_test_file_name (filename);
    g_free (filename);

    profile = create_audio_video_profile (current_profile);
    ges_timeline_pipeline_set_render_settings (pipeline, render_uri, profile);
    ges_timeline_pipeline_set_mode (pipeline, TIMELINE_MODE_RENDER);

    gst_object_unref (profile);
  }

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_watch (bus, my_bus_callback, &ret);
  gst_object_unref (bus);

  ges_timeline_pipeline_add_timeline (pipeline, timeline);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
  gst_element_get_state (GST_ELEMENT (pipeline), NULL, NULL, -1);
  fail_unless (gst_element_query_duration (GST_ELEMENT (pipeline),
          GST_FORMAT_TIME, &duration));

  if (seeks != NULL)
    g_timeout_add (50, (GSourceFunc) get_position, NULL);

  g_main_loop_run (loop);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
  gst_element_get_state (GST_ELEMENT (pipeline), NULL, NULL, -1);

  if (current_profile != PROFILE_NONE) {
    fail_unless (check_rendered_file_properties (render_uri, duration));
    g_free (render_uri);
  }

  gst_object_unref (pipeline);

  return ret;
}

/* Test seeking in various situations */
static void
run_simple_seeks_test (GESTimeline * timeline)
{
  GList *tmp;
  GESLayer *layer;
  GError *error = NULL;
  GESUriClipAsset *asset1;
  gchar *uri = ges_test_file_name (testfilename1);

  asset1 = ges_uri_clip_asset_request_sync (uri, &error);
  g_free (uri);

  fail_unless (asset1 != NULL);

  layer = ges_layer_new ();
  fail_unless (ges_timeline_add_layer (timeline, layer));
  ges_layer_add_asset (layer, GES_ASSET (asset1), 0 * GST_SECOND,
      0 * GST_SECOND, 1 * GST_SECOND, GES_TRACK_TYPE_UNKNOWN);

  gst_object_unref (asset1);

  ges_layer_add_asset (layer, GES_ASSET (asset1), 1 * GST_SECOND,
      0 * GST_SECOND, 1 * GST_SECOND, GES_TRACK_TYPE_UNKNOWN);

    /**
   * Our timeline
   *          [   E    ]
   * inpoints 0--------01--------2
   *          |  clip  |   clip  |
   * time     0--------10--------1
   */

  seeks =
      g_list_append (seeks, new_seek_info (0.2 * GST_SECOND, 0.6 * GST_SECOND));
  seeks =
      g_list_append (seeks, new_seek_info (1.0 * GST_SECOND, 1.2 * GST_SECOND));
  seeks =
      g_list_append (seeks, new_seek_info (1.5 * GST_SECOND, 1.8 * GST_SECOND));
  fail_unless (check_timeline (timeline));
  if (seeks != NULL) {
    /* free failed seeks */
    while (seeks) {
      SeekInfo *info = seeks->data;

      tmp = seeks;
      GST_ERROR ("Seeking at %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT
          " did not happen", GST_TIME_ARGS (info->seeking_position),
          GST_TIME_ARGS (info->position));
      seeks = g_list_remove_link (seeks, tmp);
      g_slice_free (SeekInfo, info);
      g_list_free (tmp);
    }
    fail_if (TRUE, "Got EOS before being able to execute all seeks");
  }
}

static void
test_seeking_audio (void)
{
  GESTimeline *timeline = ges_timeline_new ();

  fail_unless (ges_timeline_add_track (timeline,
          GES_TRACK (ges_audio_track_new ())));

  run_simple_seeks_test (timeline);
}

static void
test_seeking_video (void)
{
  GESTimeline *timeline = ges_timeline_new ();

  fail_unless (ges_timeline_add_track (timeline,
          GES_TRACK (ges_video_track_new ())));

  run_simple_seeks_test (timeline);
}

static void
test_seeking (void)
{
  GESTimeline *timeline = ges_timeline_new_audio_video ();

  run_simple_seeks_test (timeline);
}

/* Test adding an effect [E] marks the effect */
static void
test_effect (void)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GError *error = NULL;
  GESUriClipAsset *asset1;
  GESEffect *effect;
  GESClip *clip;
  gchar *uri = ges_test_file_name (testfilename1);

  asset1 = ges_uri_clip_asset_request_sync (uri, &error);
  g_free (uri);

  fail_unless (asset1 != NULL);

  layer = ges_layer_new ();
  timeline = ges_timeline_new_audio_video ();
  fail_unless (ges_timeline_add_layer (timeline, layer));

  clip =
      ges_layer_add_asset (layer, GES_ASSET (asset1), 0 * GST_SECOND,
      0 * GST_SECOND, 1 * GST_SECOND, GES_TRACK_TYPE_UNKNOWN);
  gst_object_unref (asset1);

  effect = ges_effect_new ("agingtv");
  ges_container_add (GES_CONTAINER (clip), GES_TIMELINE_ELEMENT (effect));

    /**
   * Our timeline
   *          [   E    ]
   * inpoints 0--------0
   *          |  clip  |
   * time     0--------1
   */

  fail_unless (check_timeline (timeline));
}

static void
test_transition (void)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GError *error = NULL;
  GESUriClipAsset *asset1, *asset2;
  GESClip *clip;
  gchar *uri1 = ges_test_file_name (testfilename1);
  gchar *uri2 = ges_test_file_name (testfilename2);

  timeline = ges_timeline_new_audio_video ();
  layer = ges_layer_new ();
  fail_unless (ges_timeline_add_layer (timeline, layer));

  g_object_set (layer, "auto-transition", TRUE, NULL);

  asset1 = ges_uri_clip_asset_request_sync (uri1, &error);
  asset2 = ges_uri_clip_asset_request_sync (uri2, &error);

  g_free (uri1);
  g_free (uri2);

  fail_unless (asset1 != NULL && asset2 != NULL);

  clip =
      ges_layer_add_asset (layer, GES_ASSET (asset1), 0 * GST_SECOND,
      0 * GST_SECOND, 2 * GST_SECOND, GES_TRACK_TYPE_UNKNOWN);
  gst_object_unref (asset1);

  clip =
      ges_layer_add_asset (layer, GES_ASSET (asset2), 1 * GST_SECOND,
      0 * GST_SECOND, 2 * GST_SECOND, GES_TRACK_TYPE_UNKNOWN);
  gst_object_unref (asset2);

  ges_timeline_element_set_start (GES_TIMELINE_ELEMENT (clip), 1 * GST_SECOND);

    /**
   * Our timeline
   *                    [T]
   * inpoints 0--------0 0--------0
   *          |  clip  | |  clip2 |
   * time     0------- 2 1--------3
   */

  fail_unless (check_timeline (timeline));
}

static void
test_basic (void)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESUriClipAsset *asset1;
  GError *error = NULL;
  gchar *uri = ges_test_file_name (testfilename1);

  asset1 = ges_uri_clip_asset_request_sync (uri, &error);
  g_free (uri);

  fail_unless (asset1 != NULL);

  layer = ges_layer_new ();
  timeline = ges_timeline_new_audio_video ();
  fail_unless (ges_timeline_add_layer (timeline, layer));

  ges_layer_add_asset (layer, GES_ASSET (asset1), 0 * GST_SECOND,
      0 * GST_SECOND, 1 * GST_SECOND, GES_TRACK_TYPE_UNKNOWN);
  gst_object_unref (asset1);
  /* Test most simple case */

    /**
   * Our timeline
   *
   * inpoints 0--------0
   *          |  clip  |
   * time     0--------1
   */

  fail_unless (check_timeline (timeline));
}

static void
test_image (void)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESUriClipAsset *asset1, *asset2;
  GError *error = NULL;
  gchar *uri = ges_test_file_name (test_image_filename);
  gchar *uri2 = ges_test_file_name (testfilename1);

  asset1 = ges_uri_clip_asset_request_sync (uri, &error);
  g_free (uri);

  asset2 = ges_uri_clip_asset_request_sync (uri2, &error);
  g_free (uri2);

  fail_unless (asset1 != NULL);

  layer = ges_layer_new ();
  timeline = ges_timeline_new_audio_video ();
  fail_unless (ges_timeline_add_layer (timeline, layer));

  ges_layer_add_asset (layer, GES_ASSET (asset1), 0 * GST_SECOND,
      0 * GST_SECOND, 1 * GST_SECOND, GES_TRACK_TYPE_UNKNOWN);
  gst_object_unref (asset1);
  /* Test most simple case */

  layer = ges_layer_new ();
  fail_unless (ges_timeline_add_layer (timeline, layer));

  ges_layer_add_asset (layer, GES_ASSET (asset2), 0 * GST_SECOND,
      0 * GST_SECOND, 1 * GST_SECOND, GES_TRACK_TYPE_UNKNOWN);
  gst_object_unref (asset2);


    /**
   * Our timeline
   *
   * inpoints 0--------0
   *          |  clip  |
   * time     0--------1
   */

  fail_unless (check_timeline (timeline));
}

#define CREATE_TEST(name, func, profile)                                       \
GST_START_TEST (test_##name##_mov )                                            \
{                                                                              \
  testfilename1 = "test1.MOV";                                                 \
  testfilename2 = "test2.MOV";                                                 \
  current_profile = profile;                                                   \
  func ();                                                                     \
}                                                                              \
GST_END_TEST;                                                                  \
GST_START_TEST (test_##name##_ogv )                                            \
{                                                                              \
  testfilename1 = "test1.ogv";                                                 \
  testfilename2 = "test2.ogv";                                                 \
  current_profile = profile;                                                   \
  func ();                                                                     \
}                                                                              \
GST_END_TEST;                                                                  \
GST_START_TEST (test_##name##_webm )                                           \
{                                                                              \
  testfilename1 = "test1.webm";                                                \
  testfilename2 = "test2.webm";                                                \
  current_profile = profile;                                                   \
  func ();                                                                     \
}                                                                              \
GST_END_TEST;

#define CREATE_TEST_FROM_NAMES(name, to, profile)                              \
  CREATE_TEST( name##to, test_##name, profile)

#define CREATE_RENDERING_TEST(name, func)                                      \
  CREATE_TEST_FROM_NAMES(name, _render_to_ogg, PROFILE_OGG)                    \
  CREATE_TEST_FROM_NAMES(name, _render_to_webm, PROFILE_WEBM)

#define CREATE_PLAYBACK_TEST(name)                                             \
  CREATE_TEST_FROM_NAMES(name, _playback, PROFILE_NONE)

#define CREATE_TEST_FULL(name)                                                 \
  CREATE_PLAYBACK_TEST(name)                                                   \
  CREATE_RENDERING_TEST(name, func)

#define ADD_PLAYBACK_TESTS(name)                                               \
  tcase_add_test (tc_chain, test_##name##_playback_webm);                      \
  tcase_add_test (tc_chain, test_##name##_playback_ogv);                       \
  tcase_add_test (tc_chain, test_##name##_playback_mov);                       \

#define ADD_RENDERING_TESTS(name)                                              \
  tcase_add_test (tc_chain, test_##name##_render_to_ogg_mov);                  \
  tcase_add_test (tc_chain, test_##name##_render_to_ogg_webm);                 \
  tcase_add_test (tc_chain, test_##name##_render_to_ogg_ogv);                  \
  tcase_add_test (tc_chain, test_##name##_render_to_webm_webm);                \
  tcase_add_test (tc_chain, test_##name##_render_to_webm_mov);                 \
  tcase_add_test (tc_chain, test_##name##_render_to_webm_ogv);

#define ADD_TESTS(name)                                                        \
  ADD_PLAYBACK_TESTS(name)                                                     \
  ADD_RENDERING_TESTS(name)


/* *INDENT-OFF* */
CREATE_TEST_FULL(basic)
CREATE_TEST_FULL(transition)
CREATE_TEST_FULL(effect)

CREATE_PLAYBACK_TEST(seeking)
CREATE_PLAYBACK_TEST(seeking_audio)
CREATE_PLAYBACK_TEST(seeking_video)
CREATE_PLAYBACK_TEST(image)
/* *INDENT-ON* */

static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges-render");
  TCase *tc_chain = tcase_create ("render");

  suite_add_tcase (s, tc_chain);

  ADD_TESTS (basic);
  ADD_TESTS (effect);
  ADD_TESTS (transition);

  ADD_PLAYBACK_TESTS (image);

  ADD_PLAYBACK_TESTS (seeking);
  ADD_PLAYBACK_TESTS (seeking_audio);
  ADD_PLAYBACK_TESTS (seeking_video);

  /* TODO : next test case : complex timeline created from project. */
  /* TODO : deep checking of rendered clips */
  /* TODO : might be interesting to try all profiles, and maintain a list of currently working profiles ? */

  return s;
}

static gboolean
generate_all_files (void)
{
  if (!ges_generate_test_file_audio_video ("test1.webm", "vorbisenc", "vp8enc",
          "webmmux", "18", "11"))
    return FALSE;
  if (!ges_generate_test_file_audio_video ("test2.webm", "vorbisenc", "vp8enc",
          "webmmux", "0", "0"))
    return FALSE;
  if (!ges_generate_test_file_audio_video ("test1.ogv", "vorbisenc",
          "theoraenc", "oggmux", "18", "11"))
    return FALSE;
  if (!ges_generate_test_file_audio_video ("test2.ogv", "vorbisenc",
          "theoraenc", "oggmux", "0", "0"))
    return FALSE;
  if (!ges_generate_test_file_audio_video ("test1.MOV", NULL,
          "x264enc", "qtmux", "18", "11"))
    return FALSE;
  if (!ges_generate_test_file_audio_video ("test2.MOV", NULL,
          "x264enc", "qtmux", "0", "0"))
    return FALSE;

  return TRUE;
}


int
main (int argc, char **argv)
{
  int nf;

  Suite *s = ges_suite ();

  gst_check_init (&argc, &argv);
  ges_init ();

  if (!generate_all_files ()) {
    GST_ERROR ("error generating necessary test files in rendering test\n");
    return 1;
  }


  loop = g_main_loop_new (NULL, FALSE);
  nf = gst_check_run_suite (s, "ges", __FILE__);

  return nf;
}
