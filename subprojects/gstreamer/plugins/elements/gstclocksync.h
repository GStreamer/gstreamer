/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2005 Wim Taymans <wim@fluendo.com>
 * Copyright (C) 2020 Jan Schmidt <jan@centricular.com>
 *
 * gstclocksync.h:
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

#ifndef __GST_CLOCKSYNC_H__
#define __GST_CLOCKSYNC_H__

#include <gst/gst.h>

G_BEGIN_DECLS

/* #defines don't like whitespacey bits */
#define GST_TYPE_CLOCKSYNC \
  (gst_clock_sync_get_type())
#define GST_CLOCKSYNC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CLOCKSYNC,GstClockSync))
#define GST_CLOCKSYNC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CLOCKSYNC,GstClockSyncClass))
#define GST_IS_CLOCKSYNC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CLOCKSYNC))
#define GST_IS_CLOCKSYNC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CLOCKSYNC))

typedef struct _GstClockSync      GstClockSync;
typedef struct _GstClockSyncClass GstClockSyncClass;

struct _GstClockSync
{
  GstElement element;

  /*< private >*/
  GstPad *sinkpad, *srcpad;

  GstSegment     segment;
  GstClockID     clock_id;
  gboolean       flushing;
  gboolean 	     sync;

  GCond          blocked_cond;
  gboolean       blocked;
  GstClockTimeDiff  ts_offset;
  gboolean sync_to_first;
  gboolean is_first;

  GstClockTime   upstream_latency;

  /* QoS */
  gboolean qos_enabled;
  // With STREAM_LOCK {
  GstClockTime earliest_in_time;
  GstClockTime current_rstart;
  GstClockTimeDiff current_jitter;
  GstClockTime avg_pt, avg_in_diff;
  gdouble avg_rate;             /* average with infinite window */

  /* when the last buffer left the sink, running time */
  GstClockTime last_left;

  /* the running time of the previous buffer */
  GstClockTime prev_rstart;

  // } With STREAM_LOCK
};

struct _GstClockSyncClass
{
  GstElementClass parent_class;
};

GType gst_clock_sync_get_type (void);

G_END_DECLS

#endif /* __GST_CLOCKSYNC_H__ */
