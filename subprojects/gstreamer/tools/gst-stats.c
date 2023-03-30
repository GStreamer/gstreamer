/* GStreamer
 * Copyright (C) 2013 Stefan Sauer <ensonic@users.sf.net>
 *
 * gst-stats.c: statistics tracing front end
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "tools.h"

/* log parser */
static GRegex *raw_log = NULL;
static GRegex *ansi_log = NULL;

/* global statistics */
static GHashTable *threads = NULL;
static GPtrArray *elements = NULL;
static GPtrArray *pads = NULL;
static GHashTable *latencies = NULL;
static GHashTable *element_latencies = NULL;
static GQueue *element_reported_latencies = NULL;
static guint64 num_buffers = 0, num_events = 0, num_messages = 0, num_queries =
    0;
static guint num_elements = 0, num_bins = 0, num_pads = 0, num_ghostpads = 0;
static GstClockTime last_ts = G_GUINT64_CONSTANT (0);
static guint total_cpuload = 0;
static gboolean have_cpuload = FALSE;

static GPtrArray *plugin_stats = NULL;

static gboolean have_latency = FALSE;
static gboolean have_element_latency = FALSE;
static gboolean have_element_reported_latency = FALSE;

typedef struct
{
  /* display name of the element */
  gchar *name;
  /* the number of latencies counted  */
  guint64 count;
  /* the total of all latencies */
  guint64 total;
  /* the min of all latencies */
  guint64 min;
  /* the max of all latencies */
  guint64 max;
  GstClockTime first_latency_ts;
} GstLatencyStats;

typedef struct
{
  /* The element name */
  gchar *element;
  /* The timestamp of the reported latency */
  guint64 ts;
  /* the min reported latency */
  guint64 min;
  /* the max reported latency */
  guint64 max;
} GstReportedLatency;

typedef struct
{
  /* human readable pad name and details */
  gchar *name, *type_name;
  guint index;
  gboolean is_ghost_pad;
  GstPadDirection dir;
  /* buffer statistics */
  guint num_buffers;
  guint num_live, num_decode_only, num_discont, num_resync, num_corrupted,
      num_marker, num_header, num_gap, num_droppable, num_delta;
  guint min_size, max_size, avg_size;
  /* first and last activity on the pad, expected next_ts */
  GstClockTime first_ts, last_ts, next_ts;
  /* in which thread does it operate */
  gpointer thread_id;
  /* hierarchy */
  guint parent_ix;
} GstPadStats;

typedef struct
{
  /* human readable element name */
  gchar *name, *type_name;
  guint index;
  gboolean is_bin;
  /* buffer statistics */
  guint recv_buffers, sent_buffers;
  guint64 recv_bytes, sent_bytes;
  /* event, message statistics */
  guint num_events, num_messages, num_queries;
  /* first activity on the element */
  GstClockTime first_ts, last_ts;
  /* hierarchy */
  guint parent_ix;
} GstElementStats;

typedef struct
{
  /* time spend in this thread */
  GstClockTime tthread;
  guint cpuload;
} GstThreadStats;

static const gchar *FACTORY_TYPES[] = {
  "element",
  "device-provider",
  "typefind",
  "dynamic-type",
};

#define N_FACTORY_TYPES G_N_ELEMENTS(FACTORY_TYPES)

typedef struct
{
  gchar *name;

  GPtrArray *factories[N_FACTORY_TYPES];
} GstPluginStats;

/* stats helper */

static gint
sort_latency_stats_by_first_ts (gconstpointer a, gconstpointer b)
{
  const GstLatencyStats *ls1 = a, *ls2 = b;

  return (GST_CLOCK_DIFF (ls2->first_latency_ts, ls1->first_latency_ts));
}

static void
print_latency_stats (gpointer value, gpointer user_data)
{
  GstLatencyStats *ls = value;

  printf ("\t%s: mean=%" GST_TIME_FORMAT " min=%" GST_TIME_FORMAT " max=%"
      GST_TIME_FORMAT "\n", ls->name, GST_TIME_ARGS (ls->total / ls->count),
      GST_TIME_ARGS (ls->min), GST_TIME_ARGS (ls->max));
}

static void
reported_latencies_foreach_print_stats (GstReportedLatency * rl, gpointer data)
{
  printf ("\t%s: min=%" GST_TIME_FORMAT " max=%" GST_TIME_FORMAT " ts=%"
      GST_TIME_FORMAT "\n", rl->element, GST_TIME_ARGS (rl->min),
      GST_TIME_ARGS (rl->max), GST_TIME_ARGS (rl->ts));
}

static void
free_latency_stats (gpointer data)
{
  GstLatencyStats *ls = data;

  g_free (ls->name);
  g_free (data);
}

static void
free_reported_latency (gpointer data)
{
  GstReportedLatency *rl = data;

  if (rl->element)
    g_free (rl->element);

  g_free (data);
}

static void
free_element_stats (gpointer data)
{
  g_free (data);
}

static inline GstElementStats *
get_element_stats (guint ix)
{
  return (ix != G_MAXUINT && ix < elements->len) ?
      g_ptr_array_index (elements, ix) : NULL;
}

static inline GstPadStats *
get_pad_stats (guint ix)
{
  return (ix != G_MAXUINT && ix < pads->len) ?
      g_ptr_array_index (pads, ix) : NULL;
}

static void
free_pad_stats (gpointer data)
{
  g_free (data);
}

