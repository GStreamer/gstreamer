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

static void gst_log_tracer_invoke (GstTracer * self, GstTracerMessageId mid,
    va_list var_args);

static void
gst_log_tracer_class_init (GstLogTracerClass * klass)
{
  GstTracerClass *gst_tracer_class = GST_TRACER_CLASS (klass);

  gst_tracer_class->invoke = gst_log_tracer_invoke;
}

static void
gst_log_tracer_init (GstLogTracer * self)
{
  g_object_set (self, "mask", GST_TRACER_HOOK_ALL, NULL);
}

static void
gst_log_tracer_invoke (GstTracer * self, GstTracerMessageId mid,
    va_list var_args)
{
  const gchar *fmt = NULL;
  GstDebugCategory *cat = GST_CAT_DEFAULT;
  guint64 ts = va_arg (var_args, guint64);

  /* TODO(ensonic): log to different categories depending on 'mid'
   * GST_TRACER_HOOK_ID_QUERIES  -> (static category)
   * GST_TRACER_HOOK_ID_TOPLOGY  -> ?
   */
  switch (mid) {
    case GST_TRACER_MESSAGE_ID_PAD_PUSH_PRE:
      cat = GST_CAT_BUFFER;
      fmt = "pad=%" GST_PTR_FORMAT ", buffer=%" GST_PTR_FORMAT;
      break;
    case GST_TRACER_MESSAGE_ID_PAD_PUSH_POST:
      cat = GST_CAT_BUFFER;
      fmt = "pad=%" GST_PTR_FORMAT ", res=%d";
      break;
    case GST_TRACER_MESSAGE_ID_PAD_PUSH_LIST_PRE:
      cat = GST_CAT_BUFFER_LIST;
      fmt = "pad=%" GST_PTR_FORMAT ", list=%p";
      break;
    case GST_TRACER_MESSAGE_ID_PAD_PUSH_LIST_POST:
      cat = GST_CAT_BUFFER_LIST;
      fmt = "pad=%" GST_PTR_FORMAT ", res=%d";
      break;
    case GST_TRACER_MESSAGE_ID_PAD_PULL_RANGE_PRE:
      cat = GST_CAT_BUFFER;
      fmt = "pad=%" GST_PTR_FORMAT ", offset=%" G_GUINT64_FORMAT ", size=%u";
      break;
    case GST_TRACER_MESSAGE_ID_PAD_PULL_RANGE_POST:
      cat = GST_CAT_BUFFER;
      fmt = "pad=%" GST_PTR_FORMAT ", buffer=%" GST_PTR_FORMAT ", res=%d";
      break;
    case GST_TRACER_MESSAGE_ID_PAD_PUSH_EVENT_PRE:
      cat = GST_CAT_EVENT;
      fmt = "pad=%" GST_PTR_FORMAT ", event=%" GST_PTR_FORMAT;
      break;
    case GST_TRACER_MESSAGE_ID_PAD_PUSH_EVENT_POST:
      cat = GST_CAT_EVENT;
      fmt = "pad=%" GST_PTR_FORMAT ", res=%d";
      break;
    case GST_TRACER_MESSAGE_ID_ELEMENT_POST_MESSAGE_PRE:
      cat = GST_CAT_MESSAGE;
      fmt = "element=%" GST_PTR_FORMAT ", message=%" GST_PTR_FORMAT;
      break;
    case GST_TRACER_MESSAGE_ID_ELEMENT_POST_MESSAGE_POST:
      cat = GST_CAT_MESSAGE;
      fmt = "element=%" GST_PTR_FORMAT ", res=%d";
      break;
    case GST_TRACER_MESSAGE_ID_ELEMENT_QUERY_PRE:
      cat = GST_CAT_QUERY;
      fmt = "element=%" GST_PTR_FORMAT ", query=%" GST_PTR_FORMAT;
      break;
    case GST_TRACER_MESSAGE_ID_ELEMENT_QUERY_POST:
      cat = GST_CAT_QUERY;
      fmt = "element=%" GST_PTR_FORMAT ", res=%d";
      break;
    default:
      break;
  }
  if (fmt) {
    gchar *str;

    __gst_vasprintf (&str, fmt, var_args);
    GST_CAT_TRACE (cat, "[%d] %" GST_TIME_FORMAT ", %s",
        mid, GST_TIME_ARGS (ts), str);
    g_free (str);
  } else {
    GST_CAT_TRACE (cat, "[%d] %" GST_TIME_FORMAT, mid, GST_TIME_ARGS (ts));
  }
}
