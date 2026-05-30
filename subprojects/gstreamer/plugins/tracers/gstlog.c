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
 * SECTION:tracer-log
 * @short_description: log hook event
 *
 * A tracing module that logs all data from all GstTracer hooks. Takes no
 * arguments other than an optional name.
 *
 * ### Enabling the log tracer
 *
 * Enable through an environment variable: `GST_TRACERS=log` (notice
 * the plural).
 *
 * You can double check the plugin has been enabled using
 * `GST_DEBUG='*:INFO'`. You should see:
 *
 * ```
 * $ GST_TRACERS="log" GST_DEBUG='*:INFO' \
 *      gst-launch-1.0 fakesrc num-buffers=1 ! fakesink \
 *      2>&1 | grep "enabling tracers"
[...] _priv_gst_tracing_init: enabling tracers: 'log'
 * ```
 *
 * ### Using the log tracer
 *
 * This tracer logs accross a number of categories at the `TRACE` level.
 *
 * **For this reason, you need to set `GST_DEBUG` to capture the output from
 * this plugin.**
 *
 * These are the logging categories under which the different hooks operate:
 *
 *  * `GST_DEBUG=GST_BUFFER:TRACE`
 *    * `pad-push-pre`, `pad-push-post`
 *    * `pad-chain-pre`, `pad-chain-post`
 *    * `pad-pull-range-pre`, `pad-pull-range-post`
 *  * `GST_DEBUG=GST_BUFFER_LIST:TRACE`
 *    * `pad-push-list-pre`, `pad-push-list-post`
 *    * `pad-chain-list-pre`, `pad-chain-list-post`
 *  * `GST_DEBUG=GST_EVENT:TRACE`
 *    * `pad-push-event-pre`, `pad-push-event-post`
 *    * `pad-send-event-pre`, `pad-send-event-post`
 *  * `GST_DEBUG=query:TRACE`
 *    * `pad-query-pre`, `pad-query-post`
 *    * `element-query-pre`, `element-query-post`
 *  * `GST_DEBUG=GST_MESSAGE:TRACE`
 *    * `element-post-message-pre`, `element-post-message-post`
 *  * `GST_DEBUG=GST_ELEMENT_FACTORY:TRACE`
 *    * `element-new`
 *  * `GST_DEBUG=GST_ELEMENT_PADS:TRACE`
 *    * `element-add-pad`
 *    * `element-remove-pad`
 *  * `GST_DEBUG=GST_STATES:TRACE`
 *    * `element-change-state-pre`, `element-change-state-post`
 *  * `GST_DEBUG=bin:TRACE`
 *    * `bin-add-pre`, `bin-add-post`
 *    * `bin-remove-pre`, `bin-remove-post`
 *  * `GST_DEBUG=GST_PADS:TRACE`
 *    * `pad-link-pre`, `pad-link-post`
 *    * `pad-unlink-pre`, `pad-unlink-post`
 *
 * Since the categories mentioned above are not exclusive to this tracer
 * plugin, but are also used by core GStreamer code, you should expect a lot of
 * unrelated logging to appear.
 *
 * On the other hand, the functions in this plugin have a consistent naming
 * scheme, which should make it easy to filter the logs: `do_{hook_name}`
 *
 * ### Example
 *
 * As an example, if we wanted to log the flow of events and pads being linked
 * we could run the following command:
 *
 * ```
 * $ GST_TRACERS="log" \
 *       GST_DEBUG=GST_EVENT:TRACE,GST_PADS:TRACE \
 *       gst-play-1.0 file.webm \
 *       2>&1 | egrep -w 'do_(pad_link)_(pre|post):'
 * [...]
 * [...] GST_PADS :0:do_pad_link_pre:<typefind:src> 0:00:00.096516923, src=<typefind:src>, sink=<matroskademux0:sink>
 * [...] GST_PADS :0:do_pad_link_post:<typefind:src> 0:00:00.096678191, src=<typefind:src>, sink=<matroskademux0:sink>, res=0
 * [...] GST_PADS :0:do_pad_link_pre:<matroskademux0:audio_0> 0:00:00.103133773, src=<matroskademux0:audio_0>, sink=<decodepad1:proxypad2>
 * [...] GST_PADS :0:do_pad_link_post:<matroskademux0:audio_0> 0:00:00.103567148, src=<matroskademux0:audio_0>, sink=<decodepad1:proxypad2>, res=0
 * [...]
 * [...] GST_EVENT :0:do_push_event_pre:<vp8dec0:sink> 0:00:00.930848627, pad=<vp8dec0:sink>, event=qos event: 0x7fec9c00c0a0, time 99:99:99.999999999, seq-num 393, GstEventQOS, type=(GstQOSType)overflow, proportion=(double)0.036137789409526271, diff=(gint64)-29350000, timestamp=(guint64)533000000;
 * [...] GST_EVENT :0:do_push_event_pre:<multiqueue0:sink_1> 0:00:00.930901498, pad=<multiqueue0:sink_1>, event=qos event: 0x7fec9c00c0a0, time 99:99:99.999999999, seq-num 393, GstEventQOS, type=(GstQOSType)overflow, proportion=(double)0.036137789409526271, diff=(gint64)-29350000, timestamp=(guint64)533000000;
 * [...] GST_EVENT :0:do_push_event_post:<multiqueue0:sink_1> 0:00:00.931041882, pad=<multiqueue0:sink_1>, res=1
 * [...] GST_EVENT :0:do_push_event_post:<vp8dec0:sink> 0:00:00.931082112, pad=<vp8dec0:sink>, res=1
 * [...]
 * ```
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
GST_DEBUG_CATEGORY_STATIC (GST_CAT_TRACER);

