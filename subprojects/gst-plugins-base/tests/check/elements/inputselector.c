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
#include "config.h"
#endif

#ifdef HAVE_VALGRIND
# include <valgrind/valgrind.h>
#endif

#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/app/app.h>

#define BUFFER_INTERVAL 20000   /* useconds */
#define SWITCH_INTERVAL 15000   /* useconds */
#define MIN_COUNT 400

#define RELEASE_TEST_BRANCHES 2
#define RELEASE_TEST_SWITCH_INTERVAL 25000      /* useconds */
#define RELINK_INTERVAL 30000   /* useconds */

static void
push_buffers (GstAppSrc * src)
{
  GstCaps *caps;
  GstStructure *s;
  gint id;
  GstBuffer *buffer;
  char *data;
  GstClockTime pts = 0;
  GstStateChangeReturn state_change_ret;
  GstState state;

  g_assert_nonnull (src);
  gst_object_ref (src);

  caps = gst_app_src_get_caps (src);
  fail_unless (caps != NULL);
  s = gst_caps_get_structure (caps, 0);
  g_assert_nonnull (s);
  fail_unless (gst_structure_get_int (s, "id", &id));
  fail_unless (id < 256);

  gst_caps_unref (caps);

  while (TRUE) {
    g_usleep (BUFFER_INTERVAL);

    state_change_ret =
        gst_element_get_state (GST_ELEMENT (src), &state, NULL,
        GST_CLOCK_TIME_NONE);
    if (state_change_ret != GST_STATE_CHANGE_SUCCESS
        || state < GST_STATE_PAUSED) {
      GST_DEBUG_OBJECT (src, "Exiting push_buffer loop");
      break;
    }

    data = g_new (char, 1);
    *data = (char) id;
    buffer = gst_buffer_new_wrapped (data, 1);
    GST_BUFFER_PTS (buffer) = pts;

    if (gst_app_src_push_buffer (src, buffer) == GST_FLOW_OK)
      GST_DEBUG_OBJECT (src, "Pushed buffer to src %d", id);

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
  gulong delay = SWITCH_INTERVAL;

#ifdef HAVE_VALGRIND
  if (RUNNING_ON_VALGRIND) {
    delay *= 20;
  }
#endif

  gst_object_ref (selector);

  while (TRUE) {
    g_usleep (delay);
    state_change_ret =
        gst_element_get_state (GST_ELEMENT (selector), &state, NULL,
        GST_CLOCK_TIME_NONE);
    if (state_change_ret != GST_STATE_CHANGE_SUCCESS
        || state < GST_STATE_PAUSED) {
      GST_DEBUG_OBJECT (selector, "Exiting switch_sinkpads loop");
      break;
    }

    active_pad_id = (active_pad_id + 1) % 2;
    g_snprintf (active_pad_name, sizeof (active_pad_name), "sink_%d",
        active_pad_id);
    GST_DEBUG_OBJECT (selector, "switching to pad %s", active_pad_name);
    pad = gst_element_get_static_pad (selector, active_pad_name);
    g_assert_nonnull (pad);

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
  gst_object_unref (bus);

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
      GST_DEBUG ("eos");
      break;
    }

    caps = gst_sample_get_caps (sample);
    fail_unless (caps != NULL);
    s = gst_caps_get_structure (caps, 0);
    fail_unless (s != NULL);
    fail_unless (gst_structure_get_int (s, "id", &id));
    fail_unless (id < 256);

    buffer = gst_sample_get_buffer (sample);
    fail_unless (buffer != NULL);
    fail_unless (gst_buffer_map (buffer, &minfo, GST_MAP_READ));
    fail_unless_equals_int (minfo.size, 1);
    fail_unless_equals_int (minfo.data[0], (char) id);
    gst_buffer_unmap (buffer, &minfo);

    gst_sample_unref (sample);

    count[id] += 1;
    GST_DEBUG ("Pulled buffer from src %d, count: %d", id, count[id]);
    if (count[0] > MIN_COUNT && count[1] > MIN_COUNT) {
      GST_DEBUG ("Reached min count, sending eos...");
      fail_unless (gst_element_send_event (pipeline, gst_event_new_eos ()));
    }
  }

  fail_unless_equals_int (gst_element_set_state (pipeline,
          GST_STATE_NULL), GST_STATE_CHANGE_SUCCESS);

  g_thread_join (switch_thrd);
  g_thread_join (src_0_thrd);
  g_thread_join (src_1_thrd);

  gst_object_unref (pipeline);
}

