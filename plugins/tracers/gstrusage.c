/* GStreamer
 * Copyright (C) 2013 Stefan Sauer <ensonic@users.sf.net>
 *
 * gstrusage.c: tracing module that logs resource usage stats
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
 * SECTION:gstrusage
 * @short_description: log resource usage stats
 *
 * A tracing module that take rusage() snapshots and logs them.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <unistd.h>
#include "gstrusage.h"

#ifdef HAVE_SYS_RESOURCE_H
#ifndef __USE_GNU
# define __USE_GNU              /* RUSAGE_THREAD */
#endif
#include <sys/resource.h>
#endif

GST_DEBUG_CATEGORY_STATIC (gst_rusage_debug);
#define GST_CAT_DEFAULT gst_rusage_debug

G_LOCK_DEFINE (_proc);

#define _do_init \
    GST_DEBUG_CATEGORY_INIT (gst_rusage_debug, "rusage", 0, "rusage tracer");
#define gst_rusage_tracer_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstRUsageTracer, gst_rusage_tracer, GST_TYPE_TRACER,
    _do_init);

/* remember x measurements per self->window */
#define WINDOW_SUBDIV 100

/* number of cpus to scale cpu-usage in threads */
static glong num_cpus = 1;

static GstTracerRecord *tr_proc, *tr_thread;

typedef struct
{
  /* time spend in this thread */
  GstClockTime tthread;
  GstTraceValues *tvs_thread;
} GstThreadStats;

/* data helper */

static void
free_trace_value (gpointer data)
{
  g_slice_free (GstTraceValue, data);
}

static GstTraceValues *
make_trace_values (GstClockTime window)
{
  GstTraceValues *self = g_slice_new0 (GstTraceValues);
  self->window = window;
  g_queue_init (&self->values);
  return self;
}

static void
free_trace_values (GstTraceValues * self)
{
  g_queue_foreach (&self->values, (GFunc) free_trace_value, NULL);
  g_queue_clear (&self->values);
  g_slice_free (GstTraceValues, self);
}

static gboolean
update_trace_value (GstTraceValues * self, GstClockTime nts,
    GstClockTime nval, GstClockTime * dts, GstClockTime * dval)
{
  GstTraceValue *lv;
  GstClockTimeDiff dt;
  GstClockTime window = self->window;
  GQueue *q = &self->values;
  GList *node = q->tail;
  gboolean ret = FALSE;


  /* search from the tail of the queue for a good GstTraceValue */
  while (node) {
    lv = node->data;
    dt = GST_CLOCK_DIFF (lv->ts, nts);
    if (dt < window) {
      break;
    } else {
      node = g_list_previous (node);
    }
  }

  if (node) {
    /* calculate the windowed value */
    *dts = dt;
    *dval = GST_CLOCK_DIFF (lv->val, nval);

    /* drop all older measurements */
    while (q->tail != node) {
      free_trace_value (g_queue_pop_tail (q));
    }
    ret = TRUE;
  } else {
    *dts = nts;
    *dval = nval;
  }

  /* don't push too many data items */
  lv = q->head ? q->head->data : NULL;
  if (!lv || (GST_CLOCK_DIFF (lv->ts, nts) > (window / WINDOW_SUBDIV))) {
    /* push the new measurement */
    lv = g_slice_new0 (GstTraceValue);
    lv->ts = nts;
    lv->val = nval;
    g_queue_push_head (q, lv);
  }
  return ret;
}


static void
free_thread_stats (gpointer data)
{
  free_trace_values (((GstThreadStats *) data)->tvs_thread);
  g_slice_free (GstThreadStats, data);
}

