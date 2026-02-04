/* GStreamer unit tests for discoverer
 *
 * Copyright (C) 2011 Stefan Kost <ensonic@users.sf.net>
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <gst/check/gstcheck.h>
#include <gst/pbutils/pbutils.h>

#include <stdio.h>
#include <glib/gstdio.h>
#include <glib/gprintf.h>

static gboolean have_theora, have_ogg;

GST_START_TEST (test_disco_init)
{
  GError *err = NULL;
  GstDiscoverer *dc;

  dc = gst_discoverer_new (GST_SECOND, &err);
  fail_unless (dc != NULL);
  fail_unless (err == NULL);

  g_object_unref (dc);
}

GST_END_TEST;

GST_START_TEST (test_disco_serializing)
{
  GError *err = NULL;
  GstDiscoverer *dc;
  GstDiscovererInfo *info, *dinfo;
  gchar *uri;
  GVariant *serialized, *reserialized;
  GList *audio_streams;
  gchar *path =
      g_build_filename (GST_TEST_FILES_PATH, "theora-vorbis.ogg", NULL);

  /* high timeout, in case we're running under valgrind */
  dc = gst_discoverer_new (30 * GST_SECOND, &err);
  fail_unless (dc != NULL);
  fail_unless (err == NULL);

  uri = gst_filename_to_uri (path, &err);
  g_free (path);
  fail_unless (err == NULL);

  info = gst_discoverer_discover_uri (dc, uri, &err);
  fail_unless (info);
  if (have_theora && have_ogg) {
    fail_unless_equals_int (gst_discoverer_info_get_result (info),
        GST_DISCOVERER_OK);
  } else {
    fail_unless_equals_int (gst_discoverer_info_get_result (info),
        GST_DISCOVERER_MISSING_PLUGINS);
    g_clear_error (&err);
    goto missing_plugins;
  }
  serialized =
      gst_discoverer_info_to_variant (info, GST_DISCOVERER_SERIALIZE_ALL);


  fail_unless (serialized);
  dinfo = gst_discoverer_info_from_variant (serialized);

  fail_unless (dinfo);
  audio_streams = gst_discoverer_info_get_audio_streams (dinfo);
  fail_unless_equals_int (g_list_length (audio_streams), 1);
  gst_discoverer_stream_info_list_free (audio_streams);

  reserialized =
      gst_discoverer_info_to_variant (dinfo, GST_DISCOVERER_SERIALIZE_ALL);

  fail_unless (g_variant_equal (serialized, reserialized));

  gst_discoverer_info_unref (dinfo);
  g_variant_unref (serialized);
  g_variant_unref (reserialized);

missing_plugins:

  gst_discoverer_info_unref (info);
  g_object_unref (dc);
  g_free (uri);
}

GST_END_TEST;

GST_START_TEST (test_disco_sync)
{
  GError *err = NULL;
  GstDiscoverer *dc;
  GstDiscovererInfo *info;
  GstDiscovererResult result;
  gchar *uri;

  /* high timeout, in case we're running under valgrind */
  dc = gst_discoverer_new (30 * GST_SECOND, &err);
  fail_unless (dc != NULL);
  fail_unless (err == NULL);

  /* GST_TEST_FILE comes from makefile CFLAGS */
  GST_INFO ("discovering file '%s'", GST_TEST_FILE);
  uri = g_filename_to_uri (GST_TEST_FILE, NULL, &err);
  fail_unless (err == NULL);
  GST_INFO ("discovering uri '%s'", uri);

  info = gst_discoverer_discover_uri (dc, uri, &err);
  result = gst_discoverer_info_get_result (info);
  GST_INFO ("result: %d", result);
  gst_discoverer_info_unref (info);
  g_free (uri);

  if (err) {
    /* we won't have the codec for the jpeg */
    g_error_free (err);
  }

  g_object_unref (dc);
}

GST_END_TEST;
static void
test_disco_sync_reuse (const gchar * test_fn, guint num, GstClockTime timeout)
{
  GError *err = NULL;
  GstDiscoverer *dc;
  GstDiscovererInfo *info;
  GstDiscovererResult result;
  gchar *uri, *path;
  int i;

  dc = gst_discoverer_new (timeout, &err);
  fail_unless (dc != NULL);
  fail_unless (err == NULL);

  /* GST_TEST_FILE comes from makefile CFLAGS */
  path = g_build_filename (GST_TEST_FILES_PATH, test_fn, NULL);
  uri = gst_filename_to_uri (path, &err);
  g_free (path);
  fail_unless (err == NULL);

  for (i = 0; i < num; ++i) {
    GST_INFO ("[%02d] discovering uri '%s'", i, uri);
    info = gst_discoverer_discover_uri (dc, uri, &err);
    if (info) {
      result = gst_discoverer_info_get_result (info);
      GST_INFO ("result: %d", result);
      gst_discoverer_info_unref (info);
    }
    /* in case we don't have some of the elements needed */
    if (err) {
      g_error_free (err);
      err = NULL;
    }
  }
  g_free (uri);

  g_object_unref (dc);
}

