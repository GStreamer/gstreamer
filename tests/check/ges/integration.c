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
static const char * const profile_specs[][3] = {
  { "application/ogg", "audio/x-vorbis", "video/x-theora" },
  { "video/webm", "audio/x-vorbis", "video/x-vp8"},
};
/* *INDENT-ON* */

typedef enum
{
  PROFILE_OGG,
  PROFILE_MP4,
} PROFILE_TYPE;

typedef struct _PresetInfos
{
  const gchar *muxer_preset_name;
  const gchar *audio_preset_name;
  const gchar *video_preset_name;

  gsize expected_size;

} PresetInfos;

static GMainLoop *loop;
static GESTimelinePipeline *pipeline = NULL;

/* This allow us to run the tests multiple times with different input files */
static const gchar *testfilename1 = NULL;
static const gchar *testfilename2 = NULL;
static const gchar *test_image_filename = NULL;

#define DEFAULT_PROFILE_TYPE PROFILE_MP4
#define DURATION_TOLERANCE 0.1 * GST_SECOND

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
create_audio_video_profile (PROFILE_TYPE type)
{
  return create_profile (profile_specs[type][0], NULL, profile_specs[type][1],
      NULL, profile_specs[type][2], NULL);
}

/* This is used to specify a dot dumping after the target element started outputting buffers */
static gchar *target_element = NULL;

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
      g_print ("Error: %s\n", err->message);
      g_error_free (err);
      g_free (debug);
      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_EOS:
      g_print ("EOS\n");
      *ret = TRUE;
      g_main_loop_quit (loop);
      break;
    default:
      /* unhandled message */
      break;
  }
  return TRUE;
}

static gboolean
test_timeline_with_profile (GESTimeline * timeline,
    PROFILE_TYPE profile_type, gboolean render)
{
  GstEncodingProfile *profile;
  GstBus *bus;
  static gboolean ret;
  gchar *uri = ges_test_file_name ("rendered.ogv");

  ret = FALSE;

  ges_timeline_commit (timeline);

  profile = create_audio_video_profile (profile_type);

  pipeline = ges_timeline_pipeline_new ();

  if (render)
    ges_timeline_pipeline_set_render_settings (pipeline, uri, profile);

  g_free (uri);
  gst_object_unref (profile);

  if (render)
    ges_timeline_pipeline_set_mode (pipeline, TIMELINE_MODE_RENDER);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_watch (bus, my_bus_callback, &ret);
  gst_object_unref (bus);

  ges_timeline_pipeline_add_timeline (pipeline, timeline);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
  gst_element_get_state (GST_ELEMENT (pipeline), NULL, NULL, -1);

  g_main_loop_run (loop);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
  gst_element_get_state (GST_ELEMENT (pipeline), NULL, NULL, -1);

  gst_object_unref (pipeline);

  return ret;
}

static gboolean
check_rendered_file_properties (GstClockTime duration)
{
  gchar *uri;
  GESUriClipAsset *asset;
  GstDiscovererInfo *info;
  GError *error = NULL;
  GstClockTime real_duration;

  /* TODO: extend these tests */

  uri = ges_test_file_name ("rendered.ogv");
  asset = ges_uri_clip_asset_request_sync (uri, &error);
  g_free (uri);

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

/* Test seeking in various situations */
static void
_seeking_playback (void)
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

  ges_layer_add_asset (layer, GES_ASSET (asset1), 1 * GST_SECOND,
      0 * GST_SECOND, 1 * GST_SECOND, GES_TRACK_TYPE_UNKNOWN);

    /**
   * Our timeline
   *          [   E    ]
   * inpoints 0--------01--------2
   *          |  clip  |   clip  |
   * time     0--------10--------1
   */

  fail_unless (test_timeline_with_profile (timeline, PROFILE_OGG, FALSE));
}

GST_START_TEST (test_seeking_playback_webm)
{
  testfilename1 = "test1.webm";
  testfilename2 = "test2.webm";
  _seeking_playback ();
}

GST_END_TEST;

GST_START_TEST (test_seeking_playback_ogv)
{
  testfilename1 = "test1.ogv";
  testfilename2 = "test2.ogv";
  _seeking_playback ();
}

GST_END_TEST;

GST_START_TEST (test_seeking_playback_mov)
{
  testfilename1 = "test1.MOV";
  testfilename2 = "test2.MOV";
  _seeking_playback ();
}

GST_END_TEST;


/* Test adding an effect [E] marks the effect */
static void
test_effect (gboolean render)
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

  fail_unless (test_timeline_with_profile (timeline, PROFILE_OGG, render));
  if (render)
    fail_unless (check_rendered_file_properties (1 * GST_SECOND));
}

static void
test_transition (gboolean render)
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

  fail_unless (test_timeline_with_profile (timeline, PROFILE_OGG, render));

  if (render)
    fail_unless (check_rendered_file_properties (3 * GST_SECOND));
}

static void
test_basic (gboolean render)
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

  fail_unless (test_timeline_with_profile (timeline, PROFILE_OGG, render));

  if (render)
    fail_unless (check_rendered_file_properties (1 * GST_SECOND));
}

static void
test_image (gboolean render)
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

  fail_unless (test_timeline_with_profile (timeline, PROFILE_OGG, render));

  if (render)
    fail_unless (check_rendered_file_properties (1 * GST_SECOND));
}

GST_START_TEST (test_basic_render_webm)
{
  testfilename1 = "test1.webm";
  testfilename2 = "test2.webm";
  test_image_filename = "test.png";
  test_basic (TRUE);
}

GST_END_TEST;

GST_START_TEST (test_basic_playback_webm)
{
  testfilename1 = "test1.webm";
  testfilename2 = "test2.webm";
  test_image_filename = "test.png";
  test_basic (FALSE);
}

