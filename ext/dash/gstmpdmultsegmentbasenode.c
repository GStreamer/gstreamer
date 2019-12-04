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
#include "gstmpdmultsegmentbasenode.h"
#include "gstmpdparser.h"

G_DEFINE_TYPE (GstMPDMultSegmentBaseNode, gst_mpd_mult_segment_base_node,
    GST_TYPE_MPD_NODE);

/* GObject VMethods */

static void
gst_mpd_mult_segment_base_node_finalize (GObject * object)
{
  GstMPDMultSegmentBaseNode *self = GST_MPD_MULT_SEGMENT_BASE_NODE (object);

  gst_mpd_segment_base_node_free (self->SegmentBase);
  gst_mpd_segment_timeline_node_free (self->SegmentTimeline);
  gst_mpd_url_type_node_free (self->BitstreamSwitching);

  G_OBJECT_CLASS (gst_mpd_mult_segment_base_node_parent_class)->finalize
      (object);
}

/* Base class */

static void
gst_mpd_mult_segment_base_get_xml_node (GstMPDNode * node,
    xmlNodePtr mult_segment_base_node)
{
  GstMPDMultSegmentBaseNode *self = GST_MPD_MULT_SEGMENT_BASE_NODE (node);

  if (self->duration)
    gst_xml_helper_set_prop_uint (mult_segment_base_node, "duration",
        self->duration);
  if (self->startNumber)
    gst_xml_helper_set_prop_uint (mult_segment_base_node, "startNumber",
        self->startNumber);
  if (self->SegmentBase)
    gst_mpd_node_add_child_node (GST_MPD_NODE (self->SegmentBase),
        mult_segment_base_node);
  if (self->SegmentTimeline)
    gst_mpd_node_add_child_node (GST_MPD_NODE (self->SegmentTimeline),
        mult_segment_base_node);
  if (self->BitstreamSwitching)
    gst_mpd_node_add_child_node (GST_MPD_NODE (self->BitstreamSwitching),
        mult_segment_base_node);
}

static void
gst_mpd_mult_segment_base_node_class_init (GstMPDMultSegmentBaseNodeClass *
    klass)
{
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gst_mpd_mult_segment_base_node_finalize;
}

static void
gst_mpd_mult_segment_base_node_init (GstMPDMultSegmentBaseNode * self)
{
  self->duration = 0;
  self->startNumber = 0;
  self->SegmentBase = NULL;
  self->SegmentTimeline = NULL;
  self->BitstreamSwitching = NULL;
}

void
gst_mpd_mult_segment_base_node_add_child_node (GstMPDNode * node,
    xmlNodePtr parent_xml_node)
{
  if (node) {
    xmlNodePtr new_xml_node = gst_mpd_node_get_xml_pointer (node);
    gst_mpd_mult_segment_base_get_xml_node (node, new_xml_node);
    xmlAddChild (parent_xml_node, new_xml_node);
  }
}
