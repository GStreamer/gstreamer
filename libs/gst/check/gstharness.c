/* GstHarness - A test-harness for GStreamer testing
 *
 * Copyright (C) 2012-2015 Pexip <pexip.com>
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

/**
 * SECTION:gstharness
 * @title: GstHarness
 * @short_description: A test-harness for writing GStreamer unit tests
 * @see_also: #GstTestClock,\
 *
 * #GstHarness is meant to make writing unit test for GStreamer much easier.
 * It can be thought of as a way of treating a #GstElement as a black box,
 * deterministically feeding it data, and controlling what data it outputs.
 *
 * The basic structure of #GstHarness is two "floating" #GstPads that connect
 * to the harnessed #GstElement src and sink #GstPads like so:
 *
 * |[
 *           __________________________
 *  _____   |  _____            _____  |   _____
 * |     |  | |     |          |     | |  |     |
 * | src |--+-| sink|  Element | src |-+--| sink|
 * |_____|  | |_____|          |_____| |  |_____|
 *          |__________________________|
 *
 * ]|
 *
 * With this, you can now simulate any environment the #GstElement might find
 * itself in. By specifying the #GstCaps of the harness #GstPads, using
 * functions like gst_harness_set_src_caps() or gst_harness_set_sink_caps_str(),
 * you can test how the #GstElement interacts with different caps sets.
 *
 * Your harnessed #GstElement can of course also be a bin, and using
 * gst_harness_new_parse() supporting standard gst-launch syntax, you can
 * easily test a whole pipeline instead of just one element.
 *
 * You can then go on to push #GstBuffers and #GstEvents on to the srcpad,
 * using functions like gst_harness_push() and gst_harness_push_event(), and
 * then pull them out to examine them with gst_harness_pull() and
 * gst_harness_pull_event().
 *
 * ## A simple buffer-in buffer-out example
 *
 * |[<!-- language="C" -->
 *   #include <gst/gst.h>
 *   #include <gst/check/gstharness.h>
 *   GstHarness *h;
 *   GstBuffer *in_buf;
 *   GstBuffer *out_buf;
 *
 *   // attach the harness to the src and sink pad of GstQueue
 *   h = gst_harness_new ("queue");
 *
 *   // we must specify a caps before pushing buffers
 *   gst_harness_set_src_caps_str (h, "mycaps");
 *
 *   // create a buffer of size 42
 *   in_buf = gst_harness_create_buffer (h, 42);
 *
 *   // push the buffer into the queue
 *   gst_harness_push (h, in_buf);
 *
 *   // pull the buffer from the queue
 *   out_buf = gst_harness_pull (h);
 *
 *   // validate the buffer in is the same as buffer out
 *   fail_unless (in_buf == out_buf);
 *
 *   // cleanup
 *   gst_buffer_unref (out_buf);
 *   gst_harness_teardown (h);
 *
 *   ]|
 *
 * Another main feature of the #GstHarness is its integration with the
 * #GstTestClock. Operating the #GstTestClock can be very challenging, but
 * #GstHarness simplifies some of the most desired actions a lot, like wanting
 * to manually advance the clock while at the same time releasing a #GstClockID
 * that is waiting, with functions like gst_harness_crank_single_clock_wait().
 *
 * #GstHarness also supports sub-harnesses, as a way of generating and
 * validating data. A sub-harness is another #GstHarness that is managed by
 * the "parent" harness, and can either be created by using the standard
 * gst_harness_new type functions directly on the (GstHarness *)->src_harness,
 * or using the much more convenient gst_harness_add_src() or
 * gst_harness_add_sink_parse(). If you have a decoder-element you want to test,
 * (like vp8dec) it can be very useful to add a src-harness with both a
 * src-element (videotestsrc) and an encoder (vp8enc) to feed the decoder data
 * with different configurations, by simply doing:
 *
 * |[<!-- language="C" -->
 *   GstHarness * h = gst_harness_new (h, "vp8dec");
 *   gst_harness_add_src_parse (h, "videotestsrc is-live=1 ! vp8enc", TRUE);
 * ]|
 *
 * and then feeding it data with:
 *
 * |[<!-- language="C" -->
 * gst_harness_push_from_src (h);
 * ]|
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* we have code with side effects in asserts, so make sure they are active */
#ifdef G_DISABLE_ASSERT
#error "GstHarness must be compiled with G_DISABLE_ASSERT undefined"
#endif

#include "gstharness.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

static void gst_harness_stress_free (GstHarnessThread * t);

#define HARNESS_KEY "harness"
#define HARNESS_REF "harness-ref"
#define HARNESS_LOCK(h) g_mutex_lock (&(h)->priv->priv_mutex)
#define HARNESS_UNLOCK(h) g_mutex_unlock (&(h)->priv->priv_mutex)

static GstStaticPadTemplate hsrctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);
static GstStaticPadTemplate hsinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

struct _GstHarnessPrivate
{
  gchar *element_sinkpad_name;
  gchar *element_srcpad_name;

  GstCaps *src_caps;
  GstCaps *sink_caps;

  gboolean forwarding;
  GstPad *sink_forward_pad;
  GstTestClock *testclock;

  volatile gint recv_buffers;
  volatile gint recv_events;
  volatile gint recv_upstream_events;

  GAsyncQueue *buffer_queue;
  GAsyncQueue *src_event_queue;
  GAsyncQueue *sink_event_queue;

  GstClockTime latency_min;
  GstClockTime latency_max;
  gboolean has_clock_wait;
  gboolean drop_buffers;
  GstClockTime last_push_ts;

  GstBufferPool *pool;
  GstAllocator *allocator;
  GstAllocationParams allocation_params;
  GstAllocator *propose_allocator;
  GstAllocationParams propose_allocation_params;

  gboolean blocking_push_mode;
  GCond blocking_push_cond;
  GMutex blocking_push_mutex;
  GMutex priv_mutex;

  GPtrArray *stress;
};

static GstFlowReturn
gst_harness_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstHarness *h = g_object_get_data (G_OBJECT (pad), HARNESS_KEY);
  GstHarnessPrivate *priv = h->priv;
  (void) parent;
  g_assert (h != NULL);
  g_mutex_lock (&priv->blocking_push_mutex);
  g_atomic_int_inc (&priv->recv_buffers);

  if (priv->drop_buffers)
    gst_buffer_unref (buffer);
  else
    g_async_queue_push (priv->buffer_queue, buffer);

  if (priv->blocking_push_mode) {
    g_cond_wait (&priv->blocking_push_cond, &priv->blocking_push_mutex);
  }
  g_mutex_unlock (&priv->blocking_push_mutex);

  return GST_FLOW_OK;
}

static gboolean
gst_harness_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstHarness *h = g_object_get_data (G_OBJECT (pad), HARNESS_KEY);
  GstHarnessPrivate *priv = h->priv;
  (void) parent;
  g_assert (h != NULL);
  g_atomic_int_inc (&priv->recv_upstream_events);
  g_async_queue_push (priv->src_event_queue, event);
  return TRUE;
}

static gboolean
gst_harness_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstHarness *h = g_object_get_data (G_OBJECT (pad), HARNESS_KEY);
  GstHarnessPrivate *priv = h->priv;
  gboolean ret = TRUE;
  gboolean forward;

  g_assert (h != NULL);
  (void) parent;
  g_atomic_int_inc (&priv->recv_events);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_STREAM_START:
    case GST_EVENT_CAPS:
    case GST_EVENT_SEGMENT:
      forward = TRUE;
      break;
    default:
      forward = FALSE;
      break;
  }

  HARNESS_LOCK (h);
  if (priv->forwarding && forward && priv->sink_forward_pad) {
    GstPad *fwdpad = gst_object_ref (priv->sink_forward_pad);
    HARNESS_UNLOCK (h);
    ret = gst_pad_push_event (fwdpad, event);
    gst_object_unref (fwdpad);
    HARNESS_LOCK (h);
  } else {
    g_async_queue_push (priv->sink_event_queue, event);
  }
  HARNESS_UNLOCK (h);

  return ret;
}

static void
gst_harness_decide_allocation (GstHarness * h, GstCaps * caps)
{
  GstHarnessPrivate *priv = h->priv;
  GstQuery *query;
  GstAllocator *allocator;
  GstAllocationParams params;
  GstBufferPool *pool = NULL;
  guint size, min, max;

  query = gst_query_new_allocation (caps, FALSE);
  gst_pad_peer_query (h->srcpad, query);

  if (gst_query_get_n_allocation_params (query) > 0) {
    gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);
  } else {
    allocator = NULL;
    gst_allocation_params_init (&params);
  }

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
#if 0
    /* Most elements create their own pools if pool == NULL. Not sure if we
     * want to do that in the harness since we may want to test the pool
     * implementation of the elements. Not creating a pool will however ignore
     * the returned size. */
    if (pool == NULL)
      pool = gst_buffer_pool_new ();
#endif
  } else {
    pool = NULL;
    size = min = max = 0;
  }
  gst_query_unref (query);

  if (pool) {
    GstStructure *config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_set_params (config, caps, size, min, max);
    gst_buffer_pool_config_set_allocator (config, allocator, &params);
    gst_buffer_pool_set_config (pool, config);
  }

  if (pool != priv->pool) {
    if (priv->pool != NULL)
      gst_buffer_pool_set_active (priv->pool, FALSE);
    if (pool)
      gst_buffer_pool_set_active (pool, TRUE);
  }

  priv->allocation_params = params;
  if (priv->allocator)
    gst_object_unref (priv->allocator);
  priv->allocator = allocator;
  if (priv->pool)
    gst_object_unref (priv->pool);
  priv->pool = pool;
}

static void
gst_harness_negotiate (GstHarness * h)
{
  GstCaps *caps;

  caps = gst_pad_get_current_caps (h->srcpad);
  if (caps != NULL) {
    gst_harness_decide_allocation (h, caps);
    gst_caps_unref (caps);
  } else {
    GST_FIXME_OBJECT (h, "Cannot negotiate allocation because caps is not set");
  }
}

static gboolean
gst_harness_sink_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstHarness *h = g_object_get_data (G_OBJECT (pad), HARNESS_KEY);
  GstHarnessPrivate *priv = h->priv;
  gboolean res = TRUE;
  g_assert (h != NULL);

  // FIXME: forward all queries?

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
      gst_query_set_latency (query, TRUE, priv->latency_min, priv->latency_max);
      break;
    case GST_QUERY_CAPS:
    {
      GstCaps *caps, *filter = NULL;

      if (priv->sink_caps) {
        caps = gst_caps_ref (priv->sink_caps);
      } else {
        caps = gst_pad_get_pad_template_caps (pad);
      }

      gst_query_parse_caps (query, &filter);
      if (filter != NULL) {
        gst_caps_take (&caps,
            gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST));
      }

      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
    }
      break;
    case GST_QUERY_ALLOCATION:
    {
      HARNESS_LOCK (h);
      if (priv->forwarding && priv->sink_forward_pad != NULL) {
        GstPad *peer = gst_pad_get_peer (priv->sink_forward_pad);
        g_assert (peer != NULL);
        HARNESS_UNLOCK (h);
        res = gst_pad_query (peer, query);
        gst_object_unref (peer);
        HARNESS_LOCK (h);
      } else {
        GstCaps *caps;
        gboolean need_pool;
        guint size;

        gst_query_parse_allocation (query, &caps, &need_pool);

        /* FIXME: Can this be removed? */
        size = gst_query_get_n_allocation_params (query);
        g_assert_cmpuint (0, ==, size);
        gst_query_add_allocation_param (query,
            priv->propose_allocator, &priv->propose_allocation_params);

        GST_DEBUG_OBJECT (pad, "proposing allocation %" GST_PTR_FORMAT,
            priv->propose_allocator);
      }
      HARNESS_UNLOCK (h);
      break;
    }
    default:
      res = gst_pad_query_default (pad, parent, query);
  }

  return res;
}

static gboolean
gst_harness_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstHarness *h = g_object_get_data (G_OBJECT (pad), HARNESS_KEY);
  GstHarnessPrivate *priv = h->priv;
  gboolean res = TRUE;
  g_assert (h != NULL);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
      gst_query_set_latency (query, TRUE, priv->latency_min, priv->latency_max);
      break;
    case GST_QUERY_CAPS:
    {
      GstCaps *caps, *filter = NULL;

      if (priv->src_caps) {
        caps = gst_caps_ref (priv->src_caps);
      } else {
        caps = gst_pad_get_pad_template_caps (pad);
      }

      gst_query_parse_caps (query, &filter);
      if (filter != NULL) {
        gst_caps_take (&caps,
            gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST));
      }

      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
    }
      break;
    default:
      res = gst_pad_query_default (pad, parent, query);
  }
  return res;
}

