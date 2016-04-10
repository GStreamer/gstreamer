/*
 * collectpads.c - GstCollectPads testsuite
 * Copyright (C) 2006 Alessandro Decina <alessandro.d@gmail.com>
 *
 * Authors:
 *   Alessandro Decina <alessandro.d@gmail.com>
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

#include <gst/check/gstcheck.h>
#include <gst/base/gstcollectpads.h>

/* dummy collectpads based element */

#define GST_TYPE_AGGREGATOR            (gst_aggregator_get_type ())
#define GST_AGGREGATOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_AGGREGATOR, GstAggregator))
#define GST_AGGREGATOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_AGGREGATOR, GstAggregatorClass))
#define GST_AGGREGATOR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_AGGREGATOR, GstAggregatorClass))

typedef struct _GstAggregator GstAggregator;
typedef struct _GstAggregatorClass GstAggregatorClass;

struct _GstAggregator
{
  GstElement parent;
  GstCollectPads *collect;
  GstPad *srcpad;
  GstPad *sinkpad[2];
  gint padcount;
  gboolean first;
};
struct _GstAggregatorClass
{
  GstElementClass parent_class;
};

static GType gst_aggregator_get_type (void);

G_DEFINE_TYPE (GstAggregator, gst_aggregator, GST_TYPE_ELEMENT);

static GstStaticPadTemplate gst_aggregator_src_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_aggregator_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink_%u", GST_PAD_SINK, GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

static GstFlowReturn
gst_agregator_collected (GstCollectPads * pads, gpointer user_data)
{
  GstAggregator *aggregator = GST_AGGREGATOR (user_data);
  GstBuffer *inbuf;
  GstCollectData *collect_data = NULL;
  guint outsize = 0;
  GSList *walk;

  walk = pads->data;
  for (walk = pads->data; walk; walk = walk->next) {
    GstCollectData *tmp = (GstCollectData *) walk->data;
    if (tmp->buffer) {
      collect_data = tmp;
      break;
    }
  }

  /* can only happen when no pads to collect or all EOS */
  if (collect_data == NULL)
    goto eos;

  outsize = gst_buffer_get_size (collect_data->buffer);
  inbuf = gst_collect_pads_take_buffer (pads, collect_data, outsize);
  if (!inbuf)
    goto eos;

  if (aggregator->first) {
    GstSegment segment;

    gst_segment_init (&segment, GST_FORMAT_BYTES);
    gst_pad_push_event (aggregator->srcpad,
        gst_event_new_stream_start ("test"));
    gst_pad_push_event (aggregator->srcpad, gst_event_new_segment (&segment));
    aggregator->first = FALSE;
  }

  /* just forward the first buffer */
  GST_DEBUG_OBJECT (aggregator, "forward buffer %p", inbuf);
  return gst_pad_push (aggregator->srcpad, inbuf);
  /* ERRORS */
eos:
  {
    GST_DEBUG_OBJECT (aggregator, "no data available, must be EOS");
    gst_pad_push_event (aggregator->srcpad, gst_event_new_eos ());
    return GST_FLOW_EOS;
  }
}

static GstPad *
gst_aggregator_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * unused, const GstCaps * caps)
{
  GstAggregator *aggregator = GST_AGGREGATOR (element);
  gchar *name;
  GstPad *newpad;
  gint padcount;

  if (templ->direction != GST_PAD_SINK)
    return NULL;

  /* create new pad */
  padcount = g_atomic_int_add (&aggregator->padcount, 1);
  name = g_strdup_printf ("sink_%u", padcount);
  newpad = gst_pad_new_from_template (templ, name);
  g_free (name);

  gst_collect_pads_add_pad (aggregator->collect, newpad,
      sizeof (GstCollectData), NULL, TRUE);

  /* takes ownership of the pad */
  if (!gst_element_add_pad (GST_ELEMENT (aggregator), newpad))
    goto could_not_add;

  GST_DEBUG_OBJECT (aggregator, "added new pad %s", GST_OBJECT_NAME (newpad));
  return newpad;

  /* errors */
could_not_add:
  {
    GST_DEBUG_OBJECT (aggregator, "could not add pad");
    gst_collect_pads_remove_pad (aggregator->collect, newpad);
    gst_object_unref (newpad);
    return NULL;
  }
}

static void
gst_aggregator_release_pad (GstElement * element, GstPad * pad)
{
  GstAggregator *aggregator = GST_AGGREGATOR (element);

  if (aggregator->collect)
    gst_collect_pads_remove_pad (aggregator->collect, pad);
  gst_element_remove_pad (element, pad);
}

