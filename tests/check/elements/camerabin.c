/* GStreamer
 *
 * unit test for camerabin basic operations
 * Copyright (C) 2008 Nokia Corporation <multimedia@maemo.org>
 *
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <unistd.h>
#include <glib.h>
#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/interfaces/photography.h>

#define SINGLE_IMAGE_FILENAME "image"
#define SINGLE_IMAGE_WITH_FLAGS_FILENAME "image_with_flags"
#define SEQUENTIAL_IMAGES_FILENAME "sequential_image"
#define BURST_IMAGE_FILENAME "burst_image"
#define VIDEO_FILENAME "video"
#define VIDEO_WITH_FLAGS_FILENAME "video_with_flags"
#define VIDEO_PAUSE_FILENAME "video_pause"
#define VIDEO_NOAUDIO_FILENAME "video_noaudio"
#define CYCLE_IMAGE_FILENAME "cycle_image"
#define CYCLE_VIDEO_FILENAME "cycle_video"
#define TAGLISTS_COUNT 3
#define CYCLE_COUNT_MAX 2
#define SEQUENTIAL_IMAGES_COUNT 3
#define MAX_BURST_IMAGES 10
#define PHOTO_SETTING_DELAY_US 0

static GstElement *camera;
static guint bus_source;
static GMainLoop *main_loop;
static guint cycle_count = 0;
static gboolean received_preview_msg = FALSE;
static GstTagList *taglists[TAGLISTS_COUNT];
static GstTagList *validation_taglist;

/* helper function for filenames */
static const gchar *
make_test_file_name (const gchar * base_name, gint num)
{
  static gchar file_name[1000];

  g_snprintf (file_name, 999, "%s" G_DIR_SEPARATOR_S
      "gstcamerabintest_%s_%03d.cap", g_get_tmp_dir (), base_name, num);

  GST_INFO ("capturing to: %s (cycle: %d)", file_name, cycle_count);
  return file_name;
}

/* burst capture is not supported in camerabin for the moment */
#ifdef ENABLE_BURST_CAPTURE
static const gchar *
make_test_seq_file_name (const gchar * base_name)
{
  static gchar file_name[1000];

  g_snprintf (file_name, 999, "%s" G_DIR_SEPARATOR_S "%02u_%s",
      g_get_tmp_dir (), captured_images, base_name);

  GST_INFO ("capturing to: %s", file_name);
  return file_name;
}
#endif
/* signal handlers */

static gboolean
handle_image_captured_cb (gpointer data)
{
  GMainLoop *loop = (GMainLoop *) data;

  /* unblock viewfinder */
  g_object_set (camera, "block-after-capture", FALSE, NULL);

  GST_DEBUG ("handle_image_captured_cb, cycle: %d", cycle_count);
  if (cycle_count == 0) {
    GST_DEBUG ("all cycles done");
    g_main_loop_quit (loop);
  } else {
    /* Set video recording mode */
    g_object_set (camera, "mode", 1,
        "filename", make_test_file_name (CYCLE_VIDEO_FILENAME, cycle_count),
        NULL);
    /* Record video */
    g_signal_emit_by_name (camera, "capture-start", NULL);
    g_usleep (G_USEC_PER_SEC);
    g_signal_emit_by_name (camera, "capture-stop", NULL);
    GST_DEBUG ("video captured");

    /* Set still image mode */
    g_object_set (camera, "mode", 0,
        "filename", make_test_file_name (CYCLE_IMAGE_FILENAME, cycle_count),
        NULL);

    cycle_count--;
    GST_DEBUG ("next cycle: %d", cycle_count);

    /* Take a picture */
    g_signal_emit_by_name (camera, "capture-start", NULL);
  }
  GST_DEBUG ("handle_image_captured_cb done");
  return FALSE;
}

static gboolean
capture_done (GstElement * elem, const gchar * filename, gpointer user_data)
{
  GMainLoop *loop = (GMainLoop *) user_data;

  g_idle_add ((GSourceFunc) handle_image_captured_cb, loop);

  GST_INFO ("image saved");

  return FALSE;
}

