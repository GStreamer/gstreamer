/* GStreamer unit tests for the deinterlace element
 * Copyright (C) 2010 Thiago Santos <thiago.sousa.santos@collabora.co.uk>
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
# include "config.h"
#endif

#include <stdio.h>
#include <gst/check/gstcheck.h>
#include <gst/video/video.h>

static gboolean
gst_caps_is_interlaced (GstCaps * caps)
{
  GstStructure G_GNUC_UNUSED *structure;
  gboolean interlaced = FALSE;

  fail_unless (gst_caps_is_fixed (caps));
  structure = gst_caps_get_structure (caps, 0);
  fail_unless (gst_video_format_parse_caps_interlaced (caps, &interlaced));
  return interlaced;
}

GST_START_TEST (test_create_and_unref)
{
  GstElement *deinterlace;

  deinterlace = gst_element_factory_make ("deinterlace", NULL);
  fail_unless (deinterlace != NULL);

  gst_element_set_state (deinterlace, GST_STATE_NULL);
  gst_object_unref (deinterlace);
}

GST_END_TEST;

#define CAPS_VIDEO_COMMON \
    "width=(int)800, height=(int)600, framerate=(fraction)15/1"

#define CAPS_IMAGE_COMMON \
    "width=(int)3200, height=(int)3400, framerate=(fraction)0/1"

#define CAPS_YUY2 \
    "video/x-raw-yuv, " \
    CAPS_VIDEO_COMMON ", " \
    "format=(fourcc)YUY2"

#define CAPS_YUY2_INTERLACED \
    CAPS_YUY2 ", " \
    "interlaced=(boolean)true"

#define CAPS_YVYU \
    "video/x-raw-yuv, " \
    CAPS_VIDEO_COMMON ", " \
    "format=(fourcc)YVYU"

#define CAPS_YVYU_INTERLACED \
    CAPS_YVYU ", " \
    "interlaced=(boolean)true"

#define CAPS_YUY2_IMAGE \
    "video/x-raw-yuv, " \
    CAPS_IMAGE_COMMON ", " \
    "format=(fourcc)YUY2"

#define CAPS_YUY2_INTERLACED_IMAGE \
    CAPS_YUY2_IMAGE ", " \
    "interlaced=(boolean)true"

#define CAPS_YVYU_IMAGE \
    "video/x-raw-yuv, " \
    CAPS_IMAGE_COMMON ", " \
    "format=(fourcc)YVYU"

#define CAPS_YVYU_INTERLACED_IMAGE \
    CAPS_YVYU_IMAGE ", " \
    "interlaced=(boolean)true"

static GstElement *deinterlace;
static GstPad *srcpad;
static GstPad *sinkpad;

static GstElement *pipeline;

/* sets up deinterlace and shortcut pointers to its pads */
static void
setup_deinterlace (void)
{
  deinterlace = gst_element_factory_make ("deinterlace", NULL);
  fail_unless (deinterlace != NULL);

  sinkpad = gst_element_get_static_pad (deinterlace, "sink");
  fail_unless (sinkpad != NULL);
  srcpad = gst_element_get_static_pad (deinterlace, "src");
  fail_unless (srcpad != NULL);
}

/* sets up a basic test pipeline containing:
 *
 * videotestsrc ! capsfilter ! deinterlace ! fakesink
 *
 * The parameters set the capsfilter caps and the num-buffers
 * property of videotestsrc
 *
 * It is useful for adding buffer probes to deinterlace pads
 * and validating inputs/outputs
 */
