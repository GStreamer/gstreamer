/* GStreamer
 *
 * Copyright (C) 2025 François Laignel <francois@centricular.com>
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

/* This is a stress test which aims at detecting race conditions
 * switching active pads rapidly. These race conditions can happen
 * under specific timing conditions which can't easily be produced
 * except by simulating production-like stress conditions repeatedly.
 * That's the reason why this test case uses usleeps.
 */

#ifdef HAVE_CONFIG_H
#endif
#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/app/app.h>

#define BUFFER_INTERVAL 20000   /* useconds */
#define SWITCH_INTERVAL 15000   /* useconds */
#define MIN_COUNT 400

static void
push_buffers (GstAppSrc * src)
{
  GstCaps *caps;
  GstStructure *s;
  gint id;
  GstBuffer *buffer;
  char *data;
  GstClockTime pts = 0;

  g_assert (src != NULL);

  caps = gst_app_src_get_caps (src);
  fail_unless (caps != NULL);
  s = gst_caps_get_structure (caps, 0);
  g_assert (s != NULL);
  g_assert (gst_structure_get_int (s, "id", &id));
  g_assert (id < 256);

  gst_object_ref (src);

  while (TRUE) {
    g_usleep (BUFFER_INTERVAL);

    data = g_new (char, 1);
    *data = (char) id;
    buffer = gst_buffer_new_wrapped (data, 1);
    GST_BUFFER_PTS (buffer) = pts;

    if (gst_app_src_push_buffer (src, buffer) != GST_FLOW_OK)
      break;

    g_print ("Pushed buffer to src %d\n", id);

    pts += (GstClockTime) BUFFER_INTERVAL;
  }

  gst_object_unref (src);
}

static void
switch_sinkpads (GstElement * selector)
{
  GstStateChangeReturn state_change_ret;
  GstState state;
  gchar active_pad_id = 0;
  gchar active_pad_name[] = "sink_0";
  GstPad *pad;

  gst_object_ref (selector);

  while (TRUE) {
    g_usleep (SWITCH_INTERVAL);
    state_change_ret =
        gst_element_get_state (GST_ELEMENT (selector), &state, NULL,
        GST_CLOCK_TIME_NONE);
    if (state_change_ret != GST_STATE_CHANGE_SUCCESS
        || state < GST_STATE_PAUSED) {
      g_print ("Exiting switch_sinkpads loop");
      break;
    }

    active_pad_id = (active_pad_id + 1) % 2;
    g_snprintf (active_pad_name, sizeof (active_pad_name), "sink_%d",
        active_pad_id);
    g_print ("switching to pad %s\n", active_pad_name);
    pad = gst_element_get_static_pad (selector, active_pad_name);
    g_assert (pad != NULL);

    g_object_set (selector, "active-pad", pad, NULL);

    gst_object_unref (pad);
  }

  gst_object_unref (selector);
}

static void
message_cb (GstBus * bus, GstMessage * message, gpointer udata)
{
  if (message == NULL)
    return;

  fail_if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ERROR);
  gst_message_unref (message);
}

GST_START_TEST (stress_test)
{
  GstElement *pipeline, *src_0, *src_1, *selector, *sink;
  GstCaps *caps;
  GstBus *bus;
  GThread *src_0_thrd, *src_1_thrd, *switch_thrd;
  GError *error = NULL;
  GstSample *sample;
  guint count[2] = { 0, 0 };

  pipeline = gst_pipeline_new (NULL);

  src_0 = gst_element_factory_make ("appsrc", "src-0");
  fail_unless (src_0 != NULL);
  caps = gst_caps_from_string ("application/input-selector-test,id=0");
  g_object_set (src_0, "format", GST_FORMAT_TIME, "caps", caps, NULL);
  gst_caps_unref (caps);

  src_1 = gst_element_factory_make ("appsrc", "src-1");
  fail_unless (src_1 != NULL);
  caps = gst_caps_from_string ("application/input-selector-test,id=1");
  g_object_set (src_1, "format", GST_FORMAT_TIME, "caps", caps, NULL);
  gst_caps_unref (caps);

  selector = gst_element_factory_make ("input-selector", NULL);
  fail_unless (selector != NULL);
  g_object_set (selector, "sync-mode", 1, "drop-backwards", TRUE, NULL);

  sink = gst_element_factory_make ("appsink", NULL);
  fail_unless (sink != NULL);
  g_object_set (sink, "sync", FALSE, "wait-on-eos", TRUE, NULL);

  gst_bin_add_many (GST_BIN (pipeline), src_0, src_1, selector, sink, NULL);
  fail_unless (gst_element_link_many (src_0, selector, sink, NULL));
  fail_unless (gst_element_link (src_1, selector));

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  fail_unless (bus != NULL);

  g_signal_connect (bus, "message", (GCallback) message_cb, NULL);

  fail_if (gst_element_set_state (pipeline,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);

  src_0_thrd =
      g_thread_try_new ("src_0", (GThreadFunc) push_buffers,
      (gpointer) GST_APP_SRC (src_0), &error);
  fail_unless (error == NULL);

  src_1_thrd =
      g_thread_try_new ("src_1", (GThreadFunc) push_buffers,
      (gpointer) GST_APP_SRC (src_1), &error);
  fail_unless (error == NULL);

  switch_thrd =
      g_thread_try_new ("switch", (GThreadFunc) switch_sinkpads,
      (gpointer) selector, &error);
  fail_unless (error == NULL);

  sample = gst_app_sink_pull_preroll (GST_APP_SINK (sink));
  fail_unless (sample != NULL);
  gst_sample_unref (sample);

  while (TRUE) {
    GstCaps *caps;
    GstStructure *s;
    gint id;
    GstBuffer *buffer;
    GstMapInfo minfo;

    sample = gst_app_sink_pull_sample (GST_APP_SINK (sink));
    if (sample == NULL) {
      g_print ("eos\n");
      break;
    }

    caps = gst_sample_get_caps (sample);
    fail_unless (caps != NULL);
    s = gst_caps_get_structure (caps, 0);
    fail_unless (s != NULL);
    fail_unless (gst_structure_get_int (s, "id", &id));
    g_assert (id < 256);

    buffer = gst_sample_get_buffer (sample);
    fail_unless (buffer != NULL);
    fail_unless (gst_buffer_map (buffer, &minfo, GST_MAP_READ));
    fail_unless_equals_int (minfo.size, 1);
    fail_unless_equals_int (minfo.data[0], (char) id);
    gst_buffer_unmap (buffer, &minfo);

    gst_sample_unref (sample);

    count[id] += 1;
    g_print ("Pulled buffer from src %d, count: %d\n", id, count[id]);
    if (count[0] > MIN_COUNT && count[1] > MIN_COUNT) {
      g_print ("Reached min count, sending eos...\n");
      fail_unless (gst_element_send_event (pipeline, gst_event_new_eos ()));
    }
  }

  fail_unless_equals_int (gst_element_set_state (pipeline,
          GST_STATE_NULL), GST_STATE_CHANGE_SUCCESS);

  g_thread_join (switch_thrd);
  g_thread_join (src_0_thrd);
  g_thread_join (src_1_thrd);
}

GST_END_TEST;

static Suite *
inputselector_suite (void)
{
  Suite *s = suite_create ("inputselector");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);

  tcase_add_test (tc, stress_test);

  return s;
}

GST_CHECK_MAIN (inputselector);