/* configuration */

static gboolean
set_and_check_camerabin_element (GstElement * camera, const char *property,
    GstElement * element)
{
  GstElement *element_check;
  gboolean ret = FALSE;

  if (element) {
    g_object_set (camera, property, element, NULL);
    g_object_get (camera, property, &element_check, NULL);
    if (element_check == element)
      ret = TRUE;
    if (element_check)
      g_object_unref (element_check);
  }
  return ret;
}

static void
setup_camerabin_elements (GstElement * camera)
{
  GstElement *vfsink, *audiosrc, *videosrc, *audioenc, *videoenc, *imageenc,
      *videomux, *viewfinder_filter, *imagepp, *videopp, *formatter;
  GstCaps *audiocaps, *videocaps;

  /* Use fakesink for view finder */
  vfsink = gst_element_factory_make ("fakesink", NULL);
  g_object_set (vfsink, "sync", TRUE, NULL);
  audiosrc = gst_element_factory_make ("audiotestsrc", NULL);
  g_object_set (audiosrc, "is-live", TRUE, NULL);
  videosrc = gst_element_factory_make ("videotestsrc", NULL);
  /* Set pattern to white (3) to avoid timeouts */
  g_object_set (videosrc, "is-live", TRUE, "pattern", 3, NULL);
  audioenc = gst_element_factory_make ("capsfilter", NULL);
  audiocaps = gst_caps_from_string ("audio/x-raw-int");
  g_object_set (audioenc, "caps", audiocaps, NULL);
  gst_caps_unref (audiocaps);
  videoenc = gst_element_factory_make ("capsfilter", NULL);
  videocaps = gst_caps_from_string ("video/x-raw-yuv");
  g_object_set (videoenc, "caps", videocaps, NULL);
  gst_caps_unref (videocaps);
  videomux = gst_element_factory_make ("avimux", NULL);
  imageenc = gst_element_factory_make ("jpegenc", NULL);
  viewfinder_filter = gst_element_factory_make ("identity", NULL);
  imagepp = gst_element_factory_make ("identity", NULL);
  videopp = gst_element_factory_make ("identity", NULL);
  formatter = gst_element_factory_make ("jifmux", NULL);

  if (set_and_check_camerabin_element (camera, "viewfinder-sink", vfsink)
      && set_and_check_camerabin_element (camera, "audio-source", audiosrc)
      && set_and_check_camerabin_element (camera, "video-source", videosrc)
      && set_and_check_camerabin_element (camera, "audio-encoder", audioenc)
      && set_and_check_camerabin_element (camera, "video-encoder", videoenc)
      && set_and_check_camerabin_element (camera, "image-encoder", imageenc)
      && set_and_check_camerabin_element (camera, "video-muxer", videomux)
      && set_and_check_camerabin_element (camera, "viewfinder-filter",
          viewfinder_filter)
      && set_and_check_camerabin_element (camera, "image-post-processing",
          imagepp)
      && set_and_check_camerabin_element (camera, "video-post-processing",
          videopp)
      && set_and_check_camerabin_element (camera, "image-formatter", formatter)) {
    GST_INFO ("element properties set and checked");
  } else {
    GST_WARNING ("error setting up test plugins");
  }
}