static inline GstThreadStats *
get_thread_stats (gpointer id)
{
  GstThreadStats *stats = g_hash_table_lookup (threads, id);

  if (G_UNLIKELY (!stats)) {
    stats = g_new0 (GstThreadStats, 1);
    stats->tthread = GST_CLOCK_TIME_NONE;
    g_hash_table_insert (threads, id, stats);
  }
  return stats;
}

static void
new_pad_stats (GstStructure * s)
{
  GstPadStats *stats;
  guint ix, parent_ix;
  gchar *type, *name;
  gboolean is_ghost_pad;
  GstPadDirection dir;
  guint64 thread_id;

  gst_structure_get (s,
      "ix", G_TYPE_UINT, &ix,
      "parent-ix", G_TYPE_UINT, &parent_ix,
      "name", G_TYPE_STRING, &name,
      "type", G_TYPE_STRING, &type,
      "is-ghostpad", G_TYPE_BOOLEAN, &is_ghost_pad,
      "pad-direction", GST_TYPE_PAD_DIRECTION, &dir,
      "thread-id", G_TYPE_UINT64, &thread_id, NULL);

  stats = g_new0 (GstPadStats, 1);
  if (is_ghost_pad)
    num_ghostpads++;
  num_pads++;
  stats->name = name;
  stats->type_name = type;
  stats->index = ix;
  stats->is_ghost_pad = is_ghost_pad;
  stats->dir = dir;
  stats->min_size = G_MAXUINT;
  stats->first_ts = stats->last_ts = stats->next_ts = GST_CLOCK_TIME_NONE;
  stats->thread_id = (gpointer) (guintptr) thread_id;
  stats->parent_ix = parent_ix;

  if (pads->len <= ix)
    g_ptr_array_set_size (pads, ix + 1);
  g_ptr_array_index (pads, ix) = stats;
}

static void
new_element_stats (GstStructure * s)
{
  GstElementStats *stats;
  guint ix, parent_ix;
  gchar *type, *name;
  gboolean is_bin;

  gst_structure_get (s,
      "ix", G_TYPE_UINT, &ix,
      "parent-ix", G_TYPE_UINT, &parent_ix,
      "name", G_TYPE_STRING, &name,
      "type", G_TYPE_STRING, &type, "is-bin", G_TYPE_BOOLEAN, &is_bin, NULL);

  stats = g_new0 (GstElementStats, 1);
  if (is_bin)
    num_bins++;
  num_elements++;
  stats->index = ix;
  stats->name = name;
  stats->type_name = type;
  stats->is_bin = is_bin;
  stats->first_ts = GST_CLOCK_TIME_NONE;
  stats->parent_ix = parent_ix;

  if (elements->len <= ix)
    g_ptr_array_set_size (elements, ix + 1);
  g_ptr_array_index (elements, ix) = stats;
}

static void
free_thread_stats (gpointer data)
{
  g_free (data);
}

static GstPluginStats *
new_plugin_stats (const gchar * plugin_name)
{
  GstPluginStats *plugin = g_new0 (GstPluginStats, 1);
  guint i;

  plugin->name = g_strdup (plugin_name);

  for (i = 0; i < N_FACTORY_TYPES; i++)
    plugin->factories[i] = g_ptr_array_new_with_free_func (g_free);

  g_ptr_array_add (plugin_stats, plugin);

  return plugin;
}

static void
free_plugin_stats (gpointer data)
{
  GstPluginStats *plugin = data;
  guint i;

  g_free (plugin->name);

  for (i = 0; i < N_FACTORY_TYPES; i++)
    g_ptr_array_unref (plugin->factories[i]);

  g_free (data);
}

static void
do_pad_stats (GstPadStats * stats, guint elem_ix, guint size, guint64 ts,
    guint64 buffer_ts, guint64 buffer_dur, GstBufferFlags buffer_flags)
{
  gulong avg_size;

  /* parentage */
  if (stats->parent_ix == G_MAXUINT) {
    stats->parent_ix = elem_ix;
  }

  if (stats->thread_id) {
    get_thread_stats (stats->thread_id);
  }

  /* size stats */
  avg_size = (((gulong) stats->avg_size * (gulong) stats->num_buffers) + size);
  stats->num_buffers++;
  stats->avg_size = (guint) (avg_size / stats->num_buffers);
  if (size < stats->min_size)
    stats->min_size = size;
  else if (size > stats->max_size)
    stats->max_size = size;
  /* time stats */
  if (!GST_CLOCK_TIME_IS_VALID (stats->last_ts))
    stats->first_ts = ts;
  stats->last_ts = ts;
  /* flag stats */
  if (buffer_flags & GST_BUFFER_FLAG_LIVE)
    stats->num_live++;
  if (buffer_flags & GST_BUFFER_FLAG_DECODE_ONLY)
    stats->num_decode_only++;
  if (buffer_flags & GST_BUFFER_FLAG_DISCONT)
    stats->num_discont++;
  if (buffer_flags & GST_BUFFER_FLAG_RESYNC)
    stats->num_resync++;
  if (buffer_flags & GST_BUFFER_FLAG_CORRUPTED)
    stats->num_corrupted++;
  if (buffer_flags & GST_BUFFER_FLAG_MARKER)
    stats->num_marker++;
  if (buffer_flags & GST_BUFFER_FLAG_HEADER)
    stats->num_header++;
  if (buffer_flags & GST_BUFFER_FLAG_GAP)
    stats->num_gap++;
  if (buffer_flags & GST_BUFFER_FLAG_DROPPABLE)
    stats->num_droppable++;
  if (buffer_flags & GST_BUFFER_FLAG_DELTA_UNIT)
    stats->num_delta++;
  /* update timestamps */
  if (GST_CLOCK_TIME_IS_VALID (buffer_ts) &&
      GST_CLOCK_TIME_IS_VALID (buffer_dur)) {
    stats->next_ts = buffer_ts + buffer_dur;
  } else {
    stats->next_ts = GST_CLOCK_TIME_NONE;
  }
}