static GstStateChangeReturn
gst_aggregator_change_state (GstElement * element, GstStateChange transition)
{
  GstAggregator *aggregator = GST_AGGREGATOR (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_collect_pads_start (aggregator->collect);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      /* need to unblock the collectpads before calling the
       * parent change_state so that streaming can finish */
      gst_collect_pads_stop (aggregator->collect);
      break;
    default:
      break;
  }

  ret =
      GST_ELEMENT_CLASS (gst_aggregator_parent_class)->change_state (element,
      transition);

  switch (transition) {
    default:
      break;
  }

  return ret;
}

static void
gst_aggregator_dispose (GObject * object)
{
  GstAggregator *aggregator = GST_AGGREGATOR (object);

  if (aggregator->collect) {
    gst_object_unref (aggregator->collect);
    aggregator->collect = NULL;
  }

  G_OBJECT_CLASS (gst_aggregator_parent_class)->dispose (object);
}

static void
gst_aggregator_class_init (GstAggregatorClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  gobject_class->dispose = gst_aggregator_dispose;

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_aggregator_src_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_aggregator_sink_template);
  gst_element_class_set_static_metadata (gstelement_class, "Aggregator",
      "Testing", "Combine N buffers", "Stefan Sauer <ensonic@users.sf.net>");

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_aggregator_request_new_pad);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_aggregator_release_pad);
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_aggregator_change_state);
}

static void
gst_aggregator_init (GstAggregator * agregator)
{
  GstPadTemplate *template;

  template = gst_static_pad_template_get (&gst_aggregator_src_template);
  agregator->srcpad = gst_pad_new_from_template (template, "src");
  gst_object_unref (template);

  GST_PAD_SET_PROXY_CAPS (agregator->srcpad);
  gst_element_add_pad (GST_ELEMENT (agregator), agregator->srcpad);

  /* keep track of the sinkpads requested */
  agregator->collect = gst_collect_pads_new ();
  gst_collect_pads_set_function (agregator->collect,
      GST_DEBUG_FUNCPTR (gst_agregator_collected), agregator);

  agregator->first = TRUE;
}

static gboolean
gst_agregator_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "aggregator", GST_RANK_NONE,
      GST_TYPE_AGGREGATOR);
}

