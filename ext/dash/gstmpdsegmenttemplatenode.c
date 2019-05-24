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
#include "gstmpdsegmenttemplatenode.h"
#include "gstmpdparser.h"

G_DEFINE_TYPE (GstMPDSegmentTemplateNode, gst_mpd_segment_template_node,
    GST_TYPE_OBJECT);

/* GObject VMethods */

static void
gst_mpd_segment_template_node_finalize (GObject * object)
{
  GstMPDSegmentTemplateNode *self = GST_MPD_SEGMENT_TEMPLATE_NODE (object);

  if (self->media)
    xmlFree (self->media);
  if (self->index)
    xmlFree (self->index);
  if (self->initialization)
    xmlFree (self->initialization);
  if (self->bitstreamSwitching)
    xmlFree (self->bitstreamSwitching);
  /* MultipleSegmentBaseType extension */
  gst_mpd_helper_mult_seg_base_type_free (self->MultSegBaseType);

  G_OBJECT_CLASS (gst_mpd_segment_template_node_parent_class)->finalize
      (object);
}

static void
gst_mpd_segment_template_node_class_init (GstMPDSegmentTemplateNodeClass *
    klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = gst_mpd_segment_template_node_finalize;
}

static void
gst_mpd_segment_template_node_init (GstMPDSegmentTemplateNode * self)
{
  self->MultSegBaseType = NULL;
  self->media = NULL;
  self->index = NULL;
  self->initialization = NULL;
  self->bitstreamSwitching = NULL;
}

GstMPDSegmentTemplateNode *
gst_mpd_segment_template_node_new (void)
{
  return g_object_new (GST_TYPE_MPD_SEGMENT_TEMPLATE_NODE, NULL);
}

void
gst_mpd_segment_template_node_free (GstMPDSegmentTemplateNode * self)
{
  if (self)
    gst_object_unref (self);
}