static void
gst_harness_element_ref (GstHarness * h)
{
  guint *data;

  GST_OBJECT_LOCK (h->element);
  data = g_object_get_data (G_OBJECT (h->element), HARNESS_REF);
  if (data == NULL) {
    data = g_new0 (guint, 1);
    *data = 1;
    g_object_set_data_full (G_OBJECT (h->element), HARNESS_REF, data, g_free);
  } else {
    (*data)++;
  }
  GST_OBJECT_UNLOCK (h->element);
}

static guint
gst_harness_element_unref (GstHarness * h)
{
  guint *data;
  guint ret;

  GST_OBJECT_LOCK (h->element);
  data = g_object_get_data (G_OBJECT (h->element), HARNESS_REF);
  g_assert (data != NULL);
  (*data)--;
  ret = *data;
  GST_OBJECT_UNLOCK (h->element);

  return ret;
}

static void
gst_harness_link_element_srcpad (GstHarness * h,
    const gchar * element_srcpad_name)
{
  GstHarnessPrivate *priv = h->priv;
  GstPad *srcpad = gst_element_get_static_pad (h->element,
      element_srcpad_name);
  GstPadLinkReturn link;
  if (srcpad == NULL)
    srcpad = gst_element_get_request_pad (h->element, element_srcpad_name);
  g_assert (srcpad);
  link = gst_pad_link (srcpad, h->sinkpad);
  g_assert_cmpint (link, ==, GST_PAD_LINK_OK);
  g_free (priv->element_srcpad_name);
  priv->element_srcpad_name = gst_pad_get_name (srcpad);

  gst_object_unref (srcpad);
}

static void
gst_harness_link_element_sinkpad (GstHarness * h,
    const gchar * element_sinkpad_name)
{
  GstHarnessPrivate *priv = h->priv;
  GstPad *sinkpad = gst_element_get_static_pad (h->element,
      element_sinkpad_name);
  GstPadLinkReturn link;
  if (sinkpad == NULL)
    sinkpad = gst_element_get_request_pad (h->element, element_sinkpad_name);
  g_assert (sinkpad);
  link = gst_pad_link (h->srcpad, sinkpad);
  g_assert_cmpint (link, ==, GST_PAD_LINK_OK);
  g_free (priv->element_sinkpad_name);
  priv->element_sinkpad_name = gst_pad_get_name (sinkpad);

  gst_object_unref (sinkpad);
}

static void
gst_harness_setup_src_pad (GstHarness * h,
    GstStaticPadTemplate * src_tmpl, const gchar * element_sinkpad_name)
{
  GstHarnessPrivate *priv = h->priv;
  g_assert (src_tmpl);
  g_assert (h->srcpad == NULL);

  priv->src_event_queue =
      g_async_queue_new_full ((GDestroyNotify) gst_event_unref);

  /* sending pad */
  h->srcpad = gst_pad_new_from_static_template (src_tmpl, "src");
  g_assert (h->srcpad);
  g_object_set_data (G_OBJECT (h->srcpad), HARNESS_KEY, h);

  gst_pad_set_query_function (h->srcpad, gst_harness_src_query);
  gst_pad_set_event_function (h->srcpad, gst_harness_src_event);

  gst_pad_set_active (h->srcpad, TRUE);

  if (element_sinkpad_name)
    gst_harness_link_element_sinkpad (h, element_sinkpad_name);
}

static void
gst_harness_setup_sink_pad (GstHarness * h,
    GstStaticPadTemplate * sink_tmpl, const gchar * element_srcpad_name)
{
  GstHarnessPrivate *priv = h->priv;
  g_assert (sink_tmpl);
  g_assert (h->sinkpad == NULL);

  priv->buffer_queue = g_async_queue_new_full (
      (GDestroyNotify) gst_buffer_unref);
  priv->sink_event_queue = g_async_queue_new_full (
      (GDestroyNotify) gst_event_unref);

  /* receiving pad */
  h->sinkpad = gst_pad_new_from_static_template (sink_tmpl, "sink");
  g_assert (h->sinkpad);
  g_object_set_data (G_OBJECT (h->sinkpad), HARNESS_KEY, h);

  gst_pad_set_chain_function (h->sinkpad, gst_harness_chain);
  gst_pad_set_query_function (h->sinkpad, gst_harness_sink_query);
  gst_pad_set_event_function (h->sinkpad, gst_harness_sink_event);

  gst_pad_set_active (h->sinkpad, TRUE);

  if (element_srcpad_name)
    gst_harness_link_element_srcpad (h, element_srcpad_name);
}

static void
check_element_type (GstElement * element, gboolean * has_sinkpad,
    gboolean * has_srcpad)
{
  GstElementClass *element_class = GST_ELEMENT_GET_CLASS (element);
  const GList *tmpl_list;

  *has_srcpad = element->numsrcpads > 0;
  *has_sinkpad = element->numsinkpads > 0;

  tmpl_list = gst_element_class_get_pad_template_list (element_class);

  while (tmpl_list) {
    GstPadTemplate *pad_tmpl = (GstPadTemplate *) tmpl_list->data;
    tmpl_list = g_list_next (tmpl_list);
    if (GST_PAD_TEMPLATE_DIRECTION (pad_tmpl) == GST_PAD_SRC)
      *has_srcpad |= TRUE;
    if (GST_PAD_TEMPLATE_DIRECTION (pad_tmpl) == GST_PAD_SINK)
      *has_sinkpad |= TRUE;
  }
}

static void
turn_async_and_sync_off (GstElement * element)
{
  GObjectClass *class = G_OBJECT_GET_CLASS (element);
  if (g_object_class_find_property (class, "async"))
    g_object_set (element, "async", FALSE, NULL);
  if (g_object_class_find_property (class, "sync"))
    g_object_set (element, "sync", FALSE, NULL);
}

static gboolean
gst_pad_is_request_pad (GstPad * pad)
{
  GstPadTemplate *temp;
  gboolean is_request;

  if (pad == NULL)
    return FALSE;
  temp = gst_pad_get_pad_template (pad);
  if (temp == NULL)
    return FALSE;
  is_request = GST_PAD_TEMPLATE_PRESENCE (temp) == GST_PAD_REQUEST;
  gst_object_unref (temp);
  return is_request;
}

/**
 * gst_harness_new_empty: (skip)
 *
 * Creates a new empty harness. Use gst_harness_add_element_full() to add
 * an #GstElement to it.
 *
 * MT safe.
 *
 * Returns: (transfer full): a #GstHarness, or %NULL if the harness could
 * not be created
 *
 * Since: 1.8
 */
GstHarness *
gst_harness_new_empty (void)
{
  GstHarness *h;
  GstHarnessPrivate *priv;

  h = g_new0 (GstHarness, 1);
  g_assert (h != NULL);
  h->priv = g_new0 (GstHarnessPrivate, 1);
  priv = h->priv;

  GST_DEBUG_OBJECT (h, "about to create new harness %p", h);
  priv->last_push_ts = GST_CLOCK_TIME_NONE;
  priv->latency_min = 0;
  priv->latency_max = GST_CLOCK_TIME_NONE;
  priv->drop_buffers = FALSE;
  priv->testclock = GST_TEST_CLOCK_CAST (gst_test_clock_new ());

  priv->propose_allocator = NULL;
  gst_allocation_params_init (&priv->propose_allocation_params);

  g_mutex_init (&priv->blocking_push_mutex);
  g_cond_init (&priv->blocking_push_cond);
  g_mutex_init (&priv->priv_mutex);

  priv->stress = g_ptr_array_new_with_free_func (
      (GDestroyNotify) gst_harness_stress_free);

  /* we have forwarding on as a default */
  gst_harness_set_forwarding (h, TRUE);

  return h;
}

/**
 * gst_harness_add_element_full: (skip)
 * @h: a #GstHarness
 * @element: a #GstElement to add to the harness (transfer none)
 * @hsrc: (allow-none): a #GstStaticPadTemplate describing the harness srcpad.
 * %NULL will not create a harness srcpad.
 * @element_sinkpad_name: (allow-none): a #gchar with the name of the element
 * sinkpad that is then linked to the harness srcpad. Can be a static or request
 * or a sometimes pad that has been added. %NULL will not get/request a sinkpad
 * from the element. (Like if the element is a src.)
 * @hsink: (allow-none): a #GstStaticPadTemplate describing the harness sinkpad.
 * %NULL will not create a harness sinkpad.
 * @element_srcpad_name: (allow-none): a #gchar with the name of the element
 * srcpad that is then linked to the harness sinkpad, similar to the
 * @element_sinkpad_name.
 *
 * Adds a #GstElement to an empty #GstHarness
 *
 * MT safe.
 *
 * Since: 1.6
 */
void
gst_harness_add_element_full (GstHarness * h, GstElement * element,
    GstStaticPadTemplate * hsrc, const gchar * element_sinkpad_name,
    GstStaticPadTemplate * hsink, const gchar * element_srcpad_name)
{
  GstClock *element_clock;
  gboolean has_sinkpad, has_srcpad;

  g_return_if_fail (element != NULL);
  g_return_if_fail (h->element == NULL);

  element_clock = GST_ELEMENT_CLOCK (element);
  h->element = gst_object_ref (element);
  check_element_type (element, &has_sinkpad, &has_srcpad);

  /* setup the loose srcpad linked to the element sinkpad */
  if (has_sinkpad)
    gst_harness_setup_src_pad (h, hsrc, element_sinkpad_name);

  /* setup the loose sinkpad linked to the element srcpad */
  if (has_srcpad)
    gst_harness_setup_sink_pad (h, hsink, element_srcpad_name);

  /* as a harness sink, we should not need sync and async */
  if (has_sinkpad && !has_srcpad)
    turn_async_and_sync_off (h->element);

  if (h->srcpad != NULL) {
    gboolean handled;
    gchar *stream_id = g_strdup_printf ("%s-%p",
        GST_OBJECT_NAME (h->element), h);
    handled = gst_pad_push_event (h->srcpad,
        gst_event_new_stream_start (stream_id));
    g_assert (handled);
    g_free (stream_id);
  }

  /* if the element already has a testclock attached,
     we replace our own with it, if no clock we attach the testclock */
  if (element_clock) {
    if (GST_IS_TEST_CLOCK (element_clock)) {
      gst_object_replace ((GstObject **) & h->priv->testclock,
          (GstObject *) GST_ELEMENT_CLOCK (element));
    }
  } else {
    gst_harness_use_testclock (h);
  }

  /* don't start sources, they start producing data! */
  if (has_sinkpad)
    gst_harness_play (h);

  gst_harness_element_ref (h);

  GST_DEBUG_OBJECT (h, "added element to harness %p "
      "with element_srcpad_name (%p, %s, %s) and element_sinkpad_name (%p, %s, %s)",
      h, h->srcpad, GST_DEBUG_PAD_NAME (h->srcpad),
      h->sinkpad, GST_DEBUG_PAD_NAME (h->sinkpad));
}

/**
 * gst_harness_new_full: (skip)
 * @element: a #GstElement to attach the harness to (transfer none)
 * @hsrc: (allow-none): a #GstStaticPadTemplate describing the harness srcpad.
 * %NULL will not create a harness srcpad.
 * @element_sinkpad_name: (allow-none): a #gchar with the name of the element
 * sinkpad that is then linked to the harness srcpad. Can be a static or request
 * or a sometimes pad that has been added. %NULL will not get/request a sinkpad
 * from the element. (Like if the element is a src.)
 * @hsink: (allow-none): a #GstStaticPadTemplate describing the harness sinkpad.
 * %NULL will not create a harness sinkpad.
 * @element_srcpad_name: (allow-none): a #gchar with the name of the element
 * srcpad that is then linked to the harness sinkpad, similar to the
 * @element_sinkpad_name.
 *
 * Creates a new harness.
 *
 * MT safe.
 *
 * Returns: (transfer full): a #GstHarness, or %NULL if the harness could
 * not be created
 *
 * Since: 1.6
 */
GstHarness *
gst_harness_new_full (GstElement * element,
    GstStaticPadTemplate * hsrc, const gchar * element_sinkpad_name,
    GstStaticPadTemplate * hsink, const gchar * element_srcpad_name)
{
  GstHarness *h;
  h = gst_harness_new_empty ();
  gst_harness_add_element_full (h, element,
      hsrc, element_sinkpad_name, hsink, element_srcpad_name);
  return h;
}