static void
setup_test_pipeline (gint mode, GstCaps * infiltercaps, GstCaps * outfiltercaps,
    gint numbuffers)
{
  GstElement *src;
  GstElement *infilter;
  GstElement *outfilter;
  GstElement *sink;

  setup_deinterlace ();

  pipeline = gst_pipeline_new ("pipeline");
  src = gst_element_factory_make ("videotestsrc", NULL);
  infilter = gst_element_factory_make ("capsfilter", "infilter");
  outfilter = gst_element_factory_make ("capsfilter", "outfilter");
  sink = gst_element_factory_make ("fakesink", NULL);
  fail_if (src == NULL);
  fail_if (infilter == NULL);
  fail_if (outfilter == NULL);
  fail_if (sink == NULL);

  fail_unless (gst_bin_add (GST_BIN (pipeline), src));
  fail_unless (gst_bin_add (GST_BIN (pipeline), infilter));
  fail_unless (gst_bin_add (GST_BIN (pipeline), deinterlace));
  fail_unless (gst_bin_add (GST_BIN (pipeline), outfilter));
  fail_unless (gst_bin_add (GST_BIN (pipeline), sink));

  /* set the properties */
  g_object_set (deinterlace, "mode", mode, NULL);
  if (numbuffers > 0)
    g_object_set (src, "num-buffers", numbuffers, NULL);
  if (infiltercaps)
    g_object_set (infilter, "caps", infiltercaps, NULL);
  if (outfiltercaps)
    g_object_set (outfilter, "caps", outfiltercaps, NULL);

  fail_unless (gst_element_link_many (src, infilter, deinterlace, outfilter,
          sink, NULL));
  if (infiltercaps)
    gst_caps_unref (infiltercaps);
  if (outfiltercaps)
    gst_caps_unref (outfiltercaps);
}

/*
 * Checks if 2 buffers are equal
 *
 * Equals means same caps and same data
 */
static gboolean
test_buffer_equals (GstBuffer * buf_a, GstBuffer * buf_b)
{
  GstCaps *caps_a;
  GstCaps *caps_b;

  if (GST_BUFFER_SIZE (buf_a) != GST_BUFFER_SIZE (buf_b))
    return FALSE;

  caps_a = gst_buffer_get_caps (buf_a);
  caps_b = gst_buffer_get_caps (buf_b);

  if (!gst_caps_is_equal (caps_a, caps_b))
    return FALSE;

  gst_caps_unref (caps_a);
  gst_caps_unref (caps_b);

  return memcmp (GST_BUFFER_DATA (buf_a), GST_BUFFER_DATA (buf_b),
      GST_BUFFER_SIZE (buf_a)) == 0;
}

static gboolean
sinkpad_enqueue_buffer (GstPad * pad, GstBuffer * buf, gpointer data)
{
  GQueue *queue = (GQueue *) data;

  /* enqueue a copy for being compared later */
  g_queue_push_tail (queue, gst_buffer_copy (buf));

  return TRUE;
}

/*
 * pad buffer probe that compares the buffer with the top one
 * in the GQueue passed as the user data
 */
static gboolean
srcpad_dequeue_and_compare_buffer (GstPad * pad, GstBuffer * buf, gpointer data)
{
  GQueue *queue = (GQueue *) data;
  GstBuffer *queue_buf;

  queue_buf = (GstBuffer *) g_queue_pop_head (queue);
  fail_if (queue_buf == NULL);

  fail_unless (test_buffer_equals (buf, queue_buf));

  gst_buffer_unref (queue_buf);

  return TRUE;
}

/*
 * Utility function that sets up a pipeline with deinterlace for
 * validanting that it operates in passthrough mode when receiving
 * data with 'infiltercaps' as the input caps and operating in 'mode' mode
 */
static void
deinterlace_check_passthrough (gint mode, const gchar * infiltercaps)
{
  GstMessage *msg;
  GQueue *queue;
  GstCaps *incaps = NULL;

  if (infiltercaps)
    incaps = gst_caps_from_string (infiltercaps);

  setup_test_pipeline (mode, incaps, NULL, 20);

  queue = g_queue_new ();

  /* set up probes for testing */
  gst_pad_add_buffer_probe (sinkpad, (GCallback) sinkpad_enqueue_buffer, queue);
  gst_pad_add_buffer_probe (srcpad,
      (GCallback) srcpad_dequeue_and_compare_buffer, queue);

  fail_unless (gst_element_set_state (pipeline, GST_STATE_PLAYING) !=
      GST_STATE_CHANGE_FAILURE);

  msg = gst_bus_poll (GST_ELEMENT_BUS (pipeline), GST_MESSAGE_EOS, -1);
  gst_message_unref (msg);

  /* queue should be empty */
  fail_unless (g_queue_is_empty (queue));

  fail_unless (gst_element_set_state (pipeline, GST_STATE_NULL) ==
      GST_STATE_CHANGE_SUCCESS);
  gst_object_unref (pipeline);
  g_queue_free (queue);
}

