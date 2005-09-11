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
#define GST_TYPE_CLOCK			(gst_clock_get_type ())
#define GST_CLOCK(clock)		(G_TYPE_CHECK_INSTANCE_CAST ((clock), GST_TYPE_CLOCK, GstClock))
#define GST_IS_CLOCK(clock)		(G_TYPE_CHECK_INSTANCE_TYPE ((clock), GST_TYPE_CLOCK))
#define GST_CLOCK_CLASS(cclass)		(G_TYPE_CHECK_CLASS_CAST ((cclass), GST_TYPE_CLOCK, GstClockClass))
#define GST_IS_CLOCK_CLASS(cclass)	(G_TYPE_CHECK_CLASS_TYPE ((cclass), GST_TYPE_CLOCK))
#define GST_CLOCK_GET_CLASS(clock)	(G_TYPE_INSTANCE_GET_CLASS ((clock), GST_TYPE_CLOCK, GstClockClass))
#define GST_CLOCK_CAST(clock)		((GstClock*)(clock))

/**
 * GstClockTime:
 *
 * A datatype to hold a time, measured in nanoseconds.
 */
typedef guint64	GstClockTime;
/**
 * GstClockTimeDiff:
 *
 * A datatype to hold a timedifference, measured in nanoseconds.
 */
typedef gint64 GstClockTimeDiff;
/**
 * GstClockID:
 *
 * A detatype to hold the handle to an outstanding async clock callback
 */
typedef gpointer GstClockID;

/**
 * GST_CLOCK_TIME_NONE:
 *
 * Constant to define an undefined clock time
 */
#define GST_CLOCK_TIME_NONE		((GstClockTime) -1)
/**
 * GST_CLOCK_TIME_IS_VALID:
 * @time: clock time to validate
 *
 * Tests if a given #GstClockTime represents a valid defined time.
 */
#define GST_CLOCK_TIME_IS_VALID(time)	((time) != GST_CLOCK_TIME_NONE)

/**
 * GST_SECOND:
 *
 * Constant that defines one GStreamer second
 */
#define GST_SECOND  (G_USEC_PER_SEC * G_GINT64_CONSTANT (1000))
/**
 * GST_MSECOND:
 *
 * Constant that defines one GStreamer millisecond
 */
#define GST_MSECOND (GST_SECOND / G_GINT64_CONSTANT (1000))
/**
 * GST_USECOND:
 *
 * Constant that defines one GStreamer microsecond
 */
#define GST_USECOND (GST_SECOND / G_GINT64_CONSTANT (1000000))
/**
 * GST_NSECOND:
 *
 * Constant that defines one GStreamer nanosecond
 */
#define GST_NSECOND (GST_SECOND / G_GINT64_CONSTANT (1000000000))

/**
 * GST_CLOCK_DIFF:
 * @s: the first time
 * @e: the second time
 *
 * Calculate a difference between two clock times as a #GstClockTimeDiff.
 */
#define GST_CLOCK_DIFF(s, e)		(GstClockTimeDiff)((s) - (e))

/**
 * GST_TIMEVAL_TO_TIME:
 * @tv: the timeval to convert
 *
 * Convert a GTimeVal to a #GstClockTime.
 */
#define GST_TIMEVAL_TO_TIME(tv)		((tv).tv_sec * GST_SECOND + (tv).tv_usec * GST_USECOND)
/**
 * GST_TIME_TO_TIMEVAL:
 * @t: The GstClockTime to convert
 * @tv: The target timeval
 *
 * Convert a GstClockTime to a GTimeVal
 */
#define GST_TIME_TO_TIMEVAL(t,tv)			\
G_STMT_START {						\
  (tv).tv_sec  =  (t) / GST_SECOND;			\
  (tv).tv_usec = ((t) - (tv).tv_sec * GST_SECOND) / GST_USECOND;	\
} G_STMT_END

/**
 * GST_TIMESPEC_TO_TIME:
 * @ts: the timespec to convert
 *
 * Convert a struct timespec (see man pselect) to a #GstClockTime.
 */
#define GST_TIMESPEC_TO_TIME(ts)		((ts).tv_sec * GST_SECOND + (ts).tv_nsec * GST_NSECOND)
/**
 * GST_TIME_TO_TIMESPEC:
 * @t: The GstClockTime to convert
 * @ts: The target timespec
 *
 * Convert a #GstClockTime to a struct timespec (see man pselect)
 */
#define GST_TIME_TO_TIMESPEC(t,ts)			\
G_STMT_START {						\
  (ts).tv_sec  =  (t) / GST_SECOND;			\
  (ts).tv_nsec = ((t) - (ts).tv_sec * GST_SECOND) / GST_NSECOND;	\
} G_STMT_END