GST_START_TEST (test_disco_sync_reuse_ogg)
{
  test_disco_sync_reuse ("theora-vorbis.ogg", 2, 10 * GST_SECOND);
}

GST_END_TEST;

GST_START_TEST (test_disco_sync_reuse_mp3)
{
  /* this will cause errors because -base doesn't do mp3 parsing or decoding */
  test_disco_sync_reuse ("test.mp3", 3, 10 * GST_SECOND);
}

GST_END_TEST;

GST_START_TEST (test_disco_sync_reuse_timeout)
{
  /* set minimum timeout to test that, esp. leakage under valgrind */
  /* FIXME: should really be even shorter */
  test_disco_sync_reuse ("theora-vorbis.ogg", 2, GST_SECOND);
}

GST_END_TEST;

GST_START_TEST (test_disco_missing_plugins)
{
  const gchar *files[] = { "test.mkv", "test.mp3", "partialframe.mjpeg" };
  GError *err = NULL;
  GstDiscoverer *dc;
  GstDiscovererInfo *info;
  GstDiscovererResult result;
  gchar *uri, *path;
  int i;

  for (i = 0; i < G_N_ELEMENTS (files); ++i) {
    dc = gst_discoverer_new (5 * GST_SECOND, &err);
    fail_unless (dc != NULL);
    fail_unless (err == NULL);

    /* GST_TEST_FILE comes from makefile CFLAGS */
    path = g_build_filename (GST_TEST_FILES_PATH, files[i], NULL);
    uri = gst_filename_to_uri (path, &err);
    g_free (path);
    fail_unless (err == NULL);

    GST_INFO ("discovering uri '%s'", uri);
    info = gst_discoverer_discover_uri (dc, uri, &err);
    fail_unless (info != NULL);
    fail_unless (err != NULL);
    result = gst_discoverer_info_get_result (info);
    GST_INFO ("result: %d, error message: %s", result, err->message);
    fail_unless_equals_int (result, GST_DISCOVERER_MISSING_PLUGINS);
#ifndef GST_DISABLE_DEPRECATED
    GST_INFO ("misc: %" GST_PTR_FORMAT, gst_discoverer_info_get_misc (info));
#endif

    gst_discoverer_info_unref (info);
    g_error_free (err);
    err = NULL;
    g_free (uri);
    g_object_unref (dc);
  }
}

GST_END_TEST;

typedef struct _AsyncTestData
{
  gchar *uri;
  GMainLoop *loop;
  GstDiscovererResult result;
} AsyncTestData;

static void
discovered_cb (GstDiscoverer * discoverer,
    GstDiscovererInfo * info, GError * err, AsyncTestData * data)
{
  const gchar *uri = gst_discoverer_info_get_uri (info);

  fail_unless_equals_string (data->uri, uri);

  /* cannot ensure GST_DISCOVERER_OK since there might be missing plugins */
  data->result = gst_discoverer_info_get_result (info);

  g_main_loop_quit (data->loop);
}

static void
test_disco_async_with_context (GMainContext * context, guint num)
{
  GstDiscoverer *dc;
  GError *err = NULL;
  AsyncTestData data = { 0, };
  gchar *path =
      g_build_filename (GST_TEST_FILES_PATH, "theora-vorbis.ogg", NULL);

  if (context)
    g_main_context_push_thread_default (context);

  data.uri = gst_filename_to_uri (path, &err);
  /* something wrong if we have error here */
  fail_unless (err == NULL);
  g_free (path);

  data.loop = g_main_loop_new (context, FALSE);

  /* high timeout, in case we're running under valgrind */
  dc = gst_discoverer_new (30 * GST_SECOND, &err);
  fail_unless (dc != NULL);
  fail_unless (err == NULL);

  g_signal_connect (dc, "discovered", G_CALLBACK (discovered_cb), &data);

  gst_discoverer_start (dc);

  for (guint i = 0; i < num; ++i) {
    fail_unless (gst_discoverer_discover_uri_async (dc, data.uri) == TRUE);

    g_main_loop_run (data.loop);

    if (have_theora && have_ogg) {
      fail_unless_equals_int (data.result, GST_DISCOVERER_OK);
    } else {
      fail_unless_equals_int (data.result, GST_DISCOVERER_MISSING_PLUGINS);
    }
  }

  gst_discoverer_stop (dc);
  g_object_unref (dc);
  g_free (data.uri);

  g_main_loop_unref (data.loop);

  if (context)
    g_main_context_pop_thread_default (context);
}

GST_START_TEST (test_disco_async)
{
  /* use default GMainContext */
  test_disco_async_with_context (NULL, 1);
}

GST_END_TEST;

GST_START_TEST (test_disco_async_reuse)
{
  /* use default GMainContext */
  test_disco_async_with_context (NULL, 3);
}

GST_END_TEST;

typedef struct _CustomContextData
{
  GMutex lock;
  GCond cond;
  gboolean finish;
} CustomContextData;


static gpointer
custom_context_thread_func (CustomContextData * data)
{
  GMainContext *context;

  /* test async APIs with custom GMainContext */
  context = g_main_context_new ();
  test_disco_async_with_context (context, 1);
  g_main_context_unref (context);

  data->finish = TRUE;
  g_cond_signal (&data->cond);

  return NULL;
}