#define _do_init \
    GST_DEBUG_CATEGORY_INIT (gst_log_debug, "log", 0, "log tracer"); \
    GST_DEBUG_CATEGORY_GET (GST_CAT_TRACER, "GST_TRACER"); \
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

#define bool_to_str(val) (val ? "true" : "false")

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
      "%" GST_TIME_FORMAT ", pad=%" GST_PTR_FORMAT ", res=%s",
      GST_TIME_ARGS (ts), pad, gst_flow_get_name (res));
}

static void
do_push_buffer_list_pre (GstTracer * self, guint64 ts, GstPad * pad,
    GstBufferList * list)
{
  do_log (GST_CAT_BUFFER_LIST, GST_FUNCTION, (GObject *) pad,
      "%" GST_TIME_FORMAT ", pad=%" GST_PTR_FORMAT ", list=%" GST_PTR_FORMAT,
      GST_TIME_ARGS (ts), pad, list);
}

static void
do_push_buffer_list_post (GstTracer * self, guint64 ts, GstPad * pad,
    GstFlowReturn res)
{
  do_log (GST_CAT_BUFFER_LIST, GST_FUNCTION, (GObject *) pad,
      "%" GST_TIME_FORMAT ", pad=%" GST_PTR_FORMAT ", res=%s",
      GST_TIME_ARGS (ts), pad, gst_flow_get_name (res));
}

static void
do_chain_buffer_pre (GstTracer * self, guint64 ts, GstPad * pad,
    GstBuffer * buffer)
{
  do_log (GST_CAT_BUFFER, GST_FUNCTION, (GObject *) pad,
      "%" GST_TIME_FORMAT ", pad=%" GST_PTR_FORMAT ", buffer=%" GST_PTR_FORMAT,
      GST_TIME_ARGS (ts), pad, buffer);
}

static void
do_chain_buffer_post (GstTracer * self, guint64 ts, GstPad * pad,
    GstFlowReturn res)
{
  do_log (GST_CAT_BUFFER, GST_FUNCTION, (GObject *) pad,
      "%" GST_TIME_FORMAT ", pad=%" GST_PTR_FORMAT ", res=%s",
      GST_TIME_ARGS (ts), pad, gst_flow_get_name (res));
}

static void
do_chain_buffer_list_pre (GstTracer * self, guint64 ts, GstPad * pad,
    GstBufferList * list)
{
  do_log (GST_CAT_BUFFER_LIST, GST_FUNCTION, (GObject *) pad,
      "%" GST_TIME_FORMAT ", pad=%" GST_PTR_FORMAT ", list=%" GST_PTR_FORMAT,
      GST_TIME_ARGS (ts), pad, list);
}