static gboolean
capture_bus_cb (GstBus * bus, GstMessage * message, gpointer data)
{
  GMainLoop *loop = (GMainLoop *) data;
  const GstStructure *st;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:{
      GError *err = NULL;
      gchar *debug = NULL;

      gst_message_parse_error (message, &err, &debug);
      GST_WARNING ("ERROR: %s [%s]", err->message, debug);
      g_error_free (err);
      g_free (debug);
      /* Write debug graph to file */
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (camera),
          GST_DEBUG_GRAPH_SHOW_ALL, "camerabin.error");

      fail_if (TRUE, "error while capturing");
      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_WARNING:{
      GError *err = NULL;
      gchar *debug = NULL;

      gst_message_parse_warning (message, &err, &debug);
      GST_WARNING ("WARNING: %s [%s]", err->message, debug);
      g_error_free (err);
      g_free (debug);
      /* Write debug graph to file */
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (camera),
          GST_DEBUG_GRAPH_SHOW_ALL, "camerabin.warning");
      break;
    }
    case GST_MESSAGE_EOS:
      GST_DEBUG ("eos");
      g_main_loop_quit (loop);
      break;
    default:
      st = gst_message_get_structure (message);
      if (st && gst_structure_has_name (st, "image-captured")) {
        gboolean ready = FALSE;
        GST_INFO ("image captured");
        g_object_get (camera, "ready-for-capture", &ready, NULL);
        fail_if (!ready, "not ready for capture");
      }
      break;
  }
  return TRUE;
}

static GstBusSyncReply
bus_sync_callback (GstBus * bus, GstMessage * message, gpointer data)
{
  const GstStructure *st;
  st = gst_message_get_structure (message);
  if (st) {
    if (gst_structure_has_name (st, "preview-image")) {
      GST_DEBUG ("get preview-image message");
      received_preview_msg = TRUE;
    }
  }


  return GST_BUS_PASS;

}

static void
setup (void)
{
  GstTagSetter *setter;
  gchar *desc_str;
  GstCaps *filter_caps;
  GstBus *bus;
  gint i;

  GST_INFO ("init");

  main_loop = g_main_loop_new (NULL, TRUE);

  camera = gst_check_setup_element ("camerabin");

  setup_camerabin_elements (camera);

  g_signal_connect (camera, "image-done", G_CALLBACK (capture_done), main_loop);

  bus = gst_pipeline_get_bus (GST_PIPELINE (camera));
  bus_source = gst_bus_add_watch (bus, (GstBusFunc) capture_bus_cb, main_loop);
  gst_bus_set_sync_handler (bus, bus_sync_callback, main_loop);
  gst_object_unref (bus);

  filter_caps = gst_caps_from_string ("video/x-raw-yuv,format=(fourcc)I420");
  g_object_set (G_OBJECT (camera), "filter-caps", filter_caps, NULL);
  gst_caps_unref (filter_caps);

  /* force a low framerate here to not timeout the tests because of the
   * encoders */
  g_signal_emit_by_name (camera, "set-video-resolution-fps", 320, 240, 5, 1,
      NULL);

  /* Set some default tags */
  setter = GST_TAG_SETTER (camera);
  desc_str = g_strdup_printf ("Created by %s", g_get_real_name ());

  gst_tag_setter_add_tags (setter, GST_TAG_MERGE_REPLACE,
      GST_TAG_DESCRIPTION, desc_str, NULL);
  g_free (desc_str);

  if (gst_element_set_state (GST_ELEMENT (camera), GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE) {
    GST_WARNING ("setting camerabin to PLAYING failed");
    gst_element_set_state (GST_ELEMENT (camera), GST_STATE_NULL);
    gst_object_unref (camera);
    camera = NULL;
  }

  /* create the taglists */
  for (i = 0; i < TAGLISTS_COUNT; i++) {
    taglists[i] = gst_tag_list_new_full (GST_TAG_ARTIST, "test-artist",
        GST_TAG_GEO_LOCATION_LONGITUDE, g_random_double_range (-180, 180),
        GST_TAG_GEO_LOCATION_LATITUDE, g_random_double_range (-90, 90),
        GST_TAG_GEO_LOCATION_ELEVATION, g_random_double_range (0, 3000), NULL);
  }

  GST_INFO ("init finished");
}

static void
teardown (void)
{
  gint i;

  g_source_remove (bus_source);

  if (camera)
    gst_check_teardown_element (camera);

  for (i = 0; i < TAGLISTS_COUNT; i++) {
    gst_tag_list_free (taglists[i]);
  }

  GST_INFO ("done");
}

static void
test_camerabin_properties (GstElement * cam)
{
  guint flags;
  gfloat zoom;
  gboolean mute;

  flags = 0x1f;
  g_object_set (G_OBJECT (cam), "flags", flags, NULL);
  g_object_get (G_OBJECT (cam), "flags", &flags, NULL);
  fail_if (flags != 0x1f, "setting camerabin flags failed");

  zoom = 2.0;
  g_object_set (G_OBJECT (cam), "zoom", zoom, NULL);
  g_object_get (G_OBJECT (cam), "zoom", &zoom, NULL);
  fail_if (zoom != 2.0, "setting camerabin zoom failed");
  g_object_set (G_OBJECT (cam), "zoom", 1.0f, NULL);

  mute = TRUE;
  g_object_set (G_OBJECT (cam), "mute", mute, NULL);
  g_object_get (G_OBJECT (cam), "mute", &mute, NULL);
  fail_if (mute != TRUE, "setting camerabin mute failed");
  g_object_set (G_OBJECT (cam), "mute", FALSE, NULL);
}

static gboolean
validity_bus_cb (GstBus * bus, GstMessage * message, gpointer data)
{
  GMainLoop *loop = (GMainLoop *) data;
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
      fail_if (TRUE, "validating captured data failed");
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_EOS:
      g_main_loop_quit (loop);
      GST_DEBUG ("eos");
      break;
    case GST_MESSAGE_TAG:{
      GstTagList *tags = NULL;
      gst_message_parse_tag (message, &tags);
      if (validation_taglist) {
        gst_tag_list_insert (validation_taglist, tags, GST_TAG_MERGE_REPLACE);
        gst_tag_list_free (tags);
      } else
        validation_taglist = tags;
      break;
    }
    default:
      break;
  }
  return TRUE;
}

