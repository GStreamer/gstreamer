/* GStreamer
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


#ifndef __GST_VIDEOBALANCE_H__
#define __GST_VIDEOBALANCE_H__


#include <gst/gst.h>

#include "gstvideofilter.h"


G_BEGIN_DECLS

#define GST_TYPE_VIDEOBALANCE \
  (gst_videobalance_get_type())
#define GST_VIDEOBALANCE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEOBALANCE,GstVideobalance))
#define GST_VIDEOBALANCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEOBALANCE,GstVideobalanceClass))
#define GST_IS_VIDEOBALANCE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEOBALANCE))
#define GST_IS_VIDEOBALANCE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEOBALANCE))

typedef struct _GstVideobalance GstVideobalance;
typedef struct _GstVideobalanceClass GstVideobalanceClass;

struct _GstVideobalance {
  GstVideofilter videofilter;

  guint8   *tabley, **tableu, **tablev;
  gboolean needupdate;

  gdouble contrast;
  gdouble brightness;
  gdouble hue;
  gdouble saturation;
  
  GList *channels;
};

struct _GstVideobalanceClass {
  GstVideofilterClass parent_class;
};

GType gst_videobalance_get_type(void);

G_END_DECLS

#endif /* __GST_VIDEOBALANCE_H__ */
