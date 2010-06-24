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

#ifndef _SAT_VIDEO_FRAME_H_
#define _SAT_VIDEO_FRAME_H_

#include <gst/gst.h>

#define SAT_TYPE_VIDEO_FRAME      (sat_video_frame_get_type())
#define GST_IS_VIDEO_FRAME(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SAT_TYPE_VIDEO_FRAME))
#define SAT_VIDEO_FRAME(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), SAT_TYPE_VIDEO_FRAME, SatVideoFrame))
#define SAT_VIDEO_FRAME_CAST(obj) ((SatVideoFrame *)obj)

/**
 * SatVideoFrameFlag:
 * @SAT_VIDEO_FRAME_FLAG_PREROLL: the frame is part of a preroll and should not be
 * displayed.
 * @SAT_VIDEO_FRAME_FLAG_DISCONT: the frame marks a discontinuity in the stream.
 * This typically occurs after a seek or a dropped buffer from a live or
 * network source.
 * @SAT_VIDEO_FRAME_FLAG_GAP: the frame has been created to fill a gap in the
 * stream and contains media neutral data (elements can switch to optimized code
 * path that ignores the buffer content).
 * @SAT_VIDEO_FRAME_FLAG_DELTA_UNIT: the frame is a keyframe.
 * @SAT_VIDEO_FRAME_FLAG_SYNC_POINT: the frame marks a sync point.
 * @SAT_VIDEO_FRAME_FLAG_EOS: the frame is the last in the stream.
 * @SAT_VIDEO_FRAME_FLAG_TFF: If the frame is interlaced, then the first
 * field in the video frame is the top field. If unset, the bottom field is first.
 * @SAT_VIDEO_FRAME_FLAG_LAST: additional flags can be added starting from this flag.
 * A set of frame flags used to describe properties of a #SatVideoFrame.
 */
typedef enum
{
  SAT_VIDEO_FRAME_FLAG_PREROLL = (GST_MINI_OBJECT_FLAG_LAST << 0),
  SAT_VIDEO_FRAME_FLAG_DISCONT = (GST_MINI_OBJECT_FLAG_LAST << 1),
  SAT_VIDEO_FRAME_FLAG_GAP = (GST_MINI_OBJECT_FLAG_LAST << 2),
  SAT_VIDEO_FRAME_FLAG_KEYFRAME = (GST_MINI_OBJECT_FLAG_LAST << 3),
  SAT_VIDEO_FRAME_FLAG_SYNC_POINT = (GST_MINI_OBJECT_FLAG_LAST << 4),
  SAT_VIDEO_FRAME_FLAG_EOS = (GST_MINI_OBJECT_FLAG_LAST << 5),
  SAT_VIDEO_FRAME_FLAG_TFF = (GST_MINI_OBJECT_FLAG_LAST << 6),
  SAT_VIDEO_FRAME_FLAG_LAST = (GST_MINI_OBJECT_FLAG_LAST << 7)
} SatVideoFrameFlag;

typedef struct _SatVideoFrame SatVideoFrame;
typedef struct _SatVideoFrameClass SatVideoFrameClass;

struct _SatVideoFrame
{
  GstMiniObject mini_object;

  GstClockTime decode_timestamp;
  GstClockTime presentation_timestamp;
  GstClockTime presentation_duration;

  gint system_frame_number;
  gint decode_frame_number;
  gint presentation_frame_number;

  gint distance_from_sync;

	GstBuffer *sink_buffer;
  GstBuffer *src_buffer;
  
  gint field_index;
  gint n_fields;
  
};

struct _SatVideoFrameClass
{
  GstMiniObjectClass mini_object_class;
};

/* refcounting */
/**
 * sat_video_frame_ref:
 * @frame: a #SatVideoFrame.
 *
 * Increases the refcount of the given frame by one.
 *
 * Returns: @frame
 */
#ifdef _FOOL_GTK_DOC_
G_INLINE_FUNC SatVideoFrame * gst_buffer_ref (SatVideoFrame * frame);
#endif

static inline SatVideoFrame *
sat_video_frame_ref (SatVideoFrame *frame)
{
  return (SatVideoFrame *) gst_mini_object_ref (GST_MINI_OBJECT_CAST (frame));
}

/**
 * sat_video_frame_unref:
 * @frame: a #SatVideoFrame.
 *
 * Decreases the refcount of the frame. If the refcount reaches 0, the frame
 * will be freed.
 */
#ifdef _FOOL_GTK_DOC_
G_INLINE_FUNC void sat_video_frame_unref (SatVideoFrame * frame);
#endif

static inline void
sat_video_frame_unref (SatVideoFrame * frame)
{
  gst_mini_object_unref (GST_MINI_OBJECT_CAST (frame));
}

/**
 * SAT_VIDEO_FRAME_FLAG_IS_SET:
 * @buf: a #SatVideoFrame.
 * @flag: the #SatVideoFrameFlag to check.
 *
 * Gives the status of a specific flag on a video frame.
 */
#define SAT_VIDEO_FRAME_FLAG_IS_SET(frame,flag)        GST_MINI_OBJECT_FLAG_IS_SET (frame, flag)
/**
 * SAT_VIDEO_FRAME_FLAG_SET:
 * @buf: a #SatVideoFrame.
 * @flag: the #SatVideoFrameFlag to set.
 *
 * Sets a frame flag on a video frame.
 */
#define SAT_VIDEO_FRAME_FLAG_SET(frame,flag)           GST_MINI_OBJECT_FLAG_SET (frame, flag)
/**
 * SAT_VIDEO_FRAME_FLAG_UNSET:
 * @buf: a #SatVideoFrame.
 * @flag: the #SatVideoFrameFlag to clear.
 *
 * Clears a frame flag.
 */
#define SAT_VIDEO_FRAME_FLAG_UNSET(frame,flag)         GST_MINI_OBJECT_FLAG_UNSET (frame, flag)

SatVideoFrame *sat_video_frame_new (void);

GType sat_video_frame_get_type (void);

#endif