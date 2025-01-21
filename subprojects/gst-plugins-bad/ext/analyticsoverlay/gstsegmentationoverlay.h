/* GStreamer segmentation overlay
 * Copyright (C) <2024> Collabora Ltd.
 *  @author: Daniel Morin <daniel.morin@collabora.com>
 *
 * gstsegmentationoverlay.h
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

#ifndef __GST_SEGMENTATION_OVERLAY_H__
#define __GST_SEGMENTATION_OVERLAY_H__

#include <gst/video/gstvideofilter.h>

G_BEGIN_DECLS

#define GST_TYPE_SEGMENTATION_OVERLAY \
  (gst_segmentation_overlay_get_type())

G_DECLARE_FINAL_TYPE (GstSegmentationOverlay, gst_segmentation_overlay,
    GST, SEGMENTATION_OVERLAY, GstVideoFilter)

GST_ELEMENT_REGISTER_DECLARE (segmentationoverlay);

G_END_DECLS
#endif /* __GST_SEGMENTATION_OVERLAY_H__ */
