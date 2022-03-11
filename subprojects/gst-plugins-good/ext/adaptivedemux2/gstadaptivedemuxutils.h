/* GStreamer
 *
 * Copyright (C) 2014 Samsung Electronics. All rights reserved.
 *   Author: Thiago Santos <thiagoss@osg.samsung.com>
 * Copyright (C) 2021-2022 Jan Schmidt <jan@centricular.com>
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

#ifndef _GST_ADAPTIVE_DEMUX_UTILS_H_
#define _GST_ADAPTIVE_DEMUX_UTILS_H_

#include <gst/gst.h>

typedef struct _GstAdaptiveDemuxClock GstAdaptiveDemuxClock;

typedef struct _GstAdaptiveDemuxLoop GstAdaptiveDemuxLoop;

GstAdaptiveDemuxClock *gst_adaptive_demux_clock_new (void);
GstAdaptiveDemuxClock *gst_adaptive_demux_clock_ref (GstAdaptiveDemuxClock *
    clock);
void gst_adaptive_demux_clock_unref (GstAdaptiveDemuxClock * clock);

GstClockTime gst_adaptive_demux_clock_get_time (GstAdaptiveDemuxClock * clock);
GDateTime *gst_adaptive_demux_clock_get_now_utc (GstAdaptiveDemuxClock * clock);
void gst_adaptive_demux_clock_set_utc_time (GstAdaptiveDemuxClock * clock, GDateTime *utc_now);

GstAdaptiveDemuxLoop *gst_adaptive_demux_loop_new (void);
GstAdaptiveDemuxLoop *gst_adaptive_demux_loop_ref (GstAdaptiveDemuxLoop * loop);
void gst_adaptive_demux_loop_unref (GstAdaptiveDemuxLoop * loop);

void gst_adaptive_demux_loop_start (GstAdaptiveDemuxLoop *loop);
void gst_adaptive_demux_loop_stop (GstAdaptiveDemuxLoop * loop, gboolean wait);

guint gst_adaptive_demux_loop_call (GstAdaptiveDemuxLoop *loop, GSourceFunc func,
    gpointer data, GDestroyNotify notify);
guint gst_adaptive_demux_loop_call_delayed (GstAdaptiveDemuxLoop *loop, GstClockTime delay,
    GSourceFunc func, gpointer data, GDestroyNotify notify);
void gst_adaptive_demux_loop_cancel_call (GstAdaptiveDemuxLoop *loop, guint cb_id);

gboolean gst_adaptive_demux_loop_pause_and_lock (GstAdaptiveDemuxLoop * loop);
gboolean gst_adaptive_demux_loop_unlock_and_unpause (GstAdaptiveDemuxLoop * loop);

GstDateTime *gst_adaptive_demux_util_parse_http_head_date (const gchar *http_date);

typedef struct _GstEventStore GstEventStore;

struct _GstEventStore {
  GArray *events;
  gboolean events_pending;
};

void gst_event_store_init(GstEventStore *store);
void gst_event_store_deinit(GstEventStore *store);

void gst_event_store_flush(GstEventStore *store);

void gst_event_store_insert_event (GstEventStore *store, GstEvent * event, gboolean delivered);
GstEvent *gst_event_store_get_next_pending (GstEventStore *store);
void gst_event_store_mark_delivered (GstEventStore *store, GstEvent *event);
void gst_event_store_mark_all_undelivered (GstEventStore *store);
#endif
