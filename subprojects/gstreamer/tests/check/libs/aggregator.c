/*
 * aggregator.c - GstAggregator testsuite
 * Copyright (C) 2006 Alessandro Decina <alessandro.d@gmail.com>
 * Copyright (C) 2014 Mathieu Duponchelle <mathieu.duponchelle@oencreed.com>
 * Copyright (C) 2014 Thibault Saunier <tsaunier@opencreed.com>
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
#  include "config.h"
#endif

#include <stdlib.h>
#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>
#include <gst/base/gstaggregator.h>

/* dummy aggregator based element */

#define GST_TYPE_TEST_AGGREGATOR            (gst_test_aggregator_get_type ())
#define GST_TEST_AGGREGATOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_TEST_AGGREGATOR, GstTestAggregator))
#define GST_TEST_AGGREGATOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_TEST_AGGREGATOR, GstTestAggregatorClass))
#define GST_TEST_AGGREGATOR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_TEST_AGGREGATOR, GstTestAggregatorClass))

#define fail_error_message(msg)     \
  G_STMT_START {        \
    GError *error;        \
    gst_message_parse_error(msg, &error, NULL);       \
    fail_unless(FALSE, "Error Message from %s : %s",      \
    GST_OBJECT_NAME (GST_MESSAGE_SRC(msg)), error->message); \
    g_error_free (error);           \
  } G_STMT_END;

typedef struct _GstTestAggregator GstTestAggregator;
typedef struct _GstTestAggregatorClass GstTestAggregatorClass;

static GType gst_test_aggregator_get_type (void);

#define BUFFER_DURATION 100000000       /* 10 frames per second */
#define TEST_GAP_PTS 0
#define TEST_GAP_DURATION (5 * GST_SECOND)

struct _GstTestAggregator
{
  GstAggregator parent;

  guint64 timestamp;
  gboolean gap_expected;
  gboolean do_flush_on_aggregate;
  gboolean do_remove_pad_on_aggregate;
};

struct _GstTestAggregatorClass
{
  GstAggregatorClass parent_class;
};

static GstFlowReturn
gst_test_aggregator_aggregate (GstAggregator * aggregator, gboolean timeout)
{
  GstIterator *iter;
  gboolean all_eos = TRUE;
  GstTestAggregator *testagg;
  GstBuffer *buf;

  gboolean done_iterating = FALSE;

  testagg = GST_TEST_AGGREGATOR (aggregator);

  iter = gst_element_iterate_sink_pads (GST_ELEMENT (testagg));
  while (!done_iterating) {
    GValue value = { 0, };
    GstAggregatorPad *pad;

    switch (gst_iterator_next (iter, &value)) {
      case GST_ITERATOR_OK:
        pad = g_value_get_object (&value);

        if (gst_aggregator_pad_is_eos (pad) == FALSE)
          all_eos = FALSE;

        if (testagg->gap_expected == TRUE) {
          buf = gst_aggregator_pad_peek_buffer (pad);
          fail_unless (buf);
          fail_unless (GST_BUFFER_PTS (buf) == TEST_GAP_PTS);
          fail_unless (GST_BUFFER_DURATION (buf) == TEST_GAP_DURATION);
          fail_unless (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_GAP));
          fail_unless (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DROPPABLE));
          gst_buffer_unref (buf);
          testagg->gap_expected = FALSE;
        }

        if (testagg->do_flush_on_aggregate) {
          GstBuffer *popped_buf;
          buf = gst_aggregator_pad_peek_buffer (pad);

          GST_DEBUG_OBJECT (pad, "Flushing on aggregate");

          gst_pad_send_event (GST_PAD (pad), gst_event_new_flush_start ());
          popped_buf = gst_aggregator_pad_pop_buffer (pad);

          fail_unless (buf == popped_buf);
          gst_buffer_unref (buf);
          gst_buffer_unref (popped_buf);
        } else if (testagg->do_remove_pad_on_aggregate) {
          buf = gst_aggregator_pad_peek_buffer (pad);

          GST_DEBUG_OBJECT (pad, "Removing pad on aggregate");

          gst_buffer_unref (buf);
          gst_element_release_request_pad (GST_ELEMENT (aggregator),
              GST_PAD (pad));
        } else {
          gst_aggregator_pad_drop_buffer (pad);
        }

        g_value_reset (&value);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        break;
      case GST_ITERATOR_ERROR:
        GST_WARNING_OBJECT (testagg, "Sinkpads iteration error");
        done_iterating = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done_iterating = TRUE;
        break;
    }
  }
  gst_iterator_free (iter);

  if (!gst_aggregator_get_force_live (aggregator)) {
    if (all_eos == TRUE) {
      GST_INFO_OBJECT (testagg, "no data available, must be EOS");
      gst_pad_push_event (aggregator->srcpad, gst_event_new_eos ());
      return GST_FLOW_EOS;
    }
  }

  buf = gst_buffer_new ();
  GST_BUFFER_TIMESTAMP (buf) = testagg->timestamp;
  GST_BUFFER_DURATION (buf) = BUFFER_DURATION;
  testagg->timestamp += BUFFER_DURATION;

  gst_aggregator_finish_buffer (aggregator, buf);

  /* We just check finish_frame return FLOW_OK */
  return GST_FLOW_OK;
}

#define gst_test_aggregator_parent_class parent_class
G_DEFINE_TYPE (GstTestAggregator, gst_test_aggregator, GST_TYPE_AGGREGATOR);

static gboolean gst_aggregator_test_slow_down_sink_query = FALSE;

static gboolean
gst_aggregator_test_slow_sink_query (GstAggregator * self,
    GstAggregatorPad * aggpad, GstQuery * query)
{
  GST_DEBUG ("Handling query %" GST_PTR_FORMAT, query);
  if (GST_QUERY_IS_SERIALIZED (query)) {
    GstStructure *s = gst_query_writable_structure (query);

    if (gst_aggregator_test_slow_down_sink_query)
      g_usleep (G_TIME_SPAN_MILLISECOND * 10);
    gst_structure_set (s, "some-int", G_TYPE_INT, 123, NULL);
    GST_DEBUG ("Written to the query %" GST_PTR_FORMAT, query);
  }
  return GST_AGGREGATOR_CLASS (parent_class)->sink_query (self, aggpad, query);
}

