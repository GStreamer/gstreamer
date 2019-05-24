/* GStreamer
 *
 * Copyright (C) 2019 Collabora Ltd.
 *   Author: Stéphane Cerveau <scerveau@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library (COPYING); if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#ifndef __GSTMPDMETRICSRANGENODE_H__
#define __GSTMPDMETRICSRANGENODE_H__

#include <gst/gst.h>
#include "gstmpdhelper.h"

G_BEGIN_DECLS

#define GST_TYPE_MPD_METRICS_RANGE_NODE gst_mpd_metrics_range_node_get_type ()
#define GST_MPD_METRICS_RANGE_NODE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MPD_METRICS_RANGE_NODE, GstMPDMetricsRangeNode))
#define GST_MPD_METRICS_RANGE_NODE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MPD_METRICS_RANGE_NODE, GstMPDMetricsRangeNodeClass))
#define GST_IS_MPD_METRICS_RANGE_NODE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MPD_METRICS_RANGE_NODE))
#define GST_IS_MPD_METRICS_RANGE_NODE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MPD_METRICS_RANGE_NODE))
#define GST_MPD_METRICS_RANGE_NODE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_MPD_METRICS_RANGE_NODE, GstMPDMetricsRangeNodeClass))

typedef struct _GstMPDMetricsRangeNode                GstMPDMetricsRangeNode;
typedef struct _GstMPDMetricsRangeNodeClass           GstMPDMetricsRangeNodeClass;


struct _GstMPDMetricsRangeNode
{
  GstObject parent_instance;
  guint64 starttime;                 /* [ms] */
  guint64 duration;                  /* [ms] */
};

struct _GstMPDMetricsRangeNodeClass {
  GstObjectClass parent_class;
};


G_GNUC_INTERNAL GType gst_mpd_metrics_range_node_get_type (void);

GstMPDMetricsRangeNode * gst_mpd_metrics_range_node_new (void);
void gst_mpd_metrics_range_node_free (GstMPDMetricsRangeNode* self);

G_END_DECLS

#endif /* __GSTMPDMETRICSRANGENODE_H__ */
