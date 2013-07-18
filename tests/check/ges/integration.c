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
  { "application/ogg", "audio/x-vorbis", "video/x-theora", "assets/vorbis_theora.rendered.ogv" },
  { "video/webm", "audio/x-vorbis", "video/x-vp8", "assets/vorbis_vp8.rendered.webm"},
  { "video/quicktime,variant=iso", "audio/mpeg,mpegversion=1,layer=3", "video/x-h264",  "assets/aac_h264.rendered.mov"},
  { "video/x-matroska", "audio/x-vorbis", "video/x-h264", "assets/vorbis_h264.rendered.mkv"},
};
/* *INDENT-ON* */

typedef enum
{
  PROFILE_NONE = -1,
  PROFILE_VORBIS_THEORA_OGG,
  PROFILE_VORBIS_VP8_WEBM,
  PROFILE_AAC_H264_QUICKTIME,
  PROFILE_VORBIS_H264_MATROSKA,
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
static GESPipeline *pipeline = NULL;
static gint64 seeked_position = GST_CLOCK_TIME_NONE;    /* last seeked position */
static gint64 seek_tol = 0.05 * GST_SECOND;     /* tolerance seek interval */
static GList *seeks;            /* list of seeks */
static gboolean got_async_done = FALSE;
static gboolean seek_paused = FALSE, seek_paused_noplay = FALSE;

/* This allow us to run the tests multiple times with different input files */
static const gchar *testfilename1 = NULL;
static const gchar *testfilename2 = NULL;
static const gchar *test_image_filename = NULL;
static EncodingProfileName current_profile = PROFILE_NONE;

#define DURATION_TOLERANCE 0.1 * GST_SECOND

#define get_asset(filename, asset)                                            \
{                                                                              \
  GError *error = NULL;                                                        \
  gchar *uri = ges_test_file_name (filename);                                  \
  asset = ges_uri_clip_asset_request_sync (uri, &error);                       \
  fail_unless (GES_IS_ASSET (asset), "Testing file %s could not be used as an "\
      "asset -- Reason: %s", uri, error ? error->message : "Uknown");          \
  g_free (uri);                                                                \
}

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
static const gchar *target_element = "smart-mixer-mixer";

static GstPadProbeReturn
dump_to_dot (GstPad * pad, GstPadProbeInfo * info)
{
  GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
      GST_DEBUG_GRAPH_SHOW_ALL, "ges-integration-smart-mixer-push-buffer");
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
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
          GST_DEBUG_GRAPH_SHOW_ALL, "ges-integration-error");
      fail_unless (FALSE, "Got an error on the bus: Source: %s, message: %s\n",
          GST_MESSAGE_SRC_NAME (message), err ? err->message : "Uknown");
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
      if (seek_paused) {
        gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PAUSED);
        gst_element_get_state (GST_ELEMENT (pipeline), NULL, NULL, -1);
      }
      fail_unless (gst_element_seek_simple (GST_ELEMENT (pipeline),
              GST_FORMAT_TIME,
              GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE, seek->position));

      if (seek_paused) {
        gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
        gst_element_get_state (GST_ELEMENT (pipeline), NULL, NULL, -1);
      }
      seeks = g_list_remove_link (seeks, tmp);
      g_slice_free (SeekInfo, seek);
      g_list_free (tmp);
      break;
    }
    tmp = tmp->next;
  }
  /* if seeking paused without playing and we reached the last seek, just play
   * till the end */
  if (!tmp && seek_paused_noplay) {
    gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
    gst_element_get_state (GST_ELEMENT (pipeline), NULL, NULL, -1);
  }
  return TRUE;
}