static void
gst_test_aggregator_class_init (GstTestAggregatorClass * klass)
{
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstAggregatorClass *base_aggregator_class = (GstAggregatorClass *) klass;

  static GstStaticPadTemplate _src_template =
      GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      GST_STATIC_CAPS_ANY);

  static GstStaticPadTemplate _sink_template =
      GST_STATIC_PAD_TEMPLATE ("sink_%u", GST_PAD_SINK, GST_PAD_REQUEST,
      GST_STATIC_CAPS_ANY);

  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &_src_template, GST_TYPE_AGGREGATOR_PAD);

  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &_sink_template, GST_TYPE_AGGREGATOR_PAD);

  gst_element_class_set_static_metadata (gstelement_class, "Aggregator",
      "Testing", "Combine N buffers", "Stefan Sauer <ensonic@users.sf.net>");

  base_aggregator_class->aggregate =
      GST_DEBUG_FUNCPTR (gst_test_aggregator_aggregate);

  base_aggregator_class->get_next_time = gst_aggregator_simple_get_next_time;
  base_aggregator_class->sink_query = gst_aggregator_test_slow_sink_query;
}

static void
gst_test_aggregator_init (GstTestAggregator * self)
{
  GstAggregator *agg = GST_AGGREGATOR (self);
  gst_segment_init (&GST_AGGREGATOR_PAD (agg->srcpad)->segment,
      GST_FORMAT_TIME);
  self->timestamp = 0;
  self->gap_expected = FALSE;
}

static gboolean
gst_test_aggregator_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "testaggregator", GST_RANK_NONE,
      GST_TYPE_TEST_AGGREGATOR);
}

