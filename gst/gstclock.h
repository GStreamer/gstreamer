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

#define GST_CLOCK_DIFF(s, e) 	(GstClockTimeDiff)((s)-(e))
#define GST_TIMEVAL_TO_TIME(tv)	((tv).tv_sec * (guint64) G_USEC_PER_SEC + (tv).tv_usec)

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
  gdouble 	 speed;
  gboolean 	 active;

  GMutex	*active_mutex;
  GCond		*active_cond;
};

struct _GstClockClass {
  GstObjectClass        parent_class;

  /* vtable */
  void 			(*activate)		(GstClock *clock, gboolean active);
  void 			(*reset)		(GstClock *clock);

  void 			(*set_time)		(GstClock *clock, GstClockTime time);
  GstClockTime 		(*get_time)		(GstClock *clock);

  GstClockReturn	(*wait)			(GstClock *clock, GstClockTime time);
  GstClockID		(*wait_async)		(GstClock *clock, GstClockTime time, 
			  			 GstClockCallback func, gpointer user_data);
  void 			(*cancel_wait_async)	(GstClock *clock, GstClockID id);
  GstClockID		(*notify_async)		(GstClock *clock, GstClockTime interval, 
			  			 GstClockCallback func, gpointer user_data);
  void 			(*remove_notify_async)	(GstClock *clock, GstClockID id);
	
  void 			(*set_resolution)	(GstClock *clock, guint64 resolution);
  guint64		(*get_resolution)	(GstClock *clock);

  /* signals */
};

GType           	gst_clock_get_type 		(void);

void 			gst_clock_set_speed		(GstClock *clock, gdouble speed);
void 			gst_clock_get_speed		(GstClock *clock, gdouble speed);

void 			gst_clock_activate		(GstClock *clock, gboolean active);
gboolean 		gst_clock_is_active		(GstClock *clock);
void 			gst_clock_reset			(GstClock *clock);

void 			gst_clock_set_time		(GstClock *clock, GstClockTime time);
GstClockTime		gst_clock_get_time		(GstClock *clock);

GstClockReturn		gst_clock_wait			(GstClock *clock, GstClockTime time);
GstClockID		gst_clock_wait_async		(GstClock *clock, GstClockTime time, 
						 	 GstClockCallback func, gpointer user_data);
void			gst_clock_cancel_wait_async	(GstClock *clock, GstClockID id);
GstClockID		gst_clock_notify_async		(GstClock *clock, GstClockTime interval, 
						 	 GstClockCallback func, gpointer user_data);
void 			gst_clock_remove_notify_async	(GstClock *clock, GstClockID id);

void 			gst_clock_set_resolution	(GstClock *clock, guint64 resolution);
guint64			gst_clock_get_resolution	(GstClock *clock);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GST_CLOCK_H__ */
