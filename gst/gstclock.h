/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#include <gst/gst.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


typedef guint64 GstClockTime;
typedef gint64 GstClockTimeDiff;

#define GST_CLOCK_DIFF(s, e) (GstClockTimeDiff)((s)-(e))

typedef struct _GstClock GstClock;

struct _GstClock {
  gchar *name;
  GstClockTime start_time;
  GstClockTime current_time;
  GstClockTimeDiff adjust;
  gboolean locking;
  GList *sinkobjects;
  gint num, num_locked;
  GMutex *sinkmutex;
  GMutex *lock;
};

GstClock *gst_clock_new(gchar *name);
GstClock *gst_clock_get_system(void);

void gst_clock_register(GstClock *clock, GstObject *obj);
void gst_clock_set(GstClock *clock, GstClockTime time);
void gst_clock_reset(GstClock *clock);
void gst_clock_wait(GstClock *clock, GstClockTime time, GstObject *obj);
GstClockTimeDiff gst_clock_current_diff(GstClock *clock, GstClockTime time);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_CLOCK_H__ */