static gboolean
gst_agregator_plugin_register (void)
{
  return gst_plugin_register_static (GST_VERSION_MAJOR,
      GST_VERSION_MINOR,
      "aggregator",
      "Combine buffers",
      gst_agregator_plugin_init,
      VERSION, GST_LICENSE, PACKAGE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
}


#define fail_unless_collected(expected)           \
G_STMT_START {                                    \
  g_mutex_lock (&lock);                           \
  while (expected == TRUE && collected == FALSE)  \
    g_cond_wait (&cond, &lock);                   \
  fail_unless_equals_int (collected, expected);   \
  g_mutex_unlock (&lock);                         \
} G_STMT_END;

typedef struct
{
  char foo;
} BadCollectData;

typedef struct
{
  GstCollectData data;
  GstPad *pad;
  GstBuffer *buffer;
  GstEvent *event;
  GstFlowReturn expected_result;
} TestData;

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstCollectPads *collect;
static gboolean collected;
static GstPad *agg_srcpad, *srcpad1, *srcpad2;
static GstPad *sinkpad1, *sinkpad2;
static TestData *data1, *data2;
static GstBuffer *outbuf1, *outbuf2;
static GstElement *agg;
gboolean fail_seek;
gint flush_start_events, flush_stop_events;

static GMutex lock;
static GCond cond;

static GstFlowReturn
collected_cb (GstCollectPads * pads, gpointer user_data)
{
  outbuf1 = gst_collect_pads_pop (pads, (GstCollectData *) data1);
  outbuf2 = gst_collect_pads_pop (pads, (GstCollectData *) data2);

  g_mutex_lock (&lock);
  collected = TRUE;
  g_cond_signal (&cond);
  g_mutex_unlock (&lock);

  return GST_FLOW_OK;
}

static GstFlowReturn
handle_buffer_cb (GstCollectPads * pads, GstCollectData * data,
    GstBuffer * buf, gpointer user_data)
{
  GST_DEBUG ("collected buffers via callback");

  outbuf1 = gst_collect_pads_pop (pads, (GstCollectData *) data1);
  outbuf2 = gst_collect_pads_pop (pads, (GstCollectData *) data2);

  g_mutex_lock (&lock);
  collected = TRUE;
  g_cond_signal (&cond);
  g_mutex_unlock (&lock);

  return GST_FLOW_OK;
}

static gpointer
push_buffer (gpointer user_data)
{
  GstFlowReturn flow;
  GstCaps *caps;
  TestData *test_data = (TestData *) user_data;
  GstSegment segment;

  gst_pad_push_event (test_data->pad, gst_event_new_stream_start ("test"));

  caps = gst_caps_new_empty_simple ("foo/x-bar");
  gst_pad_push_event (test_data->pad, gst_event_new_caps (caps));
  gst_caps_unref (caps);

  gst_segment_init (&segment, GST_FORMAT_TIME);
  gst_pad_push_event (test_data->pad, gst_event_new_segment (&segment));

  flow = gst_pad_push (test_data->pad, test_data->buffer);
  fail_unless (flow == test_data->expected_result, "got flow %s instead of OK",
      gst_flow_get_name (flow));

  return NULL;
}

static gpointer
push_event (gpointer user_data)
{
  TestData *test_data = (TestData *) user_data;

  fail_unless (gst_pad_push_event (test_data->pad, test_data->event) == TRUE);

  return NULL;
}

static void
setup_default (void)
{
  collect = gst_collect_pads_new ();

  srcpad1 = gst_pad_new_from_static_template (&srctemplate, "src1");
  srcpad2 = gst_pad_new_from_static_template (&srctemplate, "src2");
  sinkpad1 = gst_pad_new_from_static_template (&sinktemplate, "sink1");
  sinkpad2 = gst_pad_new_from_static_template (&sinktemplate, "sink2");
  fail_unless (gst_pad_link (srcpad1, sinkpad1) == GST_PAD_LINK_OK);
  fail_unless (gst_pad_link (srcpad2, sinkpad2) == GST_PAD_LINK_OK);

  gst_pad_set_active (sinkpad1, TRUE);
  gst_pad_set_active (sinkpad2, TRUE);
  gst_pad_set_active (srcpad1, TRUE);
  gst_pad_set_active (srcpad2, TRUE);

  data1 = NULL;
  data2 = NULL;
  outbuf1 = NULL;
  outbuf2 = NULL;
  collected = FALSE;
}

static void
setup (void)
{
  setup_default ();
  gst_collect_pads_set_function (collect, collected_cb, NULL);
}

static void
setup_buffer_cb (void)
{
  setup_default ();
  gst_collect_pads_set_buffer_function (collect, handle_buffer_cb, NULL);
}

static void
teardown (void)
{
  gst_object_unref (srcpad1);
  gst_object_unref (srcpad2);
  gst_object_unref (sinkpad1);
  gst_object_unref (sinkpad2);
  gst_object_unref (collect);
  srcpad1 = srcpad2 = NULL;
  sinkpad1 = sinkpad2 = NULL;
  collect = NULL;
}

GST_START_TEST (test_pad_add_remove)
{
  ASSERT_CRITICAL (gst_collect_pads_add_pad (collect, sinkpad1,
          sizeof (BadCollectData), NULL, TRUE));

  data1 = (TestData *) gst_collect_pads_add_pad (collect,
      sinkpad1, sizeof (TestData), NULL, TRUE);
  fail_unless (data1 != NULL);

  fail_unless (gst_collect_pads_remove_pad (collect, sinkpad2) == FALSE);
  fail_unless (gst_collect_pads_remove_pad (collect, sinkpad1) == TRUE);
}

GST_END_TEST;

GST_START_TEST (test_collect)
{
  GstBuffer *buf1, *buf2;
  GThread *thread1, *thread2;

  data1 = (TestData *) gst_collect_pads_add_pad (collect,
      sinkpad1, sizeof (TestData), NULL, TRUE);
  fail_unless (data1 != NULL);

  data2 = (TestData *) gst_collect_pads_add_pad (collect,
      sinkpad2, sizeof (TestData), NULL, TRUE);
  fail_unless (data2 != NULL);

  buf1 = gst_buffer_new ();
  buf2 = gst_buffer_new ();

  /* start collect pads */
  gst_collect_pads_start (collect);

  /* push buffers on the pads */
  data1->pad = srcpad1;
  data1->buffer = buf1;
  thread1 = g_thread_try_new ("gst-check", push_buffer, data1, NULL);
  /* here thread1 is blocked and srcpad1 has a queued buffer */
  fail_unless_collected (FALSE);

  data2->pad = srcpad2;
  data2->buffer = buf2;
  thread2 = g_thread_try_new ("gst-check", push_buffer, data2, NULL);

  /* now both pads have a buffer */
  fail_unless_collected (TRUE);

  fail_unless (outbuf1 == buf1);
  fail_unless (outbuf2 == buf2);

  /* these will return immediately as at this point the threads have been
   * unlocked and are finished */
  g_thread_join (thread1);
  g_thread_join (thread2);

  gst_collect_pads_stop (collect);

  gst_buffer_unref (buf1);
  gst_buffer_unref (buf2);
}

GST_END_TEST;


GST_START_TEST (test_collect_eos)
{
  GstBuffer *buf1;
  GThread *thread1, *thread2;

  data1 = (TestData *) gst_collect_pads_add_pad (collect,
      sinkpad1, sizeof (TestData), NULL, TRUE);
  fail_unless (data1 != NULL);

  data2 = (TestData *) gst_collect_pads_add_pad (collect,
      sinkpad2, sizeof (TestData), NULL, TRUE);
  fail_unless (data2 != NULL);

  buf1 = gst_buffer_new ();

  /* start collect pads */
  gst_collect_pads_start (collect);

  /* push a buffer on srcpad1 and EOS on srcpad2 */
  data1->pad = srcpad1;
  data1->buffer = buf1;
  thread1 = g_thread_try_new ("gst-check", push_buffer, data1, NULL);
  /* here thread1 is blocked and srcpad1 has a queued buffer */
  fail_unless_collected (FALSE);

  data2->pad = srcpad2;
  data2->event = gst_event_new_eos ();
  thread2 = g_thread_try_new ("gst-check", push_event, data2, NULL);
  /* now sinkpad1 has a buffer and sinkpad2 has EOS */
  fail_unless_collected (TRUE);

  fail_unless (outbuf1 == buf1);
  /* sinkpad2 has EOS so a NULL buffer is returned */
  fail_unless (outbuf2 == NULL);

  /* these will return immediately as when the data is popped the threads are
   * unlocked and will terminate */
  g_thread_join (thread1);
  g_thread_join (thread2);

  gst_collect_pads_stop (collect);

  gst_buffer_unref (buf1);
}

GST_END_TEST;

GST_START_TEST (test_collect_twice)
{
  GstBuffer *buf1, *buf2;
  GThread *thread1, *thread2;

  data1 = (TestData *) gst_collect_pads_add_pad (collect,
      sinkpad1, sizeof (TestData), NULL, TRUE);
  fail_unless (data1 != NULL);

  data2 = (TestData *) gst_collect_pads_add_pad (collect,
      sinkpad2, sizeof (TestData), NULL, TRUE);
  fail_unless (data2 != NULL);

  GST_INFO ("round 1");

  buf1 = gst_buffer_new ();

  /* start collect pads */
  gst_collect_pads_start (collect);

  /* queue a buffer */
  data1->pad = srcpad1;
  data1->buffer = buf1;
  thread1 = g_thread_try_new ("gst-check", push_buffer, data1, NULL);
  /* here thread1 is blocked and srcpad1 has a queued buffer */
  fail_unless_collected (FALSE);

  /* push EOS on the other pad */
  data2->pad = srcpad2;
  data2->event = gst_event_new_eos ();
  thread2 = g_thread_try_new ("gst-check", push_event, data2, NULL);

  /* one of the pads has a buffer, the other has EOS */
  fail_unless_collected (TRUE);

  fail_unless (outbuf1 == buf1);
  /* there's nothing to pop from the one which received EOS */
  fail_unless (outbuf2 == NULL);

  /* these will return immediately as at this point the threads have been
   * unlocked and are finished */
  g_thread_join (thread1);
  g_thread_join (thread2);

  gst_collect_pads_stop (collect);
  collected = FALSE;

  GST_INFO ("round 2");

  buf2 = gst_buffer_new ();

  /* clear EOS from pads */
  gst_pad_push_event (srcpad1, gst_event_new_flush_stop (TRUE));
  gst_pad_push_event (srcpad2, gst_event_new_flush_stop (TRUE));

  /* start collect pads */
  gst_collect_pads_start (collect);

  /* push buffers on the pads */
  data1->pad = srcpad1;
  data1->buffer = buf1;
  thread1 = g_thread_try_new ("gst-check", push_buffer, data1, NULL);
  /* here thread1 is blocked and srcpad1 has a queued buffer */
  fail_unless_collected (FALSE);

  data2->pad = srcpad2;
  data2->buffer = buf2;
  thread2 = g_thread_try_new ("gst-check", push_buffer, data2, NULL);

  /* now both pads have a buffer */
  fail_unless_collected (TRUE);

  /* these will return immediately as at this point the threads have been
   * unlocked and are finished */
  g_thread_join (thread1);
  g_thread_join (thread2);

  gst_collect_pads_stop (collect);

  gst_buffer_unref (buf1);
  gst_buffer_unref (buf2);

}

GST_END_TEST;


/* Test the default collected buffer func */
GST_START_TEST (test_collect_default)
{
  GstBuffer *buf1, *buf2;
  GThread *thread1, *thread2;

  data1 = (TestData *) gst_collect_pads_add_pad (collect,
      sinkpad1, sizeof (TestData), NULL, TRUE);
  fail_unless (data1 != NULL);

  data2 = (TestData *) gst_collect_pads_add_pad (collect,
      sinkpad2, sizeof (TestData), NULL, TRUE);
  fail_unless (data2 != NULL);

  buf1 = gst_buffer_new ();
  GST_BUFFER_TIMESTAMP (buf1) = 0;
  buf2 = gst_buffer_new ();
  GST_BUFFER_TIMESTAMP (buf2) = GST_SECOND;

  /* start collect pads */
  gst_collect_pads_start (collect);

  /* push buffers on the pads */
  data1->pad = srcpad1;
  data1->buffer = buf1;
  thread1 = g_thread_try_new ("gst-check", push_buffer, data1, NULL);
  /* here thread1 is blocked and srcpad1 has a queued buffer */
  fail_unless_collected (FALSE);

  data2->pad = srcpad2;
  data2->buffer = buf2;
  thread2 = g_thread_try_new ("gst-check", push_buffer, data2, NULL);

  /* now both pads have a buffer */
  fail_unless_collected (TRUE);

  /* The default callback should have popped the buffer with lower timestamp,
   * and this should therefore be NULL: */
  fail_unless (outbuf1 == NULL);
  /* While this one should still be pending: */
  fail_unless (outbuf2 == buf2);

  /* these will return immediately as at this point the threads have been
   * unlocked and are finished */
  g_thread_join (thread1);
  g_thread_join (thread2);

  gst_collect_pads_stop (collect);

  gst_buffer_unref (buf1);
  gst_buffer_unref (buf2);
}

GST_END_TEST;


#define NUM_BUFFERS 3
static void
handoff (GstElement * fakesink, GstBuffer * buf, GstPad * pad, guint * count)
{
  *count = *count + 1;
}

/* Test a linear pipeline using aggregator */
GST_START_TEST (test_linear_pipeline)
{
  GstElement *pipeline, *src, *agg, *sink;
  GstBus *bus;
  GstMessage *msg;
  gint count = 0;

  pipeline = gst_pipeline_new ("pipeline");
  src = gst_check_setup_element ("fakesrc");
  g_object_set (src, "num-buffers", NUM_BUFFERS, "sizetype", 2, "sizemax", 4,
      NULL);
  agg = gst_check_setup_element ("aggregator");
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

/* Test a linear pipeline using aggregator */
GST_START_TEST (test_branched_pipeline)
{
  GstElement *pipeline, *src, *tee, *queue[2], *agg, *sink;
  GstBus *bus;
  GstMessage *msg;
  gint count = 0;

  pipeline = gst_pipeline_new ("pipeline");
  src = gst_check_setup_element ("fakesrc");
  g_object_set (src, "num-buffers", NUM_BUFFERS, "sizetype", 2, "sizemax", 4,
      NULL);
  tee = gst_check_setup_element ("tee");
  queue[0] = gst_check_setup_element ("queue");
  gst_object_set_name (GST_OBJECT (queue[0]), "queue0");
  queue[1] = gst_check_setup_element ("queue");
  gst_object_set_name (GST_OBJECT (queue[1]), "queue1");
  agg = gst_check_setup_element ("aggregator");
  sink = gst_check_setup_element ("fakesink");
  g_object_set (sink, "signal-handoffs", TRUE, NULL);
  g_signal_connect (sink, "handoff", (GCallback) handoff, &count);

  fail_unless (gst_bin_add (GST_BIN (pipeline), src));
  fail_unless (gst_bin_add (GST_BIN (pipeline), tee));
  fail_unless (gst_bin_add (GST_BIN (pipeline), queue[0]));
  fail_unless (gst_bin_add (GST_BIN (pipeline), queue[1]));
  fail_unless (gst_bin_add (GST_BIN (pipeline), agg));
  fail_unless (gst_bin_add (GST_BIN (pipeline), sink));
  fail_unless (gst_element_link (src, tee));
  fail_unless (gst_element_link (tee, queue[0]));
  fail_unless (gst_element_link (tee, queue[1]));
  fail_unless (gst_element_link (queue[0], agg));
  fail_unless (gst_element_link (queue[1], agg));
  fail_unless (gst_element_link (agg, sink));

  bus = gst_element_get_bus (pipeline);
  fail_if (bus == NULL);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
  fail_if (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_EOS);
  gst_message_unref (msg);

  /* we have two branches, but we still only forward buffers from one branch */
  fail_unless_equals_int (count, NUM_BUFFERS * 2);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

GST_END_TEST;

static GstPadProbeReturn
downstream_probe_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  if (info->type & GST_PAD_PROBE_TYPE_EVENT_FLUSH) {
    if (GST_EVENT_TYPE (GST_PAD_PROBE_INFO_EVENT (info)) ==
        GST_EVENT_FLUSH_START)
      g_atomic_int_inc (&flush_start_events);
    else if (GST_EVENT_TYPE (GST_PAD_PROBE_INFO_EVENT (info)) ==
        GST_EVENT_FLUSH_STOP)
      g_atomic_int_inc (&flush_stop_events);
  } else if (info->type & GST_PAD_PROBE_TYPE_DATA_DOWNSTREAM) {
    g_mutex_lock (&lock);
    collected = TRUE;
    g_cond_signal (&cond);
    g_mutex_unlock (&lock);
  }

  return GST_PAD_PROBE_DROP;
}

static gboolean
src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean ret = TRUE;
  if (GST_EVENT_TYPE (event) == GST_EVENT_SEEK) {
    if (g_atomic_int_compare_and_exchange (&fail_seek, TRUE, FALSE) == TRUE) {
      ret = FALSE;
    }
  }

  gst_event_unref (event);
  return ret;
}

static gboolean
agg_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  return gst_collect_pads_src_event_default (GST_AGGREGATOR (parent)->collect,
      pad, event);
}

