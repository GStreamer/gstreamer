/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * gstlevel.c: signals RMS, peak and decaying peak levels
 * Copyright (C) 2000,2001,2002,2003
 *           Thomas Vander Stichele <thomas at apestaart dot org>
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


#ifndef __GST_LEVEL_H__
#define __GST_LEVEL_H__


#include <gst/gst.h>

#include "gstlevel-marshal.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_LEVEL \
  (gst_level_get_type())
#define GST_LEVEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_LEVEL,GstLevel))
#define GST_LEVEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ULAW,GstLevel))
#define GST_IS_LEVEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_LEVEL))
#define GST_IS_LEVEL_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_LEVEL))

typedef struct _GstLevel GstLevel;
typedef struct _GstLevelClass GstLevelClass;

struct _GstLevel {
  GstElement element;

  GstPad *sinkpad, *srcpad;
  gboolean signal;		/* whether or not to emit signals */
  gdouble interval;		/* how many seconds between emits */

  gint rate;			/* caps variables */
  gint width;
  gint channels;

  gdouble decay_peak_ttl;	/* time to live for peak in seconds */
  gdouble decay_peak_falloff;	/* falloff in dB/sec */
  gdouble num_samples;		/* cumulative sample count */

  /* per-channel arrays for intermediate values */
  gdouble *CS;			/* normalized Cumulative Square */
  gdouble *peak;		/* normalized Peak value over buffer */
  gdouble *last_peak;		/* last normalized Peak value over interval */
  gdouble *decay_peak;		/* running decaying normalized Peak */
  gdouble *MS;			/* normalized Mean Square of buffer */
  gdouble *RMS_dB;		/* RMS in dB to emit */
  gdouble *decay_peak_age;	/* age of last peak */
};

struct _GstLevelClass {
  GstElementClass parent_class;
  void (*level) (GstElement *element, gdouble time, gint channel,
                 gdouble RMS_dB, gdouble peak_dB, gdouble decay_peak_dB);
};

GType gst_level_get_type(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_STEREO_H__ */
