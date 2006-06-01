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


#ifndef __GST_MPEG_CLOCK_H__
#define __GST_MPEG_CLOCK_H__

#include <gst/gstsystemclock.h>

G_BEGIN_DECLS

#define GST_TYPE_MPEG_CLOCK \
  (gst_mpeg_clock_get_type())
#define GST_MPEG_CLOCK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MPEG_CLOCK,GstMPEGClock))
#define GST_MPEG_CLOCK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MPEG_CLOCK,GstMPEGClockClass))
#define GST_IS_MPEG_CLOCK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MPEG_CLOCK))
#define GST_IS_MPEG_CLOCK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MPEG_CLOCK))

typedef struct _GstMPEGClock GstMPEGClock;
typedef struct _GstMPEGClockClass GstMPEGClockClass;

typedef GstClockTime (*GstMPEGClockGetTimeFunc) (GstClock *clock, gpointer user_data);


struct _GstMPEGClock {
  GstSystemClock clock;

  GstMPEGClockGetTimeFunc func;
  gpointer user_data;
};

struct _GstMPEGClockClass {
  GstSystemClockClass parent_class;
};

GType                   gst_mpeg_clock_get_type         (void);
GstClock*               gst_mpeg_clock_new              (gchar *name, GstMPEGClockGetTimeFunc func,
                                                         gpointer user_data);

G_END_DECLS

#endif /* __GST_MPEG_CLOCK_H__ */