/*
 * Sets the caps on deinterlace sinkpad and validates the
 * caps that is set on the srcpad
 */
static void
deinterlace_set_caps_and_check (GstCaps * input, gboolean must_deinterlace)
{
  GstCaps *othercaps = NULL;

  fail_unless (gst_pad_set_caps (sinkpad, input));
  g_object_get (srcpad, "caps", &othercaps, NULL);

  if (must_deinterlace) {
    fail_if (gst_caps_is_interlaced (othercaps));
  } else {
    GstStructure *s;

    fail_unless (gst_caps_is_interlaced (input) ==
        gst_caps_is_interlaced (othercaps));

    othercaps = gst_caps_make_writable (othercaps);
    s = gst_caps_get_structure (othercaps, 0);
    gst_structure_remove_field (s, "interlaced");

    input = gst_caps_make_writable (input);
    s = gst_caps_get_structure (input, 0);
    gst_structure_remove_field (s, "interlaced");

    fail_unless (gst_caps_is_equal (input, othercaps));
  }
  gst_caps_unref (input);
  gst_caps_unref (othercaps);
}

static void
deinterlace_set_string_caps_and_check (const gchar * input,
    gboolean must_deinterlace)
{
  deinterlace_set_caps_and_check (gst_caps_from_string (input),
      must_deinterlace);
}

GST_START_TEST (test_mode_auto_accept_caps)
{
  setup_deinterlace ();

  /* auto mode */
  g_object_set (deinterlace, "mode", 0, NULL);
  fail_unless (gst_element_set_state (deinterlace, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_SUCCESS);

  /* try to set non interlaced caps */
  deinterlace_set_string_caps_and_check (CAPS_YVYU, FALSE);
  deinterlace_set_string_caps_and_check (CAPS_YUY2, FALSE);
  deinterlace_set_string_caps_and_check (CAPS_YVYU_IMAGE, FALSE);
  deinterlace_set_string_caps_and_check (CAPS_YUY2_IMAGE, FALSE);

  /* now try to set interlaced caps */
  deinterlace_set_string_caps_and_check (CAPS_YVYU_INTERLACED, TRUE);
  deinterlace_set_string_caps_and_check (CAPS_YUY2_INTERLACED, TRUE);
  deinterlace_set_string_caps_and_check (CAPS_YVYU_INTERLACED_IMAGE, TRUE);
  deinterlace_set_string_caps_and_check (CAPS_YUY2_INTERLACED_IMAGE, TRUE);

  /* cleanup */
  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);
  fail_unless (gst_element_set_state (deinterlace, GST_STATE_NULL) ==
      GST_STATE_CHANGE_SUCCESS);
  gst_object_unref (deinterlace);
}

GST_END_TEST;

GST_START_TEST (test_mode_forced_accept_caps)
{
  setup_deinterlace ();

  /* forced mode */
  g_object_set (deinterlace, "mode", 1, NULL);
  fail_unless (gst_element_set_state (deinterlace, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_SUCCESS);

  /* try to set non interlaced caps */
  deinterlace_set_string_caps_and_check (CAPS_YVYU, TRUE);
  deinterlace_set_string_caps_and_check (CAPS_YUY2, TRUE);
  deinterlace_set_string_caps_and_check (CAPS_YVYU_IMAGE, TRUE);
  deinterlace_set_string_caps_and_check (CAPS_YUY2_IMAGE, TRUE);

  /* now try to set interlaced caps */
  deinterlace_set_string_caps_and_check (CAPS_YVYU_INTERLACED, TRUE);
  deinterlace_set_string_caps_and_check (CAPS_YUY2_INTERLACED, TRUE);
  deinterlace_set_string_caps_and_check (CAPS_YVYU_INTERLACED_IMAGE, TRUE);
  deinterlace_set_string_caps_and_check (CAPS_YUY2_INTERLACED_IMAGE, TRUE);

  /* cleanup */
  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);
  fail_unless (gst_element_set_state (deinterlace, GST_STATE_NULL) ==
      GST_STATE_CHANGE_SUCCESS);
  gst_object_unref (deinterlace);
}

