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
#include <gst/gst.h>
#include <gst/check/gstcheck.h>

#ifdef HAVE_GST_PHOTO_IFACE_H
#include <gst/interfaces/photography.h>
#endif

#define SINGLE_IMAGE_FILENAME "image.cap"
#define BURST_IMAGE_FILENAME "burst_image.cap"
#define VIDEO_FILENAME "video.cap"
#define CYCLE_IMAGE_FILENAME "cycle_image.cap"
#define CYCLE_VIDEO_FILENAME "cycle_video.cap"
#define MAX_BURST_IMAGES 10
#define PHOTO_SETTING_DELAY_US 0

static gboolean continuous = FALSE;
static guint captured_images = 0;

static GstElement *camera;
static GCond *cam_cond;
static GMutex *cam_mutex;


/* helper function for filenames */
static const gchar *
make_test_file_name (const gchar * base_name)
{
  static gchar file_name[1000];

  g_snprintf (file_name, 999, "%s" G_DIR_SEPARATOR_S "%s",
      g_get_tmp_dir (), base_name);

  GST_INFO ("capturing to: %s", file_name);
  return file_name;
}

static const gchar *
make_test_seq_file_name (const gchar * base_name)
{
  static gchar file_name[1000];

  g_snprintf (file_name, 999, "%s" G_DIR_SEPARATOR_S "%02u_%s",
      g_get_tmp_dir (), captured_images, base_name);

  GST_INFO ("capturing to: %s", file_name);
  return file_name;
}

/* signal handlers */

static gboolean
capture_done (GstElement * elem, GString * filename, gpointer user_data)
{
  captured_images++;

  if (captured_images >= MAX_BURST_IMAGES) {
    /* release the shutter button */
    g_mutex_lock (cam_mutex);
    g_cond_signal (cam_cond);
    g_mutex_unlock (cam_mutex);
    continuous = FALSE;
  }

  if (continuous) {
    g_string_assign (filename, make_test_seq_file_name (BURST_IMAGE_FILENAME));
    /* on needs to modify the pssed GString, the code below won't work
       g_object_set (G_OBJECT (elem), "filename",
       make_test_seq_file_name (BURST_IMAGE_FILENAME), NULL);
     */
  }

  return continuous;
}

/* configuration */

static void
setup_camerabin_elements (GstElement * camera)
{
  GstElement *vfsink, *audiosrc, *videosrc;

  /* Use fakesink for view finder */
  vfsink = gst_element_factory_make ("fakesink", NULL);
  audiosrc = gst_element_factory_make ("audiotestsrc", NULL);
  g_object_set (audiosrc, "is-live", TRUE, NULL);
  videosrc = gst_element_factory_make ("videotestsrc", NULL);
  g_object_set (videosrc, "is-live", TRUE, NULL);

  g_object_set (camera, "vfsink", vfsink, "audiosrc", audiosrc,
      "videosrc", videosrc, NULL);
}

static void
setup (void)
{
  GstTagSetter *setter;
  gchar *desc_str;

  cam_cond = g_cond_new ();
  cam_mutex = g_mutex_new ();

  camera = gst_check_setup_element ("camerabin");

  setup_camerabin_elements (camera);

  g_signal_connect (camera, "img-done", G_CALLBACK (capture_done), NULL);

  captured_images = 0;

  /* Set some default tags */
  setter = GST_TAG_SETTER (camera);
  desc_str = g_strdup_printf ("Created by %s", g_get_real_name ());

  gst_tag_setter_add_tags (setter, GST_TAG_MERGE_REPLACE,
      GST_TAG_DESCRIPTION, desc_str, NULL);

  gst_element_set_state (GST_ELEMENT (camera), GST_STATE_PLAYING);

  g_free (desc_str);
}

static void
teardown (void)
{
  g_mutex_free (cam_mutex);
  g_cond_free (cam_cond);
  gst_check_teardown_element (camera);
}

