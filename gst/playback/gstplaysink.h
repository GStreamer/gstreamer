/* GStreamer
 * Copyright (C) <2007> Wim Taymans <wim.taymans@gmail.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
 
#ifndef __GST_PLAY_SINK_H__
#define __GST_PLAY_SINK_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_PLAY_SINK \
  (gst_play_sink_get_type())
#define GST_PLAY_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_PLAY_SINK, GstPlaySink))
#define GST_PLAY_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_PLAY_SINK, GstPlaySinkClass))
#define GST_IS_PLAY_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_PLAY_SINK))
#define GST_IS_PLAY_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_PLAY_SINK))

/**
 * GstPlaySinkMode:
 * @GST_PLAY_SINK_MODE_VIDEO:
 * @GST_PLAY_SINK_MODE_AUDIO:
 * @GST_PLAY_SINK_MODE_TEXT:
 * @GST_PLAY_SINK_MODE_VIS:
 *
 * Features to enable in the sink.
 */
typedef enum {
  GST_PLAY_SINK_MODE_VIDEO       = (1 << 0),
  GST_PLAY_SINK_MODE_AUDIO       = (1 << 1),
  GST_PLAY_SINK_MODE_TEXT        = (1 << 2),
  GST_PLAY_SINK_MODE_VIS         = (1 << 3)
} GstPlaySinkMode;

/**
 * GstPlaySinkType:
 * @GST_PLAY_SINK_TYPE_AUDIO: A non-raw audio pad
 * @GST_PLAY_SINK_TYPE_AUDIO_RAW: a raw audio pad
 * @GST_PLAY_SINK_TYPE_VIDEO: a non-raw video pad
 * @GST_PLAY_SINK_TYPE_VIDEO_RAW: a raw video pad
 * @GST_PLAY_SINK_TYPE_TEXT: a raw text pad
 * @GST_PLAY_SINK_TYPE_LAST: the last type
 *
 * Types of pads that can be requested from the sinks.
 */
typedef enum {
  GST_PLAY_SINK_TYPE_AUDIO     = 0,
  GST_PLAY_SINK_TYPE_AUDIO_RAW = 1,
  GST_PLAY_SINK_TYPE_VIDEO     = 2,
  GST_PLAY_SINK_TYPE_VIDEO_RAW = 3,
  GST_PLAY_SINK_TYPE_TEXT      = 4,
  GST_PLAY_SINK_TYPE_LAST      = 5
} GstPlaySinkType;

typedef struct _GstPlaySink GstPlaySink;
typedef struct _GstPlaySinkClass GstPlaySinkClass;

GType gst_play_sink_get_type (void);

GstPad *         gst_play_sink_request_pad    (GstPlaySink *playsink, GstPlaySinkType type);
void             gst_play_sink_release_pad    (GstPlaySink *playsink, GstPad *pad);

void             gst_play_sink_set_video_sink (GstPlaySink * play_sink, GstElement * sink);
void             gst_play_sink_set_audio_sink (GstPlaySink * play_sink, GstElement * sink);
void             gst_play_sink_set_vis_plugin (GstPlaySink * play_sink, GstElement * vis);

GstPlaySinkMode  gst_play_sink_get_mode       (GstPlaySink *playsink);
gboolean         gst_play_sink_set_mode       (GstPlaySink *playsink, GstPlaySinkMode mode);

G_END_DECLS

#endif /* __GST_PLAY_SINK_H__ */
