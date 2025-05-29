/* GStreamer
 *
 * Copyright (C) 2020 Matthew Waters <matthew@centricular.com>
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
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/gl/gl.h>
#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>

static void
replace_display (GstHarness * h)
{
  GstContext *new_context;
  GstGLDisplay *new_display;
  GstGLContext *expected, *gl_context;
  GstBuffer *buf;

  /* replaces the GstGLDisplay used by @h with verification */

  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push_from_src (h));
  /* need a second buffer to pull one, videoaggregator has one frame latency */
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push_from_src (h));
  buf = gst_harness_pull (h);
  fail_unless (buf != NULL);
  gst_clear_buffer (&buf);

  g_object_get (G_OBJECT (h->element), "context", &gl_context, NULL);
  fail_unless (gl_context != NULL);
  gst_clear_object (&gl_context);

  new_display = gst_gl_display_new ();
  fail_unless (gst_gl_display_create_context (new_display, NULL, &expected,
          NULL));
  fail_unless (expected != NULL);
  fail_unless (gst_gl_display_add_context (new_display, expected));

  new_context = gst_context_new (GST_GL_DISPLAY_CONTEXT_TYPE, TRUE);
  gst_context_set_gl_display (new_context, new_display);

  gst_element_set_context (h->element, new_context);
  gst_context_unref (new_context);
  new_context = NULL;

  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push_from_src (h));
  buf = gst_harness_pull (h);
  fail_unless (buf != NULL);
  gst_clear_buffer (&buf);

  g_object_get (G_OBJECT (h->element), "context", &gl_context, NULL);
  fail_unless (gl_context != NULL);

  fail_unless (gl_context == expected);
  fail_unless (new_display == gl_context->display);

  gst_object_unref (expected);
  gst_object_unref (gl_context);
  gst_object_unref (new_display);
}

GST_START_TEST (test_glvideomixer_negotiate)
{
  GstHarness *mix;
  GstBuffer *buf;

  mix = gst_harness_new_with_padnames ("glvideomixer", "sink_0", "src");
  gst_harness_set_live (mix, FALSE);
  gst_harness_set_blocking_push_mode (mix);
  gst_harness_set_caps_str (mix,
      "video/x-raw(memory:GLMemory),format=RGBA,width=1,height=1,framerate=25/1,texture-target=2D",
      "video/x-raw(memory:GLMemory),format=RGBA,width=1,height=1,framerate=25/1,texture-target=2D");
  gst_harness_add_src (mix, "gltestsrc", FALSE);
  gst_harness_set_live (mix->src_harness, FALSE);
  gst_harness_set_blocking_push_mode (mix->src_harness);

  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push_from_src (mix));
  fail_unless (gst_harness_push_event (mix, gst_event_new_eos ()));

  buf = gst_harness_pull (mix);
  fail_unless (buf != NULL);
  gst_clear_buffer (&buf);

  gst_harness_teardown (mix);
}

GST_END_TEST;

GST_START_TEST (test_glvideomixer_display_replace)
{
  GstHarness *mix;

  mix = gst_harness_new_with_padnames ("glvideomixer", "sink_0", "src");
  gst_harness_set_live (mix, FALSE);
  gst_harness_set_blocking_push_mode (mix);
  gst_harness_set_caps_str (mix,
      "video/x-raw(memory:GLMemory),format=RGBA,width=1,height=1,framerate=25/1,texture-target=2D",
      "video/x-raw(memory:GLMemory),format=RGBA,width=1,height=1,framerate=25/1,texture-target=2D");
  gst_harness_add_src (mix, "gltestsrc", FALSE);
  gst_harness_set_live (mix->src_harness, FALSE);
  gst_harness_set_blocking_push_mode (mix->src_harness);

  replace_display (mix);

  gst_harness_teardown (mix);
}

GST_END_TEST;

typedef struct _ProbeEvent
{
  gboolean received;
  gdouble x_pos, y_pos;
} ProbeEvent;

static GstPadProbeReturn
probe_nav_event (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GstEvent *event = gst_pad_probe_info_get_event (info);
  ProbeEvent *probe_ev = (ProbeEvent *) user_data;

  if (GST_EVENT_TYPE (event) == GST_EVENT_NAVIGATION) {
    probe_ev->received = TRUE;
    gst_navigation_event_parse_mouse_move_event (event,
        &(probe_ev->x_pos), &(probe_ev->y_pos));
  }

  return GST_PAD_PROBE_OK;
}

