/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstclock.h: Header for clock subsystem
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

#ifndef __GST_CLOCK_H__
#define __GST_CLOCK_H__

#include <gst/gstobject.h>

G_BEGIN_DECLS
/* --- standard type macros --- */
#define GST_TYPE_CLOCK  		(gst_clock_get_type ())
#define GST_CLOCK(clock) 		(G_TYPE_CHECK_INSTANCE_CAST ((clock), GST_TYPE_CLOCK, GstClock))
#define GST_IS_CLOCK(clock)  		(G_TYPE_CHECK_INSTANCE_TYPE ((clock), GST_TYPE_CLOCK))
#define GST_CLOCK_CLASS(cclass)  	(G_TYPE_CHECK_CLASS_CAST ((cclass), GST_TYPE_CLOCK, GstClockClass))
#define GST_IS_CLOCK_CLASS(cclass) 	(G_TYPE_CHECK_CLASS_TYPE ((cclass), GST_TYPE_CLOCK))
#define GST_CLOCK_GET_CLASS(clock) 	(G_TYPE_INSTANCE_GET_CLASS ((clock), GST_TYPE_CLOCK, GstClockClass))
typedef guint64 GstClockTime;
typedef gint64 GstClockTimeDiff;
typedef gpointer GstClockID;

#define GST_CLOCK_TIME_NONE  		((GstClockTime)-1)
#define GST_CLOCK_TIME_IS_VALID(time)	((time) != GST_CLOCK_TIME_NONE)

#define GST_SECOND  ((guint64) G_USEC_PER_SEC * G_GINT64_CONSTANT (1000))
#define GST_MSECOND ((guint64) GST_SECOND / G_GINT64_CONSTANT (1000))
#define GST_USECOND ((guint64) GST_SECOND / G_GINT64_CONSTANT (1000000))
#define GST_NSECOND ((guint64) GST_SECOND / G_GINT64_CONSTANT (1000000000))

#define GST_CLOCK_DIFF(s, e) 		(GstClockTimeDiff)((s) - (e))
#define GST_TIMEVAL_TO_TIME(tv)		((tv).tv_sec * GST_SECOND + (tv).tv_usec * GST_USECOND)
#define GST_TIME_TO_TIMEVAL(t,tv)			\
G_STMT_START { 						\
  (tv).tv_sec  =  (t) / GST_SECOND;			\
  (tv).tv_usec = ((t) - (tv).tv_sec * GST_SECOND) / GST_USECOND;	\
} G_STMT_END

#define GST_CLOCK_ENTRY_TRACE_NAME "GstClockEntry"

typedef struct _GstClockEntry GstClockEntry;
typedef struct _GstClock GstClock;
typedef struct _GstClockClass GstClockClass;

/* --- prototype for async callbacks --- */
typedef gboolean (*GstClockCallback) (GstClock * clock, GstClockTime time,
    GstClockID id, gpointer user_data);

typedef enum
{
  /* --- protected --- */
  GST_CLOCK_ENTRY_OK,
  GST_CLOCK_ENTRY_EARLY,
  GST_CLOCK_ENTRY_RESTART
} GstClockEntryStatus;

typedef enum
{
  /* --- protected --- */
  GST_CLOCK_ENTRY_SINGLE,
  GST_CLOCK_ENTRY_PERIODIC
} GstClockEntryType;

#define GST_CLOCK_ENTRY(entry)		((GstClockEntry *)(entry))
#define GST_CLOCK_ENTRY_CLOCK(entry)	((entry)->clock)
#define GST_CLOCK_ENTRY_TYPE(entry)	((entry)->type)
#define GST_CLOCK_ENTRY_TIME(entry)	((entry)->time)
#define GST_CLOCK_ENTRY_INTERVAL(entry)	((entry)->interval)
#define GST_CLOCK_ENTRY_STATUS(entry)	((entry)->status)

struct _GstClockEntry
{
  /* --- protected --- */
  GstClock *clock;
  GstClockEntryType type;
  GstClockTime time;
  GstClockTime interval;
  GstClockEntryStatus status;
  GstClockCallback func;
  gpointer user_data;
};

