/* GStreamer
 * Copyright (C) <2005> Thomas Vander Stichele <thomas at apestaart dot org>
 *
 * gstpad.c: Unit test for GstPad
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

#include <gst/check/gstcheck.h>

static GstSegment dummy_segment;

GST_START_TEST (test_link)
{
  GstPad *src, *sink;
  GstPadTemplate *srct;

  GstPadLinkReturn ret;
  gchar *name;

  src = gst_pad_new ("source", GST_PAD_SRC);
  fail_if (src == NULL);
  ASSERT_OBJECT_REFCOUNT (src, "source pad", 1);

  name = gst_pad_get_name (src);
  fail_unless (strcmp (name, "source") == 0);
  ASSERT_OBJECT_REFCOUNT (src, "source pad", 1);
  g_free (name);

  sink = gst_pad_new ("sink", GST_PAD_SINK);
  fail_if (sink == NULL);

  /* linking without templates or caps should work */
  ret = gst_pad_link (src, sink);
  ASSERT_OBJECT_REFCOUNT (src, "source pad", 1);
  ASSERT_OBJECT_REFCOUNT (sink, "sink pad", 1);
  fail_unless (ret == GST_PAD_LINK_OK);

  ASSERT_CRITICAL (gst_pad_get_pad_template (NULL));

  srct = gst_pad_get_pad_template (src);
  fail_unless (srct == NULL);
  ASSERT_OBJECT_REFCOUNT (src, "source pad", 1);

  /* clean up */
  ASSERT_OBJECT_REFCOUNT (src, "source pad", 1);
  gst_object_unref (src);
  gst_object_unref (sink);
}

GST_END_TEST;

/* threaded link/unlink */
/* use globals */
static GstPad *src, *sink;

static void
thread_link_unlink (gpointer data)
{
  THREAD_START ();

  while (THREAD_TEST_RUNNING ()) {
    gst_pad_link (src, sink);
    gst_pad_unlink (src, sink);
    THREAD_SWITCH ();
  }
}

GST_START_TEST (test_link_unlink_threaded)
{
  GstCaps *caps;
  int i;

  src = gst_pad_new ("source", GST_PAD_SRC);
  fail_if (src == NULL);
  sink = gst_pad_new ("sink", GST_PAD_SINK);
  fail_if (sink == NULL);

  caps = gst_caps_from_string ("foo/bar");
  gst_pad_set_active (src, TRUE);
  gst_pad_set_caps (src, caps);
  gst_pad_set_active (sink, TRUE);
  gst_pad_set_caps (sink, caps);
  ASSERT_CAPS_REFCOUNT (caps, "caps", 3);

  MAIN_START_THREADS (5, thread_link_unlink, NULL);
  for (i = 0; i < 1000; ++i) {
    gst_pad_is_linked (src);
    gst_pad_is_linked (sink);
    THREAD_SWITCH ();
  }
  MAIN_STOP_THREADS ();

  ASSERT_CAPS_REFCOUNT (caps, "caps", 3);
  gst_caps_unref (caps);

  ASSERT_CAPS_REFCOUNT (caps, "caps", 2);
  gst_object_unref (src);
  gst_object_unref (sink);
}

GST_END_TEST;

GST_START_TEST (test_refcount)
{
  GstPad *src, *sink;
  GstCaps *caps;
  GstPadLinkReturn plr;

  sink = gst_pad_new ("sink", GST_PAD_SINK);
  fail_if (sink == NULL);

  src = gst_pad_new ("src", GST_PAD_SRC);
  fail_if (src == NULL);

  caps = gst_caps_from_string ("foo/bar");
  /* one for me */
  ASSERT_CAPS_REFCOUNT (caps, "caps", 1);

  /* can't set caps on flushing sinkpad */
  fail_if (gst_pad_set_caps (src, caps) == TRUE);
  fail_if (gst_pad_set_caps (sink, caps) == TRUE);
  /* one for me and one for each set_caps */
  ASSERT_CAPS_REFCOUNT (caps, "caps", 1);

  gst_pad_set_active (src, TRUE);
  fail_unless (gst_pad_set_caps (src, caps) == TRUE);
  ASSERT_CAPS_REFCOUNT (caps, "caps", 2);

  gst_pad_set_active (sink, TRUE);
  fail_unless (gst_pad_set_caps (sink, caps) == TRUE);
  ASSERT_CAPS_REFCOUNT (caps, "caps", 3);

  plr = gst_pad_link (src, sink);
  fail_unless (GST_PAD_LINK_SUCCESSFUL (plr));
  /* src caps added to pending caps on sink */
  ASSERT_CAPS_REFCOUNT (caps, "caps", 3);

  gst_pad_unlink (src, sink);
  ASSERT_CAPS_REFCOUNT (caps, "caps", 3);

  /* cleanup */
  gst_object_unref (src);
  gst_object_unref (sink);
  ASSERT_CAPS_REFCOUNT (caps, "caps", 1);

  gst_caps_unref (caps);
}

GST_END_TEST;

GST_START_TEST (test_get_allowed_caps)
{
  GstPad *src, *sink;
  GstCaps *caps, *gotcaps;
  GstBuffer *buffer;
  GstPadLinkReturn plr;

  ASSERT_CRITICAL (gst_pad_get_allowed_caps (NULL));

  buffer = gst_buffer_new ();
  ASSERT_CRITICAL (gst_pad_get_allowed_caps ((GstPad *) buffer));
  gst_buffer_unref (buffer);

  src = gst_pad_new ("src", GST_PAD_SRC);
  fail_if (src == NULL);
  caps = gst_pad_get_allowed_caps (src);
  fail_unless (caps == NULL);

  caps = gst_caps_from_string ("foo/bar");

  sink = gst_pad_new ("sink", GST_PAD_SINK);
  gst_pad_set_active (src, TRUE);
  /* source pad is active and will accept the caps event */
  fail_unless (gst_pad_set_caps (src, caps) == TRUE);
  /* sink pad is not active and will refuse the caps event */
  fail_if (gst_pad_set_caps (sink, caps) == TRUE);
  ASSERT_CAPS_REFCOUNT (caps, "caps", 2);

  gst_pad_set_active (sink, TRUE);
  /* sink pad is now active and will accept the caps event */
  fail_unless (gst_pad_set_caps (sink, caps) == TRUE);
  ASSERT_CAPS_REFCOUNT (caps, "caps", 3);

  plr = gst_pad_link (src, sink);
  fail_unless (GST_PAD_LINK_SUCCESSFUL (plr));

  gotcaps = gst_pad_get_allowed_caps (src);
  fail_if (gotcaps == NULL);
  fail_unless (gst_caps_is_equal (gotcaps, caps));

  ASSERT_CAPS_REFCOUNT (gotcaps, "gotcaps", 4);
  gst_caps_unref (gotcaps);

  gst_pad_unlink (src, sink);

  /* cleanup */
  ASSERT_CAPS_REFCOUNT (caps, "caps", 3);
  ASSERT_OBJECT_REFCOUNT (src, "src", 1);
  ASSERT_OBJECT_REFCOUNT (sink, "sink", 1);

  gst_object_unref (src);
  gst_object_unref (sink);

  ASSERT_CAPS_REFCOUNT (caps, "caps", 1);
  gst_caps_unref (caps);
}

GST_END_TEST;

static GstCaps *event_caps = NULL;

static gboolean
sticky_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstCaps *caps;

  fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_CAPS
      || GST_EVENT_TYPE (event) == GST_EVENT_STREAM_START
      || GST_EVENT_TYPE (event) == GST_EVENT_SEGMENT);

  if (GST_EVENT_TYPE (event) != GST_EVENT_CAPS) {
    gst_event_unref (event);
    return TRUE;
  }

  /* Ensure we get here just once: */
  fail_unless (event_caps == NULL);

  /* The event must arrive before any buffer: */
  fail_unless_equals_int (g_list_length (buffers), 0);

  gst_event_parse_caps (event, &caps);
  event_caps = gst_caps_ref (caps);

  gst_event_unref (event);

  return TRUE;
}

/* Tests whether caps get properly forwarded when pads
   are initially unlinked */
GST_START_TEST (test_sticky_caps_unlinked)
{
  GstCaps *caps;
  GstPadTemplate *src_template, *sink_template;
  GstPad *src, *sink;
  GstEvent *event;

  caps = gst_caps_from_string ("foo/bar, dummy=(int){1, 2}");
  src_template = gst_pad_template_new ("src", GST_PAD_SRC,
      GST_PAD_ALWAYS, caps);
  sink_template = gst_pad_template_new ("sink", GST_PAD_SINK,
      GST_PAD_ALWAYS, caps);
  gst_caps_unref (caps);

  src = gst_pad_new_from_template (src_template, "src");
  fail_if (src == NULL);
  sink = gst_pad_new_from_template (sink_template, "sink");
  fail_if (sink == NULL);
  gst_pad_set_event_function (sink, sticky_event);
  gst_pad_set_chain_function (sink, gst_check_chain_func);

  gst_object_unref (src_template);
  gst_object_unref (sink_template);

  gst_pad_set_active (src, TRUE);

  fail_unless (gst_pad_push_event (src,
          gst_event_new_stream_start ("test")) == TRUE);

  caps = gst_caps_from_string ("foo/bar, dummy=(int)1");
  ASSERT_CAPS_REFCOUNT (caps, "caps", 1);

  event = gst_event_new_caps (caps);
  fail_unless (gst_pad_push_event (src, event) == TRUE);
  fail_unless (event_caps == NULL);

  fail_unless (gst_pad_push_event (src,
          gst_event_new_segment (&dummy_segment)) == TRUE);

  /* Linking and activating will not forward the sticky event yet... */
  fail_unless (GST_PAD_LINK_SUCCESSFUL (gst_pad_link (src, sink)));
  gst_pad_set_active (sink, TRUE);
  fail_unless (event_caps == NULL);

  /* ...but the first buffer will: */
  fail_unless (gst_pad_push (src, gst_buffer_new ()) == GST_FLOW_OK);
  fail_unless (event_caps == caps);
  fail_unless_equals_int (g_list_length (buffers), 1);

  gst_check_drop_buffers ();

  gst_caps_replace (&caps, NULL);
  gst_caps_replace (&event_caps, NULL);
  ASSERT_OBJECT_REFCOUNT (src, "src", 1);
  ASSERT_OBJECT_REFCOUNT (sink, "sink", 1);
  gst_object_unref (src);
  gst_object_unref (sink);
}

GST_END_TEST;

static gboolean
check_if_caps_is_accepted (GstPad * sink, const gchar * str)
{
  GstCaps *caps;
  gboolean ret;

  caps = gst_caps_from_string (str);
  ASSERT_CAPS_REFCOUNT (caps, "caps", 1);
  ret = gst_pad_query_accept_caps (sink, caps);
  gst_caps_unref (caps);

  return ret;
}

static gboolean
sink_query_caps (GstPad * pad, GstObject * object, GstQuery * q)
{
  gboolean ret;
  GstCaps *caps;

  switch (GST_QUERY_TYPE (q)) {
    case GST_QUERY_CAPS:
      ret = TRUE;
      caps =
          gst_caps_from_string ("foo/bar, dummy=(int)1,"
          " query-only-field=(int)1");
      gst_query_set_caps_result (q, caps);
      gst_caps_unref (caps);
    default:
      ret = gst_pad_query_default (pad, object, q);
      break;
  }

  return ret;
}

/* Tests whether acceptcaps default handler works properly
   with all 4 possible flag combinations */
GST_START_TEST (test_default_accept_caps)
{
  GstCaps *caps;
  GstPadTemplate *sink_template;
  GstPad *sink;

  caps = gst_caps_from_string ("foo/bar, dummy=(int){1, 2}");
  sink_template = gst_pad_template_new ("sink", GST_PAD_SINK,
      GST_PAD_ALWAYS, caps);
  gst_caps_unref (caps);

  sink = gst_pad_new_from_template (sink_template, "sink");
  fail_if (sink == NULL);
  gst_pad_set_query_function (sink, sink_query_caps);

  gst_object_unref (sink_template);

  gst_pad_set_active (sink, TRUE);

  /* 1. Check with caps query, subset check */
  GST_PAD_UNSET_ACCEPT_INTERSECT (sink);
  GST_PAD_UNSET_ACCEPT_TEMPLATE (sink);
  fail_unless (check_if_caps_is_accepted (sink, "foo/bar, dummy=(int)1"));
  fail_if (check_if_caps_is_accepted (sink, "foo/bar, dummy=(int)3"));
  fail_unless (check_if_caps_is_accepted (sink,
          "foo/bar, dummy=(int)1, query-only-field=(int)1"));
  fail_if (check_if_caps_is_accepted (sink, "foo/bar, extra-field=(int)1"));

  /* 2. Check with caps query, intersect check */
  GST_PAD_SET_ACCEPT_INTERSECT (sink);
  GST_PAD_UNSET_ACCEPT_TEMPLATE (sink);
  fail_unless (check_if_caps_is_accepted (sink, "foo/bar, dummy=(int)1"));
  fail_if (check_if_caps_is_accepted (sink, "foo/bar, dummy=(int)3"));
  fail_unless (check_if_caps_is_accepted (sink,
          "foo/bar, dummy=(int)1, query-only-field=(int)1"));
  fail_unless (check_if_caps_is_accepted (sink, "foo/bar, extra-field=(int)1"));

  /* 3. Check with template caps, subset check */
  GST_PAD_UNSET_ACCEPT_INTERSECT (sink);
  GST_PAD_SET_ACCEPT_TEMPLATE (sink);
  fail_unless (check_if_caps_is_accepted (sink, "foo/bar, dummy=(int)1"));
  fail_if (check_if_caps_is_accepted (sink, "foo/bar, dummy=(int)3"));
  fail_unless (check_if_caps_is_accepted (sink,
          "foo/bar, dummy=(int)1, query-only-field=(int)1"));
  fail_if (check_if_caps_is_accepted (sink, "foo/bar, extra-field=(int)1"));

  /* 3. Check with template caps, intersect check */
  GST_PAD_SET_ACCEPT_INTERSECT (sink);
  GST_PAD_SET_ACCEPT_TEMPLATE (sink);
  fail_unless (check_if_caps_is_accepted (sink, "foo/bar, dummy=(int)1"));
  fail_if (check_if_caps_is_accepted (sink, "foo/bar, dummy=(int)3"));
  fail_unless (check_if_caps_is_accepted (sink,
          "foo/bar, dummy=(int)1, query-only-field=(int)1"));
  fail_unless (check_if_caps_is_accepted (sink, "foo/bar, extra-field=(int)1"));

  ASSERT_OBJECT_REFCOUNT (sink, "sink", 1);
  gst_object_unref (sink);
}

GST_END_TEST;

/* Same as test_sticky_caps_unlinked except that the source pad
 * has a template of ANY and we will attempt to push
 * incompatible caps */