static void
validate_taglist_foreach (const GstTagList * list, const gchar * tag,
    gpointer user_data)
{
  GstTagList *other = GST_TAG_LIST (user_data);

  const GValue *val1 = gst_tag_list_get_value_index (list, tag, 0);
  const GValue *val2 = gst_tag_list_get_value_index (other, tag, 0);

  fail_if (val1 == NULL);
  fail_if (val2 == NULL);

  fail_unless (gst_value_can_intersect (val1, val2));
}

static void
extract_jpeg_tags (const gchar * filename, gint num)
{
  guint source;
  GstBus *bus;
  GMainLoop *loop = g_main_loop_new (NULL, FALSE);
  const gchar *filepath = make_test_file_name (filename, num);
  gchar *pipeline_str = g_strdup_printf ("filesrc location=%s ! "
      "jpegparse ! fakesink", filepath);
  GstElement *pipeline;

  pipeline = gst_parse_launch (pipeline_str, NULL);
  fail_unless (pipeline != NULL);
  g_free (pipeline_str);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  source = gst_bus_add_watch (bus, (GstBusFunc) validity_bus_cb, loop);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_main_loop_run (loop);
  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_main_loop_unref (loop);
  g_source_remove (source);
  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

/* Validate captured files by playing them with playbin
 * and checking that no errors occur. */
static gboolean
check_file_validity (const gchar * filename, gint num, GstTagList * taglist)
{
  guint source;
  GstBus *bus;
  GMainLoop *loop = g_main_loop_new (NULL, FALSE);
  GstElement *playbin = gst_element_factory_make ("playbin2", NULL);
  GstElement *fakevideo = gst_element_factory_make ("fakesink", NULL);
  GstElement *fakeaudio = gst_element_factory_make ("fakesink", NULL);
  gchar *uri = g_strconcat ("file://", make_test_file_name (filename, num),
      NULL);

  GST_DEBUG ("checking uri: %s", uri);
  g_object_set (G_OBJECT (playbin), "uri", uri, "video-sink", fakevideo,
      "audio-sink", fakeaudio, NULL);

  validation_taglist = NULL;
  bus = gst_pipeline_get_bus (GST_PIPELINE (playbin));
  source = gst_bus_add_watch (bus, (GstBusFunc) validity_bus_cb, loop);

  gst_element_set_state (playbin, GST_STATE_PLAYING);
  g_main_loop_run (loop);
  gst_element_set_state (playbin, GST_STATE_NULL);

  /* special handling for images (jpg) as jpegparse isn't plugged by
   * default due to its current low rank */
  if (taglist && strstr (filename, "image")) {
    extract_jpeg_tags (filename, num);
  }

  /* check taglist */
  if (taglist) {
    fail_if (validation_taglist == NULL);

    GST_DEBUG ("Comparing taglists %" GST_PTR_FORMAT "; with %" GST_PTR_FORMAT,
        taglist, validation_taglist);

    gst_tag_list_foreach (taglist, validate_taglist_foreach,
        validation_taglist);
  }
  if (validation_taglist)
    gst_tag_list_free (validation_taglist);

  g_free (uri);
  g_source_remove (source);
  gst_object_unref (bus);
  gst_object_unref (playbin);
  g_main_loop_unref (loop);

  return TRUE;
}

GST_START_TEST (test_single_image_capture)
{
  gboolean ready = FALSE;
  gboolean idle = FALSE;
  if (!camera)
    return;

  /* Test photography iface settings */
  gst_element_get_state (GST_ELEMENT (camera), NULL, NULL, (2 * GST_SECOND));
  test_camerabin_properties (camera);

  /* set flags to disable additional elements */
  g_object_set (camera, "flags", 0, NULL);

  /* set still image mode */
  g_object_set (camera, "mode", 0,
      "filename", make_test_file_name (SINGLE_IMAGE_FILENAME, 0), NULL);

  /* don't run viewfinder after capture */
  g_object_set (camera, "block-after-capture", TRUE, NULL);

  /* check that capturing is possible */
  g_object_get (camera, "ready-for-capture", &ready, NULL);
  fail_if (!ready, "not ready for capture");

  /* check that the camera is idle */
  g_object_get (camera, "idle", &idle, NULL);
  fail_if (!idle, "camera should be idle");

  GST_INFO ("starting capture");
  g_signal_emit_by_name (camera, "capture-start", NULL);

  g_object_get (camera, "ready-for-capture", &ready, NULL);
  fail_if (ready, "ready for capture during capture");

  g_main_loop_run (main_loop);

  g_object_get (camera, "idle", &idle, NULL);
  fail_if (!idle, "camera should be idle");

  gst_element_set_state (GST_ELEMENT (camera), GST_STATE_NULL);

  check_file_validity (SINGLE_IMAGE_FILENAME, 0, NULL);
}

GST_END_TEST;

GST_START_TEST (test_single_image_capture_with_flags)
{
  if (!camera)
    return;

  /* set flags to enable modifier elements */
  g_object_set (camera, "flags", 79, NULL);

  /* set still image mode */
  g_object_set (camera, "mode", 0,
      "filename", make_test_file_name (SINGLE_IMAGE_WITH_FLAGS_FILENAME, 0),
      NULL);

  GST_INFO ("starting capture");
  g_signal_emit_by_name (camera, "capture-start", NULL);

  g_main_loop_run (main_loop);
  gst_element_set_state (GST_ELEMENT (camera), GST_STATE_NULL);

  check_file_validity (SINGLE_IMAGE_WITH_FLAGS_FILENAME, 0, NULL);
}

GST_END_TEST;

GST_START_TEST (test_video_recording)
{
  GstCaps *preview_caps;
  gboolean idle = FALSE;
  preview_caps = gst_caps_from_string ("video/x-raw-rgb,width=320,height=240");

  if (!camera)
    return;

  /* set flags to disable additional elements */
  g_object_set (camera, "flags", 0, NULL);

  /* Set video recording mode */
  g_object_set (camera, "mode", 1,
      "filename", make_test_file_name (VIDEO_WITH_FLAGS_FILENAME, 0), NULL);

  /* Set preview-caps */
  g_object_set (camera, "preview-caps", preview_caps, NULL);
  gst_caps_unref (preview_caps);

  /* check that the camera is idle */
  g_object_get (camera, "idle", &idle, NULL);
  fail_if (!idle, "camera should be idle");

  GST_INFO ("starting capture");
  g_signal_emit_by_name (camera, "capture-start", NULL);

  g_object_get (camera, "idle", &idle, NULL);
  fail_if (idle, "camera should not be idle");

  /* Record for one seconds  */
  g_usleep (G_USEC_PER_SEC);

  g_signal_emit_by_name (camera, "capture-stop", NULL);

  g_object_get (camera, "idle", &idle, NULL);
  fail_if (!idle, "camera should be idle");

  /* check if receiving the preview-image message */
  fail_if (!received_preview_msg,
      "creating video recording preview image failed");

  gst_element_set_state (GST_ELEMENT (camera), GST_STATE_NULL);

  check_file_validity (VIDEO_WITH_FLAGS_FILENAME, 0, NULL);
}

GST_END_TEST;

GST_START_TEST (test_video_recording_with_flags)
{
  GstCaps *preview_caps;
  preview_caps = gst_caps_from_string ("video/x-raw-rgb,width=320,height=240");

  if (!camera)
    return;

  /* set flags to enable modifier elements */
  g_object_set (camera, "flags", 95, NULL);

  /* Set video recording mode */
  g_object_set (camera, "mode", 1,
      "filename", make_test_file_name (VIDEO_FILENAME, 0), NULL);

  /* Set preview-caps */
  g_object_set (camera, "preview-caps", preview_caps, NULL);
  gst_caps_unref (preview_caps);

  GST_INFO ("starting capture");
  g_signal_emit_by_name (camera, "capture-start", NULL);
  /* Record for one seconds  */
  g_usleep (G_USEC_PER_SEC);
  g_signal_emit_by_name (camera, "capture-stop", NULL);

  /*check if receiving the preview-image message */
  fail_if (!received_preview_msg,
      "creating video recording preview image failed");

  gst_element_set_state (GST_ELEMENT (camera), GST_STATE_NULL);

  check_file_validity (VIDEO_FILENAME, 0, NULL);
}

GST_END_TEST;

GST_START_TEST (test_video_recording_pause)
{
  gboolean idle = FALSE;
  if (!camera)
    return;

  /* Set video recording mode */
  g_object_set (camera, "mode", 1,
      "filename", make_test_file_name (VIDEO_PAUSE_FILENAME, 0), NULL);

  g_object_get (camera, "idle", &idle, NULL);
  fail_unless (idle, "camera should be idle");

  GST_INFO ("starting capture");
  g_signal_emit_by_name (camera, "capture-start", NULL);

  g_object_get (camera, "idle", &idle, NULL);
  fail_if (idle, "camera shouldn't be idle when recording");

  /* Record for one seconds  */
  g_usleep (G_USEC_PER_SEC);

  GST_INFO ("pause capture");
  g_signal_emit_by_name (camera, "capture-pause", NULL);

  g_object_get (camera, "idle", &idle, NULL);
  fail_if (idle, "camera shouldn't be idle when recording and paused");

  /* Record for one seconds  */
  g_usleep (G_USEC_PER_SEC);

  GST_INFO ("continue capture");
  g_signal_emit_by_name (camera, "capture-start", NULL);

  g_object_get (camera, "idle", &idle, NULL);
  fail_if (idle, "camera shouldn't be idle when recording");

  /* Record for one seconds  */
  g_usleep (G_USEC_PER_SEC);
  g_signal_emit_by_name (camera, "capture-stop", NULL);

  g_object_get (camera, "idle", &idle, NULL);
  fail_unless (idle, "camera should be idle after capture-stop");

  gst_element_set_state (GST_ELEMENT (camera), GST_STATE_NULL);

  check_file_validity (VIDEO_PAUSE_FILENAME, 0, NULL);
}

GST_END_TEST;

GST_START_TEST (test_video_recording_no_audio)
{
  GstCaps *preview_caps;
  preview_caps = gst_caps_from_string ("video/x-raw-rgb,width=320,height=240");

  if (!camera)
    return;

  /* set flags to disable audio elements */
  g_object_set (camera, "flags", 32, NULL);

  /* Set video recording mode */
  g_object_set (camera, "mode", 1,
      "filename", make_test_file_name (VIDEO_NOAUDIO_FILENAME, 0), NULL);

  /* Set preview-caps */
  g_object_set (camera, "preview-caps", preview_caps, NULL);
  gst_caps_unref (preview_caps);

  GST_INFO ("starting capture");
  g_signal_emit_by_name (camera, "capture-start", NULL);
  /* Record for one seconds  */
  g_usleep (G_USEC_PER_SEC);
  g_signal_emit_by_name (camera, "capture-stop", NULL);

  /* check if receiving the preview-image message */
  fail_if (!received_preview_msg,
      "creating video recording preview image failed");

  gst_element_set_state (GST_ELEMENT (camera), GST_STATE_NULL);

  check_file_validity (VIDEO_NOAUDIO_FILENAME, 0, NULL);
}

GST_END_TEST;

GST_START_TEST (test_image_video_cycle)
{
  gint i;

  if (!camera)
    return;

  cycle_count = CYCLE_COUNT_MAX;

  /* set still image mode */
  g_object_set (camera, "mode", 0,
      "filename", make_test_file_name (CYCLE_IMAGE_FILENAME, cycle_count),
      NULL);

  GST_INFO ("starting capture");
  g_signal_emit_by_name (camera, "capture-start", NULL);

  g_main_loop_run (main_loop);
  gst_element_set_state (GST_ELEMENT (camera), GST_STATE_NULL);

  /* validate all the files */
  for (i = 2; i > 0; i--) {
    check_file_validity (CYCLE_IMAGE_FILENAME, i, NULL);
    check_file_validity (CYCLE_VIDEO_FILENAME, i, NULL);
  }
}

GST_END_TEST;

GST_START_TEST (test_image_tags_setting)
{
  gint i;

  g_object_set (camera, "flags", 0, NULL);
  g_object_set (camera, "block-after-capture", TRUE, NULL);

  GST_INFO ("starting capture series");

  for (i = 0; i < SEQUENTIAL_IMAGES_COUNT; i++) {
    g_object_set (camera, "filename",
        make_test_file_name (SEQUENTIAL_IMAGES_FILENAME, i), NULL);
    gst_tag_setter_merge_tags (GST_TAG_SETTER (camera),
        taglists[i % TAGLISTS_COUNT],
        gst_tag_setter_get_tag_merge_mode (GST_TAG_SETTER (camera)));
    g_signal_emit_by_name (camera, "capture-start", NULL);
    g_main_loop_run (main_loop);
  }
  gst_element_set_state (GST_ELEMENT (camera), GST_STATE_NULL);

  for (i = 0; i < SEQUENTIAL_IMAGES_COUNT; i++) {
    check_file_validity (SEQUENTIAL_IMAGES_FILENAME, i,
        taglists[i % TAGLISTS_COUNT]);
  }
}

GST_END_TEST;

static Suite *
camerabin_suite (void)
{
  Suite *s = suite_create ("camerabin");
  TCase *tc_basic = tcase_create ("general");

  /* Test that basic operations run without errors */
  suite_add_tcase (s, tc_basic);
  /* Increase timeout due to video recording */
  tcase_set_timeout (tc_basic, 20);
  tcase_add_checked_fixture (tc_basic, setup, teardown);
  tcase_add_test (tc_basic, test_single_image_capture);
  tcase_add_test (tc_basic, test_single_image_capture_with_flags);
  tcase_add_test (tc_basic, test_video_recording);
  tcase_add_test (tc_basic, test_video_recording_with_flags);
  tcase_add_test (tc_basic, test_video_recording_pause);
  tcase_add_test (tc_basic, test_video_recording_no_audio);
  tcase_add_test (tc_basic, test_image_video_cycle);
  tcase_add_test (tc_basic, test_image_tags_setting);

  return s;
}

GST_CHECK_MAIN (camerabin);