/**
 * gst_harness_new_with_element: (skip)
 * @element: a #GstElement to attach the harness to (transfer none)
 * @element_sinkpad_name: (allow-none): a #gchar with the name of the element
 * sinkpad that is then linked to the harness srcpad. %NULL does not attach a
 * sinkpad
 * @element_srcpad_name: (allow-none): a #gchar with the name of the element
 * srcpad that is then linked to the harness sinkpad. %NULL does not attach a
 * srcpad
 *
 * Creates a new harness. Works in the same way as gst_harness_new_full(), only
 * that generic padtemplates are used for the harness src and sinkpads, which
 * will be sufficient in most usecases.
 *
 * MT safe.
 *
 * Returns: (transfer full): a #GstHarness, or %NULL if the harness could
 * not be created
 *
 * Since: 1.6
 */
GstHarness *
gst_harness_new_with_element (GstElement * element,
    const gchar * element_sinkpad_name, const gchar * element_srcpad_name)
{
  return gst_harness_new_full (element,
      &hsrctemplate, element_sinkpad_name, &hsinktemplate, element_srcpad_name);
}

/**
 * gst_harness_new_with_padnames: (skip)
 * @element_name: a #gchar describing the #GstElement name
 * @element_sinkpad_name: (allow-none): a #gchar with the name of the element
 * sinkpad that is then linked to the harness srcpad. %NULL does not attach a
 * sinkpad
 * @element_srcpad_name: (allow-none): a #gchar with the name of the element
 * srcpad that is then linked to the harness sinkpad. %NULL does not attach a
 * srcpad
 *
 * Creates a new harness. Works like gst_harness_new_with_element(),
 * except you specify the factoryname of the #GstElement
 *
 * MT safe.
 *
 * Returns: (transfer full): a #GstHarness, or %NULL if the harness could
 * not be created
 *
 * Since: 1.6
 */
GstHarness *
gst_harness_new_with_padnames (const gchar * element_name,
    const gchar * element_sinkpad_name, const gchar * element_srcpad_name)
{
  GstHarness *h;
  GstElement *element = gst_element_factory_make (element_name, NULL);
  g_assert (element != NULL);

  h = gst_harness_new_with_element (element, element_sinkpad_name,
      element_srcpad_name);
  gst_object_unref (element);
  return h;
}

/**
 * gst_harness_new_with_templates: (skip)
 * @element_name: a #gchar describing the #GstElement name
 * @hsrc: (allow-none): a #GstStaticPadTemplate describing the harness srcpad.
 * %NULL will not create a harness srcpad.
 * @hsink: (allow-none): a #GstStaticPadTemplate describing the harness sinkpad.
 * %NULL will not create a harness sinkpad.
 *
 * Creates a new harness, like gst_harness_new_full(), except it
 * assumes the #GstElement sinkpad is named "sink" and srcpad is named "src"
 *
 * MT safe.
 *
 * Returns: (transfer full): a #GstHarness, or %NULL if the harness could
 * not be created
 *
 * Since: 1.6
 */
GstHarness *
gst_harness_new_with_templates (const gchar * element_name,
    GstStaticPadTemplate * hsrc, GstStaticPadTemplate * hsink)
{
  GstHarness *h;
  GstElement *element = gst_element_factory_make (element_name, NULL);
  g_assert (element != NULL);

  h = gst_harness_new_full (element, hsrc, "sink", hsink, "src");
  gst_object_unref (element);
  return h;
}

/**
 * gst_harness_new: (skip)
 * @element_name: a #gchar describing the #GstElement name
 *
 * Creates a new harness. Works like gst_harness_new_with_padnames(), except it
 * assumes the #GstElement sinkpad is named "sink" and srcpad is named "src"
 *
 * MT safe.
 *
 * Returns: (transfer full): a #GstHarness, or %NULL if the harness could
 * not be created
 *
 * Since: 1.6
 */
GstHarness *
gst_harness_new (const gchar * element_name)
{
  return gst_harness_new_with_padnames (element_name, "sink", "src");
}

/**
 * gst_harness_add_parse: (skip)
 * @h: a #GstHarness
 * @launchline: a #gchar describing a gst-launch type line
 *
 * Parses the @launchline and puts that in a #GstBin,
 * and then attches the supplied #GstHarness to the bin.
 *
 * MT safe.
 *
 * Since: 1.6
 */
void
gst_harness_add_parse (GstHarness * h, const gchar * launchline)
{
  GstBin *bin;
  gchar *desc;
  GstPad *pad;
  GstIterator *iter;
  gboolean done = FALSE;

  g_return_if_fail (launchline != NULL);

  desc = g_strdup_printf ("bin.( %s )", launchline);
  bin =
      (GstBin *) gst_parse_launch_full (desc, NULL, GST_PARSE_FLAG_NONE, NULL);
  g_free (desc);

  if (G_UNLIKELY (bin == NULL))
    return;

  /* find pads and ghost them if necessary */
  if ((pad = gst_bin_find_unlinked_pad (bin, GST_PAD_SRC)) != NULL) {
    gst_element_add_pad (GST_ELEMENT (bin), gst_ghost_pad_new ("src", pad));
    gst_object_unref (pad);
  }
  if ((pad = gst_bin_find_unlinked_pad (bin, GST_PAD_SINK)) != NULL) {
    gst_element_add_pad (GST_ELEMENT (bin), gst_ghost_pad_new ("sink", pad));
    gst_object_unref (pad);
  }

  iter = gst_bin_iterate_sinks (bin);
  while (!done) {
    GValue item = { 0, };

    switch (gst_iterator_next (iter, &item)) {
      case GST_ITERATOR_OK:
        turn_async_and_sync_off (GST_ELEMENT (g_value_get_object (&item)));
        g_value_reset (&item);
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        break;
      case GST_ITERATOR_ERROR:
        gst_object_unref (bin);
        gst_iterator_free (iter);
        g_return_if_reached ();
        break;
    }
  }
  gst_iterator_free (iter);

  gst_harness_add_element_full (h, GST_ELEMENT_CAST (bin),
      &hsrctemplate, "sink", &hsinktemplate, "src");
  gst_object_unref (bin);
}

/**
 * gst_harness_new_parse: (skip)
 * @launchline: a #gchar describing a gst-launch type line
 *
 * Creates a new harness, parsing the @launchline and putting that in a #GstBin,
 * and then attches the harness to the bin.
 *
 * MT safe.
 *
 * Returns: (transfer full): a #GstHarness, or %NULL if the harness could
 * not be created
 *
 * Since: 1.6
 */
GstHarness *
gst_harness_new_parse (const gchar * launchline)
{
  GstHarness *h;
  h = gst_harness_new_empty ();
  gst_harness_add_parse (h, launchline);
  return h;
}

/**
 * gst_harness_teardown:
 * @h: a #GstHarness
 *
 * Tears down a @GstHarness, freeing all resources allocated using it.
 *
 * MT safe.
 *
 * Since: 1.6
 */
void
gst_harness_teardown (GstHarness * h)
{
  GstHarnessPrivate *priv = h->priv;

  if (priv->blocking_push_mode) {
    g_mutex_lock (&priv->blocking_push_mutex);
    priv->blocking_push_mode = FALSE;
    g_cond_signal (&priv->blocking_push_cond);
    g_mutex_unlock (&priv->blocking_push_mutex);
  }

  if (h->src_harness) {
    gst_harness_teardown (h->src_harness);
  }

  gst_object_replace ((GstObject **) & priv->sink_forward_pad, NULL);
  if (h->sink_harness) {
    gst_harness_teardown (h->sink_harness);
  }

  if (priv->src_caps)
    gst_caps_unref (priv->src_caps);

  if (priv->sink_caps)
    gst_caps_unref (priv->sink_caps);

  if (h->srcpad) {
    if (gst_pad_is_request_pad (GST_PAD_PEER (h->srcpad)))
      gst_element_release_request_pad (h->element, GST_PAD_PEER (h->srcpad));
    g_free (priv->element_sinkpad_name);

    gst_pad_set_active (h->srcpad, FALSE);
    gst_object_unref (h->srcpad);

    g_async_queue_unref (priv->src_event_queue);
  }

  if (h->sinkpad) {
    if (gst_pad_is_request_pad (GST_PAD_PEER (h->sinkpad)))
      gst_element_release_request_pad (h->element, GST_PAD_PEER (h->sinkpad));
    g_free (priv->element_srcpad_name);

    gst_pad_set_active (h->sinkpad, FALSE);
    gst_object_unref (h->sinkpad);

    g_async_queue_unref (priv->buffer_queue);
    g_async_queue_unref (priv->sink_event_queue);
  }

  gst_object_replace ((GstObject **) & priv->propose_allocator, NULL);
  gst_object_replace ((GstObject **) & priv->allocator, NULL);
  gst_object_replace ((GstObject **) & priv->pool, NULL);

  /* if we hold the last ref, set to NULL */
  if (gst_harness_element_unref (h) == 0) {
    gboolean state_change;
    GstState state, pending;
    state_change = gst_element_set_state (h->element, GST_STATE_NULL);
    g_assert (state_change == GST_STATE_CHANGE_SUCCESS);
    state_change = gst_element_get_state (h->element, &state, &pending, 0);
    g_assert (state_change == GST_STATE_CHANGE_SUCCESS);
    g_assert (state == GST_STATE_NULL);
  }

  g_cond_clear (&priv->blocking_push_cond);
  g_mutex_clear (&priv->blocking_push_mutex);
  g_mutex_clear (&priv->priv_mutex);

  g_ptr_array_unref (priv->stress);

  gst_object_unref (h->element);

  gst_object_replace ((GstObject **) & priv->testclock, NULL);

  g_free (h->priv);
  g_free (h);
}

/**
 * gst_harness_add_element_src_pad:
 * @h: a #GstHarness
 * @srcpad: a #GstPad to link to the harness sinkpad
 *
 * Links the specifed #GstPad the @GstHarness sinkpad. This can be useful if
 * perhaps the srcpad did not exist at the time of creating the harness,
 * like a demuxer that provides a sometimes-pad after receiving data.
 *
 * MT safe.
 *
 * Since: 1.6
 */
void
gst_harness_add_element_src_pad (GstHarness * h, GstPad * srcpad)
{
  GstHarnessPrivate *priv = h->priv;
  GstPadLinkReturn link;
  if (h->sinkpad == NULL)
    gst_harness_setup_sink_pad (h, &hsinktemplate, NULL);
  link = gst_pad_link (srcpad, h->sinkpad);
  g_assert_cmpint (link, ==, GST_PAD_LINK_OK);
  g_free (priv->element_srcpad_name);
  priv->element_srcpad_name = gst_pad_get_name (srcpad);
}

/**
 * gst_harness_add_element_sink_pad:
 * @h: a #GstHarness
 * @sinkpad: a #GstPad to link to the harness srcpad
 *
 * Links the specifed #GstPad the @GstHarness srcpad.
 *
 * MT safe.
 *
 * Since: 1.6
 */
void
gst_harness_add_element_sink_pad (GstHarness * h, GstPad * sinkpad)
{
  GstHarnessPrivate *priv = h->priv;
  GstPadLinkReturn link;
  if (h->srcpad == NULL)
    gst_harness_setup_src_pad (h, &hsrctemplate, NULL);
  link = gst_pad_link (h->srcpad, sinkpad);
  g_assert_cmpint (link, ==, GST_PAD_LINK_OK);
  g_free (priv->element_sinkpad_name);
  priv->element_sinkpad_name = gst_pad_get_name (sinkpad);
}

/**
 * gst_harness_set_src_caps:
 * @h: a #GstHarness
 * @caps: (transfer full): a #GstCaps to set on the harness srcpad
 *
 * Sets the @GstHarness srcpad caps. This must be done before any buffers
 * can legally be pushed from the harness to the element.
 *
 * MT safe.
 *
 * Since: 1.6
 */
void
gst_harness_set_src_caps (GstHarness * h, GstCaps * caps)
{
  GstHarnessPrivate *priv = h->priv;
  GstSegment segment;
  gboolean handled;

  handled = gst_pad_push_event (h->srcpad, gst_event_new_caps (caps));
  g_assert (handled);
  gst_caps_take (&priv->src_caps, caps);

  gst_segment_init (&segment, GST_FORMAT_TIME);
  handled = gst_pad_push_event (h->srcpad, gst_event_new_segment (&segment));
}

