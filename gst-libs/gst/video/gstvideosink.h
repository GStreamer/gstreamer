/*
 *  GStreamer Video sink.
 *
 *  Copyright (C) <2003> Julien Moutte <julien@moutte.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
 
#ifndef __GST_VIDEOSINK_H__
#define __GST_VIDEOSINK_H__

#include <gst/gst.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
  
#define GST_TYPE_VIDEOSINK (gst_videosink_get_type())
#define GST_VIDEOSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VIDEOSINK, GstVideoSink))
#define GST_VIDEOSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VIDEOSINK, GstVideoSink))
#define GST_IS_VIDEOSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VIDEOSINK))
#define GST_IS_VIDEOSINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VIDEOSINK))
#define GST_VIDEOSINK_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VIDEOSINK, GstVideoSinkClass))
  
#define GST_VIDEOSINK_PAD(obj) (GST_VIDEOSINK (obj)->sinkpad)
#define GST_VIDEOSINK_WIDTH(obj) (GST_VIDEOSINK (obj)->width)
#define GST_VIDEOSINK_HEIGHT(obj) (GST_VIDEOSINK (obj)->height)
#define GST_VIDEOSINK_CLOCK(obj) (GST_VIDEOSINK (obj)->clock)
  
typedef struct _GstVideoSink GstVideoSink;
typedef struct _GstVideoSinkClass GstVideoSinkClass;

struct _GstVideoSink {
  GstElement element;
  
  GstPad *sinkpad;
  
  gpointer video_out;
  
  gint width, height;
  gint frames_displayed;
  guint64 frame_time;
  
  GstClock *clock;
  
  GstCaps *formats;
};

struct _GstVideoSinkClass {
  GstElementClass parent_class;
  
  /* public virtual methods */
  void (*set_video_out) (GstVideoSink *videosink, gpointer video_out);
  void (*push_ui_event) (GstVideoSink *videosink, GstEvent *event);
  void (*set_geometry)  (GstVideoSink *videosink, gint width, gint height);
  
  /* signals */
  void (*have_video_out)  (GstVideoSink *element, gpointer video_out);
  void (*have_size)       (GstVideoSink *element, gint width, gint height);
  void (*frame_displayed) (GstVideoSink *element);
};

GType gst_videosink_get_type (void);

/* public virtual methods */
void gst_video_sink_set_video_out (GstVideoSink *videosink, gpointer video_out);
void gst_video_sink_push_ui_event (GstVideoSink *videosink, GstEvent *event);
void gst_video_sink_set_geometry  (GstVideoSink *videosink, gint width,
                                   gint height);

/* public methods to fire signals */
void gst_video_sink_got_video_out (GstVideoSink *videosink, gpointer video_out);
void gst_video_sink_got_video_size (GstVideoSink *videosink,
                                    gint width, gint height);
void gst_video_sink_frame_displayed (GstVideoSink *videosink);

/* public methods */
void gst_video_sink_get_geometry (GstVideoSink *videosink,
                                  gint *width, gint *height);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* __GST_VIDEOSINK_H__ */
