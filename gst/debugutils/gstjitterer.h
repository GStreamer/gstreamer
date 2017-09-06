/* 
 * GStreamer
 * Copyright (C) 2017 Vivia Nikolaidou <vivia@ahiru.eu>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
 
#ifndef __GST_JITTERER_H__
#define __GST_JITTERER_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_JITTERER            (gst_jitterer_get_type())
#define GST_JITTERER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_JITTERER,GstJitterer))
#define GST_IS_JITTERER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_JITTERER))
#define GST_JITTERER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_JITTERER,GstJittererClass))
#define GST_IS_JITTERER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_JITTERER))
#define GST_JITTERER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_JITTERER,GstJittererClass))

typedef struct _GstJitterer      GstJitterer;
typedef struct _GstJittererClass GstJittererClass;

struct _GstJitterer {
  GstElement parent;

  GstClockTime jitter_ampl;
  GstClockTimeDiff jitter_avg;
  GstClockTime drift_ampl;
  GstClockTimeDiff drift_avg;
  gboolean change_pts;
  gboolean change_dts;

  gint64 dts_drift_so_far;
  gint64 pts_drift_so_far;
  GRand *rand;
  GstClockTime prev_pts;

  GstPad *srcpad;
  GstPad *sinkpad;
};

struct _GstJittererClass {
  GstElementClass parent_class;
};

GType gst_jitterer_get_type (void);

gboolean gst_jitterer_plugin_init (GstPlugin * plugin);

G_END_DECLS

#endif /* __GST_JITTERER_H__ */