/**
 * gst_harness_set_sink_caps:
 * @h: a #GstHarness
 * @caps: (transfer full): a #GstCaps to set on the harness sinkpad
 *
 * Sets the @GstHarness sinkpad caps.
 *
 * MT safe.
 *
 * Since: 1.6
 */
void
gst_harness_set_sink_caps (GstHarness * h, GstCaps * caps)
{
  GstHarnessPrivate *priv = h->priv;

  gst_caps_take (&priv->sink_caps, caps);
  gst_pad_push_event (h->sinkpad, gst_event_new_reconfigure ());
}

/**
 * gst_harness_set_caps:
 * @h: a #GstHarness
 * @in: (transfer full): a #GstCaps to set on the harness srcpad
 * @out: (transfer full): a #GstCaps to set on the harness sinkpad
 *
 * Sets the @GstHarness srcpad and sinkpad caps.
 *
 * MT safe.
 *
 * Since: 1.6
 */
void
gst_harness_set_caps (GstHarness * h, GstCaps * in, GstCaps * out)
{
  gst_harness_set_sink_caps (h, out);
  gst_harness_set_src_caps (h, in);
}

/**
 * gst_harness_set_src_caps_str:
 * @h: a #GstHarness
 * @str: a @gchar describing a #GstCaps to set on the harness srcpad
 *
 * Sets the @GstHarness srcpad caps using a string. This must be done before
 * any buffers can legally be pushed from the harness to the element.
 *
 * MT safe.
 *
 * Since: 1.6
 */
void
gst_harness_set_src_caps_str (GstHarness * h, const gchar * str)
{
  gst_harness_set_src_caps (h, gst_caps_from_string (str));
}

/**
 * gst_harness_set_sink_caps_str:
 * @h: a #GstHarness
 * @str: a @gchar describing a #GstCaps to set on the harness sinkpad
 *
 * Sets the @GstHarness sinkpad caps using a string.
 *
 * MT safe.
 *
 * Since: 1.6
 */
void
gst_harness_set_sink_caps_str (GstHarness * h, const gchar * str)
{
  gst_harness_set_sink_caps (h, gst_caps_from_string (str));
}

/**
 * gst_harness_set_caps_str:
 * @h: a #GstHarness
 * @in: a @gchar describing a #GstCaps to set on the harness srcpad
 * @out: a @gchar describing a #GstCaps to set on the harness sinkpad
 *
 * Sets the @GstHarness srcpad and sinkpad caps using strings.
 *
 * MT safe.
 *
 * Since: 1.6
 */
void
gst_harness_set_caps_str (GstHarness * h, const gchar * in, const gchar * out)
{
  gst_harness_set_sink_caps_str (h, out);
  gst_harness_set_src_caps_str (h, in);
}

/**
 * gst_harness_use_systemclock:
 * @h: a #GstHarness
 *
 * Sets the system #GstClock on the @GstHarness #GstElement
 *
 * MT safe.
 *
 * Since: 1.6
 */
void
gst_harness_use_systemclock (GstHarness * h)
{
  GstClock *clock = gst_system_clock_obtain ();
  g_assert (clock != NULL);
  gst_element_set_clock (h->element, clock);
  gst_object_unref (clock);
}

/**
 * gst_harness_use_testclock:
 * @h: a #GstHarness
 *
 * Sets the #GstTestClock on the #GstHarness #GstElement
 *
 * MT safe.
 *
 * Since: 1.6
 */
void
gst_harness_use_testclock (GstHarness * h)
{
  gst_element_set_clock (h->element, GST_CLOCK_CAST (h->priv->testclock));
}

/**
 * gst_harness_get_testclock:
 * @h: a #GstHarness
 *
 * Get the #GstTestClock. Useful if specific operations on the testclock is
 * needed.
 *
 * MT safe.
 *
 * Returns: (transfer full): a #GstTestClock, or %NULL if the testclock is not
 * present.
 *
 * Since: 1.6
 */
GstTestClock *
gst_harness_get_testclock (GstHarness * h)
{
  return gst_object_ref (h->priv->testclock);
}

/**
 * gst_harness_set_time:
 * @h: a #GstHarness
 * @time: a #GstClockTime to advance the clock to
 *
 * Advance the #GstTestClock to a specific time.
 *
 * MT safe.
 *
 * Returns: a @gboolean %TRUE if the time could be set. %FALSE if not.
 *
 * Since: 1.6
 */
gboolean
gst_harness_set_time (GstHarness * h, GstClockTime time)
{
  gst_test_clock_set_time (h->priv->testclock, time);
  return TRUE;
}

/**
 * gst_harness_wait_for_clock_id_waits:
 * @h: a #GstHarness
 * @waits: a #guint describing the numbers of #GstClockID registered with
 * the #GstTestClock
 * @timeout: a #guint describing how many seconds to wait for @waits to be true
 *
 * Waits for @timeout seconds until @waits number of #GstClockID waits is
 * registered with the #GstTestClock. Useful for writing deterministic tests,
 * where you want to make sure that an expected number of waits have been
 * reached.
 *
 * MT safe.
 *
 * Returns: a @gboolean %TRUE if the waits have been registered, %FALSE if not.
 * (Could be that it timed out waiting or that more waits then waits was found)
 *
 * Since: 1.6
 */
gboolean
gst_harness_wait_for_clock_id_waits (GstHarness * h, guint waits, guint timeout)
{
  GstTestClock *testclock = h->priv->testclock;
  gint64 start_time;
  gboolean ret;

  start_time = g_get_monotonic_time ();
  while (gst_test_clock_peek_id_count (testclock) < waits) {
    gint64 time_spent;

    g_usleep (G_USEC_PER_SEC / 1000);
    time_spent = g_get_monotonic_time () - start_time;
    if ((time_spent / G_USEC_PER_SEC) > timeout)
      break;
  }

  ret = (waits == gst_test_clock_peek_id_count (testclock));

  return ret;
}

/**
 * gst_harness_crank_single_clock_wait:
 * @h: a #GstHarness
 *
 * A "crank" consists of three steps:
 * 1: Wait for a #GstClockID to be registered with the #GstTestClock.
 * 2: Advance the #GstTestClock to the time the #GstClockID is waiting for.
 * 3: Release the #GstClockID wait.
 * Together, this provides an easy way to not have to think about the details
 * around clocks and time, but still being able to write deterministic tests
 * that are dependant on this. A "crank" can be though of as the notion of
 * manually driving the clock forward to its next logical step.
 *
 * MT safe.
 *
 * Returns: a @gboolean %TRUE if the "crank" was successful, %FALSE if not.
 *
 * Since: 1.6
 */
gboolean
gst_harness_crank_single_clock_wait (GstHarness * h)
{
  return gst_test_clock_crank (h->priv->testclock);
}

/**
 * gst_harness_crank_multiple_clock_waits:
 * @h: a #GstHarness
 * @waits: a #guint describing the number of #GstClockIDs to crank
 *
 * Similar to gst_harness_crank_single_clock_wait(), this is the function to use
 * if your harnessed element(s) are using more then one gst_clock_id_wait.
 * Failing to do so can (and will) make it racy which #GstClockID you actually
 * are releasing, where as this function will process all the waits at the
 * same time, ensuring that one thread can't register another wait before
 * both are released.
 *
 * MT safe.
 *
 * Returns: a @gboolean %TRUE if the "crank" was successful, %FALSE if not.
 *
 * Since: 1.6
 */
gboolean
gst_harness_crank_multiple_clock_waits (GstHarness * h, guint waits)
{
  GstTestClock *testclock = h->priv->testclock;
  GList *pending;
  guint processed;

  gst_test_clock_wait_for_multiple_pending_ids (testclock, waits, &pending);
  gst_harness_set_time (h, gst_test_clock_id_list_get_latest_time (pending));
  processed = gst_test_clock_process_id_list (testclock, pending);

  g_list_free_full (pending, gst_clock_id_unref);
  return processed == waits;
}

/**
 * gst_harness_play:
 * @h: a #GstHarness
 *
 * This will set the harnessed #GstElement to %GST_STATE_PLAYING.
 * #GstElements without a sink-#GstPad and with the %GST_ELEMENT_FLAG_SOURCE
 * flag set is concidered a src #GstElement
 * Non-src #GstElements (like sinks and filters) are automatically set to
 * playing by the #GstHarness, but src #GstElements are not to avoid them
 * starting to produce buffers.
 * Hence, for src #GstElement you must call gst_harness_play() explicitly.
 *
 * MT safe.
 *
 * Since: 1.6
 */
void
gst_harness_play (GstHarness * h)
{
  GstState state, pending;
  gboolean state_change;
  state_change = gst_element_set_state (h->element, GST_STATE_PLAYING);
  g_assert_cmpint (GST_STATE_CHANGE_SUCCESS, ==, state_change);
  state_change = gst_element_get_state (h->element, &state, &pending, 0);
  g_assert_cmpint (GST_STATE_CHANGE_SUCCESS, ==, state_change);
  g_assert_cmpint (GST_STATE_PLAYING, ==, state);
}

/**
 * gst_harness_set_blocking_push_mode:
 * @h: a #GstHarness
 *
 * Setting this will make the harness block in the chain-function, and
 * then release when gst_harness_pull() or gst_harness_try_pull() is called.
 * Can be useful when wanting to control a src-element that is not implementing
 * gst_clock_id_wait() so it can't be controlled by the #GstTestClock, since
 * it otherwise would produce buffers as fast as possible.
 *
 * MT safe.
 *
 * Since: 1.6
 */
void
gst_harness_set_blocking_push_mode (GstHarness * h)
{
  GstHarnessPrivate *priv = h->priv;
  priv->blocking_push_mode = TRUE;
}

/**
 * gst_harness_set_forwarding:
 * @h: a #GstHarness
 * @forwarding: a #gboolean to enable/disable forwarding
 *
 * As a convenience, a src-harness will forward %GST_EVENT_STREAM_START,
 * %GST_EVENT_CAPS and %GST_EVENT_SEGMENT to the main-harness if forwarding
 * is enabled, and forward any sticky-events from the main-harness to
 * the sink-harness. It will also forward the %GST_QUERY_ALLOCATION.
 *
 * If forwarding is disabled, the user will have to either manually push
 * these events from the src-harness using gst_harness_src_push_event(), or
 * create and push them manually. While this will allow full control and
 * inspection of these events, for the most cases having forwarding enabled
 * will be sufficient when writing a test where the src-harness' main function
 * is providing data for the main-harness.
 *
 * Forwarding is enabled by default.
 *
 * MT safe.
 *
 * Since: 1.6
 */
void
gst_harness_set_forwarding (GstHarness * h, gboolean forwarding)
{
  GstHarnessPrivate *priv = h->priv;
  priv->forwarding = forwarding;
  if (h->src_harness)
    gst_harness_set_forwarding (h->src_harness, forwarding);
  if (h->sink_harness)
    gst_harness_set_forwarding (h->sink_harness, forwarding);
}

static void
gst_harness_set_forward_pad (GstHarness * h, GstPad * fwdpad)
{
  HARNESS_LOCK (h);
  gst_object_replace ((GstObject **) & h->priv->sink_forward_pad,
      (GstObject *) fwdpad);
  HARNESS_UNLOCK (h);
}

/**
 * gst_harness_create_buffer:
 * @h: a #GstHarness
 * @size: a #gsize specifying the size of the buffer
 *
 * Allocates a buffer using a #GstBufferPool if present, or else using the
 * configured #GstAllocator and #GstAllocationParams
 *
 * MT safe.
 *
 * Returns: a #GstBuffer of size @size
 *
 * Since: 1.6
 */
GstBuffer *
gst_harness_create_buffer (GstHarness * h, gsize size)
{
  GstHarnessPrivate *priv = h->priv;
  GstBuffer *ret = NULL;
  GstFlowReturn flow;

  if (gst_pad_check_reconfigure (h->srcpad))
    gst_harness_negotiate (h);

  if (priv->pool) {
    flow = gst_buffer_pool_acquire_buffer (priv->pool, &ret, NULL);
    g_assert_cmpint (flow, ==, GST_FLOW_OK);
    if (gst_buffer_get_size (ret) != size) {
      GST_DEBUG_OBJECT (h,
          "use fallback, pool is configured with a different size (%zu != %zu)",
          size, gst_buffer_get_size (ret));
      gst_buffer_unref (ret);
      ret = NULL;
    }
  }

  if (!ret)
    ret =
        gst_buffer_new_allocate (priv->allocator, size,
        &priv->allocation_params);

  g_assert (ret != NULL);
  return ret;
}