typedef enum
{
  GST_CLOCK_STOPPED = 0,
  GST_CLOCK_TIMEOUT = 1,
  GST_CLOCK_EARLY = 2,
  GST_CLOCK_ERROR = 3,
  GST_CLOCK_UNSUPPORTED = 4
} GstClockReturn;

typedef enum
{
  GST_CLOCK_FLAG_CAN_DO_SINGLE_SYNC = (1 << 1),
  GST_CLOCK_FLAG_CAN_DO_SINGLE_ASYNC = (1 << 2),
  GST_CLOCK_FLAG_CAN_DO_PERIODIC_SYNC = (1 << 3),
  GST_CLOCK_FLAG_CAN_DO_PERIODIC_ASYNC = (1 << 4),
  GST_CLOCK_FLAG_CAN_SET_RESOLUTION = (1 << 5),
  GST_CLOCK_FLAG_CAN_SET_SPEED = (1 << 6)
} GstClockFlags;

#define GST_CLOCK_FLAGS(clock)  (GST_CLOCK(clock)->flags)

struct _GstClock
{
  GstObject object;

  GstClockFlags flags;

  /* --- protected --- */
  GstClockTime start_time;
  GstClockTime last_time;
  gint64 max_diff;

  /* --- private --- */
  guint64 resolution;
  GList *entries;
  GMutex *active_mutex;
  GCond *active_cond;
  gboolean stats;

  GstClockTime last_event;
  GstClockTime max_event_diff;

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstClockClass
{
  GstObjectClass parent_class;

  /* vtable */
    gdouble (*change_speed) (GstClock * clock,
      gdouble oldspeed, gdouble newspeed);
    gdouble (*get_speed) (GstClock * clock);
    guint64 (*change_resolution) (GstClock * clock, guint64 old_resolution,
      guint64 new_resolution);
    guint64 (*get_resolution) (GstClock * clock);

    GstClockTime (*get_internal_time) (GstClock * clock);

  /* waiting on an ID */
    GstClockEntryStatus (*wait) (GstClock * clock, GstClockEntry * entry);
    GstClockEntryStatus (*wait_async) (GstClock * clock, GstClockEntry * entry);
  void (*unschedule) (GstClock * clock, GstClockEntry * entry);
  void (*unlock) (GstClock * clock, GstClockEntry * entry);
  gpointer _gst_reserved[GST_PADDING];
};

GType gst_clock_get_type (void);

gdouble gst_clock_set_speed (GstClock * clock, gdouble speed);
gdouble gst_clock_get_speed (GstClock * clock);

guint64 gst_clock_set_resolution (GstClock * clock, guint64 resolution);
guint64 gst_clock_get_resolution (GstClock * clock);

void gst_clock_set_active (GstClock * clock, gboolean active);
gboolean gst_clock_is_active (GstClock * clock);
void gst_clock_reset (GstClock * clock);
gboolean gst_clock_handle_discont (GstClock * clock, guint64 time);

GstClockTime gst_clock_get_time (GstClock * clock);
GstClockTime gst_clock_get_event_time (GstClock * clock);


GstClockID gst_clock_get_next_id (GstClock * clock);

/* creating IDs that can be used to get notifications */
GstClockID gst_clock_new_single_shot_id (GstClock * clock, GstClockTime time);
GstClockID gst_clock_new_periodic_id (GstClock * clock,
    GstClockTime start_time, GstClockTime interval);

/* operations on IDs */
GstClockTime gst_clock_id_get_time (GstClockID id);
GstClockReturn gst_clock_id_wait (GstClockID id, GstClockTimeDiff * jitter);
GstClockReturn gst_clock_id_wait_async (GstClockID id,
    GstClockCallback func, gpointer user_data);
void gst_clock_id_unschedule (GstClockID id);
void gst_clock_id_unlock (GstClockID id);
void gst_clock_id_free (GstClockID id);


G_END_DECLS
#endif /* __GST_CLOCK_H__ */