static void
do_element_stats (GstElementStats * stats, GstElementStats * peer_stats,
    guint size, guint64 ts)
{
  stats->sent_buffers++;
  peer_stats->recv_buffers++;
  stats->sent_bytes += size;
  peer_stats->recv_bytes += size;
  /* time stats */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (stats->first_ts))) {
    stats->first_ts = ts;
  }
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (peer_stats->first_ts))) {
    peer_stats->first_ts = ts + 1;
  }
}

static void
do_buffer_stats (GstStructure * s)
{
  guint64 ts;
  guint64 buffer_pts = GST_CLOCK_TIME_NONE, buffer_dur = GST_CLOCK_TIME_NONE;
  guint pad_ix, elem_ix, peer_elem_ix;
  guint size;
  GstBufferFlags buffer_flags;
  GstPadStats *pad_stats;
  GstElementStats *elem_stats, *peer_elem_stats;

  num_buffers++;
  gst_structure_get (s, "ts", G_TYPE_UINT64, &ts,
      "pad-ix", G_TYPE_UINT, &pad_ix,
      "element-ix", G_TYPE_UINT, &elem_ix,
      "peer-element-ix", G_TYPE_UINT, &peer_elem_ix,
      "buffer-size", G_TYPE_UINT, &size,
      "buffer-flags", GST_TYPE_BUFFER_FLAGS, &buffer_flags, NULL);
  gst_structure_get_uint64 (s, "buffer-pts", &buffer_pts);
  gst_structure_get_uint64 (s, "buffer-duration", &buffer_dur);
  last_ts = MAX (last_ts, ts);
  if (!(pad_stats = get_pad_stats (pad_ix))) {
    GST_WARNING ("no pad stats found for ix=%u", pad_ix);
    return;
  }
  if (!(elem_stats = get_element_stats (elem_ix))) {
    GST_WARNING ("no element stats found for ix=%u", elem_ix);
    return;
  }
  if (!(peer_elem_stats = get_element_stats (peer_elem_ix))) {
    GST_WARNING ("no element stats found for ix=%u", peer_elem_ix);
    return;
  }
  do_pad_stats (pad_stats, elem_ix, size, ts, buffer_pts, buffer_dur,
      buffer_flags);
  if (pad_stats->dir == GST_PAD_SRC) {
    /* push */
    do_element_stats (elem_stats, peer_elem_stats, size, ts);
  } else {
    /* pull */
    do_element_stats (peer_elem_stats, elem_stats, size, ts);
  }
}

static void
do_event_stats (GstStructure * s)
{
  guint64 ts;
  guint pad_ix, elem_ix;
  GstPadStats *pad_stats;
  GstElementStats *elem_stats;

  num_events++;
  gst_structure_get (s, "ts", G_TYPE_UINT64, &ts,
      "pad-ix", G_TYPE_UINT, &pad_ix, "element-ix", G_TYPE_UINT, &elem_ix,
      NULL);
  last_ts = MAX (last_ts, ts);
  if (!(pad_stats = get_pad_stats (pad_ix))) {
    GST_WARNING ("no pad stats found for ix=%u", pad_ix);
    return;
  }
  if (!(elem_stats = get_element_stats (elem_ix))) {
    // e.g. reconfigure events are send over unparented pads
    GST_INFO ("no element stats found for ix=%u", elem_ix);
    return;
  }
  elem_stats->num_events++;
}

static void
do_message_stats (GstStructure * s)
{
  guint64 ts;
  guint elem_ix;
  GstElementStats *elem_stats;

  num_messages++;
  gst_structure_get (s, "ts", G_TYPE_UINT64, &ts,
      "element-ix", G_TYPE_UINT, &elem_ix, NULL);
  last_ts = MAX (last_ts, ts);
  if (!(elem_stats = get_element_stats (elem_ix))) {
    GST_WARNING ("no element stats found for ix=%u", elem_ix);
    return;
  }
  elem_stats->num_messages++;
}

static void
do_query_stats (GstStructure * s)
{
  guint64 ts;
  guint elem_ix;
  GstElementStats *elem_stats;

  num_queries++;
  gst_structure_get (s, "ts", G_TYPE_UINT64, &ts,
      "element-ix", G_TYPE_UINT, &elem_ix, NULL);
  last_ts = MAX (last_ts, ts);
  if (!(elem_stats = get_element_stats (elem_ix))) {
    GST_WARNING ("no element stats found for ix=%u", elem_ix);
    return;
  }
  elem_stats->num_queries++;
}

static void
do_thread_rusage_stats (GstStructure * s)
{
  guint64 ts, tthread, thread_id;
  guint cpuload;
  GstThreadStats *thread_stats;

  gst_structure_get (s, "ts", G_TYPE_UINT64, &ts,
      "thread-id", G_TYPE_UINT64, &thread_id,
      "average-cpuload", G_TYPE_UINT, &cpuload, "time", G_TYPE_UINT64, &tthread,
      NULL);
  thread_stats = get_thread_stats ((gpointer) (guintptr) thread_id);
  thread_stats->cpuload = cpuload;
  thread_stats->tthread = tthread;
  last_ts = MAX (last_ts, ts);
}

