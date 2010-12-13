/* GStreamer
 *
 * unit test for camerabin2 basic operations
 * Copyright (C) 2010 Nokia Corporation <multimedia@maemo.org>
 * Copyright (C) 2010 Thiago Santos <thiago.sousa.santos@collabora.co.uk>
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
#include <gst/video/video.h>
#include <gst/check/gstcheck.h>
#include <gst/basecamerabinsrc/gstbasecamerasrc.h>

#define IMAGE_FILENAME "image"
#define VIDEO_FILENAME "video"
#define CAPTURE_COUNT 3
#define VIDEO_DURATION 5

#define VIDEO_PAD_SUPPORTED_CAPS GST_VIDEO_CAPS_RGB ", width=600, height=480"
#define IMAGE_PAD_SUPPORTED_CAPS GST_VIDEO_CAPS_RGB ", width=800, height=600"

/* custom test camera src element */
#define GST_TYPE_TEST_CAMERA_SRC \
  (gst_test_camera_src_get_type())
#define GST_TEST_CAMERA_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TEST_CAMERA_SRC,GstTestCameraSrc))
#define GST_TEST_CAMERA_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TEST_CAMERA_SRC,GstTestCameraSrcClass))
#define GST_IS_TEST_REVERSE_NEGOTIATION_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TEST_CAMERA_SRC))
#define GST_IS_TEST_REVERSE_NEGOTIATION_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TEST_CAMERA_SRC))
#define GST_TEST_CAMERA_SRC_CAST(obj) ((GstTestCameraSrc *)obj)

typedef struct _GstTestCameraSrc GstTestCameraSrc;
typedef struct _GstTestCameraSrcClass GstTestCameraSrcClass;
struct _GstTestCameraSrc
{
  GstBaseCameraSrc element;

  GstPad *vfpad;
  GstPad *vidpad;
  GstPad *imgpad;

  GstCameraBinMode mode;
};

struct _GstTestCameraSrcClass
{
  GstBaseCameraSrcClass parent_class;
};

GType gst_test_camera_src_get_type (void);

GST_BOILERPLATE (GstTestCameraSrc,
    gst_test_camera_src, GstBaseCameraSrc, GST_TYPE_BASE_CAMERA_SRC);

static gboolean
gst_test_camera_src_set_mode (GstBaseCameraSrc * src, GstCameraBinMode mode)
{
  GstTestCameraSrc *self = GST_TEST_CAMERA_SRC (src);

  self->mode = mode;
  return TRUE;
}

static GstCaps *
gst_test_camera_src_get_caps (GstPad * pad)
{
  GstTestCameraSrc *self = (GstTestCameraSrc *) GST_PAD_PARENT (pad);
  GstCaps *result = NULL;

  if (pad == self->vfpad) {
    result = gst_caps_new_any ();
  } else if (pad == self->vidpad) {
    result = gst_caps_from_string (VIDEO_PAD_SUPPORTED_CAPS);
  } else if (pad == self->imgpad) {
    result = gst_caps_from_string (IMAGE_PAD_SUPPORTED_CAPS);
  } else {
    g_assert_not_reached ();
  }

  return result;
}

static void
gst_test_camera_src_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (gstelement_class,
      "Test Camera Src",
      "Camera/Src",
      "Some test camera src",
      "Thiago Santos <thiago.sousa.santos@collabora.co.uk>");
}

static void
gst_test_camera_src_class_init (GstTestCameraSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseCameraSrcClass *gstbasecamera_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  gstbasecamera_class = GST_BASE_CAMERA_SRC_CLASS (klass);

  gstbasecamera_class->set_mode = gst_test_camera_src_set_mode;
}

static void
gst_test_camera_src_init (GstTestCameraSrc * self,
    GstTestCameraSrcClass * g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);
  GstPadTemplate *template;

  /* create pads */
  template = gst_element_class_get_pad_template (gstelement_class,
      GST_BASE_CAMERA_SRC_VIEWFINDER_PAD_NAME);
  self->vfpad = gst_pad_new_from_template (template,
      GST_BASE_CAMERA_SRC_VIEWFINDER_PAD_NAME);
  gst_element_add_pad (GST_ELEMENT_CAST (self), self->vfpad);

  template = gst_element_class_get_pad_template (gstelement_class,
      GST_BASE_CAMERA_SRC_IMAGE_PAD_NAME);
  self->imgpad = gst_pad_new_from_template (template,
      GST_BASE_CAMERA_SRC_IMAGE_PAD_NAME);
  gst_element_add_pad (GST_ELEMENT_CAST (self), self->imgpad);

  template = gst_element_class_get_pad_template (gstelement_class,
      GST_BASE_CAMERA_SRC_VIDEO_PAD_NAME);
  self->vidpad = gst_pad_new_from_template (template,
      GST_BASE_CAMERA_SRC_VIDEO_PAD_NAME);
  gst_element_add_pad (GST_ELEMENT_CAST (self), self->vidpad);

  /* add get caps functions */
  gst_pad_set_getcaps_function (self->vfpad, gst_test_camera_src_get_caps);
  gst_pad_set_getcaps_function (self->vidpad, gst_test_camera_src_get_caps);
  gst_pad_set_getcaps_function (self->imgpad, gst_test_camera_src_get_caps);
}