static void
check_rendered_file_properties (const gchar * render_file,
    GstClockTime duration)
{
  GESUriClipAsset *asset;
  GstDiscovererInfo *info;
  GstClockTime real_duration;

  /* TODO: extend these tests */

  get_asset (render_file, asset);
  info = ges_uri_clip_asset_get_info (GES_URI_CLIP_ASSET (asset));
  gst_object_unref (asset);

  fail_unless (GST_IS_DISCOVERER_INFO (info), "Could not discover file %s",
      render_file);

  /* Let's not be too nazi */

  real_duration = gst_discoverer_info_get_duration (info);

  fail_if ((real_duration < duration - DURATION_TOLERANCE)
      || (real_duration > duration + DURATION_TOLERANCE), "Duration %"
      GST_TIME_FORMAT " not in range [%" GST_TIME_FORMAT " -- %"
      GST_TIME_FORMAT "]", GST_TIME_ARGS (real_duration),
      GST_TIME_ARGS (duration - DURATION_TOLERANCE),
      GST_TIME_ARGS (duration + DURATION_TOLERANCE));


  gst_object_unref (info);
}

static gboolean
check_timeline (GESTimeline * timeline)
{
  GstBus *bus;
  static gboolean ret;
  GstEncodingProfile *profile;
  gchar *render_uri = NULL;

  ret = FALSE;

  ges_timeline_commit (timeline);
  pipeline = ges_pipeline_new ();
  if (current_profile != PROFILE_NONE) {
    render_uri = ges_test_file_name (profile_specs[current_profile][3]);

    profile = create_audio_video_profile (current_profile);
    ges_pipeline_set_render_settings (pipeline, render_uri, profile);
    ges_pipeline_set_mode (pipeline, TIMELINE_MODE_RENDER);


    gst_object_unref (profile);
  } else if (g_getenv ("GES_MUTE_TESTS")) {
    GstElement *sink = gst_element_factory_make ("fakesink", NULL);

    g_object_set (sink, "sync", TRUE, NULL);
    ges_pipeline_preview_set_audio_sink (pipeline, sink);

    sink = gst_element_factory_make ("fakesink", NULL);
    g_object_set (sink, "sync", TRUE, NULL);
    ges_pipeline_preview_set_video_sink (pipeline, sink);
  }

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_watch (bus, my_bus_callback, &ret);
  gst_object_unref (bus);

  ges_pipeline_add_timeline (pipeline, timeline);
  if (!seek_paused_noplay) {
    gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
    gst_element_get_state (GST_ELEMENT (pipeline), NULL, NULL, -1);
  }
  GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
      GST_DEBUG_GRAPH_SHOW_ALL, "ges-integration-playing");
  if (seeks != NULL)
    g_timeout_add (50, (GSourceFunc) get_position, NULL);

  g_main_loop_run (loop);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
  gst_element_get_state (GST_ELEMENT (pipeline), NULL, NULL, -1);

  if (current_profile != PROFILE_NONE) {
    check_rendered_file_properties (profile_specs[current_profile][3],
        ges_timeline_get_duration (timeline));
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
  GESUriClipAsset *asset1;

  get_asset (testfilename1, asset1);

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
  if (!seek_paused_noplay) {
    seeks =
        g_list_append (seeks, new_seek_info (0.2 * GST_SECOND,
            0.6 * GST_SECOND));
    seeks =
        g_list_append (seeks, new_seek_info (1.0 * GST_SECOND,
            1.2 * GST_SECOND));
    seeks =
        g_list_append (seeks, new_seek_info (1.5 * GST_SECOND,
            1.8 * GST_SECOND));
  } else {
    /* if pipeline is not playing, let's make point-to-point seeks */
    seeks =
        g_list_append (seeks, new_seek_info (0.2 * GST_SECOND,
            0.6 * GST_SECOND));
    seeks =
        g_list_append (seeks, new_seek_info (0.6 * GST_SECOND,
            1.2 * GST_SECOND));
    seeks =
        g_list_append (seeks, new_seek_info (1.2 * GST_SECOND,
            1.8 * GST_SECOND));
  }
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

  seek_paused = FALSE;
  seek_paused_noplay = FALSE;
  run_simple_seeks_test (timeline);
}

static void
test_seeking_video (void)
{
  GESTimeline *timeline = ges_timeline_new ();

  fail_unless (ges_timeline_add_track (timeline,
          GES_TRACK (ges_video_track_new ())));

  seek_paused = FALSE;
  seek_paused_noplay = FALSE;
  run_simple_seeks_test (timeline);
}

