/* GStreamer unit test for multifile plugin
 *
 * Copyright (C) 2007 David A. Schleef <ds@schleef.org>
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

#include <glib/gstdio.h>
#include <gst/check/gstcheck.h>

static void
run_pipeline (GstElement * pipeline)
{
  gst_element_set_state (pipeline, GST_STATE_PAUSED);
  gst_element_get_state (pipeline, NULL, NULL, -1);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  /* FIXME too lazy */
  g_usleep (1000000);
  gst_element_set_state (pipeline, GST_STATE_NULL);
}

GST_START_TEST (test_multifilesink)
{
  GstElement *pipeline;
  int i;

  g_mkdir ("tmpdir", 0700);

  pipeline =
      gst_parse_launch
      ("videotestsrc num-buffers=10 ! video/x-raw-yuv,format=(fourcc)I420,width=320,height=240 ! multifilesink location=tmpdir/%05d",
      NULL);
  fail_if (pipeline == NULL);
  run_pipeline (pipeline);
  gst_object_unref (pipeline);

  for (i = 0; i < 10; i++) {
    char s[20];

    sprintf (s, "tmpdir/%05d", i);
    fail_if (g_remove (s) != 0);
  }
  fail_if (g_remove ("tmpdir") != 0);

}

GST_END_TEST;

GST_START_TEST (test_multifilesrc)
{
  GstElement *pipeline;
  int i;

  g_mkdir ("tmpdir", 0700);

  pipeline =
      gst_parse_launch
      ("videotestsrc num-buffers=10 ! video/x-raw-yuv,format=(fourcc)I420,width=320,height=240 ! multifilesink location=tmpdir/%05d",
      NULL);
  fail_if (pipeline == NULL);
  run_pipeline (pipeline);
  gst_object_unref (pipeline);

  pipeline =
      gst_parse_launch
      ("multifilesrc location=tmpdir/%05d ! video/x-raw-yuv,format=(fourcc)I420,width=320,height=240,framerate=10/1 ! fakesink",
      NULL);
  fail_if (pipeline == NULL);
  run_pipeline (pipeline);
  gst_object_unref (pipeline);

  for (i = 0; i < 10; i++) {
    char s[20];

    sprintf (s, "tmpdir/%05d", i);
    fail_if (g_remove (s) != 0);
  }
  fail_if (g_remove ("tmpdir") != 0);

}

GST_END_TEST;

static Suite *
libvisual_suite (void)
{
  Suite *s = suite_create ("multifile");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_multifilesink);
  tcase_add_test (tc_chain, test_multifilesrc);

  return s;
}

GST_CHECK_MAIN (libvisual);