static void
do_proc_rusage_stats (GstStructure * s)
{
  guint64 ts;

  gst_structure_get (s, "ts", G_TYPE_UINT64, &ts,
      "average-cpuload", G_TYPE_UINT, &total_cpuload, NULL);
  last_ts = MAX (last_ts, ts);
  have_cpuload = TRUE;
}

static void
update_latency_table (GHashTable * table, const gchar * key, guint64 time,
    GstClockTime ts)
{
  /* Find the values in the hash table */
  GstLatencyStats *ls = g_hash_table_lookup (table, key);
  if (!ls) {
    /* Insert the new key if the value does not exist */
    ls = g_new0 (GstLatencyStats, 1);
    ls->name = g_strdup (key);
    ls->count = 1;
    ls->total = time;
    ls->min = time;
    ls->max = time;
    ls->first_latency_ts = ts;
    g_hash_table_insert (table, g_strdup (key), ls);
  } else {
    /* Otherwise update the existing value */
    ls->count++;
    ls->total += time;
    if (ls->min > time)
      ls->min = time;
    if (ls->max < time)
      ls->max = time;
  }
}

static void
do_latency_stats (GstStructure * s)
{
  gchar *key = NULL;
  const gchar *src = NULL, *sink = NULL, *src_element = NULL,
      *sink_element = NULL, *src_element_id = NULL, *sink_element_id = NULL;
  guint64 ts = 0, time = 0;

  /* Get the values from the structure */
  src = gst_structure_get_string (s, "src");
  sink = gst_structure_get_string (s, "sink");
  src_element = gst_structure_get_string (s, "src-element");
  sink_element = gst_structure_get_string (s, "sink-element");
  src_element_id = gst_structure_get_string (s, "src-element-id");
  sink_element_id = gst_structure_get_string (s, "sink-element-id");
  gst_structure_get (s, "time", G_TYPE_UINT64, &time, NULL);
  gst_structure_get (s, "ts", G_TYPE_UINT64, &ts, NULL);

  /* Update last_ts */
  last_ts = MAX (last_ts, ts);

  /* Get the key */
  key = g_strdup_printf ("%s.%s.%s|%s.%s.%s", src_element_id, src_element,
      src, sink_element_id, sink_element, sink);

  /* Update the latency in the table */
  update_latency_table (latencies, key, time, ts);

  /* Clean up */
  g_free (key);

  have_latency = TRUE;
}

static void
do_element_latency_stats (GstStructure * s)
{
  gchar *key = NULL;
  const gchar *src = NULL, *element = NULL, *element_id = NULL;
  guint64 ts = 0, time = 0;

  /* Get the values from the structure */
  src = gst_structure_get_string (s, "src");
  element = gst_structure_get_string (s, "element");
  element_id = gst_structure_get_string (s, "element-id");
  gst_structure_get (s, "time", G_TYPE_UINT64, &time, NULL);
  gst_structure_get (s, "ts", G_TYPE_UINT64, &ts, NULL);

  /* Update last_ts */
  last_ts = MAX (last_ts, ts);

  /* Get the key */
  key = g_strdup_printf ("%s.%s.%s", element_id, element, src);

  /* Update the latency in the table */
  update_latency_table (element_latencies, key, time, ts);

  /* Clean up */
  g_free (key);

  have_element_latency = TRUE;
}

static void
do_element_reported_latency (GstStructure * s)
{
  const gchar *element = NULL, *element_id = NULL;
  guint64 ts = 0, min = 0, max = 0;
  GstReportedLatency *rl = NULL;

  /* Get the values from the structure */
  element_id = gst_structure_get_string (s, "element-id");
  element = gst_structure_get_string (s, "element");
  gst_structure_get (s, "min", G_TYPE_UINT64, &min, NULL);
  gst_structure_get (s, "max", G_TYPE_UINT64, &max, NULL);
  gst_structure_get (s, "ts", G_TYPE_UINT64, &ts, NULL);

  /* Update last_ts */
  last_ts = MAX (last_ts, ts);

  /* Insert/Update the key in the table */
  rl = g_new0 (GstReportedLatency, 1);
  rl->element = g_strdup_printf ("%s.%s", element_id, element);
  rl->ts = ts;
  rl->min = min;
  rl->max = max;
  g_queue_push_tail (element_reported_latencies, rl);

  have_element_reported_latency = TRUE;
}

static void
do_factory_used (GstStructure * s)
{
  const gchar *factory = NULL;
  const gchar *factory_type = NULL;
  const gchar *plugin_name = NULL;
  GstPluginStats *plugin = NULL;
  guint i, f;

  factory = gst_structure_get_string (s, "factory");
  factory_type = gst_structure_get_string (s, "factory-type");
  plugin_name = gst_structure_get_string (s, "plugin");

  if (!g_strcmp0 (plugin_name, "staticelements"))
    return;

  if (plugin_name == NULL || plugin_name[0] == 0)
    plugin_name = "built-in";

  for (f = 0; f < N_FACTORY_TYPES; f++)
    if (!g_strcmp0 (factory_type, FACTORY_TYPES[f]))
      break;
  if (f == N_FACTORY_TYPES)
    return;

  for (i = 0; i < plugin_stats->len; i++) {
    GstPluginStats *tmp_plugin = g_ptr_array_index (plugin_stats, i);
    if (!strcmp (tmp_plugin->name, plugin_name)) {
      plugin = tmp_plugin;
      break;
    }
  }

  if (plugin == NULL)
    plugin = new_plugin_stats (plugin_name);

  if (factory && factory[0] &&
      !g_ptr_array_find_with_equal_func (plugin->factories[f], factory,
          g_str_equal, NULL))
    g_ptr_array_add (plugin->factories[f], g_strdup (factory));
}