static void
do_chain_buffer_list_post (GstTracer * self, guint64 ts, GstPad * pad,
    GstFlowReturn res)
{
  do_log (GST_CAT_BUFFER_LIST, GST_FUNCTION, (GObject *) pad,
      "%" GST_TIME_FORMAT ", pad=%" GST_PTR_FORMAT ", res=%s",
      GST_TIME_ARGS (ts), pad, gst_flow_get_name (res));
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
      ", res=%s", GST_TIME_ARGS (ts), pad, buffer, gst_flow_get_name (res));
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
      "%" GST_TIME_FORMAT ", pad=%" GST_PTR_FORMAT ", res=%s",
      GST_TIME_ARGS (ts), pad, bool_to_str (res));
}

static void
do_send_event_pre (GstTracer * self, guint64 ts, GstPad * pad, GstEvent * event)
{
  do_log (GST_CAT_EVENT, GST_FUNCTION, (GObject *) pad,
      "%" GST_TIME_FORMAT ", pad=%" GST_PTR_FORMAT ", event=%" GST_PTR_FORMAT,
      GST_TIME_ARGS (ts), pad, event);
}

static void
do_send_event_post (GstTracer * self, guint64 ts, GstPad * pad,
    GstFlowReturn res)
{
  do_log (GST_CAT_EVENT, GST_FUNCTION, (GObject *) pad,
      "%" GST_TIME_FORMAT ", pad=%" GST_PTR_FORMAT ", res=%s",
      GST_TIME_ARGS (ts), pad, gst_flow_get_name (res));
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
      ", res=%s", GST_TIME_ARGS (ts), pad, query, bool_to_str (res));
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
      "%" GST_TIME_FORMAT ", element=%" GST_PTR_FORMAT ", res=%s",
      GST_TIME_ARGS (ts), elem, bool_to_str (res));
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
      GST_PTR_FORMAT ", res=%s", GST_TIME_ARGS (ts), elem, query,
      bool_to_str (res));
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
      "%" GST_TIME_FORMAT ", element=%" GST_PTR_FORMAT ", change=%s",
      GST_TIME_ARGS (ts), elem, gst_state_change_get_name (change));
}

static void
do_element_change_state_post (GstTracer * self, guint64 ts, GstElement * elem,
    GstStateChange change, GstStateChangeReturn res)
{
  do_log (GST_CAT_STATES, GST_FUNCTION, (GObject *) elem,
      "%" GST_TIME_FORMAT ", element=%" GST_PTR_FORMAT ", change=%s, res=%s",
      GST_TIME_ARGS (ts), elem, gst_state_change_get_name (change),
      gst_state_change_return_get_name (res));
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
      ", res=%s", GST_TIME_ARGS (ts), bin, elem, bool_to_str (res));
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
      "%" GST_TIME_FORMAT ", bin=%" GST_PTR_FORMAT ", res=%s",
      GST_TIME_ARGS (ts), bin, bool_to_str (res));
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
      ", res=%s", GST_TIME_ARGS (ts), src, sink, gst_pad_link_get_name (res));
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
      ", res=%s", GST_TIME_ARGS (ts), src, sink, bool_to_str (res));
}