/**
 * gst_harness_push:
 * @h: a #GstHarness
 * @buffer: a #GstBuffer to push
 *
 * Pushes a #GstBuffer on the #GstHarness srcpad. The standard way of
 * interacting with an harnessed element.
 *
 * MT safe.
 *
 * Returns: a #GstFlowReturn with the result from the push
 *
 * Since: 1.6
 */
GstFlowReturn
gst_harness_push (GstHarness * h, GstBuffer * buffer)
{
  GstHarnessPrivate *priv = h->priv;
  g_assert (buffer != NULL);
  priv->last_push_ts = GST_BUFFER_TIMESTAMP (buffer);
  return gst_pad_push (h->srcpad, buffer);
}

/**
 * gst_harness_pull:
 * @h: a #GstHarness
 *
 * Pulls a #GstBuffer from the #GAsyncQueue on the #GstHarness sinkpad. The pull
 * will timeout in 60 seconds. This is the standard way of getting a buffer
 * from a harnessed #GstElement.
 *
 * MT safe.
 *
 * Returns: a #GstBuffer or %NULL if timed out.
 *
 * Since: 1.6
 */
GstBuffer *
gst_harness_pull (GstHarness * h)
{
  GstHarnessPrivate *priv = h->priv;
  GstBuffer *buf = (GstBuffer *) g_async_queue_timeout_pop (priv->buffer_queue,
      G_USEC_PER_SEC * 60);

  if (priv->blocking_push_mode) {
    g_mutex_lock (&priv->blocking_push_mutex);
    g_cond_signal (&priv->blocking_push_cond);
    g_mutex_unlock (&priv->blocking_push_mutex);
  }

  return buf;
}

/**
 * gst_harness_try_pull:
 * @h: a #GstHarness
 *
 * Pulls a #GstBuffer from the #GAsyncQueue on the #GstHarness sinkpad. Unlike
 * gst_harness_pull this will not wait for any buffers if not any are present,
 * and return %NULL straight away.
 *
 * MT safe.
 *
 * Returns: a #GstBuffer or %NULL if no buffers are present in the #GAsyncQueue
 *
 * Since: 1.6
 */
GstBuffer *
gst_harness_try_pull (GstHarness * h)
{
  GstHarnessPrivate *priv = h->priv;
  GstBuffer *buf = (GstBuffer *) g_async_queue_try_pop (priv->buffer_queue);

  if (priv->blocking_push_mode) {
    g_mutex_lock (&priv->blocking_push_mutex);
    g_cond_signal (&priv->blocking_push_cond);
    g_mutex_unlock (&priv->blocking_push_mutex);
  }

  return buf;
}

/**
 * gst_harness_push_and_pull:
 * @h: a #GstHarness
 * @buffer: a #GstBuffer to push
 *
 * Basically a gst_harness_push and a gst_harness_pull in one line. Reflects
 * the fact that you often want to do exactly this in your test: Push one buffer
 * in, and inspect the outcome.
 *
 * MT safe.
 *
 * Returns: a #GstBuffer or %NULL if timed out.
 *
 * Since: 1.6
 */
GstBuffer *
gst_harness_push_and_pull (GstHarness * h, GstBuffer * buffer)
{
  gst_harness_push (h, buffer);
  return gst_harness_pull (h);
}

/**
 * gst_harness_buffers_received:
 * @h: a #GstHarness
 *
 * The total number of #GstBuffers that has arrived on the #GstHarness sinkpad.
 * This number includes buffers that have been dropped as well as buffers
 * that have already been pulled out.
 *
 * MT safe.
 *
 * Returns: a #guint number of buffers received
 *
 * Since: 1.6
 */
guint
gst_harness_buffers_received (GstHarness * h)
{
  GstHarnessPrivate *priv = h->priv;
  return g_atomic_int_get (&priv->recv_buffers);
}

/**
 * gst_harness_buffers_in_queue:
 * @h: a #GstHarness
 *
 * The number of #GstBuffers currently in the #GstHarness sinkpad #GAsyncQueue
 *
 * MT safe.
 *
 * Returns: a #guint number of buffers in the queue
 *
 * Since: 1.6
 */
guint
gst_harness_buffers_in_queue (GstHarness * h)
{
  GstHarnessPrivate *priv = h->priv;
  return g_async_queue_length (priv->buffer_queue);
}

/**
 * gst_harness_set_drop_buffers:
 * @h: a #GstHarness
 * @drop_buffers: a #gboolean specifying to drop outgoing buffers or not
 *
 * When set to %TRUE, instead of placing the buffers arriving from the harnessed
 * #GstElement inside the sinkpads #GAsyncQueue, they are instead unreffed.
 *
 * MT safe.
 *
 * Since: 1.6
 */
void
gst_harness_set_drop_buffers (GstHarness * h, gboolean drop_buffers)
{
  GstHarnessPrivate *priv = h->priv;
  priv->drop_buffers = drop_buffers;
}

/**
 * gst_harness_dump_to_file:
 * @h: a #GstHarness
 * @filename: a #gchar with a the name of a file
 *
 * Allows you to dump the #GstBuffers the #GstHarness sinkpad #GAsyncQueue
 * to a file.
 *
 * MT safe.
 *
 * Since: 1.6
 */
void
gst_harness_dump_to_file (GstHarness * h, const gchar * filename)
{
  GstHarnessPrivate *priv = h->priv;
  FILE *fd;
  GstBuffer *buf;
  fd = fopen (filename, "wb");
  g_assert (fd);

  while ((buf = g_async_queue_try_pop (priv->buffer_queue))) {
    GstMapInfo info;
    if (gst_buffer_map (buf, &info, GST_MAP_READ)) {
      fwrite (info.data, 1, info.size, fd);
      gst_buffer_unmap (buf, &info);
    } else {
      GST_ERROR ("failed to map buffer %p", buf);
    }
    gst_buffer_unref (buf);
  }

  fflush (fd);
  fclose (fd);
}

/**
 * gst_harness_get_last_pushed_timestamp:
 * @h: a #GstHarness
 *
 * Get the timestamp of the last #GstBuffer pushed on the #GstHarness srcpad,
 * typically with gst_harness_push or gst_harness_push_from_src.
 *
 * MT safe.
 *
 * Returns: a #GstClockTime with the timestamp or %GST_CLOCK_TIME_NONE if no
 * #GstBuffer has been pushed on the #GstHarness srcpad
 *
 * Since: 1.6
 */
GstClockTime
gst_harness_get_last_pushed_timestamp (GstHarness * h)
{
  GstHarnessPrivate *priv = h->priv;
  return priv->last_push_ts;
}

/**
 * gst_harness_push_event:
 * @h: a #GstHarness
 * @event: a #GstEvent to push
 *
 * Pushes an #GstEvent on the #GstHarness srcpad.
 *
 * MT safe.
 *
 * Returns: a #gboolean with the result from the push
 *
 * Since: 1.6
 */
gboolean
gst_harness_push_event (GstHarness * h, GstEvent * event)
{
  return gst_pad_push_event (h->srcpad, event);
}

/**
 * gst_harness_pull_event:
 * @h: a #GstHarness
 *
 * Pulls an #GstEvent from the #GAsyncQueue on the #GstHarness sinkpad.
 * Timeouts after 60 seconds similar to gst_harness_pull.
 *
 * MT safe.
 *
 * Returns: a #GstEvent or %NULL if timed out.
 *
 * Since: 1.6
 */
GstEvent *
gst_harness_pull_event (GstHarness * h)
{
  GstHarnessPrivate *priv = h->priv;
  return (GstEvent *) g_async_queue_timeout_pop (priv->sink_event_queue,
      G_USEC_PER_SEC * 60);
}

/**
 * gst_harness_try_pull_event:
 * @h: a #GstHarness
 *
 * Pulls an #GstEvent from the #GAsyncQueue on the #GstHarness sinkpad.
 * See gst_harness_try_pull for details.
 *
 * MT safe.
 *
 * Returns: a #GstEvent or %NULL if no buffers are present in the #GAsyncQueue
 *
 * Since: 1.6
 */
GstEvent *
gst_harness_try_pull_event (GstHarness * h)
{
  GstHarnessPrivate *priv = h->priv;
  return (GstEvent *) g_async_queue_try_pop (priv->sink_event_queue);
}

/**
 * gst_harness_events_received:
 * @h: a #GstHarness
 *
 * The total number of #GstEvents that has arrived on the #GstHarness sinkpad
 * This number includes events handled by the harness as well as events
 * that have already been pulled out.
 *
 * MT safe.
 *
 * Returns: a #guint number of events received
 *
 * Since: 1.6
 */
guint
gst_harness_events_received (GstHarness * h)
{
  GstHarnessPrivate *priv = h->priv;
  return g_atomic_int_get (&priv->recv_events);
}

/**
 * gst_harness_events_in_queue:
 * @h: a #GstHarness
 *
 * The number of #GstEvents currently in the #GstHarness sinkpad #GAsyncQueue
 *
 * MT safe.
 *
 * Returns: a #guint number of events in the queue
 *
 * Since: 1.6
 */
guint
gst_harness_events_in_queue (GstHarness * h)
{
  GstHarnessPrivate *priv = h->priv;
  return g_async_queue_length (priv->sink_event_queue);
}

/**
 * gst_harness_push_upstream_event:
 * @h: a #GstHarness
 * @event: a #GstEvent to push
 *
 * Pushes an #GstEvent on the #GstHarness sinkpad.
 *
 * MT safe.
 *
 * Returns: a #gboolean with the result from the push
 *
 * Since: 1.6
 */
gboolean
gst_harness_push_upstream_event (GstHarness * h, GstEvent * event)
{
  g_return_val_if_fail (event != NULL, FALSE);
  g_return_val_if_fail (GST_EVENT_IS_UPSTREAM (event), FALSE);

  return gst_pad_push_event (h->sinkpad, event);
}

/**
 * gst_harness_pull_upstream_event:
 * @h: a #GstHarness
 *
 * Pulls an #GstEvent from the #GAsyncQueue on the #GstHarness srcpad.
 * Timeouts after 60 seconds similar to gst_harness_pull.
 *
 * MT safe.
 *
 * Returns: a #GstEvent or %NULL if timed out.
 *
 * Since: 1.6
 */
GstEvent *
gst_harness_pull_upstream_event (GstHarness * h)
{
  GstHarnessPrivate *priv = h->priv;
  return (GstEvent *) g_async_queue_timeout_pop (priv->src_event_queue,
      G_USEC_PER_SEC * 60);
}

/**
 * gst_harness_try_pull_upstream_event:
 * @h: a #GstHarness
 *
 * Pulls an #GstEvent from the #GAsyncQueue on the #GstHarness srcpad.
 * See gst_harness_try_pull for details.
 *
 * MT safe.
 *
 * Returns: a #GstEvent or %NULL if no buffers are present in the #GAsyncQueue
 *
 * Since: 1.6
 */
GstEvent *
gst_harness_try_pull_upstream_event (GstHarness * h)
{
  GstHarnessPrivate *priv = h->priv;
  return (GstEvent *) g_async_queue_try_pop (priv->src_event_queue);
}

/**
 * gst_harness_upstream_events_received:
 * @h: a #GstHarness
 *
 * The total number of #GstEvents that has arrived on the #GstHarness srcpad
 * This number includes events handled by the harness as well as events
 * that have already been pulled out.
 *
 * MT safe.
 *
 * Returns: a #guint number of events received
 *
 * Since: 1.6
 */
guint
gst_harness_upstream_events_received (GstHarness * h)
{
  GstHarnessPrivate *priv = h->priv;
  return g_atomic_int_get (&priv->recv_upstream_events);
}

/**
 * gst_harness_upstream_events_in_queue:
 * @h: a #GstHarness
 *
 * The number of #GstEvents currently in the #GstHarness srcpad #GAsyncQueue
 *
 * MT safe.
 *
 * Returns: a #guint number of events in the queue
 *
 * Since: 1.6
 */
guint
gst_harness_upstream_events_in_queue (GstHarness * h)
{
  GstHarnessPrivate *priv = h->priv;
  return g_async_queue_length (priv->src_event_queue);
}

/**
 * gst_harness_query_latency:
 * @h: a #GstHarness
 *
 * Get the min latency reported by any harnessed #GstElement.
 *
 * MT safe.
 *
 * Returns: a #GstClockTime with min latency
 *
 * Since: 1.6
 */
