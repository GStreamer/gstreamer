/* GStreamer
 * Copyright (C) 2001 Steve Baker <stevebaker_org@yahoo.co.uk>
 *
 * gstdparam_smooth.h: Realtime smoothed dynamic parameter
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_DPSMOOTH_H__
#define __GST_DPSMOOTH_H__

#include "dparam.h"

G_BEGIN_DECLS
#define GST_TYPE_DPSMOOTH			(gst_dpsmooth_get_type ())
#define GST_DPSMOOTH(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_DPSMOOTH,GstDParamSmooth))
#define GST_DPSMOOTH_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_DPSMOOTH,GstDParamSmooth))
#define GST_IS_DPSMOOTH(obj)			(G_TYPE_CHECK_INSTANCE_TYPE	((obj), GST_TYPE_DPSMOOTH))
#define GST_IS_DPSMOOTH_CLASS(obj)		(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_DPSMOOTH))
typedef struct _GstDParamSmoothClass GstDParamSmoothClass;
typedef struct _GstDParamSmooth GstDParamSmooth;

struct _GstDParamSmooth
{
  GstDParam dparam;

  gint64 start_interp;
  gint64 end_interp;
  gint64 duration_interp;

  gfloat start_float;
  gfloat diff_float;
  gfloat current_float;
  gdouble start_double;
  gdouble diff_double;
  gdouble current_double;

  gint64 update_period;
  gint64 slope_time;
  gfloat slope_delta_float;
  gdouble slope_delta_double;

  gboolean need_interp_times;

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstDParamSmoothClass
{
  GstDParamClass parent_class;

  /* signal callbacks */
  gpointer _gst_reserved[GST_PADDING];
};

GType gst_dpsmooth_get_type (void);

GstDParam *gst_dpsmooth_new (GType type);

G_END_DECLS
#endif /* __GST_DPSMOOTH_H__ */
