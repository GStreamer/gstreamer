/* GStreamer
 * Copyright (C) 2003 Julien Moutte <julien@moutte.net>
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
 
#ifndef __GST_PLAY_H__
#define __GST_PLAY_H__

#include <gst/gstpipeline.h>

#define GST_TYPE_PLAY            (gst_play_get_type())
#define GST_PLAY(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_PLAY, GstPlay))
#define GST_PLAY_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_PLAY, GstPlayClass))
#define GST_IS_PLAY(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_PLAY))
#define GST_IS_PLAY_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_PLAY))
#define GST_PLAY_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GTK_TYPE_PLAY, GstPlayClass))

typedef enum
{
  GST_PLAY_SINK_TYPE_AUDIO,
  GST_PLAY_SINK_TYPE_VIDEO,
  GST_PLAY_SINK_TYPE_ANY,
} GstPlaySinkType;

typedef struct _GstPlay GstPlay;
typedef struct _GstPlayClass GstPlayClass;
typedef struct _GstPlayPrivate GstPlayPrivate;
  
struct _GstPlay
{
  GstPipeline pipeline;
  
  GstPlayPrivate *priv;
  
  gpointer _gst_reserved[GST_PADDING];
};
  
struct _GstPlayClass
{
  GstPipelineClass parent_class;
  
  void (*time_tick)       (GstPlay *play, gint64 time_nanos);
  void (*stream_length)   (GstPlay *play, gint64 length_nanos);
  void (*have_video_size) (GstPlay *play, gint width, gint height);
  
  gpointer _gst_reserved[GST_PADDING];
};

GType                 gst_play_get_type              (void);
GstPlay *             gst_play_new                   (void);

gboolean              gst_play_set_data_src          (GstPlay *play,
                                                      GstElement *data_src);
gboolean              gst_play_set_video_sink        (GstPlay *play,
                                                      GstElement *video_sink);
gboolean              gst_play_set_audio_sink        (GstPlay *play,
                                                      GstElement *audio_sink);

gboolean              gst_play_set_visualization     (GstPlay *play,
                                                      GstElement *element);
gboolean              gst_play_connect_visualization (GstPlay *play,
                                                      gboolean connect);

gboolean              gst_play_set_location          (GstPlay *play,
                                                      const char *location);
char *                gst_play_get_location          (GstPlay *play);

gboolean              gst_play_seek_to_time          (GstPlay *play,
                                                      gint64 time_nanos);
                                                      
GstElement *          gst_play_get_sink_element      (GstPlay *play,
				                      GstElement *element,
				                      GstPlaySinkType sink_type);

#endif /* __GST_PLAY_H__ */