GST_END_TEST;

GST_START_TEST (test_mode_disabled_accept_caps)
{
  setup_deinterlace ();

  /* disabled mode */
  g_object_set (deinterlace, "mode", 2, NULL);
  fail_unless (gst_element_set_state (deinterlace, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_SUCCESS);

  /* try to set non interlaced caps */
  deinterlace_set_string_caps_and_check (CAPS_YVYU, FALSE);
  deinterlace_set_string_caps_and_check (CAPS_YUY2, FALSE);
  deinterlace_set_string_caps_and_check (CAPS_YVYU_IMAGE, FALSE);
  deinterlace_set_string_caps_and_check (CAPS_YUY2_IMAGE, FALSE);

  /* now try to set interlaced caps */
  deinterlace_set_string_caps_and_check (CAPS_YVYU_INTERLACED, FALSE);
  deinterlace_set_string_caps_and_check (CAPS_YUY2_INTERLACED, FALSE);
  deinterlace_set_string_caps_and_check (CAPS_YVYU_INTERLACED_IMAGE, FALSE);
  deinterlace_set_string_caps_and_check (CAPS_YUY2_INTERLACED_IMAGE, FALSE);

  /* cleanup */
  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);
  fail_unless (gst_element_set_state (deinterlace, GST_STATE_NULL) ==
      GST_STATE_CHANGE_SUCCESS);
  gst_object_unref (deinterlace);
}

GST_END_TEST;

GST_START_TEST (test_mode_disabled_passthrough)
{
  /* 2 is disabled mode */
  deinterlace_check_passthrough (2, CAPS_YUY2_INTERLACED);
  deinterlace_check_passthrough (2, CAPS_YVYU_INTERLACED);
  deinterlace_check_passthrough (2, CAPS_YUY2);
  deinterlace_check_passthrough (2, CAPS_YVYU);

  deinterlace_check_passthrough (2, CAPS_YUY2_INTERLACED_IMAGE);
  deinterlace_check_passthrough (2, CAPS_YVYU_INTERLACED_IMAGE);
  deinterlace_check_passthrough (2, CAPS_YUY2_IMAGE);
  deinterlace_check_passthrough (2, CAPS_YVYU_IMAGE);
}

GST_END_TEST;

GST_START_TEST (test_mode_auto_deinterlaced_passthrough)
{
  /* 0 is auto mode */
  deinterlace_check_passthrough (0, CAPS_YUY2);
  deinterlace_check_passthrough (0, CAPS_YVYU);
  deinterlace_check_passthrough (0, CAPS_YUY2_IMAGE);
  deinterlace_check_passthrough (0, CAPS_YVYU_IMAGE);
}

GST_END_TEST;

static Suite *
deinterlace_suite (void)
{
  Suite *s = suite_create ("deinterlace");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_set_timeout (tc_chain, 180);
  tcase_add_test (tc_chain, test_create_and_unref);
  tcase_add_test (tc_chain, test_mode_auto_accept_caps);
  tcase_add_test (tc_chain, test_mode_forced_accept_caps);
  tcase_add_test (tc_chain, test_mode_disabled_accept_caps);
  tcase_add_test (tc_chain, test_mode_disabled_passthrough);
  tcase_add_test (tc_chain, test_mode_auto_deinterlaced_passthrough);

  return s;
}

GST_CHECK_MAIN (deinterlace);