static GstPad *
setup_src_pad (GstElement * element,
    GstStaticPadTemplate * tmpl, const char *name)
{
  GstPad *srcpad, *sinkpad;

  srcpad = gst_pad_new_from_static_template (tmpl, "src");
  sinkpad = gst_element_get_request_pad (element, name);
  fail_unless (gst_pad_link (srcpad, sinkpad) == GST_PAD_LINK_OK,
      "Could not link source and %s sink pads", GST_ELEMENT_NAME (element));
  gst_pad_set_event_function (srcpad, src_event);
  gst_pad_set_active (srcpad, TRUE);
  gst_object_unref (sinkpad);

  return srcpad;
}

static void
flush_setup (void)
{
  agg = gst_check_setup_element ("aggregator");
  agg_srcpad = gst_element_get_static_pad (agg, "src");
  srcpad1 = setup_src_pad (agg, &srctemplate, "sink_0");
  srcpad2 = setup_src_pad (agg, &srctemplate, "sink_1");
  gst_pad_add_probe (agg_srcpad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM |
      GST_PAD_PROBE_TYPE_DATA_DOWNSTREAM |
      GST_PAD_PROBE_TYPE_EVENT_FLUSH, downstream_probe_cb, NULL, NULL);
  gst_pad_set_event_function (agg_srcpad, agg_src_event);
  data1 = g_new0 (TestData, 1);
  data2 = g_new0 (TestData, 1);
  g_atomic_int_set (&flush_start_events, 0);
  g_atomic_int_set (&flush_stop_events, 0);
  gst_element_set_state (agg, GST_STATE_PLAYING);
  outbuf1 = NULL;
  outbuf2 = NULL;
  collected = FALSE;
}