/* timestamp debugging macros */
#define GST_TIME_FORMAT "u:%02u:%02u.%09u"
#define GST_TIME_ARGS(t) \
        (guint) (((GstClockTime)(t)) / (GST_SECOND * 60 * 60)), \
        (guint) ((((GstClockTime)(t)) / (GST_SECOND * 60)) % 60), \
        (guint) ((((GstClockTime)(t)) / GST_SECOND) % 60), \
        (guint) (((GstClockTime)(t)) % GST_SECOND)

/**
 * GST_CLOCK_ENTRY_TRACE_NAME:
 *
 * The name used for tracing clock entry allocations.
 */
#define GST_CLOCK_ENTRY_TRACE_NAME "GstClockEntry"

typedef struct _GstClockEntry	GstClockEntry;
typedef struct _GstClock	GstClock;
typedef struct _GstClockClass	GstClockClass;

/* --- prototype for async callbacks --- */
/**
 * GstClockCallback:
 * @clock: The clock that triggered the callback
 * @time: The time it was triggered
 * @id: The #GstClockID that expired
 * @user_data: user data passed in the async_wait call
 *
 * The function prototype of the callback.
 *
 * Returns: %TRUE or %FALSE (currently unused)
 */
typedef gboolean	(*GstClockCallback)	(GstClock *clock, GstClockTime time,
						 GstClockID id, gpointer user_data);
/**
 * GstClockReturn:
 * @GST_CLOCK_OK: The operation succeded.
 * @GST_CLOCK_EARLY: The operation was scheduled too late.
 * @GST_CLOCK_UNSCHEDULED: The clockID was unscheduled
 * @GST_CLOCK_BUSY: The ClockID is busy
 * @GST_CLOCK_BADTIME: A bad time was provided to a function.
 * @GST_CLOCK_ERROR: An error occured
 * @GST_CLOCK_UNSUPPORTED: Operation is not supported
 *
 * The return value of a clock operation.
 */
typedef enum
{
  GST_CLOCK_OK		=  0,
  GST_CLOCK_EARLY	=  1,
  GST_CLOCK_UNSCHEDULED	=  2,
  GST_CLOCK_BUSY	=  3,
  GST_CLOCK_BADTIME	=  4,
  GST_CLOCK_ERROR	=  5,
  GST_CLOCK_UNSUPPORTED	=  6,
} GstClockReturn;

/**
 * GstClockEntryType:
 * @GST_CLOCK_ENTRY_SINGLE: a single shot timeout
 * @GST_CLOCK_ENTRY_PERIODIC: a periodic timeout request
 *
 * The type of the clock entry
 */
typedef enum {
  GST_CLOCK_ENTRY_SINGLE,
  GST_CLOCK_ENTRY_PERIODIC
} GstClockEntryType;

/**
 * GST_CLOCK_ENTRY:
 * @entry: the entry to cast
 *
 * Cast to a clock entry
 */
#define GST_CLOCK_ENTRY(entry)		((GstClockEntry *)(entry))
/**
 * GST_CLOCK_ENTRY_CLOCK:
 * @entry: the entry to query
 *
 * Get the owner clock of the entry
 */
#define GST_CLOCK_ENTRY_CLOCK(entry)	((entry)->clock)
/**
 * GST_CLOCK_ENTRY_TYPE:
 * @entry: the entry to query
 *
 * Get the type of the clock entry
 */
#define GST_CLOCK_ENTRY_TYPE(entry)	((entry)->type)
/**
 * GST_CLOCK_ENTRY_TIME:
 * @entry: the entry to query
 *
 * Get the requested time of this entry
 */
#define GST_CLOCK_ENTRY_TIME(entry)	((entry)->time)
/**
 * GST_CLOCK_ENTRY_INTERVAL:
 * @entry: the entry to query
 *
 * Get the interval of this periodic entry
 */
#define GST_CLOCK_ENTRY_INTERVAL(entry)	((entry)->interval)
/**
 * GST_CLOCK_ENTRY_STATUS:
 * @entry: the entry to query
 *
 * The status of the entry
 */
#define GST_CLOCK_ENTRY_STATUS(entry)	((entry)->status)

/**
 * GstClockEntry:
 * @refcount: reference counter (read-only)
 *
 * All pending timeouts or periodic notifies are converted into
 * an entry.
 */
struct _GstClockEntry {
  gint			refcount;
  /*< protected >*/
  GstClock		*clock;
  GstClockEntryType	 type;
  GstClockTime		 time;
  GstClockTime		 interval;
  GstClockReturn	 status;
  GstClockCallback	 func;
  gpointer		 user_data;
};

