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


#ifndef __GST_OSS_CLOCK_H__
#define __GST_OSS_CLOCK_H__

#include <gst/gstsystemclock.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_OSS_CLOCK \
  (gst_oss_clock_get_type())
#define GST_OSS_CLOCK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OSS_CLOCK,GstOssClock))
#define GST_OSS_CLOCK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OSS_CLOCK,GstOssClockClass))
#define GST_IS_OSS_CLOCK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OSS_CLOCK))
#define GST_IS_OSS_CLOCK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OSS_CLOCK))

typedef struct _GstOssClock GstOssClock;
typedef struct _GstOssClockClass GstOssClockClass;

typedef GstClockTime (*GstOssClockGetTimeFunc) (GstClock *clock, gpointer user_data);


struct _GstOssClock {
  GstSystemClock clock;

  GstOssClockGetTimeFunc func;
  gpointer user_data;

  GstClockTime prev1, prev2;
  GstClockTimeDiff adjust;
};

struct _GstOssClockClass {
  GstSystemClockClass parent_class;
};

GType                   gst_oss_clock_get_type 		(void);
GstOssClock*		gst_oss_clock_new		(gchar *name, GstOssClockGetTimeFunc func,
							 gpointer user_data);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_OSS_CLOCK_H__ */