static void
test_photography_settings (GstElement * cam)
{
#ifdef HAVE_GST_PHOTO_IFACE_H
  GTypeClass *tclass;

  if (!GST_IS_PHOTOGRAPHY (cam)) {
    GST_WARNING ("omitting photography test");
    return;
  }

  gfloat ev_comp, orig_ev_comp;
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
  guint iso_speed = 100, orig_iso_speed;
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
  GstFlashMode flash, orig_flash;
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
  GstWhiteBalanceMode wb, orig_wb;
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
  GstColourToneMode ct, orig_ct;
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
  GstSceneMode sm, orig_sm;
  for (sm = 0; sm < G_ENUM_CLASS (tclass)->n_values; sm++) {
    orig_sm = sm;
    gst_photography_set_scene_mode (GST_PHOTOGRAPHY (cam), sm);
    gst_photography_get_scene_mode (GST_PHOTOGRAPHY (cam), &sm);
    fail_if (orig_sm != sm, "setting photography scene mode failed");
    sm = orig_sm;
    g_usleep (PHOTO_SETTING_DELAY_US);
  }
  g_type_class_unref (tclass);

  gfloat zoom, orig_zoom;
  gst_photography_set_zoom (GST_PHOTOGRAPHY (cam), zoom);
  for (zoom = 1.0; zoom <= 10.0; zoom += 1.0) {
    orig_zoom = zoom;
    gst_photography_set_zoom (GST_PHOTOGRAPHY (cam), zoom);
    gst_photography_get_zoom (GST_PHOTOGRAPHY (cam), &zoom);
    fail_if (orig_zoom != zoom, "setting photography zoom failed");
    zoom = orig_zoom;
    g_usleep (PHOTO_SETTING_DELAY_US);
  }
#else
  GST_DEBUG ("omitting photography test");
#endif
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
  GMainLoop *loop = g_main_loop_new (NULL, TRUE);
  GstElement *playbin = gst_element_factory_make ("playbin2", NULL);
  GstElement *fakevideo = gst_element_factory_make ("fakesink", NULL);
  GstElement *fakeaudio = gst_element_factory_make ("fakesink", NULL);
  gchar *uri = g_strconcat ("file://", make_test_file_name (filename), NULL);

  GST_DEBUG ("setting uri: %s", uri);
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
  /* set still image mode */
  g_object_set (camera, "mode", 0,
      "filename", make_test_file_name (SINGLE_IMAGE_FILENAME), NULL);

  continuous = FALSE;

  /* Test photography iface settings */
  gst_element_get_state (GST_ELEMENT (camera), NULL, NULL, (2 * GST_SECOND));
  test_photography_settings (camera);

  g_signal_emit_by_name (camera, "user-start", 0);
  g_signal_emit_by_name (camera, "user-stop", 0);
}

GST_END_TEST;

GST_START_TEST (test_burst_image_capture)
{
  /* set still image mode */
  g_object_set (camera, "mode", 0,
      "filename", make_test_seq_file_name (BURST_IMAGE_FILENAME), NULL);

  /* set burst mode */
  continuous = TRUE;

  g_signal_emit_by_name (camera, "user-start", 0);

  GST_DEBUG ("waiting for img-done");
  g_mutex_lock (cam_mutex);
  g_cond_wait (cam_cond, cam_mutex);
  g_mutex_unlock (cam_mutex);

  g_signal_emit_by_name (camera, "user-stop", 0);
}

GST_END_TEST;

GST_START_TEST (test_video_recording)
{
  /* Set video recording mode */
  g_object_set (camera, "mode", 1,
      "filename", make_test_file_name (VIDEO_FILENAME), NULL);

  g_signal_emit_by_name (camera, "user-start", 0);
  /* Record for few seconds  */
  g_usleep (2 * G_USEC_PER_SEC);
  g_signal_emit_by_name (camera, "user-stop", 0);
}

GST_END_TEST;

GST_START_TEST (test_image_video_cycle)
{
  guint i;

  continuous = FALSE;

  for (i = 0; i < 2; i++) {
    /* Set still image mode */
    g_object_set (camera, "mode", 0,
        "filename", make_test_file_name (CYCLE_IMAGE_FILENAME), NULL);

    /* Take a picture */
    g_signal_emit_by_name (camera, "user-start", 0);
    g_signal_emit_by_name (camera, "user-stop", 0);

    /* Set video recording mode */
    g_object_set (camera, "mode", 1,
        "filename", make_test_file_name (CYCLE_VIDEO_FILENAME), NULL);

    /* Record video */
    g_signal_emit_by_name (camera, "user-start", 0);
    g_usleep (2 * G_USEC_PER_SEC);
    g_signal_emit_by_name (camera, "user-stop", 0);
  }
}

GST_END_TEST;

GST_START_TEST (validate_captured_files)
{
  GString *filename;
  gint i;
  /* validate single image */
  check_file_validity (SINGLE_IMAGE_FILENAME);

  /* validate burst mode images */
  filename = g_string_new ("");
  for (i = 0; i < MAX_BURST_IMAGES; i++) {
    g_string_printf (filename, "%02u_%s", i, BURST_IMAGE_FILENAME);
    check_file_validity (filename->str);
  }
  g_string_free (filename, TRUE);

  /* validate video recording */
  check_file_validity (VIDEO_FILENAME);

  /* validate cycled image and video */
  check_file_validity (CYCLE_IMAGE_FILENAME);
  check_file_validity (CYCLE_VIDEO_FILENAME);

}

GST_END_TEST;

Suite *
camerabin_suite (void)
{
  Suite *s = suite_create ("camerabin");
  TCase *tc_basic = tcase_create ("general");
  TCase *tc_validate = tcase_create ("validate");

  /* Test that basic operations run without errors */
  suite_add_tcase (s, tc_basic);
  /* Increase timeout due to video recording for now */
  tcase_set_timeout (tc_basic, 10);
  tcase_add_checked_fixture (tc_basic, setup, teardown);
  tcase_add_test (tc_basic, test_single_image_capture);
  tcase_add_test (tc_basic, test_burst_image_capture);
  tcase_add_test (tc_basic, test_video_recording);
  tcase_add_test (tc_basic, test_image_video_cycle);

  /* Validate captured files */
  suite_add_tcase (s, tc_validate);
  tcase_add_test (tc_validate, validate_captured_files);

  return s;
}

GST_CHECK_MAIN (camerabin);