static void
flush_teardown (void)
{
  gst_element_set_state (agg, GST_STATE_NULL);
  gst_object_unref (agg);
  gst_object_unref (agg_srcpad);
  gst_object_unref (srcpad1);
  gst_object_unref (srcpad2);
  g_free (data1);
  g_free (data2);
  agg = NULL;
  agg_srcpad = NULL;
  srcpad1 = srcpad2 = NULL;
  data1 = data2 = NULL;
}

GST_START_TEST (test_flushing_seek_failure)
{
  GstBuffer *buf1, *buf2;
  GThread *thread1, *thread2;
  GstEvent *event;

  /* Queue a buffer in agg:sink_1. Do a flushing seek and simulate one upstream
   * element failing to handle the seek (see src_event()). Check that the
   * flushing seek logic doesn't get triggered by checking that the buffer
   * queued on agg:sink_1 doesn't get flushed.
   */

  /* queue a buffer in agg:sink_1 */
  buf2 = gst_buffer_new_allocate (NULL, 1, NULL);
  GST_BUFFER_TIMESTAMP (buf2) = GST_SECOND;
  data2->pad = srcpad2;
  data2->buffer = buf2;
  thread2 = g_thread_try_new ("gst-check", push_buffer, data2, NULL);
  fail_unless_collected (FALSE);

  /* do the seek */
  event = gst_event_new_seek (1, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
      GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_SET, 10 * GST_SECOND);
  g_atomic_int_set (&fail_seek, TRUE);
  fail_if (gst_pad_send_event (agg_srcpad, event));

  /* flush srcpad1 (pretending it's the upstream that didn't fail to seek) */
  fail_unless (gst_pad_push_event (srcpad1, gst_event_new_flush_start ()));
  fail_unless (gst_pad_push_event (srcpad1, gst_event_new_flush_stop (TRUE)));

  /* check that the flush events reached agg:src */
  fail_unless_equals_int (flush_start_events, 1);
  fail_unless_equals_int (flush_stop_events, 1);

  /* push a buffer on agg:sink_0. This should trigger a collect since agg:sink_1
   * should not have been flushed at this point */
  buf1 = gst_buffer_new_allocate (NULL, 1, NULL);
  GST_BUFFER_TIMESTAMP (buf1) = 0;
  data1->pad = srcpad1;
  data1->buffer = buf1;
  thread1 = g_thread_try_new ("gst-check", push_buffer, data1, NULL);
  fail_unless_collected (TRUE);
  collected = FALSE;

  /* at this point thread1 must have returned */
  g_thread_join (thread1);

  /* push eos on agg:sink_0 so the buffer queued in agg:sink_1 is collected and
   * the pushing thread returns */
  data1->pad = srcpad1;
  data1->event = gst_event_new_eos ();
  thread1 = g_thread_try_new ("gst-check", push_event, data1, NULL);
  fail_unless_collected (TRUE);

  g_thread_join (thread1);
  g_thread_join (thread2);
}