GST_START_TEST (test_sticky_caps_unlinked_incompatible)
{
  GstCaps *caps, *failcaps;
  GstPadTemplate *src_template, *sink_template;
  GstPad *src, *sink;
  GstEvent *event;

  /* Source pad has ANY caps
   * Sink pad has foobar caps
   * We will push the pony express caps (which should fail)
   */
  caps = gst_caps_new_any ();
  src_template = gst_pad_template_new ("src", GST_PAD_SRC,
      GST_PAD_ALWAYS, caps);
  gst_caps_unref (caps);
  caps = gst_caps_from_string ("foo/bar, dummy=(int){1, 2}");
  sink_template = gst_pad_template_new ("sink", GST_PAD_SINK,
      GST_PAD_ALWAYS, caps);
  gst_caps_unref (caps);

  src = gst_pad_new_from_template (src_template, "src");
  fail_if (src == NULL);
  sink = gst_pad_new_from_template (sink_template, "sink");
  fail_if (sink == NULL);
  gst_pad_set_event_function (sink, sticky_event);
  gst_pad_set_chain_function (sink, gst_check_chain_func);

  gst_object_unref (src_template);
  gst_object_unref (sink_template);

  gst_pad_set_active (src, TRUE);

  fail_unless (gst_pad_push_event (src,
          gst_event_new_stream_start ("test")) == TRUE);

  failcaps = gst_caps_from_string ("pony/express, failure=(boolean)true");
  ASSERT_CAPS_REFCOUNT (failcaps, "caps", 1);

  event = gst_event_new_caps (failcaps);
  gst_caps_unref (failcaps);
  /* The pad isn't linked yet, and anything matches the source pad template
   * (which is ANY) */
  fail_unless (gst_pad_push_event (src, event) == TRUE);
  fail_unless (event_caps == NULL);

  fail_unless (gst_pad_push_event (src,
          gst_event_new_segment (&dummy_segment)) == TRUE);

  /* Linking and activating will not forward the sticky event yet... */
  fail_unless (GST_PAD_LINK_SUCCESSFUL (gst_pad_link (src, sink)));
  gst_pad_set_active (sink, TRUE);
  fail_unless (event_caps == NULL);

  /* ...but the first buffer will and should FAIL since the caps 
   * are not compatible */
  fail_unless (gst_pad_push (src,
          gst_buffer_new ()) == GST_FLOW_NOT_NEGOTIATED);
  /* We shouldn't have received the caps event since it's incompatible */
  fail_unless (event_caps == NULL);
  /* We shouldn't have received any buffers since caps are incompatible */
  fail_unless_equals_int (g_list_length (buffers), 0);

  gst_check_drop_buffers ();

  gst_caps_replace (&event_caps, NULL);

  ASSERT_OBJECT_REFCOUNT (src, "src", 1);
  ASSERT_OBJECT_REFCOUNT (sink, "sink", 1);
  gst_object_unref (src);
  gst_object_unref (sink);
}

GST_END_TEST;

/* Like test_sticky_caps_unlinked, but link before caps: */

GST_START_TEST (test_sticky_caps_flushing)
{
  GstCaps *caps;
  GstPadTemplate *src_template, *sink_template;
  GstPad *src, *sink;
  GstEvent *event;

  caps = gst_caps_from_string ("foo/bar, dummy=(int){1, 2}");
  src_template = gst_pad_template_new ("src", GST_PAD_SRC,
      GST_PAD_ALWAYS, caps);
  sink_template = gst_pad_template_new ("sink", GST_PAD_SINK,
      GST_PAD_ALWAYS, caps);
  gst_caps_unref (caps);

  src = gst_pad_new_from_template (src_template, "src");
  fail_if (src == NULL);
  sink = gst_pad_new_from_template (sink_template, "sink");
  fail_if (sink == NULL);
  gst_pad_set_event_function (sink, sticky_event);
  gst_pad_set_chain_function (sink, gst_check_chain_func);

  gst_object_unref (src_template);
  gst_object_unref (sink_template);

  fail_unless (GST_PAD_LINK_SUCCESSFUL (gst_pad_link (src, sink)));

  caps = gst_caps_from_string ("foo/bar, dummy=(int)1");
  ASSERT_CAPS_REFCOUNT (caps, "caps", 1);

  event = gst_event_new_caps (caps);

  gst_pad_set_active (src, TRUE);
  fail_unless (gst_pad_push_event (src,
          gst_event_new_stream_start ("test")) == TRUE);
  /* The caps event gets accepted by the source pad (and stored) */
  fail_unless (gst_pad_push_event (src, event) == TRUE);
  /* But wasn't forwarded since the sink pad is flushing (not activated) */
  fail_unless (event_caps == NULL);

  fail_unless (gst_pad_push_event (src,
          gst_event_new_segment (&dummy_segment)) == TRUE);

  /* Activating will not forward the sticky event yet... */
  gst_pad_set_active (sink, TRUE);
  fail_unless (event_caps == NULL);

  /* ...but the first buffer will: */
  fail_unless (gst_pad_push (src, gst_buffer_new ()) == GST_FLOW_OK);
  fail_unless (event_caps == caps);
  fail_unless_equals_int (g_list_length (buffers), 1);

  gst_check_drop_buffers ();

  gst_caps_replace (&caps, NULL);
  gst_caps_replace (&event_caps, NULL);

  ASSERT_OBJECT_REFCOUNT (src, "src", 1);
  ASSERT_OBJECT_REFCOUNT (sink, "sink", 1);
  gst_object_unref (src);
  gst_object_unref (sink);
}

GST_END_TEST;

static gboolean
name_is_valid (const gchar * name, GstPadPresence presence)
{
  GstPadTemplate *new;
  GstCaps *any = gst_caps_new_any ();

  new = gst_pad_template_new (name, GST_PAD_SRC, presence, any);
  gst_caps_unref (any);
  if (new) {
    gst_object_unref (GST_OBJECT (new));
    return TRUE;
  }
  return FALSE;
}

GST_START_TEST (test_name_is_valid)
{
  gboolean result = FALSE;

  fail_unless (name_is_valid ("src", GST_PAD_ALWAYS));
  ASSERT_WARNING (name_is_valid ("src%", GST_PAD_ALWAYS));
  ASSERT_WARNING (result = name_is_valid ("src%d", GST_PAD_ALWAYS));
  fail_if (result);

  fail_unless (name_is_valid ("src", GST_PAD_REQUEST));
  ASSERT_WARNING (name_is_valid ("src%s%s", GST_PAD_REQUEST));
  ASSERT_WARNING (name_is_valid ("src%c", GST_PAD_REQUEST));
  ASSERT_WARNING (name_is_valid ("src%", GST_PAD_REQUEST));
  fail_unless (name_is_valid ("src%dsrc", GST_PAD_REQUEST));

  fail_unless (name_is_valid ("src", GST_PAD_SOMETIMES));
  fail_unless (name_is_valid ("src%c", GST_PAD_SOMETIMES));
}

GST_END_TEST;

static GstPadProbeReturn
_probe_handler (GstPad * pad, GstPadProbeInfo * info, gpointer userdata)
{
  GstPadProbeReturn ret = (GstPadProbeReturn) GPOINTER_TO_INT (userdata);

  /* If we are handling the data, we unref it */
  if (ret == GST_PAD_PROBE_HANDLED
      && !(GST_PAD_PROBE_INFO_TYPE (info) & GST_PAD_PROBE_TYPE_QUERY_BOTH)) {
    GST_DEBUG_OBJECT (pad, "Unreffing data");
    gst_mini_object_unref (info->data);
  }
  return ret;
}

static GstPadProbeReturn
_handled_probe_handler (GstPad * pad, GstPadProbeInfo * info, gpointer userdata)
{
  GstFlowReturn customflow = (GstFlowReturn) GPOINTER_TO_INT (userdata);

  /* We are handling the data, we unref it */
  if (!(GST_PAD_PROBE_INFO_TYPE (info) & GST_PAD_PROBE_TYPE_QUERY_BOTH))
    gst_mini_object_unref (info->data);
  GST_PAD_PROBE_INFO_FLOW_RETURN (info) = customflow;

  return GST_PAD_PROBE_HANDLED;
}



GST_START_TEST (test_events_query_unlinked)
{
  GstPad *src;
  GstCaps *caps;
  gulong id;
  GstQuery *query;

  src = gst_pad_new ("src", GST_PAD_SRC);
  fail_if (src == NULL);
  caps = gst_pad_get_allowed_caps (src);
  fail_unless (caps == NULL);

  caps = gst_caps_from_string ("foo/bar");

  gst_pad_set_active (src, TRUE);
  fail_unless (gst_pad_push_event (src,
          gst_event_new_stream_start ("test")) == TRUE);
  gst_pad_set_caps (src, caps);
  ASSERT_CAPS_REFCOUNT (caps, "caps", 2);
  fail_unless (gst_pad_push_event (src,
          gst_event_new_segment (&dummy_segment)) == TRUE);
  ASSERT_CAPS_REFCOUNT (caps, "caps", 2);

  /* Doing a query on an unlinked pad will return FALSE */
  query = gst_query_new_duration (GST_FORMAT_TIME);
  fail_unless (gst_pad_peer_query (src, query) == FALSE);
  ASSERT_MINI_OBJECT_REFCOUNT (query, "query", 1);
  gst_query_unref (query);

  /* Add a probe that returns _DROP will make the event push return TRUE
   * even if not linked */
  GST_DEBUG ("event/query DROP");
  id = gst_pad_add_probe (src,
      GST_PAD_PROBE_TYPE_EVENT_BOTH | GST_PAD_PROBE_TYPE_QUERY_BOTH,
      _probe_handler, GINT_TO_POINTER (GST_PAD_PROBE_DROP), NULL);
  fail_unless (gst_pad_push_event (src,
          gst_event_new_segment (&dummy_segment)) == TRUE);
  /* Queries should stil fail */
  query = gst_query_new_duration (GST_FORMAT_TIME);
  fail_unless (gst_pad_peer_query (src, query) == FALSE);
  ASSERT_MINI_OBJECT_REFCOUNT (query, "query", 1);
  gst_query_unref (query);
  gst_pad_remove_probe (src, id);

  /* Add a probe that returns _HANDLED will make the event push return TRUE
   * even if not linked */
  GST_DEBUG ("event/query HANDLED");
  id = gst_pad_add_probe (src,
      GST_PAD_PROBE_TYPE_EVENT_BOTH | GST_PAD_PROBE_TYPE_QUERY_BOTH,
      _probe_handler, GINT_TO_POINTER (GST_PAD_PROBE_HANDLED), NULL);
  fail_unless (gst_pad_push_event (src,
          gst_event_new_segment (&dummy_segment)) == TRUE);

  /* Queries will succeed */
  query = gst_query_new_duration (GST_FORMAT_TIME);
  fail_unless (gst_pad_peer_query (src, query) == TRUE);
  ASSERT_MINI_OBJECT_REFCOUNT (query, "query", 1);
  gst_query_unref (query);
  gst_pad_remove_probe (src, id);

  /* cleanup */
  ASSERT_CAPS_REFCOUNT (caps, "caps", 2);
  ASSERT_OBJECT_REFCOUNT (src, "src", 1);

  gst_object_unref (src);

  ASSERT_CAPS_REFCOUNT (caps, "caps", 1);
  gst_caps_unref (caps);
}

GST_END_TEST;

GST_START_TEST (test_push_unlinked)
{
  GstPad *src;
  GstCaps *caps;
  GstBuffer *buffer;
  gulong id;
  GstFlowReturn fl;

  src = gst_pad_new ("src", GST_PAD_SRC);
  fail_if (src == NULL);
  caps = gst_pad_get_allowed_caps (src);
  fail_unless (caps == NULL);

  caps = gst_caps_from_string ("foo/bar");

  /* pushing on an inactive pad will return wrong state */
  GST_DEBUG ("push buffer inactive");
  buffer = gst_buffer_new ();
  gst_buffer_ref (buffer);
  fail_unless (gst_pad_push (src, buffer) == GST_FLOW_FLUSHING);
  ASSERT_MINI_OBJECT_REFCOUNT (buffer, "buffer", 1);
  gst_buffer_unref (buffer);

  gst_pad_set_active (src, TRUE);
  fail_unless (gst_pad_push_event (src,
          gst_event_new_stream_start ("test")) == TRUE);
  GST_DEBUG ("push caps event inactive");
  gst_pad_set_caps (src, caps);
  ASSERT_CAPS_REFCOUNT (caps, "caps", 2);
  fail_unless (gst_pad_push_event (src,
          gst_event_new_segment (&dummy_segment)) == TRUE);

  /* pushing on an unlinked pad will drop the buffer */
  GST_DEBUG ("push buffer unlinked");
  buffer = gst_buffer_new ();
  gst_buffer_ref (buffer);
  fail_unless (gst_pad_push (src, buffer) == GST_FLOW_NOT_LINKED);
  ASSERT_MINI_OBJECT_REFCOUNT (buffer, "buffer", 1);
  gst_buffer_unref (buffer);

  /* adding a probe that returns _DROP will drop the buffer without trying
   * to chain */
  GST_DEBUG ("push buffer drop");
  id = gst_pad_add_probe (src, GST_PAD_PROBE_TYPE_BUFFER,
      _probe_handler, GINT_TO_POINTER (GST_PAD_PROBE_DROP), NULL);
  buffer = gst_buffer_new ();
  gst_buffer_ref (buffer);
  fail_unless (gst_pad_push (src, buffer) == GST_FLOW_OK);
  ASSERT_MINI_OBJECT_REFCOUNT (buffer, "buffer", 1);
  gst_buffer_unref (buffer);
  gst_pad_remove_probe (src, id);

  /* adding a probe that returns _HANDLED will drop the buffer without trying
   * to chain */
  GST_DEBUG ("push buffer handled");
  id = gst_pad_add_probe (src, GST_PAD_PROBE_TYPE_BUFFER,
      _probe_handler, GINT_TO_POINTER (GST_PAD_PROBE_HANDLED), NULL);
  buffer = gst_buffer_new ();
  gst_buffer_ref (buffer);
  fail_unless (gst_pad_push (src, buffer) == GST_FLOW_OK);
  ASSERT_MINI_OBJECT_REFCOUNT (buffer, "buffer", 1);
  gst_buffer_unref (buffer);
  gst_pad_remove_probe (src, id);

  /* adding a probe that returns _OK will still chain the buffer,
   * and hence drop because pad is unlinked */
  GST_DEBUG ("push buffer ok");
  id = gst_pad_add_probe (src, GST_PAD_PROBE_TYPE_BUFFER,
      _probe_handler, GINT_TO_POINTER (GST_PAD_PROBE_OK), NULL);
  buffer = gst_buffer_new ();
  gst_buffer_ref (buffer);
  fail_unless (gst_pad_push (src, buffer) == GST_FLOW_NOT_LINKED);
  ASSERT_MINI_OBJECT_REFCOUNT (buffer, "buffer", 1);
  gst_buffer_unref (buffer);
  gst_pad_remove_probe (src, id);

  GST_DEBUG ("push buffer handled and custom return");
  for (fl = GST_FLOW_NOT_SUPPORTED; fl <= GST_FLOW_OK; fl += 1) {
    GST_DEBUG ("Testing with %s", gst_flow_get_name (fl));
    id = gst_pad_add_probe (src, GST_PAD_PROBE_TYPE_BUFFER,
        _handled_probe_handler, GINT_TO_POINTER (fl), NULL);
    buffer = gst_buffer_new ();
    gst_buffer_ref (buffer);
    fail_unless (gst_pad_push (src, buffer) == fl);
    ASSERT_MINI_OBJECT_REFCOUNT (buffer, "buffer", 1);
    gst_buffer_unref (buffer);
    gst_pad_remove_probe (src, id);

  }


  /* cleanup */
  ASSERT_CAPS_REFCOUNT (caps, "caps", 2);
  ASSERT_OBJECT_REFCOUNT (src, "src", 1);

  gst_object_unref (src);

  ASSERT_CAPS_REFCOUNT (caps, "caps", 1);
  gst_caps_unref (caps);
}