GST_START_TEST (test_disco_async_custom_context)
{
  GThread *thread;
  CustomContextData data;

  g_mutex_init (&data.lock);
  g_cond_init (&data.cond);
  data.finish = FALSE;

  /* ensure default context here, but we will use other thread default context
   * instead of this */
  g_main_context_default ();

  thread = g_thread_new ("test-custom-context-thread",
      (GThreadFunc) custom_context_thread_func, &data);

  g_mutex_lock (&data.lock);
  while (!data.finish)
    g_cond_wait (&data.cond, &data.lock);
  g_mutex_unlock (&data.lock);

  g_thread_join (thread);
  g_mutex_clear (&data.lock);
  g_cond_clear (&data.cond);
}

GST_END_TEST;

GST_START_TEST (test_disco_info_builder_basic)
{
  GstDiscovererInfo *info;
  GstDiscovererInfoBuilder *builder;
  GstTagList *tags;
  GstCaps *container_caps, *video_caps;
  GstDiscovererContainerInfoBuilder *cb;
  GstDiscovererVideoInfoBuilder *vb;

  container_caps = gst_caps_from_string ("video/quicktime");
  video_caps = gst_caps_from_string ("video/x-raw, width=640, height=480");

  /* Build a simple container with one video stream */
  cb = gst_discoverer_container_info_builder_new (container_caps);
  vb = gst_discoverer_video_info_builder_new ("video_0", video_caps);
  gst_discoverer_container_info_builder_add_stream (cb,
      GST_DISCOVERER_STREAM_INFO (gst_discoverer_video_info_builder_build
          (vb)));

  tags = gst_tag_list_new (GST_TAG_TITLE, "Test Title", NULL);

  builder = gst_discoverer_info_builder_new ("file:///test/file.mp4",
      GST_DISCOVERER_STREAM_INFO (gst_discoverer_container_info_builder_build
          (cb)));
  fail_unless (builder != NULL);

  gst_discoverer_info_builder_set_duration (builder, 10 * GST_SECOND);
  gst_discoverer_info_builder_set_seekable (builder, TRUE);
  gst_discoverer_info_builder_set_live (builder, FALSE);
  gst_discoverer_info_builder_set_tags (builder, tags);

  info = gst_discoverer_info_builder_build (builder);
  fail_unless (info != NULL);

  fail_unless_equals_string (gst_discoverer_info_get_uri (info),
      "file:///test/file.mp4");
  fail_unless_equals_int64 (gst_discoverer_info_get_duration (info),
      10 * GST_SECOND);
  fail_unless (gst_discoverer_info_get_seekable (info) == TRUE);
  fail_unless (gst_discoverer_info_get_live (info) == FALSE);
  fail_unless_equals_int (gst_discoverer_info_get_result (info),
      GST_DISCOVERER_OK);

  gst_tag_list_unref (tags);
  gst_caps_unref (container_caps);
  gst_caps_unref (video_caps);
  gst_discoverer_info_unref (info);
}

GST_END_TEST;

/* The tests below rely on the declarative builder DSL, which uses GCC
 * statement expressions and is therefore unavailable on MSVC. */
#if defined(__GNUC__) || defined(__clang__)

GST_START_TEST (test_disco_info_builder_with_audio_stream)
{
  GstDiscovererInfo *info;
  GstCaps *audio_caps;
  GList *audio_streams;
  GstDiscovererAudioInfo *ainfo;

  audio_caps =
      gst_caps_from_string ("audio/x-raw, rate=48000, channels=2, depth=16");

  /* *INDENT-OFF* */
  /* Single audio stream - stream is mandatory second parameter */
  info = GST_DISCOVERER_INFO_BUILD ("file:///test/audio.ogg",
      GST_DISCOVERER_AUDIO_STREAM_BUILD ("audio_0", audio_caps,
          GST_DISCOVERER_AUDIO_LANGUAGE ("en");
          GST_DISCOVERER_AUDIO_BITRATE (128000);
      ),
      GST_DISCOVERER_INFO_DURATION (5 * GST_SECOND);
      GST_DISCOVERER_INFO_SEEKABLE (TRUE);
  );
  /* *INDENT-ON* */

  fail_unless (info != NULL);
  fail_unless_equals_int64 (gst_discoverer_info_get_duration (info),
      5 * GST_SECOND);

  audio_streams = gst_discoverer_info_get_audio_streams (info);
  fail_unless_equals_int (g_list_length (audio_streams), 1);

  ainfo = (GstDiscovererAudioInfo *) audio_streams->data;
  fail_unless_equals_int (gst_discoverer_audio_info_get_channels (ainfo), 2);
  fail_unless_equals_int (gst_discoverer_audio_info_get_sample_rate (ainfo),
      48000);
  fail_unless_equals_int (gst_discoverer_audio_info_get_bitrate (ainfo),
      128000);
  fail_unless_equals_string (gst_discoverer_audio_info_get_language (ainfo),
      "en");

  gst_discoverer_stream_info_list_free (audio_streams);
  gst_caps_unref (audio_caps);
  gst_discoverer_info_unref (info);
}