GST_END_TEST;

typedef struct
{
  GstElement *tee;
  GstPad *selpad;
} ReleaseTestBranch;

typedef struct
{
  GstElement *selector;
  GMutex mutex;
  ReleaseTestBranch branches[RELEASE_TEST_BRANCHES];
} ReleaseTestCtx;

typedef struct
{
  ReleaseTestCtx *ctx;
  guint branch_idx;
} UnlinkReleaseData;

static GstPadProbeReturn
pad_probe_unlink_release (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  UnlinkReleaseData *data = user_data;
  GstElement *selector, *peer_parent;
  GstPad *peer;

  gst_pad_remove_probe (pad, info->id);

  peer = gst_pad_get_peer (pad);
  g_print ("Unlinking %d\n", data->branch_idx);
  fail_unless (gst_pad_unlink (peer, pad));
  g_mutex_lock (&data->ctx->mutex);
  gst_object_unref (data->ctx->branches[data->branch_idx].selpad);
  data->ctx->branches[data->branch_idx].selpad = NULL;
  g_mutex_unlock (&data->ctx->mutex);

  selector = GST_ELEMENT (gst_pad_get_parent (pad));
  fail_unless (selector != NULL);
  g_print ("Releasing %d\n", data->branch_idx);
  gst_element_release_request_pad (selector, pad);
  gst_object_unref (selector);

  peer_parent = GST_ELEMENT (gst_pad_get_parent (peer));
  fail_unless (peer_parent != NULL);
  gst_element_release_request_pad (peer_parent, peer);
  gst_object_unref (peer);
  gst_object_unref (peer_parent);

  return GST_PAD_PROBE_DROP;
}

static void
release_test_release_link_loop (ReleaseTestCtx * ctx)
{
  GstStateChangeReturn state_change_ret;
  GstState state;
  guint idx;
  gulong delay = RELINK_INTERVAL;

#ifdef HAVE_VALGRIND
  if (RUNNING_ON_VALGRIND) {
    delay *= 20;
  }
#endif

  while (TRUE) {
    g_usleep (delay);

    state_change_ret =
        gst_element_get_state (GST_ELEMENT (ctx->selector), &state, NULL,
        GST_CLOCK_TIME_NONE);
    if (state_change_ret != GST_STATE_CHANGE_SUCCESS
        || state < GST_STATE_PAUSED) {
      g_print ("Exiting switch_release_link loop\n");
      break;
    }

    g_mutex_lock (&ctx->mutex);

    for (idx = 0; idx < RELEASE_TEST_BRANCHES; ++idx) {
      ReleaseTestBranch *branch = &ctx->branches[idx];

      if (branch->selpad) {
        UnlinkReleaseData *probe_data;

        if (g_random_double_range (0.0, 1.0) < 0.3) {
          continue;
        }

        /* Block pad to unlink and release it */
        probe_data = g_malloc (sizeof (UnlinkReleaseData));
        probe_data->ctx = ctx;
        probe_data->branch_idx = idx;
        gst_pad_add_probe (branch->selpad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM,
            pad_probe_unlink_release, probe_data, g_free);
      } else {
        GstPad *selpad, *peer;

        peer = gst_element_request_pad_simple (branch->tee, "src_%u");
        fail_unless (peer != NULL);

        selpad = gst_element_request_pad_simple (ctx->selector, "sink_%u");
        fail_unless (selpad != NULL);

        g_print ("Linking %d\n", idx);
        fail_unless (gst_pad_link (peer, selpad) == GST_PAD_LINK_OK);
        gst_object_unref (peer);

        branch->selpad = selpad;
      }
    }

    g_mutex_unlock (&ctx->mutex);
  }
}

