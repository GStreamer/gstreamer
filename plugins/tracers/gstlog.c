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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstlog.h"

GST_DEBUG_CATEGORY_STATIC (gst_log_debug);
#define GST_CAT_DEFAULT gst_log_debug

#define _do_init \
    GST_DEBUG_CATEGORY_INIT (gst_log_debug, "log", 0, "log tracer");
#define gst_log_tracer_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstLogTracer, gst_log_tracer, GST_TYPE_TRACER,
    _do_init);

static void gst_log_tracer_invoke (GstTracerHookId id, GstStructure * s);

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
gst_log_tracer_invoke (GstTracerHookId id, GstStructure * s)
{
  gchar *str = gst_structure_to_string (s);
  /* TODO(ensonic): log to different categories depending on 'id'
   * GST_TRACER_HOOK_ID_BUFFERS  -> GST_CAT_BUFFER
   * GST_TRACER_HOOK_ID_EVENTS   -> GST_CAT_EVENT
   * GST_TRACER_HOOK_ID_MESSAGES -> GST_CAT_MESSAGE
   * GST_TRACER_HOOK_ID_QUERIES  -> ?
   * GST_TRACER_HOOK_ID_TOPLOGY  -> ?
   */
  GST_TRACE ("%s", str);
  g_free (str);
}
