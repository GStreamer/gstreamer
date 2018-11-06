/* GStreamer
 * Copyright (C) 2013 Stefan Sauer <ensonic@users.sf.net>
 *
 * gstlog.c: tracing module that logs events
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
/**
 * SECTION:element-logtracer
 * @short_description: log hook event
 *
 * A tracing module that logs all data from all hooks.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstlog.h"

GST_DEBUG_CATEGORY_STATIC (gst_log_debug);
#define GST_CAT_DEFAULT gst_log_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_BIN);
GST_DEBUG_CATEGORY_STATIC (GST_CAT_BUFFER);
GST_DEBUG_CATEGORY_STATIC (GST_CAT_BUFFER_LIST);
GST_DEBUG_CATEGORY_STATIC (GST_CAT_EVENT);
GST_DEBUG_CATEGORY_STATIC (GST_CAT_MESSAGE);
GST_DEBUG_CATEGORY_STATIC (GST_CAT_QUERY);
GST_DEBUG_CATEGORY_STATIC (GST_CAT_STATES);
GST_DEBUG_CATEGORY_STATIC (GST_CAT_PADS);
GST_DEBUG_CATEGORY_STATIC (GST_CAT_ELEMENT_PADS);
GST_DEBUG_CATEGORY_STATIC (GST_CAT_ELEMENT_FACTORY);

#define _do_init \
    GST_DEBUG_CATEGORY_INIT (gst_log_debug, "log", 0, "log tracer"); \
    GST_DEBUG_CATEGORY_GET (GST_CAT_BUFFER, "GST_BUFFER"); \
    GST_DEBUG_CATEGORY_GET (GST_CAT_BUFFER_LIST, "GST_BUFFER_LIST"); \
    GST_DEBUG_CATEGORY_GET (GST_CAT_EVENT, "GST_EVENT"); \
    GST_DEBUG_CATEGORY_GET (GST_CAT_MESSAGE, "GST_MESSAGE"); \
    GST_DEBUG_CATEGORY_GET (GST_CAT_STATES, "GST_STATES"); \
    GST_DEBUG_CATEGORY_GET (GST_CAT_PADS, "GST_PADS"); \
    GST_DEBUG_CATEGORY_GET (GST_CAT_ELEMENT_PADS, "GST_ELEMENT_PADS"); \
    GST_DEBUG_CATEGORY_GET (GST_CAT_ELEMENT_FACTORY, "GST_ELEMENT_FACTORY"); \
    GST_DEBUG_CATEGORY_GET (GST_CAT_QUERY, "query"); \
    GST_DEBUG_CATEGORY_GET (GST_CAT_BIN, "bin");
#define gst_log_tracer_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstLogTracer, gst_log_tracer, GST_TYPE_TRACER,
    _do_init);

static void
do_log (GstDebugCategory * cat, const char *func, GObject * obj,
    const char *fmt, ...)
{
  va_list var_args;

  va_start (var_args, fmt);
  gst_debug_log_valist (cat, GST_LEVEL_TRACE, "", func, 0, obj, fmt, var_args);
  va_end (var_args);
}

static void
do_push_buffer_pre (GstTracer * self, guint64 ts, GstPad * pad,
    GstBuffer * buffer)
{
  do_log (GST_CAT_BUFFER, GST_FUNCTION, (GObject *) pad,
      "%" GST_TIME_FORMAT ", pad=%" GST_PTR_FORMAT ", buffer=%" GST_PTR_FORMAT,
      GST_TIME_ARGS (ts), pad, buffer);
}

static void
do_push_buffer_post (GstTracer * self, guint64 ts, GstPad * pad,
    GstFlowReturn res)
{
  do_log (GST_CAT_BUFFER, GST_FUNCTION, (GObject *) pad,
      "%" GST_TIME_FORMAT ", pad=%" GST_PTR_FORMAT ", res=%d",
      GST_TIME_ARGS (ts), pad, res);
}

static void
do_push_buffer_list_pre (GstTracer * self, guint64 ts, GstPad * pad,
    GstBufferList * list)
{
  do_log (GST_CAT_BUFFER_LIST, GST_FUNCTION, (GObject *) pad,
      "%" GST_TIME_FORMAT ", pad=%" GST_PTR_FORMAT ", list=%p",
      GST_TIME_ARGS (ts), pad, list);
}

static void
do_push_buffer_list_post (GstTracer * self, guint64 ts, GstPad * pad,
    GstFlowReturn res)
{
  do_log (GST_CAT_BUFFER_LIST, GST_FUNCTION, (GObject *) pad,
      "%" GST_TIME_FORMAT ", pad=%" GST_PTR_FORMAT ", res=%d",
      GST_TIME_ARGS (ts), pad, res);
}

static void
do_pull_range_pre (GstTracer * self, guint64 ts, GstPad * pad, guint64 offset,
    guint size)
{
  do_log (GST_CAT_BUFFER, GST_FUNCTION, (GObject *) pad,
      "%" GST_TIME_FORMAT ", pad=%" GST_PTR_FORMAT ", offset=%" G_GUINT64_FORMAT
      ", size=%u", GST_TIME_ARGS (ts), pad, offset, size);
}

static void
do_pull_range_post (GstTracer * self, guint64 ts, GstPad * pad,
    GstBuffer * buffer, GstFlowReturn res)
{
  do_log (GST_CAT_BUFFER, GST_FUNCTION, (GObject *) pad,
      "%" GST_TIME_FORMAT ", pad=%" GST_PTR_FORMAT ", buffer=%" GST_PTR_FORMAT
      ", res=%d", GST_TIME_ARGS (ts), pad, buffer, res);
}

static void
do_push_event_pre (GstTracer * self, guint64 ts, GstPad * pad, GstEvent * event)
{
  do_log (GST_CAT_EVENT, GST_FUNCTION, (GObject *) pad,
      "%" GST_TIME_FORMAT ", pad=%" GST_PTR_FORMAT ", event=%" GST_PTR_FORMAT,
      GST_TIME_ARGS (ts), pad, event);
}

static void
do_push_event_post (GstTracer * self, guint64 ts, GstPad * pad, gboolean res)
{
  do_log (GST_CAT_EVENT, GST_FUNCTION, (GObject *) pad,
      "%" GST_TIME_FORMAT ", pad=%" GST_PTR_FORMAT ", res=%d",
      GST_TIME_ARGS (ts), pad, res);
}

static void
do_pad_query_pre (GstTracer * self, guint64 ts, GstPad * pad, GstQuery * query)
{
  do_log (GST_CAT_QUERY, GST_FUNCTION, (GObject *) pad,
      "%" GST_TIME_FORMAT ", pad=%" GST_PTR_FORMAT ", query=%"
      GST_PTR_FORMAT, GST_TIME_ARGS (ts), pad, query);
}

static void
do_pad_query_post (GstTracer * self, guint64 ts, GstPad * pad, GstQuery * query,
    gboolean res)
{
  do_log (GST_CAT_QUERY, GST_FUNCTION, (GObject *) pad,
      "%" GST_TIME_FORMAT ", pad=%" GST_PTR_FORMAT ", query=%" GST_PTR_FORMAT
      ", res=%d", GST_TIME_ARGS (ts), pad, query, res);
}

static void
do_post_message_pre (GstTracer * self, guint64 ts, GstElement * elem,
    GstMessage * msg)
{
  do_log (GST_CAT_MESSAGE, GST_FUNCTION, (GObject *) elem,
      "%" GST_TIME_FORMAT ", element=%" GST_PTR_FORMAT ", message=%"
      GST_PTR_FORMAT, GST_TIME_ARGS (ts), elem, msg);
}

static void
do_post_message_post (GstTracer * self, guint64 ts, GstElement * elem,
    gboolean res)
{
  do_log (GST_CAT_MESSAGE, GST_FUNCTION, (GObject *) elem,
      "%" GST_TIME_FORMAT ", element=%" GST_PTR_FORMAT ", res=%d",
      GST_TIME_ARGS (ts), elem, res);
}

static void
do_element_query_pre (GstTracer * self, guint64 ts, GstElement * elem,
    GstQuery * query)
{
  do_log (GST_CAT_QUERY, GST_FUNCTION, (GObject *) elem,
      "%" GST_TIME_FORMAT ", element=%" GST_PTR_FORMAT ", query=%"
      GST_PTR_FORMAT, GST_TIME_ARGS (ts), elem, query);
}

static void
do_element_query_post (GstTracer * self, guint64 ts, GstElement * elem,
    GstQuery * query, gboolean res)
{
  do_log (GST_CAT_QUERY, GST_FUNCTION, (GObject *) elem,
      "%" GST_TIME_FORMAT ", element=%" GST_PTR_FORMAT ", query=%"
      GST_PTR_FORMAT ", res=%d", GST_TIME_ARGS (ts), elem, query, res);
}

static void
do_element_new (GstTracer * self, guint64 ts, GstElement * elem)
{
  do_log (GST_CAT_ELEMENT_FACTORY, GST_FUNCTION, (GObject *) elem,
      "%" GST_TIME_FORMAT ", element=%" GST_PTR_FORMAT,
      GST_TIME_ARGS (ts), elem);
}

static void
do_element_add_pad (GstTracer * self, guint64 ts, GstElement * elem,
    GstPad * pad)
{
  do_log (GST_CAT_ELEMENT_PADS, GST_FUNCTION, (GObject *) elem,
      "%" GST_TIME_FORMAT ", element=%" GST_PTR_FORMAT ", pad=%" GST_PTR_FORMAT,
      GST_TIME_ARGS (ts), elem, pad);
}

static void
do_element_remove_pad (GstTracer * self, guint64 ts, GstElement * elem,
    GstPad * pad)
{
  do_log (GST_CAT_ELEMENT_PADS, GST_FUNCTION, (GObject *) elem,
      "%" GST_TIME_FORMAT ", element=%" GST_PTR_FORMAT ", pad=%" GST_PTR_FORMAT,
      GST_TIME_ARGS (ts), elem, pad);
}

static void
do_element_change_state_pre (GstTracer * self, guint64 ts, GstElement * elem,
    GstStateChange change)
{
  do_log (GST_CAT_STATES, GST_FUNCTION, (GObject *) elem,
      "%" GST_TIME_FORMAT ", element=%" GST_PTR_FORMAT ", change=%d",
      GST_TIME_ARGS (ts), elem, (gint) change);
}

static void
do_element_change_state_post (GstTracer * self, guint64 ts, GstElement * elem,
    GstStateChange change, GstStateChangeReturn res)
{
  do_log (GST_CAT_STATES, GST_FUNCTION, (GObject *) elem,
      "%" GST_TIME_FORMAT ", element=%" GST_PTR_FORMAT ", change=%d, res=%d",
      GST_TIME_ARGS (ts), elem, (gint) change, (gint) res);
}

static void
do_bin_add_pre (GstTracer * self, guint64 ts, GstBin * bin, GstElement * elem)
{
  do_log (GST_CAT_BIN, GST_FUNCTION, (GObject *) bin,
      "%" GST_TIME_FORMAT ", bin=%" GST_PTR_FORMAT ", element=%" GST_PTR_FORMAT,
      GST_TIME_ARGS (ts), bin, elem);
}

static void
do_bin_add_post (GstTracer * self, guint64 ts, GstBin * bin, GstElement * elem,
    gboolean res)
{
  do_log (GST_CAT_BIN, GST_FUNCTION, (GObject *) bin,
      "%" GST_TIME_FORMAT ", bin=%" GST_PTR_FORMAT ", element=%" GST_PTR_FORMAT
      ", res=%d", GST_TIME_ARGS (ts), bin, elem, res);
}

static void
do_bin_remove_pre (GstTracer * self, guint64 ts, GstBin * bin,
    GstElement * elem)
{
  do_log (GST_CAT_BIN, GST_FUNCTION, (GObject *) bin,
      "%" GST_TIME_FORMAT ", bin=%" GST_PTR_FORMAT ", element=%" GST_PTR_FORMAT,
      GST_TIME_ARGS (ts), bin, elem);
}

static void
do_bin_remove_post (GstTracer * self, guint64 ts, GstBin * bin, gboolean res)
{
  do_log (GST_CAT_BIN, GST_FUNCTION, (GObject *) bin,
      "%" GST_TIME_FORMAT ", bin=%" GST_PTR_FORMAT ", res=%d",
      GST_TIME_ARGS (ts), bin, res);
}

static void
do_pad_link_pre (GstTracer * self, guint64 ts, GstPad * src, GstPad * sink)
{
  do_log (GST_CAT_PADS, GST_FUNCTION, (GObject *) src,
      "%" GST_TIME_FORMAT ", src=%" GST_PTR_FORMAT ", sink=%" GST_PTR_FORMAT,
      GST_TIME_ARGS (ts), src, sink);
}

static void
do_pad_link_post (GstTracer * self, guint64 ts, GstPad * src, GstPad * sink,
    GstPadLinkReturn res)
{
  do_log (GST_CAT_PADS, GST_FUNCTION, (GObject *) src,
      "%" GST_TIME_FORMAT ", src=%" GST_PTR_FORMAT ", sink=%" GST_PTR_FORMAT
      ", res=%d", GST_TIME_ARGS (ts), src, sink, (gint) res);
}

static void
do_pad_unlink_pre (GstTracer * self, guint64 ts, GstPad * src,
    GstElement * sink)
{
  do_log (GST_CAT_PADS, GST_FUNCTION, (GObject *) src,
      "%" GST_TIME_FORMAT ", src=%" GST_PTR_FORMAT ", sink=%" GST_PTR_FORMAT,
      GST_TIME_ARGS (ts), src, sink);
}

static void
do_pad_unlink_post (GstTracer * self, guint64 ts, GstPad * src,
    GstElement * sink, gboolean res)
{
  do_log (GST_CAT_PADS, GST_FUNCTION, (GObject *) src,
      "%" GST_TIME_FORMAT ", src=%" GST_PTR_FORMAT ", sink=%" GST_PTR_FORMAT
      ", res=%d", GST_TIME_ARGS (ts), src, sink, (gint) res);
}

/* tracer class */