/* reporting */

static gint
find_pad_stats_for_thread (gconstpointer value, gconstpointer user_data)
{
  const GstPadStats *stats = (const GstPadStats *) value;

  if ((stats->thread_id == user_data) && (stats->num_buffers)) {
    return 0;
  }
  return 1;
}

static void
print_pad_stats (gpointer value, gpointer user_data)
{
  GstPadStats *stats = (GstPadStats *) value;

  if (stats->thread_id == user_data) {
    /* there seem to be some temporary pads */
    if (stats->num_buffers) {
      GstClockTimeDiff running =
          GST_CLOCK_DIFF (stats->first_ts, stats->last_ts);
      GstElementStats *elem_stats = get_element_stats (stats->parent_ix);
      gchar fullname[30 + 1];

      g_snprintf (fullname, 30, "%s.%s", elem_stats->name, stats->name);

      printf
          ("    %c %-30.30s: buffers %7u (live %5u,dec %5u,dis %5u,res %5u,"
          "cor %5u,mar %5u,hdr %5u,gap %5u,drop %5u,dlt %5u),",
          (stats->dir == GST_PAD_SRC) ? '>' : '<', fullname, stats->num_buffers,
          stats->num_live, stats->num_decode_only, stats->num_discont,
          stats->num_resync, stats->num_corrupted, stats->num_marker,
          stats->num_header, stats->num_gap, stats->num_droppable,
          stats->num_delta);
      if (stats->min_size == stats->max_size) {
        printf (" size (min/avg/max) ......./%7u/.......,", stats->avg_size);
      } else {
        printf (" size (min/avg/max) %7u/%7u/%7u,",
            stats->min_size, stats->avg_size, stats->max_size);
      }
      printf (" time %" GST_TIME_FORMAT ","
          " bytes/sec %lf\n",
          GST_TIME_ARGS (running),
          ((gdouble) (stats->num_buffers * stats->avg_size) * GST_SECOND) /
          ((gdouble) running));
    }
  }
}