static void
release_test_switch_sinkpads_loop (ReleaseTestCtx * ctx)
{
  GstStateChangeReturn state_change_ret;
  GstState state;
  GstPad *active_pad;
  guint idx;
  gulong delay = RELEASE_TEST_SWITCH_INTERVAL;

#ifdef HAVE_VALGRIND
  if (RUNNING_ON_VALGRIND) {
    delay *= 20;
  }
#endif

  while (TRUE) {
    g_usleep (delay);

    state_change_ret =
        gst_element_get_state (GST_ELEMENT (ctx->selector), &state, NULL,
        GST_CLOCK_TIME_NONE);
    if (state_change_ret != GST_STATE_CHANGE_SUCCESS
        || state < GST_STATE_PAUSED) {
      g_print ("Exiting switch_sinkpads loop\n");
      break;
    }

    g_object_get (ctx->selector, "active-pad", &active_pad, NULL);
    if (active_pad) {
      GstPad *other_pad = NULL;

      g_mutex_lock (&ctx->mutex);

      for (idx = 0; idx < RELEASE_TEST_BRANCHES; ++idx) {
        GstPad *pad = ctx->branches[idx].selpad;

        if (pad != active_pad) {
          other_pad = pad;
          break;
        }
      }

      if (other_pad) {
        g_print ("Switching to %d\n", idx);
        g_object_set (ctx->selector, "active-pad", other_pad, NULL);
      }

      g_mutex_unlock (&ctx->mutex);

      gst_object_unref (active_pad);
    }
  }
}