static void
test_seeking (void)
{
  GESTimeline *timeline = ges_timeline_new_audio_video ();

  seek_paused = FALSE;
  seek_paused_noplay = FALSE;
  run_simple_seeks_test (timeline);
}

static void
test_seeking_paused_audio (void)
{
  GESTimeline *timeline = ges_timeline_new ();

  fail_unless (ges_timeline_add_track (timeline,
          GES_TRACK (ges_audio_track_new ())));

  seek_paused = TRUE;
  seek_paused_noplay = FALSE;
  run_simple_seeks_test (timeline);
}

static void
test_seeking_paused_video (void)
{
  GESTimeline *timeline = ges_timeline_new ();

  fail_unless (ges_timeline_add_track (timeline,
          GES_TRACK (ges_video_track_new ())));

  seek_paused = TRUE;
  seek_paused_noplay = FALSE;
  run_simple_seeks_test (timeline);
}

static void
test_seeking_paused (void)
{
  GESTimeline *timeline = ges_timeline_new_audio_video ();

  seek_paused = TRUE;
  seek_paused_noplay = FALSE;
  run_simple_seeks_test (timeline);
}

static void
test_seeking_paused_audio_noplay (void)
{
  GESTimeline *timeline = ges_timeline_new ();

  fail_unless (ges_timeline_add_track (timeline,
          GES_TRACK (ges_audio_track_new ())));

  seek_paused = FALSE;
  seek_paused_noplay = TRUE;
  run_simple_seeks_test (timeline);
}

static void
test_seeking_paused_video_noplay (void)
{
  GESTimeline *timeline = ges_timeline_new ();

  fail_unless (ges_timeline_add_track (timeline,
          GES_TRACK (ges_video_track_new ())));

  seek_paused = FALSE;
  seek_paused_noplay = TRUE;
  run_simple_seeks_test (timeline);
}

