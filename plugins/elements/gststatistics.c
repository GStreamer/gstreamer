/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gststatistics.c: 
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gststatistics.h"

GST_DEBUG_CATEGORY_STATIC (gst_statistics_debug);
#define GST_CAT_DEFAULT gst_statistics_debug

GstElementDetails gst_statistics_details = GST_ELEMENT_DETAILS ("Statistics",
    "Generic",
    "Statistics on buffers/bytes/events",
    "David I. Lehn <dlehn@users.sourceforge.net>");


/* Statistics signals and args */
enum
{
  SIGNAL_UPDATE,
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_BUFFERS,
  ARG_BYTES,
  ARG_EVENTS,
  ARG_BUFFER_UPDATE_FREQ,
  ARG_BYTES_UPDATE_FREQ,
  ARG_EVENT_UPDATE_FREQ,
  ARG_UPDATE_ON_EOS,
  ARG_UPDATE,
  ARG_SILENT
};


#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_statistics_debug, "statistics", 0, "statistics element");

GST_BOILERPLATE_FULL (GstStatistics, gst_statistics, GstElement,
    GST_TYPE_ELEMENT, _do_init);

static void gst_statistics_finalize (GObject * object);
static void gst_statistics_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_statistics_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_statistics_chain (GstPad * pad, GstData * _data);
static void gst_statistics_reset (GstStatistics * statistics);
static void gst_statistics_print (GstStatistics * statistics);

static guint gst_statistics_signals[LAST_SIGNAL] = { 0, };

static stats zero_stats = { 0, };


static void
gst_statistics_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (gstelement_class, &gst_statistics_details);
}