/**
 * GstClockFlags:
 * @GST_CLOCK_FLAG_CAN_DO_SINGLE_SYNC: clock can do a single sync timeout request
 * @GST_CLOCK_FLAG_CAN_DO_SINGLE_ASYNC: clock can do a single async timeout request
 * @GST_CLOCK_FLAG_CAN_DO_PERIODIC_SYNC: clock can do sync periodic timeout requests
 * @GST_CLOCK_FLAG_CAN_DO_PERIODIC_ASYNC: clock can do async periodic timeout callbacks
 * @GST_CLOCK_FLAG_CAN_SET_RESOLUTION: clock's resolution can be changed
 *
 * The capabilities of this clock
 */
typedef enum {
  GST_CLOCK_FLAG_CAN_DO_SINGLE_SYNC     = (1 << 1),
  GST_CLOCK_FLAG_CAN_DO_SINGLE_ASYNC    = (1 << 2),
  GST_CLOCK_FLAG_CAN_DO_PERIODIC_SYNC   = (1 << 3),
  GST_CLOCK_FLAG_CAN_DO_PERIODIC_ASYNC  = (1 << 4),
  GST_CLOCK_FLAG_CAN_SET_RESOLUTION     = (1 << 5),
} GstClockFlags;

/**
 * GST_CLOCK_FLAGS:
 * @clock: the clock to query
 *
 * Gets the #GstClockFlags clock flags.
 */
#define GST_CLOCK_FLAGS(clock)  (GST_CLOCK(clock)->flags)

#define GST_CLOCK_COND(clock)            (GST_CLOCK_CAST(clock)->entries_changed)
#define GST_CLOCK_WAIT(clock)            g_cond_wait(GST_CLOCK_COND(clock),GST_GET_LOCK(clock))
#define GST_CLOCK_TIMED_WAIT(clock,tv)   g_cond_timed_wait(GST_CLOCK_COND(clock),GST_GET_LOCK(clock),tv)
#define GST_CLOCK_BROADCAST(clock)       g_cond_broadcast(GST_CLOCK_COND(clock))

struct _GstClock {
  GstObject	 object;

  /*< public >*/
  GstClockFlags	 flags;

  /*< protected >*/ /* with LOCK */
  GstClockTime	 adjust;
  GstClockTime	 last_time;
  GList		*entries;
  GCond		*entries_changed;

  /*< private >*/
  guint64	 resolution;
  gboolean	 stats;

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstClockClass {
  GstObjectClass        parent_class;

  /*< protected >*/
  /* vtable */
  guint64               (*change_resolution)    (GstClock *clock, guint64 old_resolution,
						 guint64 new_resolution);
  guint64               (*get_resolution)       (GstClock *clock);

  GstClockTime		(*get_internal_time)	(GstClock *clock);

  /* waiting on an ID */
  GstClockReturn        (*wait)			(GstClock *clock, GstClockEntry *entry);
  GstClockReturn        (*wait_async)           (GstClock *clock, GstClockEntry *entry);
  void                  (*unschedule)		(GstClock *clock, GstClockEntry *entry);

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GType			gst_clock_get_type		(void);

guint64			gst_clock_set_resolution	(GstClock *clock, guint64 resolution);
guint64			gst_clock_get_resolution	(GstClock *clock);

GstClockTime		gst_clock_get_time		(GstClock *clock);
void			gst_clock_set_time_adjust	(GstClock *clock, GstClockTime adjust);

GstClockTime		gst_clock_adjust_unlocked	(GstClock *clock, GstClockTime internal);


/* creating IDs that can be used to get notifications */
GstClockID		gst_clock_new_single_shot_id	(GstClock *clock,
							 GstClockTime time);
GstClockID		gst_clock_new_periodic_id	(GstClock *clock,
							 GstClockTime start_time,
							 GstClockTime interval);

/* reference counting */
GstClockID              gst_clock_id_ref                (GstClockID id);
void                    gst_clock_id_unref              (GstClockID id);

/* operations on IDs */
gint			gst_clock_id_compare_func	(gconstpointer id1, gconstpointer id2);

GstClockTime		gst_clock_id_get_time		(GstClockID id);
GstClockReturn		gst_clock_id_wait		(GstClockID id,
							 GstClockTimeDiff *jitter);
GstClockReturn		gst_clock_id_wait_async		(GstClockID id,
							 GstClockCallback func,
							 gpointer user_data);
void			gst_clock_id_unschedule		(GstClockID id);

G_END_DECLS

#endif /* __GST_CLOCK_H__ */