GST_END_TEST;

GST_START_TEST (test_push_linked)
{
  GstPad *src, *sink;
  GstPadLinkReturn plr;
  GstCaps *caps;
  GstBuffer *buffer;
  gulong id;

  /* setup */
  sink = gst_pad_new ("sink", GST_PAD_SINK);
  fail_if (sink == NULL);
  gst_pad_set_chain_function (sink, gst_check_chain_func);

  src = gst_pad_new ("src", GST_PAD_SRC);
  fail_if (src == NULL);

  caps = gst_caps_from_string ("foo/bar");
  /* one for me */
  ASSERT_CAPS_REFCOUNT (caps, "caps", 1);

  gst_pad_set_active (src, TRUE);

  fail_unless (gst_pad_push_event (src,
          gst_event_new_stream_start ("test")) == TRUE);

  gst_pad_set_caps (src, caps);

  fail_unless (gst_pad_push_event (src,
          gst_event_new_segment (&dummy_segment)) == TRUE);

  gst_pad_set_active (sink, TRUE);
  /* one for me and one for each set_caps */
  ASSERT_CAPS_REFCOUNT (caps, "caps", 2);

  plr = gst_pad_link (src, sink);
  fail_unless (GST_PAD_LINK_SUCCESSFUL (plr));
  ASSERT_CAPS_REFCOUNT (caps, "caps", 2);

  buffer = gst_buffer_new ();

  /* test */
  /* pushing on a linked pad will drop the ref to the buffer */
  gst_buffer_ref (buffer);
  fail_unless (gst_pad_push (src, buffer) == GST_FLOW_OK);
  ASSERT_MINI_OBJECT_REFCOUNT (buffer, "buffer", 2);
  gst_buffer_unref (buffer);
  fail_unless_equals_int (g_list_length (buffers), 1);
  buffer = GST_BUFFER (buffers->data);
  ASSERT_MINI_OBJECT_REFCOUNT (buffer, "buffer", 1);
  gst_buffer_unref (buffer);
  g_list_free (buffers);
  buffers = NULL;

  /* adding a probe that returns _DROP will drop the buffer without trying
   * to chain */
  id = gst_pad_add_probe (src, GST_PAD_PROBE_TYPE_BUFFER,
      _probe_handler, GINT_TO_POINTER (GST_PAD_PROBE_DROP), NULL);
  buffer = gst_buffer_new ();
  gst_buffer_ref (buffer);
  fail_unless (gst_pad_push (src, buffer) == GST_FLOW_OK);
  ASSERT_MINI_OBJECT_REFCOUNT (buffer, "buffer", 1);
  gst_buffer_unref (buffer);
  gst_pad_remove_probe (src, id);
  fail_unless_equals_int (g_list_length (buffers), 0);

  /* adding a probe that returns _OK will still chain the buffer */
  id = gst_pad_add_probe (src, GST_PAD_PROBE_TYPE_BUFFER,
      _probe_handler, GINT_TO_POINTER (GST_PAD_PROBE_OK), NULL);
  buffer = gst_buffer_new ();
  gst_buffer_ref (buffer);
  fail_unless (gst_pad_push (src, buffer) == GST_FLOW_OK);
  gst_pad_remove_probe (src, id);

  ASSERT_MINI_OBJECT_REFCOUNT (buffer, "buffer", 2);
  gst_buffer_unref (buffer);
  fail_unless_equals_int (g_list_length (buffers), 1);
  buffer = GST_BUFFER (buffers->data);
  ASSERT_MINI_OBJECT_REFCOUNT (buffer, "buffer", 1);
  gst_buffer_unref (buffer);
  g_list_free (buffers);
  buffers = NULL;

  /* adding a probe that returns _HANDLED will not chain the buffer */
  id = gst_pad_add_probe (src, GST_PAD_PROBE_TYPE_BUFFER,
      _probe_handler, GINT_TO_POINTER (GST_PAD_PROBE_HANDLED), NULL);
  buffer = gst_buffer_new ();
  gst_buffer_ref (buffer);
  fail_unless (gst_pad_push (src, buffer) == GST_FLOW_OK);
  gst_pad_remove_probe (src, id);

  ASSERT_MINI_OBJECT_REFCOUNT (buffer, "buffer", 1);
  gst_buffer_unref (buffer);
  fail_unless_equals_int (g_list_length (buffers), 0);
  g_list_free (buffers);
  buffers = NULL;

  /* teardown */
  gst_check_drop_buffers ();
  gst_pad_unlink (src, sink);
  ASSERT_CAPS_REFCOUNT (caps, "caps", 2);
  gst_object_unref (src);
  gst_object_unref (sink);
  ASSERT_CAPS_REFCOUNT (caps, "caps", 1);

  gst_caps_unref (caps);
}

GST_END_TEST;

GST_START_TEST (test_push_linked_flushing)
{
  GstPad *src, *sink;
  GstCaps *caps;
  GstPadLinkReturn plr;
  GstBuffer *buffer;
  gulong id;

  /* setup */
  src = gst_pad_new ("src", GST_PAD_SRC);
  fail_if (src == NULL);
  sink = gst_pad_new ("sink", GST_PAD_SINK);
  fail_if (sink == NULL);
  gst_pad_set_chain_function (sink, gst_check_chain_func);

  caps = gst_pad_get_allowed_caps (src);
  fail_unless (caps == NULL);
  caps = gst_pad_get_allowed_caps (sink);
  fail_unless (caps == NULL);

  caps = gst_caps_from_string ("foo/bar");
  /* one for me */
  ASSERT_CAPS_REFCOUNT (caps, "caps", 1);

  gst_pad_set_active (src, TRUE);
  fail_unless (gst_pad_push_event (src,
          gst_event_new_stream_start ("test")) == TRUE);
  gst_pad_set_caps (src, caps);
  fail_unless (gst_pad_push_event (src,
          gst_event_new_segment (&dummy_segment)) == TRUE);
  /* need to activate to make it accept the caps */
  gst_pad_set_active (sink, TRUE);
  /* one for me and one for each set_caps */
  ASSERT_CAPS_REFCOUNT (caps, "caps", 2);

  plr = gst_pad_link (src, sink);
  fail_unless (GST_PAD_LINK_SUCCESSFUL (plr));
  ASSERT_CAPS_REFCOUNT (caps, "caps", 2);

  /* not activating the pads here, which keeps them flushing */
  gst_pad_set_active (src, FALSE);
  gst_pad_set_active (sink, FALSE);

  /* pushing on a flushing pad will drop the buffer */
  buffer = gst_buffer_new ();
  gst_buffer_ref (buffer);
  fail_unless (gst_pad_push (src, buffer) == GST_FLOW_FLUSHING);
  ASSERT_MINI_OBJECT_REFCOUNT (buffer, "buffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 0);
  gst_buffer_unref (buffer);

  gst_pad_set_active (src, TRUE);
  gst_pad_set_active (sink, FALSE);

  fail_unless (gst_pad_push_event (src,
          gst_event_new_stream_start ("test")) == TRUE);
  gst_pad_set_caps (src, caps);
  fail_unless (gst_pad_push_event (src,
          gst_event_new_segment (&dummy_segment)) == TRUE);
  /* adding a probe that returns _DROP will drop the buffer without trying
   * to chain */
  id = gst_pad_add_probe (src, GST_PAD_PROBE_TYPE_BUFFER, _probe_handler,
      GINT_TO_POINTER (GST_PAD_PROBE_DROP), NULL);
  buffer = gst_buffer_new ();
  gst_buffer_ref (buffer);
  fail_unless (gst_pad_push (src, buffer) == GST_FLOW_FLUSHING);
  ASSERT_MINI_OBJECT_REFCOUNT (buffer, "buffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 0);
  gst_buffer_unref (buffer);
  gst_pad_remove_probe (src, id);

  /* adding a probe that returns _OK will still chain the buffer,
   * and hence drop because pad is flushing */
  id = gst_pad_add_probe (src, GST_PAD_PROBE_TYPE_BUFFER, _probe_handler,
      GINT_TO_POINTER (GST_PAD_PROBE_OK), NULL);
  buffer = gst_buffer_new ();
  gst_buffer_ref (buffer);
  fail_unless (gst_pad_push (src, buffer) == GST_FLOW_FLUSHING);
  ASSERT_MINI_OBJECT_REFCOUNT (buffer, "buffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 0);
  gst_buffer_unref (buffer);
  gst_pad_remove_probe (src, id);

  /* cleanup */
  gst_check_drop_buffers ();
  ASSERT_CAPS_REFCOUNT (caps, "caps", 2);
  ASSERT_OBJECT_REFCOUNT (src, "src", 1);
  gst_pad_link (src, sink);
  gst_object_unref (src);
  gst_object_unref (sink);
  ASSERT_CAPS_REFCOUNT (caps, "caps", 1);
  gst_caps_unref (caps);
}

GST_END_TEST;

static GstBuffer *
buffer_from_string (const gchar * str)
{
  guint size;
  GstBuffer *buf;

  size = strlen (str);
  buf = gst_buffer_new_and_alloc (size);

  gst_buffer_fill (buf, 0, str, size);

  return buf;
}

static gboolean
buffer_compare (GstBuffer * buf, const gchar * str, gsize size)
{
  gboolean res;
  GstMapInfo info;

  fail_unless (gst_buffer_map (buf, &info, GST_MAP_READ));
  res = memcmp (info.data, str, size) == 0;
  GST_MEMDUMP ("buffer  data", info.data, size);
  GST_MEMDUMP ("compare data", (guint8 *) str, size);
  GST_DEBUG ("buffers match: %s", res ? "yes" : "no");
  gst_buffer_unmap (buf, &info);

  return res;
}

GST_START_TEST (test_push_buffer_list_compat)
{
  GstPad *src, *sink;
  GstPadLinkReturn plr;
  GstCaps *caps;
  GstBufferList *list;
  GstBuffer *buffer;

  /* setup */
  sink = gst_pad_new ("sink", GST_PAD_SINK);
  fail_if (sink == NULL);
  gst_pad_set_chain_function (sink, gst_check_chain_func);
  /* leave chainlistfunc unset */

  src = gst_pad_new ("src", GST_PAD_SRC);
  fail_if (src == NULL);

  caps = gst_caps_from_string ("foo/bar");

  gst_pad_set_active (src, TRUE);

  fail_unless (gst_pad_push_event (src,
          gst_event_new_stream_start ("test")) == TRUE);

  gst_pad_set_caps (src, caps);

  fail_unless (gst_pad_push_event (src,
          gst_event_new_segment (&dummy_segment)) == TRUE);

  gst_pad_set_active (sink, TRUE);

  plr = gst_pad_link (src, sink);
  fail_unless (GST_PAD_LINK_SUCCESSFUL (plr));

  list = gst_buffer_list_new ();

  /* test */
  /* adding to a buffer list will drop the ref to the buffer */
  gst_buffer_list_add (list, buffer_from_string ("ListGroup"));
  gst_buffer_list_add (list, buffer_from_string ("AnotherListGroup"));

  fail_unless (gst_pad_push_list (src, list) == GST_FLOW_OK);
  fail_unless_equals_int (g_list_length (buffers), 2);
  buffer = GST_BUFFER (buffers->data);
  ASSERT_MINI_OBJECT_REFCOUNT (buffer, "buffer", 1);
  fail_unless (buffer_compare (buffer, "ListGroup", 9));
  gst_buffer_unref (buffer);
  buffers = g_list_delete_link (buffers, buffers);
  buffer = GST_BUFFER (buffers->data);
  ASSERT_MINI_OBJECT_REFCOUNT (buffer, "buffer", 1);
  fail_unless (buffer_compare (buffer, "AnotherListGroup", 16));
  gst_buffer_unref (buffer);
  buffers = g_list_delete_link (buffers, buffers);
  fail_unless (buffers == NULL);

  /* teardown */
  gst_check_drop_buffers ();
  gst_pad_unlink (src, sink);
  gst_object_unref (src);
  gst_object_unref (sink);
  ASSERT_CAPS_REFCOUNT (caps, "caps", 1);
  gst_caps_unref (caps);
}

GST_END_TEST;

GST_START_TEST (test_flowreturn)
{
  GstFlowReturn ret;
  GQuark quark;

  /* test some of the macros */
  ret = GST_FLOW_EOS;
  fail_if (strcmp (gst_flow_get_name (ret), "eos"));
  quark = gst_flow_to_quark (ret);
  fail_if (strcmp (g_quark_to_string (quark), "eos"));

  /* custom returns */
  ret = GST_FLOW_CUSTOM_SUCCESS;
  fail_if (strcmp (gst_flow_get_name (ret), "custom-success"));
  quark = gst_flow_to_quark (ret);
  fail_if (strcmp (g_quark_to_string (quark), "custom-success"));

  ret = GST_FLOW_CUSTOM_ERROR;
  fail_if (strcmp (gst_flow_get_name (ret), "custom-error"));
  quark = gst_flow_to_quark (ret);
  fail_if (strcmp (g_quark_to_string (quark), "custom-error"));

  /* custom returns clamping */
  ret = GST_FLOW_CUSTOM_SUCCESS + 2;
  fail_if (strcmp (gst_flow_get_name (ret), "custom-success"));
  quark = gst_flow_to_quark (ret);
  fail_if (strcmp (g_quark_to_string (quark), "custom-success"));

  ret = GST_FLOW_CUSTOM_ERROR - 2;
  fail_if (strcmp (gst_flow_get_name (ret), "custom-error"));
  quark = gst_flow_to_quark (ret);
  fail_if (strcmp (g_quark_to_string (quark), "custom-error"));

  /* unknown values */
  ret = GST_FLOW_CUSTOM_ERROR + 2;
  fail_if (strcmp (gst_flow_get_name (ret), "unknown"));
  quark = gst_flow_to_quark (ret);
  fail_unless (quark == 0);
}

GST_END_TEST;

GST_START_TEST (test_push_negotiation)
{
  GstPad *src, *sink;
  GstPadLinkReturn plr;
  GstCaps *srccaps =
      gst_caps_from_string ("audio/x-raw,width={16,32},depth={16,32}");
  GstCaps *sinkcaps =
      gst_caps_from_string ("audio/x-raw,width=32,depth={16,32}");
  GstPadTemplate *src_template;
  GstPadTemplate *sink_template;
  GstCaps *caps;

  /* setup */
  src_template = gst_pad_template_new ("src", GST_PAD_SRC,
      GST_PAD_ALWAYS, srccaps);
  sink_template = gst_pad_template_new ("sink", GST_PAD_SINK,
      GST_PAD_ALWAYS, sinkcaps);
  gst_caps_unref (srccaps);
  gst_caps_unref (sinkcaps);

  sink = gst_pad_new_from_template (sink_template, "sink");
  fail_if (sink == NULL);
  gst_pad_set_chain_function (sink, gst_check_chain_func);

  src = gst_pad_new_from_template (src_template, "src");
  fail_if (src == NULL);

  plr = gst_pad_link (src, sink);
  fail_unless (GST_PAD_LINK_SUCCESSFUL (plr));

  /* activate pads */
  gst_pad_set_active (src, TRUE);
  gst_pad_set_active (sink, TRUE);

  caps = gst_caps_from_string ("audio/x-raw,width=16,depth=16");

  /* Should fail if src pad caps are incompatible with sink pad caps */
  gst_pad_set_caps (src, caps);
  fail_unless (gst_pad_set_caps (sink, caps) == FALSE);

  /* teardown */
  gst_check_drop_buffers ();
  gst_pad_unlink (src, sink);
  gst_object_unref (src);
  gst_object_unref (sink);
  gst_caps_unref (caps);
  gst_object_unref (sink_template);
  gst_object_unref (src_template);
}

GST_END_TEST;

/* see that an unref also unlinks the pads */
GST_START_TEST (test_src_unref_unlink)
{
  GstPad *src, *sink;
  GstCaps *caps;
  GstPadLinkReturn plr;

  sink = gst_pad_new ("sink", GST_PAD_SINK);
  fail_if (sink == NULL);

  src = gst_pad_new ("src", GST_PAD_SRC);
  fail_if (src == NULL);

  caps = gst_caps_from_string ("foo/bar");

  gst_pad_set_active (src, TRUE);
  gst_pad_set_caps (src, caps);
  gst_pad_set_active (sink, TRUE);
  gst_pad_set_caps (sink, caps);

  plr = gst_pad_link (src, sink);
  fail_unless (GST_PAD_LINK_SUCCESSFUL (plr));

  /* unref the srcpad */
  gst_object_unref (src);

  /* sink should be unlinked now */
  fail_if (gst_pad_is_linked (sink));

  /* cleanup */
  gst_object_unref (sink);
  gst_caps_unref (caps);
}

GST_END_TEST;

/* see that an unref also unlinks the pads */
GST_START_TEST (test_sink_unref_unlink)
{
  GstPad *src, *sink;
  GstCaps *caps;
  GstPadLinkReturn plr;

  sink = gst_pad_new ("sink", GST_PAD_SINK);
  fail_if (sink == NULL);

  src = gst_pad_new ("src", GST_PAD_SRC);
  fail_if (src == NULL);

  caps = gst_caps_from_string ("foo/bar");

  gst_pad_set_active (src, TRUE);
  gst_pad_set_caps (src, caps);
  gst_pad_set_active (sink, TRUE);
  gst_pad_set_caps (sink, caps);

  plr = gst_pad_link (src, sink);
  fail_unless (GST_PAD_LINK_SUCCESSFUL (plr));

  /* unref the sinkpad */
  gst_object_unref (sink);

  /* src should be unlinked now */
  fail_if (gst_pad_is_linked (src));

  /* cleanup */
  gst_object_unref (src);
  gst_caps_unref (caps);
}

GST_END_TEST;

static gulong id;

static GstPadProbeReturn
block_async_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  gboolean *bool_user_data = (gboolean *) user_data;

  fail_unless ((info->type & GST_PAD_PROBE_TYPE_BLOCK) != 0);

  /* here we should have blocked == 0 unblocked == 0 */
  fail_unless (bool_user_data[0] == FALSE);
  fail_unless (bool_user_data[1] == FALSE);

  bool_user_data[0] = TRUE;

  gst_pad_remove_probe (pad, id);
  bool_user_data[1] = TRUE;

  return GST_PAD_PROBE_OK;
}

GST_START_TEST (test_block_async)
{
  GstPad *pad;
  /* we set data[0] = TRUE when the pad is blocked, data[1] = TRUE when it's
   * unblocked */
  gboolean data[2] = { FALSE, FALSE };

  pad = gst_pad_new ("src", GST_PAD_SRC);
  fail_unless (pad != NULL);

  gst_pad_set_active (pad, TRUE);

  fail_unless (gst_pad_push_event (pad,
          gst_event_new_stream_start ("test")) == TRUE);
  fail_unless (gst_pad_push_event (pad,
          gst_event_new_segment (&dummy_segment)) == TRUE);

  id = gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BLOCK, block_async_cb, &data,
      NULL);

  fail_unless (data[0] == FALSE);
  fail_unless (data[1] == FALSE);
  gst_pad_push (pad, gst_buffer_new ());

  gst_object_unref (pad);
}

GST_END_TEST;

static GstPadProbeReturn
block_async_cb_return_ok (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  return GST_PAD_PROBE_OK;
}

static gpointer
push_buffer_async (GstPad * pad)
{
  return GINT_TO_POINTER (gst_pad_push (pad, gst_buffer_new ()));
}

static void
test_pad_blocking_with_type (GstPadProbeType type)
{
  GstPad *pad;
  GThread *thread;
  GstFlowReturn ret;

  pad = gst_pad_new ("src", GST_PAD_SRC);
  fail_unless (pad != NULL);

  gst_pad_set_active (pad, TRUE);

  fail_unless (gst_pad_push_event (pad,
          gst_event_new_stream_start ("test")) == TRUE);
  fail_unless (gst_pad_push_event (pad,
          gst_event_new_segment (&dummy_segment)) == TRUE);

  id = gst_pad_add_probe (pad, type, block_async_cb_return_ok, NULL, NULL);

  thread = g_thread_try_new ("gst-check", (GThreadFunc) push_buffer_async,
      pad, NULL);

  /* wait for the block */
  while (!gst_pad_is_blocking (pad)) {
    g_usleep (10000);
  }

  /* stop with flushing */
  gst_pad_push_event (pad, gst_event_new_flush_start ());

  /* get return value from push */
  ret = GPOINTER_TO_INT (g_thread_join (thread));
  /* unflush now */
  gst_pad_push_event (pad, gst_event_new_flush_stop (FALSE));
  /* must be wrong state */
  fail_unless (ret == GST_FLOW_FLUSHING);

  gst_object_unref (pad);
}

GST_START_TEST (test_pad_blocking_with_probe_type_block)
{
  test_pad_blocking_with_type (GST_PAD_PROBE_TYPE_BLOCK);
}

GST_END_TEST;

GST_START_TEST (test_pad_blocking_with_probe_type_blocking)
{
  test_pad_blocking_with_type (GST_PAD_PROBE_TYPE_BLOCKING);
}

GST_END_TEST;

static gboolean idle_probe_running;

static GstFlowReturn
idletest_sink_pad_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  if (idle_probe_running)
    fail ("Should not be reached");
  gst_buffer_unref (buf);
  return GST_FLOW_OK;
}