static void
gst_log_tracer_class_init (GstLogTracerClass * klass)
{
}

static void
gst_log_tracer_init (GstLogTracer * self)
{
  GstTracer *tracer = GST_TRACER (self);

  gst_tracing_register_hook (tracer, "pad-push-pre",
      G_CALLBACK (do_push_buffer_pre));
  gst_tracing_register_hook (tracer, "pad-push-post",
      G_CALLBACK (do_push_buffer_post));
  gst_tracing_register_hook (tracer, "pad-push-list-pre",
      G_CALLBACK (do_push_buffer_list_pre));
  gst_tracing_register_hook (tracer, "pad-push-list-post",
      G_CALLBACK (do_push_buffer_list_post));
  gst_tracing_register_hook (tracer, "pad-pull-range-pre",
      G_CALLBACK (do_pull_range_pre));
  gst_tracing_register_hook (tracer, "pad-pull-range-post",
      G_CALLBACK (do_pull_range_post));
  gst_tracing_register_hook (tracer, "pad-push-event-pre",
      G_CALLBACK (do_push_event_pre));
  gst_tracing_register_hook (tracer, "pad-push-event-post",
      G_CALLBACK (do_push_event_post));
  gst_tracing_register_hook (tracer, "pad-query-pre",
      G_CALLBACK (do_pad_query_pre));
  gst_tracing_register_hook (tracer, "pad-query-post",
      G_CALLBACK (do_pad_query_post));
  gst_tracing_register_hook (tracer, "element-post-message-pre",
      G_CALLBACK (do_post_message_pre));
  gst_tracing_register_hook (tracer, "element-post-message-post",
      G_CALLBACK (do_post_message_post));
  gst_tracing_register_hook (tracer, "element-query-pre",
      G_CALLBACK (do_element_query_pre));
  gst_tracing_register_hook (tracer, "element-query-post",
      G_CALLBACK (do_element_query_post));
  gst_tracing_register_hook (tracer, "element-new",
      G_CALLBACK (do_element_new));
  gst_tracing_register_hook (tracer, "element-add-pad",
      G_CALLBACK (do_element_add_pad));
  gst_tracing_register_hook (tracer, "element-remove-pad",
      G_CALLBACK (do_element_remove_pad));
  gst_tracing_register_hook (tracer, "element-change-state-pre",
      G_CALLBACK (do_element_change_state_pre));
  gst_tracing_register_hook (tracer, "element-change-state-post",
      G_CALLBACK (do_element_change_state_post));
  gst_tracing_register_hook (tracer, "bin-add-pre",
      G_CALLBACK (do_bin_add_pre));
  gst_tracing_register_hook (tracer, "bin-add-post",
      G_CALLBACK (do_bin_add_post));
  gst_tracing_register_hook (tracer, "bin-remove-pre",
      G_CALLBACK (do_bin_remove_pre));
  gst_tracing_register_hook (tracer, "bin-remove-post",
      G_CALLBACK (do_bin_remove_post));
  gst_tracing_register_hook (tracer, "pad-link-pre",
      G_CALLBACK (do_pad_link_pre));
  gst_tracing_register_hook (tracer, "pad-link-post",
      G_CALLBACK (do_pad_link_post));
  gst_tracing_register_hook (tracer, "pad-unlink-pre",
      G_CALLBACK (do_pad_unlink_pre));
  gst_tracing_register_hook (tracer, "pad-unlink-post",
      G_CALLBACK (do_pad_unlink_post));
}