GST_END_TEST;

GST_START_TEST (test_disco_info_builder_with_video_stream)
{
  GstDiscovererInfo *info;
  GstCaps *video_caps;
  GList *video_streams;
  GstDiscovererVideoInfo *vinfo;

  video_caps = gst_caps_from_string
      ("video/x-raw, width=1920, height=1080, framerate=30/1, pixel-aspect-ratio=1/1");

  /* *INDENT-OFF* */
  /* Single video stream - stream is mandatory second parameter */
  info = GST_DISCOVERER_INFO_BUILD ("file:///test/video.mp4",
      GST_DISCOVERER_VIDEO_STREAM_BUILD ("video_0", video_caps,
          GST_DISCOVERER_VIDEO_BITRATE (5000000);
          GST_DISCOVERER_VIDEO_INTERLACED (FALSE);
      ),
      GST_DISCOVERER_INFO_DURATION (120 * GST_SECOND);
      GST_DISCOVERER_INFO_SEEKABLE (TRUE);
  );
  /* *INDENT-ON* */

  fail_unless (info != NULL);

  video_streams = gst_discoverer_info_get_video_streams (info);
  fail_unless_equals_int (g_list_length (video_streams), 1);

  vinfo = (GstDiscovererVideoInfo *) video_streams->data;
  fail_unless_equals_int (gst_discoverer_video_info_get_width (vinfo), 1920);
  fail_unless_equals_int (gst_discoverer_video_info_get_height (vinfo), 1080);
  fail_unless_equals_int (gst_discoverer_video_info_get_framerate_num (vinfo),
      30);
  fail_unless_equals_int (gst_discoverer_video_info_get_framerate_denom (vinfo),
      1);
  fail_unless_equals_int (gst_discoverer_video_info_get_bitrate (vinfo),
      5000000);
  fail_unless (gst_discoverer_video_info_is_interlaced (vinfo) == FALSE);

  gst_discoverer_stream_info_list_free (video_streams);
  gst_caps_unref (video_caps);
  gst_discoverer_info_unref (info);
}

GST_END_TEST;

GST_START_TEST (test_disco_info_builder_with_multiple_streams)
{
  GstDiscovererInfo *info;
  GstCaps *container_caps, *video_caps, *audio_caps;
  GList *video_streams, *audio_streams;
  GstDiscovererStreamInfo *stream_info;

  container_caps = gst_caps_from_string ("video/x-matroska");
  video_caps = gst_caps_from_string ("video/x-raw, width=1280, height=720");
  audio_caps = gst_caps_from_string ("audio/x-raw, rate=44100, channels=2");

  /* *INDENT-OFF* */
  /* Multiple streams - use container builder */
  info = GST_DISCOVERER_INFO_BUILD ("file:///test/av.mkv",
      GST_DISCOVERER_CONTAINER_BUILD (container_caps,
          GST_DISCOVERER_CONTAINER_ADD_STREAM (
              GST_DISCOVERER_VIDEO_STREAM_BUILD ("video_0", video_caps,));
          GST_DISCOVERER_CONTAINER_ADD_STREAM (
              GST_DISCOVERER_AUDIO_STREAM_BUILD ("audio_0", audio_caps,
                  GST_DISCOVERER_AUDIO_LANGUAGE ("en");));
          GST_DISCOVERER_CONTAINER_ADD_STREAM (
              GST_DISCOVERER_AUDIO_STREAM_BUILD ("audio_1", audio_caps,
                  GST_DISCOVERER_AUDIO_LANGUAGE ("es");));
      ),
      GST_DISCOVERER_INFO_DURATION (60 * GST_SECOND);
      GST_DISCOVERER_INFO_SEEKABLE (TRUE);
  );
  /* *INDENT-ON* */

  fail_unless (info != NULL);

  video_streams = gst_discoverer_info_get_video_streams (info);
  fail_unless_equals_int (g_list_length (video_streams), 1);
  gst_discoverer_stream_info_list_free (video_streams);

  audio_streams = gst_discoverer_info_get_audio_streams (info);
  fail_unless_equals_int (g_list_length (audio_streams), 2);

  /* Check languages are correct */
  fail_unless_equals_string (gst_discoverer_audio_info_get_language
      ((GstDiscovererAudioInfo *) audio_streams->data), "en");
  fail_unless_equals_string (gst_discoverer_audio_info_get_language
      ((GstDiscovererAudioInfo *) audio_streams->next->data), "es");
  gst_discoverer_stream_info_list_free (audio_streams);

  /* With multiple streams, there should be a container info */
  stream_info = gst_discoverer_info_get_stream_info (info);
  fail_unless (GST_IS_DISCOVERER_CONTAINER_INFO (stream_info));
  gst_discoverer_stream_info_unref (stream_info);

  gst_caps_unref (container_caps);
  gst_caps_unref (video_caps);
  gst_caps_unref (audio_caps);
  gst_discoverer_info_unref (info);
}

GST_END_TEST;