static GstPadProbeReturn
idle_probe_wait (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  /* it is ok to have a probe called multiple times but it is not
   * acceptable in our scenario */
  fail_if (idle_probe_running);

  idle_probe_running = TRUE;
  while (idle_probe_running) {
    g_usleep (10000);
  }

  return GST_PAD_PROBE_REMOVE;
}

static gpointer
add_idle_probe_async (GstPad * pad)
{
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_IDLE, idle_probe_wait, NULL, NULL);

  return NULL;
}

GST_START_TEST (test_pad_blocking_with_probe_type_idle)
{
  GstPad *srcpad, *sinkpad;
  GThread *idle_thread, *thread;

  srcpad = gst_pad_new ("src", GST_PAD_SRC);
  fail_unless (srcpad != NULL);
  sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  fail_unless (sinkpad != NULL);

  gst_pad_set_chain_function (sinkpad, idletest_sink_pad_chain);

  fail_unless (gst_pad_link (srcpad, sinkpad) == GST_PAD_LINK_OK);

  gst_pad_set_active (sinkpad, TRUE);
  gst_pad_set_active (srcpad, TRUE);

  fail_unless (gst_pad_push_event (srcpad,
          gst_event_new_stream_start ("test")) == TRUE);
  fail_unless (gst_pad_push_event (srcpad,
          gst_event_new_segment (&dummy_segment)) == TRUE);

  idle_probe_running = FALSE;
  idle_thread =
      g_thread_try_new ("gst-check", (GThreadFunc) add_idle_probe_async, srcpad,
      NULL);

  /* wait for the idle function to signal it is being called */
  while (!idle_probe_running) {
    g_usleep (10000);
  }

  thread = g_thread_try_new ("gst-check", (GThreadFunc) push_buffer_async,
      srcpad, NULL);

  while (!gst_pad_is_blocking (srcpad)) {
    g_usleep (10000);
  }

  idle_probe_running = FALSE;

  g_thread_join (idle_thread);
  g_thread_join (thread);
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);
}

GST_END_TEST;

static gboolean pull_probe_called;
static gboolean pull_probe_called_with_bad_type;
static gboolean pull_probe_called_with_bad_data;

static GstPadProbeReturn
probe_pull_buffer_cb_check_buffer_return_ok (GstPad * pad,
    GstPadProbeInfo * info, gpointer user_data)
{
  if (info->type & GST_PAD_PROBE_TYPE_BUFFER) {
    if (GST_IS_BUFFER (info->data))
      pull_probe_called = TRUE;
    else
      pull_probe_called_with_bad_data = TRUE;
  } else {
    /* shouldn't be called */
    pull_probe_called_with_bad_type = TRUE;
  }
  return GST_PAD_PROBE_OK;
}

static GstFlowReturn
test_probe_pull_getrange (GstPad * pad, GstObject * parent, guint64 offset,
    guint length, GstBuffer ** buf)
{
  *buf = gst_buffer_new ();
  return GST_FLOW_OK;
}

static gboolean
test_probe_pull_activate_pull (GstPad * pad, GstObject * object)
{
  return gst_pad_activate_mode (pad, GST_PAD_MODE_PULL, TRUE);
}

static gpointer
pull_range_async (GstPad * pad)
{
  GstBuffer *buf = NULL;
  GstFlowReturn res = gst_pad_pull_range (pad, 0, 100, &buf);
  if (buf)
    gst_buffer_unref (buf);
  return GINT_TO_POINTER (res);
}

GST_START_TEST (test_pad_probe_pull)
{
  GstPad *srcpad, *sinkpad;
  GThread *thread;
  GstFlowReturn ret;

  srcpad = gst_pad_new ("src", GST_PAD_SRC);
  fail_unless (srcpad != NULL);
  sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  fail_unless (sinkpad != NULL);

  gst_pad_set_getrange_function (srcpad, test_probe_pull_getrange);
  gst_pad_set_activate_function (sinkpad, test_probe_pull_activate_pull);
  gst_pad_link (srcpad, sinkpad);

  gst_pad_set_active (sinkpad, TRUE);
  gst_pad_set_active (srcpad, TRUE);

  id = gst_pad_add_probe (sinkpad,
      GST_PAD_PROBE_TYPE_BLOCK | GST_PAD_PROBE_TYPE_PULL,
      block_async_cb_return_ok, NULL, NULL);

  thread = g_thread_try_new ("gst-check", (GThreadFunc) pull_range_async,
      sinkpad, NULL);

  /* wait for the block */
  while (!gst_pad_is_blocking (sinkpad)) {
    g_usleep (10000);
  }

  /* stop with flushing */
  gst_pad_push_event (srcpad, gst_event_new_flush_start ());

  /* get return value from push */
  ret = GPOINTER_TO_INT (g_thread_join (thread));
  /* unflush now */
  gst_pad_push_event (srcpad, gst_event_new_flush_stop (FALSE));
  /* must be wrong state */
  fail_unless (ret == GST_FLOW_FLUSHING);

  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);
}

GST_END_TEST;

static gboolean idle_probe_called;
static gboolean get_range_wait;
static gboolean getrange_waiting;

static GstPadProbeReturn
idle_cb_return_ok (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  idle_probe_called = TRUE;
  return GST_PAD_PROBE_OK;
}

static GstFlowReturn
test_probe_pull_getrange_wait (GstPad * pad, GstObject * parent, guint64 offset,
    guint length, GstBuffer ** buf)
{
  getrange_waiting = TRUE;

  *buf = gst_buffer_new ();
  while (get_range_wait) {
    g_usleep (10000);
  }

  getrange_waiting = FALSE;
  return GST_FLOW_OK;
}

GST_START_TEST (test_pad_probe_pull_idle)
{
  GstPad *srcpad, *sinkpad;
  GThread *thread;
  GstFlowReturn ret;

  srcpad = gst_pad_new ("src", GST_PAD_SRC);
  fail_unless (srcpad != NULL);
  sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  fail_unless (sinkpad != NULL);

  gst_pad_set_getrange_function (srcpad, test_probe_pull_getrange_wait);
  gst_pad_set_activate_function (sinkpad, test_probe_pull_activate_pull);
  gst_pad_link (srcpad, sinkpad);

  gst_pad_set_active (sinkpad, TRUE);
  gst_pad_set_active (srcpad, TRUE);

  idle_probe_called = FALSE;
  get_range_wait = TRUE;
  thread = g_thread_try_new ("gst-check", (GThreadFunc) pull_range_async,
      sinkpad, NULL);

  /* wait for the block */
  while (!getrange_waiting) {
    g_usleep (10000);
  }

  id = gst_pad_add_probe (sinkpad,
      GST_PAD_PROBE_TYPE_IDLE | GST_PAD_PROBE_TYPE_PULL,
      idle_cb_return_ok, NULL, NULL);

  fail_if (idle_probe_called);

  get_range_wait = FALSE;
  while (getrange_waiting) {
    g_usleep (10000);
  }
  while (!idle_probe_called) {
    g_usleep (10000);
  }

  ret = GPOINTER_TO_INT (g_thread_join (thread));
  fail_unless (ret == GST_FLOW_OK);
  gst_pad_set_active (srcpad, FALSE);
  gst_pad_set_active (sinkpad, FALSE);
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);
}