static void
gst_statistics_finalize (GObject * object)
{
  GstStatistics *statistics;

  statistics = GST_STATISTICS (object);

  if (statistics->timer)
    g_timer_destroy (statistics->timer);

  if (statistics->last_timer)
    g_timer_destroy (statistics->last_timer);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_statistics_class_init (GstStatisticsClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);


  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BUFFERS,
      g_param_spec_int64 ("buffers", "buffers", "total buffers count",
          0, G_MAXINT64, 0, G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BYTES,
      g_param_spec_int64 ("bytes", "bytes", "total bytes count",
          0, G_MAXINT64, 0, G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_EVENTS,
      g_param_spec_int64 ("events", "events", "total event count",
          0, G_MAXINT64, 0, G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      ARG_BUFFER_UPDATE_FREQ, g_param_spec_int64 ("buffer_update_freq",
          "buffer update freq", "buffer update frequency", 0, G_MAXINT64, 0,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      ARG_BYTES_UPDATE_FREQ, g_param_spec_int64 ("bytes_update_freq",
          "bytes update freq", "bytes update frequency", 0, G_MAXINT64, 0,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      ARG_EVENT_UPDATE_FREQ, g_param_spec_int64 ("event_update_freq",
          "event update freq", "event update frequency", 0, G_MAXINT64, 0,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_UPDATE_ON_EOS,
      g_param_spec_boolean ("update_on_eos", "update on EOS",
          "update on EOS event", TRUE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_UPDATE,
      g_param_spec_boolean ("update", "update", "update", TRUE,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SILENT,
      g_param_spec_boolean ("silent", "silent", "silent", TRUE,
          G_PARAM_READWRITE));

  gst_statistics_signals[SIGNAL_UPDATE] =
      g_signal_new ("update", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstStatisticsClass, update), NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_statistics_finalize);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_statistics_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_statistics_get_property);
}

static void
gst_statistics_init (GstStatistics * statistics)
{
  statistics->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (statistics), statistics->sinkpad);
  gst_pad_set_chain_function (statistics->sinkpad,
      GST_DEBUG_FUNCPTR (gst_statistics_chain));

  statistics->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (statistics), statistics->srcpad);

  statistics->timer = NULL;
  statistics->last_timer = NULL;
  gst_statistics_reset (statistics);
}

static void
gst_statistics_reset (GstStatistics * statistics)
{
  g_return_if_fail (statistics != NULL);
  g_return_if_fail (GST_IS_STATISTICS (statistics));

  statistics->stats.buffers = 0;
  statistics->stats.bytes = 0;
  statistics->stats.events = 0;

  statistics->last_stats.buffers = 0;
  statistics->last_stats.bytes = 0;
  statistics->last_stats.events = 0;

  statistics->update_count.buffers = 0;
  statistics->update_count.bytes = 0;
  statistics->update_count.events = 0;

  statistics->update_freq.buffers = 0;
  statistics->update_freq.bytes = 0;
  statistics->update_freq.events = 0;

  statistics->update_on_eos = TRUE;
  statistics->update = TRUE;
  statistics->silent = FALSE;

  if (!statistics->timer) {
    statistics->timer = g_timer_new ();
  }
  if (!statistics->last_timer) {
    statistics->last_timer = g_timer_new ();
  }
}

static void
print_stats (gboolean first, const gchar * name, const gchar * type,
    stats * base, stats * final, double time)
{
  const gchar *header0 = "statistics";
  const gchar *headerN = "          ";
  stats delta;

  delta.buffers = final->buffers - base->buffers;
  delta.bytes = final->bytes - base->bytes;
  delta.events = final->events - base->events;

  g_print ("%s: (%s) %s: s:%g buffers:%" G_GINT64_FORMAT
      " bytes:%" G_GINT64_FORMAT
      " events:%" G_GINT64_FORMAT "\n",
      first ? header0 : headerN,
      name, type, time, final->buffers, final->bytes, final->events);
  g_print ("%s: (%s) %s: buf/s:%g B/s:%g e/s:%g B/buf:%g\n",
      headerN,
      name, type,
      delta.buffers / time,
      delta.bytes / time,
      delta.events / time, ((double) delta.bytes / (double) delta.buffers));
}

static void
gst_statistics_print (GstStatistics * statistics)
{
  const gchar *name;
  double elapsed;
  double last_elapsed;

  g_return_if_fail (statistics != NULL);
  g_return_if_fail (GST_IS_STATISTICS (statistics));

  name = gst_object_get_name (GST_OBJECT (statistics));
  if (!name) {
    name = "";
  }

  elapsed = g_timer_elapsed (statistics->timer, NULL);
  last_elapsed = g_timer_elapsed (statistics->last_timer, NULL);

  print_stats (1, name, "total", &zero_stats, &statistics->stats, elapsed);
  print_stats (0, name, "last", &statistics->last_stats, &statistics->stats,
      last_elapsed);
  statistics->last_stats = statistics->stats;
  g_timer_reset (statistics->last_timer);
}

static void
gst_statistics_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstStatistics *statistics;
  gboolean update = FALSE;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  statistics = GST_STATISTICS (gst_pad_get_parent (pad));

  if (GST_IS_EVENT (buf)) {
    GstEvent *event = GST_EVENT (buf);

    statistics->stats.events += 1;
    if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
      gst_element_set_eos (GST_ELEMENT (statistics));
      if (statistics->update_on_eos) {
        update = TRUE;
      }
    }
    if (statistics->update_freq.events) {
      statistics->update_count.events += 1;
      if (statistics->update_count.events == statistics->update_freq.events) {
        statistics->update_count.events = 0;
        update = TRUE;
      }
    }
  } else {
    statistics->stats.buffers += 1;
    if (statistics->update_freq.buffers) {
      statistics->update_count.buffers += 1;
      if (statistics->update_count.buffers == statistics->update_freq.buffers) {
        statistics->update_count.buffers = 0;
        update = TRUE;
      }
    }

    statistics->stats.bytes += GST_BUFFER_SIZE (buf);
    if (statistics->update_freq.bytes) {
      statistics->update_count.bytes += GST_BUFFER_SIZE (buf);
      if (statistics->update_count.bytes >= statistics->update_freq.bytes) {
        statistics->update_count.bytes = 0;
        update = TRUE;
      }
    }
  }

  if (update) {
    if (statistics->update) {
      GST_DEBUG ("[%s]: pre update emit", GST_ELEMENT_NAME (statistics));
      g_signal_emit (G_OBJECT (statistics),
          gst_statistics_signals[SIGNAL_UPDATE], 0);
      GST_DEBUG ("[%s]: post update emit", GST_ELEMENT_NAME (statistics));
    }
    if (!statistics->silent) {
      gst_statistics_print (statistics);
    }
  }
  gst_pad_push (statistics->srcpad, GST_DATA (buf));
}

static void
gst_statistics_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstStatistics *statistics;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_STATISTICS (object));

  statistics = GST_STATISTICS (object);

  switch (prop_id) {
    case ARG_BUFFER_UPDATE_FREQ:
      statistics->update_freq.buffers = g_value_get_int64 (value);
      break;
    case ARG_BYTES_UPDATE_FREQ:
      statistics->update_freq.bytes = g_value_get_int64 (value);
      break;
    case ARG_EVENT_UPDATE_FREQ:
      statistics->update_freq.events = g_value_get_int64 (value);
      break;
    case ARG_UPDATE_ON_EOS:
      statistics->update_on_eos = g_value_get_boolean (value);
      break;
    case ARG_UPDATE:
      statistics->update = g_value_get_boolean (value);
      break;
    case ARG_SILENT:
      statistics->silent = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_statistics_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstStatistics *statistics;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_STATISTICS (object));

  statistics = GST_STATISTICS (object);

  switch (prop_id) {
    case ARG_BUFFERS:
      g_value_set_int64 (value, statistics->stats.buffers);
      break;
    case ARG_BYTES:
      g_value_set_int64 (value, statistics->stats.bytes);
      break;
    case ARG_EVENTS:
      g_value_set_int64 (value, statistics->stats.events);
      break;
    case ARG_BUFFER_UPDATE_FREQ:
      g_value_set_int64 (value, statistics->update_freq.buffers);
      break;
    case ARG_BYTES_UPDATE_FREQ:
      g_value_set_int64 (value, statistics->update_freq.bytes);
      break;
    case ARG_EVENT_UPDATE_FREQ:
      g_value_set_int64 (value, statistics->update_freq.events);
      break;
    case ARG_UPDATE_ON_EOS:
      g_value_set_boolean (value, statistics->update_on_eos);
      break;
    case ARG_UPDATE:
      g_value_set_boolean (value, statistics->update);
      break;
    case ARG_SILENT:
      g_value_set_boolean (value, statistics->silent);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
