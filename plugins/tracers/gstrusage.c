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

#define _do_init \
    GST_DEBUG_CATEGORY_INIT (gst_rusage_debug, "rusage", 0, "rusage tracer");
#define gst_rusage_tracer_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstRUsageTracer, gst_rusage_tracer, GST_TYPE_TRACER,
    _do_init);

/* we remember x measurements per self->window */
#define WINDOW_SUBDIV 100

/* for ts calibration */
static gpointer main_thread_id = NULL;
static guint64 tproc_base = G_GINT64_CONSTANT (0);
static glong num_cpus = 1;

typedef struct
{
  GstClockTime ts;
  GstClockTime val;
} GstTraceValue;

typedef struct
{
  GstClockTime window;
  GMutex lock;
  GQueue values;                /* GstTraceValue* */
} GstTraceValues;

typedef struct
{
  /* time spend in this thread */
  GstClockTime tthread;
  GstTraceValues *tvs_thread;
} GstThreadStats;

static GstTraceValues *tvs_proc;

static void gst_rusage_tracer_invoke (GstTracer * self, GstTracerHookId id,
    GstTracerMessageId mid, va_list var_args);

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
  g_mutex_init (&self->lock);
  g_queue_init (&self->values);
  return self;
}

static void
free_trace_values (GstTraceValues * self)
{
  g_queue_free_full (&self->values, free_trace_value);
  g_mutex_clear (&self->lock);
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
gst_rusage_tracer_class_init (GstRUsageTracerClass * klass)
{
  GstTracerClass *gst_tracer_class = GST_TRACER_CLASS (klass);

  gst_tracer_class->invoke = gst_rusage_tracer_invoke;
}

static void
gst_rusage_tracer_init (GstRUsageTracer * self)
{
  g_object_set (self, "mask", GST_TRACER_HOOK_ALL, NULL);
  self->threads = g_hash_table_new_full (NULL, NULL, NULL, free_thread_stats);

  main_thread_id = g_thread_self ();
  tvs_proc = make_trace_values (GST_SECOND);
  if ((num_cpus = sysconf (_SC_NPROCESSORS_ONLN)) == -1) {
    GST_WARNING_OBJECT (self, "failed to get number of cpus online");
    if ((num_cpus = sysconf (_SC_NPROCESSORS_CONF)) == -1) {
      GST_WARNING_OBJECT (self, "failed to get number of cpus, assuming 1");
      num_cpus = 1;
    }
  }

  GST_DEBUG ("rusage: main thread=%p, num_cpus=%ld", main_thread_id, num_cpus);

  /* announce trace formats */
  /* *INDENT-OFF* */
  gst_tracer_log_trace (gst_structure_new ("thread-rusage.class",
      "thread-id", GST_TYPE_STRUCTURE, gst_structure_new ("scope",
          "related-to", G_TYPE_STRING, "thread",  // use genum
          NULL),
      "average-cpuload", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_UINT,
          "description", G_TYPE_STRING, "average cpu usage per thread in ‰",
          "flags", G_TYPE_STRING, "aggregated",  // use gflags 
          "min", G_TYPE_UINT, 0, 
          "max", G_TYPE_UINT, 1000,
          NULL),
      "current-cpuload", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_UINT,
          "description", G_TYPE_STRING, "current cpu usage per thread in ‰",
          "flags", G_TYPE_STRING, "windowed",  // use gflags 
          "min", G_TYPE_UINT, 0, 
          "max", G_TYPE_UINT, 1000,
          NULL),
      "time", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_UINT64,
          "description", G_TYPE_STRING, "time spent in thread",
          "flags", G_TYPE_STRING, "aggregated",  // use gflags 
          "min", G_TYPE_UINT64, G_GUINT64_CONSTANT (0),
          "max", G_TYPE_UINT64, G_MAXUINT64,
          NULL),
      NULL));
  gst_tracer_log_trace (gst_structure_new ("proc-rusage.class",
      "thread-id", GST_TYPE_STRUCTURE, gst_structure_new ("scope",
          "related-to", G_TYPE_STRING, "process",  // use genum
          NULL),
      "average-cpuload", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_UINT,
          "description", G_TYPE_STRING, "average cpu usage per process in ‰",
          "flags", G_TYPE_STRING, "aggregated",  // use gflags 
          "min", G_TYPE_UINT, 0, 
          "max", G_TYPE_UINT, 1000,
          NULL),
      "current-cpuload", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_UINT,
          "description", G_TYPE_STRING, "current cpu usage per process in ‰",
          "flags", G_TYPE_STRING, "windowed",  // use gflags 
          "min", G_TYPE_UINT, 0, 
          "max", G_TYPE_UINT, 1000,
          NULL),
      "time", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_UINT64,
          "description", G_TYPE_STRING, "time spent in process",
          "flags", G_TYPE_STRING, "aggregated",  // use gflags 
          "min", G_TYPE_UINT64, G_GUINT64_CONSTANT (0),
          "max", G_TYPE_UINT64, G_MAXUINT64,
          NULL),
      NULL));
  /* *INDENT-ON* */
}

