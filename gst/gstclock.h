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

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <gst/gstobject.h>

#define GST_TYPE_CLOCK \
  (gst_clock_get_type())
#define GST_CLOCK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CLOCK,GstClock))
#define GST_CLOCK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CLOCK,GstClockClass))
#define GST_IS_CLOCK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CLOCK))
#define GST_IS_CLOCK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CLOCK))
	
typedef guint64 	GstClockTime;
typedef gint64 		GstClockTimeDiff;
typedef gpointer 	GstClockID;

#define GST_CLOCK_TIME_NONE  ((guint64)-1)

#define GST_SECOND  ((guint64)G_USEC_PER_SEC * 1000LL)
#define GST_MSECOND ((guint64)GST_SECOND/1000LL)
#define GST_USECOND ((guint64)GST_SECOND/1000000LL)
#define GST_NSECOND ((guint64)GST_SECOND/1000000000LL)

#define GST_CLOCK_DIFF(s, e) 		(GstClockTimeDiff)((s)-(e))
#define GST_TIMEVAL_TO_TIME(tv)		((tv).tv_sec * GST_SECOND + (tv).tv_usec * GST_USECOND)
#define GST_TIME_TO_TIMEVAL(t,tv)			\
G_STMT_START { 						\
  (tv).tv_sec  = (t) / GST_SECOND;			\
  (tv).tv_usec = ((t) / GST_USECOND) % GST_MSECOND;	\
} G_STMT_END

typedef struct _GstClock 	GstClock;
typedef struct _GstClockClass 	GstClockClass;

typedef void (*GstClockCallback) (GstClock *clock, GstClockTime time, GstClockID id, gpointer user_data);

typedef enum
{
  GST_CLOCK_STOPPED 	= 0,
  GST_CLOCK_TIMEOUT 	= 1,
  GST_CLOCK_EARLY 	= 2,
  GST_CLOCK_ERROR 	= 3,
} GstClockReturn;

struct _GstClock {
  GstObject 	 object;

  GstClockTime	 start_time;
  GstClockTime	 last_time;
  gboolean 	 accept_discont;
  gdouble 	 speed;
  gboolean 	 active;
  GList		*entries;
  gboolean	 async_supported;

  GMutex	*active_mutex;
  GCond		*active_cond;
};

struct _GstClockClass {
  GstObjectClass        parent_class;

  /* vtable */
  GstClockTime 		(*get_internal_time)	(GstClock *clock);

  void 			(*set_resolution)	(GstClock *clock, guint64 resolution);
  guint64		(*get_resolution)	(GstClock *clock);

  /* signals */
};

GType           	gst_clock_get_type 		(void);

void 			gst_clock_set_speed		(GstClock *clock, gdouble speed);
gdouble 		gst_clock_get_speed		(GstClock *clock);

void 			gst_clock_set_active		(GstClock *clock, gboolean active);
gboolean 		gst_clock_is_active		(GstClock *clock);
void 			gst_clock_reset			(GstClock *clock);
gboolean		gst_clock_handle_discont	(GstClock *clock, guint64 time);
gboolean 		gst_clock_async_supported	(GstClock *clock);

GstClockTime		gst_clock_get_time		(GstClock *clock);

GstClockReturn		gst_clock_wait			(GstClock *clock, GstClockTime time, GstClockTimeDiff *jitter);
GstClockID		gst_clock_wait_async		(GstClock *clock, GstClockTime time, 
						 	 GstClockCallback func, gpointer user_data);
void			gst_clock_cancel_wait_async	(GstClock *clock, GstClockID id);
GstClockID		gst_clock_notify_async		(GstClock *clock, GstClockTime interval, 
						 	 GstClockCallback func, gpointer user_data);
void 			gst_clock_remove_notify_async	(GstClock *clock, GstClockID id);
GstClockReturn		gst_clock_wait_id		(GstClock *clock, GstClockID id, GstClockTimeDiff *jitter);

GstClockID		gst_clock_get_next_id		(GstClock *clock);
void			gst_clock_unlock_id		(GstClock *clock, GstClockID id);

GstClockTime		gst_clock_id_get_time		(GstClockID id);

void 			gst_clock_set_resolution	(GstClock *clock, guint64 resolution);
guint64			gst_clock_get_resolution	(GstClock *clock);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GST_CLOCK_H__ */