/* end of custom test camera src element */


static GstElement *camera;
static GMainLoop *main_loop;

/* helper function for filenames */
static const gchar *
make_test_file_name (const gchar * base_name, gint num)
{
  static gchar file_name[1000];

  /* num == -1 means to keep the %d in the resulting string to be used on
   * multifilesink like location */
  if (num == -1) {
    g_snprintf (file_name, 999, "%s" G_DIR_SEPARATOR_S
        "gstcamerabin2test_%s_%%03d.cap", g_get_tmp_dir (), base_name);
  } else {
    g_snprintf (file_name, 999, "%s" G_DIR_SEPARATOR_S
        "gstcamerabin2test_%s_%03d.cap", g_get_tmp_dir (), base_name, num);
  }

  GST_INFO ("capturing to: %s", file_name);
  return file_name;
}

/* configuration */

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

static void
setup (void)
{
  GstBus *bus;
  GstElement *vfbin;
  GstElement *fakevideosink;
  GstElement *src;

  GST_INFO ("init");

  main_loop = g_main_loop_new (NULL, TRUE);

  camera = gst_check_setup_element ("camerabin2");
  fakevideosink = gst_check_setup_element ("fakesink");
  src = gst_check_setup_element ("v4l2camerasrc");

  vfbin = gst_bin_get_by_name (GST_BIN (camera), "vf-bin");
  g_object_set (G_OBJECT (vfbin), "video-sink", fakevideosink, NULL);
  gst_object_unref (vfbin);
  gst_object_unref (fakevideosink);

  bus = gst_pipeline_get_bus (GST_PIPELINE (camera));
  gst_bus_add_watch (bus, (GstBusFunc) capture_bus_cb, main_loop);
  gst_object_unref (bus);

  GST_INFO ("init finished");
}

static void
teardown (void)
{
  gst_element_set_state (camera, GST_STATE_NULL);

  if (camera)
    gst_check_teardown_element (camera);

  GST_INFO ("done");
}

