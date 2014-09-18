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
 * SECTION:gstlog
 * @short_description: log hook event
 *
 * A tracing module that logs all data from all hooks. 
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstlog.h"

#include <gst/printf/printf.h>

GST_DEBUG_CATEGORY_STATIC (gst_log_debug);
#define GST_CAT_DEFAULT gst_log_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_BUFFER);
GST_DEBUG_CATEGORY_STATIC (GST_CAT_BUFFER_LIST);
GST_DEBUG_CATEGORY_STATIC (GST_CAT_EVENT);
GST_DEBUG_CATEGORY_STATIC (GST_CAT_MESSAGE);
GST_DEBUG_CATEGORY_STATIC (GST_CAT_QUERY);

#define _do_init \
    GST_DEBUG_CATEGORY_INIT (gst_log_debug, "log", 0, "log tracer"); \
    GST_DEBUG_CATEGORY_GET (GST_CAT_BUFFER, "GST_BUFFER"); \
    GST_DEBUG_CATEGORY_GET (GST_CAT_BUFFER_LIST, "GST_BUFFER_LIST"); \
    GST_DEBUG_CATEGORY_GET (GST_CAT_EVENT, "GST_EVENT"); \
    GST_DEBUG_CATEGORY_GET (GST_CAT_MESSAGE, "GST_MESSAGE"); \
    GST_DEBUG_CATEGORY_GET (GST_CAT_QUERY, "query");
#define gst_log_tracer_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstLogTracer, gst_log_tracer, GST_TYPE_TRACER,
    _do_init);

static void
do_log (GstDebugCategory * cat, const char *fmt, ...)
{
  va_list var_args;

  va_start (var_args, fmt);
  gst_debug_log_valist (cat, GST_LEVEL_TRACE, "", "", 0, NULL, fmt, var_args);
  va_end (var_args);
}

static void
do_push_buffer_pre (GstTracer * self, guint64 ts, GstPad * pad,
    GstBuffer * buffer)
{
  do_log (GST_CAT_BUFFER,
      "%" GST_TIME_FORMAT ", pad=%" GST_PTR_FORMAT ", buffer=%" GST_PTR_FORMAT,
      ts, pad, buffer);
}

static void
do_push_buffer_post (GstTracer * self, guint64 ts, GstPad * pad,
    GstFlowReturn res)
{
  do_log (GST_CAT_BUFFER,
      "%" GST_TIME_FORMAT ", pad=%" GST_PTR_FORMAT ", res=%d", ts, pad, res);
}

static void
do_push_buffer_list_pre (GstTracer * self, guint64 ts, GstPad * pad,
    GstBufferList * list)
{
  do_log (GST_CAT_BUFFER_LIST,
      "%" GST_TIME_FORMAT ", pad=%" GST_PTR_FORMAT ", list=%p", ts, pad, list);
}

static void
do_push_buffer_list_post (GstTracer * self, guint64 ts, GstPad * pad,
    GstFlowReturn res)
{
  do_log (GST_CAT_BUFFER_LIST,
      "%" GST_TIME_FORMAT ", pad=%" GST_PTR_FORMAT ", res=%d", ts, pad, res);
}

static void
do_pull_range_pre (GstTracer * self, guint64 ts, GstPad * pad, guint64 offset,
    guint size)
{
  do_log (GST_CAT_BUFFER,
      "%" GST_TIME_FORMAT ", pad=%" GST_PTR_FORMAT ", offset=%" G_GUINT64_FORMAT
      ", size=%u", ts, pad, offset, size);
}

static void
do_pull_range_post (GstTracer * self, guint64 ts, GstPad * pad,
    GstBuffer * buffer, GstFlowReturn res)
{
  do_log (GST_CAT_BUFFER,
      "%" GST_TIME_FORMAT ", pad=%" GST_PTR_FORMAT ", buffer=%" GST_PTR_FORMAT
      ", res=%d", ts, pad, buffer, res);
}

static void
do_push_event_pre (GstTracer * self, guint64 ts, GstPad * pad, GstEvent * event)
{
  do_log (GST_CAT_EVENT,
      "%" GST_TIME_FORMAT ", pad=%" GST_PTR_FORMAT ", event=%" GST_PTR_FORMAT,
      ts, pad, event);
}

static void
do_push_event_post (GstTracer * self, guint64 ts, GstPad * pad, gboolean res)
{
  do_log (GST_CAT_EVENT,
      "%" GST_TIME_FORMAT ", pad=%" GST_PTR_FORMAT ", res=%d", ts, pad, res);
}

static void
do_post_message_pre (GstTracer * self, guint64 ts, GstElement * elem,
    GstMessage * msg)
{
  do_log (GST_CAT_EVENT,
      "%" GST_TIME_FORMAT ", element=%" GST_PTR_FORMAT ", message=%"
      GST_PTR_FORMAT, ts, elem, msg);
}

static void
do_post_message_post (GstTracer * self, guint64 ts, GstElement * elem,
    gboolean res)
{
  do_log (GST_CAT_EVENT,
      "%" GST_TIME_FORMAT ", element=%" GST_PTR_FORMAT ", res=%d", ts, elem,
      res);
}

static void
do_query_pre (GstTracer * self, guint64 ts, GstElement * elem, GstQuery * query)
{
  do_log (GST_CAT_QUERY,
      "%" GST_TIME_FORMAT ", element=%" GST_PTR_FORMAT ", query=%"
      GST_PTR_FORMAT, ts, elem, query);
}

static void
do_query_post (GstTracer * self, guint64 ts, GstElement * elem, gboolean res)
{
  do_log (GST_CAT_QUERY,
      "%" GST_TIME_FORMAT ", element=%" GST_PTR_FORMAT ", res=%d", ts, elem,
      res);
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

  gst_tracer_register_hook (tracer, "pad-push-pre",
      G_CALLBACK (do_push_buffer_pre));
  gst_tracer_register_hook (tracer, "pad-push-post",
      G_CALLBACK (do_push_buffer_post));
  gst_tracer_register_hook (tracer, "pad-push-list-pre",
      G_CALLBACK (do_push_buffer_list_pre));
  gst_tracer_register_hook (tracer, "pad-push-list-post",
      G_CALLBACK (do_push_buffer_list_post));
  gst_tracer_register_hook (tracer, "pad-pull-range-pre",
      G_CALLBACK (do_pull_range_pre));
  gst_tracer_register_hook (tracer, "pad-pull-range-post",
      G_CALLBACK (do_pull_range_post));
  gst_tracer_register_hook (tracer, "pad-push-event-pre",
      G_CALLBACK (do_push_event_pre));
  gst_tracer_register_hook (tracer, "pad-push-event-post",
      G_CALLBACK (do_push_event_post));
  gst_tracer_register_hook (tracer, "element-post-message-pre",
      G_CALLBACK (do_post_message_pre));
  gst_tracer_register_hook (tracer, "element-post-message-post",
      G_CALLBACK (do_post_message_post));
  gst_tracer_register_hook (tracer, "element-query-pre",
      G_CALLBACK (do_query_pre));
  gst_tracer_register_hook (tracer, "element-query-post",
      G_CALLBACK (do_query_post));
}