GST_START_TEST (test_disco_info_builder_with_container)
{
  GstDiscovererInfo *info;
  GstCaps *container_caps, *video_caps;
  GstTagList *container_tags;
  GstDiscovererStreamInfo *stream_info;
  GstDiscovererContainerInfo *cinfo;
  const GstTagList *retrieved_tags;
  GstCaps *retrieved_caps;

  container_caps = gst_caps_from_string ("video/quicktime, variant=iso");
  video_caps = gst_caps_from_string ("video/x-raw, width=640, height=480");
  container_tags = gst_tag_list_new (GST_TAG_CONTAINER_FORMAT, "MP4", NULL);

  /* *INDENT-OFF* */
  /* Container with tags - use container builder */
  info = GST_DISCOVERER_INFO_BUILD ("file:///test/container.mp4",
      GST_DISCOVERER_CONTAINER_BUILD (container_caps,
          GST_DISCOVERER_CONTAINER_TAGS (container_tags);
          GST_DISCOVERER_CONTAINER_ADD_STREAM (
              GST_DISCOVERER_VIDEO_STREAM_BUILD ("video_0", video_caps,));
      ),
      GST_DISCOVERER_INFO_DURATION (30 * GST_SECOND);
  );
  /* *INDENT-ON* */

  fail_unless (info != NULL);

  stream_info = gst_discoverer_info_get_stream_info (info);
  fail_unless (GST_IS_DISCOVERER_CONTAINER_INFO (stream_info));

  cinfo = GST_DISCOVERER_CONTAINER_INFO (stream_info);
  retrieved_caps = gst_discoverer_stream_info_get_caps (stream_info);
  fail_unless (retrieved_caps != NULL);
  gst_caps_unref (retrieved_caps);

  retrieved_tags = gst_discoverer_container_info_get_tags (cinfo);
  fail_unless (retrieved_tags != NULL);

  gst_discoverer_stream_info_unref (stream_info);
  gst_caps_unref (container_caps);
  gst_caps_unref (video_caps);
  gst_tag_list_unref (container_tags);
  gst_discoverer_info_unref (info);
}

GST_END_TEST;

GST_START_TEST (test_disco_info_builder_subtitle_stream)
{
  GstDiscovererInfo *info;
  GstCaps *subtitle_caps;
  GList *subtitle_streams;
  GstDiscovererSubtitleInfo *sinfo;

  subtitle_caps = gst_caps_from_string ("text/x-raw, format=utf8");

  /* *INDENT-OFF* */
  /* Single subtitle stream - stream is mandatory second parameter */
  info = GST_DISCOVERER_INFO_BUILD ("file:///test/subtitles.srt",
      GST_DISCOVERER_SUBTITLE_STREAM_BUILD ("subtitle_0", subtitle_caps,
          GST_DISCOVERER_SUBTITLE_LANGUAGE ("fr");
      ),
      GST_DISCOVERER_INFO_DURATION (60 * GST_SECOND);
  );
  /* *INDENT-ON* */

  fail_unless (info != NULL);

  subtitle_streams = gst_discoverer_info_get_subtitle_streams (info);
  fail_unless_equals_int (g_list_length (subtitle_streams), 1);

  sinfo = (GstDiscovererSubtitleInfo *) subtitle_streams->data;
  fail_unless_equals_string (gst_discoverer_subtitle_info_get_language (sinfo),
      "fr");

  gst_discoverer_stream_info_list_free (subtitle_streams);
  gst_caps_unref (subtitle_caps);
  gst_discoverer_info_unref (info);
}

GST_END_TEST;

#endif /* defined(__GNUC__) || defined(__clang__) */

GST_START_TEST (test_disco_audio_builder_standalone)
{
  GstDiscovererAudioInfoBuilder *builder;
  GstDiscovererAudioInfo *ainfo;
  GstCaps *caps;

  caps = gst_caps_from_string ("audio/x-raw, rate=96000, channels=6, depth=24");

  builder = gst_discoverer_audio_info_builder_new ("audio_test", caps);
  fail_unless (builder != NULL);

  gst_discoverer_audio_info_builder_set_language (builder, "de");
  gst_discoverer_audio_info_builder_set_bitrate (builder, 256000);
  gst_discoverer_audio_info_builder_set_max_bitrate (builder, 320000);

  ainfo = gst_discoverer_audio_info_builder_build (builder);
  fail_unless (ainfo != NULL);

  fail_unless_equals_int (gst_discoverer_audio_info_get_channels (ainfo), 6);
  fail_unless_equals_int (gst_discoverer_audio_info_get_sample_rate (ainfo),
      96000);
  fail_unless_equals_int (gst_discoverer_audio_info_get_depth (ainfo), 24);
  fail_unless_equals_string (gst_discoverer_audio_info_get_language (ainfo),
      "de");
  fail_unless_equals_int (gst_discoverer_audio_info_get_bitrate (ainfo),
      256000);
  fail_unless_equals_int (gst_discoverer_audio_info_get_max_bitrate (ainfo),
      320000);
  fail_unless_equals_string (gst_discoverer_stream_info_get_stream_id
      (GST_DISCOVERER_STREAM_INFO (ainfo)), "audio_test");

  gst_caps_unref (caps);
  gst_discoverer_stream_info_unref (GST_DISCOVERER_STREAM_INFO (ainfo));
}

