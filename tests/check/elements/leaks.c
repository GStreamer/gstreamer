/* GStreamer
 *
 * Unit test for leakstracer
 *
 * Copyright (C) <2019> Nirbheek Chauhan <nirbheek@centricular.com>
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
#include <gst/check/gstcheck.h>

#define PROBE_TYPE GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BLOCK

#define NUM_BUFFERS 2

struct RetBufferCtx
{
  GstBuffer *bufs[NUM_BUFFERS];
  guint idx;
};

static void
ret_buffer_ctx_free (struct RetBufferCtx *ctx, gboolean free_bufs)
{
  guint ii;
  if (free_bufs)
    for (ii = 0; ii < ctx->idx; ii++)
      gst_buffer_unref (ctx->bufs[ii]);
  g_free (ctx);
}

static GstPadProbeReturn
ref_buffer (GstPad * srcpad, GstPadProbeInfo * info, gpointer user_data)
{
  GstBuffer *buffer;
  struct RetBufferCtx *ctx = user_data;

  if (!(GST_PAD_PROBE_INFO_TYPE (info) & GST_PAD_PROBE_TYPE_BUFFER))
    return GST_PAD_PROBE_PASS;

  buffer = GST_PAD_PROBE_INFO_BUFFER (info);
  /* ref buffer so it leaks */
  gst_buffer_ref (buffer);

  if (ctx) {
    /* we can only store NUM_BUFFERS buffers */
    fail_unless (ctx->idx < NUM_BUFFERS);
    /* return the buffer so it can be freed later to avoid triggering valgrind
     * in gst-validate */
    ctx->bufs[ctx->idx] = buffer;
    ctx->idx++;
  }

  return GST_PAD_PROBE_PASS;
}

static GstTracer *
get_tracer_by_name (const gchar * name)
{
  GList *tracers, *l;
  GstTracer *tracer = NULL;

  tracers = gst_tracing_get_active_tracers ();
  for (l = tracers; l; l = l->next)
    if (g_strcmp0 (GST_OBJECT_NAME (l->data), name) == 0)
      tracer = l->data;

  g_list_free (tracers);
  return tracer;
}

