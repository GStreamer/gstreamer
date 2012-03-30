/* GStreamer
 *
 * Copyright (C) 2010, Thiago Santos <thiago.sousa.santos@collabora.co.uk>
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

#include <gst/check/gstcheck.h>
#include <gst/app/gstappsrc.h>

#define SAMPLE_CAPS "application/x-gst-check-test"

static GstPad *mysinkpad;

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstElement *
setup_appsrc (void)
{
  GstElement *appsrc;

  GST_DEBUG ("setup_appsrc");
  appsrc = gst_check_setup_element ("appsrc");
  mysinkpad = gst_check_setup_sink_pad (appsrc, &sinktemplate);

  gst_pad_set_active (mysinkpad, TRUE);

  return appsrc;
}

static void
cleanup_appsrc (GstElement * appsrc)
{
  GST_DEBUG ("cleanup_appsrc");

  gst_check_teardown_sink_pad (appsrc);
  gst_check_teardown_element (appsrc);
}

/*
 * Pushes 4 buffers into appsrc and checks the caps on them on the output.
 *
 * Appsrc is configured with caps=SAMPLE_CAPS, so the buffers should have the
 * same caps that they were pushed with.
 *
 * The 4 buffers have NULL, SAMPLE_CAPS, NULL, SAMPLE_CAPS caps,
 * respectively.
 */
GST_START_TEST (test_appsrc_non_null_caps)
{
  GstElement *src;
  GstBuffer *buffer;
  GstCaps *caps, *ccaps;

  src = setup_appsrc ();

  caps = gst_caps_from_string (SAMPLE_CAPS);
  g_object_set (src, "caps", caps, NULL);

  ASSERT_SET_STATE (src, GST_STATE_PLAYING, GST_STATE_CHANGE_SUCCESS);

  buffer = gst_buffer_new_and_alloc (4);
  fail_unless (gst_app_src_push_buffer (GST_APP_SRC (src),
          buffer) == GST_FLOW_OK);

  buffer = gst_buffer_new_and_alloc (4);
  fail_unless (gst_app_src_push_buffer (GST_APP_SRC (src),
          buffer) == GST_FLOW_OK);

  buffer = gst_buffer_new_and_alloc (4);
  fail_unless (gst_app_src_push_buffer (GST_APP_SRC (src),
          buffer) == GST_FLOW_OK);

  buffer = gst_buffer_new_and_alloc (4);
  fail_unless (gst_app_src_push_buffer (GST_APP_SRC (src),
          buffer) == GST_FLOW_OK);

  fail_unless (gst_app_src_end_of_stream (GST_APP_SRC (src)) == GST_FLOW_OK);

  /* Give some time to the appsrc loop to push the buffers */
  g_usleep (G_USEC_PER_SEC * 3);

  /* Check the output caps */
  fail_unless (g_list_length (buffers) == 4);

  ccaps = gst_pad_get_current_caps (mysinkpad);
  fail_unless (gst_caps_is_equal (ccaps, caps));
  gst_caps_unref (ccaps);

  ASSERT_SET_STATE (src, GST_STATE_NULL, GST_STATE_CHANGE_SUCCESS);
  gst_caps_unref (caps);
  cleanup_appsrc (src);
}

GST_END_TEST;


static Suite *
appsrc_suite (void)
{
  Suite *s = suite_create ("appsrc");
  TCase *tc_chain = tcase_create ("general");

  tcase_add_test (tc_chain, test_appsrc_non_null_caps);

  suite_add_tcase (s, tc_chain);

  return s;
}

GST_CHECK_MAIN (appsrc);