GstClockTime
gst_harness_query_latency (GstHarness * h)
{
  GstQuery *query;
  gboolean is_live;
  GstClockTime min = GST_CLOCK_TIME_NONE;
  GstClockTime max;

  query = gst_query_new_latency ();

  if (gst_pad_peer_query (h->sinkpad, query)) {
    gst_query_parse_latency (query, &is_live, &min, &max);
  }
  gst_query_unref (query);

  return min;
}

/**
 * gst_harness_set_upstream_latency:
 * @h: a #GstHarness
 * @latency: a #GstClockTime specifying the latency
 *
 * Sets the min latency reported by #GstHarness when receiving a latency-query
 *
 * Since: 1.6
 */
void
gst_harness_set_upstream_latency (GstHarness * h, GstClockTime latency)
{
  GstHarnessPrivate *priv = h->priv;
  priv->latency_min = latency;
}

/**
 * gst_harness_get_allocator:
 * @h: a #GstHarness
 * @allocator: (out) (allow-none) (transfer none): the #GstAllocator used
 * @params: (out) (allow-none) (transfer full): the #GstAllocationParams of
 *   @allocator
 *
 * Gets the @allocator and its @params that has been decided to use after an
 * allocation query.
 *
 * MT safe.
 *
 * Since: 1.6
 */
void
gst_harness_get_allocator (GstHarness * h, GstAllocator ** allocator,
    GstAllocationParams * params)
{
  GstHarnessPrivate *priv = h->priv;
  if (allocator)
    *allocator = priv->allocator;
  if (params)
    *params = priv->allocation_params;
}


/**
 * gst_harness_set_propose_allocator:
 * @h: a #GstHarness
 * @allocator: (allow-none) (transfer full): a #GstAllocator
 * @params: (allow-none) (transfer none): a #GstAllocationParams
 *
 * Sets the @allocator and @params to propose when receiving an allocation
 * query.
 *
 * MT safe.
 *
 * Since: 1.6
 */
void
gst_harness_set_propose_allocator (GstHarness * h, GstAllocator * allocator,
    const GstAllocationParams * params)
{
  GstHarnessPrivate *priv = h->priv;
  if (allocator)
    priv->propose_allocator = allocator;
  if (params)
    priv->propose_allocation_params = *params;
}

/**
 * gst_harness_add_src_harness:
 * @h: a #GstHarness
 * @src_harness: (transfer full): a #GstHarness to be added as a src-harness.
 * @has_clock_wait: a #gboolean specifying if the #GstElement uses
 * gst_clock_wait_id internally.
 *
 * A src-harness is a great way of providing the #GstHarness with data.
 * By adding a src-type #GstElement, it is then easy to use functions like
 * gst_harness_push_from_src or gst_harness_src_crank_and_push_many
 * to provide your harnessed element with input. The @has_clock_wait variable
 * is a greate way to control you src-element with, in that you can have it
 * produce a buffer for you by simply cranking the clock, and not have it
 * spin out of control producing buffers as fast as possible.
 *
 * If a src-harness already exists it will be replaced.
 *
 * MT safe.
 *
 * Since: 1.6
 */
void
gst_harness_add_src_harness (GstHarness * h,
    GstHarness * src_harness, gboolean has_clock_wait)
{
  if (h->src_harness)
    gst_harness_teardown (h->src_harness);
  h->src_harness = src_harness;
  gst_harness_set_forward_pad (h->src_harness, h->srcpad);
  h->src_harness->priv->has_clock_wait = has_clock_wait;
  gst_harness_set_forwarding (h->src_harness, h->priv->forwarding);
}

/**
 * gst_harness_add_src:
 * @h: a #GstHarness
 * @src_element_name: a #gchar with the name of a #GstElement
 * @has_clock_wait: a #gboolean specifying if the #GstElement uses
 * gst_clock_wait_id internally.
 *
 * Similar to gst_harness_add_src_harness, this is a convenience to
 * directly create a src-harness using the @src_element_name name specified.
 *
 * MT safe.
 *
 * Since: 1.6
 */
void
gst_harness_add_src (GstHarness * h,
    const gchar * src_element_name, gboolean has_clock_wait)
{
  GstHarness *src_harness = gst_harness_new (src_element_name);
  gst_harness_add_src_harness (h, src_harness, has_clock_wait);
}

/**
 * gst_harness_add_src_parse:
 * @h: a #GstHarness
 * @launchline: a #gchar describing a gst-launch type line
 * @has_clock_wait: a #gboolean specifying if the #GstElement uses
 * gst_clock_wait_id internally.
 *
 * Similar to gst_harness_add_src, this allows you to specify a launch-line,
 * which can be useful for both having more then one #GstElement acting as your
 * src (Like a src producing raw buffers, and then an encoder, providing encoded
 * data), but also by allowing you to set properties like "is-live" directly on
 * the elements.
 *
 * MT safe.
 *
 * Since: 1.6
 */
void
gst_harness_add_src_parse (GstHarness * h,
    const gchar * launchline, gboolean has_clock_wait)
{
  GstHarness *src_harness = gst_harness_new_parse (launchline);
  gst_harness_add_src_harness (h, src_harness, has_clock_wait);
}

/**
 * gst_harness_push_from_src:
 * @h: a #GstHarness
 *
 * Transfer data from the src-#GstHarness to the main-#GstHarness. It consists
 * of 4 steps:
 * 1: Make sure the src is started. (see: gst_harness_play)
 * 2: Crank the clock (see: gst_harness_crank_single_clock_wait)
 * 3: Pull a #GstBuffer from the src-#GstHarness (see: gst_harness_pull)
 * 4: Push the same #GstBuffer into the main-#GstHarness (see: gst_harness_push)
 *
 * MT safe.
 *
 * Returns: a #GstFlowReturn with the result of the push
 *
 * Since: 1.6
 */
GstFlowReturn
gst_harness_push_from_src (GstHarness * h)
{
  GstBuffer *buf;
  gboolean crank;

  g_assert (h->src_harness);

  /* FIXME: this *is* the right time to start the src,
     but maybe a flag so we don't keep telling it to play? */
  gst_harness_play (h->src_harness);

  if (h->src_harness->priv->has_clock_wait) {
    crank = gst_harness_crank_single_clock_wait (h->src_harness);
    g_assert (crank);
  }

  buf = gst_harness_pull (h->src_harness);
  g_assert (buf != NULL);
  return gst_harness_push (h, buf);
}

/**
 * gst_harness_src_crank_and_push_many:
 * @h: a #GstHarness
 * @cranks: a #gint with the number of calls to gst_harness_crank_single_clock_wait
 * @pushes: a #gint with the number of calls to gst_harness_push
 *
 * Transfer data from the src-#GstHarness to the main-#GstHarness. Similar to
 * gst_harness_push_from_src, this variant allows you to specify how many cranks
 * and how many pushes to perform. This can be useful for both moving a lot
 * of data at the same time, as well as cases when one crank does not equal one
 * buffer to push and v.v.
 *
 * MT safe.
 *
 * Returns: a #GstFlowReturn with the result of the push
 *
 * Since: 1.6
 */
GstFlowReturn
gst_harness_src_crank_and_push_many (GstHarness * h, gint cranks, gint pushes)
{
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean crank;
  int i;

  g_assert (h->src_harness);
  gst_harness_play (h->src_harness);

  for (i = 0; i < cranks; i++) {
    crank = gst_harness_crank_single_clock_wait (h->src_harness);
    g_assert (crank);
  }

  for (i = 0; i < pushes; i++) {
    GstBuffer *buf;
    buf = gst_harness_pull (h->src_harness);
    g_assert (buf != NULL);
    ret = gst_harness_push (h, buf);
    if (ret != GST_FLOW_OK)
      break;
  }

  return ret;
}

/**
 * gst_harness_src_push_event:
 * @h: a #GstHarness
 *
 * Similar to what gst_harness_src_push does with #GstBuffers, this transfers
 * a #GstEvent from the src-#GstHarness to the main-#GstHarness. Note that
 * some #GstEvents are being transferred automagically. Look at sink_forward_pad
 * for details.
 *
 * MT safe.
 *
 * Returns: a #gboolean with the result of the push
 *
 * Since: 1.6
 */
gboolean
gst_harness_src_push_event (GstHarness * h)
{
  return gst_harness_push_event (h, gst_harness_pull_event (h->src_harness));
}


static gboolean
forward_sticky_events (GstPad * pad, GstEvent ** ev, gpointer user_data)
{
  GstHarness *h = user_data;
  return gst_pad_push_event (h->priv->sink_forward_pad, gst_event_ref (*ev));
}

/**
 * gst_harness_add_sink_harness:
 * @h: a #GstHarness
 * @sink_harness: (transfer full): a #GstHarness to be added as a sink-harness.
 *
 * Similar to gst_harness_add_src, this allows you to send the data coming out
 * of your harnessed #GstElement to a sink-element, allowing to test different
 * responses the element output might create in sink elements. An example might
 * be an existing sink providing some analytical data on the input it receives that
 * can be useful to your testing. If the goal is to test a sink-element itself,
 * this is better acheived using gst_harness_new directly on the sink.
 *
 * If a sink-harness already exists it will be replaced.
 *
 * MT safe.
 *
 * Since: 1.6
 */
void
gst_harness_add_sink_harness (GstHarness * h, GstHarness * sink_harness)
{
  GstHarnessPrivate *priv = h->priv;

  if (h->sink_harness) {
    gst_harness_set_forward_pad (h, NULL);
    gst_harness_teardown (h->sink_harness);
  }
  h->sink_harness = sink_harness;
  gst_harness_set_forward_pad (h, h->sink_harness->srcpad);
  if (priv->forwarding && h->sinkpad)
    gst_pad_sticky_events_foreach (h->sinkpad, forward_sticky_events, h);
  gst_harness_set_forwarding (h->sink_harness, priv->forwarding);
}

/**
 * gst_harness_add_sink:
 * @h: a #GstHarness
 * @sink_element_name: a #gchar with the name of a #GstElement
 *
 * Similar to gst_harness_add_sink_harness, this is a convenience to
 * directly create a sink-harness using the @sink_element_name name specified.
 *
 * MT safe.
 *
 * Since: 1.6
 */
void
gst_harness_add_sink (GstHarness * h, const gchar * sink_element_name)
{
  GstHarness *sink_harness = gst_harness_new (sink_element_name);
  gst_harness_add_sink_harness (h, sink_harness);
}

/**
 * gst_harness_add_sink_parse:
 * @h: a #GstHarness
 * @launchline: a #gchar with the name of a #GstElement
 *
 * Similar to gst_harness_add_sink, this allows you to specify a launch-line
 * instead of just an element name. See gst_harness_add_src_parse for details.
 *
 * MT safe.
 *
 * Since: 1.6
 */
void
gst_harness_add_sink_parse (GstHarness * h, const gchar * launchline)
{
  GstHarness *sink_harness = gst_harness_new_parse (launchline);
  gst_harness_add_sink_harness (h, sink_harness);
}

/**
 * gst_harness_push_to_sink:
 * @h: a #GstHarness
 *
 * Transfer one #GstBuffer from the main-#GstHarness to the sink-#GstHarness.
 * See gst_harness_push_from_src for details.
 *
 * MT safe.
 *
 * Returns: a #GstFlowReturn with the result of the push
 *
 * Since: 1.6
 */
GstFlowReturn
gst_harness_push_to_sink (GstHarness * h)
{
  GstBuffer *buf;
  g_assert (h->sink_harness);
  buf = gst_harness_pull (h);
  g_assert (buf != NULL);
  return gst_harness_push (h->sink_harness, buf);
}

/**
 * gst_harness_sink_push_many:
 * @h: a #GstHarness
 * @pushes: a #gint with the number of calls to gst_harness_push_to_sink
 *
 * Convenience that calls gst_harness_push_to_sink @pushes number of times.
 * Will abort the pushing if any one push fails.
 *
 * MT safe.
 *
 * Returns: a #GstFlowReturn with the result of the push
 *
 * Since: 1.6
 */
GstFlowReturn
gst_harness_sink_push_many (GstHarness * h, gint pushes)
{
  GstFlowReturn ret = GST_FLOW_OK;
  int i;
  g_assert (h->sink_harness);
  for (i = 0; i < pushes; i++) {
    ret = gst_harness_push_to_sink (h);
    if (ret != GST_FLOW_OK)
      break;
  }
  return ret;
}

/**
 * gst_harness_find_element:
 * @h: a #GstHarness
 * @element_name: a #gchar with a #GstElementFactory name
 *
 * Most useful in conjunction with gst_harness_new_parse, this will scan the
 * #GstElements inside the #GstHarness, and check if any of them matches
 * @element_name. Typical usecase being that you need to access one of the
 * harnessed elements for properties and/or signals.
 *
 * MT safe.
 *
 * Returns: (transfer full) (allow-none): a #GstElement or %NULL if not found
 *
 * Since: 1.6
 */