/* Test logging of live objects to debug logs */
GST_START_TEST (test_log_live_objects)
{
  GstElement *pipe, *src, *sink;
  GstPad *srcpad;
  GstMessage *m;
  struct RetBufferCtx *ctx = g_new0 (struct RetBufferCtx, 1);

  pipe = gst_pipeline_new ("pipeline");
  fail_unless (pipe);
  src = gst_element_factory_make ("fakesrc", NULL);
  fail_unless (src);
  g_object_set (src, "num-buffers", NUM_BUFFERS, NULL);

  sink = gst_element_factory_make ("fakesink", NULL);
  fail_unless (sink);

  gst_bin_add_many (GST_BIN (pipe), src, sink, NULL);
  fail_unless (gst_element_link (src, sink));

  srcpad = gst_element_get_static_pad (src, "src");
  gst_pad_add_probe (srcpad, PROBE_TYPE, ref_buffer, ctx, NULL);
  gst_object_unref (srcpad);

  GST_DEBUG ("Setting pipeline to PLAYING");
  fail_unless_equals_int (gst_element_set_state (pipe, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  m = gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (pipe), -1, GST_MESSAGE_EOS);
  gst_message_unref (m);

  fail_unless_equals_int (gst_element_set_state (pipe, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);
  gst_object_unref (pipe);

  /* Check the live-objects data returned by the tracer */
  {
    GstTracer *tracer = get_tracer_by_name ("plain");
    fail_unless (tracer);
    g_signal_emit_by_name (tracer, "log-live-objects");
    gst_object_unref (tracer);
  }

  ret_buffer_ctx_free (ctx, TRUE);
}

GST_END_TEST;

/* Test fetching of live objects with no detail */
GST_START_TEST (test_get_live_objects)
{
  GstElement *pipe, *src, *sink;
  GstPad *srcpad;
  GstMessage *m;
  struct RetBufferCtx *ctx = g_new0 (struct RetBufferCtx, 1);

  pipe = gst_pipeline_new ("pipeline");
  fail_unless (pipe);
  src = gst_element_factory_make ("fakesrc", NULL);
  fail_unless (src);
  g_object_set (src, "num-buffers", NUM_BUFFERS, NULL);

  sink = gst_element_factory_make ("fakesink", NULL);
  fail_unless (sink);

  gst_bin_add_many (GST_BIN (pipe), src, sink, NULL);
  fail_unless (gst_element_link (src, sink));

  srcpad = gst_element_get_static_pad (src, "src");
  gst_pad_add_probe (srcpad, PROBE_TYPE, ref_buffer, ctx, NULL);
  gst_object_unref (srcpad);

  GST_DEBUG ("Setting pipeline to PLAYING");
  fail_unless_equals_int (gst_element_set_state (pipe, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  m = gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (pipe), -1, GST_MESSAGE_EOS);
  gst_message_unref (m);

  fail_unless_equals_int (gst_element_set_state (pipe, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);
  gst_object_unref (pipe);

  /* Force all leftover tasks to stop */
  gst_task_cleanup_all ();

  /* Check the live-objects data returned by the tracer */
  {
    guint ii, size;
    GstStructure *info;
    const GValue *leaks;
    GstTracer *tracer = get_tracer_by_name ("plain");
    fail_unless (tracer);
    g_signal_emit_by_name (tracer, "get-live-objects", &info);
    fail_unless_equals_int (gst_structure_n_fields (info), 1);
    leaks = gst_structure_get_value (info, "live-objects-list");
    fail_unless (G_VALUE_HOLDS (leaks, GST_TYPE_LIST));
    size = gst_value_list_get_size (leaks);
    fail_unless_equals_int (size, NUM_BUFFERS);
    for (ii = 0; ii < size; ii++) {
      const GValue *v;
      const GstStructure *s;
      guint ref_count;

      v = gst_value_list_get_value (leaks, ii);
      fail_unless (G_VALUE_HOLDS (v, GST_TYPE_STRUCTURE));

      s = gst_value_get_structure (v);
      fail_unless (gst_structure_has_field_typed (s, "object",
              GST_TYPE_BUFFER));

      fail_unless (gst_structure_has_field_typed (s, "ref-count", G_TYPE_UINT));
      fail_unless (gst_structure_get_uint (s, "ref-count", &ref_count));
      fail_unless_equals_int (ref_count, 1);

      fail_unless (gst_structure_has_field_typed (s, "trace", G_TYPE_STRING));
      fail_unless_equals_string (gst_structure_get_string (s, "trace"), NULL);

      fail_unless (!gst_structure_has_field (s, "ref-infos"));
      fail_unless_equals_int (gst_structure_n_fields (s), 3);
    }
    gst_structure_free (info);
    gst_object_unref (tracer);
  }

  /* leaked buffers were freed above with @info */
  ret_buffer_ctx_free (ctx, FALSE);
}

GST_END_TEST;

/* Test fetching of filtered live objects with full detail */
GST_START_TEST (test_get_live_objects_filtered_detailed)
{
  GstElement *pipe, *src, *sink;
  GstPad *srcpad;
  GstMessage *m;
  struct RetBufferCtx *ctx = g_new0 (struct RetBufferCtx, 1);
  gboolean check_trace = FALSE;

#if defined (HAVE_BACKTRACE) || defined (HAVE_UNWIND) || defined (HAVE_DBGHELP)
  /* Otherwise, trace string (returned from gst_debug_get_stack_trace())
   * will be null */
  check_trace = TRUE;
#endif

  pipe = gst_pipeline_new ("pipeline");
  fail_unless (pipe);
  src = gst_element_factory_make ("fakesrc", NULL);
  fail_unless (src);
  g_object_set (src, "num-buffers", NUM_BUFFERS, NULL);

  sink = gst_element_factory_make ("fakesink", NULL);
  fail_unless (sink);

  gst_bin_add_many (GST_BIN (pipe), src, sink, NULL);
  fail_unless (gst_element_link (src, sink));

  srcpad = gst_element_get_static_pad (src, "src");
  gst_pad_add_probe (srcpad, PROBE_TYPE, ref_buffer, ctx, NULL);
  /* leak srcpad on purpose */
  gst_element_get_static_pad (sink, "sink");
  /* leak sinkpad on purpose */

  GST_DEBUG ("Setting pipeline to PLAYING");
  fail_unless_equals_int (gst_element_set_state (pipe, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  m = gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (pipe), -1, GST_MESSAGE_EOS);
  gst_message_unref (m);

  fail_unless_equals_int (gst_element_set_state (pipe, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);
  gst_object_unref (pipe);

  /* Force all leftover tasks to stop */
  gst_task_cleanup_all ();

  /* Check the live-objects data returned by the tracer */
  {
    guint ii, jj, isize, jsize;
    GstStructure *info;
    const GValue *leaks;
    GstTracer *tracer = get_tracer_by_name ("more");
    fail_unless (tracer);
    g_signal_emit_by_name (tracer, "get-live-objects", &info);
    fail_unless_equals_int (gst_structure_n_fields (info), 1);
    leaks = gst_structure_get_value (info, "live-objects-list");
    fail_unless (G_VALUE_HOLDS (leaks, GST_TYPE_LIST));
    isize = gst_value_list_get_size (leaks);
    fail_unless_equals_int (isize, NUM_BUFFERS);
    for (ii = 0; ii < isize; ii++) {
      const GValue *v;
      const GstStructure *s;
      guint ref_count;

      v = gst_value_list_get_value (leaks, ii);
      fail_unless (G_VALUE_HOLDS (v, GST_TYPE_STRUCTURE));

      s = gst_value_get_structure (v);
      fail_unless (gst_structure_has_field_typed (s, "object", GST_TYPE_PAD));

      fail_unless (gst_structure_has_field_typed (s, "ref-count", G_TYPE_UINT));
      fail_unless (gst_structure_get_uint (s, "ref-count", &ref_count));
      fail_unless_equals_int (ref_count, 1);

      fail_unless (gst_structure_has_field_typed (s, "trace", G_TYPE_STRING));
      if (check_trace)
        fail_unless (gst_structure_get_string (s, "trace"));

      fail_unless (gst_structure_has_field_typed (s, "ref-infos",
              GST_TYPE_LIST));
      fail_unless_equals_int (gst_structure_n_fields (s), 4);

      v = gst_structure_get_value (s, "ref-infos");
      jsize = gst_value_list_get_size (v);
      for (jj = 0; jj < jsize; jj++) {
        const GValue *rv;
        const GstStructure *r;

        rv = gst_value_list_get_value (v, jj);
        fail_unless (G_VALUE_HOLDS (rv, GST_TYPE_STRUCTURE));

        r = gst_value_get_structure (rv);
        fail_unless (gst_structure_has_field_typed (r, "ts",
                GST_TYPE_CLOCK_TIME));

        fail_unless (gst_structure_has_field_typed (r, "desc", G_TYPE_STRING));
        fail_unless (gst_structure_get_string (r, "desc"));

        fail_unless (gst_structure_get_uint (r, "ref-count", &ref_count));
        fail_unless (ref_count > 0);

        fail_unless (gst_structure_has_field_typed (r, "trace", G_TYPE_STRING));
        if (check_trace)
          fail_unless (gst_structure_get_string (r, "trace"));

        fail_unless_equals_int (gst_structure_n_fields (r), 4);
      }
    }
    gst_structure_free (info);
    gst_object_unref (tracer);
  }

  ret_buffer_ctx_free (ctx, TRUE);
  /* leaked pads were freed above with @info */
}

GST_END_TEST;

/* Just start and stop tracking without any checkpoints */
GST_START_TEST (test_activity_start_stop)
{
  GstElement *pipe, *src, *sink;
  GstMessage *m;
  GstTracer *tracer = get_tracer_by_name ("plain");

  fail_unless (tracer);

  g_signal_emit_by_name (tracer, "activity-start-tracking");

  pipe = gst_pipeline_new ("pipeline");
  fail_unless (pipe);
  src = gst_element_factory_make ("fakesrc", NULL);
  fail_unless (src);
  g_object_set (src, "num-buffers", NUM_BUFFERS, NULL);

  sink = gst_element_factory_make ("fakesink", NULL);
  fail_unless (sink);

  gst_bin_add_many (GST_BIN (pipe), src, sink, NULL);
  fail_unless (gst_element_link (src, sink));

  GST_DEBUG ("Setting pipeline to PLAYING");
  fail_unless_equals_int (gst_element_set_state (pipe, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  m = gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (pipe), -1, GST_MESSAGE_EOS);
  gst_message_unref (m);

  fail_unless_equals_int (gst_element_set_state (pipe, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);
  gst_object_unref (pipe);

  g_signal_emit_by_name (tracer, "activity-stop-tracking");
  gst_object_unref (tracer);
}

GST_END_TEST;

/* Track objects, and checkpoint twice */
GST_START_TEST (test_activity_log_checkpoint)
{
  GstElement *pipe, *src, *sink;
  GstMessage *m;
  GstTracer *tracer = get_tracer_by_name ("plain");

  fail_unless (tracer);

  g_signal_emit_by_name (tracer, "activity-start-tracking");

  pipe = gst_pipeline_new ("pipeline");
  fail_unless (pipe);
  src = gst_element_factory_make ("fakesrc", NULL);
  fail_unless (src);
  g_object_set (src, "num-buffers", NUM_BUFFERS, NULL);

  sink = gst_element_factory_make ("fakesink", NULL);
  fail_unless (sink);

  gst_bin_add_many (GST_BIN (pipe), src, sink, NULL);
  fail_unless (gst_element_link (src, sink));

  g_signal_emit_by_name (tracer, "activity-log-checkpoint");

  GST_DEBUG ("Setting pipeline to PLAYING");
  fail_unless_equals_int (gst_element_set_state (pipe, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  m = gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (pipe), -1, GST_MESSAGE_EOS);
  gst_message_unref (m);

  fail_unless_equals_int (gst_element_set_state (pipe, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);
  gst_object_unref (pipe);

  g_signal_emit_by_name (tracer, "activity-log-checkpoint");
  g_signal_emit_by_name (tracer, "activity-stop-tracking");
  gst_object_unref (tracer);
}

GST_END_TEST;

/* Track objects, checkpoint once, and assert the format of the data */
GST_START_TEST (test_activity_get_checkpoint)
{
  GstElement *pipe, *src, *sink;
  GstMessage *m;
  GstTracer *tracer = get_tracer_by_name ("more");

  fail_unless (tracer);

  g_signal_emit_by_name (tracer, "activity-start-tracking");

  pipe = gst_pipeline_new ("pipeline");
  fail_unless (pipe);
  src = gst_element_factory_make ("fakesrc", NULL);
  fail_unless (src);
  g_object_set (src, "num-buffers", NUM_BUFFERS, NULL);

  sink = gst_element_factory_make ("fakesink", NULL);
  fail_unless (sink);

  gst_bin_add_many (GST_BIN (pipe), src, sink, NULL);
  fail_unless (gst_element_link (src, sink));

  GST_DEBUG ("Setting pipeline to PLAYING");
  fail_unless_equals_int (gst_element_set_state (pipe, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  m = gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (pipe), -1, GST_MESSAGE_EOS);
  gst_message_unref (m);

  fail_unless_equals_int (gst_element_set_state (pipe, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);
  gst_object_unref (pipe);

  /* Force all leftover tasks to stop */
  gst_task_cleanup_all ();

  {
    GstStructure *cpoint;
    const GstStructure *cs, *rs;
    const GValue *created, *removed;

    g_signal_emit_by_name (tracer, "activity-get-checkpoint", &cpoint);
    fail_unless_equals_int (gst_structure_n_fields (cpoint), 2);

    created = gst_structure_get_value (cpoint, "objects-created-list");
    fail_unless (G_VALUE_HOLDS (created, GST_TYPE_LIST));
    created = gst_value_list_get_value (created, 0);
    fail_unless (G_VALUE_HOLDS (created, GST_TYPE_STRUCTURE));
    cs = gst_value_get_structure (created);
    fail_unless (gst_structure_has_field_typed (cs, "type-name",
            G_TYPE_STRING));
    fail_unless (gst_structure_get_string (cs, "type-name"));
    fail_unless (gst_structure_has_field_typed (cs, "address", G_TYPE_STRING));
    fail_unless (gst_structure_get_string (cs, "address"));

    removed = gst_structure_get_value (cpoint, "objects-removed-list");
    fail_unless (G_VALUE_HOLDS (removed, GST_TYPE_LIST));
    removed = gst_value_list_get_value (removed, 0);
    fail_unless (G_VALUE_HOLDS (removed, GST_TYPE_STRUCTURE));
    rs = gst_value_get_structure (removed);
    fail_unless (gst_structure_has_field_typed (rs, "type-name",
            G_TYPE_STRING));
    fail_unless (gst_structure_get_string (rs, "type-name"));
    fail_unless (gst_structure_has_field_typed (rs, "address", G_TYPE_STRING));
    fail_unless (gst_structure_get_string (rs, "address"));
    gst_structure_free (cpoint);
  }
  g_signal_emit_by_name (tracer, "activity-stop-tracking");
  gst_object_unref (tracer);
}

GST_END_TEST;

static Suite *
leakstracer_suite (void)
{
  Suite *s = suite_create ("leakstracer");
  TCase *tc_chain_1 = tcase_create ("live-objects");
  TCase *tc_chain_2 = tcase_create ("activity-tracking");

  suite_add_tcase (s, tc_chain_1);
  tcase_add_test (tc_chain_1, test_log_live_objects);
  tcase_add_test (tc_chain_1, test_get_live_objects);
  tcase_add_test (tc_chain_1, test_get_live_objects_filtered_detailed);

  suite_add_tcase (s, tc_chain_2);
  tcase_add_test (tc_chain_2, test_activity_start_stop);
  tcase_add_test (tc_chain_2, test_activity_log_checkpoint);
  tcase_add_test (tc_chain_2, test_activity_get_checkpoint);

  return s;
}

/* Replacement for GST_CHECK_MAIN (leakstracer); because we need to set the
 * env before gst_init() is called */
int
main (int argc, char **argv)
{
  Suite *s;
  g_setenv ("GST_TRACERS", "leaks(name=plain,log-leaks-on-deinit=false);"
      "leaks(name=more,filters=GstPad,check-refs=true,stack-traces-flags=none,log-leaks-on-deinit=false);",
      TRUE);
  gst_check_init (&argc, &argv);
  s = leakstracer_suite ();
  return gst_check_run_suite (s, "leakstracer", __FILE__);
}