GST_END_TEST;


GST_START_TEST (test_pad_probe_pull_buffer)
{
  GstPad *srcpad, *sinkpad;
  GThread *thread;
  GstFlowReturn ret;

  srcpad = gst_pad_new ("src", GST_PAD_SRC);
  fail_unless (srcpad != NULL);
  sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  fail_unless (sinkpad != NULL);

  gst_pad_set_getrange_function (srcpad, test_probe_pull_getrange);
  gst_pad_set_activate_function (sinkpad, test_probe_pull_activate_pull);
  gst_pad_link (srcpad, sinkpad);

  gst_pad_set_active (sinkpad, TRUE);
  gst_pad_set_active (srcpad, TRUE);

  id = gst_pad_add_probe (sinkpad,
      GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_PULL,
      probe_pull_buffer_cb_check_buffer_return_ok, NULL, NULL);

  pull_probe_called = FALSE;
  pull_probe_called_with_bad_type = FALSE;
  pull_probe_called_with_bad_data = FALSE;

  thread = g_thread_try_new ("gst-check", (GThreadFunc) pull_range_async,
      sinkpad, NULL);

  /* wait for the block */
  while (!pull_probe_called && !pull_probe_called_with_bad_data
      && !pull_probe_called_with_bad_type) {
    g_usleep (10000);
  }

  fail_unless (pull_probe_called);
  fail_if (pull_probe_called_with_bad_data);
  fail_if (pull_probe_called_with_bad_type);

  /* get return value from push */
  ret = GPOINTER_TO_INT (g_thread_join (thread));
  fail_unless (ret == GST_FLOW_OK);

  gst_pad_set_active (sinkpad, FALSE);
  gst_pad_set_active (srcpad, FALSE);
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);
}

GST_END_TEST;

static gboolean pad_probe_remove_notifiy_called = FALSE;

static GstPadProbeReturn
probe_remove_self_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  gst_pad_remove_probe (pad, info->id);

  fail_unless (pad->num_probes == 0);
  fail_unless (pad->num_blocked == 0);

  return GST_PAD_PROBE_REMOVE;
}

static void
probe_remove_notify_cb (gpointer data)
{
  fail_unless (pad_probe_remove_notifiy_called == FALSE);
  pad_probe_remove_notifiy_called = TRUE;
}

GST_START_TEST (test_pad_probe_remove)
{
  GstPad *pad;

  pad = gst_pad_new ("src", GST_PAD_SRC);
  fail_unless (pad != NULL);

  gst_pad_set_active (pad, TRUE);
  fail_unless (pad->num_probes == 0);
  fail_unless (pad->num_blocked == 0);
  gst_pad_add_probe (pad,
      GST_PAD_PROBE_TYPE_BLOCK | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
      probe_remove_self_cb, NULL, probe_remove_notify_cb);
  fail_unless (pad->num_probes == 1);
  fail_unless (pad->num_blocked == 1);

  pad_probe_remove_notifiy_called = FALSE;
  gst_pad_push_event (pad, gst_event_new_stream_start ("asda"));

  fail_unless (pad->num_probes == 0);
  fail_unless (pad->num_blocked == 0);

  gst_object_unref (pad);
}

GST_END_TEST;

typedef struct
{
  gulong probe_id;
  GstPad *probe_pad;
  GThread *thread;
} BlockReplaceProbeHelper;

static gpointer
unblock_probe_thread (gpointer user_data)
{
  BlockReplaceProbeHelper *helper = user_data;

  GST_INFO_OBJECT (helper->probe_pad, "removing probe to unblock pad");
  gst_pad_remove_probe (helper->probe_pad, helper->probe_id);
  return NULL;
}

static GstPadProbeReturn
block_and_replace_buffer_probe_cb (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  BlockReplaceProbeHelper *helper = user_data;

  GST_INFO_OBJECT (pad, "about to block pad, replacing buffer");

  /* we want to block, but also drop this buffer */
  gst_buffer_unref (GST_BUFFER (info->data));
  info->data = NULL;

  helper->thread =
      g_thread_new ("gst-pad-test-thread", unblock_probe_thread, helper);

  return GST_PAD_PROBE_OK;
}

GST_START_TEST (test_pad_probe_block_and_drop_buffer)
{
  BlockReplaceProbeHelper helper;
  GstFlowReturn flow;
  GstPad *src, *sink;

  src = gst_pad_new ("src", GST_PAD_SRC);
  gst_pad_set_active (src, TRUE);
  sink = gst_pad_new ("sink", GST_PAD_SINK);
  gst_pad_set_chain_function (sink, gst_check_chain_func);
  gst_pad_set_active (sink, TRUE);

  fail_unless (gst_pad_push_event (src,
          gst_event_new_stream_start ("test")) == TRUE);
  fail_unless (gst_pad_push_event (src,
          gst_event_new_segment (&dummy_segment)) == TRUE);

  fail_unless_equals_int (gst_pad_link (src, sink), GST_PAD_LINK_OK);

  helper.probe_id = gst_pad_add_probe (src,
      GST_PAD_PROBE_TYPE_BLOCK | GST_PAD_PROBE_TYPE_BUFFER,
      block_and_replace_buffer_probe_cb, &helper, NULL);
  helper.probe_pad = src;

  /* push a buffer so the events are propagated downstream */
  flow = gst_pad_push (src, gst_buffer_new ());

  g_thread_join (helper.thread);

  fail_unless_equals_int (flow, GST_FLOW_OK);

  /* no buffer should have made it through to the sink pad, and especially
   * not a NULL pointer buffer */
  fail_if (buffers && buffers->data == NULL);
  fail_unless (buffers == NULL);

  gst_check_drop_buffers ();
  gst_object_unref (src);
  gst_object_unref (sink);
}

GST_END_TEST;

static GstPadProbeReturn
probe_block_a (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn
probe_block_b (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  gboolean *probe_b_called = user_data;

  *probe_b_called = TRUE;

  return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn
probe_block_c (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  gboolean *probe_c_called = user_data;

  *probe_c_called = TRUE;

  return GST_PAD_PROBE_REMOVE;
}

GST_START_TEST (test_pad_probe_block_add_remove)
{
  GstPad *pad;
  GThread *thread;
  gulong probe_a, probe_b;
  gboolean probe_b_called = FALSE;
  gboolean probe_c_called = FALSE;

  pad = gst_pad_new ("src", GST_PAD_SRC);
  fail_unless (pad != NULL);

  gst_pad_set_active (pad, TRUE);
  fail_unless (pad->num_probes == 0);
  fail_unless (pad->num_blocked == 0);

  fail_unless (gst_pad_push_event (pad,
          gst_event_new_stream_start ("test")) == TRUE);
  fail_unless (gst_pad_push_event (pad,
          gst_event_new_segment (&dummy_segment)) == TRUE);

  probe_a = gst_pad_add_probe (pad,
      GST_PAD_PROBE_TYPE_BLOCK | GST_PAD_PROBE_TYPE_BUFFER,
      probe_block_a, NULL, NULL);

  fail_unless (pad->num_probes == 1);
  fail_unless (pad->num_blocked == 1);

  thread = g_thread_try_new ("gst-check", (GThreadFunc) push_buffer_async,
      pad, NULL);

  /* wait for the block */
  while (!gst_pad_is_blocking (pad)) {
    g_usleep (10000);
  }

  probe_b = gst_pad_add_probe (pad,
      GST_PAD_PROBE_TYPE_BLOCK | GST_PAD_PROBE_TYPE_BUFFER,
      probe_block_b, &probe_b_called, NULL);

  gst_pad_remove_probe (pad, probe_a);

  /* wait for the callback */
  while (!probe_b_called) {
    g_usleep (10000);
  }

  /* wait for the block */
  while (!gst_pad_is_blocking (pad)) {
    g_usleep (10000);
  }

  gst_pad_add_probe (pad,
      GST_PAD_PROBE_TYPE_BLOCK | GST_PAD_PROBE_TYPE_BUFFER,
      probe_block_c, &probe_c_called, NULL);

  gst_pad_remove_probe (pad, probe_b);

  /* wait for the callback */
  while (!probe_c_called) {
    g_usleep (10000);
  }

  /* wait for the unblock */
  while (gst_pad_is_blocking (pad)) {
    g_usleep (10000);
  }

  gst_object_unref (pad);

  g_thread_join (thread);
}

GST_END_TEST;

static gboolean src_flush_start_probe_called = FALSE;
static gboolean src_flush_stop_probe_called = FALSE;
static gboolean sink_flush_start_probe_called = FALSE;
static gboolean sink_flush_stop_probe_called = FALSE;

static GstPadProbeReturn
flush_probe_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GstEvent *event;

  if (!(GST_PAD_PROBE_INFO_TYPE (info) & GST_PAD_PROBE_TYPE_EVENT_FLUSH))
    goto out;

  event = gst_pad_probe_info_get_event (info);
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      if (GST_PAD_DIRECTION (pad) == GST_PAD_SRC)
        src_flush_start_probe_called = TRUE;
      else
        sink_flush_start_probe_called = TRUE;
      break;
    case GST_EVENT_FLUSH_STOP:
      if (GST_PAD_DIRECTION (pad) == GST_PAD_SRC)
        src_flush_stop_probe_called = TRUE;
      else
        sink_flush_stop_probe_called = TRUE;
      break;
    default:
      break;
  }

out:
  return GST_PAD_PROBE_OK;
}

GST_START_TEST (test_pad_probe_flush_events)
{
  GstPad *src, *sink;

  src = gst_pad_new ("src", GST_PAD_SRC);
  sink = gst_pad_new ("sink", GST_PAD_SINK);
  gst_pad_set_chain_function (sink, gst_check_chain_func);
  gst_pad_set_active (src, TRUE);
  gst_pad_set_active (sink, TRUE);

  fail_unless (gst_pad_push_event (src,
          gst_event_new_stream_start ("test")) == TRUE);
  fail_unless (gst_pad_push_event (src,
          gst_event_new_segment (&dummy_segment)) == TRUE);

  fail_unless (gst_pad_link (src, sink) == GST_PAD_LINK_OK);

  gst_pad_add_probe (src,
      GST_PAD_PROBE_TYPE_PUSH | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM |
      GST_PAD_PROBE_TYPE_EVENT_FLUSH, flush_probe_cb, NULL, NULL);
  gst_pad_add_probe (sink,
      GST_PAD_PROBE_TYPE_PUSH | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM |
      GST_PAD_PROBE_TYPE_EVENT_FLUSH, flush_probe_cb, NULL, NULL);

  gst_pad_push_event (src, gst_event_new_flush_start ());
  gst_pad_push_event (src, gst_event_new_flush_stop (TRUE));

  fail_unless (gst_pad_push_event (src,
          gst_event_new_segment (&dummy_segment)) == TRUE);

  /* push a buffer so the events are propagated downstream */
  gst_pad_push (src, gst_buffer_new ());

  fail_unless (src_flush_start_probe_called);
  fail_unless (src_flush_stop_probe_called);
  fail_unless (sink_flush_start_probe_called);
  fail_unless (sink_flush_stop_probe_called);

  gst_check_drop_buffers ();
  gst_object_unref (src);
  gst_object_unref (sink);
}

GST_END_TEST;

static gboolean probe_was_called;

static GstPadProbeReturn
flush_events_only_probe (GstPad * pad, GstPadProbeInfo * info, gpointer data)
{
  GST_LOG_OBJECT (pad, "%" GST_PTR_FORMAT, GST_PAD_PROBE_INFO_DATA (info));

  probe_was_called = TRUE;

  return GST_PAD_PROBE_OK;
}

GST_START_TEST (test_pad_probe_flush_events_only)
{
  GstPad *src, *sink;

  src = gst_pad_new ("src", GST_PAD_SRC);
  sink = gst_pad_new ("sink", GST_PAD_SINK);
  gst_pad_set_chain_function (sink, gst_check_chain_func);
  gst_pad_set_active (src, TRUE);
  gst_pad_set_active (sink, TRUE);

  fail_unless (gst_pad_link (src, sink) == GST_PAD_LINK_OK);

  fail_unless (gst_pad_push_event (src,
          gst_event_new_stream_start ("test")) == TRUE);

  gst_pad_add_probe (src, GST_PAD_PROBE_TYPE_EVENT_FLUSH,
      flush_events_only_probe, NULL, NULL);

  probe_was_called = FALSE;
  fail_unless (gst_pad_push_event (src,
          gst_event_new_segment (&dummy_segment)) == TRUE);
  fail_if (probe_was_called);

  fail_unless_equals_int (gst_pad_push (src, gst_buffer_new ()), GST_FLOW_OK);
  fail_if (probe_was_called);

  gst_pad_push_event (src, gst_event_new_flush_start ());
  fail_unless (probe_was_called);

  probe_was_called = FALSE;
  gst_pad_push_event (src, gst_event_new_flush_stop (TRUE));
  fail_unless (probe_was_called);

  gst_check_drop_buffers ();
  gst_object_unref (src);
  gst_object_unref (sink);
}

GST_END_TEST;

#define NUM_PROBES 4
static guint count;

static GstPadProbeReturn
order_others_probe_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  *(guint *) (user_data) = ++count;

  return GST_PAD_PROBE_REMOVE;
}

GST_START_TEST (test_pad_probe_call_order)
{
  GstFlowReturn flow;
  GstPad *src, *sink;
  guint counters[NUM_PROBES];
  guint i;

  src = gst_pad_new ("src", GST_PAD_SRC);
  gst_pad_set_active (src, TRUE);
  sink = gst_pad_new ("sink", GST_PAD_SINK);
  gst_pad_set_chain_function (sink, gst_check_chain_func);
  gst_pad_set_active (sink, TRUE);

  fail_unless (gst_pad_push_event (src,
          gst_event_new_stream_start ("test")) == TRUE);
  fail_unless (gst_pad_push_event (src,
          gst_event_new_segment (&dummy_segment)) == TRUE);

  fail_unless_equals_int (gst_pad_link (src, sink), GST_PAD_LINK_OK);

  for (i = 0; i < NUM_PROBES; i++) {
    gst_pad_add_probe (src,
        GST_PAD_PROBE_TYPE_BUFFER, order_others_probe_cb, &(counters[i]), NULL);
  }

  /* push a buffer so the events are propagated downstream */
  flow = gst_pad_push (src, gst_buffer_new ());
  fail_unless_equals_int (flow, GST_FLOW_OK);

  for (i = 0; i < NUM_PROBES; i++) {
    fail_unless (counters[i] == i + 1);
  }

  gst_check_drop_buffers ();
  gst_object_unref (src);
  gst_object_unref (sink);
}

GST_END_TEST;

static gboolean got_notify;

