/* GStreamer
 *
 * Copyright (C) 2011 David Schleef <ds@schleef.org>
 * Copyright (C) 2014 Sebastian Dröge <sebastian@centricular.com>
 * Copyright (C) 2015 Florian Langlois <florian.langlois@fr.thalesgroup.com>
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

#ifndef __GST_DECKLINK_VIDEO_SRC_H__
#define __GST_DECKLINK_VIDEO_SRC_H__

#include <gst/gst.h>
#include <gst/base/base.h>
#include <gst/video/video.h>
#include "gstdecklink.h"

G_BEGIN_DECLS

#define GST_TYPE_DECKLINK_VIDEO_SRC \
  (gst_decklink_video_src_get_type())
#define GST_DECKLINK_VIDEO_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_DECKLINK_VIDEO_SRC, GstDecklinkVideoSrc))
#define GST_DECKLINK_VIDEO_SRC_CAST(obj) \
  ((GstDecklinkVideoSrc*)obj)
#define GST_DECKLINK_VIDEO_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_DECKLINK_VIDEO_SRC, GstDecklinkVideoSrcClass))
#define GST_IS_DECKLINK_VIDEO_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_DECKLINK_VIDEO_SRC))
#define GST_IS_DECKLINK_VIDEO_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_DECKLINK_VIDEO_SRC))

typedef struct _GstDecklinkVideoSrc GstDecklinkVideoSrc;
typedef struct _GstDecklinkVideoSrcClass GstDecklinkVideoSrcClass;

struct _GstDecklinkVideoSrc
{
  GstPushSrc parent;

  GstDecklinkModeEnum mode;
  GstDecklinkModeEnum caps_mode;
  BMDPixelFormat caps_format;
  GstDecklinkConnectionEnum connection;
  gint device_number;

  GstVideoInfo info;
  GstDecklinkVideoFormat video_format;
  BMDTimecodeFormat timecode_format;

  GstDecklinkInput *input;

  GCond cond;
  GMutex lock;
  gboolean flushing;
  GQueue current_frames;

  guint buffer_size;

  GstClockTime internal_base_time;
  GstClockTime external_base_time;
};

struct _GstDecklinkVideoSrcClass
{
  GstPushSrcClass parent_class;
};

GType gst_decklink_video_src_get_type (void);
void gst_decklink_video_src_convert_to_external_clock (GstDecklinkVideoSrc * self,
    GstClockTime * timestamp, GstClockTime * duration);

G_END_DECLS

#endif /* __GST_DECKLINK_VIDEO_SRC_H__ */