static void
print_thread_stats (gpointer key, gpointer value, gpointer user_data)
{
  GSList *list = user_data;
  GSList *node = g_slist_find_custom (list, key, find_pad_stats_for_thread);
  GstThreadStats *stats = (GstThreadStats *) value;

  /* skip stats if there are no pads for that thread (e.g. a pipeline) */
  if (!node)
    return;

  printf ("Thread %p Statistics:\n", key);
  if (GST_CLOCK_TIME_IS_VALID (stats->tthread)) {
    printf ("  Time: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS (stats->tthread));
    printf ("  Avg CPU load: %4.1f %%\n", (gfloat) stats->cpuload / 10.0);
  }

  puts ("  Pad Statistics:");
  g_slist_foreach (node, print_pad_stats, key);
}

static void
print_element_stats (gpointer value, gpointer user_data)
{
  GstElementStats *stats = (GstElementStats *) value;

  /* skip temporary elements */
  if (stats->first_ts != GST_CLOCK_TIME_NONE) {
    gchar fullname[45 + 1];

    g_snprintf (fullname, 45, "%s:%s", stats->type_name, stats->name);

    printf ("  %-45s:", fullname);
    if (stats->recv_buffers)
      g_print (" buffers in/out %7u", stats->recv_buffers);
    else
      g_print (" buffers in/out %7s", "-");
    if (stats->sent_buffers)
      g_print ("/%7u", stats->sent_buffers);
    else
      g_print ("/%7s", "-");
    if (stats->recv_bytes)
      g_print (" bytes in/out %12" G_GUINT64_FORMAT, stats->recv_bytes);
    else
      g_print (" bytes in/out %12s", "-");
    if (stats->sent_bytes)
      g_print ("/%12" G_GUINT64_FORMAT, stats->sent_bytes);
    else
      printf ("/%12s", "-");
    g_print (" first activity %" GST_TIME_FORMAT ", "
        " ev/msg/qry sent %5u/%5u/%5u\n", GST_TIME_ARGS (stats->first_ts),
        stats->num_events, stats->num_messages, stats->num_queries);
  }
}

static void
accum_element_stats (gpointer value, gpointer user_data)
{
  GstElementStats *stats = (GstElementStats *) value;

  if (stats->parent_ix != G_MAXUINT) {
    GstElementStats *parent_stats = get_element_stats (stats->parent_ix);

    parent_stats->num_events += stats->num_events;
    parent_stats->num_messages += stats->num_messages;
    parent_stats->num_queries += stats->num_queries;
    if (!GST_CLOCK_TIME_IS_VALID (parent_stats->first_ts)) {
      parent_stats->first_ts = stats->first_ts;
    } else if (GST_CLOCK_TIME_IS_VALID (stats->first_ts)) {
      parent_stats->first_ts = MIN (parent_stats->first_ts, stats->first_ts);
    }
    if (!GST_CLOCK_TIME_IS_VALID (parent_stats->last_ts)) {
      parent_stats->last_ts = stats->last_ts;
    } else if (GST_CLOCK_TIME_IS_VALID (stats->last_ts)) {
      parent_stats->last_ts = MAX (parent_stats->last_ts, stats->last_ts);
    }
  }
}

/* sorting */

static gint
sort_pad_stats_by_first_activity (gconstpointer ps1, gconstpointer ps2)
{
  GstPadStats *s1 = (GstPadStats *) ps1;
  GstPadStats *s2 = (GstPadStats *) ps2;

  gint order = GST_CLOCK_DIFF (s2->first_ts, s1->first_ts);

  if (!order) {
    order = s1->dir - s2->dir;
  }
  return (order);
}

static void
sort_pad_stats (gpointer value, gpointer user_data)
{
  GSList **list = user_data;

  *list =
      g_slist_insert_sorted (*list, value, sort_pad_stats_by_first_activity);
}

static gint
sort_element_stats_by_first_activity (gconstpointer es1, gconstpointer es2)
{
  return (GST_CLOCK_DIFF (((GstElementStats *) es2)->first_ts,
          ((GstElementStats *) es1)->first_ts));
}

static void
sort_bin_stats (gpointer value, gpointer user_data)
{
  if (value != NULL && ((GstElementStats *) value)->is_bin) {
    GSList **list = user_data;

    *list =
        g_slist_insert_sorted (*list, value,
        sort_element_stats_by_first_activity);
  }
}

static void
sort_element_stats (gpointer value, gpointer user_data)
{
  if (value != NULL && !(((GstElementStats *) value)->is_bin)) {
    GSList **list = user_data;

    *list =
        g_slist_insert_sorted (*list, value,
        sort_element_stats_by_first_activity);
  }
}

static gboolean
check_bin_parent (gpointer key, gpointer value, gpointer user_data)
{
  GstElementStats *stats = (GstElementStats *) value;

  return (stats->parent_ix == GPOINTER_TO_UINT (user_data));
}

static gboolean
process_leaf_bins (gpointer key, gpointer value, gpointer user_data)
{
  GHashTable *accum_bins = user_data;

  /* if we find no bin that has this bin as a parent ... */
  if (!g_hash_table_find (accum_bins, check_bin_parent, key)) {
    /* accumulate stats to the parent and remove */
    accum_element_stats (value, NULL);
    return TRUE;
  }
  return FALSE;
}

/* main */

static gboolean
init (void)
{
  /* compile the parser regexps */
  /* 0:00:00.004925027 31586      0x1c5c600 DEBUG           GST_REGISTRY gstregistry.c:463:gst_registry_add_plugin:<registry0> adding plugin 0x1c79160 for filename "/usr/lib/gstreamer-1.0/libgstxxx.so"
   * 0:00:02.719599000 35292 000001C031A49C60 DEBUG             GST_TRACER gsttracer.c:162:gst_tracer_register:<registry0> update existing feature 000001C02F9843C0 (latency)
   */
  raw_log = g_regex_new (
      /* 1: ts */
      "^([0-9:.]+) +"
      /* 2: pid */
      "([0-9]+) +"
      /* 3: thread */
      "(0?x?[0-9a-fA-F]+) +"
      /* 4: level */
      "([A-Z]+) +"
      /* 5: category */
      "([a-zA-Z_-]+) +"
      /* 6: file:line:func: */
      "([^:]*:[0-9]+:[^:]*:) +"
      /* 7: (obj)? log-text */
      "(.*)$", 0, 0, NULL);
  if (!raw_log) {
    GST_WARNING ("failed to compile the 'raw' parser");
    return FALSE;
  }

  ansi_log = g_regex_new (
      /* 1: ts */
      "^([0-9:.]+) +"
      /* 2: pid */
      "\\\x1b\\[[0-9;]+m *([0-9]+)\\\x1b\\[00m +"
      /* 3: thread */
      "(0x[0-9a-fA-F]+) +"
      /* 4: level */
      "(?:\\\x1b\\[[0-9;]+m)?([A-Z]+) +\\\x1b\\[00m +"
      /* 5: category */
      "\\\x1b\\[[0-9;]+m +([a-zA-Z_-]+) +"
      /* 6: file:line:func: */
      "([^:]*:[0-9]+:[^:]*:)(?:\\\x1b\\[00m)? +"
      /* 7: (obj)? log-text */
      "(.*)$", 0, 0, NULL);
  if (!raw_log) {
    GST_WARNING ("failed to compile the 'ansi' parser");
    return FALSE;
  }

  elements = g_ptr_array_new_with_free_func (free_element_stats);
  pads = g_ptr_array_new_with_free_func (free_pad_stats);
  threads = g_hash_table_new_full (NULL, NULL, NULL, free_thread_stats);
  latencies = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      free_latency_stats);
  element_latencies = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      free_latency_stats);
  element_reported_latencies = g_queue_new ();

  plugin_stats = g_ptr_array_new_with_free_func (free_plugin_stats);

  return TRUE;
}