GST_END_TEST;

GST_START_TEST (test_flushing_seek)
{
  GstBuffer *buf1, *buf2;
  GThread *thread1, *thread2;
  GstEvent *event;

  /* Queue a buffer in agg:sink_1. Then do a flushing seek and check that the
   * new flushing seek logic is triggered. On the first FLUSH_START call the
   * buffers queued in collectpads should get flushed. Only one FLUSH_START and
   * one FLUSH_STOP should be forwarded downstream.
   */
  buf2 = gst_buffer_new_allocate (NULL, 1, NULL);
  GST_BUFFER_TIMESTAMP (buf2) = 0;
  data2->pad = srcpad2;
  data2->buffer = buf2;
  /* expect this buffer to be flushed */
  data2->expected_result = GST_FLOW_FLUSHING;
  thread2 = g_thread_try_new ("gst-check", push_buffer, data2, NULL);

  /* now do a successful flushing seek */
  event = gst_event_new_seek (1, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
      GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_SET, 10 * GST_SECOND);
  g_atomic_int_set (&fail_seek, FALSE);
  fail_unless (gst_pad_send_event (agg_srcpad, event));

  /* flushing starts once one of the upstream elements sends the first
   * FLUSH_START */
  fail_unless_equals_int (flush_start_events, 0);
  fail_unless_equals_int (flush_stop_events, 0);

  /* flush ogg:sink_0. This flushs collectpads, calls ::flush() and sends
   * FLUSH_START downstream */
  fail_unless (gst_pad_push_event (srcpad1, gst_event_new_flush_start ()));
  fail_unless_equals_int (flush_start_events, 1);
  fail_unless_equals_int (flush_stop_events, 0);
  /* the first FLUSH_STOP is forwarded downstream */
  fail_unless (gst_pad_push_event (srcpad1, gst_event_new_flush_stop (TRUE)));
  fail_unless_equals_int (flush_start_events, 1);
  fail_unless_equals_int (flush_stop_events, 1);
  /* at this point even the other pad agg:sink_1 should be flushing so thread2
   * should have stopped */
  g_thread_join (thread2);

  /* push a buffer on agg:sink_0 to trigger one collect after flushing to verify
   * that flushing completes once all the pads have been flushed */
  buf1 = gst_buffer_new_allocate (NULL, 1, NULL);
  GST_BUFFER_TIMESTAMP (buf1) = GST_SECOND;
  data1->pad = srcpad1;
  data1->buffer = buf1;
  thread1 = g_thread_try_new ("gst-check", push_buffer, data1, NULL);

  /* flush agg:sink_1 as well. This completes the flushing seek so a FLUSH_STOP is
   * sent downstream */
  gst_pad_push_event (srcpad2, gst_event_new_flush_start ());
  gst_pad_push_event (srcpad2, gst_event_new_flush_stop (TRUE));

  /* still, only one FLUSH_START and one FLUSH_STOP are forwarded downstream */
  fail_unless_equals_int (flush_start_events, 1);
  fail_unless_equals_int (flush_stop_events, 1);

  /* EOS agg:sink_1 so the buffer queued in agg:sink_0 is collected */
  data2->pad = srcpad2;
  data2->event = gst_event_new_eos ();
  thread2 = g_thread_try_new ("gst-check", push_event, data2, NULL);
  fail_unless_collected (TRUE);

  /* these will return immediately as at this point the threads have been
   * unlocked and are finished */
  g_thread_join (thread1);
  g_thread_join (thread2);
}