static void
gst_rusage_tracer_invoke (GstTracer * obj, GstTracerHookId hid,
    GstTracerMessageId mid, va_list var_args)
{
  GstRUsageTracer *self = GST_RUSAGE_TRACER_CAST (obj);
  guint64 ts = va_arg (var_args, guint64);
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
      getrusage (RUSAGE_THREAD, &ru);
      tthread =
          GST_TIMEVAL_TO_TIME (ru.ru_utime) + GST_TIMEVAL_TO_TIME (ru.ru_stime);
    }
  }
#else
  getrusage (RUSAGE_SELF, &ru);
  tproc = GST_TIMEVAL_TO_TIME (ru.ru_utime) + GST_TIMEVAL_TO_TIME (ru.ru_stime);
  getrusage (RUSAGE_THREAD, &ru);
  tthread =
      GST_TIMEVAL_TO_TIME (ru.ru_utime) + GST_TIMEVAL_TO_TIME (ru.ru_stime);
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
  if (G_UNLIKELY (thread_id == main_thread_id)) {
    main_thread_id = NULL;
    /* when the registry gets updated, the tproc is less than the debug time ? */
    /* TODO(ensonic): we still see cases where tproc overtakes ts, especially
     * when with sync=false, can this be due to multiple cores in use? */
    if (tproc > ts) {
      tproc_base = tproc - ts;
      GST_DEBUG ("rusage: calibrating by %" G_GUINT64_FORMAT ", thread: %"
          G_GUINT64_FORMAT ", proc: %" G_GUINT64_FORMAT,
          tproc_base, stats->tthread, tproc);
      stats->tthread -= tproc_base;
    }
  }
  /* we always need to correct proc time */
  tproc -= tproc_base;

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
  gst_tracer_log_trace (gst_structure_new ("thread-rusage", 
      "ts", G_TYPE_UINT64, ts, 
      "thread-id", G_TYPE_UINT, GPOINTER_TO_UINT (thread_id), 
      "average-cpuload", G_TYPE_UINT, MIN (avg_cpuload, 1000),
      "current-cpuload", G_TYPE_UINT, MIN (cur_cpuload, 1000),
      "time", G_TYPE_UINT64, stats->tthread,
      NULL));

  avg_cpuload = (guint) gst_util_uint64_scale (tproc / num_cpus,
      G_GINT64_CONSTANT (1000), ts);
  g_mutex_lock (&tvs_proc->lock);
  update_trace_value (tvs_proc, ts, tproc, &dts, &dtproc);
  g_mutex_unlock (&tvs_proc->lock);
  cur_cpuload = (guint) gst_util_uint64_scale (dtproc / num_cpus,
      G_GINT64_CONSTANT (1000), dts);
  gst_tracer_log_trace (gst_structure_new ("proc-rusage", 
      "ts", G_TYPE_UINT64, ts, 
      "average-cpuload", G_TYPE_UINT, MIN (avg_cpuload, 1000),
      "current-cpuload", G_TYPE_UINT, MIN (cur_cpuload, 1000),
      "time", G_TYPE_UINT64, tproc,
      NULL));
  /* *INDENT-ON* */
}