GST_START_TEST (test_glvideomixer_navigation_events)
{
  GstElement *bin, *src1, *src2, *src3, *filter1, *filter2, *filter3;
  GstElement *glvideomixer, *sink;
  GstPad *srcpad, *sinkpad;
  GstCaps *caps;
  gboolean res;
  ProbeEvent probe_events[3];
  GstStateChangeReturn state_res;
  GstEvent *event;

  GST_INFO ("preparing test");

  /* build pipeline */
  bin = gst_pipeline_new ("pipeline");
  src1 = gst_element_factory_make ("videotestsrc", "src1");
  src2 = gst_element_factory_make ("videotestsrc", "src2");
  src3 = gst_element_factory_make ("videotestsrc", "src3");
  filter1 = gst_element_factory_make ("capsfilter", "filter1");
  filter2 = gst_element_factory_make ("capsfilter", "filter2");
  filter3 = gst_element_factory_make ("capsfilter", "filter3");
  glvideomixer = gst_element_factory_make ("glvideomixer", "glvideomixer");
  sink = gst_element_factory_make ("fakesink", "sink");
  gst_bin_add_many (GST_BIN (bin), src1, src2, src3, filter1, filter2, filter3,
      glvideomixer, sink, NULL);

  /* configure capsfilters */
  caps = gst_caps_from_string ("video/x-raw,width=800,height=400");
  g_object_set (filter1, "caps", caps, NULL);
  gst_caps_unref (caps);
  caps = gst_caps_from_string ("video/x-raw,width=400,height=200");
  g_object_set (filter2, "caps", caps, NULL);
  gst_caps_unref (caps);
  caps = gst_caps_from_string ("video/x-raw,width=200,height=50");
  g_object_set (filter3, "caps", caps, NULL);
  gst_caps_unref (caps);

  res = gst_element_link_many (src1, filter1, glvideomixer, NULL);
  fail_unless (res == TRUE, NULL);
  res = gst_element_link_many (src2, filter2, glvideomixer, NULL);
  fail_unless (res == TRUE, NULL);
  res = gst_element_link_many (src3, filter3, glvideomixer, NULL);
  fail_unless (res == TRUE, NULL);
  res = gst_element_link (glvideomixer, sink);
  fail_unless (res == TRUE, NULL);

  srcpad = gst_element_get_static_pad (glvideomixer, "src");
  gst_object_unref (srcpad);

  /* configure pads and add probes */
  srcpad = gst_element_get_static_pad (filter1, "src");
  sinkpad = gst_pad_get_peer (srcpad);
  g_object_set (sinkpad,
      "width", 400, "height", 300, "xpos", 200, "ypos", 100, NULL);
  probe_events[0].received = FALSE;
  gst_pad_add_probe (srcpad, GST_PAD_PROBE_TYPE_EVENT_UPSTREAM,
      probe_nav_event, (gpointer) probe_events, NULL);
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  srcpad = gst_element_get_static_pad (filter2, "src");
  sinkpad = gst_pad_get_peer (srcpad);
  g_object_set (sinkpad,
      "width", 400, "height", 200, "xpos", 20, "ypos", 0, NULL);
  probe_events[1].received = FALSE;
  gst_pad_add_probe (srcpad, GST_PAD_PROBE_TYPE_EVENT_UPSTREAM,
      probe_nav_event, (gpointer) (probe_events + 1), NULL);
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  srcpad = gst_element_get_static_pad (filter3, "src");
  sinkpad = gst_pad_get_peer (srcpad);
  g_object_set (sinkpad,
      "width", 200, "height", 50, "xpos", 0, "ypos", 0, NULL);
  probe_events[2].received = FALSE;
  gst_pad_add_probe (srcpad, GST_PAD_PROBE_TYPE_EVENT_UPSTREAM,
      probe_nav_event, (gpointer) (probe_events + 2), NULL);
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  event =
      gst_event_new_navigation (gst_structure_new
      ("application/x-gst-navigation", "event", G_TYPE_STRING, "mouse-move",
          "button", G_TYPE_INT, 0, "pointer_x", G_TYPE_DOUBLE, 350.0,
          "pointer_y", G_TYPE_DOUBLE, 100.0, NULL));

  GST_INFO ("starting test");

  /* prepare playing */
  state_res = gst_element_set_state (bin, GST_STATE_PAUSED);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  /* wait for completion */
  state_res = gst_element_get_state (bin, NULL, NULL, GST_CLOCK_TIME_NONE);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  /* send event and validate */
  res = gst_element_send_event (sink, event);
  fail_unless (res == FALSE, NULL);

  /* check received events */
  ck_assert_msg (probe_events[0].received);
  ck_assert_msg (probe_events[1].received);
  ck_assert_msg (!probe_events[2].received);

  ck_assert_int_eq ((gint) probe_events[0].x_pos, 300);
  ck_assert_int_eq ((gint) probe_events[0].y_pos, 0);
  ck_assert_int_eq ((gint) probe_events[1].x_pos, 330);
  ck_assert_int_eq ((gint) probe_events[1].y_pos, 100);

  state_res = gst_element_set_state (bin, GST_STATE_NULL);
  ck_assert_int_ne (state_res, GST_STATE_CHANGE_FAILURE);

  gst_object_unref (bin);
}

GST_END_TEST;

static Suite *
glmixer_suite (void)
{
  Suite *s = suite_create ("glmixer");
  TCase *tc = tcase_create ("general");

  tcase_add_test (tc, test_glvideomixer_negotiate);
  tcase_add_test (tc, test_glvideomixer_display_replace);
  tcase_add_test (tc, test_glvideomixer_navigation_events);
  suite_add_tcase (s, tc);

  return s;
}

int
main (int argc, char **argv)
{
  Suite *s;
  g_setenv ("GST_GL_XINITTHREADS", "1", TRUE);
  g_setenv ("GST_XINITTHREADS", "1", TRUE);
  gst_check_init (&argc, &argv);
  s = glmixer_suite ();
  return gst_check_run_suite (s, "glmixer", __FILE__);
}