GST_END_TEST;

GST_START_TEST (test_disco_video_builder_standalone)
{
  GstDiscovererVideoInfoBuilder *builder;
  GstDiscovererVideoInfo *vinfo;
  GstCaps *caps;

  caps = gst_caps_from_string
      ("video/x-raw, width=3840, height=2160, framerate=60/1, pixel-aspect-ratio=1/1");

  builder = gst_discoverer_video_info_builder_new ("video_test", caps);
  fail_unless (builder != NULL);

  gst_discoverer_video_info_builder_set_bitrate (builder, 25000000);
  gst_discoverer_video_info_builder_set_max_bitrate (builder, 30000000);
  gst_discoverer_video_info_builder_set_interlaced (builder, FALSE);
  gst_discoverer_video_info_builder_set_is_image (builder, FALSE);

  vinfo = gst_discoverer_video_info_builder_build (builder);
  fail_unless (vinfo != NULL);

  fail_unless_equals_int (gst_discoverer_video_info_get_width (vinfo), 3840);
  fail_unless_equals_int (gst_discoverer_video_info_get_height (vinfo), 2160);
  fail_unless_equals_int (gst_discoverer_video_info_get_framerate_num (vinfo),
      60);
  fail_unless_equals_int (gst_discoverer_video_info_get_framerate_denom (vinfo),
      1);
  fail_unless_equals_int (gst_discoverer_video_info_get_par_num (vinfo), 1);
  fail_unless_equals_int (gst_discoverer_video_info_get_par_denom (vinfo), 1);
  fail_unless_equals_int (gst_discoverer_video_info_get_bitrate (vinfo),
      25000000);
  fail_unless_equals_int (gst_discoverer_video_info_get_max_bitrate (vinfo),
      30000000);
  fail_unless (gst_discoverer_video_info_is_interlaced (vinfo) == FALSE);
  fail_unless (gst_discoverer_video_info_is_image (vinfo) == FALSE);

  gst_caps_unref (caps);
  gst_discoverer_stream_info_unref (GST_DISCOVERER_STREAM_INFO (vinfo));
}

GST_END_TEST;

#if defined(__GNUC__) || defined(__clang__)

/* Test dynamic stream building using macros - simulates GES track iteration */
GST_START_TEST (test_disco_info_builder_dynamic_streams)
{
  GstDiscovererInfo *info;
  GstCaps *container_caps;
  GList *audio_streams, *video_streams;
  guint i;

  struct
  {
    const gchar *type;
    const gchar *caps_str;
    const gchar *language;
  } tracks[] = {
    {"video", "video/x-raw, width=1920, height=1080, framerate=25/1", NULL},
    {"audio", "audio/x-raw, rate=48000, channels=2", "en"},
    {"audio", "audio/x-raw, rate=48000, channels=6", "de"},
    {"video", "video/x-raw, width=1280, height=720, framerate=30/1", NULL},
  };

  container_caps = gst_caps_from_string ("application/x-ges-timeline");

  /* *INDENT-OFF* */
  /* Multiple streams - use container builder with dynamic stream addition */
  info = GST_DISCOVERER_INFO_BUILD ("gessubtimeline://test-timeline",
      GST_DISCOVERER_CONTAINER_BUILD (container_caps,
          /* Dynamic loop adding streams  */
          for (i = 0; i < G_N_ELEMENTS (tracks); i++) {
            GstCaps * caps = gst_caps_from_string (tracks[i].caps_str);
            gchar * stream_id = g_strdup_printf ("track_%u", i);
            if (g_str_equal (tracks[i].type, "video")) {
              GST_DISCOVERER_CONTAINER_ADD_STREAM (
                  GST_DISCOVERER_VIDEO_STREAM_BUILD (stream_id, caps,
                      GST_DISCOVERER_VIDEO_BITRATE (5000000);));
            } else if (g_str_equal (tracks[i].type, "audio")) {
              GST_DISCOVERER_CONTAINER_ADD_STREAM (
                  GST_DISCOVERER_AUDIO_STREAM_BUILD (stream_id, caps,
                      GST_DISCOVERER_AUDIO_LANGUAGE (tracks[i].language);));
            }
            g_free (stream_id);
            gst_caps_unref (caps);
          }
      ),
      GST_DISCOVERER_INFO_DURATION (120 * GST_SECOND);
      GST_DISCOVERER_INFO_SEEKABLE (TRUE);
  );
  /* *INDENT-ON* */

  fail_unless (info != NULL);
  fail_unless_equals_int64 (gst_discoverer_info_get_duration (info),
      120 * GST_SECOND);

  /* Verify we got 2 video streams */
  video_streams = gst_discoverer_info_get_video_streams (info);
  fail_unless_equals_int (g_list_length (video_streams), 2);

  /* Check first video stream dimensions */
  fail_unless_equals_int (gst_discoverer_video_info_get_width
      ((GstDiscovererVideoInfo *) video_streams->data), 1920);
  fail_unless_equals_int (gst_discoverer_video_info_get_height
      ((GstDiscovererVideoInfo *) video_streams->data), 1080);

  /* Check second video stream dimensions */
  fail_unless_equals_int (gst_discoverer_video_info_get_width
      ((GstDiscovererVideoInfo *) video_streams->next->data), 1280);
  fail_unless_equals_int (gst_discoverer_video_info_get_height
      ((GstDiscovererVideoInfo *) video_streams->next->data), 720);
  gst_discoverer_stream_info_list_free (video_streams);

  /* Verify we got 2 audio streams with correct languages */
  audio_streams = gst_discoverer_info_get_audio_streams (info);
  fail_unless_equals_int (g_list_length (audio_streams), 2);
  fail_unless_equals_string (gst_discoverer_audio_info_get_language
      ((GstDiscovererAudioInfo *) audio_streams->data), "en");
  fail_unless_equals_string (gst_discoverer_audio_info_get_language
      ((GstDiscovererAudioInfo *) audio_streams->next->data), "de");
  gst_discoverer_stream_info_list_free (audio_streams);

  gst_caps_unref (container_caps);
  gst_discoverer_info_unref (info);
}

