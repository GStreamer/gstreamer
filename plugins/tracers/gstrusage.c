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

typedef struct
{
  /* time spend in this thread */
  GstClockTime treal;
  guint max_cpuload;
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
}

static void
gst_rusage_tracer_invoke (GstTracer * obj, GstTracerHookId hid,
    GstTracerMessageId mid, va_list var_args)
{
  GstRUsageTracer *self = GST_RUSAGE_TRACER_CAST (obj);
  guint64 treal = va_arg (var_args, guint64);
  GstThreadStats *stats;
  gpointer thread_id = g_thread_self ();
  guint cpuload = 0;
  struct rusage ru;
  GstClockTime tusersys = G_GUINT64_CONSTANT (0);

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
      tusersys = GST_TIMESPEC_TO_TIME (now);
    } else {
      GST_WARNING ("clock_gettime (CLOCK_PROCESS_CPUTIME_ID,...) failed.");
      tusersys =
          GST_TIMEVAL_TO_TIME (ru.ru_utime) + GST_TIMEVAL_TO_TIME (ru.ru_stime);
    }

    /* cpu time per thread */
    if (!clock_gettime (CLOCK_THREAD_CPUTIME_ID, &now)) {
      stats->treal = GST_TIMESPEC_TO_TIME (now);
    } else {
      GST_WARNING ("clock_gettime (CLOCK_THREAD_CPUTIME_ID,...) failed.");
      stats->treal += GST_CLOCK_DIFF (last_ts, treal);
    }
  }
#else
  tusersys =
      GST_TIMEVAL_TO_TIME (ru.ru_utime) + GST_TIMEVAL_TO_TIME (ru.ru_stime);
  /* crude way to meassure time spend in which thread */
  stats->treal += GST_CLOCK_DIFF (last_ts, treal);
#endif

  /* remember last timestamp for percentage calculations */
  last_ts = treal;

  /* FIXME: how can we take cpu-frequency scaling into account?
   * - looking at /sys/devices/system/cpu/cpu0/cpufreq/
   *   scale_factor=scaling_max_freq/scaling_cur_freq
   * - as a workaround we can switch it via /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
   *   cpufreq-selector -g performance
   *   cpufreq-selector -g ondemand
   */
  cpuload =
      (guint) gst_util_uint64_scale (tusersys, G_GINT64_CONSTANT (100), treal);
  gst_tracer_log_trace (gst_structure_new ("rusage", "ts", G_TYPE_UINT64, treal, "thread-id", G_TYPE_UINT, GPOINTER_TO_UINT (thread_id), "cpuload", G_TYPE_UINT, cpuload, "treal", G_TYPE_UINT64, stats->treal, /* time in thread */
          "tsum", G_TYPE_UINT64, tusersys,      /* time in process */
          NULL));
}
