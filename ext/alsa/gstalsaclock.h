/*
 * Copyright (C) 2001 CodeFactory AB
 * Copyright (C) 2001 Thomas Nyberg <thomas@codefactory.se>
 * Copyright (C) 2001-2002 Andy Wingo <apwingo@eos.ncsu.edu>
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __GST_ALSA_CLOCK_H__
#define __GST_ALSA_CLOCK_H__

#include "gstalsa.h"

G_BEGIN_DECLS

#define GST_ALSA_CLOCK(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ALSA_CLOCK,GstAlsaClock))
#define GST_ALSA_CLOCK_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ALSA_CLOCK,GstAlsaClockClass))
#define GST_IS_ALSA_CLOCK(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ALSA_CLOCK))
#define GST_IS_ALSA_CLOCK_CLASS(obj)	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ALSA_CLOCK))
#define GST_TYPE_ALSA_CLOCK		(gst_alsa_clock_get_type())

typedef GstClockTime (*GstAlsaClockGetTimeFunc) (GstAlsa *);

struct _GstAlsaClock {
  GstSystemClock		parent;

  GstAlsaClockGetTimeFunc	get_time;
  GstAlsa *			owner;

  GstClockTimeDiff		adjust; 	/* adjustment to real clock (recalculated when stopping) */
  GstClockTime			start_time;	/* time when the stream started (NONE when stopped) */
  GstClockTime			last_unlock;    /* time of last unlock request */
};

struct _GstAlsaClockClass {
  GstSystemClockClass parent_class;
};

GType		gst_alsa_clock_get_type	(void);
GstAlsaClock *	gst_alsa_clock_new	(gchar *			name,
					 GstAlsaClockGetTimeFunc	func,
					 GstAlsa *			owner);

void	gst_alsa_clock_start	(GstAlsaClock *	clock);
void	gst_alsa_clock_stop	(GstAlsaClock *	clock);

G_END_DECLS

#endif /* __GST_ALSA_CLOCK_H__ */
