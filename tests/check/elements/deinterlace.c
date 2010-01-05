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
  GstStructure *structure;
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

static GstElement *deinterlace;
static GstPad *srcpad;
static GstPad *sinkpad;

static void
setup_deinterlace ()
{
  deinterlace = gst_element_factory_make ("deinterlace", NULL);
  fail_unless (deinterlace != NULL);

  sinkpad = gst_element_get_static_pad (deinterlace, "sink");
  fail_unless (sinkpad != NULL);
  srcpad = gst_element_get_static_pad (deinterlace, "src");
  fail_unless (srcpad != NULL);
}

static void
deinterlace_set_caps_and_check (GstCaps * input, gboolean must_deinterlace)
{
  GstCaps *othercaps = NULL;

  fail_unless (gst_pad_set_caps (sinkpad, input));
  g_object_get (srcpad, "caps", &othercaps, NULL);

  if (must_deinterlace) {
    fail_if (gst_caps_is_interlaced (othercaps));
  } else {
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

  /* now try to set interlaced caps */
  deinterlace_set_string_caps_and_check (CAPS_YVYU_INTERLACED, TRUE);
  deinterlace_set_string_caps_and_check (CAPS_YUY2_INTERLACED, TRUE);

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

  /* now try to set interlaced caps */
  deinterlace_set_string_caps_and_check (CAPS_YVYU_INTERLACED, TRUE);
  deinterlace_set_string_caps_and_check (CAPS_YUY2_INTERLACED, TRUE);

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

  /* now try to set interlaced caps */
  deinterlace_set_string_caps_and_check (CAPS_YVYU_INTERLACED, FALSE);
  deinterlace_set_string_caps_and_check (CAPS_YUY2_INTERLACED, FALSE);

  /* cleanup */
  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);
  fail_unless (gst_element_set_state (deinterlace, GST_STATE_NULL) ==
      GST_STATE_CHANGE_SUCCESS);
  gst_object_unref (deinterlace);
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

  return s;
}

GST_CHECK_MAIN (deinterlace);