GST_END_TEST;

GST_START_TEST (test_clip_running_time)
{
  GstBuffer *buf;
  GstCollectData data = { 0 };

  buf = gst_buffer_new ();
  data.pad = gst_pad_new ("clip_test", GST_PAD_SRC);

  GST_BUFFER_PTS (buf) = 0;
  GST_BUFFER_DTS (buf) = 0;
  gst_segment_init (&data.segment, GST_FORMAT_TIME);

  gst_collect_pads_clip_running_time (NULL, &data, buf, &buf, NULL);

  fail_unless (buf != NULL);
  fail_unless_equals_uint64 (GST_BUFFER_PTS (buf), 0);
  fail_unless_equals_uint64 (GST_BUFFER_DTS (buf), 0);
  fail_unless_equals_int64 (GST_COLLECT_PADS_DTS (&data), 0);

  GST_BUFFER_PTS (buf) = 1000;
  GST_BUFFER_DTS (buf) = 0;
  data.segment.start = 1000;

  gst_collect_pads_clip_running_time (NULL, &data, buf, &buf, NULL);

  fail_unless (buf != NULL);
  fail_unless_equals_uint64 (GST_BUFFER_PTS (buf), 0);
  fail_unless_equals_uint64 (GST_BUFFER_DTS (buf), GST_CLOCK_TIME_NONE);
  fail_unless_equals_int64 (GST_COLLECT_PADS_DTS (&data), -1000);

  GST_BUFFER_PTS (buf) = 1000;
  GST_BUFFER_DTS (buf) = GST_CLOCK_TIME_NONE;

  gst_collect_pads_clip_running_time (NULL, &data, buf, &buf, NULL);

  fail_unless (buf != NULL);
  fail_unless_equals_uint64 (GST_BUFFER_PTS (buf), 0);
  fail_unless_equals_uint64 (GST_BUFFER_DTS (buf), GST_CLOCK_TIME_NONE);
  fail_if (GST_COLLECT_PADS_DTS_IS_VALID (&data));

  GST_BUFFER_PTS (buf) = 0;
  GST_BUFFER_DTS (buf) = 0;

  gst_collect_pads_clip_running_time (NULL, &data, buf, &buf, NULL);

  fail_unless (buf == NULL);
  gst_object_unref (data.pad);
}

