/* GStreamer
 *
 * Copyright (C) 2019 Collabora Ltd.
 *   Author: Stéphane Cerveau <scerveau@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */
#include "gstmpdmetricsrangenode.h"
#include "gstmpdparser.h"

G_DEFINE_TYPE (GstMPDMetricsRangeNode, gst_mpd_metrics_range_node,
    GST_TYPE_OBJECT);

/* GObject VMethods */

static void
gst_mpd_metrics_range_node_class_init (GstMPDMetricsRangeNodeClass * klass)
{
}

static void
gst_mpd_metrics_range_node_init (GstMPDMetricsRangeNode * self)
{
  self->starttime = 0;          /* [ms] */
  self->duration = 0;           /* [ms] */
}

GstMPDMetricsRangeNode *
gst_mpd_metrics_range_node_new (void)
{
  return g_object_new (GST_TYPE_MPD_METRICS_RANGE_NODE, NULL);
}

void
gst_mpd_metrics_range_node_free (GstMPDMetricsRangeNode * self)
{
  if (self)
    gst_object_unref (self);
}
