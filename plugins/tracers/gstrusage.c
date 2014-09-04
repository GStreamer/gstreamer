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
/* TODO: log more items, cpuload is calculated as an aggregated value
 * - in many cases a windowed value would be more interesting to see local
 *   cpu-load spikes
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstrusage.h"

#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif

GST_DEBUG_CATEGORY_STATIC (gst_rusage_debug);
#define GST_CAT_DEFAULT gst_rusage_debug

#define _do_init \
    GST_DEBUG_CATEGORY_INIT (gst_rusage_debug, "rusage", 0, "rusage tracer");
#define gst_rusage_tracer_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstRUsageTracer, gst_rusage_tracer, GST_TYPE_TRACER,
    _do_init);

/* for ts calibration */
static gpointer main_thread_id = NULL;
static guint64 tproc_base = G_GINT64_CONSTANT (0);

typedef struct
{
  /* time spend in this thread */
  GstClockTime tthread;
} GstThreadStats;

static void gst_rusage_tracer_invoke (GstTracer * self, GstTracerHookId id,
    GstTracerMessageId mid, va_list var_args);

/* data helper */

static void
free_thread_stats (gpointer data)
{
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
  GST_DEBUG ("rusage: main thread=%p", main_thread_id);

  /* announce trace formats */
  /* *INDENT-OFF* */
  gst_tracer_log_trace (gst_structure_new ("thread-rusage.class",
      "thread-id", GST_TYPE_STRUCTURE, gst_structure_new ("scope",
          "related-to", G_TYPE_STRING, "thread",  // use genum
          NULL),
      "cpuload", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_UINT,
          "description", G_TYPE_STRING, "cpu usage per thread",
          "flags", G_TYPE_STRING, "aggregated",  // use gflags 
          "min", G_TYPE_UINT, 0, "max", G_TYPE_UINT, 100,
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
      "cpuload", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_UINT,
          "description", G_TYPE_STRING, "cpu usage per process",
          "flags", G_TYPE_STRING, "aggregated",  // use gflags 
          "min", G_TYPE_UINT, 0, "max", G_TYPE_UINT, 100,
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
  guint cpuload = 0;
  struct rusage ru;
  GstClockTime tproc = G_GUINT64_CONSTANT (0);

  // FIXME(ensonic): not threadsafe
  static GstClockTime last_ts = G_GUINT64_CONSTANT (0);

  getrusage (RUSAGE_SELF, &ru);

  /* get stats record for current thread */
  if (!(stats = g_hash_table_lookup (self->threads, thread_id))) {
    stats = g_slice_new0 (GstThreadStats);
    g_hash_table_insert (self->threads, thread_id, stats);
  }
#ifdef HAVE_CLOCK_GETTIME
  {
    struct timespec now;

    if (!clock_gettime (CLOCK_PROCESS_CPUTIME_ID, &now)) {
      tproc = GST_TIMESPEC_TO_TIME (now);
    } else {
      GST_WARNING ("clock_gettime (CLOCK_PROCESS_CPUTIME_ID,...) failed.");
      tproc =
          GST_TIMEVAL_TO_TIME (ru.ru_utime) + GST_TIMEVAL_TO_TIME (ru.ru_stime);
    }

    /* cpu time per thread */
    if (!clock_gettime (CLOCK_THREAD_CPUTIME_ID, &now)) {
      stats->tthread = GST_TIMESPEC_TO_TIME (now);
    } else {
      GST_WARNING ("clock_gettime (CLOCK_THREAD_CPUTIME_ID,...) failed.");
      stats->tthread += GST_CLOCK_DIFF (last_ts, ts);
    }
  }
#else
  tproc = GST_TIMEVAL_TO_TIME (ru.ru_utime) + GST_TIMEVAL_TO_TIME (ru.ru_stime);
  /* crude way to meassure time spend in which thread */
  stats->tthread += GST_CLOCK_DIFF (last_ts, ts);
#endif

  /* remember last timestamp for fallback calculations */
  last_ts = ts;

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
  /* we always need to corect proc time */
  tproc -= tproc_base;

  /* FIXME: how can we take cpu-frequency scaling into account?
   * - looking at /sys/devices/system/cpu/cpu0/cpufreq/
   *   scale_factor=scaling_max_freq/scaling_cur_freq
   * - as a workaround we can switch it via /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
   *   cpufreq-selector -g performance
   *   cpufreq-selector -g ondemand
   */
  /* *INDENT-OFF* */
  cpuload = (guint) gst_util_uint64_scale (stats->tthread,
      G_GINT64_CONSTANT (100), ts);
  gst_tracer_log_trace (gst_structure_new ("thread-rusage", 
      "ts", G_TYPE_UINT64, ts, 
      "thread-id", G_TYPE_UINT, GPOINTER_TO_UINT (thread_id), 
      "cpuload", G_TYPE_UINT, cpuload,
      "time", G_TYPE_UINT64, stats->tthread,
      NULL));
  cpuload = (guint) gst_util_uint64_scale (tproc, G_GINT64_CONSTANT (100), ts);
  gst_tracer_log_trace (gst_structure_new ("proc-rusage", 
      "ts", G_TYPE_UINT64, ts, 
      "cpuload", G_TYPE_UINT, cpuload,
      "time", G_TYPE_UINT64, tproc,
      NULL));
  /* *INDENT-ON* */
}