/* Sets one event field on @s from its declared @type and borrowed value @v. */
static void
log_event_set_field (GstStructure * s, const gchar * name,
    GstTracerFieldType type, const GstTraceValue * v)
{
  if (!v) {
    gst_structure_set (s, name, G_TYPE_STRING, "NULL", NULL);
    return;
  }

  switch (type) {
    case GST_TRACER_FIELD_TYPE_BOOLEAN:
      gst_structure_set (s, name, G_TYPE_BOOLEAN, v->v_boolean, NULL);
      break;
    case GST_TRACER_FIELD_TYPE_INT:
      gst_structure_set (s, name, G_TYPE_INT, v->v_int, NULL);
      break;
    case GST_TRACER_FIELD_TYPE_UINT:
      gst_structure_set (s, name, G_TYPE_UINT, v->v_uint, NULL);
      break;
    case GST_TRACER_FIELD_TYPE_INT64:
      gst_structure_set (s, name, G_TYPE_INT64, v->v_int64, NULL);
      break;
    case GST_TRACER_FIELD_TYPE_UINT64:
      gst_structure_set (s, name, G_TYPE_UINT64, v->v_uint64, NULL);
      break;
    case GST_TRACER_FIELD_TYPE_CLOCK_TIME:
      gst_structure_set (s, name, GST_TYPE_CLOCK_TIME, v->v_uint64, NULL);
      break;
    case GST_TRACER_FIELD_TYPE_DOUBLE:
      gst_structure_set (s, name, G_TYPE_DOUBLE, v->v_double, NULL);
      break;
    case GST_TRACER_FIELD_TYPE_STRING:
      gst_structure_set (s, name, G_TYPE_STRING, v->v_string, NULL);
      break;
    case GST_TRACER_FIELD_TYPE_STRUCTURE:
      gst_structure_set (s, name, GST_TYPE_STRUCTURE, v->v_structure, NULL);
      break;
    case GST_TRACER_FIELD_TYPE_OBJECT:{
      /* Render an opaque identity; @v_object may be a non-GObject mini-object
       * so only its address is logged, never dereferenced. */
      gchar *p = g_strdup_printf ("%p", v->v_object);
      gst_structure_set (s, name, G_TYPE_STRING, p, NULL);
      g_free (p);
      break;
    }
  }
}

/* Renders an application-defined point event to the GST_TRACER debug log in
 * the #GstStructure form `gst-stats` consumes. */
/* Formats whose schema has already been announced, so each GstTraceFormat is
 * described exactly once. Formats live until gst_deinit() so the pointer is a
 * stable key. */
static GHashTable *announced_formats;   /* set of GstTraceFormat* */
static GMutex announced_formats_lock;

/* Announce a format's schema as a "<name>.class, <field>=(structure)...;" line
 * the first time it is rendered, mirroring the legacy GstTracerRecord
 * announcement so log post-processors (gst-stats, the python tracer framework)
 * can learn each record's field layout. The field type is reported as the
 * GstTracerFieldType nick. */
static void
maybe_announce_format (GstTraceFormat * format)
{
  guint n_fields, i;
  GstStructure *klass;
  gchar *name, *str;

  g_mutex_lock (&announced_formats_lock);
  if (G_UNLIKELY (!announced_formats))
    announced_formats = g_hash_table_new (NULL, NULL);
  if (g_hash_table_contains (announced_formats, format)) {
    g_mutex_unlock (&announced_formats_lock);
    return;
  }
  g_hash_table_add (announced_formats, format);
  g_mutex_unlock (&announced_formats_lock);

  name = g_strconcat (gst_trace_format_get_name (format), ".class", NULL);
  klass = gst_structure_new_empty (name);
  g_free (name);

  n_fields = gst_trace_format_get_n_fields (format);
  for (i = 0; i < n_fields; i++) {
    const gchar *fname = gst_trace_format_get_field_name (format, i);
    const GstStructure *meta = gst_trace_format_get_field_structure (format, i);
    GstTracerFieldType type = gst_trace_format_get_field_type (format, i);
    GstTracerValueScope scope;
    GstStructure *desc;

    /* Mirror the legacy GstTracerRecord schema so existing log post-processors
     * keep working: a field that relates to an entity is described by a
     * "scope" sub-structure carrying "related-to", everything else by a
     * "value" sub-structure. The field type is rendered as its
     * GstTracerFieldType nick rather than the raw gint it is stored as. */
    if (meta && gst_structure_get_enum (meta, "scope",
            GST_TYPE_TRACER_VALUE_SCOPE, (gint *) & scope)) {
      desc = gst_structure_copy (meta);
      gst_structure_set_name (desc, "scope");
      gst_structure_remove_field (desc, "scope");
      gst_structure_set (desc, "type", GST_TYPE_TRACER_FIELD_TYPE, type,
          "related-to", GST_TYPE_TRACER_VALUE_SCOPE, scope, NULL);
    } else {
      desc =
          meta ? gst_structure_copy (meta) : gst_structure_new_empty ("value");
      gst_structure_set_name (desc, "value");
      gst_structure_set (desc, "type", GST_TYPE_TRACER_FIELD_TYPE, type, NULL);
    }

    gst_structure_set (klass, fname, GST_TYPE_STRUCTURE, desc, NULL);
    gst_structure_free (desc);
  }

  str = gst_structure_to_string (klass);
  gst_debug_log (GST_CAT_TRACER, GST_LEVEL_TRACE, "", "", 0, NULL, "%s", str);
  g_free (str);
  gst_structure_free (klass);
}