static void
caps_notify (GstPad * pad, GParamSpec * spec, gpointer data)
{
  got_notify = TRUE;
}

static void
test_queue_src_caps_notify (gboolean link_queue)
{
  GstElement *queue;
  GstPad *src, *sink, *another_pad;
  GstCaps *caps;

  queue = gst_element_factory_make ("queue", NULL);
  fail_unless (queue != NULL);

  src = gst_element_get_static_pad (queue, "src");
  fail_unless (src != NULL);

  sink = gst_element_get_static_pad (queue, "sink");
  fail_unless (sink != NULL);

  if (link_queue) {
    another_pad = gst_pad_new ("sink", GST_PAD_SINK);
    fail_unless (another_pad != NULL);
    gst_pad_set_active (another_pad, TRUE);

    gst_pad_link_full (src, another_pad, GST_PAD_LINK_CHECK_NOTHING);
  } else {
    another_pad = NULL;
  }

  gst_element_set_state (queue, GST_STATE_PLAYING);

  got_notify = FALSE;

  g_signal_connect (src, "notify::caps", G_CALLBACK (caps_notify), NULL);

  caps = gst_caps_from_string ("caps");
  gst_pad_send_event (sink, gst_event_new_caps (caps));
  gst_caps_unref (caps);

  while (got_notify == FALSE)
    g_usleep (10000);

  gst_element_set_state (queue, GST_STATE_NULL);

  gst_object_unref (src);
  gst_object_unref (sink);
  gst_object_unref (queue);
  if (another_pad) {
    gst_object_unref (another_pad);
  }
}

GST_START_TEST (test_queue_src_caps_notify_linked)
{
  test_queue_src_caps_notify (TRUE);
}

GST_END_TEST
GST_START_TEST (test_queue_src_caps_notify_not_linked)
{
  /* This test will fail because queue doesn't set the caps
     on src pad unless it is linked */
  test_queue_src_caps_notify (FALSE);
}

GST_END_TEST;

#if 0
static void
block_async_second (GstPad * pad, gboolean blocked, gpointer user_data)
{
  gst_pad_set_blocked (pad, FALSE, unblock_async_cb, NULL, NULL);
}

static void
block_async_first (GstPad * pad, gboolean blocked, gpointer user_data)
{
  static int n_calls = 0;
  gboolean *bool_user_data = (gboolean *) user_data;

  if (++n_calls > 1)
    /* we expect this callback to be called only once */
    g_warn_if_reached ();

  *bool_user_data = blocked;

  /* replace block_async_first with block_async_second so next time the pad is
   * blocked the latter should be called */
  gst_pad_set_blocked (pad, TRUE, block_async_second, NULL, NULL);

  /* unblock temporarily, in the next push block_async_second should be called
   */
  gst_pad_push_event (pad, gst_event_new_flush_start ());
}

GST_START_TEST (test_block_async_replace_callback)
{
  GstPad *pad;
  gboolean blocked;

  pad = gst_pad_new ("src", GST_PAD_SRC);
  fail_unless (pad != NULL);
  gst_pad_set_active (pad, TRUE);

  gst_pad_set_blocked (pad, TRUE, block_async_first, &blocked, NULL);
  blocked = FALSE;

  gst_pad_push (pad, gst_buffer_new ());
  fail_unless (blocked == TRUE);
  /* block_async_first flushes to unblock */
  gst_pad_push_event (pad, gst_event_new_flush_stop ());

  /* push again, this time block_async_second should be called */
  gst_pad_push (pad, gst_buffer_new ());
  fail_unless (blocked == TRUE);

  gst_object_unref (pad);
}

GST_END_TEST;
#endif

static void
block_async_full_destroy (gpointer user_data)
{
  gint *state = (gint *) user_data;

  fail_unless (*state < 2);

  GST_DEBUG ("setting state to 2");
  *state = 2;
}

static GstPadProbeReturn
block_async_full_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  *(gint *) user_data = (gint) TRUE;

  gst_pad_push_event (pad, gst_event_new_flush_start ());
  GST_DEBUG ("setting state to 1");

  return GST_PAD_PROBE_OK;
}

GST_START_TEST (test_block_async_full_destroy)
{
  GstPad *pad;
  /* 0 = unblocked, 1 = blocked, 2 = destroyed */
  gint state = 0;
  gulong id;

  pad = gst_pad_new ("src", GST_PAD_SRC);
  fail_unless (pad != NULL);
  gst_pad_set_active (pad, TRUE);

  fail_unless (gst_pad_push_event (pad,
          gst_event_new_stream_start ("test")) == TRUE);
  fail_unless (gst_pad_push_event (pad,
          gst_event_new_segment (&dummy_segment)) == TRUE);

  id = gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BLOCK, block_async_full_cb,
      &state, block_async_full_destroy);
  fail_unless (state == 0);

  gst_pad_push (pad, gst_buffer_new ());
  /* block_async_full_cb sets state to 1 and then flushes to unblock temporarily
   */
  fail_unless (state == 1);
  gst_pad_push_event (pad, gst_event_new_flush_stop (TRUE));

  /* unblock callback is called */
  gst_pad_remove_probe (pad, id);
  fail_unless (state == 2);

  gst_object_unref (pad);
}

GST_END_TEST;

GST_START_TEST (test_block_async_full_destroy_dispose)
{
  GstPad *pad;
  /* 0 = unblocked, 1 = blocked, 2 = destroyed */
  gint state = 0;

  pad = gst_pad_new ("src", GST_PAD_SRC);
  fail_unless (pad != NULL);
  gst_pad_set_active (pad, TRUE);

  fail_unless (gst_pad_push_event (pad,
          gst_event_new_stream_start ("test")) == TRUE);
  fail_unless (gst_pad_push_event (pad,
          gst_event_new_segment (&dummy_segment)) == TRUE);

  (void) gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BLOCK, block_async_full_cb,
      &state, block_async_full_destroy);

  gst_pad_push (pad, gst_buffer_new ());
  /* block_async_full_cb sets state to 1 and then flushes to unblock temporarily
   */
  fail_unless_equals_int (state, 1);
  gst_pad_push_event (pad, gst_event_new_flush_stop (TRUE));

  /* gst_BLOCK calls the destroy_notify function if necessary */
  gst_object_unref (pad);

  fail_unless_equals_int (state, 2);
}

GST_END_TEST;


#if 0
static void
unblock_async_no_flush_cb (GstPad * pad, gboolean blocked, gpointer user_data)
{
  gboolean *bool_user_data = (gboolean *) user_data;

  /* here we should have blocked == 1 unblocked == 0 */

  fail_unless (blocked == FALSE);

  fail_unless (bool_user_data[0] == TRUE);
  fail_unless (bool_user_data[1] == TRUE);
  fail_unless (bool_user_data[2] == FALSE);

  bool_user_data[2] = TRUE;
}
#endif


#if 0
static void
unblock_async_not_called (GstPad * pad, gboolean blocked, gpointer user_data)
{
  g_warn_if_reached ();
}
#endif

static GstPadProbeReturn
block_async_second_no_flush (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  gboolean *bool_user_data = (gboolean *) user_data;

  GST_DEBUG ("second probe called");

  fail_unless (info->type & GST_PAD_PROBE_TYPE_BLOCK);

  fail_unless (bool_user_data[0] == TRUE);
  fail_unless (bool_user_data[1] == FALSE);
  fail_unless (bool_user_data[2] == FALSE);

  bool_user_data[1] = TRUE;

  GST_DEBUG ("removing second probe with id %lu", id);
  gst_pad_remove_probe (pad, id);

  return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn
block_async_first_no_flush (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  static int n_calls = 0;
  gboolean *bool_user_data = (gboolean *) user_data;

  fail_unless (info->type & GST_PAD_PROBE_TYPE_BLOCK);

  GST_DEBUG ("first probe called");

  if (++n_calls > 1)
    /* we expect this callback to be called only once */
    g_warn_if_reached ();

  *bool_user_data = TRUE;

  fail_unless (bool_user_data[0] == TRUE);
  fail_unless (bool_user_data[1] == FALSE);
  fail_unless (bool_user_data[2] == FALSE);

  GST_DEBUG ("removing first probe with id %lu", id);
  gst_pad_remove_probe (pad, id);

  GST_DEBUG ("adding second probe");
  /* replace block_async_first with block_async_second so next time the pad is
   * blocked the latter should be called */
  id = gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BLOCK,
      block_async_second_no_flush, user_data, NULL);
  GST_DEBUG ("added probe with id %lu", id);

  return GST_PAD_PROBE_OK;
}

GST_START_TEST (test_block_async_replace_callback_no_flush)
{
  GstPad *pad;
  gboolean bool_user_data[3] = { FALSE, FALSE, FALSE };

  pad = gst_pad_new ("src", GST_PAD_SRC);
  fail_unless (pad != NULL);
  gst_pad_set_active (pad, TRUE);

  fail_unless (gst_pad_push_event (pad,
          gst_event_new_stream_start ("test")) == TRUE);
  fail_unless (gst_pad_push_event (pad,
          gst_event_new_segment (&dummy_segment)) == TRUE);

  GST_DEBUG ("adding probe");
  id = gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BLOCK,
      block_async_first_no_flush, bool_user_data, NULL);
  GST_DEBUG ("added probe with id %lu", id);
  fail_if (id == 0);

  GST_DEBUG ("pushing buffer");
  gst_pad_push (pad, gst_buffer_new ());
  fail_unless (bool_user_data[0] == TRUE);
  fail_unless (bool_user_data[1] == TRUE);
  fail_unless (bool_user_data[2] == FALSE);

  gst_object_unref (pad);
}

GST_END_TEST;

static gint sticky_count;

static gboolean
test_sticky_events_handler (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GST_DEBUG_OBJECT (pad, "received event %" GST_PTR_FORMAT, event);

  switch (sticky_count) {
    case 0:
      fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_STREAM_START);
      break;
    case 1:
    {
      GstCaps *caps;
      GstStructure *s;

      fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_CAPS);

      gst_event_parse_caps (event, &caps);
      fail_unless (gst_caps_get_size (caps) == 1);
      s = gst_caps_get_structure (caps, 0);
      fail_unless (gst_structure_has_name (s, "foo/baz"));
      break;
    }
    case 2:
      fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_SEGMENT);
      break;
    default:
      fail_unless (FALSE);
      break;
  }

  gst_event_unref (event);
  sticky_count++;

  return TRUE;
}

static GstFlowReturn
test_sticky_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  gst_buffer_unref (buffer);
  return GST_FLOW_OK;
}

GST_START_TEST (test_sticky_events)
{
  GstPad *srcpad, *sinkpad;
  GstCaps *caps;
  GstSegment seg;
  gchar *id;

  /* make unlinked srcpad */
  srcpad = gst_pad_new ("src", GST_PAD_SRC);
  fail_unless (srcpad != NULL);
  gst_pad_set_active (srcpad, TRUE);

  /* test stream-start */
  fail_unless (gst_pad_get_stream_id (srcpad) == NULL);

  /* push an event, it should be sticky on the srcpad */
  fail_unless (gst_pad_push_event (srcpad,
          gst_event_new_stream_start ("test")) == TRUE);

  /* let's see if it stuck */
  id = gst_pad_get_stream_id (srcpad);
  fail_unless_equals_string (id, "test");
  g_free (id);

  /* make a caps event */
  caps = gst_caps_new_empty_simple ("foo/bar");
  gst_pad_push_event (srcpad, gst_event_new_caps (caps));
  gst_caps_unref (caps);

  /* make segment event */
  gst_segment_init (&seg, GST_FORMAT_TIME);
  gst_pad_push_event (srcpad, gst_event_new_segment (&seg));

  /* now make a sinkpad */
  sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  fail_unless (sinkpad != NULL);
  sticky_count = 0;
  gst_pad_set_event_function (sinkpad, test_sticky_events_handler);
  gst_pad_set_chain_function (sinkpad, test_sticky_chain);
  fail_unless (sticky_count == 0);
  gst_pad_set_active (sinkpad, TRUE);

  /* link the pads */
  gst_pad_link (srcpad, sinkpad);
  /* should not trigger events */
  fail_unless (sticky_count == 0);

  /* caps replaces old caps event at position 2, the pushes all
   * pending events */
  caps = gst_caps_new_empty_simple ("foo/baz");
  gst_pad_push_event (srcpad, gst_event_new_caps (caps));
  gst_caps_unref (caps);

  /* should have triggered 2 events, the segment event is still pending */
  fail_unless_equals_int (sticky_count, 2);

  fail_unless (gst_pad_push (srcpad, gst_buffer_new ()) == GST_FLOW_OK);

  /* should have triggered 3 events */
  fail_unless_equals_int (sticky_count, 3);

  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);
}

GST_END_TEST;

static GstFlowReturn next_return;

static GstFlowReturn
test_lastflow_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  gst_buffer_unref (buffer);
  return next_return;
}

GST_START_TEST (test_last_flow_return_push)
{
  GstPad *srcpad, *sinkpad;
  GstSegment seg;

  srcpad = gst_pad_new ("src", GST_PAD_SRC);
  fail_unless (srcpad != NULL);
  sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  fail_unless (sinkpad != NULL);
  gst_pad_set_chain_function (sinkpad, test_lastflow_chain);
  gst_pad_link (srcpad, sinkpad);

  /* initial value is flushing */
  fail_unless (gst_pad_get_last_flow_return (srcpad) == GST_FLOW_FLUSHING);

  /* when active it goes to ok */
  gst_pad_set_active (srcpad, TRUE);
  fail_unless (gst_pad_get_last_flow_return (srcpad) == GST_FLOW_OK);
  gst_pad_set_active (sinkpad, TRUE);

  /* startup events */
  gst_pad_push_event (srcpad, gst_event_new_stream_start ("test"));
  gst_segment_init (&seg, GST_FORMAT_TIME);
  gst_pad_push_event (srcpad, gst_event_new_segment (&seg));


  /* push Ok */
  next_return = GST_FLOW_OK;
  fail_unless (gst_pad_push (srcpad, gst_buffer_new ()) == GST_FLOW_OK);
  fail_unless (gst_pad_get_last_flow_return (srcpad) == GST_FLOW_OK);

  /* push not-linked */
  next_return = GST_FLOW_NOT_LINKED;
  fail_unless (gst_pad_push (srcpad, gst_buffer_new ()) == GST_FLOW_NOT_LINKED);
  fail_unless (gst_pad_get_last_flow_return (srcpad) == GST_FLOW_NOT_LINKED);

  /* push not-linked */
  next_return = GST_FLOW_NOT_NEGOTIATED;
  fail_unless (gst_pad_push (srcpad,
          gst_buffer_new ()) == GST_FLOW_NOT_NEGOTIATED);
  fail_unless (gst_pad_get_last_flow_return (srcpad) ==
      GST_FLOW_NOT_NEGOTIATED);

  /* push error */
  next_return = GST_FLOW_ERROR;
  fail_unless (gst_pad_push (srcpad, gst_buffer_new ()) == GST_FLOW_ERROR);
  fail_unless (gst_pad_get_last_flow_return (srcpad) == GST_FLOW_ERROR);

  /* back to ok */
  next_return = GST_FLOW_OK;
  fail_unless (gst_pad_push (srcpad, gst_buffer_new ()) == GST_FLOW_OK);
  fail_unless (gst_pad_get_last_flow_return (srcpad) == GST_FLOW_OK);

  /* unlinked push */
  gst_pad_unlink (srcpad, sinkpad);
  fail_unless (gst_pad_push (srcpad, gst_buffer_new ()) == GST_FLOW_NOT_LINKED);
  fail_unless (gst_pad_get_last_flow_return (srcpad) == GST_FLOW_NOT_LINKED);

  gst_pad_link (srcpad, sinkpad);
  fail_unless (gst_pad_push_event (srcpad, gst_event_new_eos ()));
  fail_unless (gst_pad_get_last_flow_return (srcpad) == GST_FLOW_EOS);

  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);
}

