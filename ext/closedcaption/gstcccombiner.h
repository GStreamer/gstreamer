/*
 * GStreamer
 * Copyright (C) 2018 Sebastian Dröge <sebastian@centricular.com>
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

#ifndef __GST_CCCOMBINER_H__
#define __GST_CCCOMBINER_H__

#include <gst/gst.h>
#include <gst/base/base.h>
#include <gst/video/video.h>

G_BEGIN_DECLS
#define GST_TYPE_CCCOMBINER \
  (gst_cc_combiner_get_type())
#define GST_CCCOMBINER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CCCOMBINER,GstCCCombiner))
#define GST_CCCOMBINER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CCCOMBINER,GstCCCombinerClass))
#define GST_IS_CCCOMBINER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CCCOMBINER))
#define GST_IS_CCCOMBINER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CCCOMBINER))

typedef struct _GstCCCombiner GstCCCombiner;
typedef struct _GstCCCombinerClass GstCCCombinerClass;

struct _GstCCCombiner
{
  GstAggregator parent;

  gint video_fps_n, video_fps_d;
  GstClockTime current_video_running_time;
  GstClockTime current_video_running_time_end;
  GstBuffer *current_video_buffer;

  GArray *current_frame_captions;
  GstVideoCaptionType current_caption_type;
};

struct _GstCCCombinerClass
{
  GstAggregatorClass parent_class;
};

GType gst_cc_combiner_get_type (void);

G_END_DECLS
#endif /* __GST_CCCOMBINER_H__ */