GST_START_TEST (pad_release_stress_test)
{
  GstElement *pipeline, *src_0, *tee_0, *fakesink_0, *src_1, *tee_1,
      *fakesink_1, *selector, *sink;
  GstCaps *caps;
  GstBus *bus;
  GThread *push_0_thrd, *push_1_thrd, *release_link_thrd, *switch_thrd;
  ReleaseTestCtx ctx;
  GError *error = NULL;
  GstSample *sample;
  guint count;

  pipeline = gst_pipeline_new (NULL);

  src_0 = gst_element_factory_make ("appsrc", "src-0");
  fail_unless (src_0 != NULL);
  caps = gst_caps_from_string ("application/input-selector-test,id=0");
  g_object_set (src_0, "format", GST_FORMAT_TIME, "caps", caps, NULL);
  gst_caps_unref (caps);

  tee_0 = gst_element_factory_make ("tee", "tee-0");
  fail_unless (tee_0 != NULL);

  fakesink_0 = gst_element_factory_make ("fakesink", "fakesink-0");
  fail_unless (fakesink_0 != NULL);
  g_object_set (fakesink_0, "async", FALSE, NULL);

  src_1 = gst_element_factory_make ("appsrc", "src-1");
  fail_unless (src_1 != NULL);
  caps = gst_caps_from_string ("application/input-selector-test,id=1");
  g_object_set (src_1, "format", GST_FORMAT_TIME, "caps", caps, NULL);
  gst_caps_unref (caps);

  tee_1 = gst_element_factory_make ("tee", "tee-1");
  fail_unless (tee_1 != NULL);

  fakesink_1 = gst_element_factory_make ("fakesink", "fakesink-1");
  fail_unless (fakesink_1 != NULL);
  g_object_set (fakesink_1, "async", FALSE, NULL);

  selector = gst_element_factory_make ("input-selector", NULL);
  fail_unless (selector != NULL);
  g_object_set (selector, "sync-mode", 1, "drop-backwards", TRUE, NULL);

  sink = gst_element_factory_make ("appsink", NULL);
  fail_unless (sink != NULL);
  g_object_set (sink, "sync", FALSE, "async", FALSE, NULL);

  gst_bin_add_many (GST_BIN (pipeline), src_0, tee_0, fakesink_0, src_1, tee_1,
      fakesink_1, selector, sink, NULL);
  fail_unless (gst_element_link_many (src_0, tee_0, fakesink_0, NULL));
  fail_unless (gst_element_link_many (src_1, tee_1, fakesink_1, NULL));
  fail_unless (gst_element_link (selector, sink));

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  fail_unless (bus != NULL);

  g_signal_connect (bus, "message", (GCallback) message_cb, NULL);
  gst_object_unref (bus);

  fail_if (gst_element_set_state (pipeline,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);

  push_0_thrd =
      g_thread_try_new ("push_0", (GThreadFunc) push_buffers,
      (gpointer) GST_APP_SRC (src_0), &error);
  fail_unless (error == NULL);

  push_1_thrd =
      g_thread_try_new ("push_1", (GThreadFunc) push_buffers,
      (gpointer) GST_APP_SRC (src_1), &error);
  fail_unless (error == NULL);

  ctx.selector = gst_object_ref (selector);
  ctx.branches[0].tee = gst_object_ref (tee_0);
  ctx.branches[0].selpad = NULL;
  ctx.branches[1].tee = gst_object_ref (tee_1);
  ctx.branches[1].selpad = NULL;
  g_mutex_init (&ctx.mutex);

  release_link_thrd =
      g_thread_try_new ("release_link",
      (GThreadFunc) release_test_release_link_loop, (gpointer) & ctx, &error);
  fail_unless (error == NULL);

  switch_thrd =
      g_thread_try_new ("switch",
      (GThreadFunc) release_test_switch_sinkpads_loop, (gpointer) & ctx,
      &error);
  fail_unless (error == NULL);

  sample = gst_app_sink_pull_preroll (GST_APP_SINK (sink));
  fail_unless (sample != NULL);
  gst_sample_unref (sample);

  for (count = 0; count < 100; ++count) {
    sample = gst_app_sink_pull_sample (GST_APP_SINK (sink));
    fail_unless (sample != NULL);
    gst_sample_unref (sample);
    g_print ("count: %d\n", count);
  }

  fail_unless_equals_int (gst_element_set_state (pipeline,
          GST_STATE_NULL), GST_STATE_CHANGE_SUCCESS);

  g_thread_join (release_link_thrd);
  g_thread_join (switch_thrd);
  g_thread_join (push_0_thrd);
  g_thread_join (push_1_thrd);

  gst_object_unref (ctx.selector);
  gst_object_unref (ctx.branches[0].tee);
  gst_clear_object (&ctx.branches[0].selpad);
  gst_object_unref (ctx.branches[1].tee);
  gst_clear_object (&ctx.branches[1].selpad);
  g_mutex_clear (&ctx.mutex);
  gst_object_unref (pipeline);
}

GST_END_TEST;

typedef struct
{
  gboolean eos_received;
  GMutex lock;
  GCond cond;
} EosReceivedCtx;

static GstPadProbeReturn
eos_received_probe (GstPad * pad, GstPadProbeInfo * info, gpointer udata)
{
  EosReceivedCtx *ctx = udata;

  if (GST_EVENT_TYPE (info->data) == GST_EVENT_EOS) {
    g_mutex_lock (&ctx->lock);
    ctx->eos_received = TRUE;
    g_cond_broadcast (&ctx->cond);
    g_mutex_unlock (&ctx->lock);
  }
  return GST_PAD_PROBE_OK;
}

GST_START_TEST (eos_on_remaining_inactive_pad)
{
  GstElement *pipeline, *remaining_src, *active_src, *selector, *sink;
  GstBus *bus;
  GstPad *remaining_srcpad, *remaining_sinkpad;
  GstPad *active_srcpad, *active_sinkpad;
  GstPad *sinkpad;
  GstBuffer *buf;
  EosReceivedCtx eos_received_ctx = { 0 };

  pipeline = gst_pipeline_new (NULL);

  remaining_src = gst_element_factory_make ("appsrc", "remaining-src");
  fail_unless (remaining_src != NULL);

  active_src = gst_element_factory_make ("appsrc", "active-src");
  fail_unless (active_src != NULL);

  selector = gst_element_factory_make ("input-selector", NULL);
  fail_unless (selector != NULL);
  g_object_set (selector, "sync-mode", 1, "drop-backwards", TRUE, NULL);

  sink = gst_element_factory_make ("fakesink", NULL);
  fail_unless (sink != NULL);
  g_object_set (sink, "sync", FALSE, "async", FALSE, NULL);

  gst_bin_add_many (GST_BIN (pipeline), remaining_src, active_src, selector,
      sink, NULL);

  remaining_srcpad = gst_element_get_static_pad (remaining_src, "src");
  remaining_sinkpad = gst_element_request_pad_simple (selector, "sink_%u");
  fail_unless (gst_pad_link (remaining_srcpad, remaining_sinkpad) ==
      GST_PAD_LINK_OK);

  active_srcpad = gst_element_get_static_pad (active_src, "src");
  active_sinkpad = gst_element_request_pad_simple (selector, "sink_%u");
  fail_unless (gst_pad_link (active_srcpad, active_sinkpad) == GST_PAD_LINK_OK);

  fail_unless (gst_element_link (selector, sink));

  g_object_set (selector, "active-pad", active_sinkpad, NULL);

  sinkpad = gst_element_get_static_pad (sink, "sink");

  g_mutex_init (&eos_received_ctx.lock);
  g_cond_init (&eos_received_ctx.cond);
  gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
      eos_received_probe, &eos_received_ctx, NULL);
  gst_object_unref (sinkpad);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  fail_unless (bus != NULL);
  g_signal_connect (bus, "message", (GCallback) message_cb, NULL);
  gst_object_unref (bus);

  fail_if (gst_element_set_state (pipeline,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);

  buf = gst_buffer_new_wrapped (g_memdup2 ("a", 1), 1);
  GST_BUFFER_PTS (buf) = 0;
  fail_unless (gst_app_src_push_buffer (GST_APP_SRC (active_src),
          buf) == GST_FLOW_OK);

  buf = gst_buffer_new_wrapped (g_memdup2 ("b", 1), 1);
  GST_BUFFER_PTS (buf) = 0;
  fail_unless (gst_app_src_push_buffer (GST_APP_SRC (remaining_src),
          buf) == GST_FLOW_OK);

  g_usleep (BUFFER_INTERVAL);

  /* it's ok not to block the pad here because we won't send other buffers / events */
  gst_pad_unlink (active_srcpad, active_sinkpad);
  gst_element_release_request_pad (selector, active_sinkpad);

  gst_app_src_end_of_stream (GST_APP_SRC (remaining_src));

  g_mutex_lock (&eos_received_ctx.lock);
  while (!eos_received_ctx.eos_received)
    g_cond_wait (&eos_received_ctx.cond, &eos_received_ctx.lock);

  g_mutex_unlock (&eos_received_ctx.lock);

  fail_unless_equals_int (gst_element_set_state (pipeline,
          GST_STATE_NULL), GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (remaining_srcpad);
  gst_object_unref (remaining_sinkpad);
  gst_object_unref (active_srcpad);
  gst_object_unref (active_sinkpad);
  gst_object_unref (pipeline);

  g_mutex_clear (&eos_received_ctx.lock);
  g_cond_clear (&eos_received_ctx.cond);
}

GST_END_TEST;

static Suite *
inputselector_suite (void)
{
  Suite *s = suite_create ("inputselector");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);

  tcase_add_test (tc, stress_test);
  tcase_add_test (tc, pad_release_stress_test);
  tcase_add_test (tc, eos_on_remaining_inactive_pad);

  return s;
}

GST_CHECK_MAIN (inputselector);
