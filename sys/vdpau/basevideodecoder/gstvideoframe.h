/*
* GStreamer
* Copyright (C) 2009 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>
*
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Library General Public
* License as published by the Free Software Foundation; either
* version 2 of the License, or (at your option) any later version.
*
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
* Library General Public License for more details.
*
* You should have received a copy of the GNU Library General Public
* License along with this library; if not, write to the
* Free Software Foundation, Inc., 59 Temple Place - Suite 330,
* Boston, MA 02111-1307, USA.
*/

#ifndef _GST_VIDEO_FRAME_H_
#define _GST_VIDEO_FRAME_H_

#include <gst/gst.h>

#define GST_TYPE_VIDEO_FRAME      (gst_video_frame_get_type())
#define GST_IS_VIDEO_FRAME(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VIDEO_FRAME))
#define GST_VIDEO_FRAME(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VIDEO_FRAME, GstVideoFrame))
#define GST_VIDEO_FRAME_CAST(obj) ((GstVideoFrame *)obj)

/**
 * GstVideoFrameFlag:
 * @GST_VIDEO_FRAME_FLAG_PREROLL: the frame is part of a preroll and should not be
 * displayed.
 * @GST_VIDEO_FRAME_FLAG_DISCONT: the frame marks a discontinuity in the stream.
 * This typically occurs after a seek or a dropped buffer from a live or
 * network source.
 * @GST_VIDEO_FRAME_FLAG_GAP: the frame has been created to fill a gap in the
 * stream and contains media neutral data (elements can switch to optimized code
 * path that ignores the buffer content).
 * @GST_VIDEO_FRAME_FLAG_DELTA_UNIT: the frame is a keyframe.
 * @GST_VIDEO_FRAME_FLAG_SYNC_POINT: the frame marks a sync point.
 * @GST_VIDEO_FRAME_FLAG_EOS: the frame is the last in the stream.
 * @GST_VIDEO_FRAME_FLAG_TFF: If the frame is interlaced, then the first
 * field in the video frame is the top field. If unset, the bottom field is first.
 * @GST_VIDEO_FRAME_FLAG_LAST: additional flags can be added starting from this flag.
 * A set of frame flags used to describe properties of a #GstVideoFrame.
 */
typedef enum
{
  GST_VIDEO_FRAME_FLAG_PREROLL = (GST_MINI_OBJECT_FLAG_LAST << 0),
  GST_VIDEO_FRAME_FLAG_DISCONT = (GST_MINI_OBJECT_FLAG_LAST << 1),
  GST_VIDEO_FRAME_FLAG_GAP = (GST_MINI_OBJECT_FLAG_LAST << 2),
  GST_VIDEO_FRAME_FLAG_KEYFRAME = (GST_MINI_OBJECT_FLAG_LAST << 3),
  GST_VIDEO_FRAME_FLAG_SYNC_POINT = (GST_MINI_OBJECT_FLAG_LAST << 4),
  GST_VIDEO_FRAME_FLAG_EOS = (GST_MINI_OBJECT_FLAG_LAST << 5),
  GST_VIDEO_FRAME_FLAG_TFF = (GST_MINI_OBJECT_FLAG_LAST << 6),
  GST_VIDEO_FRAME_FLAG_LAST = (GST_MINI_OBJECT_FLAG_LAST << 7)
} GstVideoFrameFlag;

typedef struct _GstVideoFrame GstVideoFrame;
typedef struct _GstVideoFrameClass GstVideoFrameClass;

struct _GstVideoFrame
{
  GstMiniObject mini_object;

  GstClockTime upstream_timestamp;
  GstClockTime upstream_duration;

  GstClockTime parsed_timestamp;

  guint n_fields;

	GstBuffer *sink_buffer;
  GstBuffer *src_buffer;
};

struct _GstVideoFrameClass
{
  GstMiniObjectClass mini_object_class;
};

/* refcounting */
/**
 * gst_video_frame_ref:
 * @frame: a #GstVideoFrame.
 *
 * Increases the refcount of the given frame by one.
 *
 * Returns: @frame
 */
#ifdef _FOOL_GTK_DOC_
G_INLINE_FUNC GstVideoFrame * gst_buffer_ref (GstVideoFrame * frame);
#endif

static inline GstVideoFrame *
gst_video_frame_ref (GstVideoFrame *frame)
{
  return (GstVideoFrame *) gst_mini_object_ref (GST_MINI_OBJECT_CAST (frame));
}

/**
 * gst_video_frame_unref:
 * @frame: a #GstVideoFrame.
 *
 * Decreases the refcount of the frame. If the refcount reaches 0, the frame
 * will be freed.
 */
#ifdef _FOOL_GTK_DOC_
G_INLINE_FUNC void gst_video_frame_unref (GstVideoFrame * frame);
#endif

static inline void
gst_video_frame_unref (GstVideoFrame * frame)
{
  gst_mini_object_unref (GST_MINI_OBJECT_CAST (frame));
}

/**
 * GST_VIDEO_FRAME_FLAG_IS_SET:
 * @buf: a #GstVideoFrame.
 * @flag: the #GstVideoFrameFlag to check.
 *
 * Gives the status of a specific flag on a video frame.
 */
#define GST_VIDEO_FRAME_FLAG_IS_SET(frame,flag)        GST_MINI_OBJECT_FLAG_IS_SET (frame, flag)
/**
 * GST_VIDEO_FRAME_FLAG_SET:
 * @buf: a #GstVideoFrame.
 * @flag: the #GstVideoFrameFlag to set.
 *
 * Sets a frame flag on a video frame.
 */
#define GST_VIDEO_FRAME_FLAG_SET(frame,flag)           GST_MINI_OBJECT_FLAG_SET (frame, flag)
/**
 * GST_VIDEO_FRAME_FLAG_UNSET:
 * @buf: a #GstVideoFrame.
 * @flag: the #GstVideoFrameFlag to clear.
 *
 * Clears a frame flag.
 */
#define GST_VIDEO_FRAME_FLAG_UNSET(frame,flag)         GST_MINI_OBJECT_FLAG_UNSET (frame, flag)

GstVideoFrame *gst_video_frame_new (void);

GType gst_video_frame_get_type (void);

#endif