GST_END_TEST;

#endif /* defined(__GNUC__) || defined(__clang__) */

GST_START_TEST (test_disco_info_builder_single_stream)
{
  GstDiscovererInfo *info;
  GstDiscovererInfoBuilder *builder;
  GstCaps *audio_caps;
  GstDiscovererAudioInfoBuilder *ab;
  GstDiscovererStreamInfo *stream_info;

  /* Test single-stream - no container created */
  audio_caps =
      gst_caps_from_string ("audio/x-raw, rate=48000, channels=2, depth=16");

  ab = gst_discoverer_audio_info_builder_new ("audio_0", audio_caps);
  gst_discoverer_audio_info_builder_set_language (ab, "en");

  builder = gst_discoverer_info_builder_new ("file:///test/single.flac",
      GST_DISCOVERER_STREAM_INFO (gst_discoverer_audio_info_builder_build
          (ab)));
  fail_unless (builder != NULL);

  gst_discoverer_info_builder_set_duration (builder, 180 * GST_SECOND);
  gst_discoverer_info_builder_set_seekable (builder, TRUE);

  info = gst_discoverer_info_builder_build (builder);
  fail_unless (info != NULL);

  /* With single stream, stream_info should be the audio stream directly */
  stream_info = gst_discoverer_info_get_stream_info (info);
  fail_unless (GST_IS_DISCOVERER_AUDIO_INFO (stream_info));
  fail_unless (!GST_IS_DISCOVERER_CONTAINER_INFO (stream_info));
  gst_discoverer_stream_info_unref (stream_info);

  gst_caps_unref (audio_caps);
  gst_discoverer_info_unref (info);
}

GST_END_TEST;

GST_START_TEST (test_disco_container_builder_standalone)
{
  GstDiscovererContainerInfoBuilder *builder;
  GstDiscovererContainerInfo *container;
  GstCaps *container_caps, *audio_caps, *video_caps;
  GstDiscovererAudioInfoBuilder *ab;
  GstDiscovererVideoInfoBuilder *vb;
  GList *streams;

  container_caps = gst_caps_from_string ("video/x-matroska");
  audio_caps = gst_caps_from_string ("audio/x-raw, rate=48000, channels=2");
  video_caps = gst_caps_from_string ("video/x-raw, width=1920, height=1080");

  builder = gst_discoverer_container_info_builder_new (container_caps);
  fail_unless (builder != NULL);

  /* Add a video stream */
  vb = gst_discoverer_video_info_builder_new ("video_0", video_caps);
  gst_discoverer_container_info_builder_add_stream (builder,
      GST_DISCOVERER_STREAM_INFO (gst_discoverer_video_info_builder_build
          (vb)));

  /* Add an audio stream */
  ab = gst_discoverer_audio_info_builder_new ("audio_0", audio_caps);
  gst_discoverer_container_info_builder_add_stream (builder,
      GST_DISCOVERER_STREAM_INFO (gst_discoverer_audio_info_builder_build
          (ab)));

  container = gst_discoverer_container_info_builder_build (builder);
  fail_unless (container != NULL);

  /* Check we have 2 streams */
  streams = gst_discoverer_container_info_get_streams (container);
  fail_unless_equals_int (g_list_length (streams), 2);
  gst_discoverer_stream_info_list_free (streams);

  gst_caps_unref (container_caps);
  gst_caps_unref (audio_caps);
  gst_caps_unref (video_caps);
  gst_discoverer_stream_info_unref (GST_DISCOVERER_STREAM_INFO (container));
}

GST_END_TEST;

#if defined(__GNUC__) || defined(__clang__)