static void
test_seeking_paused_noplay (void)
{
  GESTimeline *timeline = ges_timeline_new_audio_video ();

  seek_paused = FALSE;
  seek_paused_noplay = TRUE;
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
  fail_unless (GES_IS_ASSET (asset1), "Testing file %s could not be used as an "
      "asset -- Reason: %s", uri, error ? error->message : "Uknown");

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
  GESUriClipAsset *asset1, *asset2;
  GESClip *clip;

  timeline = ges_timeline_new_audio_video ();
  layer = ges_layer_new ();
  fail_unless (ges_timeline_add_layer (timeline, layer));

  g_object_set (layer, "auto-transition", TRUE, NULL);

  get_asset (testfilename1, asset1);
  get_asset (testfilename2, asset2);

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
run_basic (GESTimeline * timeline)
{
  GESLayer *layer;
  GESUriClipAsset *asset1;

  get_asset (testfilename1, asset1);
  layer = ges_layer_new ();
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
test_basic (void)
{
  run_basic (ges_timeline_new_audio_video ());
}

static void
test_basic_audio (void)
{
  GESTimeline *timeline = ges_timeline_new ();

  fail_unless (ges_timeline_add_track (timeline,
          GES_TRACK (ges_audio_track_new ())));

  run_basic (timeline);
}

static void
test_basic_video (void)
{
  GESTimeline *timeline = ges_timeline_new ();

  fail_unless (ges_timeline_add_track (timeline,
          GES_TRACK (ges_video_track_new ())));

  run_basic (timeline);
}

static void
test_image (void)
{
  GESTimeline *timeline;
  GESLayer *layer;
  GESUriClipAsset *asset1, *asset2;

  get_asset (test_image_filename, asset1);
  get_asset (testfilename1, asset2);

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

static gboolean
test_mix_layers (GESTimeline * timeline, GESUriClipAsset ** assets,
    guint32 num_assets, guint32 num_layers)
{
  GESLayer *layer;
  GESClip *clip;
  GList *children, *tmp;
  GESTrackElement *track_element;
  GESTrackType track_type;
  GESUriClipAsset *asset;
  guint32 i, j;
  gfloat step = 1.0 / num_layers;

  for (i = 0; i < num_layers; i++) {
    layer = ges_timeline_append_layer (timeline);
    fail_unless (layer != NULL);

    for (j = 0; j < num_assets; j++) {
      asset = assets[j];

      clip =
          ges_layer_add_asset (layer, GES_ASSET (asset),
          (i * step + j) * GST_SECOND, 0 * GST_SECOND, 1 * GST_SECOND,
          GES_TRACK_TYPE_UNKNOWN);
      fail_unless (clip != NULL);

      children = ges_container_get_children (GES_CONTAINER (clip), FALSE);

      for (tmp = children; tmp; tmp = tmp->next) {
        track_element = GES_TRACK_ELEMENT (tmp->data);
        track_type = ges_track_element_get_track_type (track_element);

        switch (track_type) {
          case GES_TRACK_TYPE_VIDEO:
            ges_track_element_set_child_properties (track_element, "alpha",
                (gdouble) (num_layers - 1 - i) * step, NULL);
            break;
          case GES_TRACK_TYPE_AUDIO:
            ges_track_element_set_child_properties (track_element, "volume",
                (gdouble) (num_layers - 1 - i) * step, NULL);
            break;
          default:
            break;
        }
      }
    }
  }
  return TRUE;
}


static void
test_mixing (void)
{
  GESTimeline *timeline;
  GESUriClipAsset *asset[2];
  GError *error = NULL;

  gchar *uri1 = ges_test_file_name (testfilename1);
  gchar *uri2 = ges_test_file_name (testfilename1);

  timeline = ges_timeline_new_audio_video ();

  asset[0] = ges_uri_clip_asset_request_sync (uri1, &error);
  asset[1] = ges_uri_clip_asset_request_sync (uri2, &error);

  g_free (uri1);
  g_free (uri2);

  /* we are only using the first asset / clip for now */
  fail_unless (test_mix_layers (timeline, asset, 1, 4));

    /**
   * Our timeline has 4 layers
   *
   * inpoints 0--------0
   *          |  clip  |
   * time     0--------1
   * inpoints    0--------0
   *             |  clip  |
   * time        0.25--1.25
   * inpoints       0--------0
   *                |  clip  |
   * time           0.5----1.5
   * inpoints          0--------0
   *                   |  clip  |
   * time              0.75--1.75
   */

  fail_unless (check_timeline (timeline));

}


#define CREATE_TEST(name, func, profile)                                       \
GST_START_TEST (test_##name##_raw_h264_mov)                                    \
{                                                                              \
  g_print("running test_%s_%s\n", #name, "raw_h264_mov");		       \
  testfilename1 = "raw_h264.0.mov";                                            \
  testfilename2 = "raw_h264.1.mov";                                            \
  test_image_filename = "test.png";                                            \
  func ();                                                                     \
}                                                                              \
GST_END_TEST;                                                                  \
GST_START_TEST (test_##name##_vorbis_theora_ogv)                               \
{                                                                              \
  g_print("running test_%s_%s\n", #name, "vorbis_theora_ogv");		       \
  testfilename1 = "vorbis_theora.0.ogg";                                       \
  testfilename2 = "vorbis_theora.1.ogg";                                       \
  test_image_filename = "test.png";                                            \
  func ();                                                                     \
}                                                                              \
GST_END_TEST;                                                                  \
GST_START_TEST (test_##name##_vorbis_vp8_webm)                                 \
{                                                                              \
  g_print("running test_%s_%s\n", #name, "vorbis_vp8_webm");		       \
  testfilename1 = "vorbis_vp8.0.webm";                                         \
  testfilename2 = "vorbis_vp8.1.webm";                                         \
  test_image_filename = "test.png";                                            \
  current_profile = profile;                                                   \
  func ();                                                                     \
}                                                                              \
GST_END_TEST;                                                                  \
GST_START_TEST (test_##name##_mp3_h264_mov)                                    \
{                                                                              \
  g_print("running test_%s_%s\n", #name, "mp3_h264_mov");		       \
  testfilename1 = "mp3_h264.0.mov";                                            \
  testfilename2 = "mp3_h264.1.mov";                                            \
  current_profile = profile;                                                   \
  test_image_filename = "test.png";                                            \
  func ();                                                                     \
}                                                                              \
GST_END_TEST;

#define CREATE_TEST_FROM_NAMES(name, to, profile)                              \
  CREATE_TEST( name##to, test_##name, profile)

#define CREATE_RENDERING_TEST(name, func)                                      \
  CREATE_TEST_FROM_NAMES(name, _render_to_vorbis_theora_ogg, PROFILE_VORBIS_THEORA_OGG)   \
  CREATE_TEST_FROM_NAMES(name, _render_to_vorbis_vp8_webm, PROFILE_VORBIS_VP8_WEBM)       \
  CREATE_TEST_FROM_NAMES(name, _render_to_aac_h264_quicktime, PROFILE_AAC_H264_QUICKTIME) \
  CREATE_TEST_FROM_NAMES(name, _render_to_vorbis_h264_matroska, PROFILE_VORBIS_H264_MATROSKA)

#define CREATE_PLAYBACK_TEST(name)                                             \
  CREATE_TEST_FROM_NAMES(name, _playback, PROFILE_NONE)

#define CREATE_TEST_FULL(name)                                                 \
  CREATE_PLAYBACK_TEST(name)                                                   \
  CREATE_RENDERING_TEST(name, func)

#define ADD_PLAYBACK_TESTS(name)                                               \
  tcase_add_test (tc_chain, test_##name##_playback_vorbis_vp8_webm);           \
  tcase_add_test (tc_chain, test_##name##_playback_vorbis_theora_ogv);         \
  tcase_add_test (tc_chain, test_##name##_playback_raw_h264_mov);              \
  tcase_add_test (tc_chain, test_##name##_playback_mp3_h264_mov);

#define ADD_RENDERING_TESTS(name)                                              \
  tcase_add_test (tc_chain, test_##name##_render_to_vorbis_theora_ogg_raw_h264_mov);      \
  tcase_add_test (tc_chain, test_##name##_render_to_vorbis_theora_ogg_mp3_h264_mov);      \
  tcase_add_test (tc_chain, test_##name##_render_to_vorbis_theora_ogg_vorbis_vp8_webm);   \
  tcase_add_test (tc_chain, test_##name##_render_to_vorbis_theora_ogg_vorbis_theora_ogv); \
  tcase_add_test (tc_chain, test_##name##_render_to_vorbis_vp8_webm_vorbis_vp8_webm);     \
  tcase_add_test (tc_chain, test_##name##_render_to_vorbis_vp8_webm_raw_h264_mov);        \
  tcase_add_test (tc_chain, test_##name##_render_to_vorbis_vp8_webm_mp3_h264_mov);        \
  tcase_add_test (tc_chain, test_##name##_render_to_vorbis_vp8_webm_vorbis_theora_ogv);   \
  tcase_add_test (tc_chain, test_##name##_render_to_aac_h264_quicktime_raw_h264_mov);      \
  tcase_add_test (tc_chain, test_##name##_render_to_aac_h264_quicktime_vorbis_theora_ogv);   \
  tcase_add_test (tc_chain, test_##name##_render_to_aac_h264_quicktime_vorbis_vp8_webm);   \
  tcase_add_test (tc_chain, test_##name##_render_to_aac_h264_quicktime_mp3_h264_mov); \
  tcase_add_test (tc_chain, test_##name##_render_to_vorbis_h264_matroska_raw_h264_mov);      \
  tcase_add_test (tc_chain, test_##name##_render_to_vorbis_h264_matroska_vorbis_theora_ogv);   \
  tcase_add_test (tc_chain, test_##name##_render_to_vorbis_h264_matroska_vorbis_vp8_webm);   \
  tcase_add_test (tc_chain, test_##name##_render_to_vorbis_h264_matroska_mp3_h264_mov);

#define ADD_TESTS(name)                                                        \
  ADD_PLAYBACK_TESTS(name)                                                     \
  ADD_RENDERING_TESTS(name)


/* *INDENT-OFF* */
CREATE_TEST_FULL(basic)
CREATE_TEST_FULL(basic_audio)
CREATE_TEST_FULL(basic_video)
CREATE_TEST_FULL(transition)
CREATE_TEST_FULL(effect)
CREATE_TEST_FULL(mixing)

CREATE_PLAYBACK_TEST(seeking)
CREATE_PLAYBACK_TEST(seeking_audio)
CREATE_PLAYBACK_TEST(seeking_video)
CREATE_PLAYBACK_TEST(seeking_paused)
CREATE_PLAYBACK_TEST(seeking_paused_audio)
CREATE_PLAYBACK_TEST(seeking_paused_video)
CREATE_PLAYBACK_TEST(seeking_paused_noplay)
CREATE_PLAYBACK_TEST(seeking_paused_audio_noplay)
CREATE_PLAYBACK_TEST(seeking_paused_video_noplay)
CREATE_PLAYBACK_TEST(image)
/* *INDENT-ON* */

static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges-render");
  TCase *tc_chain = tcase_create ("render");

  suite_add_tcase (s, tc_chain);

  ADD_TESTS (basic);
  ADD_TESTS (basic_audio);
  ADD_TESTS (basic_video);

  ADD_TESTS (effect);
  ADD_TESTS (transition);

  ADD_TESTS (mixing);

  ADD_PLAYBACK_TESTS (image);

  ADD_PLAYBACK_TESTS (seeking);
  ADD_PLAYBACK_TESTS (seeking_audio);
  ADD_PLAYBACK_TESTS (seeking_video);

  ADD_PLAYBACK_TESTS (seeking_paused);
  ADD_PLAYBACK_TESTS (seeking_paused_audio);
  ADD_PLAYBACK_TESTS (seeking_paused_video);

  ADD_PLAYBACK_TESTS (seeking_paused_noplay);
  ADD_PLAYBACK_TESTS (seeking_paused_audio_noplay);
  ADD_PLAYBACK_TESTS (seeking_paused_video_noplay);

  /* TODO : next test case : complex timeline created from project. */
  /* TODO : deep checking of rendered clips */
  /* TODO : might be interesting to try all profiles, and maintain a list of currently working profiles ? */

  return s;
}

static gboolean
generate_all_files (void)
{
  if (!ges_generate_test_file_audio_video ("assets/vorbis_vp8.0.webm",
          "vorbisenc", "vp8enc", "webmmux", "18", "11"))
    return FALSE;
  if (!ges_generate_test_file_audio_video ("assets/vorbis_vp8.1.webm",
          "vorbisenc", "vp8enc", "webmmux", "0", "0"))
    return FALSE;
  if (!ges_generate_test_file_audio_video ("assets/vorbis_theora.0.ogg",
          "vorbisenc", "theoraenc", "oggmux", "18", "11"))
    return FALSE;
  if (!ges_generate_test_file_audio_video ("assets/vorbis_theora.1.ogg",
          "vorbisenc", "theoraenc", "oggmux", "0", "0"))
    return FALSE;
  if (!ges_generate_test_file_audio_video ("assets/raw_h264.0.mov", NULL,
          "x264enc", "qtmux", "18", "11"))
    return FALSE;
  if (!ges_generate_test_file_audio_video ("assets/raw_h264.1.mov", NULL,
          "x264enc", "qtmux", "0", "0"))
    return FALSE;
  if (!ges_generate_test_file_audio_video ("assets/mp3_h264.0.mov",
          "lamemp3enc", "x264enc", "qtmux", "18", "11"))
    return FALSE;
  if (!ges_generate_test_file_audio_video ("assets/mp3_h264.1.mov",
          "lamemp3enc", "x264enc", "qtmux", "0", "0"))
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