GST_END_TEST;


static Suite *
gst_collect_pads_suite (void)
{
  Suite *suite;
  TCase *general, *buffers, *pipeline, *flush;

  gst_agregator_plugin_register ();

  suite = suite_create ("GstCollectPads");

  general = tcase_create ("general");
  suite_add_tcase (suite, general);
  tcase_add_checked_fixture (general, setup, teardown);
  tcase_add_test (general, test_pad_add_remove);

  tcase_add_test (general, test_collect);
  tcase_add_test (general, test_collect_eos);
  tcase_add_test (general, test_collect_twice);
  tcase_add_test (general, test_clip_running_time);

  buffers = tcase_create ("buffers");
  suite_add_tcase (suite, buffers);
  tcase_add_checked_fixture (buffers, setup_buffer_cb, teardown);
  tcase_add_test (buffers, test_collect_default);

  pipeline = tcase_create ("pipeline");
  suite_add_tcase (suite, pipeline);
  tcase_add_test (pipeline, test_linear_pipeline);
  tcase_add_test (pipeline, test_branched_pipeline);

  flush = tcase_create ("flush");
  suite_add_tcase (suite, flush);
  tcase_add_checked_fixture (flush, flush_setup, flush_teardown);
  tcase_add_test (flush, test_flushing_seek_failure);
  tcase_add_test (flush, test_flushing_seek);

  return suite;
}

GST_CHECK_MAIN (gst_collect_pads);