static void
done (void)
{
  if (pads)
    g_ptr_array_free (pads, TRUE);
  if (elements)
    g_ptr_array_free (elements, TRUE);
  if (threads)
    g_hash_table_destroy (threads);

  if (latencies) {
    g_hash_table_remove_all (latencies);
    g_hash_table_destroy (latencies);
    latencies = NULL;
  }
  if (element_latencies) {
    g_hash_table_remove_all (element_latencies);
    g_hash_table_destroy (element_latencies);
    element_latencies = NULL;
  }
  if (element_reported_latencies) {
    g_queue_free_full (element_reported_latencies, free_reported_latency);
    element_reported_latencies = NULL;
  }

  g_clear_pointer (&plugin_stats, g_ptr_array_unref);

  if (raw_log)
    g_regex_unref (raw_log);
  if (ansi_log)
    g_regex_unref (ansi_log);
}

static gint
compare_plugin_stats (gconstpointer a, gconstpointer b)
{
  const GstPluginStats *plugin_a = *(GstPluginStats **) a;
  const GstPluginStats *plugin_b = *(GstPluginStats **) b;

  return strcmp (plugin_a->name, plugin_b->name);
}

static gint
compare_string (gconstpointer a, gconstpointer b)
{
  const char *str_a = *(const char **) a;
  const char *str_b = *(const char **) b;

  return strcmp (str_a, str_b);
}

