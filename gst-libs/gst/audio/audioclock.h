/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * audioclock.h: Clock for use by audio plugins
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


#ifndef __GST_AUDIO_CLOCK_H__
#define __GST_AUDIO_CLOCK_H__

#include <gst/gstsystemclock.h>

G_BEGIN_DECLS

#define GST_TYPE_AUDIO_CLOCK \
  (gst_audio_clock_get_type())
#define GST_AUDIO_CLOCK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUDIO_CLOCK,GstAudioClock))
#define GST_AUDIO_CLOCK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUDIO_CLOCK,GstAudioClockClass))
#define GST_IS_AUDIO_CLOCK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUDIO_CLOCK))
#define GST_IS_AUDIO_CLOCK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AUDIO_CLOCK))

typedef struct _GstAudioClock GstAudioClock;
typedef struct _GstAudioClockClass GstAudioClockClass;

typedef GstClockTime (*GstAudioClockGetTimeFunc) (GstClock *clock, gpointer user_data);


struct _GstAudioClock {
  GstSystemClock clock;

  GstClockTime prev1, prev2;

  /* --- protected --- */
  GstAudioClockGetTimeFunc func;
  gpointer user_data;

  GstClockTimeDiff adjust;

  GSList *async_entries;

  gboolean active;

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstAudioClockClass {
  GstSystemClockClass parent_class;

  gpointer _gst_reserved[GST_PADDING];
};

GType           gst_audio_clock_get_type 	(void);
GstClock*	gst_audio_clock_new		(gchar *name, GstAudioClockGetTimeFunc func,
                                                 gpointer user_data);
void		gst_audio_clock_set_active 	(GstAudioClock *aclock, gboolean active);

void		gst_audio_clock_update_time	(GstAudioClock *aclock, GstClockTime time);

G_END_DECLS

#endif /* __GST_AUDIO_CLOCK_H__ */
