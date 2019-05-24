/*
 * No description.
 *
 * gstmpdmetricsnode.h
 *
 * Copyright (C) 2019 Collabora inc.
 * Authors:
 *   Stéphane Cerveau <scerveau@collabora.com>
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
#ifndef __GSTMPDMETRICSNODE_H__
#define __GSTMPDMETRICSNODE_H__

#include <gst/gst.h>
#include "gstmpdhelper.h"

G_BEGIN_DECLS

#define GST_TYPE_MPD_METRICS_NODE gst_mpd_metrics_node_get_type ()
#define GST_MPD_METRICS_NODE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MPD_METRICS_NODE, GstMPDMetricsNode))
#define GST_MPD_METRICS_NODE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MPD_METRICS_NODE, GstMPDMetricsNodeClass))
#define GST_IS_MPD_METRICS_NODE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MPD_METRICS_NODE))
#define GST_IS_MPD_METRICS_NODE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MPD_METRICS_NODE))
#define GST_MPD_METRICS_NODE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_MPD_METRICS_NODE, GstMPDMetricsNodeClass))

typedef struct _GstMPDMetricsNode                GstMPDMetricsNode;
typedef struct _GstMPDMetricsNodeClass           GstMPDMetricsNodeClass;


struct _GstMPDMetricsNode
{
  GstObject parent_instance;
  gchar *metrics;
  /* list of Metrics Range nodes */
  GList *MetricsRanges;
  /* list of Reporting nodes */
  GList *Reportings;
};

struct _GstMPDMetricsNodeClass {
  GstObjectClass parent_class;
};


G_GNUC_INTERNAL GType gst_mpd_metrics_node_get_type (void);

GstMPDMetricsNode * gst_mpd_metrics_node_new (void);
void gst_mpd_metrics_node_free (GstMPDMetricsNode* self);

G_END_DECLS

#endif /* __GSTMPDMETRICSNODE_H__ */