static void
print_stats (void)
{
  guint num_threads = g_hash_table_size (threads);

  /* print overall stats */
  g_print ("\nOverall Statistics:\n");
  g_print ("Number of Threads: %u\n", num_threads);
  g_print ("Number of Elements: %u\n", num_elements - num_bins);
  g_print ("Number of Bins: %u\n", num_bins);
  g_print ("Number of Pads: %u\n", num_pads - num_ghostpads);
  g_print ("Number of GhostPads: %u\n", num_ghostpads);
  g_print ("Number of Buffers passed: %" G_GUINT64_FORMAT "\n", num_buffers);
  g_print ("Number of Events sent: %" G_GUINT64_FORMAT "\n", num_events);
  g_print ("Number of Message sent: %" G_GUINT64_FORMAT "\n", num_messages);
  g_print ("Number of Queries sent: %" G_GUINT64_FORMAT "\n", num_queries);
  g_print ("Time: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS (last_ts));
  if (have_cpuload) {
    g_print ("Avg CPU load: %4.1f %%\n", (gfloat) total_cpuload / 10.0);
  }
  g_print ("\n");

  /* thread stats */
  if (num_threads) {
    GSList *list = NULL;

    g_ptr_array_foreach (pads, sort_pad_stats, &list);
    g_hash_table_foreach (threads, print_thread_stats, list);
    puts ("");
    g_slist_free (list);
  }

  /* element stats */
  if (num_elements) {
    GSList *list = NULL;

    puts ("Element Statistics:");
    /* sort by first_activity */
    g_ptr_array_foreach (elements, sort_element_stats, &list);
    /* attribute element stats to bins */
    g_slist_foreach (list, accum_element_stats, NULL);
    g_slist_foreach (list, print_element_stats, NULL);
    puts ("");
    g_slist_free (list);
  }

  /* bin stats */
  if (num_bins) {
    GSList *list = NULL;
    guint i;
    GHashTable *accum_bins = g_hash_table_new_full (NULL, NULL, NULL, NULL);

    puts ("Bin Statistics:");
    /* attribute bin stats to parent-bins */
    for (i = 0; i < num_elements; i++) {
      GstElementStats *stats = g_ptr_array_index (elements, i);
      if (stats != NULL && stats->is_bin) {
        g_hash_table_insert (accum_bins, GUINT_TO_POINTER (i), stats);
      }
    }
    while (g_hash_table_size (accum_bins)) {
      g_hash_table_foreach_remove (accum_bins, process_leaf_bins, accum_bins);
    }
    g_hash_table_destroy (accum_bins);
    /* sort by first_activity */
    g_ptr_array_foreach (elements, sort_bin_stats, &list);
    g_slist_foreach (list, print_element_stats, NULL);
    puts ("");
    g_slist_free (list);
  }

  /* latency stats */
  if (have_latency) {
    GList *list = NULL;

    puts ("Latency Statistics:");
    list = g_hash_table_get_values (latencies);
    /* Sort by first activity */
    list = g_list_sort (list, sort_latency_stats_by_first_ts);
    g_list_foreach (list, print_latency_stats, NULL);
    puts ("");
    g_list_free (list);
  }

  /* element latency stats */
  if (have_element_latency) {
    GList *list = NULL;

    puts ("Element Latency Statistics:");
    list = g_hash_table_get_values (element_latencies);
    /* Sort by first activity */
    list = g_list_sort (list, sort_latency_stats_by_first_ts);
    g_list_foreach (list, print_latency_stats, NULL);
    puts ("");
    g_list_free (list);
  }

  /* element reported latency stats */
  if (have_element_reported_latency) {
    puts ("Element Reported Latency:");
    g_queue_foreach (element_reported_latencies,
        (GFunc) reported_latencies_foreach_print_stats, NULL);
    puts ("");
  }

  if (plugin_stats->len > 0) {
    guint i, j, f;

    g_ptr_array_sort (plugin_stats, compare_plugin_stats);

    printf ("Plugins used: ");
    for (i = 0; i < plugin_stats->len; i++) {
      GstPluginStats *ps = g_ptr_array_index (plugin_stats, i);
      printf ("%s%s", i == 0 ? "" : ";", ps->name);
    }
    printf ("\n");

    for (f = 0; f < N_FACTORY_TYPES; f++) {
      gboolean first = TRUE;

      printf ("%c%ss: ", g_ascii_toupper (FACTORY_TYPES[f][0]),
          FACTORY_TYPES[f] + 1);
      for (i = 0; i < plugin_stats->len; i++) {
        GstPluginStats *ps = g_ptr_array_index (plugin_stats, i);

        if (ps->factories[f]->len > 0) {
          printf ("%s%s:", first ? "" : ";", ps->name);
          first = FALSE;

          g_ptr_array_sort (ps->factories[f], compare_string);

          for (j = 0; j < ps->factories[f]->len; j++) {
            const gchar *factory = g_ptr_array_index (ps->factories[f], j);

            printf ("%s%s", j == 0 ? "" : ",", factory);
          }
        }
      }
      printf ("\n");
    }
  }
}

static void
collect_stats (const gchar * filename)
{
  FILE *log;

  if ((log = fopen (filename, "rt"))) {
    gchar line[5001];

    /* probe format */
    if (fgets (line, 5000, log)) {
      GMatchInfo *match_info;
      GRegex *parser;
      GstStructure *s;
      guint lnr = 0;
      gchar *level, *data;

      if (strchr (line, 27)) {
        parser = ansi_log;
        GST_INFO ("format is 'ansi'");
      } else {
        parser = raw_log;
        GST_INFO ("format is 'raw'");
      }
      rewind (log);

      /* parse the log */
      while (!feof (log)) {
        if (fgets (line, 5000, log)) {
          if (g_regex_match (parser, line, 0, &match_info)) {
            /* filter by level */
            level = g_match_info_fetch (match_info, 4);
            if (!strcmp (level, "TRACE")) {
              data = g_match_info_fetch (match_info, 7);
              if ((s = gst_structure_from_string (data, NULL))) {
                const gchar *name = gst_structure_get_name (s);

                if (!strcmp (name, "new-pad")) {
                  new_pad_stats (s);
                } else if (!strcmp (name, "new-element")) {
                  new_element_stats (s);
                } else if (!strcmp (name, "buffer")) {
                  do_buffer_stats (s);
                } else if (!strcmp (name, "event")) {
                  do_event_stats (s);
                } else if (!strcmp (name, "message")) {
                  do_message_stats (s);
                } else if (!strcmp (name, "query")) {
                  do_query_stats (s);
                } else if (!strcmp (name, "thread-rusage")) {
                  do_thread_rusage_stats (s);
                } else if (!strcmp (name, "proc-rusage")) {
                  do_proc_rusage_stats (s);
                } else if (!strcmp (name, "latency")) {
                  do_latency_stats (s);
                } else if (!strcmp (name, "element-latency")) {
                  do_element_latency_stats (s);
                } else if (!strcmp (name, "element-reported-latency")) {
                  do_element_reported_latency (s);
                } else if (!strcmp (name, "factory-used")) {
                  do_factory_used (s);
                } else {
                  // TODO(ensonic): parse the xxx.class log lines
                  if (!g_str_has_suffix (data, ".class")) {
                    GST_WARNING ("unknown log entry: '%s'", data);
                  }
                }
                gst_structure_free (s);
              } else {
                GST_WARNING ("unknown log entry: '%s'", data);
              }
              g_free (data);
            }
            g_free (level);
          } else {
            if (*line) {
              GST_WARNING ("foreign log entry: %s:%d:'%s'", filename, lnr,
                  g_strchomp (line));
            }
          }
          g_match_info_free (match_info);
          match_info = NULL;
          lnr++;
        } else {
          if (!feof (log)) {
            // TODO(ensonic): run wc -L on the log file
            fprintf (stderr, "line too long");
          }
        }
      }
    } else {
      GST_WARNING ("empty log");
    }
    fclose (log);
  }
}

gint
main (gint argc, gchar * argv[])
{
  gchar **filenames = NULL;
  guint num;
  GError *err = NULL;
  GOptionContext *ctx;
  GOptionEntry options[] = {
    GST_TOOLS_GOPTION_VERSION,
    // TODO(ensonic): add a summary flag, if set read the whole thing, print
    // stats once, and exit
    {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &filenames, NULL}
    ,
    {NULL}
  };

#ifdef ENABLE_NLS
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);
#endif

  g_set_prgname ("gst-stats-" GST_API_VERSION);

#ifdef G_OS_WIN32
  argv = g_win32_get_command_line ();
#endif

  ctx = g_option_context_new ("FILE");
  g_option_context_add_main_entries (ctx, options, GETTEXT_PACKAGE);
  g_option_context_add_group (ctx, gst_init_get_option_group ());
#ifdef G_OS_WIN32
  if (!g_option_context_parse_strv (ctx, &argv, &err))
#else
  if (!g_option_context_parse (ctx, &argc, &argv, &err))
#endif
  {
    g_print ("Error initializing: %s\n", GST_STR_NULL (err->message));
    exit (1);
  }
  g_option_context_free (ctx);

#ifdef G_OS_WIN32
  argc = g_strv_length (argv);
#endif

  gst_tools_print_version ();

  if (filenames == NULL || *filenames == NULL) {
    g_print ("Please give one filename to %s\n\n", g_get_prgname ());
    return 1;
  }
  num = g_strv_length (filenames);
  if (num == 0 || num > 1) {
    g_print ("Please give exactly one filename to %s (%d given).\n\n",
        g_get_prgname (), num);
    return 1;
  }

  if (init ()) {
    collect_stats (filenames[0]);
    print_stats ();
  }
  done ();

  g_strfreev (filenames);

#ifdef G_OS_WIN23
  g_strfreev (argv);
#endif

  return 0;
}
