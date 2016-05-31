/*
 * GStreamer
 * Copyright (C) 2016 Vivia Nikolaidou <vivia@toolsonair.com>
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

#ifndef __GST_TIMECODEWAIT_H__
#define __GST_TIMECODEWAIT_H__

#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/video/video.h>

G_BEGIN_DECLS
#define GST_TYPE_TIMECODEWAIT                    (gst_timecodewait_get_type())
#define GST_TIMECODEWAIT(obj)                    (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TIMECODEWAIT,GstTimeCodeWait))
#define GST_IS_TIMECODEWAIT(obj)                 (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TIMECODEWAIT))
#define GST_TIMECODEWAIT_CLASS(klass)            (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_TIMECODEWAIT,GstTimeCodeWaitClass))
#define GST_IS_TIMECODEWAIT_CLASS(klass)         (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_TIMECODEWAIT))
#define GST_TIMECODEWAIT_GET_CLASS(obj)          (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_TIMECODEWAIT,GstTimeCodeWaitClass))
typedef struct _GstTimeCodeWait GstTimeCodeWait;
typedef struct _GstTimeCodeWaitClass GstTimeCodeWaitClass;

struct _GstTimeCodeWait
{
  GstElement parent;

  GstVideoTimeCode *tc;
  gboolean from_string;

  GstPad *asrcpad, *asinkpad, *vsrcpad, *vsinkpad;

  GstAudioInfo ainfo;
  GstVideoInfo vinfo;

  GstSegment asegment, vsegment;

  GstClockTime running_time_of_timecode;

  gboolean video_eos_flag;
  gboolean audio_flush_flag;
  gboolean shutdown_flag;

  GCond cond;
  GMutex mutex;
};

struct _GstTimeCodeWaitClass
{
  GstElementClass parent_class;
};

GType gst_timecodewait_get_type (void);

G_END_DECLS
#endif /* __GST_TIMECODEWAIT_H__ */