GstElement *
gst_harness_find_element (GstHarness * h, const gchar * element_name)
{
  gboolean done = FALSE;
  GstIterator *iter;
  GValue data = G_VALUE_INIT;

  iter = gst_bin_iterate_elements (GST_BIN (h->element));
  done = FALSE;

  while (!done) {
    switch (gst_iterator_next (iter, &data)) {
      case GST_ITERATOR_OK:
      {
        GstElement *element = g_value_get_object (&data);
        GstPluginFeature *feature =
            GST_PLUGIN_FEATURE (gst_element_get_factory (element));
        if (!strcmp (element_name, gst_plugin_feature_get_name (feature))) {
          gst_iterator_free (iter);
          return element;
        }
        g_value_reset (&data);
        break;
      }
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        break;
      case GST_ITERATOR_ERROR:
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (iter);

  return NULL;
}

/**
 * gst_harness_set:
 * @h: a #GstHarness
 * @element_name: a #gchar with a #GstElementFactory name
 * @first_property_name: a #gchar with the first property name
 * @...: value for the first property, followed optionally by more
 *  name/value pairs, followed by %NULL
 *
 * A convenience function to allows you to call g_object_set on a #GstElement
 * that are residing inside the #GstHarness, by using normal g_object_set
 * syntax.
 *
 * MT safe.
 *
 * Since: 1.6
 */
void
gst_harness_set (GstHarness * h,
    const gchar * element_name, const gchar * first_property_name, ...)
{
  va_list var_args;
  GstElement *element = gst_harness_find_element (h, element_name);
  va_start (var_args, first_property_name);
  g_object_set_valist (G_OBJECT (element), first_property_name, var_args);
  va_end (var_args);
  gst_object_unref (element);
}

/**
 * gst_harness_get:
 * @h: a #GstHarness
 * @element_name: a #gchar with a #GstElementFactory name
 * @first_property_name: a #gchar with the first property name
 * @...: return location for the first property, followed optionally by more
 *  name/return location pairs, followed by %NULL
 *
 * A convenience function to allows you to call g_object_get on a #GstElement
 * that are residing inside the #GstHarness, by using normal g_object_get
 * syntax.
 *
 * MT safe.
 *
 * Since: 1.6
 */
void
gst_harness_get (GstHarness * h,
    const gchar * element_name, const gchar * first_property_name, ...)
{
  va_list var_args;
  GstElement *element = gst_harness_find_element (h, element_name);
  va_start (var_args, first_property_name);
  g_object_get_valist (G_OBJECT (element), first_property_name, var_args);
  va_end (var_args);
  gst_object_unref (element);
}

/**
 * gst_harness_add_probe:
 * @h: a #GstHarness
 * @element_name: a #gchar with a #GstElementFactory name
 * @pad_name: a #gchar with the name of the pad to attach the probe to
 * @mask: a #GstPadProbeType (see gst_pad_add_probe)
 * @callback: a #GstPadProbeCallback (see gst_pad_add_probe)
 * @user_data: a #gpointer (see gst_pad_add_probe)
 * @destroy_data: a #GDestroyNotify (see gst_pad_add_probe)
 *
 * A convenience function to allows you to call gst_pad_add_probe on a
 * #GstPad of a #GstElement that are residing inside the #GstHarness,
 * by using normal gst_pad_add_probe syntax
 *
 * MT safe.
 *
 * Since: 1.6
 */
void
gst_harness_add_probe (GstHarness * h,
    const gchar * element_name, const gchar * pad_name, GstPadProbeType mask,
    GstPadProbeCallback callback, gpointer user_data,
    GDestroyNotify destroy_data)
{
  GstElement *element = gst_harness_find_element (h, element_name);
  GstPad *pad = gst_element_get_static_pad (element, pad_name);
  gst_pad_add_probe (pad, mask, callback, user_data, destroy_data);
  gst_object_unref (pad);
  gst_object_unref (element);
}

/******************************************************************************/
/*       STRESS                                                               */
/******************************************************************************/
struct _GstHarnessThread
{
  GstHarness *h;
  GThread *thread;
  gboolean running;

  gulong sleep;

  GDestroyNotify freefunc;
};

typedef struct
{
  GstHarnessThread t;

  GFunc init;
  GFunc callback;
  gpointer data;
} GstHarnessCustomThread;

typedef struct
{
  GstHarnessThread t;

  GstCaps *caps;
  GstSegment segment;
  GstHarnessPrepareBufferFunc func;
  gpointer data;
  GDestroyNotify notify;
} GstHarnessPushBufferThread;

typedef struct
{
  GstHarnessThread t;

  GstHarnessPrepareEventFunc func;
  gpointer data;
  GDestroyNotify notify;
} GstHarnessPushEventThread;

typedef struct
{
  GstHarnessThread t;

  gchar *name;
  GValue value;
} GstHarnessPropThread;

typedef struct
{
  GstHarnessThread t;

  GstPadTemplate *templ;
  gchar *name;
  GstCaps *caps;
  gboolean release;

  GSList *pads;
} GstHarnessReqPadThread;

static void
gst_harness_thread_init (GstHarnessThread * t, GDestroyNotify freefunc,
    GstHarness * h, gulong sleep)
{
  t->freefunc = freefunc;
  t->h = h;
  t->sleep = sleep;

  g_ptr_array_add (h->priv->stress, t);
}

static void
gst_harness_thread_free (GstHarnessThread * t)
{
  g_slice_free (GstHarnessThread, t);
}

static void
gst_harness_custom_thread_free (GstHarnessCustomThread * t)
{
  g_slice_free (GstHarnessCustomThread, t);
}

static void
gst_harness_push_buffer_thread_free (GstHarnessPushBufferThread * t)
{
  if (t != NULL) {
    gst_caps_replace (&t->caps, NULL);
    if (t->notify != NULL)
      t->notify (t->data);
    g_slice_free (GstHarnessPushBufferThread, t);
  }
}

static void
gst_harness_push_event_thread_free (GstHarnessPushEventThread * t)
{
  if (t != NULL) {
    if (t->notify != NULL)
      t->notify (t->data);
    g_slice_free (GstHarnessPushEventThread, t);
  }
}

static void
gst_harness_property_thread_free (GstHarnessPropThread * t)
{
  if (t != NULL) {
    g_free (t->name);
    g_value_unset (&t->value);
    g_slice_free (GstHarnessPropThread, t);
  }
}

static void
gst_harness_requestpad_release (GstPad * pad, GstElement * element)
{
  gst_element_release_request_pad (element, pad);
  gst_object_unref (pad);
}

static void
gst_harness_requestpad_release_pads (GstHarnessReqPadThread * rpt)
{
  g_slist_foreach (rpt->pads, (GFunc) gst_harness_requestpad_release,
      rpt->t.h->element);
  g_slist_free (rpt->pads);
  rpt->pads = NULL;
}

static void
gst_harness_requestpad_thread_free (GstHarnessReqPadThread * t)
{
  if (t != NULL) {
    gst_object_replace ((GstObject **) & t->templ, NULL);
    g_free (t->name);
    gst_caps_replace (&t->caps, NULL);

    gst_harness_requestpad_release_pads (t);
    g_slice_free (GstHarnessReqPadThread, t);
  }
}

#define GST_HARNESS_THREAD_START(ID, t)                                        \
  (((GstHarnessThread *)t)->running = TRUE,                                    \
  ((GstHarnessThread *)t)->thread = g_thread_new (                             \
      "gst-harness-stress-"G_STRINGIFY(ID),                                    \
      (GThreadFunc)gst_harness_stress_##ID##_func, t))
#define GST_HARNESS_THREAD_END(t)                                              \
   (t->running = FALSE,                                                        \
   GPOINTER_TO_UINT (g_thread_join (t->thread)))

static void
gst_harness_stress_free (GstHarnessThread * t)
{
  if (t != NULL && t->freefunc != NULL)
    t->freefunc (t);
}

static gpointer
gst_harness_stress_custom_func (GstHarnessThread * t)
{
  GstHarnessCustomThread *ct = (GstHarnessCustomThread *) t;
  guint count = 0;

  if (ct->init != NULL)
    ct->init (ct, ct->data);

  while (t->running) {
    ct->callback (ct, ct->data);

    count++;
    g_usleep (t->sleep);
  }
  return GUINT_TO_POINTER (count);
}


static gpointer
gst_harness_stress_statechange_func (GstHarnessThread * t)
{
  guint count = 0;

  while (t->running) {
    GstClock *clock = gst_element_get_clock (t->h->element);
    GstIterator *it;
    gboolean done = FALSE;
    gboolean change;

    change = gst_element_set_state (t->h->element, GST_STATE_NULL);
    g_assert (change == GST_STATE_CHANGE_SUCCESS);
    g_thread_yield ();

    it = gst_element_iterate_sink_pads (t->h->element);
    while (!done) {
      GValue item = G_VALUE_INIT;
      switch (gst_iterator_next (it, &item)) {
        case GST_ITERATOR_OK:
        {
          GstPad *sinkpad = g_value_get_object (&item);
          GstPad *srcpad = gst_pad_get_peer (sinkpad);
          if (srcpad != NULL) {
            gst_pad_unlink (srcpad, sinkpad);
            gst_pad_link (srcpad, sinkpad);
            gst_object_unref (srcpad);
          }
          g_value_reset (&item);
          break;
        }
        case GST_ITERATOR_RESYNC:
          gst_iterator_resync (it);
          break;
        case GST_ITERATOR_ERROR:
          g_assert_not_reached ();
        case GST_ITERATOR_DONE:
          done = TRUE;
          break;
      }
      g_value_unset (&item);
    }
    gst_iterator_free (it);

    if (clock != NULL) {
      gst_element_set_clock (t->h->element, clock);
      gst_object_unref (clock);
    }
    change = gst_element_set_state (t->h->element, GST_STATE_PLAYING);
    g_assert (change == GST_STATE_CHANGE_SUCCESS);

    count++;
    g_usleep (t->sleep);
  }
  return GUINT_TO_POINTER (count);
}

static gpointer
gst_harness_stress_buffer_func (GstHarnessThread * t)
{
  GstHarnessPushBufferThread *pt = (GstHarnessPushBufferThread *) t;
  guint count = 0;
  gchar *sid;
  gboolean handled;

  /* Push stream start, caps and segment events */
  sid = g_strdup_printf ("%s-%p", GST_OBJECT_NAME (t->h->element), t->h);
  handled = gst_pad_push_event (t->h->srcpad, gst_event_new_stream_start (sid));
  g_assert (handled);
  g_free (sid);
  handled = gst_pad_push_event (t->h->srcpad, gst_event_new_caps (pt->caps));
  g_assert (handled);
  handled = gst_pad_push_event (t->h->srcpad,
      gst_event_new_segment (&pt->segment));
  g_assert (handled);

  while (t->running) {
    gst_harness_push (t->h, pt->func (t->h, pt->data));

    count++;
    g_usleep (t->sleep);
  }
  return GUINT_TO_POINTER (count);
}

static gpointer
gst_harness_stress_event_func (GstHarnessThread * t)
{
  GstHarnessPushEventThread *pet = (GstHarnessPushEventThread *) t;
  guint count = 0;

  while (t->running) {
    gst_harness_push_event (t->h, pet->func (t->h, pet->data));

    count++;
    g_usleep (t->sleep);
  }
  return GUINT_TO_POINTER (count);
}

static gpointer
gst_harness_stress_upstream_event_func (GstHarnessThread * t)
{
  GstHarnessPushEventThread *pet = (GstHarnessPushEventThread *) t;
  guint count = 0;

  while (t->running) {
    gst_harness_push_upstream_event (t->h, pet->func (t->h, pet->data));

    count++;
    g_usleep (t->sleep);
  }
  return GUINT_TO_POINTER (count);
}

static gpointer
gst_harness_stress_property_func (GstHarnessThread * t)
{
  GstHarnessPropThread *pt = (GstHarnessPropThread *) t;
  guint count = 0;

  while (t->running) {
    GValue value = G_VALUE_INIT;

    g_object_set_property (G_OBJECT (t->h->element), pt->name, &pt->value);

    g_value_init (&value, G_VALUE_TYPE (&pt->value));
    g_object_get_property (G_OBJECT (t->h->element), pt->name, &value);
    g_value_reset (&value);

    count++;
    g_usleep (t->sleep);
  }
  return GUINT_TO_POINTER (count);
}

static gpointer
gst_harness_stress_requestpad_func (GstHarnessThread * t)
{
  GstHarnessReqPadThread *rpt = (GstHarnessReqPadThread *) t;
  guint count = 0;

  while (t->running) {
    GstPad *reqpad;

    if (rpt->release)
      gst_harness_requestpad_release_pads (rpt);

    g_thread_yield ();

    reqpad = gst_element_request_pad (t->h->element,
        rpt->templ, rpt->name, rpt->caps);

    g_assert (reqpad != NULL);

    rpt->pads = g_slist_prepend (rpt->pads, reqpad);

    count++;
    g_usleep (t->sleep);
  }
  return GUINT_TO_POINTER (count);
}

/**
 * gst_harness_stress_thread_stop:
 * @t: a #GstHarnessThread
 *
 * Stop the running #GstHarnessThread
 *
 * MT safe.
 *
 * Since: 1.6
 */
guint
gst_harness_stress_thread_stop (GstHarnessThread * t)
{
  guint ret;

  g_return_val_if_fail (t != NULL, 0);

  ret = GST_HARNESS_THREAD_END (t);
  g_ptr_array_remove (t->h->priv->stress, t);
  return ret;
}

/**
 * gst_harness_stress_custom_start: (skip)
 * @h: a #GstHarness
 * @init: (allow-none): a #GFunc that is called initially and only once
 * @callback: a #GFunc that is called as often as possible
 * @data: a #gpointer with custom data to pass to the @callback function
 * @sleep: a #gulong specifying how long to sleep in (microseconds) for
 * each call to the @callback
 *
 * Start a custom stress-thread that will call your @callback for every
 * iteration allowing you to do something nasty.
 *
 * MT safe.
 *
 * Returns: a #GstHarnessThread
 *
 * Since: 1.6
 */
GstHarnessThread *
gst_harness_stress_custom_start (GstHarness * h,
    GFunc init, GFunc callback, gpointer data, gulong sleep)
{
  GstHarnessCustomThread *t = g_slice_new0 (GstHarnessCustomThread);
  gst_harness_thread_init (&t->t,
      (GDestroyNotify) gst_harness_custom_thread_free, h, sleep);

  t->init = init;
  t->callback = callback;
  t->data = data;

  GST_HARNESS_THREAD_START (custom, t);
  return &t->t;
}

/**
 * gst_harness_stress_statechange_start_full: (skip)
 * @h: a #GstHarness
 * @sleep: a #gulong specifying how long to sleep in (microseconds) for
 * each state-change
 *
 * Change the state of your harnessed #GstElement from NULL to PLAYING and
 * back again, only pausing for @sleep microseconds every time.
 *
 * MT safe.
 *
 * Returns: a #GstHarnessThread
 *
 * Since: 1.6
 */
GstHarnessThread *
gst_harness_stress_statechange_start_full (GstHarness * h, gulong sleep)
{
  GstHarnessThread *t = g_slice_new0 (GstHarnessThread);
  gst_harness_thread_init (t,
      (GDestroyNotify) gst_harness_thread_free, h, sleep);
  GST_HARNESS_THREAD_START (statechange, t);
  return t;
}

static GstBuffer *
gst_harness_ref_buffer (GstHarness * h, gpointer data)
{
  (void) h;
  return gst_buffer_ref (GST_BUFFER_CAST (data));
}

static GstEvent *
gst_harness_ref_event (GstHarness * h, gpointer data)
{
  (void) h;
  return gst_event_ref (GST_EVENT_CAST (data));
}

/**
 * gst_harness_stress_push_buffer_start_full: (skip)
 * @h: a #GstHarness
 * @caps: a #GstCaps for the #GstBuffer
 * @segment: a #GstSegment
 * @buf: a #GstBuffer to push
 * @sleep: a #gulong specifying how long to sleep in (microseconds) for
 * each call to gst_pad_push
 *
 * Push a #GstBuffer in intervals of @sleep microseconds.
 *
 * MT safe.
 *
 * Returns: a #GstHarnessThread
 *
 * Since: 1.6
 */
GstHarnessThread *
gst_harness_stress_push_buffer_start_full (GstHarness * h,
    GstCaps * caps, const GstSegment * segment, GstBuffer * buf, gulong sleep)
{
  return gst_harness_stress_push_buffer_with_cb_start_full (h, caps, segment,
      gst_harness_ref_buffer, gst_buffer_ref (buf),
      (GDestroyNotify) gst_buffer_unref, sleep);
}

/**
 * gst_harness_stress_push_buffer_with_cb_start_full: (skip)
 * @h: a #GstHarness
 * @caps: a #GstCaps for the #GstBuffer
 * @segment: a #GstSegment
 * @func: a #GstHarnessPrepareBufferFunc function called before every iteration
 * to prepare / create a #GstBuffer for pushing
 * @data: a #gpointer with data to the #GstHarnessPrepareBufferFunc function
 * @notify: a #GDestroyNotify that is called when thread is stopped
 * @sleep: a #gulong specifying how long to sleep in (microseconds) for
 * each call to gst_pad_push
 *
 * Push a #GstBuffer returned by @func in intervals of @sleep microseconds.
 *
 * MT safe.
 *
 * Returns: a #GstHarnessThread
 *
 * Since: 1.6
 */
GstHarnessThread *
gst_harness_stress_push_buffer_with_cb_start_full (GstHarness * h,
    GstCaps * caps, const GstSegment * segment,
    GstHarnessPrepareBufferFunc func, gpointer data, GDestroyNotify notify,
    gulong sleep)
{
  GstHarnessPushBufferThread *t = g_slice_new0 (GstHarnessPushBufferThread);
  gst_harness_thread_init (&t->t,
      (GDestroyNotify) gst_harness_push_buffer_thread_free, h, sleep);

  gst_caps_replace (&t->caps, caps);
  t->segment = *segment;
  t->func = func;
  t->data = data;
  t->notify = notify;

  GST_HARNESS_THREAD_START (buffer, t);
  return &t->t;
}

/**
 * gst_harness_stress_push_event_start_full: (skip)
 * @h: a #GstHarness
 * @event: a #GstEvent to push
 * @sleep: a #gulong specifying how long to sleep in (microseconds) for
 * each gst_event_push with @event
 *
 * Push the @event onto the harnessed #GstElement sinkpad in intervals of
 * @sleep microseconds
 *
 * MT safe.
 *
 * Returns: a #GstHarnessThread
 *
 * Since: 1.6
 */
GstHarnessThread *
gst_harness_stress_push_event_start_full (GstHarness * h,
    GstEvent * event, gulong sleep)
{
  return gst_harness_stress_push_event_with_cb_start_full (h,
      gst_harness_ref_event, gst_event_ref (event),
      (GDestroyNotify) gst_event_unref, sleep);
}

/**
 * gst_harness_stress_push_event_with_cb_start_full: (skip)
 * @h: a #GstHarness
 * @func: a #GstHarnessPrepareEventFunc function called before every iteration
 * to prepare / create a #GstEvent for pushing
 * @data: a #gpointer with data to the #GstHarnessPrepareEventFunc function
 * @notify: a #GDestroyNotify that is called when thread is stopped
 * @sleep: a #gulong specifying how long to sleep in (microseconds) for
 * each call to gst_pad_push
 *
 * Push a #GstEvent returned by @func onto the harnessed #GstElement sinkpad
 * in intervals of @sleep microseconds.
 *
 * MT safe.
 *
 * Returns: a #GstHarnessThread
 *
 * Since: 1.8
 */
GstHarnessThread *
gst_harness_stress_push_event_with_cb_start_full (GstHarness * h,
    GstHarnessPrepareEventFunc func, gpointer data, GDestroyNotify notify,
    gulong sleep)
{
  GstHarnessPushEventThread *t = g_slice_new0 (GstHarnessPushEventThread);
  gst_harness_thread_init (&t->t,
      (GDestroyNotify) gst_harness_push_event_thread_free, h, sleep);

  t->func = func;
  t->data = data;
  t->notify = notify;

  GST_HARNESS_THREAD_START (event, t);
  return &t->t;
}

/**
 * gst_harness_stress_push_upstream_event_start_full: (skip)
 * @h: a #GstHarness
 * @event: a #GstEvent to push
 * @sleep: a #gulong specifying how long to sleep in (microseconds) for
 * each gst_event_push with @event
 *
 * Push the @event onto the harnessed #GstElement srcpad in intervals of
 * @sleep microseconds.
 *
 * MT safe.
 *
 * Returns: a #GstHarnessThread
 *
 * Since: 1.6
 */
GstHarnessThread *
gst_harness_stress_push_upstream_event_start_full (GstHarness * h,
    GstEvent * event, gulong sleep)
{
  return gst_harness_stress_push_upstream_event_with_cb_start_full (h,
      gst_harness_ref_event, gst_event_ref (event),
      (GDestroyNotify) gst_event_unref, sleep);
}

/**
 * gst_harness_stress_push_upstream_event_with_cb_start_full: (skip)
 * @h: a #GstHarness
 * @func: a #GstHarnessPrepareEventFunc function called before every iteration
 * to prepare / create a #GstEvent for pushing
 * @data: a #gpointer with data to the #GstHarnessPrepareEventFunc function
 * @notify: a #GDestroyNotify that is called when thread is stopped
 * @sleep: a #gulong specifying how long to sleep in (microseconds) for
 * each call to gst_pad_push
 *
 * Push a #GstEvent returned by @func onto the harnessed #GstElement srcpad
 * in intervals of @sleep microseconds.
 *
 * MT safe.
 *
 * Returns: a #GstHarnessThread
 *
 * Since: 1.8
 */
GstHarnessThread *
gst_harness_stress_push_upstream_event_with_cb_start_full (GstHarness * h,
    GstHarnessPrepareEventFunc func, gpointer data, GDestroyNotify notify,
    gulong sleep)
{
  GstHarnessPushEventThread *t = g_slice_new0 (GstHarnessPushEventThread);
  gst_harness_thread_init (&t->t,
      (GDestroyNotify) gst_harness_push_event_thread_free, h, sleep);

  t->func = func;
  t->data = data;
  t->notify = notify;

  GST_HARNESS_THREAD_START (upstream_event, t);
  return &t->t;
}

/**
 * gst_harness_stress_property_start_full: (skip)
 * @h: a #GstHarness
 * @name: a #gchar specifying a property name
 * @value: a #GValue to set the property to
 * @sleep: a #gulong specifying how long to sleep in (microseconds) for
 * each g_object_set with @name and @value
 *
 * Call g_object_set with @name and @value in intervals of @sleep microseconds
 *
 * MT safe.
 *
 * Returns: a #GstHarnessThread
 *
 * Since: 1.6
 */
GstHarnessThread *
gst_harness_stress_property_start_full (GstHarness * h,
    const gchar * name, const GValue * value, gulong sleep)
{
  GstHarnessPropThread *t = g_slice_new0 (GstHarnessPropThread);
  gst_harness_thread_init (&t->t,
      (GDestroyNotify) gst_harness_property_thread_free, h, sleep);

  t->name = g_strdup (name);
  g_value_init (&t->value, G_VALUE_TYPE (value));
  g_value_copy (value, &t->value);

  GST_HARNESS_THREAD_START (property, t);
  return &t->t;
}

/**
 * gst_harness_stress_requestpad_start_full: (skip)
 * @h: a #GstHarness
 * @templ: a #GstPadTemplate
 * @name: a #gchar
 * @caps: a #GstCaps
 * @release: a #gboolean
 * @sleep: a #gulong specifying how long to sleep in (microseconds) for
 * each gst_element_request_pad
 *
 * Call gst_element_request_pad in intervals of @sleep microseconds
 *
 * MT safe.
 *
 * Returns: a #GstHarnessThread
 *
 * Since: 1.6
 */
GstHarnessThread *
gst_harness_stress_requestpad_start_full (GstHarness * h,
    GstPadTemplate * templ, const gchar * name, GstCaps * caps,
    gboolean release, gulong sleep)
{
  GstHarnessReqPadThread *t = g_slice_new0 (GstHarnessReqPadThread);
  gst_harness_thread_init (&t->t,
      (GDestroyNotify) gst_harness_requestpad_thread_free, h, sleep);

  t->templ = gst_object_ref (templ);
  t->name = g_strdup (name);
  gst_caps_replace (&t->caps, caps);
  t->release = release;

  GST_HARNESS_THREAD_START (requestpad, t);
  return &t->t;
}
