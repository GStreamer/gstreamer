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

#define SINGLE_IMAGE_FILENAME "image.cap"
#define BURST_IMAGE_FILENAME "burst_image.cap"
#define VIDEO_FILENAME "video.cap"
#define CYCLE_IMAGE_FILENAME "cycle_image.cap"
#define CYCLE_VIDEO_FILENAME "cycle_video.cap"
#define MAX_BURST_IMAGES 10
#define PHOTO_SETTING_DELAY_US 0

static GstElement *camera;
static GMainLoop *main_loop;
static guint cycle_count = 0;
static gboolean received_preview_msg = FALSE;

/* helper function for filenames */
static const gchar *
make_test_file_name (const gchar * base_name)
{
  static gchar file_name[1000];

  g_snprintf (file_name, 999, "%s" G_DIR_SEPARATOR_S "%s",
      g_get_tmp_dir (), base_name);

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

  GST_DEBUG ("handle_image_captured_cb, cycle: %d", cycle_count);
  if (cycle_count == 0) {
    GST_DEBUG ("all cycles done");
    g_main_loop_quit (loop);
  } else {
    /* Set video recording mode */
    g_object_set (camera, "mode", 1,
        "filename", make_test_file_name (CYCLE_VIDEO_FILENAME), NULL);
    /* Record video */
    g_signal_emit_by_name (camera, "capture-start", NULL);
    g_usleep (G_USEC_PER_SEC);
    g_signal_emit_by_name (camera, "capture-stop", NULL);
    GST_DEBUG ("video captured");

    /* Set still image mode */
    g_object_set (camera, "mode", 0,
        "filename", make_test_file_name (CYCLE_IMAGE_FILENAME), NULL);
    /* Take a picture */
    g_signal_emit_by_name (camera, "capture-start", NULL);

    cycle_count--;
    GST_DEBUG ("next cycle: %d", cycle_count);
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

static void
setup_camerabin_elements (GstElement * camera)
{
  GstElement *vfsink, *audiosrc, *videosrc, *audioenc, *videoenc, *imageenc,
      *videomux;
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

  if (vfsink && audiosrc && videosrc && audioenc && videoenc && videomux
      && imageenc) {
    g_object_set (camera, "viewfinder-sink", vfsink, "audio-source", audiosrc,
        "video-source", videosrc, "audio-encoder", audioenc, "video-encoder",
        videoenc, "image-encoder", imageenc, "video-muxer", videomux, NULL);
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
        GST_INFO ("image captured");
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

  GST_INFO ("init");

  main_loop = g_main_loop_new (NULL, TRUE);

  camera = gst_check_setup_element ("camerabin");

  setup_camerabin_elements (camera);

  g_signal_connect (camera, "image-done", G_CALLBACK (capture_done), main_loop);

  bus = gst_pipeline_get_bus (GST_PIPELINE (camera));
  gst_bus_add_watch (bus, (GstBusFunc) capture_bus_cb, main_loop);
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
  GST_INFO ("init finished");
}

static void
teardown (void)
{
  if (camera)
    gst_check_teardown_element (camera);

  GST_INFO ("done");
}

static void
test_photography_settings (GstElement * cam)
{
  GTypeClass *tclass;
  gfloat ev_comp, orig_ev_comp;
  guint iso_speed = 100, orig_iso_speed;
  GstFlashMode flash, orig_flash;
  GstWhiteBalanceMode wb, orig_wb;
  GstColourToneMode ct, orig_ct;
  GstSceneMode sm, orig_sm;
  gfloat zoom, orig_zoom;

  if (!GST_IS_PHOTOGRAPHY (cam)) {
    GST_WARNING
        ("omitting photography settings test, "
        "photography interface not implemented");
    return;
  }

  for (ev_comp = -3.0; ev_comp <= 3.0; ev_comp += 0.5) {
    orig_ev_comp = ev_comp;
    gst_photography_set_ev_compensation (GST_PHOTOGRAPHY (cam), ev_comp);
    gst_photography_get_ev_compensation (GST_PHOTOGRAPHY (cam), &ev_comp);
    fail_if (orig_ev_comp != ev_comp,
        "setting photography ev compensation failed");
    ev_comp = orig_ev_comp;
    g_usleep (PHOTO_SETTING_DELAY_US);
  }

  /* FIXME: what are the actual iso values? */
  for (iso_speed = 100; iso_speed <= 800; iso_speed *= 2) {
    orig_iso_speed = iso_speed;
    gst_photography_set_iso_speed (GST_PHOTOGRAPHY (cam), iso_speed);
    gst_photography_get_iso_speed (GST_PHOTOGRAPHY (cam), &iso_speed);
    fail_if (orig_iso_speed != iso_speed,
        "setting photography iso speed failed");
    iso_speed = orig_iso_speed;
    g_usleep (PHOTO_SETTING_DELAY_US);
  }

  tclass = g_type_class_ref (GST_TYPE_FLASH_MODE);
  for (flash = 0; flash < G_ENUM_CLASS (tclass)->n_values; flash++) {
    orig_flash = flash;
    gst_photography_set_flash_mode (GST_PHOTOGRAPHY (cam), flash);
    gst_photography_get_flash_mode (GST_PHOTOGRAPHY (cam), &flash);
    fail_if (orig_flash != flash, "setting photography flash failed");
    flash = orig_flash;
    g_usleep (PHOTO_SETTING_DELAY_US);
  }
  g_type_class_unref (tclass);

  tclass = g_type_class_ref (GST_TYPE_WHITE_BALANCE_MODE);
  for (wb = 0; wb < G_ENUM_CLASS (tclass)->n_values; wb++) {
    orig_wb = wb;
    gst_photography_set_white_balance_mode (GST_PHOTOGRAPHY (cam), wb);
    gst_photography_get_white_balance_mode (GST_PHOTOGRAPHY (cam), &wb);
    fail_if (orig_wb != wb, "setting photography white balance mode failed");
    wb = orig_wb;
    g_usleep (PHOTO_SETTING_DELAY_US);
  }
  g_type_class_unref (tclass);

  tclass = g_type_class_ref (GST_TYPE_COLOUR_TONE_MODE);
  for (ct = 0; ct < G_ENUM_CLASS (tclass)->n_values; ct++) {
    orig_ct = ct;
    gst_photography_set_colour_tone_mode (GST_PHOTOGRAPHY (cam), ct);
    gst_photography_get_colour_tone_mode (GST_PHOTOGRAPHY (cam), &ct);
    fail_if (orig_ct != ct, "setting photography colour tone mode failed");
    ct = orig_ct;
    g_usleep (PHOTO_SETTING_DELAY_US);
  }
  g_type_class_unref (tclass);

  tclass = g_type_class_ref (GST_TYPE_SCENE_MODE);
  for (sm = 0; sm < G_ENUM_CLASS (tclass)->n_values; sm++) {
    orig_sm = sm;
    gst_photography_set_scene_mode (GST_PHOTOGRAPHY (cam), sm);
    gst_photography_get_scene_mode (GST_PHOTOGRAPHY (cam), &sm);
    fail_if (orig_sm != sm, "setting photography scene mode failed");
    sm = orig_sm;
    g_usleep (PHOTO_SETTING_DELAY_US);
  }
  g_type_class_unref (tclass);

  for (zoom = 1.0; zoom <= 10.0; zoom += 1.0) {
    orig_zoom = zoom;
    gst_photography_set_zoom (GST_PHOTOGRAPHY (cam), zoom);
    gst_photography_get_zoom (GST_PHOTOGRAPHY (cam), &zoom);
    fail_if (orig_zoom != zoom, "setting photography zoom failed");
    zoom = orig_zoom;
    g_usleep (PHOTO_SETTING_DELAY_US);
  }
}

static void
test_photography_properties (GstElement * cam)
{
  GTypeClass *tclass;
  gulong capabilities;
  guint aperture;
  guint32 exposure;
  gfloat ev_comp, orig_ev_comp;
  guint iso_speed = 100, orig_iso_speed;
  GstFlashMode flash, orig_flash;
  GstWhiteBalanceMode wb, orig_wb;
  GstColourToneMode ct, orig_ct;
  GstSceneMode sm, orig_sm;
  GstCaps *caps = NULL;

  if (!GST_IS_PHOTOGRAPHY (cam)) {
    GST_WARNING
        ("omitting photography properties test, not photography interface");
    return;
  }

  /* NOTE: unit testing uses videotestsrc element which is doesn't implement
     photography interface so we just check that values returned
     are sane */

  /* read only flags */
  g_object_get (GST_PHOTOGRAPHY (cam), "capabilities", &capabilities, NULL);
  fail_if (capabilities < 0, "getting photography capabilities failed");

  /* for image-capture-supported-caps we should get something always */
  g_object_get (GST_PHOTOGRAPHY (cam), "image-capture-supported-caps", &caps,
      NULL);
  fail_if (caps == NULL, "getting photography capabilities failed");
  if (caps)
    gst_caps_unref (caps);

  exposure = 0;                 /* auto */
  g_object_set (GST_PHOTOGRAPHY (cam), "exposure", exposure, NULL);
  g_object_get (GST_PHOTOGRAPHY (cam), "exposure", &exposure, NULL);
  fail_if (exposure < 0, "setting photography exposure failed");

  aperture = 0;                 /* auto */
  g_object_set (GST_PHOTOGRAPHY (cam), "aperture", aperture, NULL);
  g_object_get (GST_PHOTOGRAPHY (cam), "aperture", &aperture, NULL);
  fail_if (aperture < 0 || aperture > 255,
      "setting photography aperture failed");

  for (ev_comp = -2.5; ev_comp <= 2.5; ev_comp += 0.5) {
    orig_ev_comp = ev_comp;
    g_object_set (GST_PHOTOGRAPHY (cam), "ev-compensation", ev_comp, NULL);
    g_object_get (GST_PHOTOGRAPHY (cam), "ev-compensation", &ev_comp, NULL);
    fail_if (ev_comp < -2.5 || ev_comp > 2.5,
        "setting photography ev compensation failed");
    ev_comp = orig_ev_comp;
  }

  /* FIXME: what are the actual iso values? */
  for (iso_speed = 100; iso_speed <= 800; iso_speed *= 2) {
    orig_iso_speed = iso_speed;
    g_object_set (GST_PHOTOGRAPHY (cam), "iso-speed", iso_speed, NULL);
    g_object_get (GST_PHOTOGRAPHY (cam), "iso-speed", &iso_speed, NULL);
    GST_INFO ("iso speed %d", iso_speed);
    fail_if (iso_speed < 0 || iso_speed > 800,
        "setting photography iso speed failed");
    iso_speed = orig_iso_speed;
  }

  tclass = g_type_class_ref (GST_TYPE_FLASH_MODE);
  for (flash = 0; flash < G_ENUM_CLASS (tclass)->n_values; flash++) {
    orig_flash = flash;
    g_object_set (GST_PHOTOGRAPHY (cam), "flash-mode", flash, NULL);
    g_object_get (GST_PHOTOGRAPHY (cam), "flash-mode", &flash, NULL);
    fail_if (flash < 0 || flash >= G_ENUM_CLASS (tclass)->n_values,
        "setting photography flash failed");
    flash = orig_flash;
  }
  g_type_class_unref (tclass);

  tclass = g_type_class_ref (GST_TYPE_WHITE_BALANCE_MODE);
  for (wb = 0; wb < G_ENUM_CLASS (tclass)->n_values; wb++) {
    orig_wb = wb;
    g_object_set (G_OBJECT (cam), "white-balance-mode", wb, NULL);
    g_object_get (G_OBJECT (cam), "white-balance-mode", &wb, NULL);
    fail_if (wb < 0 || wb >= G_ENUM_CLASS (tclass)->n_values,
        "setting photography white balance mode failed");
    wb = orig_wb;
  }
  g_type_class_unref (tclass);

  tclass = g_type_class_ref (GST_TYPE_COLOUR_TONE_MODE);
  for (ct = 0; ct < G_ENUM_CLASS (tclass)->n_values; ct++) {
    orig_ct = ct;
    g_object_set (G_OBJECT (cam), "colour-tone-mode", ct, NULL);
    g_object_get (G_OBJECT (cam), "colour-tone-mode", &ct, NULL);
    fail_if (ct < 0 || ct >= G_ENUM_CLASS (tclass)->n_values,
        "setting photography colour tone mode failed");
    ct = orig_ct;
  }
  g_type_class_unref (tclass);

  tclass = g_type_class_ref (GST_TYPE_SCENE_MODE);
  for (sm = 0; sm < G_ENUM_CLASS (tclass)->n_values; sm++) {
    orig_sm = sm;
    g_object_set (G_OBJECT (cam), "scene-mode", sm, NULL);
    g_object_get (G_OBJECT (cam), "scene-mode", &sm, NULL);
    fail_if (sm < 0 || sm > G_ENUM_CLASS (tclass)->n_values,
        "setting photography scene mode failed");
    sm = orig_sm;
  }
  g_type_class_unref (tclass);
}

static void
test_camerabin_properties (GstElement * cam)
{
  guint flags;
  gint zoom;
  gboolean mute;

  flags = 0x1f;
  g_object_set (G_OBJECT (cam), "flags", flags, NULL);
  g_object_get (G_OBJECT (cam), "flags", &flags, NULL);
  fail_if (flags != 0x1f, "setting camerabin flags failed");

  zoom = 200;
  g_object_set (G_OBJECT (cam), "zoom", zoom, NULL);
  g_object_get (G_OBJECT (cam), "zoom", &zoom, NULL);
  fail_if (zoom != 200, "setting camerabin zoom failed");
  g_object_set (G_OBJECT (cam), "zoom", 100, NULL);

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
    default:
      break;
  }
  return TRUE;
}

/* Validate captured files by playing them with playbin
 * and checking that no errors occur. */
static gboolean
check_file_validity (const gchar * filename)
{
  GstBus *bus;
  GMainLoop *loop = g_main_loop_new (NULL, FALSE);
  GstElement *playbin = gst_element_factory_make ("playbin2", NULL);
  GstElement *fakevideo = gst_element_factory_make ("fakesink", NULL);
  GstElement *fakeaudio = gst_element_factory_make ("fakesink", NULL);
  gchar *uri = g_strconcat ("file://", make_test_file_name (filename), NULL);

  GST_DEBUG ("checking uri: %s", uri);
  g_object_set (G_OBJECT (playbin), "uri", uri, "video-sink", fakevideo,
      "audio-sink", fakeaudio, NULL);

  bus = gst_pipeline_get_bus (GST_PIPELINE (playbin));
  gst_bus_add_watch (bus, (GstBusFunc) validity_bus_cb, loop);

  gst_element_set_state (playbin, GST_STATE_PLAYING);
  g_main_loop_run (loop);
  gst_element_set_state (playbin, GST_STATE_NULL);

  g_free (uri);
  gst_object_unref (bus);
  gst_object_unref (playbin);

  return TRUE;
}

GST_START_TEST (test_single_image_capture)
{
  if (!camera)
    return;

  /* set still image mode */
  g_object_set (camera, "mode", 0,
      "filename", make_test_file_name (SINGLE_IMAGE_FILENAME), NULL);

  /* Test photography iface settings */
  gst_element_get_state (GST_ELEMENT (camera), NULL, NULL, (2 * GST_SECOND));
  test_photography_settings (camera);
  test_photography_properties (camera);
  test_camerabin_properties (camera);

  GST_INFO ("starting capture");
  g_signal_emit_by_name (camera, "capture-start", NULL);

  g_main_loop_run (main_loop);
  gst_element_set_state (GST_ELEMENT (camera), GST_STATE_NULL);
}

GST_END_TEST;

GST_START_TEST (test_video_recording)
{
  GstCaps *preview_caps;
  preview_caps = gst_caps_from_string ("video/x-raw-rgb,width=320,height=240");

  if (!camera)
    return;

  /* Set video recording mode */
  g_object_set (camera, "mode", 1,
      "filename", make_test_file_name (VIDEO_FILENAME), NULL);

  /* Set preview-caps */
  g_object_set (camera, "preview-caps", preview_caps, NULL);

  GST_INFO ("starting capture");
  g_signal_emit_by_name (camera, "capture-start", NULL);
  /* Record for one seconds  */
  g_usleep (G_USEC_PER_SEC);
  g_signal_emit_by_name (camera, "capture-stop", NULL);

  /*check if receiving the preview-image message */
  fail_if (!received_preview_msg,
      "creating video recording preview image failed");

  gst_element_set_state (GST_ELEMENT (camera), GST_STATE_NULL);
}

GST_END_TEST;

GST_START_TEST (test_video_recording_pause)
{
  if (!camera)
    return;

  /* Set video recording mode */
  g_object_set (camera, "mode", 1,
      "filename", make_test_file_name (VIDEO_FILENAME), NULL);

  GST_INFO ("starting capture");
  g_signal_emit_by_name (camera, "capture-start", NULL);
  /* Record for one seconds  */
  g_usleep (G_USEC_PER_SEC);

  GST_INFO ("pause capture");
  g_signal_emit_by_name (camera, "capture-pause", NULL);
  /* Record for one seconds  */
  g_usleep (G_USEC_PER_SEC);

  GST_INFO ("continue capture");
  g_signal_emit_by_name (camera, "capture-start", NULL);
  /* Record for one seconds  */
  g_usleep (G_USEC_PER_SEC);
  g_signal_emit_by_name (camera, "capture-stop", NULL);
  gst_element_set_state (GST_ELEMENT (camera), GST_STATE_NULL);
}

GST_END_TEST;

GST_START_TEST (test_image_video_cycle)
{
  if (!camera)
    return;

  cycle_count = 2;

  /* set still image mode */
  g_object_set (camera, "mode", 0,
      "filename", make_test_file_name (CYCLE_IMAGE_FILENAME), NULL);

  GST_INFO ("starting capture");
  g_signal_emit_by_name (camera, "capture-start", NULL);

  g_main_loop_run (main_loop);
  gst_element_set_state (GST_ELEMENT (camera), GST_STATE_NULL);
}

GST_END_TEST;

GST_START_TEST (validate_captured_image_files)
{
  if (!camera)
    return;

  /* validate single image */
  check_file_validity (SINGLE_IMAGE_FILENAME);

/* burst capture is not supported in camerabin for the moment */
#ifdef ENABLE_BURST_CAPTURE
  GString *filename;
  gint i;

  /* validate burst mode images */
  filename = g_string_new ("");
  for (i = 0; i < MAX_BURST_IMAGES; i++) {
    g_string_printf (filename, "%02u_%s", i, BURST_IMAGE_FILENAME);
    check_file_validity (filename->str);
  }
  g_string_free (filename, TRUE);
#endif
  /* validate cycled image */
  check_file_validity (CYCLE_IMAGE_FILENAME);
}

GST_END_TEST;

GST_START_TEST (validate_captured_video_files)
{
  if (!camera)
    return;

  /* validate video recording */
  check_file_validity (VIDEO_FILENAME);

  /* validate cycled  video */
  check_file_validity (CYCLE_VIDEO_FILENAME);
}

GST_END_TEST;

static Suite *
camerabin_suite (void)
{
  Suite *s = suite_create ("camerabin");
  TCase *tc_basic = tcase_create ("general");
  TCase *tc_validate = tcase_create ("validate");

  /* Test that basic operations run without errors */
  suite_add_tcase (s, tc_basic);
  /* Increase timeout due to video recording */
  tcase_set_timeout (tc_basic, 20);
  tcase_add_checked_fixture (tc_basic, setup, teardown);
  tcase_add_test (tc_basic, test_single_image_capture);
  tcase_add_test (tc_basic, test_video_recording);
  tcase_add_test (tc_basic, test_video_recording_pause);
  tcase_add_test (tc_basic, test_image_video_cycle);

  /* Validate captured files */
  suite_add_tcase (s, tc_validate);
  /* Increase timeout due to file playback */
  tcase_set_timeout (tc_validate, 20);
  tcase_add_test (tc_validate, validate_captured_image_files);
  tcase_add_test (tc_validate, validate_captured_video_files);

  return s;
}

GST_CHECK_MAIN (camerabin);