GST_START_TEST (test_disco_info_builder_serialization)
{
  GstDiscovererInfo *info, *deserialized;
  GstCaps *container_caps, *video_caps, *audio_caps;
  GVariant *variant;
  GList *audio_streams, *video_streams;

  container_caps = gst_caps_from_string ("video/quicktime");
  video_caps = gst_caps_from_string ("video/x-raw, width=1920, height=1080");
  audio_caps = gst_caps_from_string ("audio/x-raw, rate=48000, channels=2");

  /* *INDENT-OFF* */
  /* Container with multiple streams - use container builder */
  info = GST_DISCOVERER_INFO_BUILD ("file:///test/serialize.mp4",
      GST_DISCOVERER_CONTAINER_BUILD (container_caps,
          GST_DISCOVERER_CONTAINER_ADD_STREAM (
              GST_DISCOVERER_VIDEO_STREAM_BUILD ("video_0", video_caps,
                  GST_DISCOVERER_VIDEO_BITRATE (5000000);));
          GST_DISCOVERER_CONTAINER_ADD_STREAM (
              GST_DISCOVERER_AUDIO_STREAM_BUILD ("audio_0", audio_caps,
                  GST_DISCOVERER_AUDIO_LANGUAGE ("en");));
      ),
      GST_DISCOVERER_INFO_DURATION (60 * GST_SECOND);
      GST_DISCOVERER_INFO_SEEKABLE (TRUE);
  );
  /* *INDENT-ON* */

  fail_unless (info != NULL);

  /* Serialize and deserialize */
  variant = gst_discoverer_info_to_variant (info, GST_DISCOVERER_SERIALIZE_ALL);
  fail_unless (variant != NULL);

  deserialized = gst_discoverer_info_from_variant (variant);
  fail_unless (deserialized != NULL);

  /* Verify deserialized info matches */
  fail_unless_equals_string (gst_discoverer_info_get_uri (deserialized),
      "file:///test/serialize.mp4");
  fail_unless_equals_int64 (gst_discoverer_info_get_duration (deserialized),
      60 * GST_SECOND);
  fail_unless (gst_discoverer_info_get_seekable (deserialized) == TRUE);

  video_streams = gst_discoverer_info_get_video_streams (deserialized);
  fail_unless_equals_int (g_list_length (video_streams), 1);
  gst_discoverer_stream_info_list_free (video_streams);

  audio_streams = gst_discoverer_info_get_audio_streams (deserialized);
  fail_unless_equals_int (g_list_length (audio_streams), 1);
  gst_discoverer_stream_info_list_free (audio_streams);

  g_variant_unref (variant);
  gst_caps_unref (container_caps);
  gst_caps_unref (video_caps);
  gst_caps_unref (audio_caps);
  gst_discoverer_info_unref (info);
  gst_discoverer_info_unref (deserialized);
}

GST_END_TEST;

#endif /* defined(__GNUC__) || defined(__clang__) */

static Suite *
discoverer_suite (void)
{
  Suite *s = suite_create ("discoverer");
  TCase *tc_chain = tcase_create ("general");
  TCase *tc_builder = tcase_create ("builder");

  have_theora = gst_registry_check_feature_version (gst_registry_get (),
      "theoradec", GST_VERSION_MAJOR, GST_VERSION_MINOR, 0);
  have_ogg = gst_registry_check_feature_version (gst_registry_get (),
      "oggdemux", GST_VERSION_MAJOR, GST_VERSION_MINOR, 0);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_disco_init);
  tcase_add_test (tc_chain, test_disco_sync);
  tcase_add_test (tc_chain, test_disco_sync_reuse_ogg);
  tcase_add_test (tc_chain, test_disco_sync_reuse_mp3);
  tcase_add_test (tc_chain, test_disco_sync_reuse_timeout);
  tcase_add_test (tc_chain, test_disco_missing_plugins);
  tcase_add_test (tc_chain, test_disco_serializing);
  tcase_add_test (tc_chain, test_disco_async);
  tcase_add_test (tc_chain, test_disco_async_custom_context);
  tcase_add_test (tc_chain, test_disco_async_reuse);

  suite_add_tcase (s, tc_builder);
  tcase_add_test (tc_builder, test_disco_info_builder_basic);
#if defined(__GNUC__) || defined(__clang__)
  tcase_add_test (tc_builder, test_disco_info_builder_with_audio_stream);
  tcase_add_test (tc_builder, test_disco_info_builder_with_video_stream);
  tcase_add_test (tc_builder, test_disco_info_builder_with_multiple_streams);
  tcase_add_test (tc_builder, test_disco_info_builder_with_container);
  tcase_add_test (tc_builder, test_disco_info_builder_subtitle_stream);
#endif
  tcase_add_test (tc_builder, test_disco_audio_builder_standalone);
  tcase_add_test (tc_builder, test_disco_video_builder_standalone);
#if defined(__GNUC__) || defined(__clang__)
  tcase_add_test (tc_builder, test_disco_info_builder_dynamic_streams);
#endif
  tcase_add_test (tc_builder, test_disco_info_builder_single_stream);
  tcase_add_test (tc_builder, test_disco_container_builder_standalone);
#if defined(__GNUC__) || defined(__clang__)
  tcase_add_test (tc_builder, test_disco_info_builder_serialization);
#endif

  return s;
}

GST_CHECK_MAIN (discoverer);