GST_END_TEST;

static GstFlowReturn
test_lastflow_getrange (GstPad * pad, GstObject * parent, guint64 offset,
    guint length, GstBuffer ** buf)
{
  if (next_return == GST_FLOW_OK)
    *buf = gst_buffer_new ();
  else
    *buf = NULL;
  return next_return;
}

static gboolean
test_lastflow_activate_pull_func (GstPad * pad, GstObject * object)
{
  return gst_pad_activate_mode (pad, GST_PAD_MODE_PULL, TRUE);
}

GST_START_TEST (test_last_flow_return_pull)
{
  GstPad *srcpad, *sinkpad;
  GstBuffer *buf = NULL;

  srcpad = gst_pad_new ("src", GST_PAD_SRC);
  fail_unless (srcpad != NULL);
  sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  fail_unless (sinkpad != NULL);
  gst_pad_set_getrange_function (srcpad, test_lastflow_getrange);
  gst_pad_set_activate_function (sinkpad, test_lastflow_activate_pull_func);
  gst_pad_link (srcpad, sinkpad);

  /* initial value is flushing */
  fail_unless (gst_pad_get_last_flow_return (sinkpad) == GST_FLOW_FLUSHING);

  /* when active it goes to ok */
  gst_pad_set_active (sinkpad, TRUE);
  fail_unless (gst_pad_get_last_flow_return (sinkpad) == GST_FLOW_OK);
  gst_pad_set_active (srcpad, TRUE);

  /* pull Ok */
  next_return = GST_FLOW_OK;
  fail_unless (gst_pad_pull_range (sinkpad, 0, 1, &buf) == GST_FLOW_OK);
  fail_unless (gst_pad_get_last_flow_return (sinkpad) == GST_FLOW_OK);
  gst_buffer_unref (buf);
  buf = NULL;

  /* pull not-linked */
  next_return = GST_FLOW_NOT_LINKED;
  fail_unless (gst_pad_pull_range (sinkpad, 0, 1, &buf) == GST_FLOW_NOT_LINKED);
  fail_unless (gst_pad_get_last_flow_return (sinkpad) == GST_FLOW_NOT_LINKED);

  /* pull error */
  next_return = GST_FLOW_ERROR;
  fail_unless (gst_pad_pull_range (sinkpad, 0, 1, &buf) == GST_FLOW_ERROR);
  fail_unless (gst_pad_get_last_flow_return (sinkpad) == GST_FLOW_ERROR);

  /* pull not-nego */
  next_return = GST_FLOW_NOT_NEGOTIATED;
  fail_unless (gst_pad_pull_range (sinkpad, 0, 1,
          &buf) == GST_FLOW_NOT_NEGOTIATED);
  fail_unless (gst_pad_get_last_flow_return (sinkpad) ==
      GST_FLOW_NOT_NEGOTIATED);

  /* pull ok again */
  next_return = GST_FLOW_OK;
  fail_unless (gst_pad_pull_range (sinkpad, 0, 1, &buf) == GST_FLOW_OK);
  fail_unless (gst_pad_get_last_flow_return (sinkpad) == GST_FLOW_OK);
  gst_buffer_unref (buf);
  buf = NULL;

  /* unlinked pads */
  gst_pad_unlink (srcpad, sinkpad);
  fail_unless (gst_pad_pull_range (sinkpad, 0, 1, &buf) == GST_FLOW_NOT_LINKED);
  fail_unless (gst_pad_get_last_flow_return (sinkpad) == GST_FLOW_NOT_LINKED);

  /* eos */
  gst_pad_link (srcpad, sinkpad);
  next_return = GST_FLOW_EOS;
  fail_unless (gst_pad_pull_range (sinkpad, 0, 1, &buf) == GST_FLOW_EOS);
  fail_unless (gst_pad_get_last_flow_return (sinkpad) == GST_FLOW_EOS);

  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);
}

GST_END_TEST;

GST_START_TEST (test_flush_stop_inactive)
{
  GstPad *sinkpad, *srcpad;

  sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  fail_unless (sinkpad != NULL);

  /* new pads are inactive and flushing */
  fail_if (GST_PAD_IS_ACTIVE (sinkpad));
  fail_unless (GST_PAD_IS_FLUSHING (sinkpad));

  /* this should fail, pad is inactive */
  fail_if (gst_pad_send_event (sinkpad, gst_event_new_flush_stop (FALSE)));

  /* nothing should have changed */
  fail_if (GST_PAD_IS_ACTIVE (sinkpad));
  fail_unless (GST_PAD_IS_FLUSHING (sinkpad));

  gst_pad_set_active (sinkpad, TRUE);

  /* pad is now active an not flushing anymore */
  fail_unless (GST_PAD_IS_ACTIVE (sinkpad));
  fail_if (GST_PAD_IS_FLUSHING (sinkpad));

  /* do flush, does not deactivate the pad */
  fail_unless (gst_pad_send_event (sinkpad, gst_event_new_flush_start ()));
  fail_unless (GST_PAD_IS_ACTIVE (sinkpad));
  fail_unless (GST_PAD_IS_FLUSHING (sinkpad));

  fail_unless (gst_pad_send_event (sinkpad, gst_event_new_flush_stop (FALSE)));
  fail_unless (GST_PAD_IS_ACTIVE (sinkpad));
  fail_if (GST_PAD_IS_FLUSHING (sinkpad));

  gst_pad_set_active (sinkpad, FALSE);
  fail_if (GST_PAD_IS_ACTIVE (sinkpad));
  fail_unless (GST_PAD_IS_FLUSHING (sinkpad));

  gst_object_unref (sinkpad);

  /* we should not be able to push on an inactive srcpad */
  srcpad = gst_pad_new ("src", GST_PAD_SRC);
  fail_unless (srcpad != NULL);

  fail_if (GST_PAD_IS_ACTIVE (srcpad));
  fail_unless (GST_PAD_IS_FLUSHING (srcpad));

  fail_if (gst_pad_push_event (srcpad, gst_event_new_flush_stop (FALSE)));

  /* should still be inactive and flushing */
  fail_if (GST_PAD_IS_ACTIVE (srcpad));
  fail_unless (GST_PAD_IS_FLUSHING (srcpad));

  gst_pad_set_active (srcpad, TRUE);

  /* pad is now active an not flushing anymore */
  fail_unless (GST_PAD_IS_ACTIVE (srcpad));
  fail_if (GST_PAD_IS_FLUSHING (srcpad));

  /* do flush, does not deactivate the pad */
  fail_if (gst_pad_push_event (srcpad, gst_event_new_flush_start ()));
  fail_unless (GST_PAD_IS_ACTIVE (srcpad));
  fail_unless (GST_PAD_IS_FLUSHING (srcpad));

  fail_if (gst_pad_push_event (srcpad, gst_event_new_flush_stop (FALSE)));
  fail_unless (GST_PAD_IS_ACTIVE (srcpad));
  fail_if (GST_PAD_IS_FLUSHING (srcpad));

  gst_pad_set_active (srcpad, FALSE);
  fail_if (GST_PAD_IS_ACTIVE (srcpad));
  fail_unless (GST_PAD_IS_FLUSHING (srcpad));

  gst_object_unref (srcpad);
}

GST_END_TEST;

/* For proxy caps flag tests */

typedef struct _GstProxyTestElement GstProxyTestElement;
typedef struct _GstProxyTestElementClass GstProxyTestElementClass;

struct _GstProxyTestElement
{
  GstElement element;
};

struct _GstProxyTestElementClass
{
  GstElementClass parent_class;
};

G_GNUC_INTERNAL GType gst_proxytestelement_get_type (void);

static GstStaticPadTemplate proxytestelement_peer_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("test/proxy, option=(int)1"));

static GstStaticPadTemplate proxytestelement_peer_incompatible_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("test/proxy-incompatible"));

static GstStaticPadTemplate proxytestelement_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("test/proxy"));

static GstStaticPadTemplate proxytestelement_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

G_DEFINE_TYPE (GstProxyTestElement, gst_proxytestelement, GST_TYPE_ELEMENT);

static void
gst_proxytestelement_class_init (GstProxyTestElementClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_static_metadata (gstelement_class,
      "Proxy Test Element", "Test", "Proxy test element",
      "Thiago Santos <thiagoss@osg.samsung.com>");

  gst_element_class_add_static_pad_template (gstelement_class,
      &proxytestelement_sink_template);
}

static void
gst_proxytestelement_init (GstProxyTestElement * element)
{
  GstPad *sinkpad;
  sinkpad =
      gst_pad_new_from_static_template (&proxytestelement_sink_template,
      "sink");
  GST_PAD_SET_PROXY_CAPS (sinkpad);
  gst_element_add_pad (GST_ELEMENT_CAST (element), sinkpad);
}

GST_START_TEST (test_proxy_accept_caps_no_proxy)
{
  GstElement *element;
  GstPad *sinkpad;
  GstCaps *caps;

  gst_element_register (NULL, "proxytestelement", GST_RANK_NONE,
      gst_proxytestelement_get_type ());
  element = gst_element_factory_make ("proxytestelement", NULL);
  sinkpad = gst_element_get_static_pad (element, "sink");

  gst_element_set_state (element, GST_STATE_PLAYING);

  caps = gst_caps_from_string ("test/proxy");
  fail_unless (gst_pad_query_accept_caps (sinkpad, caps));
  gst_caps_unref (caps);

  caps = gst_caps_from_string ("test/bad");
  fail_if (gst_pad_query_accept_caps (sinkpad, caps));
  gst_caps_unref (caps);

  gst_object_unref (sinkpad);
  gst_element_set_state (element, GST_STATE_NULL);
  gst_object_unref (element);
}

GST_END_TEST;


GST_START_TEST (test_proxy_accept_caps_with_proxy)
{
  GstElement *element;
  GstPad *sinkpad, *srcpad;
  GstPad *peerpad;
  GstCaps *caps;

  gst_element_register (NULL, "proxytestelement", GST_RANK_NONE,
      gst_proxytestelement_get_type ());
  element = gst_element_factory_make ("proxytestelement", NULL);

  srcpad =
      gst_pad_new_from_static_template (&proxytestelement_src_template, "src");
  gst_element_add_pad (GST_ELEMENT_CAST (element), srcpad);

  sinkpad = gst_element_get_static_pad (element, "sink");
  srcpad = gst_element_get_static_pad (element, "src");

  peerpad =
      gst_pad_new_from_static_template (&proxytestelement_peer_template,
      "sink");
  fail_unless (gst_pad_link (srcpad, peerpad) == GST_PAD_LINK_OK);
  gst_pad_set_active (peerpad, TRUE);

  gst_element_set_state (element, GST_STATE_PLAYING);

  caps = gst_caps_from_string ("test/bad");
  fail_if (gst_pad_query_accept_caps (sinkpad, caps));
  gst_caps_unref (caps);

  caps = gst_caps_from_string ("test/proxy, option=(int)1");
  fail_unless (gst_pad_query_accept_caps (sinkpad, caps));
  gst_caps_unref (caps);

  caps = gst_caps_from_string ("test/proxy, option=(int)2");
  fail_if (gst_pad_query_accept_caps (sinkpad, caps));
  gst_caps_unref (caps);

  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);
  gst_pad_set_active (peerpad, FALSE);
  gst_object_unref (peerpad);
  gst_element_set_state (element, GST_STATE_NULL);
  gst_object_unref (element);
}

GST_END_TEST;

GST_START_TEST (test_proxy_accept_caps_with_incompatible_proxy)
{
  GstElement *element;
  GstPad *sinkpad, *srcpad;
  GstPad *peerpad;
  GstCaps *caps;

  gst_element_register (NULL, "proxytestelement", GST_RANK_NONE,
      gst_proxytestelement_get_type ());
  element = gst_element_factory_make ("proxytestelement", NULL);

  srcpad =
      gst_pad_new_from_static_template (&proxytestelement_src_template, "src");
  gst_element_add_pad (GST_ELEMENT_CAST (element), srcpad);

  sinkpad = gst_element_get_static_pad (element, "sink");
  srcpad = gst_element_get_static_pad (element, "src");

  peerpad =
      gst_pad_new_from_static_template
      (&proxytestelement_peer_incompatible_template, "sink");
  fail_unless (gst_pad_link (srcpad, peerpad) == GST_PAD_LINK_OK);

  gst_element_set_state (element, GST_STATE_PLAYING);

  caps = gst_caps_from_string ("test/bad");
  fail_if (gst_pad_query_accept_caps (sinkpad, caps));
  gst_caps_unref (caps);

  caps = gst_caps_from_string ("test/proxy");
  fail_if (gst_pad_query_accept_caps (sinkpad, caps));
  gst_caps_unref (caps);

  caps = gst_caps_from_string ("test/proxy-incompatible");
  fail_if (gst_pad_query_accept_caps (sinkpad, caps));
  gst_caps_unref (caps);

  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);
  gst_pad_set_active (peerpad, FALSE);
  gst_object_unref (peerpad);
  gst_element_set_state (element, GST_STATE_NULL);
  gst_object_unref (element);
}

GST_END_TEST;

static GstSegment sink_segment;
static gint sink_segment_counter;

static gboolean
segment_event_func (GstPad * pad, GstObject * parent, GstEvent * event)
{
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
      gst_event_copy_segment (event, &sink_segment);
      sink_segment_counter++;
      break;
    default:
      break;
  }

  gst_event_unref (event);
  return TRUE;
}

