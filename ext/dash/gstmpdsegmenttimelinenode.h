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
#ifndef __GSTMPDSEGMENTTIMELINENODE_H__
#define __GSTMPDSEGMENTTIMELINENODE_H__

#include <gst/gst.h>
#include "gstxmlhelper.h"

G_BEGIN_DECLS

#define GST_TYPE_MPD_SEGMENT_TIMELINE_NODE gst_mpd_segment_timeline_node_get_type ()
#define GST_MPD_SEGMENT_TIMELINE_NODE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MPD_SEGMENT_TIMELINE_NODE, GstMPDSegmentTimelineNode))
#define GST_MPD_SEGMENT_TIMELINE_NODE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MPD_SEGMENT_TIMELINE_NODE, GstMPDSegmentTimelineNodeClass))
#define GST_IS_MPD_SEGMENT_TIMELINE_NODE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MPD_SEGMENT_TIMELINE_NODE))
#define GST_IS_MPD_SEGMENT_TIMELINE_NODE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MPD_SEGMENT_TIMELINE_NODE))
#define GST_MPD_SEGMENT_TIMELINE_NODE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_MPD_SEGMENT_TIMELINE_NODE, GstMPDSegmentTimelineNodeClass))

typedef struct _GstMPDSegmentTimelineNode                GstMPDSegmentTimelineNode;
typedef struct _GstMPDSegmentTimelineNodeClass           GstMPDSegmentTimelineNodeClass;


struct _GstMPDSegmentTimelineNode
{
  GstObject parent_instance;
  /* list of S nodes */
  GQueue S;
};

struct _GstMPDSegmentTimelineNodeClass {
  GstObjectClass parent_class;
};


G_GNUC_INTERNAL GType gst_mpd_segment_timeline_node_get_type (void);

GstMPDSegmentTimelineNode * gst_mpd_segment_timeline_node_new (void);
void gst_mpd_segment_timeline_node_free (GstMPDSegmentTimelineNode* self);

GstMPDSegmentTimelineNode *gst_mpd_segment_timeline_node_clone (GstMPDSegmentTimelineNode * pointer);

G_END_DECLS

#endif /* __GSTMPDSEGMENTTIMELINENODE_H__ */
