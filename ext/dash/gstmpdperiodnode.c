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
#include "gstmpdperiodnode.h"
#include "gstmpdparser.h"

G_DEFINE_TYPE (GstMPDPeriodNode, gst_mpd_period_node, GST_TYPE_OBJECT);

/* GObject VMethods */

static void
gst_mpd_period_node_finalize (GObject * object)
{
  GstMPDPeriodNode *self = GST_MPD_PERIOD_NODE (object);

  if (self->id)
    xmlFree (self->id);
  gst_mpd_helper_segment_base_type_free (self->SegmentBase);
  gst_mpd_segment_list_node_free (self->SegmentList);
  gst_mpd_segment_template_node_free (self->SegmentTemplate);
  g_list_free_full (self->AdaptationSets,
      (GDestroyNotify) gst_mpd_adaptation_set_node_free);
  g_list_free_full (self->Subsets, (GDestroyNotify) gst_mpd_subset_node_free);
  g_list_free_full (self->BaseURLs, (GDestroyNotify) gst_mpd_baseurl_node_free);
  if (self->xlink_href)
    xmlFree (self->xlink_href);

  G_OBJECT_CLASS (gst_mpd_period_node_parent_class)->finalize (object);
}

static void
gst_mpd_period_node_class_init (GstMPDPeriodNodeClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = gst_mpd_period_node_finalize;
}

static void
gst_mpd_period_node_init (GstMPDPeriodNode * self)
{
  self->id = NULL;
  self->start = 0;              /* [ms] */
  self->duration = 0;           /* [ms] */
  self->bitstreamSwitching = 0;
  self->SegmentBase = NULL;
  self->SegmentList = NULL;
  self->SegmentTemplate = NULL;
  self->AdaptationSets = NULL;
  self->Subsets = NULL;
  self->BaseURLs = NULL;
  self->xlink_href = NULL;
  self->actuate = 0;
}

GstMPDPeriodNode *
gst_mpd_period_node_new (void)
{
  return g_object_new (GST_TYPE_MPD_PERIOD_NODE, NULL);
}

void
gst_mpd_period_node_free (GstMPDPeriodNode * self)
{
  if (self)
    gst_object_unref (self);
}