static gboolean
validity_bus_cb (GstBus * bus, GstMessage * message, gpointer data)
{
  GMainLoop *loop = (GMainLoop *) data;
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:{
      GError *err = NULL;
      gchar *debug = NULL;

      gst_message_parse_error (message, &err, &debug);

      GST_ERROR ("Error: %s : %s", err->message, debug);
      g_error_free (err);
      g_free (debug);

      fail_if (TRUE, "validating captured data failed");
      g_main_loop_quit (loop);
    }
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
check_file_validity (const gchar * filename, gint num, GstTagList * taglist)
{
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
  g_object_set (camera, "mode", 1,
      "location", make_test_file_name (IMAGE_FILENAME, -1), NULL);

  if (gst_element_set_state (GST_ELEMENT (camera), GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE) {
    GST_WARNING ("setting camerabin to PLAYING failed");
    gst_element_set_state (GST_ELEMENT (camera), GST_STATE_NULL);
    gst_object_unref (camera);
    camera = NULL;
  }
  GST_INFO ("starting capture");
  fail_unless (camera != NULL);
  g_signal_emit_by_name (camera, "start-capture", NULL);

  g_usleep (G_USEC_PER_SEC * 3);

  gst_element_set_state (GST_ELEMENT (camera), GST_STATE_NULL);
  check_file_validity (IMAGE_FILENAME, 0, NULL);
}

GST_END_TEST;

GST_START_TEST (test_video_recording)
{
  if (!camera)
    return;

  /* Set video recording mode */
  g_object_set (camera, "mode", 2,
      "location", make_test_file_name (VIDEO_FILENAME, -1), NULL);

  if (gst_element_set_state (GST_ELEMENT (camera), GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE) {
    GST_WARNING ("setting camerabin to PLAYING failed");
    gst_element_set_state (GST_ELEMENT (camera), GST_STATE_NULL);
    gst_object_unref (camera);
    camera = NULL;
  }

  GST_INFO ("starting capture");
  fail_unless (camera != NULL);
  g_signal_emit_by_name (camera, "start-capture", NULL);

  /* Record for one seconds  */
  g_usleep (VIDEO_DURATION * G_USEC_PER_SEC);

  g_signal_emit_by_name (camera, "stop-capture", NULL);

  gst_element_set_state (GST_ELEMENT (camera), GST_STATE_NULL);

  check_file_validity (VIDEO_FILENAME, 0, NULL);
}

GST_END_TEST;

GST_START_TEST (test_image_video_cycle)
{
  gint i;

  if (!camera)
    return;

  /* set filepaths for image and videos */
  g_object_set (camera, "mode", 1, NULL);
  g_object_set (camera, "location", make_test_file_name (IMAGE_FILENAME, -1),
      NULL);
  g_object_set (camera, "mode", 2, NULL);
  g_object_set (camera, "location", make_test_file_name (VIDEO_FILENAME, -1),
      NULL);

  if (gst_element_set_state (GST_ELEMENT (camera), GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE) {
    GST_WARNING ("setting camerabin to PLAYING failed");
    gst_element_set_state (GST_ELEMENT (camera), GST_STATE_NULL);
    gst_object_unref (camera);
    camera = NULL;
  }

  GST_INFO ("starting capture");
  for (i = 0; i < 2; i++) {
    /* take a picture */
    g_object_set (camera, "mode", 1, NULL);
    g_signal_emit_by_name (camera, "start-capture", NULL);
    g_usleep (G_USEC_PER_SEC * 3);

    /* now go to video */
    g_object_set (camera, "mode", 2, NULL);
    g_signal_emit_by_name (camera, "start-capture", NULL);
    g_usleep (G_USEC_PER_SEC * 5);
    g_signal_emit_by_name (camera, "stop-capture", NULL);

    /* wait for capture to finish */
    g_usleep (G_USEC_PER_SEC);
  }
  gst_element_set_state (GST_ELEMENT (camera), GST_STATE_NULL);

  /* validate all the files */
  for (i = 0; i < 2; i++) {
    check_file_validity (IMAGE_FILENAME, i, NULL);
    check_file_validity (VIDEO_FILENAME, i, NULL);
  }
}

GST_END_TEST;

GST_START_TEST (test_supported_caps)
{
  GstCaps *padcaps = NULL;
  GstCaps *expectedcaps;
  GstElement *src;

  if (!camera)
    return;

  src = g_object_new (GST_TYPE_TEST_CAMERA_SRC, NULL);
  g_object_set (camera, "camera-src", src, NULL);
  gst_object_unref (src);

  if (gst_element_set_state (GST_ELEMENT (camera), GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE) {
    GST_WARNING ("setting camerabin to PLAYING failed");
    gst_element_set_state (GST_ELEMENT (camera), GST_STATE_NULL);
    gst_object_unref (camera);
    camera = NULL;
  }
  g_assert (camera != NULL);

  expectedcaps = gst_caps_from_string (VIDEO_PAD_SUPPORTED_CAPS);
  g_object_get (G_OBJECT (camera), "video-capture-supported-caps", &padcaps,
      NULL);
  g_assert (expectedcaps != NULL);
  g_assert (padcaps != NULL);
  g_assert (gst_caps_is_equal (padcaps, expectedcaps));
  gst_caps_unref (expectedcaps);
  gst_caps_unref (padcaps);

  expectedcaps = gst_caps_from_string (IMAGE_PAD_SUPPORTED_CAPS);
  g_object_get (G_OBJECT (camera), "image-capture-supported-caps", &padcaps,
      NULL);
  g_assert (expectedcaps != NULL);
  g_assert (padcaps != NULL);
  g_assert (gst_caps_is_equal (padcaps, expectedcaps));
  gst_caps_unref (expectedcaps);
  gst_caps_unref (padcaps);

  gst_element_set_state (camera, GST_STATE_NULL);
}

GST_END_TEST;

static Suite *
camerabin_suite (void)
{
  Suite *s = suite_create ("camerabin2");
  TCase *tc_basic = tcase_create ("general");

  /* Test that basic operations run without errors */
  suite_add_tcase (s, tc_basic);
  /* Increase timeout due to video recording */
  tcase_set_timeout (tc_basic, 30);
  tcase_add_checked_fixture (tc_basic, setup, teardown);
  tcase_add_test (tc_basic, test_single_image_capture);
  tcase_add_test (tc_basic, test_video_recording);
  tcase_add_test (tc_basic, test_image_video_cycle);
  tcase_add_test (tc_basic, test_supported_caps);

  return s;
}

GST_CHECK_MAIN (camerabin);