static void
do_stats (GstTracer * obj, guint64 ts)
{
  GstRUsageTracer *self = GST_RUSAGE_TRACER_CAST (obj);
  GstThreadStats *stats;
  gpointer thread_id = g_thread_self ();
  guint avg_cpuload, cur_cpuload;
  struct rusage ru;
  GstClockTime tproc = G_GUINT64_CONSTANT (0);
  GstClockTime tthread = G_GUINT64_CONSTANT (0);
  GstClockTime dts, dtproc;

#ifdef HAVE_CLOCK_GETTIME
  {
    struct timespec now;

    if (!clock_gettime (CLOCK_PROCESS_CPUTIME_ID, &now)) {
      tproc = GST_TIMESPEC_TO_TIME (now);
    } else {
      GST_WARNING_OBJECT (obj,
          "clock_gettime (CLOCK_PROCESS_CPUTIME_ID,...) failed: %s",
          g_strerror (errno));
      getrusage (RUSAGE_SELF, &ru);
      tproc =
          GST_TIMEVAL_TO_TIME (ru.ru_utime) + GST_TIMEVAL_TO_TIME (ru.ru_stime);
    }

    /* cpu time per thread */
    if (!clock_gettime (CLOCK_THREAD_CPUTIME_ID, &now)) {
      tthread = GST_TIMESPEC_TO_TIME (now);
    } else {
      GST_WARNING_OBJECT (obj,
          "clock_gettime (CLOCK_THREAD_CPUTIME_ID,...) failed: %s",
          g_strerror (errno));
#ifdef RUSAGE_THREAD
      getrusage (RUSAGE_THREAD, &ru);
      tthread =
          GST_TIMEVAL_TO_TIME (ru.ru_utime) + GST_TIMEVAL_TO_TIME (ru.ru_stime);
#endif
    }
  }
#else
  getrusage (RUSAGE_SELF, &ru);
  tproc = GST_TIMEVAL_TO_TIME (ru.ru_utime) + GST_TIMEVAL_TO_TIME (ru.ru_stime);
#ifdef RUSAGE_THREAD
  getrusage (RUSAGE_THREAD, &ru);
  tthread =
      GST_TIMEVAL_TO_TIME (ru.ru_utime) + GST_TIMEVAL_TO_TIME (ru.ru_stime);
#endif
#endif
  /* get stats record for current thread */
  if (!(stats = g_hash_table_lookup (self->threads, thread_id))) {
    stats = g_slice_new0 (GstThreadStats);
    stats->tvs_thread = make_trace_values (GST_SECOND);
    g_hash_table_insert (self->threads, thread_id, stats);
  }
  stats->tthread = tthread;

  /* Calibrate ts for the process and main thread. For tthread[main] and tproc
   * the time is larger than ts, as our base-ts is taken after the process has
   * started.
   */
  if (G_UNLIKELY (thread_id == self->main_thread_id)) {
    self->main_thread_id = NULL;
    /* when the registry gets updated, the tproc is less than the debug time ? */
    /* TODO(ensonic): we still see cases where tproc overtakes ts, especially
     * when with sync=false, can this be due to multiple cores in use? */
    if (tproc > ts) {
      self->tproc_base = tproc - ts;
      GST_DEBUG ("rusage: calibrating by %" G_GUINT64_FORMAT ", thread: %"
          G_GUINT64_FORMAT ", proc: %" G_GUINT64_FORMAT,
          self->tproc_base, stats->tthread, tproc);
      stats->tthread -= self->tproc_base;
    }
  }
  /* we always need to correct proc time */
  tproc -= self->tproc_base;

  /* FIXME: how can we take cpu-frequency scaling into account?
   * - looking at /sys/devices/system/cpu/cpu0/cpufreq/
   *   scale_factor=scaling_max_freq/scaling_cur_freq
   * - as a workaround we can switch it via /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
   *   cpufreq-selector -g performance
   *   cpufreq-selector -g ondemand
   */
  /* *INDENT-OFF* */
  avg_cpuload = (guint) gst_util_uint64_scale (stats->tthread,
      G_GINT64_CONSTANT (1000), ts);
  update_trace_value (stats->tvs_thread, ts, stats->tthread, &dts, &dtproc);
  cur_cpuload = (guint) gst_util_uint64_scale (dtproc,
      G_GINT64_CONSTANT (1000), dts);
  gst_tracer_record_log (tr_thread, (guint64) (guintptr) thread_id, ts,
      MIN (avg_cpuload, 1000), MIN (cur_cpuload, 1000), stats->tthread);

  avg_cpuload = (guint) gst_util_uint64_scale (tproc / num_cpus,
      G_GINT64_CONSTANT (1000), ts);
  G_LOCK (_proc);
  update_trace_value (self->tvs_proc, ts, tproc, &dts, &dtproc);
  G_UNLOCK (_proc);
  cur_cpuload = (guint) gst_util_uint64_scale (dtproc / num_cpus,
      G_GINT64_CONSTANT (1000), dts);
  gst_tracer_record_log (tr_proc, (guint64) getpid (), ts,
      MIN (avg_cpuload, 1000), MIN (cur_cpuload, 1000), tproc);
  /* *INDENT-ON* */
}

/* tracer class */

