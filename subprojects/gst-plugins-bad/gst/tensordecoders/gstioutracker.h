/*
 * GStreamer gstreamer-ioutracker
 * Copyright (C) 2025 Collabora Ltd
 *
 * gstioutracker.h
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

#ifndef __GST_IOU_TRACKER_H__
#define __GST_IOU_TRACKER_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>

G_BEGIN_DECLS

#define GST_TYPE_IOU_TRACKER            (gst_iou_tracker_get_type())
G_DECLARE_FINAL_TYPE (GstIouTracker, gst_iou_tracker, GST, IOU_TRACKER, GstBaseTransform)

/*
 * GstIouTracker:
 *
 * @basetransform: the parent class
 * @min_frame_count_for_lost_track: Minimum no of consecutive frame where object is absent, before track is marked lost
 * @iou_score_threshold: the threshold for Intersection over Union (IoU) score to consider a detection as a match
 * @tracks: a list of current tracks being tracked
 * @picked_odmtds: a hash table to keep track of picked object detection metadata
 * @next_track_id: the next tracking id to assign to a new track
 *
 * Since: 1.28
 */
struct _GstIouTracker
{
  GstBaseTransform basetransform;
  guint min_frame_count_for_lost_track;
  gfloat iou_score_threshold;
  GList *tracks;
  GHashTable *picked_odmtds;
  guint next_track_id; // Next tracking id to assign
};

/**
 * GstIouTrackerClass:
 *
 * @parent_class base transform base class
 *
 * Since: 1.28
 */
struct _GstIouTrackerClass
{
  GstBaseTransformClass parent_class;
};

GST_ELEMENT_REGISTER_DECLARE (iou_tracker);

G_END_DECLS

#endif /* __GST_IOU_TRACKER_H__ */