static gboolean
gst_test_aggregator_plugin_register (void)
{
  return gst_plugin_register_static (GST_VERSION_MAJOR,
      GST_VERSION_MINOR,
      "testaggregator",
      "Combine buffers",
      gst_test_aggregator_plugin_init,
      VERSION, GST_LICENSE, PACKAGE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
}

/* test helpers */

typedef struct
{
  GQueue *queue;
  GstElement *aggregator;
  GstPad *sinkpad, *srcpad;
  GstFlowReturn expected_result;

  /*                       ------------------
   * -----------   --------|--              |
   * | srcpad | -- | sinkpad |  aggregator  |
   * -----------   --------|--              |
   *                       ------------------
   *  This is for 1 Chain, we can have several
   */
} ChainData;

typedef struct
{
  GMainLoop *ml;
  GstPad *srcpad,               /* srcpad of the GstAggregator */
   *sinkpad;                    /* fake sinkpad to which GstAggregator.srcpad is linked */
  guint timeout_id;
  GstElement *aggregator;

  /* -----------------|
   * |             ----------    -----------
   * | aggregator  | srcpad | -- | sinkpad |
   * |             ----------    -----------
   * -----------------|
   */

  gint flush_start_events, flush_stop_events;
} TestData;

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void
start_flow (ChainData * chain_data)
{
  GstSegment segment;
  GstCaps *caps;

  gst_pad_push_event (chain_data->srcpad, gst_event_new_stream_start ("test"));

  caps = gst_caps_new_empty_simple ("foo/x-bar");
  gst_pad_push_event (chain_data->srcpad, gst_event_new_caps (caps));
  gst_caps_unref (caps);

  gst_segment_init (&segment, GST_FORMAT_TIME);
  gst_pad_push_event (chain_data->srcpad, gst_event_new_segment (&segment));
}

static gpointer
push_data (gpointer user_data)
{
  ChainData *chain_data = (ChainData *) user_data;
  GstTestAggregator *aggregator = (GstTestAggregator *) chain_data->aggregator;
  GstPad *sinkpad = chain_data->sinkpad;
  GstPad *srcpad = chain_data->srcpad;
  gpointer data;

  start_flow (chain_data);

  while ((data = g_queue_pop_head (chain_data->queue))) {
    GST_DEBUG_OBJECT (sinkpad, "Pushing %" GST_PTR_FORMAT, data);

    /* switch on the data type and push */
    if (GST_IS_BUFFER (data)) {
      GstFlowReturn flow = gst_pad_push (srcpad, GST_BUFFER_CAST (data));
      fail_unless (flow == chain_data->expected_result,
          "got flow %s instead of %s on %s:%s", gst_flow_get_name (flow),
          gst_flow_get_name (chain_data->expected_result),
          GST_DEBUG_PAD_NAME (sinkpad));
    } else if (GST_IS_EVENT (data)) {
      switch (GST_EVENT_TYPE (data)) {
        case GST_EVENT_GAP:
          aggregator->gap_expected = TRUE;
          break;
        default:
          break;
      }
      fail_unless (gst_pad_push_event (srcpad, GST_EVENT_CAST (data)));
    } else if (GST_IS_QUERY (data)) {
      /* we don't care whether the query actually got handled */
      gst_pad_peer_query (srcpad, GST_QUERY_CAST (data));
      gst_query_unref (GST_QUERY_CAST (data));
    } else {
      GST_WARNING_OBJECT (sinkpad, "bad queue entry: %" GST_PTR_FORMAT, data);
    }
  }
  GST_DEBUG_OBJECT (sinkpad, "All data from queue sent");

  return NULL;
}

static gboolean
_aggregate_timeout (GMainLoop * ml)
{
  g_main_loop_quit (ml);

  fail_unless ("No buffer found on aggregator.srcpad -> TIMEOUT" == NULL);

  return FALSE;
}

static gboolean
_quit (GMainLoop * ml)
{
  GST_DEBUG ("QUITTING ML");
  g_main_loop_quit (ml);

  return G_SOURCE_REMOVE;
}

static GstPadProbeReturn
_aggregated_cb (GstPad * pad, GstPadProbeInfo * info, GMainLoop * ml)
{
  GST_DEBUG ("Received data %" GST_PTR_FORMAT, info->data);
  GST_DEBUG ("Should quit ML");
  g_idle_add ((GSourceFunc) _quit, ml);

  return GST_PAD_PROBE_REMOVE;
}

static GstPadProbeReturn
_downstream_probe_cb (GstPad * pad, GstPadProbeInfo * info, TestData * test)
{
  GST_DEBUG ("PROBING ");
  if (info->type & GST_PAD_PROBE_TYPE_EVENT_FLUSH) {
    if (GST_EVENT_TYPE (GST_PAD_PROBE_INFO_EVENT (info)) ==
        GST_EVENT_FLUSH_START) {

      g_atomic_int_inc (&test->flush_start_events);
      GST_DEBUG ("==========> FLUSH: %i", test->flush_start_events);
    } else if (GST_EVENT_TYPE (GST_PAD_PROBE_INFO_EVENT (info)) ==
        GST_EVENT_FLUSH_STOP)
      g_atomic_int_inc (&test->flush_stop_events);
  }
  return GST_PAD_PROBE_OK;
}

/*
 * Not thread safe, will create a new ChainData which contains
 * an activated src pad linked to a requested sink pad of @agg, and
 * a newly allocated buffer ready to be pushed. Caller needs to
 * clear with _chain_data_clear after.
 */
static void
_chain_data_init (ChainData * data, GstElement * agg, ...)
{
  static gint num_src_pads = 0;
  gchar *pad_name = g_strdup_printf ("src%d", num_src_pads);
  va_list var_args;
  gpointer d;

  num_src_pads += 1;

  data->srcpad = gst_pad_new_from_static_template (&srctemplate, pad_name);
  g_free (pad_name);
  gst_pad_set_active (data->srcpad, TRUE);
  data->aggregator = agg;
  data->sinkpad = gst_element_request_pad_simple (agg, "sink_%u");
  fail_unless (GST_IS_PAD (data->sinkpad));
  fail_unless (gst_pad_link (data->srcpad, data->sinkpad) == GST_PAD_LINK_OK);

  /* add data items */
  data->queue = g_queue_new ();
  va_start (var_args, agg);
  while (TRUE) {
    if (!(d = va_arg (var_args, gpointer)))
      break;
    g_queue_push_tail (data->queue, d);
    GST_DEBUG_OBJECT (data->sinkpad, "Adding to queue: %" GST_PTR_FORMAT, d);
  }
  va_end (var_args);
}

static void
_chain_data_clear (ChainData * chain_data)
{
  gpointer data;

  while ((data = g_queue_pop_head (chain_data->queue))) {
    /* switch on the data type and free */
    if (GST_IS_BUFFER (data)) {
      gst_buffer_unref (GST_BUFFER_CAST (data));
    } else if (GST_IS_EVENT (data)) {
      gst_event_unref (GST_EVENT_CAST (data));
    } else if (GST_IS_QUERY (data)) {
      gst_query_unref (GST_QUERY_CAST (data));
    } else {
      GST_WARNING_OBJECT (chain_data->sinkpad, "bad queue entry: %"
          GST_PTR_FORMAT, data);
    }
  }
  g_queue_free (chain_data->queue);

  if (chain_data->srcpad)
    gst_object_unref (chain_data->srcpad);
  if (chain_data->sinkpad)
    gst_object_unref (chain_data->sinkpad);
}

static GstFlowReturn
_test_chain (GstPad * pad, GstObject * object, GstBuffer * buffer)
{
  /* accept any buffers */
  gst_buffer_unref (buffer);
  return GST_FLOW_OK;
}

static void
_test_data_init (TestData * test, gboolean needs_flushing)
{
  const gchar *timeout_factor_str = g_getenv ("TIMEOUT_FACTOR");
  gint timeout = 1000;

  test->aggregator = gst_element_factory_make ("testaggregator", NULL);
  gst_element_set_state (test->aggregator, GST_STATE_PLAYING);
  test->ml = g_main_loop_new (NULL, TRUE);
  test->srcpad = GST_AGGREGATOR (test->aggregator)->srcpad;

  GST_DEBUG_OBJECT (test->srcpad, "Init test data for srcpad");

  if (needs_flushing) {
    static gint num_sink_pads = 0;
    gchar *pad_name = g_strdup_printf ("sink%d", num_sink_pads);

    num_sink_pads += 1;
    test->sinkpad = gst_pad_new_from_static_template (&sinktemplate, pad_name);
    gst_pad_set_chain_function (test->sinkpad, _test_chain);
    gst_pad_set_active (test->sinkpad, TRUE);
    g_free (pad_name);
    fail_unless (gst_pad_link (test->srcpad, test->sinkpad) == GST_PAD_LINK_OK);
    gst_pad_add_probe (test->srcpad, GST_PAD_PROBE_TYPE_EVENT_FLUSH,
        (GstPadProbeCallback) _downstream_probe_cb, test, NULL);
  } else {
    gst_pad_add_probe (test->srcpad, GST_PAD_PROBE_TYPE_BUFFER,
        (GstPadProbeCallback) _aggregated_cb, test->ml, NULL);
  }

  if (timeout_factor_str) {
    gint factor = g_ascii_strtoll (timeout_factor_str, NULL, 10);
    if (factor)
      timeout *= factor;
  }

  test->timeout_id =
      g_timeout_add (timeout, (GSourceFunc) _aggregate_timeout, test->ml);
}

static void
_test_data_clear (TestData * test)
{
  gst_element_set_state (test->aggregator, GST_STATE_NULL);
  gst_object_unref (test->aggregator);

  if (test->sinkpad)
    gst_object_unref (test->sinkpad);

  g_main_loop_unref (test->ml);
}

/* tests */

GST_START_TEST (test_aggregate)
{
  GThread *thread1, *thread2;
  ChainData data1 = { 0, };
  ChainData data2 = { 0, };
  TestData test = { 0, };

  _test_data_init (&test, FALSE);
  _chain_data_init (&data1, test.aggregator, gst_buffer_new (), NULL);
  _chain_data_init (&data2, test.aggregator, gst_buffer_new (), NULL);

  thread1 = g_thread_try_new ("gst-check", push_data, &data1, NULL);
  thread2 = g_thread_try_new ("gst-check", push_data, &data2, NULL);

  g_main_loop_run (test.ml);
  g_source_remove (test.timeout_id);

  /* these will return immediately as when the data is popped the threads are
   * unlocked and will terminate */
  g_thread_join (thread1);
  g_thread_join (thread2);

  _chain_data_clear (&data1);
  _chain_data_clear (&data2);
  _test_data_clear (&test);
}

GST_END_TEST;

GST_START_TEST (test_aggregate_eos)
{
  GThread *thread1, *thread2;
  ChainData data1 = { 0, };
  ChainData data2 = { 0, };
  TestData test = { 0, };

  _test_data_init (&test, FALSE);
  _chain_data_init (&data1, test.aggregator, gst_buffer_new (), NULL);
  _chain_data_init (&data2, test.aggregator, gst_event_new_eos (), NULL);

  thread1 = g_thread_try_new ("gst-check", push_data, &data1, NULL);
  thread2 = g_thread_try_new ("gst-check", push_data, &data2, NULL);

  g_main_loop_run (test.ml);
  g_source_remove (test.timeout_id);

  /* these will return immediately as when the data is popped the threads are
   * unlocked and will terminate */
  g_thread_join (thread1);
  g_thread_join (thread2);

  _chain_data_clear (&data1);
  _chain_data_clear (&data2);
  _test_data_clear (&test);
}

GST_END_TEST;

GST_START_TEST (test_aggregate_gap)
{
  GThread *thread;
  ChainData data = { 0, };
  TestData test = { 0, };

  _test_data_init (&test, FALSE);
  _chain_data_init (&data, test.aggregator,
      gst_event_new_gap (TEST_GAP_PTS, TEST_GAP_DURATION), NULL);

  thread = g_thread_try_new ("gst-check", push_data, &data, NULL);

  g_main_loop_run (test.ml);
  g_source_remove (test.timeout_id);

  /* these will return immediately as when the data is popped the threads are
   * unlocked and will terminate */
  g_thread_join (thread);

  _chain_data_clear (&data);
  _test_data_clear (&test);
}

GST_END_TEST;

GST_START_TEST (test_aggregate_handle_events)
{
  GThread *thread1, *thread2;
  ChainData data1 = { 0, };
  ChainData data2 = { 0, };
  TestData test = { 0, };

  _test_data_init (&test, FALSE);
  _chain_data_init (&data1, test.aggregator,
      gst_event_new_tag (gst_tag_list_new_empty ()), gst_buffer_new (), NULL);
  _chain_data_init (&data2, test.aggregator, gst_buffer_new (), NULL);

  thread1 = g_thread_try_new ("gst-check", push_data, &data1, NULL);
  thread2 = g_thread_try_new ("gst-check", push_data, &data2, NULL);

  g_main_loop_run (test.ml);
  g_source_remove (test.timeout_id);

  /* these will return immediately as when the data is popped the threads are
   * unlocked and will terminate */
  g_thread_join (thread1);
  g_thread_join (thread2);

  _chain_data_clear (&data1);
  _chain_data_clear (&data2);
  _test_data_clear (&test);
}

GST_END_TEST;

GST_START_TEST (test_aggregate_handle_queries)
{
  GThread *thread1, *thread2;
  ChainData data1 = { 0, };
  ChainData data2 = { 0, };
  TestData test = { 0, };
  GstCaps *caps;

  _test_data_init (&test, FALSE);

  caps = gst_caps_new_empty_simple ("foo/x-bar");
  _chain_data_init (&data1, test.aggregator,
      gst_query_new_allocation (caps, FALSE), gst_buffer_new (), NULL);
  gst_caps_unref (caps);

  _chain_data_init (&data2, test.aggregator, gst_buffer_new (), NULL);

  thread1 = g_thread_try_new ("gst-check", push_data, &data1, NULL);
  thread2 = g_thread_try_new ("gst-check", push_data, &data2, NULL);

  g_main_loop_run (test.ml);
  g_source_remove (test.timeout_id);

  /* these will return immediately as when the data is popped the threads are
   * unlocked and will terminate */
  g_thread_join (thread1);
  g_thread_join (thread2);

  _chain_data_clear (&data1);
  _chain_data_clear (&data2);
  _test_data_clear (&test);
}

GST_END_TEST;

GST_START_TEST (test_aggregate_queries_robustness)
{
  GThread *thread1;
  ChainData data1 = { 0, };
  TestData test = { 0, };
  GstCaps *caps;
  gint64 start_time;

  gst_aggregator_test_slow_down_sink_query = TRUE;

  _test_data_init (&test, FALSE);

  caps = gst_caps_new_empty_simple ("foo/x-bar");
  _chain_data_init (&data1, test.aggregator,
      gst_query_new_allocation (caps, FALSE),
      gst_query_new_allocation (caps, FALSE),
      gst_query_new_allocation (caps, FALSE),
      gst_query_new_allocation (caps, FALSE),
      gst_query_new_allocation (caps, FALSE),
      gst_query_new_allocation (caps, FALSE),
      gst_query_new_allocation (caps, FALSE),
      gst_query_new_allocation (caps, FALSE),
      gst_query_new_allocation (caps, FALSE),
      gst_query_new_allocation (caps, FALSE),
      gst_query_new_allocation (caps, FALSE),
      gst_query_new_allocation (caps, FALSE),
      gst_query_new_allocation (caps, FALSE),
      gst_query_new_allocation (caps, FALSE),
      gst_query_new_allocation (caps, FALSE),
      gst_query_new_allocation (caps, FALSE),
      gst_query_new_allocation (caps, FALSE), NULL);
  gst_caps_unref (caps);

  thread1 = g_thread_try_new ("gst-check", push_data, &data1, NULL);
  g_usleep (G_TIME_SPAN_MILLISECOND * 5);
  for (start_time = g_get_monotonic_time ();
      start_time + G_TIME_SPAN_SECOND > g_get_monotonic_time ();
      g_usleep (G_TIME_SPAN_MILLISECOND)) {
    fail_unless (gst_element_send_event (test.aggregator,
            gst_event_new_flush_start ()));
    fail_unless (gst_element_send_event (test.aggregator,
            gst_event_new_flush_stop (TRUE)));
  }

  g_thread_join (thread1);

  _chain_data_clear (&data1);
  _test_data_clear (&test);

  gst_aggregator_test_slow_down_sink_query = FALSE;
}

GST_END_TEST;

#define NUM_BUFFERS 3
static void
handoff (GstElement * fakesink, GstBuffer * buf, GstPad * pad, guint * count)
{
  *count = *count + 1;
  GST_DEBUG ("HANDOFF: %i", *count);
}

/* Test a linear pipeline using aggregator */
GST_START_TEST (test_linear_pipeline)
{
  GstBus *bus;
  GstMessage *msg;
  GstElement *pipeline, *src, *agg, *sink;
  gint count = 0;

  pipeline = gst_pipeline_new ("pipeline");
  src = gst_check_setup_element ("fakesrc");
  g_object_set (src, "num-buffers", NUM_BUFFERS, "sizetype", 2, "sizemax", 4,
      NULL);
  agg = gst_check_setup_element ("testaggregator");
  sink = gst_check_setup_element ("fakesink");
  g_object_set (sink, "signal-handoffs", TRUE, NULL);
  g_signal_connect (sink, "handoff", (GCallback) handoff, &count);

  fail_unless (gst_bin_add (GST_BIN (pipeline), src));
  fail_unless (gst_bin_add (GST_BIN (pipeline), agg));
  fail_unless (gst_bin_add (GST_BIN (pipeline), sink));
  fail_unless (gst_element_link (src, agg));
  fail_unless (gst_element_link (agg, sink));

  bus = gst_element_get_bus (pipeline);
  fail_if (bus == NULL);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
  fail_if (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_EOS);
  gst_message_unref (msg);

  fail_unless_equals_int (count, NUM_BUFFERS);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_two_src_pipeline)
{
  GstBus *bus;
  GstMessage *msg;
  GstElement *pipeline, *src, *src1, *agg, *sink;
  gint count = 0;

  pipeline = gst_pipeline_new ("pipeline");
  src = gst_element_factory_make ("fakesrc", NULL);
  g_object_set (src, "num-buffers", NUM_BUFFERS, "sizetype", 2, "sizemax", 4,
      NULL);

  src1 = gst_element_factory_make ("fakesrc", NULL);
  g_object_set (src1, "num-buffers", NUM_BUFFERS + 1, "sizetype", 2, "sizemax",
      4, NULL);

  agg = gst_check_setup_element ("testaggregator");
  sink = gst_check_setup_element ("fakesink");
  g_object_set (sink, "signal-handoffs", TRUE, NULL);
  g_signal_connect (sink, "handoff", (GCallback) handoff, &count);

  fail_unless (gst_bin_add (GST_BIN (pipeline), src));
  fail_unless (gst_bin_add (GST_BIN (pipeline), src1));
  fail_unless (gst_bin_add (GST_BIN (pipeline), agg));
  fail_unless (gst_bin_add (GST_BIN (pipeline), sink));
  fail_unless (gst_element_link (src, agg));
  fail_unless (gst_element_link (src1, agg));
  fail_unless (gst_element_link (agg, sink));

  bus = gst_element_get_bus (pipeline);
  fail_if (bus == NULL);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
  fail_if (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_EOS);
  gst_message_unref (msg);

  fail_unless_equals_int (count, NUM_BUFFERS + 1);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

GST_END_TEST;

static GstPadProbeReturn
_drop_buffer_probe_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  gint wait;

  if (GST_IS_BUFFER (info->data)) {
    wait = GPOINTER_TO_INT (user_data);
    if (wait > 0)
      g_usleep (wait / 1000);
    return GST_PAD_PROBE_DROP;
  }

  return GST_PAD_PROBE_PASS;
}

#define TIMEOUT_NUM_BUFFERS 20
static void
_test_timeout (gint buffer_wait)
{
  GstBus *bus;
  GstMessage *msg;
  GstElement *pipeline, *src, *src1, *agg, *sink;
  GstPad *src1pad;
  gint count = 0;

  pipeline = gst_pipeline_new ("pipeline");
  src = gst_element_factory_make ("fakesrc", NULL);
  g_object_set (src, "num-buffers", TIMEOUT_NUM_BUFFERS, "sizetype", 2,
      "sizemax", 4, "is-live", TRUE, "datarate", 4000, NULL);

  src1 = gst_element_factory_make ("fakesrc", NULL);
  g_object_set (src1, "num-buffers", TIMEOUT_NUM_BUFFERS, "sizetype", 2,
      "sizemax", 4, "is-live", TRUE, "datarate", 4000, NULL);

  agg = gst_check_setup_element ("testaggregator");
  g_object_set (agg, "latency", GST_USECOND, NULL);
  sink = gst_check_setup_element ("fakesink");
  g_object_set (sink, "signal-handoffs", TRUE, NULL);
  g_signal_connect (sink, "handoff", (GCallback) handoff, &count);

  fail_unless (gst_bin_add (GST_BIN (pipeline), src));
  fail_unless (gst_bin_add (GST_BIN (pipeline), src1));
  fail_unless (gst_bin_add (GST_BIN (pipeline), agg));
  fail_unless (gst_bin_add (GST_BIN (pipeline), sink));

  src1pad = gst_element_get_static_pad (src1, "src");
  fail_if (src1pad == NULL);
  gst_pad_add_probe (src1pad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM,
      (GstPadProbeCallback) _drop_buffer_probe_cb,
      GINT_TO_POINTER (buffer_wait), NULL);

  fail_unless (gst_element_link (src, agg));
  fail_unless (gst_element_link (src1, agg));
  fail_unless (gst_element_link (agg, sink));

  bus = gst_element_get_bus (pipeline);
  fail_if (bus == NULL);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
  fail_if (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_EOS);
  gst_message_unref (msg);

  /* cannot rely on the exact number of buffers as the timeout may produce
   * more buffers with the unsynchronized _aggregate() implementation in
   * testaggregator */
  fail_if (count < TIMEOUT_NUM_BUFFERS);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (src1pad);
  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

GST_START_TEST (test_timeout_pipeline)
{
  _test_timeout (0);
}

GST_END_TEST;

GST_START_TEST (test_timeout_pipeline_with_wait)
{
  _test_timeout (1000000 /* 1 ms */ );
}

GST_END_TEST;

GST_START_TEST (test_flushing_seek)
{
  GstEvent *event;
  GThread *thread1, *thread2;
  ChainData data1 = { 0, };
  ChainData data2 = { 0, };
  TestData test = { 0, };
  GstBuffer *buf;
  guint32 seqnum;

  _test_data_init (&test, TRUE);

  /* Queue a buffer in agg:sink_1. Then do a flushing seek and check that the
   * new flushing seek logic is triggered. On the first FLUSH_START call the
   * buffers queued in collectpads should get flushed. Only one FLUSH_START and
   * one FLUSH_STOP should be forwarded downstream.
   */
  _chain_data_init (&data1, test.aggregator, gst_buffer_new (), NULL);

  buf = gst_buffer_new ();
  GST_BUFFER_TIMESTAMP (buf) = 0;
  _chain_data_init (&data2, test.aggregator, buf, NULL);

  gst_segment_init (&GST_AGGREGATOR_PAD (GST_AGGREGATOR (test.aggregator)->
          srcpad)->segment, GST_FORMAT_TIME);

  /* now do a successful flushing seek */
  event = gst_event_new_seek (1, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
      GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_SET, 10 * GST_SECOND);
  seqnum = gst_event_get_seqnum (event);
  fail_unless (gst_pad_send_event (test.srcpad, event));

  /* flushing starts when a flushing seek is received, and stops
   * when all sink pads have received FLUSH_STOP */
  fail_unless_equals_int (test.flush_start_events, 1);
  fail_unless_equals_int (test.flush_stop_events, 0);

  /* send a first FLUSH_START on agg:sink_0, nothing will be sent
   * downstream */
  GST_DEBUG_OBJECT (data2.sinkpad, "send flush_start");
  event = gst_event_new_flush_start ();
  gst_event_set_seqnum (event, seqnum);
  fail_unless (gst_pad_push_event (data2.srcpad, event));
  fail_unless_equals_int (test.flush_start_events, 1);
  fail_unless_equals_int (test.flush_stop_events, 0);

  /* expect this buffer to be flushed */
  data2.expected_result = GST_FLOW_FLUSHING;
  thread2 = g_thread_try_new ("gst-check", push_data, &data2, NULL);

  /* this should send no additional flush_start */
  GST_DEBUG_OBJECT (data1.sinkpad, "send flush_start");
  event = gst_event_new_flush_start ();
  gst_event_set_seqnum (event, seqnum);
  fail_unless (gst_pad_push_event (data1.srcpad, event));
  fail_unless_equals_int (test.flush_start_events, 1);
  fail_unless_equals_int (test.flush_stop_events, 0);

  /* the first FLUSH_STOP is not forwarded downstream */
  GST_DEBUG_OBJECT (data1.srcpad, "send flush_stop");
  event = gst_event_new_flush_stop (TRUE);
  gst_event_set_seqnum (event, seqnum);
  fail_unless (gst_pad_push_event (data1.srcpad, event));
  fail_unless_equals_int (test.flush_start_events, 1);
  fail_unless_equals_int (test.flush_stop_events, 0);

  /* at this point even the other pad agg:sink_1 should be flushing so thread2
   * should have stopped */
  g_thread_join (thread2);

  /* push a buffer on agg:sink_0 to trigger one collect after flushing to verify
   * that flushing completes once all the pads have been flushed */
  thread1 = g_thread_try_new ("gst-check", push_data, &data1, NULL);

  /* flush agg:sink_1 as well. This completes the flushing seek so a FLUSH_STOP is
   * sent downstream */
  GST_DEBUG_OBJECT (data2.srcpad, "send flush_stop");
  event = gst_event_new_flush_stop (TRUE);
  gst_event_set_seqnum (event, seqnum);
  gst_pad_push_event (data2.srcpad, event);

  /* and the last FLUSH_STOP is forwarded downstream */
  fail_unless_equals_int (test.flush_stop_events, 1);

  /*  Check collected */
  gst_pad_add_probe (test.srcpad, GST_PAD_PROBE_TYPE_BUFFER,
      (GstPadProbeCallback) _aggregated_cb, test.ml, NULL);

  g_queue_push_tail (data2.queue, gst_event_new_eos ());
  thread2 = g_thread_try_new ("gst-check", push_data, &data2, NULL);

  g_main_loop_run (test.ml);
  g_source_remove (test.timeout_id);

  fail_unless_equals_int (test.flush_stop_events, 1);

  /* these will return immediately as at this point the threads have been
   * unlocked and are finished */
  g_thread_join (thread1);
  g_thread_join (thread2);

  _chain_data_clear (&data1);
  _chain_data_clear (&data2);
  _test_data_clear (&test);

}

GST_END_TEST;

static void
infinite_seek (guint num_srcs, guint num_seeks, gboolean is_live)
{
  GstBus *bus;
  GstMessage *message;
  GstElement *pipeline, *src, *agg, *sink;
  gint count = 0, i;
  gboolean seek_res, carry_on = TRUE;

  pipeline = gst_pipeline_new ("pipeline");

  agg = gst_check_setup_element ("testaggregator");
  sink = gst_check_setup_element ("fakesink");

  if (is_live)
    g_object_set (agg, "latency", GST_MSECOND, NULL);

  fail_unless (gst_bin_add (GST_BIN (pipeline), agg));
  fail_unless (gst_bin_add (GST_BIN (pipeline), sink));
  fail_unless (gst_element_link (agg, sink));

  for (i = 0; i < num_srcs; i++) {
    src = gst_element_factory_make ("fakesrc", NULL);
    g_object_set (src, "sizetype", 2, "sizemax", 4,
        "format", GST_FORMAT_TIME, "datarate", 1000, NULL);
    if (is_live)
      g_object_set (src, "is-live", TRUE, NULL);
    fail_unless (gst_bin_add (GST_BIN (pipeline), src));
    fail_unless (gst_element_link (src, agg));
  }

  bus = gst_element_get_bus (pipeline);
  fail_if (bus == NULL);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  while (count < num_seeks && carry_on) {
    message = gst_bus_poll (bus, GST_MESSAGE_ANY, GST_SECOND / 10);
    if (message) {
      switch (GST_MESSAGE_TYPE (message)) {
        case GST_MESSAGE_EOS:
        {
          /* we should check if we really finished here */
          GST_WARNING ("Got an EOS");
          carry_on = FALSE;
          break;
        }
        case GST_MESSAGE_STATE_CHANGED:
        {
          GstState new;

          if (GST_MESSAGE_SRC (message) == GST_OBJECT (pipeline)) {
            gst_message_parse_state_changed (message, NULL, &new, NULL);

            if (new != GST_STATE_PLAYING)
              break;

            GST_INFO ("Seeking (num: %i)", count);
            seek_res =
                gst_element_seek_simple (sink, GST_FORMAT_TIME,
                GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE, 0);
            GST_INFO ("seek result is : %d", seek_res);
            fail_unless (seek_res != 0);
            count++;
          }

          break;
        }
        case GST_MESSAGE_ERROR:
          GST_ERROR ("Error on the bus: %" GST_PTR_FORMAT, message);
          carry_on = FALSE;
          fail_error_message (message);
          break;
        default:
          break;
      }
      gst_message_unref (message);
    }
  }

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

GST_START_TEST (test_infinite_seek)
{
  infinite_seek (2, 500, FALSE);
}

GST_END_TEST;

GST_START_TEST (test_infinite_seek_50_src)
{
  infinite_seek (50, 100, FALSE);
}

GST_END_TEST;

GST_START_TEST (test_infinite_seek_50_src_live)
{
  infinite_seek (50, 100, TRUE);
}

GST_END_TEST;

typedef struct
{
  GstElement *agg, *src, *pipeline;
  GCond *cond;
  GMutex *lock;
} RemoveElementData;

static GstPadProbeReturn
pad_probe_cb (GstPad * pad, GstPadProbeInfo * info, RemoveElementData * data)
{
  GstPad *peer;

  GST_INFO_OBJECT (pad, "Removing pad");

  peer = gst_pad_get_peer (pad);
  gst_pad_unlink (pad, peer);
  gst_element_release_request_pad (data->agg, peer);
  fail_unless (gst_bin_remove (GST_BIN (data->pipeline), data->src));
  gst_object_unref (peer);

  g_mutex_lock (data->lock);
  g_cond_broadcast (data->cond);
  g_mutex_unlock (data->lock);

  return GST_PAD_PROBE_OK;
}

GST_START_TEST (test_add_remove)
{
  /* Used to notify that we removed the pad from  */
  GCond cond;
  GMutex lock;
  GstBus *bus;
  GstState state;
  GstMessage *message;
  gboolean carry_on = TRUE;
  guint num_iterations = 100;
  GstPad *pad;
  GstElement *pipeline, *src, *src1 = NULL, *agg, *sink;
  gint count = 0;

  g_mutex_init (&lock);
  g_cond_init (&cond);

  pipeline = gst_pipeline_new ("pipeline");

  agg = gst_check_setup_element ("testaggregator");
  sink = gst_check_setup_element ("fakesink");

  fail_unless (gst_bin_add (GST_BIN (pipeline), agg));
  fail_unless (gst_bin_add (GST_BIN (pipeline), sink));
  fail_unless (gst_element_link (agg, sink));

  bus = gst_element_get_bus (pipeline);
  while (count < num_iterations) {

    src = gst_element_factory_make ("fakesrc", NULL);
    g_object_set (src, "num-buffers", 100000, "sizetype", 2, "sizemax", 4,
        "format", GST_FORMAT_TIME, "datarate", 1000, NULL);
    gst_element_set_locked_state (src, TRUE);
    fail_unless (gst_bin_add (GST_BIN (pipeline), src));
    fail_unless (gst_element_link (src, agg));
    gst_element_set_locked_state (src, FALSE);
    fail_unless (gst_element_sync_state_with_parent (src));

    if (count == 0)
      gst_element_set_state (pipeline, GST_STATE_PLAYING);

    /* Now make sure the seek happened */
    carry_on = TRUE;
    do {
      message = gst_bus_timed_pop (bus, -1);
      switch (GST_MESSAGE_TYPE (message)) {
        case GST_MESSAGE_EOS:
        {
          /* we should check if we really finished here */
          GST_WARNING ("Got an EOS");
          carry_on = FALSE;
          break;
        }
        case GST_MESSAGE_STATE_CHANGED:
        {
          if (GST_MESSAGE_SRC (message) == GST_OBJECT (pipeline)) {
            gst_message_parse_state_changed (message, NULL, &state, NULL);

            if (state == GST_STATE_PLAYING) {
              RemoveElementData data;

              carry_on = FALSE;
              if (count == 0) {
                GST_DEBUG ("First run, not removing any element yet");

                break;
              }

              data.src = gst_object_ref (src1);
              data.agg = agg;
              data.lock = &lock;
              data.cond = &cond;
              data.pipeline = pipeline;
              pad = gst_element_get_static_pad (data.src, "src");

              g_mutex_lock (&lock);
              gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM,
                  (GstPadProbeCallback) pad_probe_cb, &data, NULL);
              GST_INFO ("Waiting for %" GST_PTR_FORMAT " %s", pad,
                  gst_element_state_get_name (GST_STATE (data.src)));
              g_cond_wait (&cond, &lock);
              g_mutex_unlock (&lock);
              gst_object_unref (pad);

              /*  We can not set state from the streaming thread so we
               *  need to make sure that the source has been removed
               *  before setting its state to NULL */
              gst_element_set_state (data.src, GST_STATE_NULL);

              gst_object_unref (data.src);
            }
          }

          break;
        }
        case GST_MESSAGE_ERROR:
        {
          GST_ERROR ("Error on the bus: %" GST_PTR_FORMAT, message);
          carry_on = FALSE;
          fail_error_message (message);
          break;
        }
        default:
          break;
      }

      gst_message_unref (message);
    } while (carry_on);

    GST_INFO ("Seeking");
    fail_unless (gst_element_seek_simple (pipeline, GST_FORMAT_TIME,
            GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE, 0));

    count++;
    src1 = src;
  }
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (bus);
  gst_object_unref (pipeline);
  g_mutex_clear (&lock);
  g_cond_clear (&cond);
}

GST_END_TEST;

GST_START_TEST (test_change_state_intensive)
{
  GstBus *bus;
  GstMessage *message;
  GstElement *pipeline, *src, *agg, *sink;
  gint i, state_i = 0, num_srcs = 3;
  gboolean carry_on = TRUE, ready = FALSE;
  GstStateChangeReturn state_return;
  GstState wanted_state, wanted_states[] = {
    GST_STATE_PLAYING, GST_STATE_NULL, GST_STATE_PAUSED, GST_STATE_READY,
    GST_STATE_PLAYING, GST_STATE_NULL, GST_STATE_PAUSED, GST_STATE_READY,
    GST_STATE_PLAYING, GST_STATE_NULL, GST_STATE_PAUSED, GST_STATE_READY,
    GST_STATE_PAUSED, GST_STATE_READY, GST_STATE_PAUSED, GST_STATE_READY,
    GST_STATE_PAUSED, GST_STATE_READY, GST_STATE_PAUSED, GST_STATE_NULL,
    GST_STATE_PAUSED, GST_STATE_NULL, GST_STATE_PAUSED, GST_STATE_NULL,
    GST_STATE_PAUSED, GST_STATE_NULL, GST_STATE_PAUSED, GST_STATE_NULL,
    GST_STATE_PAUSED, GST_STATE_NULL, GST_STATE_PLAYING, GST_STATE_NULL,
    GST_STATE_PLAYING, GST_STATE_NULL, GST_STATE_PLAYING, GST_STATE_NULL,
    GST_STATE_PLAYING, GST_STATE_NULL, GST_STATE_PLAYING, GST_STATE_NULL,
    GST_STATE_PLAYING, GST_STATE_NULL, GST_STATE_PLAYING, GST_STATE_NULL,
  };

  pipeline = gst_pipeline_new ("pipeline");

  agg = gst_check_setup_element ("testaggregator");
  sink = gst_check_setup_element ("fakesink");

  fail_unless (gst_bin_add (GST_BIN (pipeline), agg));
  fail_unless (gst_bin_add (GST_BIN (pipeline), sink));
  fail_unless (gst_element_link (agg, sink));

  for (i = 0; i < num_srcs; i++) {
    src = gst_element_factory_make ("fakesrc", NULL);
    g_object_set (src, "sizetype", 2, "sizemax", 4, NULL);
    fail_unless (gst_bin_add (GST_BIN (pipeline), src));
    fail_unless (gst_element_link (src, agg));
  }

  bus = gst_element_get_bus (pipeline);
  fail_if (bus == NULL);

  wanted_state = wanted_states[state_i++];
  state_return = gst_element_set_state (pipeline, wanted_state);

  while (state_i < G_N_ELEMENTS (wanted_states) && carry_on) {
    if (state_return == GST_STATE_CHANGE_SUCCESS && ready) {
      wanted_state = wanted_states[state_i++];
      fail_unless (gst_element_set_state (pipeline, wanted_state),
          GST_STATE_CHANGE_SUCCESS);
      GST_INFO ("Wanted state: %s", gst_element_state_get_name (wanted_state));
    }

    message = gst_bus_poll (bus, GST_MESSAGE_ANY, GST_SECOND / 10);
    if (message) {
      switch (GST_MESSAGE_TYPE (message)) {
        case GST_MESSAGE_EOS:
        {
          /* we should check if we really finished here */
          GST_WARNING ("Got an EOS");
          carry_on = FALSE;
          break;
        }
        case GST_MESSAGE_STATE_CHANGED:
        {
          GstState new;

          if (GST_MESSAGE_SRC (message) == GST_OBJECT (pipeline)) {
            gst_message_parse_state_changed (message, NULL, &new, NULL);

            if (new != wanted_state) {
              ready = FALSE;
              break;
            }

            GST_DEBUG ("State %s reached",
                gst_element_state_get_name (wanted_state));
            wanted_state = wanted_states[state_i++];
            GST_DEBUG ("Wanted state: %s",
                gst_element_state_get_name (wanted_state));
            state_return = gst_element_set_state (pipeline, wanted_state);
            fail_unless (state_return == GST_STATE_CHANGE_SUCCESS ||
                state_return == GST_STATE_CHANGE_ASYNC);
            ready = TRUE;
          }

          break;
        }
        case GST_MESSAGE_ERROR:
          GST_ERROR ("Error on the bus: %" GST_PTR_FORMAT, message);
          carry_on = FALSE;
          break;
        default:
          break;
      }
      gst_message_unref (message);
    }
  }

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_flush_on_aggregate)
{
  GThread *thread1, *thread2;
  ChainData data1 = { 0, };
  ChainData data2 = { 0, };
  TestData test = { 0, };

  _test_data_init (&test, FALSE);
  ((GstTestAggregator *) test.aggregator)->do_flush_on_aggregate = TRUE;
  _chain_data_init (&data1, test.aggregator, gst_buffer_new (), NULL);
  _chain_data_init (&data2, test.aggregator, gst_buffer_new (), NULL);

  thread1 = g_thread_try_new ("gst-check", push_data, &data1, NULL);
  thread2 = g_thread_try_new ("gst-check", push_data, &data2, NULL);

  g_main_loop_run (test.ml);
  g_source_remove (test.timeout_id);

  /* these will return immediately as when the data is popped the threads are
   * unlocked and will terminate */
  g_thread_join (thread1);
  g_thread_join (thread2);

  _chain_data_clear (&data1);
  _chain_data_clear (&data2);
  _test_data_clear (&test);
}

GST_END_TEST;

GST_START_TEST (test_remove_pad_on_aggregate)
{
  GThread *thread1, *thread2;
  ChainData data1 = { 0, };
  ChainData data2 = { 0, };
  TestData test = { 0, };

  _test_data_init (&test, FALSE);
  ((GstTestAggregator *) test.aggregator)->do_remove_pad_on_aggregate = TRUE;
  _chain_data_init (&data1, test.aggregator, gst_buffer_new (), NULL);
  _chain_data_init (&data2, test.aggregator, gst_buffer_new (), NULL);

  thread1 = g_thread_try_new ("gst-check", push_data, &data1, NULL);
  thread2 = g_thread_try_new ("gst-check", push_data, &data2, NULL);

  g_main_loop_run (test.ml);
  g_source_remove (test.timeout_id);

  /* these will return immediately as when the data is popped the threads are
   * unlocked and will terminate */
  g_thread_join (thread1);
  g_thread_join (thread2);

  _chain_data_clear (&data1);
  _chain_data_clear (&data2);
  _test_data_clear (&test);
}

GST_END_TEST;

GST_START_TEST (test_force_live)
{
  GstElement *agg;
  GstHarness *h;
  GstBuffer *buf;

  agg = gst_check_setup_element ("testaggregator");
  g_object_set (agg, "latency", GST_USECOND, NULL);
  gst_aggregator_set_force_live (GST_AGGREGATOR (agg), TRUE);
  h = gst_harness_new_with_element (agg, NULL, "src");

  gst_harness_play (h);

  gst_harness_crank_single_clock_wait (h);
  buf = gst_harness_pull (h);

  gst_buffer_unref (buf);
  gst_harness_teardown (h);
  gst_object_unref (agg);
}

GST_END_TEST;

static Suite *
gst_aggregator_suite (void)
{
  Suite *suite;
  TCase *general;

  gst_test_aggregator_plugin_register ();

  suite = suite_create ("GstAggregator");

  general = tcase_create ("general");
  suite_add_tcase (suite, general);
  tcase_add_test (general, test_aggregate);
  tcase_add_test (general, test_aggregate_eos);
  tcase_add_test (general, test_aggregate_gap);
  tcase_add_test (general, test_aggregate_handle_events);
  tcase_add_test (general, test_aggregate_handle_queries);
  tcase_add_test (general, test_aggregate_queries_robustness);
  tcase_add_test (general, test_flushing_seek);
  tcase_add_test (general, test_infinite_seek);
  tcase_add_test (general, test_infinite_seek_50_src);
  tcase_add_test (general, test_infinite_seek_50_src_live);
  tcase_add_test (general, test_linear_pipeline);
  tcase_add_test (general, test_two_src_pipeline);
  tcase_add_test (general, test_timeout_pipeline);
  tcase_add_test (general, test_timeout_pipeline_with_wait);
  tcase_add_test (general, test_add_remove);
  tcase_add_test (general, test_change_state_intensive);
  tcase_add_test (general, test_flush_on_aggregate);
  tcase_add_test (general, test_remove_pad_on_aggregate);
  tcase_add_test (general, test_force_live);

  return suite;
}

GST_CHECK_MAIN (gst_aggregator);