static void
gst_rusage_tracer_finalize (GObject * obj)
{
  GstRUsageTracer *self = GST_RUSAGE_TRACER (obj);

  g_hash_table_destroy (self->threads);
  free_trace_values (self->tvs_proc);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_rusage_tracer_class_init (GstRUsageTracerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_rusage_tracer_finalize;

  if ((num_cpus = sysconf (_SC_NPROCESSORS_ONLN)) == -1) {
    GST_WARNING ("failed to get number of cpus online");
    if ((num_cpus = sysconf (_SC_NPROCESSORS_CONF)) == -1) {
      GST_WARNING ("failed to get number of cpus, assuming 1");
      num_cpus = 1;
    }
  }
  GST_DEBUG ("rusage: num_cpus=%ld", num_cpus);

  /* announce trace formats */
  /* *INDENT-OFF* */
  tr_thread = gst_tracer_record_new ("thread-rusage.class",
      "thread-id", GST_TYPE_STRUCTURE, gst_structure_new ("scope",
          "type", G_TYPE_GTYPE, G_TYPE_UINT64,
          "related-to", GST_TYPE_TRACER_VALUE_SCOPE, GST_TRACER_VALUE_SCOPE_THREAD,
          NULL),
      "ts", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_UINT64,
          "description", G_TYPE_STRING, "event ts",
          NULL),
      "average-cpuload", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_UINT,
          "description", G_TYPE_STRING, "average cpu usage per thread in ‰",
          "flags", GST_TYPE_TRACER_VALUE_FLAGS, GST_TRACER_VALUE_FLAGS_AGGREGATED,
          "min", G_TYPE_UINT, 0,
          "max", G_TYPE_UINT, 1000,
          NULL),
      "current-cpuload", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_UINT,
          "description", G_TYPE_STRING, "current cpu usage per thread in ‰",
          "min", G_TYPE_UINT, 0,
          "max", G_TYPE_UINT, 1000,
          NULL),
      "time", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_UINT64,
          "description", G_TYPE_STRING, "time spent in thread in ns",
          "flags", GST_TYPE_TRACER_VALUE_FLAGS, GST_TRACER_VALUE_FLAGS_AGGREGATED,
          "min", G_TYPE_UINT64, G_GUINT64_CONSTANT (0),
          "max", G_TYPE_UINT64, G_MAXUINT64,
          NULL),
      NULL);
  tr_proc = gst_tracer_record_new ("proc-rusage.class",
      "process-id", GST_TYPE_STRUCTURE, gst_structure_new ("scope",
          "type", G_TYPE_GTYPE, G_TYPE_UINT64,
          "related-to", GST_TYPE_TRACER_VALUE_SCOPE, GST_TRACER_VALUE_SCOPE_PROCESS,
          NULL),
      "ts", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_UINT64,
          "description", G_TYPE_STRING, "event ts",
          NULL),
      "average-cpuload", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_UINT,
          "description", G_TYPE_STRING, "average cpu usage per process in ‰",
          "flags", GST_TYPE_TRACER_VALUE_FLAGS, GST_TRACER_VALUE_FLAGS_AGGREGATED,
          "min", G_TYPE_UINT, 0,
          "max", G_TYPE_UINT, 1000,
          NULL),
      "current-cpuload", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_UINT,
          "description", G_TYPE_STRING, "current cpu usage per process in ‰",
          "min", G_TYPE_UINT, 0,
          "max", G_TYPE_UINT, 1000,
          NULL),
      "time", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_UINT64,
          "description", G_TYPE_STRING, "time spent in process in ns",
          "flags", GST_TYPE_TRACER_VALUE_FLAGS, GST_TRACER_VALUE_FLAGS_AGGREGATED,
          "min", G_TYPE_UINT64, G_GUINT64_CONSTANT (0),
          "max", G_TYPE_UINT64, G_MAXUINT64,
          NULL),
      NULL);
  /* *INDENT-ON* */
}

static void
gst_rusage_tracer_init (GstRUsageTracer * self)
{
  GstTracer *tracer = GST_TRACER (self);
  gint i;
  const gchar *hooks[] = { "pad-push-pre", "pad-push-post", "pad-push-list-pre",
    "pad-push-list-post", "pad-pull-range-pre", "pad-pull-range-post",
    "pad-push-event-pre", "pad-push-event-post", "pad-query-pre",
    "pad-query-post", "element-post-message-pre", "element-post-message-post",
    "element-query-pre", "element-query-post", "element-new", "element-add-pad",
    "element-remove-pad", "element-change-state-pre",
    "element-change-state-post", "bin-add-pre", "bin-add-post",
    "bin-remove-pre", "bin-remove-post", "pad-link-pre", "pad-link-post",
    "pad-unlink-pre", "pad-unlink-post"
  };

  for (i = 0; i < G_N_ELEMENTS (hooks); i++) {
    gst_tracing_register_hook (tracer, hooks[i], G_CALLBACK (do_stats));
  }

  self->threads = g_hash_table_new_full (NULL, NULL, NULL, free_thread_stats);
  self->tvs_proc = make_trace_values (GST_SECOND);
  self->main_thread_id = g_thread_self ();

  GST_DEBUG ("rusage: main thread=%p", self->main_thread_id);
}