GST_END_TEST;

GST_START_TEST (test_effect_render_webm)
{
  testfilename1 = "test1.webm";
  testfilename2 = "test2.webm";
  test_image_filename = "test.png";
  test_effect (TRUE);
}

GST_END_TEST;

GST_START_TEST (test_effect_playback_webm)
{
  testfilename1 = "test1.webm";
  testfilename2 = "test2.webm";
  test_image_filename = "test.png";
  test_effect (FALSE);
}

GST_END_TEST;

GST_START_TEST (test_transition_render_webm)
{
  testfilename1 = "test1.webm";
  testfilename2 = "test2.webm";
  test_image_filename = "test.png";
  test_transition (TRUE);
}

GST_END_TEST;

GST_START_TEST (test_transition_playback_webm)
{
  testfilename1 = "test1.webm";
  testfilename2 = "test2.webm";
  test_image_filename = "test.png";
  test_transition (FALSE);
}

GST_END_TEST;

GST_START_TEST (test_image_playback_webm)
{
  testfilename1 = "test1.webm";
  testfilename2 = "test2.webm";
  test_image_filename = "test.png";
  test_image (FALSE);
}

GST_END_TEST;

GST_START_TEST (test_basic_render_ogv)
{
  testfilename1 = "test1.ogv";
  testfilename2 = "test2.ogv";
  test_basic (TRUE);
}

GST_END_TEST;

GST_START_TEST (test_basic_playback_ogv)
{
  testfilename1 = "test1.ogv";
  testfilename2 = "test2.ogv";
  test_basic (FALSE);
}

GST_END_TEST;

GST_START_TEST (test_effect_render_ogv)
{
  testfilename1 = "test1.ogv";
  testfilename2 = "test2.ogv";
  test_effect (TRUE);
}

GST_END_TEST;

GST_START_TEST (test_effect_playback_ogv)
{
  testfilename1 = "test1.ogv";
  testfilename2 = "test2.ogv";
  test_effect (FALSE);
}

GST_END_TEST;

GST_START_TEST (test_transition_render_ogv)
{
  testfilename1 = "test1.ogv";
  testfilename2 = "test2.ogv";
  test_transition (TRUE);
}

GST_END_TEST;

GST_START_TEST (test_transition_playback_ogv)
{
  testfilename1 = "test1.ogv";
  testfilename2 = "test2.ogv";
  test_transition (FALSE);
}

GST_END_TEST;

GST_START_TEST (test_image_playback_ogv)
{
  testfilename1 = "test1.ogv";
  testfilename2 = "test2.ogv";
  test_image (FALSE);
}

GST_END_TEST;

GST_START_TEST (test_basic_render_mov)
{
  testfilename1 = "test1.MOV";
  testfilename2 = "test2.MOV";
  test_basic (TRUE);
}

GST_END_TEST;

GST_START_TEST (test_basic_playback_mov)
{
  testfilename1 = "test1.MOV";
  testfilename2 = "test2.MOV";
  test_basic (FALSE);
}

GST_END_TEST;

GST_START_TEST (test_effect_render_mov)
{
  testfilename1 = "test1.MOV";
  testfilename2 = "test2.MOV";
  test_effect (TRUE);
}

GST_END_TEST;

GST_START_TEST (test_effect_playback_mov)
{
  testfilename1 = "test1.MOV";
  testfilename2 = "test2.MOV";
  test_effect (FALSE);
}

GST_END_TEST;

GST_START_TEST (test_transition_render_mov)
{
  testfilename1 = "test1.MOV";
  testfilename2 = "test2.MOV";
  test_transition (TRUE);
}

GST_END_TEST;

GST_START_TEST (test_transition_playback_mov)
{
  testfilename1 = "test1.MOV";
  testfilename2 = "test2.MOV";
  test_transition (FALSE);
}

GST_END_TEST;

GST_START_TEST (test_image_playback_mov)
{
  testfilename1 = "test1.MOV";
  testfilename2 = "test2.MOV";
  test_image (FALSE);
}

GST_END_TEST;

static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges-render");
  TCase *tc_chain = tcase_create ("render");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_basic_render_webm);
  tcase_add_test (tc_chain, test_basic_render_ogv);
  tcase_add_test (tc_chain, test_basic_render_mov);

  tcase_add_test (tc_chain, test_basic_playback_webm);
  tcase_add_test (tc_chain, test_basic_playback_ogv);
  tcase_add_test (tc_chain, test_basic_playback_mov);

  tcase_add_test (tc_chain, test_effect_render_webm);
  tcase_add_test (tc_chain, test_effect_render_ogv);
  tcase_add_test (tc_chain, test_effect_render_mov);

  tcase_add_test (tc_chain, test_effect_playback_webm);
  tcase_add_test (tc_chain, test_effect_playback_ogv);
  tcase_add_test (tc_chain, test_effect_playback_mov);

  tcase_add_test (tc_chain, test_transition_render_webm);
  tcase_add_test (tc_chain, test_transition_render_ogv);
  tcase_add_test (tc_chain, test_transition_render_mov);

  tcase_add_test (tc_chain, test_transition_playback_webm);
  tcase_add_test (tc_chain, test_transition_playback_ogv);
  tcase_add_test (tc_chain, test_transition_playback_mov);

  tcase_add_test (tc_chain, test_image_playback_webm);
  tcase_add_test (tc_chain, test_image_playback_ogv);
  tcase_add_test (tc_chain, test_image_playback_mov);

  tcase_add_test (tc_chain, test_seeking_playback_webm);
  tcase_add_test (tc_chain, test_seeking_playback_ogv);
  tcase_add_test (tc_chain, test_seeking_playback_mov);

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