static void
do_event (GstTracer * self, GstClockTime ts, GstTraceFormat * format,
    const GstTraceValue * values)
{
  guint n_fields, i, vi;
  GstStructure *s;
  gchar *str;

  if (G_UNLIKELY (!format))
    return;

  maybe_announce_format (format);

  n_fields = gst_trace_format_get_n_fields (format);
  s = gst_structure_new_empty (gst_trace_format_get_name (format));

  for (i = 0, vi = 0; i < n_fields; i++) {
    const gchar *fname = gst_trace_format_get_field_name (format, i);
    GstTracerFieldType type = gst_trace_format_get_field_type (format, i);
    const GstStructure *meta = gst_trace_format_get_field_structure (format, i);
    GstTracerValueFlags flags = GST_TRACER_VALUE_FLAGS_NONE;

    gst_structure_get (meta, "flags", GST_TYPE_TRACER_VALUE_FLAGS, &flags,
        NULL);

    /* Optional fields are preceded by a "have-<field>" boolean. */
    if (flags & GST_TRACER_VALUE_FLAGS_OPTIONAL) {
      gchar *have_name = g_strconcat ("have-", fname, NULL);
      gst_structure_set (s, have_name, G_TYPE_BOOLEAN,
          values ? values[vi].v_boolean : FALSE, NULL);
      g_free (have_name);
      vi++;
    }

    log_event_set_field (s, fname, type, values ? &values[vi] : NULL);
    vi++;
  }

  str = gst_structure_to_string (s);
  gst_debug_log (GST_CAT_TRACER, GST_LEVEL_TRACE, "", "", 0, NULL, "%s", str);
  g_free (str);
  gst_structure_free (s);
}

/* tracer class */

static void
gst_log_tracer_constructed (GObject * object)
{
  GstLogTracer *self = GST_LOG_TRACER (object);
  gchar *params, *tmp;
  const gchar *name;
  GstStructure *params_struct = NULL;

  G_OBJECT_CLASS (parent_class)->constructed (object);

  g_object_get (self, "params", &params, NULL);

  if (!params)
    return;

  tmp = g_strdup_printf ("log,%s", params);
  params_struct = gst_structure_from_string (tmp, NULL);
  g_free (tmp);
  if (!params_struct)
    return;

  /* Set the name if assigned */
  name = gst_structure_get_string (params_struct, "name");
  if (name)
    gst_object_set_name (GST_OBJECT (self), name);
  gst_structure_free (params_struct);
}

static void
gst_log_tracer_class_init (GstLogTracerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->constructed = gst_log_tracer_constructed;
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
  gst_tracing_register_hook (tracer, "pad-chain-pre",
      G_CALLBACK (do_chain_buffer_pre));
  gst_tracing_register_hook (tracer, "pad-chain-post",
      G_CALLBACK (do_chain_buffer_post));
  gst_tracing_register_hook (tracer, "pad-chain-list-pre",
      G_CALLBACK (do_chain_buffer_list_pre));
  gst_tracing_register_hook (tracer, "pad-chain-list-post",
      G_CALLBACK (do_chain_buffer_list_post));
  gst_tracing_register_hook (tracer, "pad-pull-range-pre",
      G_CALLBACK (do_pull_range_pre));
  gst_tracing_register_hook (tracer, "pad-pull-range-post",
      G_CALLBACK (do_pull_range_post));
  gst_tracing_register_hook (tracer, "pad-push-event-pre",
      G_CALLBACK (do_push_event_pre));
  gst_tracing_register_hook (tracer, "pad-push-event-post",
      G_CALLBACK (do_push_event_post));
  gst_tracing_register_hook (tracer, "pad-send-event-pre",
      G_CALLBACK (do_send_event_pre));
  gst_tracing_register_hook (tracer, "pad-send-event-post",
      G_CALLBACK (do_send_event_post));
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
  gst_tracing_register_hook (tracer, "event", G_CALLBACK (do_event));
}