static void
test_pad_offset (gboolean on_srcpad)
{
  GstPad *srcpad, *sinkpad, *offset_pad;
  GstSegment segment;
  GstBuffer *buffer;
  GstQuery *query;

  srcpad = gst_pad_new ("src", GST_PAD_SRC);
  fail_unless (srcpad != NULL);
  sinkpad = gst_pad_new ("sink", GST_PAD_SINK);

  offset_pad = on_srcpad ? srcpad : sinkpad;

  gst_segment_init (&sink_segment, GST_FORMAT_UNDEFINED);
  sink_segment_counter = 0;
  gst_pad_set_chain_function (sinkpad, gst_check_chain_func);
  gst_pad_set_event_function (sinkpad, segment_event_func);

  fail_unless (sinkpad != NULL);
  fail_unless_equals_int (gst_pad_link (srcpad, sinkpad), GST_PAD_LINK_OK);
  fail_unless (gst_pad_set_active (sinkpad, TRUE));
  fail_unless (gst_pad_set_active (srcpad, TRUE));

  /* Set an offset of 5s, meaning:
   * segment position 0 gives running time 5s, stream time 0s
   * segment start of 0 should stay 0
   */
  gst_pad_set_offset (offset_pad, 5 * GST_SECOND);

  fail_unless (gst_pad_push_event (srcpad,
          gst_event_new_stream_start ("test")) == TRUE);
  /* We should have no segment event yet */
  fail_if (sink_segment.format != GST_FORMAT_UNDEFINED);
  fail_unless_equals_int (sink_segment_counter, 0);

  /* Send segment event, expect it to arrive with a modified start running time */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  fail_unless (gst_pad_push_event (srcpad,
          gst_event_new_segment (&segment)) == TRUE);
  fail_if (sink_segment.format == GST_FORMAT_UNDEFINED);
  fail_unless_equals_uint64 (gst_segment_to_running_time (&sink_segment,
          GST_FORMAT_TIME, sink_segment.start), 5 * GST_SECOND);
  fail_unless_equals_uint64 (gst_segment_to_stream_time (&sink_segment,
          GST_FORMAT_TIME, sink_segment.start), 0 * GST_SECOND);
  fail_unless_equals_uint64 (sink_segment.start, 0 * GST_SECOND);

  fail_unless_equals_int (sink_segment_counter, 1);

  /* Send a buffer and check if all timestamps are as expected, and especially
   * if the buffer timestamp was not changed */
  buffer = gst_buffer_new ();
  GST_BUFFER_PTS (buffer) = 0 * GST_SECOND;
  fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);

  fail_unless_equals_int (g_list_length (buffers), 1);
  buffer = buffers->data;
  buffers = g_list_delete_link (buffers, buffers);
  fail_unless_equals_uint64 (gst_segment_to_running_time (&sink_segment,
          GST_FORMAT_TIME, GST_BUFFER_PTS (buffer)), 5 * GST_SECOND);
  fail_unless_equals_uint64 (gst_segment_to_stream_time (&sink_segment,
          GST_FORMAT_TIME, GST_BUFFER_PTS (buffer)), 0 * GST_SECOND);
  fail_unless_equals_uint64 (GST_BUFFER_PTS (buffer), 0 * GST_SECOND);
  gst_buffer_unref (buffer);

  fail_unless_equals_int (sink_segment_counter, 1);

  /* Set a negative offset of -5s, meaning:
   * segment position 5s gives running time 0s, stream time 5s
   * segment start would have a negative running time!
   */
  gst_pad_set_offset (offset_pad, -5 * GST_SECOND);

  /* Segment should still be the same as before */
  fail_if (sink_segment.format == GST_FORMAT_UNDEFINED);
  fail_unless_equals_uint64 (gst_segment_to_running_time (&sink_segment,
          GST_FORMAT_TIME, sink_segment.start), 5 * GST_SECOND);
  fail_unless_equals_uint64 (gst_segment_to_stream_time (&sink_segment,
          GST_FORMAT_TIME, sink_segment.start), 0 * GST_SECOND);
  fail_unless_equals_uint64 (sink_segment.start, 0 * GST_SECOND);

  fail_unless_equals_int (sink_segment_counter, 1);

  /* Send segment event, expect it to arrive with a modified start running time */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  fail_unless (gst_pad_push_event (srcpad,
          gst_event_new_segment (&segment)) == TRUE);
  fail_if (sink_segment.format == GST_FORMAT_UNDEFINED);
  fail_unless_equals_uint64 (gst_segment_to_running_time (&sink_segment,
          GST_FORMAT_TIME, sink_segment.start + 5 * GST_SECOND),
      0 * GST_SECOND);
  fail_unless_equals_uint64 (gst_segment_to_stream_time (&sink_segment,
          GST_FORMAT_TIME, sink_segment.start + 5 * GST_SECOND),
      5 * GST_SECOND);
  fail_unless_equals_uint64 (sink_segment.start, 0 * GST_SECOND);

  fail_unless_equals_int (sink_segment_counter, 2);

  /* Send a buffer and check if all timestamps are as expected, and especially
   * if the buffer timestamp was not changed */
  buffer = gst_buffer_new ();
  GST_BUFFER_PTS (buffer) = 5 * GST_SECOND;
  fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);

  fail_unless_equals_int (g_list_length (buffers), 1);
  buffer = buffers->data;
  buffers = g_list_delete_link (buffers, buffers);
  fail_unless_equals_uint64 (gst_segment_to_running_time (&sink_segment,
          GST_FORMAT_TIME, GST_BUFFER_PTS (buffer)), 0 * GST_SECOND);
  fail_unless_equals_uint64 (gst_segment_to_stream_time (&sink_segment,
          GST_FORMAT_TIME, GST_BUFFER_PTS (buffer)), 5 * GST_SECOND);
  fail_unless_equals_uint64 (GST_BUFFER_PTS (buffer), 5 * GST_SECOND);
  gst_buffer_unref (buffer);

  fail_unless_equals_int (sink_segment_counter, 2);

  /* Set offset to 5s again, same situation as above but don't send a new
   * segment event. The segment should be adjusted *before* the buffer comes
   * out of the srcpad */
  gst_pad_set_offset (offset_pad, 5 * GST_SECOND);

  /* Segment should still be the same as before */
  fail_if (sink_segment.format == GST_FORMAT_UNDEFINED);
  fail_unless_equals_uint64 (gst_segment_to_running_time (&sink_segment,
          GST_FORMAT_TIME, sink_segment.start + 5 * GST_SECOND),
      0 * GST_SECOND);
  fail_unless_equals_uint64 (gst_segment_to_stream_time (&sink_segment,
          GST_FORMAT_TIME, sink_segment.start + 5 * GST_SECOND),
      5 * GST_SECOND);
  fail_unless_equals_uint64 (sink_segment.start, 0 * GST_SECOND);

  fail_unless_equals_int (sink_segment_counter, 2);

  /* Send a buffer and check if a new segment event was sent and all buffer
   * timestamps are as expected */
  buffer = gst_buffer_new ();
  GST_BUFFER_PTS (buffer) = 0 * GST_SECOND;
  fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);

  fail_if (sink_segment.format == GST_FORMAT_UNDEFINED);
  fail_unless_equals_uint64 (gst_segment_to_running_time (&sink_segment,
          GST_FORMAT_TIME, sink_segment.start), 5 * GST_SECOND);
  fail_unless_equals_uint64 (gst_segment_to_stream_time (&sink_segment,
          GST_FORMAT_TIME, sink_segment.start), 0 * GST_SECOND);
  fail_unless_equals_uint64 (sink_segment.start, 0 * GST_SECOND);

  fail_unless_equals_int (sink_segment_counter, 3);

  fail_unless_equals_int (g_list_length (buffers), 1);
  buffer = buffers->data;
  buffers = g_list_delete_link (buffers, buffers);
  fail_unless_equals_uint64 (gst_segment_to_running_time (&sink_segment,
          GST_FORMAT_TIME, GST_BUFFER_PTS (buffer)), 5 * GST_SECOND);
  fail_unless_equals_uint64 (gst_segment_to_stream_time (&sink_segment,
          GST_FORMAT_TIME, GST_BUFFER_PTS (buffer)), 0 * GST_SECOND);
  fail_unless_equals_uint64 (GST_BUFFER_PTS (buffer), 0 * GST_SECOND);
  gst_buffer_unref (buffer);

  fail_unless_equals_int (sink_segment_counter, 3);

  /* Set offset to 10s and send another sticky event. In between a new
   * segment event should've been sent */
  gst_pad_set_offset (offset_pad, 10 * GST_SECOND);

  /* Segment should still be the same as before */
  fail_if (sink_segment.format == GST_FORMAT_UNDEFINED);
  fail_unless_equals_uint64 (gst_segment_to_running_time (&sink_segment,
          GST_FORMAT_TIME, sink_segment.start), 5 * GST_SECOND);
  fail_unless_equals_uint64 (gst_segment_to_stream_time (&sink_segment,
          GST_FORMAT_TIME, sink_segment.start), 0 * GST_SECOND);
  fail_unless_equals_uint64 (sink_segment.start, 0 * GST_SECOND);
  fail_unless_equals_int (sink_segment_counter, 3);

  fail_unless (gst_pad_push_event (srcpad,
          gst_event_new_tag (gst_tag_list_new_empty ())) == TRUE);

  /* Segment should be updated */
  fail_if (sink_segment.format == GST_FORMAT_UNDEFINED);
  fail_unless_equals_uint64 (gst_segment_to_running_time (&sink_segment,
          GST_FORMAT_TIME, sink_segment.start), 10 * GST_SECOND);
  fail_unless_equals_uint64 (gst_segment_to_stream_time (&sink_segment,
          GST_FORMAT_TIME, sink_segment.start), 0 * GST_SECOND);
  fail_unless_equals_uint64 (sink_segment.start, 0 * GST_SECOND);

  fail_unless_equals_int (sink_segment_counter, 4);

  /* Set offset to 15s and do a serialized query. In between a new
   * segment event should've been sent */
  gst_pad_set_offset (offset_pad, 15 * GST_SECOND);

  /* Segment should still be the same as before */
  fail_if (sink_segment.format == GST_FORMAT_UNDEFINED);
  fail_unless_equals_uint64 (gst_segment_to_running_time (&sink_segment,
          GST_FORMAT_TIME, sink_segment.start), 10 * GST_SECOND);
  fail_unless_equals_uint64 (gst_segment_to_stream_time (&sink_segment,
          GST_FORMAT_TIME, sink_segment.start), 0 * GST_SECOND);
  fail_unless_equals_uint64 (sink_segment.start, 0 * GST_SECOND);
  fail_unless_equals_int (sink_segment_counter, 4);

  query = gst_query_new_drain ();
  gst_pad_peer_query (srcpad, query);
  gst_query_unref (query);

  /* Segment should be updated */
  fail_if (sink_segment.format == GST_FORMAT_UNDEFINED);
  fail_unless_equals_uint64 (gst_segment_to_running_time (&sink_segment,
          GST_FORMAT_TIME, sink_segment.start), 15 * GST_SECOND);
  fail_unless_equals_uint64 (gst_segment_to_stream_time (&sink_segment,
          GST_FORMAT_TIME, sink_segment.start), 0 * GST_SECOND);
  fail_unless_equals_uint64 (sink_segment.start, 0 * GST_SECOND);

  fail_unless_equals_int (sink_segment_counter, 5);

  gst_check_drop_buffers ();

  fail_unless (gst_pad_set_active (sinkpad, FALSE));
  fail_unless (gst_pad_set_active (srcpad, FALSE));
  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);
}

GST_START_TEST (test_pad_offset_src)
{
  test_pad_offset (TRUE);
}

GST_END_TEST;

static Suite *
gst_pad_suite (void)
{
  Suite *s = suite_create ("GstPad");
  TCase *tc_chain = tcase_create ("general");

  /* turn off timeout */
  tcase_set_timeout (tc_chain, 60);

  gst_segment_init (&dummy_segment, GST_FORMAT_BYTES);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_link);
  tcase_add_test (tc_chain, test_refcount);
  tcase_add_test (tc_chain, test_get_allowed_caps);
  tcase_add_test (tc_chain, test_sticky_caps_unlinked);
  tcase_add_test (tc_chain, test_sticky_caps_unlinked_incompatible);
  tcase_add_test (tc_chain, test_sticky_caps_flushing);
  tcase_add_test (tc_chain, test_default_accept_caps);
  tcase_add_test (tc_chain, test_link_unlink_threaded);
  tcase_add_test (tc_chain, test_name_is_valid);
  tcase_add_test (tc_chain, test_push_unlinked);
  tcase_add_test (tc_chain, test_push_linked);
  tcase_add_test (tc_chain, test_push_linked_flushing);
  tcase_add_test (tc_chain, test_push_buffer_list_compat);
  tcase_add_test (tc_chain, test_flowreturn);
  tcase_add_test (tc_chain, test_push_negotiation);
  tcase_add_test (tc_chain, test_src_unref_unlink);
  tcase_add_test (tc_chain, test_sink_unref_unlink);
  tcase_add_test (tc_chain, test_block_async);
  tcase_add_test (tc_chain, test_pad_blocking_with_probe_type_block);
  tcase_add_test (tc_chain, test_pad_blocking_with_probe_type_blocking);
  tcase_add_test (tc_chain, test_pad_blocking_with_probe_type_idle);
  tcase_add_test (tc_chain, test_pad_probe_pull);
  tcase_add_test (tc_chain, test_pad_probe_pull_idle);
  tcase_add_test (tc_chain, test_pad_probe_pull_buffer);
  tcase_add_test (tc_chain, test_pad_probe_remove);
  tcase_add_test (tc_chain, test_pad_probe_block_add_remove);
  tcase_add_test (tc_chain, test_pad_probe_block_and_drop_buffer);
  tcase_add_test (tc_chain, test_pad_probe_flush_events);
  tcase_add_test (tc_chain, test_pad_probe_flush_events_only);
  tcase_add_test (tc_chain, test_pad_probe_call_order);
  tcase_add_test (tc_chain, test_events_query_unlinked);
  tcase_add_test (tc_chain, test_queue_src_caps_notify_linked);
  tcase_add_test (tc_chain, test_queue_src_caps_notify_not_linked);
#if 0
  tcase_add_test (tc_chain, test_block_async_replace_callback);
#endif
  tcase_add_test (tc_chain, test_block_async_full_destroy);
  tcase_add_test (tc_chain, test_block_async_full_destroy_dispose);
  tcase_add_test (tc_chain, test_block_async_replace_callback_no_flush);
  tcase_add_test (tc_chain, test_sticky_events);
  tcase_add_test (tc_chain, test_last_flow_return_push);
  tcase_add_test (tc_chain, test_last_flow_return_pull);
  tcase_add_test (tc_chain, test_flush_stop_inactive);
  tcase_add_test (tc_chain, test_proxy_accept_caps_no_proxy);
  tcase_add_test (tc_chain, test_proxy_accept_caps_with_proxy);
  tcase_add_test (tc_chain, test_proxy_accept_caps_with_incompatible_proxy);
  tcase_add_test (tc_chain, test_pad_offset_src);

  return s;
}

GST_CHECK_MAIN (gst_pad);
